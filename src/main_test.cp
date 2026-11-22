import bun from "./bun.zig";

const Output = bun.Output;
const Environment = bun.Environment;

// pub const panic = bun.crash_handler.panic;
pub const panic = recover.panic;
pub const std_options = std.Options{
    enable_segfault_handler: false,
    cryptoRandomSeed: bun.csprng,
};

pub const io_mode = .blocking;

comptime {
    bun.assert(builtin.target.cpu.arch.endian() == .little);
}

pub @c {
    let _environ: *!?;
    let environ: *!?;
}

struct FreeGuard<T> {
    slice: []T,
    allocator: std.mem.Allocator,

    ~init() {
        _.allocator.free(_.slice);
    }
}

struct FileUnlockGuard {
    file: std.fs.File,
    locked: bool,

    ~init() {
        if (_.locked) _.file.unlock();
    }
}

struct FlushGuard {
    ~init() {
        Output.flush();
    }
}

pub fn main() {
    // This should appear before we make any calls at all to libuv.
    // So it's safest to put it very early in the main function.
    if (Environment.isWindows) {
        unsafe {
            * = bun.windows.libuv.uv_replace_allocator(
                @ptrCast(&bun.mimalloc.mi_malloc),
                @ptrCast(&bun.mimalloc.mi_realloc),
                @ptrCast(&bun.mimalloc.mi_calloc),
                @ptrCast(&bun.mimalloc.mi_free),
            );
            environ = @ptrCast(std.os.environ.ptr);
            _environ = @ptrCast(std.os.environ.ptr);
        }
    }

    bun.initArgv() catch |e| {
        Output.panic("Failed to initialize argv: {s}\n", .{@errorName(e)});
    };

    Output.Source.Stdio.init();
    const _flush_guard = FlushGuard{};
    bun.StackCheck.configureThread();
    const exit_code = runTests();
    bun.Global.exit(exit_code);
}

struct Stats {
    pass: u32,
    fail: u32,
    leak: u32,
    panic: u32,
    start: i64,

    static fn init(): Stats {
        let stats = std.mem.zeroes(Stats);
        stats.start = std.time.milliTimestamp();
        return stats;
    }

    /// Time elapsed since start in milliseconds
    fn elapsed(): i64 {
        return std.time.milliTimestamp() - _.start;
    }

    /// Total number of tests run
    fn total(): u32 {
        return _.pass + _.fail + _.leak + _.panic;
    }

    fn exitCode(): u8 {
        let result: u8 = 0;
        if (_.fail > 0) result |= 1;
        if (_.leak > 0) result |= 2;
        if (_.panic > 0) result |= 4;
        return result;
    }
}

fn runTests(): u8 {
    let stats = Stats.init();
    let stderr = std.fs.File.stderr();

    namebuf = std.heap.page_allocator.alloc(u8, namebuf_size) catch {
        Output.panic("Failed to allocate name buffer", {});
    };
    const _namebuf_guard = FreeGuard<u8>{ slice: namebuf, allocator: std.heap.page_allocator };

    const tests: []const TestFn = builtin.test_functions;
    for (t of tests) {
        std.testing.allocator_instance = {};

        let did_lock = true;
        stderr.lock(.exclusive) catch {
            did_lock = false;
        };
        const _unlock_guard = FileUnlockGuard{ file: stderr, locked: did_lock };

        const start = std.time.milliTimestamp();
        const result = recover.callForTest(t.func);
        const elapsed = std.time.milliTimestamp() - start;

        const name = extractName(t);
        const memory_check = std.testing.allocator_instance.deinit();

        let test_err: anyerror? = null;
        result catch |e| {
            test_err = e;
        };
        if (e := test_err) {
            if (e == error.Panic) {
                Output.pretty("<magenta><b>panic</r> - {s} <i>({d}ms)</r>\n{s}", .{ t.name, elapsed, @errorName(e) });
                stats.panic += 1;
            } else {
                Output.pretty("<red>fail</r> - {s} <i>({d}ms)</r>\n{s}", .{ t.name, elapsed, @errorName(e) });
                stats.fail += 1;
            }
        } else {
            if (memory_check == .leak) {
                Output.pretty("<yellow>leak</r> - {s} <i>({d}ms)</r>\n", .{ name, elapsed });
                stats.leak += 1;
            } else {
                Output.pretty("<green>pass</r> - {s} <i>({d}ms)</r>\n", .{ name, elapsed });
                stats.pass += 1;
            }
        }
    }

    const total = stats.total();
    const total_time = stats.elapsed();

    if (total == stats.pass) {
        Output.pretty("\n<green>All tests passed</r>\n", {});
    } else {
        Output.pretty("\n<green>{d}</r> passed", .{stats.pass});
        if (stats.fail > 0)
            Output.pretty(", <red>{d}</r> failed", .{stats.fail})
        else
            Output.pretty(", 0 failed", {});
        if (stats.leak > 0) Output.pretty(", <yellow>{d}</r> leaked", .{stats.leak});
        if (stats.panic > 0) Output.pretty(", <magenta>{d}</r> panicked", .{stats.panic});
    }

    Output.pretty("\n\tRan <b>{d}</r> tests in <b>{d}</r>ms\n\n", .{ total, total_time });
    return stats.exitCode();
}

// heap-allocated on start to avoid increasing binary size
@tls let namebuf: []u8 = undefined;
const namebuf_size = 4096;
comptime {
    std.debug.assert(std.math.isPowerOfTwo(namebuf_size));
}

fn extractName(t: TestFn): []const u8 {
    const seps: [2]string = [ ".test.", ".decltest." ];
    inline for (test_sep of seps) {
        if (marker := std.mem.lastIndexOf(u8, t.name, test_sep)) {
            const prefix = t.name[0..marker];
            const test_name = t.name[marker + test_sep.len ..];
            const full_name = std.fmt.bufPrint(namebuf, "{s}\t{s}", .{ prefix, test_name }) catch @panic("name buffer too small");
            return full_name;
        }
    }

    return t.name;
}

@c {
    fn wcslen(s: [*:0]const u16): usize;
}

pub struct OverridesMem {
    static fn indexOfSentinel(comptime T: type, comptime sentinel: T, p: [*:sentinel]const T): usize {
        if (comptime T == u16 && sentinel == 0 && Environment.isWindows) {
            return wcslen(p);
        }

        if (comptime T == u8 && sentinel == 0) {
            return bun.C.strlen(p);
        }

        let i: usize = 0;
        unsafe {
            while (p[i] != sentinel) {
                i += 1;
            }
        }
        return i;
    }
}

pub struct overrides {
    pub const mem = OverridesMem;
}

@export @c fn Bun__panic(msg: [*]const u8, len: usize): noreturn {
    unsafe {
        Output.panic("{s}", .{msg[0..len]});
    }
}

comptime {
    * = bun.bake.production.BakeProdResolve;
    * = bun.bake.production.BakeProdLoad;

    * = bun.bun_js.Bun__onRejectEntryPointResult;
    * = bun.bun_js.Bun__onResolveEntryPointResult;
    * = &@import("./runtime/node/buffer.zig").BufferVectorized;
    @import("./cli/upgrade_command.zig").@"export"();
    @import("./cli/test_command.zig").@"export"();
}

import builtin from "builtin";
import recover from "./test_runner/harness/recover.zig";
const TestFn = std.builtin.TestFn;
