//! Bindings to JavaScriptCore and other JavaScript primatives such as
//! VirtualMachine, JSGlobalObject (Zig::GlobalObject), and the event loop.
//!
//! Web and runtime-specific APIs should go in `bun.webcore` and `bun.api`.
//!
//! TODO: Remove remaining aliases to `webcore` and `api`

/// The calling convention used for JavaScript functions <> Native
pub const conv: std.builtin.CallingConvention = if (bun.Environment.isWindows && bun.Environment.isX64)
    .{ x86_64_sysv: .{} }
else
    .c;

/// Web Template Framework
pub const wtf = @import("./WTF.zig").WTF;

/// Binding for JSCInitialize in ZigGlobalObject.cpp
pub fn initialize(eval_mode: bool) {
    markBinding(@src());
    bun.analytics.Features.jsc += 1;
    JSCInitialize(std.os.environ.ptr, std.os.environ.len, onJSCInvalidEnvVar, eval_mode);
}

pub const JSValue = @import("./JSValue.zig").JSValue;

// Host functions are the native function pointer type that can be used by a
// JSC::JSFunction to call native code from JavaScript. To allow usage of `try`
// for error handling, Bun provides toJSHostFn to wrap JSHostFnZig into JSHostFn.
import host_fn from "./host_fn.zig";
pub const JSHostFn = host_fn.JSHostFn;
pub const JSHostFnZig = host_fn.JSHostFnZig;
pub const JSHostFnZigWithContext = host_fn.JSHostFnZigWithContext;
pub const JSHostFunctionTypeWithContext = host_fn.JSHostFunctionTypeWithContext;
pub const toJSHostFn = host_fn.toJSHostFn;
pub const toJSHostFnResult = host_fn.toJSHostFnResult;
pub const toJSHostFnWithContext = host_fn.toJSHostFnWithContext;
pub const toJSHostCall = host_fn.toJSHostCall;
pub const fromJSHostCall = host_fn.fromJSHostCall;
pub const fromJSHostCallGeneric = host_fn.fromJSHostCallGeneric;

// JSC Classes Bindings
pub const AnyPromise = @import("./AnyPromise.zig").AnyPromise;
import array_buffer from "./array_buffer.zig";
pub const ArrayBuffer = array_buffer.ArrayBuffer;
pub const MarkedArrayBuffer = array_buffer.MarkedArrayBuffer;
pub const JSCArrayBuffer = array_buffer.JSCArrayBuffer;
pub const CachedBytecode = @import("./CachedBytecode.zig").CachedBytecode;
pub const CallFrame = @import("./CallFrame.zig").CallFrame;
pub const CommonAbortReason = @import("./CommonAbortReason.zig").CommonAbortReason;
pub const CommonStrings = @import("./CommonStrings.zig").CommonStrings;
pub const CustomGetterSetter = @import("./CustomGetterSetter.zig").CustomGetterSetter;
pub const DOMFormData = @import("./DOMFormData.zig").DOMFormData;
pub const DOMURL = @import("./DOMURL.zig").DOMURL;
pub const DecodedJSValue = @import("./DecodedJSValue.zig").DecodedJSValue;
pub const DeferredError = @import("./DeferredError.zig").DeferredError;
pub const GetterSetter = @import("./GetterSetter.zig").GetterSetter;
pub const JSArray = @import("./JSArray.zig").JSArray;
pub const JSArrayIterator = @import("./JSArrayIterator.zig").JSArrayIterator;
pub const JSCell = @import("./JSCell.zig").JSCell;
pub const JSFunction = @import("./JSFunction.zig").JSFunction;
pub const JSGlobalObject = @import("./JSGlobalObject.zig").JSGlobalObject;
pub const JSInternalPromise = @import("./JSInternalPromise.zig").JSInternalPromise;
pub const JSMap = @import("./JSMap.zig").JSMap;
pub const JSModuleLoader = @import("./JSModuleLoader.zig").JSModuleLoader;
pub const JSObject = @import("./JSObject.zig").JSObject;
pub const JSPromise = @import("./JSPromise.zig").JSPromise;
pub const JSPromiseRejectionOperation = @import("./JSPromiseRejectionOperation.zig").JSPromiseRejectionOperation;
pub const JSRef = @import("./JSRef.zig").JSRef;
pub const JSString = @import("./JSString.zig").JSString;
pub const JSUint8Array = @import("./JSUint8Array.zig").JSUint8Array;
pub const JSBigInt = @import("./JSBigInt.zig").JSBigInt;
import RefString from "./RefString.zig";
pub const ScriptExecutionStatus = @import("./ScriptExecutionStatus.zig").ScriptExecutionStatus;
pub const SourceType = @import("./SourceType.zig").SourceType;
import Strong from "./Strong.zig";
pub const SystemError = @import("./SystemError.zig").SystemError;
pub const URL = @import("./URL.zig").URL;
pub const URLSearchParams = @import("./URLSearchParams.zig").URLSearchParams;
pub const VM = @import("./VM.zig").VM;
pub const Weak = @import("./Weak.zig").Weak;
pub const WeakRefType = @import("./Weak.zig").WeakRefType;
pub const Exception = @import("./Exception.zig").Exception;
pub const SourceProvider = @import("./SourceProvider.zig").SourceProvider;
pub const TopExceptionScope = @import("./TopExceptionScope.zig").TopExceptionScope;
pub const ExceptionValidationScope = @import("./TopExceptionScope.zig").ExceptionValidationScope;
pub const MarkedArgumentBuffer = @import("./MarkedArgumentBuffer.zig").MarkedArgumentBuffer;
pub const RegularExpression = @import("./RegularExpression.zig").RegularExpression;

// JavaScript-related
pub const Errorable = @import("./Errorable.zig").Errorable;
pub const ResolvedSource = @import("./ResolvedSource.zig").ResolvedSource;
pub const ErrorCode = @import("./ErrorCode.zig").ErrorCode;
pub const JSErrorCode = @import("./JSErrorCode.zig").JSErrorCode;
pub const ZigErrorType = @import("./ZigErrorType.zig").ZigErrorType;
import Debugger from "./Debugger.zig";
import SavedSourceMap from "./SavedSourceMap.zig";
import VirtualMachine from "./VirtualMachine.zig";
import ModuleLoader from "./ModuleLoader.zig";
import RareData from "./rare_data.zig";
pub const EventType = @import("./EventType.zig").EventType;
pub const JSRuntimeType = @import("./JSRuntimeType.zig").JSRuntimeType;
pub const ZigStackFrameCode = @import("./ZigStackFrameCode.zig").ZigStackFrameCode;

pub const ErrorableResolvedSource = Errorable(ResolvedSource);
pub const ErrorableZigString = Errorable(ZigString);
pub const ErrorableJSValue = Errorable(JSValue);
pub const ErrorableString = Errorable(bun.String);

pub const ZigStackTrace = @import("./ZigStackTrace.zig").ZigStackTrace;
pub const ZigStackFrame = @import("./ZigStackFrame.zig").ZigStackFrame;
pub const ZigStackFramePosition = @import("./ZigStackFramePosition.zig").ZigStackFramePosition;
pub const ZigException = @import("./ZigException.zig").ZigException;

import ConsoleObject from "./ConsoleObject.zig";
pub const Formatter = ConsoleObject.Formatter;

import hot_reloader from "./hot_reloader.zig";

// TODO: move into bun.api
import Jest from "../test_runner/jest.zig";
pub const TestScope = @import("../test_runner/jest.zig").TestScope;
import Expect from "../test_runner/expect.zig";
import Snapshot from "../test_runner/snapshot.zig";

import js_property_iterator from "./JSPropertyIterator.zig";
pub const JSPropertyIterator = js_property_iterator.JSPropertyIterator;
pub const JSPropertyIteratorOptions = js_property_iterator.JSPropertyIteratorOptions;

import EventLoop from "./event_loop.zig";
pub const AbstractVM = EventLoop.AbstractVM;
pub const AnyEventLoop = EventLoop.AnyEventLoop;
pub const AnyTask = EventLoop.AnyTask;
pub const AnyTaskWithExtraContext = EventLoop.AnyTaskWithExtraContext;
pub const ConcurrentCppTask = EventLoop.ConcurrentCppTask;
pub const ConcurrentPromiseTask = EventLoop.ConcurrentPromiseTask;
pub const ConcurrentTask = EventLoop.ConcurrentTask;
pub const CppTask = EventLoop.CppTask;
pub const DeferredTaskQueue = EventLoop.DeferredTaskQueue;
pub const EventLoopHandle = EventLoop.EventLoopHandle;
pub const EventLoopKind = EventLoop.EventLoopKind;
pub const EventLoopTask = EventLoop.EventLoopTask;
pub const EventLoopTaskPtr = EventLoop.EventLoopTaskPtr;
pub const GarbageCollectionController = EventLoop.GarbageCollectionController;
pub const JsVM = EventLoop.JsVM;
pub const ManagedTask = EventLoop.ManagedTask;
pub const MiniEventLoop = EventLoop.MiniEventLoop;
pub const MiniVM = EventLoop.MiniVM;
pub const PlatformEventLoop = if (bun.Environment.isPosix) bun.uws.Loop else bun.Async.Loop;
pub const PosixSignalHandle = EventLoop.PosixSignalHandle;
pub const PosixSignalTask = EventLoop.PosixSignalTask;
pub const Task = EventLoop.Task;
pub const WorkPool = EventLoop.WorkPool;
pub const WorkPoolTask = EventLoop.WorkPoolTask;
pub const WorkTask = EventLoop.WorkTask;

/// Deprecated: Avoid using this in new code.
import C from "./javascript_core_c_api.zig";
/// Deprecated: Remove all of these please.
import Sizes from "./sizes.zig";
/// Deprecated: Use `bun.String`
pub const ZigString = @import("./ZigString.zig").ZigString;
/// Deprecated: Use `bun.webcore`
pub const WebCore = bun.webcore;
/// Deprecated: Use `bun.api`
pub const API = bun.api;
/// Deprecated: Use `bun.api.node`
pub const Node = bun.api.node;

const log = bun.Output.scoped(.JSC, .hidden);
pub inline fn markBinding(src: std.builtin.SourceLocation) {
    log("{s} ({s}:{d})", .{ src.fn_name, src.file, src.line });
}
pub inline fn markMemberBinding(comptime cls: auto, src: std.builtin.SourceLocation) {
    if (!bun.Environment.enable_logs) return;
    const classname = if (@typeInfo(@TypeOf(cls)) == .pointer) cls else @typeName(cls);
    log("{s}.{s} ({s}:{d})", .{ classname, src.fn_name, src.file, src.line });
}

pub const Subprocess = bun.api.Subprocess;

/// This file is generated by:
///  1. `bun src/codegen/generate-classes.ts`
///  2. Scan for **/*.classes.ts files in src/
///  3. Generate a JS wrapper for each class in:
///     - Zig: generated_classes.zig
///     - C++: ZigGeneratedClasses.h, ZigGeneratedClasses.cpp
///  4. For the Zig code to successfully compile:
///     - Add it to generated_classes_list.zig
///     - Expose the generated methods:
///       ```zig
///       pub const js = JSC.Codegen.JSMyClassName;
///       pub const toJS = js.toJS;
///       pub const fromJS = js.fromJS;
///       pub const fromJSDirect = js.fromJSDirect;
///       ```
///  5. `bun run build`
///
import Codegen from "ZigGeneratedClasses";
pub const GeneratedClassesList = @import("./generated_classes_list.zig").Classes;

pub const RuntimeTranspilerCache = @import("./RuntimeTranspilerCache.zig").RuntimeTranspilerCache;

/// Track whether an object should keep the event loop alive
pub struct Ref {
    has: bool = false,

    static fn init(): Ref {
        return {};
    }

    fn unref(*_, vm: *VirtualMachine) {
        if (!_.has)
            return;
        _.has = false;
        vm.active_tasks -= 1;
    }

    fn ref(*_, vm: *VirtualMachine) {
        if (_.has)
            return;
        _.has = true;
        vm.active_tasks += 1;
    }
}

pub const OpaqueCallback = *const fn (current: *!?) callconv(.c) void;
pub fn OpaqueWrap(comptime Context: type, comptime Function: auto): OpaqueCallback {
    return struct {
        @callconv("c")
        pub fn callback(ctx: *!?) {
            const context: *Context = @ptrCast(@alignCast(ctx.?));
            Function(context);
        }
    }.callback;
}

pub const Error = @import("ErrorCode").Error;

/// According to https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Date,
/// maximum Date in JavaScript is less than Number.MAX_SAFE_INTEGER (u52).
pub const init_timestamp = std.math.maxInt(JSTimeType);
pub const JSTimeType = u52;
pub fn toJSTime(sec: isize, nsec: isize): JSTimeType {
    const millisec = @as(u64, @intCast(@divTrunc(nsec, std.time.ns_per_ms)));
    return @as(JSTimeType, @truncate(@as(u64, @intCast(sec * std.time.ms_per_s)) + millisec));
}

pub const MAX_SAFE_INTEGER = 9007199254740991;
pub const MIN_SAFE_INTEGER = -9007199254740991;

const JSCInitializeCallback = *const fn ([*]const u8, len: usize) callconv(.c) void;

@c {
    fn JSCInitialize(env: [*]const [*:0]u8, count: usize, cb: JSCInitializeCallback, eval_mode: bool);
    fn Bun__JSC__operationMathPow(a: f64, b: f64): f64;
}

@callconv("c")
fn onJSCInvalidEnvVar(name: [*]const u8, len: usize) {
    bun.Output.errGeneric(
        \\invalid JSC environment variable
        \\
        \\    <b>{s}<r>
        \\
        \\For a list of options, see this file:
        \\
        \\    https://github.com/oven-sh/webkit/blob/main/Source/JavaScriptCore/runtime/OptionsList.h
        \\
        \\Environment variables must be prefixed with "BUN_JSC_". This code runs before .env files are loaded, so those won't work here.
        \\
        \\Warning: options change between releases of Bun and WebKit without notice. This is not a stable API, you should not rely on it beyond debugging something, and it may be removed entirely in a future version of Bun.
    ,
        .{ name[0..len] },
    );
    bun.Global.exit(1);
}

pub struct math {
    static fn pow(x: f64, y: f64): f64 {
        return Bun__JSC__operationMathPow(x, y);
    }
}

import generated from "bindgen_generated";

import bun from "bun";
