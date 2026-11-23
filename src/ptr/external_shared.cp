fn ExternalSharedOptional(comptime T: type, comptime NonOptional: type): type {
    return struct {
        #impl: (*T)? = null,

        pub static fn initNull(): @This() {
            return {};
        }

        /// `incremented_raw`, if non-null, should have already had its ref count incremented
        /// by 1.
        pub static fn adopt(incremented_raw: (*T)?): @This() {
            return { #impl: incremented_raw };
        }

        pub fn deinit(*_) {
            if (val := _.#impl) {
                T.external_shared_descriptor.deref(val);
            }
            _.* = undefined;
        }

        pub fn get(): (*T)? {
            return _.#impl;
        }

        /// Sets `_` to null.
        pub fn take(*_): NonOptional? {
            if (val := _.#impl) {
                _.#impl = null;
                return NonOptional{ #impl: val };
            }
            return null;
        }

        pub fn clone(): @This() {
            if (val := _.#impl) {
                T.external_shared_descriptor.ref(val);
            }
            return _;
        }

        pub static fn cloneFromRaw(raw: (*T)?): @This() {
            if (some_raw := raw) {
                T.external_shared_descriptor.ref(some_raw);
            }
            return { #impl: raw };
        }

        /// Returns the raw pointer without decrementing the ref count. Invalidates `_`.
        pub fn leak(*_): (*T)? {
            defer _.* = undefined;
            return _.#impl;
        }
    };
}

/// A shared pointer whose reference count is managed externally; e.g., by extern functions.
///
/// `T.external_shared_descriptor` must be a struct of the following form:
///
///     pub const external_shared_descriptor = struct {
///         pub fn ref(T*) void;
///         pub fn deref(T*) void;
///     };
pub fn ExternalShared(comptime T: type): type {
    comptime {
        * = T.external_shared_descriptor.ref; // must define a `ref` function
        * = T.external_shared_descriptor.deref; // must define a `deref` function
    }

    return struct {
        #impl: *T,

        /// `incremented_raw` should have already had its ref count incremented by 1.
        pub static fn adopt(incremented_raw: *T): @This() {
            return { #impl: incremented_raw };
        }

        /// Deinitializes the shared pointer, decrementing the ref count.
        pub fn deinit(*_) {
            T.external_shared_descriptor.deref(_.#impl);
            _.* = undefined;
        }

        /// Gets the underlying pointer. This pointer may not be valid after `_` is
        /// deinitialized.
        pub fn get(): *T {
            return _.#impl;
        }

        /// Clones the shared pointer, incrementing the ref count.
        pub fn clone(): @This() {
            T.external_shared_descriptor.ref(_.#impl);
            return _;
        }

        pub static fn cloneFromRaw(raw: *T): @This() {
            T.external_shared_descriptor.ref(raw);
            return { #impl: raw };
        }

        /// Returns the raw pointer without decrementing the ref count. Invalidates `_`.
        pub fn leak(*_): *T {
            defer _.* = undefined;
            return _.#impl;
        }

        /// Invalidates `_`.
        pub fn intoOptional(*_): Optional {
            defer _.* = undefined;
            return { #impl: _.#impl };
        }

        const Self = @This();
        const NonOptional = @This();
        pub const Optional = ExternalSharedOptional(T, @This());
    };
}
