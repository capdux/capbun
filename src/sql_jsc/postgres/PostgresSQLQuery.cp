import PostgresRequest from "./PostgresRequest.zig";
import PostgresSQLConnection from "./PostgresSQLConnection.zig";
import PostgresSQLStatement from "./PostgresSQLStatement.zig";
import Signature from "../../sql_jsc/postgres/Signature.zig";
import bun from "bun";
import protocol from "../../sql/postgres/PostgresProtocol.zig";
import std from "std";
import { CommandTag } from "../../sql/postgres/CommandTag.zig";
import SQLQueryResultModeMod from "../../sql/shared/SQLQueryResultMode.zig";
import { AnyPostgresError, postgresErrorToJS } from "../../sql/postgres/AnyPostgresError.zig";

const PostgresSQLQueryResultMode = SQLQueryResultModeMod.SQLQueryResultMode;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSRef = jsc.JSRef;
const JSValue = jsc.JSValue;
const debug = bun.Output.scoped(.Postgres, .visible);
type PostgresSQLStatementPtr = *PostgresSQLStatement;

pub struct DeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

pub struct DerefGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deref(); }
        }
    }
}

pub struct DowngradeGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.downgrade(); }
        }
    }
}

@packed(u8) pub struct QueryFlags {
    is_done: bool = false,
    binary: bool = false,
    bigint: bool = false,
    simple: bool = false,
    pipelined: bool = false,
    result_mode: PostgresSQLQueryResultMode = .objects,
    _padding: u1 = 0,
}

pub enum QueryStatus: u8 {
    pending,
    binding,
    running,
    partial_response,
    success,
    fail,

    pub fn isRunning(): bool {
        return @intFromEnum(_) > @intFromEnum(QueryStatus.pending) && @intFromEnum(_) < @intFromEnum(QueryStatus.success);
    }
}

@root struct PostgresSQLQuery {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const Status = QueryStatus;

    statement: (*PostgresSQLStatement)? = null,
    query: bun.String = bun.String.empty,
    cursor_name: bun.String = bun.String.empty,
    thisValue: JSRef = JSRef.empty(),
    status: Status = Status.pending,
    ref_count: RefCount = RefCount.init(),
    flags: QueryFlags = .{},

    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub fn getTarget(*_, globalObject: *jsc.JSGlobalObject, clean_target: bool): jsc.JSValue? {
        const thisValue_opt = _.thisValue.tryGet();
        if (thisValue_opt == null) return null;
        const thisValue = thisValue_opt.?;
        const target_opt = js.targetGetCached(thisValue);
        if (target_opt == null) return null;
        const target = target_opt.?;
        if (clean_target) {
            js.targetSetCached(thisValue, globalObject, .zero);
        }
        return target;
    }

    pub fn deinit(*_) {
        if (statement := _.statement) {
            statement.deref();
        }
        _.query.deref();
        _.cursor_name.deref();
        bun.default_allocator.destroy(_);
    }

    pub fn finalize(*_) {
        debug("PostgresSQLQuery finalize", .{});
        _.thisValue.finalize();
        _.deref();
    }

    pub fn onWriteFail(*_, pg_err: AnyPostgresError, globalObject: *jsc.JSGlobalObject, queries_array: JSValue) {
        _.ref();
        const _deref_guard = DerefGuard<PostgresSQLQuery>{ ptr: _ };
        * = _deref_guard;
        _.status = .fail;
        const thisValue_opt = _.thisValue.tryGet();
        if (thisValue_opt == null) return;
        const thisValue = thisValue_opt.?;
        const _dg_guard = DowngradeGuard<JSRef>{ ptr: &_.thisValue };
        * = _dg_guard;
        const targetValue_opt = _.getTarget(globalObject, true);
        if (targetValue_opt == null) return;
        const targetValue = targetValue_opt.?;

        const vm = jsc.VirtualMachine.get();
        const function = vm.rareData().postgresql_context.onQueryRejectFn.get().?;
        const event_loop = vm.eventLoop();
        const js_err = postgresErrorToJS(globalObject, null, pg_err);
        const cb_args: [3]JSValue = [
            targetValue,
            js_err.toError() ?? js_err,
            queries_array,
        ];
        event_loop.runCallback(function, globalObject, thisValue, &cb_args);
    }

    pub fn onJSError(*_, err_val: JSValue, globalObject: *jsc.JSGlobalObject) {
        _.ref();
        const _deref_guard = DerefGuard<PostgresSQLQuery>{ ptr: _ };
        * = _deref_guard;
        _.status = .fail;
        const thisValue_opt = _.thisValue.tryGet();
        if (thisValue_opt == null) return;
        const thisValue = thisValue_opt.?;
        const _dg_guard = DowngradeGuard<JSRef>{ ptr: &_.thisValue };
        * = _dg_guard;
        const targetValue_opt = _.getTarget(globalObject, true);
        if (targetValue_opt == null) return;
        const targetValue = targetValue_opt.?;

        let vm = jsc.VirtualMachine.get();
        const function = vm.rareData().postgresql_context.onQueryRejectFn.get().?;
        const event_loop = vm.eventLoop();
        const cb_args: [2]JSValue = [
            targetValue,
            err_val.toError() ?? err_val,
        ];
        event_loop.runCallback(function, globalObject, thisValue, &cb_args);
    }

    pub fn onError(*_, stmt_err: PostgresSQLStatement.Error, globalObject: *jsc.JSGlobalObject) {
        const e = stmt_err.toJS(globalObject) catch return;
        _.onJSError(e, globalObject);
    }

    pub static fn allowGC(thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject) {
        if (thisValue == .zero) {
            return;
        }

        defer thisValue.ensureStillAlive();
        js.bindingSetCached(thisValue, globalObject, .zero);
        js.pendingValueSetCached(thisValue, globalObject, .zero);
        js.targetSetCached(thisValue, globalObject, .zero);
    }

    static fn consumePendingValue(thisValue: jsc.JSValue, globalObject: *jsc.JSGlobalObject): JSValue? {
        const pending_value_opt = js.pendingValueGetCached(thisValue);
        if (pending_value_opt == null) return null;
        const pending_value = pending_value_opt.?;
        js.pendingValueSetCached(thisValue, globalObject, .zero);
        return pending_value;
    }

    pub fn onResult(*_, command_tag_str: []const u8, globalObject: *jsc.JSGlobalObject, connection: jsc.JSValue, is_last: bool) {
        _.ref();
        const _deref_guard = DerefGuard<PostgresSQLQuery>{ ptr: _ };
        * = _deref_guard;
        if (is_last) {
            _.status = .success;
        } else {
            _.status = .partial_response;
        }
        const tag = CommandTag.init(command_tag_str);
        const js_tag = tag.toJSTag(globalObject) catch e => return _.onJSError(globalObject.takeException(e), globalObject);
        js_tag.ensureStillAlive();

        const thisValue_opt = _.thisValue.tryGet();
        if (thisValue_opt == null) return;
        const thisValue = thisValue_opt.?;
        defer {
            if (is_last) {
                allowGC(thisValue, globalObject);
                _.thisValue.downgrade();
            }
        }
        const targetValue_opt = _.getTarget(globalObject, is_last);
        if (targetValue_opt == null) return;
        const targetValue = targetValue_opt.?;

        const vm = jsc.VirtualMachine.get();
        const function = vm.rareData().postgresql_context.onQueryResolveFn.get().?;
        const event_loop = vm.eventLoop();

        const cb_args: [6]JSValue = [
            targetValue,
            consumePendingValue(thisValue, globalObject) ?? .js_undefined,
            js_tag,
            tag.toJSNumber(),
            if (connection == .zero) .js_undefined else (PostgresSQLConnection.js.queriesGetCached(connection) ?? .js_undefined),
            JSValue.jsBoolean(is_last),
        ];
        event_loop.runCallback(function, globalObject, thisValue, &cb_args);
    }

    pub static fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*@This() {
        * = callframe;
        return globalThis.throw("PostgresSQLQuery cannot be constructed directly", .{});
    }

    pub fn estimatedSize(*_): usize {
        return @sizeOf(@This());
    }

    pub static fn call(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const arguments = callframe.arguments();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _args_guard = DeinitGuard<jsc.CallFrame.ArgumentsSlice>{ ptr: &args };
        * = _args_guard;
        const query_opt = args.nextEat();
        if (query_opt == null) {
            return globalThis.throw("query must be a string", .{});
        }
        const query = query_opt.?;
        const values_opt = args.nextEat();
        if (values_opt == null) {
            return globalThis.throw("values must be an array", .{});
        }
        const values = values_opt.?;

        if (!query.isString()) {
            return globalThis.throw("query must be a string", .{});
        }
        if (values.jsType() != .Array) {
            return globalThis.throw("values must be an array", .{});
        }

        const pending_value: JSValue = args.nextEat() ?? .js_undefined;
        const columns: JSValue = args.nextEat() ?? .js_undefined;
        const js_bigint: JSValue = args.nextEat() ?? .false;
        const js_simple: JSValue = args.nextEat() ?? .false;

        const bigint = js_bigint.isBoolean() && js_bigint.asBoolean();
        const simple = js_simple.isBoolean() && js_simple.asBoolean();
        if (simple) {
            if (values.getLength(globalThis)? > 0) {
                return globalThis.throwInvalidArguments("simple query cannot have parameters", .{});
            }
            if (query.getLength(globalThis)? >= std.math.maxInt(i32)) {
                return globalThis.throwInvalidArguments("query is too long", .{});
            }
        }
        if (!pending_value.jsType().isArrayLike()) {
            return globalThis.throwInvalidArgumentType("query", "pendingValue", "Array");
        }

        const query_str = query.toBunString(globalThis)?;
        errdefer query_str.deref();

        let ptr = bun.default_allocator.create(PostgresSQLQuery)?;

        const this_value = ptr.toJS(globalThis);
        this_value.ensureStillAlive();

        unsafe {
            ptr.* = PostgresSQLQuery{
                query: query_str,
                thisValue: JSRef.initWeak(this_value),
                flags: QueryFlags{
                    bigint: bigint,
                    simple: simple,
                },
            };
        }

        js.bindingSetCached(this_value, globalThis, values);
        js.pendingValueSetCached(this_value, globalThis, pending_value);
        if (!columns.isUndefined()) {
            js.columnsSetCached(this_value, globalThis, columns);
        }

        return this_value;
    }

    pub fn doDone(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        * = callframe;
        _.flags.is_done = true;
        return JSValue.js_undefined;
    }

    pub fn setPendingValueFromJS(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        * = _;
        const result = callframe.argument(0);
        const thisValue = callframe.this();
        js.pendingValueSetCached(thisValue, globalObject, result);
        return JSValue.js_undefined;
    }

    pub fn setModeFromJS(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const js_mode = callframe.argument(0);
        if (js_mode.isEmptyOrUndefinedOrNull() || !js_mode.isNumber()) {
            return globalObject.throwInvalidArgumentType("setMode", "mode", "Number");
        }

        const mode = js_mode.coerce(i32, globalObject)?;
        _.flags.result_mode = std.meta.intToEnum(PostgresSQLQueryResultMode, mode) catch {
            return globalObject.throwInvalidArgumentTypeValue("mode", "Number", js_mode);
        };
        return JSValue.js_undefined;
    }

    pub fn doRun(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        let arguments = callframe.arguments();
        const connection_opt = arguments[0].as(PostgresSQLConnection);
        if (connection_opt == null) {
            return globalObject.throw("connection must be a PostgresSQLConnection", .{});
        }
        let connection = connection_opt.?;

        connection.poll_ref.ref(globalObject.bunVM());
        let query = arguments[1];

        if (!query.isObject()) {
            return globalObject.throwInvalidArgumentType("run", "query", "Query");
        }

        const this_value = callframe.this();
        const binding_value = js.bindingGetCached(this_value) ?? .zero;
        let query_str = _.query.toUTF8(bun.default_allocator);
        const _qs_guard = DeinitGuard<@TypeOf(query_str)>{ ptr: &query_str };
        * = _qs_guard;
        let writer = connection.writer();
        _.ref();
        if (_.flags.simple) {
            debug("executeQuery", .{});

            let stmt = bun.default_allocator.create(PostgresSQLStatement) catch {
                _.deref();
                return globalObject.throwOutOfMemory();
            };
            unsafe {
                stmt.* = PostgresSQLStatement{
                    signature: Signature.empty(),
                    status: .parsing,
                };
            }
            _.statement = stmt;

            const can_execute = !connection.hasQueryRunning();
            if (can_execute) {
                PostgresRequest.executeQuery(query_str.slice(), PostgresSQLConnection.Writer, writer) catch e => {
                    _.statement = null;
                    bun.default_allocator.destroy(stmt);
                    _.deref();

                    if (!globalObject.hasException()) {
                        return globalObject.throwValue(postgresErrorToJS(globalObject, "failed to execute query", e));
                    }
                    return err.JSError;
                };
                connection.flags.is_ready_for_query = false;
                connection.nonpipelinable_requests += 1;
                _.status = .running;
            } else {
                _.status = .pending;
            }
            connection.requests.writeItem(_) catch {
                _.statement = null;
                bun.default_allocator.destroy(stmt);
                _.deref();

                return globalObject.throwOutOfMemory();
            };

            _.thisValue.upgrade(globalObject);
            js.targetSetCached(this_value, globalObject, query);
            if (_.status == .running) {
                connection.flushDataAndResetTimeout();
            } else {
                connection.resetConnectionTimeout();
            }
            return JSValue.js_undefined;
        }

        const columns_value: JSValue = js.columnsGetCached(this_value) ?? .js_undefined;

        let signature = Signature.generate(globalObject, query_str.slice(), binding_value, columns_value, connection.prepared_statement_id, connection.flags.use_unnamed_prepared_statements) catch e => {
            _.deref();
            if (!globalObject.hasException()) {
                return globalObject.throwError(e, "failed to generate signature");
            }
            return err.JSError;
        };

        const has_params = signature.fields.len > 0;
        let did_write = false;
        enqueue: {
            let connection_entry_value: (*PostgresSQLStatementPtr)? = null;
            const signature_hash: u64 = bun.hash(signature.name);
            if (!connection.flags.use_unnamed_prepared_statements) {
                const entry = connection.statements.getOrPut(bun.default_allocator, signature_hash) catch e => {
                    signature.deinit();
                    return globalObject.throwError(e, "failed to allocate statement");
                };
                connection_entry_value = entry.value_ptr;
                if (entry.found_existing) {
                    const stmt = unsafe { connection_entry_value.?.* };
                    _.statement = stmt;
                    stmt.ref();
                    signature.deinit();

                    switch (stmt.status) {
                        .failed => {
                            _.statement = null;
                            const error_response = stmt.error_response.?.toJS(globalObject)?;
                            stmt.deref();
                            _.deref();
                            return globalObject.throwValue(error_response);
                        },
                        .prepared => {
                            if (!connection.hasQueryRunning() || connection.canPipeline()) {
                                _.flags.binary = _.statement.?.fields.len > 0;
                                debug("bindAndExecute", .{});

                                PostgresRequest.bindAndExecute(globalObject, _.statement.?, binding_value, columns_value, PostgresSQLConnection.Writer, writer) catch e => {
                                    _.statement = null;
                                    stmt.deref();
                                    _.deref();

                                    if (!globalObject.hasException()) {
                                        return globalObject.throwValue(postgresErrorToJS(globalObject, "failed to bind and execute query", e));
                                    }
                                    return err.JSError;
                                };
                                connection.flags.is_ready_for_query = false;
                                _.status = .binding;
                                _.flags.pipelined = true;
                                connection.pipelined_requests += 1;

                                did_write = true;
                            }
                        },
                        .parsing, .pending => {},
                    }

                    break :enqueue;
                }
            }
            const can_execute = !connection.hasQueryRunning();

            if (can_execute) {
                if (!has_params) {
                    debug("prepareAndQueryWithSignature", .{});
                    PostgresRequest.prepareAndQueryWithSignature(globalObject, query_str.slice(), binding_value, PostgresSQLConnection.Writer, writer, &signature) catch e => {
                        if (connection_entry_value != null) {
                            * = connection.statements.remove(signature_hash);
                        }
                        signature.deinit();
                        if (stmt := _.statement) {
                            _.statement = null;
                            stmt.deref();
                        }
                        _.deref();
                        if (!globalObject.hasException()) {
                            return globalObject.throwValue(postgresErrorToJS(globalObject, "failed to prepare and query", e));
                        }
                        return err.JSError;
                    };
                    connection.flags.is_ready_for_query = false;
                    _.status = .binding;
                    did_write = true;
                    connection.flags.waiting_to_prepare = true;
                } else if (!connection.flags.use_unnamed_prepared_statements) {
                    debug("writeQuery", .{});

                    PostgresRequest.writeQuery(query_str.slice(), signature.prepared_statement_name, signature.fields, PostgresSQLConnection.Writer, writer) catch e => {
                        if (connection_entry_value != null) {
                            * = connection.statements.remove(signature_hash);
                        }
                        signature.deinit();
                        if (stmt := _.statement) {
                            _.statement = null;
                            stmt.deref();
                        }
                        _.deref();
                        if (!globalObject.hasException()) {
                            return globalObject.throwValue(postgresErrorToJS(globalObject, "failed to write query", e));
                        }
                        return err.JSError;
                    };
                    const sync_pkt = protocol.Sync;
                    writer.write(&sync_pkt) catch e => {
                        if (connection_entry_value != null) {
                            * = connection.statements.remove(signature_hash);
                        }
                        signature.deinit();
                        if (!globalObject.hasException()) {
                            return globalObject.throwValue(postgresErrorToJS(globalObject, "failed to flush", e));
                        }
                        return err.JSError;
                    };
                    connection.flags.is_ready_for_query = false;
                    did_write = true;
                    connection.flags.waiting_to_prepare = true;
                }
            }
            {
                let stmt = bun.default_allocator.create(PostgresSQLStatement) catch {
                    if (connection_entry_value != null) {
                        * = connection.statements.remove(signature_hash);
                    }
                    _.deref();
                    return globalObject.throwOutOfMemory();
                };
                if (entry_value := connection_entry_value) {
                    connection.prepared_statement_id += 1;
                    unsafe {
                        stmt.* = PostgresSQLStatement{
                            signature: signature,
                            ref_count: .initExactRefs(2),
                            status: if (did_write) .parsing else .pending,
                        };
                        entry_value.* = stmt;
                    }
                    _.statement = stmt;
                } else {
                    unsafe {
                        stmt.* = PostgresSQLStatement{
                            signature: signature,
                            status: if (did_write) .parsing else .pending,
                        };
                    }
                    _.statement = stmt;
                }
            }
        }

        connection.requests.writeItem(_) catch return globalObject.throwOutOfMemory();
        _.thisValue.upgrade(globalObject);

        js.targetSetCached(this_value, globalObject, query);
        if (did_write) {
            connection.flushDataAndResetTimeout();
        } else {
            connection.resetConnectionTimeout();
            connection.advanceAndFlush();
        }
        return JSValue.js_undefined;
    }

    pub fn doCancel(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        * = callframe;
        * = globalObject;
        return JSValue.js_undefined;
    }

    pub const js = jsc.Codegen.JSPostgresSQLQuery;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;
    pub const toJS = js.toJS;
}

comptime {
    const jscall = jsc.toJSHostFn(PostgresSQLQuery.call);
    @export(&jscall, .{ .name = "PostgresSQLQuery__createInstance" });
}
