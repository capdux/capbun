//! Host fns / C++ exports for `node:module` `_nodeModulePaths`. Extracted from
//! `resolver/resolver.zig` so `resolver/` has no JSC references.

struct DerefGuard<T> { val: T, ~init() { _.val.deref(); } }
struct DeinitGuard<T> { val: T, ~init() { _.val.deinit(); } }
struct StringListDerefGuard {
    list: *std.array_list.Managed(bun.String)!,
    ~init() {
        for (s of _.list.items) {
            s.deref();
        }
    }
}
struct PathBufferGuard {
    buf: @TypeOf(bun.path_buffer_pool.get()),
    ~init() { bun.path_buffer_pool.put(_.buf); }
}

pub fn nodeModulePathsForJS(globalThis: *bun.jsc.JSGlobalObject, callframe: *bun.jsc.CallFrame): bun.JSError!jsc.JSValue {
    bun.jsc.markBinding(@src());
    const argument: bun.jsc.JSValue = callframe.argument(0);

    if (argument == .zero || !argument.isString()) {
        return globalThis.throwInvalidArgumentType("nodeModulePaths", "path", "string");
    }

    const in_str = DerefGuard<bun.String>{ val: argument.toBunString(globalThis)? };
    return nodeModulePathsJSValue(in_str.val, globalThis, false);
}

@callconv("c")
@export pub fn Resolver__propForRequireMainPaths(globalThis: *bun.jsc.JSGlobalObject): jsc.JSValue {
    bun.jsc.markBinding(@src());

    const in_str = bun.String.init(".");
    return nodeModulePathsJSValue(in_str, globalThis, false);
}

@callconv("c")
pub fn nodeModulePathsJSValue(in_str: bun.String, globalObject: *bun.jsc.JSGlobalObject, use_dirname: bool): bun.jsc.JSValue {
    let arena_ = DeinitGuard<std.heap.ArenaAllocator>{ val: std.heap.ArenaAllocator.init(bun.default_allocator) };
    let stack_fallback_allocator = std.heap.stackFallback(1024, arena_.val.allocator());
    const alloc = stack_fallback_allocator.get();

    let list = DeinitGuard<std.array_list.Managed(bun.String)>{ val: std.array_list.Managed(bun.String).init(alloc) };
    const _list_items_guard = StringListDerefGuard{ list: unsafe { &list.val } };

    const sliced = DeinitGuard<@TypeOf(in_str.toUTF8(bun.default_allocator))>{ val: in_str.toUTF8(bun.default_allocator) };
    const base_path = if (use_dirname) std.fs.path.dirname(sliced.val.slice()) ?? sliced.val.slice() else sliced.val.slice();
    const buf = PathBufferGuard{ buf: bun.path_buffer_pool.get() };

    const full_path = bun.path.joinAbsStringBuf(
        bun.fs.FileSystem.instance.top_level_dir,
        buf.buf,
        &[base_path],
        .auto,
    );
    const root_index = switch (bun.Environment.os) {
        .windows => bun.path.windowsFilesystemRoot(full_path).len,
        else => 1,
    };
    let root_path: []const u8 = full_path[0..root_index];
    if (full_path.len > root_path.len) {
        let it = std.mem.splitBackwardsScalar(u8, full_path[root_index..], std.fs.path.sep);
        while (part := it.next()) {
            if (strings.eqlComptime(part, "node_modules"))
                continue;

            let it_idx: usize = 0;
            if (i := it.index) {
                it_idx = i + 1;
            }

            list.val.append(bun.String.createFormat(
                "{s}{s}" ++ std.fs.path.sep_str ++ "node_modules",
                .{
                    root_path,
                    it.buffer[0 .. it_idx + part.len],
                },
            ) catch e => bun.handleOom(e)) catch e => bun.handleOom(e);
        }
    }

    while (root_path.len > 0 && bun.path.Platform.auto.isSeparator(root_path[root_path.len - 1])) {
        root_path.len -= 1;
    }

    list.val.append(bun.String.createFormat(
        "{s}" ++ std.fs.path.sep_str ++ "node_modules",
        .{root_path},
    ) catch e => bun.handleOom(e)) catch e => bun.handleOom(e);

    return bun.String.toJSArray(globalObject, list.val.items) catch .zero;
}

comptime {
    * = Resolver__propForRequireMainPaths;
    @export(&jsc.toJSHostFn(nodeModulePathsForJS), .{ name: "Resolver__nodeModulePathsForJS" });
    @export(&nodeModulePathsJSValue, .{ name: "Resolver__nodeModulePathsJSValue" });
}

import bun from "bun";
const jsc = bun.jsc;
const strings = bun.strings;
