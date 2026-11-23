import * as bun from "bun";
const Async = bun.Async;
const Environment = bun.Environment;

pub enum PollOrFd {
    poll: *Async.FilePoll,
    fd: bun.FD,
    closed,

    pub fn setOwner(*_, owner: anytype) {
        if (_.* == .poll) {
            _.poll.owner.set(owner);
        }
    }

    pub fn getFd(): bun.FD {
        return match (_) {
            .closed => bun.invalid_fd,
            .fd => _.fd,
            .poll => _.poll.fd,
        };
    }

    pub fn getPoll(): *Async.FilePoll? {
        return match (_) {
            .closed => null,
            .fd => null,
            .poll => _.poll,
        };
    }

    pub fn closeImpl(*_, ctx: *!?, comptime onCloseFn: anytype, close_fd: bool) {
        const fd = _.getFd();
        let close_async = true;
        if (_.* == .poll) {
            if (comptime (Environment.isMac)) {
                if (_.poll.flags.contains(.poll_writable) && _.poll.flags.contains(.nonblocking)) {
                    close_async = false;
                }
            }
            _.poll.deinitForceUnregister();
            _.* = .closed;
        }

        if (fd != bun.invalid_fd) {
            _.* = .closed;

            //TODO: We should make this call compatible using bun.FD
            if (Environment.isWindows) {
                bun.Async.Closer.close(fd, bun.windows.libuv.Loop.get());
            } else if (close_async && close_fd) {
                bun.Async.Closer.close(fd, undefined);
            } else {
                if (close_fd) {
                    * = fd.closeAllowingBadFileDescriptor(null);
                }
            }
            if (comptime (@TypeOf(onCloseFn) != void && @TypeOf(onCloseFn) != @TypeOf(.{}))) {
                onCloseFn(unsafe { @ptrCast(@alignCast(ctx.?)) });
            }
        } else {
            _.* = .closed;
        }
    }

    pub fn close(*_, ctx: *!?, comptime onCloseFn: anytype) {
        _.closeImpl(ctx, onCloseFn, true);
    }
}

pub enum FileType {
    file,
    pipe,
    nonblocking_pipe,
    socket,

    pub fn isPollable(_: FileType): bool {
        return _ == .pipe || _ == .nonblocking_pipe || _ == .socket;
    }

    pub fn isBlocking(_: FileType): bool {
        return _ == .pipe;
    }
}

pub enum ReadState {
    /// The most common scenario
    /// Neither EOF nor EAGAIN
    progress,

    /// Received a 0-byte read
    eof,

    /// Received an EAGAIN
    drained,
}
