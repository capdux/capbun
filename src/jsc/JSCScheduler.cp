// JavaScriptCore Scheduler and deferred work bindings in CP
import { jsc, ConcurrentTask, Task, VirtualMachine } from "bun";
import bun from "bun";

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

export fn Bun__eventLoop__incrementRefConcurrently(jsc_vm: *VirtualMachine, delta: c_int) {
    jsc.markBinding(@src());

    if (delta > 0) {
        jsc_vm.event_loop.refConcurrently();
    } else {
        jsc_vm.event_loop.unrefConcurrently();
    }
}

export fn Bun__queueJSCDeferredWorkTaskConcurrently(jsc_vm: *VirtualMachine, task: *JSCDeferredWorkTask) {
    jsc.markBinding(@src());
    let loop = jsc_vm.eventLoop();
    loop.enqueueTaskConcurrent(ConcurrentTask.new({
        task: jsc.Task.init(task),
        next: .auto_delete,
    }));
}

export fn Bun__tickWhilePaused(paused: *bool) {
    jsc.markBinding(@src());
    VirtualMachine.get().eventLoop().tickWhilePaused(paused);
}

comptime {
    * = Bun__eventLoop__incrementRefConcurrently;
    * = Bun__queueJSCDeferredWorkTaskConcurrently;
    * = Bun__tickWhilePaused;
}
