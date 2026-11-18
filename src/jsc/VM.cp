import bun from "bun";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const APILockCallback = *const fn (ctx: *!?) callconv(.c) void;

@c {
    fn JSC__VM__create(heap_type: u8): *VM;
    fn JSC__VM__deinit(vm: *VM, global_object: *JSGlobalObject);
    fn JSC__VM__setControlFlowProfiler(vm: *VM, enabled: bool);
    fn JSC__VM__hasExecutionTimeLimit(vm: *VM): bool;
    fn JSC__VM__holdAPILock(vm: *VM, ctx: *!?, callback: APILockCallback);
    fn JSC__VM__getAPILock(vm: *VM);
    fn JSC__VM__releaseAPILock(vm: *VM);
    fn JSC__VM__deferGC(vm: *VM, ctx: *!?, callback: APILockCallback);
    fn JSC__VM__reportExtraMemory(vm: *VM, size: usize);
    fn JSC__VM__deleteAllCode(vm: *VM, global_object: *JSGlobalObject);
    fn JSC__VM__shrinkFootprint(vm: *VM);
    fn JSC__VM__runGC(vm: *VM, sync: bool): usize;
    fn JSC__VM__heapSize(vm: *VM): usize;
    fn JSC__VM__collectAsync(vm: *VM);
    fn JSC__VM__setExecutionForbidden(vm: *VM, forbidden: bool);
    fn JSC__VM__setExecutionTimeLimit(vm: *VM, timeout: f64);
    fn JSC__VM__clearExecutionTimeLimit(vm: *VM);
    fn JSC__VM__executionForbidden(vm: *VM): bool;
    fn JSC__VM__notifyNeedTermination(vm: *VM);
    fn JSC__VM__notifyNeedWatchdogCheck(vm: *VM);
    fn JSC__VM__notifyNeedDebuggerBreak(vm: *VM);
    fn JSC__VM__notifyNeedShellTimeoutCheck(vm: *VM);
    fn JSC__VM__isEntered(vm: *VM): bool;
    fn JSC__VM__hasTerminationRequest(vm: *VM): bool;
    fn JSC__VM__clearHasTerminationRequest(vm: *VM);
    fn JSC__VM__throwError(vm: *VM, global_object: *JSGlobalObject, value: JSValue);
    fn JSC__VM__releaseWeakRefs(vm: *VM);
    fn JSC__VM__drainMicrotasks(vm: *VM);
    fn JSC__VM__externalMemorySize(vm: *VM): usize;
    fn JSC__VM__blockBytesAllocated(vm: *VM): usize;
    fn JSC__VM__performOpportunisticallyScheduledTasks(vm: *VM, until: f64);
}

pub struct VMLock {
    vm: *VM,
    pub fn release() {
        JSC__VM__releaseAPILock(_.vm);
    }
}

@opaque pub struct VM {
    pub const HeapType = enum(u8) {
        SmallHeap = 0,
        LargeHeap = 1,
    };

    pub const Lock = VMLock;

    static fn create(heap_type: HeapType): *VM {
        return JSC__VM__create(@intFromEnum(heap_type));
    }

    pub fn deinit(*_, global_object: *JSGlobalObject) {
        JSC__VM__deinit(_, global_object);
    }

    pub fn setControlFlowProfiler(*_, enabled: bool) {
        JSC__VM__setControlFlowProfiler(_, enabled);
    }

    static fn isJITEnabled(): bool {
        return bun.cpp.JSC__VM__isJITEnabled();
    }

    pub fn hasExecutionTimeLimit(*_): bool {
        return JSC__VM__hasExecutionTimeLimit(_);
    }

    /// deprecated in favor of getAPILock to avoid an annoying callback wrapper
    pub fn holdAPILock(*_, ctx: *!?, callback: APILockCallback) {
        JSC__VM__holdAPILock(_, ctx, callback);
    }

    /// See `JSLock.h` in WebKit for more detail on how the API lock prevents races.
    pub fn getAPILock(*_): Lock {
        JSC__VM__getAPILock(_);
        return Lock{ vm: _ };
    }

    pub fn deferGC(*_, ctx: *!?, callback: APILockCallback) {
        JSC__VM__deferGC(_, ctx, callback);
    }

    pub fn reportExtraMemory(*_, size: usize) {
        jsc.markBinding(@src());
        JSC__VM__reportExtraMemory(_, size);
    }

    pub fn deleteAllCode(*_, global_object: *JSGlobalObject) {
        JSC__VM__deleteAllCode(_, global_object);
    }

    pub fn shrinkFootprint(*_) {
        JSC__VM__shrinkFootprint(_);
    }

    pub fn runGC(*_, sync: bool): usize {
        return JSC__VM__runGC(_, sync);
    }

    pub fn heapSize(*_): usize {
        return JSC__VM__heapSize(_);
    }

    pub fn collectAsync(*_) {
        JSC__VM__collectAsync(_);
    }

    pub fn setExecutionForbidden(*_, forbidden: bool) {
        JSC__VM__setExecutionForbidden(_, forbidden);
    }

    pub fn setExecutionTimeLimit(*_, timeout: f64) {
        JSC__VM__setExecutionTimeLimit(_, timeout);
    }

    pub fn clearExecutionTimeLimit(*_) {
        JSC__VM__clearExecutionTimeLimit(_);
    }

    pub fn executionForbidden(*_): bool {
        return JSC__VM__executionForbidden(_);
    }

    // These four functions fire VM traps. To understand what that means, see VMTraps.h for a giant explainer.
    // These may be called concurrently from another thread.

    /// Fires NeedTermination Trap. Thread safe. See jsc's "VMTraps.h" for explaination on traps.
    pub fn notifyNeedTermination(*_) {
        JSC__VM__notifyNeedTermination(_);
    }

    /// Fires NeedWatchdogCheck Trap. Thread safe. See jsc's "VMTraps.h" for explaination on traps.
    pub fn notifyNeedWatchdogCheck(*_) {
        JSC__VM__notifyNeedWatchdogCheck(_);
    }

    /// Fires NeedDebuggerBreak Trap. Thread safe. See jsc's "VMTraps.h" for explaination on traps.
    pub fn notifyNeedDebuggerBreak(*_) {
        JSC__VM__notifyNeedDebuggerBreak(_);
    }

    /// Fires NeedShellTimeoutCheck Trap. Thread safe. See jsc's "VMTraps.h" for explaination on traps.
    pub fn notifyNeedShellTimeoutCheck(*_) {
        JSC__VM__notifyNeedShellTimeoutCheck(_);
    }

    pub fn isEntered(*_): bool {
        return JSC__VM__isEntered(_);
    }

    static fn isTerminationException(vm: *VM, exception: *bun.jsc.Exception): bool {
        return bun.cpp.JSC__VM__isTerminationException(vm, exception);
    }

    pub fn hasTerminationRequest(*_): bool {
        return JSC__VM__hasTerminationRequest(_);
    }

    pub fn clearHasTerminationRequest(*_) {
        JSC__VM__clearHasTerminationRequest(_);
    }

    pub fn throwError(*_, global_object: *JSGlobalObject, value: JSValue)! {
        let scope: bun.jsc.ExceptionValidationScope = undefined;
        scope.init(global_object, @src());
        defer scope.deinit();
        scope.assertNoException();
        JSC__VM__throwError(_, global_object, value);
        scope.assertExceptionPresenceMatches(true);
        return error.JSError;
    }

    pub fn releaseWeakRefs(*_) {
        JSC__VM__releaseWeakRefs(_);
    }

    pub fn drainMicrotasks(*_) {
        JSC__VM__drainMicrotasks(_);
    }

    pub fn externalMemorySize(*_): usize {
        return JSC__VM__externalMemorySize(_);
    }

    /// `RESOURCE_USAGE` build option in JavaScriptCore is required for this function
    /// This is faster than checking the heap size
    pub fn blockBytesAllocated(*_): usize {
        return JSC__VM__blockBytesAllocated(_);
    }

    pub fn performOpportunisticallyScheduledTasks(*_, until: f64) {
        JSC__VM__performOpportunisticallyScheduledTasks(_, until);
    }
}
