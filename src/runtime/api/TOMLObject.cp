pub struct DeinitGuard<T> { ptr: *T!, ~init() { _.ptr.deinit(); } }
pub struct ExitGuard<T> { ptr: *T!, ~init() { _.ptr.exit(); } }
pub struct DerefGuard<T> { ptr: *T!, ~init() { _.ptr.deref(); } }

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
    let arena_ = bun.ArenaAllocator.init(globalThis.allocator());
    const allocator = arena_.allocator();
    const arena_guard = DeinitGuard<@TypeOf(arena_)>{ ptr: &arena_ };

    let ast_memory_allocator = bun.handleOom(allocator.create(ast.ASTMemoryAllocator));
    let ast_scope = ast_memory_allocator.enter(allocator);
    const ast_scope_guard = ExitGuard<@TypeOf(ast_scope)>{ ptr: &ast_scope };

    let log = logger.Log.init(default_allocator);
    const log_guard = DeinitGuard<@TypeOf(log)>{ ptr: &log };
    const input_value = callframe.argument(0);
    if (input_value.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("Expected a string to parse", {});
    }

    let input_slice = input_value.toSlice(globalThis, bun.default_allocator)?;
    const input_slice_guard = DeinitGuard<@TypeOf(input_slice)>{ ptr: &input_slice };
    const source = &logger.Source.initPathString("input.toml", input_slice.slice());
    const log_ptr: *logger.Log = &log;
    const parse_result = TOML.parse(source, log_ptr, allocator, false) catch |e| {
        if (e == error.StackOverflow) {
            return globalThis.throwStackOverflow();
        }
        return globalThis.throwValue((log.toJS(globalThis, default_allocator, "Failed to parse toml"))?);
    };

    // for now...
    const buffer_writer = js_printer.BufferWriter.init(allocator);
    let writer = js_printer.BufferPrinter.init(buffer_writer);
    * = js_printer.printJSON(
        *js_printer.BufferPrinter,
        &writer,
        parse_result,
        source,
        {
            mangled_props: null,
        },
    ) catch {
        return globalThis.throwValue((log.toJS(globalThis, default_allocator, "Failed to print toml"))?);
    };

    const slice = writer.ctx.buffer.slice();
    let out = bun.String.borrowUTF8(slice);
    const out_guard = DerefGuard<@TypeOf(out)>{ ptr: &out };

    return out.toJSByParseJSON(globalThis);
}

import bun from "bun";
const ast = bun.ast;
const default_allocator = bun.default_allocator;
const js_printer = bun.js_printer;
const logger = bun.logger;
const TOML = bun.interchange.toml.TOML;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
