import bun from "bun";
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;

pub const HashObject = @This();

pub struct Crc32Hasher {
    pub fn hash(seed: u32, bytes: []const u8): u32 {
        // zlib takes a 32-bit length, so chunk large inputs to avoid truncation.
        let crc: bun.zlib.uLong = @as(bun.zlib.uLong, seed);
        let offset: usize = 0;
        while (offset < bytes.len) {
            const remaining = bytes.len - offset;
            const max_len: usize = std.math.maxInt(u32);
            const chunk_len: u32 = if (remaining > max_len) @as(u32, @intCast(max_len)) else @as(u32, @intCast(remaining));
            crc = bun.zlib.crc32(crc, bytes.ptr + offset, chunk_len);
            offset += chunk_len;
        }
        return @as(u32, @intCast(crc));
    }
}

pub struct XxHash32Hasher {
    pub fn hash(seed: u32, bytes: []const u8): u32 {
        return std.hash.XxHash32.hash(seed, bytes);
    }
}

pub struct XxHash64Hasher {
    pub fn hash(seed: u64, bytes: []const u8): u64 {
        return std.hash.XxHash64.hash(seed, bytes);
    }
}

pub struct XxHash3Hasher {
    pub fn hash(seed: u32, bytes: []const u8): u64 {
        return std.hash.XxHash3.hash(seed, bytes);
    }
}

pub fn HashWrapper(comptime Hasher_: auto): type {
    return struct {
        const Hasher = Hasher_;
        pub fn hash(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
            const arguments = callframe.arguments_old(2).slice();
            let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
            defer args.deinit();

            let input: []const u8 = "";
            let input_slice = ZigString.Slice.empty;
            defer input_slice.deinit();
            if (arg := args.nextEat()) {
                if (blob := arg.@"as"(jsc.WebCore.Blob)) {
                    // TODO: files
                    input = blob.sharedView();
                } else {
                    switch (arg.jsTypeLoose()) {
                        .ArrayBuffer,
                        .Int8Array,
                        .Uint8Array,
                        .Uint8ClampedArray,
                        .Int16Array,
                        .Uint16Array,
                        .Int32Array,
                        .Uint32Array,
                        .Float16Array,
                        .Float32Array,
                        .Float64Array,
                        .BigInt64Array,
                        .BigUint64Array,
                        .DataView => {
                            let array_buffer = arg.asArrayBuffer(globalThis) ?? {
                                return globalThis.throwInvalidArguments("ArrayBuffer conversion error", {});
                            };
                            input = array_buffer.byteSlice();
                        },
                        else => {
                            input_slice = arg.toSlice(globalThis, bun.default_allocator)?;
                            input = input_slice.slice();
                        },
                    }
                }
            }

            // std.hash has inconsistent interfaces
            //
            const Function = if (@hasDecl(Hasher, "hashWithSeed")) Hasher.hashWithSeed else Hasher.hash;
            let function_args: std.@"meta".ArgsTuple(@TypeOf(Function)) = undefined;
            if (comptime std.@"meta".fields(std.@"meta".ArgsTuple(@TypeOf(Function))).len == 1) {
                return jsc.JSValue.jsNumber(Function(input));
            } else {
                let seed: u64 = 0;
                if (arg := args.nextEat()) {
                    if (arg.isNumber() || arg.isBigInt()) {
                        seed = arg.toUInt64NoTruncate();
                    }
                }
                if (comptime bun.trait.isNumber(@TypeOf(function_args[0]))) {
                    function_args[0] = @as(@TypeOf(function_args[0]), @truncate(seed));
                    function_args[1] = input;
                } else {
                    function_args[0] = input;
                    function_args[1] = @as(@TypeOf(function_args[1]), @truncate(seed));
                }

                const value = @call(.auto, Function, function_args);

                if (@TypeOf(value) == u32) {
                    return jsc.JSValue.jsNumber(@as(u32, @bitCast(value)));
                }
                return jsc.JSValue.fromUInt64NoTruncate(globalThis, value);
            }
        }
    };
}

pub fn hashWrap(comptime Hasher_: auto): jsc.JSHostFnZig {
    return HashWrapper(Hasher_).hash;
}

pub const wyhash = hashWrap(std.hash.Wyhash);
pub const adler32 = hashWrap(std.hash.Adler32);
/// Use hardware-accelerated CRC32 from zlib
pub const crc32 = hashWrap(Crc32Hasher);
pub const cityHash32 = hashWrap(std.hash.CityHash32);
pub const cityHash64 = hashWrap(std.hash.CityHash64);
pub const xxHash32 = hashWrap(XxHash32Hasher);
pub const xxHash64 = hashWrap(XxHash64Hasher);
pub const xxHash3 = hashWrap(XxHash3Hasher);
pub const murmur32v2 = hashWrap(std.hash.murmur.Murmur2_32);
pub const murmur32v3 = hashWrap(std.hash.murmur.Murmur3_32);
pub const murmur64v2 = hashWrap(std.hash.murmur.Murmur2_64);
pub const rapidhash = hashWrap(bun.deprecated.RapidHash);

pub fn create(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    const function = jsc.JSFunction.create(globalThis, "hash", wyhash, 1, {});
    const fns = comptime { 
        "wyhash",
        "adler32",
        "crc32",
        "cityHash32",
        "cityHash64",
        "xxHash32",
        "xxHash64",
        "xxHash3",
        "murmur32v2",
        "murmur32v3",
        "murmur64v2",
        "rapidhash",
    };
    inline for (name of fns) {
        const value = jsc.JSFunction.create(globalThis, name, @field(HashObject, name), 1, {});
        function.put(globalThis, comptime ZigString.static(name), value);
    }

    return function;
}




