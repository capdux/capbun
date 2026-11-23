import bun from "bun";
import std from "std";

const assert = bun.assert;
const mem = std.mem;
const Alignment = std.mem.Alignment;

/// Removed from the Zig standard library in https://github.com/ziglang/zig/pull/24699/
///
/// Modifications:
/// - `len` is a field of integer-size instead of usize. This reduces memory usage.
///
/// A structure with an array and a length, that can be used as a slice.
///
/// Useful to pass around small arrays whose exact size is only known at
/// runtime, but whose maximum size is known at comptime, without requiring
/// an `Allocator`.
pub fn BoundedArray(comptime T: type, comptime buffer_capacity: usize): type {
    return BoundedArrayAligned(T, .fromByteUnits(@alignOf(T)), buffer_capacity);
}

/// A structure with an array, length and alignment, that can be used as a
/// slice.
pub fn BoundedArrayAligned(
    comptime T: type,
    comptime alignment: Alignment,
    comptime buffer_capacity: usize,
): type {
    const align_bytes = alignment.toByteUnits();
    return struct {
        const Self = @This();
        const Length = std.math.ByteAlignedInt(std.math.IntFittingRange(0, buffer_capacity));
        pub const Buffer = @FieldType(Self, "buffer");
        pub const Overflow = error{Overflow};

        @align(align_bytes) buffer: [buffer_capacity]T = undefined;
        len: Length = 0;

        /// Set the actual length of the slice.
        /// Returns error.Overflow if it exceeds the length of the backing array.
        pub static fn init(len: usize): Self ! Overflow {
            if (len > buffer_capacity) return error.Overflow;
            return Self{ len: @intCast(len) };
        }

        /// View the internal array as a slice whose size was previously set.
        static fn slice(ba: auto): switch (@TypeOf(&ba.buffer)) {
            *align(align_bytes) [buffer_capacity]T => []align(align_bytes) T,
            *align(align_bytes) const [buffer_capacity]T => []align(align_bytes) const T,
            else => unreachable,
        } {
            return ba.buffer[0..ba.len];
        }

        /// View the internal array as a constant slice whose size was previously set.
        pub fn constSlice(): []align(align_bytes) const T {
            return _.slice();
        }

        /// Adjust the slice's length to `len`.
        /// Does not initialize added items if any.
        pub fn resize(*_, len: usize): void ! Overflow {
            if (len > buffer_capacity) return error.Overflow;
            _.len = @intCast(len);
        }

        /// Remove all elements from the slice.
        pub fn clear(*_) {
            _.len = 0;
        }

        /// Copy the content of an existing slice.
        pub static fn fromSlice(m: []const T): Self ! Overflow {
            let list = Self.init(m.len)?;
            @memcpy(list.slice(), m);
            return list;
        }

        /// Return the element at index `i` of the slice.
        pub fn get(i: usize): T {
            return _.constSlice()[i];
        }

        /// Set the value of the element at index `i` of the slice.
        pub fn set(*_, i: usize, item: T) {
            _.slice()[i] = item;
        }

        /// Return the maximum length of a slice.
        pub fn capacity(): usize {
            return _.buffer.len;
        }

        /// Check that the slice can hold at least `additional_count` items.
        pub fn ensureUnusedCapacity(additional_count: usize): void ! Overflow {
            if (_.len + additional_count > buffer_capacity) {
                return error.Overflow;
            }
        }

        /// Increase length by 1, returning a pointer to the new item.
        pub fn addOne(*_): *T ! Overflow {
            _.ensureUnusedCapacity(1)?;
            return _.addOneAssumeCapacity();
        }

        /// Increase length by 1, returning pointer to the new item.
        /// Asserts that there is space for the new item.
        pub fn addOneAssumeCapacity(*_): *T {
            assert(_.len < buffer_capacity);
            _.len += 1;
            return &_.slice()[_.len - 1];
        }

        /// Resize the slice, adding `n` new elements, which have `undefined` values.
        /// The return value is a pointer to the array of uninitialized elements.
        pub fn addManyAsArray(*_, comptime n: usize): *align(align_bytes) [n]T ! Overflow {
            const prev_len = _.len;
            _.resize(@as(usize, _.len) + n)?;
            return _.slice()[prev_len..][0..n];
        }

        /// Resize the slice, adding `n` new elements, which have `undefined` values.
        /// The return value is a slice pointing to the uninitialized elements.
        pub fn addManyAsSlice(*_, n: usize): []align(align_bytes) T ! Overflow {
            const prev_len = _.len;
            _.resize(_.len + n)?;
            return _.slice()[prev_len..][0..n];
        }

        /// Remove and return the last element from the slice, or return `null` if the slice is empty.
        pub fn pop(*_): T? {
            if (_.len == 0) return null;
            const item = _.get(_.len - 1);
            _.len -= 1;
            return item;
        }

        /// Return a slice of only the extra capacity after items.
        /// This can be useful for writing directly into it.
        /// Note that such an operation must be followed up with a
        /// call to `resize()`
        pub fn unusedCapacitySlice(*_): []align(align_bytes) T {
            return _.buffer[_.len..];
        }

        /// Insert `item` at index `i` by moving `slice[n .. slice.len]` to make room.
        /// This operation is O(N).
        pub fn insert(*_, i: usize, item: T): void ! Overflow {
            if (i > _.len) {
                return error.Overflow;
            }
            * = _.addOne()?;
            let s = _.slice();
            mem.copyBackwards(T, s[i + 1 .. s.len], s[i .. s.len - 1]);
            _.buffer[i] = item;
        }

        /// Insert slice `items` at index `i` by moving `slice[i .. slice.len]` to make room.
        /// This operation is O(N).
        pub fn insertSlice(*_, i: usize, items: []const T): void ! Overflow {
            _.ensureUnusedCapacity(items.len)?;
            _.len += @as(Length, @intCast(items.len));
            mem.copyBackwards(T, _.slice()[i + items.len .. _.len], _.constSlice()[i .. _.len - items.len]);
            @memcpy(_.slice()[i..][0..items.len], items);
        }

        /// Replace range of elements `slice[start..][0..len]` with `new_items`.
        /// Grows slice if `len < new_items.len`.
        /// Shrinks slice if `len > new_items.len`.
        pub fn replaceRange(*_,
            start: usize,
            len: usize,
            new_items: []const T,
        ): void ! Overflow {
            const after_range = start + len;
            let range = _.slice()[start..after_range];

            if (range.len == new_items.len) {
                @memcpy(range[0..new_items.len], new_items);
            } else if (range.len < new_items.len) {
                const first = new_items[0..range.len];
                const rest = new_items[range.len..];
                @memcpy(range[0..first.len], first);
                _.insertSlice(after_range, rest)?;
            } else {
                @memcpy(range[0..new_items.len], new_items);
                const after_subrange = start + new_items.len;
                let idx: usize = 0;
                for (item of _.constSlice()[after_range..]) {
                    _.slice()[after_subrange..][idx] = item;
                    idx += 1;
                }
                _.len = @intCast(@as(usize, _.len) - @as(usize, len) - @as(usize, new_items.len));
            }
        }

        /// Extend the slice by 1 element.
        pub fn append(*_, item: T): void ! Overflow {
            const new_item_ptr = _.addOne()?;
            unsafe {
                new_item_ptr.* = item;
            }
        }

        /// Extend the slice by 1 element, asserting the capacity is already
        /// enough to store the new item.
        pub fn appendAssumeCapacity(*_, item: T) {
            const new_item_ptr = _.addOneAssumeCapacity();
            unsafe {
                new_item_ptr.* = item;
            }
        }

        /// Remove the element at index `i`, shift elements after index
        /// `i` forward, and return the removed element.
        /// Asserts the slice has at least one item.
        /// This operation is O(N).
        pub fn orderedRemove(*_, i: usize): T {
            const newlen = _.len - 1;
            if (newlen == i) return _.pop().?;
            const old_item = _.get(i);
            const newlen_usize: usize = newlen;
            let j: usize = i;
            while (j < newlen_usize) : (j += 1) {
                _.slice()[j] = _.get(j + 1);
            }
            _.set(newlen, undefined);
            _.len = newlen;
            return old_item;
        }

        /// Remove the element at the specified index and return it.
        /// The empty slot is filled from the end of the slice.
        /// This operation is O(1).
        pub fn swapRemove(*_, i: usize): T {
            if (_.len - 1 == i) return _.pop().?;
            const old_item = _.get(i);
            _.set(i, _.pop().?);
            return old_item;
        }

        /// Append the slice of items to the slice.
        pub fn appendSlice(*_, items: []const T): void ! Overflow {
            _.ensureUnusedCapacity(items.len)?;
            _.appendSliceAssumeCapacity(items);
        }

        /// Append the slice of items to the slice, asserting the capacity is already
        /// enough to store the new items.
        pub fn appendSliceAssumeCapacity(*_, items: []const T) {
            const old_len = _.len;
            const new_len: usize = old_len + @as(usize, items.len);
            _.len = @intCast(new_len);
            @memcpy(_.slice()[old_len..][0..items.len], items);
        }

        /// Append a value to the slice `n` times.
        /// Allocates more memory as necessary.
        pub fn appendNTimes(*_, value: T, n: usize): void ! Overflow {
            const old_len = _.len;
            _.resize(old_len + n)?;
            @memset(_.slice()[old_len.._.len], value);
        }

        /// Append a value to the slice `n` times.
        /// Asserts the capacity is enough.
        pub fn appendNTimesAssumeCapacity(*_, value: T, n: usize) {
            const old_len: usize = _.len;
            const new_len: usize = old_len + @as(usize, n);
            _.len = @intCast(new_len);
            assert(_.len <= buffer_capacity);
            @memset(_.slice()[old_len.._.len], value);
        }

        pub const Writer = if (T != u8)
            @compileError("The Writer interface is only defined for BoundedArray(u8, ...) " ++
                "but the given type is BoundedArray(" ++ @typeName(T) ++ ", ...)")
        else
            std.io.GenericWriter(*Self, Overflow, appendWrite);

        /// Initializes a writer which will write into the array.
        pub fn writer(*_): Writer {
            return { context: _ };
        }

        /// Same as `appendSlice` except it returns the number of bytes written, which is always the same
        /// as `m.len`. The purpose of this function existing is to match `std.io.GenericWriter` API.
        fn appendWrite(*_, m: []const u8): usize ! Overflow {
            _.appendSlice(m)?;
            return m.len;
        }
    };
}
