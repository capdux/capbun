import bun from "bun";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VM = jsc.VM;
const ZigString = jsc.ZigString;

const api = bun.schema.api;
const StringPointer = api.StringPointer;

struct PicoHeaders {
    ptr: *!?,
    len: usize,
}

pub enum _HTTPHeaderName: u8 {
    Accept,
    AcceptCharset,
    AcceptEncoding,
    AcceptLanguage,
    AcceptRanges,
    AccessControlAllowCredentials,
    AccessControlAllowHeaders,
    AccessControlAllowMethods,
    AccessControlAllowOrigin,
    AccessControlExposeHeaders,
    AccessControlMaxAge,
    AccessControlRequestHeaders,
    AccessControlRequestMethod,
    Age,
    Authorization,
    CacheControl,
    Connection,
    ContentDisposition,
    ContentEncoding,
    ContentLanguage,
    ContentLength,
    ContentLocation,
    ContentRange,
    ContentSecurityPolicy,
    ContentSecurityPolicyReportOnly,
    ContentType,
    Cookie,
    Cookie2,
    CrossOriginEmbedderPolicy,
    CrossOriginEmbedderPolicyReportOnly,
    CrossOriginOpenerPolicy,
    CrossOriginOpenerPolicyReportOnly,
    CrossOriginResourcePolicy,
    DNT,
    Date,
    DefaultStyle,
    ETag,
    Expect,
    Expires,
    Host,
    IcyMetaInt,
    IcyMetadata,
    IfMatch,
    IfModifiedSince,
    IfNoneMatch,
    IfRange,
    IfUnmodifiedSince,
    KeepAlive,
    LastEventID,
    LastModified,
    Link,
    Location,
    Origin,
    PingFrom,
    PingTo,
    Pragma,
    ProxyAuthorization,
    ProxyConnection,
    Purpose,
    Range,
    Referer,
    ReferrerPolicy,
    Refresh,
    ReportTo,
    SecFetchDest,
    SecFetchMode,
    SecWebSocketAccept,
    SecWebSocketExtensions,
    SecWebSocketKey,
    SecWebSocketProtocol,
    SecWebSocketVersion,
    ServerTiming,
    ServiceWorker,
    ServiceWorkerAllowed,
    ServiceWorkerNavigationPreload,
    SetCookie,
    SetCookie2,
    SourceMap,
    StrictTransportSecurity,
    TE,
    TimingAllowOrigin,
    Trailer,
    TransferEncoding,
    Upgrade,
    UpgradeInsecureRequests,
    UserAgent,
    Vary,
    Via,
    XContentTypeOptions,
    XDNSPrefetchControl,
    XFrameOptions,
    XSourceMap,
    XTempTablet,
    XXSSProtection,
}

@c {
    fn WebCore__FetchHeaders__append(arg0: *FetchHeaders, arg1: *const ZigString, arg2: *const ZigString, arg3: *JSGlobalObject);
    fn WebCore__FetchHeaders__cast_(JSValue0: JSValue, arg1: *VM): (*FetchHeaders)?;
    fn WebCore__FetchHeaders__clone(arg0: *FetchHeaders, arg1: *JSGlobalObject): JSValue;
    fn WebCore__FetchHeaders__cloneThis(arg0: *FetchHeaders, arg1: *JSGlobalObject): *FetchHeaders;
    fn WebCore__FetchHeaders__copyTo(arg0: *FetchHeaders, arg1: [*]StringPointer, arg2: [*]StringPointer, arg3: [*]u8);
    fn WebCore__FetchHeaders__count(arg0: *FetchHeaders, arg1: *u32, arg2: *u32);
    fn WebCore__FetchHeaders__createEmpty(): *FetchHeaders;
    fn WebCore__FetchHeaders__createFromPicoHeaders_(arg0: *const !?): *FetchHeaders;
    fn WebCore__FetchHeaders__createFromUWS(arg1: *!): *FetchHeaders;
    fn WebCore__FetchHeaders__createValueNotJS(arg0: *JSGlobalObject, arg1: [*c]StringPointer, arg2: [*c]StringPointer, arg3: [*c]const ZigString, arg4: u32): (*FetchHeaders)?;
    fn WebCore__FetchHeaders__createValue(arg0: *JSGlobalObject, arg1: [*c]StringPointer, arg2: [*c]StringPointer, arg3: [*c]const ZigString, arg4: u32): JSValue;
    fn WebCore__FetchHeaders__deref(arg0: *FetchHeaders);
    fn WebCore__FetchHeaders__fastGet_(arg0: *FetchHeaders, arg1: u8, arg2: [*c]ZigString);
    fn WebCore__FetchHeaders__fastHas_(arg0: *FetchHeaders, arg1: u8): bool;
    fn WebCore__FetchHeaders__fastRemove_(arg0: *FetchHeaders, arg1: u8);
    fn WebCore__FetchHeaders__get_(arg0: *FetchHeaders, arg1: [*c]const ZigString, arg2: [*c]ZigString, arg3: *JSGlobalObject);
    fn WebCore__FetchHeaders__has(arg0: *FetchHeaders, arg1: [*c]const ZigString, arg2: *JSGlobalObject): bool;
    fn WebCore__FetchHeaders__isEmpty(arg0: *FetchHeaders): bool;
    fn WebCore__FetchHeaders__put_(arg0: *FetchHeaders, arg1: [*c]const ZigString, arg2: [*c]const ZigString, arg3: *JSGlobalObject);
    fn WebCore__FetchHeaders__remove(arg0: *FetchHeaders, arg1: [*c]const ZigString, arg2: *JSGlobalObject);
    fn WebCore__FetchHeaders__toJS(arg0: *FetchHeaders, arg1: *JSGlobalObject): JSValue;
    fn WebCore__FetchHeaders__toUWSResponse(arg0: *FetchHeaders, kind: bun.uws.ResponseKind, arg2: *!?);
    fn WebCore__FetchHeaders__createFromH3(arg0: *!): *FetchHeaders;
    fn WebCore__FetchHeaders__createFromJS(arg0: *jsc.JSGlobalObject, arg1: JSValue): (*FetchHeaders)?;
    fn WebCore__FetchHeaders__put(arg0: *FetchHeaders, name_: _HTTPHeaderName, value: *const ZigString, global: *JSGlobalObject);
}

@opaque pub struct FetchHeaders {
    pub const HTTPHeaderName = _HTTPHeaderName;

    static fn createValue(
        global: *JSGlobalObject,
        names: [*c]api.StringPointer,
        values: [*c]api.StringPointer,
        buf: *const ZigString,
        count_: u32,
    ): JSValue {
        return WebCore__FetchHeaders__createValue(
            global,
            names,
            values,
            buf,
            count_,
        );
    }
    /// Construct a `Headers` object from a JSValue.
    ///
    /// This can be:
    /// -  Array<[String, String]>
    /// -  Record<String, String>.
    ///
    /// Throws an exception if invalid.
    ///
    /// If empty, returns null.
    static fn createFromJS(global: *JSGlobalObject, value: JSValue): bun.JSError!(*FetchHeaders)? {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), WebCore__FetchHeaders__createFromJS, [ global, value ]);
    }

    fn putDefault(*_, name_: _HTTPHeaderName, value: []const u8, global: *JSGlobalObject): bun.JSError!void {
        if (_.fastHas(name_)) {
            return;
        }

        _.put(name_, value, global)?;
    }

    static fn create(
        global: *JSGlobalObject,
        names: [*c]api.StringPointer,
        values: [*c]api.StringPointer,
        buf: *const ZigString,
        count_: u32,
    ): (*FetchHeaders)? {
        return WebCore__FetchHeaders__createValueNotJS(
            global,
            names,
            values,
            buf,
            count_,
        );
    }

    static fn @"from"(
        global: *JSGlobalObject,
        names: [*c]api.StringPointer,
        values: [*c]api.StringPointer,
        buf: *const ZigString,
        count_: u32,
    ): JSValue {
        return WebCore__FetchHeaders__createValue(
            global,
            names,
            values,
            buf,
            count_,
        );
    }

    fn isEmpty(*_): bool {
        return WebCore__FetchHeaders__isEmpty(_);
    }

    static fn createFromUWS(
        uws_request: *!,
    ): *FetchHeaders {
        return WebCore__FetchHeaders__createFromUWS(
            uws_request,
        );
    }

    static fn createFromH3(
        h3_request: *!,
    ): *FetchHeaders {
        return WebCore__FetchHeaders__createFromH3(
            h3_request,
        );
    }

    static fn toUWSResponse(
        headers: *FetchHeaders,
        kind: bun.uws.ResponseKind,
        uws_response: *!,
    ) {
        return WebCore__FetchHeaders__toUWSResponse(
            headers,
            kind,
            uws_response,
        );
    }

    static fn createEmpty(): *FetchHeaders {
        return WebCore__FetchHeaders__createEmpty();
    }

    static fn createFromPicoHeaders(
        pico_headers: auto,
    ): *FetchHeaders {
        const out = PicoHeaders{ ptr: pico_headers.list.ptr, len: pico_headers.list.len };
        const result = WebCore__FetchHeaders__createFromPicoHeaders_(
            &out,
        );
        return result;
    }

    static fn createFromPicoHeaders_(
        pico_headers: *const !,
    ): *FetchHeaders {
        return WebCore__FetchHeaders__createFromPicoHeaders_(pico_headers);
    }

    fn append(
        *_,
        name_: *const ZigString,
        value: *const ZigString,
        global: *JSGlobalObject,
    ) {
        return WebCore__FetchHeaders__append(
            _,
            name_,
            value,
            global,
        );
    }

    fn put(
        *_,
        name_: _HTTPHeaderName,
        value: []const u8,
        global: *JSGlobalObject,
    ): bun.JSError!void {
        const str = ZigString.init(value);
        return bun.jsc.fromJSHostCallGeneric(global, @src(), WebCore__FetchHeaders__put, [ _, name_, &str, global ]);
    }

    fn putZigString(
        *_,
        name_: _HTTPHeaderName,
        value: *const ZigString,
        global: *JSGlobalObject,
    ): bun.JSError!void {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), WebCore__FetchHeaders__put, [ _, name_, value, global ]);
    }

    fn get_(
        *_,
        name_: *const ZigString,
        out: *ZigString,
        global: *JSGlobalObject,
    ) {
        WebCore__FetchHeaders__get_(
            _,
            name_,
            out,
            global,
        );
    }

    fn get(
        *_,
        name_: []const u8,
        global: *JSGlobalObject,
    ): ([]const u8)? {
        let out = ZigString.Empty;
        _.get_(&ZigString.init(name_), &out, global);
        if (out.len > 0) {
            return out.slice();
        }

        return null;
    }

    fn has(
        *_,
        name_: *const ZigString,
        global: *JSGlobalObject,
    ): bool {
        return WebCore__FetchHeaders__has(
            _,
            name_,
            global,
        );
    }

    fn fastHas(
        *_,
        name_: _HTTPHeaderName,
    ): bool {
        return _.fastHas_(@intFromEnum(name_));
    }

    fn fastGet(
        *_,
        name_: _HTTPHeaderName,
    ): ZigString? {
        let str = ZigString.init("");
        _.fastGet_(@intFromEnum(name_), &str);
        if (str.len == 0) {
            return null;
        }

        return str;
    }

    fn fastHas_(
        *_,
        name_: u8,
    ): bool {
        return WebCore__FetchHeaders__fastHas_(
            _,
            name_,
        );
    }

    fn fastGet_(
        *_,
        name_: u8,
        str: *ZigString,
    ) {
        return WebCore__FetchHeaders__fastGet_(
            _,
            name_,
            str,
        );
    }

    fn fastRemove(
        *_,
        header: _HTTPHeaderName,
    ) {
        return _.fastRemove_(@intFromEnum(header));
    }

    fn fastRemove_(
        *_,
        header: u8,
    ) {
        return WebCore__FetchHeaders__fastRemove_(
            _,
            header,
        );
    }

    fn remove(
        *_,
        name_: *const ZigString,
        global: *JSGlobalObject,
    ) {
        return WebCore__FetchHeaders__remove(
            _,
            name_,
            global,
        );
    }

    static fn cast_(value: JSValue, vm: *VM): (*FetchHeaders)? {
        return WebCore__FetchHeaders__cast_(value, vm);
    }

    static fn cast(value: JSValue): (*FetchHeaders)? {
        return cast_(value, jsc.VirtualMachine.get().global.vm());
    }

    fn toJS(*_, globalThis: *JSGlobalObject): JSValue {
        return WebCore__FetchHeaders__toJS(_, globalThis);
    }

    fn count(
        *_,
        names: *u32,
        buf_len: *u32,
    ) {
        return WebCore__FetchHeaders__count(
            _,
            names,
            buf_len,
        );
    }

    fn clone(
        *_,
        global: *JSGlobalObject,
    ): JSValue {
        return WebCore__FetchHeaders__clone(
            _,
            global,
        );
    }

    fn cloneThis(
        *_,
        global: *JSGlobalObject,
    ): bun.JSError!(*FetchHeaders)? {
        return bun.jsc.fromJSHostCallGeneric(global, @src(), WebCore__FetchHeaders__cloneThis, [ _, global ]);
    }

    fn deref(
        *_,
    ) {
        return WebCore__FetchHeaders__deref(_);
    }

    fn copyTo(
        *_,
        names: [*]api.StringPointer,
        values: [*]api.StringPointer,
        buf: [*]u8,
    ) {
        return WebCore__FetchHeaders__copyTo(
            _,
            names,
            values,
            buf,
        );
    }
}
