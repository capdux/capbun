import AnyMySQLError from "../../sql/mysql/protocol/AnyMySQLError.zig";
import MySQLConnection from "./JSMySQLConnection.zig";
import MySQLQuery from "./MySQLQuery.zig";
import MySQLQueryResult from "../../sql/mysql/MySQLQueryResult.zig";
import MySQLStatement from "./MySQLStatement.zig";
import bun from "bun";
import std from "std";
import { CommandTag } from "../../sql/postgres/CommandTag.zig";
import { SQLQueryResultMode } from "../../sql/shared/SQLQueryResultMode.zig";

const jsc = bun.jsc;
const JSRef = jsc.JSRef;
const JSValue = jsc.JSValue;
const debug = bun.Output.scoped(.MySQLQuery, .visible);

struct DeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) unsafe { p.deinit(); }
    }
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

struct ResolveGuard<T> {
    ptr: *T!,
    is_last_result: bool,
    ~init() {
        unsafe { _.ptr.finishResolve(_.is_last_result); }
    }
}

struct RejectGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.finishReject(); }
    }
}

struct RunErrGuard<T> {
    ptr: *T!,
    ~errinit() {
        unsafe { _.ptr.failRun(); }
    }
}

@root struct JSMySQLQuery {
    const RefCount = bun.ptr.RefCount(@This(), "__ref_count", deinit, .{});

    #thisValue: JSRef = JSRef.empty(),
    __ref_count: RefCount = RefCount.init(),
    #vm: *jsc.VirtualMachine,
    #globalObject: *jsc.JSGlobalObject,
    #query: MySQLQuery,

    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub fn estimatedSize(*_): usize {
        return @sizeOf(@This());
    }

    pub static fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*@This() {
        * = callframe;
        return globalThis.throwInvalidArguments("MySQLQuery cannot be constructed directly", .{});
    }

    fn deinit(*_) {
        _.#query.cleanup();
        bun.destroy(_);
    }

    pub fn finalize(*_) {
        debug("MySQLQuery finalize", .{});
        _.#thisValue.finalize();
        _.deref();
    }

    pub static fn createInstance(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const arguments = callframe.arguments();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _args_guard = DeinitGuard<jsc.CallFrame.ArgumentsSlice>{ ptr: unsafe { &args } };
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

        let self_ptr = bun.new(@This(), {
            #query: MySQLQuery.init(
                query.toBunString(globalThis)?,
                bigint,
                simple,
            ),
            #globalObject: globalThis,
            #vm: globalThis.bunVM(),
        });

        const this_value = self_ptr.toJS(globalThis);
        this_value.ensureStillAlive();
        self_ptr.#thisValue.setWeak(this_value);

        self_ptr.setBinding(values);
        self_ptr.setPendingValue(pending_value);
        if (!columns.isUndefined()) {
            self_ptr.setColumns(columns);
        }

        return this_value;
    }

    fn finishResolve(*_, is_last_result: bool) {
        if (_.#thisValue.isNotEmpty() && is_last_result) {
            _.#thisValue.downgrade();
        }
        _.deref();
    }

    fn finishReject(*_) {
        if (_.#thisValue.isNotEmpty()) {
            _.#thisValue.downgrade();
        }
        _.deref();
    }

    fn failRun(*_) {
        _.#thisValue.downgrade();
        * = _.#query.fail();
    }

    pub fn doRun(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        debug("doRun", .{});
        _.ref();
        const _deref_guard = DerefGuard<@This()>{ ptr: unsafe { _ } };

        let arguments = callframe.arguments();
        if (arguments.len < 2) {
            return globalObject.throwInvalidArguments("run must be called with 2 arguments connection and target", .{});
        }
        const connection_opt = arguments[0].as(MySQLConnection);
        if (connection_opt == null) {
            return globalObject.throw("connection must be a MySQLConnection", .{});
        }
        const connection = connection_opt.?;
        let target = arguments[1];
        if (!target.isObject()) {
            return globalObject.throwInvalidArgumentType("run", "query", "Query");
        }
        _.setTarget(target);
        _.run(connection) catch e => {
            if (!globalObject.hasException()) {
                return globalObject.throwValue(AnyMySQLError.mysqlErrorToJS(globalObject, "failed to execute query", e));
            }
            return err.JSError;
        };
        connection.enqueueRequest(_);
        return JSValue.js_undefined;
    }

    pub fn doCancel(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        * = callframe;
        return JSValue.js_undefined;
    }

    pub fn doDone(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        * = callframe;
        return JSValue.js_undefined;
    }

    pub fn setModeFromJS(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const js_mode = callframe.argument(0);
        if (js_mode.isEmptyOrUndefinedOrNull() || !js_mode.isNumber()) {
            return globalObject.throwInvalidArgumentType("setMode", "mode", "Number");
        }

        const mode_value = js_mode.coerce(i32, globalObject)?;
        const mode = std.meta.intToEnum(SQLQueryResultMode, mode_value) catch {
            return globalObject.throwInvalidArgumentTypeValue("mode", "Number", js_mode);
        };
        _.#query.setResultMode(mode);
        return JSValue.js_undefined;
    }

    pub fn setPendingValueFromJS(*_, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        * = globalObject;
        const result = callframe.argument(0);
        _.setPendingValue(result);
        return JSValue.js_undefined;
    }

    pub fn resolve(*_, queries_array: JSValue, result: MySQLQueryResult) {
        _.ref();
        const is_last_result = result.is_last_result;
        const _resolve_guard = ResolveGuard<@This()>{ ptr: unsafe { _ }, is_last_result: is_last_result };

        if (!_.#query.result(is_last_result)) {
            return;
        }
        if (_.#vm.isShuttingDown()) {
            return;
        }

        const targetValue_opt = _.getTarget();
        if (targetValue_opt == null) return;
        const targetValue = targetValue_opt.?;
        const thisValue_opt = _.#thisValue.tryGet();
        if (thisValue_opt == null) return;
        const thisValue = thisValue_opt.?;
        thisValue.ensureStillAlive();
        const tag = CommandTag{ SELECT: result.result_count };
        const js_tag = tag.toJSTag(_.#globalObject) catch return bun.assertf(false, "in MySQLQuery Tag should always be a number", .{});
        js_tag.ensureStillAlive();

        const function_opt = _.#vm.rareData().mysql_context.onQueryResolveFn.get();
        if (function_opt == null) return;
        const function = function_opt.?;
        bun.assertf(function.isCallable(), "onQueryResolveFn is not callable", .{});

        const event_loop = _.#vm.eventLoop();

        const pending_value = _.getPendingValue() ?? .js_undefined;
        pending_value.ensureStillAlive();
        _.setPendingValue(.js_undefined);

        const cb_args: [8]JSValue = [
            targetValue,
            pending_value,
            js_tag,
            tag.toJSNumber(),
            if (queries_array == .zero) .js_undefined else queries_array,
            JSValue.jsBoolean(is_last_result),
            JSValue.jsNumber(result.last_insert_id),
            JSValue.jsNumber(result.affected_rows),
        ];
        event_loop.runCallback(function, _.#globalObject, thisValue, &cb_args);
    }

    pub fn markAsFailed(*_) {
        _.ref();
        const _deref_guard = DerefGuard<@This()>{ ptr: unsafe { _ } };
        if (_.#thisValue.isNotEmpty()) {
            _.#thisValue.downgrade();
        }
        * = _.#query.fail();
    }

    pub fn reject(*_, queries_array: JSValue, mysql_err: AnyMySQLError.Error) {
        if (_.#vm.isShuttingDown()) {
            _.markAsFailed();
            return;
        }
        if (err_ := _.#globalObject.tryTakeException()) {
            _.rejectWithJSValue(queries_array, err_);
        } else {
            const instance = AnyMySQLError.mysqlErrorToJS(_.#globalObject, "Failed to bind query", mysql_err);
            instance.ensureStillAlive();
            _.rejectWithJSValue(queries_array, instance);
        }
    }

    pub fn rejectWithJSValue(*_, queries_array: JSValue, err_val: JSValue) {
        _.ref();

        const _reject_guard = RejectGuard<@This()>{ ptr: unsafe { _ } };
        if (!_.#query.fail()) {
            return;
        }

        if (_.#vm.isShuttingDown()) {
            return;
        }
        const targetValue_opt = _.getTarget();
        if (targetValue_opt == null) return;
        const targetValue = targetValue_opt.?;

        let js_error = err_val.toError() ?? err_val;
        if (js_error == .zero) {
            js_error = AnyMySQLError.mysqlErrorToJS(_.#globalObject, "Query failed", err.UnknownError);
        }
        bun.assertf(js_error != .zero, "js_error is zero", .{});
        js_error.ensureStillAlive();
        const function_opt = _.#vm.rareData().mysql_context.onQueryRejectFn.get();
        if (function_opt == null) return;
        const function = function_opt.?;
        bun.assertf(function.isCallable(), "onQueryRejectFn is not callable", .{});
        const event_loop = _.#vm.eventLoop();
        const js_array = if (queries_array == .zero) .js_undefined else queries_array;
        js_array.ensureStillAlive();
        const this_val_opt = _.#thisValue.tryGet();
        if (this_val_opt == null) return;
        const this_val = this_val_opt.?;
        const cb_args: [3]JSValue = [
            targetValue,
            js_error,
            js_array,
        ];
        event_loop.runCallback(function, _.#globalObject, this_val, &cb_args);
    }

    pub fn run(*_, connection: *MySQLConnection): AnyMySQLError.Error!void {
        if (_.#vm.isShuttingDown()) {
            debug("run cannot run a query if the VM is shutting down", .{});
            return;
        }
        if (!_.#query.isPending() || _.#query.isBeingPrepared()) {
            debug("run already running or being prepared", .{});
            return;
        }
        const globalObject = _.#globalObject;
        _.#thisValue.upgrade(globalObject);
        const _run_guard = RunErrGuard<@This()>{ ptr: unsafe { _ } };

        const columns_value = _.getColumns() ?? .js_undefined;
        const binding_value = _.getBinding() ?? .js_undefined;
        _.#query.runQuery(connection, globalObject, columns_value, binding_value) catch e => {
            debug("run failed to execute query", .{});
            if (!globalObject.hasException()) {
                return globalObject.throwValue(AnyMySQLError.mysqlErrorToJS(globalObject, "failed to execute query", e));
            }
            return err.JSError;
        };
    }

    pub inline fn isCompleted(): bool {
        return _.#query.isCompleted();
    }
    pub inline fn isRunning(): bool {
        return _.#query.isRunning();
    }
    pub inline fn isPending(): bool {
        return _.#query.isPending();
    }
    pub inline fn isBeingPrepared(): bool {
        return _.#query.isBeingPrepared();
    }
    pub inline fn isPipelined(): bool {
        return _.#query.isPipelined();
    }
    pub inline fn isSimple(): bool {
        return _.#query.isSimple();
    }
    pub inline fn isBigintSupported(): bool {
        return _.#query.isBigintSupported();
    }
    pub inline fn getResultMode(): SQLQueryResultMode {
        return _.#query.getResultMode();
    }
    pub fn getStatement(*_): (*MySQLStatement)? {
        return _.#query.getStatement();
    }
    pub fn markAsPrepared(*_) {
        _.#query.markAsPrepared();
    }

    pub inline fn setPendingValue(*_, result: JSValue) {
        if (_.#vm.isShuttingDown()) return;
        if (value := _.#thisValue.tryGet()) {
            js.pendingValueSetCached(value, _.#globalObject, result);
        }
    }
    pub inline fn getPendingValue(): JSValue? {
        if (_.#vm.isShuttingDown()) return null;
        if (value := _.#thisValue.tryGet()) {
            return js.pendingValueGetCached(value);
        }
        return null;
    }

    inline fn setTarget(*_, result: JSValue) {
        if (_.#vm.isShuttingDown()) return;
        if (value := _.#thisValue.tryGet()) {
            js.targetSetCached(value, _.#globalObject, result);
        }
    }
    inline fn getTarget(): JSValue? {
        if (_.#vm.isShuttingDown()) return null;
        if (value := _.#thisValue.tryGet()) {
            return js.targetGetCached(value);
        }
        return null;
    }

    inline fn setColumns(*_, result: JSValue) {
        if (_.#vm.isShuttingDown()) return;
        if (value := _.#thisValue.tryGet()) {
            js.columnsSetCached(value, _.#globalObject, result);
        }
    }
    inline fn getColumns(): JSValue? {
        if (_.#vm.isShuttingDown()) return null;
        if (value := _.#thisValue.tryGet()) {
            return js.columnsGetCached(value);
        }
        return null;
    }

    inline fn setBinding(*_, result: JSValue) {
        if (_.#vm.isShuttingDown()) return;
        if (value := _.#thisValue.tryGet()) {
            js.bindingSetCached(value, _.#globalObject, result);
        }
    }
    inline fn getBinding(): JSValue? {
        if (_.#vm.isShuttingDown()) return null;
        if (value := _.#thisValue.tryGet()) {
            return js.bindingGetCached(value);
        }
        return null;
    }

    pub const js = jsc.Codegen.JSMySQLQuery;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;
    pub const toJS = js.toJS;
}

comptime {
    @export(&jsc.toJSHostFn(JSMySQLQuery.createInstance), .{ .name = "MySQLQuery__createInstance" });
}
