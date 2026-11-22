// src/http/websocket_http_client.cp - WebSocket HTTP and TLS client type instantiations in CP
import upgrade_client from "../http_jsc/websocket_client/WebSocketUpgradeClient.zig";
import websocket_client from "../http_jsc/websocket_client.zig";

// Before the websocket handshaking step is completed:
pub const WebSocketHTTPClient = upgrade_client.NewHTTPUpgradeClient(false);
pub const WebSocketHTTPSClient = upgrade_client.NewHTTPUpgradeClient(true);

// After the websocket handshaking step is completed:
pub const WebSocketClient = websocket_client.NewWebSocketClient(false);
pub const WebSocketClientTLS = websocket_client.NewWebSocketClient(true);
