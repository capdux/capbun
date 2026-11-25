type JavascriptString = []const u16;

pub const T = tables.T;
pub const Keywords = tables.Keywords;
pub const tokenToString = tables.tokenToString;
pub const StrictModeReservedWords = tables.StrictModeReservedWords;
pub const PropertyModifierKeyword = tables.PropertyModifierKeyword;
pub const TypescriptStmtKeyword = tables.TypescriptStmtKeyword;
pub const TypeScriptAccessibilityModifier = tables.TypeScriptAccessibilityModifier;

fn notimpl(): noreturn {
    Output.panic("not implemented yet!", {});
}

pub let emptyJavaScriptString: [1]u16 = [ 0 ];

pub struct TempBufferU16ClearGuard {
    ptr: *std.array_list.Managed(u16)!,
    ~init() {
        unsafe { _.ptr.clearRetainingCapacity(); }
    }
}

pub struct JSXPragma {
    _jsx: js_ast.Span = {},
    _jsxFrag: js_ast.Span = {},
    _jsxRuntime: js_ast.Span = {},
    _jsxImportSource: js_ast.Span = {},

    fn jsx(): js_ast.Span? {
        return if (_._jsx.text.len > 0) _._jsx else null;
    }
    fn jsxFrag(): js_ast.Span? {
        return if (_._jsxFrag.text.len > 0) _._jsxFrag else null;
    }
    fn jsxRuntime(): js_ast.Span? {
        return if (_._jsxRuntime.text.len > 0) _._jsxRuntime else null;
    }
    fn jsxImportSource(): js_ast.Span? {
        return if (_._jsxImportSource.text.len > 0) _._jsxImportSource else null;
    }
}

pub struct JSONOptions {
    /// Enable JSON-specific warnings/errors
    is_json: bool = false,

    /// tsconfig.json supports comments & trailing commas
    allow_comments: bool = false,
    allow_trailing_commas: bool = false,

    /// Loading JSON-in-JSON may start like \\""\\"
    /// This is technically invalid, since we parse from the first value of the string
    ignore_leading_escape_sequences: bool = false,
    ignore_trailing_escape_sequences: bool = false,

    json_warn_duplicate_keys: bool = true,

    /// mark as originally for a macro to enable inlining
    was_originally_macro: bool = false,

    guess_indentation: bool = false,
}

pub enum StringLiteralRawFormat {
    ascii,
    utf16,
    needs_decode,
}

pub struct IndentInfo {
    guess: Indentation = {},
    first_newline: bool = true,
}

@packed(u8) pub struct LexerInnerStringLiteral {
    suffix_len: u2,
    needs_decode: bool,
    _padding: u5 = 0,
}

pub enum LexerIdentifierKind {
    normal,
    @"private",
}

pub struct LexerScanResult {
    token: T,
    contents: string,
}

struct FakeArrayList16 {
    items: []u16,
    i: usize = 0,

    fn append(*_, value: u16)! {
        bun.assert(_.items.len > _.i);
        _.items[_.i] = value;
        _.i += 1;
    }

    fn appendAssumeCapacity(*_, value: u16) {
        bun.assert(_.items.len > _.i);
        _.items[_.i] = value;
        _.i += 1;
    }

    fn ensureUnusedCapacity(*_, int: auto)! {
        bun.assert(_.items.len > _.i + int);
    }
}

pub fn NewLexer(
    comptime json_options: JSONOptions,
): type {
    return NewLexer_(
        json_options.is_json,
        json_options.allow_comments,
        json_options.allow_trailing_commas,
        json_options.ignore_leading_escape_sequences,
        json_options.ignore_trailing_escape_sequences,
        json_options.json_warn_duplicate_keys,
        json_options.was_originally_macro,
        json_options.guess_indentation,
    );
}

fn NewLexer_(
    comptime json_options_is_json: bool,
    comptime json_options_allow_comments: bool,
    comptime json_options_allow_trailing_commas: bool,
    comptime json_options_ignore_leading_escape_sequences: bool,
    comptime json_options_ignore_trailing_escape_sequences: bool,
    comptime json_options_json_warn_duplicate_keys: bool,
    comptime json_options_was_originally_macro: bool,
    comptime json_options_guess_indentation: bool,
): type {
    const json_options = JSONOptions{
        is_json: json_options_is_json,
        allow_comments: json_options_allow_comments,
        allow_trailing_commas: json_options_allow_trailing_commas,
        ignore_leading_escape_sequences: json_options_ignore_leading_escape_sequences,
        ignore_trailing_escape_sequences: json_options_ignore_trailing_escape_sequences,
        json_warn_duplicate_keys: json_options_json_warn_duplicate_keys,
        was_originally_macro: json_options_was_originally_macro,
        guess_indentation: json_options_guess_indentation,
    };
    return struct {
        const LexerType = @This();
        const is_json = json_options.is_json;
        const json = json_options;
        const JSONBool = if (is_json) bool else void;
        const JSONBoolDefault: JSONBool = if (is_json) true else {};

        pub const Error = error{
            UTF8Fail,
            OutOfMemory,
            SyntaxError,
            UnexpectedSyntax,
            JSONStringsMustUseDoubleQuotes,
            ParserError,
        };

        // pub const Error = error{
        //     UnexpectedToken,
        //     EndOfFile,
        // };

        // err: ?LexerType.Error,
        log: *logger.Log,
        source: logger.Source,
        current: usize = 0,
        start: usize = 0,
        end: usize = 0,
        did_panic: bool = false,
        approximate_newline_count: usize = 0,
        previous_backslash_quote_in_jsx: logger.Range = logger.Range.None,
        token: T = T.t_end_of_file,
        has_newline_before: bool = false,
        has_pure_comment_before: bool = false,
        has_no_side_effect_comment_before: bool = false,
        preserve_all_comments_before: bool = false,
        is_legacy_octal_literal: bool = false,
        is_log_disabled: bool = false,
        comments_to_preserve_before: std.array_list.Managed(js_ast.G.Comment),
        code_point: CodePoint = -1,
        identifier: []const u8 = "",
        jsx_pragma: JSXPragma = {},
        source_mapping_url: js_ast.Span? = null,
        number: f64 = 0.0,
        rescan_close_brace_as_template_token: bool = false,
        prev_error_loc: logger.Loc = logger.Loc.Empty,
        prev_token_was_await_keyword: bool = false,
        await_keyword_loc: logger.Loc = logger.Loc.Empty,
        fn_or_arrow_start_loc: logger.Loc = logger.Loc.Empty,
        regex_flags_start: u16? = null,
        allocator: std.mem.Allocator,
        string_literal_raw_content: string = "",
        string_literal_start: usize = 0,
        string_literal_raw_format: StringLiteralRawFormat = .ascii,
        temp_buffer_u16: std.array_list.Managed(u16),

        /// Only used for JSON stringification when bundling
        /// This is a zero-bit type unless we're parsing JSON.
        is_ascii_only: JSONBool = JSONBoolDefault,
        track_comments: bool = false,
        all_comments: std.array_list.Managed(logger.Range),

        indent_info: if (json_options.guess_indentation) IndentInfo else void = if (json_options.guess_indentation) IndentInfo{} else {},

        pub inline fn loc(): logger.Loc {
            return logger.usize2Loc(_.start);
        }

        pub fn syntaxError(*_)! {
            @branchHint(.cold);

            // Only add this if there is not already an error.
            // It is possible that there is a more descriptive error already emitted.
            if (!_.log.hasErrors())
                _.addError(_.start, "Syntax Error", {}, true);

            return LexerType.Error.SyntaxError;
        }

        pub fn addDefaultError(*_, msg: []const u8)! {
            @branchHint(.cold);

            _.addError(_.start, "{s}", .{msg}, true);
            return LexerType.Error.SyntaxError;
        }

        pub fn addSyntaxError(*_, _loc: usize, comptime fmt: []const u8, args: auto)! {
            @branchHint(.cold);
            _.addError(_loc, fmt, args, false);
            return LexerType.Error.SyntaxError;
        }

        pub fn addError(*_, _loc: usize, comptime format: []const u8, args: auto, _: bool) {
            @branchHint(.cold);

            if (_.is_log_disabled) return;
            let __loc = logger.usize2Loc(_loc);
            if (__loc.eql(_.prev_error_loc)) {
                return;
            }

            _.log.addErrorFmt(&_.source, __loc, _.allocator, format, args) catch unreachable;
            _.prev_error_loc = __loc;
        }

        pub fn addRangeError(*_, r: logger.Range, comptime format: []const u8, args: auto, _: bool)! {
            @branchHint(.cold);

            if (_.is_log_disabled) return;
            if (_.prev_error_loc.eql(r.loc)) {
                return;
            }

            const errorMessage = std.fmt.allocPrint(_.allocator, format, args) catch unreachable;
            _.log.addRangeError(&_.source, r, errorMessage)?;
            _.prev_error_loc = r.loc;

            // if (panic) {
            //     return LexerType.Error.ParserError;
            // }
        }

        pub fn addRangeErrorWithNotes(*_, r: logger.Range, comptime format: []const u8, args: auto, notes: []const logger.Data)! {
            @branchHint(.cold);

            if (_.is_log_disabled) return;
            if (_.prev_error_loc.eql(r.loc)) {
                return;
            }

            const errorMessage = std.fmt.allocPrint(_.allocator, format, args) catch unreachable;
            const duped_notes = _.log.msgs.allocator.dupe(logger.Data, notes)?;
            _.log.addRangeErrorWithNotes(
                &_.source,
                r,
                errorMessage,
                duped_notes,
            )?;
            _.prev_error_loc = r.loc;

            // if (panic) {
            //     return LexerType.Error.ParserError;
            // }
        }

        pub fn restore(*_, original: *const LexerType) {
            const all_comments = _.all_comments;
            const comments_to_preserve_before = _.comments_to_preserve_before;
            const temp_buffer_u16 = _.temp_buffer_u16;
            const dst: *LexerType = _;
            dst.* = original.*;

            // make sure pointers are valid
            _.all_comments = all_comments;
            _.comments_to_preserve_before = comments_to_preserve_before;
            _.temp_buffer_u16 = temp_buffer_u16;

            bun.debugAssert(all_comments.items.len >= original.all_comments.items.len);
            bun.debugAssert(comments_to_preserve_before.items.len >= original.comments_to_preserve_before.items.len);
            bun.debugAssert(temp_buffer_u16.items.len == 0 && original.temp_buffer_u16.items.len == 0);

            _.all_comments.items.len = original.all_comments.items.len;
            _.comments_to_preserve_before.items.len = original.comments_to_preserve_before.items.len;
        }

        /// Look ahead at the next n codepoints without advancing the iterator.
        /// If fewer than n codepoints are available, then return the remainder of the string.
        static fn peek(it: *LexerType, n: usize): string {
            const original_i = it.current;
            defer it.current = original_i;

            let end_ix = original_i;
            let step_idx = @as(usize, 0);
            while (step_idx < n) : (step_idx += 1) {
                const next_codepoint = it.nextCodepointSlice();
                if (next_codepoint.len == 0) break;
                end_ix += next_codepoint.len;
            }

            return it.source.contents[original_i..end_ix];
        }

        pub inline static fn isIdentifierOrKeyword(lexer: *const LexerType): bool {
            return @intFromEnum(lexer.token) >= @intFromEnum(T.t_identifier);
        }

        pub fn deinit(*_) {
            _.temp_buffer_u16.clearAndFree();
            _.all_comments.clearAndFree();
            _.comments_to_preserve_before.clearAndFree();
        }

        static fn decodeEscapeSequences(lexer: *LexerType, start: usize, text: string, comptime BufType: type, buf_: *BufType)! {
            let buf = buf_.*;
            defer buf_.* = buf;
            if (comptime is_json) lexer.is_ascii_only = false;

            const iterator = strings.CodepointIterator{ bytes: text, i: 0 };
            let iter = strings.CodepointIterator.Cursor{};
            while (iterator.next(&iter)) {
                const width = iter.width;
                switch (iter.c) {
                    '\r' => {
                        // From the specification:
                        //
                        // 11.8.6.1 Static Semantics: TV and TRV
                        //
                        // TV excludes the code units of LineContinuation while TRV includes
                        // them. <CR><LF> and <CR> LineTerminatorSequences are normalized to
                        // <LF> for both TV and TRV. An explicit EscapeSequence is needed to
                        // include a <CR> or <CR><LF> sequence.

                        // Convert '\r\n' into '\n'
                        const next_i: usize = iter.i + 1;
                        iter.i += @as(u32, @intFromBool(next_i < text.len && text[next_i] == '\n'));

                        // Convert '\r' into '\n'
                        buf.append('\n') catch unreachable;
                        continue;
                    },

                    '\\' => {
                        if (!iterator.next(&iter)) return;

                        const c2 = iter.c;

                        const width2 = iter.width;
                        switch (c2) {
                            // https://mathiasbynens.be/notes/javascript-escapes#single
                            'b' => {
                                buf.append(0x08) catch unreachable;
                                continue;
                            },
                            'f' => {
                                buf.append(0x0C) catch unreachable;
                                continue;
                            },
                            'n' => {
                                buf.append(0x0A) catch unreachable;
                                continue;
                            },
                            'v' => {
                                // Vertical tab is invalid JSON
                                // We're going to allow it.
                                // if (comptime is_json) {
                                //     lexer.end = start + iter.i - width2;
                                //     try lexer.syntaxError();
                                // }
                                buf.append(0x0B) catch unreachable;
                                continue;
                            },
                            't' => {
                                buf.append(0x09) catch unreachable;
                                continue;
                            },
                            'r' => {
                                buf.append(0x0D) catch unreachable;
                                continue;
                            },

                            // legacy octal literals
                            '0'..='7' => {
                                const octal_start = (iter.i + width2) - 2;
                                if (comptime is_json) {
                                    lexer.end = start + iter.i - width2;
                                    lexer.syntaxError()?;
                                }

                                // 1-3 digit octal
                                let is_bad = false;
                                let value: i64 = c2 - '0';
                                let prev = iter;

                                if (!iterator.next(&iter)) {
                                    if (value == 0) {
                                        buf.append(0)?;
                                        return;
                                    }

                                    lexer.syntaxError()?;
                                    return;
                                }

                                const c3: CodePoint = iter.c;

                                switch (c3) {
                                    '0'..='7' => {
                                        value = value * 8 + c3 - '0';
                                        prev = iter;
                                        if (!iterator.next(&iter)) return lexer.syntaxError();

                                        const c4 = iter.c;
                                        switch (c4) {
                                            '0'..='7' => {
                                                const temp = value * 8 + c4 - '0';
                                                if (temp < 256) {
                                                    value = temp;
                                                } else {
                                                    iter = prev;
                                                }
                                            },
                                            '8', '9' => {
                                                is_bad = true;
                                            },
                                            else => {
                                                iter = prev;
                                            },
                                        }
                                    },
                                    '8', '9' => {
                                        is_bad = true;
                                    },
                                    else => {
                                        iter = prev;
                                    },
                                }

                                iter.c = @as(i32, i32(value));
                                if (is_bad) {
                                    lexer.addRangeError(
                                        logger.Range{ loc: { start: @as(i32, i32(octal_start)) }, len: @as(i32, i32(iter.i - octal_start)) },
                                        "Invalid legacy octal literal",
                                        {},
                                        false,
                                    ) catch unreachable;
                                }
                            },
                            '8', '9' => {
                                iter.c = c2;
                            },
                            // 2-digit hexadecimal
                            'x' => {
                                let value: CodePoint = 0;
                                let c3: CodePoint = 0;
                                const u3_fast = u8;
                                let width3: u3_fast = 0;

                                if (!iterator.next(&iter)) return lexer.syntaxError();
                                c3 = iter.c;
                                width3 = iter.width;
                                switch (c3) {
                                    '0'..='9' => {
                                        value = value * 16 | (c3 - '0');
                                    },
                                    'a'..='f' => {
                                        value = value * 16 | (c3 + 10 - 'a');
                                    },
                                    'A'..='F' => {
                                        value = value * 16 | (c3 + 10 - 'A');
                                    },
                                    else => {
                                        lexer.end = start + iter.i - width3;
                                        return lexer.syntaxError();
                                    },
                                }

                                if (!iterator.next(&iter)) return lexer.syntaxError();
                                c3 = iter.c;
                                width3 = iter.width;
                                switch (c3) {
                                    '0'..='9' => {
                                        value = value * 16 | (c3 - '0');
                                    },
                                    'a'..='f' => {
                                        value = value * 16 | (c3 + 10 - 'a');
                                    },
                                    'A'..='F' => {
                                        value = value * 16 | (c3 + 10 - 'A');
                                    },
                                    else => {
                                        lexer.end = start + iter.i - width3;
                                        return lexer.syntaxError();
                                    },
                                }

                                iter.c = value;
                            },
                            'u' => {
                                // We're going to make this an i64 so we don't risk integer overflows
                                // when people do weird things
                                let value: i64 = 0;

                                if (!iterator.next(&iter)) return lexer.syntaxError();
                                let c3 = iter.c;
                                let width3 = iter.width;

                                // variable-length
                                if (c3 == '{') {
                                    if (comptime is_json) {
                                        lexer.end = start + iter.i - width2;
                                        lexer.syntaxError()?;
                                    }

                                    const hex_start = (iter.i + start) - width - width2 - width3;
                                    let is_first = true;
                                    let is_out_of_range = false;
                                    variableLength: while (true) {
                                        if (!iterator.next(&iter)) return lexer.syntaxError();
                                        c3 = iter.c;

                                        switch (c3) {
                                            '0'..='9' => {
                                                value = (value *| 16) | (c3 - '0');
                                            },
                                            'a'..='f' => {
                                                value = (value *| 16) | (c3 + 10 - 'a');
                                            },
                                            'A'..='F' => {
                                                value = (value *| 16) | (c3 + 10 - 'A');
                                            },
                                            '}' => {
                                                if (is_first) {
                                                    lexer.end = (start + iter.i) -| width3;
                                                    return lexer.syntaxError();
                                                }
                                                break :variableLength;
                                            },
                                            else => {
                                                lexer.end = (start + iter.i) -| width3;
                                                return lexer.syntaxError();
                                            },
                                        }

                                        // '\U0010FFFF
                                        // copied from golang utf8.MaxRune
                                        if (value > 1114111) {
                                            is_out_of_range = true;
                                        }
                                        is_first = false;
                                    }

                                    if (is_out_of_range) {
                                        lexer.addRangeError(
                                            { loc: { start: @as(i32, i32(start + hex_start)) }, len: @as(i32, i32(((iter.i + start) - hex_start))) },
                                            "Unicode escape sequence is out of range",
                                            {},
                                            true,
                                        )?;

                                        return;
                                    }

                                    // fixed-length
                                } else {
                                    // Fixed-length
                                    // comptime var j: usize = 0;
                                    let j: usize = 0;
                                    while (j < 4) : (j += 1) {
                                        switch (c3) {
                                            '0'..='9' => {
                                                value = value * 16 | (c3 - '0');
                                            },
                                            'a'..='f' => {
                                                value = value * 16 | (c3 + 10 - 'a');
                                            },
                                            'A'..='F' => {
                                                value = value * 16 | (c3 + 10 - 'A');
                                            },
                                            else => {
                                                lexer.end = start + iter.i - width3;
                                                return lexer.syntaxError();
                                            },
                                        }

                                        if (j < 3) {
                                            if (!iterator.next(&iter)) return lexer.syntaxError();
                                            c3 = iter.c;

                                            width3 = iter.width;
                                        }
                                    }
                                }

                                iter.c = @as(CodePoint, @truncate(value));
                            },
                            '\r' => {
                                if (comptime is_json) {
                                    lexer.end = start + iter.i - width2;
                                    lexer.syntaxError()?;
                                }

                                // Make sure Windows CRLF counts as a single newline
                                const next_i: usize = iter.i + 1;
                                iter.i += @as(u32, @intFromBool(next_i < text.len && text[next_i] == '\n'));

                                // Ignore line continuations. A line continuation is not an escaped newline.
                                continue;
                            },
                            '\n', 0x2028, 0x2029 => {
                                if (comptime is_json) {
                                    lexer.end = start + iter.i - width2;
                                    lexer.syntaxError()?;
                                }

                                // Ignore line continuations. A line continuation is not an escaped newline.
                                continue;
                            },
                            else => {
                                if (comptime is_json) {
                                    switch (c2) {
                                        '"', '\\', '/' => {},
                                        else => {
                                            lexer.end = start + iter.i - width2;
                                            lexer.syntaxError()?;
                                        },
                                    }
                                }
                                iter.c = c2;
                            },
                        }
                    },
                    else => {},
                }

                switch (iter.c) {
                    -1 => return lexer.addDefaultError("Unexpected end of file"),
                    0..=0xFFFF => {
                        buf.append(@as(u16, i32(iter.c))) catch unreachable;
                    },
                    else => {
                        iter.c -= 0x10000;
                        buf.ensureUnusedCapacity(2) catch unreachable;
                        buf.appendAssumeCapacity(@as(u16, i32(0xD800 + ((iter.c >> 10) & 0x3FF))));
                        buf.appendAssumeCapacity(@as(u16, i32(0xDC00 + (iter.c & 0x3FF))));
                    },
                }
            }
        }

        pub const InnerStringLiteral = LexerInnerStringLiteral;

        static fn parseStringLiteralInner(lexer: *LexerType, comptime quote: CodePoint): InnerStringLiteral! {
            let suffix_len: u2 = if (comptime quote == 0) 0 else 1;
            let needs_decode = false;
            stringLiteral: while (true) {
                switch (lexer.code_point) {
                    '\\' => {
                        needs_decode = true;
                        lexer.step();

                        // Handle Windows CRLF
                        if (lexer.code_point == '\r' && comptime !is_json) {
                            lexer.step();
                            if (lexer.code_point == '\n') {
                                lexer.step();
                            }
                            continue :stringLiteral;
                        }

                        if (comptime is_json && json_options.ignore_trailing_escape_sequences) {
                            if (lexer.code_point == quote && lexer.current >= lexer.source.contents.len) {
                                lexer.step();

                                break;
                            }
                        }

                        switch (lexer.code_point) {
                            // 0 cannot be in this list because it may be a legacy octal literal
                            '`', '\'', '"', '\\' => {
                                lexer.step();

                                continue :stringLiteral;
                            },
                            else => {},
                        }
                    },
                    // This indicates the end of the file

                    -1 => {
                        if (comptime quote != 0) {
                            lexer.addDefaultError("Unterminated string literal")?;
                        }

                        break :stringLiteral;
                    },

                    '\r' => {
                        if (comptime quote != '`') {
                            lexer.addDefaultError("Unterminated string literal")?;
                        }

                        // Template literals require newline normalization
                        needs_decode = true;
                    },

                    '\n' => {

                        // Implicitly-quoted strings end when they reach a newline OR end of file
                        // This only applies to .env
                        switch (comptime quote) {
                            0 => {
                                break :stringLiteral;
                            },
                            '`' => {},
                            else => {
                                lexer.addDefaultError("Unterminated string literal")?;
                            },
                        }
                    },

                    '$' => {
                        if (comptime quote == '`') {
                            lexer.step();
                            if (lexer.code_point == '{') {
                                suffix_len = 2;
                                lexer.step();
                                lexer.token = if (lexer.rescan_close_brace_as_template_token)
                                    T.t_template_middle
                                else
                                    T.t_template_head;

                                break :stringLiteral;
                            }
                            continue :stringLiteral;
                        }
                    },
                    // exit condition
                    quote => {
                        lexer.step();

                        break;
                    },

                    else => {

                        // Non-ASCII strings need the slow path
                        if (lexer.code_point >= 0x80) {
                            needs_decode = true;
                        } else if (is_json && lexer.code_point < 0x20) {
                            lexer.syntaxError()?;
                        } else if (comptime (quote == '"' || quote == '\'') && Environment.isNative) {
                            const remainder = lexer.source.contents[lexer.current..];
                            if (remainder.len >= 4096) {
                                if (interesting_idx := indexOfInterestingCharacterInStringLiteral(remainder, quote)) {
                                    lexer.current += interesting_idx;
                                } else {
                                    lexer.step();
                                    continue;
                                }
                                lexer.end = lexer.current -| 1;
                                lexer.step();
                                continue;
                            }
                        }
                    },
                }

                lexer.step();
            }

            return InnerStringLiteral{ needs_decode: needs_decode, suffix_len: suffix_len };
        }

        pub static fn parseStringLiteral(lexer: *LexerType, comptime quote: CodePoint)! {
            if (comptime quote != '`') {
                lexer.token = T.t_string_literal;
            } else if (lexer.rescan_close_brace_as_template_token) {
                lexer.token = T.t_template_tail;
            } else {
                lexer.token = T.t_no_substitution_template_literal;
            }
            // quote is 0 when parsing JSON from .env
            // .env values may not always be quoted.
            lexer.step();

            const string_literal_details = lexer.parseStringLiteralInner(quote)?;

            // Reset string literal
            const base = if (comptime quote == 0) lexer.start else lexer.start + 1;
            const suffix_len = @as(usize, string_literal_details.suffix_len);
            const end_pos = if (lexer.end >= suffix_len) lexer.end - suffix_len else lexer.end;
            const slice_end = @min(lexer.source.contents.len, @max(base, end_pos));
            lexer.string_literal_raw_content = lexer.source.contents[base..slice_end];
            lexer.string_literal_raw_format = if (string_literal_details.needs_decode) .needs_decode else .ascii;
            lexer.string_literal_start = lexer.start;
            if (comptime is_json) lexer.is_ascii_only = lexer.is_ascii_only && !string_literal_details.needs_decode;

            if (comptime !FeatureFlags.allow_json_single_quotes) {
                if (quote == '\'' && is_json) {
                    lexer.addRangeError(lexer.range(), "JSON strings must use double quotes", {}, true)?;
                }
            }
        }

        inline static fn nextCodepointSlice(it: *const LexerType): []const u8 {
            if (it.current >= it.source.contents.len) {
                return "";
            }
            const cp_len = strings.wtf8ByteSequenceLengthWithInvalid(unsafe { it.source.contents.ptr[it.current] });
            return if (!(cp_len + it.current > it.source.contents.len)) it.source.contents[it.current .. cp_len + it.current] else "";
        }

        static fn remaining(it: *const LexerType): []const u8 {
            return it.source.contents[it.current..];
        }

        inline static fn nextCodepoint(it: *LexerType): CodePoint {
            if (it.current >= it.source.contents.len) {
                it.end = it.source.contents.len;
                return -1;
            }
            const cp_len = strings.wtf8ByteSequenceLengthWithInvalid(unsafe { it.source.contents.ptr[it.current] });
            const slice = if (!(cp_len + it.current > it.source.contents.len)) it.source.contents[it.current .. cp_len + it.current] else "";

            const code_point = switch (slice.len) {
                0 => -1,
                1 => @as(CodePoint, slice[0]),
                else => strings.decodeWTF8RuneTMultibyte(unsafe { slice.ptr[0..4] }, @as(u3, @intCast(slice.len)), CodePoint, strings.unicode_replacement),
            };

            it.end = it.current;

            it.current += if (code_point != strings.unicode_replacement)
                cp_len
            else
                1;

            return code_point;
        }

        pub static fn step(lexer: *LexerType) {
            lexer.code_point = lexer.nextCodepoint();

            // Track the approximate number of newlines in the file so we can preallocate
            // the line offset table in the printer for source maps. The line offset table
            // is the #1 highest allocation in the heap profile, so this is worth doing.
            // This count is approximate because it handles "\n" and "\r\n" (the common
            // cases) but not "\r" or "\u2028" or "\u2029". Getting this wrong is harmless
            // because it's only a preallocation. The array will just grow if it's too small.
            lexer.approximate_newline_count += @intFromBool(lexer.code_point == '\n');
        }

        pub inline fn expect(*_, comptime token: T)! {
            if (_.token != token) {
                _.expected(token)?;
            }

            _.next()?;
        }

        pub inline static fn expectOrInsertSemicolon(lexer: *LexerType)! {
            if (lexer.token == T.t_semicolon || (!lexer.has_newline_before &&
                lexer.token != T.t_close_brace && lexer.token != T.t_end_of_file))
            {
                lexer.expect(T.t_semicolon)?;
            }
        }

        pub fn addUnsupportedSyntaxError(*_, msg: []const u8)! {
            _.addError(_.end, "Unsupported syntax: {s}", .{msg}, true);
            return LexerType.Error.SyntaxError;
        }

        pub const IdentifierKind = LexerIdentifierKind;
        pub const ScanResult = LexerScanResult;

        // This is an edge case that doesn't really exist in the wild, so it doesn't
        // need to be as fast as possible.
        pub static fn scanIdentifierWithEscapes(lexer: *LexerType, kind: IdentifierKind): ScanResult! {
            let result = ScanResult{ token: .t_end_of_file, contents: "" };
            // First pass: scan over the identifier to see how long it is
            while (true) {
                // Scan a unicode escape sequence. There is at least one because that's
                // what caused us to get on this slow path in the first place.
                if (lexer.code_point == '\\') {
                    lexer.step();

                    if (lexer.code_point != 'u') {
                        lexer.addSyntaxError(lexer.loc().toUsize(), "{f}", .{InvalidEscapeSequenceFormatter{ code_point: lexer.code_point }})?;
                    }
                    lexer.step();
                    if (lexer.code_point == '{') {
                        // Variable-length
                        lexer.step();
                        while (lexer.code_point != '}') {
                            switch (lexer.code_point) {
                                '0'..='9', 'a'..='f', 'A'..='F' => {
                                    lexer.step();
                                },
                                else => lexer.syntaxError()?,
                            }
                        }

                        lexer.step();
                    } else {
                        // Fixed-length
                        // comptime var j: usize = 0;

                        switch (lexer.code_point) {
                            '0'..='9', 'a'..='f', 'A'..='F' => {
                                lexer.step();
                            },
                            else => lexer.syntaxError()?,
                        }
                        switch (lexer.code_point) {
                            '0'..='9', 'a'..='f', 'A'..='F' => {
                                lexer.step();
                            },
                            else => lexer.syntaxError()?,
                        }
                        switch (lexer.code_point) {
                            '0'..='9', 'a'..='f', 'A'..='F' => {
                                lexer.step();
                            },
                            else => lexer.syntaxError()?,
                        }
                        switch (lexer.code_point) {
                            '0'..='9', 'a'..='f', 'A'..='F' => {
                                lexer.step();
                            },
                            else => lexer.syntaxError()?,
                        }
                    }
                    continue;
                }

                if (!isIdentifierContinue(lexer.code_point)) {
                    break;
                }
                lexer.step();
            }

            // Second pass: re-use our existing escape sequence parser
            const original_text = lexer.raw();

            bun.assert(lexer.temp_buffer_u16.items.len == 0);
            const buf_ptr = unsafe { &lexer.temp_buffer_u16 };
            const _clear_guard = TempBufferU16ClearGuard{ ptr: buf_ptr };
            lexer.temp_buffer_u16.ensureUnusedCapacity(original_text.len)?;
            lexer.decodeEscapeSequences(lexer.start, original_text, std.array_list.Managed(u16), &lexer.temp_buffer_u16)?;
            result.contents = lexer.utf16ToString(lexer.temp_buffer_u16.items)?;

            let identifier = result.contents;
            if (kind == .@"private") {
                identifier = result.contents[1..];
            }

            if (!isIdentifier(identifier)) {
                lexer.addRangeError(
                    { loc: logger.usize2Loc(lexer.start), len: @as(i32, i32(lexer.end - lexer.start)) },
                    "Invalid identifier: \"{s}\"",
                    .{result.contents},
                    true,
                )?;
            }

            result.contents = result.contents;

            // Escaped keywords are not allowed to work as actual keywords, but they are
            // allowed wherever we allow identifiers or keywords. For example:
            //
            //   // This is an error (equivalent to "var var;")
            //   var \u0076\u0061\u0072;
            //
            //   // This is an error (equivalent to "var foo;" except for this rule)
            //   \u0076\u0061\u0072 foo;
            //
            //   // This is an fine (equivalent to "foo.var;")
            //   foo.\u0076\u0061\u0072;
            //
            result.token = if (Keywords.has(result.contents)) .t_escaped_keyword else .t_identifier;

            return result;
        }

        pub fn expectContextualKeyword(*_, comptime keyword: string)! {
            if (!_.isContextualKeyword(keyword)) {
                if (builtin.mode == std.builtin.OptimizeMode.Debug) {
                    _.addError(_.start, "Expected \"{s}\" but found \"{s}\" (token: {s})", .{
                        keyword,
                        _.raw(),
                        @tagName(_.token),
                    }, true);
                } else {
                    _.addError(_.start, "Expected \"{s}\" but found \"{s}\"", .{ keyword, _.raw() }, true);
                }
                return LexerType.Error.UnexpectedSyntax;
            }
            _.next()?;
        }

        pub static fn maybeExpandEquals(lexer: *LexerType)! {
            switch (lexer.code_point) {
                '>' => {
                    // "=" + ">" = "=>"
                    lexer.token = .t_equals_greater_than;
                    lexer.step();
                },
                '=' => {
                    // "=" + "=" = "=="
                    lexer.token = .t_equals_equals;
                    lexer.step();

                    if (lexer.code_point == '=') {
                        // "=" + "==" = "==="
                        lexer.token = .t_equals_equals_equals;
                        lexer.step();
                    }
                },
                else => {},
            }
        }

        pub static fn expectLessThan(lexer: *LexerType, comptime is_inside_jsx_element: bool)! {
            switch (lexer.token) {
                .t_less_than => {
                    if (is_inside_jsx_element) {
                        lexer.nextInsideJSXElement()?;
                    } else {
                        lexer.next()?;
                    }
                },
                .t_less_than_equals => {
                    lexer.token = .t_equals;
                    lexer.start += 1;
                    lexer.maybeExpandEquals()?;
                },
                .t_less_than_less_than => {
                    lexer.token = .t_less_than;
                    lexer.start += 1;
                },
                .t_less_than_less_than_equals => {
                    lexer.token = .t_less_than_equals;
                    lexer.start += 1;
                },
                else => {
                    lexer.expected(.t_less_than)?;
                },
            }
        }

        pub static fn expectGreaterThan(lexer: *LexerType, comptime is_inside_jsx_element: bool)! {
            switch (lexer.token) {
                .t_greater_than => {
                    if (is_inside_jsx_element) {
                        lexer.nextInsideJSXElement()?;
                    } else {
                        lexer.next()?;
                    }
                },

                .t_greater_than_equals => {
                    lexer.token = .t_equals;
                    lexer.start += 1;
                    lexer.maybeExpandEquals()?;
                },

                .t_greater_than_greater_than_equals => {
                    lexer.token = .t_greater_than_equals;
                    lexer.start += 1;
                },

                .t_greater_than_greater_than_greater_than_equals => {
                    lexer.token = .t_greater_than_greater_than_equals;
                    lexer.start += 1;
                },

                .t_greater_than_greater_than => {
                    lexer.token = .t_greater_than;
                    lexer.start += 1;
                },

                .t_greater_than_greater_than_greater_than => {
                    lexer.token = .t_greater_than_greater_than;
                    lexer.start += 1;
                },

                else => {
                    lexer.expected(.t_greater_than)?;
                },
            }
        }

        pub static fn next(lexer: *LexerType)! {
            lexer.has_newline_before = lexer.end == 0;
            lexer.has_pure_comment_before = false;
            lexer.has_no_side_effect_comment_before = false;
            lexer.prev_token_was_await_keyword = false;

            while (true) {
                lexer.start = lexer.end;
                lexer.token = T.t_end_of_file;

                switch (lexer.code_point) {
                    -1 => {
                        lexer.token = T.t_end_of_file;
                    },

                    '#' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Private identifiers are not allowed in JSON");
                        }
                        if (lexer.start == 0 && lexer.source.contents.len > 1 && lexer.source.contents[1] == '!') {
                            // "#!/usr/bin/env node"
                            lexer.token = .t_hashbang;
                            hashbang: while (true) {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '\r', '\n', 0x2028, 0x2029 => {
                                        break :hashbang;
                                    },
                                    -1 => {
                                        break :hashbang;
                                    },
                                    else => {},
                                }
                            }
                            lexer.identifier = lexer.raw();
                        } else {
                            // "#foo"
                            lexer.step();
                            if (lexer.code_point == '\\') {
                                const scan_res = lexer.scanIdentifierWithEscapes(.@"private")?;
                                lexer.identifier = scan_res.contents;
                            } else {
                                if (!isIdentifierStart(lexer.code_point)) {
                                    lexer.syntaxError()?;
                                }

                                lexer.step();
                                while (isIdentifierContinue(lexer.code_point)) {
                                    lexer.step();
                                }
                                if (lexer.code_point == '\\') {
                                    const scan_res = lexer.scanIdentifierWithEscapes(.@"private")?;
                                    lexer.identifier = scan_res.contents;
                                } else {
                                    lexer.identifier = lexer.raw();
                                }
                            }
                            lexer.token = T.t_private_identifier;
                            break;
                        }
                    },
                    '\r', '\n', 0x2028, 0x2029 => {
                        lexer.has_newline_before = true;

                        if (comptime json_options.guess_indentation) {
                            if (lexer.indent_info.first_newline && lexer.code_point == '\n') {
                                while (lexer.code_point == '\n' || lexer.code_point == '\r') {
                                    lexer.step();
                                }

                                if (lexer.code_point != ' ' && lexer.code_point != '\t') {
                                    // try to get the next one. this handles cases where the file starts
                                    // with a newline
                                    continue;
                                }

                                lexer.indent_info.first_newline = false;

                                const indent_character = lexer.code_point;
                                let count: usize = 0;
                                while (lexer.code_point == indent_character) {
                                    lexer.step();
                                    count += 1;
                                }

                                lexer.indent_info.guess.character = if (indent_character == ' ') .space else .tab;
                                lexer.indent_info.guess.scalar = count;
                                continue;
                            }
                        }

                        lexer.step();
                        continue;
                    },
                    '\t', ' ' => {
                        lexer.step();
                        continue;
                    },
                    '(' => {
                        lexer.step();
                        lexer.token = T.t_open_paren;
                    },
                    ')' => {
                        lexer.step();
                        lexer.token = T.t_close_paren;
                    },
                    '[' => {
                        lexer.step();
                        lexer.token = T.t_open_bracket;
                    },
                    ']' => {
                        lexer.step();
                        lexer.token = T.t_close_bracket;
                    },
                    '{' => {
                        lexer.step();
                        lexer.token = T.t_open_brace;
                    },
                    '}' => {
                        lexer.step();
                        lexer.token = T.t_close_brace;
                    },
                    ',' => {
                        lexer.step();
                        lexer.token = T.t_comma;
                    },
                    ':' => {
                        lexer.step();
                        lexer.token = T.t_colon;
                    },
                    ';' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Semicolons are not allowed in JSON");
                        }

                        lexer.step();
                        lexer.token = T.t_semicolon;
                    },
                    '@' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Decorators are not allowed in JSON");
                        }

                        lexer.step();
                        lexer.token = T.t_at;
                    },
                    '~' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("~ is not allowed in JSON");
                        }

                        lexer.step();
                        lexer.token = T.t_tilde;
                    },
                    '?' => {
                        // '?' or '?.' or '??' or '??='
                        lexer.step();
                        switch (lexer.code_point) {
                            '?' => {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '=' => {
                                        lexer.step();
                                        lexer.token = T.t_question_question_equals;
                                    },
                                    else => {
                                        lexer.token = T.t_question_question;
                                    },
                                }
                            },

                            '.' => {
                                lexer.token = T.t_question;
                                const current = lexer.current;
                                const contents = lexer.source.contents;

                                // Lookahead to disambiguate with 'a?.1:b'
                                if (current < contents.len) {
                                    const c = contents[current];
                                    if (c < '0' || c > '9') {
                                        lexer.step();
                                        lexer.token = T.t_question_dot;
                                    }
                                }
                            },
                            else => {
                                lexer.token = T.t_question;
                            },
                        }
                    },
                    '%' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '%' or '%='
                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = T.t_percent_equals;
                            },

                            else => {
                                lexer.token = T.t_percent;
                            },
                        }
                    },

                    '&' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '&' or '&=' or '&&' or '&&='
                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = T.t_ampersand_equals;
                            },

                            '&' => {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '=' => {
                                        lexer.step();
                                        lexer.token = T.t_ampersand_ampersand_equals;
                                    },

                                    else => {
                                        lexer.token = T.t_ampersand_ampersand;
                                    },
                                }
                            },
                            else => {
                                lexer.token = T.t_ampersand;
                            },
                        }
                    },

                    '|' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '|' or '|=' or '||' or '||='
                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = T.t_bar_equals;
                            },
                            '|' => {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '=' => {
                                        lexer.step();
                                        lexer.token = T.t_bar_bar_equals;
                                    },

                                    else => {
                                        lexer.token = T.t_bar_bar;
                                    },
                                }
                            },
                            else => {
                                lexer.token = T.t_bar;
                            },
                        }
                    },

                    '^' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '^' or '^='
                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = T.t_caret_equals;
                            },

                            else => {
                                lexer.token = T.t_caret;
                            },
                        }
                    },

                    '+' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '+' or '+=' or '++'
                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = T.t_plus_equals;
                            },

                            '+' => {
                                lexer.step();
                                lexer.token = T.t_plus_plus;
                            },

                            else => {
                                lexer.token = T.t_plus;
                            },
                        }
                    },

                    '-' => {

                        // '+' or '+=' or '++'
                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                if (comptime is_json) {
                                    return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                                }
                                lexer.step();
                                lexer.token = T.t_minus_equals;
                            },

                            '-' => {
                                if (comptime is_json) {
                                    return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                                }
                                lexer.step();

                                if (lexer.code_point == '>' && lexer.has_newline_before) {
                                    lexer.step();
                                    lexer.log.addRangeWarning(&lexer.source, lexer.range(), "Treating \"-->\" as the start of a legacy HTML single-line comment") catch unreachable;

                                    singleLineHTMLCloseComment: while (true) {
                                        switch (lexer.code_point) {
                                            '\r', '\n', 0x2028, 0x2029 => {
                                                break :singleLineHTMLCloseComment;
                                            },
                                            -1 => {
                                                break :singleLineHTMLCloseComment;
                                            },
                                            else => {},
                                        }
                                        lexer.step();
                                    }
                                    continue;
                                }

                                lexer.token = T.t_minus_minus;
                            },

                            else => {
                                lexer.token = T.t_minus;
                            },
                        }
                    },

                    '*' => {
                        // '*' or '*=' or '**' or '**='

                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = .t_asterisk_equals;
                            },
                            '*' => {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '=' => {
                                        lexer.step();
                                        lexer.token = .t_asterisk_asterisk_equals;
                                    },
                                    else => {
                                        lexer.token = .t_asterisk_asterisk;
                                    },
                                }
                            },
                            else => {
                                lexer.token = .t_asterisk;
                            },
                        }
                    },
                    '/' => {

                        // '/' or '/=' or '//' or '/* ... */'
                        lexer.step();

                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = .t_slash_equals;
                            },
                            '/' => {
                                lexer.scanSingleLineComment();
                                if (comptime is_json) {
                                    if (!json.allow_comments) {
                                        lexer.addRangeError(lexer.range(), "JSON does not support comments", {}, true)?;
                                        return;
                                    }
                                }
                                lexer.scanCommentText(false);
                                continue;
                            },
                            '*' => {
                                lexer.step();

                                multiLineComment: while (true) {
                                    switch (lexer.code_point) {
                                        '*' => {
                                            lexer.step();
                                            if (lexer.code_point == '/') {
                                                lexer.step();
                                                break :multiLineComment;
                                            }
                                        },
                                        '\r', '\n', 0x2028, 0x2029 => {
                                            lexer.step();
                                            lexer.has_newline_before = true;
                                        },
                                        -1 => {
                                            lexer.start = lexer.end;
                                            lexer.addSyntaxError(
                                                lexer.start,
                                                "Expected \"*/\" to terminate multi-line comment",
                                                {},
                                            )?;
                                        },
                                        else => {
                                            if (comptime Environment.enableSIMD) {
                                                if (lexer.code_point < 128) {
                                                    const remainder = lexer.source.contents[lexer.current..];
                                                    if (remainder.len >= 512) {
                                                        if (skip_idx := skipToInterestingCharacterInMultilineComment(remainder)) {
                                                            lexer.current += skip_idx;
                                                        } else {
                                                            lexer.step();
                                                            continue;
                                                        }
                                                        lexer.end = lexer.current -| 1;
                                                        lexer.step();
                                                        continue;
                                                    }
                                                }
                                            }

                                            lexer.step();
                                        },
                                    }
                                }
                                if (comptime is_json) {
                                    if (!json.allow_comments) {
                                        lexer.addRangeError(lexer.range(), "JSON does not support comments", {}, true)?;
                                        return;
                                    }
                                }
                                lexer.scanCommentText(true);
                                continue;
                            },
                            else => {
                                lexer.token = .t_slash;
                            },
                        }
                    },

                    '=' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '=' or '=>' or '==' or '==='
                        lexer.step();
                        switch (lexer.code_point) {
                            '>' => {
                                lexer.step();
                                lexer.token = T.t_equals_greater_than;
                            },

                            '=' => {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '=' => {
                                        lexer.step();
                                        lexer.token = T.t_equals_equals_equals;
                                    },

                                    else => {
                                        lexer.token = T.t_equals_equals;
                                    },
                                }
                            },

                            else => {
                                lexer.token = T.t_equals;
                            },
                        }
                    },

                    '<' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '<' or '<<' or '<=' or '<<=' or '<!--'
                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = T.t_less_than_equals;
                            },

                            '<' => {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '=' => {
                                        lexer.step();
                                        lexer.token = T.t_less_than_less_than_equals;
                                    },

                                    else => {
                                        lexer.token = T.t_less_than_less_than;
                                    },
                                }
                            },
                            // Handle legacy HTML-style comments
                            '!' => {
                                if (strings.eqlComptime(lexer.peek("--".len), "--")) {
                                    lexer.addUnsupportedSyntaxError("Legacy HTML comments not implemented yet!")?;
                                    return;
                                }

                                lexer.token = T.t_less_than;
                            },

                            else => {
                                lexer.token = T.t_less_than;
                            },
                        }
                    },

                    '>' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '>' or '>>' or '>>>' or '>=' or '>>=' or '>>>='
                        lexer.step();

                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                lexer.token = T.t_greater_than_equals;
                            },
                            '>' => {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '=' => {
                                        lexer.step();
                                        lexer.token = T.t_greater_than_greater_than_equals;
                                    },
                                    '>' => {
                                        lexer.step();
                                        switch (lexer.code_point) {
                                            '=' => {
                                                lexer.step();
                                                lexer.token = T.t_greater_than_greater_than_greater_than_equals;
                                            },
                                            else => {
                                                lexer.token = T.t_greater_than_greater_than_greater_than;
                                            },
                                        }
                                    },
                                    else => {
                                        lexer.token = T.t_greater_than_greater_than;
                                    },
                                }
                            },
                            else => {
                                lexer.token = T.t_greater_than;
                            },
                        }
                    },

                    '!' => {
                        if (comptime is_json) {
                            return lexer.addUnsupportedSyntaxError("Operators are not allowed in JSON");
                        }

                        // '!' or '!=' or '!=='
                        lexer.step();
                        switch (lexer.code_point) {
                            '=' => {
                                lexer.step();
                                switch (lexer.code_point) {
                                    '=' => {
                                        lexer.step();
                                        lexer.token = T.t_exclamation_equals_equals;
                                    },

                                    else => {
                                        lexer.token = T.t_exclamation_equals;
                                    },
                                }
                            },
                            else => {
                                lexer.token = T.t_exclamation;
                            },
                        }
                    },

                    '\'' => {
                        lexer.parseStringLiteral('\'')?;
                    },
                    '"' => {
                        lexer.parseStringLiteral('"')?;
                    },
                    '`' => {
                        lexer.parseStringLiteral('`')?;
                    },

                    '_', '$', 'a'..='z', 'A'..='Z' => {
                        const advance = latin1IdentifierContinueLength(lexer.source.contents[lexer.current..]);

                        lexer.end = lexer.current + advance;
                        lexer.current = lexer.end;

                        lexer.step();

                        if (lexer.code_point >= 0x80) {
                            @branchHint(.unlikely);
                            while (isIdentifierContinue(lexer.code_point)) {
                                lexer.step();
                            }
                        }

                        if (lexer.code_point != '\\') {
                            @branchHint(.likely);
                            // this code is so hot that if you save lexer.raw() into a temporary variable
                            // it shows up in profiling
                            lexer.identifier = lexer.raw();
                            if (kw := Keywords.get(lexer.identifier)) {
                                lexer.token = kw;
                            } else {
                                lexer.token = T.t_identifier;
                            }
                        } else {
                            @branchHint(.unlikely);
                            const scan_result = lexer.scanIdentifierWithEscapes(.normal)?;
                            lexer.identifier = scan_result.contents;
                            lexer.token = scan_result.token;
                        }
                    },

                    '\\' => {
                        if (comptime is_json && json_options.ignore_leading_escape_sequences) {
                            if (lexer.start == 0 || lexer.current == lexer.source.contents.len - 1) {
                                lexer.step();
                                continue;
                            }
                        }

                        const scan_result = lexer.scanIdentifierWithEscapes(.normal)?;
                        lexer.identifier = scan_result.contents;
                        lexer.token = scan_result.token;
                    },

                    '.', '0'..='9' => {
                        lexer.parseNumericLiteralOrDot()?;
                    },

                    else => {
                        // Check for unusual whitespace characters
                        if (isWhitespace(lexer.code_point)) {
                            lexer.step();
                            continue;
                        }

                        if (isIdentifierStart(lexer.code_point)) {
                            lexer.step();
                            while (isIdentifierContinue(lexer.code_point)) {
                                lexer.step();
                            }
                            if (lexer.code_point == '\\') {
                                const scan_result = lexer.scanIdentifierWithEscapes(.normal)?;
                                lexer.identifier = scan_result.contents;
                                lexer.token = scan_result.token;
                            } else {
                                lexer.token = T.t_identifier;
                                lexer.identifier = lexer.raw();
                            }
                            break;
                        }

                        lexer.end = lexer.current;
                        lexer.token = T.t_syntax_error;
                    },
                }

                return;
            }
        }

        pub fn expected(*_, token: T)! {
            if (_.is_log_disabled) {
                return error.Backtrack;
            } else if (tokenToString.get(token).len > 0) {
                _.expectedString(tokenToString.get(token))?;
            } else {
                _.unexpected()?;
            }
        }

        pub static fn unexpected(lexer: *LexerType)! {
            const found = finder: {
                lexer.start = @min(lexer.start, lexer.end);

                if (lexer.start == lexer.source.contents.len) {
                    break :finder "end of file";
                } else {
                    break :finder lexer.raw();
                }
            };

            lexer.did_panic = true;
            lexer.addRangeError(lexer.range(), "Unexpected {s}", .{found}, true)?;
        }

        pub fn raw(): []const u8 {
            return _.source.contents[_.start.._.end];
        }

        pub fn isContextualKeyword(comptime keyword: string): bool {
            return _.token == .t_identifier && strings.eqlComptime(_.raw(), keyword);
        }

        pub fn expectedString(*_, text: string)! {
            if (_.prev_token_was_await_keyword) {
                let notes: [1]logger.Data = undefined;
                if (!_.fn_or_arrow_start_loc.isEmpty()) {
                    notes[0] = logger.rangeData(
                        &_.source,
                        rangeOfIdentifier(
                            &_.source,
                            _.fn_or_arrow_start_loc,
                        ),
                        "Consider adding the \"async\" keyword here",
                    );
                }

                const notes_len = @as(usize, @intFromBool(!_.fn_or_arrow_start_loc.isEmpty()));
                const notes_ptr = notes[0..notes_len];

                _.addRangeErrorWithNotes(
                    _.range(),
                    "\"await\" can only be used inside an \"async\" function",
                    {},
                    notes_ptr,
                )?;
                return;
            }
            if (_.source.contents.len != _.start) {
                _.addRangeError(
                    _.range(),
                    "Expected {s} but found \"{s}\"",
                    .{ text, _.raw() },
                    true,
                )?;
            } else {
                _.addRangeError(
                    _.range(),
                    "Expected {s} but found end of file",
                    .{text},
                    true,
                )?;
            }
        }

        static fn scanCommentText(lexer: *LexerType, for_pragma: bool) {
            const text = lexer.source.contents[lexer.start..lexer.end];
            const has_legal_annotation = text.len > 2 && text[2] == '!';
            const is_multiline_comment = text.len > 1 && text[1] == '*';

            if (lexer.track_comments)
                // Save the original comment text so we can subtract comments from the
                // character frequency analysis used by symbol minification
                lexer.all_comments.append(lexer.range()) catch unreachable;

            // Omit the trailing "*/" from the checks below
            const end_comment_text =
                if (is_multiline_comment)
                    text.len - 2
                else
                    text.len;

            if (has_legal_annotation || lexer.preserve_all_comments_before) {
                if (is_multiline_comment) {
                    // text = lexer.removeMultilineCommentIndent(lexer.source.contents[0..lexer.start], text);
                }

                lexer.comments_to_preserve_before.append(js_ast.G.Comment{
                    text: text,
                    loc: lexer.loc(),
                }) catch unreachable;
            }

            // tsconfig.json doesn't care about annotations
            if (comptime is_json)
                return;

            if (!for_pragma) {
                return;
            }

            let rest = text[0..end_comment_text];

            while (i := strings.indexOfAny(rest, "@#")) {
                const c = rest[i];
                rest = rest[@min(i + 1, rest.len)..];
                switch (c) {
                    '@', '#' => {
                        const chunk = rest;
                        const offset = lexer.scanPragma(lexer.start + i + (text.len - rest.len), chunk, false);

                        rest = rest[
                            // The @min is necessary because the file could end
                            // with a pragma and hasPrefixWithWordBoundary
                            // returns true when that "word boundary" is EOF
                            @min(offset, rest.len)..];
                    },
                    else => {},
                }
            }
        }

        /// This scans a "// comment" in a single pass over the input.
        static fn scanSingleLineComment(lexer: *LexerType) {
            while (true) {
                // Find index of newline (ASCII/Unicode), non-ASCII, '#', or '@'.
                if (relative_index := bun.highway.indexOfNewlineOrNonASCIIOrHashOrAt(lexer.remaining())) {
                    const absolute_index = lexer.current + relative_index;
                    lexer.current = absolute_index; // Move TO the interesting char

                    lexer.step(); // Consume the interesting char, sets code_point, advances current

                    switch (lexer.code_point) {
                        '\r', '\n', 0x2028, 0x2029 => { // Is it a line terminator?
                            // Found the end of the comment line.
                            return; // Stop scanning. Lexer state is ready for the next token.
                        },
                        -1 => {
                            return;
                        }, // EOF? Stop.

                        '#', '@' => {
                            if (comptime !is_json) {
                                const pragma_trigger_pos = lexer.end; // Position OF #/@
                                // Use remaining() which starts *after* the consumed #/@
                                const chunk = lexer.remaining();

                                const offset = lexer.scanPragma(pragma_trigger_pos, chunk, true);

                                if (offset > 0) {
                                    // Pragma found (e.g., __PURE__).
                                    // Advance current past the pragma's argument text.
                                    // 'current' is already after the #/@ trigger.
                                    lexer.current += offset;
                                    // Do NOT consume the character immediately after the pragma.
                                    // Let the main loop find the actual line terminator.

                                    // Continue the outer loop from the position AFTER the pragma arg.
                                    continue;
                                }
                                // If offset == 0, it wasn't a valid pragma start.
                            }
                            // Not a pragma or is_json. Treat #/@ as a normal comment character.
                            // The character was consumed by step(). Let the outer loop continue.
                            continue;
                        },
                        else => {
                            // Non-ASCII (but not LS/PS), etc. Treat as normal comment char.
                            // The character was consumed by step(). Let the outer loop continue.
                            continue;
                        },
                    }
                } else { // Highway found nothing until EOF
                    // Consume the rest of the line.
                    lexer.end = lexer.source.contents.len;
                    lexer.current = lexer.source.contents.len;
                    lexer.code_point = -1; // Set EOF state
                    return;
                }
            }
            unreachable;
        }
        /// Scans the string for a pragma.
        /// offset is used when there's an issue with the JSX pragma later on.
        /// Returns the byte length to advance by if found, otherwise 0.
        static fn scanPragma(lexer: *LexerType, offset_for_errors: usize, chunk: string, allow_newline: bool): usize {
            if (!lexer.has_pure_comment_before) {
                if (strings.hasPrefixWithWordBoundary(chunk, "__PURE__")) {
                    lexer.has_pure_comment_before = true;
                    return "__PURE__".len;
                }
            }

            if (strings.hasPrefixWithWordBoundary(chunk, "jsx")) {
                if (span := PragmaArg.scan(.skip_space_first, lexer.start + offset_for_errors, "jsx", chunk, allow_newline)) {
                    lexer.jsx_pragma._jsx = span;
                    return "jsx".len +
                        if (span.range.len > 0) @as(usize, i32(span.range.len)) else 0;
                }
            } else if (strings.hasPrefixWithWordBoundary(chunk, "jsxFrag")) {
                if (span := PragmaArg.scan(.skip_space_first, lexer.start + offset_for_errors, "jsxFrag", chunk, allow_newline)) {
                    lexer.jsx_pragma._jsxFrag = span;
                    return "jsxFrag".len +
                        if (span.range.len > 0) @as(usize, i32(span.range.len)) else 0;
                }
            } else if (strings.hasPrefixWithWordBoundary(chunk, "jsxRuntime")) {
                if (span := PragmaArg.scan(.skip_space_first, lexer.start + offset_for_errors, "jsxRuntime", chunk, allow_newline)) {
                    lexer.jsx_pragma._jsxRuntime = span;
                    return "jsxRuntime".len +
                        if (span.range.len > 0) @as(usize, i32(span.range.len)) else 0;
                }
            } else if (strings.hasPrefixWithWordBoundary(chunk, "jsxImportSource")) {
                if (span := PragmaArg.scan(.skip_space_first, lexer.start + offset_for_errors, "jsxImportSource", chunk, allow_newline)) {
                    lexer.jsx_pragma._jsxImportSource = span;
                    return "jsxImportSource".len +
                        if (span.range.len > 0) @as(usize, i32(span.range.len)) else 0;
                }
            } else if (chunk.len >= " sourceMappingURL=".len + 1 && strings.hasPrefixComptime(chunk, " sourceMappingURL=")) { // Check includes space for prefix
                return PragmaArg.scanSourceMappingURLValue(lexer.start, offset_for_errors, chunk, &lexer.source_mapping_url);
            }

            return 0;
        }
        // TODO: implement this
        pub static fn removeMultilineCommentIndent(_: *LexerType, _: string, text: string): string {
            return text;
        }

        pub fn range(*_): logger.Range {
            return logger.Range{
                loc: logger.usize2Loc(_.start),
                len: std.math.lossyCast(i32, _.end - _.start),
            };
        }

        pub static fn initJSON(log: *logger.Log, source: *const logger.Source, allocator: std.mem.Allocator): LexerType! {
            let lex = LexerType{
                log: log,
                source: source.*,
                temp_buffer_u16: std.array_list.Managed(u16).init(allocator),
                prev_error_loc: logger.Loc.Empty,
                allocator: allocator,
                comments_to_preserve_before: std.array_list.Managed(js_ast.G.Comment).init(allocator),
                all_comments: std.array_list.Managed(logger.Range).init(allocator),
            };
            errdefer lex.deinit();
            lex.step();
            lex.next()?;

            return lex;
        }

        pub static fn initWithoutReading(log: *logger.Log, source: *const logger.Source, allocator: std.mem.Allocator): LexerType {
            return LexerType{
                log: log,
                source: source.*,
                temp_buffer_u16: std.array_list.Managed(u16).init(allocator),
                prev_error_loc: logger.Loc.Empty,
                allocator: allocator,
                comments_to_preserve_before: std.array_list.Managed(js_ast.G.Comment).init(allocator),
                all_comments: std.array_list.Managed(logger.Range).init(allocator),
            };
        }

        pub fn init(log: *logger.Log, source: *const logger.Source, allocator: std.mem.Allocator): LexerType! {
            let lex = initWithoutReading(log, source, allocator);
            errdefer lex.deinit();
            lex.step();
            lex.next()?;

            return lex;
        }

        pub static fn toEString(lexer: *LexerType): js_ast.E.String! {
            switch (lexer.string_literal_raw_format) {
                .ascii => {
                    // string_literal_raw_content contains ascii without escapes
                    return js_ast.E.String.init(lexer.string_literal_raw_content);
                },
                .utf16 => {
                    // string_literal_raw_content is already parsed, duplicated, and utf-16
                    return js_ast.E.String.init(@as([]const u16, std.mem.bytesAsSlice(u16, lexer.string_literal_raw_content)));
                },
                .needs_decode => {
                    // string_literal_raw_content contains escapes (ie '\n') that need to be converted to their values (ie 0x0A).
                    // escape parsing may cause a syntax error.
                    bun.assert(lexer.temp_buffer_u16.items.len == 0);
                    const buf_ptr = unsafe { &lexer.temp_buffer_u16 };
                    const _clear_guard = TempBufferU16ClearGuard{ ptr: buf_ptr };
                    lexer.temp_buffer_u16.ensureUnusedCapacity(lexer.string_literal_raw_content.len)?;
                    lexer.decodeEscapeSequences(lexer.string_literal_start, lexer.string_literal_raw_content, std.array_list.Managed(u16), &lexer.temp_buffer_u16)?;
                    const first_non_ascii = strings.firstNonASCII16(lexer.temp_buffer_u16.items);
                    // prefer to store an ascii e.string rather than a utf-16 one. ascii takes less memory, and `+` folding is not yet supported on utf-16.
                    if (first_non_ascii != null) {
                        const duped = lexer.allocator.dupe(u16, lexer.temp_buffer_u16.items)?;
                        return js_ast.E.String.init(duped);
                    } else {
                        const result = lexer.allocator.alloc(u8, lexer.temp_buffer_u16.items.len)?;
                        strings.copyU16IntoU8(result, lexer.temp_buffer_u16.items);
                        return js_ast.E.String.init(result);
                    }
                },
            }
        }

        pub static fn toUTF8EString(lexer: *LexerType): js_ast.E.String! {
            let res = lexer.toEString()?;
            res.toUTF8(lexer.allocator)?;
            return res;
        }

        static inline fn assertNotJSON() {
            if (comptime is_json) @compileError("JSON should not reach this point");
            if (comptime is_json) unreachable;
        }

        pub static fn scanRegExp(lexer: *LexerType)! {
            assertNotJSON();
            lexer.regex_flags_start = null;
            while (true) {
                switch (lexer.code_point) {
                    '/' => {
                        lexer.step();

                        let has_set_flags_start = false;
                        const flag_characters = "dgimsuvy";
                        const min_flag = comptime std.mem.min(u8, flag_characters);
                        const max_flag = comptime std.mem.max(u8, flag_characters);
                        const RegexpFlags = bun.bit_set.IntegerBitSet((max_flag - min_flag) + 1);
                        let flags = RegexpFlags.initEmpty();
                        while (isIdentifierContinue(lexer.code_point)) {
                            switch (lexer.code_point) {
                                'd', 'g', 'i', 'm', 's', 'u', 'y', 'v' => {
                                    if (!has_set_flags_start) {
                                        lexer.regex_flags_start = @as(u16, @intCast(lexer.end - lexer.start));
                                        has_set_flags_start = true;
                                    }
                                    const flag = max_flag - @as(u8, i32(lexer.code_point));
                                    if (flags.isSet(flag)) {
                                        lexer.addError(
                                            lexer.current,
                                            "Duplicate flag \"{u}\" in regular expression",
                                            .{@as(u21, @intCast(lexer.code_point))},
                                            false,
                                        );
                                    }
                                    flags.set(flag);

                                    lexer.step();
                                },
                                else => {
                                    lexer.addError(
                                        lexer.current,
                                        "Invalid flag \"{u}\" in regular expression",
                                        .{@as(u21, @intCast(lexer.code_point))},
                                        false,
                                    );

                                    lexer.step();
                                },
                            }
                        }
                        return;
                    },
                    '[' => {
                        lexer.step();
                        while (lexer.code_point != ']') {
                            lexer.scanRegExpValidateAndStep()?;
                        }
                        lexer.step();
                    },
                    else => {
                        lexer.scanRegExpValidateAndStep()?;
                    },
                }
            }
        }

        pub static fn utf16ToString(lexer: *const LexerType, js: JavascriptString): string! {
            return strings.toUTF8AllocWithType(lexer.allocator, js)?;
        }
        pub static fn nextInsideJSXElement(lexer: *LexerType)! {
            assertNotJSON();

            lexer.has_newline_before = false;

            while (true) {
                lexer.start = lexer.end;
                lexer.token = .t_end_of_file;

                switch (lexer.code_point) {
                    -1 => {
                        lexer.token = .t_end_of_file;
                    },
                    '\r', '\n', 0x2028, 0x2029 => {
                        lexer.step();
                        lexer.has_newline_before = true;
                        continue;
                    },
                    '\t', ' ' => {
                        lexer.step();
                        continue;
                    },
                    '.' => {
                        lexer.step();
                        lexer.token = .t_dot;
                    },
                    '=' => {
                        lexer.step();
                        lexer.token = .t_equals;
                    },
                    '{' => {
                        lexer.step();
                        lexer.token = .t_open_brace;
                    },
                    '}' => {
                        lexer.step();
                        lexer.token = .t_close_brace;
                    },
                    '<' => {
                        lexer.step();
                        lexer.token = .t_less_than;
                    },
                    '>' => {
                        lexer.step();
                        lexer.token = .t_greater_than;
                    },
                    '/' => {
                        // '/' or '//' or '/* ... */'

                        lexer.step();
                        switch (lexer.code_point) {
                            '/' => {
                                single_line_comment: {
                                    while (true) {
                                        lexer.step();
                                        switch (lexer.code_point) {
                                            '\r', '\n', 0x2028, 0x2029 => {
                                                break :single_line_comment;
                                            },
                                            -1 => {
                                                break :single_line_comment;
                                            },
                                            else => {},
                                        }
                                    }
                                }
                                continue;
                            },
                            '*' => {
                                lexer.step();
                                multi_line_comment: {
                                    while (true) {
                                        switch (lexer.code_point) {
                                            '*' => {
                                                lexer.step();
                                                if (lexer.code_point == '/') {
                                                    lexer.step();
                                                    break :multi_line_comment;
                                                }
                                            },
                                            '\r', '\n', 0x2028, 0x2029 => {
                                                lexer.step();
                                                lexer.has_newline_before = true;
                                            },
                                            -1 => {
                                                lexer.start = lexer.end;
                                                lexer.addSyntaxError(lexer.start, "Expected \"*/\" to terminate multi-line comment", {})?;
                                            },
                                            else => {
                                                lexer.step();
                                            },
                                        }
                                    }
                                }
                                continue;
                            },
                            else => {
                                lexer.token = .t_slash;
                            },
                        }
                    },
                    '\'' => {
                        lexer.step();
                        lexer.parseJSXStringLiteral('\'')?;
                    },
                    '"' => {
                        lexer.step();
                        lexer.parseJSXStringLiteral('"')?;
                    },
                    else => {
                        if (isWhitespace(lexer.code_point)) {
                            lexer.step();
                            continue;
                        }

                        if (isIdentifierStart(lexer.code_point)) {
                            lexer.step();
                            while (isIdentifierContinue(lexer.code_point) || lexer.code_point == '-') {
                                lexer.step();
                            }

                            // Parse JSX namespaces. These are not supported by React or TypeScript
                            // but someone using JSX syntax in more obscure ways may find a use for
                            // them. A namespaced name is just always turned into a string so you
                            // can't use this feature to reference JavaScript identifiers.
                            if (lexer.code_point == ':') {
                                lexer.step();

                                if (isIdentifierStart(lexer.code_point)) {
                                    while (isIdentifierContinue(lexer.code_point) || lexer.code_point == '-') {
                                        lexer.step();
                                    }
                                } else {
                                    lexer.addSyntaxError(lexer.range().endI(), "Expected identifier after \"{s}\" in namespaced JSX name", .{lexer.raw()})?;
                                }
                            }

                            lexer.identifier = lexer.raw();
                            lexer.token = .t_identifier;
                            break;
                        }

                        lexer.end = lexer.current;
                        lexer.token = .t_syntax_error;
                    },
                }

                return;
            }
        }
        pub static fn parseJSXStringLiteral(lexer: *LexerType, comptime quote: u8)! {
            * = quote;
            assertNotJSON();

            let backslash = logger.Range.None;
            let needs_decode = false;

            string_literal: while (true) {
                switch (lexer.code_point) {
                    -1 => {
                        lexer.syntaxError()?;
                    },
                    '&' => {
                        needs_decode = true;
                        lexer.step();
                    },

                    '\\' => {
                        backslash = logger.Range{ loc: logger.Loc{
                            start: @as(i32, i32(lexer.end)),
                        }, len: 1 };
                        lexer.step();

                        // JSX string literals do not support escaping
                        // They're "pre" escaped
                        switch (lexer.code_point) {
                            'u', 0x0C, 0, '\t', 0x0B, 0x08 => {
                                needs_decode = true;
                            },
                            else => {},
                        }

                        continue;
                    },
                    quote => {
                        if (backslash.len > 0) {
                            backslash.len += 1;
                            lexer.previous_backslash_quote_in_jsx = backslash;
                        }
                        lexer.step();
                        break :string_literal;
                    },

                    else => {
                        // Non-ASCII strings need the slow path
                        if (lexer.code_point >= 0x80) {
                            needs_decode = true;
                        } else if ((comptime is_json) && lexer.code_point < 0x20) {
                            lexer.syntaxError()?;
                        }
                        lexer.step();
                    },
                }
                backslash = logger.Range.None;
            }

            lexer.token = .t_string_literal;

            const raw_content_slice = lexer.source.contents[lexer.start + 1 .. lexer.end - 1];
            if (needs_decode) {
                bun.assert(lexer.temp_buffer_u16.items.len == 0);
                const buf_ptr = unsafe { &lexer.temp_buffer_u16 };
                const _clear_guard = TempBufferU16ClearGuard{ ptr: buf_ptr };
                lexer.temp_buffer_u16.ensureUnusedCapacity(raw_content_slice.len)?;
                lexer.fixWhitespaceAndDecodeJSXEntities(raw_content_slice, &lexer.temp_buffer_u16)?;

                lexer.string_literal_raw_content = std.mem.sliceAsBytes(lexer.allocator.dupe(u16, lexer.temp_buffer_u16.items)?);
                lexer.string_literal_raw_format = .utf16;
            } else {
                lexer.string_literal_raw_content = raw_content_slice;
                lexer.string_literal_raw_format = .ascii;
            }
        }

        pub static fn expectJSXElementChild(lexer: *LexerType, token: T)! {
            assertNotJSON();

            if (lexer.token != token) {
                lexer.expected(token)?;
            }

            lexer.nextJSXElementChild()?;
        }

        pub static fn nextJSXElementChild(lexer: *LexerType)! {
            assertNotJSON();

            lexer.has_newline_before = false;
            const original_start = lexer.end;

            while (true) {
                lexer.start = lexer.end;
                lexer.token = T.t_end_of_file;

                switch (lexer.code_point) {
                    -1 => {
                        lexer.token = .t_end_of_file;
                    },
                    '{' => {
                        lexer.step();
                        lexer.token = .t_open_brace;
                    },
                    '<' => {
                        lexer.step();
                        lexer.token = .t_less_than;
                    },
                    else => {
                        let needs_fixing = false;

                        string_literal: while (true) {
                            switch (lexer.code_point) {
                                -1 => {
                                    lexer.syntaxError()?;
                                },
                                '&', '\r', '\n', 0x2028, 0x2029 => {
                                    needs_fixing = true;
                                    lexer.step();
                                },
                                '{', '<' => {
                                    break :string_literal;
                                },
                                else => {
                                    // Non-ASCII strings need the slow path
                                    needs_fixing = needs_fixing || lexer.code_point >= 0x80;
                                    lexer.step();
                                },
                            }
                        }

                        lexer.token = .t_string_literal;
                        const raw_content_slice = lexer.source.contents[original_start..lexer.end];

                        if (needs_fixing) {
                            bun.assert(lexer.temp_buffer_u16.items.len == 0);
                            const buf_ptr = unsafe { &lexer.temp_buffer_u16 };
                            const _clear_guard = TempBufferU16ClearGuard{ ptr: buf_ptr };
                            lexer.temp_buffer_u16.ensureUnusedCapacity(raw_content_slice.len)?;
                            lexer.fixWhitespaceAndDecodeJSXEntities(raw_content_slice, &lexer.temp_buffer_u16)?;
                            lexer.string_literal_raw_content = std.mem.sliceAsBytes(lexer.allocator.dupe(u16, lexer.temp_buffer_u16.items)?);
                            lexer.string_literal_raw_format = .utf16;

                            if (lexer.temp_buffer_u16.items.len == 0) {
                                lexer.has_newline_before = true;
                                continue;
                            }
                        } else {
                            lexer.string_literal_raw_content = raw_content_slice;
                            lexer.string_literal_raw_format = .ascii;
                        }
                    },
                }

                break;
            }
        }

        pub static fn fixWhitespaceAndDecodeJSXEntities(lexer: *LexerType, text: string, decoded: *std.array_list.Managed(u16))! {
            assertNotJSON();

            let after_last_non_whitespace: u32? = null;

            // Trim whitespace off the end of the first line
            let first_non_whitespace: u32? = 0;

            const iterator = strings.CodepointIterator.init(text);
            let cursor = strings.CodepointIterator.Cursor{};

            while (iterator.next(&cursor)) {
                switch (cursor.c) {
                    '\r', '\n', 0x2028, 0x2029 => {
                        if (first_non_whitespace != null && after_last_non_whitespace != null) {
                            // Newline
                            if (decoded.items.len > 0) {
                                decoded.append(' ')?;
                            }

                            // Trim whitespace off the start and end of lines in the middle
                            lexer.decodeJSXEntities(text[first_non_whitespace.?..after_last_non_whitespace.?], decoded)?;
                        }

                        // Reset for the next line
                        first_non_whitespace = null;
                    },
                    '\t', ' ' => {},
                    else => {
                        // Check for unusual whitespace characters
                        if (!isWhitespace(cursor.c)) {
                            after_last_non_whitespace = cursor.i + @as(u32, cursor.width);
                            if (first_non_whitespace == null) {
                                first_non_whitespace = cursor.i;
                            }
                        }
                    },
                }
            }

            if (start := first_non_whitespace) {
                if (decoded.items.len > 0) {
                    decoded.append(' ')?;
                }

                decodeJSXEntities(lexer, text[start..text.len], decoded)?;
            }
        }

        static fn maybeDecodeJSXEntity(lexer: *LexerType, text: string, cursor: *strings.CodepointIterator.Cursor) {
            assertNotJSON();

            if (length := strings.indexOfChar(text[cursor.width + cursor.i ..], ';')) {
                const end = cursor.width + cursor.i;
                const entity = text[end .. end + length];
                if (entity[0] == '#') {
                    let number = entity[1..entity.len];
                    let base: u8 = 10;
                    if (number.len > 1 && number[0] == 'x') {
                        number = number[1..number.len];
                        base = 16;
                    }

                    cursor.c = std.fmt.parseInt(i32, number, base) catch (e) {
                        if (e == error.InvalidCharacter) {
                            lexer.addError(lexer.start, "Invalid JSX entity escape: {s}", .{entity}, false);
                        } else if (e == error.Overflow) {
                            lexer.addError(lexer.start, "JSX entity escape is too big: {s}", .{entity}, false);
                        }
                        strings.unicode_replacement;
                    };

                    cursor.i += @as(u32, i32(length)) + 1;
                    cursor.width = 1;
                } else if (ent := tables.jsxEntity.get(entity)) {
                    cursor.c = ent;
                    cursor.i += @as(u32, i32(length)) + 1;
                }
            }
        }

        pub static fn decodeJSXEntities(lexer: *LexerType, text: string, out: *std.array_list.Managed(u16))! {
            assertNotJSON();

            const iterator = strings.CodepointIterator.init(text);
            let cursor = strings.CodepointIterator.Cursor{};

            while (iterator.next(&cursor)) {
                if (cursor.c == '&') lexer.maybeDecodeJSXEntity(text, &cursor);

                if (cursor.c <= 0xFFFF) {
                    out.append(@as(u16, i32(cursor.c)))?;
                } else {
                    cursor.c -= 0x10000;
                    out.ensureUnusedCapacity(2)?;
                    out.appendAssumeCapacity(@as(u16, @truncate(@as(u32, @bitCast(@as(i32, 0xD800) + ((cursor.c >> 10) & 0x3FF))))));
                    out.appendAssumeCapacity(@as(u16, @truncate(@as(u32, @bitCast(@as(i32, 0xDC00) + (cursor.c & 0x3FF))))));
                }
            }
        }
        pub static fn expectInsideJSXElement(lexer: *LexerType, token: T)! {
            assertNotJSON();

            if (lexer.token != token) {
                lexer.expected(token)?;
                return LexerType.Error.SyntaxError;
            }

            lexer.nextInsideJSXElement()?;
        }

        pub static fn expectInsideJSXElementWithName(lexer: *LexerType, token: T, name: string)! {
            assertNotJSON();

            if (lexer.token != token) {
                lexer.expectedString(name)?;
                return LexerType.Error.SyntaxError;
            }

            lexer.nextInsideJSXElement()?;
        }

        static fn scanRegExpValidateAndStep(lexer: *LexerType)! {
            assertNotJSON();

            if (lexer.code_point == '\\') {
                lexer.step();
            }

            switch (lexer.code_point) {
                '\r', '\n', 0x2028, 0x2029 => {
                    // Newlines aren't allowed in regular expressions
                    lexer.syntaxError()?;
                },
                -1 => { // EOF
                    lexer.syntaxError()?;
                },
                else => {
                    lexer.step();
                },
            }
        }

        pub static fn rescanCloseBraceAsTemplateToken(lexer: *LexerType)! {
            assertNotJSON();

            if (lexer.token != .t_close_brace) {
                lexer.expected(.t_close_brace)?;
            }

            lexer.rescan_close_brace_as_template_token = true;
            lexer.code_point = '`';
            lexer.current = lexer.end;
            lexer.end -= 1;
            lexer.next()?;
            lexer.rescan_close_brace_as_template_token = false;
        }

        pub static fn rawTemplateContents(lexer: *LexerType): string {
            assertNotJSON();

            let text: string = undefined;

            switch (lexer.token) {
                .t_no_substitution_template_literal, .t_template_tail => {
                    text = lexer.source.contents[lexer.start + 1 .. lexer.end - 1];
                },
                .t_template_middle, .t_template_head => {
                    text = lexer.source.contents[lexer.start + 1 .. lexer.end - 2];
                },
                else => {},
            }

            if (strings.indexOfChar(text, '\r') == null) {
                return text;
            }

            // From the specification:
            //
            // 11.8.6.1 Static Semantics: TV and TRV
            //
            // TV excludes the code units of LineContinuation while TRV includes
            // them. <CR><LF> and <CR> LineTerminatorSequences are normalized to
            // <LF> for both TV and TRV. An explicit EscapeSequence is needed to
            // include a <CR> or <CR><LF> sequence.
            let bytes = bun.handleOom(MutableString.initCopy(lexer.allocator, text));
            let end: usize = 0;
            let i: usize = 0;
            let c: u8 = '0';
            while (i < bytes.list.items.len) {
                c = bytes.list.items[i];
                i += 1;

                if (c == '\r') {
                    // Convert '\r\n' into '\n'
                    if (i < bytes.list.items.len && bytes.list.items[i] == '\n') {
                        i += 1;
                    }

                    // Convert '\r' into '\n'
                    c = '\n';
                }

                bytes.list.items[end] = c;
                end += 1;
            }

            return bytes.toOwnedSliceLength(end);
        }

        static fn parseNumericLiteralOrDot(lexer: *LexerType)! {
            // Number or dot;
            const first = lexer.code_point;
            lexer.step();

            // Dot without a digit after it;
            if (first == '.' && (lexer.code_point < '0' || lexer.code_point > '9')) {
                // "..."
                if ((lexer.code_point == '.' &&
                    lexer.current < lexer.source.contents.len) &&
                    lexer.source.contents[lexer.current] == '.')
                {
                    lexer.step();
                    lexer.step();
                    lexer.token = T.t_dot_dot_dot;
                    return;
                }

                // "."
                lexer.token = T.t_dot;
                return;
            }

            let underscoreCount: usize = 0;
            let lastUnderscoreEnd: usize = 0;
            let hasDotOrExponent = first == '.';
            let base: f32 = 0.0;
            lexer.is_legacy_octal_literal = false;

            // Assume this is a number, but potentially change to a bigint later;
            lexer.token = T.t_numeric_literal;

            // Check for binary, octal, or hexadecimal literal;
            if (first == '0') {
                switch (lexer.code_point) {
                    'b', 'B' => {
                        base = 2;
                    },

                    'o', 'O' => {
                        base = 8;
                    },

                    'x', 'X' => {
                        base = 16;
                    },

                    '0'..='7', '_' => {
                        base = 8;
                        lexer.is_legacy_octal_literal = true;
                    },
                    else => {},
                }
            }

            if (base != 0) {
                // Integer literal;
                let isFirst = true;
                let isInvalidLegacyOctalLiteral = false;
                lexer.number = 0;
                if (!lexer.is_legacy_octal_literal) {
                    lexer.step();
                }

                integerLiteral: while (true) {
                    switch (lexer.code_point) {
                        '_' => {
                            // Cannot have multiple underscores in a row;
                            if (lastUnderscoreEnd > 0 && lexer.end == lastUnderscoreEnd + 1) {
                                lexer.syntaxError()?;
                            }

                            // The first digit must exist;
                            if (isFirst || lexer.is_legacy_octal_literal) {
                                lexer.syntaxError()?;
                            }

                            lastUnderscoreEnd = lexer.end;
                            underscoreCount += 1;
                        },

                        '0', '1' => {
                            lexer.number = lexer.number * base + float64(lexer.code_point - '0');
                        },

                        '2', '3', '4', '5', '6', '7' => {
                            if (base == 2) {
                                lexer.syntaxError()?;
                            }
                            lexer.number = lexer.number * base + float64(lexer.code_point - '0');
                        },
                        '8', '9' => {
                            if (lexer.is_legacy_octal_literal) {
                                isInvalidLegacyOctalLiteral = true;
                            } else if (base < 10) {
                                lexer.syntaxError()?;
                            }
                            lexer.number = lexer.number * base + float64(lexer.code_point - '0');
                        },
                        'A', 'B', 'C', 'D', 'E', 'F' => {
                            if (base != 16) {
                                lexer.syntaxError()?;
                            }
                            lexer.number = lexer.number * base + float64(lexer.code_point + 10 - 'A');
                        },

                        'a', 'b', 'c', 'd', 'e', 'f' => {
                            if (base != 16) {
                                lexer.syntaxError()?;
                            }
                            lexer.number = lexer.number * base + float64(lexer.code_point + 10 - 'a');
                        },
                        else => {
                            // The first digit must exist;
                            if (isFirst) {
                                lexer.syntaxError()?;
                            }

                            break :integerLiteral;
                        },
                    }

                    lexer.step();
                    isFirst = false;
                }

                const isBigIntegerLiteral = lexer.code_point == 'n' && !hasDotOrExponent;

                // Slow path: do we need to re-scan the input as text?
                if (isBigIntegerLiteral || isInvalidLegacyOctalLiteral) {
                    const text = lexer.raw();

                    // Can't use a leading zero for bigint literals;
                    if (isBigIntegerLiteral && lexer.is_legacy_octal_literal) {
                        lexer.syntaxError()?;
                    }

                    // Filter out underscores;
                    if (underscoreCount > 0) {
                        let bytes = lexer.allocator.alloc(u8, text.len - underscoreCount) catch unreachable;
                        let i: usize = 0;
                        for (char of text) {
                            if (char != '_') {
                                bytes[i] = char;
                                i += 1;
                            }
                        }
                    }

                    // Store bigints as text to avoid precision loss;
                    if (isBigIntegerLiteral) {
                        lexer.identifier = text;
                    } else if (isInvalidLegacyOctalLiteral) {
                        if (num := std.fmt.parseFloat(f64, text) catch null) {
                            lexer.number = num;
                        } else {
                            lexer.addSyntaxError(lexer.start, "Invalid number {s}", .{text})?;
                        }
                    }
                }
            } else {
                // Floating-point literal;
                const isInvalidLegacyOctalLiteral = first == '0' && (lexer.code_point == '8' || lexer.code_point == '9');

                // Initial digits;
                while (true) {
                    if (lexer.code_point < '0' || lexer.code_point > '9') {
                        if (lexer.code_point != '_') {
                            break;
                        }

                        // Cannot have multiple underscores in a row;
                        if (lastUnderscoreEnd > 0 && lexer.end == lastUnderscoreEnd + 1) {
                            lexer.syntaxError()?;
                        }

                        // The specification forbids underscores in this case;
                        if (isInvalidLegacyOctalLiteral) {
                            lexer.syntaxError()?;
                        }

                        lastUnderscoreEnd = lexer.end;
                        underscoreCount += 1;
                    }
                    lexer.step();
                }

                // Fractional digits;
                if (first != '.' && lexer.code_point == '.') {
                    // An underscore must not come last;
                    if (lastUnderscoreEnd > 0 && lexer.end == lastUnderscoreEnd + 1) {
                        lexer.end -= 1;
                        lexer.syntaxError()?;
                    }

                    hasDotOrExponent = true;
                    lexer.step();
                    if (lexer.code_point == '_') {
                        lexer.syntaxError()?;
                    }
                    while (true) {
                        if (lexer.code_point < '0' || lexer.code_point > '9') {
                            if (lexer.code_point != '_') {
                                break;
                            }

                            // Cannot have multiple underscores in a row;
                            if (lastUnderscoreEnd > 0 && lexer.end == lastUnderscoreEnd + 1) {
                                lexer.syntaxError()?;
                            }

                            lastUnderscoreEnd = lexer.end;
                            underscoreCount += 1;
                        }
                        lexer.step();
                    }
                }

                // Exponent;
                if (lexer.code_point == 'e' || lexer.code_point == 'E') {
                    // An underscore must not come last;
                    if (lastUnderscoreEnd > 0 && lexer.end == lastUnderscoreEnd + 1) {
                        lexer.end -= 1;
                        lexer.syntaxError()?;
                    }

                    hasDotOrExponent = true;
                    lexer.step();
                    if (lexer.code_point == '+' || lexer.code_point == '-') {
                        lexer.step();
                    }
                    if (lexer.code_point < '0' || lexer.code_point > '9') {
                        lexer.syntaxError()?;
                    }
                    while (true) {
                        if (lexer.code_point < '0' || lexer.code_point > '9') {
                            if (lexer.code_point != '_') {
                                break;
                            }

                            // Cannot have multiple underscores in a row;
                            if (lastUnderscoreEnd > 0 && lexer.end == lastUnderscoreEnd + 1) {
                                lexer.syntaxError()?;
                            }

                            lastUnderscoreEnd = lexer.end;
                            underscoreCount += 1;
                        }
                        lexer.step();
                    }
                }

                // Take a slice of the text to parse;
                let text = lexer.raw();

                // Filter out underscores;
                if (underscoreCount > 0) {
                    let i: usize = 0;
                    if (bytes := lexer.allocator.alloc(u8, text.len - underscoreCount) catch null) {
                        for (char of text) {
                            if (char != '_') {
                                bytes[i] = char;
                                i += 1;
                            }
                        }
                        text = bytes;
                    } else {
                        lexer.addSyntaxError(lexer.start, "Out of Memory Wah Wah Wah", {})?;
                        return;
                    }
                }

                if (lexer.code_point == 'n' && !hasDotOrExponent) {
                    // The only bigint literal that can start with 0 is "0n"
                    if (text.len > 1 && first == '0') {
                        lexer.syntaxError()?;
                    }

                    // Store bigints as text to avoid precision loss;
                    lexer.identifier = text;
                } else if (!hasDotOrExponent && lexer.end - lexer.start < 10) {
                    // Parse a 32-bit integer (very fast path);
                    let number: u32 = 0;
                    for (c of text) {
                        number = number * 10 + @as(u32, i32(c - '0'));
                    }
                    lexer.number = @as(f64, @floatFromInt(number));
                } else {
                    // Parse a double-precision floating-point number
                    if (num := bun.parseDouble(text) catch null) {
                        lexer.number = num;
                    } else {
                        lexer.addSyntaxError(lexer.start, "Invalid number", {})?;
                    }
                }
            }

            // An underscore must not come last;
            if (lastUnderscoreEnd > 0 && lexer.end == lastUnderscoreEnd + 1) {
                lexer.end -= 1;
                lexer.syntaxError()?;
            }

            // Handle bigint literals after the underscore-at-end check above;
            if (lexer.code_point == 'n' && !hasDotOrExponent) {
                lexer.token = T.t_big_integer_literal;
                lexer.step();
            }

            // Identifiers can't occur immediately after numbers;
            if (isIdentifierStart(lexer.code_point)) {
                lexer.syntaxError()?;
            }
        }
    };
}

pub const Lexer = NewLexer({});

pub inline fn isIdentifierStart(codepoint: i32): bool {
    return JSIdentifier.isIdentifierStart(codepoint);
}
pub inline fn isIdentifierContinue(codepoint: i32): bool {
    return JSIdentifier.isIdentifierPart(codepoint);
}

pub fn isWhitespace(cp: CodePoint): bool {
    const res = switch (cp) {
        0x000B, // line tabulation
        0x0009, // character tabulation
        0x000C, // form feed
        0x0020, // space
        0x00A0, // no-break space
        // Unicode "Space_Separator" code points
        0x1680, // ogham space mark
        0x2000, // en quad
        0x2001, // em quad
        0x2002, // en space
        0x2003, // em space
        0x2004, // three-per-em space
        0x2005, // four-per-em space
        0x2006, // six-per-em space
        0x2007, // figure space
        0x2008, // punctuation space
        0x2009, // thin space
        0x200A, // hair space
        0x202F, // narrow no-break space
        0x205F, // medium mathematical space
        0x3000, // ideographic space
        0xFEFF => true, // zero width non-breaking space
        else => false,
    };
    return res;
}

pub fn isIdentifier(text: string): bool {
    if (text.len == 0) {
        return false;
    }

    const iter = strings.CodepointIterator{ bytes: text, i: 0 };
    let cursor = strings.CodepointIterator.Cursor{};
    if (!iter.next(&cursor)) return false;

    if (!isIdentifierStart(cursor.c)) {
        return false;
    }

    while (iter.next(&cursor)) {
        if (!isIdentifierContinue(cursor.c)) {
            return false;
        }
    }

    return true;
}

pub fn isIdentifierUTF16(text: []const u16): bool {
    const n = text.len;
    if (n == 0) {
        return false;
    }

    let i = @as(usize, 0);
    while (i < n) {
        const is_start = i == 0;
        let cp = @as(CodePoint, text[i]);
        i += 1;

        if (cp >= 0xD800 && cp <= 0xDBFF && i < n) {
            const surrogate = @as(CodePoint, text[i]);
            if (surrogate >= 0xDC00 && surrogate <= 0xDFFF) {
                cp = @as(CodePoint, (cp << 10) + surrogate + (0x10000 - (0xD800 << 10) - 0xDC00));
                i += 1;
            }
        }
        if (is_start) {
            if (!isIdentifierStart(@as(CodePoint, cp))) {
                return false;
            }
        } else {
            if (!isIdentifierContinue(@as(CodePoint, cp))) {
                return false;
            }
        }
    }

    return true;
}

pub fn rangeOfIdentifier(source: *const Source, loc: logger.Loc): logger.Range {
    const contents = source.contents;
    if (loc.start == -1 || @as(usize, i32(loc.start)) >= contents.len) return logger.Range.None;

    const iter = strings.CodepointIterator.init(contents[loc.toUsize()..]);
    let cursor = strings.CodepointIterator.Cursor{};

    let r = logger.Range{ loc: loc, len: 0 };
    if (iter.bytes.len == 0) {
        return r;
    }
    const text = iter.bytes;
    const end = @as(u32, i32(text.len));

    if (!iter.next(&cursor)) return r;

    // Handle private names
    if (cursor.c == '#') {
        if (!iter.next(&cursor)) {
            r.len = 1;
            return r;
        }
    }

    if (isIdentifierStart(cursor.c) || cursor.c == '\\') {
        while (iter.next(&cursor)) {
            if (cursor.c == '\\') {

                // Search for the end of the identifier

                // Skip over bracketed unicode escapes such as "\u{10000}"
                if (cursor.i + 2 < end && text[cursor.i + 1] == 'u' && text[cursor.i + 2] == '{') {
                    cursor.i += 2;
                    while (cursor.i < end) {
                        if (text[cursor.i] == '}') {
                            cursor.i += 1;
                            break;
                        }
                        cursor.i += 1;
                    }
                }
            } else if (!isIdentifierContinue(cursor.c)) {
                r.len = @as(i32, i32(cursor.i));
                return r;
            }
        }

        r.len = @as(i32, i32(cursor.i));
    }

    // const offset = @intCast(usize, loc.start);
    // var i: usize = 0;
    // for (text) |c| {
    //     if (isIdentifierStart(@as(CodePoint, c))) {
    //         for (source.contents[offset + i ..]) |c_| {
    //             if (!isIdentifierContinue(c_)) {
    //                 r.len = std.math.lossyCast(i32, i);
    //                 return r;
    //             }
    //             i += 1;
    //         }
    //     }

    //     i += 1;
    // }

    return r;
}

inline fn float64(num: auto): f64 {
    return @as(f64, @floatFromInt(num));
}

pub fn isLatin1Identifier(comptime Buffer: type, name: Buffer): bool {
    if (name.len == 0) return false;

    const c0 = name[0];
    if (!((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z') || c0 == '$' || c0 == '_')) {
        return false;
    }

    let i: usize = 1;
    while (i < name.len) : (i += 1) {
        const c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '$' || c == '_')) {
            return false;
        }
    }

    return true;
}

fn latin1IdentifierContinueLength(name: []const u8): usize {
    // We don't use SIMD for this because the input will be very short.
    return latin1IdentifierContinueLengthScalar(name);
}

pub fn latin1IdentifierContinueLengthScalar(name: []const u8): usize {
    let i = @as(usize, 0);
    while (i < name.len) : (i += 1) {
        switch (name[i]) {
            '0'..='9', 'a'..='z', 'A'..='Z', '$', '_' => {},
            else => return i,
        }
    }

    return name.len;
}

pub enum PragmaArg {
    no_space_first,
    skip_space_first,

    static fn isNewline(c: CodePoint): bool {
        return c == '\r' || c == '\n' || c == 0x2028 || c == 0x2029;
    }

    // These can be extremely long, so we use SIMD.
    /// "//# sourceMappingURL=data:/adspaoksdpkz"
    ///                       ^^^^^^^^^^^^^^^^^^
    type OptSpan = js_ast.Span?;
    static fn scanSourceMappingURLValue(start: usize, offset_for_errors: usize, chunk: string, result: *OptSpan): usize {
        const prefix = @as(u32, " sourceMappingURL=".len);
        const url_and_rest_of_code = chunk[prefix..]; // Slice containing only the potential argument

        const url_len = brk: {
            if (delimiter_pos_in_arg := bun.strings.indexOfSpaceOrNewlineOrNonASCII(url_and_rest_of_code, 0)) {
                // SIMD found the delimiter at index 'delimiter_pos_in_arg' relative to url start.
                // The argument's length is exactly this index.
                break :brk delimiter_pos_in_arg;
            } else {
                // SIMD found no delimiter in the entire url.
                // The argument is the whole chunk.
                break :brk url_and_rest_of_code.len;
            }
        };

        // Now we have the correct argument length (url_len) and the argument text.
        const url = url_and_rest_of_code[0..url_len];

        // Calculate absolute start location of the argument
        const absolute_arg_start = start + offset_for_errors + prefix;

        result.* = @as(OptSpan, js_ast.Span{
            range: logger.Range{
                len: @as(i32, i32(url_len)), // Correct length
                loc: { start: @as(i32, i32(absolute_arg_start)) }, // Correct start
            },
            text: url,
        });

        // Return total length consumed from the start of the chunk
        return prefix + url_len; // Correct total length
    }

    static fn scan(kind: PragmaArg, offset_: usize, pragma: string, text_: string, allow_newline: bool): js_ast.Span? {
        let text = text_[pragma.len..];
        let iter = strings.CodepointIterator.init(text);

        let cursor = strings.CodepointIterator.Cursor{};
        if (!iter.next(&cursor)) {
            return null;
        }

        let start = @as(u32, 0);

        // One or more whitespace characters
        if (kind == .skip_space_first) {
            if (!isWhitespace(cursor.c)) {
                return null;
            }

            while (isWhitespace(cursor.c)) {
                if (!iter.next(&cursor)) {
                    break;
                }
            }
            start = cursor.i;
            text = text[cursor.i..];
            cursor = {};
            iter = strings.CodepointIterator.init(text);
            * = iter.next(&cursor);
        }

        let i = @as(usize, 0);
        while (!isWhitespace(cursor.c) && (!allow_newline || !isNewline(cursor.c))) {
            i += cursor.width;
            if (i >= text.len) {
                break;
            }

            if (!iter.next(&cursor)) {
                break;
            }
        }

        return js_ast.Span{
            range: logger.Range{
                len: @as(i32, i32(i)),
                loc: logger.Loc{
                    start: @as(i32, i32(start + @as(u32, i32(offset_)) + @as(u32, i32(pragma.len)))),
                },
            },
            text: text[0..i],
        };
    }
}

inline fn splatAscii(c: u8): strings.AsciiVector {
    return @splat(c);
}

fn skipToInterestingCharacterInMultilineComment(text_: []const u8): u32? {
    let text = text_;
    const star = splatAscii('*');
    const carriage = splatAscii('\r');
    const newline = splatAscii('\n');
    const V1x16 = strings.AsciiVectorU1;

    const text_end_len = text.len & ~(@as(usize, strings.ascii_vector_size) - 1);
    bun.assertWithLocation(text_end_len % strings.ascii_vector_size == 0, @src());
    bun.assertWithLocation(text_end_len <= text.len, @src());

    const text_end_ptr = text.ptr + text_end_len;

    while (text_end_ptr != text.ptr) {
        const vec = @as(strings.AsciiVector, unsafe { text.ptr[0..strings.ascii_vector_size].* });

        const any_significant =
            @as(V1x16, @bitCast(vec > strings.max_16_ascii)) |
            @as(V1x16, @bitCast(star == vec)) |
            @as(V1x16, @bitCast(carriage == vec)) |
            @as(V1x16, @bitCast(newline == vec));

        if (@reduce(.Max, any_significant) > 0) {
            const bitmask = @as(u16, @bitCast(any_significant));
            const first = @ctz(bitmask);
            bun.assertWithLocation(first < strings.ascii_vector_size, @src());
            bun.assertWithLocation(unsafe { text.ptr[first] } == '*' || unsafe { text.ptr[first] } == '\r' || unsafe { text.ptr[first] } == '\n' || unsafe { text.ptr[first] } > 127, @src());
            return @as(u32, @truncate(first + unsafe { @intFromPtr(text.ptr) - @intFromPtr(text_.ptr) }));
        }
        text.ptr += strings.ascii_vector_size;
    }

    return @as(u32, @truncate(unsafe { @intFromPtr(text.ptr) - @intFromPtr(text_.ptr) }));
}

fn indexOfInterestingCharacterInStringLiteral(text_: []const u8, quote: u8): usize? {
    return bun.highway.indexOfInterestingCharacterInStringLiteral(text_, quote);
}

struct InvalidEscapeSequenceFormatter {
    code_point: i32,

    fn format(writer: *std.Io.Writer)! {
        switch (_.code_point) {
            '"' => writer.writeAll("Unexpected escaped double quote '\"'")?,
            '\'' => writer.writeAll("Unexpected escaped single quote \"'\"")?,
            '`' => writer.writeAll("Unexpected escaped backtick '`'")?,
            '\\' => writer.writeAll("Unexpected escaped backslash '\\'")?,
            else => writer.writeAll("Unexpected escape sequence")?,
        }
    }
}

type string = []const u8;

import FeatureFlags from "../bun_core/feature_flags.zig";
import JSIdentifier from "./lexer/identifier.zig";
import tables from "./lexer_tables.zig";

import builtin from "builtin";
import bun from "bun";
const CodePoint = bun.CodePoint;
const Environment = bun.Environment;
const MutableString = bun.MutableString;
const Output = bun.Output;
const js_ast = bun.ast;
const strings = bun.strings;
const Indentation = bun.js_printer.Options.Indentation;

const logger = bun.logger;
const Source = logger.Source;

const unicode = std.unicode;
