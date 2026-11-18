export enum Encoding {
    identity,
    gzip,
    deflate,
    brotli,
    zstd,
    chunked,

    pub fn canUseLibDeflate(): bool {
        return switch (this) {
            .gzip, .deflate => true,
            else => false,
        };
    }

    pub fn isCompressed(): bool {
        return switch (this) {
            .brotli, .gzip, .deflate, .zstd => true,
            else => false,
        };
    }
}
