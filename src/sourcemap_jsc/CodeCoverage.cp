// Code coverage reporting and byte range mapping for JSC.
import std from "std";
import bun from "bun";

const LinesHits = bun.collections.BabyList(u32);
const Bitset = bun.bit_set.DynamicBitSetUnmanaged;
const LineOffsetTable = bun.SourceMap.LineOffsetTable;
const LineOffsetList = LineOffsetTable.List;
const BlockList = std.ArrayListUnmanaged(Block);
const Output = bun.Output;
const prettyFmt = Output.prettyFmt;
const ByteRangeMappingHashMap = std.HashMap(u64, ByteRangeMapping, bun.IdentityContext(u64), std.hash_map.default_max_load_percentage);
const Cursor = bun.SourceMap.InternalSourceMap.Cursor;
const Mapping = bun.SourceMap.Mapping;
const AllocatingWriter = std.Io.Writer.Allocating;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    allocator: std.mem.Allocator,
    ~init() {
        _.ptr.deinit(_.allocator);
    }
}

pub struct ErrDeinitWithAllocGuard<T> {
    ptr: *T!,
    allocator: std.mem.Allocator,
    armed: bool = true,
    ~init() {
        if (_.armed) {
            _.ptr.deinit(_.allocator);
        }
    }
}

pub struct DerefOptGuard<T> {
    opt: T,
    ~init() {
        if (v := _.opt) {
            v.deref();
        }
    }
}

@c struct BasicBlockRange {
    startOffset: c_int = 0,
    endOffset: c_int = 0,
    hasExecuted: bool = false,
    executionCount: usize = 0,
}

pub struct Fraction {
    functions: f64 = 0.9,
    lines: f64 = 0.9,
    stmts: f64 = 0.75,
    failing: bool = false,
}

pub struct Block {
    start_line: u32 = 0,
    end_line: u32 = 0,
}

struct Generator {
    allocator: std.mem.Allocator,
    byte_range_mapping: *ByteRangeMapping,
    result: *(Report?)!,

    @c pub fn do(
        *_,
        blocks_ptr: [*]const BasicBlockRange!,
        blocks_len: usize,
        function_start_offset: usize,
        ignore_sourcemap: bool,
    ) {
        const blocks = unsafe { blocks_ptr[0..function_start_offset] };
        let function_blocks = unsafe { blocks_ptr[function_start_offset..blocks_len] };
        if (function_blocks.len > 1) {
            function_blocks = function_blocks[1..];
        }

        if (blocks.len == 0) {
            return;
        }

        const rep = _.byte_range_mapping.generateReportFromBlocks(
            _.allocator,
            _.byte_range_mapping.source_url,
            blocks,
            function_blocks,
            ignore_sourcemap,
        ) catch null;
        unsafe {
            _.result.* = rep;
        }
    }
}

type GeneratorCallback = *const fn (
    gen: *Generator,
    blocks_ptr: [*]const BasicBlockRange!,
    blocks_len: usize,
    function_start_offset: usize,
    ignore_sourcemap: bool,
) callconv(.c) void;

extern fn CodeCoverage__withBlocksAndFunctions(
    vm: *bun.jsc.VM,
    source_id: i32,
    ctx: *!,
    ignore_sourcemap: bool,
    cb: GeneratorCallback,
): bool;

pub struct Report {
    source_url: bun.jsc.ZigString.Slice,
    executable_lines: Bitset,
    lines_which_have_executed: Bitset,
    line_hits: LinesHits = {},
    functions: BlockList,
    functions_which_have_executed: Bitset,
    stmts_which_have_executed: Bitset,
    stmts: BlockList,
    total_lines: u32 = 0,

    pub fn linesCoverageFraction(): f64 {
        let intersected: Bitset = bun.handleOom(_.executable_lines.clone(bun.default_allocator));
        const _guard_intersected = DeinitWithAllocGuard<Bitset>{ ptr: &intersected, allocator: bun.default_allocator };
        intersected.setIntersection(_.lines_which_have_executed);

        const total_count: f64 = @floatFromInt(_.executable_lines.count());
        if (total_count == 0) {
            return 1.0;
        }

        const intersected_count: f64 = @floatFromInt(intersected.count());
        return intersected_count / total_count;
    }

    pub fn stmtsCoverageFraction(): f64 {
        const total_count: f64 = @floatFromInt(_.stmts.items.len);
        if (total_count == 0) {
            return 1.0;
        }
        const exec_count: f64 = @floatFromInt(_.stmts_which_have_executed.count());
        return exec_count / total_count;
    }

    pub fn functionCoverageFraction(): f64 {
        const total_count: f64 = @floatFromInt(_.functions.items.len);
        if (total_count == 0) {
            return 1.0;
        }
        const exec_count: f64 = @floatFromInt(_.functions_which_have_executed.count());
        return exec_count / total_count;
    }

    pub const Text = ReportText;
    pub const Lcov = ReportLcov;

    pub fn deinit(*_, allocator: std.mem.Allocator) {
        _.executable_lines.deinit(allocator);
        _.lines_which_have_executed.deinit(allocator);
        _.line_hits.deinit(allocator);
        _.functions.deinit(allocator);
        _.stmts.deinit(allocator);
        _.functions_which_have_executed.deinit(allocator);
        _.stmts_which_have_executed.deinit(allocator);
    }

    pub static fn generate(
        globalThis: *bun.jsc.JSGlobalObject,
        allocator: std.mem.Allocator,
        byte_range_mapping: *ByteRangeMapping,
        ignore_sourcemap_: bool,
    ): Report? {
        bun.jsc.markBinding(@src());
        const vm = globalThis.vm();

        let result: Report? = null;
        let generator = Generator{
            result: unsafe { *result! },
            allocator: allocator,
            byte_range_mapping: byte_range_mapping,
        };

        const ok = unsafe {
            CodeCoverage__withBlocksAndFunctions(
                vm,
                byte_range_mapping.source_id,
                *generator as *!,
                ignore_sourcemap_,
                &Generator.do,
            )
        };
        if (!ok) {
            return null;
        }

        return result;
    }
}

pub struct ReportText {
    pub static fn writeFormatWithValues(
        filename: []const u8,
        max_filename_length: usize,
        vals: Fraction,
        failing: Fraction,
        failed: bool,
        writer: *std.Io.Writer,
        indent_name: bool,
        comptime enable_colors: bool,
    ): ! {
        @if (enable_colors) {
            if (failed) {
                writer.writeAll(prettyFmt("<r><b><red>", true))?;
            } else {
                writer.writeAll(prettyFmt("<r><b><green>", true))?;
            }
        }

        if (indent_name) {
            writer.writeAll(" ")?;
        }

        writer.writeAll(filename)?;
        const pad_extra: usize = @intFromBool(!indent_name);
        writer.splatByteAll(' ', max_filename_length - filename.len + pad_extra)?;
        writer.writeAll(prettyFmt("<r><d> | <r>", enable_colors))?;

        @if (enable_colors) {
            if (vals.functions < failing.functions) {
                writer.writeAll(prettyFmt("<b><red>", true))?;
            } else {
                writer.writeAll(prettyFmt("<b><green>", true))?;
            }
        }

        writer.print("{d: >7.2}", [vals.functions * 100.0])?;
        writer.writeAll(prettyFmt("<r><d> | <r>", enable_colors))?;

        @if (enable_colors) {
            if (vals.lines < failing.lines) {
                writer.writeAll(prettyFmt("<b><red>", true))?;
            } else {
                writer.writeAll(prettyFmt("<b><green>", true))?;
            }
        }

        writer.print("{d: >7.2}", [vals.lines * 100.0])?;
    }

    pub static fn writeFormat(
        report: *const Report,
        max_filename_length: usize,
        fraction: *Fraction,
        base_path: []const u8,
        writer: *std.Io.Writer,
        comptime enable_colors: bool,
    ): ! {
        const failing: Fraction = unsafe { fraction.* };
        const fns = report.functionCoverageFraction();
        const lines = report.linesCoverageFraction();
        const stmts = report.stmtsCoverageFraction();
        fraction.functions = fns;
        fraction.lines = lines;
        fraction.stmts = stmts;

        const failed = fns < failing.functions || lines < failing.lines;
        fraction.failing = failed;

        let filename = report.source_url.slice();
        if (base_path.len > 0) {
            filename = bun.path.relative(base_path, filename);
        }

        const current_frac: Fraction = unsafe { fraction.* };
        writeFormatWithValues(
            filename,
            max_filename_length,
            current_frac,
            failing,
            failed,
            writer,
            true,
            enable_colors,
        )?;

        writer.writeAll(prettyFmt("<r><d> | <r>", enable_colors))?;

        let executable_lines_that_havent_been_executed: Bitset = bun.handleOom(report.lines_which_have_executed.clone(bun.default_allocator));
        const _guard_unexec = DeinitWithAllocGuard<Bitset>{ ptr: &executable_lines_that_havent_been_executed, allocator: bun.default_allocator };
        executable_lines_that_havent_been_executed.toggleAll();
        executable_lines_that_havent_been_executed.setIntersection(report.executable_lines);

        let iter = executable_lines_that_havent_been_executed.iterator({});
        let start_of_line_range: usize = 0;
        let prev_line: usize = 0;
        let is_first = true;

        while (next_line := iter.next()) {
            if (next_line == prev_line + 1) {
                prev_line = next_line;
                continue;
            } else if (is_first && start_of_line_range == 0 && prev_line == 0) {
                start_of_line_range = next_line;
                prev_line = next_line;
                continue;
            }

            if (is_first) {
                is_first = false;
            } else {
                writer.print(prettyFmt("<r><d>,<r>", enable_colors), {})?;
            }

            if (start_of_line_range == prev_line) {
                writer.print(prettyFmt("<red>{d}", enable_colors), [start_of_line_range + 1])?;
            } else {
                writer.print(prettyFmt("<red>{d}-{d}", enable_colors), [start_of_line_range + 1, prev_line + 1])?;
            }

            prev_line = next_line;
            start_of_line_range = next_line;
        }

        if (prev_line != start_of_line_range) {
            if (is_first) {
                is_first = false;
            } else {
                writer.print(prettyFmt("<r><d>,<r>", enable_colors), {})?;
            }

            if (start_of_line_range == prev_line) {
                writer.print(prettyFmt("<red>{d}", enable_colors), [start_of_line_range + 1])?;
            } else {
                writer.print(prettyFmt("<red>{d}-{d}", enable_colors), [start_of_line_range + 1, prev_line + 1])?;
            }
        }
    }
}

pub struct ReportLcov {
    pub static fn writeFormat(
        report: *const Report,
        base_path: []const u8,
        writer: *std.Io.Writer,
    ): ! {
        let filename = report.source_url.slice();
        if (base_path.len > 0) {
            filename = bun.path.relative(base_path, filename);
        }

        writer.writeAll("TN:\n")?;
        writer.print("SF:{s}\n", [filename])?;
        writer.print("FNF:{d}\n", [report.functions.items.len])?;
        writer.print("FNH:{d}\n", [report.functions_which_have_executed.count()])?;

        let executable_lines: Bitset = bun.handleOom(report.executable_lines.clone(bun.default_allocator));
        const _guard_exec_lines = DeinitWithAllocGuard<Bitset>{ ptr: &executable_lines, allocator: bun.default_allocator };
        let iter = executable_lines.iterator({});

        const line_hits = report.line_hits.slice();
        while (line := iter.next()) {
            writer.print("DA:{d},{d}\n", [line + 1, line_hits[line]])?;
        }

        writer.print("LF:{d}\n", [report.executable_lines.count()])?;
        writer.print("LH:{d}\n", [report.lines_which_have_executed.count()])?;
        writer.writeAll("end_of_record\n")?;
    }
}

pub struct ByteRangeMapping {
    line_offset_table: LineOffsetList = {},
    source_id: i32,
    source_url: bun.jsc.ZigString.Slice,

    pub const HashMap = ByteRangeMappingHashMap;
    @tls pub let map: (*ByteRangeMappingHashMap)? = null;

    pub static fn isLessThan(_: void, a: ByteRangeMapping, b: ByteRangeMapping): bool {
        return bun.strings.order(a.source_url.slice(), b.source_url.slice()) == .lt;
    }

    pub fn deinit(*_) {
        _.line_offset_table.deinit(bun.default_allocator);
    }

    @c pub static fn generate(str: bun.String, source_contents_str: bun.String, source_id: i32) {
        if (map == null) {
            const vm: *bun.jsc.VirtualMachine = bun.jsc.VirtualMachine.get();
            let created = bun.handleOom(vm.allocator.create(HashMap));
            unsafe {
                created.* = HashMap.init(vm.allocator);
            }
            map = created;
        }
        let _map = map.?;
        let slice = str.toUTF8(bun.default_allocator);
        const hash = bun.hash(slice.slice());
        let entry = bun.handleOom(_map.getOrPut(hash));
        if (entry.found_existing) {
            entry.value_ptr.deinit();
        }

        let source_contents = source_contents_str.toUTF8(bun.default_allocator);
        const _guard_source_contents = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &source_contents };

        const computed = compute(source_contents.slice(), source_id, slice);
        unsafe {
            entry.value_ptr.* = computed;
        }
    }

    @c pub fn getSourceID(*_): i32 {
        return _.source_id;
    }

    @c pub static fn find(path: bun.String): (*ByteRangeMapping)? {
        let slice = path.toUTF8(bun.default_allocator);
        const _guard_slice = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &slice };

        if (map == null) {
            return null;
        }
        let map_ = map.?;
        const hash = bun.hash(slice.slice());
        return map_.getPtr(hash);
    }

    pub fn generateReportFromBlocks(
        *_,
        allocator: std.mem.Allocator,
        source_url: bun.jsc.ZigString.Slice,
        blocks: []const BasicBlockRange,
        function_blocks: []const BasicBlockRange,
        ignore_sourcemap: bool,
    ): Report! {
        const line_starts = _.line_offset_table.items(.byte_offset_to_start_of_line);

        let executable_lines: Bitset = Bitset{};
        let lines_which_have_executed: Bitset = Bitset{};
        const vm: *bun.jsc.VirtualMachine = bun.jsc.VirtualMachine.get();
        const parsed_mappings_ = vm.source_mappings.get(source_url.slice());
        const _guard_parsed_mappings = DerefOptGuard<@TypeOf(parsed_mappings_)>{ opt: parsed_mappings_ };
        let line_hits = LinesHits{};
        let _guard_line_hits = ErrDeinitWithAllocGuard<LinesHits>{ ptr: &line_hits, allocator: allocator, armed: false };

        let functions = BlockList{};
        functions.ensureTotalCapacityPrecise(allocator, function_blocks.len)?;
        let _guard_functions = ErrDeinitWithAllocGuard<BlockList>{ ptr: &functions, allocator: allocator };
        let functions_which_have_executed: Bitset = Bitset.initEmpty(allocator, function_blocks.len)?;
        let _guard_fns_exec = ErrDeinitWithAllocGuard<Bitset>{ ptr: &functions_which_have_executed, allocator: allocator };
        let stmts_which_have_executed: Bitset = Bitset.initEmpty(allocator, blocks.len)?;
        let _guard_stmts_exec = ErrDeinitWithAllocGuard<Bitset>{ ptr: &stmts_which_have_executed, allocator: allocator };

        let stmts = BlockList{};
        stmts.ensureTotalCapacityPrecise(allocator, function_blocks.len)?;
        let _guard_stmts = ErrDeinitWithAllocGuard<BlockList>{ ptr: &stmts, allocator: allocator };

        let _guard_exec_lines = ErrDeinitWithAllocGuard<Bitset>{ ptr: &executable_lines, allocator: allocator };
        let _guard_lines_exec = ErrDeinitWithAllocGuard<Bitset>{ ptr: &lines_which_have_executed, allocator: allocator };
        let line_count: u32 = 0;

        if (ignore_sourcemap || parsed_mappings_ == null) {
            line_count = @truncate(line_starts.len);
            executable_lines = Bitset.initEmpty(allocator, line_count)?;
            lines_which_have_executed = Bitset.initEmpty(allocator, line_count)?;
            line_hits = LinesHits.initCapacity(allocator, line_count)?;
            line_hits.len = line_count;
            let line_hits_slice = line_hits.slice();
            @memset(line_hits_slice, 0);
            _guard_line_hits.armed = true;

            for (i of 0..blocks.len) {
                const block = blocks[i];
                if (block.endOffset < 0 || block.startOffset < 0) continue;

                const min: usize = @intCast(@min(block.startOffset, block.endOffset));
                const max: usize = @intCast(@max(block.startOffset, block.endOffset));
                let min_line: u32 = std.math.maxInt(u32);
                let max_line: u32 = 0;
                const has_executed = block.hasExecuted || block.executionCount > 0;

                let byte_offset: usize = min;
                while (byte_offset < max) : (byte_offset += 1) {
                    const start_i32: i32 = @intCast(byte_offset);
                    const new_line_index = LineOffsetTable.findIndex(line_starts, { start: start_i32 }) ?? continue;
                    const line_start_byte_offset = line_starts[new_line_index];
                    if (line_start_byte_offset >= byte_offset) continue;

                    const line: u32 = @intCast(new_line_index);
                    min_line = @min(min_line, line);
                    max_line = @max(max_line, line);

                    executable_lines.set(line);
                    if (has_executed) {
                        lines_which_have_executed.set(line);
                        line_hits_slice[line] += 1;
                    }
                }

                if (min_line != std.math.maxInt(u32)) {
                    if (has_executed) stmts_which_have_executed.set(i as usize);
                    stmts.append(allocator, { start_line: min_line, end_line: max_line })?;
                }
            }

            for (i of 0..function_blocks.len) {
                const function = function_blocks[i];
                if (function.endOffset < 0 || function.startOffset < 0) continue;

                const min: usize = @intCast(@min(function.startOffset, function.endOffset));
                const max: usize = @intCast(@max(function.startOffset, function.endOffset));
                let min_line: u32 = std.math.maxInt(u32);
                let max_line: u32 = 0;

                let byte_offset: usize = min;
                while (byte_offset < max) : (byte_offset += 1) {
                    const start_i32: i32 = @intCast(byte_offset);
                    const new_line_index = LineOffsetTable.findIndex(line_starts, { start: start_i32 }) ?? continue;
                    const line_start_byte_offset = line_starts[new_line_index];
                    if (line_start_byte_offset >= byte_offset) continue;

                    const line: u32 = @intCast(new_line_index);
                    min_line = @min(min_line, line);
                    max_line = @max(max_line, line);
                }

                const did_fn_execute = function.executionCount > 0 || function.hasExecuted;
                if (!did_fn_execute) {
                    const end = @min(max_line, line_count);
                    @memset(line_hits_slice[min_line..end], 0);
                    let line: u32 = min_line;
                    while (line < end) : (line += 1) {
                        executable_lines.set(line);
                        lines_which_have_executed.unset(line);
                    }
                }

                functions.append(allocator, { start_line: min_line, end_line: max_line })?;
                if (did_fn_execute) functions_which_have_executed.set(@intCast(i));
            }
        } else if (parsed_mappings_ != null) {
            const parsed_mapping = parsed_mappings_.?;
            const input_lc: u32 = @truncate(parsed_mapping.input_line_count);
            line_count = input_lc + 1;
            executable_lines = Bitset.initEmpty(allocator, line_count)?;
            lines_which_have_executed = Bitset.initEmpty(allocator, line_count)?;
            line_hits = LinesHits.initCapacity(allocator, line_count)?;
            line_hits.len = line_count;
            let line_hits_slice = line_hits.slice();
            @memset(line_hits_slice, 0);
            _guard_line_hits.armed = true;

            let cur_: Cursor? = parsed_mapping.internalCursor();

            for (i of 0..blocks.len) {
                const block = blocks[i];
                if (block.endOffset < 0 || block.startOffset < 0) continue;

                const min: usize = @intCast(@min(block.startOffset, block.endOffset));
                const max: usize = @intCast(@max(block.startOffset, block.endOffset));
                let min_line: u32 = std.math.maxInt(u32);
                let max_line: u32 = 0;
                const has_executed = block.hasExecuted || block.executionCount > 0;

                let byte_offset: usize = min;
                while (byte_offset < max) : (byte_offset += 1) {
                    const start_i32: i32 = @intCast(byte_offset);
                    const new_line_index = LineOffsetTable.findIndex(line_starts, { start: start_i32 }) ?? continue;
                    const line_start_byte_offset = line_starts[new_line_index];
                    if (line_start_byte_offset >= byte_offset) continue;
                    const column_position = byte_offset -| line_start_byte_offset;

                    const line_zb: i32 = @intCast(new_line_index);
                    const col_zb: i32 = @intCast(column_position);
                    let found: Mapping? = null;
                    if (*c := cur_) {
                        found = c.moveTo(.fromZeroBased(line_zb), .fromZeroBased(col_zb));
                    } else {
                        found = parsed_mapping.findMapping(.fromZeroBased(line_zb), .fromZeroBased(col_zb));
                    }
                    if (point := found) {
                        if (point.original.lines.zeroBased() < 0) continue;
                        const line: u32 = @intCast(point.original.lines.zeroBased());
                        executable_lines.set(line);
                        if (has_executed) {
                            lines_which_have_executed.set(line);
                            line_hits_slice[line] += 1;
                        }
                        min_line = @min(min_line, line);
                        max_line = @max(max_line, line);
                    }
                }

                if (min_line != std.math.maxInt(u32)) {
                    stmts.append(allocator, { start_line: min_line, end_line: max_line })?;
                    if (has_executed) stmts_which_have_executed.set(i as usize);
                }
            }

            for (i of 0..function_blocks.len) {
                const function = function_blocks[i];
                if (function.endOffset < 0 || function.startOffset < 0) continue;

                const min: usize = @intCast(@min(function.startOffset, function.endOffset));
                const max: usize = @intCast(@max(function.startOffset, function.endOffset));
                let min_line: u32 = std.math.maxInt(u32);
                let max_line: u32 = 0;

                let byte_offset: usize = min;
                while (byte_offset < max) : (byte_offset += 1) {
                    const start_i32: i32 = @intCast(byte_offset);
                    const new_line_index = LineOffsetTable.findIndex(line_starts, { start: start_i32 }) ?? continue;
                    const line_start_byte_offset = line_starts[new_line_index];
                    if (line_start_byte_offset >= byte_offset) continue;
                    const column_position = byte_offset -| line_start_byte_offset;

                    const line_zb: i32 = @intCast(new_line_index);
                    const col_zb: i32 = @intCast(column_position);
                    let found: Mapping? = null;
                    if (*c := cur_) {
                        found = c.moveTo(.fromZeroBased(line_zb), .fromZeroBased(col_zb));
                    } else {
                        found = parsed_mapping.findMapping(.fromZeroBased(line_zb), .fromZeroBased(col_zb));
                    }
                    if (point := found) {
                        if (point.original.lines.zeroBased() < 0) continue;
                        const line: u32 = @intCast(point.original.lines.zeroBased());
                        min_line = @min(min_line, line);
                        max_line = @max(max_line, line);
                    }
                }

                if (min_line == std.math.maxInt(u32) && max_line == 0) continue;

                const did_fn_execute = function.executionCount > 0 || function.hasExecuted;
                if (!did_fn_execute) {
                    const end = @min(max_line, line_count);
                    let line: u32 = min_line;
                    while (line < end) : (line += 1) {
                        executable_lines.set(line);
                        lines_which_have_executed.unset(line);
                        line_hits_slice[line] = 0;
                    }
                }

                functions.append(allocator, { start_line: min_line, end_line: max_line })?;
                if (did_fn_execute) functions_which_have_executed.set(@intCast(i));
            }
        } else {
            unreachable;
        }

        _guard_line_hits.armed = false;
        _guard_functions.armed = false;
        _guard_fns_exec.armed = false;
        _guard_stmts_exec.armed = false;
        _guard_stmts.armed = false;
        _guard_exec_lines.armed = false;
        _guard_lines_exec.armed = false;

        return Report{
            source_url: source_url,
            functions: functions,
            executable_lines: executable_lines,
            lines_which_have_executed: lines_which_have_executed,
            line_hits: line_hits,
            total_lines: line_count,
            stmts: stmts,
            functions_which_have_executed: functions_which_have_executed,
            stmts_which_have_executed: stmts_which_have_executed,
        };
    }

    @c pub static fn findExecutedLines(
        globalThis: *bun.jsc.JSGlobalObject,
        source_url: bun.String,
        blocks_ptr: [*]const BasicBlockRange!,
        blocks_len: usize,
        function_start_offset: usize,
        ignore_sourcemap: bool,
    ): bun.jsc.JSValue {
        const mapping_opt = ByteRangeMapping.find(source_url);
        if (mapping_opt == null) {
            return bun.jsc.JSValue.@"null";
        }
        let mapping = mapping_opt.?;

        const blocks = unsafe { blocks_ptr[0..function_start_offset] };
        let function_blocks = unsafe { blocks_ptr[function_start_offset..blocks_len] };
        if (function_blocks.len > 1) {
            function_blocks = function_blocks[1..];
        }
        let url_slice = source_url.toUTF8(bun.default_allocator);
        const _guard_url_slice = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &url_slice };
        let report: Report = mapping.generateReportFromBlocks(bun.default_allocator, url_slice, blocks, function_blocks, ignore_sourcemap) catch {
            return globalThis.throwOutOfMemoryValue();
        };
        const _guard_report = DeinitWithAllocGuard<Report>{ ptr: &report, allocator: bun.default_allocator };

        let coverage_fraction: Fraction = Fraction{};

        let allocating_writer: AllocatingWriter = AllocatingWriter.init(bun.default_allocator);
        const _guard_writer = DeinitGuard<AllocatingWriter>{ ptr: &allocating_writer };
        const buffered_writer = &allocating_writer.writer;

        Report.Text.writeFormat(&report, source_url.utf8ByteLength(), &coverage_fraction, "", buffered_writer, false) catch {
            return globalThis.throwOutOfMemoryValue();
        };

        buffered_writer.flush() catch {
            return globalThis.throwOutOfMemoryValue();
        };

        return bun.String.createUTF8ForJS(globalThis, allocating_writer.written()) catch bun.jsc.JSValue.zero;
    }

    pub static fn compute(source_contents: []const u8, source_id: i32, source_url: bun.jsc.ZigString.Slice): ByteRangeMapping {
        const vm: *bun.jsc.VirtualMachine = bun.jsc.VirtualMachine.get();
        return ByteRangeMapping{
            line_offset_table: LineOffsetTable.generate(vm.allocator, source_contents, 0),
            source_id: source_id,
            source_url: source_url,
        };
    }
}

comptime {
    if (bun.Environment.isNative) {
        @export(&ByteRangeMapping.generate, { name: "ByteRangeMapping__generate" });
        @export(&ByteRangeMapping.findExecutedLines, { name: "ByteRangeMapping__findExecutedLines" });
        @export(&ByteRangeMapping.find, { name: "ByteRangeMapping__find" });
        @export(&ByteRangeMapping.getSourceID, { name: "ByteRangeMapping__getSourceID" });
    }
}
