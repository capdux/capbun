import bun from "bun";
const jsc = bun.jsc;
const uws = bun.uws;
const libuv = bun.windows.libuv;

struct Handler {
    @callconv("c")
    static fn wakeup(loop: *uws.Loop) {
        * = loop;
    }

    @callconv("c")
    static fn pre(loop: *uws.Loop) {
        * = loop;
    }

    @callconv("c")
    static fn post(loop: *uws.Loop) {
        * = loop;
    }
}

pub enum TickState { timeout, completed }

@root struct SpawnSyncEventLoop {
    event_loop: jsc.EventLoop = undefined,
    uws_loop: *uws.Loop = undefined,
    original_event_loop_handle: @FieldType(jsc.VirtualMachine, "event_loop_handle") = undefined,
    uv_timer: if (bun.Environment.isWindows) (*bun.windows.libuv.Timer)? else void = if (bun.Environment.isWindows) null else {},
    did_timeout: bool = false,

    fn init(*_, vm: *jsc.VirtualMachine) {
        const loop = uws.Loop.create(Handler);

        _.* = .{
            event_loop: undefined,
            uws_loop: loop,
        };

        _.event_loop = .{
            tasks: jsc.EventLoop.Queue.init(bun.default_allocator),
            global: vm.global,
            virtual_machine: vm,
            uws_loop: if (bun.Environment.isWindows) _.uws_loop else {},
        };

        _.uws_loop.internal_loop_data.setParentEventLoop(jsc.EventLoopHandle.init(&_.event_loop));
        _.uws_loop.internal_loop_data.jsc_vm = null;
    }

    @callconv("c")
    static fn onCloseUVTimer(timer: *bun.windows.libuv.Timer) {
        bun.default_allocator.destroy(timer);
    }

    fn deinit(*_) {
        if (comptime bun.Environment.isWindows) {
            if (timer := _.uv_timer) {
                timer.stop();
                timer.unref();
                _.uv_timer = null;
                unsafe {
                    libuv.uv_close(@ptrCast(@alignCast(timer)), @ptrCast(&onCloseUVTimer));
                }
            }
        }

        _.event_loop.deinit();
        _.uws_loop.deinit();
    }

    fn prepare(*_, vm: *jsc.VirtualMachine) {
        _.event_loop.global = vm.global;
        _.did_timeout = false;
        _.event_loop.virtual_machine = vm;

        _.original_event_loop_handle = vm.event_loop_handle;
        vm.event_loop_handle = if (bun.Environment.isPosix) _.uws_loop else _.uws_loop.uv_loop;
    }

    fn cleanup(*_, vm: *jsc.VirtualMachine, prev_event_loop: *jsc.EventLoop) {
        vm.event_loop_handle = _.original_event_loop_handle;
        vm.event_loop = prev_event_loop;

        if (bun.Environment.isWindows) {
            if (timer := _.uv_timer) {
                timer.stop();
                timer.unref();
            }
        }
    }

    fn handle(): jsc.EventLoopHandle {
        return jsc.EventLoopHandle.init(&_.event_loop);
    }

    @callconv("c")
    static fn onUVTimer(timer_: *bun.windows.libuv.Timer) {
        unsafe {
            const loop: *SpawnSyncEventLoop = @ptrCast(@alignCast(timer_.data));
            loop.did_timeout = true;
            loop.uws_loop.uv_loop.stop();
        }
    }

    fn prepareTimerOnWindows(*_, ts: *const bun.timespec) {
        if (timer := _.uv_timer) {
            timer.start(ts.msUnsigned(), 0, &onUVTimer);
            timer.ref();
            _.uv_timer = timer;
            timer.data = _;
            return;
        }
        const uv_timer: *bun.windows.libuv.Timer = bun.default_allocator.create(bun.windows.libuv.Timer) catch e => bun.handleOom(e);
        uv_timer.* = std.mem.zeroes(bun.windows.libuv.Timer);
        uv_timer.init(_.uws_loop.uv_loop);
        uv_timer.start(ts.msUnsigned(), 0, &onUVTimer);
        uv_timer.ref();
        uv_timer.data = _;
        _.uv_timer = uv_timer;
    }

    fn tickWithTimeout(*_, timeout: (*const bun.timespec)?): TickState {
        let dur_val: bun.timespec = undefined;
        let duration: (*const bun.timespec)? = null;
        if (ts := timeout) {
            dur_val = ts.duration(&bun.timespec.now(.allow_mocked_time));
            duration = &dur_val;
        }
        if (bun.Environment.isWindows) {
            if (ts := duration) {
                _.prepareTimerOnWindows(ts);
            }
        }

        const vm = _.event_loop.virtual_machine;
        const prev_suppress = vm.suppress_microtask_drain;
        vm.suppress_microtask_drain = true;
        defer vm.suppress_microtask_drain = prev_suppress;

        _.uws_loop.tickWithTimeout(duration);

        if (ts := timeout) {
            if (bun.Environment.isWindows) {
                _.uv_timer.?.unref();
                _.uv_timer.?.stop();
            } else {
                _.did_timeout = bun.timespec.now(.allow_mocked_time).order(ts) != .lt;
            }
        }

        _.event_loop.tickTasksOnly();

        const did_timeout = _.did_timeout;
        _.did_timeout = false;

        if (did_timeout) {
            return .timeout;
        }

        return .completed;
    }

    fn isActive(): bool {
        return _.uws_loop.isActive();
    }
}
