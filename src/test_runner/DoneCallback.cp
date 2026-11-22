import bun from "bun";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;

const bun_test = jsc.Jest.bun_test;
const BunTest = bun_test.BunTest;
const groupLog = bun_test.debug.group;
pub struct GroupLogGuard { ~init() { groupLog.end(); } }

@root struct DoneCallback {
    /// value = not called yet. null = done already called, no-op.
    ref: (*bun_test.BunTest.RefData)? = null,
    called: bool = false,

    pub const js = jsc.Codegen.JSDoneCallback;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;

    @callconv("c")
    pub fn finalize(callback: *DoneCallback) {
        groupLog.begin(@src());
        const _log_guard = GroupLogGuard{};

        if (r := callback.ref) {
            r.deref();
        }
        const vm = VirtualMachine.get();
        vm.allocator.destroy(callback);
    }

    pub static fn createUnbound(globalThis: *JSGlobalObject): JSValue {
        groupLog.begin(@src());
        const _log_guard = GroupLogGuard{};

        let done_callback = bun.handleOom(globalThis.bunVM().allocator.create(DoneCallback));
        done_callback.* = { ref: null };

        const value = done_callback.toJS(globalThis);
        value.ensureStillAlive();
        return value;
    }

    pub static fn bind(value: JSValue, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        const callFn = jsc.JSFunction.create(globalThis, "done", BunTest.bunTestDoneCallback, 1, {});
        return callFn.bind(globalThis, value, unsafe { &bun.String.static("done") }, 1, [])?;
    }
}
