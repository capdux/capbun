import { Expect } from "./expect.zig";
import { DiffFormatter } from "./diff_format.zig";
import jest from "./jest.zig";
const Jest = jest.Jest;
const TestRunner = jest.TestRunner;

import bun from "bun";
const js_ast = bun.ast;
const js_parser = bun.js_parser;
const logger = bun.logger;
const strings = bun.strings;

const jsc = bun.jsc;
const VirtualMachine = jsc.VirtualMachine;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() { _.ptr.deinit(); }
}

pub struct FreeGuard<T> {
    slice: ([]const T)?,
    allocator: std.mem.Allocator,
    ~init() {
        if (s := _.slice) _.allocator.free(s);
    }
}

pub struct CloseGuard {
    file: (*std.fs.File!)?,
    ~init() {
        if (f := _.file) f.close();
    }
}

pub struct SnapshotInlineSnapshotToWrite {
    line: c_ulong,
    col: c_ulong,
    value: []const u8, // owned by Snapshots.allocator
    has_matchers: bool,
    is_added: bool,
    kind: []const u8, // static lifetime
    start_indent: ([]const u8)?, // owned by Snapshots.allocator
    end_indent: ([]const u8)?, // owned by Snapshots.allocator

    static fn lessThanFn(_: void, a: SnapshotInlineSnapshotToWrite, b: SnapshotInlineSnapshotToWrite): bool {
        if (a.line < b.line) return true;
        if (a.line > b.line) return false;
        if (a.col < b.col) return true;
        return false;
    }
}

pub struct SnapshotFile {
    id: TestRunner.File.ID,
    file: std.fs.File,
}

pub struct AddCountResult {
    name: []const u8,
    counter: usize,
}

pub struct UpdateRange {
    final_start: i32,
    final_end: i32,
    needs_pre_comma: bool,
}

pub struct Snapshots {
    const file_header = "// Bun Snapshot v1, https://bun.sh/docs/test/snapshots\n";
    const snapshots_dir_name = if (bun.Environment.isWindows) "__snapshots__\\" else "__snapshots__/";
    pub const ValuesHashMap = std.HashMap(usize, string, bun.IdentityContext(usize), std.hash_map.default_max_load_percentage);

    allocator: std.mem.Allocator,
    update_snapshots: bool,
    total: usize = 0,
    added: usize = 0,
    passed: usize = 0,
    failed: usize = 0,

    file_buf: *std.array_list.Managed(u8),
    values: *ValuesHashMap,
    counts: *bun.StringHashMap(usize),
    _current_file: SnapshotFile? = null,
    snapshot_dir_path: string? = null,
    inline_snapshots_to_write: *std.AutoArrayHashMap(TestRunner.File.ID, std.array_list.Managed(SnapshotInlineSnapshotToWrite)),
    last_error_snapshot_name: ([]const u8)? = null,

    pub const InlineSnapshotToWrite = SnapshotInlineSnapshotToWrite;
    pub const File = SnapshotFile;

    /// Reset per-run snapshot counters to 0. Keys stay owned by the map until
    /// `writeSnapshotFile` tears them down on file switch.
    fn resetCounts(*_) {
        let it = _.counts.valueIterator();
        while (v := it.next()) v.* = 0;
    }

    fn addCount(*_, expect: *Expect, hint: []const u8): !AddCountResult {
        _.total += 1;
        const snapshot_name = expect.getSnapshotName(_.allocator, hint)?;
        const count_entry = _.counts.getOrPut(snapshot_name)?;
        if (count_entry.found_existing) {
            _.allocator.free(snapshot_name);
            count_entry.value_ptr.* += 1;
            return AddCountResult{ name: count_entry.key_ptr.*, counter: count_entry.value_ptr.* };
        }
        count_entry.value_ptr.* = 1;
        return AddCountResult{ name: count_entry.key_ptr.*, counter: count_entry.value_ptr.* };
    }

    fn getOrPut(*_, expect: *Expect, target_value: []const u8, hint: string): !string? {
        let buntest_opt = expect.bunTest();
        if (buntest_opt == null) return error.SnapshotFailed;
        let buntest_strong = buntest_opt.?;
        const _buntest_guard = DeinitGuard<@TypeOf(buntest_strong)>{ ptr: &buntest_strong };
        const bunTest = buntest_strong.get();
        switch (_.getSnapshotFile(bunTest.file_id)?) {
            .result => {},
            .err => |e| {
                return switch (e.syscall) {
                    .mkdir => error.FailedToMakeSnapshotDirectory,
                    .open => error.FailedToOpenSnapshotFile,
                    else => error.SnapshotFailed,
                };
            },
        }

        const count_res = _.addCount(expect, hint)?;
        const name = count_res.name;
        const counter = count_res.counter;

        let counter_string_buf: [32]u8 = undefined;
        const counter_string = std.fmt.bufPrint(&counter_string_buf, "{d}", .{counter})?;

        let name_with_counter = _.allocator.alloc(u8, name.len + 1 + counter_string.len)?;
        const _name_guard = FreeGuard<u8>{ slice: name_with_counter, allocator: _.allocator };
        bun.copy(u8, name_with_counter[0..name.len], name);
        name_with_counter[name.len] = ' ';
        bun.copy(u8, name_with_counter[name.len + 1 ..], counter_string);

        const name_hash = bun.hash(name_with_counter);
        if (expected := _.values.get(name_hash)) {
            return expected;
        }

        // doesn't exist. append to file bytes and add to hashmap.
        // Prevent snapshot creation in CI environments unless --update-snapshots is used
        if (bun.ci.isCI()) {
            if (!_.update_snapshots) {
                // Store the snapshot name for error reporting
                if (old_name := _.last_error_snapshot_name) {
                    _.allocator.free(old_name);
                    _.last_error_snapshot_name = null;
                }
                _.last_error_snapshot_name = _.allocator.dupe(u8, name_with_counter)?;
                return error.SnapshotCreationNotAllowedInCI;
            }
        }

        const estimated_length = "\nexports[`".len + name_with_counter.len + "`] = `".len + target_value.len + "`;\n".len;
        _.file_buf.ensureUnusedCapacity(estimated_length + 10)?;
        _.file_buf.writer().print(
            "\nexports[`{f}`] = `{f}`;\n",
            .{
                strings.formatEscapes(name_with_counter, .{ .quote_char = '`' }),
                strings.formatEscapes(target_value, .{ .quote_char = '`' }),
            },
        )?;

        _.added += 1;
        _.values.put(name_hash, _.allocator.dupe(u8, target_value)?)?;
        return null;
    }

    fn parseFile(*_, file: SnapshotFile): !void {
        if (_.file_buf.items.len == 0) return;

        const vm = VirtualMachine.get();
        const opts = js_parser.Parser.Options.init(vm.transpiler.options.jsx, .js);
        let temp_log = logger.Log.init(_.allocator);

        const test_file = Jest.runner.?.files.get(file.id);
        const test_filename = test_file.source.path.name.filename;
        const dir_path = test_file.source.path.name.dirWithTrailingSlash();

        let snapshot_file_path_buf: bun.PathBuffer = undefined;
        let remain: []u8 = snapshot_file_path_buf[0..bun.MAX_PATH_BYTES];
        bun.copy(u8, remain, dir_path);
        remain = remain[dir_path.len..];
        bun.copy(u8, remain, snapshots_dir_name);
        remain = remain[snapshots_dir_name.len..];
        bun.copy(u8, remain, test_filename);
        remain = remain[test_filename.len..];
        bun.copy(u8, remain, ".snap");
        remain = remain[".snap".len..];
        remain[0] = 0;
        const snapshot_file_path = snapshot_file_path_buf[0 .. snapshot_file_path_buf.len - remain.len :0];

        const source = &logger.Source.initPathString(snapshot_file_path, _.file_buf.items);

        let parser = js_parser.Parser.init(
            opts,
            &temp_log,
            source,
            vm.transpiler.options.define,
            _.allocator,
        )?;

        const parse_result = parser.parse()?;
        if (parse_result != .ast) return error.ParseError;
        let ast = parse_result.ast;
        const _ast_guard = DeinitGuard<@TypeOf(ast)>{ ptr: &ast };

        if (ast.exports_ref.isNull()) return;
        const exports_ref = ast.exports_ref;

        // TODO: when common js transform changes, keep this updated or add flag to support this version

        for (part of ast.parts.slice()) {
            for (stmt of part.stmts) {
                switch (stmt.data) {
                    .s_expr => |expr| {
                        if (expr.value.data == .e_binary && expr.value.data.e_binary.op == .bin_assign) {
                            const left = expr.value.data.e_binary.left;
                            if (left.data == .e_index && left.data.e_index.index.data == .e_string && left.data.e_index.target.data == .e_identifier) {
                                const target: js_ast.E.Identifier = left.data.e_index.target.data.e_identifier;
                                let index: *js_ast.E.String = left.data.e_index.index.data.e_string;
                                if (target.ref.eql(exports_ref) && expr.value.data.e_binary.right.data == .e_string) {
                                    const key = index.slice(_.allocator);
                                    let value_string = expr.value.data.e_binary.right.data.e_string;
                                    const value = value_string.slice(_.allocator);
                                    const _key_guard = FreeGuard<u8>{ slice: if (!index.isUTF8()) key else null, allocator: _.allocator };
                                    const _val_guard = FreeGuard<u8>{ slice: if (!value_string.isUTF8()) value else null, allocator: _.allocator };
                                    const value_clone = _.allocator.alloc(u8, value.len)?;
                                    bun.copy(u8, value_clone, value);
                                    const name_hash = bun.hash(key);
                                    _.values.put(name_hash, value_clone)?;
                                }
                            }
                        }
                    },
                    else => {},
                }
            }
        }
    }

    fn writeSnapshotFile(*_): !void {
        if (_file := _._current_file) {
            let file = _file;
            file.file.writeAll(_.file_buf.items) catch {
                return error.FailedToWriteSnapshotFile;
            };
            file.file.close();
            _.file_buf.clearAndFree();

            let value_itr = _.values.valueIterator();
            while (value := value_itr.next()) {
                _.allocator.free(value.*);
            }
            _.values.clearAndFree();

            let count_key_itr = _.counts.keyIterator();
            while (key := count_key_itr.next()) {
                _.allocator.free(key.*);
            }
            _.counts.clearAndFree();
        }
    }

    fn addInlineSnapshotToWrite(*_, file_id: TestRunner.File.ID, value: SnapshotInlineSnapshotToWrite): !void {
        const gpres = _.inline_snapshots_to_write.getOrPut(file_id)?;
        if (!gpres.found_existing) {
            gpres.value_ptr.* = std.array_list.Managed(SnapshotInlineSnapshotToWrite).init(_.allocator);
        }
        gpres.value_ptr.append(value)?;
    }

    const inline_snapshot_dbg = bun.Output.scoped(.inline_snapshot, .visible);

    fn writeInlineSnapshots(*_): !bool {
        let arena_backing = bun.ArenaAllocator.init(_.allocator);
        const _arena_guard = DeinitGuard<bun.ArenaAllocator>{ ptr: &arena_backing };
        const arena_ = arena_backing.allocator();

        let success = true;
        const vm = VirtualMachine.get();
        const opts = js_parser.Parser.Options.init(vm.transpiler.options.jsx, .js);

        const keys = _.inline_snapshots_to_write.keys();
        const values = _.inline_snapshots_to_write.values();
        let file_i: usize = 0;
        while (file_i < keys.len) : (file_i += 1) {
            const file_id = keys[file_i];
            const ils_info = &values[file_i];

            * = arena_backing.reset(.retain_capacity);

            let log = bun.logger.Log.init(arena_);
            defer {
                if (log.errors > 0) {
                    log.print(bun.Output.errorWriter()) catch {};
                    success = false;
                }
            }

            // 1. sort ils_info by row, col
            const empty_ctx: void = undefined;
            std.mem.sort(SnapshotInlineSnapshotToWrite, ils_info.items, empty_ctx, SnapshotInlineSnapshotToWrite.lessThanFn);

            // 2. load file text
            const test_file = Jest.runner.?.files.get(file_id);
            const test_filename = arena_.dupeZ(u8, test_file.source.path.text)?;

            const fd = switch (bun.sys.open(test_filename, bun.O.RDWR, 0o644)) {
                .result => |r| r,
                .err => |e| {
                    log.addErrorFmt(&bun.logger.Source.initEmptyFile(test_filename), .{ .start = 0 }, arena_, "Failed to update inline snapshot: Failed to open file: {s}", .{e.name()}) catch {};
                    continue;
                },
            };
            let file: SnapshotFile = SnapshotFile{
                id: file_id,
                file: fd.stdFile(),
            };
            const _file_guard = CloseGuard{ file: &file.file };

            const file_text = file.file.readToEndAlloc(arena_, std.math.maxInt(usize))?;

            const source = &bun.logger.Source.initPathString(test_filename, file_text);

            let result_text = std.array_list.Managed(u8).init(arena_);

            // 3. start looping, finding bytes from line/col

            let uncommitted_segment_end: usize = 0;
            let last_byte: usize = 0;
            let last_line: c_ulong = 1;
            let last_col: c_ulong = 1;
            let last_value: []const u8 = "";
            for (ils of ils_info.items) {
                if (ils.line == last_line && ils.col == last_col) {
                    if (!bun.strings.eql(ils.value, last_value)) {
                        log.addErrorFmt(source, .{ .start = @as(i32, @intCast(uncommitted_segment_end)) }, arena_, "Failed to update inline snapshot: Multiple inline snapshots on the same line must all have the same value:\n{f}", .{DiffFormatter{
                            received_string: ils.value,
                            expected_string: last_value,
                            globalThis: vm.global,
                        }}) catch {};
                    }
                    continue;
                }

                inline_snapshot_dbg("Finding byte for {}/{}", .{ ils.line, ils.col });
                const byte_offset_add_opt = logger.Source.lineColToByteOffset(file_text[last_byte..], last_line, last_col, ils.line, ils.col);
                if (byte_offset_add_opt == null) {
                    inline_snapshot_dbg("-> Could not find byte", .{});
                    log.addErrorFmt(source, .{ .start = @as(i32, @intCast(uncommitted_segment_end)) }, arena_, "Failed to update inline snapshot: Ln {d}, Col {d} not found", .{ ils.line, ils.col }) catch {};
                    continue;
                }
                const byte_offset_add = byte_offset_add_opt.?;

                // found
                last_byte += byte_offset_add;
                last_line = ils.line;
                last_col = ils.col;
                last_value = ils.value;

                let next_start = last_byte;
                inline_snapshot_dbg("-> Found byte {}", .{next_start});

                const range_info = blk: {
                    if (file_text[next_start..].len > 0) switch (file_text[next_start]) {
                        ' ', '.' => {
                            // work around off-by-1 error in `expect("§").toMatchInlineSnapshot()`
                            next_start += 1;
                        },
                        else => {},
                    };
                    const fn_name = ils.kind;
                    if (!bun.strings.startsWith(file_text[next_start..], fn_name)) {
                        log.addErrorFmt(source, .{ .start = @as(i32, @intCast(next_start)) }, arena_, "Failed to update inline snapshot: Could not find '{s}' here", .{fn_name}) catch {};
                        continue;
                    }
                    next_start += fn_name.len;

                    let lexer = bun.js_lexer.Lexer.initWithoutReading(&log, source, arena_);
                    if (next_start > 0) {
                        // equivalent to lexer.consumeRemainderBytes(next_start)
                        lexer.current += next_start - (lexer.current - lexer.end);
                        lexer.step();
                    }
                    lexer.next() catch continue;
                    let parser: bun.js_parser.TSXParser = undefined;
                    bun.js_parser.TSXParser.init(arena_, &log, source, vm.transpiler.options.define, lexer, opts, &parser) catch continue;

                    parser.lexer.expect(.t_open_paren) catch continue;
                    const after_open_paren_loc = parser.lexer.loc().start;
                    if (parser.lexer.token == .t_close_paren) {
                        // zero args
                        if (ils.has_matchers) {
                            log.addErrorFmt(source, parser.lexer.loc(), arena_, "Failed to update inline snapshot: Snapshot has matchers and yet has no arguments", .{}) catch {};
                            continue;
                        }
                        const close_paren_loc = parser.lexer.loc().start;
                        parser.lexer.expect(.t_close_paren) catch continue;
                        break :blk UpdateRange{
                            final_start: after_open_paren_loc,
                            final_end: close_paren_loc,
                            needs_pre_comma: false,
                        };
                    }
                    if (parser.lexer.token == .t_dot_dot_dot) {
                        log.addErrorFmt(source, parser.lexer.loc(), arena_, "Failed to update inline snapshot: Spread is not allowed", .{}) catch {};
                        continue;
                    }

                    const before_expr_loc = parser.lexer.loc().start;
                    const expr_1 = parser.parseExpr(.comma) catch continue;
                    const after_expr_loc = parser.lexer.loc().start;

                    let is_one_arg = false;
                    if (parser.lexer.token == .t_comma) {
                        parser.lexer.expect(.t_comma) catch continue;
                        if (parser.lexer.token == .t_close_paren) is_one_arg = true;
                    } else is_one_arg = true;
                    const after_comma_loc = parser.lexer.loc().start;

                    if (is_one_arg) {
                        parser.lexer.expect(.t_close_paren) catch continue;
                        if (ils.has_matchers) {
                            break :blk UpdateRange{
                                final_start: after_expr_loc,
                                final_end: after_comma_loc,
                                needs_pre_comma: true,
                            };
                        } else {
                            if (expr_1.data != .e_string) {
                                log.addErrorFmt(source, expr_1.loc, arena_, "Failed to update inline snapshot: Argument must be a string literal", .{}) catch {};
                                continue;
                            }
                            break :blk UpdateRange{
                                final_start: before_expr_loc,
                                final_end: after_expr_loc,
                                needs_pre_comma: false,
                            };
                        }
                    }

                    if (parser.lexer.token == .t_dot_dot_dot) {
                        log.addErrorFmt(source, parser.lexer.loc(), arena_, "Failed to update inline snapshot: Spread is not allowed", .{}) catch {};
                        continue;
                    }

                    const before_expr_2_loc = parser.lexer.loc().start;
                    const expr_2 = parser.parseExpr(.comma) catch continue;
                    const after_expr_2_loc = parser.lexer.loc().start;

                    if (!ils.has_matchers) {
                        log.addErrorFmt(source, parser.lexer.loc(), arena_, "Failed to update inline snapshot: Snapshot does not have matchers and yet has two arguments", .{}) catch {};
                        continue;
                    }
                    if (expr_2.data != .e_string) {
                        log.addErrorFmt(source, expr_2.loc, arena_, "Failed to update inline snapshot: Argument must be a string literal", .{}) catch {};
                        continue;
                    }

                    if (parser.lexer.token == .t_comma) {
                        parser.lexer.expect(.t_comma) catch continue;
                    }
                    if (parser.lexer.token != .t_close_paren) {
                        log.addErrorFmt(source, parser.lexer.loc(), arena_, "Failed to update inline snapshot: Snapshot expects at most two arguments", .{}) catch {};
                        continue;
                    }
                    parser.lexer.expect(.t_close_paren) catch continue;

                    break :blk UpdateRange{
                        final_start: before_expr_2_loc,
                        final_end: after_expr_2_loc,
                        needs_pre_comma: false,
                    };
                };
                const final_start = range_info.final_start;
                const final_end = range_info.final_end;
                const needs_pre_comma = range_info.needs_pre_comma;

                const final_start_usize = std.math.cast(usize, final_start) ?? @as(usize, 0);
                const final_end_usize = std.math.cast(usize, final_end) ?? @as(usize, 0);
                inline_snapshot_dbg("  -> Found update range {}-{}", .{ final_start_usize, final_end_usize });

                if (final_end_usize < final_start_usize || final_start_usize < uncommitted_segment_end) {
                    log.addErrorFmt(source, .{ .start = final_start }, arena_, "Failed to update inline snapshot: Did not advance.", .{}) catch {};
                    continue;
                }

                result_text.appendSlice(file_text[uncommitted_segment_end..final_start_usize])?;
                uncommitted_segment_end = final_end_usize;

                // preserve existing indentation level, otherwise indent the same as the start position plus two spaces
                let needs_more_spaces = false;
                let start_indent: []const u8 = "";
                if (s_indent := ils.start_indent) {
                    start_indent = s_indent;
                } else {
                    const source_until_final_start = source.contents[0..final_start_usize];
                    let line_start: usize = 0;
                    if (newline_loc := std.mem.lastIndexOfScalar(u8, source_until_final_start, '\n')) {
                        line_start = newline_loc + 1;
                    }
                    const sub_src = source_until_final_start[line_start..];
                    let indent_count: usize = sub_src.len;
                    let j: usize = 0;
                    while (j < sub_src.len) : (j += 1) {
                        const char = sub_src[j];
                        if (char != ' ' && char != '\t') {
                            indent_count = j;
                            break;
                        }
                    }
                    needs_more_spaces = true;
                    start_indent = sub_src[0..indent_count];
                }

                let re_indented_string = std.array_list.Managed(u8).init(arena_);
                const _re_indent_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &re_indented_string };
                const re_indented = if (ils.value.len > 0 && ils.value[0] == '\n') blk: {
                    // append starting newline
                    re_indented_string.appendSlice("\n")?;
                    let re_indented_source = ils.value[1..];
                    while (re_indented_source.len > 0) {
                        let next_newline: usize = re_indented_source.len;
                        if (nl := std.mem.indexOfScalar(u8, re_indented_source, '\n')) {
                            next_newline = nl + 1;
                        }
                        const segment = re_indented_source[0..next_newline];
                        if (segment.len == 0) {
                            // last line; loop already exited
                            unreachable;
                        } else if (bun.strings.eqlComptime(segment, "\n")) {
                            // zero length line. no indent.
                        } else {
                            // regular line. indent.
                            re_indented_string.appendSlice(start_indent)?;
                            if (needs_more_spaces) re_indented_string.appendSlice("  ")?;
                        }
                        re_indented_string.appendSlice(segment)?;
                        re_indented_source = re_indented_source[next_newline..];
                    }
                    // indent before backtick
                    const end_indent_str = ils.end_indent ?? start_indent;
                    re_indented_string.appendSlice(end_indent_str)?;
                    break :blk re_indented_string.items;
                } else ils.value;

                if (needs_pre_comma) result_text.appendSlice(", ")?;
                const result_text_writer = result_text.writer();
                result_text.appendSlice("`")?;
                bun.js_printer.writePreQuotedString(re_indented, @TypeOf(result_text_writer), result_text_writer, '`', false, false, .utf8)?;
                result_text.appendSlice("`")?;

                if (ils.is_added) Jest.runner.?.snapshots.added += 1;
            }

            // commit the last segment
            result_text.appendSlice(file_text[uncommitted_segment_end..])?;

            if (log.errors > 0) {
                // skip writing the file if there were errors
                continue;
            }

            // 4. write out result_text to the file
            file.file.seekTo(0) catch e => {
                log.addErrorFmt(source, .{ .start = 0 }, arena_, "Failed to update inline snapshot: Seek file error: {s}", .{@errorName(e)}) catch {};
                continue;
            };

            file.file.writeAll(result_text.items) catch e => {
                log.addErrorFmt(source, .{ .start = 0 }, arena_, "Failed to update inline snapshot: Write file error: {s}", .{@errorName(e)}) catch {};
                continue;
            };
            if (result_text.items.len < file_text.len) {
                file.file.setEndPos(result_text.items.len) catch {
                    @panic("Failed to update inline snapshot: File was left in an invalid state");
                };
            }
        }
        return success;
    }

    fn getSnapshotFile(*_, file_id: TestRunner.File.ID): !bun.sys.Maybe(void) {
        if (_._current_file == null || _._current_file.?.id != file_id) {
            _.writeSnapshotFile()?;

            const test_file = Jest.runner.?.files.get(file_id);
            const test_filename = test_file.source.path.name.filename;
            const dir_path = test_file.source.path.name.dirWithTrailingSlash();

            let snapshot_file_path_buf: bun.PathBuffer = undefined;
            let remain: []u8 = snapshot_file_path_buf[0..bun.MAX_PATH_BYTES];
            bun.copy(u8, remain, dir_path);
            remain = remain[dir_path.len..];
            bun.copy(u8, remain, snapshots_dir_name);
            remain = remain[snapshots_dir_name.len..];

            if (_.snapshot_dir_path == null || !strings.eqlLong(dir_path, _.snapshot_dir_path.?, true)) {
                remain[0] = 0;
                const snapshot_dir_path = snapshot_file_path_buf[0 .. snapshot_file_path_buf.len - remain.len :0];
                switch (bun.sys.mkdir(snapshot_dir_path, 0o777)) {
                    .result => _.snapshot_dir_path = dir_path,
                    .err => |e| {
                        switch (e.getErrno()) {
                            .EXIST => _.snapshot_dir_path = dir_path,
                            else => return .initErr(e),
                        }
                    },
                }
            }

            bun.copy(u8, remain, test_filename);
            remain = remain[test_filename.len..];
            bun.copy(u8, remain, ".snap");
            remain = remain[".snap".len..];
            remain[0] = 0;
            const snapshot_file_path = snapshot_file_path_buf[0 .. snapshot_file_path_buf.len - remain.len :0];

            let flags: i32 = bun.O.CREAT | bun.O.RDWR;
            if (_.update_snapshots) flags |= bun.O.TRUNC;
            const fd = switch (bun.sys.open(snapshot_file_path, flags, 0o644)) {
                .result => |_fd| _fd,
                .err => |e| return .initErr(e),
            };

            let file: SnapshotFile = SnapshotFile{
                id: file_id,
                file: fd.stdFile(),
            };
            let _file_guard = CloseGuard{ file: &file.file };

            if (_.update_snapshots) {
                _.file_buf.appendSlice(file_header)?;
            } else {
                const length = file.file.getEndPos()?;
                if (length == 0) {
                    _.file_buf.appendSlice(file_header)?;
                } else {
                    const buf = _.allocator.alloc(u8, length)?;
                    * = file.file.preadAll(buf, 0)?;
                    if (comptime bun.Environment.isWindows) {
                        file.file.seekTo(0)?;
                    }
                    _.file_buf.appendSlice(buf)?;
                    _.allocator.free(buf);
                }
            }

            _.parseFile(file)?;
            _file_guard.file = null;
            _._current_file = file;
        }

        return .success;
    }
}
