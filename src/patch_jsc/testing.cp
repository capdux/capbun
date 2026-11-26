// JS testing bindings for `bun.patch`. Keeps `src/patch/` free of JSC types.

import std from "std";
import bun from "bun";

const jsc = bun.jsc;

const PatchFile = bun.patch.PatchFile;
const gitDiffInternal = bun.patch.gitDiffInternal;
const parsePatchFile = bun.patch.parsePatchFile;

struct DerefGuard<T> {
    ptr: *T!? = null,
    ~init() { if (p := _.ptr) unsafe { p.deref(); } }
}
struct DeinitGuard<T> {
    ptr: *T!? = null,
    ~init() { if (p := _.ptr) unsafe { p.deinit(); } }
}
struct DeinitAllocGuard<T> {
    ptr: *T!? = null,
    allocator: std.mem.Allocator,
    ~init() { if (p := _.ptr) unsafe { p.deinit(_.allocator); } }
}
struct FreeGuard<T> {
    slice: T? = null,
    allocator: std.mem.Allocator,
    ~init() { if (s := _.slice) _.allocator.free(s); }
}

struct ApplyArgs {
    patchfile_txt: jsc.ZigString.Slice,
    patchfile: PatchFile,
    dirfd: bun.FD,

    pub fn deinit(*_) {
        _.patchfile_txt.deinit();
        _.patchfile.deinit(bun.default_allocator);
        // TODO: HAVE @zackradisic REVIEW THIS DIFF
        if (bun.FD.cwd() != _.dirfd) {
            _.dirfd.close();
        }
    }
}

pub struct TestingAPIs {
    pub static fn makeDiff(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const arguments_ = callframe.arguments_old(2);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());

        const old_folder_jsval_opt = arguments.nextEat();
        if (old_folder_jsval_opt == null) return globalThis.throw("expected 2 strings", {});
        const old_folder_jsval = old_folder_jsval_opt.?;
        let old_folder_bunstr = old_folder_jsval.toBunString(globalThis)?;
        const _g_old_bunstr = DerefGuard<@TypeOf(old_folder_bunstr)>{ ptr: &old_folder_bunstr };

        const new_folder_jsval_opt = arguments.nextEat();
        if (new_folder_jsval_opt == null) return globalThis.throw("expected 2 strings", {});
        const new_folder_jsval = new_folder_jsval_opt.?;
        let new_folder_bunstr = new_folder_jsval.toBunString(globalThis)?;
        const _g_new_bunstr = DerefGuard<@TypeOf(new_folder_bunstr)>{ ptr: &new_folder_bunstr };

        let old_folder = old_folder_bunstr.toUTF8(bun.default_allocator);
        const _g_old_folder = DeinitGuard<@TypeOf(old_folder)>{ ptr: &old_folder };

        let new_folder = new_folder_bunstr.toUTF8(bun.default_allocator);
        const _g_new_folder = DeinitGuard<@TypeOf(new_folder)>{ ptr: &new_folder };

        return switch (gitDiffInternal(bun.default_allocator, old_folder.slice(), new_folder.slice()) catch e => {
            return globalThis.throwError(e, "failed to make diff");
        }) {
            .result s => {
                let s_mut = s;
                const _g_s = DeinitGuard<@TypeOf(s_mut)>{ ptr: &s_mut };
                return bun.String.fromBytes(s_mut.items).toJS(globalThis);
            },
            .err e => {
                let e_mut = e;
                const _g_e = DeinitGuard<@TypeOf(e_mut)>{ ptr: &e_mut };
                return globalThis.throw("failed to make diff: {s}", [e_mut.items]);
            },
        };
    }

    pub static fn apply(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        let args = switch (parseApplyArgs(globalThis, callframe)) {
            .err => return error.JSError,
            .result a => a,
        };
        const _g_args = DeinitGuard<ApplyArgs>{ ptr: &args };

        if (err := args.patchfile.apply(bun.default_allocator, args.dirfd)) {
            switch (err) {
                .sys e => return globalThis.throwValue(e.toJS(globalThis)?),
                .hunk_does_not_apply h => return globalThis.throw("hunk #{d} does not apply to {s} (expected at line {d})", [h.hunk_number, h.path, h.expected_line]),
            }
        }
        return jsc.JSValue.true;
    }
    /// Used in JS tests, see `internal-for-testing.ts` and patch tests.
    pub static fn parse(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const arguments_ = callframe.arguments_old(2);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());

        const patchfile_src_js_opt = arguments.nextEat();
        if (patchfile_src_js_opt == null) return globalThis.throw("TestingAPIs.parse: expected at least 1 argument, got 0", {});
        const patchfile_src_js = patchfile_src_js_opt.?;
        const patchfile_src_bunstr = patchfile_src_js.toBunString(globalThis)?;
        const patchfile_src = patchfile_src_bunstr.toUTF8(bun.default_allocator);

        let patchfile = parsePatchFile(patchfile_src.slice()) catch e => {
            if (e == error.hunk_header_integrity_check_failed) {
                return globalThis.throwError(e, "this indicates either that the supplied patch file was incorrect, or there is a bug in Bun. Please check your .patch file, or open a GitHub issue :)");
            } else {
                return globalThis.throwError(e, "failed to parse patch file");
            }
        };
        const _g_patchfile = DeinitAllocGuard<PatchFile>{ ptr: &patchfile, allocator: bun.default_allocator };

        const str = bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "{f}", [std.json.fmt(patchfile, {})]));
        const outstr = bun.String.borrowUTF8(str);
        return outstr.toJS(globalThis);
    }

    pub static fn parseApplyArgs(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.jsc.Node.Maybe(ApplyArgs, jsc.JSValue) {
        const arguments_ = callframe.arguments_old(2);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());

        const patchfile_js_opt = arguments.nextEat();
        if (patchfile_js_opt == null) {
            if (globalThis.throw("apply: expected at least 1 argument, got 0", {}) == error.JSError) {}
            return .initErr(.zero);
        }
        const patchfile_js = patchfile_js_opt.?;

        let dir_fd = bun.FD.cwd();
        if (dir_js := arguments.nextEat()) {
            let bunstr = dir_js.toBunString(globalThis) catch return .initErr(.zero);
            const _g_bunstr = DerefGuard<@TypeOf(bunstr)>{ ptr: &bunstr };
            const path = bunstr.toOwnedSliceZ(bun.default_allocator) catch unreachable;
            const _g_path = FreeGuard<@TypeOf(path)>{ slice: path, allocator: bun.default_allocator };

            dir_fd = switch (bun.sys.open(path, bun.O.DIRECTORY | bun.O.RDONLY, 0)) {
                .err e => {
                    if (globalThis.throwValue((e.withPath(path).toJS(globalThis) catch return .initErr(.zero))) == error.JSError) {}
                    return .initErr(.zero);
                },
                .result fd => fd,
            };
        }

        let patchfile_bunstr = patchfile_js.toBunString(globalThis) catch { if (bun.FD.cwd() != dir_fd) dir_fd.close(); return .initErr(.zero); };
        const _g_patchfile_bunstr = DerefGuard<@TypeOf(patchfile_bunstr)>{ ptr: &patchfile_bunstr };
        const patchfile_src = patchfile_bunstr.toUTF8(bun.default_allocator);

        const patch_file = parsePatchFile(patchfile_src.slice()) catch e => {
            // TODO: HAVE @zackradisic REVIEW THIS DIFF
            if (bun.FD.cwd() != dir_fd) {
                dir_fd.close();
            }

            patchfile_src.deinit();
            if (globalThis.throwError(e, "failed to parse patchfile") == error.JSError) {}
            return .initErr(.zero);
        };

        return {
            result: ApplyArgs{
                dirfd: dir_fd,
                patchfile: patch_file,
                patchfile_txt: patchfile_src,
            },
        };
    }
}
