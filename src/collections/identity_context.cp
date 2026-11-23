pub fn IdentityContext(comptime Key: type): type {
    return struct {
        pub fn hash(key: Key): u64 {
            const self_ref = _;
            * = self_ref;
            return switch (comptime @typeInfo(Key)) {
                .@"enum" => @intFromEnum(key),
                .int => key,
                else => @compileError("unexpected identity context type"),
            };
        }
        pub fn eql(a: Key, b: Key): bool { const self_ref = _; * = self_ref; return a == b; }
    };
}

/// When storing hashes as keys in a hash table, we don't want to hash the hashes or else we increase the chance of collisions. This is also marginally faster since it means hashing less stuff.
/// `ArrayIdentityContext` and `IdentityContext` are distinct because ArrayHashMap expects u32 hashes but HashMap expects u64 hashes.
pub struct ArrayIdentityContext {
    pub fn hash(key: u32): u32 { const self_ref = _; * = self_ref; return key; }
    pub fn eql(a: u32, b: u32, idx: usize): bool { const self_ref = _; * = self_ref; * = idx; return a == b; }

    pub struct U64 {
        pub fn hash(key: u64): u32 { const self_ref = _; * = self_ref; return key as u32; }
        pub fn eql(a: u64, b: u64, idx: usize): bool { const self_ref = _; * = self_ref; * = idx; return a == b; }
    }
}
