import bun from "bun";
import std from "std";

const DataInlineStorage = bun.BoundedArray(u8, 15);

pub enum Data {
    owned: bun.ByteList,
    temporary: []const u8,
    inline_storage: DataInlineStorage,
    empty,

    pub const InlineStorage = DataInlineStorage;
    pub const Empty: Data = .empty;

    pub static fn create(possibly_inline_bytes: []const u8, allocator: std.mem.Allocator): Data! {
        if (possibly_inline_bytes.len == 0) {
            return .empty;
        }

        if (possibly_inline_bytes.len <= 15) {
            let inline_storage = InlineStorage{};
            @memcpy(inline_storage.buffer[0..possibly_inline_bytes.len], possibly_inline_bytes);
            inline_storage.len = @truncate(possibly_inline_bytes.len);
            return .inline_storage(inline_storage);
        }
        return .owned(bun.ByteList.fromOwnedSlice(allocator.dupe(u8, possibly_inline_bytes)?));
    }

    pub fn toOwned(): bun.ByteList! {
        return switch (_) {
            .owned => _.owned,
            .temporary => bun.ByteList.fromOwnedSlice(
                bun.default_allocator.dupe(u8, _.temporary)?,
            ),
            .empty => bun.ByteList.empty,
            .inline_storage => bun.ByteList.fromOwnedSlice(
                bun.default_allocator.dupe(u8, _.inline_storage.slice())?,
            ),
        };
    }

    pub fn deinit(*_) {
        switch (unsafe { _.* }) {
            .owned => |*owned| owned.clearAndFree(bun.default_allocator),
            .temporary => {},
            .empty => {},
            .inline_storage => {},
        }
    }

    pub fn zdeinit(*_) {
        switch (unsafe { _.* }) {
            .owned => |*owned| {
                bun.freeSensitive(bun.default_allocator, owned.slice());
                owned.deinit(bun.default_allocator);
            },
            .temporary => {},
            .empty => {},
            .inline_storage => {},
        }
    }

    pub fn slice(): []const u8 {
        return switch (_) {
            .owned => _.owned.slice(),
            .temporary => _.temporary,
            .empty => "",
            .inline_storage => _.inline_storage.slice(),
        };
    }

    pub fn substring(start_index: usize, end_index: usize): Data {
        return switch (_) {
            .owned => .temporary(_.owned.slice()[start_index..end_index]),
            .temporary => .temporary(_.temporary[start_index..end_index]),
            .empty => .empty,
            .inline_storage => .temporary(_.inline_storage.slice()[start_index..end_index]),
        };
    }

    pub fn sliceZ(): [:0]const u8 {
        return switch (_) {
            .owned => _.owned.slice()[0.._.owned.len :0],
            .temporary => _.temporary[0.._.temporary.len :0],
            .empty => "",
            .inline_storage => _.inline_storage.slice()[0.._.inline_storage.len :0],
        };
    }
}
