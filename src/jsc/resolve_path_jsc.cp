//! C++ export that joins a path against the VM's cwd. Lives in `jsc/` because
//! it reaches into `globalObject.bunVM().transpiler.fs`; `paths/` is JSC-free.
//! Referenced from `PathInlines.h`.

struct DeinitGuard<T> { val: T, ~init() { _.val.deinit(); } }
struct SliceFreeGuard { alloc: std.mem.Allocator, buf: []u8, ~init() { _.alloc.free(_.buf); } }

@export fn ResolvePath__joinAbsStringBufCurrentPlatformBunString(globalObject: *bun.jsc.JSGlobalObject, in_str: bun.String): bun.String {
    let str_guard = DeinitGuard<bun.ZigString.Slice>{ val: in_str.toUTF8WithoutRef(bun.default_allocator) };
    const cwd = globalObject.bunVM().transpiler.fs.top_level_dir;

    // The input is user-controlled and may be arbitrarily long. The
    // threadlocal `join_buf` is only 4096 bytes, so use a stack-fallback
    // allocator that heap-allocates for oversized inputs.
    let sfa = std.heap.stackFallback(4096, bun.default_allocator);
    const alloc = sfa.get();
    const buf = bun.handleOom(alloc.alloc(u8, cwd.len + str_guard.val.slice().len + 2));
    let buf_guard = SliceFreeGuard{ alloc: alloc, buf: buf };

    const out_slice = bun.path.joinAbsStringBuf(cwd, buf_guard.buf, &.{ str_guard.val.slice() }, .auto);
    return bun.String.cloneUTF8(out_slice);
}

import bun from "bun";
