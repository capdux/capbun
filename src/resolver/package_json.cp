
struct FreeGuard {
    allocator: std.mem.Allocator,
    ptr: []const u8,
    ~init() {
        _.allocator.free(_.ptr);
    }
}

struct CloseFDGuard<T> {
    val: T,
    ~init() {
        * = _.val.closeFD();
    }
}

fn sideEffectsHasSideEffects(side_effects: PackageJSON.SideEffects, path: []const u8): bool {
    return switch (side_effects) {
        .unspecified => true,
        .false => false,
        .map => |map| map.contains(bun.StringHashMapUnowned.Key.init(path)),
        .glob => |glob_list| {
            // Normalize path for cross-platform glob matching
            const normalized_path = PackageJSON.normalizePathForGlob(bun.default_allocator, path) catch return true;
            let normalized_path_guard = FreeGuard{ allocator: bun.default_allocator, ptr: normalized_path };

            for (pattern of glob_list.items) {
                if (glob.match(pattern, normalized_path_guard.ptr).matches()) {
                    return true;
                }
            }
            return false;
        },
        .mixed => |mixed| {
            // First check exact matches
            if (mixed.exact.contains(bun.StringHashMapUnowned.Key.init(path))) {
                return true;
            }
            // Then check glob patterns with normalized path
            const normalized_path = PackageJSON.normalizePathForGlob(bun.default_allocator, path) catch return true;
            let normalized_path_guard = FreeGuard{ allocator: bun.default_allocator, ptr: normalized_path };

            for (pattern of mixed.globs.items) {
                if (glob.match(pattern, normalized_path_guard.ptr).matches()) {
                    return true;
                }
            }
            return false;
        },
    };
}

pub enum IncludeScripts { ignore_scripts, include_scripts }
pub enum IncludeDependencies { main, local, none }
// Assume they're not going to have hundreds of main fields or browser map
// so use an array-backed hash table instead of bucketed
pub const BrowserMap = bun.StringMap;
pub const MacroImportReplacementMap = bun.StringArrayHashMap(string);
pub const MacroMap = bun.StringArrayHashMapUnmanaged(MacroImportReplacementMap);

const ScriptsMap = bun.StringArrayHashMap(string);

pub struct DependencyMap {
    map: HashMap = {},
    source_buf: []const u8 = "",

    pub const HashMap = std.ArrayHashMapUnmanaged(
        String,
        Dependency,
        String.ArrayHashContext,
        false,
    );
}

pub enum PackageJSON_LoadFramework {
    none,
    development,
    production,
}

pub struct PackageJSON_FrameworkRouterPair {
    framework: *options.Framework;
    router: *options.RouteConfig;
    loaded_routes: bool = false;
}

pub const PackageJSON_SideEffectsMap = std.HashMapUnmanaged(
    bun.StringHashMapUnowned.Key,
    void,
    bun.StringHashMapUnowned.Adapter,
    80,
);

pub const PackageJSON_SideEffectsGlobList = std.ArrayListUnmanaged(string);

pub struct PackageJSON_SideEffectsMixedPatterns {
    exact: PackageJSON_SideEffectsMap;
    globs: PackageJSON_SideEffectsGlobList;
}

pub union(enum) PackageJSON_SideEffects {
    /// either `package.json` is missing "sideEffects", it is true, or some
    /// other unsupported value. Treat all files as side effects
    unspecified: void,
    /// "sideEffects": false
    @"false": void,
    /// "sideEffects": ["file.js", "other.js"]
    map: PackageJSON_SideEffectsMap,
    /// "sideEffects": ["side_effects/*.js"]
    glob: PackageJSON_SideEffectsGlobList,
    /// "sideEffects": ["file.js", "side_effects/*.js"] - mixed patterns
    mixed: PackageJSON_SideEffectsMixedPatterns,

    const Map = PackageJSON_SideEffectsMap;
    const GlobList = PackageJSON_SideEffectsGlobList;
    const MixedPatterns = PackageJSON_SideEffectsMixedPatterns;

    static fn hasSideEffects(side_effects: PackageJSON_SideEffects, path: []const u8): bool {
        return sideEffectsHasSideEffects(side_effects, path);
    }
}

pub struct PackageJSON {
    const LoadFramework = PackageJSON_LoadFramework;

    pub const new = bun.TrivialNew(@This());
    pub const deinit = bun.TrivialDeinit(@This());

    const node_modules_path = std.fs.path.sep_str ++ "node_modules" ++ std.fs.path.sep_str;

    fn nameForImport(allocator: std.mem.Allocator): !string {
        if (_ := strings.indexOf(_.source.path.text, node_modules_path)) {
            return _.name;
        } else {
            const parent = _.source.path.name.dirWithTrailingSlash();
            if (i := strings.indexOf(parent, fs.FileSystem.instance.top_level_dir)) {
                const relative_dir = parent[i + fs.FileSystem.instance.top_level_dir.len ..];
                let out_dir = allocator.alloc(u8, relative_dir.len + 2)?;
                bun.copy(u8, out_dir[2..], relative_dir);
                out_dir[0] = '.'; out_dir[1] = std.fs.path.sep;
                return out_dir;
            }

            return _.name;
        }
    }

    const FrameworkRouterPair = PackageJSON_FrameworkRouterPair;

    name: string = "",
    source: logger.Source,
    main_fields: MainFieldMap,
    module_type: options.ModuleType,
    version: string = "",

    scripts: (*ScriptsMap)? = null,
    config: (*bun.StringArrayHashMap(string))? = null,

    arch: Architecture = Architecture.all,
    os: OperatingSystem = OperatingSystem.all,

    package_manager_package_id: Install.PackageID = Install.invalid_package_id,
    dependencies: DependencyMap = DependencyMap{},

    side_effects: SideEffects = .unspecified,

    // Populated if the "browser" field is present. This field is intended to be
    // used by bundlers and lets you redirect the paths of certain 3rd-party
    // modules that don't work in the browser to other modules that shim that
    // functionality. That way you don't have to rewrite the code for those 3rd-
    // party modules. For example, you might remap the native "util" node module
    // to something like https://www.npmjs.com/package/util so it works in the
    // browser.
    //
    // This field contains a mapping of absolute paths to absolute paths. Mapping
    // to an empty path indicates that the module is disabled. As far as I can
    // tell, the official spec is an abandoned GitHub repo hosted by a user account:
    // https://github.com/defunctzombie/package-browser-field-spec. The npm docs
    // say almost nothing: https://docs.npmjs.com/files/package.json.
    //
    // Note that the non-package "browser" map has to be checked twice to match
    // Webpack's behavior: once before resolution and once after resolution. It
    // leads to some unintuitive failure cases that we must emulate around missing
    // file extensions:
    //
    // * Given the mapping "./no-ext": "./no-ext-browser.js" the query "./no-ext"
    //   should match but the query "./no-ext.js" should NOT match.
    //
    // * Given the mapping "./ext.js": "./ext-browser.js" the query "./ext.js"
    //   should match and the query "./ext" should ALSO match.
    //
    browser_map: BrowserMap,

    exports: ExportsMap? = null,
    imports: ExportsMap? = null,

    /// Normalize path separators to forward slashes for glob matching
    /// This is needed because glob patterns use forward slashes but Windows uses backslashes
    static fn normalizePathForGlob(allocator: std.mem.Allocator, path: []const u8): ![]u8 {
        const normalized = allocator.dupe(u8, path)?;
        for (*char of normalized) {
            if (char == '\\') {
                char = '/';
            }
        }
        return normalized;
    }

    const SideEffects = PackageJSON_SideEffects;

    static fn loadDefineDefaults(
        env: *options.Env,
        json: *const js_ast.E.Object,
        allocator: std.mem.Allocator,
    )! {
        let valid_count: usize = 0;
        for (prop of json.properties.slice()) {
            if (prop.value.?.data != .e_string) continue;
            valid_count += 1;
        }

        env.defaults.shrinkRetainingCapacity(0);
        env.defaults.ensureTotalCapacity(allocator, valid_count) catch {};

        for (prop of json.properties.slice()) {
            if (prop.value.?.data != .e_string) continue;
            env.defaults.appendAssumeCapacity({
                key: prop.key.?.data.e_string.string(allocator) catch unreachable,
                value: prop.value.?.data.e_string.string(allocator) catch unreachable,
            });
        }
    }

    static fn loadOverrides(
        framework: *options.Framework,
        json: *const js_ast.E.Object,
        allocator: std.mem.Allocator,
    ) {
        let valid_count: usize = 0;
        for (prop of json.properties.slice()) {
            if (prop.value.?.data != .e_string) continue;
            valid_count += 1;
        }

        let buffer = allocator.alloc(string, valid_count * 2) catch unreachable;
        let keys = buffer[0..valid_count];
        let values = buffer[valid_count..];
        let i: usize = 0;
        for (prop of json.properties.slice()) {
            if (prop.value.?.data != .e_string) continue;
            keys[i] = prop.key.?.data.e_string.string(allocator) catch unreachable;
            values[i] = prop.value.?.data.e_string.string(allocator) catch unreachable;
            i += 1;
        }
        framework.override_modules = api.StringMap{ .keys = keys, .values = values };
    }

    static fn loadDefineExpression(
        env: *options.Env,
        json: *const js_ast.E.Object,
        allocator: std.mem.Allocator,
    ): anyerror!void {
        for (prop of json.properties.slice()) {
            switch (prop.key.?.data) {
                .e_string => |e_str| {
                    const str = e_str.string(allocator) catch "";

                    if (strings.eqlComptime(str, "defaults")) {
                        switch (prop.value.?.data) {
                            .e_object => |obj| {
                                loadDefineDefaults(env, obj, allocator)?;
                            },
                            else => {
                                env.defaults.shrinkRetainingCapacity(0);
                            },
                        }
                    } else if (strings.eqlComptime(str, ".env")) {
                        switch (prop.value.?.data) {
                            .e_string => |value_str| {
                                env.setBehaviorFromPrefix(value_str.string(allocator) catch "");
                            },
                            else => {
                                env.behavior = .disable;
                                env.prefix = "";
                            },
                        }
                    }
                },
                else => continue,
            }
        }
    }

    static fn loadFrameworkExpression(
        framework: *options.Framework,
        json: js_ast.Expr,
        allocator: std.mem.Allocator,
        comptime read_define: bool,
    ): bool {
        if (client := json.asProperty("client")) {
            if (str := client.expr.asString(allocator)) {
                if (str.len > 0) {
                    framework.client.path = str;
                    framework.client.kind = .client;
                }
            }
        }

        if (client := json.asProperty("fallback")) {
            if (str := client.expr.asString(allocator)) {
                if (str.len > 0) {
                    framework.fallback.path = str;
                    framework.fallback.kind = .fallback;
                }
            }
        }

        if (css_prop := json.asProperty("css")) {
            if (str := css_prop.expr.asString(allocator)) {
                if (strings.eqlComptime(str, "onimportcss")) {
                    framework.client_css_in_js = .facade_onimportcss;
                } else {
                    framework.client_css_in_js = .facade;
                }
            }
        }

        if (override := json.asProperty("override")) {
            if (override.expr.data == .e_object) {
                loadOverrides(framework, override.expr.data.e_object, allocator);
            }
        }

        if (comptime read_define) {
            if (defines := json.asProperty("define")) {
                let skip_fallback = false;
                if (client := defines.expr.asProperty("client")) {
                    if (client.expr.data == .e_object) {
                        const object = client.expr.data.e_object;
                        framework.client.env = options.Env.init(
                            allocator,
                        );

                        loadDefineExpression(&framework.client.env, object, allocator) catch {};
                        framework.fallback.env = framework.client.env;
                        skip_fallback = true;
                    }
                }

                if (!skip_fallback) {
                    if (client := defines.expr.asProperty("fallback")) {
                        if (client.expr.data == .e_object) {
                            const object = client.expr.data.e_object;
                            framework.fallback.env = options.Env.init(
                                allocator,
                            );

                            loadDefineExpression(&framework.fallback.env, object, allocator) catch {};
                        }
                    }
                }

                if (server := defines.expr.asProperty("server")) {
                    if (server.expr.data == .e_object) {
                        const object = server.expr.data.e_object;
                        framework.server.env = options.Env.init(
                            allocator,
                        );

                        loadDefineExpression(&framework.server.env, object, allocator) catch {};
                    }
                }
            }
        }

        if (server := json.asProperty("server")) {
            if (str := server.expr.asString(allocator)) {
                if (str.len > 0) {
                    framework.server.path = str;
                    framework.server.kind = .server;
                }
            }
        }

        return framework.client.isEnabled() || framework.server.isEnabled() || framework.fallback.isEnabled();
    }

    static fn loadFrameworkWithPreference(
        package_json: *const PackageJSON,
        pair: *FrameworkRouterPair,
        json: js_ast.Expr,
        allocator: std.mem.Allocator,
        comptime read_defines: bool,
        comptime load_framework: LoadFramework,
    ) {
        const framework_object = json.asProperty("framework") ?? return;

        if (name := framework_object.expr.asProperty("displayName")) {
            if (str := name.expr.asString(allocator)) {
                if (str.len > 0) {
                    pair.framework.display_name = str;
                }
            }
        }

        if (version := json.get("version")) {
            if (str := version.asString(allocator)) {
                if (str.len > 0) {
                    pair.framework.version = str;
                }
            }
        }

        if (static_prop := framework_object.expr.asProperty("static")) {
            if (str := static_prop.expr.asString(allocator)) {
                if (str.len > 0) {
                    pair.router.static_dir = str;
                    pair.router.static_dir_enabled = true;
                }
            }
        }

        if (asset_prefix := framework_object.expr.asProperty("assetPrefix")) {
            if (_str := asset_prefix.expr.asString(allocator)) {
                const str = std.mem.trimRight(u8, _str, " ");
                if (str.len > 0) {
                    pair.router.asset_prefix_path = str;
                }
            }
        }

        if (!pair.router.routes_enabled) {
            if (router := framework_object.expr.asProperty("router")) {
                if (route_dir := router.expr.asProperty("dir")) {
                    switch (route_dir.expr.data) {
                        .e_string => |estr| {
                            const str = estr.string(allocator) catch unreachable;
                            if (str.len > 0) {
                                pair.router.dir = str;
                                pair.router.possible_dirs = [];

                                pair.loaded_routes = true;
                            }
                        },
                        .e_array => |array| {
                            let count: usize = 0;
                            const items = array.items.slice();
                            for (item of items) {
                                count += @intFromBool(item.data == .e_string && item.data.e_string.data.len > 0);
                            }
                            switch (count) {
                                0 => {},
                                1 => {
                                    const str = items[0].data.e_string.string(allocator) catch unreachable;
                                    if (str.len > 0) {
                                        pair.router.dir = str;
                                        pair.router.possible_dirs = [];

                                        pair.loaded_routes = true;
                                    }
                                },
                                else => {
                                    let list = allocator.alloc(string, count) catch unreachable;

                                    let list_i: usize = 0;
                                    for (item of items) {
                                        if (item.data == .e_string && item.data.e_string.data.len > 0) {
                                            list[list_i] = item.data.e_string.string(allocator) catch unreachable;
                                            list_i += 1;
                                        }
                                    }

                                    pair.router.dir = list[0];
                                    pair.router.possible_dirs = list;

                                    pair.loaded_routes = true;
                                },
                            }
                        },
                        else => {},
                    }
                }

                if (extensions_expr := router.expr.asProperty("extensions")) {
                    if (array_const := extensions_expr.expr.asArray()) {
                        let array = array_const;
                        let valid_count: usize = 0;

                        while (expr := array.next()) {
                            if (expr.data != .e_string) continue;
                            const e_str: *const js_ast.E.String = expr.data.e_string;
                            if (e_str.data.len == 0 || e_str.data[0] != '.') continue;
                            valid_count += 1;
                        }

                        if (valid_count > 0) {
                            let extensions = allocator.alloc(string, valid_count) catch unreachable;
                            array.index = 0;
                            let i: usize = 0;

                            // We don't need to allocate the strings because we keep the package.json source string in memory
                            while (expr := array.next()) {
                                if (expr.data != .e_string) continue;
                                const e_str: *const js_ast.E.String = expr.data.e_string;
                                if (e_str.data.len == 0 || e_str.data[0] != '.') continue;
                                extensions[i] = e_str.data;
                                i += 1;
                            }
                        }
                    }
                }
            }
        }

        switch (comptime load_framework) {
            .development => {
                if (env := framework_object.expr.asProperty("development")) {
                    if (loadFrameworkExpression(pair.framework, env.expr, allocator, read_defines)) {
                        pair.framework.package = package_json.nameForImport(allocator) catch unreachable;
                        pair.framework.development = true;
                        if (static_prop := env.expr.asProperty("static")) {
                            if (str := static_prop.expr.asString(allocator)) {
                                if (str.len > 0) {
                                    pair.router.static_dir = str;
                                    pair.router.static_dir_enabled = true;
                                }
                            }
                        }

                        return;
                    }
                }
            },
            .production => {
                if (env := framework_object.expr.asProperty("production")) {
                    if (loadFrameworkExpression(pair.framework, env.expr, allocator, read_defines)) {
                        pair.framework.package = package_json.nameForImport(allocator) catch unreachable;
                        pair.framework.development = false;

                        if (static_prop := env.expr.asProperty("static")) {
                            if (str := static_prop.expr.asString(allocator)) {
                                if (str.len > 0) {
                                    pair.router.static_dir = str;
                                    pair.router.static_dir_enabled = true;
                                }
                            }
                        }

                        return;
                    }
                }
            },
            else => @compileError("unreachable"),
        }

        if (loadFrameworkExpression(pair.framework, framework_object.expr, allocator, read_defines)) {
            pair.framework.package = package_json.nameForImport(allocator) catch unreachable;
            pair.framework.development = false;
        }
    }

    static fn parseMacrosJSON(
        allocator: std.mem.Allocator,
        macros: js_ast.Expr,
        log: *logger.Log,
        json_source: *const logger.Source,
    ): MacroMap {
        let macro_map = MacroMap{};
        if (macros.data != .e_object) return macro_map;

        const properties = macros.data.e_object.properties.slice();

        for (property of properties) {
            const key = property.key.?.asString(allocator) ?? continue;
            if (!resolver.isPackagePath(key)) {
                log.addRangeWarningFmt(
                    json_source,
                    json_source.rangeOfString(property.key.?.loc),
                    allocator,
                    "\"{s}\" is not a package path. \"macros\" remaps package paths to macros. Skipping.",
                    .{key},
                ) catch unreachable;
                continue;
            }

            const value = property.value.?;
            if (value.data != .e_object) {
                log.addWarningFmt(
                    json_source,
                    value.loc,
                    allocator,
                    "Invalid macro remapping in \"{s}\": expected object where the keys are import names and the value is a string path to replace",
                    .{key},
                ) catch unreachable;
                continue;
            }

            const remap_properties = value.data.e_object.properties.slice();
            if (remap_properties.len == 0) continue;

            let map = MacroImportReplacementMap.init(allocator);
            map.ensureUnusedCapacity(remap_properties.len) catch unreachable;
            for (remap of remap_properties) {
                const import_name = remap.key.?.asString(allocator) ?? continue;
                const remap_value = remap.value.?;
                if (remap_value.data != .e_string || remap_value.data.e_string.data.len == 0) {
                    log.addWarningFmt(
                        json_source,
                        remap_value.loc,
                        allocator,
                        "Invalid macro remapping for import \"{s}\": expected string to remap to. e.g. \"graphql\": \"bun-macro-relay\" ",
                        .{import_name},
                    ) catch unreachable;
                    continue;
                }

                const remap_value_str = remap_value.data.e_string.data;

                map.putAssumeCapacityNoClobber(import_name, remap_value_str);
            }

            if (map.count() > 0) {
                macro_map.put(allocator, key, map) catch unreachable;
            }
        }

        return macro_map;
    }

    static fn parse(
        r: *resolver.Resolver,
        input_path: string,
        dirname_fd: FD,
        package_id: Install.PackageID?,
        comptime include_scripts_: IncludeScripts,
        comptime include_dependencies: IncludeDependencies,
    ): PackageJSON? {
        const include_scripts = include_scripts_ == .include_scripts;

        // TODO: remove it extra copy
        const parts: [2]string = [input_path, "package.json"];
        let path_buf: bun.PathBuffer = undefined;
        const package_json_path_ = r.fs.absBufChecked(&parts, &path_buf) ?? return null;
        const package_json_path = r.fs.dirname_store.append(@TypeOf(package_json_path_), package_json_path_) catch unreachable;

        // DirInfo cache is reused globally
        // So we cannot free these
        const allocator = bun.default_allocator;

        let entry_raw = r.caches.fs.readFileWithAllocator(
            allocator,
            r.fs,
            package_json_path,
            dirname_fd,
            false,
            null,
        ) catch e => {
            if (e != error.IsDir) {
                r.log.addErrorFmt(null, logger.Loc.Empty, allocator, "Cannot read file \"{s}\": {s}", .{ input_path, @errorName(e) }) catch unreachable;
            }

            return null;
        };
        let entry_guard = CloseFDGuard<@TypeOf(entry_raw)>{ val: entry_raw };
        const entry = entry_guard.val;

        if (*debug := r.debug_logs) {
            debug.addNoteFmt("The file \"{s}\" exists", .{package_json_path});
        }

        const key_path = fs.Path.init(package_json_path);

        let json_source = logger.Source.initPathString(key_path.text, entry.contents);
        json_source.path.pretty = json_source.path.text;

        const json: js_ast.Expr = (r.caches.json.parsePackageJSON(r.log, &json_source, allocator, true) catch e => {
            if (Environment.isDebug) {
                Output.printError("{s}: JSON parse error: {s}", .{ package_json_path, @errorName(e) });
            }
            return null;
        } ?? return null);

        if (json.data != .e_object) {
            // Invalid package.json in node_modules is noisy.
            // Let's just ignore it.
            allocator.free(entry.contents);
            return null;
        }

        let package_json = PackageJSON{
            name: "",
            version: "",
            source: json_source,
            module_type: .unknown,
            browser_map: BrowserMap.init(allocator, false),
            main_fields: MainFieldMap.init(allocator, false),
        };

        // Note: we tried rewriting it to be fewer loops over all the properties (asProperty loops over each)
        // The end result was: it's not faster! Sometimes, it's slower.
        // It's hard to say why.
        // Feels like a codegen issue.
        // or that looping over every property doesn't really matter because most package.jsons are < 20 properties
        if (version_json := json.asProperty("version")) {
            if (version_str := version_json.expr.asString(allocator)) {
                if (version_str.len > 0) {
                    package_json.version = allocator.dupe(u8, version_str) catch unreachable;
                }
            }
        }

        if (name_json := json.asProperty("name")) {
            if (name_str := name_json.expr.asString(allocator)) {
                if (name_str.len > 0) {
                    package_json.name = allocator.dupe(u8, name_str) catch unreachable;
                }
            }
        }

        if (type_json := json.asProperty("type")) {
            if (type_str := type_json.expr.asString(allocator)) {
                switch (options.ModuleType.List.get(type_str) ?? options.ModuleType.unknown) {
                    .cjs => {
                        package_json.module_type = .cjs;
                    },
                    .esm => {
                        package_json.module_type = .esm;
                    },
                    .unknown => {
                        r.log.addRangeWarningFmt(
                            &json_source,
                            json_source.rangeOfString(type_json.loc),
                            allocator,
                            "\"{s}\" is not a valid value for \"type\" field (must be either \"commonjs\" or \"module\")",
                            .{type_str},
                        ) catch unreachable;
                    },
                }
            } else {
                r.log.addWarning(&json_source, type_json.loc, "The value for \"type\" must be a string") catch unreachable;
            }
        }

        // Read the "main" fields
        for (main of r.opts.main_fields) {
            if (main_json := json.asProperty(main)) {
                const expr: js_ast.Expr = main_json.expr;

                if (str := (expr.asString(allocator))) {
                    if (str.len > 0) {
                        package_json.main_fields.put(main, str) catch unreachable;
                    }
                }
            }
        }

        // Read the "browser" property
        // Since we cache parsed package.json in-memory, we have to read the "browser" field
        // including when `target` is not `browser` since the developer may later
        // run a build for the browser in the same process (like the DevServer).
        {
            // We both want the ability to have the option of CJS vs. ESM and the
            // option of having node vs. browser. The way to do it is to use the
            // object literal form of the "browser" field like it:
            //
            //   "main": "dist/index.node.cjs.js",
            //   "module": "dist/index.node.esm.js",
            //   "browser": {
            //     "./dist/index.node.cjs.js": "./dist/index.browser.cjs.js",
            //     "./dist/index.node.esm.js": "./dist/index.browser.esm.js"
            //   },
            //
            if (browser_prop := json.asProperty("browser")) {
                switch (browser_prop.expr.data) {
                    .e_object => |obj| {
                        // The value is an object

                        // Remap all files in the browser field
                        let key_spill: std.ArrayListUnmanaged(u8) = {};
                        defer key_spill.deinit(allocator);
                        for (*prop of obj.properties.slice()) {
                            const _key_str = (prop.key ?? continue).asString(allocator) ?? continue;
                            const value: js_ast.Expr = prop.value ?? continue;

                            // Normalize the path so we can compare against it without getting
                            // confused by "./". There is no distinction between package paths and
                            // relative paths for these values because some tools (i.e. Browserify)
                            // don't make such a distinction.
                            //
                            // This leads to weird things like a mapping for "./foo" matching an
                            // import of "foo", but that's actually not a bug. Or arguably it's a
                            // bug in Browserify but we have to replicate it bug because packages
                            // do it in the wild.
                            const normalized_key = resolve_path.normalizeStringSpill(&key_spill, allocator, _key_str, true, .auto);
                            const key = allocator.dupe(u8, normalized_key) catch unreachable;

                            switch (value.data) {
                                .e_string => |str| {
                                    // If it is a string, it's a replacement package
                                    package_json.browser_map.put(key, str.string(allocator) catch unreachable) catch unreachable;
                                },
                                .e_boolean => |boolean| {
                                    if (!boolean.value) {
                                        package_json.browser_map.put(key, "") catch unreachable;
                                    }
                                },
                                else => {
                                    // Only print it warning if its not inside node_modules, since node_modules/ is not actionable.
                                    if (!json_source.path.isNodeModule())
                                        r.log.addWarning(&json_source, value.loc, "Each \"browser\" mapping must be a string or boolean") catch unreachable;
                                },
                            }
                        }
                    },
                    else => {},
                }
            }
        }

        if (exports_prop := json.asProperty("exports")) {
            if (exports_map := ExportsMap.parse(bun.default_allocator, &json_source, r.log, exports_prop.expr, exports_prop.loc)) {
                package_json.exports = exports_map;
            }
        }

        if (imports_prop := json.asProperty("imports")) {
            if (imports_map := ExportsMap.parse(bun.default_allocator, &json_source, r.log, imports_prop.expr, imports_prop.loc)) {
                package_json.imports = imports_map;
            }
        }

        if (side_effects_field := json.get("sideEffects")) {
            if (boolean := side_effects_field.asBool()) {
                if (!boolean) {
                    package_json.side_effects = .false;
                }
            } else if (side_effects_field.data == .e_array) {
                // Handle arrays, including empty arrays
                if (array_ := side_effects_field.asArray()) {
                    let array = array_;
                    let map: SideEffects.Map = {};
                    let glob_list: SideEffects.GlobList = {};
                    let has_globs = false;
                    let has_exact = false;

                    // First pass: check if we have glob patterns and exact patterns
                    while (item := array.next()) {
                        if (name := item.asString(allocator)) {
                            if (strings.containsChar(name, '*') || strings.containsChar(name, '?') || strings.containsChar(name, '[') || strings.containsChar(name, '{')) {
                                has_globs = true;
                            } else {
                                has_exact = true;
                            }
                        }
                    }

                    // Reset array for second pass
                    array = array_;

                    // If the array is empty, treat it as false (no side effects)
                    if (!has_globs && !has_exact) {
                        package_json.side_effects = .false;
                    } else if (has_globs && has_exact) {
                        // Mixed patterns - use both exact and glob matching
                        map.ensureTotalCapacity(allocator, array.array.items.len) catch unreachable;
                        glob_list.ensureTotalCapacity(allocator, array.array.items.len) catch unreachable;

                        while (item := array.next()) {
                            if (name := item.asString(allocator)) {
                                // Skip CSS files as they're not relevant for tree-shaking
                                if (strings.eqlComptime(std.fs.path.extension(name), ".css"))
                                    continue;

                                // Store the pattern relative to the package directory
                                let joined = [
                                    json_source.path.name.dirWithTrailingSlash(),
                                    name,
                                ];

                                const pattern = r.fs.join(&joined);

                                if (strings.containsChar(name, '*') || strings.containsChar(name, '?') || strings.containsChar(name, '[') || strings.containsChar(name, '{')) {
                                    // Normalize pattern to use forward slashes for cross-platform compatibility
                                    const normalized_pattern = normalizePathForGlob(allocator, pattern) catch unsafe { @constCast(pattern) };
                                    glob_list.appendAssumeCapacity(normalized_pattern);
                                } else {
                                    * = map.getOrPutAssumeCapacity(
                                        bun.StringHashMapUnowned.Key.init(pattern),
                                    );
                                }
                            }
                        }
                        package_json.side_effects = { mixed: { exact: map, globs: glob_list } };
                    } else if (has_globs) {
                        // Only glob patterns
                        glob_list.ensureTotalCapacity(allocator, array.array.items.len) catch unreachable;
                        while (item := array.next()) {
                            if (name := item.asString(allocator)) {
                                // Skip CSS files as they're not relevant for tree-shaking
                                if (strings.eqlComptime(std.fs.path.extension(name), ".css"))
                                    continue;

                                // Store the pattern relative to the package directory
                                let joined = [
                                    json_source.path.name.dirWithTrailingSlash(),
                                    name,
                                ];

                                const pattern = r.fs.join(&joined);
                                // Normalize pattern to use forward slashes for cross-platform compatibility
                                const normalized_pattern = normalizePathForGlob(allocator, pattern) catch unsafe { @constCast(pattern) };
                                glob_list.appendAssumeCapacity(normalized_pattern);
                            }
                        }
                        package_json.side_effects = { glob: glob_list };
                    } else {
                        // Only exact matches
                        map.ensureTotalCapacity(allocator, array.array.items.len) catch unreachable;
                        while (item := array.next()) {
                            if (name := item.asString(allocator)) {
                                let joined = [
                                    json_source.path.name.dirWithTrailingSlash(),
                                    name,
                                ];

                                * = map.getOrPutAssumeCapacity(
                                    bun.StringHashMapUnowned.Key.init(r.fs.join(&joined)),
                                );
                            }
                        }
                        package_json.side_effects = { map: map };
                    }
                } else {
                    // Empty array - treat as false (no side effects)
                    package_json.side_effects = .false;
                }
            }
        }

        if (comptime include_dependencies == .main || include_dependencies == .local) {
            update_dependencies: {
                if (pkg := package_id) {
                    package_json.package_manager_package_id = pkg;
                    break :update_dependencies;
                }

                // // if there is a name & version, check if the lockfile has the package
                if (package_json.name.len > 0 && package_json.version.len > 0) {
                    if (pm := r.package_manager) {
                        const tag = Dependency.Version.Tag.infer(package_json.version);

                        if (tag == .npm) {
                            const sliced = Semver.SlicedString.init(package_json.version, package_json.version);
                            if (dependency_version := Dependency.parseWithTag(
                                allocator,
                                String.init(package_json.name, package_json.name),
                                String.Builder.stringHash(package_json.name),
                                package_json.version,
                                .npm,
                                &sliced,
                                r.log,
                                pm,
                            )) {
                                if (dependency_version.value.npm.version.isExact()) {
                                    if (resolved := pm.lockfile.resolvePackageFromNameAndVersion(package_json.name, dependency_version)) {
                                        package_json.package_manager_package_id = resolved;
                                        if (resolved > 0) {
                                            break :update_dependencies;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (os_field := json.get("cpu")) {
                    if (array_const := os_field.asArray()) {
                        let array = array_const;
                        let arch = Architecture.none.negatable();
                        while (item := array.next()) {
                            if (str := item.asString(bun.default_allocator)) {
                                arch.apply(str);
                            }
                        }

                        package_json.arch = arch.combine();
                    }
                }

                if (os_field := json.get("os")) {
                    let tmp = os_field.asArray();
                    if (*array := tmp) {
                        let os = OperatingSystem.none.negatable();
                        while (item := array.next()) {
                            if (str := item.asString(bun.default_allocator)) {
                                os.apply(str);
                            }
                        }

                        package_json.os = os.combine();
                    }
                }

                const DependencyGroup = Install.Lockfile.Package.DependencyGroup;
                const dependency_groups = if (include_dependencies == .main)
                    [DependencyGroup.dependencies, DependencyGroup.dev, DependencyGroup.optional]
                else
                    [DependencyGroup.dependencies, DependencyGroup.optional];

                let total_dependency_count: usize = 0;
                inline for (group of dependency_groups) {
                    if (group_json := json.get(group.field)) {
                        if (group_json.data == .e_object) {
                            total_dependency_count += group_json.data.e_object.properties.len;
                        }
                    }
                }

                if (total_dependency_count > 0) {
                    package_json.dependencies.map = {};
                    package_json.dependencies.source_buf = json_source.contents;
                    const ctx = String.ArrayHashContext{
                        arg_buf: json_source.contents,
                        existing_buf: json_source.contents,
                    };
                    package_json.dependencies.map.ensureTotalCapacityContext(
                        allocator,
                        total_dependency_count,
                        ctx,
                    ) catch unreachable;

                    inline for (group of dependency_groups) {
                        if (group_json := json.get(group.field)) {
                            if (group_json.data == .e_object) {
                                let group_obj = group_json.data.e_object;
                                for (*prop of group_obj.properties.slice()) {
                                    const name_prop = prop.key ?? continue;
                                    const name_str = name_prop.asString(allocator) ?? continue;
                                    const name_hash = String.Builder.stringHash(name_str);
                                    const name = String.init(package_json.dependencies.source_buf, name_str);
                                    const version_value = prop.value ?? continue;
                                    const version_str = version_value.asString(allocator) ?? continue;
                                    const sliced_str = Semver.SlicedString.init(version_str, version_str);

                                    if (dependency_version := Dependency.parse(
                                        allocator,
                                        name,
                                        name_hash,
                                        version_str,
                                        &sliced_str,
                                        r.log,
                                        r.package_manager,
                                    )) {
                                        const dependency = Dependency{
                                            name: name,
                                            version: dependency_version,
                                            name_hash: name_hash,
                                            behavior: group.behavior,
                                        };
                                        package_json.dependencies.map.putAssumeCapacityContext(
                                            dependency.name,
                                            dependency,
                                            ctx,
                                        );
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // used by `bun run`
        if (include_scripts) {
            if (scripts := json.asPropertyStringMap("scripts", allocator)) {
                package_json.scripts = scripts;
            }
            if (config := json.asPropertyStringMap("config", allocator)) {
                package_json.config = config;
            }
        }

        return package_json;
    }

    fn hashModule(module: string): u32 {
        let hasher = bun.Wyhash.init(0);
        hasher.update(std.mem.asBytes(&_.hash));
        hasher.update(module);

        return @as(u32, @truncate(hasher.final()));
    }
}

pub enum ExportsMap_EntryDataTag {
    invalid,
    @"null",
    boolean,
    string,
    array,
    map,
}

pub struct ExportsMap_EntryDataMapEntry {
    key: string;
    key_range: logger.Range;
    value: ExportsMap_Entry;
}

pub struct ExportsMap_EntryDataMap {
    // This is not a std.ArrayHashMap because we also store the key_range which is a little weird
    const List = std.MultiArrayList(ExportsMap_EntryDataMapEntry);
    const MapEntry = ExportsMap_EntryDataMapEntry;
    expansion_keys: []ExportsMap_EntryDataMapEntry;
    list: List;
}

pub union(ExportsMap_EntryDataTag) ExportsMap_EntryData {
    invalid: void,
    @"null": void,
    boolean: bool,
    string: string,
    array: []const ExportsMap_Entry,
    map: ExportsMap_EntryDataMap,

    const Tag = ExportsMap_EntryDataTag;
    const Map = ExportsMap_EntryDataMap;
}

pub struct ExportsMap_Entry {
    first_token: logger.Range;
    data: ExportsMap_EntryData;

    const Data = ExportsMap_EntryData;

    fn keysStartWithDot(): bool {
        return _.data == .map && _.data.map.list.len > 0 && strings.startsWithChar(_.data.map.list.items(.key)[0], '.');
    }

    fn valueForKey(key_: string): ExportsMap_Entry? {
        switch (_.data) {
            .map => {
                let slice = _.data.map.list.slice();
                const keys = slice.items(.key);
                let i: usize = 0;
                while (i < keys.len) : (i += 1) {
                    const key = keys[i];
                    if (strings.eql(key, key_)) {
                        return slice.items(.value)[i];
                    }
                }

                return null;
            },
            else => {
                return null;
            },
        }
    }
}

pub struct ExportsMap_Visitor {
    allocator: std.mem.Allocator;
    source: *const logger.Source;
    log: *logger.Log;

    fn visit(*_, expr: js_ast.Expr): ExportsMap_Entry {
        let first_token: logger.Range = logger.Range.None;

        switch (expr.data) {
            .e_null => {
                return ExportsMap_Entry{ first_token: js_lexer.rangeOfIdentifier(_.source, expr.loc), data: ExportsMap_EntryData{ @"null": undefined } };
            },
            .e_string => {
                const str = expr.data.e_string;
                const str_slice = js_ast.E.String.slice;
                return ExportsMap_Entry{
                    data: ExportsMap_EntryData{
                        string: str_slice(str, _.allocator),
                    },
                    first_token: _.source.rangeOfString(expr.loc),
                };
            },
            .e_array => {
                const e_array = expr.data.e_array;
                let array = _.allocator.alloc(ExportsMap_Entry, e_array.items.len) catch unreachable;
                let i: usize = 0;
                for (item of e_array.items.slice()) {
                    array[i] = _.visit(item);
                    i += 1;
                }
                return ExportsMap_Entry{
                    data: ExportsMap_EntryData{
                        array: array,
                    },
                    first_token: logger.Range{ loc: expr.loc, len: 1 },
                };
            },
            .e_object => {
                const e_obj = expr.data.e_object;
                let map_data: ExportsMap_EntryDataMap.List = .{};
                map_data.ensureTotalCapacity(_.allocator, e_obj.properties.len) catch unreachable;
                map_data.len = e_obj.properties.len;
                let expansion_keys = _.allocator.alloc(ExportsMap_EntryDataMapEntry, e_obj.properties.len) catch unreachable;
                let expansion_key_i: usize = 0;
                let map_data_slices = map_data.slice();
                let map_data_keys = map_data_slices.items(.key);
                let map_data_ranges = map_data_slices.items(.key_range);
                let map_data_entries = map_data_slices.items(.value);
                let is_conditional_sugar = false;
                first_token.loc = expr.loc;
                first_token.len = 1;
                const str_slice = js_ast.E.String.slice;
                let i: usize = 0;
                for (prop of e_obj.properties.slice()) {
                    const key: string = str_slice(prop.key.?.data.e_string, _.allocator);
                    const key_range: logger.Range = _.source.rangeOfString(prop.key.?.loc);

                    // If exports is an Object with both a key starting with "." and a key
                    // not starting with ".", throw an Invalid Package Configuration error.
                    const cur_is_conditional_sugar = !strings.startsWithChar(key, '.');
                    if (i == 0) {
                        is_conditional_sugar = cur_is_conditional_sugar;
                    } else if (is_conditional_sugar != cur_is_conditional_sugar) {
                        const prev_key_range = map_data_ranges[i - 1];
                        const prev_key = map_data_keys[i - 1];
                        _.log.addRangeWarningFmtWithNote(
                            _.source,
                            key_range,
                            _.allocator,
                            "This object cannot contain keys that both start with \".\" and don't start with \".\"",
                            .{},
                            "The previous key \"{s}\" is incompatible with the current key \"{s}\"",
                            .{ prev_key, key },
                            prev_key_range,
                        ) catch unreachable;
                        map_data.deinit(_.allocator);
                        _.allocator.free(expansion_keys);
                        return ExportsMap_Entry{
                            data: ExportsMap_EntryData{ invalid: undefined },
                            first_token: first_token,
                        };
                    }

                    map_data_keys[i] = key;
                    map_data_ranges[i] = key_range;
                    map_data_entries[i] = _.visit(prop.value.?);

                    // safe to use "/" on windows. exports in package.json does not use "\\"
                    if (strings.endsWithComptime(key, "/") || strings.containsChar(key, '*')) {
                        expansion_keys[expansion_key_i] = ExportsMap_EntryDataMapEntry{
                            value: map_data_entries[i],
                            key: key,
                            key_range: key_range,
                        };
                        expansion_key_i += 1;
                    }
                    i += 1;
                }

                // it leaks a lil, but it's fine.
                expansion_keys = expansion_keys[0..expansion_key_i];

                // Let expansionKeys be the list of keys of matchObj either ending in "/"
                // or containing only a single "*", sorted by the sorting function
                // PATTERN_KEY_COMPARE which orders in descending order of specificity.
                const GlobLengthSorter: type = strings.NewGlobLengthSorter(ExportsMap_EntryDataMapEntry, "key");
                const sorter = GlobLengthSorter{};
                std.sort.pdq(ExportsMap_EntryDataMapEntry, expansion_keys, sorter, GlobLengthSorter.lessThan);

                return ExportsMap_Entry{
                    data: ExportsMap_EntryData{
                        map: ExportsMap_EntryDataMap{
                            list: map_data,
                            expansion_keys: expansion_keys,
                        },
                    },
                    first_token: first_token,
                };
            },
            .e_boolean => {
                first_token = js_lexer.rangeOfIdentifier(_.source, expr.loc);
            },
            .e_number => {
                // TODO: range of number
                first_token.loc = expr.loc;
                first_token.len = 1;
            },
            else => {
                first_token.loc = expr.loc;
            },
        }

        _.log.addRangeWarning(_.source, first_token, "This value must be a string, an object, an array, or null") catch unreachable;
        return ExportsMap_Entry{
            data: ExportsMap_EntryData{ invalid: undefined },
            first_token: first_token,
        };
    }
}

pub struct ExportsMap {
    root: Entry,
    exports_range: logger.Range = logger.Range.None,
    property_key_loc: logger.Loc,

    static fn parse(allocator: std.mem.Allocator, source: *const logger.Source, log: *logger.Log, json: js_ast.Expr, property_key_loc: logger.Loc): ExportsMap? {
        let visitor = Visitor{ allocator: allocator, source: source, log: log };

        const root = visitor.visit(json);

        if (root.data == .@"null") {
            return null;
        }

        return ExportsMap{
            root: root,
            exports_range: source.rangeOfString(json.loc),
            property_key_loc: property_key_loc,
        };
    }

    const Visitor = ExportsMap_Visitor;
    const Entry = ExportsMap_Entry;
}

pub enum ESModule_Status {
    Undefined,
    UndefinedNoConditionsMatch, // A more friendly error message for when no conditions are matched
    Null,
    Exact,
    ExactEndsWithStar,
    Inexact, // This means we may need to try CommonJS-style extension suffixes

    /// Module specifier is an invalid URL, package name or package subpath specifier.
    InvalidModuleSpecifier,

    /// package.json configuration is invalid or contains an invalid configuration.
    InvalidPackageConfiguration,

    /// Package exports or imports define a target module for the package that is an invalid type or string target.
    InvalidPackageTarget,

    /// Package exports do not define or permit a target subpath in the package for the given module.
    PackagePathNotExported,

    /// The package or module requested does not exist.
    ModuleNotFound,

    /// The user just needs to add the missing extension
    ModuleNotFoundMissingExtension,

    /// The resolved path corresponds to a directory, which is not a supported target for module imports.
    UnsupportedDirectoryImport,

    /// The user just needs to add the missing "/index.js" suffix
    UnsupportedDirectoryImportMissingIndex,

    /// When a package path is explicitly set to null, that means it's not exported.
    PackagePathDisabled,

    // The internal #import specifier was not found
    PackageImportNotDefined,

    PackageResolve,

    inline fn isUndefined(): bool {
        return switch (_) {
            .Undefined, .UndefinedNoConditionsMatch => true,
            else => false,
        };
    }
}

pub struct ESModule_ResolutionDebug {
    // This is the range of the token to use for error messages
    token: logger.Range = logger.Range.None;
    // If the status is "UndefinedNoConditionsMatch", it is the set of
    // conditions that didn't match. This information is used for error messages.
    unmatched_conditions: []string = [];
}

pub struct ESModule_Resolution {
    status: ESModule_Status = ESModule_Status.Undefined;
    path: string = "";
    debug: ESModule_ResolutionDebug = ESModule_ResolutionDebug{};

    const Debug = ESModule_ResolutionDebug;
}

pub struct ESModule_PackageExternal {
    name: Semver.String = {};
    version: Semver.String = {};
    subpath: Semver.String = {};
}

pub struct ESModule_Package {
    name: string;
    version: string = "";
    subpath: string;

    pub const External = ESModule_PackageExternal;

    fn count(builder: *Semver.String.Builder) {
        builder.count(_.name);
        builder.count(_.version);
        builder.count(_.subpath);
    }

    fn clone(builder: *Semver.String.Builder): ESModule_PackageExternal {
        return ESModule_PackageExternal{
            name: builder.appendUTF8WithoutPool(Semver.String, _.name, 0),
            version: builder.appendUTF8WithoutPool(Semver.String, _.version, 0),
            subpath: builder.appendUTF8WithoutPool(Semver.String, _.subpath, 0),
        };
    }

    fn toExternal(buffer: []const u8): ESModule_PackageExternal {
        return ESModule_PackageExternal{
            name: Semver.String.init(buffer, _.name),
            version: Semver.String.init(buffer, _.version),
            subpath: Semver.String.init(buffer, _.subpath),
        };
    }

    fn withAutoVersion(): ESModule_Package {
        if (_.version.len == 0) {
            return ESModule_Package{
                name: _.name,
                subpath: _.subpath,
                version: "latest",
            };
        }

        return _;
    }

    static fn parseName(specifier: string): string? {
        let slash = strings.indexOfCharNeg(specifier, '/');
        if (!strings.startsWithChar(specifier, '@')) {
            slash = if (slash == -1) @as(i32, @intCast(specifier.len)) else slash;
            return specifier[0..@as(usize, @intCast(slash))];
        } else {
            if (slash == -1) return null;

            const slash2 = strings.indexOfChar(specifier[@as(usize, @intCast(slash)) + 1 ..], '/') ??
                specifier[@as(u32, @intCast(slash + 1))..].len;
            return specifier[0 .. @as(usize, @intCast(slash + 1)) + slash2];
        }
    }

    static fn parseVersion(specifier_after_name: string): string? {
        if (slash := strings.indexOfChar(specifier_after_name, '/')) {
            // "foo@/bar" is not a valid specifier\
            // "foo@/"   is not a valid specifier
            // "foo/@/bar" is not a valid specifier
            // "foo@1/bar" is a valid specifier
            // "foo@^123.2.3+ba-ab/bar" is a valid specifier
            //      ^^^^^^^^^^^^^^
            //    it is the version

            const remainder = specifier_after_name[0..slash];
            if (remainder.len > 0 && remainder[0] == '@') {
                return remainder[1..];
            }

            return remainder;
        }

        return null;
    }

    static fn parse(specifier: string, subpath_buf: []u8): ESModule_Package? {
        if (specifier.len == 0) return null;
        let package = ESModule_Package{ name: parseName(specifier) ?? return null, subpath: "" };

        if (strings.startsWith(package.name, ".") || strings.indexAnyComptime(package.name, "\\%") != null)
            return null;

        // A version delimiter `@` is only valid within the package-name portion of
        // the specifier. Searching the entire specifier misparses wildcard subpaths
        // whose matched substring contains `@` (e.g. `test-pkg/@scope/sub/index.js`
        // or `ember-source/@ember/renderer/...`) as if the package had a version.
        const offset: usize = if (package.name.len == 0 || package.name[0] != '@') 0 else 1;
        if (offset < package.name.len) {
            if (at := strings.indexOfChar(specifier[offset..package.name.len], '@')) {
                if (ver := parseVersion(specifier[offset..][at..])) {
                    package.version = ver;
                } else {
                    package.version = "";
                }
                if (package.version.len == 0) {
                    package.version = specifier[offset..][at..];
                    if (package.version.len > 0 && package.version[0] == '@') {
                        package.version = package.version[1..];
                    }
                }
                package.name = specifier[0 .. at + offset];

                parseSubpath(&package.subpath, specifier[@min(package.name.len + package.version.len + 1, specifier.len)..], subpath_buf);
                return package;
            }
        }
        parseSubpath(&package.subpath, specifier[package.name.len..], subpath_buf);

        return package;
    }

    static fn parseSubpath(subpath: *[]const u8, specifier: string, subpath_buf: []u8) {
        if (specifier.len + 1 > subpath_buf.len) {
            subpath.* = "";
            return;
        }
        subpath_buf[0] = '.';
        bun.copy(u8, subpath_buf[1..], specifier);
        subpath.* = subpath_buf[0 .. specifier.len + 1];
    }
}

pub enum ESModule_ReverseKind { exact, pattern, prefix }
pub struct ESModule_ReverseResolution {
    subpath: string = "";
    token: logger.Range = logger.Range.None;
}

pub struct ESModule {
    pub const ConditionsMap = bun.StringArrayHashMap(void);

    debug_logs: (*resolver.DebugLogs)? = null,
    conditions: ConditionsMap,
    allocator: std.mem.Allocator,
    module_type: *options.ModuleType = undefined,

    pub const Resolution = ESModule_Resolution;

    const Status = ESModule_Status;

    pub const Package = ESModule_Package;
    const ReverseKind = ESModule_ReverseKind;
    const ReverseResolution = ESModule_ReverseResolution;
    const invalid_percent_chars = [
        "%2f",
        "%2F",
        "%5c",
        "%5C",
    ];

    const module_bufs = bun.ThreadlocalBuffers(struct {
        resolved_path_buf_percent: bun.PathBuffer = undefined,
        resolve_target_buf: bun.PathBuffer = undefined,
        resolve_target_buf2: bun.PathBuffer = undefined,
        resolve_target_reverse_prefix_buf: bun.PathBuffer = undefined,
        resolve_target_reverse_prefix_buf2: bun.PathBuffer = undefined,
    });

    static fn resolve(r: *const ESModule, package_url: string, subpath: string, exports: ExportsMap.Entry): Resolution {
        return finalize(
            r.resolveExports(package_url, subpath, exports),
        );
    }

    static fn resolveImports(r: *const ESModule, specifier: string, imports: ExportsMap.Entry): Resolution {
        if (imports.data != .map) {
            return {
                status: .InvalidPackageConfiguration,
                debug: {
                    token: logger.Range.None,
                },
            };
        }

        const result = r.resolveImportsExports(
            specifier,
            imports,
            true,
            "/",
        );

        switch (result.status) {
            .Undefined, .Null => {
                return { status: .PackageImportNotDefined, debug: { token: result.debug.token } };
            },
            else => {
                return finalize(result);
            },
        }
    }

    static fn finalize(result_: Resolution): Resolution {
        let result = result_;
        if (result.status != .Exact && result.status != .ExactEndsWithStar && result.status != .Inexact) {
            return result;
        }

        // If resolved contains any percent encodings of "/" or "\" ("%2f" and "%5C"
        // respectively), then throw an Invalid Module Specifier error.
        const bufs_ptr = module_bufs.get();
        const resolved_path_buf_percent = &bufs_ptr.resolved_path_buf_percent;
        let fbs = std.io.fixedBufferStream(resolved_path_buf_percent);
        let writer = fbs.writer();
        const len = url_mod.PercentEncoding.decode(@TypeOf(&writer), &writer, result.path) catch return Resolution{
            status: .InvalidModuleSpecifier,
            path: result.path,
            debug: result.debug,
        };

        const resolved_path = resolved_path_buf_percent[0..len];

        let found: string = "";
        if (strings.contains(resolved_path, invalid_percent_chars[0])) {
            found = invalid_percent_chars[0];
        } else if (strings.contains(resolved_path, invalid_percent_chars[1])) {
            found = invalid_percent_chars[1];
        } else if (strings.contains(resolved_path, invalid_percent_chars[2])) {
            found = invalid_percent_chars[2];
        } else if (strings.contains(resolved_path, invalid_percent_chars[3])) {
            found = invalid_percent_chars[3];
        }

        if (found.len != 0) {
            return Resolution{ .status = .InvalidModuleSpecifier, .path = result.path, .debug = result.debug };
        }

        // If resolved is a directory, throw an Unsupported Directory Import error.
        if (strings.endsWithAnyComptime(resolved_path, "/\\")) {
            return Resolution{ .status = .UnsupportedDirectoryImport, .path = result.path, .debug = result.debug };
        }

        result.path = resolved_path;
        return result;
    }

    static fn resolveExports(
        r: *const ESModule,
        package_url: string,
        subpath: string,
        exports: ExportsMap.Entry,
    ): Resolution {
        if (exports.data == .invalid) {
            if (logs := r.debug_logs) {
                logs.addNote("Invalid package configuration");
            }

            return Resolution{ .status = .InvalidPackageConfiguration, debug: { token: exports.first_token } };
        }

        if (strings.eqlComptime(subpath, ".")) {
            let main_export: ExportsMap.Entry = { data: { @"null": undefined }, first_token: logger.Range.None };
            if (switch (exports.data) {
                .string, .array => true,
                .map => !exports.keysStartWithDot(),
                else => false,
            }) {
                main_export = exports;
            } else if (exports.data == .map) {
                if (value := exports.valueForKey(".")) {
                    main_export = value;
                }
            }

            if (main_export.data != .@"null") {
                const result = r.resolveTarget(package_url, main_export, "", false, false);
                if (result.status != .Null && result.status != .Undefined) {
                    return result;
                }
            }
        } else if (exports.data == .map && exports.keysStartWithDot()) {
            const result = r.resolveImportsExports(subpath, exports, false, package_url);
            if (result.status != .Null && result.status != .Undefined) {
                return result;
            }

            if (result.status == .Null) {
                return Resolution{ .status = .PackagePathDisabled, debug: { token: exports.first_token } };
            }
        }

        if (logs := r.debug_logs) {
            logs.addNoteFmt("The path \"{s}\" was not exported", .{subpath});
        }

        return Resolution{ .status = .PackagePathNotExported, debug: { token: exports.first_token } };
    }

    static fn resolveImportsExports(
        r: *const ESModule,
        match_key: string,
        match_obj: ExportsMap.Entry,
        is_imports: bool,
        package_url: string,
    ): Resolution {
        if (logs := r.debug_logs) {
            logs.addNoteFmt("Checking object path map for \"{s}\"", .{match_key});
        }

        // If matchKey is a key of matchObj and does not end in "/" or contain "*", then
        if (!strings.endsWithChar(match_key, '/') && !strings.containsChar(match_key, '*')) {
            if (target := match_obj.valueForKey(match_key)) {
                if (log := r.debug_logs) {
                    log.addNoteFmt("Found \"{s}\"", .{match_key});
                }

                return r.resolveTarget(package_url, target, "", is_imports, false);
            }
        }

        if (match_obj.data == .map) {
            const expansion_keys = match_obj.data.map.expansion_keys;
            for (expansion of expansion_keys) {

                // If expansionKey contains "*", set patternBase to the substring of
                // expansionKey up to but excluding the first "*" character
                if (star := strings.indexOfChar(expansion.key, '*')) {
                    const pattern_base = expansion.key[0..star];
                    // If patternBase is not null and matchKey starts with but is not equal
                    // to patternBase, then
                    if (strings.startsWith(match_key, pattern_base)) {
                        // Let patternTrailer be the substring of expansionKey from the index
                        // after the first "*" character.
                        const pattern_trailer = expansion.key[star + 1 ..];

                        // If patternTrailer has zero length, or if matchKey ends with
                        // patternTrailer and the length of matchKey is greater than or
                        // equal to the length of expansionKey, then
                        if (pattern_trailer.len == 0 || (strings.endsWith(match_key, pattern_trailer) && match_key.len >= expansion.key.len)) {
                            const target = expansion.value;
                            const subpath = match_key[pattern_base.len .. match_key.len - pattern_trailer.len];
                            if (log := r.debug_logs) {
                                log.addNoteFmt("The key \"{s}\" matched with \"{s}\" left over", .{ expansion.key, subpath });
                            }
                            return r.resolveTarget(package_url, target, subpath, is_imports, true);
                        }
                    }
                } else {
                    // Otherwise if patternBase is null and matchKey starts with
                    // expansionKey, then
                    if (strings.startsWith(match_key, expansion.key)) {
                        const target = expansion.value;
                        const subpath = match_key[expansion.key.len..];
                        if (log := r.debug_logs) {
                            log.addNoteFmt("The key \"{s}\" matched with \"{s}\" left over", .{ expansion.key, subpath });
                        }
                        let result = r.resolveTarget(package_url, target, subpath, is_imports, false);
                        if (result.status == .Exact || result.status == .ExactEndsWithStar) {
                            // Return the object { resolved, exact: false }.
                            result.status = .Inexact;
                        }
                        return result;
                    }
                }

                if (log := r.debug_logs) {
                    log.addNoteFmt("The key \"{s}\" did not match", .{expansion.key});
                }
            }
        }

        if (log := r.debug_logs) {
            log.addNoteFmt("No keys matched \"{s}\"", .{match_key});
        }

        return Resolution{
            status: .Null,
            debug: { token: match_obj.first_token },
        };
    }

    static fn resolveTarget(
        r: *const ESModule,
        package_url: string,
        target: ExportsMap.Entry,
        subpath: string,
        internal: bool,
        comptime pattern: bool,
    ): Resolution {
        const bufs_ptr = module_bufs.get();
        const resolve_target_buf = &bufs_ptr.resolve_target_buf;
        const resolve_target_buf2 = &bufs_ptr.resolve_target_buf2;
        switch (target.data) {
            .string => |str| {
                if (log := r.debug_logs) {
                    log.addNoteFmt("Checking path \"{s}\" against target \"{s}\"", .{ subpath, str });
                    log.increaseIndent();
                }
                defer {
                    if (log := r.debug_logs) {
                        log.decreaseIndent();
                    }
                }

                // If pattern is false, subpath has non-zero length and target
                // does not end with "/", throw an Invalid Module Specifier error.
                if (comptime !pattern) {
                    if (subpath.len > 0 && !strings.endsWithChar(str, '/')) {
                        if (log := r.debug_logs) {
                            log.addNoteFmt("The target \"{s}\" is invalid because it doesn't end with a \"/\"", .{str});
                        }

                        return Resolution{ .path = str, status: .InvalidModuleSpecifier, debug: { token: target.first_token } };
                    }
                }

                if (subpath.len > 0) {
                    if (invalid := findInvalidSubpathSegment(subpath)) {
                        if (log := r.debug_logs) {
                            log.addNoteFmt("The path \"{s}\" is invalid because it contains an invalid segment \"{s}\"", .{ subpath, invalid });
                        }

                        return Resolution{ .path = subpath, status: .InvalidModuleSpecifier, debug: { token: target.first_token } };
                    }
                }

                // If target does not start with "./", then...
                if (!strings.startsWith(str, "./")) {
                    if (log := r.debug_logs) {
                        log.addNoteFmt("The target \"{s}\" is invalid because it doesn't start with a \"./\"", .{str});
                    }

                    if (internal && !strings.hasPrefixComptime(str, "../") && !strings.hasPrefix(str, "/")) {
                        if (comptime pattern) {
                            // Return the URL resolution of resolvedTarget with every instance of "*" replaced with subpath.
                            const len = std.mem.replacementSize(u8, str, "*", subpath);
                            * = std.mem.replace(u8, str, "*", subpath, resolve_target_buf2);
                            const result = resolve_target_buf2[0..len];
                            if (log := r.debug_logs) {
                                log.addNoteFmt("Subsituted \"{s}\" for \"*\" in \".{s}\" to get \".{s}\" ", .{ subpath, str, result });
                            }

                            return Resolution{ .path = result, status: .PackageResolve, debug: { token: target.first_token } };
                        } else {
                            const parts2 = [str, subpath];
                            const result = resolve_path.joinStringBuf(resolve_target_buf2, parts2, .auto);
                            if (log := r.debug_logs) {
                                log.addNoteFmt("Resolved \".{s}\" to \".{s}\"", .{ str, result });
                            }

                            return Resolution{ .path = result, status: .PackageResolve, debug: { token: target.first_token } };
                        }
                    }

                    return Resolution{ .path = str, status: .InvalidPackageTarget, debug: { token: target.first_token } };
                }

                // If target split on "/" or "\" contains any ".", ".." or "node_modules"
                // segments after the first segment, throw an Invalid Package Target error.
                if (invalid := findInvalidSegment(str)) {
                    if (log := r.debug_logs) {
                        log.addNoteFmt("The target \"{s}\" is invalid because it contains an invalid segment \"{s}\"", .{ str, invalid });
                    }

                    return Resolution{ .path = str, status: .InvalidPackageTarget, debug: { token: target.first_token } };
                }

                // Let resolvedTarget be the URL resolution of the concatenation of packageURL and target.
                const parts: [2]string = [package_url, str];
                const resolved_target = resolve_path.joinStringBuf(resolve_target_buf, parts, .auto);

                // If target split on "/" or "\" contains any ".", ".." or "node_modules"
                // segments after the first segment, throw an Invalid Package Target error.
                if (invalid := findInvalidSegment(resolved_target)) {
                    if (log := r.debug_logs) {
                        log.addNoteFmt("The target \"{s}\" is invalid because it contains an invalid segment \"{s}\"", .{ str, invalid });
                    }

                    return Resolution{ .path = str, status: .InvalidModuleSpecifier, debug: { token: target.first_token } };
                }

                if (comptime pattern) {
                    // Return the URL resolution of resolvedTarget with every instance of "*" replaced with subpath.
                    const len = std.mem.replacementSize(u8, resolved_target, "*", subpath);
                    * = std.mem.replace(u8, resolved_target, "*", subpath, resolve_target_buf2);
                    const result = resolve_target_buf2[0..len];
                    if (log := r.debug_logs) {
                        log.addNoteFmt("Substituted \"{s}\" for \"*\" in \".{s}\" to get \".{s}\" ", .{ subpath, resolved_target, result });
                    }

                    const status: Status = if (strings.endsWithCharOrIsZeroLength(result, '*') && strings.indexOfChar(result, '*').? == result.len - 1)
                        .ExactEndsWithStar
                    else
                        .Exact;
                    return Resolution{ .path = result, status: status, debug: { token: target.first_token } };
                } else {
                    const parts2: [3][]const u8 = [package_url, str, subpath];
                    const result = resolve_path.joinStringBuf(resolve_target_buf2, parts2, .auto);
                    if (log := r.debug_logs) {
                        log.addNoteFmt("Substituted \"{s}\" for \"*\" in \".{s}\" to get \".{s}\" ", .{ subpath, resolved_target, result });
                    }

                    return Resolution{ .path = result, status: .Exact, debug: { token: target.first_token } };
                }
            },
            .map => |object| {
                let did_find_map_entry = false;
                let last_map_entry_i: usize = 0;

                const slice = object.list.slice();
                const keys = slice.items(.key);
                let i: usize = 0;
                while (i < keys.len) : (i += 1) {
                    const key = keys[i];
                    if (r.conditions.contains(key)) {
                        if (log := r.debug_logs) {
                            log.addNoteFmt("The key \"{s}\" matched", .{key});
                        }

                        const prev_module_type = r.module_type.*;
                        let result = r.resolveTarget(package_url, slice.items(.value)[i], subpath, internal, pattern);
                        if (result.status.isUndefined()) {
                            did_find_map_entry = true;
                            last_map_entry_i = i;
                            r.module_type.* = prev_module_type;
                            continue;
                        }

                        if (strings.eqlComptime(key, "import")) {
                            r.module_type.* = .esm;
                        }

                        if (strings.eqlComptime(key, "require")) {
                            r.module_type.* = .cjs;
                        }

                        return result;
                    }

                    if (log := r.debug_logs) {
                        log.addNoteFmt("The key \"{s}\" did not match", .{key});
                    }
                }

                if (log := r.debug_logs) {
                    log.addNoteFmt("No keys matched", {});
                }

                let return_target = target;
                // ALGORITHM DEVIATION: Provide a friendly error message if no conditions matched
                if (keys.len > 0 && !target.keysStartWithDot()) {
                    let last_map_entry = ExportsMap.Entry.Data.Map.MapEntry{
                        key: keys[last_map_entry_i],
                        value: slice.items(.value)[last_map_entry_i],
                        // key_range is unused, so we don't need to pull up the array for it.
                        key_range: logger.Range.None,
                    };
                    if (did_find_map_entry &&
                        last_map_entry.value.data == .map &&
                        last_map_entry.value.data.map.list.len > 0 &&
                        !last_map_entry.value.keysStartWithDot())
                    {
                        // If a top-level condition did match but no sub-condition matched,
                        // complain about the sub-condition instead of the top-level condition.
                        // This leads to a less confusing error message. For example:
                        //
                        //   "exports": {
                        //     "node": {
                        //       "require": "./dist/bwip-js-node.js"
                        //     }
                        //   },
                        //
                        // We want the warning to say it:
                        //
                        //   note: None of the conditions provided ("require") match any of the
                        //         currently active conditions ("default", "import", "node")
                        //   14 |       "node": {
                        //      |               ^
                        //
                        // We don't want the warning to say it:
                        //
                        //   note: None of the conditions provided ("browser", "electron", "node")
                        //         match any of the currently active conditions ("default", "import", "node")
                        //   7 |   "exports": {
                        //     |              ^
                        //
                        // More information: https://github.com/evanw/esbuild/issues/1484
                        return_target = last_map_entry.value;
                    }

                    return Resolution{
                        path: "",
                        status: .UndefinedNoConditionsMatch,
                        debug: {
                            token: target.first_token,
                            unmatched_conditions: return_target.data.map.list.items(.key),
                        },
                    };
                }

                return Resolution{
                    path: "",
                    status: .UndefinedNoConditionsMatch,
                    debug: { token: target.first_token },
                };
            },
            .array => |array| {
                if (array.len == 0) {
                    if (log := r.debug_logs) {
                        log.addNoteFmt("The path \"{s}\" is an empty array", .{subpath});
                    }

                    return Resolution{ .path = "", status: .Null, debug: { token: target.first_token } };
                }

                let last_exception = Status.Undefined;
                let last_debug = Resolution.Debug{ .token = target.first_token };

                for (targetValue of array) {
                    // Let resolved be the result, continuing the loop on any Invalid Package Target error.
                    const prev_module_type = r.module_type.*;
                    const result = r.resolveTarget(package_url, targetValue, subpath, internal, pattern);
                    if (result.status == .InvalidPackageTarget || result.status == .Null) {
                        last_debug = result.debug;
                        last_exception = result.status;
                    }

                    if (result.status.isUndefined()) {
                        r.module_type.* = prev_module_type;
                        continue;
                    }

                    return result;
                }

                return Resolution{ .path = "", .status = last_exception, .debug = last_debug };
            },
            .@"null" => {
                if (log := r.debug_logs) {
                    log.addNoteFmt("The path \"{s}\" is null", .{subpath});
                }

                return Resolution{ .path = "", status: .Null, debug: { token: target.first_token } };
            },
            else => {},
        }

        if (logs := r.debug_logs) {
            logs.addNoteFmt("Invalid package target for path \"{s}\"", .{subpath});
        }

        return Resolution{ .status = .InvalidPackageTarget, debug: { token: target.first_token } };
    }

    static fn resolveExportsReverse(
        r: *const ESModule,
        query: string,
        root: ExportsMap.Entry,
    ): ReverseResolution? {
        if (root.data == .map && root.keysStartWithDot()) {
            if (res := r.resolveImportsExportsReverse(query, root)) {
                return res;
            }
        }

        return null;
    }

    static fn resolveImportsExportsReverse(
        r: *const ESModule,
        query: string,
        match_obj: ExportsMap.Entry,
    ): ReverseResolution? {
        if (match_obj.data != .map) return null;
        const map = match_obj.data.map;

        if (!strings.endsWithCharOrIsZeroLength(query, "*")) {
            let slices = map.list.slice();
            const keys = slices.items(.key);
            const values = slices.items(.value);
            let i: usize = 0;
            while (i < keys.len) : (i += 1) {
                const key = keys[i];
                if (result := r.resolveTargetReverse(query, key, values[i], .exact)) {
                    return result;
                }
            }
        }

        for (expansion of map.expansion_keys) {
            if (strings.endsWithCharOrIsZeroLength(expansion.key, '*')) {
                if (result := r.resolveTargetReverse(query, expansion.key, expansion.value, .pattern)) {
                    return result;
                }
            }

            if (result := r.resolveTargetReverse(query, expansion.key, expansion.value, .reverse)) {
                return result;
            }
        }
    }

    static fn resolveTargetReverse(
        r: *const ESModule,
        query: string,
        key: string,
        target: ExportsMap.Entry,
        comptime kind: ReverseKind,
    ): ReverseResolution? {
        const resolve_target_reverse_prefix_buf = &module_bufs.get().resolve_target_reverse_prefix_buf;
        const resolve_target_reverse_prefix_buf2 = &module_bufs.get().resolve_target_reverse_prefix_buf2;
        switch (target.data) {
            .string => |str| {
                switch (comptime kind) {
                    .exact => {
                        if (strings.eql(query, str)) {
                            return ReverseResolution{ .subpath = str, .token = target.first_token };
                        }
                    },
                    .prefix => {
                        if (strings.startsWith(query, str)) {
                            return ReverseResolution{
                                subpath: std.fmt.bufPrint(resolve_target_reverse_prefix_buf, "{s}{s}", .{ key, query[str.len..] }) catch unreachable,
                                token: target.first_token,
                            };
                        }
                    },
                    .pattern => {
                        const key_without_trailing_star = std.mem.trimRight(u8, key, "*");

                        const star_opt = strings.indexOfChar(str, '*');
                        if (star_opt == null) {
                            if (strings.eql(query, str)) {
                                return ReverseResolution{ .subpath = key_without_trailing_star, .token = target.first_token };
                            }
                            return null;
                        }
                        const star = star_opt.?;

                        // Only support tracing through a single "*"
                        const prefix = str[0..star];
                        const suffix = str[star + 1 ..];
                        if (strings.startsWith(query, prefix) && !strings.containsChar(suffix, '*')) {
                            const after_prefix = query[prefix.len..];
                            if (strings.endsWith(after_prefix, suffix)) {
                                const star_data = after_prefix[0 .. after_prefix.len - suffix.len];
                                return ReverseResolution{
                                    subpath: std.fmt.bufPrint(
                                        resolve_target_reverse_prefix_buf2,
                                        "{s}{s}",
                                        .{
                                            key_without_trailing_star,
                                            star_data,
                                        },
                                    ) catch unreachable,
                                    token: target.first_token,
                                };
                            }
                        }
                    },
                }
            },
            .map => |map| {
                const slice = map.list.slice();
                const keys = slice.items(.key);
                let i: usize = 0;
                while (i < keys.len) : (i += 1) {
                    const map_key = keys[i];
                    if (r.conditions.contains(map_key)) {
                        if (result := r.resolveTargetReverse(query, key, slice.items(.value)[i], kind)) {
                            if (strings.eqlComptime(map_key, "import")) {
                                r.module_type.* = .esm;
                            } else if (strings.eqlComptime(map_key, "require")) {
                                r.module_type.* = .cjs;
                            }

                            return result;
                        }
                    }
                }
            },

            .array => |array| {
                for (target_value of array) {
                    if (result := r.resolveTargetReverse(query, key, target_value, kind)) {
                        return result;
                    }
                }
            },

            else => {},
        }

        return null;
    }
}

fn findInvalidSegment(path_: string): string? {
    const slash = strings.indexAnyComptime(path_, "/\\") ?? return "";
    let path = path_[slash + 1 ..];

    while (path.len > 0) {
        let segment = path;
        if (new_slash := strings.indexAnyComptime(path, "/\\")) {
            segment = path[0..new_slash];
            path = path[new_slash + 1 ..];
        } else {
            path = "";
        }

        if (isInvalidSegment(segment)) return segment;
    }

    return null;
}

fn findInvalidSubpathSegment(path_: string): string? {
    let path = path_;
    while (path.len > 0) {
        let segment = path;
        if (new_slash := strings.indexAnyComptime(path, "/\\")) {
            segment = path[0..new_slash];
            path = path[new_slash + 1 ..];
        } else {
            path = "";
        }

        if (isInvalidSegment(segment)) return segment;
    }

    return null;
}

fn hexDigitValue(c: u8): u8? {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return null;
}

fn isInvalidSegment(segment: string): bool {
    let decoded: [12]u8 = undefined;
    let len: usize = 0;
    let i: usize = 0;
    while (i < segment.len) {
        const b = segment[i];
        let c = b;
        if (b == '%' && i + 2 < segment.len) {
            if (hi := hexDigitValue(segment[i + 1])) {
                if (lo := hexDigitValue(segment[i + 2])) {
                    c = (hi << 4) | lo;
                    i += 3;
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        } else {
            i += 1;
        }
        if (len == decoded.len) return false;
        decoded[len] = std.ascii.toLower(c);
        len += 1;
    }
    const d = decoded[0..len];
    return strings.eqlComptime(d, ".") || strings.eqlComptime(d, "..") || strings.eqlComptime(d, "node_modules");
}


import Dependency from "../install/dependency.zig";
import Install from "../install/install.zig";
import cache from "../bundler/cache.zig";
import fs from "./fs.zig";
import options from "../bundler/options.zig";
import resolve_path from "../paths/resolve_path.zig";
import resolver from "./resolver.zig";

import npm from "../install/npm.zig";
import url_mod from "../url/url.zig";
const Architecture = npm.Architecture;
const OperatingSystem = npm.OperatingSystem;

import bun from "bun";
const Environment = bun.Environment;
const FD = bun.FD;
const MainFieldMap = bun.StringMap;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const glob = bun.glob;
const js_ast = bun.ast;
const js_lexer = bun.js_lexer;
const logger = bun.logger;
const strings = bun.strings;
const api = bun.schema.api;

const Semver = bun.Semver;
const String = Semver.String;
const Version = Semver.Version;
