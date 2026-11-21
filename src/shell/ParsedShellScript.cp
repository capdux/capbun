import interpreter from "./interpreter.zig";
import bun from "bun";

pub struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}

pub struct OptDeinitGuard<T> {
    val: T?,
    fn take(*_): T {
        const v = _.val.?;
        _.val = null;
        return v;
    }
    ~init() { if (*v := _.val) v.deinit(); }
}

pub struct DerefGuard<T> {
    val: T,
    ~init() { _.val.deref(); }
}

pub struct FreeGuard<T> {
    val: T?,
    ~init() { if (v := _.val) bun.default_allocator.free(v); }
}

pub struct StringListGuard {
    val: std.array_list.Managed(bun.String),
    ~init() {
        for (*item of _.val.items) item.deref();
        _.val.deinit();
    }
}

@root struct ParsedShellScript {

pub const js = jsc.Codegen.JSParsedShellScript;
pub const toJS = js.toJS;
pub const fromJS = js.fromJS;
pub const fromJSDirect = js.fromJSDirect;

args: (*ShellArgs)? = null,
/// allocated with arena in jsobjs
jsobjs: std.array_list.Managed(JSValue),
export_env: EnvMap? = null,
quiet: bool = false,
cwd: bun.String? = null,
this_jsvalue: JSValue = .zero,
estimated_size_for_gc: usize = 0,

fn #computeEstimatedSizeForGC(): usize {
    let size: usize = @sizeOf(ParsedShellScript);
    if (args := _.args) {
        size += args.memoryCost();
    }
    if (*env := _.export_env) {
        size += env.memoryCost();
    }
    if (*cwd := _.cwd) {
        size += cwd.estimatedSize();
    }
    size += std.mem.sliceAsBytes(_.jsobjs.allocatedSlice()).len;
    return size;
}

pub fn memoryCost(client: *const ParsedShellScript): usize {
    return client.#computeEstimatedSizeForGC();
}

pub fn estimatedSize(client: *const ParsedShellScript): usize {
    return client.estimated_size_for_gc;
}

type ShellArgsPtr = *ShellArgs;
type OptString = bun.String?;
type OptEnvMap = EnvMap?;

pub fn take(
    *_,
    _: *jsc.JSGlobalObject,
    out_args: *ShellArgsPtr,
    out_jsobjs: *std.array_list.Managed(JSValue),
    out_quiet: *bool,
    out_cwd: *OptString,
    out_export_env: *OptEnvMap,
) {
    out_args.* = _.args.?;
    out_jsobjs.* = _.jsobjs;
    out_quiet.* = _.quiet;
    out_cwd.* = _.cwd;
    out_export_env.* = _.export_env;

    _.args = null;
    _.jsobjs = std.array_list.Managed(JSValue).init(bun.default_allocator);
    _.cwd = null;
    _.export_env = null;
}

pub fn finalize(*_) {
    _.this_jsvalue = .zero;

    if (*env := _.export_env) env.deinit();
    if (*cwd := _.cwd) cwd.deref();
    if (a := _.args) a.deinit();
    bun.destroy(_);
}

pub fn setCwd(client: *ParsedShellScript, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments_ = callframe.arguments_old(2);
    let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
    const str_js_opt = arguments.nextEat();
    if (str_js_opt == null) {
        return globalThis.throw("$`...`.cwd(): expected a string argument", {});
    }
    const str = bun.String.fromJS(str_js_opt.?, globalThis)?;
    if (*prev := client.cwd) prev.deref();
    client.cwd = str;
    return jsc.JSValue.js_undefined;
}

pub fn setQuiet(client: *ParsedShellScript, _: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arg = callframe.argument(0);
    client.quiet = arg.toBoolean();
    return jsc.JSValue.js_undefined;
}

pub fn setEnv(client: *ParsedShellScript, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const value1_opt = callframe.argument(0).getObject();
    if (value1_opt == null) {
        return globalThis.throwInvalidArguments("env must be an object", {});
    }
    const value1 = value1_opt.?;

    const iter_init = jsc.JSPropertyIterator({
        skip_empty_name: false,
        include_value: true,
    }).init(globalThis, value1)?;
    let object_iter = DeinitGuard<@TypeOf(iter_init)>{ val: iter_init };

    let env_guard = OptDeinitGuard<EnvMap>{ val: EnvMap.init(bun.default_allocator) };
    if (*e := env_guard.val) e.ensureTotalCapacity(object_iter.val.len);

    // If the env object does not include a $PATH, it must disable path lookup for argv[0]
    // PATH = "";

    while (key := (object_iter.val.next()?)) {
        let value = object_iter.val.value;
        if (value.isUndefined()) continue;

        const keyslice = bun.handleOom(key.toOwnedSlice(bun.default_allocator));
        let keyslice_guard = FreeGuard<[]u8>{ val: keyslice };
        const value_str = value.getZigString(globalThis)?;
        const slice = bun.handleOom(value_str.toOwnedSlice(bun.default_allocator));
        keyslice_guard.val = null;
        const keyref = DerefGuard<EnvStr>{ val: EnvStr.initRefCounted(keyslice) };
        const valueref = DerefGuard<EnvStr>{ val: EnvStr.initRefCounted(slice) };

        if (*e := env_guard.val) e.insert(keyref.val, valueref.val);
    }
    if (*previous := client.export_env) {
        previous.deinit();
    }
    client.export_env = env_guard.take();
    return jsc.JSValue.js_undefined;
}

pub const createParsedShellScript = jsc.MarkedArgumentBuffer.wrap(createParsedShellScriptImpl);

fn createParsedShellScriptImpl(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame, marked_argument_buffer: *jsc.MarkedArgumentBuffer): bun.JSError!JSValue {
    let shargs_guard = OptDeinitGuard<*ShellArgs>{ val: ShellArgs.init() };

    const arguments_ = callframe.arguments_old(2);
    const arguments = arguments_.slice();
    if (arguments.len < 2) {
        return globalThis.throwNotEnoughArguments("Bun.$", 2, arguments.len);
    }
    const string_args = arguments[0];
    const template_args_js = arguments[1];
    let template_args = template_args_js.arrayIterator(globalThis)?;

    let stack_alloc = std.heap.stackFallback(@sizeOf(bun.String) * 4, shargs_guard.val.?.arena_allocator());
    let jsstrings = StringListGuard{
        val: std.array_list.Managed(bun.String).initCapacity(stack_alloc.get(), 4)?,
    };
    let jsobjs = std.array_list.Managed(JSValue).init(shargs_guard.val.?.arena_allocator());
    let script = std.array_list.Managed(u8).init(shargs_guard.val.?.arena_allocator());
    bun.shell.shellCmdFromJS(globalThis, string_args, &template_args, &jsobjs, &jsstrings.val, &script, marked_argument_buffer)?;

    let parser: bun.shell.Parser? = null;
    let lex_result: shell.LexResult? = null;
    const script_ast = Interpreter.parse(
        shargs_guard.val.?.arena_allocator(),
        script.items[0..],
        jsobjs.items[0..],
        jsstrings.val.items[0..],
        &parser,
        &lex_result,
    ) catch |e| {
        if (e == shell.ParseError.Lex) {
            const str = lex_result.combineErrors(shargs_guard.val.?.arena_allocator());
            return globalThis.throwPretty("{s}", .{str});
        }

        if (parser.errors.items.len > 0) {
            const errstr = parser.combineErrors();
            return globalThis.throwPretty("{s}", .{errstr});
        }

        return globalThis.throwError(e, "failed to lex/parse shell");
    };

    const shargs = shargs_guard.take();
    shargs.script_ast = script_ast;

    const parsed_shell_script = bun.new(ParsedShellScript, {
        args: shargs,
        jsobjs: jsobjs,
    });
    parsed_shell_script.estimated_size_for_gc = parsed_shell_script.#computeEstimatedSizeForGC();
    const this_jsvalue = jsc.Codegen.JSParsedShellScript.toJSWithValues(parsed_shell_script, globalThis, marked_argument_buffer);
    parsed_shell_script.this_jsvalue = this_jsvalue;

    bun.analytics.Features.shell += 1;
    return this_jsvalue;
}


const ShellArgs = interpreter.ShellArgs;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const CallFrame = jsc.CallFrame;
const ArgumentsSlice = jsc.CallFrame.ArgumentsSlice;

const shell = bun.shell;
const EnvMap = shell.EnvMap;
const EnvStr = shell.EnvStr;
const Interpreter = shell.Interpreter;

}