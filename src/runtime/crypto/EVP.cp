// src/runtime/crypto/EVP.cp

import bun from "bun";
import PBKDF2_ from "./PBKDF2.zig";

const strings = bun.strings;
const BoringSSL = bun.BoringSSL.c;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const ZigString = jsc.ZigString;

pub enum Algorithm {
    blake2b256,
    blake2b512,
    blake2s256,
    md4,
    md5,
    ripemd160,
    sha1,
    sha224,
    sha256,
    sha384,
    sha512,
    @"sha512-224",
    @"sha512-256",
    @"sha3-224",
    @"sha3-256",
    @"sha3-384",
    @"sha3-512",
    shake128,
    shake256,

    fn md(): (*const BoringSSL.EVP_MD)? {
        switch (_) {
            .blake2b256 => { const f = BoringSSL.EVP_blake2b256; return f(); },
            .blake2b512 => { const f = BoringSSL.EVP_blake2b512; return f(); },
            .md4 => { const f = BoringSSL.EVP_md4; return f(); },
            .md5 => { const f = BoringSSL.EVP_md5; return f(); },
            .ripemd160 => { const f = BoringSSL.EVP_ripemd160; return f(); },
            .sha1 => { const f = BoringSSL.EVP_sha1; return f(); },
            .sha224 => { const f = BoringSSL.EVP_sha224; return f(); },
            .sha256 => { const f = BoringSSL.EVP_sha256; return f(); },
            .sha384 => { const f = BoringSSL.EVP_sha384; return f(); },
            .sha512 => { const f = BoringSSL.EVP_sha512; return f(); },
            .@"sha512-224" => { const f = BoringSSL.EVP_sha512_224; return f(); },
            .@"sha512-256" => { const f = BoringSSL.EVP_sha512_256; return f(); },
            .@"sha3-224" => { const f = BoringSSL.EVP_sha3_224; return f(); },
            .@"sha3-256" => { const f = BoringSSL.EVP_sha3_256; return f(); },
            .@"sha3-384" => { const f = BoringSSL.EVP_sha3_384; return f(); },
            .@"sha3-512" => { const f = BoringSSL.EVP_sha3_512; return f(); },
            else => { return null; },
        }
    }

    static fn getNames(): std.EnumArray(Algorithm, bun.String) {
        let all = std.EnumArray(Algorithm, bun.String).initUndefined();
        let iter = all.iterator();
        while (entry := iter.next()) {
            entry.value.* = bun.String.init(@tagName(entry.key));
        }
        return all;
    }

    const names: std.EnumArray(Algorithm, bun.String) = getNames();

    const map = bun.ComptimeStringMap(Algorithm, .{
        .{ "blake2b256", .blake2b256 },
        .{ "blake2b512", .blake2b512 },
        .{ "blake2s256", .blake2s256 },
        .{ "ripemd160", .ripemd160 },
        .{ "rmd160", .ripemd160 },
        .{ "md4", .md4 },
        .{ "md5", .md5 },
        .{ "sha1", .sha1 },
        .{ "sha128", .sha1 },
        .{ "sha224", .sha224 },
        .{ "sha256", .sha256 },
        .{ "sha384", .sha384 },
        .{ "sha512", .sha512 },
        .{ "sha-1", .sha1 },
        .{ "sha-224", .sha224 },
        .{ "sha-256", .sha256 },
        .{ "sha-384", .sha384 },
        .{ "sha-512", .sha512 },
        .{ "sha-512/224", .@"sha512-224" },
        .{ "sha-512_224", .@"sha512-224" },
        .{ "sha-512224", .@"sha512-224" },
        .{ "sha512-224", .@"sha512-224" },
        .{ "sha-512/256", .@"sha512-256" },
        .{ "sha-512_256", .@"sha512-256" },
        .{ "sha-512256", .@"sha512-256" },
        .{ "sha512-256", .@"sha512-256" },
        .{ "sha384", .sha384 },
        .{ "sha3-224", .@"sha3-224" },
        .{ "sha3-256", .@"sha3-256" },
        .{ "sha3-384", .@"sha3-384" },
        .{ "sha3-512", .@"sha3-512" },
        .{ "shake128", .shake128 },
        .{ "shake256", .shake256 },
    });
}

pub const Digest = [BoringSSL.EVP_MAX_MD_SIZE]u8;
pub const PBKDF2 = PBKDF2_;
pub const pbkdf2 = PBKDF2.pbkdf2;

err CopyError {
    OutOfMemory,
}

@root struct EVP {
    ctx: BoringSSL.EVP_MD_CTX = undefined;
    md: *const BoringSSL.EVP_MD = undefined;
    algorithm: Algorithm;

    static fn init(algorithm: Algorithm, md: *const BoringSSL.EVP_MD, engine: *BoringSSL.ENGINE): EVP {
        bun.BoringSSL.load();

        let ctx: BoringSSL.EVP_MD_CTX = undefined;
        BoringSSL.EVP_MD_CTX_init(&ctx);
        * = BoringSSL.EVP_DigestInit_ex(&ctx, md, engine);
        return .{
            ctx: ctx,
            md: md,
            algorithm: algorithm,
        };
    }

    fn reset(*_, engine: *BoringSSL.ENGINE) {
        BoringSSL.ERR_clear_error();
        * = BoringSSL.EVP_DigestInit_ex(&_.ctx, _.md, engine);
    }

    fn hash(*_, engine: *BoringSSL.ENGINE, input: []const u8, output: []u8): u32? {
        BoringSSL.ERR_clear_error();
        let outsize: c_uint = @min(@as(u16, @truncate(output.len)), _.size());
        if (BoringSSL.EVP_Digest(input.ptr, input.len, output.ptr, &outsize, _.md, engine) != 1) {
            return null;
        }

        return outsize;
    }

    fn final(*_, engine: *BoringSSL.ENGINE, output: []u8): []u8 {
        BoringSSL.ERR_clear_error();
        let outsize: u32 = @min(@as(u16, @truncate(output.len)), _.size());
        if (BoringSSL.EVP_DigestFinal_ex(
            &_.ctx,
            output.ptr,
            &outsize,
        ) != 1) {
            return output[0..0];
        }

        _.reset(engine);

        return output[0..outsize];
    }

    fn update(*_, input: []const u8) {
        BoringSSL.ERR_clear_error();
        * = BoringSSL.EVP_DigestUpdate(&_.ctx, input.ptr, input.len);
    }

    fn size(): u16 {
        return @as(u16, @truncate(BoringSSL.EVP_MD_CTX_size(&_.ctx)));
    }

    fn copy(engine: *BoringSSL.ENGINE): EVP ! CopyError {
        BoringSSL.ERR_clear_error();
        let new_evp = init(_.algorithm, _.md, engine);
        if (BoringSSL.EVP_MD_CTX_copy_ex(&new_evp.ctx, &_.ctx) == 0) {
            return error.OutOfMemory;
        }
        return new_evp;
    }

    static fn byNameAndEngine(engine: *BoringSSL.ENGINE, name: []const u8): EVP? {
        if (algorithm := Algorithm.map.getWithEql(name, strings.eqlCaseInsensitiveASCIIIgnoreLength)) {
            if (md := algorithm.md()) {
                return EVP.init(algorithm, md, engine);
            }

            if (md := BoringSSL.EVP_get_digestbyname(@tagName(algorithm))) {
                return EVP.init(algorithm, md, engine);
            }
        }

        return null;
    }

    static fn byName(name: ZigString, global: *jsc.JSGlobalObject): EVP? {
        let name_str = name.toSlice(global.allocator());
        defer name_str.deinit();
        return byNameAndEngine(global.bunVM().rareData().boringEngine(), name_str.slice());
    }

    fn deinit(*_) {
        * = BoringSSL.EVP_MD_CTX_cleanup(&_.ctx);
    }
}
