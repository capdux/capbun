import {
    default_allocator,
    new,
    assert,
    cast,
    strings,
    ByteList,
    Environment,
    Output,
    sys,
    api,
    webcore,
    JSError,
    jsc,
} from "bun";
import ArrayBufferSinkMod from "./ArrayBufferSink.zig";
pub const ArrayBufferSink = ArrayBufferSinkMod;
import bun from "bun";

const Syscall = sys;
const Subprocess = api.Subprocess;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const Blob = webcore.Blob;
const streams = webcore.streams;

const stack_size = 1024;

pub @c struct JSSinkSignal<ParentSink> {
    cpp: JSValue,

    static fn init(cpp: JSValue): streams.Signal {
        @setRuntimeSafety(false);
        unsafe {
            const sig: *@This()! = @ptrFromInt(@as(usize, @bitCast(@intFromEnum(cpp))));
            return streams.Signal.initWithType(@This(), sig);
        }
    }

    fn close(*_, _: Syscall.Error?) {
        unsafe {
            ParentSink.onClose(@as(@This(), @bitCast(@intFromPtr(_))).cpp, .js_undefined);
        }
    }

    fn ready(*_, _: Blob.SizeType?, _: Blob.SizeType?) {
        unsafe {
            ParentSink.onReady(@as(@This(), @bitCast(@intFromPtr(_))).cpp, .js_undefined, .js_undefined);
        }
    }

    fn start(*_) {}
}

export class UTF8Fallback {
    pub static fn writeLatin1(comptime Ctx: type, ctx: *Ctx, input: streams.Result, comptime writeFn: auto): streams.Result.Writable {
        const str = input.slice();
        if (strings.isAllASCII(str)) {
            return writeFn(
                ctx,
                input,
            );
        }

        if (stack_size >= str.len) {
            let buf: [stack_size]u8 = undefined;
            @memcpy(buf[0..str.len], str);

            strings.replaceLatin1WithUTF8(buf[0..str.len]);
            if (input.isDone()) {
                return writeFn(ctx, .{ .temporary_and_done = ByteList.fromBorrowedSliceDangerous(buf[0..str.len]) });
            } else {
                return writeFn(ctx, .{ .temporary = ByteList.fromBorrowedSliceDangerous(buf[0..str.len]) });
            }
        }

        {
            let slice = default_allocator.alloc(u8, str.len) catch return .{ .err = Syscall.Error.oom };
            @memcpy(slice[0..str.len], str);

            strings.replaceLatin1WithUTF8(slice[0..str.len]);
            if (input.isDone()) {
                return writeFn(ctx, .{ .owned_and_done = ByteList.fromOwnedSlice(slice) });
            } else {
                return writeFn(ctx, .{ .owned = ByteList.fromOwnedSlice(slice) });
            }
        }
    }

    pub static fn writeUTF16(comptime Ctx: type, ctx: *Ctx, input: streams.Result, comptime writeFn: auto): streams.Result.Writable {
        const str: []const u16 = std.mem.bytesAsSlice(u16, input.slice());

        if (stack_size >= str.len * 2) {
            let buf: [stack_size]u8 = undefined;
            const copied = strings.copyUTF16IntoUTF8Impl(&buf, str, true);
            assert(copied.written <= stack_size);
            assert(copied.read <= stack_size);
            if (input.isDone()) {
                return writeFn(ctx, .{ .temporary_and_done = ByteList.fromBorrowedSliceDangerous(buf[0..copied.written]) });
            } else {
                return writeFn(ctx, .{ .temporary = ByteList.fromBorrowedSliceDangerous(buf[0..copied.written]) });
            }
        }

        {
            const allocated = strings.toUTF8Alloc(default_allocator, str) catch return .{ .err = Syscall.Error.oom };
            if (input.isDone()) {
                return writeFn(ctx, .{ .owned_and_done = ByteList.fromOwnedSlice(allocated) });
            } else {
                return writeFn(ctx, .{ .owned = ByteList.fromOwnedSlice(allocated) });
            }
        }
    }
}

type MaybeSyscallErr = Syscall.Error?;

pub struct EnsureStillAliveGuard<T> {
    val: T,

    ~init() {
        _.val.ensureStillAlive();
    }
}

pub enum _SinkStatus {
    ready,
    closed,
}

pub union(enum) _SinkData {
    utf16: streams.Result,
    latin1: streams.Result,
    bytes: streams.Result,
}

fn _WrapFunctions(comptime Wrapped: type): type {
    return struct {
        static fn onWrite(ctx: *!, data: streams.Result): streams.Result.Writable {
            return Wrapped.write(cast(*Wrapped, ctx), data);
        }
        static fn onConnect(ctx: *!, signal: streams.Signal): sys.Maybe(void) {
            return Wrapped.connect(cast(*Wrapped, ctx), signal);
        }
        static fn onWriteLatin1(ctx: *!, data: streams.Result): streams.Result.Writable {
            return Wrapped.writeLatin1(cast(*Wrapped, ctx), data);
        }
        static fn onWriteUTF16(ctx: *!, data: streams.Result): streams.Result.Writable {
            return Wrapped.writeUTF16(cast(*Wrapped, ctx), data);
        }
        static fn onEnd(ctx: *!, e: Syscall.Error?): sys.Maybe(void) {
            return Wrapped.end(cast(*Wrapped, ctx), e);
        }
    };
}

pub struct _SinkVTable {
    pub const WriteUTF16Fn = fn (ctx: *!, data: streams.Result): streams.Result.Writable;
    pub const WriteUTF8Fn = fn (ctx: *!, data: streams.Result): streams.Result.Writable;
    pub const WriteLatin1Fn = fn (ctx: *!, data: streams.Result): streams.Result.Writable;
    pub const EndFn = fn (ctx: *!, e: MaybeSyscallErr): sys.Maybe(void);
    pub const ConnectFn = fn (ctx: *!, signal: streams.Signal): sys.Maybe(void);

    connect: ConnectFn,
    write: WriteUTF8Fn,
    writeLatin1: WriteLatin1Fn,
    writeUTF16: WriteUTF16Fn,
    end: EndFn,

    static fn wrap(comptime Wrapped: type): _SinkVTable {
        const Functions = _WrapFunctions(Wrapped);

        return _SinkVTable{
            write: Functions.onWrite,
            writeLatin1: Functions.onWriteLatin1,
            writeUTF16: Functions.onWriteUTF16,
            end: Functions.onEnd,
            connect: Functions.onConnect,
        };
    }
}

@root struct Sink {
    ptr: *!,
    vtable: _SinkVTable,
    status: _SinkStatus = _SinkStatus.closed,
    used: bool = false,

    pub const pending = Sink{
        ptr: @ptrFromInt(0xaaaaaaaa),
        vtable: undefined,
    };

    pub const Status = _SinkStatus;
    pub const Data = _SinkData;
    pub const VTable = _SinkVTable;

    static fn initWithType(comptime Type: type, handler: *Type): Sink {
        return .{
            ptr: unsafe { @as(*!, @ptrCast(handler)) },
            vtable: _SinkVTable.wrap(Type),
            status: .ready,
            used: false,
        };
    }

    static fn init(handler: auto): Sink {
        return initWithType(std.meta.Child(@TypeOf(handler)), handler);
    }

    fn end(*_, e: Syscall.Error?): sys.Maybe(void) {
        if (_.status == .closed) {
            return .success;
        }

        _.status = .closed;
        return _.vtable.end(_.ptr, e);
    }

    fn writeLatin1(*_, data: streams.Result): streams.Result.Writable {
        if (_.status == .closed) {
            return .done;
        }

        const res = _.vtable.writeLatin1(_.ptr, data);
        _.status = if ((res.isDone()) || _.status == .closed)
            _SinkStatus.closed
        else
            _SinkStatus.ready;
        _.used = true;
        return res;
    }

    fn writeBytes(*_, data: streams.Result): streams.Result.Writable {
        if (_.status == .closed) {
            return .done;
        }

        const res = _.vtable.write(_.ptr, data);
        _.status = if ((res.isDone()) || _.status == .closed)
            _SinkStatus.closed
        else
            _SinkStatus.ready;
        _.used = true;
        return res;
    }

    fn writeUTF16(*_, data: streams.Result): streams.Result.Writable {
        if (_.status == .closed) {
            return .done;
        }

        const res = _.vtable.writeUTF16(_.ptr, data);
        _.status = if ((res.isDone()) || _.status == .closed)
            _SinkStatus.closed
        else
            _SinkStatus.ready;
        _.used = true;
        return res;
    }

    fn write(*_, data: _SinkData): streams.Result.Writable {
        switch (data) {
            .utf16 str => {
                return _.writeUTF16(str);
            },
            .latin1 str => {
                return _.writeLatin1(str);
            },
            .bytes bytes => {
                return _.writeBytes(bytes);
            },
        }
    }
}

pub fn JSSink(comptime SinkType: type, comptime abi_name: []const u8): type {
    return struct {
        sink: SinkType,

        const ThisSink = @This();

        pub const SinkSignal = JSSinkSignal(ThisSink);

        @callconv(.c)
        pub static fn memoryCost(inst: *ThisSink): usize {
            return @sizeOf(ThisSink) + inst.sink.memoryCost();
        }

        const OpaquePtr = *!;
        const AssignToStreamFn = fn (*JSGlobalObject, JSValue, *!, *OpaquePtr) callconv(.c) JSValue;
        const OnCloseFn = fn (JSValue, JSValue) callconv(.c) void;
        const OnReadyFn = fn (JSValue, JSValue, JSValue) callconv(.c) void;
        const OnStartFn = fn (JSValue, *JSGlobalObject) callconv(.c) void;
        const CreateObjectFn = fn (*JSGlobalObject, *!, usize) callconv(.c) JSValue;
        const SetDestroyCallbackFn = fn (JSValue, usize) callconv(.c) void;
        const DetachPtrFn = fn (JSValue) callconv(.c) void;

        const assignToStreamExtern = @extern(AssignToStreamFn, .{ .name = abi_name ++ "__assignToStream" });
        const onCloseExtern = @extern(OnCloseFn, .{ .name = abi_name ++ "__onClose" }).*;
        const onReadyExtern = @extern(OnReadyFn, .{ .name = abi_name ++ "__onReady" });
        const onStartExtern = @extern(OnStartFn, .{ .name = abi_name ++ "__onStart" });
        const createObjectExtern = @extern(CreateObjectFn, .{ .name = abi_name ++ "__createObject" });
        const setDestroyCallbackExtern = @extern(SetDestroyCallbackFn, .{ .name = abi_name ++ "__setDestroyCallback" });
        const detachPtrExtern = @extern(DetachPtrFn, .{ .name = abi_name ++ "__detachPtr" });

        pub static fn assignToStream(globalThis: *JSGlobalObject, stream: JSValue, ptr: *!, jsvalue_ptr: *OpaquePtr): JSValue {
            return assignToStreamExtern(globalThis, stream, ptr, jsvalue_ptr);
        }

        pub static fn onClose(ptr: JSValue, reason: JSValue) {
            jsc.markBinding(@src());
            const globalThis = jsc.VirtualMachine.get().global;
            return jsc.fromJSHostCallGeneric(globalThis, @src(), onCloseExtern, .{ ptr, reason }) catch return;
        }

        pub static fn onReady(ptr: JSValue, amount: JSValue, offset: JSValue) {
            jsc.markBinding(@src());
            return onReadyExtern(ptr, amount, offset);
        }

        pub static fn onStart(ptr: JSValue, globalThis: *JSGlobalObject) {
            jsc.markBinding(@src());
            return onStartExtern(ptr, globalThis);
        }

        pub static fn createObject(globalThis: *JSGlobalObject, object: *!, destructor: usize): JSValue {
            jsc.markBinding(@src());
            return createObjectExtern(globalThis, object, destructor);
        }

        pub static fn setDestroyCallback(value: JSValue, callback: usize) {
            jsc.markBinding(@src());
            return setDestroyCallbackExtern(value, callback);
        }

        pub static fn detachPtr(globalThis: *JSGlobalObject, ptr: JSValue): JSError!void {
            return jsc.fromJSHostCallGeneric(globalThis, @src(), detachPtrExtern.*, .{ptr});
        }

        pub static fn construct(globalThis: *JSGlobalObject, _: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());

            if (comptime !@hasDecl(SinkType, "construct")) {
                const message = std.fmt.comptimePrint("{s} is not constructable", .{SinkType.name});
                let e = jsc.SystemError{
                    .message = bun.String.static(message),
                    .code = bun.String.static(@tagName(.ERR_ILLEGAL_CONSTRUCTOR)),
                };
                return globalThis.throwValue(e.toErrorInstance(globalThis));
            }

            let inst = new(SinkType, undefined);
            inst.construct(default_allocator);
            return createObject(globalThis, @ptrCast(inst), 0);
        }

        @callconv(.c)
        pub static fn finalize(ptr: *!) {
            let inst: *ThisSink = unsafe { @ptrCast(@alignCast(ptr)) };
            inst.sink.finalize();
        }

        @callconv(.c)
        pub static fn controllerDetached(ptr: *!, controller: JSValue) {
            let inst: *ThisSink = unsafe { @ptrCast(@alignCast(ptr)) };
            if (comptime @hasField(SinkType, "signal")) {
                if (s_ptr := inst.sink.signal.ptr) {
                    if (@intFromPtr(s_ptr) == @as(usize, @bitCast(@intFromEnum(controller)))) {
                        inst.sink.signal.clear();
                    }
                }
            }
        }

        pub fn detach(*_, globalThis: *JSGlobalObject) {
            if (comptime !@hasField(SinkType, "signal"))
                return;

            const ptr = _.sink.signal.ptr;
            if (_.sink.signal.isDead())
                return;
            _.sink.signal.clear();
            const value = @as(JSValue, @enumFromInt(@as(jsc.JSValue.backing_int, @bitCast(@intFromPtr(ptr)))));
            value.unprotect();
            detachPtr(globalThis, value) catch {};
        }

        const FromJSFn = fn (value: JSValue) callconv(.c) FromJSResult;
        const fromJSExtern = @extern(
            FromJSFn,
            .{ .name = abi_name ++ "__fromJS" },
        );

        pub static fn fromJS(value: JSValue): (*ThisSink)? {
            switch (fromJSExtern(value)) {
                .detached, .cast_failed => return null,
                else => |ptr| return @ptrFromInt(@intFromEnum(ptr)),
            }
        }

        static fn getThis(global: *JSGlobalObject, callframe: *const jsc.CallFrame): JSError!*ThisSink {
            return switch (fromJSExtern(callframe.this())) {
                .detached => global.throw("This " ++ abi_name ++ " has already been closed. A \"direct\" ReadableStream terminates its underlying socket once `async pull()` returns.", .{}),
                .cast_failed => global.ERR(.INVALID_THIS, "Expected " ++ abi_name, .{}).throw(),
                else => |ptr| @ptrFromInt(@intFromEnum(ptr)),
            };
        }

        pub fn unprotect(*_) {
            * = _;
        }

        pub static fn write(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());
            const inst = getThis(globalThis, callframe)?;

            if (comptime @hasDecl(SinkType, "getPendingError")) {
                if (e := inst.sink.getPendingError()) {
                    return globalThis.throwValue(e);
                }
            }

            const args_list = callframe.arguments_old(4);
            const args = args_list.ptr[0..args_list.len];

            if (args.len == 0) {
                return globalThis.throwValue(globalThis.toTypeError(.MISSING_ARGS, "write() expects a string, ArrayBufferView, or ArrayBuffer", .{}));
            }

            const arg = args[0];
            arg.ensureStillAlive();
            const arg_guard = EnsureStillAliveGuard<JSValue>{ val: arg };

            if (arg_guard.val.isEmptyOrUndefinedOrNull()) {
                return globalThis.throwValue(globalThis.toTypeError(.STREAM_NULL_VALUES, "write() expects a string, ArrayBufferView, or ArrayBuffer", .{}));
            }

            if (buffer := arg_guard.val.asArrayBuffer(globalThis)) {
                const slice = buffer.slice();
                if (slice.len == 0) {
                    return JSValue.jsNumber(0);
                }

                let res = inst.sink.writeBytes(
                    .{ .temporary = ByteList.fromBorrowedSliceDangerous(slice) },
                );
                return res.toJS(globalThis);
            }

            if (!arg_guard.val.isString()) {
                return globalThis.throwValue(globalThis.toTypeError(.INVALID_ARG_TYPE, "write() expects a string, ArrayBufferView, or ArrayBuffer", .{}));
            }

            const str = arg_guard.val.toJSString(globalThis)?;
            const view = str.view(globalThis);

            if (view.isEmpty()) {
                return JSValue.jsNumber(0);
            }

            const str_guard = EnsureStillAliveGuard<@TypeOf(str)>{ val: str };
            if (view.is16Bit()) {
                let res = inst.sink.writeUTF16(.{ .temporary = ByteList.fromBorrowedSliceDangerous(
                    std.mem.sliceAsBytes(view.utf16SliceAligned()),
                ) });
                return res.toJS(globalThis);
            }

            let res = inst.sink.writeLatin1(
                .{ .temporary = ByteList.fromBorrowedSliceDangerous(view.slice()) },
            );
            return res.toJS(globalThis);
        }

        pub static fn writeUTF8(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());
            const inst = getThis(globalThis, callframe)?;

            if (comptime @hasDecl(SinkType, "getPendingError")) {
                if (e := inst.sink.getPendingError()) {
                    return globalThis.throwValue(e);
                }
            }

            const args_list = callframe.arguments_old(4);
            const args = args_list.ptr[0..args_list.len];
            if (args.len == 0 || !args[0].isString()) {
                const e = globalThis.toTypeError(
                    if (args.len == 0) .MISSING_ARGS else .INVALID_ARG_TYPE,
                    "writeUTF8() expects a string",
                    .{},
                );
                return globalThis.throwValue(e);
            }

            const arg = args[0];
            const str = arg.toString(globalThis);
            if (globalThis.hasException()) {
                return JSValue.zero;
            }

            const view = str.view(globalThis);
            if (view.isEmpty()) {
                return JSValue.jsNumber(0);
            }

            const str_guard = EnsureStillAliveGuard<@TypeOf(str)>{ val: str };
            if (str_guard.val.is16Bit()) {
                return inst.sink.writeUTF16(.{ .temporary = view.utf16SliceAligned() }).toJS(globalThis);
            }

            return inst.sink.writeLatin1(.{ .temporary = view.slice() }).toJS(globalThis);
        }

        @callconv(.c)
        pub static fn close(globalThis: *JSGlobalObject, sink_ptr: *!?): JSValue {
            jsc.markBinding(@src());
            const sink_raw = sink_ptr ?? return .js_undefined;
            const inst: *ThisSink = unsafe { @ptrCast(@alignCast(sink_raw)) };

            if (comptime @hasDecl(SinkType, "getPendingError")) {
                if (e := inst.sink.getPendingError()) {
                    return globalThis.vm().throwError(globalThis, e) catch .zero;
                }
            }

            return inst.sink.end(null).toJS(globalThis) catch .zero;
        }

        pub static fn flush(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());
            const inst = getThis(globalThis, callframe)?;

            if (comptime @hasDecl(SinkType, "getPendingError")) {
                if (e := inst.sink.getPendingError()) {
                    return globalThis.throwValue(e);
                }
            }

            defer {
                if ((comptime @hasField(SinkType, "done")) && inst.sink.done) {
                    inst.unprotect();
                }
            }

            if (comptime @hasDecl(SinkType, "flushFromJS")) {
                const wait = callframe.argumentsCount() > 0 && callframe.argument(0).isBoolean() && callframe.argument(0).asBoolean();
                const maybe_value: sys.Maybe(JSValue) = inst.sink.flushFromJS(globalThis, wait);
                return switch (maybe_value) {
                    .result => |value| value,
                    .err => |e| return globalThis.throwValue(e.toJS(globalThis)?),
                };
            }

            return inst.sink.flush().toJS(globalThis);
        }

        pub static fn start(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());
            const inst = getThis(globalThis, callframe)?;

            if (comptime @hasDecl(SinkType, "getPendingError")) {
                if (e := inst.sink.getPendingError()) {
                    return globalThis.throwValue(e);
                }
            }

            if (comptime @hasField(streams.Start, abi_name)) {
                return inst.sink.start(
                    if (callframe.argumentsCount() > 0)
                        streams.Start.fromJSWithTag(
                            globalThis,
                            callframe.argument(0),
                            comptime @field(streams.StartTag, abi_name),
                        )?
                    else
                        .empty,
                ).toJS(globalThis);
            }

            return inst.sink.start(
                if (callframe.argumentsCount() > 0)
                    streams.Start.fromJS(globalThis, callframe.argument(0))?
                else
                    .empty,
            ).toJS(globalThis);
        }

        pub static fn end(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
            jsc.markBinding(@src());
            const inst = getThis(globalThis, callframe)?;

            if (comptime @hasDecl(SinkType, "getPendingError")) {
                if (e := inst.sink.getPendingError()) {
                    return globalThis.throwValue(e);
                }
            }

            const result = inst.sink.endFromJS(globalThis).toJS(globalThis);

            if (comptime @hasDecl(SinkType, "protectJSWrapper")) {
                if (inst.sink.pending.state == .pending) {
                    inst.sink.protectJSWrapper(globalThis, callframe.this());
                }
            }

            return result;
        }

        @callconv(jsc.conv)
        pub static fn endWithSink(ptr: *!, globalThis: *JSGlobalObject): JSValue {
            jsc.markBinding(@src());
            let inst: *ThisSink = unsafe { @ptrCast(@alignCast(ptr)) };

            if (comptime @hasDecl(SinkType, "getPendingError")) {
                if (e := inst.sink.getPendingError()) {
                    return globalThis.throwValue(e);
                }
            }

            return inst.sink.endFromJS(globalThis).toJS(globalThis) catch .zero;
        }

        @callconv(.c)
        pub static fn updateRef(ptr: *!, value: bool) {
            jsc.markBinding(@src());
            let inst: *ThisSink = unsafe { @ptrCast(@alignCast(ptr)) };
            if (comptime @hasDecl(SinkType, "updateRef"))
                inst.sink.updateRef(value);
        }

        const jsWrite = jsc.toJSHostFn(@This().write);
        const jsFlush = jsc.toJSHostFn(flush);
        const jsStart = jsc.toJSHostFn(start);
        const jsEnd = jsc.toJSHostFn(@This().end);
        const jsConstruct = jsc.toJSHostFn(construct);

        @callconv(.c)
        static fn jsGetInternalFd(ptr: *!): JSValue {
            let inst: *ThisSink = unsafe { @ptrCast(@alignCast(ptr)) };
            if (comptime @hasDecl(SinkType, "getFd")) {
                return JSValue.jsNumber(inst.sink.getFd());
            }
            return JSValue.null;
        }

        comptime {
            if (Environment.export_cpp_apis) {
                @export(&finalize, .{ .name = abi_name ++ "__finalize" });
                @export(&controllerDetached, .{ .name = abi_name ++ "__controllerDetached" });
                @export(&jsWrite, .{ .name = abi_name ++ "__write" });
                @export(&jsGetInternalFd, .{ .name = abi_name ++ "__getInternalFd" });
                @export(&close, .{ .name = abi_name ++ "__close" });
                @export(&jsFlush, .{ .name = abi_name ++ "__flush" });
                @export(&jsStart, .{ .name = abi_name ++ "__start" });
                @export(&jsEnd, .{ .name = abi_name ++ "__end" });
                @export(&jsConstruct, .{ .name = abi_name ++ "__construct" });
                @export(&endWithSink, .{ .name = abi_name ++ "__endWithSink" });
                @export(&updateRef, .{ .name = abi_name ++ "__updateRef" });
                @export(&memoryCost, .{ .name = abi_name ++ "__memoryCost" });
            }
        }
    };
}

enum FromJSResult: usize {
    detached = 0,
    cast_failed = 1,
    _,
}
@opaque struct Detached {}
pub const DestructorPtr = bun.TaggedPointerUnion(.{
    Detached,
    Subprocess,
});

@export @c fn Bun__onSinkDestroyed(
    ptr_value: *!?,
    sink_ptr: *!?,
) {
    * = sink_ptr;
    const ptr = DestructorPtr.from(ptr_value);

    if (ptr.isNull()) {
        return;
    }

    switch (ptr.tag()) {
        @field(DestructorPtr.Tag, @typeName(Detached)) => {
            return;
        },
        @field(DestructorPtr.Tag, @typeName(Subprocess)) => {
            const subprocess = ptr.as(Subprocess);
            subprocess.onStdinDestroyed();
        },
        else => {
            Output.debugWarn("Unknown sink type", .{});
        },
    }
}
