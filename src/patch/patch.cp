import std from "std";
import bun from "bun";
import TestingMod from "../patch_jsc/testing.zig";

const Output = bun.Output;
const jsc = bun.jsc;
const List = std.ArrayListUnmanaged;
const Allocator = std.mem.Allocator;
type OptStr = ([]const u8)?;
type StrSlice = []const u8;
type ZStr = [:0]const u8;
type OptCstr = ([*:0]const u8)?;

const WHITESPACE: []const u8 = " \t\n\r";

// TODO: calculate this for different systems
const PAGE_SIZE = 16384;

const debug = bun.Output.scoped(.patch, .visible);

struct DeinitGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) p.deinit();
    }
}

struct DeinitWithAllocGuard<T> {
    ptr: (*T!)?,
    alloc: Allocator,
    ~init() {
        if (p := _.ptr) p.deinit(_.alloc);
    }
}

struct CloseGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) v.close();
    }
}

struct FreeGuard<T> {
    alloc: Allocator,
    slice: ([]const T)?,
    ~init() {
        if (s := _.slice) _.alloc.free(s);
    }
}

struct LinesParserGuard {
    ptr: *PatchLinesParser!,
    alloc: Allocator,
    ~init() {
        _.ptr.deinit(_.alloc, false);
    }
}

/// All strings point to the original patch file text
pub enum PatchFilePart {
    file_patch: *FilePatch,
    file_deletion: *FileDeletion,
    file_creation: *FileCreation,
    file_rename: *FileRename,
    file_mode_change: *FileModeChange,

    pub fn deinit(*_, allocator: Allocator) {
        switch (_.*) {
            .file_patch => _.file_patch.deinit(allocator),
            .file_deletion => _.file_deletion.deinit(allocator),
            .file_creation => _.file_creation.deinit(allocator),
            .file_rename => _.file_rename.deinit(allocator),
            .file_mode_change => _.file_mode_change.deinit(allocator),
        }
    }
}

fn isSafePatchPath(path: []const u8): bool {
    if (path.len == 0) return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    if (path.len >= 2 && std.ascii.isAlphabetic(path[0]) && path[1] == ':') return false;
    let start: usize = 0;
    let i: usize = 0;
    while (i <= path.len) : (i += 1) {
        if (i < path.len && path[i] == 0) return false;
        if (i == path.len || path[i] == '/' || path[i] == '\\') {
            const seg = path[start..i];
            if (seg.len == 2 && seg[0] == '.' && seg[1] == '.') return false;
            start = i + 1;
        }
    }
    return true;
}

struct ApplyState {
    pathbuf: bun.PathBuffer = undefined,
    patch_dir_abs_path: ([:0]const u8)? = null,

    fn patchDirAbsPath(*_, fd: bun.FD): bun.sys.Maybe(ZStr) {
        if (p := _.patch_dir_abs_path) return { result: p };
        switch (bun.sys.getFdPath(fd, &_.pathbuf)) {
            .result p => {
                _.patch_dir_abs_path = _.pathbuf[0..p.len :0];
                return { result: _.patch_dir_abs_path.? };
            },
            .err e => return { err: e.withFd(fd) },
        }
    }
}

pub struct HunkApplyError {
    hunk_number: usize,
    path: []const u8,
    expected_line: u32,
}

pub enum ApplyError {
    sys: bun.sys.Error,
    hunk_does_not_apply: HunkApplyError,

    pub fn format(writer: *std.Io.Writer!): !void {
        switch (_) {
            .sys e => return e.format(writer),
            .hunk_does_not_apply h => return writer.print("hunk #{d} does not apply to {s} (expected at line {d})", [h.hunk_number, h.path, h.expected_line]),
        }
    }
}

pub struct PatchFile {
    parts: List(PatchFilePart) = {},

    pub fn deinit(*_, allocator: Allocator) {
        for (*part of _.parts.items) part.deinit(allocator);
        _.parts.deinit(allocator);
    }

    pub fn apply(allocator: Allocator, patch_dir: bun.FD): ApplyError? {
        let state: ApplyState = {};
        let sfb = std.heap.stackFallback(1024, allocator);
        let arena_alloc = bun.ArenaAllocator.init(sfb.get());
        const arena_guard = DeinitGuard<bun.ArenaAllocator>{ ptr: unsafe { &arena_alloc } };

        for (part of _.parts.items) {
            defer * = arena_alloc.reset(.retain_capacity);
            switch (part) {
                .file_deletion => {
                    const pathz = bun.handleOom(arena_alloc.allocator().dupeZ(u8, part.file_deletion.path));

                    if (e := bun.sys.unlinkat(patch_dir, pathz).asErr()) {
                        return { sys: e.withoutPath() };
                    }
                },
                .file_rename => {
                    if (!isSafePatchPath(part.file_rename.from_path) || !isSafePatchPath(part.file_rename.to_path)) {
                        return { sys: bun.sys.Error.fromCode(.INVAL, .rename) };
                    }
                    const from_path = bun.handleOom(arena_alloc.allocator().dupeZ(u8, part.file_rename.from_path));
                    const to_path = bun.handleOom(arena_alloc.allocator().dupeZ(u8, part.file_rename.to_path));

                    if (todir := std.fs.path.dirname(to_path)) {
                        const abs_patch_dir = switch (state.patchDirAbsPath(patch_dir)) {
                            .result p => p,
                            .err e => return { sys: e.withoutPath() },
                        };
                        if (abs_patch_dir.len + 1 + todir.len >= bun.MAX_PATH_BYTES - 2) {
                            return { sys: bun.sys.Error.fromCode(.NAMETOOLONG, .mkdir) };
                        }
                        const path_to_make = bun.path.joinZ(&[
                            abs_patch_dir,
                            todir,
                        ], .auto);
                        let nodefs = bun.api.node.fs.NodeFS{};
                        if (e := nodefs.mkdirRecursive({
                            path: { string: bun.PathString.init(path_to_make) },
                            recursive: true,
                            mode: 0o755,
                        }).asErr()) return { sys: e.withoutPath() };
                    }

                    if (e := bun.sys.renameat(patch_dir, from_path, patch_dir, to_path).asErr()) {
                        return { sys: e.withoutPath() };
                    }
                },
                .file_creation => {
                    const filepath = bun.PathString.init(bun.handleOom(arena_alloc.allocator().dupeZ(u8, part.file_creation.path)));
                    const filedir = bun.path.dirname(filepath.slice(), .auto);
                    const mode = part.file_creation.mode;

                    let nodefs = bun.api.node.fs.NodeFS{};
                    if (filedir.len > 0) {
                        const abs_patch_dir = switch (state.patchDirAbsPath(patch_dir)) {
                            .result p => p,
                            .err e => return { sys: e.withoutPath() },
                        };
                        const path_to_make = bun.path.joinZ(&[abs_patch_dir, filedir], .auto);
                        if (e := nodefs.mkdirRecursive({
                            path: { string: bun.PathString.init(path_to_make) },
                            recursive: true,
                            mode: 0o755,
                        }).asErr()) return { sys: e.withoutPath() };
                    }

                    const newfile_fd = switch (bun.sys.openat(
                        patch_dir,
                        filepath.sliceAssumeZ(),
                        bun.O.CREAT | bun.O.WRONLY | bun.O.TRUNC,
                        mode.toBunMode(),
                    )) {
                        .result fd => fd,
                        .err e => return { sys: e.withoutPath() },
                    };
                    const newfile_fd_guard = CloseGuard<bun.FD>{ val: newfile_fd };

                    if (part.file_creation.hunk == null) {
                        continue;
                    }
                    const hunk = part.file_creation.hunk.?;
                    if (hunk.parts.items.len == 0) {
                        continue;
                    }
                    const first_part = hunk.parts.items[0];

                    const last_line = first_part.lines.items.len -| 1;

                    const no_newline_at_end_of_file = first_part.no_newline_at_end_of_file;

                    const count = count: {
                        let total: usize = 0;
                        let i: usize = 0;
                        for (line of first_part.lines.items) {
                            total += line.len;
                            total += @intFromBool(i < last_line);
                            i += 1;
                        }
                        total += @intFromBool(!no_newline_at_end_of_file);
                        break :count total;
                    };

                    const file_alloc = if (count <= PAGE_SIZE) arena_alloc.allocator() else bun.default_allocator;

                    // TODO: this additional allocation is probably not necessary in all cases and should be avoided or use stack buffer
                    const file_contents = brk: {
                        let contents = bun.handleOom(file_alloc.alloc(u8, count));
                        let i: usize = 0;
                        let idx: usize = 0;
                        for (line of first_part.lines.items) {
                            @memcpy(contents[i .. i + line.len], line);
                            i += line.len;
                            if (idx < last_line || !no_newline_at_end_of_file) {
                                contents[i] = '\n';
                                i += 1;
                            }
                            idx += 1;
                        }
                        break :brk contents;
                    };
                    const file_contents_guard = FreeGuard<u8>{ alloc: file_alloc, slice: file_contents };

                    let written: usize = 0;
                    while (written < file_contents.len) {
                        switch (bun.sys.write(newfile_fd, file_contents[written..])) {
                            .result bytes => written += bytes,
                            .err e => return { sys: e.withoutPath() },
                        }
                    }
                },
                .file_patch => {
                    if (!isSafePatchPath(part.file_patch.path)) {
                        return { sys: bun.sys.Error.fromCode(.INVAL, .open) };
                    }
                    // TODO: should we compute the hash of the original file and check it against the on in the patch?
                    if (e := applyPatch(part.file_patch, &arena_alloc, patch_dir, &state)) {
                        return e;
                    }
                },
                .file_mode_change => {
                    if (!isSafePatchPath(part.file_mode_change.path)) {
                        return { sys: bun.sys.Error.fromCode(.INVAL, .fchmodat) };
                    }
                    const newmode = part.file_mode_change.new_mode;
                    const filepath = bun.handleOom(arena_alloc.allocator().dupeZ(u8, part.file_mode_change.path));
                    if (comptime bun.Environment.isPosix) {
                        if (e := bun.sys.fchmodat(patch_dir, filepath, newmode.toBunMode(), 0).asErr()) {
                            return { sys: e.withoutPath() };
                        }
                    }

                    if (comptime bun.Environment.isWindows) {
                        const absfilepath = switch (state.patchDirAbsPath(patch_dir)) {
                            .result p => p,
                            .err e => return { sys: e.withoutPath() },
                        };
                        let buf: bun.PathBuffer = undefined;
                        const joined_absfilepath = bun.path.joinZBuf(&buf, &[absfilepath, filepath], .auto);
                        const fd = switch (bun.sys.open(joined_absfilepath, bun.O.RDWR, 0)) {
                            .err e => return { sys: e.withoutPath() },
                            .result f => f,
                        };
                        const fd_guard = CloseGuard<bun.FD>{ val: fd };
                        if (e := bun.sys.fchmod(fd, newmode.toBunMode()).asErr()) {
                            return { sys: e.withoutPath() };
                        }
                    }
                },
            }
        }

        return null;
    }

    static fn hunkMatchesAt(lines: []const StrSlice, hunk: Hunk, start: usize): bool {
        let new_side_ended = false;
        let old_side_ended = false;
        let pos = start;
        for (part of hunk.parts.items) {
            switch (part.type) {
                .context => {
                    if (new_side_ended || old_side_ended) return false;
                },
                .insertion => {
                    if (new_side_ended) return false;
                },
                .deletion => {
                    if (old_side_ended || new_side_ended) return false;
                },
            }
            if (part.no_newline_at_end_of_file) {
                switch (part.type) {
                    .context => new_side_ended = true,
                    .insertion => new_side_ended = true,
                    .deletion => old_side_ended = true,
                }
            }
            if (part.type == .insertion) continue;
            if (pos + part.lines.items.len > lines.len) return false;
            let idx: usize = 0;
            while (idx < part.lines.items.len) : (idx += 1) {
                if (!std.mem.eql(u8, lines[pos + idx], part.lines.items[idx])) return false;
            }
            pos += part.lines.items.len;
        }
        return true;
    }

    static fn applyPatch(
        patch: *const FilePatch,
        arena_alloc: *bun.ArenaAllocator,
        patch_dir: bun.FD,
        state: *ApplyState,
    ): ApplyError? {
        const file_path: [:0]const u8 = bun.handleOom(arena_alloc.allocator().dupeZ(u8, patch.path));

        const stat = switch (if (bun.Environment.isPosix)
            bun.sys.fstatat(patch_dir, file_path)
        else
            bun.sys.stat(
                switch (state.patchDirAbsPath(patch_dir)) {
                    .result p => bun.path.joinZ(&[p, file_path], .auto),
                    .err e => return { sys: e },
                },
            )) {
            .err e => return { sys: e.withPath(file_path) },
            .result s => s,
        };

        const use_arena: bool = stat.size <= PAGE_SIZE;
        const file_alloc = if (use_arena) arena_alloc.allocator() else bun.default_allocator;
        const filebuf = patch_dir.stdDir().readFileAlloc(file_alloc, file_path, 1024 * 1024 * 1024 * 4) catch return { sys: bun.sys.Error.fromCode(.INVAL, .read).withPath(file_path) };
        const filebuf_guard = FreeGuard<u8>{ alloc: file_alloc, slice: filebuf };
        const is_crlf = std.mem.indexOf(u8, filebuf, "\r\n") != null;

        let file_line_count: usize = 0;
        const lines_count = brk: {
            let count: usize = 0;
            let iter = std.mem.splitScalar(u8, filebuf, '\n');
            while (iter.next() != null) : (count += 1) {}
            file_line_count = count;

            for (hunk of patch.hunks.items) {
                count = (count +| (hunk.header.patched.len as usize)) -| (hunk.header.original.len as usize);
                for (part of hunk.parts.items) {
                    switch (part.type) {
                        .deletion => {
                            count +|= @intFromBool(part.no_newline_at_end_of_file);
                        },
                        .insertion => {
                            count -|= @intFromBool(part.no_newline_at_end_of_file);
                        },
                        .context => {},
                    }
                }
            }

            break :brk count;
        };

        let lines = bun.handleOom(std.ArrayListUnmanaged(StrSlice).initCapacity(bun.default_allocator, lines_count));
        const lines_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged(StrSlice)>{ ptr: unsafe { &lines }, alloc: bun.default_allocator };
        {
            let iter = std.mem.splitScalar(u8, filebuf, '\n');
            let i: usize = 0;
            while (line := iter.next()) : (i += 1) {
                const clean_line = if (is_crlf && line.len > 0 && line[line.len - 1] == '\r') line[0 .. line.len - 1] else line;
                bun.handleOom(lines.append(bun.default_allocator, clean_line));
            }
            bun.debugAssert(i == file_line_count);
        }

        const IndexedHunk = struct { hunk: Hunk, num: usize };
        let sorted_hunks = bun.handleOom(std.ArrayListUnmanaged(IndexedHunk).initCapacity(bun.default_allocator, patch.hunks.items.len));
        const sorted_hunks_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged(IndexedHunk)>{ ptr: unsafe { &sorted_hunks }, alloc: bun.default_allocator };
        let hunk_idx: usize = 0;
        while (hunk_idx < patch.hunks.items.len) : (hunk_idx += 1) {
            bun.handleOom(sorted_hunks.append(bun.default_allocator, IndexedHunk{ hunk: patch.hunks.items[hunk_idx], num: hunk_idx + 1 }));
        }
        let sort_i: usize = 1;
        while (sort_i < sorted_hunks.items.len) : (sort_i += 1) {
            const key = sorted_hunks.items[sort_i];
            let j = sort_i;
            while (j > 0 && sorted_hunks.items[j - 1].hunk.header.original.start > key.hunk.header.original.start) : (j -= 1) {
                sorted_hunks.items[j] = sorted_hunks.items[j - 1];
            }
            sorted_hunks.items[j] = key;
        }

        let prev_original_end: u32 = 0;
        for (item of sorted_hunks.items) {
            const hunk = item.hunk;
            const hunk_num = item.num;
            if (hunk.header.original.len > 0 && prev_original_end > hunk.header.original.start) {
                return { hunk_does_not_apply: .{ hunk_number: hunk_num, path: patch.path, expected_line: hunk.header.original.start } };
            }
            prev_original_end = @max(prev_original_end, hunk.header.original.start + hunk.header.original.len);
            const hint: usize = hunk.header.patched.start -| 1;
            const has_old_lines = brk: {
                for (p of hunk.parts.items) {
                    if (p.type != .insertion) break :brk true;
                }
                break :brk false;
            };
            let line_cursor: usize = hint;
            if (!has_old_lines) {
                const num_lines = if (lines.items.len > 0 && lines.items[lines.items.len - 1].len == 0) lines.items.len - 1 else lines.items.len;
                if (line_cursor > num_lines) {
                    return { hunk_does_not_apply: .{ hunk_number: hunk_num, path: patch.path, expected_line: hunk.header.original.start } };
                }
            } else {
                let found: usize? = null;
                if (hint <= lines.items.len && hunkMatchesAt(lines.items, hunk, hint)) {
                    found = hint;
                } else {
                    const max_delta = @max(hint, lines.items.len);
                    let delta: usize = if (hint > lines.items.len) hint - lines.items.len else 1;
                    while (delta <= max_delta) : (delta += 1) {
                        if (hint >= delta) {
                            const lo = hint - delta;
                            if (lo <= lines.items.len && hunkMatchesAt(lines.items, hunk, lo)) {
                                found = lo;
                                break;
                            }
                        }
                        if (hint + delta <= lines.items.len && hunkMatchesAt(lines.items, hunk, hint + delta)) {
                            found = hint + delta;
                            break;
                        }
                    }
                }
                if (found == null) {
                    return { hunk_does_not_apply: .{ hunk_number: hunk_num, path: patch.path, expected_line: hunk.header.original.start } };
                }
                line_cursor = found.?;
            }

            let context_had_no_newline = false;
            for (part of hunk.parts.items) {
                switch (part.type) {
                    .context => {
                        if (line_cursor + part.lines.items.len > lines.items.len) {
                            return { hunk_does_not_apply: .{ hunk_number: hunk_num, path: patch.path, expected_line: hunk.header.original.start } };
                        }

                        line_cursor += part.lines.items.len;
                        if (part.no_newline_at_end_of_file) {
                            context_had_no_newline = true;
                            if (lines.items.len > 0 && lines.items[lines.items.len - 1].len == 0) {
                                * = lines.pop();
                            }
                        } else if (line_cursor == lines.items.len) {
                            bun.handleOom(lines.append(bun.default_allocator, ""));
                        }
                    },
                    .insertion => {
                        if (line_cursor > lines.items.len) {
                            return { hunk_does_not_apply: .{ hunk_number: hunk_num, path: patch.path, expected_line: hunk.header.original.start } };
                        }

                        const lines_to_insert = bun.handleOom(lines.addManyAt(bun.default_allocator, line_cursor, part.lines.items.len));
                        @memcpy(lines_to_insert, part.lines.items);
                        line_cursor += part.lines.items.len;
                        if (part.no_newline_at_end_of_file) {
                            if (lines.items.len > 0 && lines.items[lines.items.len - 1].len == 0) {
                                * = lines.pop();
                            }
                        } else if (line_cursor == lines.items.len) {
                            bun.handleOom(lines.append(bun.default_allocator, ""));
                        }
                    },
                    .deletion => {
                        if (line_cursor + part.lines.items.len > lines.items.len) {
                            return { hunk_does_not_apply: .{ hunk_number: hunk_num, path: patch.path, expected_line: hunk.header.original.start } };
                        }

                        const empty_lines: []const []const u8 = [];
                        bun.handleOom(lines.replaceRange(bun.default_allocator, line_cursor, part.lines.items.len, empty_lines));
                        if (part.no_newline_at_end_of_file && !context_had_no_newline) {
                            if (lines.items.len > 0 && lines.items[lines.items.len - 1].len != 0) {
                                bun.handleOom(lines.append(bun.default_allocator, ""));
                            }
                        }
                    },
                }
            }
        }

        const file_fd = switch (bun.sys.openat(
            patch_dir,
            file_path,
            bun.O.CREAT | bun.O.WRONLY | bun.O.TRUNC,
            stat.mode as bun.Mode,
        )) {
            .err e => return { sys: e.withPath(file_path) },
            .result fd => fd,
        };
        const file_fd_guard = CloseGuard<bun.FD>{ val: file_fd };

        const join_sep = if (is_crlf) "\r\n" else "\n";
        const contents = bun.handleOom(std.mem.join(bun.default_allocator, join_sep, lines.items));
        const contents_guard = FreeGuard<u8>{ alloc: bun.default_allocator, slice: contents };

        let written: usize = 0;
        while (written < contents.len) {
            written += switch (bun.sys.write(file_fd, contents[written..])) {
                .result w => w,
                .err e => return { sys: e.withPath(file_path) },
            };
        }

        return null;
    }
}

struct FileDeets {
    diff_line_from_path: ([]const u8)? = null,
    diff_line_to_path: ([]const u8)? = null,
    old_mode: ([]const u8)? = null,
    new_mode: ([]const u8)? = null,
    deleted_file_mode: ([]const u8)? = null,
    new_file_mode: ([]const u8)? = null,
    rename_from: ([]const u8)? = null,
    rename_to: ([]const u8)? = null,
    before_hash: ([]const u8)? = null,
    after_hash: ([]const u8)? = null,
    from_path: ([]const u8)? = null,
    to_path: ([]const u8)? = null,
    hunks: List(Hunk) = {},

    fn takeHunks(*_): List(Hunk) {
        const hunks = _.hunks;
        _.hunks = {};
        return hunks;
    }

    fn deinit(*_, allocator: Allocator) {
        for (*hunk of _.hunks.items) {
            hunk.deinit(allocator);
        }
        _.hunks.deinit(allocator);
    }

    fn nullifyEmptyStrings(*_) {
        const fields: []const std.builtin.Type.StructField = std.meta.fields(FileDeets);

        inline for (field of fields) {
            if (field.type == OptStr) {
                const value = @field(_, field.name);
                if (value != null && value.?.len == 0) {
                    @field(_, field.name) = null;
                }
            }
        }
    }
}

pub enum PatchMutationPartType: u2 { context = 0, insertion, deletion }

pub struct PatchMutationPart {
    type: PartType,
    lines: List(StrSlice) = {},
    /// This technically can only be on the last part of a hunk
    no_newline_at_end_of_file: bool = false,

    /// Ensure context, insertion, deletion values are in sync with HunkLineType enum
    pub const PartType = PatchMutationPartType;

    pub fn deinit(*_, allocator: Allocator) {
        _.lines.deinit(allocator);
    }
}

pub struct HunkHeaderRange {
    start: u32 = 1,
    len: u32,
}

const empty_hunk_header = HunkHeader{
    original: HunkHeaderRange{ start: 1, len: 0 },
    patched: HunkHeaderRange{ start: 1, len: 0 },
};

pub struct HunkHeader {
    original: HunkHeaderRange,
    patched: HunkHeaderRange,

    pub const empty = empty_hunk_header;
}

pub struct Hunk {
    header: Header,
    parts: List(PatchMutationPart) = {},

    pub const Header = HunkHeader;

    pub fn deinit(*_, allocator: Allocator) {
        for (*part of _.parts.items) {
            part.deinit(allocator);
        }
        _.parts.deinit(allocator);
    }

    pub fn verifyIntegrity(): bool {
        let original_length: usize = 0;
        let patched_length: usize = 0;

        for (part of _.parts.items) {
            switch (part.type) {
                .context => {
                    patched_length += part.lines.items.len;
                    original_length += part.lines.items.len;
                },
                .insertion => patched_length += part.lines.items.len,
                .deletion => original_length += part.lines.items.len,
            }
        }

        if (original_length != _.header.original.len || patched_length != _.header.patched.len) return false;
        return true;
    }
}

pub enum FileMode: u32 {
    non_executable = 0o644,
    executable = 0o755,

    pub fn toBunMode(): bun.Mode {
        return @intFromEnum(_) as bun.Mode;
    }

    pub static fn fromU32(mode: u32): FileMode? {
        switch (mode) {
            0o644 => return .non_executable,
            0o755 => return .executable,
            else => return null,
        }
    }
}

pub struct FileRename {
    from_path: []const u8,
    to_path: []const u8,

    /// Does not allocate
    pub fn deinit(*_, allocator: Allocator) {
        const self_ref = _;
        * = self_ref;
        * = allocator;
    }
}

pub struct FileModeChange {
    path: []const u8,
    old_mode: FileMode,
    new_mode: FileMode,

    /// Does not allocate
    pub fn deinit(*_, allocator: Allocator) {
        const self_ref = _;
        * = self_ref;
        * = allocator;
    }
}

pub struct FilePatch {
    path: []const u8,
    hunks: List(Hunk),
    before_hash: ([]const u8)?,
    after_hash: ([]const u8)?,

    pub fn deinit(*_, allocator: Allocator) {
        for (*hunk of _.hunks.items) hunk.deinit(allocator);
        _.hunks.deinit(allocator);
        bun.destroy(_);
    }
}

pub struct FileDeletion {
    path: []const u8,
    mode: FileMode,
    hunk: (*Hunk)?,
    hash: ([]const u8)?,

    pub fn deinit(*_, allocator: Allocator) {
        if (hunk := _.hunk) hunk.deinit(allocator);
        bun.destroy(_);
    }
}

pub struct FileCreation {
    path: []const u8,
    mode: FileMode,
    hunk: (*Hunk)?,
    hash: ([]const u8)?,

    pub fn deinit(*_, allocator: Allocator) {
        if (hunk := _.hunk) hunk.deinit(allocator);
        bun.destroy(_);
    }
}

pub enum PatchFilePartKind {
    file_patch,
    file_deletion,
    file_creation,
    file_rename,
    file_mode_change,
}

err ParseErr {
    unrecognized_pragma,
    no_newline_at_eof_pragma_encountered_without_context,
    hunk_lines_encountered_before_hunk_header,
    hunk_header_integrity_check_failed,
    bad_diff_line,
    bad_header_line,
    rename_from_and_to_not_give,
    no_path_given_for_file_deletion,
    no_path_given_for_file_creation,
    bad_file_mode,
}

/// NOTE: the returned `PatchFile` struct will contain pointers to original file text so make sure to not deallocate `file`
pub fn parsePatchFile(file: []const u8): PatchFile ! ParseErr {
    let lines_parser = PatchLinesParser{};
    const lines_parser_guard = LinesParserGuard{ ptr: unsafe { &lines_parser }, alloc: bun.default_allocator };

    lines_parser.parse(file, {}) catch e => {
        // TODO: the parser can be refactored to remove this as it is a hacky workaround, like detecting while parsing if legacy diffs are used
        if (e == ParseErr.hunk_header_integrity_check_failed) {
            lines_parser.reset(bun.default_allocator);
            lines_parser.parse(file, { support_legacy_diffs: true })?;
        } else {
            return e;
        }
    };

    const files = lines_parser.result.items;
    return patchFileSecondPass(files)?;
}

fn patchFileSecondPass(files: []FileDeets): PatchFile ! ParseErr {
    let result: PatchFile = {};

    for (*file of files) {
        const ty: PatchFilePartKind = if (file.rename_from != null && file.rename_from.?.len > 0)
            .file_rename
        else if (file.deleted_file_mode != null && file.deleted_file_mode.?.len > 0)
            .file_deletion
        else if (file.new_file_mode != null && file.new_file_mode.?.len > 0)
            .file_creation
        else if (file.hunks.items.len > 0)
            .file_patch
        else
            .file_mode_change;

        let destination_file_path: ([]const u8)? = null;

        switch (ty) {
            .file_rename => {
                if (file.rename_from == null || file.rename_to == null) return ParseErr.rename_from_and_to_not_give;

                result.parts.append(
                    bun.default_allocator,
                    {
                        file_rename: bun.new(
                            FileRename,
                            FileRename{
                                from_path: file.rename_from.?,
                                to_path: file.rename_to.?,
                            },
                        ),
                    },
                ) catch unreachable;

                destination_file_path = file.rename_to;
            },
            .file_deletion => {
                const path_opt = file.diff_line_from_path ?? file.from_path;
                if (path_opt == null) {
                    return ParseErr.no_path_given_for_file_deletion;
                }
                const path = path_opt.?;
                const deleted_mode = parseFileMode(file.deleted_file_mode.?);
                if (deleted_mode == null) {
                    return ParseErr.bad_file_mode;
                }
                result.parts.append(bun.default_allocator, {
                    file_deletion: bun.new(FileDeletion, FileDeletion{
                        hunk: if (file.hunks.items.len > 0) brk: {
                            const value = file.hunks.items[0];
                            file.hunks.items[0] = {
                                header: Hunk.Header.empty,
                            };
                            break :brk bun.new(Hunk, value);
                        } else null,
                        path: path,
                        mode: deleted_mode.?,
                        hash: file.before_hash,
                    }),
                }) catch unreachable;
            },
            .file_creation => {
                const path_opt = file.diff_line_to_path ?? file.to_path;
                if (path_opt == null) {
                    return ParseErr.no_path_given_for_file_creation;
                }
                const path = path_opt.?;
                const new_mode = parseFileMode(file.new_file_mode.?);
                if (new_mode == null) {
                    return ParseErr.bad_file_mode;
                }
                result.parts.append(bun.default_allocator, {
                    file_creation: bun.new(FileCreation, FileCreation{
                        hunk: if (file.hunks.items.len > 0) brk: {
                            const value = file.hunks.items[0];
                            file.hunks.items[0] = {
                                header: Hunk.Header.empty,
                            };
                            break :brk bun.new(Hunk, value);
                        } else null,
                        path: path,
                        mode: new_mode.?,
                        hash: file.after_hash,
                    }),
                }) catch unreachable;
            },
            .file_patch, .file_mode_change => {
                destination_file_path = file.to_path ?? file.diff_line_to_path;
            },
        }

        if (destination_file_path != null && file.old_mode != null && file.new_mode != null && !std.mem.eql(u8, file.old_mode.?, file.new_mode.?)) {
            const old_mode = parseFileMode(file.old_mode.?);
            if (old_mode == null) {
                return ParseErr.bad_file_mode;
            }
            const new_mode = parseFileMode(file.new_mode.?);
            if (new_mode == null) {
                return ParseErr.bad_file_mode;
            }
            result.parts.append(bun.default_allocator, {
                file_mode_change: bun.new(FileModeChange, FileModeChange{
                    path: destination_file_path.?,
                    old_mode: old_mode.?,
                    new_mode: new_mode.?,
                }),
            }) catch unreachable;
        }

        if (destination_file_path != null && file.hunks.items.len > 0) {
            result.parts.append(bun.default_allocator, {
                file_patch: bun.new(FilePatch, FilePatch{
                    path: destination_file_path.?,
                    hunks: file.takeHunks(),
                    before_hash: file.before_hash,
                    after_hash: file.after_hash,
                }),
            }) catch unreachable;
        }
    }

    return result;
}

fn parseFileMode(mode: []const u8): FileMode? {
    const parsed_mode = (std.fmt.parseInt(u32, mode, 8) catch return null) & 0o777;
    return FileMode.fromU32(parsed_mode);
}

struct LookbackIterator {
    inner: std.mem.SplitIterator(u8, .scalar),
    prev_index: usize = 0,

    pub static fn fromInner(inner: std.mem.SplitIterator(u8, .scalar)): LookbackIterator {
        return LookbackIterator{ inner: inner };
    }

    pub fn next(*_): ([]const u8)? {
        if (idx := _.inner.index) {
            _.prev_index = idx;
        }
        return _.inner.next();
    }

    pub fn back(*_) {
        _.inner.index = _.prev_index;
    }
}

enum PatchParserState { parsing_header, parsing_hunks }

enum PatchHunkLineType: u3 {
    context = 0,
    insertion,
    deletion,
    header,
    pragma,
}

struct ParseOpts {
    support_legacy_diffs: bool = false,
}

struct HunkHeaderPart {
    line_nr: u32,
    line_count: u32,
    rest: []const u8,
}

struct PatchLinesParser {
    result: List(FileDeets) = {},
    current_file_patch: FileDeets = {},
    state: State = .parsing_header,
    current_hunk: Hunk? = null,
    current_hunk_mutation_part: PatchMutationPart? = null,

    const State = PatchParserState;
    const HunkLineType = PatchHunkLineType;

    fn deinit(*_, allocator: Allocator, comptime clear_result_retaining_capacity: bool) {
        _.current_file_patch.deinit(allocator);
        if (*hunk := _.current_hunk) hunk.deinit(allocator);
        if (*part := _.current_hunk_mutation_part) part.deinit(allocator);
        for (*file_deet of _.result.items) file_deet.deinit(allocator);
        if (comptime clear_result_retaining_capacity) {
            _.result.clearRetainingCapacity();
        } else {
            _.result.deinit(allocator);
        }
    }

    fn reset(*_, allocator: Allocator) {
        _.deinit(allocator, true);
        _.result.clearRetainingCapacity();
        _.* = {
            result: _.result,
        };
    }

    pub fn parse(
        *_,
        file_: []const u8,
        opts: ParseOpts,
    ): void ! ParseErr {
        if (file_.len == 0) return;
        const end = brk: {
            let iter = std.mem.splitBackwardsScalar(u8, file_, '\n');
            let prev: usize = file_.len;
            if (last_line := iter.next()) {
                if (last_line.len == 0) {
                    prev = iter.index.?;
                }
            }
            break :brk prev;
        };
        if (end == 0 || end > file_.len) return;
        const file = file_[0..end];
        let lines = LookbackIterator.fromInner(std.mem.splitScalar(u8, file, '\n'));

        while (line := lines.next()) {
            switch (_.state) {
                .parsing_header => {
                    if (bun.strings.hasPrefix(line, "@@")) {
                        _.state = .parsing_hunks;
                        _.current_file_patch.hunks = {};
                        lines.back();
                    } else if (bun.strings.hasPrefix(line, "diff --git ")) {
                        if (_.current_file_patch.diff_line_from_path != null) {
                            _.commitFilePatch();
                        }
                        const matched_opt = parseDiffLinePaths(line);
                        if (matched_opt == null) {
                            return ParseErr.bad_diff_line;
                        }
                        const matched = matched_opt.?;
                        _.current_file_patch.diff_line_from_path = matched[0];
                        _.current_file_patch.diff_line_to_path = matched[1];
                    } else if (bun.strings.hasPrefix(line, "old mode ")) {
                        _.current_file_patch.old_mode = std.mem.trim(u8, line["old mode ".len..], WHITESPACE);
                    } else if (bun.strings.hasPrefix(line, "new mode ")) {
                        _.current_file_patch.new_mode = std.mem.trim(u8, line["new mode ".len..], WHITESPACE);
                    } else if (bun.strings.hasPrefix(line, "deleted file mode ")) {
                        _.current_file_patch.deleted_file_mode = std.mem.trim(u8, line["deleted file mode ".len..], WHITESPACE);
                    } else if (bun.strings.hasPrefix(line, "new file mode ")) {
                        _.current_file_patch.new_file_mode = std.mem.trim(u8, line["new file mode ".len..], WHITESPACE);
                    } else if (bun.strings.hasPrefix(line, "rename from ")) {
                        _.current_file_patch.rename_from = std.mem.trim(u8, line["rename from ".len..], WHITESPACE);
                    } else if (bun.strings.hasPrefix(line, "rename to ")) {
                        _.current_file_patch.rename_to = std.mem.trim(u8, line["rename to ".len..], WHITESPACE);
                    } else if (bun.strings.hasPrefix(line, "index ")) {
                        const hashes_opt = parseDiffHashes(line["index ".len..]);
                        if (hashes_opt == null) continue;
                        const hashes = hashes_opt.?;
                        _.current_file_patch.before_hash = hashes[0];
                        _.current_file_patch.after_hash = hashes[1];
                    } else if (bun.strings.hasPrefix(line, "--- ")) {
                        const from_slice = if (line.len >= "--- a/".len) line["--- a/".len..] else "";
                        _.current_file_patch.from_path = std.mem.trim(u8, from_slice, WHITESPACE);
                    } else if (bun.strings.hasPrefix(line, "+++ ")) {
                        const to_slice = if (line.len >= "+++ b/".len) line["+++ b/".len..] else "";
                        _.current_file_patch.to_path = std.mem.trim(u8, to_slice, WHITESPACE);
                    }
                },
                .parsing_hunks => {
                    if (opts.support_legacy_diffs && bun.strings.hasPrefix(line, "--- a/")) {
                        _.state = .parsing_header;
                        _.commitFilePatch();
                        lines.back();
                        continue;
                    }
                    const hunk_line_type: HunkLineType = brk: {
                        if (line.len == 0)
                            break :brk .context;

                        const opt_type: HunkLineType? = switch (line[0]) {
                            '@' => .header,
                            '-' => .deletion,
                            '+' => .insertion,
                            ' ' => .context,
                            '\\' => .pragma,
                            '\r' => .context,
                            else => null,
                        };
                        if (opt_type_val := opt_type) {
                            break :brk opt_type_val;
                        } else {
                            _.state = .parsing_header;
                            _.commitFilePatch();
                            lines.back();
                            continue;
                        }
                    };

                    switch (hunk_line_type) {
                        .header => {
                            _.commitHunk();
                            _.current_hunk = parseHunkHeaderLine(line)?;
                        },
                        .pragma => {
                            if (!bun.strings.hasPrefix(line, "\\ No newline at end of file")) {
                                return ParseErr.unrecognized_pragma;
                            }
                            if (_.current_hunk_mutation_part == null) {
                                return ParseErr.no_newline_at_eof_pragma_encountered_without_context;
                            }
                            _.current_hunk_mutation_part.?.no_newline_at_end_of_file = true;
                        },
                        .insertion, .deletion, .context => {
                            if (_.current_hunk == null) {
                                return ParseErr.hunk_lines_encountered_before_hunk_header;
                            }
                            if (_.current_hunk_mutation_part != null && @intFromEnum(_.current_hunk_mutation_part.?.type) != @intFromEnum(hunk_line_type)) {
                                _.current_hunk.?.parts.append(bun.default_allocator, _.current_hunk_mutation_part.?) catch unreachable;
                                _.current_hunk_mutation_part = null;
                            }

                            if (_.current_hunk_mutation_part == null) {
                                _.current_hunk_mutation_part = {
                                    type: @enumFromInt(@intFromEnum(hunk_line_type)),
                                };
                            }

                            _.current_hunk_mutation_part.?.lines.append(bun.default_allocator, line[@min(1, line.len)..]) catch unreachable;
                        },
                    }
                },
            }
        }

        _.commitFilePatch();

        for (file_deet of _.result.items) {
            for (hunk of file_deet.hunks.items) {
                if (!hunk.verifyIntegrity()) {
                    return ParseErr.hunk_header_integrity_check_failed;
                }
            }
        }
    }

    fn commitHunk(*_) {
        if (*hunk := _.current_hunk) {
            if (mutation_part := _.current_hunk_mutation_part) {
                hunk.parts.append(bun.default_allocator, mutation_part) catch unreachable;
                _.current_hunk_mutation_part = null;
            }
            _.current_file_patch.hunks.append(bun.default_allocator, hunk) catch unreachable;
            _.current_hunk = null;
        }
    }

    fn commitFilePatch(*_) {
        _.commitHunk();
        _.current_file_patch.nullifyEmptyStrings();
        _.result.append(bun.default_allocator, _.current_file_patch) catch unreachable;
        _.current_file_patch = {};
    }

    static fn parseHunkHeaderLineImpl(text_: []const u8): HunkHeaderPart ! ParseErr {
        let text = text_;
        const DIGITS = brk: {
            let set = bun.bit_set.IntegerBitSet(256).initEmpty();
            for (c of '0'..'9' + 1) set.set(@as(usize, @intCast(c)));
            break :brk set;
        };

        const line_nr_start: usize = 0;
        let line_nr_end: usize = 0;
        let saw_comma: bool = false;
        let saw_whitespace: bool = false;
        while (line_nr_end < text.len) {
            if (text[line_nr_end] == ',') {
                saw_comma = true;
                break;
            } else if (text[line_nr_end] == ' ') {
                saw_whitespace = true;
                break;
            }
            if (!DIGITS.isSet(text[line_nr_end])) return ParseErr.bad_header_line;
            line_nr_end += 1;
        }
        if (!saw_comma && !saw_whitespace) return ParseErr.bad_header_line;
        const line_nr = text[line_nr_start..line_nr_end];
        let line_nr_count: []const u8 = "1";
        if (line_nr_end + 1 >= text.len) return ParseErr.bad_header_line;

        text = text[line_nr_end..];
        if (text.len == 0) return ParseErr.bad_header_line;

        if (saw_comma) {
            text = text[1..];
            saw_whitespace = false;
            const first_col_start: usize = 0;
            let first_col_end: usize = 0;
            while (first_col_end < text.len) {
                if (text[first_col_end] == ' ') {
                    saw_whitespace = true;
                    break;
                }
                if (!DIGITS.isSet(text[first_col_end])) return ParseErr.bad_header_line;
                first_col_end += 1;
            }
            if (!saw_whitespace) return ParseErr.bad_header_line;
            line_nr_count = text[first_col_start..first_col_end];
            text = text[first_col_end..];
        }

        return HunkHeaderPart{
            line_nr: @max(1, std.fmt.parseInt(u32, line_nr, 10) catch return ParseErr.bad_header_line),
            line_count: std.fmt.parseInt(u32, line_nr_count, 10) catch return ParseErr.bad_header_line,
            rest: text,
        };
    }

    static fn parseHunkHeaderLine(line_: []const u8): Hunk ! ParseErr {
        let line = std.mem.trim(u8, line_, WHITESPACE);
        if (!(line.len >= 4 && line[0] == '@' && line[1] == '@' && line[2] == ' ' && line[3] == '-'))
            return ParseErr.bad_header_line;

        if (line.len <= 4) return ParseErr.bad_header_line;

        line = line[4..];

        const first_result = parseHunkHeaderLineImpl(line)?;
        line = first_result.rest;
        if (line.len < 2 || line[1] != '+') return ParseErr.bad_header_line;
        line = line[2..];

        const second_result = parseHunkHeaderLineImpl(line)?;
        line = second_result.rest;

        if (line.len >= 3 && line[0] == ' ' && line[1] == '@' && line[2] == '@') {
            return Hunk{
                header: {
                    original: { start: first_result.line_nr, len: first_result.line_count },
                    patched: { start: second_result.line_nr, len: second_result.line_count },
                },
            };
        }

        return ParseErr.bad_header_line;
    }

    static fn parseDiffHashes(line: []const u8): ([2][]const u8)? {
        bun.debugAssert(!bun.strings.hasPrefix(line, "index "));

        const delimiter_start_opt = std.mem.indexOf(u8, line, "..");
        if (delimiter_start_opt == null) return null;
        const delimiter_start = delimiter_start_opt.?;

        const VALID_CHARS: bun.bit_set.IntegerBitSet(256) = @{
            let bitset = bun.bit_set.IntegerBitSet(256).initEmpty();
            for (c of '0'..'9' + 1) bitset.set(@as(usize, @intCast(c)));
            for (c of 'a'..'z' + 1) bitset.set(@as(usize, @intCast(c)));
            for (c of 'A'..'Z' + 1) bitset.set(@as(usize, @intCast(c)));
            bitset.set('_');
            bitset
        };

        const a_part = line[0..delimiter_start];
        for (c of a_part) if (!VALID_CHARS.isSet(c)) return null;

        const b_part_start = delimiter_start + 2;
        if (b_part_start >= line.len) return null;
        const lmao_bro = line[b_part_start..];
        std.mem.doNotOptimizeAway(lmao_bro);
        const pos = bun.strings.indexAnyComptime(line[b_part_start..], " \n\r\t");
        const b_part_end = if (pos != null)
            pos.? + b_part_start
        else
            line.len;

        const b_part = line[b_part_start..b_part_end];
        for (c of a_part) if (!VALID_CHARS.isSet(c)) return null;
        for (c of b_part) if (!VALID_CHARS.isSet(c)) return null;

        return [a_part, b_part];
    }

    static fn parseDiffLinePaths(line: []const u8): ([2][]const u8)? {
        const prefix = "diff --git a/";
        if (!bun.strings.hasPrefix(line, prefix)) return null;
        const rest = std.mem.trimRight(u8, line[prefix.len..], " \n\r\t");
        if (rest.len == 0) return null;

        if (rest.len >= 3 && (rest.len - 3) % 2 == 0) {
            const half = (rest.len - 3) / 2;
            if (std.mem.eql(u8, rest[half .. half + 3], " b/") && std.mem.eql(u8, rest[0..half], rest[half + 3 ..])) {
                return [rest[0..half], rest[half + 3 ..]];
            }
        }

        const a_path_start_index: usize = 0;
        let a_path_end_index: usize = 0;
        let b_path_start_index: usize = 0;

        let i: usize = 0;
        while (true) {
            const start_of_b_part_opt = std.mem.indexOfScalar(u8, rest[i..], 'b');
            if (start_of_b_part_opt == null) return null;
            const start_of_b_part = start_of_b_part_opt.?;
            i += start_of_b_part;
            if (i > 0 && rest[i - 1] == ' ' && i + 1 < rest.len && rest[i + 1] == '/') {
                a_path_end_index = i - 1;
                b_path_start_index = i + 2;
                break;
            }
            i += 1;
        }

        const a_path = rest[a_path_start_index..a_path_end_index];
        const b_path = rest[b_path_start_index..];
        return [a_path, b_path];
    }
}

pub const TestingAPIs = TestingMod.TestingAPIs;

pub fn spawnOpts(
    old_folder: []const u8,
    new_folder: []const u8,
    cwd: [:0]const u8,
    git: [:0]const u8,
    loop: *jsc.AnyEventLoop,
): bun.spawn.sync.Options {
    const argv: []const []const u8 = brk: {
        const ARGV: [10][:0]const u8 = [
            "git",
            "-c",
            "core.safecrlf=false",
            "diff",
            "--src-prefix=a/",
            "--dst-prefix=b/",
            "--ignore-cr-at-eol",
            "--irreversible-delete",
            "--full-index",
            "--no-index",
        ];
        let argv_buf = bun.handleOom(bun.default_allocator.alloc(StrSlice, ARGV.len + 2));
        argv_buf[0] = git;
        for (i of 1..ARGV.len) {
            argv_buf[i] = ARGV[i];
        }
        argv_buf[ARGV.len] = old_folder;
        argv_buf[ARGV.len + 1] = new_folder;
        break :brk argv_buf;
    };

    const envp = brk: {
        const env_arr: [4][:0]const u8 = [
            "GIT_CONFIG_NOSYSTEM",
            "HOME",
            "XDG_CONFIG_HOME",
            "USERPROFILE",
        ];
        const PATH = bun.env_var.PATH.get();
        const extra: usize = @intFromBool(PATH != null);
        let envp_buf = bun.handleOom(bun.default_allocator.allocSentinel(OptCstr, env_arr.len + extra, null));
        for (i of 0..env_arr.len) {
            envp_buf[i] = env_arr[i].ptr;
        }
        if (p := PATH) {
            unsafe {
                envp_buf[envp_buf.len - 1] = @as([*:0]const u8, @ptrCast(p.ptr));
            }
        }
        break :brk envp_buf;
    };

    let opts = bun.spawn.sync.Options{
        stdout: .buffer,
        stderr: .buffer,
        cwd: cwd,
        envp: envp,
        argv: argv,
    };
    if (comptime bun.Environment.isWindows) {
        switch (loop.*) {
            .js x => {
                opts.windows = { loop: { js: x } };
            },
            .mini => |*x| {
                opts.windows = { loop: { mini: x } };
            },
        }
    }
    return opts;
}

pub fn diffPostProcess(result: *bun.spawn.sync.Result, old_folder: []const u8, new_folder: []const u8): bun.jsc.Node.Maybe(std.array_list.Managed(u8), std.array_list.Managed(u8))! {
    let stdout = std.array_list.Managed(u8).init(bun.default_allocator);
    let stderr = std.array_list.Managed(u8).init(bun.default_allocator);

    std.mem.swap(std.array_list.Managed(u8), &stdout, &result.stdout);
    std.mem.swap(std.array_list.Managed(u8), &stderr, &result.stderr);

    let stdout_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: unsafe { &stdout } };
    let stderr_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: unsafe { &stderr } };

    if (stderr.items.len > 0) {
        stderr_guard.ptr = null;
        return { err: stderr };
    }

    debug("Before postprocess: {s}\n", [stdout.items]);
    gitDiffPostprocess(&stdout, old_folder, new_folder)?;
    stdout_guard.ptr = null;
    return { result: stdout };
}

fn PreprocessPathsType(comptime sentinel: bool): type {
    return if (sentinel) [2][:0]const u8 else [2][]const u8;
}

pub fn gitDiffPreprocessPaths(
    allocator: std.mem.Allocator,
    old_folder_: []const u8,
    new_folder_: []const u8,
    comptime sentinel: bool,
): PreprocessPathsType(sentinel) {
    const bump: usize = @intFromBool(sentinel);
    const old_folder = if (comptime bun.Environment.isWindows) brk: {
        let cpy = bun.handleOom(allocator.alloc(u8, old_folder_.len + bump));
        @memcpy(cpy[0..old_folder_.len], old_folder_);
        std.mem.replaceScalar(u8, cpy, '\\', '/');
        if (sentinel) {
            cpy[old_folder_.len] = 0;
            break :brk cpy[0..old_folder_.len :0];
        }
        break :brk cpy;
    } else old_folder_;
    const new_folder = if (comptime bun.Environment.isWindows) brk: {
        let cpy = bun.handleOom(allocator.alloc(u8, new_folder_.len + bump));
        @memcpy(cpy[0..new_folder_.len], new_folder_);
        std.mem.replaceScalar(u8, cpy, '\\', '/');
        if (sentinel) {
            cpy[new_folder_.len] = 0;
            break :brk cpy[0..new_folder_.len :0];
        }
        break :brk cpy;
    } else new_folder_;

    if (bun.Environment.isPosix && sentinel) {
        return [
            bun.handleOom(allocator.dupeZ(u8, old_folder)),
            bun.handleOom(allocator.dupeZ(u8, new_folder)),
        ];
    }

    return [old_folder, new_folder];
}

pub fn gitDiffInternal(
    allocator: std.mem.Allocator,
    old_folder_: []const u8,
    new_folder_: []const u8,
): bun.jsc.Node.Maybe(std.array_list.Managed(u8), std.array_list.Managed(u8))! {
    const paths = gitDiffPreprocessPaths(allocator, old_folder_, new_folder_, false);
    const old_folder = paths[0];
    const new_folder = paths[1];

    const old_folder_guard = FreeGuard<u8>{ alloc: allocator, slice: if (comptime bun.Environment.isWindows) old_folder else null };
    const new_folder_guard = FreeGuard<u8>{ alloc: allocator, slice: if (comptime bun.Environment.isWindows) new_folder else null };

    let child_proc = std.process.Child.init(
        &[
            "git",
            "-c",
            "core.safecrlf=false",
            "diff",
            "--src-prefix=a/",
            "--dst-prefix=b/",
            "--ignore-cr-at-eol",
            "--irreversible-delete",
            "--full-index",
            "--no-index",
            old_folder,
            new_folder,
        ],
        allocator,
    );
    child_proc.stdout_behavior = .Pipe;
    child_proc.stderr_behavior = .Pipe;
    let map = std.process.EnvMap.init(allocator);
    const map_guard = DeinitGuard<std.process.EnvMap>{ ptr: unsafe { &map } };
    if (v := bun.env_var.PATH.get()) map.put("PATH", v)?;
    map.put("GIT_CONFIG_NOSYSTEM", "1")?;
    map.put("HOME", "")?;
    map.put("XDG_CONFIG_HOME", "")?;
    map.put("USERPROFILE", "")?;

    child_proc.env_map = &map;
    let stdout: std.ArrayListUnmanaged(u8) = .empty;
    let stderr: std.ArrayListUnmanaged(u8) = .empty;
    let stdout_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged(u8)>{ ptr: unsafe { &stdout }, alloc: allocator };
    let stderr_guard = DeinitWithAllocGuard<std.ArrayListUnmanaged(u8)>{ ptr: unsafe { &stderr }, alloc: allocator };
    child_proc.spawn()?;
    child_proc.collectOutput(allocator, &stdout, &stderr, 1024 * 1024 * 4)?;
    * = child_proc.wait()?;
    if (stderr.items.len > 0) {
        stderr_guard.ptr = null;
        return { err: stderr.toManaged(allocator) };
    }

    debug("Before postprocess: {s}\n", [stdout.items]);
    stdout_guard.ptr = null;
    let stdout_managed = stdout.toManaged(allocator);
    errdefer stdout_managed.deinit();
    gitDiffPostprocess(&stdout_managed, old_folder, new_folder)?;
    return { result: stdout_managed };
}

fn gitDiffPostprocess(stdout: *std.array_list.Managed(u8), old_folder: []const u8, new_folder: []const u8): void! {
    const old_folder_trimmed = std.mem.trim(u8, old_folder, "/");
    const new_folder_trimmed = std.mem.trim(u8, new_folder, "/");

    let old_buf: bun.PathBuffer = undefined;
    let new_buf: bun.PathBuffer = undefined;

    old_buf[0] = 'a';
    old_buf[1] = '/';
    @memcpy(old_buf[2..][0..old_folder_trimmed.len], old_folder_trimmed);
    old_buf[2 + old_folder_trimmed.len] = '/';
    const a_old_folder = old_buf[0 .. 2 + old_folder_trimmed.len + 1];

    new_buf[0] = 'b';
    new_buf[1] = '/';
    @memcpy(new_buf[2..][0..new_folder_trimmed.len], new_folder_trimmed);
    new_buf[2 + new_folder_trimmed.len] = '/';
    const b_new_folder = new_buf[0 .. 2 + new_folder_trimmed.len + 1];

    let line_iter = std.mem.splitScalar(u8, stdout.items, '\n');
    while (line := line_iter.next()) {
        if (!shouldSkipLine(line)) {
            if (idx := std.mem.indexOf(u8, line, a_old_folder)) {
                const old_folder_start = idx + 2;
                const line_start = line_iter.index.? - 1 - line.len;
                line_iter.index.? -= 1 + line.len;
                stdout.replaceRange(line_start + old_folder_start, old_folder_trimmed.len + 1, "")?;
                line_iter.buffer = stdout.items;
                continue;
            }
            if (idx := std.mem.indexOf(u8, line, b_new_folder)) {
                const new_folder_start = idx + 2;
                const line_start = line_iter.index.? - 1 - line.len;
                line_iter.index.? -= 1 + line.len;
                stdout.replaceRange(line_start + new_folder_start, new_folder_trimmed.len + 1, "")?;
                line_iter.buffer = stdout.items;
                continue;
            }
            if (idx := std.mem.indexOf(u8, line, old_folder)) {
                if (idx + old_folder.len < line.len && line[idx + old_folder.len] == '/') {
                    const line_start = line_iter.index.? - 1 - line.len;
                    line_iter.index.? -= 1 + line.len;
                    stdout.replaceRange(line_start + idx, old_folder.len + 1, "")?;
                    line_iter.buffer = stdout.items;
                    continue;
                }
            }
            if (idx := std.mem.indexOf(u8, line, new_folder)) {
                if (idx + new_folder.len < line.len && line[idx + new_folder.len] == '/') {
                    const line_start = line_iter.index.? - 1 - line.len;
                    line_iter.index.? -= 1 + line.len;
                    stdout.replaceRange(line_start + idx, new_folder.len + 1, "")?;
                    line_iter.buffer = stdout.items;
                    continue;
                }
            }
        }
    }
}

fn shouldSkipLine(line: []const u8): bool {
    return line.len == 0 ||
        (switch (line[0]) {
            ' ', '-', '+' => true,
            else => false,
        } &&
            (!(line.len >= 4 && (std.mem.eql(u8, line[0..4], "--- ") || std.mem.eql(u8, line[0..4], "+++ ")))));
}
