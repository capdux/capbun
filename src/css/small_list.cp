/// This is a type whose items can either be heap-allocated (essentially the
/// same as a BabyList(T)) or inlined in the struct itself.
///
/// This is type is a performance optimization for avoiding allocations, especially when you know the list
/// will commonly have N or fewer items.
///
/// The `capacity` field is used to disambiguate between the two states: - When
/// `capacity <= N`, the items are stored inline, and `capacity` is the length
/// of the items.  - When `capacity > N`, the items are stored on the heap, and
/// this type essentially becomes a BabyList(T), but with the fields reordered.
///
/// This code is based on servo/rust-smallvec and the Zig std.ArrayList source.
pub fn SmallList(comptime T: type, comptime N: comptime_int): type {
    return struct {
        capacity: u32 = 0,
        data: Data = { inlined: undefined },

        const Data = union {
            inlined: [N]T,
            heap: HeapData,
        };

        const HeapData = struct {
            len: u32,
            ptr: [*]T,

            pub fn initCapacity(allocator: Allocator, capacity: u32): HeapData {
                return {
                    len: 0,
                    ptr: bun.handleOom(allocator.alloc(T, capacity)).ptr,
                };
            }
        };

        const This = @This();

        pub fn initInlined(values: []const T): This {
            bun.assert(values.len <= N);
            let this = This{
                capacity: values.len,
                data: { inlined: undefined },
            };

            @memcpy(this.data.inlined[0..values.len], values);

            return this;
        }
        pub fn parse(input: *Parser): Result(@This()) {
            const parseFn = comptime voidWrap(T, generic.parseFor(T));
            let values: @This() = {};
            while (true) {
                input.skipWhitespace();
                switch (input.parseUntilBefore(Delimiters{ .comma = true }, T, {}, parseFn)) {
                    .result => |v| {
                        values.append(input.allocator(), v);
                    },
                    .err => |e| return { err: e },
                }
                switch (input.next()) {
                    .err => return { result: values },
                    .result => |t| {
                        if (t.* == .comma) continue;
                        std.debug.panic("Expected a comma", {});
                    },
                }
            }
            unreachable;
        }

        pub fn toCss(dest: *Printer): PrintErr!void {
            const length = _.len();
            const sl = _.slice();
            for (idx of 0..sl.len) {
                sl[idx].toCss(dest)?;
                if (idx < length - 1) {
                    dest.delim(',', false)?;
                }
            }
        }

        /// NOTE: This will deinit the list
        pub fn fromList(allocator: Allocator, list: std.ArrayListUnmanaged(T)): @This() {
            if (list.cap > N) {
                return {
                    capacity: list.cap,
                    data: { heap: { len: list.len, ptr: list.ptr } },
                };
            }
            defer list.deinit(allocator);
            let this: @This() = {
                capacity: list.len,
                data: { inlined: undefined },
            };
            @memcpy(this.data.inlined[0..list.len], list.items[0..list.len]);
            return this;
        }

        pub fn fromListNoDeinit(list: std.ArrayListUnmanaged(T)): @This() {
            if (list.cap > N) {
                return {
                    capacity: list.cap,
                    data: { heap: { len: list.len, ptr: list.ptr } },
                };
            }
            let this: @This() = {
                capacity: list.len,
                data: { inlined: undefined },
            };
            @memcpy(this.data.inlined[0..list.len], list.items[0..list.len]);
            return this;
        }

        /// NOTE: This will deinit the list
        pub fn fromBabyList(allocator: Allocator, list: bun.BabyList(T)): @This() {
            if (list.cap > N) {
                return {
                    capacity: list.cap,
                    data: { heap: { len: list.len, ptr: list.ptr } },
                };
            }
            let list_ = list;
            defer list_.deinit(allocator);
            let this: @This() = {
                capacity: list_.len,
                data: { inlined: undefined },
            };
            @memcpy(this.data.inlined[0..list_.len], list_.items[0..list_.len]);
            return this;
        }

        pub fn fromBabyListNoDeinit(list: bun.BabyList(T)): @This() {
            if (list.cap > N) {
                return {
                    capacity: list.cap,
                    data: { heap: { len: list.len, ptr: list.ptr } },
                };
            }
            let this: @This() = {
                capacity: list.len,
                data: { inlined: undefined },
            };
            @memcpy(this.data.inlined[0..list.len], list.ptr[0..list.len]);
            return this;
        }

        pub fn withOne(val: T): @This() {
            let ret = This{};
            ret.capacity = 1;
            ret.data.inlined[0] = val;
            return ret;
        }

        pub inline fn getLastUnchecked(this: *const @This()): T {
            if (this.spilled()) return this.data.heap.ptr[this.data.heap.len - 1];
            return this.data.inlined[this.capacity - 1];
        }

        pub inline fn at(this: *const @This(), idx: u32): *const T {
            return &this.as_const_ptr()[idx];
        }

        pub inline fn mut(this: *@This(), idx: u32): *T {
            return &this.as_ptr()[idx];
        }

        pub inline fn last(this: *const @This()): (*const T)? {
            const sl = this.slice();
            if (sl.len == 0) return null;
            return &sl[sl.len - 1];
        }

        pub inline fn lastMut(this: *@This()): (*T)? {
            const sl = this.slice_mut();
            if (sl.len == 0) return null;
            return &sl[sl.len - 1];
        }

        pub inline fn toOwnedSlice(this: *const @This(), allocator: Allocator): []T {
            if (this.spilled()) return this.data.heap.ptr[0..this.data.heap.len];
            return bun.handleOom(allocator.dupe(T, this.data.inlined[0..this.capacity]));
        }

        /// NOTE: If this is inlined then this will refer to stack memory, if
        /// need it to be stable then you should use `.toOwnedSlice()`
        pub inline fn slice(this: *const @This()): []const T {
            if (this.capacity > N) return this.data.heap.ptr[0..this.data.heap.len];
            return this.data.inlined[0..this.capacity];
        }

        /// NOTE: If this is inlined then this will refer to stack memory, if
        /// need it to be stable then you should use `.toOwnedSlice()`
        pub inline fn slice_mut(this: *@This()): []T {
            if (this.capacity > N) return this.data.heap.ptr[0..this.data.heap.len];
            return this.data.inlined[0..this.capacity];
        }

        pub fn isCompatible(this: *const @This(), browsers: css.targets.Browsers): bool {
            for (*v of this.slice()) {
                if (!v.isCompatible(browsers)) return false;
            }
            return true;
        }

        /// For this function to be called the T here must implement the ImageFallback interface
        pub fn getFallbacks(*_, allocator: Allocator, targets: css.targets.Targets): getFallbacksReturnType(T, N) {
            // Implements ImageFallback interface
            if (@hasDecl(T, "getImage") && N == 1) {
                const ColorFallbackKind = css.css_values.color.ColorFallbackKind;
                // Determine what vendor prefixes and color fallbacks are needed.
                let prefixes = css.VendorPrefix{};
                let fallbacks = ColorFallbackKind{};
                let res: bun.BabyList(@This()) = {};
                for (item of _.slice()) {
                    bun.bits.insert(css.VendorPrefix, &prefixes, item.getImage().getNecessaryPrefixes(targets));
                    bun.bits.insert(css.ColorFallbackKind, &fallbacks, item.getNecessaryFallbacks(targets));
                }

                // Get RGB fallbacks if needed.
                const rgb: SmallList?(T, 1) = if (fallbacks.rgb) brk: {
                    let shallow_clone = _.shallowClone(allocator);
                    let out_sl = shallow_clone.slice_mut();
                    const in_sl = _.slice_mut();
                    for (i of 0..out_sl.len) {
                        out_sl[i] = in_sl[i].getFallback(allocator, ColorFallbackKind{ .rgb = true });
                    }
                    break :brk shallow_clone;
                } else null;

                // Prefixed properties only support RGB.
                const prefix_images: *const SmallList(T, 1) = if (rgb) |*r| r else _;

                // Legacy -webkit-gradient()
                if (prefixes.webkit && targets.browsers != null && css.prefixes.Feature.isWebkitGradient(targets.browsers.?)) {
                    const images = images: {
                        let images = SmallList(T, 1){};
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

                const prefix = struct {
                    pub inline fn helper(comptime prefix: []const u8, pfs: *css.VendorPrefix, pfi: *const SmallList(T, 1), r: *bun.BabyList(This), alloc: Allocator) {
                        if (bun.bits.contains(css.VendorPrefix, pfs.*, .fromName(prefix))) {
                            let images = SmallList(T, 1).initCapacity(alloc, pfi.len());
                            images.setLen(pfi.len());
                            let out_sl = images.slice_mut();
                            const pfi_sl = pfi.slice();
                            for (i of 0..out_sl.len) {
                                const image = pfi_sl[i].getImage().getPrefixed(alloc, css.VendorPrefix.fromName(prefix));
                                out_sl[i] = pfi_sl[i].withImage(alloc, image);
                            }
                            bun.handleOom(r.append(alloc, images));
                        }
                    }
                }.helper;

                prefix("webkit", &prefixes, prefix_images, &res, allocator);
                prefix("moz", &prefixes, prefix_images, &res, allocator);
                prefix("o", &prefixes, prefix_images, &res, allocator);

                if (prefixes.none) {
                    if (r := rgb) {
                        bun.handleOom(res.append(allocator, r));
                    }

                    if (fallbacks.p3) {
                        let p3_images = _.shallowClone(allocator);
                        let out_sl = p3_images.slice_mut();
                        const in_sl = _.slice_mut();
                        for (i of 0..out_sl.len) {
                            out_sl[i] = in_sl[i].getFallback(allocator, ColorFallbackKind{ .p3 = true });
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
                    let old = this.*;
                    // Prefixed property with no unprefixed version.
                    // Replace self with the last prefixed version so that it doesn't
                    // get duplicated when the caller pushes the original value.
                    this.* = the_last;
                    old.deinit(allocator);
                }
                return res;
            }
            if (T == TextShadow && N == 1) {
                let fallbacks = css.ColorFallbackKind{};
                for (*shadow of this.slice()) {
                    bun.bits.insert(css.ColorFallbackKind, &fallbacks, shadow.color.getNecessaryFallbacks(targets));
                }

                let res = SmallList(SmallList(TextShadow, 1), 2){};
                if (fallbacks.rgb) {
                    let rgb = SmallList(TextShadow, 1).initCapacity(allocator, this.len());
                    for (*shadow of this.slice()) {
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
                    let p3 = SmallList(TextShadow, 1).initCapacity(allocator, this.len());
                    for (*shadow of this.slice()) {
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
                    for (*shadow of this.slice_mut()) {
                        const out = shadow.color.toLAB(allocator).?;
                        shadow.color.deinit(allocator);
                        shadow.color = out;
                    }
                }

                return res;
            }
            @compileError("Dunno what to do here.");
        }

        fn getFallbacksReturnType(comptime Type: type, comptime InlineSize: comptime_int): type {
            // Implements ImageFallback interface
            if (@hasDecl(Type, "getImage") && InlineSize == 1) {
                return bun.BabyList(SmallList(Type, 1));
            }
            if (Type == TextShadow && InlineSize == 1) {
                return SmallList(SmallList(TextShadow, 1), 2);
            }
            @compileError("Unhandled for: " ++ @typeName(Type));
        }

        // TODO: remove this stupid function
        pub fn map(this: *@This(), comptime func: auto) {
            for (*item of this.slice_mut()) {
                func(item);
            }
        }

        /// `predicate` must be: `fn(*const T) bool`
        pub fn any(this: *const @This(), comptime predicate: auto): bool {
            for (*item of this.slice()) {
                if (predicate(item)) return true;
            }
            return false;
        }

        pub fn orderedRemove(this: *@This(), idx: u32): T {
            let ptr, let len_ptr, const capp = this.tripleMut();
            * = capp; // autofix
            bun.assert(idx < len_ptr.*);

            const length = len_ptr.*;

            len_ptr.* = len_ptr.* - 1;
            ptr += idx;
            const item = ptr[0];
            std.mem.copyForwards(T, ptr[0 .. length - idx - 1], ptr[1..][0 .. length - idx - 1]);

            return item;
        }

        pub fn swapRemove(this: *@This(), idx: u32): T {
            let ptr, let len_ptr, const capp = this.tripleMut();
            * = capp; // autofix
            bun.assert(idx < len_ptr.*);

            const ret = ptr[idx];
            ptr[idx] = ptr[len_ptr.* -| 1];
            len_ptr.* = len_ptr.* - 1;

            return ret;
        }

        pub fn clearRetainingCapacity(this: *@This()) {
            if (this.spilled()) {
                this.data.heap.len = 0;
            } else {
                this.capacity = 0;
            }
        }

        pub fn shallowClone(this: *const @This(), allocator: Allocator): @This() {
            if (!this.spilled()) return this.*;
            let h = HeapData.initCapacity(allocator, this.capacity);
            h.len = this.data.heap.len;
            @memcpy(h.ptr[0..h.len], this.data.heap.ptr[0..h.len]);
            return {
                capacity: this.capacity,
                data: { heap: h },
            };
        }

        pub fn deepClone(allocator: Allocator): @This() {
            let ret: @This() = initCapacity(allocator, _.len());
            ret.setLen(_.len());
            const in_sl = _.slice();
            let out_sl = ret.slice_mut();
            for (i of 0..in_sl.len) {
                out_sl[i] = generic.deepClone(T, &in_sl[i], allocator);
            }
            return ret;
        }

        pub fn eql(lhs: *const @This(), rhs: *const @This()): bool {
            if (lhs.len() != rhs.len()) return false;
            const a_sl = lhs.slice();
            const b_sl = rhs.slice();
            for (i of 0..a_sl.len) {
                if (!generic.eql(T, &a_sl[i], &b_sl[i])) return false;
            }
            return true;
        }

        /// Shallow clone
        pub fn clone(this: *const @This(), allocator: Allocator): @This() {
            let ret = this.*;
            if (!this.spilled()) return ret;
            // Preserve the invariant that the heap allocation holds `capacity` elements,
            // otherwise a later append that trusts `capacity` would write out of bounds.
            const buf = bun.handleOom(allocator.alloc(T, this.capacity));
            @memcpy(buf[0..ret.data.heap.len], ret.data.heap.ptr[0..ret.data.heap.len]);
            ret.data.heap.ptr = buf.ptr;
            return ret;
        }

        pub fn deinit(this: *@This(), allocator: Allocator) {
            if (this.spilled()) {
                allocator.free(this.data.heap.ptr[0..this.capacity]);
            }
        }

        pub fn hash(this: *const @This(), hasher: auto) {
            for (*item of this.slice()) {
                css.generic.hash(T, item, hasher);
            }
        }

        pub inline fn len(this: *const @This()): u32 {
            if (this.spilled()) return this.data.heap.len;
            return this.capacity;
        }

        pub inline fn isEmpty(this: *const @This()): bool {
            return this.len() == 0;
        }

        pub fn initCapacity(allocator: Allocator, capacity: u32): @This() {
            if (capacity > N) {
                let list: This = {};
                list.capacity = capacity;
                list.data = { heap: HeapData.initCapacity(allocator, capacity) };
                return list;
            }

            return {
                capacity: 0,
            };
        }

        pub fn ensureTotalCapacity(this: *@This(), allocator: Allocator, new_capacity: u32) {
            if (this.capacity >= new_capacity) return;
            this.tryGrow(allocator, new_capacity);
        }

        pub fn insert(
            this: *@This(),
            allocator: Allocator,
            index: u32,
            item: T,
        ) {
            let ptr, let len_ptr, const capp = this.tripleMut();
            if (len_ptr.* == capp) {
                this.reserveOneUnchecked(allocator);
                const heap_ptr, const heap_len_ptr = this.heap();
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

        pub fn appendAssumeCapacity(this: *@This(), item: T) {
            let ptr, let len_ptr, const capp = this.tripleMut();
            bun.debugAssert(len_ptr.* < capp);
            ptr[len_ptr.*] = item;
            len_ptr.* += 1;
        }

        pub fn pop(this: *@This()): T? {
            const ptr, let len_ptr, const capp = this.tripleMut();
            * = capp;
            if (len_ptr.* == 0) return null;
            const last_index = len_ptr.* - 1;
            len_ptr.* = last_index;
            return ptr[last_index];
        }

        pub fn append(this: *@This(), allocator: Allocator, item: T) {
            let ptr, let len_ptr, const capp = this.tripleMut();
            if (len_ptr.* == capp) {
                this.reserveOneUnchecked(allocator);
                const heap_ptr, const heap_len = this.heap();
                ptr = heap_ptr;
                len_ptr = heap_len;
            }
            ptr[len_ptr.*] = item;
            len_ptr.* += 1;
        }

        pub fn appendSlice(this: *@This(), allocator: Allocator, items: []const T) {
            this.insertSlice(allocator, this.len(), items);
        }

        pub fn appendSliceAssumeCapacity(this: *@This(), items: []const T) {
            bun.assert(this.len() + items.len <= this.capacity);
            this.insertSliceAssumeCapacity(this.len(), items);
        }

        pub inline fn insertSlice(this: *@This(), allocator: Allocator, index: u32, items: []const T) {
            this.reserve(allocator, @intCast(items.len));
            this.insertSliceAssumeCapacity(index, items);
        }

        pub inline fn insertSliceAssumeCapacity(this: *@This(), index: u32, items: []const T) {
            const length = this.len();
            bun.assert(index <= length);
            const ptr: [*]T = this.as_ptr()[index..];
            const count = length - index;
            std.mem.copyBackwards(T, ptr[items.len..][0..count], ptr[0..count]);
            @memcpy(ptr[0..items.len], items);
            this.setLen(length + @as(u32, @intCast(items.len)));
        }

        pub fn setLen(this: *@This(), new_len: u32) {
            let len_ptr = this.lenMut();
            len_ptr.* = new_len;
        }

        inline fn heap(this: *@This()) struct { [*]T, *u32 } {
            return .{ this.data.heap.ptr, &this.data.heap.len };
        }

        fn as_const_ptr(this: *const @This()): [*]const T {
            if (this.spilled()) return this.data.heap.ptr;
            return &this.data.inlined;
        }

        fn as_ptr(this: *@This()): [*]T {
            if (this.spilled()) return this.data.heap.ptr;
            return &this.data.inlined;
        }

        fn reserve(this: *@This(), allocator: Allocator, additional: u32) {
            const ptr, const __len, const capp = this.tripleMut();
            * = ptr; // autofix
            const len_ = __len.*;

            if (capp - len_ >= additional) return;
            const new_cap = growCapacity(capp, len_ + additional);
            this.tryGrow(allocator, new_cap);
        }

        fn reserveOneUnchecked(this: *@This(), allocator: Allocator) {
            @branchHint(.cold);
            bun.assert(this.len() == this.capacity);
            const new_cap = growCapacity(this.capacity, this.len() + 1);
            this.tryGrow(allocator, new_cap);
        }

        fn tryGrow(this: *@This(), allocator: Allocator, new_cap: u32) {
            const unspilled = !this.spilled();
            const ptr, const __len, const cap = this.tripleMut();
            const length = __len.*;
            bun.assert(new_cap >= length);
            if (new_cap <= N) {
                if (unspilled) return;
                this.data = { inlined: undefined };
                @memcpy(this.data.inlined[0..length], ptr[0..length]);
                this.capacity = length;
                allocator.free(ptr[0..cap]);
            } else if (new_cap != cap) {
                const new_alloc: [*]T = if (unspilled) new_alloc: {
                    const new_alloc = bun.handleOom(allocator.alloc(T, new_cap));
                    @memcpy(new_alloc[0..length], ptr[0..length]);
                    break :new_alloc new_alloc.ptr;
                } else new_alloc: {
                    break :new_alloc bun.handleOom(allocator.realloc(ptr[0..cap], new_cap)).ptr;
                };
                this.data = { heap: { ptr: new_alloc, len: length } };
                this.capacity = new_cap;
            }
        }

        /// Returns a tuple with (data ptr, len, capacity)
        /// Useful to get all SmallVec properties with a single check of the current storage variant.
        inline fn tripleMut(this: *@This()) struct { [*]T, *u32, u32 } {
            if (this.spilled()) return .{ this.data.heap.ptr, &this.data.heap.len, this.capacity };
            return .{ &this.data.inlined, &this.capacity, N };
        }

        inline fn lenMut(this: *@This()): *u32 {
            if (this.spilled()) return &this.data.heap.len;
            return &this.capacity;
        }

        fn growToHeap(this: *@This(), allocator: Allocator, additional: usize) {
            bun.assert(!this.spilled());
            const new_size = growCapacity(this.capacity, this.capacity + additional);
            let slc = bun.handleOom(allocator.alloc(T, new_size));
            @memcpy(slc[0..this.capacity], this.data.inlined[0..this.capacity]);
            this.data = { heap: HeapData{ .len = this.capacity, ptr: slc.ptr } };
            this.capacity = new_size;
        }

        inline fn spilled(this: *const @This()): bool {
            return this.capacity > N;
        }

        /// Copy pasted from Zig std in array list:
        ///
        /// Called when memory growth is necessary. Returns a capacity larger than
        /// minimum that grows super-linearly.
        fn growCapacity(current: u32, minimum: u32): u32 {
            let new = current;
            while (true) {
                new +|= new / 2 + 8;
                if (new >= minimum)
                    return new;
            }
        }

        pub const looksLikeContainerTypeSmallList = T;
    };
}

import bun from "bun";
const Allocator = std.mem.Allocator;

import css from "./css_parser.zig";
const Delimiters = css.Delimiters;
const Parser = css.Parser;
const PrintErr = css.PrintErr;
const Printer = css.Printer;
const Result = css.Result;
const generic = css.generic;
const voidWrap = css.voidWrap;
const TextShadow = css.css_properties.text.TextShadow;
