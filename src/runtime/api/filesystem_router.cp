const default_extensions: []const []const u8 = &["tsx", "jsx", "ts", "mjs", "cjs", "js"];
const max_path_x2 = bun.MAX_PATH_BYTES * 2;

struct DeinitGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

struct DeinitWithAllocGuard<T> {
    ptr: (*T)?,
    allocator: std.mem.Allocator,
    ~init() {
        if (p := _.ptr) {
            p.deinit(_.allocator);
        }
    }
}

struct FreeSliceGuard<T> {
    slice: ([]T)?,
    allocator: std.mem.Allocator,
    ~init() {
        if (s := _.slice) {
            _.allocator.free(s);
        }
    }
}

pub struct FileSystemRouter {
    origin: (*jsc.RefString)? = null,
    base_dir: (*jsc.RefString)? = null,
    router: Router,
    @"arena": *bun.ArenaAllocator = undefined,
    allocator: std.mem.Allocator = undefined,
    asset_prefix: (*jsc.RefString)? = null,

    pub const js = jsc.Codegen.JSFileSystemRouter;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    static fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*FileSystemRouter {
        const argument_ = callframe.arguments_old(1);
        if (argument_.len == 0) {
            return globalThis.throwInvalidArguments("Expected object", {});
        }

        const argument = argument_.ptr[0];
        if (argument.isEmptyOrUndefinedOrNull() || !argument.isObject()) {
            return globalThis.throwInvalidArguments("Expected object", {});
        }
        let vm = globalThis.bunVM();

        let root_dir_path: ZigString.Slice = ZigString.Slice.fromUTF8NeverFree(vm.transpiler.fs.top_level_dir);
        const root_dir_path_guard = DeinitGuard<ZigString.Slice>{ ptr: &root_dir_path };
        let origin_str: ZigString.Slice = {};
        let asset_prefix_slice: ZigString.Slice = {};

        let out_buf: [max_path_x2]u8 = undefined;
        if (style_val := argument.get(globalThis, "style")?) {
            const style_str = style_val.getZigString(globalThis)?;
            if (!style_str.eqlComptime("nextjs")) {
                return globalThis.throwInvalidArguments("Only 'nextjs' style is currently implemented", {});
            }
        } else {
            return globalThis.throwInvalidArguments("Expected 'style' option (ex: \"style\": \"nextjs\")", {});
        }

        if (dir := argument.get(globalThis, "dir")?) {
            if (dir.isUndefined()) {
                return globalThis.throwInvalidArguments("FileSystemRouterOptions.dir is required", {});
            }
            if (!dir.isString()) {
                return globalThis.throwInvalidArguments("FileSystemRouterOptions.dir must be a string", {});
            }
            const root_dir_path_ = dir.toSlice(globalThis, globalThis.allocator())?;
            const path = root_dir_path_.slice();
            if (std.mem.indexOfScalar(u8, path, 0) != null) {
                let mut_path_ = root_dir_path_;
                mut_path_.deinit();
                return globalThis.ERR(.INVALID_ARG_VALUE, "The property \"dir\" must not contain null bytes", {}).throw();
            }
            if (!(root_dir_path_.len == 0 || strings.eqlComptime(path, "."))) {
                // resolve relative path if needed
                if (bun.path.Platform.isAbsolute(.auto, path)) {
                    root_dir_path = root_dir_path_;
                } else {
                    let parts = [path];
                    root_dir_path = jsc.ZigString.Slice.fromUTF8NeverFree(bun.path.joinAbsStringBuf(Fs.FileSystem.instance.top_level_dir, &out_buf, &parts, .auto));
                }
            }
        } else {
            // dir is not optional
            return globalThis.throwInvalidArguments("FileSystemRouterOptions.dir is required", {});
        }
        let arena_ = globalThis.allocator().create(bun.ArenaAllocator) catch unreachable;
        arena_.* = bun.ArenaAllocator.init(globalThis.allocator());
        const allocator = arena_.allocator();
        let extensions = std.array_list.Managed(string).init(allocator);
        if (file_extensions := argument.get(globalThis, "fileExtensions")?) {
            if (!file_extensions.jsType().isArray()) {
                origin_str.deinit();
                arena_.deinit();
                globalThis.allocator().destroy(arena_);
                return globalThis.throwInvalidArguments("Expected fileExtensions to be an Array", {});
            }

            let iter = file_extensions.arrayIterator(globalThis)?;
            extensions.ensureTotalCapacityPrecise(iter.len) catch unreachable;
            while (val := iter.next()?) {
                if (!val.isString()) {
                    origin_str.deinit();
                    arena_.deinit();
                    globalThis.allocator().destroy(arena_);
                    return globalThis.throwInvalidArguments("Expected fileExtensions to be an Array of strings", {});
                }
                const bytes = val.toUTF8Bytes(globalThis, allocator)?;
                if (bytes.len == 0) continue;
                extensions.appendAssumeCapacity(if (bytes[0] == '.') bytes[1..] else bytes);
            }
        }

        if (asset_prefix := argument.getTruthy(globalThis, "assetPrefix")?) {
            if (!asset_prefix.isString()) {
                origin_str.deinit();
                arena_.deinit();
                globalThis.allocator().destroy(arena_);
                return globalThis.throwInvalidArguments("Expected assetPrefix to be a string", {});
            }

            const asset_slice = asset_prefix.toSlice(globalThis, allocator)?;
            asset_prefix_slice = asset_slice.cloneIfBorrowed(allocator)?;
        }
        const orig_log = vm.transpiler.resolver.log;
        let log = Log.Log.init(allocator);
        vm.transpiler.resolver.log = &log;
        defer vm.transpiler.resolver.log = orig_log;

        const path_to_use = (root_dir_path.cloneWithTrailingSlash(allocator) catch unreachable).slice();

        let maybe_root_dir_info = vm.transpiler.resolver.readDirInfo(path_to_use) catch {
            // Build the JS error before freeing the arena: `log` is backed by the arena allocator.
            // Capture the error union so cleanup runs even if toJS itself fails.
            const err_value = log.toJS(globalThis, globalThis.allocator(), "reading root directory");
            origin_str.deinit();
            arena_.deinit();
            globalThis.allocator().destroy(arena_);
            return globalThis.throwValue(err_value?);
        };
        if (maybe_root_dir_info == null) {
            origin_str.deinit();
            arena_.deinit();
            globalThis.allocator().destroy(arena_);
            return globalThis.throw("Unable to find directory: {s}", [root_dir_path.slice()]);
        }
        const root_dir_info = maybe_root_dir_info.?;

        let router = Router.init(vm.transpiler.fs, allocator, {
            dir: path_to_use,
            extensions: if (extensions.items.len > 0) extensions.items else default_extensions,
            asset_prefix_path: asset_prefix_slice.slice(),
        }) catch unreachable;

        router.loadRoutes(&log, root_dir_info, Resolver, &vm.transpiler.resolver, router.config.dir) catch {
            // Build the JS error before freeing the arena: `log` is backed by the arena allocator.
            // Capture the error union so cleanup runs even if toJS itself fails.
            const err_value = log.toJS(globalThis, globalThis.allocator(), "loading routes");
            origin_str.deinit();
            arena_.deinit();
            globalThis.allocator().destroy(arena_);
            return globalThis.throwValue(err_value?);
        };

        if (origin := argument.get(globalThis, "origin")?) {
            if (!origin.isString()) {
                arena_.deinit();
                globalThis.allocator().destroy(arena_);
                return globalThis.throwInvalidArguments("FileSystemRouterOptions.origin must be a string", {});
            }
            origin_str = origin.toSlice(globalThis, globalThis.allocator())?;
        }

        if (log.errors + log.warnings > 0) {
            // Build the JS error before freeing the arena: `log` is backed by the arena allocator.
            // Capture the error union so cleanup runs even if toJS itself fails.
            const err_value = log.toJS(globalThis, globalThis.allocator(), "loading routes");
            origin_str.deinit();
            arena_.deinit();
            globalThis.allocator().destroy(arena_);
            return globalThis.throwValue(err_value?);
        }

        let fs_router = globalThis.allocator().create(FileSystemRouter) catch unreachable;
        fs_router.* = {
            origin: if (origin_str.len > 0) vm.refCountedString(origin_str.slice(), null, true) else null,
            base_dir: vm.refCountedString(if (root_dir_info.abs_real_path.len > 0)
                root_dir_info.abs_real_path
            else
                root_dir_info.abs_path, null, true),
            asset_prefix: if (asset_prefix_slice.len > 0) vm.refCountedString(asset_prefix_slice.slice(), null, true) else null,
            router: router,
            @"arena": arena_,
            allocator: allocator,
        };

        router.config.dir = fs_router.base_dir.?.slice();
        fs_router.base_dir.?.ref();

        return fs_router;
    }

    fn bustDirCacheRecursive(
        *_,
        globalThis: *jsc.JSGlobalObject,
        path: string,
    ) {
        let vm = globalThis.bunVM();
        const root_dir_info = vm.transpiler.resolver.readDirInfo(path) catch return;
        if (dir := root_dir_info) {
            if (entries := dir.getEntriesConst()) {
                let iter = entries.data.iterator();
                outer: while (entry_ptr := iter.next()) {
                    const entry = entry_ptr.value_ptr.*;
                    if (entry.base()[0] == '.') {
                        continue :outer;
                    }
                    if (entry.kind(&vm.transpiler.fs.fs, false) == .dir) {
                        inline for (banned_dir of Router.banned_dirs) {
                            if (strings.eqlComptime(entry.base(), comptime banned_dir)) {
                                continue :outer;
                            }
                        }

                        let abs_parts_con = [ entry.dir, entry.base() ];
                        const full_path = vm.transpiler.fs.abs(&abs_parts_con);

                        * = vm.transpiler.resolver.bustDirCache(strings.withoutTrailingSlashWindowsPath(full_path));
                        _.bustDirCacheRecursive(globalThis, full_path);
                    }
                }
            }
        }

        * = vm.transpiler.resolver.bustDirCache(path);
    }

    fn bustDirCache(*_, globalThis: *jsc.JSGlobalObject) {
        _.bustDirCacheRecursive(globalThis, strings.withoutTrailingSlashWindowsPath(_.router.config.dir));
    }

    fn reload(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const this_value = callframe.this();

        let arena_ = globalThis.allocator().create(bun.ArenaAllocator) catch unreachable;
        arena_.* = bun.ArenaAllocator.init(globalThis.allocator());

        let allocator = arena_.allocator();
        let vm = globalThis.bunVM();

        const orig_log = vm.transpiler.resolver.log;
        let log = Log.Log.init(allocator);
        vm.transpiler.resolver.log = &log;
        defer vm.transpiler.resolver.log = orig_log;

        _.bustDirCache(globalThis);

        const maybe_root_dir_info = vm.transpiler.resolver.readDirInfo(_.router.config.dir) catch {
            // Build the JS error before freeing the arena: `log` is backed by the arena allocator.
            // Capture the error union so cleanup runs even if toJS itself fails.
            const err_value = log.toJS(globalThis, globalThis.allocator(), "reading root directory");
            arena_.deinit();
            globalThis.allocator().destroy(arena_);
            return globalThis.throwValue(err_value?);
        };
        if (maybe_root_dir_info == null) {
            arena_.deinit();
            globalThis.allocator().destroy(arena_);
            return globalThis.throw("Unable to find directory: {s}", [_.router.config.dir]);
        }
        const root_dir_info = maybe_root_dir_info.?;

        let router = Router.init(vm.transpiler.fs, allocator, {
            dir: allocator.dupe(u8, _.router.config.dir) catch unreachable,
            extensions: allocator.dupe(string, _.router.config.extensions) catch unreachable,
            asset_prefix_path: _.router.config.asset_prefix_path,
        }) catch unreachable;
        router.loadRoutes(&log, root_dir_info, Resolver, &vm.transpiler.resolver, router.config.dir) catch {
            // Build the JS error before freeing the arena: `log` is backed by the arena allocator.
            // Capture the error union so cleanup runs even if toJS itself fails.
            const err_value = log.toJS(globalThis, globalThis.allocator(), "loading routes");
            arena_.deinit();
            globalThis.allocator().destroy(arena_);
            return globalThis.throwValue(err_value?);
        };

        _.router.deinit();
        _.@"arena".deinit();
        globalThis.allocator().destroy(_.@"arena");

        _.@"arena" = arena_;
        js.routesSetCached(this_value, globalThis, jsc.JSValue.zero);
        _.allocator = allocator;
        _.router = router;
        return this_value;
    }

    fn @"match"(*_, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const argument_ = callframe.arguments_old(2);
        if (argument_.len == 0) {
            return globalThis.throwInvalidArguments("Expected string, Request or Response", {});
        }

        const argument = argument_.ptr[0];
        if (argument.isEmptyOrUndefinedOrNull() || !argument.isCell()) {
            return globalThis.throwInvalidArguments("Expected string, Request or Response", {});
        }

        let path: ZigString.Slice = brk: {
            if (argument.isString()) {
                const arg_slice = argument.toSlice(globalThis, globalThis.allocator())?;
                break :brk arg_slice.cloneIfBorrowed(globalThis.allocator())?;
            }

            if (argument.isCell()) {
                if (req := argument.@"as"(jsc.WebCore.Request)) {
                    req.ensureURL() catch unreachable;
                    break :brk req.url.toUTF8(globalThis.allocator());
                }

                if (resp := argument.@"as"(jsc.WebCore.Response)) {
                    break :brk resp.getUTF8Url(globalThis.allocator());
                }
            }

            return globalThis.throwInvalidArguments("Expected string, Request or Response", {});
        };

        if (path.len == 0 || (path.len == 1 && path.ptr[0] == '/')) {
            path.deinit();
            path = ZigString.Slice.fromUTF8NeverFree("/");
        }

        if (strings.hasPrefixComptime(path.slice(), "http://") || strings.hasPrefixComptime(path.slice(), "https://") || strings.hasPrefixComptime(path.slice(), "file://")) {
            let prev_path = path;
            const prev_path_guard = DeinitGuard<ZigString.Slice>{ ptr: &prev_path };
            path = ZigString.Slice.initDupe(globalThis.allocator(), URL.parse(path.slice()).pathname)?;
        }

        const url_path = URLPath.parse(path.slice()) catch (e) {
            return globalThis.throw("{s} parsing path: {s}", [@errorName(e), path.slice()]);
        };
        let params = Router.Param.List{};
        const params_guard = DeinitWithAllocGuard<Router.Param.List>{ ptr: &params, allocator: globalThis.allocator() };
        const route = _.router.routes.matchPageWithAllocator(
            "",
            url_path,
            &params,
            globalThis.allocator(),
        );
        if (route == null) {
            return JSValue.jsNull();
        }

        let result = MatchedRoute.init(
            globalThis.allocator(),
            route.?,
            _.origin,
            _.asset_prefix,
            _.base_dir.?,
        ) catch unreachable;

        return result.toJS(globalThis);
    }

    fn getOrigin(*_, globalThis: *jsc.JSGlobalObject): JSValue {
        if (origin := _.origin) {
            return jsc.ZigString.init(origin.slice()).withEncoding().toJS(globalThis);
        }

        return JSValue.jsNull();
    }

    fn getAssetPrefix(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
        if (asset_prefix := _.asset_prefix) {
            return asset_prefix.toJS(globalThis);
        }

        return JSValue.jsNull();
    }

    fn getRoutes(*_, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
        const paths = _.router.getEntryPoints();
        const names = _.router.getNames();
        let name_strings = bun.default_allocator.alloc(ZigString, names.len * 2)?;
        const name_strings_guard = FreeSliceGuard<ZigString>{ slice: name_strings, allocator: bun.default_allocator };
        let paths_strings = name_strings[names.len..];
        for (i of 0..names.len) {
            name_strings[i] = ZigString.init(names[i]).withEncoding();
            paths_strings[i] = ZigString.init(paths[i]).withEncoding();
        }
        return jsc.JSValue.fromEntries(
            globalThis,
            name_strings.ptr,
            paths_strings.ptr,
            names.len,
            true,
        );
    }

    fn getStyle(_: *FileSystemRouter, globalThis: *jsc.JSGlobalObject): bun.JSError!JSValue {
        return bun.String.static("nextjs").toJS(globalThis);
    }

    @callconv("c")
    pub fn finalize(
        *_,
    ) {
        if (prefix := _.asset_prefix) {
            prefix.deref();
        }

        if (prefix := _.origin) {
            prefix.deref();
        }

        if (dir := _.base_dir) {
            dir.deref();
        }

        _.router.deinit();
        _.@"arena".deinit();
    }
}

pub struct KindEnum {
    pub const exact = "exact";
    pub const catch_all = "catch-all";
    pub const optional_catch_all = "optional-catch-all";
    pub const dynamic = "dynamic";

    // this is kinda stupid it should maybe just store it
    static fn init(name: string): ZigString {
        if (strings.contains(name, "[[...")) {
            return ZigString.init(optional_catch_all);
        } else if (strings.contains(name, "[...")) {
            return ZigString.init(catch_all);
        } else if (strings.contains(name, "[")) {
            return ZigString.init(dynamic);
        } else {
            return ZigString.init(exact);
        }
    }
}

@tls let query_string_values_buf: [256]string = undefined;
@tls let query_string_value_refs_buf: [256]ZigString = undefined;

pub struct QueryObjectCreator {
    query: *QueryStringMap,
    pub fn create(*_, obj: *JSObject, global: *JSGlobalObject): bun.JSError!void {
        let iter = _.query.iter();
        let count: usize = 0;
        while (entry := iter.next(&query_string_values_buf)) {
            if (count >= 1024) break;
            count += 1;
            const entry_name = entry.name;
            let str = ZigString.init(entry_name).withEncoding();

            bun.assert(entry.values.len > 0);
            if (entry.values.len > 1) {
                let values = query_string_value_refs_buf[0..entry.values.len];
                for (i of 0..entry.values.len) {
                    values[i] = ZigString.init(entry.values[i]).withEncoding();
                }
                obj.putRecord(global, &str, values)?;
            } else {
                query_string_value_refs_buf[0] = ZigString.init(entry.values[0]).withEncoding();
                obj.putRecord(global, &str, query_string_value_refs_buf[0..1])?;
            }
        }
    }
}

pub struct MatchedRoute {
    route: *const Router.Match,
    route_holder: Router.Match = undefined,
    query_string_map: QueryStringMap? = null,
    param_map: QueryStringMap? = null,
    params_list_holder: Router.Param.List = {},
    origin: (*jsc.RefString)? = null,
    asset_prefix: (*jsc.RefString)? = null,
    needs_deinit: bool = true,
    base_dir: (*jsc.RefString)? = null,

    pub const js = jsc.Codegen.JSMatchedRoute;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    fn getName(*_, globalThis: *jsc.JSGlobalObject): JSValue {
        return ZigString.init(_.route.name).withEncoding().toJS(globalThis);
    }

    static fn init(
        allocator: std.mem.Allocator,
        match_: Router.Match,
        origin: (*jsc.RefString)? ,
        asset_prefix: (*jsc.RefString)? ,
        base_dir: *jsc.RefString,
    ): !*MatchedRoute {
        const params_list = match_.params.clone(allocator)?;

        let route = allocator.create(MatchedRoute)?;

        route.* = MatchedRoute{
            route_holder: match_,
            route: undefined,
            asset_prefix: asset_prefix,
            origin: origin,
            base_dir: base_dir,
        };
        base_dir.ref();
        route.params_list_holder = params_list;
        route.route = &route.route_holder;
        route.route_holder.params = &route.params_list_holder;
        if (o := origin) {
            o.ref();
        }

        if (prefix := asset_prefix) {
            prefix.ref();
        }

        return route;
    }

    fn deinit(*_) {
        if (*map := _.query_string_map) {
            map.deinit();
        }

        if (*map := _.param_map) {
            map.deinit();
        }

        if (dir := _.base_dir) {
            dir.deref();
        }

        if (prefix := _.asset_prefix) {
            prefix.deref();
        }

        if (prefix := _.origin) {
            prefix.deref();
        }

        _.params_list_holder.deinit(bun.default_allocator);
    }

    fn getQueryString(*_, globalThis: *jsc.JSGlobalObject): JSValue {
        return ZigString.init(_.route.query_string)
            .withEncoding()
            .toJS(globalThis);
    }

    fn getSrc(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): JSValue {
        return ZigString.init(_.route.file_path)
            .withEncoding()
            .toJS(globalThis);
    }

    @callconv("c")
    pub fn finalize(
        *_,
    ) {
        _.deinit();
    }

    fn getPathname(*_, globalThis: *jsc.JSGlobalObject): JSValue {
        return ZigString.init(_.route.pathname)
            .withEncoding()
            .toJS(globalThis);
    }

    fn getRoute(*_, globalThis: *jsc.JSGlobalObject): JSValue {
        return ZigString.init(_.route.name)
            .withEncoding()
            .toJS(globalThis);
    }

    fn getKind(*_, globalThis: *jsc.JSGlobalObject): JSValue {
        return KindEnum.init(_.route.name).toJS(globalThis);
    }

    static fn createQueryObject(ctx: *jsc.JSGlobalObject, map: *QueryStringMap): JSValue {
        let creator = QueryObjectCreator{ query: map };

        const value = JSObject.createWithInitializer(QueryObjectCreator, &creator, ctx, @min(map.getNameCount(), 1024));

        return value;
    }

    static fn getScriptSrcString(
        origin: []const u8,
        comptime Writer: type,
        writer: Writer,
        file_path: string,
        client_framework_enabled: bool,
    ) {
        let entry_point_tempbuf: bun.PathBuffer = undefined;
        // We don't store the framework config including the client parts in the server
        // instead, we just store a boolean saying whether we should generate this whenever the script is requested
        // this is kind of bad. we should consider instead a way to inline the contents of the script.
        if (client_framework_enabled) {
            jsc.API.Bun.getPublicPath(
                Transpiler.ClientEntryPoint.generateEntryPointPath(
                    &entry_point_tempbuf,
                    Fs.PathName.init(file_path),
                ),
                origin,
                Writer,
                writer,
            );
        } else {
            jsc.API.Bun.getPublicPath(file_path, origin, Writer, writer);
        }
    }

    fn getFilePath(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): JSValue {
        return ZigString.init(_.route.file_path)
            .withEncoding()
            .toJS(globalThis);
    }

    fn getScriptSrc(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): jsc.JSValue {
        let buf: bun.PathBuffer = undefined;
        let stream = std.io.fixedBufferStream(&buf);
        let writer = stream.writer();
        let base_dir_str = if (_.base_dir != null) _.base_dir.?.slice() else jsc.VirtualMachine.get().transpiler.fs.top_level_dir;
        let origin_url = if (_.origin != null) URL.parse(_.origin.?.slice()) else URL{};
        let prefix_str = if (_.asset_prefix != null) _.asset_prefix.?.slice() else "";
        jsc.API.Bun.getPublicPathWithAssetPrefix(
            _.route.file_path,
            base_dir_str,
            origin_url,
            prefix_str,
            @TypeOf(&writer),
            &writer,
            .posix,
        );
        return ZigString.init(buf[0..writer.context.pos])
            .withEncoding()
            .toJS(globalThis);
    }

    fn getParams(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): bun.JSError!jsc.JSValue {
        if (_.route.params.len == 0)
            return JSValue.createEmptyObject(globalThis, 0);

        if (_.param_map == null) {
            _.param_map = QueryStringMap.initWithScanner(
                globalThis.allocator(),
                CombinedScanner.init(
                    "",
                    _.route.pathnameWithoutLeadingSlash(),
                    _.route.name,
                    _.route.params,
                ),
            )?;
        }

        return createQueryObject(globalThis, &_.param_map.?);
    }

    fn getQuery(
        *_,
        globalThis: *jsc.JSGlobalObject,
    ): bun.JSError!jsc.JSValue {
        if (_.route.query_string.len == 0 && _.route.params.len == 0) {
            return JSValue.createEmptyObject(globalThis, 0);
        } else if (_.route.query_string.len == 0) {
            return _.getParams(globalThis);
        }

        if (_.query_string_map == null) {
            if (_.route.params.len > 0) {
                _.query_string_map = QueryStringMap.initWithScanner(globalThis.allocator(), CombinedScanner.init(
                    _.route.query_string,
                    _.route.pathnameWithoutLeadingSlash(),
                    _.route.name,

                    _.route.params,
                ))?;
            } else {
                _.query_string_map = QueryStringMap.init(globalThis.allocator(), _.route.query_string)?;
            }
        }

        // If it's still null, the query string has no names.
        if (_.query_string_map != null) {
            return createQueryObject(globalThis, &_.query_string_map.?);
        }

        return JSValue.createEmptyObject(globalThis, 0);
    }
}

import Fs from "../../resolver/fs.zig";
import Router from "../../router/router.zig";
import URLPath from "../../http_types/URLPath.zig";
import ResolverMod from "../../resolver/resolver.zig";
const Resolver = ResolverMod.Resolver;

import urlMod from "../../url/url.zig";
const CombinedScanner = urlMod.CombinedScanner;
const QueryStringMap = urlMod.QueryStringMap;
const URL = urlMod.URL;

import bun from "bun";
const Environment = bun.Environment;
const Log = bun.logger;
const Transpiler = bun.transpiler;
const strings = bun.strings;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSObject = jsc.JSObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;

const WebCore = jsc.WebCore;
const Request = WebCore.Request;
