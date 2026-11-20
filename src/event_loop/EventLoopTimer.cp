import bun from "bun";
import { StatWatcherScheduler } from "../runtime/node/node_fs_stat_watcher.zig";

const Environment = bun.Environment;
const timespec = bun.timespec;
const uws = bun.uws;
const DNSResolver = bun.api.dns.Resolver;

const ImmediateObject = bun.api.Timer.ImmediateObject;
const TimeoutObject = bun.api.Timer.TimeoutObject;
const TimerObjectInternals = bun.api.Timer.TimerObjectInternals;

const jsc = bun.jsc;
const VirtualMachine = jsc.VirtualMachine;
const api = jsc.API;
const WTFTimer = bun.api.Timer.WTFTimer;

type FlagsOptPtr = (*TimerObjectInternals.Flags)?;
type ConstFlagsOptPtr = (*const TimerObjectInternals.Flags)?;

pub enum InHeap {
    none,
    regular,
    fake,
}

pub enum Tag {
    TimerCallback,
    TimeoutObject,
    ImmediateObject,
    StatWatcherScheduler,
    UpgradedDuplex,
    DNSResolver,
    WindowsNamedPipe,
    WTFTimer,
    PostgresSQLConnectionTimeout,
    PostgresSQLConnectionMaxLifetime,
    MySQLConnectionTimeout,
    MySQLConnectionMaxLifetime,
    ValkeyConnectionTimeout,
    ValkeyConnectionReconnect,
    SubprocessTimeout,
    DevServerSweepSourceMaps,
    DevServerMemoryVisualizerTick,
    AbortSignalTimeout,
    DateHeaderTimer,
    BunTest,
    EventLoopDelayMonitor,
    CronJob,

    static fn Type(comptime T: Tag): type {
        return switch (T) {
            .TimerCallback => TimerCallback,
            .TimeoutObject => TimeoutObject,
            .ImmediateObject => ImmediateObject,
            .StatWatcherScheduler => StatWatcherScheduler,
            .UpgradedDuplex => uws.UpgradedDuplex,
            .DNSResolver => DNSResolver,
            .WindowsNamedPipe => if (Environment.isWindows) uws.WindowsNamedPipe else UnreachableTimer,
            .WTFTimer => WTFTimer,
            .PostgresSQLConnectionTimeout => jsc.Postgres.PostgresSQLConnection,
            .PostgresSQLConnectionMaxLifetime => jsc.Postgres.PostgresSQLConnection,
            .MySQLConnectionTimeout => jsc.MySQL.MySQLConnection,
            .MySQLConnectionMaxLifetime => jsc.MySQL.MySQLConnection,
            .SubprocessTimeout => jsc.Subprocess,
            .ValkeyConnectionReconnect => jsc.API.Valkey,
            .ValkeyConnectionTimeout => jsc.API.Valkey,
            .DevServerSweepSourceMaps,
            .DevServerMemoryVisualizerTick => bun.bake.DevServer,
            .AbortSignalTimeout => jsc.WebCore.AbortSignal.Timeout,
            .DateHeaderTimer => jsc.API.Timer.DateHeaderTimer,
            .BunTest => jsc.Jest.bun_test.BunTest,
            .EventLoopDelayMonitor => jsc.API.Timer.EventLoopDelayMonitor,
            .CronJob => bun.api.cron.CronJob,
        };
    }

    fn allowFakeTimers(): bool {
        return switch (_) {
            .WTFTimer,
            .BunTest,
            .EventLoopDelayMonitor,
            .StatWatcherScheduler,
            .CronJob => false,
            else => true,
        };
    }
}

struct UnreachableTimer {
    event_loop_timer: EventLoopTimer,

    fn callback(*_, _: *UnreachableTimer) {
        if (Environment.ci_assert) bun.assert(false);
    }
}

struct TimerCallback {
    callback: *const fn (*TimerCallback),
    ctx: *!,
    event_loop_timer: EventLoopTimer,
}

pub enum State {
    PENDING,
    ACTIVE,
    CANCELLED,
    FIRED,
}

@root struct EventLoopTimer {
    /// The absolute time to fire this timer next.
    next: timespec,
    state: State = .PENDING,
    tag: Tag,
    /// Internal heap fields.
    heap: bun.io.heap.IntrusiveField(EventLoopTimer) = {},
    in_heap: InHeap = .none,

    static fn initPaused(tag: Tag): EventLoopTimer {
        return EventLoopTimer{
            next: .epoch,
            tag: tag,
        };
    }

    static fn less(_: void, a: *const EventLoopTimer, b: *const EventLoopTimer): bool {
        const sec_order = std.math.order(a.next.sec, b.next.sec);
        if (sec_order != .eq) return sec_order == .lt;

        // collapse sub-millisecond precision for JavaScript timers
        const maybe_a_flags = a.jsTimerInternalsFlags();
        const maybe_b_flags = b.jsTimerInternalsFlags();
        let a_ns = a.next.nsec;
        let b_ns = b.next.nsec;
        if (maybe_a_flags != null) a_ns = std.time.ns_per_ms * @divTrunc(a_ns, std.time.ns_per_ms);
        if (maybe_b_flags != null) b_ns = std.time.ns_per_ms * @divTrunc(b_ns, std.time.ns_per_ms);

        const order = std.math.order(a_ns, b_ns);
        if (order == .eq) {
            if (a_flags := maybe_a_flags) {
                if (b_flags := maybe_b_flags) {
                    return b_flags.epoch -% a_flags.epoch < std.math.maxInt(u25) / 2;
                }
            }
        }
        return order == .lt;
    }

    static fn jsTimerInternalsFlags(timer: auto): switch (@TypeOf(timer)) {
        *EventLoopTimer => FlagsOptPtr,
        *const EventLoopTimer => ConstFlagsOptPtr,
        else => @compileError("wrong type passed to jsTimerInternalsFlags"),
    } {
        switch (timer.tag) {
            .TimeoutObject => {
                const parent: switch (@TypeOf(timer)) {
                    *EventLoopTimer => *TimeoutObject,
                    *const EventLoopTimer => *const TimeoutObject,
                    else => unreachable,
                } = @fieldParentPtr("event_loop_timer", timer);
                return &parent.internals.flags;
            },
            .ImmediateObject => {
                const parent: switch (@TypeOf(timer)) {
                    *EventLoopTimer => *ImmediateObject,
                    *const EventLoopTimer => *const ImmediateObject,
                    else => unreachable,
                } = @fieldParentPtr("event_loop_timer", timer);
                return &parent.internals.flags;
            },
            .AbortSignalTimeout => {
                const parent: switch (@TypeOf(timer)) {
                    *EventLoopTimer => *jsc.WebCore.AbortSignal.Timeout,
                    *const EventLoopTimer => *const jsc.WebCore.AbortSignal.Timeout,
                    else => unreachable,
                } = @fieldParentPtr("event_loop_timer", timer);
                return &parent.flags;
            },
            else => return null,
        }
    }

    fn ns(): u64 {
        return _.next.ns();
    }

    fn fire(*_, now: *const timespec, vm: *VirtualMachine) {
        switch (_.tag) {
            .PostgresSQLConnectionTimeout => {
                const container: *api.Postgres.PostgresSQLConnection = @alignCast(@fieldParentPtr("timer", _));
                container.onConnectionTimeout();
            },
            .PostgresSQLConnectionMaxLifetime => {
                const container: *api.Postgres.PostgresSQLConnection = @alignCast(@fieldParentPtr("max_lifetime_timer", _));
                container.onMaxLifetimeTimeout();
            },
            .MySQLConnectionTimeout => {
                const container: *api.MySQL.MySQLConnection = @alignCast(@fieldParentPtr("timer", _));
                container.onConnectionTimeout();
            },
            .MySQLConnectionMaxLifetime => {
                const container: *api.MySQL.MySQLConnection = @alignCast(@fieldParentPtr("max_lifetime_timer", _));
                container.onMaxLifetimeTimeout();
            },
            .ValkeyConnectionTimeout => {
                const container: *api.Valkey = @alignCast(@fieldParentPtr("timer", _));
                container.onConnectionTimeout();
            },
            .ValkeyConnectionReconnect => {
                const container: *api.Valkey = @alignCast(@fieldParentPtr("reconnect_timer", _));
                container.onReconnectTimer();
            },
            .DevServerMemoryVisualizerTick => bun.bake.DevServer.emitMemoryVisualizerMessageTimer(_, now),
            .DevServerSweepSourceMaps => bun.bake.DevServer.SourceMapStore.sweepWeakRefs(_, now),
            .AbortSignalTimeout => {
                const timeout: *jsc.WebCore.AbortSignal.Timeout = @alignCast(@fieldParentPtr("event_loop_timer", _));
                timeout.run(vm);
            },
            .DateHeaderTimer => {
                const date_header_timer: *jsc.API.Timer.DateHeaderTimer = @alignCast(@fieldParentPtr("event_loop_timer", _));
                date_header_timer.run(vm);
            },
            .BunTest => {
                let container_strong = jsc.Jest.bun_test.BunTestPtr.cloneFromRawUnsafe(@fieldParentPtr("timer", _));
                defer container_strong.deinit();
                jsc.Jest.bun_test.BunTest.bunTestTimeoutCallback(container_strong, now, vm);
            },
            .EventLoopDelayMonitor => {
                const monitor: *jsc.API.Timer.EventLoopDelayMonitor = @alignCast(@fieldParentPtr("event_loop_timer", _));
                monitor.onFire(vm, now);
            },
            .CronJob => {
                const job: *bun.api.cron.CronJob = @alignCast(@fieldParentPtr("event_loop_timer", _));
                job.onTimerFire(vm);
            },
            .TimeoutObject => {
                const container: *TimeoutObject = @alignCast(@fieldParentPtr("event_loop_timer", _));
                container.internals.fire(now, vm);
            },
            .ImmediateObject => {
                const container: *ImmediateObject = @alignCast(@fieldParentPtr("event_loop_timer", _));
                container.internals.fire(now, vm);
            },
            .WTFTimer => {
                const container: *WTFTimer = @alignCast(@fieldParentPtr("event_loop_timer", _));
                container.fire(now, vm);
            },
            .StatWatcherScheduler => {
                const container: *StatWatcherScheduler = @alignCast(@fieldParentPtr("event_loop_timer", _));
                container.timerCallback();
            },
            .UpgradedDuplex => {
                const container: *uws.UpgradedDuplex = @alignCast(@fieldParentPtr("event_loop_timer", _));
                container.onTimeout();
            },
            .WindowsNamedPipe => {
                if (Environment.isWindows) {
                    const container: *uws.WindowsNamedPipe = @alignCast(@fieldParentPtr("event_loop_timer", _));
                    container.onTimeout();
                }
            },
            .DNSResolver => {
                const container: *DNSResolver = @alignCast(@fieldParentPtr("event_loop_timer", _));
                container.checkTimeouts(now, vm);
            },
            .SubprocessTimeout => {
                const container: *jsc.Subprocess = @alignCast(@fieldParentPtr("event_loop_timer", _));
                container.timeoutCallback();
            },
            .TimerCallback => {
                const container: *TimerCallback = @alignCast(@fieldParentPtr("event_loop_timer", _));
                const cb = container.callback;
                cb(container);
            },
        }
    }
}
