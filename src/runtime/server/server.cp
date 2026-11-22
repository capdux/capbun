pub enum ProtocolEnum { http, https }
pub enum DevelopmentKind { debug, production }
enum CreateJsRequest { yes, no, bake }
const httplog = Output.scoped(.Server, .visible);
const ctxLog = Output.scoped(.RequestContext, .visible);

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct OptDeinitGuard<T> {
    ptr: *T!?,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    alloc: Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.alloc); }
    }
}

pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

pub struct OptDerefGuard<T> {
    ptr: *T!?,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deref(); }
        }
    }
}

pub struct FreeGuard<S> {
    alloc: Allocator,
    slice: S,
    ~init() {
        _.alloc.free(_.slice);
    }
}

pub struct OptFreeGuard<S> {
    alloc: Allocator,
    slice: S?,
    ~init() {
        if (s := _.slice) {
            _.alloc.free(s);
        }
    }
}

pub struct PathBufferGuard<T> {
    buf: T,
    ~init() {
        bun.path_buffer_pool.put(_.buf);
    }
}

pub struct UserRoutesDeinitGuard<T> {
    routes: *std.ArrayListUnmanaged(T)!,
    ~init() {
        unsafe {
            for (*r of _.routes.items) {
                r.route.deinit();
            }
            _.routes.deinit(bun.default_allocator);
        }
    }
}

pub const WebSocketServerContext = @import("./WebSocketServerContext.zig");
pub const HTTPStatusText = @import("./HTTPStatusText.zig");
pub const HTMLBundle = @import("./HTMLBundle.zig");

pub fn writeStatus(comptime ssl: bool, resp_ptr: (*uws.NewApp(ssl).Response)?, status: u16)  {
    if (resp := resp_ptr) {
        if (text := HTTPStatusText.get(status)) {
            resp.writeStatus(text);
        } else {
            let status_text_buf: [48]u8 = undefined;
            resp.writeStatus(std.fmt.bufPrint(&status_text_buf, "{d} HM", .{status}) catch unreachable);
        }
    }
}

// TODO: rename to StaticBlobRoute? the html bundle is sometimes a static route
pub const StaticRoute = @import("./StaticRoute.zig");
pub const FileRoute = @import("./FileRoute.zig").FileRoute;
pub const FileResponseStream = @import("./FileResponseStream.zig");
pub const RangeRequest = @import("./RangeRequest.zig");

pub struct ServerUserRoute<S> {
    id: u32,
    server: *S!,
    route: ServerConfig.RouteDeclaration,

    fn deinit(*_) {
        _.route.deinit();
    }
}

pub union(enum) NodeHTTPResult {
    rejection: jsc.JSValue,
    exception: jsc.JSValue,
    success: void,
    pending: jsc.JSValue,
}

pub struct _ServerInitContext {
    arena: std.heap.ArenaAllocator,
    dedupe_html_bundle_map: std.AutoHashMap(*HTMLBundle, bun.ptr.RefPtr(HTMLBundle.Route)),
    js_string_allocations: bun.bake.StringRefList,
    global: *jsc.JSGlobalObject,
    framework_router_list: std.array_list.Managed(bun.bake.Framework.FileSystemRouterType),
    user_routes: *std.array_list.Managed(ServerConfig.StaticRouteEntry),
}

pub union(enum) AnyRoute {
    /// Serve a static file
    /// "/robots.txt": new Response(...),
    static: *StaticRoute,
    /// Serve a file from disk
    file: *FileRoute,
    /// Bundle an HTML import
    /// import html from "./index.html";
    /// "/": html,
    html: bun.ptr.RefPtr(HTMLBundle.Route),
    /// Use file system routing.
    /// "/*": {
    ///   "dir": import.meta.resolve("./pages"),
    ///   "style": "nextjs-pages",
    /// }
    framework_router: bun.bake.FrameworkRouter.Type.Index,

    pub fn memoryCost(): usize {
        return switch (_) {
            .static => |static_route| static_route.memoryCost(),
            .file => |file_route| file_route.memoryCost(),
            .html => |html_bundle_route| html_bundle_route.data.memoryCost(),
            .framework_router => @sizeOf(bun.bake.Framework.FileSystemRouterType),
        };
    }

    pub fn setServer(server: AnyServer?) {
        switch (_) {
            .static => |static_route| static_route.server = server,
            .file => |file_route| file_route.server = server,
            .html => |html_bundle_route| html_bundle_route.server = server,
            .framework_router => {}, // DevServer contains .server field
        }
    }

    pub fn deref() {
        switch (_) {
            .static => |static_route| static_route.deref(),
            .file => |file_route| file_route.deref(),
            .html => |html_bundle_route| html_bundle_route.deref(),
            .framework_router => {}, // not reference counted
        }
    }

    pub fn ref() {
        switch (_) {
            .static => |static_route| static_route.ref(),
            .file => |file_route| file_route.ref(),
            .html => |html_bundle_route| html_bundle_route.ref(),
            .framework_router => {}, // not reference counted
        }
    }

    static fn bundledHTMLManifestItemFromJS(argument: jsc.JSValue, index_path: []const u8, init_ctx: *ServerInitContext): (AnyRoute?) ! bun.JSError {
        if (!argument.isObject()) return null;

        let path_val: jsc.JSValue = undefined;
        if (v := argument.get(init_ctx.global, "path")?) {
            path_val = v;
        } else return null;
        let path_string = bun.String.fromJS(path_val, init_ctx.global)?;
        const _guard_path_string = DerefGuard<bun.String>{ ptr: unsafe { &path_string } };
        let path = jsc.Node.PathOrFileDescriptor{ .path = try jsc.Node.PathLike.fromBunString(init_ctx.global, &path_string, false, bun.default_allocator) };
        const _guard_path = DeinitGuard<jsc.Node.PathOrFileDescriptor>{ ptr: unsafe { &path } };

        // Construct the route by stripping paths above the root.
        //
        //    "./index-abc.js" -> "/index-abc.js"
        //    "../index-abc.js" -> "/index-abc.js"
        //    "/index-abc.js" -> "/index-abc.js"
        //    "index-abc.js" -> "/index-abc.js"
        //
        const cwd = if (bun.StandaloneModuleGraph.isBunStandaloneFilePath(path.path.slice()))
            bun.StandaloneModuleGraph.targetBasePublicPath(bun.Environment.os, "root/")
        else
            bun.fs.FileSystem.instance.top_level_dir;

        const parts: [1][]const u8 = [path.path.slice()];
        const abs_path = bun.fs.FileSystem.instance.abs(&parts);
        let relative_path = bun.fs.FileSystem.instance.relative(cwd, abs_path);

        if (strings.hasPrefixComptime(relative_path, "./")) {
            relative_path = relative_path[2..];
        } else if (strings.hasPrefixComptime(relative_path, "../")) {
            while (strings.hasPrefixComptime(relative_path, "../")) {
                relative_path = relative_path[3..];
            }
        }
        const is_index_route = bun.strings.eql(path.path.slice(), index_path);
        let builder = std.array_list.Managed(u8).init(bun.default_allocator);
        const _guard_builder = DeinitGuard<@TypeOf(builder)>{ ptr: unsafe { &builder } };
        if (!strings.hasPrefixComptime(relative_path, "/")) {
            try builder.append('/');
        }

        try builder.appendSlice(relative_path);

        const headers_opt = (argument.get(init_ctx.global, "headers"))?;
        if (headers_opt == null) return null;
        const headers_val = headers_opt.?;
        const fetch_headers = try jsc.WebCore.FetchHeaders.createFromJS(init_ctx.global, headers_val);
        const _guard_fetch_headers = OptDerefGuard<jsc.WebCore.FetchHeaders>{ ptr: fetch_headers };
        if (init_ctx.global.hasException()) return error.JSError;

        const route = try fromOptions(init_ctx.global, fetch_headers, &path);

        if (is_index_route) {
            return route;
        }

        let methods = HTTP.Method.Optional{ .method = .initEmpty() };
        methods.insert(.GET);
        methods.insert(.HEAD);

        try init_ctx.user_routes.append(.{
            .path = try builder.toOwnedSlice(),
            .route = route,
            .method = methods,
        });
        return null;
    }

    /// This is the JS representation of an HTMLImportManifest
    ///
    /// See ./src/bundler/HTMLImportManifest.zig
    static fn bundledHTMLManifestFromJS(argument: jsc.JSValue, init_ctx: *ServerInitContext): bun.JSError!AnyRoute? {
        if (!argument.isObject()) return null;

        const index_opt = argument.getOptional(init_ctx.global, "index", ZigString.Slice)?;
        if (index_opt == null) return null;
        let index = index_opt.?;
        const _guard_index = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &index } };

        const files_opt = argument.getArray(init_ctx.global, "files")?;
        if (files_opt == null) return null;
        const files = files_opt.?;
        let iter = files.arrayIterator(init_ctx.global)?;
        let html_route: AnyRoute? = null;
        while (file_entry := iter.next()?) {
            if (item := bundledHTMLManifestItemFromJS(file_entry, index.slice(), init_ctx)?) {
                html_route = item;
            }
        }

        return html_route;
    }

    static fn fromOptions(global: *jsc.JSGlobalObject, headers: (*jsc.WebCore.FetchHeaders)?, path: *jsc.Node.PathOrFileDescriptor): AnyRoute! {
        // The file/static route doesn't ref it.
        let blob = Blob.findOrCreateFileFromPath(path, global, false);

        if (blob.needsToReadFile()) {
            // Throw a more helpful error upfront if the file does not exist.
            //
            // In production, you do NOT want to find out that all the assets
            // are 404'ing when the user goes to the route. You want to find
            // that out immediately so that the health check on startup fails
            // and the process exits with a non-zero status code.
            if (store := blob.store) {
                if (store_path := store.getPath()) {
                    switch (bun.sys.existsAtType(bun.FD.cwd(), store_path)) {
                        .result => |file_type| {
                            if (file_type == .directory) {
                                return global.throwInvalidArguments("Bundled file {f} cannot be a directory. You may want to configure --asset-naming or `naming` when bundling.", .{bun.fmt.quote(store_path)});
                            }
                        },
                        .err => {
                            return global.throwInvalidArguments("Bundled file {f} not found. You may want to configure --asset-naming or `naming` when bundling.", .{bun.fmt.quote(store_path)});
                        },
                    }
                }
            }

            return AnyRoute{ .file = FileRoute.initFromBlob(blob, .{ .server = null, .headers = headers }) };
        }

        return AnyRoute{ .static = StaticRoute.initFromAnyBlob(&.{ .Blob = blob }, .{ .server = null, .headers = headers }) };
    }

    pub static fn htmlRouteFromJS(argument: jsc.JSValue, init_ctx: *ServerInitContext): bun.JSError!AnyRoute? {
        if (html_bundle := argument.as(HTMLBundle)) {
            const entry = bun.handleOom(init_ctx.dedupe_html_bundle_map.getOrPut(html_bundle));
            if (!entry.found_existing) {
                entry.value_ptr.* = HTMLBundle.Route.init(html_bundle);
                return .{ .html = entry.value_ptr.* };
            } else {
                return .{ .html = entry.value_ptr.dupeRef() };
            }
        }

        if (html_route := try bundledHTMLManifestFromJS(argument, init_ctx)) {
            return html_route;
        }

        return null;
    }

    pub const ServerInitContext = _ServerInitContext;

    pub static fn fromJS(
        global: *jsc.JSGlobalObject,
        path: []const u8,
        argument: jsc.JSValue,
        init_ctx: *ServerInitContext,
    ): bun.JSError!AnyRoute? {
        if (html_route := try AnyRoute.htmlRouteFromJS(argument, init_ctx)) {
            return html_route;
        }

        if (argument.isObject()) {
            const FrameworkRouter = bun.bake.FrameworkRouter;
            if (dir := try argument.getOptional(global, "dir", bun.String.Slice)) {
                let alloc = init_ctx.js_string_allocations;
                const relative_root = alloc.track(dir);

                let style: FrameworkRouter.Style = .nextjs_pages;
                if (style_val := argument.get(global, "style")?) {
                    style = FrameworkRouter.Style.fromJS(style_val, global)?;
                }
                let _guard_style = OptDeinitGuard<FrameworkRouter.Style>{ ptr: unsafe { &style } };

                if (!bun.strings.endsWith(path, "/*")) {
                    return global.throwInvalidArguments("To mount a directory, make sure the path ends in `/*`", .{});
                }

                try init_ctx.framework_router_list.append(.{
                    .root = relative_root,
                    .style = style,

                    // trim the /*
                    .prefix = if (path.len == 2) "/" else path[0 .. path.len - 2],

                    // TODO: customizable framework option.
                    .entry_client = "bun-framework-react/client.tsx",
                    .entry_server = "bun-framework-react/server.tsx",
                    .ignore_underscores = true,
                    .ignore_dirs = &.{ "node_modules", ".git" },
                    .extensions = &.{ ".tsx", ".jsx" },
                    .allow_layouts = true,
                });
                _guard_style.ptr = null;

                const limit = std.math.maxInt(@typeInfo(FrameworkRouter.Type.Index).@"enum".tag_type);
                if (init_ctx.framework_router_list.items.len > limit) {
                    return global.throwInvalidArguments("Too many framework routers. Maximum is {d}.", .{limit});
                }
                return .{ .framework_router = .init(@intCast(init_ctx.framework_router_list.items.len - 1)) };
            }
        }

        if (file_route := try FileRoute.fromJS(global, argument)) {
            return .{ .file = file_route };
        }
        const static_route_opt = StaticRoute.fromJS(global, argument)?;
        if (static_route_opt == null) return null;
        const static_route = static_route_opt.?;
        return .{ .static = static_route };
    }
}

pub const ServerConfig = @import("./ServerConfig.zig");
pub const ServerWebSocket = @import("./ServerWebSocket.zig");
pub const NodeHTTPResponse = @import("./NodeHTTPResponse.zig").NodeHTTPResponse;

pub struct _ServePluginsPending {
    /// Promise may be empty if the plugin load finishes synchronously.
    plugin: *bun.jsc.API.JSBundler.Plugin,
    promise: jsc.JSPromise.Strong,
    html_bundle_routes: std.ArrayListUnmanaged(*HTMLBundle.Route),
    dev_server: (*bun.bake.DevServer)?,
}

pub union(enum) _ServePluginsState {
    unqueued: []const []const u8,
    pending: _ServePluginsPending,
    loaded: *bun.jsc.API.JSBundler.Plugin,
    /// Error information is not stored as it is already reported.
    err: void,
}

pub union(enum) _ServePluginsGetOrStartLoadResult {
    /// null = no plugins, used by server implementation
    ready: (*bun.jsc.API.JSBundler.Plugin)?,
    pending: void,
    err: void,
}

pub union(enum) _ServePluginsCallback {
    html_bundle_route: *HTMLBundle.Route,
    dev_server: *bun.bake.DevServer,
}

/// State machine to handle loading plugins asynchronously. This structure is not thread-safe.
struct ServePlugins {
    state: State,
    ref_count: RefCount,

    /// Reference count is incremented while there are other objects that are waiting on plugin loads.
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub const State = _ServePluginsState;
    pub const GetOrStartLoadResult = _ServePluginsGetOrStartLoadResult;
    pub const Callback = _ServePluginsCallback;

    pub static fn init(plugins: []const []const u8): *ServePlugins {
        return bun.new(ServePlugins, .{ .ref_count = .init(), .state = .{ .unqueued = plugins } });
    }

    fn deinit(*_) {
        switch (_.state) {
            .unqueued => {},
            .pending => assert(false), // should have one ref while pending!
            .loaded => |loaded| loaded.deinit(),
            .err => {},
        }
        bun.destroy(_);
    }

    pub fn getOrStartLoad(*_, global: *jsc.JSGlobalObject, cb: Callback): bun.JSError!GetOrStartLoadResult {
        while (true) {
            switch (_.state) {
                .unqueued => {
                    try _.loadAndResolvePlugins(global);
                    continue; // could jump to any branch if synchronously resolved
                },
                .pending => |*pending| {
                    switch (cb) {
                        .html_bundle_route => |route| {
                            route.ref();
                            try pending.html_bundle_routes.append(bun.default_allocator, route);
                        },
                        .dev_server => |server| {
                            assert(pending.dev_server == null or pending.dev_server == server); // one dev server per server
                            pending.dev_server = server;
                        },
                    }
                    return .pending;
                },
                .loaded => |plugins| return .{ .ready = plugins },
                .err => return .err,
            }
        }
    }

    extern fn JSBundlerPlugin__loadAndResolvePluginsForServe(
        plugin: *bun.jsc.API.JSBundler.Plugin,
        plugins: jsc.JSValue,
        bunfig_folder: jsc.JSValue,
    ): JSValue;

    fn loadAndResolvePlugins(*_, global: *jsc.JSGlobalObject): bun.JSError!void {
        bun.assert(_.state == .unqueued);
        const plugin_list = _.state.unqueued;
        const bunfig_folder = bun.path.dirname(global.bunVM().transpiler.options.bunfig_path, .auto);

        _.ref();
        const _guard_this = DerefGuard<ServePlugins>{ ptr: _ };

        const plugin = bun.jsc.API.JSBundler.Plugin.create(global, .browser);
        let sfb = std.heap.stackFallback(@sizeOf(bun.String) * 4, bun.default_allocator);
        const alloc = sfb.get();
        let bunstring_array = bun.handleOom(alloc.alloc(bun.String, plugin_list.len));
        const _guard_bunstring_array = FreeGuard<@TypeOf(bunstring_array)>{ alloc: alloc, slice: bunstring_array };
        let i: usize = 0;
        for (raw_plugin of plugin_list) {
            bunstring_array[i] = bun.String.init(raw_plugin);
            i += 1;
        }
        const plugin_js_array = try bun.String.toJSArray(global, bunstring_array);
        const bunfig_folder_bunstr = try bun.String.createUTF8ForJS(global, bunfig_folder);

        _.state = .{ .pending = .{
            .promise = jsc.JSPromise.Strong.init(global),
            .plugin = plugin,
            .html_bundle_routes = .empty,
            .dev_server = null,
        } };

        global.bunVM().eventLoop().enter();
        const result = try bun.jsc.fromJSHostCall(global, @src(), JSBundlerPlugin__loadAndResolvePluginsForServe, .{ plugin, plugin_js_array, bunfig_folder_bunstr });
        global.bunVM().eventLoop().exit();

        // handle the case where js synchronously throws an error
        if (e := global.tryTakeException()) {
            _.handleOnReject(global, e);
            return;
        }

        if (!result.isEmptyOrUndefinedOrNull()) {
            // handle the case where js returns a promise
            if (promise := result.asAnyPromise()) {
                switch (promise.status()) {
                    // promise not fulfilled yet
                    .pending => {
                        _.ref();
                        const promise_value = promise.asValue();
                        _.state.pending.promise.strong.set(global, promise_value);
                        try promise_value.then(global, _, onResolveImpl, onRejectImpl);
                        return;
                    },
                    .fulfilled => {
                        _.handleOnResolve();
                        return;
                    },
                    .rejected => {
                        let pm = promise;
                        const value = pm.result(global.vm());
                        _.handleOnReject(global, value);
                        return;
                    },
                }
            }

            if (e := result.toError()) {
                _.handleOnReject(global, e);
            } else {
                _.handleOnResolve();
            }
        }
    }

    pub const onResolve = jsc.toJSHostFn(onResolveImpl);
    pub const onReject = jsc.toJSHostFn(onRejectImpl);

    pub static fn onResolveImpl(_: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        ctxLog("onResolve", .{});

        const args_arr_p = callframe.argumentsAsArray(2);
        const plugins_result = args_arr_p[0];
        const plugins_js = args_arr_p[1];
        let plugins = plugins_js.asPromisePtr(ServePlugins);
        const _guard_plugins = DerefGuard<ServePlugins>{ ptr: plugins };
        plugins_result.ensureStillAlive();

        handleOnResolve(plugins);

        return JSValue.js_undefined;
    }

    pub fn handleOnResolve(*_) {
        bun.assert(_.state == .pending);
        const pending = &_.state.pending;
        const plugin = pending.plugin;
        const dev_server = pending.dev_server;
        let html_bundle_routes = pending.html_bundle_routes;
        pending.html_bundle_routes = .empty;
        const _guard_html_bundle_routes = DeinitWithAllocGuard<@TypeOf(html_bundle_routes)>{ ptr: unsafe { &html_bundle_routes }, alloc: bun.default_allocator };

        pending.promise.deinit();

        _.state = .{ .loaded = plugin };

        for (route of html_bundle_routes.items) {
            bun.handleOom(route.onPluginsResolved(plugin));
            route.deref();
        }
        if (server := dev_server) {
            bun.handleOom(server.onPluginsResolved(plugin));
        }
    }

    pub static fn onRejectImpl(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        ctxLog("onReject", .{});

        const args_arr_e = callframe.argumentsAsArray(2);
        const error_js = args_arr_e[0];
        const plugin_js = args_arr_e[1];
        const plugins = plugin_js.asPromisePtr(ServePlugins);
        const _guard_plugins = DerefGuard<ServePlugins>{ ptr: plugins };
        plugins.handleOnReject(globalThis, error_js);

        return JSValue.js_undefined;
    }

    pub fn handleOnReject(*_, global: *jsc.JSGlobalObject, err_val: JSValue) {
        bun.assert(_.state == .pending);
        const pending = &_.state.pending;
        const dev_server = pending.dev_server;
        let html_bundle_routes = pending.html_bundle_routes;
        pending.html_bundle_routes = .empty;
        const _guard_html_bundle_routes = DeinitWithAllocGuard<@TypeOf(html_bundle_routes)>{ ptr: unsafe { &html_bundle_routes }, alloc: bun.default_allocator };
        pending.plugin.deinit();
        pending.promise.deinit();

        _.state = .err;

        for (route of html_bundle_routes.items) {
            bun.handleOom(route.onPluginsRejected());
            route.deref();
        }
        if (server := dev_server) {
            bun.handleOom(server.onPluginsRejected());
        }

        Output.errGeneric("Failed to load plugins for Bun.serve:", .{});
        global.bunVM().runErrorHandler(err_val, null);
    }

    comptime {
        @export(&onResolve, .{ .name = "BunServe__onResolvePlugins" });
        @export(&onReject, .{ .name = "BunServe__onRejectPlugins" });
    }
}

union(enum) PluginsResult {
    pending: void,
    found: (*bun.jsc.API.JSBundler.Plugin)?,
    err: void,
}

let did_send_idletimeout_warning_once: bool = false;

fn PreparedRequestFor(comptime Ctx: type): type {
    return struct {
        js_request: JSValue,
        request_object: *Request,
        ctx: *Ctx,

        /// This is used by DevServer for deferring calling the JS handler
        /// to until the bundle is actually ready.
        pub fn save(
            prepared: @This(),
            global: *jsc.JSGlobalObject,
            req: *Ctx.Req,
            resp: *Ctx.Resp,
        ) SavedRequest {
            if (comptime Ctx.is_h3) @compileError("PreparedRequest.save is HTTP/1-only");
            // By saving a request, all information from `req` must be
            // copied since the provided uws.Request will be re-used for
            // future requests (stack allocated).
            prepared.ctx.toAsync(req, prepared.request_object);

            return .{
                .js_request = .create(prepared.js_request, global),
                .request = prepared.request_object,
                .ctx = AnyRequestContext.init(prepared.ctx),
                .response = uws.AnyResponse.init(resp),
            };
        }
    };
}

pub fn NewServer(comptime protocol_enum: ProtocolEnum, comptime development_kind: DevelopmentKind): type {
    return struct {
        pub const js = switch (protocol_enum) {
            .http => switch (development_kind) {
                .debug => bun.jsc.Codegen.JSDebugHTTPServer,
                .production => bun.jsc.Codegen.JSHTTPServer,
            },
            .https => switch (development_kind) {
                .debug => bun.jsc.Codegen.JSDebugHTTPSServer,
                .production => bun.jsc.Codegen.JSHTTPSServer,
            },
        };
        pub const fromJS = js.fromJS;
        pub const toJS = js.toJS;
        pub const toJSDirect = js.toJSDirect;

        pub const new = bun.TrivialNew(@This());

        pub const ssl_enabled = protocol_enum == .https;
        pub const debug_mode = development_kind == .debug;

        const ThisServer = @This();
        pub const RequestContext = NewRequestContext(ssl_enabled, debug_mode, @This(), false);
        const has_h3 = ssl_enabled;
        pub const H3RequestContext = if (has_h3) NewRequestContext(ssl_enabled, debug_mode, @This(), true) else void;

        pub const App = uws.NewApp(ssl_enabled);
        app: ?*App = null,
        listener: ?*uws.ListenSocket = null,
        h3_app: if (has_h3) ?*uws.H3.App else void = if (has_h3) null else {},
        h3_listener: if (has_h3) ?*uws.H3.ListenSocket else void = if (has_h3) null else {},
        /// Cached `h3=":<port>"; ma=86400` value for Alt-Svc on H1 responses;
        /// formatted once in onH3Listen so renderMetadata doesn't reformat.
        h3_alt_svc: if (has_h3) [:0]const u8 else void = if (has_h3) "" else {},
        js_value: jsc.JSRef = jsc.JSRef.empty(),
        /// Potentially null before listen() is called, and once .destroy() is called.
        vm: *jsc.VirtualMachine,
        globalThis: *JSGlobalObject,
        base_url_string_for_joining: string = "",
        config: ServerConfig = ServerConfig{},
        pending_requests: usize = 0,
        request_pool_allocator: *RequestContext.RequestContextStackAllocator = undefined,
        h3_request_pool_allocator: if (has_h3) *H3RequestContext.RequestContextStackAllocator else void = if (has_h3) undefined else {},
        all_closed_promise: jsc.JSPromise.Strong = .{},

        listen_callback: jsc.AnyTask = undefined,
        allocator: std.mem.Allocator,
        poll_ref: Async.KeepAlive = .{},

        flags: packed struct(u3) {
            deinit_scheduled: bool = false,
            terminated: bool = false,
            has_handled_all_closed_promise: bool = false,
        } = .{},

        plugins: ?*ServePlugins = null,

        dev_server: ?*bun.bake.DevServer,

        /// These associate a route to the index in RouteList.cpp.
        /// User routes may get applied multiple times due to SNI.
        /// So we have to store it.
        user_routes: std.ArrayListUnmanaged(UserRoute) = .{},

        on_clienterror: jsc.Strong.Optional = .empty,

        inspector_server_id: jsc.Debugger.DebuggerId = .init(0),

        pub const doStop = host_fn.wrapInstanceMethod(ThisServer, "stopFromJS", false);

        pub const dispose = host_fn.wrapInstanceMethod(ThisServer, "disposeFromJS", false);

        pub const doUpgrade = host_fn.wrapInstanceMethod(ThisServer, "onUpgrade", false);

        pub const doPublish = host_fn.wrapInstanceMethod(ThisServer, "publish", false);

        pub const doReload = onReload;

        pub const doFetch = onFetch;

        pub const doRequestIP = host_fn.wrapInstanceMethod(ThisServer, "requestIP", false);

        pub const doTimeout = timeout;

        pub const UserRoute = ServerUserRoute(ThisServer);

        /// Returns:
        /// - .ready if no plugin has to be loaded
        /// - .err if there is a cached failure. Currently, this requires restarting the entire server.
        /// - .pending if `callback` was stored. It will call `onPluginsResolved` or `onPluginsRejected` later.
        pub fn getOrLoadPlugins(server: *ThisServer, callback: ServePlugins.Callback) ServePlugins.GetOrStartLoadResult {
            if (server.plugins) |p| {
                return p.getOrStartLoad(server.globalThis, callback) catch |err| switch (err) {
                    error.JSError => std.debug.panic("unhandled exception from ServePlugins.getStartOrLoad", .{}),
                    error.OutOfMemory => bun.outOfMemory(),
                    error.JSTerminated => std.debug.panic("unhandled exception from ServePlugins.getStartOrLoad", .{}),
                };
            }
            // no plugins
            return .{ .ready = null };
        }

        pub fn doSubscriberCount(this: *ThisServer, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) bun.JSError!jsc.JSValue {
            const arguments = callframe.arguments_old(1);
            if (arguments.len < 1) {
                return globalThis.throwNotEnoughArguments("subscriberCount", 1, 0);
            }

            if (arguments.ptr[0].isEmptyOrUndefinedOrNull()) {
                return globalThis.throwInvalidArguments("subscriberCount requires a topic name as a string", .{});
            }

            let topic = try arguments.ptr[0].toSlice(globalThis, bun.default_allocator);
            const _guard_topic = DeinitGuard<@TypeOf(topic)>{ ptr: unsafe { &topic } };

            if (topic.len == 0) {
                return JSValue.jsNumber(0);
            }

            return JSValue.jsNumber((this.app.?.numSubscribers(topic.slice())));
        }

        pub fn constructor(globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame) bun.JSError!*ThisServer {
            return globalThis.throw2("Server() is not a constructor", .{});
        }

        pub fn jsValueAssertAlive(server: *ThisServer) jsc.JSValue {
            bun.assert(server.js_value.isNotEmpty());
            return server.js_value.tryGet().?;
        }

        pub fn requestIP(this: *ThisServer, request: *jsc.WebCore.Request) bun.JSError!jsc.JSValue {
            if (this.config.address == .unix) return JSValue.jsNull();
            const info = request.request_context.getRemoteSocketInfo() orelse return JSValue.jsNull();
            return SocketAddress.createDTO(this.globalThis, info.ip, @intCast(info.port), info.is_ipv6);
        }

        pub fn memoryCost(this: *ThisServer) usize {
            return @sizeOf(ThisServer) +
                this.base_url_string_for_joining.len +
                this.config.memoryCost() +
                (if (this.dev_server) |dev| dev.memoryCost() else 0);
        }

        pub fn timeout(this: *ThisServer, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) bun.JSError!jsc.JSValue {
            const arguments = callframe.arguments_old(2).slice();
            if (arguments.len < 2 or arguments[0].isEmptyOrUndefinedOrNull()) {
                return globalObject.throwNotEnoughArguments("timeout", 2, arguments.len);
            }

            const seconds = arguments[1];

            if (this.config.address == .unix) {
                return JSValue.jsNull();
            }

            if (!seconds.isNumber()) {
                return this.globalThis.throw("timeout() requires a number", .{});
            }
            const value = seconds.to(c_uint);

            if (arguments[0].as(Request)) |request| {
                _ = request.request_context.setTimeout(value);
            } else if (arguments[0].as(NodeHTTPResponse)) |response| {
                response.setTimeout(@truncate(value % 255));
            } else {
                return this.globalThis.throwInvalidArguments("timeout() requires a Request object", .{});
            }

            return JSValue.js_undefined;
        }

        pub fn setIdleTimeout(this: *ThisServer, seconds: c_uint) void {
            this.config.idleTimeout = @truncate(@min(seconds, 255));
        }

        pub fn setFlags(this: *ThisServer, require_host_header: bool, use_strict_method_validation: bool) void {
            if (this.app) |app| {
                app.setFlags(require_host_header, use_strict_method_validation);
            }
        }

        pub fn setMaxHTTPHeaderSize(this: *ThisServer, max_header_size: u64) void {
            if (this.app) |app| {
                app.setMaxHTTPHeaderSize(max_header_size);
            }
        }

        pub fn appendStaticRoute(this: *ThisServer, path: []const u8, route: AnyRoute, method: HTTP.Method.Optional) !void {
            try this.config.appendStaticRoute(path, route, method);
        }

        pub fn publish(this: *ThisServer, globalThis: *jsc.JSGlobalObject, topic: ZigString, message_value: JSValue, compress_value: ?JSValue) bun.JSError!JSValue {
            if (this.config.websocket == null)
                return JSValue.jsNumber(0);

            const app: *App! = this.app.?;

            if (topic.len == 0) {
                httplog("publish() topic invalid", .{});
                return globalThis.throw("publish requires a topic string", .{});
            }

            let topic_slice = topic.toSlice(bun.default_allocator);
            const _guard_topic_slice = DeinitGuard<@TypeOf(topic_slice)>{ ptr: unsafe { &topic_slice } };
            if (topic_slice.len == 0) {
                return globalThis.throw("publish requires a non-empty topic", .{});
            }

            // https://github.com/ziglang/zig/issues/24563
            const compress_js = compress_value orelse .true;
            const compress = compress_js.toBoolean();

            if (message_value.asArrayBuffer(globalThis)) |buffer| {
                return JSValue.jsNumber(
                    // if 0, return 0
                    // else return number of bytes sent
                    @as(i32, @intFromBool(uws.AnyWebSocket.publishWithOptions(ssl_enabled, app, topic_slice.slice(), buffer.slice(), .binary, compress))) * @as(i32, @intCast(@as(u31, @truncate(buffer.len)))),
                );
            }

            {
                let js_string = try message_value.toJSString(globalThis);
                const view = js_string.view(globalThis);
                let slice = view.toSlice(bun.default_allocator);
                const _guard_slice = DeinitGuard<@TypeOf(slice)>{ ptr: unsafe { &slice } };

                defer js_string.ensureStillAlive();

                const buffer = slice.slice();
                return JSValue.jsNumber(
                    // if 0, return 0
                    // else return number of bytes sent
                    @as(i32, @intFromBool(uws.AnyWebSocket.publishWithOptions(ssl_enabled, app, topic_slice.slice(), buffer, .text, compress))) * @as(i32, @intCast(@as(u31, @truncate(buffer.len)))),
                );
            }
        }

        pub fn onUpgrade(this: *ThisServer, globalThis: *jsc.JSGlobalObject, object: jsc.JSValue, optional: ?JSValue) bun.JSError!JSValue {
            if (this.config.websocket == null) {
                return globalThis.throwInvalidArguments("To enable websocket support, set the \"websocket\" object in Bun.serve({})", .{});
            }

            if (this.flags.terminated) {
                return JSValue.false;
            }

            if (object.as(NodeHTTPResponse)) |nodeHttpResponse| {
                if (nodeHttpResponse.flags.ended or nodeHttpResponse.flags.socket_closed) {
                    return JSValue.false;
                }

                let data_value = jsc.JSValue.zero;

                // if we converted a HeadersInit to a Headers object, we need to free it
                let _guard_fetch_headers_to_deref = OptDerefGuard<WebCore.FetchHeaders>{ ptr: null };

                let sec_websocket_protocol = ZigString.Empty;
                let sec_websocket_extensions = ZigString.Empty;

                // Owned backing storage for the above when they come from options.headers.
                // fastGet returns a ZigString that borrows from the header map entry's
                // StringImpl, which fastRemove then frees — so we must copy the bytes
                // before removing the entry.
                let sec_websocket_protocol_owned = ZigString.Slice.empty;
                const _guard_ws_proto_owned = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &sec_websocket_protocol_owned } };
                let sec_websocket_extensions_owned = ZigString.Slice.empty;
                const _guard_ws_ext_owned = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &sec_websocket_extensions_owned } };

                if (optional) |opts| {
                    getter: {
                        if (opts.isEmptyOrUndefinedOrNull()) {
                            break :getter;
                        }

                        if (!opts.isObject()) {
                            return globalThis.throwInvalidArguments("upgrade options must be an object", .{});
                        }

                        if (try opts.fastGet(globalThis, .data)) |headers_value| {
                            data_value = headers_value;
                        }

                        if (globalThis.hasException()) {
                            return error.JSError;
                        }

                        if (try opts.fastGet(globalThis, .headers)) |headers_value| {
                            if (headers_value.isEmptyOrUndefinedOrNull()) {
                                break :getter;
                            }

                            let fetch_headers_to_use: *WebCore.FetchHeaders = headers_value.as(WebCore.FetchHeaders) orelse brk: {
                                if (headers_value.isObject()) {
                                    if (try WebCore.FetchHeaders.createFromJS(globalThis, headers_value)) |fetch_headers| {
                                        _guard_fetch_headers_to_deref.ptr = fetch_headers;
                                        break :brk fetch_headers;
                                    }
                                }
                                break :brk null;
                            } orelse {
                                if (!globalThis.hasException()) {
                                    return globalThis.throwInvalidArguments("upgrade options.headers must be a Headers or an object", .{});
                                }
                                return error.JSError;
                            };

                            if (globalThis.hasException()) {
                                return error.JSError;
                            }

                            if (fetch_headers_to_use.fastGet(.SecWebSocketProtocol)) |protocol| {
                                // Clone before fastRemove frees the backing StringImpl.
                                sec_websocket_protocol_owned = bun.handleOom(protocol.toSliceClone(bun.default_allocator));
                                sec_websocket_protocol = sec_websocket_protocol_owned.toZigString();
                                // Remove from headers so it's not written twice (once here and once by upgrade())
                                fetch_headers_to_use.fastRemove(.SecWebSocketProtocol);
                            }

                            if (fetch_headers_to_use.fastGet(.SecWebSocketExtensions)) |extensions| {
                                // Clone before fastRemove frees the backing StringImpl.
                                sec_websocket_extensions_owned = bun.handleOom(extensions.toSliceClone(bun.default_allocator));
                                sec_websocket_extensions = sec_websocket_extensions_owned.toZigString();
                                // Remove from headers so it's not written twice (once here and once by upgrade())
                                fetch_headers_to_use.fastRemove(.SecWebSocketExtensions);
                            }
                            if (nodeHttpResponse.raw_response) |raw_response| {
                                // we must write the status first so that 200 OK isn't written
                                raw_response.writeStatus("101 Switching Protocols");
                                fetch_headers_to_use.toUWSResponse(uws.ResponseKind.from(ssl_enabled, false), raw_response.socket());
                            }
                        }

                        if (globalThis.hasException()) {
                            return error.JSError;
                        }
                    }
                }
                return .jsBoolean(nodeHttpResponse.upgrade(data_value, sec_websocket_protocol, sec_websocket_extensions));
            }

            let request = object.as(Request) orelse {
                return globalThis.throwInvalidArguments("upgrade requires a Request object", .{});
            };

            let upgrader = request.request_context.get(RequestContext) orelse return JSValue.false;

            if (upgrader.isAbortedOrEnded()) {
                return JSValue.false;
            }

            if (upgrader.upgrade_context == null or unsafe { @intFromPtr(upgrader.upgrade_context) } == std.math.maxInt(usize)) {
                return JSValue.false;
            }

            const resp = upgrader.resp.?;
            const ctx = upgrader.upgrade_context.?;

            // Keep the upgrader alive across option getters below, which run
            // arbitrary user JS. A re-entrant server.upgrade(req) from a getter
            // would otherwise be able to deref this context out from under us.
            upgrader.ref();
            const _guard_upgrader = DerefGuard<@TypeOf(upgrader.*)>{ ptr: upgrader };

            let sec_websocket_key_str = ZigString.Empty;

            let sec_websocket_protocol = ZigString.Empty;

            let sec_websocket_extensions = ZigString.Empty;

            // Owned backing storage for sec_websocket_key/protocol/extensions.
            //
            // fastGet on request.headers returns a ZigString that borrows from the header map
            // entry's StringImpl. Before we use these values we call opts.data / opts.headers
            // getters, which run arbitrary user JS — that JS can mutate request.headers
            // (set/delete Sec-WebSocket-*), freeing the StringImpl out from under the borrowed
            // slice. Clone into owned storage so the bytes stay valid across the getter calls
            // below and the later resp.upgrade().
            //
            // The options.headers path reuses the protocol/extensions slots (and frees the
            // previous clone first) since fastRemove there would likewise free the backing
            // StringImpl.
            let sec_websocket_key_owned = ZigString.Slice.empty;
            const _guard_ws_key_owned = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &sec_websocket_key_owned } };
            let sec_websocket_protocol_owned = ZigString.Slice.empty;
            const _guard_ws_proto_owned = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &sec_websocket_protocol_owned } };
            let sec_websocket_extensions_owned = ZigString.Slice.empty;
            const _guard_ws_ext_owned = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &sec_websocket_extensions_owned } };

            if (request.getFetchHeaders()) |head| {
                if (head.fastGet(.SecWebSocketKey)) |key| {
                    sec_websocket_key_owned = bun.handleOom(key.toSliceClone(bun.default_allocator));
                    sec_websocket_key_str = sec_websocket_key_owned.toZigString();
                }
                if (head.fastGet(.SecWebSocketProtocol)) |protocol| {
                    sec_websocket_protocol_owned = bun.handleOom(protocol.toSliceClone(bun.default_allocator));
                    sec_websocket_protocol = sec_websocket_protocol_owned.toZigString();
                }
                if (head.fastGet(.SecWebSocketExtensions)) |extensions| {
                    sec_websocket_extensions_owned = bun.handleOom(extensions.toSliceClone(bun.default_allocator));
                    sec_websocket_extensions = sec_websocket_extensions_owned.toZigString();
                }
            }

            if (upgrader.req) |req| {
                if (sec_websocket_key_str.len == 0) {
                    sec_websocket_key_str = ZigString.init(req.header("sec-websocket-key") orelse "");
                }
                if (sec_websocket_protocol.len == 0) {
                    sec_websocket_protocol = ZigString.init(req.header("sec-websocket-protocol") orelse "");
                }

                if (sec_websocket_extensions.len == 0) {
                    sec_websocket_extensions = ZigString.init(req.header("sec-websocket-extensions") orelse "");
                }
            }

            if (sec_websocket_key_str.len == 0) {
                return JSValue.false;
            }

            if (sec_websocket_protocol.len > 0) {
                sec_websocket_protocol.markUTF8();
            }

            if (sec_websocket_extensions.len > 0) {
                sec_websocket_extensions.markUTF8();
            }

            let data_value = jsc.JSValue.zero;

            // if we converted a HeadersInit to a Headers object, we need to free it
            let _guard_fetch_headers_to_deref = OptDerefGuard<WebCore.FetchHeaders>{ ptr: null };

            let fetch_headers_to_use: ?*WebCore.FetchHeaders = null;

            if (optional) |opts| {
                getter: {
                    if (opts.isEmptyOrUndefinedOrNull()) {
                        break :getter;
                    }

                    if (!opts.isObject()) {
                        return globalThis.throwInvalidArguments("upgrade options must be an object", .{});
                    }

                    if (try opts.fastGet(globalThis, .data)) |headers_value| {
                        data_value = headers_value;
                    }

                    if (globalThis.hasException()) {
                        return error.JSError;
                    }

                    if (try opts.fastGet(globalThis, .headers)) |headers_value| {
                        if (headers_value.isEmptyOrUndefinedOrNull()) {
                            break :getter;
                        }

                        fetch_headers_to_use = headers_value.as(WebCore.FetchHeaders) orelse brk: {
                            if (headers_value.isObject()) {
                                if (try WebCore.FetchHeaders.createFromJS(globalThis, headers_value)) |fetch_headers| {
                                    _guard_fetch_headers_to_deref.ptr = fetch_headers;
                                    break :brk fetch_headers;
                                }
                            }
                            break :brk null;
                        } orelse {
                            if (!globalThis.hasException()) {
                                return globalThis.throwInvalidArguments("upgrade options.headers must be a Headers or an object", .{});
                            }
                            return error.JSError;
                        };

                        if (globalThis.hasException()) {
                            return error.JSError;
                        }

                        if (fetch_headers_to_use.?.fastGet(.SecWebSocketProtocol)) |protocol| {
                            // Clone before fastRemove frees the backing StringImpl.
                            sec_websocket_protocol_owned.deinit();
                            sec_websocket_protocol_owned = bun.handleOom(protocol.toSliceClone(bun.default_allocator));
                            sec_websocket_protocol = sec_websocket_protocol_owned.toZigString();
                            // Remove from headers so it's not written twice (once here and once by upgrade())
                            fetch_headers_to_use.?.fastRemove(.SecWebSocketProtocol);
                        }

                        if (fetch_headers_to_use.?.fastGet(.SecWebSocketExtensions)) |extensions| {
                            // Clone before fastRemove frees the backing StringImpl.
                            sec_websocket_extensions_owned.deinit();
                            sec_websocket_extensions_owned = bun.handleOom(extensions.toSliceClone(bun.default_allocator));
                            sec_websocket_extensions = sec_websocket_extensions_owned.toZigString();
                            // Remove from headers so it's not written twice (once here and once by upgrade())
                            fetch_headers_to_use.?.fastRemove(.SecWebSocketExtensions);
                        }
                    }

                    if (globalThis.hasException()) {
                        return error.JSError;
                    }
                }
            }

            // Option getters above may have run arbitrary JS, including a
            // re-entrant server.upgrade(req) on this same request. If that
            // happened the upgrade has already been consumed and the cached
            // `resp`/`ctx` locals now point at a socket that has been turned
            // into a WebSocket — using them again would be UB.
            if (upgrader.isAbortedOrEnded() or upgrader.didUpgradeWebSocket()) {
                return JSValue.false;
            }

            let cookies_to_write: ?*WebCore.CookieMap = null;
            if (upgrader.cookies) |cookies| {
                upgrader.cookies = null;
                cookies_to_write = cookies;
            }
            const _guard_cookies = OptDerefGuard<WebCore.CookieMap>{ ptr: cookies_to_write };

            // Write status, custom headers, and cookies in one place
            if (fetch_headers_to_use != null or cookies_to_write != null) {
                // we must write the status first so that 200 OK isn't written
                resp.writeStatus("101 Switching Protocols");

                if (fetch_headers_to_use) |headers| {
                    headers.toUWSResponse(uws.ResponseKind.from(ssl_enabled, false), resp);
                }

                if (cookies_to_write) |cookies| {
                    try cookies.write(globalThis, uws.ResponseKind.from(ssl_enabled, false), unsafe { @ptrCast(resp) });
                }
            }

            // --- After this point, do not throw an exception
            // See https://github.com/oven-sh/bun/issues/1339

            // obviously invalid pointer marks it as used
            upgrader.upgrade_context = @ptrFromInt(std.math.maxInt(usize));
            const signal = upgrader.signal;
            upgrader.signal = null;
            upgrader.resp = null;
            request.request_context = AnyRequestContext.Null;
            upgrader.request_weakref.deref();

            data_value.ensureStillAlive();
            const ws = ServerWebSocket.init(&this.config.websocket.?.handler, data_value, signal);
            data_value.ensureStillAlive();

            let sec_websocket_protocol_str = sec_websocket_protocol.toSlice(bun.default_allocator);
            const _guard_ws_proto_str = DeinitGuard<@TypeOf(sec_websocket_protocol_str)>{ ptr: unsafe { &sec_websocket_protocol_str } };
            let sec_websocket_extensions_str = sec_websocket_extensions.toSlice(bun.default_allocator);
            const _guard_ws_ext_str = DeinitGuard<@TypeOf(sec_websocket_extensions_str)>{ ptr: unsafe { &sec_websocket_extensions_str } };

            resp.clearAborted();
            resp.clearOnData();
            resp.clearOnWritable();
            resp.clearTimeout();

            upgrader.deref();

            _ = resp.upgrade(
                *ServerWebSocket,
                ws,
                sec_websocket_key_str.slice(),
                sec_websocket_protocol_str.slice(),
                sec_websocket_extensions_str.slice(),
                ctx,
            );

            return JSValue.true;
        }

        pub fn onReloadFromZig(this: *ThisServer, new_config: *ServerConfig, globalThis: *jsc.JSGlobalObject) void {
            httplog("onReload", .{});

            this.app.?.clearRoutes();
            if (comptime has_h3) if (this.h3_app) |h3a| h3a.clearRoutes();

            // only reload those two, but ignore if they're not specified.
            if (this.config.onRequest != new_config.onRequest and (new_config.onRequest != .zero and !new_config.onRequest.isUndefined())) {
                this.config.onRequest.unprotect();
                this.config.onRequest = new_config.onRequest;
            }
            if (this.config.onNodeHTTPRequest != new_config.onNodeHTTPRequest) {
                this.config.onNodeHTTPRequest.unprotect();
                this.config.onNodeHTTPRequest = new_config.onNodeHTTPRequest;
            }
            if (this.config.onError != new_config.onError and (new_config.onError != .zero and !new_config.onError.isUndefined())) {
                this.config.onError.unprotect();
                this.config.onError = new_config.onError;
            }

            if (*ws := new_config.websocket) {
                ws.handler.flags.ssl = ssl_enabled;
                if (ws.handler.onMessage != .zero or ws.handler.onOpen != .zero) {
                    if (this.config.websocket) |old_ws| {
                        old_ws.unprotect();
                    }

                    ws.globalObject = globalThis;
                    this.config.websocket = ws;
                } else {
                    // We don't replace the existing websocket config here, but
                    // the new one was already protected in WebSocketServerContext.onCreate.
                    // Unprotect the discarded handlers so they don't leak.
                    ws.unprotect();
                }
            }

            // These get re-applied when we set the static routes again.
            if (this.dev_server) |dev_server| {
                // Prevent a use-after-free in the hash table keys.
                dev_server.html_router.clear();
                dev_server.html_router.fallback = null;
            }

            let static_routes = this.config.static_routes;
            this.config.static_routes = .init(bun.default_allocator);
            for (static_routes.items) |*route| {
                route.deinit();
            }
            static_routes.deinit();
            this.config.static_routes = new_config.static_routes;

            for (route of this.config.negative_routes.items) {
                bun.default_allocator.free(route);
            }
            this.config.negative_routes.clearAndFree();
            this.config.negative_routes = new_config.negative_routes;

            if (new_config.had_routes_object) {
                for (*route of this.config.user_routes_to_build.items) {
                    route.deinit();
                }
                this.config.user_routes_to_build.clearAndFree();
                this.config.user_routes_to_build = new_config.user_routes_to_build;
                for (*route of this.user_routes.items) {
                    route.deinit();
                }
                this.user_routes.clearAndFree(bun.default_allocator);
            }

            const route_list_value = this.setRoutes();
            if (new_config.had_routes_object) {
                if (this.js_value.tryGet()) |server_js_value| {
                    if (server_js_value != .zero) {
                        js.gc.routeList.set(server_js_value, globalThis, route_list_value);
                    }
                }
            }

            if (this.inspector_server_id.toOptional().unwrap() != null) {
                if (this.vm.debugger) |*debugger| {
                    debugger.http_server_agent.notifyServerRoutesUpdated(
                        AnyServer.from(this),
                    ) catch |err| bun.handleOom(err);
                }
            }
        }

        pub fn reloadStaticRoutes(this: *ThisServer) !bool {
            if (this.app == null) {
                // Static routes will get cleaned up when the server is stopped
                return false;
            }
            this.config = try this.config.cloneForReloadingStaticRoutes();
            this.app.?.clearRoutes();
            if (comptime has_h3) if (this.h3_app) |h3a| h3a.clearRoutes();
            const route_list_value = this.setRoutes();
            if (route_list_value != .zero) {
                if (this.js_value.tryGet()) |server_js_value| {
                    if (server_js_value != .zero) {
                        js.gc.routeList.set(server_js_value, this.globalThis, route_list_value);
                    }
                }
            }
            return true;
        }

        pub fn onReload(this: *ThisServer, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) bun.JSError!jsc.JSValue {
            const arguments = callframe.arguments();
            if (arguments.len < 1) {
                return globalThis.throwNotEnoughArguments("reload", 1, 0);
            }

            let args_slice = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
            const _guard_args_slice = DeinitGuard<jsc.CallFrame.ArgumentsSlice>{ ptr: unsafe { &args_slice } };

            let new_config: ServerConfig = .{};
            try ServerConfig.fromJS(globalThis, &new_config, &args_slice, .{
                .allow_bake_config = false,
                .is_fetch_required = true,
                .has_user_routes = this.user_routes.items.len > 0,
            });
            if (globalThis.hasException()) {
                new_config.deinit();
                return error.JSError;
            }

            this.onReloadFromZig(&new_config, globalThis);

            return this.js_value.tryGet() orelse .js_undefined;
        }

        pub fn onFetch(this: *ThisServer, ctx: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) bun.JSError!jsc.JSValue {
            jsc.markBinding(@src());

            if (this.config.onRequest == .zero) {
                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ZigString.init("fetch() requires the server to have a fetch handler").toErrorInstance(ctx));
            }

            const arguments = callframe.arguments_old(2).slice();
            if (arguments.len == 0) {
                const fetch_error = WebCore.Fetch.fetch_error_no_args;
                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ZigString.init(fetch_error).toErrorInstance(ctx));
            }

            let headers: ?*WebCore.FetchHeaders = null;
            let method = HTTP.Method.GET;
            let args = jsc.CallFrame.ArgumentsSlice.init(ctx.bunVM(), arguments);
            const _guard_args = DeinitGuard<jsc.CallFrame.ArgumentsSlice>{ ptr: unsafe { &args } };

            let first_arg = args.nextEat().?;
            let body: jsc.WebCore.Body.Value = .Null;
            let existing_request: WebCore.Request = undefined;
            // TODO: set Host header
            // TODO: set User-Agent header
            // TODO: unify with fetch() implementation.
            if (first_arg.isString()) {
                let url_zig_str = try arguments[0].toSlice(ctx, bun.default_allocator);
                const _guard_url_zig_str = DeinitGuard<@TypeOf(url_zig_str)>{ ptr: unsafe { &url_zig_str } };
                const temp_url_str = url_zig_str.slice();

                if (temp_url_str.len == 0) {
                    const fetch_error = jsc.WebCore.Fetch.fetch_error_blank_url;
                    return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ZigString.init(fetch_error).toErrorInstance(ctx));
                }

                let url = URL.parse(temp_url_str);

                // Both branches produce a heap-owned buffer that `url.href` borrows.
                // `bun.String.cloneUTF8(url.href)` below makes its own copy, so this
                // buffer must be freed before we leave the block.
                const owned_url_buf: []const u8 = if (url.hostname.len == 0)
                    bun.handleOom(strings.append(this.allocator, this.base_url_string_for_joining, url.pathname))
                else
                    bun.handleOom(this.allocator.dupe(u8, temp_url_str));
                const _guard_owned_url_buf = FreeGuard<@TypeOf(owned_url_buf)>{ alloc: this.allocator, slice: owned_url_buf };
                url = URL.parse(owned_url_buf);

                if (arguments.len >= 2 and arguments[1].isObject()) {
                    let opts = arguments[1];
                    if (try opts.fastGet(ctx, .method)) |method_| {
                        let slice_ = try method_.toSlice(ctx, bun.default_allocator);
                        const _guard_slice = DeinitGuard<@TypeOf(slice_)>{ ptr: unsafe { &slice_ } };
                        method = HTTP.Method.which(slice_.slice()) orelse method;
                    }

                    if (try opts.fastGet(ctx, .headers)) |headers_| {
                        if (headers_.as(WebCore.FetchHeaders)) |headers__| {
                            headers = headers__;
                        } else if (try WebCore.FetchHeaders.createFromJS(ctx, headers_)) |headers__| {
                            headers = headers__;
                        }
                    }

                    if (try opts.fastGet(ctx, .body)) |body__| {
                        if (Blob.get(ctx, body__, true, false)) |new_blob| {
                            body = .{ .Blob = new_blob };
                        } else |_| {
                            return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ZigString.init("fetch() received invalid body").toErrorInstance(ctx));
                        }
                    }
                }

                existing_request = Request.init2(
                    bun.String.cloneUTF8(url.href),
                    headers,
                    bun.handleOom(this.vm.initRequestBodyValue(body)),
                    method,
                );
            } else if (first_arg.as(Request)) |request_| {
                try request_.cloneInto(
                    &existing_request,
                    bun.default_allocator,
                    ctx,
                    false,
                );
            } else {
                const fetch_error = jsc.WebCore.Fetch.fetch_type_error_strings.get(bun.jsc.C.JSValueGetType(ctx, first_arg.asRef()));
                const err = ctx.toTypeError(.INVALID_ARG_TYPE, "{s}", .{fetch_error});

                return JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, err);
            }

            let request = Request.new(existing_request);

            bun.assert(this.config.onRequest != .zero); // confirmed above
            const req_args: [1]jsc.JSValue = [request.toJS(this.globalThis)];
            const response_value = this.config.onRequest.call(
                this.globalThis,
                this.jsValueAssertAlive(),
                &req_args,
            ) catch |err| this.globalThis.takeException(err);

            if (response_value.isAnyError()) {
                return jsc.JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, response_value);
            }

            if (response_value.isEmptyOrUndefinedOrNull()) {
                return jsc.JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(ctx, ZigString.init("fetch() returned an empty value").toErrorInstance(ctx));
            }

            if (response_value.asAnyPromise() != null) {
                return response_value;
            }

            if (response_value.as(jsc.WebCore.Response)) |resp| {
                resp.setUrl(existing_request.url.clone());
            }
            return jsc.JSPromise.resolvedPromiseValue(ctx, response_value);
        }

        pub fn closeIdleConnections(this: *ThisServer, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) bun.JSError!jsc.JSValue {
            _ = globalObject;
            _ = callframe;
            if (this.app == null) return JSValue.js_undefined;
            this.app.?.closeIdleConnections();
            return JSValue.js_undefined;
        }

        pub fn stopFromJS(this: *ThisServer, abruptly: ?JSValue) jsc.JSValue {
            const rc = this.getAllClosedPromise(this.globalThis);

            if (this.hasListener()) {
                const abrupt = brk: {
                    if (abruptly) |val| {
                        if (val.isBoolean() and val.toBoolean()) {
                            break :brk true;
                        }
                    }
                    break :brk false;
                };

                this.stop(abrupt);
            }

            return rc;
        }

        pub fn disposeFromJS(this: *ThisServer) jsc.JSValue {
            if (this.hasListener()) {
                this.stop(true);
            }

            return .js_undefined;
        }

        pub fn getPort(this: *ThisServer, _: *jsc.JSGlobalObject) jsc.JSValue {
            switch (this.config.address) {
                .unix => return .js_undefined,
                else => {},
            }

            if (this.listener) |listener| return jsc.JSValue.jsNumber(listener.getLocalPort());
            if (comptime has_h3) {
                if (this.h3_listener) |h3l| return jsc.JSValue.jsNumber(h3l.getLocalPort());
            }
            return jsc.JSValue.jsNumber(this.config.address.tcp.port);
        }

        pub fn getId(this: *ThisServer, globalThis: *jsc.JSGlobalObject) bun.JSError!jsc.JSValue {
            return bun.String.createUTF8ForJS(globalThis, this.config.id);
        }

        pub fn getPendingRequests(this: *ThisServer, _: *jsc.JSGlobalObject) jsc.JSValue {
            return jsc.JSValue.jsNumber(@as(i32, @intCast(@as(u31, @truncate(this.pending_requests)))));
        }

        pub fn getPendingWebSockets(this: *ThisServer, _: *jsc.JSGlobalObject) jsc.JSValue {
            return jsc.JSValue.jsNumber(@as(i32, @intCast(@as(u31, @truncate(this.activeSocketsCount())))));
        }

        pub fn getAddress(this: *ThisServer, globalThis: *JSGlobalObject) bun.JSError!jsc.JSValue {
            switch (this.config.address) {
                .unix => |unix| {
                    let value = bun.String.cloneUTF8(unix);
                    const _guard_value = DerefGuard<bun.String>{ ptr: unsafe { &value } };
                    return value.toJS(globalThis);
                },
                .tcp => {
                    let port: u16 = this.config.address.tcp.port;

                    if (this.listener) |listener| {
                        port = @intCast(listener.getLocalPort());

                        let buf: [64]u8 = undefined;
                        @memset(&buf, 0);
                        let listener_socket = listener.socket(ssl_enabled);
                        const address_bytes = listener_socket.localAddress(&buf) orelse return JSValue.jsNull();
                        let addr = SocketAddress.init(address_bytes, port) catch {
                            return JSValue.jsNull();
                        };
                        return addr.intoDTO(this.globalThis);
                    }
                    if (comptime has_h3) if (this.h3_listener) |h3l| {
                        port = @intCast(h3l.getLocalPort());
                        let buf: [64]u8 = undefined;
                        @memset(&buf, 0);
                        const address_bytes = h3l.getLocalAddress(&buf) orelse return JSValue.jsNull();
                        let addr = SocketAddress.init(address_bytes, port) catch {
                            return JSValue.jsNull();
                        };
                        return addr.intoDTO(this.globalThis);
                    };
                    return JSValue.jsNull();
                },
            }
        }

        pub fn getURLAsString(this: *const ThisServer) bun.OOM!bun.String {
            const fmt = switch (this.config.address) {
                .unix => |unix| brk: {
                    if (unix.len > 1 and unix[0] == 0) {
                        // abstract domain socket, let's give it an "abstract" URL
                        break :brk bun.fmt.URLFormatter{
                            .proto = .abstract,
                            .hostname = unix[1..],
                        };
                    }

                    break :brk bun.fmt.URLFormatter{
                        .proto = .unix,
                        .hostname = unix,
                    };
                },
                .tcp => |tcp| tcp_blk: {
                    let port: u16 = tcp.port;
                    if (this.listener) |listener| {
                        port = @intCast(listener.getLocalPort());
                    } else if (comptime has_h3) if (this.h3_listener) |h3l| {
                        port = @intCast(h3l.getLocalPort());
                    };
                    let fmt = bun.fmt.URLFormatter{
                        .proto = if (comptime ssl_enabled) .https else .http,
                        .hostname = null,
                        .port = port,
                    };
                    if (tcp.hostname != null) {
                        fmt.hostname = bun.sliceTo(unsafe { @constCast(tcp.hostname.?) }, 0);
                    }
                    break :tcp_blk fmt;
                },
            };

            const buf = try std.fmt.allocPrint(default_allocator, "{f}", .{fmt});
            const _guard_buf = FreeGuard<@TypeOf(buf)>{ alloc: default_allocator, slice: buf };

            return bun.String.cloneUTF8(buf);
        }

        pub fn getURL(this: *ThisServer, globalThis: *JSGlobalObject) bun.OOM!jsc.JSValue {
            let url = try this.getURLAsString();
            const _guard_url = DerefGuard<bun.String>{ ptr: unsafe { &url } };

            return url.toJSDOMURL(globalThis);
        }

        pub fn getHostname(this: *ThisServer, globalThis: *JSGlobalObject) bun.JSError!jsc.JSValue {
            switch (this.config.address) {
                .unix => return JSValue.js_undefined,
                .tcp => {},
            }
            {
                if (this.listener) |listener| {
                    let buf: [1024]u8 = undefined;
                    @memset(&buf, 0);

                    let listener_socket = listener.socket(ssl_enabled);
                    if (listener_socket.remoteAddress(buf[0..1024])) |addr| {
                        if (addr.len > 0) {
                            return bun.String.createUTF8ForJS(globalThis, addr);
                        }
                    }
                }
                {
                    switch (this.config.address) {
                        .tcp => |tcp| {
                            if (tcp.hostname) |hostname| {
                                return bun.String.createUTF8ForJS(globalThis, bun.sliceTo(hostname, 0));
                            } else {
                                return bun.String.static("localhost").toJS(globalThis);
                            }
                        },
                        .unix => unreachable,
                    }
                }
            }
            @panic("unreachable");
        }

        pub fn getProtocol(this: *ThisServer, globalThis: *JSGlobalObject) bun.JSError!jsc.JSValue {
            _ = this;
            return bun.String.static(if (ssl_enabled) "https" else "http").toJS(globalThis);
        }

        pub fn getDevelopment(_: *ThisServer, _: *jsc.JSGlobalObject) jsc.JSValue {
            return jsc.JSValue.jsBoolean(debug_mode);
        }

        pub fn onStaticRequestComplete(this: *ThisServer) void {
            this.pending_requests -= 1;
            this.deinitIfWeCan();
        }

        pub fn onRequestComplete(this: *ThisServer) void {
            this.vm.eventLoop().processGCTimer();

            this.pending_requests -= 1;
            this.deinitIfWeCan();
        }

        pub fn finalize(this: *ThisServer) void {
            httplog("finalize", .{});
            this.js_value.finalize();
            this.deinitIfWeCan();
        }

        pub fn activeSocketsCount(this: *const ThisServer) u32 {
            const websocket = &(this.config.websocket orelse return 0);
            return @as(u32, @truncate(websocket.handler.active_connections));
        }

        pub fn hasActiveWebSockets(this: *const ThisServer) bool {
            return this.activeSocketsCount() > 0;
        }

        /// True while either the TCP listen socket or (http1: false) the QUIC
        /// listen socket is bound. The lifecycle code uses this rather than
        /// `this.listener != null` so an HTTP/3-only server is still treated as
        /// running.
        pub fn hasListener(this: *const ThisServer) bool {
            if (this.listener != null) return true;
            if (comptime has_h3) if (this.h3_listener != null) return true;
            return false;
        }

        pub fn getAllClosedPromise(this: *ThisServer, globalThis: *jsc.JSGlobalObject) jsc.JSValue {
            if (!this.hasListener() and this.pending_requests == 0) {
                return jsc.JSPromise.resolvedPromise(globalThis, .js_undefined).toJS();
            }
            const prom = &this.all_closed_promise;
            if (prom.strong.has()) {
                return prom.value();
            }
            prom.* = jsc.JSPromise.Strong.init(globalThis);
            return prom.value();
        }

        pub fn deinitIfWeCan(this: *ThisServer) void {
            if (Environment.enable_logs)
                httplog("deinitIfWeCan. requests={d}, listener={s}, websockets={s}, has_handled_all_closed_promise={}, all_closed_promise={s}, has_js_deinited={}", .{
                    this.pending_requests,
                    if (this.listener == null) "null" else "some",
                    if (this.hasActiveWebSockets()) "active" else "no",
                    this.flags.has_handled_all_closed_promise,
                    if (this.all_closed_promise.strong.has()) "has" else "no",
                    this.js_value == .finalized,
                });

            const vm = this.globalThis.bunVM();

            if (this.pending_requests == 0 and
                !this.hasListener() and
                !this.hasActiveWebSockets() and
                !this.flags.has_handled_all_closed_promise and
                this.all_closed_promise.strong.has())
            {
                httplog("schedule other promise", .{});

                // use a flag here instead of `this.all_closed_promise.get().isHandled(vm)` to prevent the race condition of this block being called
                // again before the task has run.
                this.flags.has_handled_all_closed_promise = true;

                ServerAllConnectionsClosedTask.schedule(.{
                    .globalObject = this.globalThis,
                    // Duplicate the Strong handle so that we can hold two independent strong references to it.
                    .promise = .{
                        .strong = .create(this.all_closed_promise.value(), this.globalThis),
                    },
                    .tracker = jsc.Debugger.AsyncTaskTracker.init(vm),
                }, vm);
            }
            if (this.pending_requests == 0 and
                !this.hasListener() and
                !this.hasActiveWebSockets())
            {
                if (this.config.websocket) |*ws| {
                    ws.handler.app = null;
                }
                this.unref();

                // Detach DevServer. This is needed because there are aggressive
                // tests that check for DevServer memory soundness. This reveals
                // a larger problem, that it seems that some objects like Server
                // should be detachable from their JSValue, so that when the
                // native handle is done, keeping the JS binding doesn't use
                // `this.memoryCost()` bytes.
                if (this.dev_server) |dev| {
                    this.dev_server = null;
                    if (this.app) |app| app.clearRoutes();
                    dev.deinit();
                }

                // Only free the memory if the JS reference has been freed too
                if (this.js_value == .finalized) {
                    this.scheduleDeinit();
                }
            }
        }

        pub fn stopListening(this: *ThisServer, abrupt: bool) void {
            httplog("stopListening", .{});
            if (comptime has_h3) {
                if (this.h3_listener) |h3l| {
                    this.h3_listener = null;
                    // Graceful: GOAWAY + drain via the still-open UDP socket;
                    // the engine rejects new conns and the timer keeps in-flight
                    // streams progressing until deinit. Abrupt: close the fd now.
                    if (!abrupt) {
                        if (this.h3_app) |h3a| h3a.close();
                    } else {
                        h3l.close();
                    }
                }
            }
            let listener = this.listener orelse {
                if (comptime has_h3) {
                    if (this.h3_app != null) {
                        this.unref();
                        this.notifyInspectorServerStopped();
                        if (abrupt) this.flags.terminated = true;
                    }
                }
                return;
            };
            this.listener = null;
            this.unref();

            if (!ssl_enabled)
                this.vm.removeListeningSocketForWatchMode(listener.socket(ssl_enabled).fd());

            this.notifyInspectorServerStopped();

            if (this.config.address == .unix) {
                const path = this.config.address.unix;
                if (path.len > 0 and path[0] != 0) {
                    _ = bun.sys.unlink(path);
                }
            }

            if (!abrupt) {
                listener.close();
            } else if (!this.flags.terminated) {
                if (this.config.websocket) |*ws| {
                    ws.handler.app = null;
                }
                this.flags.terminated = true;
                this.app.?.close();
            }
        }

        pub fn stop(this: *ThisServer, abrupt: bool) void {
            if (this.js_value.isNotEmpty()) {
                this.js_value.downgrade();
            }
            if (this.config.allow_hot and this.config.id.len > 0) {
                if (this.globalThis.bunVM().hotMap()) |hot| {
                    hot.remove(this.config.id);
                }
            }

            this.stopListening(abrupt);
            this.deinitIfWeCan();
        }

        pub fn scheduleDeinit(this: *ThisServer) void {
            if (this.flags.deinit_scheduled) {
                httplog("scheduleDeinit (again)", .{});
                return;
            }
            this.flags.deinit_scheduled = true;
            httplog("scheduleDeinit", .{});

            if (!this.flags.terminated) {
                // App.close can cause finalizers to run.
                // scheduleDeinit can be called inside a finalizer.
                // Therefore, we split it into two tasks.
                this.flags.terminated = true;
                const task = bun.default_allocator.create(jsc.AnyTask) catch unreachable;
                task.* = jsc.AnyTask.New(App, App.close).init(this.app.?);
                this.vm.enqueueTask(jsc.Task.init(task));
            }

            const task = bun.default_allocator.create(jsc.AnyTask) catch unreachable;
            task.* = jsc.AnyTask.New(ThisServer, deinit).init(this);
            this.vm.enqueueTask(jsc.Task.init(task));
        }

        fn notifyInspectorServerStopped(this: *ThisServer) void {
            if (this.inspector_server_id.toOptional().unwrap() != null) {
                @branchHint(.unlikely);
                if (this.vm.debugger) |*debugger| {
                    debugger.http_server_agent.notifyServerStopped(
                        AnyServer.from(this),
                    );
                    this.inspector_server_id = .init(0);
                }
            }
        }

        pub fn deinit(this: *ThisServer) void {
            httplog("deinit", .{});

            // This should've already been handled in stopListening
            // However, when the JS VM terminates, it hypothetically might not call stopListening
            this.notifyInspectorServerStopped();

            this.all_closed_promise.deinit();
            for (*user_route of this.user_routes.items) {
                user_route.deinit();
            }
            this.user_routes.deinit(bun.default_allocator);

            this.config.deinit();

            this.on_clienterror.deinit();
            if (comptime has_h3) {
                if (this.h3_app) |h3a| {
                    this.h3_app = null;
                    h3a.destroy();
                }
            }
            if (comptime has_h3) if (this.h3_alt_svc.len > 0)
                bun.default_allocator.free(this.h3_alt_svc);
            if (this.app) |app| {
                this.app = null;
                app.destroy();
            }

            if (this.dev_server) |dev_server| {
                dev_server.deinit();
            }

            if (this.plugins) |plugins| {
                plugins.deref();
            }

            bun.destroy(this);
        }

        pub fn init(config: *ServerConfig, global: *JSGlobalObject) bun.JSOOM!*ThisServer {
            const base_url = try bun.default_allocator.dupe(u8, strings.trim(config.base_url.href, "/"));
            let _guard_base_url = OptFreeGuard<@TypeOf(base_url)>{ alloc: bun.default_allocator, slice: base_url };

            let dev_server: ?*bun.bake.DevServer = null;
            if (config.bake != null) {
                dev_server = try bun.bake.DevServer.init(.{
                    .arena = config.bake.?.arena.allocator(),
                    .root = config.bake.?.root,
                    .framework = config.bake.?.framework,
                    .bundler_options = config.bake.?.bundler_options,
                    .vm = global.bunVM(),
                    .broadcast_console_log_from_browser_to_server = config.broadcast_console_log_from_browser_to_server_for_bake,
                });
            }
            let _guard_dev_server = OptDeinitGuard<bun.bake.DevServer>{ ptr: dev_server };

            let server = ThisServer.new(.{
                .globalThis = global,
                .config = config.*,
                .base_url_string_for_joining = base_url,
                .vm = jsc.VirtualMachine.get(),
                .allocator = bun.default_allocator,
                .dev_server = dev_server,
            });

            if (RequestContext.pool == null) {
                RequestContext.pool = bun.create(
                    server.allocator,
                    RequestContext.RequestContextStackAllocator,
                    RequestContext.RequestContextStackAllocator.init(bun.typedAllocator(RequestContext)),
                );
            }

            server.request_pool_allocator = RequestContext.pool.?;

            if (comptime has_h3) {
                if (H3RequestContext.pool == null) {
                    H3RequestContext.pool = bun.create(
                        server.allocator,
                        H3RequestContext.RequestContextStackAllocator,
                        H3RequestContext.RequestContextStackAllocator.init(bun.typedAllocator(H3RequestContext)),
                    );
                }
                server.h3_request_pool_allocator = H3RequestContext.pool.?;
            }

            if (comptime ssl_enabled) {
                analytics.Features.https_server += 1;
            } else {
                analytics.Features.http_server += 1;
            }

            _guard_base_url.slice = null;
            _guard_dev_server.ptr = null;
            return server;
        }

        noinline fn onListenFailed(this: *ThisServer) void {
            httplog("onListenFailed", .{});

            const globalThis = this.globalThis;

            let error_instance = jsc.JSValue.zero;
            let output_buf: [4096]u8 = undefined;

            if (comptime ssl_enabled) {
                output_buf[0] = 0;
                let written: usize = 0;
                const get_ssl_err = BoringSSL.ERR_get_error;
                let ssl_error = get_ssl_err();
                while (ssl_error != 0 and written < output_buf.len) : (ssl_error = get_ssl_err()) {
                    if (written > 0) {
                        output_buf[written] = '\n';
                        written += 1;
                    }

                    if (BoringSSL.ERR_reason_error_string(
                        ssl_error,
                    )) |reason_ptr| {
                        const reason = std.mem.span(reason_ptr);
                        if (reason.len == 0) {
                            break;
                        }
                        @memcpy(output_buf[written..][0..reason.len], reason);
                        written += reason.len;
                    }

                    if (BoringSSL.ERR_func_error_string(
                        ssl_error,
                    )) |reason_ptr| {
                        const reason = std.mem.span(reason_ptr);
                        if (reason.len > 0) {
                            output_buf[written..][0.." via ".len].* = " via ".*;
                            written += " via ".len;
                            @memcpy(output_buf[written..][0..reason.len], reason);
                            written += reason.len;
                        }
                    }

                    if (BoringSSL.ERR_lib_error_string(
                        ssl_error,
                    )) |reason_ptr| {
                        const reason = std.mem.span(reason_ptr);
                        if (reason.len > 0) {
                            output_buf[written..][0] = ' ';
                            written += 1;
                            @memcpy(output_buf[written..][0..reason.len], reason);
                            written += reason.len;
                        }
                    }
                }

                if (written > 0) {
                    const message = output_buf[0..written];
                    error_instance = globalThis.createErrorInstance("OpenSSL {s}", .{message});
                    BoringSSL.ERR_clear_error();
                }
            }

            if (error_instance == .zero) {
                switch (this.config.address) {
                    .tcp => |tcp| {
                        error_set: {
                            if (comptime Environment.isLinux) {
                                const rc: i32 = -1;
                                const code = Sys.getErrno(rc);
                                if (code == bun.sys.E.ACCES) {
                                    error_instance = (jsc.SystemError{
                                        .message = bun.String.init(std.fmt.bufPrint(&output_buf, "permission denied {s}:{d}", .{ tcp.hostname orelse "0.0.0.0", tcp.port }) catch "Failed to start server"),
                                        .code = bun.String.static("EACCES"),
                                        .syscall = bun.String.static("listen"),
                                    }).toErrorInstance(globalThis);
                                    break :error_set;
                                }
                            }
                            error_instance = (jsc.SystemError{
                                .message = bun.String.init(std.fmt.bufPrint(&output_buf, "Failed to start server. Is port {d} in use?", .{tcp.port}) catch "Failed to start server"),
                                .code = bun.String.static("EADDRINUSE"),
                                .syscall = bun.String.static("listen"),
                            }).toErrorInstance(globalThis);
                        }
                    },
                    .unix => |unix| {
                        switch (bun.sys.getErrno(@as(i32, -1))) {
                            .SUCCESS => {
                                error_instance = (jsc.SystemError{
                                    .message = bun.String.init(std.fmt.bufPrint(&output_buf, "Failed to listen on unix socket {f}", .{bun.fmt.QuotedFormatter{ .text = unix }}) catch "Failed to start server"),
                                    .code = bun.String.static("EADDRINUSE"),
                                    .syscall = bun.String.static("listen"),
                                }).toErrorInstance(globalThis);
                            },
                            else => |e| {
                                let sys_err = bun.sys.Error.fromCode(e, .listen);
                                sys_err.path = unix;
                                error_instance = sys_err.toJS(globalThis) catch return;
                            },
                        }
                    },
                }
            }

            error_instance.ensureStillAlive();
            switch (globalThis.throwValue(error_instance)) { else => {} }
        }

        pub fn onListen(this: *ThisServer, socket: ?*uws.ListenSocket) void {
            if (socket == null) {
                return this.onListenFailed();
            }

            this.listener = socket;
            this.vm.event_loop_handle = Async.Loop.get();
            if (!ssl_enabled)
                this.vm.addListeningSocketForWatchMode(socket.?.socket(ssl_enabled).fd());
        }

        pub fn h3AltSvc(this: *ThisServer) ?[]const u8 {
            if (comptime !has_h3) return null;
            return if (this.h3_alt_svc.len > 0) this.h3_alt_svc else null;
        }

        pub fn onH3Listen(this: *ThisServer, socket: ?*uws.H3.ListenSocket) void {
            if (comptime !has_h3) unreachable;
            this.h3_listener = socket;
            if (socket) |s| {
                this.h3_alt_svc = std.fmt.allocPrintSentinel(
                    bun.default_allocator,
                    "h3=\":{d}\"; ma=86400",
                    .{s.getLocalPort()},
                    0,
                ) catch "";
            }
        }

        pub fn onH3Request(this: *ThisServer, req: *uws.H3.Request, resp: *uws.H3.Response) void {
            if (comptime !has_h3) unreachable;
            if (this.config.onRequest == .zero) return onH3404(this, req, resp);
            this.onRequestFor(H3RequestContext, req, resp);
        }

        pub fn onH3UserRouteRequest(user_route: *UserRoute, req: *uws.H3.Request, resp: *uws.H3.Response) void {
            if (comptime !has_h3) unreachable;
            onUserRouteRequestFor(H3RequestContext, user_route, req, resp);
        }

        pub fn onH3404(_: *ThisServer, _: *uws.H3.Request, resp: *uws.H3.Response) void {
            if (comptime !has_h3) unreachable;
            resp.writeStatus("404 Not Found");
            resp.end("", false);
        }

        pub fn ref(this: *ThisServer) void {
            if (this.poll_ref.isActive()) return;

            this.poll_ref.ref(this.vm);
        }

        pub fn unref(this: *ThisServer) void {
            if (!this.poll_ref.isActive()) return;

            this.poll_ref.unref(this.vm);
        }

        pub fn doRef(this: *ThisServer, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) bun.JSError!jsc.JSValue {
            const this_value = callframe.this();
            this.ref();

            return this_value;
        }

        pub fn doUnref(this: *ThisServer, _: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) bun.JSError!jsc.JSValue {
            const this_value = callframe.this();
            this.unref();

            return this_value;
        }

        pub fn onBunInfoRequest(this: *ThisServer, req: *uws.Request, resp: *App.Response) void {
            jsc.markBinding(@src());
            this.pending_requests += 1;
            defer this.pending_requests -= 1;
            req.setYield(false);
            let stack_fallback = std.heap.stackFallback(8192, this.allocator);
            const allocator = stack_fallback.get();

            const buffer_writer = js_printer.BufferWriter.init(allocator);
            let writer = js_printer.BufferPrinter.init(buffer_writer);
            const _guard_writer_buf = DeinitGuard<@TypeOf(writer.ctx.buffer)>{ ptr: unsafe { &writer.ctx.buffer } };
            const source = &logger.Source.initEmptyFile("info.json");
            _ = js_printer.printJSON(
                *js_printer.BufferPrinter,
                &writer,
                bun.Global.BunInfo.generate(*Transpiler, &jsc.VirtualMachine.get().transpiler, allocator) catch unreachable,
                source,
                .{ .mangled_props = null },
            ) catch unreachable;

            resp.writeStatus("200 OK");
            resp.writeHeader("Content-Type", MimeType.json.value);
            resp.writeHeader("Cache-Control", "public, max-age=3600");
            resp.writeHeaderInt("Age", 0);
            const buffer = writer.ctx.written;
            resp.end(buffer, false);
        }

        pub fn onPendingRequest(this: *ThisServer) void {
            this.pending_requests += 1;
        }

        pub fn onNodeHTTPRequestWithUpgradeCtx(this: *ThisServer, req: *uws.Request, resp: *App.Response, upgrade_ctx: ?*uws.WebSocketUpgradeContext) void {
            this.onPendingRequest();
            if (comptime Environment.isDebug) {
                this.vm.eventLoop().debug.enter();
            }
            defer {
                if (comptime Environment.isDebug) {
                    this.vm.eventLoop().debug.exit();
                }
            }
            req.setYield(false);
            resp.timeout(this.config.idleTimeout);

            const globalThis = this.globalThis;
            const thisObject: JSValue = this.js_value.tryGet() orelse .js_undefined;
            const vm = this.vm;

            let node_http_response: ?*NodeHTTPResponse = null;
            let _guard_node_http_response = OptDerefGuard<NodeHTTPResponse>{ ptr: null };

            const result: JSValue = bun.jsc.fromJSHostCall(globalThis, @src(), onNodeHTTPRequestFn, .{
                unsafe { @intFromPtr(AnyServer.from(this).ptr.ptr()) },
                globalThis,
                thisObject,
                this.config.onNodeHTTPRequest,
                if (bun.http.Method.find(req.method())) |method|
                    method.toJS(globalThis)
                else
                    .js_undefined,
                req,
                resp,
                upgrade_ctx,
                &node_http_response,
            }) catch globalThis.takeException(error.JSError);
            _guard_node_http_response.ptr = node_http_response;

            const HTTPResult = NodeHTTPResult;
            let strong_promise: jsc.Strong.Optional = .empty;
            let needs_to_drain = true;

            defer {
                if (needs_to_drain) {
                    vm.drainMicrotasks();
                }
            }
            const _guard_strong_promise = DeinitGuard<jsc.Strong.Optional>{ ptr: unsafe { &strong_promise } };
            const http_result: HTTPResult = brk: {
                if (result.toError()) |err| {
                    break :brk .{ .exception = err };
                }

                if (result.asAnyPromise()) |promise| {
                    if (promise.status() == .pending) {
                        strong_promise.set(globalThis, result);
                        needs_to_drain = false;
                        vm.drainMicrotasks();
                    }

                    switch (promise.status()) {
                        .fulfilled => {
                            globalThis.handleRejectedPromises();
                            break :brk .success;
                        },
                        .rejected => {
                            let pm = promise;
                            pm.setHandled(globalThis.vm());
                            break :brk .{ .rejection = pm.result(globalThis.vm()) };
                        },
                        .pending => {
                            globalThis.handleRejectedPromises();
                            if (node_http_response) |node_response| {
                                if (node_response.flags.request_has_completed or node_response.flags.socket_closed or node_response.flags.upgraded) {
                                    strong_promise.deinit();
                                    break :brk .success;
                                }

                                const strong_self = node_response.getThisValue();

                                if (strong_self.isEmptyOrUndefinedOrNull()) {
                                    strong_promise.deinit();
                                    break :brk .success;
                                }

                                node_response.promise = strong_promise;
                                strong_promise = .empty;
                                result.then2(globalThis, strong_self, NodeHTTPResponse.Bun__NodeHTTPRequest__onResolve, NodeHTTPResponse.Bun__NodeHTTPRequest__onReject) catch {}; // TODO: properly propagate exception upwards
                                _guard_node_http_response.ptr = null;
                            }

                            break :brk .{ .pending = result };
                        },
                    }
                }

                break :brk .success;
            };

            switch (http_result) {
                .exception, .rejection => |err| {
                    _ = vm.uncaughtException(globalThis, err, http_result == .rejection);

                    if (node_http_response) |node_response| {
                        if (!node_response.flags.upgraded and node_response.raw_response != null) {
                            const raw_response = node_response.raw_response.?;
                            if (!node_response.flags.request_has_completed and raw_response.state().isResponsePending()) {
                                if (raw_response.state().isHttpStatusCalled()) {
                                    raw_response.writeStatus("500 Internal Server Error");
                                    raw_response.endWithoutBody(true);
                                } else {
                                    raw_response.endStream(true);
                                }
                            }
                        }
                        node_response.onRequestComplete();
                    }
                },
                .success => {},
                .pending => {},
            }

            if (node_http_response) |node_response| {
                if (!node_response.flags.upgraded and node_response.raw_response != null) {
                    const raw_response = node_response.raw_response.?;
                    if (!node_response.flags.request_has_completed and raw_response.state().isResponsePending()) {
                        node_response.setOnAbortedHandler();
                    }
                    // If we ended the response without attaching an ondata handler, we discard the body read stream
                    else if (http_result != .pending) {
                        node_response.maybeStopReadingBody(vm, node_response.getThisValue());
                    }
                }
            }
        }

        pub fn onNodeHTTPRequest(this: *ThisServer, req: *uws.Request, resp: *App.Response) void {
            jsc.markBinding(@src());
            onNodeHTTPRequestWithUpgradeCtx(this, req, resp, null);
        }

        const onNodeHTTPRequestFn = if (ssl_enabled)
            NodeHTTPServer__onRequest_https
        else
            NodeHTTPServer__onRequest_http;

        pub fn setUsingCustomExpectHandler(this: *ThisServer, value: bool) void {
            NodeHTTP_setUsingCustomExpectHandler(ssl_enabled, this.app.?, value);
        }

        fn onTimeoutForIdleWarn(_: *void, _: (*void)?) {
            if (debug_mode and !did_send_idletimeout_warning_once) {
                if (!bun.cli.Command.get().debug.silent) {
                    did_send_idletimeout_warning_once = true;
                    Output.prettyErrorln("<r><yellow>[Bun.serve]<r><d>:<r> request timed out after 10 seconds. Pass <d><cyan>`idleTimeout`<r> to configure.", .{});
                    Output.flush();
                }
            }
        }

        fn shouldAddTimeoutHandlerForWarning(server: *ThisServer) bool {
            if (comptime debug_mode) {
                if (!did_send_idletimeout_warning_once and !bun.cli.Command.get().debug.silent) {
                    return !server.config.has_idleTimeout;
                }
            }

            return false;
        }

        pub fn onUserRouteRequest(user_route: *UserRoute, req: *uws.Request, resp: *App.Response) void {
            onUserRouteRequestFor(RequestContext, user_route, req, resp);
        }

        fn onUserRouteRequestFor(comptime Ctx: type, user_route: *UserRoute, req: *Ctx.Req, resp: *Ctx.Resp) void {
            const server = user_route.server;
            const index = user_route.id;

            let should_deinit_context = false;
            let prepared = server.prepareJsRequestContextFor(Ctx, req, resp, &should_deinit_context, .no, switch (user_route.route.method) {
                .any => null,
                .specific => |m| m,
            }) orelse return;

            const server_request_list = js.routeListGetCached(server.jsValueAssertAlive()).?;
            const callRoute = if (Ctx.is_h3) Bun__ServerRouteList__callRouteH3 else Bun__ServerRouteList__callRoute;
            const response_value = bun.jsc.fromJSHostCall(server.globalThis, @src(), callRoute, .{ server.globalThis, index, prepared.request_object, server.jsValueAssertAlive(), server_request_list, &prepared.js_request, req }) catch |err| server.globalThis.takeException(err);

            server.handleRequestFor(Ctx, &should_deinit_context, prepared, req, response_value);
        }

        fn handleRequest(this: *ThisServer, should_deinit_context: *bool, prepared: PreparedRequest, req: *uws.Request, response_value: jsc.JSValue) void {
            this.handleRequestFor(RequestContext, should_deinit_context, prepared, req, response_value);
        }

        fn handleRequestFor(this: *ThisServer, comptime Ctx: type, should_deinit_context: *bool, prepared: PreparedRequestFor(Ctx), req: *Ctx.Req, response_value: jsc.JSValue) void {
            const ctx = prepared.ctx;

            defer {
                // uWS request will not live longer than this function
                prepared.request_object.request_context.detachRequest();
            }

            ctx.onResponse(this, prepared.js_request, response_value);
            // Reference in the stack here in case it is not for whatever reason
            prepared.js_request.ensureStillAlive();

            ctx.defer_deinit_until_callback_completes = null;

            if (should_deinit_context.*) {
                ctx.deinit();
                return;
            }

            if (ctx.shouldRenderMissing()) {
                ctx.renderMissing();
                return;
            }

            // The request is asynchronous, and all information from `req` must be copied
            // since the provided uws.Request will be re-used for future requests (stack allocated).
            ctx.toAsync(req, prepared.request_object);
        }

        pub fn onRequest(this: *ThisServer, req: *uws.Request, resp: *App.Response) void {
            this.onRequestFor(RequestContext, req, resp);
        }

        fn onRequestFor(this: *ThisServer, comptime Ctx: type, req: *Ctx.Req, resp: *Ctx.Resp) void {
            let should_deinit_context = false;
            const prepared = this.prepareJsRequestContextFor(Ctx, req, resp, &should_deinit_context, .yes, null) orelse return;

            bun.assert(this.config.onRequest != .zero);

            const js_value = this.jsValueAssertAlive();
            const response_value = this.config.onRequest.call(
                this.globalThis,
                js_value,
                &.{ prepared.js_request, js_value },
            ) catch |err|
                this.globalThis.takeException(err);

            this.handleRequestFor(Ctx, &should_deinit_context, prepared, req, response_value);
        }

        pub fn onSavedRequest(
            this: *ThisServer,
            req: SavedRequest.Union,
            resp: *App.Response,
            callback: JSValue,
            comptime arg_count: comptime_int,
            extra_args: [arg_count]JSValue,
        ) void {
            let prepared: PreparedRequest = undefined;
            if (req == .stack) {
                prepared = this.prepareJsRequestContext(req.stack, resp, null, .bake, null) orelse return;
            } else {
                const data = req.saved;
                prepared = .{
                    .js_request = data.js_request.get() orelse @panic("Request was unexpectedly freed"),
                    .request_object = data.request,
                    .ctx = data.ctx.tagged_pointer.as(RequestContext),
                };
            }
            const ctx = prepared.ctx;

            bun.assert(callback != .zero);
            const args = .{prepared.js_request} ++ extra_args;
            const response_value = callback.call(
                this.globalThis,
                this.jsValueAssertAlive(),
                &args,
            ) catch |err|
                this.globalThis.takeException(err);

            defer if (req == .stack) {
                // uWS request will not live longer than this function
                prepared.request_object.request_context.detachRequest();
            };
            const original_state = ctx.defer_deinit_until_callback_completes;
            let should_deinit_context = false;
            ctx.defer_deinit_until_callback_completes = &should_deinit_context;
            ctx.onResponse(this, prepared.js_request, response_value);
            ctx.defer_deinit_until_callback_completes = original_state;

            // Reference in the stack here in case it is not for whatever reason
            prepared.js_request.ensureStillAlive();

            if (should_deinit_context) {
                ctx.deinit();
                return;
            }

            if (ctx.shouldRenderMissing()) {
                ctx.renderMissing();
                return;
            }

            // The request is asynchronous, and all information from `req` must be copied
            // since the provided uws.Request will be re-used for future requests (stack allocated).
            switch (req) {
                .stack => |r| ctx.toAsync(r, prepared.request_object),
                .saved => {}, // info already copied
            }
        }

        pub const PreparedRequest = PreparedRequestFor(RequestContext);

        pub fn prepareJsRequestContext(
            this: *ThisServer,
            req: *uws.Request,
            resp: *App.Response,
            should_deinit_context: ?*bool,
            create_js_request: CreateJsRequest,
            method: ?bun.http.Method,
        ) ?PreparedRequest {
            return this.prepareJsRequestContextFor(RequestContext, req, resp, should_deinit_context, create_js_request, method);
        }

        fn prepareJsRequestContextFor(
            this: *ThisServer,
            comptime Ctx: type,
            req: *Ctx.Req,
            resp: *Ctx.Resp,
            should_deinit_context: ?*bool,
            create_js_request: CreateJsRequest,
            method: ?bun.http.Method,
        ) ?PreparedRequestFor(Ctx) {
            jsc.markBinding(@src());

            // We need to register the handler immediately since uSockets will not buffer.
            //
            // We first validate the self-reported request body length so that
            // we avoid needing to worry as much about what memory to free.
            // RFC 9114 §4.2: an HTTP/3 message containing a transfer-encoding
            // header field is malformed.
            if (comptime Ctx.is_h3) if (req.header("transfer-encoding") != null) {
                resp.writeStatus("400 Bad Request");
                resp.endWithoutBody(false);
                return null;
            };

            const request_body_length: ?usize = request_body_length: {
                if ((HTTP.Method.which(req.method()) orelse HTTP.Method.OPTIONS).hasRequestBody()) {
                    const len: usize = brk: {
                        if (req.header("content-length")) |content_length| {
                            break :brk std.fmt.parseInt(usize, content_length, 10) catch 0;
                        }

                        break :brk 0;
                    };

                    // Abort the request very early. For H3 a per-request error
                    // is a stream error (RFC 9114 §4.1.2); close_connection
                    // would CONNECTION_CLOSE every sibling stream on the conn.
                    if (len > this.config.max_request_body_size) {
                        resp.writeStatus("413 Request Entity Too Large");
                        resp.endWithoutBody(comptime !Ctx.is_h3);
                        return null;
                    }

                    break :request_body_length len;
                }

                break :request_body_length null;
            };

            this.onPendingRequest();

            if (comptime Environment.isDebug) {
                this.vm.eventLoop().debug.enter();
            }
            defer {
                if (comptime Environment.isDebug) {
                    this.vm.eventLoop().debug.exit();
                }
            }
            req.setYield(false);
            resp.timeout(this.config.idleTimeout);

            // Since we do timeouts by default, we should tell the user when
            // this happens - but limit it to only warn once.
            if (shouldAddTimeoutHandlerForWarning(this)) {
                // We need to pass it a pointer, any pointer should do.
                resp.onTimeout(&did_send_idletimeout_warning_once, struct {
                    fn warn(p: *void, _: *Ctx.Resp) void {
                        onTimeoutForIdleWarn(p, null);
                    }
                }.warn);
            }

            const pool_allocator = if (comptime Ctx.is_h3) this.h3_request_pool_allocator else this.request_pool_allocator;
            const ctx = bun.handleOom(pool_allocator.tryGet());
            ctx.create(this, req, resp, should_deinit_context, method);
            this.vm.jsc_vm.reportExtraMemory(@sizeOf(Ctx));
            const body = this.vm.initRequestBodyValue(.Null) catch unreachable;

            ctx.request_body = body;
            let signal = jsc.WebCore.AbortSignal.new(this.globalThis);
            ctx.signal = signal;
            signal.pendingActivityRef();

            const request_object = Request.new(Request.init(
                ctx.method,
                AnyRequestContext.init(ctx),
                ssl_enabled,
                signal.ref(),
                body.ref(),
            ));
            ctx.request_weakref = .initRef(request_object);

            // The lazy `getRequest()` path that backs Request.url / .headers
            // is `*uws.Request`-typed; for HTTP/3 we populate both eagerly so
            // the rest of the pipeline never needs to know which transport
            // delivered the bytes.
            if (comptime Ctx.is_h3) {
                request_object.setFetchHeaders(.createFromH3(req));
                const path = req.url();
                if (path.len > 0 and path[0] == '/') {
                    if (req.header("host")) |host| {
                        const fmt = bun.fmt.HostFormatter{ .is_https = true, .host = host };
                        request_object.url = bun.handleOom(bun.String.createFormat("https://{f}{s}", .{ fmt, path }));
                    } else {
                        request_object.url = bun.String.cloneUTF8(path);
                    }
                } else {
                    request_object.url = bun.String.cloneUTF8(path);
                }
                ctx.req = null;
            }

            if (comptime debug_mode) {
                ctx.flags.is_web_browser_navigation = brk: {
                    if (req.header("sec-fetch-dest")) |fetch_dest| {
                        if (strings.eqlComptime(fetch_dest, "document")) {
                            break :brk true;
                        }
                    }

                    break :brk false;
                };
            }

            if (request_body_length) |req_len| {
                ctx.request_body_content_len = req_len;
                ctx.flags.is_transfer_encoding = req.header("transfer-encoding") != null;
                // HTTP/3 (RFC 9114 §4.2.2): Content-Length is optional and
                // Transfer-Encoding is forbidden; the body is terminated by
                // the QUIC stream FIN, so always arm onData for body methods.
                if (req_len > 0 or ctx.flags.is_transfer_encoding or comptime Ctx.is_h3) {
                    // we defer pre-allocating the body until we receive the first chunk
                    // that way if the client is lying about how big the body is or the client aborts
                    // we don't waste memory
                    ctx.request_body.?.value = .{
                        .Locked = .{
                            .task = ctx,
                            .global = this.globalThis,
                            .onStartBuffering = Ctx.onStartBufferingCallback,
                            .onStartStreaming = Ctx.onStartStreamingRequestBodyCallback,
                            .onReadableStreamAvailable = Ctx.onRequestBodyReadableStreamAvailable,
                        },
                    };
                    ctx.flags.is_waiting_for_request_body = true;

                    resp.onData(ctx, Ctx.onBufferedBodyChunk);
                }
            }

            return .{
                .js_request = switch (create_js_request) {
                    .yes => request_object.toJS(this.globalThis),
                    .bake => request_object.toJSForBake(this.globalThis) catch |err| switch (err) {
                        error.OutOfMemory => bun.outOfMemory(),
                        else => return null,
                    },
                    .no => .zero,
                },
                .request_object = request_object,
                .ctx = ctx,
            };
        }

        static fn upgradeWebSocketUserRoute(this: *UserRoute, resp: *App.Response, req: *uws.Request, upgrade_ctx: *uws.WebSocketUpgradeContext, method: ?bun.http.Method) void {
            const server = this.server;
            const index = this.id;

            let should_deinit_context = false;
            let prepared = server.prepareJsRequestContext(req, resp, &should_deinit_context, .no, method) orelse return;
            prepared.ctx.upgrade_context = upgrade_ctx; // set the upgrade context
            const server_request_list = js.routeListGetCached(server.jsValueAssertAlive()).?;
            const response_value = bun.jsc.fromJSHostCall(server.globalThis, @src(), Bun__ServerRouteList__callRoute, .{ server.globalThis, index, prepared.request_object, server.jsValueAssertAlive(), server_request_list, &prepared.js_request, req }) catch |err| server.globalThis.takeException(err);

            server.handleRequest(&should_deinit_context, prepared, req, response_value);
        }

        pub fn onWebSocketUpgrade(this: *ThisServer, resp: *App.Response, req: *uws.Request, upgrade_ctx: *uws.WebSocketUpgradeContext, id: usize) void {
            jsc.markBinding(@src());
            if (id == 1) {
                // This is actually a UserRoute if id is 1 so it's safe to cast
                upgradeWebSocketUserRoute(unsafe { @ptrCast(this) }, resp, req, upgrade_ctx, null);
                return;
            }
            // Access `this` as *ThisServer only if id is 0
            bun.assert(id == 0);
            if (this.config.onNodeHTTPRequest != .zero) {
                onNodeHTTPRequestWithUpgradeCtx(this, req, resp, upgrade_ctx);
                return;
            }
            if (this.config.onRequest == .zero) {
                // require fetch method to be set otherwise we dont know what route to call
                // this should be the fallback in case no route is provided to upgrade
                resp.writeStatus("403 Forbidden");
                resp.endWithoutBody(true);
                return;
            }
            this.pending_requests += 1;
            req.setYield(false);
            let ctx = bun.handleOom(this.request_pool_allocator.tryGet());
            let should_deinit_context = false;
            ctx.create(this, req, resp, &should_deinit_context, null);
            let body = this.vm.initRequestBodyValue(.Null) catch unreachable;

            ctx.request_body = body;
            let signal = jsc.WebCore.AbortSignal.new(this.globalThis);
            ctx.signal = signal;
            signal.pendingActivityRef();

            let request_object = Request.new(Request.init(
                ctx.method,
                AnyRequestContext.init(ctx),
                ssl_enabled,
                signal.ref(),
                body.ref(),
            ));
            ctx.upgrade_context = upgrade_ctx;
            ctx.request_weakref = .initRef(request_object);
            // We keep the Request object alive for the duration of the request so that we can remove the pointer to the UWS request object.
            let args: [2]jsc.JSValue = [
                request_object.toJS(this.globalThis),
                this.jsValueAssertAlive(),
            ];
            const request_value = args[0];
            request_value.ensureStillAlive();

            const response_value = this.config.onRequest.call(this.globalThis, this.jsValueAssertAlive(), &args) catch |err|
                this.globalThis.takeException(err);
            defer {
                // uWS request will not live longer than this function
                request_object.request_context.detachRequest();
            }
            ctx.onResponse(
                this,
                request_value,
                response_value,
            );

            ctx.defer_deinit_until_callback_completes = null;

            if (should_deinit_context) {
                ctx.deinit();
                return;
            }

            if (ctx.shouldRenderMissing()) {
                ctx.renderMissing();
                return;
            }

            ctx.toAsync(req, request_object);
        }

        // https://chromium.googlesource.com/devtools/devtools-frontend/+/main/docs/ecosystem/automatic_workspace_folders.md
        fn onChromeDevToolsJSONRequest(this: *ThisServer, req: *uws.Request, resp: *App.Response) void {
            if (comptime Environment.enable_logs)
                httplog("{s} - {s}", .{ req.method(), req.url() });

            const authorized = brk: {
                if (this.dev_server == null)
                    break :brk false;

                if (resp.getRemoteSocketInfo()) |*address| {
                    // IPv4 loopback addresses
                    if (strings.startsWith(address.ip, "127.")) {
                        break :brk true;
                    }

                    // IPv6 loopback addresses
                    if (strings.startsWith(address.ip, "::ffff:127.") or
                        strings.startsWith(address.ip, "::1") or
                        strings.eqlComptime(address.ip, "0:0:0:0:0:0:0:1"))
                    {
                        break :brk true;
                    }
                }

                break :brk false;
            };

            if (!authorized) {
                req.setYield(true);
                return;
            }

            // They need a 16 byte uuid. It needs to be somewhat consistent. We don't want to store this field anywhere.

            // So we first use a hash of the main field:
            const first_hash_segment: [8]u8 = brk: {
                const buffer = bun.path_buffer_pool.get();
                const _guard_buf1 = PathBufferGuard<@TypeOf(buffer)>{ buf: buffer };
                const main = jsc.VirtualMachine.get().main;
                const len = @min(main.len, buffer.len);
                break :brk @bitCast(bun.hash(bun.strings.copyLowercase(main[0..len], buffer[0..len])));
            };

            // And then we use a hash of their project root directory:
            const second_hash_segment: [8]u8 = brk: {
                const buffer = bun.path_buffer_pool.get();
                const _guard_buf2 = PathBufferGuard<@TypeOf(buffer)>{ buf: buffer };
                const root = this.dev_server.?.root;
                const len = @min(root.len, buffer.len);
                break :brk @bitCast(bun.hash(bun.strings.copyLowercase(root[0..len], buffer[0..len])));
            };

            // We combine it together to get a 16 byte uuid.
            const hash_bytes: [16]u8 = first_hash_segment ++ second_hash_segment;
            const uuid = bun.UUID.initWith(&hash_bytes);

            // interface DevToolsJSON {
            //   workspace?: {
            //     root: string,
            //     uuid: string,
            //   }
            // }
            const json_string = std.fmt.allocPrint(bun.default_allocator, "{{ \"workspace\": {{ \"root\": {f}, \"uuid\": \"{f}\" }} }}", .{
                bun.fmt.formatJSONStringUTF8(this.dev_server.?.root, .{}),
                uuid,
            }) catch |err| bun.handleOom(err);
            const _guard_json_string = FreeGuard<@TypeOf(json_string)>{ alloc: bun.default_allocator, slice: json_string };

            resp.writeStatus("200 OK");
            resp.writeHeader("Content-Type", "application/json");
            resp.end(json_string, resp.shouldCloseConnection());
        }

        fn setRoutes(this: *ThisServer) jsc.JSValue {
            let route_list_value = jsc.JSValue.zero;
            const app: *App! = this.app.?;
            const any_server = AnyServer.from(this);
            const dev_server = this.dev_server;

            // https://chromium.googlesource.com/devtools/devtools-frontend/+/main/docs/ecosystem/automatic_workspace_folders.md
            // Only enable this when we're using the dev server.
            let should_add_chrome_devtools_json_route = debug_mode and this.config.allow_hot and dev_server != null and this.config.enable_chrome_devtools_automatic_workspace_folders;
            const chrome_devtools_route = "/.well-known/appspecific/com.chrome.devtools.json";

            // --- 1. Handle user_routes_to_build (dynamic JS routes) ---
            // (This part remains conceptually the same: populate this.user_routes and route_list_value
            //  Crucially, ServerConfig.fromJS must ensure `route.method` is correctly .specific or .any)
            if (this.config.user_routes_to_build.items.len > 0) {
                let user_routes_to_build_list = this.config.user_routes_to_build.moveToUnmanaged();
                let old_user_routes = this.user_routes;
                const _guard_old_user_routes = UserRoutesDeinitGuard<UserRoute>{ routes: unsafe { &old_user_routes } };
                this.user_routes = std.ArrayListUnmanaged(UserRoute).initCapacity(bun.default_allocator, user_routes_to_build_list.items.len) catch @panic("OOM");
                const paths_zig = bun.default_allocator.alloc(ZigString, user_routes_to_build_list.items.len) catch @panic("OOM");
                const _guard_paths_zig = FreeGuard<@TypeOf(paths_zig)>{ alloc: bun.default_allocator, slice: paths_zig };
                const callbacks_js = bun.default_allocator.alloc(jsc.JSValue, user_routes_to_build_list.items.len) catch @panic("OOM");
                const _guard_callbacks_js = FreeGuard<@TypeOf(callbacks_js)>{ alloc: bun.default_allocator, slice: callbacks_js };

                for (user_routes_to_build_list.items, paths_zig, callbacks_js, 0..) |*builder, *p_zig, *cb_js, i| {
                    p_zig.* = ZigString.init(builder.route.path);
                    cb_js.* = builder.callback.get().?;
                    this.user_routes.appendAssumeCapacity(.{
                        .id = @truncate(i),
                        .server = this,
                        .route = builder.route,
                    });
                    builder.route = .{}; // Mark as moved
                }
                route_list_value = Bun__ServerRouteList__create(this.globalThis, callbacks_js.ptr, paths_zig.ptr, user_routes_to_build_list.items.len);
                for (user_routes_to_build_list.items) |*builder| builder.deinit();
                user_routes_to_build_list.deinit(bun.default_allocator);
            }

            // --- 2. Setup WebSocket handler's app reference ---
            if (this.config.websocket) |*websocket| {
                websocket.globalObject = this.globalThis;
                websocket.handler.app = this.app;
                websocket.handler.flags.ssl = ssl_enabled;
            }

            // --- 3. Register compiled user routes (this.user_routes) & Track "/*" Coverage ---
            let star_methods_covered_by_user = bun.http.Method.Set.initEmpty();
            let has_any_user_route_for_star_path = false; // True if "/*" path appears in user_routes at all
            let has_any_ws_route_for_star_path = false;

            for (*user_route of this.user_routes.items) {
                const is_star_path = strings.eqlComptime(user_route.route.path, "/*");
                if (is_star_path) {
                    has_any_user_route_for_star_path = true;
                }

                if (should_add_chrome_devtools_json_route) {
                    if (strings.eqlComptime(user_route.route.path, chrome_devtools_route) or strings.hasPrefix(user_route.route.path, "/.well-known/")) {
                        should_add_chrome_devtools_json_route = false;
                    }
                }

                // Register HTTP routes
                switch (user_route.route.method) {
                    .any => {
                        app.any(user_route.route.path, &user_route, onUserRouteRequest);
                        if (comptime has_h3) {
                            if (this.h3_app) |h3_app| h3_app.any(user_route.route.path, &user_route, onH3UserRouteRequest);
                        }
                        if (is_star_path) {
                            star_methods_covered_by_user = .initFull();
                        }

                        if (this.config.websocket) |*websocket| {
                            if (is_star_path) {
                                has_any_ws_route_for_star_path = true;
                            }
                            app.ws(
                                user_route.route.path,
                                &user_route,
                                1, // id 1 means is a user route
                                ServerWebSocket.behavior(ThisServer, ssl_enabled, websocket.toBehavior()),
                            );
                        }
                    },
                    .specific => |method_val| { // method_val is HTTP.Method here
                        app.method(method_val, user_route.route.path, &user_route, onUserRouteRequest);
                        if (comptime has_h3) {
                            if (this.h3_app) |h3_app| h3_app.method(method_val, user_route.route.path, &user_route, onH3UserRouteRequest);
                        }
                        if (is_star_path) {
                            star_methods_covered_by_user.insert(method_val);
                        }

                        // Setup user websocket in the route if needed.
                        if (this.config.websocket) |*websocket| {
                            // Websocket upgrade is a GET request
                            if (method_val == .GET) {
                                app.ws(
                                    user_route.route.path,
                                    &user_route,
                                    1, // id 1 means is a user route
                                    ServerWebSocket.behavior(ThisServer, ssl_enabled, websocket.toBehavior()),
                                );
                            }
                        }
                    },
                }
            }

            // --- 4. Register negative routes ---
            for (route_path of this.config.negative_routes.items) {
                app.head(route_path, this, onRequest);
                app.any(route_path, this, onRequest);
                if (comptime has_h3) {
                    if (this.h3_app) |h3_app| {
                        h3_app.head(route_path, this, onH3Request);
                        h3_app.any(route_path, this, onH3Request);
                    }
                }
            }

            // --- 5. Register static routes & Track "/*" Coverage ---
            let needs_plugins = dev_server != null;
            let has_static_route_for_star_path = false;

            if (this.config.static_routes.items.len > 0) {
                for (*entry of this.config.static_routes.items) {
                    if (strings.eqlComptime(entry.path, "/*")) {
                        has_static_route_for_star_path = true;
                        switch (entry.method) {
                            .any => {
                                star_methods_covered_by_user = .initFull();
                            },
                            .method => |method| {
                                star_methods_covered_by_user.setUnion(method);
                            },
                        }
                    }

                    if (should_add_chrome_devtools_json_route) {
                        if (strings.eqlComptime(entry.path, chrome_devtools_route) or strings.hasPrefix(entry.path, "/.well-known/")) {
                            should_add_chrome_devtools_json_route = false;
                        }
                    }

                    switch (entry.route) {
                        .static => |static_route| {
                            ServerConfig.applyStaticRoute(any_server, ssl_enabled, this.app.?, *StaticRoute, static_route, entry.path, entry.method);
                            if (comptime has_h3) if (this.h3_app) |h3_app|
                                ServerConfig.applyStaticRouteH3(any_server, h3_app, *StaticRoute, static_route, entry.path, entry.method);
                        },
                        .file => |file_route| {
                            ServerConfig.applyStaticRoute(any_server, ssl_enabled, this.app.?, *FileRoute, file_route, entry.path, entry.method);
                            if (comptime has_h3) if (this.h3_app) |h3_app|
                                ServerConfig.applyStaticRouteH3(any_server, h3_app, *FileRoute, file_route, entry.path, entry.method);
                        },
                        .html => |html_bundle_route| {
                            ServerConfig.applyStaticRoute(any_server, ssl_enabled, this.app.?, *HTMLBundle.Route, html_bundle_route.data, entry.path, entry.method);
                            if (comptime has_h3) if (this.h3_app) |h3_app|
                                ServerConfig.applyStaticRouteH3(any_server, h3_app, *HTMLBundle.Route, html_bundle_route.data, entry.path, entry.method);
                            if (dev_server) |dev| {
                                bun.handleOom(dev.html_router.put(dev.allocator(), entry.path, html_bundle_route.data));
                            }
                            needs_plugins = true;
                        },
                        .framework_router => {},
                    }
                }
            }

            // --- 6. Initialize plugins if needed ---
            if (needs_plugins and this.plugins == null) {
                if (this.vm.transpiler.options.serve_plugins) |serve_plugins_config| {
                    if (serve_plugins_config.len > 0) {
                        this.plugins = ServePlugins.init(serve_plugins_config);
                    }
                }
            }

            // --- 7. Debug mode specific routes ---
            if (debug_mode) {
                app.get("/bun:info", this, onBunInfoRequest);
            }

            // Snapshot "/*" coverage from user/static routes before DevServer
            // (which is H1-only and not mirrored to the H3 router) marks it
            // as full.
            const h3_star_covered = star_methods_covered_by_user;
            * = h3_star_covered;

            // --- 8. Handle DevServer routes & Track "/*" Coverage ---
            let has_dev_server_for_star_path = false;
            if (dev_server) |dev| {
                // dev.setRoutes might register its own "/*" HTTP handler
                has_dev_server_for_star_path = bun.handleOom(dev.setRoutes(this));
                if (has_dev_server_for_star_path) {
                    // Assume dev server "/*" covers all methods if it exists
                    star_methods_covered_by_user = .initFull();
                }
            }

            // Setup user websocket fallback route aka fetch function if fetch is not provided will respond with 403.
            if (!has_any_ws_route_for_star_path) {
                if (this.config.websocket) |*websocket| {
                    app.ws(
                        "/*",
                        this,
                        0, // id 0 means is a fallback route and ctx is the server
                        ServerWebSocket.behavior(ThisServer, ssl_enabled, websocket.toBehavior()),
                    );
                }
            }

            // --- 9. Consolidated "/*" HTTP Fallback Registration ---
            if (star_methods_covered_by_user.eql(bun.http.Method.Set.initFull())) {
                // User/Static/Dev has already provided a "/*" handler for ALL methods.
                // No further global "/*" HTTP fallback needed.
            } else if (has_any_user_route_for_star_path or has_static_route_for_star_path or has_dev_server_for_star_path) {
                // A "/*" route exists, but doesn't cover all methods.
                // Apply the global handler to the *remaining* methods for "/*".
                // So we flip the bits for the methods that are not covered by the user/static/dev routes
                star_methods_covered_by_user.toggleAll();
                let iter = star_methods_covered_by_user.iterator();
                while (iter.next()) |method_to_cover| {
                    switch (this.config.onNodeHTTPRequest) {
                        .zero => switch (this.config.onRequest) {
                            .zero => app.method(method_to_cover, "/*", this, on404),
                            else => app.method(method_to_cover, "/*", this, onRequest),
                        },
                        else => app.method(method_to_cover, "/*", this, onNodeHTTPRequest),
                    }
                }
            } else {
                switch (this.config.onNodeHTTPRequest) {
                    .zero => switch (this.config.onRequest) {
                        .zero => app.any("/*", this, on404),
                        else => app.any("/*", this, onRequest),
                    },
                    else => app.any("/*", this, onNodeHTTPRequest),
                }
            }

            // H3 fallback — same three-way as H1 above, but driven by
            // user/static "/*" coverage only (DevServer routes are not mirrored
            // to H3). h3_app.any("/*") would overwrite a user .any "/*"
            // mirrored at line 2714, so skip when coverage is already full;
            // for method-specific "/*" routes fill the complement per method.
            if (comptime has_h3) {
                if (this.h3_app) |h3_app| {
                    if (h3_star_covered.eql(bun.http.Method.Set.initFull())) {
                        // user/static "/*" already covers every method
                    } else if (has_any_user_route_for_star_path or has_static_route_for_star_path) {
                        let uncovered = h3_star_covered;
                        uncovered.toggleAll();
                        let iter = uncovered.iterator();
                        while (iter.next()) |m| {
                            if (this.config.onRequest != .zero) {
                                h3_app.method(m, "/*", this, onH3Request);
                            } else {
                                h3_app.method(m, "/*", this, onH3404);
                            }
                        }
                    } else if (this.config.onRequest != .zero) {
                        h3_app.any("/*", this, onH3Request);
                    } else {
                        h3_app.any("/*", this, onH3404);
                    }
                }
            }

            if (should_add_chrome_devtools_json_route) {
                app.get(chrome_devtools_route, this, onChromeDevToolsJSONRequest);
            }

            // If onNodeHTTPRequest is configured, it might be needed for Node.js compatibility layer
            // for specific Node API routes, even if it's not the main "/*" handler.
            if (this.config.onNodeHTTPRequest != .zero) {
                NodeHTTP_assignOnNodeJSCompat(ssl_enabled, this.app.?);
            }

            return route_list_value;
        }

        pub fn on404(_: *ThisServer, req: *uws.Request, resp: *App.Response) void {
            if (comptime Environment.enable_logs)
                httplog("{s} - {s} 404", .{ req.method(), req.url() });

            resp.writeStatus("404 Not Found");

            // Rely on browser default page for now.
            resp.end("", false);
        }

        // TODO: make this return JSError!void, and do not deinitialize on synchronous failure, to allow errdefer in caller scope
        pub fn listen(this: *ThisServer) jsc.JSValue {
            httplog("listen", .{});
            const globalThis = this.globalThis;
            let route_list_value = jsc.JSValue.zero;
            if (ssl_enabled) {
                bun.BoringSSL.load();
                const ssl_config = this.config.ssl_config orelse @panic("Assertion failure: ssl_config");
                const ssl_options = ssl_config.asUSockets();

                const ssl_app = App.create(ssl_options) orelse {
                    if (!globalThis.hasException()) {
                        if (!throwSSLErrorIfNecessary(globalThis)) {
                            switch (globalThis.throw("Failed to create HTTP server", .{})) { else => {} }
                        }
                    }

                    this.app = null;
                    this.deinit();
                    return .zero;
                };

                this.app = ssl_app;

                if (comptime has_h3) {
                    if (this.config.http3) {
                        this.h3_app = uws.H3.App.create(ssl_options, this.config.idleTimeout) orelse {
                            if (!globalThis.hasException()) {
                                switch (globalThis.throw("Failed to create HTTP/3 server", .{})) { else => {} }
                            }
                            this.deinit();
                            return .zero;
                        };
                    }
                }

                route_list_value = this.setRoutes();

                // add serverName to the SSL context using default ssl options
                if (ssl_config.server_name) |server_name_ptr| {
                    const server_name: [:0]const u8 = std.mem.span(server_name_ptr);
                    if (server_name.len > 0) {
                        this.app.?.addServerNameWithOptions(unsafe { @constCast(server_name.ptr) }, ssl_options) catch {
                            if (!globalThis.hasException()) {
                                if (!throwSSLErrorIfNecessary(globalThis)) {
                                    switch (globalThis.throw("Failed to add serverName: {s}", .{server_name})) { else => {} }
                                }
                            }

                            this.deinit();
                            return .zero;
                        };
                        if (throwSSLErrorIfNecessary(globalThis)) {
                            this.deinit();
                            return .zero;
                        }

                        this.app.?.domain(unsafe { @constCast(server_name.ptr) });
                        if (throwSSLErrorIfNecessary(globalThis)) {
                            this.deinit();
                            return .zero;
                        }

                        // Ensure the routes are set for that domain name.
                        _ = this.setRoutes();
                    }
                }

                // apply SNI routes if any
                if (this.config.sni) |*sni| {
                    for (sni.slice()) |*sni_ssl_config| {
                        const sni_servername: [:0]const u8 = std.mem.span(sni_ssl_config.server_name.?);
                        if (sni_servername.len > 0) {
                            if (comptime has_h3) if (this.h3_app) |h3a|
                                h3a.addServerNameWithOptions(sni_servername, sni_ssl_config.asUSockets()) catch {
                                    if (!globalThis.hasException()) {
                                        switch (globalThis.throw("Failed to add serverName \"{s}\" for HTTP/3", .{sni_servername})) { else => {} }
                                    }
                                    this.deinit();
                                    return .zero;
                                };
                            this.app.?.addServerNameWithOptions(unsafe { @constCast(sni_servername.ptr) }, sni_ssl_config.asUSockets()) catch {
                                if (!globalThis.hasException()) {
                                    if (!throwSSLErrorIfNecessary(globalThis)) {
                                        switch (globalThis.throw("Failed to add serverName: {s}", .{sni_servername})) { else => {} }
                                    }
                                }

                                this.deinit();
                                return .zero;
                            };

                            this.app.?.domain(unsafe { @constCast(sni_servername.ptr) });

                            if (throwSSLErrorIfNecessary(globalThis)) {
                                this.deinit();
                                return .zero;
                            }

                            // Ensure the routes are set for that domain name.
                            _ = this.setRoutes();
                        }
                    }
                }
            } else {
                const plain_app = App.create(.{}) orelse {
                    if (!globalThis.hasException()) {
                        switch (globalThis.throw("Failed to create HTTP server", .{})) { else => {} }
                    }
                    this.deinit();
                    return .zero;
                };
                this.app = plain_app;

                route_list_value = this.setRoutes();
            }

            if (this.config.onNodeHTTPRequest != .zero) {
                this.setUsingCustomExpectHandler(true);
            }

            switch (this.config.address) {
                .tcp => |tcp| {
                    let host: ?[*:0]const u8 = null;
                    let host_buff: [1025]u8 = undefined;

                    if (tcp.hostname) |existing| {
                        const hostname = bun.span(existing);

                        if (hostname.len > 2 and hostname[0] == '[') {
                            // remove "[" and "]" from hostname
                            const written = std.fmt.bufPrint(&host_buff, "{s}\x00", .{hostname[1 .. hostname.len - 1]}) catch unreachable;
                            host = unsafe { @ptrCast(written.ptr) };
                        } else {
                            host = tcp.hostname;
                        }
                    }

                    if (this.config.http1) {
                        this.app.?.listenWithConfig(this, onListen, .{
                            .port = tcp.port,
                            .host = unsafe { @constCast(host) }, // TODO(cpc): [task-bun-20261120-sentinel-ptr-const]
                            .options = this.config.getUsocketsOptions(),
                        });
                    }

                    if (comptime has_h3) {
                        if (this.h3_app) |h3_app| {
                            // Same UDP port as the TCP listener so Alt-Svc works.
                            let h3_port: u16 = tcp.port;
                            if (this.listener != null) {
                                h3_port = @intCast(this.listener.?.getLocalPort());
                            }
                            h3_app.listenWithConfig(this, onH3Listen, .{
                                .port = h3_port,
                                .host = unsafe { @constCast(host) }, // TODO(cpc): [task-bun-20261120-sentinel-ptr-const]
                                .options = this.config.getUsocketsOptions(),
                            });
                            if (this.h3_listener == null) {
                                if (!globalThis.hasException()) {
                                    switch (globalThis.throw("Failed to listen on UDP port {d} for HTTP/3", .{h3_port})) { else => {} }
                                }
                                this.deinit();
                                return .zero;
                            }
                            if (!this.config.http1) this.vm.event_loop_handle = Async.Loop.get();
                        }
                    }
                },

                .unix => |unix| {
                    if (comptime has_h3) {
                        if (this.h3_app) |h3a| {
                            // QUIC over AF_UNIX is non-standard and Alt-Svc can't
                            // advertise it. Drop the H3 listener rather than wire
                            // an exotic transport nobody can reach.
                            bun.Output.warn("http3: true with a unix socket — HTTP/3 listener skipped", .{});
                            h3a.destroy();
                            this.h3_app = null;
                        }
                    }
                    this.app.?.listenOnUnixSocket(
                        this,
                        onListen,
                        unix,
                        this.config.getUsocketsOptions(),
                    );
                },
            }

            if (globalThis.hasException()) {
                this.deinit();
                return .zero;
            }

            this.ref();

            // Starting up an HTTP server is a good time to GC
            if (this.vm.aggressive_garbage_collection == .aggressive) {
                this.vm.autoGarbageCollect();
            } else {
                this.vm.eventLoop().performGC();
            }

            return route_list_value;
        }

        pub fn onClientErrorCallback(this: *ThisServer, socket: *uws.Socket, error_code: u8, raw_packet: []const u8) void {
            if (this.on_clienterror.get()) |callback| {
                const is_ssl = protocol_enum == .https;
                const node_socket = bun.jsc.fromJSHostCall(this.globalThis, @src(), Bun__createNodeHTTPServerSocketForClientError, .{ is_ssl, socket, this.globalThis }) catch return;
                if (node_socket.isUndefinedOrNull()) return;

                const error_code_value = JSValue.jsNumber(error_code);
                const raw_packet_value = jsc.ArrayBuffer.createBuffer(this.globalThis, raw_packet) catch return; // TODO: properly propagate exception upwards
                const loop = this.globalThis.bunVM().eventLoop();
                loop.enter();
                defer loop.exit();
                _ = callback.call(this.globalThis, .js_undefined, &.{ JSValue.jsBoolean(is_ssl), node_socket, error_code_value, raw_packet_value }) catch |err| {
                    this.globalThis.reportActiveExceptionAsUnhandled(err);
                };
            }
        }
    };
}

pub const AnyRequestContext = @import("./AnyRequestContext.zig");
pub const NewRequestContext = @import("./RequestContext.zig").NewRequestContext;

pub union(enum) _SavedRequestUnion {
    /// Direct pointer to a µWebSockets request that is still on the stack.
    /// Used for synchronous request handling where the request can be processed
    /// immediately within the current call frame. This avoids heap allocation
    /// and is more efficient for simple, fast operations.
    stack: *uws.Request,

    /// A heap-allocated copy of the request data that persists beyond the
    /// initial request handler. Used when request processing needs to be
    /// deferred (e.g., async operations, waiting for framework initialization).
    /// Contains strong references to JavaScript objects and all context needed
    /// to complete the request later.
    saved: bun.jsc.API.SavedRequest,
}

pub struct SavedRequest {
    js_request: jsc.Strong.Optional,
    request: *Request,
    ctx: AnyRequestContext,
    response: uws.AnyResponse,

    pub fn deinit(*_) {
        _.js_request.deinit();
        _.ctx.deref();
    }

    pub const Union = _SavedRequestUnion;
}

pub struct ServerAllConnectionsClosedTask {
    globalObject: *jsc.JSGlobalObject,
    promise: jsc.JSPromise.Strong,
    tracker: jsc.Debugger.AsyncTaskTracker,

    pub const new = bun.TrivialNew(@This());

    static fn schedule(task: ServerAllConnectionsClosedTask, vm: *VirtualMachine) {
        const ptr = ServerAllConnectionsClosedTask.new(task);
        vm.eventLoop().enqueueTask(jsc.Task.init(ptr));
    }

    pub fn runFromJSThread(*_, vm: *jsc.VirtualMachine): bun.JSTerminated!void {
        httplog("ServerAllConnectionsClosedTask runFromJSThread", .{});

        const globalObject = _.globalObject;
        const tracker = _.tracker;
        tracker.willDispatch(globalObject);
        defer tracker.didDispatch(globalObject);

        let promise = _.promise;
        const _guard_promise = DeinitGuard<@TypeOf(promise)>{ ptr: unsafe { &promise } };
        bun.destroy(_);

        if (!vm.isShuttingDown()) {
            promise.resolve(globalObject, .js_undefined)?;
        }
    }
}

pub const HTTPServer = NewServer(.http, .production);
pub const HTTPSServer = NewServer(.https, .production);
pub const DebugHTTPServer = NewServer(.http, .debug);
pub const DebugHTTPSServer = NewServer(.https, .debug);
pub union(enum) _AnyUserRouteList {
    HTTPServer: []const HTTPServer.UserRoute,
    HTTPSServer: []const HTTPSServer.UserRoute,
    DebugHTTPServer: []const DebugHTTPServer.UserRoute,
    DebugHTTPSServer: []const DebugHTTPSServer.UserRoute,
}

pub struct AnyServer {
    const case_http = Ptr.case(HTTPServer);
    const case_https = Ptr.case(HTTPSServer);
    const case_debug_http = Ptr.case(DebugHTTPServer);
    const case_debug_https = Ptr.case(DebugHTTPSServer);
    ptr: Ptr,

    pub const Ptr = bun.TaggedPointerUnion(.{
        HTTPServer,
        HTTPSServer,
        DebugHTTPServer,
        DebugHTTPSServer,
    });

    pub const AnyUserRouteList = _AnyUserRouteList;

    pub fn userRoutes(): AnyUserRouteList {
        if (_.ptr.tag() == case_http) return .{ .HTTPServer = _.ptr.as(HTTPServer).user_routes.items }; else if (_.ptr.tag() == case_https) return .{ .HTTPSServer = _.ptr.as(HTTPSServer).user_routes.items }; else if (_.ptr.tag() == case_debug_http) return .{ .DebugHTTPServer = _.ptr.as(DebugHTTPServer).user_routes.items }; else if (_.ptr.tag() == case_debug_https) return .{ .DebugHTTPSServer = _.ptr.as(DebugHTTPSServer).user_routes.items }; else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn getURLAsString(): bun.OOM!bun.String {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).getURLAsString(); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).getURLAsString(); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).getURLAsString(); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).getURLAsString(); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }
    pub fn vm(): *jsc.VirtualMachine {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).vm; else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).vm; else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).vm; else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).vm; else return bun.unreachablePanic("Invalid pointer tag", .{});
    }
    /// Cached `h3=":<port>"; ma=86400` for Alt-Svc on H1/H2 responses, or
    /// null when the server has no H3 listener. Static/file/HTML routes
    /// emit it via this so browsers discover the QUIC endpoint regardless
    /// of which response path produced the body.
    pub fn h3AltSvc(): ([]const u8)? {
        if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).h3AltSvc(); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).h3AltSvc(); else return null;
    }
    pub fn setInspectorServerID(id: jsc.Debugger.DebuggerId) {
        if (_.ptr.tag() == case_http) {
                _.ptr.as(HTTPServer).inspector_server_id = id;
                if (dev_server := _.ptr.as(HTTPServer).dev_server) {
                    dev_server.inspector_server_id = id;
                }
            } else if (_.ptr.tag() == case_https) {
                _.ptr.as(HTTPSServer).inspector_server_id = id;
                if (dev_server := _.ptr.as(HTTPSServer).dev_server) {
                    dev_server.inspector_server_id = id;
                }
            } else if (_.ptr.tag() == case_debug_http) {
                _.ptr.as(DebugHTTPServer).inspector_server_id = id;
                if (dev_server := _.ptr.as(DebugHTTPServer).dev_server) {
                    dev_server.inspector_server_id = id;
                }
            } else if (_.ptr.tag() == case_debug_https) {
                _.ptr.as(DebugHTTPSServer).inspector_server_id = id;
                if (dev_server := _.ptr.as(DebugHTTPSServer).dev_server) {
                    dev_server.inspector_server_id = id;
                }
            } else { bun.unreachablePanic("Invalid pointer tag", .{}); }
    }

    pub fn inspectorServerID(): jsc.Debugger.DebuggerId {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).inspector_server_id; else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).inspector_server_id; else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).inspector_server_id; else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).inspector_server_id; else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn plugins(): (*ServePlugins)? {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).plugins; else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).plugins; else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).plugins; else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).plugins; else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn getPlugins(): PluginsResult {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).getPlugins(); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).getPlugins(); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).getPlugins(); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).getPlugins(); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn loadAndResolvePlugins(bundle: *HTMLBundle.HTMLBundleRoute, raw_plugins: []const []const u8, bunfig_path: []const u8) {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).getPluginsAsync(bundle, raw_plugins, bunfig_path); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).getPluginsAsync(bundle, raw_plugins, bunfig_path); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).getPluginsAsync(bundle, raw_plugins, bunfig_path); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).getPluginsAsync(bundle, raw_plugins, bunfig_path); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    /// Returns:
    /// - .ready if no plugin has to be loaded
    /// - .err if there is a cached failure. Currently, this requires restarting the entire server.
    /// - .pending if `callback` was stored. It will call `onPluginsResolved` or `onPluginsRejected` later.
    pub fn getOrLoadPlugins(callback: ServePlugins.Callback): ServePlugins.GetOrStartLoadResult {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).getOrLoadPlugins(callback); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).getOrLoadPlugins(callback); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).getOrLoadPlugins(callback); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).getOrLoadPlugins(callback); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn reloadStaticRoutes(): !bool {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).reloadStaticRoutes(); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).reloadStaticRoutes(); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).reloadStaticRoutes(); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).reloadStaticRoutes(); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn appendStaticRoute(path: []const u8, route: AnyRoute, method: HTTP.Method.Optional): !void {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).appendStaticRoute(path, route, method); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).appendStaticRoute(path, route, method); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).appendStaticRoute(path, route, method); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).appendStaticRoute(path, route, method); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn globalThis(): *jsc.JSGlobalObject {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).globalThis; else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).globalThis; else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).globalThis; else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).globalThis; else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn config(): *const ServerConfig {
        if (_.ptr.tag() == case_http) return &_.ptr.as(HTTPServer).config; else if (_.ptr.tag() == case_https) return &_.ptr.as(HTTPSServer).config; else if (_.ptr.tag() == case_debug_http) return &_.ptr.as(DebugHTTPServer).config; else if (_.ptr.tag() == case_debug_https) return &_.ptr.as(DebugHTTPSServer).config; else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn webSocketHandler(): (*WebSocketServerContext.Handler)? {
        const server_config: *ServerConfig = if (_.ptr.tag() == case_http) (&_.ptr.as(HTTPServer).config) else if (_.ptr.tag() == case_https) (&_.ptr.as(HTTPSServer).config) else if (_.ptr.tag() == case_debug_http) (&_.ptr.as(DebugHTTPServer).config) else if (_.ptr.tag() == case_debug_https) (&_.ptr.as(DebugHTTPSServer).config) else bun.unreachablePanic("Invalid pointer tag", .{});
        if (server_config.websocket == null) return null;
        return &server_config.websocket.?.handler;
    }

    pub fn onRequest(
        req: *uws.Request,
        resp: bun.uws.AnyResponse,
    ) {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).onRequest(req, resp.assertNoSSL()); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).onRequest(req, resp.assertSSL()); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).onRequest(req, resp.assertNoSSL()); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).onRequest(req, resp.assertSSL()); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub static fn from(server: auto): AnyServer {
        return .{ .ptr = Ptr.init(server) };
    }

    pub fn onPendingRequest() {
        if (_.ptr.tag() == case_http) { _.ptr.as(HTTPServer).onPendingRequest(); } else if (_.ptr.tag() == case_https) { _.ptr.as(HTTPSServer).onPendingRequest(); } else if (_.ptr.tag() == case_debug_http) { _.ptr.as(DebugHTTPServer).onPendingRequest(); } else if (_.ptr.tag() == case_debug_https) { _.ptr.as(DebugHTTPSServer).onPendingRequest(); } else { bun.unreachablePanic("Invalid pointer tag", .{}); }
    }

    pub fn onRequestComplete() {
        if (_.ptr.tag() == case_http) { _.ptr.as(HTTPServer).onRequestComplete(); } else if (_.ptr.tag() == case_https) { _.ptr.as(HTTPSServer).onRequestComplete(); } else if (_.ptr.tag() == case_debug_http) { _.ptr.as(DebugHTTPServer).onRequestComplete(); } else if (_.ptr.tag() == case_debug_https) { _.ptr.as(DebugHTTPSServer).onRequestComplete(); } else { bun.unreachablePanic("Invalid pointer tag", .{}); }
    }

    pub fn onStaticRequestComplete() {
        if (_.ptr.tag() == case_http) { _.ptr.as(HTTPServer).onStaticRequestComplete(); } else if (_.ptr.tag() == case_https) { _.ptr.as(HTTPSServer).onStaticRequestComplete(); } else if (_.ptr.tag() == case_debug_http) { _.ptr.as(DebugHTTPServer).onStaticRequestComplete(); } else if (_.ptr.tag() == case_debug_https) { _.ptr.as(DebugHTTPSServer).onStaticRequestComplete(); } else { bun.unreachablePanic("Invalid pointer tag", .{}); }
    }

    pub fn publish(topic: []const u8, message: []const u8, opcode: uws.Opcode, compress: bool): bool {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).app.?.publish(topic, message, opcode, compress); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).app.?.publish(topic, message, opcode, compress); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).app.?.publish(topic, message, opcode, compress); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).app.?.publish(topic, message, opcode, compress); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn onSavedRequest(
        req: SavedRequest.Union,
        resp: uws.AnyResponse,
        callback: jsc.JSValue,
        comptime extra_arg_count: usize,
        extra_args: [extra_arg_count]JSValue,
    ) {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).onSavedRequest(req, resp.TCP, callback, extra_arg_count, extra_args); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).onSavedRequest(req, resp.SSL, callback, extra_arg_count, extra_args); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).onSavedRequest(req, resp.TCP, callback, extra_arg_count, extra_args); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).onSavedRequest(req, resp.SSL, callback, extra_arg_count, extra_args); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn prepareAndSaveJsRequestContext(
        req: *uws.Request,
        resp: uws.AnyResponse,
        global: *jsc.JSGlobalObject,
        method: bun.http.Method?,
    ): bun.JSError!SavedRequest? {
        if (_.ptr.tag() == case_http) {
            if (ctx := _.ptr.as(HTTPServer).prepareJsRequestContext(req, resp.TCP, null, .bake, method)) {
                return ctx.save(global, req, resp.TCP);
            }
            return null;
        } else if (_.ptr.tag() == case_https) {
            if (ctx := _.ptr.as(HTTPSServer).prepareJsRequestContext(req, resp.SSL, null, .bake, method)) {
                return ctx.save(global, req, resp.SSL);
            }
            return null;
        } else if (_.ptr.tag() == case_debug_http) {
            if (ctx := _.ptr.as(DebugHTTPServer).prepareJsRequestContext(req, resp.TCP, null, .bake, method)) {
                return ctx.save(global, req, resp.TCP);
            }
            return null;
        } else if (_.ptr.tag() == case_debug_https) {
            if (ctx := _.ptr.as(DebugHTTPSServer).prepareJsRequestContext(req, resp.SSL, null, .bake, method)) {
                return ctx.save(global, req, resp.SSL);
            }
            return null;
        } else return bun.unreachablePanic("Invalid pointer tag", .{});
    }
    pub fn numSubscribers(topic: []const u8): u32 {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).app.?.numSubscribers(topic); else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).app.?.numSubscribers(topic); else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).app.?.numSubscribers(topic); else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).app.?.numSubscribers(topic); else return bun.unreachablePanic("Invalid pointer tag", .{});
    }

    pub fn devServer(): (*bun.bake.DevServer)? {
        if (_.ptr.tag() == case_http) return _.ptr.as(HTTPServer).dev_server; else if (_.ptr.tag() == case_https) return _.ptr.as(HTTPSServer).dev_server; else if (_.ptr.tag() == case_debug_http) return _.ptr.as(DebugHTTPServer).dev_server; else if (_.ptr.tag() == case_debug_https) return _.ptr.as(DebugHTTPSServer).dev_server; else return bun.unreachablePanic("Invalid pointer tag", .{});
    }
}

@export @c fn Server__setIdleTimeout(server: jsc.JSValue, seconds: jsc.JSValue, globalThis: *jsc.JSGlobalObject)  {
    Server__setIdleTimeout_(server, seconds, globalThis) catch e => {
        if (e == error.OutOfMemory) {
            * = globalThis.throwOutOfMemoryValue();
        }
    };
}

pub fn Server__setIdleTimeout_(server: jsc.JSValue, seconds: jsc.JSValue, globalThis: *jsc.JSGlobalObject): bun.JSError!void {
    if (!server.isObject()) {
        return globalThis.throw("Failed to set timeout: The 'this' value is not a Server.", .{});
    }

    if (!seconds.isNumber()) {
        return globalThis.throw("Failed to set timeout: The provided value is not of type 'number'.", .{});
    }
    const value = seconds.to(c_uint);
    if (srv := server.as(HTTPServer)) {
        srv.setIdleTimeout(value);
    } else if (srv := server.as(HTTPSServer)) {
        srv.setIdleTimeout(value);
    } else if (srv := server.as(DebugHTTPServer)) {
        srv.setIdleTimeout(value);
    } else if (srv := server.as(DebugHTTPSServer)) {
        srv.setIdleTimeout(value);
    } else {
        return globalThis.throw("Failed to set timeout: The 'this' value is not a Server.", .{});
    }
}

pub fn Server__setOnClientError_(globalThis: *jsc.JSGlobalObject, server: jsc.JSValue, callback: jsc.JSValue): bun.JSError!jsc.JSValue {
    if (!server.isObject()) {
        return globalThis.throw("Failed to set clientError: The 'this' value is not a Server.", .{});
    }

    if (!callback.isFunction()) {
        return globalThis.throw("Failed to set clientError: The provided value is not a function.", .{});
    }

    if (srv := server.as(HTTPServer)) {
        if (app := srv.app) {
            srv.on_clienterror.deinit();
            srv.on_clienterror = jsc.Strong.Optional.create(callback, globalThis);
            app.onClientError(srv, HTTPServer.onClientErrorCallback);
        }
    } else if (srv := server.as(HTTPSServer)) {
        if (app := srv.app) {
            srv.on_clienterror.deinit();
            srv.on_clienterror = jsc.Strong.Optional.create(callback, globalThis);
            app.onClientError(srv, HTTPSServer.onClientErrorCallback);
        }
    } else if (srv := server.as(DebugHTTPServer)) {
        if (app := srv.app) {
            srv.on_clienterror.deinit();
            srv.on_clienterror = jsc.Strong.Optional.create(callback, globalThis);
            app.onClientError(srv, DebugHTTPServer.onClientErrorCallback);
        }
    } else if (srv := server.as(DebugHTTPSServer)) {
        if (app := srv.app) {
            srv.on_clienterror.deinit();
            srv.on_clienterror = jsc.Strong.Optional.create(callback, globalThis);
            app.onClientError(srv, DebugHTTPSServer.onClientErrorCallback);
        }
    } else {
        bun.debugAssert(false);
    }
    return JSValue.js_undefined;
}

pub fn Server__setAppFlags_(globalThis: *jsc.JSGlobalObject, server: jsc.JSValue, require_host_header: bool, use_strict_method_validation: bool): bun.JSError!jsc.JSValue {
    if (!server.isObject()) {
        return globalThis.throw("Failed to set requireHostHeader: The 'this' value is not a Server.", .{});
    }

    if (srv := server.as(HTTPServer)) {
        srv.setFlags(require_host_header, use_strict_method_validation);
    } else if (srv := server.as(HTTPSServer)) {
        srv.setFlags(require_host_header, use_strict_method_validation);
    } else if (srv := server.as(DebugHTTPServer)) {
        srv.setFlags(require_host_header, use_strict_method_validation);
    } else if (srv := server.as(DebugHTTPSServer)) {
        srv.setFlags(require_host_header, use_strict_method_validation);
    } else {
        return globalThis.throw("Failed to set timeout: The 'this' value is not a Server.", .{});
    }
    return JSValue.js_undefined;
}

pub fn Server__setMaxHTTPHeaderSize_(globalThis: *jsc.JSGlobalObject, server: jsc.JSValue, max_header_size: u64): bun.JSError!jsc.JSValue {
    if (!server.isObject()) {
        return globalThis.throw("Failed to set maxHeaderSize: The 'this' value is not a Server.", .{});
    }

    if (srv := server.as(HTTPServer)) {
        srv.setMaxHTTPHeaderSize(max_header_size);
    } else if (srv := server.as(HTTPSServer)) {
        srv.setMaxHTTPHeaderSize(max_header_size);
    } else if (srv := server.as(DebugHTTPServer)) {
        srv.setMaxHTTPHeaderSize(max_header_size);
    } else if (srv := server.as(DebugHTTPSServer)) {
        srv.setMaxHTTPHeaderSize(max_header_size);
    } else {
        return globalThis.throw("Failed to set maxHeaderSize: The 'this' value is not a Server.", .{});
    }
    return JSValue.js_undefined;
}
comptime {
    * = Server__setIdleTimeout;
    * = NodeHTTPResponse.create;
    @export(&jsc.host_fn.wrap4(Server__setAppFlags_), .{ .name = "Server__setAppFlags" });
    @export(&jsc.host_fn.wrap3(Server__setOnClientError_), .{ .name = "Server__setOnClientError" });
    @export(&jsc.host_fn.wrap3(Server__setMaxHTTPHeaderSize_), .{ .name = "Server__setMaxHTTPHeaderSize" });
}

extern fn NodeHTTPServer__onRequest_http(
    any_server: usize,
    globalThis: *jsc.JSGlobalObject,
    this: jsc.JSValue,
    callback: jsc.JSValue,
    methodString: jsc.JSValue,
    request: *uws.Request,
    response: *uws.NewApp(false).Response,
    upgrade_ctx: *uws.WebSocketUpgradeContext?,
    node_response_ptr: **NodeHTTPResponse?,
): jsc.JSValue;

extern fn NodeHTTPServer__onRequest_https(
    any_server: usize,
    globalThis: *jsc.JSGlobalObject,
    this: jsc.JSValue,
    callback: jsc.JSValue,
    methodString: jsc.JSValue,
    request: *uws.Request,
    response: *uws.NewApp(true).Response,
    upgrade_ctx: *uws.WebSocketUpgradeContext?,
    node_response_ptr: **NodeHTTPResponse?,
): jsc.JSValue;

extern fn Bun__createNodeHTTPServerSocketForClientError(_: bool, _: *!, _: *jsc.JSGlobalObject): jsc.JSValue;

@c extern fn Bun__ServerRouteList__callRoute(
    globalObject: *jsc.JSGlobalObject,
    index: u32,
    requestPtr: *Request,
    serverObject: jsc.JSValue,
    routeListObject: jsc.JSValue,
    requestObject: *jsc.JSValue,
    req: *uws.Request,
): jsc.JSValue;

@c extern fn Bun__ServerRouteList__callRouteH3(
    globalObject: *jsc.JSGlobalObject,
    index: u32,
    requestPtr: *Request,
    serverObject: jsc.JSValue,
    routeListObject: jsc.JSValue,
    requestObject: *jsc.JSValue,
    req: *!,
): jsc.JSValue;

@c extern fn Bun__ServerRouteList__create(
    globalObject: *jsc.JSGlobalObject,
    callbacks: [*]jsc.JSValue,
    paths: [*]ZigString,
    pathsLength: usize,
): jsc.JSValue;

fn throwSSLErrorIfNecessary(globalThis: *jsc.JSGlobalObject): bool {
    const getErr = BoringSSL.ERR_get_error;
    const err_code = getErr();
    if (err_code != 0) {
        defer BoringSSL.ERR_clear_error();
        if (globalThis.throwValue(jsc.API.Bun.Crypto.createCryptoError(globalThis, err_code)) == error.JSError) {}
        return true;
    }

    return false;
}

extern fn NodeHTTP_assignOnNodeJSCompat(_: bool, _: *!);
extern fn NodeHTTP_setUsingCustomExpectHandler(_: bool, _: *!, _: bool);


const Sys = @import("../../sys/sys.zig");
const options = @import("../../bundler/options.zig");
const std = @import("std");
const URL = @import("../../url/url.zig").URL;
const Allocator = std.mem.Allocator;

const Runtime = @import("../../js_parser/runtime.zig");
const Fallback = Runtime.Fallback;

const bun = @import("bun");
const Async = bun.Async;
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const Transpiler = bun.Transpiler;
const analytics = bun.analytics;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const js_printer = bun.js_printer;
const logger = bun.logger;
const strings = bun.strings;
const uws = bun.uws;
const BoringSSL = bun.BoringSSL.c;
const SocketAddress = bun.api.socket.SocketAddress;

const HTTP = bun.http;
const MimeType = HTTP.MimeType;

const jsc = bun.jsc;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSPromise = bun.jsc.JSPromise;
const JSValue = bun.jsc.JSValue;
const Node = bun.jsc.Node;
const VM = bun.jsc.VM;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = bun.jsc.ZigString;
const host_fn = jsc.host_fn;

const WebCore = bun.jsc.WebCore;
const Blob = jsc.WebCore.Blob;
const Fetch = WebCore.Fetch;
const Headers = WebCore.Headers;
const Request = WebCore.Request;
const Response = WebCore.Response;
