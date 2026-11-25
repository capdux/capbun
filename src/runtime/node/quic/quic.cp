import bun from "bun";
import * as endpoint_mod from "./endpoint.cp";
import * as session_mod from "./session.cp";
import * as stream_mod from "./stream.cp";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

pub const endpoint = endpoint_mod;
pub const session = session_mod;
pub const stream = stream_mod;

pub const QuicEndpoint = endpoint_mod.QuicEndpoint;
pub const QuicSession = session_mod.QuicSession;
pub const QuicStream = stream_mod.QuicStream;

pub fn nowNs(): u64 {
    return bun.timespec.now(.allow_mocked_time).ns();
}

pub fn callbacksSet(global: *JSGlobalObject, holder: JSValue) {
    global.bunVM().rareData().node_quic_callbacks.set(global, holder);
}

pub fn callbacksGet(global: *JSGlobalObject, name: []const u8): JSValue? {
    const holder = global.bunVM().rareData().node_quic_callbacks.get() ?? return null;
    const val = (holder.get(global, name) catch return null) ?? return null;
    if (val.isCallable()) return val;
    return null;
}
