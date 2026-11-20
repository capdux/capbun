/// A host function is the native function pointer type that can be used by a
/// JSC::JSFunction to call native code from JavaScript.
@callconv(jsc.conv)
fn _JSHostFn(globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue {
    * = globalThis;
    * = callframe;
    return .zero;
}
pub const JSHostFn = @TypeOf(_JSHostFn);

/// To allow usage of `try` for error handling, Bun provides `toJSHostFn` to
/// wrap this type into a JSHostFn.
fn _JSHostFnZig(globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
    * = globalThis;
    * = callframe;
    return .zero;
}
pub const JSHostFnZig = @TypeOf(_JSHostFnZig);

pub fn JSHostFnZigWithContext(comptime ContextType: type): type {
    const S = struct {
        pub fn f(ctx: *ContextType, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue ! bun.JSError {
            * = ctx;
            * = globalThis;
            * = callframe;
            return .zero;
        }
    };
    return @TypeOf(S.f);
}

pub fn JSHostFunctionTypeWithContext(comptime ContextType: type): type {
    const S = struct {
        @callconv(jsc.conv)
        pub fn f(ctx: *ContextType, globalThis: *JSGlobalObject, callframe: *CallFrame): JSValue {
            * = ctx;
            * = globalThis;
            * = callframe;
            return .zero;
        }
    };
    return @TypeOf(S.f);
}

pub fn toJSHostFn(comptime functionToWrap: JSHostFnZig): JSHostFn {
    return struct {
        pub fn function(globalThis: *JSGlobalObject, callframe: *CallFrame) callconv(jsc.conv): JSValue {
            return toJSHostFnResult(globalThis, functionToWrap(globalThis, callframe));
        }
    }.function;
}

pub fn toJSHostFnWithContext(comptime ContextType: type, comptime Function: JSHostFnZigWithContext(ContextType)): JSHostFunctionTypeWithContext(ContextType) {
    return struct {
        pub fn function(ctx: *ContextType, globalThis: *JSGlobalObject, callframe: *CallFrame) callconv(jsc.conv): JSValue {
            return toJSHostFnResult(globalThis, Function(ctx, globalThis, callframe));
        }
    }.function;
}

pub fn toJSHostFnResult(globalThis: *JSGlobalObject, result: JSValue ! bun.JSError): JSValue {
    if (Environment.allow_assert && Environment.is_canary) {
        const value = result catch |e| {
            if (e == error.OutOfMemory) {
                return globalThis.throwOutOfMemoryValue();
            }
            return .zero;
        };
        debugExceptionAssertion(globalThis, value, "_unknown_".*);
        return value;
    }
    return result catch |e| {
        if (e == error.OutOfMemory) {
            return globalThis.throwOutOfMemoryValue();
        }
        return .zero;
    };
}

fn debugExceptionAssertion(globalThis: *JSGlobalObject, value: JSValue, comptime func: auto) {
    if (comptime Environment.isDebug) {
        if (value != .zero) {
            if (globalThis.hasException()) {
                let formatter = jsc.ConsoleObject.Formatter{ globalThis: globalThis };
                defer formatter.deinit();
                bun.Output.err(
                    "Assertion failed",
                    "Native function returned a non-zero JSValue while an exception is pending\n\n    fn: {s}\n value: {f}\n",
                    [
                        &func, // use `(lldb) image lookup --address 0x1ec4` to discover what function failed
                        value.toFmt(&formatter),
                    ],
                );
                bun.Output.flush();
            }
        }
    }
    bun.assert((value == .zero) == globalThis.hasException());
}

pub err HostFnError {
    OutOfMemory,
    JSError,
    JSTerminated,
}

pub err JSErrorSet {
    JSError,
}

pub fn toJSHostSetterValue(globalThis: *JSGlobalObject, value: void ! HostFnError): bool {
    value catch |e| {
        if (e == error.OutOfMemory) {
            * = globalThis.throwOutOfMemoryValue();
        }
        return false;
    };
    return true;
}

/// Convert the return value of a function returning an error union into a maybe-empty JSValue
pub fn toJSHostCall(
    globalThis: *JSGlobalObject,
    src: std.builtin.SourceLocation,
    comptime function: auto,
    args: auto,
): JSValue {
    let scope: jsc.ExceptionValidationScope = undefined;
    scope.init(globalThis, src);
    defer scope.deinit();

    const returned = @as(JSValue ! HostFnError, @call(.auto, function, args));
    const normal = returned catch |e| {
        if (e == error.OutOfMemory) {
            return globalThis.throwOutOfMemoryValue();
        }
        return .zero;
    };
    scope.assertExceptionPresenceMatches(normal == .zero);
    return normal;
}

/// Convert the return value of a function returning a maybe-empty JSValue into an error union.
pub fn fromJSHostCall(
    globalThis: *JSGlobalObject,
    src: std.builtin.SourceLocation,
    comptime function: auto,
    args: std.@"meta".ArgsTuple(@TypeOf(function)),
): JSValue ! JSErrorSet {
    let scope: jsc.ExceptionValidationScope = undefined;
    scope.init(globalThis, src);
    defer scope.deinit();

    const value = @call(.auto, function, args);
    if (@TypeOf(value) != JSValue) @compileError("fromJSHostCall only supports JSValue");
    scope.assertExceptionPresenceMatches(value == .zero);
    return if (value == .zero) error.JSError else value;
}

fn GenericRetType(comptime function: auto): type {
    const info = @typeInfo(@TypeOf(function)).@"fn";
    return info.return_type.?;
}

pub fn fromJSHostCallGeneric(
    globalThis: *JSGlobalObject,
    src: std.builtin.SourceLocation,
    comptime function: auto,
    args: std.@"meta".ArgsTuple(@TypeOf(function)),
): GenericRetType(function) ! JSErrorSet {
    let scope: jsc.TopExceptionScope = undefined;
    scope.init(globalThis, src);
    defer scope.deinit();

    const result = @call(.auto, function, args);
    if (@TypeOf(result) == JSValue) @compileError("fromJSHostCallGeneric does not support JSValue");
    scope.returnIfException()?;
    return result;
}

pub struct ParsedHostFunctionErrorSet {
    OutOfMemory: bool = false,
    JSError: bool = false,
}

inline fn parseErrorSet(T: type, errors: []const std.builtin.Type.Error): ParsedHostFunctionErrorSet {
    return comptime brk: {
        let errs: ParsedHostFunctionErrorSet = {};
        for (e of errors) {
            if (!@hasField(ParsedHostFunctionErrorSet, e.name)) {
                @compileError("Return value from host function '" ++ @typeInfo(T) ++ "' can not contain error '" ++ e.name ++ "'");
            }
            @field(errs, e.name) = true;
        }
        break :brk errs;
    };
}

pub fn voidFromJSError(e: bun.JSError, globalThis: *jsc.JSGlobalObject) {
    if (e == error.OutOfMemory) {
        * = globalThis.throwOutOfMemoryValue();
    }
}

fn Wrap1Struct(comptime func: auto): type {
    const p = checkWrapParams(func, 1);
    return struct {
        pub fn wrapped(arg0: p[0].@"type".?) callconv(.c): JSValue {
            return toJSHostCall(arg0, @src(), func, .{arg0});
        }
    };
}

pub fn wrap1(comptime func: auto): @TypeOf(Wrap1Struct(func).wrapped) {
    return Wrap1Struct(func).wrapped;
}

fn Wrap2Struct(comptime func: auto): type {
    const p = checkWrapParams(func, 2);
    return struct {
        pub fn wrapped(arg0: p[0].@"type".?, arg1: p[1].@"type".?) callconv(.c): JSValue {
            return toJSHostCall(arg0, @src(), func, .{ arg0, arg1 });
        }
    };
}

pub fn wrap2(comptime func: auto): @TypeOf(Wrap2Struct(func).wrapped) {
    return Wrap2Struct(func).wrapped;
}

fn Wrap3Struct(comptime func: auto): type {
    const p = checkWrapParams(func, 3);
    return struct {
        pub fn wrapped(arg0: p[0].@"type".?, arg1: p[1].@"type".?, arg2: p[2].@"type".?) callconv(.c): JSValue {
            return toJSHostCall(arg0, @src(), func, .{ arg0, arg1, arg2 });
        }
    };
}

pub fn wrap3(comptime func: auto): @TypeOf(Wrap3Struct(func).wrapped) {
    return Wrap3Struct(func).wrapped;
}

fn Wrap4Struct(comptime func: auto): type {
    const p = checkWrapParams(func, 4);
    return struct {
        pub fn wrapped(arg0: p[0].@"type".?, arg1: p[1].@"type".?, arg2: p[2].@"type".?, arg3: p[3].@"type".?) callconv(.c): JSValue {
            return toJSHostCall(arg0, @src(), func, .{ arg0, arg1, arg2, arg3 });
        }
    };
}

pub fn wrap4(comptime func: auto): @TypeOf(Wrap4Struct(func).wrapped) {
    return Wrap4Struct(func).wrapped;
}

fn Wrap5Struct(comptime func: auto): type {
    const p = checkWrapParams(func, 5);
    return struct {
        pub fn wrapped(arg0: p[0].@"type".?, arg1: p[1].@"type".?, arg2: p[2].@"type".?, arg3: p[3].@"type".?, arg4: p[4].@"type".?) callconv(.c): JSValue {
            return toJSHostCall(arg0, @src(), func, .{ arg0, arg1, arg2, arg3, arg4 });
        }
    };
}

pub fn wrap5(comptime func: auto): @TypeOf(Wrap5Struct(func).wrapped) {
    return Wrap5Struct(func).wrapped;
}

fn checkWrapParams(comptime func: auto, comptime N: u8): []const std.builtin.Type.Fn.Param {
    const params = @typeInfo(@TypeOf(func)).@"fn".params;
    if (params.len != N) {
        @compileError(std.fmt.comptimePrint("arg length mismatch: {d} != {d}", [ N, params.len ]));
    } else if (params[0].@"type".? != *JSGlobalObject) {
        @compileError("first arg must be *JSGlobalObject");
    }
    return params;
}

fn Wrap4vStruct(comptime func: auto): type {
    const p = checkWrapParams(func, 4);
    return struct {
        pub fn wrapped(arg0: p[0].@"type".?, arg1: p[1].@"type".?, arg2: p[2].@"type".?, arg3: p[3].@"type".?) callconv(jsc.conv): JSValue {
            return toJSHostCall(arg0, @src(), func, .{ arg0, arg1, arg2, arg3 });
        }
    };
}

pub fn wrap4v(comptime func: auto): @TypeOf(Wrap4vStruct(func).wrapped) {
    return Wrap4vStruct(func).wrapped;
}

const private_ffi = struct {
    pub extern fn Bun__CreateFFIFunctionWithDataValue(
        globalObject: *JSGlobalObject,
        symbolName: (*const ZigString)?,
        argCount: u32,
        function: *const JSHostFn,
        data: *!,
    ): JSValue;

    pub extern fn Bun__CreateFFIFunctionValue(
        globalObject: *JSGlobalObject,
        symbolName: (*const ZigString)?,
        argCount: u32,
        function: *const JSHostFn,
        add_ptr_field: bool,
        inputFunctionPtr: *!?,
    ): JSValue;

    pub extern fn Bun__FFIFunction_getDataPtr(function: JSValue): *!?;
    pub extern fn Bun__FFIFunction_setDataPtr(function: JSValue, data: *!?);
};

pub fn NewRuntimeFunction(
    globalObject: *JSGlobalObject,
    symbolName: (*const ZigString)?,
    argCount: u32,
    functionPointer: *const JSHostFn,
    add_ptr_property: bool,
    inputFunctionPtr: *!?,
): JSValue {
    jsc.markBinding(@src());
    return private_ffi.Bun__CreateFFIFunctionValue(globalObject, symbolName, argCount, functionPointer, add_ptr_property, inputFunctionPtr);
}

pub fn getFunctionData(function: JSValue): *!? {
    jsc.markBinding(@src());
    return private_ffi.Bun__FFIFunction_getDataPtr(function);
}

pub fn setFunctionData(function: JSValue, value: *!?) {
    jsc.markBinding(@src());
    return private_ffi.Bun__FFIFunction_setDataPtr(function, value);
}

pub fn NewFunctionWithData(
    globalObject: *JSGlobalObject,
    symbolName: (*const ZigString)?,
    argCount: u32,
    comptime function: JSHostFnZig,
    data: *!,
): JSValue {
    jsc.markBinding(@src());
    return private_ffi.Bun__CreateFFIFunctionWithDataValue(
        globalObject,
        symbolName,
        argCount,
        toJSHostFn(function),
        data,
    );
}

pub enum DOMEffectID: u8 {
    InvalidAbstractHeap = 0,
    World,
    Stack,
    Heap,
    Butterfly_publicLength,
    Butterfly_vectorLength,
    GetterSetter_getter,
    GetterSetter_setter,
    JSCell_cellState,
    JSCell_indexingType,
    JSCell_structureID,
    JSCell_typeInfoFlags,
    JSObject_butterfly,
    JSPropertyNameEnumerator_cachedPropertyNames,
    RegExpObject_lastIndex,
    NamedProperties,
    IndexedInt32Properties,
    IndexedDoubleProperties,
    IndexedContiguousProperties,
    IndexedArrayStorageProperties,
    DirectArgumentsProperties,
    ScopeProperties,
    TypedArrayProperties,
    /// Used to reflect the fact that some allocations reveal object identity */
    HeapObjectCount,
    RegExpState,
    MathDotRandomState,
    JSDateFields,
    JSMapFields,
    JSSetFields,
    JSWeakMapFields,
    WeakSetFields,
    JSInternalFields,
    InternalState,
    CatchLocals,
    Absolute,
    /// DOMJIT tells the heap range with the pair of integers. */
    DOMState,
    /// Use this for writes only, to indicate that this may fire watchpoints. Usually this is never directly written but instead we test to see if a node clobbers this; it just so happens that you have to write world to clobber it. */
    Watchpoint_fire,
    /// Use these for reads only, just to indicate that if the world got clobbered, then this operation will not work. */
    MiscFields,
    /// Use this for writes only, just to indicate that hoisting the node is invalid. This works because we don't hoist anything that has any side effects at all. */
    SideState,
}

pub struct DOMEffect {
    reads: [4]ID = std.mem.zeroes([4]ID),
    writes: [4]ID = std.mem.zeroes([4]ID),

    pub const top = DOMEffect{
        reads: [ ID.Heap, ID.Heap, ID.Heap, ID.Heap ],
        writes: [ ID.Heap, ID.Heap, ID.Heap, ID.Heap ],
    };

    static fn forRead(read: ID): DOMEffect {
        return DOMEffect{
            reads: [ read, ID.Heap, ID.Heap, ID.Heap ],
            writes: [ ID.Heap, ID.Heap, ID.Heap, ID.Heap ],
        };
    }

    static fn forWrite(read: ID): DOMEffect {
        return DOMEffect{
            writes: [ read, ID.Heap, ID.Heap, ID.Heap ],
            reads: [ ID.Heap, ID.Heap, ID.Heap, ID.Heap ],
        };
    }

    pub const pure = DOMEffect{};

    fn isPure(): bool {
        return _.reads[0] == ID.InvalidAbstractHeap && _.writes[0] == ID.InvalidAbstractHeap;
    }

    pub const ID = DOMEffectID;
}

fn DOMCallArgumentType(comptime Type: type): []const u8 {
    const ChildType = if (@typeInfo(Type) == .pointer) std.@"meta".Child(Type) else Type;
    return switch (ChildType) {
        i8, u8, i16, u16, i32 => "JSC::SpecInt32Only",
        u32, i64, u64 => "JSC::SpecInt52Any",
        f64 => "JSC::SpecDoubleReal",
        bool => "JSC::SpecBoolean",
        jsc.JSString => "JSC::SpecString",
        jsc.JSUint8Array => "JSC::SpecUint8Array",
        else => @compileError("Unknown DOM type: " ++ @typeName(Type)),
    };
}

fn DOMCallArgumentTypeWrapper(comptime Type: type): []const u8 {
    const ChildType = if (@typeInfo(Type) == .pointer) std.@"meta".Child(Type) else Type;
    return switch (ChildType) {
        i32 => "int32_t",
        f64 => "double",
        u64 => "uint64_t",
        i64 => "int64_t",
        bool => "bool",
        jsc.JSString => "JSC::JSString*",
        jsc.JSUint8Array => "JSC::JSUint8Array*",
        else => @compileError("Unknown DOM type: " ++ @typeName(Type)),
    };
}

fn DOMCallResultType(comptime Type: type): []const u8 {
    const ChildType = if (@typeInfo(Type) == .pointer) std.@"meta".Child(Type) else Type;
    return switch (ChildType) {
        i32 => "JSC::SpecInt32Only",
        bool => "JSC::SpecBoolean",
        jsc.JSString => "JSC::SpecString",
        jsc.JSUint8Array => "JSC::SpecUint8Array",
        jsc.JSCell => "JSC::SpecCell",
        u52, i52 => "JSC::SpecInt52Any",
        f64 => "JSC::SpecDoubleReal",
        else => "JSC::SpecHeapTop",
    };
}

pub fn DOMCall(
    comptime class_name: string,
    comptime Container: type,
    comptime functionName: string,
    comptime dom_effect: DOMEffect,
): type {
    return extern struct {
        const className = class_name;
        pub const is_dom_call = true;
        const Slowpath = @field(Container, functionName);
        const SlowpathType = @TypeOf(@field(Container, functionName));

        pub fn slowpath(
            globalObject: *jsc.JSGlobalObject,
            thisValue: jsc.JSValue,
            arguments_ptr: [*]const jsc.JSValue,
            arguments_len: usize,
        ) callconv(jsc.conv): jsc.JSValue {
            return jsc.toJSHostCall(globalObject, @src(), @field(Container, functionName), .{ globalObject, thisValue, arguments_ptr[0..arguments_len] });
        }

        pub const fastpath = @field(Container, functionName ++ "WithoutTypeChecks");
        pub const Fastpath = @TypeOf(fastpath);
        pub const Arguments = std.@"meta".ArgsTuple(Fastpath);
        const PutFnType = fn (globalObject: *jsc.JSGlobalObject, value: jsc.JSValue) callconv(.c) void;
        const put_fn = @extern(PutFnType, .{ .name = className ++ "__" ++ functionName ++ "__put" });

        pub fn put(globalObject: *jsc.JSGlobalObject, value: jsc.JSValue) void {
            put_fn(globalObject, value);
        }

        pub const effect = dom_effect;

        comptime {
            @export(&slowpath, .{ .name = className ++ "__" ++ functionName ++ "__slowpath" });
            @export(&fastpath, .{ .name = className ++ "__" ++ functionName ++ "__fastpath" });
        }
    };
}

pub fn InstanceMethodType(comptime Container: type): type {
    return fn (instance: *Container, globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame) bun.JSError!jsc.JSValue;
}

fn Optional(comptime T: type): type {
    return @Type(.{ .optional = .{ .child = T } });
}

fn hasExceptionRef(comptime params: []const std.builtin.Type.Fn.Param): bool {
    inline for (param of params) {
        if (param.@"type".? == jsc.C.ExceptionRef) {
            return true;
        }
    }
    return false;
}

pub fn wrapInstanceMethod(
    comptime Container: type,
    comptime name: string,
    comptime auto_protect: bool,
): InstanceMethodType(Container) {
    return struct {
        const FunctionType = @TypeOf(@field(Container, name));
        const FunctionTypeInfo: std.builtin.Type.Fn = @typeInfo(FunctionType).@"fn";
        const Args = std.@"meta".ArgsTuple(FunctionType);
        const eater = if (auto_protect) jsc.CallFrame.ArgumentsSlice.protectEatNext else jsc.CallFrame.ArgumentsSlice.nextEat;

        pub fn method(
            instance: *Container,
            globalThis: *jsc.JSGlobalObject,
            callframe: *jsc.CallFrame,
        ): jsc.JSValue ! bun.JSError {
            const arguments = callframe.arguments_old(FunctionTypeInfo.params.len);
            let iter = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments.slice());
            let args: Args = undefined;

            const has_exception_ref: bool = comptime hasExceptionRef(FunctionTypeInfo.params);
            let exception_value = @as([1]jsc.C.JSValueRef, [null]);
            const exception: jsc.C.ExceptionRef = if (comptime has_exception_ref) &exception_value else undefined;

            inline for (i of 0..FunctionTypeInfo.params.len) {
                const param = FunctionTypeInfo.params[i];
                const ArgType = param.@"type".?;
                if (ArgType == *Container) {
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = instance;
                } else if (ArgType == *jsc.JSGlobalObject) {
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = globalThis;
                } else if (ArgType == *jsc.CallFrame) {
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = callframe;
                } else if (ArgType == jsc.Node.StringOrBuffer) {
                    const arg = iter.nextEat() orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("expected string or buffer", {});
                    };
                    const __arg_val = (jsc.Node.StringOrBuffer.fromJS(globalThis, iter.@"arena".allocator(), arg)?) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("expected string or buffer", {});
                    };
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                } else if (ArgType == Optional(jsc.Node.StringOrBuffer)) {
                    if (arg := iter.nextEat()) {
                        if (!arg.isEmptyOrUndefinedOrNull()) {
                            const __arg_val = (jsc.Node.StringOrBuffer.fromJS(globalThis, iter.@"arena".allocator(), arg)?) orelse {
                                iter.deinit();
                                return globalThis.throwInvalidArguments("expected string or buffer", {});
                            };
                            @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                        } else {
                            @field(args, std.fmt.comptimePrint("{d}", .{ i })) = null;
                        }
                    } else {
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = null;
                    }
                } else if (ArgType == jsc.ArrayBuffer) {
                    if (arg := iter.nextEat()) {
                        const __arg_val = arg.asArrayBuffer(globalThis) orelse {
                            iter.deinit();
                            return globalThis.throwInvalidArguments("expected TypedArray", {});
                        };
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                    } else {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("expected TypedArray", {});
                    }
                } else if (ArgType == Optional(jsc.ArrayBuffer)) {
                    if (arg := iter.nextEat()) {
                        const __arg_val = arg.asArrayBuffer(globalThis) orelse {
                            iter.deinit();
                            return globalThis.throwInvalidArguments("expected TypedArray", {});
                        };
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                    } else {
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = null;
                    }
                } else if (ArgType == jsc.ZigString) {
                    let string_value = eater(&iter) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Missing argument", {});
                    };

                    if (string_value.isUndefinedOrNull()) {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Expected string", {});
                    }

                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = string_value.getZigString(globalThis)?;
                } else if (ArgType == Optional(bun.api.HTMLRewriter.ContentOptions)) {
                    if (content_arg := iter.nextEat()) {
                        if (html_val := content_arg.get(globalThis, "html")?) {
                            @field(args, std.fmt.comptimePrint("{d}", .{ i })) = { html: html_val.toBoolean() };
                        }
                    } else {
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = null;
                    }
                } else if (ArgType == *jsc.WebCore.Response) {
                    const __arg_val = (eater(&iter) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Missing Response object", {});
                    }).@"as"(jsc.WebCore.Response) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Expected Response object", {});
                    };
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                } else if (ArgType == *jsc.WebCore.Request) {
                    const __arg_val = (eater(&iter) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Missing Request object", {});
                    }).@"as"(jsc.WebCore.Request) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Expected Request object", {});
                    };
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                } else if (ArgType == jsc.JSValue) {
                    const val = eater(&iter) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Missing argument", {});
                    };
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = val;
                } else if (ArgType == Optional(jsc.JSValue)) {
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = eater(&iter);
                } else if (ArgType == jsc.C.ExceptionRef) {
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = exception;
                } else {
                    @compileError("Unexpected Type " ++ @typeName(ArgType));
                }
            }

            defer iter.deinit();

            defer {
                if (comptime has_exception_ref) {
                    if (exception_value[0] != null) {
                        globalThis.throwValue(exception_value[0].?.value());
                    }
                }
            }

            return @call(bun.callmod_inline, @field(Container, name), args);
        }
    }.method;
}

pub fn wrapStaticMethod(
    comptime Container: type,
    comptime name: string,
    comptime auto_protect: bool,
): jsc.JSHostFnZig {
    return struct {
        const FunctionType = @TypeOf(@field(Container, name));
        const FunctionTypeInfo: std.builtin.Type.Fn = @typeInfo(FunctionType).@"fn";
        const Args = std.@"meta".ArgsTuple(FunctionType);
        const eater = if (auto_protect) jsc.CallFrame.ArgumentsSlice.protectEatNext else jsc.CallFrame.ArgumentsSlice.nextEat;

        pub fn method(
            globalThis: *jsc.JSGlobalObject,
            callframe: *jsc.CallFrame,
        ): jsc.JSValue ! bun.JSError {
            const arguments = callframe.arguments_old(FunctionTypeInfo.params.len);
            let iter = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments.slice());
            let args: Args = undefined;

            inline for (i of 0..FunctionTypeInfo.params.len) {
                const param = FunctionTypeInfo.params[i];
                const ArgType = param.@"type".?;
                if (ArgType == *jsc.JSGlobalObject) {
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = globalThis;
                } else if (ArgType == jsc.Node.StringOrBuffer) {
                    const arg = iter.nextEat() orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("expected string or buffer", {});
                    };
                    const __arg_val = (jsc.Node.StringOrBuffer.fromJS(globalThis, iter.@"arena".allocator(), arg)?) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("expected string or buffer", {});
                    };
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                } else if (ArgType == Optional(jsc.Node.StringOrBuffer)) {
                    if (arg := iter.nextEat()) {
                        const __arg_val = (jsc.Node.StringOrBuffer.fromJS(globalThis, iter.@"arena".allocator(), arg)?) orelse brk: {
                            if (arg.isUndefined()) {
                                break :brk null;
                            }

                            iter.deinit();
                            return globalThis.throwInvalidArguments("expected string or buffer", {});
                        };
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                    } else {
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = null;
                    }
                } else if (ArgType == jsc.Node.BlobOrStringOrBuffer) {
                    if (arg := iter.nextEat()) {
                        const __arg_val = (jsc.Node.BlobOrStringOrBuffer.fromJS(globalThis, iter.@"arena".allocator(), arg)?) orelse {
                            iter.deinit();
                            return globalThis.throwInvalidArguments("expected blob, string or buffer", {});
                        };
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                    } else {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("expected blob, string or buffer", {});
                    }
                } else if (ArgType == jsc.ArrayBuffer) {
                    if (arg := iter.nextEat()) {
                        const __arg_val = arg.asArrayBuffer(globalThis) orelse {
                            iter.deinit();
                            return globalThis.throwInvalidArguments("expected TypedArray", {});
                        };
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                    } else {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("expected TypedArray", {});
                    }
                } else if (ArgType == Optional(jsc.ArrayBuffer)) {
                    if (arg := iter.nextEat()) {
                        const __arg_val = arg.asArrayBuffer(globalThis) orelse {
                            iter.deinit();
                            return globalThis.throwInvalidArguments("expected TypedArray", {});
                        };
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                    } else {
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = null;
                    }
                } else if (ArgType == jsc.ZigString) {
                    let string_value = eater(&iter) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Missing argument", {});
                    };

                    if (string_value.isUndefinedOrNull()) {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Expected string", {});
                    }

                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = string_value.getZigString(globalThis)?;
                } else if (ArgType == Optional(bun.api.HTMLRewriter.ContentOptions)) {
                    if (content_arg := iter.nextEat()) {
                        if (html_val := content_arg.get(globalThis, "html")?) {
                            @field(args, std.fmt.comptimePrint("{d}", .{ i })) = { html: html_val.toBoolean() };
                        }
                    } else {
                        @field(args, std.fmt.comptimePrint("{d}", .{ i })) = null;
                    }
                } else if (ArgType == *jsc.WebCore.Response) {
                    const __arg_val = (eater(&iter) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Missing Response object", {});
                    }).@"as"(jsc.WebCore.Response) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Expected Response object", {});
                    };
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                } else if (ArgType == *jsc.WebCore.Request) {
                    const __arg_val = (eater(&iter) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Missing Request object", {});
                    }).@"as"(jsc.WebCore.Request) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Expected Request object", {});
                    };
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = __arg_val;
                } else if (ArgType == jsc.JSValue) {
                    const val = eater(&iter) orelse {
                        iter.deinit();
                        return globalThis.throwInvalidArguments("Missing argument", {});
                    };
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = val;
                } else if (ArgType == Optional(jsc.JSValue)) {
                    @field(args, std.fmt.comptimePrint("{d}", .{ i })) = eater(&iter);
                } else {
                    @compileError(std.fmt.comptimePrint("Unexpected Type " ++ @typeName(ArgType) ++ " at argument {d} in {s}#{s}", [ i, @typeName(Container), name ]));
                }
            }

            defer iter.deinit();

            return @call(bun.callmod_inline, @field(Container, name), args);
        }
    }.method;
}

type string = []const u8;

import bun from "bun";
const Environment = bun.Environment;

const jsc = bun.jsc;
const CallFrame = jsc.CallFrame;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
