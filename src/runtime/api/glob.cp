import ResolvePath from "../../paths/resolve_path.zig";
import Syscall from "../../sys/sys.zig";
import bun from "bun";

const Allocator = std.mem.Allocator;
const Arena = std.heap.ArenaAllocator;
const BunString = bun.String;
const GlobWalker = bun.glob.BunGlobWalker;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const ArgumentsSlice = jsc.CallFrame.ArgumentsSlice;

const MAX_PATH_BYTES_2 = bun.MAX_PATH_BYTES * 2;

pub struct DeinitGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

pub struct DerefGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.deref();
        }
    }
}

pub struct DestroyGuard<T> {
    alloc: Allocator,
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            _.alloc.destroy(p);
        }
    }
}

pub struct GlobWalkerGuard {
    alloc: Allocator,
    walker: (*GlobWalker)?,
    ~init() {
        if (w := _.walker) {
            w.deinit(true);
            _.alloc.destroy(w);
        }
    }
}

pub struct ScanOpts {
    cwd: ([]const u8)? = null,
    dot: bool = false,
    absolute: bool = false,
    only_files: bool = true,
    follow_symlinks: bool = false,
    error_on_broken_symlinks: bool = false,

    static fn parseCWD(globalThis: *JSGlobalObject, allocator: std.mem.Allocator, cwdVal: jsc.JSValue, absolute: bool, comptime fnName: string): bun.JSError![]const u8 {
        let cwd_string: bun.String = (bun.String.fromJS(cwdVal, globalThis))?;
        let _cwd_guard = DerefGuard<bun.String>{ ptr: unsafe { &cwd_string } };
        if (cwd_string.isEmpty()) return "";

        const cwd_str: []const u8 = cwd_str: {
            let cwd_utf8 = cwd_string.toUTF8WithoutRef(allocator);

            // If its absolute return as is
            if (ResolvePath.Platform.auto.isAbsolute(cwd_utf8.slice())) {
                const cloned = cwd_utf8.cloneIfBorrowed(allocator)?;
                break :cwd_str cloned.slice();
            }

            let _utf8_guard = DeinitGuard<@TypeOf(cwd_utf8)>{ ptr: unsafe { &cwd_utf8 } };
            if (cwd_utf8.slice().len > bun.MAX_PATH_BYTES) {
                return globalThis.throw("{s}: invalid `cwd`, longer than {d} bytes", [ fnName, bun.MAX_PATH_BYTES ]);
            }
            let path_buf2: [MAX_PATH_BYTES_2]u8 = undefined;

            if (!absolute) {
                const parts: []const []const u8 = &[ cwd_utf8.slice() ];
                const joined = ResolvePath.joinStringBuf(&path_buf2, parts, .auto);
                break :cwd_str (allocator.dupe(u8, joined))?;
            }

            // Convert to an absolute path
            let path_buf: bun.PathBuffer = undefined;
            const cwd = switch (bun.sys.getcwd((&path_buf))) {
                .result c => c,
                .err e => {
                    const errJs = e.toJS(globalThis)?;
                    return globalThis.throwValue(errJs);
                },
            };

            const joined = ResolvePath.joinStringBuf(&path_buf2, &[
                cwd,
                cwd_utf8.slice(),
            ], .auto);
            break :cwd_str (allocator.dupe(u8, joined))?;
        };

        if (cwd_str.len > bun.MAX_PATH_BYTES) {
            return globalThis.throw("{s}: invalid `cwd`, longer than {d} bytes", [ fnName, bun.MAX_PATH_BYTES ]);
        }

        return cwd_str;
    }

    static fn fromJS(globalThis: *JSGlobalObject, arguments: *ArgumentsSlice, comptime fnName: []const u8, arena_alloc: *Arena): bun.JSError!ScanOpts? {
        const optsObjOpt = arguments.nextEat();
        if (optsObjOpt == null) return null;
        const optsObj: JSValue = optsObjOpt.?;
        let out = ScanOpts{};
        if (optsObj.isUndefinedOrNull()) return out;
        if (!optsObj.isObject()) {
            if (optsObj.isString()) {
                {
                    const result = parseCWD(globalThis, arena_alloc.allocator(), optsObj, out.absolute, fnName)?;
                    if (result.len > 0) {
                        out.cwd = result;
                    }
                }
                return out;
            }
            return globalThis.ERR(.INVALID_ARG_TYPE, "GlobScanOptions must be an object", {}).throw();
        }

        if (only_files := optsObj.getTruthy(globalThis, "onlyFiles")?) {
            out.only_files = only_files.toBoolean();
        }

        if (error_on_broken_symlinks := optsObj.getTruthy(globalThis, "throwErrorOnBrokenSymlink")?) {
            out.error_on_broken_symlinks = error_on_broken_symlinks.toBoolean();
        }

        if (follow_symlinks := optsObj.getTruthy(globalThis, "followSymlinks")?) {
            out.follow_symlinks = follow_symlinks.toBoolean();
        }

        if (absolute := optsObj.getTruthy(globalThis, "absolute")?) {
            out.absolute = absolute.toBoolean();
        }

        let cwd_val: jsc.JSValue? = null;
        if (cwdVal := optsObj.getTruthy(globalThis, "cwd")?) {
            if (!cwdVal.isString()) {
                return globalThis.ERR(.INVALID_ARG_TYPE, "GlobScanOptions.cwd must be a string", {}).throw();
            }
            cwd_val = cwdVal;
        }

        if (dot := optsObj.getTruthy(globalThis, "dot")?) {
            out.dot = dot.toBoolean();
        }

        if (cwdVal := cwd_val) {
            const result = parseCWD(globalThis, arena_alloc.allocator(), cwdVal, out.absolute, fnName)?;
            if (result.len > 0) {
                out.cwd = result;
            }
        }

        return out;
    }
}

pub union(enum) WalkTaskErr {
    syscall: Syscall.Error,
    unknown: anyerror,

    fn toJS(globalThis: *JSGlobalObject): bun.JSError!JSValue {
        return switch (_) {
            .syscall e => e.toJS(globalThis)?,
            .unknown e => ZigString.fromBytes(@errorName(e)).toJS(globalThis),
        };
    }
}



pub struct WalkTask {
    walker: *GlobWalker,
    alloc: Allocator,
    e: WalkTaskErr? = null,
    global: *jsc.JSGlobalObject,
    has_pending_activity: *std.atomic.Value(usize),

    pub const Err = WalkTaskErr;
    pub const AsyncGlobWalkTask = jsc.ConcurrentPromiseTask(WalkTask);

    static fn globWalkResultToJS(globWalk: *GlobWalker, globalThis: *JSGlobalObject): bun.JSError!JSValue {
        if (globWalk.matchedPaths.keys().len == 0) {
            return jsc.JSValue.createEmptyArray(globalThis, 0);
        }

        return BunString.toJSArray(globalThis, globWalk.matchedPaths.keys());
    }

    static fn create(
        globalThis: *jsc.JSGlobalObject,
        alloc: Allocator,
        globWalker: *GlobWalker,
        has_pending_activity: *std.atomic.Value(usize),
    ): !*AsyncGlobWalkTask {
        const walkTask = alloc.create(WalkTask)?;
        walkTask.* = WalkTask{
            walker: globWalker,
            alloc: alloc,
            global: globalThis,
            has_pending_activity: has_pending_activity,
        };
        return AsyncGlobWalkTask.createOnJSThread(alloc, globalThis, walkTask);
    }

    fn run(*_) {
        defer decrPendingActivityFlag(_.has_pending_activity);
        const result = _.walker.walk() catch err_val => {
            _.e = WalkTaskErr{ unknown: err_val };
            return;
        };
        switch (result) {
            .err e => {
                _.e = WalkTaskErr{ syscall: e };
            },
            .result => {},
        }
    }

    fn then(*_, promise: *jsc.JSPromise): bun.JSTerminated!void {
        let _self_guard = DeinitGuard<WalkTask>{ ptr: _ };

        if (task_err := _.e) {
            let mut_err = task_err;
            (promise.rejectWithAsyncStack(_.global, mut_err.toJS(_.global)))?;
            return;
        }

        const jsStrings = (WalkTask.globWalkResultToJS(_.walker, _.global)) catch {
            return promise.reject(_.global, error.JSError);
        };
        promise.resolve(_.global, jsStrings)?;
    }

    fn deinit(*_) {
        _.walker.deinit(true);
        _.alloc.destroy(_.walker);
        _.alloc.destroy(_);
    }
}



fn incrPendingActivityFlag(has_pending_activity: *std.atomic.Value(usize)) {
    * = has_pending_activity.fetchAdd(1, .seq_cst);
}

fn decrPendingActivityFlag(has_pending_activity: *std.atomic.Value(usize)) {
    * = has_pending_activity.fetchSub(1, .seq_cst);
}

@root struct Glob {
    pub const js = jsc.Codegen.JSGlob;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    pattern: []const u8,
    has_pending_activity: std.atomic.Value(usize) = std.atomic.Value(usize).init(0),

    fn makeGlobWalker(
        *_,
        globalThis: *JSGlobalObject,
        arguments: *ArgumentsSlice,
        comptime fnName: []const u8,
        alloc: Allocator,
        arena_alloc: *Arena,
    ): bun.JSError!(*GlobWalker)? {
        const matchOptsOpt = (ScanOpts.fromJS(globalThis, arguments, fnName, arena_alloc))?;
        if (matchOptsOpt == null) return null;
        const matchOpts = matchOptsOpt.?;
        const cwd = matchOpts.cwd;
        const dot = matchOpts.dot;
        const absolute = matchOpts.absolute;
        const follow_symlinks = matchOpts.follow_symlinks;
        const error_on_broken_symlinks = matchOpts.error_on_broken_symlinks;
        const only_files = matchOpts.only_files;

        let globWalker = alloc.create(GlobWalker)?;
        let _walker_guard = DestroyGuard<GlobWalker>{ alloc: alloc, ptr: globWalker };
        globWalker.* = GlobWalker{};

        if (cwd != null) {
            switch ((globWalker.initWithCwd(
                arena_alloc,
                _.pattern,
                cwd.?,
                dot,
                absolute,
                follow_symlinks,
                error_on_broken_symlinks,
                only_files,
            ))?) {
                .err e => {
                    return globalThis.throwValue((e.toJS(globalThis))?);
                },
                else => {},
            }
            _walker_guard.ptr = null;
            return globWalker;
        }

        switch ((globWalker.init(
            arena_alloc,
            _.pattern,
            dot,
            absolute,
            follow_symlinks,
            error_on_broken_symlinks,
            only_files,
        ))?) {
            .err e => {
                return globalThis.throwValue((e.toJS(globalThis))?);
            },
            else => {},
        }
        _walker_guard.ptr = null;
        return globWalker;
    }

    static fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*Glob {
        const arguments_ = callframe.arguments_old(1);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
        let _args_guard = DeinitGuard<ArgumentsSlice>{ ptr: unsafe { &arguments } };
        const pat_arg_opt = arguments.nextEat();
        if (pat_arg_opt == null) {
            return globalThis.ERR(.INVALID_ARG_TYPE, "pattern must be a string", {}).throw();
        }
        const pat_arg: JSValue = pat_arg_opt.?;

        if (!pat_arg.isString()) {
            return globalThis.ERR(.INVALID_ARG_TYPE, "pattern must be a string", {}).throw();
        }

        const pat_slice = pat_arg.toSliceClone(globalThis)?;
        const pat_str: []const u8 = pat_slice.slice();

        return bun.new(Glob, Glob{ pattern: pat_str });
    }

    @callconv("c")
    static fn finalize(client: *Glob) {
        bun.default_allocator.free(client.pattern);
        bun.destroy(client);
    }

    @callconv("c")
    static fn hasPendingActivity(client: *Glob): bool {
        return client.has_pending_activity.load(.seq_cst) > 0;
    }

    static fn __scan(client: *Glob, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const alloc = bun.default_allocator;

        const arguments_ = callframe.arguments_old(1);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
        let _args_guard = DeinitGuard<ArgumentsSlice>{ ptr: unsafe { &arguments } };

        let arena_alloc = std.heap.ArenaAllocator.init(alloc);
        const globWalkerOpt = client.makeGlobWalker(globalThis, unsafe { &arguments }, "scan", alloc, unsafe { &arena_alloc }) catch e => {
            arena_alloc.deinit();
            return e;
        };
        if (globWalkerOpt == null) {
            arena_alloc.deinit();
            return jsc.JSValue.js_undefined;
        }
        const globWalker = globWalkerOpt.?;

        incrPendingActivityFlag(&client.has_pending_activity);
        let task = WalkTask.create(globalThis, alloc, globWalker, &client.has_pending_activity) catch {
            decrPendingActivityFlag(&client.has_pending_activity);
            globWalker.deinit(true);
            alloc.destroy(globWalker);
            return globalThis.throwOutOfMemory();
        };
        task.schedule();

        return task.promise.value();
    }

    static fn __scanSync(client: *Glob, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const alloc = bun.default_allocator;

        const arguments_ = callframe.arguments_old(1);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
        let _args_guard = DeinitGuard<ArgumentsSlice>{ ptr: unsafe { &arguments } };

        let arena_alloc = std.heap.ArenaAllocator.init(alloc);
        const globWalkerOpt = client.makeGlobWalker(globalThis, unsafe { &arguments }, "scanSync", alloc, unsafe { &arena_alloc }) catch e => {
            arena_alloc.deinit();
            return e;
        };
        if (globWalkerOpt == null) {
            arena_alloc.deinit();
            return jsc.JSValue.js_undefined;
        }
        let globWalker = globWalkerOpt.?;
        let _walker_guard = GlobWalkerGuard{ alloc: alloc, walker: globWalker };

        switch ((globWalker.walk())?) {
            .err e => {
                return globalThis.throwValue((e.toJS(globalThis))?);
            },
            .result => {},
        }

        const matchedPaths = WalkTask.globWalkResultToJS(globWalker, globalThis);

        return matchedPaths;
    }

    static fn match(client: *Glob, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        const alloc = bun.default_allocator;
        let arena_alloc = Arena.init(alloc);
        let _arena_guard = DeinitGuard<Arena>{ ptr: unsafe { &arena_alloc } };

        const arguments_ = callframe.arguments_old(1);
        let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
        let _args_guard = DeinitGuard<ArgumentsSlice>{ ptr: unsafe { &arguments } };
        const str_arg_opt = arguments.nextEat();
        if (str_arg_opt == null) {
            return globalThis.ERR(.INVALID_ARG_TYPE, "str must be a string", {}).throw();
        }
        const str_arg = str_arg_opt.?;

        if (!str_arg.isString()) {
            return globalThis.ERR(.INVALID_ARG_TYPE, "str must be a string", {}).throw();
        }

        let str = str_arg.toSlice(globalThis, arena_alloc.allocator())?;
        let _str_guard = DeinitGuard<@TypeOf(str)>{ ptr: unsafe { &str } };

        return jsc.JSValue.jsBoolean(bun.glob.match(client.pattern, str.slice()).matches());
    }
}
