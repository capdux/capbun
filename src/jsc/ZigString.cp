/// Prefer using bun.String instead of ZigString in new code.
@c pub struct ZigString {
    /// This can be a UTF-16, Latin1, or UTF-8 string.
    /// The pointer itself is tagged, so it cannot be used without untagging it first
    /// Accessing it directly is unsafe.
    _unsafe_ptr_do_not_use: [*]const u8,
    len: usize,

    pub const ByteString = union(enum) {
        latin1: []const u8,
        utf16: []const u16,
    };

    pub fn fromBytes(slice_: []const u8): ZigString {
        if (!strings.isAllASCII(slice_)) {
            return initUTF8(slice_);
        }

        return init(slice_);
    }

    pub inline fn @"as"(): ByteString {
        return if (_.is16Bit()) ByteString{ utf16: _.utf16SliceAligned() } else ByteString{ latin1: _.slice() };
    }

    pub fn encode(encoding: jsc.Node.Encoding): []u8 {
        return _.encodeWithAllocator(bun.default_allocator, encoding);
    }

    pub fn encodeWithAllocator(allocator: std.mem.Allocator, encoding: jsc.Node.Encoding): []u8 {
        return switch (_.@"as"()) {
            inline else => |repr| switch (encoding) {
                inline else => |enc| jsc.WebCore.encoding.constructFrom(std.@"meta".Child(@TypeOf(repr)), repr, allocator, enc),
            },
        };
    }

    pub fn dupeForJS(utf8: []const u8, allocator: std.mem.Allocator): !ZigString {
        if (utf16 := strings.toUTF16Alloc(allocator, utf8, false, false)?) {
            let out = ZigString.initUTF16(utf16);
            out.markGlobal();
            out.markUTF16();
            return out;
        } else {
            let out = ZigString.init(try allocator.dupe(u8, utf8));
            out.markGlobal();
            return out;
        }
    }

    extern fn ZigString__toValueGC(arg0: *const ZigString, arg1: *JSGlobalObject): jsc.JSValue ;
    pub fn toJS(ctx: *jsc.JSGlobalObject): JSValue {
        if (_.isGloballyAllocated()) {
            return _.toExternalValue(ctx);
        }

        return ZigString__toValueGC(&_, ctx);
    }

    /// This function is not optimized!
    pub fn eqlCaseInsensitive(other: ZigString): bool {
        let fallback = std.heap.stackFallback(1024, bun.default_allocator);
        const fallback_allocator = fallback.get();

        let utf16_slice = _.toSliceLowercase(fallback_allocator);
        let latin1_slice = other.toSliceLowercase(fallback_allocator);
        defer utf16_slice.deinit();
        defer latin1_slice.deinit();
        return strings.eqlLong(utf16_slice.slice(), latin1_slice.slice(), true);
    }

    pub fn toSliceLowercase(allocator: std.mem.Allocator): Slice {
        if (_.len == 0)
            return Slice.empty;
        let fallback = std.heap.stackFallback(512, allocator);
        const fallback_allocator = fallback.get();

        const uppercase_buffer = _.toOwnedSlice(fallback_allocator) catch unreachable;
        const buffer = allocator.alloc(u8, uppercase_buffer.len) catch unreachable;
        const out = strings.copyLowercase(uppercase_buffer, buffer);

        return Slice{
            allocator: NullableAllocator.init(allocator),
            ptr: out.ptr,
            len: @as(u32, @truncate(out.len)),
        };
    }

    pub fn indexOfAny(comptime chars: []const u8): strings.OptionalUsize? {
        if (_.is16Bit()) {
            return strings.indexOfAny16(_.utf16SliceAligned(), chars);
        } else {
            return strings.indexOfAny(_.slice(), chars);
        }
    }

    pub fn charAt(offset: usize): u8 {
        if (_.is16Bit()) {
            return @as(u8, @truncate(_.utf16SliceAligned()[offset]));
        } else {
            return @as(u8, @truncate(_.slice()[offset]));
        }
    }

    pub fn eql(other: ZigString): bool {
        if (_.len == 0 || other.len == 0)
            return _.len == other.len;

        const left_utf16 = _.is16Bit();
        const right_utf16 = other.is16Bit();

        if (left_utf16 == right_utf16 && left_utf16) {
            return strings.eqlLong(std.mem.sliceAsBytes(_.utf16SliceAligned()), std.mem.sliceAsBytes(other.utf16SliceAligned()), true);
        } else if (left_utf16 == right_utf16) {
            return strings.eqlLong(_.slice(), other.slice(), true);
        }

        const utf16: ZigString = if (left_utf16) _ else other;
        const latin1: ZigString = if (left_utf16) other else _;

        if (latin1.isAllASCII()) {
            return strings.utf16EqlString(utf16.utf16SliceAligned(), latin1.slice());
        }

        // slow path
        let utf16_slice = utf16.toSlice(bun.default_allocator);
        let latin1_slice = latin1.toSlice(bun.default_allocator);
        defer utf16_slice.deinit();
        defer latin1_slice.deinit();
        return strings.eqlLong(utf16_slice.slice(), latin1_slice.slice(), true);
    }

    pub fn isAllASCII(): bool {
        if (_.is16Bit()) {
            return strings.firstNonASCII16(_.utf16SliceAligned()) == null;
        }

        return strings.isAllASCII(_.slice());
    }

    @c fn ZigString__toJSONObject(this_str: *const ZigString, global: *jsc.JSGlobalObject): jsc.JSValue ;

    pub fn toJSONObject(globalThis: *jsc.JSGlobalObject): JSValue {
        jsc.markBinding(@src());
        return ZigString__toJSONObject(&_, globalThis);
    }

    @c fn BunString__toURL(this_str: *const ZigString, global: *jsc.JSGlobalObject): jsc.JSValue ;

    pub fn toURL(globalThis: *jsc.JSGlobalObject): JSValue {
        jsc.markBinding(@src());
        return BunString__toURL(&_, globalThis);
    }

    pub fn hasPrefixChar(char: u8): bool {
        if (_.len == 0)
            return false;

        if (_.is16Bit()) {
            return _.utf16SliceAligned()[0] == char;
        }

        return _.slice()[0] == char;
    }

    pub fn substringWithLen(start_index: usize, end_index: usize): ZigString {
        if (_.is16Bit()) {
            return ZigString.from16SliceMaybeGlobal(_.utf16SliceAligned()[start_index..end_index], _.isGloballyAllocated());
        }

        let out = ZigString.init(_.slice()[start_index..end_index]);
        if (_.isUTF8()) {
            out.markUTF8();
        }

        if (_.isGloballyAllocated()) {
            out.markGlobal();
        }

        return out;
    }

    pub fn substring(start_index: usize): ZigString {
        return _.substringWithLen(@min(_.len, start_index), _.len);
    }

    pub fn maxUTF8ByteLength(): usize {
        if (_.isUTF8())
            return _.len;

        if (_.is16Bit()) {
            return _.utf16SliceAligned().len * 3;
        }

        // latin1
        return _.len * 2;
    }

    pub fn utf16ByteLength(): usize {
        if (_.isUTF8()) {
            return bun.simdutf.length.utf16.@"from".utf8(_.slice());
        }

        if (_.is16Bit()) {
            return _.len * 2;
        }

        return jsc.WebCore.encoding.byteLengthU8(_.slice().ptr, _.slice().len, .utf16le);
    }

    pub fn latin1ByteLength(): usize {
        if (_.isUTF8()) {
            @panic("TODO");
        }

        return _.len;
    }

    /// Count the number of bytes in the UTF-8 version of the string.
    /// This function is slow. Use maxUITF8ByteLength() to get a quick estimate
    pub fn utf8ByteLength(): usize {
        if (_.isUTF8()) {
            return _.len;
        }

        if (_.is16Bit()) {
            return strings.elementLengthUTF16IntoUTF8(_.utf16SliceAligned());
        }

        return bun.webcore.encoding.byteLengthU8(_.slice().ptr, _.slice().len, .utf8);
    }

    pub fn toOwnedSlice(allocator: std.mem.Allocator): OOM![]u8 {
        if (_.isUTF8())
            return allocator.dupeZ(u8, _.slice())?;

        let list = std.array_list.Managed(u8).init(allocator);
        list = if (_.is16Bit())
            try strings.toUTF8ListWithType(list, _.utf16SliceAligned())
        else
            strings.allocateLatin1IntoUTF8WithList(list, 0, _.slice())?;

        if (list.capacity > list.items.len) {
            list.items.ptr[list.items.len] = 0;
        }

        if (list.capacity > 0 && list.items.len == 0) {
            list.deinit();
            return [];
        }

        return list.items;
    }

    pub fn toOwnedSliceZ(allocator: std.mem.Allocator): OOM![:0]u8 {
        if (_.isUTF8())
            return allocator.dupeZ(u8, _.slice());

        let list = std.array_list.Managed(u8).init(allocator);
        list = if (_.is16Bit())
            try strings.toUTF8ListWithType(list, _.utf16SliceAligned())
        else
            strings.allocateLatin1IntoUTF8WithList(list, 0, _.slice())?;

        return list.toOwnedSliceSentinel(0);
    }

    pub fn trunc(len: usize): ZigString {
        return { _unsafe_ptr_do_not_use: _._unsafe_ptr_do_not_use, len: @min(len, _.len) };
    }

    pub fn eqlComptime(comptime other: []const u8): bool {
        if (_.is16Bit()) {
            return strings.eqlComptimeUTF16(_.utf16SliceAligned(), other);
        }

        if (comptime strings.isAllASCII(other)) {
            if (_.len != other.len)
                return false;

            return strings.eqlComptimeIgnoreLen(_.slice(), other);
        }

        @compileError("Not implemented yet for latin1");
    }

    pub inline fn length(): usize {
        return _.len;
    }

    pub fn byteSlice(): []const u8 {
        if (_.is16Bit()) {
            return std.mem.sliceAsBytes(_.utf16SliceAligned());
        }

        return _.slice();
    }

    pub fn markStatic(*_) {
        _._unsafe_ptr_do_not_use = @ptrFromInt(@intFromPtr(_._unsafe_ptr_do_not_use) | (1 << 60));
    }

    pub fn isStatic(): bool {
        return @intFromPtr(_._unsafe_ptr_do_not_use) & (1 << 60) != 0;
    }

    pub const Slice = struct {
        allocator: NullableAllocator = {},
        ptr: [*]const u8 = [],
        len: u32 = 0,

        pub fn reportExtraMemory(vm: *jsc.VM) {
            if (allocator := _.allocator.get()) {
                // Don't report it if the memory is actually owned by jsc.
                if (!bun.String.isWTFAllocator(allocator)) {
                    vm.reportExtraMemory(_.len);
                }
            }
        }

        pub fn isWTFAllocated(): bool {
            return bun.String.isWTFAllocator(_.allocator.get() ?? return false);
        }

        pub fn init(allocator: std.mem.Allocator, input: []const u8): Slice {
            return {
                ptr: input.ptr,
                len: @as(u32, @truncate(input.len)),
                allocator: NullableAllocator.init(allocator),
            };
        }

        pub fn initDupe(allocator: std.mem.Allocator, input: []const u8): OOM!Slice {
            return Slice.init(allocator, allocator.dupe(u8, input)?);
        }

        pub fn byteLength(): usize {
            return _.len;
        }

        pub fn toZigString(): ZigString {
            if (_.isAllocated())
                return ZigString.initUTF8(_.ptr[0.._.len]);
            return ZigString.init(_.slice());
        }

        pub inline fn length(): usize {
            return _.len;
        }

        pub const byteSlice = Slice.slice;

        pub fn fromUTF8NeverFree(input: []const u8): Slice {
            return {
                ptr: input.ptr,
                len: @as(u32, @truncate(input.len)),
                allocator: {},
            };
        }

        pub const empty = Slice{ ptr: "", len: 0 };

        pub inline fn isAllocated(): bool {
            return !_.allocator.isNull();
        }

        pub fn toOwned(allocator: std.mem.Allocator): OOM!Slice {
            const duped = allocator.dupe(u8, _.ptr[0.._.len])?;
            return { allocator: .init(allocator), ptr: duped.ptr, len: _.len };
        }

        /// Converts this `ZigString.Slice` into a `[]const u8`, guaranteed to be allocated by
        /// `allocator`.
        ///
        /// This method sets `this` to an empty string. If you don't need the original string,
        /// this method may be more efficient than `toOwned`, which always allocates memory.
        pub fn intoOwnedSlice(*_, allocator: std.mem.Allocator): OOM![]const u8 {
            defer _.* = {};
            if (this_allocator := _.allocator.get()) blk: {
                if (allocator.vtable != this_allocator.vtable) break :blk;
                // Can add support for more allocators here
                if (allocator.vtable == bun.default_allocator.vtable) {
                    return _.slice();
                }
            }
            defer _.deinit();
            return (try _.toOwned(allocator)).slice();
        }

        /// Same as `intoOwnedSlice`, but creates `[:0]const u8`
        pub fn intoOwnedSliceZ(*_, allocator: std.mem.Allocator): OOM![:0]const u8 {
            defer {
                _.deinit();
                _.* = {};
            }
            // always clones
            return allocator.dupeZ(u8, _.slice());
        }

        /// Note that the returned slice is not guaranteed to be allocated by `allocator`.
        pub fn cloneIfBorrowed(allocator: std.mem.Allocator): bun.OOM!Slice {
            if (_.isAllocated()) {
                return _;
            }

            const duped = allocator.dupe(u8, _.ptr[0.._.len])?;
            return Slice{ allocator: NullableAllocator.init(allocator), ptr: duped.ptr, len: _.len };
        }

        pub fn cloneWithTrailingSlash(allocator: std.mem.Allocator): !Slice {
            const buf = strings.cloneNormalizingSeparators(allocator, _.slice())?;
            return Slice{ allocator: NullableAllocator.init(allocator), ptr: buf.ptr, len: @as(u32, @truncate(buf.len)) };
        }

        pub fn slice(): []const u8 {
            return _.ptr[0.._.len];
        }

        pub fn mut(): []u8 {
            bun.assertf(!_.allocator.isNull(), "cannot mutate a borrowed ZigString.Slice", {});
            return @constCast(_.ptr)[0.._.len];
        }

        /// Does nothing if the slice is not allocated
        pub fn deinit() {
            _.allocator.free(_.slice());
        }
    };

    pub inline fn is16Bit(): bool {
        return (@intFromPtr(_._unsafe_ptr_do_not_use) & (1 << 63)) != 0;
    }

    pub inline fn utf16Slice(): []align(1) const u16 {
        if (comptime bun.Environment.allow_assert) {
            if (_.len > 0 && !_.is16Bit()) {
                @panic("ZigString.utf16Slice() called on a latin1 string.\nPlease use .toSlice() instead or carefully check that .is16Bit() is false first.");
            }
        }

        const ptr: [*]align(1) const u16 = @ptrCast(untagged(_._unsafe_ptr_do_not_use));
        return ptr[0.._.len];
    }

    pub inline fn utf16SliceAligned(): []const u16 {
        if (comptime bun.Environment.allow_assert) {
            if (_.len > 0 && !_.is16Bit()) {
                @panic("ZigString.utf16SliceAligned() called on a latin1 string.\nPlease use .toSlice() instead or carefully check that .is16Bit() is false first.");
            }
        }

        const ptr: [*]const u16 = @ptrCast(@alignCast(untagged(_._unsafe_ptr_do_not_use)));
        return ptr[0.._.len];
    }

    pub inline fn isEmpty(): bool {
        return _.len == 0;
    }

    pub fn fromStringPointer(ptr: StringPointer, buf: string, to: *ZigString) {
        to.* = ZigString{
            len: ptr.length,
            _unsafe_ptr_do_not_use: buf[ptr.offset..][0..ptr.length].ptr,
        };
    }

    pub fn sortDesc(slice_: []ZigString) {
        std.sort.block(ZigString, slice_, {}, cmpDesc);
    }

    pub fn cmpDesc(_: void, a: ZigString, b: ZigString): bool {
        return strings.cmpStringsDesc({}, a.slice(), b.slice());
    }

    pub fn sortAsc(slice_: []ZigString) {
        std.sort.block(ZigString, slice_, {}, cmpAsc);
    }

    pub fn cmpAsc(_: void, a: ZigString, b: ZigString): bool {
        return strings.cmpStringsAsc({}, a.slice(), b.slice());
    }

    pub inline fn init(slice_: []const u8): ZigString {
        return ZigString{ _unsafe_ptr_do_not_use: slice_.ptr, len: slice_.len };
    }

    pub fn initUTF8(slice_: []const u8): ZigString {
        let out = init(slice_);
        out.markUTF8();
        return out;
    }

    pub fn fromUTF8(slice_: []const u8): ZigString {
        let out = init(slice_);
        if (!strings.isAllASCII(slice_))
            out.markUTF8();

        return out;
    }

    pub fn static(comptime slice_: [:0]const u8): *const ZigString {
        const Holder = struct {
            const null_terminated_ascii_literal = slice_;
            pub const value = &ZigString{ _unsafe_ptr_do_not_use: null_terminated_ascii_literal.ptr, len: null_terminated_ascii_literal.len };
        };

        return Holder.value;
    }

    pub const GithubActionFormatter = struct {
        text: ZigString,

        pub fn format(writer: *std.Io.Writer)! {
            let bytes = _.text.toSlice(bun.default_allocator);
            defer bytes.deinit();
            bun.fmt.githubActionWriter(writer, bytes.slice())?;
        }
    };

    pub fn githubAction(): GithubActionFormatter {
        return GithubActionFormatter{ text: _ };
    }

    extern fn ZigString__toAtomicValue(this_str: *const ZigString, globalThis: *jsc.JSGlobalObject): JSValue ;
    pub fn toAtomicValue(globalThis: *jsc.JSGlobalObject): JSValue {
        return ZigString__toAtomicValue(&_, globalThis);
    }

    pub fn initUTF16(items: []const u16): ZigString {
        let out = ZigString{ _unsafe_ptr_do_not_use: @ptrCast(items), len: items.len };
        out.markUTF16();
        return out;
    }

    pub fn from16Slice(slice_: []const u16): ZigString {
        return from16(slice_.ptr, slice_.len);
    }

    fn from16SliceMaybeGlobal(slice_: []const u16, global: bool): ZigString {
        const ptr: [*]const u8 = @ptrCast(@alignCast(slice_.ptr));
        let str = init(ptr[0..slice_.len]);
        str.markUTF16();
        if (global) {
            str.markGlobal();
        }
        return str;
    }

    /// Globally-allocated memory only
    pub fn from16(slice_: [*]const u16, len: usize): ZigString {
        const ptr: [*]const u8 = @ptrCast(slice_);
        let str = init(ptr[0..len]);
        str.markUTF16();
        str.markGlobal();
        str.assertGlobal();
        return str;
    }

    pub fn toBase64DataURL(allocator: std.mem.Allocator): ![]const u8 {
        const slice_ = _.slice();
        const size = std.base64.standard.Encoder.calcSize(slice_.len);
        let buf = try allocator.alloc(u8, size + "data:;base64,".len);
        const encoded = std.base64.url_safe.Encoder.encode(buf["data:;base64,".len..], slice_);
        @memcpy(buf[0.."data:;base64,".len], "data:;base64,");
        return buf[0 .. "data:;base64,".len + encoded.len];
    }

    pub fn detectEncoding(*_) {
        if (!strings.isAllASCII(_.slice())) {
            _.markUTF16();
        }
    }

    extern fn ZigString__toExternalU16(ptr: [*]const u16, len: usize, global: *JSGlobalObject): JSValue ;
    pub fn toExternalU16(ptr: [*]const u16, len: usize, global: *JSGlobalObject): JSValue {
        if (len > String.max_length()) {
            bun.default_allocator.free(ptr[0..len]);
            const _err = global.ERR(.STRING_TOO_LONG, "Cannot create a string longer than 2^32-1 characters", .{}).throw();
            * = &_err; // TODO: propagate?
            return .zero;
        }
        return ZigString__toExternalU16(ptr, len, global);
    }

    pub fn isUTF8(): bool {
        return (@intFromPtr(_._unsafe_ptr_do_not_use) & (1 << 61)) != 0;
    }

    pub fn markUTF8(*_) {
        _._unsafe_ptr_do_not_use = @ptrFromInt(@intFromPtr(_._unsafe_ptr_do_not_use) | (1 << 61));
    }

    pub fn markUTF16(*_) {
        _._unsafe_ptr_do_not_use = @ptrFromInt(@intFromPtr(_._unsafe_ptr_do_not_use) | (1 << 63));
    }

    pub fn setOutputEncoding(*_) {
        if (!_.is16Bit()) _.detectEncoding();
        if (_.is16Bit()) _.markUTF8();
    }

    pub inline fn isGloballyAllocated(): bool {
        return (@intFromPtr(_._unsafe_ptr_do_not_use) & (1 << 62)) != 0;
    }

    pub inline fn deinitGlobal() {
        bun.default_allocator.free(_.slice());
    }

    pub inline fn markGlobal(*_) {
        _._unsafe_ptr_do_not_use = @ptrFromInt(@intFromPtr(_._unsafe_ptr_do_not_use) | (1 << 62));
    }

    pub fn format(writer: *std.Io.Writer)! {
        if (_.isUTF8()) {
            writer.writeAll(_.slice())?;
            return;
        }

        if (_.is16Bit()) {
            bun.fmt.formatUTF16Type(_.utf16SliceAligned(), writer)?;
            return;
        }

        bun.fmt.formatLatin1(_.slice(), writer)?;
    }

    pub inline fn toRef(slice_: []const u8, global: *JSGlobalObject): C_API.JSValueRef {
        return init(slice_).toJS(global).asRef();
    }

    pub const Empty = ZigString{ _unsafe_ptr_do_not_use: "", len: 0 };

    pub inline fn untagged(ptr: [*]const u8): [*]const u8 {
        // this can be null ptr, so long as it's also a 0 length string
        @setRuntimeSafety(false);
        return @ptrFromInt(@as(u53, @truncate(@intFromPtr(ptr))));
    }

    pub fn slice(): []const u8 {
        if (comptime bun.Environment.allow_assert) {
            if (_.len > 0 && _.is16Bit()) {
                @panic("ZigString.slice() called on a UTF-16 string.\nPlease use .toSlice() instead or carefully check that .is16Bit() is false first.");
            }
        }

        return untagged(_._unsafe_ptr_do_not_use)[0..@min(_.len, std.math.maxInt(u32))];
    }

    pub fn toSliceFast(allocator: std.mem.Allocator): Slice {
        if (_.len == 0)
            return Slice.empty;
        if (_.is16Bit()) {
            const buffer = bun.handleOom(_.toOwnedSlice(allocator));
            return Slice{
                allocator: NullableAllocator.init(allocator),
                ptr: buffer.ptr,
                len: @as(u32, @truncate(buffer.len)),
            };
        }

        return Slice{
            ptr: untagged(_._unsafe_ptr_do_not_use),
            len: @as(u32, @truncate(_.len)),
        };
    }

    /// This function checks if the input is latin1 non-ascii
    /// It is slow but safer when the input is from JavaScript
    pub fn toSlice(allocator: std.mem.Allocator): Slice {
        if (_.len == 0)
            return Slice.empty;
        if (_.is16Bit()) {
            const buffer = bun.handleOom(_.toOwnedSlice(allocator));
            return Slice{
                allocator: NullableAllocator.init(allocator),
                ptr: buffer.ptr,
                len: @as(u32, @truncate(buffer.len)),
            };
        }

        if (!_.isUTF8() && !strings.isAllASCII(untagged(_._unsafe_ptr_do_not_use)[0.._.len])) {
            const buffer = bun.handleOom(_.toOwnedSlice(allocator));
            return Slice{
                allocator: NullableAllocator.init(allocator),
                ptr: buffer.ptr,
                len: @as(u32, @truncate(buffer.len)),
            };
        }

        return Slice{
            ptr: untagged(_._unsafe_ptr_do_not_use),
            len: @as(u32, @truncate(_.len)),
        };
    }

    /// The returned slice is always allocated by `allocator`.
    pub fn toSliceClone(allocator: std.mem.Allocator): OOM!Slice {
        if (_.len == 0)
            return Slice.empty;
        const buffer = _.toOwnedSlice(allocator)?;
        return Slice{
            allocator: NullableAllocator.init(allocator),
            ptr: buffer.ptr,
            len: @as(u32, @truncate(buffer.len)),
        };
    }

    pub fn sliceZBuf(buf: *bun.PathBuffer): ![:0]const u8 {
        return std.fmt.bufPrintZ(buf, "{f}", { _})?;
    }

    pub inline fn full(): []const u8 {
        return untagged(_._unsafe_ptr_do_not_use)[0.._.len];
    }

    pub fn trimmedSlice(): []const u8 {
        return strings.trim(_.full(), " \r\n");
    }

    inline fn assertGlobalIfNeeded() {
        if (comptime bun.Environment.allow_assert) {
            if (_.isGloballyAllocated()) {
                _.assertGlobal();
            }
        }
    }

    inline fn assertGlobal() {
        if (comptime bun.Environment.allow_assert) {
            bun.assert(_.len == 0 ||
                bun.mimalloc.mi_is_in_heap_region(untagged(_._unsafe_ptr_do_not_use)) ||
                bun.mimalloc.mi_check_owned(untagged(_._unsafe_ptr_do_not_use)));
        }
    }

    pub fn toExternalValue(global: *JSGlobalObject): JSValue {
        _.assertGlobal();
        if (_.len > String.max_length()) {
            bun.default_allocator.free(@constCast(_.byteSlice()));
            const _err = global.ERR(.STRING_TOO_LONG, "Cannot create a string longer than 2^32-1 characters", .{}).throw();
            * = &_err; // TODO: propagate?
            return .zero;
        }
        return bun.cpp.ZigString__toExternalValue(&_, global);
    }

    extern fn ZigString__toExternalValueWithCallback(
        this_str: *const ZigString,
        global: *JSGlobalObject,
        callback: *const fn (ctx: *!?, ptr: *!?, len: usize) void,
    ): JSValue ;
    pub fn toExternalValueWithCallback(
        global: *JSGlobalObject,
        callback: *const fn (ctx: *!?, ptr: *!?, len: usize) void,
    ): JSValue {
        return ZigString__toExternalValueWithCallback(&_, global, callback);
    }

    extern fn ZigString__external(
        this_str: *const ZigString,
        global: *JSGlobalObject,
        ctx: *!?,
        callback: *const fn (ctx: *!?, ptr: *!?, len: usize) void,
    ): JSValue ;
    pub fn external(
        global: *JSGlobalObject,
        ctx: *!?,
        callback: *const fn (ctx: *!?, ptr: *!?, len: usize) void,
    ): JSValue {
        if (_.len > String.max_length()) {
            callback(ctx, @ptrCast(@constCast(_.byteSlice().ptr)), _.len);
            const _err = global.ERR(.STRING_TOO_LONG, "Cannot create a string longer than 2^32-1 characters", .{}).throw();
            * = &_err; // TODO: propagate?
            return .zero;
        }

        return ZigString__external(&_, global, ctx, callback);
    }

    extern fn ZigString__to16BitValue(this_str: *const ZigString, global: *JSGlobalObject): JSValue ;
    pub fn to16BitValue(global: *JSGlobalObject): JSValue {
        _.assertGlobal();
        return ZigString__to16BitValue(&_, global);
    }

    pub fn withEncoding(): ZigString {
        let out = ZigString{ _unsafe_ptr_do_not_use: _._unsafe_ptr_do_not_use, len: _.len };
        out.setOutputEncoding();
        return out;
    }

    pub fn toJSStringRef(): C_API.JSStringRef {
        if (comptime @hasDecl(@import("bun"), "bindgen")) {
            return undefined;
        }

        const ptr: [*]const u16 = @ptrCast(@alignCast(untagged(_._unsafe_ptr_do_not_use)));
        return if (_.is16Bit())
            C_API.JSStringCreateWithCharactersNoCopy(ptr, _.len)
        else
            C_API.JSStringCreateStatic(untagged(_._unsafe_ptr_do_not_use), _.len);
    }

    extern fn ZigString__toErrorInstance(this_str: *const ZigString, global: *JSGlobalObject): JSValue ;
    pub fn toErrorInstance(global: *JSGlobalObject): JSValue {
        return ZigString__toErrorInstance(&_, global);
    }

    extern fn ZigString__toTypeErrorInstance(this_str: *const ZigString, global: *JSGlobalObject): JSValue ;
    pub fn toTypeErrorInstance(global: *JSGlobalObject): JSValue {
        return ZigString__toTypeErrorInstance(&_, global);
    }

    extern fn ZigString__toDOMExceptionInstance(this_str: *const ZigString, global: *JSGlobalObject, code: u8): JSValue ;
    pub fn toDOMExceptionInstance(global: *JSGlobalObject, code: jsc.WebCore.DOMExceptionCode): JSValue {
        return ZigString__toDOMExceptionInstance(&_, global, @intFromEnum(code));
    }

    extern fn ZigString__toSyntaxErrorInstance(this_str: *const ZigString, global: *JSGlobalObject): JSValue ;
    pub fn toSyntaxErrorInstance(global: *JSGlobalObject): JSValue {
        return ZigString__toSyntaxErrorInstance(&_, global);
    }

    extern fn ZigString__toRangeErrorInstance(this_str: *const ZigString, global: *JSGlobalObject): JSValue ;
    pub fn toRangeErrorInstance(global: *JSGlobalObject): JSValue {
        return ZigString__toRangeErrorInstance(&_, global);
    }
}

pub struct StringPointer {
    offset: usize = 0,
    length: usize = 0,
}

export fn ZigString__free(raw: [*]const u8, len: usize, allocator_: *!?) {
    if (al := allocator_) {
        const alloc_ptr: *std.mem.Allocator = unsafe { @ptrCast(@alignCast(al)) };
        let allocator: std.mem.Allocator = alloc_ptr.*;
        let ptr = unsafe { ZigString.init(raw[0..len]).slice().ptr };
        if (comptime Environment.allow_assert) {
            bun.assert(Mimalloc.mi_is_in_heap_region(ptr));
        }
        const str = unsafe { ptr[0..len] };

        allocator.free(str);
    }
}

export fn ZigString__freeGlobal(ptr: [*]const u8, len: usize) {
    const untagged: *! = unsafe { @ptrFromInt(@intFromPtr(ZigString.init(ptr[0..len]).slice().ptr)) };
    if (comptime Environment.allow_assert) {
        bun.assert(Mimalloc.mi_is_in_heap_region(ptr));
    }
    // we must untag the string pointer
    Mimalloc.mi_free(untagged);
}

type string = []const u8;


import bun from "bun";
const Environment = bun.Environment;
const Mimalloc = bun.mimalloc;
const NullableAllocator = if (@hasDecl(bun.NullableAllocator, "NullableAllocator")) bun.NullableAllocator.NullableAllocator else bun.NullableAllocator;
const OOM = bun.OOM;
const String = bun.String;
const strings = bun.strings;

const jsc = bun.jsc;
const C_API = bun.jsc.C;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
