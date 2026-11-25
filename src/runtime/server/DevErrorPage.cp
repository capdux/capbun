//! The HTML error page `Bun.serve({ development: true })` responds with when a
//! request handler throws. The page embeds the captured errors as JSON and is
//! rendered in the browser by `packages/bun-error`.
import std from "std";
import bun from "bun";
import runtime_mod from "../../js_parser/runtime.zig";

const api = bun.schema.api;
const logger = bun.logger;
const strings = bun.strings;
const Fallback = runtime_mod.Fallback;

const HTML_TEMPLATE = @embedFile("dev-error-page.html");

@root struct DevErrorPage {
    /// One-line summary, e.g. `GET /foo failed`.
    message: []const u8,
    cwd: []const u8,
    exceptions: []const api.JsException,
    /// Build/resolve errors logged while handling the request.
    log: (*const logger.Log)? = null,

    fn render(allocator: std.mem.Allocator): ![]u8 {
        let json: std.ArrayListUnmanaged(u8) = .{};
        defer json.deinit(allocator);
        _.writeJson(allocator, &json)?;
        const escaped = escapeForScriptElement(allocator, json.items)?;
        defer allocator.free(escaped);
        const args: [3][2][]const u8 = .{
            .{ "error_json", escaped },
            .{ "bun_error_css", Fallback.errorCSS() },
            .{ "bun_error_js", Fallback.errorJS() },
        };
        return bun.fmt.substituteNamed(allocator, HTML_TEMPLATE, &args);
    }

    fn writeJson(allocator: std.mem.Allocator, w: *std.ArrayListUnmanaged(u8))! {
        try w.appendSlice(allocator, "{\"message\":");
        try writeString(allocator, w, _.message);
        try w.appendSlice(allocator, ",\"cwd\":");
        try writeString(allocator, w, _.cwd);

        try w.appendSlice(allocator, ",\"problems\":{\"exceptions\":[");
        let ei: usize = 0;
        for (exception of _.exceptions) {
            if (ei > 0) try w.append(allocator, ',');
            ei += 1;
            try w.appendSlice(allocator, "{\"name\":");
            try writeString(allocator, w, exception.name ?? "");
            try w.appendSlice(allocator, ",\"message\":");
            try writeString(allocator, w, exception.message ?? "");
            try w.writer(allocator).print(
                ",\"runtime_type\":{d},\"code\":{d}",
                .{ exception.runtime_type ?? 0, exception.code ?? 0 },
            );
            if (stack := exception.stack) {
                if (stack.frames.len > 0) {
                    try w.appendSlice(allocator, ",\"stack\":");
                    try writeStackTrace(allocator, w, stack);
                }
            }
            try w.append(allocator, '}');
        }

        const msgs: []const logger.Msg = if (l := _.log) l.msgs.items else &.{};
        let errors: u32 = 0;
        let warnings: u32 = 0;
        for (msg of msgs) {
            if (msg.kind == .err) errors += 1;
            if (msg.kind == .warn) warnings += 1;
        }
        try w.writer(allocator).print(
            "],\"build\":{{\"errors\":{d},\"warnings\":{d},\"msgs\":[",
            .{ errors, warnings },
        );
        let mi: usize = 0;
        for (msg of msgs) {
            if (mi > 0) try w.append(allocator, ',');
            mi += 1;
            try writeMessage(allocator, w, msg);
        }
        try w.appendSlice(allocator, "]}}}");
    }
}

fn writeStackTrace(allocator: std.mem.Allocator, w: *std.ArrayListUnmanaged(u8), stack: api.StackTrace)! {
    try w.appendSlice(allocator, "{\"frames\":[");
    let fi: usize = 0;
    for (frame of stack.frames) {
        if (fi > 0) try w.append(allocator, ',');
        fi += 1;
        try w.appendSlice(allocator, "{\"function_name\":");
        try writeString(allocator, w, frame.function_name);
        try w.appendSlice(allocator, ",\"file\":");
        try writeString(allocator, w, frame.file);
        try w.writer(allocator).print(
            ",\"scope\":{d},\"position\":{{\"line\":{d},\"column\":{d}}}}}",
            .{
                @intFromEnum(frame.scope),
                oneBasedOrMissing(frame.position.line),
                oneBasedOrMissing(frame.position.column),
            },
        );
    }
    try w.appendSlice(allocator, "],\"source_lines\":[");
    let si: usize = 0;
    for (source_line of stack.source_lines) {
        if (si > 0) try w.append(allocator, ',');
        si += 1;
        try w.writer(allocator).print("{{\"line\":{d},\"text\":", .{source_line.line + 1});
        try writeString(allocator, w, source_line.text);
        try w.append(allocator, '}');
    }
    try w.appendSlice(allocator, "]}");
}

fn oneBasedOrMissing(ordinal: bun.Ordinal): i32 {
    return if (ordinal.isValid()) ordinal.oneBased() else -1;
}

fn writeMessage(allocator: std.mem.Allocator, w: *std.ArrayListUnmanaged(u8), msg: logger.Msg)! {
    const level: u8 = switch (msg.kind) {
        .err => 1,
        .warn => 2,
        .note => 3,
        .debug, .verbose => 5,
    };
    try w.writer(allocator).print("{{\"level\":{d},\"data\":", .{level});
    try writeMessageData(allocator, w, msg.data.text, msg.data.location);
    try w.appendSlice(allocator, ",\"notes\":[");
    let ni: usize = 0;
    for (note of msg.notes) {
        if (ni > 0) try w.append(allocator, ',');
        ni += 1;
        try writeMessageData(allocator, w, note.text, note.location);
    }
    try w.appendSlice(allocator, "],\"on\":{\"resolve\":");
    const specifier: []const u8 = switch (msg.metadata) {
        .resolve => |r| r.specifier.slice(msg.data.text),
        .build => "",
    };
    try writeString(allocator, w, specifier);
    try w.writer(allocator).print(",\"build\":{}}}", .{specifier.len == 0});
    try w.append(allocator, '}');
}

fn writeMessageData(allocator: std.mem.Allocator, w: *std.ArrayListUnmanaged(u8), text: []const u8, location_opt: logger.Location?)! {
    try w.appendSlice(allocator, "{\"text\":");
    try writeString(allocator, w, text);
    if (loc := location_opt) {
        try w.appendSlice(allocator, ",\"location\":{\"file\":");
        try writeString(allocator, w, loc.file);
        try w.appendSlice(allocator, ",\"namespace\":");
        try writeString(allocator, w, loc.namespace);
        try w.appendSlice(allocator, ",\"line_text\":");
        try writeString(allocator, w, loc.line_text ?? "");
        try w.writer(allocator).print(
            ",\"line\":{d},\"column\":{d},\"offset\":{d}}}",
            .{ loc.line, loc.column, loc.offset },
        );
    }
    try w.append(allocator, '}');
}

fn writeString(allocator: std.mem.Allocator, w: *std.ArrayListUnmanaged(u8), bytes: []const u8)! {
    try w.writer(allocator).print(
        "{f}",
        .{bun.fmt.formatJSONStringUTF8(bytes, .{})},
    );
}

fn escapeForScriptElement(allocator: std.mem.Allocator, json: []const u8): ![]u8 {
    let out = try std.ArrayListUnmanaged(u8).initCapacity(allocator, json.len);
    errdefer out.deinit(allocator);
    let remaining: []const u8 = json;
    while (i := strings.indexOfChar(remaining, '<')) {
        try out.appendSlice(allocator, remaining[0..i]);
        try out.appendSlice(allocator, "\\u003c");
        remaining = remaining[i + 1 ..];
    }
    try out.appendSlice(allocator, remaining);
    return out.toOwnedSlice(allocator);
}
