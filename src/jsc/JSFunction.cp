const _ImplementationVisibility = enum(u8) {
    public,
    private,
    private_recursive,
};

/// In WebKit: Intrinsic.h
const _Intrinsic = enum(u8) {
    none,
    _,
};

const _CreateJSFunctionOptions = struct {
    implementation_visibility: _ImplementationVisibility = .public,
    intrinsic: _Intrinsic = .none,
    constructor: (*const JSHostFn)? = null,
};

@c {
    fn JSFunction__createFromZig(
        global: *JSGlobalObject,
        fn_name: bun.String,
        implementation: *const JSHostFn,
        arg_count: u32,
        implementation_visibility: _ImplementationVisibility,
        intrinsic: _Intrinsic,
        constructor: (*const JSHostFn)?,
    ): JSValue;

    fn JSC__JSFunction__optimizeSoon(value: JSValue);

    fn JSC__JSFunction__getSourceCode(value: JSValue, out: *ZigString): bool;
}

@opaque pub struct JSFunction {
    pub const ImplementationVisibility = _ImplementationVisibility;
    pub const Intrinsic = _Intrinsic;
    pub const CreateJSFunctionOptions = _CreateJSFunctionOptions;

    static fn create(
        global: *JSGlobalObject,
        fn_name: auto,
        comptime implementation: auto,
        function_length: u32,
        options: _CreateJSFunctionOptions,
    ): JSValue {
        return JSFunction__createFromZig(
            global,
            switch (@TypeOf(fn_name)) {
                bun.String => fn_name,
                else => bun.String.init(fn_name),
            },
            switch (@TypeOf(implementation)) {
                jsc.JSHostFnZig => jsc.toJSHostFn(implementation),
                jsc.JSHostFn => implementation,
                else => @compileError("unexpected function type"),
            },
            function_length,
            options.implementation_visibility,
            options.intrinsic,
            options.constructor,
        );
    }

    static fn optimizeSoon(value: JSValue) {
        JSC__JSFunction__optimizeSoon(value);
    }

    static fn getSourceCode(value: JSValue): bun.String? {
        let str: ZigString = undefined;
        return if (JSC__JSFunction__getSourceCode(value, &str)) bun.String.init(str) else null;
    }
}

import bun from "bun";
const String = bun.String;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSHostFn = jsc.JSHostFn;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
