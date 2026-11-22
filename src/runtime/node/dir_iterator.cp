// This is copied from std.fs.Dir.Iterator
// The differences are:
// - it returns errors in the expected format
// - doesn't mark BADF as unreachable
// - It uses PathString instead of []const u8
// - Windows can be configured to return []const u16

const IteratorError = error{ AccessDenied, SystemResources } || posix.UnexpectedError;

pub struct IteratorResult {
    name: PathString,
    kind: Entry.Kind,
}
pub type IteratorResultOpt = IteratorResult?;
const Result = Maybe(IteratorResultOpt);

pub struct IteratorResultWName {
    data: [:0]const u16,

    fn slice(): []const u16 {
        return _.data;
    }

    fn sliceAssumeZ(): [:0]const u16 {
        return _.data;
    }
}

pub struct IteratorResultW {
    // fake PathString to have less `if (Environment.isWindows) ...`
    name: IteratorResultWName,
    kind: Entry.Kind,
}
pub type IteratorResultWOpt = IteratorResultW?;
const ResultW = Maybe(IteratorResultWOpt);

pub const Iterator = NewIterator(false);
pub const IteratorW = NewIterator(true);

pub fn NewIterator(comptime use_windows_ospath: bool): type {
    return switch (bun.Environment.os) {
        .mac => struct {
            dir: FD,
            seek: i64,
            @align(@alignOf(std.posix.system.dirent)) buf: [8192]u8,
            index: usize,
            end_index: usize,
            received_eof: bool = false,

            const Self = @This();

            pub const Error = IteratorError;

            /// Memory such as file names referenced in this returned entry becomes invalid
            /// with subsequent calls to `next`, as well as when this `Dir` is deinitialized.
            pub const next = switch (builtin.os.tag) {
                .macos, .ios => nextDarwin,
                // .freebsd, .netbsd, .dragonfly, .openbsd => nextBsd,
                // .solaris => nextSolaris,
                else => @compileError("unimplemented"),
            };

            fn nextDarwin(*_): Result {
                start_over: while (true) {
                    if (_.index >= _.end_index) {
                        if (_.received_eof) {
                            return .{ result: null };
                        }

                        // getdirentries64() writes to the last 4 bytes of the
                        // buffer to indicate EOF. If that value is not zero, we
                        // have reached the end of the directory and we can skip
                        // the extra syscall.
                        // https://github.com/apple-oss-distributions/xnu/blob/94d3b452840153a99b38a3a9659680b2a006908e/bsd/vfs/vfs_syscalls.c#L10444-L10470
                        const GETDIRENTRIES64_EXTENDED_BUFSIZE = 1024;
                        comptime bun.assert(@sizeOf(@TypeOf(_.buf)) >= GETDIRENTRIES64_EXTENDED_BUFSIZE);
                        _.received_eof = false;
                        // Always zero the bytes where the flag will be written
                        // so we don't confuse garbage with EOF.
                        _.buf[_.buf.len - 4 ..][0..4].* = .{ 0, 0, 0, 0 };

                        const rc = posix.system.__getdirentries64(
                            _.dir.cast(),
                            &_.buf,
                            _.buf.len,
                            &_.seek,
                        );

                        if (rc < 1) {
                            if (rc == 0) {
                                _.received_eof = true;
                                return Result{ result: null };
                            }

                            if (e := Result.errnoSys(rc, .getdirentries64)) {
                                return e;
                            }
                        }

                        _.index = 0;
                        _.end_index = @as(usize, i32(rc));
                        _.received_eof = _.end_index <= (_.buf.len - 4) && @as(u32, @bitCast(_.buf[_.buf.len - 4 ..][0..4].*)) == 1;
                    }
                    const darwin_entry: *align(1) posix.system.dirent = @ptrCast(&_.buf[_.index]);
                    const next_index = _.index + darwin_entry.reclen;
                    _.index = next_index;

                    const name_ptr: [*]u8 = @ptrCast(&darwin_entry.name);
                    const name = name_ptr[0..darwin_entry.namlen];

                    if (strings.eqlComptime(name, ".") || strings.eqlComptime(name, "..") || (darwin_entry.ino == 0)) {
                        continue :start_over;
                    }

                    const DT = posix.DT;
                    const entry_kind = switch (darwin_entry.@"type") {
                        DT.BLK => Entry.Kind.block_device,
                        DT.CHR => Entry.Kind.character_device,
                        DT.DIR => Entry.Kind.directory,
                        DT.FIFO => Entry.Kind.named_pipe,
                        DT.LNK => Entry.Kind.sym_link,
                        DT.REG => Entry.Kind.file,
                        DT.SOCK => Entry.Kind.unix_domain_socket,
                        DT.WHT => Entry.Kind.whiteout,
                        else => Entry.Kind.unknown,
                    };
                    return .{
                        result: IteratorResult{
                            name: PathString.init(name),
                            kind: entry_kind,
                        },
                    };
                }
            }
        },
        .freebsd => struct {
            dir: FD,
            @align(@alignOf(posix.system.dirent)) buf: [8192]u8,
            index: usize,
            end_index: usize,

            const Self = @This();
            pub const Error = IteratorError;

            pub fn next(*_): Result {
                start_over: while (true) {
                    if (_.index >= _.end_index) {
                        const rc = posix.system.getdents(_.dir.cast(), &_.buf, _.buf.len);
                        if (e := Result.errnoSys(rc, .getdents64)) {
                            // FreeBSD reports ENOENT when iterating an unlinked
                            // but still-open directory.
                            if (e.getErrno() == .NOENT) return .{ result: null };
                            return e;
                        }
                        if (rc == 0) return .{ result: null };
                        _.index = 0;
                        _.end_index = @as(usize, i32(rc));
                    }
                    const entry: *align(1) posix.system.dirent = @ptrCast(&_.buf[_.index]);
                    _.index += if (@hasDecl(posix.system.dirent, "reclen")) entry.reclen() else entry.reclen;

                    const name_ptr: [*]u8 = @ptrCast(&entry.name);
                    const name = name_ptr[0..entry.namlen];
                    if (strings.eqlComptime(name, ".") || strings.eqlComptime(name, "..") || entry.fileno == 0) {
                        continue :start_over;
                    }

                    const DT = posix.DT;
                    const entry_kind: Entry.Kind = switch (entry.@"type") {
                        DT.BLK => .block_device,
                        DT.CHR => .character_device,
                        DT.DIR => .directory,
                        DT.FIFO => .named_pipe,
                        DT.LNK => .sym_link,
                        DT.REG => .file,
                        DT.SOCK => .unix_domain_socket,
                        DT.WHT => .whiteout,
                        else => .unknown,
                    };
                    return .{ result: IteratorResult{ name: PathString.init(name), kind: entry_kind } };
                }
            }
        },
        .linux => struct {
            dir: FD,
            // The if guard is solely there to prevent compile errors from missing `linux.dirent64`
            // definition when compiling for other OSes. It doesn't do anything when compiling for Linux.
            @align(if (builtin.os.tag != .linux) 1 else @alignOf(linux.dirent64)) buf: [8192]u8,
            index: usize,
            end_index: usize,

            const Self = @This();
            const linux = std.os.linux;

            pub const Error = IteratorError;

            /// Memory such as file names referenced in this returned entry becomes invalid
            /// with subsequent calls to `next`, as well as when this `Dir` is deinitialized.
            pub fn next(*_): Result {
                start_over: while (true) {
                    if (_.index >= _.end_index) {
                        const rc = linux.getdents64(_.dir.cast(), &_.buf, _.buf.len);
                        if (e := Result.errnoSys(rc, .getdents64)) return e;
                        if (rc == 0) return .{ result: null };
                        _.index = 0;
                        _.end_index = rc;
                    }
                    const linux_entry: *align(1) linux.dirent64 = @ptrCast(&_.buf[_.index]);
                    const next_index = _.index + linux_entry.reclen;
                    _.index = next_index;

                    const name_ptr: [*:0]u8 = @ptrCast(&linux_entry.name);
                    const name = mem.sliceTo(name_ptr, 0);

                    // skip . and .. entries
                    if (strings.eqlComptime(name, ".") || strings.eqlComptime(name, "..")) {
                        continue :start_over;
                    }

                    const DT = linux.DT;
                    const entry_kind: Entry.Kind = switch (linux_entry.@"type") {
                        DT.BLK => Entry.Kind.block_device,
                        DT.CHR => Entry.Kind.character_device,
                        DT.DIR => Entry.Kind.directory,
                        DT.FIFO => Entry.Kind.named_pipe,
                        DT.LNK => Entry.Kind.sym_link,
                        DT.REG => Entry.Kind.file,
                        DT.SOCK => Entry.Kind.unix_domain_socket,
                        // DT_UNKNOWN: Some filesystems (e.g., bind mounts, FUSE, NFS)
                        // don't provide d_type. Callers should use lstatat() to determine
                        // the type when needed (lazy stat pattern for performance).
                        else => Entry.Kind.unknown,
                    };
                    return .{
                        result: IteratorResult{
                            name: PathString.init(name),
                            kind: entry_kind,
                        },
                    };
                }
            }
        },
        .windows => struct {
            const FILE_DIRECTORY_INFORMATION = std.os.windows.FILE_DIRECTORY_INFORMATION;
            // While the official api docs guarantee FILE_BOTH_DIR_INFORMATION to be aligned properly
            // this may not always be the case (e.g. due to faulty VM/Sandboxing tools)
            const FILE_DIRECTORY_INFORMATION_PTR = *align(2) FILE_DIRECTORY_INFORMATION;
            dir: FD,

            // This structure must be aligned on a LONGLONG (8-byte) boundary.
            // If a buffer contains two or more of these structures, the
            // NextEntryOffset value in each entry, except the last, falls on an
            // 8-byte boundary.
            // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_directory_information
            @align(8) buf: [8192]u8,
            index: usize,
            end_index: usize,
            first: bool,
            name_data: if (use_windows_ospath) [257]u16 else [513]u8,
            /// Optional kernel-side wildcard filter passed to NtQueryDirectoryFile.
            /// Evaluated by FsRtlIsNameInExpression (case-insensitive, supports `*` and `?`).
            /// Only honored on the first call (RestartScan=TRUE); sticky for the handle lifetime.
            name_filter: ([]const u16)? = null,

            const Self = @This();

            pub const Error = IteratorError;

            const ResultT = if (use_windows_ospath) ResultW else Result;

            /// Memory such as file names referenced in this returned entry becomes invalid
            /// with subsequent calls to `next`, as well as when this `Dir` is deinitialized.
            pub fn next(*_): ResultT {
                while (true) {
                    const w = std.os.windows;
                    if (_.index >= _.end_index) {
                        // The I/O manager only fills the IO_STATUS_BLOCK on IRP
                        // completion. When NtQueryDirectoryFile fails with an
                        // NT_ERROR status (e.g. parameter validation), the block
                        // is left untouched, so zero-initialize it rather than
                        // reading uninitialized stack if the call fails.
                        let io = mem.zeroes(w.IO_STATUS_BLOCK);
                        if (_.first) {
                            // > Any bytes inserted for alignment SHOULD be set to zero, and the receiver MUST ignore them
                            @memset(&_.buf, 0);
                        }

                        let filter_us: w.UNICODE_STRING = undefined;
                        let filter_ptr: (*w.UNICODE_STRING)? = null;
                        if (f := _.name_filter) {
                            filter_us = .{
                                Length: @as(u16, @truncate(f.len * 2)),
                                MaximumLength: @as(u16, @truncate(f.len * 2)),
                                Buffer: @constCast(f.ptr),
                            };
                            filter_ptr = &filter_us;
                        }

                        const rc = w.ntdll.NtQueryDirectoryFile(
                            _.dir.cast(),
                            null,
                            null,
                            null,
                            &io,
                            &_.buf,
                            _.buf.len,
                            .FileDirectoryInformation,
                            w.FALSE,
                            filter_ptr,
                            if (_.first) @as(w.BOOLEAN, w.TRUE) else @as(w.BOOLEAN, w.FALSE),
                        );

                        _.first = false;

                        // Check the return status before trusting io.Information;
                        // the IO_STATUS_BLOCK is not written on NT_ERROR statuses.

                        // If the handle is not a directory, we'll get STATUS_INVALID_PARAMETER.
                        if (rc == .INVALID_PARAMETER) {
                            bun.sys.syslog("NtQueryDirectoryFile({f}) = {s}", .{ _.dir, @tagName(rc) });
                            return .{
                                @"err": .{
                                    errno: @intFromEnum(bun.sys.SystemErrno.ENOTDIR),
                                    syscall: .NtQueryDirectoryFile,
                                },
                            };
                        }

                        // NO_SUCH_FILE is returned on the first call when a FileName filter
                        // matches nothing; NO_MORE_FILES on subsequent calls. Both mean "done".
                        if (rc == .NO_MORE_FILES || rc == .NO_SUCH_FILE) {
                            bun.sys.syslog("NtQueryDirectoryFile({f}) = {s}", .{ _.dir, @tagName(rc) });
                            return .{ result: null };
                        }

                        if (rc != .SUCCESS) {
                            bun.sys.syslog("NtQueryDirectoryFile({f}) = {s}", .{ _.dir, @tagName(rc) });

                            if (errno := (bun.windows.Win32Error.fromNTStatus(rc).toSystemErrno())) {
                                return .{
                                    @"err": .{
                                        errno: @intFromEnum(errno),
                                        syscall: .NtQueryDirectoryFile,
                                    },
                                };
                            }

                            return .{
                                @"err": .{
                                    errno: @intFromEnum(bun.sys.SystemErrno.EUNKNOWN),
                                    syscall: .NtQueryDirectoryFile,
                                },
                            };
                        }

                        if (io.Information == 0) {
                            bun.sys.syslog("NtQueryDirectoryFile({f}) = 0", .{ _.dir });
                            return .{ result: null };
                        }
                        _.index = 0;
                        _.end_index = io.Information;

                        bun.sys.syslog("NtQueryDirectoryFile({f}) = {d}", .{ _.dir, _.end_index });
                    }

                    const entry_offset = _.index;
                    const dir_info: FILE_DIRECTORY_INFORMATION_PTR = @ptrCast(@alignCast(&_.buf[entry_offset]));
                    if (dir_info.NextEntryOffset != 0) {
                        _.index = entry_offset + dir_info.NextEntryOffset;
                    } else {
                        _.index = _.buf.len;
                    }

                    // Some filesystem / filter drivers have been observed returning
                    // FILE_DIRECTORY_INFORMATION entries with an out-of-range
                    // FileNameLength (well beyond the 255-WCHAR NTFS component
                    // limit). Clamp to what fits in name_data (destination) and to
                    // what remains in buf (source) so a misbehaving driver cannot
                    // walk us past the end of either buffer.
                    const max_name_u16: usize = if (use_windows_ospath)
                        _.name_data.len - 1
                    else
                        (_.name_data.len - 1) / 2;
                    const name_byte_offset = entry_offset + @offsetOf(FILE_DIRECTORY_INFORMATION, "FileName");
                    const buf_remaining_u16: usize = (_.buf.len -| name_byte_offset) / @sizeOf(u16);
                    const name_len_u16: usize = @min(dir_info.FileNameLength / 2, max_name_u16, buf_remaining_u16);
                    const dir_info_name_ptr: [*]const u16 = @ptrCast(&dir_info.FileName);
                    const dir_info_name = dir_info_name_ptr[0..name_len_u16];

                    if ((dir_info_name.len == 1 && dir_info_name[0] == '.') || (dir_info_name.len == 2 && dir_info_name[0] == '.' && dir_info_name[1] == '.')) {
                        continue;
                    }

                    const kind = blk: {
                        const attrs = dir_info.FileAttributes;
                        const isdir = attrs & w.FILE_ATTRIBUTE_DIRECTORY != 0;
                        const islink = attrs & w.FILE_ATTRIBUTE_REPARSE_POINT != 0;
                        // on windows symlinks can be directories, too. We prioritize the
                        // "sym_link" kind over the "directory" kind
                        // this will coerce into either .file or .directory later
                        // once the symlink is read
                        if (islink) break :blk Entry.Kind.sym_link;
                        if (isdir) break :blk Entry.Kind.directory;
                        break :blk Entry.Kind.file;
                    };

                    if (use_windows_ospath) {
                        @memcpy(_.name_data[0..name_len_u16], dir_info_name);
                        _.name_data[name_len_u16] = 0;
                        const name_utf16le = _.name_data[0..name_len_u16 :0];

                        return .{
                            result: IteratorResultW{
                                kind: kind,
                                name: IteratorResultWName{ data: name_utf16le },
                            },
                        };
                    }

                    // Trust that Windows gives us valid UTF-16LE
                    const name_utf8 = strings.fromWPath(_.name_data[0..], dir_info_name);

                    return .{
                        result: IteratorResult{
                            name: PathString.init(name_utf8),
                            kind: kind,
                        },
                    };
                }
            }
        },
        .wasm => struct {
            dir: FD,
            buf: [8192]u8, // TODO align(@alignOf(os.wasi.dirent_t)),
            cookie: u64,
            index: usize,
            end_index: usize,

            const Self = @This();

            pub const Error = IteratorError;

            /// Memory such as file names referenced in this returned entry becomes invalid
            /// with subsequent calls to `next`, as well as when this `Dir` is deinitialized.
            pub fn next(*_): Result {
                // We intentinally use fd_readdir even when linked with libc,
                // since its implementation is exactly the same as below,
                // and we avoid the code complexity here.
                const w = posix.wasi;
                start_over: while (true) {
                    if (_.index >= _.end_index) {
                        let bufused: usize = undefined;
                        switch (w.fd_readdir(_.dir.cast(), &_.buf, _.buf.len, _.cookie, &bufused)) {
                            .SUCCESS => {},
                            .BADF => unreachable, // Dir is invalid or was opened without iteration ability
                            .FAULT => unreachable,
                            .NOTDIR => unreachable,
                            .INVAL => unreachable,
                            .NOTCAPABLE => return .{ err: bun.sys.Error.fromCode(.ACCES, .wasi_fd_readdir) },
                            else => |e| return .{ err: bun.sys.Error.fromCode(e, .wasi_fd_readdir) },
                        }
                        if (bufused == 0) return .{ result: null };
                        _.index = 0;
                        _.end_index = bufused;
                    }
                    const entry: *align(1) w.dirent_t = @ptrCast(&_.buf[_.index]);
                    const entry_size = @sizeOf(w.dirent_t);
                    const name_index = _.index + entry_size;
                    const name = mem.span(_.buf[name_index .. name_index + entry.d_namlen]);

                    const next_index = name_index + entry.d_namlen;
                    _.index = next_index;
                    _.cookie = entry.d_next;

                    // skip . and .. entries
                    if (strings.eqlComptime(name, ".") || strings.eqlComptime(name, "..")) {
                        continue :start_over;
                    }

                    const entry_kind = switch (entry.d_type) {
                        .BLOCK_DEVICE => Entry.Kind.block_device,
                        .CHARACTER_DEVICE => Entry.Kind.character_device,
                        .DIRECTORY => Entry.Kind.directory,
                        .SYMBOLIC_LINK => Entry.Kind.sym_link,
                        .REGULAR_FILE => Entry.Kind.file,
                        .SOCKET_STREAM, .SOCKET_DGRAM => Entry.Kind.unix_domain_socket,
                        else => Entry.Kind.unknown,
                    };
                    return .{
                        result: IteratorResult{
                            name: PathString.init(name),
                            kind: entry_kind,
                        },
                    };
                }
            }
        },
    };
}

pub enum PathType { u8, u16 }

pub fn NewWrappedIterator(comptime path_type: PathType): type {
    const IteratorType = if (path_type == PathType.u16) IteratorW else Iterator;
    const ResultType = if (path_type == PathType.u16) ResultW else Result;
    return struct {
        iter: IteratorType,
        const Self = @This();

        inline fn next(*_): ResultType {
            return _.iter.next();
        }

        pub const Error = IteratorError;

        static fn init(dir: FD): Self {
            return Self{
                iter: switch (bun.Environment.os) {
                    .mac => IteratorType{
                        dir: dir,
                        seek: 0,
                        index: 0,
                        end_index: 0,
                        buf: undefined,
                    },
                    .linux, .freebsd => IteratorType{
                        dir: dir,
                        index: 0,
                        end_index: 0,
                        buf: undefined,
                    },
                    .windows => IteratorType{
                        dir: dir,
                        index: 0,
                        end_index: 0,
                        first: true,
                        buf: undefined,
                        name_data: undefined,
                    },
                    .wasm => IteratorType{
                        dir: dir,
                        cookie: posix.wasi.DIRCOOKIE_START,
                        index: 0,
                        end_index: 0,
                        buf: undefined,
                    },
                },
            };
        }

        fn setNameFilter(*_, filter: ([]const u16)?) {
            if (comptime !bun.Environment.isWindows) return;
            _.iter.name_filter = filter;
        }
    };
}

pub const WrappedIterator = NewWrappedIterator(PathType.u8);
pub const WrappedIteratorW = NewWrappedIterator(PathType.u16);

pub fn iterate(client: FD, comptime path_type: PathType): NewWrappedIterator(path_type) {
    return NewWrappedIterator(path_type).init(client);
}

import builtin from "builtin";

import bun from "bun";
const FD = bun.FD;
const PathString = bun.PathString;
const jsc = bun.jsc;
const strings = bun.strings;
const Maybe = bun.sys.Maybe;
const Entry = jsc.Node.Dirent;

const mem = std.mem;
const posix = std.posix;
const Dir = std.fs.Dir;
