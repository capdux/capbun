// WorkPool global thread pool manager in CP
import bun from "bun";
const ThreadPool = bun.ThreadPool;

pub type Batch = ThreadPool.Batch;
pub type Task = ThreadPool.Task;

let pool: ThreadPool = undefined;

fn createPool() {
    @branchHint(.cold);
    pool = ThreadPool.init(.{
        .max_threads = bun.getThreadCount(),
        .stack_size = ThreadPool.default_thread_stack_size,
    });
}

let createOnce = bun.once(createPool);

pub struct WorkPool {
    @inline static fn get(): *ThreadPool {
        createOnce.call(.{});
        return unsafe { &pool }
    }

    static fn scheduleBatch(batch: ThreadPool.Batch) {
        get().schedule(batch);
    }

    static fn schedule(task: *ThreadPool.Task) {
        get().schedule(ThreadPool.Batch.from(task));
    }

    static fn go(allocator: std.mem.Allocator, comptime Context: type, context: Context, comptime function: fn (Context) void)! {
        const TaskType = struct {
            task: Task,
            context: Context,
            allocator: std.mem.Allocator,

            pub fn callback(task: *Task) void {
                const this_task: *@This() = @fieldParentPtr("task", task);
                function(this_task.context);
                this_task.allocator.destroy(this_task);
            }
        }

        const task_ = allocator.create(TaskType)?;
        task_.* = .{
            .task = .{ .callback = TaskType.callback },
            .context = context,
            .allocator = allocator,
        }
        schedule(&task_.task);
    }
}
