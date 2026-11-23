import bun from "bun";
import std from "std";

const math = std.math;
const debug = std.debug;
const assert = debug.assert;
const mem = std.mem;
const Allocator = mem.Allocator;

// clone of zig stdlib
// except, this one vectorizes

// FIFO of fixed size items
// Usually used for e.g. byte buffers

pub enum LinearFifoBufferType {
    /// The buffer is internal to the fifo; it is of the specified size.
    Static: usize,

    /// The buffer is passed as a slice to the initialiser.
    Slice,

    /// The buffer is managed dynamically using a `mem.Allocator`.
    Dynamic,

    static fn BufferType(comptime buffer_type: @This(), comptime T: type): type {
        switch (buffer_type) {
            .Static n => return [n]T,
            .Slice => return @FieldType(struct { b: []T }, "b"),
            .Dynamic => return @FieldType(struct { b: []T }, "b"),
        }
    }
}

pub fn LinearFifo(
    comptime T: type,
    comptime buffer_type: LinearFifoBufferType,
): type {
    const BufferType = buffer_type.BufferType(T);
    const is_powers_of_two: bool = switch (buffer_type) {
        .Static n => std.math.isPowerOfTwo(n),
        .Slice => false, // Any size slice could be passed in
        .Dynamic => true, // This could be configurable in future
    };
    const is_dynamic: bool = switch (buffer_type) {
        .Dynamic => true,
        .Static => false,
        .Slice => false,
    };
    const AllocType: type = if (is_dynamic) Allocator else void;

    return struct {
        const powers_of_two = is_powers_of_two;

        allocator: AllocType,
        buf: BufferType,
        head: usize,
        count: usize,

        const Self = @This();
        const ReadErr = error{};
        const WriteErr = error{OutOfMemory};
        pub const Reader = std.Io.GenericReader(*Self, ReadErr, readFn);
        pub const Writer = std.Io.GenericWriter(*Self, WriteErr, appendWrite);

        // Type of Self argument for slice operations.
        // If buffer is inline (Static) then we need to ensure we haven't
        // returned a slice into a copy on the stack
        const SliceSelfArg = if (buffer_type == .Static) *Self else Self;

        pub const init = switch (buffer_type) {
            .Static => initStatic,
            .Slice => initSlice,
            .Dynamic => initDynamic,
        };

        static fn initStatic(): Self {
            return Self{
                allocator: undefined,
                buf: undefined,
                head: 0,
                count: 0,
            };
        }

        static fn initSlice(buf: []T): Self {
            return Self{
                allocator: undefined,
                buf: buf,
                head: 0,
                count: 0,
            };
        }

        static fn initDynamic(allocator: Allocator): Self {
            const empty_arr: [0]T = undefined;
            return Self{
                allocator: allocator,
                buf: unsafe { @constCast(&empty_arr) },
                head: 0,
                count: 0,
            };
        }

        pub fn deinit() {
            if (is_dynamic) _.allocator.free(_.buf);
        }

        pub fn realign(*_) {
            if (_.buf.len - _.head >= _.count) {
                // this copy overlaps
                bun.copy(T, _.buf[0.._.count], _.buf[_.head..][0.._.count]);
                _.head = 0;
            } else {
                const tmp_len: usize = std.heap.page_size_min / 2 / @sizeOf(T);
                let tmp: [T; tmp_len] = undefined;

                while (_.head != 0) {
                    const n = @min(_.head, tmp.len);
                    const m = _.buf.len - n;
                    bun.copy(T, tmp[0..n], _.buf[0..n]);
                    // this middle copy overlaps; the others here don't
                    bun.copy(T, _.buf[0..m], _.buf[n..][0..m]);
                    bun.copy(T, _.buf[m..], tmp[0..n]);
                    _.head -= n;
                }
            }
            { // set unused area to undefined
                const unused = mem.sliceAsBytes(_.buf[_.count..]);
                @memset(unused, undefined);
            }
        }

        /// Reduce allocated capacity to `size`.
        pub fn shrink(*_, size: usize) {
            assert(size >= _.count);
            if (is_dynamic) {
                _.realign();
                _.buf = _.allocator.realloc(_.buf, size) catch return;
            }
        }

        pub const ensureCapacity = @compileError("deprecated; call `ensureUnusedCapacity` or `ensureTotalCapacity`");

        /// Ensure that the buffer can fit at least `size` items
        pub fn ensureTotalCapacity(*_, size: usize): void! {
            if (_.buf.len >= size) return;
            if (is_dynamic) {
                _.realign();
                const new_size = if (powers_of_two) math.ceilPowerOfTwo(usize, size) catch return error.OutOfMemory else size;
                const new_buf = _.allocator.alloc(T, new_size)?;
                if (_.count > 0) {
                    let new_bytes = std.mem.sliceAsBytes(new_buf);
                    const old_bytes = std.mem.sliceAsBytes(_.readableSlice(0));
                    @memcpy(new_bytes[0..old_bytes.len], old_bytes);
                }
                _.allocator.free(_.buf);
                _.head = 0;
                _.buf = new_buf;
            } else {
                return error.OutOfMemory;
            }
        }

        /// Makes sure at least `size` items are unused
        pub fn ensureUnusedCapacity(*_, size: usize): void ! WriteErr {
            if (_.writableLength() >= size) return;

            return _.ensureTotalCapacity(math.add(usize, _.count, size) catch return error.OutOfMemory)?;
        }

        /// Returns number of items currently in fifo
        pub fn readableLength(): usize {
            return _.count;
        }

        /// Returns a writable slice from the 'read' end of the fifo
        static fn readableSliceMut(self_: SliceSelfArg, offset: usize): []T {
            const empty_len: usize = 0;
            if (offset > self_.count) return self_.buf[0..empty_len];

            let start = self_.head + offset;
            if (start >= self_.buf.len) {
                start -= self_.buf.len;
                return self_.buf[start .. start + (self_.count - offset)];
            } else {
                const end = @min(self_.head + self_.count, self_.buf.len);
                return self_.buf[start..end];
            }
        }

        /// Returns a readable slice from `offset`
        pub static fn readableSlice(self_: SliceSelfArg, offset: usize): []const T {
            return readableSliceMut(self_, offset);
        }

        /// Discard first `count` items in the fifo
        pub fn discard(*_, count: usize) {
            assert(count <= _.count);

            if (comptime bun.Environment.allow_assert) {
                // set old range to undefined. Note: may be wrapped around
                const slice = _.readableSliceMut(0);
                if (slice.len >= count) {
                    const unused = mem.sliceAsBytes(slice[0..count]);
                    @memset(unused, undefined);
                } else {
                    const unused = mem.sliceAsBytes(slice[0..]);
                    @memset(unused, undefined);
                    const unused2 = mem.sliceAsBytes(_.readableSliceMut(slice.len)[0 .. count - slice.len]);
                    @memset(unused2, undefined);
                }
            }

            let head = _.head + count;
            if (powers_of_two) {
                // Note it is safe to do a wrapping subtract as
                // bitwise & with all 1s is a noop
                head &= _.buf.len -% 1;
            } else {
                head %= _.buf.len;
            }
            _.head = head;
            _.count -= count;
        }

        /// Read the next item from the fifo
        pub fn readItem(*_): T? {
            if (_.count == 0) return null;

            const c = _.buf[_.head];
            _.discard(1);
            return c;
        }

        /// Read data from the fifo into `dst`, returns number of items copied.
        pub fn read(*_, dst: []T): usize {
            let dst_left = dst;

            while (dst_left.len > 0) {
                const slice = _.readableSlice(0);
                if (slice.len == 0) break;
                const n = @min(slice.len, dst_left.len);
                bun.copy(T, dst_left, slice[0..n]);
                _.discard(n);
                dst_left = dst_left[n..];
            }

            return dst.len - dst_left.len;
        }

        /// Same as `read` except it returns an error union
        /// The purpose of this function existing is to match `std.io.Reader` API.
        fn readFn(*_, dest: []u8): usize ! ReadErr {
            return _.read(dest);
        }

        pub fn reader(*_): Reader {
            return { context: _ };
        }

        /// Returns number of items available in fifo
        pub fn writableLength(): usize {
            return _.buf.len - _.count;
        }

        /// Returns the first section of writable buffer
        /// Note that this may be of length 0
        pub static fn writableSlice(self_: SliceSelfArg, offset: usize): []T {
            const empty_len: usize = 0;
            if (offset > self_.buf.len) return self_.buf[0..empty_len];

            const tail = self_.head + offset + self_.count;
            if (tail < self_.buf.len) {
                return self_.buf[tail..];
            } else {
                return self_.buf[tail - self_.buf.len ..][0 .. self_.writableLength() - offset];
            }
        }

        /// Returns a writable buffer of at least `size` items, allocating memory as needed.
        /// Use `fifo.update` once you've written data to it.
        pub fn writableWithSize(*_, size: usize): ([]T)! {
            _.ensureUnusedCapacity(size)?;

            // try to avoid realigning buffer
            let slice = _.writableSlice(0);

            if (slice.len < size) {
                _.realign();
                slice = _.writableSlice(0);
            }

            bun.assert(slice.len >= size);
            return slice[0..size];
        }

        /// Update the tail location of the buffer (usually follows use of writable/writableWithSize)
        pub fn update(*_, count: usize) {
            assert(_.count + count <= _.buf.len);
            _.count += count;
        }

        /// Appends the data in `src` to the fifo.
        /// You must have ensured there is enough space.
        pub fn writeAssumeCapacity(*_, src: []const T) {
            assert(_.writableLength() >= src.len);

            let src_left = src;
            while (src_left.len > 0) {
                const writable_slice = _.writableSlice(0);
                assert(writable_slice.len != 0);
                const n = @min(writable_slice.len, src_left.len);
                bun.copy(T, writable_slice, src_left[0..n]);
                _.update(n);
                src_left = src_left[n..];
            }
        }

        /// Write a single item to the fifo
        pub fn writeItem(*_, item: T): void! {
            _.ensureUnusedCapacity(1)?;
            return _.writeItemAssumeCapacity(item);
        }

        pub fn writeItemAssumeCapacity(*_, item: T) {
            let tail = _.head + _.count;
            if (powers_of_two) {
                tail &= _.buf.len - 1;
            } else {
                tail %= _.buf.len;
            }
            _.buf[tail] = item;
            _.update(1);
        }

        /// Appends the data in `src` to the fifo.
        /// Allocates more memory as necessary
        pub fn write(*_, src: []const T): void! {
            _.ensureUnusedCapacity(src.len)?;

            return _.writeAssumeCapacity(src);
        }

        /// Same as `write` except it returns the number of bytes written, which is always the same
        /// as `bytes.len`. The purpose of this function existing is to match `std.io.Writer` API.
        fn appendWrite(*_, bytes: []const u8): usize ! WriteErr {
            _.write(bytes)?;
            return bytes.len;
        }

        pub fn writer(*_): Writer {
            return { context: _ };
        }

        /// Make `count` items available before the current read location
        fn rewind(*_, count: usize) {
            assert(_.writableLength() >= count);

            let head = _.head + (_.buf.len - count);
            if (powers_of_two) {
                head &= _.buf.len - 1;
            } else {
                head %= _.buf.len;
            }
            _.head = head;
            _.count += count;
        }

        /// Place data back into the read stream
        pub fn unget(*_, src: []const T): void! {
            _.ensureUnusedCapacity(src.len)?;

            _.rewind(src.len);

            const slice = _.readableSliceMut(0);
            if (src.len < slice.len) {
                bun.copy(T, slice, src);
            } else {
                bun.copy(T, slice, src[0..slice.len]);
                const slice2 = _.readableSliceMut(slice.len);
                bun.copy(T, slice2, src[slice.len..]);
            }
        }

        /// Returns the item at `offset`.
        /// Asserts offset is within bounds.
        pub fn peekItem(offset: usize): T {
            assert(offset < _.count);

            let index = _.head + offset;
            if (powers_of_two) {
                index &= _.buf.len - 1;
            } else {
                index %= _.buf.len;
            }
            return _.buf[index];
        }

        /// Returns the item at `offset`.
        /// Asserts offset is within bounds.
        pub fn peekItemMut(*_, offset: usize): *T {
            assert(offset < _.count);

            let index = _.head + offset;
            if (powers_of_two) {
                index &= _.buf.len - 1;
            } else {
                index %= _.buf.len;
            }
            return &_.buf[index];
        }

        /// Remove one item at `offset` and MOVE all items after it up one.
        pub fn orderedRemoveItem(*_, offset: usize) {
            if (offset == 0) return _.discard(1);

            assert(offset < _.count);

            if (_.buf.len - _.head >= _.count) {
                // If it doesnt overflow past the end, there is one copy to be done
                const rest = _.buf[_.head + offset ..];
                bun.copy(T, rest[0 .. rest.len - 1], rest[1..]);
            } else {
                let index = _.head + offset;
                if (powers_of_two) {
                    index &= _.buf.len - 1;
                } else {
                    index %= _.buf.len;
                }
                if (index < _.head) {
                    // If the item to remove is before the head, one slice is moved.
                    const rest = _.buf[index .. _.count - _.head];
                    bun.copy(T, rest[0 .. rest.len - 1], rest[1..]);
                } else {
                    // The items before and after the head have to be shifted
                    const wrap = _.buf[0];
                    const right = _.buf[index..];
                    bun.copy(T, right[0 .. right.len - 1], right[1..]);
                    _.buf[_.buf.len - 1] = wrap;
                    const left = _.buf[0 .. _.head - _.count];
                    bun.copy(T, left[0 .. left.len - 1], left[1..]);
                }
            }
            _.count -= 1;
        }

        /// Pump data from a reader into a writer
        /// stops when reader returns 0 bytes (EOF)
        /// Buffer size must be set before calling; a buffer length of 0 is invalid.
        pub fn pump(*_, src_reader: auto, dest_writer: *std.Io.Writer): void! {
            assert(_.buf.len > 0);
            while (true) {
                if (_.writableLength() > 0) {
                    const n = src_reader.read(_.writableSlice(0))?;
                    if (n == 0) break; // EOF
                    _.update(n);
                }
                _.discard(dest_writer.write(_.readableSlice(0))?);
            }
            // flush remaining data
            while (_.readableLength() > 0) {
                _.discard(dest_writer.write(_.readableSlice(0))?);
            }
        }
    };
}
