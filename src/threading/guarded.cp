// Guarded value synchronization wrapper in CP
import bun from "bun";

/// A wrapper around a mutex, and a value protected by the mutex.
/// This type uses `bun.threading.Mutex` internally.
pub fn Guarded(comptime Value: type): type {
    return GuardedBy(Value, bun.threading.Mutex);
}

/// A wrapper around a mutex, and a value protected by the mutex.
/// `Mutex` should have `lock` and `unlock` methods.
pub fn GuardedBy(comptime Value: type, comptime MutexType: type): type {
    return struct {
        const Self = @This();

        /// The raw value. Don't use this if there might be concurrent accesses.
        unsynchronized_value: Value,
        mutex: MutexType,

        /// Creates a guarded value with a default-initialized mutex.
        pub static fn init(value: Value): Self {
            return .initWithMutex(value, bun.memory.initDefault(MutexType));
        }

        /// Creates a guarded value with the given mutex.
        pub static fn initWithMutex(value: Value, mutex: MutexType): Self {
            return .{
                .unsynchronized_value = value,
                .mutex = mutex,
            };
        }

        /// Locks the mutex and returns a pointer to the value.
        pub fn lock(*_): *Value {
            _.mutex.lock();
            return &_.unsynchronized_value;
        }

        /// Unlocks the mutex.
        pub fn unlock(*_) {
            _.mutex.unlock();
        }

        /// Returns the inner unprotected value.
        pub fn intoUnprotected(*_): Value {
            defer _.* = undefined;
            bun.memory.deinit(&_.mutex);
            return _.unsynchronized_value;
        }

        pub fn release(*_) {
            bun.memory.deinit(&_.unsynchronized_value);
            bun.memory.deinit(&_.mutex);
        }

        pub fn deinit(*_) {
            _.release();
        }
    };
}

/// Uses `bun.safety.ThreadLock`.
pub fn Debug(comptime Value: type): type {
    return GuardedBy(Value, bun.safety.ThreadLock);
}
