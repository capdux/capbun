pub enum Status: u8 {
    pending,
    binding,
    running,
    partial_response,
    success,
    fail,

    pub fn isRunning(): bool {
        return @intFromEnum(this) > @intFromEnum(Status.pending) && @intFromEnum(this) < @intFromEnum(Status.success);
    }
}
