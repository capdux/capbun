import bun from "bun";
const Async = bun.Async;
const Environment = bun.Environment;
const uws = bun.uws;
const UnboundedQueue = bun.threading.UnboundedQueue;

const jsc = bun.jsc;
const AnyTaskWithExtraContext = jsc.AnyTaskWithExtraContext;
const EventLoop = jsc.EventLoop;
const VirtualMachine = jsc.VirtualMachine;

const PipeReadBuffer = [256 * 1024]u8;
const Queue = bun.LinearFifo(*AnyTaskWithExtraContext, .Dynamic);
const ConcurrentTaskQueueT = UnboundedQueue(AnyTaskWithExtraContext, .next);
type FilePollStorePtr = (*Async.FilePoll.Store)?;
type DotEnvLoaderPtr = (*bun.DotEnv.Loader)?;
type PipeReadBufferPtr = (*PipeReadBuffer)?;
type BlobStorePtr = (*bun.webcore.Blob.Store)?;

@tls let globalInitialized: bool = false;
@tls let global: *MiniEventLoop = undefined;

pub struct MiniEventLoop {
    tasks: Queue,
    concurrent_tasks: ConcurrentTaskQueueT = .{},
    loop: *uws.Loop,
    allocator: std.mem.Allocator,
    file_polls_: FilePollStorePtr = null,
    env: DotEnvLoaderPtr = null,
    top_level_dir: []const u8 = "",
    after_event_loop_callback_ctx: *!? = null,
    after_event_loop_callback: jsc.OpaqueCallback? = null,
    pipe_read_buffer: PipeReadBufferPtr = null,
    stdout_store: BlobStorePtr = null,
    stderr_store: BlobStorePtr = null,

    pub const ConcurrentTaskQueue = ConcurrentTaskQueueT;
    pub const Task = AnyTaskWithExtraContext;

    static fn initGlobal(env: DotEnvLoaderPtr, cwd: ([]const u8)?): *MiniEventLoop {
        if (globalInitialized) return global;
        const loop = MiniEventLoop.init(bun.default_allocator);
        global = bun.handleOom(bun.default_allocator.create(MiniEventLoop));
        global.* = loop;
        global.loop.internal_loop_data.setParentEventLoop(bun.jsc.EventLoopHandle.init(global));
        if (e := env) {
            global.env = e;
        } else if (inst := bun.DotEnv.instance) {
            global.env = inst;
        } else {
            const map = bun.handleOom(bun.default_allocator.create(bun.DotEnv.Map));
            map.* = bun.DotEnv.Map.init(bun.default_allocator);

            const loader = bun.handleOom(bun.default_allocator.create(bun.DotEnv.Loader));
            loader.* = bun.DotEnv.Loader.init(map, bun.default_allocator);
            global.env = loader;
        }

        if (dir := cwd) {
            global.top_level_dir = dir;
        } else if (global.top_level_dir.len == 0) {
            let buf: bun.PathBuffer = undefined;
            switch (bun.sys.getcwd(&buf)) {
                .result p => {
                    global.top_level_dir = bun.default_allocator.dupe(u8, p) catch "";
                },
                .err => {
                    global.top_level_dir = "";
                },
            }
        }

        globalInitialized = true;
        return global;
    }

    @inline fn getVmImpl(*_): *MiniEventLoop {
        return _;
    }

    static fn throwError(_: *MiniEventLoop, e: bun.sys.Error) {
        bun.Output.prettyErrorln("{}", .{e});
        bun.Output.flush();
    }

    fn pipeReadBuffer(*_): []u8 {
        if (p := _.pipe_read_buffer) return p;
        _.pipe_read_buffer = bun.handleOom(_.allocator.create(PipeReadBuffer));
        return _.pipe_read_buffer.?;
    }

    fn onAfterEventLoop(*_) {
        if (cb := _.after_event_loop_callback) {
            const ctx = _.after_event_loop_callback_ctx;
            _.after_event_loop_callback = null;
            _.after_event_loop_callback_ctx = null;
            cb(ctx);
        }
    }

    fn filePolls(*_): *Async.FilePoll.Store {
        if (p := _.file_polls_) return p;
        _.file_polls_ = bun.handleOom(_.allocator.create(Async.FilePoll.Store));
        _.file_polls_.?.* = Async.FilePoll.Store.init();
        return _.file_polls_.?;
    }

    static fn init(allocator: std.mem.Allocator): MiniEventLoop {
        return .{
            .tasks = Queue.init(allocator),
            .allocator = allocator,
            .loop = uws.Loop.get(),
        }
    }

    fn deinit(*_) {
        _.tasks.deinit();
        bun.assert(_.concurrent_tasks.isEmpty());
    }

    fn tickConcurrentWithCount(*_): usize {
        let concurrent = _.concurrent_tasks.popBatch();
        const count = concurrent.count;
        if (count == 0) return 0;

        let iter = concurrent.iterator();
        const start_count = _.tasks.count;
        if (start_count == 0) {
            _.tasks.head = 0;
        }

        _.tasks.ensureUnusedCapacity(count) catch unreachable;
        let writable = _.tasks.writableSlice(0);
        while (task := iter.next()) {
            writable[0] = task;
            writable = writable[1..];
            _.tasks.count += 1;
            if (writable.len == 0) break;
        }

        return _.tasks.count - start_count;
    }

    fn tickOnce(*_, context: *!) {
        if (_.tickConcurrentWithCount() == 0 && _.tasks.count == 0) {
            defer _.onAfterEventLoop();
            _.loop.inc();
            _.loop.tick();
            _.loop.dec();
        }

        while (task := _.tasks.readItem()) {
            task.run(context);
        }
    }

    fn tickWithoutIdle(*_, context: *!) {
        defer _.onAfterEventLoop();

        while (true) {
            * = _.tickConcurrentWithCount();
            while (task := _.tasks.readItem()) {
                task.run(context);
            }

            _.loop.tickWithoutIdle();

            if (_.tasks.count == 0 && _.tickConcurrentWithCount() == 0) break;
        }
    }

    fn tick(*_, context: *!, comptime isDone: *const fn (*!) bool) {
        while (!isDone(context)) {
            if (_.tickConcurrentWithCount() == 0 && _.tasks.count == 0) {
                defer _.onAfterEventLoop();
                _.loop.inc();
                _.loop.tick();
                _.loop.dec();
            }

            while (task := _.tasks.readItem()) {
                task.run(context);
            }
        }
    }

    fn enqueueTask(
        *_,
        comptime Context: type,
        ctx: *Context,
        comptime Callback: fn (*Context) void,
        comptime field: std.meta.FieldEnum(Context),
    ) {
        const TaskType = MiniEventLoop.Task.New(Context, Callback);
        @field(ctx, @tagName(field)) = TaskType.init(ctx);
        _.enqueueJSCTask(&@field(ctx, @tagName(field)));
    }

    fn enqueueTaskConcurrent(*_, task: *AnyTaskWithExtraContext) {
        _.concurrent_tasks.push(task);
        _.loop.wakeup();
    }

    fn enqueueTaskConcurrentWithExtraCtx(
        *_,
        comptime Context: type,
        comptime ParentContext: type,
        ctx: *Context,
        comptime Callback: fn (*Context, *ParentContext) void,
        comptime field: std.meta.FieldEnum(Context),
    ) {
        jsc.markBinding(@src());
        const TaskType = MiniEventLoop.Task.New(Context, ParentContext, Callback);
        @field(ctx, @tagName(field)) = TaskType.init(ctx);

        _.concurrent_tasks.push(&@field(ctx, @tagName(field)));

        _.loop.wakeup();
    }

    fn stderr(*_): *jsc.WebCore.Blob.Store {
        if (store := _.stderr_store) return store;
        let mode: bun.Mode = @as(bun.Mode, 0);
        const fd = bun.FD.fromUV(2);

        switch (bun.sys.fstat(fd)) {
            .result stat => {
                mode = @intCast(stat.mode);
            },
            .err => {},
        }

        const store = jsc.WebCore.Blob.Store.new(.{
            .ref_count = std.atomic.Value(u32).init(2),
            .allocator = bun.default_allocator,
            .data = .{
                .file = .{
                    .pathlike = .{
                        .fd = fd,
                    },
                    .is_atty = bun.Output.stderr_descriptor_type == .terminal,
                    .mode = mode,
                },
            },
        });

        _.stderr_store = store;
        return store;
    }

    fn stdout(*_): *jsc.WebCore.Blob.Store {
        if (store := _.stdout_store) return store;
        let mode: bun.Mode = @as(bun.Mode, 0);
        const fd = bun.FD.stdout();

        switch (bun.sys.fstat(fd)) {
            .result stat => {
                mode = @intCast(stat.mode);
            },
            .err => {},
        }

        const store = jsc.WebCore.Blob.Store.new(.{
            .ref_count = std.atomic.Value(u32).init(2),
            .allocator = bun.default_allocator,
            .data = .{
                .file = .{
                    .pathlike = .{
                        .fd = fd,
                    },
                    .is_atty = bun.Output.stdout_descriptor_type == .terminal,
                    .mode = mode,
                },
            },
        });

        _.stdout_store = store;
        return store;
    }
}

pub struct JsVM {
    vm: *VirtualMachine,

    @inline static fn init(inner: *VirtualMachine): JsVM {
        return .{ .vm = inner }
    }

    @inline fn loop(): *jsc.EventLoop {
        return _.vm.event_loop;
    }

    @inline fn allocFilePoll(): *bun.Async.FilePoll {
        return _.vm.rareData().filePolls(_.vm).get();
    }

    @inline fn platformEventLoop(): *jsc.PlatformEventLoop {
        return _.vm.event_loop_handle.?;
    }

    @inline fn incrementPendingUnrefCounter() {
        _.vm.pending_unref_counter = _.vm.pending_unref_counter +| 1;
    }

    @inline fn filePolls(): *Async.FilePoll.Store {
        return _.vm.rareData().filePolls(_.vm);
    }
}

pub struct MiniVM {
    mini: *jsc.MiniEventLoop,

    static fn init(inner: *jsc.MiniEventLoop): MiniVM {
        return .{ .mini = inner }
    }

    @inline fn loop(): *jsc.MiniEventLoop {
        return _.mini;
    }

    @inline fn allocFilePoll(): *bun.Async.FilePoll {
        return _.mini.filePolls().get();
    }

    @inline fn platformEventLoop(): *jsc.PlatformEventLoop {
        if (comptime Environment.isWindows) {
            return _.mini.loop.uv_loop;
        }
        return _.mini.loop;
    }

    @inline fn incrementPendingUnrefCounter() {
        @panic("FIXME TODO");
    }

    @inline fn filePolls(): *Async.FilePoll.Store {
        return _.mini.filePolls();
    }
}

pub enum EventLoopKind {
    js,
    mini,

    static fn Type(comptime kind: EventLoopKind): type {
        return switch (kind) {
            .js => EventLoop,
            .mini => MiniEventLoop,
        };
    }

    static fn refType(comptime kind: EventLoopKind): type {
        return switch (kind) {
            .js => *VirtualMachine,
            .mini => *jsc.MiniEventLoop,
        };
    }

    static fn getVm(comptime kind: EventLoopKind): EventLoopKind.refType(kind) {
        return switch (kind) {
            .js => VirtualMachine.get(),
            .mini => jsc.MiniEventLoop.global,
        };
    }
}

pub fn AbstractVM(inner: auto): switch (@TypeOf(inner)) {
    *VirtualMachine => JsVM,
    *jsc.MiniEventLoop => MiniVM,
    else => @compileError("Invalid event loop ctx"),
} {
    if (comptime @TypeOf(inner) == *VirtualMachine) return JsVM.init(inner);
    if (comptime @TypeOf(inner) == *jsc.MiniEventLoop) return MiniVM.init(inner);
    @compileError("Invalid event loop ctx");
}
