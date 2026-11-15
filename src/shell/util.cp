pub enum OutKind {
    stdout,
    stderr,

    fn toFd(): bun.FD {
        return switch (_) {
            .stdout => .stdout(),
            .stderr => .stderr(),
        };
    }
}

pub const Stdio = bun.spawn.Stdio;

pub const WatchFd = if (Environment.isLinux) posix.fd_t else i32;

import bun from "bun";
const Environment = bun.Environment;

const posix = std.posix;
