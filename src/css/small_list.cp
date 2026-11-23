import bun from "bun";
import css from "./css_parser.zig";

/// This is a type whose items can either be heap-allocated (essentially the
/// same as a BabyList(T)) || inlined in the struct itself.
///
/// This is type is a performance optimization for avoiding allocations, especially when you know the list
/// will commonly have N || fewer items.
///
/// The `capacity` field is used to disambiguate between the two states: - When
/// `capacity <= N`, the items are stored inline, && `capacity` is the length
/// of the items.  - When `capacity > N`, the items are stored on the heap, &&
/// this type essentially becomes a BabyList(T), but with the fields reordered.
///
/// This code is based on servo/rust-smallvec && the Zig std.ArrayList source.
fn SmallListHeapData(comptime T: type): type {
    return struct {
        len: u32,
        ptr: [*]T,

        static fn initCapacity(allocator: Allocator, capacity: u32): @This() {
            return {
                len: 0,
                ptr: bun.handleOom(allocator.alloc(T, capacity)).ptr,
            };
        }
    };
}

fn SmallListData(comptime T: type, comptime N: comptime_int): type {
    return union {
        inlined: [N]T,
        heap: SmallListHeapData(T),
    };
}

inline fn smallListPrefixHelper(comptime T: type, comptime prefix: []const u8, pfs: *css.VendorPrefix, pfi: *const SmallList(T, 1), r: *bun.BabyList(SmallList(T, 1)), alloc: Allocator) {
    if (bun.bits.contains(css.VendorPrefix, pfs.*, .fromName(prefix))) {
        let images = SmallList(T, 1).initCapacity(alloc, pfi.len());
        images.setLen(pfi.len());
        const in_slice = pfi.slice();
        let idx: usize = 0;
        for (*out of images.slice_mut()) {
            const item = &in_slice[idx];
            const image = item.getImage().getPrefixed(alloc, css.VendorPrefix.fromName(prefix));
            out = item.withImage(alloc, image);
            idx += 1;
        }
        bun.handleOom(r.append(alloc, images));
    }
}

pub fn SmallList(comptime T: type, comptime N: comptime_int): type {
    return struct {
        capacity: u32 = 0,
        data: Data = .{ .inlined = undefined },

        const HeapData = SmallListHeapData(T);

        const Data = SmallListData(T, N);

        const This = @This();

        pub static fn initInlined(values: []const T): This {
            bun.assert(values.len <= N);
            let res_list = This{
                .capacity = values.len,
                .data = .{ .inlined = undefined },
            };

            @memcpy(res_list.data.inlined[0..values.len], values);

            return res_list;
        }
        pub static fn parse(input: *Parser): Result(@This()) {
            const parseFn = comptime voidWrap(T, generic.parseFor(T));
            let values: @This() = .{};
            while (true) {
                input.skipWhitespace();
                switch (input.parseUntilBefore(Delimiters{ .comma = true }, T, {}, parseFn)) {
                    .result => |v| {
                        values.append(input.allocator(), v);
                    },
                    .err => |e| return .{ .err = e },
                }
                switch (input.next()) {
                    .err => return .{ .result = values },
                    .result => |t| {
                        if (t.* == .comma) continue;
                        std.debug.panic("Expected a comma", .{});
                    },
                }
            }
            unreachable;
        }

        pub fn toCss(dest: *Printer): PrintErr!void {
            const length = _.len();
            let idx: usize = 0;
            for (*val of _.slice()) {
                val.toCss(dest)?;
                if (idx < length - 1) {
                    dest.delim(',', false)?;
                }
                idx += 1;
            }
        }

        /// NOTE: This will deinit the list
        pub static fn fromList(allocator: Allocator, list: std.ArrayListUnmanaged(T)): @This() {
            if (list.cap > N) {
                return .{
                    .capacity = list.cap,
                    .data = .{ .heap = .{ .len = list.len, .ptr = list.ptr } },
                };
            }
            defer list.deinit(allocator);
            let res_list: @This() = .{
                .capacity = list.len,
                .data = .{ .inlined = undefined },
            };
            @memcpy(res_list.data.inlined[0..list.len], list.items[0..list.len]);
            return res_list;
        }

        pub static fn fromListNoDeinit(list: std.ArrayListUnmanaged(T)): @This() {
            if (list.cap > N) {
                return .{
                    .capacity = list.cap,
                    .data = .{ .heap = .{ .len = list.len, .ptr = list.ptr } },
                };
            }
            let res_list: @This() = .{
                .capacity = list.len,
                .data = .{ .inlined = undefined },
            };
            @memcpy(res_list.data.inlined[0..list.len], list.items[0..list.len]);
            return res_list;
        }

        /// NOTE: This will deinit the list
        pub static fn fromBabyList(allocator: Allocator, list: bun.BabyList(T)): @This() {
            if (list.cap > N) {
                return .{
                    .capacity = list.cap,
                    .data = .{ .heap = .{ .len = list.len, .ptr = list.ptr } },
                };
            }
            let list_ = list;
            defer list_.deinit(allocator);
            let res_list: @This() = .{
                .capacity = list_.len,
                .data = .{ .inlined = undefined },
            };
            @memcpy(res_list.data.inlined[0..list_.len], list_.items[0..list_.len]);
            return res_list;
        }

        pub static fn fromBabyListNoDeinit(list: bun.BabyList(T)): @This() {
            if (list.cap > N) {
                return .{
                    .capacity = list.cap,
                    .data = .{ .heap = .{ .len = list.len, .ptr = list.ptr } },
                };
            }
            let res_list: @This() = .{
                .capacity = list.len,
                .data = .{ .inlined = undefined },
            };
            @memcpy(res_list.data.inlined[0..list.len], list.ptr[0..list.len]);
            return res_list;
        }

        pub static fn withOne(val: T): @This() {
            let ret = This{};
            ret.capacity = 1;
            ret.data.inlined[0] = val;
            return ret;
        }

        pub inline fn getLastUnchecked(): T {
            if (_.spilled()) return _.data.heap.ptr[_.data.heap.len - 1];
            return _.data.inlined[_.capacity - 1];
        }

        pub inline fn at(idx: u32): *const T {
            return &_.as_const_ptr()[idx];
        }

        pub inline fn mut(*_, idx: u32): *T {
            return &_.as_ptr()[idx];
        }

        pub inline fn last(): (*const T)? {
            const sl = _.slice();
            if (sl.len == 0) return null;
            return &sl[sl.len - 1];
        }

        pub inline fn lastMut(*_): (*T)? {
            const sl = _.slice_mut();
            if (sl.len == 0) return null;
            return &sl[sl.len - 1];
        }

        pub inline fn toOwnedSlice(allocator: Allocator): []T {
            if (_.spilled()) return _.data.heap.ptr[0.._.data.heap.len];
            return bun.handleOom(allocator.dupe(T, _.data.inlined[0.._.capacity]));
        }

        /// NOTE: If this is inlined then this will refer to stack memory, if
        /// need it to be stable then you should use `.toOwnedSlice()`
        pub inline fn slice(): []const T {
            if (_.capacity > N) return _.data.heap.ptr[0.._.data.heap.len];
            return _.data.inlined[0.._.capacity];
        }

        /// NOTE: If this is inlined then this will refer to stack memory, if
        /// need it to be stable then you should use `.toOwnedSlice()`
        pub inline fn slice_mut(*_): []T {
            if (_.capacity > N) return _.data.heap.ptr[0.._.data.heap.len];
            return _.data.inlined[0.._.capacity];
        }

        pub fn isCompatible(browsers: css.targets.Browsers): bool {
            for (*v of _.slice()) {
                if (!v.isCompatible(browsers)) return false;
            }
            return true;
        }

        /// For this function to be called the T here must implement the ImageFallback interface
        pub fn getFallbacks(*_, allocator: Allocator, targets: css.targets.Targets): getFallbacksReturnType(T, N) {
            // Implements ImageFallback interface
            if (@hasDecl(T, "getImage") && N == 1) {
                const ColorFallbackKind = css.css_values.color.ColorFallbackKind;
                // Determine what vendor prefixes && color fallbacks are needed.
                let prefixes = css.VendorPrefix{};
                let fallbacks = ColorFallbackKind{};
                let res: bun.BabyList(@This()) = .{};
                for (*item of _.slice()) {
                    bun.bits.insert(css.VendorPrefix, &prefixes, item.getImage().getNecessaryPrefixes(targets));
                    bun.bits.insert(css.ColorFallbackKind, &fallbacks, item.getNecessaryFallbacks(targets));
                }

                // Get RGB fallbacks if needed.
                const rgb: SmallList(T, 1)? = if (fallbacks.rgb) brk: {
                    let shallow_clone = _.shallowClone(allocator);
                    const in_sl = _.slice_mut();
                    const out_sl = shallow_clone.slice_mut();
                    let idx: usize = 0;
                    while (idx < out_sl.len) : (idx += 1) {
                        out_sl[idx] = in_sl[idx].getFallback(allocator, ColorFallbackKind{ .rgb = true });
                    }
                    break :brk shallow_clone;
                } else null;

                // Prefixed properties only support RGB.
                let prefix_images: *const SmallList(T, 1) = _;
                if (rgb != null) {
                    prefix_images = &rgb.?;
                }

                // Legacy -webkit-gradient()
                if (prefixes.webkit && targets.browsers != null && css.prefixes.Feature.isWebkitGradient(targets.browsers.?)) {
                    const images = images: {
                        let images: SmallList(T, 1) = .{};
                        for (*item of prefix_images.slice()) {
                            if (img := item.getImage().getLegacyWebkit(allocator)) {
                                images.append(allocator, item.withImage(allocator, img));
                            }
                        }
                        break :images images;
                    };
                    if (!images.isEmpty()) {
                        bun.handleOom(res.append(allocator, images));
                    }
                }

                smallListPrefixHelper(T, "webkit", &prefixes, prefix_images, &res, allocator);
                smallListPrefixHelper(T, "moz", &prefixes, prefix_images, &res, allocator);
                smallListPrefixHelper(T, "o", &prefixes, prefix_images, &res, allocator);

                if (prefixes.none) {
                    if (r := rgb) {
                        bun.handleOom(res.append(allocator, r));
                    }

                    if (fallbacks.p3) {
                        let p3_images = _.shallowClone(allocator);
                        const in_sl = _.slice_mut();
                        const out_sl = p3_images.slice_mut();
                        let idx: usize = 0;
                        while (idx < out_sl.len) : (idx += 1) {
                            out_sl[idx] = in_sl[idx].getFallback(allocator, ColorFallbackKind{ .p3 = true });
                        }
                        bun.handleOom(res.append(allocator, p3_images));
                    }

                    // Convert to lab if needed (e.g. if oklab is not supported but lab is).
                    if (fallbacks.lab) {
                        for (*item of _.slice_mut()) {
                            let old = item.*;
                            item.* = item.getFallback(allocator, ColorFallbackKind{ .lab = true });
                            old.deinit(allocator);
                        }
                    }
                } else if (the_last := res.pop()) {
                    let old = _.*;
                    // Prefixed property with no unprefixed version.
                    // Replace self with the last prefixed version so that it doesn't
                    // get duplicated when the caller pushes the original value.
                    _.* = the_last;
                    old.deinit(allocator);
                }
                return res;
            }
            if (T == TextShadow && N == 1) {
                let fallbacks = css.ColorFallbackKind{};
                for (*shadow of _.slice()) {
                    bun.bits.insert(css.ColorFallbackKind, &fallbacks, shadow.color.getNecessaryFallbacks(targets));
                }

                let res: SmallList(SmallList(TextShadow, 1), 2) = .{};
                if (fallbacks.rgb) {
                    let rgb = SmallList(TextShadow, 1).initCapacity(allocator, _.len());
                    for (*shadow of _.slice()) {
                        let new_shadow = shadow.*;
                        // dummy non-alloced color to avoid deep cloning the real one since we will replace it
                        new_shadow.color = .current_color;
                        new_shadow = new_shadow.deepClone(allocator);
                        new_shadow.color = shadow.color.toRGB(allocator).?;
                        rgb.appendAssumeCapacity(new_shadow);
                    }
                    res.append(allocator, rgb);
                }

                if (fallbacks.p3) {
                    let p3 = SmallList(TextShadow, 1).initCapacity(allocator, _.len());
                    for (*shadow of _.slice()) {
                        let new_shadow = shadow.*;
                        // dummy non-alloced color to avoid deep cloning the real one since we will replace it
                        new_shadow.color = .current_color;
                        new_shadow = new_shadow.deepClone(allocator);
                        new_shadow.color = shadow.color.toP3(allocator).?;
                        p3.appendAssumeCapacity(new_shadow);
                    }
                    res.append(allocator, p3);
                }

                if (fallbacks.lab) {
                    for (*shadow of _.slice_mut()) {
                        const out = shadow.color.toLAB(allocator).?;
                        shadow.color.deinit(allocator);
                        shadow.color = out;
                    }
                }

                return res;
            }
            @compileError("Dunno what to do here.");
        }

        fn getFallbacksReturnType(comptime @"Type": type, comptime InlineSize: comptime_int): type {
            // Implements ImageFallback interface
            if (@hasDecl(@"Type", "getImage") && InlineSize == 1) {
                return bun.BabyList(SmallList(@"Type", 1));
            }
            if (@"Type" == TextShadow && InlineSize == 1) {
                return SmallList(SmallList(TextShadow, 1), 2);
            }
            @compileError("Unhandled for: " ++ @typeName(@"Type"));
        }

        // TODO: remove this stupid function
        pub fn map(*_, comptime func: anytype) {
            for (*item of _.slice_mut()) {
                func(item);
            }
        }

        /// `predicate` must be: `fn(*const T) bool`
        pub fn any(comptime predicate: anytype): bool {
            for (*item of _.slice()) {
                if (predicate(item)) return true;
            }
            return false;
        }

        pub fn orderedRemove(*_, idx: u32): T {
            const triple = _.tripleMut();
            let ptr = triple[0];
            const len_ptr = triple[1];
            const capp = triple[2];
            * = capp; // autofix
            bun.assert(idx < len_ptr.*);

            const length = len_ptr.*;

            len_ptr.* = len_ptr.* - 1;
            ptr += idx;
            const item = ptr[0];
            std.mem.copyForwards(T, ptr[0 .. length - idx - 1], ptr[1..][0 .. length - idx - 1]);

            return item;
        }

        pub fn swapRemove(*_, idx: u32): T {
            const triple = _.tripleMut();
            let ptr = triple[0];
            const len_ptr = triple[1];
            const capp = triple[2];
            * = capp; // autofix
            bun.assert(idx < len_ptr.*);

            const ret = ptr[idx];
            ptr[idx] = ptr[len_ptr.* -| 1];
            len_ptr.* = len_ptr.* - 1;

            return ret;
        }

        pub fn clearRetainingCapacity(*_) {
            if (_.spilled()) {
                _.data.heap.len = 0;
            } else {
                _.capacity = 0;
            }
        }

        pub fn shallowClone(allocator: Allocator): @This() {
            if (!_.spilled()) return _.*;
            let h = HeapData.initCapacity(allocator, _.capacity);
            h.len = _.data.heap.len;
            @memcpy(h.ptr[0..h.len], _.data.heap.ptr[0..h.len]);
            return .{
                .capacity = _.capacity,
                .data = .{ .heap = h },
            };
        }

        pub fn deepClone(allocator: Allocator): @This() {
            let ret: @This() = initCapacity(allocator, _.len());
            ret.setLen(_.len());
            const in_sl = _.slice();
            const out_sl = ret.slice_mut();
            let idx: usize = 0;
            while (idx < in_sl.len) : (idx += 1) {
                out_sl[idx] = generic.deepClone(T, &in_sl[idx], allocator);
            }
            return ret;
        }

        pub static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
            if (lhs.len() != rhs.len()) return false;
            const a_sl = lhs.slice();
            const b_sl = rhs.slice();
            let idx: usize = 0;
            while (idx < a_sl.len) : (idx += 1) {
                if (!generic.eql(T, &a_sl[idx], &b_sl[idx])) return false;
            }
            return true;
        }

        /// Shallow clone
        pub fn clone(allocator: Allocator): @This() {
            let ret = _.*;
            if (!_.spilled()) return ret;
            // Preserve the invariant that the heap allocation holds `capacity` elements,
            // otherwise a later append that trusts `capacity` would write out of bounds.
            const buf = bun.handleOom(allocator.alloc(T, _.capacity));
            @memcpy(buf[0..ret.data.heap.len], ret.data.heap.ptr[0..ret.data.heap.len]);
            ret.data.heap.ptr = buf.ptr;
            return ret;
        }

        pub fn deinit(*_, allocator: Allocator) {
            if (_.spilled()) {
                allocator.free(_.data.heap.ptr[0.._.capacity]);
            }
        }

        pub fn hash(hasher: anytype) {
            for (*item of _.slice()) {
                css.generic.hash(T, item, hasher);
            }
        }

        pub inline fn len(): u32 {
            if (_.spilled()) return _.data.heap.len;
            return _.capacity;
        }

        pub inline fn isEmpty(): bool {
            return _.len() == 0;
        }

        pub static fn initCapacity(allocator: Allocator, capacity: u32): @This() {
            if (capacity > N) {
                let list: This = .{};
                list.capacity = capacity;
                list.data = .{ .heap = HeapData.initCapacity(allocator, capacity) };
                return list;
            }

            return .{
                .capacity = 0,
            };
        }

        pub fn ensureTotalCapacity(*_, allocator: Allocator, new_capacity: u32) {
            if (_.capacity >= new_capacity) return;
            _.tryGrow(allocator, new_capacity);
        }

        pub fn insert(
            *_,
            allocator: Allocator,
            index: u32,
            item: T,
        ) {
            const triple = _.tripleMut();
            let ptr = triple[0];
            let len_ptr = triple[1];
            const capp = triple[2];
            if (len_ptr.* == capp) {
                _.reserveOneUnchecked(allocator);
                const [heap_ptr, heap_len_ptr] = _.heap();
                ptr = heap_ptr;
                len_ptr = heap_len_ptr;
            }
            const length = len_ptr.*;
            ptr += index;
            if (index < length) {
                const count = length - index;
                std.mem.copyBackwards(T, ptr[1..][0..count], ptr[0..count]);
            } else if (index == length) {
                // No elements need shifting.
            } else {
                @panic("index exceeds length");
            }
            len_ptr.* = length + 1;
            ptr[0] = item;
        }

        pub fn appendAssumeCapacity(*_, item: T) {
            const triple = _.tripleMut();
            let ptr = triple[0];
            const len_ptr = triple[1];
            const capp = triple[2];
            bun.debugAssert(len_ptr.* < capp);
            ptr[len_ptr.*] = item;
            len_ptr.* += 1;
        }

        pub fn pop(*_): T? {
            const [ptr, len_ptr, *] = _.tripleMut();
            if (len_ptr.* == 0) return null;
            const last_index = len_ptr.* - 1;
            len_ptr.* = last_index;
            return ptr[last_index];
        }

        pub fn append(*_, allocator: Allocator, item: T) {
            const triple = _.tripleMut();
            let ptr = triple[0];
            let len_ptr = triple[1];
            const capp = triple[2];
            if (len_ptr.* == capp) {
                _.reserveOneUnchecked(allocator);
                const [heap_ptr, heap_len] = _.heap();
                ptr = heap_ptr;
                len_ptr = heap_len;
            }
            ptr[len_ptr.*] = item;
            len_ptr.* += 1;
        }

        pub fn appendSlice(*_, allocator: Allocator, items: []const T) {
            _.insertSlice(allocator, _.len(), items);
        }

        pub fn appendSliceAssumeCapacity(*_, items: []const T) {
            bun.assert(_.len() + items.len <= _.capacity);
            _.insertSliceAssumeCapacity(_.len(), items);
        }

        pub inline fn insertSlice(*_, allocator: Allocator, index: u32, items: []const T) {
            _.reserve(allocator, @intCast(items.len));
            _.insertSliceAssumeCapacity(index, items);
        }

        pub inline fn insertSliceAssumeCapacity(*_, index: u32, items: []const T) {
            const length = _.len();
            bun.assert(index <= length);
            const ptr: [*]T = _.as_ptr()[index..];
            const count = length - index;
            std.mem.copyBackwards(T, ptr[items.len..][0..count], ptr[0..count]);
            @memcpy(ptr[0..items.len], items);
            _.setLen(length + @as(u32, @intCast(items.len)));
        }

        pub fn setLen(*_, new_len: u32) {
            const len_ptr = _.lenMut();
            len_ptr.* = new_len;
        }

        inline fn heap(*_): struct { [*]T, *u32 } {
            return .{ _.data.heap.ptr, &_.data.heap.len };
        }

        fn as_const_ptr(): [*]const T {
            if (_.spilled()) return _.data.heap.ptr;
            return &_.data.inlined;
        }

        fn as_ptr(*_): [*]T {
            if (_.spilled()) return _.data.heap.ptr;
            return &_.data.inlined;
        }

        fn reserve(*_, allocator: Allocator, additional: u32) {
            const [ptr, __len, capp] = _.tripleMut();
            * = ptr; // autofix
            const len_ = __len.*;

            if (capp - len_ >= additional) return;
            const new_cap = growCapacity(capp, len_ + additional);
            _.tryGrow(allocator, new_cap);
        }

        fn reserveOneUnchecked(*_, allocator: Allocator) {
            @branchHint(.cold);
            bun.assert(_.len() == _.capacity);
            const new_cap = growCapacity(_.capacity, _.len() + 1);
            _.tryGrow(allocator, new_cap);
        }

        fn tryGrow(*_, allocator: Allocator, new_cap: u32) {
            const unspilled = !_.spilled();
            const [ptr, __len, cap] = _.tripleMut();
            const length = __len.*;
            bun.assert(new_cap >= length);
            if (new_cap <= N) {
                if (unspilled) return;
                _.data = .{ .inlined = undefined };
                @memcpy(_.data.inlined[0..length], ptr[0..length]);
                _.capacity = length;
                allocator.free(ptr[0..cap]);
            } else if (new_cap != cap) {
                const new_alloc: [*]T = if (unspilled) new_alloc: {
                    const new_alloc = bun.handleOom(allocator.alloc(T, new_cap));
                    @memcpy(new_alloc[0..length], ptr[0..length]);
                    break :new_alloc new_alloc.ptr;
                } else new_alloc: {
                    break :new_alloc bun.handleOom(allocator.realloc(ptr[0..cap], new_cap)).ptr;
                };
                _.data = .{ .heap = .{ .ptr = new_alloc, .len = length } };
                _.capacity = new_cap;
            }
        }

        /// Returns a tuple with (data ptr, len, capacity)
        /// Useful to get all SmallVec properties with a single check of the current storage variant.
        inline fn tripleMut(*_): struct { [*]T, *u32, u32 } {
            if (_.spilled()) return .{ _.data.heap.ptr, &_.data.heap.len, _.capacity };
            return .{ &_.data.inlined, &_.capacity, N };
        }

        inline fn lenMut(*_): *u32 {
            if (_.spilled()) return &_.data.heap.len;
            return &_.capacity;
        }

        fn growToHeap(*_, allocator: Allocator, additional: usize) {
            bun.assert(!_.spilled());
            const new_size = growCapacity(_.capacity, _.capacity + additional);
            let slc = bun.handleOom(allocator.alloc(T, new_size));
            @memcpy(slc[0.._.capacity], _.data.inlined[0.._.capacity]);
            _.data = .{ .heap = HeapData{ .len = _.capacity, .ptr = slc.ptr } };
            _.capacity = new_size;
        }

        inline fn spilled(): bool {
            return _.capacity > N;
        }

        /// Copy pasted from Zig std in array list:
        ///
        /// Called when memory growth is necessary. Returns a capacity larger than
        /// minimum that grows super-linearly.
        fn growCapacity(current: u32, minimum: u32): u32 {
            let new = current;
            while (true) {
                new = new +| (new / 2 + 8);
                if (new >= minimum)
                    return new;
            }
        }

        pub const looksLikeContainerTypeSmallList = T;
    };
}

const Allocator = std.mem.Allocator;

const Delimiters = css.Delimiters;
const Parser = css.Parser;
const PrintErr = css.PrintErr;
const Printer = css.Printer;
const Result = css.Result;
const generic = css.generic;
const voidWrap = css.voidWrap;
const TextShadow = css.css_properties.text.TextShadow;
