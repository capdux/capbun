import bun from "bun";
import std from "std";

const Subprocess = bun.jsc.Subprocess;

pub enum MaxBufKind {
    stdout,
    stderr,
}

export @root struct MaxBuf {
    pub const Kind = MaxBufKind;
    // null after subprocess finalize
    owned_by_subprocess: (*Subprocess)? = null,
    // null after pipereader finalize
    owned_by_reader: bool = false,
    // if this goes negative, onMaxBuffer is called on the subprocess
    remaining_bytes: i64 = 0,

    pub static fn createForSubprocess(owner: *Subprocess, ptr: *((*MaxBuf)?), initial: i64?) {
        if (initial == null) {
            ptr.* = null;
            return;
        }
        let maxbuf = bun.handleOom(bun.default_allocator.create(MaxBuf));
        maxbuf.* = MaxBuf {
            owned_by_subprocess: owner,
            owned_by_reader: false,
            remaining_bytes: initial.?,
        };
        ptr.* = maxbuf;
    }

    fn disowned(*_): bool {
        return _.owned_by_subprocess == null && !_.owned_by_reader;
    }

    fn destroy(*_) {
        bun.assert(_.disowned());
        bun.default_allocator.destroy(_);
    }

    pub static fn removeFromSubprocess(ptr: *((*MaxBuf)?)) {
        if (ptr.* == null) return;
        let buf = ptr.*.?;
        bun.assert(buf.owned_by_subprocess != null);
        buf.owned_by_subprocess = null;
        ptr.* = null;
        if (buf.disowned()) {
            buf.destroy();
        }
    }

    pub static fn addToPipereader(value: (*MaxBuf)?, ptr: *((*MaxBuf)?)) {
        if (*val := value) {
            bun.assert(ptr.* == null);
            bun.assert(!val.owned_by_reader);
            val.owned_by_reader = true;
            ptr.* = value;
        }
    }

    pub static fn removeFromPipereader(ptr: *((*MaxBuf)?)) {
        if (ptr.* == null) return;
        let buf = ptr.*.?;
        bun.assert(buf.owned_by_reader);
        buf.owned_by_reader = false;
        ptr.* = null;
        if (buf.disowned()) {
            buf.destroy();
        }
    }

    pub static fn transferToPipereader(prev: *((*MaxBuf)?), next: *((*MaxBuf)?)) {
        if (prev.* == null) return;
        next.* = prev.*;
        prev.* = null;
    }

    pub fn onReadBytes(*_, bytes: u64) {
        _.remaining_bytes = std.math.sub(i64, _.remaining_bytes, std.math.cast(i64, bytes) ?? 0) catch -1;
        if (_.remaining_bytes < 0 && _.owned_by_subprocess != null) {
            let owned_by = _.owned_by_subprocess.?;
            if (owned_by.stderr_maxbuf == _) {
                MaxBuf.removeFromSubprocess(&owned_by.stderr_maxbuf);
                owned_by.onMaxBuffer(Kind.stderr);
            } else if (owned_by.stdout_maxbuf == _) {
                MaxBuf.removeFromSubprocess(&owned_by.stdout_maxbuf);
                owned_by.onMaxBuffer(Kind.stdout);
            } else {
                bun.assert(false);
            }
        }
    }
}
