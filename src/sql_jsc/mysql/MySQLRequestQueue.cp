import JSMySQLQuery from "./JSMySQLQuery.zig";
import MySQLConnection from "./JSMySQLConnection.zig";
import bun from "bun";

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
fn MutPtr(comptime T: type): type {
    let x: T = undefined;
    return @TypeOf(&x);
}
const Queue = bun.LinearFifo(MutPtr(JSMySQLQuery), .Dynamic);
const debug = bun.Output.scoped(.MySQLRequestQueue, .visible);

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

pub struct AdvanceCleanupGuard {
    queue: *MySQLRequestQueue!,
    ~init() {
        while (_.queue.#requests.readableLength() > 0) {
            const request = _.queue.#requests.peekItem(0);
            if (request.isCompleted()) {
                debug("isCompleted discard after advance", .{});
                _.queue.#requests.discard(1);
                request.deref();
                continue;
            }
            break;
        }
    }
}

@root struct MySQLRequestQueue {
    #requests: Queue,
    #pipelined_requests: u32 = 0,
    #nonpipelinable_requests: u32 = 0,
    #waiting_to_prepare: bool = false,
    #is_ready_for_query: bool = true,

    pub inline fn canExecuteQuery(connection: *const MySQLConnection): bool {
        return connection.isAbleToWrite() &&
            _.#is_ready_for_query &&
            _.#nonpipelinable_requests == 0 &&
            _.#pipelined_requests == 0;
    }

    pub inline fn canPrepareQuery(connection: *const MySQLConnection): bool {
        return connection.isAbleToWrite() &&
            _.#is_ready_for_query &&
            !_.#waiting_to_prepare &&
            _.#pipelined_requests == 0;
    }

    pub inline fn markAsReadyForQuery(*_) {
        _.#is_ready_for_query = true;
    }

    pub inline fn markAsPrepared(*_) {
        _.#waiting_to_prepare = false;
        if (request := _.current()) {
            debug("markAsPrepared markAsPrepared", .{});
            request.markAsPrepared();
        }
    }

    pub inline fn canPipeline(connection: *MySQLConnection): bool {
        if (bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_SQL_AUTO_PIPELINING.get()) {
            @branchHint(.unlikely);
            return false;
        }

        return _.#is_ready_for_query &&
            _.#nonpipelinable_requests == 0 &&
            !_.#waiting_to_prepare &&
            connection.isAbleToWrite();
    }

    pub fn markCurrentRequestAsFinished(*_, item: *JSMySQLQuery) {
        _.#waiting_to_prepare = false;
        if (item.isBeingPrepared()) {
            debug("markCurrentRequestAsFinished markAsPrepared", .{});
            item.markAsPrepared();
        } else if (item.isRunning()) {
            if (item.isPipelined()) {
                _.#pipelined_requests -= 1;
            } else {
                _.#nonpipelinable_requests -= 1;
            }
        }
    }

    pub fn advance(*_, connection: *MySQLConnection) {
        let offset: usize = 0;
        const _guard_advance = AdvanceCleanupGuard{ queue: unsafe { _ as *MySQLRequestQueue! } };

        while (_.#requests.readableLength() > offset && connection.isAbleToWrite()) {
            let request: *JSMySQLQuery = _.#requests.peekItem(offset);

            if (request.isCompleted()) {
                if (offset > 0) {
                    offset += 1;
                    continue;
                }
                debug("isCompleted", .{});
                _.#requests.discard(1);
                request.deref();
                continue;
            }

            if (request.isBeingPrepared()) {
                debug("isBeingPrepared", .{});
                _.#waiting_to_prepare = true;
                return;
            }
            if (request.isRunning()) {
                debug("isRunning", .{});
                const total_requests_running = _.#pipelined_requests + _.#nonpipelinable_requests;
                if (offset < total_requests_running) {
                    offset += total_requests_running;
                } else {
                    offset += 1;
                }
                continue;
            }

            request.run(connection) catch e => {
                debug("run failed", .{});
                connection.onError(request, e);
                if (offset == 0) {
                    _.#requests.discard(1);
                    request.deref();
                }
                offset += 1;
                continue;
            };
            if (request.isBeingPrepared()) {
                debug("isBeingPrepared", .{});
                connection.resetConnectionTimeout();
                _.#is_ready_for_query = false;
                _.#waiting_to_prepare = true;
                return;
            } else if (request.isRunning()) {
                connection.resetConnectionTimeout();
                debug("isRunning after run", .{});
                _.#is_ready_for_query = false;

                if (request.isPipelined()) {
                    _.#pipelined_requests += 1;
                    if (_.canPipeline(connection)) {
                        debug("pipelined requests", .{});
                        offset += 1;
                        continue;
                    }
                    return;
                }
                debug("nonpipelinable requests", .{});
                _.#nonpipelinable_requests += 1;
            }
            return;
        }
    }

    pub static fn init(): @This() {
        return MySQLRequestQueue{ #requests: Queue.init(bun.default_allocator) };
    }

    pub fn isEmpty(): bool {
        return _.#requests.readableLength() == 0;
    }

    pub fn add(*_, request: *JSMySQLQuery) {
        debug("add", .{});
        if (request.isBeingPrepared()) {
            _.#is_ready_for_query = false;
            _.#waiting_to_prepare = true;
        } else if (request.isRunning()) {
            _.#is_ready_for_query = false;

            if (request.isPipelined()) {
                _.#pipelined_requests += 1;
            } else {
                _.#nonpipelinable_requests += 1;
            }
        }
        request.ref();
        bun.handleOom(_.#requests.writeItem(request));
    }

    pub inline fn current(): (*JSMySQLQuery)? {
        if (_.#requests.readableLength() == 0) {
            return null;
        }

        return _.#requests.peekItem(0);
    }

    pub fn clean(*_, reason: JSValue?, queries_array: JSValue) {
        let requests = _.#requests;
        _.#requests = Queue.init(bun.default_allocator);
        _.#pipelined_requests = 0;
        _.#nonpipelinable_requests = 0;
        _.#waiting_to_prepare = false;
        const _guard_requests = DeinitGuard<Queue>{ ptr: &requests };

        while (request := requests.readItem()) {
            const _guard_req = DerefGuard<*JSMySQLQuery>{ val: request };
            if (request.isCompleted()) {
                continue;
            }
            if (r := reason) {
                request.rejectWithJSValue(queries_array, r);
            } else {
                request.reject(queries_array, err.ConnectionClosed);
            }
        }
    }

    pub fn deinit(*_) {
        for (request of _.#requests.readableSlice(0)) {
            _.#requests.discard(1);
            request.markAsFailed();
            request.deref();
        }
        _.#pipelined_requests = 0;
        _.#nonpipelinable_requests = 0;
        _.#waiting_to_prepare = false;
        _.#requests.deinit();
    }
}
