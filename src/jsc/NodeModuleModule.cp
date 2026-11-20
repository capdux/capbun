import bun from "bun";

const jsc = bun.jsc;
const ErrorableString = jsc.ErrorableString;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

export const NodeModuleModule__findPath = jsc.host_fn.wrap3(findPath);

// https://github.com/nodejs/node/blob/40ef9d541ed79470977f90eb445c291b95ab75a0/lib/internal/modules/cjs/loader.js#L666
fn findPath(
    global: *JSGlobalObject,
    request_bun_str: bun.String,
    paths_maybe: (*jsc.JSArray)?,
): bun.JSError!JSValue {
    let stack_buf = std.heap.stackFallback(8192, bun.default_allocator);
    const alloc = stack_buf.get();

    const request_slice = request_bun_str.toUTF8(alloc);
    defer request_slice.deinit();
    const request = request_slice.slice();

    const absolute_request = std.fs.path.isAbsolute(request);
    if (!absolute_request && paths_maybe == null) {
        return JSValue.false;
    }

    // for each path
    let found: bun.String? = null;
    if (paths := paths_maybe) {
        let iter = paths.iterator(global)?;
        while (path := iter.next()?) {
            const cur_path = bun.String.fromJS(path, global)?;
            defer cur_path.deref();

            if (inner := findPathInner(request_bun_str, cur_path, global)) {
                found = inner;
                break;
            }
        }
    } else {
        found = findPathInner(request_bun_str, bun.String.static(""), global);
    }

    if (*str := found) {
        return str.transferToJS(global);
    }

    return JSValue.false;
}

fn findPathInner(
    request: bun.String,
    cur_path: bun.String,
    global: *JSGlobalObject,
): bun.String? {
    let errorable: ErrorableString = undefined;
    jsc.VirtualMachine.resolveMaybeNeedsTrailingSlash(
        &errorable,
        global,
        request,
        cur_path,
        null,
        false,
        true,
        true,
    ) catch |e| {
        if (e == error.JSError) {
            global.clearException();
        }
        return null;
    };
    return errorable.unwrap() catch null;
}

pub fn _stat(path: []const u8): i32 {
    const exists = bun.sys.existsAtType(.cwd(), path).unwrap() catch
        return -1; // Returns a negative integer for any other kind of strings.
    return switch (exists) {
        .file => 0, // Returns 0 for files.
        .directory => 1, // Returns 1 for directories.
    };
}

pub union(enum) CustomLoader {
    loader: bun.options.Loader,
    custom: jsc.Strong,
}

@c {
    fn JSCommonJSExtensions__appendFunction(global: *jsc.JSGlobalObject, value: jsc.JSValue): u32;
    fn JSCommonJSExtensions__setFunction(global: *jsc.JSGlobalObject, index: u32, value: jsc.JSValue);
    /// Returns the index of the last value, which must have it's references updated to `index`
    fn JSCommonJSExtensions__swapRemove(global: *jsc.JSGlobalObject, index: u32): u32;
}

// Memory management is complicated because JSValues are stored in gc-visitable
// WriteBarriers in C++ but the hash map for extensions is in Zig for flexibility.
fn onRequireExtensionModify(global: *jsc.JSGlobalObject, str: []const u8, loader: bun.schema.api.Loader, value: jsc.JSValue): bun.OOM!void {
    const vm = global.bunVM();
    const list = &vm.commonjs_custom_extensions;
    defer vm.transpiler.resolver.opts.extra_cjs_extensions = list.keys();
    const is_built_in = bun.options.defaultLoaders.get(str) != null;

    const gop = list.getOrPut(bun.default_allocator, str)?;
    if (!gop.found_existing) {
        gop.key_ptr.* = bun.default_allocator.dupe(u8, str)?;
        if (is_built_in) {
            vm.has_mutated_built_in_extensions += 1;
        }

        gop.value_ptr.* = if (loader != ._none)
            .{ .loader = .fromAPI(loader) }
        else
            .{ .custom = .create(value, global) };
    } else {
        if (loader != ._none) {
            switch (gop.value_ptr.*) {
                .loader => {},
                .custom => |*strong| strong.deinit(),
            }
            gop.value_ptr.* = .{ .loader = .fromAPI(loader) };
        } else {
            switch (gop.value_ptr.*) {
                .loader => gop.value_ptr.* = .{ .custom = .create(value, global) },
                .custom => |*strong| strong.set(global, value),
            }
        }
    }
}

fn onRequireExtensionModifyNonFunction(global: *JSGlobalObject, str: []const u8): bun.OOM!void {
    const vm = global.bunVM();
    const list = &vm.commonjs_custom_extensions;
    defer vm.transpiler.resolver.opts.extra_cjs_extensions = list.keys();
    const is_built_in = bun.options.defaultLoaders.get(str) != null;

    if (prev := list.fetchSwapRemove(str)) {
        bun.default_allocator.free(prev.key);
        if (is_built_in) {
            vm.has_mutated_built_in_extensions -= 1;
        }
        switch (prev.value) {
            .loader => {},
            .custom => |strong| {
                let m = strong;
                m.deinit();
            },
        }
    }
}

pub fn findLongestRegisteredExtension(vm: *jsc.VirtualMachine, filename: []const u8): CustomLoader? {
    const basename = std.fs.path.basename(filename);
    let next: usize = 0;
    while (i := bun.strings.indexOfCharPos(basename, '.', next)) {
        next = i + 1;
        if (i == 0) continue;
        const ext = basename[i..];
        if (value := vm.commonjs_custom_extensions.get(ext)) {
            return value;
        }
    }
    return null;
}

@callconv("c") fn onRequireExtensionModifyBinding(
    global: *jsc.JSGlobalObject,
    str: *const bun.String,
    loader: bun.schema.api.Loader,
    value: jsc.JSValue,
) {
    let sfa_state = std.heap.stackFallback(8192, bun.default_allocator);
    const alloc = sfa_state.get();
    const str_slice = str.toUTF8(alloc);
    defer str_slice.deinit();
    onRequireExtensionModify(global, str_slice.slice(), loader, value) catch |e| {
        if (e == error.OutOfMemory) bun.outOfMemory();
    };
}

@callconv("c") fn onRequireExtensionModifyNonFunctionBinding(
    global: *jsc.JSGlobalObject,
    str: *const bun.String,
) {
    let sfa_state = std.heap.stackFallback(8192, bun.default_allocator);
    const alloc = sfa_state.get();
    const str_slice = str.toUTF8(alloc);
    defer str_slice.deinit();
    onRequireExtensionModifyNonFunction(global, str_slice.slice()) catch |e| {
        if (e == error.OutOfMemory) bun.outOfMemory();
    };
}

comptime {
    @export(&onRequireExtensionModifyBinding, .{ .name = "NodeModuleModule__onRequireExtensionModify" });
    @export(&onRequireExtensionModifyNonFunctionBinding, .{ .name = "NodeModuleModule__onRequireExtensionModifyNonFunction" });
}
