// src/http/H3Client.cp
// HTTP/3 client coordinator exports in CP language

pub let live_sessions = std.atomic.Value(u32).init(0);
pub let live_streams = std.atomic.Value(u32).init(0);

pub const Stream = @import("./h3_client/Stream.zig");
pub const ClientSession = @import("./h3_client/ClientSession.zig");
pub const ClientContext = @import("./h3_client/ClientContext.zig");
pub const PendingConnect = @import("./h3_client/PendingConnect.zig");
pub const AltSvc = @import("./h3_client/AltSvc.zig");
pub const TestingAPIs = @import("../http_jsc/headers_jsc.zig").H3TestingAPIs;
