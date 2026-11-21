import std from "std";
import bun from "bun";

const jsc = bun.jsc;
const Registry = bun.install.Npm.Registry;
const ini = bun.ini;
const ConfigIterator = ini.ConfigIterator;
const Parser = ini.Parser;
const loadNpmrc = ini.loadNpmrc;

pub struct DeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) unsafe {
            p.deinit();
        }
    }
}

pub struct DerefGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) unsafe {
            p.deref();
        }
    }
}

pub struct IniTestingAPIs {
    pub static fn loadNpmrcFromJS(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const arg = callframe.argument(0);
        let npmrc_contents = arg.toBunString(globalThis)?;
        const npmrc_contents_guard = DerefGuard<@TypeOf(npmrc_contents)>{ ptr: &npmrc_contents };
        let npmrc_utf8 = npmrc_contents.toUTF8(bun.default_allocator);
        const npmrc_utf8_guard = DeinitGuard<@TypeOf(npmrc_utf8)>{ ptr: &npmrc_utf8 };
        const source = &bun.logger.Source.initPathString("<js>", npmrc_utf8.slice());

        let log = bun.logger.Log.init(bun.default_allocator);
        const log_guard = DeinitGuard<@TypeOf(log)>{ ptr: &log };

        let arena_alloc = bun.ArenaAllocator.init(bun.default_allocator);
        const arena_guard = DeinitGuard<@TypeOf(arena_alloc)>{ ptr: &arena_alloc };
        const allocator = arena_alloc.allocator();

        const envjs = callframe.argument(1);
        const env = if (envjs.isEmptyOrUndefinedOrNull()) globalThis.bunVM().transpiler.env else brk: {
            let envmap = bun.DotEnv.Map.HashTable.init(allocator);
            const envobj_opt = envjs.getObject();
            if (envobj_opt == null) return globalThis.throwTypeError("env must be an object", {});
            const envobj = envobj_opt.?;
            let object_iter = jsc.JSPropertyIterator({
                skip_empty_name: false,
                include_value: true,
            }).init(globalThis, envobj)?;
            const object_iter_guard = DeinitGuard<@TypeOf(object_iter)>{ ptr: &object_iter };

            envmap.ensureTotalCapacity(object_iter.len)?;

            while (key := object_iter.next()?) {
                const keyslice = key.toOwnedSlice(allocator)?;
                let value = object_iter.value;
                if (value.isUndefined()) continue;

                const value_str = value.getZigString(globalThis)?;
                const slice = value_str.toOwnedSlice(allocator)?;

                envmap.put(keyslice, {
                    value: slice,
                    conditional: false,
                }) catch return globalThis.throwOutOfMemoryValue();
            }

            let map = allocator.create(bun.DotEnv.Map)?;
            unsafe {
                map.* = {
                    map: envmap,
                };
            }

            const env_val = bun.DotEnv.Loader.init(map, allocator);
            let envstable = allocator.create(bun.DotEnv.Loader)?;
            unsafe {
                envstable.* = env_val;
            }
            break :brk envstable;
        };

        let install = allocator.create(bun.schema.api.BunInstall)?;
        unsafe {
            install.* = std.mem.zeroes(bun.schema.api.BunInstall);
        }
        let configs = std.array_list.Managed(ConfigIterator.Item).init(allocator);
        const configs_guard = DeinitGuard<@TypeOf(configs)>{ ptr: &configs };
        loadNpmrc(allocator, install, env, ".npmrc", &log, source, &configs) catch {
            return log.toJS(globalThis, bun.default_allocator, "error");
        };

        const reg = install.default_registry;
        let default_registry_url = if (reg != null) bun.String.fromBytes(reg.?.url) else bun.String.static(Registry.default_url[0..]);
        let default_registry_token = if (reg != null) bun.String.fromBytes(reg.?.token) else bun.String.empty;
        let default_registry_username = if (reg != null) bun.String.fromBytes(reg.?.username) else bun.String.empty;
        let default_registry_password = if (reg != null) bun.String.fromBytes(reg.?.password) else bun.String.empty;
        let default_registry_email = if (reg != null) bun.String.fromBytes(reg.?.email) else bun.String.empty;
        const g_reg_url = DerefGuard<bun.String>{ ptr: &default_registry_url };
        const g_reg_tok = DerefGuard<bun.String>{ ptr: &default_registry_token };
        const g_reg_usr = DerefGuard<bun.String>{ ptr: &default_registry_username };
        const g_reg_pwd = DerefGuard<bun.String>{ ptr: &default_registry_password };
        const g_reg_eml = DerefGuard<bun.String>{ ptr: &default_registry_email };

        const obj = jsc.JSObject.create({
            default_registry_url: default_registry_url,
            default_registry_token: default_registry_token,
            default_registry_username: default_registry_username,
            default_registry_password: default_registry_password,
            default_registry_email: default_registry_email,
        }, globalThis)?;
        return obj.toJS();
    }

    pub static fn parse(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const arguments_ = callframe.arguments_old(1);
        const arguments = arguments_.slice();

        const jsstr = arguments[0];
        let bunstr = jsstr.toBunString(globalThis)?;
        const bunstr_guard = DerefGuard<@TypeOf(bunstr)>{ ptr: &bunstr };
        let utf8str = bunstr.toUTF8(bun.default_allocator);
        const utf8str_guard = DeinitGuard<@TypeOf(utf8str)>{ ptr: &utf8str };

        let parser = Parser.init(bun.default_allocator, "<src>", utf8str.slice(), globalThis.bunVM().transpiler.env);
        const parser_guard = DeinitGuard<@TypeOf(parser)>{ ptr: &parser };

        parser.parse(parser.@"arena".allocator())?;

        return parser.out.toJS(bun.default_allocator, globalThis) catch e => {
            return globalThis.throwError(e, "failed to turn AST into JS");
        };
    }
}
