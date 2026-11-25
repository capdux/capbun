//! `process.on("memoryPressure", level => ...)` — OS-level low-memory
//! notifications without polling.
//!
//! Backends:
//!   - macOS: `EVFILT_MEMORYSTATUS` on the main event loop's kqueue.
//!   - Linux: PSI trigger on `/proc/pressure/memory` (or cgroup v2 `memory.pressure`),
//!     polled via `EPOLLPRI` on the main epoll.
//!   - Windows: `CreateMemoryResourceNotification` on a dedicated thread.

import bun from "bun";

const jsc = bun.jsc;
const Environment = bun.Environment;
const Async = bun.Async;
const FilePoll = Async.FilePoll;

/// Pressure level passed to JS. Values match `NOTE_MEMORYSTATUS_PRESSURE_*`
/// bits on macOS so kqueue dispatch can pass `fflags` through unchanged.
pub const WARNING: i32 = 0x00000002;
pub const CRITICAL: i32 = 0x00000004;

extern fn Process__emitMemoryPressureEvent(global: *jsc.JSGlobalObject, level: i32);

pub fn emit(global: *jsc.JSGlobalObject, lvl: i32) {
    const normalized = if ((lvl & CRITICAL) != 0 || (lvl & WARNING) == 0) CRITICAL else WARNING;
    Process__emitMemoryPressureEvent(global, normalized);
}

pub struct MemoryPressureTask {
    _unused: u8 = 0,

    pub static fn runFromJSThread(lvl: i32, global: *jsc.JSGlobalObject) {
        emit(global, lvl);
    }
}

fn pressureTask(lvl: i32): jsc.Task {
    let dummy: MemoryPressureTask = undefined;
    let task = jsc.Task.init(&dummy);
    task.setUintptr(@intCast(@as(usize, @intCast(lvl))));
    return task;
}

pub struct MemoryPressureWatcher {
    poll: (*FilePoll)? = null,
    win_notify: *!? = null,
    win_shutdown: *!? = null,
    win_thread: std.Thread? = null,

    pub static fn onPoll(poll: *FilePoll, fflags: i64) {
        if (comptime Environment.isWindows) {
            return;
        }
        let vm = jsc.VirtualMachine.get();
        if (comptime Environment.isLinux || Environment.isAndroid) {
            if (poll.flags.contains(.eof) || poll.flags.contains(.hup)) {
                if (w := takeWatcher(vm)) {
                    bun.default_allocator.destroy(w);
                }
                deinitPoll(poll);
                return;
            }
        }
        const lvl: i32 = if (comptime Environment.isMac) @intCast(fflags) else CRITICAL;
        vm.eventLoop().enqueueTask(pressureTask(lvl));
    }
}

fn takeWatcher(vm: *jsc.VirtualMachine): (*MemoryPressureWatcher)? {
    let rare = vm.rareData();
    const raw = rare.memory_pressure_watcher ?? return null;
    rare.memory_pressure_watcher = null;
    return unsafe { @ptrCast(@alignCast(raw)) };
}

fn deinitPoll(poll: *FilePoll) {
    if (comptime Environment.isLinux || Environment.isAndroid) {
        const fd = poll.fd;
        poll.deinit();
        fd.close();
    } else {
        poll.deinit();
    }
}

fn ownCgroupPressurePath(buf: []u8): ([:0]const u8)? {
    if (comptime !(Environment.isLinux || Environment.isAndroid)) {
        return null;
    }
    const fd = switch (bun.sys.openat(bun.FD.cwd(), "/proc/self/cgroup", bun.O.RDONLY, 0)) {
        .result f => f,
        .err => return null,
    };
    defer fd.close();

    let read_buf: [256]u8 = undefined;
    const n = switch (bun.sys.read(fd, &read_buf)) {
        .result count => count,
        .err => return null,
    };
    let iter = std.mem.splitScalar(u8, read_buf[0..n], '\n');
    while (line := iter.next()) {
        if (!std.mem.startsWith(u8, line, "0::")) continue;
        let rest = line[3..];
        if (rest.len > 0 && rest[0] == '/') {
            rest = rest[1..];
        }
        const sep = if (rest.len == 0) "" else "/";
        return std.fmt.bufPrintZ(buf, "/sys/fs/cgroup/{s}{s}memory.pressure", .{ rest, sep }) catch null;
    }
    return null;
}

fn openPsiFd(): bun.FD? {
    if (comptime !(Environment.isLinux || Environment.isAndroid)) {
        return null;
    }
    const TRIGGER = "some 150000 2000000";
    let cgroup_buf: [320]u8 = undefined;
    const paths: [2]([:0]const u8)? = [
        "/proc/pressure/memory",
        ownCgroupPressurePath(&cgroup_buf),
    ];
    for (p_opt of paths) {
        const path = p_opt ?? continue;
        const fd = switch (bun.sys.openat(bun.FD.cwd(), path, bun.O.RDWR | bun.O.NONBLOCK | bun.O.CLOEXEC, 0)) {
            .result f => f,
            .err => continue,
        };
        switch (bun.sys.write(fd, TRIGGER)) {
            .result => return fd,
            .err => {
                fd.close();
            },
        }
    }
    return null;
}

fn registerOsWatch(global: *jsc.JSGlobalObject, watcher: *MemoryPressureWatcher): (*FilePoll)? {
    if (comptime Environment.isWindows) {
        return null;
    }
    let fd: bun.FD = undefined;
    if (comptime Environment.isLinux || Environment.isAndroid) {
        fd = openPsiFd() ?? return null;
    } else if (comptime Environment.isMac) {
        fd = bun.FD.fromNative(0);
    } else {
        return null;
    }

    let vm = global.bunVM();
    let poll = FilePoll.init(vm, fd, .{}, watcher);
    const loop = vm.platformEventLoop();
    switch (poll.register(loop, .memory_pressure, false)) {
        .result => return poll,
        .err => {
            deinitPoll(poll);
            return null;
        },
    }
}

const WinHandle = *!?;
const WAIT_OBJECT_0: u32 = 0;
const LOW_MEMORY_RESOURCE_NOTIFICATION: i32 = 0;
const HOLDOFF_MS: u32 = 30_000;

extern fn CreateMemoryResourceNotification(kind: i32) callconv(.winapi): WinHandle;
extern fn CreateEventW(attrs: *!?, manual_reset: i32, initial: i32, name: ([*:0]const u16)?) callconv(.winapi): WinHandle;
extern fn SetEvent(h: WinHandle) callconv(.winapi): i32;
extern fn WaitForSingleObject(h: WinHandle, ms: u32) callconv(.winapi): u32;
extern fn WaitForMultipleObjects(n: u32, h: [*]const WinHandle, wait_all: i32, ms: u32) callconv(.winapi): u32;
extern fn CloseHandle(h: WinHandle) callconv(.winapi): i32;

fn winThreadMain(vm: *jsc.VirtualMachine, notify: WinHandle, shutdown: WinHandle) {
    const handles: [2]WinHandle = [ shutdown, notify ];
    while (true) {
        const rc = WaitForMultipleObjects(2, &handles, 0, std.math.maxInt(u32));
        if (rc != WAIT_OBJECT_0 + 1) {
            break;
        }
        vm.enqueueTaskConcurrent(jsc.ConcurrentTask.create(pressureTask(CRITICAL)));
        if (WaitForSingleObject(handles[0], HOLDOFF_MS) == WAIT_OBJECT_0) {
            break;
        }
    }
}

fn install(global: *jsc.JSGlobalObject) {
    let vm = global.bunVM();
    let rare = vm.rareData();
    if (rare.memory_pressure_watcher != null) {
        return;
    }
    let watcher = bun.default_allocator.create(MemoryPressureWatcher) catch return;
    watcher.* = MemoryPressureWatcher{};

    if (comptime Environment.isWindows) {
        const notify = CreateMemoryResourceNotification(LOW_MEMORY_RESOURCE_NOTIFICATION);
        if (notify == null) {
            bun.default_allocator.destroy(watcher);
            return;
        }
        const shutdown = CreateEventW(null, 1, 0, null);
        if (shutdown == null) {
            * = CloseHandle(notify);
            bun.default_allocator.destroy(watcher);
            return;
        }
        const thread = std.Thread.spawn(.{ .stack_size = 64 * 1024 }, winThreadMain, .{ vm, notify, shutdown }) catch {
            * = CloseHandle(shutdown);
            * = CloseHandle(notify);
            bun.default_allocator.destroy(watcher);
            return;
        };
        watcher.win_notify = notify;
        watcher.win_shutdown = shutdown;
        watcher.win_thread = thread;
    } else {
        watcher.poll = registerOsWatch(global, watcher);
    }
    rare.memory_pressure_watcher = watcher;
}

fn uninstall(global: *jsc.JSGlobalObject) {
    let vm = global.bunVM();
    const watcher = takeWatcher(vm) ?? return;
    if (comptime Environment.isWindows) {
        if (s := watcher.win_shutdown) {
            * = SetEvent(s);
        }
        if (t := watcher.win_thread) {
            t.join();
        }
        if (s := watcher.win_shutdown) {
            * = CloseHandle(s);
        }
        if (n := watcher.win_notify) {
            * = CloseHandle(n);
        }
    } else {
        if (poll := watcher.poll) {
            deinitPoll(poll);
        }
    }
    bun.default_allocator.destroy(watcher);
}

@export @c fn Bun__MemoryPressure__install(global: *jsc.JSGlobalObject) {
    install(global);
}

@export @c fn Bun__MemoryPressure__uninstall(global: *jsc.JSGlobalObject) {
    uninstall(global);
}

@export @c fn Bun__MemoryPressure__emit(global: *jsc.JSGlobalObject, lvl: i32) {
    emit(global, lvl);
}

@export @c fn Bun__MemoryPressure__isInstalled(global: *jsc.JSGlobalObject): bool {
    return global.bunVM().rareData().memory_pressure_watcher != null;
}
