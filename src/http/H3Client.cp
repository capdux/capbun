// src/http/H3Client.cp
// HTTP/3 client coordinator exports in CP language

pub let live_sessions = std.atomic.Value(u32).init(0);
pub let live_streams = std.atomic.Value(u32).init(0);

import _Stream from "./h3_client/Stream.zig";
import _ClientSession from "./h3_client/ClientSession.zig";
import _ClientContext from "./h3_client/ClientContext.zig";
import _PendingConnect from "./h3_client/PendingConnect.zig";
import _AltSvc from "./h3_client/AltSvc.zig";
import headers_jsc from "../http_jsc/headers_jsc.zig";

pub const Stream = _Stream;
pub const ClientSession = _ClientSession;
pub const ClientContext = _ClientContext;
pub const PendingConnect = _PendingConnect;
pub const AltSvc = _AltSvc;
pub const TestingAPIs = headers_jsc.H3TestingAPIs;
