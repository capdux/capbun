import bun from "bun";
import js_internal_promise_mod from "./JSInternalPromise.zig";

@c {
    fn JSC__JSModuleLoader__evaluate(
        globalObject: *JSGlobalObject,
        sourceCodePtr: [*]const u8,
        sourceCodeLen: usize,
        originUrlPtr: [*]const u8,
        originUrlLen: usize,
        referrerUrlPtr: [*]const u8,
        referrerUrlLen: usize,
        thisValue: JSValue,
        exception: [*]JSValue,
    ): JSValue ;

    fn JSC__JSModuleLoader__loadAndEvaluateModule(arg0: *JSGlobalObject, arg1: (*const String)?): *JSInternalPromise ;

    fn JSModuleLoader__import(globalObject: *JSGlobalObject, module_name: *const bun.String): (*JSInternalPromise)? ;
}

@opaque pub struct JSModuleLoader {
    static fn evaluate(
        globalObject: *JSGlobalObject,
        sourceCodePtr: [*]const u8,
        sourceCodeLen: usize,
        originUrlPtr: [*]const u8,
        originUrlLen: usize,
        referrerUrlPtr: [*]const u8,
        referrerUrlLen: usize,
        thisValue: JSValue,
        exception: [*]JSValue,
    ): JSValue {
        return JSC__JSModuleLoader__evaluate(
            globalObject,
            sourceCodePtr,
            sourceCodeLen,
            originUrlPtr,
            originUrlLen,
            referrerUrlPtr,
            referrerUrlLen,
            thisValue,
            exception,
        );
    }

    static fn loadAndEvaluateModule(globalObject: *JSGlobalObject, module_name: (*const bun.String)?): (*JSInternalPromise)? {
        return JSC__JSModuleLoader__loadAndEvaluateModule(globalObject, module_name);
    }

    static fn @"import"(globalObject: *JSGlobalObject, module_name: *const bun.String): bun.JSError!*JSInternalPromise {
        return JSModuleLoader__import(globalObject, module_name) ?? error.JSError;
    }
}

const JSInternalPromise = js_internal_promise_mod.JSInternalPromise;
const String = bun.String;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
