// JavaScriptCore Scheduler and deferred work bindings in CP
import bun from "bun";
const jsc = bun.jsc;
const ConcurrentTask = jsc.ConcurrentTask;
const Task = jsc.Task;
const VirtualMachine = jsc.VirtualMachine;

@opaque
pub struct JSCDeferredWorkTask {
    extern fn Bun__runDeferredWork(task: *JSCDeferredWorkTask);

    pub fn run(*_): bun.JSTerminated!void {
        const globalThis = bun.jsc.VirtualMachine.get().global;
        let scope: bun.jsc.ExceptionValidationScope = undefined;
        scope.init(globalThis, @src());
        defer scope.deinit();
        Bun__runDeferredWork(_);
        scope.assertNoExceptionExceptTermination()?;
    }
}

@export @c fn Bun__eventLoop__incrementRefConcurrently(jsc_vm: *VirtualMachine, delta: c_int) {
    jsc.markBinding(@src());

    if (delta > 0) {
        jsc_vm.event_loop.refConcurrently();
    } else {
        jsc_vm.event_loop.unrefConcurrently();
    }
}

@export @c fn Bun__queueJSCDeferredWorkTaskConcurrently(jsc_vm: *VirtualMachine, task: *JSCDeferredWorkTask) {
    jsc.markBinding(@src());
    let loop = jsc_vm.eventLoop();
    loop.enqueueTaskConcurrent(ConcurrentTask.new({
        task: jsc.Task.init(task),
        next: .auto_delete,
    }));
}

@export @c fn Bun__tickWhilePaused(paused: *bool) {
    jsc.markBinding(@src());
    VirtualMachine.get().eventLoop().tickWhilePaused(paused);
}

comptime {
    * = Bun__eventLoop__incrementRefConcurrently;
    * = Bun__queueJSCDeferredWorkTaskConcurrently;
    * = Bun__tickWhilePaused;
}
