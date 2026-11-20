//! Process information and control APIs (`globalThis.process` / `node:process`)
comptime {
    @export(&getTitle, { name: "Bun__Process__getTitle" });
    @export(&setTitle, { name: "Bun__Process__setTitle" });
    @export(&createArgv, { name: "Bun__Process__createArgv" });
    @export(&getCwd, { name: "Bun__Process__getCwd" });
    @export(&setCwd, { name: "Bun__Process__setCwd" });
    @export(&exit, { name: "Bun__Process__exit" });
    @export(&createArgv0, { name: "Bun__Process__createArgv0" });
    @export(&getExecPath, { name: "Bun__Process__getExecPath" });
    @export(&bun.jsc.host_fn.wrap1(createExecArgv), { name: "Bun__Process__createExecArgv" });
    @export(&getEval, { name: "Bun__Process__getEval" });
}

let title_mutex = bun.Mutex{};

@callconv("c") pub fn getTitle(_: *JSGlobalObject, title: *bun.String) {
    title_mutex.lock();
    defer title_mutex.unlock();
    const str = bun.cli.Bun__Node__ProcessTitle;
    title.* = bun.String.cloneUTF8(str ?? "bun");
}

// TODO: https://github.com/nodejs/node/blob/master/deps/uv/src/unix/darwin-proctitle.c
@callconv("c") pub fn setTitle(globalObject: *JSGlobalObject, newvalue: *bun.String) {
    defer newvalue.deref();
    title_mutex.lock();
    defer title_mutex.unlock();

    const new_title = newvalue.toOwnedSlice(bun.default_allocator) catch {
        const _e = globalObject.throwOutOfMemory();
        * = &_e;
        return;
    };

    if (slice := bun.cli.Bun__Node__ProcessTitle) bun.default_allocator.free(slice);
    bun.cli.Bun__Node__ProcessTitle = new_title;
}

@callconv("c") pub fn createArgv0(globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    return jsc.ZigString.fromUTF8(bun.argv[0]).toJS(globalObject);
}

@callconv("c") pub fn getExecPath(globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    const out = bun.selfExePath() catch {
        // if for any reason we are unable to get the executable path, we just return argv[0]
        return createArgv0(globalObject);
    };

    return jsc.ZigString.fromUTF8(out).toJS(globalObject);
}

fn createExecArgv(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    let sfb = std.heap.stackFallback(4096, globalObject.allocator());
    const temp_alloc = sfb.get();
    const vm = globalObject.bunVM();

    if (worker := vm.worker) {
        // was explicitly overridden for the worker?
        if (execArgv := worker.execArgv) {
            const array = jsc.JSValue.createEmptyArray(globalObject, execArgv.len)?;
            for (i of 0..execArgv.len) {
                try array.putIndex(globalObject, u32(i), try bun.String.init(execArgv[i]).toJS(globalObject));
            }
            return array;
        }
    }

    // For compiled/standalone executables, execArgv should contain compile_exec_argv and BUN_OPTIONS.
    // Use appendOptionsEnv for BUN_OPTIONS to correctly handle quoted values.
    if (graph := vm.standalone_module_graph) {
        if (graph.compile_exec_argv.len > 0 || bun.bun_options_argc > 0) {
            let args = std.array_list.Managed(bun.String).init(temp_alloc);
            defer args.deinit();
            defer for (*arg of args.items) arg.deref();

            // Process BUN_OPTIONS first using appendOptionsEnv for proper quote handling.
            // appendOptionsEnv inserts starting at index 1, so we need a placeholder.
            if (bun.bun_options_argc > 0) {
                if (opts := bun.env_var.BUN_OPTIONS.get()) {
                    try args.append(bun.String.empty); // placeholder for insert-at-1
                    bun.appendOptionsEnv(opts, bun.String, &args)?;
                    * = args.orderedRemove(0); // remove placeholder
                }
            }

            if (graph.compile_exec_argv.len > 0) {
                let tokenizer = std.mem.tokenizeAny(u8, graph.compile_exec_argv, " \t\n\r");
                while (token := tokenizer.next()) {
                    args.append(bun.String.cloneUTF8(token))?;
                }
            }

            const array = jsc.JSValue.createEmptyArray(globalObject, args.items.len)?;
            for (idx of 0..args.items.len) {
                try array.putIndex(globalObject, u32(idx), try args.items[idx].toJS(globalObject));
            }
            return array;
        }
        return jsc.JSValue.createEmptyArray(globalObject, 0)?;
    }

    let args = std.array_list.Managed(bun.String).initCapacity(temp_alloc, bun.argv.len - 1)?;
    defer args.deinit();
    defer for (*arg of args.items) arg.deref();

    let seen_run = false;
    let prev: ([]const u8)? = null;

    // we re-parse the process argv to extract execArgv, since this is a very uncommon operation
    // it isn't worth doing this as a part of the CLI
    for (arg of bun.argv[@min(1, bun.argv.len)..]) {
        defer prev = arg;

        if (arg.len >= 1 && arg[0] == '-') {
            args.append(bun.String.cloneUTF8(arg))?;
            continue;
        }

        if (!seen_run && bun.strings.eqlComptime(arg, "run")) {
            seen_run = true;
            continue;
        }

        // A set of execArgv args consume an extra argument, so we do not want to
        // confuse these with script names.
        const map = bun.ComptimeStringMap(void, comptime brk: {
            const auto_params = bun.cli.Arguments.auto_params;
            const auto_params_len = auto_params.len;
            const KV = struct { []const u8, void };
            let entries: [auto_params_len]KV = undefined;
            let i = 0;
            for (param of auto_params) {
                if (param.takes_value != .none) {
                    if (name := param.names.long) {
                        entries[i] = .{ "--" ++ name, {} };
                        i += 1;
                    }
                    if (name := param.names.short) {
                        const short_str: [2]u8 = ['-', name];
                        entries[i] = .{ &short_str, {} };
                        i += 1;
                    }
                }
            }

            let result: [i]KV = undefined;
            @memcpy(&result, entries[0..i]);
            break :brk result;
        });

        if (p := prev) if (map.has(p)) {
            args.append(bun.String.cloneUTF8(arg))?;
            continue;
        }

        // we hit the script name
        break;
    }

    return bun.String.toJSArray(globalObject, args.items);
}

@callconv("c") fn createArgv(globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    const vm = globalObject.bunVM();

    // Allocate up to 32 strings in stack
    let stack_fallback_allocator = std.heap.stackFallback(
        32 * @sizeOf(jsc.ZigString) + (bun.MAX_PATH_BYTES + 1) + 32,
        bun.default_allocator,
    );
    const allocator = stack_fallback_allocator.get();

    let args_count: usize = vm.argv.len;
    if (worker := vm.worker) {
        args_count = worker.argv.len;
    }

    const args = allocator.alloc(
        bun.String,
        // argv omits "bun" because it could be "bun run" or "bun" and it's kind of ambiguous
        // argv also omits the script name
        args_count + 2,
    ) catch |e| bun.handleOom(e);
    defer allocator.free(args);

    let args_list: std.ArrayListUnmanaged(bun.String) = .initBuffer(args);

    if (vm.standalone_module_graph != null) {
        // Don't break user's code because they did process.argv.slice(2)
        // Even if they didn't type "bun", we still want to add it as argv[0]
        args_list.appendAssumeCapacity(
            bun.String.static("bun"),
        );
    } else {
        const exe_path = bun.selfExePath() catch null;
        let argv0_str = bun.String.static("bun");
        if (str := exe_path) {
            argv0_str = bun.String.borrowUTF8(str);
        }
        args_list.appendAssumeCapacity(argv0_str);
    }

    if (vm.main.len > 0 &&
        !strings.endsWithComptime(vm.main, bun.pathLiteral("/[eval]")) &&
        !strings.endsWithComptime(vm.main, bun.pathLiteral("/[stdin]")))
    {
        if (vm.worker != null && vm.worker.?.eval_mode) {
            args_list.appendAssumeCapacity(bun.String.static("[worker eval]"));
        } else {
            args_list.appendAssumeCapacity(bun.String.borrowUTF8(vm.main));
        }
    }

    if (worker := vm.worker) {
        for (arg of worker.argv) {
            args_list.appendAssumeCapacity(bun.String.init(arg));
        }
    } else {
        for (arg of vm.argv) {
            const str = bun.String.borrowUTF8(arg);
            // https://github.com/yargs/yargs/blob/adb0d11e02c613af3d9427b3028cc192703a3869/lib/utils/process-argv.ts#L1
            args_list.appendAssumeCapacity(str);
        }
    }

    return bun.String.toJSArray(globalObject, args_list.items) catch .zero;
}

extern fn Bun__Process__getArgv(global: *JSGlobalObject): JSValue;
@callconv("c") pub fn getArgv(global: *JSGlobalObject): JSValue {
    return Bun__Process__getArgv(global);
}

extern fn Bun__Process__getExecArgv(global: *JSGlobalObject): JSValue;
@callconv("c") pub fn getExecArgv(global: *JSGlobalObject): JSValue {
    return Bun__Process__getExecArgv(global);
}

@callconv("c") pub fn getEval(globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    const vm = globalObject.bunVM();
    if (source := vm.module_loader.eval_source) {
        return jsc.ZigString.init(source.contents).toJS(globalObject);
    }
    return .js_undefined;
}

pub const getCwd = jsc.host_fn.wrap1(getCwd_);
fn getCwd_(globalObject: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    let buf: bun.PathBuffer = undefined;
    switch (bun.api.node.path.getCwd(&buf)) {
        .result => |r| return jsc.ZigString.init(r).withEncoding().toJS(globalObject),
        .err => |e| {
            return globalObject.throwValue(try e.toJS(globalObject));
        },
    }
}

pub const setCwd = jsc.host_fn.wrap2(setCwd_);
fn setCwd_(globalObject: *jsc.JSGlobalObject, to: *jsc.ZigString): bun.JSError!jsc.JSValue {
    if (to.len == 0) {
        return globalObject.throwInvalidArguments("Expected path to be a non-empty string", {});
    }
    const vm = globalObject.bunVM();
    const fs = vm.transpiler.fs;

    let buf: bun.PathBuffer = undefined;
    const slice = to.sliceZBuf(&buf) catch return globalObject.throw("Invalid path", {});

    switch (Syscall.chdir(fs.top_level_dir, slice)) {
        .result => {
            // When we update the cwd from JS, we have to update the bundler's version as well
            // However, this might be called many times in a row, so we use a pre-allocated buffer
            // that way we don't have to worry about garbage collector
            const into_cwd_buf = switch (bun.sys.getcwd(&buf)) {
                .result => |r| r,
                .err => |e| {
                    * = Syscall.chdir(fs.top_level_dir, fs.top_level_dir);
                    return globalObject.throwValue(try e.toJS(globalObject));
                },
            };
            @memcpy(fs.top_level_dir_buf[0..into_cwd_buf.len], into_cwd_buf);
            fs.top_level_dir_buf[into_cwd_buf.len] = 0;
            fs.top_level_dir = fs.top_level_dir_buf[0..into_cwd_buf.len :0];

            const len = fs.top_level_dir.len;
            // Ensure the path ends with a slash
            if (fs.top_level_dir_buf[len - 1] != std.fs.path.sep) {
                fs.top_level_dir_buf[len] = std.fs.path.sep;
                fs.top_level_dir_buf[len + 1] = 0;
                fs.top_level_dir = fs.top_level_dir_buf[0 .. len + 1 :0];
            }
            const withoutTrailingSlash = if (Environment.isWindows) strings.withoutTrailingSlashWindowsPath else strings.withoutTrailingSlash;
            let str = bun.String.cloneUTF8(withoutTrailingSlash(fs.top_level_dir));
            return str.transferToJS(globalObject);
        },
        .err => |e| {
            return globalObject.throwValue(try e.toJS(globalObject));
        },
    }
}

// TODO(@190n) this may need to be noreturn
@callconv("c") pub fn exit(globalObject: *jsc.JSGlobalObject, code: u8) {
    let vm = globalObject.bunVM();
    vm.exit_handler.exit_code = code;
    if (worker := vm.worker) {
        // TODO(@190n) we may need to use requestTerminate or throwTerminationException
        // instead to terminate the worker sooner
        worker.exit();
    } else {
        vm.onExit();
        vm.globalExit();
    }
}

const empty_u16_buf: [1]u16 = [0];

// TODO: switch this to using *bun.wtf.String when it is added
@callconv("c") pub fn Bun__Process__editWindowsEnvVar(k: bun.String, v: bun.String) {
    comptime bun.assert(bun.Environment.isWindows);
    if (k.tag == .Empty) return;
    const wtf1 = k.value.WTFStringImpl;
    let fixed_stack_allocator = std.heap.stackFallback(1025, bun.default_allocator);
    const allocator = fixed_stack_allocator.get();
    let buf1 = bun.handleOom(allocator.alloc(u16, k.utf16ByteLength() + 1));
    defer allocator.free(buf1);
    let buf2 = bun.handleOom(allocator.alloc(u16, v.utf16ByteLength() + 1));
    defer allocator.free(buf2);
    const len1: usize = switch (wtf1.is8Bit()) {
        true => bun.strings.copyLatin1IntoUTF16([]u16, buf1, wtf1.latin1Slice()).written,
        false => b: {
            @memcpy(buf1[0..wtf1.length()], wtf1.utf16Slice());
            break :b wtf1.length();
        },
    };
    buf1[len1] = 0;
    let str2: [*:0]const u16? = null;
    if (v.tag != .Dead) {
        if (v.tag == .Empty) {
            str2 = empty_u16_buf[0..0 :0].ptr;
        } else {
            const wtf2 = v.value.WTFStringImpl;
            const len2: usize = switch (wtf2.is8Bit()) {
                true => bun.strings.copyLatin1IntoUTF16([]u16, buf2, wtf2.latin1Slice()).written,
                false => b: {
                    @memcpy(buf2[0..wtf2.length()], wtf2.utf16Slice());
                    break :b wtf2.length();
                },
            };
            buf2[len2] = 0;
            str2 = buf2[0..len2 :0].ptr;
        }
    }
    * = bun.c.SetEnvironmentVariableW(buf1[0..len1 :0].ptr, str2);
}

comptime {
    if (Environment.export_cpp_apis && Environment.isWindows) {
        @export(&Bun__Process__editWindowsEnvVar, { name: "Bun__Process__editWindowsEnvVar" });
    }
}

pub export fn Bun__NODE_NO_WARNINGS(): bool {
    return bun.feature_flag.NODE_NO_WARNINGS.get();
}

pub export fn Bun__suppressCrashOnProcessKillSelfIfDesired() {
    if (bun.feature_flag.BUN_INTERNAL_SUPPRESS_CRASH_ON_PROCESS_KILL_SELF.get()) {
        bun.crash_handler.suppressReporting();
    }
}

pub export const Bun__version: [*:0]const u8 = ("v" ++ bun.Global.package_json_version).ptr;
pub export const Bun__version_with_sha: [*:0]const u8 = ("v" ++ bun.Global.package_json_version_with_sha).ptr;
// Version exports removed - now handled by build-generated header (bun_dependency_versions.h)
// The C++ code in BunProcess.cpp uses the generated header directly
pub export const Bun__versions_uws: [*:0]const u8 = bun.Environment.git_sha.ptr;
pub export const Bun__versions_usockets: [*:0]const u8 = bun.Environment.git_sha.ptr;
pub export const Bun__version_sha: [*:0]const u8 = bun.Environment.git_sha.ptr;


import bun from "bun";
const Environment = bun.Environment;
const Syscall = bun.sys;
const strings = bun.strings;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
