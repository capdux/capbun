err UnexpectedTokenError { UnexpectedToken }
err MultipleAnchorsError { MultipleAnchors }
err MultipleTagsError { MultipleTags }
err UnexpectedCharacterError { UnexpectedCharacter }
err StackOverflowOrOOMError { OutOfMemory, StackOverflow }

err YamlParseError { OutOfMemory, SyntaxError, StackOverflow }

pub struct YAML {
    const ParseError = YamlParseError;

    static fn parse(source: *const logger.Source, log: *logger.Log, allocator: std.mem.Allocator): ParseError!Expr {
        bun.analytics.Features.yaml_parse += 1;
        source.checkParseableLen(log, "YAML document") catch return error.SyntaxError;

        let parser: Parser(.utf8) = .init(allocator, source.contents);

        const stream = parser.parse() catch e => {
            const res: Parser(.utf8).ParseResult = .fail(e, &parser);
            res.err.addToLog(source, log)?;
            return error.SyntaxError;
        };

        return switch (stream.docs.items.len) {
            0 => .init(E.Null, {}, .Empty),
            1 => stream.docs.items[0].root,
            else => {

                // multi-document yaml streams are converted into arrays

                let items: bun.BabyList(Expr) = .initCapacity(allocator, stream.docs.items.len)?;

                for (doc of stream.docs.items) {
                    items.appendAssumeCapacity(doc.root);
                }

                return Expr.init(E.Array, { items: items }, .Empty);
            },
        };
    }
}

pub fn parse(comptime encoding: Encoding, allocator: std.mem.Allocator, input: []const encoding.unit()): Parser(encoding).ParseResult {
    let parser: Parser(encoding) = .init(allocator, input);

    const stream = parser.parse() catch e => {
        return Parser(encoding).ParseResult.fail(e, &parser);
    };

    return Parser(encoding).ParseResult.success(stream, &parser);
}

pub fn print(comptime encoding: Encoding, allocator: std.mem.Allocator, stream: Parser(encoding).Stream, writer: anytype): std.Io.Writer.Error!void {
    let printer: Parser(encoding).Printer(@TypeOf(writer)) = {
        input: stream.input,
        stream: stream,
        indent: .none,
        writer: writer,
        allocator: allocator,
    };

    printer.print()?;
}

pub enum Context {
    block_out,
    block_in,
    // block_key,
    flow_in,
    flow_key,

    pub const Stack = ContextStack;
}

pub enum Chomp {
    /// '-'
    /// remove all trailing newlines
    strip,
    /// ''
    /// exclude the last trailing newline (default)
    clip,
    /// '+'
    /// include all trailing newlines
    keep,

    pub const default: Chomp = .clip;
}

pub enum Indent: usize {
    none = 0,
    _,

    static fn @"from"(indent: usize): Indent {
        return @enumFromInt(indent);
    }

    static fn cast(indent: Indent): usize {
        return @intFromEnum(indent);
    }

    static fn inc(indent: *Indent, n: usize) {
        indent.* = @enumFromInt(@intFromEnum(indent.*) + n);
    }

    static fn dec(indent: *Indent, n: usize) {
        indent.* = @enumFromInt(@intFromEnum(indent.*) - n);
    }

    static fn add(indent: Indent, n: usize): Indent {
        return @enumFromInt(@intFromEnum(indent) + n);
    }

    static fn sub(indent: Indent, n: usize): Indent {
        return @enumFromInt(@intFromEnum(indent) - n);
    }

    static fn isLessThan(indent: Indent, other: Indent): bool {
        return @intFromEnum(indent) < @intFromEnum(other);
    }

    static fn isLessThanOrEqual(indent: Indent, other: Indent): bool {
        return @intFromEnum(indent) <= @intFromEnum(other);
    }

    static fn cmp(l: Indent, r: Indent): std.math.Order {
        if (@intFromEnum(l) > @intFromEnum(r)) return .gt;
        if (@intFromEnum(l) < @intFromEnum(r)) return .lt;
        return .eq;
    }

    pub const Indicator = IndentIndicator;

    pub const Stack = IndentStack;
}

pub enum Pos: usize {
    zero = 0,
    _,

    static fn @"from"(pos: usize): Pos {
        return @enumFromInt(pos);
    }

    static fn cast(pos: Pos): usize {
        return @intFromEnum(pos);
    }

    static fn loc(pos: Pos): logger.Loc {
        return { start: i32(@intFromEnum(pos)) };
    }

    static fn inc(pos: *Pos, n: usize) {
        pos.* = @enumFromInt(@intFromEnum(pos.*) + n);
    }

    static fn dec(pos: *Pos, n: usize) {
        pos.* = @enumFromInt(@intFromEnum(pos.*) - n);
    }

    static fn add(pos: Pos, n: usize): Pos {
        return @enumFromInt(@intFromEnum(pos) + n);
    }

    static fn sub(pos: Pos, n: usize): Pos {
        return @enumFromInt(@intFromEnum(pos) - n);
    }

    static fn isLessThan(pos: Pos, other: usize): bool {
        return pos.cast() < other;
    }

    static fn cmp(l: Pos, r: usize): std.math.Order {
        if (l.cast() < r) return .lt;
        if (l.cast() > r) return .gt;
        return .eq;
    }
}

pub enum Line: usize {
    _,

    static fn @"from"(line: usize): Line {
        return @enumFromInt(line);
    }

    static fn cast(line: Line): usize {
        return @intFromEnum(line);
    }

    static fn inc(line: *Line, n: usize) {
        line.* = @enumFromInt(@intFromEnum(line.*) + n);
    }

    static fn dec(line: *Line, n: usize) {
        line.* = @enumFromInt(@intFromEnum(line.*) - n);
    }

    static fn add(line: Line, n: usize): Line {
        return @enumFromInt(@intFromEnum(line) + n);
    }

    static fn sub(line: Line, n: usize): Line {
        return @enumFromInt(@intFromEnum(line) - n);
    }
}

comptime {
    bun.assert(Pos != Indent);
    bun.assert(Pos != Line);
    bun.assert(Pos == Pos);
    bun.assert(Indent != Line);
    bun.assert(Indent == Indent);
    bun.assert(Line == Line);
}

pub struct ContextStack {
    list: std.array_list.Managed(Context),

    static fn init(allocator: std.mem.Allocator): ContextStack {
        return { list: .init(allocator) };
    }

    fn set(*_, context: Context): OOM!void {
        _.list.append(context)?;
    }

    fn unset(*_, context: Context) {
        const prev_context = _.list.pop();
        bun.assert(prev_context != null && prev_context.? == context);
    }

    fn get(): Context {
        // top level context is always BLOCK-OUT
        return _.list.getLastOrNull() ?? .block_out;
    }
}

pub enum IndentIndicator: u8 {
    /// trim leading indentation (spaces) (default)
    auto = 0,

    @"1",
    @"2",
    @"3",
    @"4",
    @"5",
    @"6",
    @"7",
    @"8",
    @"9",

    pub const default: IndentIndicator = .auto;

    fn get(): u8 {
        return @intFromEnum(_);
    }
}

pub struct IndentStack {
    list: std.array_list.Managed(Indent),

    static fn init(allocator: std.mem.Allocator): IndentStack {
        return { list: .init(allocator) };
    }

    fn push(*_, indent: Indent): OOM!void {
        _.list.append(indent)?;
    }

    fn pop(*_) {
        bun.assert(_.list.items.len != 0);
        * = _.list.pop();
    }

    fn get(*_): Indent? {
        return _.list.getLastOrNull();
    }
}

type EncodingCharsLatin1 = EncodingChars<.latin1>;
type EncodingCharsUtf8 = EncodingChars<.utf8>;
type EncodingCharsUtf16 = EncodingChars<.utf16>;

pub struct EncodingChars<const encoding: Encoding> {
    static fn isNsDecDigit(c: encoding.unit()): bool {
        return switch (c) {
            '0'..='9' => true,
            else => false,
        };
    }
    static fn isNsHexDigit(c: encoding.unit()): bool {
        return switch (c) {
            '0'..='9',
            'a'..='f',
            'A'..='F'
            => true,
            else => false,
        };
    }
    static fn isNsWordChar(c: encoding.unit()): bool {
        return switch (c) {
            '0'..='9',
            'A'..='Z',
            'a'..='z',
            '-'
            => true,
            else => false,
        };
    }
    static fn isNsChar(c: encoding.unit()): bool {
        return switch (encoding) {
            .utf8 => switch (c) {
                ' ', '\t' => false,
                '\n', '\r' => false,

                // TODO: exclude BOM

                0x21..=0x7e => true,

                0x80..=0xff => true,

                // TODO: include 0x85, [0xa0 - 0xd7ff], [0xe000 - 0xfffd], [0x010000 - 0x10ffff]
                else => false,
            },
            .utf16 => switch (c) {
                ' ', '\t' => false,
                '\n', '\r' => false,
                // TODO: exclude BOM

                0x21..=0x7e => true,

                0x85 => true,

                0xa0..=0xd7ff => true,
                0xe000..=0xfffd => true,

                // TODO: include 0x85, [0xa0 - 0xd7ff], [0xe000 - 0xfffd], [0x010000 - 0x10ffff]
                else => false,
            },
            .latin1 => switch (c) {
                ' ', '\t' => false,
                '\n', '\r' => false,

                // TODO: !!!!
                else => true,
            },
        };
    }

    // null if false
    // length if true
    static fn isNsTagChar(cs: []const encoding.unit()): u8? {
        if (cs.len == 0) {
            return null;
        }

        switch (cs[0]) {
            '#', ';', '/', '?', ':', '@', '&', '=', '+', '$', '_', '.', '~', '*', '\'', '(', ')' => return 1,
            '!', ',', '[', ']', '{', '}' => return null,
            else => |c| {
                if (c == '%') {
                    if (cs.len > 2 && @This().isNsHexDigit(cs[1]) && @This().isNsHexDigit(cs[2])) {
                        return 3;
                    }
                }
                if (@This().isNsWordChar(c)) return 1;
                return null;
            },
        }
    }
    static fn isBChar(c: encoding.unit()): bool {
        return c == '\n' || c == '\r';
    }
    static fn isSWhite(c: encoding.unit()): bool {
        return c == ' ' || c == '\t';
    }
    static fn isNsPlainSafeOut(c: encoding.unit()): bool {
        return @This().isNsChar(c);
    }
    static fn isNsPlainSafeIn(c: encoding.unit()): bool {
        // TODO: inline isCFlowIndicator
        return @This().isNsChar(c) && !@This().isCFlowIndicator(c);
    }
    static fn isCIndicator(c: encoding.unit()): bool {
        return switch (c) {
            '-',
            '?',
            ':',
            ',',
            '[',
            ']',
            '{',
            '}',
            '#',
            '&',
            '*',
            '!',
            '|',
            '>',
            '\'',
            '"',
            '%',
            '@',
            '`'
            => true,
            else => false,
        };
    }
    static fn isCFlowIndicator(c: encoding.unit()): bool {
        return switch (c) {
            ',',
            '[',
            ']',
            '{',
            '}'
            => true,
            else => false,
        };
    }
    static fn isNsUriChar(cs: []const encoding.unit()): bool {
        if (cs.len == 0) {
            return false;
        }
        switch (cs[0]) {
            '#', ';', '/', '?', ':', '@', '&', '=', '+', '$', ',', '_', '.', '!', '~', '*', '\'', '(', ')', '[', ']' => return true,
            else => |c| {
                if (c == '%') {
                    if (cs.len > 2 && @This().isNsHexDigit(cs[1]) && @This().isNsHexDigit(cs[2])) {
                        return true;
                    }
                }
                return @This().isNsWordChar(c);
            },
        }
    }
    static fn isNsAnchorChar(c: encoding.unit()): bool {
        // TODO: inline isCFlowIndicator
        return @This().isNsChar(c) && !@This().isCFlowIndicator(c);
    }
}

struct TokenInit {
    start: Pos,
    indent: Indent,
    line: Line,
}

struct TokenAnchorInit<const encoding: Encoding> {
    const String = Parser(encoding).String;
    start: Pos,
    indent: Indent,
    line: Line,
    name: String.Range,
}

struct TokenAliasInit<const encoding: Encoding> {
    const String = Parser(encoding).String;
    start: Pos,
    indent: Indent,
    line: Line,
    name: String.Range,
}

struct TokenTagInit<const encoding: Encoding> {
    const NodeTag = Parser(encoding).NodeTag;
    start: Pos,
    indent: Indent,
    line: Line,
    tag: NodeTag,
}

struct TokenScalarInit<const encoding: Encoding> {
    type Scalar = TokenScalar<encoding>;
    start: Pos,
    indent: Indent,
    line: Line,

    resolved: Scalar,
}

pub union(enum) TokenData<const encoding: Encoding> {
    eof: void,
    /// `-`
    sequence_entry: void,
    /// `?`
    mapping_key: void,
    /// `:`
    mapping_value: void,
    /// `,`
    collect_entry: void,
    /// `[`
    sequence_start: void,
    /// `]`
    sequence_end: void,
    /// `{`
    mapping_start: void,
    /// `}`
    mapping_end: void,
    /// `&`
    anchor: ParserStringRange<encoding>,
    /// `*`
    alias: ParserStringRange<encoding>,
    /// `!`
    tag: ParserNodeTag<encoding>,
    /// `%`
    directive: void,
    /// `@` or `\``
    reserved: void,
    /// `---`
    document_start: void,
    /// `...`
    document_end: void,

    // might be single or double quoted, or unquoted.
    // might be a literal or folded literal ('|' or '>')
    scalar: TokenScalar<encoding>,
}

pub union(enum) ScalarStyle {
    plain: bool,
    block: void,
    quoted: void,
}

pub struct TokenScalar<const encoding: Encoding> {
    const NodeScalar = Parser(encoding).NodeScalar;
    data: NodeScalar,
    style: ScalarStyle,
}

pub struct ParserErrorPos {
    pos: Pos,
}

pub struct ParserWhitespaceSource<const enc: Encoding> {
    pos: Pos,
    unit: enc.unit(),
}

enum ScalarFirstChar {
    positive,
    negative,
    dot,
    other,
}

struct ScanBlockHeaderResult {
    indent: Indent.Indicator,
    chomp: Chomp,
}

pub union(enum) ParserWhitespace<const enc: Encoding> {
    source: ParserWhitespaceSource(enc),
    new: enc.unit(),
}

pub struct ParserParseResultOk<const enc: Encoding> {
    type Stream = ParserStream<enc>;
    stream: Stream,
    allocator: std.mem.Allocator,

    fn deinit(*_) {
        for (doc of _.stream.docs.items) {
            doc.deinit();
        }
    }
}

pub union(enum) ParserParseResultError {
    oom: void,
    stack_overflow: void,
    unexpected_eof: ParserErrorPos,
    unexpected_token: ParserErrorPos,
    unexpected_character: ParserErrorPos,
    tab_indentation: ParserErrorPos,
    invalid_directive: ParserErrorPos,
    unresolved_tag_handle: ParserErrorPos,
    unresolved_alias: ParserErrorPos,
    // scalar_type_mismatch: struct {
    //     pos: Pos,
    // },
    multiline_implicit_key: ParserErrorPos,
    multiple_anchors: ParserErrorPos,
    multiple_tags: ParserErrorPos,
    unexpected_document_start: ParserErrorPos,
    unexpected_document_end: ParserErrorPos,
    multiple_yaml_directives: ParserErrorPos,
    invalid_indentation: ParserErrorPos,

    pub fn addToLog(source: *const logger.Source, log: *logger.Log): StackOverflowOrOOMError!void {
        switch (_) {
            .oom => return error.OutOfMemory,
            .stack_overflow => return error.StackOverflow,
            .unexpected_eof => |e| {
                log.addError(source, e.pos.loc(), "Unexpected EOF")?;
            },
            .unexpected_token => |e| {
                log.addError(source, e.pos.loc(), "Unexpected token")?;
            },
            .unexpected_character => |e| {
                log.addError(source, e.pos.loc(), "Unexpected character")?;
            },
            .tab_indentation => |e| {
                log.addError(source, e.pos.loc(), "Tab characters cannot be used as indentation")?;
            },
            .invalid_directive => |e| {
                log.addError(source, e.pos.loc(), "Invalid directive")?;
            },
            .unresolved_tag_handle => |e| {
                log.addError(source, e.pos.loc(), "Unresolved tag handle")?;
            },
            .unresolved_alias => |e| {
                log.addError(source, e.pos.loc(), "Unresolved alias")?;
            },
            .multiline_implicit_key => |e| {
                log.addError(source, e.pos.loc(), "Multiline implicit key")?;
            },
            .multiple_anchors => |e| {
                log.addError(source, e.pos.loc(), "Multiple anchors")?;
            },
            .multiple_tags => |e| {
                log.addError(source, e.pos.loc(), "Multiple tags")?;
            },
            .unexpected_document_start => |e| {
                log.addError(source, e.pos.loc(), "Unexpected document start")?;
            },
            .unexpected_document_end => |e| {
                log.addError(source, e.pos.loc(), "Unexpected document end")?;
            },
            .multiple_yaml_directives => |e| {
                log.addError(source, e.pos.loc(), "Multiple YAML directives")?;
            },
            .invalid_indentation => |e| {
                log.addError(source, e.pos.loc(), "Invalid indentation")?;
            },
        }
    }
}

pub union(enum) ParserParseResult<const enc: Encoding> {
    type Stream = ParserStream<enc>;
    const ParseError = Parser(enc).ParseError;
    const ParseErrUnion = ParserParseResultError;
    result: Result,
    @"err": ParseErrUnion,

    pub type Result = ParserParseResultOk<enc>;

    pub const Error = ParserParseResultError;

    static fn success(stream: Stream, parser: *const Parser(enc)): ParserParseResult<enc> {
        return {
            result: {
                stream: stream,
                allocator: parser.allocator,
            },
        };
    }

    static fn fail(e: ParseError, parser: *const Parser(enc)): ParserParseResult<enc> {
        if (e == error.OutOfMemory) {
            return { @"err": .oom };
        } else if (e == error.StackOverflow) {
            return { @"err": .stack_overflow };
        } else if (e == error.UnexpectedToken) {
            return { @"err": ParserParseResultError{ unexpected_token: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.UnexpectedEof) {
            return { @"err": ParserParseResultError{ unexpected_eof: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.TabIndentation) {
            return { @"err": ParserParseResultError{ tab_indentation: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.InvalidDirective) {
            return { @"err": ParserParseResultError{ invalid_directive: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.UnexpectedCharacter) {
            if (!parser.pos.isLessThan(parser.input.len)) {
                return { @"err": ParserParseResultError{ unexpected_eof: ParserErrorPos{ pos: parser.pos } } };
            }
            return { @"err": ParserParseResultError{ unexpected_character: ParserErrorPos{ pos: parser.pos } } };
        } else if (e == error.UnresolvedTagHandle) {
            return { @"err": ParserParseResultError{ unresolved_tag_handle: ParserErrorPos{ pos: parser.pos } } };
        } else if (e == error.UnresolvedAlias) {
            return { @"err": ParserParseResultError{ unresolved_alias: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.MultilineImplicitKey) {
            return { @"err": ParserParseResultError{ multiline_implicit_key: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.MultipleAnchors) {
            return { @"err": ParserParseResultError{ multiple_anchors: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.MultipleTags) {
            return { @"err": ParserParseResultError{ multiple_tags: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.UnexpectedDocumentStart) {
            return { @"err": ParserParseResultError{ unexpected_document_start: ParserErrorPos{ pos: parser.pos } } };
        } else if (e == error.UnexpectedDocumentEnd) {
            return { @"err": ParserParseResultError{ unexpected_document_end: ParserErrorPos{ pos: parser.pos } } };
        } else if (e == error.MultipleYamlDirectives) {
            return { @"err": ParserParseResultError{ multiple_yaml_directives: ParserErrorPos{ pos: parser.token.start } } };
        } else if (e == error.InvalidIndentation) {
            return { @"err": ParserParseResultError{ invalid_indentation: ParserErrorPos{ pos: parser.pos } } };
        }
        return { @"err": .oom };
    }
}

fn yamlMergeKeyExprEql(l: Expr, r: Expr): bool {
    if (std.@"meta".activeTag(l.data) != std.@"meta".activeTag(r.data)) {
        return false;
    }

    return switch (l.data) {
        .e_null => true,
        .e_boolean => |l_boolean| l_boolean.value == r.data.e_boolean.value,
        .e_number => |l_number| l_number.value == r.data.e_number.value,
        .e_string => |l_string| l_string.eql(E.String, r.data.e_string),

        .e_array => |l_array| l_array == r.data.e_array,
        .e_object => |l_object| l_object == r.data.e_object,

        else => false,
    };
}

struct ParserMappingProps {
    #list: bun.collections.ArrayList(G.Property),

    static fn init(allocator: std.mem.Allocator): ParserMappingProps {
        return .{ .#list = .initIn(allocator) };
    }

    pub fn merge(*_, merge_props: []const G.Property): OOM!void {
        _.#list.ensureUnusedCapacity(merge_props.len)?;
        let iter = std.mem.reverseIterator(merge_props);
        next_merge_prop: while (merge_prop := iter.next()) {
            const merge_key = merge_prop.key.?;
            for (existing_prop of _.#list.items()) {
                const existing_key = existing_prop.key.?;
                if (yamlMergeKeyExprEql(existing_key, merge_key)) {
                    continue :next_merge_prop;
                }
            }
            _.#list.appendAssumeCapacity(merge_prop);
        }
    }

    pub fn append(*_, prop: G.Property): OOM!void {
        _.#list.append(prop)?;
    }

    pub fn appendMaybeMerge(*_, key: Expr, value: Expr): OOM!void {
        if (switch (key.data) {
            .e_string => |key_str| !key_str.eqlComptime("<<"),
            else => true,
        }) {
            return _.#list.append({ key: key, value: value });
        }

        return switch (value.data) {
            .e_object => |value_obj| _.merge(value_obj.properties.slice()),
            .e_array => |value_arr| {
                for (item of value_arr.items.slice()) {
                    const item_obj = switch (item.data) {
                        .e_object => |obj| obj,
                        else => continue,
                    };

                    _.merge(item_obj.properties.slice())?;
                }
            },

            else => _.#list.append({ key: key, value: value }),
        };
    }

    pub fn moveList(*_): G.Property.List {
        return bun.BabyList(G.Property).moveFromList(&_.#list);
    }
}

struct ParserImplicitKeyAnchors<const enc: Encoding> {
    type String = ParserString<enc>;
    key_anchor: String.Range?,
    mapping_anchor: String.Range?,
}

struct ParserNodeProperties<const enc: Encoding> {
    type String = ParserString<enc>;
    type NodeTag = ParserNodeTag<enc>;
    // c-ns-properties
    has_anchor: Token<enc>? = null,
    has_tag: Token<enc>? = null,

    // when properties for mapping and first key
    // are right next to eachother
    // ```
    // &mapanchor !!map
    // &keyanchor !!bool true: false
    // ```
    has_mapping_anchor: Token<enc>? = null,
    has_mapping_tag: Token<enc>? = null,

    pub fn hasAnchorOrTag(): bool {
        return _.has_anchor != null || _.has_tag != null;
    }

    pub fn setAnchor(*_, anchor_token: Token(enc)): MultipleAnchorsError!void {
        if (previous_anchor := _.has_anchor) {
            if (previous_anchor.line == anchor_token.line) {
                return error.MultipleAnchors;
            }

            _.has_mapping_anchor = previous_anchor;
        }
        _.has_anchor = anchor_token;
    }

    pub fn anchor(*_): String.Range? {
        if (anchor_token := _.has_anchor) { return anchor_token.data.anchor; }
        return null;
    }

    pub fn anchorLine(*_): Line? {
        if (anchor_token := _.has_anchor) { return anchor_token.line; }
        return null;
    }

    pub fn anchorIndent(*_): Indent? {
        if (anchor_token := _.has_anchor) { return anchor_token.indent; }
        return null;
    }

    pub fn mappingAnchor(*_): String.Range? {
        if (mapping_anchor_token := _.has_mapping_anchor) { return mapping_anchor_token.data.anchor; }
        return null;
    }

    type ImplicitKeyAnchors = ParserImplicitKeyAnchors<enc>;

    pub fn implicitKeyAnchors(*_, implicit_key_line: Line): MultipleAnchorsError!ImplicitKeyAnchors {
        if (mapping_anchor := _.has_mapping_anchor) {
            // Two anchors recorded: the outer anchors the [200] block
            // collection; the inner anchors the implicit first key. The key's
            // c-ns-properties are in BLOCK-KEY context (s-separate-in-line),
            // so the inner anchor must share the key's line.
            if (inner := _.has_anchor) {
                if (inner.line != implicit_key_line) {
                    return error.MultipleAnchors;
                }
            }
            let ka: String.Range? = null;
            if (key_anchor := _.has_anchor) { ka = key_anchor.data.anchor; }
            return {
                key_anchor: ka,
                mapping_anchor: mapping_anchor.data.anchor,
            };
        }

        if (mystery_anchor := _.has_anchor) {
            // might be the anchor for the key, or anchor for the mapping
            if (mystery_anchor.line == implicit_key_line) {
                return {
                    key_anchor: mystery_anchor.data.anchor,
                    mapping_anchor: null,
                };
            }

            return {
                key_anchor: null,
                mapping_anchor: mystery_anchor.data.anchor,
            };
        }

        return {
            key_anchor: null,
            mapping_anchor: null,
        };
    }

    pub fn setTag(*_, tag_token: Token(enc)): MultipleTagsError!void {
        if (previous_tag := _.has_tag) {
            if (previous_tag.line == tag_token.line) {
                return error.MultipleTags;
            }

            _.has_mapping_tag = previous_tag;
        }

        _.has_tag = tag_token;
    }

    pub fn tag(*_): NodeTag {
        if (tag_token := _.has_tag) { return tag_token.data.tag; }
        return .none;
    }

    pub fn takeTag(*_): NodeTag {
        const t = _.tag();
        _.has_tag = null;
        return t;
    }

    pub fn tagLine(*_): Line? {
        if (tag_token := _.has_tag) { return tag_token.line; }
        return null;
    }

    pub fn tagIndent(*_): Indent? {
        if (tag_token := _.has_tag) { return tag_token.indent; }
        return null;
    }
}

struct ParserParseNodeOptions<const enc: Encoding> {
    type NodeProperties = ParserNodeProperties<enc>;
    current_mapping_indent: Indent? = null,
    explicit_mapping_key: bool = false,
    scanned_tag: Token<enc>? = null,
    scanned_anchor: Token<enc>? = null,
}

enum ScalarResolverKeywords {
    null,
    Null,
    NULL,
    @"~",

    true,
    True,
    TRUE,
    yes,
    Yes,
    YES,
    on,
    On,
    ON,

    false,
    False,
    FALSE,
    no,
    No,
    NO,
    off,
    Off,
    OFF,
}

struct ParserScalarResolverCtx<const enc: Encoding> {
    const chars = enc.chars();
    type String = ParserString<enc>;
    type NodeScalar = ParserNodeScalar<enc>;
    type NodeTag = ParserNodeTag<enc>;
    str_builder: String.Builder,

    resolved: bool = false,
    scalar: NodeScalar?,
    tag: NodeTag,

    parser: *Parser(enc),

    resolved_scalar_len: usize = 0,

    start: Pos,
    line: Line,
    line_indent: Indent,
    multiline: bool = false,

    fn done(*_): Token(enc) {
        const scalar: Token(enc).Scalar = scalar: {
            let scalar_str = _.str_builder.done();

            if (scalar := _.scalar) {
                if (scalar_str.len() == _.resolved_scalar_len) {
                    scalar_str.deinit();
                    break :scalar {
                        style: { plain: _.multiline },
                        data: scalar,
                    };
                }
                // the first characters resolved to something
                // but there were more characters afterwards
            }

            break :scalar {
                style: { plain: _.multiline },
                data: { string: scalar_str },
            };
        };

        return Token(enc).scalar({
            start: _.start,
            indent: _.line_indent,
            line: _.line,
            resolved: scalar,
        });
    }

    fn checkAppend(*_) {
        if (_.str_builder.len() == 0) {
            _.line_indent = _.parser.line_indent;
            _.line = _.parser.line;
        } else if (_.line != _.parser.line) {
            _.multiline = true;
        }
    }

    fn appendSource(*_, unit: enc.unit(), pos: Pos): OOM!void {
        _.checkAppend();
        _.str_builder.appendSource(unit, pos)?;
    }

    fn appendSourceWhitespace(*_, unit: enc.unit(), pos: Pos): OOM!void {
        _.str_builder.appendSourceWhitespace(unit, pos)?;
    }

    fn appendSourceSlice(*_, off: Pos, end: Pos): OOM!void {
        _.checkAppend();
        _.str_builder.appendSourceSlice(off, end)?;
    }

    // may or may not contain whitespace
    fn appendUnknownSourceSlice(*_, off: Pos, end: Pos): OOM!void {
        let _pos: usize = off.cast();
        const end_pos: usize = end.cast();
        while (_pos < end_pos) : (_pos += 1) {
            const pos: Pos = Pos.@"from"(_pos);
            const unit = _.parser.input[pos.cast()];
            switch (unit) {
                ' ',
                '\t',
                '\r',
                '\n'
                => {
                    _.str_builder.appendSourceWhitespace(unit, pos)?;
                },
                else => {
                    _.checkAppend();
                    _.str_builder.appendSource(unit, pos)?;
                },
            }
        }
    }

    fn append(*_, unit: enc.unit()): OOM!void {
        _.checkAppend();
        _.str_builder.append(unit)?;
    }

    fn appendWhitespace(*_, unit: enc.unit()): OOM!void {
        _.str_builder.appendWhitespace(unit)?;
    }

    fn appendSlice(*_, str: []const enc.unit()): OOM!void {
        _.checkAppend();
        _.str_builder.appendSlice(str)?;
    }

    fn appendNTimes(*_, unit: enc.unit(), n: usize): OOM!void {
        if (n == 0) {
            return;
        }
        _.checkAppend();
        _.str_builder.appendNTimes(unit, n)?;
    }

    fn appendWhitespaceNTimes(*_, unit: enc.unit(), n: usize): OOM!void {
        if (n == 0) {
            return;
        }

        _.str_builder.appendWhitespaceNTimes(unit, n)?;
    }

    const Keywords = ScalarResolverKeywords;

    const ResolveError = error{
        OutOfMemory,
        // ScalarTypeMismatch,
    };

    pub static fn resolve(
        ctx: *@This(),
        scalar: NodeScalar,
        off: Pos,
        text: []const enc.unit(),
    ): ResolveError!void {
        ctx.str_builder.appendExpectedSourceSlice(off, off.add(text.len), text)?;

        ctx.resolved = true;

        switch (ctx.tag) {
            .none => {
                ctx.resolved_scalar_len = ctx.str_builder.len();
                ctx.scalar = scalar;
            },
            .non_specific => {
                // always becomes string
            },
            .bool => {
                if (scalar == .boolean) {
                    ctx.resolved_scalar_len = ctx.str_builder.len();
                    ctx.scalar = scalar;
                }
                // return error.ScalarTypeMismatch;
            },
            .int => {
                if (scalar == .number) {
                    ctx.resolved_scalar_len = ctx.str_builder.len();
                    ctx.scalar = scalar;
                }
                // return error.ScalarTypeMismatch;
            },
            .float => {
                if (scalar == .number) {
                    ctx.resolved_scalar_len = ctx.str_builder.len();
                    ctx.scalar = scalar;
                }
                // return error.ScalarTypeMismatch;
            },
            .null => {
                if (scalar == .null) {
                    ctx.resolved_scalar_len = ctx.str_builder.len();
                    ctx.scalar = scalar;
                }
                // return error.ScalarTypeMismatch;
            },
            .str => {
                // always becomes string
            },

            .verbatim,
            .unknown
            => {
                // also always becomes a string
            },
        }
    }

    pub static fn tryResolveNumber(
        ctx: *@This(),
        parser: *Parser(enc),
        first_char: ScalarFirstChar,
    ): ResolveError!void {
        const nan = std.math.nan(f64);
        const inf = std.math.inf(f64);

        switch (first_char) {
            .dot => {
                switch (parser.next()) {
                    'n' => {
                        const n_start = parser.pos;
                        parser.inc(1);
                        if (parser.remainStartsWith("an")) {
                            ctx.resolve({ number: nan }, n_start, "nan")?;
                            parser.inc(2);
                            return;
                        }
                        ctx.appendSource('n', n_start)?;
                        return;
                    },
                    'N' => {
                        const n_start = parser.pos;
                        parser.inc(1);
                        if (parser.remainStartsWith("aN")) {
                            ctx.resolve({ number: nan }, n_start, "NaN")?;
                            parser.inc(2);
                            return;
                        }
                        if (parser.remainStartsWith("AN")) {
                            ctx.resolve({ number: nan }, n_start, "NAN")?;
                            parser.inc(2);
                            return;
                        }
                        ctx.appendSource('N', n_start)?;
                        return;
                    },
                    'i' => {
                        const i_start = parser.pos;
                        parser.inc(1);
                        if (parser.remainStartsWith("nf")) {
                            ctx.resolve({ number: inf }, i_start, "inf")?;
                            parser.inc(2);
                            return;
                        }
                        ctx.appendSource('i', i_start)?;
                        return;
                    },
                    'I' => {
                        const i_start = parser.pos;
                        parser.inc(1);
                        if (parser.remainStartsWith("nf")) {
                            ctx.resolve({ number: inf }, i_start, "Inf")?;
                            parser.inc(2);
                            return;
                        }
                        if (parser.remainStartsWith("NF")) {
                            ctx.resolve({ number: inf }, i_start, "INF")?;
                            parser.inc(2);
                            return;
                        }
                        ctx.appendSource('I', i_start)?;
                        return;
                    },
                    else => {},
                }
            },
            .negative, .positive => {
                if (parser.next() == '.' && parser.peek(1) == 'i' || parser.peek(1) == 'I') {
                    ctx.appendSource('.', parser.pos)?;
                    parser.inc(1);
                    switch (parser.next()) {
                        'i' => {
                            const i_start = parser.pos;
                            parser.inc(1);
                            if (parser.remainStartsWith("nf")) {
                                ctx.resolve(
                                    { number: if (first_char == .negative) -inf else inf },
                                    i_start,
                                    "inf",
                                )?;
                                parser.inc(2);
                                return;
                            }
                            ctx.appendSource('i', i_start)?;
                            return;
                        },
                        'I' => {
                            const i_start = parser.pos;
                            parser.inc(1);
                            if (parser.remainStartsWith("nf")) {
                                ctx.resolve(
                                    { number: if (first_char == .negative) -inf else inf },
                                    i_start,
                                    "Inf",
                                )?;
                                parser.inc(2);
                                return;
                            }
                            if (parser.remainStartsWith("NF")) {
                                ctx.resolve(
                                    { number: if (first_char == .negative) -inf else inf },
                                    i_start,
                                    "INF",
                                )?;
                                parser.inc(2);
                                return;
                            }
                            ctx.appendSource('I', i_start)?;
                            return;
                        },
                        else => {
                            return;
                        },
                    }
                }
            },
            .other => {},
        }

        const start = parser.pos;

        let has_digits = false;
        let has_dot = false;
        let has_e = false;
        let e_digits = false;
        let e_sign = false;
        let is_hex = false;
        let is_oct = false;

        if (first_char != .negative && first_char != .positive) {
            const c0 = parser.next();
            if (c0 == '0' && parser.peek(1) == 'x') {
                is_hex = true;
                parser.inc(2);
            } else if (c0 == '0' && parser.peek(1) == 'o') {
                is_oct = true;
                parser.inc(2);
            } else if (c0 == '.') {
                has_dot = true;
                parser.inc(1);
            } else if (c0 >= '0' && c0 <= '9') {
                has_digits = true;
                parser.inc(1);
            } else {
                parser.inc(1);
            }
        }

        let end: Pos = parser.pos;
        let valid: bool = false;
        let end_ch = parser.next();
        end_sw: while (true) {
            switch (end_ch) {

            // can only be valid if it ends on:
            // - ' '
            // - '\t'
            // - eof
            // - '\n'
            // - '\r'
            // - ':'
            ' ',
            '\t',
            0,
            '\n',
            '\r',
            ':'
            => {
                end = parser.pos;
                valid = has_digits && (!has_e || e_digits);
                break :end_sw;
            },

            ',',
            ']',
            '}'
            => {
                switch (parser.context.get()) {
                    // it's valid for ',' ']' '}' to end the scalar
                    // in flow context
                    .flow_in,
                    .flow_key
                    => {
                        end = parser.pos;
                        valid = has_digits && (!has_e || e_digits);
                        break :end_sw;
                    },

                    .block_in,
                    .block_out
                    => { end = parser.pos; valid = false; break :end_sw; },
                }
            },

            '0'..='9' => |c| {
                if (is_oct && c > '7') {
                    end = parser.pos; valid = false; break :end_sw;
                }
                if (has_e) {
                    e_digits = true;
                } else {
                    has_digits = true;
                }
                parser.inc(1);
                end_ch = parser.next(); continue :end_sw;
            },

            'e',
            'E'
            => {
                if (is_hex) {
                    has_digits = true;
                    parser.inc(1);
                    end_ch = parser.next(); continue :end_sw;
                }
                if (has_e || !has_digits) {
                    end = parser.pos; valid = false; break :end_sw;
                }
                has_e = true;
                parser.inc(1);
                end_ch = parser.next(); continue :end_sw;
            },

            'a'..='d',
            'f',
            'A'..='D',
            'F'
            => {
                if (!is_hex) {
                    end = parser.pos; valid = false; break :end_sw;
                }
                has_digits = true;
                parser.inc(1);
                end_ch = parser.next(); continue :end_sw;
            },

            '.' => {
                if (is_hex || is_oct || has_dot || has_e) {
                    end = parser.pos; valid = false; break :end_sw;
                }
                has_dot = true;
                parser.inc(1);
                end_ch = parser.next(); continue :end_sw;
            },

            '+',
            '-'
            => {
                if (!has_e || e_sign || e_digits) {
                    end = parser.pos; valid = false; break :end_sw;
                }
                e_sign = true;
                parser.inc(1);
                end_ch = parser.next(); continue :end_sw;
            },
            else => {
                end = parser.pos; valid = false; break :end_sw;
            },
            }
        }

        ctx.appendUnknownSourceSlice(start, end)?;

        if (!valid) {
            return;
        }

        let scalar: NodeScalar = scalar: {
            if (is_hex || is_oct) {
                const unsigned = std.fmt.parseUnsigned(u64, parser.slice(start, end), 0) catch {
                    return;
                };
                break :scalar { number: @floatFromInt(unsigned) };
            }
            const float = bun.jsc.wtf.parseDouble(parser.slice(start, end)) catch {
                return;
            };

            break :scalar { number: float };
        };

        ctx.resolved = true;

        switch (ctx.tag) {
            .none,
            .float,
            .int
            => {
                ctx.resolved_scalar_len = ctx.str_builder.len();
                if (first_char == .negative) {
                    const cur_number = scalar.number;
                    scalar.number = -cur_number;
                }
                ctx.scalar = scalar;
            },
            else => {},
        }
    }
}

struct ParserLiteralScalarCtx<const enc: Encoding> {
    type String = ParserString<enc>;
    chomp: Chomp,
    leading_newlines: usize,
    text: std.array_list.Managed(enc.unit()),
    start: Pos,
    content_indent: Indent,
    max_leading_indent: Indent,
    line: Line,
    folded: bool,
    explicit_indent: bool,
    prev_more_indented: bool,
    cur_more_indented: bool,

    fn done(*_): OOM!Token(enc) {
        if (_.text.items.len != 0 && _.leading_newlines == 0) {
            _.leading_newlines = 1;
        }
        switch (_.chomp) {
            .keep => {
                _.text.appendNTimes('\n', _.leading_newlines)?;
            },
            .clip => {
                if (_.text.items.len != 0) {
                    _.text.append('\n')?;
                }
            },
            .strip => {
                // no trailing newlines
            },
        }

        return Token(enc).scalar({
            start: _.start,
            indent: _.content_indent,
            line: _.line,
            resolved: {
                data: { string: { list: _.text } },
                style: .block,
            },
        });
    }

    const AppendError = OOM || error{UnexpectedCharacter};

    fn append(*_, c: enc.unit()): AppendError!void {
        if (_.text.items.len == 0) {
            if (!_.explicit_indent && _.content_indent.isLessThan(_.max_leading_indent)) {
                return error.UnexpectedCharacter;
            }
            _.text.ensureUnusedCapacity(_.leading_newlines + 1)?;
            _.text.appendNTimesAssumeCapacity('\n', _.leading_newlines);
            _.text.appendAssumeCapacity(c);
            _.leading_newlines = 0;
            _.prev_more_indented = _.cur_more_indented;
            return;
        }
        if (_.leading_newlines == 0) {
            _.text.append(c)?;
            return;
        }
        if (_.folded && !_.prev_more_indented && !_.cur_more_indented) {
            if (_.leading_newlines == 1) {
                _.text.append(' ')?;
            } else {
                _.text.ensureUnusedCapacity(_.leading_newlines)?;
                _.text.appendNTimesAssumeCapacity('\n', _.leading_newlines - 1);
            }
        } else {
            _.text.ensureUnusedCapacity(_.leading_newlines + 1)?;
            _.text.appendNTimesAssumeCapacity('\n', _.leading_newlines);
        }
        _.text.appendAssumeCapacity(c);
        _.leading_newlines = 0;
        _.prev_more_indented = _.cur_more_indented;
    }
}

enum ParserEscape: u8 {
    x = 2,
    u = 4,
    U = 8,

    pub fn characters(comptime escape: @This()): u8 {
        return @intFromEnum(escape);
    }

    pub fn cp(): type {
        return switch (_) {
            .x => u8,
            .u => u16,
            .U => u32,
        };
    }
}

struct ParserScanOptions<const enc: Encoding> {
    type NodeTag = ParserNodeTag<enc>;
    /// Used by compact sequences. We need to add
    /// the parent indentation
    /// ```
    /// - - - - one # indent = 4 + 2
    ///       - two
    /// ```
    additional_parent_indent: Indent? = null,

    /// If a scalar is scanned, this tag might be used.
    tag: NodeTag = .none,

    /// The scanner only counts indentation after a newline
    /// (or in compact collections). First scan needs to
    /// count indentation.
    first_scan: bool = false,

    outside_context: bool = false,
}

struct ParserScanCtx<const enc: Encoding> {
    const chars = enc.chars();
    const ScanError = Parser(enc).ScanError;
    parser: *Parser(enc),

    count_indentation: bool,
    in_indent_position: bool,
    additional_parent_indent: Indent?,

    fn scanWhitespace(*_, comptime ws: enc.unit()): ScanError!enc.unit() {
        const parser = _.parser;

        switch (ws) {
            '\r' => {
                if (parser.peek(1) == '\n') {
                    parser.inc(1);
                }

                return '\n';
            },
            '\n' => {
                _.count_indentation = true;
                _.in_indent_position = true;
                _.additional_parent_indent = null;

                parser.newline();
                parser.inc(1);
                return parser.next();
            },
            ' ' => {
                let total: usize = 1;
                parser.inc(1);

                while (parser.next() == ' ') {
                    parser.inc(1);
                    total += 1;
                }

                if (_.count_indentation) {
                    let parent_indent: usize = 0;
                    if (additional := _.additional_parent_indent) { parent_indent = additional.cast(); }
                    parser.line_indent = Indent.@"from"(total + parent_indent);
                }

                _.count_indentation = false;

                return parser.next();
            },
            '\t' => {
                if (_.count_indentation && _.additional_parent_indent == null && _.parser.context.get() == .block_in) {
                    return error.TabIndentation;
                }
                if (_.in_indent_position) {
                    _.parser.tab_after_indent = true;
                    _.additional_parent_indent = null;
                }
                _.count_indentation = false;
                parser.inc(1);
                return parser.next();
            },
            else => @compileError("unexpected character"),
        }
    }
}

pub struct ParserStringRangeStart<const enc: Encoding> {
    type String = ParserString<enc>;
    off: Pos,
    parser: *const Parser(enc),

    fn end(): ParserStringRange<enc> {
        return {
            off: _.off,
            end: _.parser.pos,
        };
    }
}

pub struct ParserStringRange<const enc: Encoding> {
    type String = ParserString<enc>;
    off: Pos,
    end: Pos,

    pub type Start = ParserStringRangeStart<enc>;

    fn isEmpty(): bool {
        return _.off == _.end;
    }

    fn len(): usize {
        return _.end.cast() - _.off.cast();
    }

    pub fn slice(input: []const enc.unit()): []const enc.unit() {
        return input[_.off.cast().._.end.cast()];
    }
}

pub struct ParserStringBuilder<const enc: Encoding> {
    type String = ParserString<enc>;
    parser: *Parser(enc),
    str: String,

    fn appendSource(*_, unit: enc.unit(), pos: Pos): OOM!void {
        _.drainWhitespace()?;

        if (comptime Environment.ci_assert) {
            const actual = _.parser.input[pos.cast()];
            bun.assert(actual == unit);
        }
        switch (_.str) {
            .range => |*range| {
                if (range.isEmpty()) {
                    range.off = pos;
                    range.end = pos;
                }

                bun.assert(range.end == pos);

                range.end = pos.add(1);
            },
            .list => |*list| {
                list.append(unit)?;
            },
        }
    }

    fn drainWhitespace(*_): OOM!void {
        const parser = _.parser;
        defer parser.whitespace_buf.clearRetainingCapacity();

        for (ws of parser.whitespace_buf.items) {
            switch (ws) {
                .source => |source| {
                    if (comptime Environment.ci_assert) {
                        const actual = _.parser.input[source.pos.cast()];
                        bun.assert(actual == source.unit);
                    }

                    switch (_.str) {
                        .range => |*range| {
                            if (range.isEmpty()) {
                                range.off = source.pos;
                                range.end = source.pos;
                            }

                            bun.assert(range.end == source.pos);

                            range.end = source.pos.add(1);
                        },
                        .list => |*list| {
                            list.append(source.unit)?;
                        },
                    }
                },
                .new => |unit| {
                    switch (_.str) {
                        .range => |range| {
                            let list: std.array_list.Managed(enc.unit()) = .initCapacity(parser.allocator, range.len() + 1)?;
                            list.appendSliceAssumeCapacity(range.slice(parser.input));
                            list.appendAssumeCapacity(unit);
                            _.str = { list: list };
                        },
                        .list => |*list| {
                            list.append(unit)?;
                        },
                    }
                },
            }
        }
    }

    fn appendSourceWhitespace(*_, unit: enc.unit(), pos: Pos): OOM!void {
        _.parser.whitespace_buf.append({ source: { unit: unit, pos: pos } })?;
    }

    fn appendWhitespace(*_, unit: enc.unit()): OOM!void {
        _.parser.whitespace_buf.append({ new: unit })?;
    }

    fn appendWhitespaceNTimes(*_, unit: enc.unit(), n: usize): OOM!void {
        _.parser.whitespace_buf.appendNTimes({ new: unit }, n)?;
    }

    fn appendSourceSlice(*_, off: Pos, end: Pos): OOM!void {
        _.drainWhitespace()?;
        switch (_.str) {
            .range => |*range| {
                if (range.isEmpty()) {
                    range.off = off;
                    range.end = off;
                }

                bun.assert(range.end == off);

                range.end = end;
            },
            .list => |*list| {
                list.appendSlice(_.parser.slice(off, end))?;
            },
        }
    }

    fn appendExpectedSourceSlice(*_, off: Pos, end: Pos, expected: []const enc.unit()): OOM!void {
        _.drainWhitespace()?;

        if (comptime Environment.ci_assert) {
            const actual = _.parser.slice(off, end);
            bun.assert(std.mem.eql(enc.unit(), actual, expected));
        }

        switch (_.str) {
            .range => |*range| {
                if (range.isEmpty()) {
                    range.off = off;
                    range.end = off;
                }

                bun.assert(range.end == off);

                range.end = end;
            },
            .list => |*list| {
                list.appendSlice(_.parser.slice(off, end))?;
            },
        }
    }

    fn append(*_, unit: enc.unit()): OOM!void {
        _.drainWhitespace()?;

        const parser = _.parser;

        switch (_.str) {
            .range => |range| {
                let list: std.array_list.Managed(enc.unit()) = .initCapacity(parser.allocator, range.len() + 1)?;
                list.appendSliceAssumeCapacity(range.slice(parser.input));
                list.appendAssumeCapacity(unit);
                _.str = { list: list };
            },
            .list => |*list| {
                list.append(unit)?;
            },
        }
    }

    fn appendSlice(*_, str: []const enc.unit()): OOM!void {
        if (str.len == 0) {
            return;
        }

        _.drainWhitespace()?;

        const parser = _.parser;

        switch (_.str) {
            .range => |range| {
                let list: std.array_list.Managed(enc.unit()) = .initCapacity(parser.allocator, range.len() + str.len)?;
                list.appendSliceAssumeCapacity(_.str.range.slice(parser.input));
                list.appendSliceAssumeCapacity(str);
                _.str = { list: list };
            },
            .list => |*list| {
                list.appendSlice(str)?;
            },
        }
    }

    fn appendNTimes(*_, unit: enc.unit(), n: usize): OOM!void {
        if (n == 0) {
            return;
        }

        _.drainWhitespace()?;

        const parser = _.parser;

        switch (_.str) {
            .range => |range| {
                let list: std.array_list.Managed(enc.unit()) = .initCapacity(parser.allocator, range.len() + n)?;
                list.appendSliceAssumeCapacity(_.str.range.slice(parser.input));
                list.appendNTimesAssumeCapacity(unit, n);
                _.str = { list: list };
            },
            .list => |*list| {
                list.appendNTimes(unit, n)?;
            },
        }
    }

    fn len(): usize {
        return _.str.len();
    }

    fn done(*_): String {
        _.parser.whitespace_buf.clearRetainingCapacity();
        return _.str;
    }
}

pub union(enum) ParserString<const enc: Encoding> {
    range: ParserStringRange<enc>,
    list: std.array_list.Managed(enc.unit()),

    static fn init(data: auto): ParserString<enc> {
        if (comptime @TypeOf(data) == Range) {
            return { range: data };
        }
        if (comptime @TypeOf(data) == std.array_list.Managed(enc.unit())) {
            return { list: data };
        }
        @compileError("unexpected type");
    }

    fn deinit(*_) {
        switch (_.*) {
            .range => {},
            .list => |*list| list.deinit(),
        }
    }

    fn slice(input: []const enc.unit()): []const enc.unit() {
        return switch (_) {
            .range => |range| range.slice(input),
            .list => |list| list.items,
        };
    }

    fn len(): usize {
        return switch (_) {
            .range => |*range| range.len(),
            .list => |*list| list.items.len,
        };
    }

    fn isEmpty(): bool {
        return switch (_) {
            .range => |*range| range.isEmpty(),
            .list => |*list| list.items.len == 0,
        };
    }

    pub fn eql(l: *const @This(), r: []const u8, input: []const enc.unit()): bool {
        const l_slice = l.slice(input);
        return std.mem.eql(enc.unit(), l_slice, r);
    }

    pub const Builder = ParserStringBuilder(enc);
    pub const Range = ParserStringRange(enc);
}

pub union(enum) ParserNodeTag<const enc: Encoding> {
    type String = ParserString<enc>;
    /// ''
    none: void,

    /// '!'
    non_specific: void,

    /// '!!bool'
    bool: void,
    /// '!!int'
    int: void,
    /// '!!float'
    float: void,
    /// '!!null'
    null: void,
    /// '!!str'
    str: void,

    /// '!<...>'
    verbatim: String.Range,

    /// '!!unknown'
    unknown: String.Range,

    pub fn resolveNull(loc: logger.Loc): Expr {
        return switch (_) {
            .none,
            .bool,
            .int,
            .float,
            .null,
            .verbatim,
            .unknown
            => .init(E.Null, {}, loc),

            // non-specific tags become seq, map, or str
            .non_specific,
            .str
            => .init(E.String, {}, loc),
        };
    }
}

pub union(enum) ParserNodeScalar<const enc: Encoding> {
    type String = ParserString<enc>;
    null: void,
    boolean: bool,
    number: f64,
    string: String,

    pub fn toExpr(pos: Pos, input: []const enc.unit()): Expr {
        return switch (_) {
            .null => .init(E.Null, {}, pos.loc()),
            .boolean => |value| .init(E.Boolean, { value: value }, pos.loc()),
            .number => |value| .init(E.Number, { value: value }, pos.loc()),
            .string => |value| .init(E.String, { data: value.slice(input) }, pos.loc()),
        };
    }
}

pub union(enum) ParserDirectiveTagHandle<const enc: Encoding> {
    type String = ParserString<enc>;
    /// '!name!'
    named: String.Range,
    /// '!!'
    secondary: void,
    /// '!'
    primary: void,
}

pub union(enum) ParserDirectiveTagPrefix<const enc: Encoding> {
    type String = ParserString<enc>;
    /// c-ns-local-tag-prefix
    /// '!my-prefix'
    local: String.Range,
    /// ns-global-tag-prefix
    /// 'tag:example.com,2000:app/'
    global: String.Range,
}

pub struct ParserDirectiveTag<const enc: Encoding> {
    handle: Handle,
    prefix: Prefix,

    pub type Handle = ParserDirectiveTagHandle<enc>;

    pub type Prefix = ParserDirectiveTagPrefix<enc>;
}

pub union(enum) ParserDirective<const enc: Encoding> {
    type String = ParserString<enc>;
    yaml: void,
    tag: ParserDirectiveTag<enc>,
    reserved: String.Range,

    /// '%TAG <handle> <prefix>'
    pub type Tag = ParserDirectiveTag<enc>;
}

pub struct ParserDocument<const enc: Encoding> {
    type Directive = ParserDirective<enc>;
    directives: std.array_list.Managed(Directive),
    root: Expr,

    pub fn deinit(*_) {
        _.directives.deinit();
    }
}

pub struct ParserStream<const enc: Encoding> {
    type Document = ParserDocument<enc>;
    docs: std.array_list.Managed(Document),
    input: []const enc.unit(),
}

enum BlockIndentedKind {
    seq_entry,
    map_explicit_key,
    map_value,
}

pub struct Parser<const enc: Encoding> {
    const chars = enc.chars();

    input: []const enc.unit(),

    pos: Pos,
    line_indent: Indent,
    tab_after_indent: bool,
    line: Line,
    token: Token(enc),

    allocator: std.mem.Allocator,

    context: Context.Stack,
    block_indents: Indent.Stack,

    explicit_document_start_line: Line?,

    // anchors: Anchors,
    anchors: bun.StringHashMap(Expr),
    // aliases: PendingAliases,

    tag_handles: bun.StringHashMap(void),

    // const PendingAliases = struct {
    //     list: std.array_list.Managed(State),

    //     const State = struct {
    //         name: String.Range,
    //         index: usize,
    //         prop: enum { key, value },
    //         collection_node: *Node,
    //     };
    // };

    whitespace_buf: std.array_list.Managed(Whitespace),

    stack_check: bun.StackCheck,

    type Whitespace = ParserWhitespace<enc>;

    pub fn init(allocator: std.mem.Allocator, input: []const enc.unit()): @This() {
        return {
            input: input,
            allocator: allocator,
            pos: Pos.@"from"(0),
            line_indent: .none,
            tab_after_indent: false,
            line: Line.@"from"(1),
            token: Token(enc).eof({ start: Pos.@"from"(0), indent: .none, line: Line.@"from"(1) }),
            // .key = null,
            // .literal = null,
            context: .init(allocator),
            block_indents: .init(allocator),
            explicit_document_start_line: null,
            // .anchors = .{ .map = .init(allocator) },
            anchors: .init(allocator),
            // .aliases = .{ .list = .init(allocator) },
            tag_handles: .init(allocator),
            whitespace_buf: .init(allocator),
            stack_check: .init(),
        };
    }

    fn deinit(*_) {
        _.context.list.deinit();
        _.block_indents.list.deinit();
        _.anchors.deinit();
        _.tag_handles.deinit();
        _.whitespace_buf.deinit();
        // std.debug.assert(_.future == null);
    }

    pub type ParseResult = ParserParseResult<enc>;

    fn unexpectedToken() UnexpectedTokenError {
        return error.UnexpectedToken;
    }

    fn parse(*_): ParseError!Stream {
        _.scan({ first_scan: true })?;

        return _.parseStream()?;
    }

    const ParseError = error{
        OutOfMemory,
        UnexpectedToken,
        UnexpectedEof,
        InvalidDirective,
        UnexpectedCharacter,
        TabIndentation,
        UnresolvedTagHandle,
        UnresolvedAlias,
        MultilineImplicitKey,
        MultipleAnchors,
        MultipleTags,
        UnexpectedDocumentStart,
        UnexpectedDocumentEnd,
        MultipleYamlDirectives,
        InvalidIndentation,
        StackOverflow,
        // ScalarTypeMismatch,

        // InvalidSyntax,
        // UnexpectedDirective,
    };

    fn parseStream(*_): ParseError!Stream {
        let docs: std.array_list.Managed(Document) = .init(_.allocator);
        errdefer docs.deinit();

        // we want one null document if eof, not zero documents.
        let first = true;
        while (first || _.token.data != .eof) {
            first = false;

            const doc = _.parseDocument()?;

            docs.append(doc)?;
        }

        return { docs: docs, input: _.input };
    }

    fn peek(comptime n: usize): enc.unit() {
        const pos = _.pos.add(n);
        if (pos.isLessThan(_.input.len)) {
            return _.input[pos.cast()];
        }

        return 0;
    }

    fn inc(*_, n: usize) {
        _.pos = Pos.@"from"(@min(_.pos.cast() + n, _.input.len));
    }

    fn newline(*_) {
        _.line_indent = .none;
        _.tab_after_indent = false;
        _.line.inc(1);
    }

    fn slice(off: Pos, end: Pos): []const enc.unit() {
        return _.input[off.cast()..end.cast()];
    }

    fn remain(): []const enc.unit() {
        return _.input[_.pos.cast()..];
    }

    fn remainStartsWith(cs: []const enc.unit()): bool {
        return std.mem.startsWith(enc.unit(), _.remain(), cs);
    }

    fn remainStartsWithChar(char: enc.unit()): bool {
        const r = _.remain();
        return r.len != 0 && r[0] == char;
    }

    fn remainStartsWithAny(cs: []const enc.unit()): bool {
        const r = _.remain();
        if (r.len == 0) {
            return false;
        }

        return std.mem.indexOfScalar(enc.unit(), cs, r[0]) != null;
    }

    // this looks different from node parsing code because directives
    // exist mostly outside of the normal token scanning logic. they are
    // not part of the root expression.

    // TODO: move most of this into `scan()`
    fn parseDirective(*_): ParseError!Directive {
        if (_.token.indent != .none) {
            return error.InvalidDirective;
        }

        // yaml directive
        if (_.remainStartsWith(enc.literal("YAML")) && _.isSWhiteAt(4)) {
            _.inc(4);

            _.trySkipSWhite()?;
            _.trySkipNsDecDigits()?;
            _.trySkipChar('.')?;
            _.trySkipNsDecDigits()?;

            // s-l-comments
            _.trySkipToNewLine()?;

            return Directive.yaml;
        }

        // tag directive
        if (_.remainStartsWith(enc.literal("TAG")) && _.isSWhiteAt(3)) {
            _.inc(3);

            _.trySkipSWhite()?;
            _.trySkipChar('!')?;

            // primary tag handle
            if (_.isSWhite()) {
                _.skipSWhite();
                const prefix = _.parseDirectiveTagPrefix()?;
                _.trySkipToNewLine()?;
                return { tag: { handle: .primary, prefix: prefix } };
            }

            // secondary tag handle
            if (_.isChar('!')) {
                _.inc(1);
                _.trySkipSWhite()?;
                const prefix = _.parseDirectiveTagPrefix()?;
                _.trySkipToNewLine()?;
                return { tag: { handle: .secondary, prefix: prefix } };
            }

            // named tag handle
            let range = _.stringRange();
            _.trySkipNsWordChars()?;
            const handle = range.end();
            _.trySkipChar('!')?;
            _.trySkipSWhite()?;

            const unit_val: void = undefined;
            _.tag_handles.put(handle.slice(_.input), unit_val)?;

            const prefix = _.parseDirectiveTagPrefix()?;
            _.trySkipToNewLine()?;
            return { tag: { handle: { named: handle }, prefix: prefix } };
        }

        // reserved directive
        let range = _.stringRange();
        _.trySkipNsChars()?;
        const reserved = range.end();

        _.skipSWhite();

        while (_.isNsChar()) {
            _.skipNsChars();
            _.skipSWhite();
        }

        _.trySkipToNewLine()?;

        return { reserved: reserved };
    }

    fn parseDirectiveTagPrefix(*_): ParseError!Directive.Tag.Prefix {
        // local tag prefix
        if (_.isChar('!')) {
            _.inc(1);
            let range = _.stringRange();
            _.skipNsUriChars();
            return { local: range.end() };
        }

        // global tag prefix
        if (char_len := _.isNsTagChar()) {
            let range = _.stringRange();
            _.inc(char_len);
            _.skipNsUriChars();
            return { global: range.end() };
        }

        return error.InvalidDirective;
    }

    fn parseDocument(*_): ParseError!Document {
        let directives: std.array_list.Managed(Directive) = .init(_.allocator);
        errdefer directives.deinit();

        _.anchors.clearRetainingCapacity();
        _.tag_handles.clearRetainingCapacity();

        let has_yaml_directive = false;

        while (_.token.data == .directive) {
            const directive = _.parseDirective()?;
            if (directive == .yaml) {
                if (has_yaml_directive) {
                    return error.MultipleYamlDirectives;
                }
                has_yaml_directive = true;
            }
            directives.append(directive)?;
            _.scan({})?;
        }

        _.explicit_document_start_line = null;

        if (_.token.data == .document_start) {
            _.explicit_document_start_line = _.token.line;
            _.scan({})?;
        } else if (directives.items.len > 0) {
            // if there's directives they must end with '---'
            return error.UnexpectedToken;
        }

        const root = _.parseNode({})?;

        // If document_start it needs to create a new document.
        // If document_end, consume as many as possible. They should
        // not create new documents.
        switch (_.token.data) {
            .eof => {},
            .document_start => {},
            .document_end => {
                const document_end_line = _.token.line;
                _.scan({})?;

                // consume all bare documents
                while (_.token.data == .document_end) {
                    _.scan({})?;
                }

                if (_.token.line == document_end_line) {
                    return error.UnexpectedToken;
                }
            },
            else => {
                return error.UnexpectedToken;
            },
        }

        return { root: root, directives: directives };
    }

    fn parseFlowSequence(*_): ParseError!Expr {
        const sequence_start = _.token.start;
        const sequence_indent = _.token.indent;
        * = sequence_indent;
        const sequence_line = _.line;
        * = sequence_line;

        let seq: std.array_list.Managed(Expr) = .init(_.allocator);
        errdefer seq.deinit();

        {
            _.context.set(.flow_in)?;
            defer _.context.unset(.flow_in);

            _.scan({})?;
            while (_.token.data != .sequence_end) {
                const item = _.parseNode({})?;
                seq.append(item)?;

                if (_.token.data == .sequence_end) {
                    break;
                }

                if (_.token.data != .collect_entry) {
                    return error.UnexpectedToken;
                }

                _.scan({})?;
            }
        }

        _.scan({})?;

        return Expr.init(E.Array, E.Array{ items: bun.BabyList(Expr).moveFromList(&seq) }, sequence_start.loc());
    }

    fn parseFlowMapping(*_): ParseError!Expr {
        const mapping_start = _.token.start;
        const mapping_indent = _.token.indent;
        * = mapping_indent;
        const mapping_line = _.token.line;
        * = mapping_line;

        let props: MappingProps = .init(_.allocator);

        {
            _.context.set(.flow_in)?;
            defer _.context.unset(.flow_in);

            {
                _.context.set(.flow_key)?;
                defer _.context.unset(.flow_key);
                _.scan({})?;
            }

            while (_.token.data != .mapping_end) {
                const key = key: {
                    _.context.set(.flow_key)?;
                    defer _.context.unset(.flow_key);
                    break :key _.parseNode({})?;
                };

                switch (_.token.data) {
                    .collect_entry => {
                        const value: Expr = .init(E.Null, {}, _.token.start.loc());
                        props.append({
                            key: key,
                            value: value,
                        })?;

                        _.context.set(.flow_key)?;
                        defer _.context.unset(.flow_key);
                        _.scan({})?;
                        continue;
                    },
                    .mapping_end => {
                        const value: Expr = .init(E.Null, {}, _.token.start.loc());
                        props.append({
                            key: key,
                            value: value,
                        })?;
                        continue;
                    },
                    .mapping_value => {},
                    else => {
                        return error.UnexpectedToken;
                    },
                }

                _.scan({})?;

                if (_.token.data == .mapping_end ||
                    _.token.data == .collect_entry)
                {
                    const value: Expr = .init(E.Null, {}, _.token.start.loc());
                    props.append({
                        key: key,
                        value: value,
                    })?;
                } else {
                    // [147] the value is ns-flow-node; threading the value's
                    // own indent as current_mapping_indent makes the Scalar
                    // arm's cmi==scalar_indent check return the bare scalar
                    // instead of consuming a trailing `: …` as a nested
                    // mapping (`{a: b: c}`).
                    const value = _.parseNode({ current_mapping_indent: _.token.indent })?;
                    props.appendMaybeMerge(key, value)?;
                }

                // [140] ns-s-flow-map-entries: after an entry, only `,` or `}`.
                switch (_.token.data) {
                    .collect_entry => {
                        _.context.set(.flow_key)?;
                        defer _.context.unset(.flow_key);
                        _.scan({})?;
                    },
                    .mapping_end => {},
                    else => return error.UnexpectedToken,
                }
            }
        }

        _.scan({})?;

        return Expr.init(E.Object, { properties: props.moveList() }, mapping_start.loc());
    }

    fn parseBlockSequence(*_): ParseError!Expr {
        const sequence_start = _.token.start;
        const sequence_indent = _.token.indent;
        // const sequence_line = _.token.line;

        _.block_indents.push(sequence_indent)?;
        defer _.block_indents.pop();

        let seq: std.array_list.Managed(Expr) = .init(_.allocator);
        errdefer seq.deinit();

        let prev_line = Line.@"from"(0);

        while (_.token.data == .sequence_entry && _.token.indent == sequence_indent) {
            // [184] each `-` sits at s-indent(n) (spaces only).
            if (_.tab_after_indent) {
                return error.TabIndentation;
            }
            const entry_line = _.token.line;
            const entry_start = _.token.start;

            if (seq.items.len != 0 && prev_line == entry_line) {
                // only the first entry can be another sequence entry on the
                // same line
                break;
            }

            prev_line = entry_line;

            _.scan({ additional_parent_indent: sequence_indent.add(1) })?;

            const item = _.parseBlockIndented(
                sequence_indent,
                entry_line,
                entry_start.add(2),
                .seq_entry,
            )?;

            seq.append(item)?;
        }

        return Expr.init(E.Array, E.Array{ items: bun.BabyList(Expr).moveFromList(&seq) }, sequence_start.loc());
    }

    /// Should only be used with expressions created with the YAML parser. It assumes
    /// only null, boolean, number, string, array, object are possible. It also only
    /// does pointer comparison with arrays and objects (so exponential merges are avoided)

    const MappingProps = ParserMappingProps;

    fn parseBlockMapping(
        *_,
        first_key: Expr,
        mapping_start: Pos,
        mapping_indent: Indent,
        mapping_line: Line,
    ): ParseError!Expr {
        if (explicit_document_start_line := _.explicit_document_start_line) {
            if (mapping_line == explicit_document_start_line) {
                // TODO: more specific error
                return error.UnexpectedToken;
            }
        }

        const pushed_block_indent = _.context.get() != .flow_in && _.context.get() != .flow_key;
        if (pushed_block_indent) {
            _.block_indents.push(mapping_indent)?;
        }
        defer {
            if (pushed_block_indent) {
                _.block_indents.pop();
            }
        }

        let props: MappingProps = .init(_.allocator);
        let first_entry_end_line = mapping_line;

        {
            // try _.context.set(.block_in);
            // defer _.context.unset(.block_in);

            // get the first value

            const mapping_value_start = _.token.start;
            const mapping_value_line = _.token.line;

            const value: Expr = switch (_.token.data) {
                // it's a !!set entry
                .mapping_key => value: {
                    if (_.token.line == mapping_line) {
                        return error.UnexpectedToken;
                    }
                    break :value Expr.init(E.Null, {}, mapping_value_start.loc());
                },
                .mapping_value => value: {
                    first_entry_end_line = mapping_value_line;
                    if (mapping_value_line != mapping_line &&
                        (_.context.get() != .flow_in && _.context.get() != .flow_key) &&
                        (_.token.indent != mapping_indent || _.tab_after_indent))
                    {
                        if (_.token.indent.isLessThan(mapping_indent)) {
                            break :value Expr.init(E.Null, {}, mapping_value_start.loc());
                        }
                        if (_.tab_after_indent) {
                            return error.TabIndentation;
                        }
                        return error.UnexpectedToken;
                    }
                    const parent_indent: Indent? = if (mapping_value_line != mapping_line) mapping_indent.add(1) else null;
                    _.scan({ additional_parent_indent: parent_indent })?;

                    break :value _.parseBlockIndented(
                        mapping_indent,
                        mapping_value_line,
                        mapping_value_start,
                        .map_value,
                    )?;
                },
                else => Expr.init(E.Null, {}, mapping_value_start.loc()),
            };

            props.appendMaybeMerge(first_key, value)?;
        }

        if (_.context.get() == .flow_in) {
            return Expr.init(E.Object, { properties: props.moveList() }, mapping_start.loc());
        }

        _.context.set(.block_in)?;
        defer _.context.unset(.block_in);

        let previous_line = first_entry_end_line;

        while (switch (_.token.data) {
            .eof,
            .document_start,
            .document_end
            => false,
            else => true,
        } && _.token.indent == mapping_indent && _.token.line != previous_line) {
            // [192]/[195] each entry sits at s-indent(n) (spaces only).
            if (_.tab_after_indent) {
                return error.TabIndentation;
            }
            const key_line = _.token.line;
            previous_line = key_line;
            const explicit_key = _.token.data == .mapping_key;

            const key = _.parseNode({ current_mapping_indent: mapping_indent })?;

            switch (_.token.data) {
                .eof
                => {
                    if (explicit_key) {
                        const value: Expr = .init(E.Null, {}, _.pos.loc());
                        props.append({
                            key: key,
                            value: value,
                        })?;
                        continue;
                    }
                    return error.UnexpectedToken;
                },
                .mapping_value => {
                    if (explicit_key) {
                        if (_.token.indent != mapping_indent || _.tab_after_indent) {
                            if (_.token.indent.isLessThan(mapping_indent)) {
                                const value: Expr = .init(E.Null, {}, _.pos.loc());
                                props.append({
                                    key: key,
                                    value: value,
                                })?;
                                continue;
                            }
                            if (_.tab_after_indent) {
                                return error.TabIndentation;
                            }
                            return error.UnexpectedToken;
                        }
                    } else if (key_line != _.token.line) {
                        return error.MultilineImplicitKey;
                    }
                },
                .mapping_key => {},
                else => {
                    if (explicit_key) {
                        const value: Expr = .init(E.Null, {}, _.pos.loc());
                        props.append({
                            key: key,
                            value: value,
                        })?;
                        continue;
                    }
                    return error.UnexpectedToken;
                },
            }

            const mapping_value_line = _.token.line;
            const mapping_value_start = _.token.start;
            if (_.token.data == .mapping_value) {
                previous_line = mapping_value_line;
            }

            const value: Expr = switch (_.token.data) {
                // it's a !!set entry
                .mapping_key => value: {
                    if (_.token.line == key_line) {
                        return error.UnexpectedToken;
                    }
                    break :value Expr.init(E.Null, {}, mapping_value_start.loc());
                },
                else => value: {
                    const parent_indent: Indent? = if (mapping_value_line != key_line) mapping_indent.add(1) else null;
                    _.scan({ additional_parent_indent: parent_indent })?;

                    break :value _.parseBlockIndented(
                        mapping_indent,
                        mapping_value_line,
                        mapping_value_start,
                        .map_value,
                    )?;
                },
            };

            props.appendMaybeMerge(key, value)?;
        }

        return Expr.init(E.Object, { properties: props.moveList() }, mapping_start.loc());
    }

    type NodeProperties = ParserNodeProperties<enc>;

    type ParseNodeOptions = ParserParseNodeOptions<enc>;

    fn propsToENode(*_, tag: Token<enc>?, anchor: Token<enc>?, loc: logger.Loc): ParseError!Expr {
        let resolved_tag: NodeTag = .none;
        if (t := tag) {
            resolved_tag = t.data.tag;
        }
        const e_node = resolved_tag.resolveNull(loc);
        if (a := anchor) {
            _.anchors.put(a.data.anchor.slice(_.input), e_node)?;
        }
        return e_node;
    }

    fn parseBlockIndented(
        *_,
        n: Indent,
        indicator_line: Line,
        indicator_start: Pos,
        kind: BlockIndentedKind,
    ): ParseError!Expr {
        let value_tag: Token<enc>? = null;
        let value_anchor: Token<enc>? = null;
        const in_flow = _.context.get() == .flow_in || _.context.get() == .flow_key;

        while (true) {
            if (!in_flow && _.token.line != indicator_line) {
                const belongs_to_parent = if (_.token.data == .sequence_entry && kind != .seq_entry)
                    _.token.indent.isLessThan(n)
                else
                    _.token.indent.isLessThanOrEqual(n);
                if (belongs_to_parent) {
                    if (value_tag != null) {
                        switch (_.token.data) {
                            .scalar => |s| {
                                switch (s.style) {
                                    .plain => |multiline| {
                                        if (!multiline) {
                                            _.pos = _.token.start;
                                            _.line = _.token.line;
                                            _.line_indent = _.token.indent;
                                            _.scan({})?;
                                        }
                                    },
                                    else => {},
                                }
                            },
                            else => {},
                        }
                    }
                    return _.propsToENode(value_tag, value_anchor, indicator_start.loc());
                }
            }

            switch (_.token.data) {
                .anchor => {
                    if (value_anchor == null) {
                        value_anchor = _.token;
                    } else {
                        return _.parseNode({
                            current_mapping_indent: n,
                            explicit_mapping_key: kind == .map_explicit_key,
                            scanned_tag: value_tag,
                            scanned_anchor: value_anchor,
                        });
                    }
                },
                .tag => {
                    if (value_tag == null) {
                        value_tag = _.token;
                    } else {
                        return _.parseNode({
                            current_mapping_indent: n,
                            explicit_mapping_key: kind == .map_explicit_key,
                            scanned_tag: value_tag,
                            scanned_anchor: value_anchor,
                        });
                    }
                },
                .sequence_entry, .mapping_key => {
                    if (_.token.line == indicator_line && (_.token.indent.isLessThanOrEqual(n) || _.tab_after_indent)) {
                        if (_.tab_after_indent) {
                            return error.TabIndentation;
                        }
                        return error.UnexpectedToken;
                    }
                    return _.parseNode({
                        current_mapping_indent: n,
                        explicit_mapping_key: kind == .map_explicit_key,
                        scanned_tag: value_tag,
                        scanned_anchor: value_anchor,
                    });
                },
                else => {
                    return _.parseNode({
                        current_mapping_indent: n,
                        explicit_mapping_key: kind == .map_explicit_key,
                        scanned_tag: value_tag,
                        scanned_anchor: value_anchor,
                    });
                },
            }

            let tag: NodeTag = .none;
            if (t := value_tag) {
                tag = t.data.tag;
            }
            _.scan({ tag: tag })?;
        }
    }

    fn parseNode(*_, opts: ParseNodeOptions): ParseError!Expr {
        if (!_.stack_check.isSafeToRecurse()) {
            bun.throwStackOverflow()?;
        }

        // c-ns-properties
        let node_props: NodeProperties = {};

        if (tag := opts.scanned_tag) {
            node_props.setTag(tag)?;
        }

        if (anchor := opts.scanned_anchor) {
            node_props.setAnchor(anchor)?;
        }

        let node: Expr = undefined;
        let node_data = _.token.data;
        node_sw: while (true) {
            switch (node_data) {
            .eof,
            .document_start,
            .document_end
            => {
                node = Expr.init(E.Null, {}, _.token.start.loc()); break :node_sw;
            },

            .anchor => |anchor| {
                * = anchor;
                node_props.setAnchor(_.token)?;

                _.scan({ tag: node_props.tag() })?;

                node_data = _.token.data; continue :node_sw;
            },

            .tag => |tag| {
                node_props.setTag(_.token)?;

                _.scan({ tag: tag })?;

                node_data = _.token.data; continue :node_sw;
            },

            .alias => |alias| {
                const alias_start = _.token.start;
                const alias_indent = _.token.indent;
                const alias_line = _.token.line;
                const alias_tab_after_indent = _.tab_after_indent;

                if (anchor := node_props.has_anchor) {
                    if (anchor.line == alias_line) {
                        return error.UnexpectedToken;
                    }
                }
                if (tag := node_props.has_tag) {
                    if (tag.line == alias_line) {
                        return error.UnexpectedToken;
                    }
                }

                let copy: Expr = undefined;
                if (found_alias := _.anchors.get(alias.slice(_.input))) {
                    copy = found_alias;
                } else {
                    return error.UnresolvedAlias;
                }

                // update position from the anchor node to the alias node.
                copy.loc = alias_start.loc();

                _.scan({})?;

                if (_.token.data == .mapping_value) {
                    if (_.token.indent.isLessThan(alias_indent) ||
                        (opts.explicit_mapping_key && alias_line != _.token.line && _.token.indent.isLessThanOrEqual(alias_indent)))
                    {
                        node = copy; break :node_sw;
                    }
                    if (_.context.get() == .flow_key) {
                        return copy;
                    }
                    if (alias_line != _.token.line && !opts.explicit_mapping_key) {
                        return error.MultilineImplicitKey;
                    }
                    if (alias_tab_after_indent && (_.context.get() == .block_out || _.context.get() == .block_in)) {
                        return error.TabIndentation;
                    }

                    if (current_mapping_indent := opts.current_mapping_indent) {
                        if (current_mapping_indent == alias_indent) {
                            return copy;
                        }
                    }

                    const map = _.parseBlockMapping(
                        copy,
                        alias_start,
                        alias_indent,
                        alias_line,
                    )?;

                    return map;
                }

                node = copy; break :node_sw;
            },

            .sequence_start => {
                const sequence_start = _.token.start;
                const sequence_indent = _.token.indent;
                const sequence_line = _.token.line;
                const sequence_tab_after_indent = _.tab_after_indent;
                const seq = _.parseFlowSequence()?;

                if (_.token.data == .mapping_value) {
                    if (_.token.indent.isLessThan(sequence_indent) ||
                        (opts.explicit_mapping_key && sequence_line != _.token.line && _.token.indent.isLessThanOrEqual(sequence_indent)))
                    {
                        node = seq; break :node_sw;
                    }
                    if (_.context.get() == .flow_key) {
                        node = seq; break :node_sw;
                    }
                    if (sequence_line != _.token.line && !opts.explicit_mapping_key) {
                        return error.MultilineImplicitKey;
                    }
                    if (sequence_tab_after_indent && (_.context.get() == .block_out || _.context.get() == .block_in)) {
                        return error.TabIndentation;
                    }

                    if (current_mapping_indent := opts.current_mapping_indent) {
                        if (current_mapping_indent == sequence_indent) {
                            node = seq; break :node_sw;
                        }
                    }

                    const implicit_key_anchors = node_props.implicitKeyAnchors(sequence_line)?;

                    if (key_anchor := implicit_key_anchors.key_anchor) {
                        _.anchors.put(key_anchor.slice(_.input), seq)?;
                    }

                    const map = _.parseBlockMapping(
                        seq,
                        sequence_start,
                        sequence_indent,
                        sequence_line,
                    )?;

                    if (mapping_anchor := implicit_key_anchors.mapping_anchor) {
                        _.anchors.put(mapping_anchor.slice(_.input), map)?;
                    }

                    return map;
                }

                node = seq; break :node_sw;
            },
            .collect_entry,
            .sequence_end,
            .mapping_end
            => {
                if (node_props.hasAnchorOrTag()) {
                    node = Expr.init(E.Null, {}, _.pos.loc()); break :node_sw;
                }
                return error.UnexpectedToken;
            },
            .sequence_entry => {
                if (anchor_line := node_props.anchorLine()) {
                    if (anchor_line == _.token.line) {
                        return error.UnexpectedToken;
                    }
                }
                if (tag_line := node_props.tagLine()) {
                    if (tag_line == _.token.line) {
                        return error.UnexpectedToken;
                    }
                }

                node = _.parseBlockSequence()?; break :node_sw;
            },
            .mapping_start => {
                const mapping_start = _.token.start;
                const mapping_indent = _.token.indent;
                const mapping_line = _.token.line;
                const mapping_tab_after_indent = _.tab_after_indent;

                const map = _.parseFlowMapping()?;

                if (_.token.data == .mapping_value) {
                    if (_.token.indent.isLessThan(mapping_indent) ||
                        (opts.explicit_mapping_key && mapping_line != _.token.line && _.token.indent.isLessThanOrEqual(mapping_indent)))
                    {
                        node = map; break :node_sw;
                    }
                    if (_.context.get() == .flow_key) {
                        node = map; break :node_sw;
                    }
                    if (mapping_line != _.token.line && !opts.explicit_mapping_key) {
                        return error.MultilineImplicitKey;
                    }
                    if (mapping_tab_after_indent && (_.context.get() == .block_out || _.context.get() == .block_in)) {
                        return error.TabIndentation;
                    }

                    if (current_mapping_indent := opts.current_mapping_indent) {
                        if (current_mapping_indent == mapping_indent) {
                            node = map; break :node_sw;
                        }
                    }

                    const implicit_key_anchors = node_props.implicitKeyAnchors(mapping_line)?;

                    if (key_anchor := implicit_key_anchors.key_anchor) {
                        _.anchors.put(key_anchor.slice(_.input), map)?;
                    }

                    const parent_map = _.parseBlockMapping(
                        map,
                        mapping_start,
                        mapping_indent,
                        mapping_line,
                    )?;

                    if (mapping_anchor := implicit_key_anchors.mapping_anchor) {
                        _.anchors.put(mapping_anchor.slice(_.input), parent_map)?;
                    }

                    node = parent_map; break :node_sw;
                }
                node = map; break :node_sw;
            },

            .mapping_key => {
                if (_.tab_after_indent) {
                    return error.TabIndentation;
                }
                const mapping_start = _.token.start;
                const mapping_indent = _.token.indent;
                const mapping_line = _.token.line;

                _.block_indents.push(mapping_indent)?;

                const in_flow = _.context.get() == .flow_in || _.context.get() == .flow_key;
                const parent_indent: Indent? = if (in_flow) null else mapping_indent.add(1);
                _.scan({ additional_parent_indent: parent_indent })?;

                const key = _.parseBlockIndented(
                    mapping_indent,
                    mapping_line,
                    mapping_start,
                    .map_explicit_key,
                )?;

                _.block_indents.pop();

                if (current_mapping_indent := opts.current_mapping_indent) {
                    if (current_mapping_indent == mapping_indent) {
                        return key;
                    }
                }

                node = _.parseBlockMapping(
                    key,
                    mapping_start,
                    mapping_indent,
                    mapping_line,
                )?; break :node_sw;
            },
            .mapping_value => {
                if (_.context.get() == .flow_key) {
                    node = Expr.init(E.Null, {}, _.token.start.loc()); break :node_sw;
                }
                if (_.tab_after_indent && _.context.get() != .flow_in) {
                    return error.TabIndentation;
                }
                if (current_mapping_indent := opts.current_mapping_indent) {
                    if (current_mapping_indent == _.token.indent) {
                        node = Expr.init(E.Null, {}, _.token.start.loc()); break :node_sw;
                    }
                }
                const colon_line = _.token.line;
                const key_tag = if (node_props.tagLine() == colon_line) node_props.takeTag() else NodeTag.none;
                const first_key = key_tag.resolveNull(_.token.start.loc());

                const implicit_key_anchors = node_props.implicitKeyAnchors(colon_line)?;
                if (key_anchor := implicit_key_anchors.key_anchor) {
                    _.anchors.put(key_anchor.slice(_.input), first_key)?;
                }

                const mapping = _.parseBlockMapping(
                    first_key,
                    _.token.start,
                    _.token.indent,
                    colon_line,
                )?;

                if (mapping_anchor := implicit_key_anchors.mapping_anchor) {
                    _.anchors.put(mapping_anchor.slice(_.input), mapping)?;
                }
                node_props.has_anchor = null;
                node = mapping; break :node_sw;
            },
            .scalar => |scalar| {
                const scalar_start = _.token.start;
                const scalar_indent = _.token.indent;
                const scalar_line = _.token.line;
                const scalar_tab_after_indent = _.tab_after_indent;

                _.scan({ tag: node_props.tag(), outside_context: true })?;

                if (_.token.data == .mapping_value) {
                    if (_.token.indent.isLessThan(scalar_indent) ||
                        (opts.explicit_mapping_key && scalar_line != _.token.line && _.token.indent.isLessThanOrEqual(scalar_indent)))
                    {
                        node = scalar.data.toExpr(scalar_start, _.input); break :node_sw;
                    }
                    if (scalar_tab_after_indent && (_.context.get() == .block_out || _.context.get() == .block_in)) {
                        return error.TabIndentation;
                    }
                    // this might be the start of a new object with an implicit key
                    //
                    // ```
                    // foo: bar        # yes
                    // ---
                    // {foo: bar}      # no (1)
                    // ---
                    // [foo: bar]      # yes (but can't have more than one prop) (2)
                    // ---
                    // - foo: bar      # yes
                    // ---
                    // [hi]: 123       # yes
                    // ---
                    // one: two        # first property is
                    // three: four     # no, this is another prop in the same object (3)
                    // ---
                    // one:            # yes
                    //   two: three    # and yes (nested object)
                    // ```
                    if (current_mapping_indent := opts.current_mapping_indent) {
                        if (current_mapping_indent == scalar_indent) {
                            // 3
                            node = scalar.data.toExpr(scalar_start, _.input); break :node_sw;
                        }
                    }

                    switch (_.context.get()) {
                        .flow_key => {
                            // 1
                            node = scalar.data.toExpr(scalar_start, _.input); break :node_sw;
                        },
                        // => {
                        //     // 2
                        //     // can be multiline
                        // },
                        .flow_in,
                        .block_out,
                        .block_in
                        => {
                            if (scalar_line != _.token.line && !opts.explicit_mapping_key) {
                                return error.MultilineImplicitKey;
                            }
                            // if (scalar.multiline) {
                            //     // TODO: maybe get rid of multiline and just check
                            //     // `scalar_line != _.token.line`. this will depend
                            //     // on how we decide scalar_line. if that's including
                            //     // whitespace for plain scalars it might not work
                            //     return error.MultilineImplicitKey;
                            // }
                        },
                    }

                    const implicit_key = scalar.data.toExpr(scalar_start, _.input);

                    const implicit_key_anchors = node_props.implicitKeyAnchors(scalar_line)?;

                    if (key_anchor := implicit_key_anchors.key_anchor) {
                        _.anchors.put(key_anchor.slice(_.input), implicit_key)?;
                    }

                    const mapping = _.parseBlockMapping(
                        implicit_key,
                        scalar_start,
                        scalar_indent,
                        scalar_line,
                    )?;

                    if (mapping_anchor := implicit_key_anchors.mapping_anchor) {
                        _.anchors.put(mapping_anchor.slice(_.input), mapping)?;
                    }

                    return mapping;
                }

                node = scalar.data.toExpr(scalar_start, _.input); break :node_sw;
            },
            .directive => {
                return error.UnexpectedToken;
            },
            .reserved => {
                return error.UnexpectedToken;
            },
            }
        }

        if (mapping_anchor := node_props.has_mapping_anchor) {
            _.token = mapping_anchor;
            return error.MultipleAnchors;
        }

        if (mapping_tag := node_props.has_mapping_tag) {
            _.token = mapping_tag;
            return error.MultipleTags;
        }

        const resolved = switch (node.data) {
            .e_null => node_props.tag().resolveNull(node.loc),
            else => node,
        };

        if (anchor := node_props.anchor()) {
            _.anchors.put(anchor.slice(_.input), resolved)?;
        }

        return resolved;
    }

    fn next(): enc.unit() {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return _.input[pos.cast()];
        }
        return 0;
    }

    fn foldLines(*_): usize {
        let total: usize = 0;
        let sw_v_11 = _.next();
        sw_11: while (true) {
            switch (sw_v_11) {
            '\r' => {
                if (_.peek(1) == '\n') {
                    _.inc(1);
                }

                sw_v_11 = '\n'; continue :sw_11;
            },
            '\n' => {
                total += 1;
                _.newline();
                _.tab_after_indent = false;
                _.inc(1);
                sw_v_11 = _.next(); continue :sw_11;
            },
            ' ' => {
                let indent: Indent = Indent.@"from"(1);
                _.inc(1);
                while (_.next() == ' ') {
                    _.inc(1);
                    indent.inc(1);
                }

                _.line_indent = indent;

                if (_.next() == '\t') {
                    _.tab_after_indent = true;
                }
                _.skipSWhite();
                sw_v_11 = _.next(); continue :sw_11;
            },
            '\t' => {
                // there's no indentation, but we still skip
                // the whitespace
                _.tab_after_indent = true;
                _.inc(1);
                _.skipSWhite();
                sw_v_11 = _.next(); continue :sw_11;
            },
            else => return total,
            }
        }
    }

    const ScanPlainScalarError = error{
        OutOfMemory,
        UnexpectedCharacter,
        // ScalarTypeMismatch,
    };

    type ScalarResolverCtx = ParserScalarResolverCtx<enc>;

    fn scanPlainScalar(*_, opts: ScanOptions): ScanPlainScalarError!Token(enc) {

        let ctx: ScalarResolverCtx = {
            str_builder: _.stringBuilder(),
            parser: _,
            scalar: null,
            tag: opts.tag,
            start: _.pos,
            line: _.line,
            line_indent: _.line_indent,
        };

        let sw_v_12 = _.next();
        sw_12: while (true) {
            switch (sw_v_12) {
            0 => {
                if (!_.isEof()) {
                    return error.UnexpectedCharacter;
                }
                return ctx.done();
            },

            '-' => {
                if (_.line_indent == .none && _.remainStartsWith("---") && _.isAnyOrEofAt(" \t\n\r", 3)) {
                    return ctx.done();
                }

                if (!ctx.resolved && ctx.str_builder.len() == 0) {
                    ctx.appendSource('-', _.pos)?;
                    _.inc(1);
                    ctx.tryResolveNumber(_, .negative)?;
                    sw_v_12 = _.next(); continue :sw_12;
                }

                ctx.appendSource('-', _.pos)?;
                _.inc(1);
                sw_v_12 = _.next(); continue :sw_12;
            },

            '.' => {
                if (_.line_indent == .none && _.remainStartsWith("...") && _.isAnyOrEofAt(" \t\n\r", 3)) {
                    return ctx.done();
                }

                if (!ctx.resolved && ctx.str_builder.len() == 0) {
                    switch (_.peek(1)) {
                        'n',
                        'N',
                        'i',
                        'I'
                        => {
                            ctx.appendSource('.', _.pos)?;
                            _.inc(1);
                            ctx.tryResolveNumber(_, .dot)?;
                            sw_v_12 = _.next(); continue :sw_12;
                        },

                        else => {
                            ctx.tryResolveNumber(_, .other)?;
                            sw_v_12 = _.next(); continue :sw_12;
                        },
                    }
                }

                ctx.appendSource('.', _.pos)?;
                _.inc(1);
                sw_v_12 = _.next(); continue :sw_12;
            },

            ':' => {
                if (_.isSWhiteOrBCharOrEofAt(1)) {
                    return ctx.done();
                }

                switch (_.context.get()) {
                    .block_out,
                    .block_in,
                    .flow_in
                    => {},
                    .flow_key => {
                        switch (_.peek(1)) {
                            ',',
                            '[',
                            ']',
                            '{',
                            '}'
                            => {
                                return ctx.done();
                            },
                            else => {},
                        }
                    },
                }

                ctx.appendSource(':', _.pos)?;
                _.inc(1);
                sw_v_12 = _.next(); continue :sw_12;
            },

            '#' => {
                const prev = _.input[_.pos.sub(1).cast()];
                if (_.pos == .zero || switch (prev) {
                    ' ',
                    '\t',
                    '\r',
                    '\n'
                    => true,
                    else => false,
                }) {
                    return ctx.done();
                }

                ctx.appendSource('#', _.pos)?;
                _.inc(1);
                sw_v_12 = _.next(); continue :sw_12;
            },

            ',',
            '[',
            ']',
            '{',
            '}'
            => |c| {
                switch (_.context.get()) {
                    .block_in,
                    .block_out
                    => {},

                    .flow_in,
                    .flow_key
                    => {
                        return ctx.done();
                    },
                }

                ctx.appendSource(c, _.pos)?;
                _.inc(1);
                sw_v_12 = _.next(); continue :sw_12;
            },

            ' ',
            '\t'
            => |c| {
                ctx.appendSourceWhitespace(c, _.pos)?;
                _.inc(1);
                sw_v_12 = _.next(); continue :sw_12;
            },

            '\r' => {
                if (_.peek(1) == '\n') {
                    _.inc(1);
                }

                sw_v_12 = '\n'; continue :sw_12;
            },

            '\n' => {
                _.newline();
                _.inc(1);

                const lines = _.foldLines();

                if (block_indent := _.block_indents.get()) {
                    switch (_.line_indent.cmp(block_indent)) {
                        .gt => {
                            // continue (whitespace already stripped)
                        },
                        .lt, .eq => {
                            // end here. this it the start of a new value.
                            return ctx.done();
                        },
                    }
                }

                // clear the leading whitespace before the newline.
                ctx.parser.whitespace_buf.clearRetainingCapacity();

                if (lines == 0 && !_.isEof()) {
                    ctx.appendWhitespace(' ')?;
                }

                ctx.appendWhitespaceNTimes('\n', lines)?;

                sw_v_12 = _.next(); continue :sw_12;
            },

            else => |c| {
                if (ctx.resolved || ctx.str_builder.len() != 0) {
                    const start = _.pos;
                    _.inc(1);
                    ctx.appendSource(c, start)?;
                    sw_v_12 = _.next(); continue :sw_12;
                }

                // first non-whitespace

                // TODO: make more better
                switch (c) {
                    'n' => {
                        const n_start = _.pos;
                        _.inc(1);
                        if (_.remainStartsWith("ull")) {
                            ctx.resolve(NodeScalar.null, n_start, "null")?;
                            _.inc(3);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        ctx.appendSource(c, n_start)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },
                    'N' => {
                        const n_start = _.pos;
                        _.inc(1);
                        if (_.remainStartsWith("ull")) {
                            ctx.resolve(NodeScalar.null, n_start, "Null")?;
                            _.inc(3);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        if (_.remainStartsWith("ULL")) {
                            ctx.resolve(NodeScalar.null, n_start, "NULL")?;
                            _.inc(3);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        ctx.appendSource(c, n_start)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },
                    '~' => {
                        const start = _.pos;
                        _.inc(1);
                        ctx.resolve(NodeScalar.null, start, "~")?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },
                    't' => {
                        const t_start = _.pos;
                        _.inc(1);
                        if (_.remainStartsWith("rue")) {
                            ctx.resolve({ boolean: true }, t_start, "true")?;
                            _.inc(3);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        ctx.appendSource(c, t_start)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },
                    'T' => {
                        const t_start = _.pos;
                        _.inc(1);
                        if (_.remainStartsWith("rue")) {
                            ctx.resolve({ boolean: true }, t_start, "True")?;
                            _.inc(3);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        if (_.remainStartsWith("RUE")) {
                            ctx.resolve({ boolean: true }, t_start, "TRUE")?;
                            _.inc(3);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        ctx.appendSource(c, t_start)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },
                    'f' => {
                        const f_start = _.pos;
                        _.inc(1);
                        if (_.remainStartsWith("alse")) {
                            ctx.resolve({ boolean: false }, f_start, "false")?;
                            _.inc(4);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        ctx.appendSource(c, f_start)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },
                    'F' => {
                        const f_start = _.pos;
                        _.inc(1);
                        if (_.remainStartsWith("alse")) {
                            ctx.resolve({ boolean: false }, f_start, "False")?;
                            _.inc(4);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        if (_.remainStartsWith("ALSE")) {
                            ctx.resolve({ boolean: false }, f_start, "FALSE")?;
                            _.inc(4);
                            sw_v_12 = _.next(); continue :sw_12;
                        }
                        ctx.appendSource(c, f_start)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },

                    '-' => {
                        ctx.appendSource('-', _.pos)?;
                        _.inc(1);
                        ctx.tryResolveNumber(_, .negative)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },

                    '+' => {
                        ctx.appendSource('+', _.pos)?;
                        _.inc(1);
                        ctx.tryResolveNumber(_, .positive)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },

                    '0'..='9' => {
                        ctx.tryResolveNumber(_, .other)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },

                    '.' => {
                        switch (_.peek(1)) {
                            'n',
                            'N',
                            'i',
                            'I'
                            => {
                                ctx.appendSource('.', _.pos)?;
                                _.inc(1);
                                ctx.tryResolveNumber(_, .dot)?;
                                sw_v_12 = _.next(); continue :sw_12;
                            },

                            else => {
                                ctx.tryResolveNumber(_, .other)?;
                                sw_v_12 = _.next(); continue :sw_12;
                            },
                        }
                    },

                    else => {
                        const start = _.pos;
                        _.inc(1);
                        ctx.appendSource(c, start)?;
                        sw_v_12 = _.next(); continue :sw_12;
                    },
                }
            },
            }
        }
    }

    const ScanBlockHeaderError = error{UnexpectedCharacter, TabIndentation};

    // positions parser at the first line break, or eof
    fn scanBlockHeader(*_): ScanBlockHeaderError!ScanBlockHeaderResult {
        // consume c-b-block-header

        let indent_indicator: Indent.Indicator? = null;
        let chomp: Chomp? = null;

        let sw_v_13 = _.next();
        sw_13: while (true) {
            switch (sw_v_13) {
            0 => {
                if (!_.isEof()) {
                    return error.UnexpectedCharacter;
                }
                return ScanBlockHeaderResult{ indent: indent_indicator ?? .default, chomp: chomp ?? .default };
            },
            '1'..='9' => |digit| {
                if (indent_indicator != null) {
                    return error.UnexpectedCharacter;
                }

                indent_indicator = @enumFromInt(digit - '0');
                _.inc(1);
                sw_v_13 = _.next(); continue :sw_13;
            },
            '-' => {
                if (chomp != null) {
                    return error.UnexpectedCharacter;
                }

                chomp = .strip;
                _.inc(1);
                sw_v_13 = _.next(); continue :sw_13;
            },
            '+' => {
                if (chomp != null) {
                    return error.UnexpectedCharacter;
                }

                chomp = .keep;
                _.inc(1);
                sw_v_13 = _.next(); continue :sw_13;
            },

            ' ',
            '\t'
            => {
                _.inc(1);

                _.skipSWhite();

                if (_.next() == '#') {
                    _.inc(1);
                    while (!_.isBCharOrEof()) {
                        if (_.next() == 0) {
                            return error.UnexpectedCharacter;
                        }
                        _.inc(1);
                    }
                }

                sw_v_13 = _.next(); continue :sw_13;
            },

            '\r' => {
                if (_.peek(1) == '\n') {
                    _.inc(1);
                }
                sw_v_13 = '\n'; continue :sw_13;
            },

            '\n' => {

                // the first newline is always excluded from a literal
                _.newline();
                _.inc(1);

                if (_.next() == '\t') {
                    // tab for indentation
                    return error.TabIndentation;
                }

                return ScanBlockHeaderResult{ indent: indent_indicator ?? .default, chomp: chomp ?? .default };
            },

            else => {
                return error.UnexpectedCharacter;
            },
            }
        }
    }

    const ScanLiteralScalarError = error{
        OutOfMemory,
        UnexpectedCharacter,
        TabIndentation,
        InvalidIndentation,
    };

    type LiteralScalarCtx = ParserLiteralScalarCtx<enc>;

    fn scanAutoIndentedLiteralScalar(*_, indent_indicator: Indent.Indicator, chomp: Chomp, folded: bool, start: Pos, line: Line): ScanLiteralScalarError!Token(enc) {
        const explicit_indent: Indent? = if (indent_indicator == .auto)
            null
        else blk: {
            let parent: usize = 0;
            if (b := _.block_indents.get()) {
                parent = b.cast();
            }
            break :blk Indent.@"from"(parent + @intFromEnum(indent_indicator));
        };

        let ctx: LiteralScalarCtx = {
            chomp: chomp,
            text: .init(_.allocator),
            folded: folded,
            start: start,
            line: line,

            leading_newlines: 0,
            content_indent: explicit_indent ?? .none,
            max_leading_indent: .none,
            explicit_indent: explicit_indent != null,
            prev_more_indented: false,
            cur_more_indented: false,
        };

        let first = @as(enc.unit(), 0);
        let first_ch = _.next();
        first_sw: while (true) {
            switch (first_ch) {
            0 => {
                if (!_.isEof()) {
                    return error.UnexpectedCharacter;
                }
                if (explicit_indent == null) {
                    ctx.content_indent = _.line_indent;
                }
                return ctx.done();
            },

            '\r' => {
                if (_.peek(1) == '\n') {
                    _.inc(1);
                }
                first_ch = '\n'; continue :first_sw;
            },
            '\n' => {
                _.newline();
                _.inc(1);
                if (_.next() == '\t') {
                    // tab for indentation
                    return error.TabIndentation;
                }
                ctx.leading_newlines += 1;
                first_ch = _.next(); continue :first_sw;
            },

            ' ' => {
                let indent: Indent = Indent.@"from"(1);
                _.inc(1);
                if (ci := explicit_indent) {
                    while (indent.isLessThan(ci) && _.next() == ' ') {
                        indent.inc(1);
                        _.inc(1);
                    }
                    _.line_indent = indent;
                    const nc = _.next();
                    if (nc == 0 || nc == '\n' || nc == '\r') {
                        first_ch = nc; continue :first_sw;
                    }
                    ctx.content_indent = ci; first = nc; break :first_sw;
                }
                while (_.next() == ' ') {
                    indent.inc(1);
                    _.inc(1);
                }

                if (ctx.max_leading_indent.isLessThan(indent)) {
                    ctx.max_leading_indent = indent;
                }

                _.line_indent = indent;

                first_ch = _.next(); continue :first_sw;
            },

            else => |c| {
                ctx.content_indent = explicit_indent ?? _.line_indent;
                first = c;
                break :first_sw;
            },
            }
        }

        ctx.cur_more_indented = first == ' ' || first == '\t';
        if (first == '\t') {
            _.tab_after_indent = true;
        }

        const min_indent = if (b := _.block_indents.get())
            Indent.@"from"(@max(ctx.content_indent.cast(), b.cast() + 1))
        else
            ctx.content_indent;

        let lit_ch = first;
        lit_sw: while (true) {
            switch (lit_ch) {
            0 => {
                if (!_.isEof()) {
                    return error.UnexpectedCharacter;
                }
                return ctx.done();
            },

            '\r' => {
                if (_.peek(1) == '\n') {
                    _.inc(1);
                }
                lit_ch = @as(enc.unit(), '\n'); continue :lit_sw;
            },
            '\n' => {
                ctx.leading_newlines += 1;
                _.newline();
                _.inc(1);
                let nl_ch = _.next();
                newlines_sw: while (true) {
                    switch (nl_ch) {
                    '\r' => {
                        if (_.peek(1) == '\n') {
                            _.inc(1);
                        }
                        nl_ch = '\n'; continue :newlines_sw;
                    },
                    '\n' => {
                        ctx.leading_newlines += 1;
                        _.newline();
                        _.inc(1);
                        if (_.next() == '\t') {
                            // tab for indentation
                            return error.TabIndentation;
                        }
                        nl_ch = _.next(); continue :newlines_sw;
                    },
                    ' ' => {
                        let indent: Indent = Indent.@"from"(0);
                        while (indent.isLessThan(ctx.content_indent) && _.next() == ' ') {
                            indent.inc(1);
                            _.inc(1);
                        }

                        _.line_indent = indent;
                        const nc = _.next();
                        ctx.cur_more_indented = nc == ' ' || nc == '\t';
                        if (nc == '\t') {
                            _.tab_after_indent = true;
                        }
                        lit_ch = nc; continue :lit_sw;
                    },
                    else => |c| {
                        ctx.cur_more_indented = c == '\t';
                        if (c == '\t') {
                            _.tab_after_indent = true;
                        }
                        lit_ch = c;
                        continue :lit_sw;
                    },
                    }
                }
            },

            '-' => {
                if (_.line_indent == .none && _.remainStartsWith("---") && _.isAnyOrEofAt(" \t\n\r", 3)) {
                    return ctx.done();
                }

                if (_.line_indent.isLessThan(min_indent)) {
                    return ctx.done();
                }

                ctx.append('-')?;

                _.inc(1);
                lit_ch = _.next(); continue :lit_sw;
            },

            '.' => {
                if (_.line_indent == .none && _.remainStartsWith("...") && _.isAnyOrEofAt(" \t\n\r", 3)) {
                    return ctx.done();
                }

                if (_.line_indent.isLessThan(min_indent)) {
                    return ctx.done();
                }

                ctx.append('.')?;

                _.inc(1);
                lit_ch = _.next(); continue :lit_sw;
            },

            else => |c| {
                if (_.line_indent.isLessThan(min_indent)) {
                    return ctx.done();
                }

                ctx.append(c)?;

                _.inc(1);
                lit_ch = _.next(); continue :lit_sw;
            },
            }
        }
    }

    fn scanLiteralScalar(*_): ScanLiteralScalarError!Token(enc) {
        defer _.whitespace_buf.clearRetainingCapacity();

        const start = _.pos;
        const line = _.line;

        const block_hdr = _.scanBlockHeader()?;
        const indent_indicator = block_hdr.indent;
        const chomp = block_hdr.chomp;

        return _.scanAutoIndentedLiteralScalar(indent_indicator, chomp, false, start, line);
    }

    fn scanFoldedScalar(*_): ScanLiteralScalarError!Token(enc) {
        const start = _.pos;
        const line = _.line;

        const block_hdr = _.scanBlockHeader()?;
        const indent_indicator = block_hdr.indent;
        const chomp = block_hdr.chomp;

        return _.scanAutoIndentedLiteralScalar(indent_indicator, chomp, true, start, line);
    }

    const ScanSingleQuotedScalarError = OOM || error{
        UnexpectedCharacter,
        UnexpectedDocumentStart,
        UnexpectedDocumentEnd,
    };

    fn scanSingleQuotedScalar(*_): ScanSingleQuotedScalarError!Token(enc) {
        const start = _.pos;
        const scalar_line = _.line;
        const scalar_indent = _.line_indent;

        let text: std.array_list.Managed(enc.unit()) = .init(_.allocator);
        errdefer text.deinit();

        let nl = false;

        let sw_v_14 = _.next();
        sw_14: while (true) {
            switch (sw_v_14) {
            0 => return error.UnexpectedCharacter,

            '.' => {
                if (nl && _.line_indent == .none && _.remainStartsWith("...") && _.isSWhiteOrBCharAt(3)) {
                    return error.UnexpectedDocumentEnd;
                }
                nl = false;
                text.append('.')?;
                _.inc(1);
                sw_v_14 = _.next(); continue :sw_14;
            },

            '-' => {
                if (nl && _.line_indent == .none && _.remainStartsWith("---") && _.isSWhiteOrBCharAt(3)) {
                    return error.UnexpectedDocumentStart;
                }
                nl = false;
                text.append('-')?;
                _.inc(1);
                sw_v_14 = _.next(); continue :sw_14;
            },

            '\r',
            '\n'
            => {
                nl = true;
                _.newline();
                _.inc(1);
                switch (_.foldLines()) {
                    0 => text.append(' ')?,
                    else => |lines| text.appendNTimes('\n', lines)?,
                }
                if (block_indent := _.block_indents.get()) {
                    if (_.line_indent.isLessThanOrEqual(block_indent)) {
                        return error.UnexpectedCharacter;
                    }
                }
                sw_v_14 = _.next(); continue :sw_14;
            },

            ' ',
            '\t'
            => {
                nl = false;
                const off = _.pos;
                _.inc(1);
                _.skipSWhite();
                if (!_.isBChar()) {
                    text.appendSlice(_.slice(off, _.pos))?;
                }
                sw_v_14 = _.next(); continue :sw_14;
            },

            '\'' => {
                nl = false;
                _.inc(1);
                if (_.next() == '\'') {
                    text.append('\'')?;
                    _.inc(1);
                    sw_v_14 = _.next(); continue :sw_14;
                }

                return Token(enc).scalar({
                    start: start,
                    indent: scalar_indent,
                    line: scalar_line,
                    resolved: .{
                        style: .quoted,
                        data: {
                            string: {
                                list: text,
                            },
                        },
                    },
                });
            },
            else => |c| {
                nl = false;
                text.append(c)?;
                _.inc(1);
                sw_v_14 = _.next(); continue :sw_14;
            },
            }
        }
    }

    const ScanDoubleQuotedScalarError = OOM || error{
        UnexpectedCharacter,
        UnexpectedDocumentStart,
        UnexpectedDocumentEnd,
    };

    fn scanDoubleQuotedScalar(*_): ScanDoubleQuotedScalarError!Token(enc) {
        const start = _.pos;
        const scalar_line = _.line;
        const scalar_indent = _.line_indent;
        let text: std.array_list.Managed(enc.unit()) = .init(_.allocator);
        errdefer text.deinit();

        let nl = false;

        let sw_v_15 = _.next();
        sw_15: while (true) {
            switch (sw_v_15) {
            0 => return error.UnexpectedCharacter,

            '.' => {
                if (nl && _.line_indent == .none && _.remainStartsWith("...") && _.isSWhiteOrBCharAt(3)) {
                    return error.UnexpectedDocumentEnd;
                }
                nl = false;
                text.append('.')?;
                _.inc(1);
                sw_v_15 = _.next(); continue :sw_15;
            },

            '-' => {
                if (nl && _.line_indent == .none && _.remainStartsWith("---") && _.isSWhiteOrBCharAt(3)) {
                    return error.UnexpectedDocumentStart;
                }
                nl = false;
                text.append('-')?;
                _.inc(1);
                sw_v_15 = _.next(); continue :sw_15;
            },

            '\r',
            '\n'
            => {
                _.newline();
                _.inc(1);
                switch (_.foldLines()) {
                    0 => text.append(' ')?,
                    else => |lines| text.appendNTimes('\n', lines)?,
                }

                if (block_indent := _.block_indents.get()) {
                    if (_.line_indent.isLessThanOrEqual(block_indent)) {
                        return error.UnexpectedCharacter;
                    }
                }
                nl = true;
                sw_v_15 = _.next(); continue :sw_15;
            },

            ' ',
            '\t'
            => {
                nl = false;
                const off = _.pos;
                _.inc(1);
                _.skipSWhite();
                if (!_.isBChar()) {
                    text.appendSlice(_.slice(off, _.pos))?;
                }
                sw_v_15 = _.next(); continue :sw_15;
            },

            '"' => {
                nl = false;
                _.inc(1);
                return Token(enc).scalar({
                    start: start,
                    indent: scalar_indent,
                    line: scalar_line,
                    resolved: .{
                        style: .quoted,
                        data: {
                            string: { list: text },
                        },
                    },
                });
            },

            '\\' => {
                nl = false;
                _.inc(1);
                switch (_.next()) {
                    '\r',
                    '\n'
                    => {
                        _.newline();
                        _.inc(1);
                        const lines = _.foldLines();

                        if (block_indent := _.block_indents.get()) {
                            if (_.line_indent.isLessThanOrEqual(block_indent)) {
                                return error.UnexpectedCharacter;
                            }
                        }

                        text.appendNTimes('\n', lines)?;
                        _.skipSWhite();
                        sw_v_15 = _.next(); continue :sw_15;
                    },

                    // escaped whitespace
                    ' ' => text.append(' ')?,
                    '\t' => text.append('\t')?,

                    '0' => text.append(0)?,
                    'a' => text.append(0x7)?,
                    'b' => text.append(0x8)?,
                    't' => text.append('\t')?,
                    'n' => text.append('\n')?,
                    'v' => text.append(0x0b)?,
                    'f' => text.append(0xc)?,
                    'r' => text.append(0xd)?,
                    'e' => text.append(0x1b)?,
                    '"' => text.append('"')?,
                    '/' => text.append('/')?,
                    '\\' => text.append('\\')?,

                    'N' => switch (enc) {
                        .utf8 => text.appendSlice(&.{ 0xc2, 0x85 })?,
                        .utf16 => text.append(0x0085)?,
                        .latin1 => return error.UnexpectedCharacter,
                    },
                    '_' => switch (enc) {
                        .utf8 => text.appendSlice(&.{ 0xc2, 0xa0 })?,
                        .utf16 => text.append(0x00a0)?,
                        .latin1 => return error.UnexpectedCharacter,
                    },
                    'L' => switch (enc) {
                        .utf8 => text.appendSlice(&.{ 0xe2, 0x80, 0xa8 })?,
                        .utf16 => text.append(0x2028)?,
                        .latin1 => return error.UnexpectedCharacter,
                    },
                    'P' => switch (enc) {
                        .utf8 => text.appendSlice(&.{ 0xe2, 0x80, 0xa9 })?,
                        .utf16 => text.append(0x2029)?,
                        .latin1 => return error.UnexpectedCharacter,
                    },

                    'x' => _.decodeHexCodePoint(.x, &text)?,
                    'u' => _.decodeHexCodePoint(.u, &text)?,
                    'U' => _.decodeHexCodePoint(.U, &text)?,

                    else => return error.UnexpectedCharacter,
                }

                _.inc(1);
                sw_v_15 = _.next(); continue :sw_15;
            },

            else => |c| {
                nl = false;
                text.append(c)?;
                _.inc(1);
                sw_v_15 = _.next(); continue :sw_15;
            },
            }
        }
    }

    const Escape = ParserEscape;

    const DecodeHexCodePointError = OOM || error{UnexpectedCharacter};

    // TODO: should this append replacement characters instead of erroring?
    fn decodeHexCodePoint(
        *_: *@This(),
        comptime escape: Escape,
        text: *std.array_list.Managed(enc.unit()),
    ): DecodeHexCodePointError!void {
        let value = @as(escape.cp(), 0);
        let esc_i: usize = 0;
        while (esc_i < @intFromEnum(escape)) : (esc_i += 1) {
            _.inc(1);
            const digit = _.next();
            const num: u8 = switch (digit) {
                '0'..='9' => @intCast(digit - '0'),
                'a'..='f' => @intCast(digit - 'a' + 10),
                'A'..='F' => @intCast(digit - 'A' + 10),
                else => return error.UnexpectedCharacter,
            };

            value = value * @as(escape.cp(), 16) + @as(escape.cp(), num);
        }

        let cp_u21: u21 = 0;
        if (v := std.math.cast(u21, value)) {
            cp_u21 = v;
        } else {
            return error.UnexpectedCharacter;
        }

        if (escape == .u && cp_u21 >= 0xD800 && cp_u21 <= 0xDBFF) {
            if (_.peek(1) == '\\' && _.peek(2) == 'u') {
                let low: u21 = 0;
                let ok = true;
                const ds: [4]enc.unit() = [ _.peek(3), _.peek(4), _.peek(5), _.peek(6) ];
                for (d of ds) {
                    const n: u21 = switch (d) {
                        '0'..='9' => @intCast(d - '0'),
                        'a'..='f' => @intCast(d - 'a' + 10),
                        'A'..='F' => @intCast(d - 'A' + 10),
                        else => { ok = false; break; },
                    };
                    low = low * 16 + n;
                }
                if (ok && low >= 0xDC00 && low <= 0xDFFF) {
                    _.inc(6);
                    cp_u21 = 0x10000 + ((cp_u21 - 0xD800) << 10) + (low - 0xDC00);
                }
            }
        }

        switch (enc) {
            .utf8 => {
                let buf: [4]u8 = undefined;
                const enc_len = std.unicode.utf8Encode(cp_u21, &buf) catch {
                    return error.UnexpectedCharacter;
                };
                text.appendSlice(buf[0..enc_len])?;
            },
            .utf16 => {
                const enc_len = std.unicode.utf16CodepointSequenceLength(cp_u21) catch {
                    return error.UnexpectedCharacter;
                };

                switch (enc_len) {
                    1 => text.append(@intCast(cp_u21))?,
                    2 => {
                        const val = cp_u21 - 0x10000;
                        const high: u16 = @as(u16, 0xd800) + @as(u16, @intCast(val >> 10));
                        const low: u16 = @as(u16, 0xdc00) + @as(u16, @intCast(val & 0x3ff));
                        const pair: [2]u16 = [ high, low ];
                        text.appendSlice(&pair)?;
                    },
                    else => return error.UnexpectedCharacter,
                }
            },
            .latin1 => {
                if (cp_u21 > 0xff) {
                    return error.UnexpectedCharacter;
                }
                text.append(@intCast(cp_u21))?;
            },
        }
    }

    const ScanTagPropertyError = error{ UnresolvedTagHandle, UnexpectedCharacter };

    // c-ns-tag-property
    fn scanTagProperty(*_): ScanTagPropertyError!Token(enc) {
        const start = _.pos;

        // already at '!'
        _.inc(1);

        switch (_.next()) {
            0,
            ' ',
            '\t',
            '\n',
            '\r'
            => {
                // c-non-specific-tag
                // primary tag handle

                return Token(enc).tag({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                    tag: .non_specific,
                });
            },

            '<' => {
                // c-verbatim-tag

                _.inc(1);

                const prefix = prefix: {
                    if (_.next() == '!') {
                        _.inc(1);
                        let range = _.stringRange();
                        _.skipNsUriChars();
                        break :prefix range.end();
                    }

                    if (tag_len := _.isNsTagChar()) {
                        let range = _.stringRange();
                        _.inc(tag_len);
                        _.skipNsUriChars();
                        break :prefix range.end();
                    }

                    return error.UnexpectedCharacter;
                };

                _.trySkipChar('>')?;

                return Token(enc).tag({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                    tag: { verbatim: prefix },
                });
            },

            '!' => {
                // c-ns-shorthand-tag
                // secondary tag handle

                _.inc(1);
                let range = _.stringRange();
                _.trySkipNsTagChars()?;

                // s-separate
                switch (_.next()) {
                    0,
                    ' ',
                    '\t',
                    '\r',
                    '\n'
                    => {},

                    ',',
                    '[',
                    ']',
                    '{',
                    '}'
                    => {
                        switch (_.context.get()) {
                            .block_out,
                            .block_in
                            => {
                                return error.UnexpectedCharacter;
                            },
                            .flow_in,
                            .flow_key
                            => {},
                        }
                    },
                    else => {
                        return error.UnexpectedCharacter;
                    },
                }

                const shorthand = range.end();

                let tag: NodeTag = tag: {
                    const s = shorthand.slice(_.input);
                    if (std.mem.eql(enc.unit(), s, "bool")) {
                        break :tag NodeTag.bool;
                    }
                    if (std.mem.eql(enc.unit(), s, "int")) {
                        break :tag .int;
                    }
                    if (std.mem.eql(enc.unit(), s, "float")) {
                        break :tag .float;
                    }
                    if (std.mem.eql(enc.unit(), s, "null")) {
                        break :tag NodeTag.null;
                    }
                    if (std.mem.eql(enc.unit(), s, "str")) {
                        break :tag .str;
                    }

                    break :tag { unknown: shorthand };
                };

                return Token(enc).tag({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                    tag: tag,
                });
            },

            else => {
                // c-ns-shorthand-tag
                // named tag handle

                let range = _.stringRange();
                _.trySkipNsWordChars()?;
                let handle_or_shorthand = range.end();

                if (_.next() == '!') {
                    _.inc(1);
                    if (!_.tag_handles.contains(handle_or_shorthand.slice(_.input))) {
                        _.pos = range.off;
                        return error.UnresolvedTagHandle;
                    }

                    range = _.stringRange();
                    _.trySkipNsTagChars()?;
                    const shorthand = range.end();

                    return Token(enc).tag({
                        start: start,
                        indent: _.line_indent,
                        line: _.line,
                        tag: { unknown: shorthand },
                    });
                }

                // primary
                _.skipNsTagChars();
                handle_or_shorthand = range.end();

                let tag: NodeTag = tag: {
                    const s = handle_or_shorthand.slice(_.input);
                    if (std.mem.eql(enc.unit(), s, "bool")) {
                        break :tag NodeTag.bool;
                    }
                    if (std.mem.eql(enc.unit(), s, "int")) {
                        break :tag .int;
                    }
                    if (std.mem.eql(enc.unit(), s, "float")) {
                        break :tag .float;
                    }
                    if (std.mem.eql(enc.unit(), s, "null")) {
                        break :tag NodeTag.null;
                    }
                    if (std.mem.eql(enc.unit(), s, "str")) {
                        break :tag .str;
                    }

                    break :tag { unknown: handle_or_shorthand };
                };

                return Token(enc).tag({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                    tag: tag,
                });
            },
        }
    }

    // fn scanIndentation(self: *@This()) void {}

    const ScanError = error{
        OutOfMemory,
        UnexpectedToken,
        UnexpectedCharacter,
        UnresolvedTagHandle,
        UnexpectedDocumentStart,
        UnexpectedDocumentEnd,
        InvalidIndentation,
        TabIndentation,
        // ScalarTypeMismatch,
    };

    type ScanOptions = ParserScanOptions<enc>;
    type ScanCtx = ParserScanCtx<enc>;

    fn scan(*_, opts: ScanOptions): ScanError!void {

        const init_count_indent = opts.first_scan || opts.additional_parent_indent != null;
        if (init_count_indent) {
            _.tab_after_indent = false;
        }
        let ctx: ScanCtx = {
            parser: _,

            count_indentation: init_count_indent,
            in_indent_position: init_count_indent,
            additional_parent_indent: opts.additional_parent_indent,
        };

        const previous_token_line = _.token.line;

        let scan_ch = _.next();
        scan_sw: while (true) {
            switch (scan_ch) {
            0 => {
                // [1] c-printable excludes U+0000. `next()` returns NUL as
                // the EOF sentinel, so a literal NUL in the input must be
                // rejected here rather than silently ending the stream.
                if (!_.isEof()) {
                    return error.UnexpectedCharacter;
                }
                const start = _.pos;
                _.token = Token(enc).eof({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                }); break :scan_sw;
            },
            '-' => {
                const start = _.pos;

                if (_.line_indent == .none && _.remainStartsWith(enc.literal("---")) && _.isSWhiteOrBCharOrEofAt(3)) {
                    _.inc(3);
                    _.token = Token(enc).documentStart({
                        start: start,
                        indent: _.line_indent,
                        line: _.line,
                    }); break :scan_sw;
                }

                switch (_.peek(1)) {

                    // eof
                    // b-char
                    // s-white
                    0,
                    '\n',
                    '\r',
                    ' ',
                    '\t'
                    => {
                        _.inc(1);

                        switch (_.context.get()) {
                            .block_out,
                            .block_in
                            => {},
                            .flow_in,
                            .flow_key
                            => {
                                _.token.start = start;
                                return error.UnexpectedToken;
                            },
                        }

                        _.token = Token(enc).sequenceEntry({
                            start: start,
                            indent: _.line_indent,
                            line: _.line,
                        }); break :scan_sw;
                    },

                    // c-flow-indicator
                    ',',
                    ']',
                    '[',
                    '}',
                    '{'
                    => {
                        switch (_.context.get()) {
                            .flow_in,
                            .flow_key
                            => {
                                _.inc(1);

                                _.token = Token(enc).sequenceEntry({
                                    start: start,
                                    indent: _.line_indent,
                                    line: _.line,
                                });

                                return error.UnexpectedToken;
                            },
                            .block_in,
                            .block_out
                            => {
                                //  scanPlainScalar
                            },
                        }
                    },

                    else => {
                        //  scanPlainScalar
                    },
                }

                _.token = _.scanPlainScalar(opts)?; break :scan_sw;
            },
            '.' => {
                const start = _.pos;

                if (_.line_indent == .none && _.remainStartsWith(enc.literal("...")) && _.isSWhiteOrBCharOrEofAt(3)) {
                    _.inc(3);
                    _.token = Token(enc).documentEnd({
                        start: start,
                        indent: _.line_indent,
                        line: _.line,
                    }); break :scan_sw;
                }

                _.token = _.scanPlainScalar(opts)?; break :scan_sw;
            },
            '?' => {
                const start = _.pos;

                switch (_.peek(1)) {
                    // eof
                    // s-white
                    // b-char
                    0,
                    ' ',
                    '\t',
                    '\n',
                    '\r'
                    => {
                        _.inc(1);
                        _.token = Token(enc).mappingKey({
                            start: start,
                            indent: _.line_indent,
                            line: _.line,
                        }); break :scan_sw;
                    },

                    // c-flow-indicator
                    ',',
                    ']',
                    '[',
                    '}',
                    '{'
                    => {
                        switch (_.context.get()) {
                            .block_in,
                            .block_out
                            => {
                                // scanPlainScalar
                            },
                            .flow_in,
                            .flow_key
                            => {
                                _.inc(1);
                                _.token = Token(enc).mappingKey({
                                    start: start,
                                    indent: _.line_indent,
                                    line: _.line,
                                }); break :scan_sw;
                            },
                        }
                    },

                    else => {
                        // scanPlainScalar
                    },
                }

                _.token = _.scanPlainScalar(opts)?; break :scan_sw;
            },
            ':' => {
                const start = _.pos;

                switch (_.peek(1)) {
                    0,
                    ' ',
                    '\t',
                    '\n',
                    '\r'
                    => {
                        _.inc(1);
                        _.token = Token(enc).mappingValue({
                            start: start,
                            indent: _.line_indent,
                            line: _.line,
                        }); break :scan_sw;
                    },

                    // c-flow-indicator
                    ',',
                    ']',
                    '[',
                    '}',
                    '{'
                    => {
                        // scanPlainScalar
                        switch (_.context.get()) {
                            .block_in,
                            .block_out
                            => {
                                // scanPlainScalar
                            },
                            .flow_in,
                            .flow_key
                            => {
                                _.inc(1);
                                _.token = Token(enc).mappingValue({
                                    start: start,
                                    indent: _.line_indent,
                                    line: _.line,
                                }); break :scan_sw;
                            },
                        }
                    },

                    else => {
                        switch (_.context.get()) {
                            .block_in,
                            .block_out,
                            .flow_in
                            => {
                                // scanPlainScalar
                            },
                            .flow_key
                            => {
                                _.inc(1);
                                _.token = Token(enc).mappingValue({
                                    start: start,
                                    indent: _.line_indent,
                                    line: _.line,
                                }); break :scan_sw;
                            },
                        }
                    },
                }

                _.token = _.scanPlainScalar(opts)?; break :scan_sw;
            },
            ',' => {
                const start = _.pos;

                switch (_.context.get()) {
                    .flow_in,
                    .flow_key
                    => {
                        _.inc(1);
                        _.token = Token(enc).collectEntry({
                            start: start,
                            indent: _.line_indent,
                            line: _.line,
                        }); break :scan_sw;
                    },
                    .block_in,
                    .block_out
                    => {},
                }

                _.token = _.scanPlainScalar(opts)?; break :scan_sw;
            },
            '[' => {
                const start = _.pos;

                _.inc(1);
                _.token = Token(enc).sequenceStart({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                }); break :scan_sw;
            },
            ']' => {
                const start = _.pos;

                _.inc(1);
                _.token = Token(enc).sequenceEnd({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                }); break :scan_sw;
            },
            '{' => {
                const start = _.pos;

                _.inc(1);
                _.token = Token(enc).mappingStart({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                }); break :scan_sw;
            },
            '}' => {
                const start = _.pos;

                _.inc(1);
                _.token = Token(enc).mappingEnd({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                }); break :scan_sw;
            },
            '#' => {
                const start = _.pos;

                const prev = if (start == .zero) 0 else _.input[start.cast() - 1];
                switch (prev) {
                    0,
                    ' ',
                    '\t',
                    '\n',
                    '\r'
                    => {},
                    else => {
                        // TODO: prove this is unreachable
                        return error.UnexpectedCharacter;
                    },
                }

                _.inc(1);
                while (!_.isBCharOrEof()) {
                    if (_.next() == 0) {
                        return error.UnexpectedCharacter;
                    }
                    _.inc(1);
                }
                scan_ch = _.next(); continue :scan_sw;
            },
            '&' => {
                const start = _.pos;

                _.inc(1);

                let range = _.stringRange();
                _.trySkipNsAnchorChars()?;

                const anchor: Token(enc) = .anchor({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                    name: range.end(),
                });

                switch (_.next()) {
                    0,
                    ' ',
                    '\t',
                    '\n',
                    '\r'
                    => {
                        _.token = anchor; break :scan_sw;
                    },

                    ',',
                    ']',
                    '[',
                    '}',
                    '{'
                    => {
                        switch (_.context.get()) {
                            .block_in,
                            .block_out
                            => {
                                // error.UnexpectedCharacter
                            },
                            .flow_key,
                            .flow_in
                            => {
                                _.token = anchor; break :scan_sw;
                            },
                        }
                    },

                    else => {},
                }

                return error.UnexpectedCharacter;
            },
            '*' => {
                const start = _.pos;

                _.inc(1);

                let range = _.stringRange();
                _.trySkipNsAnchorChars()?;

                const alias: Token(enc) = .alias({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                    name: range.end(),
                });

                switch (_.next()) {
                    0,
                    ' ',
                    '\t',
                    '\n',
                    '\r'
                    => {
                        _.token = alias; break :scan_sw;
                    },

                    ',',
                    ']',
                    '[',
                    '}',
                    '{'
                    => {
                        switch (_.context.get()) {
                            .block_in,
                            .block_out
                            => {
                                // error.UnexpectedCharacter
                            },
                            .flow_key,
                            .flow_in
                            => {
                                _.token = alias; break :scan_sw;
                            },
                        }
                    },

                    else => {},
                }

                return error.UnexpectedCharacter;
            },
            '!' => {
                _.token = _.scanTagProperty()?; break :scan_sw;
            },
            '|' => {
                const start = _.pos;

                switch (_.context.get()) {
                    .block_out,
                    .block_in
                    => {
                        const indicator_indent = _.line_indent;
                        _.inc(1);
                        let tok = _.scanLiteralScalar()?;
                        tok.indent = indicator_indent;
                        _.token = tok; break :scan_sw;
                    },
                    .flow_in,
                    .flow_key
                    => {},
                }
                _.token.start = start;
                return error.UnexpectedToken;
            },
            '>' => {
                const start = _.pos;

                switch (_.context.get()) {
                    .block_out,
                    .block_in
                    => {
                        const indicator_indent = _.line_indent;
                        _.inc(1);
                        let tok = _.scanFoldedScalar()?;
                        tok.indent = indicator_indent;
                        _.token = tok; break :scan_sw;
                    },
                    .flow_in,
                    .flow_key
                    => {},
                }
                _.token.start = start;
                return error.UnexpectedToken;
            },
            '\'' => {
                _.inc(1);
                _.token = _.scanSingleQuotedScalar()?; break :scan_sw;
            },
            '"' => {
                _.inc(1);
                _.token = _.scanDoubleQuotedScalar()?; break :scan_sw;
            },
            '%' => {
                const start = _.pos;

                _.inc(1);
                _.token = Token(enc).directive({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                }); break :scan_sw;
            },
            '@', '`' => {
                const start = _.pos;

                _.inc(1);
                _.token = Token(enc).reserved({
                    start: start,
                    indent: _.line_indent,
                    line: _.line,
                });
                return error.UnexpectedToken;
            },

            '\n' => { scan_ch = ctx.scanWhitespace('\n')?; continue :scan_sw; },
            '\r' => { scan_ch = ctx.scanWhitespace('\r')?; continue :scan_sw; },
            ' ' => { scan_ch = ctx.scanWhitespace(' ')?; continue :scan_sw; },
            '\t' => { scan_ch = ctx.scanWhitespace('\t')?; continue :scan_sw; },

            else => {
                _.token = _.scanPlainScalar(opts)?; break :scan_sw;
            },
            }
        }

        switch (_.context.get()) {
            .block_out,
            .block_in
            => {},
            .flow_in,
            .flow_key
            => {
                if (block_indent := _.block_indents.get()) {
                    if (!opts.outside_context && _.token.line != previous_token_line && _.token.indent.isLessThanOrEqual(block_indent)) {
                        return error.UnexpectedToken;
                    }
                }
            },
        }
    }

    fn isChar(*_, char: enc.unit()): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return _.input[pos.cast()] == char;
        }
        return false;
    }

    fn trySkipChar(*_, char: enc.unit()): UnexpectedCharacterError!void {
        if (!_.isChar(char)) {
            return error.UnexpectedCharacter;
        }
        _.inc(1);
    }

    fn isNsWordChar(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return chars.isNsWordChar(_.input[pos.cast()]);
        }
        return false;
    }

    /// ns-char
    fn isNsChar(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return chars.isNsChar(_.input[pos.cast()]);
        }
        return false;
    }

    fn skipNsChars(*_) {
        while (_.isNsChar()) {
            _.inc(1);
        }
    }

    fn trySkipNsChars(*_): UnexpectedCharacterError!void {
        if (!_.isNsChar()) {
            return error.UnexpectedCharacter;
        }
        _.skipNsChars();
    }

    fn isNsTagChar(*_): u8? {
        const r = _.remain();
        return chars.isNsTagChar(r);
    }

    fn skipNsTagChars(*_) {
        while (tag_len := _.isNsTagChar()) {
            _.inc(tag_len);
        }
    }

    fn trySkipNsTagChars(*_): UnexpectedCharacterError!void {
        const first_len = _.isNsTagChar() ?? {
            return error.UnexpectedCharacter;
        };
        _.inc(first_len);
        while (tag_len := _.isNsTagChar()) {
            _.inc(tag_len);
        }
    }

    fn isNsAnchorChar(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return chars.isNsAnchorChar(_.input[pos.cast()]);
        }
        return false;
    }

    fn trySkipNsAnchorChars(*_): UnexpectedCharacterError!void {
        if (!_.isNsAnchorChar()) {
            return error.UnexpectedCharacter;
        }
        _.inc(1);
        while (_.isNsAnchorChar()) {
            _.inc(1);
        }
    }

    /// s-l-comments
    ///
    /// positions `pos` on the next newline, or eof. Errors
    fn trySkipToNewLine(*_): UnexpectedCharacterError!void {
        let whitespace = false;

        if (_.isSWhite()) {
            whitespace = true;
            _.skipSWhite();
        }

        if (_.isChar('#')) {
            if (!whitespace) {
                return error.UnexpectedCharacter;
            }
            _.inc(1);
            while (_.pos.isLessThan(_.input.len) && !_.isChar('\n') && !_.isChar('\r')) {
                if (_.next() == 0) {
                    return error.UnexpectedCharacter;
                }
                _.inc(1);
            }
        }

        if (_.pos.isLessThan(_.input.len) && !_.isChar('\n') && !_.isChar('\r')) {
            return error.UnexpectedCharacter;
        }
    }

    fn isSWhiteOrBCharOrEofAt(*_, n: usize): bool {
        const pos = _.pos.add(n);
        if (pos.isLessThan(_.input.len)) {
            const c = _.input[pos.cast()];
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }
        return true;
    }

    fn isSWhiteOrBCharAt(*_, n: usize): bool {
        const pos = _.pos.add(n);
        if (pos.isLessThan(_.input.len)) {
            const c = _.input[pos.cast()];
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }
        return false;
    }

    fn isAnyAt(values: []const enc.unit(), n: usize): bool {
        const pos = _.pos.add(n);
        if (pos.isLessThan(_.input.len)) {
            return std.mem.indexOfScalar(enc.unit(), values, _.input[pos.cast()]) != null;
        }
        return false;
    }

    fn isAnyOrEofAt(values: []const enc.unit(), n: usize): bool {
        const pos = _.pos.add(n);
        if (pos.isLessThan(_.input.len)) {
            return std.mem.indexOfScalar(enc.unit(), values, _.input[pos.cast()]) != null;
        }
        return false;
    }

    fn isEof(): bool {
        return !_.pos.isLessThan(_.input.len);
    }

    fn isEofAt(n: usize): bool {
        return !_.pos.add(n).isLessThan(_.input.len);
    }

    fn isBChar(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return chars.isBChar(_.input[pos.cast()]);
        }
        return false;
    }

    fn isBCharOrEof(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return chars.isBChar(_.input[pos.cast()]);
        }
        return true;
    }

    fn isSWhiteOrBCharOrEof(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            const c = _.input[pos.cast()];
            return chars.isSWhite(c) || chars.isBChar(c);
        }
        return true;
    }

    fn isSWhite(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return chars.isSWhite(_.input[pos.cast()]);
        }
        return false;
    }

    fn isSWhiteAt(*_, n: usize): bool {
        const pos = _.pos.add(n);
        if (pos.isLessThan(_.input.len)) {
            return chars.isSWhite(_.input[pos.cast()]);
        }
        return false;
    }

    fn skipSWhite(*_) {
        while (_.isSWhite()) {
            _.inc(1);
        }
    }

    fn trySkipSWhite(*_): UnexpectedCharacterError!void {
        if (!_.isSWhite()) {
            return error.UnexpectedCharacter;
        }
        while (_.isSWhite()) {
            _.inc(1);
        }
    }

    fn isNsHexDigit(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return chars.isNsHexDigit(_.input[pos.cast()]);
        }
        return false;
    }

    fn isNsDecDigit(*_): bool {
        const pos = _.pos;
        if (pos.isLessThan(_.input.len)) {
            return chars.isNsDecDigit(_.input[pos.cast()]);
        }
        return false;
    }

    fn skipNsDecDigits(*_) {
        while (_.isNsDecDigit()) {
            _.inc(1);
        }
    }

    fn trySkipNsDecDigits(*_): UnexpectedCharacterError!void {
        if (!_.isNsDecDigit()) {
            return error.UnexpectedCharacter;
        }
        _.skipNsDecDigits();
    }

    fn skipNsWordChars(*_) {
        while (_.isNsWordChar()) {
            _.inc(1);
        }
    }

    fn trySkipNsWordChars(*_): UnexpectedCharacterError!void {
        if (!_.isNsWordChar()) {
            return error.UnexpectedCharacter;
        }
        _.skipNsWordChars();
    }

    fn isNsUriChar(*_): bool {
        const r = _.remain();
        return chars.isNsUriChar(r);
    }

    fn skipNsUriChars(*_) {
        while (_.isNsUriChar()) {
            _.inc(1);
        }
    }

    fn trySkipNsUriChars(*_): UnexpectedCharacterError!void {
        if (!_.isNsUriChar()) {
            return error.UnexpectedCharacter;
        }
        _.skipNsUriChars();
    }

    fn stringRange(*_): String.Range.Start {
        return {
            off: _.pos,
            parser: _,
        };
    }

    fn stringBuilder(*_): String.Builder {
        return {
            parser: _,
            str: { range: { off: .zero, end: .zero } },
        };
    }

    pub type String = ParserString<enc>;

    pub type NodeTag = ParserNodeTag<enc>;

    pub type NodeScalar = ParserNodeScalar<enc>;

    // pub const Node = struct {
    //     start: Pos,
    //     data: Data,

    //     pub const Data = union(enum) {
    //         scalar: Scalar,
    //         sequence: *Sequence,
    //         mapping: *Mapping,

    //         // TODO: we will probably need an alias
    //         // node that is resolved later. problem:
    //         // ```
    //         // &map
    //         // hi:
    //         //  hello: *map
    //         // ```
    //         // map needs to be put in the map before
    //         // we finish parsing the map node, because
    //         // 'hello' value needs to be able to find it.
    //         //
    //         // alias: Alias,
    //     };

    //     pub const Sequence = struct {
    //         list: std.array_list.Managed(Node),

    //         pub fn init(allocator: std.mem.Allocator) Sequence {
    //             return .{ .list = .init(allocator) };
    //         }

    //         pub fn count(this: *const Sequence) usize {
    //             return this.list.items.len;
    //         }

    //         pub fn slice(this: *const Sequence) []const Node {
    //             return this.list.items;
    //         }
    //     };

    //     pub const Mapping = struct {
    //         keys: std.array_list.Managed(Node),
    //         values: std.array_list.Managed(Node),

    //         pub fn init(allocator: std.mem.Allocator) Mapping {
    //             return .{ .keys = .init(allocator), .values = .init(allocator) };
    //         }

    //         pub fn append(this: *Mapping, key: Node, value: Node) OOM!void {
    //             try this.keys.append(key);
    //             try this.values.append(value);
    //         }

    //         pub fn count(this: *const Mapping) usize {
    //             return this.keys.items.len;
    //         }
    //     };

    //     // pub const Alias = struct {
    //     //     anchor_id: Anchors.Id,
    //     // };

    //     pub fn isNull(this: *const Node) bool {
    //         return switch (this.data) {
    //             .scalar => |s| s == .null,
    //             else => false,
    //         };
    //     }

    //     pub fn @"null"(start: Pos) Node {
    //         return .{
    //             .start = start,
    //             .data = .{ .scalar = .null },
    //         };
    //     }

    //     pub fn boolean(start: Pos, value: bool) Node {
    //         return .{
    //             .start = start,
    //             .data = .{ .scalar = .{ .boolean = value } },
    //         };
    //     }

    //     pub fn number(start: Pos, value: f64) Node {
    //         return .{
    //             .start = start,
    //             .data = .{ .scalar = .{ .number = value } },
    //         };
    //     }

    //     pub fn string(start: Pos, str: String) Node {
    //         return .{
    //             .start = start,
    //             .data = .{ .scalar = .{ .string = .{ .text = str } } },
    //         };
    //     }

    //     // pub fn alias(start: Pos, anchor_id: Anchors.Id) Node {
    //     //     return .{
    //     //         .start = start,
    //     //         .data = .{ .alias = .{ .anchor_id = anchor_id } },
    //     //     };
    //     // }

    //     pub fn init(allocator: std.mem.Allocator, start: Pos, data: anytype) OOM!Node {
    //         return .{
    //             .start = start,
    //             .data = switch (@TypeOf(data)) {
    //                 Scalar => .{ .scalar = data },
    //                 Sequence => sequence: {
    //                     const seq = try allocator.create(Sequence);
    //                     seq.* = data;
    //                     break :sequence .{ .sequence = seq };
    //                 },
    //                 Mapping => mapping: {
    //                     const map = try allocator.create(Mapping);
    //                     map.* = data;
    //                     break :mapping .{ .mapping = map };
    //                 },
    //                 // Alias => .{ .alias = data },
    //                 else => @compileError("unexpected data type"),
    //             },
    //         };
    //     }
    // };

    type Directive = ParserDirective<enc>;

    pub type Document = ParserDocument<enc>;

    pub type Stream = ParserStream<enc>;

    // fn Printer(comptime Writer: type) type {
    //     return struct {
    //         input: []const enc.unit(),
    //         stream: Stream,
    //         indent: Indent,
    //         writer: Writer,

    //         allocator: std.mem.Allocator,

    //         pub fn print(this: *@This()) Writer.Error!void {
    //             if (this.stream.docs.items.len == 0) {
    //                 return;
    //             }

    //             var first = true;

    //             for (this.stream.docs.items) |doc| {
    //                 try this.printDocument(&doc, first);
    //                 try this.writer.writeByte('\n');
    //                 first = false;

    //                 if (this.stream.docs.items.len != 1) {
    //                     try this.writer.writeAll("...\n");
    //                 }
    //             }
    //         }

    //         pub fn printDocument(this: *@This(), doc: *const Document, first: bool) Writer.Error!void {
    //             for (doc.directives.items) |directive| {
    //                 switch (directive) {
    //                     .yaml => {
    //                         try this.writer.writeAll("%YAML X.X\n");
    //                     },
    //                     .tag => |tag| {
    //                         try this.writer.print("%TAG {s} {s}{s}\n", .{
    //                             switch (tag.handle) {
    //                                 .named => |name| name.slice(this.input),
    //                                 .secondary => "!!",
    //                                 .primary => "!",
    //                             },
    //                             if (tag.prefix == .local) "!" else "",
    //                             switch (tag.prefix) {
    //                                 .local => |local| local.slice(this.input),
    //                                 .global => |global| global.slice(this.input),
    //                             },
    //                         });
    //                     },
    //                     .reserved => |reserved| {
    //                         try this.writer.print("%{s}\n", .{reserved.slice(this.input)});
    //                     },
    //                 }
    //             }

    //             if (!first or doc.directives.items.len != 0) {
    //                 try this.writer.writeAll("---\n");
    //             }

    //             try this.printNode(doc.root);
    //         }

    //         pub fn printString(this: *@This(), str: []const enc.unit()) Writer.Error!void {
    //             const quote = quote: {
    //                 if (true) {
    //                     break :quote true;
    //                 }
    //                 if (str.len == 0) {
    //                     break :quote true;
    //                 }

    //                 if (str[str.len - 1] == ' ') {
    //                     break :quote true;
    //                 }

    //                 for (str, 0..) |c, i| {
    //                     if (i == 0) {
    //                         switch (c) {
    //                             '&',
    //                             '*',
    //                             '?',
    //                             '|',
    //                             '-',
    //                             '<',
    //                             '>',
    //                             '=',
    //                             '!',
    //                             '%',
    //                             '@',

    //                             ' ',
    //                             => break :quote true,
    //                             else => {},
    //                         }
    //                         continue;
    //                     }

    //                     switch (c) {
    //                         '{',
    //                         '}',
    //                         '[',
    //                         ']',
    //                         ',',
    //                         '#',
    //                         '`',
    //                         '"',
    //                         '\'',
    //                         '\\',
    //                         '\t',
    //                         '\n',
    //                         '\r',
    //                         => break :quote true,

    //                         0x00...0x06,
    //                         0x0e...0x1a,
    //                         0x1c...0x1f,
    //                         => break :quote true,

    //                         't', 'T' => {
    //                             const r = str[i + 1 ..];
    //                             if (std.mem.startsWith(enc.unit(), r, "rue")) {
    //                                 break :quote true;
    //                             }
    //                             if (std.mem.startsWith(enc.unit(), r, "RUE")) {
    //                                 break :quote true;
    //                             }
    //                         },

    //                         'f', 'F' => {
    //                             const r = str[i + 1 ..];
    //                             if (std.mem.startsWith(enc.unit(), r, "alse")) {
    //                                 break :quote true;
    //                             }
    //                             if (std.mem.startsWith(enc.unit(), r, "ALSE")) {
    //                                 break :quote true;
    //                             }
    //                         },

    //                         '~' => break :quote true,
    //                         // 'n', 'N' => break :quote true,
    //                         // 'y', 'Y' => break :quote true,

    //                         'o', 'O' => {
    //                             const r = str[i + 1 ..];
    //                             if (std.mem.startsWith(enc.unit(), r, "ff")) {
    //                                 break :quote true;
    //                             }
    //                             if (std.mem.startsWith(enc.unit(), r, "FF")) {
    //                                 break :quote true;
    //                             }
    //                         },

    //                         // TODO: is this one needed
    //                         '.' => break :quote true,

    //                         // '0'...'9' => break :quote true,

    //                         else => {},
    //                     }
    //                 }

    //                 break :quote false;
    //             };

    //             if (!quote) {
    //                 try this.writer.writeAll(str);
    //                 return;
    //             }

    //             try this.writer.writeByte('"');

    //             var i: usize = 0;
    //             while (i < str.len) : (i += 1) {
    //                 const c = str[i];

    //                 // Check for UTF-8 multi-byte sequences for line/paragraph separators
    //                 if (enc == .utf8 and c == 0xe2 and i + 2 < str.len) {
    //                     if (str[i + 1] == 0x80) {
    //                         if (str[i + 2] == 0xa8) {
    //                             // U+2028 Line separator
    //                             try this.writer.writeAll("\\L");
    //                             i += 2;
    //                             continue;
    //                         } else if (str[i + 2] == 0xa9) {
    //                             // U+2029 Paragraph separator
    //                             try this.writer.writeAll("\\P");
    //                             i += 2;
    //                             continue;
    //                         }
    //                     }
    //                 }

    //                 // Check for UTF-8 sequences for NEL (U+0085) and NBSP (U+00A0)
    //                 if (enc == .utf8 and c == 0xc2 and i + 1 < str.len) {
    //                     if (str[i + 1] == 0x85) {
    //                         // U+0085 Next line
    //                         try this.writer.writeAll("\\N");
    //                         i += 1;
    //                         continue;
    //                     } else if (str[i + 1] == 0xa0) {
    //                         // U+00A0 Non-breaking space
    //                         try this.writer.writeAll("\\_");
    //                         i += 1;
    //                         continue;
    //                     }
    //                 }

    //                 const escaped = switch (c) {
    //                     // Standard escape sequences
    //                     '\\' => "\\\\",
    //                     '"' => "\\\"",
    //                     '\n' => "\\n",

    //                     // Control characters that need hex escaping
    //                     0x00 => "\\0",
    //                     0x01 => "\\x01",
    //                     0x02 => "\\x02",
    //                     0x03 => "\\x03",
    //                     0x04 => "\\x04",
    //                     0x05 => "\\x05",
    //                     0x06 => "\\x06",
    //                     0x07 => "\\a", // Bell
    //                     0x08 => "\\b", // Backspace
    //                     0x09 => "\\t", // Tab
    //                     0x0b => "\\v", // Vertical tab
    //                     0x0c => "\\f", // Form feed
    //                     0x0d => "\\r", // Carriage return
    //                     0x0e => "\\x0e",
    //                     0x0f => "\\x0f",
    //                     0x10 => "\\x10",
    //                     0x11 => "\\x11",
    //                     0x12 => "\\x12",
    //                     0x13 => "\\x13",
    //                     0x14 => "\\x14",
    //                     0x15 => "\\x15",
    //                     0x16 => "\\x16",
    //                     0x17 => "\\x17",
    //                     0x18 => "\\x18",
    //                     0x19 => "\\x19",
    //                     0x1a => "\\x1a",
    //                     0x1b => "\\e", // Escape
    //                     0x1c => "\\x1c",
    //                     0x1d => "\\x1d",
    //                     0x1e => "\\x1e",
    //                     0x1f => "\\x1f",
    //                     0x7f => "\\x7f", // Delete

    //                     0x20...0x21,
    //                     0x23...0x5b,
    //                     0x5d...0x7e,
    //                     => &.{c},

    //                     0x80...std.math.maxInt(enc.unit()) => &.{c},
    //                 };

    //                 try this.writer.writeAll(escaped);
    //             }

    //             try this.writer.writeByte('"');
    //         }

    //         pub fn printNode(this: *@This(), node: Node) Writer.Error!void {
    //             switch (node.data) {
    //                 .scalar => |scalar| {
    //                     switch (scalar) {
    //                         .null => {
    //                             try this.writer.writeAll("null");
    //                         },
    //                         .boolean => |boolean| {
    //                             try this.writer.print("{}", .{boolean});
    //                         },
    //                         .number => |number| {
    //                             try this.writer.print("{d}", .{number});
    //                         },
    //                         .string => |string| {
    //                             try this.printString(string.slice(this.input));
    //                         },
    //                     }
    //                 },
    //                 .sequence => |sequence| {
    //                     for (sequence.list.items, 0..) |item, i| {
    //                         try this.writer.writeAll("- ");
    //                         this.indent.inc(2);
    //                         try this.printNode(item);
    //                         this.indent.dec(2);

    //                         if (i + 1 != sequence.list.items.len) {
    //                             try this.writer.writeByte('\n');
    //                             try this.printIndent();
    //                         }
    //                     }
    //                 },
    //                 .mapping => |mapping| {
    //                     for (mapping.keys.items, mapping.values.items, 0..) |key, value, i| {
    //                         try this.printNode(key);
    //                         try this.writer.writeAll(": ");

    //                         this.indent.inc(1);

    //                         if (value.data == .mapping) {
    //                             try this.writer.writeByte('\n');
    //                             try this.printIndent();
    //                         }

    //                         try this.printNode(value);

    //                         this.indent.dec(1);

    //                         if (i + 1 != mapping.keys.items.len) {
    //                             try this.writer.writeByte('\n');
    //                             try this.printIndent();
    //                         }
    //                     }
    //                 },
    //             }
    //         }

    //         pub fn printIndent(this: *@This()) Writer.Error!void {
    //             for (0..this.indent.cast()) |_| {
    //                 try this.writer.writeByte(' ');
    //             }
    //         }
    //     };
    // }
}

pub enum Encoding {
    latin1,
    utf8,
    utf16,

    static fn unit(comptime encoding: Encoding): type {
        return switch (encoding) {
            .latin1 => u8,
            .utf8 => u8,
            .utf16 => u16,
        };
    }

    // fn Unit(comptime T: type) type {
    //     return enum(T) {

    //         _,
    //     };
    // }

    static fn literal(comptime encoding: Encoding, comptime str: []const u8): []const encoding.unit() {
        return switch (encoding) {
            .latin1 => str,
            .utf8 => str,
            .utf16 => std.unicode.utf8ToUtf16LeStringLiteral(str),
        };
    }

    static fn chars(comptime encoding: Encoding): type {
        return switch (encoding) {
            .latin1 => EncodingCharsLatin1,
            .utf8 => EncodingCharsUtf8,
            .utf16 => EncodingCharsUtf16,
        };
    }
}

pub struct Token<const encoding: Encoding> {
    const NodeTag = Parser(encoding).NodeTag;
    const NodeScalar = Parser(encoding).NodeScalar;
    const String = Parser(encoding).String;

    start: Pos,
    indent: Indent,
    line: Line,
    data: Data,


    pub static fn eof(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .eof,
        };
    }

    pub static fn sequenceEntry(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .sequence_entry,
        };
    }

    pub static fn mappingKey(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .mapping_key,
        };
    }

    pub static fn mappingValue(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .mapping_value,
        };
    }

    pub static fn collectEntry(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .collect_entry,
        };
    }

    pub static fn sequenceStart(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .sequence_start,
        };
    }

    pub static fn sequenceEnd(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .sequence_end,
        };
    }

    pub static fn mappingStart(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .mapping_start,
        };
    }

    pub static fn mappingEnd(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .mapping_end,
        };
    }

    type AnchorInit = TokenAnchorInit<encoding>;

    pub static fn anchor(init: AnchorInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: { anchor: init.name },
        };
    }

    type AliasInit = TokenAliasInit<encoding>;

    pub static fn alias(init: AliasInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: { alias: init.name },
        };
    }

    type TagInit = TokenTagInit<encoding>;

    pub static fn tag(init: TagInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: { tag: init.tag },
        };
    }

    pub static fn directive(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .directive,
        };
    }

    pub static fn reserved(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .reserved,
        };
    }

    pub static fn documentStart(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .document_start,
        };
    }

    pub static fn documentEnd(init: TokenInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: .document_end,
        };
    }

    type ScalarInit = TokenScalarInit<encoding>;

    pub static fn scalar(init: ScalarInit): @This() {
        return {
            start: init.start,
            indent: init.indent,
            line: init.line,
            data: { scalar: init.resolved },
        };
    }

    pub type Data = TokenData<encoding>;

    pub type Scalar = TokenScalar<encoding>;
}


import bun from "bun";
const Environment = bun.Environment;
const OOM = bun.OOM;
const logger = bun.logger;

const ast = bun.ast;
const E = ast.E;
const Expr = ast.Expr;
const G = ast.G;
