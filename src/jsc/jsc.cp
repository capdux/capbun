/// Bindings to JavaScriptCore and other JavaScript primatives such as
/// VirtualMachine, JSGlobalObject (Zig::GlobalObject), and the event loop.

/// The calling convention used for JavaScript functions <> Native
pub const conv: std.builtin.CallingConvention = if (bun.Environment.isWindows && bun.Environment.isX64)
    .{ x86_64_sysv: .{} }
else
    .c;

/// Web Template Framework
import _WTFMod from "./WTF.zig";
pub const wtf = _WTFMod.WTF;

/// Binding for JSCInitialize in ZigGlobalObject.cpp
pub fn initialize(eval_mode: bool) {
    markBinding(@src());
    bun.analytics.Features.jsc += 1;
    JSCInitialize(std.os.environ.ptr, std.os.environ.len, onJSCInvalidEnvVar, eval_mode);
}

import _JSValueMod from "./JSValue.zig";
pub const JSValue = _JSValueMod.JSValue;

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
import _AnyPromiseMod from "./AnyPromise.zig";
pub const AnyPromise = _AnyPromiseMod.AnyPromise;
import array_buffer from "./array_buffer.zig";
pub const ArrayBuffer = array_buffer.ArrayBuffer;
pub const MarkedArrayBuffer = array_buffer.MarkedArrayBuffer;
pub const JSCArrayBuffer = array_buffer.JSCArrayBuffer;
import _CachedBytecodeMod from "./CachedBytecode.zig";
import _CallFrameMod from "./CallFrame.zig";
import _CommonAbortReasonMod from "./CommonAbortReason.zig";
import _CommonStringsMod from "./CommonStrings.zig";
import _CustomGetterSetterMod from "./CustomGetterSetter.zig";
import _DOMFormDataMod from "./DOMFormData.zig";
import _DOMURLMod from "./DOMURL.zig";
import _DecodedJSValueMod from "./DecodedJSValue.zig";
import _DeferredErrorMod from "./DeferredError.zig";
import _GetterSetterMod from "./GetterSetter.zig";
import _JSArrayMod from "./JSArray.zig";
import _JSArrayIteratorMod from "./JSArrayIterator.zig";
import _JSCellMod from "./JSCell.zig";
import _JSFunctionMod from "./JSFunction.zig";
import _JSGlobalObjectMod from "./JSGlobalObject.zig";
import _JSInternalPromiseMod from "./JSInternalPromise.zig";
import _JSMapMod from "./JSMap.zig";
import _JSModuleLoaderMod from "./JSModuleLoader.zig";
import _JSObjectMod from "./JSObject.zig";
import _JSPromiseMod from "./JSPromise.zig";
import _JSPromiseRejectionOperationMod from "./JSPromiseRejectionOperation.zig";
import _JSRefMod from "./JSRef.zig";
import _JSStringMod from "./JSString.zig";
import _JSUint8ArrayMod from "./JSUint8Array.zig";
import _JSBigIntMod from "./JSBigInt.zig";
import RefString from "./RefString.zig";
import _ScriptExecutionStatusMod from "./ScriptExecutionStatus.zig";
import _SourceTypeMod from "./SourceType.zig";
import Strong from "./Strong.zig";
import _SystemErrorMod from "./SystemError.zig";
import _URLMod from "./URL.zig";
import _URLSearchParamsMod from "./URLSearchParams.zig";
import _VMMod from "./VM.zig";
import _WeakMod from "./Weak.zig";
import _ExceptionMod from "./Exception.zig";
import _SourceProviderMod from "./SourceProvider.zig";
import _TopExceptionScopeMod from "./TopExceptionScope.zig";
import _MarkedArgumentBufferMod from "./MarkedArgumentBuffer.zig";
import _RegularExpressionMod from "./RegularExpression.zig";

pub const CachedBytecode = _CachedBytecodeMod.CachedBytecode;
pub const CallFrame = _CallFrameMod.CallFrame;
pub const CommonAbortReason = _CommonAbortReasonMod.CommonAbortReason;
pub const CommonStrings = _CommonStringsMod.CommonStrings;
pub const CustomGetterSetter = _CustomGetterSetterMod.CustomGetterSetter;
pub const DOMFormData = _DOMFormDataMod.DOMFormData;
pub const DOMURL = _DOMURLMod.DOMURL;
pub const DecodedJSValue = _DecodedJSValueMod.DecodedJSValue;
pub const DeferredError = _DeferredErrorMod.DeferredError;
pub const GetterSetter = _GetterSetterMod.GetterSetter;
pub const JSArray = _JSArrayMod.JSArray;
pub const JSArrayIterator = _JSArrayIteratorMod.JSArrayIterator;
pub const JSCell = _JSCellMod.JSCell;
pub const JSFunction = _JSFunctionMod.JSFunction;
pub const JSGlobalObject = _JSGlobalObjectMod.JSGlobalObject;
pub const JSInternalPromise = _JSInternalPromiseMod.JSInternalPromise;
pub const JSMap = _JSMapMod.JSMap;
pub const JSModuleLoader = _JSModuleLoaderMod.JSModuleLoader;
pub const JSObject = _JSObjectMod.JSObject;
pub const JSPromise = _JSPromiseMod.JSPromise;
pub const JSPromiseRejectionOperation = _JSPromiseRejectionOperationMod.JSPromiseRejectionOperation;
pub const JSRef = _JSRefMod.JSRef;
pub const JSString = _JSStringMod.JSString;
pub const JSUint8Array = _JSUint8ArrayMod.JSUint8Array;
pub const JSBigInt = _JSBigIntMod.JSBigInt;
pub const ScriptExecutionStatus = _ScriptExecutionStatusMod.ScriptExecutionStatus;
pub const SourceType = _SourceTypeMod.SourceType;
pub const SystemError = _SystemErrorMod.SystemError;
pub const URL = _URLMod.URL;
pub const URLSearchParams = _URLSearchParamsMod.URLSearchParams;
pub const VM = _VMMod.VM;
pub const Weak = _WeakMod.Weak;
pub const WeakRefType = _WeakMod.WeakRefType;
pub const Exception = _ExceptionMod.Exception;
pub const SourceProvider = _SourceProviderMod.SourceProvider;
pub const TopExceptionScope = _TopExceptionScopeMod.TopExceptionScope;
pub const ExceptionValidationScope = _TopExceptionScopeMod.ExceptionValidationScope;
pub const MarkedArgumentBuffer = _MarkedArgumentBufferMod.MarkedArgumentBuffer;
pub const RegularExpression = _RegularExpressionMod.RegularExpression;

// JavaScript-related
import _ErrorableMod from "./Errorable.zig";
import _ResolvedSourceMod from "./ResolvedSource.zig";
import _ErrorCodeMod from "./ErrorCode.zig";
import _JSErrorCodeMod from "./JSErrorCode.zig";
import _ZigErrorTypeMod from "./ZigErrorType.zig";
import Debugger from "./Debugger.zig";
import SavedSourceMap from "./SavedSourceMap.zig";
import VirtualMachine from "./VirtualMachine.zig";
import ModuleLoader from "./ModuleLoader.zig";
import RareData from "./rare_data.zig";
import _EventTypeMod from "./EventType.zig";
import _JSRuntimeTypeMod from "./JSRuntimeType.zig";
import _ZigStackFrameCodeMod from "./ZigStackFrameCode.zig";
import _ZigStackTraceMod from "./ZigStackTrace.zig";
import _ZigStackFrameMod from "./ZigStackFrame.zig";
import _ZigStackFramePositionMod from "./ZigStackFramePosition.zig";
import _ZigExceptionMod from "./ZigException.zig";

pub const Errorable = _ErrorableMod.Errorable;
pub const ResolvedSource = _ResolvedSourceMod.ResolvedSource;
pub const ErrorCode = _ErrorCodeMod.ErrorCode;
pub const JSErrorCode = _JSErrorCodeMod.JSErrorCode;
pub const ZigErrorType = _ZigErrorTypeMod.ZigErrorType;
pub const EventType = _EventTypeMod.EventType;
pub const JSRuntimeType = _JSRuntimeTypeMod.JSRuntimeType;
pub const ZigStackFrameCode = _ZigStackFrameCodeMod.ZigStackFrameCode;

pub const ErrorableResolvedSource = Errorable(ResolvedSource);
pub const ErrorableZigString = Errorable(ZigString);
pub const ErrorableJSValue = Errorable(JSValue);
pub const ErrorableString = Errorable(bun.String);

pub const ZigStackTrace = _ZigStackTraceMod.ZigStackTrace;
pub const ZigStackFrame = _ZigStackFrameMod.ZigStackFrame;
pub const ZigStackFramePosition = _ZigStackFramePositionMod.ZigStackFramePosition;
pub const ZigException = _ZigExceptionMod.ZigException;

import ConsoleObject from "./ConsoleObject.zig";
pub const Formatter = ConsoleObject.Formatter;

import hot_reloader from "./hot_reloader.zig";

// TODO: move into bun.api
import Jest from "../test_runner/jest.zig";
pub const TestScope = Jest.TestScope;
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

import VmHandleMod from "./VmHandle.zig";
pub const VmHandle = VmHandleMod.VmHandle;
pub const Ticket = VmHandleMod.Ticket;
pub const VmLoopKind = VmHandleMod.LoopKind;
pub const Posted = VmHandleMod.Posted;
pub const ConcurrentPoster = VmHandleMod.ConcurrentPoster;
pub const IsolatedPosterInner = VmHandleMod.IsolatedPosterInner;

/// Deprecated: Avoid using this in new code.
import C from "./javascript_core_c_api.zig";
/// Deprecated: Remove all of these please.
import Sizes from "./sizes.zig";
/// Deprecated: Use `bun.String`
import _ZigStringMod from "./ZigString.zig";
pub const ZigString = _ZigStringMod.ZigString;
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
import _generated_classes_listMod from "./generated_classes_list.zig";
pub const GeneratedClassesList = _generated_classes_listMod.Classes;

import _RuntimeTranspilerCacheMod from "./RuntimeTranspilerCache.zig";
pub const RuntimeTranspilerCache = _RuntimeTranspilerCacheMod.RuntimeTranspilerCache;

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
        static fn callback(ctx: *!?) {
            const context: *Context = @ptrCast(@alignCast(ctx.?));
            Function(context);
        }
    }.callback;
}

import _ErrorCodePkgMod from "ErrorCode";
pub const Error = _ErrorCodePkgMod.Error;

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
        "invalid JSC environment variable\n\n    <b>{s}<r>\n\nFor a list of options, see this file:\n\n    https://github.com/oven-sh/webkit/blob/main/Source/JavaScriptCore/runtime/OptionsList.h\n\nEnvironment variables must be prefixed with \"BUN_JSC_\". This code runs before .env files are loaded, so those won't work here.\n\nWarning: options change between releases of Bun and WebKit without notice. This is not a stable API, you should not rely on it beyond debugging something, and it may be removed entirely in a future version of Bun.",
        .{ unsafe { name[0..len] } },
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
