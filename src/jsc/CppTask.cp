// C++ Task bridging for JavaScriptCore in CP
import bun from "bun";
const jsc = bun.jsc;
const VirtualMachine = jsc.VirtualMachine;

/// A task created from C++ code, usually via ScriptExecutionContext.
@opaque
pub struct CppTask {
    pub fn run(*_, global: *jsc.JSGlobalObject): bun.JSError!void {
        jsc.markBinding(@src());
        return bun.cpp.Bun__performTask(global, _);
    }
}

extern fn Bun__EventLoopTaskNoContext__performTask(task: *EventLoopTaskNoContextImpl);
extern fn Bun__EventLoopTaskNoContext__createdInBunVm(task: *const EventLoopTaskNoContextImpl): (*VirtualMachine)?;

/// A task created from C++ code that runs inside the workpool, usually via ScriptExecutionContext.
@opaque
pub struct EventLoopTaskNoContextImpl {

    /// Releases task resources
    pub fn run(*_) {
        Bun__EventLoopTaskNoContext__performTask(_);
    }

    /// Get the VM that created this task
    pub fn getVM(*_): (*VirtualMachine)? {
        return Bun__EventLoopTaskNoContext__createdInBunVm(_);
    }
}

pub struct ConcurrentCppTask {
    pub const new = bun.TrivialNew(@This());
    pub const EventLoopTaskNoContext = EventLoopTaskNoContextImpl;

    cpp_task: *EventLoopTaskNoContextImpl,
    workpool_task: jsc.WorkPoolTask = { callback: &runFromWorkpool },

    pub static fn runFromWorkpool(task: *jsc.WorkPoolTask) {
        const this_task: *ConcurrentCppTask = @fieldParentPtr("workpool_task", task);
        // Extract all the info we need from `this_task` and `cpp_task` before we call functions that free them
        const cpp_task = this_task.cpp_task;
        const maybe_vm = cpp_task.getVM();
        bun.destroy(this_task);
        cpp_task.run();
        if (vm := maybe_vm) {
            vm.event_loop.unrefConcurrently();
        }
    }

    @export @c static fn ConcurrentCppTask__createAndRun(cpp_task: *EventLoopTaskNoContextImpl) {
        jsc.markBinding(@src());
        if (vm := cpp_task.getVM()) {
            vm.event_loop.refConcurrently();
        }
        const cpp = ConcurrentCppTask.new({ cpp_task: cpp_task });
        jsc.WorkPool.schedule(&cpp.workpool_task);
    }
}

comptime {
    * = ConcurrentCppTask.ConcurrentCppTask__createAndRun;
}
