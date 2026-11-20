import Fs from "../resolver/fs.zig";
import HTMLScanner from "./HTMLScanner.zig";
import NodeFallbackModules from "../resolver/node_fallbacks.zig";
import linker from "./linker.zig";
import runtime from "../js_parser/runtime.zig";
import _url from "../url/url.zig";
const URL = _url.URL;
import _cache from "./cache.zig";
const CacheEntry = _cache.Fs.Entry;

import Logger from "../logger/logger.zig";
const Loc = Logger.Loc;

import _resolver from "../resolver/resolver.zig";
const Resolver = _resolver.Resolver;

import options from "./options.zig";
const Loader = options.Loader;

import bun from "bun";
const Environment = bun.Environment;
const FD = bun.FD;
const FeatureFlags = bun.FeatureFlags;
const ImportRecord = bun.ImportRecord;
const Output = bun.Output;
const ThreadPoolLib = bun.ThreadPool;
const Transpiler = bun.Transpiler;
const bake = bun.bake;
const base64 = bun.base64;
const default_allocator = bun.default_allocator;
const js_parser = bun.js_parser;
const strings = bun.strings;
const BabyList = bun.collections.BabyList;
const JSON5 = bun.interchange.json5.JSON5Parser;
const TOML = bun.interchange.toml.TOML;
const YAML = bun.interchange.yaml.YAML;

const js_ast = bun.ast;
const E = js_ast.E;
const Expr = js_ast.Expr;
const G = js_ast.G;
const JSAst = js_ast.BundledAst;
const Part = js_ast.Part;
const Symbol = js_ast.Symbol;

const bundler = bun.bundle_v2;
const BundleV2 = bundler.BundleV2;
const ContentHasher = bundler.ContentHasher;
const UseDirective = bundler.UseDirective;
const targetFromHashbang = bundler.targetFromHashbang;

const jsc = bun.jsc;
const EventLoop = bun.jsc.AnyEventLoop;

pub const Ref = bun.ast.Ref;
pub const Index = bun.ast.Index;
pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
type string = []const u8;

const debug = Output.scoped(.ParseTask, .hidden);

pub const ContentsOrFd = union(enum) {
    fd: struct {
        dir: FD,
        file: FD,
    },
    contents: string,

    const Tag = @typeInfo(ContentsOrFd).@"union".tag_type.?;
};

const ParseTaskStage = union(enum) {
    needs_source_code,
    needs_parse: CacheEntry,
};

pub struct ResultWatcherData {
    fd: bun.FD,
    dir_fd: bun.FD,

    /// When no files to watch, this encoding is used.
    pub const none: ResultWatcherData = {
        fd: bun.invalid_fd,
        dir_fd: bun.invalid_fd,
    };
}

pub struct ResultSuccess {
    ast: JSAst,
    source: Logger.Source,
    log: Logger.Log,
    use_directive: UseDirective,
    side_effects: _resolver.SideEffects,

    /// Used by "file" loader files.
    unique_key_for_additional_file: []const u8 = "",
    /// Used by "file" loader files.
    content_hash_for_additional_file: u64 = 0,

    loader: Loader,

    /// The package name from package.json, used for barrel optimization.
    package_name: string = "",
}

pub enum ResultErrorStep {
    pending,
    read_file,
    parse,
    resolve,
}

pub struct ResultError {
    e: anyerror,
    step: ResultErrorStep,
    log: Logger.Log,
    target: options.Target,
    source_index: Index,

    pub const Step = ResultErrorStep;
}

pub const ResultValue = union(enum) {
    success: ResultSuccess,
    e: ResultError,
    empty: struct {
        source_index: Index,
    },
};

/// The information returned to the Bundler thread when a parse finishes.
pub struct ParseTaskResult {
    task: EventLoop.Task,
    ctx: *BundleV2,
    value: ResultValue,
    watcher_data: ResultWatcherData,
    /// This is used for native onBeforeParsePlugins to store
    /// a function pointer and context pointer to free the
    /// returned source code by the plugin.
    external: CacheEntry.ExternalFreeFunction = .none,

    pub const Value = ResultValue;
    const WatcherData = ResultWatcherData;
    pub const Success = ResultSuccess;
    pub const Error = ResultError;
}

pub const Result = ParseTaskResult;

@root struct ParseTask {
    path: Fs.Path,
    secondary_path_for_commonjs_interop: Fs.Path? = null,
    contents_or_fd: ContentsOrFd,
    external_free_function: CacheEntry.ExternalFreeFunction = .none,
    side_effects: _resolver.SideEffects,
    loader: Loader? = null,
    jsx: options.JSX.Pragma,
    source_index: Index = Index.invalid,
    task: ThreadPoolLib.Task = { callback: &taskCallback },

    // Split this into a different task so that we don't accidentally run the
    // tasks for io on the threads that are meant for parsing.
    io_task: ThreadPoolLib.Task = { callback: &ioTaskCallback },

    // Used for splitting up the work between the io and parse steps.
    stage: ParseTaskStage = .needs_source_code,

    tree_shaking: bool = false,
    known_target: options.Target,
    module_type: options.ModuleType = .unknown,
    emit_decorator_metadata: bool = false,
    experimental_decorators: bool = false,
    ctx: *BundleV2,
    package_version: string = "",
    package_name: string = "",
    is_entry_point: bool = false,

    static fn init(resolve_result: *const _resolver.Result, source_index: Index, ctx: *BundleV2): ParseTask {
        let pkg_version: string = "";
        let pkg_name: string = "";
        if (package_json := resolve_result.package_json) {
            pkg_version = package_json.version;
            pkg_name = package_json.name;
        }
        return {
            ctx: ctx,
            path: resolve_result.path_pair.primary,
            contents_or_fd: {
                fd: {
                    dir: resolve_result.dirname_fd,
                    file: resolve_result.file_fd,
                },
            },
            side_effects: resolve_result.primary_side_effects_data,
            jsx: resolve_result.jsx,
            source_index: source_index,
            module_type: resolve_result.module_type,
            emit_decorator_metadata: resolve_result.flags.emit_decorator_metadata,
            experimental_decorators: resolve_result.flags.experimental_decorators,
            package_version: pkg_version,
            package_name: pkg_name,
            known_target: ctx.transpiler.options.target,
        };
    }

    static fn getRuntimeSource(target: options.Target): RuntimeSource {
        return switch (target) {
            .bun => comptime getRuntimeSourceComptime(.bun),
            .bun_macro => comptime getRuntimeSourceComptime(.bun_macro),
            .node => comptime getRuntimeSourceComptime(.node),
            else => comptime getRuntimeSourceComptime(.browser),
        };
    }

    static fn ioTaskCallback(task: *ThreadPoolLib.Task) {
        runFromThreadPool(@fieldParentPtr("io_task", task));
    }

    static fn taskCallback(task: *ThreadPoolLib.Task) {
        runFromThreadPool(@fieldParentPtr("task", task));
    }

    static fn runFromThreadPool(client: *ParseTask) {
        ParseTask_runFromThreadPool(client);
    }

    static fn onComplete(result: *ParseTaskResult) {
        BundleV2.onParseTaskComplete(result, result.ctx);
    }
}

struct RuntimeSource {
    parse_task: ParseTask,
    source: Logger.Source,
}

fn getRuntimeSourceComptime(comptime target: options.Target): RuntimeSource {
    // When the `require` identifier is visited, it is replaced with e_require_call_target
    // and then that is either replaced with the module itself, or an import to the
    // runtime here.
    const runtime_require = switch (target) {
        // Previously, Bun inlined `import.meta.require` at all usages. This broke
        // code that called `fn.toString()` and parsed the code outside a module
        // context.
        .bun, .bun_macro => "export var __require = import.meta.require;\n",

        .node => "import { createRequire } from \"node:module\";\nexport var __require = /* @__PURE__ */ createRequire(import.meta.url);\n",

        // Copied from esbuild's runtime.go:
        //
        // > This fallback "require" function exists so that "typeof require" can
        // > naturally be "function" even in non-CommonJS environments since esbuild
        // > emulates a CommonJS environment (issue #1202). However, people want this
        // > shim to fall back to "globalThis.require" even if it's defined later
        // > (including property accesses such as "require.resolve") so we need to
        // > use a proxy (issue #1614).
        //
        // When bundling to node, esbuild picks this code path as well, but `globalThis.require`
        // is not always defined there. The `createRequire` call approach is more reliable.
        else => "export var __require = /* @__PURE__ */ (x =>\n" ++
            "  typeof require !== 'undefined' ? require :\n" ++
            "  typeof Proxy !== 'undefined' ? new Proxy(x, {\n" ++
            "    get: (a, b) => (typeof require !== 'undefined' ? require : a)[b]\n" ++
            "  }) : x\n" ++
            ")(function (x) {\n" ++
            "  if (typeof require !== 'undefined') return require.apply(this, arguments)\n" ++
            "  throw Error('Dynamic require of \"' + x + '\" is not supported')\n" ++
            "});\n",
    };
    const runtime_using_symbols = switch (target) {
        // JavaScriptCore supports `using` / `await using` natively (see
        // `lower_using = !target.isBun()` below), so these helpers are unused
        // when bundling for Bun and will be tree-shaken. They are still defined
        // here so the runtime module exports a consistent shape across targets.
        // Bun's WebKit also has Symbol.asyncDispose, Symbol.dispose, and
        // SuppressedError, so no polyfills are needed.
        .bun => "export var __using = (stack, value, async) => {\n" ++
            "  if (value != null) {\n" ++
            "    if (typeof value !== 'object' && typeof value !== 'function') throw TypeError('Object expected to be assigned to \"using\" declaration')\n" ++
            "    let dispose\n" ++
            "    if (async) dispose = value[Symbol.asyncDispose]\n" ++
            "    if (dispose === void 0) dispose = value[Symbol.dispose]\n" ++
            "    if (typeof dispose !== 'function') throw TypeError('Object not disposable')\n" ++
            "    stack.push([async, dispose, value])\n" ++
            "  } else if (async) {\n" ++
            "    stack.push([async])\n" ++
            "  }\n" ++
            "  return value\n" ++
            "}\n\n" ++
            "export var __callDispose = (stack, error, hasError) => {\n" ++
            "  let fail = e => error = hasError ? new SuppressedError(e, error, 'An error was suppressed during disposal') : (hasError = true, e)\n" ++
            "    , next = (it) => {\n" ++
            "      while (it = stack.pop()) {\n" ++
            "        try {\n" ++
            "          var result = it[1] && it[1].call(it[2])\n" ++
            "          if (it[0]) return Promise.resolve(result).then(next, (e) => (fail(e), next()))\n" ++
            "        } catch (e) {\n" ++
            "          fail(e)\n" ++
            "        }\n" ++
            "      }\n" ++
            "      if (hasError) throw error\n" ++
            "    }\n" ++
            "  return next()\n" ++
            "}\n",
        // Other platforms may or may not have the symbol or errors
        // The definitions of __dispose and __asyncDispose match what esbuild's __wellKnownSymbol() helper does
        else => "var __dispose = Symbol.dispose || /* @__PURE__ */ Symbol.for('Symbol.dispose');\n" ++
            "var __asyncDispose =  Symbol.asyncDispose || /* @__PURE__ */ Symbol.for('Symbol.asyncDispose');\n\n" ++
            "export var __using = (stack, value, async) => {\n" ++
            "  if (value != null) {\n" ++
            "    if (typeof value !== 'object' && typeof value !== 'function') throw TypeError('Object expected to be assigned to \"using\" declaration')\n" ++
            "    var dispose\n" ++
            "    if (async) dispose = value[__asyncDispose]\n" ++
            "    if (dispose === void 0) dispose = value[__dispose]\n" ++
            "    if (typeof dispose !== 'function') throw TypeError('Object not disposable')\n" ++
            "    stack.push([async, dispose, value])\n" ++
            "  } else if (async) {\n" ++
            "    stack.push([async])\n" ++
            "  }\n" ++
            "  return value\n" ++
            "}\n\n" ++
            "export var __callDispose = (stack, error, hasError) => {\n" ++
            "  var E = typeof SuppressedError === 'function' ? SuppressedError :\n" ++
            "    function (e, s, m, _) { return _ = Error(m), _.name = 'SuppressedError', _.error = e, _.suppressed = s, _ },\n" ++
            "    fail = e => error = hasError ? new E(e, error, 'An error was suppressed during disposal') : (hasError = true, e),\n" ++
            "    next = (it) => {\n" ++
            "      while (it = stack.pop()) {\n" ++
            "        try {\n" ++
            "          var result = it[1] && it[1].call(it[2])\n" ++
            "          if (it[0]) return Promise.resolve(result).then(next, (e) => (fail(e), next()))\n" ++
            "        } catch (e) {\n" ++
            "          fail(e)\n" ++
            "        }\n" ++
            "      }\n" ++
            "      if (hasError) throw error\n" ++
            "    }\n" ++
            "  return next()\n" ++
            "}\n",
    };
    const runtime_code = @embedFile("../runtime.js") ++ runtime_require ++ runtime_using_symbols;

    const parse_task = ParseTask{
        ctx: undefined,
        path: Fs.Path.initWithNamespace("runtime", "bun:runtime"),
        side_effects: .no_side_effects__pure_data,
        jsx: {
            parse: false,
        },
        contents_or_fd: {
            contents: runtime_code,
        },
        source_index: Index.runtime,
        loader: .js,
        known_target: target,
    };
    const source = Logger.Source{
        path: parse_task.path,
        contents: parse_task.contents_or_fd.contents,
        index: Index.runtime,
    };
    return { parse_task: parse_task, source: source };
}

fn getEmptyCSSAST(
    log: *Logger.Log,
    transpiler: *Transpiler,
    opts: js_parser.Parser.Options,
    allocator: std.mem.Allocator,
    source: *const Logger.Source,
): JSAst! {
    const root = Expr.init(E.Object, std.mem.zeroes(E.Object), Logger.Loc{ start: 0 });
    let ast = JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, log, root, source, "")?).?);
    ast.css = bun.create(allocator, bun.css.BundlerStyleSheet, bun.css.BundlerStyleSheet.empty(allocator));
    return ast;
}

fn getEmptyAST(log: *Logger.Log, transpiler: *Transpiler, opts: js_parser.Parser.Options, allocator: std.mem.Allocator, source: *const Logger.Source, comptime RootType: type): JSAst! {
    const root = Expr.init(RootType, RootType{}, Logger.Loc.Empty);
    return JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, log, root, source, "")?).?);
}

struct FileLoaderHash {
    key: []const u8,
    content_hash: u64,
}

fn getAST(
    log: *Logger.Log,
    transpiler: *Transpiler,
    opts: js_parser.Parser.Options,
    allocator: std.mem.Allocator,
    resolver: *Resolver,
    source: *const Logger.Source,
    loader: Loader,
    unique_key_prefix: u64,
    unique_key_for_additional_file: *FileLoaderHash,
    has_any_css_locals: *std.atomic.Value(u32),
): JSAst! {
    switch (loader) {
        .jsx, .tsx, .js, .ts => {
            let trace = bun.perf.trace("Bundler.ParseJS");
            defer trace.end();
            if (res := resolver.caches.js.parse(
                transpiler.allocator,
                opts,
                transpiler.options.define,
                log,
                source,
            )?) {
                return JSAst.init(res.ast);
            } else {
                return getEmptyAST(
                    log,
                    transpiler,
                    opts,
                    allocator,
                    source,
                    if (opts.module_type == .esm) E.Undefined else E.Object,
                )?;
            }
        },
        .json, .jsonc => {
            let trace = bun.perf.trace("Bundler.ParseJSON");
            defer trace.end();
            const root = (resolver.caches.json.parseJSON(log, source, allocator, if (loader == .jsonc) .jsonc else .json, true)?) ?? Expr.init(E.Object, std.mem.zeroes(E.Object), Logger.Loc.Empty);
            return JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, log, root, source, "")?).?);
        },
        .toml => {
            let trace = bun.perf.trace("Bundler.ParseTOML");
            defer trace.end();
            let temp_log = bun.logger.Log.init(allocator);
            defer {
                bun.handleOom(temp_log.cloneToWithRecycled(log, true));
                temp_log.msgs.clearAndFree();
            }
            const root = TOML.parse(source, &temp_log, allocator, false)?;
            return JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, &temp_log, root, source, "")?).?);
        },
        .yaml => {
            let trace = bun.perf.trace("Bundler.ParseYAML");
            defer trace.end();
            let temp_log = bun.logger.Log.init(allocator);
            defer {
                bun.handleOom(temp_log.cloneToWithRecycled(log, true));
                temp_log.msgs.clearAndFree();
            }
            const root = YAML.parse(source, &temp_log, allocator)?;
            return JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, &temp_log, root, source, "")?).?);
        },
        .json5 => {
            let trace = bun.perf.trace("Bundler.ParseJSON5");
            defer trace.end();
            let temp_log = bun.logger.Log.init(allocator);
            defer {
                bun.handleOom(temp_log.cloneToWithRecycled(log, true));
                temp_log.msgs.clearAndFree();
            }
            const root = JSON5.parse(source, &temp_log, allocator)?;
            return JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, &temp_log, root, source, "")?).?);
        },
        .text => {
            const root = Expr.init(E.String, E.String{
                data: source.contents,
            }, Logger.Loc{ start: 0 });
            let ast = JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, log, root, source, "")?).?);
            ast.addUrlForCss(allocator, source, "text/plain", null, transpiler.options.compile_to_standalone_html);
            return ast;
        },
        .md => {
            const html = bun.md.renderToHtml(source.contents, allocator) catch {
                log.addError(
                    source,
                    Logger.Loc.Empty,
                    "Failed to render markdown to HTML",
                ) catch e => bun.handleOom(e);
                return error.ParserError;
            };
            const root = Expr.init(E.String, E.String{
                data: html,
            }, Logger.Loc{ start: 0 });
            let ast = JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, log, root, source, "")?).?);
            ast.addUrlForCss(allocator, source, "text/html", null, transpiler.options.compile_to_standalone_html);
            return ast;
        },

        .sqlite_embedded, .sqlite => {
            if (!transpiler.options.target.isBun()) {
                log.addError(
                    source,
                    Logger.Loc.Empty,
                    "To use the \"sqlite\" loader, set target to \"bun\"",
                ) catch e => bun.handleOom(e);
                return error.ParserError;
            }

            const path_to_use = brk: {
                // Implements embedded sqlite
                if (loader == .sqlite_embedded) {
                    const embedded_path = std.fmt.allocPrint(allocator, "{f}A{d:0>8}", .{ bun.fmt.hexIntLower(unique_key_prefix), source.index.get() }) catch unreachable;
                    unique_key_for_additional_file.* = {
                        key: embedded_path,
                        content_hash: ContentHasher.run(source.contents),
                    };
                    break :brk embedded_path;
                }

                break :brk source.path.text;
            };

            // This injects the following code:
            //
            // import.meta.require(unique_key).db
            //
            const import_path = Expr.init(E.String, E.String{
                data: path_to_use,
            }, Logger.Loc{ start: 0 });

            const import_meta = Expr.init(E.ImportMeta, std.mem.zeroes(E.ImportMeta), Logger.Loc{ start: 0 });
            const require_property = Expr.init(E.Dot, E.Dot{
                target: import_meta,
                name_loc: Logger.Loc.Empty,
                name: "require",
            }, Logger.Loc{ start: 0 });
            let require_args = allocator.alloc(Expr, 2) catch unreachable;
            require_args[0] = import_path;
            let object_properties = allocator.alloc(G.Property, 1) catch unreachable;
            object_properties[0] = G.Property{
                key: Expr.init(E.String, E.String{
                    data: "type",
                }, Logger.Loc{ start: 0 }),
                value: Expr.init(E.String, E.String{
                    data: "sqlite",
                }, Logger.Loc{ start: 0 }),
            };
            require_args[1] = Expr.init(E.Object, E.Object{
                properties: G.Property.List.fromOwnedSlice(object_properties),
                is_single_line: true,
            }, Logger.Loc{ start: 0 });
            const require_call = Expr.init(E.Call, E.Call{
                target: require_property,
                args: BabyList(Expr).fromOwnedSlice(require_args),
            }, Logger.Loc{ start: 0 });

            const root = Expr.init(E.Dot, E.Dot{
                target: require_call,
                name_loc: Logger.Loc.Empty,
                name: "db",
            }, Logger.Loc{ start: 0 });

            return JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, log, root, source, "")?).?);
        },
        .napi => {
            // (dap-eval-cb "source.contents.ptr")
            if (transpiler.options.target == .browser) {
                log.addError(
                    source,
                    Logger.Loc.Empty,
                    "Loading .node files won't work in the browser. Make sure to set target to \"bun\" or \"node\"",
                ) catch e => bun.handleOom(e);
                return error.ParserError;
            }

            const unique_key = std.fmt.allocPrint(allocator, "{f}A{d:0>8}", .{ bun.fmt.hexIntLower(unique_key_prefix), source.index.get() }) catch unreachable;
            // This injects the following code:
            //
            // require(unique_key)
            //
            const import_path = Expr.init(E.String, E.String{
                data: unique_key,
            }, Logger.Loc{ start: 0 });

            let require_args = allocator.alloc(Expr, 1) catch unreachable;
            require_args[0] = import_path;

            const root = Expr.init(E.Call, E.Call{
                target: { data: { e_require_call_target: undefined }, loc: { start: 0 } },
                args: BabyList(Expr).fromOwnedSlice(require_args),
            }, Logger.Loc{ start: 0 });

            unique_key_for_additional_file.* = {
                key: unique_key,
                content_hash: ContentHasher.run(source.contents),
            };
            return JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, log, root, source, "")?).?);
        },
        .html => {
            let scanner = HTMLScanner.init(allocator, log, source);
            scanner.scan(source.contents)?;

            // Reuse existing code for creating the AST
            // because it handles the various Ref and other structs we
            // need in order to print code later.
            let ast = (js_parser.newLazyExportAST(
                allocator,
                transpiler.options.define,
                opts,
                log,
                Expr.init(E.Missing, std.mem.zeroes(E.Missing), Logger.Loc.Empty),
                source,
                "",
            )?).?;
            ast.import_records = scanner.import_records;

            // We're banning import default of html loader files for now.
            //
            // TLDR: it kept including:
            //
            //   var name_default = ...;
            //
            // in the bundle because of the exports AST, and
            // gave up on figuring out how to fix it so that
            // this feature could ship.
            ast.has_lazy_export = false;
            ast.parts.ptr[1] = {
                stmts: [],
                is_live: true,
                import_record_indices: brk2: {
                    // Generate a single part that depends on all the import records.
                    // This is to ensure that we generate a JavaScript bundle containing all the user's code.
                    let import_record_indices = Part.ImportRecordIndices.initCapacity(allocator, scanner.import_records.len)?;
                    import_record_indices.len = @truncate(scanner.import_records.len);
                    let index: usize = 0;
                    for (*import_record of import_record_indices.slice()) {
                        import_record.* = @intCast(index);
                        index += 1;
                    }
                    break :brk2 import_record_indices;
                },
            };

            // Try to avoid generating unnecessary ESM <> CJS wrapper code.
            if (opts.output_format == .esm || opts.output_format == .iife) {
                ast.exports_kind = .esm;
            }

            return JSAst.init(ast);
        },
        .css => {
            // make css ast
            let import_records: BabyList(ImportRecord) = {};
            const source_code = source.contents;
            let temp_log = bun.logger.Log.init(allocator);
            defer {
                bun.handleOom(temp_log.appendToMaybeRecycled(log, source));
            }

            const css_module_suffix = ".module.css";
            const enable_css_modules = source.path.pretty.len > css_module_suffix.len &&
                strings.eqlComptime(source.path.pretty[source.path.pretty.len - css_module_suffix.len ..], css_module_suffix);
            const parser_options = if (enable_css_modules) init: {
                let parseropts = bun.css.ParserOptions.default(allocator, &temp_log);
                parseropts.filename = bun.path.basename(source.path.pretty);
                parseropts.css_modules = bun.css.CssModuleConfig{};
                break :init parseropts;
            } else bun.css.ParserOptions.default(allocator, &temp_log);

            const parsed_css = switch (bun.css.BundlerStyleSheet.parseBundler(
                allocator,
                source_code,
                parser_options,
                &import_records,
                source.index,
            )) {
                .result v => v,
                .err e => {
                    e.addToLogger(&temp_log, source, allocator)?;
                    return error.SyntaxError;
                },
            };
            let css_ast = parsed_css[0];
            let extra = parsed_css[1];
            // Make sure the css modules local refs have a valid tag
            if (comptime bun.Environment.isDebug) {
                if (css_ast.local_scope.count() > 0) {
                    for (entry of css_ast.local_scope.values()) {
                        const sym_ref = entry.ref;
                        bun.assert(sym_ref.innerIndex() < extra.symbols.len);
                    }
                }
            }
            if (e := css_ast.minify(allocator, bun.css.MinifyOptions{
                targets: bun.css.Targets.forBundlerTarget(transpiler.options.target),
                unused_symbols: {},
            }, &extra).asErr()) {
                e.addToLogger(&temp_log, source, allocator)?;
                return error.MinifyError;
            }
            if (css_ast.local_scope.count() > 0) {
                * = has_any_css_locals.fetchAdd(1, .monotonic);
            }
            // If this is a css module, the final exports object wil be set in `generateCodeForLazyExport`.
            const root = Expr.init(E.Object, std.mem.zeroes(E.Object), Logger.Loc{ start: 0 });
            const css_ast_heap = bun.create(allocator, bun.css.BundlerStyleSheet, css_ast);
            let ast = JSAst.init((js_parser.newLazyExportASTImpl(allocator, transpiler.options.define, opts, &temp_log, root, source, "", extra.symbols)?).?);
            ast.css = css_ast_heap;
            ast.import_records = import_records;
            return ast;
        },
        // TODO:
        .dataurl, .base64, .bunsh => {
            return getEmptyAST(log, transpiler, opts, allocator, source, E.String)?;
        },
        .file, .wasm => {
            bun.assert(loader.shouldCopyForBundling());

            // Put a unique key in the AST to implement the URL loader. At the end
            // of the bundle, the key is replaced with the actual URL.
            const content_hash = ContentHasher.run(source.contents);

            const unique_key: []const u8 = if (transpiler.options.dev_server != null)
                // With DevServer, the actual URL is added now, since it can be
                // known this far ahead of time, and it means the unique key code
                // does not have to perform an additional pass over files.
                //
                // To avoid a mutex, the actual insertion of the asset to DevServer
                // is done on the bundler thread.
                std.fmt.allocPrint(
                    allocator,
                    bun.bake.DevServer.asset_prefix ++ "/{s}{s}",
                    .{
                        &std.fmt.bytesToHex(std.mem.asBytes(&content_hash), .lower),
                        std.fs.path.extension(source.path.text),
                    },
                )?
            else
                std.fmt.allocPrint(
                    allocator,
                    "{f}A{d:0>8}",
                    .{ bun.fmt.hexIntLower(unique_key_prefix), source.index.get() },
                )?;
            const root = Expr.init(E.String, { data: unique_key }, { start: 0 });
            unique_key_for_additional_file.* = {
                key: unique_key,
                content_hash: content_hash,
            };
            let ast = JSAst.init((js_parser.newLazyExportAST(allocator, transpiler.options.define, opts, log, root, source, "")?).?);
            ast.addUrlForCss(allocator, source, null, unique_key, transpiler.options.compile_to_standalone_html);
            return ast;
        },
    }
}

fn getCodeForParseTaskWithoutPlugins(
    task: *ParseTask,
    log: *Logger.Log,
    transpiler: *Transpiler,
    resolver: *Resolver,
    allocator: std.mem.Allocator,
    file_path: *Fs.Path,
    loader: Loader,
): CacheEntry! {
    return switch (task.contents_or_fd) {
        .fd contents => brk: {
            let trace = bun.perf.trace("Bundler.readFile");
            defer trace.end();

            // Check FileMap for in-memory files first
            if (file_map := task.ctx.file_map) {
                if (file_contents := file_map.get(file_path.text)) {
                    break :brk {
                        contents: file_contents,
                        fd: bun.invalid_fd,
                    };
                }
            }

            if (strings.eqlComptime(file_path.namespace, "node")) lookup_builtin: {
                if (f := task.ctx.framework) {
                    if (file := f.built_in_modules.get(file_path.text)) {
                        switch (file) {
                            .code code => break :brk { contents: code, fd: bun.invalid_fd },
                            .@"import" path => {
                                file_path.* = Fs.Path.init(path);
                                break :lookup_builtin;
                            },
                        }
                    }
                }

                break :brk {
                    contents: NodeFallbackModules.contentsFromPath(file_path.text) ?? "",
                    fd: bun.invalid_fd,
                };
            }

            break :brk resolver.caches.fs.readFileWithAllocator(
                // TODO: this allocator may be wrong for native plugins
                if (loader.shouldCopyForBundling())
                    // The OutputFile will own the memory for the contents
                    bun.default_allocator
                else
                    allocator,
                transpiler.fs,
                file_path.text,
                task.contents_or_fd.fd.dir,
                false,
                contents.file.unwrapValid(),
            ) catch e => {
                const source = &Logger.Source.initEmptyFile(log.msgs.allocator.dupe(u8, file_path.text) catch unreachable);
                if (e == error.ENOENT || e == error.FileNotFound) {
                    log.addErrorFmt(
                        source,
                        Logger.Loc.Empty,
                        allocator,
                        "File not found {f}",
                        .{ bun.fmt.quote(file_path.text) },
                    ) catch {};
                    return error.FileNotFound;
                } else {
                    log.addErrorFmt(
                        source,
                        Logger.Loc.Empty,
                        allocator,
                        "{s} reading file: {f}",
                        .{ @errorName(e), bun.fmt.quote(file_path.text) },
                    ) catch {};
                }
                return e;
            };
        },
        .contents contents => CacheEntry{
            contents: contents,
            fd: bun.invalid_fd,
        },
    };
}

fn getCodeForParseTask(
    task: *ParseTask,
    log: *Logger.Log,
    transpiler: *Transpiler,
    resolver: *Resolver,
    allocator: std.mem.Allocator,
    file_path: *Fs.Path,
    loader: *Loader,
    from_plugin: *bool,
): CacheEntry! {
    let might_have_on_parse_plugins = false;
    if (!task.source_index.isRuntime() && !strings.eqlComptime(file_path.namespace, "node")) {
        if (plugin := task.ctx.plugins) {
            might_have_on_parse_plugins = plugin.hasOnBeforeParsePlugins();
        }
    }

    if (!might_have_on_parse_plugins) {
        return getCodeForParseTaskWithoutPlugins(task, log, transpiler, resolver, allocator, file_path, loader.*);
    }

    let should_continue_running: i32 = 1;

    let ctx = OnBeforeParsePlugin{
        task: task,
        log: log,
        transpiler: transpiler,
        resolver: resolver,
        allocator: allocator,
        file_path: file_path,
        loader: loader,
        deferred_error: null,
        should_continue_running: &should_continue_running,
    };

    return ctx.run(task.ctx.plugins.?, from_plugin)?;
}

const headers = bun.c;

@c struct OnBeforeParseArguments {
    struct_size: usize = @sizeOf(OnBeforeParseArguments),
    context: *OnBeforeParsePlugin,
    path_ptr: ([*]const u8)? = "",
    path_len: usize = 0,
    namespace_ptr: ([*]const u8)? = "file",
    namespace_len: usize = "file".len,
    default_loader: Loader = .file,
    external: *!? = null,
}

@c struct BunLogOptions {
    struct_size: usize = @sizeOf(BunLogOptions),
    message_ptr: ([*]const u8)? = null,
    message_len: usize = 0,
    path_ptr: ([*]const u8)? = null,
    path_len: usize = 0,
    source_line_text_ptr: ([*]const u8)? = null,
    source_line_text_len: usize = 0,
    level: Logger.Log.Level = .err,
    line: i32 = 0,
    column: i32 = 0,
    line_end: i32 = 0,
    column_end: i32 = 0,

    fn sourceLineText(): string {
        if (ptr := _.source_line_text_ptr) {
            if (_.source_line_text_len > 0) {
                return unsafe { ptr[0.._.source_line_text_len] };
            }
        }
        return "";
    }

    fn path(): string {
        if (ptr := _.path_ptr) {
            if (_.path_len > 0) {
                return unsafe { ptr[0.._.path_len] };
            }
        }
        return "";
    }

    fn message(): string {
        if (ptr := _.message_ptr) {
            if (_.message_len > 0) {
                return unsafe { ptr[0.._.message_len] };
            }
        }
        return "";
    }

    fn append(log: *Logger.Log, namespace: string) {
        const allocator = log.msgs.allocator;
        const source_line_text = _.sourceLineText();
        const location = Logger.Location.init(
            _.path(),
            namespace,
            @max(_.line, -1),
            @max(_.column, -1),
            @max(_.column_end - _.column, 0),
            if (source_line_text.len > 0) bun.handleOom(allocator.dupe(u8, source_line_text)) else null,
        );
        let msg = Logger.Msg{ data: { location: location, text: bun.handleOom(allocator.dupe(u8, _.message())) } };
        switch (_.level) {
            .err => msg.kind = .err,
            .warn => msg.kind = .warn,
            .verbose => msg.kind = .verbose,
            .debug => msg.kind = .debug,
            else => {},
        }
        if (msg.kind == .err) {
            log.errors += 1;
        } else if (msg.kind == .warn) {
            log.warnings += 1;
        }
        bun.handleOom(log.addMsg(msg));
    }

    @callconv("c")
    static fn logFn(
        args_: (*OnBeforeParseArguments)?,
        log_options_: (*BunLogOptions)?,
    ) {
        const args = args_ ?? return;
        const log_options = log_options_ ?? return;
        log_options.append(args.context.log, args.context.file_path.namespace);
    }
}

@c struct OnBeforeParseResultWrapper {
    original_source: ([*]const u8)? = null,
    original_source_len: usize = 0,
    original_source_fd: bun.FD = bun.invalid_fd,
    loader: Loader,
    check: if (bun.Environment.isDebug) u32 else u0 = if (bun.Environment.isDebug) 42069 else 0, // Value to ensure OnBeforeParseResult is wrapped in this struct
    result: OnBeforeParseResult,
}

type OptAnyPtr = *!?;
@callconv("c")
fn dummyFreeUserContext(_: OptAnyPtr) {}

@c struct OnBeforeParseResult {
    struct_size: usize = @sizeOf(OnBeforeParseResult),
    source_ptr: ([*]const u8)? = null,
    source_len: usize = 0,
    loader: Loader,

    fetch_source_code_fn: @TypeOf(&OnBeforeParsePlugin.fetchSourceCode) = &OnBeforeParsePlugin.fetchSourceCode,

    user_context: *!? = null,
    free_user_context: (@TypeOf(&dummyFreeUserContext))? = null,

    log: @TypeOf(&BunLogOptions.logFn) = &BunLogOptions.logFn,

    static fn getWrapper(result: *OnBeforeParseResult): *OnBeforeParseResultWrapper {
        const wrapper: *OnBeforeParseResultWrapper = @fieldParentPtr("result", result);
        bun.debugAssert(wrapper.check == 42069);
        return wrapper;
    }
}

struct OnBeforeParsePlugin {
    task: *ParseTask,
    log: *Logger.Log,
    transpiler: *Transpiler,
    resolver: *Resolver,
    allocator: std.mem.Allocator,
    file_path: *Fs.Path,
    loader: *Loader,
    deferred_error: anyerror? = null,
    should_continue_running: *i32,

    result: (*OnBeforeParseResult)? = null,

    @callconv("c")
    static fn fetchSourceCode(args: *OnBeforeParseArguments, result: *OnBeforeParseResult): i32 {
        debug("fetchSourceCode", .{});
        const self_ctx = args.context;
        if (self_ctx.log.errors > 0 || self_ctx.deferred_error != null || self_ctx.should_continue_running.* != 1) {
            return 1;
        }

        if (result.source_ptr != null) {
            return 0;
        }

        const entry = getCodeForParseTaskWithoutPlugins(
            self_ctx.task,
            self_ctx.log,
            self_ctx.transpiler,
            self_ctx.resolver,
            self_ctx.allocator,
            self_ctx.file_path,

            result.loader,
        ) catch e => {
            self_ctx.deferred_error = e;
            self_ctx.should_continue_running.* = 0;
            return 1;
        };
        result.source_ptr = entry.contents.ptr;
        result.source_len = entry.contents.len;
        result.free_user_context = null;
        result.user_context = null;
        const wrapper: *OnBeforeParseResultWrapper = result.getWrapper();
        wrapper.original_source = entry.contents.ptr;
        wrapper.original_source_len = entry.contents.len;
        wrapper.original_source_fd = entry.fd;
        return 0;
    }

    pub export fn OnBeforeParseResult__reset(*_) {
        const wrapper = _.getWrapper();
        _.loader = wrapper.loader;
        if (src_ptr := wrapper.original_source) {
            const src = unsafe { src_ptr[0..wrapper.original_source_len] };
            _.source_ptr = src.ptr;
            _.source_len = src.len;
        } else {
            _.source_ptr = null;
            _.source_len = 0;
        }
    }

    pub export fn OnBeforeParsePlugin__isDone(*_): i32 {
        if (_.should_continue_running.* != 1) {
            return 1;
        }

        const result = _.result ?? return 1;
        // The first plugin to set the source wins.
        // But, we must check that they actually modified it
        // since fetching the source stores it inside `result.source_ptr`
        if (result.source_ptr != null) {
            const wrapper: *OnBeforeParseResultWrapper = result.getWrapper();
            return @intFromBool(result.source_ptr.? != wrapper.original_source.?);
        }

        return 0;
    }

    fn run(*_, plugin: *jsc.API.JSBundler.Plugin, from_plugin: *bool): CacheEntry! {
        let args = OnBeforeParseArguments{
            context: _,
            path_ptr: _.file_path.text.ptr,
            path_len: _.file_path.text.len,
            default_loader: _.loader.*,
        };
        if (_.file_path.namespace.len > 0) {
            args.namespace_ptr = _.file_path.namespace.ptr;
            args.namespace_len = _.file_path.namespace.len;
        }
        let wrapper = OnBeforeParseResultWrapper{
            loader: _.loader.*,
            result: OnBeforeParseResult{
                loader: _.loader.*,
            },
        };

        _.result = &wrapper.result;
        const count = plugin.callOnBeforeParsePlugins(
            _,
            if (bun.strings.eqlComptime(_.file_path.namespace, "file"))
                &bun.String.empty
            else
                &bun.String.init(_.file_path.namespace),

            &bun.String.init(_.file_path.text),
            &args,
            &wrapper.result,
            _.should_continue_running,
        );
        if (comptime Environment.enable_logs)
            debug("callOnBeforeParsePlugins({s}:{s}) = {d}", .{ _.file_path.namespace, _.file_path.text, count });
        if (count > 0) {
            if (e := _.deferred_error) {
                if (free_user_context := wrapper.result.free_user_context) {
                    free_user_context(wrapper.result.user_context);
                }

                return e;
            }

            // If the plugin sets the `free_user_context` function pointer, it _must_ set the `user_context` pointer.
            // Otherwise this is just invalid behavior.
            if (wrapper.result.user_context == null && wrapper.result.free_user_context != null) {
                let msg = Logger.Msg{ data: { location: null, text: bun.default_allocator.dupe(
                    u8,
                    "Native plugin set the `free_plugin_source_code_context` field without setting the `plugin_source_code_context` field.",
                ) catch e => bun.handleOom(e) } };
                msg.kind = .err;
                args.context.log.errors += 1;
                bun.handleOom(args.context.log.addMsg(msg));
                return error.InvalidNativePlugin;
            }

            if (_.log.errors > 0) {
                if (free_user_context := wrapper.result.free_user_context) {
                    free_user_context(wrapper.result.user_context);
                }

                return error.SyntaxError;
            }

            if (ptr := wrapper.result.source_ptr) {
                if (wrapper.result.free_user_context != null) {
                    _.task.external_free_function = {
                        ctx: wrapper.result.user_context,
                        function: wrapper.result.free_user_context,
                    };
                }
                from_plugin.* = true;
                _.loader.* = wrapper.result.loader;
                return {
                    contents: unsafe { ptr[0..wrapper.result.source_len] },
                    external_free_function: {
                        ctx: wrapper.result.user_context,
                        function: wrapper.result.free_user_context,
                    },
                    fd: wrapper.original_source_fd,
                };
            }
        }

        return getCodeForParseTaskWithoutPlugins(_.task, _.log, _.transpiler, _.resolver, _.allocator, _.file_path, _.loader.*)?;
    }
}

fn getSourceCode(
    task: *ParseTask,
    worker: *ThreadPool.Worker,
    log: *Logger.Log,
): CacheEntry! {
    const allocator = worker.allocator;

    let data = worker.data;
    const transpiler = &data.transpiler;
    errdefer transpiler.resetStore();
    const resolver: *Resolver = &transpiler.resolver;
    let file_path = task.path;
    let loader = task.loader ?? file_path.loader(&transpiler.options.loaders) ?? options.Loader.file;

    let contents_came_from_plugin: bool = false;
    return getCodeForParseTask(task, log, transpiler, resolver, allocator, &file_path, &loader, &contents_came_from_plugin)?;
}

fn runWithSourceCode(
    task: *ParseTask,
    worker: *ThreadPool.Worker,
    step: *ResultErrorStep,
    log: *Logger.Log,
    entry: *CacheEntry,
): ResultSuccess! {
    const allocator = worker.allocator;

    let transpiler = worker.transpilerForTarget(task.known_target);
    errdefer transpiler.resetStore();
    const resolver: *Resolver = &transpiler.resolver;
    const file_path = &task.path;
    const loader = task.loader ?? file_path.loader(&transpiler.options.loaders) ?? options.Loader.file;

    // WARNING: Do not change the variant of `task.contents_or_fd` from
    // `.fd` to `.contents` (or back) after this point!
    //
    // When `task.contents_or_fd == .fd`, `entry.contents` is an owned string.
    // When `task.contents_or_fd == .contents`, `entry.contents` is NOT owned! Freeing it here will cause a double free!
    //
    // Changing from `.contents` to `.fd` will cause a double free.
    // This was the case in the situation where the ParseTask receives its `.contents` from an onLoad plugin, which caused it to be
    // allocated by `bun.default_allocator` and then freed in `BundleV2.deinit` (and also by `entry.deinit(allocator)` below).
    const debug_original_variant_check: if (bun.Environment.isDebug) ContentsOrFd.Tag else void =
        if (bun.Environment.isDebug) std.meta.activeTag(task.contents_or_fd);
    errdefer {
        if (comptime bun.Environment.isDebug) {
            if (std.meta.activeTag(task.contents_or_fd) != debug_original_variant_check) {
                std.debug.panic("BUG: `task.contents_or_fd` changed in a way that will cause a double free or memory to leak!\n\n    Original = {s}\n    New = {s}\n", .{
                    @tagName(debug_original_variant_check),
                    @tagName(task.contents_or_fd),
                });
            }
        }
        if (task.contents_or_fd == .fd) entry.deinit(allocator);
    }

    const will_close_file_descriptor = task.contents_or_fd == .fd &&
        entry.fd.isValid() &&
        entry.fd.stdioTag() == null &&
        worker.ctx.bun_watcher == null;
    if (will_close_file_descriptor) {
        * = entry.closeFD();
        task.contents_or_fd = { fd: {
            file: bun.invalid_fd,
            dir: bun.invalid_fd,
        } };
    } else if (task.contents_or_fd == .fd) {
        task.contents_or_fd = { fd: {
            file: entry.fd,
            dir: bun.invalid_fd,
        } };
    }
    step.* = .parse;

    const is_empty = strings.isAllWhitespace(entry.contents);

    let use_directive: UseDirective = .none;
    if (!is_empty && transpiler.options.server_components) {
        if (use := UseDirective.parse(entry.contents)) {
            use_directive = use;
        }
    }

    if (use_directive == .client &&
        task.known_target != .bake_server_components_ssr &&
        worker.ctx.framework != null &&
        worker.ctx.framework.?.server_components.?.separate_ssr_graph ||
        // set the target to the client when bundling client-side files
        ((transpiler.options.server_components || transpiler.options.dev_server != null) &&
            task.known_target == .browser))
    {
        // separate_ssr_graph makes boundaries switch to client because the server file uses that generated file as input.
        // this is not done when there is one server graph because it is easier for plugins to deal with.
        transpiler = worker.transpilerForTarget(.browser);
    }

    const source = &Logger.Source{
        path: file_path.*,
        index: task.source_index,
        contents: entry.contents,
        contents_is_recycled: false,
    };

    const target = (if (task.source_index.get() == 1) targetFromHashbang(entry.contents) else null) ??
        if (task.known_target == .bake_server_components_ssr && transpiler.options.framework.?.server_components.?.separate_ssr_graph)
            .bake_server_components_ssr
        else
            transpiler.options.target;

    const output_format = transpiler.options.output_format;

    let opts = js_parser.Parser.Options.init(task.jsx, loader);
    opts.bundle = true;
    opts.warn_about_unbundled_modules = false;
    opts.allow_unresolved = &transpiler.options.allow_unresolved;
    opts.macro_context = &transpiler.macro_context.?;
    opts.package_version = task.package_version;

    opts.features.allow_runtime = !source.index.isRuntime();
    opts.features.unwrap_commonjs_to_esm = output_format == .esm && FeatureFlags.unwrap_commonjs_to_esm;
    opts.features.top_level_await = output_format == .esm || output_format == .internal_bake_dev;
    opts.features.auto_import_jsx = task.jsx.parse && transpiler.options.auto_import_jsx;
    opts.features.trim_unused_imports = loader.isTypeScript() || (transpiler.options.trim_unused_imports ?? false);
    opts.features.inlining = transpiler.options.minify_syntax;
    opts.output_format = output_format;
    opts.features.minify_syntax = transpiler.options.minify_syntax;
    opts.features.minify_identifiers = transpiler.options.minify_identifiers;
    opts.features.minify_keep_names = transpiler.options.keep_names;
    opts.features.minify_whitespace = transpiler.options.minify_whitespace;
    opts.features.emit_decorator_metadata = task.emit_decorator_metadata;
    // emitDecoratorMetadata implies legacy/experimental decorators, as it only
    // makes sense with TypeScript's legacy decorator system (reflect-metadata).
    // TC39 standard decorators have their own metadata mechanism.
    opts.features.standard_decorators = !loader.isTypeScript() || !(task.experimental_decorators || task.emit_decorator_metadata);
    opts.features.unwrap_commonjs_packages = transpiler.options.unwrap_commonjs_packages;
    opts.features.bundler_feature_flags = transpiler.options.bundler_feature_flags;
    // JavaScriptCore implements `using` / `await using` natively, so when
    // targeting Bun there is no need to lower them.
    opts.features.lower_using = !target.isBun();
    opts.features.hot_module_reloading = output_format == .internal_bake_dev && !source.index.isRuntime();
    opts.features.auto_polyfill_require = output_format == .esm && !opts.features.hot_module_reloading;
    opts.features.react_fast_refresh = transpiler.options.react_fast_refresh &&
        loader.isJSX() &&
        !source.path.isNodeModule();

    opts.features.server_components = if (transpiler.options.server_components) switch (target) {
        .browser => .client_side,
        else => switch (use_directive) {
            .none => .wrap_anon_server_functions,
            .client => if (transpiler.options.framework.?.server_components.?.separate_ssr_graph)
                .client_side
            else
                .wrap_exports_for_client_reference,
            .server => .wrap_exports_for_server_reference,
        },
    } else .none;

    opts.framework = transpiler.options.framework;

    opts.ignore_dce_annotations = transpiler.options.ignore_dce_annotations && !source.index.isRuntime();

    // For files that are not user-specified entrypoints, set `import.meta.main` to `false`.
    // Entrypoints will have `import.meta.main` set as "unknown", unless we use `--compile`,
    // in which we inline `true`.
    if (transpiler.options.inline_entrypoint_import_meta_main || !task.is_entry_point) {
        opts.import_meta_main_value = task.is_entry_point && transpiler.options.dev_server == null;
    } else if (target == .node) {
        opts.lower_import_meta_main_for_node_js = true;
    }

    opts.tree_shaking = if (source.index.isRuntime()) true else transpiler.options.tree_shaking;
    opts.code_splitting = transpiler.options.code_splitting;
    opts.module_type = task.module_type;

    task.jsx.parse = loader.isJSX();

    let unique_key_for_additional_file: FileLoaderHash = {
        key: "",
        content_hash: 0,
    };
    let ast: JSAst = if (!is_empty || loader.handlesEmptyFile())
        getAST(log, transpiler, opts, allocator, resolver, source, loader, task.ctx.unique_key, &unique_key_for_additional_file, &task.ctx.linker.has_any_css_locals)?
    else if (loader.isCSS())
        getEmptyCSSAST(log, transpiler, opts, allocator, source)?
    else
        getEmptyAST(log, transpiler, opts, allocator, source, if (opts.module_type == .esm) E.Undefined else E.Object)?;

    ast.target = target;
    if (ast.parts.len <= 1 && ast.css == null && (task.loader == null || task.loader.? != .html)) {
        task.side_effects = .no_side_effects__empty_ast;
    }

    // bun.debugAssert(ast.parts.len > 0); // when parts.len == 0, it is assumed to be pending/failed. empty ast has at least 1 part.

    step.* = .resolve;

    return {
        ast: ast,
        source: source.*,
        log: log.*,
        use_directive: use_directive,
        unique_key_for_additional_file: unique_key_for_additional_file.key,
        side_effects: task.side_effects,
        loader: loader,
        package_name: task.package_name,

        // Hash the files in here so that we do it in parallel.
        content_hash_for_additional_file: if (loader.shouldCopyForBundling())
            unique_key_for_additional_file.content_hash
        else
            0,
    };
}

fn ParseTask_runFromThreadPool(client: *ParseTask) {
    let worker = ThreadPool.Worker.get(client.ctx);
    defer worker.unget();
    debug("ParseTask(0x{x}, {s}) callback", .{ @intFromPtr(client), client.path.text });

    let step: ResultErrorStep = .pending;
    let log = Logger.Log.init(worker.allocator);
    bun.assert(client.source_index.isValid()); // forgot to set source_index

    let value: ResultValue = { empty: { source_index: client.source_index } };
    let done = false;
    if (client.stage == .needs_source_code) {
        let maybe_entry: CacheEntry? = null;
        const entry_res = getSourceCode(client, worker, &log) catch |e| blk_err: {
            value = { e: {
                e: e,
                step: step,
                log: log,
                source_index: client.source_index,
                target: client.known_target,
            } };
            done = true;
            break :blk_err undefined;
        };
        if (!done) {
            maybe_entry = entry_res;
            if (entry := maybe_entry) {
                client.stage = { needs_parse: entry };
            }
            if (log.hasErrors()) {
                value = { e: {
                    e: error.SyntaxError,
                    step: step,
                    log: log,
                    source_index: client.source_index,
                    target: client.known_target,
                } };
                done = true;
            } else if (ThreadPool.usesIOPool()) {
                client.ctx.graph.pool.scheduleInsideThreadPool(client);
                return;
            }
        }
    }

    if (!done) {
        let maybe_ast: ResultSuccess? = runWithSourceCode(client, worker, &step, &log, &client.stage.needs_parse) catch e => blk: {
            if (e == error.EmptyAST) {
                log.deinit();
                value = { empty: {
                    source_index: client.source_index,
                } };
            } else {
                value = { e: {
                    e: e,
                    step: step,
                    log: log,
                    source_index: client.source_index,
                    target: client.known_target,
                } };
            }
            break :blk null;
        };
        if (ast := maybe_ast) {
            // When using HMR, always flag asts with errors as parse failures.
            // Not done outside of the dev server out of fear of breaking existing code.
            if (client.ctx.transpiler.options.dev_server != null && ast.log.hasErrors()) {
                value = {
                    e: {
                        e: error.SyntaxError,
                        step: .parse,
                        log: ast.log,
                        source_index: client.source_index,
                        target: client.known_target,
                    },
                };
            } else {
                value = { success: ast };
            }
        }
    }

    const result = bun.handleOom(bun.default_allocator.create(ParseTaskResult));

    result.* = {
        ctx: client.ctx,
        task: {},
        value: value,
        external: client.external_free_function,
        watcher_data: switch (client.contents_or_fd) {
            .fd fd => ResultWatcherData{ fd: fd.file, dir_fd: fd.dir },
            .contents => .none,
        },
    };

    const event_loop = worker.ctx.loop();
    switch (event_loop.*) {
        .js jsc_event_loop => {
            jsc_event_loop.enqueueTaskConcurrent(jsc.ConcurrentTask.fromCallback(result, ParseTask.onComplete));
        },
        .mini => {
            event_loop.mini.enqueueTaskConcurrentWithExtraCtx(
                ParseTaskResult,
                BundleV2,
                result,
                BundleV2.onParseTaskComplete,
                .task,
            );
        },
    }
}
