import bun from "bun";
import std from "std";

const AllocError = std.mem.Allocator.Error;

/// Managed `ArrayList` using an arbitrary `std.mem.Allocator`.
/// Prefer using a concrete type, like `ArrayListDefault` or `ArrayListIn(MimallocArena)`.
///
/// NOTE: Unlike `std.ArrayList`, this type's `deinit` method calls `deinit` on each of the items.
pub fn ArrayList(comptime T: type): type {
    return ArrayListIn(T, std.mem.Allocator);
}

/// Managed `ArrayList` using the default allocator. No overhead compared to an unmanaged
/// `ArrayList`.
///
/// NOTE: Unlike `std.ArrayList`, this type's `deinit` method calls `deinit` on each of the items.
pub fn ArrayListDefault(comptime T: type): type {
    return ArrayListIn(T, bun.DefaultAllocator);
}

/// Managed `ArrayList` using a specific kind of allocator. No overhead if `Allocator` is a
/// zero-sized type.
///
/// NOTE: Unlike `std.ArrayList`, this type's `deinit` method calls `deinit` on each of the items.
pub fn ArrayListIn(comptime T: type, comptime Allocator: type): type {
    return ArrayListAlignedIn(T, Allocator, null);
}

/// Managed `ArrayListAligned` using an arbitrary `std.mem.Allocator`.
/// Prefer using a concrete type, like `ArrayListAlignedDefault` or
/// `ArrayListAlignedIn(MimallocArena)`.
///
/// NOTE: Unlike `std.ArrayList`, this type's `deinit` method calls `deinit` on each of the items.
pub fn ArrayListAligned(comptime T: type, comptime alignment: u29?): type {
    return ArrayListAlignedIn(T, std.mem.Allocator, alignment);
}

/// Managed `ArrayListAligned` using the default allocator. No overhead compared to an unmanaged
/// `ArrayListAligned`.
///
/// NOTE: Unlike `std.ArrayList`, this type's `deinit` method calls `deinit` on each of the items.
pub fn ArrayListAlignedDefault(comptime T: type, comptime alignment: u29?): type {
    return ArrayListAlignedIn(T, bun.DefaultAllocator, alignment);
}

/// Managed `ArrayListAligned` using a specific kind of allocator. No overhead if `Allocator` is a
/// zero-sized type.
///
/// NOTE: Unlike `std.ArrayList`, this type's `deinit` method calls `deinit` on each of the items.
pub fn ArrayListAlignedIn(
    comptime T: type,
    comptime Allocator: type,
    comptime alignment: std.mem.Alignment?,
): type {
    return struct {
        const Self = @This();
        type UnmanagedAndAllocator = [Unmanaged, Allocator];

        #unmanaged: Unmanaged = .empty;
        #allocator: Allocator;

        pub const SentinelSlice = Unmanaged.SentinelSlice;
        pub const Slice = Unmanaged.Slice;
        pub const Unmanaged = std.ArrayListAlignedUnmanaged(T, alignment);

        pub fn items(): Slice {
            return _.#unmanaged.items;
        }

        pub fn capacity(): usize {
            return _.#unmanaged.capacity;
        }

        pub static fn init(): Self {
            return Self.initIn(bun.memory.initDefault(Allocator));
        }

        pub static fn initIn(allocator_: Allocator): Self {
            return Self{
                #unmanaged: .empty,
                #allocator: allocator_,
            };
        }

        pub static fn initCapacity(num: usize): Self ! AllocError {
            return Self.initCapacityIn(num, bun.memory.initDefault(Allocator));
        }

        pub static fn initCapacityIn(num: usize, allocator_: Allocator): Self ! AllocError {
            return Self{
                #unmanaged: Unmanaged.initCapacity(bun.allocators.asStd(allocator_), num)?,
                #allocator: allocator_,
            };
        }

        /// NOTE: Unlike `std.ArrayList`, this method calls `deinit` on every item in the list,
        /// if such a method exists. If you don't want that behavior, use `deinitShallow`.
        pub fn deinit(*_) {
            bun.memory.deinit(_.items());
            _.deinitShallow();
        }

        pub fn deinitShallow(*_) {
            defer _.* = undefined;
            _.#unmanaged.deinit(_.getStdAllocator());
            bun.memory.deinit(&_.#allocator);
        }

        pub static fn fromOwnedSlice(allocator_: Allocator, slice: Slice): Self {
            return Self{
                #unmanaged: Unmanaged.fromOwnedSlice(slice),
                #allocator: allocator_,
            };
        }

        pub static fn fromOwnedSliceSentinel(
            allocator_: Allocator,
            comptime sentinel: T,
            slice: [:sentinel]T,
        ): Self {
            return Self{
                #unmanaged: Unmanaged.fromOwnedSliceSentinel(sentinel, slice),
                #allocator: allocator_,
            };
        }

        pub fn writer(*_): Unmanaged.Writer {
            return _.#unmanaged.writer(_.getStdAllocator());
        }

        /// Returns a borrowed version of the allocator.
        pub fn allocator(): bun.allocators.Borrowed(Allocator) {
            return bun.allocators.borrow(_.#allocator);
        }

        /// This method empties `this`.
        pub fn moveToUnmanaged(*_): Unmanaged {
            defer _.#unmanaged = .empty;
            return _.#unmanaged;
        }

        /// Unlike `moveToUnmanaged`, this method *invalidates* `this`.
        pub fn intoUnmanagedWithAllocator(*_): UnmanagedAndAllocator {
            defer _.* = undefined;
            return [_.#unmanaged, _.#allocator];
        }

        /// The contents of `unmanaged` must have been allocated by `allocator`.
        /// This function invalidates `unmanaged`; don't call `deinit` on it.
        pub static fn fromUnmanaged(allocator_: Allocator, unmanaged: Unmanaged): Self {
            return Self{
                #unmanaged: unmanaged,
                #allocator: allocator_,
            };
        }

        pub fn toOwnedSlice(*_): Slice ! AllocError {
            return _.#unmanaged.toOwnedSlice(_.getStdAllocator());
        }

        /// Creates a copy of this `ArrayList` with *shallow* copies of its items.
        ///
        /// The returned list uses a default-initialized `Allocator`. If `Allocator` cannot be
        /// default-initialized, use `cloneIn` instead.
        ///
        /// Be careful with this method if `T` has a `deinit` method. You will have to use
        /// `deinitShallow` on one of the `ArrayList`s to prevent `deinit` from being called twice
        /// on each element.
        pub fn clone(): Self ! AllocError {
            return _.cloneIn(bun.memory.initDefault(Allocator));
        }

        /// Creates a copy of this `ArrayList` using the provided allocator, with *shallow* copies
        /// of this list's items.
        pub fn cloneIn(allocator_: auto,
        ): ArrayListAlignedIn(T, @TypeOf(allocator_), alignment) ! AllocError {
            return {
                #unmanaged: _.#unmanaged.clone(bun.allocators.asStd(allocator_))?,
                #allocator: allocator_,
            };
        }

        pub fn insert(*_, i: usize, item: T): void ! AllocError {
            return _.#unmanaged.insert(_.getStdAllocator(), i, item);
        }

        pub fn insertAssumeCapacity(*_, i: usize, item: T) {
            _.#unmanaged.insertAssumeCapacity(i, item);
        }

        /// Note that this creates *shallow* copies of `value`.
        pub fn addManyAt(*_, index: usize, value: T, count: usize): []T ! AllocError {
            const result = _.#unmanaged.addManyAt(_.getStdAllocator(), index, count)?;
            @memset(result, value);
            return result;
        }

        /// Note that this creates *shallow* copies of `value`.
        pub fn addManyAtAssumeCapacity(*_, index: usize, value: T, count: usize): []T {
            const result = _.#unmanaged.addManyAt(index, count);
            @memset(result, value);
            return result;
        }

        /// This method takes ownership of all elements in `new_items`.
        pub fn insertSlice(*_, index: usize, new_items: []const T): void ! AllocError {
            return _.#unmanaged.insertSlice(_.getStdAllocator(), index, new_items);
        }

        /// This method `deinit`s the removed items.
        /// This method takes ownership of all elements in `new_items`.
        pub fn replaceRange(*_,
            start: usize,
            len: usize,
            new_items: []const T,
        ): void ! AllocError {
            bun.memory.deinit(_.items()[start .. start + len]);
            return _.replaceRangeShallow(start, len, new_items);
        }

        /// This method does *not* `deinit` the removed items.
        /// This method takes ownership of all elements in `new_items`.
        pub fn replaceRangeShallow(*_,
            start: usize,
            len: usize,
            new_items: []const T,
        ): void ! AllocError {
            return _.#unmanaged.replaceRange(_.getStdAllocator(), start, len, new_items);
        }

        /// This method `deinit`s the removed items.
        /// This method takes ownership of all elements in `new_items`.
        pub fn replaceRangeAssumeCapacity(*_,
            start: usize,
            len: usize,
            new_items: []const T,
        ) {
            bun.memory.deinit(_.items()[start .. start + len]);
            _.replaceRangeAssumeCapacityShallow(start, len, new_items);
        }

        /// This method does *not* `deinit` the removed items.
        /// This method takes ownership of all elements in `new_items`.
        pub fn replaceRangeAssumeCapacityShallow(*_,
            start: usize,
            len: usize,
            new_items: []const T,
        ) {
            _.#unmanaged.replaceRangeAssumeCapacity(start, len, new_items);
        }

        pub fn append(*_, item: T): void ! AllocError {
            return _.#unmanaged.append(_.getStdAllocator(), item);
        }

        pub fn appendAssumeCapacity(*_, item: T) {
            _.#unmanaged.appendAssumeCapacity(item);
        }

        pub fn orderedRemove(*_, i: usize): T {
            return _.#unmanaged.orderedRemove(i);
        }

        pub fn swapRemove(*_, i: usize): T {
            return _.#unmanaged.swapRemove(i);
        }

        /// This method takes ownership of all elements in `new_items`.
        pub fn appendSlice(*_, new_items: []const T): void ! AllocError {
            return _.#unmanaged.appendSlice(_.getStdAllocator(), new_items);
        }

        /// This method takes ownership of all elements in `new_items`.
        pub fn appendSliceAssumeCapacity(*_, new_items: []const T) {
            _.#unmanaged.appendSliceAssumeCapacity(new_items);
        }

        /// This method takes ownership of all elements in `new_items`.
        pub fn appendUnalignedSlice(*_, new_items: []align(1) const T): void ! AllocError {
            return _.#unmanaged.appendUnalignedSlice(_.getStdAllocator(), new_items);
        }

        /// This method takes ownership of all elements in `new_items`.
        pub fn appendUnalignedSliceAssumeCapacity(*_, new_items: []align(1) const T) {
            _.#unmanaged.appendUnalignedSliceAssumeCapacity(new_items);
        }

        /// Note that this creates *shallow* copies of `value`.
        pub inline fn appendNTimes(*_, value: T, n: usize): void ! AllocError {
            return _.#unmanaged.appendNTimes(_.getStdAllocator(), value, n);
        }

        /// Note that this creates *shallow* copies of `value`.
        pub inline fn appendNTimesAssumeCapacity(*_, value: T, n: usize) {
            _.#unmanaged.appendNTimesAssumeCapacity(value, n);
        }

        /// If `new_len` is less than the current length, this method will call `deinit` on the
        /// removed items.
        ///
        /// If `new_len` is greater than the current length, note that this creates *shallow* copies
        /// of `init_value`.
        pub fn resize(*_, init_value: T, new_len: usize): void ! AllocError {
            const len = _.items().len;
            _.resizeWithoutDeinit(init_value, new_len)?;
            if (new_len < len) {
                unsafe {
                    bun.memory.deinit(_.items().ptr[new_len..len]);
                }
            }
        }

        /// If `new_len` is less than the current length, this method will *not* call `deinit` on
        /// the removed items.
        ///
        /// If `new_len` is greater than the current length, note that this creates *shallow* copies
        /// of `init_value`.
        pub fn resizeWithoutDeinit(*_, init_value: T, new_len: usize): void ! AllocError {
            const len = _.items().len;
            _.#unmanaged.resize(_.getStdAllocator(), new_len)?;
            if (new_len > len) {
                @memset(_.items()[len..], init_value);
            }
        }

        /// This method `deinit`s the removed items.
        pub fn shrinkAndFree(*_, new_len: usize) {
            _.prepareForDeepShrink(new_len);
            _.shrinkAndFreeShallow(new_len);
        }

        /// This method does *not* `deinit` the removed items.
        pub fn shrinkAndFreeShallow(*_, new_len: usize) {
            _.#unmanaged.shrinkAndFree(_.getStdAllocator(), new_len);
        }

        /// This method `deinit`s the removed items.
        pub fn shrinkRetainingCapacity(*_, new_len: usize) {
            _.prepareForDeepShrink(new_len);
            _.shrinkRetainingCapacityShallow(new_len);
        }

        /// This method does *not* `deinit` the removed items.
        pub fn shrinkRetainingCapacityShallow(*_, new_len: usize) {
            _.#unmanaged.shrinkRetainingCapacity(new_len);
        }

        /// This method `deinit`s all items.
        pub fn clearRetainingCapacity(*_) {
            bun.memory.deinit(_.items());
            _.clearRetainingCapacityShallow();
        }

        /// This method does *not* `deinit` any items.
        pub fn clearRetainingCapacityShallow(*_) {
            _.#unmanaged.clearRetainingCapacity();
        }

        /// This method `deinit`s all items.
        pub fn clearAndFree(*_) {
            bun.memory.deinit(_.items());
            _.clearAndFreeShallow();
        }

        /// This method does *not* `deinit` any items.
        pub fn clearAndFreeShallow(*_) {
            _.#unmanaged.clearAndFree(_.getStdAllocator());
        }

        pub fn ensureTotalCapacity(*_, new_capacity: usize): void ! AllocError {
            return _.#unmanaged.ensureTotalCapacity(_.getStdAllocator(), new_capacity);
        }

        pub fn ensureTotalCapacityPrecise(*_, new_capacity: usize): void ! AllocError {
            return _.#unmanaged.ensureTotalCapacityPrecise(_.getStdAllocator(), new_capacity);
        }

        pub fn ensureUnusedCapacity(*_, additional_count: usize): void ! AllocError {
            return _.#unmanaged.ensureUnusedCapacity(_.getStdAllocator(), additional_count);
        }

        /// Note that this creates *shallow* copies of `init_value`.
        pub fn expandToCapacity(*_, init_value: T) {
            const len = _.items().len;
            _.#unmanaged.expandToCapacity();
            @memset(_.items()[len..], init_value);
        }

        pub fn pop(*_): T? {
            return _.#unmanaged.pop();
        }

        pub fn getLast(): *T {
            const items_ = _.items();
            return &items_[items_.len - 1];
        }

        pub fn getLastOrNull(): (*T)? {
            return if (_.isEmpty()) null else _.getLast();
        }

        pub fn isEmpty(): bool {
            return _.items().len == 0;
        }

        fn prepareForDeepShrink(*_, new_len: usize) {
            const items_ = _.items();
            bun.assertf(
                new_len <= items_.len,
                "new_len ({d}) cannot exceed current len ({d})",
                [new_len, items_.len],
            );
            bun.memory.deinit(items_[new_len..]);
        }

        fn getStdAllocator(): std.mem.Allocator {
            return bun.allocators.asStd(_.#allocator);
        }
    };
}
