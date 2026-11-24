import node_fallbacks from "../resolver/node_fallbacks.zig";
import Fs from "../resolver/fs.zig";
import Runtime from "../js_parser/runtime.zig";
import analyze_transpiled_module from "../bundler/analyze_transpiled_module.zig";
import ast from "../options_types/import_record.zig";
import node_module_module from "./NodeModuleModule.zig";
import options from "../bundler/options.zig";
import bun from "bun";
import async_module_mod from "./AsyncModule.zig";
import runtime_transpiler_store_mod from "./RuntimeTranspilerStore.zig";
import hardcoded_module_mod from "../resolve_builtins/HardcodedModule.zig";
import package_json_mod from "../resolver/package_json.zig";

pub const AsyncModule = async_module_mod.AsyncModule;
pub const RuntimeTranspilerStore = runtime_transpiler_store_mod.RuntimeTranspilerStore;
pub const HardcodedModule = hardcoded_module_mod.HardcodedModule;

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

struct DeinitValGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

struct OptDeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe {
            if (*v := _.ptr.*) v.deinit();
        }
    }
}

struct PathBufferPoolGuard {
    buf: *bun.PathBuffer!,
    ~init() {
        bun.path_buffer_pool.put(_.buf);
    }
}

struct CloseFdGuard {
    fd: FD,
    ~init() {
        _.fd.close();
    }
}

struct AstScopeGuard {
    scope: *js_ast.ASTMemoryAllocator.Scope!,
    ~init() {
        _.scope.exit();
    }
}

struct ArenaReturnGuard {
    give_back: *bool!,
    arena_ptr: (*bun.ArenaAllocator)?,
    jsc_vm: *VirtualMachine!,
    free_all: bool,
    ~init() {
        if (unsafe { _.give_back.* }) {
            if (_.jsc_vm.module_loader.transpile_source_code_arena == null) {
                if (_.free_all) {
                    if (_.jsc_vm.smol) {
                        * = _.arena_ptr.?.reset(.free_all);
                    } else {
                        * = _.arena_ptr.?.reset({ retain_with_limit: 8 * 1024 * 1024 });
                    }
                }
                _.jsc_vm.module_loader.transpile_source_code_arena = _.arena_ptr;
            } else {
                _.arena_ptr.?.deinit();
                _.jsc_vm.allocator.destroy(_.arena_ptr.?);
            }
        }
    }
}

struct InputFdGuard {
    should_close: *bool,
    fd: *FD!,
    ~init() {
        unsafe {
            if (_.should_close.* && _.fd.* != bun.invalid_fd) {
                _.fd.*.close();
                _.fd.* = bun.invalid_fd;
            }
        }
    }
}

struct ModuleInfoErrGuard {
    mi: (*analyze_transpiled_module.ModuleInfo)?,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            if (m := _.mi) m.destroy();
        }
    }
}

struct ResetArenaGuard {
    vm: *VirtualMachine!,
    ~init() {
        _.vm.module_loader.resetArena(_.vm);
    }
}

@root struct ModuleLoader {
    transpile_source_code_arena: (*bun.ArenaAllocator)? = null,
    eval_source: (*logger.Source)? = null,

    /// This must be called after calling transpileSourceCode
    pub fn resetArena(*_, jsc_vm: *VirtualMachine) {
        bun.assert(&jsc_vm.module_loader == _);
        if (arena_ := _.transpile_source_code_arena) {
            if (jsc_vm.smol) {
                * = arena_.reset(.free_all);
            } else {
                * = arena_.reset({ retain_with_limit: 8 * 1024 * 1024 });
            }
        }
    }
}

comptime {
    * = Bun__transpileVirtualModule;
    * = Bun__runVirtualModule;
    * = Bun__transpileFile;
    * = Bun__fetchBuiltinModule;
    * = Bun__getDefaultLoader;
}

pub let is_allowed_to_use_internal_testing_apis = false;

pub fn resolveEmbeddedFile(vm: *VirtualMachine, path_buf: *bun.PathBuffer, input_path: []const u8, extname: []const u8): ([]const u8)? {
    if (input_path.len == 0) return null;
    if (vm.standalone_module_graph == null) return null;
    let graph = vm.standalone_module_graph.?;
    const file_opt = graph.find(input_path);
    if (file_opt == null) return null;
    const file = file_opt.?;

    if (comptime Environment.isLinux) {
        // TODO: use /proc/fd/12346 instead! Avoid the copy!
    }

    // atomically write to a tmpfile and then move it to the final destination
    const tmpname_buf = bun.path_buffer_pool.get();
    const _guard_tmpname = PathBufferPoolGuard{ buf: tmpname_buf };
    const tmpfilename = bun.fs.FileSystem.tmpname(extname, tmpname_buf, bun.hash(file.name)) catch return null;

    const tmpdir: bun.FD = .fromStdDir(bun.fs.FileSystem.instance.tmpdir() catch return null);

    // First we open the tmpfile, to avoid any other work in the event of failure.
    const tmpfile = bun.Tmpfile.create(tmpdir, tmpfilename).unwrap() catch return null;
    const _guard_tmpfile = CloseFdGuard{ fd: tmpfile.fd };

    switch (bun.api.node.fs.NodeFS.writeFileWithPathBuffer(
        tmpname_buf, // not used

        {
            data: {
                encoded_slice: ZigString.Slice.fromUTF8NeverFree(file.contents),
            },
            dirfd: tmpdir,
            file: { fd: tmpfile.fd },
            encoding: .buffer,
        },
    )) {
        .err => {
            return null;
        },
        else => {},
    }
    return bun.path.joinAbsStringBuf(bun.fs.FileSystem.RealFS.tmpdirPath(), path_buf, &[tmpfilename], .auto);
}

@export @c fn Bun__getDefaultLoader(global: *JSGlobalObject, str: *const bun.String): api.Loader {
    let jsc_vm = global.bunVM();
    const filename = str.toUTF8(jsc_vm.allocator);
    const _guard_filename = DeinitValGuard<@TypeOf(filename)>{ val: filename };
    const loader = jsc_vm.transpiler.options.loader(Fs.PathName.init(filename.slice()).ext).toAPI();
    if (loader == .file) {
        return api.Loader.js;
    }

    return loader;
}

pub fn transpileSourceCode(
    jsc_vm: *VirtualMachine,
    specifier: string,
    referrer: string,
    input_specifier: String,
    path: Fs.Path,
    loader: options.Loader,
    module_type: options.ModuleType,
    log: *logger.Log,
    virtual_source: (*const logger.Source)?,
    promise_ptr: ((*(*jsc.JSInternalPromise)?))?,
    source_code_printer: *js_printer.BufferPrinter,
    globalObject: (*JSGlobalObject)?,
    comptime flags: FetchFlags,
): !ResolvedSource {
    const disable_transpilying = comptime flags.disableTranspiling();

    if (comptime disable_transpilying) {
        if (!(loader.isJavaScriptLike() || loader == .toml || loader == .yaml || loader == .json5 || loader == .text || loader == .json || loader == .jsonc)) {
            // Don't print "export default <file path>"
            return ResolvedSource{
                allocator: null,
                source_code: bun.String.empty,
                specifier: input_specifier,
                source_url: input_specifier.createIfDifferent(path.text),
            };
        }
    }

    switch (loader) {
        .js, .jsx, .ts, .tsx, .json, .jsonc, .toml, .yaml, .json5, .text, .md => {
            // Ensure that if there was an ASTMemoryAllocator in use, it's not used anymore.
            let ast_scope = js_ast.ASTMemoryAllocator.Scope{};
            ast_scope.enter();
            const _guard_ast_scope = AstScopeGuard{ scope: &ast_scope };

            jsc_vm.transpiled_count += 1;
            jsc_vm.transpiler.resetStore();
            const hash = bun.Watcher.getHash(path.text);
            const is_main = jsc_vm.main.len == path.text.len &&
                jsc_vm.main_hash == hash &&
                strings.eqlLong(jsc_vm.main, path.text, false);

            let arena_: (*bun.ArenaAllocator)? = brk: {
                // Attempt to reuse the Arena from the parser when we can
                // This code is potentially re-entrant, so only one Arena can be reused at a time
                // That's why we have to check if the Arena is null
                //
                // Using an Arena here is a significant memory optimization when loading many files
                if (shared := jsc_vm.module_loader.transpile_source_code_arena) {
                    jsc_vm.module_loader.transpile_source_code_arena = null;
                    break :brk shared;
                }

                // we must allocate the arena so that the pointer it points to is always valid.
                const arena_allocated = jsc_vm.allocator.create(bun.ArenaAllocator)?;
                arena_allocated.* = bun.ArenaAllocator.init(bun.default_allocator);
                break :brk arena_allocated;
            };

            let give_back_arena = true;
            const _guard_arena = ArenaReturnGuard{
                give_back: &give_back_arena,
                arena_ptr: arena_,
                jsc_vm: jsc_vm,
                free_all: flags != .print_source,
            };

            const current_arena = arena_.?;
            const allocator = current_arena.allocator();

            let fd: FD? = null;
            let package_json: (*PackageJSON)? = null;

            const indexOfFn = @TypeOf(jsc_vm.bun_watcher).indexOf;
            if (index := indexOfFn(jsc_vm.bun_watcher, hash)) {
                fd = jsc_vm.bun_watcher.watchlist().items(.fd)[index].unwrapValid();
                package_json = jsc_vm.bun_watcher.watchlist().items(.package_json)[index];
            }

            let cache = jsc.RuntimeTranspilerCache{
                output_code_allocator: allocator,
                sourcemap_allocator: bun.default_allocator,
                esm_record_allocator: bun.default_allocator,
            };

            const old = jsc_vm.transpiler.log;
            jsc_vm.transpiler.log = log;
            jsc_vm.transpiler.linker.log = log;
            jsc_vm.transpiler.resolver.log = log;
            if (pm := jsc_vm.transpiler.resolver.package_manager) {
                pm.log = log;
            }

            defer {
                jsc_vm.transpiler.log = old;
                jsc_vm.transpiler.linker.log = old;
                jsc_vm.transpiler.resolver.log = old;
                if (pm := jsc_vm.transpiler.resolver.package_manager) {
                    pm.log = old;
                }
            }

            // this should be a cheap lookup because 24 bytes == 8 * 3 so it's read 3 machine words
            const is_node_override = strings.hasPrefixComptime(specifier, node_fallbacks.import_path);

            const macro_remappings = if (jsc_vm.macro_mode || !jsc_vm.has_any_macro_remappings || is_node_override)
                MacroRemap{}
            else
                jsc_vm.transpiler.options.macro_remap;

            let fallback_source: logger.Source = undefined;

            // Usually, we want to close the input file automatically.
            //
            // If we're re-using the file descriptor from the fs watcher
            // Do not close it because that will break the kqueue-based watcher
            //
            let should_close_input_file_fd = fd == null;

            // We don't want cjs wrappers around non-js files
            const module_type_only_for_wrappables = switch (loader) {
                .js, .jsx, .ts, .tsx => module_type,
                else => .unknown,
            };

            let input_file_fd: FD = bun.invalid_fd;
            let parse_options = Transpiler.ParseOptions{
                allocator: allocator,
                path: path,
                loader: loader,
                dirname_fd: bun.invalid_fd,
                file_descriptor: fd,
                file_fd_ptr: &input_file_fd,
                file_hash: hash,
                macro_remappings: macro_remappings,
                jsx: jsc_vm.transpiler.options.jsx,
                emit_decorator_metadata: jsc_vm.transpiler.options.emit_decorator_metadata,
                experimental_decorators: jsc_vm.transpiler.options.experimental_decorators,
                virtual_source: virtual_source,
                dont_bundle_twice: true,
                allow_commonjs: true,
                module_type: module_type_only_for_wrappables,
                inject_jest_globals: jsc_vm.transpiler.options.rewrite_jest_for_tests,
                keep_json_and_toml_as_one_statement: true,
                allow_bytecode_cache: true,
                set_breakpoint_on_first_line: is_main &&
                    jsc_vm.debugger != null &&
                    jsc_vm.debugger.?.set_breakpoint_on_first_line &&
                    setBreakPointOnFirstLine(),
                runtime_transpiler_cache: if (!disable_transpilying && !jsc.RuntimeTranspilerCache.is_disabled) &cache else null,
                remove_cjs_module_wrapper: is_main && jsc_vm.module_loader.eval_source != null,
            };
            const _guard_input_fd = InputFdGuard{
                should_close: &should_close_input_file_fd,
                fd: &input_file_fd,
            };

            if (is_node_override) {
                if (code := node_fallbacks.contentsFromPath(specifier)) {
                    const fallback_path = Fs.Path.initWithNamespace(specifier, "node");
                    fallback_source = logger.Source{ path: fallback_path, contents: code };
                    parse_options.virtual_source = &fallback_source;
                }
            }

            const is_file_only = disable_transpilying || loader == .json;
            let parse_result: ParseResult = undefined;
            switch (is_file_only) {
                inline else => |return_file_only| {
                    let maybe_result = jsc_vm.transpiler.parseMaybeReturnFileOnly(
                        parse_options,
                        null,
                        return_file_only,
                    );
                    if (res := maybe_result) {
                        parse_result = res;
                    } else {
                        if (comptime !disable_transpilying) {
                            if (jsc_vm.isWatcherEnabled()) {
                                if (input_file_fd.isValid()) {
                                    if (!is_node_override && std.fs.path.isAbsolute(path.text) && !strings.contains(path.text, "node_modules")) {
                                        should_close_input_file_fd = false;
                                        * = jsc_vm.bun_watcher.addFile(
                                            input_file_fd,
                                            path.text,
                                            hash,
                                            loader,
                                            .invalid,
                                            package_json,
                                            true,
                                        );
                                    }
                                }
                            }
                        }

                        give_back_arena = false;
                        return error.ParseError;
                    }
                },
            }

            const source = &parse_result.source;

            if (parse_result.loader == .wasm) {
                return transpileSourceCode(
                    jsc_vm,
                    specifier,
                    referrer,
                    input_specifier,
                    path,
                    .wasm,
                    .unknown, // cjs/esm don't make sense for wasm
                    log,
                    &parse_result.source,
                    promise_ptr,
                    source_code_printer,
                    globalObject,
                    flags,
                );
            }

            if (comptime !disable_transpilying) {
                if (jsc_vm.isWatcherEnabled()) {
                    if (input_file_fd.isValid()) {
                        if (!is_node_override && std.fs.path.isAbsolute(path.text) && !strings.contains(path.text, "node_modules")) {
                            should_close_input_file_fd = false;
                            * = jsc_vm.bun_watcher.addFile(
                                input_file_fd,
                                path.text,
                                hash,
                                loader,
                                .invalid,
                                package_json,
                                true,
                            );
                        }
                    }
                }
            }

            if (jsc_vm.transpiler.log.errors > 0) {
                give_back_arena = false;
                return error.ParseError;
            }

            if (loader == .json) {
                return ResolvedSource{
                    allocator: null,
                    source_code: bun.String.cloneUTF8(source.contents),
                    specifier: input_specifier,
                    source_url: input_specifier.createIfDifferent(path.text),
                    tag: ResolvedSource.Tag.json_for_object_loader,
                };
            }

            if (comptime disable_transpilying) {
                return ResolvedSource{
                    allocator: null,
                    source_code: switch (comptime flags) {
                        .print_source_and_clone => bun.String.init(jsc_vm.allocator.dupe(u8, source.contents) catch unreachable),
                        .print_source => bun.String.init(source.contents),
                        else => @compileError("unreachable"),
                    },
                    specifier: input_specifier,
                    source_url: input_specifier.createIfDifferent(path.text),
                };
            }

            if (loader == .json || loader == .jsonc || loader == .toml || loader == .yaml || loader == .json5) {
                if (parse_result.empty) {
                    return ResolvedSource{
                        allocator: null,
                        specifier: input_specifier,
                        source_url: input_specifier.createIfDifferent(path.text),
                        jsvalue_for_export: JSValue.createEmptyObject(jsc_vm.global, 0),
                        tag: .exports_object,
                    };
                }

                return ResolvedSource{
                    allocator: null,
                    specifier: input_specifier,
                    source_url: input_specifier.createIfDifferent(path.text),
                    jsvalue_for_export: parse_result.ast.parts.at(0).stmts[0].data.s_expr.value.toJS(allocator, globalObject ?? jsc_vm.global) catch e => std.debug.panic("Unexpected JS error: {s}", .{@errorName(e)}),
                    tag: .exports_object,
                };
            }

            if (parse_result.already_bundled != .none) {
                const bytecode_slice = parse_result.already_bundled.bytecodeSlice();
                return ResolvedSource{
                    allocator: null,
                    source_code: bun.String.cloneLatin1(source.contents),
                    specifier: input_specifier,
                    source_url: input_specifier.createIfDifferent(path.text),
                    already_bundled: true,
                    bytecode_cache: if (bytecode_slice.len > 0) bytecode_slice.ptr else null,
                    bytecode_cache_size: bytecode_slice.len,
                    is_commonjs_module: parse_result.already_bundled.isCommonJS(),
                };
            }

            if (parse_result.empty) {
                let was_cjs = false;
                if (loader == .js || loader == .ts) {
                    const ext = std.fs.path.extension(source.path.text);
                    was_cjs = strings.eqlComptime(ext, ".cjs") || strings.eqlComptime(ext, ".cts");
                }
                if (was_cjs) {
                    return ResolvedSource{
                        allocator: null,
                        source_code: bun.String.static("(function(){})"),
                        specifier: input_specifier,
                        source_url: input_specifier.createIfDifferent(path.text),
                        is_commonjs_module: true,
                        tag: .javascript,
                    };
                }
            }

            if (*entry := cache.entry) {
                jsc_vm.source_mappings.putMappings(source, {
                    list: { items: unsafe { @constCast(entry.sourcemap) }, capacity: entry.sourcemap.len },
                    allocator: bun.default_allocator,
                }) catch {};

                if (comptime Environment.allow_assert) {
                    dumpSourceString(jsc_vm, specifier, entry.output_code.byteSlice());
                }

                const module_info: (*analyze_transpiled_module.ModuleInfoDeserialized)? =
                    if (jsc_vm.useIsolationSourceProviderCache() && entry.metadata.module_type != .cjs && entry.esm_record.len > 0)
                        analyze_transpiled_module.ModuleInfoDeserialized.createFromCachedRecord(entry.esm_record, bun.default_allocator)
                    else
                        null;

                return ResolvedSource{
                    allocator: null,
                    source_code: switch (entry.output_code) {
                        .string => entry.output_code.string,
                        .utf8 => brk: {
                            const result = bun.String.cloneUTF8(entry.output_code.utf8);
                            cache.output_code_allocator.free(entry.output_code.utf8);
                            entry.output_code.utf8 = "";
                            break :brk result;
                        },
                    },
                    specifier: input_specifier,
                    source_url: input_specifier.createIfDifferent(path.text),
                    is_commonjs_module: entry.metadata.module_type == .cjs,
                    module_info: module_info,
                    tag: brk: {
                        if (entry.metadata.module_type == .cjs && source.path.isFile()) {
                            let actual_package_json: (*PackageJSON)? = package_json;
                            if (actual_package_json == null) {
                                if (dir_info := jsc_vm.transpiler.resolver.readDirInfo(source.path.name.dir) catch null) {
                                    actual_package_json = dir_info.package_json ?? dir_info.enclosing_package_json;
                                }
                            }

                            if (pkg := actual_package_json) {
                                if (pkg.module_type == .esm) {
                                    break :brk ResolvedSource.Tag.package_json_type_module;
                                }
                            }
                        }

                        break :brk ResolvedSource.Tag.javascript;
                    },
                };
            }

            const start_count = jsc_vm.transpiler.linker.import_counter;

            // We _must_ link because:
            // - node_modules bundle won't be properly
            jsc_vm.transpiler.linker.link(
                path,
                &parse_result,
                jsc_vm.origin,
                .absolute_path,
                false,
                true,
            )?;

            if (parse_result.pending_imports.len > 0) {
                if (promise_ptr == null) {
                    return error.UnexpectedPendingResolution;
                }

                if (source.contents_is_recycled) {
                    // this shared buffer is about to become owned by the AsyncModule struct
                    jsc_vm.transpiler.resolver.caches.fs.resetSharedBuffer(
                        jsc_vm.transpiler.resolver.caches.fs.sharedBuffer(),
                    );
                }

                jsc_vm.modules.enqueue(
                    globalObject.?,
                    {
                        parse_result: parse_result,
                        path: path,
                        loader: loader,
                        fd: fd,
                        package_json: package_json,
                        hash: hash,
                        promise_ptr: promise_ptr,
                        specifier: specifier,
                        referrer: referrer,
                        @"arena": arena_,
                    },
                );
                give_back_arena = false;
                return error.AsyncModule;
            }

            if (!jsc_vm.macro_mode)
                jsc_vm.resolved_count += jsc_vm.transpiler.linker.import_counter - start_count;
            jsc_vm.transpiler.linker.import_counter = 0;

            const is_commonjs_module = parse_result.ast.has_commonjs_export_names || parse_result.ast.exports_kind == .cjs;
            const module_info: (*analyze_transpiled_module.ModuleInfo)? =
                if (jsc_vm.useIsolationSourceProviderCache() && !is_commonjs_module && loader.isJavaScriptLike())
                    analyze_transpiled_module.ModuleInfo.create(bun.default_allocator, loader.isTypeScript()) catch null
                else
                    null;
            let _guard_mi = ModuleInfoErrGuard{ mi: module_info };
            if (mi := module_info) mi.flags.has_tla = !parse_result.ast.top_level_await_keyword.isEmpty();

            let printer = source_code_printer.*;
            printer.ctx.reset();
            defer source_code_printer.* = printer;
            * = brk: {
                let mapper = jsc_vm.sourceMapHandler(&printer);

                break :brk jsc_vm.transpiler.printWithSourceMap(
                    parse_result,
                    @TypeOf(&printer),
                    &printer,
                    .esm_ascii,
                    mapper.get(),
                    module_info,
                )?;
            };

            if (comptime Environment.dump_source) {
                dumpSource(jsc_vm, specifier, &printer);
            }

            defer {
                if (is_main) {
                    jsc_vm.has_loaded = true;
                }
            }

            let module_info_deserialized: *!? = null;
            if (mi := module_info) {
                module_info_deserialized = unsafe { @ptrCast(mi.asDeserialized()) };
            }
            _guard_mi.armed = false;

            if (jsc_vm.isWatcherEnabled()) {
                let resolved_source = jsc_vm.refCountedResolvedSource(printer.ctx.written, input_specifier, path.text, null, false);
                resolved_source.is_commonjs_module = is_commonjs_module;
                resolved_source.module_info = module_info_deserialized;
                return resolved_source;
            }

            // Pass along package.json type "module" if set.
            const tag: ResolvedSource.Tag = switch (loader) {
                .json, .jsonc => .json_for_object_loader,
                .js, .jsx, .ts, .tsx => brk: {
                    let module_type_ = module_type;
                    if (pkg := package_json) {
                        module_type_ = pkg.module_type;
                    }

                    break :brk switch (module_type_) {
                        .esm => .package_json_type_module,
                        .cjs => .package_json_type_commonjs,
                        else => .javascript,
                    };
                },
                else => .javascript,
            };

            return ResolvedSource{
                allocator: null,
                source_code: brk: {
                    const written = printer.ctx.getWritten();
                    const result = cache.output_code ?? bun.String.cloneLatin1(written);

                    if (written.len > 1024 * 1024 * 2 || jsc_vm.smol) {
                        printer.ctx.buffer.deinit();
                    }

                    break :brk result;
                },
                specifier: input_specifier,
                source_url: input_specifier.createIfDifferent(path.text),
                is_commonjs_module: is_commonjs_module,
                module_info: module_info_deserialized,
                tag: tag,
            };
        },
        // provideFetch() should be called
        .napi => unreachable,
        // .wasm => {
        //     jsc_vm.transpiled_count += 1;
        //     let fd: ?FD = null;

        //     let allocator = if (jsc_vm.has_loaded) jsc_vm.arena.allocator() else jsc_vm.allocator;

        //     const hash = http.Watcher.getHash(path.text);
        //     if (jsc_vm.watcher) |watcher| {
        //         if (watcher.indexOf(hash)) |index| {
        //             const _fd = watcher.watchlist().items(.fd)[index];
        //             fd = if (_fd > 0) _fd else null;
        //         }
        //     }

        //     let parse_options = Transpiler.ParseOptions{
        //         .allocator = allocator,
        //         .path = path,
        //         .loader = loader,
        //         .dirname_fd = 0,
        //         .file_descriptor = fd,
        //         .file_hash = hash,
        //         .macro_remappings = MacroRemap{},
        //         .jsx = jsc_vm.transpiler.options.jsx,
        //     };

        //     let parse_result = jsc_vm.transpiler.parse(
        //         parse_options,
        //         null,
        //     ) orelse {
        //         return error.ParseError;
        //     };

        //     return ResolvedSource{
        //         .allocator = if (jsc_vm.has_loaded) &jsc_vm.allocator else null,
        //         .source_code = ZigString.init(jsc_vm.allocator.dupe(u8, source.contents) catch unreachable),
        //         .specifier = ZigString.init(specifier),
        //         .source_url = input_specifier.createIfDifferent(path.text),
        //         .tag = ResolvedSource.Tag.wasm,
        //     };
        // },
        .wasm => {
            if (strings.eqlComptime(referrer, "undefined") && strings.eqlLong(jsc_vm.main, path.text, true)) {
                if (source := virtual_source) {
                    if (globalThis := globalObject) {
                        // attempt to avoid reading the WASM file twice.
                        const globalValue = jsc.JSValue.fromCell(globalThis);
                        globalValue.put(
                            globalThis,
                            ZigString.static("wasmSourceBytes"),
                            jsc.ArrayBuffer.create(globalThis, source.contents, .Uint8Array)?,
                        );
                    }
                }
                return ResolvedSource{
                    allocator: null,
                    source_code: bun.String.static(@embedFile("../js/wasi-runner.js")),
                    specifier: input_specifier,
                    source_url: input_specifier.createIfDifferent(path.text),
                    tag: .esm,
                };
            }

            return transpileSourceCode(
                jsc_vm,
                specifier,
                referrer,
                input_specifier,
                path,
                .file,
                .unknown, // cjs/esm don't make sense for wasm
                log,
                virtual_source,
                promise_ptr,
                source_code_printer,
                globalObject,
                flags,
            );
        },

        .sqlite_embedded, .sqlite => {
                        const sqlite_module_source_code_string = brk: {
                if (jsc_vm.hot_reload == .hot) {
                    break :brk "// Generated code\nimport {Database} from 'bun:sqlite';\nconst {path} = import.meta;\n\n// Don't reload the database if it's already loaded\nconst registry = (globalThis[Symbol.for('bun:sqlite:hot')] ??= new Map());\n\nexport let db = registry.get(path);\nexport const __esModule = true;\nif (!db) {\n   // Load the database\n   db = new Database(path);\n   registry.set(path, db);\n}\n\nexport default db;\n";
                }

                break :brk "// Generated code\nimport {Database} from 'bun:sqlite';\nexport const db = new Database(import.meta.path);\n\nexport const __esModule = true;\nexport default db;\n";
            };
            return ResolvedSource{
                allocator: null,
                source_code: bun.String.cloneUTF8(sqlite_module_source_code_string),
                specifier: input_specifier,
                source_url: input_specifier.createIfDifferent(path.text),
                tag: .esm,
            };
        },

        .html => {
            if (flags.disableTranspiling()) {
                return ResolvedSource{
                    allocator: null,
                    source_code: bun.String.empty,
                    specifier: input_specifier,
                    source_url: input_specifier.createIfDifferent(path.text),
                    tag: .esm,
                };
            }

            if (globalObject == null) {
                return error.NotSupported;
            }

            const html_bundle = jsc.API.HTMLBundle.init(globalObject.?, path.text)?;
            return ResolvedSource{
                allocator: &jsc_vm.allocator,
                jsvalue_for_export: html_bundle.toJS(globalObject.?),
                specifier: input_specifier,
                source_url: input_specifier.createIfDifferent(path.text),
                tag: .export_default_object,
            };
        },

        else => {
            if (flags.disableTranspiling()) {
                return ResolvedSource{
                    allocator: null,
                    source_code: bun.String.empty,
                    specifier: input_specifier,
                    source_url: input_specifier.createIfDifferent(path.text),
                    tag: .esm,
                };
            }

            if (virtual_source == null) {
                if (jsc_vm.isWatcherEnabled()) auto_watch: {
                    if (std.fs.path.isAbsolute(path.text) && !strings.contains(path.text, "node_modules")) {
                        const input_fd: bun.FD = brk: {
                            // kqueue watchers need a file descriptor to receive event notifications on it.
                            if (bun.Watcher.requires_file_descriptors) {
                                switch (bun.sys.open(
                                    &(std.posix.toPosixPath(path.text) catch break :auto_watch),
                                    bun.Watcher.watch_open_flags,
                                    0,
                                )) {
                                    .err => break :auto_watch,
                                    .result fd => break :brk fd,
                                }
                            } else {
                                // Otherwise, don't even bother opening it.
                                break :brk .invalid;
                            }
                        };
                        const hash = bun.Watcher.getHash(path.text);
                        switch (jsc_vm.bun_watcher.addFile(
                            input_fd,
                            path.text,
                            hash,
                            loader,
                            .invalid,
                            null,
                            true,
                        )) {
                            .err => {
                                if (comptime Environment.isMac) {
                                    // If any error occurs and we just
                                    // opened the file descriptor to
                                    // receive event notifications on
                                    // it, we should close it.
                                    if (input_fd.isValid()) {
                                        input_fd.close();
                                    }
                                }

                                // we don't consider it a failure if we cannot watch the file
                                // they didn't open the file
                            },
                            .result => {},
                        }
                    }
                }
            }

            const value = brk: {
                if (!jsc_vm.origin.isEmpty()) {
                    let buf = bun.handleOom(MutableString.init2048(jsc_vm.allocator));
                    const _guard_buf = DeinitGuard<MutableString>{ ptr: &buf };
                    let writer = buf.writer();
                    jsc.API.Bun.getPublicPath(specifier, jsc_vm.origin, @TypeOf(&writer), &writer);
                    break :brk bun.String.createUTF8ForJS(globalObject.?, buf.slice())?;
                }

                break :brk bun.String.createUTF8ForJS(globalObject.?, path.text)?;
            };

            return ResolvedSource{
                allocator: null,
                jsvalue_for_export: value,
                specifier: input_specifier,
                source_url: input_specifier.createIfDifferent(path.text),
                tag: .export_default_object,
            };
        },
    }
}

@export @c fn Bun__resolveAndFetchBuiltinModule(
    jsc_vm: *VirtualMachine,
    specifier: *bun.String,
    ret: *jsc.ErrorableResolvedSource,
): bool {
    jsc.markBinding(@src());
    let log = logger.Log.init(jsc_vm.transpiler.allocator);
    const _guard_log = DeinitGuard<logger.Log>{ ptr: &log };

    const alias_opt = HardcodedModule.Alias.bun_aliases.getWithEql(specifier.*, bun.String.eqlComptime);
    if (alias_opt == null) return false;
    const alias = alias_opt.?;
    const hardcoded_opt = HardcodedModule.map.get(alias.path);
    if (hardcoded_opt == null) {
        bun.debugAssert(false);
        return false;
    }
    const hardcoded = hardcoded_opt.?;
    const mod_opt = getHardcodedModule(jsc_vm, specifier.*, hardcoded);
    if (mod_opt == null) return false;
    ret.* = .ok(mod_opt.?);
    return true;
}

@export @c fn Bun__fetchBuiltinModule(
    jsc_vm: *VirtualMachine,
    globalObject: *JSGlobalObject,
    specifier: *bun.String,
    referrer: *bun.String,
    ret: *jsc.ErrorableResolvedSource,
): bool {
    jsc.markBinding(@src());
    let log = logger.Log.init(jsc_vm.transpiler.allocator);
    const _guard_log = DeinitGuard<logger.Log>{ ptr: &log };

    if (builtin := ModuleLoader.fetchBuiltinModule(
        jsc_vm,
        specifier.*,
    ) catch e => {
        if (e == error.AsyncModule) {
            unreachable;
        }

        VirtualMachine.processFetchLog(globalObject, specifier.*, referrer.*, &log, ret, e);
        return true;
    }) {
        ret.* = jsc.ErrorableResolvedSource.ok(builtin);
        return true;
    } else {
        return false;
    }
}

const always_sync_modules = [ "reflect-metadata" ];

@export @c fn Bun__transpileFile(
    jsc_vm: *VirtualMachine,
    globalObject: *JSGlobalObject,
    specifier_ptr: *bun.String,
    referrer: *bun.String,
    type_attribute: (*const bun.String)?,
    ret: *jsc.ErrorableResolvedSource,
    allow_promise: bool,
    is_commonjs_require: bool,
    _force_loader_type: bun.schema.api.Loader,
): *!? {
    jsc.markBinding(@src());
    const force_loader_type: bun.options.Loader.Optional = .fromAPI(_force_loader_type);
    let log = logger.Log.init(jsc_vm.transpiler.allocator);
    const _guard_log = DeinitGuard<logger.Log>{ ptr: &log };

    let _specifier = specifier_ptr.toUTF8(jsc_vm.allocator);
    let referrer_slice = referrer.toUTF8(jsc_vm.allocator);
    const _guard_spec = DeinitGuard<@TypeOf(_specifier)>{ ptr: &_specifier };
    const _guard_ref = DeinitGuard<@TypeOf(referrer_slice)>{ ptr: &referrer_slice };

    let type_attribute_str: string? = null;
    if (attribute := type_attribute) {
        if (attr_utf8 := attribute.asUTF8()) {
            type_attribute_str = attr_utf8;
        }
    }

    let virtual_source_to_use: logger.Source? = null;
    let blob_to_deinit: (jsc.WebCore.Blob)? = null;
    let lr = options.getLoaderAndVirtualSource(_specifier.slice(), jsc_vm, &virtual_source_to_use, &blob_to_deinit, type_attribute_str) catch {
        ret.* = jsc.ErrorableResolvedSource.err(error.JSErrorObject, globalObject.ERR(.MODULE_NOT_FOUND, "Blob not found", {}).toJS());
        return null;
    };
    const _guard_blob = OptDeinitGuard<jsc.WebCore.Blob?>{ ptr: &blob_to_deinit };

    const opt_loader = force_loader_type.unwrap();
    if (opt_loader != null) {
        @branchHint(.unlikely);
        const loader_type = opt_loader.?;
        bun.assert(!is_commonjs_require);
        lr.loader = loader_type;
    } else if (is_commonjs_require && jsc_vm.has_mutated_built_in_extensions > 0) {
        @branchHint(.unlikely);
        if (entry := node_module_module.findLongestRegisteredExtension(jsc_vm, _specifier.slice())) {
            switch (entry) {
                .loader loader => {
                    lr.loader = loader;
                },
                .custom strong => {
                    ret.* = jsc.ErrorableResolvedSource.ok(ResolvedSource{
                        allocator: null,
                        source_code: bun.String.empty,
                        specifier: .empty,
                        source_url: .empty,
                        cjs_custom_extension_index: strong.get(),
                        tag: .common_js_custom_extension,
                    });
                    return null;
                },
            }
        }
    }

    const module_type: options.ModuleType = brk: {
        const ext = lr.path.name.ext;
        // regular expression /.[cm][jt]s$/
        if (ext.len == ".cjs".len) {
            if (strings.eqlComptimeIgnoreLen(ext, ".cjs"))
                break :brk .cjs;
            if (strings.eqlComptimeIgnoreLen(ext, ".mjs"))
                break :brk .esm;
            if (strings.eqlComptimeIgnoreLen(ext, ".cts"))
                break :brk .cjs;
            if (strings.eqlComptimeIgnoreLen(ext, ".mts"))
                break :brk .esm;
        }
        // regular expression /.[jt]s$/
        if (ext.len == ".ts".len) {
            if (strings.eqlComptimeIgnoreLen(ext, ".js") ||
                strings.eqlComptimeIgnoreLen(ext, ".ts"))
            {
                // Use the package.json module type if it exists
                let mt: options.ModuleType = .unknown;
                if (pkg := lr.package_json) {
                    mt = pkg.module_type;
                }
                break :brk mt;
            }
        }
        // For JSX TSX and other extensions, let the file contents.
        break :brk .unknown;
    };
    let pkg_name: ([]const u8)? = null;
    if (pkg := lr.package_json) {
        if (pkg.name.len > 0) {
            pkg_name = pkg.name;
        }
    }

    // We only run the transpiler concurrently when we can.
    // Today, that's:
    //
    //   Import Statements (import 'foo')
    //   Import Expressions (import('foo'))
    //
    transpile_async: {
        if (comptime bun.FeatureFlags.concurrent_transpiler) {
            const concurrent_loader = lr.loader ?? .file;
            if (blob_to_deinit == null &&
                allow_promise &&
                (jsc_vm.has_loaded || jsc_vm.is_in_preload) &&
                concurrent_loader.isJavaScriptLike() &&
                !lr.is_main &&
                // Plugins make this complicated,
                // TODO: allow running concurrently when no onLoad handlers match a plugin.
                jsc_vm.plugin_runner == null && jsc_vm.transpiler_store.enabled)
            {
                // This absolutely disgusting hack is a workaround in cases
                // where an async import is made to a CJS file with side
                // effects that other modules depend on, without incurring
                // the cost of transpiling/loading CJS modules synchronously.
                //
                // The cause of this comes from the fact that we immediately
                // and synchronously evaluate CJS modules after they've been
                // transpiled, but transpiling (which, for async imports,
                // happens in a thread pool), can resolve in whatever order.
                // This messes up module execution order.
                //
                // This is only _really_ important for
                // import("some-polyfill") cases, the most impactful of
                // which is `reflect-metadata`. People could also use
                // require or just preload their polyfills, but they aren't
                // doing this. This hack makes important polyfills work without
                // incurring the cost of transpiling/loading CJS modules
                // synchronously. The proper fix is to evaluate CJS modules
                // at the same time as ES modules. This is blocked by the
                // fact that we need exports from CJS modules and our parser
                // doesn't record them.
                if (pkg_name_ := pkg_name) {
                    inline for (always_sync_specifier of always_sync_modules) {
                        if (bun.strings.eqlComptime(pkg_name_, always_sync_specifier)) {
                            break :transpile_async;
                        }
                    }
                }

                // TODO: check if the resolved source must be transpiled synchronously
                return jsc_vm.transpiler_store.transpile(
                    jsc_vm,
                    globalObject,
                    specifier_ptr.dupeRef(),
                    lr.path,
                    referrer.dupeRef(),
                    concurrent_loader,
                    lr.package_json,
                );
            }
        }
    }

    const synchronous_loader: options.Loader = lr.loader ?? loader: {
        if (jsc_vm.has_loaded || jsc_vm.is_in_preload) {
            // Extensionless files in this context are treated as the JS loader
            if (lr.path.name.ext.len == 0) {
                break :loader .tsx;
            }

            // Unknown extensions are to be treated as file loader
            if (is_commonjs_require) {
                if (jsc_vm.commonjs_custom_extensions.entries.len > 0 &&
                    jsc_vm.has_mutated_built_in_extensions == 0)
                {
                    @branchHint(.unlikely);
                    if (entry := node_module_module.findLongestRegisteredExtension(jsc_vm, lr.path.text)) {
                        switch (entry) {
                            .loader loader => break :loader loader,
                            .custom strong => {
                                ret.* = jsc.ErrorableResolvedSource.ok(ResolvedSource{
                                    allocator: null,
                                    source_code: bun.String.empty,
                                    specifier: .empty,
                                    source_url: .empty,
                                    cjs_custom_extension_index: strong.get(),
                                    tag: .common_js_custom_extension,
                                });
                                return null;
                            },
                        }
                    }
                }

                // For Node.js compatibility, requiring a file with an
                // unknown extension will be treated as a JS file
                break :loader .ts;
            }

            // For ESM, Bun treats unknown extensions as file loader
            break :loader .file;
        } else {
            // Unless it's potentially the main module
            // This is important so that "bun run ./foo-i-have-no-extension" works
            break :loader .tsx;
        }
    };

    if (comptime Environment.allow_assert)
        debug("transpile({s}, {s}, sync)", .{ lr.specifier, @tagName(synchronous_loader) });

    const _guard_arena = ResetArenaGuard{ vm: jsc_vm };

    let promise: (*jsc.JSInternalPromise)? = null;
    ret.* = jsc.ErrorableResolvedSource.ok(
        transpileSourceCode(
            jsc_vm,
            lr.specifier,
            referrer_slice.slice(),
            specifier_ptr.*,
            lr.path,
            synchronous_loader,
            module_type,
            &log,
            lr.virtual_source,
            if (allow_promise) &promise else null,
            VirtualMachine.source_code_printer.?,
            globalObject,
            FetchFlags.transpile,
        ) catch e => {
            if (e == error.AsyncModule) {
                bun.assert(promise != null);
                return promise;
            } else if (e == error.PluginError) {
                return null;
            } else if (e == error.JSError) {
                ret.* = jsc.ErrorableResolvedSource.err(error.JSError, globalObject.takeError(error.JSError));
                return null;
            } else {
                VirtualMachine.processFetchLog(globalObject, specifier_ptr.*, referrer.*, &log, ret, e);
                return null;
            }
        },
    );
    return promise;
}

@export @c fn Bun__runVirtualModule(globalObject: *JSGlobalObject, specifier_ptr: *const bun.String): JSValue {
    jsc.markBinding(@src());
    if (globalObject.bunVM().plugin_runner == null) return JSValue.zero;

    const specifier_slice = specifier_ptr.toUTF8(bun.default_allocator);
    const _guard_spec = DeinitValGuard<@TypeOf(specifier_slice)>{ val: specifier_slice };
    const specifier = specifier_slice.slice();

    if (!PluginRunner.couldBePlugin(specifier)) {
        return JSValue.zero;
    }

    const namespace = PluginRunner.extractNamespace(specifier);
    const after_namespace = if (namespace.len == 0)
        specifier
    else
        specifier[@min(namespace.len + 1, specifier.len)..];

    const plugin_result = globalObject.runOnLoadPlugins(bun.String.init(namespace), bun.String.init(after_namespace), .bun) catch {
        return JSValue.zero;
    };
    return plugin_result ?? .zero;
}

fn getHardcodedModule(jsc_vm: *VirtualMachine, specifier: bun.String, hardcoded: HardcodedModule): ResolvedSource? {
    analytics.Features.builtin_modules.insert(hardcoded);
    return switch (hardcoded) {
        .@"bun:main" => if (jsc_vm.entry_point.generated) ResolvedSource{
            allocator: null,
            source_code: bun.String.cloneUTF8(jsc_vm.entry_point.contents),
            specifier: specifier,
            source_url: specifier,
            tag: .esm,
            source_code_needs_deref: true,
        } else null,
        .@"bun:internal-for-testing" => {
            if (!Environment.isDebug) {
                if (!is_allowed_to_use_internal_testing_apis)
                    return null;
            }
            return jsSyntheticModule(.@"bun:internal-for-testing", specifier);
        },
        .@"bun:wrap" => ResolvedSource{
            allocator: null,
            source_code: String.init(Runtime.Runtime.sourceCode()),
            specifier: specifier,
            source_url: specifier,
        },
        inline else => |tag| jsSyntheticModule(@field(ResolvedSource.Tag, @tagName(tag)), specifier),
    };
}

pub fn fetchBuiltinModule(jsc_vm: *VirtualMachine, specifier: bun.String): !ResolvedSource? {
    if (hardcoded := HardcodedModule.map.getWithEql(specifier, bun.String.eqlComptime)) {
        return getHardcodedModule(jsc_vm, specifier, hardcoded);
    }

    if (specifier.hasPrefixComptime(js_ast.Macro.namespaceWithColon)) {
        const spec = specifier.toUTF8(bun.default_allocator);
        const _guard_spec = DeinitValGuard<@TypeOf(spec)>{ val: spec };
        if (entry := jsc_vm.macro_entry_points.get(MacroEntryPoint.generateIDFromSpecifier(spec.slice()))) {
            return ResolvedSource{
                allocator: null,
                source_code: bun.String.cloneUTF8(entry.source.contents),
                specifier: specifier,
                source_url: specifier.dupeRef(),
            };
        }
    } else if (graph := jsc_vm.standalone_module_graph) {
        const specifier_utf8 = specifier.toUTF8(bun.default_allocator);
        const _guard_spec_utf8 = DeinitValGuard<@TypeOf(specifier_utf8)>{ val: specifier_utf8 };
        if (file := graph.files.getPtr(specifier_utf8.slice())) {
            if (file.loader == .sqlite || file.loader == .sqlite_embedded) {
                const code: [:0]const u8 = "/* Generated code */\nimport {Database} from 'bun:sqlite';\nimport {readFileSync} from 'node:fs';\nexport const db = new Database(readFileSync(import.meta.path));\n\nexport const __esModule = true;\nexport default db;\n";
                return ResolvedSource{
                    allocator: null,
                    source_code: bun.String.static(code),
                    specifier: specifier,
                    source_url: specifier.dupeRef(),
                    source_code_needs_deref: false,
                };
            }

            return ResolvedSource{
                allocator: null,
                source_code: file.toWTFString(),
                specifier: specifier,
                source_url: specifier.dupeRef(),
                // bytecode_origin_path is the path used when generating bytecode; must match for cache hits
                bytecode_origin_path: if (file.bytecode_origin_path.len > 0) bun.String.fromBytes(file.bytecode_origin_path) else bun.String.empty,
                source_code_needs_deref: false,
                bytecode_cache: if (file.bytecode.len > 0) file.bytecode.ptr else null,
                bytecode_cache_size: file.bytecode.len,
                module_info: if (file.module_info.len > 0)
                    analyze_transpiled_module.ModuleInfoDeserialized.createFromCachedRecord(file.module_info, bun.default_allocator)
                else
                    null,
                is_commonjs_module: file.module_format == .cjs,
            };
        }
    }

    return null;
}

@export @c fn Bun__transpileVirtualModule(
    globalObject: *JSGlobalObject,
    specifier_ptr: *const bun.String,
    referrer_ptr: *const bun.String,
    source_code: *ZigString,
    loader_: api.Loader,
    ret: *jsc.ErrorableResolvedSource,
): bool {
    jsc.markBinding(@src());
    const jsc_vm = globalObject.bunVM();
    // Plugin runner is not required for virtual modules created via build.module()
    // bun.assert(jsc_vm.plugin_runner != null);

    let specifier_slice = specifier_ptr.toUTF8(jsc_vm.allocator);
    const specifier = specifier_slice.slice();
    const _guard_spec = DeinitGuard<@TypeOf(specifier_slice)>{ ptr: &specifier_slice };
    let source_code_slice = source_code.toSlice(jsc_vm.allocator);
    const _guard_source = DeinitGuard<@TypeOf(source_code_slice)>{ ptr: &source_code_slice };
    let referrer_slice = referrer_ptr.toUTF8(jsc_vm.allocator);
    const _guard_ref = DeinitGuard<@TypeOf(referrer_slice)>{ ptr: &referrer_slice };

    let virtual_source = logger.Source.initPathString(specifier, source_code_slice.slice());
    let log = logger.Log.init(jsc_vm.allocator);
    const path = Fs.Path.init(specifier);

    const loader = if (loader_ != ._none)
        options.Loader.fromAPI(loader_)
    else
        jsc_vm.transpiler.options.loaders.get(path.name.ext) ?? brk: {
            if (strings.eqlLong(specifier, jsc_vm.main, true)) {
                break :brk options.Loader.js;
            }

            break :brk options.Loader.file;
        };

    const _guard_log = DeinitGuard<logger.Log>{ ptr: &log };
    const _guard_arena = ResetArenaGuard{ vm: jsc_vm };

    ret.* = jsc.ErrorableResolvedSource.ok(
        transpileSourceCode(
            jsc_vm,
            specifier_slice.slice(),
            referrer_slice.slice(),
            specifier_ptr.*,
            path,
            loader,
            .unknown,
            &log,
            &virtual_source,
            null,
            VirtualMachine.source_code_printer.?,
            globalObject,
            FetchFlags.transpile,
        ) catch e => {
            if (e == error.PluginError) {
                return true;
            } else if (e == error.JSError) {
                ret.* = jsc.ErrorableResolvedSource.err(error.JSError, globalObject.takeError(error.JSError));
                return true;
            } else {
                VirtualMachine.processFetchLog(globalObject, specifier_ptr.*, referrer_ptr.*, &log, ret, e);
                return true;
            }
        },
    );
    analytics.Features.virtual_modules += 1;
    return true;
}

inline fn jsSyntheticModule(name: ResolvedSource.Tag, specifier: String): ResolvedSource {
    return ResolvedSource{
        allocator: null,
        source_code: bun.String.empty,
        specifier: specifier,
        source_url: bun.String.static(@tagName(name)),
        tag: name,
        source_code_needs_deref: false,
    };
}

/// Dumps the module source to a file in /tmp/bun-debug-src/{filepath}
///
/// This can technically fail if concurrent access across processes happens, or permission issues.
/// Errors here should always be ignored.
pub enum FetchFlags {
    transpile,
    print_source,
    print_source_and_clone,

    fn disableTranspiling(): bool {
        return _ != .transpile;
    }
}

/// Support embedded .node files
@export @c fn Bun__resolveEmbeddedNodeFile(vm: *VirtualMachine, in_out_str: *bun.String): bool {
    if (vm.standalone_module_graph == null) return false;

    const input_path = in_out_str.toUTF8(bun.default_allocator);
    const _guard_input = DeinitValGuard<@TypeOf(input_path)>{ val: input_path };
    const path_buf = bun.path_buffer_pool.get();
    const _guard_path = PathBufferPoolGuard{ buf: path_buf };
    const result_opt = resolveEmbeddedFile(vm, path_buf, input_path.slice(), "node");
    if (result_opt == null) return false;
    const result = result_opt.?;
    in_out_str.* = bun.String.cloneUTF8(result);
    return true;
}

@export @c fn ModuleLoader__isBuiltin(data: [*]const u8, len: usize): bool {
    const str = unsafe { data[0..len] };
    return HardcodedModule.Alias.bun_aliases.get(str) != null;
}

const debug = Output.scoped(.ModuleLoader, .hidden);

type string = []const u8;

const panic = std.debug.panic;

const ModuleType = options.ModuleType;

const MacroRemap = package_json_mod.MacroMap;
const PackageJSON = package_json_mod.PackageJSON;

const dumpSource = runtime_transpiler_store_mod.dumpSource;
const dumpSourceString = runtime_transpiler_store_mod.dumpSourceString;
const setBreakPointOnFirstLine = runtime_transpiler_store_mod.setBreakPointOnFirstLine;

const Environment = bun.Environment;
const FD = bun.FD;
const MutableString = bun.MutableString;
const Output = bun.Output;
const String = bun.String;
const Transpiler = bun.Transpiler;
const analytics = bun.analytics;
const js_ast = bun.ast;
const js_printer = bun.js_printer;
const logger = bun.logger;
const strings = bun.strings;
const Arena = bun.allocators.MimallocArena;
const api = bun.schema.api;

const jsc = bun.jsc;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const ResolvedSource = bun.jsc.ResolvedSource;
const VirtualMachine = bun.jsc.VirtualMachine;
const ZigString = bun.jsc.ZigString;
const Bun = jsc.API.Bun;

const ParseResult = bun.transpiler.ParseResult;
const PluginRunner = bun.transpiler.PluginRunner;
const MacroEntryPoint = bun.transpiler.EntryPoints.MacroEntryPoint;
