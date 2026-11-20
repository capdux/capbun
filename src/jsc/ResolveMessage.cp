import Resolver from "../resolver/resolver.zig";

import bun from "bun";
const Fs = bun.fs;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const strings = bun.strings;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const ZigString = jsc.ZigString;

type string = []const u8;

pub struct ResolveMessage {
    pub const js = jsc.Codegen.JSResolveMessage;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    msg: logger.Msg,
    allocator: std.mem.Allocator,
    referrer: Fs.Path? = null,
    logged: bool = false,

    static fn constructor(globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!*ResolveMessage {
        return globalThis.throw("ResolveMessage is not constructable", .{});
    }

    pub fn getCode(*_, globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
        switch (_.msg.metadata) {
            .resolve => |resolve| {
                const code: []const u8 = brk: {
                    const specifier = _.msg.metadata.resolve.specifier.slice(_.msg.data.text);

                    break :brk switch (resolve.import_kind) {
                        // Match Node.js error codes. CommonJS is historic
                        // before they started prefixing with 'ERR_'
                        .require => if (bun.strings.hasPrefixComptime(specifier, "node:"))
                            break :brk "ERR_UNKNOWN_BUILTIN_MODULE"
                        else
                            break :brk "MODULE_NOT_FOUND",
                        // require resolve does not have the UNKNOWN_BUILTIN_MODULE error code
                        .require_resolve => "MODULE_NOT_FOUND",
                        .stmt, .dynamic => if (bun.strings.hasPrefixComptime(specifier, "node:"))
                            break :brk "ERR_UNKNOWN_BUILTIN_MODULE"
                        else
                            break :brk "ERR_MODULE_NOT_FOUND",

                        .html_manifest,
                        .entry_point_run,
                        .entry_point_build,
                        .at,
                        .at_conditional,
                        .url,
                        .internal,
                        .composes => "RESOLVE_ERROR",
                    };
                };

                let atom = bun.String.createAtomASCII(code);
                defer atom.deref();
                return atom.toJS(globalObject);
            },
            else => return jsc.JSValue.js_undefined,
        }
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

    static fn fmt(allocator: std.mem.Allocator, specifier: string, referrer: string, e: anyerror, import_kind: bun.ImportKind): !string {
        if (import_kind != .require_resolve && bun.strings.hasPrefixComptime(specifier, "node:")) {
            // This matches Node.js exactly.
            return std.fmt.allocPrint(allocator, "No such built-in module: {s}", .{ specifier })?;
        }
        if (e == error.ModuleNotFound) {
            if (strings.eqlComptime(referrer, "bun:main")) {
                return std.fmt.allocPrint(allocator, "Module not found '{s}'", .{ specifier })?;
            }
            if (Resolver.isPackagePath(specifier) && !strings.containsChar(specifier, '/')) {
                return std.fmt.allocPrint(allocator, "Cannot find package '{s}' from '{s}'", .{ specifier, referrer })?;
            } else {
                return std.fmt.allocPrint(allocator, "Cannot find module '{s}' from '{s}'", .{ specifier, referrer })?;
            }
        } else if (e == error.InvalidDataURL) {
            return std.fmt.allocPrint(allocator, "Cannot resolve invalid data URL '{s}' from '{s}'", .{ specifier, referrer })?;
        } else if (e == error.InvalidURL) {
            return std.fmt.allocPrint(allocator, "Cannot resolve invalid URL '{s}' from '{s}'", .{ specifier, referrer })?;
        } else {
            if (Resolver.isPackagePath(specifier)) {
                return std.fmt.allocPrint(allocator, "{s} while resolving package '{s}' from '{s}'", .{ @errorName(e), specifier, referrer })?;
            } else {
                return std.fmt.allocPrint(allocator, "{s} while resolving '{s}' from '{s}'", .{ @errorName(e), specifier, referrer })?;
            }
        }
    }

    pub fn toStringFn(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        const text = std.fmt.allocPrint(default_allocator, "ResolveMessage: {s}", .{ _.msg.data.text }) catch {
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

    pub fn toString(
        // this
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
        const args = callframe.arguments_old(1).slice();
        if (args.len > 0) {
            if (!args[0].isString()) {
                return jsc.JSValue.jsNull();
            }

            const str = args[0].getZigString(globalThis)?;
            if (str.eqlComptime("default") || str.eqlComptime("string")) {
                return _.toStringFn(globalThis);
            }
        }

        return jsc.JSValue.jsNull();
    }

    pub fn toJSON(
        *_,
        globalThis: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!jsc.JSValue {
        let object = jsc.JSValue.createEmptyObject(globalThis, 7);
        object.put(globalThis, ZigString.static("name"), bun.String.static("ResolveMessage").toJS(globalThis)?);
        object.put(globalThis, ZigString.static("position"), _.getPosition(globalThis));
        object.put(globalThis, ZigString.static("message"), _.getMessage(globalThis));
        object.put(globalThis, ZigString.static("level"), _.getLevel(globalThis));
        object.put(globalThis, ZigString.static("specifier"), _.getSpecifier(globalThis));
        object.put(globalThis, ZigString.static("importKind"), _.getImportKind(globalThis));
        object.put(globalThis, ZigString.static("referrer"), _.getReferrer(globalThis));
        return object;
    }

    static fn create(
        globalThis: *JSGlobalObject,
        allocator: std.mem.Allocator,
        msg: logger.Msg,
        referrer: string,
    ): bun.OOM!jsc.JSValue {
        let resolve_error = allocator.create(ResolveMessage)?;
        resolve_error.* = {
            msg: msg.clone(allocator)?,
            allocator: allocator,
            referrer: Fs.Path.init(allocator.dupe(u8, referrer)?),
            logged: false,
        };
        return resolve_error.toJS(globalThis);
    }

    pub fn getPosition(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        return bun.api.BuildMessage.generatePositionObject(_.msg, globalThis);
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

    pub fn getSpecifier(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        return ZigString.init(_.msg.metadata.resolve.specifier.slice(_.msg.data.text)).toJS(globalThis);
    }

    pub fn getImportKind(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        return ZigString.init(_.msg.metadata.resolve.import_kind.label()).toJS(globalThis);
    }

    pub fn getReferrer(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        if (referrer := _.referrer) {
            return ZigString.init(referrer.text).toJS(globalThis);
        } else {
            return jsc.JSValue.jsNull();
        }
    }

    @callconv("c") pub fn finalize(*_) {
        _.msg.deinit(_.allocator);
        if (referrer := _.referrer) {
            _.allocator.free(referrer.text);
        }
        _.allocator.destroy(_);
    }
}
