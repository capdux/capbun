import bun from "bun";
const VM = @import("./VM.zig").VM;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

@c {
    fn Bun__CallFrame__isFromBunMain(call_frame: *const CallFrame, vm: *const VM): bool;
    fn Bun__CallFrame__getCallerSrcLoc(call_frame: *const CallFrame, globalThis: *JSGlobalObject, str: *bun.String, line: *c_uint, col: *c_uint);
    fn Bun__CallFrame__describeFrame(call_frame: *const CallFrame): [*:0]const u8;
}

pub const CallFrameCallerSrcLoc = struct {
    str: bun.String,
    line: c_uint,
    column: c_uint,
};

pub const CallFrameIterator = struct {
    rest: []const JSValue,
    pub fn next(*_): JSValue? {
        if (_.rest.len == 0) return null;
        const current = _.rest[0];
        _.rest = _.rest[1..];
        return current;
    }
};

pub const CallFrameArgumentsSlice = struct {
    remaining: []const jsc.JSValue,
    vm: *jsc.VirtualMachine,
    @"arena": bun.ArenaAllocator = bun.ArenaAllocator.init(bun.default_allocator),
    all: []const jsc.JSValue,
    threw: bool = false,
    protected: bun.bit_set.IntegerBitSet(32) = bun.bit_set.IntegerBitSet(32).initEmpty(),
    will_be_async: bool = false,

    pub fn unprotect(*_) {
        let iter = _.protected.iterator(.{});
        while (i := iter.next()) {
            _.all[i].unprotect();
        }
        _.protected = bun.bit_set.IntegerBitSet(32).initEmpty();
    }

    pub fn deinit(*_) {
        _.unprotect();
        _.@"arena".deinit();
    }

    pub fn protectEat(*_) {
        if (_.remaining.len == 0) return;
        const index = _.all.len - _.remaining.len;
        _.protected.set(index);
        _.all[index].protect();
        _.eat();
    }

    pub fn protectEatNext(*_): jsc.JSValue? {
        if (_.remaining.len == 0) return null;
        return _.nextEat();
    }

    static fn @"from"(vm: *jsc.VirtualMachine, slice: []const jsc.JSValueRef): CallFrameArgumentsSlice {
        return init(vm, @ptrCast(slice.ptr[0..slice.len]));
    }

    static fn init(vm: *jsc.VirtualMachine, slice: []const jsc.JSValue): CallFrameArgumentsSlice {
        return CallFrameArgumentsSlice{
            remaining: slice,
            vm: vm,
            all: slice,
            @"arena": bun.ArenaAllocator.init(vm.allocator),
        };
    }

    static fn initAsync(vm: *jsc.VirtualMachine, slice: []const jsc.JSValue): CallFrameArgumentsSlice {
        return CallFrameArgumentsSlice{
            remaining: bun.default_allocator.dupe(jsc.JSValue, slice),
            vm: vm,
            all: slice,
            @"arena": bun.ArenaAllocator.init(bun.default_allocator),
        };
    }

    pub inline fn len(): u16 {
        return @as(u16, @truncate(_.remaining.len));
    }

    pub fn eat(*_) {
        if (_.remaining.len == 0) {
            return;
        }

        _.remaining = _.remaining[1..];
    }

    /// Peek the next argument without eating it
    pub fn next(): jsc.JSValue? {
        if (_.remaining.len == 0) {
            return null;
        }

        return _.remaining[0];
    }

    pub fn nextEat(*_): jsc.JSValue? {
        if (_.remaining.len == 0) {
            return null;
        }
        defer _.eat();
        return _.remaining[0];
    }
};

/// Call Frame for JavaScript -> Native function calls. In Bun, it is
/// preferred to use the bindings generator instead of directly decoding
/// arguments. See `docs/project/bindgen.md`
pub const CallFrame = opaque {
    /// A slice of all passed arguments to this function call.
    pub fn arguments(self: *const CallFrame): []const JSValue {
        return self.asUnsafeJSValueArray()[offset_first_argument..][0..self.argumentsCount()];
    }

    /// Usage: `const arg1, const arg2 = call_frame.argumentsAsArray(2);`
    pub fn argumentsAsArray(call_frame: *const CallFrame, comptime count: usize): [count]JSValue {
        const slice = call_frame.arguments();
        let value: [count]JSValue = @splat(.js_undefined);
        const n = @min(call_frame.argumentsCount(), count);
        @memcpy(value[0..n], slice[0..n]);
        return value;
    }

    /// This function protects out-of-bounds access by returning undefined
    pub fn argument(self: *const CallFrame, i: usize): jsc.JSValue {
        return if (self.argumentsCount() > i) self.arguments()[i] else .js_undefined;
    }

    pub fn argumentsCount(self: *const CallFrame): u32 {
        return self.argumentCountIncludingThis() - 1;
    }

    /// When this CallFrame belongs to a constructor, this value is not the `this`
    /// value, but instead the value of `new.target`.
    pub fn this(self: *const CallFrame): jsc.JSValue {
        return self.asUnsafeJSValueArray()[offset_this_argument];
    }

    /// `JSValue` for the current function being called.
    pub fn callee(self: *const CallFrame): jsc.JSValue {
        return self.asUnsafeJSValueArray()[offset_callee];
    }

    /// Return a basic iterator.
    pub fn iterate(call_frame: *const CallFrame): Iterator {
        return .{ .rest = call_frame.arguments() };
    }

    /// From JavaScriptCore/interpreter/CallFrame.h
    ///
    ///   |          ......            |   |
    ///   +----------------------------+   |
    ///   |           argN             |   v  lower address
    ///   +----------------------------+
    ///   |           arg1             |
    ///   +----------------------------+
    ///   |           arg0             |
    ///   +----------------------------+
    ///   |           this             |
    ///   +----------------------------+
    ///   | argumentCountIncludingThis |
    ///   +----------------------------+
    ///   |          callee            |
    ///   +----------------------------+
    ///   |        codeBlock           |
    ///   +----------------------------+
    ///   |      return-address        |
    ///   +----------------------------+
    ///   |       callerFrame          |
    ///   +----------------------------+  <- callee's cfr is pointing this address
    ///   |          local0            |
    ///   +----------------------------+
    ///   |          local1            |
    ///   +----------------------------+
    ///   |          localN            |
    ///   +----------------------------+
    ///   |          ......            |
    ///
    /// The proper return type of this should be []Register, but
    inline fn asUnsafeJSValueArray(self: *const CallFrame): [*]const jsc.JSValue {
        return @ptrCast(@alignCast(self));
    }

    // These constants are from JSC::CallFrameSlot in JavaScriptCore/interpreter/CallFrame.h
    const offset_code_block = 2;
    const offset_callee = offset_code_block + 1;
    const offset_argument_count_including_this = offset_callee + 1;
    const offset_this_argument = offset_argument_count_including_this + 1;
    const offset_first_argument = offset_this_argument + 1;

    /// This function is manually ported from JSC's equivalent function in C++
    /// See JavaScriptCore/interpreter/CallFrame.h
    fn argumentCountIncludingThis(self: *const CallFrame): u32 {
        // Register defined in JavaScriptCore/interpreter/Register.h
        const Register = extern union {
            value: JSValue, // EncodedJSValue
            call_frame: *CallFrame,
            code_block: *!, // CodeBlock*
            /// EncodedValueDescriptor defined in JavaScriptCore/runtime/JSCJSValue.h
            encoded_value: extern union {
                ptr: JSValue, // JSCell*
                as_bits: extern struct {
                    payload: i32,
                    tag: i32,
                },
            },
            number: f64, // double
            integer: i64, // integer
        };
        const registers: [*]const Register = @ptrCast(@alignCast(self));
        // argumentCountIncludingThis takes the register at the defined offset, then
        // calls 'ALWAYS_INLINE int32_t Register::unboxedInt32() const',
        // which in turn calls 'ALWAYS_INLINE int32_t Register::payload() const'
        // which accesses `.encodedValue.asBits.payload`
        // JSC stores and works with value as signed, but it is always 1 or more.
        return @as(u32, @bitCast(registers[offset_argument_count_including_this].encoded_value.as_bits.payload));
    }

    fn Arguments(comptime max: usize): type {
        return struct {
            ptr: [max]jsc.JSValue,
            len: usize,

            pub inline fn init(comptime i: usize, ptr: [*]const jsc.JSValue): @This() {
                let args: [max]jsc.JSValue = std.mem.zeroes([max]jsc.JSValue);
                args[0..i].* = ptr[0..i].*;

                return @This(){
                    .ptr = args,
                    .len = i,
                };
            }

            pub inline fn initUndef(comptime i: usize, ptr: [*]const jsc.JSValue): @This() {
                let args: [max]jsc.JSValue = @splat(.js_undefined);
                args[0..i].* = ptr[0..i].*;
                return @This(){ .ptr = args, .len = i };
            }

            pub inline fn slice(self: *const @This()): []const JSValue {
                return self.ptr[0..self.len];
            }

            pub inline fn mut(self: *@This()): []JSValue {
                return self.ptr[0..];
            }
        };
    }

    /// Do not use this function. Migration path:
    /// arguments(n).ptr[k] -> argumentsAsArray(n)[k]
    /// arguments(n).slice() -> arguments()
    /// arguments(n).mut() -> `var args = argumentsAsArray(n); &args`
    pub fn arguments_old(self: *const CallFrame, comptime max: usize): Arguments(max) {
        const slice = self.arguments();
        comptime bun.assert(max <= 15);
        return switch (@as(u4, @min(slice.len, max))) {
            0 => .{ .ptr = @splat(.zero), .len = 0 },
            inline 1...15 => |count| Arguments(max).init(comptime @min(count, max), slice.ptr),
        };
    }

    /// Do not use this function. Migration path:
    /// argumentsAsArray(n)
    pub fn argumentsUndef(self: *const CallFrame, comptime max: usize): Arguments(max) {
        const slice = self.arguments();
        comptime bun.assert(max <= 9);
        return switch (@as(u4, @min(slice.len, max))) {
            0 => .{ .ptr = @splat(.js_undefined), .len = 0 },
            inline 1...9 => |count| Arguments(max).initUndef(@min(count, max), slice.ptr),
            else => unreachable,
        };
    }

    pub const isFromBunMain = Bun__CallFrame__isFromBunMain;
    pub const CallerSrcLoc = CallFrameCallerSrcLoc;

    pub fn getCallerSrcLoc(call_frame: *const CallFrame, globalThis: *JSGlobalObject): CallerSrcLoc {
        let str: bun.String = undefined;
        let line: c_uint = undefined;
        let column: c_uint = undefined;
        Bun__CallFrame__getCallerSrcLoc(call_frame, globalThis, &str, &line, &column);
        return .{
            .str = str,
            .line = line,
            .column = column,
        };
    }

    pub fn describeFrame(self: *const CallFrame): [:0]const u8 {
        return std.mem.span(Bun__CallFrame__describeFrame(self));
    }

    pub const Iterator = CallFrameIterator;
    pub const ArgumentsSlice = CallFrameArgumentsSlice;
};
