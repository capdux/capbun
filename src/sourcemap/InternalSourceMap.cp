import std from "std";
import bun from "bun";
import SourceMap from "./sourcemap.zig";
import internal_jsc from "../sourcemap_jsc/internal_jsc.zig";

const Mapping = SourceMap.Mapping;
const SourceMapState = SourceMap.SourceMapState;
const VLQ = SourceMap.VLQ;
const MutableString = bun.MutableString;

pub const sync_interval: usize = 64;
pub const header_size: usize = 32;
const stream_tail_pad: usize = 1;
const max_varint_len: usize = 5;
const flag_has_gen_line_exceptions: u8 = 1 << 2;
const flag_has_src_idx: u8 = 1 << 3;

pub const TestingAPIs = internal_jsc.TestingAPIs;

struct ErrDeinitPtrGuard<T> {
    ptr: *T!,
    ~errinit() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct ObjectBrace {
    generated: SourceMap.LineColumnOffset,
    original: SourceMap.LineColumnOffset,
}

pub @c struct SyncEntry {
    generated_line: i32,
    generated_column: i32,
    byte_offset: u32,
    original_line: i32,
    original_column: i32,
    source_index: i32,

    @inline pub fn lessOrEqual(line: i32, col: i32): bool {
        return _.generated_line < line ||
            (_.generated_line == line && _.generated_column <= col);
    }

    @inline pub fn toState(): State {
        return State{
            generated_line: _.generated_line,
            generated_column: _.generated_column,
            source_index: _.source_index,
            original_line: _.original_line,
            original_column: _.original_column,
        };
    }
}

struct State {
    generated_line: i32 = 0,
    generated_column: i32 = 0,
    source_index: i32 = 0,
    original_line: i32 = 0,
    original_column: i32 = 0,

    @inline pub fn lessOrEqual(line: i32, col: i32): bool {
        return _.generated_line < line ||
            (_.generated_line == line && _.generated_column <= col);
    }

    pub fn toMapping(): Mapping {
        return Mapping{
            generated: {
                lines: bun.Ordinal.fromZeroBased(_.generated_line),
                columns: bun.Ordinal.fromZeroBased(_.generated_column),
            },
            original: {
                lines: bun.Ordinal.fromZeroBased(_.original_line),
                columns: bun.Ordinal.fromZeroBased(_.original_column),
            },
            source_index: _.source_index,
            name_index: -1,
        };
    }
}

@inline fn zigzagEncode(value: i32): u32 {
    return @bitCast((value << 1) ^ (value >> 31));
}

@inline fn zigzagDecode(value: u32): i32 {
    return @as(i32, @bitCast(value >> 1)) ^ (-@as(i32, @bitCast(value & 1)));
}

fn writeVarint(buf: [*]u8!, signed: i32): usize {
    let v = zigzagEncode(signed);
    let i: usize = 0;
    while (true) {
        let byte: u8 = (v & 0x7f) as u8;
        v >>= 7;
        if (v != 0) byte |= 0x80;
        unsafe { buf[i] = byte; }
        i += 1;
        if (v == 0) return i;
    }
}

fn readVarint(bytes: []const u8, pos: *usize!): i32 {
    let i = unsafe { pos.* };
    const first = bytes[i];
    i += 1;
    if (first < 0x80) {
        unsafe { pos.* = i; }
        return zigzagDecode(first);
    }
    let result: u32 = first & 0x7f;
    let shift: u6 = 7;
    while (true) {
        if (i >= bytes.len || shift > 28) break;
        const byte = bytes[i];
        i += 1;
        result |= @as(u32, byte & 0x7f) << @as(u5, @intCast(shift));
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    unsafe { pos.* = i; }
    return zigzagDecode(result);
}

@inline fn testBit(base: [*]const u8!, idx: usize): bool {
    return unsafe { ((base[idx >> 3] >> @as(u3, @intCast(idx & 7))) & 1) != 0 };
}

static struct win_hdr {
    pub const count_off: usize = 0;
    pub const flags_off: usize = 1;
    pub const gen_col_len_off: usize = 2;
    pub const orig_line_len_off: usize = 4;
    pub const orig_col_len_off: usize = 6;
    pub const gen_line_mask_off: usize = 8;
    pub const orig_line_eq_mask_off: usize = 16;
    pub const orig_col_eq_mask_off: usize = 24;
    pub const gen_col_lane_off: usize = 32;
}

struct WindowReader {
    bytes: []const u8,
    base: [*]const u8!,
    gen_col_pos: usize,
    orig_line_exc_pos: usize,
    orig_col_exc_pos: usize,
    gen_line_exc_pos: usize,
    src_idx_mask: [*]const u8!,
    src_idx_exc_pos: usize,
    count: u8,
    flags: u8,
    gen_line_exc_next_idx: u8,
    delta_idx: u8,

    pub fn parse(*_, bytes: []const u8, start: usize) {
        unsafe {
            const b = bytes.ptr + start;
            _.bytes = bytes;
            _.base = b;
            _.count = @min(b[win_hdr.count_off], sync_interval as u8);
            const flags = b[win_hdr.flags_off];
            _.flags = flags;
            _.delta_idx = 0;

            const gen_col_len: usize = @as(u16, @bitCast(b[win_hdr.gen_col_len_off..][0..2].*));
            const orig_line_len: usize = @as(u16, @bitCast(b[win_hdr.orig_line_len_off..][0..2].*));
            const orig_col_len: usize = @as(u16, @bitCast(b[win_hdr.orig_col_len_off..][0..2].*));

            _.gen_col_pos = start + win_hdr.gen_col_lane_off;
            _.orig_line_exc_pos = _.gen_col_pos + gen_col_len;
            _.orig_col_exc_pos = _.orig_line_exc_pos + orig_line_len;
            let pos = _.orig_col_exc_pos + orig_col_len;
            _.gen_line_exc_next_idx = 0xFF;
            if (flags != 0) {
                if ((flags & flag_has_gen_line_exceptions) != 0 && pos < bytes.len) {
                    _.gen_line_exc_pos = pos;
                    _.gen_line_exc_next_idx = bytes[pos];
                    while (pos < bytes.len && bytes[pos] != 0xFF) {
                        pos += 1;
                        * = readVarint(bytes, *pos!);
                    }
                    pos += 1;
                }
                if ((flags & flag_has_src_idx) != 0) {
                    _.src_idx_mask = bytes.ptr + pos;
                    pos += 8;
                    _.src_idx_exc_pos = pos;
                }
            }
        }
    }

    @inline pub fn done(_: &WindowReader): bool {
        return _.delta_idx + 1 >= _.count;
    }

    pub fn next(*_, state: *State) {
        const delta_idx = _.delta_idx;
        _.delta_idx = delta_idx + 1;
        const b = _.base;

        let d_gen_line: i32 = if (unsafe { testBit(b + win_hdr.gen_line_mask_off, delta_idx) }) 1 else 0;
        const d_gen_col = unsafe { readVarint(_.bytes, *_.gen_col_pos!) };
        let d_orig_line: i32 = if (unsafe { testBit(b + win_hdr.orig_line_eq_mask_off, delta_idx) }) d_gen_line else unsafe { readVarint(_.bytes, *_.orig_line_exc_pos!) };
        const d_orig_col: i32 = if (unsafe { testBit(b + win_hdr.orig_col_eq_mask_off, delta_idx) }) d_gen_col else unsafe { readVarint(_.bytes, *_.orig_col_exc_pos!) };

        if (_.flags != 0) {
            unsafe {
                _.nextRare(delta_idx, *d_gen_line!, *d_orig_line!, state);
            }
        }

        if (d_gen_line != 0) {
            state.generated_line += d_gen_line;
            state.generated_column = d_gen_col;
        } else {
            state.generated_column += d_gen_col;
        }
        state.original_line += d_orig_line;
        state.original_column += d_orig_col;
    }

    fn nextRare(*_, delta_idx: u8, d_gen_line: *i32!, d_orig_line: *i32!, state: *State) {
        @branchHint(.cold);
        unsafe {
            if (_.gen_line_exc_next_idx == delta_idx) {
                let p = _.gen_line_exc_pos + 1;
                d_gen_line.* = readVarint(_.bytes, *p!);
                if (testBit(_.base + win_hdr.orig_line_eq_mask_off, delta_idx)) d_orig_line.* = d_gen_line.*;
                _.gen_line_exc_pos = p;
                _.gen_line_exc_next_idx = _.bytes[p];
            }
            if ((_.flags & flag_has_src_idx) != 0 && !testBit(_.src_idx_mask, delta_idx)) {
                state.source_index += readVarint(_.bytes, *_.src_idx_exc_pos!);
            }
        }
    }
}

pub struct FindCacheSlot {
    data: [*]const u8?! = null,
    sync_idx: u32 = 0,
    decoded_count: u8 = 0,
    reader: WindowReader = undefined,
    decoded: [64]State = undefined,
}

struct FindCacheKey {
    data: [*]const u8?! = null,
    sync_idx: u32 = 0,
}

pub struct FindCache {
    pub const slot_count: usize = 16;

    keys: [16]FindCacheKey = [FindCacheKey{}; 16],
    slots: [16]FindCacheSlot = [FindCacheSlot{}; 16],
    next_victim: u8 = 0,

    pub const Key = FindCacheKey;

    pub fn invalidate(*_, data: [*]const u8!) {
        for (i of 0..slot_count) {
            if (_.keys[i].data == data) {
                _.keys[i].data = null;
                _.slots[i].data = null;
            }
        }
    }

    pub fn invalidateAll(*_) {
        for (i of 0..slot_count) {
            _.keys[i].data = null;
            _.slots[i].data = null;
        }
    }

    @inline pub fn slotFor(*_, data: [*]const u8!, sync_idx: u32): *FindCacheSlot! {
        for (i of 0..slot_count) {
            const k = _.keys[i];
            if (k.data == data && k.sync_idx == sync_idx) return unsafe { *_.slots[i]! };
        }
        for (i of 0..slot_count) {
            if (_.keys[i].data == null) {
                _.keys[i] = FindCacheKey{ data: data, sync_idx: sync_idx };
                return unsafe { *_.slots[i]! };
            }
        }
        const v = _.next_victim;
        _.next_victim = (v + 1) & ((slot_count as u8) - 1);
        _.keys[v] = FindCacheKey{ data: data, sync_idx: sync_idx };
        return unsafe { *_.slots[v]! };
    }
}

pub struct Cursor {
    map: InternalSourceMap,
    state: State,
    peek: State?,
    reader: WindowReader,
    sync_idx: u32,
    has_state: bool,

    pub static fn init(map: InternalSourceMap): Cursor {
        return Cursor{ map: map, state: {}, peek: null, reader: undefined, sync_idx: 0, has_state: false };
    }

    pub fn moveTo(*_, line: bun.Ordinal, column: bun.Ordinal): Mapping? {
        const target_line = line.zeroBased();
        const target_col = column.zeroBased();

        if (!_.has_state || !_.state.lessOrEqual(target_line, target_col)) {
            if (!_.reseek(target_line, target_col)) return null;
        }

        while (true) {
            if (p := _.peek) {
                if (!p.lessOrEqual(target_line, target_col)) break;
                _.state = p;
                _.peek = null;
            }
            const nxt = _.advanceOne() ?? break;
            if (nxt.lessOrEqual(target_line, target_col)) {
                _.state = nxt;
            } else {
                _.peek = nxt;
                break;
            }
        }

        if (_.state.generated_line != target_line) return null;
        return _.state.toMapping();
    }

    fn advanceOne(*_): State? {
        let seed: State = {};
        if (_.reader.done()) {
            const n_sync = _.map.syncCount();
            if (_.sync_idx + 1 >= n_sync) return null;
            _.sync_idx += 1;
            _.map.seedWindow(_.sync_idx, *seed, *_.reader);
            return seed;
        }
        let nxt: State = _.peek ?? _.state;
        _.reader.next(*nxt);
        return nxt;
    }

    fn reseek(*_, target_line: i32, target_col: i32): bool {
        const idx_opt = _.map.locateWindow(target_line, target_col);
        if (idx_opt == null) {
            _.has_state = false;
            return false;
        }
        const idx = idx_opt.?;
        _.sync_idx = idx;
        _.map.seedWindow(idx, *_.state, *_.reader);
        _.peek = null;
        _.has_state = true;
        return true;
    }
}

struct Delta {
    d_gen_line: i32,
    d_gen_col: i32,
    d_orig_line: i32,
    d_orig_col: i32,
    d_src_idx: i32,
}

pub struct Builder {
    allocator: std.mem.Allocator,
    sync_entries: std.ArrayListUnmanaged(SyncEntry) = {},
    win_stream: std.ArrayListUnmanaged(u8) = {},
    pending: [64]State = undefined,
    pending_n: u8 = 0,
    pending_generated_line_delta: i32 = 0,
    state: State = {},
    count: u32 = 0,
    finalized: MutableString? = null,
    object_braces: std.ArrayListUnmanaged(ObjectBrace) = {},

    pub static fn init(allocator: std.mem.Allocator): Builder {
        return Builder{ allocator: allocator };
    }

    pub fn deinit(*_) {
        _.sync_entries.deinit(_.allocator);
        _.win_stream.deinit(_.allocator);
        _.object_braces.deinit(_.allocator);
        if (*m := _.finalized) m.deinit();
    }

    pub fn markObjectBrace(*_, last: SourceMapState) {
        _.object_braces.append(_.allocator, ObjectBrace{
            generated: {
                lines: bun.Ordinal.fromZeroBased(_.state.generated_line),
                columns: bun.Ordinal.fromZeroBased(last.generated_column),
            },
            original: {
                lines: bun.Ordinal.fromZeroBased(last.original_line),
                columns: bun.Ordinal.fromZeroBased(last.original_column),
            },
        }) catch e => bun.handleOom(e);
    }

    pub fn appendLineSeparator(*_) {
        _.pending_generated_line_delta += 1;
    }

    pub fn appendMapping(*_, current: SourceMapState) {
        _.state = State{
            generated_line: _.state.generated_line + _.pending_generated_line_delta,
            generated_column: current.generated_column,
            source_index: current.source_index,
            original_line: current.original_line,
            original_column: current.original_column,
        };
        _.pending_generated_line_delta = 0;

        _.pending[_.pending_n] = _.state;
        _.pending_n += 1;
        _.count += 1;

        if (_.pending_n == sync_interval) _.flushWindow();
    }

    fn flushWindow(*_) {
        const n = _.pending_n;
        if (n == 0) return;
        const seed = _.pending[0];
        const start_off = _.win_stream.items.len as u32;
        _.sync_entries.append(_.allocator, SyncEntry{
            generated_line: seed.generated_line,
            generated_column: seed.generated_column,
            byte_offset: start_off,
            original_line: seed.original_line,
            original_column: seed.original_column,
            source_index: seed.source_index,
        }) catch e => bun.handleOom(e);

        const n_deltas: usize = n - 1;
        let deltas: [63]Delta = undefined;
        let flags: u8 = 0;
        let prev = seed;
        for (k of 0..n_deltas) {
            const cur = _.pending[k + 1];
            const d_gen_line = cur.generated_line - prev.generated_line;
            const d_gen_col = if (d_gen_line != 0) cur.generated_column else cur.generated_column - prev.generated_column;
            const d_orig_line = cur.original_line - prev.original_line;
            const d_orig_col = cur.original_column - prev.original_column;
            const d_src_idx = cur.source_index - prev.source_index;
            deltas[k] = Delta{ d_gen_line: d_gen_line, d_gen_col: d_gen_col, d_orig_line: d_orig_line, d_orig_col: d_orig_col, d_src_idx: d_src_idx };
            if (d_gen_line > 1 || d_gen_line < 0) flags |= flag_has_gen_line_exceptions;
            if (d_src_idx != 0) flags |= flag_has_src_idx;
            prev = cur;
        }

        let cap: usize = win_hdr.gen_col_lane_off + 3 * n_deltas * max_varint_len;
        if ((flags & flag_has_gen_line_exceptions) != 0) cap += n_deltas * (1 + max_varint_len) + 1;
        if ((flags & flag_has_src_idx) != 0) cap += 8 + n_deltas * max_varint_len;

        _.win_stream.ensureUnusedCapacity(_.allocator, cap) catch e => bun.handleOom(e);
        const base = _.win_stream.items.len;
        _.win_stream.items.len += cap;
        let buf = _.win_stream.items[base..][0..cap];
        @memset(buf[0..win_hdr.gen_col_lane_off], 0);

        buf[win_hdr.count_off] = n;
        buf[win_hdr.flags_off] = flags;

        let w: usize = win_hdr.gen_col_lane_off;
        for (k of 0..n_deltas) {
            const d = deltas[k];
            const bit = (1 as u8) << @as(u3, @intCast(k & 7));
            if (d.d_gen_line >= 1) buf[win_hdr.gen_line_mask_off + ((k as usize) >> 3)] |= bit;
            if (d.d_orig_line == d.d_gen_line) buf[win_hdr.orig_line_eq_mask_off + ((k as usize) >> 3)] |= bit;
            if (d.d_orig_col == d.d_gen_col) buf[win_hdr.orig_col_eq_mask_off + ((k as usize) >> 3)] |= bit;
            w += writeVarint(buf[w..].ptr, d.d_gen_col);
        }
        const gen_col_len = (w - win_hdr.gen_col_lane_off) as u16;
        unsafe { buf[win_hdr.gen_col_len_off..][0..2].* = @bitCast(gen_col_len); }

        const orig_line_start = w;
        for (k of 0..n_deltas) {
            const d = deltas[k];
            if (d.d_orig_line != d.d_gen_line) w += writeVarint(buf[w..].ptr, d.d_orig_line);
        }
        const orig_line_len = (w - orig_line_start) as u16;
        const orig_col_start = w;
        for (k of 0..n_deltas) {
            const d = deltas[k];
            if (d.d_orig_col != d.d_gen_col) w += writeVarint(buf[w..].ptr, d.d_orig_col);
        }
        const orig_col_len = (w - orig_col_start) as u16;
        unsafe {
            buf[win_hdr.orig_line_len_off..][0..2].* = @bitCast(orig_line_len);
            buf[win_hdr.orig_col_len_off..][0..2].* = @bitCast(orig_col_len);
        }

        if ((flags & flag_has_gen_line_exceptions) != 0) {
            for (k of 0..n_deltas) {
                if (deltas[k].d_gen_line > 1 || deltas[k].d_gen_line < 0) {
                    buf[w] = k as u8;
                    w += 1;
                    w += writeVarint(buf[w..].ptr, deltas[k].d_gen_line);
                }
            }
            buf[w] = 0xFF;
            w += 1;
        }
        if ((flags & flag_has_src_idx) != 0) {
            const mask_off = w;
            @memset(buf[w..][0..8], 0);
            w += 8;
            for (k of 0..n_deltas) {
                if (deltas[k].d_src_idx == 0) {
                    buf[mask_off + @as(usize, @intCast(k >> 3))] |= (1 as u8) << @as(u3, @intCast(k & 7));
                }
            }
            for (k of 0..n_deltas) {
                const d = deltas[k];
                if (d.d_src_idx != 0) w += writeVarint(buf[w..].ptr, d.d_src_idx);
            }
        }

        _.win_stream.items.len = base + w;
        _.pending_n = 0;
    }

    pub fn finalize(*_): *MutableString! {
        if (_.finalized != null) return unsafe { &_.finalized.? };

        _.flushWindow();

        const sync_bytes = _.sync_entries.items.len * @sizeOf(SyncEntry);
        const stream_offset = (header_size + sync_bytes) as u32;
        const object_braces_offset: usize = stream_offset + _.win_stream.items.len;
        const object_braces_bytes: usize = _.object_braces.items.len * @sizeOf(ObjectBrace);
        const total: usize = object_braces_offset + object_braces_bytes + stream_tail_pad;

        let out = MutableString.initEmpty(_.allocator);
        out.list.resize(_.allocator, total) catch e => bun.handleOom(e);
        let blob = out.list.items;

        @memset(blob[0..20], 0);
        unsafe {
            blob[20..24].* = @bitCast(_.object_braces.items.len as u32);
            blob[24..28].* = @bitCast(_.sync_entries.items.len as u32);
            blob[28..32].* = @bitCast(stream_offset);
        }
        if (sync_bytes > 0) {
            @memcpy(blob[header_size..][0..sync_bytes], std.mem.sliceAsBytes(_.sync_entries.items));
        }
        @memcpy(blob[stream_offset..][0.._.win_stream.items.len], _.win_stream.items);
        if (object_braces_bytes > 0) {
            @memcpy(blob[object_braces_offset..][0..object_braces_bytes], std.mem.sliceAsBytes(_.object_braces.items));
        }
        @memset(blob[total - stream_tail_pad ..][0..stream_tail_pad], 0);

        _.win_stream.clearAndFree(_.allocator);
        _.sync_entries.clearAndFree(_.allocator);
        _.object_braces.clearAndFree(_.allocator);

        _.finalized = out;
        return unsafe { &_.finalized.? as *MutableString! };
    }
}

pub err InvalidSourceMapError { InvalidSourceMap }

export @root struct InternalSourceMap {
    data: [*]const u8!,

    @inline pub fn totalLen(): usize {
        return unsafe { @as(u64, @bitCast(_.data[0..8].*)) };
    }

    @inline pub fn mappingCount(): usize {
        return unsafe { @as(u64, @bitCast(_.data[8..16].*)) };
    }

    @inline pub fn inputLineCount(): usize {
        return unsafe { @as(u32, @bitCast(_.data[16..20].*)) };
    }

    @inline pub fn objectBraceCount(): u32 {
        return unsafe { @as(u32, @bitCast(_.data[20..24].*)) };
    }

    pub fn objectBraces(): []const ObjectBrace {
        const count = _.objectBraceCount() as usize;
        if (count == 0) return [];
        const bytes = count * @sizeOf(ObjectBrace);
        const end = _.totalLen() - stream_tail_pad;
        const start = end - bytes;
        return unsafe { std.mem.bytesAsSlice(ObjectBrace, @as([]align(@alignOf(ObjectBrace)) const u8, @alignCast(_.data[start..end]))) };
    }

    @inline pub fn syncCount(): u32 {
        return unsafe { @as(u32, @bitCast(_.data[24..28].*)) };
    }

    @inline pub fn streamOffset(): u32 {
        return unsafe { @as(u32, @bitCast(_.data[28..32].*)) };
    }

    pub fn syncEntry(index: usize): SyncEntry {
        const off = header_size + index * @sizeOf(SyncEntry);
        return unsafe { @as(SyncEntry, @bitCast(_.data[off..][0..@sizeOf(SyncEntry)].*)) };
    }

    @inline pub fn stream(): []const u8 {
        return unsafe { _.data[_.streamOffset().._.totalLen()] };
    }

    pub fn deinit() {
        unsafe {
            bun.default_allocator.free(@constCast(_.data[0.._.totalLen()]));
        }
    }

    pub fn memoryCost(): usize {
        return _.totalLen();
    }

    pub static fn isValidBlob(blob: []const u8): bool {
        if (blob.len < header_size) return false;
        const ism = InternalSourceMap{ data: blob.ptr };
        const total = ism.totalLen();
        if (total != blob.len) return false;
        const sync_n = ism.syncCount();
        const stream_off = ism.streamOffset();
        const sync_end = header_size + (sync_n as usize) * @sizeOf(SyncEntry);
        if (stream_off < sync_end) return false;
        if (stream_off > total) return false;
        if (total < stream_off + stream_tail_pad) return false;
        return true;
    }

    fn locateWindow(target_line: i32, target_col: i32): u32? {
        const n_sync = _.syncCount();
        if (n_sync == 0) return null;
        let lo: usize = 0;
        let hi: usize = n_sync;
        while (lo < hi) {
            const mid = lo + (hi - lo) / 2;
            if (_.syncEntry(mid).lessOrEqual(target_line, target_col)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo == 0) return null;
        return (lo - 1) as u32;
    }

    fn seedWindow(sync_idx: u32, state: *State, reader: *WindowReader) {
        const se = _.syncEntry(sync_idx);
        unsafe { state.* = se.toState(); }
        reader.parse(_.stream(), se.byte_offset);
    }

    pub fn findWithCache(line: bun.Ordinal, column: bun.Ordinal, set: *FindCache): Mapping? {
        const target_line = line.zeroBased();
        const target_col = column.zeroBased();

        const sync_idx_opt = _.locateWindow(target_line, target_col);
        if (sync_idx_opt == null) return null;
        const sync_idx = sync_idx_opt.?;
        const cache = set.slotFor(_.data, sync_idx);

        if (cache.data != _.data || cache.sync_idx != sync_idx || cache.decoded_count == 0) {
            _.seedWindow(sync_idx, *cache.decoded[0], *cache.reader);
            cache.data = _.data;
            cache.sync_idx = sync_idx;
            cache.decoded_count = 1;
        }

        let decoded_count = cache.decoded_count;
        let state: State = cache.decoded[decoded_count - 1];
        while (!cache.reader.done() && state.lessOrEqual(target_line, target_col)) {
            cache.reader.next(*state);
            cache.decoded[decoded_count] = state;
            decoded_count += 1;
        }
        cache.decoded_count = decoded_count;

        const decoded = cache.decoded[0..cache.decoded_count];
        let lo: usize = 0;
        let hi: usize = decoded.len;
        while (lo < hi) {
            const mid = lo + (hi - lo) / 2;
            if (decoded[mid].lessOrEqual(target_line, target_col)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo == 0) return null;
        const best = decoded[lo - 1];
        if (best.generated_line != target_line) return null;
        return best.toMapping();
    }

    pub fn find(line: bun.Ordinal, column: bun.Ordinal): Mapping? {
        const target_line = line.zeroBased();
        const target_col = column.zeroBased();

        const sync_idx_opt = _.locateWindow(target_line, target_col);
        if (sync_idx_opt == null) return null;
        const sync_idx = sync_idx_opt.?;

        let state: State = {};
        let reader: WindowReader = undefined;
        _.seedWindow(sync_idx, *state, *reader);

        let best: State = state;
        let nxt: State = state;
        while (!reader.done()) {
            nxt = state;
            reader.next(*nxt);
            if (!nxt.lessOrEqual(target_line, target_col)) break;
            best = nxt;
            state = nxt;
        }

        if (best.generated_line != target_line) return null;
        return best.toMapping();
    }

    pub fn cursor(): Cursor {
        return Cursor.init(_);
    }

    pub fn appendVLQTo(out: *MutableString) {
        const n_sync = _.syncCount();
        let prev: SourceMapState = {};
        let generated_line: i32 = 0;
        let state: State = {};
        let reader: WindowReader = undefined;

        let idx: u32 = 0;
        while (idx < n_sync) : (idx += 1) {
            state = {};
            _.seedWindow(idx, *state, *reader);
            unsafe { emitVLQ(*state, *prev, *generated_line!, out); }
            while (!reader.done()) {
                reader.next(*state);
                unsafe { emitVLQ(*state, *prev, *generated_line!, out); }
            }
        }
    }

    static fn emitVLQ(state: *State, prev: *SourceMapState, generated_line: *i32!, out: *MutableString) {
        unsafe {
            while (generated_line.* < state.generated_line) : (generated_line.* += 1) {
                out.appendChar(';') catch e => bun.handleOom(e);
                prev.generated_column = 0;
            }
        }
        const current = SourceMapState{
            generated_line: state.generated_line,
            generated_column: state.generated_column,
            source_index: state.source_index,
            original_line: state.original_line,
            original_column: state.original_column,
        };
        const last_byte: u8 = if (out.list.items.len > 0)
            out.list.items[out.list.items.len - 1]
        else
            0;
        SourceMap.appendMappingToBuffer(out, last_byte, unsafe { prev.* }, current);
        unsafe { prev.* = current; }
    }

    static fn accumulateVLQ(absolute: i32, delta: i32): i32 ! InvalidSourceMapError {
        const next = std.math.add(i32, absolute, delta) catch return error.InvalidSourceMap;
        if (next < 0) return error.InvalidSourceMap;
        return next;
    }

    pub static fn fromVLQ(
        allocator: std.mem.Allocator,
        vlq: []const u8,
        input_line_count_hint: u32,
    ): []u8 ! InvalidSourceMapError {
        let builder = Builder.init(allocator);
        const _builder_guard = ErrDeinitPtrGuard<Builder>{ ptr: unsafe { &builder } };

        let generated_column: i32 = 0;
        let source_index: i32 = 0;
        let original_line: i32 = 0;
        let original_column: i32 = 0;
        let max_original_line: i32 = 0;

        let remain = vlq;
        while (remain.len > 0) {
            if (remain[0] == ';') {
                generated_column = 0;
                while (remain.len > 0 && remain[0] == ';') {
                    builder.appendLineSeparator();
                    remain = remain[1..];
                }
                if (remain.len == 0) break;
            }

            const gc = VLQ.decode(remain, 0);
            if (gc.start == 0) return error.InvalidSourceMap;
            generated_column = accumulateVLQ(generated_column, gc.value)?;
            remain = remain[gc.start..];

            if (remain.len == 0 || remain[0] == ',' || remain[0] == ';') {
                if (remain.len > 0 && remain[0] == ',') remain = remain[1..];
                continue;
            }

            const si = VLQ.decode(remain, 0);
            if (si.start == 0) return error.InvalidSourceMap;
            source_index = accumulateVLQ(source_index, si.value)?;
            remain = remain[si.start..];

            const ol = VLQ.decode(remain, 0);
            if (ol.start == 0) return error.InvalidSourceMap;
            original_line = accumulateVLQ(original_line, ol.value)?;
            remain = remain[ol.start..];

            const oc = VLQ.decode(remain, 0);
            if (oc.start == 0) return error.InvalidSourceMap;
            original_column = accumulateVLQ(original_column, oc.value)?;
            remain = remain[oc.start..];

            if (remain.len > 0 && remain[0] != ',' && remain[0] != ';') {
                const ni = VLQ.decode(remain, 0);
                if (ni.start == 0) return error.InvalidSourceMap;
                remain = remain[ni.start..];
            }
            if (remain.len > 0 && remain[0] == ',') remain = remain[1..];

            max_original_line = @max(max_original_line, original_line);
            builder.appendMapping(SourceMapState{
                generated_column: generated_column,
                source_index: source_index,
                original_line: original_line,
                original_column: original_column,
            });
        }

        const out = builder.finalize();
        let blob = out.list.items;
        const total_len = blob.len as u64;
        const mapping_count: u64 = builder.count;
        const input_lines: u32 = @truncate(@max(
            input_line_count_hint as u64,
            (max_original_line as u64) + 1,
        ));
        unsafe {
            blob[0..8].* = @bitCast(total_len);
            blob[8..16].* = @bitCast(mapping_count);
            blob[16..20].* = @bitCast(input_lines);
            blob[20..24].* = @bitCast(@as(u32, 0));
        }

        const owned = out.list.toOwnedSlice(allocator) catch e => bun.handleOom(e);
        builder.finalized = null;
        return owned;
    }
}
