import H2Client from "../http/H2Client.zig";
import H3Client from "../http/H3Client.zig";
import bun from "bun";
const jsc = bun.jsc;
const FetchHeaders = bun.webcore.FetchHeaders;
const Headers = bun.http.Headers;

pub fn toFetchHeaders(h: *Headers, global: *bun.jsc.JSGlobalObject): *FetchHeaders ! bun.JSError {
    if (h.entries.len == 0) {
        return FetchHeaders.createEmpty();
    }
    if (headers := FetchHeaders.create(
        global,
        h.entries.items(.name).ptr,
        h.entries.items(.value).ptr,
        &bun.ZigString.fromBytes(h.buf.items),
        @truncate(h.entries.len),
    )) {
        return headers;
    }
    return error.JSError;
}

pub struct H2TestingAPIs {
    pub static fn liveCounts(globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const obj = jsc.JSValue.createEmptyObject(globalThis, 2);
        obj.put(globalThis, jsc.ZigString.static("sessions"), .jsNumber(H2Client.live_sessions.load(.monotonic)));
        obj.put(globalThis, jsc.ZigString.static("streams"), .jsNumber(H2Client.live_streams.load(.monotonic)));
        return obj;
    }
}

pub struct H3TestingAPIs {
    pub static fn quicLiveCounts(globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): jsc.JSValue ! bun.JSError {
        const obj = jsc.JSValue.createEmptyObject(globalThis, 2);
        obj.put(globalThis, jsc.ZigString.static("sessions"), .jsNumber(H3Client.live_sessions.load(.monotonic)));
        obj.put(globalThis, jsc.ZigString.static("streams"), .jsNumber(H3Client.live_streams.load(.monotonic)));
        return obj;
    }
}
