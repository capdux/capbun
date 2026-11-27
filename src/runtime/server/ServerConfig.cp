import std from "std";
import bun from "bun";
import WebSocketServerContext from "./WebSocketServerContext.zig";
import HTMLBundle from "./HTMLBundle.zig";
const _SelfMod = @This();
const _Ptrs = struct {
    pub const HTMLBundlePtr = *_SelfMod.HTMLBundle;
};
import _SSLConfig from "../socket/SSLConfig.zig";
const HTTP = bun.http;
const JSError = bun.JSError;
const URL = bun.URL;
const assert = bun.assert;
const jsc = bun.jsc;
const strings = bun.strings;
const uws = bun.uws;
const AnyRoute = bun.api.server.AnyRoute;
const AnyServer = jsc.API.AnyServer;
pub const SSLConfig = _SSLConfig;
pub type StringZ = [:0]const u8;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct ErrDeinitGuard<T> {
    ptr: *T!,
    armed: bool = true,
    ~init() {
        if (_.armed) unsafe { _.ptr.deinit(); }
    }
}

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct ErrFreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,
    armed: bool = true,
    ~init() {
        if (_.armed) _.allocator.free(_.slice);
    }
}

pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

pub struct ExceptionSslGuard {
    global: *jsc.JSGlobalObject!,
    args: *ServerConfig!,
    ~init() {
        unsafe {
            if (_.global.hasException()) {
                if (*conf := _.args.ssl_config) {
                    conf.deinit();
                    _.args.ssl_config = null;
                }
            }
        }
    }
}

pub struct InitCtxErrGuard {
    ctx: *AnyRoute.ServerInitContext!,
    armed: bool = true,
    ~init() {
        if (_.armed) unsafe {
            _.ctx.@"arena".deinit();
            _.ctx.framework_router_list.deinit();
        }
    }
}

pub struct StaticRoutesErrGuard {
    args: *ServerConfig!,
    armed: bool = true,
    ~init() {
        if (_.armed) unsafe {
            for (*static_route of _.args.static_routes.items) {
                static_route.deinit();
            }
            _.args.static_routes.clearAndFree();
        }
    }
}

pub struct SslErrGuard {
    args: *ServerConfig!,
    armed: bool = true,
    ~init() {
        if (_.armed) unsafe {
            if (*conf := _.args.ssl_config) conf.deinit();
        }
    }
}

pub struct _AddressTcp {
    port: u16 = 0,
    hostname: ([*:0]const u8)? = null,
}

pub union(enum) Address {
    tcp: _AddressTcp,
    unix: [:0]const u8,

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        switch (_.*) {
            .tcp => |tcp| {
                if (host := tcp.hostname) {
                    allocator.free(bun.sliceTo(host, 0));
                }
            },
            .unix => |addr| {
                allocator.free(addr);
            },
        }
        _.* = .{ .tcp = .{} };
    }

    pub fn release(*_, allocator: std.mem.Allocator) {
        _.deinit(allocator);
    }
}

pub enum _DevelopmentOption {
    development,
    production,
    development_without_hmr,

    pub fn isHMREnabled(): bool {
        return _ == .development;
    }

    pub fn isDevelopment(): bool {
        return _ == .development || _ == .development_without_hmr;
    }
}

pub union(enum) _RouteDeclarationMethod {
    any: void,
    specific: HTTP.Method,
}

pub struct _RouteDeclaration {
    path: [:0]const u8 = "",
    method: _RouteDeclarationMethod = .@"any",

    pub fn release(*_) {
        if (_.path.len > 0) {
            bun.default_allocator.free(_.path);
        }
    }

    pub fn deinit(*_) {
        _.release();
    }
}

pub struct _StaticRouteEntry {
    path: []const u8,
    route: AnyRoute,
    method: HTTP.Method.Optional = .@"any",

    pub fn memoryCost(): usize {
        return _.path.len + _.route.memoryCost();
    }

    pub fn clone(): _StaticRouteEntry! {
        _.route.ref();

        return .{
            .path = bun.default_allocator.dupe(u8, _.path)?,
            .route = _.route,
            .method = _.method,
        };
    }

    pub fn release(*_) {
        bun.default_allocator.free(_.path);
        _.path = "";
        _.route.deref();
        _.* = undefined;
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub static fn isLessThan(_: void, lhs: _StaticRouteEntry, rhs: _StaticRouteEntry): bool {
        return strings.cmpStringsDesc(undefined, lhs.path, rhs.path);
    }
}

struct _StaticRoutesHashContext {
    pub static fn hash(route: *_StaticRouteEntry): u64 {
        let hasher = std.hash.Wyhash.init(0);
        switch (route.method) {
            .@"any" => hasher.update("ANY"),
            .method => |*set| {
                let iter = set.iterator();
                while (method := iter.next()) {
                    hasher.update(@tagName(method));
                }
            },
        }
        hasher.update(route.path);
        return hasher.final();
    }
}

fn _HandlerWrap(comptime ssl: bool, comptime T: type): type {
    return struct {
        pub static fn handler(route: T, req: *uws.Request, resp: *uws.NewApp(ssl).Response) {
            const any_resp: uws.AnyResponse = if (comptime ssl) .{ .SSL = resp } else .{ .TCP = resp };
            route.onRequest(.{ .h1 = req }, any_resp);
        }

        pub static fn HEAD(route: T, req: *uws.Request, resp: *uws.NewApp(ssl).Response) {
            const any_resp: uws.AnyResponse = if (comptime ssl) .{ .SSL = resp } else .{ .TCP = resp };
            route.onHEADRequest(.{ .h1 = req }, any_resp);
        }
    };
}

fn _H3HandlerWrap(comptime T: type): type {
    return struct {
        pub static fn handler(route: T, req: *uws.H3.Request, resp: *uws.H3.Response) {
            route.onRequest(.{ .h3 = req }, .{ .H3 = resp });
        }
        pub static fn HEAD(route: T, req: *uws.H3.Request, resp: *uws.H3.Response) {
            route.onHEADRequest(.{ .h3 = req }, .{ .H3 = resp });
        }
    };
}

pub struct _FromJSOptions {
    allow_bake_config: bool = true,
    is_fetch_required: bool = true,
    has_user_routes: bool = false,
}

struct UserRouteBuilder {
    route: _RouteDeclaration,
    callback: jsc.Strong.Optional = .empty,

    pub fn release(*_) {
        _.route.deinit();
        _.callback.deinit();
    }

    pub fn deinit(*_) {
        _.release();
    }
}

export @root struct ServerConfig {

address: Address = .{ .tcp = .{} },
idleTimeout: u8 = 10,
has_idleTimeout: bool = false,
base_url: URL = URL{},
base_uri: string = "",

ssl_config: SSLConfig? = null,
sni: bun.BabyList(SSLConfig)? = null,
max_request_body_size: usize = 1024 * 1024 * 128,
development: DevelopmentOption = .development,
broadcast_console_log_from_browser_to_server_for_bake: bool = false,

enable_chrome_devtools_automatic_workspace_folders: bool = true,

onError: jsc.JSValue = jsc.JSValue.zero,
onRequest: jsc.JSValue = jsc.JSValue.zero,
onNodeHTTPRequest: jsc.JSValue = jsc.JSValue.zero,

websocket: WebSocketServerContext? = null,

reuse_port: bool = false,
id: []const u8 = "",
allow_hot: bool = true,
ipv6_only: bool = false,
http3: bool = false,
http1: bool = true,

is_node_http: bool = false,
had_routes_object: bool = false,

static_routes: std.array_list.Managed(StaticRouteEntry) = std.array_list.Managed(StaticRouteEntry).init(bun.default_allocator),
negative_routes: std.array_list.Managed(StringZ) = std.array_list.Managed(StringZ).init(bun.default_allocator),
user_routes_to_build: std.array_list.Managed(UserRouteBuilder) = std.array_list.Managed(UserRouteBuilder).init(bun.default_allocator),

bake: bun.bake.UserOptions? = null,

pub const DevelopmentOption = _DevelopmentOption;

pub fn isDevelopment(): bool {
    return _.development.isDevelopment();
}

pub fn memoryCost(): usize {
    let cost: usize = 0;
    for (*entry of _.static_routes.items) {
        cost += entry.memoryCost();
    }
    cost += _.id.len;
    cost += _.base_url.href.len;
    for (route of _.negative_routes.items) {
        cost += route.len;
    }

    return cost;
}

pub const RouteDeclaration = _RouteDeclaration;
pub const StaticRouteEntry = _StaticRouteEntry;

fn normalizeStaticRoutesList(*_): !void {
    let static_routes_dedupe_list = std.array_list.Managed(u64).init(bun.default_allocator);
    static_routes_dedupe_list.ensureTotalCapacity(@truncate(_.static_routes.items.len))?;
    const _guard_dedupe = DeinitGuard<@TypeOf(static_routes_dedupe_list)>{ ptr: unsafe { &static_routes_dedupe_list } };

    let list = &_.static_routes;
    if (list.items.len > 0) {
        let index = list.items.len - 1;
        while (true) {
            const route = &list.items[index];
            const hash = _StaticRoutesHashContext.hash(route);
            if (std.mem.indexOfScalar(u64, static_routes_dedupe_list.items, hash) != null) {
                let item = list.orderedRemove(index);
                item.deinit();
            } else {
                static_routes_dedupe_list.append(hash)?;
            }

            if (index == 0) break;
            index -= 1;
        }
    }

    std.mem.sort(StaticRouteEntry, list.items, @as(void, undefined), StaticRouteEntry.isLessThan);
}

pub fn cloneForReloadingStaticRoutes(*_): !ServerConfig {
    let that = _.*;
    _.ssl_config = null;
    _.sni = null;
    _.address = .{ .tcp = .{} };
    _.websocket = null;
    _.bake = null;

    that.normalizeStaticRoutesList()?;

    return that;
}

pub fn appendStaticRoute(*_, path: []const u8, route: AnyRoute, method: HTTP.Method.Optional): !void {
    _.static_routes.append(StaticRouteEntry{
        .path = bun.default_allocator.dupe(u8, path)?,
        .route = route,
        .method = method,
    })?;
}

pub static fn applyStaticRoute(server: AnyServer, comptime ssl: bool, app: *uws.NewApp(ssl), comptime T: type, entry: T, path: []const u8, method: HTTP.Method.Optional) {
    entry.server = server;
    const handler_wrap = _HandlerWrap(ssl, T);
    app.head(path, entry, handler_wrap.HEAD);
    switch (method) {
        .@"any" => {
            app.@"any"(path, entry, handler_wrap.handler);
        },
        .method => |*m| {
            let iter = m.iterator();
            while (method_ := iter.next()) {
                app.method(method_, path, entry, handler_wrap.handler);
            }
        },
    }
}

pub static fn applyStaticRouteH3(server: AnyServer, app: *uws.H3.App, comptime T: type, entry: T, path: []const u8, method: HTTP.Method.Optional) {
    entry.server = server;
    const handler_wrap = _H3HandlerWrap(T);
    app.head(path, entry, handler_wrap.HEAD);
    switch (method) {
        .@"any" => app.@"any"(path, entry, handler_wrap.handler),
        .method => |*m| {
            let iter = m.iterator();
            while (method_ := iter.next()) app.method(method_, path, entry, handler_wrap.handler);
        },
    }
}

pub fn release(*_) {
    _.address.deinit(bun.default_allocator);

    for (route of _.negative_routes.items) {
        bun.default_allocator.free(route);
    }
    _.negative_routes.clearAndFree();

    if (_.base_url.href.len > 0) {
        bun.default_allocator.free(_.base_url.href);
        _.base_url = URL{};
    }
    if (*ssl_config := _.ssl_config) {
        ssl_config.deinit();
        _.ssl_config = null;
    }
    if (*sni := _.sni) {
        for (*cfg of sni.slice()) { cfg.deinit(); }
        sni.deinit(bun.default_allocator);
        _.sni = null;
    }

    for (*entry of _.static_routes.items) {
        entry.deinit();
    }
    _.static_routes.clearAndFree();

    if (*bake := _.bake) {
        bake.deinit();
    }

    for (*builder of _.user_routes_to_build.items) {
        builder.deinit();
    }
    _.user_routes_to_build.clearAndFree();
}

pub fn deinit(*_) {
    _.release();
}

pub fn computeID(allocator: std.mem.Allocator): []const u8 {
    let arraylist = std.array_list.Managed(u8).init(allocator);
    let writer = arraylist.writer();

    writer.writeAll("[http]-") catch {};
    switch (_.address) {
        .tcp => {
            if (host := _.address.tcp.hostname) {
                writer.print("tcp:{s}:{d}", .{
                    bun.sliceTo(host, 0),
                    _.address.tcp.port,
                }) catch {};
            } else {
                writer.print("tcp:localhost:{d}", .{
                    _.address.tcp.port,
                }) catch {};
            }
        },
        .unix => {
            writer.print("unix:{s}", .{
                bun.sliceTo(_.address.unix, 0),
            }) catch {};
        },
    }

    return arraylist.items;
}

pub fn getUsocketsOptions(): i32 {
    let out: i32 = if (_.reuse_port)
        uws.LIBUS_LISTEN_REUSE_PORT | uws.LIBUS_LISTEN_REUSE_ADDR
    else
        uws.LIBUS_LISTEN_EXCLUSIVE_PORT;

    if (_.ipv6_only) {
        out |= uws.LIBUS_SOCKET_IPV6_ONLY;
    }

    return out;
}

static fn validateRouteName(global: *jsc.JSGlobalObject, path: []const u8): !void {
    bun.debugAssert(path.len > 0 && path[0] == '/');

    let remaining = path;
    let duped_route_names = bun.StringHashMap(void).init(bun.default_allocator);
    const _guard_route_names = DeinitGuard<@TypeOf(duped_route_names)>{ ptr: unsafe { &duped_route_names } };
    while (index := strings.indexOfChar(remaining, ':')) {
        remaining = remaining[index + 1 ..];
        const end = strings.indexOfChar(remaining, '/') ?? remaining.len;
        const route_name = remaining[0..end];
        if (route_name.len > 0 && std.ascii.isDigit(route_name[0])) {
            return global.throwTODO(
                "Route parameter names cannot start with a number.\n\nIf you run into this, please file an issue and we will add support for it."
            );
        }

        const entry = bun.handleOom(duped_route_names.getOrPut(route_name));
        if (entry.found_existing) {
            return global.throwTODO(
                "Support for duplicate route parameter names is not yet implemented.\n\nIf you run into this, please file an issue and we will add support for it."
            );
        }

        remaining = remaining[end..];
    }
}


static fn getRoutesObject(global: *jsc.JSGlobalObject, arg: jsc.JSValue): bun.JSError!jsc.JSValue? {
    inline for (key of .{ "routes", "static" }) {
        if (routes := arg.get(global, key)?) {
            if (routes.isArray()) {
                return null;
            }
            return routes;
        }
    }
    return null;
}

pub const FromJSOptions = _FromJSOptions;

pub static fn fromJS(
    global: *jsc.JSGlobalObject,
    args: *ServerConfig,
    arguments: *jsc.CallFrame.ArgumentsSlice,
    opts: FromJSOptions,
): bun.JSError!void {
    const vm = arguments.vm;
    const env = vm.transpiler.env;

    args.* = .{
        .address = .{
            .tcp = .{
                .port = 3000,
                .hostname = null,
            },
        },
        .development = if (vm.transpiler.options.transform_options.serve_hmr != null && !vm.transpiler.options.transform_options.serve_hmr.?) .development_without_hmr else .development,

        .reuse_port = env.get("NODE_UNIQUE_ID") != null,
    };
    let has_hostname = false;

    defer {
        if (!args.development.isHMREnabled()) {
            bun.assert(args.bake == null);
        }
    }

    if (strings.eqlComptime(env.get("NODE_ENV") ?? "", "production")) {
        args.development = .production;
    }

    if (arguments.vm.transpiler.options.production) {
        args.development = .production;
    }

    args.address.tcp.port = brk: {
        const PORT_ENV = .{ "BUN_PORT", "PORT", "NODE_PORT" };

        inline for (PORT of PORT_ENV) {
            if (port := env.get(PORT)) {
                if (p := std.fmt.parseInt(u16, port, 10) catch null) {
                    break :brk p;
                }
            }
        }

        if (port := arguments.vm.transpiler.options.transform_options.port) {
            break :brk port;
        }

        break :brk args.address.tcp.port;
    };
    let port = args.address.tcp.port;

    if (origin := arguments.vm.transpiler.options.transform_options.origin) {
        args.base_uri = bun.default_allocator.dupeZ(u8, origin)?;
    }

    const _guard_exc_ssl = ExceptionSslGuard{ global: global, args: args };

    if (arg := arguments.next()) {
        if (!arg.isObject()) {
            return global.throwInvalidArguments("Bun.serve expects an object", .{});
        }

        if (dev := arg.get(global, "development")?) {
            if (dev.isObject()) {
                if (hmr := dev.getBooleanStrict(global, "hmr")?) {
                    args.development = if (!hmr) .development_without_hmr else .development;
                } else {
                    args.development = .development;
                }

                if (console := dev.getBooleanStrict(global, "console")?) {
                    args.broadcast_console_log_from_browser_to_server_for_bake = console;
                }

                if (enable_chrome_devtools_automatic_workspace_folders := dev.getBooleanStrict(global, "chromeDevToolsAutomaticWorkspaceFolders")?) {
                    args.enable_chrome_devtools_automatic_workspace_folders = enable_chrome_devtools_automatic_workspace_folders;
                }
            } else {
                args.development = if (dev.toBoolean()) .development else .production;
            }
            args.reuse_port = args.development == .production;
        }
        if (global.hasException()) return error.JSError;

        if (static_val := getRoutesObject(global, arg)?) {
            const static_obj_opt = static_val.getObject();
            if (static_obj_opt == null) {
                return global.throwInvalidArguments(
                    "Bun.serve() expects 'routes' to be an object shaped like:\n\n  {\n    \"/path\": {\n      GET: (req) => new Response(\"Hello\"),\n      POST: (req) => new Response(\"Hello\"),\n    },\n    \"/path2/:param\": new Response(\"Hello\"),\n    \"/path3/:param1/:param2\": (req) => new Response(\"Hello\")\n  }\n\nLearn more at https://bun.com/docs/api/http"
                , .{});
            }
            const static_obj = static_obj_opt.?;
            args.had_routes_object = true;

            let iter = jsc.JSPropertyIterator(.{
                .skip_empty_name = true,
                .include_value = true,
            }).init(global, static_obj)?;
            const _guard_iter = DeinitGuard<@TypeOf(iter)>{ ptr: unsafe { &iter } };

            let init_ctx_: AnyRoute.ServerInitContext = .{
                .@"arena" = std.heap.ArenaAllocator.init(bun.default_allocator),
                .dedupe_html_bundle_map = std.AutoHashMap(_Ptrs.HTMLBundlePtr, bun.ptr.RefPtr(HTMLBundle.Route)).init(bun.default_allocator),
                .framework_router_list = std.array_list.Managed(bun.bake.Framework.FileSystemRouterType).init(bun.default_allocator),
                .js_string_allocations = .empty,
                .user_routes = &args.static_routes,
                .global = global,
            };
            const init_ctx: *AnyRoute.ServerInitContext = &mut init_ctx_;
            let _guard_init_ctx = InitCtxErrGuard{ ctx: init_ctx };
            const _guard_dedupe_map = DeinitGuard<@TypeOf(init_ctx.dedupe_html_bundle_map)>{ ptr: unsafe { &init_ctx.dedupe_html_bundle_map } };

            let framework_router_list = std.array_list.Managed(bun.bake.FrameworkRouter.Type).init(bun.default_allocator);
            let _guard_fw_router = ErrDeinitGuard<@TypeOf(framework_router_list)>{ ptr: unsafe { &framework_router_list } };

            let _guard_static_routes = StaticRoutesErrGuard{ args: args };

            while (key := iter.next()?) {
                const path_pair = bun.handleOom(key.toOwnedSliceReturningAllASCII(bun.default_allocator));
                const path = path_pair[0];
                const is_ascii = path_pair[1];
                let _guard_path = ErrFreeGuard<u8>{ allocator: bun.default_allocator, slice: path };

                const value: jsc.JSValue = iter.value;

                if (value.isUndefined()) {
                    _guard_path.armed = false;
                    continue;
                }

                if (path.len == 0 || (path[0] != '/')) {
                    return global.throwInvalidArguments("Invalid route {f}. Path must start with '/'", .{bun.fmt.quote(path)});
                }

                if (!is_ascii) {
                    return global.throwInvalidArguments("Invalid route {f}. Please encode all non-ASCII characters in the path.", .{bun.fmt.quote(path)});
                }

                if (value == .false) {
                    _guard_path.armed = false;
                    const _guard_neg_path = FreeGuard<u8>{ allocator: bun.default_allocator, slice: path };
                    const duped = bun.handleOom(bun.default_allocator.dupeZ(u8, path));
                    bun.handleOom(args.negative_routes.append(duped));
                    continue;
                }

                if (value.isCallable()) {
                    validateRouteName(global, path)?;
                    args.user_routes_to_build.append(.{
                        .route = .{
                            .path = bun.handleOom(bun.default_allocator.dupeZ(u8, path)),
                            .method = .@"any",
                        },
                        .callback = jsc.Strong.Optional.create(value.withAsyncContextIfNeeded(global), global),
                    }) catch e => bun.handleOom(e);
                    _guard_path.armed = false;
                    bun.default_allocator.free(path);
                    continue;
                } else if (value.isObject()) {
                    const methods: [9]HTTP.Method = [
                        HTTP.Method.CONNECT,
                        HTTP.Method.DELETE,
                        HTTP.Method.GET,
                        HTTP.Method.HEAD,
                        HTTP.Method.OPTIONS,
                        HTTP.Method.PATCH,
                        HTTP.Method.POST,
                        HTTP.Method.PUT,
                        HTTP.Method.TRACE,
                    ];
                    let found = false;
                    inline for (method of methods) {
                        if (function := value.getOwn(global, @tagName(method))?) {
                            if (!found) {
                                validateRouteName(global, path)?;
                            }
                            found = true;

                            if (function.isCallable()) {
                                args.user_routes_to_build.append(.{
                                    .route = .{
                                        .path = bun.handleOom(bun.default_allocator.dupeZ(u8, path)),
                                        .method = .{ .specific = method },
                                    },
                                    .callback = jsc.Strong.Optional.create(function.withAsyncContextIfNeeded(global), global),
                                }) catch e => bun.handleOom(e);
                            } else if (html_route := AnyRoute.fromJS(global, path, function, init_ctx)?) {
                                let method_set = bun.http.Method.Set.initEmpty();
                                method_set.insert(method);

                                args.static_routes.append(.{
                                    .path = bun.handleOom(bun.default_allocator.dupe(u8, path)),
                                    .route = html_route,
                                    .method = .{ .method = method_set },
                                }) catch e => bun.handleOom(e);
                            }
                        }
                    }

                    if (found) {
                        _guard_path.armed = false;
                        bun.default_allocator.free(path);
                        continue;
                    }
                }

                const route_opt = AnyRoute.fromJS(global, path, value, init_ctx)?;
                if (route_opt == null) {
                    return global.throwInvalidArguments(
                        "'routes' expects a Record<string, Response | HTMLBundle | {[method: string]: (req: BunRequest) => Response|Promise<Response>}>\n\nTo bundle frontend apps on-demand with Bun.serve(), import HTML files.\n\nExample:\n\n```js\nimport { serve } from \"bun\";\nimport app from \"./app.html\";\n\nserve({\n  routes: {\n    \"/index.json\": Response.json({ message: \"Hello World\" }),\n    \"/app\": app,\n    \"/path/:param\": (req) => {\n      const param = req.params.param;\n      return Response.json({ message: `Hello ${param}` });\n    },\n    \"/path\": {\n      GET(req) {\n        return Response.json({ message: \"Hello World\" });\n      },\n      POST(req) {\n        return Response.json({ message: \"Hello World\" });\n      },\n    },\n  },\n\n  fetch(request) {\n    return new Response(\"fallback response\");\n  },\n});\n```\n\nSee https://bun.com/docs/api/http for more information."
                    ,
                        .{},
                    );
                }
                const route = route_opt.?;
                args.static_routes.append(.{
                    .path = path,
                    .route = route,
                }) catch e => bun.handleOom(e);
                _guard_path.armed = false;
            }

            if ((init_ctx.dedupe_html_bundle_map.count() > 0 ||
                init_ctx.framework_router_list.items.len > 0))
            {
                if (args.development.isHMREnabled()) {
                    const root = bun.fs.FileSystem.instance.top_level_dir;
                    const framework = bun.bake.Framework.auto(
                        init_ctx.@"arena".allocator(),
                        &global.bunVM().transpiler.resolver,
                        init_ctx.framework_router_list.items,
                    )?;
                    args.bake = .{
                        .@"arena" = init_ctx.@"arena",
                        .allocations = init_ctx.js_string_allocations,
                        .root = root,
                        .framework = framework,
                        .bundler_options = bun.bake.SplitBundlerOptions.empty,
                    };
                    const bake_ptr = &args.bake.?;

                    const o = vm.transpiler.options.transform_options;

                    switch (o.serve_env_behavior) {
                        .prefix => {
                            bake_ptr.bundler_options.client.env_prefix = vm.transpiler.options.transform_options.serve_env_prefix;
                            bake_ptr.bundler_options.client.env = .prefix;
                        },
                        .load_all => {
                            bake_ptr.bundler_options.client.env = .load_all;
                        },
                        .disable => {
                            bake_ptr.bundler_options.client.env = .disable;
                        },
                        else => {},
                    }

                    if (define := o.serve_define) {
                        bake_ptr.bundler_options.client.define = define;
                        bake_ptr.bundler_options.server.define = define;
                        bake_ptr.bundler_options.ssr.define = define;
                    }
                } else {
                    if (init_ctx.framework_router_list.items.len > 0) {
                        return global.throwInvalidArguments("FrameworkRouter is currently only supported when `development: true`", .{});
                    }
                    init_ctx.@"arena".deinit();
                }
            } else {
                bun.debugAssert(init_ctx.@"arena".state.end_index == 0 &&
                    init_ctx.@"arena".state.buffer_list.first == null);
                init_ctx.@"arena".deinit();
            }
            _guard_static_routes.armed = false;
            _guard_fw_router.armed = false;
            _guard_init_ctx.armed = false;
        }

        if (global.hasException()) return error.JSError;

        if (value := arg.get(global, "idleTimeout")?) {
            if (!value.isUndefinedOrNull()) {
                if (!value.isAnyInt()) {
                    return global.throwInvalidArguments("Bun.serve expects idleTimeout to be an integer", .{});
                }
                args.has_idleTimeout = true;

                const idle_timeout_val: u64 = @intCast(@max(value.toInt64(), 0));
                if (idle_timeout_val > 255) {
                    return global.throwInvalidArguments("Bun.serve expects idleTimeout to be 255 or less", .{});
                }

                args.idleTimeout = @truncate(idle_timeout_val);
            }
        }

        const ws_obj_1 = arg.getTruthy(global, "webSocket")?;
        const ws_obj_2 = if (ws_obj_1 == null) arg.getTruthy(global, "websocket")? else ws_obj_1;
        if (websocket_object := ws_obj_2) {
            if (!websocket_object.isObject()) {
                if (*conf := args.ssl_config) {
                    conf.deinit();
                }
                return global.throwInvalidArguments("Expected websocket to be an object", .{});
            }

            let _guard_ws_ssl = SslErrGuard{ args: args };
            args.websocket = WebSocketServerContext.onCreate(global, websocket_object)?;
            _guard_ws_ssl.armed = false;
        }
        if (global.hasException()) return error.JSError;

        if (port_ := arg.get(global, "port")?) {
            if (!port_.isUndefined()) {
                const port_num = port_.toNumber(global)?;
                if (!std.math.isFinite(port_num) || @trunc(port_num) != port_num || port_num < 0 || port_num > 65535) {
                    return global.throwRangeError(port_num, .{ field_name: "options.port", min: 0, max: 65535 });
                }
                args.address.tcp.port = @as(u16, @intFromFloat(port_num));
                port = args.address.tcp.port;
            }
        }
        if (global.hasException()) return error.JSError;

        if (baseURI := arg.getTruthy(global, "baseURI")?) {
            let sliced = baseURI.toSlice(global, bun.default_allocator)?;

            if (sliced.len > 0) {
                const _guard_sliced = DeinitGuard<@TypeOf(sliced)>{ ptr: unsafe { &sliced } };
                if (args.base_uri.len > 0) {
                    unsafe { bun.default_allocator.free(@constCast(args.base_uri)); }
                }
                args.base_uri = bun.default_allocator.dupe(u8, sliced.slice()) catch unreachable;
            }
        }
        if (global.hasException()) return error.JSError;

        const host_1 = arg.getStringish(global, "hostname")?;
        const host_2 = host_1 ?? (arg.getStringish(global, "host")?);
        if (host_2 != null) {
            let host = host_2.?;
            const _guard_host = DerefGuard<bun.String>{ ptr: unsafe { &host } };
            let host_str = host.toUTF8(bun.default_allocator);
            const _guard_host_str = DeinitGuard<@TypeOf(host_str)>{ ptr: unsafe { &host_str } };

            if (host_str.len > 0) {
                args.address.tcp.hostname = bun.default_allocator.dupeZ(u8, host_str.slice()) catch unreachable;
                has_hostname = true;
            }
        }
        if (global.hasException()) return error.JSError;

        const unix_opt = arg.getStringish(global, "unix")?;
        if (unix_opt != null) {
            let unix = unix_opt.?;
            const _guard_unix = DerefGuard<bun.String>{ ptr: unsafe { &unix } };
            let unix_str = unix.toUTF8(bun.default_allocator);
            const _guard_unix_str = DeinitGuard<@TypeOf(unix_str)>{ ptr: unsafe { &unix_str } };
            if (unix_str.len > 0) {
                if (has_hostname) {
                    return global.throwInvalidArguments("Cannot specify both hostname and unix", .{});
                }

                args.address = .{ .unix = bun.default_allocator.dupeZ(u8, unix_str.slice()) catch unreachable };
            }
        }
        if (global.hasException()) return error.JSError;

        if (id := arg.get(global, "id")?) {
            if (id.isUndefinedOrNull()) {
                args.allow_hot = false;
            } else {
                const id_str = id.toUTF8Bytes(global, bun.default_allocator)?;
                if (id_str.len > 0) {
                    args.id = id_str;
                } else {
                    args.allow_hot = false;
                }
            }
        }
        if (global.hasException()) return error.JSError;

        if (opts.allow_bake_config) {
            if (bake_args_js := arg.getTruthy(global, "app")?) {
                if (bun.FeatureFlags.bake()) {
                    if (args.bake != null) {
                        return global.throwInvalidArguments("'app' + HTML loader not supported.", .{});
                    }

                    if (args.development == .production) {
                        return global.throwInvalidArguments("TODO: 'development: false' in serve options with 'app'. For now, use `bun build --app` or set 'development: true'", .{});
                    }

                    args.bake = bun.bake.UserOptions.fromJS(bake_args_js, global)?;
                }
            }
        }

        if (dev := arg.get(global, "reusePort")?) {
            args.reuse_port = dev.toBoolean();
        }
        if (global.hasException()) return error.JSError;

        if (dev := arg.get(global, "ipv6Only")?) {
            args.ipv6_only = dev.toBoolean();
        }
        if (global.hasException()) return error.JSError;

        if (v := arg.get(global, "http3")?) {
            args.http3 = v.toBoolean();
        }
        if (global.hasException()) return error.JSError;

        if (v := arg.get(global, "http1")?) {
            args.http1 = v.toBoolean();
        }
        if (global.hasException()) return error.JSError;

        if (max_request_body_size := arg.getTruthy(global, "maxRequestBodySize")?) {
            if (max_request_body_size.isNumber()) {
                args.max_request_body_size = @as(u64, @intCast(@max(0, max_request_body_size.toInt64())));
            }
        }
        if (global.hasException()) return error.JSError;

        if (onError := arg.getTruthyComptime(global, "error")?) {
            if (!onError.isCallable()) {
                return global.throwInvalidArguments("Expected error to be a function", .{});
            }
            const onErrorSnapshot = onError.withAsyncContextIfNeeded(global);
            args.onError = onErrorSnapshot;
            onErrorSnapshot.protect();
        }
        if (global.hasException()) return error.JSError;

        if (onRequest_ := arg.getTruthy(global, "onNodeHTTPRequest")?) {
            if (!onRequest_.isCallable()) {
                return global.throwInvalidArguments("Expected onNodeHTTPRequest to be a function", .{});
            }
            const onRequest = onRequest_.withAsyncContextIfNeeded(global);
            onRequest.protect();
            args.onNodeHTTPRequest = onRequest;
        }

        if (onRequest_ := arg.getTruthy(global, "fetch")?) {
            if (!onRequest_.isCallable()) {
                return global.throwInvalidArguments("Expected fetch() to be a function", .{});
            }
            const onRequest = onRequest_.withAsyncContextIfNeeded(global);
            onRequest.protect();
            args.onRequest = onRequest;
        } else if (args.bake == null && args.onNodeHTTPRequest == .zero && ((args.static_routes.items.len + args.user_routes_to_build.items.len) == 0 && !opts.has_user_routes) && opts.is_fetch_required) {
            if (global.hasException()) return error.JSError;
            return global.throwInvalidArguments(
                "Bun.serve() needs either:\n\n  - A routes object:\n     routes: {\n       \"/path\": {\n         GET: (req) => new Response(\"Hello\")\n       }\n     }\n\n  - Or a fetch handler:\n     fetch: (req) => {\n       return new Response(\"Hello\")\n     }\n\nLearn more at https://bun.com/docs/api/http"
            , .{});
        } else {
            if (global.hasException()) return error.JSError;
        }

        if (tls := arg.getTruthy(global, "tls")?) {
            if (tls.isFalsey()) {
                args.ssl_config = null;
            } else if (tls.jsType().isArray()) {
                let value_iter = tls.arrayIterator(global)?;
                if (value_iter.len == 0) {
                } else {
                    while (item := value_iter.next()?) {
                        if (cfg := SSLConfig.fromJS(vm, global, item)?) {
                            let ssl_config = cfg;
                            if (args.ssl_config == null) {
                                args.ssl_config = ssl_config;
                            } else {
                                if ((ssl_config.server_name ?? "")[0] == 0) {
                                    const _guard_sni_ssl = DeinitGuard<SSLConfig>{ ptr: unsafe { &ssl_config } };
                                    return global.throwInvalidArguments("SNI tls object must have a serverName", .{});
                                }
                                if (args.sni == null) {
                                    args.sni = bun.handleOom(bun.BabyList(SSLConfig).initCapacity(bun.default_allocator, value_iter.len - 1));
                                }

                                bun.handleOom(args.sni.?.append(bun.default_allocator, ssl_config));
                            }
                        } else {
                            if (global.hasException()) {
                                return error.JSError;
                            }
                            continue;
                        }
                    }
                }
            } else {
                if (ssl_config := SSLConfig.fromJS(vm, global, tls)?) {
                    args.ssl_config = ssl_config;
                }
                if (global.hasException()) {
                    return error.JSError;
                }
            }
        }
        if (global.hasException()) return error.JSError;

        if (args.ssl_config == null) {
            if (ssl_config := SSLConfig.fromJS(vm, global, arg)?) {
                args.ssl_config = ssl_config;
            }
            if (global.hasException()) {
                return error.JSError;
            }
        }

        if (args.http3) {
            if (args.ssl_config == null) {
                return global.throwInvalidArguments("HTTP/3 requires 'tls' to be set", .{});
            }
        } else if (!args.http1) {
            return global.throwInvalidArguments("Cannot disable http1 without enabling http3", .{});
        }
        if (!args.http1 && args.address == .unix) {
            return global.throwInvalidArguments("Cannot disable http1 with a unix socket - HTTP/3 over AF_UNIX is not supported", .{});
        }
    } else {
        return global.throwInvalidArguments("Bun.serve expects an object", .{});
    }

    if (args.base_uri.len > 0) {
        args.base_url = URL.parse(args.base_uri);
        if (args.base_url.hostname.len == 0) {
            unsafe { bun.default_allocator.free(@constCast(args.base_uri)); }
            args.base_uri = "";
            return global.throwInvalidArguments("baseURI must have a hostname", .{});
        }

        if (!strings.isAllASCII(args.base_uri)) {
            unsafe { bun.default_allocator.free(@constCast(args.base_uri)); }
            args.base_uri = "";
            return global.throwInvalidArguments("Unicode baseURI must already be encoded for now.\nnew URL(baseuRI).toString() should do the trick.", .{});
        }

        if (args.base_url.protocol.len == 0) {
            const protocol: string = if (args.ssl_config != null) "https" else "http";
            const hostname = args.base_url.hostname;
            const needsBrackets: bool = strings.isIPV6Address(hostname) && hostname[0] != '[';
            const original_base_uri = args.base_uri;
            const _guard_orig_uri = FreeGuard<u8>{ allocator: bun.default_allocator, slice: original_base_uri };
            if (needsBrackets) {
                args.base_uri = (if ((port == 80 && args.ssl_config == null) || (port == 443 && args.ssl_config != null))
                    std.fmt.allocPrint(bun.default_allocator, "{s}://[{s}]/{s}", .{
                        protocol,
                        hostname,
                        strings.trimLeadingChar(args.base_url.pathname, '/'),
                    })
                else
                    std.fmt.allocPrint(bun.default_allocator, "{s}://[{s}]:{d}/{s}", .{
                        protocol,
                        hostname,
                        port,
                        strings.trimLeadingChar(args.base_url.pathname, '/'),
                    })) catch unreachable;
            } else {
                args.base_uri = (if ((port == 80 && args.ssl_config == null) || (port == 443 && args.ssl_config != null))
                    std.fmt.allocPrint(bun.default_allocator, "{s}://{s}/{s}", .{
                        protocol,
                        hostname,
                        strings.trimLeadingChar(args.base_url.pathname, '/'),
                    })
                else
                    std.fmt.allocPrint(bun.default_allocator, "{s}://{s}:{d}/{s}", .{
                        protocol,
                        hostname,
                        port,
                        strings.trimLeadingChar(args.base_url.pathname, '/'),
                    })) catch unreachable;
            }

            args.base_url = URL.parse(args.base_uri);
        }
    } else {
        const hostname: string =
            if (has_hostname) std.mem.span(args.address.tcp.hostname.?) else "0.0.0.0";

        const needsBrackets: bool = strings.isIPV6Address(hostname) && hostname[0] != '[';

        const protocol: string = if (args.ssl_config != null) "https" else "http";
        if (needsBrackets) {
            args.base_uri = (if ((port == 80 && args.ssl_config == null) || (port == 443 && args.ssl_config != null))
                std.fmt.allocPrint(bun.default_allocator, "{s}://[{s}]/", .{
                    protocol,
                    hostname,
                })
            else
                std.fmt.allocPrint(bun.default_allocator, "{s}://[{s}]:{d}/", .{ protocol, hostname, port })) catch unreachable;
        } else {
            args.base_uri = (if ((port == 80 && args.ssl_config == null) || (port == 443 && args.ssl_config != null))
                std.fmt.allocPrint(bun.default_allocator, "{s}://{s}/", .{
                    protocol,
                    hostname,
                })
            else
                std.fmt.allocPrint(bun.default_allocator, "{s}://{s}:{d}/", .{ protocol, hostname, port })) catch unreachable;
        }

        if (!strings.isAllASCII(hostname)) {
            unsafe { bun.default_allocator.free(@constCast(args.base_uri)); }
            args.base_uri = "";
            return global.throwInvalidArguments("Unicode hostnames must already be encoded for now.\nnew URL(input).hostname should do the trick.", .{});
        }

        args.base_url = URL.parse(args.base_uri);
    }

    if (args.base_url.hostname.len == 0) {
        unsafe { bun.default_allocator.free(@constCast(args.base_uri)); }
        args.base_uri = "";
        return global.throwInvalidArguments("baseURI must have a hostname", .{});
    }

    if (args.base_url.username.len > 0 || args.base_url.password.len > 0) {
        unsafe { bun.default_allocator.free(@constCast(args.base_uri)); }
        args.base_uri = "";
        return global.throwInvalidArguments("baseURI can't have a username or password", .{});
    }

    return;
}
}
