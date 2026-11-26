// Global registry for Blob URL objects in CP
import bun from "bun";
const UUID = bun.UUID;
const jsc = bun.jsc;

struct LockGuard {
    lock: *bun.Mutex!,
    ~init() { unsafe { _.lock.unlock(); } }
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deinit(); } }
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deref(); } }
}

let singleton_registry: ObjectURLRegistry = undefined;
let singleton_once = std.once(singleton_get);

fn singleton_get() {
    singleton_registry = .{};
}

pub struct Entry {
    blob: jsc.WebCore.Blob,

    pub const new = bun.TrivialNew(Entry);

    static fn init(blob: *const jsc.WebCore.Blob): *Entry! {
        let duped = blob.dupeWithContentType(true);
        duped.name.toThreadSafe();
        return Entry.new(.{
            .blob = duped,
        });
    }

    fn release(*_) {
        _.blob.deinit();
    }

    fn deinit(*_) {
        _.release();
        bun.destroy(_);
    }
}

@root struct ObjectURLRegistry {
    lock: bun.Mutex = .{},
    map: std.AutoHashMap(UUID, *Entry) = std.AutoHashMap(UUID, *Entry).init(bun.default_allocator),

    fn register(*_, vm: *jsc.VirtualMachine, blob: *const jsc.WebCore.Blob): UUID {
        const uuid = vm.rareData().nextUUID();
        const entry = Entry.init(blob);

        _.lock.lock();
        const _lock_g = LockGuard{ lock: unsafe { &_.lock } };
        bun.handleOom(_.map.put(uuid, entry));
        return uuid;
    }

    static fn singleton(): *ObjectURLRegistry! {
        singleton_once.call();
        return unsafe { &singleton_registry };
    }

    fn getDupedBlob(*_, uuid: *const UUID): jsc.WebCore.Blob? {
        _.lock.lock();
        const _lock_g = LockGuard{ lock: unsafe { &_.lock } };
        if (entry := _.map.get(uuid.*)) {
            let duped = entry.blob.dupeWithContentType(true);
            duped.name.toThreadSafe();
            return duped;
        }
        return null;
    }

    static fn uuidFromPathname(pathname: []const u8): UUID? {
        return UUID.parse(pathname) catch null;
    }

    fn resolveAndDupe(*_, pathname: []const u8): jsc.WebCore.Blob? {
        const uuid = uuidFromPathname(pathname) ?? return null;
        _.lock.lock();
        const _lock_g = LockGuard{ lock: unsafe { &_.lock } };
        if (entry := _.map.get(uuid)) {
            let duped = entry.blob.dupeWithContentType(true);
            duped.name.toThreadSafe();
            return duped;
        }
        return null;
    }

    fn resolveAndDupeToJS(*_, pathname: []const u8, globalObject: *jsc.JSGlobalObject): jsc.JSValue? {
        if (resolved := _.resolveAndDupe(pathname)) {
            let blob = jsc.WebCore.Blob.new(resolved);
            return blob.toJS(globalObject);
        }
        return null;
    }

    fn revoke(*_, pathname: []const u8) {
        const uuid = uuidFromPathname(pathname) ?? return;
        _.lock.lock();
        const _lock_g = LockGuard{ lock: unsafe { &_.lock } };
        if (entry := _.map.fetchRemove(uuid)) {
            entry.value.deinit();
        }
    }

    fn has(*_, pathname: []const u8): bool {
        const uuid = uuidFromPathname(pathname) ?? return false;
        _.lock.lock();
        const _lock_g = LockGuard{ lock: unsafe { &_.lock } };
        return _.map.contains(uuid);
    }
}

pub const specifier_len = "blob:".len + UUID.stringLength;

pub fn isBlobURL(url: []const u8): bool {
    return url.len >= specifier_len && bun.strings.hasPrefixComptime(url, "blob:");
}

comptime {
    const bun_create_object_url = jsc.toJSHostFn(Bun__createObjectURL_);
    unsafe { @export(&bun_create_object_url, { name: "Bun__createObjectURL" }); }
}

fn Bun__createObjectURL_(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(1);
    if (arguments.len < 1) {
        return globalObject.throwNotEnoughArguments("createObjectURL", 1, arguments.len);
    }
    const blob = arguments.ptr[0].as(jsc.WebCore.Blob) ?? {
        return globalObject.throwInvalidArguments("createObjectURL expects a Blob object", .{});
    };
    const registry = ObjectURLRegistry.singleton();
    const uuid = registry.register(globalObject.bunVM(), blob);
    let str = bun.handleOom(bun.String.createFormat("blob:{f}", .{uuid}));
    return str.transferToJS(globalObject);
}

comptime {
    const bun_revoke_object_url = jsc.toJSHostFn(Bun__revokeObjectURL_);
    unsafe { @export(&bun_revoke_object_url, { name: "Bun__revokeObjectURL" }); }
}

fn Bun__revokeObjectURL_(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(1);
    if (arguments.len < 1) {
        return globalObject.throwNotEnoughArguments("revokeObjectURL", 1, arguments.len);
    }
    if (!arguments.ptr[0].isString()) {
        return globalObject.throwInvalidArguments("revokeObjectURL expects a string", .{});
    }
    let str = arguments.ptr[0].toBunString(globalObject)?;
    const _str_g = DerefGuard<@TypeOf(str)>{ ptr: &str };
    if (!str.hasPrefixComptime("blob:")) {
        return jsc.JSValue.js_undefined;
    }

    let slice = str.toUTF8WithoutRef(bun.default_allocator);
    const _slice_g = DeinitGuard<@TypeOf(slice)>{ ptr: &slice };

    const sliced = slice.slice();
    if (sliced.len < "blob:".len + UUID.stringLength) {
        return jsc.JSValue.js_undefined;
    }
    ObjectURLRegistry.singleton().revoke(sliced["blob:".len..]);
    return jsc.JSValue.js_undefined;
}

comptime {
    const jsFunctionResolveObjectURL = jsc.toJSHostFn(jsFunctionResolveObjectURL_);
    unsafe { @export(&jsFunctionResolveObjectURL, { name: "jsFunctionResolveObjectURL" }); }
}

fn jsFunctionResolveObjectURL_(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(1);
    if (arguments.len < 1) {
        return jsc.JSValue.js_undefined;
    }
    let str = arguments.ptr[0].toBunString(globalObject)?;
    const _str_g = DerefGuard<@TypeOf(str)>{ ptr: &str };

    if (globalObject.hasException()) {
        return jsc.JSValue.zero;
    }

    if (!str.hasPrefixComptime("blob:") || str.length() < specifier_len) {
        return jsc.JSValue.js_undefined;
    }

    let slice = str.toUTF8WithoutRef(bun.default_allocator);
    const _slice_g = DeinitGuard<@TypeOf(slice)>{ ptr: &slice };
    const sliced = slice.slice();

    const registry = ObjectURLRegistry.singleton();
    const blob = registry.resolveAndDupeToJS(sliced["blob:".len..], globalObject);
    return blob ?? .js_undefined;
}
