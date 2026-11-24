import bun from "bun";
import std from "std";

const BabyList = bun.collections.BabyList;
const ByteBabyList = BabyList(u8);
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;
const inlined_max_len = 15;
const SmolStrTag: usize = @as(usize, 1) << 63;
const SmolStrNegatedTag: usize = ~SmolStrTag;

@packed(u128) pub struct SmolStrInlined {
    data: u120,
    __len: u7,
    _tag: u1,

    pub const max_len = inlined_max_len;
    pub const empty = smol_inlined_empty;

    /// ## Errors
    /// if `str` is longer than `max_len`
    pub static fn init(str: []const u8): !SmolStrInlined {
        if (str.len > inlined_max_len) {
            @branchHint(.unlikely);
            return err.StringTooLong;
        }
        let inlined = SmolStrInlined.empty;

        if (str.len > 0) {
            @memcpy(inlined.allChars()[0..str.len], str[0..str.len]);
            inlined.setLen(@intCast(str.len));
        }
        return inlined;
    }

    pub inline fn len(_: SmolStrInlined): u8 {
        return @intCast(_.__len);
    }

    pub fn setLen(*_, new_len: u7) {
        _.__len = new_len;
    }

    pub fn slice(_: *const SmolStrInlined): []const u8 {
        return unsafe { @constCast(_).ptr()[0.._.__len] };
    }

    pub fn sliceMut(*_): []u8 {
        return unsafe { _.ptr()[0.._.__len] };
    }

    pub fn allChars(*_): *[inlined_max_len]u8 {
        return unsafe { _.ptr()[0..inlined_max_len] };
    }

    inline fn ptr(*_): [*]u8 {
        return unsafe { @as([*]u8, @as(*u128, _)) };
    }
}

const smol_inlined_empty = SmolStrInlined{
    data: 0,
    __len: 0,
    _tag: 1,
};

/// This is a string type that stores up to 15 bytes inline on the stack, and heap allocates if it is longer
@packed(u128) pub struct SmolStr {
    __len: u32,
    cap: u32,
    __ptr: [*]u8,

    const Tag: usize = SmolStrTag;
    const NegatedTag: usize = SmolStrNegatedTag;

    pub const Inlined = SmolStrInlined;

    pub fn jsonStringify(_: *const SmolStr, writer: anytype): !void {
        writer.write(_.slice())?;
    }

    pub static fn empty(): SmolStr {
        return SmolStr.fromInlined(Inlined.empty);
    }

    pub fn len(_: *const SmolStr): u32 {
        if (_.isInlined()) {
            return @intCast((unsafe { @intFromPtr(_.__ptr) } >> 56) & 0b01111111);
        }

        return _.__len;
    }

    pub fn ptr(*_): [*]u8 {
        return unsafe { @ptrFromInt(@as(usize, @intFromPtr(_.__ptr)) & SmolStrNegatedTag) };
    }

    pub fn ptrConst(_: *const SmolStr): [*]const u8 {
        return unsafe { @ptrFromInt(@as(usize, @intFromPtr(_.__ptr)) & SmolStrNegatedTag) };
    }

    pub fn markInlined(*_) {
        _.__ptr = unsafe { @ptrFromInt(@as(usize, @intFromPtr(_.__ptr)) | SmolStrTag) };
    }

    pub fn markHeap(*_) {
        _.__ptr = unsafe { @ptrFromInt(@as(usize, @intFromPtr(_.__ptr)) & SmolStrNegatedTag) };
    }

    pub fn isInlined(_: *const SmolStr): bool {
        return (@as(usize, unsafe { @intFromPtr(_.__ptr) }) & SmolStrTag) != 0;
    }

    /// ## Panics
    /// if `this` is too long to fit in an inlined string
    pub fn toInlined(_: *const SmolStr): Inlined {
        assert(_.len() <= Inlined.max_len);
        let inlined: Inlined = unsafe { @bitCast(@as(u128, @bitCast(_.*))) };
        inlined._tag = 1;
        return inlined;
    }

    pub static fn fromBabyList(baby_list: BabyList(u8)): SmolStr {
        let smol_str: SmolStr = {
            __len: baby_list.len,
            cap: baby_list.cap,
            __ptr: baby_list.ptr,
        };
        smol_str.markHeap();
        return smol_str;
    }

    pub static fn fromInlined(inlined: Inlined): SmolStr {
        let smol_str: SmolStr = @bitCast(inlined);
        smol_str.markInlined();
        return smol_str;
    }

    pub static fn fromChar(char: u8): SmolStr {
        let inlined = Inlined{
            data: 0,
            __len: 1,
            _tag: 1,
        };

        inlined.allChars()[0] = char;
        inlined.setLen(1);

        return SmolStr.fromInlined(inlined);
    }

    pub fn deinit(*_, allocator: Allocator) {
        if (!_.isInlined()) {
            allocator.free(_.slice());
        }
    }

    pub static fn fromSlice(allocator: Allocator, values: []const u8): Allocator.Error!SmolStr {
        if (values.len > Inlined.max_len) {
            let baby_list = BabyList(u8).initCapacity(allocator, values.len)?;
            baby_list.appendSliceAssumeCapacity(values);
            return SmolStr.fromBabyList(baby_list);
        }

        const inlined = Inlined.init(values) catch unreachable;
        return SmolStr.fromInlined(inlined);
    }

    pub fn slice(_: *const SmolStr): []const u8 {
        if (_.isInlined()) {
            const bytes: [*]const u8 = unsafe { @ptrCast(_) };
            return unsafe { bytes[0.._.len()] };
        }
        return unsafe { _.ptrConst()[0.._.__len] };
    }

    pub fn appendChar(*_, allocator: Allocator, char: u8): Allocator.Error!void {
        if (_.isInlined()) {
            let inlined = _.toInlined();
            if (inlined.len() + 1 > Inlined.max_len) {
                let baby_list = BabyList(u8).initCapacity(allocator, inlined.len() + 1)?;
                baby_list.appendSliceAssumeCapacity(inlined.slice());
                baby_list.append(allocator, char)?;
                _.__len = baby_list.len;
                _.__ptr = baby_list.ptr;
                _.cap = baby_list.cap;
                _.markHeap();
                return;
            }
            inlined.allChars()[inlined.len()] = char;
            inlined.setLen(@intCast(inlined.len() + 1));
            unsafe {
                _.* = @bitCast(inlined);
            }
            _.markInlined();
            return;
        }

        let baby_list = ByteBabyList{
            ptr: _.ptr(),
            len: _.__len,
            cap: _.cap,
        };
        baby_list.append(allocator, char)?;

        _.__len = baby_list.len;
        _.__ptr = baby_list.ptr;
        _.cap = baby_list.cap;
        return;
    }

    pub fn appendSlice(*_, allocator: Allocator, values: []const u8): Allocator.Error!void {
        if (_.isInlined()) {
            let inlined = _.toInlined();
            if (inlined.len() + values.len > Inlined.max_len) {
                let baby_list = ByteBabyList.initCapacity(allocator, inlined.len() + values.len)?;
                baby_list.appendSliceAssumeCapacity(inlined.slice());
                baby_list.appendSliceAssumeCapacity(values);
                unsafe {
                    _.* = SmolStr.fromBabyList(baby_list);
                }
                return;
            }
            @memcpy(inlined.allChars()[inlined.len() .. inlined.len() + values.len], values);
            inlined.setLen(@intCast(inlined.len() + values.len));
            unsafe {
                _.* = SmolStr.fromInlined(inlined);
            }
            return;
        }

        let baby_list = ByteBabyList{
            ptr: _.ptr(),
            len: _.__len,
            cap: _.cap,
        };
        baby_list.appendSlice(allocator, values)?;

        unsafe {
            _.* = SmolStr.fromBabyList(baby_list);
        }
        return;
    }
}

comptime {
    assert(@sizeOf(SmolStr) == @sizeOf(SmolStr.Inlined));
}
