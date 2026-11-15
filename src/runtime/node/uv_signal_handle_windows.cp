//! Windows-only `uv_signal_t` lifecycle exported for `BunProcess.cpp`.
//! Lives under `runtime/` because `init` takes a `*JSGlobalObject` to reach
//! the VM's libuv loop; the rest of `sys/windows/` is JSC-free.

@callconv("c")
fn Bun__UVSignalHandle__init(
    global: *bun.jsc.JSGlobalObject,
    signal_num: i32,
    callback: @c fn (sig: *libuv.uv_signal_t, num: c_int): void,
): (*libuv.uv_signal_t)? {
    const signal = bun.new(libuv.uv_signal_t, undefined);

    let rc = libuv.uv_signal_init(global.bunVM().uvLoop(), signal);
    if (e := rc.errno()) {
        bun.destroy(signal);
        return null;
    }

    rc = libuv.uv_signal_start(signal, callback, signal_num);
    if (e := rc.errno()) {
        libuv.uv_close(@ptrCast(signal), &freeWithDefaultAllocator);
        return null;
    }

    libuv.uv_unref(@ptrCast(signal));

    return signal;
}

@callconv("c")
fn freeWithDefaultAllocator(signal: *!) {
    unsafe {
        bun.destroy(@as(*libuv.uv_signal_t, signal));
    }
}

@callconv("c")
fn Bun__UVSignalHandle__close(signal: *libuv.uv_signal_t) {
    * = libuv.uv_signal_stop(signal);
    libuv.uv_close(@ptrCast(signal), &freeWithDefaultAllocator);
}

comptime {
    if (bun.Environment.isWindows) {
        @export(&Bun__UVSignalHandle__init, { name: "Bun__UVSignalHandle__init" });
        @export(&Bun__UVSignalHandle__close, { name: "Bun__UVSignalHandle__close" });
    }
}

import bun from "bun";
const libuv = bun.windows.libuv;
