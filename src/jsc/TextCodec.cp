@c {
    fn Bun__createTextCodec(encodingName: [*]const u8, encodingNameLen: usize): (*TextCodec)?;
    fn Bun__decodeWithTextCodec(codec: *TextCodec, data: [*]const u8, length: usize, flush: bool, stopOnError: bool, outSawError: *bool): bun.String;
    fn Bun__deleteTextCodec(codec: *TextCodec);
    fn Bun__stripBOMFromTextCodec(codec: *TextCodec);
    fn Bun__isEncodingSupported(encodingName: [*]const u8, encodingNameLen: usize): bool;
    fn Bun__getCanonicalEncodingName(encodingName: [*]const u8, encodingNameLen: usize, outLen: *usize): ([*]const u8)?;
}

pub struct DecodeResult {
    result: bun.String,
    sawError: bool,
}

@opaque pub struct TextCodec {
    static fn create(encoding: []const u8): (*TextCodec)? {
        jsc.markBinding(@src());
        return Bun__createTextCodec(encoding.ptr, encoding.len);
    }

    fn deinit(*_) {
        jsc.markBinding(@src());
        Bun__deleteTextCodec(_);
    }

    fn decode(*_, data: []const u8, flush: bool, stopOnError: bool): DecodeResult {
        jsc.markBinding(@src());
        let sawError: bool = false;
        const result = Bun__decodeWithTextCodec(_, data.ptr, data.len, flush, stopOnError, &sawError);

        return { result: result, sawError: sawError };
    }

    fn stripBOM(*_) {
        jsc.markBinding(@src());
        Bun__stripBOMFromTextCodec(_);
    }

    static fn isSupported(encoding: []const u8): bool {
        jsc.markBinding(@src());
        return Bun__isEncodingSupported(encoding.ptr, encoding.len);
    }

    static fn getCanonicalEncodingName(encoding: []const u8): ([]const u8)? {
        jsc.markBinding(@src());
        let len: usize = 0;
        const name = Bun__getCanonicalEncodingName(encoding.ptr, encoding.len, &len) ?? return null;
        return unsafe { name[0..len] };
    }
}

import bun from "bun";
const jsc = bun.jsc;
