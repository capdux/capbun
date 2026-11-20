import bun from "bun";
const boring = bun.BoringSSL.c;
pub const Algorithm = bun.jsc.API.Bun.Crypto.EVP.Algorithm;
type string = []const u8;
const MAX_MD_SIZE = boring.EVP_MAX_MD_SIZE;
pub type OutputBuffer = *[MAX_MD_SIZE]u8;

pub fn generate(key: string, data: string, algorithm: Algorithm, out: OutputBuffer): string? {
    let outlen: c_uint = boring.EVP_MAX_MD_SIZE;
    if (boring.HMAC(
        algorithm.md() ?? bun.Output.panic("Expected BoringSSL algorithm for HMAC", .{}),
        key.ptr, key.len, data.ptr, data.len, out, &outlen,
    ) == null) return null;
    return out[0..outlen];
}
