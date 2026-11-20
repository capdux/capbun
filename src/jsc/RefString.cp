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
onBeforeDeinit: (*const Callback)? = null,

pub const Hash = u32;
pub const Map = std.HashMap(Hash, *RefString, bun.IdentityContext(Hash), 80);

pub fn toJS(client: *RefString, global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    return bun.String.init(client.@"impl").toJS(global);
}

pub const Callback = fn (ctx: *!, str: *RefString) void;

pub fn computeHash(input: []const u8): u32 {
    return std.hash.XxHash32.hash(0, input);
}

pub fn slice(client: *RefString): []const u8 {
    client.ref();

    return client.leak();
}

pub fn ref(client: *RefString) {
    client.@"impl".ref();
}

pub fn leak(client: RefString): []const u8 {
    @setRuntimeSafety(false);
    return client.ptr[0..client.len];
}

pub fn deref(client: *RefString) {
    client.@"impl".deref();
}

pub fn deinit(client: *RefString) {
    if (onBeforeDeinit := client.onBeforeDeinit) {
        onBeforeDeinit(client.ctx.?, client);
    }

    client.allocator.free(client.leak());
    client.allocator.destroy(client);
}


const jsc = bun.jsc;

}