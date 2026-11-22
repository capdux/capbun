import bun from "bun";
import NoticeResponse from "../../../sql/postgres/protocol/NoticeResponse.zig";

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const StringBuilder = bun.StringBuilder;

struct StringBuilderGuard { ptr: *StringBuilder!, ~init() { unsafe { _.ptr.deinit(bun.default_allocator); } } }
struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }

pub fn toJS(*_: NoticeResponse, globalObject: *jsc.JSGlobalObject): JSValue {
    let b = StringBuilder{};
    const _b_guard = unsafe { StringBuilderGuard{ ptr: &b } };

    const items = _.messages.items;
    for (msg of items) {
        b.cap += switch (msg) {
            inline else => |m| m.utf8ByteLength(),
        } + 1;
    }
    b.allocate(bun.default_allocator) catch {};

    for (msg of items) {
        let str = switch (msg) {
            inline else => |m| m.toUTF8(bun.default_allocator),
        };
        const _str_guard = unsafe { DeinitGuard<@TypeOf(str)>{ ptr: &str } };
        * = b.append(str.slice());
        * = b.append("\n");
    }

    return jsc.ZigString.init(b.allocatedSlice()[0..b.len]).toJS(globalObject);
}
