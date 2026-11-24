const hex_chars = "0123456789ABCDEF";
let empty_comments: [0]G.Comment = undefined;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct OptDeinitGuard<T> {
    ptr: (*T!)? = null,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(); }
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    alloc: std.mem.Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.alloc); }
    }
}

pub struct OptDeinitWithAllocGuard<T> {
    ptr: (*T!)? = null,
    alloc: std.mem.Allocator = undefined,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.deinit(_.alloc); }
        }
    }
}

pub struct ClearAndFreeGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.clearAndFree(); }
    }
}

pub struct EndGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.end(); }
    }
}
const first_ascii = 0x20;
const last_ascii = 0x7E;
const first_high_surrogate = 0xD800;
const first_low_surrogate = 0xDC00;
const last_low_surrogate = 0xDFFF;

/// For support JavaScriptCore
const ascii_only_always_on_unless_minifying = true;

fn formatUnsignedIntegerBetween(comptime len: u16, buf: *[len]u8, val: u64) {
    comptime let i: u16 = len;
    let remainder = val;

    // Write out the number from the end to the front
    inline while (i > 0) {
        i -= 1;
        buf[i] = @as(u8, @intCast((remainder % 10))) + '0';
        remainder /= 10;
    }
}

pub fn writeModuleId(comptime Writer: type, writer: Writer, module_id: u32) {
    bun.assert(module_id != 0); // either module_id is forgotten or it should be disabled
    * = writer.writeAll("$") catch unreachable;
    std.fmt.formatInt(module_id, 16, .lower, {}, writer) catch unreachable;
}

pub fn canPrintWithoutEscape(comptime CodePointType: type, c: CodePointType, comptime ascii_only: bool): bool {
    if (c <= last_ascii) {
        return c >= first_ascii && c != '\\' && c != '"' && c != '\'' && c != '`' && c != '$';
    } else {
        return !ascii_only && c != 0xFEFF && c != 0x2028 && c != 0x2029 && (c < first_high_surrogate || c > last_low_surrogate);
    }
}

const indentation_space_buf: [128]u8 = @splat(' ');
const indentation_tab_buf: [128]u8 = @splat('\t');

pub fn bestQuoteCharForString(comptime Type: type, str: []const Type, allow_backtick: bool): u8 {
    let single_cost: usize = 0;
    let double_cost: usize = 0;
    let backtick_cost: usize = 0;
    let i: usize = 0;
    while (i < @min(str.len, 1024)) {
        switch (str[i]) {
            '\'' => {
                single_cost += 1;
            },
            '"' => {
                double_cost += 1;
            },
            '`' => {
                backtick_cost += 1;
            },
            '\n' => {
                single_cost += 1;
                double_cost += 1;
            },
            '\\' => {
                i += 1;
            },
            '$' => {
                if (i + 1 < str.len && str[i + 1] == '{') {
                    backtick_cost += 1;
                }
            },
            else => {},
        }
        i += 1;
    }

    if (allow_backtick && backtick_cost < @min(single_cost, double_cost)) {
        return '`';
    }
    if (single_cost < double_cost) {
        return '\'';
    }
    return '"';
}

struct Whitespacer {
    normal: []const u8,
    minify: []const u8,

    fn append(comptime str: []const u8): Whitespacer {
        return { normal: _.normal ++ str, minify: _.minify ++ str };
    }
}

fn ws(comptime str: []const u8): Whitespacer {
    const without: []const u8 = comptime brk: {
        let buf = std.mem.zeroes([str.len]u8);
        let buf_i: usize = 0;
        for (c of str) {
            if (c != ' ') {
                buf[buf_i] = c;
                buf_i += 1;
            }
        }
        const final = buf[0..buf_i].*;
        break :brk &final;
    };

    return { normal: str, minify: without };
}

pub fn estimateLengthForUTF8(input: []const u8, comptime ascii_only: bool, comptime quote_char: u8): usize {
    let remaining = input;
    let len: usize = 2; // for quotes

    while (i := strings.indexOfNeedsEscapeForJavaScriptString(remaining, quote_char)) {
        len += i;
        remaining = remaining[i..];
        const char_len = strings.wtf8ByteSequenceLengthWithInvalid(remaining[0]);
        const c = strings.decodeWTF8RuneT(
            &switch (char_len) {
                // 0 is not returned by `wtf8ByteSequenceLengthWithInvalid`
                1 => .{ remaining[0], 0, 0, 0 },
                2 => remaining[0..2].* ++ .{ 0, 0 },
                3 => remaining[0..3].* ++ .{0},
                4 => remaining[0..4].*,
                else => unreachable,
            },
            char_len,
            i32,
            0,
        );
        if (canPrintWithoutEscape(i32, c, ascii_only)) {
            len += @as(usize, char_len);
        } else if (c <= 0xFFFF) {
            len += 6;
        } else {
            len += 12;
        }
        remaining = remaining[char_len..];
    }

    return remaining.len + 2;
}

pub fn writePreQuotedString(text_in: []const u8, comptime Writer: type, writer: Writer, comptime quote_char: u8, comptime ascii_only: bool, comptime json: bool, comptime encoding: strings.Encoding)! {
    const text = if (comptime encoding == .utf16) @as([]const u16, std.mem.bytesAsSlice(u16, text_in)) else text_in;
    if (comptime json && quote_char != '"') @compileError("for json, quote_char must be '\"'");
    let i: usize = 0;
    const n: usize = text.len;
    while (i < n) {
        const width = switch (comptime encoding) {
            .latin1, .ascii => 1,
            .utf8 => strings.wtf8ByteSequenceLengthWithInvalid(text[i]),
            .utf16 => 1,
        };
        const clamped_width = @min(@as(usize, width), n -| i);
        const c = switch (encoding) {
            .utf8 => strings.decodeWTF8RuneT(
                &switch (clamped_width) {
                    // 0 is not returned by `wtf8ByteSequenceLengthWithInvalid`
                    1 => .{ text[i], 0, 0, 0 },
                    2 => text[i..][0..2].* ++ .{ 0, 0 },
                    3 => text[i..][0..3].* ++ .{0},
                    4 => text[i..][0..4].*,
                    else => unreachable,
                },
                width,
                i32,
                0,
            ),
            .ascii => brk: {
                std.debug.assert(text[i] <= 0x7F);
                break :brk text[i];
            },
            .latin1 => text[i],
            .utf16 => brk: {
                // TODO: if this is a part of a surrogate pair, we could parse the whole codepoint in order
                // to emit it as a single \u{result} rather than two paired \uLOW\uHIGH.
                // eg: "\u{10334}" will convert to "\uD800\uDF34" without this.
                break :brk @as(i32, text[i]);
            },
        };
        if (canPrintWithoutEscape(i32, c, ascii_only)) {
            const remain = text[i + clamped_width ..];

            switch (encoding) {
                .ascii, .utf8 => {
                    if (j := strings.indexOfNeedsEscapeForJavaScriptString(remain, quote_char)) {
                        const text_chunk = text[i .. i + clamped_width];
                        writer.writeAll(text_chunk)?;
                        i += clamped_width;
                        writer.writeAll(remain[0..j])?;
                        i += j;
                    } else {
                        writer.writeAll(text[i..])?;
                        i = n;
                        break;
                    }
                },
                .latin1, .utf16 => {
                    let codepoint_bytes: [4]u8 = undefined;
                    const codepoint_len = strings.encodeWTF8Rune(codepoint_bytes[0..4], c);
                    writer.writeAll(codepoint_bytes[0..codepoint_len])?;
                    i += clamped_width;
                },
            }
            continue;
        }
        switch (c) {
            0x07 => {
                writer.writeAll("\\x07")?;
                i += 1;
            },
            0x08 => {
                writer.writeAll("\\b")?;
                i += 1;
            },
            0x0C => {
                writer.writeAll("\\f")?;
                i += 1;
            },
            '\n' => {
                if (quote_char == '`') {
                    writer.writeAll("\n")?;
                } else {
                    writer.writeAll("\\n")?;
                }
                i += 1;
            },
            0x0D => {
                writer.writeAll("\\r")?;
                i += 1;
            },
            // \v
            0x0B => {
                writer.writeAll("\\v")?;
                i += 1;
            },
            // "\\"
            '\\' => {
                writer.writeAll("\\\\")?;
                i += 1;
            },
            '"' => {
                if (quote_char == '"') {
                    writer.writeAll("\\\"")?;
                } else {
                    writer.writeAll("\"")?;
                }
                i += 1;
            },
            '\'' => {
                if (quote_char == '\'') {
                    writer.writeAll("\\'")?;
                } else {
                    writer.writeAll("'")?;
                }
                i += 1;
            },
            '`' => {
                if (quote_char == '`') {
                    writer.writeAll("\\`")?;
                } else {
                    writer.writeAll("`")?;
                }
                i += 1;
            },
            '$' => {
                if (quote_char == '`') {
                    const remain = text[i + clamped_width ..];
                    if (remain.len > 0 && remain[0] == '{') {
                        writer.writeAll("\\$")?;
                    } else {
                        writer.writeAll("$")?;
                    }
                } else {
                    writer.writeAll("$")?;
                }
                i += 1;
            },

            '\t' => {
                if (quote_char == '`') {
                    writer.writeAll("\t")?;
                } else {
                    writer.writeAll("\\t")?;
                }
                i += 1;
            },

            else => {
                i += @as(usize, width);

                if (c <= 0xFF && !json) {
                    const k = @as(usize, @intCast(c));

                    const buf: [4]u8 = [
                        '\\',
                        'x',
                        hex_chars[(k >> 4) & 0xF],
                        hex_chars[k & 0xF],
                    ];
                    writer.writeAll(&buf)?;
                } else if (c <= 0xFFFF) {
                    const k = @as(usize, @intCast(c));

                    const buf: [6]u8 = [
                        '\\',
                        'u',
                        hex_chars[(k >> 12) & 0xF],
                        hex_chars[(k >> 8) & 0xF],
                        hex_chars[(k >> 4) & 0xF],
                        hex_chars[k & 0xF],
                    ];
                    writer.writeAll(&buf)?;
                } else {
                    const k = c - 0x10000;
                    const lo = @as(usize, @intCast(first_high_surrogate + ((k >> 10) & 0x3FF)));
                    const hi = @as(usize, @intCast(first_low_surrogate + (k & 0x3FF)));

                    const buf: [12]u8 = [
                        '\\',
                        'u',
                        hex_chars[lo >> 12],
                        hex_chars[(lo >> 8) & 15],
                        hex_chars[(lo >> 4) & 15],
                        hex_chars[lo & 15],
                        '\\',
                        'u',
                        hex_chars[hi >> 12],
                        hex_chars[(hi >> 8) & 15],
                        hex_chars[(hi >> 4) & 15],
                        hex_chars[hi & 15],
                    ];
                    writer.writeAll(&buf)?;
                }
            },
        }
    }
}
pub fn quoteForJSON(text: []const u8, bytes: *MutableString, comptime ascii_only: bool)! {
    const writer = bytes.writer();

    bytes.growIfNeeded(estimateLengthForUTF8(text, ascii_only, '"'))?;
    bytes.appendChar('"')?;
    writePreQuotedString(text, @TypeOf(writer), writer, '"', ascii_only, true, .utf8)?;
    bytes.appendChar('"') catch unreachable;
}

pub fn writeJSONString(input: []const u8, comptime Writer: type, writer: Writer, comptime encoding: strings.Encoding)! {
    writer.writeAll("\"")?;
    writePreQuotedString(input, Writer, writer, '"', false, true, encoding)?;
    writer.writeAll("\"")?;
}

pub type OpaquePtr = *!;
pub type SourceMapHandlerCallback = *const fn (*!, chunk: SourceMap.Chunk, source: *const logger.Source) anyerror!void;

pub fn SourceMapHandlerFor(comptime Type: type, comptime handler: (fn (t: *Type, chunk: SourceMap.Chunk, source: *const logger.Source) anyerror!void)): type {
    return struct {
        static fn onChunk(ptr: *!, chunk: SourceMap.Chunk, source: *const logger.Source)! {
            handler(unsafe { @as(*Type, ptr) }, chunk, source)?;
        }

        static fn init(t: *Type): SourceMapHandler {
            return SourceMapHandler{ ctx: t, callback: onChunk };
        }
    };
}

pub struct SourceMapHandler {
    ctx: *!,
    callback: SourceMapHandlerCallback,

    pub const Callback = SourceMapHandlerCallback;
    pub const For = SourceMapHandlerFor;

    fn onSourceMapChunk(chunk: SourceMap.Chunk, source: *const logger.Source)! {
        const cb = _.callback;
        cb(_.ctx, chunk, source)?;
    }
}

pub struct Options {
    bundling: bool = false,
    transform_imports: bool = true,
    to_commonjs_ref: Ref = Ref.None,
    to_esm_ref: Ref = Ref.None,
    require_ref: Ref? = null,
    import_meta_ref: Ref = Ref.None,
    hmr_ref: Ref = Ref.None,
    indent: OptionsIndentation = {},
    runtime_imports: runtime.Runtime.Imports = runtime.Runtime.Imports{},
    module_hash: u32 = 0,
    source_path: fs.Path? = null,
    allocator: std.mem.Allocator = default_allocator,
    source_map_allocator: std.mem.Allocator? = null,
    source_map_handler: SourceMapHandler? = null,
    source_map_builder: (*bun.SourceMap.Chunk.Builder)? = null,
    css_import_behavior: api.CssInJsBehavior = api.CssInJsBehavior.facade,
    target: options.Target = .browser,

    runtime_transpiler_cache: (*bun.jsc.RuntimeTranspilerCache)? = null,
    module_info: (*analyze_transpiled_module.ModuleInfo)? = null,
    input_files_for_dev_server: ([]logger.Source)? = null,

    commonjs_named_exports: js_ast.Ast.CommonJSNamedExports = {},
    commonjs_named_exports_deoptimized: bool = false,
    commonjs_module_exports_assigned_deoptimized: bool = false,
    commonjs_named_exports_ref: Ref = Ref.None,
    commonjs_module_ref: Ref = Ref.None,

    minify_whitespace: bool = false,
    minify_identifiers: bool = false,
    minify_syntax: bool = false,
    print_dce_annotations: bool = true,

    transform_only: bool = false,
    inline_require_and_import_errors: bool = true,
    has_run_symbol_renamer: bool = false,

    require_or_import_meta_for_source_callback: RequireOrImportMeta.Callback = {},

    /// The module type of the importing file (after linking), used to determine interop helper behavior.
    /// Controls whether __toESM uses Node ESM semantics (isNodeMode=1 for .esm) or respects __esModule markers.
    input_module_type: options.ModuleType = .unknown,
    module_type: options.Format = .esm,

    // /// Used for cross-module inlining of import items when bundling
    // const_values: Ast.ConstValuesMap = .{},
    ts_enums: Ast.TsEnumsMap = {},

    // If we're writing out a source map, this table of line start indices lets
    // us do binary search on to figure out what line a given AST node came from
    line_offset_tables: SourceMap.LineOffsetTable.List? = null,

    mangled_props: (*const bun.bundle_v2.MangledProps)?,

    pub const Indentation = OptionsIndentation;

    fn requireOrImportMetaForSource(
        id: u32,
        was_unwrapped_require: bool,
    ): RequireOrImportMeta {
        if (_.require_or_import_meta_for_source_callback.ctx == null)
            return {};

        return _.require_or_import_meta_for_source_callback.call(id, was_unwrapped_require);
    }
}

pub enum OptionsIndentationCharacter { tab, space }

pub struct OptionsIndentation {
    scalar: usize = 2,
    count: usize = 0,
    character: OptionsIndentationCharacter = .space,

    pub const Character = OptionsIndentationCharacter;
}

pub type RequireOrImportMetaCallbackFn = *const fn (OpaquePtr, u32, bool) RequireOrImportMeta;

pub struct RequireOrImportMetaCallback {
    ctx: *!? = null,
    callback: RequireOrImportMetaCallbackFn = undefined,

    pub const Fn = fn (OpaquePtr, u32, bool) RequireOrImportMeta;

    fn call(id: u32, was_unwrapped_require: bool): RequireOrImportMeta {
        const cb = _.callback;
        return cb(_.ctx.?, id, was_unwrapped_require);
    }

    static fn init(
        comptime Type: type,
        comptime callback: (fn (t: *Type, id: u32, was_unwrapped_require: bool) RequireOrImportMeta),
        ctx: *Type,
    ): RequireOrImportMetaCallback {
        return RequireOrImportMetaCallback{
            ctx: bun.cast(OpaquePtr, ctx),
            callback: unsafe { @as(RequireOrImportMetaCallbackFn, @ptrCast(callback)) },
        };
    }
}

pub struct RequireOrImportMeta {
    // CommonJS files will return the "require_*" wrapper function and an invalid
    // exports object reference. Lazily-initialized ESM files will return the
    // "init_*" wrapper function and the exports object for that file.
    wrapper_ref: Ref = Ref.None,
    exports_ref: Ref = Ref.None,
    is_wrapper_async: bool = false,
    was_unwrapped_require: bool = false,

    pub const Callback = RequireOrImportMetaCallback;
}

fn isIdentifierOrNumericConstantOrPropertyAccess(expr: *const Expr): bool {
    return switch (expr.data) {
        .e_identifier, .e_dot, .e_index => true,
        .e_number e => std.math.isInf(e.value) || std.math.isNan(e.value),
        else => false,
    };
}

pub struct PrintResultSuccess {
    code: []u8,
    source_map: SourceMap.Chunk? = null,
}

pub union(enum) PrintResult {
    result: PrintResultSuccess,
    @"err": anyerror,

    pub const Success = PrintResultSuccess;
}

// do not make this a packed struct
// stage1 compiler bug:
// > /optional-chain-with-function.js: Evaluation failed: TypeError: (intermediate value) is not a function
// this test failure was caused by the packed struct implementation
type U16Slice = []const u16;

enum ExprFlag {
    forbid_call,
    forbid_in,
    has_non_optional_chain_parent,
    expr_result_is_unused,
    pub const Set = std.enums.EnumSet(ExprFlag);

    static fn None(): ExprFlag.Set {
        return Set{};
    }

    static fn ForbidCall(): ExprFlag.Set {
        return Set.init({ forbid_call: true });
    }

    static fn ForbidAnd(): ExprFlag.Set {
        return Set.init({ forbid_and: true });
    }

    static fn HasNonOptionalChainParent(): ExprFlag.Set {
        return Set.init({ has_non_optional_chain_parent: true });
    }

    static fn ExprResultIsUnused(): ExprFlag.Set {
        return Set.init({ expr_result_is_unused: true });
    }
}

enum ImportVariant {
    path_only,
    import_star,
    import_default,
    import_star_and_import_default,
    import_items,
    import_items_and_default,
    import_items_and_star,
    import_items_and_default_and_star,

    fn hasItems(): @This() {
        return switch (_) {
            .import_default => .import_items_and_default,
            .import_star => .import_items_and_star,
            .import_star_and_import_default => .import_items_and_default_and_star,
            else => .import_items,
        };
    }

    // We always check star first so don't need to be exhaustive here
    fn hasStar(): @This() {
        return switch (_) {
            .path_only => .import_star,
            else => _,
        };
    }

    // We check default after star
    fn hasDefault(): @This() {
        return switch (_) {
            .path_only => .import_default,
            .import_star => .import_star_and_import_default,
            else => _,
        };
    }

    static fn determine(record: *const ImportRecord, s_import: *const S.Import): ImportVariant {
        let variant = ImportVariant.path_only;

        if (record.flags.contains_import_star) {
            variant = variant.hasStar();
        }

        if (!record.flags.was_originally_bare_import) {
            if (!record.flags.contains_default_alias) {
                if (default_name := s_import.default_name) {
                    if (default_name.ref != null) {
                        variant = variant.hasDefault();
                    }
                }
            } else {
                variant = variant.hasDefault();
            }
        }

        if (s_import.items.len > 0) {
            variant = variant.hasItems();
        }

        return variant;
    }
}

enum IsTopLevel { yes, var_only, no }

struct TopLevelAndIsExportEmpty {}

struct TopLevelAndIsExportFull {
    is_export: bool = false,
    is_top_level: analyze_transpiled_module.ModuleInfo.VarKind? = null,
}

struct TopLevelEmpty {
    static fn init(v: IsTopLevel): TopLevelEmpty {
        * = v;
        return {};
    }
    fn subVar(): TopLevelEmpty {
        return _;
    }
    fn isTopLevel(): bool {
        std.mem.doNotOptimizeAway(_);
        return false;
    }
}

struct TopLevelFull {
    is_top_level: IsTopLevel = .no,

    static fn init(is_top_level: IsTopLevel): TopLevelFull {
        return { is_top_level: is_top_level };
    }
    fn subVar(): TopLevelFull {
        if (_.is_top_level == .no) return TopLevelFull.init(.no);
        return TopLevelFull.init(.var_only);
    }
    fn isTopLevel(): bool {
        return _.is_top_level != .no;
    }
}

pub struct BinaryExpressionVisitorStruct {
    // Inputs
    e: *E.Binary,
    level: Level = .lowest,
    flags: ExprFlag.Set = ExprFlag.Set.initEmpty(),

    // Input for visiting the left child
    left_level: Level = .lowest,
    left_flags: ExprFlag.Set = ExprFlag.Set.initEmpty(),

    // "Local variables" passed from "checkAndPrepare" to "visitRightAndFinish"
    entry: *const Op = undefined,
    wrap: bool = false,
    right_level: Level = .lowest,

    fn checkAndPrepare(*_, p: auto): bool {
        let e = _.e;

        const entry: *const Op = Op.Table.getPtrConst(e.op);
        const e_level = entry.level;
        _.entry = entry;
        _.wrap = _.level.gte(e_level) || (e.op == Op.Code.bin_in && _.flags.contains(.forbid_in));

        // Destructuring assignments must be parenthesized
        const n = p.writer.written;
        if (n == p.stmt_start || n == p.arrow_expr_start) {
            switch (e.left.data) {
                .e_object => {
                    _.wrap = true;
                },
                else => {},
            }
        }

        if (_.wrap) {
            p.print("(");
            _.flags.insert(.forbid_in);
        }

        _.left_level = e_level.sub(1);
        _.right_level = e_level.sub(1);
        const left_level = &_.left_level;
        const right_level = &_.right_level;

        if (e.op.isRightAssociative()) {
            left_level.* = e_level;
        }

        if (e.op.isLeftAssociative()) {
            right_level.* = e_level;
        }

        switch (e.op) {
            // "??" can't directly contain "||" or "&&" without being wrapped in parentheses
            .bin_nullish_coalescing => {
                switch (e.left.data) {
                    .e_binary => {
                        const left = e.left.data.e_binary;
                        switch (left.op) {
                            .bin_logical_and, .bin_logical_or => {
                                left_level.* = .prefix;
                            },
                            else => {},
                        }
                    },
                    else => {},
                }

                switch (e.right.data) {
                    .e_binary => {
                        const right = e.right.data.e_binary;
                        switch (right.op) {
                            .bin_logical_and, .bin_logical_or => {
                                right_level.* = .prefix;
                            },
                            else => {},
                        }
                    },
                    else => {},
                }
            },
            // "**" can't contain certain unary expressions
            .bin_pow => {
                switch (e.left.data) {
                    .e_unary => {
                        const left = e.left.data.e_unary;
                        if (left.op.unaryAssignTarget() == .none) {
                            left_level.* = .call;
                        }
                    },
                    .e_await, .e_undefined, .e_number => {
                        left_level.* = .call;
                    },
                    .e_boolean, .e_branch_boolean => {
                        // When minifying, booleans are printed as "!0 and "!1"
                        if (p.options.minify_syntax) {
                            left_level.* = .call;
                        }
                    },
                    else => {},
                }
            },
            else => {},
        }

        // Special-case "#foo in bar"
        if (e.left.data == .e_private_identifier && e.op == .bin_in) {
            const priv_id = e.left.data.e_private_identifier;
            const name = p.renamer.nameForSymbol(priv_id.ref);
            p.addSourceMappingForName(e.left.loc, name, priv_id.ref);
            p.printIdentifier(name);
            _.visitRightAndFinish(p);
            return false;
        }

        _.left_flags = {};

        if (_.flags.contains(.forbid_in)) {
            _.left_flags.insert(.forbid_in);
        }

        if (e.op == .bin_comma)
            _.left_flags.insert(.expr_result_is_unused);

        return true;
    }
    fn visitRightAndFinish(*_, p: auto) {
        const e = _.e;
        const entry = _.entry;
        let flags = ExprFlag.Set.initEmpty();

        if (e.op != .bin_comma) {
            p.printSpace();
        }

        if (entry.is_keyword) {
            p.printSpaceBeforeIdentifier();
            p.print(entry.text);
        } else {
            p.printSpaceBeforeOperator(e.op);
            p.print(entry.text);
            p.prev_op = e.op;
            p.prev_op_end = p.writer.written;
        }

        p.printSpace();

        // The result of the right operand of the comma operator is unused if the caller doesn't use it
        if (e.op == .bin_comma && _.flags.contains(.expr_result_is_unused)) {
            flags.insert(.expr_result_is_unused);
        }

        if (_.flags.contains(.forbid_in)) {
            flags.insert(.forbid_in);
        }

        p.printExpr(e.right, _.right_level, flags);

        if (_.wrap) {
            p.print(")");
        }
    }
}


fn NewPrinter(
    comptime ascii_only: bool,
    comptime Writer: type,
    comptime rewrite_esm_to_cjs: bool,
    comptime is_bun_platform: bool,
    comptime is_json: bool,
    comptime generate_source_map: bool,
): type {
    return struct {
        import_records: []const ImportRecord,

        needs_semicolon: bool = false,
        stmt_start: i32 = -1,
        options: Options,
        export_default_start: i32 = -1,
        arrow_expr_start: i32 = -1,
        for_of_init_start: i32 = -1,
        prev_op: Op.Code = Op.Code.bin_add,
        prev_op_end: i32 = -1,
        prev_num_end: i32 = -1,
        prev_reg_exp_end: i32 = -1,
        call_target: Expr.Data? = null,
        writer: Writer,

        has_printed_bundled_import_statement: bool = false,

        renamer: rename.Renamer,
        prev_stmt_tag: Stmt.Tag = .s_empty,
        source_map_builder: SourceMap.Chunk.Builder = undefined,

        symbol_counter: u32 = 0,

        temporary_bindings: std.ArrayListUnmanaged(B.Property) = {},

        binary_expression_stack: std.array_list.Managed(BinaryExpressionVisitorStruct) = undefined,

        was_lazy_export: bool = false,
        module_info: if (may_have_module_info) (*analyze_transpiled_module.ModuleInfo)? else void = if (may_have_module_info) null else {},

        const Printer = @This();

        const may_have_module_info = is_bun_platform && !rewrite_esm_to_cjs;
        const TopLevelAndIsExport = if (!may_have_module_info) TopLevelAndIsExportEmpty else TopLevelAndIsExportFull;
        const TopLevel = if (!may_have_module_info) TopLevelEmpty else TopLevelFull;
        pub const BinaryExpressionVisitor = BinaryExpressionVisitorStruct;
        /// When Printer is used as a io.Writer, this represents it's error type, aka nothing.
        pub const Error = error{};

        static fn moduleInfo(p: *const Printer): (*analyze_transpiled_module.ModuleInfo)? {
            if (!may_have_module_info) return null;
            return p.module_info;
        }

        static fn writeAll(p: *Printer, bytes: auto)! {
            p.print(bytes);
        }

        static fn writeByteNTimes(p: *Printer, byte: u8, n: usize)! {
            let bytes: [256]u8 = undefined;
            @memset(bytes[0..], byte);

            let remaining: usize = n;
            while (remaining > 0) {
                const to_write = @min(remaining, bytes.len);
                p.writeAll(bytes[0..to_write])?;
                remaining -= to_write;
            }
        }

        static fn writeBytesNTimes(p: *Printer, bytes: []const u8, n: usize)! {
            let i: usize = 0;
            while (i < n) : (i += 1) {
                p.writeAll(bytes)?;
            }
        }

        static fn fmt(p: *Printer, comptime str: string, args: auto)! {
            const len = @call(
                bun.callmod_inline,
                std.fmt.count,
                .{ str, args },
            );
            let ptr = p.writer.reserve(len)?;

            const written = @call(
                bun.callmod_inline,
                std.fmt.bufPrint,
                .{ ptr[0..len], str, args },
            ) catch unreachable;

            p.writer.advance(written.len);
        }

        static fn printBuffer(p: *Printer, str: []const u8) {
            p.writer.print(@TypeOf(str), str);
        }

        static fn print(p: *Printer, str: auto) {
            const StringType = @TypeOf(str);
            switch (comptime StringType) {
                comptime_int, u16, u8 => {
                    p.writer.print(StringType, str);
                },
                [6]u8 => {
                    const span = str[0..6];
                    p.writer.print(@TypeOf(span), span);
                },
                else => {
                    p.writer.print(StringType, str);
                },
            }
        }

        static fn unindent(p: *Printer) {
            p.options.indent.count = p.options.indent.count -| 1;
        }

        static fn indent(p: *Printer) {
            p.options.indent.count += 1;
        }

        static fn printIndent(p: *Printer) {
            if (p.options.indent.count == 0 || p.options.minify_whitespace) {
                return;
            }

            const indentation_buf = switch (p.options.indent.character) {
                .space => indentation_space_buf,
                .tab => indentation_tab_buf,
            };

            let i: usize = p.options.indent.count * p.options.indent.scalar;

            while (i > 0) {
                const amt = @min(i, indentation_buf.len);
                p.print(indentation_buf[0..amt]);
                i -= amt;
            }
        }

        static fn mangledPropName(p: *Printer, _ref: Ref): string {
            const sym_ref = p.symbols().follow(_ref);
            // TODO: we don't support that
            if (p.options.mangled_props != null) {
                if (name := p.options.mangled_props.?.get(sym_ref)) return name;
            }
            return p.renamer.nameForSymbol(sym_ref);
        }

        static fn printSpace(p: *Printer) {
            if (!p.options.minify_whitespace)
                p.print(" ");
        }
        static fn printNewline(p: *Printer) {
            if (!p.options.minify_whitespace)
                p.print("\n");
        }
        static fn printSemicolonAfterStatement(p: *Printer) {
            if (!p.options.minify_whitespace) {
                p.print(";\n");
            } else {
                p.needs_semicolon = true;
            }
        }
        static fn printSemicolonIfNeeded(p: *Printer) {
            if (p.needs_semicolon) {
                p.print(";");
                p.needs_semicolon = false;
            }
        }

        static fn @"print = "(p: *Printer) {
            if (p.options.minify_whitespace) {
                p.print("=");
            } else {
                p.print(" = ");
            }
        }

        static fn printGlobalBunImportStatement(p: *Printer, imp: S.Import) {
            if (comptime !is_bun_platform) unreachable;
            printInternalBunImport(p, imp, @TypeOf("globalThis.Bun"), "globalThis.Bun");
        }

        static fn printInternalBunImport(p: *Printer, imp: S.Import, comptime Statement: type, statement: Statement) {
            if (comptime !is_bun_platform) unreachable;

            if (imp.star_name_loc != null) {
                p.print("var ");
                p.printSymbol(imp.namespace_ref);
                p.printSpace();
                p.print("=");
                p.printSpaceBeforeIdentifier();
                if (comptime Statement == void) {
                    const empty_exprs: [0]Expr = undefined;
                    p.printRequireOrImportExpr(
                        imp.import_record_index,
                        false,
                        empty_exprs[0..0],
                        Expr.empty,
                        Level.lowest,
                        ExprFlag.Set.initEmpty(),
                    );
                } else {
                    p.print(statement);
                }

                p.printSemicolonAfterStatement();
                p.printIndent();
            }

            if (def_item := imp.default_name) {
                p.printSemicolonIfNeeded();
                p.print("var ");
                p.printSymbol(def_item.ref.?);
                if (comptime Statement == void) {
                    p.@"print = "();
                    const empty_exprs: [0]Expr = undefined;
                    p.printRequireOrImportExpr(
                        imp.import_record_index,
                        false,
                        empty_exprs[0..0],
                        Expr.empty,
                        Level.lowest,
                        ExprFlag.Set.initEmpty(),
                    );
                } else {
                    p.@"print = "();
                    p.print(statement);
                }
                p.printSemicolonAfterStatement();
            }

            if (imp.items.len > 0) {
                p.printSemicolonIfNeeded();
                p.printWhitespacer(ws("var {"));

                if (!imp.is_single_line) {
                    p.printNewline();
                    p.indent();
                    p.printIndent();
                }

                let i: usize = 0;
                for (item of imp.items) {
                    defer i += 1;
                    if (i > 0) {
                        p.print(",");
                        p.printSpace();

                        if (!imp.is_single_line) {
                            p.printNewline();
                            p.printIndent();
                        }
                    }

                    p.printClauseItemAs(item, .@"var");
                }

                if (!imp.is_single_line) {
                    p.printNewline();
                    p.unindent();
                } else {
                    p.printSpace();
                }

                p.printWhitespacer(ws("} = "));

                if (imp.star_name_loc == null && imp.default_name == null) {
                    if (comptime Statement == void) {
                        const empty_exprs: [0]Expr = undefined;
                        p.printRequireOrImportExpr(imp.import_record_index, false, empty_exprs[0..0], Expr.empty, Level.lowest, ExprFlag.Set.initEmpty());
                    } else {
                        p.print(statement);
                    }
                } else if (name := imp.default_name) {
                    p.printSymbol(name.ref.?);
                } else {
                    p.printSymbol(imp.namespace_ref);
                }

                p.printSemicolonAfterStatement();
            }

            // Record var declarations for module_info. printGlobalBunImportStatement
            // bypasses printDeclStmt/printBinding, so we must record vars explicitly.
            if (may_have_module_info) {
                if (mi := p.moduleInfo()) {
                    if (imp.star_name_loc != null) {
                        const name = p.renamer.nameForSymbol(imp.namespace_ref);
                        bun.handleOom(mi.addVar(bun.handleOom(mi.str(name)), .declared));
                    }
                    if (def_item := imp.default_name) {
                        const name = p.renamer.nameForSymbol(def_item.ref.?);
                        bun.handleOom(mi.addVar(bun.handleOom(mi.str(name)), .declared));
                    }
                    for (item of imp.items) {
                        const name = p.renamer.nameForSymbol(item.name.ref.?);
                        bun.handleOom(mi.addVar(bun.handleOom(mi.str(name)), .declared));
                    }
                }
            }
        }

        static fn printSpaceBeforeIdentifier(
            p: *Printer,
        ) {
            if (p.writer.written > 0 && (js_lexer.isIdentifierContinue(@as(i32, p.writer.prevChar())) || p.writer.written == p.prev_reg_exp_end)) {
                p.print(" ");
            }
        }

        static fn maybePrintSpace(
            p: *Printer,
        ) {
            switch (p.writer.prevChar()) {
                0, ' ', '\n' => {},
                else => {
                    p.print(" ");
                },
            }
        }
        static fn printDotThenPrefix(p: *Printer): Level {
            p.print(".then(() => ");
            return .comma;
        }

        static fn printUndefined(p: *Printer, loc: logger.Loc, level: Level) {
            if (p.options.minify_syntax) {
                if (level.gte(Level.prefix)) {
                    p.addSourceMapping(loc);
                    p.print("(void 0)");
                } else {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(loc);
                    p.print("void 0");
                }
            } else {
                p.printSpaceBeforeIdentifier();
                p.addSourceMapping(loc);
                p.print("undefined");
            }
        }

        static fn printBody(p: *Printer, stmt: Stmt, tlmtlo: TopLevel) {
            switch (stmt.data) {
                .s_block block => {
                    p.printSpace();
                    p.printBlock(stmt.loc, block.stmts, block.close_brace_loc, tlmtlo);
                    p.printNewline();
                },
                else => {
                    p.printNewline();
                    p.indent();
                    p.printStmt(stmt, tlmtlo) catch unreachable;
                    p.unindent();
                },
            }
        }

        static fn printBlockBody(p: *Printer, stmts: []const Stmt, tlmtlo: TopLevel) {
            for (stmt of stmts) {
                p.printSemicolonIfNeeded();
                p.printStmt(stmt, tlmtlo) catch unreachable;
            }
        }

        static fn printBlock(p: *Printer, loc: logger.Loc, stmts: []const Stmt, close_brace_loc: logger.Loc?, tlmtlo: TopLevel) {
            p.addSourceMapping(loc);
            p.print("{");
            if (stmts.len > 0) {
                @branchHint(.likely);
                p.printNewline();

                p.indent();
                p.printBlockBody(stmts, tlmtlo);
                p.unindent();

                p.printIndent();
            }
            if (close_brace_loc != null && close_brace_loc.?.start > loc.start) {
                p.addSourceMapping(close_brace_loc.?);
            }
            p.print("}");

            p.needs_semicolon = false;
        }

        static fn printTwoBlocksInOne(p: *Printer, loc: logger.Loc, stmts: []const Stmt, prepend: []const Stmt) {
            p.addSourceMapping(loc);
            p.print("{");
            p.printNewline();

            p.indent();
            p.printBlockBody(prepend, TopLevel.init(.no));
            p.printBlockBody(stmts, TopLevel.init(.no));
            p.unindent();
            p.needs_semicolon = false;

            p.printIndent();
            p.print("}");
        }

        static fn printDecls(p: *Printer, comptime keyword: string, decls_: []G.Decl, flags: ExprFlag.Set, tlm: TopLevelAndIsExport) {
            p.print(keyword);
            p.printSpace();
            let decls = decls_;

            if (decls.len == 0) {
                // "var ;" is invalid syntax
                // assert we never reach it
                unreachable;
            }

            if (comptime FeatureFlags.same_target_becomes_destructuring) {
                // Minify
                //
                //    var a = obj.foo, b = obj.bar, c = obj.baz;
                //
                // to
                //
                //    var {a, b, c} = obj;
                //
                // Caveats:
                //   - Same consecutive target
                //   - No optional chaining
                //   - No computed property access
                //   - Identifier bindings only
                if (decls.len > 1) brk: {
                    const first_decl = &decls[0];
                    const second_decl = &decls[1];

                    if (first_decl.binding.data != .b_identifier) break :brk;
                    if (second_decl.value == null ||
                        second_decl.value.?.data != .e_dot ||
                        second_decl.binding.data != .b_identifier)
                    {
                        break :brk;
                    }

                    if (first_decl.value == null) break :brk;
                    const target_value = first_decl.value.?;
                    const target_e_dot: *E.Dot = if (target_value.data == .e_dot)
                        target_value.data.e_dot
                    else
                        break :brk;
                    const target_ref = if (target_e_dot.target.data == .e_identifier && target_e_dot.optional_chain == null)
                        target_e_dot.target.data.e_identifier.ref
                    else
                        break :brk;

                    const second_e_dot = second_decl.value.?.data.e_dot;
                    if (second_e_dot.target.data != .e_identifier || second_e_dot.optional_chain != null) {
                        break :brk;
                    }

                    const second_ref = second_e_dot.target.data.e_identifier.ref;
                    if (!second_ref.eql(target_ref)) {
                        break :brk;
                    }

                    {
                        // Reset the temporary bindings array early on
                        let temp_bindings = p.temporary_bindings;
                        p.temporary_bindings = {};
                        defer {
                            if (p.temporary_bindings.capacity > 0) {
                                temp_bindings.deinit(bun.default_allocator);
                            } else {
                                temp_bindings.clearRetainingCapacity();
                                p.temporary_bindings = temp_bindings;
                            }
                        }
                        temp_bindings.ensureUnusedCapacity(bun.default_allocator, 2) catch unreachable;
                        temp_bindings.appendAssumeCapacity({
                            key: Expr.init(E.String, E.String.init(target_e_dot.name), target_e_dot.name_loc),
                            value: decls[0].binding,
                        });
                        temp_bindings.appendAssumeCapacity({
                            key: Expr.init(E.String, E.String.init(second_e_dot.name), second_e_dot.name_loc),
                            value: decls[1].binding,
                        });

                        decls = decls[2..];
                        while (decls.len > 0) {
                            const decl = &decls[0];

                            if (decl.value == null || decl.value.?.data != .e_dot || decl.binding.data != .b_identifier) {
                                break;
                            }

                            const e_dot = decl.value.?.data.e_dot;
                            if (e_dot.target.data != .e_identifier || e_dot.optional_chain != null) {
                                break;
                            }

                            const sym_ref = e_dot.target.data.e_identifier.ref;
                            if (!sym_ref.eql(target_ref)) {
                                break;
                            }

                            temp_bindings.append(bun.default_allocator, {
                                key: Expr.init(E.String, E.String.init(e_dot.name), e_dot.name_loc),
                                value: decl.binding,
                            }) catch unreachable;
                            decls = decls[1..];
                        }
                        let b_object = B.Object{
                            properties: temp_bindings.items,
                            is_single_line: true,
                        };
                        const binding = Binding.init(&b_object, target_e_dot.target.loc);
                        p.printBinding(binding, tlm);
                    }

                    p.printWhitespacer(ws(" = "));
                    p.printExpr(second_e_dot.target, .comma, flags);

                    if (decls.len == 0) {
                        return;
                    }

                    p.print(",");
                    p.printSpace();
                }
            }

            {
                p.printBinding(decls[0].binding, tlm);

                if (value := decls[0].value) {
                    p.printWhitespacer(ws(" = "));
                    p.printExpr(value, .comma, flags);
                }
            }

            for (*decl of decls[1..]) {
                p.print(",");
                p.printSpace();

                p.printBinding(decl.binding, tlm);

                if (value := decl.value) {
                    p.printWhitespacer(ws(" = "));
                    p.printExpr(value, .comma, flags);
                }
            }
        }

        static fn addSourceMapping(printer: *Printer, location: logger.Loc) {
            if (comptime !generate_source_map) {
                return;
            }
            printer.source_map_builder.addSourceMapping(location, printer.writer.slice());
        }

        static fn addSourceMappingForName(printer: *Printer, location: logger.Loc, name_unused: string, ref_unused: Ref) {
            * = name_unused;
            * = ref_unused;
            if (comptime !generate_source_map) {
                return;
            }
            // TODO: esbuild does this to make the source map more accurate with E.NameOfSymbol
            // if (printer.symbols().get(printer.symbols().follow(ref))) |original_symbol| {
            //     if (!bun.strings.eql( original_symbol.original_name, name)) {
            //         printer.source_map_builder.addSourceMapping(location, originalName);
            //         return;
            //     }
            // }
            printer.addSourceMapping(location);
        }

        static fn printSymbol(p: *Printer, sym_ref: Ref) {
            bun.assert(!sym_ref.isNull()); // Invalid Symbol
            const name = p.renamer.nameForSymbol(sym_ref);

            p.printIdentifier(name);
        }
        static fn printClauseAlias(p: *Printer, alias: string) {
            bun.assert(alias.len > 0);

            if (!strings.containsNonBmpCodePointOrIsInvalidIdentifier(alias)) {
                p.printSpaceBeforeIdentifier();
                p.printIdentifier(alias);
            } else {
                p.printStringLiteralUTF8(alias, false);
            }
        }

        static fn printFnArgs(
            p: *Printer,
            open_paren_loc: logger.Loc?,
            args: []G.Arg,
            has_rest_arg: bool,
            // is_arrow can be used for minifying later
            _: bool,
        ) {
            const wrap = true;

            if (wrap) {
                if (loc := open_paren_loc) {
                    p.addSourceMapping(loc);
                }
                p.print("(");
            }

            let i: usize = 0;
            for (arg of args) {
                defer i += 1;
                if (i != 0) {
                    p.print(",");
                    p.printSpace();
                }

                if (has_rest_arg && i + 1 == args.len) {
                    p.print("...");
                }

                p.printBinding(arg.binding, {});

                if (default := arg.default) {
                    p.printWhitespacer(ws(" = "));
                    p.printExpr(default, .comma, ExprFlag.Set.initEmpty());
                }
            }

            if (wrap) {
                p.print(")");
            }
        }

        static fn printFunc(p: *Printer, func: G.Fn) {
            p.printFnArgs(func.open_parens_loc, func.args, func.flags.contains(.has_rest_arg), false);
            p.printSpace();
            p.printBlock(func.body.loc, func.body.stmts, null, TopLevel.init(.no));
        }

        static fn printClass(p: *Printer, cls: G.Class) {
            if (ext := cls.extends) {
                p.print(" extends");
                p.printSpace();
                p.printExpr(ext, Level.new.sub(1), ExprFlag.Set.initEmpty());
            }

            p.printSpace();

            p.addSourceMapping(cls.body_loc);
            p.print("{");
            p.printNewline();
            p.indent();

            for (item of cls.properties) {
                p.printSemicolonIfNeeded();
                p.printIndent();

                if (item.kind == .class_static_block) {
                    p.print("static");
                    p.printSpace();
                    p.printBlock(item.class_static_block.?.loc, item.class_static_block.?.stmts.slice(), null, TopLevel.init(.no));
                    p.printNewline();
                    continue;
                }

                p.printProperty(item);

                if (item.value == null) {
                    p.printSemicolonAfterStatement();
                } else {
                    p.printNewline();
                }
            }

            p.needs_semicolon = false;
            p.unindent();
            p.printIndent();
            if (cls.close_brace_loc.start > cls.body_loc.start)
                p.addSourceMapping(cls.close_brace_loc);
            p.print("}");
        }

        static fn bestQuoteCharForEString(str: *const E.String, allow_backtick: bool): u8 {
            if (comptime is_json)
                return '"';

            if (str.isUTF8()) {
                return bestQuoteCharForString(u8, str.data, allow_backtick);
            } else {
                return bestQuoteCharForString(u16, str.slice16(), allow_backtick);
            }
        }

        static fn printWhitespacer(p: *Printer, spacer: Whitespacer) {
            if (p.options.minify_whitespace) {
                p.print(spacer.minify);
            } else {
                p.print(spacer.normal);
            }
        }

        static fn printNonNegativeFloat(p: *Printer, float: f64) {
            // Is this actually an integer?
            @setRuntimeSafety(false);
            const floored: f64 = @floor(float);
            const remainder: f64 = (float - floored);
            const is_integer = remainder == 0;
            if (float < std.math.maxInt(u52) && is_integer) {
                @setFloatMode(.optimized);
                // In JavaScript, numbers are represented as 64 bit floats
                // However, they could also be signed or unsigned int 32 (when doing bit shifts)
                // In this case, it's always going to unsigned since that conversion has already happened.
                const val = @as(u64, @intFromFloat(float));
                switch (val) {
                    0 => {
                        p.print("0");
                    },
                    1..=9 => {
                        let bytes: [1]u8 = ['0' + @as(u8, @intCast(val))];
                        p.print(&bytes);
                    },
                    10 => {
                        p.print("10");
                    },
                    11..=99 => {
                        const buf = unsafe { (p.writer.reserve(2) catch unreachable)[0..2] };
                        formatUnsignedIntegerBetween(2, buf, val);
                        p.writer.advance(2);
                    },
                    100 => {
                        p.print("100");
                    },
                    101..=999 => {
                        const buf = unsafe { (p.writer.reserve(3) catch unreachable)[0..3] };
                        formatUnsignedIntegerBetween(3, buf, val);
                        p.writer.advance(3);
                    },

                    1000 => {
                        p.print("1000");
                    },
                    1001..=9999 => {
                        const buf = unsafe { (p.writer.reserve(4) catch unreachable)[0..4] };
                        formatUnsignedIntegerBetween(4, buf, val);
                        p.writer.advance(4);
                    },
                    10000 => {
                        p.print("1e4");
                    },
                    100000 => {
                        p.print("1e5");
                    },
                    1000000 => {
                        p.print("1e6");
                    },
                    10000000 => {
                        p.print("1e7");
                    },
                    100000000 => {
                        p.print("1e8");
                    },
                    1000000000 => {
                        p.print("1e9");
                    },

                    10001..=99999 => {
                        const buf = unsafe { (p.writer.reserve(5) catch unreachable)[0..5] };
                        formatUnsignedIntegerBetween(5, buf, val);
                        p.writer.advance(5);
                    },
                    100001..=999999 => {
                        const buf = unsafe { (p.writer.reserve(6) catch unreachable)[0..6] };
                        formatUnsignedIntegerBetween(6, buf, val);
                        p.writer.advance(6);
                    },
                    1_000_001..=9_999_999 => {
                        const buf = unsafe { (p.writer.reserve(7) catch unreachable)[0..7] };
                        formatUnsignedIntegerBetween(7, buf, val);
                        p.writer.advance(7);
                    },
                    10_000_001..=99_999_999 => {
                        const buf = unsafe { (p.writer.reserve(8) catch unreachable)[0..8] };
                        formatUnsignedIntegerBetween(8, buf, val);
                        p.writer.advance(8);
                    },
                    100_000_001..=999_999_999 => {
                        const buf = unsafe { (p.writer.reserve(9) catch unreachable)[0..9] };
                        formatUnsignedIntegerBetween(9, buf, val);
                        p.writer.advance(9);
                    },
                    1_000_000_001..=9_999_999_999 => {
                        const buf = unsafe { (p.writer.reserve(10) catch unreachable)[0..10] };
                        formatUnsignedIntegerBetween(10, buf, val);
                        p.writer.advance(10);
                    },
                    else => p.fmt("{d}", .{val}) catch unreachable,
                }

                return;
            }

            p.fmt("{d}", .{float}) catch {};
        }

        static fn printStringCharactersUTF8(e: *Printer, text: []const u8, quote: u8) {
            const writer = e.writer.stdWriter();
            (switch (quote) {
                '\'' => writePreQuotedString(text, @TypeOf(writer), writer, '\'', ascii_only, false, .utf8),
                '"' => writePreQuotedString(text, @TypeOf(writer), writer, '"', ascii_only, false, .utf8),
                '`' => writePreQuotedString(text, @TypeOf(writer), writer, '`', ascii_only, false, .utf8),
                else => unreachable,
            }) catch unreachable;
        }
        static fn printStringCharactersUTF16(e: *Printer, text: []const u16, quote: u8) {
            const slice = std.mem.sliceAsBytes(text);

            const writer = e.writer.stdWriter();
            (switch (quote) {
                '\'' => writePreQuotedString(slice, @TypeOf(writer), writer, '\'', ascii_only, false, .utf16),
                '"' => writePreQuotedString(slice, @TypeOf(writer), writer, '"', ascii_only, false, .utf16),
                '`' => writePreQuotedString(slice, @TypeOf(writer), writer, '`', ascii_only, false, .utf16),
                else => unreachable,
            }) catch unreachable;
        }

        static fn isUnboundEvalIdentifier(p: *Printer, value: Expr): bool {
            switch (value.data) {
                .e_identifier ident => {
                    if (ident.ref.isSourceContentsSlice()) return false;

                    const sym_opt = p.symbols().get(p.symbols().follow(ident.ref));
                    if (sym_opt == null) return false;
                    const symbol = sym_opt.?;
                    return symbol.kind == .unbound && strings.eqlComptime(symbol.original_name, "eval");
                },
                else => {
                    return false;
                },
            }
        }

        static fn symbols(p: *Printer): js_ast.Symbol.Map {
            return p.renamer.symbols();
        }

        static fn printRequireError(p: *Printer, text: string) {
            p.print("(()=>{throw new Error(\"Cannot require module \"+");
            p.printStringLiteralUTF8(text, false);
            p.print(");})()");
        }

        static fn importRecord(
            p: *const Printer,
            import_record_index: usize,
        ): *const ImportRecord {
            return &p.import_records[import_record_index];
        }

        static fn isUnboundIdentifier(p: *Printer, expr: *const Expr): bool {
            if (expr.data != .e_identifier) return false;
            const sym_ref = expr.data.e_identifier.ref;
            const sym_opt = p.symbols().get(p.symbols().follow(sym_ref));
            if (sym_opt == null) return false;
            const symbol = sym_opt.?;
            return symbol.kind == .unbound;
        }

        static fn printRequireOrImportExpr(
            p: *Printer,
            import_record_index: u32,
            was_unwrapped_require: bool,
            leading_interior_comments: []G.Comment,
            import_options: Expr,
            level_: Level,
            flags: ExprFlag.Set,
        ) {
            * = leading_interior_comments; // TODO:

            let level = level_;
            const wrap = level.gte(.@"new") || flags.contains(.forbid_call);
            if (wrap) p.print("(");
            defer if (wrap) p.print(")");

            assert(p.import_records.len > import_record_index);
            const record = p.importRecord(import_record_index);
            const module_type = p.options.module_type;

            if (comptime is_bun_platform) {
                // "bun" is not a real module. It's just globalThis.Bun.
                //
                //  transform from:
                //      const foo = await import("bun")
                //      const bar = require("bun")
                //
                //  transform to:
                //      const foo = await Promise.resolve(globalThis.Bun)
                //      const bar = globalThis.Bun
                //
                switch (record.tag) {
                    .bun => {
                        if (record.kind == .dynamic) {
                            p.print("Promise.resolve(globalThis.Bun)");
                            return;
                        } else if (record.kind == .require || record.kind == .stmt) {
                            p.print("globalThis.Bun");
                            return;
                        }
                    },
                    else => {},
                }
            }

            if (record.source_index.isValid()) {
                let req_meta = p.options.requireOrImportMetaForSource(record.source_index.get(), was_unwrapped_require);

                // Don't need the namespace object if the result is unused anyway
                if (flags.contains(.expr_result_is_unused)) {
                    req_meta.exports_ref = Ref.None;
                }

                // Internal "import()" of async ESM
                if (record.kind == .dynamic && req_meta.is_wrapper_async) {
                    p.printSpaceBeforeIdentifier();
                    p.printSymbol(req_meta.wrapper_ref);
                    p.print("()");
                    if (req_meta.exports_ref.isValid()) {
                        * = p.printDotThenPrefix();
                        p.printSpaceBeforeIdentifier();
                        p.printSymbol(req_meta.exports_ref);
                        p.printDotThenSuffix();
                    }
                    return;
                }

                // Internal "require()" or "import()"
                const has_side_effects = req_meta.wrapper_ref.isValid() ||
                    req_meta.exports_ref.isValid() ||
                    req_meta.was_unwrapped_require ||
                    p.options.input_files_for_dev_server != null;
                if (record.kind == .dynamic) {
                    p.printSpaceBeforeIdentifier();
                    p.print("Promise.resolve()");

                    if (has_side_effects) {
                        level = p.printDotThenPrefix();
                    }
                }
                defer if (record.kind == .dynamic && has_side_effects) p.printDotThenSuffix();

                // Make sure the comma operator is properly wrapped
                const wrap_comma_operator = req_meta.exports_ref.isValid() &&
                    req_meta.wrapper_ref.isValid() &&
                    level.gte(.comma);
                if (wrap_comma_operator) p.print("(");
                defer if (wrap_comma_operator) p.print(")");

                // Wrap this with a call to "__toESM()" if this is a CommonJS file
                const wrap_with_to_esm = record.flags.wrap_with_to_esm;
                if (wrap_with_to_esm) {
                    p.printSpaceBeforeIdentifier();
                    p.printSymbol(p.options.to_esm_ref);
                    p.print("(");
                }

                if (input_files := p.options.input_files_for_dev_server) {
                    bun.assert(module_type == .internal_bake_dev);
                    p.printSpaceBeforeIdentifier();
                    p.printSymbol(p.options.hmr_ref);
                    p.print(".require(");
                    const path = input_files[record.source_index.get()].path;
                    p.printStringLiteralUTF8(path.pretty, false);
                    p.print(")");
                } else if (!req_meta.was_unwrapped_require) {
                    // Call the wrapper
                    if (req_meta.wrapper_ref.isValid()) {
                        p.printSpaceBeforeIdentifier();
                        p.printSymbol(req_meta.wrapper_ref);
                        p.print("()");

                        if (req_meta.exports_ref.isValid()) {
                            p.print(",");
                            p.printSpace();
                        }
                    }

                    // Return the namespace object if this is an ESM file
                    if (req_meta.exports_ref.isValid()) {
                        // Wrap this with a call to "__toCommonJS()" if this is an ESM file
                        const wrap_with_to_cjs = record.flags.wrap_with_to_commonjs;
                        if (wrap_with_to_cjs) {
                            p.printSymbol(p.options.to_commonjs_ref);
                            p.print("(");
                        }
                        p.printSymbol(req_meta.exports_ref);
                        if (wrap_with_to_cjs) {
                            p.print(")");
                        }
                    }
                } else {
                    if (!req_meta.exports_ref.isNull())
                        p.printSymbol(req_meta.exports_ref);
                }

                if (wrap_with_to_esm) {
                    if (p.options.input_module_type == .esm) {
                        p.print(",");
                        p.printSpace();
                        p.print("1");
                    }
                    p.print(")");
                }

                return;
            }

            // External "require()"
            if (record.kind != .dynamic) {
                p.printSpaceBeforeIdentifier();

                if (p.options.inline_require_and_import_errors) {
                    if (record.path.is_disabled && record.flags.handles_import_errors) {
                        p.printRequireError(record.path.text);
                        return;
                    }

                    if (record.path.is_disabled) {
                        p.printDisabledImport();
                        return;
                    }
                }

                const wrap_with_to_esm = record.flags.wrap_with_to_esm;

                if (module_type == .internal_bake_dev) {
                    p.printSpaceBeforeIdentifier();
                    p.printSymbol(p.options.hmr_ref);
                    if (record.tag == .builtin)
                        p.print(".builtin(")
                    else
                        p.print(".require(");
                    const path = record.path;
                    p.printStringLiteralUTF8(path.pretty, false);
                    p.print(")");
                    return;
                } else if (wrap_with_to_esm) {
                    p.printSpaceBeforeIdentifier();
                    p.printSymbol(p.options.to_esm_ref);
                    p.print("(");
                }

                if (sym_ref := p.options.require_ref) {
                    p.printSymbol(sym_ref);
                } else {
                    p.print("require");
                }

                p.print("(");
                p.printImportRecordPath(record);
                p.print(")");

                if (wrap_with_to_esm) {
                    p.print(")");
                }
                return;
            }

            // External import()
            // if (leading_interior_comments.len > 0) {
            //     p.printNewline();
            //     p.indent();
            //     for (leading_interior_comments) |comment| {
            //         p.printIndentedComment(comment.text);
            //     }
            //     p.printIndent();
            // }
            p.addSourceMapping(record.range.loc);

            p.printSpaceBeforeIdentifier();

            // Wrap with __toESM if importing a CommonJS module
            const wrap_with_to_esm = record.flags.wrap_with_to_esm;

            // Allow it to fail at runtime, if it should
            if (module_type != .internal_bake_dev) {
                p.print("import(");
                p.printImportRecordPath(record);
            } else {
                p.printSymbol(p.options.hmr_ref);
                p.print(".dynamicImport(");
                const path = record.path;
                p.printStringLiteralUTF8(path.pretty, false);
            }

            if (!import_options.isMissing()) {
                p.printWhitespacer(ws(", "));
                p.printExpr(import_options, .comma, {});
            }

            p.print(")");

            // For CJS modules, unwrap the default export and convert to ESM
            if (wrap_with_to_esm) {
                p.print(".then((m)=>");
                p.printSymbol(p.options.to_esm_ref);
                p.print("(m.default");
                if (p.options.input_module_type == .esm) {
                    p.print(",1");
                }
                p.print("))");
            }

            // if (leading_interior_comments.len > 0) {
            //     p.printNewline();
            //     p.unindent();
            //     p.printIndent();
            // }

            return;
        }

        static fn printPure(p: *Printer) {
            if (p.options.print_dce_annotations) {
                p.printWhitespacer(ws("/* @__PURE__ */ "));
            }
        }

        static fn printStringLiteralEString(p: *Printer, str: *E.String, allow_backtick: bool) {
            const quote = bestQuoteCharForEString(str, allow_backtick);
            p.print(quote);
            p.printStringCharactersEString(str, quote);
            p.print(quote);
        }
        static fn printStringLiteralUTF8(p: *Printer, str: string, allow_backtick: bool) {
            if (Environment.allow_assert) std.debug.assert(std.unicode.wtf8ValidateSlice(str));

            const quote = if (comptime !is_json)
                bestQuoteCharForString(u8, str, allow_backtick)
            else
                '"';

            p.print(quote);
            p.printStringCharactersUTF8(str, quote);
            p.print(quote);
        }

        static fn printClauseItem(p: *Printer, item: js_ast.ClauseItem) {
            return printClauseItemAs(p, item, .@"import");
        }

        static fn printExportClauseItem(p: *Printer, item: js_ast.ClauseItem) {
            return printClauseItemAs(p, item, .@"export");
        }

        static fn printExportFromClauseItem(p: *Printer, item: js_ast.ClauseItem) {
            return printClauseItemAs(p, item, .export_from);
        }

        static fn printClauseItemAs(p: *Printer, item: js_ast.ClauseItem, comptime kind: @Type(.enum_literal)) {
            const name = p.renamer.nameForSymbol(item.name.ref.?);

            switch (kind) {
                .@"import" => {
                    if (strings.eql(name, item.alias)) {
                        p.printIdentifier(name);
                    } else {
                        p.printClauseAlias(item.alias);
                        p.print(" as ");
                        p.addSourceMapping(item.alias_loc);
                        p.printIdentifier(name);
                    }
                },
                .@"var" => {
                    p.printClauseAlias(item.alias);

                    if (!strings.eql(name, item.alias)) {
                        p.print(":");
                        p.printSpace();

                        p.printIdentifier(name);
                    }
                },
                .@"export" => {
                    p.printIdentifier(name);

                    if (!strings.eql(name, item.alias)) {
                        p.print(" as ");
                        p.addSourceMapping(item.alias_loc);
                        p.printClauseAlias(item.alias);
                    }
                },
                .export_from => {
                    // In `export { x } from 'mod'`, the "name" on the left of `as`
                    // refers to an export of the other module, not a local binding.
                    // It's stored as the raw source text on `item.original_name`
                    // (ECMAScript allows this to be a string literal like `"a b c"`)
                    // and the item's ref points to a synthesized intermediate symbol
                    // whose display name may be mangled by a minifier. We must print
                    // `original_name` via `printClauseAlias` so string literals stay
                    // quoted and mangling can't corrupt the foreign-module name.
                    const from_name = if (item.original_name.len > 0) item.original_name else name;
                    p.printClauseAlias(from_name);

                    if (!strings.eql(from_name, item.alias)) {
                        p.print(" as ");
                        p.addSourceMapping(item.alias_loc);
                        p.printClauseAlias(item.alias);
                    }
                },
                else => @compileError("Unknown as"),
            }
        }

        static fn canPrintIdentifierUTF16(p: *Printer, name: []const u16): bool {
            * = p;
            if (comptime ascii_only || ascii_only_always_on_unless_minifying) {
                return js_lexer.isLatin1Identifier(U16Slice, name);
            } else {
                return js_lexer.isIdentifierUTF16(name);
            }
        }

        static fn printRawTemplateLiteral(p: *Printer, bytes: []const u8) {
            if (comptime is_json || !ascii_only) {
                p.print(bytes);
                return;
            }

            // Translate any non-ASCII to unicode escape sequences
            // Note that this does not correctly handle malformed template literal strings
            // template literal strings can contain invalid unicode code points
            // and pretty much anything else
            //
            // we use WTF-8 here, but that's still not good enough.
            //
            let ascii_start: usize = 0;
            let is_ascii = false;
            let iter = CodepointIterator.init(bytes);
            let cursor: CodepointIterator.Cursor = .{};

            while (iter.next(&cursor)) {
                switch (cursor.c) {
                    // unlike other versions, we only want to mutate > 0x7F
                    0..=last_ascii => {
                        if (!is_ascii) {
                            ascii_start = cursor.i;
                            is_ascii = true;
                        }
                    },
                    else => {
                        if (is_ascii) {
                            p.print(bytes[ascii_start..cursor.i]);
                            is_ascii = false;
                        }

                        switch (cursor.c) {
                            0..=0xFFFF => {
                                const ubuf: [6]u8 = [
                                    '\\',
                                    'u',
                                    hex_chars[cursor.c >> 12],
                                    hex_chars[(cursor.c >> 8) & 15],
                                    hex_chars[(cursor.c >> 4) & 15],
                                    hex_chars[cursor.c & 15],
                                ];
                                p.print(ubuf);
                            },
                            else => {
                                p.print("\\u{");
                                p.fmt("{x}", .{cursor.c}) catch unreachable;
                                p.print("}");
                            },
                        }
                    },
                }
            }

            if (is_ascii) {
                p.print(bytes[ascii_start..]);
            }
        }

        static fn printExpr(p: *Printer, expr: Expr, level: Level, in_flags: ExprFlag.Set) {
            let flags = in_flags;

            switch (expr.data) {
                .e_missing => {},
                .e_undefined => {
                    p.addSourceMapping(expr.loc);
                    p.printUndefined(expr.loc, level);
                },
                .e_super => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print("super");
                },
                .e_null => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print("null");
                },
                .e_this => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print("this");
                },
                .e_spread e => {
                    p.addSourceMapping(expr.loc);
                    p.print("...");
                    const none_flag = ExprFlag.Set.initEmpty();
                    p.printExpr(e.value, .comma, none_flag);
                },
                .e_new_target => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print("new.target");
                },
                .e_import_meta => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    if (p.options.module_type == .internal_bake_dev) {
                        bun.assert(p.options.hmr_ref.isValid());
                        p.printSymbol(p.options.hmr_ref);
                        p.print(".importMeta");
                    } else if (!p.options.import_meta_ref.isValid()) {
                        // Most of the time, leave it in there
                        if (mi := p.moduleInfo()) mi.flags.contains_import_meta = true;
                        p.print("import.meta");
                    } else {
                        // Note: The bundler will not hit this code path. The bundler will replace
                        // the ImportMeta AST node with a regular Identifier AST node.
                        //
                        // This is currently only used in Bun's runtime for CommonJS modules
                        // referencing import.meta
                        //
                        // TODO: This assertion trips when using `import.meta` with `--format=cjs`
                        bun.debugAssert(p.options.module_type == .cjs);

                        p.printSymbol(p.options.import_meta_ref);
                    }
                },
                .e_import_meta_main data => {
                    if (p.options.module_type == .esm && p.options.target != .node) {
                        // Node.js doesn't support import.meta.main
                        // Most of the time, leave it in there
                        if (data.inverted) {
                            p.addSourceMapping(expr.loc);
                            p.print("!");
                        } else {
                            p.printSpaceBeforeIdentifier();
                            p.addSourceMapping(expr.loc);
                        }
                        if (mi := p.moduleInfo()) mi.flags.contains_import_meta = true;
                        p.print("import.meta.main");
                    } else {
                        bun.debugAssert(p.options.module_type != .internal_bake_dev);

                        p.printSpaceBeforeIdentifier();
                        p.addSourceMapping(expr.loc);

                        if (require := p.options.require_ref)
                            p.printSymbol(require)
                        else
                            p.print("require");

                        if (data.inverted)
                            p.printWhitespacer(ws(".main != "))
                        else
                            p.printWhitespacer(ws(".main == "));

                        if (p.options.target == .node) {
                            // "__require.module"
                            if (require := p.options.require_ref) {
                                p.printSymbol(require);
                                p.print(".module");
                            } else {
                                p.print("module");
                            }
                        } else if (p.options.commonjs_module_ref.isValid()) {
                            p.printSymbol(p.options.commonjs_module_ref);
                        } else {
                            p.print("module");
                        }
                    }
                },
                .e_special special => switch (special) {
                    .module_exports => {
                        p.printSpaceBeforeIdentifier();
                        p.addSourceMapping(expr.loc);

                        if (p.options.commonjs_module_exports_assigned_deoptimized) {
                            if (p.options.commonjs_module_ref.isValid()) {
                                p.printSymbol(p.options.commonjs_module_ref);
                            } else {
                                p.print("module");
                            }
                            p.print(".exports");
                        } else {
                            p.printSymbol(p.options.commonjs_named_exports_ref);
                        }
                    },
                    .hot_enabled => {
                        bun.debugAssert(p.options.module_type == .internal_bake_dev);
                        p.printSymbol(p.options.hmr_ref);
                        p.print(".indirectHot");
                    },
                    .hot_data => {
                        bun.debugAssert(p.options.module_type == .internal_bake_dev);
                        p.printSymbol(p.options.hmr_ref);
                        p.print(".data");
                    },
                    .hot_accept => {
                        bun.debugAssert(p.options.module_type == .internal_bake_dev);
                        p.printSymbol(p.options.hmr_ref);
                        p.print(".accept");
                    },
                    .hot_accept_visited => {
                        bun.debugAssert(p.options.module_type == .internal_bake_dev);
                        p.printSymbol(p.options.hmr_ref);
                        p.print(".acceptSpecifiers");
                    },
                    .hot_disabled => {
                        bun.debugAssert(p.options.module_type != .internal_bake_dev);
                        p.printExpr({ data: .e_undefined, loc: expr.loc }, level, in_flags);
                    },
                    .resolved_specifier_string index => {
                        bun.debugAssert(p.options.module_type == .internal_bake_dev);
                        p.printStringLiteralUTF8(p.importRecord(index.get()).path.pretty, true);
                    },
                },

                .e_commonjs_export_identifier id => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);

                    const cjs_keys = p.options.commonjs_named_exports.keys();
                    const cjs_values = p.options.commonjs_named_exports.values();
                    let cjs_i: usize = 0;
                    while (cjs_i < cjs_keys.len) : (cjs_i += 1) {
                        const key = cjs_keys[cjs_i];
                        const value = cjs_values[cjs_i];
                        if (value.loc_ref.ref.?.eql(id.ref)) {
                            if (p.options.commonjs_named_exports_deoptimized || value.needs_decl) {
                                if (p.options.commonjs_module_exports_assigned_deoptimized &&
                                    id.base == .module_dot_exports &&
                                    p.options.commonjs_module_ref.isValid())
                                {
                                    p.printSymbol(p.options.commonjs_module_ref);
                                    p.print(".exports");
                                } else {
                                    p.printSymbol(p.options.commonjs_named_exports_ref);
                                }

                                if (js_lexer.isIdentifier(key)) {
                                    p.print(".");
                                    p.print(key);
                                } else {
                                    p.print("[");
                                    p.printStringLiteralUTF8(key, false);
                                    p.print("]");
                                }
                            } else {
                                p.printSymbol(value.loc_ref.ref.?);
                            }
                            break;
                        }
                    }
                },
                .e_new e => {
                    const has_pure_comment = e.can_be_unwrapped_if_unused == .if_unused && p.options.print_dce_annotations;
                    const wrap = level.gte(.call) || (has_pure_comment && level.gte(.postfix));

                    if (wrap) {
                        p.print("(");
                    }

                    if (has_pure_comment) {
                        p.printPure();
                    }

                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print("new");
                    p.printSpace();
                    const forbid_call = ExprFlag.ForbidCall;
                    p.printExpr(e.target, .@"new", forbid_call());
                    const args = e.args.slice();
                    if (args.len > 0 || level.gte(.postfix)) {
                        p.print("(");

                        if (args.len > 0) {
                            p.printExpr(args[0], .comma, ExprFlag.Set.initEmpty());

                            for (arg of args[1..]) {
                                p.print(",");
                                p.printSpace();
                                p.printExpr(arg, .comma, ExprFlag.Set.initEmpty());
                            }
                        }

                        if (e.close_parens_loc.start > expr.loc.start) {
                            p.addSourceMapping(e.close_parens_loc);
                        }

                        p.print(")");
                    }

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_call e => {
                    let wrap = level.gte(.@"new") || flags.contains(.forbid_call);
                    let target_flags = ExprFlag.Set.initEmpty();
                    if (e.optional_chain == null) {
                        target_flags = ExprFlag.Set.init({ has_non_optional_chain_parent: true });
                    } else if (flags.contains(.has_non_optional_chain_parent)) {
                        wrap = true;
                    }

                    const has_pure_comment = e.can_be_unwrapped_if_unused == .if_unused && p.options.print_dce_annotations;
                    if (has_pure_comment && level.gte(.postfix)) {
                        wrap = true;
                    }

                    if (wrap) {
                        p.print("(");
                    }

                    if (has_pure_comment) {
                        const was_stmt_start = p.stmt_start == p.writer.written;
                        p.printPure();
                        if (was_stmt_start) {
                            p.stmt_start = p.writer.written;
                        }
                    }
                    // We only want to generate an unbound eval() in CommonJS
                    p.call_target = e.target.data;

                    const is_unbound_eval = (!e.is_direct_eval &&
                        p.isUnboundEvalIdentifier(e.target) &&
                        e.optional_chain == null);

                    if (is_unbound_eval) {
                        p.print("(0,");
                        p.printSpace();
                        p.printExpr(e.target, .postfix, ExprFlag.Set.initEmpty());
                        p.print(")");
                    } else {
                        p.printExpr(e.target, .postfix, target_flags);
                    }

                    if (e.optional_chain != null && (e.optional_chain ?? unreachable) == .start) {
                        p.print("?.");
                    }
                    p.print("(");
                    const args = e.args.slice();

                    if (args.len > 0) {
                        p.printExpr(args[0], .comma, ExprFlag.Set.initEmpty());
                        for (arg of args[1..]) {
                            p.print(",");
                            p.printSpace();
                            p.printExpr(arg, .comma, ExprFlag.Set.initEmpty());
                        }
                    }
                    if (e.close_paren_loc.start > expr.loc.start) {
                        p.addSourceMapping(e.close_paren_loc);
                    }
                    p.print(")");
                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_require_main => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);

                    if (require_ref := p.options.require_ref) {
                        p.printSymbol(require_ref);
                        p.print(".main");
                    } else if (p.options.module_type == .internal_bake_dev) {
                        p.print("false"); // there is no true main entry point
                    } else {
                        p.print("require.main");
                    }
                },
                .e_require_call_target => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);

                    if (require_ref := p.options.require_ref) {
                        p.printSymbol(require_ref);
                    } else if (p.options.module_type == .internal_bake_dev) {
                        p.printSymbol(p.options.hmr_ref);
                        p.print(".require");
                    } else {
                        p.print("require");
                    }
                },
                .e_require_resolve_call_target => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);

                    if (require_ref := p.options.require_ref) {
                        p.printSymbol(require_ref);
                        p.print(".resolve");
                    } else if (p.options.module_type == .internal_bake_dev) {
                        p.printSymbol(p.options.hmr_ref);
                        p.print(".requireResolve");
                    } else {
                        p.print("require.resolve");
                    }
                },
                .e_require_string e => {
                    if (!rewrite_esm_to_cjs) {
                        p.printRequireOrImportExpr(
                            e.import_record_index,
                            e.unwrapped_id != std.math.maxInt(u32),
                            &empty_comments,
                            Expr.empty,
                            level,
                            flags,
                        );
                    }
                },
                .e_require_resolve_string e => {
                    const wrap = level.gte(.@"new") || flags.contains(.forbid_call);
                    if (wrap) {
                        p.print("(");
                    }

                    p.printSpaceBeforeIdentifier();

                    if (require_ref := p.options.require_ref) {
                        p.printSymbol(require_ref);
                        p.print(".resolve");
                    } else if (p.options.module_type == .internal_bake_dev) {
                        p.printSymbol(p.options.hmr_ref);
                        p.print(".requireResolve");
                    } else {
                        p.print("require.resolve");
                    }

                    p.print("(");
                    p.printStringLiteralUTF8(p.importRecord(e.import_record_index).path.text, true);
                    p.print(")");

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_import e => {
                    // Handle non-string expressions
                    if (e.isImportRecordNull()) {
                        const wrap = level.gte(.@"new") || flags.contains(.forbid_call);
                        if (wrap) {
                            p.print("(");
                        }

                        p.printSpaceBeforeIdentifier();
                        p.addSourceMapping(expr.loc);
                        if (p.options.module_type == .internal_bake_dev) {
                            p.printSymbol(p.options.hmr_ref);
                            p.print(".dynamicImport(");
                        } else {
                            p.print("import(");
                        }
                        // TODO:
                        // if (e.leading_interior_comments.len > 0) {
                        //     p.printNewline();
                        //     p.indent();
                        //     for (e.leading_interior_comments) |comment| {
                        //         p.printIndentedComment(comment.text);
                        //     }
                        //     p.printIndent();
                        // }
                        p.printExpr(e.expr, .comma, ExprFlag.Set.initEmpty());

                        if (!e.options.isMissing()) {
                            p.printWhitespacer(ws(", "));
                            p.printExpr(e.options, .comma, {});
                        }

                        // TODO:
                        // if (e.leading_interior_comments.len > 0) {
                        //     p.printNewline();
                        //     p.unindent();
                        //     p.printIndent();
                        // }
                        p.print(")");
                        if (wrap) {
                            p.print(")");
                        }
                    } else {
                        p.printRequireOrImportExpr(
                            e.import_record_index,
                            false,
                            [], // e.leading_interior_comments,
                            e.options,
                            level,
                            flags,
                        );
                    }
                },
                .e_dot e => {
                    const isOptionalChain = e.optional_chain == .start;

                    let wrap = false;
                    if (e.optional_chain == null) {
                        flags.insert(.has_non_optional_chain_parent);

                        // Inline cross-module TypeScript enum references here
                        if (inlined := p.tryToGetImportedEnumValue(e.target, e.name)) {
                            p.printInlinedEnum(inlined, e.name, level);
                            return;
                        }
                    } else {
                        if (flags.contains(.has_non_optional_chain_parent)) {
                            wrap = true;
                            p.print("(");
                        }

                        flags.remove(.has_non_optional_chain_parent);
                    }
                    flags.setIntersection(ExprFlag.Set.init({ has_non_optional_chain_parent: true, forbid_call: true }));

                    p.printExpr(
                        e.target,
                        .postfix,
                        flags,
                    );

                    if (js_lexer.isIdentifier(e.name)) {
                        if (isOptionalChain) {
                            p.print("?.");
                        } else {
                            if (p.prev_num_end == p.writer.written) {
                                // "1.toString" is a syntax error, so print "1 .toString" instead
                                p.print(" ");
                            }

                            p.print(".");
                        }

                        p.addSourceMapping(e.name_loc);
                        p.printIdentifier(e.name);
                    } else {
                        if (isOptionalChain) {
                            p.print("?.[");
                        } else {
                            p.print("[");
                        }

                        p.printStringLiteralUTF8(e.name, false);

                        p.print("]");
                    }

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_index e => {
                    let wrap = false;
                    if (e.optional_chain == null) {
                        flags.insert(.has_non_optional_chain_parent);

                        if (str := e.index.data.@"as"(.e_string)) {
                            str.resolveRopeIfNeeded(p.options.allocator);

                            if (str.isUTF8()) if (value := p.tryToGetImportedEnumValue(e.target, str.data)) {
                                p.printInlinedEnum(value, str.data, level);
                                return;
                            }
                        }
                    } else {
                        if (flags.contains(.has_non_optional_chain_parent)) {
                            wrap = true;
                            p.print("(");
                        }
                        flags.remove(.has_non_optional_chain_parent);
                    }

                    p.printExpr(e.target, .postfix, flags);

                    const is_optional_chain_start = e.optional_chain == .start;
                    if (is_optional_chain_start) {
                        p.print("?.");
                    }

                    switch (e.index.data) {
                        .e_private_identifier => {
                            const priv_id = e.index.data.e_private_identifier;
                            if (!is_optional_chain_start) {
                                p.print(".");
                            }
                            p.addSourceMapping(e.index.loc);
                            p.printSymbol(priv_id.ref);
                        },
                        else => {
                            p.print("[");
                            p.addSourceMapping(e.index.loc);
                            p.printExpr(e.index, .lowest, ExprFlag.Set.initEmpty());
                            p.print("]");
                        },
                    }

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_if e => {
                    const wrap = level.gte(.conditional);
                    if (wrap) {
                        p.print("(");
                        flags.remove(.forbid_in);
                    }
                    p.printExpr(e.test_, .conditional, flags);
                    p.printSpace();
                    p.print("?");
                    p.printSpace();
                    p.printExpr(e.yes, .yield, ExprFlag.Set.initEmpty());
                    p.printSpace();
                    p.print(":");
                    p.printSpace();
                    flags.insert(.forbid_in);
                    p.printExpr(e.no, .yield, flags);
                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_arrow e => {
                    const wrap = level.gte(.assign);

                    if (wrap) {
                        p.print("(");
                    }

                    if (e.is_async) {
                        p.addSourceMapping(expr.loc);
                        p.printSpaceBeforeIdentifier();
                        p.print("async");
                        p.printSpace();
                    }

                    p.printFnArgs(if (e.is_async) null else expr.loc, e.args, e.has_rest_arg, true);
                    p.printWhitespacer(ws(" => "));

                    let wasPrinted = false;
                    if (e.body.stmts.len == 1 && e.prefer_expr) {
                        switch (e.body.stmts[0].data) {
                            .s_return => {
                                if (val := e.body.stmts[0].data.s_return.value) {
                                    p.arrow_expr_start = p.writer.written;
                                    p.printExpr(val, .comma, ExprFlag.Set.init({ forbid_in: true }));
                                    wasPrinted = true;
                                }
                            },
                            else => {},
                        }
                    }

                    if (!wasPrinted) {
                        p.printBlock(e.body.loc, e.body.stmts, null, TopLevel.init(.no));
                    }

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_function e => {
                    const n = p.writer.written;
                    const wrap = p.stmt_start == n || p.export_default_start == n;

                    if (wrap) {
                        p.print("(");
                    }

                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    if (e.func.flags.contains(.is_async)) {
                        p.print("async ");
                    }
                    p.print("function");
                    if (e.func.flags.contains(.is_generator)) {
                        p.print("*");
                        p.printSpace();
                    }

                    if (sym := e.func.name) {
                        p.printSpaceBeforeIdentifier();
                        p.addSourceMapping(sym.loc);
                        p.printSymbol(sym.ref ?? Output.panic("internal error: expected E.Function's name symbol to have a ref", {}));
                    }

                    p.printFunc(e.func);
                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_class e => {
                    const n = p.writer.written;
                    const wrap = p.stmt_start == n || p.export_default_start == n;
                    if (wrap) {
                        p.print("(");
                    }

                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print("class");
                    if (name := e.class_name) {
                        p.print(" ");
                        p.addSourceMapping(name.loc);
                        p.printSymbol(name.ref ?? Output.panic("internal error: expected E.Class's name symbol to have a ref", {}));
                    }
                    p.printClass(e.*);
                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_array e => {
                    p.addSourceMapping(expr.loc);
                    p.print("[");
                    const items = e.items.slice();
                    if (items.len > 0) {
                        if (!e.is_single_line) {
                            p.indent();
                        }

                        let i: usize = 0;
                        for (item of items) {
                            defer i += 1;
                            if (i != 0) {
                                p.print(",");
                                if (e.is_single_line) {
                                    p.printSpace();
                                }
                            }
                            if (!e.is_single_line) {
                                p.printNewline();
                                p.printIndent();
                            }
                            p.printExpr(item, .comma, ExprFlag.Set.initEmpty());

                            if (i == items.len - 1 && item.data == .e_missing) {
                                // Make sure there's a comma after trailing missing items
                                p.print(",");
                            }
                        }

                        if (!e.is_single_line) {
                            p.unindent();
                            p.printNewline();
                            p.printIndent();
                        }
                    }

                    if (e.close_bracket_loc.start > expr.loc.start) {
                        p.addSourceMapping(e.close_bracket_loc);
                    }

                    p.print("]");
                },
                .e_object e => {
                    const n = p.writer.written;
                    const wrap = if (comptime is_json)
                        false
                    else
                        p.stmt_start == n || p.arrow_expr_start == n;

                    if (wrap) {
                        p.print("(");
                    }
                    p.addSourceMapping(expr.loc);
                    p.print("{");
                    const props = expr.data.e_object.properties.slice();
                    if (props.len > 0) {
                        if (!e.is_single_line) {
                            p.indent();
                        }

                        if (e.is_single_line && !is_json) {
                            p.printSpace();
                        } else {
                            p.printNewline();
                            p.printIndent();
                        }
                        p.printProperty(props[0]);

                        if (props.len > 1) {
                            for (property of props[1..]) {
                                p.print(",");

                                if (e.is_single_line && !is_json) {
                                    p.printSpace();
                                } else {
                                    p.printNewline();
                                    p.printIndent();
                                }
                                p.printProperty(property);
                            }
                        }

                        if (e.is_single_line && !is_json) {
                            p.printSpace();
                        } else {
                            p.unindent();
                            p.printNewline();
                            p.printIndent();
                        }
                    }
                    if (e.close_brace_loc.start > expr.loc.start) {
                        p.addSourceMapping(e.close_brace_loc);
                    }
                    p.print("}");
                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_boolean, .e_branch_boolean => {
                    const bval = if (expr.data == .e_boolean) expr.data.e_boolean.value else expr.data.e_branch_boolean.value;
                    p.addSourceMapping(expr.loc);
                    if (p.options.minify_syntax) {
                        if (level.gte(Level.prefix)) {
                            p.print(if (bval) "(!0)" else "(!1)");
                        } else {
                            p.print(if (bval) "!0" else "!1");
                        }
                    } else {
                        p.printSpaceBeforeIdentifier();
                        p.print(if (bval) "true" else "false");
                    }
                },
                .e_string e => {
                    e.resolveRopeIfNeeded(p.options.allocator);
                    p.addSourceMapping(expr.loc);

                    // If this was originally a template literal, print it as one as long as we're not minifying
                    if (e.prefer_template && !p.options.minify_syntax) {
                        p.print("`");
                        p.printStringCharactersEString(e, '`');
                        p.print("`");
                        return;
                    }

                    p.printStringLiteralEString(e, true);
                },
                .e_template e => {
                    if (e.tag == null && (p.options.minify_syntax || p.was_lazy_export)) {
                        let replaced = std.array_list.Managed(E.TemplatePart).init(p.options.allocator);
                        let i: usize = 0;
                        for (_part of e.parts) {
                            defer i += 1;
                            let part = _part;
                            const inlined_value: js_ast.Expr? = switch (part.value.data) {
                                .e_name_of_symbol e2 => Expr.init(
                                    E.String,
                                    E.String.init(p.mangledPropName(e2.ref)),
                                    part.value.loc,
                                ),
                                .e_dot => brk: {
                                    // TODO: handle inlining of dot properties
                                    break :brk null;
                                },
                                else => null,
                            };

                            if (value := inlined_value) {
                                if (replaced.items.len == 0) {
                                    bun.handleOom(replaced.appendSlice(e.parts[0..i]));
                                }
                                part.value = value;
                                bun.handleOom(replaced.append(part));
                            } else if (replaced.items.len > 0) {
                                bun.handleOom(replaced.append(part));
                            }
                        }

                        if (replaced.items.len > 0) {
                            let copy = e.*;
                            copy.parts = replaced.items;
                            const e2 = copy.fold(p.options.allocator, expr.loc);
                            switch (e2.data) {
                                .e_string => {
                                    p.print('"');
                                    p.printStringCharactersUTF8(e2.data.e_string.data, '"');
                                    p.print('"');
                                    return;
                                },
                                .e_template => {
                                    e.* = e2.data.e_template.*;
                                },
                                else => {},
                            }
                        }

                        // Convert no-substitution template literals into strings if it's smaller
                        if (e.parts.len == 0) {
                            p.addSourceMapping(expr.loc);
                            p.printStringCharactersEString(&e.head.cooked, '`');
                            return;
                        }
                    }

                    if (tag := e.tag) {
                        p.addSourceMapping(expr.loc);
                        // Optional chains are forbidden in template tags
                        if (expr.isOptionalChain()) {
                            p.print("(");
                            p.printExpr(tag, .lowest, ExprFlag.Set.initEmpty());
                            p.print(")");
                        } else {
                            p.printExpr(tag, .postfix, ExprFlag.Set.initEmpty());
                        }
                    } else {
                        p.addSourceMapping(expr.loc);
                    }

                    p.print("`");
                    switch (e.head) {
                        .raw raw => p.printRawTemplateLiteral(raw),
                        .cooked => {
                            let cooked = &e.head.cooked;
                            if (cooked.isPresent()) {
                                cooked.resolveRopeIfNeeded(p.options.allocator);
                                p.printStringCharactersEString(cooked, '`');
                            }
                        },
                    }

                    for (*part of e.parts) {
                        p.print("${");
                        p.printExpr(part.value, .lowest, ExprFlag.Set.initEmpty());
                        p.print("}");
                        switch (part.tail) {
                            .raw raw => p.printRawTemplateLiteral(raw),
                            .cooked => {
                                let cooked = &part.tail.cooked;
                                if (cooked.isPresent()) {
                                    cooked.resolveRopeIfNeeded(p.options.allocator);
                                    p.printStringCharactersEString(cooked, '`');
                                }
                            },
                        }
                    }
                    p.print("`");
                },
                .e_reg_exp e => {
                    p.addSourceMapping(expr.loc);
                    p.printRegExpLiteral(e);
                },
                .e_big_int e => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print(e.value);
                    p.print('n');
                },
                .e_number e => {
                    p.addSourceMapping(expr.loc);
                    p.printNumber(e.value, level);
                },
                .e_identifier e => {
                    const name = p.renamer.nameForSymbol(e.ref);
                    const wrap = p.writer.written == p.for_of_init_start && strings.eqlComptime(name, "let");

                    if (wrap) {
                        p.print("(");
                    }

                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.printIdentifier(name);

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_import_identifier e => {
                    // Potentially use a property access instead of an identifier
                    let didPrint = false;

                    const sym_ref = if (p.options.module_type != .internal_bake_dev)
                        p.symbols().follow(e.ref)
                    else
                        e.ref;
                    const symbol = p.symbols().get(sym_ref).?;

                    // if (bun.strings.eql(symbol.original_name, "registerClientReference")) {
                    //     @breakpoint();
                    // }

                    if (symbol.import_item_status == .missing) {
                        p.printUndefined(expr.loc, level);
                        didPrint = true;
                    } else if (namespace := symbol.namespace_alias) {
                        if (namespace.import_record_index < p.import_records.len) {
                            const import_record = p.importRecord(namespace.import_record_index);
                            if (namespace.was_originally_property_access) {
                                let wrap = false;
                                didPrint = true;

                                if (target := p.call_target) {
                                    wrap = e.was_originally_identifier && (target == .e_identifier &&
                                        target.e_identifier.ref.eql(expr.data.e_import_identifier.ref));
                                }

                                if (wrap) {
                                    p.printWhitespacer(ws("(0, "));
                                }
                                p.printSpaceBeforeIdentifier();
                                p.addSourceMapping(expr.loc);
                                p.printNamespaceAlias(import_record.*, namespace);

                                if (wrap) {
                                    p.print(")");
                                }
                            } else if (import_record.flags.was_originally_require && import_record.path.is_disabled) {
                                p.addSourceMapping(expr.loc);

                                if (import_record.flags.handles_import_errors) {
                                    p.printRequireError(import_record.path.text);
                                } else {
                                    p.printDisabledImport();
                                }
                                didPrint = true;
                            }
                        }

                        if (!didPrint) {
                            didPrint = true;

                            let wrap = false;
                            if (target := p.call_target) {
                                wrap = e.was_originally_identifier && (target == .e_identifier &&
                                    target.e_identifier.ref.eql(expr.data.e_import_identifier.ref));
                            }

                            if (wrap) {
                                p.printWhitespacer(ws("(0, "));
                            }

                            p.printSpaceBeforeIdentifier();
                            p.addSourceMapping(expr.loc);
                            p.printSymbol(namespace.namespace_ref);
                            const alias = namespace.alias;
                            if (js_lexer.isIdentifier(alias)) {
                                p.print(".");
                                // TODO: addSourceMappingForName
                                p.printIdentifier(alias);
                            } else {
                                p.print("[");
                                // TODO: addSourceMappingForName
                                // p.addSourceMappingForName(alias);
                                p.printStringLiteralUTF8(alias, false);
                                p.print("]");
                            }

                            if (wrap) {
                                p.print(")");
                            }
                        }
                    }
                    // else if (p.options.const_values.get(ref)) |const_value| {
                    //     p.printSpaceBeforeIdentifier();
                    //     // TODO: addSourceMappingForName
                    //     // p.addSourceMappingForName(renamer.nameForSymbol(e.ref));
                    //     p.addSourceMapping(expr.loc);
                    //     p.printExpr(const_value, level, flags);
                    //     didPrint = true;
                    // }

                    if (!didPrint) {
                        p.printSpaceBeforeIdentifier();
                        p.addSourceMapping(expr.loc);
                        p.printSymbol(e.ref);
                    }
                },
                .e_await e => {
                    const wrap = level.gte(.prefix);

                    if (wrap) {
                        p.print("(");
                    }

                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print("await");
                    p.printSpace();
                    p.printExpr(e.value, Level.sub(.prefix, 1), ExprFlag.Set.initEmpty());

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_yield e => {
                    const wrap = level.gte(.assign);
                    if (wrap) {
                        p.print("(");
                    }

                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(expr.loc);
                    p.print("yield");

                    if (val := e.value) {
                        if (e.is_star) {
                            p.print("*");
                        }
                        p.printSpace();
                        p.printExpr(val, .yield, ExprFlag.Set.initEmpty());
                    }

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_unary e => {
                    // 4.00 ms  eums.EnumIndexer(src.js_ast.Op.Code).indexOf
                    const entry: *const Op = Op.Table.getPtrConst(e.op);
                    const wrap = level.gte(entry.level);

                    if (wrap) {
                        p.print("(");
                    }

                    if (!e.op.isPrefix()) {
                        p.printExpr(e.value, Op.Level.sub(.postfix, 1), ExprFlag.Set.initEmpty());
                    }

                    if (entry.is_keyword) {
                        p.printSpaceBeforeIdentifier();
                        p.addSourceMapping(expr.loc);
                        p.print(entry.text);
                        p.printSpace();
                    } else {
                        p.printSpaceBeforeOperator(e.op);
                        if (e.op.isPrefix()) {
                            p.addSourceMapping(expr.loc);
                        }
                        p.print(entry.text);
                        p.prev_op = e.op;
                        p.prev_op_end = p.writer.written;
                    }

                    if (e.op.isPrefix()) {
                        // Never turn "typeof (0, x)" into "typeof x" or "delete (0, x)" into "delete x"
                        if ((e.op == .un_typeof && !e.flags.was_originally_typeof_identifier && p.isUnboundIdentifier(&e.value)) ||
                            (e.op == .un_delete && !e.flags.was_originally_delete_of_identifier_or_property_access && isIdentifierOrNumericConstantOrPropertyAccess(&e.value)))
                        {
                            p.print("(0,");
                            p.printSpace();
                            p.printExpr(e.value, Op.Level.sub(.prefix, 1), ExprFlag.Set.initEmpty());
                            p.print(")");
                        } else {
                            p.printExpr(e.value, Op.Level.sub(.prefix, 1), ExprFlag.Set.initEmpty());
                        }
                    }

                    if (wrap) {
                        p.print(")");
                    }
                },
                .e_binary e => {
                    // The handling of binary expressions is convoluted because we're using
                    // iteration on the heap instead of recursion on the call stack to avoid
                    // stack overflow for deeply-nested ASTs. See the comments for the similar
                    // code in the JavaScript parser for details.
                    let v = BinaryExpressionVisitor{
                        e: e,
                        level: level,
                        flags: flags,
                        entry: Op.Table.getPtrConst(e.op),
                    };

                    // Use a single stack to reduce allocation overhead
                    const stack_bottom = p.binary_expression_stack.items.len;

                    while (true) {
                        if (!v.checkAndPrepare(p)) {
                            break;
                        }

                        const left = v.e.left;
                        const left_binary: (*E.Binary)? = if (left.data == .e_binary) left.data.e_binary else null;

                        // Stop iterating if iteration doesn't apply to the left node
                        if (left_binary == null) {
                            p.printExpr(left, v.left_level, v.left_flags);
                            v.visitRightAndFinish(p);
                            break;
                        }

                        // Only allocate heap memory on the stack for nested binary expressions
                        bun.handleOom(p.binary_expression_stack.append(v));
                        v = BinaryExpressionVisitor{
                            e: left_binary.?,
                            level: v.left_level,
                            flags: v.left_flags,
                        };
                    }

                    // Process all binary operations from the deepest-visited node back toward
                    // our original top-level binary operation
                    while (p.binary_expression_stack.items.len > stack_bottom) {
                        let last = p.binary_expression_stack.pop().?;
                        last.visitRightAndFinish(p);
                    }
                },
                .e_inlined_enum e => {
                    p.printExpr(e.value, level, flags);
                    if (!p.options.minify_whitespace && !p.options.minify_identifiers) {
                        p.print(" /* ");
                        p.print(e.comment);
                        p.print(" */");
                    }
                },
                .e_name_of_symbol e => {
                    const name = p.mangledPropName(e.ref);
                    p.addSourceMappingForName(expr.loc, name, e.ref);

                    if (!p.options.minify_whitespace && e.has_property_key_comment) {
                        p.print(" /* @__KEY__ */");
                    }

                    p.print('"');
                    p.printStringCharactersUTF8(name, '"');
                    p.print('"');
                },

                .e_jsx_element,
                .e_private_identifier => {
                    if (Environment.isDebug)
                        Output.panic("Unexpected expression of type .{s}", .{@tagName(expr.data)});
                },
            }
        }

        static fn printSpaceBeforeOperator(p: *Printer, next: Op.Code) {
            if (p.prev_op_end == p.writer.written) {
                const prev = p.prev_op;
                // "+ + y" => "+ +y"
                // "+ ++ y" => "+ ++y"
                // "x + + y" => "x+ +y"
                // "x ++ + y" => "x+++y"
                // "x + ++ y" => "x+ ++y"
                // "-- >" => "-- >"
                // "< ! --" => "<! --"
                if (((prev == Op.Code.bin_add || prev == Op.Code.un_pos) && (next == Op.Code.bin_add || next == Op.Code.un_pos || next == Op.Code.un_pre_inc)) ||
                    ((prev == Op.Code.bin_sub || prev == Op.Code.un_neg) && (next == Op.Code.bin_sub || next == Op.Code.un_neg || next == Op.Code.un_pre_dec)) ||
                    (prev == Op.Code.un_post_dec && next == Op.Code.bin_gt) ||
                    (prev == Op.Code.un_not && next == Op.Code.un_pre_dec && p.writer.written > 1 && p.writer.prevPrevChar() == '<'))
                {
                    p.print(" ");
                }
            }
        }

        static fn printDotThenSuffix(p: *Printer) {
            p.print(")");
        }

        // This assumes the string has already been quoted.
        static fn printStringCharactersEString(p: *Printer, str: *const E.String, c: u8) {
            if (!str.isUTF8()) {
                p.printStringCharactersUTF16(str.slice16(), c);
            } else {
                p.printStringCharactersUTF8(str.data, c);
            }
        }

        static fn printNamespaceAlias(p: *Printer, rec: ImportRecord, namespace: G.NamespaceAlias) {
            * = rec;
            p.printSymbol(namespace.namespace_ref);

            // In the case of code like this:
            // module.exports = require("foo")
            // if "foo" is bundled
            // then we access it as the namespace symbol itself
            // that means the namespace alias is empty
            if (namespace.alias.len == 0) return;

            if (js_lexer.isIdentifier(namespace.alias)) {
                p.print(".");
                p.printIdentifier(namespace.alias);
            } else {
                p.print("[");
                p.printStringLiteralUTF8(namespace.alias, false);
                p.print("]");
            }
        }

        static fn printRegExpLiteral(p: *Printer, e: *const E.RegExp) {
            const n = p.writer.written;

            // Avoid forming a single-line comment
            if (n > 0 && p.writer.prevChar() == '/') {
                p.print(" ");
            }

            if (comptime is_bun_platform) {
                // Translate any non-ASCII to unicode escape sequences
                let ascii_start: usize = 0;
                let is_ascii = false;
                let iter = CodepointIterator.init(e.value);
                let cursor: CodepointIterator.Cursor = .{};
                while (iter.next(&cursor)) {
                    switch (cursor.c) {
                        first_ascii..=last_ascii => {
                            if (!is_ascii) {
                                ascii_start = cursor.i;
                                is_ascii = true;
                            }
                        },
                        else => {
                            if (is_ascii) {
                                p.print(e.value[ascii_start..cursor.i]);
                                is_ascii = false;
                            }

                            switch (cursor.c) {
                                0..=0xFFFF => {
                                    const ubuf: [6]u8 = [
                                        '\\',
                                        'u',
                                        hex_chars[cursor.c >> 12],
                                        hex_chars[(cursor.c >> 8) & 15],
                                        hex_chars[(cursor.c >> 4) & 15],
                                        hex_chars[cursor.c & 15],
                                    ];
                                    p.print(ubuf);
                                },

                                else => {
                                    const c = cursor.c;
                                    const k = c - 0x10000;
                                    const lo = @as(usize, @intCast(first_high_surrogate + ((k >> 10) & 0x3FF)));
                                    const hi = @as(usize, @intCast(first_low_surrogate + (k & 0x3FF)));

                                    const ubuf: [12]u8 = [
                                        '\\',
                                        'u',
                                        hex_chars[lo >> 12],
                                        hex_chars[(lo >> 8) & 15],
                                        hex_chars[(lo >> 4) & 15],
                                        hex_chars[lo & 15],
                                        '\\',
                                        'u',
                                        hex_chars[hi >> 12],
                                        hex_chars[(hi >> 8) & 15],
                                        hex_chars[(hi >> 4) & 15],
                                        hex_chars[hi & 15],
                                    ];
                                    p.print(&ubuf);
                                },
                            }
                        },
                    }
                }

                if (is_ascii) {
                    p.print(e.value[ascii_start..]);
                }
            } else {
                // UTF8 sequence is fine
                p.print(e.value);
            }

            // Need a space before the next identifier to avoid it turning into flags
            p.prev_reg_exp_end = p.writer.written;
        }

        static fn printProperty(p: *Printer, item_in: G.Property) {
            let item = item_in;
            if (comptime !is_json) {
                if (item.kind == .spread) {
                    if (comptime is_json && Environment.allow_assert)
                        unreachable;
                    p.print("...");
                    p.printExpr(item.value.?, .comma, ExprFlag.Set.initEmpty());
                    return;
                }

                // Handle key syntax compression for cross-module constant inlining of enums
                if (p.options.minify_syntax && item.flags.contains(.is_computed)) {
                    if (dot := item.key.?.data.@"as"(.e_dot)) {
                        if (value := p.tryToGetImportedEnumValue(dot.target, dot.name)) {
                            switch (value) {
                                .string str => {
                                    item.key.?.data = { e_string: str };

                                    // Problematic key names must stay computed for correctness
                                    if (!str.eqlComptime("__proto__") && !str.eqlComptime("constructor") && !str.eqlComptime("prototype")) {
                                        item.flags.setPresent(.is_computed, false);
                                    }
                                },
                                .number num => {
                                    item.key.?.data = { e_number: { value: num } };
                                    item.flags.setPresent(.is_computed, false);
                                },
                            }
                        }
                    }
                }

                if (item.flags.contains(.is_static)) {
                    if (comptime is_json && Environment.allow_assert)
                        unreachable;
                    p.print("static");
                    p.printSpace();
                }

                switch (item.kind) {
                    .get => {
                        if (comptime is_json && Environment.allow_assert)
                            unreachable;
                        p.printSpaceBeforeIdentifier();
                        p.print("get");
                        p.printSpace();
                    },
                    .set => {
                        if (comptime is_json && Environment.allow_assert)
                            unreachable;
                        p.printSpaceBeforeIdentifier();
                        p.print("set");
                        p.printSpace();
                    },
                    .auto_accessor => {
                        p.printSpaceBeforeIdentifier();
                        p.print("accessor");
                        p.printSpace();
                    },
                    else => {},
                }

                if (val := item.value) {
                    switch (val.data) {
                        .e_function func => {
                            if (item.flags.contains(.is_method)) {
                                if (func.func.flags.contains(.is_async)) {
                                    p.printSpaceBeforeIdentifier();
                                    p.print("async");
                                }

                                if (func.func.flags.contains(.is_generator)) {
                                    p.print("*");
                                }

                                if (func.func.flags.contains(.is_generator) && func.func.flags.contains(.is_async)) {
                                    p.printSpace();
                                }
                            }
                        },
                        else => {},
                    }

                    // If var is declared in a parent scope and var is then written via destructuring pattern, key is null
                    // example:
                    //  var foo = 1;
                    //  if (true) {
                    //      var { foo } = { foo: 2 };
                    //  }
                    if (item.key == null) {
                        p.printExpr(val, .comma, ExprFlag.Set.initEmpty());
                        return;
                    }
                }
            }

            const _key = item.key.?;

            if (!is_json && item.flags.contains(.is_computed)) {
                p.print("[");
                p.printExpr(_key, .comma, ExprFlag.Set.initEmpty());
                p.print("]");

                if (val := item.value) {
                    switch (val.data) {
                        .e_function func => {
                            if (item.flags.contains(.is_method)) {
                                p.printFunc(func.func);
                                return;
                            }
                        },
                        else => {},
                    }

                    p.print(":");
                    p.printSpace();
                    p.printExpr(val, .comma, ExprFlag.Set.initEmpty());
                }

                if (initial := item.initializer) {
                    p.printInitializer(initial);
                }
                return;
            }

            switch (_key.data) {
                .e_private_identifier priv_id => {
                    if (comptime is_json) {
                        unreachable;
                    }

                    p.addSourceMapping(_key.loc);
                    p.printSymbol(priv_id.ref);
                },
                .e_string key => {
                    p.addSourceMapping(_key.loc);
                    if (key.isUTF8()) {
                        key.resolveRopeIfNeeded(p.options.allocator);
                        p.printSpaceBeforeIdentifier();
                        let allow_shorthand: bool = true;
                        // In react/cjs/react.development.js, there's part of a function like this:
                        // var escaperLookup = {
                        //     "=": "=0",
                        //     ":": "=2"
                        //   };
                        // While each of those property keys are ASCII, a subset of ASCII is valid as the start of an identifier
                        // "=" and ":" are not valid
                        // So we need to check
                        if (!is_json && js_lexer.isIdentifier(key.data)) {
                            p.printIdentifier(key.data);
                        } else {
                            allow_shorthand = false;
                            p.printStringLiteralEString(key, false);
                        }

                        // Use a shorthand property if the names are the same
                        if (val := item.value) {
                            switch (val.data) {
                                .e_identifier e => {
                                    if (key.eql(string, p.renamer.nameForSymbol(e.ref))) {
                                        if (initial := item.initializer) {
                                            p.printInitializer(initial);
                                        }
                                        if (allow_shorthand) {
                                            return;
                                        }
                                    }
                                },
                                .e_import_identifier e => inner: {
                                    const sym_ref = p.symbols().follow(e.ref);
                                    if (p.options.input_files_for_dev_server != null)
                                        break :inner;
                                    // if (p.options.const_values.count() > 0 and p.options.const_values.contains(ref))
                                    //     break :inner;

                                    if (symbol := p.symbols().get(sym_ref)) {
                                        if (symbol.namespace_alias == null && strings.eql(key.data, p.renamer.nameForSymbol(e.ref))) {
                                            if (initial := item.initializer) {
                                                p.printInitializer(initial);
                                            }
                                            if (allow_shorthand) {
                                                return;
                                            }
                                        }
                                    }
                                },
                                else => {},
                            }
                        }
                    } else if (!is_json && p.canPrintIdentifierUTF16(key.slice16())) {
                        p.printSpaceBeforeIdentifier();
                        p.printIdentifierUTF16(key.slice16()) catch unreachable;

                        // Use a shorthand property if the names are the same
                        if (val := item.value) {
                            switch (val.data) {
                                .e_identifier e => {

                                    // TODO: is needing to check item.flags.contains(.was_shorthand) a bug?
                                    // esbuild doesn't have to do that..
                                    // maybe it's a symptom of some other underlying issue
                                    // or maybe, it's because i'm not lowering the same way that esbuild does.
                                    if (item.flags.contains(.was_shorthand) || strings.utf16EqlString(key.slice16(), p.renamer.nameForSymbol(e.ref))) {
                                        if (initial := item.initializer) {
                                            p.printInitializer(initial);
                                        }
                                        return;
                                    }
                                    // if (strings) {}
                                },
                                // .e_import_identifier e => inner: {
                                .e_import_identifier e => {
                                    const sym_ref = p.symbols().follow(e.ref);

                                    // if (p.options.const_values.count() > 0 and p.options.const_values.contains(ref))
                                    //     break :inner;

                                    if (symbol := p.symbols().get(sym_ref)) {
                                        if (symbol.namespace_alias == null && strings.utf16EqlString(key.slice16(), p.renamer.nameForSymbol(e.ref))) {
                                            if (initial := item.initializer) {
                                                p.printInitializer(initial);
                                            }
                                            return;
                                        }
                                    }
                                },
                                else => {},
                            }
                        }
                    } else {
                        const c = bestQuoteCharForString(u16, key.slice16(), false);
                        p.print(c);
                        p.printStringCharactersUTF16(key.slice16(), c);
                        p.print(c);
                    }
                },
                else => {
                    if (comptime is_json) {
                        unreachable;
                    }

                    p.printExpr(_key, .lowest, ExprFlag.Set.initEmpty());
                },
            }

            if (item.kind != .normal && item.kind != .auto_accessor) {
                if (comptime is_json) {
                    bun.unreachablePanic("item.kind must be normal in json", {});
                }

                switch (item.value.?.data) {
                    .e_function func => {
                        p.printFunc(func.func);
                        return;
                    },
                    else => {},
                }
            }

            if (val := item.value) {
                switch (val.data) {
                    .e_function f => {
                        if (item.flags.contains(.is_method)) {
                            p.printFunc(f.func);

                            return;
                        }
                    },
                    else => {},
                }

                p.print(":");
                p.printSpace();
                p.printExpr(val, .comma, ExprFlag.Set.initEmpty());
            }

            if (comptime is_json) {
                bun.assert(item.initializer == null);
            }

            if (initial := item.initializer) {
                p.printInitializer(initial);
            }
        }

        static fn printInitializer(p: *Printer, initial: Expr) {
            p.printSpace();
            p.print("=");
            p.printSpace();
            p.printExpr(initial, .comma, ExprFlag.Set.initEmpty());
        }

        static fn printBinding(p: *Printer, binding: Binding, tlm: TopLevelAndIsExport) {
            switch (binding.data) {
                .b_missing => {},
                .b_identifier b => {
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(binding.loc);
                    p.printSymbol(b.ref);
                    if (may_have_module_info) {
                        if (mi := p.moduleInfo()) {
                            const local_name = p.renamer.nameForSymbol(b.ref);
                            const name_id = bun.handleOom(mi.str(local_name));
                            if (vk := tlm.is_top_level) bun.handleOom(mi.addVar(name_id, vk));
                            if (tlm.is_export) bun.handleOom(mi.addExportInfoLocal(name_id, name_id));
                        }
                    }
                },
                .b_array b => {
                    p.print("[");
                    if (b.items.len > 0) {
                        if (!b.is_single_line) {
                            p.indent();
                        }

                        let i: usize = 0;
                        for (*item of b.items) {
                            defer i += 1;
                            if (i != 0) {
                                p.print(",");
                                if (b.is_single_line) {
                                    p.printSpace();
                                }
                            }

                            if (!b.is_single_line) {
                                p.printNewline();
                                p.printIndent();
                            }

                            const is_last = i + 1 == b.items.len;
                            if (b.has_spread && is_last) {
                                p.print("...");
                            }

                            p.printBinding(item.binding, tlm);

                            p.maybePrintDefaultBindingValue(item);

                            // Make sure there's a comma after trailing missing items
                            if (is_last && item.binding.data == .b_missing) {
                                p.print(",");
                            }
                        }

                        if (!b.is_single_line) {
                            p.unindent();
                            p.printNewline();
                            p.printIndent();
                        }
                    }

                    p.print("]");
                },
                .b_object b => {
                    p.print("{");
                    if (b.properties.len > 0) {
                        if (!b.is_single_line) {
                            p.indent();
                        }

                        let i: usize = 0;
                        for (*property of b.properties) {
                            defer i += 1;
                            if (i != 0) {
                                p.print(",");
                            }

                            if (b.is_single_line) {
                                p.printSpace();
                            } else {
                                p.printNewline();
                                p.printIndent();
                            }

                            if (property.flags.contains(.is_spread)) {
                                p.print("...");
                            } else {
                                if (property.flags.contains(.is_computed)) {
                                    p.print("[");
                                    p.printExpr(property.key, .comma, ExprFlag.Set.initEmpty());
                                    p.print("]:");
                                    p.printSpace();

                                    p.printBinding(property.value, tlm);
                                    p.maybePrintDefaultBindingValue(property);
                                    continue;
                                }

                                switch (property.key.data) {
                                    .e_string str => {
                                        str.resolveRopeIfNeeded(p.options.allocator);
                                        p.addSourceMapping(property.key.loc);

                                        if (str.isUTF8()) {
                                            p.printSpaceBeforeIdentifier();
                                            // Example case:
                                            //      const Menu = React.memo(function Menu({
                                            //          aria-label: ariaLabel,
                                            //              ^
                                            // That needs to be:
                                            //          "aria-label": ariaLabel,
                                            if (js_lexer.isIdentifier(str.data)) {
                                                p.printIdentifier(str.data);

                                                // Use a shorthand property if the names are the same
                                                switch (property.value.data) {
                                                    .b_identifier id => {
                                                        if (str.eql(string, p.renamer.nameForSymbol(id.ref))) {
                                                            if (may_have_module_info) {
                                                                if (mi := p.moduleInfo()) {
                                                                    const name_id = bun.handleOom(mi.str(str.data));
                                                                    if (vk := tlm.is_top_level) bun.handleOom(mi.addVar(name_id, vk));
                                                                    if (tlm.is_export) bun.handleOom(mi.addExportInfoLocal(name_id, name_id));
                                                                }
                                                            }
                                                            p.maybePrintDefaultBindingValue(property);
                                                            continue;
                                                        }
                                                    },
                                                    else => {},
                                                }
                                            } else {
                                                p.printStringLiteralUTF8(str.data, false);
                                            }
                                        } else if (p.canPrintIdentifierUTF16(str.slice16())) {
                                            p.printSpaceBeforeIdentifier();
                                            p.printIdentifierUTF16(str.slice16()) catch unreachable;

                                            // Use a shorthand property if the names are the same
                                            switch (property.value.data) {
                                                .b_identifier id => {
                                                    if (strings.utf16EqlString(str.slice16(), p.renamer.nameForSymbol(id.ref))) {
                                                        if (may_have_module_info) {
                                                            if (mi := p.moduleInfo()) {
                                                                const str8 = str.slice(p.options.allocator);
                                                                const name_id = bun.handleOom(mi.str(str8));
                                                                if (vk := tlm.is_top_level) bun.handleOom(mi.addVar(name_id, vk));
                                                                if (tlm.is_export) bun.handleOom(mi.addExportInfoLocal(name_id, name_id));
                                                            }
                                                        }
                                                        p.maybePrintDefaultBindingValue(property);
                                                        continue;
                                                    }
                                                },
                                                else => {},
                                            }
                                        } else {
                                            p.printExpr(property.key, .lowest, ExprFlag.Set.initEmpty());
                                        }
                                    },
                                    else => {
                                        p.printExpr(property.key, .lowest, ExprFlag.Set.initEmpty());
                                    },
                                }

                                p.print(":");
                                p.printSpace();
                            }

                            p.printBinding(property.value, tlm);
                            p.maybePrintDefaultBindingValue(property);
                        }

                        if (!b.is_single_line) {
                            p.unindent();
                            p.printNewline();
                            p.printIndent();
                        } else {
                            p.printSpace();
                        }
                    }
                    p.print("}");
                },
            }
        }

        static fn maybePrintDefaultBindingValue(p: *Printer, property: auto) {
            if (default := property.default_value) {
                p.printSpace();
                p.print("=");
                p.printSpace();
                p.printExpr(default, .comma, ExprFlag.Set.initEmpty());
            }
        }

        static fn printStmt(p: *Printer, stmt: Stmt, tlmtlo: TopLevel)! {
            const prev_stmt_tag = p.prev_stmt_tag;

            defer {
                p.prev_stmt_tag = std.meta.activeTag(stmt.data);
            }

            switch (stmt.data) {
                .s_comment s => {
                    p.printIndent();
                    p.addSourceMapping(stmt.loc);
                    p.printIndentedComment(s.text);
                },
                .s_function s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    const name = s.func.name ?? Output.panic("Internal error: expected func to have a name ref", {});
                    const nameRef = name.ref ?? Output.panic("Internal error: expected func to have a name", {});

                    if (s.func.flags.contains(.is_export)) {
                        if (!rewrite_esm_to_cjs) {
                            p.print("export ");
                        }
                    }
                    if (s.func.flags.contains(.is_async)) {
                        p.print("async ");
                    }
                    p.print("function");
                    if (s.func.flags.contains(.is_generator)) {
                        p.print("*");
                        p.printSpace();
                    } else {
                        p.printSpaceBeforeIdentifier();
                    }

                    p.addSourceMapping(name.loc);
                    const local_name = p.renamer.nameForSymbol(nameRef);
                    p.printIdentifier(local_name);
                    p.printFunc(s.func);

                    if (may_have_module_info) {
                        if (mi := p.moduleInfo()) {
                            const name_id = bun.handleOom(mi.str(local_name));
                            // function declarations are lexical (block-scoped in modules);
                            // only record at true top-level, not inside blocks.
                            if (tlmtlo.is_top_level == .yes) bun.handleOom(mi.addVar(name_id, .lexical));
                            if (s.func.flags.contains(.is_export)) bun.handleOom(mi.addExportInfoLocal(name_id, name_id));
                        }
                    }

                    p.printNewline();

                    if (rewrite_esm_to_cjs && s.func.flags.contains(.is_export)) {
                        p.printIndent();
                        p.printBundledExport(local_name, local_name);
                        p.printSemicolonAfterStatement();
                    }
                },
                .s_class s => {
                    // Give an extra newline for readaiblity
                    if (prev_stmt_tag != .s_empty) {
                        p.printNewline();
                    }

                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    const nameRef = s.class.class_name.?.ref.?;
                    if (s.is_export) {
                        if (!rewrite_esm_to_cjs) {
                            p.print("export ");
                        }
                    }

                    p.print("class ");
                    p.addSourceMapping(s.class.class_name.?.loc);
                    const nameStr = p.renamer.nameForSymbol(nameRef);
                    p.printIdentifier(nameStr);
                    p.printClass(s.class);

                    if (may_have_module_info) {
                        if (mi := p.moduleInfo()) {
                            const name_id = bun.handleOom(mi.str(nameStr));
                            // class declarations are lexical (block-scoped in modules);
                            // only record at true top-level, not inside blocks.
                            if (tlmtlo.is_top_level == .yes) bun.handleOom(mi.addVar(name_id, .lexical));
                            if (s.is_export) bun.handleOom(mi.addExportInfoLocal(name_id, name_id));
                        }
                    }

                    if (rewrite_esm_to_cjs && s.is_export) {
                        p.printSemicolonAfterStatement();
                    } else {
                        p.printNewline();
                    }

                    if (rewrite_esm_to_cjs) {
                        if (s.is_export) {
                            p.printIndent();
                            p.printBundledExport(p.renamer.nameForSymbol(nameRef), p.renamer.nameForSymbol(nameRef));
                            p.printSemicolonAfterStatement();
                        }
                    }
                },
                .s_empty => {
                    if (p.prev_stmt_tag == .s_empty && p.options.indent.count == 0) return;

                    p.printIndent();
                    p.addSourceMapping(stmt.loc);
                    p.print(";");
                    p.printNewline();
                },
                .s_export_default s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("export default ");

                    switch (s.value) {
                        .expr expr => {

                            // Functions and classes must be wrapped to avoid confusion with their statement forms
                            p.export_default_start = p.writer.written;
                            p.printExpr(expr, .comma, ExprFlag.Set.initEmpty());
                            p.printSemicolonAfterStatement();

                            if (may_have_module_info) {
                                if (mi := p.moduleInfo()) {
                                    bun.handleOom(mi.addExportInfoLocal(bun.handleOom(mi.str("default")), .star_default));
                                    bun.handleOom(mi.addVar(.star_default, .lexical));
                                }
                            }
                            return;
                        },

                        .stmt s2 => {
                            switch (s2.data) {
                                .s_function func => {
                                    p.printSpaceBeforeIdentifier();

                                    if (func.func.flags.contains(.is_async)) {
                                        p.print("async ");
                                    }
                                    p.print("function");

                                    if (func.func.flags.contains(.is_generator)) {
                                        p.print("*");
                                        p.printSpace();
                                    } else {
                                        p.maybePrintSpace();
                                    }

                                    let func_name: ([]const u8)? = null;
                                    if (name := func.func.name) { func_name = p.renamer.nameForSymbol(name.ref.?); }
                                    if (fn_name := func_name) {
                                        p.printIdentifier(fn_name);
                                    }

                                    p.printFunc(func.func);

                                    if (may_have_module_info) {
                                        if (mi := p.moduleInfo()) {
                                            let local_name: analyze_transpiled_module.StringID = .star_default;
                                            if (f := func_name) { local_name = bun.handleOom(mi.str(f)); }
                                            bun.handleOom(mi.addExportInfoLocal(bun.handleOom(mi.str("default")), local_name));
                                            bun.handleOom(mi.addVar(local_name, .lexical));
                                        }
                                    }

                                    p.printNewline();
                                },
                                .s_class class_stmt => {
                                    p.printSpaceBeforeIdentifier();

                                    let class_name: ([]const u8)? = null;
                                    if (name := class_stmt.class.class_name) { class_name = p.renamer.nameForSymbol(name.ref ?? Output.panic("Internal error: Expected class to have a name ref", {})); }
                                    if (name := class_stmt.class.class_name) {
                                        p.print("class ");
                                        p.printIdentifier(p.renamer.nameForSymbol(name.ref.?));
                                    } else {
                                        p.print("class");
                                    }

                                    p.printClass(class_stmt.class);

                                    if (may_have_module_info) {
                                        if (mi := p.moduleInfo()) {
                                            let local_name: analyze_transpiled_module.StringID = .star_default;
                                            if (f := class_name) { local_name = bun.handleOom(mi.str(f)); }
                                            bun.handleOom(mi.addExportInfoLocal(bun.handleOom(mi.str("default")), local_name));
                                            bun.handleOom(mi.addVar(local_name, .lexical));
                                        }
                                    }

                                    p.printNewline();
                                },
                                else => {
                                    Output.panic("Internal error: unexpected export default stmt data", {});
                                },
                            }
                        },
                    }
                },
                .s_export_star s => {

                    // Give an extra newline for readaiblity
                    if (!prev_stmt_tag.isExportLike()) {
                        p.printNewline();
                    }
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);

                    if (s.alias != null)
                        p.printWhitespacer(comptime ws("export *").append(" as "))
                    else
                        p.printWhitespacer(comptime ws("export * from "));

                    if (alias := s.alias) {
                        p.printClauseAlias(alias.original_name);
                        p.print(" ");
                        p.printWhitespacer(ws("from "));
                    }

                    const irp = p.importRecord(s.import_record_index).path.text;
                    p.printImportRecordPath(p.importRecord(s.import_record_index));
                    p.printSemicolonAfterStatement();

                    if (may_have_module_info) {
                        if (mi := p.moduleInfo()) {
                            const irp_id = bun.handleOom(mi.str(irp));
                            bun.handleOom(mi.requestModule(irp_id, .none));
                            if (alias := s.alias) {
                                bun.handleOom(mi.addExportInfoNamespace(bun.handleOom(mi.str(alias.original_name)), irp_id));
                            } else {
                                bun.handleOom(mi.addExportInfoStar(irp_id));
                            }
                        }
                    }
                },
                .s_export_clause s => {
                    if (rewrite_esm_to_cjs) {
                        p.printIndent();
                        p.printSpaceBeforeIdentifier();
                        p.addSourceMapping(stmt.loc);

                        switch (s.items.len) {
                            0 => {},
                            // It unfortunately cannot be so simple as exports.foo = foo;
                            // If we have a lazy re-export and it's read-only..
                            // we have to overwrite it via Object.defineProperty

                            // Object.assign(__export, {prop1, prop2, prop3});
                            else => {
                                p.print("Object.assign");

                                p.print("(");
                                p.printModuleExportSymbol();
                                p.print(",");
                                p.printSpace();
                                p.print("{");
                                p.printSpace();
                                const last = s.items.len - 1;
                                let i: usize = 0;
                                for (item of s.items) {
                                    defer i += 1;
                                    const symbol = p.symbols().getWithLink(item.name.ref.?).?;
                                    const name = symbol.original_name;
                                    let did_print = false;

                                    if (namespace := symbol.namespace_alias) {
                                        const import_record = p.importRecord(namespace.import_record_index);
                                        if (namespace.was_originally_property_access) {
                                            p.printIdentifier(name);
                                            p.print(": () => ");
                                            p.printNamespaceAlias(import_record.*, namespace);
                                            did_print = true;
                                        }
                                    }

                                    if (!did_print) {
                                        p.printClauseAlias(item.alias);
                                        if (!strings.eql(name, item.alias)) {
                                            p.print(":");
                                            p.printSpaceBeforeIdentifier();
                                            p.printIdentifier(name);
                                        }
                                    }

                                    if (i < last) {
                                        p.print(",");
                                    }
                                }
                                p.print("})");
                                p.printSemicolonAfterStatement();
                            },
                        }
                        return;
                    }

                    // Give an extra newline for export default for readability
                    if (!prev_stmt_tag.isExportLike()) {
                        p.printNewline();
                    }

                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("export");
                    p.printSpace();

                    if (s.items.len == 0) {
                        p.print("{}");
                        p.printSemicolonAfterStatement();
                        return;
                    }

                    // This transforms code like this:
                    // import {Foo, Bar} from 'bundled-module';
                    // export {Foo, Bar};
                    // into
                    // export var Foo = $$bundledModule.Foo; (where $$bundledModule is created at import time)
                    // This is necessary unfortunately because export statements do not allow dot expressions
                    // The correct approach here is to invert the logic
                    // instead, make the entire module behave like a CommonJS module
                    // and export that one instead
                    // This particular code segment does the transform inline by adding an extra pass over export clauses
                    // and then swapRemove'ing them as we go
                    let array: std.ArrayListUnmanaged(js_ast.ClauseItem) = { items: s.items, capacity: s.items.len };
                    {
                        let i: usize = 0;
                        while (i < array.items.len) {
                            const item: js_ast.ClauseItem = array.items[i];

                            if (item.original_name.len > 0) {
                                if (symbol := p.symbols().get(item.name.ref.?)) {
                                    if (namespace := symbol.namespace_alias) {
                                        const import_record = p.importRecord(namespace.import_record_index);
                                        if (namespace.was_originally_property_access) {
                                            p.print("var ");
                                            p.printSymbol(item.name.ref.?);
                                            p.@"print = "();
                                            p.printNamespaceAlias(import_record.*, namespace);
                                            p.printSemicolonAfterStatement();
                                            * = array.swapRemove(i);

                                            if (i < array.items.len) {
                                                p.printIndent();
                                                p.printSpaceBeforeIdentifier();
                                                p.print("export");
                                                p.printSpace();
                                            }

                                            continue;
                                        }
                                    }
                                }
                            }

                            i += 1;
                        }

                        if (array.items.len == 0) {
                            return;
                        }

                        s.items = array.items;
                    }

                    p.print("{");

                    if (!s.is_single_line) {
                        p.indent();
                    } else {
                        p.printSpace();
                    }

                    let i: usize = 0;
                    for (item of s.items) {
                        defer i += 1;
                        if (i != 0) {
                            p.print(",");
                            if (s.is_single_line) {
                                p.printSpace();
                            }
                        }

                        if (!s.is_single_line) {
                            p.printNewline();
                            p.printIndent();
                        }

                        const name = p.renamer.nameForSymbol(item.name.ref.?);
                        p.printExportClauseItem(item);

                        if (may_have_module_info) {
                            if (mi := p.moduleInfo()) {
                                bun.handleOom(mi.addExportInfoLocal(bun.handleOom(mi.str(item.alias)), bun.handleOom(mi.str(name))));
                            }
                        }
                    }

                    if (!s.is_single_line) {
                        p.unindent();
                        p.printNewline();
                        p.printIndent();
                    } else {
                        p.printSpace();
                    }

                    p.print("}");
                    p.printSemicolonAfterStatement();
                },
                .s_export_from s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);

                    const import_record = p.importRecord(s.import_record_index);

                    p.printWhitespacer(ws("export {"));

                    if (!s.is_single_line) {
                        p.indent();
                    } else {
                        p.printSpace();
                    }

                    let i: usize = 0;
                    for (item of s.items) {
                        defer i += 1;
                        if (i != 0) {
                            p.print(",");
                            if (s.is_single_line) {
                                p.printSpace();
                            }
                        }

                        if (!s.is_single_line) {
                            p.printNewline();
                            p.printIndent();
                        }
                        p.printExportFromClauseItem(item);
                    }

                    if (!s.is_single_line) {
                        p.unindent();
                        p.printNewline();
                        p.printIndent();
                    } else {
                        p.printSpace();
                    }

                    p.printWhitespacer(ws("} from "));
                    const irp = import_record.path.text;
                    p.printImportRecordPath(import_record);
                    p.printSemicolonAfterStatement();

                    if (may_have_module_info) {
                        if (mi := p.moduleInfo()) {
                            const irp_id = bun.handleOom(mi.str(irp));
                            bun.handleOom(mi.requestModule(irp_id, .none));
                            for (item of s.items) {
                                const name = p.renamer.nameForSymbol(item.name.ref.?);
                                bun.handleOom(mi.addExportInfoIndirect(bun.handleOom(mi.str(item.alias)), bun.handleOom(mi.str(name)), irp_id));
                            }
                        }
                    }
                },
                .s_local s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    switch (s.kind) {
                        .k_const => {
                            p.printDeclStmt(s.is_export, "const", s.decls.slice(), tlmtlo);
                        },
                        .k_let => {
                            p.printDeclStmt(s.is_export, "let", s.decls.slice(), tlmtlo);
                        },
                        .k_var => {
                            p.printDeclStmt(s.is_export, "var", s.decls.slice(), tlmtlo);
                        },
                        .k_using => {
                            p.printDeclStmt(s.is_export, "using", s.decls.slice(), tlmtlo);
                        },
                        .k_await_using => {
                            p.printDeclStmt(s.is_export, "await using", s.decls.slice(), tlmtlo);
                        },
                    }
                },
                .s_if s => {
                    p.printIndent();
                    p.printIf(s, stmt.loc, tlmtlo.subVar());
                },
                .s_do_while s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("do");
                    const sub_var = tlmtlo.subVar();
                    switch (s.body.data) {
                        .s_block => {
                            p.printSpace();
                            p.printBlock(s.body.loc, s.body.data.s_block.stmts, s.body.data.s_block.close_brace_loc, sub_var);
                            p.printSpace();
                        },
                        else => {
                            p.printNewline();
                            p.indent();
                            p.printStmt(s.body, sub_var) catch unreachable;
                            p.printSemicolonIfNeeded();
                            p.unindent();
                            p.printIndent();
                        },
                    }

                    p.print("while");
                    p.printSpace();
                    p.print("(");
                    p.printExpr(s.test_, .lowest, ExprFlag.Set.initEmpty());
                    p.print(")");
                    p.printSemicolonAfterStatement();
                },
                .s_for_in s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("for");
                    p.printSpace();
                    p.print("(");
                    p.printForLoopInit(s.init);
                    p.printSpace();
                    p.printSpaceBeforeIdentifier();
                    p.print("in");
                    p.printSpace();
                    p.printExpr(s.value, .lowest, ExprFlag.Set.initEmpty());
                    p.print(")");
                    p.printBody(s.body, tlmtlo.subVar());
                },
                .s_for_of s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("for");
                    if (s.is_await) {
                        p.print(" await");
                    }
                    p.printSpace();
                    p.print("(");
                    p.for_of_init_start = p.writer.written;
                    p.printForLoopInit(s.init);
                    p.printSpace();
                    p.printSpaceBeforeIdentifier();
                    p.print("of");
                    p.printSpace();
                    p.printExpr(s.value, .comma, ExprFlag.Set.initEmpty());
                    p.print(")");
                    p.printBody(s.body, tlmtlo.subVar());
                },
                .s_while s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("while");
                    p.printSpace();
                    p.print("(");
                    p.printExpr(s.test_, .lowest, ExprFlag.Set.initEmpty());
                    p.print(")");
                    p.printBody(s.body, tlmtlo.subVar());
                },
                .s_with s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("with");
                    p.printSpace();
                    p.print("(");
                    p.printExpr(s.value, .lowest, ExprFlag.Set.initEmpty());
                    p.print(")");
                    p.printBody(s.body, tlmtlo.subVar());
                },
                .s_label s => {
                    if (!p.options.minify_whitespace && p.options.indent.count > 0) {
                        p.printIndent();
                    }
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.printSymbol(s.name.ref ?? Output.panic("Internal error: expected label to have a name", {}));
                    p.print(":");
                    p.printBody(s.stmt, tlmtlo.subVar());
                },
                .s_try s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("try");
                    p.printSpace();
                    const sub_var_try = tlmtlo.subVar();
                    p.printBlock(s.body_loc, s.body, null, sub_var_try);

                    if (catch_ := s.catch_) {
                        p.printSpace();
                        p.addSourceMapping(catch_.loc);
                        p.print("catch");
                        if (binding := catch_.binding) {
                            p.printSpace();
                            p.print("(");
                            p.printBinding(binding, {});
                            p.print(")");
                        }
                        p.printSpace();
                        p.printBlock(catch_.body_loc, catch_.body, null, sub_var_try);
                    }

                    if (finally := s.finally) {
                        p.printSpace();
                        p.print("finally");
                        p.printSpace();
                        p.printBlock(finally.loc, finally.stmts, null, sub_var_try);
                    }

                    p.printNewline();
                },
                .s_for s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("for");
                    p.printSpace();
                    p.print("(");

                    if (init_ := s.init) {
                        p.printForLoopInit(init_);
                    }

                    p.print(";");

                    if (test_ := s.test_) {
                        p.printExpr(test_, .lowest, ExprFlag.Set.initEmpty());
                    }

                    p.print(";");
                    p.printSpace();

                    if (update := s.update) {
                        p.printExpr(update, .lowest, ExprFlag.Set.initEmpty());
                    }

                    p.print(")");
                    p.printBody(s.body, tlmtlo.subVar());
                },
                .s_switch s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("switch");
                    p.printSpace();
                    p.print("(");

                    p.printExpr(s.test_, .lowest, ExprFlag.Set.initEmpty());

                    p.print(")");
                    p.printSpace();
                    p.print("{");
                    p.printNewline();
                    p.indent();

                    for (c of s.cases) {
                        p.printSemicolonIfNeeded();
                        p.printIndent();

                        if (val := c.value) {
                            p.print("case");
                            p.printSpace();
                            p.printExpr(val, .logical_and, ExprFlag.Set.initEmpty());
                        } else {
                            p.print("default");
                        }

                        p.print(":");

                        const sub_var_case = tlmtlo.subVar();
                        if (c.body.len == 1) {
                            switch (c.body[0].data) {
                                .s_block => {
                                    p.printSpace();
                                    p.printBlock(c.body[0].loc, c.body[0].data.s_block.stmts, c.body[0].data.s_block.close_brace_loc, sub_var_case);
                                    p.printNewline();
                                    continue;
                                },
                                else => {},
                            }
                        }

                        p.printNewline();
                        p.indent();
                        for (st of c.body) {
                            p.printSemicolonIfNeeded();
                            p.printStmt(st, sub_var_case) catch unreachable;
                        }
                        p.unindent();
                    }

                    p.unindent();
                    p.printIndent();
                    p.print("}");
                    p.printNewline();
                    p.needs_semicolon = false;
                },
                .s_import s => {
                    bun.assert(s.import_record_index < p.import_records.len);
                    bun.debugAssert(p.options.module_type != .internal_bake_dev);

                    const record: *const ImportRecord = p.importRecord(s.import_record_index);
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);

                    if (comptime is_bun_platform) {
                        switch (record.tag) {
                            .bun => {
                                p.printGlobalBunImportStatement(s.*);
                                return;
                            },
                            else => {},
                        }
                    }

                    if (record.path.is_disabled) {
                        if (record.flags.contains_import_star) {
                            p.print("var ");
                            p.printSymbol(s.namespace_ref);
                            p.@"print = "();
                            p.printDisabledImport();
                            p.printSemicolonAfterStatement();
                        }

                        if (s.items.len > 0 || s.default_name != null) {
                            p.printIndent();
                            p.printSpaceBeforeIdentifier();
                            p.printWhitespacer(ws("var {"));

                            if (default_name := s.default_name) {
                                p.printSpace();
                                p.print("default:");
                                p.printSpace();
                                p.printSymbol(default_name.ref.?);

                                if (s.items.len > 0) {
                                    p.printSpace();
                                    p.print(",");
                                    p.printSpace();
                                    let i: usize = 0;
                                    for (item of s.items) {
                                        defer i += 1;
                                        p.printClauseItemAs(item, .@"var");

                                        if (i < s.items.len - 1) {
                                            p.print(",");
                                            p.printSpace();
                                        }
                                    }
                                }
                            } else {
                                let i: usize = 0;
                                for (item of s.items) {
                                    defer i += 1;
                                    p.printClauseItemAs(item, .@"var");

                                    if (i < s.items.len - 1) {
                                        p.print(",");
                                        p.printSpace();
                                    }
                                }
                            }

                            p.print("}");
                            p.@"print = "();

                            if (record.flags.contains_import_star) {
                                p.printSymbol(s.namespace_ref);
                                p.printSemicolonAfterStatement();
                            } else {
                                p.printDisabledImport();
                                p.printSemicolonAfterStatement();
                            }
                        }

                        return;
                    }

                    if (record.flags.handles_import_errors && record.path.is_disabled && record.kind.isCommonJS()) {
                        return;
                    }

                    p.print("import");

                    let item_count: usize = 0;

                    if (name := s.default_name) {
                        p.print(" ");
                        p.printSymbol(name.ref.?);
                        item_count += 1;
                    }

                    if (s.items.len > 0) {
                        if (item_count > 0) {
                            p.print(",");
                        }
                        p.printSpace();

                        p.print("{");
                        if (!s.is_single_line) {
                            p.indent();
                        } else {
                            p.printSpace();
                        }

                        let i: usize = 0;
                        for (item of s.items) {
                            defer i += 1;
                            if (i != 0) {
                                p.print(",");
                                if (s.is_single_line) {
                                    p.printSpace();
                                }
                            }

                            if (!s.is_single_line) {
                                p.printNewline();
                                p.printIndent();
                            }

                            p.printClauseItem(item);
                        }

                        if (!s.is_single_line) {
                            p.unindent();
                            p.printNewline();
                            p.printIndent();
                        } else {
                            p.printSpace();
                        }
                        p.print("}");
                        item_count += 1;
                    }

                    if (record.flags.contains_import_star) {
                        if (item_count > 0) {
                            p.print(",");
                        }
                        p.printSpace();

                        p.printWhitespacer(ws("* as"));
                        p.print(" ");
                        p.printSymbol(s.namespace_ref);
                        item_count += 1;
                    }

                    if (item_count > 0) {
                        if (!p.options.minify_whitespace ||
                            record.flags.contains_import_star ||
                            s.items.len == 0)
                            p.print(" ");

                        p.printWhitespacer(ws("from "));
                    }

                    p.printImportRecordPath(record);

                    // backwards compatibility: previously, we always stripped type
                    if (comptime is_bun_platform) if (loader := record.loader) switch (loader) {
                        .jsx => p.printWhitespacer(ws(" with { type: \"jsx\" }")),
                        .js => p.printWhitespacer(ws(" with { type: \"js\" }")),
                        .ts => p.printWhitespacer(ws(" with { type: \"ts\" }")),
                        .tsx => p.printWhitespacer(ws(" with { type: \"tsx\" }")),
                        .css => p.printWhitespacer(ws(" with { type: \"css\" }")),
                        .file => p.printWhitespacer(ws(" with { type: \"file\" }")),
                        .json => p.printWhitespacer(ws(" with { type: \"json\" }")),
                        .jsonc => p.printWhitespacer(ws(" with { type: \"jsonc\" }")),
                        .toml => p.printWhitespacer(ws(" with { type: \"toml\" }")),
                        .yaml => p.printWhitespacer(ws(" with { type: \"yaml\" }")),
                        .json5 => p.printWhitespacer(ws(" with { type: \"json5\" }")),
                        .wasm => p.printWhitespacer(ws(" with { type: \"wasm\" }")),
                        .napi => p.printWhitespacer(ws(" with { type: \"napi\" }")),
                        .base64 => p.printWhitespacer(ws(" with { type: \"base64\" }")),
                        .dataurl => p.printWhitespacer(ws(" with { type: \"dataurl\" }")),
                        .text => p.printWhitespacer(ws(" with { type: \"text\" }")),
                        .bunsh => p.printWhitespacer(ws(" with { type: \"sh\" }")),
                        .sqlite, .sqlite_embedded => p.printWhitespacer(ws(" with { type: \"sqlite\" }")),
                        .html => p.printWhitespacer(ws(" with { type: \"html\" }")),
                        .md => p.printWhitespacer(ws(" with { type: \"md\" }")),
                    };
                    p.printSemicolonAfterStatement();

                    if (may_have_module_info) {
                        if (mi := p.moduleInfo()) {
                            const import_record_path = record.path.text;
                            const irp_id = bun.handleOom(mi.str(import_record_path));
                            let fetch_parameters: analyze_transpiled_module.ModuleInfo.FetchParameters = .none;
                            if (comptime is_bun_platform) if (loader := record.loader) { fetch_parameters = switch (loader) {
                                .json => .json,
                                .jsx => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("jsx"))),
                                .js => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("js"))),
                                .ts => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("ts"))),
                                .tsx => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("tsx"))),
                                .css => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("css"))),
                                .file => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("file"))),
                                .jsonc => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("jsonc"))),
                                .toml => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("toml"))),
                                .yaml => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("yaml"))),
                                .wasm => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("wasm"))),
                                .napi => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("napi"))),
                                .base64 => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("base64"))),
                                .dataurl => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("dataurl"))),
                                .text => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("text"))),
                                .bunsh => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("sh"))),
                                .sqlite, .sqlite_embedded => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("sqlite"))),
                                .html => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("html"))),
                                .json5 => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("json5"))),
                                .md => analyze_transpiled_module.ModuleInfo.FetchParameters.hostDefined(bun.handleOom(mi.str("md"))),
                            }; }
                            bun.handleOom(mi.requestModule(irp_id, fetch_parameters));

                            if (name := s.default_name) {
                                const local_name = p.renamer.nameForSymbol(name.ref.?);
                                const local_name_id = bun.handleOom(mi.str(local_name));
                                bun.handleOom(mi.addVar(local_name_id, .lexical));
                                bun.handleOom(mi.addImportInfoSingle(irp_id, bun.handleOom(mi.str("default")), local_name_id, false));
                            }

                            for (item of s.items) {
                                const local_name = p.renamer.nameForSymbol(item.name.ref.?);
                                const local_name_id = bun.handleOom(mi.str(local_name));
                                bun.handleOom(mi.addVar(local_name_id, .lexical));
                                // In bundled output, all surviving imports are value imports
                                // (tree-shaking already removed type-only ones). The finalize()
                                // step handles re-export type-script conversion separately.
                                bun.handleOom(mi.addImportInfoSingle(irp_id, bun.handleOom(mi.str(item.alias)), local_name_id, false));
                            }

                            if (record.flags.contains_import_star) {
                                const local_name = p.renamer.nameForSymbol(s.namespace_ref);
                                bun.handleOom(mi.addVar(bun.handleOom(mi.str(local_name)), .lexical));
                                bun.handleOom(mi.addImportInfoNamespace(irp_id, bun.handleOom(mi.str(local_name))));
                            }
                        }
                    }
                },
                .s_block s => {
                    p.printIndent();
                    p.printBlock(stmt.loc, s.stmts, s.close_brace_loc, tlmtlo.subVar());
                    p.printNewline();
                },
                .s_debugger => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("debugger");
                    p.printSemicolonAfterStatement();
                },
                .s_directive s => {
                    if (comptime is_json)
                        unreachable;

                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.printStringLiteralUTF8(s.value, false);
                    p.printSemicolonAfterStatement();
                },
                .s_break s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("break");
                    if (label := s.label) {
                        p.print(" ");
                        p.printSymbol(label.ref.?);
                    }

                    p.printSemicolonAfterStatement();
                },
                .s_continue s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("continue");

                    if (label := s.label) {
                        p.print(" ");
                        p.printSymbol(label.ref.?);
                    }
                    p.printSemicolonAfterStatement();
                },
                .s_return s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("return");

                    if (value := s.value) {
                        p.printSpace();
                        p.printExpr(value, .lowest, ExprFlag.Set.initEmpty());
                    }
                    p.printSemicolonAfterStatement();
                },
                .s_throw s => {
                    p.printIndent();
                    p.printSpaceBeforeIdentifier();
                    p.addSourceMapping(stmt.loc);
                    p.print("throw");
                    p.printSpace();
                    p.printExpr(s.value, .lowest, ExprFlag.Set.initEmpty());
                    p.printSemicolonAfterStatement();
                },
                .s_expr s => {
                    if (!p.options.minify_whitespace && p.options.indent.count > 0) {
                        p.printIndent();
                    }

                    p.stmt_start = p.writer.written;
                    const result_unused = ExprFlag.ExprResultIsUnused;
                    p.printExpr(s.value, .lowest, result_unused());
                    p.printSemicolonAfterStatement();
                },
                else => {
                    Output.panic("Unexpected tag in printStmt: .{s}", {@tagName(std.meta.activeTag(stmt.data))});
                },
            }
        }

        static fn printModuleExportSymbol(p: *Printer) {
            p.print("module.exports");
        }

        static fn printImportRecordPath(p: *Printer, import_record: *const ImportRecord) {
            if (comptime is_json)
                unreachable;

            const quote = bestQuoteCharForString(u8, import_record.path.text, false);
            if (import_record.flags.print_namespace_in_path && !import_record.path.isFile()) {
                p.print(quote);
                p.printStringCharactersUTF8(import_record.path.namespace, quote);
                p.print(":");
                p.printStringCharactersUTF8(import_record.path.text, quote);
                p.print(quote);
            } else {
                p.print(quote);
                p.printStringCharactersUTF8(import_record.path.text, quote);
                p.print(quote);
            }
        }

        static fn printBundledImport(p: *Printer, record: ImportRecord, s: *S.Import) {
            if (record.flags.is_internal) {
                return;
            }

            const import_record = p.importRecord(s.import_record_index);
            const is_disabled = import_record.path.is_disabled;
            const module_id = import_record.module_id;

            // If the bundled import was disabled and only imported for side effects
            // we can skip it

            if (record.path.is_disabled) {
                if (p.symbols().get(s.namespace_ref) == null)
                    return;
            }

            switch (ImportVariant.determine(&record, s)) {
                .path_only => {
                    if (!is_disabled) {
                        p.printCallModuleID(module_id);
                        p.printSemicolonAfterStatement();
                    }
                },
                .import_items_and_default, .import_default => {
                    if (!is_disabled) {
                        p.print("var $");
                        p.printModuleId(module_id);
                        p.@"print = "();
                        p.printLoadFromBundle(s.import_record_index);

                        if (default_name := s.default_name) {
                            p.print(", ");
                            p.printSymbol(default_name.ref.?);
                            p.print(" = (($");
                            p.printModuleId(module_id);

                            p.print(" && \"default\" in $");
                            p.printModuleId(module_id);
                            p.print(") ? $");
                            p.printModuleId(module_id);
                            p.print(".default : $");
                            p.printModuleId(module_id);
                            p.print(")");
                        }
                    } else {
                        if (default_name := s.default_name) {
                            p.print("var ");
                            p.printSymbol(default_name.ref.?);
                            p.@"print = "();
                            p.printDisabledImport();
                        }
                    }

                    p.printSemicolonAfterStatement();
                },
                .import_star_and_import_default => {
                    p.print("var ");
                    p.printSymbol(s.namespace_ref);
                    p.@"print = "();
                    p.printLoadFromBundle(s.import_record_index);

                    if (default_name := s.default_name) {
                        p.print(",");
                        p.printSpace();
                        p.printSymbol(default_name.ref.?);
                        p.@"print = "();

                        if (!is_bun_platform) {
                            p.print("(");
                            p.printSymbol(s.namespace_ref);
                            p.printWhitespacer(ws(" && \"default\" in "));
                            p.printSymbol(s.namespace_ref);
                            p.printWhitespacer(ws(" ? "));
                            p.printSymbol(s.namespace_ref);
                            p.printWhitespacer(ws(".default : "));
                            p.printSymbol(s.namespace_ref);
                            p.print(")");
                        } else {
                            p.printSymbol(s.namespace_ref);
                        }
                    }
                    p.printSemicolonAfterStatement();
                },
                .import_star => {
                    p.print("var ");
                    p.printSymbol(s.namespace_ref);
                    p.@"print = "();
                    p.printLoadFromBundle(s.import_record_index);
                    p.printSemicolonAfterStatement();
                },

                else => {
                    p.print("var $");
                    p.printModuleIdAssumeEnabled(module_id);
                    p.@"print = "();
                    p.printLoadFromBundle(s.import_record_index);
                    p.printSemicolonAfterStatement();
                },
            }
        }
        static fn printLoadFromBundle(p: *Printer, import_record_index: u32) {
            p.printLoadFromBundleWithoutCall(import_record_index);
            p.print("()");
        }

        static fn printDisabledImport(p: *Printer) {
            p.printWhitespacer(ws("(() => ({}))"));
        }

        static fn printLoadFromBundleWithoutCall(p: *Printer, import_record_index: u32) {
            const record = p.importRecord(import_record_index);
            if (record.path.is_disabled) {
                p.printDisabledImport();
                return;
            }

            @call(bun.callmod_inline, printModuleId, .{ p, p.importRecord(import_record_index).module_id });
        }

        static fn printCallModuleID(p: *Printer, module_id: u32) {
            printModuleId(p, module_id);
            p.print("()");
        }

        static fn printModuleId(p: *Printer, module_id: u32) {
            bun.assert(module_id != 0); // either module_id is forgotten or it should be disabled
            p.printModuleIdAssumeEnabled(module_id);
        }

        static fn printModuleIdAssumeEnabled(p: *Printer, module_id: u32) {
            p.print("$");
            std.fmt.formatInt(module_id, 16, .lower, {}, p) catch unreachable;
        }

        static fn printBundledRexport(p: *Printer, name: string, import_record_index: u32) {
            p.print("Object.defineProperty(");
            p.printModuleExportSymbol();
            p.print(",");
            p.printStringLiteralUTF8(name, true);

            p.printWhitespacer(ws(",{get: () => ("));
            p.printLoadFromBundle(import_record_index);
            p.printWhitespacer(ws("), enumerable: true, configurable: true})"));
        }

        // We must use Object.defineProperty() to handle re-exports from ESM -> CJS
        // Here is an example where a runtime error occurs when assigning directly to module.exports
        // > 24077 |   module.exports.init = init;
        // >       ^
        // >  TypeError: Attempted to assign to readonly property.
        static fn printBundledExport(p: *Printer, name: string, identifier: string) {
            // In the event that
            p.print("Object.defineProperty(");
            p.printModuleExportSymbol();
            p.print(",");
            p.printStringLiteralUTF8(name, true);
            p.print(",{get: () => ");
            p.printIdentifier(identifier);
            p.print(", enumerable: true, configurable: true})");
        }

        static fn printForLoopInit(p: *Printer, initSt: Stmt) {
            switch (initSt.data) {
                .s_expr s => {
                    p.printExpr(
                        s.value,
                        .lowest,
                        ExprFlag.Set.init({ forbid_in: true, expr_result_is_unused: true }),
                    );
                },
                .s_local s => {
                    switch (s.kind) {
                        .k_var => {
                            p.printDecls("var", s.decls.slice(), ExprFlag.Set.init({ forbid_in: true }), {});
                        },
                        .k_let => {
                            p.printDecls("let", s.decls.slice(), ExprFlag.Set.init({ forbid_in: true }), {});
                        },
                        .k_const => {
                            p.printDecls("const", s.decls.slice(), ExprFlag.Set.init({ forbid_in: true }), {});
                        },
                        .k_using => {
                            p.printDecls("using", s.decls.slice(), ExprFlag.Set.init({ forbid_in: true }), {});
                        },
                        .k_await_using => {
                            p.printDecls("await using", s.decls.slice(), ExprFlag.Set.init({ forbid_in: true }), {});
                        },
                    }
                },
                // for(;)
                .s_empty => {},
                else => {
                    Output.panic("Internal error: Unexpected stmt in for loop", {});
                },
            }
        }
        static fn printIf(p: *Printer, s: *const S.If, loc: logger.Loc, tlmtlo: TopLevel) {
            p.printSpaceBeforeIdentifier();
            p.addSourceMapping(loc);
            p.print("if");
            p.printSpace();
            p.print("(");
            p.printExpr(s.test_, .lowest, ExprFlag.Set.initEmpty());
            p.print(")");

            switch (s.yes.data) {
                .s_block block => {
                    p.printSpace();
                    p.printBlock(s.yes.loc, block.stmts, block.close_brace_loc, tlmtlo);

                    if (s.no != null) {
                        p.printSpace();
                    } else {
                        p.printNewline();
                    }
                },
                else => {
                    if (wrapToAvoidAmbiguousElse(&s.yes.data)) {
                        p.printSpace();
                        p.print("{");
                        p.printNewline();

                        p.indent();
                        p.printStmt(s.yes, tlmtlo) catch unreachable;
                        p.unindent();
                        p.needs_semicolon = false;

                        p.printIndent();
                        p.print("}");

                        if (s.no != null) {
                            p.printSpace();
                        } else {
                            p.printNewline();
                        }
                    } else {
                        p.printNewline();
                        p.indent();
                        p.printStmt(s.yes, tlmtlo) catch unreachable;
                        p.unindent();

                        if (s.no != null) {
                            p.printIndent();
                        }
                    }
                },
            }

            if (no_block := s.no) {
                p.printSemicolonIfNeeded();
                p.printSpaceBeforeIdentifier();
                p.addSourceMapping(no_block.loc);
                p.print("else");

                switch (no_block.data) {
                    .s_block => {
                        p.printSpace();
                        p.printBlock(no_block.loc, no_block.data.s_block.stmts, null, tlmtlo);
                        p.printNewline();
                    },
                    .s_if => {
                        p.printIf(no_block.data.s_if, no_block.loc, tlmtlo);
                    },
                    else => {
                        p.printNewline();
                        p.indent();
                        p.printStmt(no_block, tlmtlo) catch unreachable;
                        p.unindent();
                    },
                }
            }
        }

        static fn wrapToAvoidAmbiguousElse(s_: *const Stmt.Data): bool {
            let s = s_;
            while (true) {
                switch (s.*) {
                    .s_if index => {
                        if (*no := index.no) {
                            s = &no.data;
                        } else {
                            return true;
                        }
                    },
                    .s_for current => {
                        s = &current.body.data;
                    },
                    .s_for_in current => {
                        s = &current.body.data;
                    },
                    .s_for_of current => {
                        s = &current.body.data;
                    },
                    .s_while current => {
                        s = &current.body.data;
                    },
                    .s_with current => {
                        s = &current.body.data;
                    },
                    .s_label current => {
                        s = &current.stmt.data;
                    },
                    else => {
                        return false;
                    },
                }
            }
        }

        static fn tryToGetImportedEnumValue(p: *Printer, target: Expr, name: []const u8): js_ast.InlinedEnumValue.Decoded? {
            if (id := target.data.@"as"(.e_import_identifier)) {
                const sym_ref = p.symbols().follow(id.ref);
                if (symbol := p.symbols().get(sym_ref)) {
                    if (symbol.kind == .ts_enum) {
                        if (enum_value := p.options.ts_enums.get(sym_ref)) {
                            if (value := enum_value.get(name))
                                return value.decode();
                        }
                    }
                }
            }
            return null;
        }

        static fn printInlinedEnum(
            p: *Printer,
            inlined: js_ast.InlinedEnumValue.Decoded,
            comment: []const u8,
            level: Level,
        ) {
            switch (inlined) {
                .number num => p.printNumber(num, level),

                // TODO: extract printString
                .string str => p.printExpr({
                    data: { e_string: str },
                    loc: logger.Loc.Empty,
                }, level, {}),
            }

            if (!p.options.minify_whitespace && !p.options.minify_identifiers) {
                // TODO: rewrite this to handle </script>
                if (!bun.strings.containsComptime(comment, "*/")) {
                    p.print(" /* ");
                    p.print(comment);
                    p.print(" */");
                }
            }
        }

        static fn printDeclStmt(p: *Printer, is_export: bool, comptime keyword: string, decls: []G.Decl, tlmtlo: TopLevel) {
            if (!rewrite_esm_to_cjs && is_export) {
                p.print("export ");
            }
            const tlm: TopLevelAndIsExport = if (may_have_module_info) TopLevelAndIsExport{
                is_export: is_export,
                is_top_level: if (comptime strings.eqlComptime(keyword, "var"))
                    (if (tlmtlo.isTopLevel()) .declared else null)
                else
                    // let/const are block-scoped: only record at true top-level,
                    // not inside blocks where subVar() downgrades to .var_only.
                    (if (tlmtlo.is_top_level == .yes) .lexical else null),
            } else TopLevelAndIsExportEmpty{};
            p.printDecls(keyword, decls, ExprFlag.Set.initEmpty(), tlm);
            p.printSemicolonAfterStatement();
            if (rewrite_esm_to_cjs && is_export && decls.len > 0) {
                for (decl of decls) {
                    p.printIndent();
                    p.printSymbol(p.options.runtime_imports.__export.?.ref);
                    p.print("(");
                    p.printSpaceBeforeIdentifier();
                    p.printModuleExportSymbol();
                    p.print(",");
                    p.printSpace();

                    switch (decl.binding.data) {
                        .b_identifier ident => {
                            p.print("{");
                            p.printSpace();
                            p.printSymbol(ident.ref);
                            if (p.options.minify_whitespace)
                                p.print(":()=>(")
                            else
                                p.print(": () => (");
                            p.printSymbol(ident.ref);
                            p.print(") }");
                        },
                        .b_object obj => {
                            p.print("{");
                            p.printSpace();
                            for (prop of obj.properties) {
                                switch (prop.value.data) {
                                    .b_identifier ident => {
                                        p.printSymbol(ident.ref);
                                        if (p.options.minify_whitespace)
                                            p.print(":()=>(")
                                        else
                                            p.print(": () => (");
                                        p.printSymbol(ident.ref);
                                        p.print("),");
                                        p.printNewline();
                                    },
                                    else => {},
                                }
                            }
                            p.print("}");
                        },
                        else => {
                            p.printBinding(decl.binding, {});
                        },
                    }
                    p.print(")");
                    p.printSemicolonAfterStatement();
                }
            }
        }

        static fn printIdentifier(p: *Printer, identifier: string) {
            if (comptime ascii_only) {
                p.printIdentifierAsciiOnly(identifier);
            } else {
                p.print(identifier);
            }
        }

        static fn printIdentifierAsciiOnly(p: *Printer, identifier: string) {
            let ascii_start: usize = 0;
            let is_ascii = false;
            let iter = CodepointIterator.init(identifier);
            let cursor: CodepointIterator.Cursor = .{};
            while (iter.next(&cursor)) {
                switch (cursor.c) {
                    first_ascii..=last_ascii => {
                        if (!is_ascii) {
                            ascii_start = cursor.i;
                            is_ascii = true;
                        }
                    },
                    else => {
                        if (is_ascii) {
                            p.print(identifier[ascii_start..cursor.i]);
                            is_ascii = false;
                        }

                        p.print("\\u{");
                        p.fmt("{x}", .{cursor.c}) catch unreachable;
                        p.print("}");
                    },
                }
            }

            if (is_ascii) {
                p.print(identifier[ascii_start..]);
            }
        }

        static fn printIdentifierUTF16(p: *Printer, name: []const u16)! {
            const n = name.len;
            let i: usize = 0;

            while (i < n) {
                let c: u32 = @as(u32, name[i]);
                i += 1;

                if ((c & ~@as(u32, 0x03ff)) == 0xd800 && i < n) {
                    c = 0x10000 + (((c & 0x03ff) << 10) | (name[i] & 0x03ff));
                    i += 1;
                }

                if ((comptime ascii_only) && c > last_ascii) {
                    switch (c) {
                        0..=0xFFFF => {
                            const ubuf: [6]u8 = [
                                '\\',
                                'u',
                                hex_chars[c >> 12],
                                hex_chars[(c >> 8) & 15],
                                hex_chars[(c >> 4) & 15],
                                hex_chars[c & 15],
                            ];
                            p.print(ubuf);
                        },
                        else => {
                            p.print("\\u");
                            let buf_ptr = p.writer.reserve(4) catch unreachable;
                            p.writer.advance(strings.encodeWTF8RuneT(buf_ptr[0..4], u32, c));
                        },
                    }
                    continue;
                }

                {
                    let buf_ptr = p.writer.reserve(4) catch unreachable;
                    p.writer.advance(strings.encodeWTF8RuneT(buf_ptr[0..4], u32, c));
                }
            }
        }

        static fn printNumber(p: *Printer, value: f64, level: Level) {
            const absValue = @abs(value);
            if (std.math.isNan(value)) {
                p.printSpaceBeforeIdentifier();
                p.print("NaN");
            } else if (std.math.isPositiveInf(value) || std.math.isNegativeInf(value)) {
                const wrap = ((!p.options.has_run_symbol_renamer || p.options.minify_syntax) && level.gte(.multiply)) ||
                    (std.math.isNegativeInf(value) && level.gte(.prefix));

                if (wrap) {
                    p.print("(");
                }

                if (std.math.isNegativeInf(value)) {
                    p.printSpaceBeforeOperator(.un_neg);
                    p.print("-");
                } else {
                    p.printSpaceBeforeIdentifier();
                }

                // If we are not running the symbol renamer, we must not print "Infinity".
                // Some code may assign `Infinity` to another idenitifier.
                //
                // We do not want:
                //
                //   const Infinity = 1 / 0
                //
                // to be transformed into:
                //
                //   const Infinity = Infinity
                //
                if (is_json || (!p.options.minify_syntax && p.options.has_run_symbol_renamer)) {
                    p.print("Infinity");
                } else if (p.options.minify_whitespace) {
                    p.print("1/0");
                } else {
                    p.print("1 / 0");
                }

                if (wrap) {
                    p.print(")");
                }
            } else if (!std.math.signbit(value)) {
                p.printSpaceBeforeIdentifier();

                p.printNonNegativeFloat(absValue);

                // Remember the end of the latest number
                p.prev_num_end = p.writer.written;
            } else if (level.gte(.prefix)) {
                // Expressions such as "(-1).toString" need to wrap negative numbers.
                // Instead of testing for "value < 0" we test for "signbit(value)" and
                // "!isNaN(value)" because we need this to be true for "-0" and "-0 < 0"
                // is false.
                p.print("(-");
                p.printNonNegativeFloat(absValue);
                p.print(")");
            } else {
                p.printSpaceBeforeOperator(Op.Code.un_neg);
                p.print("-");
                p.printNonNegativeFloat(absValue);

                // Remember the end of the latest number
                p.prev_num_end = p.writer.written;
            }
        }

        static fn printIndentedComment(p: *Printer, _text: string) {
            let text = _text;
            if (strings.startsWith(text, "/*")) {
                // Re-indent multi-line comments
                while (newline_index := strings.indexOfChar(text, '\n')) {

                    // Skip over \r if it precedes \n
                    if (newline_index > 0 && text[newline_index - 1] == '\r') {
                        p.print(text[0 .. newline_index - 1]);
                        p.print("\n");
                    } else {
                        p.print(text[0 .. newline_index + 1]);
                    }
                    p.printIndent();

                    text = text[newline_index + 1 ..];
                }
                p.print(text);
                p.printNewline();
            } else {
                // Print a mandatory newline after single-line comments
                if (text.len > 0 && text[text.len - 1] == '\r') {
                    text = text[0 .. text.len - 1];
                }

                p.print(text);
                p.print("\n");
            }
        }

        static fn init(
            writer: Writer,
            import_records: []const ImportRecord,
            opts: Options,
            renamer: bun.renamer.Renamer,
            source_map_builder: SourceMap.Chunk.Builder,
        ): Printer {
            let printer = Printer{
                import_records: import_records,
                options: opts,
                writer: writer,
                renamer: renamer,
                source_map_builder: source_map_builder,
            };
            if (comptime generate_source_map) {
                // This seems silly to cache but the .items() function apparently costs 1ms according to Instruments.
                printer.source_map_builder.line_offset_table_byte_offset_list =
                    printer
                        .source_map_builder
                        .line_offset_tables
                        .items(.byte_offset_to_start_of_line);
            }

            return printer;
        }

        static fn printDevServerModule(
            p: *Printer,
            source: *const logger.Source,
            ast: *const Ast,
            part: *const js_ast.Part,
        ) {
            p.indent();
            p.printIndent();

            p.printStringLiteralUTF8(source.path.pretty, false);

            const func = part.stmts[0].data.s_expr.value.data.e_function.func;

            // Special-case lazy-export AST
            if (ast.has_lazy_export) {
                @branchHint(.unlikely);
                p.printFnArgs(func.open_parens_loc, func.args, func.flags.contains(.has_rest_arg), false);
                p.printSpace();
                p.print("{\n");
                if (func.body.stmts[0].data.s_lazy_export.* != .e_undefined) {
                    p.indent();
                    p.printIndent();
                    p.printSymbol(p.options.hmr_ref);
                    p.print(".cjs.exports = ");
                    p.printExpr({
                        data: func.body.stmts[0].data.s_lazy_export.*,
                        loc: func.body.stmts[0].loc,
                    }, .comma, {});
                    p.print("; // bun .s_lazy_export\n");
                    p.unindent();
                }
                p.printIndent();
                p.print("},\n");
                return;
            }

            // ESM is represented by an array tuple [ dependencies, exports, starImports, load, async ];
            else if (ast.exports_kind == .esm) {
                p.print(": [ [");
                // Print the dependencies.
                if (part.stmts.len > 1) {
                    p.indent();
                    p.print("\n");
                    for (stmt of part.stmts[1..]) {
                        p.printIndent();
                        const imp = stmt.data.s_import;
                        const record = p.importRecord(imp.import_record_index);
                        p.printStringLiteralUTF8(record.path.pretty, false);

                        const item_count = @as(u32, @intFromBool(imp.default_name != null)) +
                            @as(u32, @intCast(imp.items.len));
                        p.fmt(", {d},", .{item_count}) catch {};
                        if (item_count == 0) {
                            // Add a comment explaining why the number could be zero
                            p.print(if (imp.star_name_loc != null) " // namespace import" else " // bare import");
                        } else {
                            if (imp.default_name != null) {
                                p.print(" \"default\",");
                            }
                            for (item of imp.items) {
                                p.print(" ");
                                p.printStringLiteralUTF8(item.alias, false);
                                p.print(",");
                            }
                        }
                        p.print("\n");
                    }
                    p.unindent();
                    p.printIndent();
                }
                p.print("], [");

                // Print the exports
                if (ast.named_exports.count() > 0) {
                    p.indent();
                    let len: usize = std.math.maxInt(usize);
                    for (key of ast.named_exports.keys()) {
                        if (len > 120) {
                            p.printNewline();
                            p.printIndent();
                            len = 0;
                        } else {
                            p.print(" ");
                        }
                        len += key.len;
                        p.printStringLiteralUTF8(key, false);
                        p.print(",");
                    }
                    p.unindent();
                    p.printNewline();
                    p.printIndent();
                }
                p.print("], [");

                // Print export stars
                p.indent();
                let had_any_stars = false;
                for (star of ast.export_star_import_records) {
                    const record = p.importRecord(star);
                    if (record.path.is_disabled) continue;
                    had_any_stars = true;
                    p.printNewline();
                    p.printIndent();
                    p.printStringLiteralUTF8(record.path.pretty, false);
                    p.print(",");
                }
                p.unindent();
                if (had_any_stars) {
                    p.printNewline();
                    p.printIndent();
                }
                p.print("], ");

                // Print the code
                if (!ast.top_level_await_keyword.isEmpty()) p.print("async");
                p.printFnArgs(func.open_parens_loc, func.args, func.flags.contains(.has_rest_arg), false);
                p.print(" => {\n");
                p.indent();
                p.printBlockBody(func.body.stmts, TopLevel.init(.no));
                p.unindent();
                p.printIndent();
                p.print("}, ");

                // Print isAsync
                p.print(if (!ast.top_level_await_keyword.isEmpty()) "true" else "false");
                p.print("],\n");
            } else {
                bun.assert(ast.exports_kind == .cjs);
                p.printFunc(func);
                p.print(",\n");
            }

            p.unindent();
        }
    };
}

pub struct WriteResult {
    off: u32,
    len: usize,
    end_off: u32,
}

err EmptyError {}

pub fn NewWriter(
    comptime ContextType: type,
    comptime writeByte: fn (ctx: *ContextType, char: u8) anyerror!usize,
    comptime writeAllFn: fn (ctx: *ContextType, buf: auto) anyerror!usize,
    comptime getLastByte: fn (ctx: *const ContextType) u8,
    comptime getLastLastByte: fn (ctx: *const ContextType) u8,
    comptime reserveNext: fn (ctx: *ContextType, count: u64) anyerror![*]u8,
    comptime advanceBy: fn (ctx: *ContextType, count: u64) void,
): type {
    return struct {
        ctx: ContextType,
        written: i32 = -1,
        // Used by the printer
        prev_char: u8 = 0,
        prev_prev_char: u8 = 0,
        @"err": anyerror? = null,
        orig_err: anyerror? = null,

        const Self = @This();

        static fn init(ctx: ContextType): Self {
            return {
                ctx: ctx,
            };
        }

        static fn stdWriter(writer: *Self): std.Io.GenericWriter(*Self, EmptyError, stdWriterWrite) {
            return { context: writer };
        }
        static fn stdWriterWrite(writer: *Self, bytes: []const u8): usize ! EmptyError {
            writer.print(@TypeOf(bytes), bytes);
            return bytes.len;
        }

        static fn isCopyFileRangeSupported(): bool {
            return comptime std.meta.hasFn(ContextType, "copyFileRange");
        }

        static fn copyFileRange(ctx: ContextType, in_file: FD, start: usize, end: usize)! {
            ctx.sendfile(
                in_file,
                start,
                end,
            );
        }

        static fn getMutableBuffer(writer: *Self): *MutableString {
            return writer.ctx.getMutableBuffer();
        }

        static fn takeBuffer(writer: *Self): MutableString {
            return writer.ctx.takeBuffer();
        }

        static fn slice(writer: *Self): string {
            return writer.ctx.slice();
        }

        static fn getError(writer: *const Self)! {
            if (orig_err := writer.orig_err) {
                return orig_err;
            }

            if (e := writer.@"err") {
                return e;
            }
        }

        static fn prevChar(writer: *const Self): u8 {
            return @call(bun.callmod_inline, getLastByte, .{&writer.ctx});
        }

        static fn prevPrevChar(writer: *const Self): u8 {
            return @call(bun.callmod_inline, getLastLastByte, .{&writer.ctx});
        }

        static fn reserve(writer: *Self, count: u64): ([*]u8)! {
            return reserveNext(&writer.ctx, count)?;
        }

        static fn advance(writer: *Self, count: u64) {
            advanceBy(&writer.ctx, count);
            writer.written += @as(i32, @intCast(count));
        }

        pub const Error = error{FormatError};

        static fn writeAll(writer: *Self, bytes: auto): usize ! Error {
            const written = @max(writer.written, 0);
            writer.print(@TypeOf(bytes), bytes);
            return @as(usize, @intCast(writer.written)) - @as(usize, @intCast(written));
        }

        static fn print(writer: *Self, comptime ValueType: type, str: ValueType) {
            switch (ValueType) {
                comptime_int, u16, u8 => {
                    const written = writeByte(&writer.ctx, @as(u8, @intCast(str))) catch e => brk: {
                        writer.orig_err = e;
                        break :brk 0;
                    };

                    writer.written += @as(i32, @intCast(written));
                    writer.@"err" = if (written == 0) error.WriteFailed else writer.@"err";
                },
                else => {
                    const written = writeAllFn(&writer.ctx, str) catch e => brk: {
                        writer.orig_err = e;
                        break :brk 0;
                    };

                    writer.written += @as(i32, @intCast(written));
                    if (written < str.len) {
                        writer.@"err" = if (written == 0) error.WriteFailed else error.PartialWrite;
                    }
                },
            }
        }

        static fn flush(writer: *Self)! {
            if (std.meta.hasFn(ContextType, "flush")) {
                writer.ctx.flush()?;
            }
        }
        static fn done(writer: *Self)! {
            if (std.meta.hasFn(ContextType, "done")) {
                writer.ctx.done()?;
            }
        }
    };
}

pub struct DirectWriter {
    handle: FileDescriptorType,

    static fn write(writer: *DirectWriter, buf: []const u8): usize! {
        return std.posix.write(writer.handle, buf)?;
    }

    static fn writeAll(writer: *DirectWriter, buf: []const u8)! {
        * = std.posix.write(writer.handle, buf)?;
    }

    pub const Error = std.posix.WriteError;
}

pub struct BufferWriter {
    buffer: MutableString = undefined,
    written: []u8 = [],
    sentinel: [:0]const u8 = "",
    append_null_byte: bool = false,
    append_newline: bool = false,
    approximate_newline_count: usize = 0,
    last_bytes: [2]u8 = [ 0, 0 ],

    fn getMutableBuffer(*_): *MutableString {
        return &_.buffer;
    }

    fn takeBuffer(*_): MutableString {
        defer _.buffer = .initEmpty(_.buffer.allocator);
        return _.buffer;
    }

    fn getWritten(*_): []u8 {
        return _.buffer.list.items;
    }

    static fn init(allocator: std.mem.Allocator): BufferWriter {
        return BufferWriter{
            buffer: MutableString.initEmpty(allocator),
        };
    }

    static fn print(ctx: *BufferWriter, comptime fmt: string, args: auto)! {
        ctx.buffer.list.writer(ctx.buffer.allocator).print(fmt, args)?;
    }

    static fn writeByteNTimes(ctx: *BufferWriter, byte: u8, n: usize)! {
        ctx.buffer.appendCharNTimes(byte, n)?;
    }
    pub const splatByteAll = writeByteNTimes;

    static fn writeByte(ctx: *BufferWriter, byte: u8): usize! {
        ctx.buffer.appendChar(byte)?;
        ctx.approximate_newline_count += @intFromBool(byte == '\n');
        ctx.last_bytes = .{ ctx.last_bytes[1], byte };
        return 1;
    }
    static fn writeAll(ctx: *BufferWriter, bytes: auto): usize! {
        ctx.buffer.append(bytes)?;
        ctx.approximate_newline_count += @intFromBool(bytes.len > 0 && bytes[bytes.len - 1] == '\n');

        if (bytes.len >= 2) {
            ctx.last_bytes = bytes[bytes.len - 2 ..][0..2].*;
        } else if (bytes.len >= 1) {
            ctx.last_bytes = .{ ctx.last_bytes[1], bytes[bytes.len - 1] };
        }

        return bytes.len;
    }

    fn slice(*_): string {
        return _.buffer.list.items;
    }

    static fn getLastByte(ctx: *const BufferWriter): u8 {
        return ctx.last_bytes[1];
    }

    static fn getLastLastByte(ctx: *const BufferWriter): u8 {
        return ctx.last_bytes[0];
    }

    static fn reserveNext(ctx: *BufferWriter, count: u64): anyerror![*]u8 {
        ctx.buffer.growIfNeeded(count)?;
        return unsafe { @as([*]u8, &ctx.buffer.list.items.ptr[ctx.buffer.list.items.len]) };
    }

    static fn advanceBy(ctx: *BufferWriter, count: u64) {
        if (comptime Environment.isDebug) bun.assert(ctx.buffer.list.items.len + count <= ctx.buffer.list.capacity);

        ctx.buffer.list.items = ctx.buffer.list.items.ptr[0 .. ctx.buffer.list.items.len + count];

        if (count >= 2) {
            ctx.last_bytes = ctx.buffer.list.items[ctx.buffer.list.items.len - 2 ..][0..2].*;
        } else if (count >= 1) {
            ctx.last_bytes = .{ ctx.last_bytes[1], ctx.buffer.list.items[ctx.buffer.list.items.len - 1] };
        }
    }

    static fn reset(ctx: *BufferWriter) {
        ctx.buffer.reset();
        ctx.approximate_newline_count = 0;
        ctx.written = [];
    }

    static fn writtenWithoutTrailingZero(ctx: *const BufferWriter): []u8 {
        let written = ctx.written;
        while (written.len > 0 && written[written.len - 1] == 0) {
            written = written[0 .. written.len - 1];
        }

        return written;
    }

    static fn done(
        ctx: *BufferWriter,
    )! {
        if (ctx.append_newline) {
            ctx.append_newline = false;
            ctx.buffer.appendChar('\n')?;
        }

        if (ctx.append_null_byte) {
            ctx.sentinel = ctx.buffer.sliceWithSentinel();
            ctx.written = ctx.buffer.slice();
        } else {
            ctx.written = ctx.buffer.slice();
        }
    }

    static fn flush(
        ctx: *BufferWriter,
    )! {
        * = ctx;
    }
}
pub const BufferPrinter = NewWriter(
    BufferWriter,
    BufferWriter.writeByte,
    BufferWriter.writeAll,
    BufferWriter.getLastByte,
    BufferWriter.getLastLastByte,
    BufferWriter.reserveNext,
    BufferWriter.advanceBy,
);

pub enum Format {
    esm,
    cjs,

    // bun.js must escape non-latin1 identifiers in the output This is because
    // we load JavaScript as a UTF-8 buffer instead of a UTF-16 buffer
    // JavaScriptCore does not support UTF-8 identifiers when the source code
    // string is loaded as const char* We don't want to double the size of code
    // in memory..
    esm_ascii,
    cjs_ascii,
}

enum GenerateSourceMap {
    disable,
    lazy,
    eager,
}
pub fn getSourceMapBuilder(
    comptime generate_source_map: GenerateSourceMap,
    comptime is_bun_platform: bool,
    opts: Options,
    source: *const logger.Source,
    tree: *const Ast,
): SourceMap.Chunk.Builder {
    if (comptime generate_source_map == .disable)
        return undefined;

    return {
        source_map: .init(
            opts.source_map_allocator ?? opts.allocator,
            is_bun_platform && generate_source_map == .lazy,
        ),
        cover_lines_without_mappings: true,
        approximate_input_line_count: tree.approximate_newline_count,
        prepend_count: is_bun_platform && generate_source_map == .lazy,
        line_offset_tables: opts.line_offset_tables ?? brk: {
            if (generate_source_map == .lazy) break :brk SourceMap.LineOffsetTable.generate(
                opts.source_map_allocator ?? opts.allocator,
                source.contents,
                @as(i32, @intCast(tree.approximate_newline_count)),
            );
            break :brk .empty;
        },
    };
}

pub fn printAst(
    comptime Writer: type,
    _writer: Writer,
    tree: Ast,
    symbols_: js_ast.Symbol.Map,
    source: *const logger.Source,
    comptime ascii_only: bool,
    opts: Options,
    comptime generate_source_map: bool,
): usize! {
    let symbols = symbols_;
    let renamer: rename.Renamer = undefined;
    let _renamer_guard = OptDeinitWithAllocGuard<rename.Renamer>{ ptr: null, alloc: opts.allocator };
    let no_op_renamer: rename.NoOpRenamer = undefined;
    let module_scope = tree.module_scope;
    if (opts.minify_identifiers) {
        const allocator = opts.allocator;
        let reserved_names = rename.computeInitialReservedNames(allocator, opts.module_type)?;
        let _reserved_guard = OptDeinitWithAllocGuard<bun.StringHashMapUnmanaged(u32)>{ ptr: unsafe { &reserved_names }, alloc: allocator };
        for (child of module_scope.children.slice()) {
            child.parent = &module_scope;
        }

        rename.computeReservedNamesForScope(&module_scope, &symbols, &reserved_names, allocator);
        let minify_renamer = rename.MinifyRenamer.init(allocator, symbols, tree.nested_scope_slot_counts, reserved_names)?;
        _reserved_guard.ptr = null;
        renamer = minify_renamer.toRenamer();
        _renamer_guard.ptr = unsafe { &renamer };

        let top_level_symbols = rename.StableSymbolCount.Array.init(allocator);
        const _top_level_symbols_guard = DeinitGuard<rename.StableSymbolCount.Array>{ ptr: unsafe { &top_level_symbols } };

        const uses_exports_ref = tree.uses_exports_ref;
        const uses_module_ref = tree.uses_module_ref;
        const exports_ref = tree.exports_ref;
        const module_ref = tree.module_ref;
        const parts = tree.parts;

        const dont_break_the_code: [3]Ref = [
            tree.module_ref,
            tree.exports_ref,
            tree.require_ref,
        ];

        inline for (r of dont_break_the_code) {
            if (symbol := symbols.get(r)) {
                symbol.must_not_be_renamed = true;
            }
        }

        for (named_export of tree.named_exports.values()) {
            if (symbol := symbols.get(named_export.ref)) {
                symbol.must_not_be_renamed = true;
            }
        }

        if (uses_exports_ref) {
            minify_renamer.accumulateSymbolUseCount(&top_level_symbols, exports_ref, 1, &.{source.index.value})?;
        }

        if (uses_module_ref) {
            minify_renamer.accumulateSymbolUseCount(&top_level_symbols, module_ref, 1, &.{source.index.value})?;
        }

        for (part of parts.slice()) {
            minify_renamer.accumulateSymbolUseCounts(&top_level_symbols, part.symbol_uses, &.{source.index.value})?;

            for (declared_ref of part.declared_symbols.refs()) {
                minify_renamer.accumulateSymbolUseCount(&top_level_symbols, declared_ref, 1, &.{source.index.value})?;
            }
        }

        std.sort.pdq(rename.StableSymbolCount, top_level_symbols.items, @as(void, undefined), rename.StableSymbolCount.lessThan);

        minify_renamer.allocateTopLevelSymbolSlots(top_level_symbols)?;
        let minifier = tree.char_freq.?.compile(allocator);
        minify_renamer.assignNamesByFrequency(&minifier)?;

    } else {
        no_op_renamer = rename.NoOpRenamer.init(symbols, source);
        renamer = no_op_renamer.toRenamer();
    }

    const PrinterType = NewPrinter(
        ascii_only,
        Writer,
        false,
        // if it's ascii_only, it is also bun
        ascii_only,
        false,
        generate_source_map,
    );
    const writer = _writer;

    let printer = PrinterType.init(
        writer,
        tree.import_records.slice(),
        opts,
        renamer,
        getSourceMapBuilder(if (generate_source_map) .lazy else .disable, ascii_only, opts, source, &tree),
    );
    const _lot_guard = OptDeinitWithAllocGuard<@TypeOf(printer.source_map_builder.line_offset_tables)>{ ptr: if (comptime generate_source_map) unsafe { &printer.source_map_builder.line_offset_tables } else null, alloc: opts.allocator };
    printer.was_lazy_export = tree.has_lazy_export;
    if (PrinterType.may_have_module_info) {
        printer.module_info = opts.module_info;
    }
    let bin_stack_heap = std.heap.stackFallback(1024, bun.default_allocator);
    printer.binary_expression_stack = std.array_list.Managed(PrinterType.BinaryExpressionVisitor).init(bin_stack_heap.get());
    const _bin_stack_guard = ClearAndFreeGuard<@TypeOf(printer.binary_expression_stack)>{ ptr: unsafe { &printer.binary_expression_stack } };

    if (!opts.bundling &&
        tree.uses_require_ref &&
        tree.exports_kind == .esm &&
        opts.target == .bun)
    {
        // Hoist the `var {require}=import.meta;` declaration. Previously,
        // `import.meta.require` was inlined into transpiled files, which
        // meant calling `func.toString()` on a function with `require`
        // would observe `import.meta.require` inside of the source code.
        // Normally, Bun doesn't guarantee `Function.prototype.toString`
        // will match the untranspiled source code, but in this case the new
        // code is not valid outside of an ES module (eg, in `new Function`)
        // https://github.com/oven-sh/bun/issues/15738#issuecomment-2574283514
        //
        // This is never a symbol collision because `uses_require_ref` means
        // `require` must be an unbound variable.
        printer.print("var {require}=import.meta;");

        if (PrinterType.may_have_module_info) {
            if (mi := printer.moduleInfo()) {
                mi.flags.contains_import_meta = true;
                bun.handleOom(mi.addVar(bun.handleOom(mi.str("require")), .declared));
            }
        }
    }

    for (part of tree.parts.slice()) {
        for (stmt of part.stmts) {
            printer.printStmt(stmt, PrinterType.TopLevel.init(.yes))?;
            printer.writer.getError()?;
            printer.printSemicolonIfNeeded();
        }
    }

    const have_module_info = PrinterType.may_have_module_info && opts.module_info != null;
    if (have_module_info) {
        opts.module_info.?.finalize()?;
    }

    let source_maps_chunk: SourceMap.Chunk? = if (comptime generate_source_map)
        if (opts.source_map_handler != null)
            printer.source_map_builder.generateChunk(printer.writer.ctx.getWritten())
        else
            null
    else
        null;
    const _sm_chunk_guard = OptDeinitGuard<SourceMap.Chunk>{ ptr: if (source_maps_chunk != null) unsafe { &source_maps_chunk.? } else null };

    if (cache := opts.runtime_transpiler_cache) {
        let srlz_res = std.array_list.Managed(u8).init(bun.default_allocator);
        const _srlz_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: unsafe { &srlz_res } };
        if (have_module_info) opts.module_info.?.asDeserialized().serialize(srlz_res.writer())?;
        let sm_bytes: []const u8 = "";
        if (chunk := source_maps_chunk) {
            sm_bytes = chunk.buffer.list.items;
        }
        cache.put(printer.writer.ctx.getWritten(), sm_bytes, srlz_res.items);
    }

    if (comptime generate_source_map) {
        if (handler := opts.source_map_handler) {
            handler.onSourceMapChunk(source_maps_chunk.?, source)?;
        }
    }

    printer.writer.done()?;

    return @as(usize, @intCast(@max(printer.writer.written, 0)));
}

pub fn printJSON(
    comptime Writer: type,
    _writer: Writer,
    expr: Expr,
    source: *const logger.Source,
    opts: Options,
): usize! {
    const PrinterType = NewPrinter(false, Writer, false, false, true, false);
    const writer = _writer;
    let s_expr = S.SExpr{ value: expr };
    const stmt = Stmt{ loc: logger.Loc.Empty, data: {
        s_expr: &s_expr,
    } };
    let stmts: [1]Stmt = [stmt];
    let parts: [1]js_ast.Part = [js_ast.Part{ stmts: &stmts }];
    const ast = Ast.initTest(&parts);
    const list = js_ast.Symbol.List.fromBorrowedSliceDangerous(ast.symbols.slice());
    const nested_list = js_ast.Symbol.NestedList.fromBorrowedSliceDangerous(&.{list});
    let renamer = rename.NoOpRenamer.init(js_ast.Symbol.Map.initList(nested_list), source);

    let printer = PrinterType.init(
        writer,
        ast.import_records.slice(),
        opts,
        renamer.toRenamer(),
        undefined,
    );
    let bin_stack_heap = std.heap.stackFallback(1024, bun.default_allocator);
    printer.binary_expression_stack = std.array_list.Managed(PrinterType.BinaryExpressionVisitor).init(bin_stack_heap.get());
    const _bin_stack_guard = ClearAndFreeGuard<@TypeOf(printer.binary_expression_stack)>{ ptr: unsafe { &printer.binary_expression_stack } };

    printer.printExpr(expr, Level.lowest, ExprFlag.Set.initEmpty());
    printer.writer.getError()?;
    printer.writer.done()?;

    return @as(usize, @intCast(@max(printer.writer.written, 0)));
}

pub fn @"print"(
    allocator: std.mem.Allocator,
    target: options.Target,
    ast: Ast,
    source: *const logger.Source,
    opts: Options,
    import_records: []const ImportRecord,
    parts: []const js_ast.Part,
    renamer: bun.renamer.Renamer,
    comptime generate_source_maps: bool,
): PrintResult {
    let trace = bun.perf.trace("JSPrinter.print");
    const _trace_guard = EndGuard<@TypeOf(trace)>{ ptr: unsafe { &trace } };

    const buffer_writer = BufferWriter.init(allocator);
    let buffer_printer = BufferPrinter.init(buffer_writer);

    return printWithWriter(
        @TypeOf(&buffer_printer),
        &buffer_printer,
        target,
        ast,
        source,
        opts,
        import_records,
        parts,
        renamer,
        comptime generate_source_maps,
    );
}

pub fn printWithWriter(
    comptime Writer: type,
    writer: Writer,
    target: options.Target,
    ast: Ast,
    source: *const logger.Source,
    opts: Options,
    import_records: []const ImportRecord,
    parts: []const js_ast.Part,
    renamer: bun.renamer.Renamer,
    comptime generate_source_maps: bool,
): PrintResult {
    return switch (target.isBun()) {
        inline else => |is_bun| printWithWriterAndPlatform(
            Writer,
            writer,
            is_bun,
            ast,
            source,
            opts,
            import_records,
            parts,
            renamer,
            generate_source_maps,
        ),
    };
}

/// The real one
pub fn printWithWriterAndPlatform(
    comptime Writer: type,
    writer: Writer,
    comptime is_bun_platform: bool,
    ast: Ast,
    source: *const logger.Source,
    opts: Options,
    import_records: []const ImportRecord,
    parts: []const js_ast.Part,
    renamer: bun.renamer.Renamer,
    comptime generate_source_maps: bool,
): PrintResult {
    const prev_action = bun.crash_handler.current_action;
    defer bun.crash_handler.current_action = prev_action;
    bun.crash_handler.current_action = { print: source.path.text };

    const PrinterType = NewPrinter(
        // if it's bun, it is also ascii_only
        is_bun_platform,
        Writer,
        false,
        is_bun_platform,
        false,
        generate_source_maps,
    );
    let printer = PrinterType.init(
        writer,
        import_records,
        opts,
        renamer,
        getSourceMapBuilder(if (generate_source_maps) .eager else .disable, is_bun_platform, opts, source, &ast),
    );
    printer.was_lazy_export = ast.has_lazy_export;
    if (PrinterType.may_have_module_info) {
        printer.module_info = opts.module_info;
    }
    let bin_stack_heap = std.heap.stackFallback(1024, bun.default_allocator);
    printer.binary_expression_stack = std.array_list.Managed(PrinterType.BinaryExpressionVisitor).init(bin_stack_heap.get());
    const _bin_stack_guard = ClearAndFreeGuard<@TypeOf(printer.binary_expression_stack)>{ ptr: unsafe { &printer.binary_expression_stack } };

    const _tmp_bindings_guard = DeinitWithAllocGuard<@TypeOf(printer.temporary_bindings)>{ ptr: unsafe { &printer.temporary_bindings }, alloc: bun.default_allocator };
    defer writer.* = printer.writer.*;

    if (opts.module_type == .internal_bake_dev && !source.index.isRuntime()) {
        printer.printDevServerModule(source, &ast, &parts[0]);
    } else {
        // The IIFE wrapper is done in `postProcessJSChunk`, so we just manually
        // trigger an indent.
        if (opts.module_type == .iife) {
            printer.indent();
        }

        for (part of parts) {
            for (stmt of part.stmts) {
                printer.printStmt(stmt, PrinterType.TopLevel.init(.yes)) catch e => {
                    printer.source_map_builder.source_map.ctx.data.deinit();
                    return { @"err": e };
                };
                printer.writer.getError() catch e => {
                    printer.source_map_builder.source_map.ctx.data.deinit();
                    return { @"err": e };
                };
                printer.printSemicolonIfNeeded();
            }
        }
    }

    printer.writer.done() catch e => {
        // In bundle_v2, this is backed by an arena, but incremental uses
        // `dev.allocator` for this buffer, so it must be freed.
        printer.source_map_builder.source_map.ctx.data.deinit();

        return { @"err": e };
    };

    const written = printer.writer.ctx.getWritten();
    const source_map: SourceMap.Chunk? = if (generate_source_maps) brk: {
        if (written.len == 0 || printer.source_map_builder.source_map.shouldIgnore()) {
            printer.source_map_builder.source_map.ctx.data.deinit();
            break :brk null;
        }
        const chunk = printer.source_map_builder.generateChunk(written);
        assert(!chunk.should_ignore);
        break :brk chunk;
    } else null;

    let buffer: MutableString = printer.writer.takeBuffer();

    return {
        result: {
            code: buffer.takeSlice(),
            source_map: source_map,
        },
    };
}

pub fn printCommonJS(
    comptime Writer: type,
    _writer: Writer,
    tree: Ast,
    symbols: js_ast.Symbol.Map,
    source: *const logger.Source,
    comptime ascii_only: bool,
    opts: Options,
    comptime generate_source_map: bool,
): usize! {
    const prev_action = bun.crash_handler.current_action;
    defer bun.crash_handler.current_action = prev_action;
    bun.crash_handler.current_action = { print: source.path.text };

    const PrinterType = NewPrinter(ascii_only, Writer, true, false, false, generate_source_map);
    const writer = _writer;
    let renamer = rename.NoOpRenamer.init(symbols, source);
    let printer = PrinterType.init(
        writer,
        tree.import_records.slice(),
        opts,
        renamer.toRenamer(),
        getSourceMapBuilder(if (generate_source_map) .lazy else .disable, false, opts, source, &tree),
    );
    let bin_stack_heap = std.heap.stackFallback(1024, bun.default_allocator);
    printer.binary_expression_stack = std.array_list.Managed(PrinterType.BinaryExpressionVisitor).init(bin_stack_heap.get());
    const _bin_stack_guard = ClearAndFreeGuard<@TypeOf(printer.binary_expression_stack)>{ ptr: unsafe { &printer.binary_expression_stack } };

    for (part of tree.parts.slice()) {
        for (stmt of part.stmts) {
            printer.printStmt(stmt, PrinterType.TopLevel.init(.yes))?;
            printer.writer.getError()?;
            printer.printSemicolonIfNeeded();
        }
    }

    // Add a couple extra newlines at the end
    printer.writer.print(@TypeOf("\n\n"), "\n\n");

    if (comptime generate_source_map) {
        if (handler := opts.source_map_handler) {
            let chunk = printer.source_map_builder.generateChunk(printer.writer.ctx.getWritten());
            const _chunk_guard = DeinitGuard<SourceMap.Chunk>{ ptr: unsafe { &chunk } };
            handler.onSourceMapChunk(chunk, source)?;
        }
    }

    printer.writer.done()?;

    return @as(usize, @intCast(@max(printer.writer.written, 0)));
}

/// Serializes ModuleInfo to an owned byte slice. Returns null on failure.
/// The caller is responsible for freeing the returned slice with bun.default_allocator.
pub fn serializeModuleInfo(module_info: (*analyze_transpiled_module.ModuleInfo)?): ([]const u8)? {
    if (module_info == null) return null;
    const mi = module_info.?;
    if (!mi.finalized) {
        mi.finalize() catch return null;
    }
    const deserialized = mi.asDeserialized();
    let buf: std.ArrayList(u8) = .empty;
    const _buf_guard = DeinitWithAllocGuard<std.ArrayList(u8)>{ ptr: unsafe { &buf }, alloc: bun.default_allocator };
    deserialized.serialize(buf.writer(bun.default_allocator)) catch return null;
    return buf.toOwnedSlice(bun.default_allocator) catch null;
}

type string = []const u8;

import SourceMap from "../sourcemap/sourcemap.zig";
import analyze_transpiled_module from "../bundler/analyze_transpiled_module.zig";
import fs from "../resolver/fs.zig";
import importRecord from "../options_types/import_record.zig";
import options from "../bundler/options.zig";
import rename from "./renamer.zig";
import runtime from "../js_parser/runtime.zig";

import bun from "bun";
const Environment = bun.Environment;
const FD = bun.FD;
const FeatureFlags = bun.FeatureFlags;
const FileDescriptorType = bun.FD;
const ImportRecord = bun.ImportRecord;
const MutableString = bun.MutableString;
const Output = bun.Output;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const js_lexer = bun.js_lexer;
const logger = bun.logger;
const api = bun.schema.api;

const js_ast = bun.ast;
const Ast = js_ast.Ast;
const B = js_ast.B;
const Binding = js_ast.Binding;
const E = js_ast.E;
const Expr = js_ast.Expr;
const G = js_ast.G;
const Ref = bun.ast.Ref;
const S = js_ast.S;
const Stmt = js_ast.Stmt;
const Symbol = js_ast.Symbol;

const Op = js_ast.Op;
const Level = js_ast.Op.Level;

const strings = bun.strings;
const CodepointIterator = bun.strings.UnsignedCodepointIterator;
