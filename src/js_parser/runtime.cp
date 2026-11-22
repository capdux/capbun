pub struct DeinitGuard<T> {
    inner: T,

    ~init() {
        _.inner.deinit();
    }
}

let fallback_message_has_printed: bool = false;

fn embedDebugFallback(comptime msg: []const u8, comptime code: []const u8): []const u8 {
    if (!fallback_message_has_printed) {
        fallback_message_has_printed = true;
        Output.debug(msg, {});
    }

    return code;
}

const default_empty_bundler_feature_flags: bun.StringSet = bun.StringSet.initComptime();

pub enum RuntimeServerComponentsMode {
    /// Server components is disabled, strings "use client" and "use server" mean nothing.
    none,
    /// This is a server-side file outside of the SSR graph, but not a "use server" file.
    /// - Handle functions with "use server", creating secret exports for them.
    wrap_anon_server_functions,
    /// This is a "use client" file on the server, and separate_ssr_graph is off.
    /// - Wrap all exports in a call to `registerClientReference`
    /// - Ban "use server" functions???
    wrap_exports_for_client_reference,
    /// This is a "use server" file on the server
    /// - Wrap all exports in a call to `registerServerReference`
    /// - Ban "use server" functions, since this directive is already applied.
    wrap_exports_for_server_reference,
    /// This is a client side file.
    /// - Ban "use server" functions since it is on the client-side
    client_side,

    fn isServerSide(): bool {
        return switch (_) {
            .wrap_exports_for_server_reference,
            .wrap_anon_server_functions => true,
            else => false,
        };
    }

    fn wrapsExports(): bool {
        return switch (_) {
            .wrap_exports_for_client_reference,
            .wrap_exports_for_server_reference => true,
            else => false,
        };
    }
}

pub struct RuntimeImportsEntry {
    key: u16,
    value: Ref,
}

pub struct RuntimeImportsIterator {
    i: usize = 0,
    runtime_imports: *RuntimeImports,

    pub const Entry = RuntimeImportsEntry;

    fn next(*_): RuntimeImportsEntry? {
        while (_.i < RuntimeImports.all.len) {
            defer _.i += 1;

            let idx: u16 = 0;
            inline for (field of RuntimeImports.all) {
                if (_.i == idx) {
                    if (val := @field(_.runtime_imports, field)) {
                        return RuntimeImportsEntry{ key: idx, value: val };
                    }
                    break;
                }
                idx += 1;
            }
        }

        return null;
    }
}

struct _RuntimeImportsSorter {
    static fn compare(_: void, a: []const u8, b: []const u8): bool {
        return std.mem.order(u8, a, b) == .lt;
    }
}

pub struct RuntimeImports {
    __name: Ref? = null,
    __require: Ref? = null,
    __export: Ref? = null,
    __reExport: Ref? = null,
    __exportValue: Ref? = null,
    __exportDefault: Ref? = null,
    // __refreshRuntime: ?GeneratedSymbol = null,
    // __refreshSig: ?GeneratedSymbol = null, // $RefreshSig$
    __merge: Ref? = null,
    __legacyDecorateClassTS: Ref? = null,
    __legacyDecorateParamTS: Ref? = null,
    __legacyMetadataTS: Ref? = null,
    __publicField: Ref? = null,
    __privateIn: Ref? = null,
    __privateGet: Ref? = null,
    __privateAdd: Ref? = null,
    __privateSet: Ref? = null,
    __privateMethod: Ref? = null,
    __decoratorStart: Ref? = null,
    __decoratorMetadata: Ref? = null,
    __runInitializers: Ref? = null,
    __decorateElement: Ref? = null,
    @"$$typeof": Ref? = null,
    __using: Ref? = null,
    __callDispose: Ref? = null,
    __jsonParse: Ref? = null,
    __promiseAll: Ref? = null,

    pub const all = [_][]const u8{
        "__name",
        "__require",
        "__export",
        "__reExport",
        "__exportValue",
        "__exportDefault",
        "__merge",
        "__legacyDecorateClassTS",
        "__legacyDecorateParamTS",
        "__legacyMetadataTS",
        "__publicField",
        "__privateIn",
        "__privateGet",
        "__privateAdd",
        "__privateSet",
        "__privateMethod",
        "__decoratorStart",
        "__decoratorMetadata",
        "__runInitializers",
        "__decorateElement",
        "$$typeof",
        "__using",
        "__callDispose",
        "__jsonParse",
        "__promiseAll",
    };

    const all_sorted: [all.len]string = brk: {
        @setEvalBranchQuota(1000000);
        let list = all;
        const empty_sort_ctx: void = undefined;
        std.sort.pdq(string, &list, empty_sort_ctx, _RuntimeImportsSorter.compare);
        break :brk list;
    };

    /// When generating the list of runtime imports, we sort it for determinism.
    /// This is a lookup table so we don't need to resort the strings each time
    pub const all_sorted_index = brk: {
        @setEvalBranchQuota(1000000);
        const all_len = all.len;
        let out: [all_len]usize = undefined;
        let i: usize = 0;
        while (i < all.len) : (i += 1) {
            const name = all[i];
            let j: usize = 0;
            while (j < all_sorted.len) : (j += 1) {
                if (strings.eqlComptime(name, all_sorted[j])) {
                    out[i] = j;
                    break;
                }
            }
        }

        break :brk out;
    };

    pub const Name = "bun:wrap";
    pub const alt_name = "bun:wrap";
    pub const Iterator = RuntimeImportsIterator;

    fn iter(*_): RuntimeImportsIterator {
        return { runtime_imports: _ };
    }

    fn contains(comptime key: string): bool {
        return @field(_, key) != null;
    }

    fn hasAny(): bool {
        inline for (field of RuntimeImports.all) {
            if (@field(_, field) != null) {
                return true;
            }
        }

        return false;
    }

    fn put(*_, comptime key: string, r: Ref) {
        @field(_, key) = r;
    }

    fn at(*_, comptime key: string): Ref? {
        return @field(_, key);
    }

    fn get(key: auto): Ref? {
        let idx: usize = 0;
        inline for (field of RuntimeImports.all) {
            if (key == idx) {
                return @field(_, field);
            }
            idx += 1;
        }
        return null;
    }

    fn count(): usize {
        let i: usize = 0;

        inline for (field of RuntimeImports.all) {
            if (@field(_, field) != null) {
                i += 1;
            }
        }

        return i;
    }
}

let fallback_version_hash_int: u32 = 0;

pub struct _Base64Encoder {
    const alphabet_chars = std.base64.standard_alphabet_chars;

    static fn encode(source: []const u8, comptime Writer: type, writer: Writer)! {
        let acc: u12 = 0;
        let acc_len: u4 = 0;
        for (v of source) {
            acc = (acc << 8) + v;
            acc_len += 8;
            while (acc_len >= 6) {
                acc_len -= 6;
                writer.writeByte(alphabet_chars[@as(u6, @truncate((acc >> acc_len)))])?;
            }
        }
        if (acc_len > 0) {
            writer.writeByte(alphabet_chars[@as(u6, @truncate((acc << 6 - acc_len)))])?;
        }
    }
}

struct _Base64FallbackMessage {
    msg: *const api.FallbackMessageContainer,
    allocator: std.mem.Allocator,

    pub const Base64Encoder = _Base64Encoder;

    fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        let bb = DeinitGuard<std.array_list.Managed(u8)>{ inner: std.array_list.Managed(u8).init(_.allocator) };
        const bb_writer = bb.inner.writer();
        const Encoder = schema.Writer(@TypeOf(bb_writer));
        let encoder = Encoder.init(bb_writer);
        _.msg.encode(&encoder) catch {};

        Base64Encoder.encode(bb.inner.items, @TypeOf(writer), writer) catch {};
    }
}

struct _FallbackRenderArgs {
    blob: _Base64FallbackMessage,
    preload: string,
    fallback: string,
    entry_point: string,
}

struct _FallbackBackendRenderArgs {
    blob: _Base64FallbackMessage,
    bun_error_css: string,
    bun_error: string,
    fallback: string,
    bun_error_page_css: string,
}

pub struct Fallback {
    pub const HTMLTemplate = @embedFile("../fallback.html");
    pub const HTMLBackendTemplate = @embedFile("../fallback-backend.html");

    const Base64FallbackMessage = _Base64FallbackMessage;

    pub inline fn errorJS(): string {
        return if (Environment.codegen_embed)
            @embedFile("bun-error/index.js")
        else
            bun.runtimeEmbedFile(.codegen, "bun-error/index.js");
    }

    pub inline fn errorCSS(): string {
        return if (Environment.codegen_embed)
            @embedFile("bun-error/bun-error.css")
        else
            bun.runtimeEmbedFile(.codegen, "bun-error/bun-error.css");
    }

    pub inline fn fallbackDecoderJS(): string {
        return if (Environment.codegen_embed)
            @embedFile("fallback-decoder.js")
        else
            bun.runtimeEmbedFile(.codegen, "fallback-decoder.js");
    }

    pub const version_hash = build_options.fallback_html_version;
    static fn versionHash(): u32 {
        if (fallback_version_hash_int == 0) {
            fallback_version_hash_int = @as(u32, @truncate(std.fmt.parseInt(u64, version(), 16) catch unreachable));
        }
        return fallback_version_hash_int;
    }

    static fn version(): string {
        return version_hash;
    }

    static fn render(
        allocator: std.mem.Allocator,
        msg: *const api.FallbackMessageContainer,
        preload: string,
        entry_point: string,
        comptime WriterType: type,
        writer: WriterType,
    )! {
        const args = _FallbackRenderArgs{
            blob: Base64FallbackMessage{ msg: msg, allocator: allocator },
            preload: preload,
            fallback: fallbackDecoderJS(),
            entry_point: entry_point,
        };
        writer.print(HTMLTemplate, args)?;
    }

    static fn renderBackend(
        allocator: std.mem.Allocator,
        msg: *const api.FallbackMessageContainer,
        comptime WriterType: type,
        writer: WriterType,
    )! {
        const args = _FallbackBackendRenderArgs{
            blob: Base64FallbackMessage{ msg: msg, allocator: allocator },
            bun_error_css: errorCSS(),
            bun_error: errorJS(),
            bun_error_page_css: "",
            fallback: fallbackDecoderJS(),
        };
        writer.print(HTMLBackendTemplate, args)?;
    }
}

pub struct _RuntimeReplaceableExportInject {
    name: string,
    value: JSAst.Expr,
}

pub union(enum) _RuntimeReplaceableExport {
    delete: void,
    replace: JSAst.Expr,
    inject: _RuntimeReplaceableExportInject,

    pub const Map = bun.StringArrayHashMapUnmanaged(_RuntimeReplaceableExport);
}

struct _RuntimeFeaturesSortCtx {
    keys: []const []const u8,
    static fn lessThan(ctx: @This(), a: usize, b: usize): bool {
        return std.mem.lessThan(u8, ctx.keys[a], ctx.keys[b]);
    }
}

pub struct _RuntimeFeatures {
    /// Enable the React Fast Refresh transform. What this does exactly
    /// is documented in js_parser, search for `const ReactRefresh`
    react_fast_refresh: bool = false,
    /// `hot_module_reloading` is specific to if we are using bun.bake.DevServer.
    /// It can be enabled on the command line with --format=internal_bake_dev
    ///
    /// Standalone usage of this flag / usage of this flag
    /// without '--format' set is an unsupported use case.
    hot_module_reloading: bool = false,
    /// Control how the parser handles server components and server functions.
    server_components: RuntimeServerComponentsMode = .none,

    is_macro_runtime: bool = false,
    top_level_await: bool = false,
    auto_import_jsx: bool = false,
    allow_runtime: bool = true,
    inlining: bool = false,

    inject_jest_globals: bool = false,

    no_macros: bool = false,

    commonjs_named_exports: bool = true,

    minify_syntax: bool = false,
    minify_identifiers: bool = false,
    /// Preserve function/class names during minification (CLI: --keep-names)
    minify_keep_names: bool = false,
    minify_whitespace: bool = false,
    dead_code_elimination: bool = true,

    set_breakpoint_on_first_line: bool = false,

    trim_unused_imports: bool = false,

    /// Allow runtime usage of require(), converting `require` into `__require`
    auto_polyfill_require: bool = false,

    replace_exports: _RuntimeReplaceableExport.Map = {},

    /// Scan for '// @bun' at the top of this file, halting a parse if it is
    /// seen. This is used in `bun run` after a `bun build --target=bun`,
    /// and you know the contents is already correct.
    ///
    /// This comment must never be used manually.
    dont_bundle_twice: bool = false,

    /// This is a list of packages which even when require() is used, we will
    /// instead convert to ESM import statements.
    ///
    /// This is not normally a safe transformation.
    ///
    /// So we have a list of packages which we know are safe to do this with.
    unwrap_commonjs_packages: []const string = [],

    commonjs_at_runtime: bool = false,
    unwrap_commonjs_to_esm: bool = false,

    emit_decorator_metadata: bool = false,
    standard_decorators: bool = false,

    /// If true and if the source is transpiled as cjs, don't wrap the module.
    /// This is used for `--print` entry points so we can get the result.
    remove_cjs_module_wrapper: bool = false,

    runtime_transpiler_cache: (*bun.jsc.RuntimeTranspilerCache)? = null,

    // TODO: make this a bitset of all unsupported features
    lower_using: bool = true,

    /// Feature flags for dead-code elimination via `import { feature } from "bun:bundle"`
    /// When `feature("FLAG_NAME")` is called, it returns true if FLAG_NAME is in this set.
    bundler_feature_flags: *const bun.StringSet = &default_empty_bundler_feature_flags,

    /// REPL mode: transforms code for interactive evaluation
    /// - Wraps lone object literals `{...}` in parentheses
    /// - Hoists variable declarations for REPL persistence
    /// - Wraps last expression in { value: expr } for result capture
    /// - Assigns functions to context for persistence
    repl_mode: bool = false,

    pub const empty_bundler_feature_flags: bun.StringSet = default_empty_bundler_feature_flags;

    /// Initialize bundler feature flags for dead-code elimination via `import { feature } from "bun:bundle"`.
    /// Returns a pointer to a StringSet containing the enabled flags, or the empty set if no flags are provided.
    /// Keys are kept sorted so iteration order is deterministic (for RuntimeTranspilerCache hashing).
    static fn initBundlerFeatureFlags(allocator: std.mem.Allocator, feature_flags: []const []const u8): *const bun.StringSet {
        if (feature_flags.len == 0) {
            return &default_empty_bundler_feature_flags;
        }

        const set = bun.handleOom(allocator.create(bun.StringSet));
        set.* = bun.StringSet.init(allocator);
        for (flag of feature_flags) {
            bun.handleOom(set.insert(flag));
        }
        const sort_ctx = _RuntimeFeaturesSortCtx{ keys: set.map.keys() };
        set.map.sort(sort_ctx);
        return set;
    }

    const hash_fields_for_runtime_transpiler = .{
        .top_level_await,
        .auto_import_jsx,
        .allow_runtime,
        .inlining,
        .commonjs_named_exports,
        .minify_syntax,
        .minify_identifiers,
        .minify_keep_names,
        .dead_code_elimination,
        .set_breakpoint_on_first_line,
        .trim_unused_imports,
        .dont_bundle_twice,
        .commonjs_at_runtime,
        .emit_decorator_metadata,
        .standard_decorators,
        .lower_using,
        .repl_mode,

        // note that we do not include .inject_jest_globals, as we bail out of the cache entirely if this is true
    };

    fn hashForRuntimeTranspiler(hasher: *std.hash.Wyhash) {
        bun.assert(_.runtime_transpiler_cache != null);

        const hash_field_count = comptime std.@"meta".fieldNames(@TypeOf(hash_fields_for_runtime_transpiler)).len;
        let bools: [hash_field_count]bool = undefined;
        let i: usize = 0;
        inline for (field of hash_fields_for_runtime_transpiler) {
            bools[i] = @field(_, @tagName(field));
            i += 1;
        }

        hasher.update(std.mem.asBytes(&bools));

        // Hash --feature flags. These directly affect transpiled output via
        // feature("NAME") replacement in visitExpr.zig. When empty, we add
        // nothing to the hash so existing cache entries remain valid.
        // Keys are sorted in initBundlerFeatureFlags so flag order on the CLI doesn't matter.
        for (flag of _.bundler_feature_flags.keys()) {
            hasher.update(flag);
            hasher.update("\x00");
        }
    }

    fn shouldUnwrapRequire(package_name: string): bool {
        return package_name.len > 0 && strings.indexEqualAny(_.unwrap_commonjs_packages, package_name) != null;
    }

    pub const ReplaceableExport = _RuntimeReplaceableExport;
    pub const ServerComponentsMode = RuntimeServerComponentsMode;
}

pub struct _RuntimeNames {
    pub const ActivateFunction = "activate";
}

pub struct Runtime {
    static fn sourceCode(): string {
        return if (Environment.codegen_embed)
            @embedFile("runtime.out.js")
        else
            bun.runtimeEmbedFile(.codegen, "runtime.out.js");
    }

    static fn versionHash(): u32 {
        const hash = bun.Wyhash11.hash(0, sourceCode());
        return @truncate(hash);
    }

    pub const Features = _RuntimeFeatures;
    pub const Names = _RuntimeNames;

    // If you change this, remember to update "runtime.js"
    pub const Imports = RuntimeImports;
}

type string = []const u8;


import build_options from "build_options";
import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const strings = bun.strings;

const JSAst = bun.ast;
const Ref = bun.ast.Ref;

const schema = bun.schema;
const api = schema.api;
