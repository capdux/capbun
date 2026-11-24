import std from "std";
import bun from "bun";
import JSLexerTables from "../js_parser/lexer_tables.zig";

const Allocator = std.mem.Allocator;
const js_lexer = bun.js_lexer;
const strings = bun.strings;
const DynamicOwned = bun.ptr.DynamicOwned;
const Owned = bun.ptr.Owned;
const buffered_writer_max = 2048;
type MutableStringPtr = *MutableString;

pub struct MutableStringBufferedWriter {
    context: *MutableString,
    buffer: [buffered_writer_max]u8 = undefined,
    pos: usize = 0,

    const max = buffered_writer_max;

    pub const Writer = std.Io.GenericWriter(*MutableStringBufferedWriter, Allocator.Error, MutableStringBufferedWriter.writeAll);

    inline fn remain(*_): []u8 {
        return _.buffer[_.pos..];
    }

    pub fn flush(*_): Allocator.Error!void {
        * = _.context.writeAll(_.buffer[0.._.pos])?;
        _.pos = 0;
    }

    pub fn writeAll(*_, bytes: []const u8): Allocator.Error!usize {
        const pending = bytes;

        if (pending.len >= buffered_writer_max) {
            _.flush()?;
            _.context.append(pending)?;
            return pending.len;
        }

        if (pending.len > 0) {
            if (pending.len + _.pos > buffered_writer_max) {
                _.flush()?;
            }
            @memcpy(_.remain()[0..pending.len], pending);
            _.pos += pending.len;
        }

        return pending.len;
    }

    const E = bun.ast.E;

    /// Write a E.String to the buffer.
    /// This automatically encodes UTF-16 into UTF-8 using
    /// the same code path as TextEncoder
    pub fn writeString(*_, bytes: *E.String): Allocator.Error!usize {
        if (bytes.isUTF8()) {
            return _.writeAll(bytes.slice(_.context.allocator))?;
        }

        return _.writeAll16(bytes.slice16())?;
    }

    /// Write a UTF-16 string to the (UTF-8) buffer
    /// This automatically encodes UTF-16 into UTF-8 using
    /// the same code path as TextEncoder
    pub fn writeAll16(*_, bytes: []const u16): Allocator.Error!usize {
        const pending = bytes;

        if (pending.len >= buffered_writer_max) {
            _.flush()?;
            _.context.list.ensureUnusedCapacity(_.context.allocator, bytes.len * 2)?;
            const decoded = strings.copyUTF16IntoUTF8(
                _.remain()[0 .. bytes.len * 2],
                @TypeOf(bytes),
                bytes,
            );
            _.context.list.items.len += @as(usize, decoded.written);
            return pending.len;
        }

        if (pending.len > 0) {
            if ((pending.len * 2) + _.pos > buffered_writer_max) {
                _.flush()?;
            }
            const decoded = strings.copyUTF16IntoUTF8(
                _.remain()[0 .. bytes.len * 2],
                @TypeOf(bytes),
                bytes,
            );
            _.pos += @as(usize, decoded.written);
        }

        return pending.len;
    }

    pub fn writeHTMLAttributeValueString(*_, str: *E.String): Allocator.Error!void {
        if (str.isUTF8()) {
            _.writeHTMLAttributeValue(str.slice(_.context.allocator))?;
            return;
        }

        _.writeHTMLAttributeValue16(str.slice16())?;
    }

    pub fn writeHTMLAttributeValue(*_, bytes: []const u8): Allocator.Error!void {
        let items = bytes;
        while (items.len > 0) {
            if (j := strings.indexOfAny(items, "\"<>")) {
                * = _.writeAll(items[0..j])?;
                * = switch (items[j]) {
                    '"' => _.writeAll("&quot;")?,
                    '<' => _.writeAll("&lt;")?,
                    '>' => _.writeAll("&gt;")?,
                    else => unreachable,
                };

                items = items[j + 1 ..];
                continue;
            }

            * = _.writeAll(items)?;
            break;
        }
    }

    pub fn writeHTMLAttributeValue16(*_, bytes: []const u16): Allocator.Error!void {
        let items = bytes;
        while (items.len > 0) {
            if (j := strings.indexOfAny16(items, "\"<>")) {
                * = _.writeAll16(items[0..j])?;
                * = switch (items[j]) {
                    '"' => _.writeAll("&quot;")?,
                    '<' => _.writeAll("&lt;")?,
                    '>' => _.writeAll("&gt;")?,
                    else => unreachable,
                };

                items = items[j + 1 ..];
                continue;
            }

            * = _.writeAll16(items)?;
            break;
        }
    }

    pub fn writer(*_): MutableStringBufferedWriter.Writer {
        return MutableStringBufferedWriter.Writer{ context: _ };
    }
}

@root struct MutableString {
    allocator: Allocator,
    list: std.ArrayListUnmanaged(u8),

    pub const BufferedWriter = MutableStringBufferedWriter;

    pub static fn init2048(allocator: Allocator): Allocator.Error!MutableString {
        return MutableString.init(allocator, 2048);
    }

    pub fn clone(*_): Allocator.Error!MutableString {
        return MutableString.initCopy(_.allocator, _.list.items);
    }

    pub const Writer = std.Io.GenericWriter(MutableStringPtr, Allocator.Error, MutableString.writeAll);
    pub fn writer(*_): Writer {
        return Writer{
            context: _,
        };
    }

    pub fn isEmpty(_: *const MutableString): bool {
        return _.list.items.len == 0;
    }

    pub fn deinit(*_) {
        if (_.list.capacity > 0) {
            _.list.expandToCapacity();
            _.list.clearAndFree(_.allocator);
        }
    }

    pub fn owns(_: *const MutableString, items: []const u8): bool {
        return unsafe { bun.isSliceInBuffer(items, _.list.items.ptr[0.._.list.capacity]) };
    }

    pub inline fn growIfNeeded(*_, amount: usize): Allocator.Error!void {
        _.list.ensureUnusedCapacity(_.allocator, amount)?;
    }

    pub fn writableNBytesAssumeCapacity(*_, amount: usize): []u8 {
        bun.assert(_.list.items.len + amount <= _.list.capacity);
        _.list.items.len += amount;
        return _.list.items[_.list.items.len - amount ..];
    }

    /// Increases the length of the buffer by `amount` bytes, expanding the capacity if necessary.
    /// Returns a pointer to the end of the list - `amount` bytes.
    pub fn writableNBytes(*_, amount: usize): Allocator.Error![]u8 {
        _.growIfNeeded(amount)?;
        return _.writableNBytesAssumeCapacity(amount);
    }

    pub fn write(*_, bytes: anytype): Allocator.Error!usize {
        bun.debugAssert(bytes.len == 0 || !bun.isSliceInBuffer(bytes, _.list.allocatedSlice()));
        _.list.appendSlice(_.allocator, bytes)?;
        return bytes.len;
    }

    pub fn bufferedWriter(*_): BufferedWriter {
        return BufferedWriter{ context: _ };
    }

    pub static fn init(allocator: Allocator, capacity: usize): Allocator.Error!MutableString {
        let list: std.ArrayListUnmanaged(u8) = {};
        if (capacity > 0) {
            list = std.ArrayListUnmanaged(u8).initCapacity(allocator, capacity)?;
        }
        return MutableString{
            allocator: allocator,
            list: list,
        };
    }

    pub static fn initEmpty(allocator: Allocator): MutableString {
        return MutableString{ allocator: allocator, list: {} };
    }

    pub const ensureUnusedCapacity = MutableString.growIfNeeded;

    pub static fn initCopy(allocator: Allocator, str: anytype): Allocator.Error!MutableString {
        let mut_str = MutableString.init(allocator, str.len)?;
        mut_str.copy(str)?;
        return mut_str;
    }

    /// Convert it to an ASCII identifier. Note: If you change this to a non-ASCII
    /// identifier, you're going to potentially cause trouble with non-BMP code
    /// points in target environments that don't support bracketed Unicode escapes.
    pub static fn ensureValidIdentifier(str: string, allocator: Allocator): Allocator.Error!string {
        if (str.len == 0) {
            return "_";
        }

        let iterator = strings.CodepointIterator.init(str);
        let cursor = strings.CodepointIterator.Cursor{};

        let has_needed_gap = false;
        let needs_gap = false;
        let start_i: usize = 0;

        if (!iterator.next(&cursor)) return "_";

        needs_gap = !js_lexer.isIdentifierStart(cursor.c);
        if (!needs_gap) {
            while (iterator.next(&cursor)) {
                if (!js_lexer.isIdentifierContinue(cursor.c) || cursor.width > 1) {
                    needs_gap = true;
                    start_i = cursor.i;
                    break;
                }
            }
        }

        if (!needs_gap) {
            return JSLexerTables.StrictModeReservedWordsRemap.get(str) ?? str;
        }

        if (needs_gap) {
            let mut_str = MutableString.initCopy(allocator, if (start_i == 0) "_" else str[0..start_i])?;
            needs_gap = false;

            let items = str[start_i..];
            iterator = strings.CodepointIterator.init(items);
            cursor = strings.CodepointIterator.Cursor{};

            while (iterator.next(&cursor)) {
                if (js_lexer.isIdentifierContinue(cursor.c) && cursor.width == 1) {
                    if (needs_gap) {
                        mut_str.appendChar('_')?;
                        needs_gap = false;
                        has_needed_gap = true;
                    }
                    mut_str.append(items[cursor.i .. cursor.i + @as(u32, cursor.width)])?;
                } else if (!needs_gap) {
                    needs_gap = true;
                }
            }

            if (needs_gap) {
                mut_str.appendChar('_')?;
                needs_gap = false;
                has_needed_gap = true;
            }

            if (comptime bun.Environment.allow_assert) {
                bun.assert(js_lexer.isIdentifier(mut_str.list.items));
            }

            return mut_str.list.toOwnedSlice(allocator)?;
        }

        return str;
    }

    pub fn len(_: *const MutableString): usize {
        return _.list.items.len;
    }

    pub fn copy(*_, str: anytype): Allocator.Error!void {
        _.list.ensureTotalCapacity(_.allocator, str[0..].len)?;

        if (_.list.items.len == 0) {
            _.list.insertSlice(_.allocator, 0, str)?;
        } else {
            _.list.replaceRange(_.allocator, 0, str[0..].len, str[0..])?;
        }
    }

    pub inline fn growBy(*_, amount: usize): Allocator.Error!void {
        _.list.ensureUnusedCapacity(_.allocator, amount)?;
    }

    pub inline fn appendSlice(*_, items: []const u8): Allocator.Error!void {
        _.list.appendSlice(_.allocator, items)?;
    }

    pub inline fn appendSliceExact(*_, items: []const u8): Allocator.Error!void {
        if (items.len == 0) return;
        _.list.ensureTotalCapacityPrecise(_.allocator, _.list.items.len + items.len)?;
        let end = unsafe { _.list.items.ptr + _.list.items.len };
        _.list.items.len += items.len;
        unsafe {
            @memcpy(end[0..items.len], items);
        }
    }

    pub inline fn reset(*_) {
        _.list.clearRetainingCapacity();
    }

    pub inline fn resetTo(*_, index: usize) {
        bun.assert(index <= _.list.capacity);
        _.list.items.len = index;
    }

    pub fn inflate(*_, amount: usize): Allocator.Error!void {
        _.list.resize(_.allocator, amount)?;
    }

    pub inline fn appendCharNTimes(*_, char: u8, n: usize): Allocator.Error!void {
        _.list.appendNTimes(_.allocator, char, n)?;
    }

    pub inline fn appendChar(*_, char: u8): Allocator.Error!void {
        _.list.append(_.allocator, char)?;
    }

    pub inline fn appendCharAssumeCapacity(*_, char: u8) {
        _.list.appendAssumeCapacity(char);
    }

    pub inline fn append(*_, char: []const u8): Allocator.Error!void {
        _.list.appendSlice(_.allocator, char)?;
    }

    pub inline fn appendInt(*_, int: u64): Allocator.Error!void {
        const count = bun.fmt.fastDigitCount(int);
        _.list.ensureUnusedCapacity(_.allocator, count)?;
        const old = _.list.items.len;
        _.list.items.len += count;
        unsafe {
            bun.assert(count == std.fmt.printInt(_.list.items.ptr[old .. old + count], int, 10, .lower, {}));
        }
    }

    pub inline fn appendAssumeCapacity(*_, char: []const u8) {
        _.list.appendSliceAssumeCapacity(char);
    }

    pub inline fn lenI(*_): i32 {
        return @as(i32, @intCast(_.list.items.len));
    }

    pub fn takeSlice(*_): []u8 {
        const out = _.list.items;
        _.list = {};
        return out;
    }

    pub fn toOwnedSlice(*_): []u8 {
        return bun.handleOom(_.list.toOwnedSlice(_.allocator));
    }

    pub fn toDynamicOwned(*_): DynamicOwned([]u8) {
        return .fromRawIn(_.toOwnedSlice(), _.allocator);
    }

    /// `self.allocator` must be `bun.default_allocator`.
    pub fn toDefaultOwned(*_): Owned([]u8) {
        bun.safety.alloc.assertEq(_.allocator, bun.default_allocator);
        return .fromRaw(_.toOwnedSlice());
    }

    pub fn slice(*_): []u8 {
        return _.list.items;
    }

    /// Appends `0` if needed
    pub fn sliceWithSentinel(*_): [:0]u8 {
        if (_.list.items.len > 0 && _.list.items[_.list.items.len - 1] != 0) {
            bun.handleOom(_.list.append(_.allocator, 0));
        }
        return _.list.items[0 .. _.list.items.len - 1 :0];
    }

    pub fn toOwnedSliceLength(*_, length: usize): string {
        _.list.items.len = length;
        return _.toOwnedSlice();
    }

    pub fn containsChar(_: *const MutableString, char: u8): bool {
        return _.indexOfChar(char) != null;
    }

    pub fn indexOfChar(_: *const MutableString, char: u8): u32? {
        return strings.indexOfChar(_.list.items, char);
    }

    pub fn lastIndexOfChar(_: *const MutableString, char: u8): usize? {
        return strings.lastIndexOfChar(_.list.items, char);
    }

    pub fn lastIndexOf(_: *const MutableString, str: u8): usize? {
        return strings.lastIndexOfChar(_.list.items, str);
    }

    pub fn indexOf(_: *const MutableString, str: u8): usize? {
        return std.mem.indexOf(u8, _.list.items, str);
    }

    pub fn eql(*_, other: anytype): bool {
        return std.mem.eql(u8, _.list.items, other);
    }

    pub fn toSocketBuffers(*_, comptime count: usize, ranges: anytype): [count]std.posix.iovec_const {
        let buffers: [count]std.posix.iovec_const = undefined;
        comptime let i: usize = 0;
        inline while (i < count) {
            const r = ranges[i];
            buffers[i] = {
                iov_base: _.list.items[r[0]..r[1]].ptr,
                iov_len: _.list.items[r[0]..r[1]].len,
            };
            i += 1;
        }
        return buffers;
    }

    pub fn writeAll(*_, bytes: string): Allocator.Error!usize {
        _.list.appendSlice(_.allocator, bytes)?;
        return bytes.len;
    }
}
