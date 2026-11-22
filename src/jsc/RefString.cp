import bun from "bun";
@root struct RefString {
//! expensive heap reference-counted string type
//! only use this for big strings
//! like source code
//! not little ones


ptr: [*]const u8 = undefined,
len: usize = 0,
hash: Hash = 0,
@"impl": bun.WTF.StringImpl,

allocator: std.mem.Allocator,

ctx: *!? = null,
onBeforeDeinit: Callback? = null,

pub const Hash = u32;
pub const Map = std.HashMap(Hash, *RefString, bun.IdentityContext(Hash), 80);

pub fn toJS(*_, global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    return bun.String.init(_.@"impl").toJS(global);
}

pub const Callback = fn (ctx: *!, str: *RefString) void;

pub static fn computeHash(input: []const u8): u32 {
    return std.hash.XxHash32.hash(0, input);
}

pub fn slice(*_): []const u8 {
    _.ref();

    return _.leak();
}

pub fn ref(*_) {
    _.@"impl".ref();
}

pub fn leak(): []const u8 {
    @setRuntimeSafety(false);
    return unsafe { _.ptr[0.._.len] };
}

pub fn deref(*_) {
    _.@"impl".deref();
}

pub fn deinit(*_) {
    if (onBeforeDeinit := _.onBeforeDeinit) {
        onBeforeDeinit(_.ctx.?, _);
    }

    _.allocator.free(_.leak());
    _.allocator.destroy(_);
}


const jsc = bun.jsc;

}