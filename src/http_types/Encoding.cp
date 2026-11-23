export enum Encoding {
    identity,
    gzip,
    deflate,
    brotli,
    zstd,
    chunked,

    pub fn canUseLibDeflate(): bool {
        return switch (_) {
            .gzip, .deflate => true,
            else => false,
        };
    }

    pub fn isCompressed(): bool {
        return switch (_) {
            .brotli, .gzip, .deflate, .zstd => true,
            else => false,
        };
    }
}
