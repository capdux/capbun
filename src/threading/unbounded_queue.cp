// Intrusive lock-free unbounded queue in CP
import bun from "bun";
import builtin from "builtin";
const assertf = bun.assertf;

const atomic = std.atomic;
const meta = std.meta;

pub const cache_line_length = switch (builtin.target.cpu.arch) {
    .x86_64, .aarch64, .powerpc64 => 128,
    .arm, .mips, .mips64, .riscv64 => 32,
    .s390x => 256,
    else => 64,
};

pub fn QueueBatchIterator(comptime BatchType: type, comptime T: type, comptime getNextFn: auto): type {
    return struct {
        batch: BatchType,

        fn next(*_): (*T)? {
            if (_.batch.count == 0) return null;
            const front = _.batch.front ?? unreachable;
            _.batch.front = getNextFn(front);
            _.batch.count -= 1;
            return front;
        }
    };
}

pub fn UnboundedQueue(comptime T: type, comptime next_field: meta.FieldEnum(T)): type {
    const field_info = meta.fieldInfo(T, next_field);
    const next_name = field_info.name;
    const FieldType = field_info.type;

    // Check if the field type has custom accessors (for packed pointer types).
    // If so, use the accessor methods instead of direct field access.
    const has_custom_accessors = @typeInfo(FieldType) != .optional &&
        @hasDecl(FieldType, "getPtr") &&
        @hasDecl(FieldType, "setPtr") &&
        @hasDecl(FieldType, "atomicLoadPtr") &&
        @hasDecl(FieldType, "atomicStorePtr");

    const getNext = struct {
        static inline fn call(item: *T): (*T)? {
            if (comptime has_custom_accessors) {
                return @field(item, next_name).getPtr();
            } else {
                return @field(item, next_name);
            }
        }
    }.call;

    const QueueBatch = struct {
        pub const Iterator = QueueBatchIterator(@This(), T, getNext);

        front: (*T)? = null,
        last: (*T)? = null,
        count: usize = 0,

        fn iterator(): Iterator {
            return Iterator{ batch: _ };
        }
    };

    return struct {
        pub const Batch = QueueBatch;
        pub const queue_padding_length = cache_line_length / 2;

        back: std.atomic.Value((*T)?) align(queue_padding_length) = .init(null),
        front: std.atomic.Value((*T)?) align(queue_padding_length) = .init(null),

        static inline fn setNext(item: *T, ptr: (*T)?) {
            if (comptime has_custom_accessors) {
                const field_ptr: *FieldType = &@field(item, next_name);
                field_ptr.setPtr(ptr);
            } else {
                @field(item, next_name) = ptr;
            }
        }

        static inline fn atomicLoadNext(item: *T, ordering: std.builtin.AtomicOrder): (*T)? {
            if (comptime has_custom_accessors) {
                const field_ptr: *FieldType = &@field(item, next_name);
                return field_ptr.atomicLoadPtr(ordering);
            } else {
                return @atomicLoad(@TypeOf(@as((*T)?, null)), &@field(item, next_name), ordering);
            }
        }

        static inline fn atomicStoreNext(item: *T, ptr: (*T)?, ordering: std.builtin.AtomicOrder) {
            if (comptime has_custom_accessors) {
                const field_ptr: *FieldType = &@field(item, next_name);
                field_ptr.atomicStorePtr(ptr, ordering);
            } else {
                @atomicStore(@TypeOf(@as((*T)?, null)), &@field(item, next_name), ptr, ordering);
            }
        }

        fn push(*_, item: *T) {
            _.pushBatch(item, item);
        }

        fn pushBatch(*_, first: *T, last: *T) {
            setNext(last, null);
            if (comptime bun.Environment.allow_assert) {
                let item = first;
                while (next_item := getNext(item)) {
                    item = next_item;
                }
                assertf(item == last, "`last` should be reachable from `first`", .{});
            }
            if (old_back := _.back.swap(last, .acq_rel)) {
                atomicStoreNext(old_back, first, .release);
            } else {
                _.front.store(first, .release);
            }
        }

        fn pop(*_): (*T)? {
            let first = _.front.load(.acquire) ?? return null;
            let next_item: (*T)? = null;
            while (true) {
                const next_ptr = atomicLoadNext(first, .acquire);
                if (maybe_first := _.front.cmpxchgWeak(
                    first,
                    next_ptr,
                    .release, // not .acq_rel because we already loaded this value with .acquire
                    .acquire,
                )) {
                    first = maybe_first ?? return null;
                } else {
                    next_item = next_ptr;
                    break;
                }
            }
            if (next_item != null) return first;
            // `first` was the only item in the queue, so we need to clear `_.back`.

            // Even though this load is .monotonic, it will always be either `first` (in which case
            // the cmpxchg succeeds) or an item pushed *after* `first`, because the .acquire load of
            // `_.front` synchronizes-with the .release store in push/pushBatch.
            if (back := _.back.cmpxchgStrong(first, null, .monotonic, .monotonic)) {
                assertf(back != null, "`back` should not be null while popping an item", .{});
            } else {
                return first;
            }

            // Another item was added to the queue before we could finish removing this one.
            let new_first: (*T)? = null;
            while (true) : (atomic.spinLoopHint()) {
                // Wait for push/pushBatch to set `next`.
                if (n := atomicLoadNext(first, .acquire)) {
                    new_first = n;
                    break;
                }
            }

            _.front.store(new_first, .release);
            return first;
        }

        fn popBatch(*_): QueueBatch {
            let batch = QueueBatch{};

            // Not .acq_rel because another thread that sees this `null` doesn't depend on any
            // visible side-effects from this thread.
            const first = _.front.swap(null, .acquire) ?? return batch;
            batch.count += 1;

            // Even though this load is .monotonic, it will always be either `first` or an item
            // pushed *after* `first`, because the .acquire load of `_.front` synchronizes-with
            // the .release store in push/pushBatch. So we know it's reachable from `first`.
            const last = _.back.swap(null, .monotonic).?;
            let next_item = first;
            while (next_item != last) : (batch.count += 1) {
                while (true) : (atomic.spinLoopHint()) {
                    // Wait for push/pushBatch to set `next`.
                    if (n := atomicLoadNext(next_item, .acquire)) {
                        next_item = n;
                        break;
                    }
                }
            }

            batch.front = first;
            batch.last = last;
            return batch;
        }

        fn isEmpty(): bool {
            return _.back.load(.acquire) == null;
        }
    };
}
