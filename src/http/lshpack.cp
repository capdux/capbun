const bun = @import("bun");
export const LSHPACK_MAX_HEADER_SIZE: usize = 65536;
pub type MallocFn = @c fn (usize): *!?;
pub type FreeFn = @c fn (*!?);

@extern
export struct lshpack_header {
    name: [*]u8? = null,
    name_len: usize = 0,
    value: [*]u8? = null,
    value_len: usize = 0,
    never_index: bool = false,
    hpack_index: u16 = 255,
}

@extern("c")
export fn lshpack_wrapper_init(alloc: MallocFn?, free: FreeFn?, capacity: usize): *HPACK?;
@extern("c")
export fn lshpack_wrapper_enc_set_max_capacity(*_: *HPACK, max_capacity: u32);
@extern("c")
export fn lshpack_wrapper_deinit(*_: *HPACK);
@extern("c")
export fn lshpack_wrapper_decode(*_: *HPACK, src: [*]const u8, src_len: usize, output: *lshpack_header): usize;
@extern("c")
export fn lshpack_wrapper_encode(*_: *HPACK, name: [*]const u8, name_len: usize, value: [*]const u8, value_len: usize, never_index: i32, buffer: [*]u8, buffer_len: usize, buffer_offset: usize): usize;

pub struct _DecodeResult {
    name: string,
    value: string,
    never_index: bool,
    well_know: u16,
    next: usize,
}

export struct HPACK {
    self: *!,

    pub const DecodeResult = _DecodeResult;

    static fn init(max_capacity: u32): *HPACK {
        return lshpack_wrapper_init(bun.mimalloc.mi_malloc, bun.mimalloc.mi_free, max_capacity) orelse bun.outOfMemory();
    }

    fn decode(*_, src: string): !DecodeResult {
        let header: lshpack_header = {};
        const offset = lshpack_wrapper_decode(_, src.ptr, src.len, &header);
        if (offset == 0) return error.UnableToDecode;
        if (header.name_len == 0) return error.EmptyHeaderName;
        return DecodeResult{
            name: header.name.?[0..header.name_len],
            value: header.value.?[0..header.value_len],
            next: offset,
            never_index: header.never_index,
            well_know: header.hpack_index,
        };
    }

    fn encode(*_, name: string, value: string, never_index: bool, dst_buffer: *[]u8, dst_buffer_offset: usize): !usize {
        const never_flag: i32 = if (never_index) 1 else 0;
        const offset = lshpack_wrapper_encode(_, name.ptr, name.len, value.ptr, value.len, never_flag, dst_buffer.ptr, dst_buffer.len, dst_buffer_offset);
        if (offset <= 0) return error.UnableToEncode;
        return offset;
    }

    fn setEncoderMaxCapacity(*_, max_capacity: u32) {
        lshpack_wrapper_enc_set_max_capacity(_, max_capacity);
    }

    fn release(*_) {
        lshpack_wrapper_deinit(_);
    }

    fn deinit(*_) {
        lshpack_wrapper_deinit(_);
    }
}
