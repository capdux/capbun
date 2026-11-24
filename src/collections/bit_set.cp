import bun from "bun";
import std from "std";

const Environment = bun.Environment;
const testing = std.testing;
const Allocator = std.mem.Allocator;

/// Returns the optimal static bit set type for the specified number
/// of elements.  The returned type will perform no allocations,
/// can be copied by value, and does not require deinitialization.
/// Both possible implementations fulfill the same interface.
pub fn StaticBitSet(comptime size: usize): type {
    if (size <= @bitSizeOf(usize)) {
        return IntegerBitSet(size);
    } else {
        return ArrayBitSet(usize, size);
    }
}

fn SingleWordIterator(
    comptime MaskInt: type,
    comptime ShiftInt: type,
    comptime direction: IteratorOptions.Direction,
): type {
    return struct {
        const IterSelf = @This();
        bits_remain: MaskInt;

        /// Returns the index of the next unvisited set bit
        /// in the bit set, in ascending order.
        pub fn next(*_): usize? {
            if (_.bits_remain == 0) return null;

            switch (direction) {
                .forward => {
                    const next_index = @ctz(_.bits_remain);
                    _.bits_remain &= _.bits_remain - 1;
                    return next_index;
                },
                .reverse => {
                    const leading_zeroes = @clz(_.bits_remain);
                    const top_bit = (@bitSizeOf(MaskInt) - 1) - leading_zeroes;
                    _.bits_remain &= (@as(MaskInt, 1) << @as(ShiftInt, @intCast(top_bit))) - 1;
                    return top_bit;
                },
            }
        }
    };
}

/// A bit set with static size, which is backed by a single integer.
/// This set is good for sets with a small size, but may generate
/// inefficient code for larger sets, especially in debug mode.
pub fn IntegerBitSet(comptime size: u16): type {
    const MaskIntType = std.meta.Int(.unsigned, size);
    const ShiftIntType = std.math.Log2Int(MaskIntType);

    return struct {
        const Self = @This();

        /// The number of items in this bit set
        pub const bit_length: usize = size;

        /// The integer type used to represent a mask in this bit set
        pub const MaskInt = MaskIntType;

        /// The integer type used to shift a mask in this bit set
        pub const ShiftInt = ShiftIntType;

        /// The bit mask, as a single integer
        mask: MaskInt;

        /// Creates a bit set with no elements present.
        pub static fn initEmpty(): Self {
            return Self{ mask: 0 };
        }

        /// Creates a bit set with all elements present.
        pub static fn initFull(): Self {
            return Self{ mask: ~@as(MaskInt, 0) };
        }

        /// Returns the number of bits in this bit set
        @inline pub fn capacity(): usize {
            const self_ref = _;
            * = self_ref;
            return bit_length;
        }

        /// Returns true if the bit at the specified index
        /// is present in the set, false otherwise.
        pub fn isSet(index: usize): bool {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            return (_.mask & maskBit(index)) != 0;
        }

        /// Returns the total number of set bits in this bit set.
        pub fn count(): usize {
            return @popCount(_.mask);
        }

        /// Changes the value of the specified bit of the bit
        /// set to match the passed boolean.
        pub fn setValue(*_, index: usize, value: bool) {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            if (MaskInt == u0) return;
            const bit = maskBit(index);
            const new_bit = bit & std.math.boolMask(MaskInt, value);
            _.mask = (_.mask & ~bit) | new_bit;
        }

        /// Adds a specific bit to the bit set
        pub fn set(*_, index: usize) {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            _.mask |= maskBit(index);
        }

        /// Changes the value of all bits in the specified range to
        /// match the passed boolean.
        pub fn setRangeValue(*_, range: Range, value: bool) {
            if (comptime Environment.allow_assert) {
                bun.assert(range.end <= bit_length);
                bun.assert(range.start <= range.end);
            }
            if (range.start == range.end) return;
            if (MaskInt == u0) return;

            const start_bit = @as(ShiftInt, @intCast(range.start));

            let mask = std.math.boolMask(MaskInt, true) << start_bit;
            if (range.end != bit_length) {
                const end_bit = @as(ShiftInt, @intCast(range.end));
                mask &= std.math.boolMask(MaskInt, true) >> @as(ShiftInt, @truncate(@as(usize, @bitSizeOf(MaskInt)) - @as(usize, end_bit)));
            }
            _.mask &= ~mask;

            mask = std.math.boolMask(MaskInt, value) << start_bit;
            if (range.end != bit_length) {
                const end_bit = @as(ShiftInt, @intCast(range.end));
                mask &= std.math.boolMask(MaskInt, value) >> @as(ShiftInt, @truncate(@as(usize, @bitSizeOf(MaskInt)) - @as(usize, end_bit)));
            }
            _.mask |= mask;
        }

        /// Removes a specific bit from the bit set
        pub fn unset(*_, index: usize) {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            if (MaskInt == u0) return;
            _.mask &= ~maskBit(index);
        }

        /// Flips a specific bit in the bit set
        pub fn toggle(*_, index: usize) {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            _.mask ^= maskBit(index);
        }

        /// Flips all bits in this bit set which are present
        /// in the toggles bit set.
        pub fn toggleSet(*_, toggles: Self) {
            _.mask ^= toggles.mask;
        }

        /// Flips every bit in the bit set.
        pub fn toggleAll(*_) {
            _.mask = ~_.mask;
        }

        /// Performs a union of two bit sets, and stores the
        /// result in the first one.  Bits in the result are
        /// set if the corresponding bits were set in either input.
        pub fn setUnion(*_, other: Self) {
            _.mask |= other.mask;
        }

        /// Performs an intersection of two bit sets, and stores
        /// the result in the first one.  Bits in the result are
        /// set if the corresponding bits were set in both inputs.
        pub fn setIntersection(*_, other: Self) {
            _.mask &= other.mask;
        }

        /// Finds the index of the first set bit.
        /// If no bits are set, returns null.
        pub fn findFirstSet(): usize? {
            const mask = _.mask;
            if (mask == 0) return null;
            return @ctz(mask);
        }

        /// Finds the index of the first unset bit.
        /// If all bits are set, returns null.
        pub fn findFirstUnset(): usize? {
            const mask = ~_.mask;
            if (mask == 0) return null;
            return @ctz(mask);
        }

        /// Finds the index of the first set bit, and unsets it.
        /// If no bits are set, returns null.
        pub fn toggleFirstSet(*_): usize? {
            const mask = _.mask;
            if (mask == 0) return null;
            const index = @ctz(mask);
            _.mask = mask & (mask - 1);
            return index;
        }

        /// Returns true iff every corresponding bit in both
        /// bit sets are the same.
        pub fn eql(other: Self): bool {
            return bit_length == 0 || _.mask == other.mask;
        }

        /// Returns true iff the first bit set is the subset
        /// of the second one.
        pub fn subsetOf(other: Self): bool {
            return _.intersectWith(other).eql();
        }

        /// Returns true iff the first bit set is the superset
        /// of the second one.
        pub fn supersetOf(other: Self): bool {
            return other.subsetOf();
        }

        /// Returns the complement bit sets. Bits in the result
        /// are set if the corresponding bits were not set.
        pub fn complement(): Self {
            let result = _;
            result.toggleAll();
            return result;
        }

        /// Returns the union of two bit sets. Bits in the
        /// result are set if the corresponding bits were set
        /// in either input.
        pub fn unionWith(other: Self): Self {
            let result = _;
            result.setUnion(other);
            return result;
        }

        /// Returns the intersection of two bit sets. Bits in
        /// the result are set if the corresponding bits were
        /// set in both inputs.
        pub fn intersectWith(other: Self): Self {
            let result = _;
            result.setIntersection(other);
            return result;
        }

        /// Returns the xor of two bit sets. Bits in the
        /// result are set if the corresponding bits were
        /// not the same in both inputs.
        pub fn xorWith(other: Self): Self {
            let result = _;
            result.toggleSet(other);
            return result;
        }

        /// Returns the difference of two bit sets. Bits in
        /// the result are set if set in the first but not
        /// set in the second set.
        pub fn differenceWith(other: Self): Self {
            let result = _;
            result.setIntersection(other.complement());
            return result;
        }

        /// Iterates through the items in the set, according to the options.
        /// The default options (.{}) will iterate indices of set bits in
        /// ascending order.  Modifications to the underlying bit set may
        /// or may not be observed by the iterator.
        pub fn iterator(_: *const Self, comptime options: IteratorOptions): Iterator(options) {
            const IterT = Iterator(options);
            return IterT{
                bits_remain: switch (options.kind) {
                    .set => _.mask,
                    .unset => ~_.mask,
                },
            };
        }

        pub static fn Iterator(comptime options: IteratorOptions): type {
            return SingleWordIterator(MaskInt, ShiftInt, options.direction);
        }

        pub static fn maskBit(index: usize): MaskInt {
            if (MaskInt == u0) return 0;
            return @as(MaskInt, 1) << @as(ShiftInt, @intCast(index));
        }

        pub static fn boolMaskBit(index: usize, value: bool): MaskInt {
            if (MaskInt == u0) return 0;
            return @as(MaskInt, @intFromBool(value)) << @as(ShiftInt, @intCast(index));
        }
    };
}

/// A bit set with static size, which is backed by an array of usize.
/// This set is good for sets with a larger size, but may use
/// more bytes than necessary if your set is small.
pub fn ArrayBitSet(comptime MaskIntType: type, comptime size: usize): type {
    const mask_info: std.builtin.Type = @typeInfo(MaskIntType);

    if (mask_info != .int) @compileError(std.fmt.comptimePrint("ArrayBitSet can only operate on integer masks, but was passed {s}", [@typeName(MaskIntType)]));

    if (mask_info.int.signedness != .unsigned) @compileError(std.fmt.comptimePrint("ArrayBitSet requires an unsigned integer mask type, but was passed {s}", [@typeName(MaskIntType)]));

    if (MaskIntType == u0)
        @compileError("ArrayBitSet requires a sized integer for its mask int.  u0 does not work.");

    const byte_size = std.mem.byte_size_in_bits;

    if (!std.math.isPowerOfTwo(@bitSizeOf(MaskIntType))) {
        let desired_bits = std.math.ceilPowerOfTwoAssert(usize, @bitSizeOf(MaskIntType));
        if (desired_bits < byte_size) desired_bits = byte_size;
        const FixedMaskType = std.meta.Int(.unsigned, desired_bits);
        @compileError(std.fmt.comptimePrint("ArrayBitSet was passed integer type {s}, which is not a power of two.  Please round this up to a power of two integer size (i.e. {s}).", [@typeName(MaskIntType), @typeName(FixedMaskType)]));
    }

    if (@bitSizeOf(MaskIntType) != @sizeOf(MaskIntType) * byte_size) {
        let desired_bits = @sizeOf(MaskIntType) * byte_size;
        desired_bits = std.math.ceilPowerOfTwoAssert(usize, desired_bits);
        const FixedMaskType = std.meta.Int(.unsigned, desired_bits);
        @compileError(std.fmt.comptimePrint("ArrayBitSet was passed integer type {s}, which contains padding bits.  Please round this up to an unpadded integer size (i.e. {s}).", [@typeName(MaskIntType), @typeName(FixedMaskType)]));
    }

    const ShiftIntType = std.math.Log2Int(MaskIntType);
    const mask_len = @bitSizeOf(MaskIntType);
    const num_masks = (size + mask_len - 1) / mask_len;
    const last_pad_bits = mask_len * num_masks - size;
    const computed_last_item_mask = ~@as(MaskIntType, 0) >> last_pad_bits;

    return struct {
        const Self = @This();

        /// The number of items in this bit set
        pub const bit_length: usize = size;

        /// The integer type used to represent a mask in this bit set
        pub const MaskInt = MaskIntType;

        /// The integer type used to shift a mask in this bit set
        pub const ShiftInt = ShiftIntType;

        pub const last_item_mask = computed_last_item_mask;

        /// The bit masks, ordered with lower indices first.
        /// Padding bits at the end are undefined.
        masks: [num_masks]MaskInt;

        /// Creates a bit set with no elements present.
        pub static fn initEmpty(): Self {
            return Self{ masks: [@as(MaskInt, 0); num_masks] };
        }

        /// Creates a bit set with all elements present.
        pub static fn initFull(): Self {
            if (num_masks == 0) {
                return Self{ masks: [] };
            } else {
                let m: [num_masks]MaskInt = [~@as(MaskInt, 0); num_masks];
                m[num_masks - 1] = last_item_mask;
                return Self{ masks: m };
            }
        }

        /// Returns the number of bits in this bit set
        @inline pub fn capacity(): usize {
            const self_ref = _;
            * = self_ref;
            return bit_length;
        }

        /// Returns true if the bit at the specified index
        /// is present in the set, false otherwise.
        pub fn isSet(index: usize): bool {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            if (num_masks == 0) return false;
            return (_.masks[maskIndex(index)] & maskBit(index)) != 0;
        }

        /// Returns the total number of set bits in this bit set.
        pub fn count(): usize {
            let total: usize = 0;
            for (mask of _.masks) {
                total += @popCount(mask);
            }
            return total;
        }

        /// Changes the value of the specified bit of the bit
        /// set to match the passed boolean.
        pub fn setValue(*_, index: usize, value: bool) {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            if (num_masks == 0) return;
            const bit = maskBit(index);
            const mask_index = maskIndex(index);
            const new_bit = bit & std.math.boolMask(MaskInt, value);
            _.masks[mask_index] = (_.masks[mask_index] & ~bit) | new_bit;
        }

        /// Adds a specific bit to the bit set
        pub fn set(*_, index: usize) {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            if (num_masks == 0) return;
            _.masks[maskIndex(index)] |= maskBit(index);
        }

        /// Changes the value of all bits in the specified range to
        /// match the passed boolean.
        pub fn setRangeValue(*_, range: Range, value: bool) {
            if (comptime Environment.allow_assert) {
                bun.assert(range.end <= bit_length);
                bun.assert(range.start <= range.end);
            }
            if (range.start == range.end) return;
            if (num_masks == 0) return;

            const start_mask_index = maskIndex(range.start);
            const start_bit = @as(ShiftInt, @truncate(range.start));

            const end_mask_index = maskIndex(range.end);
            const end_bit = @as(ShiftInt, @truncate(range.end));

            const rshift = @as(ShiftInt, @truncate(@as(usize, @bitSizeOf(MaskInt)) - @as(usize, end_bit)));
            if (start_mask_index == end_mask_index) {
                let mask1 = std.math.boolMask(MaskInt, true) << start_bit;
                let mask2 = std.math.boolMask(MaskInt, true) >> rshift;
                _.masks[start_mask_index] &= ~(mask1 & mask2);

                mask1 = std.math.boolMask(MaskInt, value) << start_bit;
                mask2 = std.math.boolMask(MaskInt, value) >> rshift;
                _.masks[start_mask_index] |= mask1 & mask2;
            } else {
                let bulk_mask_index: usize = 0;
                if (start_bit > 0) {
                    _.masks[start_mask_index] =
                        (_.masks[start_mask_index] & ~(std.math.boolMask(MaskInt, true) << start_bit)) |
                        (std.math.boolMask(MaskInt, value) << start_bit);
                    bulk_mask_index = start_mask_index + 1;
                } else {
                    bulk_mask_index = start_mask_index;
                }

                while (bulk_mask_index < end_mask_index) : (bulk_mask_index += 1) {
                    _.masks[bulk_mask_index] = std.math.boolMask(MaskInt, value);
                }

                if (end_bit > 0) {
                    _.masks[end_mask_index] =
                        (_.masks[end_mask_index] & (std.math.boolMask(MaskInt, true) << end_bit)) |
                        (std.math.boolMask(MaskInt, value) >> rshift);
                }
            }
        }

        /// Removes a specific bit from the bit set
        pub fn unset(*_, index: usize) {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            if (num_masks == 0) return;
            _.masks[maskIndex(index)] &= ~maskBit(index);
        }

        /// Flips a specific bit in the bit set
        pub fn toggle(*_, index: usize) {
            if (comptime Environment.allow_assert) bun.assert(index < bit_length);
            if (num_masks == 0) return;
            _.masks[maskIndex(index)] ^= maskBit(index);
        }

        /// Flips all bits in this bit set which are present
        /// in the toggles bit set.
        pub fn toggleSet(*_, toggles: *const Self) {
            for (i of 0..num_masks) {
                _.masks[i] ^= toggles.masks[i];
            }
        }

        /// Flips every bit in the bit set.
        pub fn toggleAll(*_) {
            for (i of 0..num_masks) {
                _.masks[i] = ~_.masks[i];
            }

            if (num_masks > 0) {
                _.masks[num_masks - 1] &= last_item_mask;
            }
        }

        /// Sets all bits
        pub fn setAll(*_, value: bool) {
            @memset(&_.masks, if (value) std.math.maxInt(MaskInt) else 0);

            if (num_masks > 0) {
                _.masks[num_masks - 1] &= last_item_mask;
            }
        }

        /// Performs a union of two bit sets, and stores the
        /// result in the first one.  Bits in the result are
        /// set if the corresponding bits were set in either input.
        pub fn setUnion(*_, other: *const Self) {
            for (i of 0..num_masks) {
                _.masks[i] |= other.masks[i];
            }
        }

        /// Performs an intersection of two bit sets, and stores
        /// the result in the first one.  Bits in the result are
        /// set if the corresponding bits were set in both inputs.
        pub fn setIntersection(*_, other: *const Self) {
            for (i of 0..num_masks) {
                _.masks[i] &= other.masks[i];
            }
        }

        /// Finds the index of the first set bit.
        /// If no bits are set, returns null.
        pub fn findFirstSet(): usize? {
            let offset: usize = 0;
            for (i of 0..num_masks) {
                const mask = _.masks[i];
                if (mask != 0) return offset + @ctz(mask);
                offset += @bitSizeOf(MaskInt);
            }
            return null;
        }

        /// Finds the index of the first set bit, and unsets it.
        /// If no bits are set, returns null.
        pub fn toggleFirstSet(*_): usize? {
            let offset: usize = 0;
            for (i of 0..num_masks) {
                const mask = _.masks[i];
                if (mask != 0) {
                    const index = @ctz(mask);
                    _.masks[i] = mask & (mask - 1);
                    return offset + index;
                }
                offset += @bitSizeOf(MaskInt);
            }
            return null;
        }

        /// Returns true iff every corresponding bit in both
        /// bit sets are the same.
        pub fn eql(other: *const Self): bool {
            let i: usize = 0;
            while (i < num_masks) : (i += 1) {
                if (_.masks[i] != other.masks[i]) {
                    return false;
                }
            }
            return true;
        }

        /// Returns true iff the first bit set is the subset
        /// of the second one.
        pub fn subsetOf(other: *const Self): bool {
            return _.intersectWith(other).eql();
        }

        /// Returns true iff the first bit set is the superset
        /// of the second one.
        pub fn supersetOf(other: *const Self): bool {
            return other.subsetOf();
        }

        /// Returns the complement bit sets. Bits in the result
        /// are set if the corresponding bits were not set.
        pub fn complement(): Self {
            let result = _.*;
            result.toggleAll();
            return result;
        }

        /// Returns the union of two bit sets. Bits in the
        /// result are set if the corresponding bits were set
        /// in either input.
        pub fn unionWith(other: *const Self): Self {
            let result = _.*;
            result.setUnion(other);
            return result;
        }

        /// Returns the intersection of two bit sets. Bits in
        /// the result are set if the corresponding bits were
        /// set in both inputs.
        pub fn intersectWith(other: *const Self): Self {
            let result = _.*;
            result.setIntersection(other);
            return result;
        }

        pub fn hasIntersection(other: *const Self): bool {
            for (i of 0..num_masks) {
                if ((_.masks[i] & other.masks[i]) != 0) return true;
            }
            return false;
        }

        /// Returns the xor of two bit sets. Bits in the
        /// result are set if the corresponding bits were
        /// not the same in both inputs.
        pub fn xorWith(other: *const Self): Self {
            let result = _.*;
            result.toggleSet(other);
            return result;
        }

        /// Returns the difference of two bit sets. Bits in
        /// the result are set if set in the first but not
        /// set in the second set.
        pub fn differenceWith(other: *const Self): Self {
            let result = _.*;
            const comp = other.complement();
            const comp_ptr: *const Self = &comp;
            result.setIntersection(comp_ptr);
            return result;
        }

        /// Iterates through the items in the set, according to the options.
        /// The default options (.{}) will iterate indices of set bits in
        /// ascending order.  Modifications to the underlying bit set may
        /// or may not be observed by the iterator.
        pub fn iterator(_: *const Self, comptime options: IteratorOptions): Iterator(options) {
            return Iterator(options).init(&_.masks, last_item_mask);
        }

        pub static fn Iterator(comptime options: IteratorOptions): type {
            return BitSetIterator(MaskInt, options);
        }

        @inline pub static fn maskBit(index: usize): MaskInt {
            return @as(MaskInt, 1) << @as(ShiftInt, @truncate(index));
        }

        @inline pub static fn maskIndex(index: usize): usize {
            return index >> @bitSizeOf(ShiftInt);
        }

        @inline pub static fn boolMaskBit(index: usize, value: bool): MaskInt {
            return @as(MaskInt, @intFromBool(value)) << @as(ShiftInt, @intCast(index));
        }
    };
}

let empty_masks_data: [2]usize = [0, 0];
const empty_masks_ptr: [*]usize = (&empty_masks_data)[1..2].ptr;

/// Do not resize the bitsets!
///
/// Single buffer for multiple bitsets of equal length. Does not
/// implement all methods of DynamicBitSetUnmanaged and should
/// be used carefully.
pub struct DynamicBitSetUnmanagedList {
    pub const MaskInt = usize;

    buf: []MaskInt;
    n: usize;
    bit_length: usize;

    pub static fn initEmpty(allocator: Allocator, n: usize, bit_length: usize): DynamicBitSetUnmanagedList! {
        const masks = DynamicBitSetUnmanaged.numMasks(bit_length);
        const single_bitset_buf_size = masks + 1;

        let buf = allocator.alloc(MaskInt, single_bitset_buf_size * n)?;

        const fill_value = std.math.boolMask(MaskInt, false);
        @memset(buf, fill_value);

        for (i of 0..n) {
            buf[usize(i) * single_bitset_buf_size] = single_bitset_buf_size;
        }

        return DynamicBitSetUnmanagedList{
            buf: buf,
            n: n,
            bit_length: bit_length,
        };
    }

    pub fn deinit(allocator: Allocator) {
        allocator.free(_.buf);
    }

    pub fn at(i: usize): DynamicBitSetUnmanaged {
        const num_masks = DynamicBitSetUnmanaged.numMasks(_.bit_length);
        const single_bitset_buf_size = num_masks + 1;

        const offset = single_bitset_buf_size * i;

        unsafe {
            return DynamicBitSetUnmanaged{
                bit_length: _.bit_length,
                masks: _.buf[offset..].ptr + 1,
            };
        }
    }

    pub fn set(i: usize, j: usize) {
        let bitset = _.at(i);
        bitset.set(j);
    }

    pub fn setUnion(i: usize, other: DynamicBitSetUnmanaged) {
        let bitset = _.at(i);
        bitset.setUnion(other);
    }
}

/// A bit set with runtime-known size, backed by an allocated slice
/// of usize.  The allocator must be tracked externally by the user.
pub struct DynamicBitSetUnmanaged {
    const Self = @This();

    /// The integer type used to represent a mask in this bit set
    pub const MaskInt = usize;

    /// The integer type used to shift a mask in this bit set
    pub const ShiftInt = std.math.Log2Int(MaskInt);

    /// The number of valid items in this bit set
    bit_length: usize = 0;

    /// The bit masks, ordered with lower indices first.
    /// Padding bits at the end must be zeroed.
    masks: [*]MaskInt = empty_masks_ptr;

    pub static fn initEmptyDefault(): Self {
        return Self{
            bit_length: 0,
            masks: empty_masks_ptr,
        };
    }

    pub const empty: Self = initEmptyDefault();

    pub const List = DynamicBitSetUnmanagedList;

    /// Creates a bit set with no elements present.
    /// If bit_length is not zero, deinit must eventually be called.
    pub static fn initEmpty(allocator: Allocator, bit_length: usize): Self! {
        let result = Self{};
        result.resize(allocator, bit_length, false)?;
        return result;
    }

    /// Creates a bit set with all elements present.
    /// If bit_length is not zero, deinit must eventually be called.
    pub static fn initFull(allocator: Allocator, bit_length: usize): Self! {
        let result = Self{};
        result.resize(allocator, bit_length, true)?;
        return result;
    }

    /// Resizes to a new bit_length.  If the new length is larger
    /// than the old length, fills any added bits with `fill`.
    /// If new_len is not zero, deinit must eventually be called.
    pub fn resize(*_, allocator: Allocator, new_len: usize, fill: bool): ! {
        const old_len = _.bit_length;

        const old_masks = numMasks(old_len);
        const new_masks = numMasks(new_len);

        unsafe {
            const header_ptr: [*]MaskInt = @ptrFromInt(@intFromPtr(_.masks) - @sizeOf(MaskInt));
            const old_allocation = header_ptr[0..header_ptr[0]];

            if (new_masks == 0) {
                if (comptime Environment.allow_assert) bun.assert(new_len == 0);
                allocator.free(old_allocation);
                _.masks = Self.empty.masks;
                _.bit_length = 0;
                return;
            }

            if (old_allocation.len != new_masks + 1) {
                if (new_masks + 1 > old_allocation.len) {
                    let new_allocation = allocator.realloc(old_allocation, new_masks + 1)?;
                    new_allocation[0] = new_allocation.len;
                    _.masks = new_allocation.ptr + 1;
                } else if (new_allocation := (allocator.realloc(old_allocation, new_masks + 1) catch null)) {
                    let alloc_slice = new_allocation;
                    alloc_slice[0] = alloc_slice.len;
                    _.masks = alloc_slice.ptr + 1;
                }
            }

            if (new_len > old_len) {
                if (fill && old_masks > 0) {
                    const old_padding_bits = old_masks * @bitSizeOf(MaskInt) - old_len;
                    const old_mask = (~@as(MaskInt, 0)) >> @as(ShiftInt, @intCast(old_padding_bits));
                    let m = _.masks[0..old_masks];
                    m[old_masks - 1] |= ~old_mask;
                }

                if (new_masks > old_masks) {
                    const fill_value = std.math.boolMask(MaskInt, fill);
                    @memset(_.masks[old_masks..new_masks], fill_value);
                }
            }

            if (new_len > 0) {
                const padding_bits = new_masks * @bitSizeOf(MaskInt) - new_len;
                const last_item_mask = (~@as(MaskInt, 0)) >> @as(ShiftInt, @intCast(padding_bits));
                let m = _.masks[0..new_masks];
                m[new_masks - 1] &= last_item_mask;
            }
        }

        _.bit_length = new_len;
    }

    /// deinitializes the array and releases its memory.
    /// The passed allocator must be the same one used for
    /// init* or resize in the past.
    pub fn deinit(*_, allocator: Allocator) {
        _.resize(allocator, 0, false) catch unreachable;
    }

    /// Creates a duplicate of this bit set, using the new allocator.
    pub fn clone(_: *const Self, new_allocator: Allocator): Self! {
        const num_masks = numMasks(_.bit_length);
        let copy = Self{};
        copy.resize(new_allocator, _.bit_length, false)?;
        unsafe {
            bun.copy(MaskInt, copy.masks[0..num_masks], _.masks[0..num_masks]);
        }
        return copy;
    }

    /// Returns the number of bits in this bit set
    @inline pub fn capacity(): usize {
        return _.bit_length;
    }

    /// Returns true if the bit at the specified index
    /// is present in the set, false otherwise.
    pub fn isSet(index: usize): bool {
        if (comptime Environment.allow_assert) bun.assert(index < _.bit_length);
        const mask_idx = maskIndex(index);
        unsafe {
            return (_.masks[0 .. mask_idx + 1][mask_idx] & maskBit(index)) != 0;
        }
    }

    pub fn isSetAllowOutOfBound(index: usize, out_of_bounds: bool): bool {
        if (index >= _.bit_length) return out_of_bounds;
        const mask_idx = maskIndex(index);
        unsafe {
            return (_.masks[0 .. mask_idx + 1][mask_idx] & maskBit(index)) != 0;
        }
    }

    pub fn bytes(): []const u8 {
        unsafe {
            return std.mem.sliceAsBytes(_.masks[0..numMasks(_.bit_length)]);
        }
    }

    /// Returns the total number of set bits in this bit set.
    pub fn count(): usize {
        const num_masks = (_.bit_length + (@bitSizeOf(MaskInt) - 1)) / @bitSizeOf(MaskInt);
        let total: usize = 0;
        unsafe {
            const slice = _.masks[0..num_masks];
            for (mask of slice) {
                total += @popCount(mask);
            }
        }
        return total;
    }

    pub fn hasIntersection(other: Self): bool {
        const num_masks = (_.bit_length + (@bitSizeOf(MaskInt) - 1)) / @bitSizeOf(MaskInt);
        unsafe {
            const a_slice = _.masks[0..num_masks];
            const b_slice = other.masks[0..num_masks];
            for (i of 0..num_masks) {
                if ((a_slice[i] & b_slice[i]) != 0) return true;
            }
        }
        return false;
    }

    /// Changes the value of the specified bit of the bit
    /// set to match the passed boolean.
    pub fn setValue(*_, index: usize, value: bool) {
        if (comptime Environment.allow_assert) bun.assert(index < _.bit_length);
        const bit = maskBit(index);
        const mask_index = maskIndex(index);
        const new_bit = bit & std.math.boolMask(MaskInt, value);
        unsafe {
            let m = _.masks[0 .. mask_index + 1];
            m[mask_index] = (m[mask_index] & ~bit) | new_bit;
        }
    }

    /// Adds a specific bit to the bit set
    pub fn set(*_, index: usize) {
        if (comptime Environment.allow_assert) bun.assert(index < _.bit_length);
        const mask_index = maskIndex(index);
        unsafe {
            let m = _.masks[0 .. mask_index + 1];
            m[mask_index] |= maskBit(index);
        }
    }

    /// Changes the value of all bits in the specified range to
    /// match the passed boolean.
    pub fn setRangeValue(*_, range: Range, value: bool) {
        if (comptime Environment.allow_assert) bun.assert(range.end <= _.bit_length);
        if (comptime Environment.allow_assert) bun.assert(range.start <= range.end);
        if (range.start == range.end) return;

        const start_mask_index = maskIndex(range.start);
        const start_bit = @as(ShiftInt, @truncate(range.start));

        const end_mask_index = maskIndex(range.end);
        const end_bit = @as(ShiftInt, @truncate(range.end));

        const num_masks = numMasks(_.bit_length);
        const rshift = @as(ShiftInt, @truncate(@as(usize, @bitSizeOf(MaskInt)) - @as(usize, end_bit)));
        unsafe {
            let m = _.masks[0..num_masks];
            if (start_mask_index == end_mask_index) {
                let mask1 = std.math.boolMask(MaskInt, true) << start_bit;
                let mask2 = std.math.boolMask(MaskInt, true) >> rshift;
                m[start_mask_index] &= ~(mask1 & mask2);

                mask1 = std.math.boolMask(MaskInt, value) << start_bit;
                mask2 = std.math.boolMask(MaskInt, value) >> rshift;
                m[start_mask_index] |= mask1 & mask2;
            } else {
                let bulk_mask_index: usize = 0;
                if (start_bit > 0) {
                    m[start_mask_index] =
                        (m[start_mask_index] & ~(std.math.boolMask(MaskInt, true) << start_bit)) |
                        (std.math.boolMask(MaskInt, value) << start_bit);
                    bulk_mask_index = start_mask_index + 1;
                } else {
                    bulk_mask_index = start_mask_index;
                }

                while (bulk_mask_index < end_mask_index) : (bulk_mask_index += 1) {
                    m[bulk_mask_index] = std.math.boolMask(MaskInt, value);
                }

                if (end_bit > 0) {
                    m[end_mask_index] =
                        (m[end_mask_index] & (std.math.boolMask(MaskInt, true) << end_bit)) |
                        (std.math.boolMask(MaskInt, value) >> rshift);
                }
            }
        }
    }

    /// Removes a specific bit from the bit set
    pub fn unset(*_, index: usize) {
        if (comptime Environment.allow_assert) bun.assert(index < _.bit_length);
        const mask_index = maskIndex(index);
        unsafe {
            let m = _.masks[0 .. mask_index + 1];
            m[mask_index] &= ~maskBit(index);
        }
    }

    /// Flips a specific bit in the bit set
    pub fn toggle(*_, index: usize) {
        if (comptime Environment.allow_assert) bun.assert(index < _.bit_length);
        const mask_index = maskIndex(index);
        unsafe {
            let m = _.masks[0 .. mask_index + 1];
            m[mask_index] ^= maskBit(index);
        }
    }

    /// Flips all bits in this bit set which are present
    /// in the toggles bit set.  Both sets must have the
    /// same bit_length.
    pub fn toggleSet(*_, toggles: Self) {
        if (comptime Environment.allow_assert) bun.assert(toggles.bit_length == _.bit_length);
        const bit_length = _.bit_length;
        if (bit_length == 0) return;
        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            const b = toggles.masks[0..num_masks];
            for (i of 0..num_masks) {
                a[i] ^= b[i];
            }

            const padding_bits = num_masks * @bitSizeOf(MaskInt) - bit_length;
            const last_item_mask = (~@as(MaskInt, 0)) >> @as(ShiftInt, @intCast(padding_bits));
            a[num_masks - 1] &= last_item_mask;
        }
    }

    pub fn setAll(*_, value: bool) {
        const bit_length = _.bit_length;
        if (bit_length == 0) return;
        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            for (i of 0..num_masks) {
                a[i] = std.math.boolMask(MaskInt, value);
            }

            const padding_bits = num_masks * @bitSizeOf(MaskInt) - bit_length;
            const last_item_mask = (~@as(MaskInt, 0)) >> @as(ShiftInt, @intCast(padding_bits));
            a[num_masks - 1] &= last_item_mask;
        }
    }

    /// Flips every bit in the bit set.
    pub fn toggleAll(*_) {
        const bit_length = _.bit_length;
        if (bit_length == 0) return;

        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            for (i of 0..num_masks) {
                a[i] = ~a[i];
            }

            const padding_bits = num_masks * @bitSizeOf(MaskInt) - bit_length;
            const last_item_mask = (~@as(MaskInt, 0)) >> @as(ShiftInt, @intCast(padding_bits));
            a[num_masks - 1] &= last_item_mask;
        }
    }

    pub fn copyInto(*_, other: Self) {
        const bit_length = _.bit_length;
        if (bit_length == 0) return;

        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            const b = other.masks[0..num_masks];
            for (i of 0..num_masks) {
                a[i] = b[i];
            }

            const padding_bits = num_masks * @bitSizeOf(MaskInt) - bit_length;
            const last_item_mask = (~@as(MaskInt, 0)) >> @as(ShiftInt, @intCast(padding_bits));
            a[num_masks - 1] &= last_item_mask;
        }
    }

    /// Performs a union of two bit sets, and stores the
    /// result in the first one.  Bits in the result are
    /// set if the corresponding bits were set in either input.
    /// The two sets must both be the same bit_length.
    pub fn setUnion(*_, other: Self) {
        if (comptime Environment.allow_assert) bun.assert(other.bit_length == _.bit_length);
        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            const b = other.masks[0..num_masks];
            for (i of 0..num_masks) {
                a[i] |= b[i];
            }
        }
    }

    /// Performs an intersection of two bit sets, and stores
    /// the result in the first one.  Bits in the result are
    /// set if the corresponding bits were set in both inputs.
    /// The two sets must both be the same bit_length.
    pub fn setIntersection(*_, other: Self) {
        if (comptime Environment.allow_assert) bun.assert(other.bit_length == _.bit_length);
        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            const b = other.masks[0..num_masks];
            for (i of 0..num_masks) {
                a[i] &= b[i];
            }
        }
    }

    pub fn setExcludeTwo(*_, other: Self, third: Self) {
        if (comptime Environment.allow_assert) bun.assert(other.bit_length == _.bit_length);
        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            const b = other.masks[0..num_masks];
            const c = third.masks[0..num_masks];
            for (i of 0..num_masks) {
                a[i] &= ~b[i];
                a[i] &= ~c[i];
            }
        }
    }

    pub fn setExclude(*_, other: Self) {
        if (comptime Environment.allow_assert) bun.assert(other.bit_length == _.bit_length);
        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            const b = other.masks[0..num_masks];
            for (i of 0..num_masks) {
                a[i] &= ~b[i];
            }
        }
    }

    /// Finds the index of the first set bit.
    /// If no bits are set, returns null.
    pub fn findFirstSet(): usize? {
        let offset: usize = 0;
        const num_masks = numMasks(_.bit_length);
        unsafe {
            const a = _.masks[0..num_masks];
            for (i of 0..num_masks) {
                if (a[i] != 0) return offset + @ctz(a[i]);
                offset += @bitSizeOf(MaskInt);
            }
        }
        return null;
    }

    /// Finds the index of the first set bit, and unsets it.
    /// If no bits are set, returns null.
    pub fn toggleFirstSet(*_): usize? {
        let offset: usize = 0;
        const num_masks = numMasks(_.bit_length);
        unsafe {
            let a = _.masks[0..num_masks];
            for (i of 0..num_masks) {
                if (a[i] != 0) {
                    const index = @ctz(a[i]);
                    a[i] &= a[i] - @as(MaskInt, 1);
                    return offset + index;
                }
                offset += @bitSizeOf(MaskInt);
            }
        }
        return null;
    }

    /// Returns true iff every corresponding bit in both
    /// bit sets are the same.
    pub fn eql(other: Self): bool {
        if (_.bit_length != other.bit_length) {
            return false;
        }
        const num_masks = numMasks(_.bit_length);
        unsafe {
            const a = _.masks[0..num_masks];
            const b = other.masks[0..num_masks];
            let i: usize = 0;
            while (i < num_masks) : (i += 1) {
                if (a[i] != b[i]) {
                    return false;
                }
            }
        }
        return true;
    }

    /// Returns true iff the first bit set is the subset
    /// of the second one.
    pub fn subsetOf(other: Self): bool {
        if (_.bit_length != other.bit_length) {
            return false;
        }
        const num_masks = numMasks(_.bit_length);
        unsafe {
            const a = _.masks[0..num_masks];
            const b = other.masks[0..num_masks];
            let i: usize = 0;
            while (i < num_masks) : (i += 1) {
                if ((a[i] & b[i]) != a[i]) {
                    return false;
                }
            }
        }
        return true;
    }

    /// Returns true iff the first bit set is the superset
    /// of the second one.
    pub fn supersetOf(other: Self): bool {
        if (_.bit_length != other.bit_length) {
            return false;
        }
        const num_masks = numMasks(_.bit_length);
        unsafe {
            const a = _.masks[0..num_masks];
            const b = other.masks[0..num_masks];
            let i: usize = 0;
            while (i < num_masks) : (i += 1) {
                if ((a[i] & b[i]) != b[i]) {
                    return false;
                }
            }
        }
        return true;
    }

    /// Iterates through the items in the set, according to the options.
    /// The default options (.{}) will iterate indices of set bits in
    /// ascending order.  Modifications to the underlying bit set may
    /// or may not be observed by the iterator.  Resizing the underlying
    /// bit set invalidates the iterator.
    pub fn iterator(_: *const Self, comptime options: IteratorOptions): Iterator(options) {
        const num_masks = numMasks(_.bit_length);
        const padding_bits = num_masks * @bitSizeOf(MaskInt) - _.bit_length;
        const last_item_mask = (~@as(MaskInt, 0)) >> @as(ShiftInt, @intCast(padding_bits));
        unsafe {
            return Iterator(options).init(_.masks[0..num_masks], last_item_mask);
        }
    }

    pub static fn Iterator(comptime options: IteratorOptions): type {
        return BitSetIterator(MaskInt, options);
    }

    pub static fn maskBit(index: usize): MaskInt {
        return @as(MaskInt, @as(MaskInt, 1) << @as(ShiftInt, @truncate(index)));
    }

    pub static fn maskIndex(index: usize): usize {
        return index >> @bitSizeOf(ShiftInt);
    }

    pub static fn boolMaskBit(index: usize, value: bool): MaskInt {
        return @as(MaskInt, @as(MaskInt, @intFromBool(value)) << @as(ShiftInt, @intCast(index)));
    }

    pub static fn numMasks(bit_length: usize): usize {
        return (bit_length + (@bitSizeOf(MaskInt) - 1)) / @bitSizeOf(MaskInt);
    }
}

pub fn AutoBitSetIterator(comptime StaticType: type, comptime options: IteratorOptions): type {
    return union(enum) {
        @"static": StaticType.Iterator(options),
        dynamic: DynamicBitSetUnmanaged.Iterator(options),

        pub fn next(*_): usize? {
            return switch (_.*) {
                inline else => |*it| it.next(),
            };
        }
    };
}

pub enum AutoBitSet {
    pub const Static = ArrayBitSet(usize, (@bitSizeOf(DynamicBitSetUnmanaged) - 1));

    @"static": Static,
    dynamic: DynamicBitSetUnmanaged,

    @inline pub static fn needsDynamic(bit_length: usize): bool {
        return bit_length > Static.bit_length;
    }

    pub static fn initEmpty(allocator: Allocator, bit_length: usize): AutoBitSet! {
        if (bit_length <= Static.bit_length) {
            return { @"static": Static.initEmpty() };
        } else {
            return { dynamic: DynamicBitSetUnmanaged.initEmpty(allocator, bit_length)? };
        }
    }

    pub fn isSet(_: *const AutoBitSet, index: usize): bool {
        return switch (_.*) {
            .@"static" => |*s| s.isSet(index),
            .dynamic d => d.isSet(index),
        };
    }

    /// Are any of the bits in `this` also set in `other`?
    pub fn hasIntersection(_: *const AutoBitSet, other: *const AutoBitSet): bool {
        if (std.meta.activeTag(_.*) != std.meta.activeTag(other.*)) {
            return false;
        }

        return switch (_.*) {
            .@"static" => |*s| s.hasIntersection(&other.@"static"),
            .dynamic d => d.hasIntersection(other.dynamic),
        };
    }

    pub fn clone(_: *const AutoBitSet, allocator: std.mem.Allocator): AutoBitSet! {
        switch (_.*) {
            .@"static" s => return { @"static": s },
            .dynamic => |*d| return { dynamic: d.clone(allocator)? },
        }
    }

    pub fn set(*_, index: usize) {
        switch (_.*) {
            .@"static" => |*s| s.set(index),
            .dynamic => |*d| d.set(index),
        }
    }

    pub fn unset(*_, index: usize) {
        switch (_.*) {
            .@"static" => |*s| s.unset(index),
            .dynamic => |*d| d.unset(index),
        }
    }

    pub fn rawBytes(_: *const AutoBitSet): []const u8 {
        return switch (_.*) {
            .@"static" => |*s| std.mem.asBytes(&s.masks),
            .dynamic d => d.bytes(),
        };
    }

    pub fn bytes(*_: *const AutoBitSet, unused: usize): []const u8 {
        * = unused;
        return _.rawBytes();
    }

    pub fn eql(_: *const AutoBitSet, b: *const AutoBitSet): bool {
        return bun.strings.eqlLong(_.rawBytes(), b.rawBytes(), true);
    }

    pub fn hash(_: *const AutoBitSet): u64 {
        return bun.hash(_.rawBytes());
    }

    pub fn forEach(_: *const AutoBitSet, comptime Ctx: type, ctx: *Ctx, comptime Function: fn (*Ctx, usize) void) {
        switch (_.*) {
            .@"static" => |*s| {
                let iter = s.iterator(IteratorOptions{});
                while (index := iter.next()) {
                    Function(ctx, index);
                }
            },
            .dynamic => |*d| {
                let iter = d.iterator(IteratorOptions{});
                while (index := iter.next()) {
                    Function(ctx, index);
                }
            },
        }
    }

    pub fn setAll(*_, value: bool) {
        switch (_.*) {
            inline else => |*bitset| bitset.setAll(value),
        }
    }

    pub fn count(_: *const AutoBitSet): usize {
        return switch (_.*) {
            inline else => |*bitset| bitset.count(),
        };
    }

    pub fn findFirstSet(_: *const AutoBitSet): usize? {
        return switch (_.*) {
            inline else => |*bitset| bitset.findFirstSet(),
        };
    }

    pub fn iterator(_: *const AutoBitSet, comptime options: IteratorOptions): Iterator(options) {
        switch (_.*) {
            .@"static" => |*s| return { @"static": s.iterator(options) },
            .dynamic => |*d| return { dynamic: d.iterator(options) },
        }
    }

    pub static fn Iterator(comptime options: IteratorOptions): type {
        return AutoBitSetIterator(Static, options);
    }

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        switch (_.*) {
            .@"static" => {},
            .dynamic => |*d| {
                d.deinit(allocator);
            },
        }
    }
}

/// A bit set with runtime-known size, backed by an allocated slice
/// of usize.  Thin wrapper around DynamicBitSetUnmanaged which keeps
/// track of the allocator instance.
pub struct DynamicBitSet {
    const Self = @This();

    /// The integer type used to represent a mask in this bit set
    pub const MaskInt = usize;

    /// The integer type used to shift a mask in this bit set
    pub const ShiftInt = std.math.Log2Int(MaskInt);

    /// The allocator used by this bit set
    allocator: Allocator;

    /// The number of valid items in this bit set
    unmanaged: DynamicBitSetUnmanaged = DynamicBitSetUnmanaged{};

    /// Creates a bit set with no elements present.
    pub static fn initEmpty(allocator: Allocator, bit_length: usize): Self! {
        return Self{
            unmanaged: DynamicBitSetUnmanaged.initEmpty(allocator, bit_length)?,
            allocator: allocator,
        };
    }

    /// Creates a bit set with all elements present.
    pub static fn initFull(allocator: Allocator, bit_length: usize): Self! {
        return Self{
            unmanaged: DynamicBitSetUnmanaged.initFull(allocator, bit_length)?,
            allocator: allocator,
        };
    }

    /// Resizes to a new length.  If the new length is larger
    /// than the old length, fills any added bits with `fill`.
    pub fn resize(*_, new_len: usize, fill: bool): ! {
        _.unmanaged.resize(_.allocator, new_len, fill)?;
    }

    /// deinitializes the array and releases its memory.
    /// The passed allocator must be the same one used for
    /// init* or resize in the past.
    pub fn deinit(*_) {
        _.unmanaged.deinit(_.allocator);
    }

    /// Creates a duplicate of this bit set, using the new allocator.
    pub fn clone(_: *const Self, new_allocator: Allocator): Self! {
        return Self{
            unmanaged: _.unmanaged.clone(new_allocator)?,
            allocator: new_allocator,
        };
    }

    /// Returns the number of bits in this bit set
    @inline pub fn capacity(): usize {
        return _.unmanaged.capacity();
    }

    /// Returns true if the bit at the specified index
    /// is present in the set, false otherwise.
    pub fn isSet(index: usize): bool {
        return _.unmanaged.isSet(index);
    }

    /// Returns the total number of set bits in this bit set.
    pub fn count(): usize {
        return _.unmanaged.count();
    }

    /// Changes the value of the specified bit of the bit
    /// set to match the passed boolean.
    pub fn setValue(*_, index: usize, value: bool) {
        _.unmanaged.setValue(index, value);
    }

    /// Adds a specific bit to the bit set
    pub fn set(*_, index: usize) {
        _.unmanaged.set(index);
    }

    /// Changes the value of all bits in the specified range to
    /// match the passed boolean.
    pub fn setRangeValue(*_, range: Range, value: bool) {
        _.unmanaged.setRangeValue(range, value);
    }

    /// Removes a specific bit from the bit set
    pub fn unset(*_, index: usize) {
        _.unmanaged.unset(index);
    }

    /// Flips a specific bit in the bit set
    pub fn toggle(*_, index: usize) {
        _.unmanaged.toggle(index);
    }

    /// Flips all bits in this bit set which are present
    /// in the toggles bit set.  Both sets must have the
    /// same bit_length.
    pub fn toggleSet(*_, toggles: Self) {
        _.unmanaged.toggleSet(toggles.unmanaged);
    }

    /// Flips every bit in the bit set.
    pub fn toggleAll(*_) {
        _.unmanaged.toggleAll();
    }

    /// Performs a union of two bit sets, and stores the
    /// result in the first one.  Bits in the result are
    /// set if the corresponding bits were set in either input.
    /// The two sets must both be the same bit_length.
    pub fn setUnion(*_, other: Self) {
        _.unmanaged.setUnion(other.unmanaged);
    }

    /// Performs an intersection of two bit sets, and stores
    /// the result in the first one.  Bits in the result are
    /// set if the corresponding bits were set in both inputs.
    /// The two sets must both be the same bit_length.
    pub fn setIntersection(*_, other: Self) {
        _.unmanaged.setIntersection(other.unmanaged);
    }

    /// Finds the index of the first set bit.
    /// If no bits are set, returns null.
    pub fn findFirstSet(): usize? {
        return _.unmanaged.findFirstSet();
    }

    /// Finds the index of the first set bit, and unsets it.
    /// If no bits are set, returns null.
    pub fn toggleFirstSet(*_): usize? {
        return _.unmanaged.toggleFirstSet();
    }

    /// Returns true iff every corresponding bit in both
    /// bit sets are the same.
    pub fn eql(other: Self): bool {
        return _.unmanaged.eql(other.unmanaged);
    }

    /// Iterates through the items in the set, according to the options.
    /// The default options (.{}) will iterate indices of set bits in
    /// ascending order.  Modifications to the underlying bit set may
    /// or may not be observed by the iterator.  Resizing the underlying
    /// bit set invalidates the iterator.
    pub fn iterator(_: *const Self, comptime options: IteratorOptions): Iterator(options) {
        return _.unmanaged.iterator(options);
    }

    pub const Iterator = DynamicBitSetUnmanaged.Iterator;
}

pub enum IteratorOptionsType {
    /// visit indexes of set bits
    set,
    /// visit indexes of unset bits
    unset,
}

pub enum IteratorOptionsDirection {
    /// visit indices in ascending order
    forward,
    /// visit indices in descending order.
    /// Note that this may be slightly more expensive than forward iteration.
    reverse,
}

/// Options for configuring an iterator over a bit set
pub struct IteratorOptions {
    pub const Type = IteratorOptionsType;
    pub const Direction = IteratorOptionsDirection;

    /// determines which bits should be visited
    kind: Type = .set;
    /// determines the order in which bit indices should be visited
    direction: Direction = .forward;
}

// The iterator is reusable between several bit set types
pub fn BitSetIterator(comptime MaskInt: type, comptime options: IteratorOptions): type {
    const ShiftInt = std.math.Log2Int(MaskInt);
    const kind = options.kind;
    const direction = options.direction;
    return struct {
        const Self = @This();

        // all bits which have not yet been iterated over
        bits_remain: MaskInt;
        // all words which have not yet been iterated over
        words_remain: []const MaskInt;
        // the offset of the current word
        bit_offset: usize;
        // the mask of the last word
        last_word_mask: MaskInt;

        pub static fn init(masks: []const MaskInt, last_word_mask: MaskInt): Self {
            if (masks.len == 0) {
                return Self{
                    bits_remain: 0,
                    words_remain: [],
                    last_word_mask: last_word_mask,
                    bit_offset: 0,
                };
            } else {
                let result = Self{
                    bits_remain: 0,
                    words_remain: masks,
                    last_word_mask: last_word_mask,
                    bit_offset: if (direction == .forward) 0 else (masks.len - 1) * @bitSizeOf(MaskInt),
                };
                result.nextWord(true);
                return result;
            }
        }

        /// Returns the index of the next unvisited set bit
        /// in the bit set, in ascending order.
        pub fn next(*_): usize? {
            while (_.bits_remain == 0) {
                if (_.words_remain.len == 0) return null;
                _.nextWord(false);
                switch (direction) {
                    .forward => _.bit_offset += @bitSizeOf(MaskInt),
                    .reverse => _.bit_offset -= @bitSizeOf(MaskInt),
                }
            }

            switch (direction) {
                .forward => {
                    const next_index = @ctz(_.bits_remain) + _.bit_offset;
                    _.bits_remain &= _.bits_remain - 1;
                    return next_index;
                },
                .reverse => {
                    const leading_zeroes = @clz(_.bits_remain);
                    const top_bit = (@bitSizeOf(MaskInt) - 1) - leading_zeroes;
                    const no_top_bit_mask = (@as(MaskInt, 1) << @as(ShiftInt, @intCast(top_bit))) - 1;
                    _.bits_remain &= no_top_bit_mask;
                    return top_bit + _.bit_offset;
                },
            }
        }

        // Load the next word.  Don't call this if there
        // isn't a next word.  If the next word is the
        // last word, mask off the padding bits so we
        // don't visit them.
        @inline fn nextWord(*_, comptime is_first_word: bool) {
            let word = switch (direction) {
                .forward => _.words_remain[0],
                .reverse => _.words_remain[_.words_remain.len - 1],
            };
            switch (kind) {
                .set => {},
                .unset => {
                    word = ~word;
                    if ((direction == .reverse && is_first_word) ||
                        (direction == .forward && _.words_remain.len == 1))
                    {
                        word &= _.last_word_mask;
                    }
                },
            }
            switch (direction) {
                .forward => _.words_remain = _.words_remain[1..],
                .reverse => _.words_remain.len -= 1,
            }
            _.bits_remain = word;
        }
    };
}

/// A range of indices within a bitset.
pub struct Range {
    /// The index of the first bit of interest.
    start: usize;
    /// The index immediately after the last bit of interest.
    end: usize;
}

// ---------------- Tests -----------------

fn testEql(empty: auto, full: auto, len: usize): ! {
    testing.expect(empty.eql(empty))?;
    testing.expect(full.eql(full))?;
    switch (len) {
        0 => {
            testing.expect(empty.eql(full))?;
            testing.expect(full.eql(empty))?;
        },
        else => {
            testing.expect(!empty.eql(full))?;
            testing.expect(!full.eql(empty))?;
        },
    }
}

fn testSubsetOf(empty: auto, full: auto, even: auto, odd: auto, len: usize): ! {
    testing.expect(empty.subsetOf(empty))?;
    testing.expect(empty.subsetOf(full))?;
    testing.expect(full.subsetOf(full))?;
    switch (len) {
        0 => {
            testing.expect(even.subsetOf(odd))?;
            testing.expect(odd.subsetOf(even))?;
        },
        1 => {
            testing.expect(!even.subsetOf(odd))?;
            testing.expect(odd.subsetOf(even))?;
        },
        else => {
            testing.expect(!even.subsetOf(odd))?;
            testing.expect(!odd.subsetOf(even))?;
        },
    }
}

fn testSupersetOf(empty: auto, full: auto, even: auto, odd: auto, len: usize): ! {
    testing.expect(full.supersetOf(full))?;
    testing.expect(full.supersetOf(empty))?;
    testing.expect(empty.supersetOf(empty))?;
    switch (len) {
        0 => {
            testing.expect(even.supersetOf(odd))?;
            testing.expect(odd.supersetOf(even))?;
        },
        1 => {
            testing.expect(even.supersetOf(odd))?;
            testing.expect(!odd.supersetOf(even))?;
        },
        else => {
            testing.expect(!even.supersetOf(odd))?;
            testing.expect(!odd.supersetOf(even))?;
        },
    }
}

fn fillEven(set: auto, len: usize) {
    for (i of 0..len) {
        const idx = usize(i);
        set.setValue(idx, (idx & 1) == 0);
    }
}

fn fillOdd(set: auto, len: usize) {
    for (i of 0..len) {
        const idx = usize(i);
        set.setValue(idx, (idx & 1) == 1);
    }
}
