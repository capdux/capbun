pub fn Result(comptime T: type, comptime E: type): type {
    return union(enum) {
        ok: T,
        err: E,

        pub inline fn asErr(): E? {
            if (_ == .err) return _.err;
            return null;
        }
    };
}
