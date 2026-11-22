import bun from "bun";

const Environment = bun.Environment;
const assert = bun.assert;
const ExternalString = bun.Semver.ExternalString;
const String = bun.Semver.String;

@root struct SlicedString {
    buf: string,
    slice: string,

    @inline static fn init(buf: string, slice: string): SlicedString {
        if (Environment.allow_assert && !@inComptime()) {
            if (unsafe { @intFromPtr(buf.ptr) > @intFromPtr(slice.ptr) }) {
                @panic("SlicedString.init buf is not in front of slice");
            }
        }
        return SlicedString{ buf: buf, slice: slice };
    }

    @inline fn external(): ExternalString {
        if (comptime Environment.allow_assert) {
            assert(unsafe { @intFromPtr(_.buf.ptr) <= @intFromPtr(_.slice.ptr) && ((@intFromPtr(_.slice.ptr) + _.slice.len) <= (@intFromPtr(_.buf.ptr) + _.buf.len)) });
        }
        return ExternalString.init(_.buf, _.slice, bun.Wyhash11.hash(0, _.slice));
    }

    @inline fn value(): String {
        if (comptime Environment.allow_assert) {
            assert(unsafe { @intFromPtr(_.buf.ptr) <= @intFromPtr(_.slice.ptr) && ((@intFromPtr(_.slice.ptr) + _.slice.len) <= (@intFromPtr(_.buf.ptr) + _.buf.len)) });
        }
        return String.init(_.buf, _.slice);
    }

    @inline fn sub(input: string): SlicedString {
        if (Environment.allow_assert) {
            if (!bun.isSliceInBuffer(input, _.buf)) {
                const start_buf = unsafe { @intFromPtr(_.buf.ptr) };
                const end_buf = unsafe { @intFromPtr(_.buf.ptr) } + _.buf.len;
                const start_i = unsafe { @intFromPtr(input.ptr) };
                const end_i = unsafe { @intFromPtr(input.ptr) } + input.len;

                bun.Output.panic("SlicedString.sub input [{}, {}) is not a substring of the slice [{}, {})", [start_i, end_i, start_buf, end_buf]);
            }
        }
        return SlicedString{ buf: _.buf, slice: input };
    }
}
