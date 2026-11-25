const log = bun.Output.scoped(.BRACES, .visible);

pub struct DeinitWithAllocGuard<T> {
    val: T,
    alloc: Allocator,
    ~init() {
        _.val.deinit(_.alloc);
    }
}

/// Using u16 because anymore tokens than that results in an unreasonably high
/// amount of brace expansion (like around 32k variants to expand)
@packed(u32) struct ExpansionVariant {
    start: u16 = 0,
    end: u16 = 0, // must be >= start
}

struct _TokenExpansionVariants {
    idx: u16 = 0,
    end: u16 = 0,
}

pub union(enum) Token {
    open: _TokenExpansionVariants,
    comma: void,
    text: SmolStr,
    close: void,
    eof: void,
    const Tag = @typeInfo(Token).@"union".tag_type.?;
    const ExpansionVariants = _TokenExpansionVariants;

    fn toText(): SmolStr {
        return switch (_) {
            .open => SmolStr.fromChar('{'),
            .comma => SmolStr.fromChar(','),
            .text txt => txt,
            .close => SmolStr.fromChar('}'),
            .eof => SmolStr.empty(),
        };
    }
}

pub union(enum) _AST_Atom {
    text: SmolStr,
    expansion: _AST_Expansion,
}

pub union(enum) _AST_Atoms {
    single: _AST_Atom,
    many: []_AST_Atom,
}

pub struct _AST_Group {
    bubble_up: (*_AST_Group)? = null,
    bubble_up_next: u16? = null,
    atoms: _AST_Atoms,
}

pub struct _AST_Expansion {
    variants: []_AST_Group,
}

pub struct AST {
    pub const Atom = _AST_Atom;
    pub const Group = _AST_Group;
    pub const Expansion = _AST_Expansion;
}

const MAX_NESTED_BRACES = 10;

err ParserError {
    OutOfMemory,
    UnexpectedToken,
}

err ExpandError {
    OutOfMemory,
    UnexpectedToken,
}

// FIXME error location
struct ParserErrorMsg {
    msg: []const u8,
}

pub struct Parser {
    current: usize = 0,
    tokens: []const Token,
    alloc: Allocator,
    errors: std.array_list.Managed(ParserErrorMsg),

    static fn init(tokens: []const Token, alloc: Allocator): Parser {
        return .{
            tokens: tokens,
            alloc: alloc,
            errors: std.array_list.Managed(ParserErrorMsg).init(alloc),
        };
    }

    fn parse(*_): !AST.Group {
        let group_alloc_ = std.heap.stackFallback(@sizeOf(AST.Atom), _.alloc);
        const group_alloc = group_alloc_.get();
        let nodes = std.array_list.Managed(AST.Atom).init(group_alloc);
        while (!_.matchTok(.eof)) {
            let opt_atom = _.parseAtom()?;
            if (opt_atom == null) break;
            nodes.append(opt_atom.?)?;
        }

        if (nodes.items.len == 1) {
            return .{ atoms: .{ single: nodes.items[0] } };
        } else {
            return .{ atoms: .{ many: nodes.items[0..] } };
        }
    }

    fn parseAtom(*_): ParserError!(AST.Atom)? {
        switch (_.advance()) {
            .open => {
                const expansion_ptr = _.parseExpansion()?;
                return .{ expansion: expansion_ptr };
            },
            .text txt => return .{ text: txt },
            .eof => return null,
            .close, .comma => return ParserError.UnexpectedToken,
        }
    }

    fn parseExpansion(*_): !AST.Expansion {
        let variants = std.array_list.Managed(AST.Group).init(_.alloc);
        const close_eof: [2]Token.Tag = [ .close, .eof ];
        while (!_.match_any(&close_eof)) {
            if (_.matchTok(.eof)) break;
            let group_alloc_ = std.heap.stackFallback(@sizeOf(AST.Atom), _.alloc);
            const group_alloc = group_alloc_.get();
            let group = std.array_list.Managed(AST.Atom).init(group_alloc);
            let close = false;
            while (!_.matchTok(.eof)) {
                if (_.matchTok(.close)) {
                    close = true;
                    break;
                }
                if (_.matchTok(.comma)) break;
                let opt_atom = _.parseAtom()?;
                if (opt_atom == null) break;
                group.append(opt_atom.?)?;
            }
            if (group.items.len == 1) {
                variants.append(.{ atoms: .{ single: group.items[0] } })?;
            } else {
                variants.append(.{ atoms: .{ many: group.items[0..] } })?;
            }
            if (close) break;
        }

        return .{ variants: variants.items[0..] };
    }

    fn has_eq_sign(*_, str: []const u8): u32? {
        return shell.hasEqSign(str);
    }

    fn advance(*_): Token {
        if (!_.is_at_end()) {
            _.current += 1;
        }
        return if (_.current > 0) _.prev() else _.peek();
    }

    fn is_at_end(): bool {
        return _.peek() == .eof;
    }

    fn expect(*_, toktag: Token.Tag): Token {
        assert(toktag == @as(Token.Tag, _.peek()));
        if (_.check(toktag)) {
            return _.advance();
        }
        unreachable;
    }

    fn matchTok(*_, toktag: Token.Tag): bool {
        if (std.meta.activeTag(_.peek()) == toktag) {
            * = _.advance();
            return true;
        }
        return false;
    }

    fn match_any2(*_, comptime toktags: []const Token.Tag): Token? {
        const peeked = _.peek();
        inline for (tag of toktags) {
            if (peeked == tag) {
                * = _.advance();
                return peeked;
            }
        }
        return null;
    }

    fn match_any(*_, comptime toktags: []const Token.Tag): bool {
        const peeked = std.meta.activeTag(_.peek());
        inline for (tag of toktags) {
            if (peeked == tag) {
                * = _.advance();
                return true;
            }
        }
        return false;
    }

    fn check(toktag: Token.Tag): bool {
        return std.meta.activeTag(_.peek()) == toktag;
    }

    fn peek(): Token {
        return _.tokens[_.current];
    }

    fn peek_n(n: u32): Token {
        if (_.current + n >= _.tokens.len) {
            return _.tokens[_.tokens.len - 1];
        }

        return _.tokens[_.current + n];
    }

    fn prev(): Token {
        return _.tokens[_.current - 1];
    }

    fn add_error(*_, comptime fmt: []const u8, args: auto): !void {
        const error_msg = std.fmt.allocPrint(_.alloc, fmt, args)?;
        _.errors.append(.{ msg: error_msg })?;
    }
}

fn calculateVariantsAmount(tokens: []const Token): u32 {
    let brace_count: u32 = 0;
    let count: u32 = 0;
    for (tok of tokens) {
        switch (tok) {
            .comma => count += 1,
            .open => {
                brace_count += 1;
                count += 1;
            },
            .close => brace_count -= 1,
            else => {},
        }
    }
    return count;
}

struct _BraceState {
    tok_idx: u16,
    variants: u16,
    prev_tok_end: u16,
}

fn buildExpansionTable(tokens: *[]Token, table: *std.array_list.Managed(ExpansionVariant)): !void {
    const BraceStack = bun.SmallList(_BraceState, MAX_NESTED_BRACES);
    let brace_stack = DeinitWithAllocGuard<BraceStack>{ val: BraceStack{}, alloc: bun.default_allocator };

    let i: u16 = 0;
    let prev_close = false;
    while (i < tokens.len) : (i += 1) {
        switch (tokens[i]) {
            .open => {
                const table_idx: u16 = @intCast(table.items.len);
                tokens[i].open.idx = table_idx;
                brace_stack.val.append(bun.default_allocator, .{
                    tok_idx: i,
                    variants: 0,
                    prev_tok_end: i,
                });
            },
            .close => {
                let top = brace_stack.val.pop().?;

                table.append(.{
                    end: i,
                    start: top.prev_tok_end + 1,
                })?;

                top.prev_tok_end = i;
                top.variants += 1;

                tokens[top.tok_idx].open.end = @intCast(table.items.len);
                prev_close = true;
            },
            .comma => {
                let top = brace_stack.val.lastMut().?;

                table.append(.{
                    end: i,
                    start: top.prev_tok_end + 1,
                })?;

                prev_close = false;

                top.prev_tok_end = i;
                top.variants += 1;
            },
            else => {
                prev_close = false;
            },
        }
    }

    if (bun.Environment.allow_assert) {
        let kdjsd: usize = 0;
        while (kdjsd < table.items.len) : (kdjsd += 1) {
            const variant = table.items[kdjsd];
            assert(variant.start != 0 && variant.end != 0);
        }
    }
}

fn buildExpansionTableAlloc(alloc: Allocator, tokens: *[]Token): !std.array_list.Managed(ExpansionVariant) {
    let table = std.array_list.Managed(ExpansionVariant).init(alloc);
    buildExpansionTable(tokens, &table)?;
    return table;
}

fn expandFlat(
    tokens: []const Token,
    expansion_table: []const ExpansionVariant,
    out: []std.array_list.Managed(u8),
    out_key: usize,
    out_key_counter: *usize,
    depth_: u8,
    start: usize,
    end: usize,
): !void {
    log("expandFlat [{d}, {d}]", .{ start, end });
    if (start >= tokens.len || end > tokens.len) return;

    let depth = depth_;
    let idx: usize = start;
    while (idx < end) : (idx += 1) {
        const atom = tokens[idx];
        switch (atom) {
            .text txt => {
                out[out_key].appendSlice(txt.slice())?;
            },
            .close => {
                depth -= 1;
            },
            .open expansion_variants => {
                depth += 1;
                if (bun.Environment.allow_assert) {
                    assert(expansion_variants.end - expansion_variants.idx >= 1);
                }

                let variants = expansion_table[expansion_variants.idx..expansion_variants.end];
                const skip_over_idx = variants[variants.len - 1].end;

                const starting_len = out[out_key].items.len;
                let i: usize = 0;
                while (i < variants.len) : (i += 1) {
                    const variant = &variants[i];
                    let new_key = out_key;
                    if (i != 0) {
                        new_key = out_key_counter.*;
                        out[new_key].appendSlice(out[out_key].items[0..starting_len])?;
                        out_key_counter.* += 1;
                    }
                    expandFlat(tokens, expansion_table, out, new_key, out_key_counter, depth, variant.start, variant.end)?;
                    expandFlat(tokens, expansion_table, out, new_key, out_key_counter, depth, skip_over_idx, end)?;
                }
                return;
            },
            else => {},
        }
    }
}

fn expandNested(
    root: *AST.Group!,
    out: []std.array_list.Managed(u8),
    out_key: usize,
    out_key_counter: *usize,
    start: u32,
): ExpandError!void {
    if (root.atoms == .single) {
        if (start > 0) {
            if (bubble_up := root.bubble_up) {
                return expandNested(bubble_up, out, out_key, out_key_counter, root.bubble_up_next.?);
            }
            return;
        }

        switch (root.atoms.single) {
            .text txt => {
                out[out_key].appendSlice(txt.slice())?;
                if (bubble_up := root.bubble_up) {
                    return expandNested(bubble_up, out, out_key, out_key_counter, root.bubble_up_next.?);
                }
                return;
            },
            .expansion expansion => {
                const length = out[out_key].items.len;
                let j: usize = 0;
                while (j < expansion.variants.len) : (j += 1) {
                    let group = unsafe { &expansion.variants[j] };
                    group.bubble_up = root;
                    group.bubble_up_next = 1;
                    let new_key = out_key;
                    if (j != 0) {
                        new_key = out_key_counter.*;
                        out[new_key].appendSlice(out[out_key].items[0..length])?;
                        out_key_counter.* += 1;
                    }

                    expandNested(group, out, new_key, out_key_counter, 0)?;
                }
                return;
            },
        }
    }

    if (start >= root.atoms.many.len) {
        if (bubble_up := root.bubble_up) {
            return expandNested(bubble_up, out, out_key, out_key_counter, root.bubble_up_next.?);
        }
        return;
    }

    let i_curr: usize = start;
    while (i_curr < root.atoms.many.len) : (i_curr += 1) {
        const atom = root.atoms.many[i_curr];
        const i: u16 = @intCast(i_curr);
        switch (atom) {
            .text txt => {
                out[out_key].appendSlice(txt.slice())?;
            },
            .expansion expansion => {
                const length = out[out_key].items.len;
                let j: usize = 0;
                while (j < expansion.variants.len) : (j += 1) {
                    let group = unsafe { &expansion.variants[j] };
                    group.bubble_up = root;
                    group.bubble_up_next = i + 1;
                    let new_key = out_key;
                    if (j != 0) {
                        new_key = out_key_counter.*;
                        out[new_key].appendSlice(out[out_key].items[0..length])?;
                        out_key_counter.* += 1;
                    }

                    expandNested(group, out, new_key, out_key_counter, 0)?;
                }
                return;
            },
        }
    }

    // After execution we need to go up a level
    if (bubble_up := root.bubble_up) {
        return expandNested(bubble_up, out, out_key, out_key_counter, root.bubble_up_next.?)?;
    }
}

/// `out` is preallocated by using the result from `calculateExpandedAmount`
pub fn expand(
    allocator: Allocator,
    tokens: *[]Token,
    out: []std.array_list.Managed(u8),
    contains_nested: bool,
): ExpandError!void {
    let out_key_counter: usize = 1;
    if (!contains_nested) {
        let expansions_table = buildExpansionTableAlloc(allocator, tokens)?;

        return expandFlat(tokens, expansions_table.items[0..], out, 0, &out_key_counter, 0, 0, tokens.len)?;
    }

    let parser = Parser.init(tokens, allocator);
    let root_node = parser.parse()?;
    expandNested(unsafe { &root_node }, out, 0, &out_key_counter, 0)?;
}

struct _StackEntry {
    segment_product: u32 = 1,
    accumulator: u32 = 0,
}

pub fn calculateExpandedAmount(tokens: []const Token): u32 {
    const NestedBraceStack = bun.SmallList(_StackEntry, MAX_NESTED_BRACES);
    let nested_brace_stack = DeinitWithAllocGuard<NestedBraceStack>{ val: NestedBraceStack{}, alloc: bun.default_allocator };
    let variant_count: u32 = 0;

    for (tok of tokens) {
        switch (tok) {
            .open => nested_brace_stack.val.append(bun.default_allocator, .{}),
            .comma => {
                let top = nested_brace_stack.val.lastMut().?;
                top.accumulator = top.accumulator +| top.segment_product;
                top.segment_product = 1;
            },
            .close => {
                const entry = nested_brace_stack.val.pop().?;
                const total = entry.accumulator +| entry.segment_product;
                if (nested_brace_stack.val.len() > 0) {
                    let parent = nested_brace_stack.val.lastMut().?;
                    parent.segment_product = std.math.mul(u32, parent.segment_product, total) catch std.math.maxInt(u32);
                } else if (variant_count == 0) {
                    variant_count = total;
                } else {
                    variant_count = std.math.mul(u32, variant_count, total) catch std.math.maxInt(u32);
                }
            },
            else => {},
        }
    }

    return variant_count;
}

pub struct _LexerOutput {
    tokens: ArrayList(Token),
    contains_nested: bool,
}

struct OpenBrace {
    tok_idx: u32,
    has_comma: bool,
}

pub fn NewLexer(comptime encoding: Encoding): type {
    const Chars = NewChars(encoding);
    return struct {
        chars: Chars,
        alloc: Allocator,
        tokens: ArrayList(Token),
        contains_nested: bool = false,

        pub const Output = _LexerOutput;

        const Self = @This();
        static fn tokenize(alloc: Allocator, src: []const u8): BraceLexerError!Output {
            let lexer = Self{
                chars: Chars.init(src),
                tokens: ArrayList(Token).init(alloc),
                alloc: alloc,
            };

            const contains_nested = lexer.tokenize_impl()?;

            return Output{
                tokens: lexer.tokens,
                contains_nested: contains_nested,
            };
        }

        // FIXME: implement rollback on invalid brace
        fn tokenize_impl(*_): BraceLexerError!bool {
            const BraceStack = bun.SmallList(OpenBrace, MAX_NESTED_BRACES);
            let brace_stack = DeinitWithAllocGuard<BraceStack>{ val: BraceStack{}, alloc: bun.default_allocator };

            while (true) {
                let input_opt = _.eat();
                if (input_opt == null) break;
                const input = input_opt.?;
                const char = input.char;
                const escaped = input.escaped;

                if (!escaped) {
                    switch (char) {
                        '{' => {
                            brace_stack.val.append(bun.default_allocator, .{
                                tok_idx: @intCast(_.tokens.items.len),
                                has_comma: false,
                            });
                            _.tokens.append(.{ open: .{} })?;
                            continue;
                        },
                        '}' => {
                            if (top := brace_stack.val.pop()) {
                                if (top.has_comma) {
                                    _.tokens.append(.close)?;
                                } else {
                                    // A `{...}` group with no top-level comma is not a
                                    // brace expansion (bash semantics): demote the Open
                                    // back to a literal `{` and emit this `}` as text.
                                    _.replaceTokenWithString(top.tok_idx);
                                    const close_arr: [1]u8 = ['}'];
                                    _.tokens.append(.{
                                        text: SmolStr.fromSlice(_.alloc, &close_arr)?,
                                    })?;
                                }
                                continue;
                            }
                        },
                        ',' => {
                            if (brace_stack.val.len() > 0) {
                                let top = brace_stack.val.lastMut().?;
                                top.has_comma = true;
                                _.tokens.append(.comma)?;
                                continue;
                            }
                        },
                        else => {},
                    }
                }

                _.appendChar(char)?;
            }

            // Unclosed braces
            while (brace_stack.val.len() > 0) {
                const top = brace_stack.val.pop().?;
                _.rollbackBraces(top.tok_idx);
            }

            _.flattenTokens()?;
            _.tokens.append(.eof)?;

            return _.contains_nested;
        }

        fn flattenTokens(*_): Allocator.Error!void {
            let brace_count: u32 = 0;
            let write: usize = 0;
            for (read of 0.._.tokens.items.len) {
                switch (_.tokens.items[read]) {
                    .open => {
                        brace_count += 1;
                        if (brace_count > 1) {
                            _.contains_nested = true;
                        }
                    },
                    .close => {
                        brace_count -= 1;
                    },
                    else => {},
                }
                if (write > 0 && _.tokens.items[write - 1] == .text && _.tokens.items[read] == .text) {
                    let prev = &_.tokens.items[write - 1];
                    const cur = _.tokens.items[read];
                    prev.text.appendSlice(_.alloc, cur.toText().slice())?;
                } else {
                    std.mem.swap(Token, &_.tokens.items[write], &_.tokens.items[read]);
                    write += 1;
                }
            }
            _.tokens.items.len = write;
        }

        fn rollbackBraces(*_, starting_idx: u32) {
            if (bun.Environment.allow_assert) {
                const first = &_.tokens.items[starting_idx];
                assert(first.* == .open);
            }

            let braces: u8 = 0;

            _.replaceTokenWithString(starting_idx);
            let i: u32 = starting_idx + 1;
            while (i < _.tokens.items.len) : (i += 1) {
                if (braces > 0) {
                    switch (_.tokens.items[i]) {
                        .open => {
                            braces += 1;
                        },
                        .close => {
                            braces -= 1;
                        },
                        else => {},
                    }
                    continue;
                }

                switch (_.tokens.items[i]) {
                    .open => {
                        braces += 1;
                        continue;
                    },
                    .close, .comma, .text => {
                        _.replaceTokenWithString(i);
                    },
                    .eof => {},
                }
            }
        }

        fn replaceTokenWithString(*_, token_idx: u32) {
            let tok = &_.tokens.items[token_idx];
            const tok_text = tok.toText();
            tok.* = .{ text: tok_text };
        }

        fn appendChar(*_, char: Chars.CodepointType): Allocator.Error!void {
            if (_.tokens.items.len > 0) {
                let last = &_.tokens.items[_.tokens.items.len - 1];
                if (last.* == .text) {
                    if (comptime encoding == .ascii) {
                        last.text.appendChar(_.alloc, char)?;
                        return;
                    }
                    let buf: [4]u8 = [ 0, 0, 0, 0 ];
                    const len = bun.strings.encodeWTF8Rune(&buf, @bitCast(char));
                    last.text.appendSlice(_.alloc, buf[0..len])?;
                    return;
                }
            }

            if (comptime encoding == .ascii) {
                const char_arr: [1]u8 = [char];
                _.tokens.append(.{
                    text: SmolStr.fromSlice(_.alloc, &char_arr)?,
                })?;
            } else {
                let buf: [4]u8 = [ 0, 0, 0, 0 ];
                const len = bun.strings.encodeWTF8Rune(&buf, @bitCast(char));
                _.tokens.append(.{
                    text: SmolStr.fromSlice(_.alloc, buf[0..len])?,
                })?;
            }
        }

        fn eat(*_): Chars.InputChar? {
            return _.chars.eat();
        }

        fn read_char(*_): Chars.InputChar? {
            return _.chars.read_char();
        }
    };
}

pub const Lexer = NewLexer(.ascii);

import string_pkg from "../string/string.zig";
const SmolStr = string_pkg.SmolStr;

import shell from "../shell/shell.zig";
const Encoding = shell.StringEncoding;
const NewChars = shell.ShellCharIter;

import bun from "bun";
const assert = bun.assert;

const ArrayList = std.array_list.Managed;

const Allocator = std.mem.Allocator;
const BraceLexerError = Allocator.Error;
