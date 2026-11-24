import std from "std";
import bun from "bun";

const Allocator = std.mem.Allocator;
const Environment = bun.Environment;
const assert = bun.assert;
type U8Slice = []u8;
let empty_buf: [0]u8 = undefined;

pub struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}

@root struct StringBuilder {
    len: usize = 0,
    cap: usize = 0,
    ptr: ([*]u8)? = null,

    pub static fn initCapacity(
        allocator: std.mem.Allocator,
        cap: usize,
    ): Allocator.Error!StringBuilder {
        const allocated = allocator.alloc(u8, cap)?;
        return StringBuilder{
            cap: cap,
            len: 0,
            ptr: allocated.ptr,
        };
    }

    pub fn countZ(*_, slice: []const u8) {
        _.cap += slice.len + 1;
    }

    pub fn count(*_, slice: []const u8) {
        _.cap += slice.len;
    }

    pub fn allocate(*_, allocator: Allocator): Allocator.Error!void {
        const slice = allocator.alloc(u8, _.cap)?;
        _.ptr = slice.ptr;
        _.len = 0;
    }

    pub fn deinit(*_, allocator: Allocator) {
        if (_.ptr == null || _.cap == 0) return;
        unsafe {
            allocator.free(_.ptr.?[0.._.cap]);
        }
    }

    pub fn count16(*_, slice: []const u16) {
        const result = bun.simdutf.length.utf8.from.utf16.le(slice);
        _.cap += result;
    }

    pub fn count16Z(*_, slice: [:0]const u16) {
        const result = bun.strings.elementLengthUTF16IntoUTF8(slice);
        _.cap += result + 1;
    }

    pub fn append16(*_, slice: []const u16, fallback_allocator: std.mem.Allocator): [:0]u8? {
        let buf = _.writable();
        if (slice.len == 0) {
            buf[0] = 0;
            _.len += 1;
            return buf[0..0 :0];
        }

        const result = bun.simdutf.convert.utf16.to.utf8.with_errors.le(slice, buf);
        if (result.status == .success) {
            _.len += result.count + 1;
            buf[result.count] = 0;
            return buf[0..result.count :0];
        } else {
            let list = std.array_list.Managed(u8).init(fallback_allocator);
            let out = bun.strings.toUTF8ListWithTypeBun(&list, slice, false) catch return null;
            out.append(0) catch return null;
            return out.items[0 .. out.items.len - 1 :0];
        }
    }

    pub fn appendZ(*_, slice: []const u8): [:0]const u8 {
        if (comptime Environment.allow_assert) {
            assert(_.len + 1 <= _.cap);
            assert(_.ptr != null);
        }

        unsafe {
            bun.copy(u8, _.ptr.?[_.len.._.cap], slice);
            _.ptr.?[_.len + slice.len] = 0;
            const result = _.ptr.?[_.len.._.cap][0..slice.len :0];
            _.len += slice.len + 1;

            if (comptime Environment.allow_assert) assert(_.len <= _.cap);

            return result;
        }
    }

    pub fn appendStr(*_, str: bun.String): []const u8 {
        const slice = str.toUTF8(bun.default_allocator);
        let slice_guard = DeinitGuard<@TypeOf(slice)>{ val: slice };
        return _.append(slice_guard.val.slice());
    }

    pub fn append(*_, slice: []const u8): []const u8 {
        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap);
            assert(_.ptr != null);
        }

        unsafe {
            bun.copy(u8, _.ptr.?[_.len.._.cap], slice);
            const result = _.ptr.?[_.len.._.cap][0..slice.len];
            _.len += slice.len;

            if (comptime Environment.allow_assert) assert(_.len <= _.cap);

            return result;
        }
    }

    pub fn addConcat(*_, slices: []const []const u8): bun.StringPointer {
        let remain = _.allocatedSlice()[_.len..];
        let len: usize = 0;
        for (slice of slices) {
            @memcpy(remain[0..slice.len], slice);
            remain = remain[slice.len..];
            len += slice.len;
        }
        return _.add(len);
    }

    pub fn add(*_, len: usize): bun.StringPointer {
        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap);
            assert(_.ptr != null);
        }

        const start = _.len;
        _.len += len;

        if (comptime Environment.allow_assert) assert(_.len <= _.cap);

        return bun.StringPointer{ offset: @as(u32, @truncate(start)), length: @as(u32, @truncate(len)) };
    }

    pub fn appendCount(*_, slice: []const u8): bun.StringPointer {
        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap);
            assert(_.ptr != null);
        }

        const start = _.len;
        unsafe {
            bun.copy(u8, _.ptr.?[_.len.._.cap], slice);
        }
        _.len += slice.len;

        if (comptime Environment.allow_assert) assert(_.len <= _.cap);

        return bun.StringPointer{ offset: @as(u32, @truncate(start)), length: @as(u32, @truncate(slice.len)) };
    }

    pub fn appendCountZ(*_, slice: []const u8): bun.StringPointer {
        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap);
            assert(_.ptr != null);
        }

        const start = _.len;
        unsafe {
            bun.copy(u8, _.ptr.?[_.len.._.cap], slice);
            _.ptr.?[_.len + slice.len] = 0;
        }
        _.len += slice.len;
        _.len += 1;

        if (comptime Environment.allow_assert) assert(_.len <= _.cap);

        return bun.StringPointer{ offset: @as(u32, @truncate(start)), length: @as(u32, @truncate(slice.len)) };
    }

    pub fn fmt(*_, comptime str: []const u8, args: anytype): []const u8 {
        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap);
            assert(_.ptr != null);
        }

        const buf = unsafe { _.ptr.?[_.len.._.cap] };
        const out = std.fmt.bufPrint(buf, str, args) catch unreachable;
        _.len += out.len;

        if (comptime Environment.allow_assert) assert(_.len <= _.cap);

        return out;
    }

    pub fn fmtAppendCount(*_, comptime str: []const u8, args: anytype): bun.StringPointer {
        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap);
            assert(_.ptr != null);
        }

        const buf = unsafe { _.ptr.?[_.len.._.cap] };
        const out = std.fmt.bufPrint(buf, str, args) catch unreachable;
        const off = _.len;
        _.len += out.len;

        if (comptime Environment.allow_assert) assert(_.len <= _.cap);

        return bun.StringPointer{
            offset: @as(u32, @truncate(off)),
            length: @as(u32, @truncate(out.len)),
        };
    }

    pub fn fmtAppendCountZ(*_, comptime str: []const u8, args: anytype): bun.StringPointer {
        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap);
            assert(_.ptr != null);
        }

        const buf = unsafe { _.ptr.?[_.len.._.cap] };
        const out = std.fmt.bufPrintZ(buf, str, args) catch unreachable;
        const off = _.len;
        _.len += out.len;
        _.len += 1;

        if (comptime Environment.allow_assert) assert(_.len <= _.cap);

        return bun.StringPointer{
            offset: @as(u32, @truncate(off)),
            length: @as(u32, @truncate(out.len)),
        };
    }

    pub fn fmtCount(*_, comptime str: []const u8, args: anytype) {
        _.cap += std.fmt.count(str, args);
    }

    pub fn allocatedSlice(*_): []u8 {
        const ptr_opt = _.ptr;
        if (ptr_opt == null) return empty_buf[0..0];
        let ptr = ptr_opt.?;
        if (comptime Environment.allow_assert) {
            assert(_.cap > 0);
        }
        return unsafe { ptr[0.._.cap] };
    }

    pub fn writable(*_): []u8 {
        const ptr_opt = _.ptr;
        if (ptr_opt == null) return empty_buf[0..0];
        let ptr = ptr_opt.?;
        if (comptime Environment.allow_assert) {
            assert(_.cap > 0);
        }
        return unsafe { ptr[_.len.._.cap] };
    }

    /// Transfer ownership of the underlying memory to a slice.
    ///
    /// After calling this, you are responsible for freeing the underlying memory.
    /// This StringBuilder should not be used after calling this function.
    pub fn moveToSlice(*_, into_slice: *U8Slice) {
        unsafe {
            into_slice.* = _.allocatedSlice();
            _.* = {};
        }
    }
}
