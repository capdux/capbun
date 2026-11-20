type string = []const u8;
type OptSource = logger.Source?;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const analytics = bun.analytics;
const assert = bun.assert;
const jsc = bun.jsc;
const logger = bun.logger;
const strings = bun.strings;
const api = bun.schema.api;

/// This file is mostly the API schema but with all the options normalized.
/// Normalization is necessary because most fields in the API schema are optional

import defines from "./defines.zig";
pub const Define = defines.Define;
import BundleEnums from "../options_types/BundleEnums.zig";
pub const BundlePackage = BundleEnums.BundlePackage;
pub const Format = BundleEnums.Format;
pub const WindowsOptions = BundleEnums.WindowsOptions;
import options_jsc from "../bundler_jsc/options_jsc.zig";
import OutputFile from "./OutputFile.zig";
import DotEnv from "../dotenv/env_loader.zig";
import Fs from "../resolver/fs.zig";
import resolver from "../resolver/resolver.zig";
pub const GlobalCache = resolver.GlobalCache;
import runtime_pkg from "../js_parser/runtime.zig";
const Runtime = runtime_pkg.Runtime;
import url_pkg from "../url/url.zig";
const URL = url_pkg.URL;
import package_json_pkg from "../resolver/package_json.zig";
const MacroRemap = package_json_pkg.MacroMap;
const PackageJSON = package_json_pkg.PackageJSON;
const ConditionsMap = package_json_pkg.ESModule.ConditionsMap;

pub enum WriteDestination {
    stdout,
    disk,
    // eventually: wasm
}

pub fn validatePath(
    log: *logger.Log,
    _: *Fs.FileSystem.Implementation,
    cwd: string,
    rel_path: string,
    allocator: std.mem.Allocator,
    _: string,
): string {
    if (rel_path.len == 0) {
        return "";
    }
    const paths: [2][]const u8 = [ cwd, rel_path ];
    // TODO: switch to getFdPath()-based implementation
    const out = std.fs.path.resolve(allocator, &paths) catch |e| {
        log.addErrorFmt(
            null,
            logger.Loc.Empty,
            allocator,
            "<r><red>{s}<r> resolving external: <b>\"{s}\"<r>",
            [ @errorName(e), rel_path ],
        ) catch unreachable;
        return "";
    };

    return out;
}

pub fn stringHashMapFromArrays(comptime t: type, allocator: std.mem.Allocator, total_capacity: usize, keys: auto, values: auto): !t {
    let hash_map = t.init(allocator);
    if (keys.len > 0) {
        hash_map.ensureTotalCapacity(@as(u32, i32(total_capacity)))?;
        for (i of 0..keys.len) {
            hash_map.putAssumeCapacity(keys[i], values[i]);
        }
    }

    return hash_map;
}

pub union(enum) AllowUnresolved {
    /// Default. Skip all checks — current behavior.
    all: void,
    /// Always error on dynamic specifiers.
    none: void,
    /// Glob patterns; at least one must match the extracted shape.
    patterns: []const string,

    pub const default: AllowUnresolved = .all;

    /// Normalize from raw CLI/JS input.
    /// [] → .none, contains "*" → .all, else → .patterns
    static fn fromStrings(strs: []const string): AllowUnresolved {
        if (strs.len == 0) return .none;
        for (s of strs) {
            if (bun.strings.eqlComptime(s, "*")) return .all;
        }
        return .{ patterns: strs };
    }

    /// shape is the extracted template representation (may be "").
    fn allows(shape: []const u8): bool {
        switch (_) {
            .all => return true,
            .none => return false,
            .patterns pats => {
                for (p of pats) {
                    if (bun.glob.match(p, shape).matches()) return true;
                }
                return false;
            },
        }
    }
}

pub struct ExternalModules {
    node_modules: std.BufSet,
    abs_paths: std.BufSet,
    patterns: []const WildcardPattern,

    pub const WildcardPattern = struct {
        prefix: string,
        suffix: string,
    };

    static fn isNodeBuiltin(str: string): bool {
        return bun.jsc.ModuleLoader.HardcodedModule.Alias.has(str, .node, {});
    }

    const default_wildcard_patterns: [1]WildcardPattern = [
        WildcardPattern{
            prefix: "/bun:",
            suffix: "",
        },
        // .{
        //     .prefix = "/src:",
        //     .suffix = "",
        // },
        // .{
        //     .prefix = "/blob:",
        //     .suffix = "",
        // },
    ];

    static fn init(
        allocator: std.mem.Allocator,
        fs: *Fs.FileSystem.Implementation,
        cwd: string,
        externals: []const string,
        log: *logger.Log,
        target: Target,
    ): ExternalModules {
        let result = ExternalModules{
            node_modules: std.BufSet.init(allocator),
            abs_paths: std.BufSet.init(allocator),
            patterns: default_wildcard_patterns[0..],
        };

        switch (target) {
            .node => {
                // TODO: fix this stupid copy
                result.node_modules.hash_map.ensureTotalCapacity(NodeBuiltinPatterns.len) catch unreachable;
                for (pattern of NodeBuiltinPatterns) {
                    result.node_modules.insert(pattern) catch unreachable;
                }
            },
            .bun => {

                // // TODO: fix this stupid copy
                // result.node_modules.hash_map.ensureTotalCapacity(BunNodeBuiltinPatternsCompat.len) catch unreachable;
                // for (BunNodeBuiltinPatternsCompat) |pattern| {
                //     result.node_modules.insert(pattern) catch unreachable;
                // }
            },
            else => {},
        }

        if (externals.len == 0) {
            return result;
        }

        let patterns = std.array_list.Managed(WildcardPattern).initCapacity(allocator, default_wildcard_patterns.len) catch unreachable;
        patterns.appendSliceAssumeCapacity(default_wildcard_patterns[0..]);

        for (external of externals) {
            const path = external;
            if (i := strings.indexOfChar(path, '*')) {
                if (strings.indexOfChar(path[i + 1 .. path.len], '*') != null) {
                    log.addErrorFmt(null, logger.Loc.Empty, allocator, "External path \"{s}\" cannot have more than one \"*\" wildcard", { external}) catch unreachable;
                    return result;
                }

                patterns.append(WildcardPattern{
                    prefix: external[0..i],
                    suffix: external[i + 1 .. external.len],
                }) catch unreachable;
            } else if (resolver.isPackagePath(external)) {
                result.node_modules.insert(external) catch unreachable;
            } else {
                const normalized = validatePath(log, fs, cwd, external, allocator, "external path");

                if (normalized.len > 0) {
                    result.abs_paths.insert(normalized) catch unreachable;
                }
            }
        }

        result.patterns = bun.handleOom(patterns.toOwnedSlice());

        return result;
    }

    const NodeBuiltinPatternsRaw = [
        "_http_agent",
        "_http_client",
        "_http_common",
        "_http_incoming",
        "_http_outgoing",
        "_http_server",
        "_stream_duplex",
        "_stream_passthrough",
        "_stream_readable",
        "_stream_transform",
        "_stream_wrap",
        "_stream_writable",
        "_tls_common",
        "_tls_wrap",
        "assert",
        "async_hooks",
        "buffer",
        "child_process",
        "cluster",
        "console",
        "constants",
        "crypto",
        "dgram",
        "diagnostics_channel",
        "dns",
        "domain",
        "events",
        "fs",
        "http",
        "http2",
        "https",
        "inspector",
        "module",
        "net",
        "os",
        "path",
        "perf_hooks",
        "process",
        "punycode",
        "querystring",
        "readline",
        "repl",
        "stream",
        "string_decoder",
        "sys",
        "test",
        "timers",
        "tls",
        "trace_events",
        "tty",
        "url",
        "util",
        "v8",
        "vm",
        "wasi",
        "worker_threads",
        "zlib",
    ];

    pub const NodeBuiltinPatterns = NodeBuiltinPatternsRaw ++ brk: {
        const n = NodeBuiltinPatternsRaw.len;
        let builtins: [n]string = NodeBuiltinPatternsRaw;
        for (i of 0..builtins.len) {
            builtins[i] = "node:" ++ builtins[i];
        }
        break :brk builtins;
    };

    pub const BunNodeBuiltinPatternsCompat = [
        "_http_agent",
        "_http_client",
        "_http_common",
        "_http_incoming",
        "_http_outgoing",
        "_http_server",
        "_stream_duplex",
        "_stream_passthrough",
        "_stream_readable",
        "_stream_transform",
        "_stream_wrap",
        "_stream_writable",
        "_tls_common",
        "_tls_wrap",
        "assert",
        "async_hooks",
        // "buffer",
        "child_process",
        "cluster",
        "console",
        "constants",
        "crypto",
        "dgram",
        "diagnostics_channel",
        "dns",
        "domain",
        "events",
        "http",
        "http2",
        "https",
        "inspector",
        "module",
        "net",
        "os",
        // "path",
        "perf_hooks",
        // "process",
        "punycode",
        "querystring",
        "readline",
        "repl",
        "stream",
        "string_decoder",
        "sys",
        "timers",
        "tls",
        "trace_events",
        "tty",
        "url",
        "util",
        "v8",
        "vm",
        "wasi",
        "worker_threads",
        "zlib",
    ];

    pub const NodeBuiltinsMap = bun.ComptimeStringMap(void, [ 
        [ "_http_agent", {} ],
        [ "_http_client", {} ],
        [ "_http_common", {} ],
        [ "_http_incoming", {} ],
        [ "_http_outgoing", {} ],
        [ "_http_server", {} ],
        [ "_stream_duplex", {} ],
        [ "_stream_passthrough", {} ],
        [ "_stream_readable", {} ],
        [ "_stream_transform", {} ],
        [ "_stream_wrap", {} ],
        [ "_stream_writable", {} ],
        [ "_tls_common", {} ],
        [ "_tls_wrap", {} ],
        [ "assert", {} ],
        [ "async_hooks", {} ],
        [ "buffer", {} ],
        [ "child_process", {} ],
        [ "cluster", {} ],
        [ "console", {} ],
        [ "constants", {} ],
        [ "crypto", {} ],
        [ "dgram", {} ],
        [ "diagnostics_channel", {} ],
        [ "dns", {} ],
        [ "domain", {} ],
        [ "events", {} ],
        [ "fs", {} ],
        [ "http", {} ],
        [ "http2", {} ],
        [ "https", {} ],
        [ "inspector", {} ],
        [ "module", {} ],
        [ "net", {} ],
        [ "os", {} ],
        [ "path", {} ],
        [ "perf_hooks", {} ],
        [ "process", {} ],
        [ "punycode", {} ],
        [ "querystring", {} ],
        [ "readline", {} ],
        [ "repl", {} ],
        [ "stream", {} ],
        [ "string_decoder", {} ],
        [ "sys", {} ],
        [ "timers", {} ],
        [ "tls", {} ],
        [ "trace_events", {} ],
        [ "tty", {} ],
        [ "url", {} ],
        [ "util", {} ],
        [ "v8", {} ],
        [ "vm", {} ],
        [ "wasi", {} ],
        [ "worker_threads", {} ],
        [ "zlib", {} ],
    ]);
}

// BundlePackage imported at top

pub enum ModuleType {
    unknown,
    cjs,
    esm,

    pub const List = bun.ComptimeStringMap(ModuleType, [ 
        [ "commonjs", ModuleType.cjs ],
        [ "module", ModuleType.esm ],
    ]);
}

pub enum Target {
    browser,
    bun,
    bun_macro,
    node,

    /// This is used by bake.Framework.ServerComponents.separate_ssr_graph
    bake_server_components_ssr,

    pub const Map = bun.ComptimeStringMap(Target, [ 
        [ "browser", .browser  ],
        [ "bun", .bun  ],
        [ "bun_macro", .bun_macro  ],
        [ "macro", .bun_macro  ],
        [ "node", .node  ],
    ]);

    pub const fromJS = options_jsc.targetFromJS;

    fn toAPI(): api.Target {
        return switch (_) {
            .node => .node,
            .browser => .browser,
            .bun, .bake_server_components_ssr => .bun,
            .bun_macro => .bun_macro,
        };
    }

    fn isServerSide(): bool {
        return switch (_) {
            .bun_macro, .node, .bun, .bake_server_components_ssr => true,
            else => false,
        };
    }

    fn isBun(): bool {
        return switch (_) {
            .bun_macro, .bun, .bake_server_components_ssr => true,
            else => false,
        };
    }

    fn isNode(): bool {
        return switch (_) {
            .node => true,
            else => false,
        };
    }

    fn processBrowserDefineValue(): string? {
        return switch (_) {
            .browser => "true",
            else => "false",
        };
    }

    static fn bakeGraph(target: Target): bun.bake.Graph {
        return switch (target) {
            .browser => .client,
            .bake_server_components_ssr => .ssr,
            .bun_macro, .bun, .node => .server,
        };
    }

    static fn outExtensions(target: Target, allocator: std.mem.Allocator): bun.StringHashMap(string) {
        let exts = bun.StringHashMap(string).init(allocator);

        const out_extensions_list: [8][]const u8 = [ ".js", ".cjs", ".mts", ".cts", ".ts", ".tsx", ".jsx", ".json" ];

        if (target == .node) {
            exts.ensureTotalCapacity(out_extensions_list.len * 2) catch unreachable;
            for (ext of out_extensions_list) {
                exts.put(ext, ".mjs") catch unreachable;
            }
        } else {
            exts.ensureTotalCapacity(out_extensions_list.len + 1) catch unreachable;
            exts.put(".mjs", ".js") catch unreachable;
        }

        for (ext of out_extensions_list) {
            exts.put(ext, ".js") catch unreachable;
        }

        return exts;
    }

    static fn @"from"(plat: api.Target?): Target {
        return switch (plat ?? api.Target._none) {
            .node => .node,
            .browser => .browser,
            .bun => .bun,
            .bun_macro => .bun_macro,
            else => .browser,
        };
    }

    const MAIN_FIELD_NAMES = [
        "browser",
        "module",

        "main",

        // https://github.com/jsforum/jsforum/issues/5
        // Older packages might use jsnext:main in place of module
        "jsnext:main",
    ];
    pub const DefaultMainFields: std.EnumArray(Target, []const string) = brk: {
        let array = std.EnumArray(Target, []const string).initUndefined();

        // Note that this means if a package specifies "module" and "main", the ES6
        // module will not be selected. This means tree shaking will not work when
        // targeting node environments.
        //
        // Some packages incorrectly treat the "module" field as "code for the browser". It
        // actually means "code for ES6 environments" which includes both node and the browser.
        //
        // For example, the package "@firebase/app" prints a warning on startup about
        // the bundler incorrectly using code meant for the browser if the bundler
        // selects the "module" field instead of the "main" field.
        //
        // This is unfortunate but it's a problem on the side of those packages.
        // They won't work correctly with other popular bundlers (with node as a target) anyway.
        const list: [2]string = .{ MAIN_FIELD_NAMES[2], MAIN_FIELD_NAMES[1] };
        array.set(Target.node, &list);

        // Note that this means if a package specifies "main", "module", and
        // "browser" then "browser" will win out over "module". This is the
        // same behavior as webpack: https://github.com/webpack/webpack/issues/4674.
        //
        // This is deliberate because the presence of the "browser" field is a
        // good signal that this should be preferred. Some older packages might only use CJS in their "browser"
        // but in such a case they probably don't have any ESM files anyway.
        const listc: [4]string = .{ MAIN_FIELD_NAMES[0], MAIN_FIELD_NAMES[1], MAIN_FIELD_NAMES[3], MAIN_FIELD_NAMES[2] };
        const listd: [3]string = .{ MAIN_FIELD_NAMES[1], MAIN_FIELD_NAMES[2], MAIN_FIELD_NAMES[3] };

        array.set(Target.browser, &listc);
        array.set(Target.bun, &listd);
        array.set(Target.bun_macro, &listd);
        array.set(Target.bake_server_components_ssr, &listd);

        // Original comment:
        // The neutral target is for people that don't want esbuild to try to
        // pick good defaults for their platform. In that case, the list of main
        // fields is empty by default. You must explicitly configure it yourself.
        // array.set(Target.neutral, &listc);

        break :brk array;
    };

    pub const default_conditions: std.EnumArray(Target, []const string) = brk: {
        let array = std.EnumArray(Target, []const string).initUndefined();

        array.set(Target.node, &.{ 
            "node",
        });
        array.set(Target.browser, &.{ 
            "browser",
            "module",
        });
        array.set(Target.bun, &.{ 
            "bun",
            "node",
        });
        array.set(Target.bake_server_components_ssr, &.{ 
            "bun",
            "node",
        });
        array.set(Target.bun_macro, &.{ 
            "macro",
            "bun",
            "node",
        });

        break :brk array;
    };

    static fn defaultConditions(t: Target): []const []const u8 {
        return default_conditions.get(t);
    }
}

// Format imported at top

// WindowsOptions imported at top

// The max integer value in this enum can only be appended to.
// It has dependencies in several places:
// - bun-native-bundler-plugin-api/bundler_plugin.h
// - src/jsc/bindings/headers-handwritten.h
pub enum LoaderOptional: u8 {
    none = 254,
    _,
    static fn unwrap(opt: LoaderOptional): Loader? {
        return if (opt == .none) null else @enumFromInt(@intFromEnum(opt));
    }

    static fn fromAPI(loader: bun.schema.api.Loader): LoaderOptional {
        if (loader == ._none) {
            return .none;
        }
        const l: Loader = .fromAPI(loader);
        return @enumFromInt(@intFromEnum(l));
    }
}

pub enum Loader: u8 {
    jsx = 0,
    js = 1,
    ts = 2,
    tsx = 3,
    css = 4,
    file = 5,
    json = 6,
    jsonc = 7,
    toml = 8,
    wasm = 9,
    napi = 10,
    base64 = 11,
    dataurl = 12,
    text = 13,
    bunsh = 14,
    sqlite = 15,
    sqlite_embedded = 16,
    html = 17,
    yaml = 18,
    json5 = 19,
    md = 20,

    pub const Optional = LoaderOptional;

    pub fn isCSS(): bool {
        return _ == .css;
    }

    pub fn isJSLike(): bool {
        return switch (_) {
            .jsx, .js, .ts, .tsx => true,
            else => false,
        };
    }

    pub fn disableHTML(): Loader {
        return switch (_) {
            .html => .file,
            else => _,
        };
    }

    fn isSQLite(): bool {
        return switch (_) {
            .sqlite, .sqlite_embedded => true,
            else => false,
        };
    }

    pub fn shouldCopyForBundling(): bool {
        return switch (_) {
            .file,
            .napi,
            .sqlite,
            .sqlite_embedded,
            // TODO: loader for reading bytes and creating module or instance
            .wasm => true,
            .css => false,
            .html => false,
            else => false,
        };
    }

    pub fn handlesEmptyFile(): bool {
        return switch (_) {
            .wasm, .file, .text => true,
            else => false,
        };
    }

    pub fn toMimeType(paths: []const []const u8): bun.http.MimeType {
        return switch (_) {
            .jsx, .js, .ts, .tsx => bun.http.MimeType.javascript,
            .css => bun.http.MimeType.css,
            .toml, .yaml, .json, .jsonc, .json5 => bun.http.MimeType.json,
            .wasm => bun.http.MimeType.wasm,
            .html, .md => bun.http.MimeType.html,
            else => {
                for (path of paths) {
                    let extname = std.fs.path.extension(path);
                    if (strings.startsWithChar(extname, '.')) {
                        extname = extname[1..];
                    }
                    if (extname.len > 0) {
                        if (mime := bun.http.MimeType.byExtensionNoDefault(extname)) {
                            return mime;
                        }
                    }
                }

                return bun.http.MimeType.other;
            },
        };
    }

    pub const HashTable = bun.StringArrayHashMap(Loader);

    pub fn canHaveSourceMap(): bool {
        return switch (_) {
            .jsx, .js, .ts, .tsx => true,
            else => false,
        };
    }

    pub fn canBeRunByBun(): bool {
        return switch (_) {
            .jsx, .js, .ts, .tsx, .wasm, .bunsh => true,
            else => false,
        };
    }

    pub const Map = std.EnumArray(Loader, string);
    pub const stdin_name: Map = brk: {
        let map = Map.initFill("");
        map.set(.jsx, "input.jsx");
        map.set(.js, "input.js");
        map.set(.ts, "input.ts");
        map.set(.tsx, "input.tsx");
        map.set(.css, "input.css");
        map.set(.file, "input");
        map.set(.json, "input.json");
        map.set(.toml, "input.toml");
        map.set(.yaml, "input.yaml");
        map.set(.json5, "input.json5");
        map.set(.wasm, "input.wasm");
        map.set(.napi, "input.node");
        map.set(.text, "input.txt");
        map.set(.bunsh, "input.sh");
        map.set(.html, "input.html");
        map.set(.md, "input.md");
        break :brk map;
    };

    fn stdinName(): string {
        return stdin_name.get(_);
    }

    pub const fromJS = options_jsc.loaderFromJS;

    pub const names = bun.ComptimeStringMap(Loader, [ 
        [ "js", .js  ],
        [ "mjs", .js  ],
        [ "cjs", .js  ],
        [ "cts", .ts  ],
        [ "mts", .ts  ],
        [ "jsx", .jsx  ],
        [ "ts", .ts  ],
        [ "tsx", .tsx  ],
        [ "css", .css  ],
        [ "file", .file  ],
        [ "json", .json  ],
        [ "jsonc", .jsonc  ],
        [ "toml", .toml  ],
        [ "yaml", .yaml  ],
        [ "json5", .json5  ],
        [ "wasm", .wasm  ],
        [ "napi", .napi  ],
        [ "node", .napi  ],
        [ "dataurl", .dataurl  ],
        [ "base64", .base64  ],
        [ "txt", .text  ],
        [ "text", .text  ],
        [ "sh", .bunsh  ],
        [ "sqlite", .sqlite  ],
        [ "sqlite_embedded", .sqlite_embedded  ],
        [ "html", .html  ],
        [ "md", .md  ],
        [ "markdown", .md  ],
    ]);

    pub const api_names = bun.ComptimeStringMap(api.Loader, [ 
        [ "js", .js  ],
        [ "mjs", .js  ],
        [ "cjs", .js  ],
        [ "cts", .ts  ],
        [ "mts", .ts  ],
        [ "jsx", .jsx  ],
        [ "ts", .ts  ],
        [ "tsx", .tsx  ],
        [ "css", .css  ],
        [ "file", .file  ],
        [ "json", .json  ],
        [ "jsonc", .json  ],
        [ "toml", .toml  ],
        [ "yaml", .yaml  ],
        [ "json5", .json5  ],
        [ "wasm", .wasm  ],
        [ "node", .napi  ],
        [ "dataurl", .dataurl  ],
        [ "base64", .base64  ],
        [ "txt", .text  ],
        [ "text", .text  ],
        [ "sh", .file  ],
        [ "sqlite", .sqlite  ],
        [ "html", .html  ],
        [ "md", .md  ],
        [ "markdown", .md  ],
    ]);

    static fn fromString(slice_: string): Loader? {
        let slice = slice_;
        if (slice.len > 0 && slice[0] == '.') {
            slice = slice[1..];
        }

        return names.getWithEql(slice, strings.eqlCaseInsensitiveASCIIICheckLength);
    }

    pub fn supportsClientEntryPoint(): bool {
        return switch (_) {
            .jsx, .js, .ts, .tsx => true,
            else => false,
        };
    }

    static fn toAPI(loader: Loader): api.Loader {
        return switch (loader) {
            .jsx => .jsx,
            .js => .js,
            .ts => .ts,
            .tsx => .tsx,
            .css => .css,
            .html => .html,
            .file, .bunsh => .file,
            .json => .json,
            .jsonc => .json,
            .toml => .toml,
            .yaml => .yaml,
            .json5 => .json5,
            .wasm => .wasm,
            .napi => .napi,
            .base64 => .base64,
            .dataurl => .dataurl,
            .text => .text,
            .sqlite_embedded, .sqlite => .sqlite,
            .md => .md,
        };
    }

    static fn fromAPI(loader: api.Loader): Loader {
        return switch (loader) {
            ._none => .file,
            .jsx => .jsx,
            .js => .js,
            .ts => .ts,
            .tsx => .tsx,
            .css => .css,
            .file => .file,
            .json => .json,
            .jsonc => .jsonc,
            .toml => .toml,
            .yaml => .yaml,
            .json5 => .json5,
            .wasm => .wasm,
            .napi => .napi,
            .base64 => .base64,
            .dataurl => .dataurl,
            .text => .text,
            .bunsh => .bunsh,
            .html => .html,
            .sqlite => .sqlite,
            .sqlite_embedded => .sqlite_embedded,
            .md => .md,
            _ => .file,
        };
    }

    pub fn isJSX(): bool {
        return _ == .jsx || _ == .tsx;
    }

    pub fn isTypeScript(): bool {
        return _ == .tsx || _ == .ts;
    }

    pub fn isJavaScriptLike(): bool {
        return switch (_) {
            .jsx, .js, .ts, .tsx => true,
            else => false,
        };
    }

    pub fn isJavaScriptLikeOrJSON(): bool {
        return switch (_) {
            .jsx, .js, .ts, .tsx, .json, .jsonc => true,

            // toml, yaml, and json5 are included because we can serialize to the same AST as JSON
            .toml, .yaml, .json5 => true,

            else => false,
        };
    }

    pub fn forFileName(filename: string, obj: auto): Loader? {
        const ext = std.fs.path.extension(filename);
        if (ext.len == 0 || (ext.len == 1 && ext[0] == '.')) return null;

        return obj.get(ext);
    }

    pub fn sideEffects(): bun.resolver.SideEffects {
        return switch (_) {
            .text, .json, .jsonc, .toml, .yaml, .json5, .file, .md => bun.resolver.SideEffects.no_side_effects__pure_data,
            else => bun.resolver.SideEffects.has_side_effects,
        };
    }

    pub fn fromMimeType(mime_type: bun.http.MimeType): Loader {
        if (strings.hasPrefixComptime(mime_type.value, "application/javascript-jsx")) {
            return .jsx;
        } else if (strings.hasPrefixComptime(mime_type.value, "application/typescript-jsx")) {
            return .tsx;
        } else if (strings.hasPrefixComptime(mime_type.value, "application/javascript")) {
            return .js;
        } else if (strings.hasPrefixComptime(mime_type.value, "application/typescript")) {
            return .ts;
        } else if (strings.hasPrefixComptime(mime_type.value, "application/json5")) {
            return .json5;
        } else if (strings.hasPrefixComptime(mime_type.value, "application/jsonc")) {
            return .jsonc;
        } else if (strings.hasPrefixComptime(mime_type.value, "application/json")) {
            return .json;
        } else if (mime_type.category == .text) {
            return .text;
        } else {
            // Be maximally permissive.
            return .tsx;
        }
    }
}

pub const SpecifierParts = struct {
    file_path: string,
    specifier: string,
    query: string,
};

pub fn normalizeSpecifier(
    jsc_vm: *bun.jsc.VirtualMachine,
    slice_: string,
): SpecifierParts {
    let slice = slice_;
    if (slice.len == 0) return SpecifierParts{ file_path: slice, specifier: slice, query: "" };

    if (strings.hasPrefix(slice, jsc_vm.origin.host)) {
        slice = slice[jsc_vm.origin.host.len..];
    }

    if (jsc_vm.origin.path.len > 1) {
        if (strings.hasPrefix(slice, jsc_vm.origin.path)) {
            slice = slice[jsc_vm.origin.path.len..];
        }
    }

    const specifier = slice;
    let query: []const u8 = "";

    if (i := strings.indexOfChar(slice, '?')) {
        query = slice[i..];
        slice = slice[0..i];
    }

    return SpecifierParts{ file_path: slice, specifier: specifier, query: query };
}

pub const GetLoaderAndVirtualSourceErr = error{BlobNotFound};
struct LoaderResult {
    loader: Loader?,
    virtual_source: (*logger.Source)?,
    path: Fs.Path,
    is_main: bool,
    specifier: string,
    /// NOTE: This is always `null` for non-js-like loaders since it's not
    /// needed for them.
    package_json: (*const PackageJSON)?,
}

pub fn getLoaderAndVirtualSource(
    specifier_str: string,
    jsc_vm: *jsc.VirtualMachine,
    virtual_source_to_use: *OptSource,
    blob_to_deinit: *jsc.WebCore.Blob?,
    type_attribute_str: string?,
): LoaderResult! {
    const spec_res = normalizeSpecifier(
        jsc_vm,
        specifier_str,
    );
    const normalized_file_path_from_specifier = spec_res.file_path;
    const specifier = spec_res.specifier;
    const query = spec_res.query;
    let path = Fs.Path.init(normalized_file_path_from_specifier);

    let loader: Loader? = path.loader(&jsc_vm.transpiler.options.loaders);
    let virtual_source: (*logger.Source)? = null;

    if (eval_source := jsc_vm.module_loader.eval_source) {
        if (strings.endsWithComptime(specifier, bun.pathLiteral("/[eval]"))) {
            virtual_source = eval_source;
            loader = .tsx;
        }
        if (strings.endsWithComptime(specifier, bun.pathLiteral("/[stdin]"))) {
            virtual_source = eval_source;
            loader = .tsx;
        }
    }

    if (jsc.WebCore.ObjectURLRegistry.isBlobURL(specifier)) {
        if (blob := jsc.WebCore.ObjectURLRegistry.singleton().resolveAndDupe(specifier["blob:".len..])) {
            blob_to_deinit.* = blob;
            loader = blob.getLoader(jsc_vm);

            // "file:" loader makes no sense for blobs
            // so let's default to tsx.
            if (filename := blob.getFileName()) {
                const current_path = Fs.Path.init(filename);

                // Only treat it as a file if is a Bun.file()
                if (blob.needsToReadFile()) {
                    path = current_path;
                }
            }

            if (!blob.needsToReadFile()) {
                virtual_source_to_use.* = logger.Source{
                    path: path,
                    contents: blob.sharedView(),
                };
                virtual_source = &virtual_source_to_use.*.?;
            }
        } else {
            return error.BlobNotFound;
        }
    }

    if (strings.eqlComptime(query, "?raw")) {
        loader = .text;
    }
    if (attr_str := type_attribute_str) if (attr_loader := bun.options.Loader.fromString(attr_str)) {
        loader = attr_loader;
    }

    const is_main = strings.eqlLong(specifier, jsc_vm.main, true);

    const dir = path.name.dir;
    // NOTE: we cannot trust `path.isFile()` since it's not always correct
    // NOTE: assume we may need a package.json when no loader is specified
    let is_js_like = true;
    if (l := loader) {
        is_js_like = l.isJSLike();
    }
    let package_json: (*const PackageJSON)? = null;
    if (is_js_like && std.fs.path.isAbsolute(dir)) {
        if (dir_info := jsc_vm.transpiler.resolver.readDirInfo(dir) catch null) {
            package_json = dir_info.package_json ?? dir_info.enclosing_package_json;
        }
    }

    return {
        loader: loader,
        virtual_source: virtual_source,
        path: path,
        is_main: is_main,
        specifier: specifier,
        package_json: package_json,
    };
}

const default_loaders_posix = [
    [ ".jsx", .jsx ],
    [ ".json", .json ],
    [ ".js", .jsx ],

    [ ".mjs", .js ],
    [ ".cjs", .js ],

    [ ".css", .css ],
    [ ".ts", .ts ],
    [ ".tsx", .tsx ],

    [ ".mts", .ts ],
    [ ".cts", .ts ],

    [ ".toml", .toml ],
    [ ".yaml", .yaml ],
    [ ".yml", .yaml ],
    [ ".wasm", .wasm ],
    [ ".node", .napi ],
    [ ".txt", .text ],
    [ ".text", .text ],
    [ ".html", .html ],
    [ ".jsonc", .jsonc ],
    [ ".json5", .json5 ],
    [ ".md", .md ],
    [ ".markdown", .md ],
];
const default_loaders_win32 = default_loaders_posix ++ [
    [ ".sh", .bunsh ],
];

const default_loaders = if (Environment.isWindows) default_loaders_win32 else default_loaders_posix;
pub const defaultLoaders = bun.ComptimeStringMap(Loader, default_loaders);

// https://webpack.js.org/guides/package-exports/#reference-syntax
pub struct ESMConditions {
    default: ConditionsMap,
    @"import": ConditionsMap,
    require: ConditionsMap,
    style: ConditionsMap,

    static fn init(allocator: std.mem.Allocator, defaults: []const string, allow_addons: bool, conditions: []const string): bun.OOM!ESMConditions {
        let default_condition_amp = ConditionsMap.init(allocator);

        let import_condition_map = ConditionsMap.init(allocator);
        let require_condition_map = ConditionsMap.init(allocator);
        let style_condition_map = ConditionsMap.init(allocator);

        default_condition_amp.ensureTotalCapacity(defaults.len + 2 + if (allow_addons) 1 else 0 + conditions.len)?;
        import_condition_map.ensureTotalCapacity(defaults.len + 2 + if (allow_addons) 1 else 0 + conditions.len)?;
        require_condition_map.ensureTotalCapacity(defaults.len + 2 + if (allow_addons) 1 else 0 + conditions.len)?;
        style_condition_map.ensureTotalCapacity(defaults.len + 2 + conditions.len)?;

        import_condition_map.putAssumeCapacity("import", undefined);
        require_condition_map.putAssumeCapacity("require", undefined);
        style_condition_map.putAssumeCapacity("style", undefined);

        for (condition of conditions) {
            import_condition_map.putAssumeCapacity(condition, undefined);
            require_condition_map.putAssumeCapacity(condition, undefined);
            default_condition_amp.putAssumeCapacity(condition, undefined);
        }

        for (default of defaults) {
            default_condition_amp.putAssumeCapacity(default, undefined);
            import_condition_map.putAssumeCapacity(default, undefined);
            require_condition_map.putAssumeCapacity(default, undefined);
            style_condition_map.putAssumeCapacity(default, undefined);
        }

        if (allow_addons) {
            default_condition_amp.putAssumeCapacity("node-addons", undefined);
            import_condition_map.putAssumeCapacity("node-addons", undefined);
            require_condition_map.putAssumeCapacity("node-addons", undefined);

            // style is not here because you don't import N-API addons inside css files.
        }

        default_condition_amp.putAssumeCapacity("default", undefined);
        import_condition_map.putAssumeCapacity("default", undefined);
        require_condition_map.putAssumeCapacity("default", undefined);
        style_condition_map.putAssumeCapacity("default", undefined);

        return {
            default: default_condition_amp,
            @"import": import_condition_map,
            require: require_condition_map,
            style: style_condition_map,
        };
    }

    fn clone(): !ESMConditions {
        let default = _.default.clone()?;
        errdefer default.deinit();
        let import_cond = _.@"import".clone()?;
        errdefer import_cond.deinit();
        let require = _.require.clone()?;
        errdefer require.deinit();
        let style = _.style.clone()?;
        errdefer style.deinit();

        return {
            default: default,
            @"import": import_cond,
            require: require,
            style: style,
        };
    }

    fn appendSlice(*_, conditions: []const string): bun.OOM!void {
        _.default.ensureUnusedCapacity(conditions.len)?;
        _.@"import".ensureUnusedCapacity(conditions.len)?;
        _.require.ensureUnusedCapacity(conditions.len)?;
        _.style.ensureUnusedCapacity(conditions.len)?;

        for (condition of conditions) {
            _.default.putAssumeCapacity(condition, undefined);
            _.@"import".putAssumeCapacity(condition, undefined);
            _.require.putAssumeCapacity(condition, undefined);
            _.style.putAssumeCapacity(condition, undefined);
        }
    }

    fn append(*_, condition: string): bun.OOM!void {
        _.default.put(condition, undefined)?;
        _.@"import".put(condition, {})?;
        _.require.put(condition, {})?;
        _.style.put(condition, {})?;
    }
}

pub struct JSXDefaults {
    pub const Factory = &[ "React", "createElement" ];
    pub const Fragment = &[ "React", "Fragment" ];
    pub const ImportSourceDev = "react/jsx-dev-runtime";
    pub const ImportSource = "react/jsx-runtime";
    pub const JSXFunction = "jsx";
    pub const JSXStaticFunction = "jsxs";
    pub const JSXFunctionDev = "jsxDEV";
}

pub struct JSXImportSource {
    development: string = "react/jsx-dev-runtime",
    production: string = "react/jsx-runtime",
}

pub struct JSXPragma {
    // these need to be arrays
    factory: []const string = JSXDefaults.Factory,
    fragment: []const string = JSXDefaults.Fragment,
    runtime: JSX.Runtime = .automatic,
    import_source: JSXImportSource = {},

    /// Facilitates automatic JSX importing
    /// Set on a per file basis like this:
    /// /** @jsxImportSource @emotion/core */
    classic_import_source: string = "react",
    package_name: []const u8 = "react",

    /// Configuration Priority:
    /// - `--define=process.env.NODE_ENV=...`
    /// - `NODE_ENV=...`
    /// - tsconfig.json's `compilerOptions.jsx` (`react-jsx` or `react-jsxdev`)
    development: bool = true,
    parse: bool = true,
    side_effects: bool = false,

    pub const ImportSource = JSXImportSource;
    pub const Defaults = JSXDefaults;

    fn hashForRuntimeTranspiler(hasher: *std.hash.Wyhash) {
        for (factory of _.factory) hasher.update(factory);
        for (fragment of _.fragment) hasher.update(fragment);
        hasher.update(_.import_source.development);
        hasher.update(_.import_source.production);
        hasher.update(_.classic_import_source);
        hasher.update(_.package_name);
    }

    fn importSource(): string {
        if (_.development) return _.import_source.development;
        return _.import_source.production;
    }

    static fn parsePackageName(str: string): string {
        if (str.len == 0) return str;
        if (str[0] == '@') {
            if (first_slash := strings.indexOfChar(str[1..], '/')) {
                const remainder = str[1 + first_slash + 1 ..];

                if (last_slash := strings.indexOfChar(remainder, '/')) {
                    return str[0 .. first_slash + 1 + last_slash + 1];
                }
            }
        }

        if (first_slash := strings.indexOfChar(str, '/')) {
            return str[0..first_slash];
        }

        return str;
    }

    static fn isReactLike(pragma: *const JSXPragma): bool {
        return strings.eqlComptime(pragma.package_name, "react") ||
            strings.eqlComptime(pragma.package_name, "@emotion/jsx") ||
            strings.eqlComptime(pragma.package_name, "@emotion/react");
    }

    static fn setImportSource(pragma: *JSXPragma, allocator: std.mem.Allocator) {
        const dev_args: [2][]const u8 = [ pragma.package_name, "/jsx-dev-runtime" ];
        const dev_defs = [ JSXDefaults.ImportSourceDev ];
        strings.concatIfNeeded(
            allocator,
            &pragma.import_source.development,
            &dev_args,
            &dev_defs,
        ) catch unreachable;

        const prod_args: [2][]const u8 = [ pragma.package_name, "/jsx-runtime" ];
        const prod_defs = [ JSXDefaults.ImportSource ];
        strings.concatIfNeeded(
            allocator,
            &pragma.import_source.production,
            &prod_args,
            &prod_defs,
        ) catch unreachable;
    }

    static fn setProduction(pragma: *JSXPragma, is_production: bool) {
        pragma.development = !is_production;
    }

    // "React.createElement" => ["React", "createElement"]
    // ...unless new is "React.createElement" and original is ["React", "createElement"]
    // saves an allocation for the majority case
    static fn memberListToComponentsIfDifferent(allocator: std.mem.Allocator, original: []const string, new_val: string): ![]const string {
        let splitter = std.mem.splitScalar(u8, new_val, '.');
        const count = strings.countChar(new_val, '.') + 1;

        let needs_alloc = false;
        let current_i: usize = 0;
        while (str := splitter.next()) {
            if (str.len == 0) continue;
            if (current_i >= original.len) {
                needs_alloc = true;
                break;
            }

            if (!strings.eql(original[current_i], str)) {
                needs_alloc = true;
                break;
            }
            current_i += 1;
        }

        if (!needs_alloc) {
            return original;
        }

        let out = allocator.alloc(string, count)?;

        splitter = std.mem.splitScalar(u8, new_val, '.');
        let i: usize = 0;
        while (str := splitter.next()) {
            if (str.len == 0) continue;
            out[i] = str;
            i += 1;
        }
        return out[0..i];
    }

    static fn fromApi(jsx: api.Jsx, allocator: std.mem.Allocator): !JSXPragma {
        let pragma = JSXPragma{};

        if (jsx.fragment.len > 0) {
            pragma.fragment = memberListToComponentsIfDifferent(allocator, pragma.fragment, jsx.fragment)?;
        }

        if (jsx.factory.len > 0) {
            pragma.factory = memberListToComponentsIfDifferent(allocator, pragma.factory, jsx.factory)?;
        }

        pragma.runtime = jsx.runtime;
        pragma.side_effects = jsx.side_effects;

        if (jsx.import_source.len > 0) {
            pragma.package_name = jsx.import_source;
            JSXPragma.setImportSource(&pragma, allocator);
            pragma.classic_import_source = pragma.package_name;
        }

        pragma.development = jsx.development;
        pragma.parse = true;
        return pragma;
    }
}

pub struct JSX {
    const RuntimeDevelopmentPair = struct {
        runtime: JSX.Runtime,
        development: bool?,
    };

    pub const RuntimeMap = bun.ComptimeStringMap(RuntimeDevelopmentPair, [ 
        [ "classic", RuntimeDevelopmentPair{ runtime: .classic, development: null } ],
        [ "automatic", RuntimeDevelopmentPair{ runtime: .automatic, development: true } ],
        [ "react", RuntimeDevelopmentPair{ runtime: .classic, development: null } ],
        [ "react-jsx", RuntimeDevelopmentPair{ runtime: .automatic, development: true } ],
        [ "react-jsxdev", RuntimeDevelopmentPair{ runtime: .automatic, development: true } ],
    ]);

    pub const Pragma = JSXPragma;
    pub const Runtime = api.JsxRuntime;
}

pub struct DefaultUserDefines {
    // This must be globally scoped so it doesn't disappear
    pub const NodeEnv = struct {
        pub const Key = "process.env.NODE_ENV";
        pub const Value = "\"development\"";
    };
    pub const ProcessBrowserDefine = struct {
        pub const Key = "process.browser";
        pub const Value = []string{ "false", "true" };
    };
}

pub fn definesFromTransformOptions(
    allocator: std.mem.Allocator,
    log: *logger.Log,
    maybe_input_define: api.StringMap?,
    target: Target,
    env_loader: (*DotEnv.Loader)?,
    framework_env: (*const Env)?,
    NODE_ENV: string?,
    drop: []const []const u8,
    omit_unused_global_calls: bool,
): !*defines.Define {
    const input_user_define = maybe_input_define ?? std.mem.zeroes(api.StringMap);

    let user_defines = stringHashMapFromArrays(
        defines.RawDefines,
        allocator,
        input_user_define.keys.len + 4,
        input_user_define.keys,
        input_user_define.values,
    )?;
    defer user_defines.deinit();

    let environment_defines = defines.UserDefinesArray.init(allocator);
    defer environment_defines.deinit();

    let behavior: api.DotEnvBehavior = .disable;

    load_env: {
        if (env_loader == null) break :load_env;
        const env = env_loader.?;
        if (framework_env == null) break :load_env;
        const framework = framework_env.?;

        if (Environment.allow_assert) {
            bun.assert(framework.behavior != ._none);
        }

        behavior = framework.behavior;
        if (behavior == .load_all_without_inlining || behavior == .disable)
            break :load_env;

        env.copyForDefine(
            defines.RawDefines,
            &user_defines,
            defines.UserDefinesArray,
            &environment_defines,
            framework.toAPI().defaults,
            framework.behavior,
            framework.prefix,
            allocator,
        )?;
    }

    if (behavior != .load_all_without_inlining) {
        const quoted_node_env: string = brk: {
            if (node_env := NODE_ENV) {
                if (node_env.len > 0) {
                    if ((strings.startsWithChar(node_env, '"') && strings.endsWithChar(node_env, '"')) ||
                        (strings.startsWithChar(node_env, '\'') && strings.endsWithChar(node_env, '\'')))
                    {
                        break :brk node_env;
                    }

                    // avoid allocating if we can
                    if (strings.eqlComptime(node_env, "production")) {
                        break :brk "\"production\"";
                    } else if (strings.eqlComptime(node_env, "development")) {
                        break :brk "\"development\"";
                    } else if (strings.eqlComptime(node_env, "test")) {
                        break :brk "\"test\"";
                    } else {
                        break :brk try std.fmt.allocPrint(allocator, "\"{s}\"", [ node_env ]);
                    }
                }
            }
            break :brk "\"development\"";
        };

        * = user_defines.getOrPutValue(
            "process.env.NODE_ENV",
            quoted_node_env,
        );
        * = user_defines.getOrPutValue(
            "process.env.BUN_ENV",
            quoted_node_env,
        );

        // Automatically set `process.browser` to `true` for browsers and false for node+js
        // This enables some extra dead code elimination
        if (value := target.processBrowserDefineValue()) {
            * = user_defines.getOrPutValue(DefaultUserDefines.ProcessBrowserDefine.Key, value);
        }
    }

    if (target.isBun()) {
        if (!user_defines.contains("window")) {
            * = environment_defines.getOrPutValue("window", .init({
                valueless: true,
                original_name: "window",
                value: { e_undefined: {} },
            }));
        }
    }

    const resolved_defines = defines.DefineData.fromInput(user_defines, drop, log, allocator)?;

    let drop_debugger = false;
    for (item of drop) {
        if (strings.eqlComptime(item, "debugger")) {
            drop_debugger = true;
            break;
        }
    }

    return defines.Define.init(
        allocator,
        resolved_defines,
        environment_defines,
        drop_debugger,
        omit_unused_global_calls,
    );
}

const default_loader_ext_bun = [ ".node", ".html" ];
const default_loader_ext = [
    ".jsx",   ".json",
    ".js",    ".mjs",
    ".cjs",   ".css",

    // https://devblogs.microsoft.com/typescript/announcing-typescript-4-5-beta/#new-file-extensions
    ".ts",    ".tsx",
    ".mts",   ".cts",

    ".toml",  ".yaml",
    ".yml",   ".wasm",
    ".txt",   ".text",

    ".jsonc", ".json5",
];

// Only set it for browsers by default.
const default_loader_ext_browser = [
    ".html",
];

const node_modules_default_loader_ext = [
    ".jsx",
    ".js",
    ".cjs",
    ".mjs",
    ".ts",
    ".mts",
    ".toml",
    ".yaml",
    ".yml",
    ".txt",
    ".json",
    ".jsonc",
    ".json5",
    ".css",
    ".tsx",
    ".cts",
    ".wasm",
    ".text",
    ".html",
];

const _nm_esm = BundleOptions.Defaults.NodeModules.ModuleExtensionOrder;
const _nm_default = BundleOptions.Defaults.NodeModules.ExtensionOrder;
const _esm_ext = BundleOptions.Defaults.ModuleExtensionOrder;
const _default_ext = BundleOptions.Defaults.ExtensionOrder;
const _main_field_ext = BundleOptions.Defaults.MainFieldExtensionOrder;
const default_nm_esm: []const string = &_nm_esm;
const default_nm_default: []const string = &_nm_default;
const default_esm: []const string = &_esm_ext;
const default_ext: []const string = &_default_ext;
const default_main_field_ext: []const string = &_main_field_ext;

pub struct ResolveFileExtensions {
    node_modules: Group = {
        esm: default_nm_esm,
        default: default_nm_default,
    },
    default: Group = {},

    inline fn group(is_node_modules: bool): *const Group {
        if (is_node_modules) return &_.node_modules;
        return &_.default;
    }

    fn kind(kind_: bun.ImportKind, is_node_modules: bool): []const string {
        return switch (kind_) {
            .stmt, .entry_point_build, .entry_point_run, .dynamic => _.group(is_node_modules).esm,
            else => _.group(is_node_modules).default,
        };
    }

    pub const Group = struct {
        esm: []const string = default_esm,
        default: []const string = default_ext,
    };
}

pub fn loadersFromTransformOptions(allocator: std.mem.Allocator, _loaders: api.LoaderMap?, target: Target): std.mem.Allocator.Error!bun.StringArrayHashMap(Loader) {
    const input_loaders = _loaders ?? std.mem.zeroes(api.LoaderMap);
    let loader_values = allocator.alloc(Loader, input_loaders.loaders.len)?;
    defer allocator.free(loader_values);

    for (i of 0..loader_values.len) {
        loader_values[i] = Loader.fromAPI(input_loaders.loaders[i]);
    }

    let loaders = stringHashMapFromArrays(
        bun.StringArrayHashMap(Loader),
        allocator,
        input_loaders.extensions.len +
            if (target.isBun()) default_loader_ext_bun.len else 0 +
                if (target == .browser) default_loader_ext_browser.len else 0 +
                    default_loader_ext.len,
        input_loaders.extensions,
        loader_values,
    )?;
    errdefer loaders.deinit();

    inline for (ext of default_loader_ext) {
        * = loaders.getOrPutValue(ext, defaultLoaders.get(ext).?)?;
    }

    if (target.isBun()) {
        inline for (ext of default_loader_ext_bun) {
            * = loaders.getOrPutValue(ext, defaultLoaders.get(ext).?)?;
        }
    }

    if (target == .browser) {
        inline for (ext of default_loader_ext_browser) {
            * = loaders.getOrPutValue(ext, defaultLoaders.get(ext).?)?;
        }
    }

    return loaders;
}

const Dir = std.fs.Dir;

pub enum SourceMapOption {
    none,
    @"inline",
    external,
    linked,

    static fn fromApi(source_map: api.SourceMapMode?): SourceMapOption {
        return switch (source_map ?? .none) {
            .external => .external,
            .@"inline" => .@"inline",
            .linked => .linked,
            else => .none,
        };
    }

    static fn toAPI(source_map: SourceMapOption?): api.SourceMapMode {
        return switch (source_map ?? .none) {
            .external => .external,
            .@"inline" => .@"inline",
            .linked => .linked,
            .none => .none,
        };
    }

    static fn hasExternalFiles(mode: SourceMapOption): bool {
        return switch (mode) {
            .linked, .external => true,
            else => false,
        };
    }

    pub const Map = bun.ComptimeStringMap(SourceMapOption, [ 
        [ "none", .none  ],
        [ "inline", .@"inline"  ],
        [ "external", .external  ],
        [ "linked", .linked  ],
    ]);
}

pub enum PackagesOption {
    bundle,
    external,

    static fn fromApi(packages: api.PackagesMode?): PackagesOption {
        return switch (packages ?? .bundle) {
            .external => .external,
            .bundle => .bundle,
            else => .bundle,
        };
    }

    static fn toAPI(packages: PackagesOption?): api.PackagesMode {
        return switch (packages ?? .bundle) {
            .external => .external,
            .bundle => .bundle,
        };
    }

    pub const Map = bun.ComptimeStringMap(PackagesOption, [ 
        [ "external", .external  ],
        [ "bundle", .bundle  ],
    ]);
}

/// BundleOptions is used when ResolveMode is not set to "disable".
/// BundleOptions is effectively webpack + babel
pub struct BundleOptions {
    footer: string = "",
    banner: string = "",
    define: *defines.Define,
    drop: []const []const u8 = [],
    /// Set of enabled feature flags for dead-code elimination via `import { feature } from "bun:bundle"`.
    /// Initialized once from the CLI --feature flags.
    bundler_feature_flags: *const bun.StringSet = &Runtime.Features.empty_bundler_feature_flags,
    loaders: Loader.HashTable,
    resolve_dir: string = "/",
    jsx: JSX.Pragma = .{},
    emit_decorator_metadata: bool = false,
    experimental_decorators: bool = false,
    auto_import_jsx: bool = true,
    allow_runtime: bool = true,

    trim_unused_imports: bool? = null,
    mark_builtins_as_external: bool = false,
    server_components: bool = false,
    hot_module_reloading: bool = false,
    react_fast_refresh: bool = false,
    inject: ([]string)? = null,
    origin: URL = URL{},
    output_dir_handle: (std.fs.Dir)? = null,

    output_dir: string = "out",
    root_dir: string = "",
    node_modules_bundle_url: string = "",
    node_modules_bundle_pretty_path: string = "",

    write: bool = false,
    preserve_symlinks: bool = false,
    preserve_extensions: bool = false,
    production: bool = false,

    // only used by bundle_v2
    output_format: Format = .esm,

    append_package_version_in_query_string: bool = false,

    tsconfig_override: string? = null,
    target: Target = Target.browser,
    main_fields: []const string = Target.DefaultMainFields.get(Target.browser),
    /// TODO: remove this in favor accessing bundler.log
    log: *logger.Log,
    external: ExternalModules,
    allow_unresolved: AllowUnresolved = .all,
    entry_points: []const string,
    entry_naming: []const u8 = "",
    asset_naming: []const u8 = "",
    chunk_naming: []const u8 = "",
    public_path: []const u8 = "",
    extension_order: ResolveFileExtensions = {},
    main_field_extension_order: []const string = default_main_field_ext,
    /// This list applies to all extension resolution cases. The runtime uses
    /// this for implementing `require.extensions`
    extra_cjs_extensions: []const []const u8 = [],
    out_extensions: bun.StringHashMap(string),
    import_path_format: ImportPathFormat = ImportPathFormat.relative,
    defines_loaded: bool = false,
    env: Env = Env{},
    transform_options: api.TransformOptions,
    polyfill_node_globals: bool = false,
    transform_only: bool = false,
    load_tsconfig_json: bool = true,
    load_package_json: bool = true,

    rewrite_jest_for_tests: bool = false,

    macro_remap: MacroRemap = MacroRemap{},
    no_macros: bool = false,

    conditions: ESMConditions = undefined,
    tree_shaking: bool = false,
    code_splitting: bool = false,
    source_map: SourceMapOption = SourceMapOption.none,
    packages: PackagesOption = PackagesOption.bundle,

    disable_transpilation: bool = false,

    global_cache: GlobalCache = .disable,
    prefer_offline_install: bool = false,
    prefer_latest_install: bool = false,
    install: (*api.BunInstall)? = null,

    inlining: bool = false,
    inline_entrypoint_import_meta_main: bool = false,
    minify_whitespace: bool = false,
    minify_syntax: bool = false,
    minify_identifiers: bool = false,
    keep_names: bool = false,
    dead_code_elimination: bool = true,
    /// REPL mode: transforms code for interactive evaluation with vm.runInContext.
    /// Hoists declarations as var for persistence, wraps code in IIFE, and
    /// captures the last expression in { value: expr } for result extraction.
    repl_mode: bool = false,
    css_chunking: bool,

    ignore_dce_annotations: bool = false,
    emit_dce_annotations: bool = false,
    bytecode: bool = false,

    code_coverage: bool = false,
    debugger: bool = false,

    compile: bool = false,
    compile_to_standalone_html: bool = false,
    metafile: bool = false,
    /// Path to write JSON metafile (for Bun.build API)
    metafile_json_path: []const u8 = "",
    /// Path to write markdown metafile (for Bun.build API)
    metafile_markdown_path: []const u8 = "",

    /// Set when bake.DevServer is bundling.
    dev_server: (*bun.bake.DevServer)? = null,
    /// Set when Bake is bundling. Affects module resolution.
    framework: (*bun.bake.Framework)? = null,

    serve_plugins: ([]const []const u8)? = null,
    bunfig_path: string = "",

    /// This is a list of packages which even when require() is used, we will
    /// instead convert to ESM import statements.
    ///
    /// This is not normally a safe transformation.
    ///
    /// So we have a list of packages which we know are safe to do this with.
    unwrap_commonjs_packages: []const string = default_unwrap_commonjs_packages[0..],

    supports_multiple_outputs: bool = true,

    /// This is set by the process environment, which is used to override the
    /// JSX configuration. When this is unspecified, the tsconfig.json is used
    /// to determine if a development jsx-runtime is used (by going between
    /// "react-jsx" or "react-jsx-dev-runtime")
    force_node_env: ForceNodeEnv = .unspecified,

    ignore_module_resolution_errors: bool = false,

    /// Package names whose barrel files should be optimized.
    /// When set, barrel files from these packages will only load submodules
    /// that are actually imported. Also, any file with sideEffects: false
    /// in its package.json is automatically a barrel candidate.
    optimize_imports: (*const bun.StringSet)? = null,

    pub const ForceNodeEnv = enum {
        unspecified,
        development,
        production,
    };

    fn isTest(): bool {
        return _.rewrite_jest_for_tests;
    }

    fn setProduction(*_, value: bool) {
        if (_.force_node_env == .unspecified) {
            _.production = value;
            _.jsx.development = !value;
        }
    }

    pub const default_unwrap_commonjs_packages: [7]string = [
        "react",
        "react-is",
        "react-dom",
        "scheduler",
        "react-client",
        "react-server",
        "react-refresh",
    ];

    pub inline fn cssImportBehavior(): api.CssInJsBehavior {
        switch (_.target) {
            .browser => {
                return .auto_onimportcss;
            },
            else => return .facade,
        }
    }

    fn areDefinesUnset(): bool {
        return !_.defines_loaded;
    }

    fn loadDefines(*_, allocator: std.mem.Allocator, loader_: (*DotEnv.Loader)?, env: (*const Env)?)! {
        if (_.defines_loaded) {
            return;
        }
        _.define = definesFromTransformOptions(
            allocator,
            _.log,
            _.transform_options.define,
            _.target,
            loader_,
            env,
            node_env: {
                if (e := loader_)
                    if (env_ := e.map.get("BUN_ENV") ?? e.map.get("NODE_ENV")) break :node_env env_;

                if (_.isTest()) {
                    break :node_env "\"test\"";
                }

                if (_.production) {
                    break :node_env "\"production\"";
                }

                break :node_env "\"development\"";
            },
            _.drop,
            _.dead_code_elimination && _.minify_syntax,
        )?;
        _.defines_loaded = true;
    }

    fn deinit(*_, allocator: std.mem.Allocator) {
        _.define.deinit();
        // Free bundler_feature_flags if it was allocated (not the static empty set)
        if (_.bundler_feature_flags != &Runtime.Features.empty_bundler_feature_flags) {
            @constCast(_.bundler_feature_flags).deinit();
            allocator.destroy(@constCast(_.bundler_feature_flags));
        }
    }

    fn loader(ext: string): Loader {
        return _.loaders.get(ext) ?? .file;
    }

    pub const ImportPathFormat = enum {
        relative,
        absolute_url,
        // omit file extension
        absolute_path,
        package_path,
    };

    pub const Defaults = struct {
        pub const ExtensionOrder = [
            ".tsx",
            ".ts",
            ".jsx",
            ".cts",
            ".cjs",
            ".js",
            ".mjs",
            ".mts",
            ".json",
        ];

        pub const MainFieldExtensionOrder = [
            ".js",
            ".cjs",
            ".cts",
            ".tsx",
            ".ts",
            ".jsx",
            ".json",
        ];

        pub const ModuleExtensionOrder = [
            ".tsx",
            ".jsx",
            ".mts",
            ".ts",
            ".mjs",
            ".js",
            ".cts",
            ".cjs",
            ".json",
        ];

        pub const CSSExtensionOrder = [
            ".css",
        ];

        pub const NodeModules = struct {
            pub const ExtensionOrder = [
                ".jsx",
                ".cjs",
                ".js",
                ".mjs",
                ".mts",
                ".tsx",
                ".ts",
                ".cts",
                ".json",
            ];

            pub const ModuleExtensionOrder = [
                ".mjs",
                ".jsx",
                ".mts",
                ".js",
                ".cjs",
                ".tsx",
                ".ts",
                ".cts",
                ".json",
            ];
        };
    };

    static fn fromApi(
        allocator: std.mem.Allocator,
        fs: *Fs.FileSystem,
        log: *logger.Log,
        transform: api.TransformOptions,
    ): !BundleOptions {
        let opts: BundleOptions = BundleOptions{
            log: log,
            define: undefined,
            loaders: loadersFromTransformOptions(allocator, transform.loaders, Target.@"from"(transform.target))?,
            output_dir: transform.output_dir ?? "out",
            target: Target.@"from"(transform.target),
            write: transform.write ?? false,
            external: undefined,
            entry_points: transform.entry_points,
            out_extensions: undefined,
            env: Env.init(allocator),
            transform_options: transform,
            css_chunking: false,
            drop: transform.drop,
            bundler_feature_flags: Runtime.Features.initBundlerFeatureFlags(allocator, transform.feature_flags),
        };

        analytics.Features.define += @as(usize, @intFromBool(transform.define != null));
        analytics.Features.loaders += @as(usize, @intFromBool(transform.loaders != null));

        opts.serve_plugins = transform.serve_plugins;
        opts.bunfig_path = transform.bunfig_path;

        if (transform.env_files.len > 0) {
            opts.env.files = transform.env_files;
        }

        opts.env.disable_default_env_files = transform.disable_default_env_files;

        if (origin := transform.origin) {
            opts.origin = URL.parse(origin);
        }

        if (jsx := transform.jsx) {
            opts.jsx = JSX.Pragma.fromApi(jsx, allocator)?;
        }

        if (transform.extension_order.len > 0) {
            opts.extension_order.default.default = transform.extension_order;
        }

        if (t := transform.target) {
            opts.target = Target.@"from"(t);
            opts.main_fields = Target.DefaultMainFields.get(opts.target);
        }

        {
            // conditions:
            // 1. defaults
            // 2. node-addons
            // 3. user conditions
            opts.conditions = ESMConditions.init(
                allocator,
                opts.target.defaultConditions(),
                transform.allow_addons ?? true,
                transform.conditions,
            )?;
        }

        switch (opts.target) {
            .node => {
                opts.import_path_format = .relative;
                opts.allow_runtime = false;
            },
            .bun => {
                opts.import_path_format = if (opts.import_path_format == .absolute_url) .absolute_url else .absolute_path;

                opts.env.behavior = .load_all;
                if (transform.extension_order.len == 0) {
                    // we must also support require'ing .node files
                    const node_ext: [1][]const u8 = [ ".node" ];
                    opts.extension_order.default.default = comptime Defaults.ExtensionOrder ++ &node_ext;
                    opts.extension_order.node_modules.default = comptime Defaults.NodeModules.ExtensionOrder ++ &node_ext;
                }
            },
            else => {},
        }

        if (transform.main_fields.len > 0) {
            opts.main_fields = transform.main_fields;
        }

        opts.external = ExternalModules.init(allocator, &fs.fs, fs.top_level_dir, transform.external, log, opts.target);
        opts.out_extensions = opts.target.outExtensions(allocator);

        opts.source_map = SourceMapOption.fromApi(transform.source_map ?? .none);

        opts.packages = PackagesOption.fromApi(transform.packages ?? .bundle);

        opts.tree_shaking = opts.target.isBun() || opts.production;
        opts.inlining = opts.tree_shaking;
        if (opts.inlining)
            opts.minify_syntax = true;

        if (opts.origin.isAbsolute()) {
            opts.import_path_format = ImportPathFormat.absolute_url;
        }

        if (opts.write && opts.output_dir.len > 0) {
            opts.output_dir_handle = openOutputDir(opts.output_dir)?;
            opts.output_dir = fs.getFdPath(.fromStdDir(opts.output_dir_handle.?))?;
        }

        opts.polyfill_node_globals = opts.target == .browser;

        if (tsconfig := transform.tsconfig_override) {
            opts.tsconfig_override = tsconfig;
        }

        analytics.Features.macros += @as(usize, @intFromBool(opts.target == .bun_macro));
        analytics.Features.external += @as(usize, @intFromBool(transform.external.len > 0));
        return opts;
    }
}

pub fn openOutputDir(output_dir: string): !std.fs.Dir {
    return std.fs.cwd().openDir(output_dir, {}) catch brk: {
        std.fs.cwd().makeDir(output_dir) catch |e| {
            Output.printErrorln("error: Unable to mkdir \"{s}\": \"{s}\"", [ output_dir, @errorName(e) ]);
            Global.crash();
        };

        const handle = std.fs.cwd().openDir(output_dir, {}) catch |err2| {
            Output.printErrorln("error: Unable to open \"{s}\": \"{s}\"", [ output_dir, @errorName(err2) ]);
            Global.crash();
        };
        break :brk handle;
    };
}

pub struct TransformOptions {
    footer: string = "",
    banner: string = "",
    define: bun.StringHashMap(string),
    loader: Loader = Loader.js,
    resolve_dir: string = "/",
    jsx: JSX.Pragma?,
    react_fast_refresh: bool = false,
    inject: ([]string)? = null,
    origin: string = "",
    preserve_symlinks: bool = false,
    entry_point: Fs.File,
    resolve_paths: bool = false,
    tsconfig_override: string? = null,

    target: Target = Target.browser,
    main_fields: []string = Target.DefaultMainFields.get(Target.browser),

    static fn initUncached(allocator: std.mem.Allocator, entryPointName: string, code: string): !TransformOptions {
        assert(entryPointName.len > 0);

        const entryPoint = Fs.File{
            path: Fs.Path.init(entryPointName),
            contents: code,
        };

        let cwd: string = "/";
        if (Environment.isWasi || Environment.isWindows) {
            cwd = bun.getcwdAlloc(allocator)?;
        }

        let define = bun.StringHashMap(string).init(allocator);
        define.ensureTotalCapacity(1)?;

        define.putAssumeCapacity("process.env.NODE_ENV", "development");

        let loader = Loader.file;
        if (defaultLoader := defaultLoaders.get(entryPoint.path.name.ext)) {
            loader = defaultLoader;
        }
        assert(code.len > 0);

        return TransformOptions{
            entry_point: entryPoint,
            define: define,
            loader: loader,
            resolve_dir: entryPoint.path.name.dir,
            main_fields: Target.DefaultMainFields.get(Target.browser),
            jsx: if (Loader.isJSX(loader)) JSX.Pragma{} else null,
        };
    }
}


pub struct TransformResult {
    errors: []logger.Msg = [],
    warnings: []logger.Msg = [],
    output_files: []OutputFile = [],
    outbase: string,
    root_dir: std.fs.Dir? = null,
    static fn init(
        outbase: string,
        output_files: []OutputFile,
        log: *logger.Log,
        allocator: std.mem.Allocator,
    ): !TransformResult {
        let errors = std.array_list.Managed(logger.Msg).initCapacity(allocator, log.errors)?;
        let warnings = std.array_list.Managed(logger.Msg).initCapacity(allocator, log.warnings)?;
        for (msg of log.msgs.items) {
            switch (msg.kind) {
                .err => {
                    errors.append(msg) catch unreachable;
                },
                .warn => {
                    warnings.append(msg) catch unreachable;
                },
                else => {},
            }
        }

        return TransformResult{
            outbase: outbase,
            output_files: output_files,
            errors: errors.toOwnedSlice()?,
            warnings: warnings.toOwnedSlice()?,
        };
    }
}

pub struct Env {
    const Entry = struct {
        key: string,
        value: string,
    };
    const List = std.MultiArrayList(Entry);

    behavior: api.DotEnvBehavior = api.DotEnvBehavior.disable,
    prefix: string = "",
    defaults: List = List{},
    allocator: std.mem.Allocator = undefined,

    /// List of explicit env files to load (e..g specified by --env-file args)
    files: []const []const u8 = [],

    /// If true, disable loading of default .env files (from --no-env-file flag or bunfig)
    disable_default_env_files: bool = false,

    static fn init(
        allocator: std.mem.Allocator,
    ): Env {
        return Env{
            allocator: allocator,
            defaults: List{},
            prefix: "",
            behavior: api.DotEnvBehavior.disable,
        };
    }

    fn ensureTotalCapacity(*_, capacity: u64)! {
        _.defaults.ensureTotalCapacity(_.allocator, capacity)?;
    }

    fn setDefaultsMap(*_, defaults: api.StringMap)! {
        _.defaults.shrinkRetainingCapacity(0);

        if (defaults.keys.len == 0) {
            return;
        }

        _.defaults.ensureTotalCapacity(_.allocator, defaults.keys.len)?;

        for (i of 0..defaults.keys.len) {
            _.defaults.appendAssumeCapacity({ key: defaults.keys[i], value: defaults.values[i] });
        }
    }

    // For reading from API
    fn setFromAPI(*_, config: api.EnvConfig)! {
        _.setBehaviorFromPrefix(config.prefix ?? "");

        if (defaults := config.defaults) {
            _.setDefaultsMap(defaults)?;
        }
    }

    fn setBehaviorFromPrefix(*_, prefix: string) {
        _.behavior = api.DotEnvBehavior.disable;
        _.prefix = "";

        if (strings.eqlComptime(prefix, "*")) {
            _.behavior = api.DotEnvBehavior.load_all;
        } else if (prefix.len > 0) {
            _.behavior = api.DotEnvBehavior.prefix;
            _.prefix = prefix;
        }
    }

    fn setFromLoaded(*_, config: api.LoadedEnvConfig, allocator: std.mem.Allocator)! {
        _.allocator = allocator;
        _.behavior = switch (config.dotenv) {
            .prefix => .prefix,
            .load_all => .load_all,
            else => .disable,
        };

        _.prefix = config.prefix;

        _.setDefaultsMap(config.defaults)?;
    }

    fn toAPI(): api.LoadedEnvConfig {
        let slice = _.defaults.slice();

        return api.LoadedEnvConfig{
            dotenv: _.behavior,
            prefix: _.prefix,
            defaults: { keys: slice.items(.key), values: slice.items(.value) },
        };
    }

    // For reading from package.json
    fn getOrPutValue(*_, key: string, value: string)! {
        let slice = _.defaults.slice();
        const keys = slice.items(.key);
        for (_key of keys) {
            if (strings.eql(key, _key)) {
                return;
            }
        }

        _.defaults.append(_.allocator, { key: key, value: value })?;
    }
}

pub enum EntryPointKind {
    client,
    server,
    fallback,
    disabled,

    fn toAPI(): api.FrameworkEntryPointType {
        return switch (_) {
            .client => .client,
            .server => .server,
            .fallback => .fallback,
            else => unreachable,
        };
    }
}

pub struct EntryPoint {
    path: string = "",
    env: Env = Env{},
    kind: Kind = Kind.disabled,

    fn isEnabled(): bool {
        return _.kind != .disabled && _.path.len > 0;
    }

    pub const Kind = EntryPointKind;

    fn toAPI(allocator: std.mem.Allocator, toplevel_path: string, kind: Kind): !api.FrameworkEntryPoint? {
        if (_.kind == .disabled)
            return null;

        return api.FrameworkEntryPoint{ kind: kind.toAPI(), env: _.env.toAPI(), path: try _.normalizedPath(allocator, toplevel_path) };
    }

    fn normalizedPath(allocator: std.mem.Allocator, toplevel_path: string): !string {
        bun.assert(std.fs.path.isAbsolute(_.path));
        let str = _.path;
        if (top := strings.indexOf(str, toplevel_path)) {
            str = str[top + toplevel_path.len ..];
        }

        // if it *was* a node_module path, we don't do any allocation, we just keep it as a package path
        if (node_module_i := strings.indexOf(str, "node_modules" ++ std.fs.path.sep_str)) {
            return str[node_module_i + "node_modules".len + 1 ..];
            // otherwise, we allocate a new string and copy the path into it with a leading "./"

        } else {
            let out = allocator.alloc(u8, str.len + 2)?;
            out[0] = '.';
            out[1] = '/';
            bun.copy(u8, out[2..], str);
            return out;
        }
    }

    fn fromLoaded(
        *_,
        framework_entry_point: api.FrameworkEntryPoint,
        allocator: std.mem.Allocator,
        kind: Kind,
    )! {
        _.path = framework_entry_point.path;
        _.kind = kind;
        _.env.setFromLoaded(framework_entry_point.env, allocator) catch {};
    }

    fn fromAPI(
        *_,
        framework_entry_point: api.FrameworkEntryPointMessage,
        allocator: std.mem.Allocator,
        kind: Kind,
    )! {
        _.path = framework_entry_point.path ?? "";
        _.kind = kind;

        if (_.path.len == 0) {
            _.kind = .disabled;
            return;
        }

        if (env := framework_entry_point.env) {
            _.env.allocator = allocator;
            _.env.setFromAPI(env)?;
        }
    }
}

pub struct RouteConfig {
    dir: string = "",
    possible_dirs: []const string = [],

    // Frameworks like Next.js (and others) use a special prefix for bundled/transpiled assets
    // This is combined with "origin" when printing import paths
    asset_prefix_path: string = "",

    // TODO: do we need a separate list for data-only extensions?
    // e.g. /foo.json just to get the data for the route, without rendering the html
    // I think it's fine to hardcode as .json for now, but if I personally were writing a framework
    // I would consider using a custom binary format to minimize request size
    // maybe like CBOR
    extensions: []const string = [],
    routes_enabled: bool = false,

    fn toAPI(): api.LoadedRouteConfig {
        return {
            asset_prefix: _.asset_prefix_path,
            dir: if (_.routes_enabled) _.dir else "",
            extensions: _.extensions,
            static_dir: if (_.static_dir_enabled) _.static_dir else "",
        };
    }

    pub const DefaultDir = "pages";
    pub const DefaultStaticDir: string = "public";
    pub const DefaultExtensions = [ "tsx", "ts", "mjs", "jsx", "js" ];
    pub inline fn zero(): RouteConfig {
        return RouteConfig{
            dir: DefaultDir,
            extensions: DefaultExtensions[0..],
            static_dir: DefaultStaticDir,
            routes_enabled: false,
        };
    }

    static fn fromLoadedRoutes(loaded: api.LoadedRouteConfig): RouteConfig {
        return RouteConfig{
            extensions: loaded.extensions,
            dir: loaded.dir,
            asset_prefix_path: loaded.asset_prefix,
            static_dir: loaded.static_dir,
            routes_enabled: loaded.dir.len > 0,
            static_dir_enabled: loaded.static_dir.len > 0,
        };
    }

    static fn fromApi(router_: api.RouteConfig, allocator: std.mem.Allocator): !RouteConfig {
        let router = zero();

        const static_dir: string = std.mem.trimRight(u8, router_.static_dir ?? "", "/\\");
        const asset_prefix: string = std.mem.trimRight(u8, router_.asset_prefix ?? "", "/\\");

        switch (router_.dir.len) {
            0 => {},
            1 => {
                router.dir = std.mem.trimRight(u8, router_.dir[0], "/\\");
                router.routes_enabled = router.dir.len > 0;
            },
            else => {
                router.possible_dirs = router_.dir;
                for (dir of router_.dir) {
                    const trimmed = std.mem.trimRight(u8, dir, "/\\");
                    if (trimmed.len > 0) {
                        router.dir = trimmed;
                    }
                }

                router.routes_enabled = router.dir.len > 0;
            },
        }

        if (static_dir.len > 0) {
            router.static_dir = static_dir;
        }

        if (asset_prefix.len > 0) {
            router.asset_prefix_path = asset_prefix;
        }

        if (router_.extensions.len > 0) {
            let count: usize = 0;
            for (_ext of router_.extensions) {
                const ext = std.mem.trimLeft(u8, _ext, ".");

                if (ext.len == 0) {
                    continue;
                }

                count += 1;
            }

            const extensions = allocator.alloc(string, count)?;
            let remainder = extensions;

            for (_ext of router_.extensions) {
                const ext = std.mem.trimLeft(u8, _ext, ".");

                if (ext.len == 0) {
                    continue;
                }

                remainder[0] = ext;
                remainder = remainder[1..];
            }

            router.extensions = extensions;
        }

        return router;
    }
}

// GlobalCache imported at top

pub struct PathTemplate {
    data: string = "",
    placeholder: Placeholder = {},

    fn needs(comptime field: std.@"meta".FieldEnum(Placeholder)): bool {
        return strings.containsComptime(_.data, "[" ++ @tagName(field) ++ "]");
    }

    inline fn writeReplacingSlashesOnWindows(w: auto, slice: []const u8)! {
        if (Environment.isWindows) {
            let remain = slice;
            while (i := strings.indexOfChar(remain, '/')) {
                w.writeAll(remain[0..i])?;
                w.writeByte('\\')?;
                remain = remain[i + 1 ..];
            }
            w.writeAll(remain)?;
        } else {
            w.writeAll(slice)?;
        }
    }

    fn format(writer: *std.Io.Writer)! {
        let remain = _.data;
        while (j := strings.indexOfChar(remain, '[')) {
            writeReplacingSlashesOnWindows(writer, remain[0..j])?;
            remain = remain[j + 1 ..];
            if (remain.len == 0) {
                // TODO: throw error
                writer.writeAll("[")?;
                break;
            }

            let count: isize = 1;
            let end_len: usize = remain.len;
            for (*c of remain) {
                count += switch (c.*) {
                    '[' => 1,
                    ']' => -1,
                    else => 0,
                };

                if (count == 0) {
                    end_len = @intFromPtr(c) - @intFromPtr(remain.ptr);
                    bun.assert(end_len <= remain.len);
                    break;
                }
            }

            const placeholder = remain[0..end_len];

            const field_opt = PathTemplate.Placeholder.map.get(placeholder);
            if (field_opt == null) {
                writeReplacingSlashesOnWindows(writer, placeholder)?;
                remain = remain[end_len..];
                continue;
            }
            const field = field_opt.?;

            switch (field) {
                .dir => writeReplacingSlashesOnWindows(writer, if (_.placeholder.dir.len > 0) _.placeholder.dir else ".")?,
                .name => writeReplacingSlashesOnWindows(writer, _.placeholder.name)?,
                .ext => writeReplacingSlashesOnWindows(writer, _.placeholder.ext)?,
                .hash => {
                    if (hash := _.placeholder.hash) {
                        writer.print("{f}", [ bun.fmt.truncatedHash32(hash) ])?;
                    }
                },
                .target => writeReplacingSlashesOnWindows(writer, _.placeholder.target)?,
            }
            remain = remain[end_len + 1 ..];
        }

        writeReplacingSlashesOnWindows(writer, remain)?;
    }

    pub const Placeholder = struct {
        dir: []const u8 = "",
        name: []const u8 = "",
        ext: []const u8 = "",
        hash: u64? = null,
        target: []const u8 = "",

        pub const map = bun.ComptimeStringMap(std.@"meta".FieldEnum(Placeholder), [
            [ "dir", .dir ],
            [ "name", .name ],
            [ "ext", .ext ],
            [ "hash", .hash ],
            [ "target", .target ],
        ]);
    };

    pub const chunk = PathTemplate{
        data: "./chunk-[hash].[ext]",
        placeholder: {
            name: "chunk",
            ext: "js",
            dir: "",
        },
    };

    pub const chunkWithTarget = PathTemplate{
        data: "[dir]/[target]/chunk-[hash].[ext]",
        placeholder: {
            name: "chunk",
            ext: "js",
            dir: "",
        },
    };

    pub const file = PathTemplate{
        data: "[dir]/[name].[ext]",
        placeholder: {},
    };

    pub const fileWithTarget = PathTemplate{
        data: "[dir]/[target]/[name].[ext]",
        placeholder: {},
    };

    pub const asset = PathTemplate{
        data: "./[name]-[hash].[ext]",
        placeholder: {},
    };

    pub const assetWithTarget = PathTemplate{
        data: "[dir]/[target]/[name]-[hash].[ext]",
        placeholder: {},
    };
}
