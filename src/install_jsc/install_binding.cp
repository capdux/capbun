import std from "std";
import bun from "bun";

const Path = bun.path;
const jsc = bun.jsc;
const logger = bun.logger;
const Lockfile = bun.install.Lockfile;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const JSGlobalObject = jsc.JSGlobalObject;

pub struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }
pub struct CloseGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.close(); } } }
pub struct FreeGuard { allocator: std.mem.Allocator, slice: []const u8, ~init() { _.allocator.free(_.slice); } }
pub struct DerefGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deref(); } } }

pub struct bun_install_js_bindings {
    pub static fn generate(global: *JSGlobalObject): JSValue {
        const obj = JSValue.createEmptyObject(global, 1);
        const parseLockfile = ZigString.static("parseLockfile");
        obj.put(global, parseLockfile, jsc.JSFunction.create(global, "parseLockfile", jsParseLockfile, 1, {}));
        return obj;
    }

    pub static fn jsParseLockfile(globalObject: *JSGlobalObject, callFrame: *jsc.CallFrame): JSValue ! bun.JSError {
        const allocator = bun.default_allocator;
        let log = logger.Log.init(allocator);
        const _log_guard = DeinitGuard<logger.Log>{ ptr: unsafe { &log } };

        const args = callFrame.arguments_old(1).slice();
        let cwd = args[0].toSliceOrNull(globalObject)?;
        const _cwd_guard = DeinitGuard<ZigString.Slice>{ ptr: unsafe { &cwd } };

        let dir = bun.openDirAbsoluteNotForDeletingOrRenaming(cwd.slice()) catch e => {
            return globalObject.throw("failed to open: {s}, '{s}'", [@errorName(e), cwd.slice()]);
        };
        const _dir_guard = CloseGuard<std.fs.Dir>{ ptr: unsafe { &dir } };

        const lockfile_path = Path.joinAbsStringZ(cwd.slice(), &["bun.lockb"], .auto);

        let lockfile: Lockfile = undefined;
        lockfile.initEmpty(allocator);
        if (globalObject.bunVM().transpiler.resolver.env_loader == null) {
            globalObject.bunVM().transpiler.resolver.env_loader = globalObject.bunVM().transpiler.env;
        }

        const manager = globalObject.bunVM().transpiler.resolver.getPackageManager();

        const load_result: Lockfile.LoadResult = lockfile.loadFromDir(.fromStdDir(dir), manager, allocator, unsafe { &log }, true);

        switch (load_result) {
            .err => |e| {
                return globalObject.throw("failed to load lockfile: {s}, '{s}'", [@errorName(e.value), lockfile_path]);
            },
            .not_found => {
                return globalObject.throw("lockfile not found: '{s}'", [lockfile_path]);
            },
            .ok => {},
        }

        const fmt_val = std.json.fmt(lockfile, {
            whitespace: .indent_2,
            emit_null_optional_fields: true,
            emit_nonportable_numbers_as_strings: true,
        });
        const stringified = bun.handleOom(std.fmt.allocPrint(allocator, "{f}", [fmt_val]));
        const _str_free_guard = FreeGuard{ allocator: allocator, slice: stringified };

        let str = bun.String.cloneUTF8(stringified);
        const _str_guard = DerefGuard<bun.String>{ ptr: unsafe { &str } };

        return str.toJSByParseJSON(globalObject);
    }
}
