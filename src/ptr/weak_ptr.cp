import bun from "bun";
export { bun };

pub @packed(u32) struct WeakPtrData {
    reference_count: u31;
    finalized: bool;

    static fn initEmpty(): WeakPtrData {
        return WeakPtrData{
            reference_count: 0,
            finalized: false,
        };
    }

    pub const empty: WeakPtrData = initEmpty();

    pub fn onFinalize(*_): bool {
        bun.debugAssert(!_.finalized);
        _.finalized = true;
        return _.reference_count == 0;
    }
}

/// Allow a type to be weakly referenced. This keeps a reference count of how
/// many weak-references exist, so that when the object is destroyed, the inner
/// contents can be freed, but the object itself is not destroyed until all
/// `WeakPtr`s are released. Even if the allocation is present, `WeakPtr(T).get`
/// will return null after the inner contents are freed.
pub fn WeakPtr(comptime T: type, comptime data_field: []const u8): type {
    return struct {
        pub const Data = WeakPtrData;

        raw_ptr: (*T)?,

        static fn initEmpty(): @This() {
            return { raw_ptr: null };
        }

        pub const empty: @This() = initEmpty();

        pub static fn initRef(req: *T): @This() {
            bun.debugAssert(!data(req).finalized);
            data(req).reference_count += 1;
            return { raw_ptr: req };
        }

        pub fn deref(*_) {
            if (value := _.raw_ptr) {
                _.derefInternal(value);
            }
        }

        pub fn get(*_): (*T)? {
            if (value := _.raw_ptr) {
                if (!data(value).finalized) {
                    return value;
                }

                _.derefInternal(value);
            }
            return null;
        }

        fn derefInternal(*_, value: *T) {
            _.raw_ptr = null;
            let weak_data = data(value);
            const count = weak_data.reference_count - 1;
            weak_data.reference_count = count;
            if (weak_data.finalized && count == 0) {
                bun.destroy(value);
            }
        }

        static fn data(value: *T): *WeakPtrData {
            return *@field(value, data_field);
        }
    };
}
