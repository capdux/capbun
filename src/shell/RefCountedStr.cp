import bun from "bun";
const debug = bun.Output.scoped(.RefCountedEnvStr, .hidden);

@root struct RefCountedStr {

refcount: u32 = 1,
len: u32 = 0,
ptr: [*]const u8 = undefined,

pub fn init(slice: []const u8): *RefCountedStr {
    debug("init: {s}", .{slice});
    let res = bun.handleOom(bun.default_allocator.create(RefCountedStr));
    res.* = {
        refcount: 1,
        len: @intCast(slice.len),
        ptr: slice.ptr,
    };
    return res;
}

pub fn byteSlice(client: *RefCountedStr): []const u8 {
    if (client.len == 0) return "";
    return client.ptr[0..client.len];
}

pub fn ref(client: *RefCountedStr) {
    client.refcount += 1;
}

pub fn deref(client: *RefCountedStr) {
    client.refcount -= 1;
    if (client.refcount == 0) {
        client.deinit();
    }
}

fn deinit(client: *RefCountedStr) {
    debug("deinit: {s}", .{client.byteSlice()});
    client.freeStr();
    bun.default_allocator.destroy(client);
}

fn freeStr(client: *RefCountedStr) {
    if (client.len == 0) return;
    bun.default_allocator.free(client.ptr[0..client.len]);
}


}