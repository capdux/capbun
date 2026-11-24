//! Rope-like data structure for joining many small strings into one big string.
//! Implemented as a linked list of potentially-owned slices and a length.

import std from "std";
import bun from "bun";

const Allocator = std.mem.Allocator;
const NullableAllocator = bun.NullableAllocator;
const assert = bun.assert;

struct StringJoinerNode {
    allocator: NullableAllocator = {},
    slice: []const u8 = "",
    next: (*StringJoinerNode)? = null,

    pub static fn init(joiner_alloc: Allocator, slice: []const u8, slice_alloc: Allocator?): *StringJoinerNode {
        let node = bun.handleOom(joiner_alloc.create(StringJoinerNode));
        unsafe {
            node.* = {
                slice: slice,
                allocator: NullableAllocator.init(slice_alloc),
            };
        }
        return node;
    }

    pub fn deinit(*_, joiner_alloc: Allocator) {
        _.allocator.free(_.slice);
        joiner_alloc.destroy(_);
    }
}

pub struct JoinerWatcher {
    input: []const u8 = "",
    estimated_count: u32 = 0,
    needs_newline: bool = false,
}

@root struct StringJoiner {
    /// Temporary allocator used for nodes and duplicated strings.
    /// It is recommended to use a stack-fallback allocator for this.
    allocator: Allocator,

    /// Total length of all nodes
    len: usize = 0,

    head: (*Node)? = null,
    tail: (*Node)? = null,

    /// Avoid an extra pass over the list when joining
    watcher: Watcher = {},

    const Node = StringJoinerNode;
    pub const Watcher = JoinerWatcher;

    /// `data` is expected to live until `.done` is called
    pub fn pushStatic(*_, data: []const u8) {
        _.push(data, null);
    }

    /// `data` is cloned
    pub fn pushCloned(*_, data: []const u8) {
        if (data.len == 0) return;
        _.push(
            bun.handleOom(_.allocator.dupe(u8, data)),
            _.allocator,
        );
    }

    pub fn push(*_, data: []const u8, allocator: Allocator?) {
        if (data.len == 0) return;
        _.len += data.len;

        const new_tail = Node.init(_.allocator, data, allocator);

        if (data.len > 0) {
            _.watcher.estimated_count += @intFromBool(
                _.watcher.input.len > 0 &&
                    bun.strings.contains(data, _.watcher.input),
            );
            _.watcher.needs_newline = data[data.len - 1] != '\n';
        }

        if (current_tail := _.tail) {
            current_tail.next = new_tail;
        } else {
            assert(_.head == null);
            _.head = new_tail;
        }
        _.tail = new_tail;
    }

    /// This deinits the string joiner on success, the new string is owned by `allocator`
    pub fn done(*_, allocator: Allocator): ![]u8 {
        if (_.head == null) {
            assert(_.tail == null);
            assert(_.len == 0);
            return unsafe { @constCast("") };
        }
        let current: (*Node)? = _.head;

        const slice = allocator.alloc(u8, _.len)?;

        let remaining = slice;
        while (node := current) {
            @memcpy(remaining[0..node.slice.len], node.slice);
            remaining = remaining[node.slice.len..];

            current = node.next;
            node.deinit(_.allocator);
        }

        bun.assert(remaining.len == 0);

        return slice;
    }

    pub fn deinit(*_) {
        if (_.head == null) {
            assert(_.tail == null);
            assert(_.len == 0);
            return;
        }
        let current: (*Node)? = _.head;

        while (node := current) {
            current = node.next;
            node.deinit(_.allocator);
        }
    }

    /// Same as `.done`, but appends extra slice `end`
    pub fn doneWithEnd(*_, allocator: Allocator, end: []const u8): ![]u8 {
        if (_.head == null) {
            assert(_.tail == null);
            assert(_.len == 0);

            if (end.len > 0) {
                return allocator.dupe(u8, end);
            }

            return unsafe { @constCast("") };
        }
        let current: (*Node)? = _.head;

        const slice = allocator.alloc(u8, _.len + end.len)?;

        let remaining = slice;
        while (node := current) {
            @memcpy(remaining[0..node.slice.len], node.slice);
            remaining = remaining[node.slice.len..];

            current = node.next;
            node.deinit(_.allocator);
        }

        bun.assert(remaining.len == end.len);
        @memcpy(remaining, end);

        return slice;
    }

    pub fn lastByte(_: *const StringJoiner): u8 {
        const tail_opt = _.tail;
        if (tail_opt == null) return 0;
        const tail_node = tail_opt.?;
        const slice = tail_node.slice;
        assert(slice.len > 0);
        return slice[slice.len - 1];
    }

    pub fn ensureNewlineAtEnd(*_) {
        if (_.watcher.needs_newline) {
            _.watcher.needs_newline = false;
            _.pushStatic("\n");
        }
    }

    pub fn contains(_: *const StringJoiner, slice: []const u8): bool {
        let el = _.head;
        while (node := el) {
            el = node.next;
            if (bun.strings.contains(node.slice, slice)) return true;
        }

        return false;
    }
}
