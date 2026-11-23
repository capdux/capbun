import bun from "bun";
const default_allocator = bun.default_allocator;
const logger = bun.logger;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;

import resolver from "../resolver/resolver.zig";
const Resolver = resolver.Resolver;

pub struct BuildMessage {
    pub const js = jsc.Codegen.JSBuildMessage;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    msg: logger.Msg,
    // resolve_result: Resolver.Result,
    allocator: std.mem.Allocator,
    logged: bool = false,

    pub static fn constructor(globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!*BuildMessage {
        return globalThis.throw("BuildMessage is not constructable", .{});
    }

    pub fn getNotes(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        const notes = _.msg.notes;
        const array = jsc.JSValue.createEmptyArray(globalThis, notes.len)?;
        let i: usize = 0;
        for (note of notes) {
            const cloned = note.clone(bun.default_allocator)?;
            array.putIndex(
                globalThis,
                @intCast(i),
                BuildMessage.create(globalThis, bun.default_allocator, logger.Msg{ data: cloned, kind: .note })?,
            )?;
            i += 1;
        }

        return array;
    }

    pub fn toStringFn(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        const text = std.fmt.allocPrint(default_allocator, "BuildMessage: {s}", .{ _.msg.data.text }) catch {
            return globalThis.throwOutOfMemoryValue();
        };
        let str = ZigString.init(text);
        str.setOutputEncoding();
        if (str.isUTF8()) {
            const out = str.toJS(globalThis);
            default_allocator.free(text);
            return out;
        }

        return str.toExternalValue(globalThis);
    }

    pub static fn create(
        globalThis: *jsc.JSGlobalObject,
        allocator: std.mem.Allocator,
        msg: logger.Msg,
        // resolve_result: *const Resolver.Result,
    ): bun.OOM!jsc.JSValue {
        let build_error = allocator.create(BuildMessage)?;
        build_error.* = BuildMessage{
            msg: msg.clone(allocator)?,
            // .resolve_result = resolve_result.*,
            allocator: allocator,
        };

        return build_error.toJS(globalThis);
    }

    pub fn toString(
        *_,
        globalThis: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!jsc.JSValue {
        return _.toStringFn(globalThis);
    }

    pub fn toPrimitive(
        *_,
        globalThis: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!jsc.JSValue {
        const args_ = callframe.arguments_old(1);
        unsafe {
            const args = args_.ptr[0..args_.len];
            if (args.len > 0) {
                if (!args[0].isString()) {
                    return jsc.JSValue.jsNull();
                }

                const str = args[0].getZigString(globalThis)?;
                if (str.eqlComptime("default") || str.eqlComptime("string")) {
                    return _.toStringFn(globalThis);
                }
            }
        }

        return jsc.JSValue.jsNull();
    }

    pub fn toJSON(
        *_,
        globalThis: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!jsc.JSValue {
        let object = jsc.JSValue.createEmptyObject(globalThis, 4);
        object.put(globalThis, ZigString.static("name"), (bun.String.static("BuildMessage").toJS(globalThis))?);
        object.put(globalThis, ZigString.static("position"), _.getPosition(globalThis));
        object.put(globalThis, ZigString.static("message"), _.getMessage(globalThis));
        object.put(globalThis, ZigString.static("level"), _.getLevel(globalThis));
        return object;
    }

    pub static fn generatePositionObject(msg: logger.Msg, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        let location = msg.data.location;
        if (location == null) return jsc.JSValue.jsNull();
        const loc = location.?;

        let object = jsc.JSValue.createEmptyObject(globalThis, 7);

        object.put(
            globalThis,
            ZigString.static("lineText"),
            ZigString.init(loc.line_text ?? "").toJS(globalThis),
        );
        object.put(
            globalThis,
            ZigString.static("file"),
            ZigString.init(loc.file).toJS(globalThis),
        );
        object.put(
            globalThis,
            ZigString.static("namespace"),
            ZigString.init(loc.namespace).toJS(globalThis),
        );
        object.put(
            globalThis,
            ZigString.static("line"),
            JSValue.jsNumber(loc.line),
        );
        object.put(
            globalThis,
            ZigString.static("column"),
            JSValue.jsNumber(loc.column),
        );
        object.put(
            globalThis,
            ZigString.static("length"),
            JSValue.jsNumber(loc.length),
        );
        object.put(
            globalThis,
            ZigString.static("offset"),
            JSValue.jsNumber(loc.offset),
        );

        return object;
    }

    // https://github.com/oven-sh/bun/issues/2375#issuecomment-2121530202
    pub fn getColumn(*_, _: *jsc.JSGlobalObject): jsc.JSValue {
        if (location := _.msg.data.location) {
            return jsc.JSValue.jsNumber(@max(location.column - 1, 0));
        }

        return jsc.JSValue.jsNumber(@as(i32, 0));
    }

    pub fn getLine(*_, _: *jsc.JSGlobalObject): jsc.JSValue {
        if (location := _.msg.data.location) {
            return jsc.JSValue.jsNumber(@max(location.line - 1, 0));
        }

        return jsc.JSValue.jsNumber(@as(i32, 0));
    }

    pub fn getPosition(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        return BuildMessage.generatePositionObject(_.msg, globalThis);
    }

    pub fn getMessage(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        return ZigString.init(_.msg.data.text).toJS(globalThis);
    }

    pub fn getLevel(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        return ZigString.init(_.msg.kind.string()).toJS(globalThis);
    }

    pub fn finalize(*_) {
        _.msg.deinit(_.allocator);
        _.allocator.destroy(_);
    }
}
