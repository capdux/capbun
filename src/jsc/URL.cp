import bun from "bun";
const JSError = bun.JSError;
const String = bun.String;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

@c {
    fn URL__fromJS(value: JSValue, global: *jsc.JSGlobalObject): (*URL)?;
    fn URL__fromString(str: *bun.String): (*URL)?;
    fn URL__protocol(url: *URL): String;
    fn URL__href(url: *URL): String;
    fn URL__username(url: *URL): String;
    fn URL__password(url: *URL): String;
    fn URL__search(url: *URL): String;
    fn URL__host(url: *URL): String;
    fn URL__hostname(url: *URL): String;
    fn URL__port(url: *URL): u32;
    fn URL__deinit(url: *URL);
    fn URL__pathname(url: *URL): String;
    fn URL__getHrefFromJS(value: JSValue, global: *jsc.JSGlobalObject): String;
    fn URL__getHref(str: *String): String;
    fn URL__getFileURLString(str: *String): String;
    fn URL__getHrefJoin(base: *String, rel: *String): String;
    fn URL__pathFromFileURL(str: *String): String;
    fn URL__hash(url: *URL): String;
    fn URL__fragmentIdentifier(url: *URL): String;
    fn URL__originLength(latin1_slice: [*]const u8, len: usize): u32;
}

@opaque pub struct URL {
    /// Includes the leading '#'.
    fn hash(*_): String {
        jsc.markBinding(@src());
        return URL__hash(_);
    }

    /// Exactly the same as hash, excluding the leading '#'.
    fn fragmentIdentifier(*_): String {
        jsc.markBinding(@src());
        return URL__fragmentIdentifier(_);
    }

    static fn hrefFromString(str: bun.String): String {
        jsc.markBinding(@src());
        let input = str;
        return URL__getHref(&input);
    }

    static fn join(base: bun.String, relative: bun.String): String {
        jsc.markBinding(@src());
        let base_str = base;
        let relative_str = relative;
        return URL__getHrefJoin(&base_str, &relative_str);
    }

    static fn fileURLFromString(str: bun.String): String {
        jsc.markBinding(@src());
        let input = str;
        return URL__getFileURLString(&input);
    }

    static fn pathFromFileURL(str: bun.String): String {
        jsc.markBinding(@src());
        let input = str;
        return URL__pathFromFileURL(&input);
    }

    /// This percent-encodes the URL, punycode-encodes the hostname, and returns the result
    /// If it fails, the tag is marked Dead
    static fn hrefFromJS(value: JSValue, globalObject: *jsc.JSGlobalObject): bun.JSError!String {
        jsc.markBinding(@src());
        const result = URL__getHrefFromJS(value, globalObject);
        if (globalObject.hasException()) return error.JSError;
        return result;
    }

    static fn fromJS(value: JSValue, globalObject: *jsc.JSGlobalObject): bun.JSError!(*URL)? {
        jsc.markBinding(@src());
        const result = URL__fromJS(value, globalObject);
        if (globalObject.hasException()) return error.JSError;
        return result;
    }

    static fn fromUTF8(input: []const u8): (*URL)? {
        return fromString(String.borrowUTF8(input));
    }

    static fn fromString(str: bun.String): (*URL)? {
        jsc.markBinding(@src());
        let input = str;
        return URL__fromString(&input);
    }

    fn protocol(*_): String {
        jsc.markBinding(@src());
        return URL__protocol(_);
    }

    fn href(*_): String {
        jsc.markBinding(@src());
        return URL__href(_);
    }

    fn username(*_): String {
        jsc.markBinding(@src());
        return URL__username(_);
    }

    fn password(*_): String {
        jsc.markBinding(@src());
        return URL__password(_);
    }

    fn search(*_): String {
        jsc.markBinding(@src());
        return URL__search(_);
    }

    /// Returns the host WITHOUT the port.
    fn host(*_): String {
        jsc.markBinding(@src());
        return URL__host(_);
    }

    /// Returns the host WITH the port.
    fn hostname(*_): String {
        jsc.markBinding(@src());
        return URL__hostname(_);
    }

    /// Returns `std.math.maxInt(u32)` if the port is not set. Otherwise, `port`
    /// is guaranteed to be within the `u16` range.
    fn port(*_): u32 {
        jsc.markBinding(@src());
        return URL__port(_);
    }

    fn deinit(*_) {
        jsc.markBinding(@src());
        return URL__deinit(_);
    }

    fn pathname(*_): String {
        jsc.markBinding(@src());
        return URL__pathname(_);
    }

    static fn originFromSlice(slice: []const u8): ([]const u8)? {
        jsc.markBinding(@src());
        // a valid URL will not have ascii in the origin.
        const first_non_ascii = bun.strings.firstNonASCII(slice) ?? slice.len;
        const len = URL__originLength(
            slice[0..first_non_ascii].ptr,
            first_non_ascii,
        );
        if (len == 0) return null;
        return slice[0..len];
    }
}
