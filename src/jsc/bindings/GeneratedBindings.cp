import bun from "bun";
import import_DevServer from "../../bake/DevServer.zig";
import import_bake from "../../bake/bake.zig";
import import_NodeModuleModule from "../../jsc/NodeModuleModule.zig";
import import_bindgen_test from "../../jsc/bindgen_test.zig";
import import_fmt_jsc from "../../jsc/fmt_jsc.zig";
import import_BunObject from "../../runtime/api/BunObject.zig";
import import_node_os from "../../runtime/node/node_os.zig";

const jsc = bun.jsc;
const JSHostFunctionType = jsc.JSHostFn;

struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}

pub enum fmt_jsc_Formatter: u8 {
    escape_powershell,
    highlight_javascript,
}

@c pub struct BunObject_BracesOptions {
    parse: bool,
    tokenize: bool,
}

@c pub struct node_os_UserInfoOptions {
    encoding: bun.String,
}

@c struct BindgenTestRequiredAndOptionalArgArguments {
    b_set: bool,
    d_set: bool,
    d_value: u8,
    b_value: usize,
}

/// Generated for "src/bake/DevServer.zig"
pub struct DevServer {
    pub const jsGetDeinitCountForTesting = @extern(*const JSHostFunctionType, .{ .name = "bindgen_DevServer_jsGetDeinitCountForTesting" });

    static fn createGetDeinitCountForTestingCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("getDeinitCountForTesting"), 0, jsGetDeinitCountForTesting, false, null);
    }
}

/// Generated for "src/bake/bake.zig"
pub struct bake {
}

/// Generated for "src/jsc/NodeModuleModule.zig"
pub struct NodeModuleModule {
    pub const js_stat = @extern(*const JSHostFunctionType, .{ .name = "bindgen_NodeModuleModule_js_stat" });

    static fn create_statCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("_stat"), 1, js_stat, false, null);
    }
}

/// Generated for "src/jsc/bindgen_test.zig"
pub struct bindgen_test {
    pub const jsAdd = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Bindgen_test_jsAdd" });
    pub const jsRequiredAndOptionalArg = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Bindgen_test_jsRequiredAndOptionalArg" });

    static fn createAddCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("add"), 3, jsAdd, false, null);
    }
    static fn createRequiredAndOptionalArgCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("requiredAndOptionalArg"), 4, jsRequiredAndOptionalArg, false, null);
    }
}

/// Generated for "src/jsc/fmt_jsc.zig"
pub struct fmt_jsc {
    pub const jsFmtString = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Fmt_jsc_jsFmtString" });
    pub const Formatter = fmt_jsc_Formatter;

    static fn createFmtStringCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("fmtString"), 3, jsFmtString, false, null);
    }
}

/// Generated for "src/runtime/api/BunObject.zig"
pub struct BunObject {
    pub const jsBraces = @extern(*const JSHostFunctionType, .{ .name = "bindgen_BunObject_jsBraces" });
    pub const jsGc = @extern(*const JSHostFunctionType, .{ .name = "bindgen_BunObject_jsGc" });
    pub const BracesOptions = BunObject_BracesOptions;

    static fn createBracesCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("braces"), 3, jsBraces, false, null);
    }
    static fn createGcCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("gc"), 2, jsGc, false, null);
    }
}

/// Generated for "src/runtime/node/node_os.zig"
pub struct node_os {
    pub const jsCpus = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsCpus" });
    pub const jsFreemem = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsFreemem" });
    pub const jsGetPriority = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsGetPriority" });
    pub const jsHomedir = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsHomedir" });
    pub const jsHostname = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsHostname" });
    pub const jsLoadavg = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsLoadavg" });
    pub const jsNetworkInterfaces = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsNetworkInterfaces" });
    pub const jsRelease = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsRelease" });
    pub const jsTotalmem = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsTotalmem" });
    pub const jsUptime = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsUptime" });
    pub const jsUserInfo = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsUserInfo" });
    pub const jsVersion = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsVersion" });
    pub const jsSetPriority = @extern(*const JSHostFunctionType, .{ .name = "bindgen_Node_os_jsSetPriority" });
    pub const UserInfoOptions = node_os_UserInfoOptions;

    static fn createCpusCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("cpus"), 1, jsCpus, false, null);
    }
    static fn createFreememCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("freemem"), 0, jsFreemem, false, null);
    }
    static fn createGetPriorityCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("getPriority"), 2, jsGetPriority, false, null);
    }
    static fn createHomedirCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("homedir"), 1, jsHomedir, false, null);
    }
    static fn createHostnameCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("hostname"), 1, jsHostname, false, null);
    }
    static fn createLoadavgCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("loadavg"), 1, jsLoadavg, false, null);
    }
    static fn createNetworkInterfacesCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("networkInterfaces"), 1, jsNetworkInterfaces, false, null);
    }
    static fn createReleaseCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("release"), 0, jsRelease, false, null);
    }
    static fn createTotalmemCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("totalmem"), 0, jsTotalmem, false, null);
    }
    static fn createUptimeCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("uptime"), 1, jsUptime, false, null);
    }
    static fn createUserInfoCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("userInfo"), 2, jsUserInfo, false, null);
    }
    static fn createVersionCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("version"), 0, jsVersion, false, null);
    }
    static fn createSetPriorityCallback(global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue {
        return jsc.host_fn.NewRuntimeFunction(global, jsc.ZigString.static("setPriority"), 2, jsSetPriority, false, null);
    }
}

struct binding_internals {
    @export @c static fn bindgen_DevServer_dispatchGetDeinitCountForTesting1(global: *jsc.JSGlobalObject, out: *usize): bool {
        if (!@hasDecl(import_DevServer, "getDeinitCountForTesting"))
            @compileError("Missing binding declaration \"getDeinitCountForTesting\" in \"DevServer.zig\"");
        out.* = @as(bun.JSError!usize, import_DevServer.getDeinitCountForTesting(
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_NodeModuleModule_dispatch_stat1(global: *jsc.JSGlobalObject, arg_str: *const bun.String, out: *i32): bool {
        if (!@hasDecl(import_NodeModuleModule, "_stat"))
            @compileError("Missing binding declaration \"_stat\" in \"NodeModuleModule.zig\"");
        let arg_str_utf8 = DeinitGuard<bun.ZigString.Slice>{ val: arg_str.toUTF8(bun.default_allocator) };
        out.* = @as(bun.JSError!i32, import_NodeModuleModule._stat(
            arg_str_utf8.val.slice(),
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Bindgen_test_dispatchAdd1(arg_global: *jsc.JSGlobalObject, arg_a: *const i32, arg_b: *const i32, out: *i32): bool {
        if (!@hasDecl(import_bindgen_test, "add"))
            @compileError("Missing binding declaration \"add\" in \"bindgen_test.zig\"");
        out.* = @as(bun.JSError!i32, import_bindgen_test.add(
            arg_global,
            arg_a.*,
            arg_b.*,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = arg_global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Bindgen_test_dispatchRequiredAndOptionalArg1(global: *jsc.JSGlobalObject, arg_a: *const bool, arg_c: *const i32, buf: *BindgenTestRequiredAndOptionalArgArguments, out: *i32): bool {
        if (!@hasDecl(import_bindgen_test, "requiredAndOptionalArg"))
            @compileError("Missing binding declaration \"requiredAndOptionalArg\" in \"bindgen_test.zig\"");
        out.* = @as(bun.JSError!i32, import_bindgen_test.requiredAndOptionalArg(
            arg_a.*,
            if (buf.b_set) buf.b_value else null,
            arg_c.*,
            if (buf.d_set) buf.d_value else null,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Fmt_jsc_dispatchFmtString1(arg_global: *jsc.JSGlobalObject, arg_code: *const bun.String, arg_formatter: *const fmt_jsc.Formatter, out: *bun.String): bool {
        if (!@hasDecl(import_fmt_jsc.js_bindings, "fmtString"))
            @compileError("Missing binding declaration \"js_bindings.fmtString\" in \"fmt_jsc.zig\"");
        let arg_code_utf8 = DeinitGuard<bun.ZigString.Slice>{ val: arg_code.toUTF8(bun.default_allocator) };
        out.* = @as(bun.JSError!bun.String, import_fmt_jsc.js_bindings.fmtString(
            arg_global,
            arg_code_utf8.val.slice(),
            arg_formatter.*,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = arg_global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_BunObject_dispatchBraces1(arg_global: *jsc.JSGlobalObject, arg_input: *const bun.String, arg_options: *const BunObject.BracesOptions): jsc.JSValue {
        if (!@hasDecl(import_BunObject, "braces"))
            @compileError("Missing binding declaration \"braces\" in \"BunObject.zig\"");
        return jsc.toJSHostCall(arg_global, @src(), import_BunObject.braces, .{
            arg_global,
            arg_input.*,
            arg_options.*,
        });
    }
    @export @c static fn bindgen_BunObject_dispatchGc1(global: *jsc.JSGlobalObject, arg_force: *const bool, out: *usize): bool {
        if (!@hasDecl(import_BunObject, "gc"))
            @compileError("Missing binding declaration \"gc\" in \"BunObject.zig\"");
        out.* = @as(bun.JSError!usize, import_BunObject.gc(
            global.bunVM(),
            arg_force.*,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchCpus1(arg_global: *jsc.JSGlobalObject): jsc.JSValue {
        if (!@hasDecl(import_node_os, "cpus"))
            @compileError("Missing binding declaration \"cpus\" in \"node_os.zig\"");
        return jsc.toJSHostCall(arg_global, @src(), import_node_os.cpus, .{
            arg_global,
        });
    }
    @export @c static fn bindgen_Node_os_dispatchFreemem1(global: *jsc.JSGlobalObject, out: *u64): bool {
        if (!@hasDecl(import_node_os, "freemem"))
            @compileError("Missing binding declaration \"freemem\" in \"node_os.zig\"");
        out.* = @as(bun.JSError!u64, import_node_os.freemem(
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchGetPriority1(arg_global: *jsc.JSGlobalObject, arg_pid: *const i32, out: *i32): bool {
        if (!@hasDecl(import_node_os, "getPriority"))
            @compileError("Missing binding declaration \"getPriority\" in \"node_os.zig\"");
        out.* = @as(bun.JSError!i32, import_node_os.getPriority(
            arg_global,
            arg_pid.*,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = arg_global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchHomedir1(arg_global: *jsc.JSGlobalObject, out: *bun.String): bool {
        if (!@hasDecl(import_node_os, "homedir"))
            @compileError("Missing binding declaration \"homedir\" in \"node_os.zig\"");
        out.* = @as(bun.JSError!bun.String, import_node_os.homedir(
            arg_global,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = arg_global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchHostname1(arg_global: *jsc.JSGlobalObject): jsc.JSValue {
        if (!@hasDecl(import_node_os, "hostname"))
            @compileError("Missing binding declaration \"hostname\" in \"node_os.zig\"");
        return jsc.toJSHostCall(arg_global, @src(), import_node_os.hostname, .{
            arg_global,
        });
    }
    @export @c static fn bindgen_Node_os_dispatchLoadavg1(arg_global: *jsc.JSGlobalObject): jsc.JSValue {
        if (!@hasDecl(import_node_os, "loadavg"))
            @compileError("Missing binding declaration \"loadavg\" in \"node_os.zig\"");
        return jsc.toJSHostCall(arg_global, @src(), import_node_os.loadavg, .{
            arg_global,
        });
    }
    @export @c static fn bindgen_Node_os_dispatchNetworkInterfaces1(arg_global: *jsc.JSGlobalObject): jsc.JSValue {
        if (!@hasDecl(import_node_os, "networkInterfaces"))
            @compileError("Missing binding declaration \"networkInterfaces\" in \"node_os.zig\"");
        return jsc.toJSHostCall(arg_global, @src(), import_node_os.networkInterfaces, .{
            arg_global,
        });
    }
    @export @c static fn bindgen_Node_os_dispatchRelease1(global: *jsc.JSGlobalObject, out: *bun.String): bool {
        if (!@hasDecl(import_node_os, "release"))
            @compileError("Missing binding declaration \"release\" in \"node_os.zig\"");
        out.* = @as(bun.JSError!bun.String, import_node_os.release(
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchTotalmem1(global: *jsc.JSGlobalObject, out: *u64): bool {
        if (!@hasDecl(import_node_os, "totalmem"))
            @compileError("Missing binding declaration \"totalmem\" in \"node_os.zig\"");
        out.* = @as(bun.JSError!u64, import_node_os.totalmem(
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchUptime1(arg_global: *jsc.JSGlobalObject, out: *f64): bool {
        if (!@hasDecl(import_node_os, "uptime"))
            @compileError("Missing binding declaration \"uptime\" in \"node_os.zig\"");
        out.* = @as(bun.JSError!f64, import_node_os.uptime(
            arg_global,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = arg_global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchUserInfo1(arg_global: *jsc.JSGlobalObject, arg_options: *const node_os.UserInfoOptions): jsc.JSValue {
        if (!@hasDecl(import_node_os, "userInfo"))
            @compileError("Missing binding declaration \"userInfo\" in \"node_os.zig\"");
        return jsc.toJSHostCall(arg_global, @src(), import_node_os.userInfo, .{
            arg_global,
            arg_options.*,
        });
    }
    @export @c static fn bindgen_Node_os_dispatchVersion1(global: *jsc.JSGlobalObject, out: *bun.String): bool {
        if (!@hasDecl(import_node_os, "version"))
            @compileError("Missing binding declaration \"version\" in \"node_os.zig\"");
        out.* = @as(bun.JSError!bun.String, import_node_os.version(
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchSetPriority1(arg_global: *jsc.JSGlobalObject, arg_pid: *const i32, arg_priority: *const i32): bool {
        if (!@hasDecl(import_node_os, "setPriority1"))
            @compileError("Missing binding declaration \"setPriority1\" in \"node_os.zig\"");
        @as(bun.JSError!void, import_node_os.setPriority1(
            arg_global,
            arg_pid.*,
            arg_priority.*,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = arg_global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
    @export @c static fn bindgen_Node_os_dispatchSetPriority2(arg_global: *jsc.JSGlobalObject, arg_priority: *const i32): bool {
        if (!@hasDecl(import_node_os, "setPriority2"))
            @compileError("Missing binding declaration \"setPriority2\" in \"node_os.zig\"");
        @as(bun.JSError!void, import_node_os.setPriority2(
            arg_global,
            arg_priority.*,
        )) catch e => {
            if (e == error.OutOfMemory) {
                * = arg_global.throwOutOfMemoryValue();
            }
            return false;
        };
        return true;
    }
}

comptime {
    if (bun.Environment.export_cpp_apis) {
        * = import_bake;
        const decls = @typeInfo(binding_internals).@"struct".decls;
        let i: usize = 0;
        while (i < decls.len) : (i += 1) {
            * = &@field(binding_internals, decls[i].name);
        }
    }
}
