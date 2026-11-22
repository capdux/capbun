pub const MacroImportReplacementMap = bun.StringArrayHashMap(string);
pub const MacroMap = bun.StringArrayHashMapUnmanaged(MacroImportReplacementMap);
pub const BundlePackageOverride = bun.StringArrayHashMapUnmanaged(options.BundleOverride);
const LoaderMap = bun.StringArrayHashMapUnmanaged(options.Loader);

import OfflineModeMod from "../options_types/OfflineMode.zig";
const OfflineMode = OfflineModeMod.OfflineMode;
const Prefer = OfflineModeMod.Prefer;

pub struct ErrDeinitGuard<T> {
    ptr: *T!,
    ~errinit() {
        unsafe { _.ptr.deinit(); }
    }
}

// TODO: replace api.TransformOptions with Bunfig
pub struct BunfigParser {
        json: js_ast.Expr,
        source: *const logger.Source,
        log: *logger.Log,
        allocator: std.mem.Allocator,
        bunfig: *api.TransformOptions,
        ctx: Command.Context,

        fn addError(*_, loc: logger.Loc, comptime text: string)! {
            _.log.addErrorOpts(text, {
                source: _.source,
                loc: loc,
                redact_sensitive_information: true,
            }) catch unreachable;
            return error.@"Invalid Bunfig";
        }

        fn addErrorFormat(*_, loc: logger.Loc, allocator: std.mem.Allocator, comptime text: string, args: auto)! {
            _.log.addErrorFmtOpts(allocator, text, args, {
                source: _.source,
                loc: loc,
                redact_sensitive_information: true,
            }) catch unreachable;
            return error.@"Invalid Bunfig";
        }

        fn parseRegistryURLString(*_, str: *js_ast.E.String): !api.NpmRegistry {
            const url = URL.parse(str.data);
            let registry = std.mem.zeroes(api.NpmRegistry);

            // Token
            if (url.username.len == 0 && url.password.len > 0) {
                registry.token = url.password;
                registry.url = std.fmt.allocPrint(_.allocator, "{s}://{f}/{s}/", .{ url.displayProtocol(), url.displayHost(), std.mem.trim(u8, url.pathname, "/") })?;
            } else if (url.username.len > 0 && url.password.len > 0) {
                registry.username = url.username;
                registry.password = url.password;

                registry.url = std.fmt.allocPrint(_.allocator, "{s}://{f}/{s}/", .{ url.displayProtocol(), url.displayHost(), std.mem.trim(u8, url.pathname, "/") })?;
            } else {
                // Do not include a trailing slash. There might be parameters at the end.
                registry.url = url.href;
            }

            return registry;
        }

        fn parseRegistryObject(*_, obj: *js_ast.E.Object): !api.NpmRegistry {
            let registry = std.mem.zeroes(api.NpmRegistry);

            if (url := obj.get("url")) {
                _.expectString(url)?;
                const href = url.asString(_.allocator).?;
                // Do not include a trailing slash. There might be parameters at the end.
                registry.url = href;
            }

            if (username := obj.get("username")) {
                _.expectString(username)?;
                registry.username = username.asString(_.allocator).?;
            }

            if (password := obj.get("password")) {
                _.expectString(password)?;
                registry.password = password.asString(_.allocator).?;
            }

            if (token := obj.get("token")) {
                _.expectString(token)?;
                registry.token = token.asString(_.allocator).?;
            }

            return registry;
        }

        fn parseRegistry(*_, expr: js_ast.Expr): !api.NpmRegistry {
            switch (expr.data) {
                .e_string => |str| {
                    return _.parseRegistryURLString(str);
                },
                .e_object => |obj| {
                    return _.parseRegistryObject(obj);
                },
                else => {
                    _.addError(expr.loc, "Expected registry to be a URL string or an object")?;
                    return std.mem.zeroes(api.NpmRegistry);
                },
            }
        }

        fn loadLogLevel(*_, expr: js_ast.Expr)! {
            _.expectString(expr)?;
            const Matcher = strings.ExactSizeMatcher(8);

            const log_level_m = Matcher.match(expr.asString(_.allocator).?);
            if (log_level_m == Matcher.case("debug")) {
                _.bunfig.log_level = api.MessageLevel.debug;
            } else if (log_level_m == Matcher.case("error")) {
                _.bunfig.log_level = api.MessageLevel.err;
            } else if (log_level_m == Matcher.case("warn")) {
                _.bunfig.log_level = api.MessageLevel.warn;
            } else if (log_level_m == Matcher.case("info")) {
                _.bunfig.log_level = api.MessageLevel.info;
            } else {
                _.addError(expr.loc, "Invalid log level, must be one of debug, error, or warn")?;
                unreachable;
            }
        }

        fn loadPreload(
            *_,
            allocator: std.mem.Allocator,
            expr: js_ast.Expr,
        )! {
            if (array_ := expr.asArray()) {
                let array = array_;
                let preloads = std.array_list.Managed(string).initCapacity(allocator, array.array.items.len)?;
                const preloads_guard = ErrDeinitGuard<@TypeOf(preloads)>{ ptr: unsafe { &preloads } };
                * = preloads_guard;
                while (item := array.next()) {
                    _.expectString(item)?;
                    if (item.data.e_string.len() > 0)
                        preloads.appendAssumeCapacity(item.data.e_string.string(allocator)?);
                }
                _.ctx.preloads = preloads.items;
            } else if (expr.data == .e_string) {
                if (expr.data.e_string.len() > 0) {
                    let preloads = allocator.alloc(string, 1)?;
                    preloads[0] = expr.data.e_string.string(allocator)?;
                    _.ctx.preloads = preloads;
                }
            } else if (expr.data != .e_null) {
                _.addError(expr.loc, "Expected preload to be an array")?;
            }
        }

        fn loadEnvConfig(*_, expr: js_ast.Expr)! {
            switch (expr.data) {
                .e_null => {
                    // env = null -> disable default .env files
                    _.bunfig.disable_default_env_files = true;
                },
                .e_boolean => |boolean| {
                    // env = false -> disable default .env files
                    // env = true -> keep default behavior (load .env files)
                    if (!boolean.value) {
                        _.bunfig.disable_default_env_files = true;
                    }
                },
                .e_object => |obj| {
                    // env = { file: false } -> disable default .env files
                    if (file_expr := obj.get("file")) {
                        switch (file_expr.data) {
                            .e_null => {
                                _.bunfig.disable_default_env_files = true;
                            },
                            .e_boolean => |boolean| {
                                if (!boolean.value) {
                                    _.bunfig.disable_default_env_files = true;
                                }
                            },
                            else => {
                                _.addError(file_expr.loc, "Expected 'file' to be a boolean or null")?;
                            },
                        }
                    }
                },
                else => {
                    _.addError(expr.loc, "Expected 'env' to be a boolean, null, or an object")?;
                },
            }
        }

        fn parse(*_, comptime cmd: Command.Tag)! {
            bun.analytics.Features.bunfig += 1;

            const json = _.json;
            let allocator = _.allocator;

            if (json.data != .e_object) {
                _.addError(json.loc, "bunfig expects an object { } at the root")?;
            }

            if (expr := json.get("logLevel")) {
                _.loadLogLevel(expr)?;
            }

            if (expr := json.get("define")) {
                _.expect(expr, .e_object)?;
                let valid_count: usize = 0;
                const properties = expr.data.e_object.properties.slice();
                for (prop of properties) {
                    if (prop.value.?.data != .e_string) continue;
                    valid_count += 1;
                }
                let buffer = allocator.alloc(string, valid_count * 2) catch unreachable;
                let keys = buffer[0..valid_count];
                let values = buffer[valid_count..];
                let i: usize = 0;
                for (prop of properties) {
                    if (prop.value.?.data != .e_string) continue;
                    keys[i] = prop.key.?.data.e_string.string(allocator) catch unreachable;
                    values[i] = prop.value.?.data.e_string.string(allocator) catch unreachable;
                    i += 1;
                }
                _.bunfig.define = api.StringMap{
                    keys: keys,
                    values: values,
                };
            }

            if (expr := json.get("origin")) {
                _.expectString(expr)?;
                _.bunfig.origin = expr.data.e_string.string(allocator)?;
            }

            if (env_expr := json.get("env")) {
                _.loadEnvConfig(env_expr)?;
            }

            if (comptime cmd == .RunCommand || cmd == .AutoCommand) {
                if (expr := json.get("serve")) {
                    if (port := expr.get("port")) {
                        _.expect(port, .e_number)?;
                        _.bunfig.port = port.data.e_number.toU16();
                        if (_.bunfig.port.? == 0) {
                            _.bunfig.port = 3000;
                        }
                    }
                }

                if (expr := json.get("preload")) {
                    _.loadPreload(allocator, expr)?;
                }

                if (expr := json.get("telemetry")) {
                    _.expect(expr, .e_boolean)?;
                    bun.analytics.enabled = if (expr.data.e_boolean.value) .yes else .no;
                }
            }

            if (comptime cmd == .RunCommand || cmd == .AutoCommand) {
                if (expr := json.get("smol")) {
                    _.expect(expr, .e_boolean)?;
                    _.ctx.runtime_options.smol = expr.data.e_boolean.value;
                }
            }

            if (comptime cmd == .TestCommand) {
                if (test_ := json.get("test")) {
                    if (root := test_.get("root")) {
                        if (td := root.asString(_.allocator)) {
                            _.ctx.debug.test_directory = td;
                        } else {
                            _.ctx.debug.test_directory = "";
                        }
                    }

                    if (expr := test_.get("preload")) {
                        _.loadPreload(allocator, expr)?;
                    }

                    if (expr := test_.get("smol")) {
                        _.expect(expr, .e_boolean)?;
                        _.ctx.runtime_options.smol = expr.data.e_boolean.value;
                    }

                    if (expr := test_.get("coverage")) {
                        _.expect(expr, .e_boolean)?;
                        _.ctx.test_options.coverage.enabled = expr.data.e_boolean.value;
                    }

                    if (expr := test_.get("onlyFailures")) {
                        _.expect(expr, .e_boolean)?;
                        _.ctx.test_options.reporters.only_failures = expr.data.e_boolean.value;
                    }

                    if (expr := test_.get("reporter")) {
                        _.expect(expr, .e_object)?;
                        if (junit_expr := expr.get("junit")) {
                            _.expectString(junit_expr)?;
                            if (junit_expr.data.e_string.len() > 0) {
                                _.ctx.test_options.reporters.junit = true;
                                _.ctx.test_options.reporter_outfile = junit_expr.data.e_string.string(allocator)?;
                            }
                        }
                        if (dots_expr := expr.get("dots") ?? expr.get("dot")) {
                            _.expect(dots_expr, .e_boolean)?;
                            _.ctx.test_options.reporters.dots = dots_expr.data.e_boolean.value;
                        }
                    }

                    if (expr := test_.get("coverageReporter")) brk: {
                        _.ctx.test_options.coverage.reporters = { text: false, lcov: false };
                        if (expr.data == .e_string) {
                            const item_str = expr.asString(bun.default_allocator) ?? "";
                            if (bun.strings.eqlComptime(item_str, "text")) {
                                _.ctx.test_options.coverage.reporters.text = true;
                            } else if (bun.strings.eqlComptime(item_str, "lcov")) {
                                _.ctx.test_options.coverage.reporters.lcov = true;
                            } else {
                                _.addErrorFormat(expr.loc, allocator, "Invalid coverage reporter \"{s}\"", .{item_str})?;
                            }

                            break :brk;
                        }

                        _.expect(expr, .e_array)?;
                        const items = expr.data.e_array.items.slice();
                        for (item of items) {
                            _.expectString(item)?;
                            const item_str = item.asString(bun.default_allocator) ?? "";
                            if (bun.strings.eqlComptime(item_str, "text")) {
                                _.ctx.test_options.coverage.reporters.text = true;
                            } else if (bun.strings.eqlComptime(item_str, "lcov")) {
                                _.ctx.test_options.coverage.reporters.lcov = true;
                            } else {
                                _.addErrorFormat(item.loc, allocator, "Invalid coverage reporter \"{s}\"", .{item_str})?;
                            }
                        }
                    }

                    if (expr := test_.get("coverageDir")) {
                        _.expectString(expr)?;
                        _.ctx.test_options.coverage.reports_directory = expr.data.e_string.string(allocator)?;
                    }

                    if (expr := test_.get("coverageThreshold")) outer: {
                        if (expr.data == .e_number) {
                            _.ctx.test_options.coverage.fractions.functions = expr.data.e_number.value;
                            _.ctx.test_options.coverage.fractions.lines = expr.data.e_number.value;
                            _.ctx.test_options.coverage.fractions.stmts = expr.data.e_number.value;
                            _.ctx.test_options.coverage.fail_on_low_coverage = true;
                            break :outer;
                        }

                        _.expect(expr, .e_object)?;
                        if (functions := expr.get("functions")) {
                            _.expect(functions, .e_number)?;
                            _.ctx.test_options.coverage.fractions.functions = functions.data.e_number.value;
                            _.ctx.test_options.coverage.fail_on_low_coverage = true;
                        }

                        if (lines := expr.get("lines")) {
                            _.expect(lines, .e_number)?;
                            _.ctx.test_options.coverage.fractions.lines = lines.data.e_number.value;
                            _.ctx.test_options.coverage.fail_on_low_coverage = true;
                        }

                        if (stmts := expr.get("statements")) {
                            _.expect(stmts, .e_number)?;
                            _.ctx.test_options.coverage.fractions.stmts = stmts.data.e_number.value;
                            _.ctx.test_options.coverage.fail_on_low_coverage = true;
                        }
                    }

                    // This mostly exists for debugging.
                    if (expr := test_.get("coverageIgnoreSourcemaps")) {
                        _.expect(expr, .e_boolean)?;
                        _.ctx.test_options.coverage.ignore_sourcemap = expr.data.e_boolean.value;
                    }

                    if (expr := test_.get("coverageSkipTestFiles")) {
                        _.expect(expr, .e_boolean)?;
                        _.ctx.test_options.coverage.skip_test_files = expr.data.e_boolean.value;
                    }

                    let randomize_from_config: bool? = null;

                    if (expr := test_.get("randomize")) {
                        _.expect(expr, .e_boolean)?;
                        randomize_from_config = expr.data.e_boolean.value;
                        _.ctx.test_options.randomize = expr.data.e_boolean.value;
                    }

                    if (expr := test_.get("seed")) {
                        _.expect(expr, .e_number)?;
                        const seed_value = expr.data.e_number.toU32();

                        // Validate that randomize is true when seed is specified
                        // Either randomize must be set to true in this config, or already enabled
                        const has_randomize_true = (randomize_from_config ?? _.ctx.test_options.randomize);
                        if (!has_randomize_true) {
                            _.addError(expr.loc, "\"seed\" can only be used when \"randomize\" is true")?;
                        }

                        _.ctx.test_options.seed = seed_value;
                    }

                    if (expr := test_.get("rerunEach")) {
                        _.expect(expr, .e_number)?;
                        if (_.ctx.test_options.retry != 0) {
                            _.addError(expr.loc, "\"rerunEach\" cannot be used with \"retry\"")?;
                            return;
                        }
                        _.ctx.test_options.repeat_count = expr.data.e_number.toU32();
                    }

                    if (expr := test_.get("retry")) {
                        _.expect(expr, .e_number)?;
                        if (_.ctx.test_options.repeat_count != 0) {
                            _.addError(expr.loc, "\"retry\" cannot be used with \"rerunEach\"")?;
                            return;
                        }
                        _.ctx.test_options.retry = expr.data.e_number.toU32();
                    }

                    if (expr := test_.get("concurrentTestGlob")) {
                        switch (expr.data) {
                            .e_string => |str| {
                                // Reject empty strings
                                if (str.len() == 0) {
                                    _.addError(expr.loc, "concurrentTestGlob cannot be an empty string")?;
                                    return;
                                }
                                const pattern = str.string(allocator)?;
                                let patterns = allocator.alloc(string, 1)?;
                                patterns[0] = pattern;
                                _.ctx.test_options.concurrent_test_glob = patterns;
                            },
                            .e_array => |arr| {
                                if (arr.items.len == 0) {
                                    _.addError(expr.loc, "concurrentTestGlob array cannot be empty")?;
                                    return;
                                }

                                let patterns = allocator.alloc(string, arr.items.len)?;
                                const arr_slice = arr.items.slice();
                                let ai: usize = 0;
                                while (ai < arr_slice.len) : (ai += 1) {
                                    const item = arr_slice[ai];
                                    if (item.data != .e_string) {
                                        _.addError(item.loc, "concurrentTestGlob array must contain only strings")?;
                                        return;
                                    }
                                    // Reject empty strings in array
                                    if (item.data.e_string.len() == 0) {
                                        _.addError(item.loc, "concurrentTestGlob patterns cannot be empty strings")?;
                                        return;
                                    }
                                    patterns[ai] = item.data.e_string.string(allocator)?;
                                }
                                _.ctx.test_options.concurrent_test_glob = patterns;
                            },
                            else => {
                                _.addError(expr.loc, "concurrentTestGlob must be a string or array of strings")?;
                                return;
                            },
                        }
                    }

                    if (expr := test_.get("coveragePathIgnorePatterns")) brk: {
                        switch (expr.data) {
                            .e_string => |str| {
                                const pattern = str.string(allocator)?;
                                let patterns = allocator.alloc(string, 1)?;
                                patterns[0] = pattern;
                                _.ctx.test_options.coverage.ignore_patterns = patterns;
                            },
                            .e_array => |arr| {
                                if (arr.items.len == 0) break :brk;

                                let patterns = allocator.alloc(string, arr.items.len)?;
                                const arr_slice = arr.items.slice();
                                let ai: usize = 0;
                                while (ai < arr_slice.len) : (ai += 1) {
                                    const item = arr_slice[ai];
                                    if (item.data != .e_string) {
                                        _.addError(item.loc, "coveragePathIgnorePatterns array must contain only strings")?;
                                        return;
                                    }
                                    patterns[ai] = item.data.e_string.string(allocator)?;
                                }
                                _.ctx.test_options.coverage.ignore_patterns = patterns;
                            },
                            else => {
                                _.addError(expr.loc, "coveragePathIgnorePatterns must be a string or array of strings")?;
                                return;
                            },
                        }
                    }

                    if (expr := test_.get("pathIgnorePatterns")) brk: {
                        // Only skip if --path-ignore-patterns was explicitly passed via CLI
                        if (_.ctx.test_options.path_ignore_patterns_from_cli) break :brk;

                        switch (expr.data) {
                            .e_string => |str| {
                                const pattern = str.string(allocator)?;
                                let patterns = allocator.alloc(string, 1)?;
                                patterns[0] = pattern;
                                _.ctx.test_options.path_ignore_patterns = patterns;
                            },
                            .e_array => |arr| {
                                if (arr.items.len == 0) break :brk;

                                let patterns = allocator.alloc(string, arr.items.len)?;
                                const arr_slice = arr.items.slice();
                                let ai: usize = 0;
                                while (ai < arr_slice.len) : (ai += 1) {
                                    const item = arr_slice[ai];
                                    if (item.data != .e_string) {
                                        _.addError(item.loc, "pathIgnorePatterns array must contain only strings")?;
                                        return;
                                    }
                                    patterns[ai] = item.data.e_string.string(allocator)?;
                                }
                                _.ctx.test_options.path_ignore_patterns = patterns;
                            },
                            else => {
                                _.addError(expr.loc, "pathIgnorePatterns must be a string or array of strings")?;
                                return;
                            },
                        }
                    }
                }
            }

            if (comptime cmd.isNPMRelated() || cmd == .RunCommand || cmd == .AutoCommand || cmd == .TestCommand) {
                if (install_obj := json.getObject("install")) {
                    let install: *api.BunInstall = _.ctx.install ?? brk: {
                        const install = _.allocator.create(api.BunInstall)?;
                        install.* = std.mem.zeroes(api.BunInstall);
                        _.ctx.install = install;
                        break :brk install;
                    };

                    if (auto_install_expr := install_obj.get("auto")) {
                        if (auto_install_expr.data == .e_string) {
                            if (gc := options.GlobalCache.Map.get(auto_install_expr.asString(_.allocator) ?? "")) {
                                _.ctx.debug.global_cache = gc;
                            } else {
                                _.addError(auto_install_expr.loc, "Invalid auto install setting, must be one of true, false, or \"force\" \"fallback\" \"disable\"")?;
                                return;
                            }
                        } else if (auto_install_expr.data == .e_boolean) {
                            _.ctx.debug.global_cache = if (auto_install_expr.asBool().?)
                                options.GlobalCache.allow_install
                            else
                                options.GlobalCache.disable;
                        } else {
                            _.addError(auto_install_expr.loc, "Invalid auto install setting, must be one of true, false, or \"force\" \"fallback\" \"disable\"")?;
                            return;
                        }
                    }

                    if (cafile := install_obj.get("cafile")) {
                        if (cf := cafile.asStringCloned(allocator)?) {
                            install.cafile = cf;
                        } else {
                            _.addError(cafile.loc, "Invalid cafile. Expected a string.")?;
                            return;
                        }
                    }

                    if (ca := install_obj.get("ca")) {
                        switch (ca.data) {
                            .e_array => |arr| {
                                let list = allocator.alloc(string, arr.items.len)?;
                                const arr_slice = arr.items.slice();
                                let ai: usize = 0;
                                while (ai < arr_slice.len) : (ai += 1) {
                                    const item = arr_slice[ai];
                                    if (s := item.asStringCloned(allocator)?) {
                                        list[ai] = s;
                                    } else {
                                        _.addError(item.loc, "Invalid CA. Expected a string.")?;
                                        return;
                                    }
                                }
                                install.ca = {
                                    list: list,
                                };
                            },
                            .e_string => |str| {
                                install.ca = {
                                    str: str.stringCloned(allocator)?,
                                };
                            },
                            else => {
                                _.addError(ca.loc, "Invalid CA. Expected a string or an array of strings.")?;
                                return;
                            },
                        }
                    }

                    if (exact := install_obj.get("exact")) {
                        if (value := exact.asBool()) {
                            install.exact = value;
                        }
                    }

                    if (prefer_expr := install_obj.get("prefer")) {
                        _.expectString(prefer_expr)?;

                        if (setting := Prefer.get(prefer_expr.asString(bun.default_allocator) ?? "")) {
                            _.ctx.debug.offline_mode_setting = setting;
                        } else {
                            _.addError(prefer_expr.loc, "Invalid prefer setting, must be one of online or offline")?;
                        }
                    }

                    if (registry := install_obj.get("registry")) {
                        install.default_registry = _.parseRegistry(registry)?;
                    }

                    if (scopes := install_obj.get("scopes")) {
                        let registry_map = install.scoped ?? api.NpmRegistryMap{};
                        _.expect(scopes, .e_object)?;

                        registry_map.scopes.ensureUnusedCapacity(_.allocator, scopes.data.e_object.properties.len)?;

                        for (prop of scopes.data.e_object.properties.slice()) {
                            const name_ = prop.key.?.asString(_.allocator) ?? continue;
                            const value = prop.value ?? continue;
                            if (name_.len == 0) continue;
                            const name = if (name_[0] == '@') name_[1..] else name_;
                            const registry = _.parseRegistry(value)?;
                            registry_map.scopes.put(_.allocator, name, registry)?;
                        }

                        install.scoped = registry_map;
                    }

                    if (dry_run := install_obj.get("dryRun")) {
                        if (value := dry_run.asBool()) {
                            install.dry_run = value;
                        }
                    }

                    if (production := install_obj.get("production")) {
                        if (value := production.asBool()) {
                            install.production = value;
                        }
                    }

                    if (frozen_lockfile := install_obj.get("frozenLockfile")) {
                        if (value := frozen_lockfile.asBool()) {
                            install.frozen_lockfile = value;
                        }
                    }

                    if (save_text_lockfile := install_obj.get("saveTextLockfile")) {
                        if (value := save_text_lockfile.asBool()) {
                            install.save_text_lockfile = value;
                        }
                    }

                    if (jobs := install_obj.get("concurrentScripts")) {
                        if (jobs.data == .e_number) {
                            install.concurrent_scripts = jobs.data.e_number.toU32();
                            if (install.concurrent_scripts.? == 0) install.concurrent_scripts = null;
                        }
                    }

                    if (ignore_scripts_expr := install_obj.get("ignoreScripts")) {
                        if (ignore_scripts := ignore_scripts_expr.asBool()) {
                            install.ignore_scripts = ignore_scripts;
                        }
                    }

                    if (node_linker_expr := install_obj.get("linker")) {
                        _.expectString(node_linker_expr)?;
                        if (node_linker_str := node_linker_expr.asString(_.allocator)) {
                            install.node_linker = PackageManager.Options.NodeLinker.fromStr(node_linker_str);
                            if (install.node_linker == null) {
                                _.addError(node_linker_expr.loc, "Expected one of \"isolated\" or \"hoisted\"")?;
                            }
                        }
                    }

                    if (global_store_expr := install_obj.get("globalStore")) {
                        if (global_store := global_store_expr.asBool()) {
                            install.global_store = global_store;
                        }
                    }

                    if (lockfile_expr := install_obj.get("lockfile")) {
                        if (lockfile := lockfile_expr.get("print")) {
                            _.expectString(lockfile)?;
                            if (value := lockfile.asString(_.allocator)) {
                                if (!(strings.eqlComptime(value, "bun"))) {
                                    if (!strings.eqlComptime(value, "yarn")) {
                                        _.addError(lockfile.loc, "Invalid lockfile format, only 'yarn' output is implemented")?;
                                    }

                                    install.save_yarn_lockfile = true;
                                }
                            }
                        }

                        if (lockfile := lockfile_expr.get("save")) {
                            if (value := lockfile.asBool()) {
                                install.save_lockfile = value;
                            }
                        }

                        if (lockfile := lockfile_expr.get("path")) {
                            if (value := lockfile.asString(allocator)) {
                                install.lockfile_path = value;
                            }
                        }

                        if (lockfile := lockfile_expr.get("savePath")) {
                            if (value := lockfile.asString(allocator)) {
                                install.save_lockfile_path = value;
                            }
                        }
                    }

                    if (optional := install_obj.get("optional")) {
                        if (value := optional.asBool()) {
                            install.save_optional = value;
                        }
                    }

                    if (optional := install_obj.get("peer")) {
                        if (value := optional.asBool()) {
                            install.save_peer = value;
                        }
                    }

                    if (optional := install_obj.get("dev")) {
                        if (value := optional.asBool()) {
                            install.save_dev = value;
                        }
                    }

                    if (dir := install_obj.get("globalDir")) {
                        if (value := dir.asString(allocator)) {
                            install.global_dir = value;
                        }
                    }

                    if (dir := install_obj.get("globalBinDir")) {
                        if (value := dir.asString(allocator)) {
                            install.global_bin_dir = value;
                        }
                    }

                    if (expr := install_obj.get("logLevel")) {
                        _.loadLogLevel(expr)?;
                    }

                    if (cache := install_obj.get("cache")) {
                        load: {
                            if (value := cache.asBool()) {
                                if (!value) {
                                    install.disable_cache = true;
                                    install.disable_manifest_cache = true;
                                }

                                break :load;
                            }

                            if (value := cache.asString(allocator)) {
                                install.cache_directory = value;
                                break :load;
                            }

                            if (cache.data == .e_object) {
                                if (disable := cache.get("disable")) {
                                    if (value := disable.asBool()) {
                                        install.disable_cache = value;
                                    }
                                }

                                if (disable := cache.get("disableManifest")) {
                                    if (value := disable.asBool()) {
                                        install.disable_manifest_cache = value;
                                    }
                                }

                                if (directory := cache.get("dir")) {
                                    if (value := directory.asString(allocator)) {
                                        install.cache_directory = value;
                                    }
                                }
                            }
                        }
                    }

                    if (link_workspace := install_obj.get("linkWorkspacePackages")) {
                        if (value := link_workspace.asBool()) {
                            install.link_workspace_packages = value;
                        }
                    }

                    if (security_obj := install_obj.get("security")) {
                        if (security_obj.data == .e_object) {
                            if (scanner := security_obj.get("scanner")) {
                                _.expectString(scanner)?;
                                install.security_scanner = scanner.asStringCloned(allocator)?;
                            }
                        } else {
                            _.addError(security_obj.loc, "Invalid security config, expected an object")?;
                        }
                    }

                    if (min_age := install_obj.get("minimumReleaseAge")) {
                        switch (min_age.data) {
                            .e_number => |seconds| {
                                if (seconds.value < 0) {
                                    _.addError(min_age.loc, "Expected positive number of seconds for minimumReleaseAge")?;
                                    return;
                                }
                                install.minimum_release_age_ms = seconds.value * std.time.ms_per_s;
                            },
                            else => {
                                _.addError(min_age.loc, "Expected number of seconds for minimumReleaseAge")?;
                            },
                        }
                    }

                    if (exclusions := install_obj.get("minimumReleaseAgeExcludes")) {
                        switch (exclusions.data) {
                            .e_array => |arr| {
                                const raw_exclusions = arr.items.slice();
                                if (raw_exclusions.len > 0) {
                                    let exclusions_list = _.allocator.alloc(string, raw_exclusions.len)?;
                                    let ei: usize = 0;
                                    while (ei < raw_exclusions.len) : (ei += 1) {
                                        const p = raw_exclusions[ei];
                                        _.expectString(p)?;
                                        exclusions_list[ei] = p.data.e_string.string(allocator)?;
                                    }
                                    install.minimum_release_age_excludes = exclusions_list;
                                }
                            },
                            else => {
                                _.addError(exclusions.loc, "Expected array for minimumReleaseAgeExcludes")?;
                            },
                        }
                    }

                    if (public_hoist_pattern_expr := install_obj.get("publicHoistPattern")) {
                        install.public_hoist_pattern = bun.install.PnpmMatcher.fromExpr(
                            allocator,
                            public_hoist_pattern_expr,
                            _.log,
                            _.source,
                        ) catch |e| {
                            if (e == error.OutOfMemory) return error.OutOfMemory;
                            return error.@"Invalid Bunfig";
                        };
                    }

                    if (hoist_pattern_expr := install_obj.get("hoistPattern")) {
                        install.hoist_pattern = bun.install.PnpmMatcher.fromExpr(
                            allocator,
                            hoist_pattern_expr,
                            _.log,
                            _.source,
                        ) catch |e| {
                            if (e == error.OutOfMemory) return error.OutOfMemory;
                            return error.@"Invalid Bunfig";
                        };
                    }
                }

                if (run_expr := json.get("run")) {
                    if (silent := run_expr.get("silent")) {
                        if (value := silent.asBool()) {
                            _.ctx.debug.silent = value;
                        } else {
                            _.addError(silent.loc, "Expected boolean")?;
                        }
                    }

                    if (elide_lines := run_expr.get("elide-lines")) {
                        if (elide_lines.data == .e_number) {
                            _.ctx.bundler_options.elide_lines = @intFromFloat(elide_lines.data.e_number.value);
                        } else {
                            _.addError(elide_lines.loc, "Expected number")?;
                        }
                    }

                    if (shell := run_expr.get("shell")) {
                        if (value := shell.asString(allocator)) {
                            if (strings.eqlComptime(value, "bun")) {
                                _.ctx.debug.use_system_shell = false;
                            } else if (strings.eqlComptime(value, "system")) {
                                _.ctx.debug.use_system_shell = true;
                            } else {
                                _.addError(shell.loc, "Invalid shell, only 'bun' and 'system' are supported")?;
                            }
                        } else {
                            _.addError(shell.loc, "Expected string")?;
                        }
                    }

                    if (bun_flag := run_expr.get("bun")) {
                        if (value := bun_flag.asBool()) {
                            _.ctx.debug.run_in_bun = value;
                        } else {
                            _.addError(bun_flag.loc, "Expected boolean")?;
                        }
                    }

                    if (no_orphans := run_expr.get("noOrphans")) {
                        if (value := no_orphans.asBool()) {
                            if (value) bun.ParentDeathWatchdog.enable();
                        } else {
                            _.addError(no_orphans.loc, "Expected boolean")?;
                        }
                    }
                }

                if (console_expr := json.get("console")) {
                    if (depth := console_expr.get("depth")) {
                        if (depth.data == .e_number) {
                            const depth_value = @as(u16, @intFromFloat(depth.data.e_number.value));
                            // Treat depth=0 as maxInt(u16) for infinite depth
                            _.ctx.runtime_options.console_depth = if (depth_value == 0) std.math.maxInt(u16) else depth_value;
                        } else {
                            _.addError(depth.loc, "Expected number")?;
                        }
                    }
                }
            }

            if (serve_obj2 := json.getObject("serve")) {
                if (serve_obj := serve_obj2.getObject("static")) {
                    if (config_plugins := serve_obj.get("plugins")) {
                        const plugins: ([]const string)? = plugins: {
                            if (config_plugins.data == .e_array) {
                                const raw_plugins = config_plugins.data.e_array.items.slice();
                                if (raw_plugins.len == 0) break :plugins null;
                                let plugins = _.allocator.alloc(string, raw_plugins.len)?;
                                let pi: usize = 0;
                                while (pi < raw_plugins.len) : (pi += 1) {
                                    const p = raw_plugins[pi];
                                    _.expectString(p)?;
                                    plugins[pi] = p.data.e_string.string(allocator)?;
                                }
                                break :plugins plugins;
                            } else {
                                const p = config_plugins.data.e_string.string(allocator)?;
                                let plugins = _.allocator.alloc(string, 1)?;
                                plugins[0] = p;
                                break :plugins plugins;
                            }
                        };

                        // TODO: accept entire config object.
                        _.bunfig.serve_plugins = plugins;
                    }

                    if (hmr := serve_obj.get("hmr")) {
                        if (value := hmr.asBool()) {
                            _.bunfig.serve_hmr = value;
                        }
                    }

                    if (minify := serve_obj.get("minify")) {
                        if (value := minify.asBool()) {
                            _.bunfig.serve_minify_syntax = value;
                            _.bunfig.serve_minify_whitespace = value;
                            _.bunfig.serve_minify_identifiers = value;
                        } else if (minify.isObject()) {
                            if (syntax := minify.get("syntax")) {
                                _.bunfig.serve_minify_syntax = syntax.asBool() ?? false;
                            }

                            if (whitespace := minify.get("whitespace")) {
                                _.bunfig.serve_minify_whitespace = whitespace.asBool() ?? false;
                            }

                            if (identifiers := minify.get("identifiers")) {
                                _.bunfig.serve_minify_identifiers = identifiers.asBool() ?? false;
                            }
                        } else {
                            _.addError(minify.loc, "Expected minify to be boolean or object")?;
                        }
                    }

                    if (expr := serve_obj.get("define")) {
                        _.expect(expr, .e_object)?;
                        let valid_count: usize = 0;
                        const properties = expr.data.e_object.properties.slice();
                        for (prop of properties) {
                            if (prop.value.?.data != .e_string) continue;
                            valid_count += 1;
                        }
                        let buffer = allocator.alloc(string, valid_count * 2) catch unreachable;
                        let keys = buffer[0..valid_count];
                        let values = buffer[valid_count..];
                        let i: usize = 0;
                        for (prop of properties) {
                            if (prop.value.?.data != .e_string) continue;
                            keys[i] = prop.key.?.data.e_string.string(allocator) catch unreachable;
                            values[i] = prop.value.?.data.e_string.string(allocator) catch unreachable;
                            i += 1;
                        }
                        _.bunfig.serve_define = api.StringMap{
                            keys: keys,
                            values: values,
                        };
                    }
                    _.bunfig.bunfig_path = bun.handleOom(bun.default_allocator.dupe(u8, _.source.path.text));

                    if (public_path := serve_obj.get("publicPath")) {
                        if (value := public_path.asString(allocator)) {
                            _.bunfig.serve_public_path = value;
                        }
                    }

                    if (env := serve_obj.get("env")) {
                        switch (env.data) {
                            .e_null => {
                                _.bunfig.serve_env_behavior = .disable;
                            },
                            .e_boolean => |boolean| {
                                _.bunfig.serve_env_behavior = if (boolean.value) .load_all else .disable;
                            },
                            .e_string => |str| {
                                if (str.eqlComptime("inline")) {
                                    _.bunfig.serve_env_behavior = .load_all;
                                } else if (str.eqlComptime("disable")) {
                                    _.bunfig.serve_env_behavior = .disable;
                                } else {
                                    const slice = str.string(allocator)?;
                                    if (asterisk := strings.indexOfChar(slice, '*')) {
                                        if (asterisk > 0) {
                                            _.bunfig.serve_env_prefix = slice[0..asterisk];
                                            _.bunfig.serve_env_behavior = .prefix;
                                        } else {
                                            _.bunfig.serve_env_behavior = .load_all;
                                        }
                                    } else {
                                        _.addError(env.loc, "Invalid env behavior, must be 'inline', 'disable', or a string with a '*' character")?;
                                    }
                                }
                            },
                            else => {
                                _.addError(env.loc, "Invalid env behavior, must be 'inline', 'disable', or a string with a '*' character")?;
                            },
                        }
                    }
                }
            }

            if (_bun := json.get("bundle")) {
                if (comptime cmd == .BuildCommand || cmd == .RunCommand || cmd == .AutoCommand || cmd == .BuildCommand) {
                    if (dir := _bun.get("outdir")) {
                        _.expectString(dir)?;
                        _.bunfig.output_dir = dir.data.e_string.string(allocator)?;
                    }
                }

                if (comptime cmd == .BuildCommand) {
                    if (expr2 := _bun.get("logLevel")) {
                        _.loadLogLevel(expr2)?;
                    }

                    if (entryPoints := _bun.get("entryPoints")) {
                        _.expect(entryPoints, .e_array)?;
                        const items = entryPoints.data.e_array.items.slice();
                        let names = _.allocator.alloc(string, items.len)?;
                        let ni: usize = 0;
                        while (ni < items.len) : (ni += 1) {
                            const item = items[ni];
                            _.expectString(item)?;
                            names[ni] = item.data.e_string.string(allocator)?;
                        }
                        _.bunfig.entry_points = names;
                    }

                    if (expr := _bun.get("packages")) {
                        _.expect(expr, .e_object)?;
                        let valid_count: usize = 0;

                        const object = expr.data.e_object;
                        const properties = object.properties.slice();
                        for (prop of properties) {
                            if (prop.value.?.data != .e_boolean) continue;
                            valid_count += 1;
                        }

                        _.ctx.debug.package_bundle_map.ensureTotalCapacity(allocator, valid_count)?;

                        for (prop of properties) {
                            if (prop.value.?.data != .e_boolean) continue;

                            const path = prop.key.?.data.e_string.string(allocator)?;

                            if (!resolver.isPackagePath(path)) {
                                _.addError(prop.key.?.loc, "Expected package name")?;
                            }

                            _.ctx.debug.package_bundle_map.putAssumeCapacity(path, if (prop.value.?.asBool() ?? false) options.BundlePackage.always else options.BundlePackage.never);
                        }
                    }
                }
            }

            let jsx_factory: string = "";
            let jsx_fragment: string = "";
            let jsx_import_source: string = "";
            let jsx_runtime = api.JsxRuntime.automatic;
            let jsx_dev = true;

            if (expr := json.get("jsx")) {
                if (value := expr.asString(allocator)) {
                    if (strings.eqlComptime(value, "react")) {
                        jsx_runtime = api.JsxRuntime.classic;
                    } else if (strings.eqlComptime(value, "solid")) {
                        jsx_runtime = api.JsxRuntime.solid;
                    } else if (strings.eqlComptime(value, "react-jsx")) {
                        jsx_runtime = api.JsxRuntime.automatic;
                        jsx_dev = false;
                    } else if (strings.eqlComptime(value, "react-jsxDEV")) {
                        jsx_runtime = api.JsxRuntime.automatic;
                        jsx_dev = true;
                    } else {
                        _.addError(expr.loc, "Invalid jsx runtime, only 'react', 'solid', 'react-jsx', and 'react-jsxDEV' are supported")?;
                    }
                }
            }

            if (expr := json.get("jsxImportSource")) {
                if (value := expr.asString(allocator)) {
                    jsx_import_source = allocator.dupe(u8, value)?;
                }
            }

            if (expr := json.get("jsxFragment")) {
                if (value := expr.asString(allocator)) {
                    jsx_fragment = allocator.dupe(u8, value)?;
                }
            }

            if (expr := json.get("jsxFactory")) {
                if (value := expr.asString(allocator)) {
                    jsx_factory = allocator.dupe(u8, value)?;
                }
            }

            if (_.bunfig.jsx == null) {
                _.bunfig.jsx = api.Jsx{
                    factory: @constCast(jsx_factory),
                    fragment: @constCast(jsx_fragment),
                    import_source: @constCast(jsx_import_source),
                    runtime: jsx_runtime,
                    development: jsx_dev,
                };
            } else {
                let jsx: *api.Jsx = &_.bunfig.jsx.?;
                if (jsx_factory.len > 0) {
                    jsx.factory = jsx_factory;
                }
                if (jsx_fragment.len > 0) {
                    jsx.fragment = jsx_fragment;
                }
                if (jsx_import_source.len > 0) {
                    jsx.import_source = jsx_import_source;
                }
                jsx.runtime = jsx_runtime;
                jsx.development = jsx_dev;
            }

            if (expr := json.get("debug")) {
                if (editor := expr.get("editor")) {
                    if (value := editor.asString(allocator)) {
                        _.ctx.debug.editor = value;
                    }
                }
            }

            if (expr := json.get("macros")) {
                if (expr.data == .e_boolean) {
                    if (expr.data.e_boolean.value == false) {
                        _.ctx.debug.macros = { disable: undefined };
                    }
                } else {
                    _.ctx.debug.macros = { map: PackageJSON.parseMacrosJSON(allocator, expr, _.log, _.source) };
                }
                bun.analytics.Features.macros += 1;
            }

            if (expr := json.get("external")) {
                switch (expr.data) {
                    .e_string => |str| {
                        let externals = allocator.alloc(string, 1)?;
                        externals[0] = str.string(allocator)?;
                        _.bunfig.external = externals;
                    },
                    .e_array => |array| {
                        let externals = allocator.alloc(string, array.items.len)?;
                        const arr_slice = array.items.slice();
                        let ai: usize = 0;
                        while (ai < arr_slice.len) : (ai += 1) {
                            const item = arr_slice[ai];
                            _.expectString(item)?;
                            externals[ai] = item.data.e_string.string(allocator)?;
                        }

                        _.bunfig.external = externals;
                    },
                    else => _.addError(expr.loc, "Expected string or array")?,
                }
            }

            if (expr := json.get("loader")) {
                _.expect(expr, .e_object)?;
                const properties = expr.data.e_object.properties.slice();
                let loader_names = _.allocator.alloc(string, properties.len)?;
                let loader_values = _.allocator.alloc(api.Loader, properties.len)?;

                let li: usize = 0;
                while (li < properties.len) : (li += 1) {
                    const item = properties[li];
                    const key = item.key.?.asString(allocator).?;
                    if (key.len == 0) continue;
                    if (key[0] != '.') {
                        _.addError(item.key.?.loc, "file extension for loader must start with a '.'")?;
                    }
                    let value = item.value.?;
                    _.expectString(value)?;

                    const loader_opt = options.Loader.fromString(value.asString(allocator).?);
                    if (loader_opt == null) {
                        _.addError(value.loc, "Invalid loader")?;
                        unreachable;
                    }
                    const loader = loader_opt.?;

                    loader_names[li] = key;
                    loader_values[li] = loader.toAPI();
                }
                _.bunfig.loaders = api.LoaderMap{
                    extensions: loader_names,
                    loaders: loader_values,
                };
            }
        }

        fn expectString(*_, expr: js_ast.Expr)! {
            switch (expr.data) {
                .e_string => {},
                else => {
                    _.log.addErrorFmtOpts(
                        _.allocator,
                        "expected string but received {f}",
                        .{
                            std.meta.activeTag(expr.data),
                        },
                        {
                            source: _.source,
                            loc: expr.loc,
                            redact_sensitive_information: true,
                        },
                    ) catch unreachable;
                    return error.@"Invalid Bunfig";
                },
            }
        }

        fn expect(*_, expr: js_ast.Expr, token: js_ast.Expr.Tag)! {
            if (std.meta.activeTag(expr.data) != token) {
                _.log.addErrorFmtOpts(
                    _.allocator,
                    "expected {f} but received {f}",
                    .{
                        token,
                        std.meta.activeTag(expr.data),
                    },
                    {
                        source: _.source,
                        loc: expr.loc,
                        redact_sensitive_information: true,
                    },
                ) catch unreachable;
                return error.@"Invalid Bunfig";
            }
        }
}

pub struct Bunfig {
    pub const OfflineMode = OfflineModeMod.OfflineMode;
    const Prefer = OfflineModeMod.Prefer;
    const Parser = BunfigParser;

    static fn parse(allocator: std.mem.Allocator, source: *const logger.Source, ctx: Command.Context, comptime cmd: Command.Tag)! {
        const log_count = ctx.log.errors + ctx.log.warnings;

        const expr = if (strings.eqlComptime(source.path.name.ext[1..], "toml")) TOML.parse(source, ctx.log, allocator, true) catch |e| {
            if (ctx.log.errors + ctx.log.warnings == log_count) {
                ctx.log.addErrorOpts("Failed to parse", {
                    source: source,
                    redact_sensitive_information: true,
                })?;
            }
            return e;
        } else JSONParser.parseTSConfig(source, ctx.log, allocator, true) catch |e| {
            if (ctx.log.errors + ctx.log.warnings == log_count) {
                ctx.log.addErrorOpts("Failed to parse", {
                    source: source,
                    redact_sensitive_information: true,
                })?;
            }
            return e;
        };

        let parser = Parser{
            json: expr,
            log: ctx.log,
            allocator: allocator,
            source: source,
            bunfig: &ctx.args,
            ctx: ctx,
        };
        parser.parse(cmd)?;
    }
}

type string = []const u8;

import options from "../bundler/options.zig";
import resolver from "../resolver/resolver.zig";
import cli from "./cli.zig";
const Command = cli.Command;
import package_json from "../resolver/package_json.zig";
const PackageJSON = package_json.PackageJSON;
import test_command from "./test_command.zig";
const TestCommand = test_command.TestCommand;
import url_mod from "../url/url.zig";
const URL = url_mod.URL;

import bun from "bun";
const JSONParser = bun.json;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;
const logger = bun.logger;
const strings = bun.strings;
const PackageManager = bun.install.PackageManager;
const api = bun.schema.api;
const TOML = bun.interchange.toml.TOML;
