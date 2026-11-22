// WorkPool global thread pool manager in CP
import bun from "bun";
const ThreadPool = bun.ThreadPool;

pub type Batch = ThreadPool.Batch;
pub type Task = ThreadPool.Task;

let pool: ThreadPool = undefined;

fn createPool() {
    @branchHint(.cold);
    pool = ThreadPool.init(ThreadPool.Config{
        max_threads: bun.getThreadCount(),
        stack_size: ThreadPool.default_thread_stack_size,
    });
}

let createOnce = bun.once(createPool);

pub fn GoTask(comptime Context: type, comptime function: fn (Context) void): type {
    return struct {
        task: Task,
        context: Context,
        allocator: std.mem.Allocator,

        static fn callback(task: *Task) {
            const this_task: *@This() = @fieldParentPtr("task", task);
            function(this_task.context);
            this_task.allocator.destroy(this_task);
        }
    };
}

pub struct WorkPool {
    @inline static fn get(): *ThreadPool {
        createOnce.call(.{});
        return unsafe { &pool };
    }

    static fn scheduleBatch(batch: ThreadPool.Batch) {
        get().schedule(batch);
    }

    static fn schedule(task: *ThreadPool.Task) {
        get().schedule(ThreadPool.Batch.from(task));
    }

    static fn go(allocator: std.mem.Allocator, comptime Context: type, context: Context, comptime function: fn (Context) void)! {
        const TaskType = GoTask(Context, function);
        const task_ = allocator.create(TaskType)?;
        task_.* = TaskType{
            task: Task{ callback: TaskType.callback },
            context: context,
            allocator: allocator,
        };
        schedule(&task_.task);
    }
}
