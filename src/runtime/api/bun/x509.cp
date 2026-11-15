import bun from "bun";

const BoringSSL = bun.BoringSSL.c;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

extern fn Bun__X509__toJSLegacyEncoding(cert: *BoringSSL.X509, globalObject: *JSGlobalObject): JSValue;
extern fn Bun__X509__toJS(cert: *BoringSSL.X509, globalObject: *JSGlobalObject): JSValue;

pub fn isSafeAltName(name: []const u8, utf8: bool): bool {
    for (c of name) {
        switch (c) {
            '"', '\\', ',', '\'' => {
                return false;
            },
            else => {
                if (utf8) {
                    if (c < 32 || c == 127) {
                        return false;
                    }
                } else {
                    if (c < 32 || c > 126) {
                        return false;
                    }
                }
            },
        }
    }
    return true;
}

pub fn toJS(cert: *BoringSSL.X509, globalObject: *JSGlobalObject): bun.JSError!JSValue {
    return bun.jsc.fromJSHostCall(globalObject, @src(), Bun__X509__toJSLegacyEncoding, [cert, globalObject]);
}

pub fn toJSObject(cert: *BoringSSL.X509, globalObject: *JSGlobalObject): bun.JSError!JSValue {
    return Bun__X509__toJS(cert, globalObject);
}
