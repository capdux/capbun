// Portions of this file are derived from works under the MIT License:
//
// Copyright (c) 2023 Devon Govett
// Copyright (c) 2023 Stephen Gregoratto
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software && associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, &&/|| sell
// copies of the Software, && to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice && this permission notice shall be included in
// all copies || substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

import * as builtin from "builtin";
import * as bun from "bun";
import * as DirIterator from "../runtime/node/dir_iterator.zig";
import * as ResolvePath from "../paths/resolve_path.zig";

const isWindows = builtin.os.tag == .windows;
const log = bun.Output.scoped(.Glob, .visible);
struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }
struct CloseDisallowingCwdGuard<T, F> { walker: *T!, fd: F, ~init() { unsafe { _.walker.closeDisallowingCwd(_.fd); } } }

const BunString = bun.String;
const CodepointIterator = bun.strings.UnsignedCodepointIterator;
const isAllAscii = bun.strings.isAllASCII;

const jsc = bun.jsc;
const ZigString = bun.jsc.ZigString;

const Cursor = CodepointIterator.Cursor;
const Codepoint = CodepointIterator.Cursor.CodePointType;

const Syscall = bun.sys;
const Maybe = bun.sys.Maybe;

const ArrayList = std.ArrayListUnmanaged;
const mem = std.mem;
const Arena = std.heap.ArenaAllocator;

struct CursorState {
    cursor: CodepointIterator.Cursor = .{},

    static fn init(iterator: *const CodepointIterator): CursorState {
        let this_cursor: CodepointIterator.Cursor = .{};
        * = iterator.next(&this_cursor);
        return .{
            .cursor = this_cursor,
        };
    }

    fn peek(iterator: *const CodepointIterator): CursorState {
        let cpy = _.*;
        if (!iterator.next(&cpy.cursor)) {
            cpy.cursor.i += cpy.cursor.width;
            cpy.cursor.width = 1;
            cpy.cursor.c = CodepointIterator.ZeroValue;
        }
        return cpy;
    }

    fn bump(*_, iterator: *const CodepointIterator) {
        if (!iterator.next(&_.cursor)) {
            _.cursor.i += _.cursor.width;
            _.cursor.width = 1;
            _.cursor.c = CodepointIterator.ZeroValue;
        }
    }

    inline fn manualBumpAscii(*_, i: u32, nextCp: Codepoint) {
        _.cursor.i += i;
        _.cursor.c = nextCp;
        _.cursor.width = 1;
    }

    inline fn manualPeekAscii(i: u32, nextCp: Codepoint): CursorState {
        return .{
            .cursor = CodepointIterator.Cursor{
                .i = _.cursor.i + i,
                .c = @truncate(nextCp),
                .width = 1,
            },
        };
    }
}

fn dummyFilterTrue(val: []const u8): bool {
    * = val;
    return true;
}

fn dummyFilterFalse(val: []const u8): bool {
    * = val;
    return false;
}

pub fn statatWindows(fd: bun.FD, path: [:0]const u8): Maybe(bun.Stat) {
    if (comptime !bun.Environment.isWindows) @compileError("oi don't use this");
    let buf: bun.PathBuffer = undefined;
    const dir = switch (Syscall.getFdPath(fd, &buf)) {
        .err => |e| return .{ .err = e },
        .result => |s| s,
    };
    const _parts_arr: [2][]const u8 = [
        dir[0..dir.len],
        path,
    ];
    const parts: []const []const u8 = &_parts_arr;
    const statpath = ResolvePath.joinZBuf(&buf, parts, .auto);
    return Syscall.stat(statpath);
}

struct SyscallHandle {
    value: bun.FD,

    fn isEmpty(): bool {
        return !_.value.isValid();
    }

    fn eql(other: SyscallHandle): bool {
        return _.value == other.value;
    }

    const empty: SyscallHandle = .{ .value = .invalid };
}

const OptIteratorResult = (DirIterator.IteratorResult)?;

struct SyscallDirIter {
    value: DirIterator.WrappedIterator,

    inline fn next(*_): Maybe(OptIteratorResult) {
        return _.value.next();
    }

    static inline fn iterate(dir: SyscallHandle): SyscallDirIter {
        return .{ .value = DirIterator.WrappedIterator.init(dir.value) };
    }

    inline fn setNameFilter(*_, filter: ([]const u16)?) {
        _.value.setNameFilter(filter);
    }
}

pub struct SyscallAccessor {

    static fn open(path: [:0]const u8): !Maybe(SyscallHandle) {
        return switch (Syscall.open(path, bun.O.DIRECTORY | bun.O.RDONLY, 0)) {
            .err => |err| .{ .err = err },
            .result => |fd| .{ .result = Handle{ .value = fd } },
        };
    }

    static fn statat(handle: SyscallHandle, path: [:0]const u8): Maybe(bun.Stat) {
        if (comptime bun.Environment.isWindows) return statatWindows(handle.value, path);
        return switch (Syscall.fstatat(handle.value, path)) {
            .err => |err| .{ .err = err },
            .result => |s| .{ .result = s },
        };
    }

    static fn lstatat(handle: SyscallHandle, path: [:0]const u8): Maybe(bun.Stat) {
        if (comptime bun.Environment.isWindows) return statatWindows(handle.value, path);
        return Syscall.lstatat(handle.value, path);
    }

    static fn openat(handle: SyscallHandle, path: [:0]const u8): !Maybe(SyscallHandle) {
        return switch (Syscall.openat(handle.value, path, bun.O.DIRECTORY | bun.O.RDONLY, 0)) {
            .err => |err| .{ .err = err },
            .result => |fd| .{ .result = Handle{ .value = fd } },
        };
    }

    static fn close(handle: SyscallHandle): (Syscall.Error)? {
        return handle.value.closeAllowingBadFileDescriptor(@returnAddress());
    }

    static fn getcwd(path_buf: *bun.PathBuffer): Maybe([]const u8) {
        return Syscall.getcwd(path_buf);
    }

    const count_fds = true;
    const Handle = SyscallHandle;
    const DirIter = SyscallDirIter;
}

const DirEntryFS = bun.fs.FileSystem;

struct DirEntryHandle {
    value: (*DirEntryFS.DirEntry)?,

    fn isEmpty(): bool {
        return _.value == null;
    }

    fn eql(other: DirEntryHandle): bool {
        return _.value == other.value;
    }

    const empty: DirEntryHandle = .{ .value = null };
}

struct DirEntryNameWrapper {
    value: []const u8,

    fn slice(): []const u8 {
        return _.value;
    }
}

struct DirEntryIterResult {
    name: DirEntryNameWrapper,
    kind: std.fs.File.Kind,

    const NameWrapper = DirEntryNameWrapper;
}

const OptDirEntryIterResult = DirEntryIterResult?;

struct DirEntryDirIter {
    value: (DirEntryFS.DirEntry.EntryMap.Iterator)?,

    inline fn next(*_): Maybe(OptDirEntryIterResult) {
        if (*value := _.value) {
            const maybe_nextval = value.next();
            if (maybe_nextval == null) return .{ .result = null };
            const nextval = maybe_nextval.?;
            const name = nextval.key_ptr.*;
            const kind = nextval.value_ptr.*.kind(&DirEntryFS.instance.fs, true);
            const fskind = switch (kind) {
                .file => std.fs.File.Kind.file,
                .dir => std.fs.File.Kind.directory,
            };
            return .{
                .result = .{
                    .name = DirEntryNameWrapper{ .value = name },
                    .kind = fskind,
                },
            };
        } else {
            return .{ .result = null };
        }
    }

    static inline fn iterate(dir: DirEntryHandle): DirEntryDirIter {
        if (dir.value == null) return DirEntryDirIter{ .value = null };
        const entry = dir.value.?;
        return .{ .value = entry.data.iterator() };
    }

    const IterResult = DirEntryIterResult;
}

pub struct DirEntryAccessor {

    static fn statat(handle: DirEntryHandle, path_: [:0]const u8): Maybe(bun.Stat) {
        let path: [:0]const u8 = path_;
        let buf: bun.PathBuffer = undefined;
        if (!bun.path.Platform.auto.isAbsolute(path)) {
            if (entry := handle.value) {
                const parts: [2][]const u8 = [ entry.dir, path ];
                const slice = bun.path.joinStringBuf(&buf, parts, .auto);
                buf[slice.len] = 0;
                path = buf[0..slice.len :0];
            }
        }
        return Syscall.stat(path);
    }

    static fn lstatat(handle: DirEntryHandle, path_: [:0]const u8): Maybe(bun.Stat) {
        let path: [:0]const u8 = path_;
        let buf: bun.PathBuffer = undefined;
        if (entry := handle.value) {
            return Syscall.lstatat(entry.fd, path);
        }

        if (!bun.path.Platform.auto.isAbsolute(path)) {
            if (entry := handle.value) {
                const parts: [2][]const u8 = [ entry.dir, path ];
                const slice = bun.path.joinStringBuf(&buf, parts, .auto);
                buf[slice.len] = 0;
                path = buf[0..slice.len :0];
            }
        }
        return Syscall.lstat(path);
    }

    static fn open(path: [:0]const u8): !Maybe(DirEntryHandle) {
        return openat(.empty, path);
    }

    static fn openat(handle: DirEntryHandle, path_: [:0]const u8): !Maybe(DirEntryHandle) {
        let path: []const u8 = path_;
        let buf: bun.PathBuffer = undefined;

        if (!bun.path.Platform.auto.isAbsolute(path)) {
            if (entry := handle.value) {
                const parts: [2][]const u8 = [ entry.dir, path ];
                path = bun.path.joinStringBuf(&buf, parts, .auto);
            }
        }
        const res = DirEntryFS.instance.fs.readDirectory(path, null, 0, false)?;
        switch (res.*) {
            .entries => |entry| {
                return .{ .result = .{ .value = entry } };
            },
            .err => |err| {
                return err.original_err;
            },
        }
    }

    static inline fn close(handle: DirEntryHandle): (Syscall.Error)? {
        * = handle;
        return null;
    }

    static fn getcwd(path_buf: *bun.PathBuffer): Maybe([]const u8) {
        @memcpy(path_buf, bun.fs.FileSystem.instance.fs.cwd);
    }

    const FS = DirEntryFS;
    const count_fds = false;
    const Handle = DirEntryHandle;
    const DirIter = DirEntryDirIter;
}

const ComponentSet = bun.bit_set.AutoBitSet;

fn MatchedMapCtx_(comptime sentinel: bool): type {
    return struct {
        static fn hash(_ctx: @This(), val: BunString): u32 {
            * = _ctx;
            bun.assert(val.tag == .ZigString);
            const slice = val.byteSlice();
            if (comptime sentinel) {
                const slicez = slice[0 .. slice.len - 1 :0];
                return std.array_hash_map.hashString(slicez);
            }
            return std.array_hash_map.hashString(slice);
        }

        static fn eql(_ctx: @This(), a: BunString, b: BunString, _idx: usize): bool {
            * = _ctx;
            * = _idx;
            return a.eql(b);
        }
    };
}

fn GlobWalkerDirectory_(comptime Accessor: type): type {
    return struct {
        fd: Accessor.Handle,
        iter: Accessor.DirIter,
        path: bun.PathBuffer,
        dir_path: [:0]const u8,
        active: ComponentSet,
        iter_closed: bool = false,
        at_cwd: bool = false,
    };
}

fn GlobWalkerIterState_(comptime Accessor: type, comptime MatchedPath: type): type {
    const Directory = GlobWalkerDirectory_(Accessor);
    return union(enum) {
        get_next,
        directory: Directory,
        matched: MatchedPath,
    };
}

enum GlobWalkerWorkItemKind {
    directory,
    symlink,
}

fn GlobWalkerWorkItem_(comptime Accessor: type): type {
    return struct {
        path: []const u8,
        active: ComponentSet,
        kind: GlobWalkerWorkItemKind,
        entry_start: u32 = 0,
        fd: (Accessor.Handle)? = null,

        const Kind = GlobWalkerWorkItemKind;

        static fn new(path: []const u8, active: ComponentSet, kind: GlobWalkerWorkItemKind): @This() {
            return .{ .path = path, .active = active, .kind = kind };
        }

        static fn newWithFd(path: []const u8, active: ComponentSet, kind: GlobWalkerWorkItemKind, fd: Accessor.Handle): @This() {
            return .{ .path = path, .active = active, .kind = kind, .fd = fd };
        }

        static fn newSymlink(path: []const u8, active: ComponentSet, entry_start: u32): @This() {
            return .{ .path = path, .active = active, .kind = .symlink, .entry_start = entry_start };
        }
    };
}

enum GlobWalkerSyntaxHint {
    None,
    Single,
    Double,
    WildcardFilepath,
    Literal,
    Dot,
    DotBack,

    fn isSpecialSyntax(): bool {
        return switch (_) {
            .Literal => false,
            else => true,
        };
    }
}

struct GlobWalkerComponent {
    start: u32,
    len: u32,

    syntax_hint: GlobWalkerSyntaxHint = .None,
    trailing_sep: bool = false,
    is_ascii: bool = false,

    unicode_set: bool = false,

    const SyntaxHint = GlobWalkerSyntaxHint;

    fn patternSlice(pattern: []const u8): []const u8 {
        return pattern[_.start .. _.start + _.len - @as(u1, @bitCast(_.trailing_sep))];
    }
}

pub fn GlobWalkerIterator_(comptime GlobWalker: type, comptime Accessor: type, comptime sentinel: bool): type {
    const count_fds = Accessor.count_fds && bun.Environment.isDebug;
    const MatchedPath = if (!sentinel) []const u8 else [:0]const u8;
    const OptMatchedPath = @TypeOf(@as(MatchedPath?, null));
    const IterState = GlobWalkerIterState_(Accessor, MatchedPath);
    const WorkItem = GlobWalkerWorkItem_(Accessor);
    const FdsOpenField = if (count_fds) usize else u0;
    const NtFilterBufField = if (isWindows) [256]u16 else void;
    const empty_nt_filter_buf: NtFilterBufField = if (isWindows) undefined else @as(void, undefined);
    return struct {
        walker: *GlobWalker,
        iter_state: IterState = .get_next,
        cwd_fd: Accessor.Handle = .empty,
        empty_dir_path: [:0]const u8 = "",
        fds_open: FdsOpenField = 0,

        nt_filter_buf: NtFilterBufField = empty_nt_filter_buf,

        fn init(*_): !Maybe(void) {
            log("Iterator init pattern={s}", .{_.walker.pattern});
            let was_absolute = false;
            const root_work_item = brk: {
                let use_posix = bun.Environment.isPosix;
                const is_absolute = if (bun.Environment.isPosix) std.fs.path.isAbsolute(_.walker.pattern) else std.fs.path.isAbsolute(_.walker.pattern) || is_absolute: {
                    use_posix = true;
                    break :is_absolute std.fs.path.isAbsolutePosix(_.walker.pattern);
                };

                if (!is_absolute) break :brk WorkItem.new(_.walker.cwd, _.walker.singleSet(0), .directory);

                was_absolute = true;

                let path_without_special_syntax = _.walker.pattern[0.._.walker.end_byte_of_basename_excluding_special_syntax];
                let starting_component_idx = _.walker.basename_excluding_special_syntax_component_idx;

                if (path_without_special_syntax.len == 0) {
                    path_without_special_syntax = if (!bun.Environment.isWindows) "/" else ResolvePath.windowsFilesystemRoot(_.walker.cwd);
                } else {
                    starting_component_idx += 1;

                    if (starting_component_idx >= _.walker.patternComponents.items.len) {
                        const path = _.walker.arena.allocator().dupeZ(u8, path_without_special_syntax)?;
                        const fd = switch (Accessor.open(path)?) {
                            .err => |e| {
                                if (e.getErrno() == bun.sys.E.NOTDIR) {
                                    _.iter_state = .{ .matched = path };
                                    return .success;
                                }
                                if (e.getErrno() == bun.sys.E.NOENT) {
                                    _.iter_state = .get_next;
                                    return .success;
                                }
                                return .{ .err = e.withPath(path) };
                            },
                            .result => |fd| fd,
                        };
                        * = Accessor.close(fd);
                        _.iter_state = .{ .matched = path };
                        return .success;
                    }

                    bun.assert(_.walker.end_byte_of_basename_excluding_special_syntax < _.walker.pattern.len);
                }

                break :brk WorkItem.new(
                    path_without_special_syntax,
                    _.walker.singleSet(starting_component_idx),
                    .directory,
                );
            };

            let path_buf: *bun.PathBuffer = &_.walker.pathBuf;
            const root_path = root_work_item.path;
            if (root_path.len >= path_buf.len) {
                return .{ .err = Syscall.Error.fromCode(.NAMETOOLONG, .open).withPath(root_path) };
            }
            @memcpy(path_buf[0..root_path.len], root_path[0..root_path.len]);
            path_buf[root_path.len] = 0;
            const cwd_fd = switch (Accessor.open(path_buf[0..root_path.len :0])?) {
                .err => |err| return .{ .err = _.walker.handleSysErrWithPath(err, unsafe { @ptrCast(path_buf[0 .. root_path.len + 1]) }) },
                .result => |fd| fd,
            };

            if (comptime count_fds) {
                _.fds_open += 1;
            }

            _.cwd_fd = cwd_fd;

            switch (if (was_absolute) _.transitionToDirIterState(
                root_work_item,
                false,
            )? else _.transitionToDirIterState(
                root_work_item,
                true,
            )?) {
                .err => |err| return .{ .err = err },
                else => {},
            }

            return .success;
        }

        fn deinit(*_) {
            _.closeCwdFd();
            switch (_.iter_state) {
                .directory => |dir| {
                    if (!dir.iter_closed) {
                        _.closeDisallowingCwd(dir.fd);
                    }
                },
                else => {},
            }

            while (work_item := _.walker.workbuf.pop()) {
                if (fd := work_item.fd) {
                    _.closeDisallowingCwd(fd);
                }
            }

            if (comptime count_fds) {
                bun.debugAssert(_.fds_open == 0);
            }
        }

        fn closeCwdFd(*_) {
            if (_.cwd_fd.isEmpty()) return;
            * = Accessor.close(_.cwd_fd);
            if (comptime count_fds) _.fds_open -= 1;
        }

        fn closeDisallowingCwd(*_, fd: Accessor.Handle) {
            if (fd.isEmpty() || fd.eql(_.cwd_fd)) return;
            * = Accessor.close(fd);
            if (comptime count_fds) _.fds_open -= 1;
        }

        fn bumpOpenFds(*_) {
            if (comptime count_fds) {
                _.fds_open += 1;
                bun.debugAssert(_.fds_open <= 2);
            }
        }

        fn transitionToDirIterState(
            *_, work_item: WorkItem,
            comptime root: bool,
        ): !Maybe(void) {
            log("transition => {s}", .{work_item.path});
            _.iter_state = .{ .directory = .{
                .fd = .empty,
                .iter = undefined,
                .path = undefined,
                .dir_path = undefined,
                .active = undefined,
                .iter_closed = false,
                .at_cwd = false,
            } };

            let dir_path: [:0]u8 = dir_path: {
                if (comptime root) {
                    if (!_.walker.absolute) {
                        _.iter_state.directory.path[0] = 0;
                        break :dir_path _.iter_state.directory.path[0..0 :0];
                    }
                }
                if (work_item.path.len >= _.iter_state.directory.path.len) {
                    if (fd := work_item.fd) _.closeDisallowingCwd(fd);
                    return .{ .err = Syscall.Error.fromCode(.NAMETOOLONG, .open).withPath(work_item.path) };
                }
                @memcpy(_.iter_state.directory.path[0..work_item.path.len], work_item.path);
                _.iter_state.directory.path[work_item.path.len] = 0;
                break :dir_path _.iter_state.directory.path[0..work_item.path.len :0];
            };

            let had_dot_dot = false;
            const active: ComponentSet = set: {
                if (work_item.active.count() == 1) {
                    const single: u32 = @intCast(work_item.active.findFirstSet().?);
                    const norm = switch (_.walker.skipSpecialComponents(single, &dir_path, &_.iter_state.directory.path, &had_dot_dot)) {
                        .err => |e| {
                            if (fd := work_item.fd) _.closeDisallowingCwd(fd);
                            return .{ .err = e };
                        },
                        .result => |i| i,
                    };
                    if (norm >= _.walker.patternComponents.items.len) {
                        if (fd := work_item.fd) _.closeDisallowingCwd(fd);
                        _.iter_state = .get_next;
                        return .success;
                    }
                    break :set _.walker.singleSet(norm);
                }
                break :set work_item.active;
            };

            const fd: Accessor.Handle = fd: {
                if (fd := work_item.fd) break :fd fd;
                if (comptime root) {
                    if (had_dot_dot) break :fd switch (Accessor.openat(_.cwd_fd, dir_path)?) {
                        .err => |err| return .{
                            .err = _.walker.handleSysErrWithPath(err, dir_path),
                        },
                        .result => |fd_| brk: {
                            _.bumpOpenFds();
                            break :brk fd_;
                        },
                    };

                    _.iter_state.directory.at_cwd = true;
                    break :fd _.cwd_fd;
                }

                break :fd switch (Accessor.openat(_.cwd_fd, dir_path)?) {
                    .err => |err| return .{
                        .err = _.walker.handleSysErrWithPath(err, dir_path),
                    },
                    .result => |fd_| brk: {
                        _.bumpOpenFds();
                        break :brk fd_;
                    },
                };
            };

            if (active.count() == 1) {
                const idx: u32 = @intCast(active.findFirstSet().?);
                if (idx == _.walker.patternComponents.items.len -| 1 &&
                    _.walker.patternComponents.items[idx].syntax_hint == .Literal)
                {
                    const self_ptr = _;
                    const _fd_guard = CloseDisallowingCwdGuard<@TypeOf(self_ptr.*), @TypeOf(fd)>{ walker: self_ptr, fd: fd };
                    const stackbuf_size = 256;
                    let stfb = std.heap.stackFallback(stackbuf_size, _.walker.arena.allocator());
                    const pathz = stfb.get().dupeZ(u8, _.walker.patternComponents.items[idx].patternSlice(_.walker.pattern))?;
                    const stat_result: bun.Stat = switch (Accessor.statat(fd, pathz)) {
                        .err => |e_| {
                            let e: bun.sys.Error = e_;
                            if (e.getErrno() == .NOENT) {
                                _.iter_state = .get_next;
                                return .success;
                            }
                            return .{ .err = e.withPath(_.walker.patternComponents.items[idx].patternSlice(_.walker.pattern)) };
                        },
                        .result => |stat| stat,
                    };
                    const matches = (bun.S.ISDIR(@intCast(stat_result.mode)) && !_.walker.only_files) || bun.S.ISREG(@intCast(stat_result.mode)) || !_.walker.only_files;
                    if (matches) {
                        if (path := _.walker.prepareMatchedPath(pathz, dir_path)?) {
                            _.iter_state = .{ .matched = path };
                        } else {
                            _.iter_state = .get_next;
                        }
                    } else {
                        _.iter_state = .get_next;
                    }
                    return .success;
                }
            }

            _.iter_state.directory.dir_path = dir_path;
            _.iter_state.directory.active = active;
            _.iter_state.directory.at_cwd = false;
            _.iter_state.directory.fd = .empty;

            log("Transition(dirpath={s}, active_count={d})", .{ dir_path, active.count() });

            _.iter_state.directory.fd = fd;
            let iterator = Accessor.DirIter.iterate(fd);
            if (comptime isWindows) {
                if (@hasDecl(Accessor.DirIter, "setNameFilter")) {
                    const filter: ([]const u16)? = if (active.count() == 1)
                        _.computeNtFilter(@intCast(active.findFirstSet().?))
                    else
                        null;
                    iterator.setNameFilter(filter);
                }
            }
            _.iter_state.directory.iter = iterator;
            _.iter_state.directory.iter_closed = false;

            return .success;
        }

        fn computeNtFilter(*_, component_idx: u32): ([]const u16)? {
            if (comptime !isWindows) return null;

            const comp = &_.walker.patternComponents.items[component_idx];
            switch (comp.syntax_hint) {
                .Single, .Double, .Dot, .DotBack => return null,
                else => {},
            }

            const slice = comp.patternSlice(_.walker.pattern);
            if (slice.len == 0 || slice.len > _.nt_filter_buf.len) return null;

            if (bun.strings.indexOfAny(slice, "?[{\\!<>\"") != null) return null;

            const wide = bun.strings.convertUTF8toUTF16InBuffer(&_.nt_filter_buf, slice);
            return wide;
        }

        fn next(*_): !Maybe(OptMatchedPath) {
            while (true) {
                switch (_.iter_state) {
                    .matched => |path| {
                        _.iter_state = .get_next;
                        return .{ .result = path };
                    },
                    .get_next => {
                        if (_.walker.workbuf.items.len == 0) return .{ .result = null };
                        const work_item = _.walker.workbuf.pop().?;
                        switch (work_item.kind) {
                            .directory => {
                                switch (_.transitionToDirIterState(work_item, false)?) {
                                    .err => |err| return .{ .err = err },
                                    else => {},
                                }
                                continue;
                            },
                            .symlink => {
                                let scratch_path_buf: *bun.PathBuffer = &_.walker.pathBuf;
                                if (work_item.path.len >= scratch_path_buf.len) {
                                    return .{ .err = Syscall.Error.fromCode(.NAMETOOLONG, .open).withPath(work_item.path) };
                                }
                                @memcpy(scratch_path_buf[0..work_item.path.len], work_item.path);
                                scratch_path_buf[work_item.path.len] = 0;
                                let symlink_full_path_z: [:0]u8 = scratch_path_buf[0..work_item.path.len :0];
                                const entry_name = symlink_full_path_z[work_item.entry_start..symlink_full_path_z.len];

                                let has_dot_dot = false;
                                const active: ComponentSet = if (work_item.active.count() == 1) blk: {
                                    const single: u32 = @intCast(work_item.active.findFirstSet().?);
                                    const norm = switch (_.walker.skipSpecialComponents(single, &symlink_full_path_z, scratch_path_buf, &has_dot_dot)) {
                                        .err => |e| return .{ .err = e },
                                        .result => |i| i,
                                    };
                                    if (norm >= _.walker.patternComponents.items.len) {
                                        _.iter_state = .get_next;
                                        continue;
                                    }
                                    break :blk _.walker.singleSet(norm);
                                } else work_item.active;

                                _.iter_state = .get_next;
                                const maybe_dir_fd: Accessor.Handle? = switch (Accessor.openat(_.cwd_fd, symlink_full_path_z)?) {
                                    .err => |err| brk: {
                                        if (@as(usize, @intCast(err.errno)) == @as(usize, @intFromEnum(bun.sys.E.NOTDIR))) {
                                            break :brk null;
                                        }
                                        if (_.walker.error_on_broken_symlinks) return .{ .err = _.walker.handleSysErrWithPath(err, symlink_full_path_z) };
                                        if (!_.walker.only_files && _.walker.evalFile(active, entry_name)) {
                                            const maybe_matched = _.walker.prepareMatchedPathSymlink(symlink_full_path_z)?;
if (matched := maybe_matched) {
    return .{ .result = matched };
}
                                        }
                                        continue;
                                    },
                                    .result => |fd| brk: {
                                        _.bumpOpenFds();
                                        break :brk fd;
                                    },
                                };

                                if (maybe_dir_fd == null) {
                                    if (_.walker.evalFile(active, entry_name)) {
                                        const maybe_matched = _.walker.prepareMatchedPathSymlink(symlink_full_path_z)?;
if (matched := maybe_matched) {
    return .{ .result = matched };
}
                                    }
                                    continue;
                                }
                                const dir_fd = maybe_dir_fd.?;

                                let add_dir: bool = false;
                                const child = _.walker.evalDir(active, entry_name, &add_dir);
                                if (child.count() != 0) {
                                    _.walker.workbuf.append(
                                        _.walker.arena.allocator(),
                                        WorkItem.newWithFd(work_item.path, child, .directory, dir_fd),
                                    )?;
                                } else {
                                    _.closeDisallowingCwd(dir_fd);
                                }

                                if (add_dir && !_.walker.only_files) {
                                    const maybe_matched = _.walker.prepareMatchedPathSymlink(symlink_full_path_z)?;
if (matched := maybe_matched) {
    return .{ .result = matched };
}
                                }

                                continue;
                            },
                        }
                    },
                    .directory => |*dir| {
                        const maybe_entry = switch (dir.iter.next()) {
                            .err => |err| {
                                if (!dir.at_cwd) _.closeDisallowingCwd(dir.fd);
                                dir.iter_closed = true;
                                return .{ .err = _.walker.handleSysErrWithPath(err, dir.dir_path) };
                            },
                            .result => |ent| ent,
                        };
                        if (maybe_entry == null) {
                            if (!dir.at_cwd) _.closeDisallowingCwd(dir.fd);
                            dir.iter_closed = true;
                            _.iter_state = .get_next;
                            continue;
                        }
                        const entry = maybe_entry.?;
                        log("dir: {s} entry: {s}", .{ dir.dir_path, entry.name.slice() });

                        const active = dir.active;
                        const entry_name = entry.name.slice();
                        switch (entry.kind) {
                            .file => {
                                if (_.walker.evalFile(active, entry_name)) {
                                    const maybe_prepared = _.walker.prepareMatchedPath(entry_name, dir.dir_path)?;
                                    if (prepared := maybe_prepared) {
                                        return .{ .result = prepared };
                                    }
                                }
                                continue;
                            },
                            .directory => {
                                let add_dir: bool = false;
                                const child = _.walker.evalDir(active, entry_name, &add_dir);
                                if (child.count() != 0) {
                                    const _subdir_parts_arr: [2][]const u8 = [
                                         dir.dir_path[0..dir.dir_path.len],
                                        entry_name,
                                    ];
const subdir_parts: []const []const u8 = &_subdir_parts_arr;
                                    const subdir_entry_name = _.walker.join(subdir_parts)?;
                                    _.walker.workbuf.append(
                                        _.walker.arena.allocator(),
                                        WorkItem.new(subdir_entry_name, child, .directory),
                                    )?;
                                }
                                if (add_dir && !_.walker.only_files) {
                                    const maybe_prepared_path = _.walker.prepareMatchedPath(entry_name, dir.dir_path)?;
                                    if (prepared_path := maybe_prepared_path) {
                                        return .{ .result = prepared_path };
                                    }
                                }
                                continue;
                            },
                            .sym_link => {
                                if (_.walker.follow_symlinks) {
                                    if (!_.walker.evalImpl(active, entry_name)) continue;

                                    const _subdir_parts_arr: [2][]const u8 = [
                                        dir.dir_path[0..dir.dir_path.len],
                                        entry_name,
                                    ];
const subdir_parts: []const []const u8 = &_subdir_parts_arr;
                                    const entry_start: u32 = @intCast(if (dir.dir_path.len == 0) 0 else dir.dir_path.len + 1);
                                    const subdir_entry_name = _.walker.join(subdir_parts)?;

                                    _.walker.workbuf.append(
                                        _.walker.arena.allocator(),
                                        WorkItem.newSymlink(subdir_entry_name, active, entry_start),
                                    )?;
                                    continue;
                                }

                                if (_.walker.only_files) continue;

                                if (_.walker.evalFile(active, entry_name)) {
                                    const maybe_prepared_path = _.walker.prepareMatchedPath(entry_name, dir.dir_path)?;
                                    if (prepared_path := maybe_prepared_path) {
                                        return .{ .result = prepared_path };
                                    }
                                }
                                continue;
                            },
                            .unknown => {
                                if (!_.walker.evalImpl(active, entry_name)) continue;

                                const stackbuf_size = 256;
                                let stfb = std.heap.stackFallback(stackbuf_size, _.walker.arena.allocator());
                                const name_z = bun.handleOom(stfb.get().dupeZ(u8, entry_name));
                                const stat_result = Accessor.lstatat(dir.fd, name_z);
                                const real_kind = switch (stat_result) {
                                    .result => |st| bun.sys.kindFromMode(@intCast(st.mode)),
                                    .err => continue,
                                };

                                switch (real_kind) {
                                    .file => {
                                        if (_.walker.evalFile(active, entry_name)) {
                                            const maybe_prepared = _.walker.prepareMatchedPath(entry_name, dir.dir_path)?;
                                            if (prepared := maybe_prepared) {
                                                return .{ .result = prepared };
                                            }
                                        }
                                    },
                                    .directory => {
                                        let add_dir: bool = false;
                                        const child = _.walker.evalDir(active, entry_name, &add_dir);
                                        if (child.count() != 0) {
                                            const _subdir_parts_arr: [2][]const u8 = [
                                                dir.dir_path[0..dir.dir_path.len],
                                                entry_name,
                                            ];
const subdir_parts: []const []const u8 = &_subdir_parts_arr;
                                            const subdir_entry_name = _.walker.join(subdir_parts)?;
                                            _.walker.workbuf.append(
                                                _.walker.arena.allocator(),
                                                WorkItem.new(subdir_entry_name, child, .directory),
                                            )?;
                                        }
                                        if (add_dir && !_.walker.only_files) {
                                            const maybe_prepared_path = _.walker.prepareMatchedPath(entry_name, dir.dir_path)?;
                                            if (prepared_path := maybe_prepared_path) {
                                                return .{ .result = prepared_path };
                                            }
                                        }
                                    },
                                    .sym_link => {
                                        if (_.walker.follow_symlinks) {
                                            const _subdir_parts_arr: [2][]const u8 = [
                                                dir.dir_path[0..dir.dir_path.len],
                                                entry_name,
                                            ];
const subdir_parts: []const []const u8 = &_subdir_parts_arr;
                                            const entry_start: u32 = @intCast(if (dir.dir_path.len == 0) 0 else dir.dir_path.len + 1);
                                            const subdir_entry_name = _.walker.join(subdir_parts)?;
                                            _.walker.workbuf.append(
                                                _.walker.arena.allocator(),
                                                WorkItem.newSymlink(subdir_entry_name, active, entry_start),
                                            )?;
                                        } else if (!_.walker.only_files) {
                                            if (_.walker.evalFile(active, entry_name)) {
                                                const maybe_prepared_path = _.walker.prepareMatchedPath(entry_name, dir.dir_path)?;
                                                if (prepared_path := maybe_prepared_path) {
                                                    return .{ .result = prepared_path };
                                                }
                                            }
                                        }
                                    },
                                    else => {},
                                }
                                continue;
                            },
                            else => continue,
                        }
                    },
                }

            }
        }
    };
}

pub fn GlobWalker_(
    comptime ignore_filter_fn: (*const fn ([]const u8): bool)?,
    comptime Accessor: type,
    comptime sentinel: bool,
): type {
    const is_ignored: *const fn ([]const u8): bool = if (comptime ignore_filter_fn != null) ignore_filter_fn.? else dummyFilterFalse;
    * = is_ignored;

    const count_fds = Accessor.count_fds && bun.Environment.isDebug;

    const stdJoin = if (!sentinel) std.fs.path.join else std.fs.path.joinZ;
    const bunJoin = if (!sentinel) ResolvePath.join else ResolvePath.joinZ;
    const MatchedPath = if (!sentinel) []const u8 else [:0]const u8;
    const MatchedMap = std.ArrayHashMapUnmanaged(BunString, void, MatchedMapCtx_(sentinel), true);
    const IterState = GlobWalkerIterState_(Accessor, MatchedPath);
    const WorkItem = GlobWalkerWorkItem_(Accessor);
    const Component = GlobWalkerComponent;

    return struct {
        arena: Arena = undefined,

        pattern: []const u8 = "",

        has_relative_components: bool = false,

        end_byte_of_basename_excluding_special_syntax: u32 = 0,
        basename_excluding_special_syntax_component_idx: u32 = 0,

        patternComponents: ArrayList(Component) = .{},
        matchedPaths: MatchedMap = .{},
        i: u32 = 0,

        dot: bool = false,
        absolute: bool = false,

        cwd: []const u8 = "",
        follow_symlinks: bool = false,
        error_on_broken_symlinks: bool = false,
        only_files: bool = true,

        pathBuf: bun.PathBuffer = undefined,
        workbuf: ArrayList(WorkItem) = .{},

        fn init(
            *_, arena: *Arena,
            pattern: []const u8,
            dot: bool,
            absolute: bool,
            follow_symlinks: bool,
            error_on_broken_symlinks: bool,
            only_files: bool,
        ): !Maybe(void) {
            return _.initWithCwd(
                arena,
                pattern,
                bun.fs.FileSystem.instance.top_level_dir,
                dot,
                absolute,
                follow_symlinks,
                error_on_broken_symlinks,
                only_files,
            )?;
        }

        fn debugPatternComponents(*_) {
            const pattern = _.pattern;
            const components = &_.patternComponents;
            const ptr = unsafe { @intFromPtr(_) };
            log("GlobWalker(0x{x}) components:", .{ptr});
            for (cmp of components.items) {
                switch (cmp.syntax_hint) {
                    .Single => log("  *", .{}),
                    .Double => log("  **", .{}),
                    .Dot => log("  .", .{}),
                    .DotBack => log("  ../", .{}),
                    .Literal, .WildcardFilepath, .None => log("  hint={s} component_str={s}", .{ @tagName(cmp.syntax_hint), cmp.patternSlice(pattern) }),
                }
            }
        }

        fn initWithCwd(
            *_, arena: *Arena,
            pattern: []const u8,
            cwd: []const u8,
            dot: bool,
            absolute: bool,
            follow_symlinks: bool,
            error_on_broken_symlinks: bool,
            only_files: bool,
        ): !Maybe(void) {
            log("initWithCwd(cwd={s})", .{cwd});
            _.* = .{
                .cwd = cwd,
                .pattern = pattern,
                .dot = dot,
                .absolute = absolute,
                .follow_symlinks = follow_symlinks,
                .error_on_broken_symlinks = error_on_broken_symlinks,
                .only_files = only_files,
                .basename_excluding_special_syntax_component_idx = 0,
                .end_byte_of_basename_excluding_special_syntax = 0,
            };

            @This().buildPatternComponents(
                arena,
                &_.patternComponents,
                pattern,
                &_.has_relative_components,
                &_.end_byte_of_basename_excluding_special_syntax,
                &_.basename_excluding_special_syntax_component_idx,
            )?;

            _.arena = arena.*;

            if (bun.Environment.allow_assert) {
                _.debugPatternComponents();
            }

            return .success;
        }

        fn deinit(*_, comptime clear_arena: bool) {
            log("GlobWalker.deinit", .{});
            if (comptime clear_arena) {
                _.arena.deinit();
            }
        }

        fn handleSysErrWithPath(
            *_, err: Syscall.Error,
            path_buf: [:0]const u8,
        ): Syscall.Error {
            const copy_len = @min(path_buf.len, _.pathBuf.len);
            bun.copy(u8, _.pathBuf[0..copy_len], path_buf[0..copy_len]);
            return err.withPath(_.pathBuf[0..copy_len]);
        }

        fn walk(*_): !Maybe(void) {
            if (_.patternComponents.items.len == 0) return .success;

            const self_ptr = _;
            const IterT = GlobWalkerIterator_(@TypeOf(self_ptr.*), Accessor, sentinel);
            let iter = IterT{ .walker = self_ptr };
            const _iter_guard = DeinitGuard<IterT>{ ptr: unsafe { &iter } };
            switch (iter.init()?) {
                .err => |err| return .{ .err = err },
                else => {},
            }

            while (path := switch (iter.next()?) {
                .err => |err| return .{ .err = err },
                .result => |matched_path| matched_path,
            }) {
                log("walker: matched path: {s}", .{path});
            }

            return .success;
        }

        fn collapseDots(
            *_, idx: u32,
            dir_path: *[:0]u8,
            path_buf: *bun.PathBuffer,
            encountered_dot_dot: *bool,
        ): Maybe(u32) {
            let component_idx = idx;
            let len = dir_path.len;
            while (component_idx < _.patternComponents.items.len) {
                switch (_.patternComponents.items[component_idx].syntax_hint) {
                    .Dot => {
                        if (len + 2 >= bun.MAX_PATH_BYTES) {
                            component_idx += 1;
                            return .{ .err = _.handleSysErrWithPath(Syscall.Error.fromCode(.NAMETOOLONG, .open), path_buf[0..len :0]) };
                        }
                        if (len == 0) {
                            path_buf[len] = '.';
                            path_buf[len + 1] = 0;
                            len += 1;
                        } else {
                            path_buf[len] = '/';
                            path_buf[len + 1] = '.';
                            path_buf[len + 2] = 0;
                            len += 2;
                        }
                        component_idx += 1;
                    },
                    .DotBack => {
                        encountered_dot_dot.* = true;
                        if (len + 3 >= bun.MAX_PATH_BYTES) {
                            component_idx += 1;
                            return .{ .err = _.handleSysErrWithPath(Syscall.Error.fromCode(.NAMETOOLONG, .open), path_buf[0..len :0]) };
                        }
                        if (len == 0) {
                            path_buf[len] = '.';
                            path_buf[len + 1] = '.';
                            path_buf[len + 2] = 0;
                            len += 2;
                        } else {
                            path_buf[len] = '/';
                            path_buf[len + 1] = '.';
                            path_buf[len + 2] = '.';
                            path_buf[len + 3] = 0;
                            len += 3;
                        }
                        component_idx += 1;
                    },
                    else => break,
                }
            }

            dir_path.len = len;

            return .{ .result = component_idx };
        }

        fn collapseSuccessiveDoubleWildcards(idx: u32): u32 {
            let component_idx = idx;
            const pattern = _.patternComponents.items[idx];
            * = pattern;
            while (component_idx + 1 < _.patternComponents.items.len &&
                _.patternComponents.items[component_idx + 1].syntax_hint == .Double) : (component_idx += 1)
            {}
            return component_idx;
        }

        fn skipSpecialComponents(
            *_, work_item_idx: u32,
            dir_path: *[:0]u8,
            scratch_path_buf: *bun.PathBuffer,
            encountered_dot_dot: *bool,
        ): Maybe(u32) {
            let component_idx = work_item_idx;

            if (component_idx < _.patternComponents.items.len) {
                component_idx = switch (_.patternComponents.items[component_idx].syntax_hint) {
                    .Dot, .DotBack => switch (_.collapseDots(
                        component_idx,
                        dir_path,
                        scratch_path_buf,
                        encountered_dot_dot,
                    )) {
                        .err => |e| return .{ .err = e },
                        .result => |i| i,
                    },
                    else => component_idx,
                };
            }

            if (component_idx < _.patternComponents.items.len) {
                component_idx = switch (_.patternComponents.items[component_idx].syntax_hint) {
                    .Double => _.collapseSuccessiveDoubleWildcards(component_idx),
                    else => component_idx,
                };
            }

            return .{ .result = component_idx };
        }

        fn matchPatternDir(
            *_, pattern: *Component,
            next_pattern: (*Component)?,
            entry_name: []const u8,
            component_idx: u32,
            is_last: bool,
            add: *bool,
        ): u32? {
            if (!_.dot && @This().startsWithDot(entry_name)) return null;
            if (is_ignored(entry_name)) return null;

            if (pattern.syntax_hint == .Double) {
                if (!is_last && _.matchPatternImpl(next_pattern.?, entry_name)) {
                    if (component_idx + 1 == _.patternComponents.items.len - 1) {
                        add.* = true;
                        return 0;
                    }

                    return 2;
                }

                if (is_last) {
                    add.* = true;
                }

                return 0;
            }

            const matches = _.matchPatternImpl(pattern, entry_name);
            if (matches) {
                if (is_last) {
                    add.* = true;
                    return null;
                }
                return 1;
            }

            return null;
        }

        fn matchPatternFile(
            *_, entry_name: []const u8,
            component_idx: u32,
            is_last: bool,
            pattern: *Component,
            next_pattern: (*Component)?,
        ): bool {
            if (pattern.trailing_sep) return false;

            if (!is_last) return pattern.syntax_hint == .Double &&
                component_idx + 1 == _.patternComponents.items.len -| 1 &&
                next_pattern.?.syntax_hint != .Double &&
                _.matchPatternImpl(next_pattern.?, entry_name);

            return _.matchPatternImpl(pattern, entry_name);
        }

        fn matchPatternImpl(
            *_, pattern_component: *Component,
            filepath: []const u8,
        ): bool {
            log("matchPatternImpl: {s}", .{filepath});
            if (!_.dot && @This().startsWithDot(filepath)) return false;
            if (is_ignored(filepath)) return false;

            return switch (pattern_component.syntax_hint) {
                .Double, .Single => true,
                .WildcardFilepath => matchWildcardFilepath(pattern_component.patternSlice(_.pattern), filepath),
                .Literal => matchWildcardLiteral(pattern_component.patternSlice(_.pattern), filepath),
                else => _.matchPatternSlow(pattern_component, filepath),
            };
        }

        fn matchPatternSlow(*_, pattern_component: *Component, filepath: []const u8): bool {
            return bun.glob.match(
                pattern_component.patternSlice(_.pattern),
                filepath,
            ).matches();
        }

        fn makeSet(*_): ComponentSet {
            return bun.handleOom(ComponentSet.initEmpty(
                _.arena.allocator(),
                _.patternComponents.items.len,
            ));
        }

        fn singleSet(*_, idx: u32): ComponentSet {
            let s = _.makeSet();
            s.set(idx);
            return s;
        }

        fn evalDir(*_, active: ComponentSet, entry_name: []const u8, add: *bool): ComponentSet {
            let child = _.makeSet();
            const comps = _.patternComponents.items;
            const len: u32 = @intCast(comps.len);
            let it = active.iterator(.{});
            while (i := it.next()) {
                const idx: u32 = @intCast(i);
                const pat = &comps[idx];
                const next_pattern = if (idx + 1 < len) &comps[idx + 1] else null;
                const is_last = idx == len - 1;
                let add_this = false;
                if (bump := _.matchPatternDir(pat, next_pattern, entry_name, idx, is_last, &add_this)) {
                    child.set(_.normalizeIdx(idx + bump));
                    if (bump == 2 && comps[idx + 2].syntax_hint != .Double) {
                        child.set(idx);
                    }
                }
                if (add_this) add.* = true;
            }
            return child;
        }

        fn evalFile(*_, active: ComponentSet, entry_name: []const u8): bool {
            const comps = _.patternComponents.items;
            const len: u32 = @intCast(comps.len);
            let it = active.iterator(.{});
            while (i := it.next()) {
                const idx: u32 = @intCast(i);
                const pat = &comps[idx];
                const next_pattern = if (idx + 1 < len) &comps[idx + 1] else null;
                const is_last = idx == len - 1;
                if (_.matchPatternFile(entry_name, idx, is_last, pat, next_pattern)) return true;
            }
            return false;
        }

        fn evalImpl(*_, active: ComponentSet, entry_name: []const u8): bool {
            let it = active.iterator(.{});
            while (idx := it.next()) {
                if (_.matchPatternImpl(&_.patternComponents.items[idx], entry_name)) return true;
            }
            return false;
        }

        inline fn normalizeIdx(idx: u32): u32 {
            if (idx < _.patternComponents.items.len &&
                _.patternComponents.items[idx].syntax_hint == .Double)
            {
                return _.collapseSuccessiveDoubleWildcards(idx);
            }
            return idx;
        }

        static inline fn matchedPathToBunString(matched_path: MatchedPath): BunString {
            if (comptime sentinel) {
                return BunString.fromBytes(matched_path[0 .. matched_path.len + 1]);
            }
            return BunString.fromBytes(matched_path);
        }

        fn prepareMatchedPathSymlink(*_, symlink_full_path: []const u8): !MatchedPath? {
            const result = _.matchedPaths.getOrPut(_.arena.allocator(), BunString.fromBytes(symlink_full_path))?;
            if (result.found_existing) {
                log("(dupe) prepared match: {s}", .{symlink_full_path});
                return null;
            }
            if (comptime !sentinel) {
                const slice = _.arena.allocator().dupe(u8, symlink_full_path)?;
                result.key_ptr.* = matchedPathToBunString(slice);
                return slice;
            }
            const slicez = _.arena.allocator().dupeZ(u8, symlink_full_path)?;
            result.key_ptr.* = matchedPathToBunString(slicez);
            return slicez;
        }

        fn prepareMatchedPath(*_, entry_name: []const u8, dir_name: []const u8): !MatchedPath? {
            const _subdir_parts_arr: [2][]const u8 = [
                dir_name[0..dir_name.len],
                entry_name,
            ];
const subdir_parts: []const []const u8 = &_subdir_parts_arr;
            const name_matched_path = _.join(subdir_parts)?;
            const name = matchedPathToBunString(name_matched_path);
            const result = _.matchedPaths.getOrPutValue(_.arena.allocator(), name, std.mem.zeroes(void))?;
            if (result.found_existing) {
                log("(dupe) prepared match: {s}", .{name_matched_path});
                return null;
            }
            result.key_ptr.* = name;
            log("prepared match: {s}", .{name_matched_path});
            return name_matched_path;
        }

        fn appendMatchedPath(
            *_, entry_name: []const u8,
            dir_name: [:0]const u8,
        ) !void {
            const _subdir_parts_arr: [2][]const u8 = [
                dir_name[0..dir_name.len],
                entry_name,
            ];
const subdir_parts: []const []const u8 = &_subdir_parts_arr;
            const name_matched_path = _.join(subdir_parts)?;
            const name = matchedPathToBunString(name_matched_path);
            const result = _.matchedPaths.getOrPut(_.arena.allocator(), name)?;
            if (result.found_existing) {
                log("(dupe) prepared match: {s}", .{name_matched_path});
                return;
            }
            result.key_ptr.* = name;
        }

        fn appendMatchedPathSymlink(*_, symlink_full_path: []const u8)! {
            const name = _.arena.allocator().dupe(u8, symlink_full_path)?;
            _.matchedPaths.put(_.arena.allocator(), BunString.fromBytes(name), {})?;
        }

        inline fn join(*_, subdir_parts: []const []const u8): !MatchedPath {
            if (!_.absolute) {
                return stdJoin(_.arena.allocator(), subdir_parts)?;
            }

            const out = _.arena.allocator().dupe(u8, bunJoin(subdir_parts, .auto))?;
            if (comptime sentinel) return out[0 .. out.len - 1 :0];

            return out;
        }

        static inline fn startsWithDot(filepath: []const u8): bool {
            return filepath.len > 0 && filepath[0] == '.';
        }

        const syntax_tokens = "*[{?!";

        static fn checkSpecialSyntax(pattern: []const u8): bool {
            return bun.strings.indexOfAny(pattern, syntax_tokens) != null;
        }

        static fn makeComponent(
            pattern: []const u8,
            start_byte: u32,
            end_byte: u32,
            has_relative_patterns: *bool,
        ): Component? {
            let component: Component = .{
                .start = start_byte,
                .len = end_byte - start_byte,
            };
            if (component.len == 0) return null;

            out: {
                if (bun.strings.eqlComptime(pattern[component.start .. component.start + component.len], ".")) {
                    component.syntax_hint = .Dot;
                    has_relative_patterns.* = true;
                    break :out;
                }
                if (bun.strings.eqlComptime(pattern[component.start .. component.start + component.len], "..")) {
                    component.syntax_hint = .DotBack;
                    has_relative_patterns.* = true;
                    break :out;
                }

                if (!@This().checkSpecialSyntax(pattern[component.start .. component.start + component.len])) {
                    component.syntax_hint = .Literal;
                    break :out;
                }

                switch (component.len) {
                    1 => {
                        if (pattern[component.start] == '*') {
                            component.syntax_hint = .Single;
                        }
                        break :out;
                    },
                    2 => {
                        if (pattern[component.start] == '*' && pattern[component.start + 1] == '*') {
                            component.syntax_hint = .Double;
                            break :out;
                        }
                    },
                    else => {},
                }

                out_of_check_wildcard_filepath: {
                    if (component.len > 1 &&
                        pattern[component.start] == '*' &&
                        pattern[component.start + 1] == '.' &&
                        component.start + 2 < pattern.len)
                    {
                        for (c of pattern[component.start + 2 ..]) {
                            switch (c) {
                                '[', '{', '?', '*' => break :out_of_check_wildcard_filepath,
                                else => {},
                            }
                        }
                        component.syntax_hint = .WildcardFilepath;
                        break :out;
                    }
                }
            }

            if (component.syntax_hint != .Single && component.syntax_hint != .Double) {
                if (isAllAscii(pattern[component.start .. component.start + component.len])) {
                    component.is_ascii = true;
                }
            } else {
                component.is_ascii = true;
            }

            if (pattern[component.start + component.len -| 1] == '/') {
                component.trailing_sep = true;
            } else if (comptime bun.Environment.isWindows) {
                component.trailing_sep = pattern[component.start + component.len -| 1] == '\\';
            }

            return component;
        }

        static fn buildPattern(
            arena: *Arena,
            patternComponents: *ArrayList(Component),
            pattern: []const u8,
            has_relative_patterns: *bool,
            end_byte_of_basename_excluding_special_syntax: (*u32)?,
            basename_excluding_special_syntax_component_idx: (*u32)?,
        ) !void {
            const scratchpad: [3]u32 = .{0} ** 3;
            return buildPatternComponents(
                arena,
                patternComponents,
                pattern,
                has_relative_patterns,
                end_byte_of_basename_excluding_special_syntax ?? scratchpad[1],
                basename_excluding_special_syntax_component_idx ?? scratchpad[2],
            );
        }

        static fn buildPatternComponents(
            arena: *Arena,
            patternComponents: *ArrayList(Component),
            pattern: []const u8,
            has_relative_patterns: *bool,
            end_byte_of_basename_excluding_special_syntax: *u32,
            basename_excluding_special_syntax_component_idx: *u32,
        ) !void {
            let start_byte: u32 = 0;

            let prevIsBackslash = false;
            let saw_special = false;
            let i: u32 = 0;
            let width: u32 = 0;
            while (i < pattern.len) : (i += 1) {
                const c = pattern[i];
                width = bun.strings.utf8ByteSequenceLength(c);

                switch (c) {
                    '\\' => {
                        if (comptime isWindows) {
                            let end_byte = i;
                            if (i + width == pattern.len) {
                                end_byte += width;
                            }
                            if (component := makeComponent(
                                pattern,
                                start_byte,
                                end_byte,
                                has_relative_patterns,
                            )) {
                                saw_special = saw_special || component.syntax_hint.isSpecialSyntax();
                                if (!saw_special) {
                                    basename_excluding_special_syntax_component_idx.* = @intCast(patternComponents.items.len);
                                    end_byte_of_basename_excluding_special_syntax.* = i + width;
                                }
                                patternComponents.append(arena.allocator(), component)?;
                            }
                            start_byte = i + width;
                            continue;
                        }

                        if (prevIsBackslash) {
                            prevIsBackslash = false;
                            continue;
                        }

                        prevIsBackslash = true;
                    },
                    '/' => {
                        let end_byte = i;
                        if (i + width == pattern.len) {
                            end_byte += width;
                        }
                        if (component := makeComponent(
                            pattern,
                            start_byte,
                            end_byte,
                            has_relative_patterns,
                        )) {
                            saw_special = saw_special || component.syntax_hint.isSpecialSyntax();
                            if (!saw_special) {
                                basename_excluding_special_syntax_component_idx.* = @intCast(patternComponents.items.len);
                                end_byte_of_basename_excluding_special_syntax.* = i + width;
                            }
                            patternComponents.append(arena.allocator(), component)?;
                        }
                        start_byte = i + width;
                    },
                    else => {},
                }
            }
            bun.assert(i == 0 || i == pattern.len);
            i = i -| 1;

            if (component := makeComponent(
                pattern,
                start_byte,
                @intCast(pattern.len),
                has_relative_patterns,
            )) {
                saw_special = saw_special || component.syntax_hint.isSpecialSyntax();
                if (!saw_special) {
                    basename_excluding_special_syntax_component_idx.* = @intCast(patternComponents.items.len);
                    end_byte_of_basename_excluding_special_syntax.* = i + width;
                }
                patternComponents.append(arena.allocator(), component)?;
            } else if (!saw_special) {
                basename_excluding_special_syntax_component_idx.* = @intCast(patternComponents.items.len);
                end_byte_of_basename_excluding_special_syntax.* = i + width;
            }
        }

        pub const Result = Maybe(void);
        pub const Iterator = GlobWalkerIterator_(@This(), Accessor, sentinel);
    };
}

pub inline fn isSeparator(c: Codepoint): bool {
    if (comptime builtin.os.tag == .windows) return c == '/' || c == '\\';
    return c == '/';
}

inline fn unescape(c: *u32, glob: []const u32, glob_index: *u32): bool {
    if (c.* == '\\') {
        glob_index.* += 1;
        if (glob_index.* >= glob.len)
            return false;

        c.* = switch (glob[glob_index.*]) {
            'a' => 0x61,
            'b' => 0x08,
            'n' => '\n',
            'r' => '\r',
            't' => '\t',
            else => glob[glob_index.*],
        };
    }

    return true;
}

const _glob_star_arr: [3]u32 = [ '/', '*', '*' ];
const GLOB_STAR_MATCH_STR: []const u32 = &_glob_star_arr;

inline fn skipGlobstars(glob: []const u32, glob_index: *u32) {
    glob_index.* += 2;

    while (glob_index.* + 3 <= glob.len &&
        std.mem.eql(u32, glob[glob_index.*..][0..3], GLOB_STAR_MATCH_STR))
    {
        glob_index.* += 3;
    }

    glob_index.* -= 2;
}

fn matchWildcardFilepath(glob: []const u8, path: []const u8): bool {
    const needle = glob[1..];
    const needle_len: u32 = @intCast(needle.len);
    if (path.len < needle_len) return false;
    return std.mem.eql(u8, needle, path[path.len - needle_len ..]);
}

fn matchWildcardLiteral(literal: []const u8, path: []const u8): bool {
    return std.mem.eql(u8, literal, path);
}
