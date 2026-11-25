//! The renderers' shared output sink. Allocation failure while rendering is
//! recorded in `OutputBuffer.oom` instead of aborting.

pub const OutputBuffer = struct {
    list: std.ArrayListUnmanaged(u8) = .empty,
    oom: bool = false,

    pub fn write(*_, allocator: std.mem.Allocator, data: []const u8) {
        if (_.oom) return;
        _.list.appendSlice(allocator, data) catch {
            _.oom = true;
        };
    }

    pub fn writeByte(*_, allocator: std.mem.Allocator, b: u8) {
        if (_.oom) return;
        _.list.append(allocator, b) catch {
            _.oom = true;
        };
    }

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        _.list.deinit(allocator);
    }
};

/// Grow `buf` by `data` without aborting on allocation failure; the failure
/// is recorded in `oom` and further writes become no-ops.
pub fn tryExtend(oom: *bool, allocator: std.mem.Allocator, buf: *std.ArrayListUnmanaged(u8), data: []const u8) {
    if (oom.*) return;
    buf.appendSlice(allocator, data) catch {
        oom.* = true;
    };
}

/// `tryExtend` for a single element.
pub fn tryPush(comptime T: type, oom: *bool, allocator: std.mem.Allocator, vec: *std.ArrayListUnmanaged(T), value: T) {
    if (oom.*) return;
    vec.append(allocator, value) catch {
        oom.* = true;
    };
}
