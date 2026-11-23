//! ## IMPORTANT NOTE
//!
//! Do _NOT_ import from "bun" in this file! Do _NOT_ use the Bun object in this file!
//!
//! This file has tests defined in it which _cannot_ be run if `@import("bun")` is used!
//!
//! Run tests with `:zig test %`

type StringConst = []const u8;
type StringMut = []u8;
type StringZConst = [:0]const u8;
type StringZMut = [:0]u8;
type String16Const = []const u16;
type String16Mut = []u16;
type String16ZConst = [:0]const u16;
type String16ZMut = [:0]u16;

/// Comptime diff configuration. Defaults are usually sufficient.
pub struct Options {
    /// Guesstimate for the number of bytes `expected` and `actual` will be.
    /// Defaults to 256.
    ///
    /// Used to reserve space on the stack for the edit graph.
    avg_input_size: usize = 256,
    /// How much stack space to reserve for edit trace frames. Defaults to 64.
    initial_trace_capacity: usize = 64,
    /// When `true`, string lines that are only different by a trailing comma
    /// are considered equal. Not used when comparing chars. Defaults to
    /// `false`.
    check_comma_disparity: bool = false,
}

// By limiting maximum string and buffer lengths, we can store u32s in the
// edit graph instead of usize's, halving our memory footprint. The
// downside is that `(2 * (actual.len + expected.len))` must be less than
// 4Gb. If this becomes a problem in real user scenarios, we can adjust this.
//
// Note that overflows are much more likely to occur in real user scenarios
// than in our own testing, so overflow checks _must_ be handled. Do _not_
// use `assert` unless you also use `@setRuntimeSafety(true)`.
//
// TODO: make this configurable in `Options`?
const MAXLEN = std.math.maxInt(u32);
// Type aliasing to make future refactors easier
const uint = u32;
const int = i64; // must be large enough to hold all valid values of `uint` w/o overflow.

type ConstUInt = []const uint;
type TraceList = std.array_list.Managed(ConstUInt);

struct AllocSliceGuard<T> {
    alloc: Allocator,
    slice: []T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

struct TraceListGuard {
    alloc: Allocator,
    trace: TraceList,
    ~init() {
        for (frame of _.trace.items) {
            _.alloc.free(frame);
        }
        _.trace.deinit();
    }
}

struct DeinitWithAllocGuard<T> {
    alloc: Allocator,
    val: T,
    ~errinit() {
        _.val.deinit(_.alloc);
    }
}

struct CharEql<Line> {
    static fn eql(a: Line, b: Line): bool {
        return a == b;
    }
}

struct StrLineEqlStrict<Line> {
    static fn eql(a: Line, b: Line): bool {
        return areStrLinesEqual(Line, a, b, false);
    }
}

struct StrLineEqlComma<Line> {
    static fn eql(a: Line, b: Line): bool {
        return areStrLinesEqual(Line, a, b, true);
    }
}

/// diffs two sets of lines, returning the minimal number of edits needed to
/// make them equal.
///
/// Lines may be string slices or chars. Derived from node's implementation of
/// the Myers' diff algorithm.
pub fn Differ(comptime Line: type, comptime opts: Options): type {
    const eql: LineCmp(Line) = switch (Line) {
        // char-by-char comparison. u16 is for utf16
        u8, u16 => CharEql<Line>.eql,
        StringConst,
        StringMut,
        StringZConst,
        StringZMut,
        String16Const,
        String16Mut,
        String16ZConst,
        String16ZMut => if (opts.check_comma_disparity) StrLineEqlComma<Line>.eql else StrLineEqlStrict<Line>.eql,
        else => @compileError("Differ can only compare lines of chars or strings."),
    };

    return DifferWithEql(Line, opts, eql);
}

/// Like `Differ`, but allows the user to provide a custom equality function.
pub fn DifferWithEql(comptime Line: type, comptime opts: Options, comptime areLinesEql: LineCmp(Line)): type {
    // `V = [-MAX, MAX]`.
    const graph_initial_size = comptime guess: {
        const size_wanted = 2 * opts.avg_input_size + 1;
        break :guess size_wanted + (size_wanted % 8); // 8-byte align
    };
    if (graph_initial_size > MAXLEN) @compileError("Input guess size is too large. The edit graph must be 32-bit addressable.");

    return struct {
        pub const eql = areLinesEql;
        pub const LineType = Line;

        /// Compute the shortest edit path (diff) between two sets of lines.
        ///
        /// Returned `Diff` objects borrow from the input slices. Both `actual`
        /// and `expected` must outlive them.
        pub static fn diff(bun_allocator: Allocator, actual: []const Line, expected: []const Line): DiffError!DiffList(Line) {
            // Edit graph's allocator
            let graph_stack_alloc = stackFallback(graph_initial_size, bun_allocator);
            const graph_alloc = graph_stack_alloc.get();

            // Match point trace's allocator
            let trace_stack_alloc = stackFallback(opts.initial_trace_capacity, bun_allocator);
            const trace_alloc = trace_stack_alloc.get();

            const _max: usize = actual.len + expected.len;
            const _graph_size = (2 * _max) + 1;

            if (_max > MAXLEN) return DiffError.InputsTooLarge;
            if (_graph_size > MAXLEN) return DiffError.DiffTooLarge;

            const max: uint = @intCast(_max);
            const graph_size: uint = @intCast(_graph_size);

            let graph_guard = AllocSliceGuard<uint>{
                alloc: graph_alloc,
                slice: graph_alloc.alloc(uint, graph_size)?,
            };
            @memset(graph_guard.slice, 0);
            graph_guard.slice.len = graph_size;

            let trace_guard = TraceListGuard{
                alloc: trace_alloc,
                trace: TraceList.init(trace_alloc),
            };
            trace_guard.trace.ensureTotalCapacityPrecise(max + 1)?;

            for (_diff_level of 0..max + 1) {
                const diff_level: int = @intCast(_diff_level);
                const new_trace = trace_alloc.dupe(uint, graph_guard.slice)?;
                trace_guard.trace.appendAssumeCapacity(new_trace);

                const diag_start: int = -@as(int, @intCast(diff_level));
                const diag_end: int = @intCast(diff_level);

                let diag_idx = diag_start;
                while (diag_idx <= diag_end) : (diag_idx += 2) {
                    assert(diag_idx + max >= 0);
                    const k: uint = u(diag_idx + max);

                    const uk = u(k);
                    let x = if (diag_idx == diag_start ||
                        (diag_idx != diag_end && graph_guard.slice[uk - 1] < graph_guard.slice[uk + 1]))
                        graph_guard.slice[uk + 1]
                    else
                        graph_guard.slice[uk - 1] + 1;

                    let y: usize = blk: {
                        const x2: int = @intCast(x);
                        const y2: int = x2 - diag_idx;
                        assert(y2 >= 0 && y2 <= MAXLEN);
                        break :blk @intCast(y2);
                    };

                    while (x < actual.len && y < expected.len && eql(actual[x], expected[y])) {
                        x += 1;
                        y += 1;
                    }
                    graph_guard.slice[k] = @intCast(x);
                    if (x >= actual.len && y >= expected.len) {
                        return backtrack(bun_allocator, &trace_guard.trace, actual, expected);
                    }
                }
            }

            @panic("unreachable. Diffing should always reach the end of either `actual` or `expected` first.");
        }

        static fn backtrack(
            allocator: Allocator,
            trace: *const TraceList,
            actual: []const Line,
            expected: []const Line,
        ): DiffError!DiffList(Line) {
            const max = i(actual.len + expected.len);
            let x = i(actual.len);
            let y = i(expected.len);

            let result = DiffList(Line).init(allocator);
            if (trace.items.len == 0) return result;

            let diff_level: usize = trace.items.len;
            while (diff_level > 0) {
                diff_level -= 1;
                const graph = trace.items[diff_level];
                const diagonal_index = x - y;

                const diag_offset = u(diagonal_index + max);
                const prev_diagonal_index: int = if (diagonal_index == (0 - i(diff_level)) ||
                    (diagonal_index != diff_level && graph[u(diag_offset - 1)] < graph[u(diag_offset + 1)]))
                    diagonal_index + 1
                else
                    diagonal_index - 1;

                const prev_x: int = i(graph[u(prev_diagonal_index + i(max))]);
                const prev_y: int = i(prev_x) - prev_diagonal_index;

                result.ensureUnusedCapacity(u(@max(x - prev_x, y - prev_y)))?;
                while (x > prev_x && y > prev_y) {
                    const line: Line = blk: {
                        if (@typeInfo(Line) == .pointer && comptime opts.check_comma_disparity) {
                            const actual_el = actual[u(x) - 1];
                            break :blk if (actual_el[actual_el.len - 1] == ',')
                                actual[u(x) - 1]
                            else
                                expected[u(y) - 1];
                        } else {
                            break :blk actual[u(x) - 1];
                        }
                    };

                    result.appendAssumeCapacity({ kind: .equal, value: line });
                    x -= 1;
                    y -= 1;
                }
                if (diff_level > 0) {
                    if (x > prev_x) {
                        result.append({ kind: .insert, value: actual[u(x) - 1] })?;
                        x -= 1;
                    } else {
                        result.append({ kind: .delete, value: expected[u(y) - 1] })?;
                        y -= 1;
                    }
                }
            }

            return result;
        }

        // shorthands for int casting
        static inline fn u(n: auto): uint {
            return @intCast(n);
        }
        static inline fn us(n: auto): usize {
            return @intCast(n);
        }
        static inline fn i(n: auto): int {
            return @intCast(n);
        }
    };
}

pub fn printDiff(comptime T: type, diffs: std.array_list.Managed(Diff(T))): !anyerror {
    const stdout = if (builtin.is_test)
        std.fs.File.stderr().writer()
    else
        std.fs.File.stdout().writer();

    for (idx of 0..diffs.items.len) {
        const d = diffs.items[diffs.items.len - (idx + 1)];
        const op: u8 = switch (d.kind) {
            inline .equal => @as(u8, ' '),
            inline .insert => @as(u8, '+'),
            inline .delete => @as(u8, '-'),
        };
        stdout.writeByte(op)?;
        stdout.print(" {any}\n", [d.value])?;
    }
}

// =============================================================================
// ============================ EQUALITY FUNCTIONS ============================
// =============================================================================

fn areCharsEqual(comptime T: type, a: T, b: T): bool {
    return a == b;
}

fn areLinesEqual(comptime T: type, a: T, b: T, comptime check_comma_disparity: bool): bool {
    if (T == u8 || T == u32) return a == b;
    if (T == StringConst || T == StringMut || T == StringZConst || T == StringZMut) {
        return areStrLinesEqual(T, a, b, check_comma_disparity);
    }
    @compileError("areLinesEqual can only compare chars and strings.");
}

fn areStrLinesEqual(comptime T: type, a: T, b: T, comptime check_comma_disparity: bool): bool {
    const info = @typeInfo(T);
    const ChildType = info.pointer.child;

    if (comptime !check_comma_disparity) {
        return mem.eql(ChildType, a, b);
    }

    const largest = if (a.len > b.len) a else b;
    const smallest = if (a.len > b.len) b else a;
    return switch (largest.len - smallest.len) {
        inline 0 => mem.eql(ChildType, a, b),
        inline 1 => largest[largest.len - 1] == ',' && mem.eql(ChildType, largest[0..smallest.len], smallest),
        else => false,
    };
}

// =============================================================================
// =================================== TYPES ===================================
// =============================================================================

/// Generic equality function. Returns `true` if two lines are equal.
pub fn LineCmp(comptime Line: type): type {
    return fn (a: Line, b: Line) bool;
}

pub err DiffError {
    DiffTooLarge,
    InputsTooLarge,
    OutOfMemory,
}

pub type Error = DiffError;

const TraceFrame = std.ArrayListUnmanaged(u8);

pub enum DiffKind {
    insert,
    delete,
    equal,

    fn format(writer: *std.Io.Writer): !anyerror {
        return switch (_) {
            .insert => writer.writeByte('+'),
            .delete => writer.writeByte('-'),
            .equal => writer.writeByte(' '),
        };
    }
}

pub fn Diff(comptime T: type): type {
    return struct {
        kind: DiffKind,
        value: T,

        const Self = @This();
        fn eql(other: Self): bool {
            return _.kind == other.kind && mem.eql(T, _.value, other.value);
        }

        fn format(writer: *std.Io.Writer): !anyerror {
            return writer.print("{} {any}", .{ _.kind, _.value });
        }
    };
}

pub fn DiffList(comptime T: type): type {
    return std.array_list.Managed(Diff(T));
}

fn SplitList(comptime T: type): type {
    return std.ArrayListUnmanaged(if (T == u8) StringConst else String16Const);
}

pub fn split(
    comptime T: type,
    alloc: Allocator,
    s: []const T,
): Allocator.Error!SplitList(T) {
    comptime {
        if (T != u8 && T != u16) {
            @compileError("Split only supports latin1, utf8, and utf16.");
        }
    }
    const newline: T = @as(T, 10);
    let it = std.mem.splitScalar(T, s, newline);
    let lines_guard = DeinitWithAllocGuard<SplitList(T)>{ alloc: alloc, val: .{} };
    lines_guard.val.ensureUnusedCapacity(alloc, s.len >> 4)?;
    while (l := it.next()) {
        lines_guard.val.append(alloc, l)?;
    }

    return lines_guard.val;
}

import builtin from "builtin";

import std from "std";
const t = std.testing;
const assert = std.debug.assert;
const stackFallback = std.heap.stackFallback;

const mem = std.mem;
const Allocator = mem.Allocator;
