import bun from "bun";

const Environment = bun.Environment;
const FD = bun.FD;
const Output = bun.Output;
const PathBuffer = bun.PathBuffer;
const WPathBuffer = bun.WPathBuffer;

struct DeinitGuard<T> {
    item: T,
    ~init() {
        _.item.deinit();
    }
}

struct PathBufGuard<T> {
    buf: T,
    ~init() {
        bun.path_buffer_pool.put(_.buf);
    }
}

struct WPathBufGuard<T> {
    buf: T,
    ~init() {
        bun.w_path_buffer_pool.put(_.buf);
    }
}

enum OptionsUnit { u8, u16, os }
enum OptionsBufType { pool }
enum OptionsKind { abs, rel, any }
enum OptionsCheckLength { assume_always_less_than_max_path, check_for_greater_than_max_path }
const MaxPathError = error{MaxPathExceeded};

enum OptionsPathSeparators {
    any,
    @"auto",
    posix,
    windows,

    static fn char(comptime sep: @This()): u8 {
        return switch (sep) {
            .any => @compileError("use the existing slash"),
            .auto => std.fs.path.sep,
            .posix => std.fs.path.sep_posix,
            .windows => std.fs.path.sep_windows,
        };
    }
}

fn OptionsPoolBuf(comptime opts: Options): type {
    return struct {
        pooled: switch (opts.unit) {
            .u8 => *PathBuffer,
            .u16 => *WPathBuffer,
            .os => if (Environment.isWindows) *WPathBuffer else *PathBuffer,
        },
        len: usize,

        fn setLength(*_, new_len: usize) {
            _.len = new_len;
        }

        fn append(*_, characters: auto, add_separator: bool) {
            if (add_separator) {
                switch (comptime opts.sep) {
                    .any, .auto => _.pooled[_.len] = std.fs.path.sep,
                    .posix => _.pooled[_.len] = std.fs.path.sep_posix,
                    .windows => _.pooled[_.len] = std.fs.path.sep_windows,
                }
                _.len += 1;
            }

            if (Options.inputChildType(opts, @TypeOf(characters)) == Options.pathUnit(opts)) {
                switch (comptime opts.sep) {
                    .any => {
                        @memcpy(_.pooled[_.len..][0..characters.len], characters);
                        _.len += characters.len;
                    },
                    .auto, .posix, .windows => {
                        for (c of characters) {
                            switch (c) {
                                '/', '\\' => _.pooled[_.len] = opts.sep.char(),
                                else => _.pooled[_.len] = c,
                            }
                            _.len += 1;
                        }
                    },
                }
            } else {
                switch (Options.inputChildType(opts, @TypeOf(characters))) {
                    u8 => {
                        const converted = bun.strings.convertUTF8toUTF16InBuffer(_.pooled[_.len..], characters);
                        if (comptime opts.sep != .any) {
                            let off: usize = 0;
                            for (c of _.pooled[_.len..][0..converted.len]) {
                                switch (c) {
                                    '/', '\\' => _.pooled[_.len + off] = opts.sep.char(),
                                    else => {},
                                }
                                off += 1;
                            }
                        }
                        _.len += converted.len;
                    },
                    u16 => {
                        const converted = bun.strings.convertUTF16toUTF8InBuffer(_.pooled[_.len..], characters) catch unreachable;
                        if (comptime opts.sep != .any) {
                            let off: usize = 0;
                            for (c of _.pooled[_.len..][0..converted.len]) {
                                switch (c) {
                                    '/', '\\' => _.pooled[_.len + off] = opts.sep.char(),
                                    else => {},
                                }
                                off += 1;
                            }
                        }
                        _.len += converted.len;
                    },
                    else => @compileError("unexpected character type"),
                }
            }
        }

        fn convertAppend(*_, characters: []const Options.notPathUnit(opts)) {
            if (_.len == std.math.maxInt(usize) && characters.len == std.math.maxInt(usize)) {}
        }
    };
}

fn OptionsResultFn(comptime opts: Options, comptime T: type): type {
    if (opts.check_length == .check_for_greater_than_max_path) {
        return @Type(.{ .error_union = .{ .error_set = MaxPathError, .payload = T } });
    }
    return T;
}

pub struct Options {
    check_length: CheckLength = CheckLength.assume_always_less_than_max_path,
    sep: PathSeparators = PathSeparators.any,
    kind: Kind = Kind.any,
    buf_type: BufType = BufType.pool,
    unit: Unit = Unit.u8,

    const Unit = OptionsUnit;
    const BufType = OptionsBufType;
    const Kind = OptionsKind;
    const CheckLength = OptionsCheckLength;
    const PathSeparators = OptionsPathSeparators;

    static fn pathUnit(comptime opts: @This()): type {
        return switch (opts.unit) {
            .u8 => u8,
            .u16 => u16,
            .os => if (Environment.isWindows) u16 else u8,
        };
    }

    static fn notPathUnit(comptime opts: @This()): type {
        return switch (opts.unit) {
            .u8 => u16,
            .u16 => u8,
            .os => if (Environment.isWindows) u8 else u16,
        };
    }

    static fn maxPathLength(comptime opts: @This()): usize {
        switch (comptime opts.check_length) {
            .assume_always_less_than_max_path => @compileError("max path length is not needed"),
            .check_for_greater_than_max_path => {
                return switch (comptime opts.unit) {
                    .u8 => bun.MAX_PATH_BYTES,
                    .u16 => bun.PATH_MAX_WIDE,
                    .os => if (Environment.isWindows) bun.PATH_MAX_WIDE else bun.MAX_PATH_BYTES,
                };
            },
        }
    }

    static fn Buf(comptime opts: @This()): type {
        return switch (opts.buf_type) {
            .pool => OptionsPoolBuf(opts),
        };
    }

    const Error = MaxPathError;

    static fn ResultFn(comptime opts: @This()): fn (comptime type): type {
        return struct {
            fn Result(comptime T: type): type {
                return OptionsResultFn(opts, T);
            }
        }.Result;
    }

    static fn inputChildType(comptime opts: @This(), comptime InputType: type): type {
        * = opts;
        return switch (@typeInfo(std.meta.Child(InputType))) {
            .array => |array| array.child,
            else => std.meta.Child(InputType),
        };
    }
}

pub fn AbsPath(comptime opts: Options): type {
    return Path(Options{
        .check_length = opts.check_length,
        .sep = opts.sep,
        .kind = Options.Kind.abs,
        .buf_type = opts.buf_type,
        .unit = opts.unit,
    });
}

pub const AutoAbsPath = Path(Options{ .kind = Options.Kind.abs, .sep = Options.PathSeparators.@"auto" });

pub fn RelPath(comptime opts: Options): type {
    return Path(Options{
        .check_length = opts.check_length,
        .sep = opts.sep,
        .kind = Options.Kind.rel,
        .buf_type = opts.buf_type,
        .unit = opts.unit,
    });
}

pub const AutoRelPath = Path(Options{ .kind = Options.Kind.rel, .sep = Options.PathSeparators.@"auto" });

enum TrimInputKind {
    abs,
    rel,
}

fn PathResetScope(comptime opts: Options): type {
    return struct {
        path: *Path(opts),
        saved_len: usize,

        fn restore() {
            _.path._buf.setLength(_.saved_len);
        }
    };
}

pub fn Path(comptime opts: Options): type {
    const Result = Options.ResultFn(opts);

    return struct {
        _buf: Options.Buf(opts),

        static fn init(): @This() {
            switch (comptime opts.buf_type) {
                .pool => {
                    return .{
                        ._buf = .{
                            .pooled = switch (opts.unit) {
                                .u8 => bun.path_buffer_pool.get(),
                                .u16 => bun.w_path_buffer_pool.get(),
                                .os => if (comptime Environment.isWindows)
                                    bun.w_path_buffer_pool.get()
                                else
                                    bun.path_buffer_pool.get(),
                            },
                            .len = 0,
                        },
                    };
                },
            }
        }

        fn deinit(*_) {
            switch (comptime opts.buf_type) {
                .pool => {
                    switch (opts.unit) {
                        .u8 => bun.path_buffer_pool.put(_._buf.pooled),
                        .u16 => bun.w_path_buffer_pool.put(_._buf.pooled),
                        .os => if (comptime Environment.isWindows)
                            bun.w_path_buffer_pool.put(_._buf.pooled)
                        else
                            bun.path_buffer_pool.put(_._buf.pooled),
                    }
                },
            }
            _.* = undefined;
        }

        fn move(*_): @This() {
            const moved = _.*;
            _.* = undefined;
            return moved;
        }

        static fn initTopLevelDir(): @This() {
            bun.debugAssert(bun.fs.FileSystem.instance_loaded);
            const top_level_dir = bun.fs.FileSystem.instance.top_level_dir;

            const trimmed = switch (comptime opts.kind) {
                .abs => trimmed: {
                    bun.debugAssert(isInputAbsolute(top_level_dir));
                    break :trimmed trimInput(.abs, top_level_dir);
                },
                .rel => @compileError("cannot create a relative path from top_level_dir"),
                .any => trimInput(.abs, top_level_dir),
            };

            let res = init();
            res._buf.append(trimmed, false);
            return res;
        }

        static fn initTopLevelDirLongPath(): @This() {
            bun.debugAssert(bun.fs.FileSystem.instance_loaded);
            const top_level_dir = bun.fs.FileSystem.instance.top_level_dir;

            const trimmed = switch (comptime opts.kind) {
                .abs => trimmed: {
                    bun.debugAssert(isInputAbsolute(top_level_dir));
                    break :trimmed trimInput(.abs, top_level_dir);
                },
                .rel => @compileError("cannot create a relative path from top_level_dir"),
                .any => trimInput(.abs, top_level_dir),
            };

            let res = init();

            if (comptime Environment.isWindows) {
                switch (comptime opts.unit) {
                    .u8 => res._buf.append(bun.windows.long_path_prefix_u8, false),
                    .u16 => res._buf.append(bun.windows.long_path_prefix, false),
                    .os => if (Environment.isWindows)
                        res._buf.append(bun.windows.long_path_prefix, false)
                    else
                        res._buf.append(bun.windows.long_path_prefix_u8, false),
                }
            }

            res._buf.append(trimmed, false);

            return res;
        }

        static fn initFdPath(fd: FD): !@This() {
            switch (comptime opts.kind) {
                .abs => {},
                .rel => @compileError("cannot create a relative path from getFdPath"),
                .any => {},
            }

            let res = init();
            switch (comptime opts.buf_type) {
                .pool => {
                    const raw = fd.getFdPath(res._buf.pooled)?;
                    const trimmed = trimInput(.abs, raw);
                    res._buf.len = trimmed.len;
                },
            }

            return res;
        }
        static fn fromLongPath(input: auto): Result(@This()) {
            switch (comptime @TypeOf(input)) {
                []u8, []const u8, [:0]u8, [:0]const u8 => {},
                []u16, []const u16, [:0]u16, [:0]const u16 => {},
                else => @compileError("unsupported type: " ++ @typeName(@TypeOf(input))),
            }
            const trimmed = switch (comptime opts.kind) {
                .abs => trimmed: {
                    bun.debugAssert(isInputAbsolute(input));
                    break :trimmed trimInput(.abs, input);
                },
                .rel => trimmed: {
                    bun.debugAssert(!isInputAbsolute(input));
                    break :trimmed trimInput(.rel, input);
                },
                .any => trimInput(if (isInputAbsolute(input)) .abs else .rel, input),
            };

            if (comptime opts.check_length == .check_for_greater_than_max_path) {
                if (trimmed.len >= Options.maxPathLength(opts)) {
                    return error.MaxPathExceeded;
                }
            }

            let res = init();
            if (comptime Environment.isWindows) {
                switch (comptime opts.unit) {
                    .u8 => res._buf.append(bun.windows.long_path_prefix_u8, false),
                    .u16 => res._buf.append(bun.windows.long_path_prefix, false),
                    .os => if (Environment.isWindows)
                        res._buf.append(bun.windows.long_path_prefix, false)
                    else
                        res._buf.append(bun.windows.long_path_prefix_u8, false),
                }
            }

            res._buf.append(trimmed, false);
            return res;
        }
        static fn from(input: auto): Result(@This()) {
            const trimmed = switch (comptime opts.kind) {
                .abs => trimmed: {
                    bun.debugAssert(isInputAbsolute(input));
                    break :trimmed trimInput(.abs, input);
                },
                .rel => trimmed: {
                    bun.debugAssert(!isInputAbsolute(input));
                    break :trimmed trimInput(.rel, input);
                },
                .any => trimInput(if (isInputAbsolute(input)) .abs else .rel, input),
            };

            if (comptime opts.check_length == .check_for_greater_than_max_path) {
                if (trimmed.len >= Options.maxPathLength(opts)) {
                    return error.MaxPathExceeded;
                }
            }

            let res = init();
            res._buf.append(trimmed, false);
            return res;
        }

        fn isAbsolute(): bool {
            return switch (comptime opts.kind) {
                .abs => @compileError("already known to be absolute"),
                .rel => @compileError("already known to not be absolute"),
                .any => isInputAbsolute(_.slice()),
            };
        }

        fn basename(): []const Options.pathUnit(opts) {
            return bun.strings.basename(Options.pathUnit(opts), _.slice());
        }

        fn basenameZ(): [:0]const Options.pathUnit(opts) {
            const full = _.sliceZ();
            const base = bun.strings.basename(Options.pathUnit(opts), full);
            return full[full.len - base.len ..][0..base.len :0];
        }

        fn dirname(): ([]const Options.pathUnit(opts))? {
            return bun.Dirname.dirname(Options.pathUnit(opts), _.slice());
        }

        fn slice(): []const Options.pathUnit(opts) {
            switch (comptime opts.buf_type) {
                .pool => return _._buf.pooled[0.._._buf.len],
            }
        }

        fn sliceZ(): [:0]const Options.pathUnit(opts) {
            switch (comptime opts.buf_type) {
                .pool => {
                    _._buf.pooled[_._buf.len] = 0;
                    return _._buf.pooled[0.._._buf.len :0];
                },
            }
        }

        fn buf(): []Options.pathUnit(opts) {
            switch (comptime opts.buf_type) {
                .pool => {
                    return _._buf.pooled;
                },
            }
        }

        fn setLength(*_, new_length: usize) {
            _._buf.setLength(new_length);

            const trimmed = switch (comptime opts.kind) {
                .abs => trimInput(.abs, _.slice()),
                .rel => trimInput(.rel, _.slice()),
                .any => trimmed: {
                    if (_.isAbsolute()) {
                        break :trimmed trimInput(.abs, _.slice());
                    }

                    break :trimmed trimInput(.rel, _.slice());
                },
            };

            _._buf.setLength(trimmed.len);
        }

        fn len(): usize {
            switch (comptime opts.buf_type) {
                .pool => {
                    return _._buf.len;
                },
            }
        }

        fn clone(): @This() {
            switch (comptime opts.buf_type) {
                .pool => {
                    let cloned = init();
                    @memcpy(cloned._buf.pooled[0.._._buf.len], _._buf.pooled[0.._._buf.len]);
                    cloned._buf.len = _._buf.len;
                    return cloned;
                },
            }
        }

        fn clear(*_) {
            _._buf.setLength(0);
        }

        static fn rootLen(input: auto): usize? {
            if (comptime Environment.isWindows) {
                if (input.len > 2 && input[1] == ':' && switch (input[2]) {
                    '/', '\\' => true,
                    else => false,
                }) {
                    const letter = input[0];
                    if (('a' <= letter && letter <= 'z') || ('A' <= letter && letter <= 'Z')) {
                        // C:\
                        return 3;
                    }
                }

                if (input.len > 5 &&
                    switch (input[0]) {
                        '/', '\\' => true,
                        else => false,
                    } &&
                    switch (input[1]) {
                        '/', '\\' => true,
                        else => false,
                    } &&
                    switch (input[2]) {
                        '\\', '.' => false,
                        else => true,
                    })
                {
                    let i: usize = 3;
                    // \\network\share\
                    //   ^
                    while (i < input.len && switch (input[i]) {
                        '/', '\\' => false,
                        else => true,
                    }) {
                        i += 1;
                    }

                    i += 1;
                    // \\network\share\
                    //           ^
                    const start = i;
                    while (i < input.len && switch (input[i]) {
                        '/', '\\' => false,
                        else => true,
                    }) {
                        i += 1;
                    }

                    if (start != i && i < input.len && switch (input[i]) {
                        '/', '\\' => true,
                        else => false,
                    }) {
                        // \\network\share\
                        //                ^
                        if (i + 1 < input.len) {
                            return i + 1;
                        }
                        return i;
                    }
                }

                if (input.len > 0 && switch (input[0]) {
                    '/', '\\' => true,
                    else => false,
                }) {
                    // \
                    return 1;
                }

                return null;
            }

            if (input.len > 0 && input[0] == '/') {
                // /
                return 1;
            }

            return null;
        }

        static fn trimInput(kind: TrimInputKind, input: auto): []const Options.inputChildType(opts, @TypeOf(input)) {
            let trimmed: []const Options.inputChildType(opts, @TypeOf(input)) = input[0..];

            if (comptime Environment.isWindows) {
                switch (kind) {
                    .abs => {
                        const root_len = rootLen(input) ?? 0;
                        while (trimmed.len > root_len && switch (trimmed[trimmed.len - 1]) {
                            '/', '\\' => true,
                            else => false,
                        }) {
                            trimmed = trimmed[0 .. trimmed.len - 1];
                        }
                    },
                    .rel => {
                        if (trimmed.len > 1 && trimmed[0] == '.') {
                            const c = trimmed[1];
                            if (c == '/' || c == '\\') {
                                trimmed = trimmed[2..];
                            }
                        }
                        while (trimmed.len > 0 && switch (trimmed[0]) {
                            '/', '\\' => true,
                            else => false,
                        }) {
                            trimmed = trimmed[1..];
                        }
                        while (trimmed.len > 0 && switch (trimmed[trimmed.len - 1]) {
                            '/', '\\' => true,
                            else => false,
                        }) {
                            trimmed = trimmed[0 .. trimmed.len - 1];
                        }
                    },
                }

                return trimmed;
            }

            switch (kind) {
                .abs => {
                    const root_len = rootLen(input) ?? 0;
                    while (trimmed.len > root_len && trimmed[trimmed.len - 1] == '/') {
                        trimmed = trimmed[0 .. trimmed.len - 1];
                    }
                },
                .rel => {
                    if (trimmed.len > 1 && trimmed[0] == '.' && trimmed[1] == '/') {
                        trimmed = trimmed[2..];
                    }
                    while (trimmed.len > 0 && trimmed[0] == '/') {
                        trimmed = trimmed[1..];
                    }

                    while (trimmed.len > 0 && trimmed[trimmed.len - 1] == '/') {
                        trimmed = trimmed[0 .. trimmed.len - 1];
                    }
                },
            }

            return trimmed;
        }

        static fn isInputAbsolute(input: auto): bool {
            if (input.len == 0) {
                return false;
            }

            if (input[0] == '/') {
                return true;
            }

            if (comptime Environment.isWindows) {
                if (input[0] == '\\') {
                    return true;
                }

                if (input.len < 3) {
                    return false;
                }

                if (input[1] == ':' && switch (input[2]) {
                    '/', '\\' => true,
                    else => false,
                }) {
                    return true;
                }
            }

            return false;
        }

        fn append(*_, input: auto): Result(void) {
            const needs_sep = _.len() > 0 && switch (comptime opts.sep) {
                .any => switch (_.slice()[_.len() - 1]) {
                    '/', '\\' => false,
                    else => true,
                },
                else => _.slice()[_.len() - 1] != opts.sep.char(),
            };

            switch (comptime opts.kind) {
                .abs => {
                    const has_root = _.len() > 0;

                    if (comptime Environment.isDebug) {
                        if (has_root) {
                            bun.debugAssert(!isInputAbsolute(input));
                        } else {
                            bun.debugAssert(isInputAbsolute(input));
                        }
                    }

                    const trimmed = trimInput(if (has_root) .rel else .abs, input);

                    if (trimmed.len == 0) {
                        return;
                    }

                    if (comptime opts.check_length == .check_for_greater_than_max_path) {
                        if (_.len() + trimmed.len + @intFromBool(needs_sep) >= Options.maxPathLength(opts)) {
                            return error.MaxPathExceeded;
                        }
                    }

                    _._buf.append(trimmed, needs_sep);
                },
                .rel => {
                    bun.debugAssert(!isInputAbsolute(input));

                    const trimmed = trimInput(.rel, input);

                    if (trimmed.len == 0) {
                        return;
                    }

                    if (comptime opts.check_length == .check_for_greater_than_max_path) {
                        if (_.len() + trimmed.len + @intFromBool(needs_sep) >= Options.maxPathLength(opts)) {
                            return error.MaxPathExceeded;
                        }
                    }

                    _._buf.append(trimmed, needs_sep);
                },
                .any => {
                    const input_is_absolute = isInputAbsolute(input);

                    if (comptime Environment.isDebug) {
                        if (needs_sep) {
                            bun.debugAssert(!input_is_absolute);
                        }
                    }

                    const trimmed = trimInput(if (_.len() > 0)
                        // anything appended to an existing path should be trimmed
                        // as a relative path
                        .rel
                    else if (isInputAbsolute(input))
                        // path is empty, trim based on input
                        .abs
                    else
                        .rel, input);

                    if (trimmed.len == 0) {
                        return;
                    }

                    if (comptime opts.check_length == .check_for_greater_than_max_path) {
                        if (_.len() + trimmed.len + @intFromBool(needs_sep) >= Options.maxPathLength(opts)) {
                            return error.MaxPathExceeded;
                        }
                    }

                    _._buf.append(trimmed, needs_sep);
                },
            }
        }

        fn appendFmt(*_, comptime fmt: []const u8, args: auto): Result(void) {
            // TODO: there's probably a better way to do this. needed for trimming slashes
            const PoolPath = Path(.{ .buf_type = .pool });
            const temp_guard = DeinitGuard<PoolPath>{ item: .init() };

            const input = switch (comptime opts.buf_type) {
                .pool => std.fmt.bufPrint(temp_guard.item._buf.pooled, fmt, args) catch {
                    if (comptime opts.check_length == .check_for_greater_than_max_path) {
                        return error.MaxPathExceeded;
                    }
                    unreachable;
                },
            };

            return _.append(input);
        }

        fn join(*_, parts: []const []const Options.pathUnit(opts)): Result(void) {
            switch (comptime opts.unit) {
                .u8 => {},
                .u16 => @compileError("unsupported unit type"),
                .os => if (Environment.isWindows) @compileError("unsupported unit type"),
            }

            switch (comptime opts.kind) {
                .abs => {},
                .rel => @compileError("cannot join with relative path"),
                .any => {
                    bun.debugAssert(_.isAbsolute());
                },
            }

            const cloned_guard = DeinitGuard<@This()>{ item: _.clone() };

            switch (comptime opts.buf_type) {
                .pool => {
                    const joined = bun.path.joinAbsStringBuf(
                        cloned_guard.item.slice(),
                        _._buf.pooled,
                        parts,
                        switch (opts.sep) {
                            .any, .auto => .auto,
                            .posix => .posix,
                            .windows => .windows,
                        },
                    );

                    const trimmed = trimInput(.abs, joined);
                    _._buf.len = trimmed.len;
                },
            }
        }

        fn appendJoin(*_, part: auto): Result(void) {
            switch (comptime opts.kind) {
                .abs => {},
                .rel => @compileError("cannot join with relative path"),
                .any => {
                    bun.debugAssert(_.isAbsolute());
                },
            }

            switch (comptime @TypeOf(part)) {
                []u8, []const u8 => {
                    switch (comptime Options.pathUnit(opts)) {
                        u8 => {
                            const cwd_path_buf = bun.path_buffer_pool.get();
                            const cwd_buf_guard = PathBufGuard<@TypeOf(cwd_path_buf)>{ buf: cwd_path_buf };
                            const current_slice = _.slice();
                            const cwd_path = cwd_buf_guard.buf[0..current_slice.len];
                            bun.copy(u8, cwd_path, current_slice);

                            const parts: [2][]const u8 = [ cwd_path, part ];
                            const joined = bun.path.joinStringBuf(
                                _._buf.pooled,
                                &parts,
                                switch (opts.sep) {
                                    .any, .auto => .auto,
                                    .posix => .posix,
                                    .windows => .windows,
                                },
                            );

                            const trimmed = trimInput(.abs, joined);
                            _._buf.len = trimmed.len;
                        },
                        u16 => {
                            const path_buf = bun.w_path_buffer_pool.get();
                            const path_buf_guard = WPathBufGuard<@TypeOf(path_buf)>{ buf: path_buf };
                            const converted = bun.strings.convertUTF8toUTF16InBuffer(path_buf_guard.buf, part);
                            return _.appendJoin(converted);
                        },
                        else => @compileError("unsupported unit type"),
                    }
                },
                []u16, []const u16 => {
                    switch (comptime Options.pathUnit(opts)) {
                        u16 => {
                            const cwd_path_buf = bun.w_path_buffer_pool.get();
                            const cwd_buf_guard = WPathBufGuard<@TypeOf(cwd_path_buf)>{ buf: cwd_path_buf };
                            const current_slice = _.slice();
                            const cwd_path = cwd_buf_guard.buf[0..current_slice.len];
                            bun.copy(u16, cwd_path, current_slice);

                            const parts: [2][]const u16 = [ cwd_path, part ];
                            const joined = bun.path.joinStringBufW(
                                _._buf.pooled,
                                &parts,
                                switch (opts.sep) {
                                    .any, .auto => .auto,
                                    .posix => .posix,
                                    .windows => .windows,
                                },
                            );

                            const trimmed = trimInput(.abs, joined);
                            _._buf.len = trimmed.len;
                        },
                        u8 => {
                            const path_buf = bun.path_buffer_pool.get();
                            const path_buf_guard = PathBufGuard<@TypeOf(path_buf)>{ buf: path_buf };
                            const converted = bun.strings.convertUTF16toUTF8InBuffer(path_buf_guard.buf, part) catch {
                                return .initError(.MaxPathExceeded);
                            };
                            return _.appendJoin(converted);
                        },
                        else => @compileError("unsupported unit type"),
                    }
                },
                else => @compileError("unsupported type: " ++ @typeName(@TypeOf(part))),
            }
        }

        fn relative(to: auto): RelPath(opts) {
            switch (comptime opts.buf_type) {
                .pool => {
                    let output: RelPath(opts) = .init();
                    const rel = bun.path.relativeBufZ(output._buf.pooled, _.slice(), to.slice());
                    const trimmed = trimInput(.rel, rel);
                    output._buf.len = trimmed.len;
                    return output;
                },
            }
        }

        fn undo(*_, n_components: usize) {
            const min_len = switch (comptime opts.kind) {
                .abs => rootLen(_.slice()) ?? 0,
                .rel => 0,
                .any => min_len: {
                    if (_.isAbsolute()) {
                        break :min_len rootLen(_.slice()) ?? 0;
                    }
                    break :min_len 0;
                },
            };

            const any_seps: [2]Options.pathUnit(opts) = [ std.fs.path.sep_posix, std.fs.path.sep_windows ];
            let i: usize = 0;
            while (i < n_components) {
                const maybe_slash = switch (comptime opts.sep) {
                    .any => std.mem.lastIndexOfAny(Options.pathUnit(opts), _.slice(), &any_seps),
                    .auto => std.mem.lastIndexOfScalar(Options.pathUnit(opts), _.slice(), std.fs.path.sep),
                    .posix => std.mem.lastIndexOfScalar(Options.pathUnit(opts), _.slice(), std.fs.path.sep_posix),
                    .windows => std.mem.lastIndexOfScalar(Options.pathUnit(opts), _.slice(), std.fs.path.sep_windows),
                };
                if (slash := maybe_slash) {
                    if (slash < min_len) {
                        _._buf.setLength(min_len);
                        return;
                    }

                    _._buf.setLength(slash);
                    i += 1;
                } else {
                    _._buf.setLength(min_len);
                    return;
                }
            }
        }

        pub const ResetScope = PathResetScope(opts);

        fn save(*_): ResetScope {
            return .{ .path = _, .saved_len = _.len() };
        }
    };
}
