import Fs from "../resolver/fs.zig";
import * as url_mod from "../url/url.zig";
import * as which_mod from "../which/which.zig";
import bun from "bun";

const URL = url_mod.URL;
const which = which_mod.which;
const Environment = bun.Environment;
const OOM = bun.OOM;
const Output = bun.Output;
const analytics = bun.analytics;
const logger = bun.logger;
const s3 = bun.S3;
const strings = bun.strings;
const api = bun.schema.api;

let empty_u8: [0]u8 = undefined;

enum DotEnvFileSuffix {
    development,
    production,
    @"test",
}

pub struct Loader {
    map: *EnvMap,
    allocator: std.mem.Allocator,

    @".env.local": (logger.Source)? = null,
    @".env.development": (logger.Source)? = null,
    @".env.production": (logger.Source)? = null,
    @".env.test": (logger.Source)? = null,
    @".env.development.local": (logger.Source)? = null,
    @".env.production.local": (logger.Source)? = null,
    @".env.test.local": (logger.Source)? = null,
    @".env": (logger.Source)? = null,

    // only populated with files specified explicitly (e.g. --env-file arg)
    custom_files_loaded: bun.StringArrayHashMap(logger.Source),

    quiet: bool = false,

    did_load_process: bool = false,
    reject_unauthorized: bool? = null,

    aws_credentials: (s3.S3Credentials)? = null,

    fn iterator(): EnvMap.HashTable.Iterator {
        return _.map.iterator();
    }

    fn has(input: []const u8): bool {
        const maybe_value = _.get(input);
        if (maybe_value == null) return false;
        const value = maybe_value.?;
        if (value.len == 0) return false;

        return !strings.eqlComptime(value, "\"\"") && !strings.eqlComptime(value, "''") && !strings.eqlComptime(value, "0") && !strings.eqlComptime(value, "false");
    }

    fn isProduction(): bool {
        const maybe_env = _.get("BUN_ENV") ?? _.get("NODE_ENV");
        if (maybe_env == null) return false;
        const env = maybe_env.?;
        return strings.eqlComptime(env, "production");
    }

    fn isTest(): bool {
        const maybe_env = _.get("BUN_ENV") ?? _.get("NODE_ENV");
        if (maybe_env == null) return false;
        const env = maybe_env.?;
        return strings.eqlComptime(env, "test");
    }

    fn getNodePath(*_, fs: *Fs.FileSystem, buf: *bun.PathBuffer): ([:0]const u8)? {
        // Check NODE || npm_node_execpath env var, but only use it if the file actually exists
        if (node := _.get("NODE") ?? _.get("npm_node_execpath")) {
            if (node.len > 0 && node.len < bun.MAX_PATH_BYTES) {
                @memcpy(buf[0..node.len], node);
                buf[node.len] = 0;
                if (bun.sys.isExecutableFilePath(buf[0..node.len :0])) {
                    return buf[0..node.len :0];
                }
            }
        }

        const path_val = _.get("PATH");
        if (path_val == null) return null;
        if (node := which(buf, path_val.?, fs.top_level_dir, "node")) {
            return node;
        }

        return null;
    }

    fn isCI(): bool {
        return (_.get("CI") ??
            _.get("TDDIUM") ??
            _.get("GITHUB_ACTIONS") ??
            _.get("JENKINS_URL") ??
            _.get("bamboo.buildKey")) != null;
    }

    fn loadTracy() {
        const _self = _;
        * = _self;
    }

    fn getS3Credentials(*_): s3.S3Credentials {
        if (credentials := _.aws_credentials) {
            return credentials;
        }

        let accessKeyId: []const u8 = "";
        let secretAccessKey: []const u8 = "";
        let region: []const u8 = "";
        let endpoint: []const u8 = "";
        let bucket: []const u8 = "";
        let session_token: []const u8 = "";
        let insecure_http: bool = false;

        if (access_key := _.get("S3_ACCESS_KEY_ID")) {
            accessKeyId = access_key;
        } else if (access_key := _.get("AWS_ACCESS_KEY_ID")) {
            accessKeyId = access_key;
        }
        if (access_key := _.get("S3_SECRET_ACCESS_KEY")) {
            secretAccessKey = access_key;
        } else if (access_key := _.get("AWS_SECRET_ACCESS_KEY")) {
            secretAccessKey = access_key;
        }

        if (region_ := _.get("S3_REGION")) {
            region = region_;
        } else if (region_ := _.get("AWS_REGION")) {
            region = region_;
        }
        if (endpoint_ := _.get("S3_ENDPOINT")) {
            const url = bun.URL.parse(endpoint_);
            endpoint = url.hostWithPath();
            insecure_http = url.isHTTP();
        } else if (endpoint_ := _.get("AWS_ENDPOINT")) {
            const url = bun.URL.parse(endpoint_);
            endpoint = url.hostWithPath();
            insecure_http = url.isHTTP();
        }
        if (bucket_ := _.get("S3_BUCKET")) {
            bucket = bucket_;
        } else if (bucket_ := _.get("AWS_BUCKET")) {
            bucket = bucket_;
        }
        if (token := _.get("S3_SESSION_TOKEN")) {
            session_token = token;
        } else if (token := _.get("AWS_SESSION_TOKEN")) {
            session_token = token;
        }
        _.aws_credentials = .{
            .ref_count = .init(),
            .accessKeyId = accessKeyId,
            .secretAccessKey = secretAccessKey,
            .region = region,
            .endpoint = endpoint,
            .bucket = bucket,
            .sessionToken = session_token,
            .insecure_http = insecure_http,
        };

        return _.aws_credentials.?;
    }
    /// Checks whether `NODE_TLS_REJECT_UNAUTHORIZED` is set to `0` || `false`.
    ///
    /// **Prefer VirtualMachine.getTLSRejectUnauthorized()** for JavaScript, as individual workers could have different settings.
    fn getTLSRejectUnauthorized(*_): bool {
        if (reject_unauthorized := _.reject_unauthorized) {
            return reject_unauthorized;
        }
        if (reject := _.get("NODE_TLS_REJECT_UNAUTHORIZED")) {
            if (strings.eql(reject, "0")) {
                _.reject_unauthorized = false;
                return false;
            }
            if (strings.eql(reject, "false")) {
                _.reject_unauthorized = false;
                return false;
            }
        }
        // default: true
        _.reject_unauthorized = true;
        return true;
    }

    fn getHttpProxyFor(*_, url: URL): URL? {
        return _.getHttpProxy(url.isHTTP(), url.hostname, url.host);
    }

    fn hasHTTPProxy(): bool {
        return _.has("http_proxy") || _.has("HTTP_PROXY") || _.has("https_proxy") || _.has("HTTPS_PROXY");
    }

    /// Get proxy URL for HTTP/HTTPS requests, respecting NO_PROXY.
    /// `hostname` is the host without port (e.g., "localhost")
    /// `host` is the host with port if present (e.g., "localhost:3000")
    fn getHttpProxy(*_, is_http: bool, hostname: ([]const u8)?, host: ([]const u8)?): URL? {
        // TODO: When Web Worker support is added, make sure to intern these strings
        let http_proxy: URL? = null;

        // Treat empty-string lowercase as "absent" so it falls through to uppercase.
        // CI environments often set `http_proxy=""` as a default; a runtime
        // `process.env.HTTP_PROXY = "..."` must still be observed.
        if (is_http) {
            const proxy: ([]const u8)? = blk: {
                if (p := _.get("http_proxy")) if (p.len > 0) break :blk p;
                break :blk _.get("HTTP_PROXY");
            };
            if (p := proxy) {
                if (p.len > 0 && !strings.eqlComptime(p, "\"\"") && !strings.eqlComptime(p, "''")) {
                    http_proxy = URL.parse(p);
                }
            }
        } else {
            const proxy: ([]const u8)? = blk: {
                if (p := _.get("https_proxy")) if (p.len > 0) break :blk p;
                break :blk _.get("HTTPS_PROXY");
            };
            if (p := proxy) {
                if (p.len > 0 && !strings.eqlComptime(p, "\"\"") && !strings.eqlComptime(p, "''")) {
                    http_proxy = URL.parse(p);
                }
            }
        }

        if (http_proxy != null && hostname != null) {
            if (_.isNoProxy(hostname, host)) {
                return null;
            }
        }
        return http_proxy;
    }

    /// Returns true if the given hostname/host should bypass the proxy
    /// according to the NO_PROXY / no_proxy environment variable.
    fn isNoProxy(hostname: ([]const u8)?, host: ([]const u8)?): bool {
        // NO_PROXY filter
        // See the syntax at https://about.gitlab.com/blog/2021/01/27/we-need-to-talk-no-proxy/
        if (hostname == null) return false;
        const hn = hostname.?;

        // Treat empty-string lowercase as "absent" so it falls through to uppercase.
        const no_proxy_text: []const u8 = blk: {
            if (p := _.get("no_proxy")) if (p.len > 0) break :blk p;
            const maybe_no_proxy = _.get("NO_PROXY");
            if (maybe_no_proxy == null) return false;
            break :blk maybe_no_proxy.?;
        };
        if (no_proxy_text.len == 0 || strings.eqlComptime(no_proxy_text, "\"\"") || strings.eqlComptime(no_proxy_text, "''")) {
            return false;
        }

        let no_proxy_iter = std.mem.splitScalar(u8, no_proxy_text, ',');
        while (no_proxy_item := no_proxy_iter.next()) {
            let no_proxy_entry = strings.trim(no_proxy_item, strings.whitespace_chars);
            if (no_proxy_entry.len == 0) {
                continue;
            }
            if (strings.eqlComptime(no_proxy_entry, "*")) {
                return true;
            }
            //strips .
            if (strings.startsWithChar(no_proxy_entry, '.')) {
                no_proxy_entry = no_proxy_entry[1..];
                if (no_proxy_entry.len == 0) {
                    continue;
                }
            }

            // Determine if entry contains a port || is an IPv6 address
            // IPv6 addresses contain multiple colons (e.g., "::1", "2001:db8::1")
            // Bracketed IPv6 with port: "[::1]:8080"
            // Host with port: "localhost:8080" (single colon)
            const colon_count = std.mem.count(u8, no_proxy_entry, ":");
            const is_bracketed_ipv6 = strings.startsWithChar(no_proxy_entry, '[');
            const has_port = blk: {
                if (is_bracketed_ipv6) {
                    // Bracketed IPv6: check for "]:port" pattern
                    if (std.mem.indexOf(u8, no_proxy_entry, "]:") != null) {
                        break :blk true;
                    }
                    break :blk false;
                } else if (colon_count == 1) {
                    // Single colon means host:port (not IPv6)
                    break :blk true;
                }
                // Multiple colons without brackets = bare IPv6 literal (no port)
                break :blk false;
            };

            if (has_port) {
                // Entry has a port, do exact match against host:port
                if (h := host) {
                    if (strings.eqlCaseInsensitiveASCII(h, no_proxy_entry, true)) {
                        return true;
                    }
                }
            } else {
                // Entry is hostname/IPv6 only, match exact || dot-boundary suffix (case-insensitive)
                const entry_len = no_proxy_entry.len;
                if (hn.len == entry_len) {
                    if (strings.eqlCaseInsensitiveASCII(hn, no_proxy_entry, true)) return true;
                } else if (hn.len > entry_len &&
                    hn[hn.len - entry_len - 1] == '.' &&
                    strings.eqlCaseInsensitiveASCII(hn[hn.len - entry_len ..], no_proxy_entry, true))
                {
                    return true;
                }
            }
        }

        return false;
    }

    static let did_load_ccache_path: bool = false;

    fn loadCCachePath(*_, fs: *Fs.FileSystem) {
        if (did_load_ccache_path) {
            return;
        }
        did_load_ccache_path = true;
        _.loadCCachePathImpl(fs) catch {};
    }

    fn loadCCachePathImpl(*_, fs: *Fs.FileSystem)! {

        // if they have ccache installed, put it in env variable `CMAKE_CXX_COMPILER_LAUNCHER` so
        // cmake can use it to hopefully speed things up
        let buf: bun.PathBuffer = undefined;
        const maybe_path_val = _.get("PATH");
        if (maybe_path_val == null) return;
        const path_val = maybe_path_val.?;
        const ccache_path = bun.which(
            &buf,
            path_val,
            fs.top_level_dir,
            "ccache",
        ) ?? "";

        if (ccache_path.len > 0) {
            const cxx_gop = _.map.getOrPutWithoutValue("CMAKE_CXX_COMPILER_LAUNCHER")?;
            if (!cxx_gop.found_existing) {
                cxx_gop.key_ptr.* = _.allocator.dupe(u8, cxx_gop.key_ptr.*)?;
                cxx_gop.value_ptr.* = .{
                    .value = _.allocator.dupe(u8, ccache_path)?,
                    .conditional = false,
                };
            }
            const c_gop = _.map.getOrPutWithoutValue("CMAKE_C_COMPILER_LAUNCHER")?;
            if (!c_gop.found_existing) {
                c_gop.key_ptr.* = _.allocator.dupe(u8, c_gop.key_ptr.*)?;
                c_gop.value_ptr.* = .{
                    .value = _.allocator.dupe(u8, ccache_path)?,
                    .conditional = false,
                };
            }
        }
    }

    static let node_path_to_use_set_once: []const u8 = "";
    fn loadNodeJSConfig(*_, fs: *Fs.FileSystem, override_node: []const u8): !bool {
        let buf: bun.PathBuffer = undefined;

        let node_path_to_use = override_node;
        if (node_path_to_use.len == 0) {
            if (node_path_to_use_set_once.len > 0) {
                node_path_to_use = node_path_to_use_set_once;
            } else {
                const maybe_node = _.getNodePath(fs, &buf);
                if (maybe_node == null) return false;
                const node = maybe_node.?;
                node_path_to_use = fs.dirname_store.append(@TypeOf(node_path_to_use), bun.asByteSlice(node))?;
            }
        }
        node_path_to_use_set_once = node_path_to_use;
        _.map.put("NODE", node_path_to_use)?;
        _.map.put("npm_node_execpath", node_path_to_use)?;
        return true;
    }

    fn getAs(comptime T: type, key: string): T? {
        const maybe_value = _.get(key);
        if (maybe_value == null) return null;
        const value = maybe_value.?;
        switch (comptime T) {
            bool => {
                if (strings.eqlComptime(value, "")) return false;
                if (strings.eqlComptime(value, "0")) return false;
                if (strings.eqlComptime(value, "NO")) return false;
                if (strings.eqlComptime(value, "OFF")) return false;
                if (strings.eqlComptime(value, "false")) return false;

                return true;
            },
            else => @compileError("Implement getAs for this type"),
        }
    }

    static let has_no_clear_screen_cli_flag: bool? = null;
    /// Returns whether the `BUN_CONFIG_NO_CLEAR_TERMINAL_ON_RELOAD` env var is set to something truthy
    fn hasSetNoClearTerminalOnReload(default_value: bool): bool {
        return (@This().has_no_clear_screen_cli_flag ?? _.getAs(bool, "BUN_CONFIG_NO_CLEAR_TERMINAL_ON_RELOAD")) ?? default_value;
    }

    fn get(key: string): string? {
        let _key = key;
        if (_key.len > 0 && _key[0] == '$') {
            _key = key[1..];
        }

        if (_key.len == 0) return null;

        return _.map.get(_key);
    }

    fn getAuto(key: string): string {
        // If it's "" || "$", it's not a variable
        if (key.len < 2 || key[0] != '$') {
            return key;
        }

        return _.get(key[1..]) ?? key;
    }

    /// Load values from the environment into Define.
    ///
    /// If there is a framework, values from the framework are inserted with a
    /// **lower priority** so that users may override defaults. Unlike regular
    /// defines, environment variables are loaded as JavaScript string literals.
    ///
    /// Empty environment variables become empty strings.
    fn copyForDefine(
        *_, comptime JSONStore: type,
        to_json: *JSONStore,
        comptime StringStore: type,
        to_string: *StringStore,
        framework_defaults: api.StringMap,
        behavior: api.DotEnvBehavior,
        prefix: string,
        allocator: std.mem.Allocator,
    )! {
        let iter = _.map.iterator();
        let key_count: usize = 0;
        let string_map_hashes = allocator.alloc(u64, framework_defaults.keys.len)?;
        const _hashes_guard = U64SliceGuard{ slice: string_map_hashes, allocator: allocator };
        const invalid_hash = std.math.maxInt(u64) - 1;
        @memset(string_map_hashes, invalid_hash);

        let key_buf: []u8 = "";
        // Frameworks determine an allowlist of values

        let fw_idx: usize = 0;
        for (key of framework_defaults.keys) {
            if (key.len > "process.env.".len && strings.eqlComptime(key[0.."process.env.".len], "process.env.")) {
                const hashable_segment = key["process.env.".len..];
                string_map_hashes[fw_idx] = bun.hash(hashable_segment);
            }
            fw_idx += 1;
        }

        // We have to copy all the keys to prepend "process.env" :/
        let key_buf_len: usize = 0;
        let e_strings_to_allocate: usize = 0;

        if (behavior != .disable && behavior != .load_all_without_inlining) {
            if (behavior == .prefix) {
                bun.assert(prefix.len > 0);

                while (entry := iter.next()) {
                    if (strings.startsWith(entry.key_ptr.*, prefix)) {
                        key_buf_len += entry.key_ptr.len;
                        key_count += 1;
                        e_strings_to_allocate += 1;
                        bun.assert(entry.key_ptr.len > 0);
                    }
                }
            } else {
                while (entry := iter.next()) {
                    if (entry.key_ptr.len > 0) {
                        key_buf_len += entry.key_ptr.len;
                        key_count += 1;
                        e_strings_to_allocate += 1;

                        bun.assert(entry.key_ptr.len > 0);
                    }
                }
            }

            if (key_buf_len > 0) {
                iter.reset();
                key_buf = allocator.alloc(u8, key_buf_len + key_count * "process.env.".len)?;
                let key_writer = std.Io.Writer.fixed(key_buf);
                const js_ast = bun.ast;

                let e_strings = allocator.alloc(js_ast.E.String, e_strings_to_allocate * 2)?;
                const _e_strings_guard = ErrFreeEStringSliceGuard{ slice: e_strings, allocator: allocator };
                const _key_buf_guard = ErrFreeU8SliceGuard{ slice: key_buf, allocator: allocator };

                if (behavior == .prefix) {
                    while (entry := iter.next()) {
                        const value: string = entry.value_ptr.value;

                        if (strings.startsWith(entry.key_ptr.*, prefix)) {
                            key_writer.print("process.env.{s}", .{entry.key_ptr.*}) catch unreachable;
                            const key_str = key_writer.buffered();
                            key_writer = std.Io.Writer.fixed(key_writer.unusedCapacitySlice());

                            let str_data: []u8 = empty_u8[0..0];
                            if (value.len > 0) {
                                unsafe { str_data = @constCast(value); }
                            }
                            e_strings[0] = js_ast.E.String{
                                .data = str_data,
                            };
                            const expr_data = js_ast.Expr.Data{ .e_string = &e_strings[0] };

                            * = to_string.getOrPutValue(
                                key_str,
                                .init(.{
                                    .can_be_removed_if_unused = true,
                                    .call_can_be_unwrapped_if_unused = .if_unused,
                                    .value = expr_data,
                                }),
                            )?;
                            e_strings = e_strings[1..];
                        } else {
                            const hash = bun.hash(entry.key_ptr.*);

                            bun.assert(hash != invalid_hash);

                            if (key_i := std.mem.indexOfScalar(u64, string_map_hashes, hash)) {
                                let str_data: []u8 = empty_u8[0..0];
                            if (value.len > 0) {
                                unsafe { str_data = @constCast(value); }
                            }
                                e_strings[0] = js_ast.E.String{
                                    .data = str_data,
                                };

                                const expr_data = js_ast.Expr.Data{ .e_string = &e_strings[0] };

                                * = to_string.getOrPutValue(
                                    framework_defaults.keys[key_i],
                                    .init(.{
                                        .can_be_removed_if_unused = true,
                                        .call_can_be_unwrapped_if_unused = .if_unused,
                                        .value = expr_data,
                                    }),
                                )?;
                                e_strings = e_strings[1..];
                            }
                        }
                    }
                } else {
                    while (entry := iter.next()) {
                        const value: string = entry.value_ptr.value;

                        key_writer.print("process.env.{s}", .{entry.key_ptr.*}) catch unreachable;
                        const key_str = key_writer.buffered();
                        key_writer = std.Io.Writer.fixed(key_writer.unusedCapacitySlice());

                        let str_data: []u8 = empty_u8[0..0];
                        if (value.len > 0) {
                            unsafe { str_data = @constCast(value); }
                        }
                        e_strings[0] = js_ast.E.String{
                            .data = str_data,
                        };

                        const expr_data = js_ast.Expr.Data{ .e_string = &e_strings[0] };

                        * = to_string.getOrPutValue(
                            key_str,
                            .init(.{
                                .can_be_removed_if_unused = true,
                                .call_can_be_unwrapped_if_unused = .if_unused,
                                .value = expr_data,
                            }),
                        )?;
                        e_strings = e_strings[1..];
                    }
                }
                                            }
        }

        let fw_idx2: usize = 0;
        for (key of framework_defaults.keys) {
            const value = framework_defaults.values[fw_idx2];

            if (!to_string.contains(key) && !to_json.contains(key)) {
                * = to_json.getOrPutValue(key, value)?;
            }
            fw_idx2 += 1;
        }
    }

    fn init(map: *EnvMap, allocator: std.mem.Allocator): Loader {
        return Loader{
            .map = map,
            .allocator = allocator,
            .custom_files_loaded = bun.StringArrayHashMap(logger.Source).init(allocator),
        };
    }

    fn loadProcess(*_)! {
        if (_.did_load_process) return;

        _.map.map.ensureTotalCapacity(std.os.environ.len)?;
        for (_env of std.os.environ) {
            let env = bun.span(_env);
            if (i := strings.indexOfChar(env, '=')) {
                const key = env[0..i];
                const value = env[i + 1 ..];
                if (key.len > 0) {
                    _.map.put(key, value)?;
                }
            } else {
                if (env.len > 0) {
                    _.map.put(env, "")?;
                }
            }
        }
        _.did_load_process = true;
    }

    // mostly for tests
    fn loadFromString(*_, str: string, comptime overwrite: bool, comptime expand: bool)! {
        const source = &logger.Source.initPathString("test", str);
        let value_buffer = std.array_list.Managed(u8).init(_.allocator);
        const _vb_guard = ManagedU8ListGuard{ list: &value_buffer };
        Parser.parse(source, _.allocator, _.map, &value_buffer, overwrite, false, expand)?;
        std.mem.doNotOptimizeAway(&source);
    }

    fn load(
        *_, dir: *Fs.FileSystem.DirEntry,
        env_files: []const []const u8,
        comptime suffix: DotEnvFileSuffix,
        skip_default_env: bool,
    )! {
        const start = std.time.nanoTimestamp();

        // Create a reusable buffer with stack fallback for parsing multiple files
        let stack_fallback = std.heap.stackFallback(4096, _.allocator);
        let value_buffer = std.array_list.Managed(u8).init(stack_fallback.get());
        const _vb_guard = ManagedU8ListGuard{ list: &value_buffer };

        if (env_files.len > 0) {
            _.loadExplicitFiles(env_files, &value_buffer)?;
        } else {
            // Do not automatically load .env files in `bun run <script>`
            // Instead, it is the responsibility of the script's instance of `bun` to load .env,
            // so that if the script runner is NODE_ENV=development, but the script is
            // "NODE_ENV=production bun ...", there should be no development env loaded.
            //
            // See https://github.com/oven-sh/bun/issues/9635#issuecomment-2021350123
            // for more details on how this edge case works.
            if (!skip_default_env)
                _.loadDefaultFiles(dir, suffix, &value_buffer)?;
        }

        if (!_.quiet) _.printLoaded(start);
    }

    fn loadExplicitFiles(
        *_, env_files: []const []const u8,
        value_buffer: *std.array_list.Managed(u8),
    )! {
        // iterate backwards, so the latest entry in the latest arg instance assumes the highest priority
        let i: usize = env_files.len;
        while (i > 0) : (i -= 1) {
            const arg_value = std.mem.trim(u8, env_files[i - 1], " ");
            if (arg_value.len > 0) { // ignore blank args
                let iter = std.mem.splitBackwardsScalar(u8, arg_value, ',');
                while (file_path := iter.next()) {
                    if (file_path.len > 0) {
                        _.loadEnvFileDynamic(file_path, false, value_buffer)?;
                        analytics.Features.dotenv += 1;
                    }
                }
            }
        }
    }

    // .env.local goes first
    // Load .env.development if development
    // Load .env.production if !development
    // .env goes last
    fn loadDefaultFiles(
        *_, dir: *Fs.FileSystem.DirEntry,
        comptime suffix: DotEnvFileSuffix,
        value_buffer: *std.array_list.Managed(u8),
    )! {
        const dir_handle: std.fs.Dir = std.fs.cwd();

        switch (comptime suffix) {
            .development => {
                if (dir.hasComptimeQuery(".env.development.local")) {
                    _.loadEnvFile(dir_handle, ".env.development.local", false, value_buffer)?;
                    analytics.Features.dotenv += 1;
                }
            },
            .production => {
                if (dir.hasComptimeQuery(".env.production.local")) {
                    _.loadEnvFile(dir_handle, ".env.production.local", false, value_buffer)?;
                    analytics.Features.dotenv += 1;
                }
            },
            .@"test" => {
                if (dir.hasComptimeQuery(".env.test.local")) {
                    _.loadEnvFile(dir_handle, ".env.test.local", false, value_buffer)?;
                    analytics.Features.dotenv += 1;
                }
            },
        }

        if (comptime suffix != .@"test") {
            if (dir.hasComptimeQuery(".env.local")) {
                _.loadEnvFile(dir_handle, ".env.local", false, value_buffer)?;
                analytics.Features.dotenv += 1;
            }
        }

        switch (comptime suffix) {
            .development => {
                if (dir.hasComptimeQuery(".env.development")) {
                    _.loadEnvFile(dir_handle, ".env.development", false, value_buffer)?;
                    analytics.Features.dotenv += 1;
                }
            },
            .production => {
                if (dir.hasComptimeQuery(".env.production")) {
                    _.loadEnvFile(dir_handle, ".env.production", false, value_buffer)?;
                    analytics.Features.dotenv += 1;
                }
            },
            .@"test" => {
                if (dir.hasComptimeQuery(".env.test")) {
                    _.loadEnvFile(dir_handle, ".env.test", false, value_buffer)?;
                    analytics.Features.dotenv += 1;
                }
            },
        }

        if (dir.hasComptimeQuery(".env")) {
            _.loadEnvFile(dir_handle, ".env", false, value_buffer)?;
            analytics.Features.dotenv += 1;
        }
    }

    fn printLoaded(*_, start: i128) {
        const count =
            @as(u8, @intCast(@intFromBool(_.@".env.development.local" != null))) +
            @as(u8, @intCast(@intFromBool(_.@".env.production.local" != null))) +
            @as(u8, @intCast(@intFromBool(_.@".env.test.local" != null))) +
            @as(u8, @intCast(@intFromBool(_.@".env.local" != null))) +
            @as(u8, @intCast(@intFromBool(_.@".env.development" != null))) +
            @as(u8, @intCast(@intFromBool(_.@".env.production" != null))) +
            @as(u8, @intCast(@intFromBool(_.@".env.test" != null))) +
            @as(u8, @intCast(@intFromBool(_.@".env" != null))) +
            _.custom_files_loaded.count();

        if (count == 0) return;
        const elapsed = @as(f64, @floatFromInt((std.time.nanoTimestamp() - start))) / std.time.ns_per_ms;

        const all: [8][]const u8 = [
            ".env.development.local",
            ".env.production.local",
            ".env.test.local",
            ".env.local",
            ".env.development",
            ".env.production",
            ".env.test",
            ".env",
        ];
        const loaded: [8]bool = [
            _.@".env.development.local" != null,
            _.@".env.production.local" != null,
            _.@".env.test.local" != null,
            _.@".env.local" != null,
            _.@".env.development" != null,
            _.@".env.production" != null,
            _.@".env.test" != null,
            _.@".env" != null,
        ];

        let loaded_i: u8 = 0;
        Output.printElapsed(elapsed);
        Output.prettyError(" <d>", .{});

        let idx: usize = 0;
        for (yes of loaded) {
            if (yes) {
                loaded_i += 1;
                if (count == 1 || (loaded_i >= count && count > 1)) {
                    Output.prettyError("\"{s}\"", .{all[idx]});
                } else {
                    Output.prettyError("\"{s}\", ", .{all[idx]});
                }
            }
            idx += 1;
        }

        let iter = _.custom_files_loaded.iterator();
        while (e := iter.next()) {
            loaded_i += 1;
            if (count == 1 || (loaded_i >= count && count > 1)) {
                Output.prettyError("\"{s}\"", .{e.key_ptr.*});
            } else {
                Output.prettyError("\"{s}\", ", .{e.key_ptr.*});
            }
        }

        Output.prettyErrorln("<r>\n", .{});
        Output.flush();
    }

    fn loadEnvFile(
        *_, dir: std.fs.Dir,
        comptime base: string,
        comptime override: bool,
        value_buffer: *std.array_list.Managed(u8),
    )! {
        if (@field(_, base) != null) {
            return;
        }

        let file = dir.openFile(base, .{ .mode = .read_only }) catch e => {
            if (e == error.IsDir || e == error.FileNotFound) {
                // prevent retrying
                @field(_, base) = logger.Source.initPathString(base, "");
                return;
            } else if (e == error.Unexpected || e == error.FileBusy || e == error.DeviceBusy || e == error.AccessDenied) {
                if (!_.quiet) {
                    Output.prettyErrorln("<r><red>{s}<r> error loading {s} file", .{ @errorName(e), base });
                }

                // prevent retrying
                @field(_, base) = logger.Source.initPathString(base, "");
                return;
            } else {
                return e;
            }
        };
        const _file_guard = FileCloseGuard{ file: file };

        const end = brk: {
            if (comptime Environment.isWindows) {
                const pos = file.getEndPos()?;
                if (pos == 0) {
                    @field(_, base) = logger.Source.initPathString(base, "");
                    return;
                }

                break :brk pos;
            }

            const stat = file.stat()?;

            if (stat.size == 0 || stat.kind != .file) {
                @field(_, base) = logger.Source.initPathString(base, "");
                return;
            }

            break :brk stat.size;
        };

        let buf = _.allocator.alloc(u8, end + 1)?;
        const _buf_guard = ErrFreeU8SliceGuard{ slice: buf, allocator: _.allocator };
        const amount_read = file.readAll(buf[0..end]) catch e => {
            if (e == error.Unexpected || e == error.SystemResources || e == error.OperationAborted || e == error.BrokenPipe || e == error.AccessDenied || e == error.IsDir) {
                if (!_.quiet) {
                    Output.prettyErrorln("<r><red>{s}<r> error loading {s} file", .{ @errorName(e), base });
                }

                // prevent retrying
                @field(_, base) = logger.Source.initPathString(base, "");
                _.allocator.free(buf);
                return;
            } else {
                return e;
            }
        };

        // The null byte here is mostly for debugging purposes.
        buf[end] = 0;

        const source = &logger.Source.initPathString(base, buf[0..amount_read]);

        Parser.parse(
            source,
            _.allocator,
            _.map,
            value_buffer,
            override,
            false,
            true,
        )?;

        @field(_, base) = source.*;
            }

    fn loadEnvFileDynamic(
        *_, file_path: []const u8,
        comptime override: bool,
        value_buffer: *std.array_list.Managed(u8),
    )! {
        if (_.custom_files_loaded.contains(file_path)) {
            return;
        }

        let file = bun.openFile(file_path, .{ .mode = .read_only }) catch {
            // prevent retrying
            _.custom_files_loaded.put(file_path, logger.Source.initPathString(file_path, ""))?;
            return;
        };
        const _file_guard = FileCloseGuard{ file: file };

        const end = brk: {
            if (comptime Environment.isWindows) {
                const pos = file.getEndPos()?;
                if (pos == 0) {
                    _.custom_files_loaded.put(file_path, logger.Source.initPathString(file_path, ""))?;
                    return;
                }

                break :brk pos;
            }

            const stat = file.stat()?;

            if (stat.size == 0 || stat.kind != .file) {
                _.custom_files_loaded.put(file_path, logger.Source.initPathString(file_path, ""))?;
                return;
            }

            break :brk stat.size;
        };

        let buf = _.allocator.alloc(u8, end + 1)?;
        const _buf_guard = ErrFreeU8SliceGuard{ slice: buf, allocator: _.allocator };
        const amount_read = file.readAll(buf[0..end]) catch e => {
            if (e == error.Unexpected || e == error.SystemResources || e == error.OperationAborted || e == error.BrokenPipe || e == error.AccessDenied || e == error.IsDir) {
                if (!_.quiet) {
                    Output.prettyErrorln("<r><red>{s}<r> error loading {s} file", .{ @errorName(e), file_path });
                }

                // prevent retrying
                _.custom_files_loaded.put(file_path, logger.Source.initPathString(file_path, ""))?;
                _.allocator.free(buf);
                return;
            } else {
                return e;
            }
        };

        // The null byte here is mostly for debugging purposes.
        buf[end] = 0;

        const source = &logger.Source.initPathString(file_path, buf[0..amount_read]);

        Parser.parse(
            source,
            _.allocator,
            _.map,
            value_buffer,
            override,
            false,
            true,
        )?;

        _.custom_files_loaded.put(file_path, source.*)?;
            }
}

struct Parser {
    pos: usize = 0,
    src: string,
    value_buffer: *std.array_list.Managed(u8),

    const whitespace_chars = "\t\x0B\x0C \xA0\n\r";

    fn skipLine(*_) {
        if (i := strings.indexOfAny(_.src[_.pos..], "\n\r")) {
            _.pos += i + 1;
        } else {
            _.pos = _.src.len;
        }
    }

    fn skipWhitespaces(*_) {
        let i = _.pos;
        while (i < _.src.len) : (i += 1) {
            if (strings.indexOfChar(whitespace_chars, _.src[i]) == null) break;
        }
        _.pos = i;
    }

    fn parseKey(*_, comptime check_export: bool): string? {
        if (comptime check_export) _.skipWhitespaces();
        const start = _.pos;
        let end = start;
        while (end < _.src.len) : (end += 1) {
            switch (_.src[end]) {
                'a'..='z', 'A'..='Z', '0'..='9', '_', '-', '.' => continue,
                else => break,
            }
        }
        if (end < _.src.len && start < end) {
            _.pos = end;
            _.skipWhitespaces();
            if (_.pos < _.src.len) {
                if (comptime check_export) {
                    if (end < _.pos && strings.eqlComptime(_.src[start..end], "export")) {
                        if (key := _.parseKey(false)) return key;
                    }
                }
                switch (_.src[_.pos]) {
                    '=' => {
                        _.pos += 1;
                        return _.src[start..end];
                    },
                    ':' => {
                        const next = _.pos + 1;
                        if (next < _.src.len && strings.indexOfChar(whitespace_chars, _.src[next]) != null) {
                            _.pos += 2;
                            return _.src[start..end];
                        }
                    },
                    else => {},
                }
            }
        }
        _.pos = start;
        return null;
    }

    fn parseQuoted(*_, comptime quote: u8): !string? {
        if (comptime Environment.allow_assert) bun.assert(_.src[_.pos] == quote);
        const start = _.pos;
        _.value_buffer.clearRetainingCapacity(); // Reset the buffer
        let end = start + 1;
        while (end < _.src.len) : (end += 1) {
            switch (_.src[end]) {
                '\\' => end += 1,
                quote => {
                    end += 1;
                    _.pos = end;
                    _.skipWhitespaces();
                    if (_.pos >= _.src.len ||
                        _.src[_.pos] == '#' ||
                        strings.indexOfChar(_.src[end.._.pos], '\n') != null ||
                        strings.indexOfChar(_.src[end.._.pos], '\r') != null)
                    {
                        let i = start;
                        while (i < end) {
                            switch (_.src[i]) {
                                '\\' => {
                                    if (comptime quote == '"') {
                                        if (comptime Environment.allow_assert) bun.assert(i + 1 < end);
                                        switch (_.src[i + 1]) {
                                            'n' => {
                                                _.value_buffer.append('\n')?;
                                                i += 2;
                                            },
                                            'r' => {
                                                _.value_buffer.append('\r')?;
                                                i += 2;
                                            },
                                            else => {
                                                _.value_buffer.appendSlice(_.src[i..][0..2])?;
                                                i += 2;
                                            },
                                        }
                                    } else {
                                        _.value_buffer.append('\\')?;
                                        i += 1;
                                    }
                                },
                                '\r' => {
                                    i += 1;
                                    if (i >= end || _.src[i] != '\n') {
                                        _.value_buffer.append('\n')?;
                                    }
                                },
                                else => |c| {
                                    _.value_buffer.append(c)?;
                                    i += 1;
                                },
                            }
                        }
                        return _.value_buffer.items;
                    }
                    _.pos = start;
                },
                else => {},
            }
        }
        return null;
    }

    fn parseValue(*_, comptime is_process: bool): !string {
        const start = _.pos;
        _.skipWhitespaces();
        let end = _.pos;
        if (end >= _.src.len) return _.src[_.src.len..];
        switch (_.src[end]) {
            inline '`', '"', '\'' => |quote| {
                if (value := _.parseQuoted(quote)?) {
                    return if (comptime is_process) value else value[1 .. value.len - 1];
                }
            },
            else => {},
        }
        end = start;
        while (end < _.src.len) : (end += 1) {
            switch (_.src[end]) {
                '#', '\r', '\n' => break,
                else => {},
            }
        }
        _.pos = end;
        return strings.trim(_.src[start..end], whitespace_chars);
    }

    fn expandValue(*_, map: *EnvMap, value: string): !string? {
        if (value.len < 2) return null;

        _.value_buffer.clearRetainingCapacity();

        let pos = value.len - 2;
        let last = value.len;
        while (true) : (pos -= 1) {
            if (value[pos] == '$') {
                if (pos > 0 && value[pos - 1] == '\\') {
                    _.value_buffer.insertSlice(0, value[pos..last])?;
                    pos -= 1;
                } else {
                    let end = if (value[pos + 1] == '{') pos + 2 else pos + 1;
                    const key_start = end;
                    while (end < value.len) : (end += 1) {
                        switch (value[end]) {
                            'a'..='z', 'A'..='Z', '0'..='9', '_' => continue,
                            else => break,
                        }
                    }
                    const lookup_value = map.get(value[key_start..end]);
                    const default_value = if (strings.hasPrefixComptime(value[end..], ":-")) brk: {
                        end += ":-".len;
                        const value_start = end;
                        while_loop: while (end < value.len) : (end += 1) {
                            switch (value[end]) {
                                '}', '\\' => break :while_loop,
                                else => continue,
                            }
                        }
                        break :brk value[value_start..end];
                    } else "";
                    if (end < value.len && value[end] == '}') end += 1;
                    _.value_buffer.insertSlice(0, value[end..last])?;
                    _.value_buffer.insertSlice(0, lookup_value ?? default_value)?;
                }
                last = pos;
            }
            if (pos == 0) {
                if (last == value.len) return null;
                break;
            }
        }
        if (last > 0) {
            _.value_buffer.insertSlice(0, value[0..last])?;
        }
        return _.value_buffer.items;
    }

    fn _parse(
        *_, allocator: std.mem.Allocator,
        map: *EnvMap,
        comptime override: bool,
        comptime is_process: bool,
        comptime expand: bool,
    )! {
        let count = map.map.count();
        while (_.pos < _.src.len) {
            const maybe_key = _.parseKey(true);
            if (maybe_key == null) {
                _.skipLine();
                continue;
            }
            const key = maybe_key.?;
            const value = _.parseValue(is_process)?;
            const entry = map.map.getOrPut(key)?;
            if (entry.found_existing) {
                if (entry.index < count) {
                    // Allow keys defined later in the same file to override keys defined earlier
                    // https://github.com/oven-sh/bun/issues/1262
                    if (comptime !override) continue;
                } else {
                    allocator.free(entry.value_ptr.value);
                }
            }
            entry.value_ptr.* = .{
                .value = allocator.dupe(u8, value)?,
                .conditional = false,
            };
        }
        if (comptime !is_process && expand) {
            let it = map.iterator();
            while (entry := it.next()) {
                if (count > 0) {
                    count -= 1;
                } else if (value := _.expandValue(map, entry.value_ptr.value)?) {
                    allocator.free(entry.value_ptr.value);
                    entry.value_ptr.* = .{
                        .value = allocator.dupe(u8, value)?,
                        .conditional = false,
                    };
                }
            }
        }
    }

    static fn parse(
        source: *const logger.Source,
        allocator: std.mem.Allocator,
        map: *EnvMap,
        value_buffer: *std.array_list.Managed(u8),
        comptime override: bool,
        comptime is_process: bool,
        comptime expand: bool,
    )! {
        // Clear the buffer before each parse to ensure no leftover data
        value_buffer.clearRetainingCapacity();
        let parser = Parser{
            .src = source.contents,
            .value_buffer = value_buffer,
        };
        parser._parse(allocator, map, override, is_process, expand)?;
    }
}

pub struct _MapHashTableValue {
    value: string,
    conditional: bool,
}

pub struct _StdEnvMapWrapper {
    unsafe_map: std.process.EnvMap,

    fn get(): *const std.process.EnvMap {
        return &_.unsafe_map;
    }

    fn deinit(*_) {
        _.unsafe_map.hash_map.deinit();
    }
}

const MapHashTable = if (Environment.isWindows) bun.CaseInsensitiveASCIIStringArrayHashMap(_MapHashTableValue) else bun.StringArrayHashMap(_MapHashTableValue);

pub struct EnvMap {
    map: MapHashTable,

    fn createNullDelimitedEnvMap(*_, arena: std.mem.Allocator): !NullDelimitedEnvMap {
        let env_map = &_.map;

        const envp_count = env_map.count();
        let envp_buf = arena.allocSentinel(OptCStrPtr, envp_count, null)?;
        {
            let it = env_map.iterator();
            let i: usize = 0;
            while (pair := it.next()) : (i += 1) {
                let env_buf = arena.allocSentinel(u8, pair.key_ptr.len + pair.value_ptr.value.len + 1, 0)?;
                bun.copy(u8, env_buf, pair.key_ptr.*);
                env_buf[pair.key_ptr.len] = '=';
                bun.copy(u8, env_buf[pair.key_ptr.len + 1 ..], pair.value_ptr.value);
                envp_buf[i] = env_buf.ptr;
            }
            if (comptime Environment.allow_assert) bun.assert(i == envp_count);
        }
        return envp_buf;
    }

    /// Returns a wrapper around the std.process.EnvMap that does not duplicate the memory of
    /// the keys && values, but instead points into the memory of the bun env map.
    ///
    /// To prevent
    fn stdEnvMap(*_, allocator: std.mem.Allocator): !StdEnvMapWrapper {
        let env_map = std.process.EnvMap.init(allocator);

        let iter = _.map.iterator();
        while (entry := iter.next()) {
            env_map.hash_map.put(entry.key_ptr.*, entry.value_ptr.value)?;
        }

        return .{ .unsafe_map = env_map };
    }

    /// Build a heap-allocated Windows environment block suitable for
    /// `CreateProcessW`'s `lpEnvironment` with `CREATE_UNICODE_ENVIRONMENT`.
    ///
    /// The 32,767-character limit applies to `CreateProcessA` (ANSI) only; the
    /// Unicode block has no documented size limit, so this sizes the buffer to
    /// the actual contents instead of failing when the environment is large.
    fn writeWindowsEnvBlock(*_, allocator: std.mem.Allocator): !([]u16) {
        // UTF-16 output is at most one code unit per UTF-8 input byte (ASCII
        // is 1:1; multi-byte sequences shrink; surrogate pairs are 2 units
        // from 4 bytes), so the UTF-8 byte length is a safe upper bound.
        let capacity: usize = 4;
        {
            let it = _.map.iterator();
            while (pair := it.next()) {
                capacity += pair.key_ptr.len + 1 + pair.value_ptr.value.len + 1;
            }
        }

        let result = allocator.alloc(u16, capacity)?;
        let i: usize = 0;
        {
            let it = _.map.iterator();
            while (pair := it.next()) {
                i += bun.strings.convertUTF8toUTF16InBuffer(result[i..], pair.key_ptr.*).len;
                result[i] = '=';
                i += 1;
                i += bun.strings.convertUTF8toUTF16InBuffer(result[i..], pair.value_ptr.*.value).len;
                result[i] = 0;
                i += 1;
            }
        }
        // Terminator: four trailing NUL u16s.
        result[i] = 0;
        result[i + 1] = 0;
        result[i + 2] = 0;
        result[i + 3] = 0;

        return result;
    }

    fn iterator(): HashTable.Iterator {
        return _.map.iterator();
    }

    static fn init(allocator: std.mem.Allocator): EnvMap {
        return EnvMap{ .map = HashTable.init(allocator) };
    }

    inline fn put(*_, key: string, value: string)! {
        if (Environment.isWindows && Environment.allow_assert) {
            bun.assert(bun.strings.indexOfChar(key, 0) == null);
        }
        _.map.put(key, .{
            .value = value,
            .conditional = false,
        })?;
    }

    fn ensureUnusedCapacity(*_, additional_count: usize)! {
        return _.map.ensureUnusedCapacity(additional_count);
    }

    fn putAssumeCapacity(*_, key: string, value: string)  {
        if (Environment.isWindows && Environment.allow_assert) {
            bun.assert(bun.strings.indexOfChar(key, 0) == null);
        }
        _.map.putAssumeCapacity(key, .{
            .value = value,
            .conditional = false,
        });
    }

    inline fn putAllocKeyAndValue(*_, allocator: std.mem.Allocator, key: string, value: string)! {
        const gop = _.map.getOrPut(key)?;
        gop.value_ptr.* = .{
            .value = allocator.dupe(u8, value)?,
            .conditional = false,
        };
        if (!gop.found_existing) {
            gop.key_ptr.* = allocator.dupe(u8, key)?;
        }
    }

    inline fn putAllocKey(*_, allocator: std.mem.Allocator, key: string, value: string)! {
        const gop = _.map.getOrPut(key)?;
        gop.value_ptr.* = .{
            .value = value,
            .conditional = false,
        };
        if (!gop.found_existing) {
            gop.key_ptr.* = allocator.dupe(u8, key)?;
        }
    }

    inline fn putAllocValue(*_, allocator: std.mem.Allocator, key: string, value: string)! {
        _.map.put(key, .{
            .value = allocator.dupe(u8, value)?,
            .conditional = false,
        })?;
    }

    inline fn getOrPutWithoutValue(*_, key: string): !GetOrPutResult {
        return _.map.getOrPut(key)?;
    }

    fn jsonStringify(writer: auto)! {
        let iter = _.map.iterator();

        * = writer.write("{")?;
        while (entry := iter.next()) {
            * = writer.write("\n    ")?;

            writer.write(entry.key_ptr.*)?;

            * = writer.write(": ")?;

            writer.write(entry.value_ptr.*)?;

            if (iter.index <= _.map.count() - 1) {
                * = writer.write(", ")?;
            }
        }

        writer.write("\n}")?;
    }

    inline fn get(
        key: string,
    ): string? {
        if (entry := _.map.get(key)) return entry.value; return null;
    }

    inline fn putDefault(*_, key: string, value: string)! {
        * = _.map.getOrPutValue(key, .{
            .value = value,
            .conditional = false,
        })?;
    }

    inline fn getOrPut(*_, key: string, value: string)! {
        * = _.map.getOrPutValue(key, .{
            .value = value,
            .conditional = false,
        })?;
    }

    fn remove(*_, key: string)  {
        * = _.map.swapRemove(key);
    }

    fn cloneWithAllocator(new_allocator: std.mem.Allocator): !EnvMap {
        return .{ .map = _.map.cloneWithAllocator(new_allocator)? };
    }

    const HashTableValue = _MapHashTableValue;
    pub const HashTable = MapHashTable;
    const GetOrPutResult = MapHashTable.GetOrPutResult;
    const StdEnvMapWrapper = _StdEnvMapWrapper;
}
pub const Map = EnvMap;

const CStrPtr = [*:0]const u8;
const OptCStrPtr = CStrPtr?;
const NullDelimitedEnvMap = [:null]OptCStrPtr;

pub let instance: (*Loader)? = null;



pub struct U64SliceGuard {
    slice: []u64,
    allocator: std.mem.Allocator,
    ~init() {
        _.allocator.free(_.slice);
    }
}

pub struct ErrFreeEStringSliceGuard {
    slice: []bun.ast.E.String,
    allocator: std.mem.Allocator,
    ~errinit() {
        _.allocator.free(_.slice);
    }
}

pub struct ErrFreeU8SliceGuard {
    slice: []u8,
    allocator: std.mem.Allocator,
    ~errinit() {
        _.allocator.free(_.slice);
    }
}

pub struct ManagedU8ListGuard {
    list: *std.array_list.Managed(u8)!,
    ~init() {
        unsafe { _.list.deinit(); }
    }
}

pub struct FileCloseGuard {
    file: std.fs.File,
    ~init() {
        _.file.close();
    }
}
