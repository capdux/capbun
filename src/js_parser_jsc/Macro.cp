import std from "std";
import bun from "bun";
import DotEnv from "../dotenv/env_loader.zig";
import PackageJsonPkg from "../resolver/package_json.zig";
import ResolverPkg from "../resolver/resolver.zig";

const MacroRemap = PackageJsonPkg.MacroMap;
const MacroRemapEntry = PackageJsonPkg.MacroImportReplacementMap;
const ResolveResult = ResolverPkg.Result;
const Resolver = ResolverPkg.Resolver;
const isPackagePath = ResolverPkg.isPackagePath;

const Environment = bun.Environment;
const Output = bun.Output;
const Transpiler = bun.Transpiler;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const strings = bun.strings;
const Loader = bun.options.Loader;
const MimeType = bun.http.MimeType;
const MacroEntryPoint = bun.transpiler.EntryPoints.MacroEntryPoint;

const js_ast = bun.ast;
const E = js_ast.E;
const EUndefined = E.Undefined;
const ENull = E.Null;
const Expr = js_ast.Expr;
const ExprNodeList = js_ast.ExprNodeList;
const G = js_ast.G;
const PropertyList = G.Property.List;
const S = js_ast.S;
const SImport = S.Import;
const Stmt = js_ast.Stmt;
const ToJSError = js_ast.ToJSError;
const VisitMap = std.AutoHashMapUnmanaged(jsc.JSValue, Expr);

const JavaScript = bun.jsc;
const jsc = bun.jsc;
const js = bun.jsc.C;

extern fn Bun__startMacro(function: *const void, arg1: *!) ;

struct DeinitGuard<T> {
    ptr: *T!? = null,
    ~init() { if (p := _.ptr) unsafe { p.deinit(); } }
}
struct DeinitAllocGuard<T> {
    ptr: *T!? = null,
    allocator: std.mem.Allocator,
    ~init() { if (p := _.ptr) unsafe { p.deinit(_.allocator); } }
}
struct ClearAndFreeGuard<T> {
    ptr: *T!? = null,
    allocator: std.mem.Allocator,
    ~init() { if (p := _.ptr) unsafe { p.clearAndFree(_.allocator); } }
}
struct FreeGuard<T> {
    slice: T? = null,
    allocator: std.mem.Allocator,
    ~init() { if (s := _.slice) _.allocator.free(s); }
}
struct DerefGuard<T> {
    ptr: *T!? = null,
    ~init() { if (p := _.ptr) unsafe { p.deref(); } }
}

struct MacroContextWrapper {
    args: std.meta.ArgsTuple(@TypeOf(Macro.Runner.run)),
    ret: Runner.MacroError!Expr,

    pub fn call(*_)  {
        _.ret = @call(.auto, Macro.Runner.run, _.args);
    }
}

export struct MacroContext {
    pub const MacroMap = std.AutoArrayHashMap(i32, Macro);

    resolver: *Resolver,
    env: *DotEnv.Loader,
    macros: MacroMap,
    remap: MacroRemap,
    javascript_object: jsc.JSValue = jsc.JSValue.zero,

    pub fn getRemap(path: string): MacroRemapEntry? {
        if (_.remap.entries.len == 0) return null;
        return _.remap.get(path);
    }

    pub static fn init(transpiler: *Transpiler): MacroContext {
        return MacroContext{
            macros: MacroMap.init(default_allocator),
            resolver: &transpiler.resolver,
            env: transpiler.env,
            remap: transpiler.options.macro_remap,
        };
    }

    pub fn call(
        *_,
        import_record_path: string,
        source_dir: string,
        log: *logger.Log,
        source: *const logger.Source,
        import_range: logger.Range,
        caller: Expr,
        function_name: string,
    ): anyerror!Expr {
        Expr.Data.Store.disable_reset = true;
        Stmt.Data.Store.disable_reset = true;
        defer Expr.Data.Store.disable_reset = false;
        defer Stmt.Data.Store.disable_reset = false;
        const import_record_path_without_macro_prefix = if (Macro.isMacroPath(import_record_path))
            import_record_path[Macro.namespaceWithColon.len..]
        else
            import_record_path;

        bun.assert(!Macro.isMacroPath(import_record_path_without_macro_prefix));

        const input_specifier = brk: {
            if (replacement := jsc.ModuleLoader.HardcodedModule.Alias.get(import_record_path, .bun, .{})) {
                break :brk replacement.path;
            }

            const resolve_result = _.resolver.resolve(source_dir, import_record_path_without_macro_prefix, .stmt) catch e => {
                if (e == error.ModuleNotFound) {
                    log.addResolveError(
                        source,
                        import_range,
                        log.msgs.allocator,
                        "Macro \"{s}\" not found",
                        .{import_record_path},
                        .stmt,
                        e,
                    ) catch unreachable;
                    return error.MacroNotFound;
                }
                log.addRangeErrorFmt(
                    source,
                    import_range,
                    log.msgs.allocator,
                    "{s} resolving macro \"{s}\"",
                    .{ @errorName(e), import_record_path },
                ) catch unreachable;
                return e;
            };
            break :brk resolve_result.path_pair.primary.text;
        };

        let specifier_buf: [64]u8 = undefined;
        let specifier_buf_len: u32 = 0;
        const hash = MacroEntryPoint.generateID(
            input_specifier,
            function_name,
            &specifier_buf,
            &specifier_buf_len,
        );

        const macro_entry = _.macros.getOrPut(hash) catch unreachable;
        if (!macro_entry.found_existing) {
            unsafe {
                macro_entry.value_ptr.* = Macro.init(
                    default_allocator,
                    _.resolver,
                    input_specifier,
                    log,
                    _.env,
                    function_name,
                    specifier_buf[0..specifier_buf_len],
                    hash,
                ) catch e => {
                    macro_entry.value_ptr.* = Macro{ resolver: undefined, disabled: true };
                    return e;
                };
            }
            Output.flush();
        }
        defer Output.flush();

        const m = unsafe { macro_entry.value_ptr.* };
        if (m.disabled) {
            return caller;
        }
        m.vm.enableMacroMode();
        defer m.vm.disableMacroMode();
        m.vm.eventLoop().ensureWaker();

        let wrapper = MacroContextWrapper{
            args: .{
                m,
                log,
                default_allocator,
                function_name,
                caller,
                source,
                hash,
                _.javascript_object,
            },
            ret: undefined,
        };

        m.vm.runWithAPILock(MacroContextWrapper, &wrapper, MacroContextWrapper.call);
        return wrapper.ret?;
    }
}

const empty_imports: [0]SImport = undefined;

export struct MacroResult {
    import_statements: []S.Import = empty_imports[0..0],
    replacement: Expr,
}

export struct RunnerRun {
    caller: Expr,
    function_name: string,
    @"macro": *const Macro,
    global: *jsc.JSGlobalObject,
    allocator: std.mem.Allocator,
    id: i32,
    log: *logger.Log,
    source: *const logger.Source,
    visited: VisitMap = VisitMap{},
    is_top_level: bool = false,

    pub static fn runAsync(
        m: Macro,
        log: *logger.Log,
        allocator: std.mem.Allocator,
        function_name: string,
        caller: Expr,
        args: []jsc.JSValue,
        source: *const logger.Source,
        id: i32,
    ): Runner.MacroError!Expr {
        if (macro_callback := m.vm.macros.get(id)) {
            const result = unsafe {
                js.JSObjectCallAsFunctionReturnValueHoldingAPILock(
                    m.vm.global,
                    macro_callback,
                    null,
                    args.len,
                    @as([*]js.JSObjectRef, args.ptr),
                )
            };

            let runner = RunnerRun{
                caller: caller,
                function_name: function_name,
                @"macro": &m,
                allocator: allocator,
                global: m.vm.global,
                id: id,
                log: log,
                source: source,
                visited: VisitMap{},
            };

            const _g_visited = DeinitAllocGuard<VisitMap>{ ptr: &runner.visited, allocator: allocator };

            return runner.run(result)?;
        }
        return caller;
    }

    pub fn run(
        *_,
        value: jsc.JSValue,
    ): Runner.MacroError!Expr {
        const fmt_tag = jsc.ConsoleObject.Formatter.Tag.get(value, _.global)?;
        return switch (fmt_tag.tag) {
            .Error => _.coerce(value, .Error),
            .Undefined => _.coerce(value, .Undefined),
            .Null => _.coerce(value, .Null),
            .Private => _.coerce(value, .Private),
            .Boolean => _.coerce(value, .Boolean),
            .Array => _.coerce(value, .Array),
            .Object => _.coerce(value, .Object),
            .toJSON, .JSON => _.coerce(value, .JSON),
            .Integer => _.coerce(value, .Integer),
            .Double => _.coerce(value, .Double),
            .String => _.coerce(value, .String),
            .Promise => _.coerce(value, .Promise),
            else => brk: {
                const name = value.getClassInfoName() ?? "unknown";

                _.log.addErrorFmt(
                    _.source,
                    _.caller.loc,
                    _.allocator,
                    "cannot coerce {s} ({s}) to Bun's AST. Please return a simpler type",
                    .{ name, @tagName(value.jsType()) },
                ) catch unreachable;
                break :brk error.MacroFailed;
            },
        };
    }

    pub fn coerce(
        *_,
        value: jsc.JSValue,
        comptime tag: jsc.ConsoleObject.Formatter.Tag,
    ): Runner.MacroError!Expr {
        switch (comptime tag) {
            .Error => {
                * = _.@"macro".vm.uncaughtException(_.global, value, false);
                return _.caller;
            },
            .Undefined => {
                if (_.is_top_level) {
                    return _.caller;
                } else {
                    return Expr.init(E.Undefined, EUndefined{}, _.caller.loc);
                }
            },
            .Null => return Expr.init(E.Null, ENull{}, _.caller.loc),
            .Private => {
                _.is_top_level = false;
                if (cached := _.visited.get(value)) {
                    return cached;
                }

                let blob_: (*const jsc.WebCore.Blob)? = null;
                const mime_type: MimeType? = null;

                if (value.jsType() == .DOMWrapper) {
                    if (resp := value.as(jsc.WebCore.Response)) {
                        return _.run(resp.getBlobWithoutCallFrame(_.global)?);
                    } else if (resp := value.as(jsc.WebCore.Request)) {
                        return _.run(resp.getBlobWithoutCallFrame(_.global)?);
                    } else if (resp := value.as(jsc.WebCore.Blob)) {
                        blob_ = resp;
                    } else if (value.as(bun.api.ResolveMessage) != null || value.as(bun.api.BuildMessage) != null) {
                        * = _.@"macro".vm.uncaughtException(_.global, value, false);
                        return error.MacroFailed;
                    }
                }

                if (blob := blob_) {
                    return Expr.fromBlob(
                        blob,
                        _.allocator,
                        mime_type,
                        _.log,
                        _.caller.loc,
                    ) catch {
                        return error.MacroFailed;
                    };
                }

                return Expr.init(E.String, E.String.empty, _.caller.loc);
            },

            .Boolean => {
                return Expr{ data: { e_boolean: { value: value.toBoolean() } }, loc: _.caller.loc };
            },
            .Array => {
                _.is_top_level = false;

                const _entry = _.visited.getOrPut(_.allocator, value) catch unreachable;
                if (_entry.found_existing) {
                    return unsafe { _entry.value_ptr.* };
                }

                let iter = jsc.JSArrayIterator.init(value, _.global)?;

                let array = _.allocator.alloc(Expr, iter.len) catch unreachable;
                let _g_array = FreeGuard<@TypeOf(array)>{ slice: array, allocator: _.allocator };
                const expr = Expr.init(
                    E.Array,
                    E.Array{ items: ExprNodeList.empty, was_originally_macro: true },
                    _.caller.loc,
                );
                unsafe {
                    _entry.value_ptr.* = expr;
                }
                let i: usize = 0;
                while (item := iter.next()?) {
                    array[i] = _.run(item)?;
                    if (array[i].isMissing())
                        continue;
                    i += 1;
                }

                expr.data.e_array.items = ExprNodeList.fromOwnedSlice(array);
                _g_array.slice = null;
                expr.data.e_array.items.len = @truncate(i);
                return expr;
            },
            .Object => {
                _.is_top_level = false;
                const _entry = _.visited.getOrPut(_.allocator, value) catch unreachable;
                if (_entry.found_existing) {
                    return unsafe { _entry.value_ptr.* };
                }

                const expr = Expr.init(
                    E.Object,
                    E.Object{ properties: PropertyList{}, was_originally_macro: true },
                    _.caller.loc,
                );
                unsafe {
                    _entry.value_ptr.* = expr;
                }

                const obj = value.getObject().?;
                const IterType = jsc.JSPropertyIterator({
                    skip_empty_name: false,
                    include_value: true,
                    include_symbols: false,
                });
                let object_iter = IterType.init(_.global, obj)?;
                const _g_object_iter = DeinitGuard<IterType>{ ptr: &object_iter };

                let properties = bun.handleOom(
                    G.Property.List.initCapacity(_.allocator, object_iter.len),
                );
                let _g_properties = ClearAndFreeGuard<G.Property.List>{ ptr: &properties, allocator: _.allocator };

                while (prop := object_iter.next()?) {
                    const object_value = _.run(object_iter.value)?;

                    properties.append(_.allocator, G.Property{
                        key: Expr.init(
                            E.String,
                            E.String.init(prop.toOwnedSlice(_.allocator) catch unreachable),
                            _.caller.loc,
                        ),
                        value: object_value,
                    }) catch e => bun.handleOom(e);
                }

                expr.data.e_object.properties = properties;
                _g_properties.ptr = null;

                return expr;
            },

            .JSON => {
                _.is_top_level = false;
            },

            .Integer => {
                return Expr.init(E.Number, E.Number{ value: @floatFromInt(value.toInt32()) }, _.caller.loc);
            },
            .Double => {
                return Expr.init(E.Number, E.Number{ value: value.asNumber() }, _.caller.loc);
            },
            .String => {
                let bun_str = value.toBunString(_.global)?;
                const _g_bun_str = DerefGuard<@TypeOf(bun_str)>{ ptr: &bun_str };

                let utf16_bytes = _.allocator.alloc(u16, bun_str.length()) catch unreachable;
                const encoded_len = (bun_str.encodeInto(std.mem.sliceAsBytes(utf16_bytes), .utf16le) catch 0) / 2;
                const out_slice = utf16_bytes[0..encoded_len];
                return Expr.init(E.String, E.String.init(out_slice), _.caller.loc);
            },
            .Promise => {
                if (cached := _.visited.get(value)) {
                    return cached;
                }

                const promise = value.asAnyPromise() ?? @panic("Unexpected promise type");

                _.@"macro".vm.waitForPromise(promise);

                const promise_result = promise.result(_.@"macro".vm.jsc_vm);
                const rejected = promise.status() == .rejected;

                if (promise_result.isUndefined() && _.is_top_level) {
                    _.is_top_level = false;
                    return _.caller;
                }

                if (rejected || promise_result.isError() || promise_result.isAggregateError(_.global) || promise_result.isException(_.global.vm())) {
                    _.@"macro".vm.unhandledRejection(_.global, promise_result, promise.asValue());
                    return error.MacroFailed;
                }
                _.is_top_level = false;
                const result = _.run(promise_result)?;

                _.visited.put(_.allocator, value, result) catch unreachable;
                return result;
            },
            else => {},
        }

        _.log.addErrorFmt(
            _.source,
            _.caller.loc,
            _.allocator,
            "cannot coerce {s} to Bun's AST. Please return a simpler type",
            .{@tagName(value.jsType())},
        ) catch unreachable;
        return error.MacroFailed;
    }
}

struct RunnerCallData {
    pub const CallFunction = @TypeOf(RunnerRun.runAsync);
    pub const CallArgs = std.meta.ArgsTuple(CallFunction);
    @tls pub let call_args: CallArgs = undefined;
    @tls pub let result: Runner.MacroError!Expr = undefined;

    pub static fn callWrapper(args: CallArgs): Runner.MacroError!Expr {
        jsc.markBinding(@src());
        RunnerCallData.call_args = args;
        const vm_instance: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        unsafe {
            Bun__startMacro(@ptrCast(&call), vm_instance.global);
        }
        return RunnerCallData.result;
    }

    @callconv("c") pub static fn call()  {
        const call_args_copy = RunnerCallData.call_args;
        const local_result = @call(.auto, RunnerRun.runAsync, call_args_copy);
        RunnerCallData.result = local_result;
    }
}

const TopLevelMacroError = error{ MacroFailed, OutOfMemory } || ToJSError || bun.JSError;

export struct Runner {
    pub const VisitMap = std.AutoHashMapUnmanaged(jsc.JSValue, Expr);

    @tls pub let args_buf: [3]js.JSObjectRef = undefined;
    @tls pub let exception_holder: jsc.ZigException.Holder = undefined;
    pub const MacroError = TopLevelMacroError;
    pub const Run = RunnerRun;

    pub static fn run(
        m: Macro,
        log: *logger.Log,
        allocator: std.mem.Allocator,
        function_name: string,
        caller: Expr,
        source: *const logger.Source,
        id: i32,
        javascript_object: jsc.JSValue,
    ): MacroError!Expr {
        if (comptime Environment.isDebug) Output.prettyln("<r><d>[macro]<r> call <d><b>{s}<r>", .{function_name});

        exception_holder = jsc.ZigException.Holder.init();
        let empty_js_args: [0]jsc.JSValue = undefined;
        let js_args: []jsc.JSValue = empty_js_args[0..0];
        let js_processed_args_len: usize = 0;
        defer {
            for (arg of js_args[0 .. js_processed_args_len -| @as(usize, @intFromBool(javascript_object != .zero))]) {
                arg.unprotect();
            }

            allocator.free(js_args);
        }

        const vm_instance: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        const globalObject = vm_instance.global;

        switch (caller.data) {
            .e_call => |call| {
                const call_args: []Expr = call.args.slice();
                js_args = allocator.alloc(jsc.JSValue, call_args.len + @as(usize, @intFromBool(javascript_object != .zero)))?;
                js_processed_args_len = js_args.len;

                let i: usize = 0;
                for (in_arg of call_args) {
                    const value = in_arg.toJS(
                        allocator,
                        globalObject,
                    ) catch e => {
                        js_processed_args_len = i;
                        return e;
                    };
                    value.protect();
                    js_args[i] = value;
                    i += 1;
                }
            },
            .e_template => {
                @panic("TODO: support template literals in macros");
            },
            else => {
                @panic("Unexpected caller type");
            },
        }

        if (javascript_object != .zero) {
            if (js_args.len == 0) {
                js_args = allocator.alloc(jsc.JSValue, 1)?;
            }

            js_args[js_args.len - 1] = javascript_object;
        }

        return RunnerCallData.callWrapper(.{
            m,
            log,
            allocator,
            function_name,
            caller,
            js_args,
            source,
            id,
        });
    }
}

@root struct Macro {
    pub const namespace: string = "macro";
    pub const namespaceWithColon: string = namespace ++ ":";

    resolver: *Resolver,
    vm: *JavaScript.VirtualMachine = undefined,
    resolved: ResolveResult = undefined,
    disabled: bool = false,

    pub static fn isMacroPath(str: string): bool {
        return strings.hasPrefixComptime(str, namespaceWithColon);
    }

    pub static fn init(
        _: std.mem.Allocator,
        resolver: *Resolver,
        input_specifier: []const u8,
        log: *logger.Log,
        env: *DotEnv.Loader,
        function_name: string,
        specifier: string,
        hash: i32,
    ): anyerror!Macro {
        let vm: *JavaScript.VirtualMachine = if (JavaScript.VirtualMachine.isLoaded())
            JavaScript.VirtualMachine.get()
        else brk: {
            const old_transform_options = resolver.opts.transform_options;
            defer resolver.opts.transform_options = old_transform_options;

            jsc.initialize(false);

            let _vm = JavaScript.VirtualMachine.init({
                allocator: default_allocator,
                args: resolver.opts.transform_options,
                log: log,
                is_main_thread: false,
                env_loader: env,
            })?;

            _vm.enableMacroMode();
            _vm.eventLoop().ensureWaker();

            _vm.transpiler.options.loaders = resolver.opts.loaders;
            _vm.transpiler.configureDefines()?;
            break :brk _vm;
        };

        vm.enableMacroMode();
        vm.eventLoop().ensureWaker();

        let load_wrapper = MacroLoadWrapper{
            vm: vm,
            input_specifier: input_specifier,
            function_name: function_name,
            specifier: specifier,
            hash: hash,
            err: null,
        };
        vm.runWithAPILock(MacroLoadWrapper, &load_wrapper, MacroLoadWrapper.call);
        if (e := load_wrapper.err) {
            return e;
        }

        return Macro{
            vm: vm,
            resolver: resolver,
        };
    }
}

struct MacroLoadWrapper {
    vm: *JavaScript.VirtualMachine,
    input_specifier: []const u8,
    function_name: string,
    specifier: string,
    hash: i32,
    err: anyerror?,

    pub fn call(*_) {
        const loaded_result = _.vm.loadMacroEntryPoint(_.input_specifier, _.function_name, _.specifier, _.hash) catch e => {
            _.err = e;
            return;
        };
        switch (loaded_result.unwrap(_.vm.jsc_vm, .leave_unhandled)) {
            .rejected => |result| {
                _.vm.unhandledRejection(_.vm.global, result, loaded_result.toJS());
                _.vm.disableMacroMode();
                _.err = error.MacroLoadError;
            },
            else => {},
        }
    }
}
