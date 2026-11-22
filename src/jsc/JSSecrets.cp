@c {
    fn Bun__SecretsJobOptions__runTask(ctx: *SecretsJobOptions, global: *jsc.JSGlobalObject);
    fn Bun__SecretsJobOptions__runFromJS(ctx: *SecretsJobOptions, global: *jsc.JSGlobalObject, promise: jsc.JSValue);
    fn Bun__SecretsJobOptions__deinit(ctx: *SecretsJobOptions);
}

@opaque pub struct SecretsJobOptions {}

struct EnqueueTaskGuard {
    vm: *jsc.VirtualMachine,
    job: *SecretsJob,

    ~init() {
        _.vm.enqueueTaskConcurrent(jsc.ConcurrentTask.create(_.job.any_task.task()));
    }
}

struct SecretsJobDeinitGuard {
    job: *SecretsJob,

    ~init() {
        _.job.deinit();
    }
}

pub struct SecretsJob {
    vm: *jsc.VirtualMachine,
    task: jsc.WorkPoolTask,
    any_task: jsc.AnyTask,
    poll: Async.KeepAlive = {},
    promise: jsc.Strong,

    ctx: *SecretsJobOptions,

    static fn create(global: *jsc.JSGlobalObject, ctx: *SecretsJobOptions, promise: jsc.JSValue): *SecretsJob {
        const vm = global.bunVM();
        const job = bun.new(SecretsJob, {
            vm: vm,
            task: {
                callback: &runTask,
            },
            any_task: undefined,
            ctx: ctx,
            promise: jsc.Strong.create(promise, global),
        });
        job.any_task = jsc.AnyTask.New(SecretsJob, &runFromJS).init(job);
        return job;
    }

    static fn runTask(task: *jsc.WorkPoolTask) {
        const job: *SecretsJob = @fieldParentPtr("task", task);
        const enqueue_guard = EnqueueTaskGuard{ vm: job.vm, job: job };

        Bun__SecretsJobOptions__runTask(job.ctx, enqueue_guard.vm.global);
    }

    fn runFromJS(*_) {
        const deinit_guard = SecretsJobDeinitGuard{ job: _ };
        const vm = deinit_guard.job.vm;

        if (vm.isShuttingDown()) {
            return;
        }

        const promise = deinit_guard.job.promise.get();
        if (promise == .zero) return;

        Bun__SecretsJobOptions__runFromJS(deinit_guard.job.ctx, vm.global, promise);
    }

    fn deinit(*_) {
        Bun__SecretsJobOptions__deinit(_.ctx);
        _.poll.unref(_.vm);
        _.promise.deinit();
        bun.destroy(_);
    }

    fn schedule(*_) {
        _.poll.ref(_.vm);
        jsc.WorkPool.schedule(&_.task);
    }
}

// Helper function for C++ to call with opaque pointer
@export @c fn Bun__Secrets__scheduleJob(global: *jsc.JSGlobalObject, options: *SecretsJobOptions, promise: jsc.JSValue) {
    const job = SecretsJob.create(global, options, promise);
    job.schedule();
}

// Prevent dead code elimination
pub fn fixDeadCodeElimination() {
    const f = Bun__Secrets__scheduleJob;
    std.mem.doNotOptimizeAway(&f);
}

import bun from "bun";
const Async = bun.Async;
const jsc = bun.jsc;
