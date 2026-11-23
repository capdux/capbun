import bun from "bun";
import hgi from "../install/hosted_git_info.zig";

const jsc = bun.jsc;

pub struct DerefGuard<T> {
    val: T,
    ~init() { _.val.deref(); }
}

pub struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}

pub fn hostedGitInfoToJS(info: &hgi.HostedGitInfo, go: *jsc.JSGlobalObject): jsc.JSValue ! bun.JSError {
    const obj = jsc.JSValue.createEmptyObject(go, 6);
    obj.put(
        go,
        jsc.ZigString.static("type"),
        bun.String.fromBytes(info.host_provider.typeStr()).toJS(go)?,
    );
    obj.put(
        go,
        jsc.ZigString.static("domain"),
        bun.String.fromBytes(info.host_provider.domain()).toJS(go)?,
    );
    obj.put(
        go,
        jsc.ZigString.static("project"),
        bun.String.fromBytes(info.project).toJS(go)?,
    );
    obj.put(
        go,
        jsc.ZigString.static("user"),
        if (info.user != null) bun.String.fromBytes(info.user.?).toJS(go)? else .@"null",
    );
    obj.put(
        go,
        jsc.ZigString.static("committish"),
        if (info.committish != null)
            bun.String.fromBytes(info.committish.?).toJS(go)?
        else
            .@"null",
    );
    obj.put(
        go,
        jsc.ZigString.static("default"),
        bun.String.fromBytes(@tagName(info.default_representation)).toJS(go)?,
    );

    return obj;
}

pub fn jsParseUrl(go: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
    const allocator = bun.default_allocator;

    if (callframe.argumentsCount() != 1) {
        return go.throw("hostedGitInfo.prototype.parseUrl takes exactly 1 argument", {});
    }

    const arg0 = callframe.argument(0);
    if (!arg0.isString()) {
        return go.throw("hostedGitInfo.prototype.parseUrl takes a string as its first argument", {});
    }

    const npa_str = DerefGuard<bun.String>{ val: arg0.toBunString(go)? };
    let as_utf8 = DeinitGuard<@TypeOf(npa_str.val.toUTF8(allocator))>{
        val: npa_str.val.toUTF8(allocator),
    };
    const parsed = hgi.parseUrl(allocator, as_utf8.val.mut()) catch e => {
        return go.throw("Invalid Git URL: {}", [e]);
    };
    let parsed_url = DeinitGuard<@TypeOf(parsed.url)>{ val: parsed.url };

    return parsed_url.val.href().toJS(go);
}

pub fn jsFromUrl(go: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
    const allocator = bun.default_allocator;

    if (callframe.argumentsCount() != 1) {
        return go.throw("hostedGitInfo.prototype.fromUrl takes exactly 1 argument", {});
    }

    const arg0 = callframe.argument(0);
    if (!arg0.isString()) {
        return go.throw("hostedGitInfo.prototype.fromUrl takes a string as its first argument", {});
    }

    const npa_str = DerefGuard<bun.String>{ val: arg0.toBunString(go)? };
    let as_utf8 = DeinitGuard<@TypeOf(npa_str.val.toUTF8(allocator))>{
        val: npa_str.val.toUTF8(allocator),
    };
    const maybe_parsed = hgi.HostedGitInfo.fromUrl(allocator, as_utf8.val.mut()) catch e => {
        return go.throw("Invalid Git URL: {}", [e]);
    };
    if (maybe_parsed == null) return jsc.JSValue.@"null";
    const parsed = maybe_parsed.?;

    return parsed.toJS(go);
}
