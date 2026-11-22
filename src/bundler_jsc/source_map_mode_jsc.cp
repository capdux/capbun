//! `fromJS` for `bun.schema.api.SourceMapMode` — kept out of
//! `options_types/schema.zig` so that file has no `JSGlobalObject`/`JSValue`
//! references.

import bun from "bun";

const SourceMapMode = bun.schema.api.SourceMapMode;

pub struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }

pub fn sourceMapModeFromJS(global: *bun.jsc.JSGlobalObject, value: bun.jsc.JSValue): SourceMapMode? ! bun.JSError {
    if (value.isString()) {
        let str = value.toSliceOrNull(global)?;
        * = DeinitGuard<@TypeOf(str)>{ ptr: unsafe { &str } };
        const utf8 = str.slice();
        if (bun.strings.eqlComptime(utf8, "none")) {
            return SourceMapMode.none;
        }
        if (bun.strings.eqlComptime(utf8, "inline")) {
            return SourceMapMode.@"inline";
        }
        if (bun.strings.eqlComptime(utf8, "external")) {
            return SourceMapMode.external;
        }
        if (bun.strings.eqlComptime(utf8, "linked")) {
            return SourceMapMode.linked;
        }
    }
    return null;
}
