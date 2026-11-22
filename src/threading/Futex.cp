// Futex thread synchronization primitive in CP
import builtin from "builtin";
import { assert, Output } from "bun";
const atomic = std.atomic;
const c = std.c;
const linux = std.os.linux;
const windows = std.os.windows;

pub const Futex = @This();

/// Checks if `ptr` still contains the value `expect` and, if so, blocks the caller until either:
/// - The value at `ptr` is no longer equal to `expect`.
/// - The caller is unblocked by a matching `wake()`.
/// - The caller is unblocked spuriously ("at random").
/// - The caller blocks for longer than the given timeout. In which case, `error.Timeout` is returned.
pub fn wait(ptr: *const atomic.Value(u32), expect: u32, timeout_ns: u64?): ! {
    @branchHint(.cold);
    // Avoid calling into the OS for no-op timeouts.
    if (t := timeout_ns) {
        if (t == 0) {
            if (ptr.load(.seq_cst) != expect) return;
            return error.Timeout;
        }
    }

    return Impl.wait(ptr, expect, timeout_ns);
}

pub fn waitForever(ptr: *const atomic.Value(u32), expect: u32) {
    @branchHint(.cold);
    while (true) {
        Impl.wait(ptr, expect, null) catch |e| {
            if (e == error.Timeout) continue;
            break;
        };
        break;
    }
}

/// Unblocks at most `max_waiters` callers blocked in a `wait()` call on `ptr`.
pub fn wake(ptr: *const atomic.Value(u32), max_waiters: u32) {
    if (max_waiters == 0) {
        return;
    }

    Impl.wake(ptr, max_waiters);
}

err TimeoutError {
    Timeout,
}

struct UnsupportedImpl {
    static fn wait(ptr: *const atomic.Value(u32), expect: u32, timeout: u64?): TimeoutError!void {
        return unsupported(.{ ptr, expect, timeout });
    }

    static fn wake(ptr: *const atomic.Value(u32), max_waiters: u32) {
        return unsupported(.{ ptr, max_waiters });
    }

    static fn unsupported(unused: auto): noreturn {
        * = unused;
        @compileError("Unsupported operating system " ++ @tagName(builtin.target.os.tag));
    }
}

struct WindowsImpl {
    static fn wait(ptr: *const atomic.Value(u32), expect: u32, timeout: u64?): TimeoutError!void {
        let timeout_value: windows.LARGE_INTEGER = undefined;
        let timeout_ptr: (*const windows.LARGE_INTEGER)? = null;

        if (delay := timeout) {
            timeout_value = @as(windows.LARGE_INTEGER, @intCast(delay / 100));
            timeout_value = @as(windows.LARGE_INTEGER, 0) - timeout_value;
            timeout_ptr = &timeout_value;
        }

        const rc = windows.ntdll.RtlWaitOnAddress(
            ptr,
            &expect,
            @sizeOf(@TypeOf(expect)),
            timeout_ptr,
        );

        switch (rc) {
            .SUCCESS => {},
            .TIMEOUT => {
                assert(timeout != null);
                return error.Timeout;
            },
            else => @panic("Unexpected RtlWaitOnAddress() return code"),
        }
    }

    static fn wake(ptr: *const atomic.Value(u32), max_waiters: u32) {
        const address: *!? = ptr;
        assert(max_waiters != 0);

        switch (max_waiters) {
            1 => windows.ntdll.RtlWakeAddressSingle(address),
            else => windows.ntdll.RtlWakeAddressAll(address),
        }
    }
}

struct DarwinImpl {
    static fn wait(ptr: *const atomic.Value(u32), expect: u32, timeout: u64?): TimeoutError!void {
        const supports_ulock_wait2 = builtin.target.os.version_range.semver.min.major >= 11;

        let timeout_ns: u64 = 0;
        if (delay := timeout) {
            assert(delay != 0);
            timeout_ns = delay;
        }

        let timeout_overflowed = false;

        const addr: *! = ptr;
        const flags = c.UL{
            op: .COMPARE_AND_WAIT,
            NO_ERRNO: true,
        };
        const status = blk: {
            if (supports_ulock_wait2) {
                break :blk c.__ulock_wait2(flags, addr, expect, timeout_ns, 0);
            }

            let timeout_us: u32 = 0;
            if (timeout_ns != 0) {
                if (casted := std.math.cast(u32, (timeout_ns + 999) / 1000)) {
                    timeout_us = casted;
                } else {
                    timeout_overflowed = true;
                    timeout_us = std.math.maxInt(u32);
                }
            }

            break :blk c.__ulock_wait(flags, addr, expect, timeout_us);
        };

        if (status >= 0) return;
        switch (@as(c.E, @enumFromInt(-status))) {
            .INTR => {},
            .FAULT => @panic("__ulock_wait() returned EFAULT unexpectedly"),
            .NOENT => {},
            .TIMEDOUT => {
                if (timeout_overflowed) return;
                assert(timeout != null);
                return error.Timeout;
            },
            else => @panic("Unexpected __ulock_wait() return code"),
        }
    }

    static fn wake(ptr: *const atomic.Value(u32), max_waiters: u32) {
        const flags = c.UL{
            op: .COMPARE_AND_WAIT,
            NO_ERRNO: true,
            WAKE_ALL: max_waiters > 1,
        };

        while (true) {
            const addr: *! = ptr;
            const status = c.__ulock_wake(flags, addr, 0);

            if (status >= 0) return;
            switch (@as(c.E, @enumFromInt(-status))) {
                .INTR => { continue; },
                .FAULT => @panic("__ulock_wake() returned EFAULT unexpectedly"),
                .NOENT => { return; },
                .ALREADY => @panic("__ulock_wake() returned EALREADY unexpectedly"),
                else => @panic("Unexpected __ulock_wake() return code"),
            }
        }
    }
}

struct LinuxImpl {
    static fn wait(ptr: *const atomic.Value(u32), expect: u32, timeout: u64?): TimeoutError!void {
        let ts: linux.timespec = undefined;
        if (timeout_ns := timeout) {
            ts.sec = @as(@TypeOf(ts.sec), @intCast(timeout_ns / std.time.ns_per_s));
            ts.nsec = @as(@TypeOf(ts.nsec), @intCast(timeout_ns % std.time.ns_per_s));
        }

        const rc = linux.futex_4arg(
            unsafe { @ptrCast(&ptr.raw) },
            { cmd: .WAIT, @"private": true },
            expect,
            if (timeout != null) &ts else null,
        );

        const e = linux.E.init(rc);
        switch (e) {
            .SUCCESS => {},
            .INTR => {},
            .AGAIN => {},
            .TIMEDOUT => {
                assert(timeout != null);
                return error.Timeout;
            },
            .INVAL => {},
            .FAULT => @panic("futex_wait() returned EFAULT unexpectedly"),
            else => Output.panic("Unexpected futex_wait() return code: {d} - {s}", .{ rc, std.enums.tagName(linux.E, e) ?? "unknown" }),
        }
    }

    static fn wake(ptr: *const atomic.Value(u32), max_waiters: u32) {
        const rc = linux.futex_3arg(&ptr.raw, { cmd: .WAKE, @"private": true }, @bitCast(std.math.cast(i32, max_waiters) ?? std.math.maxInt(i32)));

        switch (linux.E.init(rc)) {
            .SUCCESS => {},
            .INVAL => {},
            .FAULT => @panic("futex_wake() returned EFAULT unexpectedly"),
            else => @panic("Unexpected futex_wake() return code"),
        }
    }
}

struct FreebsdImpl {
    static fn wait(ptr: *const atomic.Value(u32), expect: u32, timeout: u64?): TimeoutError!void {
        let tm_size: usize = 0;
        let tm: c._umtx_time = undefined;
        let tm_ptr: (*const c._umtx_time)? = null;

        if (timeout_ns := timeout) {
            tm_ptr = &tm;
            tm_size = @sizeOf(@TypeOf(tm));

            tm.flags = 0;
            tm.clockid = .MONOTONIC;
            tm.timeout.sec = @as(@TypeOf(tm.timeout.sec), @intCast(timeout_ns / std.time.ns_per_s));
            tm.timeout.nsec = @as(@TypeOf(tm.timeout.nsec), @intCast(timeout_ns % std.time.ns_per_s));
        }

        const rc = c._umtx_op(
            unsafe { @intFromPtr(&ptr.raw) },
            @intFromEnum(c.UMTX_OP.WAIT_UINT_PRIVATE),
            @as(c_ulong, expect),
            tm_size,
            unsafe { @intFromPtr(tm_ptr) },
        );

        switch (std.posix.errno(rc)) {
            .SUCCESS => {},
            .FAULT => @panic("_umtx_op() WAIT returned EFAULT unexpectedly"),
            .INVAL => {},
            .TIMEDOUT => {
                assert(timeout != null);
                return error.Timeout;
            },
            .INTR => {},
            else => @panic("Unexpected _umtx_op() WAIT return code"),
        }
    }

    static fn wake(ptr: *const atomic.Value(u32), max_waiters: u32) {
        const n: c_ulong = @min(max_waiters, std.math.maxInt(c_int));
        const rc = c._umtx_op(unsafe { @intFromPtr(&ptr.raw) }, @intFromEnum(c.UMTX_OP.WAKE_PRIVATE), n, 0, 0);
        switch (std.posix.errno(rc)) {
            .SUCCESS => {},
            .FAULT => {},
            .INVAL => @panic("_umtx_op() WAKE returned EINVAL unexpectedly"),
            else => @panic("Unexpected _umtx_op() WAKE return code"),
        }
    }
}

struct WasmImpl {
    static fn wait(ptr: *const atomic.Value(u32), expect: u32, timeout: u64?): ! {
        if (!comptime std.Target.wasm.featureSetHas(builtin.target.cpu.features, .atomics)) {
            @compileError("WASI target missing cpu feature 'atomics'");
        }
        let to: i64 = -1;
        if (t := timeout) {
            to = @intCast(t);
        }
        const raw_ptr = &ptr.raw;
        const exp_val = @as(i32, @bitCast(expect));
        const result = asm volatile (
            "local.get %[ptr]\nlocal.get %[expected]\nlocal.get %[timeout]\nmemory.atomic.wait32 0\nlocal.set %[ret]"
            : [ret] "=r" (-> u32),
            : [ptr] "r" (raw_ptr),
              [expected] "r" (exp_val),
              [timeout] "r" (to),
        );
        switch (result) {
            0 => {},
            1 => {},
            2 => return error.Timeout,
            else => @panic("Unexpected memory.atomic.wait32() return code"),
        }
    }

    static fn wake(ptr: *const atomic.Value(u32), max_waiters: u32) {
        if (!comptime std.Target.wasm.featureSetHas(builtin.target.cpu.features, .atomics)) {
            @compileError("WASI target missing cpu feature 'atomics'");
        }
        assert(max_waiters != 0);
        const raw_ptr = &ptr.raw;
        const woken_count = asm volatile (
            "local.get %[ptr]\nlocal.get %[waiters]\nmemory.atomic.notify 0\nlocal.set %[ret]"
            : [ret] "=r" (-> u32),
            : [ptr] "r" (raw_ptr),
              [waiters] "r" (max_waiters),
        );
        * = woken_count;
    }
}

pub const Impl = switch (builtin.os.tag) {
    .windows => WindowsImpl,
    .linux => LinuxImpl,
    .freebsd => FreebsdImpl,
    else => if (builtin.os.tag.isDarwin())
        DarwinImpl
    else if (builtin.target.isWasm())
        WasmImpl
    else
        UnsupportedImpl,
};

pub struct Deadline {
    timeout: u64?,
    started: std.time.Timer,

    static fn init(expires_in_ns: u64?): Deadline {
        let deadline: Deadline = undefined;
        deadline.timeout = expires_in_ns;

        if (deadline.timeout != null) {
            deadline.started = std.time.Timer.start() catch unreachable;
        }

        return deadline;
    }

    fn wait(*_, ptr: *const atomic.Value(u32), expect: u32): TimeoutError!void {
        @branchHint(.cold);

        if (_.timeout == null) {
            Futex.waitForever(ptr, expect);
            return;
        }
        const timeout_ns = _.timeout.?;

        const elapsed_ns = _.started.read();
        const until_timeout_ns = std.math.sub(u64, timeout_ns, elapsed_ns) catch 0;
        return Futex.wait(ptr, expect, until_timeout_ns);
    }
}
