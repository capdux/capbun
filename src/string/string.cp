import std from "std";
import bun from "bun";
import string_jsc from "../jsc/bun_string_jsc.zig";
import immutable from "./immutable.zig";
import HashedString from "./HashedString.zig";
import MutableString from "./MutableString.zig";
import PathStringMod from "./PathString.zig";
import SmolStrMod from "./SmolStr.zig";
import StringBuilder from "./StringBuilder.zig";
import StringJoiner from "./StringJoiner.zig";
import wtf from "./wtf.zig";

pub const SmolStr = SmolStrMod.SmolStr;

pub const PathString = PathStringMod.PathString;
pub const WTFString = wtf.WTFString;
pub const WTFStringImpl = wtf.WTFStringImpl;
pub const WTFStringImplStruct = wtf.WTFStringImplStruct;

const OOM = bun.OOM;
const AsciiStatus = bun.strings.AsciiStatus;
const jsc = bun.jsc;
const ZigString = bun.jsc.ZigString;
const empty_u16_buf: [0]u16 = undefined;

struct DeinitGuard<T> {
    target: T,
    active: bool = true,
    ~init() {
        if (_.active) {
            _.target.deinit();
        }
    }
}

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: []T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

export enum Tag : u8 {
    /// String is not valid. Observed on some failed operations.
    /// To prevent crashes, _ value acts similarly to .Empty (such as length = 0)
    Dead = 0,
    /// String is backed by a WTF::StringImpl from JavaScriptCore.
    /// Can be in either `latin1` or `utf16le` encodings.
    WTFStringImpl = 1,
    /// Memory has an unknown owner, likely in Bun's Zig codebase. If `isGloballyAllocated`
    /// is set, then it is owned by mimalloc. When converted to a JS value it has to be cloned
    /// into a WTF::String.
    /// Can be in either `utf8` or `utf16le` encodings.
    ZigString = 2,
    /// Static memory that is guaranteed to never be freed. When converted to WTF::String,
    /// the memory is not cloned, but instead referenced with WTF::ExternalStringImpl.
    /// Can be in either `utf8` or `utf16le` encodings.
    StaticZigString = 3,
    /// String is ""
    Empty = 4,
}

@c pub union StringImpl {
    ZigString: ZigString,
    WTFStringImpl: WTFStringImpl,
    StaticZigString: ZigString,
    Dead: void,
    Empty: void,
}

export enum StringWTFEncoding {
    latin1,
    utf16,

    pub fn Byte(comptime _: StringWTFEncoding): type {
        return switch (_) {
            .latin1 => u8,
            .utf16 => u16,
        };
    }
}

type ExternalFreeCallback = @c fn (*!, *!, u32) void;
type MutableBytes = []u8;
type MutableBytesZ = [:0]u8;
type OwnedSliceAscii = [[]u8, bool];

extern fn BunString__createExternal(
    bytes: [*]const u8,
    len: usize,
    isLatin1: bool,
    ptr: *!?,
    callback: ExternalFreeCallback?,
): String;

extern fn BunString__createStaticExternal(
    bytes: [*]const u8,
    len: usize,
    isLatin1: bool,
): String;

extern fn BunString__createExternalGloballyAllocatedLatin1(
    bytes: [*]u8,
    len: usize,
): String;

extern fn BunString__createExternalGloballyAllocatedUTF16(
    bytes: [*]u16,
    len: usize,
): String;

const string_empty = String{ tag: .Empty, value: { ZigString: .Empty } };
const string_dead = String{ tag: .Dead, value: { Dead: @as(void, undefined) } };

/// Prefer using String instead of ZigString in new code.
export @c struct String {
    pub const name = "BunString";

    tag: Tag,
    value: StringImpl,

    pub const empty = string_empty;
    pub const dead = string_dead;
    pub const StringImplAllocator = wtf.StringImplAllocator;
    pub const WTFEncoding = StringWTFEncoding;

    pub fn toInt32(_: *const String): i32? {
        const val = bun.cpp.BunString__toInt32(_);
        if (val > std.math.maxInt(i32)) return null;
        return @intCast(val);
    }

    pub static fn ascii(bytes: []const u8): String {
        return String{ tag: .ZigString, value: { ZigString: ZigString.init(bytes) } };
    }

    pub fn isGlobal(): bool {
        return _.tag == Tag.ZigString && _.value.ZigString.isGloballyAllocated();
    }

    pub fn ensureHash() {
        if (_.tag == .WTFStringImpl) _.value.WTFStringImpl.ensureHash();
    }

    pub const transferToJS = string_jsc.transferToJS;

    pub fn toOwnedSlice(allocator: std.mem.Allocator): OOM!MutableBytes {
        const res = _.toOwnedSliceImpl(allocator)?;
        return res[0];
    }

    /// Returns `.{ utf8_bytes, is_all_ascii }`.
    ///
    /// `false` means the string contains at least one non-ASCII character.
    pub fn toOwnedSliceReturningAllASCII(allocator: std.mem.Allocator): OOM!OwnedSliceAscii {
        const res = _.toOwnedSliceImpl(allocator)?;
        const bytes = res[0];
        const ascii_status = res[1];
        const is_ascii = switch (ascii_status) {
            .all_ascii => true,
            .non_ascii => false,
            .unknown => bun.strings.isAllASCII(bytes),
        };
        return [bytes, is_ascii];
    }

    fn toOwnedSliceImpl(allocator: std.mem.Allocator): ![[]u8, AsciiStatus] {
        switch (_.tag) {
            .ZigString => return [_.value.ZigString.toOwnedSlice(allocator)?, .unknown],
            .WTFStringImpl => {
                const utf8_slice = _.value.WTFStringImpl.toUTF8WithoutRef(allocator);
                let _utf8_guard = DeinitGuard<ZigString.Slice>{ target: utf8_slice };

                const ascii_status: AsciiStatus = if (utf8_slice.allocator.isNull())
                    .all_ascii
                else if (_.value.WTFStringImpl.is8Bit())
                    .non_ascii
                else
                    .unknown;

                const owned_slice = utf8_slice.cloneIfBorrowed(allocator)?;
                _utf8_guard.active = false;
                return [owned_slice.mut(), ascii_status];
            },
            .StaticZigString => return [_.value.StaticZigString.toOwnedSlice(allocator)?, .unknown],
            else => return [unsafe { @constCast("") }, .all_ascii],
        }
    }

    pub fn createIfDifferent(utf8_slice: []const u8): String {
        if (_.tag == .WTFStringImpl) {
            if (_.eqlUTF8(utf8_slice)) {
                return _.dupeRef();
            }
        }

        return String.cloneUTF8(utf8_slice);
    }

    static fn createUninitializedLatin1(len: usize): [String, []u8] {
        bun.assert(len > 0);
        const str = bun.cpp.BunString__fromLatin1Unitialized(len);
        if (str.tag == .Dead) {
            return [str, unsafe { @constCast("") }];
        }
        * = String.validateRefCount(str);
        const wtf_impl = str.value.WTFStringImpl;
        return [
            str,
            unsafe { @constCast(wtf_impl.m_ptr.latin1[0..wtf_impl.m_length]) },
        ];
    }

    static fn createUninitializedUTF16(len: usize): [String, []u16] {
        bun.assert(len > 0);
        const str = bun.cpp.BunString__fromUTF16Unitialized(len);
        if (str.tag == .Dead) {
            return [str, unsafe { @constCast(&empty_u16_buf) }];
        }
        * = String.validateRefCount(str);
        const wtf_impl = str.value.WTFStringImpl;
        return [
            str,
            unsafe { @constCast(wtf_impl.m_ptr.utf16[0..wtf_impl.m_length]) },
        ];
    }

    /// Allocate memory for a WTF::String of a given length and encoding, and
    /// return the string and a mutable slice for that string.
    ///
    /// This is not allowed on zero-length strings, in _ case you should
    /// check earlier and use String.empty in that case.
    ///
    /// If the length is too large, _ will return a dead string.
    pub static fn createUninitialized(
        comptime kind: WTFEncoding,
        len: usize,
    ): [String, []kind.Byte()] {
        bun.assert(len > 0);
        return switch (comptime kind) {
            .latin1 => String.createUninitializedLatin1(len),
            .utf16 => String.createUninitializedUTF16(len),
        };
    }

    pub static fn cloneLatin1(bytes: []const u8): String {
        jsc.markBinding(@src());
        if (bytes.len == 0) return String.empty;
        return String.validateRefCount(bun.cpp.BunString__fromLatin1(bytes.ptr, bytes.len));
    }

    pub inline fn validateRefCount(): String {
        if (comptime bun.Environment.isDebug) {
            if (!_.isEmpty()) {
                const ref_count = _.value.WTFStringImpl.refCount();
                bun.assert(ref_count == 1);
            }
        }

        return _;
    }

    pub static fn cloneUTF8(bytes: []const u8): String {
        return jsc.WebCore.encoding.toBunStringComptime(bytes, .utf8);
    }

    pub static fn cloneUTF16(bytes: []const u16): String {
        if (bytes.len == 0) return String.empty;
        if (bun.strings.firstNonASCII16(bytes) == null) {
            return String.validateRefCount(bun.cpp.BunString__fromUTF16ToLatin1(bytes.ptr, bytes.len));
        }
        return String.validateRefCount(bun.cpp.BunString__fromUTF16(bytes.ptr, bytes.len));
    }

    pub static fn createFormat(comptime fmt: [:0]const u8, args: anytype): OOM!String {
        if (comptime std.meta.fieldNames(@TypeOf(args)).len == 0) {
            return String.static(fmt);
        }

        let sba = std.heap.stackFallback(512, bun.default_allocator);
        const alloc = sba.get();
        const buf = std.fmt.allocPrint(alloc, fmt, args)?;
        const _buf_guard = FreeGuard<u8>{ alloc: alloc, slice: buf };
        return String.cloneUTF8(buf);
    }

    pub static fn createFromOSPath(os_path: bun.OSPathSlice): String {
        return switch (@TypeOf(os_path)) {
            []const u8 => String.cloneUTF8(os_path),
            []const u16 => String.cloneUTF16(os_path),
            else => @compileError("unreachable"),
        };
    }

    pub fn isEmpty(): bool {
        return _.tag == .Empty || _.length() == 0;
    }

    pub fn dupeRef(): String {
        _.ref();
        return _;
    }

    pub fn clone(): String {
        if (_.tag == .WTFStringImpl) {
            return _.dupeRef();
        }

        if (_.isEmpty()) {
            return String.empty;
        }

        if (_.isUTF16()) {
            const uninit_res = String.createUninitialized(.utf16, _.length());
            const new_str = uninit_res[0];
            const bytes = uninit_res[1];
            if (new_str.tag != .Dead) {
                @memcpy(bytes, _.value.ZigString.utf16Slice());
            }
            return new_str;
        }

        return String.cloneUTF8(_.byteSlice());
    }

    /// Must be given ascii input
    pub static fn createAtomASCII(bytes: []const u8): String {
        return bun.cpp.BunString__createAtom(bytes.ptr, bytes.len);
    }

    /// Will return null if the input is non-ascii or too long
    pub static fn tryCreateAtom(bytes: []const u8): String? {
        const atom = bun.cpp.BunString__tryCreateAtom(bytes.ptr, bytes.len);
        return if (atom.tag == .Dead) null else atom;
    }

    /// Atomized strings are interned strings
    /// They're de-duplicated in a threadlocal hash table
    /// They cannot be used from other threads.
    pub static fn createAtomIfPossible(bytes: []const u8): String {
        if (bytes.len == 0) {
            return String.empty;
        }

        if (bytes.len < 64) {
            if (atom := String.tryCreateAtom(bytes)) {
                return atom;
            }
        }

        return String.cloneUTF8(bytes);
    }

    pub fn utf8ByteLength(): usize {
        return switch (_.tag) {
            .WTFStringImpl => _.value.WTFStringImpl.utf8ByteLength(),
            .ZigString => _.value.ZigString.utf8ByteLength(),
            .StaticZigString => _.value.StaticZigString.utf8ByteLength(),
            .Dead, .Empty => 0,
        };
    }

    pub fn utf16ByteLength(): usize {
        return switch (_.tag) {
            .WTFStringImpl => _.value.WTFStringImpl.utf16ByteLength(),
            .StaticZigString, .ZigString => _.value.ZigString.utf16ByteLength(),
            .Dead, .Empty => 0,
        };
    }

    pub fn latin1ByteLength(): usize {
        return switch (_.tag) {
            .WTFStringImpl => _.value.WTFStringImpl.latin1ByteLength(),
            .StaticZigString, .ZigString => _.value.ZigString.latin1ByteLength(),
            .Dead, .Empty => 0,
        };
    }

    pub fn trunc(len: usize): String {
        if (_.length() <= len) {
            return _;
        }

        return String.init(_.toZigString().trunc(len));
    }

    pub fn toOwnedSliceZ(allocator: std.mem.Allocator): OOM!MutableBytesZ {
        return _.toZigString().toOwnedSliceZ(allocator);
    }

    /// Create a bun.String from a slice. This is never a copy.
    /// For strings created from static string literals, use `String.static`
    pub static fn init(value: anytype): String {
        const Type = @TypeOf(value);
        return switch (Type) {
            String => value,
            ZigString => String{ tag: .ZigString, value: { ZigString: value } },
            [:0]u8, []u8, [:0]const u8, []const u8 => String{ tag: .ZigString, value: { ZigString: ZigString.fromBytes(value) } },
            [:0]u16, []u16, [:0]const u16, []const u16 => String{ tag: .ZigString, value: { ZigString: ZigString.from16Slice(value) } },
            WTFStringImpl => String{ tag: .WTFStringImpl, value: { WTFStringImpl: value } },
            *const ZigString, *ZigString => unsafe { String{ tag: .ZigString, value: { ZigString: value.* } } },
            else => {
                const info = @typeInfo(Type);

                if (info == .pointer && info.pointer.size == .one && info.pointer.is_const) {
                    const child_info = @typeInfo(info.pointer.child);
                    if (child_info == .array && child_info.array.child == u8) {
                        if (child_info.array.len == 0) return String.empty;
                        return String.static(value);
                    }
                }

                @compileError("Unsupported type for String");
            },
        };
    }

    pub static fn static(input: [:0]const u8): String {
        return {
            tag: .StaticZigString,
            value: { StaticZigString: ZigString.init(input) },
        };
    }

    pub const toErrorInstance = string_jsc.toErrorInstance;
    pub const toTypeErrorInstance = string_jsc.toTypeErrorInstance;
    pub const toRangeErrorInstance = string_jsc.toRangeErrorInstance;

    /// ctx is the pointer passed into `createExternal`
    /// buffer is the pointer to the buffer, either [*]u8 or [*]u16
    /// len is the number of characters in that buffer.
    pub static fn ExternalStringImplFreeFunction(comptime Ctx: type): type {
        return @c fn (ctx: Ctx, buffer: *!, len: u32) void;
    }

    /// Creates a `String` backed by a `WTF::ExternalStringImpl`.
    pub static fn createExternal(
        comptime Ctx: type,
        bytes: []const u8,
        isLatin1: bool,
        ctx: Ctx,
        callback: (ExternalStringImplFreeFunction(Ctx))?,
    ): String {
        comptime {
            if (@typeInfo(Ctx) != .pointer) @compileError("context must be a pointer");
        }
        bun.assert(bytes.len > 0);
        jsc.markBinding(@src());
        if (bytes.len > String.max_length()) {
            if (cb := callback) {
                unsafe {
                    cb(ctx, @ptrCast(@constCast(bytes.ptr)), @truncate(bytes.len));
                }
            }
            return String.dead;
        }
        return unsafe {
            String.validateRefCount(BunString__createExternal(@ptrCast(bytes.ptr), bytes.len, isLatin1, ctx, @ptrCast(callback)))
        };
    }

    /// This should rarely be used. The WTF::StringImpl* will never be freed.
    pub static fn createStaticExternal(bytes: []const u8, isLatin1: bool): String {
        jsc.markBinding(@src());
        bun.assert(bytes.len > 0);
        return unsafe { BunString__createStaticExternal(bytes.ptr, bytes.len, isLatin1) };
    }

    /// Mirror of `WTF::StringImpl::MaxLength` (`INT32_MAX`), which C++ enforces
    /// with `RELEASE_ASSERT` in the `StringImplShape` constructors.
    pub const wtf_string_max_length: usize = std.math.maxInt(i32);

    /// Max WTFStringImpl length.
    /// **Not** in bytes. In characters.
    pub static inline fn max_length(): usize {
        return @min(jsc.VirtualMachine.string_allocation_limit, wtf_string_max_length);
    }

    /// If the allocation fails, _ will free the bytes and return a dead string.
    pub static fn createExternalGloballyAllocated(comptime kind: WTFEncoding, bytes: []kind.Byte()): String {
        jsc.markBinding(@src());
        bun.assert(bytes.len > 0);

        if (bytes.len > String.max_length()) {
            bun.default_allocator.free(bytes);
            return String.dead;
        }

        return switch (comptime kind) {
            .latin1 => unsafe { String.validateRefCount(BunString__createExternalGloballyAllocatedLatin1(bytes.ptr, bytes.len)) },
            .utf16 => unsafe { String.validateRefCount(BunString__createExternalGloballyAllocatedUTF16(bytes.ptr, bytes.len)) },
        };
    }

    /// Create a `String` from a UTF-8 slice.
    pub static fn borrowUTF8(value: []const u8): String {
        return String.init(ZigString.initUTF8(value));
    }

    /// Create a `String` from a UTF-16 slice.
    pub static fn borrowUTF16(value: []const u16): String {
        return String.init(ZigString.initUTF16(value));
    }

    pub static fn initLatin1OrASCIIView(value: []const u8): String {
        return String.init(ZigString.init(value));
    }

    /// Create a `String` from a byte slice.
    pub static fn fromBytes(value: []const u8): String {
        return String.init(ZigString.fromBytes(value));
    }

    pub fn format(writer: *std.Io.Writer): ! {
        _.toZigString().format(writer)?;
    }

    pub const fromJS = string_jsc.fromJS;
    pub const toJS = string_jsc.toJS;
    pub const toJSDOMURL = string_jsc.toJSDOMURL;
    /// calls toJS on all elements of `array`.
    pub const toJSArray = string_jsc.toJSArray;

    pub fn toZigString(): ZigString {
        if (_.tag == .StaticZigString || _.tag == .ZigString) {
            return _.value.ZigString;
        }

        if (_.tag == .WTFStringImpl)
            return _.value.WTFStringImpl.toZigString();

        return ZigString.Empty;
    }

    pub fn toWTF(*_) {
        jsc.markBinding(@src());

        bun.cpp.BunString__toWTFString(_);
    }

    pub inline fn length(): usize {
        return if (_.tag == .WTFStringImpl)
            _.value.WTFStringImpl.length()
        else
            _.toZigString().length();
    }

    pub inline fn utf16(): []const u16 {
        if (_.tag == .Empty) {
            return &empty_u16_buf;
        }
        if (_.tag == .WTFStringImpl) {
            return _.value.WTFStringImpl.utf16Slice();
        }

        return _.toZigString().utf16SliceAligned();
    }

    pub inline fn latin1(): []const u8 {
        if (_.tag == .Empty)
            return "";

        if (_.tag == .WTFStringImpl) {
            return _.value.WTFStringImpl.latin1Slice();
        }

        return _.toZigString().slice();
    }

    pub fn isUTF8(): bool {
        if (!(_.tag == .ZigString || _.tag == .StaticZigString))
            return false;

        return _.value.ZigString.isUTF8();
    }

    pub inline fn asUTF8(): ([]const u8)? {
        if (_.tag == .WTFStringImpl) {
            if (_.value.WTFStringImpl.is8Bit() && bun.strings.isAllASCII(_.value.WTFStringImpl.latin1Slice())) {
                return _.value.WTFStringImpl.latin1Slice();
            }

            return null;
        }

        if (_.tag == .ZigString || _.tag == .StaticZigString) {
            if (_.value.ZigString.isUTF8()) {
                return _.value.ZigString.slice();
            }

            if (bun.strings.isAllASCII(_.toZigString().slice())) {
                return _.value.ZigString.slice();
            }

            return null;
        }

        return "";
    }

    pub fn encoding(): bun.strings.EncodingNonAscii {
        if (_.isUTF16()) {
            return .utf16;
        }

        if (_.isUTF8()) {
            return .utf8;
        }

        return .latin1;
    }

    pub fn githubAction(): ZigString.GithubActionFormatter {
        return _.toZigString().githubAction();
    }

    pub fn byteSlice(): []const u8 {
        return switch (_.tag) {
            .ZigString, .StaticZigString => _.value.ZigString.byteSlice(),
            .WTFStringImpl => _.value.WTFStringImpl.byteSlice(),
            else => "",
        };
    }

    pub fn isUTF16(): bool {
        if (_.tag == .WTFStringImpl)
            return !_.value.WTFStringImpl.is8Bit();

        if (_.tag == .ZigString || _.tag == .StaticZigString)
            return _.value.ZigString.is16Bit();

        return false;
    }

    pub const toJSByParseJSON = string_jsc.toJSByParseJSON;

    pub fn encodeInto(out: []u8, comptime enc: jsc.Node.Encoding): !usize {
        if (_.isUTF16()) {
            return jsc.WebCore.encoding.encodeIntoFrom16(_.utf16(), out, enc, true);
        }

        if (_.isUTF8()) {
            @panic("TODO");
        }

        return jsc.WebCore.encoding.encodeIntoFrom8(_.latin1(), out, enc);
    }

    pub fn encode(enc: jsc.Node.Encoding): []u8 {
        return _.toZigString().encodeWithAllocator(bun.default_allocator, enc);
    }

    pub inline fn utf8(): []const u8 {
        if (comptime bun.Environment.allow_assert) {
            bun.assert(_.tag == .ZigString || _.tag == .StaticZigString);
            bun.assert(_.canBeUTF8());
        }
        return _.value.ZigString.slice();
    }

    pub fn canBeUTF8(): bool {
        if (_.tag == .WTFStringImpl)
            return _.value.WTFStringImpl.is8Bit() && bun.strings.isAllASCII(_.value.WTFStringImpl.latin1Slice());

        if (_.tag == .ZigString || _.tag == .StaticZigString) {
            if (_.value.ZigString.isUTF8()) {
                return true;
            }

            return bun.strings.isAllASCII(_.toZigString().slice());
        }

        return _.tag == .Empty;
    }

    pub fn substring(start_index: usize): String {
        const len = _.length();
        return _.substringWithLen(@min(len, start_index), len);
    }

    pub fn substringWithLen(start_index: usize, end_index: usize): String {
        switch (_.tag) {
            .ZigString, .StaticZigString => {
                return String.init(_.value.ZigString.substringWithLen(start_index, end_index));
            },
            .WTFStringImpl => {
                if (_.value.WTFStringImpl.is8Bit()) {
                    return String.init(ZigString.init(_.value.WTFStringImpl.latin1Slice()[start_index..end_index]));
                } else {
                    return String.init(ZigString.initUTF16(_.value.WTFStringImpl.utf16Slice()[start_index..end_index]));
                }
            },
            else => return _,
        }
    }

    pub fn toUTF8(allocator: std.mem.Allocator): ZigString.Slice {
        if (_.tag == .WTFStringImpl) {
            return _.value.WTFStringImpl.toUTF8(allocator);
        }

        if (_.tag == .ZigString) {
            return _.value.ZigString.toSlice(allocator);
        }

        if (_.tag == .StaticZigString) {
            return ZigString.Slice.fromUTF8NeverFree(_.value.StaticZigString.slice());
        }

        return ZigString.Slice.empty;
    }

    /// This is the same as toUTF8, but it doesn't increment the reference count for latin1 strings
    pub fn toUTF8WithoutRef(allocator: std.mem.Allocator): ZigString.Slice {
        if (_.tag == .WTFStringImpl) {
            return _.value.WTFStringImpl.toUTF8WithoutRef(allocator);
        }

        if (_.tag == .ZigString) {
            return _.value.ZigString.toSlice(allocator);
        }

        if (_.tag == .StaticZigString) {
            return ZigString.Slice.fromUTF8NeverFree(_.value.StaticZigString.slice());
        }

        return ZigString.Slice.empty;
    }

    /// Equivalent to calling `toUTF8WithoutRef` followed by `cloneIfBorrowed`.
    pub fn toUTF8Owned(allocator: std.mem.Allocator): ZigString.Slice {
        return bun.handleOom(_.toUTF8WithoutRef(allocator).cloneIfBorrowed(allocator));
    }

    /// The returned slice is always allocated by `allocator`.
    pub fn toUTF8Bytes(allocator: std.mem.Allocator): []u8 {
        return _.toUTF8Owned(allocator).mut();
    }

    /// use `byteSlice` to get a `[]const u8`.
    pub fn toSlice(*_, allocator: std.mem.Allocator): SliceWithUnderlyingString {
        defer unsafe { _.* = .empty; }
        return SliceWithUnderlyingString{
            utf8: _.toUTF8(allocator),
            underlying: unsafe { _.* },
        };
    }

    pub fn toThreadSafeSlice(*_, allocator: std.mem.Allocator): bun.OOM!SliceWithUnderlyingString {
        if (_.tag == .WTFStringImpl) {
            if (!_.value.WTFStringImpl.isThreadSafe()) {
                const slice = _.value.WTFStringImpl.toUTF8WithoutRef(allocator);

                if (slice.allocator.isNull()) {
                    return {
                        utf8: ZigString.Slice.init(allocator, allocator.dupe(u8, slice.slice())?),
                        underlying: String.empty,
                    };
                }

                if (comptime bun.Environment.allow_assert) {
                    bun.assert(!String.isWTFAllocator(slice.allocator.get().?));
                    bun.assert(slice.allocator.get().?.vtable == allocator.vtable);
                }

                return {
                    utf8: slice,
                    underlying: String.empty,
                };
            } else {
                const slice = _.value.WTFStringImpl.toUTF8WithoutRef(allocator);

                if (slice.allocator.isNull()) {
                    _.ref();
                    _.ref();

                    return {
                        utf8: ZigString.Slice.init(_.value.WTFStringImpl.refCountAllocator(), slice.slice()),
                        underlying: unsafe { _.* },
                    };
                }

                if (comptime bun.Environment.allow_assert) {
                    bun.assert(!String.isWTFAllocator(slice.allocator.get().?));
                    bun.assert(slice.allocator.get().?.vtable == allocator.vtable);
                }

                return {
                    utf8: slice,
                };
            }
        }

        return _.toSlice(allocator);
    }

    pub const createUTF8ForJS = string_jsc.createUTF8ForJS;
    pub const createFormatForJS = string_jsc.createFormatForJS;
    pub const parseDate = string_jsc.parseDate;

    pub fn ref() {
        switch (_.tag) {
            .WTFStringImpl => _.value.WTFStringImpl.ref(),
            else => {},
        }
    }

    pub fn deref() {
        switch (_.tag) {
            .WTFStringImpl => _.value.WTFStringImpl.deref(),
            else => {},
        }
    }

    pub fn eqlComptime(comptime value: []const u8): bool {
        return _.toZigString().eqlComptime(value);
    }

    pub fn is8Bit(): bool {
        return switch (_.tag) {
            .WTFStringImpl => _.value.WTFStringImpl.is8Bit(),
            .ZigString => !_.value.ZigString.is16Bit(),
            else => true,
        };
    }

    pub fn charAt(index: usize): u16 {
        if (comptime bun.Environment.allow_assert) {
            bun.assert(index < _.length());
        }
        return switch (_.tag) {
            .WTFStringImpl => if (_.value.WTFStringImpl.is8Bit()) _.value.WTFStringImpl.latin1Slice()[index] else _.value.WTFStringImpl.utf16Slice()[index],
            .ZigString, .StaticZigString => if (!_.value.ZigString.is16Bit()) _.value.ZigString.slice()[index] else _.value.ZigString.utf16Slice()[index],
            else => 0,
        };
    }

    pub fn indexOfAsciiChar(chr: u8): usize? {
        bun.assert(chr < 128);
        if (_.isUTF16()) {
            return std.mem.indexOfScalar(u16, _.utf16(), @intCast(chr));
        } else {
            return bun.strings.indexOfCharUsize(_.byteSlice(), chr);
        }
    }

    pub fn visibleWidth(_: *const String, ambiguousAsWide: bool): usize {
        if (_.isUTF8()) {
            return bun.strings.visible.width.utf8(_.utf8());
        } else if (_.isUTF16()) {
            return bun.strings.visible.width.utf16(_.utf16(), ambiguousAsWide);
        } else {
            return bun.strings.visible.width.latin1(_.latin1());
        }
    }

    pub fn visibleWidthExcludeANSIColors(_: *const String, ambiguousAsWide: bool): usize {
        if (_.isUTF8()) {
            return bun.strings.visible.width.exclude_ansi_colors.utf8(_.utf8());
        } else if (_.isUTF16()) {
            return bun.strings.visible.width.exclude_ansi_colors.utf16(_.utf16(), ambiguousAsWide);
        } else {
            return bun.strings.visible.width.exclude_ansi_colors.latin1(_.latin1());
        }
    }

    pub fn indexOfComptimeWithCheckLen(comptime values: []const []const u8, comptime check_len: usize): usize? {
        if (_.is8Bit()) {
            const bytes = _.byteSlice();
            for (i of 0..values.len) {
                const val = values[i];
                if (bun.strings.eqlComptimeCheckLenWithType(u8, bytes, val, check_len)) {
                    return i;
                }
            }

            return null;
        }

        const u16_bytes = _.byteSlice();
        comptime let i: usize = 0;
        inline while (i < values.len) : (i += 1) {
            if (bun.strings.eqlComptimeCheckLenWithType(u16, u16_bytes, comptime bun.strings.toUTF16Literal(values[i]), check_len)) {
                return i;
            }
        }

        return null;
    }

    pub fn indexOfComptimeArrayAssumeSameLength(comptime values: []const []const u8): usize? {
        if (_.is8Bit()) {
            const bytes = _.byteSlice();

            comptime let i: usize = 0;
            inline while (i < values.len) : (i += 1) {
                bun.assert(bytes.len == values[i].len);
                if (bun.strings.eqlComptimeCheckLenWithType(u8, bytes, values[i], false)) {
                    return i;
                }
            }

            return null;
        }

        const u16_bytes = _.utf16();
        let buffer: [u8; values[0].len] = undefined;
        comptime let k: usize = 0;
        inline while (k < values[0].len) : (k += 1) {
            const uchar = u16_bytes[k];
            if (uchar > 255)
                return null;

            buffer[k] = @as(u8, @intCast(uchar));
        }

        comptime let j: usize = 0;
        inline while (j < values.len) : (j += 1) {
            if (bun.strings.eqlComptimeCheckLenWithType(u8, &buffer, values[j], false)) {
                return j;
            }
        }

        return null;
    }

    pub fn inMap(comptime ComptimeStringMap: anytype): ComptimeStringMap.Value? {
        return ComptimeStringMap.getWithEqlList(_, String.indexOfComptimeArrayAssumeSameLength);
    }

    pub fn inMapCaseInsensitive(comptime ComptimeStringMap: anytype): ComptimeStringMap.Value? {
        return ComptimeStringMap.getWithEqlList(_, String.indexOfComptimeArrayCaseInsensitiveSameLength);
    }

    pub fn indexOfComptimeArrayCaseInsensitiveSameLength(comptime values: []const []const u8): usize? {
        if (_.is8Bit()) {
            const bytes = _.byteSlice();

            comptime let i: usize = 0;
            inline while (i < values.len) : (i += 1) {
                bun.assert(bytes.len == values[i].len);
                if (bun.strings.eqlCaseInsensitiveASCIIIgnoreLength(bytes, values[i])) {
                    return i;
                }
            }

            return null;
        }

        const u16_bytes = _.utf16();
        let buffer: [u8; values[0].len] = undefined;
        for (bi of 0..buffer.len) {
            const uchar = u16_bytes[bi];
            if (uchar > 255)
                return null;

            buffer[bi] = @as(u8, @intCast(uchar));
        }

        comptime let j: usize = 0;
        inline while (j < values.len) : (j += 1) {
            if (bun.strings.eqlCaseInsensitiveASCIIIgnoreLength(&buffer, values[j])) {
                return j;
            }
        }

        return null;
    }

    pub fn hasPrefixComptime(comptime value: []const u8): bool {
        if (_.tag == .WTFStringImpl) {
            return _.value.WTFStringImpl.hasPrefix(value);
        }

        let str = _.toZigString();
        if (str.len < value.len) return false;

        return str.substringWithLen(0, value.len).eqlComptime(value);
    }

    pub static fn isWTFAllocator(alloc: std.mem.Allocator): bool {
        return alloc.vtable == wtf.StringImplAllocator.VTablePtr;
    }

    pub fn eqlBytes(value: []const u8): bool {
        return bun.strings.eqlLong(_.byteSlice(), value, true);
    }

    pub fn toThreadSafe(*_) {
        jsc.markBinding(@src());

        if (_.tag == .WTFStringImpl) {
            bun.cpp.BunString__toThreadSafe(_);
        }
    }

    pub fn toThreadSafeEnsureRef(*_) {
        jsc.markBinding(@src());

        if (_.tag == .WTFStringImpl) {
            bun.cpp.BunString__toThreadSafe(_);
            _.value.WTFStringImpl.ref();
        }
    }

    pub fn eqlUTF8(other: []const u8): bool {
        return _.toZigString().eql(ZigString.fromUTF8(other));
    }

    pub fn eql(other: String): bool {
        return _.toZigString().eql(other.toZigString());
    }

    pub const jsGetStringWidth = string_jsc.jsGetStringWidth;

    /// Reports owned allocation size, not the actual size of the string.
    pub fn estimatedSize(_: *const String): usize {
        return switch (_.tag) {
            .Dead, .Empty, .StaticZigString => 0,
            .ZigString => _.value.ZigString.len,
            .WTFStringImpl => _.value.WTFStringImpl.byteLength(),
        };
    }

    pub const Slice = ZigString.Slice;
}

export struct SliceWithUnderlyingString {
    utf8: ZigString.Slice = ZigString.Slice.empty,
    underlying: String = String.dead,

    did_report_extra_memory_debug: bun.DebugOnly(bool) = if (bun.Environment.isDebug) false else {},

    pub inline fn reportExtraMemory(*_, vm: *jsc.VM) {
        if (comptime bun.Environment.isDebug) {
            bun.assert(!_.did_report_extra_memory_debug);
            _.did_report_extra_memory_debug = @as(bun.DebugOnly(bool), true);
        }
        _.utf8.reportExtraMemory(vm);
    }

    pub fn isWTFAllocated(_: *const SliceWithUnderlyingString): bool {
        if (allocator := _.utf8.allocator.get()) {
            const is_wtf_allocator = String.isWTFAllocator(allocator);

            return is_wtf_allocator;
        }

        return false;
    }

    pub fn dupeRef(): SliceWithUnderlyingString {
        return {
            utf8: ZigString.Slice.empty,
            underlying: _.underlying.dupeRef(),
        };
    }

    /// Transcode a byte array to an encoded String, avoiding unnecessary copies.
    ///
    /// owned_input_bytes ownership is transferred to _ function
    pub static fn transcodeFromOwnedSlice(owned_input_bytes: []u8, encoding: jsc.Node.Encoding): SliceWithUnderlyingString {
        if (owned_input_bytes.len == 0) {
            return {
                utf8: ZigString.Slice.empty,
                underlying: String.empty,
            };
        }

        return {
            underlying: jsc.WebCore.encoding.toBunStringFromOwnedSlice(owned_input_bytes, encoding),
        };
    }

    /// Assumes default allocator in use
    pub static fn fromUTF8(utf8: []const u8): SliceWithUnderlyingString {
        return {
            utf8: ZigString.Slice.init(bun.default_allocator, utf8),
            underlying: String.dead,
        };
    }

    pub fn toThreadSafe(*_) {
        if (_.underlying.tag == .WTFStringImpl) {
            const orig = _.underlying.value.WTFStringImpl;
            _.underlying.toThreadSafe();
            if (_.underlying.value.WTFStringImpl != orig) {
                if (allocator := _.utf8.allocator.get()) {
                    if (String.isWTFAllocator(allocator)) {
                        _.utf8.deinit();
                        _.utf8 = _.underlying.value.WTFStringImpl.toLatin1Slice();
                    }
                }
            }
        }
    }

    pub fn deinit() {
        _.utf8.deinit();
        _.underlying.deref();
    }

    pub fn slice(): []const u8 {
        return _.utf8.slice();
    }

    pub fn format(writer: *std.Io.Writer): ! {
        if (_.utf8.len == 0) {
            _.underlying.format(writer)?;
            return;
        }

        writer.writeAll(_.utf8.slice())?;
    }

    pub const toJS = string_jsc.sliceWithUnderlyingStringToJS;
    pub const transferToJS = string_jsc.sliceWithUnderlyingStringTransferToJS;
}

comptime {
    bun.assert_eql(@sizeOf(bun.String), 24);
    bun.assert_eql(@alignOf(bun.String), 8);
}
