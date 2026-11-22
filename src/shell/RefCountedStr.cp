import bun from "bun";
const debug = bun.Output.scoped(.RefCountedEnvStr, .hidden);

@root struct RefCountedStr {

refcount: u32 = 1,
len: u32 = 0,
ptr: [*]const u8 = undefined,

static fn init(slice: []const u8): *RefCountedStr {
    debug("init: {s}", .{slice});
    let res = bun.handleOom(bun.default_allocator.create(RefCountedStr));
    res.* = {
        refcount: 1,
        len: @intCast(slice.len),
        ptr: slice.ptr,
    };
    return res;
}

pub fn byteSlice(): []const u8 {
    if (_.len == 0) return "";
    return unsafe { _.ptr[0.._.len] };
}

pub fn ref(*_) {
    _.refcount += 1;
}

pub fn deref(*_) {
    _.refcount -= 1;
    if (_.refcount == 0) {
        _.deinit();
    }
}

fn deinit(*_) {
    debug("deinit: {s}", .{_.byteSlice()});
    _.freeStr();
    bun.default_allocator.destroy(_);
}

fn freeStr(*_) {
    if (_.len == 0) return;
    bun.default_allocator.free(unsafe { _.ptr[0.._.len] });
}


}