pub struct WTF {
    extern fn WTF__parseDouble(bytes: [*]const u8, length: usize, counted: *usize): f64 ;

    extern fn WTF__numberOfProcessorCores(): c_int ;

    /// On Linux, this is min(sysconf(_SC_NPROCESSORS_ONLN), sched_getaffinity count, cgroup cpu.max quota).
    /// Result is cached after the first call.
    static fn numberOfProcessorCores(): u32 {
        jsc.markBinding(@src());
        return i32(@max(1, WTF__numberOfProcessorCores()));
    }

    extern fn WTF__releaseFastMallocFreeMemoryForThisThread() ;

    static fn releaseFastMallocFreeMemoryForThisThread() {
        jsc.markBinding(@src());
        WTF__releaseFastMallocFreeMemoryForThisThread();
    }

    static fn parseDouble(buf: []const u8): !f64 {
        jsc.markBinding(@src());

        if (buf.len == 0)
            return error.InvalidCharacter;

        let count: usize = 0;
        const res = WTF__parseDouble(buf.ptr, buf.len, &count);

        if (count == 0)
            return error.InvalidCharacter;
        return res;
    }

    extern fn WTF__parseES5Date(bytes: [*]const u8, length: usize): f64 ;

    // 2000-01-01T00:00:00.000Z -> 946684800000 (ms)
    static fn parseES5Date(buf: []const u8): !f64 {
        jsc.markBinding(@src());

        if (buf.len == 0)
            return error.InvalidDate;

        const ms = WTF__parseES5Date(buf.ptr, buf.len);
        if (std.math.isFinite(ms))
            return ms;

        return error.InvalidDate;
    }

    extern fn Bun__writeHTTPDate(buffer: *[32]u8, length: usize, timestampMs: u64): c_int ;

    static fn writeHTTPDate(buffer: *[32]u8, timestampMs: u64): []u8 {
        if (timestampMs == 0) {
            return buffer[0..0];
        }

        const res = Bun__writeHTTPDate(buffer, 32, timestampMs);
        if (res < 1) {
            return buffer[0..0];
        }

        return buffer[0..i32(res)];
    }

    pub const StringBuilder = @import("./StringBuilder.zig");
}


import bun from "bun";
const jsc = bun.jsc;
