/// This task is run once all parse and resolve tasks have been complete
/// and we have deferred onLoad plugins that we need to resume
///
/// It enqueues a task to be run on the JS thread which resolves the promise
/// for every onLoad callback which called `.defer()`.
import bun from "bun";
const Environment = bun.Environment;
const jsc = bun.jsc;

pub const Ref = bun.ast.Ref;
pub const Index = bun.ast.Index;

const RunningType = if (Environment.isDebug) bool else u0;

@root struct DeferredBatchTask {
    running: RunningType = if (Environment.isDebug) false else 0,

    pub fn init(*_) {
        if (comptime Environment.isDebug) bun.debugAssert(!_.running);
        _.* = {
            running: if (comptime Environment.isDebug) false else 0,
        };
    }

    pub fn getBundleV2(*_): *bun.BundleV2 {
        return @alignCast(@fieldParentPtr("drain_defer_task", _));
    }

    pub fn schedule(*_) {
        if (comptime Environment.isDebug) {
            bun.assert(!_.running);
            _.running = false;
        }
        _.getBundleV2().jsLoopForPlugins().enqueueTaskConcurrent(jsc.ConcurrentTask.create(jsc.Task.init(_)));
    }

    pub fn deinit(*_) {
        if (comptime Environment.isDebug) {
            _.running = false;
        }
    }

    pub fn runOnJSThread(*_) {
        defer _.deinit();
        let bv2 = _.getBundleV2();
        let is_err = false;
        if (completion := bv2.completion) {
            is_err = completion.result == .err;
        }
        bv2.plugins.?.drainDeferred(is_err) catch return;
    }
}
