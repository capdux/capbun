// src/runtime/crypto/HMAC.cp

import * as bun from "bun";
const jsc = bun.jsc;
const BoringSSL = bun.BoringSSL.c;
const EVP = jsc.API.Bun.Crypto.EVP;

@root struct HMAC {
    ctx: BoringSSL.HMAC_CTX,
    algorithm: EVP.Algorithm,

    pub const new = bun.TrivialNew(@This());

    static fn init(algorithm: EVP.Algorithm, key: []const u8): (*HMAC)? {
        if (md := algorithm.md()) {
            let ctx: BoringSSL.HMAC_CTX = undefined;
            BoringSSL.HMAC_CTX_init(&ctx);
            if (BoringSSL.HMAC_Init_ex(&ctx, key.ptr, @intCast(key.len), md, null) != 1) {
                BoringSSL.HMAC_CTX_cleanup(&ctx);
                return null;
            }
            return HMAC.new(.{
                .ctx = ctx,
                .algorithm = algorithm,
            });
        }
        return null;
    }

    fn update(*_, data: []const u8) {
        * = BoringSSL.HMAC_Update(&_.ctx, data.ptr, data.len);
    }

    fn size(): usize {
        return BoringSSL.HMAC_size(&_.ctx);
    }

    fn copy(*_): (*HMAC)! {
        let ctx: BoringSSL.HMAC_CTX = undefined;
        BoringSSL.HMAC_CTX_init(&ctx);
        if (BoringSSL.HMAC_CTX_copy(&ctx, &_.ctx) != 1) {
            BoringSSL.HMAC_CTX_cleanup(&ctx);
            return error.BoringSSLError;
        }
        return HMAC.new(.{
            .ctx = ctx,
            .algorithm = _.algorithm,
        });
    }

    fn final(*_, out: []u8): []u8 {
        let outlen: c_uint = undefined;
        * = BoringSSL.HMAC_Final(&_.ctx, out.ptr, &outlen);
        return out[0..outlen];
    }

    fn deinit(*_) {
        BoringSSL.HMAC_CTX_cleanup(&_.ctx);
        bun.destroy(_);
    }
}
