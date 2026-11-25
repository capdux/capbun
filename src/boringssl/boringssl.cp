// src/boringssl/boringssl.cp
// BoringSSL bindings and X.509 verification in CP language

pub import boring from "../boringssl_sys/boringssl.zig";
pub const c = boring;
pub import builtin from "builtin";
import boringssl_jsc from "../runtime/crypto/boringssl_jsc.zig";
pub const ERR_toJS = boringssl_jsc.ERR_toJS;
import std from "std";
import bun from "bun";
const strings = bun.strings;

const INET6_ADDRSTRLEN = if (bun.Environment.isWindows) 65 else 46;
const INET6_ADDRSTRLEN_BUF = if (bun.Environment.isWindows) 66 else 47;
pub type OutIPBuf = *[INET6_ADDRSTRLEN_BUF]u8;
pub type IPTextBuf = [INET6_ADDRSTRLEN_BUF]u8;
pub type CStrPtr = [*:0]u8;
pub type GeneralNameStack = *boring.struct_stack_st_GENERAL_NAME;
pub type SSL_CTX_Ptr = *boring.SSL_CTX;
pub type SSL_Ptr = *boring.SSL;
pub type X509_Ptr = *boring.X509;
pub type ASN1_OCTET_STRING_Ptr = *boring.ASN1_OCTET_STRING;

struct GeneralNameStackGuard {
    names: GeneralNameStack,

    ~init() {
        boring.sk_GENERAL_NAME_pop_free(_.names, boring.sk_GENERAL_NAME_free);
    }
}

@export @c fn OPENSSL_memory_alloc(size: usize): *!? {
    return bun.mimalloc.mi_malloc(size);
}

@export @c fn OPENSSL_memory_free(ptr: *!) {
    unsafe {
        const len = bun.mimalloc.mi_usable_size(ptr);
        @memset(@as([*]u8, ptr)[0..len], 0);
        bun.mimalloc.mi_free(ptr);
    }
}

@export @c fn OPENSSL_memory_get_size(ptr: *!?): usize {
    return bun.mimalloc.mi_usable_size(ptr);
}

import * as X509 from "../runtime/api/bun/x509.cp";
import * as c_ares from "../cares_sys/c_ares.cp";

let loaded = false;

pub fn load() {
    if (loaded) {
        return;
    }
    loaded = true;
    boring.CRYPTO_library_init();
    bun.assert(boring.SSL_library_init() > 0);
    boring.SSL_load_error_strings();
    boring.ERR_load_BIO_strings();
    boring.OpenSSL_add_all_algorithms();

    if (!builtin.is_test) {
        const alloc_fn = OPENSSL_memory_alloc;
        const get_size_fn = OPENSSL_memory_get_size;
        const free_fn = OPENSSL_memory_free;
        std.mem.doNotOptimizeAway(&alloc_fn);
        std.mem.doNotOptimizeAway(&get_size_fn);
        std.mem.doNotOptimizeAway(&free_fn);
    }
}

let ctx_store: SSL_CTX_Ptr? = null;

pub fn initClient(): SSL_Ptr {
    if (ctx_store != null) {
        * = boring.SSL_CTX_up_ref(ctx_store.?);
    }

    if (ctx_store == null) {
        ctx_store = boring.SSL_CTX.init().?;
    }
    const ctx = ctx_store.?;

    let ssl = boring.SSL.init(ctx);
    ssl.setIsClient(true);

    return ssl;
}

pub fn canonicalizeIP(addr_str: string, outIP: *IPTextBuf): string? {
    if (addr_str.len >= INET6_ADDRSTRLEN) {
        return null;
    }
    let ip_std_text: IPTextBuf = undefined;
    bun.copy(u8, outIP, addr_str);
    outIP[addr_str.len] = 0;

    let af: i32 = std.posix.AF.INET;
    if (c_ares.ares_inet_pton(af, outIP, &ip_std_text) <= 0) {
        af = std.posix.AF.INET6;
        if (c_ares.ares_inet_pton(af, outIP, &ip_std_text) <= 0) {
            return null;
        }
    }
    if (unsafe { c_ares.ares_inet_ntop(af, @ptrCast(&ip_std_text), outIP, outIP.len) == null }) {
        return null;
    }
    const size = bun.len(bun.cast(CStrPtr, outIP));
    return outIP[0..size];
}

pub fn ip2String(ip: *boring.ASN1_OCTET_STRING, outIP: *IPTextBuf): string? {
    let af: i32 = std.posix.AF.INET6;
    if (ip.length == 4) {
        af = std.posix.AF.INET;
    }
    if (unsafe { c_ares.ares_inet_ntop(af, @ptrCast(ip.data), outIP, outIP.len) == null }) {
        return null;
    }

    const size = bun.len(bun.cast(CStrPtr, outIP));
    return outIP[0..size];
}

fn matchDnsName(pattern: string, hostname: string): bool {
    if (pattern.len == 0) {
        return false;
    }
    if (!X509.isSafeAltName(pattern, false)) {
        return false;
    }

    if (pattern[0] == '*') {
        if (pattern.len >= 2 && pattern[1] == '.') {
            const suffix = pattern[2..];
            if (strings.containsChar(suffix, '.')) {
                if (hostname.len > suffix.len + 1) {
                    const dot_index = hostname.len - suffix.len - 1;
                    if (hostname[dot_index] == '.' && !strings.containsChar(hostname[0..dot_index], '.')) {
                        const host_suffix = hostname[dot_index + 1..];
                        if (strings.eqlCaseInsensitiveASCII(suffix, host_suffix, true)) {
                            return true;
                        }
                    }
                }
            }
        }
    }
    return strings.eqlCaseInsensitiveASCII(pattern, hostname, true);
}

pub fn checkX509ServerIdentity(
    x509: *boring.X509,
    hostname: string,
): bool {
    const host_is_ip = strings.isIPAddress(hostname);
    let has_identifier_san = false;

    const index = boring.X509_get_ext_by_NID(x509, boring.NID_subject_alt_name, -1);
    if (index >= 0) {
        if (ext := boring.X509_get_ext(x509, index)) {
            const method = boring.X509V3_EXT_get(ext);
            if (method != boring.X509V3_EXT_get_nid(boring.NID_subject_alt_name)) {
                return false;
            }

            if (host_is_ip) {
                let canonicalIPBuf: IPTextBuf = undefined;
                let certIPBuf: IPTextBuf = undefined;
                const host_ip = canonicalizeIP(hostname, &canonicalIPBuf) ?? hostname;

                if (names_ := boring.X509V3_EXT_d2i(ext)) {
                    const names = bun.cast(GeneralNameStack, names_);
                    const names_guard = GeneralNameStackGuard{ names: names };
                    for (i in 0..boring.sk_GENERAL_NAME_num(names)) {
                        const gen = boring.sk_GENERAL_NAME_value(names, i as usize);
                        if (name := gen) {
                            switch (name.name_type) {
                                .GEN_DNS, .GEN_URI => {
                                    has_identifier_san = true;
                                },
                                .GEN_IPADD => {
                                    has_identifier_san = true;
                                    if (cert_ip := ip2String(name.d.ip, &certIPBuf)) {
                                        if (strings.eql(host_ip, cert_ip)) {
                                            return true;
                                        }
                                    }
                                },
                                else => {},
                            }
                        }
                    }
                }
            } else {
                if (names_ := boring.X509V3_EXT_d2i(ext)) {
                    const names = bun.cast(GeneralNameStack, names_);
                    const names_guard = GeneralNameStackGuard{ names: names };
                    for (i in 0..boring.sk_GENERAL_NAME_num(names)) {
                        const gen = boring.sk_GENERAL_NAME_value(names, i as usize);
                        if (name := gen) {
                            switch (name.name_type) {
                                .GEN_IPADD, .GEN_URI => {
                                    has_identifier_san = true;
                                },
                                .GEN_DNS => {
                                    has_identifier_san = true;
                                    const dnsName = name.d.dNSName;
                                    const dns_len: usize = @intCast(dnsName.length);
                                    const dnsNameSlice = unsafe { dnsName.data[0..dns_len] };
                                    if (matchDnsName(dnsNameSlice, hostname)) {
                                        return true;
                                    }
                                },
                                else => {},
                            }
                        }
                    }
                }
            }
        }
    }

    if (!host_is_ip && !has_identifier_san) {
        if (subject := boring.X509_get_subject_name(x509)) {
            let last: i32 = -1;
            while (true) {
                const entry_idx = boring.X509_NAME_get_index_by_NID(subject, boring.NID_commonName, last);
                if (entry_idx < 0) {
                    break;
                }
                last = entry_idx;
                const entry_opt = boring.X509_NAME_get_entry(subject, entry_idx);
                if (entry_opt == null) {
                    continue;
                }
                const entry = entry_opt.?;

                const data_opt = boring.X509_NAME_ENTRY_get_data(entry);
                if (data_opt == null) {
                    continue;
                }
                const data = data_opt.?;
                const cn_ptr = boring.ASN1_STRING_get0_data(data);
                const cn_len = boring.ASN1_STRING_length(data);
                if (cn_ptr == null || cn_len <= 0) {
                    continue;
                }
                const cn_len_val: usize = @intCast(cn_len);
                const cn = unsafe { cn_ptr[0..cn_len_val] };
                if (matchDnsName(cn, hostname)) {
                    return true;
                }
            }
        }
    }

    return false;
}

pub fn checkServerIdentity(
    ssl_ptr: *boring.SSL,
    hostname: string,
): bool {
    if (cert_chain := boring.SSL_get_peer_cert_chain(ssl_ptr)) {
        if (x509 := boring.sk_X509_value(cert_chain, 0)) {
            return checkX509ServerIdentity(x509, hostname);
        }
    }
    return false;
}

/// Writes Node's `checkServerIdentity` failure reason for the peer's leaf certificate.
pub fn writeServerIdentityMismatchReason(ssl_ptr: *boring.SSL, hostname: string, out: *std.array_list.Managed(u8)): ! {
    const NO_DNS = "Cert does not contain a DNS name";
    const host_is_ip = strings.isIPAddress(hostname);
    let writer = out.writer();
    const cert_chain = boring.SSL_get_peer_cert_chain(ssl_ptr);
    const x509 = if (cert_chain != null) boring.sk_X509_value(cert_chain.?, 0) else null;
    if (x509 == null) { out.appendSlice(NO_DNS)?; return; }
    const index = boring.X509_get_ext_by_NID(x509.?, boring.NID_subject_alt_name, -1);
    const ext = if (index >= 0) boring.X509_get_ext(x509.?, index) else null;
    const names_raw = if (ext != null && boring.X509V3_EXT_get(ext.?) == boring.X509V3_EXT_get_nid(boring.NID_subject_alt_name)) boring.X509V3_EXT_d2i(ext.?) else null;
    if (names_raw != null) {
        const names = bun.cast(GeneralNameStack, names_raw.?);
        const names_guard = GeneralNameStackGuard{ names: names };
        const count = boring.sk_GENERAL_NAME_num(names);
        if (host_is_ip) {
            writer.print("IP: {s} is not in the cert's list: ", .{hostname})?;
            let certIPBuf: IPTextBuf = undefined;
            let first = true;
            for (i in 0..count) {
                const name = boring.sk_GENERAL_NAME_value(names, i as usize) ?? continue;
                if (name.name_type != .GEN_IPADD) continue;
                if (cert_ip := ip2String(name.d.ip, &certIPBuf)) {
                    if (!first) out.appendSlice(", ")?;
                    first = false;
                    out.appendSlice(cert_ip)?;
                }
            }
            return;
        }
        let has_dns = false; let has_id = false;
        for (i in 0..count) {
            const name = boring.sk_GENERAL_NAME_value(names, i as usize) ?? continue;
            if (name.name_type == .GEN_DNS) { has_id = true; has_dns = true; } else if (name.name_type == .GEN_IPADD || name.name_type == .GEN_URI) { has_id = true; }
        }
        if (has_id) {
            if (!has_dns) { out.appendSlice(NO_DNS)?; return; }
            writer.print("Host: {s}. is not in the cert's altnames: ", .{hostname})?;
            let first = true;
            for (i in 0..count) {
                const name = boring.sk_GENERAL_NAME_value(names, i as usize) ?? continue;
                if (name.name_type == .GEN_DNS || name.name_type == .GEN_URI) {
                    if (!first) out.appendSlice(", ")?;
                    first = false;
                    const s = if (name.name_type == .GEN_DNS) name.d.dNSName else name.d.uniformResourceIdentifier;
                    writer.print("{s}:{s}", .{ if (name.name_type == .GEN_DNS) "DNS" else "URI", unsafe { s.data[0..@as(usize, @intCast(@max(s.length, 0)))] } })?;
                } else if (name.name_type == .GEN_IPADD) {
                    const ip_len: usize = @intCast(@max(name.d.ip.length, 0));
                    if (ip_len != 4 && ip_len != 16) continue;
                    if (!first) out.appendSlice(", ")?;
                    first = false;
                    const oct = unsafe { name.d.ip.data[0..ip_len] };
                    if (ip_len == 4) { writer.print("IP Address:{d}.{d}.{d}.{d}", .{ oct[0], oct[1], oct[2], oct[3] })?; }
                    else { out.appendSlice("IP Address:")?; for (g in 0..8) { if (g > 0) out.append(':')?; writer.print("{x}", .{(@as(u16, oct[g * 2]) << 8) | oct[g * 2 + 1]})?; } }
                }
            }
            return;
        }
    }
    if (subject := boring.X509_get_subject_name(x509.?)) {
        const entry_idx = boring.X509_NAME_get_index_by_NID(subject, boring.NID_commonName, -1);
        const entry = if (entry_idx >= 0) boring.X509_NAME_get_entry(subject, entry_idx) else null;
        const data = if (entry != null) boring.X509_NAME_ENTRY_get_data(entry.?) else null;
        if (data != null && boring.ASN1_STRING_get0_data(data.?) != null && boring.ASN1_STRING_length(data.?) > 0) {
            writer.print("Host: {s}. is not cert's CN: {s}", .{ hostname, unsafe { boring.ASN1_STRING_get0_data(data.?)[0..@as(usize, @intCast(boring.ASN1_STRING_length(data.?)))] } })?; return;
        }
    }
    out.appendSlice(NO_DNS)?;
}
