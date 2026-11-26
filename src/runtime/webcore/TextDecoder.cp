import {
    default_allocator,
    destroy,
    TrivialNew,
    assert,
    assertWithLocation,
    debugAssert,
    strings,
    JSError,
    jsc,
} from "bun";
import { meta } from "std";
import { TextCodec } from "../../jsc/TextCodec.zig";

const ArrayListUnmanaged = std.ArrayListUnmanaged;

const ArrayBuffer = jsc.ArrayBuffer;
const JSGlobalObject = jsc.JSGlobalObject;
const JSUint8Array = jsc.JSUint8Array;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;
const EncodingLabel = jsc.WebCore.EncodingLabel;

const Vector16 = meta.Vector(16, u16);
const max_16_ascii: Vector16 = @splat(@as(u16, 127));
type OutputList = ArrayListUnmanaged(u16);
pub type DecodeUTF16Result = [OutputList, bool];

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe {
            _.ptr.deinit();
        }
    }
}

pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe {
            _.ptr.deref();
        }
    }
}

pub struct Buffered {
    buf: [3]u8 = .{ 0, 0, 0 },
    len: u2 = 0,

    fn slice(): []const u8 {
        return _.buf[0.._.len];
    }
}

@root struct TextDecoder {
    buffered: Buffered = .{},

    lead_byte: u8? = null,
    lead_surrogate: u16? = null,

    ignore_bom: bool = false,
    fatal: bool = false,
    bom_seen: bool = false,
    encoding: EncodingLabel = EncodingLabel.@"UTF-8",

    pub const js = jsc.Codegen.JSTextDecoder;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    pub const @"new" = TrivialNew(TextDecoder);

    fn finalize(*_) {
        destroy(_);
    }

    fn getIgnoreBOM(
        *_,
        globalThis: *JSGlobalObject,
    ): JSValue {
        * = globalThis;
        return JSValue.jsBoolean(_.ignore_bom);
    }

    fn getFatal(
        *_,
        globalThis: *JSGlobalObject,
    ): JSValue {
        * = globalThis;
        return JSValue.jsBoolean(_.fatal);
    }

    fn getEncoding(
        *_,
        globalThis: *JSGlobalObject,
    ): JSValue {
        return ZigString.init(EncodingLabel.getLabel(_.encoding)).toJS(globalThis);
    }

    fn processCodeUnitUTF16(
        *_,
        output: *ArrayListUnmanaged(u16),
        saw_error: *bool,
        code_unit: u16,
    )! {
        if (lead_surrogate := _.lead_surrogate) {
            _.lead_surrogate = null;

            if (strings.u16IsTrail(code_unit)) {
                output.appendSlice(
                    default_allocator,
                    &.{ lead_surrogate, code_unit },
                )?;
                return;
            }
            output.append(default_allocator, strings.unicode_replacement)?;
            saw_error.* = true;
        }

        if (strings.u16IsLead(code_unit)) {
            _.lead_surrogate = code_unit;
            return;
        }

        if (strings.u16IsTrail(code_unit)) {
            output.append(default_allocator, strings.unicode_replacement)?;
            saw_error.* = true;
            return;
        }

        output.append(default_allocator, code_unit)?;
        return;
    }

    static fn codeUnitFromBytesUTF16(
        first: u16,
        second: u16,
        comptime big_endian: bool,
    ): u16 {
        return if (big_endian)
            (first << 8) | second
        else
            first | (second << 8);
    }

    fn decodeUTF16(
        *_,
        bytes: []const u8,
        comptime big_endian: bool,
        comptime flush: bool,
    ): DecodeUTF16Result! {
        let output: ArrayListUnmanaged(u16) = .{};
        errdefer output.deinit(default_allocator);
        output.ensureTotalCapacity(default_allocator, @divFloor(bytes.len, 2))?;

        let remain = bytes;
        let saw_error = false;

        if (lead_byte := _.lead_byte) {
            if (remain.len > 0) {
                _.lead_byte = null;

                _.processCodeUnitUTF16(
                    &output,
                    &saw_error,
                    codeUnitFromBytesUTF16(@intCast(lead_byte), @intCast(remain[0]), big_endian),
                )?;
                remain = remain[1..];
            }
        }

        let i: usize = 0;

        while (i + 1 < remain.len) {
            _.processCodeUnitUTF16(
                &output,
                &saw_error,
                codeUnitFromBytesUTF16(@intCast(remain[i]), @intCast(remain[i + 1]), big_endian),
            )?;
            i += 2;
        }

        if (remain.len != 0 && i == remain.len - 1) {
            _.lead_byte = remain[i];
        } else {
            assertWithLocation(i == remain.len, @src());
        }

        if (flush) {
            if (_.lead_byte != null || _.lead_surrogate != null) {
                _.lead_byte = null;
                _.lead_surrogate = null;
                output.append(default_allocator, strings.unicode_replacement)?;
                saw_error = true;
                return .{ output, saw_error };
            }
        }

        return .{ output, saw_error };
    }

    fn decode(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSValue ! JSError {
        let arguments = callframe.arguments_old(2).slice();

        let stream = false;
        if (arguments.len > 1 && !arguments[1].isUndefinedOrNull()) {
            if (!arguments[1].isObject()) {
                return globalThis.throwInvalidArgumentTypeValue("options", "object", arguments[1]);
            }
            if (stream_value := arguments[1].fastGet(globalThis, .stream)?) {
                stream = stream_value.toBoolean();
            }
        }

        let input_slice: []const u8 = "";
        if (arguments.len > 0 && !arguments[0].isUndefined()) {
            if (array_buffer := arguments[0].asArrayBuffer(globalThis)) {
                input_slice = array_buffer.slice();
            } else {
                return globalThis.throwInvalidArguments("TextDecoder.decode expects an ArrayBuffer or TypedArray", .{});
            }
        }

        if (!stream) {
            return _.decodeSlice(globalThis, input_slice, true);
        } else {
            return _.decodeSlice(globalThis, input_slice, false);
        }
    }

    fn decodeWithoutTypeChecks(*_, globalThis: *JSGlobalObject, uint8array: *jsc.JSUint8Array): JSValue ! JSError {
        return _.decodeSlice(globalThis, uint8array.slice(), false);
    }

    fn stripUTF16BOM(*_, buffer_slice: []const u8, comptime b0: u8, comptime b1: u8): []const u8 {
        if (_.ignore_bom || _.bom_seen) return buffer_slice;
        if (lead := _.lead_byte) {
            if (buffer_slice.len > 0) {
                _.bom_seen = true;
                if (lead == b0 && buffer_slice[0] == b1) {
                    _.lead_byte = null;
                    return buffer_slice[1..];
                }
            }
        } else if (buffer_slice.len >= 2) {
            _.bom_seen = true;
            if (buffer_slice[0] == b0 && buffer_slice[1] == b1) return buffer_slice[2..];
        } else if (buffer_slice.len == 1 && buffer_slice[0] != b0) {
            _.bom_seen = true;
        }
        return buffer_slice;
    }

    fn decodeSlice(*_, globalThis: *JSGlobalObject, buffer_slice: []const u8, comptime flush: bool): JSValue ! JSError {
        switch (_.encoding) {
            EncodingLabel.latin1 => {
                if (strings.isAllASCII(buffer_slice)) {
                    return ZigString.init(buffer_slice).toJS(globalThis);
                }

                let out_length = strings.elementLengthCP1252IntoUTF16(buffer_slice);
                let bytes = default_allocator.alloc(u16, out_length)?;

                let out = strings.copyCP1252IntoUTF16(bytes, buffer_slice);
                return ZigString.toExternalU16(bytes.ptr, out.written, globalThis);
            },
            EncodingLabel.@"UTF-8" => {
                let input = buffer_slice;
                let alloc_buf: ([]u8)? = null;
                if (_.buffered.len > 0) {
                    defer _.buffered.len = 0;
                    let joined = default_allocator.alloc(u8, buffer_slice.len + _.buffered.len)?;
                    @memcpy(joined[0.._.buffered.len], _.buffered.slice());
                    @memcpy(joined[_.buffered.len..][0..buffer_slice.len], buffer_slice);
                    input = joined;
                    alloc_buf = joined;
                }

                if (!_.ignore_bom && !_.bom_seen) {
                    if (strings.hasPrefixComptime(input, "\xef\xbb\xbf")) {
                        input = input[3..];
                        _.bom_seen = true;
                    } else if (input.len >= 3 || (input.len > 0 && !(input[0] == 0xef && (input.len == 1 || input[1] == 0xbb)))) {
                        _.bom_seen = true;
                    }
                }

                let maybe_decode_result = if (_.fatal) {
                    strings.toUTF16AllocMaybeBuffered(default_allocator, input, true, flush) catch e => {
                        if (buf := alloc_buf) default_allocator.free(buf);
                        if (flush) _.bom_seen = false;
                        if (e == error.InvalidByteSequence) {
                            return globalThis.ERR(.ENCODING_INVALID_ENCODED_DATA, "Invalid byte sequence", .{}).throw();
                        }

                        assert(e == error.OutOfMemory);
                        return globalThis.throwOutOfMemory();
                    }
                } else {
                    strings.toUTF16AllocMaybeBuffered(default_allocator, input, false, flush) catch e => {
                        if (buf := alloc_buf) default_allocator.free(buf);
                        assert(e == error.OutOfMemory);
                        return globalThis.throwOutOfMemory();
                    }
                };

                if (decode_result := maybe_decode_result) {
                    if (buf := alloc_buf) default_allocator.free(buf);
                    const decoded = decode_result[0];
                    const leftover = decode_result[1];
                    const leftover_len = decode_result[2];
                    assert(_.buffered.len == 0);
                    if (!flush) {
                        if (leftover_len != 0) {
                            _.buffered.buf = leftover;
                            _.buffered.len = leftover_len;
                        }
                    } else {
                        _.bom_seen = false;
                    }
                    return ZigString.toExternalU16(decoded.ptr, decoded.len, globalThis);
                }

                if (flush) _.bom_seen = false;
                if (buf := alloc_buf) {
                    const out_str = ZigString.init(input).toJS(globalThis);
                    default_allocator.free(buf);
                    return out_str;
                }
                return ZigString.init(input).toJS(globalThis);
            },

            .@"UTF-16LE" => {
                const input = _.stripUTF16BOM(buffer_slice, 0xff, 0xfe);

                const res_le = _.decodeUTF16(input, false, flush)?;
                let decoded = res_le[0];
                const saw_error = res_le[1];

                if (saw_error && _.fatal) {
                    decoded.deinit(default_allocator);
                    if (flush) _.bom_seen = false;
                    return globalThis.ERR(.ENCODING_INVALID_ENCODED_DATA, "The encoded data was not valid UTF-16LE data", .{}).throw();
                }

                if (flush) _.bom_seen = false;
                if (decoded.items.len == 0) {
                    decoded.deinit(default_allocator);
                    return ZigString.Empty.toJS(globalThis);
                }

                return ZigString.toExternalU16(decoded.items.ptr, decoded.items.len, globalThis);
            },
            .@"UTF-16BE" => {
                const input = _.stripUTF16BOM(buffer_slice, 0xfe, 0xff);

                const res_be = _.decodeUTF16(input, true, flush)?;
                let decoded = res_be[0];
                const saw_error = res_be[1];

                if (saw_error && _.fatal) {
                    decoded.deinit(default_allocator);
                    if (flush) _.bom_seen = false;
                    return globalThis.ERR(.ENCODING_INVALID_ENCODED_DATA, "The encoded data was not valid UTF-16BE data", .{}).throw();
                }

                if (flush) _.bom_seen = false;
                if (decoded.items.len == 0) {
                    decoded.deinit(default_allocator);
                    return ZigString.Empty.toJS(globalThis);
                }

                return ZigString.toExternalU16(decoded.items.ptr, decoded.items.len, globalThis);
            },

            else => {
                let encoding_name = EncodingLabel.getLabel(_.encoding);

                const codec_opt = TextCodec.create(encoding_name);
                if (codec_opt == null) {
                    return ZigString.init("").toJS(globalThis);
                }
                let codec = codec_opt.?;
                const codec_guard = DeinitGuard<TextCodec>{ ptr: codec };

                if (!_.ignore_bom) {
                    codec.stripBOM();
                }

                let result = codec.decode(buffer_slice, flush, _.fatal);
                const result_guard = DerefGuard<@TypeOf(result.result)>{ ptr: unsafe { &result.result } };
                * = &result_guard;

                if (result.sawError && _.fatal) {
                    return globalThis.ERR(.ENCODING_INVALID_ENCODED_DATA, "The encoded data was not valid {s} data", .{encoding_name}).throw();
                }

                return result.result.toJS(globalThis);
            },
        }
    }

    static fn constructor(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): (*TextDecoder) ! JSError {
        const args = callframe.argumentsAsArray(2);
        const encoding_value = args[0];
        const options_value = args[1];

        let decoder = TextDecoder{};

        if (encoding_value.isString()) {
            let str = encoding_value.toSlice(globalThis, default_allocator)?;
            const str_guard = DeinitGuard<@TypeOf(str)>{ ptr: unsafe { &str } };
            * = &str_guard;

            if (label := EncodingLabel.which(str.slice())) {
                if (label == .replacement) {
                    return globalThis.ERR(.ENCODING_NOT_SUPPORTED, "Unsupported encoding label \"{s}\"", .{str.slice()}).throw();
                }
                decoder.encoding = label;
            } else {
                return globalThis.ERR(.ENCODING_NOT_SUPPORTED, "Unsupported encoding label \"{s}\"", .{str.slice()}).throw();
            }
        } else if (encoding_value.isUndefined()) {
            decoder.encoding = EncodingLabel.@"UTF-8";
        } else {
            return globalThis.throwInvalidArguments("TextDecoder(encoding) label is invalid", .{});
        }

        if (!options_value.isUndefinedOrNull()) {
            if (!options_value.isObject()) {
                return globalThis.throwInvalidArgumentTypeValue("options", "object", options_value);
            }

            if (fatal := options_value.get(globalThis, "fatal")?) {
                decoder.fatal = fatal.toBoolean();
            }

            if (ignoreBOM := options_value.get(globalThis, "ignoreBOM")?) {
                if (ignoreBOM.isBoolean()) {
                    decoder.ignore_bom = ignoreBOM.asBoolean();
                } else {
                    return globalThis.throwInvalidArguments("TextDecoder(options) ignoreBOM is invalid. Expected boolean value", .{});
                }
            }
        }

        return TextDecoder.@"new"(decoder);
    }
}
