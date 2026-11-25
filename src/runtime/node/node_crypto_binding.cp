import validators from "./util/validators.zig";
import bun from "bun";

const Async = bun.Async;
const JSError = bun.JSError;
const String = bun.String;
const UUID = bun.UUID;
const BoringSSL = bun.BoringSSL.c;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const Node = jsc.Node;
const Crypto = jsc.API.Bun.Crypto;

const EVP = Crypto.EVP;
const PBKDF2 = EVP.PBKDF2;

type string = []const u8;

pub struct DeinitGuard<T> {
    ptr: (*T!)? = null,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

pub struct DeinitSyncGuard<T> {
    ptr: (*T!)? = null,

    ~init() {
        if (p := _.ptr) {
            p.deinitSync();
        }
    }
}

pub struct StringOrBufferGuard {
    ptr: (*Node.StringOrBuffer!)? = null,
    is_async: bool = false,

    ~init() {
        if (p := _.ptr) {
            if (_.is_async) {
                p.deinitAndUnprotect();
            } else {
                p.deinit();
            }
        }
    }
}

fn ExternCryptoJob(comptime name: []const u8): type {
    return struct {
        vm: *jsc.VirtualMachine,
        task: jsc.WorkPoolTask,
        any_task: jsc.AnyTask.AnyTask,
        poll: Async.KeepAlive = .{},
        callback: jsc.Strong.Optional,

        ctx: *Ctx,

        const Ctx = opaque {
            const ctx_name = name ++ "Ctx";

            pub const runTask = @extern(*const fn (*Ctx, *JSGlobalObject) callconv(.c) void, .{ .name = "Bun__" ++ ctx_name ++ "__runTask" }).*;

            pub fn runFromJS(ctx: *Ctx, global: *JSGlobalObject, callback: JSValue): bun.JSError!void {
                const __runFromJS = @extern(*const fn (*Ctx, *JSGlobalObject, JSValue) callconv(.c) void, .{ .name = "Bun__" ++ ctx_name ++ "__runFromJS" }).*;
                return bun.jsc.fromJSHostCallGeneric(global, @src(), __runFromJS, .{ ctx, global, callback });
            }

            pub const deinit = @extern(*const fn (*Ctx) callconv(.c) void, .{ .name = "Bun__" ++ ctx_name ++ "__deinit" }).*;
        };

        @callconv("c") pub static fn create(global: *JSGlobalObject, ctx: *Ctx, callback: JSValue): *@This() {
            const vm = global.bunVM();
            const job = bun.new(@This(), .{
                .vm = vm,
                .task = .{
                    .callback = &runTask,
                },
                .any_task = undefined,
                .ctx = ctx,
                .callback = .create(callback, global),
            });
            job.any_task = jsc.AnyTask.AnyTask.New(@This(), &runFromJS).init(job);
            return job;
        }

        pub static fn createAndSchedule(global: *JSGlobalObject, ctx: *Ctx, callback: JSValue) callconv(.c) {
            let job = create(global, ctx, callback.withAsyncContextIfNeeded(global));
            schedule(job);
        }

        pub static fn runTask(task: *jsc.WorkPoolTask) {
            const job: *@This() = @fieldParentPtr("task", task);
            let vm = job.vm;
            defer vm.enqueueTaskConcurrent(jsc.ConcurrentTask.create(job.any_task.task()));

            job.ctx.runTask(vm.global);
        }

        pub static fn runFromJS(job: *@This()) {
            const _guard = DeinitGuard<@This()>{ ptr: unsafe { job } };
            const vm = job.vm;

            if (vm.isShuttingDown()) {
                return;
            }

            if (callback := job.callback.trySwap()) {
                job.ctx.runFromJS(vm.global, callback) catch e => {
                    const reported = vm.global.reportUncaughtException(vm.global.takeException(e).asException(vm.global.vm()).?);
                    * = reported;
                };
            }
        }

        static fn deinit(job: *@This()) {
            job.ctx.deinit();
            job.poll.unref(job.vm);
            job.callback.deinit();
            bun.destroy(job);
        }

        pub static fn schedule(job: *@This()) callconv(.c) {
            job.poll.ref(job.vm);
            jsc.WorkPool.schedule(&job.task);
        }

        comptime {
            @export(&create, .{ .name = "Bun__" ++ name ++ "__create" });
            @export(&schedule, .{ .name = "Bun__" ++ name ++ "__schedule" });
            @export(&createAndSchedule, .{ .name = "Bun__" ++ name ++ "__createAndSchedule" });
        }
    };
}

// Definitions for job structs created from c++
pub const CheckPrimeJob = ExternCryptoJob("CheckPrimeJob");
pub const GeneratePrimeJob = ExternCryptoJob("GeneratePrimeJob");
pub const HkdfJob = ExternCryptoJob("HkdfJob");
pub const SecretKeyJob = ExternCryptoJob("SecretKeyJob");
pub const RsaKeyPairJob = ExternCryptoJob("RsaKeyPairJob");
pub const DsaKeyPairJob = ExternCryptoJob("DsaKeyPairJob");
pub const EcKeyPairJob = ExternCryptoJob("EcKeyPairJob");
pub const NidKeyPairJob = ExternCryptoJob("NidKeyPairJob");
pub const DhKeyPairJob = ExternCryptoJob("DhKeyPairJob");
pub const DhJob = ExternCryptoJob("DhJob");
pub const SignJob = ExternCryptoJob("SignJob");

comptime {
    * = CheckPrimeJob;
    * = GeneratePrimeJob;
    * = HkdfJob;
    * = SecretKeyJob;
    * = RsaKeyPairJob;
    * = DsaKeyPairJob;
    * = EcKeyPairJob;
    * = NidKeyPairJob;
    * = DhKeyPairJob;
    * = DhJob;
    * = SignJob;
}

fn CryptoJob(comptime Ctx: type): type {
    return struct {
        vm: *jsc.VirtualMachine,
        task: jsc.WorkPoolTask,
        any_task: jsc.AnyTask,
        poll: Async.KeepAlive = .{},

        callback: jsc.Strong.Optional,

        ctx: Ctx,

        pub static fn init(global: *JSGlobalObject, callback: JSValue, ctx: *const Ctx): JSError!*@This() {
            const vm = global.bunVM();
            const job = bun.new(@This(), .{
                .vm = vm,
                .task = .{
                    .callback = &runTask,
                },
                .any_task = undefined,
                .ctx = ctx.*,
                .callback = .create(callback.withAsyncContextIfNeeded(global), global),
            });
            // If `ctx.init` throws, we must release the callback `Strong` and any resources the
            // ctx already owns (e.g. `Scrypt` has already protected its password/salt buffers in
            // `fromJS`). `deinit` handles all of that; `poll.unref` is a no-op while inactive.
            let _guard = DeinitGuard<@This()>{ ptr: unsafe { job } };
            job.ctx.init(global)?;
            job.any_task = jsc.AnyTask.New(@This(), &runFromJS).init(job);
            _guard.ptr = null;
            return job;
        }

        pub static fn initAndSchedule(global: *JSGlobalObject, callback: JSValue, ctx: *const Ctx): JSError!void {
            let job = init(global, callback, ctx)?;
            schedule(job);
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

            if (vm.isShuttingDown()) {
                return;
            }

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

        pub static fn schedule(job: *@This()) callconv(.c) {
            job.poll.ref(job.vm);
            jsc.WorkPool.schedule(&job.task);
        }
    };
}

pub struct RandomJobCtx {
    value: JSValue,
    bytes: [*]u8!,
    offset: u32,
    length: usize,

    result: void = undefined,

    fn init(*_, _: *JSGlobalObject)! {
        _.value.protect();
    }

    fn runTask(*_, _: void) {
        unsafe {
            bun.csprng(_.bytes[_.offset..][0.._.length]);
        }
    }

    fn runFromJS(global: *JSGlobalObject, callback: JSValue) {
        const vm = global.bunVM();
        vm.eventLoop().runCallback(callback, global, .js_undefined, &.{ .@"null", _.value });
    }

    fn deinit(*_) {
        _.value.unprotect();
    }
}

pub struct random {
    pub const JobCtx = RandomJobCtx;
    pub const Job = CryptoJob(RandomJobCtx);

    const max_possible_length = @min(jsc.ArrayBuffer.max_size, std.math.maxInt(i32));
    const max_range = 0xffff_ffff_ffff;

    static fn randomInt(global: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const args = callFrame.argumentsAsArray(3);
        let min_value = args[0];
        let max_value = args[1];
        let callback = args[2];

        let min_specified = true;
        if (max_value.isUndefined() || max_value.isCallable()) {
            callback = max_value;
            max_value = min_value;
            min_value = JSValue.jsNumber(0);
            min_specified = false;
        }

        if (!callback.isUndefined()) {
            * = validators.validateFunction(global, "callback", callback)?;
        }

        if (!min_value.isSafeInteger()) {
            return global.throwInvalidArgumentTypeValue2("min", "a safe integer", min_value);
        }
        if (!max_value.isSafeInteger()) {
            return global.throwInvalidArgumentTypeValue2("max", "a safe integer", max_value);
        }

        const min: i64 = @intFromFloat(@trunc(min_value.asNumber()));
        const max: i64 = @intFromFloat(@trunc(max_value.asNumber()));

        if (max <= min) {
            return global.ERR(.OUT_OF_RANGE, "The value of \"max\" is out of range. It must be greater than the value of \"min\" ({d}). Received {d}", .{ 
                min,
                max,
            }).throw();
        }

        if (max - min > max_range) {
            if (min_specified) {
                return global.ERR(.OUT_OF_RANGE, "The value of \"max - min\" is out of range. It must be <= {d}. Received {d}", .{ max_range, max - min }).throw();
            }
            return global.ERR(.OUT_OF_RANGE, "The value of \"max\" is out of range. It must be <= {d}. Received {d}", .{ max_range, max - min }).throw();
        }

        const res = std.crypto.random.intRangeLessThan(i64, min, max);

        if (!callback.isUndefined()) {
            callback.callNextTick(global, .{ .js_undefined, JSValue.jsNumber(res) })?;
            return JSValue.js_undefined;
        }

        return JSValue.jsNumber(res);
    }

    static fn randomUUID(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
        const args = callFrame.arguments();

        let disable_entropy_cache = false;
        if (args.len > 0) {
            const options = args[0];
            if (!options.isUndefined()) {
                validators.validateObject(global, options, "options", .{}, .{})?;
                if (disable_entropy_cache_value := options.get(global, "disableEntropyCache")?) {
                    disable_entropy_cache = validators.validateBoolean(global, disable_entropy_cache_value, "options.disableEntropyCache", .{})?;
                }
            }
        }

        let str_bytes = String.createUninitialized(.latin1, 36);
        let str = str_bytes[0];
        let bytes = str_bytes[1];

        const uuid = if (disable_entropy_cache)
            UUID.init()
        else
            global.bunVM().rareData().nextUUID();

        uuid.print(bytes[0..36]);
        return str.transferToJS(global);
    }

    static fn assertOffset(global: *JSGlobalObject, offset_value: JSValue, element_size: u8, length: usize): JSError!u32 {
        if (!offset_value.isNumber()) {
            return global.throwInvalidArgumentTypeValue("offset", "number", offset_value);
        }
        const offset = offset_value.asNumber() * @as(f64, @floatFromInt(element_size));

        const max_length = @min(length, max_possible_length);
        if (std.math.isNan(offset) || offset > @as(f64, @floatFromInt(max_length)) || offset < 0) {
            return global.throwRangeError(offset, .{ .field_name = "offset", .min = 0, .max = max_length });
        }

        return @intFromFloat(offset);
    }

    static fn assertSize(global: *JSGlobalObject, size_value: JSValue, element_size: u8, offset: u32, length: usize): JSError!u32 {
        let size = validators.validateNumber(global, size_value, "size", null, null)?;
        size *= @as(f64, @floatFromInt(element_size));

        if (std.math.isNan(size) || size > max_possible_length || size < 0) {
            return global.throwRangeError(size, .{ .field_name = "size", .min = 0, .max = max_possible_length });
        }

        if (size + @as(f64, @floatFromInt(offset)) > @as(f64, @floatFromInt(length))) {
            return global.throwRangeError(size + @as(f64, @floatFromInt(offset)), .{ .field_name = "size + offset", .max = @as(i32, @intCast(length)) });
        }

        return @intFromFloat(size);
    }

    static fn randomBytes(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
        const args = callFrame.argumentsAsArray(2);
        const size_value = args[0];
        const callback = args[1];

        const size = assertSize(global, size_value, 1, 0, max_possible_length + 1)?;

        if (!callback.isUndefined()) {
            * = validators.validateFunction(global, "callback", callback)?;
        }

        const alloc_res = jsc.ArrayBuffer.alloc(global, .ArrayBuffer, size)?;
        const result = alloc_res[0];
        const bytes = alloc_res[1];

        if (callback.isUndefined()) {
            // sync
            bun.csprng(bytes);
            return result;
        }

        const ctx: JobCtx = .{
            .value = result,
            .bytes = bytes.ptr,
            .offset = 0,
            .length = size,
        };
        Job.initAndSchedule(global, callback, &ctx)?;

        return JSValue.js_undefined;
    }

    static fn randomFillSync(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
        const args = callFrame.argumentsAsArray(3);
        const buf_value = args[0];
        const offset_value = args[1];
        const size_value = args[2];

        const buf_opt = buf_value.asArrayBuffer(global);
        if (buf_opt == null) {
            return global.throwInvalidArgumentTypeValue("buf", "ArrayBuffer or ArrayBufferView", buf_value);
        }
        const buf = buf_opt.?;

        const element_size = buf.bytesPerElement() ?? 1;

        const offset = assertOffset(
            global,
            if (offset_value.isUndefined()) .jsNumber(0) else offset_value,
            element_size,
            buf.byte_len,
        )?;

        const size = if (size_value.isUndefined())
            buf.byte_len - offset
        else
            assertSize(global, size_value, element_size, offset, buf.byte_len)?;

        if (size == 0) {
            return buf_value;
        }

        bun.csprng(buf.slice()[offset..][0..size]);

        return buf_value;
    }

    static fn randomFill(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
        const args = callFrame.argumentsAsArray(4);
        const buf_value = args[0];
        let offset_value = args[1];
        let size_value = args[2];
        let callback = args[3];

        const buf_opt = buf_value.asArrayBuffer(global);
        if (buf_opt == null) {
            return global.throwInvalidArgumentTypeValue("buf", "ArrayBuffer or ArrayBufferView", buf_value);
        }
        const buf = buf_opt.?;

        const element_size = buf.bytesPerElement() ?? 1;

        let offset: u32 = 0;
        if (offset_value.isCallable()) {
            callback = offset_value;
            offset = assertOffset(global, JSValue.jsNumber(0), element_size, buf.byte_len)?;
            size_value = JSValue.jsNumber(buf.len);
        } else if (size_value.isCallable()) {
            callback = size_value;
            offset = assertOffset(global, offset_value, element_size, buf.byte_len)?;
            // `offset` is a byte offset (already scaled by element_size) but `buf.len`
            // is an element count, so `buf.len - offset` would mix units and can
            // underflow. Defer to the `buf.byte_len - offset` default below instead.
            size_value = .js_undefined;
        } else {
            * = validators.validateFunction(global, "callback", callback)?;
            offset = assertOffset(global, offset_value, element_size, buf.byte_len)?;
        }

        const size = if (size_value.isUndefined())
            buf.byte_len - offset
        else
            assertSize(global, size_value, element_size, offset, buf.byte_len)?;

        if (size == 0) {
            * = callback.call(global, .js_undefined, &.{ .@"null", buf_value })?;
            return JSValue.js_undefined;
        }

        const ctx: JobCtx = .{
            .value = buf_value,
            .bytes = buf.slice().ptr,
            .offset = offset,
            .length = size,
        };
        Job.initAndSchedule(global, callback, &ctx)?;

        return JSValue.js_undefined;
    }
}

fn pbkdf2(globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const data = PBKDF2.fromJS(globalThis, callFrame, true)?;

    const job = PBKDF2.Job.create(jsc.VirtualMachine.get(), globalThis, &data);
    return job.promise.value();
}

fn pbkdf2Sync(globalThis: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    let data = PBKDF2.fromJS(globalThis, callFrame, false)?;
    const _guard = DeinitGuard<@TypeOf(data)>{ ptr: unsafe { &data } };
    const out_arraybuffer = jsc.JSValue.createBufferFromLength(globalThis, @as(usize, @intCast(data.length)))?;

    const output_opt = out_arraybuffer.asArrayBuffer(globalThis);
    if (output_opt == null) {
        return globalThis.throwOutOfMemory();
    }
    const output = output_opt.?;

    if (!data.run(output.slice())) {
        const clear_err = BoringSSL.ERR_clear_error;
        clear_err();
        const e = globalThis.createErrorInstance("PBKDF2 derivation failed", .{});
        return globalThis.throwValue(e);
    }

    return out_arraybuffer;
}

pub fn timingSafeEqual(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
    const args = callFrame.argumentsAsArray(2);
    const l_value = args[0];
    const r_value = args[1];

    const l_buf_opt = l_value.asArrayBuffer(global);
    if (l_buf_opt == null) {
        return global.ERR(.INVALID_ARG_TYPE, "The \"buf1\" argument must be an instance of ArrayBuffer, Buffer, TypedArray, or DataView.", .{}).throw();
    }
    const l_buf = l_buf_opt.?;
    const l = l_buf.byteSlice();

    const r_buf_opt = r_value.asArrayBuffer(global);
    if (r_buf_opt == null) {
        return global.ERR(.INVALID_ARG_TYPE, "The \"buf2\" argument must be an instance of ArrayBuffer, Buffer, TypedArray, or DataView.", .{}).throw();
    }
    const r_buf = r_buf_opt.?;
    const r = r_buf.byteSlice();

    if (l.len != r.len) {
        return global.ERR(.CRYPTO_TIMING_SAFE_EQUAL_LENGTH, "Input buffers must have the same byte length", .{}).throw();
    }

    return JSValue.jsBoolean(BoringSSL.CRYPTO_memcmp(l.ptr, r.ptr, l.len) == 0);
}

pub fn secureHeapUsed(_: *JSGlobalObject, _: *jsc.CallFrame): JSError!JSValue {
    return JSValue.js_undefined;
}

pub fn getFips(_: *JSGlobalObject, _: *jsc.CallFrame): JSError!JSValue {
    return JSValue.jsNumber(0);
}

pub fn setFips(_: *JSGlobalObject, _: *jsc.CallFrame): JSError!JSValue {
    return JSValue.js_undefined;
}

pub fn setEngine(global: *JSGlobalObject, _: *jsc.CallFrame): JSError!JSValue {
    return global.ERR(.CRYPTO_CUSTOM_ENGINE_NOT_SUPPORTED, "Custom engines not supported by BoringSSL", .{}).throw();
}

@callconv("c") fn forEachHash(_: *const BoringSSL.EVP_MD, maybe_from: ([*:0]const u8)?, _: ([*:0]const u8)?, ctx: *!) {
    if (maybe_from == null) return;
    const from_str = maybe_from.?;
    unsafe {
        const hashes: *bun.CaseInsensitiveASCIIStringArrayHashMap(void) = @ptrCast(@alignCast(ctx));
        bun.handleOom(hashes.put(bun.span(from_str), undefined));
    }
}

fn getHashes(global: *JSGlobalObject, _: *jsc.CallFrame): JSError!JSValue {
    let hashes: bun.CaseInsensitiveASCIIStringArrayHashMap(void) = .init(bun.default_allocator);
    const _guard = DeinitGuard<@TypeOf(hashes)>{ ptr: unsafe { &hashes } };

    // TODO(dylan-conway): cache the names
    unsafe {
        BoringSSL.EVP_MD_do_all_sorted(&forEachHash, @ptrCast(@alignCast(&hashes)));
    }

    const array = JSValue.createEmptyArray(global, hashes.count())?;

    let i: u32 = 0;
    for (hash of hashes.keys()) {
        const str = String.createUTF8ForJS(global, hash)?;
        array.putIndex(global, @as(u32, @intCast(i)), str)?;
        i += 1;
    }

    return array;
}

pub type ScryptAsyncResult = struct { Scrypt, JSValue };

pub struct Scrypt {
    password: Node.StringOrBuffer,
    salt: Node.StringOrBuffer,
    N: u32,
    r: u32,
    p: u32,
    maxmem: u64,
    keylen: u32,

    // used in async mode
    buf: jsc.Strong.Optional = .empty,
    result: []u8 = [],
    e: u32? = null,

    pub const AsyncResult = ScryptAsyncResult;

    pub const Job = CryptoJob(Scrypt);

    static fn fromJS(global: *JSGlobalObject, callFrame: *jsc.CallFrame, comptime is_async: bool): JSError!(if (is_async) AsyncResult else Scrypt) {
        const args = callFrame.argumentsAsArray(5);
        const password_value = args[0];
        const salt_value = args[1];
        const keylen_value = args[2];
        let maybe_options_value: JSValue? = args[3];
        let callback = args[4];

        if (is_async) {
            if (callback.isUndefined()) {
                callback = maybe_options_value.?;
                maybe_options_value = null;
            }
        }

        const password_opt = Node.StringOrBuffer.fromJSMaybeAsync(global, bun.default_allocator, password_value, is_async, true)?;
        if (password_opt == null) {
            return global.throwInvalidArgumentTypeValue("password", "string, ArrayBuffer, Buffer, TypedArray, or DataView", password_value);
        }
        let password = password_opt.?;
        let _pw_guard = StringOrBufferGuard{ ptr: unsafe { &password }, is_async: is_async };

        const salt_opt = Node.StringOrBuffer.fromJSMaybeAsync(global, bun.default_allocator, salt_value, is_async, true)?;
        if (salt_opt == null) {
            return global.throwInvalidArgumentTypeValue("salt", "string, ArrayBuffer, Buffer, TypedArray, or DataView", salt_value);
        }
        let salt = salt_opt.?;
        let _salt_guard = StringOrBufferGuard{ ptr: unsafe { &salt }, is_async: is_async };

        const keylen = validators.validateInt32(global, keylen_value, "keylen", .{}, 0, null)?;

        let N: u32? = null;
        let r: u32? = null;
        let p: u32? = null;
        let maxmem: i64? = null;

        if (options_value := maybe_options_value) {
            if (options := options_value.getObject()) {
                if (N_value := options.get(global, "N")?) {
                    N = validators.validateUint32(global, N_value, "N", .{}, false)?;
                }

                if (cost_value := options.get(global, "cost")?) {
                    if (N != null) {
                        return global.throwIncompatibleOptionPair("N", "cost");
                    }

                    N = validators.validateUint32(global, cost_value, "cost", .{}, false)?;
                }

                if (r_value := options.get(global, "r")?) {
                    r = validators.validateUint32(global, r_value, "r", .{}, false)?;
                }

                if (blocksize_value := options.get(global, "blockSize")?) {
                    if (r != null) {
                        return global.throwIncompatibleOptionPair("r", "blockSize");
                    }

                    r = validators.validateUint32(global, blocksize_value, "blockSize", .{}, false)?;
                }

                if (p_value := options.get(global, "p")?) {
                    p = validators.validateUint32(global, p_value, "p", .{}, false)?;
                }

                if (parallelization_value := options.get(global, "parallelization")?) {
                    if (p != null) {
                        return global.throwIncompatibleOptionPair("p", "parallelization");
                    }

                    p = validators.validateUint32(global, parallelization_value, "parallelization", .{}, false)?;
                }

                if (maxmem_value := options.get(global, "maxmem")?) {
                    maxmem = validators.validateInteger(global, maxmem_value, "maxmem", 0, null)?;
                }
            }
        }

        const N_default: u32 = 16384;
        const r_default: u32 = 8;
        const p_default: u32 = 1;
        const maxmem_default: i64 = 33554432;

        if (N == null || N.? == 0) {
            N = N_default;
        }
        if (r == null || r.? == 0) {
            r = r_default;
        }
        if (p == null || p.? == 0) {
            p = p_default;
        }
        if (maxmem == null || maxmem.? == 0) {
            maxmem = maxmem_default;
        }

        const ctx: Scrypt = .{
            .password = password,
            .salt = salt,
            .N = N.?,
            .r = r.?,
            .p = p.?,
            .maxmem = @as(u64, @intCast(maxmem.?)),
            .keylen = @as(u32, @intCast(keylen)),
        };

        if (is_async) {
            * = validators.validateFunction(global, "callback", callback)?;
        }

        ctx.checkScryptParams(global)?;

        _pw_guard.ptr = null;
        _salt_guard.ptr = null;

        if (is_async) {
            return .{ ctx, callback };
        }

        return ctx;
    }

    fn checkScryptParams(global: *JSGlobalObject)! {
        const N = _.N;
        const r = _.r;
        const p = _.p;
        const maxmem = _.maxmem;
        if (BoringSSL.EVP_PBE_validate_scrypt_params(
            null,
            0,
            null,
            0,
            N,
            r,
            p,
            maxmem,
            null,
            0,
        ) == 0) {
            return global.throwInvalidScryptParams();
        }
    }

    fn init(*_, global: *JSGlobalObject)! {
        if (_.keylen > jsc.VirtualMachine.synthetic_allocation_limit) {
            return global.throwOutOfMemory();
        }
        const alloc_res = jsc.ArrayBuffer.alloc(global, .ArrayBuffer, _.keylen)?;
        const buf = alloc_res[0];
        const bytes = alloc_res[1];

        // to be filled in later
        _.result = bytes;
        _.buf = .create(buf, global);
    }

    fn runTask(*_, key: []u8) {
        const password = _.password.slice();
        const salt = _.salt.slice();

        if (key.len == 0) {
            // result will be an empty buffer
            return;
        }

        if (password.len > std.math.maxInt(i32) || salt.len > std.math.maxInt(i32)) {
            _.e = 0;
            return;
        }

        const res = BoringSSL.EVP_PBE_scrypt(
            password.ptr,
            password.len,
            salt.ptr,
            salt.len,
            _.N,
            _.r,
            _.p,
            _.maxmem,
            key.ptr,
            key.len,
        );

        if (res == 0) {
            const get_err = BoringSSL.ERR_peek_last_error;
            _.e = get_err();
            return;
        }
    }

    fn runFromJS(*_, global: *JSGlobalObject, callback: JSValue) {
        const vm = global.bunVM();

        if (err_code := _.e) {
            if (err_code != 0) {
                let buf: [256]u8 = undefined;
                const msg = BoringSSL.ERR_error_string_n(err_code, &buf, buf.len);
                const exception = global.ERR(.CRYPTO_OPERATION_FAILED, "Scrypt failed: {s}", .{ msg }).toJS();
                vm.eventLoop().runCallback(callback, global, .js_undefined, &.{ exception });
                return;
            }

            const exception = global.ERR(.CRYPTO_OPERATION_FAILED, "Scrypt failed", .{}).toJS();
            vm.eventLoop().runCallback(callback, global, .js_undefined, &.{ exception });
            return;
        }

        const buf = _.buf.swap();
        vm.eventLoop().runCallback(callback, global, .js_undefined, &.{ .js_undefined, buf });
    }

    fn deinit(*_) {
        _.salt.deinitAndUnprotect();
        _.password.deinitAndUnprotect();
        _.buf.deinit();
    }

    fn deinitSync(*_) {
        _.salt.deinit();
        _.password.deinit();
        _.buf.deinit();
    }
}

fn scrypt(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
    const res = Scrypt.fromJS(global, callFrame, true)?;
    const ctx = res[0];
    const callback = res[1];
    Scrypt.Job.initAndSchedule(global, callback, &ctx)?;
    return JSValue.js_undefined;
}

fn scryptSync(global: *JSGlobalObject, callFrame: *jsc.CallFrame): JSError!JSValue {
    let ctx = Scrypt.fromJS(global, callFrame, false)?;
    const _guard = DeinitSyncGuard<Scrypt>{ ptr: unsafe { &ctx } };
    const alloc_res = jsc.ArrayBuffer.alloc(global, .ArrayBuffer, ctx.keylen)?;
    const buf = alloc_res[0];
    const bytes = alloc_res[1];
    ctx.runTask(bytes);
    return buf;
}

pub fn createNodeCryptoBindingZig(global: *jsc.JSGlobalObject): jsc.JSValue {
    const crypto = jsc.JSValue.createEmptyObject(global, 15);

    crypto.put(global, String.init("pbkdf2"), jsc.JSFunction.create(global, "pbkdf2", pbkdf2, 5, .{}));
    crypto.put(global, String.init("pbkdf2Sync"), jsc.JSFunction.create(global, "pbkdf2Sync", pbkdf2Sync, 5, .{}));
    crypto.put(global, String.init("randomInt"), jsc.JSFunction.create(global, "randomInt", random.randomInt, 2, .{}));
    crypto.put(global, String.init("randomFill"), jsc.JSFunction.create(global, "randomFill", random.randomFill, 4, .{}));
    crypto.put(global, String.init("randomFillSync"), jsc.JSFunction.create(global, "randomFillSync", random.randomFillSync, 3, .{}));
    crypto.put(global, String.init("randomUUID"), jsc.JSFunction.create(global, "randomUUID", random.randomUUID, 1, .{}));
    crypto.put(global, String.init("randomBytes"), jsc.JSFunction.create(global, "randomBytes", random.randomBytes, 2, .{}));
    crypto.put(global, String.init("timingSafeEqual"), jsc.JSFunction.create(global, "timingSafeEqual", timingSafeEqual, 2, .{}));

    crypto.put(global, String.init("secureHeapUsed"), jsc.JSFunction.create(global, "secureHeapUsed", secureHeapUsed, 0, .{}));
    crypto.put(global, String.init("getFips"), jsc.JSFunction.create(global, "getFips", getFips, 0, .{}));
    crypto.put(global, String.init("setFips"), jsc.JSFunction.create(global, "setFips", setFips, 1, .{}));
    crypto.put(global, String.init("setEngine"), jsc.JSFunction.create(global, "setEngine", setEngine, 2, .{}));

    crypto.put(global, String.init("getHashes"), jsc.JSFunction.create(global, "getHashes", getHashes, 0, .{}));

    crypto.put(global, String.init("scrypt"), jsc.JSFunction.create(global, "scrypt", scrypt, 5, .{}));
    crypto.put(global, String.init("scryptSync"), jsc.JSFunction.create(global, "scryptSync", scryptSync, 4, .{}));

    return crypto;
}
