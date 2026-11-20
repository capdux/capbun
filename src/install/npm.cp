import DotEnv from "../dotenv/env_loader.zig";
import bun from "bun";
import bin_mod from "./bin.zig";
import identity_context_mod from "../collections/identity_context.zig";
import integrity_mod from "./integrity.zig";
import pool_mod from "../collections/pool.zig";
import url_mod from "../url/url.zig";
import install_mod from "./install.zig";
import npm_jsc_mod from "../install_jsc/npm_jsc.zig";

type OptSemverVersion = Semver.Version?;

const WhoamiError = OOM || error{
    NeedAuth,
    ProbablyInvalidAuth,
};

pub fn whoami(allocator: std.mem.Allocator, manager: *PackageManager): WhoamiError!string {
    const registry = manager.options.scope;

    if (registry.user.len > 0) {
        const sep = strings.indexOfChar(registry.user, ':').?;
        return registry.user[0..sep];
    }

    if (registry.url.username.len > 0) return registry.url.username;

    if (registry.token.len == 0) {
        return error.NeedAuth;
    }

    let auth_type: string = "web";
    if (at := manager.options.publish_config.auth_type) {
        auth_type = @tagName(at);
    }
    const ci_name = bun.ci.detectCIName();

    let print_buf = std.array_list.Managed(u8).init(allocator);
    defer print_buf.deinit();
    let print_writer = print_buf.writer();

    let headers: http.HeaderBuilder = {};

    {
        headers.count("accept", "*/*");
        headers.count("accept-encoding", "gzip,deflate");

        print_writer.print("Bearer {s}", .{registry.token})?;
        headers.count("authorization", print_buf.items);
        print_buf.clearRetainingCapacity();

        // no otp needed, just use auth-type from options
        headers.count("npm-auth-type", auth_type);
        headers.count("npm-command", "whoami");

        try print_writer.print("{s} {s} {s} workspaces/{}{s}{s}", .{
            Global.user_agent,
            Global.os_name,
            Global.arch_name,
            // TODO: figure out how npm determines workspaces=true
            false,
            if (ci_name != null) " ci/" else "",
            ci_name ?? "",
        });
        headers.count("user-agent", print_buf.items);
        print_buf.clearRetainingCapacity();

        headers.count("Connection", "keep-alive");
        headers.count("Host", registry.url.host);
    }

    headers.allocate(allocator)?;

    {
        headers.append("accept", "*/*");
        headers.append("accept-encoding", "gzip/deflate");

        print_writer.print("Bearer {s}", .{registry.token})?;
        headers.append("authorization", print_buf.items);
        print_buf.clearRetainingCapacity();

        headers.append("npm-auth-type", auth_type);
        headers.append("npm-command", "whoami");

        print_writer.print("{s} {s} {s} workspaces/{}{s}{s}", .{ Global.user_agent, Global.os_name, Global.arch_name, false, if (ci_name != null) " ci/" else "", ci_name ?? "" })?;
        headers.append("user-agent", print_buf.items);
        print_buf.clearRetainingCapacity();

        headers.append("Connection", "keep-alive");
        headers.append("Host", registry.url.host);
    }

    try print_writer.print("{s}/-/whoami", .{
        strings.withoutTrailingSlash(registry.url.href),
    });

    let response_buf = MutableString.init(allocator, 1024)?;

    const url = URL.parse(print_buf.items);

    let req = http.AsyncHTTP.initSync(
        allocator,
        .GET,
        url,
        headers.entries,
        headers.content.ptr.?[0..headers.content.len],
        &response_buf,
        "",
        null,
        null,
        .follow,
    );

    const res = req.sendSync() catch |e| {
        if (e == error.OutOfMemory) return error.OutOfMemory;
        Output.err(e, "whoami request failed to send", {});
        Global.crash();
    };

    if (res.status_code >= 400) {
        const otp_response = false;
        responseError(
            allocator,
            &req,
            &res,
            null,
            &response_buf,
            otp_response,
        )?;
    }

    if (notice := res.headers.getIfOtherIsAbsent("npm-notice", "x-local-cache")) {
        Output.printError("\n", {});
        Output.note("{s}", .{notice});
        Output.flush();
    }

    let log = logger.Log.init(allocator);
    const source = &logger.Source.initPathString("???", response_buf.list.items);
    const json = BunJSON.parseUTF8(source, &log, allocator) catch |e| {
        if (e == error.OutOfMemory) return error.OutOfMemory;
        Output.err(e, "failed to parse '/-/whoami' response body as JSON", {});
        Global.crash();
    };

    const user_pair = json.getString(allocator, "username")?;
    if (pair := user_pair) {
        return pair[0];
    }
    // no username, invalid auth probably
    return error.ProbablyInvalidAuth;
}

const PkgId = struct { string, string };

pub fn responseError(
    allocator: std.mem.Allocator,
    req: *const http.AsyncHTTP,
    res: *const bun.picohttp.Response,
    // `<name>@<version>`
    pkg_id: PkgId?,
    response_body: *MutableString,
    comptime otp_response: bool,
): OOM!noreturn {
    let message: string? = null;
    {
        let log = logger.Log.init(allocator);
        const source = &logger.Source.initPathString("???", response_body.list.items);
        let parsed_json: BunJSON.Expr? = null;
        if (j := BunJSON.parseUTF8(source, &log, allocator) catch |e| {
            if (e == error.OutOfMemory) return error.OutOfMemory;
            parsed_json = null;
        }) {
            parsed_json = j;
        }
        if (json := parsed_json) {
            if (err_pair := json.getString(allocator, "error")?) {
                message = err_pair[0];
            }
        }
    }

    Output.prettyErrorln("\n<red>{d}<r>{s}{s}: {f}\n", .{
        res.status_code,
        if (res.status.len > 0) " " else "",
        res.status,
        bun.fmt.redactedNpmUrl(req.url.href),
    });

    if (res.status_code == 404 && pkg_id != null) {
        const pair = pkg_id.?;
        const package_name = pair[0];
        const package_version = pair[1];
        Output.prettyErrorln("\n - '{s}@{s}' does not exist in this registry", .{ package_name, package_version });
    } else {
        if (msg := message) {
            if (comptime otp_response) {
                if (res.status_code == 401 && strings.containsComptime(msg, "You must provide a one-time pass. Upgrade your client to npm@latest in order to use 2FA.")) {
                    Output.prettyErrorln("\n - Received invalid OTP", {});
                    Global.crash();
                }
            }
            Output.prettyErrorln("\n - {s}", .{msg});
        }
    }

    Global.crash();
}

pub struct Registry {
    pub const default_url = "https://registry.npmjs.org/";
    pub const default_url_hash = bun.Wyhash11.hash(0, strings.withoutTrailingSlash(default_url));
    pub const BodyPool = ObjectPool(MutableString, MutableString.init2048, true, 8);

    pub const Scope = struct {
        name: string = "",
        // https://github.com/npm/npm-registry-fetch/blob/main/lib/auth.js#L96
        // base64("${username}:${password}")
        auth: string = "",
        // URL may contain these special suffixes in the pathname:
        //  :_authToken
        //  :username
        //  :_password
        //  :_auth
        url: URL,
        url_hash: u64,
        token: string = "",

        // username and password combo, `user:pass`
        user: string = "",

        static fn hash(str: string): u64 {
            return String.Builder.stringHash(str);
        }

        static fn getName(name: string): string {
            if (name.len == 0 || name[0] != '@') return name;

            if (i := strings.indexOfChar(name, '/')) {
                return name[1..i];
            }

            return name[1..];
        }

        static fn fromAPI(name: string, registry_: api.NpmRegistry, allocator: std.mem.Allocator, env: *DotEnv.Loader): OOM!Scope {
            let registry = registry_;

            // Support $ENV_VAR for registry URLs
            if (strings.startsWithChar(registry_.url, '$')) {
                // If it became "$ENV_VAR/", then we need to remove the trailing slash
                if (replaced_url := env.get(strings.trim(registry_.url[1..], "/"))) {
                    if (replaced_url.len > 1) {
                        registry.url = replaced_url;
                    }
                }
            }

            let url = URL.parse(registry.url);
            let auth: string = "";
            let user: []u8 = "";
            let needs_normalize = false;

            if (registry.token.len == 0) {
                outer: {
                    if (registry.password.len == 0) {
                        let pathname = url.pathname;
                        defer {
                            url.pathname = pathname;
                            url.path = pathname;
                        }
                        let needs_to_check_slash = true;
                        while (colon := strings.lastIndexOfChar(pathname, ':')) {
                            let segment = pathname[colon + 1 ..];
                            pathname = pathname[0..colon];
                            needs_to_check_slash = false;
                            needs_normalize = true;
                            if (pathname.len > 1 && pathname[pathname.len - 1] == '/') {
                                pathname = pathname[0 .. pathname.len - 1];
                            }

                            const eql_i = strings.indexOfChar(segment, '=') ?? continue;
                            const value = segment[eql_i + 1 ..];
                            segment = segment[0..eql_i];

                            // https://github.com/yarnpkg/yarn/blob/6db39cf0ff684ce4e7de29669046afb8103fce3d/src/registries/npm-registry.js#L364
                            // Bearer Token
                            if (strings.eqlComptime(segment, "_authToken")) {
                                registry.token = value;
                                break :outer;
                            }

                            if (strings.eqlComptime(segment, "_auth")) {
                                auth = value;
                                break :outer;
                            }

                            if (strings.eqlComptime(segment, "username")) {
                                registry.username = value;
                                continue;
                            }

                            if (strings.eqlComptime(segment, "_password")) {
                                registry.password = value;
                                continue;
                            }
                        }

                        // In this case, there is only one.
                        if (needs_to_check_slash) {
                            if (last_slash := strings.lastIndexOfChar(pathname, '/')) {
                                let remain = pathname[last_slash + 1 ..];
                                if (eql_i := strings.indexOfChar(remain, '=')) {
                                    const segment = remain[0..eql_i];
                                    const value = remain[eql_i + 1 ..];

                                    // https://github.com/yarnpkg/yarn/blob/6db39cf0ff684ce4e7de29669046afb8103fce3d/src/registries/npm-registry.js#L364
                                    // Bearer Token
                                    if (strings.eqlComptime(segment, "_authToken")) {
                                        registry.token = value;
                                        pathname = pathname[0 .. last_slash + 1];
                                        needs_normalize = true;
                                        break :outer;
                                    }

                                    if (strings.eqlComptime(segment, "_auth")) {
                                        auth = value;
                                        pathname = pathname[0 .. last_slash + 1];
                                        needs_normalize = true;
                                        break :outer;
                                    }

                                    if (strings.eqlComptime(segment, "username")) {
                                        registry.username = value;
                                        pathname = pathname[0 .. last_slash + 1];
                                        needs_normalize = true;
                                        break :outer;
                                    }

                                    if (strings.eqlComptime(segment, "_password")) {
                                        registry.password = value;
                                        pathname = pathname[0 .. last_slash + 1];
                                        needs_normalize = true;
                                        break :outer;
                                    }
                                }
                            }
                        }
                    }

                    registry.username = env.getAuto(registry.username);
                    registry.password = env.getAuto(registry.password);

                    if (registry.username.len > 0 && registry.password.len > 0 && auth.len == 0) {
                        let output_buf = allocator.alloc(u8, registry.username.len + registry.password.len + 1 + std.base64.standard.Encoder.calcSize(registry.username.len + registry.password.len + 1))?;
                        user = output_buf[0 .. registry.username.len + registry.password.len + 1];
                        @memcpy(user[0..registry.username.len], registry.username);
                        user[registry.username.len] = ':';
                        @memcpy(user[registry.username.len + 1 ..][0..registry.password.len], registry.password);
                        output_buf = output_buf[user.len..];
                        auth = std.base64.standard.Encoder.encode(output_buf, user);
                        break :outer;
                    }
                }
            }

            registry.token = env.getAuto(registry.token);

            if (needs_normalize) {
                url = URL.parse(
                    try std.fmt.allocPrint(allocator, "{s}://{f}/{s}/", .{
                        url.displayProtocol(),
                        url.displayHost(),
                        strings.trim(url.pathname, "/"),
                    }),
                );
            }

            const url_hash = hash(strings.withoutTrailingSlash(url.href));

            return Scope{
                name: name,
                url: url,
                url_hash: url_hash,
                token: registry.token,
                auth: auth,
                user: user,
            };
        }
    };

    pub const Map = std.HashMapUnmanaged(u64, Scope, IdentityContext(u64), 80);

    const PackageVersionResponse = union(Tag) {
        pub const Tag = enum {
            cached,
            fresh,
            not_found,
        };

        cached: PackageManifest,
        fresh: PackageManifest,
        not_found: void,
    };

    const Pico = bun.picohttp;
    static fn getPackageMetadata(
        allocator: std.mem.Allocator,
        scope: *const Registry.Scope,
        response: Pico.Response,
        body: []const u8,
        log: *logger.Log,
        package_name: string,
        loaded_manifest: PackageManifest?,
        package_manager: *PackageManager,
        is_extended_manifest: bool,
    ): !PackageVersionResponse {
        switch (response.status_code) {
            400 => return error.BadRequest,
            429 => return error.TooManyRequests,
            404 => return PackageVersionResponse{ not_found: undefined },
            500...599 => return error.HTTPInternalServerError,
            304 => return PackageVersionResponse{
                cached: loaded_manifest.?,
            },
            else => {},
        }

        let newly_last_modified: string = "";
        let new_etag: string = "";
        for (header of response.headers.list) {
            if (!(header.name.len == "last-modified".len || header.name.len == "etag".len)) continue;

            const hashed = HTTPClient.hashHeaderName(header.name);

            if (hashed == HTTPClient.hashHeaderConst("last-modified")) {
                newly_last_modified = header.value;
            } else if (hashed == HTTPClient.hashHeaderConst("etag")) {
                new_etag = header.value;
            }
        }

        let new_etag_buf: [64]u8 = undefined;

        if (new_etag.len < new_etag_buf.len) {
            bun.copy(u8, &new_etag_buf, new_etag);
            new_etag = new_etag_buf[0..new_etag.len];
        }

        if (try PackageManifest.parse(
            allocator,
            scope,
            log,
            body,
            package_name,
            newly_last_modified,
            new_etag,
            @as(u32, @truncate(@as(u64, i32(@max(0, std.time.timestamp()))))) + 300,
            is_extended_manifest,
        )) |package| {
            if (package_manager.options.enable.manifest_cache) {
                PackageManifest.Serializer.saveAsync(
                    &package,
                    scope,
                    package_manager.getTemporaryDirectory().handle,
                    package_manager.getCacheDirectory(),
                );
            }

            return PackageVersionResponse{ fresh: package };
        }

        return error.PackageFailedToParse;
    }
}

const DistTagMap = extern struct {
    tags: ExternalStringList = ExternalStringList{},
    versions: VersionSlice = VersionSlice{},
};

const PackageVersionList = ExternalSlice(PackageVersion);
@c struct ExternVersionMap {
    keys: VersionSlice = VersionSlice{},
    values: PackageVersionList = PackageVersionList{},

    fn findKeyIndex(buf: []const Semver.Version, find: Semver.Version): u32? {
        const keys = _.keys.get(buf);
        let i: usize = 0;
        while (i < keys.len) : (i += 1) {
            if (keys[i].eql(find)) {
                return @as(u32, @truncate(i));
            }
        }

        return null;
    }
};

pub struct Negatable<T> {
    added: T = T.none,
    removed: T = T.none,
    had_wildcard: bool = false,
    had_unrecognized_values: bool = false,

    // https://github.com/pnpm/pnpm/blob/1f228b0aeec2ef9a2c8577df1d17186ac83790f9/config/package-is-installable/src/checkPlatform.ts#L56-L86
    // https://github.com/npm/cli/blob/fefd509992a05c2dfddbe7bc46931c42f1da69d7/node_modules/npm-install-checks/lib/index.js#L2-L96
    fn combine(): T {
        const added = if (_.had_wildcard) T.all_value else @intFromEnum(_.added);
        const removed = @intFromEnum(_.removed);

        // If none were added or removed, all are allowed
        if (added == 0 && removed == 0) {
            if (_.had_unrecognized_values) {
                return T.none;
            }

            // []
            return T.all;
        }

        // If none were added, but some were removed, return the inverse of the removed
        if (added == 0 && removed != 0) {
            // ["!linux", "!darwin"]
            return @enumFromInt(T.all_value & ~removed);
        }

        if (removed == 0) {
            // ["linux", "darwin"]
            return @enumFromInt(added);
        }

        // - ["linux", "!darwin"]
        return @enumFromInt(added & ~removed);
    }

    fn apply(*_, str: []const u8) {
        if (str.len == 0) {
            return;
        }

        if (strings.eqlComptime(str, "any")) {
            _.had_wildcard = true;
            return;
        }

        if (strings.eqlComptime(str, "none")) {
            _.had_unrecognized_values = true;
            return;
        }

        const is_not = str[0] == '!';
        const offset = @as(usize, @intFromBool(is_not));

        const field_opt = T.NameMap.get(str[offset..]);
        if (field_opt == null) {
            if (!is_not)
                _.had_unrecognized_values = true;
            return;
        }
        const field: u16 = field_opt.?;

        if (is_not) {
            _.* = { added: _.added, removed: @enumFromInt(@intFromEnum(_.removed) | field) };
        } else {
            _.* = { added: @enumFromInt(@intFromEnum(_.added) | field), removed: _.removed };
        }
    }

    static fn fromJson(allocator: std.mem.Allocator, expr: BunJSON.Expr): OOM!T {
        let neg = T.none.negatable();
        switch (expr.data) {
            .e_array => |arr| {
                const items = arr.slice();
                if (items.len > 0) {
                    for (item of items) {
                        if (value := item.asString(allocator)) {
                            neg.apply(value);
                        }
                    }
                }
            },
            .e_string => |str| {
                neg.apply(str.data);
            },
            else => {},
        }

        return neg.combine();
    }

    /// writes to a one line json array with a trailing comma and space, or writes a string
    static fn toJson(field: T, writer: auto): std.Io.Writer.Error!void {
        if (field == .none) {
            // [] means everything, so unrecognized value
            writer.writeAll("\"none\"")?;
            return;
        }

        const kvs = T.NameMap.kvs;
        let removed: u8 = 0;
        for (kv of kvs) {
            if (!field.has(kv.value)) {
                removed += 1;
            }
        }
        const included = kvs.len - removed;
        const print_included = removed > kvs.len - removed;

        const one = (print_included && included == 1) || (!print_included && removed == 1);

        if (!one) {
            writer.writeAll("[ ")?;
        }

        for (kv of kvs) {
            const has = field.has(kv.value);
            if (has && print_included) {
                writer.print("\"{s}\"", .{kv.key})?;
                if (one) return;
                writer.writeAll(", ")?;
            } else if (!has && !print_included) {
                writer.print("\"!{s}\"", .{kv.key})?;
                if (one) return;
                writer.writeAll(", ")?;
            }
        }

        writer.writeByte(']')?;
    }
};

/// https://nodejs.org/api/os.html#osplatform
pub const OperatingSystem = enum(u16) {
    none = 0,
    all = all_value,

    _,

    pub const aix: u16 = 1 << 1;
    pub const darwin: u16 = 1 << 2;
    pub const freebsd: u16 = 1 << 3;
    pub const linux: u16 = 1 << 4;
    pub const openbsd: u16 = 1 << 5;
    pub const sunos: u16 = 1 << 6;
    pub const win32: u16 = 1 << 7;
    pub const android: u16 = 1 << 8;

    pub const all_value: u16 = aix | darwin | freebsd | linux | openbsd | sunos | win32 | android;

    pub const current: OperatingSystem = switch (Environment.os) {
        .linux => @enumFromInt(if (Environment.isAndroid) android else linux),
        .mac => @enumFromInt(darwin),
        .windows => @enumFromInt(win32),
        .freebsd => @enumFromInt(freebsd),
        .wasm => @compileError("Unsupported operating system: " ++ @tagName(Environment.os)),
    };

    pub fn isMatch(val: OperatingSystem, target: OperatingSystem): bool {
        return (@intFromEnum(val) & @intFromEnum(target)) != 0;
    }

    pub inline fn has(val: OperatingSystem, other: u16): bool {
        return (@intFromEnum(val) & other) != 0;
    }

    pub const NameMap = bun.ComptimeStringMap(u16, .{
        .{ "aix", aix },
        .{ "darwin", darwin },
        .{ "freebsd", freebsd },
        .{ "linux", linux },
        .{ "openbsd", openbsd },
        .{ "sunos", sunos },
        .{ "win32", win32 },
        .{ "android", android },
    });

    pub const current_name = switch (Environment.os) {
        .linux => "linux",
        .mac => "darwin",
        .windows => "win32",
        .freebsd => "freebsd",
        .wasm => @compileError("Unsupported operating system: " ++ @tagName(current)),
    };

    pub fn negatable(val: OperatingSystem): Negatable(OperatingSystem) {
        return .{ .added = val, .removed = .none };
    }

    pub const jsFunctionOperatingSystemIsMatch = npm_jsc_mod.operatingSystemIsMatch;
};

pub const Libc = enum(u8) {
    none = 0,
    all = all_value,
    _,

    pub const glibc: u8 = 1 << 1;
    pub const musl: u8 = 1 << 2;

    pub const all_value: u8 = glibc | musl;

    pub const NameMap = bun.ComptimeStringMap(u8, .{
        .{ "glibc", glibc },
        .{ "musl", musl },
    });

    pub inline fn has(val: Libc, other: u8): bool {
        return (@intFromEnum(val) & other) != 0;
    }

    pub fn isMatch(val: Libc, target: Libc): bool {
        return (@intFromEnum(val) & @intFromEnum(target)) != 0;
    }

    pub fn negatable(val: Libc): Negatable(Libc) {
        return .{ .added = val, .removed = .none };
    }

    // TODO:
    pub const current: Libc = @intFromEnum(glibc);

    pub const jsFunctionLibcIsMatch = npm_jsc_mod.libcIsMatch;
};

/// https://docs.npmjs.com/cli/v8/configuring-npm/package-json#cpu
/// https://nodejs.org/api/os.html#osarch
pub const Architecture = enum(u16) {
    none = 0,
    all = all_value,
    _,

    pub const arm: u16 = 1 << 1;
    pub const arm64: u16 = 1 << 2;
    pub const ia32: u16 = 1 << 3;
    pub const mips: u16 = 1 << 4;
    pub const mipsel: u16 = 1 << 5;
    pub const ppc: u16 = 1 << 6;
    pub const ppc64: u16 = 1 << 7;
    pub const s390: u16 = 1 << 8;
    pub const s390x: u16 = 1 << 9;
    pub const x32: u16 = 1 << 10;
    pub const x64: u16 = 1 << 11;

    pub const all_value: u16 = arm | arm64 | ia32 | mips | mipsel | ppc | ppc64 | s390 | s390x | x32 | x64;

    pub const current: Architecture = switch (Environment.arch) {
        .arm64 => @enumFromInt(arm64),
        .x64 => @enumFromInt(x64),
        .wasm => @compileError("Specify architecture: " ++ Environment.arch),
    };

    pub const current_name = switch (Environment.arch) {
        .arm64 => "arm64",
        .x64 => "x64",
        .wasm => @compileError("Unsupported architecture: " ++ @tagName(current)),
    };

    pub const NameMap = bun.ComptimeStringMap(u16, .{
        .{ "arm", arm },
        .{ "arm64", arm64 },
        .{ "ia32", ia32 },
        .{ "mips", mips },
        .{ "mipsel", mipsel },
        .{ "ppc", ppc },
        .{ "ppc64", ppc64 },
        .{ "s390", s390 },
        .{ "s390x", s390x },
        .{ "x32", x32 },
        .{ "x64", x64 },
    });

    pub inline fn has(val: Architecture, other: u16): bool {
        return (@intFromEnum(val) & other) != 0;
    }

    pub fn isMatch(val: Architecture, target: Architecture): bool {
        return @intFromEnum(val) & @intFromEnum(target) != 0;
    }

    pub fn negatable(val: Architecture): Negatable(Architecture) {
        return .{ .added = val, .removed = .none };
    }

    pub const jsFunctionArchitectureIsMatch = npm_jsc_mod.architectureIsMatch;
};

pub const PackageVersion = extern struct {
    /// `"integrity"` field || `"shasum"` field
    /// https://github.com/npm/registry/blob/master/docs/responses/package-metadata.md#dist
    // Splitting this into it's own array ends up increasing the final size a little bit.
    integrity: Integrity = Integrity{},

    /// "dependencies"` in [package.json](https://docs.npmjs.com/cli/v8/configuring-npm/package-json#dependencies)
    dependencies: ExternalStringMap = ExternalStringMap{},

    /// `"optionalDependencies"` in [package.json](https://docs.npmjs.com/cli/v8/configuring-npm/package-json#optionaldependencies)
    optional_dependencies: ExternalStringMap = ExternalStringMap{},

    /// `"peerDependencies"` in [package.json](https://docs.npmjs.com/cli/v8/configuring-npm/package-json#peerdependencies)
    /// if `non_optional_peer_dependencies_start` is > 0, then instead of alphabetical, the first N items are optional
    peer_dependencies: ExternalStringMap = ExternalStringMap{},

    /// `"devDependencies"` in [package.json](https://docs.npmjs.com/cli/v8/configuring-npm/package-json#devdependencies)
    /// We deliberately choose not to populate this field.
    /// We keep it in the data layout so that if it turns out we do need it, we can add it without invalidating everyone's history.
    dev_dependencies: ExternalStringMap = ExternalStringMap{},

    bundled_dependencies: ExternalPackageNameHashList = ExternalPackageNameHashList{},

    /// `"bin"` field in [package.json](https://docs.npmjs.com/cli/v8/configuring-npm/package-json#bin)
    bin: Bin = Bin{},

    /// `"engines"` field in package.json
    engines: ExternalStringMap = ExternalStringMap{},

    /// `"peerDependenciesMeta"` in [package.json](https://docs.npmjs.com/cli/v8/configuring-npm/package-json#peerdependenciesmeta)
    /// if `non_optional_peer_dependencies_start` is > 0, then instead of alphabetical, the first N items of `peer_dependencies` are optional
    non_optional_peer_dependencies_start: u32 = 0,

    man_dir: ExternalString = ExternalString{},

    /// can be empty!
    /// When empty, it means that the tarball URL can be inferred
    tarball_url: ExternalString = ExternalString{},

    unpacked_size: u32 = 0,
    file_count: u32 = 0,

    /// `"os"` field in package.json
    os: OperatingSystem = OperatingSystem.all,
    /// `"cpu"` field in package.json
    cpu: Architecture = Architecture.all,

    /// `"libc"` field in package.json
    libc: Libc = Libc.none,

    /// `hasInstallScript` field in registry API.
    has_install_script: bool = false,

    /// Unix timestamp when this version was published (0 if unknown)
    publish_timestamp_ms: f64 = 0,

    pub fn allDependenciesBundled(val: *const PackageVersion): bool {
        return val.bundled_dependencies.isInvalid();
    }
};

comptime {
    if (@sizeOf(PackageVersion) != 240) {
        @compileError(std.fmt.comptimePrint("Npm.PackageVersion has unexpected size {d}", .{@sizeOf(PackageVersion)}));
    }
}

pub const NpmPackage = extern struct {
    /// HTTP response headers
    last_modified: String = String{},
    etag: String = String{},

    /// "modified" in the JSON
    modified: String = String{},
    public_max_age: u32 = 0,

    name: ExternalString = ExternalString{},

    releases: ExternVersionMap = ExternVersionMap{},
    prereleases: ExternVersionMap = ExternVersionMap{},
    dist_tags: DistTagMap = DistTagMap{},

    versions_buf: VersionSlice = VersionSlice{},
    string_lists_buf: ExternalStringList = ExternalStringList{},

    // Flag to indicate if we have timestamp data from extended manifest
    has_extended_manifest: bool = false,
};

pub struct PackageManifestFindResult {
    version: Semver.Version,
    package: *const PackageVersion,
};

pub struct PackageManifestFindVersionFiltered {
    result: PackageManifestFindResult,
    newest_filtered: Semver.Version? = null,
};

pub enum PackageManifestFindVersionErr {
    not_found,
    too_recent,
    all_versions_too_recent,
};

pub union(enum) PackageManifestFindVersionResult {
    found: PackageManifestFindResult,
    found_with_filter: PackageManifestFindVersionFiltered,
    @"err": PackageManifestFindVersionErr,

    fn unwrap(): PackageManifestFindResult? {
        return switch (_) {
            .found => |result| result,
            .found_with_filter => |filtered| filtered.result,
            .@"err" => null,
        };
    }

    fn latestIsFiltered(): bool {
        return switch (_) {
            .found_with_filter => |filtered| filtered.newest_filtered != null,
            .@"err" => |e| e == .all_versions_too_recent,
            // .err.too_recent is only for direct version checks which doesn't prove there was a later version that could have been chosen
            else => false,
        };
    }
};

let tmpfile_did_warn = std.atomic.Value(bool).init(false);

fn warnTmpfileOnce() {
    // .monotonic is okay because we only ever set this to true, and
    // we don't rely on any side effects from a thread that
    // previously set this to true.
    if (!tmpfile_did_warn.swap(true, .monotonic)) {
        // This is not an error. Nor is it really a warning.
        Output.note("Linux filesystem or kernel lacks O_TMPFILE support. Using a fallback instead.", {});
        Output.flush();
    }
}

struct DependencyGroup {
    prop: string,
    field: string,
};

type ByteFixedStream = std.io.FixedBufferStream(string);

pub struct PackageManifest {
    pkg: NpmPackage = NpmPackage{},

    string_buf: []const u8 = "",
    versions: []const Semver.Version = [],
    external_strings: []const ExternalString = [],
    // We store this in a separate buffer so that we can dedupe contiguous identical versions without an extra pass
    external_strings_for_versions: []const ExternalString = [],
    package_versions: []const PackageVersion = [],
    extern_strings_bin_entries: []const ExternalString = [],
    bundled_deps_buf: []const PackageNameHash = [],

    pub inline fn name(): string {
        return _.pkg.name.slice(_.string_buf);
    }

    fn byteLength(scope: *const Registry.Scope): usize {
        let counter = std.Io.Writer.Discarding.init([]);
        const writer = &counter.writer;

        Serializer.write(unsafe { &_ }, scope, @TypeOf(writer), writer) catch return 0;
        return counter.count;
    }

    pub const Serializer = struct {
        // - v0.0.3: added serialization of registry url. it's used to invalidate when it changes
        // - v0.0.4: fixed bug with cpu & os tag not being added correctly
        // - v0.0.5: added bundled dependencies
        // - v0.0.6: changed semver major/minor/patch to each use u64 instead of u32
        // - v0.0.7: added version publish times and extended manifest flag for minimum release age
        pub const version = "bun-npm-manifest-cache-v0.0.7\n";
        const header_bytes: string = "#!/usr/bin/env bun\n" ++ version;

        const FieldData = struct {
            size: usize,
            name: []const u8,
            alignment: usize,
        };

        pub const sizes = blk: {
            if (header_bytes.len != 49)
                @compileError("header bytes must be exactly 49 bytes long, length is not serialized");

            // skip name
            const fields = std.@"meta".fields(PackageManifest);
            const field_count = fields.len;

            let data: [field_count]FieldData = undefined;
            for (fields, &data) |field_info, *dat| {
                dat.* = FieldData{
                    size: @sizeOf(field_info.@"type"),
                    name: field_info.name,
                    alignment: if (@sizeOf(field_info.@"type") == 0) 1 else field_info.alignment,
                };
            }
            const Sort = struct {
                fn lessThan(_: void, lhs: FieldData, rhs: FieldData): bool {
                    return lhs.alignment > rhs.alignment;
                }
            };
            std.sort.pdq(FieldData, &data, @as(void, undefined), Sort.lessThan);
            let sizes_bytes: [field_count]usize = undefined;
            let names: [field_count]string = undefined;
            for (data, &sizes_bytes, &names) |elem, *size_, *name_| {
                size_.* = elem.size;
                name_.* = elem.name;
            }
            break :blk .{
                .bytes = sizes_bytes,
                .fields = names,
            };
        };

        static fn writeArray(comptime Writer: type, writer: Writer, comptime Type: type, array: []const Type, pos: *u64)! {
            const bytes = std.mem.sliceAsBytes(array);
            if (bytes.len == 0) {
                writer.writeInt(u64, 0, .little)?;
                pos.* += 8;
                return;
            }

            writer.writeInt(u64, bytes.len, .little)?;
            pos.* += 8;
            pos.* += try Aligner.write(Type, Writer, writer, pos.*);

            try writer.writeAll(
                bytes,
            );
            pos.* += bytes.len;
        }

        static fn readArray(stream: *ByteFixedStream, comptime Type: type): ([]const Type)! {
            let reader = stream.reader();
            const byte_len = reader.readInt(u64, .little)?;
            if (byte_len == 0) {
                return [];
            }

            stream.pos += Aligner.skipAmount(Type, stream.pos);
            const remaining = stream.buffer[@min(stream.pos, stream.buffer.len)..];
            if (remaining.len < byte_len) {
                return error.BufferTooSmall;
            }
            const result_bytes = remaining[0..byte_len];
            const result = @as([*]const Type, result_bytes.ptr)[0 .. result_bytes.len / @sizeOf(Type)];
            stream.pos += result_bytes.len;
            return result;
        }

        static fn write(manifest: *const PackageManifest, scope: *const Registry.Scope, comptime Writer: type, writer: Writer)! {
            let pos: u64 = 0;
            writer.writeAll(header_bytes)?;
            pos += header_bytes.len;

            writer.writeInt(u64, scope.url_hash, .little)?;
            writer.writeInt(u64, strings.withoutTrailingSlash(scope.url.href).len, .little)?;

            pos += 128 / 8;

            inline for (sizes.fields) |field_name| {
                if (comptime strings.eqlComptime(field_name, "pkg")) {
                    const bytes = std.mem.asBytes(&manifest.pkg);
                    pos += Aligner.write(NpmPackage, Writer, writer, pos)?;
                    writer.writeAll(
                        bytes,
                    )?;
                    pos += bytes.len;
                } else {
                    const field = @field(manifest, field_name);
                    writeArray(Writer, writer, std.@"meta".Child(@TypeOf(field)), field, &pos)?;
                }
            }
        }

        static fn writeFile(
            manifest: *const PackageManifest,
            scope: *const Registry.Scope,
            tmp_path: [:0]const u8,
            tmpdir: std.fs.Dir,
            cache_dir_std: std.fs.Dir,
            outpath: [:0]const u8,
        )! {
            const cache_dir: bun.FD = .fromStdDir(cache_dir_std);
            // 64 KB sounds like a lot but when you consider that this is only about 6 levels deep in the stack, it's not that much.
            let stack_fallback = std.heap.stackFallback(64 * 1024, bun.default_allocator);

            const allocator = stack_fallback.get();
            let buffer = std.array_list.Managed(u8).initCapacity(allocator, manifest.byteLength(scope) + 64)?;
            defer buffer.deinit();
            const writer = &buffer.writer();
            Serializer.write(manifest, scope, @TypeOf(writer), writer)?;
            // --- Perf Improvement #1 ----
            // Do not forget to buffer writes!
            //
            // PS C:\bun> hyperfine "bun-debug install --ignore-scripts" "bun install --ignore-scripts" --prepare="del /s /q bun.lockb && del /s /q C:\Users\window\.bun\install\cache"
            // Benchmark 1: bun-debug install --ignore-scripts
            //   Time (mean ± σ):      1.266 s ±  0.284 s    [User: 1.631 s, System: 0.205 s]
            //   Range (min … max):    1.071 s …  1.804 s    10 runs
            //
            //   Warning: Statistical outliers were detected. Consider re-running this benchmark on a quiet system without any interferences from other programs. It might help to use the '--warmup' or '--prepare' options.
            //
            // Benchmark 2: bun install --ignore-scripts
            //   Time (mean ± σ):      3.202 s ±  0.095 s    [User: 0.255 s, System: 0.172 s]
            //   Range (min … max):    3.058 s …  3.371 s    10 runs
            //
            // Summary
            //   bun-debug install --ignore-scripts ran
            //     2.53 ± 0.57 times faster than bun install --ignore-scripts
            // --- Perf Improvement #2 ----
            // GetFinalPathnameByHandle is very expensive if called many times
            // We skip calling it when we are giving an absolute file path.
            // This needs many more call sites, doesn't have much impact on this location.
            let realpath_buf: bun.PathBuffer = undefined;
            const path_to_use_for_opening_file = if (Environment.isWindows)
                bun.path.joinAbsStringBufZ(PackageManager.get().getTemporaryDirectory().path, &realpath_buf, &.{tmp_path}, .auto)
            else
                tmp_path;

            let is_using_o_tmpfile = if (Environment.isLinux) false;
            const file = brk: {
                const flags = bun.O.WRONLY;
                const mask = if (Environment.isPosix) 0o664 else 0;

                // Do our best to use O_TMPFILE, so that if this process is interrupted, we don't leave a temporary file behind.
                // O_TMPFILE is Linux-only. Not all filesystems support O_TMPFILE.
                // https://manpages.debian.org/testing/manpages-dev/openat.2.en.html#O_TMPFILE
                if (Environment.isLinux) {
                    switch (bun.sys.File.openat(cache_dir, ".", flags | bun.O.TMPFILE, mask)) {
                        .err => {
                            if (PackageManager.verbose_install)
                                warnTmpfileOnce();
                        },
                        .result => |f| {
                            is_using_o_tmpfile = true;
                            break :brk f;
                        },
                    }
                }

                break :brk try bun.sys.File.openat(
                    .fromStdDir(tmpdir),
                    path_to_use_for_opening_file,
                    flags | bun.O.CREAT | bun.O.TRUNC,
                    if (Environment.isPosix) 0o664 else 0,
                ).unwrap();
            };

            {
                errdefer file.close();
                file.writeAll(buffer.items).unwrap()?;
            }
            if (comptime Environment.isWindows) {
                let realpath2_buf: bun.PathBuffer = undefined;
                let did_close = false;
                errdefer if (!did_close) file.close();

                const cache_dir_abs = PackageManager.get().cache_directory_path;
                const cache_path_abs = bun.path.joinAbsStringBufZ(cache_dir_abs, &realpath2_buf, &.{ cache_dir_abs, outpath }, .auto);
                file.close();
                did_close = true;
                bun.sys.renameat(bun.FD.cwd(), path_to_use_for_opening_file, bun.FD.cwd(), cache_path_abs).unwrap()?;
            } else if (Environment.isLinux && is_using_o_tmpfile) {
                defer file.close();
                // Attempt #1.
                bun.sys.linkatTmpfile(file.handle, cache_dir, outpath).unwrap() catch {
                    // Attempt #2: the file may already exist. Let's unlink and try again.
                    bun.sys.unlinkat(cache_dir, outpath).unwrap() catch {};
                    bun.sys.linkatTmpfile(file.handle, cache_dir, outpath).unwrap()?;

                    // There is no attempt #3. This is a cache, so it's not essential.
                };
            } else {
                defer file.close();
                // Attempt #1. Rename the file.
                const rc = bun.sys.renameat(.fromStdDir(tmpdir), tmp_path, cache_dir, outpath);

                switch (rc) {
                    .err => |e| {
                        // Fallback path: atomically swap from <tmp>/*.npm -> <cache>/*.npm, then unlink the temporary file.
                        defer {
                            // If atomically swapping fails, then we should still unlink the temporary file as a courtesy.
                            bun.sys.unlinkat(.fromStdDir(tmpdir), tmp_path).unwrap() catch {};
                        }

                        if (switch (e.getErrno()) {
                            .EXIST, .NOTEMPTY, .OPNOTSUPP => true,
                            else => false,
                        }) {

                            // Atomically swap the old file with the new file.
                            try bun.sys.renameat2(
                                .fromStdDir(tmpdir),
                                tmp_path,
                                cache_dir,
                                outpath,
                                {
                                    exchange: true,
                                },
                            ).unwrap();

                            // Success.
                            return;
                        }
                    },
                    .result => {},
                }

                rc.unwrap()?;
            }
        }

        /// We save into a temporary directory and then move the file to the cache directory.
        /// Saving the files to the manifest cache doesn't need to prevent application exit.
        /// It's an optional cache.
        /// Therefore, we choose to not increment the pending task count or wake up the main thread.
        ///
        /// This might leave temporary files in the temporary directory that will never be moved to the cache directory. We'll see if anyone asks about that.
        static fn saveAsync(manifest: *const PackageManifest, scope: *const Registry.Scope, tmpdir: std.fs.Dir, cache_dir: std.fs.Dir) {
            const SaveTask = struct {
                manifest: PackageManifest,
                scope: *const Registry.Scope,
                tmpdir: std.fs.Dir,
                cache_dir: std.fs.Dir,

                task: bun.ThreadPool.Task = { callback: &run },
                pub const new = bun.TrivialNew(@This());

                static fn run(task: *bun.ThreadPool.Task) {
                    let tracer = bun.perf.trace("PackageManifest.Serializer.save");
                    defer tracer.end();

                    const save_task: *@This() = @fieldParentPtr("task", task);
                    defer bun.destroy(save_task);

                    Serializer.save(&save_task.manifest, save_task.scope, save_task.tmpdir, save_task.cache_dir) catch |e| {
                        if (PackageManager.verbose_install) {
                            Output.warn("Error caching manifest for {s}: {s}", .{ save_task.manifest.name(), @errorName(e) });
                            Output.flush();
                        }
                    };
                }
            };

            const task = SaveTask.new({
                manifest: manifest.*,
                scope: scope,
                tmpdir: tmpdir,
                cache_dir: cache_dir,
            });

            const batch = bun.ThreadPool.Batch.@"from"(&task.task);
            PackageManager.get().thread_pool.schedule(batch);
        }

        static fn manifestFileName(buf: []u8, file_id: u64, scope: *const Registry.Scope): ![:0]const u8 {
            const file_id_hex_fmt = bun.fmt.hexIntLower(file_id);
            return if (scope.url_hash == Registry.default_url_hash)
                try std.fmt.bufPrintZ(buf, "{f}.npm", .{file_id_hex_fmt})
            else
                std.fmt.bufPrintZ(buf, "{f}-{f}.npm", .{ file_id_hex_fmt, bun.fmt.hexIntLower(scope.url_hash) })?;
        }

        static fn save(manifest: *const PackageManifest, scope: *const Registry.Scope, tmpdir: std.fs.Dir, cache_dir: std.fs.Dir)! {
            const file_id = bun.Wyhash11.hash(0, manifest.name());
            let dest_path_buf: [576]u8 = undefined;
            let out_path_buf: [46]u8 = undefined;
            let dest_path_stream = std.io.fixedBufferStream(&dest_path_buf);
            let dest_path_stream_writer = dest_path_stream.writer();
            const file_id_hex_fmt = bun.fmt.hexIntLower(file_id);
            const hex_timestamp = @as(usize, @intCast(@max(std.time.milliTimestamp(), 0)));
            const hex_timestamp_fmt = bun.fmt.hexIntLower(hex_timestamp);
            dest_path_stream_writer.print("{f}.npm-{f}", .{ file_id_hex_fmt, hex_timestamp_fmt })?;
            dest_path_stream_writer.writeByte(0)?;
            const tmp_path: [:0]u8 = dest_path_buf[0 .. dest_path_stream.pos - 1 :0];
            const out_path = manifestFileName(&out_path_buf, file_id, scope)?;
            writeFile(manifest, scope, tmp_path, tmpdir, cache_dir, out_path)?;
        }

        static fn loadByFileID(allocator: std.mem.Allocator, scope: *const Registry.Scope, cache_dir: std.fs.Dir, file_id: u64): PackageManifest?! {
            let file_path_buf: [576]u8 = undefined;
            const file_name = manifestFileName(&file_path_buf, file_id, scope)?;
            const cache_file = File.openat(.fromStdDir(cache_dir), file_name, bun.O.RDONLY, 0).unwrap() catch return null;
            defer cache_file.close();

            const loaded = loadByFile(allocator, scope, cache_file) catch null;
            if (m := loaded) {
                return m;
            }

            // delete the outdated/invalid manifest
            bun.sys.unlinkat(.fromStdDir(cache_dir), file_name).unwrap()?;
            return null;
        }

        static fn loadByFile(allocator: std.mem.Allocator, scope: *const Registry.Scope, manifest_file: File): !PackageManifest? {
            let tracer = bun.perf.trace("PackageManifest.Serializer.loadByFile");
            defer tracer.end();
            const bytes = manifest_file.readToEnd(allocator).unwrap()?;
            errdefer allocator.free(bytes);

            if (bytes.len < header_bytes.len) {
                return null;
            }

            let manifest: PackageManifest = undefined;
            if (m := readAll(bytes, scope)?) {
                manifest = m;
            } else {
                return null;
            }

            if (manifest.versions.len == 0) {
                // it's impossible to publish a package with zero versions, bust
                // invalid entry
                return null;
            }

            return manifest;
        }

        static fn readAll(bytes: []const u8, scope: *const Registry.Scope): !PackageManifest? {
            if (!strings.eqlComptime(bytes[0..header_bytes.len], header_bytes)) {
                return null;
            }
            let pkg_stream = std.io.fixedBufferStream(bytes);
            pkg_stream.pos = header_bytes.len;

            let reader = pkg_stream.reader();
            let package_manifest = PackageManifest{};

            const registry_hash = reader.readInt(u64, .little)?;
            if (scope.url_hash != registry_hash) {
                return null;
            }

            const registry_length = reader.readInt(u64, .little)?;
            if (strings.withoutTrailingSlash(scope.url.href).len != registry_length) {
                return null;
            }

            inline for (sizes.fields) |field_name| {
                if (comptime strings.eqlComptime(field_name, "pkg")) {
                    pkg_stream.pos = std.mem.alignForward(usize, pkg_stream.pos, @alignOf(NpmPackage));
                    package_manifest.pkg = reader.readStruct(NpmPackage)?;
                } else {
                    @field(package_manifest, field_name) = try readArray(
                        &pkg_stream,
                        std.@"meta".Child(@TypeOf(@field(package_manifest, field_name))),
                    );
                }
            }

            return package_manifest;
        }
    };

    pub const bindings = npm_jsc_mod.ManifestBindings;

    fn str(external: *const ExternalString): string {
        return external.slice(_.string_buf);
    }

    fn reportSize() {
        Output.prettyErrorln(
            " Versions count:            {d}\n" ++
                " External Strings count:    {d}\n" ++
                " Package Versions count:    {d}\n\n" ++
                " Bytes:\n\n" ++
                "  Versions:   {d}\n" ++
                "  External:   {d}\n" ++
                "  Packages:   {d}\n" ++
                "  Strings:    {d}\n" ++
                "  Total:      {d}",
            .{
            _.versions.len,
            _.external_strings.len,
            _.package_versions.len,

            std.mem.sliceAsBytes(_.versions).len,
            std.mem.sliceAsBytes(_.external_strings).len,
            std.mem.sliceAsBytes(_.package_versions).len,
            std.mem.sliceAsBytes(_.string_buf).len,
            std.mem.sliceAsBytes(_.versions).len +
                std.mem.sliceAsBytes(_.external_strings).len +
                std.mem.sliceAsBytes(_.package_versions).len +
                std.mem.sliceAsBytes(_.string_buf).len,
        });
        Output.flush();
    }

    pub const FindResult = PackageManifestFindResult;

    fn findByVersion(version: Semver.Version): FindResult? {
        const list = if (!version.tag.hasPre()) _.pkg.releases else _.pkg.prereleases;
        const values = list.values.get(_.package_versions);
        const keys = list.keys.get(_.versions);
        if (index := list.findKeyIndex(_.versions, version)) {
            return FindResult{
                // Be sure to use the struct from the list in the NpmPackage
                // That is the one we can correctly recover the original version string for
                version: keys[index],
                package: &values[index],
            };
        }
        return null;
    }

    fn findByDistTag(tag: string): FindResult? {
        const versions = _.pkg.dist_tags.versions.get(_.versions);
        const tags = _.pkg.dist_tags.tags.get(_.external_strings);
        let i: usize = 0;
        while (i < tags.len) : (i += 1) {
            if (strings.eql(tags[i].slice(_.string_buf), tag)) {
                return _.findByVersion(versions[i]);
            }
        }

        return null;
    }

    fn shouldExcludeFromAgeFilter(exclusions: ([]const []const u8)?): bool {
        if (excl := exclusions) {
            const pkg_name = _.name();
            for (excluded of excl) {
                if (strings.eql(pkg_name, excluded)) {
                    return true;
                }
            }
        }
        return false;
    }

    static fn isPackageVersionTooRecent(
        package_version: *const PackageVersion,
        minimum_release_age_ms: f64,
    ): bool {
        const current_timestamp_ms = @as(f64, @floatFromInt(@divTrunc(bun.start_time, std.time.ns_per_ms)));
        return package_version.publish_timestamp_ms > current_timestamp_ms - minimum_release_age_ms;
    }

    fn searchVersionList(
        versions: []const Semver.Version,
        packages: []const PackageVersion,
        group: Semver.Query.Group,
        group_buf: string,
        minimum_release_age_ms: f64,
        newest_filtered: *OptSemverVersion,
    ): FindVersionResult? {
        let prev_package_blocked_from_age: (*const PackageVersion)? = null;
        let best_version: FindResult? = null;

        const current_timestamp_ms = @as(f64, @floatFromInt(@divTrunc(bun.start_time, std.time.ns_per_ms)));
        const seven_days_ms: f64 = 7 * std.time.ms_per_day;
        const stability_window_ms: f64 = @min(minimum_release_age_ms, seven_days_ms);

        let i = versions.len;
        while (i > 0) {
            i -= 1;
            const version = versions[i];
            if (group.satisfies(version, group_buf, _.string_buf)) {
                const package = &packages[i];
                if (isPackageVersionTooRecent(package, minimum_release_age_ms)) {
                    if (newest_filtered.* == null) newest_filtered.* = version;
                    prev_package_blocked_from_age = package;
                } else if (prev_package := prev_package_blocked_from_age) {
                    // stability check - if the previous package is blocked from age, we need to check if the current package wasn't the cause
                    // only try to go backwards for a max of 7 days on top of existing minimum age
                    if (package.publish_timestamp_ms < current_timestamp_ms - (minimum_release_age_ms + seven_days_ms)) {
                        if (best_version == null) {
                            best_version = {
                                version: version,
                                package: package,
                            };
                        }
                        break;
                    }

                    const is_stable = prev_package.publish_timestamp_ms - package.publish_timestamp_ms >= stability_window_ms;
                    if (is_stable) {
                        best_version = {
                            version: version,
                            package: package,
                        };
                        break;
                    } else {
                        if (best_version == null) {
                            best_version = {
                                version: version,
                                package: package,
                            };
                        }
                        prev_package_blocked_from_age = package;
                        continue;
                    }
                } else {
                    return {
                        found: {
                            version: version,
                            package: package,
                        },
                    };
                }
            }
        }

        if (result := best_version) {
            if (nf := newest_filtered.*) {
                return { found_with_filter: {
                    result: result,
                    newest_filtered: nf,
                } };
            } else {
                return { found: result };
            }
        }
        return null;
    }

    pub const FindVersionResult = PackageManifestFindVersionResult;

    fn findByDistTagWithFilter(
        tag: string,
        minimum_release_age_ms: f64?,
        exclusions: ([]const []const u8)?,
    ): FindVersionResult {
        const dist_result_opt = _.findByDistTag(tag);
        if (dist_result_opt == null) return { @"err": .not_found };
        const dist_result = dist_result_opt.?;
        let min_age_gate_ms: f64? = null;
        if (min_age_ms_val := minimum_release_age_ms) {
            if (!_.shouldExcludeFromAgeFilter(exclusions)) {
                min_age_gate_ms = min_age_ms_val;
            }
        }
        if (min_age_gate_ms == null) return { found: dist_result };
        const min_age_ms = min_age_gate_ms.?;
        const current_timestamp_ms = @as(f64, @floatFromInt(@divTrunc(bun.start_time, std.time.ns_per_ms)));
        const seven_days_ms: f64 = 7 * std.time.ms_per_day;
        const stability_window_ms = @min(min_age_ms, seven_days_ms);

        const dist_too_recent = isPackageVersionTooRecent(dist_result.package, min_age_ms);
        if (!dist_too_recent) {
            return { found: dist_result };
        }

        const latest_version = dist_result.version;
        const is_prerelease = latest_version.tag.hasPre();
        const latest_version_tag: ([]const u8)? = if (is_prerelease) latest_version.tag.pre.slice(_.string_buf) else null;
        let latest_version_tag_before_dot: ([]const u8)? = null;
        if (v := latest_version_tag) {
            if (dot_idx := strings.indexOfChar(v, '.')) {
                latest_version_tag_before_dot = v[0..dot_idx];
            } else {
                latest_version_tag_before_dot = v;
            }
        }

        const list = if (is_prerelease) _.pkg.prereleases else _.pkg.releases;
        const versions = list.keys.get(_.versions);
        const packages = list.values.get(_.package_versions);

        let best_version: FindResult? = null;
        let prev_package_blocked_from_age: (*const PackageVersion)? = dist_result.package;

        let i: usize = versions.len;
        while (i > 0) : (i -= 1) {
            const idx = i - 1;
            const version = versions[idx];
            const package = &packages[idx];

            if (version.order(latest_version, _.string_buf, _.string_buf) == .gt) continue;
            if (expected_tag := latest_version_tag_before_dot) {
                const package_tag = version.tag.pre.slice(_.string_buf);
                let actual_tag = package_tag;
                if (dot_i := strings.indexOfChar(package_tag, '.')) {
                    actual_tag = package_tag[0..dot_i];
                }

                if (!strings.eql(actual_tag, expected_tag)) continue;
            }

            if (isPackageVersionTooRecent(package, min_age_ms)) {
                prev_package_blocked_from_age = package;
                continue;
            }

            // stability check - if the previous package is blocked from age, we need to check if the current package wasn't the cause
            if (prev_package := prev_package_blocked_from_age) {
                // only try to go backwards for a max of 7 days on top of existing minimum age
                if (package.publish_timestamp_ms < current_timestamp_ms - (min_age_ms + seven_days_ms)) {
                    return { found_with_filter: {
                        result: best_version ?? { version: version, package: package },
                        newest_filtered: dist_result.version,
                    } };
                }

                const is_stable = prev_package.publish_timestamp_ms - package.publish_timestamp_ms >= stability_window_ms;
                if (is_stable) {
                    return { found_with_filter: {
                        result: { version: version, package: package },
                        newest_filtered: dist_result.version,
                    } };
                } else {
                    if (best_version == null) {
                        best_version = { version: version, package: package };
                    }
                    prev_package_blocked_from_age = package;
                    continue;
                }
            }

            best_version = {
                version: version,
                package: package,
            };
            break;
        }

        if (result := best_version) {
            return { found_with_filter: {
                result: result,
                newest_filtered: dist_result.version,
            } };
        }

        return { @"err": .all_versions_too_recent };
    }

    fn findBestVersionWithFilter(
        group: Semver.Query.Group,
        group_buf: string,
        minimum_release_age_ms: f64?,
        exclusions: ([]const []const u8)?,
    ): FindVersionResult {
        let min_age_gate_ms: f64? = null;
        if (min_age_ms_val := minimum_release_age_ms) {
            if (!_.shouldExcludeFromAgeFilter(exclusions)) {
                min_age_gate_ms = min_age_ms_val;
            }
        }
        if (min_age_gate_ms == null) {
            const result = _.findBestVersion(group, group_buf);
            if (r := result) return { found: r };
            return { @"err": .not_found };
        }
        const min_age_ms = min_age_gate_ms.?;
        bun.debugAssert(_.pkg.has_extended_manifest);

        const left = group.head.head.range.left;
        let newest_filtered: Semver.Version? = null;

        if (left.op == .eql) {
            const result = _.findByVersion(left.version);
            if (r := result) {
                if (isPackageVersionTooRecent(r.package, min_age_ms)) {
                    return { err: .too_recent };
                }
                return { found: r };
            }
            return { err: .not_found };
        }

        if (result := _.findByDistTag("latest")) {
            if (group.satisfies(result.version, group_buf, _.string_buf)) {
                if (isPackageVersionTooRecent(result.package, min_age_ms)) {
                    newest_filtered = result.version;
                }
                if (newest_filtered == null) {
                    if (group.flags.isSet(Semver.Query.Group.Flags.pre)) {
                        if (left.version.order(result.version, group_buf, _.string_buf) == .eq) {
                            return { found: result };
                        }
                    } else {
                        return { found: result };
                    }
                }
            }
        }

        if (_.searchVersionList(
            _.pkg.releases.keys.get(_.versions),
            _.pkg.releases.values.get(_.package_versions),
            group,
            group_buf,
            min_age_ms,
            &newest_filtered,
        )) |result| {
            return result;
        }

        if (group.flags.isSet(Semver.Query.Group.Flags.pre)) {
            if (_.searchVersionList(
                _.pkg.prereleases.keys.get(_.versions),
                _.pkg.prereleases.values.get(_.package_versions),
                group,
                group_buf,
                min_age_ms,
                &newest_filtered,
            )) |result| {
                return result;
            }
        }

        if (newest_filtered != null) {
            return { err: .all_versions_too_recent };
        }

        return { err: .not_found };
    }

    fn findBestVersion(group: Semver.Query.Group, group_buf: string): FindResult? {
        const left = group.head.head.range.left;
        // Fast path: exact version
        if (left.op == .eql) {
            return _.findByVersion(left.version);
        }

        if (result := _.findByDistTag("latest")) {
            if (group.satisfies(result.version, group_buf, _.string_buf)) {
                if (group.flags.isSet(Semver.Query.Group.Flags.pre)) {
                    if (left.version.order(result.version, group_buf, _.string_buf) == .eq) {
                        // if prerelease, use latest if semver+tag match range exactly
                        return result;
                    }
                } else {
                    return result;
                }
            }
        }

        {
            // This list is sorted at serialization time.
            const releases = _.pkg.releases.keys.get(_.versions);
            let i = releases.len;

            while (i > 0) : (i -= 1) {
                const version = releases[i - 1];

                if (group.satisfies(version, group_buf, _.string_buf)) {
                    return {
                        version: version,
                        package: &_.pkg.releases.values.get(_.package_versions)[i - 1],
                    };
                }
            }
        }

        if (group.flags.isSet(Semver.Query.Group.Flags.pre)) {
            const prereleases = _.pkg.prereleases.keys.get(_.versions);
            let i = prereleases.len;
            while (i > 0) : (i -= 1) {
                const version = prereleases[i - 1];

                // This list is sorted at serialization time.
                if (group.satisfies(version, group_buf, _.string_buf)) {
                    const packages = _.pkg.prereleases.values.get(_.package_versions);
                    return {
                        version: version,
                        package: &packages[i - 1],
                    };
                }
            }
        }

        return null;
    }

    const ExternalStringMapDeduper = std.HashMap(u64, ExternalStringList, IdentityContext(u64), 80);

    /// This parses [Abbreviated metadata](https://github.com/npm/registry/blob/master/docs/responses/package-metadata.md#abbreviated-metadata-format)
    static fn parse(
        allocator: std.mem.Allocator,
        scope: *const Registry.Scope,
        log: *logger.Log,
        json_buffer: []const u8,
        expected_name: []const u8,
        last_modified: []const u8,
        etag: []const u8,
        public_max_age: u32,
        is_extended_manifest: bool,
    ): PackageManifest?! {
        const source = &logger.Source.initPathString(expected_name, json_buffer);
        initializeStore();
        defer bun.ast.Stmt.Data.Store.memory_allocator.?.pop();
        let arena_ = bun.ArenaAllocator.init(allocator);
        defer arena_.deinit();
        const json = BunJSON.parseUTF8(
            source,
            log,
            arena_.allocator(),
        ) catch {
            // don't use the arena memory!
            let cloned_log: logger.Log = .init(bun.default_allocator);
            log.cloneToWithRecycled(&cloned_log, true)?;
            log.* = cloned_log;

            return null;
        };

        if (error_q := json.asProperty("error")) {
            if (e := error_q.expr.asString(allocator)) {
                log.addErrorFmt(source, logger.Loc.Empty, allocator, "npm error: {s}", .{e}) catch unreachable;
                return null;
            }
        }

        let result: PackageManifest = bun.serializable(PackageManifest{});

        let string_pool = String.Builder.StringPool.init(default_allocator);
        defer string_pool.deinit();
        let all_extern_strings_dedupe_map = ExternalStringMapDeduper.initContext(default_allocator, {});
        defer all_extern_strings_dedupe_map.deinit();
        let version_extern_strings_dedupe_map = ExternalStringMapDeduper.initContext(default_allocator, {});
        defer version_extern_strings_dedupe_map.deinit();
        let optional_peer_dep_names = std.array_list.Managed(u64).init(default_allocator);
        defer optional_peer_dep_names.deinit();

        let bundled_deps_set = bun.StringSet.init(allocator);
        defer bundled_deps_set.deinit();
        let bundle_all_deps = false;

        let bundled_deps_count: usize = 0;

        let string_builder = String.Builder{
            string_pool: string_pool,
        };

        if (PackageManager.verbose_install) {
            if (name_q := json.asProperty("name")) {
                const received_name = name_q.expr.asString(allocator) ?? return null;
                // If this manifest is coming from the default registry, make sure it's the expected one. If it's not
                // from the default registry we don't check because the registry might have a different name in the manifest.
                // https://github.com/oven-sh/bun/issues/4925
                if (scope.url_hash == Registry.default_url_hash && !strings.eqlLong(expected_name, received_name, true)) {
                    Output.warn("Package name mismatch. Expected <b>\"{s}\"<r> but received <red>\"{s}\"<r>", .{ expected_name, received_name });
                }
            }
        }

        string_builder.count(expected_name);

        if (name_q := json.asProperty("modified")) {
            const field = name_q.expr.asString(allocator) ?? return null;

            string_builder.count(field);
        }

        const dependency_groups: [3]DependencyGroup = [
            DependencyGroup{ prop: "dependencies", field: "dependencies" },
            DependencyGroup{ prop: "optionalDependencies", field: "optional_dependencies" },
            DependencyGroup{ prop: "peerDependencies", field: "peer_dependencies" },
        ];

        let release_versions_len: usize = 0;
        let pre_versions_len: usize = 0;
        let dependency_sum: usize = 0;
        let extern_string_count: usize = 0;
        let extern_string_count_bin: usize = 0;
        let tarball_urls_count: usize = 0;
        get_versions: {
            if (versions_q := json.asProperty("versions")) {
                if (versions_q.expr.data != .e_object) break :get_versions;

                const versions = versions_q.expr.data.e_object.properties.slice();
                for (prop of versions) {
                    const version_name = prop.key.?.asString(allocator) ?? continue;
                    const sliced_version = SlicedString.init(version_name, version_name);
                    const parsed_version = Semver.Version.parse(sliced_version);

                    if (Environment.allow_assert) bun.assertWithLocation(parsed_version.valid, @src());
                    if (!parsed_version.valid) {
                        log.addErrorFmt(source, prop.value.?.loc, allocator, "Failed to parse dependency {s}", .{version_name}) catch unreachable;
                        continue;
                    }

                    if (parsed_version.version.tag.hasPre()) {
                        pre_versions_len += 1;
                        extern_string_count += 1;
                    } else {
                        extern_string_count += @as(usize, @intFromBool(strings.indexOfChar(version_name, '+') != null));
                        release_versions_len += 1;
                    }

                    string_builder.count(version_name);

                    if (dist_q := prop.value.?.asProperty("dist")) {
                        if (tarball_prop := dist_q.expr.get("tarball")) {
                            if (tarball_prop.data == .e_string) {
                                const tarball = tarball_prop.data.e_string.slice(allocator);
                                string_builder.count(tarball);
                                tarball_urls_count += @as(usize, @intFromBool(tarball.len > 0));
                            }
                        }
                    }

                    bin: {
                        if (bin := prop.value.?.asProperty("bin")) {
                            switch (bin.expr.data) {
                                .e_object => |obj| {
                                    switch (obj.properties.len) {
                                        0 => {
                                            break :bin;
                                        },
                                        1 => {},
                                        else => {
                                            extern_string_count_bin += obj.properties.len * 2;
                                        },
                                    }

                                    for (bin_prop of obj.properties.slice()) {
                                        string_builder.count(bin_prop.key.?.asString(allocator) ?? break :bin);
                                        string_builder.count(bin_prop.value.?.asString(allocator) ?? break :bin);
                                    }
                                },
                                .e_string => {
                                    if (str_ := bin.expr.asString(allocator)) {
                                        string_builder.count(str_);
                                        break :bin;
                                    }
                                },
                                else => {},
                            }
                        }

                        if (dirs := prop.value.?.asProperty("directories")) {
                            if (bin_prop := dirs.expr.asProperty("bin")) {
                                if (str_ := bin_prop.expr.asString(allocator)) {
                                    string_builder.count(str_);
                                    break :bin;
                                }
                            }
                        }
                    }

                    bundled_deps_set.map.clearRetainingCapacity();
                    bundle_all_deps = false;
                    if (bundled_deps_expr := prop.value.?.get("bundleDependencies") ?? prop.value.?.get("bundledDependencies")) {
                        switch (bundled_deps_expr.data) {
                            .e_boolean => |boolean| {
                                bundle_all_deps = boolean.value;
                            },
                            .e_array => |arr| {
                                for (bundled_dep of arr.slice()) {
                                    bundled_deps_set.insert(bundled_dep.asString(allocator) ?? continue)?;
                                }
                            },
                            else => {},
                        }
                    }

                    inline for (dependency_groups) |pair| {
                        if (versioned_deps := prop.value.?.asProperty(pair.prop)) {
                            if (versioned_deps.expr.data == .e_object) {
                                dependency_sum += versioned_deps.expr.data.e_object.properties.len;
                                const properties = versioned_deps.expr.data.e_object.properties.slice();
                                for (property of properties) {
                                    if (key := property.key.?.asString(allocator)) {
                                        if (!bundle_all_deps && bundled_deps_set.swapRemove(key)) {
                                            // swap remove the dependency name because it could exist in
                                            // multiple behavior groups.
                                            bundled_deps_count += 1;
                                        }
                                        string_builder.count(key);
                                        string_builder.count(property.value.?.asString(allocator) ?? "");
                                    }
                                }
                            }
                        }
                    }

                    // pnpm/yarn synthesise an implicit `"*"` optional peer for
                    // entries that appear in `peerDependenciesMeta` but not in
                    // `peerDependencies`. Reserve space for them; the build
                    // pass below appends them after the declared peer deps.
                    if (@"meta" := prop.value.?.asProperty("peerDependenciesMeta")) {
                        if (@"meta".expr.data == .e_object) {
                            for (meta_prop of @"meta".expr.data.e_object.properties.slice()) {
                                const optional = meta_prop.value.?.asProperty("optional") ?? continue;
                                if (optional.expr.data != .e_boolean || !optional.expr.data.e_boolean.value) continue;
                                const key = meta_prop.key.?.asString(allocator) ?? continue;
                                dependency_sum += 1;
                                string_builder.count(key);
                                string_builder.count("*");
                            }
                        }
                    }
                }
            }
        }

        extern_string_count += dependency_sum;

        let dist_tags_count: usize = 0;
        if (dist := json.asProperty("dist-tags")) {
            if (dist.expr.data == .e_object) {
                const tags = dist.expr.data.e_object.properties.slice();
                for (tag of tags) {
                    if (key := tag.key.?.asString(allocator)) {
                        string_builder.count(key);
                        extern_string_count += 2;

                        string_builder.count((tag.value.?.asString(allocator) ?? ""));
                        dist_tags_count += 1;
                    }
                }
            }
        }

        if (last_modified.len > 0) {
            string_builder.count(last_modified);
        }

        if (etag.len > 0) {
            string_builder.count(etag);
        }

        let versioned_packages = allocator.alloc(PackageVersion, release_versions_len + pre_versions_len)?;
        const all_semver_versions = allocator.alloc(Semver.Version, release_versions_len + pre_versions_len + dist_tags_count)?;
        let all_extern_strings = allocator.alloc(ExternalString, extern_string_count + tarball_urls_count)?;
        let version_extern_strings = allocator.alloc(ExternalString, dependency_sum)?;
        let extern_strings_bin_entries = allocator.alloc(ExternalString, extern_string_count_bin)?;
        let all_extern_strings_bin_entries = extern_strings_bin_entries;
        let all_tarball_url_strings = allocator.alloc(ExternalString, tarball_urls_count)?;
        let tarball_url_strings = all_tarball_url_strings;
        let bundled_deps_buf = allocator.alloc(PackageNameHash, bundled_deps_count)?;
        let bundled_deps_offset: usize = 0;

        if (versioned_packages.len > 0) {
            const versioned_packages_bytes = std.mem.sliceAsBytes(versioned_packages);
            @memset(versioned_packages_bytes, 0);
        }
        if (all_semver_versions.len > 0) {
            const all_semver_versions_bytes = std.mem.sliceAsBytes(all_semver_versions);
            @memset(all_semver_versions_bytes, 0);
        }
        if (all_extern_strings.len > 0) {
            const all_extern_strings_bytes = std.mem.sliceAsBytes(all_extern_strings);
            @memset(all_extern_strings_bytes, 0);
        }
        if (version_extern_strings.len > 0) {
            const version_extern_strings_bytes = std.mem.sliceAsBytes(version_extern_strings);
            @memset(version_extern_strings_bytes, 0);
        }

        let versioned_package_releases = versioned_packages[0..release_versions_len];
        const all_versioned_package_releases = versioned_package_releases;
        let versioned_package_prereleases = versioned_packages[release_versions_len..][0..pre_versions_len];
        const all_versioned_package_prereleases = versioned_package_prereleases;
        let _versions_open = all_semver_versions;
        const all_release_versions = _versions_open[0..release_versions_len];
        _versions_open = _versions_open[release_versions_len..];
        const all_prerelease_versions = _versions_open[0..pre_versions_len];
        _versions_open = _versions_open[pre_versions_len..];
        let dist_tag_versions = _versions_open[0..dist_tags_count];
        let release_versions = all_release_versions;
        let prerelease_versions = all_prerelease_versions;

        let extern_strings = all_extern_strings;
        string_builder.cap += (string_builder.cap % 64) + 64;
        string_builder.cap *= 2;

        string_builder.allocate(allocator)?;

        let string_buf: []const u8 = "";
        if (ptr := string_builder.ptr) {
            // 0 it out for better determinism
            @memset(ptr[0..string_builder.cap], 0);

            string_buf = ptr[0..string_builder.cap];
        }

        // Using `expected_name` instead of the name from the manifest. Custom registries might
        // have a different name than the dependency name in package.json.
        result.pkg.name = string_builder.append(ExternalString, expected_name);

        get_versions: {
            if (versions_q := json.asProperty("versions")) {
                if (versions_q.expr.data != .e_object) break :get_versions;

                const versions = versions_q.expr.data.e_object.properties.slice();

                const all_dependency_names_and_values = all_extern_strings[0..dependency_sum];

                // versions change more often than names
                // so names go last because we are better able to dedupe at the end
                let dependency_values = version_extern_strings;
                let dependency_names = all_dependency_names_and_values;
                let prev_extern_bin_group: ([]ExternalString)? = null;
                const empty_version = bun.serializable(PackageVersion{
                    bin: Bin.init(),
                });

                for (prop of versions) {
                    const version_name = prop.key.?.asString(allocator) ?? continue;
                    let sliced_version = SlicedString.init(version_name, version_name);
                    let parsed_version = Semver.Version.parse(sliced_version);

                    if (Environment.allow_assert) bun.assertWithLocation(parsed_version.valid, @src());
                    // We only need to copy the version tags if it contains pre and/or build
                    if (parsed_version.version.tag.hasBuild() || parsed_version.version.tag.hasPre()) {
                        const version_string = string_builder.append(String, version_name);
                        sliced_version = version_string.sliced(string_buf);
                        parsed_version = Semver.Version.parse(sliced_version);
                        if (Environment.allow_assert) {
                            bun.assertWithLocation(parsed_version.valid, @src());
                            bun.assertWithLocation(parsed_version.version.tag.hasBuild() || parsed_version.version.tag.hasPre(), @src());
                        }
                    }
                    if (!parsed_version.valid) continue;

                    bundled_deps_set.map.clearRetainingCapacity();
                    bundle_all_deps = false;
                    if (bundled_deps_expr := prop.value.?.get("bundleDependencies") ?? prop.value.?.get("bundledDependencies")) {
                        switch (bundled_deps_expr.data) {
                            .e_boolean => |boolean| {
                                bundle_all_deps = boolean.value;
                            },
                            .e_array => |arr| {
                                for (bundled_dep of arr.slice()) {
                                    bundled_deps_set.insert(bundled_dep.asString(allocator) ?? continue)?;
                                }
                            },
                            else => {},
                        }
                    }

                    let package_version: PackageVersion = empty_version;

                    if (cpu_q := prop.value.?.asProperty("cpu")) {
                        package_version.cpu = Negatable(Architecture).fromJson(allocator, cpu_q.expr)?;
                    }

                    if (os_q := prop.value.?.asProperty("os")) {
                        package_version.os = Negatable(OperatingSystem).fromJson(allocator, os_q.expr)?;
                    }

                    if (libc := prop.value.?.asProperty("libc")) {
                        package_version.libc = Negatable(Libc).fromJson(allocator, libc.expr)?;
                    }

                    if (has_install_script := prop.value.?.asProperty("hasInstallScript")) {
                        switch (has_install_script.expr.data) {
                            .e_boolean => |val| {
                                package_version.has_install_script = val.value;
                            },
                            else => {},
                        }
                    }

                    bin: {
                        // bins are extremely repetitive
                        // We try to avoid storing copies the string
                        if (bin := prop.value.?.asProperty("bin")) {
                            switch (bin.expr.data) {
                                .e_object => |obj| {
                                    switch (obj.properties.len) {
                                        0 => {},
                                        1 => {
                                            const bin_name = obj.properties.ptr[0].key.?.asString(allocator) ?? break :bin;
                                            const value = obj.properties.ptr[0].value.?.asString(allocator) ?? break :bin;

                                            package_version.bin = {
                                                tag: .named_file,
                                                value: {
                                                    named_file: .{
                                                        string_builder.append(String, bin_name),
                                                        string_builder.append(String, value),
                                                    },
                                                },
                                            };
                                        },
                                        else => {
                                            let group_slice = extern_strings_bin_entries[0 .. obj.properties.len * 2];

                                            let is_identical = false;
                                            if (bin_group := prev_extern_bin_group) {
                                                is_identical = bin_group.len == group_slice.len;
                                            }
                                            let group_i: u32 = 0;

                                            for (bin_prop of obj.properties.slice()) {
                                                group_slice[group_i] = string_builder.append(ExternalString, bin_prop.key.?.asString(allocator) ?? break :bin);
                                                if (is_identical) {
                                                    is_identical = group_slice[group_i].hash == prev_extern_bin_group.?[group_i].hash;
                                                    if (comptime Environment.allow_assert) {
                                                        if (is_identical) {
                                                            const first = group_slice[group_i].slice(string_builder.allocatedSlice());
                                                            const second = prev_extern_bin_group.?[group_i].slice(string_builder.allocatedSlice());
                                                            if (!strings.eqlLong(first, second, true)) {
                                                                Output.panic("Bin group is not identical: {s} != {s}", .{ first, second });
                                                            }
                                                        }
                                                    }
                                                }
                                                group_i += 1;

                                                group_slice[group_i] = string_builder.append(ExternalString, bin_prop.value.?.asString(allocator) ?? break :bin);
                                                if (is_identical) {
                                                    is_identical = group_slice[group_i].hash == prev_extern_bin_group.?[group_i].hash;
                                                    if (comptime Environment.allow_assert) {
                                                        if (is_identical) {
                                                            const first = group_slice[group_i].slice(string_builder.allocatedSlice());
                                                            const second = prev_extern_bin_group.?[group_i].slice(string_builder.allocatedSlice());
                                                            if (!strings.eqlLong(first, second, true)) {
                                                                Output.panic("Bin group is not identical: {s} != {s}", .{ first, second });
                                                            }
                                                        }
                                                    }
                                                }
                                                group_i += 1;
                                            }

                                            if (is_identical) {
                                                group_slice = prev_extern_bin_group.?;
                                            } else {
                                                prev_extern_bin_group = group_slice;
                                                extern_strings_bin_entries = extern_strings_bin_entries[group_slice.len..];
                                            }

                                            package_version.bin = {
                                                tag: .map,
                                                value: { map: ExternalStringList.init(all_extern_strings_bin_entries, group_slice) },
                                            };
                                        },
                                    }

                                    break :bin;
                                },
                                .e_string => |stri| {
                                    if (stri.data.len > 0) {
                                        package_version.bin = {
                                            tag: .file,
                                            value: {
                                                file: string_builder.append(String, stri.data),
                                            },
                                        };
                                        break :bin;
                                    }
                                },
                                else => {},
                            }
                        }

                        if (dirs := prop.value.?.asProperty("directories")) {
                            // https://docs.npmjs.com/cli/v8/configuring-npm/package-json#directoriesbin
                            // Because of the way the bin directive works,
                            // specifying both a bin path and setting
                            // directories.bin is an error. If you want to
                            // specify individual files, use bin, and for all
                            // the files in an existing bin directory, use
                            // directories.bin.
                            if (bin_prop := dirs.expr.asProperty("bin")) {
                                if (str_ := bin_prop.expr.asString(allocator)) {
                                    if (str_.len > 0) {
                                        package_version.bin = {
                                            tag: .dir,
                                            value: {
                                                dir: string_builder.append(String, str_),
                                            },
                                        };
                                        break :bin;
                                    }
                                }
                            }
                        }
                    }

                    integrity: {
                        if (dist := prop.value.?.asProperty("dist")) {
                            if (dist.expr.data == .e_object) {
                                if (tarball_q := dist.expr.asProperty("tarball")) {
                                    if (tarball_q.expr.data == .e_string && tarball_q.expr.data.e_string.len() > 0) {
                                        package_version.tarball_url = string_builder.append(ExternalString, tarball_q.expr.data.e_string.slice(allocator));
                                        tarball_url_strings[0] = package_version.tarball_url;
                                        tarball_url_strings = tarball_url_strings[1..];
                                    }
                                }

                                if (file_count_ := dist.expr.asProperty("fileCount")) {
                                    if (file_count_.expr.data == .e_number) {
                                        package_version.file_count = file_count_.expr.data.e_number.toU32();
                                    }
                                }

                                if (file_count_ := dist.expr.asProperty("unpackedSize")) {
                                    if (file_count_.expr.data == .e_number) {
                                        package_version.unpacked_size = file_count_.expr.data.e_number.toU32();
                                    }
                                }

                                if (shasum := dist.expr.asProperty("integrity")) {
                                    if (shasum_str := shasum.expr.asString(allocator)) {
                                        package_version.integrity = Integrity.parse(shasum_str);
                                        if (package_version.integrity.tag.isSupported()) break :integrity;
                                    }
                                }

                                if (shasum := dist.expr.asProperty("shasum")) {
                                    if (shasum_str := shasum.expr.asString(allocator)) {
                                        package_version.integrity = Integrity.parseSHASum(shasum_str) catch Integrity{};
                                    }
                                }
                            }
                        }
                    }

                    let non_optional_peer_dependency_offset: usize = 0;

                    inline for (dependency_groups) |pair| {
                        // For peer deps, fall through with an empty `items`
                        // slice when `peerDependencies` is absent so that
                        // `peerDependenciesMeta`-only entries (synthesised
                        // below) still get a build pass. The fallthrough must
                        // stay scoped to packages that actually have a
                        // `peerDependenciesMeta`: the body sets
                        // `package_version.bundled_dependencies` from this
                        // iteration's slice of `bundled_deps_buf`, so an
                        // unconditional empty pass would clobber the value the
                        // `dependencies` iteration just produced.
                        const items = items: {
                            if (versioned_deps := prop.value.?.asProperty(comptime pair.prop)) {
                                if (versioned_deps.expr.data == .e_object) {
                                    break :items versioned_deps.expr.data.e_object.properties.slice();
                                }
                            }
                            break :items [];
                        };
                        const is_peer_group = comptime strings.eqlComptime(pair.prop, "peerDependencies");
                        let has_meta_only_peers = false;
                        if (is_peer_group) {
                            if (meta_prop := prop.value.?.asProperty("peerDependenciesMeta")) {
                                has_meta_only_peers = meta_prop.expr.data == .e_object && meta_prop.expr.data.e_object.properties.len > 0;
                            }
                        }
                        if (items.len > 0 || has_meta_only_peers) {
                            {
                                let count = items.len;

                                let this_names = dependency_names[0..count];
                                let this_versions = dependency_values[0..count];

                                let name_hasher = bun.Wyhash11.init(0);
                                let version_hasher = bun.Wyhash11.init(0);

                                const is_peer = comptime strings.eqlComptime(pair.prop, "peerDependencies");

                                if (comptime is_peer) {
                                    optional_peer_dep_names.clearRetainingCapacity();

                                    if (@"meta" := prop.value.?.asProperty("peerDependenciesMeta")) {
                                        if (@"meta".expr.data == .e_object) {
                                            const meta_props = @"meta".expr.data.e_object.properties.slice();
                                            optional_peer_dep_names.ensureUnusedCapacity(meta_props.len)?;
                                            for (meta_prop of meta_props) {
                                                if (optional := meta_prop.value.?.asProperty("optional")) {
                                                    if (optional.expr.data != .e_boolean || !optional.expr.data.e_boolean.value) {
                                                        continue;
                                                    }

                                                    const meta_key = meta_prop.key.?.asString(allocator) ?? unreachable;
                                                    optional_peer_dep_names.appendAssumeCapacity(String.Builder.stringHash(meta_key));

                                                    // Reserve a slot for a meta-only synthesised peer.
                                                    // The slot is unused if `meta_key` also appears in
                                                    // `peerDependencies` below.
                                                    count += 1;
                                                }
                                            }
                                            // Re-slice now that the count grew.
                                            this_names = dependency_names[0..count];
                                            this_versions = dependency_values[0..count];
                                        }
                                    }
                                }

                                const bundled_deps_begin = bundled_deps_offset;

                                let i: usize = 0;

                                for (item of items) {
                                    const name_str = item.key.?.asString(allocator) ?? if (comptime Environment.allow_assert) unreachable else continue;
                                    const version_str = item.value.?.asString(allocator) ?? if (comptime Environment.allow_assert) unreachable else continue;

                                    this_names[i] = string_builder.append(ExternalString, name_str);
                                    this_versions[i] = string_builder.append(ExternalString, version_str);

                                    if (!bundle_all_deps && bundled_deps_set.swapRemove(name_str)) {
                                        bundled_deps_buf[bundled_deps_offset] = this_names[i].hash;
                                        bundled_deps_offset += 1;
                                    }

                                    if (comptime is_peer) {
                                        if (std.mem.indexOfScalar(u64, optional_peer_dep_names.items, this_names[i].hash) != null) {
                                            // For optional peer dependencies, we store a length instead of a whole separate array
                                            // To make that work, we have to move optional peer dependencies to the front of the array
                                            //
                                            if (non_optional_peer_dependency_offset != i) {
                                                const current_name = this_names[i];
                                                this_names[i] = this_names[non_optional_peer_dependency_offset];
                                                this_names[non_optional_peer_dependency_offset] = current_name;

                                                const current_version = this_versions[i];
                                                this_versions[i] = this_versions[non_optional_peer_dependency_offset];
                                                this_versions[non_optional_peer_dependency_offset] = current_version;
                                            }

                                            non_optional_peer_dependency_offset += 1;
                                        }

                                        if (optional_peer_dep_names.items.len == 0) {
                                            const names_hash_bytes = @as([8]u8, @bitCast(this_names[i].hash));
                                            name_hasher.update(&names_hash_bytes);
                                            const versions_hash_bytes = @as([8]u8, @bitCast(this_versions[i].hash));
                                            version_hasher.update(&versions_hash_bytes);
                                        }
                                    } else {
                                        const names_hash_bytes = @as([8]u8, @bitCast(this_names[i].hash));
                                        name_hasher.update(&names_hash_bytes);
                                        const versions_hash_bytes = @as([8]u8, @bitCast(this_versions[i].hash));
                                        version_hasher.update(&versions_hash_bytes);
                                    }

                                    i += 1;
                                }

                                if (comptime is_peer) {
                                    // Append meta-only optional peers (declared
                                    // in `peerDependenciesMeta` but not in
                                    // `peerDependencies`) as `"*"` versions.
                                    // pnpm/yarn do this; webpack relies on it
                                    // to make `webpack-cli` reachable.
                                    if (@"meta" := prop.value.?.asProperty("peerDependenciesMeta")) {
                                        if (@"meta".expr.data == .e_object) {
                                            outer: for (@"meta".expr.data.e_object.properties.slice()) |meta_prop| {
                                                const optional = meta_prop.value.?.asProperty("optional") ?? continue;
                                                if (optional.expr.data != .e_boolean || !optional.expr.data.e_boolean.value) continue;
                                                const meta_key = meta_prop.key.?.asString(allocator) ?? continue;
                                                const meta_hash = String.Builder.stringHash(meta_key);
                                                for (existing of this_names[0..i]) {
                                                    if (existing.hash == meta_hash) continue :outer;
                                                }
                                                this_names[i] = string_builder.append(ExternalString, meta_key);
                                                this_versions[i] = string_builder.append(ExternalString, "*");
                                                // Swap to the optional-peer
                                                // prefix the rest of the loop
                                                // body would have produced.
                                                if (non_optional_peer_dependency_offset != i) {
                                                    std.mem.swap(ExternalString, &this_names[i], &this_names[non_optional_peer_dependency_offset]);
                                                    std.mem.swap(ExternalString, &this_versions[i], &this_versions[non_optional_peer_dependency_offset]);
                                                }
                                                non_optional_peer_dependency_offset += 1;
                                                i += 1;
                                            }
                                        }
                                    }
                                }

                                count = i;
                                // The peer slice was over-reserved by the
                                // number of `peerDependenciesMeta` entries (so
                                // meta-only synthesised peers had room); trim
                                // to what was actually written before the
                                // ExternalStringList offsets are computed.
                                this_names = this_names[0..count];
                                this_versions = this_versions[0..count];

                                // Bundled deps are matched against the
                                // `dependencies`/`optionalDependencies` groups
                                // only; the peer pass never adds to
                                // `bundled_deps_buf`. With the meta-only
                                // synthesis above the peer body now runs even
                                // when `peerDependencies` is absent, so writing
                                // here would clobber the value the dependencies
                                // pass already produced with an empty slice.
                                if (comptime !is_peer) {
                                    if (bundle_all_deps) {
                                        package_version.bundled_dependencies = ExternalPackageNameHashList.invalid;
                                    } else {
                                        package_version.bundled_dependencies = ExternalPackageNameHashList.init(
                                            bundled_deps_buf,
                                            bundled_deps_buf[bundled_deps_begin..bundled_deps_offset],
                                        );
                                    }
                                }

                                let name_list = ExternalStringList.init(all_extern_strings, this_names);
                                let version_list = ExternalStringList.init(version_extern_strings, this_versions);

                                if (comptime is_peer) {
                                    package_version.non_optional_peer_dependencies_start = @as(u32, @truncate(non_optional_peer_dependency_offset));
                                }

                                if (count > 0 &&
                                    ((comptime !is_peer) ||
                                        optional_peer_dep_names.items.len == 0))
                                {
                                    const name_map_hash = name_hasher.final();
                                    const version_map_hash = version_hasher.final();

                                    const name_entry = all_extern_strings_dedupe_map.getOrPut(name_map_hash)?;
                                    if (name_entry.found_existing) {
                                        name_list = name_entry.value_ptr.*;
                                        this_names = name_list.mut(all_extern_strings);
                                    } else {
                                        name_entry.value_ptr.* = name_list;
                                        dependency_names = dependency_names[count..];
                                    }

                                    const version_entry = version_extern_strings_dedupe_map.getOrPut(version_map_hash)?;
                                    if (version_entry.found_existing) {
                                        version_list = version_entry.value_ptr.*;
                                        this_versions = version_list.mut(version_extern_strings);
                                    } else {
                                        version_entry.value_ptr.* = version_list;
                                        dependency_values = dependency_values[count..];
                                    }
                                }

                                if (comptime is_peer) {
                                    if (optional_peer_dep_names.items.len > 0) {
                                        dependency_names = dependency_names[count..];
                                        dependency_values = dependency_values[count..];
                                    }
                                }

                                @field(package_version, pair.field) = ExternalStringMap{
                                    name: name_list,
                                    value: version_list,
                                };

                                if (comptime Environment.allow_assert) {
                                    const dependencies_list = @field(package_version, pair.field);

                                    bun.assertWithLocation(dependencies_list.name.off < all_extern_strings.len, @src());
                                    bun.assertWithLocation(dependencies_list.value.off < all_extern_strings.len, @src());
                                    bun.assertWithLocation(dependencies_list.name.off + dependencies_list.name.len < all_extern_strings.len, @src());
                                    bun.assertWithLocation(dependencies_list.value.off + dependencies_list.value.len < all_extern_strings.len, @src());

                                    bun.assertWithLocation(std.@"meta".eql(dependencies_list.name.get(all_extern_strings), this_names), @src());
                                    bun.assertWithLocation(std.@"meta".eql(dependencies_list.value.get(version_extern_strings), this_versions), @src());
                                    let j: usize = 0;
                                    const name_dependencies = dependencies_list.name.get(all_extern_strings);

                                    if (comptime is_peer) {
                                        if (optional_peer_dep_names.items.len == 0) {
                                            while (j < name_dependencies.len) : (j += 1) {
                                                const dep_name = name_dependencies[j];
                                                bun.assertWithLocation(std.mem.eql(u8, dep_name.slice(string_buf), this_names[j].slice(string_buf)), @src());
                                                bun.assertWithLocation(std.mem.eql(u8, dep_name.slice(string_buf), items[j].key.?.asString(allocator).?), @src());
                                            }

                                            j = 0;
                                            while (j < dependencies_list.value.len) : (j += 1) {
                                                const dep_name = dependencies_list.value.get(version_extern_strings)[j];

                                                bun.assertWithLocation(std.mem.eql(u8, dep_name.slice(string_buf), this_versions[j].slice(string_buf)), @src());
                                                bun.assertWithLocation(std.mem.eql(u8, dep_name.slice(string_buf), items[j].value.?.asString(allocator).?), @src());
                                            }
                                        }
                                    } else {
                                        while (j < name_dependencies.len) : (j += 1) {
                                            const dep_name = name_dependencies[j];
                                            bun.assertWithLocation(std.mem.eql(u8, dep_name.slice(string_buf), this_names[j].slice(string_buf)), @src());
                                            bun.assertWithLocation(std.mem.eql(u8, dep_name.slice(string_buf), items[j].key.?.asString(allocator).?), @src());
                                        }

                                        j = 0;
                                        while (j < dependencies_list.value.len) : (j += 1) {
                                            const dep_name = dependencies_list.value.get(version_extern_strings)[j];

                                            bun.assertWithLocation(std.mem.eql(u8, dep_name.slice(string_buf), this_versions[j].slice(string_buf)), @src());
                                            bun.assertWithLocation(std.mem.eql(u8, dep_name.slice(string_buf), items[j].value.?.asString(allocator).?), @src());
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (time_obj := json.asProperty("time")) {
                        if (publish_time_expr := time_obj.expr.get(version_name)) {
                            if (publish_time_str := publish_time_expr.asString(allocator)) {
                                if (time := bun.jsc.wtf.parseES5Date(publish_time_str) catch null) {
                                    package_version.publish_timestamp_ms = time;
                                }
                            }
                        }
                    }

                    if (!parsed_version.version.tag.hasPre()) {
                        release_versions[0] = parsed_version.version.min();
                        versioned_package_releases[0] = package_version;
                        release_versions = release_versions[1..];
                        versioned_package_releases = versioned_package_releases[1..];
                    } else {
                        prerelease_versions[0] = parsed_version.version.min();
                        versioned_package_prereleases[0] = package_version;
                        prerelease_versions = prerelease_versions[1..];
                        versioned_package_prereleases = versioned_package_prereleases[1..];
                    }
                }

                extern_strings = all_extern_strings[all_dependency_names_and_values.len - dependency_names.len ..];
                version_extern_strings = version_extern_strings[0 .. version_extern_strings.len - dependency_values.len];
            }
        }

        if (dist := json.asProperty("dist-tags")) {
            if (dist.expr.data == .e_object) {
                const tags = dist.expr.data.e_object.properties.slice();
                let extern_strings_slice = extern_strings[0..dist_tags_count];
                let dist_tag_i: usize = 0;

                for (tag of tags) {
                    if (key := tag.key.?.asString(allocator)) {
                        extern_strings_slice[dist_tag_i] = string_builder.append(ExternalString, key);

                        const version_name = tag.value.?.asString(allocator) ?? continue;

                        const dist_tag_value_literal = string_builder.append(ExternalString, version_name);

                        const sliced_string = dist_tag_value_literal.value.sliced(string_buf);

                        dist_tag_versions[dist_tag_i] = Semver.Version.parse(sliced_string).version.min();
                        dist_tag_i += 1;
                    }
                }

                result.pkg.dist_tags = DistTagMap{
                    tags: ExternalStringList.init(all_extern_strings, extern_strings_slice[0..dist_tag_i]),
                    versions: VersionSlice.init(all_semver_versions, dist_tag_versions[0..dist_tag_i]),
                };

                if (comptime Environment.allow_assert) {
                    bun.assertWithLocation(std.@"meta".eql(result.pkg.dist_tags.versions.get(all_semver_versions), dist_tag_versions[0..dist_tag_i]), @src());
                    bun.assertWithLocation(std.@"meta".eql(result.pkg.dist_tags.tags.get(all_extern_strings), extern_strings_slice[0..dist_tag_i]), @src());
                }

                extern_strings = extern_strings[dist_tag_i..];
            }
        }

        if (last_modified.len > 0) {
            result.pkg.last_modified = string_builder.append(String, last_modified);
        }

        if (etag.len > 0) {
            result.pkg.etag = string_builder.append(String, etag);
        }

        if (name_q := json.asProperty("modified")) {
            const field = name_q.expr.asString(allocator) ?? return null;

            result.pkg.modified = string_builder.append(String, field);
        }

        result.pkg.releases.keys = VersionSlice.init(all_semver_versions, all_release_versions);
        result.pkg.releases.values = PackageVersionList.init(versioned_packages, all_versioned_package_releases);

        result.pkg.prereleases.keys = VersionSlice.init(all_semver_versions, all_prerelease_versions);
        result.pkg.prereleases.values = PackageVersionList.init(versioned_packages, all_versioned_package_prereleases);

        const max_versions_count = @max(all_release_versions.len, all_prerelease_versions.len);

        // Sort the list of packages in a deterministic order
        // Usually, npm will do this for us.
        // But, not always.
        // See https://github.com/oven-sh/bun/pull/6611
        //
        // The tricky part about this code is we need to sort two different arrays.
        // To do that, we create a 3rd array, containing indices into the other 2 arrays.
        // Creating a 3rd array is expensive! But mostly expensive if the size of the integers is large
        // Most packages don't have > 65,000 versions
        // So instead of having a hardcoded limit of how many packages we can sort, we ask
        //    > "How many bytes do we need to store the indices?"
        // We decide what size of integer to use based on that.
        const how_many_bytes_to_store_indices = switch (max_versions_count) {
            // log2(0) == Infinity
            0 => 0,
            // log2(1) == 0
            1 => 1,

            else => std.math.divCeil(usize, std.math.log2_int_ceil(usize, max_versions_count), 8) catch 0,
        };

        switch (how_many_bytes_to_store_indices) {
            inline 1...8 => |int_bytes| {
                const Int = std.@"meta".Int(.unsigned, int_bytes * 8);

                const ExternVersionSorter = struct {
                    string_bytes: []const u8,
                    all_versions: []const Semver.Version,
                    all_versioned_packages: []const PackageVersion,

                    static fn isLessThan(this: @This(), left: Int, right: Int): bool {
                        return this.all_versions[left].order(this.all_versions[right], this.string_bytes, this.string_bytes) == .lt;
                    }
                };

                let all_indices = bun.default_allocator.alloc(Int, max_versions_count)?;
                defer bun.default_allocator.free(all_indices);
                const releases_list = [ &result.pkg.releases, &result.pkg.prereleases ];

                let all_cloned_versions = bun.default_allocator.alloc(Semver.Version, max_versions_count)?;
                defer bun.default_allocator.free(all_cloned_versions);

                let all_cloned_packages = bun.default_allocator.alloc(PackageVersion, max_versions_count)?;
                defer bun.default_allocator.free(all_cloned_packages);

                inline for (release_i of 0..2) {
                    let release = releases_list[release_i];
                    let indices = all_indices[0..release.keys.len];
                    const cloned_packages = all_cloned_packages[0..release.keys.len];
                    const cloned_versions = all_cloned_versions[0..release.keys.len];
                    let versioned_packages_ = @constCast(release.values.get(versioned_packages));
                    let semver_versions_ = @constCast(release.keys.get(all_semver_versions));
                    @memcpy(cloned_packages, versioned_packages_);
                    @memcpy(cloned_versions, semver_versions_);

                    let idx: usize = 0;
                    while (idx < indices.len) : (idx += 1) {
                        indices[idx] = @truncate(idx);
                    }

                    const sorter = ExternVersionSorter{
                        string_bytes: string_buf,
                        all_versions: semver_versions_,
                        all_versioned_packages: versioned_packages_,
                    };
                    std.sort.pdq(Int, indices, sorter, ExternVersionSorter.isLessThan);

                    idx = 0;
                    while (idx < indices.len) : (idx += 1) {
                        const i = indices[idx];
                        versioned_packages_[idx] = cloned_packages[i];
                        semver_versions_[idx] = cloned_versions[i];
                    }

                    if (comptime Environment.allow_assert) {
                        if (cloned_versions.len > 1) {
                            // Sanity check:
                            // When reading the versions, we iterate through the
                            // list backwards to choose the highest matching
                            // version
                            const first = semver_versions_[0];
                            const second = semver_versions_[1];
                            const order = second.order(first, string_buf, string_buf);
                            bun.assertWithLocation(order == .gt, @src());
                        }
                    }
                }
            },
            else => {
                bun.assertWithLocation(max_versions_count == 0, @src());
            },
        }

        if (extern_strings.len + tarball_urls_count > 0) {
            const src = std.mem.sliceAsBytes(all_tarball_url_strings[0 .. all_tarball_url_strings.len - tarball_url_strings.len]);
            if (src.len > 0) {
                let dst = std.mem.sliceAsBytes(all_extern_strings[all_extern_strings.len - extern_strings.len ..]);
                bun.assertWithLocation(dst.len >= src.len, @src());
                @memcpy(dst[0..src.len], src);
            }

            all_extern_strings = all_extern_strings[0 .. all_extern_strings.len - extern_strings.len];
        }

        result.pkg.string_lists_buf.off = 0;
        result.pkg.string_lists_buf.len = @as(u32, @truncate(all_extern_strings.len));

        result.pkg.versions_buf.off = 0;
        result.pkg.versions_buf.len = @as(u32, @truncate(all_semver_versions.len));

        result.versions = all_semver_versions;
        result.external_strings = all_extern_strings;
        result.external_strings_for_versions = version_extern_strings;
        result.package_versions = versioned_packages;
        result.extern_strings_bin_entries = all_extern_strings_bin_entries[0 .. all_extern_strings_bin_entries.len - extern_strings_bin_entries.len];
        result.bundled_deps_buf = bundled_deps_buf;
        result.pkg.public_max_age = public_max_age;
        result.pkg.has_extended_manifest = is_extended_manifest;

        if (ptr := string_builder.ptr) {
            result.string_buf = ptr[0..string_builder.len];
        }

        return result;
    }
}

type string = []const u8;

const Bin = bin_mod.Bin;
const IdentityContext = identity_context_mod.IdentityContext;
const Integrity = integrity_mod.Integrity;
const ObjectPool = pool_mod.ObjectPool;
const URL = url_mod.URL;

const Aligner = install_mod.Aligner;
const ExternalSlice = install_mod.ExternalSlice;
const ExternalStringList = install_mod.ExternalStringList;
const ExternalStringMap = install_mod.ExternalStringMap;
const PackageManager = install_mod.PackageManager;
const VersionSlice = install_mod.VersionSlice;
const initializeStore = install_mod.initializeMiniStore;

const Environment = bun.Environment;
const Global = bun.Global;
const HTTPClient = bun.http;
const BunJSON = bun.json;
const MutableString = bun.MutableString;
const OOM = bun.OOM;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const http = bun.http;
const logger = bun.logger;
const strings = bun.strings;
const File = bun.sys.File;
const api = bun.schema.api;

const Semver = bun.Semver;
const ExternalString = Semver.ExternalString;
const SlicedString = Semver.SlicedString;
const String = Semver.String;

const ExternalPackageNameHashList = bun.install.ExternalPackageNameHashList;
const PackageNameHash = bun.install.PackageNameHash;