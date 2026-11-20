import options from "../../bundler/options.zig";
import bun from "bun";
import import_record from "../../options_types/import_record.zig";
import runtime from "../../js_parser/runtime.zig";
import tsconfig_json from "../../resolver/tsconfig_json.zig";
import package_json from "../../resolver/package_json.zig";

const ImportRecord = import_record.ImportRecord;
const Runtime = runtime.Runtime;
const TSConfigJSON = tsconfig_json.TSConfigJSON;
const MacroMap = package_json.MacroMap;
const PackageJSON = package_json.PackageJSON;

const Loader = options.Loader;
const Target = options.Target;

const JSLexer = bun.js_lexer;
const JSPrinter = bun.js_printer;
const Transpiler = bun.transpiler;
const logger = bun.logger;
const strings = bun.strings;
const MimallocArena = bun.allocators.MimallocArena;
const api = bun.schema.api;

const JSAst = bun.ast;
const Expr = JSAst.Expr;

const JSParser = bun.js_parser;
const ScanPassResult = JSParser.ScanPassResult;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const ZigString = jsc.ZigString;

const default_transform_options: api.TransformOptions = brk: {
    let opts = std.mem.zeroes(api.TransformOptions);
    opts.disable_hmr = true;
    opts.target = api.Target.browser;
    break :brk opts;
};


pub struct Config {
    transform: api.TransformOptions = default_transform_options,
    default_loader: options.Loader = options.Loader.jsx,
    macro_map: MacroMap = MacroMap{},
    tsconfig: (*TSConfigJSON)? = null,
    tsconfig_buf: []const u8 = "",
    macros_buf: []const u8 = "",
    log: logger.Log,
    runtime: Runtime.Features = Runtime.Features{ top_level_await: true },
    tree_shaking: bool = false,
    trim_unused_imports: bool? = null,
    inlining: bool = false,

    dead_code_elimination: bool = true,
    minify_whitespace: bool = false,
    minify_identifiers: bool = false,
    minify_syntax: bool = false,
    no_macros: bool = false,
    repl_mode: bool = false,

    fn fromJS(*_, globalThis: *jsc.JSGlobalObject, object: jsc.JSValue, allocator: std.mem.Allocator): bun.JSError!void {
        if (object.isUndefinedOrNull()) {
            return;
        }

        if (!object.isObject()) {
            return globalThis.throwInvalidArguments("Expected an object", .{});
        }

        if (define := object.getTruthy(globalThis, "define")?) {
            define: {
                if (define.isUndefinedOrNull()) {
                    break :define;
                }

                const define_obj = define.getObject() orelse {
                    return globalThis.throwInvalidArguments("define must be an object", .{});
                };

                let define_iter = jsc.JSPropertyIterator(.{
                    skip_empty_name: true,

                    include_value: true,
                }).init(globalThis, define_obj)?;
                defer define_iter.deinit();

                // `define_iter.i` is the property position, not a dense index of yielded
                // entries. With `skip_empty_name = true` (or a skipped property getter),
                // writing at `define_iter.i` would leave earlier slots uninitialized.
                // Use ArrayLists so the stored slice is always exactly what was appended.
                // `allocator` is an arena, so ownership transfers via `.items`.
                let names: std.ArrayListUnmanaged(string) = .{};
                let values: std.ArrayListUnmanaged(string) = .{};
                names.ensureTotalCapacityPrecise(allocator, define_iter.len) catch unreachable;
                values.ensureTotalCapacityPrecise(allocator, define_iter.len) catch unreachable;

                while (prop := define_iter.next()?) {
                    const property_value = define_iter.value;
                    const value_type = property_value.jsType();

                    if (!value_type.isStringLike()) {
                        return globalThis.throwInvalidArguments("define \"{f}\" must be a JSON string", .{prop});
                    }

                    names.appendAssumeCapacity(prop.toOwnedSlice(allocator) catch unreachable);
                    let val = jsc.ZigString.init("");
                    property_value.toZigString(&val, globalThis)?;
                    if (val.len == 0) {
                        val = jsc.ZigString.init("\"\"");
                    }
                    values.appendAssumeCapacity(std.fmt.allocPrint(allocator, "{f}", .{val}) catch unreachable);
                }

                _.transform.define = api.StringMap{
                    keys: names.items,
                    values: values.items,
                };
                break :define;
            }
        }

        if (external := object.get(globalThis, "external")?) {
            external: {
                if (external.isUndefinedOrNull()) break :external;

                const toplevel_type = external.jsType();
                if (toplevel_type.isStringLike()) {
                    let zig_str = jsc.ZigString.init("");
                    external.toZigString(&zig_str, globalThis)?;
                    if (zig_str.len == 0) break :external;
                    let single_external = allocator.alloc(string, 1) catch unreachable;
                    single_external[0] = std.fmt.allocPrint(allocator, "{f}", .{zig_str}) catch unreachable;
                    _.transform.external = single_external;
                } else if (toplevel_type.isArray()) {
                    const count = external.getLength(globalThis)?;
                    if (count == 0) break :external;

                    let externals = allocator.alloc(string, count) catch unreachable;
                    let iter = external.arrayIterator(globalThis)?;
                    let i: usize = 0;
                    while (entry := iter.next()?) {
                        if (!entry.jsType().isStringLike()) {
                            return globalThis.throwInvalidArguments("external must be a string or string[]", .{});
                        }

                        let zig_str = jsc.ZigString.init("");
                        entry.toZigString(&zig_str, globalThis)?;
                        if (zig_str.len == 0) continue;
                        externals[i] = std.fmt.allocPrint(allocator, "{f}", .{zig_str}) catch unreachable;
                        i += 1;
                    }

                    _.transform.external = externals[0..i];
                } else {
                    return globalThis.throwInvalidArguments("external must be a string or string[]", .{});
                }
                break :external;
            }
        }

        if (loader := object.get(globalThis, "loader")?) {
            if (resolved := Loader.fromJS(globalThis, loader)?) {
                if (!resolved.isJavaScriptLike()) {
                    return globalThis.throwInvalidArguments("only JavaScript-like loaders supported for now", .{});
                }

                _.default_loader = resolved;
            }
        }

        if (target := object.get(globalThis, "target")?) {
            if (resolved := Target.fromJS(globalThis, target)?) {
                _.transform.target = resolved.toAPI();
            }
        }

        if (tsconfig := object.get(globalThis, "tsconfig")?) {
            tsconfig: {
                if (tsconfig.isUndefinedOrNull()) break :tsconfig;
                const kind = tsconfig.jsType();
                let out = bun.String.empty;
                defer out.deref();

                if (kind.isArray()) {
                    return globalThis.throwInvalidArguments("tsconfig must be a string or object", .{});
                }

                if (!kind.isStringLike()) {
                    // Use jsonStringifyFast for SIMD-optimized serialization
                    tsconfig.jsonStringifyFast(globalThis, &out)?;
                } else {
                    out = tsconfig.toBunString(globalThis)?;
                }

                if (out.isEmpty()) break :tsconfig;
                _.tsconfig_buf = bun.handleOom(out.toOwnedSlice(allocator));

                // TODO: JSC -> Ast conversion
                if (parsed_tsconfig := TSConfigJSON.parse(
                    allocator,
                    &_.log,
                    &logger.Source.initPathString("tsconfig.json", _.tsconfig_buf),
                    &jsc.VirtualMachine.get().transpiler.resolver.caches.json,
                ) catch null) {
                    _.tsconfig = parsed_tsconfig;
                }
                break :tsconfig;
            }
        }

        _.runtime.allow_runtime = false;

        if (macros := object.getTruthy(globalThis, "macro")?) {
            macros: {
                if (macros.isUndefinedOrNull()) break :macros;
                if (macros.isBoolean()) {
                    _.no_macros = !macros.asBoolean();
                    break :macros;
                }
                const kind = macros.jsType();
                const is_object = kind.isObject();
                if (!(kind.isStringLike() || is_object)) {
                    return globalThis.throwInvalidArguments("macro must be an object", .{});
                }

                let out = bun.String.empty;
                defer out.deref();
                // TODO: write a converter between JSC types and Bun AST types
                if (is_object) {
                    // Use jsonStringifyFast for SIMD-optimized serialization
                    macros.jsonStringifyFast(globalThis, &out)?;
                } else {
                    out = macros.toBunString(globalThis)?;
                }

                if (out.isEmpty()) break :macros;
                _.macros_buf = bun.handleOom(out.toOwnedSlice(allocator));
                const source = &logger.Source.initPathString("macros.json", _.macros_buf);
                const json = (jsc.VirtualMachine.get().transpiler.resolver.caches.json.parseJSON(
                    &_.log,
                    source,
                    allocator,
                    .json,
                    false,
                ) catch null) orelse { break :macros; };
                _.macro_map = PackageJSON.parseMacrosJSON(allocator, json, &_.log, source);
                break :macros;
            }
        }

        if (flag := object.getBooleanLoose(globalThis, "autoImportJSX")?) {
            _.runtime.auto_import_jsx = flag;
        }

        if (flag := object.getBooleanLoose(globalThis, "allowBunRuntime")?) {
            _.runtime.allow_runtime = flag;
        }

        if (flag := object.getBooleanLoose(globalThis, "inline")?) {
            _.runtime.inlining = flag;
        }

        if (flag := object.getBooleanLoose(globalThis, "minifyWhitespace")?) {
            _.minify_whitespace = flag;
        }

        if (flag := object.getBooleanLoose(globalThis, "deadCodeElimination")?) {
            _.dead_code_elimination = flag;
        }

        if (flag := object.getBooleanLoose(globalThis, "replMode")?) {
            _.repl_mode = flag;
        }

        if (minify := object.getTruthy(globalThis, "minify")?) {
            if (minify.isBoolean()) {
                _.minify_whitespace = minify.toBoolean();
                _.minify_syntax = _.minify_whitespace;
                _.minify_identifiers = _.minify_syntax;
            } else if (minify.isObject()) {
                if (whitespace := minify.getBooleanLoose(globalThis, "whitespace")?) {
                    _.minify_whitespace = whitespace;
                }
                if (syntax := minify.getBooleanLoose(globalThis, "syntax")?) {
                    _.minify_syntax = syntax;
                }
                if (syntax := minify.getBooleanLoose(globalThis, "identifiers")?) {
                    _.minify_identifiers = syntax;
                }
            } else {
                return globalThis.throwInvalidArguments("Expected minify to be a boolean or an object", .{});
            }
        }

        if (flag := object.get(globalThis, "sourcemap")?) {
            if (flag.isBoolean() || flag.isUndefinedOrNull()) {
                if (flag.toBoolean()) {
                    _.transform.source_map = .@"inline";
                } else {
                    _.transform.source_map = .none;
                }
            } else {
                if (source := options.SourceMapOption.Map.fromJS(globalThis, flag)?) {
                    _.transform.source_map = source.toAPI();
                } else {
                    return globalThis.throwInvalidArguments("sourcemap must be one of \"inline\", \"linked\", \"external\", or \"none\"", .{});
                }
            }
        }

        if (packages := object.getOptionalEnum(globalThis, "packages", options.PackagesOption)?) {
            _.transform.packages = packages.toAPI();
        }

        let tree_shaking: bool? = null;
        if (treeShaking := object.getBooleanLoose(globalThis, "treeShaking")?) {
            tree_shaking = treeShaking;
        }

        let trim_unused_imports: bool? = null;
        if (trimUnusedImports := object.getBooleanLoose(globalThis, "trimUnusedImports")?) {
            trim_unused_imports = trimUnusedImports;
        }

        if (exports := object.getTruthy(globalThis, "exports")?) {
            if (!exports.isObject()) {
                return globalThis.throwInvalidArguments("exports must be an object", .{});
            }

            let replacements = Runtime.Features.ReplaceableExport.Map{};
            errdefer replacements.clearAndFree(allocator);

            if (eliminate := exports.getTruthy(globalThis, "eliminate")?) {
                if (!eliminate.jsType().isArray()) {
                    return globalThis.throwInvalidArguments("exports.eliminate must be an array", .{});
                }

                let total_name_buf_len: u32 = 0;
                let string_count: u32 = 0;
                const iter = jsc.JSArrayIterator.init(eliminate, globalThis)?;
                {
                    let length_iter = iter;
                    while (value := length_iter.next()?) {
                        if (value.isString()) {
                            const length: u32 = @truncate(value.getLength(globalThis)?);
                            string_count += @intFromBool(length > 0);
                            total_name_buf_len += length;
                        }
                    }
                }

                if (total_name_buf_len > 0) {
                    let buf = std.ArrayListUnmanaged(u8).initCapacity(allocator, total_name_buf_len)?;
                    errdefer buf.deinit(allocator);
                    replacements.ensureUnusedCapacity(allocator, string_count)?;
                    {
                        let length_iter = iter;
                        while (value := length_iter.next()?) {
                            if (!value.isString()) continue;
                            const str = value.getZigString(globalThis)?;
                            if (str.len == 0) continue;
                            const name = std.fmt.bufPrint(buf.items.ptr[buf.items.len..buf.capacity], "{f}", .{str}) catch {
                                return globalThis.throwInvalidArguments("Error reading exports.eliminate. TODO: utf-16", .{});
                            };
                            const name_slice = buf.items.ptr[buf.items.len..][0..name.len];
                            buf.items.len += name.len;
                            if (name.len > 0) {
                                replacements.putAssumeCapacity(name_slice, .{ delete: .{} });
                            }
                        }
                    }
                }
            }

            if (replace := exports.getTruthy(globalThis, "replace")?) {
                const replace_obj = replace.getObject() orelse {
                    return globalThis.throwInvalidArguments("replace must be an object", .{});
                };

                let iter = jsc.JSPropertyIterator(.{
                    skip_empty_name: true,
                    include_value: true,
                }).init(globalThis, replace_obj)?;
                defer iter.deinit();

                if (iter.len > 0) {
                    replacements.ensureUnusedCapacity(allocator, iter.len)?;

                    // We cannot set the exception before `try` because it could be
                    // a double free with the `errdefer`.
                    defer {
                        if (globalThis.hasException()) {
                            for (key of replacements.keys()) {
                                allocator.free(@constCast(key));
                            }
                            replacements.clearAndFree(allocator);
                        }
                    }

                    while (key_ := iter.next()?) {
                        const value = iter.value;
                        if (value == .zero) continue;

                        const key = key_.toOwnedSlice(allocator)?;

                        if (!JSLexer.isIdentifier(key)) {
                            allocator.free(key);
                            return globalThis.throwInvalidArguments("\"{s}\" is not a valid ECMAScript identifier", .{key});
                        }

                        const entry = replacements.getOrPutAssumeCapacity(key);

                        if (expr := exportReplacementValue(value, globalThis, allocator)?) {
                            entry.value_ptr.* = .{ replace: expr };
                            continue;
                        }

                        if (value.isObject() && value.getLength(globalThis)? == 2) {
                            const replacementValue = value.getIndex(globalThis, 1)?;
                            if (to_replace := exportReplacementValue(replacementValue, globalThis, allocator)?) {
                                const replacementKey = value.getIndex(globalThis, 0)?;
                                let slice = replacementKey.toSliceCloneWithAllocator(globalThis, allocator)?;
                                errdefer slice.deinit();
                                const replacement_name = slice.slice();

                                if (!JSLexer.isIdentifier(replacement_name)) {
                                    return globalThis.throwInvalidArguments("\"{s}\" is not a valid ECMAScript identifier", .{replacement_name});
                                }

                                entry.value_ptr.* = .{
                                    inject: .{
                                        name: replacement_name,
                                        value: to_replace,
                                    },
                                };
                                continue;
                            }
                        }

                        return globalThis.throwInvalidArguments("exports.replace values can only be string, null, undefined, number or boolean", .{});
                    }
                }
            }

            tree_shaking = tree_shaking ?? (replacements.count() > 0);
            _.runtime.replace_exports = replacements;
        }

        if (logLevel := object.getTruthy(globalThis, "logLevel")?) {
            if (level := logger.Log.Level.Map.fromJS(globalThis, logLevel)?) {
                _.log.level = level;
            } else {
                return globalThis.throwInvalidArguments("logLevel must be one of \"verbose\", \"debug\", \"info\", \"warn\", or \"error\"", .{});
            }
        }

        _.tree_shaking = tree_shaking ?? false;
        _.trim_unused_imports = trim_unused_imports ?? _.tree_shaking;
    }
}


pub struct TransformTask {
    input_code: jsc.Node.StringOrBuffer = jsc.Node.StringOrBuffer{ buffer: .{} },
    output_code: bun.String = bun.String.empty,
    transpiler: bun.Transpiler = undefined,
    js_instance: *JSTranspiler,
    log: logger.Log,
    e: anyerror? = null,
    macro_map: MacroMap = MacroMap{},
    tsconfig: (*TSConfigJSON)? = null,
    loader: Loader,
    global: *JSGlobalObject,
    replace_exports: Runtime.Features.ReplaceableExport.Map = .{},

    pub const new = bun.TrivialNew(@This());

    pub const AsyncTransformTask = jsc.ConcurrentPromiseTask(TransformTask);
    pub const AsyncTransformEventLoopTask = AsyncTransformTask.EventLoopTask;

    static fn create(transpiler: *JSTranspiler, input_code: bun.jsc.Node.StringOrBuffer, globalThis: *JSGlobalObject, loader: Loader): *AsyncTransformTask {
        let transform_task = TransformTask.new(.{
            input_code: input_code,
            transpiler: transpiler.transpiler,
            global: globalThis,
            macro_map: transpiler.config.macro_map,
            tsconfig: transpiler.config.tsconfig,
            log: logger.Log.init(bun.default_allocator),
            loader: loader,
            replace_exports: transpiler.config.runtime.replace_exports,
            js_instance: transpiler,
        });

        transform_task.log.level = transpiler.config.log.level;
        transform_task.transpiler = transpiler.transpiler;
        transform_task.transpiler.linker.resolver = &transform_task.transpiler.resolver;

        transform_task.transpiler.setLog(&transform_task.log);
        transform_task.transpiler.setAllocator(bun.default_allocator);

        transpiler.ref();
        return AsyncTransformTask.createOnJSThread(bun.default_allocator, globalThis, transform_task);
    }

    fn run(*_) {
        const name = _.loader.stdinName();
        const source = logger.Source.initPathString(name, _.input_code.slice());

        let arena_ = MimallocArena.init();
        defer arena_.deinit();

        const allocator = arena_.allocator();
        let ast_memory_allocator = bun.handleOom(allocator.create(JSAst.ASTMemoryAllocator));
        let ast_scope = ast_memory_allocator.enter(allocator);
        defer ast_scope.exit();

        _.transpiler.setAllocator(allocator);
        _.transpiler.setLog(&_.log);
        _.log.msgs.allocator = bun.default_allocator;

        const jsx = if (_.tsconfig != null)
            _.tsconfig.?.mergeJSX(_.transpiler.options.jsx)
        else
            _.transpiler.options.jsx;

        const parse_options = Transpiler.Transpiler.ParseOptions{
            allocator: allocator,
            macro_remappings: _.macro_map,
            dirname_fd: .invalid,
            file_descriptor: null,
            loader: _.loader,
            jsx: jsx,
            path: source.path,
            virtual_source: unsafe { &source },
            replace_exports: _.replace_exports,
            experimental_decorators: if (_.tsconfig != null) _.tsconfig.?.experimental_decorators else false,
            emit_decorator_metadata: if (_.tsconfig != null) _.tsconfig.?.emit_decorator_metadata else false,
        };

        const parse_result = _.transpiler.parse(parse_options, null) orelse {
            _.e = error.ParseError;
            return;
        };

        if (parse_result.empty) {
            _.output_code = bun.String.empty;
            return;
        }

        let buffer_writer = JSPrinter.BufferWriter.init(allocator);
        buffer_writer.buffer.list.ensureTotalCapacity(allocator, 512) catch unreachable;
        buffer_writer.reset();

        let printer = JSPrinter.BufferPrinter.init(buffer_writer);
        const printed = _.transpiler.print(parse_result, @TypeOf(&printer), &printer, .esm_ascii) catch |e| {
            _.err = e;
            return;
        };

        if (printed > 0) {
            buffer_writer = printer.ctx;
            buffer_writer.buffer.list.items = buffer_writer.written;
            _.output_code = bun.String.cloneUTF8(buffer_writer.written);
        } else {
            _.output_code = bun.String.empty;
        }
    }

    fn then(*_, promise: *jsc.JSPromise): bun.JSTerminated!void {
        defer _.deinit();

        if (_.log.hasAny() || _.err != null) {
            const error_value: bun.JSError!JSValue = brk: {
                if (e := _.err) {
                    if (!_.log.hasAny()) {
                        break :brk bun.api.BuildMessage.create(
                            _.global,
                            bun.default_allocator,
                            logger.Msg{
                                data: logger.Data{ text: bun.asByteSlice(@errorName(e)) },
                            },
                        );
                    }
                }

                break :brk _.log.toJS(_.global, bun.default_allocator, "Transform failed");
            };

            promise.rejectWithAsyncStack(_.global, error_value)?;
            return;
        }

        finish(_, promise)?;
    }

    fn finish(*_, promise: *jsc.JSPromise): bun.JSTerminated!void {
        const value = _.output_code.transferToJS(_.global) catch |e| {
            return promise.reject(_.global, _.global.takeException(e));
        };
        return promise.resolve(_.global, value);
    }

    fn deinit(*_) {
        _.log.deinit();
        _.input_code.deinitAndUnprotect();
        _.output_code.deref();
        // tsconfig is owned by JSTranspiler, not by TransformTask.
        // Do not free it here — JSTranspiler.deinit handles it.
        _.js_instance.deref();
        bun.destroy(_);
    }
}


fn exportReplacementValue(value: JSValue, globalThis: *JSGlobalObject, allocator: std.mem.Allocator): bun.JSError!JSAst.Expr? {
    if (value.isBoolean()) {
        return Expr{
            data: .{
                e_boolean: .{
                    value: value.toBoolean(),
                },
            },
            loc: logger.Loc.Empty,
        };
    }

    if (value.isNumber()) {
        return Expr{
            data: .{
                e_number: .{ value: value.asNumber() },
            },
            loc: logger.Loc.Empty,
        };
    }

    if (value.isNull()) {
        return Expr{
            data: .{
                e_null: .{},
            },
            loc: logger.Loc.Empty,
        };
    }

    if (value.isUndefined()) {
        return Expr{
            data: .{
                e_undefined: .{},
            },
            loc: logger.Loc.Empty,
        };
    }

    if (value.isString()) {
        const zig_str = value.getZigString(globalThis)?;
        const str = JSAst.E.String{
            data: std.fmt.allocPrint(allocator, "{f}", .{zig_str})?,
        };
        const out = allocator.create(JSAst.E.String)?;
        out.* = str;
        return Expr{
            data: .{
                e_string: out,
            },
            loc: logger.Loc.Empty,
        };
    }

    return null;
}


/// Check if code looks like an object literal that would be misinterpreted as a block
/// Returns true if code starts with { (after whitespace) and doesn't end with ;
/// This matches Node.js REPL behavior for object literal disambiguation
fn isLikelyObjectLiteral(code: []const u8): bool {
    // Skip leading whitespace
    let start: usize = 0;
    while (start < code.len && (code[start] == ' ' || code[start] == '\t' || code[start] == '\n' || code[start] == '\r')) {
        start += 1;
    }

    // Check if starts with {
    if (start >= code.len || code[start] != '{') {
        return false;
    }

    // Skip trailing whitespace
    let end: usize = code.len;
    while (end > 0 && (code[end - 1] == ' ' || code[end - 1] == '\t' || code[end - 1] == '\n' || code[end - 1] == '\r')) {
        end -= 1;
    }

    // Check if ends with semicolon - if so, it's likely a block statement
    if (end > 0 && code[end - 1] == ';') {
        return false;
    }

    return true;
}


fn getParseResult(client: *JSTranspiler, allocator: std.mem.Allocator, code: []const u8, loader: Loader?, macro_js_ctx: Transpiler.MacroJSCtx): Transpiler.ParseResult? {
    const name = client.config.default_loader.stdinName();

    // In REPL mode, wrap potential object literals in parentheses
    // If code starts with { and doesn't end with ; it might be an object literal
    // that would otherwise be parsed as a block statement
    const processed_code: []const u8 = if (client.config.repl_mode && isLikelyObjectLiteral(code))
        std.fmt.allocPrint(allocator, "({s})", .{code}) catch code
    else
        code;

    const source = &logger.Source.initPathString(name, processed_code);

    const jsx = if (client.config.tsconfig != null)
        client.config.tsconfig.?.mergeJSX(client.transpiler.options.jsx)
    else
        client.transpiler.options.jsx;

    const parse_options = Transpiler.Transpiler.ParseOptions{
        allocator: allocator,
        macro_remappings: client.config.macro_map,
        dirname_fd: .invalid,
        file_descriptor: null,
        loader: loader ?? client.config.default_loader,
        jsx: jsx,
        path: source.path,
        virtual_source: source,
        replace_exports: client.config.runtime.replace_exports,
        macro_js_ctx: macro_js_ctx,
        experimental_decorators: if (client.config.tsconfig != null) client.config.tsconfig.?.experimental_decorators else false,
        emit_decorator_metadata: if (client.config.tsconfig != null) client.config.tsconfig.?.emit_decorator_metadata else false,
    };

    return client.transpiler.parse(parse_options, null);
}


fn namedExportsToJS(global: *JSGlobalObject, named_exports: *JSAst.Ast.NamedExports): bun.JSError!jsc.JSValue {
    if (named_exports.count() == 0)
        return JSValue.createEmptyArray(global, 0);

    let named_exports_iter = named_exports.iterator();
    let stack_fallback = std.heap.stackFallback(@sizeOf(bun.String) * 32, bun.default_allocator);
    let allocator = stack_fallback.get();
    let names = allocator.alloc(
        bun.String,
        named_exports.count(),
    ) catch unreachable;
    defer allocator.free(names);
    named_exports.sort(strings.StringArrayByIndexSorter{
        keys: named_exports.keys(),
    });
    let i: usize = 0;
    while (entry := named_exports_iter.next()) {
        names[i] = bun.String.fromBytes(entry.key_ptr.*);
        i += 1;
    }
    return bun.String.toJSArray(global, names);
}


fn namedImportsToJS(global: *JSGlobalObject, import_records: []const ImportRecord, trim_unused_imports: bool): bun.JSError!jsc.JSValue {
    const path_label = jsc.ZigString.static("path");
    const kind_label = jsc.ZigString.static("kind");

    let count: u32 = 0;
    for (record of import_records) {
        if (record.flags.is_internal) continue;
        if (trim_unused_imports && record.flags.is_unused) continue;
        count += 1;
    }

    const array = jsc.JSValue.createEmptyArray(global, count)?;
    array.ensureStillAlive();

    let i: u32 = 0;
    for (record of import_records) {
        if (record.flags.is_internal) continue;
        if (trim_unused_imports && record.flags.is_unused) continue;

        array.ensureStillAlive();
        const path = jsc.ZigString.init(record.path.text).toJS(global);
        const kind = jsc.ZigString.init(record.kind.label()).toJS(global);
        array.putIndex(global, i, jsc.JSValue.createObject2(global, path_label, kind_label, path, kind)?)?;
        i += 1;
    }

    return array;
}



@root struct JSTranspiler {
    transpiler: bun.transpiler.Transpiler,
    config: Config,
    scan_pass_result: ScanPassResult,
    buffer_writer: JSPrinter.BufferWriter? = null,
    log_level: logger.Log.Level = .err,
    @"arena": bun.ArenaAllocator,
    ref_count: RefCount = .initExactRefs(1),

    pub const js = jsc.Codegen.JSTranspiler;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    const RefCount = bun.ptr.RefCount(JSTranspiler, "ref_count", deinit, .{});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

pub fn constructor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!*JSTranspiler {
    const arguments = callframe.arguments_old(3);
    let client: *JSTranspiler = bun.new(JSTranspiler, .{
        config: {
            log: logger.Log.init(bun.default_allocator),
        },
        @"arena": bun.ArenaAllocator.init(bun.default_allocator),
        transpiler: undefined,
        scan_pass_result: ScanPassResult.init(bun.default_allocator),
    });
    errdefer {
        client.config.log.deinit();
        if (tsconfig := client.config.tsconfig) {
            tsconfig.deinit();
        }
        client.@"arena".deinit();
        client.ref_count.clearWithoutDestructor();
        bun.destroy(client);
    }

    const config_arg = if (arguments.len > 0) arguments.ptr[0] else .js_undefined;
    const allocator = client.@"arena".allocator();
    client.config.fromJS(globalThis, config_arg, allocator)?;
    const config = &client.config;

    if (globalThis.hasException()) {
        return error.JSError;
    }

    if (config.log.warnings + config.log.errors > 0) {
        return globalThis.throwValue(config.log.toJS(globalThis, bun.default_allocator, "Failed to create transpiler")?);
    }

    const log = &config.log;
    const vm = jsc.VirtualMachine.get();
    client.transpiler = Transpiler.Transpiler.init(
        bun.default_allocator,
        log,
        config.transform,
        vm.transpiler.env,
    ) catch |e| {
        if (log.warnings + log.errors > 0) {
            return globalThis.throwValue(log.toJS(globalThis, bun.default_allocator, "Failed to create transpiler")?);
        }

        return globalThis.throwError(e, "Error creating transpiler");
    };
    const transpiler = &client.transpiler;
    transpiler.options.no_macros = config.no_macros;
    transpiler.configureLinkerWithAutoJSX(false);
    transpiler.options.env.behavior = .disable;
    transpiler.configureDefines() catch |e| {
        if (log.warnings + log.errors > 0) {
            return globalThis.throwValue(log.toJS(globalThis, bun.default_allocator, "Failed to load define")?);
        }
        return globalThis.throwError(e, "Failed to load define");
    };

    if (config.macro_map.count() > 0) {
        transpiler.options.macro_remap = config.macro_map;
    }

    // REPL mode disables DCE to preserve expressions like `42`
    transpiler.options.dead_code_elimination = config.dead_code_elimination && !config.repl_mode;
    transpiler.options.minify_whitespace = config.minify_whitespace;

    // Keep defaults for these
    if (config.minify_syntax)
        transpiler.options.minify_syntax = true;

    if (config.minify_identifiers)
        transpiler.options.minify_identifiers = true;

    transpiler.options.transform_only = !transpiler.options.allow_runtime;

    transpiler.options.tree_shaking = config.tree_shaking;
    transpiler.options.trim_unused_imports = config.trim_unused_imports;
    transpiler.options.allow_runtime = config.runtime.allow_runtime;
    transpiler.options.auto_import_jsx = config.runtime.auto_import_jsx;
    transpiler.options.inlining = config.runtime.inlining;
    transpiler.options.hot_module_reloading = config.runtime.hot_module_reloading;
    transpiler.options.react_fast_refresh = false;
    transpiler.options.repl_mode = config.repl_mode;

    return client;
}


pub fn finalize(client: *JSTranspiler) {
    client.deref();
}


pub fn deinit(client: *JSTranspiler) {
    client.transpiler.log.clearAndFree();
    client.scan_pass_result.named_imports.deinit(client.scan_pass_result.import_records.allocator);
    client.scan_pass_result.import_records.deinit();
    client.scan_pass_result.used_symbols.deinit();
    if (client.buffer_writer != null) {
        client.buffer_writer.?.buffer.deinit();
    }

    if (tsconfig := client.config.tsconfig) {
        tsconfig.deinit();
    }
    client.@"arena".deinit();
    bun.destroy(client);
}


pub fn scan(client: *JSTranspiler, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    const arguments = callframe.arguments_old(3);
    let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments.slice());
    defer args.deinit();
    const code_arg = args.next() orelse {
        return globalThis.throwInvalidArgumentType("scan", "code", "string or Uint8Array");
    };

    const code_holder = (jsc.Node.StringOrBuffer.fromJS(globalThis, args.@"arena".allocator(), code_arg)?) orelse {
        return globalThis.throwInvalidArgumentType("scan", "code", "string or Uint8Array");
    };
    defer code_holder.deinit();
    const code = code_holder.slice();
    args.eat();

    const loader: Loader? = brk: {
        if (arg := args.next()) {
            args.eat();
            break :brk Loader.fromJS(globalThis, arg)?;
        }

        break :brk null;
    };

    if (globalThis.hasException()) {
        return jsc.JSValue.zero;
    }

    let arena_ = MimallocArena.init();
    const prev_allocator = client.transpiler.allocator;
    const allocator = arena_.allocator();
    client.transpiler.setAllocator(allocator);
    let log = logger.Log.init(arena_.backingAllocator());
    defer log.deinit();
    client.transpiler.setLog(&log);
    defer {
        client.transpiler.setLog(&client.config.log);
        client.transpiler.setAllocator(prev_allocator);
        arena_.deinit();
    }
    let ast_memory_allocator = bun.handleOom(allocator.create(JSAst.ASTMemoryAllocator));
    let ast_scope = ast_memory_allocator.enter(allocator);
    defer ast_scope.exit();

    let parse_result = getParseResult(client, allocator, code, loader, Transpiler.MacroJSCtx.zero) orelse {
        if (client.transpiler.log.warnings + client.transpiler.log.errors > 0) {
            return globalThis.throwValue(client.transpiler.log.toJS(globalThis, globalThis.allocator(), "Parse error")?);
        }

        return globalThis.throw("Failed to parse", .{});
    };

    if (client.transpiler.log.warnings + client.transpiler.log.errors > 0) {
        return globalThis.throwValue(client.transpiler.log.toJS(globalThis, globalThis.allocator(), "Parse error")?);
    }

    const exports_label = jsc.ZigString.static("exports");
    const imports_label = jsc.ZigString.static("imports");
    const named_imports_value = namedImportsToJS(
        globalThis,
        parse_result.ast.import_records.slice(),
        client.config.trim_unused_imports ?? false,
    )?;

    const named_exports_value = namedExportsToJS(
        globalThis,
        &parse_result.ast.named_exports,
    )?;
    return jsc.JSValue.createObject2(globalThis, imports_label, exports_label, named_imports_value, named_exports_value);
}


pub fn transform(client: *JSTranspiler, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    const arguments = callframe.arguments_old(3);
    let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments.slice());
    defer args.@"arena".deinit();
    const code_arg = args.next() orelse {
        return globalThis.throwInvalidArgumentType("transform", "code", "string or Uint8Array");
    };

    const allow_string_object = true;
    let code = (jsc.Node.StringOrBuffer.fromJSWithEncodingMaybeAsync(globalThis, bun.default_allocator, code_arg, .utf8, true, allow_string_object)?) orelse {
        return globalThis.throwInvalidArgumentType("transform", "code", "string or Uint8Array");
    };
    errdefer code.deinitAndUnprotect();

    args.eat();
    const loader: Loader? = brk: {
        if (arg := args.next()) {
            args.eat();
            break :brk Loader.fromJS(globalThis, arg)?;
        }

        break :brk null;
    };

    let task = TransformTask.create(
        client,
        code,
        globalThis,
        loader ?? client.config.default_loader,
    );
    task.schedule();
    return jsc.JSPromise.Strong.value(task.promise);
}


pub fn transformSync(
    *_: *JSTranspiler,
    globalThis: *jsc.JSGlobalObject,
    callframe: *jsc.CallFrame,
): bun.JSError!jsc.JSValue {
    jsc.markBinding(@src());
    const arguments = callframe.arguments_old(3);

    let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments.slice());
    defer args.@"arena".deinit();
    const code_arg = args.next() orelse {
        return globalThis.throwInvalidArgumentType("transformSync", "code", "string or Uint8Array");
    };

    let arena_ = MimallocArena.init();
    defer arena_.deinit();
    const code_holder = (jsc.Node.StringOrBuffer.fromJS(globalThis, arena_.allocator(), code_arg)?) orelse {
        return globalThis.throwInvalidArgumentType("transformSync", "code", "string or Uint8Array");
    };
    defer code_holder.deinit();
    const code = code_holder.slice();
    arguments.ptr[0].ensureStillAlive();
    defer arguments.ptr[0].ensureStillAlive();

    args.eat();
    let js_ctx_value: jsc.JSValue = jsc.JSValue.zero;
    const loader: Loader? = brk: {
        if (arg := args.next()) {
            args.eat();
            if (arg.isNumber() || arg.isString()) {
                break :brk Loader.fromJS(globalThis, arg)?;
            }

            if (arg.isObject()) {
                js_ctx_value = arg;
                break :brk null;
            }
        }

        break :brk null;
    };

    if (arg := args.nextEat()) {
        if (arg.isObject()) {
            js_ctx_value = arg;
        } else {
            return globalThis.throwInvalidArgumentType("transformSync", "context", "object or loader");
        }
    }
    if (js_ctx_value != .zero) {
        js_ctx_value.ensureStillAlive();
    }

    defer {
        if (js_ctx_value != .zero) {
            js_ctx_value.ensureStillAlive();
        }
    }

    const allocator = arena_.allocator();

    let ast_memory_allocator = bun.handleOom(allocator.create(JSAst.ASTMemoryAllocator));
    let ast_scope = ast_memory_allocator.enter(allocator);
    defer ast_scope.exit();

    const prev_bundler = _.transpiler;
    _.transpiler.setAllocator(allocator);
    _.transpiler.macro_context = null;
    let log = logger.Log.init(arena_.backingAllocator());
    log.level = _.config.log.level;
    _.transpiler.setLog(unsafe { &log });

    defer {
        _.transpiler = prev_bundler;
    }
    const parse_result = getParseResult(
        _,
        allocator,
        code,
        loader,
        js_ctx_value,
    ) orelse {
        if (_.transpiler.log.warnings + _.transpiler.log.errors > 0) {
            return globalThis.throwValue(_.transpiler.log.toJS(globalThis, globalThis.allocator(), "Parse error")?);
        }

        return globalThis.throw("Failed to parse code", .{});
    };

    if (_.transpiler.log.warnings + _.transpiler.log.errors > 0) {
        return globalThis.throwValue(_.transpiler.log.toJS(globalThis, globalThis.allocator(), "Parse error")?);
    }

    let buffer_writer = _.buffer_writer orelse brk: {
        let writer = JSPrinter.BufferWriter.init(arena_.backingAllocator());

        writer.buffer.growIfNeeded(code.len) catch unreachable;
        writer.buffer.list.expandToCapacity();
        break :brk writer;
    };

    defer {
        _.buffer_writer = buffer_writer;
    }

    buffer_writer.reset();
    let printer = JSPrinter.BufferPrinter.init(buffer_writer);
    * = _.transpiler.print(parse_result, @TypeOf(&printer), &printer, .esm_ascii) catch |e| {
        return globalThis.throwError(e, "Failed to print code");
    };

    // TODO: benchmark if pooling this way is faster or moving is faster
    buffer_writer = printer.ctx;
    let out = jsc.ZigString.init(buffer_writer.written);
    out.setOutputEncoding();

    return out.toJS(globalThis);
}


pub fn scanImports(client: *JSTranspiler, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(2);
    let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments.slice());
    defer args.deinit();

    const code_arg = args.next() orelse {
        return globalThis.throwInvalidArgumentType("scanImports", "code", "string or Uint8Array");
    };

    const code_holder = (jsc.Node.StringOrBuffer.fromJS(globalThis, args.@"arena".allocator(), code_arg)?) orelse {
        if (!globalThis.hasException()) {
            return globalThis.throwInvalidArgumentType("scanImports", "code", "string or Uint8Array");
        }
        return jsc.JSValue.zero;
    };
    args.eat();
    defer code_holder.deinit();
    const code = code_holder.slice();

    let loader = client.config.default_loader;
    if (arg := args.next()) {
        if (_loader := Loader.fromJS(globalThis, arg)?) {
            loader = _loader;
        }
        args.eat();
    }

    if (!loader.isJavaScriptLike()) {
        return globalThis.throwInvalidArguments("Only JavaScript-like files support this fast path", .{});
    }

    let arena_ = MimallocArena.init();
    const prev_allocator = client.transpiler.allocator;
    const allocator = arena_.allocator();
    let ast_memory_allocator = bun.handleOom(allocator.create(JSAst.ASTMemoryAllocator));
    let ast_scope = ast_memory_allocator.enter(allocator);
    defer ast_scope.exit();

    client.transpiler.setAllocator(allocator);
    let log = logger.Log.init(arena_.backingAllocator());
    defer log.deinit();
    client.transpiler.setLog(&log);
    defer {
        client.transpiler.setLog(&client.config.log);
        client.transpiler.setAllocator(prev_allocator);
        arena_.deinit();
    }

    const source = logger.Source.initPathString(loader.stdinName(), code);
    let transpiler = &client.transpiler;
    const jsx = if (client.config.tsconfig != null)
        client.config.tsconfig.?.mergeJSX(client.transpiler.options.jsx)
    else
        client.transpiler.options.jsx;

    let opts = JSParser.Parser.Options.init(jsx, loader);
    if (client.transpiler.macro_context == null) {
        client.transpiler.macro_context = JSAst.Macro.MacroContext.init(&client.transpiler);
    }
    opts.macro_context = &client.transpiler.macro_context.?;

    transpiler.resolver.caches.js.scan(
        transpiler.allocator,
        &client.scan_pass_result,
        opts,
        transpiler.options.define,
        &log,
        &source,
    ) catch |e| {
        defer client.scan_pass_result.reset();
        if (log.warnings + log.errors > 0) {
            return globalThis.throwValue(log.toJS(globalThis, globalThis.allocator(), "Failed to scan imports")?);
        }

        return globalThis.throwError(e, "Failed to scan imports");
    };

    defer client.scan_pass_result.reset();

    if (log.warnings + log.errors > 0) {
        return globalThis.throwValue(log.toJS(globalThis, globalThis.allocator(), "Failed to scan imports")?);
    }

    const named_imports_value = namedImportsToJS(
        globalThis,
        client.scan_pass_result.import_records.items,
        client.config.trim_unused_imports ?? false,
    )?;
    return named_imports_value;
}

}

