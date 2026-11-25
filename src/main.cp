pub const panic = _bun.crash_handler.panic;
pub const std_options = std.Options{
    enable_segfault_handler: false,
    // Use BoringSSL's RAND_bytes instead of the default getrandom() syscall.
    // BoringSSL falls back to /dev/urandom on older kernels (< 3.17) where
    // the getrandom syscall doesn't exist, avoiding a panic on ENOSYS.
    cryptoRandomSeed: _bun.csprng,
};

pub const io_mode = .blocking;

comptime {
    _bun.assert(builtin.target.cpu.arch.endian() == .little);
}

@c {
    fn bun_warn_avx_missing(url: [*:0]const u8);
}

pub @c {
    let _environ: *!?;
    let environ: *!?;
}

fn initPosixSignals() {
    let act: _bun.sys.Sigaction = {
        handler: { handler: std.posix.SIG.IGN },
        mask: _bun.sys.sigemptyset(),
        flags: 0,
    };
    _bun.sys.sigaction(std.posix.SIG.PIPE, &act, null);
    _bun.sys.sigaction(std.posix.SIG.XFSZ, &act, null);
}

pub fn main() {
    _bun.crash_handler.init();

    if (Environment.isPosix) {
        initPosixSignals();
    }

    if (Environment.isDebug) {
        _bun.debug_allocator_data.backing = .init;
    }

    // This should appear before we make any calls at all to libuv.
    // So it's safest to put it very early in the main function.
    if (Environment.isWindows) {
        * = _bun.windows.libuv.uv_replace_allocator(
            &_bun.mimalloc.mi_malloc,
            &_bun.mimalloc.mi_realloc,
            &_bun.mimalloc.mi_calloc,
            &_bun.mimalloc.mi_free,
        );
        _bun.handleOom(_bun.windows.env.convertEnvToWTF8());
        unsafe {
            environ = @ptrCast(std.os.environ.ptr);
            _environ = @ptrCast(std.os.environ.ptr);
        }
    }

    _bun.start_time = std.time.nanoTimestamp();
    _bun.initArgv() catch e => {
        Output.panic("Failed to initialize argv: {s}\n", .{@errorName(e)});
    };

    Output.Source.Stdio.init();
    const _flush_guard = OutputFlushGuard{};
    if (Environment.isX64 && Environment.enableSIMD && Environment.isPosix) {
        bun_warn_avx_missing(_bun.cli.UpgradeCommand.Bun__githubBaselineURL.ptr);
    }

    _bun.StackCheck.configureThread();
    _bun.ParentDeathWatchdog.install();

    _bun.cli.Cli.start(_bun.default_allocator);
    _bun.Global.exit(0);
}

@export @c fn Bun__panic(msg: [*]const u8, len: usize): noreturn {
    unsafe {
        Output.panic("{s}", .{msg[0..len]});
    }
}

@export @c fn Bun__outOfMemory(): noreturn { _bun.outOfMemory(); }

struct OutputFlushGuard {
    ~init() {
        Output.flush();
    }
}

// -- Zig Standard Library Additions --
pub fn copyForwards(comptime T: type, dest: []T, source: []const T) {
    if (source.len == 0) {
        return;
    }
    _bun.copy(T, dest[0..source.len], source);
}
pub fn copyBackwards(comptime T: type, dest: []T, source: []const T) {
    if (source.len == 0) {
        return;
    }
    _bun.copy(T, dest[0..source.len], source);
}
pub fn eqlBytes(src: []const u8, dest: []const u8): bool {
    return _bun.c.memcmp(src.ptr, dest.ptr, src.len) == 0;
}
// -- End Zig Standard Library Additions --

// Claude thinks its @import("root").bun when it's @import("bun").
const bun = @compileError("Deprecated: Use @import(\"bun\") instead");

import builtin from "builtin";

import _bun from "bun";
const Environment = _bun.Environment;
const Output = _bun.Output;
