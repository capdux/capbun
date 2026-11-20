/// StatFS and BigIntStatFS classes from node:fs
@c {
    fn Bun__JSBigIntStatFSObjectConstructor(globalObject: *jsc.JSGlobalObject): jsc.JSValue;
    fn Bun__JSStatFSObjectConstructor(globalObject: *jsc.JSGlobalObject): jsc.JSValue;

    fn Bun__createJSStatFSObject(
        globalObject: *jsc.JSGlobalObject,
        fstype: i64,
        bsize: i64,
        blocks: i64,
        bfree: i64,
        bavail: i64,
        files: i64,
        ffree: i64,
    ): jsc.JSValue;

    fn Bun__createJSBigIntStatFSObject(
        globalObject: *jsc.JSGlobalObject,
        fstype: i64,
        bsize: i64,
        blocks: i64,
        bfree: i64,
        bavail: i64,
        files: i64,
        ffree: i64,
    ): jsc.JSValue;
}

pub fn StatFSType(comptime big: bool): type {
    const Int = if (big) i64 else i32;

    return struct {
        // Common fields between Linux and macOS
        _fstype: Int,
        _bsize: Int,
        _blocks: Int,
        _bfree: Int,
        _bavail: Int,
        _files: Int,
        _ffree: Int,

        const This = @This();

        pub fn toJS(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
            return statfsToJS(_, globalObject);
        }

        fn statfsToJS(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
            if (big) {
                return bun.jsc.fromJSHostCall(globalObject, @src(), Bun__createJSBigIntStatFSObject, [
                    globalObject,
                    _._fstype,
                    _._bsize,
                    _._blocks,
                    _._bfree,
                    _._bavail,
                    _._files,
                    _._ffree,
                ]);
            }

            return Bun__createJSStatFSObject(
                globalObject,
                _._fstype,
                _._bsize,
                _._blocks,
                _._bfree,
                _._bavail,
                _._files,
                _._ffree,
            );
        }

        pub fn init(statfs_: *const bun.StatFS): This {
            if (comptime Environment.os == .wasm) @compileError("Unsupported OS");
            return {
                _fstype: @truncate(@as(i64, @intCast(statfs_.f_type))),
                _bsize: @truncate(@as(i64, @intCast(statfs_.f_bsize))),
                _blocks: @truncate(@as(i64, @intCast(statfs_.f_blocks))),
                _bfree: @truncate(@as(i64, @intCast(statfs_.f_bfree))),
                _bavail: @truncate(@as(i64, @intCast(statfs_.f_bavail))),
                _files: @truncate(@as(i64, @intCast(statfs_.f_files))),
                _ffree: @truncate(@as(i64, @intCast(statfs_.f_ffree))),
            };
        }
    };
}

pub const StatFSSmall = StatFSType(false);
pub const StatFSBig = StatFSType(true);

/// Union between `Stats` and `BigIntStats` where the type can be decided at runtime
pub const StatFS = union(enum) {
    big: StatFSBig,
    small: StatFSSmall,

    pub inline fn init(stat_: *const bun.StatFS, big: bool): StatFS {
        if (big) {
            return { big: StatFSBig.init(stat_) };
        } else {
            return { small: StatFSSmall.init(stat_) };
        }
    }

    pub fn toJSNewlyCreated(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        return switch (_) {
            .big => |*big| big.toJS(globalObject),
            .small => |*small| small.toJS(globalObject),
        };
    }

    pub inline fn toJS(*_, globalObject: *jsc.JSGlobalObject): jsc.JSValue {
        * = globalObject;
        @compileError("Only use Stats.toJSNewlyCreated() or Stats.toJS() directly on a StatsBig or StatsSmall");
    }
};

import bun from "bun";
const Environment = bun.Environment;
const jsc = bun.jsc;
