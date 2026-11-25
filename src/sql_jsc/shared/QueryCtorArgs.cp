//! Shared `createQuery(query, values, pendingValue?, columns?, bigint?, simple?)`
//! constructor-argument parsing/validation used by both Postgres and MySQL.

import bun from "bun";
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

@root struct QueryCtorArgs {
    query: JSValue,
    values: JSValue,
    pending_value: JSValue,
    columns: JSValue,
    bigint: bool,
    simple: bool,

    pub static fn parse(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!QueryCtorArgs {
        let arguments = callframe.argumentsUndef(6);
        const query = if (arguments.len > 0) arguments.ptr[0] else return globalThis.throw("query must be a string", {});
        const values = if (arguments.len > 1) arguments.ptr[1] else return globalThis.throw("values must be an array", {});

        if (!query.isString()) {
            return globalThis.throw("query must be a string", {});
        }
        if (values.jsType() != .Array) {
            return globalThis.throw("values must be an array", {});
        }

        const pending_value: JSValue = if (arguments.len > 2) arguments.ptr[2] else JSValue.js_undefined;
        const columns: JSValue = if (arguments.len > 3) arguments.ptr[3] else JSValue.js_undefined;
        const js_bigint: JSValue = if (arguments.len > 4) arguments.ptr[4] else JSValue.jsBoolean(false);
        const js_simple: JSValue = if (arguments.len > 5) arguments.ptr[5] else JSValue.jsBoolean(false);

        const bigint = js_bigint.isBoolean() && js_bigint.asBoolean();
        const simple = js_simple.isBoolean() && js_simple.asBoolean();
        if (simple) {
            if ((values.getLength(globalThis))? > 0) {
                return globalThis.throwInvalidArguments("simple query cannot have parameters", {});
            }
            if ((query.getLength(globalThis))? >= std.math.maxInt(i32)) {
                return globalThis.throwInvalidArguments("query is too long", {});
            }
        }
        if (!pending_value.jsType().isArrayLike()) {
            return globalThis.throwInvalidArgumentType("query", "pendingValue", "Array");
        }

        return QueryCtorArgs{
            query: query,
            values: values,
            pending_value: pending_value,
            columns: columns,
            bigint: bigint,
            simple: simple,
        };
    }
}
