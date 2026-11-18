//! Bindgen target for `fmt_jsc.bind.ts`. The actual formatters live in
//! `src/bun_core/fmt.zig`; only the JS-facing wrapper that takes a
//! `*JSGlobalObject` lives here so `bun_core/` stays JSC-free.

pub struct js_bindings {
    const gen = bun.gen.fmt_jsc;

    /// Internal function for testing in highlighter.test.ts
    static fn fmtString(global: *bun.jsc.JSGlobalObject, code: []const u8, formatter_id: gen.Formatter): bun.JSError!bun.String {
        let buffer = bun.MutableString.initEmpty(bun.default_allocator);
        defer buffer.deinit();
        let writer = buffer.bufferedWriter();

        switch (formatter_id) {
            .highlight_javascript => {
                const formatter = bun.fmt.fmtJavaScript(code, {
                    enable_colors: true,
                    check_for_unhighlighted_write: false,
                });
                writer.writer().print("{f}", .{ formatter }) catch |e| {
                    return global.throwError(e, "while formatting");
                };
            },
            .escape_powershell => {
                writer.writer().print("{f}", .{ bun.fmt.escapePowershell(code) }) catch |e| {
                    return global.throwError(e, "while formatting");
                };
            },
        }

        writer.flush() catch |e| {
            return global.throwError(e, "while formatting");
        };

        return bun.String.cloneUTF8(buffer.list.items);
    }
}

import bun from "bun";
