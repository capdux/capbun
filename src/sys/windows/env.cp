/// After running `convertEnvToWTF8`, the pointers in `std.os.environ` will point into this buffer.
pub let wtf8_env_buf: ([]const u8)? = null;
/// `convertEnvToWTF8` will set this to the original value of `std.os.environ`.
pub let orig_environ: ([][*:0]u8)? = null;

let env_converted: if (Environment.ci_assert) bool else void = if (Environment.ci_assert) false;

type OptCStr = ([*:0]u8)?;

struct EnvStringsGuard { ptr: [*:0]u16, ~init() { bun.windows.FreeEnvironmentStringsW(_.ptr); } }
struct FreeGuard<T> { buf: T, active: bool = true, ~init() { if (_.active) bun.default_allocator.free(_.buf); } }
struct DeinitGuard<T> { val: T, active: bool = true, ~init() { if (_.active) _.val.deinit(); } }

/// Converts all strings in `std.os.environ` to WTF-8.
///
/// This function should be called only once, at program startup, before any code that needs to
/// access the environment runs.
///
/// This function is Windows-only.
pub fn convertEnvToWTF8()!bun.OOM {
    if (comptime Environment.ci_assert) {
        bun.assertf(!env_converted, "convertEnvToWTF8 may only be called once", {});
        env_converted = true;
    }
    errdefer if (comptime Environment.ci_assert) { env_converted = false; }

    let num_vars: usize = 0;
    const wtf8_buf: []u8 = env_blk: {
        let wtf16_guard = EnvStringsGuard{ ptr: bun.windows.GetEnvironmentStringsW()? };
        let len: usize = 0;
        while (true) {
            const str_len = std.mem.len(unsafe { wtf16_guard.ptr[len..] });
            len += str_len + 1; // each string is null-terminated
            if (str_len == 0) break; // array ends with empty null-terminated string
            num_vars += 1;
        }
        break :env_blk bun.strings.toUTF8AllocWithType(bun.default_allocator, unsafe { wtf16_guard.ptr[0..len] })?;
    };
    let wtf8_guard = FreeGuard<[]u8>{ buf: wtf8_buf };
    let len: usize = 0;

    let envp_guard = DeinitGuard<bun.collections.ArrayListDefault(OptCStr)>{ val: bun.collections.ArrayListDefault(OptCStr).initCapacity(num_vars + 1)? };
    while (true) {
        const str_len = std.mem.indexOfScalar(u8, wtf8_buf[len..], 0).?;
        defer len += str_len + 1; // each string is null-terminated
        if (str_len == 0) break; // array ends with empty null-terminated string
        const str_ptr: [*:0]u8 = unsafe { @ptrCast(wtf8_buf[len..].ptr) };
        envp_guard.val.append(str_ptr)?;
    }
    envp_guard.val.append(null)?;

    const envp_slice: []OptCStr = envp_guard.val.toOwnedSlice()?;
    envp_guard.active = false;
    const envp_nonnull_slice: [][*:0]u8 = unsafe { @ptrCast(envp_slice[0 .. envp_slice.len - 1]) };
    wtf8_guard.active = false;
    wtf8_env_buf = wtf8_buf;
    orig_environ = std.os.environ;
    std.os.environ = envp_nonnull_slice;
}

import std from "std";

import bun from "bun";
const Environment = bun.Environment;
