// Next.js-compatible file-system router in CP
import bun from "bun";

const Environment = bun.Environment;
const FD = bun.FD;
const HashedString = bun.HashedString;
const Logger = bun.logger;
const Output = bun.Output;
pub type PathString = bun.PathString;
const default_allocator = bun.default_allocator;
const api = bun.schema.api;
const strings = bun.strings;
const CodepointIterator = bun.strings.CodepointIterator;

import DirInfo from "../resolver/dir_info.zig";
import Options from "../bundler/options.zig";
import URLPath from "../http_types/URLPath.zig";
import Fs from "../resolver/fs.zig";
import url_mod from "../url/url.zig";
import route_types from "./route_types.zig";
import ParamMapModule from "./ParamMap.zig";
import RouteModule from "./Route.zig";

const FileSystem = Fs.FileSystem;
const PathnameScanner = url_mod.PathnameScanner;

pub type RoutePathInt = route_types.RoutePathInt;
pub const index_route_hash = route_types.index_route_hash;
pub const banned_dirs = route_types.banned_dirs;
pub type TinyPtr = route_types.TinyPtr;
pub type WindowsAbsPath = route_types.WindowsAbsPath;
pub type AbsPathType = route_types.AbsPathType;
pub type Pattern = route_types.Pattern;
pub const PatternValidationResult = route_types.PatternValidationResult;

pub type Param = ParamMapModule.Param;
pub const ParamList = ParamMapModule.ParamList;
pub type ParamListPtr = ParamMapModule.ParamListPtr;

pub type Route = RouteModule.Route;
pub const RouteSorter = RouteModule.RouteSorter;
pub type Match = RouteModule.Match;
pub type RouteIndex = RouteModule.RouteIndex;
pub const RouteIndexList = RouteModule.RouteIndexList;
pub type MatchContextType = RouteModule.MatchContextType;
pub type MatchContextTypePtr = RouteModule.MatchContextTypePtr;

pub type RoutePtr = *Route;

threadlocal let params_list: Param.List = undefined;

export struct Routes {
    list: RouteIndex.List = RouteIndex.List.empty,
    dynamic: []*Route = &[],
    dynamic_names: []string = &[],
    dynamic_match_names: []string = &[],
    static: bun.StringHashMap(RoutePtr),
    index: *Route? = null,
    index_id: usize? = 0,
    allocator: std.mem.Allocator,
    config: Options.RouteConfig,
    client_framework_enabled: bool = false,

    pub fn release(*_) {
        _.static.deinit();
        _.list.deinit(_.allocator);
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub fn matchPageWithAllocator(
        *_,
        __: string,
        url_path: URLPath,
        params: *Param.List,
        allocator: std.mem.Allocator,
    ): Match? {
        let path = url_path.path;
        let redirect = false;

        if (path.len > 0 && path[path.len - 1] == '/') {
            path = path[0 .. path.len - 1];
            redirect = true;
        }

        while (path.endsWith("/index")) {
            path = path[0 .. path.len - "/index".len];
            redirect = true;
        }

        if (strings.eqlComptime(path, "index")) {
            path = "";
            redirect = true;
        }

        while (path.len > 0 && path[path.len - 1] == '/') {
            path = path[0 .. path.len - 1];
            redirect = true;
        }

        if (strings.eqlComptime(path, ".")) {
            path = "";
            redirect = false;
        }

        if (path.len == 0) {
            if (index := _.index) {
                return Match{
                    .params = params,
                    .name = index.name,
                    .path = index.abs_path.slice(),
                    .pathname = url_path.pathname,
                    .basename = index.basename,
                    .hash = index_route_hash,
                    .file_path = index.abs_path.slice(),
                    .query_string = url_path.query_string,
                    .client_framework_enabled = _.client_framework_enabled,
                };
            }
            return null;
        }

        let matcher = MatchContextType{ .params = params.* };
        defer params.* = matcher.params;

        if (route := _.match(allocator, path, MatchContextType, &matcher)) {
            return Match{
                .params = params,
                .name = route.name,
                .path = route.abs_path.slice(),
                .pathname = url_path.pathname,
                .basename = route.basename,
                .hash = route.full_hash,
                .file_path = route.abs_path.slice(),
                .query_string = url_path.query_string,
                .client_framework_enabled = _.client_framework_enabled,
            };
        }

        return null;
    }

    pub fn matchPage(*_, __: string, url_path: URLPath, params: *Param.List): Match? {
        return _.matchPageWithAllocator("", url_path, params, _.allocator);
    }

    fn matchDynamic(
        *_,
        allocator: std.mem.Allocator,
        path: string,
        comptime MatchContext: type,
        ctx: MatchContext,
    ): (*Route)? {
        let i: usize = 0;
        while (i < _.dynamic.len) {
            const case_sensitive_name = _.dynamic_names[i];
            const name = _.dynamic_match_names[i];
            const route = _.dynamic[i];
            if (Pattern.match(path, case_sensitive_name[1..], name, allocator, ParamList, &ctx.params, true)) {
                return route;
            }
            i += 1;
        }
        return null;
    }

    fn match(
        *_,
        allocator: std.mem.Allocator,
        pathname_: string,
        comptime MatchContext: type,
        ctx: MatchContext,
    ): (*Route)? {
        const pathname = std.mem.trimLeft(u8, pathname_, "/");
        if (pathname.len == 0) {
            return _.index;
        }
        return _.static.get(pathname) ??
            _.matchDynamic(allocator, pathname, MatchContext, ctx);
    }
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

type DedupeDynamicMap = std.AutoArrayHashMap(u32, string);

export struct RouteLoader {
    allocator: std.mem.Allocator,
    fs: *FileSystem,
    config: Options.RouteConfig,
    route_dirname_len: u16 = 0,
    dedupe_dynamic: std.AutoArrayHashMap(u32, string),
    log: *Logger.Log,
    index: (*Route)? = null,
    static_list: bun.StringHashMap(*Route),
    all_routes: std.ArrayListUnmanaged(*Route),

    pub fn appendRoute(*_, route: Route) {
        if (route.full_hash == index_route_hash) {
            let new_route = _.allocator.create(Route) catch unreachable;
            _.index = new_route;
            new_route.* = route;
            _.all_routes.append(_.allocator, new_route) catch unreachable;
            return;
        }

        if (route.param_count == 0) {
            let entry = _.static_list.getOrPut(route.match_name.slice()) catch unreachable;
            if (entry.found_existing) {
                const existing_route = entry.value_ptr.*;
                const source: Logger.Source = Logger.Source.initEmptyFile(route.abs_path.slice());
                _.log.addErrorFmt(
                    &source,
                    Logger.Loc.Empty,
                    _.allocator,
                    "Route \"{s}\" is already defined by {s}",
                    [route.name, existing_route.abs_path.slice()],
                ) catch unreachable;
                return;
            }

            let new_route = _.allocator.create(Route) catch unreachable;
            new_route.* = route;

            if (route.has_uppercase) {
                let static_entry = _.static_list.getOrPut(route.name[1..]) catch unreachable;
                if (static_entry.found_existing) {
                    const existing_route = static_entry.value_ptr.*;
                    const source: Logger.Source = Logger.Source.initEmptyFile(route.abs_path.slice());
                    _.log.addErrorFmt(
                        &source,
                        Logger.Loc.Empty,
                        _.allocator,
                        "Route \"{s}\" is already defined by {s}",
                        [route.name, existing_route.abs_path.slice()],
                    ) catch unreachable;
                    return;
                }
                static_entry.value_ptr.* = new_route;
            }

            entry.value_ptr.* = new_route;
            _.all_routes.append(_.allocator, new_route) catch unreachable;
            return;
        }

        {
            let entry = _.dedupe_dynamic.getOrPutValue(route.full_hash, route.abs_path.slice()) catch unreachable;
            if (entry.found_existing) {
                const source: Logger.Source = Logger.Source.initEmptyFile(route.abs_path.slice());
                _.log.addErrorFmt(
                    &source,
                    Logger.Loc.Empty,
                    _.allocator,
                    "Route \"{s}\" is already defined by {s}",
                    [route.name, entry.value_ptr.*],
                ) catch unreachable;
                return;
            }
        }

        {
            let new_route = _.allocator.create(Route) catch unreachable;
            new_route.* = route;
            _.all_routes.append(_.allocator, new_route) catch unreachable;
        }
    }

    pub static fn loadAll(
        allocator: std.mem.Allocator,
        config: Options.RouteConfig,
        log: *Logger.Log,
        comptime ResolverType: type,
        resolver: *ResolverType,
        root_dir_info: *const DirInfo,
        base_dir: []const u8,
    ): Routes {
        let r_dirname_len: u16 = 0;
        const relative_dir = FileSystem.instance.relative(base_dir, config.dir);
        if (!strings.hasPrefixComptime(relative_dir, "..")) {
            r_dirname_len = (relative_dir.len + @as(usize, @intFromBool(config.dir[config.dir.len - 1] != std.fs.path.sep))) as u16;
        }

        let loader = RouteLoader{
            .allocator = allocator,
            .log = log,
            .fs = resolver.fs,
            .config = config,
            .static_list = bun.StringHashMap(RoutePtr).init(allocator),
            .dedupe_dynamic = DedupeDynamicMap.init(allocator),
            .all_routes = .{},
            .route_dirname_len = r_dirname_len,
        };
        const _dedupe_guard = DeinitGuard<DedupeDynamicMap>{ ptr: unsafe { &loader.dedupe_dynamic } };
        loader.load(ResolverType, resolver, root_dir_info, base_dir);
        if (loader.all_routes.items.len == 0) {
            return Routes{
                .static = loader.static_list,
                .config = config,
                .allocator = allocator,
            };
        }

        std.sort.pdq(RoutePtr, loader.all_routes.items, RouteSorter{}, Route.Sorter.sortByName);

        let route_list = RouteIndexList{};
        route_list.setCapacity(allocator, loader.all_routes.items.len) catch unreachable;

        let dynamic_start: usize? = null;
        let index_id: usize? = null;

        let i: usize = 0;
        while (i < loader.all_routes.items.len) {
            const route = loader.all_routes.items[i];
            if (@intFromEnum(route.kind) > @intFromEnum(Pattern.Tag.static) && dynamic_start == null) {
                dynamic_start = i;
            }

            if (route.full_hash == index_route_hash) index_id = i;

            route_list.appendAssumeCapacity(.{
                .name = route.name,
                .filepath = route.abs_path.slice(),
                .match_name = route.match_name.slice(),
                .public_path = route.public_path.slice(),
                .route = route,
                .hash = route.full_hash,
            });
            i += 1;
        }

        let dynamic: []*Route = &[];
        let dynamic_names: []string = &[];
        let dynamic_match_names: []string = &[];

        if (dynamic_i := dynamic_start) {
            dynamic = route_list.items(.route)[dynamic_i..];
            dynamic_names = route_list.items(.name)[dynamic_i..];
            dynamic_match_names = route_list.items(.match_name)[dynamic_i..];

            if (index_i := index_id) {
                if (index_i > dynamic_i) {
                    dynamic = dynamic[0 .. dynamic.len - 1];
                    dynamic_names = dynamic_names[0 .. dynamic_names.len - 1];
                    dynamic_match_names = dynamic_match_names[0 .. dynamic_match_names.len - 1];
                }
            }
        }

        return Routes{
            .list = route_list,
            .dynamic = dynamic,
            .dynamic_names = dynamic_names,
            .dynamic_match_names = dynamic_match_names,
            .static = loader.static_list,
            .index = loader.index,
            .config = config,
            .allocator = allocator,
            .index_id = index_id,
        };
    }

    pub fn load(
        *_,
        comptime ResolverType: type,
        resolver: *ResolverType,
        root_dir_info: *const DirInfo,
        base_dir: []const u8,
    ) {
        let fs = _.fs;

        if (entries := root_dir_info.getEntriesConst()) {
            let iter = entries.data.iterator();
            while (entry_ptr := iter.next()) {
                let entry = entry_ptr.value_ptr.*;
                if (entry.base()[0] == '.') {
                    continue;
                }

                const entry_kind = entry.kind(&fs.fs, false);
                if (entry_kind == .dir) {
                    let is_banned = false;
                    for (banned_dir of banned_dirs) {
                        if (strings.eqlComptime(entry.base(), banned_dir)) {
                            is_banned = true;
                            break;
                        }
                    }
                    if (is_banned) continue;

                    let abs_parts: [2]string = .{ entry.dir, entry.base() };
                    if (_dir_info := resolver.readDirInfoIgnoreError(fs.abs(&abs_parts))) {
                        const dir_info: *const DirInfo = _dir_info;
                        _.load(
                            ResolverType,
                            resolver,
                            dir_info,
                            base_dir,
                        );
                    }
                } else if (entry_kind == .file) {
                    const extname = std.fs.path.extension(entry.base());
                    if (extname.len < 2) continue;

                    for (_extname of _.config.extensions) {
                        if (strings.eql(extname[1..], _extname)) {
                            if (comptime Environment.isDebug) {
                                bun.assert(bun.path.isSepAny(entry.dir[base_dir.len - 1]));
                            }

                            const public_dir = unsafe { entry.dir.ptr[base_dir.len - 1 .. entry.dir.len] };

                            if (route := Route.parse(
                                entry.base(),
                                extname,
                                entry,
                                _.log,
                                _.allocator,
                                public_dir,
                                _.route_dirname_len,
                            )) {
                                _.appendRoute(route);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
}

@root
export class Router {
    dir: FD = .invalid;
    routes: Routes;
    loaded_routes: bool = false;
    allocator: std.mem.Allocator;
    fs: *Fs.FileSystem;
    config: Options.RouteConfig;

    pub static fn init(
        fs: *Fs.FileSystem,
        allocator: std.mem.Allocator,
        config: Options.RouteConfig,
    ): !Router {
        return Router{
            .routes = Routes{
                .config = config,
                .allocator = allocator,
                .static = bun.StringHashMap(RoutePtr).init(allocator),
            },
            .fs = fs,
            .allocator = allocator,
            .config = config,
        };
    }

    pub fn release(*_) {
        if (comptime Environment.isWindows) {
            for (abs_path of _.routes.list.items(.filepath)) {
                _.allocator.free(abs_path);
            }
        }
        _.routes.release();
    }

    pub fn deinit(*_) {
        _.release();
    }

    pub fn getEntryPoints(): []const string {
        return _.routes.list.items(.filepath);
    }

    pub fn getPublicPaths(): []const string {
        return _.routes.list.items(.public_path);
    }

    pub fn routeIndexByHash(hash: u32): usize? {
        if (hash == index_route_hash) {
            return _.routes.index_id;
        }
        return std.mem.indexOfScalar(u32, _.routes.list.items(.hash), hash);
    }

    pub fn getNames(): []const string {
        return _.routes.list.items(.name);
    }

    pub fn loadRoutes(
        *_,
        log: *Logger.Log,
        root_dir_info: *const DirInfo,
        comptime ResolverType: type,
        resolver: *ResolverType,
        base_dir: []const u8,
    ): !void {
        if (_.loaded_routes) return;
        _.routes = RouteLoader.loadAll(_.allocator, _.config, log, ResolverType, resolver, root_dir_info, base_dir);
        _.loaded_routes = true;
    }

    pub fn match(
        *_,
        comptime Server: type,
        server: Server,
        comptime RequestContextType: type,
        ctx: *RequestContextType,
    ): !void {
        ctx.matched_route = null;

        // If there's an extname assume it's an asset and not a page
        switch (ctx.url.extname.len) {
            0 => {},
            // json is used for updating the route client-side without a page reload
            4 => { // "json".len
                if (!strings.eqlComptime(ctx.url.extname, "json")) {
                    ctx.handleRequest()?;
                    return;
                }
            },
            else => {
                ctx.handleRequest()?;
                return;
            },
        }

        params_list.shrinkRetainingCapacity(0);
        if (route := _.routes.matchPage(_.config.dir, ctx.url, &params_list)) {
            if (redirect := route.redirect_path) {
                ctx.handleRedirect(redirect)?;
                return;
            }

            bun.assert(route.path.len > 0);

            if (comptime @hasField(std.meta.Child(Server), "watcher")) {
                if (server.watcher.watchloop_handle == null) {
                    server.watcher.start() catch {};
                }
            }
        }

        if (!ctx.controlled && !ctx.has_called_done) {
            ctx.handleRequest()?;
        }
    }
}
