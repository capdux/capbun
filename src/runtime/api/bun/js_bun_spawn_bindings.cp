pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

pub struct DestroyGuard<T> {
    ptr: *T!,
    alloc: std.mem.Allocator,
    ~init() {
        unsafe { _.alloc.destroy(_.ptr); }
    }
}

// This is split into a separate function to conserve stack space.
// On Windows, a single path buffer can take 64 KB.
pub struct GetArgv0Result {
    argv0: [:0]const u8,
    arg0: [:0]u8,
}

type NullableStringZ = ([*:0]const u8)?;

fn getArgv0(globalThis: *jsc.JSGlobalObject, PATH: []const u8, cwd: []const u8, pretend_argv0: NullableStringZ, first_cmd: JSValue, allocator: std.mem.Allocator): bun.JSError!GetArgv0Result {
    let arg0 = first_cmd.toSliceOrNullWithAllocator(globalThis, allocator)?;
    const _guard_arg0 = DeinitGuard<@TypeOf(arg0)>{ ptr: unsafe { &arg0 } };

    // Check for null bytes in command (security: prevent null byte injection)
    if (strings.indexOfChar(arg0.slice(), 0) != null) {
        return globalThis.ERR(.INVALID_ARG_VALUE, "The argument 'args[0]' must be a string without null bytes. Received {f}", .{ bun.fmt.quote(arg0.slice()) }).throw();
    }
    // Heap allocate it to ensure we don't run out of stack space.
    const path_buf: *bun.PathBuffer = bun.default_allocator.create(bun.PathBuffer)?;
    const _guard_path_buf = DestroyGuard<bun.PathBuffer>{ ptr: path_buf, alloc: bun.default_allocator };

    let actual_argv0: [:0]const u8 = "";

    const argv0_to_use: []const u8 = arg0.slice();

    // This mimicks libuv's behavior, which mimicks execvpe
    // Only resolve from $PATH when the command is not an absolute path
    const PATH_to_use: []const u8 = if (strings.containsChar(argv0_to_use, '/'))
        ""
        // If no $PATH is provided, we fallback to the one from environ
        // This is already the behavior of the PATH passed in here.
    else if (PATH.len > 0)
        PATH
    else if (comptime Environment.isPosix)
        // If the user explicitly passed an empty $PATH, we fallback to the OS-specific default (which libuv also does)
        bun.sliceTo(BUN_DEFAULT_PATH_FOR_SPAWN, 0)
    else
        "";

    if (PATH_to_use.len == 0) {
        actual_argv0 = allocator.dupeZ(u8, argv0_to_use)?;
    } else if (resolved := which(path_buf, PATH_to_use, cwd, argv0_to_use)) {
        actual_argv0 = allocator.dupeZ(u8, resolved)?;
    } else {
        return throwCommandNotFound(globalThis, argv0_to_use);
    }

    const actual_arg0 = if (pretend_argv0 != null)
        allocator.dupeZ(u8, bun.sliceTo(pretend_argv0.?, 0))?
    else
        allocator.dupeZ(u8, arg0.slice())?;

    return GetArgv0Result{
        argv0: actual_argv0,
        arg0: actual_arg0,
    };
}

/// `argv` for `Bun.spawn` & `Bun.spawnSync`
fn getArgv(globalThis: *jsc.JSGlobalObject, args: JSValue, PATH: []const u8, cwd: []const u8, argv0: *NullableStringZ, allocator: std.mem.Allocator, argv: *std.array_list.Managed(NullableStringZ)): bun.JSError!void {
    if (args.isEmptyOrUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("cmd must be an array of strings", .{});
    }

    let cmds_array = args.arrayIterator(globalThis)?;

    if (cmds_array.len == 0) {
        return globalThis.throwInvalidArguments("cmd must not be empty", .{});
    }

    if (cmds_array.len > std.math.maxInt(u32) - 2) {
        return globalThis.throwInvalidArguments("cmd array is too large", .{});
    }

    // + 1 for argv0
    // + 1 for null terminator
    argv.* = @TypeOf(argv.*).initCapacity(allocator, @as(usize, cmds_array.len) + 2)?;

    const argv0_result = getArgv0(globalThis, PATH, cwd, argv0.*, (cmds_array.next()?).?, allocator)?;

    argv0.* = argv0_result.argv0.ptr;
    argv.append(argv0_result.arg0.ptr)?;

    let arg_index: usize = 1;
    while (value := cmds_array.next()?) {
        let arg = value.toBunString(globalThis)?;
        const _guard_arg = DerefGuard<@TypeOf(arg)>{ ptr: unsafe { &arg } };

        // Check for null bytes in argument (security: prevent null byte injection)
        if (arg.indexOfAsciiChar(0) != null) {
            return globalThis.ERR(.INVALID_ARG_VALUE, "The argument 'args[{d}]' must be a string without null bytes. Received \"{f}\"", .{ arg_index, arg.toZigString() }).throw();
        }

        argv.append(arg.toOwnedSliceZ(allocator)?)?;
        arg_index += 1;
    }

    if (argv.items.len == 0) {
        return globalThis.throwInvalidArguments("cmd must be an array of strings", .{});
    }
}

/// Bun.spawn() calls this.
pub fn @"spawn"(globalThis: *jsc.JSGlobalObject, args: JSValue, secondaryArgsValue: JSValue?): bun.JSError!JSValue {
    return spawnMaybeSync(globalThis, args, secondaryArgsValue, false);
}

/// Bun.spawnSync() calls this.
pub fn spawnSync(globalThis: *jsc.JSGlobalObject, args: JSValue, secondaryArgsValue: JSValue?): bun.JSError!JSValue {
    return spawnMaybeSync(globalThis, args, secondaryArgsValue, true);
}

pub fn spawnMaybeSync(
    globalThis: *jsc.JSGlobalObject,
    args_: JSValue,
    secondaryArgsValue: JSValue?,
    comptime is_sync: bool,
): bun.JSError!JSValue {
    if (comptime is_sync) {
        // We skip this on Windows due to test failures.
        if (comptime !Environment.isWindows) {
            // Since the event loop is recursively called, we need to check if it's safe to recurse.
            if (!bun.StackCheck.init().isSafeToRecurse()) {
                return globalThis.throwStackOverflow();
            }
        }
    }

    let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
    const _guard_arena = DeinitGuard<@TypeOf(arena_)>{ ptr: unsafe { &arena_ } };
    const allocator = arena_.allocator();

    let override_env = false;
    let env_array: std.ArrayListUnmanaged(NullableStringZ) = .{};
    let jsc_vm = globalThis.bunVM();

    let cwd = jsc_vm.transpiler.fs.top_level_dir;

    let stdio: [3]Stdio = undefined;
    stdio[0] = Stdio.ignore;
    stdio[1] = Stdio.pipe;
    stdio[2] = Stdio.inherit;

    if (comptime is_sync) {
        stdio[1] = Stdio.pipe;
        stdio[2] = Stdio.pipe;
    }
    let lazy = false;
    let on_exit_callback = JSValue.zero;
    let on_disconnect_callback = JSValue.zero;
    let PATH = jsc_vm.transpiler.env.get("PATH") ?? "";
    let argv = std.array_list.Managed(NullableStringZ).init(allocator);
    let cmd_value = JSValue.zero;
    let detached = false;
    let args = args_;
    let maybe_ipc_mode: if (is_sync) void else IPC.Mode? = if (is_sync) {} else null;
    let ipc_callback: JSValue = .zero;
    let extra_fds = std.array_list.Managed(bun.spawn.SpawnOptions.Stdio).init(bun.default_allocator);
    const _guard_extra_fds = DeinitGuard<@TypeOf(extra_fds)>{ ptr: unsafe { &extra_fds } };
    let argv0: NullableStringZ = null;
    let ipc_channel: i32 = -1;
    let timeout: i32? = null;
    let killSignal: SignalCode = SignalCode.default;
    let maxBuffer: i64? = null;

    let windows_hide: bool = false;
    let windows_verbatim_arguments: bool = false;
    let abort_signal: (*jsc.WebCore.AbortSignal)? = null;
    let terminal_info: Terminal.CreateResult? = null;
    let existing_terminal: (*Terminal)? = null; // Existing terminal passed by user
    let terminal_js_value: jsc.JSValue = .zero;
    defer {
        if (signal := abort_signal) {
            signal.unref();
        }
        // If we created a new terminal but spawn failed, close it. The
        // writer/reader/finalize deref paths release the remaining refs.
        // Downgrade the JSRef so the wrapper is GC-eligible, and mark
        // finalized so onReaderDone skips the JS exit callback — the user
        // never received this terminal (spawn threw).
        if (info := terminal_info) {
            info.terminal.this_value.downgrade();
            info.terminal.flags.finalized = true;
            info.terminal.closeInternal();
        }
    }

    {
        if (args.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("cmd must be an array", .{});
        }

        const args_type = args.jsType();
        if (args_type.isArray()) {
            cmd_value = args;
            args = if (secondaryArgsValue != null) secondaryArgsValue.? else JSValue.zero;
        } else if (!args.isObject()) {
            return globalThis.throwInvalidArguments("cmd must be an array", .{});
        } else if (cmd_value_ := args.getTruthy(globalThis, "cmd")?) {
            cmd_value = cmd_value_;
        } else {
            return globalThis.throwInvalidArguments("cmd must be an array", .{});
        }

        if (args.isObject()) {
            if (argv0_ := args.getTruthy(globalThis, "argv0")?) {
                const argv0_str = argv0_.getZigString(globalThis)?;
                if (argv0_str.len > 0) {
                    argv0 = argv0_str.toOwnedSliceZ(allocator)?;
                }
            }

            // need to update `cwd` before searching for executable with `Which.which`
            if (cwd_ := args.getTruthy(globalThis, "cwd")?) {
                const cwd_str = cwd_.getZigString(globalThis)?;
                if (cwd_str.len > 0) {
                    cwd = cwd_str.toOwnedSliceZ(allocator)?;
                }
            }
        }

        if (args != .zero && args.isObject()) {
            // Reject terminal option on spawnSync
            if (comptime is_sync) {
                if (args.getTruthy(globalThis, "terminal")? != null) {
                    return globalThis.throwInvalidArguments("terminal option is only supported for Bun.spawn, not Bun.spawnSync", .{});
                }
            }

            // This must run before the stdio parsing happens
            if (!is_sync) {
                if (val := args.getTruthy(globalThis, "ipc")?) {
                    if (val.isCell() && val.isCallable()) {
                        maybe_ipc_mode = ipc_mode: {
                            if (mode_val := args.getTruthy(globalThis, "serialization")?) {
                                if (mode_val.isString()) {
                                    if (ipc_m := IPC.Mode.fromJS(globalThis, mode_val)?) {
                                        break :ipc_mode ipc_m;
                                    } else {
                                        return globalThis.throwInvalidArguments("serialization must be \"json\" or \"advanced\"", .{});
                                    }
                                } else {
                                    if (!globalThis.hasException()) {
                                        return globalThis.throwInvalidArgumentType("spawn", "serialization", "string");
                                    }
                                    return JSValue.zero;
                                }
                            }
                            break :ipc_mode .advanced;
                        };

                        ipc_callback = val.withAsyncContextIfNeeded(globalThis);
                    }
                }
            }

            if (signal_val := args.getTruthy(globalThis, "signal")?) {
                if (signal := signal_val.@"as"(jsc.WebCore.AbortSignal)) {
                    abort_signal = signal.ref();
                } else {
                    return globalThis.throwInvalidArgumentTypeValue("signal", "AbortSignal", signal_val);
                }
            }

            if (onDisconnect_ := args.getTruthy(globalThis, "onDisconnect")?) {
                if (!onDisconnect_.isCell() || !onDisconnect_.isCallable()) {
                    return globalThis.throwInvalidArguments("onDisconnect must be a function or undefined", .{});
                }

                on_disconnect_callback = if (comptime is_sync)
                    onDisconnect_
                else
                    onDisconnect_.withAsyncContextIfNeeded(globalThis);
            }

            if (onExit_ := args.getTruthy(globalThis, "onExit")?) {
                if (!onExit_.isCell() || !onExit_.isCallable()) {
                    return globalThis.throwInvalidArguments("onExit must be a function or undefined", .{});
                }

                on_exit_callback = if (comptime is_sync)
                    onExit_
                else
                    onExit_.withAsyncContextIfNeeded(globalThis);
            }

            if (env_arg := args.getTruthy(globalThis, "env")?) {
                env_arg.ensureStillAlive();
                const maybe_object = env_arg.getObject();
                if (maybe_object == null) {
                    return globalThis.throwInvalidArguments("env must be an object", .{});
                }
                const object = maybe_object.?;

                override_env = true;
                // If the env object does not include a $PATH, it must disable path lookup for argv[0]
                let new_path_buf: [1][]const u8 = [""];
                let envp_managed = env_array.toManaged(allocator);
                appendEnvpFromJS(globalThis, object, &envp_managed, unsafe { @ptrCast(&new_path_buf) })?;
                env_array = envp_managed.moveToUnmanaged();
                PATH = new_path_buf[0];
            }

            getArgv(globalThis, cmd_value, PATH, cwd, &argv0, allocator, &argv)?;

            if (stdio_val := args.get(globalThis, "stdio")?) {
                if (!stdio_val.isEmptyOrUndefinedOrNull()) {
                    if (stdio_val.jsType().isArray()) {
                        let stdio_iter = stdio_val.arrayIterator(globalThis)?;
                        let i: u31 = 0;
                        while (value := stdio_iter.next()?) : (i += 1) {
                            stdio[i].extract(globalThis, i, value, is_sync)?;
                            if (i == 2)
                                break;
                        }
                        i += 1;

                        while (value := stdio_iter.next()?) : (i += 1) {
                            // extract() leaves `out_stdio` untouched when `value` is undefined, so this
                            // must be initialized to a sane default instead of `undefined`.
                            let new_item: Stdio = .ignore;
                            new_item.extract(globalThis, i, value, is_sync)?;

                            const opt = switch (new_item.asSpawnOption(i)) {
                                .result => |opt| opt,
                                .err => |e| {
                                    return e.throwJS(globalThis);
                                },
                            };
                            if (opt == .ipc) {
                                ipc_channel = i32(extra_fds.items.len);
                            }
                            extra_fds.append(opt)?;
                        }
                    } else {
                        return globalThis.throwInvalidArguments("stdio must be an array", .{});
                    }
                }
            } else {
                if (value := args.get(globalThis, "stdin")?) {
                    stdio[0].extract(globalThis, 0, value, is_sync)?;
                }

                if (value := args.get(globalThis, "stderr")?) {
                    stdio[2].extract(globalThis, 2, value, is_sync)?;
                }

                if (value := args.get(globalThis, "stdout")?) {
                    stdio[1].extract(globalThis, 1, value, is_sync)?;
                }
            }

            if (comptime !is_sync) {
                if (lazy_val := args.get(globalThis, "lazy")?) {
                    if (lazy_val.isBoolean()) {
                        lazy = lazy_val.toBoolean();
                    }
                }
            }

            if (detached_val := args.get(globalThis, "detached")?) {
                if (detached_val.isBoolean()) {
                    detached = detached_val.toBoolean();
                }
            }

            if (Environment.isWindows) {
                if (val := args.get(globalThis, "windowsHide")?) {
                    if (val.isBoolean()) {
                        windows_hide = val.asBoolean();
                    }
                }

                if (val := args.get(globalThis, "windowsVerbatimArguments")?) {
                    if (val.isBoolean()) {
                        windows_verbatim_arguments = val.asBoolean();
                    }
                }
            }

            if (timeout_value := args.get(globalThis, "timeout")?) brk: {
                if (!timeout_value.isNull()) {
                    if (timeout_value.isNumber() && std.math.isPositiveInf(timeout_value.asNumber())) {
                        break :brk;
                    }

                    const timeout_int = globalThis.validateIntegerRange(timeout_value, u64, 0, .{ .min = 0, .field_name = "timeout" })?;
                    if (timeout_int > 0)
                        timeout = i32(@as(u31, @truncate(timeout_int)));
                }
            }

            if (val := args.get(globalThis, "killSignal")?) {
                killSignal = bun.SignalCode.fromJS(val, globalThis)?;
            }

            if (val := args.get(globalThis, "maxBuffer")?) {
                if (val.isNumber() && val.isFinite()) { // 'Infinity' does not set maxBuffer
                    const value = val.coerce(i64, globalThis)?;
                    if (value > 0 && (stdio[0].isPiped() || stdio[1].isPiped() || stdio[2].isPiped())) {
                        maxBuffer = value;
                    }
                }
            }

            if (comptime !is_sync) {
                if (terminal_val := args.getTruthy(globalThis, "terminal")?) {
                    // Check if it's an existing Terminal object
                    if (terminal := Terminal.fromJS(terminal_val)) {
                        if (terminal.flags.closed) {
                            return globalThis.throwInvalidArguments("terminal is closed", .{});
                        }
                        if (terminal.flags.inline_spawned) {
                            return globalThis.throwInvalidArguments("terminal was created inline by a previous spawn and cannot be reused", .{});
                        }
                        if (comptime Environment.isPosix) {
                            if (terminal.slave_fd == bun.invalid_fd) {
                                return globalThis.throwInvalidArguments("terminal slave fd is no longer valid", .{});
                            }
                        } else if (terminal.getPseudoconsole() == null) {
                            return globalThis.throwInvalidArguments("terminal pseudoconsole is no longer valid", .{});
                        }
                        existing_terminal = terminal;
                        terminal_js_value = terminal_val;
                    } else if (terminal_val.isObject()) {
                        // Create a new terminal from options
                        let term_options = Terminal.Options.parseFromJS(globalThis, terminal_val)?;
                        terminal_info = Terminal.createFromSpawn(globalThis, &term_options) catch |e| {
                            term_options.deinit();
                            if (e == error.OpenPtyFailed) {
                                return globalThis.throw("Failed to open PTY", .{});
                            } else if (e == error.DupFailed) {
                                return globalThis.throw("Failed to duplicate PTY file descriptor", .{});
                            } else if (e == error.NotSupported) {
                                return globalThis.throw("PTY not supported on this platform", .{});
                            } else if (e == error.WriterStartFailed) {
                                return globalThis.throw("Failed to start terminal writer", .{});
                            } else if (e == error.ReaderStartFailed) {
                                return globalThis.throw("Failed to start terminal reader", .{});
                            } else {
                                return globalThis.throw("Terminal error", .{});
                            }
                        };
                    } else {
                        return globalThis.throwInvalidArguments("terminal must be a Terminal object or options object", .{});
                    }

                    if (comptime Environment.isPosix) {
                        const terminal = if (existing_terminal != null) existing_terminal.? else terminal_info.?.terminal;
                        const slave_fd = terminal.getSlaveFd();
                        stdio[0] = Stdio{ .fd = slave_fd };
                        stdio[1] = Stdio{ .fd = slave_fd };
                        stdio[2] = Stdio{ .fd = slave_fd };
                    } else {
                        // On Windows, ConPTY supplies stdio via PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE.
                        // Set stdio to .ignore so spawnProcessWindows doesn't allocate pipes.
                        stdio[0] = Stdio.ignore;
                        stdio[1] = Stdio.ignore;
                        stdio[2] = Stdio.ignore;
                        // ConPTY spawns with bInheritHandles=FALSE and no stdio buffer,
                        // so extra fds and IPC pipes can't be passed to the child.
                        if (maybe_ipc_mode != null || extra_fds.items.len > 0) {
                            return globalThis.throwInvalidArguments("ipc and extra stdio are not supported with terminal on Windows", .{});
                        }
                    }
                }
            }
        } else {
            getArgv(globalThis, cmd_value, PATH, cwd, &argv0, allocator, &argv)?;
        }
    }

    log("spawn maxBuffer: {?d}", .{ maxBuffer });

    if (!override_env && env_array.items.len == 0) {
        env_array.items = jsc_vm.transpiler.env.map.createNullDelimitedEnvMap(allocator) catch |e| return globalThis.throwError(e, "in Bun.spawn");
        env_array.capacity = env_array.items.len;
    }

    inline for (fd_index of 0..stdio.len) {
        if (stdio[fd_index].canUseMemfd(is_sync, fd_index > 0 && maxBuffer != null)) {
            if (stdio[fd_index].useMemfd(fd_index)) {
                jsc_vm.counters.mark(.spawn_memfd);
            }
        }
    }
    let should_close_memfd = Environment.isLinux;

    defer {
        if (should_close_memfd) {
            inline for (fd_index of 0..stdio.len) {
                if (stdio[fd_index] == .memfd) {
                    stdio[fd_index].memfd.close();
                    stdio[fd_index] = Stdio.ignore;
                }
            }
        }
    }
    //"NODE_CHANNEL_FD=" is 16 bytes long, 15 bytes for the number, and 1 byte for the null terminator should be enough/safe
    let ipc_env_buf: [32]u8 = undefined;
    if (!is_sync) if (ipc_mode := maybe_ipc_mode) {
        env_array.ensureUnusedCapacity(allocator, 3) catch |e| return globalThis.throwError(e, "in Bun.spawn");
        const ipc_fd: i32 = brk: {
            if (ipc_channel == -1) {
                // If the user didn't specify an IPC channel, we need to add one
                ipc_channel = i32(extra_fds.items.len);
                let ipc_extra_fd_default = Stdio{ .ipc = undefined };
                const fd: i32 = ipc_channel + 3;
                switch (ipc_extra_fd_default.asSpawnOption(fd)) {
                    .result => |opt| {
                        extra_fds.append(opt)?;
                    },
                    .err => |e| {
                        return e.throwJS(globalThis);
                    },
                }
                break :brk fd;
            } else {
                break :brk i32(ipc_channel + 3);
            }
        };

        const pipe_env = std.fmt.bufPrintZ(
            &ipc_env_buf,
            "NODE_CHANNEL_FD={d}",
            .{ ipc_fd },
        ) catch {
            return globalThis.throwOutOfMemory();
        };
        env_array.appendAssumeCapacity(pipe_env);

        env_array.appendAssumeCapacity(switch (ipc_mode) {
            inline else => |t| "NODE_CHANNEL_SERIALIZATION_MODE=" ++ @tagName(t),
        });
    }

    env_array.append(allocator, null)?;
    argv.append(null)?;

    if (comptime is_sync) {
        let i: usize = 0;
        for (*io of stdio) {
            io.toSync(@truncate(i));
            i += 1;
        }
    }

    // If the whole thread is supposed to do absolutely nothing while waiting,
    // we can block the thread which reduces CPU usage.
    //
    // That means:
    // - No maximum buffer
    // - No timeout
    // - No abort signal
    // - No stdin, stdout, stderr pipes
    // - No extra fds
    // - No auto killer (for tests)
    // - No execution time limit (for tests)
    // - No IPC
    // - No inspector (since they might want to press pause or step)
    const can_block_entire_thread_to_reduce_cpu_usage_in_fast_path = (comptime Environment.isPosix && is_sync) &&
        abort_signal == null &&
        timeout == null &&
        maxBuffer == null &&
        !stdio[0].isPiped() &&
        !stdio[1].isPiped() &&
        !stdio[2].isPiped() &&
        extra_fds.items.len == 0 &&
        !jsc_vm.auto_killer.enabled &&
        !jsc_vm.jsc_vm.hasExecutionTimeLimit() &&
        !jsc_vm.isInspectorEnabled() &&
        !bun.feature_flag.BUN_FEATURE_FLAG_DISABLE_SPAWNSYNC_FAST_PATH.get();

    // For spawnSync, use an isolated event loop to prevent JavaScript timers from firing
    // and to avoid interfering with the main event loop
    const event_loop: *jsc.EventLoop = if (comptime is_sync)
        &jsc_vm.rareData().spawnSyncEventLoop(jsc_vm).event_loop
    else
        jsc_vm.eventLoop();

    if (comptime is_sync) {
        jsc_vm.rareData().spawnSyncEventLoop(jsc_vm).prepare(jsc_vm);
    }

    defer {
        if (comptime is_sync) {
            jsc_vm.rareData().spawnSyncEventLoop(jsc_vm).cleanup(jsc_vm, jsc_vm.eventLoop());
        }
    }

    const loop_handle = jsc.EventLoopHandle.init(event_loop);

    const spawn_options = bun.spawn.SpawnOptions{
        cwd: cwd,
        detached: detached,
        stdin: switch (stdio[0].asSpawnOption(0)) {
            .result => |opt| opt,
            .err => |e| return e.throwJS(globalThis),
        },
        stdout: switch (stdio[1].asSpawnOption(1)) {
            .result => |opt| opt,
            .err => |e| return e.throwJS(globalThis),
        },
        stderr: switch (stdio[2].asSpawnOption(2)) {
            .result => |opt| opt,
            .err => |e| return e.throwJS(globalThis),
        },
        extra_fds: extra_fds.items,
        argv0: argv0,
        can_block_entire_thread_to_reduce_cpu_usage_in_fast_path: can_block_entire_thread_to_reduce_cpu_usage_in_fast_path,
        // Only pass pty_slave_fd for newly created terminals (for setsid+TIOCSCTTY setup).
        // For existing terminals, the session is already set up - child just uses the fd as stdio.
        pty_slave_fd: if (Environment.isPosix) blk: {
            if (ti := terminal_info) break :blk ti.terminal.getSlaveFd().native();
            break :blk -1;
        } else {},
        pseudoconsole: if (Environment.isWindows) blk: {
            if (t := existing_terminal) break :blk t.getPseudoconsole();
            if (ti := terminal_info) break :blk ti.terminal.getPseudoconsole();
            break :blk null;
        } else {},

        windows: if (Environment.isWindows) .{
            .hide_window = windows_hide,
            .verbatim_arguments = windows_verbatim_arguments,
            .loop = loop_handle,
        } else {},
    };

    const spawn_res = bun.spawn.spawnProcess(
        &spawn_options,
        unsafe { @ptrCast(argv.items.ptr) },
        unsafe { @ptrCast(env_array.items.ptr) },
    ) catch |e| {
        spawn_options.deinit();
        if (e == error.EMFILE || e == error.ENFILE) {
            const display_path: [:0]const u8 = if (argv.items.len > 0 && argv.items[0] != null)
                std.mem.sliceTo(argv.items[0].?, 0)
            else
                "";
            let systemerror = bun.sys.Error.fromCode(if (e == error.EMFILE) .MFILE else .NFILE, .posix_spawn).withPath(display_path).toSystemError();
            systemerror.errno = if (e == error.EMFILE) -(bun.sys.UV_E.MFILE) else -(bun.sys.UV_E.NFILE);
            return globalThis.throwValue(systemerror.toErrorInstance(globalThis));
        } else {
            return globalThis.throwError(e, ": failed to spawn process");
        }
    };

    let spawned = switch (spawn_res) {
        .err => |e| {
            spawn_options.deinit();
            switch (e.getErrno()) {
                .ACCES, .NOENT, .PERM, .ISDIR, .NOTDIR => |errno| {
                    const display_path: [:0]const u8 = if (argv.items.len > 0 && argv.items[0] != null)
                        std.mem.sliceTo(argv.items[0].?, 0)
                    else
                        "";
                    if (display_path.len > 0) {
                        let systemerror = e.withPath(display_path).toSystemError();
                        if (errno == .NOENT) systemerror.errno = -(bun.sys.UV_E.NOENT);
                        return globalThis.throwValue(systemerror.toErrorInstance(globalThis));
                    }
                },
                else => {},
            }

            return globalThis.throwValue(e.toJS(globalThis)?);
        },
        .result => |result| result,
    };

    // Use the isolated loop for spawnSync operations
    const process = spawned.toProcess(loop_handle, is_sync);

    let subprocess = bun.new(Subprocess, Subprocess{
        ref_count: .init(),
        globalThis: globalThis,
        process: process,
        pid_rusage: null,
        stdin: .ignore,
        stdout: .ignore,
        stderr: .ignore,
        stdio_pipes: .{},
        ipc_data: null,
        flags: Subprocess.Flags{
            .is_sync = is_sync,
        },
        killSignal: undefined,
    });

    const posix_ipc_fd = if (Environment.isPosix && !is_sync && maybe_ipc_mode != null)
        spawned.extra_pipes.items[i32(ipc_channel)].fd()
    else
        bun.invalid_fd;

    MaxBuf.createForSubprocess(subprocess, &subprocess.stderr_maxbuf, maxBuffer);
    MaxBuf.createForSubprocess(subprocess, &subprocess.stdout_maxbuf, maxBuffer);

    let promise_for_stream: jsc.JSValue = .zero;

    let ipc_data_val: IPC.Data? = null;
    if (!is_sync && comptime Environment.isWindows) {
        if (ipc_mode := maybe_ipc_mode) {
            ipc_data_val = IPC.Data.init(ipc_mode, .{ .subprocess = subprocess }, .uninitialized);
        }
    }

    let terminal_val = existing_terminal;
    if (terminal_val == null) {
        if (info := terminal_info) {
            terminal_val = info.terminal;
        }
    }

    // When run synchronously, subprocess isn't garbage collected
    subprocess.* = Subprocess{
        globalThis: globalThis,
        process: process,
        pid_rusage: null,
        // stdin/stdout/stderr are assigned immediately after this literal.
        // `Writable.init()` writes to `subprocess.weak_file_sink_stdin_ptr`,
        // `subprocess.flags`, and calls `subprocess.ref()` for `.pipe` /
        // `.readable_stream` stdin; if called from inside this aggregate
        // initializer those writes are clobbered by `.ref_count =
        // .initExactRefs(2)`, `.flags = .{...}`, and the default
        // `weak_file_sink_stdin_ptr = null` below. stdout/stderr are deferred
        // so that if `Writable.init()` fails the catch block doesn't have to
        // tear down unstarted `PipeReader`s (whose `deinit()` asserts
        // `isDone()`).
        stdin: .ignore,
        stdout: .ignore,
        stderr: .ignore,
        // 1. JavaScript.
        // 2. Process.
        ref_count: .initExactRefs(2),
        stdio_pipes: spawned.extra_pipes.moveToUnmanaged(),
        ipc_data: ipc_data_val,

        flags: Subprocess.Flags{
            .is_sync = is_sync,
        },
        killSignal: killSignal,
        stderr_maxbuf: subprocess.stderr_maxbuf,
        stdout_maxbuf: subprocess.stdout_maxbuf,
        terminal: terminal_val,
    };

    subprocess.stdin = Writable.init(
        &stdio[0],
        event_loop,
        subprocess,
        spawned.stdin,
        &promise_for_stream,
    ) catch |e| {
        // ref_count = 2 from the aggregate above, but neither the JS
        // wrapper nor the process exit handler are wired up yet, so
        // release both. stdout/stderr are still `.ignore` — close the raw
        // spawned pipe handles directly since `Readable.init()` will not
        // run. `finalizeStreams()` here only closes `stdio_pipes` and the
        // pidfd; stdin/stdout/stderr are `.ignore` so their `closeIO` is a
        // no-op.
        if (Environment.isPosix) {
            if (fd := spawned.stdout) fd.close();
            if (fd := spawned.stderr) fd.close();
        } else {
            inline for (r of .{ spawned.stdout, spawned.stderr }) switch (r) {
                .buffer => |pipe| pipe.close(Subprocess.onPipeClose),
                .buffer_fd => |fd| fd.close(),
                .unavailable => {},
            };
        }
        subprocess.finalizeStreams();
        subprocess.process.detach();
        subprocess.process.deref();
        MaxBuf.removeFromSubprocess(&subprocess.stdout_maxbuf);
        MaxBuf.removeFromSubprocess(&subprocess.stderr_maxbuf);
        subprocess.deref();
        subprocess.deref();
        if (e == error.JSError) return error.JSError;
        return globalThis.throwOutOfMemory();
    };

    subprocess.stdout = Readable.init(
        stdio[1],
        event_loop,
        subprocess,
        spawned.stdout,
        jsc_vm.allocator,
        subprocess.stdout_maxbuf,
        is_sync,
    );
    subprocess.stderr = Readable.init(
        stdio[2],
        event_loop,
        subprocess,
        spawned.stderr,
        jsc_vm.allocator,
        subprocess.stderr_maxbuf,
        is_sync,
    );

    // For inline terminal options: close parent's slave_fd so EOF is received when child exits
    // For existing terminal: keep slave_fd open so terminal can be reused for more spawns
    if (info := terminal_info) {
        terminal_js_value = info.js_value;
        info.terminal.closeSlaveFd();
        subprocess.flags.owns_terminal = true;
        terminal_info = null;
    }
    // existing_terminal: don't close slave_fd - user manages lifecycle and can reuse

    subprocess.process.setExitHandler(subprocess);

    promise_for_stream.ensureStillAlive();
    subprocess.flags.is_stdin_a_readable_stream = promise_for_stream != .zero;

    if (promise_for_stream != .zero && !globalThis.hasException()) {
        if (e := promise_for_stream.toError()) {
            if (globalThis.throwValue(e) == error.JSError) {}
        }
    }

    if (globalThis.hasException()) {
        const e = globalThis.takeException(error.JSError);
        // Ensure we kill the process so we don't leave things in an unexpected state.
        * = subprocess.tryKill(subprocess.killSignal);

        if (globalThis.hasException()) {
            return error.JSError;
        }

        return globalThis.throwValue(e);
    }

    let posix_ipc_info: if (Environment.isPosix) IPC.Socket else void = undefined;
    if (Environment.isPosix && !is_sync) {
        if (mode := maybe_ipc_mode) {
            if (socket := jsc_vm.rareData().spawnIPCGroup(jsc_vm).fromFd(
                .spawn_ipc,
                null,
                @sizeOf(*IPC.SendQueue),
                posix_ipc_fd,
                true,
            )) {
                subprocess.ipc_data = IPC.Data.init(mode, .{ .subprocess = subprocess }, .uninitialized);
                posix_ipc_info = IPC.Socket.@"from"(socket);
            }
        }
    }

    if (*ipc_data := subprocess.ipc_data) {
        if (Environment.isPosix) {
            if (ctx := posix_ipc_info.ext(*IPC.SendQueue)) {
                ctx.* = &subprocess.ipc_data.?;
                subprocess.ipc_data.?.socket = .{ .open = posix_ipc_info };
            }
            // uws owns the fd now (owns_fd=1); neutralize the slot so finalizeStreams doesn't double-close.
            subprocess.stdio_pipes.items[i32(ipc_channel)] = .unavailable;
        } else {
            if (e := ipc_data.windowsConfigureServer(
                subprocess.stdio_pipes.items[i32(ipc_channel)].buffer,
            ).asErr()) {
                subprocess.deref();
                return globalThis.throwValue(e.toJS(globalThis)?);
            }
            subprocess.stdio_pipes.items[i32(ipc_channel)] = .unavailable;
        }
        ipc_data.writeVersionPacket(globalThis);
    }

    if (subprocess.stdin == .pipe && promise_for_stream == .zero) {
        subprocess.stdin.pipe.signal = jsc.WebCore.streams.Signal.init(&subprocess.stdin);
    }

    const out = if (comptime !is_sync)
        subprocess.toJS(globalThis)
    else
        JSValue.zero;
    if (out != .zero) {
        subprocess.this_value.setWeak(out);
        // Immediately upgrade to strong if there's pending activity to prevent premature GC
        subprocess.updateHasPendingActivity();
    }

    let send_exit_notification = false;

    if (comptime !is_sync) {
        // This must go before other things happen so that the exit handler is
        // registered before onProcessExit can potentially be called.
        if (timeout_val := timeout) {
            subprocess.event_loop_timer.next = bun.timespec.msFromNow(.allow_mocked_time, timeout_val);
            globalThis.bunVM().timer.insert(&subprocess.event_loop_timer);
            subprocess.setEventLoopTimerRefd(true);
        }

        bun.debugAssert(out != .zero);

        if (on_exit_callback.isCell()) {
            jsc.Codegen.JSSubprocess.onExitCallbackSetCached(out, globalThis, on_exit_callback);
        }
        if (on_disconnect_callback.isCell()) {
            jsc.Codegen.JSSubprocess.onDisconnectCallbackSetCached(out, globalThis, on_disconnect_callback);
        }
        if (ipc_callback.isCell()) {
            jsc.Codegen.JSSubprocess.ipcCallbackSetCached(out, globalThis, ipc_callback);
        }

        if (stdio[0] == .readable_stream) {
            jsc.Codegen.JSSubprocess.stdinSetCached(out, globalThis, stdio[0].readable_stream.value);
        }

        // Cache the terminal JS value if a terminal was created
        if (terminal_js_value != .zero) {
            jsc.Codegen.JSSubprocess.terminalSetCached(out, globalThis, terminal_js_value);
        }

        switch (subprocess.process.watch()) {
            .result => {},
            .err => {
                send_exit_notification = true;
                lazy = false;
            },
        }
    }

    defer {
        if (send_exit_notification) {
            if (subprocess.process.hasExited()) {
                // process has already exited, we called wait4(), but we did not call onProcessExit()
                subprocess.process.onExit(subprocess.process.status, &std.mem.zeroes(Rusage));
            } else {
                // process has already exited, but we haven't called wait4() yet
                // https://cs.github.com/libuv/libuv/blob/b00d1bd225b602570baee82a6152eaa823a84fa6/src/unix/process.c#L1007
                subprocess.process.wait(is_sync);
            }
        }
    }

    if (subprocess.stdin == .buffer) {
        if (e := subprocess.stdin.buffer.start().asErr()) {
            * = subprocess.tryKill(subprocess.killSignal);
            if (globalThis.throwValue(e.toJS(globalThis) catch return error.JSError) == error.JSError) {}
            return error.JSError;
        }
    }

    if (subprocess.stdout == .pipe) {
        if (e := subprocess.stdout.pipe.start(subprocess, event_loop).asErr()) {
            * = subprocess.tryKill(subprocess.killSignal);
            if (globalThis.throwValue(e.toJS(globalThis) catch return error.JSError) == error.JSError) {}
            return error.JSError;
        }
        if ((is_sync || !lazy) && subprocess.stdout == .pipe) {
            subprocess.stdout.pipe.readAll();
        }
    }

    if (subprocess.stderr == .pipe) {
        if (e := subprocess.stderr.pipe.start(subprocess, event_loop).asErr()) {
            * = subprocess.tryKill(subprocess.killSignal);
            if (globalThis.throwValue(e.toJS(globalThis) catch return error.JSError) == error.JSError) {}
            return error.JSError;
        }

        if ((is_sync || !lazy) && subprocess.stderr == .pipe) {
            subprocess.stderr.pipe.readAll();
        }
    }

    should_close_memfd = false;

    // Once everything is set up, we can add the abort listener
    // Adding the abort listener may call the onAbortSignal callback immediately if it was already aborted
    // Therefore, we must do this at the very end.
    if (signal := abort_signal) {
        signal.pendingActivityRef();
        subprocess.abort_signal = signal.addListener(subprocess, Subprocess.onAbortSignal);
        abort_signal = null;
    }

    if (comptime !is_sync) {
        if (!subprocess.process.hasExited()) {
            jsc_vm.onSubprocessSpawn(subprocess.process);
        }
        return out;
    }

    comptime bun.assert(is_sync);

    if (can_block_entire_thread_to_reduce_cpu_usage_in_fast_path) {
        jsc_vm.counters.mark(.spawnSync_blocking);
        const debug_timer = Output.DebugTimer.start();
        subprocess.process.wait(true);
        log("spawnSync fast path took {f}", .{ debug_timer });

        // watchOrReap will handle the already exited case for us.
    }

    switch (subprocess.process.watchOrReap()) {
        .result => {
            // Once everything is set up, we can add the abort listener
            // Adding the abort listener may call the onAbortSignal callback immediately if it was already aborted
            // Therefore, we must do this at the very end.
            if (signal := abort_signal) {
                signal.pendingActivityRef();
                subprocess.abort_signal = signal.addListener(subprocess, Subprocess.onAbortSignal);
                abort_signal = null;
            }
        },
        .err => {
            subprocess.process.wait(true);
        },
    }

    if (!subprocess.process.hasExited()) {
        jsc_vm.onSubprocessSpawn(subprocess.process);
    }

    let did_timeout = false;

    // Use the isolated event loop to tick instead of the main event loop
    // This ensures JavaScript timers don't fire and stdin/stdout from the main process aren't affected
    {
        let absolute_timespec = bun.timespec.epoch;
        let now = bun.timespec.now(.allow_mocked_time);
        let user_timespec: bun.timespec = absolute_timespec;
        if (timeout_ms := timeout) {
            user_timespec = now.addMs(timeout_ms);
        }

        // Support `AbortSignal.timeout`, but it's best-effort.
        // Specifying both `timeout: number` and `AbortSignal.timeout` chooses the soonest one.
        // This does mean if an AbortSignal times out it will throw
        if (signal := subprocess.abort_signal) {
            if (abort_signal_timeout := signal.getTimeout()) {
                if (abort_signal_timeout.event_loop_timer.state == .ACTIVE) {
                    if (user_timespec.eql(&bun.timespec.epoch) || abort_signal_timeout.event_loop_timer.next.order(&user_timespec) == .lt) {
                        user_timespec = abort_signal_timeout.event_loop_timer.next;
                    }
                }
            }
        }

        const has_user_timespec = !user_timespec.eql(&bun.timespec.epoch);

        const sync_loop = jsc_vm.rareData().spawnSyncEventLoop(jsc_vm);

        while (subprocess.computeHasPendingActivity()) {
            // Re-evaluate this at each iteration of the loop since it may change between iterations.
            let bun_test_timeout: bun.timespec = bun.timespec.epoch;
            if (runner := bun.jsc.Jest.Jest.runner) {
                bun_test_timeout = runner.getActiveTimeout();
            }
            const has_bun_test_timeout = !bun_test_timeout.eql(&bun.timespec.epoch);

            if (has_bun_test_timeout) {
                switch (bun_test_timeout.orderIgnoreEpoch(user_timespec)) {
                    .lt => absolute_timespec = bun_test_timeout,
                    .eq => {},
                    .gt => absolute_timespec = user_timespec,
                }
            } else if (has_user_timespec) {
                absolute_timespec = user_timespec;
            } else {
                absolute_timespec = bun.timespec.epoch;
            }
            const has_timespec = !absolute_timespec.eql(&bun.timespec.epoch);

            if (subprocess.stdin == .buffer) {
                subprocess.stdin.buffer.watch();
            }

            if (subprocess.stderr == .pipe) {
                subprocess.stderr.pipe.watch();
            }

            if (subprocess.stdout == .pipe) {
                subprocess.stdout.pipe.watch();
            }

            // Tick the isolated event loop without passing timeout to avoid blocking
            // The timeout check is done at the top of the loop
            switch (sync_loop.tickWithTimeout(if (has_timespec && !did_timeout) &absolute_timespec else null)) {
                .completed => {
                    now = bun.timespec.now(.allow_mocked_time);
                },
                .timeout => {
                    now = bun.timespec.now(.allow_mocked_time);
                    const did_user_timeout = has_user_timespec && (absolute_timespec.eql(&user_timespec) || user_timespec.order(&now) == .lt);

                    if (did_user_timeout) {
                        did_timeout = true;
                        * = subprocess.tryKill(subprocess.killSignal);
                    }

                    // Support bun:test timeouts AND spawnSync() timeout.
                    // There is a scenario where inside of spawnSync() a totally
                    // different test fails, and that SHOULD be okay.
                    if (has_bun_test_timeout) {
                        if (bun_test_timeout.order(&now) == .lt) {
                            let active_file_strong = bun.jsc.Jest.Jest.runner.?.bun_test_root.active_file
                                // TODO: add a .cloneNonOptional()?
                                .clone();

                            const _guard_active_file_strong = DeinitGuard<@TypeOf(active_file_strong)>{ ptr: unsafe { &active_file_strong } };
                            let taken_active_file = active_file_strong.take().?;
                            const _guard_taken_active_file = DeinitGuard<@TypeOf(taken_active_file)>{ ptr: unsafe { &taken_active_file } };

                            bun.jsc.Jest.Jest.runner.?.removeActiveTimeout(jsc_vm);

                            // This might internally call `std.c.kill` on this
                            // spawnSync process. Even if we do that, we still
                            // need to reap the process. So we may go through
                            // the event loop again, but it should wake up
                            // ~instantly so we can drain the events.
                            jsc.Jest.bun_test.BunTest.bunTestTimeoutCallback(taken_active_file, &absolute_timespec, jsc_vm);
                        }
                    }
                },
            }
        }
    }
    if (globalThis.hasException()) {
        // e.g. a termination exception.
        return JSValue.zero;
    }

    subprocess.updateHasPendingActivity();

    const signalCode = subprocess.getSignalCode(globalThis);
    const exitCode = subprocess.getExitCode(globalThis);
    const stdout = subprocess.stdout.toBufferedValue(globalThis)?;
    const stderr = subprocess.stderr.toBufferedValue(globalThis)?;
    const resource_usage: JSValue = if (!globalThis.hasException()) subprocess.createResourceUsageObject(globalThis)? else .zero;
    const exitedDueToTimeout = did_timeout;
    const exitedDueToMaxBuffer = subprocess.exited_due_to_maxbuf;
    const resultPid = jsc.JSValue.jsNumberFromInt32(subprocess.pid());
    subprocess.finalize();

    const sync_value = jsc.JSValue.createEmptyObject(globalThis, 0);
    sync_value.put(globalThis, jsc.ZigString.static("exitCode"), exitCode);
    if (!signalCode.isEmptyOrUndefinedOrNull()) {
        sync_value.put(globalThis, jsc.ZigString.static("signalCode"), signalCode);
    }
    sync_value.put(globalThis, jsc.ZigString.static("stdout"), stdout);
    sync_value.put(globalThis, jsc.ZigString.static("stderr"), stderr);
    sync_value.put(globalThis, jsc.ZigString.static("success"), JSValue.jsBoolean(exitCode.isInt32() && exitCode.asInt32() == 0));
    sync_value.put(globalThis, jsc.ZigString.static("resourceUsage"), resource_usage);
    if (timeout != null) sync_value.put(globalThis, jsc.ZigString.static("exitedDueToTimeout"), if (exitedDueToTimeout) .true else .false);
    if (maxBuffer != null) sync_value.put(globalThis, jsc.ZigString.static("exitedDueToMaxBuffer"), if (exitedDueToMaxBuffer != null) .true else .false);
    sync_value.put(globalThis, jsc.ZigString.static("pid"), resultPid);

    return sync_value;
}

fn throwCommandNotFound(globalThis: *jsc.JSGlobalObject, command: []const u8): bun.JSError {
    const e = jsc.SystemError{
        message: bun.handleOom(bun.String.createFormat("Executable not found in $PATH: \"{s}\"", .{ command })),
        code: bun.String.static("ENOENT"),
        errno: -(bun.sys.UV_E.NOENT),
        path: bun.String.cloneUTF8(command),
    };
    return globalThis.throwValue(e.toErrorInstance(globalThis));
}

pub fn appendEnvpFromJS(globalThis: *jsc.JSGlobalObject, object: *jsc.JSObject, envp: *std.array_list.Managed(NullableStringZ), PATH: *[]const u8): bun.JSError!void {
    let object_iter = jsc.JSPropertyIterator(.{ .skip_empty_name = false, .include_value = true }).init(globalThis, object)?;
    const _guard_object_iter = DeinitGuard<@TypeOf(object_iter)>{ ptr: unsafe { &object_iter } };

    envp.ensureTotalCapacityPrecise(object_iter.len +
        // +1 incase there's IPC
        // +1 for null terminator
        2)?;
    while (key := object_iter.next()?) {
        let value = object_iter.value;
        if (value.isUndefined()) continue;

        let value_bunstr = value.toBunString(globalThis)?;
        const _guard_value_bunstr = DerefGuard<@TypeOf(value_bunstr)>{ ptr: unsafe { &value_bunstr } };

        // Check for null bytes in env key and value (security: prevent null byte injection)
        if (key.indexOfAsciiChar(0) != null) {
            return globalThis.ERR(.INVALID_ARG_VALUE, "The property 'options.env['{f}']' must be a string without null bytes. Received \"{f}\"", .{ key.toZigString(), key.toZigString() }).throw();
        }
        if (value_bunstr.indexOfAsciiChar(0) != null) {
            return globalThis.ERR(.INVALID_ARG_VALUE, "The property 'options.env['{f}']' must be a string without null bytes. Received \"{f}\"", .{ key.toZigString(), value_bunstr.toZigString() }).throw();
        }

        const line = std.fmt.allocPrintSentinel(envp.allocator, "{f}={f}", .{ key, value_bunstr.toZigString() }, 0)?;

        if (key.eqlComptime("PATH")) {
            unsafe {
                PATH.* = bun.asByteSlice(line["PATH=".len..]);
            }
        }

        envp.append(line)?;
    }
}

const log = Output.scoped(.Subprocess, .hidden);
@c {
    const BUN_DEFAULT_PATH_FOR_SPAWN: [*:0]const u8;
}

import IPC from "../../../jsc/ipc.zig";
import Terminal from "./Terminal.zig";
const Allocator = std.mem.Allocator;

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const SignalCode = bun.SignalCode;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const uws = bun.uws;
const which = bun.which;
const windows = bun.windows;
const MaxBuf = bun.io.MaxBuf;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const Subprocess = jsc.Subprocess;
const Readable = Subprocess.Readable;
const Writable = Subprocess.Writable;

const Process = bun.spawn.Process;
const Rusage = bun.spawn.Rusage;
const Stdio = bun.spawn.Stdio;
