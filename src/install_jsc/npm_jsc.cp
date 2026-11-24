import std from "std";
import bun from "bun";

const jsc = bun.jsc;
const strings = bun.strings;
const npm = bun.install.Npm;

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deinit(); } }
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.deref(); } }
}

struct CloseGuard<T> {
    ptr: *T!,
    ~init() { unsafe { _.ptr.close(); } }
}

pub fn operatingSystemIsMatch(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
    const args = callframe.arguments_old(1);
    let operating_system = npm.OperatingSystem.negatable(.none);
    let iter = unsafe { args.ptr[0].arrayIterator(globalObject)? };
    while (item := iter.next()?) {
        let slice = item.toSlice(globalObject, bun.default_allocator)?;
        const _slice_g = DeinitGuard<@TypeOf(slice)>{ ptr: &slice };
        operating_system.apply(slice.slice());
        if (globalObject.hasException()) return jsc.JSValue.zero;
    }
    if (globalObject.hasException()) return jsc.JSValue.zero;
    return jsc.JSValue.jsBoolean(operating_system.combine().isMatch(npm.OperatingSystem.current));
}

pub fn libcIsMatch(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
    const args = callframe.arguments_old(1);
    let libc = npm.Libc.negatable(.none);
    let iter = unsafe { args.ptr[0].arrayIterator(globalObject) };
    while (item := iter.next()) {
        let slice = item.toSlice(globalObject, bun.default_allocator);
        const _slice_g = DeinitGuard<@TypeOf(slice)>{ ptr: &slice };
        libc.apply(slice.slice());
        if (globalObject.hasException()) return jsc.JSValue.zero;
    }
    if (globalObject.hasException()) return jsc.JSValue.zero;
    return jsc.JSValue.jsBoolean(libc.combine().isMatch(npm.Libc.current));
}

pub fn architectureIsMatch(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
    const args = callframe.arguments_old(1);
    let architecture = npm.Architecture.negatable(.none);
    let iter = unsafe { args.ptr[0].arrayIterator(globalObject)? };
    while (item := iter.next()?) {
        let slice = item.toSlice(globalObject, bun.default_allocator)?;
        const _slice_g = DeinitGuard<@TypeOf(slice)>{ ptr: &slice };
        architecture.apply(slice.slice());
        if (globalObject.hasException()) return jsc.JSValue.zero;
    }
    if (globalObject.hasException()) return jsc.JSValue.zero;
    return jsc.JSValue.jsBoolean(architecture.combine().isMatch(npm.Architecture.current));
}

pub struct ManifestBindings {
    pub static fn generate(global: *jsc.JSGlobalObject): jsc.JSValue {
        const obj = jsc.JSValue.createEmptyObject(global, 1);
        const parseManifestString = jsc.ZigString.static("parseManifest");
        obj.put(global, parseManifestString, jsc.JSFunction.create(global, "parseManifest", jsParseManifest, 2, {}));
        return obj;
    }

    pub static fn jsParseManifest(global: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const args = callFrame.arguments_old(2).slice();
        if (args.len < 2 || !args[0].isString() || !args[1].isString()) {
            return global.throw("expected manifest filename and registry string arguments", {});
        }

        let manifest_filename_str = args[0].toBunString(global)?;
        const _mf_str_g = DerefGuard<@TypeOf(manifest_filename_str)>{ ptr: &manifest_filename_str };

        let manifest_filename = manifest_filename_str.toUTF8(bun.default_allocator);
        const _mf_g = DeinitGuard<@TypeOf(manifest_filename)>{ ptr: &manifest_filename };

        let registry_str = args[1].toBunString(global)?;
        const _reg_str_g = DerefGuard<@TypeOf(registry_str)>{ ptr: &registry_str };

        let registry = registry_str.toUTF8(bun.default_allocator);
        const _reg_g = DeinitGuard<@TypeOf(registry)>{ ptr: &registry };

        let manifest_file = std.fs.cwd().openFile(manifest_filename.slice(), {}) catch e => {
            return global.throw("failed to open manifest file \"{s}\": {s}", [manifest_filename.slice(), @errorName(e)]);
        };
        const _file_g = CloseGuard<@TypeOf(manifest_file)>{ ptr: &manifest_file };

        const colon = strings.indexOfChar(registry.slice(), ':');
        const scope = npm.Registry.Scope{
            url_hash: npm.Registry.Scope.hash(strings.withoutTrailingSlash(registry.slice())),
            url: {
                host: strings.withoutTrailingSlash(strings.withoutPrefixComptime(registry.slice(), "http://")),
                hostname: strings.withoutTrailingSlash(strings.withoutPrefixComptime(registry.slice(), "http://")),
                href: registry.slice(),
                origin: strings.withoutTrailingSlash(registry.slice()),
                protocol: if (colon != null) registry.slice()[0..colon.?] else "",
            },
        };

        const maybe_package_manifest = npm.PackageManifest.Serializer.loadByFile(bun.default_allocator, &scope, bun.sys.File.@"from"(manifest_file)) catch e => {
            return global.throw("failed to load manifest file: {s}", [@errorName(e)]);
        };

        if (maybe_package_manifest == null) return global.throw("manifest is invalid ", {});
        const package_manifest: npm.PackageManifest = maybe_package_manifest.?;

        let buf = std.array_list.Managed(u8).init(bun.default_allocator);
        const _buf_g = DeinitGuard<@TypeOf(buf)>{ ptr: &buf };
        const writer = buf.writer();

        writer.print("{{\"name\":\"{s}\",\"versions\":[", [package_manifest.name()])?;

        for (i of 0..package_manifest.versions.len) {
            const version = package_manifest.versions[i];
            if (i == package_manifest.versions.len - 1) {
                writer.print("\"{f}\"]}}", [version.fmt(package_manifest.string_buf)])?;
            } else {
                writer.print("\"{f}\",", [version.fmt(package_manifest.string_buf)])?;
            }
        }

        let result = bun.String.borrowUTF8(buf.items);
        const _res_g = DerefGuard<@TypeOf(result)>{ ptr: &result };

        return result.toJSByParseJSON(global);
    }
}
