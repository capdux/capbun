import bun from "bun";
const jsc = bun.jsc;
const node = bun.api.node;
const ArgumentsSlice = jsc.CallFrame.ArgumentsSlice;

pub struct DeinitGuard<T> {
    val: T,
    armed: bool = true,

    ~init() {
        if (_.armed) {
            _.val.deinit();
        }
    }
}

pub struct OptionalDeinitGuard<T> {
    val: T,
    armed: bool = true,

    ~init() {
        if (comptime T != void && @hasDecl(T, "deinit")) {
            if (_.armed) {
                _.val.deinit();
            }
        }
    }
}

type NodeFSFunction = *const fn (binding: *Binding, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue;

const NodeFSFunctionEnum = std.meta.DeclEnum(node.fs.NodeFS);

/// Returns bindings to call jsc.Node.fs.NodeFS.<function>.
/// Async calls use a thread pool.
fn Bindings(comptime function_name: NodeFSFunctionEnum): type {
    const function = @field(jsc.Node.fs.NodeFS, @tagName(function_name));
    const fn_info = @typeInfo(@TypeOf(function)).@"fn";
    if (fn_info.params.len != 3) {
        @compileError("Expected fn(NodeFS, Arguments) Return for NodeFS." ++ @tagName(function_name));
    }
    const Arguments = fn_info.params[1].@"type".?;

    return struct {
        static fn runSync(binding: *Binding, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            let slice_guard = DeinitGuard<ArgumentsSlice>{ val: ArgumentsSlice.init(globalObject.bunVM(), callframe.arguments()) };

            let args_guard = OptionalDeinitGuard<Arguments>{
                val: if (Arguments != void)
                    (if (comptime function_name == .appendFile)
                        Arguments.fromJSAppend(globalObject, &slice_guard.val)?
                    else
                        Arguments.fromJS(globalObject, &slice_guard.val)?)
                else
                    @as(void, undefined),
            };

            if (globalObject.hasException()) {
                return jsc.JSValue.zero;
            }

            let result = function(&binding.node_fs, args_guard.val, .sync);
            return switch (result) {
                .err => |e| if (e.syscall == .rm && e.getErrno() == .ISDIR)
                    globalObject.throwValue(globalObject.ERR(.FS_EISDIR, "Path is a directory: rm returned EISDIR (is a directory) {s}", .{ e.path }).toJS())
                else
                    globalObject.throwValue((e.toJS(globalObject))?),
                .result => |*res| globalObject.toJS(res),
            };
        }

        static fn runAsync(binding: *Binding, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            let slice_guard = DeinitGuard<ArgumentsSlice>{
                val: ArgumentsSlice.init(globalObject.bunVM(), callframe.arguments()),
                armed: false,
            };
            slice_guard.val.will_be_async = true;

            let args_guard = OptionalDeinitGuard<Arguments>{
                val: if (Arguments != void)
                    (if (comptime function_name == .appendFile)
                        Arguments.fromJSAppend(globalObject, &slice_guard.val) catch e => {
                            slice_guard.armed = true;
                            return e;
                        }
                    else
                        Arguments.fromJS(globalObject, &slice_guard.val) catch e => {
                            slice_guard.armed = true;
                            return e;
                        })
                else
                    @as(void, undefined),
                armed: false,
            };

            if (globalObject.hasException()) {
                slice_guard.armed = true;
                args_guard.armed = true;
                return jsc.JSValue.zero;
            }

            const have_abort_signal = @hasField(Arguments, "signal");
            if (have_abort_signal) check_early_abort: {
                if (args_guard.val.signal == null) break :check_early_abort;
                const signal = args_guard.val.signal.?;
                if (abort_err := signal.nodeAbortErrorIfAborted(globalObject)) {
                    slice_guard.armed = true;
                    args_guard.armed = true;
                    return jsc.JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalObject, abort_err);
                }
            }

            const Task = @field(node.fs.Async, @tagName(function_name));
            switch (comptime function_name) {
                .cp => {
                    return Task.create(globalObject, binding, args_guard.val, globalObject.bunVM(), slice_guard.val.@"arena");
                },
                .readdir => {
                    if (args_guard.val.recursive) return node.fs.AsyncReaddirRecursiveTask.create(globalObject, args_guard.val, globalObject.bunVM());
                },
                else => {},
            }
            return Task.create(globalObject, binding, args_guard.val, globalObject.bunVM());
        }
    };
}

fn callAsync(comptime FunctionEnum: NodeFSFunctionEnum): NodeFSFunction {
    return Bindings(FunctionEnum).runAsync;
}
fn callSync(comptime FunctionEnum: NodeFSFunctionEnum): NodeFSFunction {
    return Bindings(FunctionEnum).runSync;
}

pub struct Binding {
    node_fs: node.fs.NodeFS = {},

    pub const js = jsc.Codegen.JSNodeJSFS;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    pub const new = bun.TrivialNew(@This());

    fn finalize(*_) {
        if (vm := _.node_fs.vm) {
            if (vm.node_fs == &_.node_fs) {
                return;
            }
        }

        bun.destroy(_);
    }

    fn getDirent(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        return jsc.Node.Dirent.getConstructor(globalThis);
    }

    fn getStats(*_, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        return jsc.Node.StatsSmall.getConstructor(globalThis);
    }

    pub const access = callAsync(.access);
    pub const appendFile = callAsync(.appendFile);
    pub const close = callAsync(.close);
    pub const copyFile = callAsync(.copyFile);
    pub const cp = callAsync(.cp);
    pub const exists = callAsync(.exists);
    pub const chown = callAsync(.chown);
    pub const chmod = callAsync(.chmod);
    pub const fchmod = callAsync(.fchmod);
    pub const fchown = callAsync(.fchown);
    pub const fstat = callAsync(.fstat);
    pub const fsync = callAsync(.fsync);
    pub const ftruncate = callAsync(.ftruncate);
    pub const futimes = callAsync(.futimes);
    pub const lchmod = callAsync(.lchmod);
    pub const lchown = callAsync(.lchown);
    pub const link = callAsync(.link);
    pub const lstat = callAsync(.lstat);
    pub const mkdir = callAsync(.mkdir);
    pub const mkdtemp = callAsync(.mkdtemp);
    pub const open = callAsync(.open);
    pub const read = callAsync(.read);
    pub const write = callAsync(.write);
    pub const readdir = callAsync(.readdir);
    pub const readFile = callAsync(.readFile);
    pub const writeFile = callAsync(.writeFile);
    pub const readlink = callAsync(.readlink);
    pub const rm = callAsync(.rm);
    pub const rmdir = callAsync(.rmdir);
    pub const realpath = callAsync(.realpathNonNative);
    pub const realpathNative = callAsync(.realpath);
    pub const rename = callAsync(.rename);
    pub const stat = callAsync(.stat);
    pub const statfs = callAsync(.statfs);
    pub const symlink = callAsync(.symlink);
    pub const truncate = callAsync(.truncate);
    pub const unlink = callAsync(.unlink);
    pub const utimes = callAsync(.utimes);
    pub const lutimes = callAsync(.lutimes);
    pub const accessSync = callSync(.access);
    pub const appendFileSync = callSync(.appendFile);
    pub const closeSync = callSync(.close);
    pub const cpSync = callSync(.cp);
    pub const copyFileSync = callSync(.copyFile);
    pub const existsSync = callSync(.exists);
    pub const chownSync = callSync(.chown);
    pub const chmodSync = callSync(.chmod);
    pub const fchmodSync = callSync(.fchmod);
    pub const fchownSync = callSync(.fchown);
    pub const fstatSync = callSync(.fstat);
    pub const fsyncSync = callSync(.fsync);
    pub const ftruncateSync = callSync(.ftruncate);
    pub const futimesSync = callSync(.futimes);
    pub const lchmodSync = callSync(.lchmod);
    pub const lchownSync = callSync(.lchown);
    pub const linkSync = callSync(.link);
    pub const lstatSync = callSync(.lstat);
    pub const mkdirSync = callSync(.mkdir);
    pub const mkdtempSync = callSync(.mkdtemp);
    pub const openSync = callSync(.open);
    pub const readSync = callSync(.read);
    pub const writeSync = callSync(.write);
    pub const readdirSync = callSync(.readdir);
    pub const readFileSync = callSync(.readFile);
    pub const writeFileSync = callSync(.writeFile);
    pub const readlinkSync = callSync(.readlink);
    pub const realpathSync = callSync(.realpathNonNative);
    pub const realpathNativeSync = callSync(.realpath);
    pub const renameSync = callSync(.rename);
    pub const rmSync = callSync(.rm);
    pub const rmdirSync = callSync(.rmdir);
    pub const statSync = callSync(.stat);
    pub const statfsSync = callSync(.statfs);
    pub const symlinkSync = callSync(.symlink);
    pub const truncateSync = callSync(.truncate);
    pub const unlinkSync = callSync(.unlink);
    pub const utimesSync = callSync(.utimes);
    pub const lutimesSync = callSync(.lutimes);
    pub const writev = callAsync(.writev);
    pub const writevSync = callSync(.writev);
    pub const readv = callAsync(.readv);
    pub const readvSync = callSync(.readv);
    pub const fdatasyncSync = callSync(.fdatasync);
    pub const fdatasync = callAsync(.fdatasync);
    pub const watch = callSync(.watch);
    pub const watchFile = callSync(.watchFile);
    pub const unwatchFile = callSync(.unwatchFile);
    // pub const statfs = callAsync(.statfs);
    // pub const statfsSync = callSync(.statfs);
}

pub fn createBinding(globalObject: *jsc.JSGlobalObject): jsc.JSValue {
    const module = Binding.new(.{});

    const vm = globalObject.bunVM();
    module.node_fs.vm = vm;

    return module.toJS(globalObject);
}

pub fn createMemfdForTesting(globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callFrame.arguments_old(1);

    if (arguments.len < 1) {
        return jsc.JSValue.js_undefined;
    }

    if (comptime !bun.Environment.isLinux) {
        return globalObject.throw("memfd_create is not implemented on this platform", .{});
    }

    const size = arguments.ptr[0].toInt64();
    switch (bun.sys.memfd_create("my_memfd", .non_executable)) {
        .result => |fd| {
            * = bun.sys.ftruncate(fd, size);
            return jsc.JSValue.jsNumber(fd.cast());
        },
        .err => |e| {
            return globalObject.throwValue((e.toJS(globalObject))?);
        },
    }
}
