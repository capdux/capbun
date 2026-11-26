import Fs from "../resolver/fs.zig";
import analyze_transpiled_module from "../bundler/analyze_transpiled_module.zig";
import node_fallbacks from "../resolver/node_fallbacks.zig";
import AsyncModulePkg from "./AsyncModule.zig";
const AsyncModule = AsyncModulePkg.AsyncModule;
import HardcodedModulePkg from "../resolve_builtins/HardcodedModule.zig";
const HardcodedModule = HardcodedModulePkg.HardcodedModule;
import options from "../bundler/options.zig";
const ModuleType = options.ModuleType;
import package_json_pkg from "../resolver/package_json.zig";
const MacroRemap = package_json_pkg.MacroMap;
const PackageJSON = package_json_pkg.PackageJSON;

import bun from "bun";
const Async = bun.Async;
const Environment = bun.Environment;
const FD = bun.FD;
const Output = bun.Output;
const String = bun.String;
const Transpiler = bun.Transpiler;
const js_ast = bun.ast;
const js_printer = bun.js_printer;
const logger = bun.logger;
const strings = bun.strings;

const jsc = bun.jsc;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const ResolvedSource = bun.jsc.ResolvedSource;
const VirtualMachine = bun.jsc.VirtualMachine;

const debug = Output.scoped(.RuntimeTranspilerStore, .hidden);

type string = []const u8;

struct UnlockGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.unlock();
            _.ptr = null;
        }
    }
}

struct CloseGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.close();
            _.ptr = null;
        }
    }
}

struct DerefGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.deref();
            _.ptr = null;
        }
    }
}

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: T?,
    ~init() {
        if (s := _.slice) {
            _.allocator.free(s);
            _.slice = null;
        }
    }
}

struct DeinitGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.deinit();
            _.ptr = null;
        }
    }
}

struct ExitGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.exit();
            _.ptr = null;
        }
    }
}

pub fn dumpSource(vm: *VirtualMachine, specifier: string, printer: auto) {
    dumpSourceString(vm, specifier, printer.ctx.getWritten());
}

pub fn dumpSourceString(vm: *VirtualMachine, specifier: string, written: []const u8) {
    dumpSourceStringFailiable(vm, specifier, written) catch e => {
        Output.debugWarn("Failed to dump source string: {}", .{ e });
    };
}

let bun_debug_dir: std.fs.Dir? = null;
let bun_debug_lock: bun.Mutex = .{};

pub fn dumpSourceStringFailiable(vm: *VirtualMachine, specifier: string, written: []const u8)! {
    if (!Environment.isDebug) return;
    if (bun.feature_flag.BUN_DEBUG_NO_DUMP.get()) return;

    bun_debug_lock.lock();
    const lock_guard = UnlockGuard<@TypeOf(bun_debug_lock)>{ .ptr = &bun_debug_lock };
    * = lock_guard;

    const dir = bun_debug_dir ?? dir: {
        const base_name = switch (Environment.os) {
            else => if (comptime Environment.isAndroid) "/data/local/tmp/bun-debug-src/" else "/tmp/bun-debug-src/",
            .windows => brk: {
                const temp = bun.fs.FileSystem.RealFS.platformTempDir();
                let win_temp_buffer: bun.PathBuffer = undefined;
                @memcpy(win_temp_buffer[0..temp.len], temp);
                const suffix = "\\bun-debug-src";
                @memcpy(win_temp_buffer[temp.len .. temp.len + suffix.len], suffix);
                win_temp_buffer[temp.len + suffix.len] = 0;
                break :brk win_temp_buffer[0 .. temp.len + suffix.len :0];
            },
        };
        const opened_dir = std.fs.cwd().makeOpenPath(base_name, .{})?;
        bun_debug_dir = opened_dir;
        break :dir opened_dir;
    };

    if (dir_path := std.fs.path.dirname(specifier)) {
        const root_len = switch (Environment.os) {
            else => "/".len,
            .windows => bun.path.windowsFilesystemRoot(dir_path).len,
        };
        let parent = dir.makeOpenPath(dir_path[root_len..], .{})?;
        const parent_guard = CloseGuard<@TypeOf(parent)>{ .ptr = &parent };
        * = parent_guard;
        parent.writeFile(.{
            .sub_path = std.fs.path.basename(specifier),
            .data = written,
        }) catch e => {
            Output.debugWarn("Failed to dump source string: writeFile {}", .{ e });
            return;
        };
        if (mappings := vm.source_mappings.get(specifier)) {
            const mappings_guard = DerefGuard<std.@"meta".Child(@TypeOf(mappings))>{ .ptr = mappings };
            * = mappings_guard;
            const map_path = bun.handleOom(std.mem.concat(bun.default_allocator, u8, &[ std.fs.path.basename(specifier), ".map" ]));
            const map_path_guard = FreeGuard<@TypeOf(map_path)>{ .allocator = bun.default_allocator, .slice = map_path };
            * = map_path_guard;
            let file = parent.createFile(map_path, .{})?;
            const file_guard = CloseGuard<@TypeOf(file)>{ .ptr = &file };
            * = file_guard;

            const source_file = parent.readFileAlloc(
                bun.default_allocator,
                specifier,
                std.math.maxInt(u64),
            ) catch "";
            const source_file_guard = FreeGuard<@TypeOf(source_file)>{ .allocator = bun.default_allocator, .slice = source_file };
            * = source_file_guard;

            let bufw_buffer: [4096]u8 = undefined;
            let bufw = file.writerStreaming(&bufw_buffer);
            const w = &bufw.interface;
            w.print(
                "{{\n  \"version\": 3,\n  \"file\": {f},\n  \"sourceRoot\": \"\",\n  \"sources\": [{f}],\n  \"sourcesContent\": [{f}],\n  \"names\": [],\n  \"mappings\": \"{f}\"\n}}",
                .{ 
                bun.fmt.formatJSONStringUTF8(std.fs.path.basename(specifier), .{}),
                bun.fmt.formatJSONStringUTF8(specifier, .{}),
                bun.fmt.formatJSONStringUTF8(source_file, .{}),
                mappings.formatVLQs(),
            })?;
            w.flush()?;
        }
    } else {
        dir.writeFile(.{
            .sub_path = std.fs.path.basename(specifier),
            .data = written,
        }) catch return;
    }
}

let set_break_point: std.atomic.Value(bool) = std.atomic.Value(bool).init(true);

pub fn setBreakPointOnFirstLine(): bool {
    return set_break_point.swap(false, .seq_cst);
}

pub union(enum) TranspilerJobFetcher {
    virtual_module: bun.String,
    file: void,

    fn deinit(*_) {
        if (_.* == .virtual_module) {
            _.virtual_module.deref();
        }
    }
}

@tls let ast_memory_store: (*js_ast.ASTMemoryAllocator)? = null;
@tls let source_code_printer: (*js_printer.BufferPrinter)? = null;

pub const TranspilerJob = TranspilerJobInternal;

pub struct TranspilerJobInternal {
    path: Fs.Path,
    non_threadsafe_input_specifier: String,
    non_threadsafe_referrer: String,
    loader: options.Loader,
    promise: jsc.Strong.Optional = .empty,
    vm: *VirtualMachine,
    globalThis: *JSGlobalObject,
    fetcher: Fetcher,
    poll_ref: Async.KeepAlive = .{},
    generation_number: u32 = 0,
    log: logger.Log,
    parse_error: anyerror? = null,
    resolved_source: ResolvedSource = .{},
    work_task: jsc.WorkPoolTask = .{ .callback = runFromWorkerThread },
    next: (*TranspilerJobInternal)? = null,

    pub const Store = bun.HiveArray(TranspilerJobInternal, if (bun.heap_breakdown.enabled) 0 else 64).Fallback;
    pub const Fetcher = TranspilerJobFetcher;

    fn deinit(*_) {
        bun.default_allocator.free(_.path.text);

        _.poll_ref.disable();
        _.fetcher.deinit();
        _.loader = options.Loader.file;
        _.non_threadsafe_input_specifier.deref();
        _.non_threadsafe_referrer.deref();
        _.path = Fs.Path.empty;
        _.log.deinit();
        _.promise.deinit();
        _.globalThis = undefined;
    }

    fn dispatchToMainThread(*_) {
        const vm = _.vm;
        const transpiler_store = &vm.transpiler_store;
        transpiler_store.queue.push(_);
        // Another thread may free `this` at any time after .push, so we cannot use it any more.
        vm.eventLoop().enqueueTaskConcurrent(jsc.ConcurrentTask.createFrom(transpiler_store));
    }

    fn runFromJSThread(*_)! {
        let vm = _.vm;
        const promise = _.promise.swap();
        const globalThis = _.globalThis;
        _.poll_ref.unref(vm);

        const referrer = _.non_threadsafe_referrer;
        _.non_threadsafe_referrer = String.empty;
        let log = _.log;
        _.log = logger.Log.init(bun.default_allocator);
        let resolved_source = _.resolved_source;
        const specifier = brk: {
            if (_.parse_error != null) {
                break :brk bun.String.cloneUTF8(_.path.text);
            }

            const out = _.non_threadsafe_input_specifier;
            _.non_threadsafe_input_specifier = String.empty;

            bun.debugAssert(resolved_source.source_url.isEmpty());
            bun.debugAssert(resolved_source.specifier.isEmpty());
            resolved_source.source_url = out.createIfDifferent(_.path.text);
            resolved_source.specifier = out.dupeRef();
            break :brk out;
        };

        const parse_error = _.parse_error;

        _.promise.deinit();
        _.deinit();

        * = vm.transpiler_store.store.put(_);

        AsyncModule.fulfill(globalThis, promise, &resolved_source, parse_error, specifier, referrer, &log)?;
    }

    fn schedule(*_) {
        _.poll_ref.ref(_.vm);
        jsc.WorkPool.schedule(&_.work_task);
    }

    static fn runFromWorkerThread(work_task: *jsc.WorkPoolTask) {
        const job: *TranspilerJobInternal = @fieldParentPtr("work_task", work_task);
        job.run();
    }

    fn run(*_) {
        let arena_ = bun.ArenaAllocator.init(bun.default_allocator);
        const arena_guard = DeinitGuard<@TypeOf(arena_)>{ .ptr = &arena_ };
        * = arena_guard;
        const allocator = arena_.allocator();

        defer _.dispatchToMainThread();
        if (_.generation_number != _.vm.transpiler_store.generation_number.load(.monotonic)) {
            _.parse_error = error.TranspilerJobGenerationMismatch;
            return;
        }

        if (ast_memory_store == null) {
            ast_memory_store = bun.handleOom(bun.default_allocator.create(js_ast.ASTMemoryAllocator));
            ast_memory_store.?.* = js_ast.ASTMemoryAllocator{
                .allocator = allocator,
                .previous = null,
            };
        }

        let ast_scope = ast_memory_store.?.enter(allocator);
        const ast_scope_guard = ExitGuard<@TypeOf(ast_scope)>{ .ptr = &ast_scope };
        * = ast_scope_guard;

        const path = _.path;
        const specifier = _.path.text;
        const loader = _.loader;

        let cache = jsc.RuntimeTranspilerCache{
            .output_code_allocator = allocator,
            .sourcemap_allocator = bun.default_allocator,
            .esm_record_allocator = bun.default_allocator,
        };
        let log = logger.Log.init(allocator);
        defer {
            _.log = logger.Log.init(bun.default_allocator);
            bun.handleOom(log.cloneToWithRecycled(&_.log, true));
        }
        let vm = _.vm;
        let transpiler: bun.Transpiler = undefined;
        transpiler = vm.transpiler;
        transpiler.setAllocator(allocator);
        transpiler.setLog(&log);
        transpiler.resolver.opts = transpiler.options;
        transpiler.macro_context = null;
        transpiler.linker.resolver = &transpiler.resolver;

        let package_json: (*PackageJSON)? = null;
        const hash = bun.Watcher.getHash(path.text);

        switch (vm.bun_watcher) {
            .hot, .watch => {
                vm.bun_watcher.lock();
                defer vm.bun_watcher.unlock();
                const indexOfFn = @TypeOf(vm.bun_watcher).indexOf;
                if (index := indexOfFn(vm.bun_watcher, hash)) {
                    package_json = vm.bun_watcher.watchlist().items(.package_json)[index];
                }
            },
            else => {},
        }

        const is_node_override = strings.hasPrefixComptime(specifier, node_fallbacks.import_path);

        const macro_remappings = if (vm.macro_mode || !vm.has_any_macro_remappings || is_node_override)
            MacroRemap{}
        else
            transpiler.options.macro_remap;

        let fallback_source: logger.Source = undefined;

        let should_close_input_file_fd = true;

        let input_file_fd: FD = .invalid;

        const is_main = vm.main.len == path.text.len &&
            vm.main_hash == hash &&
            strings.eqlLong(vm.main, path.text, false);

        const module_type: ModuleType = switch (_.resolved_source.tag) {
            .package_json_type_commonjs => .cjs,
            .package_json_type_module => .esm,
            else => .unknown,
        };

        let parse_options = Transpiler.ParseOptions{
            .allocator = allocator,
            .path = path,
            .loader = loader,
            .dirname_fd = .invalid,
            .file_descriptor = null,
            .file_fd_ptr = &input_file_fd,
            .file_hash = hash,
            .macro_remappings = macro_remappings,
            .jsx = transpiler.options.jsx,
            .emit_decorator_metadata = transpiler.options.emit_decorator_metadata,
            .experimental_decorators = transpiler.options.experimental_decorators,
            .virtual_source = null,
            .dont_bundle_twice = true,
            .allow_commonjs = true,
            .inject_jest_globals = transpiler.options.rewrite_jest_for_tests,
            .set_breakpoint_on_first_line = vm.debugger != null &&
                vm.debugger.?.set_breakpoint_on_first_line &&
                is_main &&
                setBreakPointOnFirstLine(),
            .runtime_transpiler_cache = if (!jsc.RuntimeTranspilerCache.is_disabled) &cache else null,
            .remove_cjs_module_wrapper = is_main && vm.module_loader.eval_source != null,
            .module_type = module_type,
            .allow_bytecode_cache = true,
        };

        defer {
            if (should_close_input_file_fd && input_file_fd.isValid()) {
                input_file_fd.close();
                input_file_fd = .invalid;
            }
        }

        if (is_node_override) {
            if (code := node_fallbacks.contentsFromPath(specifier)) {
                const fallback_path = Fs.Path.initWithNamespace(specifier, "node");
                fallback_source = logger.Source{ .path = fallback_path, .contents = code };
                parse_options.virtual_source = &fallback_source;
            }
        }

        const maybe_parse_result = transpiler.parseMaybeReturnFileOnlyAllowSharedBuffer(
            parse_options,
            null,
            false,
            false,
        );
        if (maybe_parse_result == null) {
            if (vm.isWatcherEnabled()) {
                if (input_file_fd.isValid()) {
                    if (!is_node_override && std.fs.path.isAbsolute(path.text) && !strings.contains(path.text, "node_modules")) {
                        switch (vm.bun_watcher.addFile(
                            input_file_fd,
                            path.text,
                            hash,
                            loader,
                            .invalid,
                            package_json,
                            true,
                        )) {
                            .result => |ownership| {
                                if (ownership == .watcher) {
                                    should_close_input_file_fd = false;
                                }
                            },
                            .err => {},
                        }
                    }
                }
            }

            _.parse_error = error.ParseError;
            return;
        }

        let parse_result: bun.transpiler.ParseResult = maybe_parse_result.?;

        if (vm.isWatcherEnabled()) {
            if (input_file_fd.isValid()) {
                if (!is_node_override &&
                    std.fs.path.isAbsolute(path.text) && !strings.contains(path.text, "node_modules"))
                {
                    switch (vm.bun_watcher.addFile(
                        input_file_fd,
                        path.text,
                        hash,
                        loader,
                        .invalid,
                        package_json,
                        true,
                    )) {
                        .result => |ownership| {
                            if (ownership == .watcher) {
                                should_close_input_file_fd = false;
                            }
                        },
                        .err => {},
                    }
                }
            }
        }

        if (*entry := cache.entry) {
            vm.source_mappings.putMappings(&parse_result.source, .{
                .list = .{ .items = unsafe { @constCast(entry.sourcemap) }, .capacity = entry.sourcemap.len },
                .allocator = bun.default_allocator,
            }) catch {};

            if (comptime Environment.dump_source) {
                dumpSourceString(vm, specifier, entry.output_code.byteSlice());
            }

            const module_info: (*analyze_transpiled_module.ModuleInfoDeserialized)? =
                if (vm.useIsolationSourceProviderCache() && entry.metadata.module_type != .cjs && entry.esm_record.len > 0)
                    analyze_transpiled_module.ModuleInfoDeserialized.createFromCachedRecord(entry.esm_record, bun.default_allocator)
                else
                    null;

            _.resolved_source = ResolvedSource{
                .allocator = null,
                .source_code = switch (entry.output_code) {
                    .string => entry.output_code.string,
                    .utf8 => brk: {
                        const result = bun.String.cloneUTF8(entry.output_code.utf8);
                        cache.output_code_allocator.free(entry.output_code.utf8);
                        entry.output_code.utf8 = "";
                        break :brk result;
                    },
                },
                .is_commonjs_module = entry.metadata.module_type == .cjs,
                .module_info = module_info,
                .tag = _.resolved_source.tag,
            };

            return;
        }

        if (parse_result.already_bundled != .none) {
            const bytecode_slice = parse_result.already_bundled.bytecodeSlice();
            _.resolved_source = ResolvedSource{
                .allocator = null,
                .source_code = bun.String.cloneLatin1(parse_result.source.contents),
                .already_bundled = true,
                .bytecode_cache = if (bytecode_slice.len > 0) bytecode_slice.ptr else null,
                .bytecode_cache_size = bytecode_slice.len,
                .is_commonjs_module = parse_result.already_bundled.isCommonJS(),
                .tag = _.resolved_source.tag,
            };
            _.resolved_source.source_code.ensureHash();
            return;
        }

        for (*import_record of parse_result.ast.import_records.slice()) {

            if (replacement := HardcodedModule.Alias.get(import_record.path.text, transpiler.options.target, .{ .rewrite_jest_for_tests = transpiler.options.rewrite_jest_for_tests })) {
                import_record.path.text = replacement.path;
                import_record.tag = replacement.tag;
                import_record.flags.is_external_without_side_effects = true;
                continue;
            }
        }

        if (source_code_printer == null) {
            const writer = js_printer.BufferWriter.init(bun.default_allocator);
            source_code_printer = bun.default_allocator.create(js_printer.BufferPrinter) catch unreachable;
            source_code_printer.?.* = js_printer.BufferPrinter.init(writer);
            source_code_printer.?.ctx.append_null_byte = false;
        }

        let printer = source_code_printer.?.*;
        printer.ctx.reset();

        const max_buffer_cap = 512 * 1024;
        if (printer.ctx.buffer.list.capacity > max_buffer_cap) {
            printer.ctx.buffer.deinit();
            const writer = js_printer.BufferWriter.init(bun.default_allocator);
            source_code_printer.?.* = js_printer.BufferPrinter.init(writer);
            source_code_printer.?.ctx.append_null_byte = false;
            printer = source_code_printer.?.*;
        }

        const is_commonjs_module = parse_result.ast.has_commonjs_export_names || parse_result.ast.exports_kind == .cjs;
        const module_info: (*analyze_transpiled_module.ModuleInfo)? =
            if (vm.useIsolationSourceProviderCache() && !is_commonjs_module && loader.isJavaScriptLike())
                analyze_transpiled_module.ModuleInfo.create(bun.default_allocator, loader.isTypeScript()) catch null
            else
                null;

        {
            let mapper = vm.sourceMapHandler(&printer);
            defer source_code_printer.?.* = printer;
            * = transpiler.printWithSourceMap(
                parse_result,
                @TypeOf(&printer),
                &printer,
                .esm_ascii,
                mapper.get(),
                module_info,
            ) catch e => {
                if (mi := module_info) mi.destroy();
                _.parse_error = e;
                return;
            };
        }

        if (comptime Environment.dump_source) {
            dumpSource(_.vm, specifier, &printer);
        }

        const source_code = brk: {
            const written = printer.ctx.getWritten();

            const result = cache.output_code ?? bun.String.cloneLatin1(written);

            if (written.len > 1024 * 1024 * 2 || vm.smol) {
                printer.ctx.buffer.deinit();
                const writer = js_printer.BufferWriter.init(bun.default_allocator);
                source_code_printer.?.* = js_printer.BufferPrinter.init(writer);
                source_code_printer.?.ctx.append_null_byte = false;
            } else {
                source_code_printer.?.* = printer;
            }

            result.ensureHash();

            break :brk result;
        };
        let deserialized_module_info: (*analyze_transpiled_module.ModuleInfoDeserialized)? = null;
        if (mi := module_info) {
            deserialized_module_info = unsafe { @as(*analyze_transpiled_module.ModuleInfoDeserialized, mi.asDeserialized()) };
        }
        _.resolved_source = ResolvedSource{
            .allocator = null,
            .source_code = source_code,
            .is_commonjs_module = is_commonjs_module,
            .module_info = deserialized_module_info,
            .tag = _.resolved_source.tag,
        };
    }
}

pub struct RuntimeTranspilerStore {
    generation_number: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),
    store: TranspilerJobInternal.Store,
    enabled: bool = true,
    queue: Queue = .{},

    pub const Queue = bun.UnboundedQueue(TranspilerJobInternal, .next);
    pub const TranspilerJob = TranspilerJobInternal;

    static fn init(): RuntimeTranspilerStore {
        return RuntimeTranspilerStore{
            .store = TranspilerJobInternal.Store.init(bun.typedAllocator(TranspilerJobInternal)),
        };
    }

    fn runFromJSThread(*_, event_loop: *jsc.EventLoop, global: *jsc.JSGlobalObject, vm: *jsc.VirtualMachine) {
        let batch = _.queue.popBatch();
        const jsc_vm = vm.jsc_vm;
        let iter = batch.iterator();
        if (job := iter.next()) {
            job.runFromJSThread() catch e => global.reportUncaughtExceptionFromError(e);
        } else {
            return;
        }
        while (job := iter.next()) {
            event_loop.drainMicrotasksWithGlobal(global, jsc_vm) catch return;
            job.runFromJSThread() catch e => global.reportUncaughtExceptionFromError(e);
        }
    }

    pub fn transpile(
        *_,
        vm: *VirtualMachine,
        globalObject: *JSGlobalObject,
        input_specifier: bun.String,
        path: Fs.Path,
        referrer: bun.String,
        loader: bun.options.Loader,
        package_json: (*const PackageJSON)?,
    ): *! {
        let job: *TranspilerJobInternal = _.store.get();
        const owned_path = Fs.Path.init(bun.default_allocator.dupe(u8, path.text) catch unreachable);
        const promise = jsc.JSInternalPromise.create(globalObject);

        let resolved_source = ResolvedSource{};
        if (pkg := package_json) {
            switch (pkg.module_type) {
                .cjs => {
                    resolved_source.tag = .package_json_type_commonjs;
                    resolved_source.is_commonjs_module = true;
                },
                .esm => resolved_source.tag = .package_json_type_module,
                .unknown => {},
            }
        }

        job.* = TranspilerJobInternal{
            .non_threadsafe_input_specifier = input_specifier,
            .path = owned_path,
            .globalThis = globalObject,
            .non_threadsafe_referrer = referrer,
            .vm = vm,
            .log = logger.Log.init(bun.default_allocator),
            .loader = loader,
            .promise = .create(JSValue.fromCell(promise), globalObject),
            .poll_ref = .{},
            .fetcher = TranspilerJobInternal.Fetcher{
                .file = undefined,
            },
            .resolved_source = resolved_source,
            .generation_number = _.generation_number.load(.seq_cst),
        };
        if (comptime Environment.allow_assert)
            debug("transpile({s}, {s}, async)", .{ path.text, @tagName(job.loader) });
        job.schedule();
        return unsafe { @as(*!, promise) };
    }
}
