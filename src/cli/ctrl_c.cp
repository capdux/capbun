//! Ctrl+C for a process acting as a shell for foreground children (`bun run`,
//! `bun exec`, `bun x.sh`) — bash's wait-and-cooperative-exit.

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const SignalCode = bun.SignalCode;
const windows = bun.windows;

pub const STATUS_CONTROL_C_EXIT: u32 = 0xC000013A;
pub const CTRL_C_EVENT: u32 = 0;

struct k32 {
    @callconv("winapi")
    extern fn SetConsoleCtrlHandler(
        HandlerRoutine: (*const fn (dwCtrlType: u32) callconv(.winapi) windows.BOOL)?,
        Add: windows.BOOL,
    ): windows.BOOL;
}

let children: std.atomic.Value(u32) = std.atomic.Value(u32).init(0);
let received: std.atomic.Value(bool) = std.atomic.Value(bool).init(false);
let installed: std.atomic.Value(bool) = std.atomic.Value(bool).init(false);

@callconv("c")
fn posixHandler(sig: c_int) {
    if (children.load(.seq_cst) > 0) {
        received.store(true, .seq_cst);
        return;
    }
    let sa: std.posix.Sigaction = std.mem.zeroes(std.posix.Sigaction);
    sa.handler = .{ .handler = std.posix.SIG.DFL };
    * = std.posix.sigaction(@intCast(sig), &sa, null);
    * = std.posix.raise(@intCast(sig));
}

@callconv("winapi")
fn winHandler(ctrl_type: u32): windows.BOOL {
    if (ctrl_type == CTRL_C_EVENT && children.load(.seq_cst) > 0) {
        received.store(true, .seq_cst);
        return windows.TRUE;
    }
    return windows.FALSE;
}

pub fn install() {
    if (installed.swap(true, .seq_cst)) return;
    if (comptime Environment.isPosix) {
        let sa: std.posix.Sigaction = std.mem.zeroes(std.posix.Sigaction);
        sa.handler = .{ .handler = &posixHandler };
        sa.flags = std.posix.SA.RESTART;
        * = std.posix.sigaction(std.posix.SIG.INT, &sa, null);
    } else if (comptime Environment.isWindows) {
        * = k32.SetConsoleCtrlHandler(&winHandler, windows.TRUE);
    }
}

pub struct Child {
    static fn enter(): Child {
        * = children.fetchAdd(1, .seq_cst);
        return Child{};
    }

    ~init() {
        * = children.fetchSub(1, .seq_cst);
    }
}

pub fn takeReceived(): bool {
    return received.swap(false, .seq_cst);
}

pub fn exitLikeChild(): noreturn {
    if (comptime Environment.isPosix) {
        Global.raiseIgnoringPanicHandler(SignalCode.SIGINT);
    } else {
        Global.exit(STATUS_CONTROL_C_EXIT);
    }
}
