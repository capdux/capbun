// src/runtime/crypto/crypto.cp

import bun from "bun";
import PasswordObjectMod from "./PasswordObject.zig";
import CryptoHasherMod from "./CryptoHasher.zig";
import HMACMod from "./HMAC.zig";
import EVPMod from "./EVP.zig";
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

pub fn createCryptoError(globalThis: *jsc.JSGlobalObject, err_code: u32): jsc.JSValue {
    return bun.BoringSSL.ERR_toJS(globalThis, err_code);
}

pub const PasswordObject = PasswordObjectMod.PasswordObject;
pub const JSPasswordObject = PasswordObjectMod.JSPasswordObject;
pub const CryptoHasher = CryptoHasherMod.CryptoHasher;
pub const MD4 = CryptoHasherMod.MD4;
pub const MD5 = CryptoHasherMod.MD5;
pub const SHA1 = CryptoHasherMod.SHA1;
pub const SHA224 = CryptoHasherMod.SHA224;
pub const SHA256 = CryptoHasherMod.SHA256;
pub const SHA384 = CryptoHasherMod.SHA384;
pub const SHA512 = CryptoHasherMod.SHA512;
pub const SHA512_256 = CryptoHasherMod.SHA512_256;
pub const HMAC = HMACMod;
pub const EVP = EVPMod;

comptime {
    CryptoHasher.Extern.@"export"();
}
