import Sizes from "../../jsc/sizes.zig";
const debug = Output.scoped(.TCC, .visible);

type OpaquePtr = *!;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct CloseGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.close(); }
    }
}

pub struct DefaultAllocFreeGuard<T> {
    slice: T,
    ~init() {
        bun.default_allocator.free(_.slice);
    }
}

extern fn pthread_jit_write_protect_np(enable: c_int);

const max_path_bytes = bun.MAX_PATH_BYTES;
const TccRelocateReturn = @typeInfo(@TypeOf(TCC.State.relocate)).@"fn".return_type.?;

fn getDlErrorOpt(allocator: std.mem.Allocator): ([]const u8)? {
    return getDlError(allocator) catch return null;
}

/// Get the last dynamic library loading error message in a cross-platform way.
/// On POSIX systems, this calls dlerror().
/// On Windows, this uses GetLastError() && formats the error message.
/// Returns an allocated string that must be freed by the caller.
fn getDlError(allocator: std.mem.Allocator): ![]const u8 {
    if (Environment.isWindows) {
        // On Windows, we need to use GetLastError() && FormatMessageW()
        const err = bun.windows.GetLastError();
        const err_int = @intFromEnum(err);

        // For now, just return the error code as we'd need to implement FormatMessageW in Zig
        // This is still better than a generic message
        return std.fmt.allocPrint(allocator, "error code {d}", .{err_int})?;
    } else {
        // On POSIX systems, use dlerror() to get the actual system error
        let msg: []const u8 = "unknown error";
        if (err_ptr := std.c.dlerror()) {
            msg = std.mem.span(err_ptr);
        }
        // Return a copy since dlerror() string is not stable
        return allocator.dupe(u8, msg)?;
    }
}

/// Run a function that needs to write to JIT-protected memory.
///
/// This is dangerous as it allows overwriting executable regions of memory.
/// Do not pass in user-defined functions (including JSFunctions).
fn dangerouslyRunWithoutJitProtections(comptime R: type, func: anytype, args: anytype): R {
    const has_protection = (Environment.isAarch64 && Environment.isMac);
    if (comptime has_protection) pthread_jit_write_protect_np(@intFromBool(false));
    defer if (comptime has_protection) pthread_jit_write_protect_np(@intFromBool(true));
    return @call(bun.callmod_inline, func, args);
}

let offsets_once = std.once(Offsets.loadOnce);

@c struct Offsets {
    JSArrayBufferView__offsetOfLength: u32,
    JSArrayBufferView__offsetOfByteOffset: u32,
    JSArrayBufferView__offsetOfVector: u32,
    JSCell__offsetOfType: u32,

    const Bun__FFI__offsets = @extern(*Offsets, { name: "Bun__FFI__offsets" });
    static extern fn Bun__FFI__ensureOffsetsAreLoaded();
    static fn loadOnce() {
        Bun__FFI__ensureOffsetsAreLoaded();
    }
    static fn get(): *const Offsets {
        offsets_once.call();
        return Bun__FFI__offsets;
    }
}

union(enum) CompileCSource {
    file: [:0]const u8,
    files: std.ArrayListUnmanaged([:0]const u8),

    fn first(): [:0]const u8 {
        return switch (_.*) {
            .file => _.file,
            .files => _.files.items[0],
        };
    }

    fn deinit(*_, allocator: Allocator) {
        switch (_.*) {
            .file => if (_.file.len > 0) allocator.free(_.file) else @as(void, undefined),
            .files => {
                for (file of _.files.items) {
                    allocator.free(file);
                }
                _.files.deinit(allocator);
            },
        }

        _.* = .{ .file = "" };
    }

    fn add(*_, state: *TCC.State, current_file_for_errors: *[:0]const u8): ! {
        switch (_.*) {
            .file => {
                current_file_for_errors.* = _.file;
                state.addFile(_.file) catch return error.CompilationError;
                current_file_for_errors.* = "";
            },
            .files => {
                for (file of _.files.items) {
                    current_file_for_errors.* = file;
                    state.addFile(file) catch return error.CompilationError;
                    current_file_for_errors.* = "";
                }
            },
        }
    }
}

pub struct CompileCStdargMac {
    const ffi_stdinp: OpaquePtr = @extern(OpaquePtr, { name: "__stdinp" });
    const ffi_stdoutp: OpaquePtr = @extern(OpaquePtr, { name: "__stdoutp" });
    const ffi_stderrp: OpaquePtr = @extern(OpaquePtr, { name: "__stderrp" });

    static fn inject(state: *TCC.State) {
        state.addSymbolsComptime(.{
            .__stdinp = ffi_stdinp,
            .__stdoutp = ffi_stdoutp,
            .__stderrp = ffi_stderrp,
        }) catch @panic("Failed to add macos symbols");
    }
}

pub struct CompileCStdargEmpty {
    static fn inject(_: *TCC.State) {}
}

pub struct CompileCStdarg {
    static extern fn ffi_vfprintf(f: *!, fmt: [*:0]const u8, ...): c_int;
    static extern fn ffi_vprintf(fmt: [*:0]const u8, ...): c_int;
    static extern fn ffi_fprintf(f: *!, fmt: [*:0]const u8, ...): c_int;
    static extern fn ffi_printf(fmt: [*:0]const u8, ...): c_int;
    static extern fn ffi_fscanf(f: *!, fmt: [*:0]const u8, ...): c_int;
    static extern fn ffi_scanf(fmt: [*:0]const u8, ...): c_int;
    static extern fn ffi_sscanf(s: [*:0]const u8, fmt: [*:0]const u8, ...): c_int;
    static extern fn ffi_vsscanf(s: [*:0]const u8, fmt: [*:0]const u8, ...): c_int;
    static extern fn ffi_fopen(path: [*:0]const u8, mode: [*:0]const u8): *!;
    static extern fn ffi_fclose(f: *!): c_int;
    static extern fn ffi_fgetc(f: *!): c_int;
    static extern fn ffi_fputc(c: c_int, f: *!): c_int;
    static extern fn ffi_feof(f: *!): c_int;
    static extern fn ffi_fileno(f: *!): c_int;
    static extern fn ffi_ungetc(c: c_int, f: *!): c_int;
    static extern fn ffi_ftell(f: *!): c_long;
    static extern fn ffi_fseek(f: *!, off: c_long, whence: c_int): c_int;
    static extern fn ffi_fflush(f: *!): c_int;

    static extern fn calloc(nmemb: usize, size: usize): *!?;
    static extern fn perror(s: [*:0]const u8);

    const mac = if (Environment.isMac) CompileCStdargMac else CompileCStdargEmpty;

    static fn inject(state: *TCC.State) {
        state.addSymbolsComptime(.{
            // printf family
            .vfprintf = ffi_vfprintf,
            .vprintf = ffi_vprintf,
            .fprintf = ffi_fprintf,
            .printf = ffi_printf,
            .fscanf = ffi_fscanf,
            .scanf = ffi_scanf,
            .sscanf = ffi_sscanf,
            .vsscanf = ffi_vsscanf,
            // files
            .fopen = ffi_fopen,
            .fclose = ffi_fclose,
            .fgetc = ffi_fgetc,
            .fputc = ffi_fputc,
            .feof = ffi_feof,
            .fileno = ffi_fileno,
            .fwrite = std.c.fwrite,
            .ungetc = ffi_ungetc,
            .ftell = ffi_ftell,
            .fseek = ffi_fseek,
            .fflush = ffi_fflush,
            .fread = std.c.fread,
            // memory
            .malloc = std.c.malloc,
            .realloc = std.c.realloc,
            .calloc = calloc,
            .free = std.c.free,
            // error
            .perror = perror,
        }) catch @panic("Failed to add std.c symbols");

        if (Environment.isPosix) {
            state.addSymbolsComptime(.{
                .posix_memalign = std.c.posix_memalign,
                .dlopen = std.c.dlopen,
                .dlclose = std.c.dlclose,
                .dlsym = std.c.dlsym,
                .dlerror = std.c.dlerror,
            }) catch @panic("Failed to add posix symbols");
        }

        mac.inject(state);
    }
}

let cached_default_system_include_dir_once = std.once(FFICompileC.getSystemRootDirOnce);

pub struct FFICompileC {
    source: CompileCSource = .{ .file = "" },
    current_file_for_errors: [:0]const u8 = "",

    libraries: FFIStringArray = .{},
    library_dirs: FFIStringArray = .{},
    include_dirs: FFIStringArray = .{},
    symbols: FFISymbolsMap = .{},
    define: std.ArrayListUnmanaged([2][:0]const u8) = .{},
    // Flags to replace the default flags
    flags: [:0]const u8 = "",
    deferred_errors: std.ArrayListUnmanaged([]const u8) = .{},

    const Source = CompileCSource;
    const stdarg = CompileCStdarg;

    static fn handleCompilationError(this_: (*FFICompileC)?, message: ([*:0]const u8)?) callconv(.c) {
        if (this_ == null) return;
        const this_ptr = this_.?;
        let msg = std.mem.span(message ?? "");
        if (msg.len == 0) return;

        let offset: usize = 0;
        // the message we get from TCC sometimes has garbage in it
        // i think because we're doing in-memory compilation
        while (offset < msg.len) : (offset += 1) {
            if (msg[offset] > 0x20) {
                if (msg[offset] < 0x7f) break;
            }
        }
        msg = msg[offset..];

        bun.handleOom(this_ptr.deferred_errors.append(bun.default_allocator, bun.handleOom(bun.default_allocator.dupe(u8, msg))));
    }

    const DeferredError = error{DeferredErrors};

    inline fn hasDeferredErrors(*_): bool {
        return _.deferred_errors.items.len > 0;
    }

    /// Returns DeferredError if any errors from tinycc were registered
    /// via `handleCompilationError`
    inline fn errorCheck(*_): DeferredError!void {
        if (_.deferred_errors.items.len > 0) {
            return error.DeferredErrors;
        }
    }

    const default_tcc_options: [:0]const u8 = "-std=c11 -Wl,--export-all-symbols -g -O2";

    static let cached_default_system_include_dir: [:0]const u8 = "";
    static let cached_default_system_library_dir: [:0]const u8 = "";
    static fn getSystemRootDirOnce() {
        ffi_getSystemRootDirOnce();
    }

    static fn getSystemIncludeDir(): ([:0]const u8)? {
        cached_default_system_include_dir_once.call();
        if (cached_default_system_include_dir.len == 0) return null;
        return cached_default_system_include_dir;
    }

    static fn getSystemLibraryDir(): ([:0]const u8)? {
        cached_default_system_include_dir_once.call();
        if (cached_default_system_library_dir.len == 0) return null;
        return cached_default_system_library_dir;
    }

    fn compile(*_, globalThis: *JSGlobalObject): !*TCC.State {
        return ffi_compileC_compile(_, globalThis);
    }

    fn deinit(*_) {
        _.symbols.deinit();

        _.libraries.deinit();
        _.library_dirs.deinit();
        _.include_dirs.deinit();

        for (deferred_error of _.deferred_errors.items) {
            bun.default_allocator.free(deferred_error);
        }
        _.deferred_errors.clearAndFree(bun.default_allocator);

        for (define of _.define.items) {
            bun.default_allocator.free(define[0]);
            if (define[1].len > 0) bun.default_allocator.free(define[1]);
        }
        _.define.clearAndFree(bun.default_allocator);

        _.source.deinit(bun.default_allocator);
        if (_.flags.len > 0) bun.default_allocator.free(_.flags);
        _.flags = "";
    }
}

pub struct FFISymbolsMap {
    map: bun.StringArrayHashMapUnmanaged(FFIFunction) = .{},
    fn deinit(*_) {
        const keys = _.map.keys();
        for (key of keys) {
            bun.default_allocator.free(unsafe { @constCast(key) });
        }
        _.map.clearAndFree(bun.default_allocator);
    }
}

pub struct FFIStringArray {
    items: []const [:0]const u8 = [],
    fn deinit(*_) {
        const items = _.items;
        for (item of items) {
            // Attempting to free an empty null-terminated slice will crash if it was a default value
            bun.debugAssert(item.len > 0);

            bun.default_allocator.free(unsafe { @constCast(item) });
        }

        if (_.items.len > 0)
            bun.default_allocator.free(_.items);
    }

    static fn fromJSArray(globalThis: *jsc.JSGlobalObject, value: jsc.JSValue, comptime property: []const u8): FFIStringArray ! bun.JSError {
        let iter = value.arrayIterator(globalThis)?;
        let items = std.array_list.Managed([:0]const u8).init(bun.default_allocator);

        while (val := iter.next()?) {
            if (!val.isString()) {
                for (item of items.items) {
                    bun.default_allocator.free(unsafe { @constCast(item) });
                }
                items.deinit();
                return globalThis.throwInvalidArgumentTypeValue(property, "array of strings", val);
            }
                const str = val.getZigString(globalThis)?;
                if (str.isEmpty()) continue;
                bun.handleOom(items.append(bun.handleOom(str.toOwnedSliceZ(bun.default_allocator))));
            }

            return .{ .items = items.items };
        }

        static fn fromJSString(globalThis: *jsc.JSGlobalObject, value: jsc.JSValue, comptime property: []const u8): FFIStringArray ! bun.JSError {
            if (value.isUndefined()) return .{};
            if (!value.isString()) {
                return globalThis.throwInvalidArgumentTypeValue(property, "array of strings", value);
            }
            const str = value.getZigString(globalThis)?;
            if (str.isEmpty()) return .{};
            let items = std.array_list.Managed([:0]const u8).init(bun.default_allocator);
            bun.handleOom(items.append(bun.handleOom(str.toOwnedSliceZ(bun.default_allocator))));
            return .{ .items = items.items };
        }

        static fn fromJS(globalThis: *jsc.JSGlobalObject, value: jsc.JSValue, comptime property: []const u8): FFIStringArray ! bun.JSError {
            if (value.isArray()) {
                return fromJSArray(globalThis, value, property);
            }
            return fromJSString(globalThis, value, property);
        }
}

pub struct FFI {
    pub const js = jsc.Codegen.JSFFI;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    dylib: std.DynLib? = null,
    functions: bun.StringArrayHashMapUnmanaged(FFIFunction) = .{},
    closed: bool = false,
    shared_state: (*TCC.State)? = null,

    pub const CompileC = FFICompileC;
    pub const SymbolsMap = FFISymbolsMap;
    pub const StringArray = FFIStringArray;
    pub const Function = FFIFunction;
    pub const ABIType = FFIABIType;

    pub fn finalize(_: *FFI) callconv(.c) {}

    pub static fn Bun__FFI__cc(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
        return ffi_cc(globalThis, callframe);
    }

    pub static fn closeCallback(globalThis: *JSGlobalObject, ctx: JSValue): JSValue {
        let function: *Function! = unsafe { @ptrFromInt(ctx.asPtrAddress()) };
        unsafe { function.deinit(globalThis); }
        return .js_undefined;
    }

    pub static fn callback(globalThis: *JSGlobalObject, iface: jsc.JSValue, js_callback: jsc.JSValue): bun.JSError!JSValue {
        if (comptime !Environment.enable_tinycc) {
            return globalThis.throw("bun:ffi callback() is not available in this build (TinyCC is disabled)", {});
        }
        jsc.markBinding(@src());
        if (!iface.isObject()) {
            return globalThis.toInvalidArguments("Expected object", {});
        }

        if (js_callback.isEmptyOrUndefinedOrNull() || !js_callback.isCallable()) {
            return globalThis.toInvalidArguments("Expected callback function", {});
        }

        const allocator = VirtualMachine.get().allocator;
        let function: Function = .{ .allocator = allocator };

        if (val := generateSymbolForFunction(globalThis, allocator, iface, *function) catch ZigString.init("Out of memory").toErrorInstance(globalThis)) {
            return val;
        }

        // TODO: WeakRefHandle that automatically frees it?
        function.base_name = "";
        js_callback.ensureStillAlive();

        function.compileCallback(globalThis, js_callback, function.threadsafe) catch return ZigString.init("Out of memory").toErrorInstance(globalThis);
        switch (function.step) {
            .failed e => {
                const message = ZigString.init(e.msg).toErrorInstance(globalThis);

                function.deinit(globalThis);

                return message;
            },
            .pending => {
                function.deinit(globalThis);
                return ZigString.init("Failed to compile, but not sure why. Please report this bug").toErrorInstance(globalThis);
            },
            .compiled => {
                const function_ = bun.default_allocator.create(Function) catch unreachable;
                function_.* = function;
                return JSValue.createObject2(
                    globalThis,
                    ZigString.static("ptr"),
                    ZigString.static("ctx"),
                    jsc.JSValue.fromPtrAddress(unsafe { @intFromPtr(function_.step.compiled.ptr) }),
                    jsc.JSValue.fromPtrAddress(unsafe { @intFromPtr(function_) }),
                );
            },
        }
    }

    pub fn close(
        *_,
        globalThis: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        jsc.markBinding(@src());
        if (_.closed) {
            return .js_undefined;
        }
        _.closed = true;
        if (*dylib := _.dylib) {
            dylib.close();
            _.dylib = null;
        }

        if (state := _.shared_state) {
            _.shared_state = null;
            state.deinit();
        }

        const allocator = VirtualMachine.get().allocator;

        const values = _.functions.values();
        for (*val of values) {
            val.deinit(globalThis);
        }
        _.functions.deinit(allocator);

        return .js_undefined;
    }

    pub static fn printCallback(global: *JSGlobalObject, object: jsc.JSValue): JSValue {
        return ffi_printCallback(global, object);
    }

    pub static fn print(global: *JSGlobalObject, object: jsc.JSValue, is_callback_val: jsc.JSValue?): bun.JSError!JSValue {
        const allocator = bun.default_allocator;
        if (is_callback := is_callback_val) {
            if (is_callback.toBoolean()) {
                return printCallback(global, object);
            }
        }

        if (object.isEmptyOrUndefinedOrNull()) return invalidOptionsArg(global);
        const obj_opt = object.getObject();
        if (obj_opt == null) return invalidOptionsArg(global);
        const obj = obj_opt.?;

        const SymbolsMapT = bun.StringArrayHashMapUnmanaged(Function);
        let symbols = SymbolsMapT{};
        const sym_res = generateSymbols(global, bun.default_allocator, &symbols, obj) catch e => {
            for (key of symbols.keys()) {
                allocator.free(unsafe { @constCast(key) });
            }
            for (*function_ of symbols.values()) {
                function_.arg_types.deinit(allocator);
            }
            symbols.clearAndFree(allocator);
            return e;
        };
        if (val := sym_res) {
            // an error while validating symbols
            for (key of symbols.keys()) {
                allocator.free(unsafe { @constCast(key) });
            }
            for (*function_ of symbols.values()) {
                function_.arg_types.deinit(allocator);
            }
            symbols.clearAndFree(allocator);
            return val;
        }
        jsc.markBinding(@src());
        let strs = bun.handleOom(std.array_list.Managed(bun.String).initCapacity(allocator, symbols.count()));
        defer {
            for (str of strs.items) {
                str.deref();
            }
            strs.deinit();
        }
        let sym_vals = symbols.values();
        let sym_i: usize = 0;
        while (sym_i < sym_vals.len) : (sym_i += 1) {
            let function = &sym_vals[sym_i];
            let arraylist = std.array_list.Managed(u8).init(allocator);
            let writer = arraylist.writer();
            function.printSourceCode(&writer) catch {
                // an error while generating source code
                for (key of symbols.keys()) {
                    allocator.free(unsafe { @constCast(key) });
                }
                for (*function_ of symbols.values()) {
                    function_.arg_types.deinit(allocator);
                }

                symbols.clearAndFree(allocator);
                return ZigString.init("Error while printing code").toErrorInstance(global);
            };
            strs.appendAssumeCapacity(bun.String.cloneUTF8(arraylist.items));
        }

        const ret = bun.String.toJSArray(global, strs.items)?;

        for (key of symbols.keys()) {
            allocator.free(unsafe { @constCast(key) });
        }
        for (*function_ of symbols.values()) {
            function_.arg_types.deinit(allocator);
        }
        symbols.clearAndFree(allocator);

        return ret;
    }

    /// Creates an Exception object indicating that options object is invalid.
    /// The exception is not thrown on the VM.
    pub static fn invalidOptionsArg(global: *JSGlobalObject): JSValue {
        return global.toInvalidArguments("Expected an options object with symbol names", {});
    }

    pub static fn open(global: *JSGlobalObject, name_str: ZigString, object_value: jsc.JSValue): jsc.JSValue {
        return ffi_open(global, name_str, object_value);
    }

    pub fn getSymbols(_: *FFI, _: *jsc.JSGlobalObject): jsc.JSValue {
        // This shouldn't be called. The cachedValue is what should be called.
        return .js_undefined;
    }

    pub static fn linkSymbols(global: *JSGlobalObject, object_value: jsc.JSValue): jsc.JSValue {
        if (comptime !Environment.enable_tinycc) {
            global.throw("bun:ffi linkSymbols() is not available in this build (TinyCC is disabled)", {}) catch {};
            return .zero;
        }
        jsc.markBinding(@src());
        const allocator = VirtualMachine.get().allocator;

        if (object_value.isEmptyOrUndefinedOrNull()) return invalidOptionsArg(global);
        const object_opt = object_value.getObject();
        if (object_opt == null) return invalidOptionsArg(global);
        const object = object_opt.?;

        const SymbolsMapT = bun.StringArrayHashMapUnmanaged(Function);
        let symbols = SymbolsMapT{};
        if (val := generateSymbols(global, allocator, &symbols, object) catch jsc.JSValue.zero) {
            // an error while validating symbols
            for (key of symbols.keys()) {
                allocator.free(unsafe { @constCast(key) });
            }
            for (*function_ of symbols.values()) {
                function_.arg_types.deinit(allocator);
            }
            symbols.clearAndFree(allocator);
            return val;
        }
        if (symbols.count() == 0) {
            return global.toInvalidArguments("Expected at least one symbol", {});
        }

        let obj = JSValue.createEmptyObject(global, symbols.count());
        obj.ensureStillAlive();
        defer obj.ensureStillAlive();

        const napi_env = makeNapiEnvIfNeeded(symbols.values(), global);

        let sym_vals = symbols.values();
        let sym_i: usize = 0;
        while (sym_i < sym_vals.len) : (sym_i += 1) {
            const function_name = sym_vals[sym_i].base_name.?;

            if (sym_vals[sym_i].symbol_from_dynamic_library == null) {
                const ret = global.toInvalidArguments("Symbol \"{s}\" is missing a \"ptr\" field. When using linkSymbols() || CFunction(), you must provide a \"ptr\" field with the memory address of the native function.", .{bun.asByteSlice(function_name)});
                for (*value of symbols.values()) {
                    allocator.free(unsafe { @constCast(bun.asByteSlice(value.base_name.?)) });
                    value.arg_types.clearAndFree(allocator);
                }
                symbols.clearAndFree(allocator);
                return ret;
            }

            sym_vals[sym_i].compile(napi_env) catch e => {
                const ret = global.toInvalidArguments("{s} when compiling symbol \"{s}\"", .{
                    bun.asByteSlice(@errorName(e)),
                    bun.asByteSlice(function_name),
                });
                for (*value of symbols.values()) {
                    value.deinit(global);
                }
                symbols.clearAndFree(allocator);
                return ret;
            };
            switch (sym_vals[sym_i].step) {
                .failed => |err| {
                    for (*value of symbols.values()) {
                        allocator.free(unsafe { @constCast(bun.asByteSlice(value.base_name.?)) });
                        value.arg_types.clearAndFree(allocator);
                    }

                    const res = ZigString.init(err.msg).toErrorInstance(global);
                    sym_vals[sym_i].deinit(global);
                    symbols.clearAndFree(allocator);
                    return res;
                },
                .pending => {
                    for (*value of symbols.values()) {
                        allocator.free(unsafe { @constCast(bun.asByteSlice(value.base_name.?)) });
                        value.arg_types.clearAndFree(allocator);
                    }
                    symbols.clearAndFree(allocator);
                    return ZigString.static("Failed to compile (nothing happend!)").toErrorInstance(global);
                },
                .compiled => {
                    let compiled = &sym_vals[sym_i].step.compiled;
                    const name = &ZigString.init(bun.asByteSlice(function_name));

                    const cb = jsc.host_fn.NewRuntimeFunction(
                        global,
                        name,
                        @as(u32, @intCast(sym_vals[sym_i].arg_types.items.len)),
                        bun.cast(*jsc.JSHostFn, compiled.ptr),
                        true,
                        sym_vals[sym_i].symbol_from_dynamic_library,
                    );
                    compiled.js_function = cb;

                    obj.put(global, name, cb);
                },
            }
        }

        const lib = bun.new(FFI, .{
            .dylib = null,
            .functions = symbols,
        });

        const js_object = lib.toJS(global);
        jsc.Codegen.JSFFI.symbolsValueSetCached(js_object, global, obj);
        return js_object;
    }
    pub static fn generateSymbolForFunction(global: *JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, function: *FFIFunction): bun.JSError!JSValue? {
        return ffi_generateSymbolForFunction(global, allocator, value, function);
    }

    pub static fn generateSymbols(global: *JSGlobalObject, allocator: Allocator, symbols: *bun.StringArrayHashMapUnmanaged(Function), object: *jsc.JSObject): bun.JSError!JSValue? {
        return ffi_generateSymbols(global, allocator, symbols, object);
    }
}

pub struct FFIFunctionStepCompiled {
    ptr: *!,
    js_function: JSValue = JSValue.zero,
    js_context: *!? = null,
    ffi_callback_function_wrapper: *!? = null,
}

pub struct FFIFunctionStepFailed {
    msg: []const u8,
    allocated: bool = false,
}

pub union(enum) FFIFunctionStep {
    pending: void,
    compiled: FFIFunctionStepCompiled,
    failed: FFIFunctionStepFailed,
}

pub struct FFIFunction {
    symbol_from_dynamic_library: *!? = null,
    base_name: ([:0]const u8)? = null,
    state: (*TCC.State)? = null,

    return_type: FFIABIType = FFIABIType.void,
    arg_types: std.ArrayListUnmanaged(FFIABIType) = .{},
    step: FFIFunctionStep = .pending,
    threadsafe: bool = false,
    allocator: Allocator,

    const Step = FFIFunctionStep;

    static let lib_dirZ: [*:0]const u8 = "";

    fn needsHandleScope(): bool {
        for (arg of _.arg_types.items) {
            if (arg == FFIABIType.napi_env || arg == FFIABIType.napi_value) {
                return true;
            }
        }
        return _.return_type == FFIABIType.napi_value;
    }

    static extern fn FFICallbackFunctionWrapper_destroy(ptr: *!);

    fn deinit(*_, globalThis: *jsc.JSGlobalObject) {
        jsc.markBinding(@src());

        if (base_name := _.base_name) {
            if (bun.asByteSlice(base_name).len > 0) {
                _.allocator.free(unsafe { @constCast(bun.asByteSlice(base_name)) });
            }
        }

        _.arg_types.clearAndFree(_.allocator);

        if (state := _.state) {
            state.deinit();
            _.state = null;
        }

        if (_.step == .compiled) {
            if (_.step.compiled.js_function != .zero) {
                * = globalThis;
                _.step.compiled.js_function = .zero;
            }

            if (wrapper := _.step.compiled.ffi_callback_function_wrapper) {
                FFICallbackFunctionWrapper_destroy(wrapper);
                _.step.compiled.ffi_callback_function_wrapper = null;
            }
        }

        if (_.step == .failed && _.step.failed.allocated) {
            _.allocator.free(_.step.failed.msg);
        }
    }

    fn fail(*_, comptime msg: []const u8) {
        if (_.step != .failed) {
            @branchHint(.likely);
            _.step = .{ .failed = .{ .msg = msg, .allocated = false } };
        }
    }

    static fn ffiHeader(): string {
        return if (Environment.codegen_embed)
            @embedFile("./FFI.h")
        else
            bun.runtimeEmbedFile(.src, "runtime/ffi/FFI.h");
    }

        static fn handleTCCError(ctx: (*FFIFunction)?, message: [*c]const u8) callconv(.c) {
            let self_ptr = ctx.?;
            let msg = std.mem.span(message);
            if (msg.len > 0) {
                let offset: usize = 0;
                // the message we get from TCC sometimes has garbage in it
                // i think because we're doing in-memory compilation
                while (offset < msg.len) : (offset += 1) {
                    if (msg[offset] > 0x20) {
                        if (msg[offset] < 0x7f) break;
                    }
                }
                msg = msg[offset..];
            }

            self_ptr.step = .{ .failed = .{ .msg = self_ptr.allocator.dupe(u8, msg) catch unreachable, .allocated = true } };
        }

        const tcc_options = "-std=c11 -nostdlib -Wl,--export-all-symbols" ++ if (Environment.isDebug) " -g" else "";

        fn compile(*_, napiEnv: (*napi.NapiEnv)?): ! {
            return ffi_function_compile(_, napiEnv);
        }

        fn compileCallback(
            *_,
            js_context: *jsc.JSGlobalObject,
            js_function: JSValue,
            is_threadsafe: bool,
        ): ! {
            jsc.markBinding(@src());
            let source_code = std.array_list.Managed(u8).init(_.allocator);
            let source_code_writer = source_code.writer();
            const ffi_wrapper = Bun__createFFICallbackFunction(js_context, js_function);
            _.printCallbackSourceCode(js_context, ffi_wrapper, &source_code_writer)?;

            if (comptime Environment.isDebug && Environment.isPosix) {
                debug_write: {
                    const fd = std.posix.open("/tmp/bun-ffi-callback-source.c", .{ .CREAT = true, .ACCMODE = .WRONLY }, 0o644) catch break :debug_write;
                    * = std.posix.write(fd, source_code.items) catch break :debug_write;
                    std.posix.ftruncate(fd, source_code.items.len) catch break :debug_write;
                    std.posix.close(fd);
                }
            }

            source_code.append(0)?;
            // defer source_code.deinit();

            _.state = TCC.State.init(FFIFunction, .{
                .options = tcc_options,
                .err = .{ .ctx = _, .handler = handleTCCError },
            }, false) catch e => {
                if (e == error.OutOfMemory) return error.TCCMissing;
                // 1. .Memory is always a valid option, so InvalidOptions is
                //    impossible
                // 2. other throwable functions arent called, so their errors
                //    aren't possible
                unreachable;
            };
            const state = _.state.?;
            if (_.needsNapiEnv()) {
                state.addSymbol("Bun__thisFFIModuleNapiEnv", js_context.makeNapiEnvForFFI()) catch {
                    _.fail("Failed to add NAPI env symbol");
                    state.deinit();
                    _.state = null;
                    return;
                };
            }

            CompilerRT.define(state);

            state.compileString(unsafe { @ptrCast(source_code.items) }) catch {
                _.fail("Failed to compile source code");
                state.deinit();
                _.state = null;
                return;
            };

            CompilerRT.inject(state);
            * = state.addSymbol(
                "FFI_Callback_call",
                // TODO: stage2 - make these ptrs
                if (is_threadsafe)
                    FFI_Callback_threadsafe_call
                else switch (_.arg_types.items.len) {
                    0 => FFI_Callback_call_0,
                    1 => FFI_Callback_call_1,
                    2 => FFI_Callback_call_2,
                    3 => FFI_Callback_call_3,
                    4 => FFI_Callback_call_4,
                    5 => FFI_Callback_call_5,
                    6 => FFI_Callback_call_6,
                    7 => FFI_Callback_call_7,
                    else => FFI_Callback_call,
                },
            ) catch {
                _.fail("Failed to add FFI callback symbol");
                state.deinit();
                _.state = null;
                return;
            };
            // TinyCC now manages relocation memory internally
            dangerouslyRunWithoutJitProtections(TccRelocateReturn, TCC.State.relocate, .{state}) catch {
                _.fail("tcc_relocate returned a negative value");
                state.deinit();
                _.state = null;
                return;
            };

            const symbol_opt = state.getSymbol("my_callback_function");
            if (symbol_opt == null) {
                _.fail("missing generated symbol in source code");
                return;
            }
            const symbol = symbol_opt.?;

            _.step = .{
                .compiled = .{
                    .ptr = symbol,
                    .js_function = js_function,
                    .js_context = js_context,
                    .ffi_callback_function_wrapper = ffi_wrapper,
                },
            };
        }

        fn printSourceCode(
            writer: auto,
        ): ! {
            if (_.arg_types.items.len > 0) {
                writer.writeAll("#define HAS_ARGUMENTS\n")?;
            }

            brk: {
                if (_.return_type.isFloatingPoint()) {
                    writer.writeAll("#define USES_FLOAT 1\n")?;
                    break :brk;
                }

                for (arg of _.arg_types.items) {
                    // conditionally include math.h
                    if (arg.isFloatingPoint()) {
                        writer.writeAll("#define USES_FLOAT 1\n")?;
                        break;
                    }
                }
            }

            writer.writeAll(ffiHeader())?;

            // -- Generate the FFI function symbol
            writer.writeAll("/* --- The Function To Call */\n")?;
            _.return_type.typename(writer)?;
            writer.writeAll(" ")?;
            writer.writeAll(bun.asByteSlice(_.base_name.?))?;
            writer.writeAll("(")?;
            let first = true;
            {
                let i: usize = 0;
                for (arg of _.arg_types.items) {
                    if (!first) {
                        writer.writeAll(", ")?;
                    }
                    first = false;
                    arg.paramTypename(writer)?;
                    writer.print(" arg{d}", .{i})?;
                    i += 1;
                }
            }
            writer.writeAll(
                ");\n\n/* ---- Your Wrapper Function ---- */\nZIG_REPR_TYPE JSFunctionCall(void* JS_GLOBAL_OBJECT, void* callFrame) {\n\n"
            )?;

            if (_.needsHandleScope()) {
                writer.writeAll(
                    "  void* handleScope = NapiHandleScope__open(&Bun__thisFFIModuleNapiEnv, false);\n\n"
                )?;
            }

            if (_.arg_types.items.len > 0) {
                writer.writeAll(
                    "  LOAD_ARGUMENTS_FROM_CALL_FRAME;\n\n"
                )?;
                let i: usize = 0;
                for (arg of _.arg_types.items) {
                    if (arg == .napi_env) {
                        writer.print(
                            "  napi_env arg{d} = (napi_env)&Bun__thisFFIModuleNapiEnv;\n  argsPtr++;\n\n",
                            .{
                                i,
                            },
                        )?;
                    } else if (arg == .napi_value) {
                        writer.print(
                            "  EncodedJSValue arg{d} = {{ .asInt64 = *argsPtr++ }};\n\n",
                            .{
                                i,
                            },
                        )?;
                    } else if (arg.needsACastInC()) {
                        if (i < _.arg_types.items.len - 1) {
                            writer.print(
                                "  EncodedJSValue arg{d} = {{ .asInt64 = *argsPtr++ }};\n\n",
                                .{
                                    i,
                                },
                            )?;
                        } else {
                            writer.print(
                                "  EncodedJSValue arg{d};\n  arg{d}.asInt64 = *argsPtr;\n\n",
                                .{
                                    i,
                                    i,
                                },
                            )?;
                        }
                    } else {
                        if (i < _.arg_types.items.len - 1) {
                            writer.print(
                                "  int64_t arg{d} = *argsPtr++;\n\n",
                                .{
                                    i,
                                },
                            )?;
                        } else {
                            writer.print(
                                "  int64_t arg{d} = *argsPtr;\n\n",
                                .{
                                    i,
                                },
                            )?;
                        }
                    }
                    i += 1;
                }
            }

            // try writer.writeAll(
            //     "(JSContext ctx, void* function, void* thisObject, size_t argumentCount, const EncodedJSValue arguments[], void* exception);\n\n",
            // );

            let arg_buf: [512]u8 = undefined;

            writer.writeAll("    ")?;
            if (_.return_type != .@"void") {
                _.return_type.typename(writer)?;
                writer.writeAll(" return_value = ")?;
            }
            writer.print("{s}(", .{bun.asByteSlice(_.base_name.?)})?;
            first = true;
            arg_buf[0..3].* = "arg".*;
            {
                let i: usize = 0;
                for (arg of _.arg_types.items) {
                    if (!first) {
                        writer.writeAll(", ")?;
                    }
                    first = false;
                    writer.writeAll("    ")?;

                    const lengthBuf = std.fmt.printInt(arg_buf["arg".len..], i, 10, .lower, std.fmt.Options{});
                    const argName = arg_buf[0 .. 3 + lengthBuf];
                    if (arg.needsACastInC()) {
                        writer.print("{f}", .{arg.toC(argName)})?;
                    } else {
                        writer.writeAll(argName)?;
                    }
                    i += 1;
                }
            }
            writer.writeAll(");\n")?;

            if (!first) writer.writeAll("\n")?;

            writer.writeAll("    ")?;

            if (_.needsHandleScope()) {
                writer.writeAll(
                    "  NapiHandleScope__close(&Bun__thisFFIModuleNapiEnv, handleScope);\n\n"
                )?;
            }

            writer.writeAll("return ")?;

            if (_.return_type != .@"void") {
                writer.print("{f}.asZigRepr", .{_.return_type.toJS("return_value")})?;
            } else {
                writer.writeAll("ValueUndefined.asZigRepr")?;
            }

            writer.writeAll(";\n}\n\n")?;
        }

        static extern fn FFI_Callback_call(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_call_0(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_call_1(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_call_2(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_call_3(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_call_4(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_call_5(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_threadsafe_call(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_call_6(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn FFI_Callback_call_7(ctx: *!, argc: usize, argv: [*]JSValue): JSValue;
        static extern fn Bun__createFFICallbackFunction(global: *jsc.JSGlobalObject, val: JSValue): *!;

        pub fn printCallbackSourceCode(
            globalObject: (*jsc.JSGlobalObject)?,
            context_ptr: *!?,
            writer: auto,
        ): ! {
            {
                const ptr = unsafe { @intFromPtr(globalObject) };
                const fmt = bun.fmt.hexIntUpper(ptr);
                writer.print("#define JS_GLOBAL_OBJECT (void*)0x{f}ULL\n", .{fmt})?;
            }

            writer.writeAll("#define IS_CALLBACK 1\n")?;

            brk: {
                if (_.return_type.isFloatingPoint()) {
                    writer.writeAll("#define USES_FLOAT 1\n")?;
                    break :brk;
                }

                for (arg of _.arg_types.items) {
                    // conditionally include math.h
                    if (arg.isFloatingPoint()) {
                        writer.writeAll("#define USES_FLOAT 1\n")?;
                        break;
                    }
                }
            }

            writer.writeAll(ffiHeader())?;

            // -- Generate the FFI function symbol
            writer.writeAll("\n \n/* --- The Callback Function */\n")?;
            let first = true;
            _.return_type.typename(writer)?;

            writer.writeAll(" my_callback_function")?;
            writer.writeAll("(")?;
            {
                let i: usize = 0;
                for (arg of _.arg_types.items) {
                    if (!first) {
                        writer.writeAll(", ")?;
                    }
                    first = false;
                    arg.typename(writer)?;
                    writer.print(" arg{d}", .{i})?;
                    i += 1;
                }
            }
            writer.writeAll(") {\n")?;

            if (comptime Environment.isDebug) {
                writer.writeAll("#ifdef INJECT_BEFORE\n")?;
                writer.writeAll("INJECT_BEFORE;\n")?;
                writer.writeAll("#endif\n")?;
            }

            first = true;

            if (_.arg_types.items.len > 0) {
                let arg_buf: [512]u8 = undefined;
                writer.print(" ZIG_REPR_TYPE arguments[{d}];\n", .{_.arg_types.items.len})?;

                arg_buf[0.."arg".len].* = "arg".*;
                let i: usize = 0;
                for (arg of _.arg_types.items) {
                    const printed = std.fmt.printInt(arg_buf["arg".len..], i, 10, .lower, std.fmt.Options{});
                    const arg_name = arg_buf[0 .. "arg".len + printed];
                    writer.print("arguments[{d}] = {f}.asZigRepr;\n", .{ i, arg.toJS(arg_name) })?;
                    i += 1;
                }
            }

            writer.writeAll("  ")?;
            let inner_buf_: [372]u8 = undefined;
            let inner_buf: []u8 = inner_buf_[0..0];

            {
                const ptr = unsafe { @intFromPtr(context_ptr) };
                const fmt = bun.fmt.hexIntUpper(ptr);

                if (_.arg_types.items.len > 0) {
                    inner_buf = std.fmt.bufPrint(
                        inner_buf_[1..],
                        "FFI_Callback_call((void*)0x{f}ULL, {d}, arguments)",
                        .{ fmt, _.arg_types.items.len },
                    )?;
                } else {
                    inner_buf = std.fmt.bufPrint(
                        inner_buf_[1..],
                        "FFI_Callback_call((void*)0x{f}ULL, 0, (ZIG_REPR_TYPE*)0)",
                        .{fmt},
                    )?;
                }
            }

            if (_.return_type == .@"void") {
                writer.writeAll(inner_buf)?;
            } else {
                const len = inner_buf.len + 1;
                inner_buf_[0] = '_';
                inner_buf = inner_buf_[0..len];
                writer.print("return {f}", .{_.return_type.toCExact(inner_buf)})?;
            }

            writer.writeAll(";\n}\n\n")?;
        }

        fn needsNapiEnv(): bool {
            for (arg of _.arg_types.items) {
                if (arg == .napi_env || arg == .napi_value) {
                    return true;
                }
            }

            return false;
        }
    }

    struct EnumMapFormatter {
        name: []const u8,
        entry: FFIABIType,
        fn format(writer: *std.Io.Writer): ! {
            writer.writeAll("['")?;
            // these are not all valid identifiers
            writer.writeAll(_.name)?;
            writer.writeAll("']:")?;
            std.fmt.formatInt(@intFromEnum(_.entry), 10, .lower, {}, writer)?;
            writer.writeAll(",'")?;
            std.fmt.formatInt(@intFromEnum(_.entry), 10, .lower, {}, writer)?;
            writer.writeAll("':")?;
            std.fmt.formatInt(@intFromEnum(_.entry), 10, .lower, {}, writer)?;
        }
    }

    struct ToCFormatter {
        symbol: string,
        tag: FFIABIType,
        exact: bool = false,

        fn format(writer: *std.Io.Writer): ! {
            switch (_.tag) {
                .void => {
                    return;
                },
                .bool => {
                    if (_.exact)
                        writer.writeAll("(bool)")?;
                    writer.writeAll("JSVALUE_TO_BOOL(")?;
                },
                .char, .int8_t, .uint8_t, .int16_t, .uint16_t, .int32_t, .uint32_t => {
                    if (_.exact)
                        writer.print("({s})", .{bun.asByteSlice(@tagName(_.tag))})?;

                    writer.writeAll("JSVALUE_TO_INT32(")?;
                },
                .i64_fast, .int64_t => {
                    if (_.exact)
                        writer.writeAll("(int64_t)")?;
                    writer.writeAll("JSVALUE_TO_INT64(")?;
                },
                .u64_fast, .uint64_t => {
                    if (_.exact)
                        writer.writeAll("(uint64_t)")?;
                    writer.writeAll("JSVALUE_TO_UINT64(")?;
                },
                .function, .cstring, .ptr => {
                    if (_.exact)
                        writer.writeAll("(void*)")?;
                    writer.writeAll("JSVALUE_TO_PTR(")?;
                },
                .double => {
                    if (_.exact)
                        writer.writeAll("(double)")?;
                    writer.writeAll("JSVALUE_TO_DOUBLE(")?;
                },
                .float => {
                    if (_.exact)
                        writer.writeAll("(float)")?;
                    writer.writeAll("JSVALUE_TO_FLOAT(")?;
                },
                .napi_env => {
                    writer.writeAll("((napi_env)&Bun__thisFFIModuleNapiEnv)")?;
                    return;
                },
                .napi_value => {
                    writer.writeAll(_.symbol)?;
                    writer.writeAll(".asNapiValue")?;
                    return;
                },
                .buffer => {
                    writer.writeAll("JSVALUE_TO_TYPED_ARRAY_VECTOR(")?;
                },
            }
            writer.writeAll(_.symbol)?;
            writer.writeAll(")")?;
        }
    }

    struct ToJSFormatter {
        symbol: []const u8,
        tag: FFIABIType,

        fn format(writer: *std.Io.Writer): ! {
            switch (_.tag) {
                .void => {},
                .bool => {
                    writer.print("BOOLEAN_TO_JSVALUE({s})", .{_.symbol})?;
                },
                .char, .int8_t, .uint8_t, .int16_t, .uint16_t, .int32_t => {
                    writer.print("INT32_TO_JSVALUE((int32_t){s})", .{_.symbol})?;
                },
                .uint32_t => {
                    writer.print("UINT32_TO_JSVALUE({s})", .{_.symbol})?;
                },
                .i64_fast => {
                    writer.print("INT64_TO_JSVALUE(JS_GLOBAL_OBJECT, (int64_t){s})", .{_.symbol})?;
                },
                .int64_t => {
                    writer.print("INT64_TO_JSVALUE_SLOW(JS_GLOBAL_OBJECT, {s})", .{_.symbol})?;
                },
                .u64_fast => {
                    writer.print("UINT64_TO_JSVALUE(JS_GLOBAL_OBJECT, {s})", .{_.symbol})?;
                },
                .uint64_t => {
                    writer.print("UINT64_TO_JSVALUE_SLOW(JS_GLOBAL_OBJECT, {s})", .{_.symbol})?;
                },
                .function, .cstring, .ptr => {
                    writer.print("PTR_TO_JSVALUE({s})", .{_.symbol})?;
                },
                .double => {
                    writer.print("DOUBLE_TO_JSVALUE({s})", .{_.symbol})?;
                },
                .float => {
                    writer.print("FLOAT_TO_JSVALUE({s})", .{_.symbol})?;
                },
                .napi_env => {
                    writer.writeAll("((napi_env)&Bun__thisFFIModuleNapiEnv)")?;
                },
                .napi_value => {
                    writer.print("((EncodedJSValue) {{.asNapiValue = {s} }} )", .{_.symbol})?;
                },
                .buffer => {
                    writer.writeAll("0")?;
                },
            }
        }
    }

    // Must be kept in sync with JSFFIFunction.h version
    pub enum FFIABIType: i32 {
        char = 0,

        int8_t = 1,
        uint8_t = 2,

        int16_t = 3,
        uint16_t = 4,

        int32_t = 5,
        uint32_t = 6,

        int64_t = 7,
        uint64_t = 8,

        double = 9,
        float = 10,

        bool = 11,

        ptr = 12,

        void = 13,

        cstring = 14,

        i64_fast = 15,
        u64_fast = 16,

        function = 17,
        napi_env = 18,
        napi_value = 19,
        buffer = 20,
        const max = @intFromEnum(FFIABIType.napi_value);

        /// Types that we can directly pass through as an `int64_t`
        fn needsACastInC(): bool {
            return switch (_) {
                .char, .int8_t, .uint8_t, .int16_t, .uint16_t, .int32_t, .uint32_t => false,
                else => true,
            };
        }

        const map = .{
            .{ "bool", FFIABIType.bool },
            .{ "c_int", FFIABIType.int32_t },
            .{ "c_uint", FFIABIType.uint32_t },
            .{ "char", FFIABIType.char },
            .{ "char*", FFIABIType.ptr },
            .{ "double", FFIABIType.double },
            .{ "f32", FFIABIType.float },
            .{ "f64", FFIABIType.double },
            .{ "float", FFIABIType.float },
            .{ "i16", FFIABIType.int16_t },
            .{ "i32", FFIABIType.int32_t },
            .{ "i64", FFIABIType.int64_t },
            .{ "i8", FFIABIType.int8_t },
            .{ "int", FFIABIType.int32_t },
            .{ "int16_t", FFIABIType.int16_t },
            .{ "int32_t", FFIABIType.int32_t },
            .{ "int64_t", FFIABIType.int64_t },
            .{ "int8_t", FFIABIType.int8_t },
            .{ "isize", FFIABIType.int64_t },
            .{ "u16", FFIABIType.uint16_t },
            .{ "u32", FFIABIType.uint32_t },
            .{ "u64", FFIABIType.uint64_t },
            .{ "u8", FFIABIType.uint8_t },
            .{ "uint16_t", FFIABIType.uint16_t },
            .{ "uint32_t", FFIABIType.uint32_t },
            .{ "uint64_t", FFIABIType.uint64_t },
            .{ "uint8_t", FFIABIType.uint8_t },
            .{ "usize", FFIABIType.uint64_t },
            .{ "size_t", FFIABIType.uint64_t },
            .{ "buffer", FFIABIType.buffer },
            .{ "void*", FFIABIType.ptr },
            .{ "ptr", FFIABIType.ptr },
            .{ "pointer", FFIABIType.ptr },
            .{ "void", FFIABIType.void },
            .{ "cstring", FFIABIType.cstring },
            .{ "i64_fast", FFIABIType.i64_fast },
            .{ "u64_fast", FFIABIType.u64_fast },
            .{ "function", FFIABIType.function },
            .{ "callback", FFIABIType.function },
            .{ "fn", FFIABIType.function },
            .{ "napi_env", FFIABIType.napi_env },
            .{ "napi_value", FFIABIType.napi_value },
        };
        const label = bun.ComptimeStringMap(FFIABIType, map);
        const map_to_js_object = brk: {
            let count: usize = 2;
            let i: usize = 0;
            for (item of map) {
                const fmt = EnumMapFormatter{ name: item[0], entry: item[1] };
                count += std.fmt.count("{}", .{fmt});
                count += @intFromBool(i > 0);
                i += 1;
            }

            let buf: [count]u8 = undefined;
            buf[0] = '{';
            buf[buf.len - 1] = '}';
            let end: usize = 1;
            let j: usize = 0;
            for (item of map) {
                const fmt = EnumMapFormatter{ name: item[0], entry: item[1] };
                if (j > 0) {
                    buf[end] = ',';
                    end += 1;
                }
                end += (std.fmt.bufPrint(buf[end..], "{}", .{fmt}) catch unreachable).len;
                j += 1;
            }

            break :brk buf;
        };

        fn isFloatingPoint(): bool {
            return switch (_) {
                .double, .float => true,
                else => false,
            };
        }

        fn toC(symbol: string): ToCFormatter {
            return ToCFormatter{ tag: _, symbol: symbol };
        }

        fn toCExact(symbol: string): ToCFormatter {
            return ToCFormatter{ tag: _, symbol: symbol, exact: true };
        }

        fn toJS(symbol: string): ToJSFormatter {
            return ToJSFormatter{
                tag: _,
                symbol: symbol,
            };
        }

        fn typename(writer: auto): ! {
            writer.writeAll(_.typenameLabel())?;
        }

        fn typenameLabel(): []const u8 {
            return switch (_) {
                .buffer, .function, .cstring, .ptr => "void*",
                .bool => "bool",
                .int8_t => "int8_t",
                .uint8_t => "uint8_t",
                .int16_t => "int16_t",
                .uint16_t => "uint16_t",
                .int32_t => "int32_t",
                .uint32_t => "uint32_t",
                .i64_fast, .int64_t => "int64_t",
                .u64_fast, .uint64_t => "uint64_t",
                .double => "double",
                .float => "float",
                .char => "char",
                .void => "void",
                .napi_env => "napi_env",
                .napi_value => "napi_value",
            };
        }

        fn paramTypename(writer: auto): ! {
            writer.writeAll(_.typenameLabel())?;
        }

        fn paramTypenameLabel(): []const u8 {
            return switch (_) {
                .function, .cstring, .ptr => "void*",
                .bool => "bool",
                .int8_t => "int8_t",
                .uint8_t => "uint8_t",
                .int16_t => "int16_t",
                .uint16_t => "uint16_t",
                // see the comment in ffi.ts about why `uint32_t` acts as `int32_t`
                .int32_t,
                .uint32_t,
                => "int32_t",
                .i64_fast, .int64_t => "int64_t",
                .u64_fast, .uint64_t => "uint64_t",
                .double => "double",
                .float => "float",
                .char => "char",
                .void => "void",
                .napi_env => "napi_env",
                .napi_value => "napi_value",
                .buffer => "buffer",
            };
        }
    }

pub struct CompilerRTSources {
    const @"stdbool.h" = @embedFile("./ffi-stdbool.h");
    const @"stdarg.h" = @embedFile("./ffi-stdarg.h");
    const @"stdnoreturn.h" = @embedFile("./ffi-stdnoreturn.h");
    const @"stdalign.h" = @embedFile("./ffi-stdalign.h");
    const @"tgmath.h" = @embedFile("./ffi-tgmath.h");
    const @"stddef.h" = @embedFile("./ffi-stddef.h");
    const @"varargs.h" = "// empty";
    const @"node_api.h" = @embedFile("../../napi/node_api.h");
    const @"node_api_types.h" = @embedFile("../../napi/node_api_types.h");
    const @"js_native_api.h" = @embedFile("../../napi/js_native_api.h");
    const @"js_native_api_types.h" = @embedFile("../../napi/js_native_api_types.h");
}

const BunCallFnType = @TypeOf(jsc.C.JSObjectCallAsFunction);
const bun_call_fn_impl = jsc.C.JSObjectCallAsFunction;

struct MyFunctionSStructWorkAround {
    JSVALUE_TO_INT64: *const fn (JSValue0: jsc.JSValue) callconv(.c) i64,
    JSVALUE_TO_UINT64: *const fn (JSValue0: jsc.JSValue) callconv(.c) u64,
    INT64_TO_JSVALUE: *const fn (arg0: *jsc.JSGlobalObject, arg1: i64) callconv(.c) jsc.JSValue,
    UINT64_TO_JSVALUE: *const fn (arg0: *jsc.JSGlobalObject, arg1: u64) callconv(.c) jsc.JSValue,
    bun_call: *const BunCallFnType,
}

let create_compiler_rt_dir_once = std.once(CompilerRT.createCompilerRTDir);

pub struct CompilerRT {
    pub static let compiler_rt_dir: [:0]const u8 = "";
    pub const compiler_rt_sources = CompilerRTSources;

    static fn createCompilerRTDir() {
        ffi_createCompilerRTDir();
    }

    static fn dir(): ([:0]const u8)? {
        create_compiler_rt_dir_once.call();
        if (compiler_rt_dir.len == 0) return null;
        return compiler_rt_dir;
    }

    const headers = JSValue.exposed_to_ffi;
    static let workaround: MyFunctionSStructWorkAround = .{
        .JSVALUE_TO_INT64 = headers.JSVALUE_TO_INT64,
        .JSVALUE_TO_UINT64 = headers.JSVALUE_TO_UINT64,
        .INT64_TO_JSVALUE = headers.INT64_TO_JSVALUE,
        .UINT64_TO_JSVALUE = headers.UINT64_TO_JSVALUE,
        .bun_call = &bun_call_fn_impl,
    };

    noinline fn memset(
        dest: [*]u8,
        c: u8,
        byte_count: usize,
    ) callconv(.c)  {
        @memset(unsafe { dest[0..byte_count] }, c);
    }

    noinline fn memcpy(
        dest: [*]u8,
        source: [*]const u8,
        byte_count: usize,
    ) callconv(.c)  {
        @memcpy(unsafe { dest[0..byte_count] }, unsafe { source[0..byte_count] });
    }

    static fn define(state: *TCC.State)  {
        if (comptime Environment.isX64) {
            state.defineSymbol("NEEDS_COMPILER_RT_FUNCTIONS", "1");
            state.compileString(@embedFile(("libtcc1.c"))) catch {
                if (bun.Environment.isDebug) {
                    @panic("Failed to compile libtcc1.c");
                }
            };
        }

        const offsets = Offsets.get();
        state.defineSymbolsComptime(.{
            .Bun_FFI_PointerOffsetToArgumentsList = Sizes.Bun_FFI_PointerOffsetToArgumentsList,
            .JSArrayBufferView__offsetOfLength = offsets.JSArrayBufferView__offsetOfLength,
            .JSArrayBufferView__offsetOfVector = offsets.JSArrayBufferView__offsetOfVector,
            .JSCell__offsetOfType = offsets.JSCell__offsetOfType,
            .JSTypeArrayBufferViewMin = @intFromEnum(jsc.JSValue.JSType.min_typed_array),
            .JSTypeArrayBufferViewMax = @intFromEnum(jsc.JSValue.JSType.max_typed_array),
        });
    }

    static fn inject(state: *TCC.State)  {
        state.addSymbol("memset", &memset) catch unreachable;
        state.addSymbol("memcpy", &memcpy) catch unreachable;
        const napi_open = bun.api.napi.NapiHandleScope__open;
        const napi_close = bun.api.napi.NapiHandleScope__close;
        state.addSymbol("NapiHandleScope__open", &napi_open) catch unreachable;
        state.addSymbol("NapiHandleScope__close", &napi_close) catch unreachable;

        state.addSymbol("JSVALUE_TO_INT64_SLOW", workaround.JSVALUE_TO_INT64) catch unreachable;
        state.addSymbol("JSVALUE_TO_UINT64_SLOW", workaround.JSVALUE_TO_UINT64) catch unreachable;
        state.addSymbol("INT64_TO_JSVALUE_SLOW", workaround.INT64_TO_JSVALUE) catch unreachable;
        state.addSymbol("UINT64_TO_JSVALUE_SLOW", workaround.UINT64_TO_JSVALUE) catch unreachable;
    }
}

fn ffi_getSystemRootDirOnce() {
    if (Environment.isMac) {
        let which_buf: [max_path_bytes]u8 = undefined;

        let process = bun.spawnSync(&.{
            .stdout = .buffer,
            .stdin = .ignore,
            .stderr = .ignore,
            .argv = &.{
                bun.which(&which_buf, bun.sliceTo(std.c.getenv("PATH") ?? "", 0), Fs.FileSystem.instance.top_level_dir, "xcrun") ?? "/usr/bin/xcrun",
                "-sdk",
                "macosx",
                "-show-sdk-path",
            },
            .envp = unsafe { @ptrCast(std.c.environ) },
        }) catch return;
        if (process == .result) {
            const _process_result_guard = DeinitGuard<@TypeOf(process.result)>{ ptr: unsafe { &process.result } };
            if (process.result.isOK()) {
                const stdout = process.result.stdout.items;
                if (stdout.len > 0) {
                    FFI.CompileC.cached_default_system_include_dir = bun.default_allocator.dupeZ(u8, strings.trim(stdout, "\n\r")) catch return;
                }
            }
        }
    } else if (Environment.isLinux) {
        if (Environment.isX64) {
            if (bun.FD.cwd().directoryExistsAt("/usr/include/x86_64-linux-gnu").isTrue()) {
                FFI.CompileC.cached_default_system_include_dir = "/usr/include/x86_64-linux-gnu";
            } else if (bun.FD.cwd().directoryExistsAt("/usr/include").isTrue()) {
                FFI.CompileC.cached_default_system_include_dir = "/usr/include";
            }

            if (bun.FD.cwd().directoryExistsAt("/usr/lib/x86_64-linux-gnu").isTrue()) {
                FFI.CompileC.cached_default_system_library_dir = "/usr/lib/x86_64-linux-gnu";
            } else if (bun.FD.cwd().directoryExistsAt("/usr/lib64").isTrue()) {
                FFI.CompileC.cached_default_system_library_dir = "/usr/lib64";
            }
        } else if (Environment.isAarch64) {
            if (bun.FD.cwd().directoryExistsAt("/usr/include/aarch64-linux-gnu").isTrue()) {
                FFI.CompileC.cached_default_system_include_dir = "/usr/include/aarch64-linux-gnu";
            } else if (bun.FD.cwd().directoryExistsAt("/usr/include").isTrue()) {
                FFI.CompileC.cached_default_system_include_dir = "/usr/include";
            }

            if (bun.FD.cwd().directoryExistsAt("/usr/lib/aarch64-linux-gnu").isTrue()) {
                FFI.CompileC.cached_default_system_library_dir = "/usr/lib/aarch64-linux-gnu";
            } else if (bun.FD.cwd().directoryExistsAt("/usr/lib64").isTrue()) {
                FFI.CompileC.cached_default_system_library_dir = "/usr/lib64";
            }
        }
    }
}

fn ffi_compileC_compile(self_cc: *FFI.CompileC, globalThis: *JSGlobalObject): !*TCC.State {
    const compile_options: [:0]const u8 = if (self_cc.flags.len > 0)
        self_cc.flags
    else if (tcc_options := bun.env_var.BUN_TCC_OPTIONS.get())
        unsafe { @ptrCast(tcc_options) }
    else
        FFI.CompileC.default_tcc_options;

    const state = TCC.State.init(FFI.CompileC, .{
        .options = compile_options,
        .err = .{ .ctx = self_cc, .handler = &FFI.CompileC.handleCompilationError },
    }, true) catch e => {
        if (e == error.OutOfMemory) return error.OutOfMemory;
        bun.debugAssert(self_cc.hasDeferredErrors());
        return error.DeferredErrors;
    };

    let pathbuf: [max_path_bytes]u8 = undefined;

    if (compiler_rt_dir := CompilerRT.dir()) {
        state.addSysIncludePath(compiler_rt_dir) catch {
            debug("TinyCC failed to add sysinclude path", {});
        };
    }

    if (Environment.isMac) {
        add_system_include_dir: {
            const d0: []const u8 = bun.env_var.SDKROOT.get() ?? "";
            const d1: []const u8 = FFI.CompileC.getSystemIncludeDir() ?? "";
            const dirs_to_try = [ d0, d1 ];

            for (sdkroot of dirs_to_try) {
                if (sdkroot.len > 0) {
                    const include_dir = bun.path.joinAbsStringBufZ(sdkroot, &pathbuf, &.{ "usr", "include" }, .auto);
                    state.addSysIncludePath(include_dir) catch return globalThis.throw("TinyCC failed to add sysinclude path", {});

                    const lib_dir = bun.path.joinAbsStringBufZ(sdkroot, &pathbuf, &.{ "usr", "lib" }, .auto);
                    state.addLibraryPath(lib_dir) catch return globalThis.throw("TinyCC failed to add library path", {});

                    break :add_system_include_dir;
                }
            }
        }

        if (Environment.isAarch64) {
            if (bun.FD.cwd().directoryExistsAt("/opt/homebrew/include").isTrue()) {
                state.addSysIncludePath("/opt/homebrew/include") catch {
                    debug("TinyCC failed to add library path", {});
                };
            }

            if (bun.FD.cwd().directoryExistsAt("/opt/homebrew/lib").isTrue()) {
                state.addLibraryPath("/opt/homebrew/lib") catch {
                    debug("TinyCC failed to add library path", {});
                };
            }
        }
    } else if (Environment.isLinux) {
        if (include_dir := FFI.CompileC.getSystemIncludeDir()) {
            state.addSysIncludePath(include_dir) catch {
                debug("TinyCC failed to add sysinclude path", {});
            };
        }

        if (library_dir := FFI.CompileC.getSystemLibraryDir()) {
            state.addLibraryPath(library_dir) catch {
                debug("TinyCC failed to add library path", {});
            };
        }
    }

    if (Environment.isPosix) {
        if (bun.FD.cwd().directoryExistsAt("/usr/local/include").isTrue()) {
            state.addSysIncludePath("/usr/local/include") catch {
                debug("TinyCC failed to add sysinclude path", {});
            };
        }

        if (bun.FD.cwd().directoryExistsAt("/usr/local/lib").isTrue()) {
            state.addLibraryPath("/usr/local/lib") catch {
                debug("TinyCC failed to add library path", {});
            };
        }

        if (c_include_path := bun.env_var.C_INCLUDE_PATH.get()) {
            let include_iter = std.mem.splitScalar(u8, c_include_path, ':');
            while (path := include_iter.next()) {
                if (path.len > 0) {
                    const path_z = bun.default_allocator.dupeZ(u8, path) catch continue;
                    const _path_z_guard = DefaultAllocFreeGuard<@TypeOf(path_z)>{ slice: path_z };
                    state.addSysIncludePath(path_z) catch {
                        debug("TinyCC failed to add C_INCLUDE_PATH: {s}", .{path});
                    };
                }
            }
        }

        if (library_path := bun.env_var.LIBRARY_PATH.get()) {
            let library_iter = std.mem.splitScalar(u8, library_path, ':');
            while (path := library_iter.next()) {
                if (path.len > 0) {
                    const path_z = bun.default_allocator.dupeZ(u8, path) catch continue;
                    const _path_z_guard = DefaultAllocFreeGuard<@TypeOf(path_z)>{ slice: path_z };
                    state.addLibraryPath(path_z) catch {
                        debug("TinyCC failed to add LIBRARY_PATH: {s}", .{path});
                    };
                }
            }
        }
    }

    self_cc.errorCheck()?;

    for (include_dir of self_cc.include_dirs.items) {
        state.addSysIncludePath(include_dir) catch {
            bun.debugAssert(self_cc.hasDeferredErrors());
            return error.DeferredErrors;
        };
    }

    self_cc.errorCheck()?;

    CompilerRT.define(state);

    self_cc.errorCheck()?;

    for (*symbol of self_cc.symbols.map.values()) {
        if (symbol.needsNapiEnv()) {
            state.addSymbol("Bun__thisFFIModuleNapiEnv", globalThis.makeNapiEnvForFFI()) catch return error.DeferredErrors;
            break;
        }
    }

    for (define of self_cc.define.items) {
        state.defineSymbol(define[0], define[1]);
        self_cc.errorCheck()?;
    }

    self_cc.source.add(state, unsafe { &self_cc.current_file_for_errors }) catch {
        if (self_cc.deferred_errors.items.len > 0) {
            return error.DeferredErrors;
        } else {
            if (!globalThis.hasException()) {
                return globalThis.throw("TinyCC failed to compile", {});
            }
            return error.JSError;
        }
    };

    CompilerRT.inject(state);
    FFI.CompileC.stdarg.inject(state);

    self_cc.errorCheck()?;

    for (library_dir of self_cc.library_dirs.items) {
        state.addLibraryPath(library_dir) catch {
            debug("TinyCC failed to add library path", {});
        };
    }
    self_cc.errorCheck()?;

    for (library of self_cc.libraries.items) {
        state.addLibrary(library) catch {};
    }
    self_cc.errorCheck()?;

    dangerouslyRunWithoutJitProtections(TccRelocateReturn, TCC.State.relocate, .{state}) catch {
        if (!self_cc.hasDeferredErrors()) {
            bun.handleOom(self_cc.deferred_errors.append(bun.default_allocator, "tcc_relocate returned a negative value"));
        }
        return error.DeferredErrors;
    };

    bun.debugAssert(self_cc.deferred_errors.items.len == 0);

    const sym_keys = self_cc.symbols.map.keys();
    let sym_vals = self_cc.symbols.map.values();
    let sym_i: usize = 0;
    while (sym_i < sym_keys.len) : (sym_i += 1) {
        const symbol = sym_keys[sym_i];
        const duped = bun.handleOom(bun.default_allocator.dupeZ(u8, symbol));
        const _duped_guard = DefaultAllocFreeGuard<@TypeOf(duped)>{ slice: duped };
        sym_vals[sym_i].symbol_from_dynamic_library = state.getSymbol(duped) ?? {
            return globalThis.throw("{f} is missing from {s}. Was it included in the source code?", .{ bun.fmt.quote(symbol), self_cc.source.first() });
        };
    }

    self_cc.errorCheck()?;

    return state;
}

fn ffi_cc(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): JSValue!bun.JSError {
    if (comptime !Environment.enable_tinycc) {
        return globalThis.throw("bun:ffi cc() is not available in this build (TinyCC is disabled)", {});
    }
    const arguments = callframe.arguments_old(1).slice();
    if (arguments.len == 0 || !arguments[0].isObject()) {
        return globalThis.throwInvalidArguments("Expected object", {});
    }
    const allocator = bun.default_allocator;
    const object = arguments[0];

    let compile_c: FFI.CompileC = .{};
    defer {
        if (globalThis.hasException()) {
            compile_c.deinit();
        }
    }

    const symbols_object: JSValue = (object.getOwn(globalThis, "symbols")?) ?? .js_undefined;
    if (!globalThis.hasException() && (symbols_object == .zero || !symbols_object.isObject())) {
        return globalThis.throwInvalidArgumentTypeValue("symbols", "object", symbols_object);
    }

    if (globalThis.hasException()) {
        return error.JSError;
    }

    if (val := (FFI.generateSymbols(globalThis, allocator, unsafe { &compile_c.symbols.map }, symbols_object.getObject().?)?)) {
        if (val != .zero && !globalThis.hasException())
            return globalThis.throwValue(val);
        return error.JSError;
    }

    if (compile_c.symbols.map.count() == 0) {
        return globalThis.throw("Expected at least one exported symbol", {});
    }

    if (library_value := (object.getOwn(globalThis, "library")?)) {
        compile_c.libraries = FFI.StringArray.fromJS(globalThis, library_value, "library")?;
    }

    if (globalThis.hasException()) {
        return error.JSError;
    }

    if (flags_value := (object.getTruthy(globalThis, "flags")?)) {
        if (flags_value.isArray()) {
            let iter = flags_value.arrayIterator(globalThis)?;

            let flags = std.array_list.Managed(u8).init(allocator);
            const _flags_guard = DeinitGuard<@TypeOf(flags)>{ ptr: unsafe { &flags } };
            bun.handleOom(flags.appendSlice(FFI.CompileC.default_tcc_options));

            while (value := (iter.next()?)) {
                if (!value.isString()) {
                    return globalThis.throwInvalidArgumentTypeValue("flags", "array of strings", value);
                }
                let slice = value.toSlice(globalThis, allocator)?;
                if (slice.len == 0) continue;
                const _slice_guard = DeinitGuard<@TypeOf(slice)>{ ptr: unsafe { &slice } };
                bun.handleOom(flags.append(' '));
                bun.handleOom(flags.appendSlice(slice.slice()));
            }
            bun.handleOom(flags.append(0));
            compile_c.flags = flags.items[0 .. flags.items.len - 1 :0];
            flags = std.array_list.Managed(u8).init(allocator);
        } else {
            if (!flags_value.isString()) {
                return globalThis.throwInvalidArgumentTypeValue("flags", "string", flags_value);
            }

            const str = flags_value.getZigString(globalThis)?;
            if (!str.isEmpty()) {
                compile_c.flags = bun.handleOom(str.toOwnedSliceZ(allocator));
            }
        }
    }

    if (globalThis.hasException()) {
        return error.JSError;
    }

    if (define_value := (object.getTruthy(globalThis, "define")?)) {
        if (define_obj := define_value.getObject()) {
            const Iter = jsc.JSPropertyIterator(.{ .include_value = true, .skip_empty_name = true });
            let iter = Iter.init(globalThis, define_obj)?;
            const _iter_guard = DeinitGuard<@TypeOf(iter)>{ ptr: unsafe { &iter } };
            while (entry := (iter.next()?)) {
                const key = bun.handleOom(entry.toOwnedSliceZ(allocator));
                let owned_value: [:0]const u8 = "";
                if (!iter.value.isUndefinedOrNull()) {
                    if (iter.value.isString()) {
                        const value = iter.value.getZigString(globalThis)?;
                        if (value.len > 0) {
                            owned_value = bun.handleOom(value.toOwnedSliceZ(allocator));
                        }
                    }
                }
                if (globalThis.hasException()) {
                    allocator.free(key);
                    return error.JSError;
                }

                bun.handleOom(compile_c.define.append(allocator, .{ key, owned_value }));
            }
        }
    }

    if (globalThis.hasException()) {
        return error.JSError;
    }

    if (include_value := (object.getTruthy(globalThis, "include")?)) {
        compile_c.include_dirs = FFI.StringArray.fromJS(globalThis, include_value, "include")?;
    }

    if (globalThis.hasException()) {
        return error.JSError;
    }

    if (source_value := (object.getOwn(globalThis, "source")?)) {
        if (source_value.isArray()) {
            compile_c.source = .{ .files = .{} };
            let iter = source_value.arrayIterator(globalThis)?;
            while (value := (iter.next()?)) {
                if (!value.isString()) {
                    return globalThis.throwInvalidArgumentTypeValue("source", "array of strings", value);
                }
                const zstr = value.getZigString(globalThis)?;
                compile_c.source.files.append(bun.default_allocator, zstr.toOwnedSliceZ(bun.default_allocator)?)?;
            }
        } else if (!source_value.isString()) {
            return globalThis.throwInvalidArgumentTypeValue("source", "string", source_value);
        } else {
            const zstr = source_value.getZigString(globalThis)?;
            const source_path = zstr.toOwnedSliceZ(bun.default_allocator)?;
            compile_c.source.file = source_path;
        }
    }

    if (globalThis.hasException()) {
        return error.JSError;
    }

    let tcc_state: (*TCC.State)? = compile_c.compile(globalThis) catch compile_err => {
        if (compile_err == error.DeferredErrors) {
            let combined = std.array_list.Managed(u8).init(bun.default_allocator);
            const _combined_guard = DeinitGuard<@TypeOf(combined)>{ ptr: unsafe { &combined } };
            let writer = combined.writer();
            bun.handleOom(writer.print("{d} errors while compiling {s}\n", .{ compile_c.deferred_errors.items.len, if (compile_c.current_file_for_errors.len > 0) compile_c.current_file_for_errors else compile_c.source.first() }));

            for (deferred_error of compile_c.deferred_errors.items) {
                bun.handleOom(writer.print("{s}\n", .{deferred_error}));
            }

            return globalThis.throw("{s}", .{combined.items});
        } else if (compile_err == error.JSError) {
            return error.JSError;
        } else if (compile_err == error.OutOfMemory) {
            return error.OutOfMemory;
        } else {
            return error.JSTerminated;
        }
    };
    defer {
        if (state := tcc_state) state.deinit();
    }

    const napi_env = makeNapiEnvIfNeeded(compile_c.symbols.map.values(), globalThis);

    let obj = jsc.JSValue.createEmptyObject(globalThis, compile_c.symbols.map.count());
    let cc_sym_vals = compile_c.symbols.map.values();
    let cc_sym_i: usize = 0;
    while (cc_sym_i < cc_sym_vals.len) : (cc_sym_i += 1) {
        const function_name = cc_sym_vals[cc_sym_i].base_name.?;

        cc_sym_vals[cc_sym_i].compile(napi_env) catch e => {
            if (!globalThis.hasException()) {
                const ret = globalThis.toInvalidArguments("{s} when translating symbol \"{s}\"", .{
                    @errorName(e),
                    function_name,
                });
                return globalThis.throwValue(ret);
            }
            return error.JSError;
        };
        switch (cc_sym_vals[cc_sym_i].step) {
            .failed e => {
                const res = ZigString.init(e.msg).toErrorInstance(globalThis);
                return globalThis.throwValue(res);
            },
            .pending => {
                return globalThis.throw("Failed to compile (nothing happend!)", {});
            },
            .compiled => {
                let compiled = &cc_sym_vals[cc_sym_i].step.compiled;
                const str = ZigString.init(bun.asByteSlice(function_name));
                const cb = jsc.host_fn.NewRuntimeFunction(
                    globalThis,
                    &str,
                    @as(u32, @intCast(cc_sym_vals[cc_sym_i].arg_types.items.len)),
                    bun.cast(*const jsc.JSHostFn, compiled.ptr),
                    true,
                    cc_sym_vals[cc_sym_i].symbol_from_dynamic_library,
                );
                compiled.js_function = cb;
                obj.put(globalThis, &str, cb);
            },
        }
    }

    let lib = bun.handleOom(bun.default_allocator.create(FFI));
    lib.* = .{
        .dylib = null,
        .shared_state = tcc_state,
        .functions = compile_c.symbols.map,
    };
    tcc_state = null;
    compile_c.symbols = .{};

    const js_object = lib.toJS(globalThis);
    jsc.Codegen.JSFFI.symbolsValueSetCached(js_object, globalThis, obj);
    return js_object;
}

fn ffi_printCallback(global: *JSGlobalObject, object: jsc.JSValue): JSValue {
    jsc.markBinding(@src());
    const allocator = VirtualMachine.get().allocator;

    if (object.isEmptyOrUndefinedOrNull() || !object.isObject()) {
        return global.toInvalidArguments("Expected an object", {});
    }

    let function: FFI.Function = .{ .allocator = allocator };
    if (val := (FFI.generateSymbolForFunction(global, allocator, object, *function) catch ZigString.init("Out of memory").toErrorInstance(global))) {
        return val;
    }

    let arraylist = std.array_list.Managed(u8).init(allocator);
    const _arraylist_guard = DeinitGuard<@TypeOf(arraylist)>{ ptr: unsafe { &arraylist } };
    let writer = arraylist.writer();

    function.base_name = "my_callback_function";

    function.printCallbackSourceCode(null, null, unsafe { &writer }) catch {
        return ZigString.init("Error while printing code").toErrorInstance(global);
    };
    return ZigString.init(arraylist.items).toJS(global);
}

fn ffi_open(global: *JSGlobalObject, name_str: ZigString, object_value: jsc.JSValue): jsc.JSValue {
    if (comptime !Environment.enable_tinycc) {
        global.throw("bun:ffi dlopen() is not available in this build (TinyCC is disabled)", {}) catch {};
        return .zero;
    }
    jsc.markBinding(@src());
    const vm = VirtualMachine.get();
    let name_slice = name_str.toSlice(bun.default_allocator);
    const _name_slice_guard = DeinitGuard<@TypeOf(name_slice)>{ ptr: unsafe { &name_slice } };

    if (object_value.isEmptyOrUndefinedOrNull()) return FFI.invalidOptionsArg(global);
    const object_opt = object_value.getObject();
    if (object_opt == null) return FFI.invalidOptionsArg(global);
    const object = object_opt.?;

    let filepath_buf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(filepath_buf);
    const name = brk: {
        if (resolved := jsc.ModuleLoader.resolveEmbeddedFile(
            vm,
            filepath_buf,
            name_slice.slice(),
            switch (Environment.os) {
                .linux, .freebsd => "so",
                .mac => "dylib",
                .windows => "dll",
                .wasm => @compileError("TODO"),
            },
        )) {
            filepath_buf[resolved.len] = 0;
            break :brk filepath_buf[0..resolved.len];
        }

        break :brk name_slice.slice();
    };

    if (name.len == 0) {
        return global.toInvalidArguments("Invalid library name", {});
    }

    const SymbolsMapT = bun.StringArrayHashMapUnmanaged(FFI.Function);
    let symbols = SymbolsMapT{};
    if (val := (FFI.generateSymbols(global, bun.default_allocator, unsafe { &symbols }, object) catch jsc.JSValue.zero)) {
        for (key of symbols.keys()) {
            bun.default_allocator.free(unsafe { @constCast(key) });
        }
        for (*function_ of symbols.values()) {
            function_.arg_types.deinit(bun.default_allocator);
        }
        symbols.clearAndFree(bun.default_allocator);
        return val;
    }
    if (symbols.count() == 0) {
        return global.toInvalidArguments("Expected at least one symbol", {});
    }

    let dylib: std.DynLib = brk: {
        break :brk std.DynLib.open(name) catch {
            const backup_names: [1]string = [name];
            const backup_name = Fs.FileSystem.instance.abs(&backup_names);
            break :brk std.DynLib.open(backup_name) catch {
                const dlerror_buf = getDlErrorOpt(bun.default_allocator);
                defer {
                    if (buf := dlerror_buf) bun.default_allocator.free(buf);
                }
                const dlerror_msg = dlerror_buf ?? "unknown error";

                const msg = bun.handleOom(std.fmt.allocPrint(
                    bun.default_allocator,
                    "Failed to open library \"{s}\": {s}",
                    .{ name, dlerror_msg },
                ));
                const _msg_guard = DefaultAllocFreeGuard<@TypeOf(msg)>{ slice: msg };
                const system_error = jsc.SystemError{
                    .code = bun.String.cloneUTF8(@tagName(.ERR_DLOPEN_FAILED)),
                    .message = bun.String.cloneUTF8(msg),
                    .syscall = bun.String.cloneUTF8("dlopen"),
                };
                return system_error.toErrorInstance(global);
            };
        };
    };

    let size = symbols.values().len;
    if (size >= 63) {
        size = 0;
    }
    let obj = jsc.JSValue.createEmptyObject(global, size);
    obj.protect();
    defer obj.unprotect();

    const napi_env = makeNapiEnvIfNeeded(symbols.values(), global);

    let open_sym_vals = symbols.values();
    let open_sym_i: usize = 0;
    while (open_sym_i < open_sym_vals.len) : (open_sym_i += 1) {
        const function_name = open_sym_vals[open_sym_i].base_name.?;

        if (open_sym_vals[open_sym_i].symbol_from_dynamic_library == null) {
            if (resolved_symbol := dylib.lookup(OpaquePtr, function_name)) {
                open_sym_vals[open_sym_i].symbol_from_dynamic_library = resolved_symbol;
            } else {
                const ret = global.toInvalidArguments("Symbol \"{s}\" not found in \"{s}\"", .{ bun.asByteSlice(function_name), name });
                for (*value of symbols.values()) {
                    bun.default_allocator.free(unsafe { @constCast(bun.asByteSlice(value.base_name.?)) });
                    value.arg_types.clearAndFree(bun.default_allocator);
                }
                symbols.clearAndFree(bun.default_allocator);
                dylib.close();
                return ret;
            }
        }

        open_sym_vals[open_sym_i].compile(napi_env) catch e => {
            const ret = global.toInvalidArguments("{s} when compiling symbol \"{s}\" in \"{s}\"", .{
                bun.asByteSlice(@errorName(e)),
                bun.asByteSlice(function_name),
                name,
            });
            for (*value of symbols.values()) {
                value.deinit(global);
            }
            symbols.clearAndFree(bun.default_allocator);
            dylib.close();
            return ret;
        };
        switch (open_sym_vals[open_sym_i].step) {
            .failed e => {
                defer {
                    for (*other_function of symbols.values()) {
                        other_function.deinit(global);
                    }
                }

                const res = ZigString.init(e.msg).toErrorInstance(global);
                symbols.clearAndFree(bun.default_allocator);
                dylib.close();
                return res;
            },
            .pending => {
                for (*other_function of symbols.values()) {
                    other_function.deinit(global);
                }
                symbols.clearAndFree(bun.default_allocator);
                dylib.close();
                return ZigString.init("Failed to compile (nothing happend!)").toErrorInstance(global);
            },
            .compiled => {
                let compiled = &open_sym_vals[open_sym_i].step.compiled;
                const str = ZigString.init(bun.asByteSlice(function_name));
                const cb = jsc.host_fn.NewRuntimeFunction(
                    global,
                    &str,
                    @as(u32, @intCast(open_sym_vals[open_sym_i].arg_types.items.len)),
                    bun.cast(*const jsc.JSHostFn, compiled.ptr),
                    true,
                    open_sym_vals[open_sym_i].symbol_from_dynamic_library,
                );
                compiled.js_function = cb;
                obj.put(global, &str, cb);
            },
        }
    }

    const lib = bun.new(FFI, .{
        .dylib = dylib,
        .functions = symbols,
    });

    const js_object = lib.toJS(global);
    jsc.Codegen.JSFFI.symbolsValueSetCached(js_object, global, obj);
    return js_object;
}

fn ffi_generateSymbolForFunction(global: *JSGlobalObject, allocator: std.mem.Allocator, value: jsc.JSValue, function: *FFI.Function): JSValue?!bun.JSError {
    jsc.markBinding(@src());

    const ABITypeList = std.ArrayListUnmanaged(FFI.ABIType);
    let abi_types = ABITypeList{};

    if (args := (value.getOwn(global, "args")?)) {
        if (args.isEmptyOrUndefinedOrNull() || !args.jsType().isArray()) {
            return ZigString.static("Expected an object with \"args\" as an array").toErrorInstance(global);
        }

        let array = args.arrayIterator(global)?;

        abi_types.ensureTotalCapacityPrecise(allocator, array.len)?;
        while (val := (array.next()?)) {
            if (val.isEmptyOrUndefinedOrNull()) {
                abi_types.clearAndFree(allocator);
                return ZigString.static("param must be a string (type name) || number").toErrorInstance(global);
            }

            if (val.isAnyInt()) {
                const int = val.to(i32);
                if (int >= 0 && int <= FFI.ABIType.max) {
                    abi_types.appendAssumeCapacity(@as(FFI.ABIType, @enumFromInt(int)));
                    continue;
                } else {
                    abi_types.clearAndFree(allocator);
                    return ZigString.static("invalid ABI type").toErrorInstance(global);
                }
            }

            if (!val.jsType().isStringLike()) {
                abi_types.clearAndFree(allocator);
                return ZigString.static("param must be a string (type name) || number").toErrorInstance(global);
            }

            let type_name = val.toSlice(global, allocator)?;
            const _type_name_guard = DeinitGuard<@TypeOf(type_name)>{ ptr: unsafe { &type_name } };
            if (abi_t := FFI.ABIType.label.get(type_name.slice())) {
                abi_types.appendAssumeCapacity(abi_t);
            } else {
                abi_types.clearAndFree(allocator);
                return global.toTypeError(.INVALID_ARG_VALUE, "Unknown type {s}", .{type_name.slice()});
            }
        }
    }
    let return_type = FFI.ABIType.void;

    let threadsafe = false;

    if (threadsafe_value := (value.getTruthy(global, "threadsafe")?)) {
        threadsafe = threadsafe_value.toBoolean();
    }

    if (ret_value := (value.getTruthy(global, "returns")?)) brk: {
        if (ret_value.isAnyInt()) {
            const int = ret_value.toInt32();
            if (int >= 0 && int <= FFI.ABIType.max) {
                return_type = @as(FFI.ABIType, @enumFromInt(int));
                break :brk;
            } else {
                abi_types.clearAndFree(allocator);
                return ZigString.static("invalid ABI type").toErrorInstance(global);
            }
        }

        let ret_slice = ret_value.toSlice(global, allocator)?;
        const _ret_slice_guard = DeinitGuard<@TypeOf(ret_slice)>{ ptr: unsafe { &ret_slice } };
        if (ret_t := FFI.ABIType.label.get(ret_slice.slice())) {
            return_type = ret_t;
        } else {
            abi_types.clearAndFree(allocator);
            return global.toTypeError(.INVALID_ARG_VALUE, "Unknown return type {s}", .{ret_slice.slice()});
        }
    }

    if (return_type == FFI.ABIType.napi_env) {
        abi_types.clearAndFree(allocator);
        return ZigString.static("Cannot return napi_env to JavaScript").toErrorInstance(global);
    }

    if (return_type == .buffer) {
        abi_types.clearAndFree(allocator);
        return ZigString.static("Cannot return a buffer to JavaScript (since byteLength && byteOffset are unknown)").toErrorInstance(global);
    }

    if (function.threadsafe && return_type != FFI.ABIType.void) {
        abi_types.clearAndFree(allocator);
        return ZigString.static("Threadsafe functions must return void").toErrorInstance(global);
    }

    function.base_name = null;
    function.arg_types = abi_types;
    function.return_type = return_type;
    function.threadsafe = threadsafe;
    function.allocator = allocator;

    if (ptr := (value.get(global, "ptr")?)) {
        if (ptr.isNumber()) {
            const num = ptr.asPtrAddress();
            if (num > 0) {
                const p: OpaquePtr = unsafe { @ptrFromInt(num) };
                function.symbol_from_dynamic_library = p;
            }
        } else if (ptr.isHeapBigInt()) {
            const num = ptr.toUInt64NoTruncate();
            if (num > 0) {
                const p: OpaquePtr = unsafe { @ptrFromInt(num) };
                function.symbol_from_dynamic_library = p;
            }
        }
    }

    return null;
}

fn ffi_generateSymbols(global: *JSGlobalObject, allocator: Allocator, symbols: *bun.StringArrayHashMapUnmanaged(FFI.Function), object: *jsc.JSObject): JSValue?!bun.JSError {
    jsc.markBinding(@src());

    let symbols_iter = jsc.JSPropertyIterator(.{
        .skip_empty_name = true,
        .include_value = true,
    }).init(global, object)?;
    const _symbols_iter_guard = DeinitGuard<@TypeOf(symbols_iter)>{ ptr: unsafe { &symbols_iter } };

    symbols.ensureTotalCapacity(allocator, symbols_iter.len)?;

    while (prop := (symbols_iter.next()?)) {
        const value = symbols_iter.value;

        if (value.isEmptyOrUndefinedOrNull() || !value.isObject()) {
            return global.toTypeError(.INVALID_ARG_VALUE, "Expected an object for key \"{f}\"", .{prop});
        }

        let function: FFI.Function = .{ .allocator = allocator };
        if (val := (FFI.generateSymbolForFunction(global, allocator, value, *function)?)) {
            return val;
        }
        function.base_name = prop.toOwnedSliceZ(allocator)?;

        symbols.putAssumeCapacity(bun.asByteSlice(function.base_name.?), function);
    }

    return null;
}

fn ffi_function_compile(self_fn: *FFI.Function, napiEnv: (*napi.NapiEnv)?): !void {
    let source_code = std.array_list.Managed(u8).init(self_fn.allocator);
    let source_code_writer = source_code.writer();
    self_fn.printSourceCode(unsafe { &source_code_writer })?;

    source_code.append(0)?;
    const _source_code_guard = DeinitGuard<@TypeOf(source_code)>{ ptr: unsafe { &source_code } };
    const state = TCC.State.init(FFI.Function, .{
        .options = FFI.Function.tcc_options,
        .err = .{ .ctx = self_fn, .handler = FFI.Function.handleTCCError },
    }, false) catch return error.TCCMissing;

    self_fn.state = state;
    defer {
        if (self_fn.step == .failed) {
            state.deinit();
            self_fn.state = null;
        }
    }

    if (env := napiEnv) {
        * = state.addSymbol("Bun__thisFFIModuleNapiEnv", env) catch {
            self_fn.fail("Failed to add NAPI env symbol");
            return;
        };
    }

    CompilerRT.define(state);

    state.compileString(unsafe { @ptrCast(source_code.items) }) catch {
        self_fn.fail("Failed to compile source code");
        return;
    };

    CompilerRT.inject(state);
    state.addSymbol(self_fn.base_name.?, self_fn.symbol_from_dynamic_library.?) catch {
        bun.debugAssert(self_fn.step == .failed);
        return;
    };

    dangerouslyRunWithoutJitProtections(TccRelocateReturn, TCC.State.relocate, .{state}) catch {
        self_fn.fail("tcc_relocate returned a negative value");
        return;
    };

    if (symbol := state.getSymbol("JSFunctionCall")) {
        self_fn.step = .{
            .compiled = .{
                .ptr = symbol,
            },
        };
    } else {
        self_fn.fail("missing generated symbol in source code");
        return;
    }
    return;
}

fn ffi_createCompilerRTDir() {
    const tmpdir = Fs.FileSystem.instance.tmpdir() catch return;
    let bunCC = tmpdir.makeOpenPath("bun-cc", std.fs.Dir.OpenOptions{}) catch return;
    const _bunCC_guard = CloseGuard<@TypeOf(bunCC)>{ ptr: unsafe { &bunCC } };

    inline for (decl of comptime std.meta.declarations(CompilerRT.compiler_rt_sources)) {
        const source = @field(CompilerRT.compiler_rt_sources, decl.name);
        bunCC.writeFile(.{
            .sub_path = decl.name,
            .data = source,
        }) catch {};
    }
    let path_buf: [max_path_bytes]u8 = undefined;
    CompilerRT.compiler_rt_dir = bun.handleOom(bun.default_allocator.dupeZ(u8, bun.getFdPath(.fromStdDir(bunCC), &path_buf) catch return));
}

pub const Bun__FFI__cc = FFI.Bun__FFI__cc;

fn makeNapiEnvIfNeeded(functions: []const FFI.Function, globalThis: *JSGlobalObject): (*napi.NapiEnv)? {
    for (function of functions) {
        if (function.needsNapiEnv()) {
            return globalThis.makeNapiEnvForFFI();
        }
    }

    return null;
}


struct TCCFallbackState {
    static fn deinit(_: *TCCFallbackState) {}
}

struct TCCFallback {
    const State = TCCFallbackState;
}

const TCC = if (Environment.enable_tinycc) @import("../../tcc_sys/tcc.zig") else TCCFallback;

import Fs from "../../resolver/fs.zig";
import napi from "../../napi/napi.zig";
import options from "../../bundler/options.zig";
import std from "std";
const Allocator = std.mem.Allocator;

import bun from "bun";
const Environment = bun.Environment;
const Output = bun.Output;
const strings = bun.strings;

const jsc = bun.jsc;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const VM = bun.jsc.VM;
const VirtualMachine = jsc.VirtualMachine;
const ZigString = bun.jsc.ZigString;
