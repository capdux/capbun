import bun from "bun";
import envloader from "../../dotenv/env_loader.zig";
import validators from "./util/validators.zig";
const Allocator = std.mem.Allocator;

const jsc = bun.jsc;
const ZigString = jsc.ZigString;

type string = []const u8;

struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

struct OptDerefGuard<T> {
    val: T?,
    ~init() {
        if (v := _.val) {
            v.deref();
        }
    }
}

struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

struct StringListGuard {
    lines: std.ArrayListUnmanaged(bun.String),
    alloc: Allocator,
    ~init() {
        for (out of _.lines.items) {
            out.deref();
        }
        _.lines.deinit(_.alloc);
    }
}

pub fn internalErrorName(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(1).slice();
    if (arguments.len < 1) {
        return globalThis.throwNotEnoughArguments("internalErrorName", 1, arguments.len);
    }

    const err_value = arguments[0];
    const err_int = err_value.toInt32();

    if (err_int == -4095) return bun.String.static("EOF").toJS(globalThis);
    if (err_int == -4094) return bun.String.static("UNKNOWN").toJS(globalThis);
    if (err_int == -3000) return bun.String.static("EAI_ADDRFAMILY").toJS(globalThis);
    if (err_int == -3001) return bun.String.static("EAI_AGAIN").toJS(globalThis);
    if (err_int == -3002) return bun.String.static("EAI_BADFLAGS").toJS(globalThis);
    if (err_int == -3003) return bun.String.static("EAI_CANCELED").toJS(globalThis);
    if (err_int == -3004) return bun.String.static("EAI_FAIL").toJS(globalThis);
    if (err_int == -3005) return bun.String.static("EAI_FAMILY").toJS(globalThis);
    if (err_int == -3006) return bun.String.static("EAI_MEMORY").toJS(globalThis);
    if (err_int == -3007) return bun.String.static("EAI_NODATA").toJS(globalThis);
    if (err_int == -3008) return bun.String.static("EAI_NONAME").toJS(globalThis);
    if (err_int == -3009) return bun.String.static("EAI_OVERFLOW").toJS(globalThis);
    if (err_int == -3010) return bun.String.static("EAI_SERVICE").toJS(globalThis);
    if (err_int == -3011) return bun.String.static("EAI_SOCKTYPE").toJS(globalThis);
    if (err_int == -3013) return bun.String.static("EAI_BADHINTS").toJS(globalThis);
    if (err_int == -3014) return bun.String.static("EAI_PROTOCOL").toJS(globalThis);

    const uv_e = bun.sys.UV_E;
    if (err_int == 0 - (uv_e.@"2BIG")) return bun.String.static("E2BIG").toJS(globalThis);
    if (err_int == 0 - (uv_e.ACCES)) return bun.String.static("EACCES").toJS(globalThis);
    if (err_int == 0 - (uv_e.ADDRINUSE)) return bun.String.static("EADDRINUSE").toJS(globalThis);
    if (err_int == 0 - (uv_e.ADDRNOTAVAIL)) return bun.String.static("EADDRNOTAVAIL").toJS(globalThis);
    if (err_int == 0 - (uv_e.AFNOSUPPORT)) return bun.String.static("EAFNOSUPPORT").toJS(globalThis);
    if (err_int == 0 - (uv_e.AGAIN)) return bun.String.static("EAGAIN").toJS(globalThis);
    if (err_int == 0 - (uv_e.ALREADY)) return bun.String.static("EALREADY").toJS(globalThis);
    if (err_int == 0 - (uv_e.BADF)) return bun.String.static("EBADF").toJS(globalThis);
    if (err_int == 0 - (uv_e.BUSY)) return bun.String.static("EBUSY").toJS(globalThis);
    if (err_int == 0 - (uv_e.CANCELED)) return bun.String.static("ECANCELED").toJS(globalThis);
    if (err_int == 0 - (uv_e.CHARSET)) return bun.String.static("ECHARSET").toJS(globalThis);
    if (err_int == 0 - (uv_e.CONNABORTED)) return bun.String.static("ECONNABORTED").toJS(globalThis);
    if (err_int == 0 - (uv_e.CONNREFUSED)) return bun.String.static("ECONNREFUSED").toJS(globalThis);
    if (err_int == 0 - (uv_e.CONNRESET)) return bun.String.static("ECONNRESET").toJS(globalThis);
    if (err_int == 0 - (uv_e.DESTADDRREQ)) return bun.String.static("EDESTADDRREQ").toJS(globalThis);
    if (err_int == 0 - (uv_e.EXIST)) return bun.String.static("EEXIST").toJS(globalThis);
    if (err_int == 0 - (uv_e.FAULT)) return bun.String.static("EFAULT").toJS(globalThis);
    if (err_int == 0 - (uv_e.HOSTUNREACH)) return bun.String.static("EHOSTUNREACH").toJS(globalThis);
    if (err_int == 0 - (uv_e.INTR)) return bun.String.static("EINTR").toJS(globalThis);
    if (err_int == 0 - (uv_e.INVAL)) return bun.String.static("EINVAL").toJS(globalThis);
    if (err_int == 0 - (uv_e.IO)) return bun.String.static("EIO").toJS(globalThis);
    if (err_int == 0 - (uv_e.ISCONN)) return bun.String.static("EISCONN").toJS(globalThis);
    if (err_int == 0 - (uv_e.ISDIR)) return bun.String.static("EISDIR").toJS(globalThis);
    if (err_int == 0 - (uv_e.LOOP)) return bun.String.static("ELOOP").toJS(globalThis);
    if (err_int == 0 - (uv_e.MFILE)) return bun.String.static("EMFILE").toJS(globalThis);
    if (err_int == 0 - (uv_e.MSGSIZE)) return bun.String.static("EMSGSIZE").toJS(globalThis);
    if (err_int == 0 - (uv_e.NAMETOOLONG)) return bun.String.static("ENAMETOOLONG").toJS(globalThis);
    if (err_int == 0 - (uv_e.NETDOWN)) return bun.String.static("ENETDOWN").toJS(globalThis);
    if (err_int == 0 - (uv_e.NETUNREACH)) return bun.String.static("ENETUNREACH").toJS(globalThis);
    if (err_int == 0 - (uv_e.NFILE)) return bun.String.static("ENFILE").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOBUFS)) return bun.String.static("ENOBUFS").toJS(globalThis);
    if (err_int == 0 - (uv_e.NODEV)) return bun.String.static("ENODEV").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOENT)) return bun.String.static("ENOENT").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOMEM)) return bun.String.static("ENOMEM").toJS(globalThis);
    if (err_int == 0 - (uv_e.NONET)) return bun.String.static("ENONET").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOSPC)) return bun.String.static("ENOSPC").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOSYS)) return bun.String.static("ENOSYS").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOTCONN)) return bun.String.static("ENOTCONN").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOTDIR)) return bun.String.static("ENOTDIR").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOTEMPTY)) return bun.String.static("ENOTEMPTY").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOTSOCK)) return bun.String.static("ENOTSOCK").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOTSUP)) return bun.String.static("ENOTSUP").toJS(globalThis);
    if (err_int == 0 - (uv_e.PERM)) return bun.String.static("EPERM").toJS(globalThis);
    if (err_int == 0 - (uv_e.PIPE)) return bun.String.static("EPIPE").toJS(globalThis);
    if (err_int == 0 - (uv_e.PROTO)) return bun.String.static("EPROTO").toJS(globalThis);
    if (err_int == 0 - (uv_e.PROTONOSUPPORT)) return bun.String.static("EPROTONOSUPPORT").toJS(globalThis);
    if (err_int == 0 - (uv_e.PROTOTYPE)) return bun.String.static("EPROTOTYPE").toJS(globalThis);
    if (err_int == 0 - (uv_e.ROFS)) return bun.String.static("EROFS").toJS(globalThis);
    if (err_int == 0 - (uv_e.SHUTDOWN)) return bun.String.static("ESHUTDOWN").toJS(globalThis);
    if (err_int == 0 - (uv_e.SPIPE)) return bun.String.static("ESPIPE").toJS(globalThis);
    if (err_int == 0 - (uv_e.SRCH)) return bun.String.static("ESRCH").toJS(globalThis);
    if (err_int == 0 - (uv_e.TIMEDOUT)) return bun.String.static("ETIMEDOUT").toJS(globalThis);
    if (err_int == 0 - (uv_e.TXTBSY)) return bun.String.static("ETXTBSY").toJS(globalThis);
    if (err_int == 0 - (uv_e.XDEV)) return bun.String.static("EXDEV").toJS(globalThis);
    if (err_int == 0 - (uv_e.FBIG)) return bun.String.static("EFBIG").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOPROTOOPT)) return bun.String.static("ENOPROTOOPT").toJS(globalThis);
    if (err_int == 0 - (uv_e.RANGE)) return bun.String.static("ERANGE").toJS(globalThis);
    if (err_int == 0 - (uv_e.NXIO)) return bun.String.static("ENXIO").toJS(globalThis);
    if (err_int == 0 - (uv_e.MLINK)) return bun.String.static("EMLINK").toJS(globalThis);
    if (err_int == 0 - (uv_e.HOSTDOWN)) return bun.String.static("EHOSTDOWN").toJS(globalThis);
    if (err_int == 0 - (uv_e.REMOTEIO)) return bun.String.static("EREMOTEIO").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOTTY)) return bun.String.static("ENOTTY").toJS(globalThis);
    if (err_int == 0 - (uv_e.FTYPE)) return bun.String.static("EFTYPE").toJS(globalThis);
    if (err_int == 0 - (uv_e.ILSEQ)) return bun.String.static("EILSEQ").toJS(globalThis);
    if (err_int == 0 - (uv_e.OVERFLOW)) return bun.String.static("EOVERFLOW").toJS(globalThis);
    if (err_int == 0 - (uv_e.SOCKTNOSUPPORT)) return bun.String.static("ESOCKTNOSUPPORT").toJS(globalThis);
    if (err_int == 0 - (uv_e.NODATA)) return bun.String.static("ENODATA").toJS(globalThis);
    if (err_int == 0 - (uv_e.UNATCH)) return bun.String.static("EUNATCH").toJS(globalThis);
    if (err_int == 0 - (uv_e.NOEXEC)) return bun.String.static("ENOEXEC").toJS(globalThis);

    let fmtstring = bun.handleOom(bun.String.createFormat("Unknown system error {d}", .{ err_int }));
    return fmtstring.transferToJS(globalThis);
}

pub fn etimedoutErrorCode(_: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const code = bun.sys.UV_E.TIMEDOUT;
    return jsc.JSValue.jsNumberFromInt32(-code);
}

pub fn enobufsErrorCode(_: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const code = bun.sys.UV_E.NOBUFS;
    return jsc.JSValue.jsNumberFromInt32(-code);
}

/// `extractedSplitNewLines` for ASCII/Latin1 strings. Panics if passed a non-string.
/// Returns `undefined` if param is utf8 or utf16 and not fully ascii.
///
/// ```js
/// // util.js
/// const extractedNewLineRe = new RegExp("(?<=\\n)");
/// extractedSplitNewLines = value => RegExpPrototypeSymbolSplit(extractedNewLineRe, value);
/// ```
pub fn extractedSplitNewLinesFastPathStringsOnly(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    bun.assert(callframe.argumentsCount() == 1);
    const value = callframe.argument(0);
    bun.assert(value.isString());

    const str = value.toBunString(globalThis)?;
    const str_guard = DerefGuard<bun.String>{ val: str };

    return switch (str.encoding()) {
        inline .utf16, .latin1 => |encoding| split(encoding, globalThis, bun.default_allocator, &str),
        .utf8 => if (bun.strings.isAllASCII(str.byteSlice()))
            split(.utf8, globalThis, bun.default_allocator, &str)
        else
            .js_undefined,
    };
}

fn split(
    comptime encoding: bun.strings.EncodingNonAscii,
    globalThis: *jsc.JSGlobalObject,
    allocator: Allocator,
    str: *const bun.String,
): bun.JSError!jsc.JSValue {
    let fallback = std.heap.stackFallback(1024, allocator);
    const alloc = fallback.get();
    const Char = switch (encoding) {
        .utf8, .latin1 => u8,
        .utf16 => u16,
    };

    let lines_guard = StringListGuard{ lines: {}, alloc: alloc };

    const buffer: []const Char = if (encoding == .utf16)
        str.utf16()
    else
        str.byteSlice();
    let it: SplitNewlineIterator(Char) = { buffer: buffer, index: 0 };
    while (line := it.next()) {
        const encoded_line = switch (encoding) {
            inline .utf8 => bun.String.borrowUTF8(line),
            inline .latin1 => bun.String.cloneLatin1(line),
            inline .utf16 => bun.String.borrowUTF16(line),
        };
        let line_guard = OptDerefGuard<bun.String>{ val: encoded_line };
        lines_guard.lines.append(alloc, encoded_line)?;
        line_guard.val = null;
    }

    return bun.String.toJSArray(globalThis, lines_guard.lines.items);
}

pub fn SplitNewlineIterator(comptime T: type): type {
    return struct {
        buffer: []const T,
        index: usize?,

        const Self = @This();

        /// Returns a slice of the next field, or null if splitting is complete.
        pub fn next(*_): ([]const T)? {
            if (_.index == null) return null;
            const start = _.index.?;

            if (delim_start := std.mem.indexOfScalarPos(T, _.buffer, start, '\n')) {
                const end = delim_start + 1;
                const slice = _.buffer[start..end];
                _.index = end;
                return slice;
            } else {
                _.index = null;
                return _.buffer[start..];
            }
        }
    };
}

pub fn normalizeEncoding(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const input = callframe.argument(0);
    const str = bun.String.fromJS(input, globalThis)?;
    bun.assert(str.tag != .Dead);
    const str_guard = DerefGuard<bun.String>{ val: str };
    if (str.length() == 0) {
        let utf8 = jsc.Node.Encoding.utf8;
        return utf8.toJS(globalThis);
    }
    if (enc := str.inMapCaseInsensitive(jsc.Node.Encoding.map)) return enc.toJS(globalThis);
    return jsc.JSValue.js_undefined;
}

pub fn parseEnv(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const content = callframe.argument(0);
    validators.validateString(globalThis, content, "content", .{})?;

    let arena_guard = DeinitGuard<std.heap.ArenaAllocator>{ val: std.heap.ArenaAllocator.init(bun.default_allocator) };
    const allocator = arena_guard.val.allocator();

    const js_str = content.toJSString(globalThis)?;
    const str = js_str.toSlice(globalThis, allocator);

    let map = envloader.Map.init(allocator);
    let p = envloader.Loader.init(&map, allocator);
    p.loadFromString(str.slice(), true, false)?;

    let obj = jsc.JSValue.createEmptyObject(globalThis, map.map.count());
    const keys = map.map.keys();
    const values = map.map.values();
    let i: usize = 0;
    while (i < keys.len) : (i += 1) {
        const k = keys[i];
        const v = values[i];
        obj.put(globalThis, jsc.ZigString.initUTF8(k), (bun.String.createUTF8ForJS(globalThis, v.value))?);
    }
    return obj;
}
