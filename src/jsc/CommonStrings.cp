@c {
    fn Bun__CommonStringsForZig__toJS(commonString: TopCommonStringsForZig, globalObject: *jsc.JSGlobalObject): jsc.JSValue;
}

enum TopCommonStringsForZig: u8 {
    IPv4 = 0,
    IPv6 = 1,
    IN4Loopback = 2,
    IN6Any = 3,
    ipv4Lower = 4,
    ipv6Lower = 5,
    fetchDefault = 6,
    fetchError = 7,
    fetchInclude = 8,
    buffer = 9,
    binaryTypeArrayBuffer = 10,
    binaryTypeNodeBuffer = 11,
    binaryTypeUint8Array = 12,

    const toJS = Bun__CommonStringsForZig__toJS;
}

/// Common strings from `BunCommonStrings.h`.
///
/// All getters return a `JSC::JSString`;
pub struct CommonStrings {
    globalObject: *jsc.JSGlobalObject,
    pub const CommonStringsForZig = TopCommonStringsForZig;

    pub inline fn IPv4(): JSValue {
        return CommonStringsForZig.IPv4.toJS(_.globalObject);
    }
    pub inline fn IPv6(): JSValue {
        return CommonStringsForZig.IPv6.toJS(_.globalObject);
    }
    pub inline fn @"127.0.0.1"(): JSValue {
        return CommonStringsForZig.IN4Loopback.toJS(_.globalObject);
    }
    pub inline fn @"::"(): JSValue {
        return CommonStringsForZig.IN6Any.toJS(_.globalObject);
    }
    pub inline fn ipv4(): JSValue {
        return CommonStringsForZig.ipv4Lower.toJS(_.globalObject);
    }
    pub inline fn ipv6(): JSValue {
        return CommonStringsForZig.ipv6Lower.toJS(_.globalObject);
    }
    pub inline fn default(): JSValue {
        return CommonStringsForZig.fetchDefault.toJS(_.globalObject);
    }
    pub inline fn @"error"(): JSValue {
        return CommonStringsForZig.fetchError.toJS(_.globalObject);
    }
    pub inline fn include(): JSValue {
        return CommonStringsForZig.fetchInclude.toJS(_.globalObject);
    }
    pub inline fn buffer(): JSValue {
        return CommonStringsForZig.buffer.toJS(_.globalObject);
    }
    pub inline fn arraybuffer(): JSValue {
        return CommonStringsForZig.binaryTypeArrayBuffer.toJS(_.globalObject);
    }
    pub inline fn nodebuffer(): JSValue {
        return CommonStringsForZig.binaryTypeNodeBuffer.toJS(_.globalObject);
    }
    pub inline fn uint8array(): JSValue {
        return CommonStringsForZig.binaryTypeUint8Array.toJS(_.globalObject);
    }
}

import bun from "bun";

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
