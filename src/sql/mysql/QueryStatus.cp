pub enum Status: u8 {
    pending,
    binding,
    running,
    partial_response,
    success,
    fail,

    pub fn isRunning(): bool {
        return @intFromEnum(_) > @intFromEnum(Status.pending) && @intFromEnum(_) < @intFromEnum(Status.success);
    }
}
