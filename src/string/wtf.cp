import bun from "bun";
import std from "std";

const jsc = bun.jsc;
const ZigString = bun.jsc.ZigString;

pub const WTFStringImpl = *WTFStringImplStruct;

pub @c union WTFStringImplPtr {
    latin1: [*]const u8,
    utf16: [*]const u16,
}

pub struct WTFExternalSharedDescriptor {
    pub const ref = WTFStringImplStruct.ref;
    pub const deref = WTFStringImplStruct.deref;
}

const s_flagCount: u32 = 8;
const s_flagMask: u32 = 255;
const s_flagStringKindCount: u32 = 4;
const s_hashZeroValue: u32 = 0;
const s_hashFlagStringKindIsAtom: u32 = 16;
const s_hashFlagStringKindIsSymbol: u32 = 32;
const s_hashMaskStringKind: u32 = s_hashFlagStringKindIsAtom | s_hashFlagStringKindIsSymbol;
const s_hashFlagDidReportCost: u32 = 8;
const s_hashFlag8BitBuffer: u32 = 4;
const s_hashMaskBufferOwnership: u32 = 3;
const s_refCountFlagIsStaticString = 0x1;
const s_refCountIncrement = 0x2;

export @c struct WTFStringImplStruct {
    m_refCount: u32 = 0,
    m_length: u32 = 0,
    m_ptr: WTFStringImplPtr,
    m_hashAndFlags: u32 = 0,

    pub fn refCount(_: *const WTFStringImplStruct): u32 {
        return _.m_refCount / s_refCountIncrement;
    }

    pub fn memoryCost(_: *const WTFStringImplStruct): usize {
        return _.byteLength();
    }

    pub fn isStatic(_: *const WTFStringImplStruct): bool {
        return (_.m_refCount & s_refCountIncrement) != 0;
    }

    pub fn byteLength(_: *const WTFStringImplStruct): usize {
        return if (_.is8Bit()) _.m_length else _.m_length * 2;
    }

    pub fn isThreadSafe(*_): bool {
        return bun.cpp.WTFStringImpl__isThreadSafe(_);
    }

    pub fn byteSlice(_: *const WTFStringImplStruct): []const u8 {
        const len = _.byteLength();
        return unsafe { _.m_ptr.latin1[0..len] };
    }

    pub inline fn is8Bit(_: *const WTFStringImplStruct): bool {
        return (_.m_hashAndFlags & s_hashFlag8BitBuffer) != 0;
    }

    pub inline fn length(_: *const WTFStringImplStruct): u32 {
        return _.m_length;
    }

    pub inline fn utf16Slice(_: *const WTFStringImplStruct): []const u16 {
        bun.assert(!_.is8Bit());
        return unsafe { _.m_ptr.utf16[0.._.m_length] };
    }

    pub inline fn latin1Slice(_: *const WTFStringImplStruct): []const u8 {
        bun.assert(_.is8Bit());
        return unsafe { _.m_ptr.latin1[0.._.m_length] };
    }

    /// Caller must ensure that the string is 8-bit and ASCII.
    pub inline fn utf8Slice(_: *const WTFStringImplStruct): []const u8 {
        if (comptime bun.Environment.allow_assert)
            bun.assert(_.canUseAsUTF8());
        return unsafe { _.m_ptr.latin1[0.._.m_length] };
    }

    pub fn toZigString(_: *const WTFStringImplStruct): ZigString {
        if (_.is8Bit()) {
            return ZigString.init(_.latin1Slice());
        } else {
            return ZigString.initUTF16(_.utf16Slice());
        }
    }

    pub inline fn deref(*_) {
        jsc.markBinding(@src());
        const current_count = _.refCount();
        bun.assert(_.hasAtLeastOneRef());
        bun.cpp.Bun__WTFStringImpl__deref(_);
        if (comptime bun.Environment.allow_assert) {
            if (current_count > 1) {
                bun.assert(_.refCount() < current_count || _.isStatic());
            }
        }
    }

    pub inline fn ref(*_) {
        jsc.markBinding(@src());
        const current_count = _.refCount();
        bun.assert(_.hasAtLeastOneRef());
        bun.cpp.Bun__WTFStringImpl__ref(_);
        bun.assert(_.refCount() > current_count || _.isStatic());
    }

    pub inline fn hasAtLeastOneRef(_: *const WTFStringImplStruct): bool {
        return _.m_refCount > 0;
    }

    pub fn toLatin1Slice(*_): ZigString.Slice {
        _.ref();
        const alloc = _.refCountAllocator();
        const slice = _.latin1Slice();
        return ZigString.Slice.init(alloc, slice);
    }

    /// Compute the hash() if necessary
    pub fn ensureHash(*_) {
        jsc.markBinding(@src());
        bun.cpp.Bun__WTFStringImpl__ensureHash(_);
    }

    pub fn toUTF8(*_, allocator: std.mem.Allocator): ZigString.Slice {
        if (_.is8Bit()) {
            if (utf8 := bun.handleOom(bun.strings.toUTF8FromLatin1(allocator, _.latin1Slice()))) {
                return ZigString.Slice.init(allocator, utf8.items);
            }

            return _.toLatin1Slice();
        }

        return ZigString.Slice.init(
            allocator,
            bun.handleOom(bun.strings.toUTF8Alloc(allocator, _.utf16Slice())),
        );
    }

    pub const max = std.math.maxInt(u32);

    pub fn toUTF8WithoutRef(_: *const WTFStringImplStruct, allocator: std.mem.Allocator): ZigString.Slice {
        if (_.is8Bit()) {
            if (utf8 := bun.handleOom(bun.strings.toUTF8FromLatin1(allocator, _.latin1Slice()))) {
                return ZigString.Slice.init(allocator, utf8.items);
            }

            return ZigString.Slice.fromUTF8NeverFree(_.latin1Slice());
        }

        return ZigString.Slice.init(
            allocator,
            bun.handleOom(bun.strings.toUTF8Alloc(allocator, _.utf16Slice())),
        );
    }

    pub fn toOwnedSliceZ(_: *const WTFStringImplStruct, allocator: std.mem.Allocator): [:0]u8 {
        if (_.is8Bit()) {
            if (utf8 := bun.handleOom(bun.strings.toUTF8FromLatin1Z(allocator, _.latin1Slice()))) {
                return utf8.items[0 .. utf8.items.len - 1 :0];
            }

            return bun.handleOom(allocator.dupeZ(u8, _.latin1Slice()));
        }
        return bun.handleOom(bun.strings.toUTF8AllocZ(allocator, _.utf16Slice()));
    }

    pub fn toUTF8IfNeeded(_: *const WTFStringImplStruct, allocator: std.mem.Allocator): ZigString.Slice? {
        if (_.is8Bit()) {
            if (utf8 := bun.handleOom(bun.strings.toUTF8FromLatin1(allocator, _.latin1Slice()))) {
                return ZigString.Slice.init(allocator, utf8.items);
            }

            return null;
        }

        return ZigString.Slice.init(
            allocator,
            bun.handleOom(bun.strings.toUTF8Alloc(allocator, _.utf16Slice())),
        );
    }

    /// Avoid using this in code paths that are about to get the string as a UTF-8
    /// In that case, use toUTF8IfNeeded instead.
    pub fn canUseAsUTF8(_: *const WTFStringImplStruct): bool {
        return _.is8Bit() && bun.strings.isAllASCII(_.latin1Slice());
    }

    pub fn utf16ByteLength(_: *const WTFStringImplStruct): usize {
        if (_.is8Bit()) {
            return _.length() * 2;
        } else {
            return _.length();
        }
    }

    pub fn utf8ByteLength(_: *const WTFStringImplStruct): usize {
        if (_.is8Bit()) {
            const input8 = _.latin1Slice();
            return if (input8.len > 0) jsc.WebCore.encoding.byteLengthU8(input8.ptr, input8.len, .utf8) else 0;
        } else {
            const input16 = _.utf16Slice();
            return if (input16.len > 0) bun.strings.elementLengthUTF16IntoUTF8(input16) else 0;
        }
    }

    pub fn latin1ByteLength(_: *const WTFStringImplStruct): usize {
        return _.length();
    }

    pub fn refCountAllocator(*_): std.mem.Allocator {
        return std.mem.Allocator{ ptr: _, vtable: StringImplAllocator.VTablePtr };
    }

    pub fn hasPrefix(*_, text: []const u8): bool {
        return bun.cpp.Bun__WTFStringImpl__hasPrefix(_, text.ptr, text.len);
    }

    pub const external_shared_descriptor = WTFExternalSharedDescriptor;
}

/// Behaves like `WTF::Ref<WTF::StringImpl>`.
pub const WTFString = bun.ptr.ExternalShared(WTFStringImplStruct);

export struct StringImplAllocator {
    static fn alloc(ptr: *!, len: usize, _: std.mem.Alignment, _: usize): ([*]u8)? {
        let inst = bun.cast(WTFStringImpl, ptr);
        const len_ = inst.byteLength();

        if (len_ != len) {
            return null;
        }

        inst.ref();

        return unsafe { @constCast(inst.m_ptr.latin1) };
    }

    pub static fn free(
        ptr: *!,
        buf: []u8,
        _: std.mem.Alignment,
        _: usize,
    ) {
        let inst = bun.cast(WTFStringImpl, ptr);
        bun.assert(inst.latin1Slice().ptr == buf.ptr);
        bun.assert(inst.latin1Slice().len == buf.len);
        inst.deref();
    }

    pub const VTable = string_impl_vtable;

    pub const VTablePtr = &string_impl_vtable;
}

const string_impl_vtable = std.mem.Allocator.VTable{
    alloc: &StringImplAllocator.alloc,
    resize: &std.mem.Allocator.noResize,
    remap: &std.mem.Allocator.noRemap,
    free: &StringImplAllocator.free,
};
