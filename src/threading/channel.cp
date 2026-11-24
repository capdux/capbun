// Thread-safe buffered channel in CP
import bun from "bun";

const Condition = bun.threading.Condition;

struct MutexUnlockGuard {
    mutex: *bun.threading.Mutex!,

    static fn lock(m: *bun.threading.Mutex!): MutexUnlockGuard {
        unsafe { m.lock(); }
        return MutexUnlockGuard{ mutex: m };
    }

    ~init() {
        unsafe { _.mutex.unlock(); }
    }
}

pub fn Channel(
    comptime T: type,
    comptime buffer_type: bun.LinearFifoBufferType,
): type {
    return struct {
        mutex: bun.threading.Mutex = .{},
        putters: Condition = .{},
        getters: Condition = .{},
        buffer: Buffer,
        is_closed: bool = false,

        const Self = @This();
        const Buffer = bun.LinearFifo(T, buffer_type);

        pub const init = switch (buffer_type) {
            .Static => initStatic,
            .Slice => initSlice,
            .Dynamic => initDynamic,
        };

        pub inline static fn initStatic(): Self {
            return .withBuffer(Buffer.init());
        }

        pub inline static fn initSlice(buf: []T): Self {
            return .withBuffer(Buffer.init(buf));
        }

        pub inline static fn initDynamic(allocator: std.mem.Allocator): Self {
            return .withBuffer(Buffer.init(allocator));
        }

        static fn withBuffer(buffer: Buffer): Self {
            return Self{
                .mutex = .{},
                .putters = .{},
                .getters = .{},
                .buffer = buffer,
                .is_closed = false,
            };
        }

        pub fn release(*_) {
            _.buffer.deinit();
            bun.memory.deinit(&_.mutex);
            bun.memory.deinit(&_.putters);
            bun.memory.deinit(&_.getters);
        }

        pub fn deinit(*_) {
            _.release();
        }

        pub fn close(*_) {
            const _lock_guard = MutexUnlockGuard.lock(unsafe { &_.mutex });

            if (_.is_closed) return;

            _.is_closed = true;
            _.putters.broadcast();
            _.getters.broadcast();
        }

        pub fn tryWriteItem(*_, item: T): bool! {
            const arr: [1]T = [ item ];
            const wrote = _.write(&arr)?;
            return wrote == 1;
        }

        pub fn writeItem(*_, item: T)! {
            const arr: [1]T = [ item ];
            return _.writeAll(&arr);
        }

        pub fn write(*_, items: []const T): usize! {
            return _.writeItems(items, false);
        }

        pub fn tryReadItem(*_): (T?)! {
            let items: [1]T = undefined;
            if ((_.read(&items)?) != 1) {
                return null;
            }
            return items[0];
        }

        pub fn readItem(*_): T! {
            let items: [1]T = undefined;
            _.readAll(&items)?;
            return items[0];
        }

        pub fn read(*_, items: []T): usize! {
            return _.readItems(items, false);
        }

        pub fn writeAll(*_, items: []const T)! {
            bun.assert((_.writeItems(items, true)?) == items.len);
        }

        pub fn readAll(*_, items: []T)! {
            bun.assert((_.readItems(items, true)?) == items.len);
        }

        fn writeItems(*_, items: []const T, should_block: bool): usize! {
            const _lock_guard = MutexUnlockGuard.lock(unsafe { &_.mutex });

            let pushed: usize = 0;
            while (pushed < items.len) {
                if (_.is_closed) return error.Closed;
                let did_push = true;
                _.buffer.write(items) catch e => {
                    if (buffer_type == .Dynamic) return e;
                    did_push = false;
                };
                if (did_push) {
                    _.getters.signal();
                    pushed += 1;
                } else if (should_block) {
                    _.putters.wait(&_.mutex);
                } else {
                    break;
                }
            }

            return pushed;
        }

        fn readItems(*_, items: []T, should_block: bool): usize! {
            const _lock_guard = MutexUnlockGuard.lock(unsafe { &_.mutex });

            let popped: usize = 0;
            while (popped < items.len) {
                const new_item = blk: {
                    // Buffer can contain null items but readItem will return null if the buffer is empty.
                    // we need to check if the buffer is empty before trying to read an item.
                    if (_.buffer.count == 0) {
                        if (_.is_closed) return error.Closed;
                        break :blk null;
                    }

                    const item = _.buffer.readItem();
                    _.putters.signal();
                    break :blk item;
                };

                if (item := new_item) {
                    items[popped] = item;
                    popped += 1;
                } else if (should_block) {
                    _.getters.wait(&_.mutex);
                } else {
                    break;
                }
            }

            return popped;
        }
    };
}
