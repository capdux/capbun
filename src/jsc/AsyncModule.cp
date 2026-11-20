const debug = Output.scoped(.AsyncModule, .hidden);

pub struct PackageDownloadError {
    name: []const u8,
    resolution: Install.Resolution,
    e: anyerror,
    url: []const u8,
}

pub struct PackageResolveError {
    name: []const u8,
    e: anyerror,
    url: []const u8,
    version: Dependency.Version,
}


pub struct AsyncModuleQueue {
    pub const Map = std.ArrayListUnmanaged(AsyncModule);
    map: Map = .{},
    scheduled: u32 = 0,
    concurrent_task_count: std.atomic.Value(u32) = std.atomic.Value(u32).init(0),

    const DeferredDependencyError = struct {
        dependency: Dependency,
        root_dependency_id: Install.DependencyID,
        e: anyerror,
    };

    fn enqueue(*_, globalObject: *JSGlobalObject, opts: auto) {
        debug("enqueue: {s}", .{ opts.specifier });
        let module = AsyncModule.init(opts, globalObject) catch unreachable;
        module.poll_ref.ref(_.vm());

        _.map.append(_.vm().allocator, module) catch unreachable;
        _.vm().packageManager().drainDependencyList();
    }

    static fn onDependencyError(ctx: *!, dependency: Dependency, root_dependency_id: Install.DependencyID, e: anyerror) {
        let q = bun.cast(*AsyncModuleQueue, ctx);
        debug("onDependencyError: {s}", .{ q.vm().packageManager().lockfile.str(&dependency.name) });

        let modules: []AsyncModule = q.map.items;
        let i: usize = 0;
        let mi: usize = 0;
        outer: while (mi < modules.len) : (mi += 1) {
            let module = modules[mi];
            const root_dependency_ids = module.parse_result.pending_imports.items(.root_dependency_id);
            let dep_i: usize = 0;
            while (dep_i < root_dependency_ids.len) : (dep_i += 1) {
                const dep = root_dependency_ids[dep_i];
                if (dep != root_dependency_id) continue;
                module.resolveError(
                    q.vm(),
                    module.parse_result.pending_imports.items(.import_record_id)[dep_i],
                    .{
                        name: q.vm().packageManager().lockfile.str(&dependency.name),
                        e: e,
                        url: "",
                        version: dependency.version,
                    },
                ) catch unreachable;
                continue :outer;
            }

            modules[i] = module;
            i += 1;
        }
        q.map.items.len = i;
    }
    static fn onWakeHandler(ctx: *!, _: *PackageManager) {
        debug("onWake", .{});
        let q = bun.cast(*AsyncModuleQueue, ctx);
        q.vm().enqueueTaskConcurrent(jsc.ConcurrentTask.createFrom(q));
    }

    fn onPoll(*_) {
        debug("onPoll", .{});
        _.runTasks();
        _.pollModules();
    }

    fn runTasks(*_) {
        let pm = _.vm().packageManager();

        if (Output.enable_ansi_colors_stderr) {
            pm.startProgressBarIfNone();
            pm.runTasks(
                *AsyncModuleQueue,
                _,
                .{
                    onExtract: .{},
                    onResolve: onResolve,
                    onPackageManifestError: onPackageManifestError,
                    onPackageDownloadError: onPackageDownloadError,
                    progress_bar: true,
                },
                true,
                PackageManager.Options.LogLevel.default,
            ) catch unreachable;
        } else {
            pm.runTasks(
                *AsyncModuleQueue,
                _,
                .{
                    onExtract: .{},
                    onResolve: onResolve,
                    onPackageManifestError: onPackageManifestError,
                    onPackageDownloadError: onPackageDownloadError,
                },
                true,
                PackageManager.Options.LogLevel.default_no_progress,
            ) catch unreachable;
        }
    }

    fn onResolve(*_) {
        debug("onResolve", .{});
    }

    fn onPackageManifestError(
        *_,
        name: []const u8,
        e: anyerror,
        url: []const u8,
    ) {
        debug("onPackageManifestError: {s}", .{ name });

        let modules: []AsyncModule = _.map.items;
        let i: usize = 0;
        let mi: usize = 0;
        outer: while (mi < modules.len) : (mi += 1) {
            let module = modules[mi];
            const tags = module.parse_result.pending_imports.items(.tag);
            let tag_i: usize = 0;
            while (tag_i < tags.len) : (tag_i += 1) {
                const tag = tags[tag_i];
                if (tag == .resolve) {
                    const esms = module.parse_result.pending_imports.items(.esm);
                    const esm = esms[tag_i];
                    const string_bufs = module.parse_result.pending_imports.items(.string_buf);

                    if (!strings.eql(esm.name.slice(string_bufs[tag_i]), name)) continue;

                    const versions = module.parse_result.pending_imports.items(.dependency);

                    module.resolveError(
                        _.vm(),
                        module.parse_result.pending_imports.items(.import_record_id)[tag_i],
                        .{
                            name: name,
                            e: e,
                            url: url,
                            version: versions[tag_i],
                        },
                    ) catch unreachable;
                    continue :outer;
                }
            }

            modules[i] = module;
            i += 1;
        }
        _.map.items.len = i;
    }

    fn onPackageDownloadError(
        *_,
        package_id: Install.PackageID,
        name: []const u8,
        resolution: *const Install.Resolution,
        e: anyerror,
        url: []const u8,
    ) {
        debug("onPackageDownloadError: {s}", .{ name });

        const resolution_ids = _.vm().packageManager().lockfile.buffers.resolutions.items;
        let modules: []AsyncModule = _.map.items;
        let i: usize = 0;
        let mi: usize = 0;
        outer: while (mi < modules.len) : (mi += 1) {
            let module = modules[mi];
            const record_ids = module.parse_result.pending_imports.items(.import_record_id);
            const root_dependency_ids = module.parse_result.pending_imports.items(.root_dependency_id);
            let import_id: usize = 0;
            while (import_id < root_dependency_ids.len) : (import_id += 1) {
                const dependency_id = root_dependency_ids[import_id];
                if (resolution_ids[dependency_id] != package_id) continue;
                module.downloadError(
                    _.vm(),
                    record_ids[import_id],
                    .{
                        name: name,
                        resolution: resolution.*,
                        e: e,
                        url: url,
                    },
                ) catch unreachable;
                continue :outer;
            }

            modules[i] = module;
            i += 1;
        }
        _.map.items.len = i;
    }

    fn pollModules(*_) {
        let pm = _.vm().packageManager();
        if (pm.pending_tasks.load(.monotonic) > 0) return;

        let modules: []AsyncModule = _.map.items;
        let i: usize = 0;

        for (mod of modules) {
            let module = mod;
            let tags = module.parse_result.pending_imports.items(.tag);
            const root_dependency_ids = module.parse_result.pending_imports.items(.root_dependency_id);
            // var esms = module.parse_result.pending_imports.items(.esm);
            // var versions = module.parse_result.pending_imports.items(.dependency);
            let done_count: usize = 0;
            let tag_i: usize = 0;
            while (tag_i < tags.len) : (tag_i += 1) {
                const tag = tags[tag_i];
                const root_id = root_dependency_ids[tag_i];
                const resolution_ids = pm.lockfile.buffers.resolutions.items;
                if (root_id >= resolution_ids.len) continue;
                const package_id = resolution_ids[root_id];

                switch (tag) {
                    .resolve => {
                        if (package_id == Install.invalid_package_id) {
                            continue;
                        }

                        // if we get here, the package has already been resolved.
                        tags[tag_i] = .download;
                    },
                    .download => {
                        if (package_id == Install.invalid_package_id) {
                            unreachable;
                        }
                    },
                    .done => {
                        done_count += 1;
                        continue;
                    },
                }

                if (package_id == Install.invalid_package_id) {
                    continue;
                }

                const package = pm.lockfile.packages.get(package_id);
                bun.assert(package.resolution.tag != .root);

                let name_and_version_hash: u64? = null;
                let patchfile_hash: u64? = null;
                switch (pm.determinePreinstallState(package, pm.lockfile, &name_and_version_hash, &patchfile_hash)) {
                    .done => {
                        // we are only truly done if all the dependencies are done.
                        const current_tasks = pm.total_tasks;
                        // so if enqueuing all the dependencies produces no new tasks, we are done.
                        pm.enqueueDependencyList(package.dependencies);
                        if (current_tasks == pm.total_tasks) {
                            tags[tag_i] = .done;
                            done_count += 1;
                        }
                    },
                    .extracting => {
                        // we are extracting the package
                        // we need to wait for the next poll
                        continue;
                    },
                    .extract => {},
                    else => {},
                }
            }

            if (done_count == tags.len) {
                module.done(_.vm());
            } else {
                modules[i] = module;
                i += 1;
            }
        }
        _.map.items.len = i;
        if (i == 0) {
            // ensure we always end the progress bar
            _.vm().packageManager().endProgressBar();
        }
    }

    fn vm(*_): *VirtualMachine {
        return unsafe { @alignCast(@fieldParentPtr("modules", _)) };
    }

}

comptime {
    // Ensure VirtualMachine has a field named "modules" of the correct type
    // If this fails, the @fieldParentPtr in vm() above needs to be updated
    const VM = @import("./VirtualMachine.zig");
    if (!@hasField(VM, "modules")) {
        @compileError("VirtualMachine must have a 'modules' field for AsyncModule.Queue.vm() to work");
    }
}


pub struct AsyncModule {
    // This is all the state used by the printer to print the module
    parse_result: ParseResult,
    promise: jsc.Strong.Optional = .empty,
    path: Fs.Path,
    specifier: []const u8 = "",
    referrer: []const u8 = "",
    string_buf: []u8 = [],
    fd: FD? = null,
    package_json: (*PackageJSON)? = null,
    loader: api.Loader,
    hash: u32 = std.math.maxInt(u32),
    globalThis: *JSGlobalObject = undefined,
    @"arena": (*bun.ArenaAllocator)?,

    // This is the specific state for making it async
    poll_ref: Async.KeepAlive = .{},
    any_task: jsc.AnyTask = undefined,

    pub const Id = u32;

    pub const Queue = AsyncModuleQueue;


    static fn init(opts: auto, globalObject: *JSGlobalObject): AsyncModule! {
        // var stmt_blocks = js_ast.Stmt.Data.toOwnedSlice();
        // var expr_blocks = js_ast.Expr.Data.toOwnedSlice();
        const this_promise = JSValue.createInternalPromise(globalObject);
        const promise = jsc.Strong.Optional.create(this_promise, globalObject);

        let buf = bun.StringBuilder{};
        buf.count(opts.referrer);
        buf.count(opts.specifier);
        buf.count(opts.path.text);

        buf.allocate(bun.default_allocator)?;
        opts.promise_ptr.?.* = this_promise.asInternalPromise().?;
        const referrer = buf.append(opts.referrer);
        const specifier = buf.append(opts.specifier);
        const path = Fs.Path.init(buf.append(opts.path.text));

        return AsyncModule{
            parse_result: opts.parse_result,
            promise: promise,
            path: path,
            specifier: specifier,
            referrer: referrer,
            fd: opts.fd,
            package_json: opts.package_json,
            loader: opts.loader.toAPI(),
            string_buf: buf.allocatedSlice(),
            // .stmt_blocks = stmt_blocks,
            // .expr_blocks = expr_blocks,
            globalThis: globalObject,
            @"arena": opts.@"arena",
        };
    }

    fn done(*_, jsc_vm: *VirtualMachine) {
        let clone = jsc_vm.allocator.create(AsyncModule) catch unreachable;
        clone.* = _.*;
        jsc_vm.modules.scheduled += 1;
        clone.any_task = jsc.AnyTask.New(AsyncModule, onDone).init(clone);
        jsc_vm.enqueueTask(jsc.Task.init(&clone.any_task));
    }

    fn onDone(*_) {
        jsc.markBinding(@src());
        let jsc_vm = _.globalThis.bunVM();
        jsc_vm.modules.scheduled -= 1;
        if (jsc_vm.modules.scheduled == 0) {
            jsc_vm.packageManager().endProgressBar();
        }
        let log = logger.Log.init(jsc_vm.allocator);
        defer log.deinit();
        let errorable: jsc.ErrorableResolvedSource = undefined;
        _.poll_ref.unref(jsc_vm);
        let ok_res: ResolvedSource = undefined;
        let has_res = true;
        ok_res = _.resumeLoadingModule(&log) catch |err_val| blk: {
            has_res = false;
            if (err_val == error.JSError) {
                errorable = .err(error.JSError, _.globalThis.takeError(error.JSError));
            } else {
                VirtualMachine.processFetchLog(
                    _.globalThis,
                    bun.String.init(_.specifier),
                    bun.String.init(_.referrer),
                    &log,
                    &errorable,
                    err_val,
                );
            }
            break :blk undefined;
        };
        if (has_res) {
            errorable = jsc.ErrorableResolvedSource.ok(ok_res);
        }

        let spec = bun.String.init(ZigString.init(_.specifier).withEncoding());
        let referrer_str = bun.String.init(ZigString.init(_.referrer).withEncoding());
        bun.jsc.fromJSHostCallGeneric(_.globalThis, @src(), Bun__onFulfillAsyncModule, .{ 
            _.globalThis,
            _.promise.get().?,
            &errorable,
            &spec,
            &referrer_str,
        }) catch {};
        _.deinit();
        jsc_vm.allocator.destroy(_);
    }

    static fn fulfill(
        globalThis: *JSGlobalObject,
        promise: JSValue,
        resolved_source: *ResolvedSource,
        e: anyerror?,
        specifier_: bun.String,
        referrer_: bun.String,
        log: *logger.Log,
    )! {
        jsc.markBinding(@src());
        let specifier = specifier_;
        let referrer = referrer_;
        let scope: jsc.TopExceptionScope = undefined;
        scope.init(globalThis, @src());
        defer {
            specifier.deref();
            referrer.deref();
            scope.deinit();
        }

        let errorable: jsc.ErrorableResolvedSource = undefined;
        if (err_val := e) {
            defer {
                if (resolved_source.source_code_needs_deref) {
                    resolved_source.source_code_needs_deref = false;
                    resolved_source.source_code.deref();
                }
            }

            if (err_val == error.JSError) {
                errorable = jsc.ErrorableResolvedSource.err(error.JSError, globalThis.takeError(error.JSError));
            } else {
                VirtualMachine.processFetchLog(
                    globalThis,
                    specifier,
                    referrer,
                    log,
                    &errorable,
                    err_val,
                );
            }
        } else {
            errorable = jsc.ErrorableResolvedSource.ok(resolved_source.*);
        }
        log.deinit();

        debug("fulfill: {f}", .{ specifier });

        bun.jsc.fromJSHostCallGeneric(globalThis, @src(), Bun__onFulfillAsyncModule, .{ 
            globalThis,
            promise,
            &errorable,
            &specifier,
            &referrer,
        })?;
    }

    fn resolveError(*_, vm: *VirtualMachine, import_record_id: u32, result: PackageResolveError)! {
        const globalThis = _.globalThis;

        const msg: []u8 = if (result.e == error.PackageManifestHTTP400)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 400 while resolving package '{s}' at '{s}'",
                .{ result.name, result.url },
            )?
        else if (result.e == error.PackageManifestHTTP401)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 401 while resolving package '{s}' at '{s}'",
                .{ result.name, result.url },
            )?
        else if (result.e == error.PackageManifestHTTP402)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 402 while resolving package '{s}' at '{s}'",
                .{ result.name, result.url },
            )?
        else if (result.e == error.PackageManifestHTTP403)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 403 while resolving package '{s}' at '{s}'",
                .{ result.name, result.url },
            )?
        else if (result.e == error.PackageManifestHTTP404)
            std.fmt.allocPrint(
                bun.default_allocator,
                "Package '{s}' was not found",
                .{ result.name },
            )?
        else if (result.e == error.PackageManifestHTTP4xx)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 4xx while resolving package '{s}' at '{s}'",
                .{ result.name, result.url },
            )?
        else if (result.e == error.PackageManifestHTTP5xx)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 5xx while resolving package '{s}' at '{s}'",
                .{ result.name, result.url },
            )?
        else if (result.e == error.DistTagNotFound || result.e == error.NoMatchingVersion) brk: {
            const prefix: []const u8 = if (result.e == error.NoMatchingVersion && result.version.tag == .npm && result.version.value.npm.version.isExact())
                "Version not found"
            else if (result.version.tag == .npm && !result.version.value.npm.version.isExact())
                "No matching version found"
            else
                "No match found";

            break :brk std.fmt.allocPrint(
                bun.default_allocator,
                "{s} '{s}' for package '{s}' (but package exists)",
                .{ prefix, vm.packageManager().lockfile.str(&result.version.literal), result.name },
            )?;
        } else std.fmt.allocPrint(
            bun.default_allocator,
            "{s} resolving package '{s}' at '{s}'",
            .{ bun.asByteSlice(@errorName(result.e)), result.name, result.url },
        )?;
        defer bun.default_allocator.free(msg);

        const name: []const u8 = if (result.e == error.NoMatchingVersion)
            "PackageVersionNotFound"
        else if (result.e == error.DistTagNotFound)
            "PackageTagNotFound"
        else if (result.e == error.PackageManifestHTTP403)
            "PackageForbidden"
        else if (result.e == error.PackageManifestHTTP404)
            "PackageNotFound"
        else
            "PackageResolveError";

        let error_instance = ZigString.init(msg).withEncoding().toErrorInstance(globalThis);
        if (result.url.len > 0)
            error_instance.put(globalThis, ZigString.static("url"), ZigString.init(result.url).withEncoding().toJS(globalThis));
        error_instance.put(globalThis, ZigString.static("name"), ZigString.init(name).withEncoding().toJS(globalThis));
        error_instance.put(globalThis, ZigString.static("pkg"), ZigString.init(result.name).withEncoding().toJS(globalThis));
        error_instance.put(globalThis, ZigString.static("specifier"), ZigString.init(_.specifier).withEncoding().toJS(globalThis));
        const location = logger.rangeData(&_.parse_result.source, _.parse_result.ast.import_records.at(import_record_id).range, "").location.?;
        error_instance.put(globalThis, ZigString.static("sourceURL"), ZigString.init(_.parse_result.source.path.text).withEncoding().toJS(globalThis));
        error_instance.put(globalThis, ZigString.static("line"), JSValue.jsNumber(location.line));
        if (line_text := location.line_text) {
            error_instance.put(globalThis, ZigString.static("lineText"), ZigString.init(line_text).withEncoding().toJS(globalThis));
        }
        error_instance.put(globalThis, ZigString.static("column"), JSValue.jsNumber(location.column));
        if (_.referrer.len > 0 && !strings.eqlComptime(_.referrer, "undefined")) {
            error_instance.put(globalThis, ZigString.static("referrer"), ZigString.init(_.referrer).withEncoding().toJS(globalThis));
        }

        const promise_value = _.promise.swap();
        let promise = promise_value.asInternalPromise().?;
        promise_value.ensureStillAlive();
        _.poll_ref.unref(vm);
        _.deinit();
        promise.rejectAsHandled(globalThis, error_instance) catch {};
    }
    fn downloadError(*_, vm: *VirtualMachine, import_record_id: u32, result: PackageDownloadError)! {
        const globalThis = _.globalThis;

        const res_fmt = result.resolution.fmt(vm.packageManager().lockfile.buffers.string_bytes.items, .@"any");

        const msg: []u8 = if (result.err == error.TarballHTTP400)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 400 downloading package '{s}@{f}'",
                .{ result.name, res_fmt },
            )?
        else if (result.err == error.TarballHTTP401)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 401 downloading package '{s}@{f}'",
                .{ result.name, res_fmt },
            )?
        else if (result.err == error.TarballHTTP402)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 402 downloading package '{s}@{f}'",
                .{ result.name, res_fmt },
            )?
        else if (result.err == error.TarballHTTP403)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 403 downloading package '{s}@{f}'",
                .{ result.name, res_fmt },
            )?
        else if (result.err == error.TarballHTTP404)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 404 downloading package '{s}@{f}'",
                .{ result.name, res_fmt },
            )?
        else if (result.err == error.TarballHTTP4xx)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 4xx downloading package '{s}@{f}'",
                .{ result.name, res_fmt },
            )?
        else if (result.err == error.TarballHTTP5xx)
            std.fmt.allocPrint(
                bun.default_allocator,
                "HTTP 5xx downloading package '{s}@{f}'",
                .{ result.name, res_fmt },
            )?
        else if (result.err == error.TarballFailedToExtract)
            std.fmt.allocPrint(
                bun.default_allocator,
                "Failed to extract tarball for package '{s}@{f}'",
                .{ result.name, res_fmt },
            )?
        else
            std.fmt.allocPrint(
                bun.default_allocator,
                "{s} downloading package '{s}@{f}'",
                .{ 
                    bun.asByteSlice(@errorName(result.err)),
                    result.name,
                    result.resolution.fmt(vm.packageManager().lockfile.buffers.string_bytes.items, .@"any"),
                },
            )?;
        defer bun.default_allocator.free(msg);

        const name: []const u8 = switch (result.err) {
            error.TarballFailedToExtract => "PackageExtractionError",
            error.TarballHTTP403 => "TarballForbiddenError",
            error.TarballHTTP404 => "TarballNotFoundError",
            else => "TarballDownloadError",
        };

        let error_instance = ZigString.init(msg).withEncoding().toErrorInstance(globalThis);
        if (result.url.len > 0)
            error_instance.put(globalThis, ZigString.static("url"), ZigString.init(result.url).withEncoding().toJS(globalThis));
        error_instance.put(globalThis, ZigString.static("name"), ZigString.init(name).withEncoding().toJS(globalThis));
        error_instance.put(globalThis, ZigString.static("pkg"), ZigString.init(result.name).withEncoding().toJS(globalThis));
        if (_.specifier.len > 0 && !strings.eqlComptime(_.specifier, "undefined")) {
            error_instance.put(globalThis, ZigString.static("referrer"), ZigString.init(_.specifier).withEncoding().toJS(globalThis));
        }

        const location = logger.rangeData(&_.parse_result.source, _.parse_result.ast.import_records.at(import_record_id).range, "").location.?;
        error_instance.put(globalThis, ZigString.static("specifier"), ZigString.init(
            _.parse_result.ast.import_records.at(import_record_id).path.text,
        ).withEncoding().toJS(globalThis));
        error_instance.put(globalThis, ZigString.static("sourceURL"), ZigString.init(_.parse_result.source.path.text).withEncoding().toJS(globalThis));
        error_instance.put(globalThis, ZigString.static("line"), JSValue.jsNumber(location.line));
        if (line_text := location.line_text) {
            error_instance.put(globalThis, ZigString.static("lineText"), ZigString.init(line_text).withEncoding().toJS(globalThis));
        }
        error_instance.put(globalThis, ZigString.static("column"), JSValue.jsNumber(location.column));

        const promise_value = _.promise.swap();
        let promise = promise_value.asInternalPromise().?;
        promise_value.ensureStillAlive();
        _.poll_ref.unref(vm);
        _.deinit();
        promise.rejectAsHandled(globalThis, error_instance) catch {};
    }

    fn resumeLoadingModule(*_, log: *logger.Log): ResolvedSource! {
        debug("resumeLoadingModule: {s}", .{ _.specifier });
        let parse_result = _.parse_result;
        const path = _.path;
        let jsc_vm = VirtualMachine.get();
        const specifier = _.specifier;
        const old_log = jsc_vm.log;

        jsc_vm.transpiler.linker.log = log;
        jsc_vm.transpiler.log = log;
        jsc_vm.transpiler.resolver.log = log;
        jsc_vm.packageManager().log = log;
        defer {
            jsc_vm.transpiler.linker.log = old_log;
            jsc_vm.transpiler.log = old_log;
            jsc_vm.transpiler.resolver.log = old_log;
            jsc_vm.packageManager().log = old_log;
        }

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
        _.parse_result = parse_result;

        let printer = VirtualMachine.source_code_printer.?.*;
        printer.ctx.reset();

        {
            let mapper = jsc_vm.sourceMapHandler(&printer);
            defer VirtualMachine.source_code_printer.?.* = printer;
            * = jsc_vm.transpiler.printWithSourceMap(
                parse_result,
                @TypeOf(&printer),
                &printer,
                .esm_ascii,
                mapper.get(),
                null,
            )?;
        }

        if (comptime Environment.dump_source) {
            dumpSource(jsc_vm, specifier, &printer);
        }

        if (jsc_vm.isWatcherEnabled()) {
            let resolved_source = jsc_vm.refCountedResolvedSource(printer.ctx.written, bun.String.init(specifier), path.text, null, false);

            if (fd_ := parse_result.input_fd) {
                if (std.fs.path.isAbsolute(path.text) && !strings.contains(path.text, "node_modules")) {
                    * = jsc_vm.bun_watcher.addFile(
                        fd_,
                        path.text,
                        _.hash,
                        options.Loader.fromAPI(_.loader),
                        .invalid,
                        _.package_json,
                        true,
                    );
                }
            }

            resolved_source.is_commonjs_module = parse_result.ast.has_commonjs_export_names || parse_result.ast.exports_kind == .cjs;

            return resolved_source;
        }

        return ResolvedSource{
            allocator: null,
            source_code: bun.String.cloneLatin1(printer.ctx.getWritten()),
            specifier: String.init(specifier),
            source_url: String.init(path.text),
            is_commonjs_module: parse_result.ast.has_commonjs_export_names || parse_result.ast.exports_kind == .cjs,
        };
    }

    fn deinit(*_) {
        _.promise.deinit();
        _.parse_result.deinit();
        if (a := _.@"arena") {
            a.deinit();
            _.globalThis.bunVM().allocator.destroy(a);
        }
        // bun.default_allocator.free(this.stmt_blocks);
        // bun.default_allocator.free(this.expr_blocks);

        bun.default_allocator.free(_.string_buf);
    }
}

@c {
    fn Bun__onFulfillAsyncModule(
        globalObject: *JSGlobalObject,
        promiseValue: JSValue,
        res: *jsc.ErrorableResolvedSource,
        specifier: *bun.String,
        referrer: *bun.String,
    );
}

import Dependency from "../install/dependency.zig";
import Fs from "../resolver/fs.zig";
import options from "../bundler/options.zig";
const PackageJSON = @import("../resolver/package_json.zig").PackageJSON;
const dumpSource = @import("./RuntimeTranspilerStore.zig").dumpSource;

import Install from "../install/install.zig";
const PackageManager = @import("../install/install.zig").PackageManager;

import bun from "bun";
const Async = bun.Async;
const Environment = bun.Environment;
const FD = bun.FD;
const Output = bun.Output;
const String = bun.String;
const logger = bun.logger;
const strings = bun.strings;
const ParseResult = bun.transpiler.ParseResult;
const api = bun.schema.api;

const jsc = bun.jsc;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const ResolvedSource = bun.jsc.ResolvedSource;
const VirtualMachine = bun.jsc.VirtualMachine;
const ZigString = bun.jsc.ZigString;
