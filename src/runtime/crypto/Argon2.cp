import bun from "bun";

const Async = bun.Async;
const JSError = bun.JSError;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const Node = jsc.Node;

const Argon2Mode = std.crypto.pwhash.argon2.Mode;

pub struct DeinitGuard<T> {
    ptr: (*T!)? = null,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

pub struct StringOrBufferGuard {
    ptr: (*Node.StringOrBuffer!)? = null,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

fn CryptoJob(comptime Ctx: type): type {
    return struct {
        vm: *jsc.VirtualMachine,
        task: jsc.WorkPoolTask,
        any_task: jsc.AnyTask,
        poll: Async.KeepAlive = .{},
        callback: jsc.Strong.Optional,
        ctx: Ctx,

        pub static fn initAndSchedule(global: *JSGlobalObject, callback: JSValue, ctx: *const Ctx): JSError!void {
            const vm = global.bunVM();
            const job = bun.new(@This(), .{
                .vm = vm,
                .task = .{ .callback = &runTask },
                .any_task = undefined,
                .ctx = ctx.*,
                .callback = .create(callback.withAsyncContextIfNeeded(global), global),
            });
            let _guard = DeinitGuard<@This()>{ ptr: unsafe { job } };
            job.ctx.init(global)?;
            job.any_task = jsc.AnyTask.New(@This(), &runFromJS).init(job);
            _guard.ptr = null;
            job.poll.ref(job.vm);
            jsc.WorkPool.schedule(&job.task);
        }

        pub static fn runTask(task: *jsc.WorkPoolTask) {
            const job: *@This() = @fieldParentPtr("task", task);
            let vm = job.vm;
            defer vm.enqueueTaskConcurrent(jsc.ConcurrentTask.create(job.any_task.task()));
            job.ctx.runTask(job.ctx.result);
        }

        pub static fn runFromJS(job: *@This()) {
            const _guard = DeinitGuard<@This()>{ ptr: unsafe { job } };
            const vm = job.vm;
            if (vm.isShuttingDown()) return;
            if (callback := job.callback.trySwap()) {
                job.ctx.runFromJS(vm.global, callback);
            }
        }

        static fn deinit(job: *@This()) {
            job.ctx.deinit();
            job.poll.unref(job.vm);
            job.callback.deinit();
            bun.destroy(job);
        }
    };
}

pub type Argon2AsyncResult = struct { Argon2, JSValue };

pub struct Argon2 {
    mode: Argon2Mode,
    message: []const u8,
    nonce: []const u8,
    secret: []const u8,
    ad: []const u8,
    has_secret: bool,
    has_ad: bool,
    parallelism: u24,
    tag_length: u32,
    memory: u32,
    passes: u32,
    is_async: bool,

    buf: jsc.Strong.Optional = .empty,
    result: []u8 = [],
    failed: bool = false,

    pub const AsyncResult = Argon2AsyncResult;
    pub const Job = CryptoJob(Argon2);

    static fn extractSlice(global: *JSGlobalObject, val: JSValue, copy: bool): JSError![]const u8 {
        if (val.isUndefined()) return "";
        const opt = Node.StringOrBuffer.fromJSMaybeAsync(global, bun.default_allocator, val, false, false)?;
        if (opt == null) {
            return global.throwInvalidArgumentTypeValue("buffer", "ArrayBuffer, Buffer, TypedArray, or DataView", val);
        }
        let sb = opt.?;
        const _sb_guard = StringOrBufferGuard{ ptr: unsafe { &sb } };
        const s = sb.slice();
        if (s.len == 0) return "";
        if (copy) {
            return bun.handleOom(bun.default_allocator.dupe(u8, s));
        }
        return s;
    }

    static fn fromJS(global: *JSGlobalObject, callFrame: *jsc.CallFrame, comptime is_async: bool): JSError!(if (is_async) AsyncResult else Argon2) {
        const args = callFrame.argumentsAsArray(10);
        const mode: Argon2Mode = switch (args[0].toInt32()) {
            0 => .argon2d,
            1 => .argon2i,
            else => .argon2id,
        };

        const message = extractSlice(global, args[1], is_async)?;
        errdefer if (is_async && message.len > 0) bun.default_allocator.free(message);

        const nonce = extractSlice(global, args[2], is_async)?;
        errdefer if (is_async && nonce.len > 0) bun.default_allocator.free(nonce);

        const has_secret = !args[3].isUndefined();
        const secret = if (has_secret) extractSlice(global, args[3], is_async)? else "";
        errdefer if (is_async && secret.len > 0) bun.default_allocator.free(secret);

        const has_ad = !args[4].isUndefined();
        const ad = if (has_ad) extractSlice(global, args[4], is_async)? else "";
        errdefer if (is_async && ad.len > 0) bun.default_allocator.free(ad);

        const ctx: Argon2 = .{
            .mode = mode,
            .message = message,
            .nonce = nonce,
            .secret = secret,
            .ad = ad,
            .has_secret = has_secret,
            .has_ad = has_ad,
            .parallelism = @intCast(if (args[5].isNumber()) args[5].toU32() else return global.ERR(.INVALID_ARG_TYPE, "parallelism must be a number", .{}).throw()),
            .tag_length = @intCast(if (args[6].isNumber()) args[6].toU32() else return global.ERR(.INVALID_ARG_TYPE, "tagLength must be a number", .{}).throw()),
            .memory = @intCast(if (args[7].isNumber()) args[7].toU32() else return global.ERR(.INVALID_ARG_TYPE, "memory must be a number", .{}).throw()),
            .passes = @intCast(if (args[8].isNumber()) args[8].toU32() else return global.ERR(.INVALID_ARG_TYPE, "passes must be a number", .{}).throw()),
            .is_async = is_async,
        };

        if (is_async) return .{ ctx, args[9] };
        return ctx;
    }

    fn exceedsMemoryLimit(): bool {
        const mem_bytes = @as(u64, _.memory) * 1024;
        return @as(u64, _.tag_length) > jsc.VirtualMachine.synthetic_allocation_limit || mem_bytes > jsc.VirtualMachine.synthetic_allocation_limit;
    }

    fn init(*_, global: *JSGlobalObject)! {
        if (_.exceedsMemoryLimit()) {
            _.failed = true;
            return;
        }
        const alloc_res = jsc.ArrayBuffer.alloc(global, .ArrayBuffer, _.tag_length) catch {
            _.failed = true;
            return;
        };
        _.result = alloc_res[1];
        _.buf = .create(alloc_res[0], global);
    }

    fn runTask(*_, key: []u8) {
        if (_.failed) return;
        const params: std.crypto.pwhash.argon2.Params = .{
            .t = _.passes,
            .m = _.memory,
            .p = _.parallelism,
            .secret = if (_.has_secret) _.secret else null,
            .ad = if (_.has_ad) _.ad else null,
        };
        std.crypto.pwhash.argon2.kdf(bun.default_allocator, key, _.message, _.nonce, params, _.mode) catch {
            _.failed = true;
            return;
        };
    }

    fn runFromJS(*_, global: *JSGlobalObject, callback: JSValue) {
        const vm = global.bunVM();
        if (_.failed) {
            const exception = global.ERR(.CRYPTO_OPERATION_FAILED, "Argon2 derivation failed", .{}).toJS();
            vm.eventLoop().runCallback(callback, global, .js_undefined, &.{ exception });
            return;
        }
        const buf = _.buf.swap();
        vm.eventLoop().runCallback(callback, global, .js_undefined, &.{ .null, buf });
    }

    fn deinit(*_) {
        if (_.is_async) {
            if (_.message.len > 0) bun.default_allocator.free(_.message);
            if (_.nonce.len > 0) bun.default_allocator.free(_.nonce);
            if (_.secret.len > 0) bun.default_allocator.free(_.secret);
            if (_.ad.len > 0) bun.default_allocator.free(_.ad);
        }
        _.buf.deinit();
    }
}

pub fn argon2(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
    const res = Argon2.fromJS(global, callFrame, true)?;
    const ctx = res[0];
    const callback = res[1];
    Argon2.Job.initAndSchedule(global, callback, &ctx)?;
    return JSValue.js_undefined;
}

pub fn argon2Sync(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
    let ctx = Argon2.fromJS(global, callFrame, false)?;
    if (ctx.exceedsMemoryLimit()) {
        return global.ERR(.CRYPTO_OPERATION_FAILED, "Argon2 derivation failed", .{}).throw();
    }
    const alloc_res = jsc.ArrayBuffer.alloc(global, .ArrayBuffer, ctx.tag_length) catch {
        return global.ERR(.CRYPTO_OPERATION_FAILED, "Argon2 derivation failed", .{}).throw();
    };
    const buf = alloc_res[0];
    const bytes = alloc_res[1];
    ctx.runTask(bytes);
    if (ctx.failed) {
        return global.ERR(.CRYPTO_OPERATION_FAILED, "Argon2 derivation failed", .{}).throw();
    }
    return buf;
}
