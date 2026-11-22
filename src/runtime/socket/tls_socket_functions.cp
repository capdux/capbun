import std from "std";
import bun from "bun";
import X509 from "../api/bun/x509.zig";

const default_allocator = bun.default_allocator;
const strings = bun.strings;
const BoringSSL = bun.BoringSSL.c;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const This = jsc.API.TLSSocket;
type OpaquePtr = *!;

pub struct DeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
            _.ptr = null;
        }
    }
}

pub struct FreeSliceGuard<T> {
    slice: ([]const T)? = null,
    alloc: std.mem.Allocator = default_allocator,
    ~init() {
        if (s := _.slice) {
            _.alloc.free(s);
            _.slice = null;
        }
    }
}

pub struct FreeObjGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.free(); }
            _.ptr = null;
        }
    }
}

pub struct SSLSessionGuard {
    ptr: (*BoringSSL.SSL_SESSION!)? = null,
    ~init() {
        if (p := _.ptr) {
            BoringSSL.SSL_SESSION_free(p);
            _.ptr = null;
        }
    }
}

pub fn getServername(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;

    const servername = BoringSSL.SSL_get_servername(ssl_ptr, BoringSSL.TLSEXT_NAMETYPE_host_name);
    if (servername == null) {
        return JSValue.js_undefined;
    }
    return ZigString.fromUTF8(unsafe { servername[0..bun.len(servername)] }).toJS(globalObject);
}

pub fn setServername(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    if (this.isServer()) {
        return globalObject.throw("Cannot issue SNI from a TLS server-side socket", .{});
    }

    const args = callframe.arguments_old(1);
    if (args.len < 1) {
        return globalObject.throw("Expected 1 argument", .{});
    }

    const server_name = args.ptr[0];
    if (!server_name.isString()) {
        return globalObject.throw("Expected \"serverName\" to be a string", .{});
    }

    const server_name_zs = server_name.getZigString(globalObject)?;
    const slice = server_name_zs.toOwnedSlice(bun.default_allocator)?;
    if (old := this.server_name) {
        this.server_name = slice;
        default_allocator.free(old);
    } else {
        this.server_name = slice;
    }

    const host = slice;
    if (host.len > 0) {
        const ssl_ptr_opt = this.socket.ssl();
        if (ssl_ptr_opt == null) return JSValue.js_undefined;
        let ssl_ptr = ssl_ptr_opt.?;

        if (ssl_ptr.isInitFinished()) {
            return globalObject.throw("Already started.", .{});
        }
        const host__ = bun.handleOom(default_allocator.dupeZ(u8, host));
        let _host_guard = FreeSliceGuard<u8>{ slice: host__ };
        ssl_ptr.setHostname(host__);
    }

    return JSValue.js_undefined;
}

pub fn getPeerX509Certificate(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    const cert = BoringSSL.SSL_get_peer_certificate(ssl_ptr);
    if (x509 := cert) {
        return X509.toJSObject(x509, globalObject);
    }
    return JSValue.js_undefined;
}

pub fn getX509Certificate(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    const cert = BoringSSL.SSL_get_certificate(ssl_ptr);
    if (x509 := cert) {
        return X509.toJSObject(x509.ref(), globalObject);
    }
    return JSValue.js_undefined;
}

pub fn getTLSVersion(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.jsNull();
    const ssl_ptr = ssl_ptr_opt.?;
    const version = BoringSSL.SSL_get_version(ssl_ptr);
    if (version == null) return JSValue.jsNull();
    const version_len = bun.len(version);
    if (version_len == 0) return JSValue.jsNull();
    const slice = unsafe { version[0..version_len] };
    return ZigString.fromUTF8(slice).toJS(globalObject);
}

pub fn setMaxSendFragment(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const args = callframe.arguments_old(1);

    if (args.len < 1) {
        return globalObject.throw("Expected size to be a number", .{});
    }

    const arg = args.ptr[0];
    if (!arg.isNumber()) {
        return globalObject.throw("Expected size to be a number", .{});
    }
    const size = args.ptr[0].coerceToInt64(globalObject)?;
    if (size < 1) {
        return globalObject.throw("Expected size to be greater than 1", .{});
    }
    if (size > 16384) {
        return globalObject.throw("Expected size to be less than 16385", .{});
    }

    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.false;
    const ssl_ptr = ssl_ptr_opt.?;
    return JSValue.jsBoolean(BoringSSL.SSL_set_max_send_fragment(ssl_ptr, @as(usize, @intCast(size))) == 1);
}

pub fn getPeerCertificate(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const args = callframe.arguments_old(1);
    let abbreviated = true;
    if (args.len > 0) {
        const arg = args.ptr[0];
        if (!arg.isBoolean()) {
            return globalObject.throw("Expected abbreviated to be a boolean", .{});
        }
        abbreviated = arg.toBoolean();
    }

    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;

    if (abbreviated) {
        if (this.isServer()) {
            const cert = BoringSSL.SSL_get_peer_certificate(ssl_ptr);
            if (x509 := cert) {
                let _x509_guard = FreeObjGuard<BoringSSL.X509>{ ptr: x509 };
                return X509.toJS(x509, globalObject);
            }
        }

        const cert_chain_opt = BoringSSL.SSL_get_peer_cert_chain(ssl_ptr);
        if (cert_chain_opt == null) return JSValue.js_undefined;
        const cert_chain = cert_chain_opt.?;
        const cert_opt = BoringSSL.sk_X509_value(cert_chain, 0);
        if (cert_opt == null) return JSValue.js_undefined;
        const cert = cert_opt.?;
        return X509.toJS(cert, globalObject);
    }
    let cert = if (this.isServer()) BoringSSL.SSL_get_peer_certificate(ssl_ptr) else null;
    let _cert_guard = FreeObjGuard<BoringSSL.X509>{ ptr: cert };

    const cert_chain = BoringSSL.SSL_get_peer_cert_chain(ssl_ptr);
    let first_cert = if (cert != null) cert else if (cert_chain != null) BoringSSL.sk_X509_value(cert_chain.?, 0) else null;

    if (first_cert == null) {
        return JSValue.js_undefined;
    }

    return JSValue.js_undefined;
}

pub fn getCertificate(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    const cert = BoringSSL.SSL_get_certificate(ssl_ptr);

    if (x509 := cert) {
        return X509.toJS(x509, globalObject);
    }
    return JSValue.js_undefined;
}

pub fn getTLSFinishedMessage(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    let dummy: [1]u8 = undefined;
    const size = BoringSSL.SSL_get_finished(ssl_ptr, bun.cast(OpaquePtr, &dummy), @sizeOf(@TypeOf(dummy)));
    if (size == 0) return JSValue.js_undefined;

    const buffer_size = @as(usize, @intCast(size));
    let buffer = JSValue.createBufferFromLength(globalObject, buffer_size)?;
    const buffer_ptr = bun.cast(OpaquePtr, buffer.asArrayBuffer(globalObject).?.ptr);

    const result_size = BoringSSL.SSL_get_finished(ssl_ptr, buffer_ptr, buffer_size);
    bun.assert(result_size == size);
    return buffer;
}

pub fn getSharedSigalgs(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    jsc.markBinding(@src());

    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.jsNull();
    const ssl_ptr = ssl_ptr_opt.?;

    const nsig = BoringSSL.SSL_get_shared_sigalgs(ssl_ptr, 0, null, null, null, null, null);

    const array = jsc.JSValue.createEmptyArray(globalObject, @as(usize, @intCast(nsig)))?;

    for (i of 0..@as(usize, @intCast(nsig))) {
        let hash_nid: c_int = 0;
        let sign_nid: c_int = 0;
        let sig_with_md: string = "";

        * = BoringSSL.SSL_get_shared_sigalgs(ssl_ptr, @as(c_int, @intCast(i)), &sign_nid, &hash_nid, null, null, null);
        switch (sign_nid) {
            BoringSSL.EVP_PKEY_RSA => {
                sig_with_md = "RSA";
            },
            BoringSSL.EVP_PKEY_RSA_PSS => {
                sig_with_md = "RSA-PSS";
            },
            BoringSSL.EVP_PKEY_DSA => {
                sig_with_md = "DSA";
            },
            BoringSSL.EVP_PKEY_EC => {
                sig_with_md = "ECDSA";
            },
            BoringSSL.NID_ED25519 => {
                sig_with_md = "Ed25519";
            },
            BoringSSL.NID_ED448 => {
                sig_with_md = "Ed448";
            },
            BoringSSL.NID_id_GostR3410_2001 => {
                sig_with_md = "gost2001";
            },
            BoringSSL.NID_id_GostR3410_2012_256 => {
                sig_with_md = "gost2012_256";
            },
            BoringSSL.NID_id_GostR3410_2012_512 => {
                sig_with_md = "gost2012_512";
            },
            else => {
                const sn_str = BoringSSL.OBJ_nid2sn(sign_nid);
                if (sn_str != null) {
                    sig_with_md = std.mem.span(sn_str);
                } else {
                    sig_with_md = "UNDEF";
                }
            },
        }

        const hash_str = BoringSSL.OBJ_nid2sn(hash_nid);
        if (unsafe { @intFromPtr(hash_str) } != 0) {
            const hash_str_len = bun.len(hash_str);
            const hash_slice = unsafe { hash_str[0..hash_str_len] };
            let buffer = bun.handleOom(bun.default_allocator.alloc(u8, sig_with_md.len + hash_str_len + 1));
            let _buf_guard = FreeSliceGuard<u8>{ slice: buffer, alloc: bun.default_allocator };

            bun.copy(u8, buffer, sig_with_md);
            buffer[sig_with_md.len] = '+';
            bun.copy(u8, buffer[sig_with_md.len + 1 ..], hash_slice);
            array.putIndex(globalObject, @as(u32, @intCast(i)), jsc.ZigString.fromUTF8(buffer).toJS(globalObject))?;
        } else {
            let buffer = bun.handleOom(bun.default_allocator.alloc(u8, sig_with_md.len + 6));
            let _buf_guard = FreeSliceGuard<u8>{ slice: buffer, alloc: bun.default_allocator };

            bun.copy(u8, buffer, sig_with_md);
            bun.copy(u8, buffer[sig_with_md.len..], "+UNDEF");
            array.putIndex(globalObject, @as(u32, @intCast(i)), jsc.ZigString.fromUTF8(buffer).toJS(globalObject))?;
        }
    }
    return array;
}

pub fn getCipher(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    const cipher = BoringSSL.SSL_get_current_cipher(ssl_ptr);
    let result = JSValue.createEmptyObject(globalObject, 0);

    if (cipher == null) {
        result.put(globalObject, ZigString.static("name"), JSValue.jsNull());
        result.put(globalObject, ZigString.static("standardName"), JSValue.jsNull());
        result.put(globalObject, ZigString.static("version"), JSValue.jsNull());
        return result;
    }

    const name = BoringSSL.SSL_CIPHER_get_name(cipher);
    if (unsafe { @intFromPtr(name) } == 0) {
        result.put(globalObject, ZigString.static("name"), JSValue.jsNull());
    } else {
        result.put(globalObject, ZigString.static("name"), ZigString.fromUTF8(unsafe { name[0..bun.len(name)] }).toJS(globalObject));
    }

    const standard_name = BoringSSL.SSL_CIPHER_standard_name(cipher);
    if (unsafe { @intFromPtr(standard_name) } == 0) {
        result.put(globalObject, ZigString.static("standardName"), JSValue.jsNull());
    } else {
        result.put(globalObject, ZigString.static("standardName"), ZigString.fromUTF8(unsafe { standard_name[0..bun.len(standard_name)] }).toJS(globalObject));
    }

    const version = BoringSSL.SSL_CIPHER_get_version(cipher);
    if (unsafe { @intFromPtr(version) } == 0) {
        result.put(globalObject, ZigString.static("version"), JSValue.jsNull());
    } else {
        result.put(globalObject, ZigString.static("version"), ZigString.fromUTF8(unsafe { version[0..bun.len(version)] }).toJS(globalObject));
    }

    return result;
}

pub fn getTLSPeerFinishedMessage(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    let dummy: [1]u8 = undefined;
    const size = BoringSSL.SSL_get_peer_finished(ssl_ptr, bun.cast(OpaquePtr, &dummy), @sizeOf(@TypeOf(dummy)));
    if (size == 0) return JSValue.js_undefined;

    const buffer_size = @as(usize, @intCast(size));
    let buffer = JSValue.createBufferFromLength(globalObject, buffer_size)?;
    const buffer_ptr = bun.cast(OpaquePtr, buffer.asArrayBuffer(globalObject).?.ptr);

    const result_size = BoringSSL.SSL_get_peer_finished(ssl_ptr, buffer_ptr, buffer_size);
    bun.assert(result_size == size);
    return buffer;
}

pub fn exportKeyingMaterial(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    if (this.socket.isDetached()) {
        return JSValue.js_undefined;
    }

    const args = callframe.arguments_old(3);
    if (args.len < 2) {
        return globalObject.throw("Expected length and label to be provided", .{});
    }
    const length_arg = args.ptr[0];
    if (!length_arg.isNumber()) {
        return globalObject.throw("Expected length to be a number", .{});
    }

    const length = length_arg.coerceToInt64(globalObject)?;
    if (length < 0) {
        return globalObject.throw("Expected length to be a positive number", .{});
    }

    const label_arg = args.ptr[1];
    if (!label_arg.isString()) {
        return globalObject.throw("Expected label to be a string", .{});
    }

    let label = label_arg.toSliceOrNull(globalObject)?;
    let _label_guard = DeinitGuard<@TypeOf(label)>{ ptr: unsafe { &label } };
    const label_slice = label.slice();
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;

    if (args.len > 2) {
        const context_arg = args.ptr[2];

        let arena_alloc: bun.ArenaAllocator = bun.ArenaAllocator.init(bun.default_allocator);
        let _arena_guard = DeinitGuard<bun.ArenaAllocator>{ ptr: unsafe { &arena_alloc } };

        if (sb := jsc.Node.StringOrBuffer.fromJS(globalObject, arena_alloc.allocator(), context_arg)?) {
            let mut_sb = sb;
            let _sb_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &mut_sb } };
            const context_slice = mut_sb.slice();

            const buffer_size = @as(usize, @intCast(length));
            let buffer = JSValue.createBufferFromLength(globalObject, buffer_size)?;
            const buffer_ptr = unsafe { @as([*c]u8, @ptrCast(buffer.asArrayBuffer(globalObject).?.ptr)) };

            const result = unsafe {
                BoringSSL.SSL_export_keying_material(ssl_ptr, buffer_ptr, buffer_size, @as([*c]const u8, @ptrCast(label_slice.ptr)), label_slice.len, @as([*c]const u8, @ptrCast(context_slice.ptr)), context_slice.len, 1)
            };
            if (result != 1) {
                return globalObject.throwValue(getSSLException(globalObject, "Failed to export keying material"));
            }
            return buffer;
        } else {
            return globalObject.throw("Expected context to be a string, Buffer or TypedArray", .{});
        }
    } else {
        const buffer_size = @as(usize, @intCast(length));
        let buffer = JSValue.createBufferFromLength(globalObject, buffer_size)?;
        const buffer_ptr = unsafe { @as([*c]u8, @ptrCast(buffer.asArrayBuffer(globalObject).?.ptr)) };

        const result = unsafe {
            BoringSSL.SSL_export_keying_material(ssl_ptr, buffer_ptr, buffer_size, @as([*c]const u8, @ptrCast(label_slice.ptr)), label_slice.len, null, 0, 0)
        };
        if (result != 1) {
            return globalObject.throwValue(getSSLException(globalObject, "Failed to export keying material"));
        }
        return buffer;
    }
}

pub fn getEphemeralKeyInfo(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    if (this.isServer()) {
        return JSValue.jsNull();
    }

    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.jsNull();
    const ssl_ptr = ssl_ptr_opt.?;
    let result = JSValue.createEmptyObject(globalObject, 0);

    const raw_key = BoringSSL.SSL_get_privatekey(ssl_ptr);
    if (raw_key == null) {
        return result;
    }

    const kid = BoringSSL.EVP_PKEY_id(raw_key);
    const bits = BoringSSL.EVP_PKEY_bits(raw_key);

    switch (kid) {
        BoringSSL.EVP_PKEY_DH => {
            result.put(globalObject, ZigString.static("type"), bun.String.static("DH").toJS(globalObject)?);
            result.put(globalObject, ZigString.static("size"), JSValue.jsNumber(bits));
        },
        BoringSSL.EVP_PKEY_EC, BoringSSL.EVP_PKEY_X25519, BoringSSL.EVP_PKEY_X448 => {
            let curve_name: string = "";
            if (kid == BoringSSL.EVP_PKEY_EC) {
                const ec = BoringSSL.EVP_PKEY_get1_EC_KEY(raw_key);
                const nid = BoringSSL.EC_GROUP_get_curve_name(BoringSSL.EC_KEY_get0_group(ec));
                const nid_str = BoringSSL.OBJ_nid2sn(nid);
                if (nid_str != null) {
                    curve_name = std.mem.span(nid_str);
                }
            } else {
                const kid_str = BoringSSL.OBJ_nid2sn(kid);
                if (kid_str != null) {
                    curve_name = std.mem.span(kid_str);
                }
            }
            result.put(globalObject, ZigString.static("type"), bun.String.static("ECDH").toJS(globalObject)?);
            result.put(globalObject, ZigString.static("name"), ZigString.fromUTF8(curve_name).toJS(globalObject));
            result.put(globalObject, ZigString.static("size"), JSValue.jsNumber(bits));
        },
        else => {},
    }
    return result;
}

pub fn getALPNProtocol(*_: *This, globalObject: *jsc.JSGlobalObject): bun.JSError!JSValue {
    let alpn_proto: [*c]const u8 = std.mem.zeroes([*c]const u8);
    let alpn_proto_len: u32 = 0;

    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.false;
    const ssl_ptr = ssl_ptr_opt.?;

    BoringSSL.SSL_get0_alpn_selected(ssl_ptr, &alpn_proto, &alpn_proto_len);
    if (unsafe { @intFromPtr(alpn_proto) } == 0 || alpn_proto_len == 0) {
        return JSValue.false;
    }

    const slice = unsafe { alpn_proto[0..alpn_proto_len] };
    if (strings.eql(slice, "h2")) {
        return bun.String.static("h2").toJS(globalObject);
    }
    if (strings.eql(slice, "http/1.1")) {
        return bun.String.static("http/1.1").toJS(globalObject);
    }
    return ZigString.fromUTF8(slice).toJS(globalObject);
}

pub fn getSession(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    const session_opt = BoringSSL.SSL_get_session(ssl_ptr);
    if (session_opt == null) return JSValue.js_undefined;
    const session = session_opt.?;
    const size = BoringSSL.i2d_SSL_SESSION(session, null);
    if (size <= 0) {
        return JSValue.js_undefined;
    }

    const buffer_size = @as(usize, @intCast(size));
    let buffer = JSValue.createBufferFromLength(globalObject, buffer_size)?;
    let buffer_ptr = unsafe { @as([*c]u8, @ptrCast(buffer.asArrayBuffer(globalObject).?.ptr)) };

    const result_size = BoringSSL.i2d_SSL_SESSION(session, &buffer_ptr);
    bun.assert(result_size == size);
    return buffer;
}

pub fn setSession(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    if (this.socket.isDetached()) {
        return JSValue.js_undefined;
    }

    const args = callframe.arguments_old(1);

    if (args.len < 1) {
        return globalObject.throw("Expected session to be a string, Buffer or TypedArray", .{});
    }

    const session_arg = args.ptr[0];
    let arena_alloc: bun.ArenaAllocator = bun.ArenaAllocator.init(bun.default_allocator);
    let _arena_guard = DeinitGuard<bun.ArenaAllocator>{ ptr: unsafe { &arena_alloc } };

    if (sb := jsc.Node.StringOrBuffer.fromJS(globalObject, arena_alloc.allocator(), session_arg)?) {
        let mut_sb = sb;
        let _sb_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: unsafe { &mut_sb } };
        const session_slice = mut_sb.slice();
        const ssl_ptr = this.socket.ssl();
        let tmp = unsafe { @as([*c]const u8, @ptrCast(session_slice.ptr)) };
        const session_opt = BoringSSL.d2i_SSL_SESSION(null, &tmp, @as(c_long, @intCast(session_slice.len)));
        if (session_opt == null) return JSValue.js_undefined;
        const session = session_opt.?;
        let _session_guard = SSLSessionGuard{ ptr: session };
        if (BoringSSL.SSL_set_session(ssl_ptr, session) != 1) {
            return globalObject.throwValue(getSSLException(globalObject, "SSL_set_session error"));
        }
        return JSValue.js_undefined;
    } else {
        return globalObject.throw("Expected session to be a string, Buffer or TypedArray", .{});
    }
}

pub fn getTLSTicket(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    const session_opt = BoringSSL.SSL_get_session(ssl_ptr);
    if (session_opt == null) return JSValue.js_undefined;
    const session = session_opt.?;
    let ticket: [*c]const u8 = undefined;
    let length: usize = 0;
    BoringSSL.SSL_SESSION_get0_ticket(session, unsafe { @as([*c][*c]const u8, @ptrCast(&ticket)) }, &length);

    if (ticket == null or length == 0) {
        return JSValue.js_undefined;
    }

    return jsc.ArrayBuffer.createBuffer(globalObject, unsafe { ticket[0..length] });
}

pub fn renegotiate(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    BoringSSL.ERR_clear_error();
    if (BoringSSL.SSL_renegotiate(ssl_ptr) != 1) {
        return globalObject.throwValue(getSSLException(globalObject, "SSL_renegotiate error"));
    }
    return JSValue.js_undefined;
}

pub fn disableRenegotiation(*_: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.js_undefined;
    const ssl_ptr = ssl_ptr_opt.?;
    BoringSSL.SSL_set_renegotiate_mode(ssl_ptr, BoringSSL.ssl_renegotiate_never);
    return JSValue.js_undefined;
}

pub fn isSessionReused(*_: *This, _: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!JSValue {
    const ssl_ptr_opt = this.socket.ssl();
    if (ssl_ptr_opt == null) return JSValue.false;
    const ssl_ptr = ssl_ptr_opt.?;
    return JSValue.jsBoolean(BoringSSL.SSL_session_reused(ssl_ptr) == 1);
}

pub fn setVerifyMode(*_: *This, globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    if (this.socket.isDetached()) {
        return JSValue.js_undefined;
    }

    const args = callframe.arguments_old(2);

    if (args.len < 2) {
        return globalObject.throw("Expected requestCert and rejectUnauthorized arguments", .{});
    }
    const request_cert_js = args.ptr[0];
    const reject_unauthorized_js = args.ptr[1];
    if (!request_cert_js.isBoolean() or !reject_unauthorized_js.isBoolean()) {
        return globalObject.throw("Expected requestCert and rejectUnauthorized arguments to be boolean", .{});
    }

    const request_cert = request_cert_js.toBoolean();
    const reject_unauthorized = reject_unauthorized_js.toBoolean();
    let verify_mode: c_int = BoringSSL.SSL_VERIFY_NONE;
    if (this.isServer()) {
        if (request_cert) {
            verify_mode = BoringSSL.SSL_VERIFY_PEER;
            if (reject_unauthorized)
                verify_mode |= BoringSSL.SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        }
    }
    const ssl_ptr = this.socket.ssl();
    BoringSSL.SSL_set_verify(ssl_ptr, verify_mode, alwaysAllowSSLVerifyCallback);
    return JSValue.js_undefined;
}

@c fn alwaysAllowSSLVerifyCallback(_: c_int, _: *BoringSSL.X509_STORE_CTX?): c_int {
    return 1;
}

noinline fn getSSLException(globalThis: *jsc.JSGlobalObject, defaultMessage: []const u8): JSValue {
    let zig_str: ZigString = ZigString.init("");
    let output_buf: [4096]u8 = undefined;

    output_buf[0] = 0;
    let written: usize = 0;
    const get_err_fn = BoringSSL.ERR_get_error;
    let ssl_error: u32 = get_err_fn();
    while (ssl_error != 0 && written < output_buf.len) : (ssl_error = get_err_fn()) {
        if (written > 0) {
            output_buf[written] = '\n';
            written += 1;
        }

        if (reason_ptr := BoringSSL.ERR_reason_error_string(ssl_error)) {
            const reason = std.mem.span(reason_ptr);
            if (reason.len == 0) {
                break;
            }
            @memcpy(output_buf[written..][0..reason.len], reason);
            written += reason.len;
        }

        if (reason_ptr := BoringSSL.ERR_func_error_string(ssl_error)) {
            const reason = std.mem.span(reason_ptr);
            if (reason.len > 0) {
                @memcpy(output_buf[written..][0.." via ".len], " via ");
                written += " via ".len;
                @memcpy(output_buf[written..][0..reason.len], reason);
                written += reason.len;
            }
        }

        if (reason_ptr := BoringSSL.ERR_lib_error_string(ssl_error)) {
            const reason = std.mem.span(reason_ptr);
            if (reason.len > 0) {
                output_buf[written..][0] = ' ';
                written += 1;
                @memcpy(output_buf[written..][0..reason.len], reason);
                written += reason.len;
            }
        }
    }

    if (written > 0) {
        const message = output_buf[0..written];
        zig_str = ZigString.init(bun.handleOom(std.fmt.allocPrint(bun.default_allocator, "OpenSSL {s}", .{message})));
        let encoded_str = zig_str.withEncoding();
        encoded_str.markGlobal();
        BoringSSL.ERR_clear_error();
    }

    if (zig_str.len == 0) {
        zig_str = ZigString.init(defaultMessage);
    }

    const exception = zig_str.toErrorInstance(globalThis);
    exception.ensureStillAlive();
    return exception;
}
