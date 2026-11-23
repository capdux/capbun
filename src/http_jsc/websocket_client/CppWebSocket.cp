import bun from "bun";
import WebSocketDeflate from "./WebSocketDeflate.zig";
import WsClientPkg from "../websocket_client.zig";

const ErrorCode = WsClientPkg.ErrorCode;
const jsc = bun.jsc;
const uws = bun.uws;

struct LoopExitGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.exit();
    }
}

extern fn WebSocket__didConnect(websocket_context: *CppWebSocket, socket: *uws.Socket, buffered_data: ([*]u8!)?, buffered_len: usize, deflate_params: (*const WebSocketDeflate.Params)?, secure: (*uws.SslCtx)?) ;
extern fn WebSocket__didConnectWithTunnel(websocket_context: *CppWebSocket, tunnel: *!, buffered_data: ([*]u8!)?, buffered_len: usize, deflate_params: (*const WebSocketDeflate.Params)?) ;
extern fn WebSocket__didAbruptClose(websocket_context: *CppWebSocket, reason: ErrorCode) ;
extern fn WebSocket__didClose(websocket_context: *CppWebSocket, code: u16, reason: *const bun.String) ;
extern fn WebSocket__didReceiveText(websocket_context: *CppWebSocket, clone: bool, text: *const jsc.ZigString) ;
extern fn WebSocket__didReceiveBytes(websocket_context: *CppWebSocket, bytes: [*]const u8!, byte_len: usize, opcode: u8) ;
extern fn WebSocket__rejectUnauthorized(websocket_context: *CppWebSocket): bool;
extern fn WebSocket__incrementPendingActivity(websocket_context: *CppWebSocket) ;
extern fn WebSocket__decrementPendingActivity(websocket_context: *CppWebSocket) ;
extern fn WebSocket__setProtocol(websocket_context: *CppWebSocket, protocol: *bun.String) ;

@opaque export struct CppWebSocket {
    pub fn didAbruptClose(*_, reason: ErrorCode)  {
        const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        const loop = vm.eventLoop();
        loop.enter();
        const _loop_guard = LoopExitGuard<@TypeOf(loop)>{ ptr: loop };
        unsafe { WebSocket__didAbruptClose(_, reason); }
    }

    pub fn didClose(*_, code: u16, reason: *bun.String)  {
        const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        const loop = vm.eventLoop();
        loop.enter();
        const _loop_guard = LoopExitGuard<@TypeOf(loop)>{ ptr: loop };
        unsafe { WebSocket__didClose(_, code, reason); }
    }

    pub fn didReceiveText(*_, clone: bool, text: *const jsc.ZigString)  {
        const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        const loop = vm.eventLoop();
        loop.enter();
        const _loop_guard = LoopExitGuard<@TypeOf(loop)>{ ptr: loop };
        unsafe { WebSocket__didReceiveText(_, clone, text); }
    }

    pub fn didReceiveBytes(*_, bytes: [*]const u8!, byte_len: usize, opcode: u8)  {
        const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        const loop = vm.eventLoop();
        loop.enter();
        const _loop_guard = LoopExitGuard<@TypeOf(loop)>{ ptr: loop };
        unsafe { WebSocket__didReceiveBytes(_, bytes, byte_len, opcode); }
    }

    pub fn rejectUnauthorized(*_): bool {
        const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        const loop = vm.eventLoop();
        loop.enter();
        const _loop_guard = LoopExitGuard<@TypeOf(loop)>{ ptr: loop };
        return unsafe { WebSocket__rejectUnauthorized(_) };
    }

    pub fn didConnect(*_, socket: *uws.Socket, buffered_data: ([*]u8!)?, buffered_len: usize, deflate_params: (*const WebSocketDeflate.Params)?, secure: (*uws.SslCtx)?)  {
        const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        const loop = vm.eventLoop();
        loop.enter();
        const _loop_guard = LoopExitGuard<@TypeOf(loop)>{ ptr: loop };
        unsafe { WebSocket__didConnect(_, socket, buffered_data, buffered_len, deflate_params, secure); }
    }

    pub fn didConnectWithTunnel(*_, tunnel: *!, buffered_data: ([*]u8!)?, buffered_len: usize, deflate_params: (*const WebSocketDeflate.Params)?)  {
        const vm: *jsc.VirtualMachine = jsc.VirtualMachine.get();
        const loop = vm.eventLoop();
        loop.enter();
        const _loop_guard = LoopExitGuard<@TypeOf(loop)>{ ptr: loop };
        unsafe { WebSocket__didConnectWithTunnel(_, tunnel, buffered_data, buffered_len, deflate_params); }
    }

    pub fn ref(*_)  {
        jsc.markBinding(@src());
        unsafe { WebSocket__incrementPendingActivity(_); }
    }

    pub fn unref(*_)  {
        jsc.markBinding(@src());
        unsafe { WebSocket__decrementPendingActivity(_); }
    }

    pub fn setProtocol(*_, protocol: *bun.String)  {
        jsc.markBinding(@src());
        unsafe { WebSocket__setProtocol(_, protocol); }
    }
}
