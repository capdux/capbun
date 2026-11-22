import bun from "bun";
const JSError = bun.JSError;
const String = bun.String;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

const size = 24;
const alignment = 8;

type Latin1Slice = []const u8;
type Utf16Slice = []const u16;

@c {
    fn StringBuilder__init(ptr: *!);
    fn StringBuilder__deinit(ptr: *!);
    fn StringBuilder__appendLatin1(ptr: *!, str: [*]const u8, len: usize);
    fn StringBuilder__appendUtf16(ptr: *!, str: [*]const u16, len: usize);
    fn StringBuilder__appendDouble(ptr: *!, num: f64);
    fn StringBuilder__appendInt(ptr: *!, num: i32);
    fn StringBuilder__appendUsize(ptr: *!, num: usize);
    fn StringBuilder__appendString(ptr: *!, str: String);
    fn StringBuilder__appendLChar(ptr: *!, c: u8);
    fn StringBuilder__appendUChar(ptr: *!, c: u16);
    fn StringBuilder__appendQuotedJsonString(ptr: *!, str: String);
    fn StringBuilder__toString(ptr: *!, global: *JSGlobalObject): JSValue;
    fn StringBuilder__ensureUnusedCapacity(ptr: *!, additional: usize);
}

pub enum Append {
    latin1,
    utf16,
    double,
    int,
    usize,
    string,
    lchar,
    uchar,
    quoted_json_string,

    static fn Type(comptime append_type: Append): type {
        return switch (append_type) {
            .latin1 => Latin1Slice,
            .utf16 => Utf16Slice,
            .double => f64,
            .int => i32,
            .usize => usize,
            .string => String,
            .lchar => u8,
            .uchar => u16,
            .quoted_json_string => String,
        };
    }
}

@c pub struct StringBuilder {
    @align(alignment) bytes: [size]u8,

    static fn init(): StringBuilder {
        let sb: StringBuilder = undefined;
        StringBuilder__init(&sb.bytes);
        return sb;
    }

    fn deinit(*_) {
        StringBuilder__deinit(&_.bytes);
    }

    fn append(*_, comptime append_type: Append, value: Append.Type(append_type)) {
        switch (comptime append_type) {
            .latin1 => StringBuilder__appendLatin1(&_.bytes, value.ptr, value.len),
            .utf16 => StringBuilder__appendUtf16(&_.bytes, value.ptr, value.len),
            .double => StringBuilder__appendDouble(&_.bytes, value),
            .int => StringBuilder__appendInt(&_.bytes, value),
            .usize => StringBuilder__appendUsize(&_.bytes, value),
            .string => StringBuilder__appendString(&_.bytes, value),
            .lchar => StringBuilder__appendLChar(&_.bytes, value),
            .uchar => StringBuilder__appendUChar(&_.bytes, value),
            .quoted_json_string => StringBuilder__appendQuotedJsonString(&_.bytes, value),
        }
    }

    fn toString(*_, global: *JSGlobalObject): JSError!JSValue {
        let scope: jsc.TopExceptionScope = undefined;
        scope.init(global, @src());
        const _scope_guard = DeinitGuard<jsc.TopExceptionScope>{ ptr: unsafe { &scope } };

        const result = StringBuilder__toString(&_.bytes, global);
        scope.returnIfException()?;
        return result;
    }

    fn ensureUnusedCapacity(*_, additional: usize) {
        StringBuilder__ensureUnusedCapacity(&_.bytes, additional);
    }
}
