import std from "std";
import bun from "bun";
import types from "./xml_types.zig";
import xml_index from "./xml_index.zig";
import scanner_mod from "./xml_scanner.zig";

const js_ast = bun.ast;
const E = js_ast.E;
const Expr = js_ast.Expr;
const G = js_ast.G;
const Loc = bun.logger.Loc;

pub const Options = types.Options;
pub const InputEncoding = types.InputEncoding;
pub const XMLError = types.XMLError;
pub const isXmlChar = types.isXmlChar;
pub const isNameStartChar = types.isNameStartChar;
pub const isNameChar = types.isNameChar;

const STOP_CONTENT: u8 = xml_index.CLASS_LT | xml_index.CLASS_GT | xml_index.CLASS_ALWAYS;
const KEY_CACHE_SIZE: usize = 64;
const LINEAR_CHILD_LIMIT: usize = 16;
const LINEAR_ATTRIBUTE_LIMIT: usize = 8;
const MAX_DEPTH: usize = 100_000;

fn keyCacheSlot(bytes: []const u8): usize {
    const n = bytes.len;
    if (n == 0) return 0;
    const h = (n *% 31) ^
        (@as(usize, bytes[0]) *% 7) ^
        @as(usize, bytes[n - 1]) ^
        (@as(usize, bytes[n / 2]) << 2);
    return h % KEY_CACHE_SIZE;
}

fn SinkTypes(comptime U: type): type {
    const is_wide = U == u16;
    const UT = types.UnitTypes(U);

    return struct {
        const Slice = []const U;

        static fn makeStrExpr(units: Slice, loc: Loc): Expr {
            if (comptime is_wide) {
                return Expr.init(E.String, E.String{
                    data: std.mem.sliceAsBytes(units),
                    is_utf16: true,
                }, loc);
            }
            return Expr.init(E.String, E.String.init(units), loc);
        }

        static fn makeAsciiStrExpr(lit: string, loc: Loc, allocator: std.mem.Allocator): Expr {
            if (comptime is_wide) {
                let buf = allocator.alloc(u16, lit.len) catch return Expr.init(E.String, E.String.init(""), loc);
                let i: usize = 0;
                while (i < lit.len) : (i += 1) {
                    buf[i] = @as(u16, lit[i]);
                }
                return makeStrExpr(buf, loc);
            }
            return Expr.init(E.String, E.String.init(lit), loc);
        }

        static fn propKeyBytes(prop: G.Property): []const u8 {
            if (k := prop.key) {
                if (k.data == .e_string) {
                    return k.data.e_string.data;
                }
            }
            return "";
        }

        static fn makeObjectExpr(allocator: std.mem.Allocator, props: *std.ArrayListUnmanaged(G.Property), mark: usize, loc: Loc): XMLError!Expr {
            const slice = props.items[mark..];
            let list = std.array_list.Managed(G.Property).init(allocator);
            list.ensureTotalCapacity(slice.len) catch return error.OutOfMemory;
            list.appendSliceAssumeCapacity(slice);
            props.items.len = mark;
            return Expr.init(E.Object, E.Object{
                properties: bun.BabyList(G.Property).moveFromList(&list),
            }, loc);
        }

        static fn makeArrayExpr(allocator: std.mem.Allocator, items: []const Expr, loc: Loc): XMLError!Expr {
            let list = std.array_list.Managed(Expr).init(allocator);
            list.ensureTotalCapacity(items.len) catch return error.OutOfMemory;
            list.appendSliceAssumeCapacity(items);
            return Expr.init(E.Array, E.Array{
                items: bun.BabyList(Expr).moveFromList(&list),
            }, loc);
        }

        const CompactFrame = struct {
            name: Slice,
            loc: Loc,
            attribute_count: u32,
            props_mark: u32,
            text_mark: u32,
            text_prop: u32,
        };

        const TextRun = struct {
            slice: Slice,
            ws_only: bool,
        };

        static fn isWsOnly(text: Slice): bool {
            for (u of text) {
                if (!types.isWs(UT.unitLow(u))) return false;
            }
            return true;
        }

        const Group = struct {
            first: u32,
            count: u32,
            cursor: u32,
        };

        const CompactSink = struct {
            allocator: std.mem.Allocator,
            props: std.ArrayListUnmanaged(G.Property),
            stack: std.ArrayListUnmanaged(CompactFrame),
            text_runs: std.ArrayListUnmanaged(TextRun),
            key_cache: [KEY_CACHE_SIZE]([]const u8)?,
            group_of: std.ArrayListUnmanaged(u32),
            groups: std.ArrayListUnmanaged(Group),
            gathered: std.ArrayListUnmanaged(Expr),
            group_index: std.StringHashMapUnmanaged(u32),
            root_name: Slice?,
            root_value: Expr?,
            root_loc: Loc,

            static fn init(allocator: std.mem.Allocator): CompactSink {
                let key_cache: [KEY_CACHE_SIZE]([]const u8)? = @splat(null);
                return CompactSink{
                    allocator: allocator,
                    props: .{},
                    stack: .{},
                    text_runs: .{},
                    key_cache: key_cache,
                    group_of: .{},
                    groups: .{},
                    gathered: .{},
                    group_index: .{},
                    root_name: null,
                    root_value: null,
                    root_loc: Loc.Empty,
                };
            }

            ~init() {
                _.props.deinit(_.allocator);
                _.stack.deinit(_.allocator);
                _.text_runs.deinit(_.allocator);
                _.group_of.deinit(_.allocator);
                _.groups.deinit(_.allocator);
                _.gathered.deinit(_.allocator);
                _.group_index.deinit(_.allocator);
            }

            fn concatText(*_, mark: usize, skip_ws_only: bool): XMLError!Slice {
                const runs = _.text_runs.items[mark..];
                let first_slice: Slice? = null;
                let second_slice: Slice? = null;
                let total_len: usize = 0;
                for (r of runs) {
                    if (skip_ws_only && r.ws_only) continue;
                    total_len += r.slice.len;
                    if (first_slice == null) {
                        first_slice = r.slice;
                    } else if (second_slice == null) {
                        second_slice = r.slice;
                    }
                }
                if (first_slice == null) return [];
                if (second_slice == null) return first_slice.?;
                let buf = _.allocator.alloc(U, total_len) catch return error.OutOfMemory;
                let off: usize = 0;
                for (r of runs) {
                    if (skip_ws_only && r.ws_only) continue;
                    std.mem.copyForwards(U, buf[off .. off + r.slice.len], r.slice);
                    off += r.slice.len;
                }
                return buf;
            }

            fn foldRepeats(*_, mark: usize): XMLError!void {
                const children = _.props.items[mark..];
                const n = children.len;
                if (n > 8) {
                    let seen: u64 = 0;
                    let collided = false;
                    for (child of children) {
                        const key = propKeyBytes(child);
                        if (key.len > 0) {
                            const h = key.len ^ (@as(usize, key[0]) << 1) ^ (@as(usize, key[key.len - 1]) << 3);
                            const bit: u64 = @as(u64, 1) << @as(u6, @intCast(h & 63));
                            collided = collided || ((seen & bit) != 0);
                            seen |= bit;
                        }
                    }
                    if (!collided) return;
                }
                if (n <= LINEAR_CHILD_LIMIT) {
                    let repeated = false;
                    let i: usize = 1;
                    outer: while (i < n) : (i += 1) {
                        const name = propKeyBytes(children[i]);
                        let j: usize = 0;
                        while (j < i) : (j += 1) {
                            if (std.mem.eql(u8, propKeyBytes(children[j]), name)) {
                                repeated = true;
                                break :outer;
                            }
                        }
                    }
                    if (!repeated) return;
                }
                return _.foldRepeatsSlow(mark);
            }

            fn foldRepeatsSlow(*_, mark: usize): XMLError!void {
                const children = _.props.items[mark..];
                const n = children.len;
                _.groups.items.len = 0;
                _.group_of.items.len = 0;
                if (n <= LINEAR_CHILD_LIMIT) {
                    let i: usize = 0;
                    next_child: while (i < n) : (i += 1) {
                        const name = propKeyBytes(children[i]);
                        let g: usize = 0;
                        while (g < _.groups.items.len) : (g += 1) {
                            const grp = &_.groups.items[g];
                            if (std.mem.eql(u8, propKeyBytes(children[grp.first]), name)) {
                                grp.count += 1;
                                _.group_of.append(_.allocator, @as(u32, @intCast(g))) catch return error.OutOfMemory;
                                continue :next_child;
                            }
                        }
                        _.group_of.append(_.allocator, @as(u32, @intCast(_.groups.items.len))) catch return error.OutOfMemory;
                        _.groups.append(_.allocator, Group{
                            first: @as(u32, @intCast(i)),
                            count: 1,
                            cursor: 0,
                        }) catch return error.OutOfMemory;
                    }
                } else {
                    _.group_index.clearRetainingCapacity();
                    let i: usize = 0;
                    while (i < n) : (i += 1) {
                        const key = propKeyBytes(children[i]);
                        const gop = _.group_index.getOrPut(_.allocator, key) catch return error.OutOfMemory;
                        if (!gop.found_existing) {
                            const g_idx = @as(u32, @intCast(_.groups.items.len));
                            gop.value_ptr.* = g_idx;
                            _.groups.append(_.allocator, Group{
                                first: @as(u32, @intCast(i)),
                                count: 0,
                                cursor: 0,
                            }) catch return error.OutOfMemory;
                        }
                        const g = gop.value_ptr.*;
                        _.groups.items[g].count += 1;
                        _.group_of.append(_.allocator, g) catch return error.OutOfMemory;
                    }
                }
                if (_.groups.items.len == n) return;

                let next_cur: u32 = 0;
                for (*grp of _.groups.items) {
                    grp.cursor = next_cur;
                    if (grp.count > 1) next_cur += grp.count;
                }
                _.gathered.resize(_.allocator, next_cur) catch return error.OutOfMemory;
                let i: usize = 0;
                while (i < _.group_of.items.len) : (i += 1) {
                    const g = _.group_of.items[i];
                    const grp = &_.groups.items[g];
                    if (grp.count > 1) {
                        _.gathered.items[grp.cursor] = children[i].value.?;
                        grp.cursor += 1;
                    }
                }
                let slot: usize = 0;
                while (slot < _.groups.items.len) : (slot += 1) {
                    const grp = _.groups.items[slot];
                    let prop = _.props.items[mark + grp.first];
                    if (grp.count > 1) {
                        const start = grp.cursor - grp.count;
                        const loc = if (k := prop.key) k.loc else Loc.Empty;
                        prop.value = makeArrayExpr(_.allocator, _.gathered.items[start..grp.cursor], loc)?;
                    }
                    _.props.items[mark + slot] = prop;
                }
                _.props.items.len = mark + _.groups.items.len;
            }

            fn beginElement(*_, name: Slice, loc: Loc): XMLError!void {
                _.stack.append(_.allocator, CompactFrame{
                    name: name,
                    loc: loc,
                    attribute_count: 0,
                    props_mark: @as(u32, @intCast(_.props.items.len)),
                    text_mark: @as(u32, @intCast(_.text_runs.items.len)),
                    text_prop: std.math.maxInt(u32),
                }) catch return error.OutOfMemory;
            }

            fn attribute(*_, name: Slice, value: Slice): XMLError!void {
                const frame = &_.stack.items[_.stack.items.len - 1];
                frame.attribute_count += 1;
                const loc = frame.loc;
                const name_bytes = UT.unitBytes(name);
                const unit_sz = @sizeOf(U);
                const slot = keyCacheSlot(name_bytes);
                let key_bytes: []const u8 = "";
                let hit = false;
                if (cached := _.key_cache[slot]) {
                    if (cached.len == unit_sz + name_bytes.len && std.mem.eql(u8, cached[unit_sz..], name_bytes)) {
                        key_bytes = cached;
                        hit = true;
                    }
                }
                if (!hit) {
                    let joined = _.allocator.alloc(U, name.len + 1) catch return error.OutOfMemory;
                    joined[0] = UT.unitAscii('@');
                    std.mem.copyForwards(U, joined[1..], name);
                    key_bytes = UT.unitBytes(joined);
                    _.key_cache[slot] = key_bytes;
                }
                const key_expr = Expr.init(E.String, E.String{
                    data: key_bytes,
                    is_utf16: is_wide,
                }, loc);
                _.props.append(_.allocator, G.Property{
                    key: key_expr,
                    value: makeStrExpr(value, loc),
                }) catch return error.OutOfMemory;
            }

            fn text(*_, txt: Slice, _loc: Loc): XMLError!void {
                * = _loc;
                const ws_only = isWsOnly(txt);
                _.text_runs.append(_.allocator, TextRun{ slice: txt, ws_only: ws_only }) catch return error.OutOfMemory;
                if (!ws_only) {
                    const frame = &_.stack.items[_.stack.items.len - 1];
                    if (frame.text_prop == std.math.maxInt(u32)) {
                        frame.text_prop = @as(u32, @intCast(_.props.items.len));
                        const empty_null: E.Null = undefined;
                        _.props.append(_.allocator, G.Property{
                            key: makeAsciiStrExpr("#text", frame.loc, _.allocator),
                            value: Expr.init(E.Null, empty_null, frame.loc),
                        }) catch return error.OutOfMemory;
                    }
                }
            }

            fn comment(*_, _txt: Slice, _loc: Loc): XMLError!void {
                * = _txt;
                * = _loc;
            }

            fn pi(*_, _target: Slice, _data: Slice, _loc: Loc): XMLError!void {
                * = _target;
                * = _data;
                * = _loc;
            }

            fn leafValue(*_, frame: CompactFrame, txt: Slice): XMLError!Expr {
                if (frame.attribute_count == 0) {
                    return makeStrExpr(txt, frame.loc);
                }
                if (txt.len > 0) {
                    _.props.append(_.allocator, G.Property{
                        key: makeAsciiStrExpr("#text", frame.loc, _.allocator),
                        value: makeStrExpr(txt, frame.loc),
                    }) catch return error.OutOfMemory;
                }
                return makeObjectExpr(_.allocator, &_.props, frame.props_mark, frame.loc);
            }

            fn deliver(*_, name: Slice, val: Expr, loc: Loc): XMLError!void {
                if (_.stack.items.len > 0) {
                    _.props.append(_.allocator, G.Property{
                        key: makeStrExpr(name, loc),
                        value: val,
                    }) catch return error.OutOfMemory;
                } else {
                    _.root_name = name;
                    _.root_value = val;
                    _.root_loc = loc;
                }
            }

            fn endLeaf(*_, txt: Slice, _loc: Loc): XMLError!void {
                * = _loc;
                const frame = _.stack.pop() orelse unreachable;
                const val = _.leafValue(frame, txt)?;
                return _.deliver(frame.name, val, frame.loc);
            }

            fn endElement(*_): XMLError!void {
                const frame = _.stack.pop() orelse unreachable;
                const props_mark = @as(usize, frame.props_mark);
                const children_mark = props_mark + @as(usize, frame.attribute_count);
                const has_text = frame.text_prop != std.math.maxInt(u32);
                const has_elements = _.props.items.len > children_mark + @intFromBool(has_text);
                let val: Expr = undefined;
                if (!has_elements) {
                    const txt = _.concatText(frame.text_mark, false)?;
                    _.props.items.len = children_mark;
                    val = _.leafValue(frame, txt)?;
                } else {
                    if (has_text) {
                        const txt = _.concatText(frame.text_mark, true)?;
                        _.props.items[frame.text_prop].value = makeStrExpr(txt, frame.loc);
                    }
                    if (_.props.items.len > children_mark + 1) {
                        _.foldRepeats(children_mark)?;
                    }
                    val = makeObjectExpr(_.allocator, &_.props, props_mark, frame.loc)?;
                }
                _.text_runs.items.len = frame.text_mark;
                return _.deliver(frame.name, val, frame.loc);
            }

            fn finish(*_): XMLError!Expr {
                const name = _.root_name orelse unreachable;
                const val = _.root_value orelse unreachable;
                const mark = _.props.items.len;
                _.props.append(_.allocator, G.Property{
                    key: makeStrExpr(name, _.root_loc),
                    value: val,
                }) catch return error.OutOfMemory;
                return makeObjectExpr(_.allocator, &_.props, mark, _.root_loc);
            }
        };

        const NodeFrame = struct {
            name: Slice,
            loc: Loc,
            props_mark: u32,
            children_mark: u32,
        };

        const NodeSink = struct {
            allocator: std.mem.Allocator,
            props: std.ArrayListUnmanaged(G.Property),
            items: std.ArrayListUnmanaged(Expr),
            stack: std.ArrayListUnmanaged(NodeFrame),
            root: Expr?,

            static fn init(allocator: std.mem.Allocator): NodeSink {
                return NodeSink{
                    allocator: allocator,
                    props: .{},
                    items: .{},
                    stack: .{},
                    root: null,
                };
            }

            ~init() {
                _.props.deinit(_.allocator);
                _.items.deinit(_.allocator);
                _.stack.deinit(_.allocator);
            }

            fn beginElement(*_, name: Slice, loc: Loc): XMLError!void {
                _.stack.append(_.allocator, NodeFrame{
                    name: name,
                    loc: loc,
                    props_mark: @as(u32, @intCast(_.props.items.len)),
                    children_mark: @as(u32, @intCast(_.items.items.len)),
                }) catch return error.OutOfMemory;
            }

            fn attribute(*_, name: Slice, value: Slice): XMLError!void {
                const loc = _.stack.items[_.stack.items.len - 1].loc;
                _.props.append(_.allocator, G.Property{
                    key: makeStrExpr(name, loc),
                    value: makeStrExpr(value, loc),
                }) catch return error.OutOfMemory;
            }

            fn text(*_, txt: Slice, loc: Loc): XMLError!void {
                _.items.append(_.allocator, makeStrExpr(txt, loc)) catch return error.OutOfMemory;
            }

            fn comment(*_, txt: Slice, loc: Loc): XMLError!void {
                const mark = _.props.items.len;
                _.props.append(_.allocator, G.Property{
                    key: makeAsciiStrExpr("comment", loc, _.allocator),
                    value: makeStrExpr(txt, loc),
                }) catch return error.OutOfMemory;
                const node = makeObjectExpr(_.allocator, &_.props, mark, loc)?;
                _.items.append(_.allocator, node) catch return error.OutOfMemory;
            }

            fn pi(*_, target: Slice, data: Slice, loc: Loc): XMLError!void {
                const mark = _.props.items.len;
                _.props.append(_.allocator, G.Property{
                    key: makeAsciiStrExpr("target", loc, _.allocator),
                    value: makeStrExpr(target, loc),
                }) catch return error.OutOfMemory;
                _.props.append(_.allocator, G.Property{
                    key: makeAsciiStrExpr("data", loc, _.allocator),
                    value: makeStrExpr(data, loc),
                }) catch return error.OutOfMemory;
                const node = makeObjectExpr(_.allocator, &_.props, mark, loc)?;
                _.items.append(_.allocator, node) catch return error.OutOfMemory;
            }

            fn endLeaf(*_, txt: Slice, loc: Loc): XMLError!void {
                if (txt.len > 0) {
                    _.items.append(_.allocator, makeStrExpr(txt, loc)) catch return error.OutOfMemory;
                }
                return _.endElement();
            }

            fn endElement(*_): XMLError!void {
                const frame = _.stack.pop() orelse unreachable;
                const attributes = makeObjectExpr(_.allocator, &_.props, frame.props_mark, frame.loc)?;
                const children_slice = _.items.items[frame.children_mark..];
                const children = makeArrayExpr(_.allocator, children_slice, frame.loc)?;
                _.items.items.len = frame.children_mark;

                const mark = _.props.items.len;
                _.props.append(_.allocator, G.Property{
                    key: makeAsciiStrExpr("name", frame.loc, _.allocator),
                    value: makeStrExpr(frame.name, frame.loc),
                }) catch return error.OutOfMemory;
                _.props.append(_.allocator, G.Property{
                    key: makeAsciiStrExpr("attributes", frame.loc, _.allocator),
                    value: attributes,
                }) catch return error.OutOfMemory;
                _.props.append(_.allocator, G.Property{
                    key: makeAsciiStrExpr("children", frame.loc, _.allocator),
                    value: children,
                }) catch return error.OutOfMemory;
                const node = makeObjectExpr(_.allocator, &_.props, mark, frame.loc)?;
                if (_.stack.items.len > 0) {
                    _.items.append(_.allocator, node) catch return error.OutOfMemory;
                } else {
                    _.root = node;
                }
            }

            fn finish(*_): XMLError!Expr {
                return _.root orelse unreachable;
            }
        };
    };
}

fn ParserType(comptime U: type, comptime Sink: type): type {
    const is_wide = U == u16;
    const UT = types.UnitTypes(U);
    const ST = scanner_mod.ScannerTypes(U);
    const Scanner = ST.Scanner;
    const Kind = ST.Kind;
    const Slice = []const U;

    return struct {
        const Self = @This();

        const AttDef = struct {
            name: Slice,
            cdata: bool,
            default: Slice?,
        };

        const AttList = struct {
            defs: std.ArrayListUnmanaged(AttDef),
            by_name: std.HashMapUnmanaged(Slice, u32, ST.SliceCtx, 80),

            static fn init(): AttList {
                return AttList{ defs: .{}, by_name: .{} };
            }

            fn deinit(*_, allocator: std.mem.Allocator) {
                _.defs.deinit(allocator);
                _.by_name.deinit(allocator);
            }

            fn get(name: Slice): AttDef? {
                if (idx := _.by_name.get(name)) {
                    return _.defs.items[idx];
                }
                return null;
            }

            fn declare(*_, allocator: std.mem.Allocator, def: AttDef): XMLError!void {
                const gop = _.by_name.getOrPut(allocator, def.name) catch return error.OutOfMemory;
                if (!gop.found_existing) {
                    gop.value_ptr.* = @as(u32, @intCast(_.defs.items.len));
                    _.defs.append(allocator, def) catch return error.OutOfMemory;
                }
            }
        };

        const OpenElem = struct {
            name: Slice,
            frame: u32,
        };

        scanner: Scanner,
        in_dtd: bool,
        sink: Sink,
        attlists: std.HashMapUnmanaged(Slice, AttList, ST.SliceCtx, 80),
        open: std.ArrayListUnmanaged(OpenElem),
        attribute_first: [LINEAR_ATTRIBUTE_LIMIT]Slice,
        attribute_names: std.HashMapUnmanaged(Slice, void, ST.SliceCtx, 80),
        attribute_count: usize,
        depth: usize,

        static fn init(
            source: *const bun.logger.Source,
            contents: Slice,
            log: *bun.logger.Log,
            allocator: std.mem.Allocator,
            options: Options,
            move sink: Sink,
        ): Self {
            let sc = Scanner.init(allocator, source, contents, log, options.encoding, !options.compact);
            if (comptime is_wide) sc.transcoded = true;
            const empty_attrs: [LINEAR_ATTRIBUTE_LIMIT]Slice = @splat([]);
            return Self{
                scanner: sc,
                in_dtd: false,
                sink: sink,
                attlists: .{},
                open: .{},
                attribute_first: empty_attrs,
                attribute_names: .{},
                attribute_count: 0,
                depth: 0,
            };
        }

        ~init() {
            let it = _.attlists.valueIterator();
            while (al := it.next()) {
                al.deinit(_.scanner.allocator);
            }
            _.attlists.deinit(_.scanner.allocator);
            _.open.deinit(_.scanner.allocator);
            _.attribute_names.deinit(_.scanner.allocator);
            _.scanner.~init();
            _.sink.~init();
        }

        fn stackOverflow(*_): XMLError {
            if (_.scanner.errMsg(_.scanner.tok.pos, "Nesting is too deep") == error.OutOfMemory) {}
            return error.StackOverflow;
        }

        fn advance(*_): XMLError!void {
            return _.scanner.next(.None);
        }

        fn advanceLiteral(*_, literal: types.Literal): XMLError!void {
            return _.scanner.next(literal);
        }

        fn advanceContent(*_): XMLError!void {
            return _.scanner.nextContent();
        }

        fn describeKind(*_, kind: Kind): string {
            const alloc = _.scanner.allocator;
            return switch (kind) {
                .eof entity => if (e := entity) (std.fmt.allocPrint(alloc, "the end of entity '{s}'", .{_.scanner.formatUnits(e)}) catch "end of input") else "end of input",
                .name n, .nmtoken n => std.fmt.allocPrint(alloc, "'{s}'", .{_.scanner.formatUnits(n)}) catch "a name",
                .hash n => std.fmt.allocPrint(alloc, "'#{s}'", .{_.scanner.formatUnits(n)}) catch "a keyword",
                .pe_reference n => std.fmt.allocPrint(alloc, "'%{s};'", .{_.scanner.formatUnits(n)}) catch "a parameter entity reference",
                .percent => "'%'",
                .percent_name n => std.fmt.allocPrint(alloc, "'%{s}'", .{_.scanner.formatUnits(n)}) catch "'%'",
                .literal => "a quoted string",
                .eq => "'='",
                .gt => "'>'",
                .slash_gt => "'/>'",
                .paren_open => "'('",
                .paren_close => "')'",
                .bar => "'|'",
                .comma => "','",
                .question => "'?'",
                .star => "'*'",
                .plus => "'+'",
                .bracket_open => "'['",
                .bracket_close => "']'",
                .decl d => d.opener(),
                .xml_decl => "'<?xml'",
                .comment => "a comment",
                .pi => "a processing instruction",
                .start_tag n => std.fmt.allocPrint(alloc, "'<{s}'", .{_.scanner.formatUnits(n)}) catch "a start tag",
                .end_tag n => std.fmt.allocPrint(alloc, "'</{s}'", .{_.scanner.formatUnits(n)}) catch "an end tag",
                .text => "text",
                .unexpected cp => blk: {
                    if (cp >= 0x21 && cp <= 0x7E) {
                        break :blk std.fmt.allocPrint(alloc, "'{c}'", .{@as(u8, @intCast(cp))}) catch "an unexpected character";
                    }
                    if (cp >= 0x20 && cp != 0x7F && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                        let tmp: [4]u8 = [0, 0, 0, 0];
                        const n = bun.strings.encodeWTF8Rune(&tmp, @as(i32, @intCast(cp)));
                        break :blk std.fmt.allocPrint(alloc, "'{s}' (U+{X:0>4})", .{ tmp[0..n], cp }) catch "an unexpected character";
                    }
                    break :blk std.fmt.allocPrint(alloc, "U+{X:0>4}", .{cp}) catch "an unexpected character";
                },
            };
        }

        fn unexpected(*_, expected: string): XMLError {
            if (_.in_dtd && _.scanner.tok.kind == .pe_reference) {
                return _.scanner.errMsg(_.scanner.tok.pos, "Parameter entity references are not allowed inside markup declarations in the internal subset");
            }
            if (_.scanner.tok.kind == .percent_name) {
                return _.scanner.errNamed(_.scanner.tok.pos, "Expected ';' to end the parameter entity reference", _.scanner.tok.kind.percent_name, "");
            }
            const found = _.describeKind(_.scanner.tok.kind);
            return _.scanner.errFmt(_.scanner.tok.pos, "Expected {s} but found {s}", .{ expected, found });
        }

        fn requireSpaced(*_): XMLError!void {
            if (_.scanner.tok.spaced) return;
            const found = _.describeKind(_.scanner.tok.kind);
            return _.scanner.errFmt(_.scanner.tok.pos, "Whitespace is required before {s}", .{found});
        }

        fn expectName(*_, expected: string): XMLError!Slice {
            return switch (_.scanner.tok.kind) {
                .name name => name,
                else => _.unexpected(expected),
            };
        }

        fn expectGt(*_, expected: string): XMLError!u32 {
            return switch (_.scanner.tok.kind) {
                .gt => _.scanner.tok.frame,
                else => _.unexpected(expected),
            };
        }

        fn parseDocument(*_): XMLError!Expr {
            const validation_deferred = _.scanner.initDocument()?;
            if (!validation_deferred) {
                _.scanner.checkUtf16Declaration()?;
                _.scanner.validateUtf8()?;
                _.scanner.buildIndex();
            }
            _.advance()?;
            const has_xml_decl = _.scanner.tok.kind == .xml_decl;
            if (has_xml_decl) {
                _.parseXmlDecl()?;
            }
            if (validation_deferred) {
                _.scanner.checkUtf16Declaration()?;
                _.scanner.validateUtf8()?;
                _.scanner.buildIndex();
            }
            if (has_xml_decl) {
                _.advance()?;
            }

            let seen_doctype = false;
            while (true) {
                switch (_.scanner.tok.kind) {
                    .comment, .pi => {},
                    .decl d => {
                        if (d == .Doctype && !seen_doctype) {
                            seen_doctype = true;
                            _.parseDoctype()?;
                        } else if (d == .Doctype) {
                            return _.scanner.errMsg(_.scanner.tok.pos, "Only one document type declaration is allowed");
                        } else {
                            return _.scanner.errMsg(_.scanner.tok.pos, "Markup declarations and parameter-entity references are only allowed in the document type declaration");
                        }
                    },
                    .start_tag => break,
                    .eof => return _.scanner.errMsg(_.scanner.tok.pos, "XML document must have a root element"),
                    .pe_reference, .percent, .percent_name => return _.scanner.errMsg(_.scanner.tok.pos, "Markup declarations and parameter-entity references are only allowed in the document type declaration"),
                    else => return _.unexpected("the root element"),
                }
                _.advance()?;
            }

            _.parseTree()?;

            while (true) {
                _.advance()?;
                switch (_.scanner.tok.kind) {
                    .comment, .pi => {},
                    .eof => break,
                    .start_tag => return _.scanner.errMsg(_.scanner.tok.pos, "Only one root element is allowed"),
                    else => {
                        const found = _.describeKind(_.scanner.tok.kind);
                        return _.scanner.errFmt(_.scanner.tok.pos, "Unexpected {s} after the root element", .{found});
                    },
                }
            }
            return _.sink.finish();
        }

        fn parseXmlDecl(*_): XMLError!void {
            _.advance()?;
            const ver_attr = _.parsePseudoAttribute("version")?;
            if (ver_attr == null) {
                switch (_.scanner.tok.kind) {
                    .name n => {
                        if (UT.eqAscii(n, "encoding") || UT.eqAscii(n, "standalone")) {
                            return _.scanner.errMsg(_.scanner.tok.pos, "The XML declaration must start with version=\"1.0\"");
                        }
                        return _.unexpected("version=\"1.0\" in the XML declaration");
                    },
                    .question => return _.scanner.errMsg(_.scanner.tok.pos, "The XML declaration must specify the version"),
                    else => return _.unexpected("version=\"1.0\" in the XML declaration"),
                }
            }
            const version = ver_attr.?.val;
            const ver_pos = ver_attr.?.pos;
            let ver_ok = version.len >= 3 && UT.startsWithAscii(version, "1.");
            if (ver_ok) {
                for (u of version[2..]) {
                    if (!std.ascii.isDigit(UT.unitLow(u))) {
                        ver_ok = false;
                        break;
                    }
                }
            }
            if (!ver_ok) {
                return _.scanner.errNamed(ver_pos, "Unsupported XML version", version, " (this is an XML 1.0 parser)");
            }

            const encoding_attr = _.parsePseudoAttribute("encoding")?;
            if (enc := encoding_attr) {
                const name = enc.val;
                let valid = name.len > 0 && std.ascii.isAlphabetic(UT.unitLow(name[0]));
                if (valid) {
                    for (u of name) {
                        const c = UT.unitLow(u);
                        if (!(std.ascii.isAlphanumeric(c) || c == '.' || c == '_' || c == '-')) {
                            valid = false;
                            break;
                        }
                    }
                }
                if (!valid) {
                    return _.scanner.errNamed(enc.pos, "Invalid encoding name", name, " in the XML declaration");
                }
            }

            if (sd := _.parsePseudoAttribute("standalone")?) {
                if (UT.eqAscii(sd.val, "yes")) {
                    _.scanner.standalone = true;
                } else if (!UT.eqAscii(sd.val, "no")) {
                    return _.scanner.errNamed(sd.pos, "Invalid value", sd.val, " for standalone in the XML declaration (expected yes or no)");
                }
            }

            switch (_.scanner.tok.kind) {
                .question => {
                    _.advance()?;
                    if (_.scanner.tok.kind != .gt || _.scanner.tok.spaced) {
                        return _.unexpected("'?>' to end the XML declaration");
                    }
                },
                .name name => {
                    if (UT.eqAscii(name, "version") || UT.eqAscii(name, "encoding") || UT.eqAscii(name, "standalone")) {
                        return _.scanner.errNamed(_.scanner.tok.pos, "Misplaced", name, " in the XML declaration (the order is version, encoding, standalone)");
                    }
                    return _.scanner.errNamed(_.scanner.tok.pos, "Unexpected", name, " in the XML declaration (expected version, encoding or standalone)");
                },
                .eof => return _.scanner.errMsg(_.scanner.tok.pos, "Unterminated XML declaration: expected '?>'"),
                else => return _.unexpected("'?>' to end the XML declaration"),
            }

            if (enc := encoding_attr) {
                _.scanner.applyDeclaredEncoding(enc.val, enc.pos)?;
            }
        }

        fn parsePseudoAttribute(*_, name: string): XMLError!(struct { val: Slice, pos: usize })? {
            if (_.scanner.tok.kind != .name || !UT.eqAscii(_.scanner.tok.kind.@"name", name)) {
                return null;
            }
            _.requireSpaced()?;
            const cur_pos = _.scanner.tok.pos;
            _.advance()?;
            if (_.scanner.tok.kind != .eq) {
                return _.unexpected("'=' after the name in the XML declaration");
            }
            _.advanceLiteral(.Plain)?;
            if (_.scanner.tok.kind != .literal) {
                return _.unexpected("a quoted value in the XML declaration");
            }
            const value = _.scanner.tok.kind.literal;
            _.advance()?;
            return .{ val: value, pos: cur_pos };
        }

        fn parseDoctype(*_): XMLError!void {
            _.in_dtd = true;
            _.advance()?;
            * = _.expectName("the document type name")?;
            _.requireSpaced()?;
            _.advance()?;
            if (_.scanner.tok.kind == .name && (UT.eqAscii(_.scanner.tok.kind.@"name", "SYSTEM") || UT.eqAscii(_.scanner.tok.kind.@"name", "PUBLIC"))) {
                _.requireSpaced()?;
                _.parseExternalId(false)?;
                _.scanner.has_external_subset = true;
            }
            switch (_.scanner.tok.kind) {
                .gt => {},
                .bracket_open => {
                    _.parseInternalSubset()?;
                    _.advance()?;
                    * = _.expectGt("'>' to close the document type declaration")?;
                },
                else => return _.unexpected("SYSTEM, PUBLIC, '[' or '>' in the document type declaration"),
            }
            _.in_dtd = false;
        }

        fn parseExternalId(*_, notation: bool): XMLError!void {
            if (_.scanner.tok.kind == .name && UT.eqAscii(_.scanner.tok.kind.@"name", "SYSTEM")) {
                _.advanceLiteral(.System)?;
                if (_.scanner.tok.kind != .literal) {
                    return _.unexpected("a quoted system identifier after SYSTEM");
                }
                _.requireSpaced()?;
                return _.advance();
            }
            _.advanceLiteral(.Pubid)?;
            if (_.scanner.tok.kind != .literal) {
                return _.unexpected("a quoted public identifier after PUBLIC");
            }
            _.requireSpaced()?;
            _.advanceLiteral(.System)?;
            if (_.scanner.tok.kind == .literal) {
                _.requireSpaced()?;
                return _.advance();
            }
            if (notation) return;
            return _.unexpected("a quoted system identifier after the public identifier");
        }

        fn processingDeclarations(): bool {
            return _.scanner.standalone || !_.scanner.saw_unread_pe;
        }

        fn parseInternalSubset(*_): XMLError!void {
            while (true) {
                _.advance()?;
                const cur_pos = _.scanner.tok.pos;
                const frame = _.scanner.tok.frame;
                let end_frame: u32 = 0;
                switch (_.scanner.tok.kind) {
                    .bracket_close => {
                        if (frame == 0) return;
                        return _.scanner.errMsg(cur_pos, "']' inside a parameter entity cannot close the internal subset");
                    },
                    .comment, .pi => continue,
                    .pe_reference name => {
                        _.scanner.includeParameterEntity(name, cur_pos)?;
                        continue;
                    },
                    .decl d => {
                        switch (d) {
                            .Element => end_frame = _.parseElementDecl()?,
                            .Attlist => end_frame = _.parseAttlistDecl()?,
                            .Entity => end_frame = _.parseEntityDecl()?,
                            .Notation => end_frame = _.parseNotationDecl()?,
                            .Doctype => return _.scanner.errMsg(cur_pos, "'<!DOCTYPE' cannot appear inside the internal subset"),
                        }
                    },
                    .eof => return _.scanner.errMsg(cur_pos, "Unterminated internal subset: expected ']'"),
                    else => return _.unexpected("a markup declaration or ']' in the internal subset"),
                }
                if (end_frame != frame) {
                    return _.scanner.errMsg(cur_pos, "A markup declaration must begin and end in the same entity");
                }
            }
        }

        fn parseElementDecl(*_): XMLError!u32 {
            _.advance()?;
            * = _.expectName("an element name after '<!ELEMENT'")?;
            _.requireSpaced()?;
            _.advance()?;
            switch (_.scanner.tok.kind) {
                .name n => {
                    if (UT.eqAscii(n, "EMPTY") || UT.eqAscii(n, "ANY")) {
                        _.requireSpaced()?;
                        _.advance()?;
                    } else {
                        return _.unexpected("EMPTY, ANY or '(' in the element declaration");
                    }
                },
                .paren_open => {
                    _.requireSpaced()?;
                    _.advance()?;
                    if (_.scanner.tok.kind == .hash && UT.eqAscii(_.scanner.tok.kind.hash, "PCDATA")) {
                        _.parseMixed()?;
                    } else {
                        _.parseGroup()?;
                    }
                },
                else => return _.unexpected("EMPTY, ANY or '(' in the element declaration"),
            }
            return _.expectGt("'>' to end the element declaration");
        }

        fn parseMixed(*_): XMLError!void {
            let names: usize = 0;
            while (true) {
                _.advance()?;
                switch (_.scanner.tok.kind) {
                    .paren_close => break,
                    .bar => {
                        _.advance()?;
                        switch (_.scanner.tok.kind) {
                            .name => names += 1,
                            .hash, .paren_open => return _.scanner.errMsg(_.scanner.tok.pos, "Only element names may follow #PCDATA in a mixed content model"),
                            else => return _.unexpected("an element name after '|'"),
                        }
                    },
                    .comma => return _.scanner.errMsg(_.scanner.tok.pos, "A mixed content model is separated by '|', not ','"),
                    .question, .star, .plus => {
                        if (!_.scanner.tok.spaced && names > 0) {
                            return _.scanner.errMsg(_.scanner.tok.pos, "Names in a mixed content model cannot have occurrence indicators");
                        }
                        return _.unexpected("'|' or ')' in the mixed content model");
                    },
                    else => return _.unexpected("'|' or ')' in the mixed content model"),
                }
            }
            _.advance()?;
            switch (_.scanner.tok.kind) {
                .star => {
                    if (!_.scanner.tok.spaced) return _.advance();
                    if (names > 0) return _.scanner.errMsg(_.scanner.tok.pos, "A mixed content model with element names must end with ')*'");
                },
                .question, .plus => {
                    if (!_.scanner.tok.spaced) {
                        return _.scanner.errMsg(_.scanner.tok.pos, "A mixed content model may only be followed by '*'");
                    }
                    if (names > 0) return _.scanner.errMsg(_.scanner.tok.pos, "A mixed content model with element names must end with ')*'");
                },
                else => {
                    if (names > 0) return _.scanner.errMsg(_.scanner.tok.pos, "A mixed content model with element names must end with ')*'");
                },
            }
        }

        fn parseGroup(*_): XMLError!void {
            _.parseParticle()?;
            let sep_bar: bool? = null;
            while (true) {
                switch (_.scanner.tok.kind) {
                    .paren_close => {
                        _.advance()?;
                        return _.parseOccurrence();
                    },
                    .bar => {
                        if (sb := sep_bar) {
                            if (!sb) return _.scanner.errMsg(_.scanner.tok.pos, "A content model group cannot mix ',' and '|'");
                        } else {
                            sep_bar = true;
                        }
                        _.advance()?;
                        _.parseParticle()?;
                    },
                    .comma => {
                        if (sb := sep_bar) {
                            if (sb) return _.scanner.errMsg(_.scanner.tok.pos, "A content model group cannot mix ',' and '|'");
                        } else {
                            sep_bar = false;
                        }
                        _.advance()?;
                        _.parseParticle()?;
                    },
                    else => return _.unexpected("')', '|' or ',' in the content model"),
                }
            }
        }

        fn parseParticle(*_): XMLError!void {
            if (_.depth >= 512) return _.stackOverflow();
            _.depth += 1;
            defer _.depth -= 1;
            switch (_.scanner.tok.kind) {
                .name => {
                    _.advance()?;
                    return _.parseOccurrence();
                },
                .paren_open => {
                    _.advance()?;
                    return _.parseGroup();
                },
                .hash n => {
                    if (UT.eqAscii(n, "PCDATA")) {
                        return _.scanner.errMsg(_.scanner.tok.pos, "#PCDATA must come first in a content model, as (#PCDATA|a|b)*");
                    }
                    return _.unexpected("an element name or '(' in the content model");
                },
                else => return _.unexpected("an element name or '(' in the content model"),
            }
        }

        fn parseOccurrence(*_): XMLError!void {
            switch (_.scanner.tok.kind) {
                .question, .star, .plus => {
                    if (!_.scanner.tok.spaced) return _.advance();
                    return _.scanner.errMsg(_.scanner.tok.pos, "An occurrence indicator must directly follow the name or ')' it applies to");
                },
                else => {},
            }
        }

        fn parseAttlistDecl(*_): XMLError!u32 {
            _.advance()?;
            const element = _.expectName("an element name after '<!ATTLIST'")?;
            _.requireSpaced()?;
            while (true) {
                _.advance()?;
                let attr_name: Slice = [];
                switch (_.scanner.tok.kind) {
                    .gt => return _.scanner.tok.frame,
                    .name n => attr_name = n,
                    else => return _.unexpected("an attribute name or '>' in the ATTLIST declaration"),
                }
                _.requireSpaced()?;
                _.advance()?;
                let cdata = false;
                switch (_.scanner.tok.kind) {
                    .name n => {
                        if (UT.eqAscii(n, "CDATA")) {
                            _.requireSpaced()?;
                            cdata = true;
                        } else if (UT.eqAscii(n, "ID") || UT.eqAscii(n, "IDREF") || UT.eqAscii(n, "IDREFS") ||
                            UT.eqAscii(n, "ENTITY") || UT.eqAscii(n, "ENTITIES") || UT.eqAscii(n, "NMTOKEN") || UT.eqAscii(n, "NMTOKENS"))
                        {
                            _.requireSpaced()?;
                            cdata = false;
                        } else if (UT.eqAscii(n, "NOTATION")) {
                            _.requireSpaced()?;
                            _.advance()?;
                            if (_.scanner.tok.kind != .paren_open) return _.unexpected("'(' after NOTATION");
                            _.requireSpaced()?;
                            _.parseEnumeration(true)?;
                            cdata = false;
                        } else {
                            return _.unexpected("an attribute type (CDATA, ID, IDREF, IDREFS, ENTITY, ENTITIES, NMTOKEN, NMTOKENS, NOTATION or an enumeration)");
                        }
                    },
                    .paren_open => {
                        _.requireSpaced()?;
                        _.parseEnumeration(false)?;
                        cdata = false;
                    },
                    else => return _.unexpected("an attribute type (CDATA, ID, IDREF, IDREFS, ENTITY, ENTITIES, NMTOKEN, NMTOKENS, NOTATION or an enumeration)"),
                }

                const lit = types.Literal{ .AttValue = !cdata };
                _.advanceLiteral(lit)?;
                let def_val: Slice? = null;
                switch (_.scanner.tok.kind) {
                    .hash n => {
                        if (UT.eqAscii(n, "REQUIRED") || UT.eqAscii(n, "IMPLIED")) {
                            _.requireSpaced()?;
                            def_val = null;
                        } else if (UT.eqAscii(n, "FIXED")) {
                            _.requireSpaced()?;
                            _.advanceLiteral(lit)?;
                            if (_.scanner.tok.kind != .literal) return _.unexpected("a quoted default value after #FIXED");
                            def_val = _.scanner.tok.kind.literal;
                            _.requireSpaced()?;
                        } else {
                            return _.unexpected("#REQUIRED, #IMPLIED, #FIXED or a quoted default value");
                        }
                    },
                    .literal v => {
                        _.requireSpaced()?;
                        def_val = v;
                    },
                    else => return _.unexpected("#REQUIRED, #IMPLIED, #FIXED or a quoted default value"),
                }

                if (_.processingDeclarations()) {
                    const gop = _.attlists.getOrPut(_.scanner.allocator, element) catch return error.OutOfMemory;
                    if (!gop.found_existing) {
                        gop.value_ptr.* = AttList.init();
                    }
                    gop.value_ptr.declare(_.scanner.allocator, AttDef{
                        name: attr_name,
                        cdata: cdata,
                        default: def_val,
                    })?;
                }
            }
        }

        fn parseEnumeration(*_, names: bool): XMLError!void {
            while (true) {
                _.advance()?;
                switch (_.scanner.tok.kind) {
                    .name => {},
                    .nmtoken => {
                        if (names) return _.unexpected("a notation name");
                    },
                    else => return if (names) _.unexpected("a notation name") else _.unexpected("a name token in the enumeration"),
                }
                _.advance()?;
                switch (_.scanner.tok.kind) {
                    .bar => {},
                    .paren_close => {
                        return;
                    },
                    else => return _.unexpected("'|' or ')' in the enumeration"),
                }
            }
        }

        fn parseEntityDecl(*_): XMLError!u32 {
            _.advance()?;
            if (_.scanner.tok.kind == .percent_name) {
                return _.scanner.errMsg(_.scanner.tok.pos, "Whitespace is required between '%' and the name in a parameter entity declaration");
            }
            const parameter = _.scanner.tok.kind == .percent;
            if (parameter) {
                _.requireSpaced()?;
                _.advance()?;
            }
            const name = _.expectName(if (parameter) "the parameter entity name after '%'" else "an entity name or '%' after '<!ENTITY'")?;
            _.requireSpaced()?;
            _.advanceLiteral(.EntityValue)?;
            let val: ST.EntityValue = .external;
            switch (_.scanner.tok.kind) {
                .literal v => {
                    _.requireSpaced()?;
                    _.advance()?;
                    val = .{ .internal = v };
                },
                .name n => {
                    if (UT.eqAscii(n, "SYSTEM") || UT.eqAscii(n, "PUBLIC")) {
                        _.requireSpaced()?;
                        _.parseExternalId(false)?;
                        if (_.scanner.tok.kind == .name && UT.eqAscii(_.scanner.tok.kind.@"name", "NDATA")) {
                            _.requireSpaced()?;
                            if (parameter) {
                                return _.scanner.errMsg(_.scanner.tok.pos, "Parameter entities cannot have NDATA");
                            }
                            _.advance()?;
                            * = _.expectName("a notation name after NDATA")?;
                            _.requireSpaced()?;
                            _.advance()?;
                            val = .unparsed;
                        } else {
                            val = .external;
                        }
                    } else {
                        return _.unexpected("a quoted entity value, SYSTEM or PUBLIC");
                    }
                },
                else => return _.unexpected("a quoted entity value, SYSTEM or PUBLIC"),
            }
            const frame = _.expectGt("'>' to end the entity declaration")?;
            if (_.processingDeclarations()) {
                const table = if (parameter) &_.scanner.entities.parameter else &_.scanner.entities.general;
                const gop = table.getOrPut(_.scanner.allocator, name) catch return error.OutOfMemory;
                if (!gop.found_existing) {
                    gop.value_ptr.* = val;
                }
            }
            return frame;
        }

        fn parseNotationDecl(*_): XMLError!u32 {
            _.advance()?;
            * = _.expectName("a notation name after '<!NOTATION'")?;
            _.requireSpaced()?;
            _.advance()?;
            if (_.scanner.tok.kind != .name || (!UT.eqAscii(_.scanner.tok.kind.@"name", "SYSTEM") && !UT.eqAscii(_.scanner.tok.kind.@"name", "PUBLIC"))) {
                return _.unexpected("SYSTEM or PUBLIC in the notation declaration");
            }
            _.requireSpaced()?;
            _.parseExternalId(true)?;
            return _.expectGt("'>' to end the notation declaration");
        }

        fn parseTree(*_): XMLError!void {
            _.openElement()?;
            while (_.open.items.len > 0) {
                const top = _.open.items[_.open.items.len - 1];
                if (_.scanner.inDocument() && _.scanner.idx != null) {
                    const step = _.contentStep(top.name, top.frame)?;
                    if (step != .Slow) continue;
                }
                _.advanceContent()?;
                switch (_.scanner.tok.kind) {
                    .text txt => {
                        const loc = _.scanner.loc(_.scanner.tok.pos);
                        _.sink.text(txt, loc)?;
                    },
                    .comment txt => {
                        const loc = _.scanner.loc(_.scanner.tok.pos);
                        _.sink.comment(txt, loc)?;
                    },
                    .pi p => {
                        const loc = _.scanner.loc(_.scanner.tok.pos);
                        _.sink.pi(p.target, p.data, loc)?;
                    },
                    .start_tag => _.openElement()?,
                    .end_tag end_name => {
                        _.closeElement(top.name, top.frame, end_name, _.scanner.tok.pos, _.scanner.tok.frame)?;
                    },
                    .eof => return _.scanner.errNamed(_.scanner.tok.pos, "Missing closing tag for element", top.name, ""),
                    else => unreachable,
                }
            }
        }

        fn openElement(*_): XMLError!void {
            if (_.scanner.tok.kind != .start_tag) unreachable;
            const name = _.scanner.tok.kind.start_tag;
            const cur_pos = _.scanner.tok.pos;
            const frame = _.scanner.tok.frame;
            if (_.open.items.len >= MAX_DEPTH) {
                return _.stackOverflow();
            }
            const loc = _.scanner.loc(cur_pos);
            _.sink.beginElement(name, loc)?;
            const empty = _.parseAttributes(name)?;
            if (empty) {
                _.sink.endElement()?;
            } else if (!(_.leafFast(name, frame)?)) {
                _.open.append(_.scanner.allocator, OpenElem{ name: name, frame: frame }) catch return error.OutOfMemory;
            }
        }

        fn leafFast(*_, name: Slice, frame: u32): XMLError!bool {
            const sc = &_.scanner;
            if (!sc.inDocument() || sc.idx == null || sc.frame_id != frame) {
                return false;
            }
            const src = sc.src;
            const start = sc.pos;
            const stop = sc.nextStop(STOP_CONTENT);
            const n = name.len;
            const close = stop + 2 + n;
            if (close < src.len &&
                UT.unitLow(src[stop]) == '<' &&
                UT.unitLow(src[stop + 1]) == '/' &&
                UT.unitLow(src[close]) == '>' &&
                UT.nameEq(src[stop + 2 .. close], name))
            {
                sc.pos = close + 1;
                sc.tag_degraded = false;
                const loc = sc.loc(start);
                _.sink.endLeaf(src[start..stop], loc)?;
                return true;
            }
            return false;
        }

        fn closeElement(*_, name: Slice, frame: u32, end_name: Slice, cur_pos: usize, end_frame: u32): XMLError!void {
            if (!UT.nameEq(end_name, name)) {
                const s1 = _.scanner.formatUnits(name);
                const s2 = _.scanner.formatUnits(end_name);
                return _.scanner.errFmt(cur_pos, "Expected closing tag \\</{s}> but found \\</{s}>", .{ s1, s2 });
            }
            if (end_frame != frame) {
                return _.scanner.errNamed(cur_pos, "Element", name, " must start and end within the same entity");
            }
            if (!_.scanner.takeGt()) {
                _.advance()?;
                * = _.expectGt("'>' to end the closing tag")?;
            }
            _.sink.endElement()?;
            * = _.open.pop();
        }

        fn contentStep(*_, name: Slice, frame: u32): XMLError!types.Step {
            const sc = &_.scanner;
            const src = sc.src;
            const start = sc.pos;
            const stop = sc.nextStop(STOP_CONTENT);
            if (stop + 1 >= src.len || UT.unitLow(src[stop]) != '<') {
                return .Slow;
            }
            const nxt = UT.unitLow(src[stop + 1]);
            if (nxt == '!' || nxt == '?') return .Slow;
            if (stop > start) {
                const loc = sc.loc(start);
                _.sink.text(src[start..stop], loc)?;
            }
            if (nxt == '/') {
                sc.pos = stop + 2;
                const n = name.len;
                const fast_match = (src.len - sc.pos) > n &&
                    UT.nameEq(src[sc.pos .. sc.pos + n], name) &&
                    !types.isNameCharAscii(UT.unitLow(src[sc.pos + n])) &&
                    UT.unitLow(src[sc.pos + n]) < 0x80;
                const end_name = if (fast_match) blk: {
                    sc.pos += n;
                    break :blk name;
                } else sc.scanName("Expected an element name after '</' but found")?;
                const end_frame = sc.frame_id;
                _.closeElement(name, frame, end_name, stop, end_frame)?;
                return .Done;
            }
            sc.pos = stop + 1;
            sc.tag_degraded = false;
            const child = sc.scanName("Expected an element name after '<' but found")?;
            sc.tok = ST.Token{
                kind: .{ .start_tag = child },
                pos: stop,
                frame: sc.frame_id,
                spaced: false,
            };
            _.openElement()?;
            return .Continue;
        }

        fn parseAttributes(*_, element: Slice): XMLError!bool {
            _.attribute_count = 0;
            const defs: (*const AttList)? = if (_.attlists.count() == 0) null else _.attlists.getPtr(element);
            let empty = false;
            while (true) {
                if (_.scanner.inDocument()) {
                    const step = _.scanner.tagStep();
                    switch (step) {
                        .end is_empty => {
                            empty = is_empty;
                            break;
                        },
                        .attr a => {
                            if (_.hasAttribute(a.name)) {
                                return _.scanner.errNamed(a.pos, "Duplicate attribute", a.name, "");
                            }
                            const collapse = if (d := defs) (if (def := d.get(a.name)) !def.cdata else false) else false;
                            const val = _.scanner.scanAttValue(a.quote, collapse)?;
                            _.pushAttribute(a.name, val)?;
                            continue;
                        },
                        .slow => {},
                    }
                }
                _.scanner.next(.None)?;
                let attr_name: Slice = [];
                switch (_.scanner.tok.kind) {
                    .gt => { empty = false; break; },
                    .slash_gt => { empty = true; break; },
                    .name n => attr_name = n,
                    else => return _.unexpected("an attribute name, '>' or '/>' in the start tag"),
                }
                _.requireSpaced()?;
                const cur_pos = _.scanner.tok.pos;
                if (_.hasAttribute(attr_name)) {
                    return _.scanner.errNamed(cur_pos, "Duplicate attribute", attr_name, "");
                }
                _.scanner.next(.None)?;
                if (_.scanner.tok.kind != .eq) {
                    return _.unexpected("'=' after the attribute name");
                }
                const collapse = if (d := defs) (if (def := d.get(attr_name)) !def.cdata else false) else false;
                _.scanner.next(.{ .AttValue = collapse })?;
                if (_.scanner.tok.kind != .literal) {
                    return _.unexpected("a quoted attribute value");
                }
                _.pushAttribute(attr_name, _.scanner.tok.kind.literal)?;
            }
            if (d := defs) {
                for (def of d.defs.items) {
                    if (def_val := def.default) {
                        if (!_.hasAttribute(def.name)) {
                            _.pushAttribute(def.name, def_val)?;
                        }
                    }
                }
            }
            return empty;
        }

        fn hasAttribute(name: Slice): bool {
            if (_.attribute_count <= LINEAR_ATTRIBUTE_LIMIT) {
                let i: usize = 0;
                while (i < _.attribute_count) : (i += 1) {
                    if (UT.nameEq(_.attribute_first[i], name)) return true;
                }
                return false;
            }
            return _.attribute_names.contains(name);
        }

        fn pushAttribute(*_, name: Slice, value: Slice): XMLError!void {
            _.sink.attribute(name, value)?;
            if (_.attribute_count < LINEAR_ATTRIBUTE_LIMIT) {
                _.attribute_first[_.attribute_count] = name;
            } else {
                _.spillAttributeName(name)?;
            }
            _.attribute_count += 1;
        }

        fn spillAttributeName(*_, name: Slice): XMLError!void {
            const empty_void: void = undefined;
            if (_.attribute_count == LINEAR_ATTRIBUTE_LIMIT) {
                _.attribute_names.clearRetainingCapacity();
                for (first of _.attribute_first) {
                    _.attribute_names.put(_.scanner.allocator, first, empty_void) catch return error.OutOfMemory;
                }
            }
            _.attribute_names.put(_.scanner.allocator, name, empty_void) catch return error.OutOfMemory;
        }
    };
}

fn parseUnits(
    comptime U: type,
    source: *const bun.logger.Source,
    contents: []const U,
    log: *bun.logger.Log,
    allocator: std.mem.Allocator,
    options: Options,
): XMLError!Expr {
    bun.analytics.Features.xml_parse += 1;
    const Sinks = SinkTypes(U);
    if (options.compact) {
        let compact_sink = Sinks.CompactSink.init(allocator);
        let parser = ParserType(U, Sinks.CompactSink).init(source, contents, log, allocator, options, move compact_sink);
        return parser.parseDocument();
    } else {
        let node_sink = Sinks.NodeSink.init(allocator);
        let parser = ParserType(U, Sinks.NodeSink).init(source, contents, log, allocator, options, move node_sink);
        return parser.parseDocument();
    }
}

pub fn parse(
    source: *const bun.logger.Source,
    log: *bun.logger.Log,
    allocator: std.mem.Allocator,
    options: Options,
): XMLError!Expr {
    return parseUnits(u8, source, source.contents, log, allocator, options);
}

pub fn parseUtf16(
    source: *const bun.logger.Source,
    units: []const u16,
    log: *bun.logger.Log,
    allocator: std.mem.Allocator,
    compact: bool,
): XMLError!Expr {
    return parseUnits(u16, source, units, log, allocator, Options{
        compact: compact,
        encoding: .Text,
    });
}
