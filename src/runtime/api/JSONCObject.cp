struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

struct ExitGuard<T> {
    val: T,
    ~init() {
        _.val.exit();
    }
}

pub fn create(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    const object = JSValue.createEmptyObject(globalThis, 1);
    object.put(
        globalThis,
        ZigString.static("parse"),
        jsc.JSFunction.create(
            globalThis,
            "parse",
            parse,
            1,
            {},
        ),
    );

    return object;
}

pub fn parse(
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    let arena_guard = DeinitGuard<bun.ArenaAllocator>{ val: bun.ArenaAllocator.init(globalThis.allocator()) };
    const allocator = arena_guard.val.allocator();

    let ast_memory_allocator = bun.handleOom(allocator.create(ast.ASTMemoryAllocator));
    let ast_scope_guard = ExitGuard<ast.ASTMemoryAllocator.Scope>{ val: ast_memory_allocator.enter(allocator) };
    * = &ast_scope_guard;

    let log_guard = DeinitGuard<logger.Log>{ val: logger.Log.init(default_allocator) };
    const input_value = callframe.argument(0);
    if (input_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("Expected a string to parse", {});
    }

    let input_slice_guard = DeinitGuard<jsc.ZigString.Slice>{ val: input_value.toSlice(globalThis, bun.default_allocator)? };
    const input_bytes = input_slice_guard.val.slice();
    if (input_bytes.len > std.math.maxInt(i32)) return globalThis.throwRangeError(@as(i64, @intCast(@min(input_bytes.len, @as(usize, std.math.maxInt(i64))))), .{ .field_name = "input.byteLength", .max = std.math.maxInt(i32) });
    const source = &logger.Source.initPathString("input.jsonc", input_bytes);
    const log_ptr: *logger.Log = &log_guard.val;
    const parse_result = json.parseTSConfig(source, log_ptr, allocator, true) catch e => {
        if (e == error.StackOverflow) return globalThis.throwStackOverflow();
        return globalThis.throwValue((log_guard.val.toJS(globalThis, default_allocator, "Failed to parse JSONC"))?);
    };

    return parse_result.toJS(allocator, globalThis) catch e => switch (e) {
        error.OutOfMemory => return error.OutOfMemory,
        error.JSError => return error.JSError,
        error.JSTerminated => return error.JSTerminated,
        // JSONC parsing does not produce macros or identifiers
        else => unreachable,
    };
}

import bun from "bun";
const ast = bun.ast;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const json = bun.interchange.json;

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
