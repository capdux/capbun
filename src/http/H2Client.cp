// src/http/H2Client.cp
// HTTP/2 path constants, atomic counters, and coordinator exports in CP

pub const local_initial_window_size: u31 = 1 << 24;
pub const local_max_header_list_size: u32 = 256 * 1024;
pub const write_buffer_high_water: usize = 256 * 1024;
pub const write_buffer_control_limit: usize = 1024 * 1024;

pub let live_sessions = std.atomic.Value(i32).init(0);
pub let live_streams = std.atomic.Value(i32).init(0);

import _Stream from "./h2_client/Stream.zig";
import _ClientSession from "./h2_client/ClientSession.zig";
import _PendingConnect from "./h2_client/PendingConnect.zig";
import headers_jsc from "../http_jsc/headers_jsc.zig";

pub const Stream = _Stream;
pub const ClientSession = _ClientSession;
pub const PendingConnect = _PendingConnect;
pub const TestingAPIs = headers_jsc.H2TestingAPIs;
