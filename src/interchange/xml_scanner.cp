import std from "std";
import bun from "bun";
import types from "./xml_types.zig";
import xml_index from "./xml_index.zig";

const XMLError = types.XMLError;
const InputEncoding = types.InputEncoding;
const DeclKind = types.DeclKind;
const Literal = types.Literal;
const FrameKind = types.FrameKind;

const STOP_CONTENT: u8 = xml_index.CLASS_LT | xml_index.CLASS_GT | xml_index.CLASS_ALWAYS;
const STOP_ATT_VALUE: u8 = xml_index.CLASS_LT | xml_index.CLASS_GT | xml_index.CLASS_ALWAYS | xml_index.CLASS_TAG;
const STOP_SKIPPED: u8 = xml_index.CLASS_ALWAYS;

fn byteClass(b: u8): u8 {
    const LUT_LO: [16]u8 = [ 0x03, 0x03, 0x13, 0x03, 0x03, 0x03, 0x07, 0x13, 0x03, 0x0a, 0x0a, 0x03, 0x43, 0x23, 0x83, 0x03 ];
    const LUT_HI: [16]u8 = [ 0x09, 0x02, 0x14, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 ];
    return LUT_LO[b & 0x0f] & LUT_HI[b >> 4];
}

pub fn ScannerTypes(comptime U: type): type {
    const is_wide = U == u16;
    const UT = types.UnitTypes(U);
    const Idx = xml_index.StructuralIndex(U);

    return struct {
        const Slice = []const U;

        pub const Kind = union(enum) {
            eof: Slice?,
            name: Slice,
            nmtoken: Slice,
            hash: Slice,
            pe_reference: Slice,
            percent: void,
            percent_name: Slice,
            literal: Slice,
            eq: void,
            gt: void,
            slash_gt: void,
            paren_open: void,
            paren_close: void,
            bar: void,
            comma: void,
            question: void,
            star: void,
            plus: void,
            bracket_open: void,
            bracket_close: void,
            decl: DeclKind,
            xml_decl: void,
            comment: Slice,
            pi: Pi,
            start_tag: Slice,
            end_tag: Slice,
            text: Slice,
            unexpected: u32,
        };

        pub const Pi = struct { target: Slice, data: Slice };

        pub const Token = struct {
            kind: Kind,
            pos: usize,
            frame: u32,
            spaced: bool,
        };

        const TagStep = union(enum) {
            end: bool,
            attr: struct {
                name: Slice,
                pos: usize,
                quote: u8,
            },
            slow: void,
        };

        pub const EntityValue = union(enum) {
            internal: Slice,
            external: void,
            unparsed: void,
        };

        pub const SliceCtx = struct {
            static fn hash(_ctx: SliceCtx, key: Slice): u64 {
                * = _ctx;
                return std.hash.Wyhash.hash(0, UT.unitBytes(key));
            }
            static fn eql(_ctx: SliceCtx, a: Slice, b: Slice): bool {
                * = _ctx;
                return UT.nameEq(a, b);
            }
        };

        const EntityMap = std.HashMapUnmanaged(Slice, EntityValue, SliceCtx, 80);

        const Entities = struct {
            general: EntityMap,
            parameter: EntityMap,

            static fn init(): Entities {
                return Entities{
                    general: .{},
                    parameter: .{},
                };
            }

            fn deinit(*_, allocator: std.mem.Allocator) {
                _.general.deinit(allocator);
                _.parameter.deinit(allocator);
            }
        };

        const Resolved = union(enum) {
            byte: u8,
            text: Slice,
            unexpanded: void,
        };

        const Frame = struct {
            src: Slice,
            pos: usize,
            id: u32,
            kind: FrameKind,
            entity_name: Slice?,
            entity_is_pe: bool,
            report_pos: usize,
        };

        static fn predefinedEntity(name: Slice): u8? {
            return switch (name.len) {
                2 => if (UT.eqAscii(name, "lt")) '<' else if (UT.eqAscii(name, "gt")) '>' else null,
                3 => if (UT.eqAscii(name, "amp")) '&' else null,
                4 => if (UT.eqAscii(name, "apos")) '\'' else if (UT.eqAscii(name, "quot")) '"' else null,
                else => null,
            };
        }

        static fn collapseSpaces(allocator: std.mem.Allocator, value: Slice): XMLError!Slice {
            const trimmed = UT.trimWs(value);
            let has_run = false;
            let i: usize = 0;
            while (i < trimmed.len) : (i += 1) {
                if (UT.unitLow(trimmed[i]) == ' ') {
                    if (i + 1 < trimmed.len && UT.unitLow(trimmed[i + 1]) == ' ') {
                        has_run = true;
                        break;
                    }
                }
            }
            if (!has_run) return trimmed;
            let out: std.ArrayListUnmanaged(U) = .{};
            out.ensureTotalCapacity(allocator, trimmed.len) catch return error.OutOfMemory;
            let prev_space = false;
            for (u of trimmed) {
                if (UT.unitLow(u) == ' ') {
                    if (!prev_space) {
                        out.appendAssumeCapacity(UT.unitAscii(' '));
                        prev_space = true;
                    }
                } else {
                    out.appendAssumeCapacity(u);
                    prev_space = false;
                }
            }
            return out.items;
        }

        pub const Scanner = struct {
            src: Slice,
            pos: usize,
            frame_id: u32,
            frame_kind: FrameKind,
            frame_entity_name: Slice?,
            frame_entity_is_pe: bool,
            frame_report_pos: usize,
            suspended: std.ArrayListUnmanaged(Frame),
            next_frame_id: u32,

            entities: Entities,
            expanded_bytes: u64,
            document_len: u64,

            standalone: bool,
            has_external_subset: bool,
            saw_pe_reference: bool,
            saw_unread_pe: bool,

            transcoded: bool,
            saw_utf8_bom: bool,
            needs_utf16_declaration: bool,
            content_start: usize,
            encoding: InputEncoding,

            idx: Idx?,
            cursor: usize,
            latin1: bool,
            keep_markup: bool,
            tok: Token,
            tag_degraded: bool,

            allocator: std.mem.Allocator,
            source: *const bun.logger.Source,
            log: *bun.logger.Log,

            static fn init(
                allocator: std.mem.Allocator,
                source: *const bun.logger.Source,
                contents: Slice,
                log: *bun.logger.Log,
                encoding: InputEncoding,
                keep_markup: bool,
            ): Scanner {
                return Scanner{
                    src: contents,
                    pos: 0,
                    frame_id: 0,
                    frame_kind: .Document,
                    frame_entity_name: null,
                    frame_entity_is_pe: false,
                    frame_report_pos: 0,
                    suspended: .{},
                    next_frame_id: 1,
                    entities: Entities.init(),
                    expanded_bytes: 0,
                    document_len: @as(u64, contents.len) * @sizeOf(U),
                    standalone: false,
                    has_external_subset: false,
                    saw_pe_reference: false,
                    saw_unread_pe: false,
                    transcoded: false,
                    saw_utf8_bom: false,
                    needs_utf16_declaration: false,
                    content_start: 0,
                    encoding: encoding,
                    idx: null,
                    cursor: 0,
                    latin1: encoding == .Latin1,
                    keep_markup: keep_markup,
                    tok: Token{ kind: .{ .eof = null }, pos: 0, frame: 0, spaced: false },
                    tag_degraded: false,
                    allocator: allocator,
                    source: source,
                    log: log,
                };
            }

            ~init() {
                if (*idx := _.idx) {
                    idx.~init();
                }
                _.suspended.deinit(_.allocator);
                _.entities.deinit(_.allocator);
            }

            fn buildIndex(*_) {
                _.idx = Idx.init(_.allocator, _.src);
                _.cursor = 0;
            }

            fn nextStop(*_, scalar_mask: u8): usize {
                if (_.frame_kind == .Document) {
                    if (*idx := _.idx) {
                        const pair = idx.seek(_.cursor, _.pos);
                        _.cursor = pair[0];
                        const p = pair[1];
                        if (!_.tag_degraded) {
                            return p;
                        }
                        return @min(p, _.scalarStop(scalar_mask));
                    }
                }
                return _.scalarStop(scalar_mask);
            }

            fn scalarStop(mask: u8): usize {
                let p = _.pos;
                while (p < _.src.len && (byteClass(UT.unitLow(_.src[p])) & mask) == 0) : (p += 1) {}
                return p;
            }

            fn errAtEntry(*_, c: u8): XMLError {
                if (c >= 0x80 && !is_wide) {
                    _.pos -|= 2;
                }
                return _.errInvalidChar();
            }

            fn sawGtInLiteral(*_) {
                if (_.inDocument()) {
                    _.tag_degraded = true;
                }
            }

            fn loc(pos: usize): bun.logger.Loc {
                if (_.transcoded) return bun.logger.Loc.Empty;
                const clamped = @min(pos, @as(usize, std.math.maxInt(i32)));
                return bun.logger.Loc{ start: @as(i32, @intCast(clamped)) };
            }

            fn errMsg(*_, pos: usize, msg: string): XMLError {
                _.log.addError(_.source, _.loc(pos), msg) catch return error.OutOfMemory;
                return error.SyntaxError;
            }

            fn errFmt(*_, pos: usize, comptime fmt: string, args: auto): XMLError {
                const msg = std.fmt.allocPrint(_.allocator, fmt, args) catch return error.OutOfMemory;
                _.log.addRangeError(_.source, bun.logger.Range{ loc: _.loc(pos) }, msg) catch return error.OutOfMemory;
                return error.SyntaxError;
            }

            fn formatUnits(*_, units: Slice): string {
                if (comptime is_wide) {
                    return bun.strings.toUTF8AllocWithType(_.allocator, units) catch return "";
                }
                if (!_.latin1) {
                    return UT.unitBytes(units);
                }
                let out = _.allocator.alloc(u8, units.len * 2) catch return "";
                let n: usize = 0;
                for (b of units) {
                    if (b < 0x80) {
                        out[n] = b;
                        n += 1;
                    } else {
                        out[n] = 0xC0 | (b >> 6);
                        out[n + 1] = 0x80 | (b & 0x3F);
                        n += 2;
                    }
                }
                return out[0..n];
            }

            fn errNamed(*_, pos: usize, before: string, name: Slice, after: string): XMLError {
                const s = _.formatUnits(name);
                return _.errFmt(pos, "{s} '{s}'{s}", .{ before, s, after });
            }

            fn errHere(*_, what: string): XMLError {
                const pos = _.here();
                if (_.atEnd()) {
                    if (ename := _.frame_entity_name) {
                        const s = _.formatUnits(ename);
                        return _.errFmt(pos, "{s} the end of entity '{s}'", .{ what, s });
                    }
                    return _.errFmt(pos, "{s} end of input", .{what});
                }
                const c = _.peek();
                if (c >= 0x21 && c <= 0x7E) {
                    return _.errFmt(pos, "{s} '{c}'", .{ what, c });
                } else if (c >= 0x80) {
                    const pair = _.decodeUtf8();
                    const cp = pair[0];
                    if (cp >= 0x20 && cp != 0x7F && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                        let tmp: [4]u8 = [0, 0, 0, 0];
                        const n = bun.strings.encodeWTF8Rune(&tmp, @as(i32, @intCast(cp)));
                        return _.errFmt(pos, "{s} '{s}' (U+{X:0>4})", .{ what, tmp[0..n], cp });
                    }
                    return _.errFmt(pos, "{s} U+{X:0>4}", .{ what, cp });
                } else {
                    if (c == ' ') return _.errFmt(pos, "{s} space", .{what});
                    if (c == '\t') return _.errFmt(pos, "{s} tab", .{what});
                    if (c == '\n' || c == '\r') return _.errFmt(pos, "{s} newline", .{what});
                    return _.errFmt(pos, "{s} control character 0x{X:0>2}", .{ what, c });
                }
            }

            fn errInvalidChar(*_): XMLError {
                return _.errHere("Invalid character in XML:");
            }

            fn here(): usize {
                return if (_.inDocument()) _.pos else _.frame_report_pos;
            }

            fn peek(): u8 {
                return _.peekAt(_.pos);
            }

            fn peekAt(pos: usize): u8 {
                return if (pos < _.src.len) UT.unitLow(_.src[pos]) else 0;
            }

            fn unit(): U {
                return _.src[_.pos];
            }

            fn atEnd(): bool {
                return _.pos >= _.src.len;
            }

            fn hasPrefix(s: string): bool {
                const start = @min(_.pos, _.src.len);
                return UT.startsWithAscii(_.src[start..], s);
            }

            fn inDocument(): bool {
                return _.frame_kind == .Document;
            }

            fn decodeUtf8(): [2]u32 {
                const first = _.unit();
                if (comptime is_wide) {
                    const lead = UT.unitValue(first);
                    if (lead >= 0xD800 && lead < 0xDC00 && _.pos + 1 < _.src.len) {
                        const trail = UT.unitValue(_.src[_.pos + 1]);
                        if (trail >= 0xDC00 && trail < 0xE000) {
                            return [ 0x10000 + ((lead - 0xD800) << 10) + (trail - 0xDC00), 2 ];
                        }
                    }
                    return [ lead, 1 ];
                }
                if (_.latin1) {
                    return [ UT.unitValue(first), 1 ];
                }
                const b0 = UT.unitLow(first);
                const len = bun.strings.wtf8ByteSequenceLength(b0);
                if (len == 1 || _.pos + @as(usize, len) > _.src.len) {
                    return [ @as(u32, b0), 1 ];
                }
                let bytes: [4]u8 = [0, 0, 0, 0];
                let i: usize = 0;
                while (i < @as(usize, len)) : (i += 1) {
                    bytes[i] = UT.unitLow(_.src[_.pos + i]);
                }
                const cp = bun.strings.decodeWTF8RuneT(&bytes, len, u32, 0);
                return [ cp, @as(u32, len) ];
            }

            fn checkNonAsciiChar(*_): XMLError!usize {
                if (_.latin1) return 1;
                const pair = _.decodeUtf8();
                const cp = pair[0];
                const len = @as(usize, pair[1]);
                if (comptime is_wide) {
                    if (cp == 0xFFFE || cp == 0xFFFF || (cp >= 0xD800 && cp < 0xE000)) {
                        return _.errInvalidChar();
                    }
                    return len;
                }
                if (len == 1 || cp == 0) {
                    return _.errMsg(_.here(), "Invalid UTF-8");
                }
                if (cp == 0xFFFE || cp == 0xFFFF) {
                    return _.errInvalidChar();
                }
                return len;
            }

            fn pushFrame(*_, text: Slice, kind: FrameKind, entity_name: Slice, is_pe: bool, ref_pos: usize): XMLError!void {
                if (_.suspended.items.len >= types.MAX_ENTITY_DEPTH) {
                    return _.errMsg(ref_pos, "Entity references are nested too deeply");
                }
                if (cur := _.frame_entity_name) {
                    if (_.frame_entity_is_pe == is_pe && UT.nameEq(cur, entity_name)) {
                        return _.errNamed(ref_pos, "Entity", entity_name, " refers to itself");
                    }
                }
                for (f of _.suspended.items) {
                    if (fname := f.entity_name) {
                        if (f.entity_is_pe == is_pe && UT.nameEq(fname, entity_name)) {
                            return _.errNamed(ref_pos, "Entity", entity_name, " refers to itself");
                        }
                    }
                }
                _.expanded_bytes += @as(u64, text.len) * @sizeOf(U);
                const produced = _.document_len + _.expanded_bytes;
                if (produced > types.AMPLIFICATION_THRESHOLD &&
                    produced > types.MAX_AMPLIFICATION * @max(_.document_len, 1))
                {
                    return _.errMsg(ref_pos, "Entity expansion exceeds the amplification limit");
                }
                const report_pos = if (_.inDocument()) ref_pos else _.frame_report_pos;
                _.suspended.append(_.allocator, Frame{
                    src: _.src,
                    pos: _.pos,
                    id: _.frame_id,
                    kind: _.frame_kind,
                    entity_name: _.frame_entity_name,
                    entity_is_pe: _.frame_entity_is_pe,
                    report_pos: _.frame_report_pos,
                }) catch return error.OutOfMemory;
                _.src = text;
                _.pos = 0;
                _.frame_id = _.next_frame_id;
                _.next_frame_id += 1;
                _.frame_kind = kind;
                _.frame_entity_name = entity_name;
                _.frame_entity_is_pe = is_pe;
                _.frame_report_pos = report_pos;
            }

            fn popFrame(*_) {
                const frame = _.suspended.pop() orelse unreachable;
                _.src = frame.src;
                _.pos = frame.pos;
                _.frame_id = frame.id;
                _.frame_kind = frame.kind;
                _.frame_entity_name = frame.entity_name;
                _.frame_entity_is_pe = frame.entity_is_pe;
                _.frame_report_pos = frame.report_pos;
            }

            fn resolveGeneralEntity(*_, name: Slice, ref_pos: usize, in_attribute: bool): XMLError!Resolved {
                if (c := predefinedEntity(name)) {
                    return Resolved{ .byte = c };
                }
                if (ev := _.entities.general.get(name)) {
                    return switch (ev) {
                        .internal text => Resolved{ .text = text },
                        .external => if (in_attribute)
                            _.errNamed(ref_pos, "Attribute values cannot reference external entity", name, "")
                        else
                            Resolved.unexpanded,
                        .unparsed => _.errNamed(ref_pos, "Unparsed entity", name, " cannot be referenced"),
                    };
                }
                if (_.standalone || !(_.has_external_subset || _.saw_pe_reference)) {
                    return _.errNamed(ref_pos, "Entity", name, " is not declared");
                }
                return Resolved.unexpanded;
            }

            static fn pushReference(allocator: std.mem.Allocator, buf: *std.ArrayListUnmanaged(U), name: Slice): XMLError!void {
                buf.append(allocator, UT.unitAscii('&')) catch return error.OutOfMemory;
                buf.appendSlice(allocator, name) catch return error.OutOfMemory;
                buf.append(allocator, UT.unitAscii(';')) catch return error.OutOfMemory;
            }

            fn includeParameterEntity(*_, name: Slice, ref_pos: usize): XMLError!void {
                _.saw_pe_reference = true;
                if (ev := _.entities.parameter.get(name)) {
                    switch (ev) {
                        .internal text => return _.pushFrame(text, .Declarations, name, true, ref_pos),
                        else => {
                            _.saw_unread_pe = true;
                            return;
                        },
                    }
                }
                if (_.standalone) {
                    return _.errNamed(ref_pos, "Parameter entity", name, " is not declared");
                }
                _.saw_unread_pe = true;
            }

            fn initDocument(*_): XMLError!bool {
                if (comptime is_wide) {
                    if (_.src.len > 0 && UT.unitValue(_.src[0]) == 0xFEFF) {
                        _.pos = 1;
                    }
                } else {
                    const bytes = UT.unitBytes(_.src);
                    if (std.mem.startsWith(u8, bytes, "\xEF\xBB\xBF")) {
                        _.pos = 3;
                        _.saw_utf8_bom = true;
                    } else if (_.encoding == .Text || _.encoding == .Latin1) {
                        // Already decoded JS string.
                    } else if (std.mem.startsWith(u8, bytes, "\xFE\xFF")) {
                        _.transcodeUtf16(bytes[2..], true)?;
                    } else if (std.mem.startsWith(u8, bytes, "\xFF\xFE")) {
                        _.transcodeUtf16(bytes[2..], false)?;
                    } else if (std.mem.startsWith(u8, bytes, "\x00<")) {
                        _.transcodeUtf16(bytes, true)?;
                        _.needs_utf16_declaration = true;
                    } else if (std.mem.startsWith(u8, bytes, "<\x00")) {
                        _.transcodeUtf16(bytes, false)?;
                        _.needs_utf16_declaration = true;
                    }
                }
                _.content_start = _.pos;
                return _.hasPrefix("<?xml") && types.isWs(_.peekAt(_.pos + 5));
            }

            fn validateUtf8(*_): XMLError!void {
                if (comptime is_wide) return;
                if (_.encoding == .Text || _.encoding == .Latin1 || _.transcoded) return;
                const bytes = UT.unitBytes(_.src);
                const res = bun.simdutf.simdutf__validate_utf8_with_errors(bytes.ptr, bytes.len);
                if (res.isSuccessful()) return;
                return _.errMsg(res.count, "Invalid UTF-8");
            }

            fn transcodeUtf16(*_, payload: []const u8, big_endian: bool): XMLError!void {
                if ((payload.len & 1) != 0) {
                    return _.errMsg(payload.len, "UTF-16 input has an odd number of bytes");
                }
                const pair_count = payload.len / 2;
                let units = _.allocator.alloc(u16, pair_count) catch return error.OutOfMemory;
                let i: usize = 0;
                while (i < pair_count) : (i += 1) {
                    const b0 = payload[i * 2];
                    const b1 = payload[i * 2 + 1];
                    units[i] = if (big_endian)
                        (@as(u16, b0) << 8) | @as(u16, b1)
                    else
                        (@as(u16, b1) << 8) | @as(u16, b0);
                }
                const max_utf8 = pair_count * 3 + 1;
                let utf8 = _.allocator.alloc(u8, max_utf8) catch return error.OutOfMemory;
                const res = bun.simdutf.simdutf__convert_utf16le_to_utf8_with_errors(units.ptr, pair_count, utf8.ptr);
                if (!res.isSuccessful()) {
                    return _.errMsg(res.count * 2, "Invalid UTF-16");
                }
                if (comptime !is_wide) {
                    _.src = utf8[0..res.count];
                }
                _.pos = 0;
                _.transcoded = true;
            }

            fn checkUtf16Declaration(*_): XMLError!void {
                if (_.needs_utf16_declaration) {
                    return _.errMsg(0, "UTF-16 input must start with a byte-order mark or declare encoding=\"UTF-16\"");
                }
            }

            fn applyDeclaredEncoding(*_, name: Slice, pos: usize): XMLError!void {
                if (comptime is_wide) return;
                if (_.encoding == .Text || _.encoding == .Latin1) return;
                if (UT.eqAsciiIgnoreCase(name, "UTF-8") ||
                    UT.eqAsciiIgnoreCase(name, "UTF8") ||
                    UT.eqAsciiIgnoreCase(name, "US-ASCII") ||
                    UT.eqAsciiIgnoreCase(name, "ASCII"))
                {
                    if (_.transcoded) {
                        return _.errNamed(pos, "Document is UTF-16 but declares encoding", name, "");
                    }
                    return;
                }
                if (UT.eqAsciiIgnoreCase(name, "UTF-16") ||
                    UT.eqAsciiIgnoreCase(name, "UTF-16LE") ||
                    UT.eqAsciiIgnoreCase(name, "UTF-16BE"))
                {
                    if (!_.transcoded && _.encoding == .Bytes) {
                        return _.errNamed(pos, "Document is not UTF-16 but declares encoding", name, "");
                    }
                    _.needs_utf16_declaration = false;
                    return;
                }
                if (UT.eqAsciiIgnoreCase(name, "ISO-8859-1") ||
                    UT.eqAsciiIgnoreCase(name, "ISO_8859-1") ||
                    UT.eqAsciiIgnoreCase(name, "LATIN1") ||
                    UT.eqAsciiIgnoreCase(name, "L1") ||
                    UT.eqAsciiIgnoreCase(name, "CP819"))
                {
                    if (_.transcoded) {
                        return _.errNamed(pos, "Document is UTF-16 but declares encoding", name, "");
                    }
                    if (_.saw_utf8_bom) {
                        return _.errNamed(pos, "Document has a UTF-8 byte-order mark but declares encoding", name, "");
                    }
                    const rest_bytes = UT.unitBytes(_.src[_.pos..]);
                    const converted = bun.strings.toUTF8FromLatin1(_.allocator, rest_bytes) catch return error.OutOfMemory;
                    if (list := converted) {
                        _.src = list.items;
                        _.pos = 0;
                        _.content_start = std.math.maxInt(usize);
                        _.transcoded = true;
                    }
                    return;
                }
                return _.errNamed(pos, "Unsupported encoding", name, " (supported: UTF-8, UTF-16, ISO-8859-1)");
            }

            fn scanName(*_, what: string): XMLError!Slice {
                const start = _.pos;
                const c = _.peek();
                if (types.isNameStartAscii(c)) {
                    _.pos += 1;
                } else if (c >= 0x80) {
                    const pair = _.decodeUtf8();
                    if (!types.isNameStartCodePoint(pair[0])) {
                        return _.errHere(what);
                    }
                    _.pos += @as(usize, pair[1]);
                } else {
                    return _.errHere(what);
                }
                _.scanNameChars();
                return _.src[start.._.pos];
            }

            fn atNameStart(): bool {
                const c = _.peek();
                if (c < 0x80) return types.isNameStartAscii(c);
                return types.isNameStartCodePoint(_.decodeUtf8()[0]);
            }

            fn scanNameChars(*_) {
                while (true) {
                    const c = _.peek();
                    if (types.isNameCharAscii(c)) {
                        _.pos += 1;
                    } else if (c >= 0x80) {
                        const pair = _.decodeUtf8();
                        if (!types.isNameCodePoint(pair[0])) return;
                        _.pos += @as(usize, pair[1]);
                    } else {
                        return;
                    }
                }
            }

            fn scanNameRun(*_): (struct { slice: Slice, is_name: bool })? {
                const start = _.pos;
                const c = _.peek();
                let is_name = false;
                let len: usize = 1;
                if (c < 0x80) {
                    if (types.isNameStartAscii(c)) {
                        is_name = true;
                    } else if (types.isNameCharAscii(c)) {
                        is_name = false;
                    } else {
                        return null;
                    }
                } else {
                    const pair = _.decodeUtf8();
                    len = @as(usize, pair[1]);
                    if (types.isNameStartCodePoint(pair[0])) {
                        is_name = true;
                    } else if (types.isNameCodePoint(pair[0])) {
                        is_name = false;
                    } else {
                        return null;
                    }
                }
                _.pos += len;
                _.scanNameChars();
                return .{ slice: _.src[start.._.pos], is_name: is_name };
            }

            fn scanReferenceName(*_, what: string): XMLError!Slice {
                const name = _.scanName(what)?;
                if (_.peek() != ';') {
                    return _.errHere("Expected ';' after the entity name but found");
                }
                _.pos += 1;
                return name;
            }

            fn scanCharRef(*_, ref_pos: usize): XMLError!u32 {
                const text_start = _.pos - 2;
                const hex = _.peek() == 'x';
                if (hex) _.pos += 1;
                let value: u32 = 0;
                let digits: usize = 0;
                while (true) {
                    const c = _.peek();
                    let digit: u32 = 0;
                    if (c >= '0' && c <= '9') {
                        digit = @as(u32, c - '0');
                    } else if (hex && c >= 'a' && c <= 'f') {
                        digit = @as(u32, c - 'a' + 10);
                    } else if (hex && c >= 'A' && c <= 'F') {
                        digit = @as(u32, c - 'A' + 10);
                    } else {
                        break;
                    }
                    value = value *| (if (hex) @as(u32, 16) else @as(u32, 10)) +| digit;
                    digits += 1;
                    _.pos += 1;
                }
                if (digits == 0 || _.peek() != ';') {
                    return _.errHere("Invalid character reference: expected a number followed by ';' but found");
                }
                _.pos += 1;
                if (!types.isXmlChar(value)) {
                    const text = _.src[text_start.._.pos];
                    return _.errNamed(ref_pos, "Character reference", text, " is not a valid XML character");
                }
                return value;
            }

            fn pushCodePoint(buf: *std.ArrayListUnmanaged(U), cp: u32): XMLError!void {
                if (comptime is_wide) {
                    if (cp < 0x10000) {
                        buf.append(_.allocator, @as(U, @intCast(cp))) catch return error.OutOfMemory;
                    } else {
                        const v = cp - 0x10000;
                        buf.append(_.allocator, @as(U, @intCast(0xD800 + (v >> 10)))) catch return error.OutOfMemory;
                        buf.append(_.allocator, @as(U, @intCast(0xDC00 + (v & 0x3FF)))) catch return error.OutOfMemory;
                    }
                    return;
                }
                if (_.latin1) {
                    if (cp > 0xFF) return error.NeedsWiderEncoding;
                    buf.append(_.allocator, @as(U, @intCast(cp))) catch return error.OutOfMemory;
                    return;
                }
                let tmp: [4]u8 = [0, 0, 0, 0];
                const n = bun.strings.encodeWTF8Rune(&tmp, @as(i32, @intCast(cp)));
                buf.appendSlice(_.allocator, tmp[0..n]) catch return error.OutOfMemory;
            }

            static fn materialize(
                allocator: std.mem.Allocator,
                src: Slice,
                start: usize,
                end: usize,
                buf: *(std.ArrayListUnmanaged(U)?),
            ): XMLError!*std.ArrayListUnmanaged(U) {
                if (buf.* == null) {
                    let b: std.ArrayListUnmanaged(U) = .{};
                    b.ensureTotalCapacity(allocator, end - start + 32) catch return error.OutOfMemory;
                    b.appendSliceAssumeCapacity(src[start..end]);
                    buf.* = b;
                }
                return &(buf.*.?) ;
            }

            fn scanSimpleLiteral(*_, quote: u8, open: usize, literal: Literal): XMLError!Slice {
                const start = _.pos;
                while (true) {
                    if (_.atEnd()) return _.errMsg(open, "Unterminated quoted string");
                    const c = _.peek();
                    if (c == quote) {
                        const value = _.src[start.._.pos];
                        _.pos += 1;
                        return value;
                    }
                    if (literal == .Pubid && !types.isPubidChar(c)) {
                        return _.errHere("Invalid character in a public identifier:");
                    }
                    if ((c == '<' || c == '>') && literal == .Plain) {
                        return _.errHere("Invalid character in a quoted string:");
                    }
                    if (c == '>') {
                        _.sawGtInLiteral();
                        _.pos += 1;
                    } else if (c >= 0x80) {
                        _.pos += _.checkNonAsciiChar()?;
                    } else if (c < 0x20 && !types.isWs(c)) {
                        return _.errInvalidChar();
                    } else {
                        _.pos += 1;
                    }
                }
            }

            fn scanAttValue(*_, quote: u8, collapse: bool): XMLError!Slice {
                if (_.inDocument() && !_.tag_degraded && _.idx != null && !collapse) {
                    const start = _.pos;
                    const stop = _.nextStop(STOP_ATT_VALUE);
                    if (stop < _.src.len && UT.unitLow(_.src[stop]) == quote) {
                        _.pos = stop + 1;
                        return _.src[start..stop];
                    }
                }
                return _.scanAttValueGeneral(quote, collapse);
            }

            fn scanAttValueGeneral(*_, quote: u8, collapse: bool): XMLError!Slice {
                const literal_frame = _.frame_id;
                const open_pos = _.here();
                const start = _.pos;
                let buf: std.ArrayListUnmanaged(U)? = null;
                while (true) {
                    const stop = _.nextStop(STOP_ATT_VALUE);
                    if (stop != _.pos) {
                        if (*b := buf) {
                            b.appendSlice(_.allocator, _.src[_.pos..stop]) catch return error.OutOfMemory;
                        }
                        _.pos = stop;
                    }
                    if (_.atEnd()) {
                        if (_.frame_id != literal_frame) {
                            _.popFrame();
                            continue;
                        }
                        return _.errMsg(open_pos, "Unterminated attribute value");
                    }
                    const c = _.peek();
                    if (c == quote && _.frame_id == literal_frame) {
                        const end = _.pos;
                        _.pos += 1;
                        const value = if (b := buf) b.items else _.src[start..end];
                        return if (collapse) collapseSpaces(_.allocator, value)? else value;
                    }
                    if (c == '<') {
                        return _.errMsg(_.here(), "'<' is not allowed in attribute values");
                    }
                    if (c == '&') {
                        const ref_pos = _.here();
                        const b = materialize(_.allocator, _.src, start, _.pos, &buf)?;
                        _.pos += 1;
                        if (_.peek() == '#') {
                            _.pos += 1;
                            const cp = _.scanCharRef(ref_pos)?;
                            _.pushCodePoint(b, cp)?;
                        } else {
                            const name = _.scanReferenceName("Expected an entity name after '&' but found")?;
                            const res = _.resolveGeneralEntity(name, ref_pos, true)?;
                            switch (res) {
                                .byte byte => b.append(_.allocator, UT.unitAscii(byte)) catch return error.OutOfMemory,
                                .text text => _.pushFrame(text, .Literal, name, false, ref_pos)?,
                                .unexpanded => pushReference(_.allocator, b, name)?,
                            }
                        }
                    } else if (c == '\r' && _.inDocument()) {
                        const b = materialize(_.allocator, _.src, start, _.pos, &buf)?;
                        b.append(_.allocator, UT.unitAscii(' ')) catch return error.OutOfMemory;
                        _.pos += 1;
                        if (_.peek() == '\n') _.pos += 1;
                    } else if (c == '\t' || c == '\n' || c == '\r') {
                        const b = materialize(_.allocator, _.src, start, _.pos, &buf)?;
                        b.append(_.allocator, UT.unitAscii(' ')) catch return error.OutOfMemory;
                        _.pos += 1;
                    } else if (c < 0x20 || (c >= 0x80 && !_.latin1)) {
                        return _.errAtEntry(c);
                    } else {
                        if (c == '>') _.sawGtInLiteral();
                        if (*b := buf) {
                            b.append(_.allocator, _.unit()) catch return error.OutOfMemory;
                        }
                        _.pos += 1;
                    }
                }
            }

            fn scanEntityValue(*_, quote: u8): XMLError!Slice {
                const literal_frame = _.frame_id;
                const in_internal_subset = _.inDocument();
                const open_pos = _.here();
                let buf: std.ArrayListUnmanaged(U) = .{};
                buf.ensureTotalCapacity(_.allocator, 32) catch return error.OutOfMemory;
                while (true) {
                    if (_.atEnd()) {
                        if (_.frame_id != literal_frame) {
                            _.popFrame();
                            continue;
                        }
                        return _.errMsg(open_pos, "Unterminated entity value");
                    }
                    const c = _.peek();
                    if (c == quote && _.frame_id == literal_frame) {
                        _.pos += 1;
                        return buf.items;
                    }
                    if (c == '%') {
                        const ref_pos = _.here();
                        _.pos += 1;
                        const name = _.scanReferenceName("Expected a parameter entity name after '%' but found")?;
                        if (in_internal_subset) {
                            return _.errMsg(ref_pos, "Parameter entity references are not allowed inside markup declarations in the internal subset");
                        }
                        _.saw_pe_reference = true;
                        if (ev := _.entities.parameter.get(name)) {
                            switch (ev) {
                                .internal text => _.pushFrame(text, .Literal, name, true, ref_pos)?,
                                else => return _.errNamed(ref_pos, "External parameter entity", name, " cannot be included because external entities are not loaded"),
                            }
                        } else {
                            return _.errNamed(ref_pos, "Parameter entity", name, " is not declared");
                        }
                    } else if (c == '&') {
                        const ref_pos = _.here();
                        _.pos += 1;
                        if (_.peek() == '#') {
                            _.pos += 1;
                            const cp = _.scanCharRef(ref_pos)?;
                            _.pushCodePoint(&buf, cp)?;
                        } else {
                            const name = _.scanReferenceName("Expected an entity name after '&' but found")?;
                            pushReference(_.allocator, &buf, name)?;
                        }
                    } else if (c == '\r' && _.inDocument()) {
                        buf.append(_.allocator, UT.unitAscii('\n')) catch return error.OutOfMemory;
                        _.pos += 1;
                        if (_.peek() == '\n') _.pos += 1;
                    } else if (c < 0x20 && !types.isWs(c)) {
                        return _.errInvalidChar();
                    } else {
                        if (c == '>') _.sawGtInLiteral();
                        const len = if (c >= 0x80) _.checkNonAsciiChar()? else 1;
                        buf.appendSlice(_.allocator, _.src[_.pos .. _.pos + len]) catch return error.OutOfMemory;
                        _.pos += len;
                    }
                }
            }

            fn skipDropped(*_, limit: usize): XMLError!void {
                while (true) {
                    const stop = @min(_.nextStop(STOP_SKIPPED), limit);
                    _.pos = stop;
                    if (stop >= limit) return;
                    const c = _.peek();
                    if ((c < 0x20 && !types.isWs(c)) || (c >= 0x80 && !_.latin1)) {
                        return _.errAtEntry(c);
                    }
                    _.pos += 1;
                }
            }

            fn kept(*_, start: usize, end: usize): Slice {
                if (!_.keep_markup) return [];
                const raw = _.src[start..end];
                if (_.frame_kind != .Document) return raw;
                if (UT.findAscii(raw, "\r") == null) return raw;
                let out: std.ArrayListUnmanaged(U) = .{};
                out.ensureTotalCapacity(_.allocator, raw.len) catch return raw;
                let i: usize = 0;
                while (i < raw.len) : (i += 1) {
                    if (UT.unitLow(raw[i]) == '\r') {
                        out.appendAssumeCapacity(UT.unitAscii('\n'));
                        if (i + 1 < raw.len && UT.unitLow(raw[i + 1]) == '\n') {
                            i += 1;
                        }
                    } else {
                        out.appendAssumeCapacity(raw[i]);
                    }
                }
                return out.items;
            }

            fn scanComment(*_, start_pos: usize): XMLError!Slice {
                const body_start = _.pos;
                const rel = UT.findAscii(_.src[_.pos..], "--");
                const dashes: usize? = if (i := rel) _.pos + i else null;
                _.skipDropped(dashes ?? _.src.len)?;
                if (d := dashes) {
                    if (_.peekAt(d + 2) == '>') {
                        _.pos = d + 3;
                        return _.kept(body_start, d);
                    }
                    return _.errMsg(_.here(), "'--' is not allowed inside a comment");
                }
                return _.errMsg(start_pos, "Unterminated comment");
            }

            fn scanPi(*_, start_pos: usize): XMLError!Pi? {
                const at_document_start = _.inDocument() && start_pos == _.content_start;
                const target = _.scanName("Expected a processing instruction target after '<?' but found")?;
                if (UT.eqAscii(target, "xml") && at_document_start) {
                    return null;
                }
                if (UT.eqAsciiIgnoreCase(target, "xml")) {
                    return _.errMsg(start_pos, "'<?xml' is reserved for the XML declaration, which is only allowed at the very start of the document");
                }
                if (_.hasPrefix("?>")) {
                    _.pos += 2;
                    return .{ target: target, data: [] };
                }
                if (!types.isWs(_.peek())) {
                    return _.errHere("Expected whitespace or '?>' after the processing instruction target but found");
                }
                while (!_.atEnd() && types.isWs(_.peek())) {
                    _.pos += 1;
                }
                const data_start = _.pos;
                const rel = UT.findAscii(_.src[_.pos..], "?>");
                const close: usize? = if (i := rel) _.pos + i else null;
                _.skipDropped(close ?? _.src.len)?;
                if (end := close) {
                    _.pos = end + 2;
                    return .{ target: target, data: _.kept(data_start, end) };
                }
                return _.errMsg(start_pos, "Unterminated processing instruction");
            }

            fn scanCdata(*_, start_pos: usize, out: *std.ArrayListUnmanaged(U)): XMLError!void {
                const rel = UT.findAscii(_.src[_.pos..], "]]>");
                const close: usize? = if (i := rel) _.pos + i else null;
                const limit = close ?? _.src.len;
                while (true) {
                    const stop = @min(_.nextStop(STOP_SKIPPED), limit);
                    out.appendSlice(_.allocator, _.src[_.pos..stop]) catch return error.OutOfMemory;
                    _.pos = stop;
                    if (stop >= limit) break;
                    const c = _.peek();
                    if (c == '\r' && _.inDocument()) {
                        out.append(_.allocator, UT.unitAscii('\n')) catch return error.OutOfMemory;
                        _.pos += 1;
                        if (_.peek() == '\n') _.pos += 1;
                    } else if ((c < 0x20 && !types.isWs(c)) || (c >= 0x80 && !_.latin1)) {
                        return _.errAtEntry(c);
                    } else {
                        out.append(_.allocator, _.unit()) catch return error.OutOfMemory;
                        _.pos += 1;
                    }
                }
                if (close == null) {
                    return _.errMsg(start_pos, "Unterminated CDATA section");
                }
                _.pos = limit + 3;
            }

            fn next(*_, literal: Literal): XMLError!void {
                let spaced = false;
                let kind: Kind = .{ .eof = null };
                let cur_pos: usize = 0;
                while (true) {
                    cur_pos = _.here();
                    if (_.atEnd()) {
                        if (_.frame_kind == .Declarations) {
                            _.popFrame();
                            spaced = true;
                            continue;
                        }
                        kind = .{ .eof = _.frame_entity_name };
                        break;
                    }
                    const c = _.peek();
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                        _.pos += 1;
                        spaced = true;
                        continue;
                    }
                    if (c == '<') {
                        if (_.inDocument()) _.tag_degraded = false;
                        const nxt = _.peekAt(_.pos + 1);
                        if (nxt == '?') {
                            _.pos += 2;
                            if (pi_res := _.scanPi(cur_pos)?) {
                                kind = .{ .pi = pi_res };
                            } else {
                                kind = .xml_decl;
                            }
                            break;
                        }
                        if (nxt == '/') {
                            _.pos += 2;
                            const name = _.scanName("Expected an element name after '</' but found")?;
                            kind = .{ .end_tag = name };
                            break;
                        }
                        if (nxt == '!') {
                            if (_.hasPrefix("<!--")) {
                                _.pos += 4;
                                const body = _.scanComment(cur_pos)?;
                                kind = .{ .comment = body };
                                break;
                            }
                            if (_.hasPrefix("<!DOCTYPE")) {
                                _.pos += 9;
                                kind = .{ .decl = .Doctype };
                                break;
                            }
                            if (_.hasPrefix("<!ELEMENT")) {
                                _.pos += 9;
                                kind = .{ .decl = .Element };
                                break;
                            }
                            if (_.hasPrefix("<!ATTLIST")) {
                                _.pos += 9;
                                kind = .{ .decl = .Attlist };
                                break;
                            }
                            if (_.hasPrefix("<!ENTITY")) {
                                _.pos += 8;
                                kind = .{ .decl = .Entity };
                                break;
                            }
                            if (_.hasPrefix("<!NOTATION")) {
                                _.pos += 10;
                                kind = .{ .decl = .Notation };
                                break;
                            }
                            if (_.hasPrefix("<![CDATA[")) {
                                return _.errMsg(cur_pos, "CDATA sections are only allowed inside elements");
                            }
                            if (_.hasPrefix("<![")) {
                                return _.errMsg(cur_pos, "Conditional sections are only allowed in the external DTD subset");
                            }
                            return _.errMsg(cur_pos, "'<!' must begin a comment, '<![CDATA[', or a DOCTYPE, ELEMENT, ATTLIST, ENTITY or NOTATION declaration");
                        }
                        _.pos += 1;
                        const name = _.scanName("Expected an element name after '<' but found")?;
                        kind = .{ .start_tag = name };
                        break;
                    }
                    if (c == '"' || c == '\'') {
                        _.pos += 1;
                        switch (literal) {
                            .None => {
                                kind = .{ .unexpected = @as(u32, c) };
                                break;
                            },
                            .AttValue collapse => {
                                const v = _.scanAttValue(c, collapse)?;
                                kind = .{ .literal = v };
                                break;
                            },
                            .EntityValue => {
                                const v = _.scanEntityValue(c)?;
                                kind = .{ .literal = v };
                                break;
                            },
                            .Plain, .System, .Pubid => {
                                const v = _.scanSimpleLiteral(c, cur_pos, literal)?;
                                kind = .{ .literal = v };
                                break;
                            },
                        }
                    }
                    if (c == '=') { _.pos += 1; kind = .eq; break; }
                    if (c == '>') { _.pos += 1; if (_.inDocument()) _.tag_degraded = false; kind = .gt; break; }
                    if (c == '/') {
                        _.pos += 1;
                        if (_.peek() != '>') return _.errHere("Expected '>' after '/' but found");
                        _.pos += 1;
                        if (_.inDocument()) _.tag_degraded = false;
                        kind = .slash_gt;
                        break;
                    }
                    if (c == '(') { _.pos += 1; kind = .paren_open; break; }
                    if (c == ')') { _.pos += 1; kind = .paren_close; break; }
                    if (c == '|') { _.pos += 1; kind = .bar; break; }
                    if (c == ',') { _.pos += 1; kind = .comma; break; }
                    if (c == '?') { _.pos += 1; kind = .question; break; }
                    if (c == '*') { _.pos += 1; kind = .star; break; }
                    if (c == '+') { _.pos += 1; kind = .plus; break; }
                    if (c == '[') { _.pos += 1; kind = .bracket_open; break; }
                    if (c == ']') { _.pos += 1; kind = .bracket_close; break; }
                    if (c == '#') {
                        _.pos += 1;
                        const kw = _.scanName("Expected a keyword after '#' but found")?;
                        kind = .{ .hash = kw };
                        break;
                    }
                    if (c == '%') {
                        _.pos += 1;
                        if (!_.atNameStart()) {
                            kind = .percent;
                            break;
                        }
                        const name = _.scanName("Expected a parameter entity name after '%' but found")?;
                        if (_.peek() != ';') {
                            kind = .{ .percent_name = name };
                            break;
                        }
                        _.pos += 1;
                        if (_.frame_kind != .Declarations) {
                            kind = .{ .pe_reference = name };
                            break;
                        }
                        _.includeParameterEntity(name, cur_pos)?;
                        spaced = true;
                        continue;
                    }
                    if (c < 0x20) return _.errInvalidChar();
                    if (run := _.scanNameRun()) {
                        kind = if (run.is_name) .{ .name = run.slice } else .{ .nmtoken = run.slice };
                        break;
                    }
                    const len = if (c >= 0x80) _.checkNonAsciiChar()? else 1;
                    const cp = _.decodeUtf8()[0];
                    _.pos += len;
                    kind = .{ .unexpected = cp };
                    break;
                }
                _.tok = Token{ kind: kind, pos: cur_pos, frame: _.frame_id, spaced: spaced };
            }

            fn tagStep(*_): TagStep {
                let p = _.pos;
                const unspaced = p;
                while (types.isWs(_.peekAt(p))) : (p += 1) {}
                const c = _.peekAt(p);
                if (c == '>') {
                    _.pos = p + 1;
                    _.tag_degraded = false;
                    return TagStep{ .end = false };
                }
                if (c == '/' && _.peekAt(p + 1) == '>') {
                    _.pos = p + 2;
                    _.tag_degraded = false;
                    return TagStep{ .end = true };
                }
                if (p > unspaced && types.isNameStartAscii(c)) {
                    const name_start = p;
                    p += 1;
                    while (types.isNameCharAscii(_.peekAt(p))) : (p += 1) {}
                    if (_.peekAt(p) >= 0x80) return TagStep.slow;
                    const name = _.src[name_start..p];
                    while (types.isWs(_.peekAt(p))) : (p += 1) {}
                    if (_.peekAt(p) != '=') return TagStep.slow;
                    p += 1;
                    while (types.isWs(_.peekAt(p))) : (p += 1) {}
                    const q = _.peekAt(p);
                    if (q == '"' || q == '\'') {
                        _.pos = p + 1;
                        return TagStep{ .attr = .{ name: name, pos: name_start, quote: q } };
                    }
                }
                return TagStep.slow;
            }

            fn takeGt(*_): bool {
                let p = _.pos;
                while (p < _.src.len && types.isWs(UT.unitLow(_.src[p]))) : (p += 1) {}
                if (p < _.src.len && UT.unitLow(_.src[p]) == '>') {
                    _.pos = p + 1;
                    if (_.inDocument()) _.tag_degraded = false;
                    return true;
                }
                return false;
            }

            fn nextContent(*_): XMLError!void {
                let start = _.pos;
                const text_pos = _.here();
                let buf: std.ArrayListUnmanaged(U)? = null;

                while (true) {
                    const stop = _.nextStop(STOP_CONTENT);
                    if (stop != _.pos) {
                        _.pos = stop;
                        if (*b := buf) {
                            b.appendSlice(_.allocator, _.src[start.._.pos]) catch return error.OutOfMemory;
                            start = _.pos;
                        }
                    }
                    if (_.atEnd()) {
                        if (_.frame_kind == .Content) {
                            * = materialize(_.allocator, _.src, start, _.pos, &buf)?;
                            _.popFrame();
                            start = _.pos;
                            continue;
                        }
                        const has_text = if (b := buf) (b.items.len > 0 || _.pos > start) else (_.pos > start);
                        if (has_text) {
                            _.finishText(start, buf, text_pos)?;
                            return;
                        }
                        _.tok = Token{
                            kind: .{ .eof = _.frame_entity_name },
                            pos: _.here(),
                            frame: _.frame_id,
                            spaced: false,
                        };
                        return;
                    }
                    const c = _.peek();
                    if (c == '<') {
                        if (_.inDocument()) _.tag_degraded = false;
                        const cur_pos = _.here();
                        const nxt = _.peekAt(_.pos + 1);
                        if (nxt == '?' || (nxt == '!' && _.hasPrefix("<!--"))) {
                            if (!_.keep_markup) {
                                * = materialize(_.allocator, _.src, start, _.pos, &buf)?;
                            } else {
                                const has_text = if (b := buf) (b.items.len > 0 || _.pos > start) else (_.pos > start);
                                if (has_text) {
                                    _.finishText(start, buf, text_pos)?;
                                    return;
                                }
                            }
                            const frame = _.frame_id;
                            let kind: Kind = .{ .eof = null };
                            if (nxt == '?') {
                                _.pos += 2;
                                if (pi_res := _.scanPi(cur_pos)?) {
                                    kind = .{ .pi = pi_res };
                                } else {
                                    unreachable;
                                }
                            } else {
                                _.pos += 4;
                                const body = _.scanComment(cur_pos)?;
                                kind = .{ .comment = body };
                            }
                            if (_.keep_markup) {
                                _.tok = Token{ kind: kind, pos: cur_pos, frame: frame, spaced: false };
                                return;
                            }
                            start = _.pos;
                            continue;
                        }
                        if (nxt == '!' && _.hasPrefix("<![CDATA[")) {
                            const b = materialize(_.allocator, _.src, start, _.pos, &buf)?;
                            _.pos += 9;
                            _.scanCdata(cur_pos, b)?;
                            start = _.pos;
                            continue;
                        }
                        const has_text = if (b := buf) (b.items.len > 0 || _.pos > start) else (_.pos > start);
                        if (has_text) {
                            _.finishText(start, buf, text_pos)?;
                            return;
                        }
                        if (nxt == '!') {
                            return _.errMsg(cur_pos, "Expected a comment or CDATA section after '<!'");
                        }
                        const frame = _.frame_id;
                        let kind: Kind = .{ .eof = null };
                        if (nxt == '/') {
                            _.pos += 2;
                            kind = .{ .end_tag = _.scanName("Expected an element name after '</' but found")? };
                        } else {
                            _.pos += 1;
                            kind = .{ .start_tag = _.scanName("Expected an element name after '<' but found")? };
                        }
                        _.tok = Token{ kind: kind, pos: cur_pos, frame: frame, spaced: false };
                        return;
                    }
                    if (c == '&') {
                        const ref_pos = _.here();
                        const b = materialize(_.allocator, _.src, start, _.pos, &buf)?;
                        _.pos += 1;
                        if (_.peek() == '#') {
                            _.pos += 1;
                            const cp = _.scanCharRef(ref_pos)?;
                            _.pushCodePoint(b, cp)?;
                        } else {
                            const name = _.scanReferenceName("Expected an entity name after '&' but found")?;
                            const res = _.resolveGeneralEntity(name, ref_pos, false)?;
                            switch (res) {
                                .byte byte => b.append(_.allocator, UT.unitAscii(byte)) catch return error.OutOfMemory,
                                .text text => _.pushFrame(text, .Content, name, false, ref_pos)?,
                                .unexpanded => pushReference(_.allocator, b, name)?,
                            }
                        }
                        start = _.pos;
                        continue;
                    }
                    if (c == '>') {
                        if (_.pos >= 2 && UT.eqAscii(_.src[_.pos - 2 .. _.pos], "]]")) {
                            const at = if (_.inDocument()) _.pos - 2 else _.here();
                            return _.errMsg(at, "']]>' is only allowed as the end of a CDATA section");
                        }
                        if (*b := buf) {
                            b.append(_.allocator, _.unit()) catch return error.OutOfMemory;
                            start = _.pos + 1;
                        }
                        _.pos += 1;
                        continue;
                    }
                    if (c == '\r' && _.frame_kind == .Document) {
                        const b = materialize(_.allocator, _.src, start, _.pos, &buf)?;
                        b.append(_.allocator, UT.unitAscii('\n')) catch return error.OutOfMemory;
                        _.pos += 1;
                        if (_.peek() == '\n') _.pos += 1;
                        start = _.pos;
                        continue;
                    }
                    if (c == '\r' || (c >= 0x80 && _.latin1)) {
                        if (*b := buf) {
                            b.append(_.allocator, _.unit()) catch return error.OutOfMemory;
                            start = _.pos + 1;
                        }
                        _.pos += 1;
                        continue;
                    }
                    return _.errAtEntry(c);
                }
            }

            fn finishText(*_, start: usize, buf: std.ArrayListUnmanaged(U)?, pos: usize): XMLError!void {
                let text: Slice = [];
                if (mut_b := buf) {
                    let b = mut_b;
                    b.appendSlice(_.allocator, _.src[start.._.pos]) catch return error.OutOfMemory;
                    text = b.items;
                } else {
                    text = _.src[start.._.pos];
                }
                _.tok = Token{
                    kind: .{ .text = text },
                    pos: pos,
                    frame: _.frame_id,
                    spaced: false,
                };
            }
        };
    };
}
