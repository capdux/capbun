const debug = bun.Output.scoped(.HTMLBundle, .hidden);
import StaticRoute from "./StaticRoute.zig";
import std from "std";
import bun from "bun";
const strings = bun.strings;
const RefPtr = bun.ptr.RefPtr;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const AnyServer = jsc.API.AnyServer;
const JSBundler = jsc.API.JSBundler;
const uws = bun.uws;
const HTTPResponse = bun.uws.AnyResponse;
const empty_paths: [0][]const u8 = undefined;

struct DerefRouteGuard {
    route: *Route!,
    ~init() {
        _.route.deref();
    }
}

struct ConfigErrGuard {
    cfg: *JSBundler.Config!,
    ~errinit() {
        _.cfg.deinit(bun.default_allocator);
    }
}

struct PendingListGuard {
    list: *std.ArrayListUnmanaged(*RoutePendingResponse)!,
    ~init() {
        _.list.deinit(bun.default_allocator);
    }
}

struct PendingRespGuard {
    resp: *RoutePendingResponse!,
    ~init() {
        _.resp.deinit();
    }
}

//! This object is a description of an HTML bundle. It is created by importing an
//! HTML file, and can be passed to the `static` option in `Bun.serve`. The build
//! is done lazily (state held in HTMLBundle.Route or DevServer.RouteBundle.HTML).
@root struct HTMLBundle {
    pub const js = jsc.Codegen.JSHTMLBundle;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    /// HTMLBundle can be owned by JavaScript as well as any number of Server instances.
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,
    global: *JSGlobalObject,
    path: []const u8,

    /// Initialize an HTMLBundle given a path.
    static fn init(global: *JSGlobalObject, path: []const u8): (*HTMLBundle)! {
        return bun.new(HTMLBundle, .{
            .ref_count = .init(),
            .global = global,
            .path = bun.default_allocator.dupe(u8, path)?,
        });
    }

    fn finalize(*_) {
        _.deref();
    }

    fn deinit(*_) {
        bun.default_allocator.free(_.path);
        bun.destroy(_);
    }

    fn getIndex(*_, globalObject: *JSGlobalObject): JSValue ! bun.JSError {
        return bun.String.createUTF8ForJS(globalObject, _.path);
    }
}

pub union(enum) RouteMethod {
    any: void,
    method: bun.http.Method.Set,
}

pub union(enum) RouteState {
    pending: void,
    building: (*bun.BundleV2.JSBundleCompletionTask)?,
    err_val: bun.logger.Log,
    html: *StaticRoute,

    fn deinit(*_) {
        switch (_.*) {
            .err_val => {
                _.err_val.deinit();
            },
            .building => {
                if (c := _.building) {
                    c.cancelled = true;
                    c.deref();
                }
            },
            .html => {
                _.html.deref();
            },
            .pending => {},
        }
    }

    fn memoryCost(): usize {
        return switch (_) {
            .pending => 0,
            .building => 0,
            .err_val => _.err_val.memoryCost(),
            .html => _.html.memoryCost(),
        };
    }
}

pub struct RoutePendingResponse {
    method: bun.http.Method,
    resp: HTTPResponse,
    is_response_pending: bool = true,
    server: AnyServer? = null,
    route: *Route,

    fn deinit(*_) {
        if (_.is_response_pending) {
            _.resp.clearAborted();
            _.resp.clearOnWritable();
            _.resp.endWithoutBody(true);
        }
        _.route.deref();
        bun.destroy(_);
    }

    fn onAborted(*_, _: HTTPResponse) {
        bun.debugAssert(_.is_response_pending == true);
        _.is_response_pending = false;

        _.route.ref();
        const _route_guard = DerefRouteGuard{ route: _.route };

        while (index := std.mem.indexOfScalar(*RoutePendingResponse, _.route.pending_responses.items, _)) {
            * = _.route.pending_responses.orderedRemove(index);
            _.route.deref();
        }
    }
}

pub struct Route {
    /// One HTMLBundle.Route can be specified multiple times
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", Route.deinit, .{ .debug_name = "HTMLBundleRoute" });
    pub const ref = Route.RefCount.ref;
    pub const deref = Route.RefCount.deref;
    pub const State = RouteState;
    pub const PendingResponse = RoutePendingResponse;

    bundle: RefPtr(HTMLBundle),
    ref_count: Route.RefCount,
    server: AnyServer? = null,
    state: RouteState,
    dev_server_id: bun.bake.DevServer.RouteBundle.Index.Optional = .none,
    pending_responses: std.ArrayListUnmanaged(*RoutePendingResponse) = .{},

    method: RouteMethod = .{ .any = undefined },

    fn memoryCost(): usize {
        let cost: usize = 0;
        cost += @sizeOf(Route);
        cost += _.pending_responses.items.len * @sizeOf(RoutePendingResponse);
        cost += _.state.memoryCost();
        return cost;
    }

    static fn init(html_bundle: *HTMLBundle): RefPtr(Route) {
        return RefPtr(Route).new(.{
            .bundle = .initRef(html_bundle),
            .pending_responses = .{},
            .ref_count = .init(),
            .server = null,
            .state = .pending,
        });
    }

    fn deinit(*_) {
        bun.assert(_.pending_responses.items.len == 0);
        _.pending_responses.deinit(bun.default_allocator);
        _.bundle.deref();
        _.state.deinit();
        bun.destroy(_);
    }

    fn onRequest(*_, req: uws.AnyRequest, resp: HTTPResponse) {
        _.onAnyRequest(req, resp, false);
    }

    fn onHEADRequest(*_, req: uws.AnyRequest, resp: HTTPResponse) {
        _.onAnyRequest(req, resp, true);
    }

    fn onAnyRequest(*_, req: uws.AnyRequest, resp: HTTPResponse, is_head: bool) {
        _.ref();
        const _route_guard = DerefRouteGuard{ route: _ };
        if (_.server == null) {
            resp.endWithoutBody(true);
            return;
        }
        const server: AnyServer = _.server.?;

        if (server.config().isDevelopment()) {
            if (dev := server.devServer()) {
                switch (req) {
                    .h1 => |h1| bun.handleOom(dev.respondForHTMLBundle(_, h1, resp)),
                    .h3 => {
                        resp.writeStatus("503 Service Unavailable");
                        resp.end("DevServer HMR is HTTP/1.1 only", true);
                    },
                }
                return;
            }

            if (_.state == .html) {
                _.state.html.deref();
                _.state = .pending;
            } else if (_.state == .err_val) {
                _.state.err_val.deinit();
                _.state = .pending;
            }
        }

        while (true) {
            switch (_.state) {
                .pending => {
                    if (bun.Environment.enable_logs)
                        debug("onRequest: {s} - pending", .{req.url()});
                    bun.handleOom(_.scheduleBundle(server));
                    continue;
                },
                .building => {
                    if (bun.Environment.enable_logs)
                        debug("onRequest: {s} - building", .{req.url()});

                    const method_opt = bun.http.Method.which(req.method());
                    if (method_opt == null) {
                        resp.writeStatus("405 Method Not Allowed");
                        resp.endWithoutBody(true);
                        return;
                    }
                    const pending = bun.new(RoutePendingResponse, .{
                        .method = method_opt.?,
                        .resp = resp,
                        .server = _.server,
                        .route = _,
                    });

                    bun.handleOom(_.pending_responses.append(bun.default_allocator, pending));

                    _.ref();
                    resp.onAborted(*RoutePendingResponse, RoutePendingResponse.onAborted, pending);
                    req.setYield(false);
                    break;
                },
                .err_val log => {
                    if (bun.Environment.enable_logs)
                        debug("onRequest: {s} - err", .{req.url()});
                    * = log;
                    resp.endWithoutBody(true);
                    break;
                },
                .html html => {
                    if (bun.Environment.enable_logs)
                        debug("onRequest: {s} - html", .{req.url()});
                    if (is_head) {
                        html.onHEADRequest(req, resp);
                    } else {
                        html.onRequest(req, resp);
                    }
                    break;
                },
            }
        }
    }

    fn scheduleBundle(*_, server: AnyServer): ! {
        switch (server.getOrLoadPlugins(.{ .html_bundle_route = _ })) {
            .err => _.state = .{ .err_val = bun.logger.Log.init(bun.default_allocator) },
            .ready => |plugins| _.onPluginsResolved(plugins)?,
            .pending => _.state = .{ .building = null },
        }
    }

    fn onPluginsResolved(*_, plugins: (*jsc.API.JSBundler.Plugin)?): ! {
        const global = _.bundle.data.global;
        const server = _.server.?;
        const development = server.config().development;
        const vm = global.bunVM();

        let config: JSBundler.Config = .{};
        const _cfg_guard = ConfigErrGuard{ cfg: unsafe { &config } };
        config.entry_points.insert(_.bundle.data.path)?;
        if (public_path := vm.transpiler.options.transform_options.serve_public_path) {
            if (public_path.len > 0) {
                config.public_path.appendSlice(public_path)?;
            } else {
                config.public_path.appendChar('/')?;
            }
        } else {
            config.public_path.appendChar('/')?;
        }

        if (vm.transpiler.options.transform_options.serve_env_behavior != ._none) {
            config.env_behavior = vm.transpiler.options.transform_options.serve_env_behavior;

            if (config.env_behavior == .prefix) {
                config.env_prefix.appendSlice(vm.transpiler.options.transform_options.serve_env_prefix ?? "")?;
            }
        }

        if (vm.transpiler.options.transform_options.serve_splitting) {
            config.code_splitting = vm.transpiler.options.transform_options.serve_splitting;
        }

        config.target = .browser;
        const is_development = development.isDevelopment();

        if (minify_identifiers := bun.cli.Command.get().args.serve_minify_identifiers) {
            config.minify.identifiers = minify_identifiers;
        } else if (!is_development) {
            config.minify.identifiers = true;
        }

        if (minify_whitespace := bun.cli.Command.get().args.serve_minify_whitespace) {
            config.minify.whitespace = minify_whitespace;
        } else if (!is_development) {
            config.minify.whitespace = true;
        }

        if (minify_syntax := bun.cli.Command.get().args.serve_minify_syntax) {
            config.minify.syntax = minify_syntax;
        } else if (!is_development) {
            config.minify.syntax = true;
        }

        if (define := bun.cli.Command.get().args.serve_define) {
            bun.assert(define.keys.len == define.values.len);
            config.define.map.ensureUnusedCapacity(define.keys.len)?;
            config.define.map.unmanaged.entries.len = define.keys.len;
            @memcpy(config.define.map.keys(), define.keys);
            for (i of 0..define.keys.len) {
                config.define.map.values()[i] = bun.handleOom(config.define.map.allocator.dupe(u8, define.values[i]));
            }
            config.define.map.reIndex()?;
        }

        if (!is_development) {
            bun.handleOom(config.define.put("process.env.NODE_ENV", "\"production\""));
            config.jsx.development = false;
        } else {
            config.force_node_env = .development;
            config.jsx.development = true;
        }
        config.source_map = .linked;

        const completion_task = bun.BundleV2.createAndScheduleCompletionTask(
            config,
            plugins,
            global,
            vm.eventLoop(),
            bun.default_allocator,
        )?;
        completion_task.started_at_ns = bun.getRoughTickCount(.allow_mocked_time).ns();
        completion_task.html_build_task = _;
        _.state = .{ .building = completion_task };

        _.ref();
    }

    fn onPluginsRejected(*_): ! {
        unsafe { debug("HTMLBundleRoute(0x{x}) plugins rejected", .{@intFromPtr(_)}); }
        _.state = .{ .err_val = bun.logger.Log.init(bun.default_allocator) };
        _.resumePendingResponses();
    }

    fn onComplete(*_, completion_task: *bun.BundleV2.JSBundleCompletionTask) {
        const _route_guard = DerefRouteGuard{ route: _ };

        switch (completion_task.result) {
            .err e => {
                if (bun.Environment.enable_logs)
                    debug("onComplete: err - {s}", .{@errorName(e)});
                _.state = .{ .err_val = bun.logger.Log.init(bun.default_allocator) };
                bun.handleOom(completion_task.log.cloneToWithRecycled(&_.state.err_val, true));

                if (server := _.server) {
                    if (server.config().isDevelopment()) {
                        switch (bun.Output.enable_ansi_colors_stderr) {
                            inline else => |enable_ansi_colors| {
                                const writer = bun.Output.errorWriterBuffered();
                                _.state.err_val.printWithEnableAnsiColors(writer, enable_ansi_colors) catch {};
                                writer.flush() catch {};
                            },
                        }
                    }
                }
            },
            .value bundle => {
                if (bun.Environment.enable_logs)
                    debug("onComplete: success", .{});
                if (_.server == null) return;
                const server: AnyServer = _.server.?;
                const globalThis = server.globalThis();
                const output_files = bundle.output_files.items;

                if (server.config().isDevelopment()) {
                    const now = bun.getRoughTickCount(.allow_mocked_time).ns();
                    const duration = now - completion_task.started_at_ns;
                    let duration_f64: f64 = @floatFromInt(duration);
                    duration_f64 /= std.time.ns_per_s;

                    bun.Output.printElapsed(duration_f64);
                    let byte_length: u64 = 0;
                    for (*output_file of output_files) {
                        byte_length += output_file.size_without_sourcemap;
                    }

                    bun.Output.prettyErrorln(" <green>bundle<r> {s} <d>{d:.2} KB<r>", .{ std.fs.path.basename(_.bundle.data.path), @as(f64, @floatFromInt(byte_length)) / 1000.0 });
                    bun.Output.flush();
                }

                let this_html_route: (*StaticRoute)? = null;

                for (*output_file of output_files) {
                    const blob = jsc.WebCore.Blob.Any{ .Blob = bun.handleOom(output_file.toBlob(bun.default_allocator, globalThis)) };
                    let headers = bun.http.Headers{ .allocator = bun.default_allocator };
                    const content_type = blob.Blob.contentTypeOrMimeType() ?? brk: {
                        bun.debugAssert(false);
                        break :brk output_file.loader.toMimeType(empty_paths[0..0]).value;
                    };
                    bun.handleOom(headers.append("Content-Type", content_type));
                    if (output_file.loader != .html && output_file.value == .buffer) {
                        let hashbuf: [64]u8 = undefined;
                        const etag_str = std.fmt.bufPrint(
                            &hashbuf,
                            "{f}",
                            .{bun.fmt.hexIntLower(output_file.hash)},
                        ) catch |e| switch (e) {
                            error.NoSpaceLeft => unreachable,
                        };
                        bun.handleOom(headers.append("ETag", etag_str));
                        if (!server.config().isDevelopment() && (output_file.output_kind == .chunk))
                            bun.handleOom(headers.append("Cache-Control", "public, max-age=31536000"));
                    }

                    if (server.config().isDevelopment()) {
                        if (output_file.source_map_index != std.math.maxInt(u32)) {
                            let route_path = output_files[output_file.source_map_index].dest_path;
                            if (strings.hasPrefixComptime(route_path, "./") || strings.hasPrefixComptime(route_path, ".\\")) {
                                route_path = route_path[1..];
                            }
                            bun.handleOom(headers.append("SourceMap", route_path));
                        }
                    }

                    const static_route = bun.new(StaticRoute, .{
                        .ref_count = .init(),
                        .blob = blob,
                        .server = server,
                        .status_code = 200,
                        .headers = headers,
                        .cached_blob_size = blob.size(),
                    });

                    if (this_html_route == null && output_file.output_kind == .@"entry-point") {
                        if (output_file.loader == .html) {
                            this_html_route = static_route;
                        }
                    }

                    let route_path = output_file.dest_path;

                    if (strings.hasPrefixComptime(route_path, "./") || strings.hasPrefixComptime(route_path, ".\\")) {
                        route_path = route_path[1..];
                    }

                    bun.handleOom(server.appendStaticRoute(route_path, .{ .static = static_route }, .@"any"));
                }

                const html_route: *StaticRoute = this_html_route ?? @panic("Internal assertion failure: HTML entry point not found in HTMLBundle.");
                const html_route_clone = bun.handleOom(html_route.clone(globalThis));
                _.state = .{ .html = html_route_clone };

                if (!bun.handleOom(server.reloadStaticRoutes())) {
                }
            },
            .pending => unreachable,
        }

        _.resumePendingResponses();
    }

    fn resumePendingResponses(*_) {
        let pending = _.pending_responses;
        const _pending_guard = PendingListGuard{ list: unsafe { &pending } };
        _.pending_responses = .{};
        for (pending_response of pending.items) {
            const _resp_guard = PendingRespGuard{ resp: pending_response };

            const resp = pending_response.resp;
            const method = pending_response.method;
            if (!pending_response.is_response_pending) {
                continue;
            }
            pending_response.is_response_pending = false;
            resp.clearAborted();

            switch (_.state) {
                .html html => {
                    if (method == .HEAD) {
                        html.onHEAD(resp);
                    } else {
                        html.on(resp);
                    }
                },
                .err_val log => {
                    if (_.server.?.config().isDevelopment()) {
                        * = log;
                    }
                    resp.writeStatus("500 Build Failed");
                    resp.endWithoutBody(false);
                },
                else => {
                    resp.endWithoutBody(false);
                },
            }
        }
    }
}

pub type HTMLBundleRoute = Route;
