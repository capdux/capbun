//! `fromJS` bridges for `bun.options.{Target,Format,Loader}` and `CompileTarget`.
//! Keeps `src/bundler/` free of `JSValue`/`JSGlobalObject` references.

import bun from "bun";
import CompileTarget from "../options_types/CompileTarget.zig";

const jsc = bun.jsc;
const options = bun.options;
const strings = bun.strings;

pub struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}

pub fn targetFromJS(global: *jsc.JSGlobalObject, value: jsc.JSValue): options.Target? ! bun.JSError {
    if (!value.isString()) {
        return global.throwInvalidArguments("target must be a string", []);
    }
    return options.Target.Map.fromJS(global, value);
}

pub fn formatFromJS(global: *jsc.JSGlobalObject, format: jsc.JSValue): options.Format? ! bun.JSError {
    if (format.isUndefinedOrNull()) return null;

    if (!format.isString()) {
        return global.throwInvalidArguments("format must be a string", []);
    }

    if (fmt := options.Format.Map.fromJS(global, format)?) return fmt;
    return global.throwInvalidArguments("Invalid format - must be esm, cjs, or iife", []);
}

pub fn loaderFromJS(global: *jsc.JSGlobalObject, loader: jsc.JSValue): options.Loader? ! bun.JSError {
    if (loader.isUndefinedOrNull()) return null;

    if (!loader.isString()) {
        return global.throwInvalidArguments("loader must be a string", []);
    }

    let zig_str = jsc.ZigString.init("");
    const zig_str_ptr: *jsc.ZigString = &zig_str;
    loader.toZigString(zig_str_ptr, global)?;
    if (zig_str.len == 0) return null;

    const slice_guard = DeinitGuard<jsc.ZigString.Slice>{
        val: zig_str.toSlice(bun.default_allocator),
    };

    const raw_loader = slice_guard.val.slice();
    const ldr = options.Loader.fromString(raw_loader) ?? return global.throwInvalidArguments("invalid loader - must be js, jsx, tsx, ts, css, json, jsonc, json5, toml, yaml, xml, text, wasm, or md", []);
    switch (ldr) {
        .file, .napi, .base64, .dataurl, .bunsh, .sqlite, .sqlite_embedded, .html => {
            return global.throwInvalidArguments("loader \"{s}\" is not supported in Bun.Transpiler", [raw_loader]);
        },
        else => {},
    }
    return ldr;
}

// ── CompileTarget ──────────────────────────────────────────────────────────
pub fn compileTargetFromJS(global: *jsc.JSGlobalObject, value: jsc.JSValue): CompileTarget ! bun.JSError {
    const slice_guard = DeinitGuard<jsc.ZigString.Slice>{
        val: value.toSlice(global, bun.default_allocator)?,
    };
    if (!strings.hasPrefixComptime(slice_guard.val.slice(), "bun-")) {
        return global.throwInvalidArguments("Expected compile target to start with 'bun-', got {s}", [slice_guard.val.slice()]);
    }

    return compileTargetFromSlice(global, slice_guard.val.slice());
}

pub fn compileTargetFromSlice(global: *jsc.JSGlobalObject, slice_with_bun_prefix: []const u8): CompileTarget ! bun.JSError {
    const slice = slice_with_bun_prefix["bun-".len..];
    const target_parsed = CompileTarget.tryFrom(slice) catch {
        return global.throwInvalidArguments("Unknown compile target: {s}", [slice_with_bun_prefix]);
    };
    if (!target_parsed.isSupported()) {
        return global.throwInvalidArguments("Unsupported compile target: {s}", [slice_with_bun_prefix]);
    }

    return target_parsed;
}
