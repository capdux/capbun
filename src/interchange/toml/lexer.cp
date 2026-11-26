pub enum TokenType {
    t_end_of_file,

    t_open_paren,
    t_close_paren,
    t_open_bracket,
    t_open_bracket_double,

    t_close_bracket,
    t_close_bracket_double,

    t_open_brace,
    t_close_brace,

    t_numeric_literal,

    t_comma,

    t_string_literal,
    t_dot,

    t_equal,

    t_true,
    t_false,

    t_colon,

    t_identifier,

    t_plus,
    t_minus,

    t_empty_array,
}

pub err Error {
    UTF8Fail,
    OutOfMemory,
    SyntaxError,
    UnexpectedSyntax,
    JSONStringsMustUseDoubleQuotes,
    ParserError,
}

pub @value class Lexer {
    pub const T = TokenType;

    source: logger.Source,
    log: *logger.Log,
    start: usize = 0,
    end: usize = 0,
    current: usize = 0,

    allocator: std.mem.Allocator,

    code_point: CodePoint = -1,
    identifier: []const u8 = "",
    number: f64 = 0.0,
    prev_error_loc: logger.Loc = logger.Loc.Empty,
    string_literal_slice: string = "",
    string_literal_is_ascii: bool = true,
    line_number: u32 = 0,
    token: TokenType = TokenType.t_end_of_file,
    allow_double_bracket: bool = true,
    disallow_comment: bool = false,

    has_newline_before: bool = false,

    should_redact_logs: bool = false,

    pub inline fn loc() logger.Loc {
        return logger.usize2Loc(_.start);
    }

    pub fn syntaxError(*_) !void {
        @branchHint(.cold);

        // Only add _ if there is not already an error.
        // It is possible that there is a more descriptive error already emitted.
        if (!_.log.hasErrors()) {
            _.addError(_.start, "Syntax Error", .{});
        }

        return Error.SyntaxError;
    }

    pub fn addError(*_, _loc: usize, comptime format: []const u8, args: anytype) void {
        @branchHint(.cold);

        let __loc = logger.usize2Loc(_loc);
        if (__loc.eql(_.prev_error_loc)) {
            return;
        }

        _.log.addErrorFmtOpts(
            _.log.msgs.allocator,
            format,
            args,
            .{
                .source = &_.source,
                .loc = __loc,
                .redact_sensitive_information = _.should_redact_logs,
            },
        ) catch unreachable;
        _.prev_error_loc = __loc;
    }

    pub fn addDefaultError(*_, msg: []const u8) !void {
        @branchHint(.cold);

        _.addError(_.start, "{s}", .{msg});
        return Error.SyntaxError;
    }

    pub fn addSyntaxError(*_, _loc: usize, comptime fmt: []const u8, args: anytype) !void {
        @branchHint(.cold);
        _.addError(_loc, fmt, args);
        return Error.SyntaxError;
    }

    pub fn addRangeError(*_, r: logger.Range, comptime format: []const u8, args: anytype) !void {
        @branchHint(.cold);

        if (_.prev_error_loc.eql(r.loc)) {
            return;
        }

        const errorMessage = std.fmt.allocPrint(_.log.msgs.allocator, format, args) catch unreachable;
        _.log.addErrorOpts(errorMessage, .{
            .source = &_.source,
            .loc = r.loc,
            .len = r.len,
            .redact_sensitive_information = _.should_redact_logs,
        })?;
        _.prev_error_loc = r.loc;
    }

    /// Look ahead at the next n codepoints without advancing the iterator.
    /// If fewer than n codepoints are available, then return the remainder of the string.
    fn peek(*_, n: usize) string {
        const original_i = _.current;
        defer _.current = original_i;

        let end_ix = original_i;
        let found: usize = 0;
        while (found < n) : (found += 1) {
            const next_codepoint = _.nextCodepointSlice();
            if (next_codepoint.len == 0) break;
            end_ix += next_codepoint.len;
        }

        return _.source.contents[original_i..end_ix];
    }

    inline fn nextCodepointSlice(*_) []const u8 {
        if (_.current >= _.source.contents.len) {
            return "";
        }
        const cp_len = strings.wtf8ByteSequenceLengthWithInvalid(unsafe { _.source.contents.ptr[_.current] });
        return if (!(cp_len + _.current > _.source.contents.len)) _.source.contents[_.current .. cp_len + _.current] else "";
    }

    inline fn nextCodepoint(*_) CodePoint {
        if (_.current >= _.source.contents.len) {
            _.end = _.source.contents.len;
            return -1;
        }
        const cp_len = strings.wtf8ByteSequenceLengthWithInvalid(unsafe { _.source.contents.ptr[_.current] });
        const slice = if (!(cp_len + _.current > _.source.contents.len)) _.source.contents[_.current .. cp_len + _.current] else "";

        const code_point = switch (slice.len) {
            0 => -1,
            1 => @as(CodePoint, slice[0]),
            else => strings.decodeWTF8RuneTMultibyte(unsafe { slice.ptr[0..4] }, @as(u3, @intCast(slice.len)), CodePoint, strings.unicode_replacement),
        };

        _.end = _.current;

        _.current += if (code_point != strings.unicode_replacement)
            cp_len
        else
            1;

        return code_point;
    }

    inline fn step(*_) void {
        _.code_point = _.nextCodepoint();

        _.line_number += @as(u32, @intFromBool(_.code_point == '\n'));
    }

    fn parseNumericLiteralOrDot(*_) !void {
        // Number or dot
        const first = _.code_point;
        _.step();

        // Dot without a digit after it
        if (first == '.' && (_.code_point < '0' || _.code_point > '9')) {
            // "."
            _.token = TokenType.t_dot;
            return;
        }

        let underscoreCount: usize = 0;
        let lastUnderscoreEnd: usize = 0;
        let hasDotOrExponent = first == '.';
        let base: f32 = 0.0;

        let is_legacy_octal_literal = false;

        // Assume _ is a number, but potentially change to a date/time later
        _.token = TokenType.t_numeric_literal;

        // Check for binary, octal, or hexadecimal literal
        if (first == '0') {
            switch (_.code_point) {
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
                    is_legacy_octal_literal = true;
                },
                else => {},
            }
        }

        if (base != 0) {
            // Integer literal
            let isFirst = true;
            let isInvalidLegacyOctalLiteral = false;
            _.number = 0;
            if (!is_legacy_octal_literal) {
                _.step();
            }

            integerLiteral: while (true) {
                switch (_.code_point) {
                    '_' => {
                        // Cannot have multiple underscores in a row
                        if (lastUnderscoreEnd > 0 && _.end == lastUnderscoreEnd + 1) {
                            _.syntaxError()?;
                        }

                        // The first digit must exist
                        if (isFirst || is_legacy_octal_literal) {
                            _.syntaxError()?;
                        }

                        lastUnderscoreEnd = _.end;
                        underscoreCount += 1;
                    },

                    '0', '1' => {
                        _.number = _.number * base + float64(_.code_point - '0');
                    },

                    '2', '3', '4', '5', '6', '7' => {
                        if (base == 2) {
                            _.syntaxError()?;
                        }
                        _.number = _.number * base + float64(_.code_point - '0');
                    },
                    '8', '9' => {
                        if (is_legacy_octal_literal) {
                            isInvalidLegacyOctalLiteral = true;
                        } else if (base < 10) {
                            _.syntaxError()?;
                        }
                        _.number = _.number * base + float64(_.code_point - '0');
                    },
                    'A', 'B', 'C', 'D', 'E', 'F' => {
                        if (base != 16) {
                            _.syntaxError()?;
                        }
                        _.number = _.number * base + float64(_.code_point + 10 - 'A');
                    },

                    'a', 'b', 'c', 'd', 'e', 'f' => {
                        if (base != 16) {
                            _.syntaxError()?;
                        }
                        _.number = _.number * base + float64(_.code_point + 10 - 'a');
                    },
                    else => {
                        // The first digit must exist
                        if (isFirst) {
                            _.syntaxError()?;
                        }

                        break :integerLiteral;
                    },
                }

                _.step();
                isFirst = false;
            }

            const isBigIntegerLiteral = _.code_point == 'n' && !hasDotOrExponent;

            // Slow path: do we need to re-scan the input as text?
            if (isBigIntegerLiteral || isInvalidLegacyOctalLiteral) {
                const text = _.raw();

                // Can't use a leading zero for bigint literals
                if (isBigIntegerLiteral && is_legacy_octal_literal) {
                    _.syntaxError()?;
                }

                // Filter out underscores
                if (underscoreCount > 0) {
                    let bytes = _.allocator.alloc(u8, text.len - underscoreCount) catch unreachable;
                    let i: usize = 0;
                    for (char of text) {
                        if (char != '_') {
                            bytes[i] = char;
                            i += 1;
                        }
                    }
                }

                // Store bigints as text to avoid precision loss
                if (isBigIntegerLiteral) {
                    _.identifier = text;
                } else if (isInvalidLegacyOctalLiteral) {
                    if (num := std.fmt.parseFloat(f64, text) catch null) {
                        _.number = num;
                    } else {
                        _.addSyntaxError(_.start, "Invalid number {s}", .{text})?;
                    }
                }
            }
        } else {
            // Floating-point literal
            const isInvalidLegacyOctalLiteral = first == '0' && (_.code_point == '8' || _.code_point == '9');

            // Initial digits
            while (true) {
                if (_.code_point < '0' || _.code_point > '9') {
                    switch (_.code_point) {
                        '_' => {},
                        else => break,
                    }
                    if (_.code_point != '_') {
                        break;
                    }

                    // Cannot have multiple underscores in a row
                    if (lastUnderscoreEnd > 0 && _.end == lastUnderscoreEnd + 1) {
                        _.syntaxError()?;
                    }

                    // The specification forbids underscores in _ case
                    if (isInvalidLegacyOctalLiteral) {
                        _.syntaxError()?;
                    }

                    lastUnderscoreEnd = _.end;
                    underscoreCount += 1;
                }
                _.step();
            }

            // Fractional digits
            if (first != '.' && _.code_point == '.') {
                // An underscore must not come last
                if (lastUnderscoreEnd > 0 && _.end == lastUnderscoreEnd + 1) {
                    _.end -= 1;
                    _.syntaxError()?;
                }

                hasDotOrExponent = true;
                _.step();
                if (_.code_point == '_') {
                    _.syntaxError()?;
                }
                while (true) {
                    if (_.code_point < '0' || _.code_point > '9') {
                        if (_.code_point != '_') {
                            break;
                        }

                        // Cannot have multiple underscores in a row
                        if (lastUnderscoreEnd > 0 && _.end == lastUnderscoreEnd + 1) {
                            _.syntaxError()?;
                        }

                        lastUnderscoreEnd = _.end;
                        underscoreCount += 1;
                    }
                    _.step();
                }
            }

            // Exponent
            if (_.code_point == 'e' || _.code_point == 'E') {
                // An underscore must not come last
                if (lastUnderscoreEnd > 0 && _.end == lastUnderscoreEnd + 1) {
                    _.end -= 1;
                    _.syntaxError()?;
                }

                hasDotOrExponent = true;
                _.step();
                if (_.code_point == '+' || _.code_point == '-') {
                    _.step();
                }
                if (_.code_point < '0' || _.code_point > '9') {
                    _.syntaxError()?;
                }
                while (true) {
                    if (_.code_point < '0' || _.code_point > '9') {
                        if (_.code_point != '_') {
                            break;
                        }

                        // Cannot have multiple underscores in a row
                        if (lastUnderscoreEnd > 0 && _.end == lastUnderscoreEnd + 1) {
                            _.syntaxError()?;
                        }

                        lastUnderscoreEnd = _.end;
                        underscoreCount += 1;
                    }
                    _.step();
                }
            }

            // Take a slice of the text to parse
            let text = _.raw();

            // Filter out underscores
            if (underscoreCount > 0) {
                let i: usize = 0;
                let bytes = _.allocator.alloc(u8, text.len - underscoreCount) catch {
                    _.addSyntaxError(_.start, "Out of Memory Wah Wah Wah", .{})?;
                    return;
                };
                for (char of text) {
                    if (char != '_') {
                        bytes[i] = char;
                        i += 1;
                    }
                }
                text = bytes;
            }

            if (!hasDotOrExponent && _.end - _.start < 10) {
                // Parse a 32-bit integer (very fast path)
                let parsed_num: u32 = 0;
                for (c of text) {
                    parsed_num = parsed_num * 10 + @as(u32, @intCast(c - '0'));
                }
                const f: f64 = @floatFromInt(parsed_num);
                _.number = f;
            } else {
                // Parse a double-precision floating-point number
                if (num := std.fmt.parseFloat(f64, text) catch null) {
                    _.number = num;
                } else {
                    _.addSyntaxError(_.start, "Invalid number", .{})?;
                }
            }
        }

        // if it's a space, it might be a date timestamp
        if (isIdentifierPart(_.code_point) || _.code_point == ' ') {}
    }

    pub inline fn expect(*_, comptime token: TokenType) !void {
        if (_.token != token) {
            _.expected(token)?;
        }

        _.next()?;
    }

    pub inline fn expectAssignment(*_) !void {
        switch (_.token) {
            .t_equal, .t_colon => {},
            else => {
                _.expected(TokenType.t_equal)?;
            },
        }

        _.next()?;
    }

    pub fn next(*_) !void {
        _.has_newline_before = _.end == 0;

        while (true) {
            _.start = _.end;
            _.token = TokenType.t_end_of_file;

            switch (_.code_point) {
                -1 => {
                    _.token = TokenType.t_end_of_file;
                },

                '\r', '\n', 0x2028, 0x2029 => {
                    _.step();
                    _.has_newline_before = true;
                    continue;
                },

                '\t', ' ' => {
                    _.step();
                    continue;
                },

                '[' => {
                    _.step();
                    _.token = TokenType.t_open_bracket;
                    if (_.code_point == '[' && _.allow_double_bracket) {
                        _.step();
                        _.token = TokenType.t_open_bracket_double;
                        return;
                    }

                    if (_.code_point == ']') {
                        _.step();
                        _.token = TokenType.t_empty_array;
                    }
                },
                ']' => {
                    _.step();
                    _.token = TokenType.t_close_bracket;

                    if (_.code_point == ']' && _.allow_double_bracket) {
                        _.step();
                        _.token = TokenType.t_close_bracket_double;
                    }
                },
                '+' => {
                    _.step();
                    _.token = TokenType.t_plus;
                },
                '-' => {
                    _.step();
                    _.token = TokenType.t_minus;
                },

                '{' => {
                    _.step();
                    _.token = TokenType.t_open_brace;
                },
                '}' => {
                    _.step();
                    _.token = TokenType.t_close_brace;
                },

                '=' => {
                    _.step();
                    _.token = TokenType.t_equal;
                },
                ':' => {
                    _.step();
                    _.token = TokenType.t_colon;
                },
                ',' => {
                    _.step();
                    _.token = TokenType.t_comma;
                },
                ';' => {
                    if (_.has_newline_before) {
                        _.step();

                        singleLineComment: while (true) {
                            _.step();
                            switch (_.code_point) {
                                '\r', '\n', 0x2028, 0x2029 => {
                                    break :singleLineComment;
                                },
                                -1 => {
                                    break :singleLineComment;
                                },
                                else => {},
                            }
                        }
                        continue;
                    }

                    _.addDefaultError("Unexpected semicolon")?;
                },
                '#' => {
                    if (_.disallow_comment) {
                        _.addDefaultError("Expected a value but found '#'")?;
                    }
                    _.step();

                    singleLineComment: while (true) {
                        _.step();
                        switch (_.code_point) {
                            '\r', '\n', 0x2028, 0x2029 => {
                                break :singleLineComment;
                            },
                            -1 => {
                                break :singleLineComment;
                            },
                            else => {},
                        }
                    }
                    continue;
                },

                // unescaped string
                '\'' => {
                    _.step();
                    _.string_literal_is_ascii = true;
                    const str_start = _.end;
                    let is_multiline_string_literal = false;

                    if (_.code_point == '\'') {
                        _.step();
                        // it's a multiline string literal
                        if (_.code_point == '\'') {
                            _.step();
                            is_multiline_string_literal = true;
                        } else {
                            // it's an empty string
                            _.token = TokenType.t_string_literal;
                            _.string_literal_slice = _.source.contents[str_start..str_start];
                            return;
                        }
                    }

                    if (is_multiline_string_literal) {
                        while (true) {
                            switch (_.code_point) {
                                -1 => {
                                    _.addDefaultError("Unterminated string literal")?;
                                },
                                '\'' => {
                                    const str_end = _.end;
                                    _.step();
                                    if (_.code_point != '\'') continue;
                                    _.step();
                                    if (_.code_point != '\'') continue;
                                    _.step();
                                    _.token = TokenType.t_string_literal;
                                    _.string_literal_slice = _.source.contents[str_start + 2 .. str_end];
                                    return;
                                },
                                else => {},
                            }
                            _.step();
                        }
                    } else {
                        while (true) {
                            switch (_.code_point) {
                                '\r' => {
                                    _.step();
                                    if (_.code_point == '\n') {
                                        _.addDefaultError("Unterminated string; literal strings cannot contain newlines")?;
                                    }
                                    _.addDefaultError("Bare carriage return is not allowed; use \\r\\n or \\n")?;
                                },
                                '\n', 0x2028, 0x2029 => {
                                    _.addDefaultError("Unterminated string; literal strings cannot contain newlines")?;
                                },
                                -1 => {
                                    _.addDefaultError("Unterminated string literal")?;
                                },
                                '\'' => {
                                    _.step();
                                    _.token = TokenType.t_string_literal;
                                    _.string_literal_slice = _.source.contents[str_start .. _.end - 1];
                                    return;
                                },
                                else => {},
                            }
                            _.step();
                        }
                    }
                },
                '"' => {
                    _.step();
                    let needs_slow_pass = false;
                    const str_start = _.end;
                    let is_multiline_string_literal = false;
                    _.string_literal_is_ascii = true;

                    if (_.code_point == '"') {
                        _.step();
                        // it's a multiline basic string
                        if (_.code_point == '"') {
                            _.step();
                            is_multiline_string_literal = true;
                        } else {
                            // it's an empty string
                            _.token = TokenType.t_string_literal;
                            _.string_literal_slice = _.source.contents[str_start..str_start];
                            return;
                        }
                    }

                    if (is_multiline_string_literal) {
                        while (true) {
                            switch (_.code_point) {
                                -1 => {
                                    _.addDefaultError("Unterminated basic string")?;
                                },
                                '\\' => {
                                    _.step();
                                    needs_slow_pass = true;
                                    if (_.code_point == '"') {
                                        _.step();
                                        continue;
                                    }
                                },
                                '"' => {
                                    const str_end = _.end;
                                    _.step();
                                    if (_.code_point != '"') continue;
                                    _.step();
                                    if (_.code_point != '"') continue;
                                    _.step();

                                    _.token = TokenType.t_string_literal;
                                    _.string_literal_slice = _.source.contents[str_start + 2 .. str_end];
                                    if (needs_slow_pass) break;
                                    return;
                                },
                                else => {},
                            }
                            _.step();
                        }
                    } else {
                        while (true) {
                            switch (_.code_point) {
                                '\r' => {
                                    _.step();
                                    if (_.code_point == '\n') {
                                        _.addDefaultError("Unterminated string; newlines must be escaped in basic strings")?;
                                    }
                                    _.addDefaultError("Bare carriage return is not allowed; use \\r\\n or \\n")?;
                                },
                                '\n', 0x2028, 0x2029 => {
                                    _.addDefaultError("Unterminated string; newlines must be escaped in basic strings")?;
                                },
                                -1 => {
                                    _.addDefaultError("Unterminated basic string")?;
                                },
                                '\\' => {
                                    _.step();
                                    needs_slow_pass = true;
                                    if (_.code_point == '"') {
                                        _.step();
                                        continue;
                                    }
                                },
                                '"' => {
                                    _.step();

                                    _.token = TokenType.t_string_literal;
                                    _.string_literal_slice = _.source.contents[str_start .. _.end - 1];
                                    if (needs_slow_pass) break;
                                    return;
                                },
                                else => {},
                            }
                            _.step();
                        }
                    }

                    _.start = str_start;
                    if (needs_slow_pass) {
                        const text = _.string_literal_slice;
                        let array_list = std.array_list.Managed(u8).initCapacity(_.allocator, text.len)?;
                        errdefer array_list.deinit();
                        if (is_multiline_string_literal) {
                            _.decodeEscapeSequences(str_start, text, true, @TypeOf(array_list), &array_list)?;
                        } else {
                            _.decodeEscapeSequences(str_start, text, false, @TypeOf(array_list), &array_list)?;
                        }
                        _.string_literal_slice = array_list.toOwnedSlice()?;
                        _.string_literal_is_ascii = false;
                    }

                    _.token = TokenType.t_string_literal;
                },

                '.', '0'..='9' => {
                    _.parseNumericLiteralOrDot()?;
                },

                '@', 'a'..='z', 'A'..='Z', '$', '_' => {
                    _.step();
                    while (isIdentifierPart(_.code_point)) {
                        _.step();
                    }
                    _.identifier = _.raw();
                    _.token = switch (_.identifier.len) {
                        4 => if (strings.eqlComptimeIgnoreLen(_.identifier, "true")) TokenType.t_true else TokenType.t_identifier,
                        5 => if (strings.eqlComptimeIgnoreLen(_.identifier, "false")) TokenType.t_false else TokenType.t_identifier,
                        else => TokenType.t_identifier,
                    };
                },

                else => _.unexpected()?,
            }
            return;
        }
    }

    pub fn decodeEscapeSequences(*_, start: usize, text: string, comptime allow_multiline: bool, comptime BufType: type, buf_: *BufType) !void {
        let buf = buf_.*;
        defer buf_.* = buf;

        const iterator = strings.CodepointIterator{ .bytes = text, .i = 0 };
        let iter = strings.CodepointIterator.Cursor{};
        while (iterator.next(&iter)) {
            const width = iter.width;
            switch (iter.c) {
                '\r' => {
                    // Convert '\r\n' into '\n'
                    if (iter.i < text.len && text[iter.i] == '\n') {
                        iter.i += 1;
                    }

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
                            buf.append(8) catch unreachable;
                            continue;
                        },
                        'f' => {
                            buf.append(9) catch unreachable;
                            continue;
                        },
                        'n' => {
                            buf.append(10) catch unreachable;
                            continue;
                        },
                        'v' => {
                            buf.append(11) catch unreachable;
                            continue;
                        },
                        't' => {
                            buf.append(12) catch unreachable;
                            continue;
                        },
                        'r' => {
                            buf.append(13) catch unreachable;
                            continue;
                        },

                        // legacy octal literals
                        '0'..='7' => {
                            const octal_start = (iter.i + width2) - 2;

                            // 1-3 digit octal
                            let is_bad = false;
                            let value: i64 = @as(i64, c2 - '0');
                            let restore = iter;

                            if (!iterator.next(&iter)) {
                                if (value == 0) {
                                    buf.append(0)?;
                                    return;
                                }

                                _.syntaxError()?;
                                return;
                            }

                            const c3 = iter.c;

                            switch (c3) {
                                '0'..='7' => {
                                    value = value * 8 + @as(i64, c3 - '0');
                                    restore = iter;
                                    if (!iterator.next(&iter)) return _.syntaxError();

                                    const c4 = iter.c;
                                    switch (c4) {
                                        '0'..='7' => {
                                            const temp = value * 8 + @as(i64, c4 - '0');
                                            if (temp < 256) {
                                                value = temp;
                                            } else {
                                                iter = restore;
                                            }
                                        },
                                        '8', '9' => {
                                            is_bad = true;
                                        },
                                        else => {
                                            iter = restore;
                                        },
                                    }
                                },
                                '8', '9' => {
                                    is_bad = true;
                                },
                                else => {
                                    iter = restore;
                                },
                            }

                            iter.c = @as(i32, @intCast(value));
                            if (is_bad) {
                                _.addRangeError(
                                    logger.Range{ .loc = .{ .start = @as(i32, @intCast(octal_start)) }, .len = @as(i32, @intCast(iter.i - octal_start)) },
                                    "Invalid legacy octal literal",
                                    .{},
                                ) catch unreachable;
                            }
                        },
                        '8', '9' => {
                            iter.c = c2;
                        },
                        // 2-digit hexadecimal
                        'x' => {
                            if (comptime allow_multiline) {
                                _.end = start + iter.i - width2;
                                _.syntaxError()?;
                            }

                            let value: i32 = 0;
                            let c3: i32 = 0;
                            let width3 = iter.width;

                            if (!iterator.next(&iter)) return _.syntaxError();
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
                                    _.end = start + iter.i - width3;
                                    return _.syntaxError();
                                },
                            }

                            if (!iterator.next(&iter)) return _.syntaxError();
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
                                    _.end = start + iter.i - width3;
                                    return _.syntaxError();
                                },
                            }

                            iter.c = value;
                        },
                        'u' => {
                            // We're going to make _ an i64 so we don't risk integer overflows
                            // when people do weird things
                            let value: i64 = 0;

                            if (!iterator.next(&iter)) return _.syntaxError();
                            let c3 = iter.c;
                            let width3 = iter.width;

                            // variable-length
                            if (c3 == '{') {
                                const hex_start = iter.i - width - width2 - width3;
                                let is_first = true;
                                let is_out_of_range = false;
                                variableLength: while (true) {
                                    if (!iterator.next(&iter)) return _.syntaxError();
                                    c3 = iter.c;

                                    switch (c3) {
                                        '0'..='9' => {
                                            value = (value *| 16) | @as(i64, c3 - '0');
                                        },
                                        'a'..='f' => {
                                            value = (value *| 16) | @as(i64, c3 + 10 - 'a');
                                        },
                                        'A'..='F' => {
                                            value = (value *| 16) | @as(i64, c3 + 10 - 'A');
                                        },
                                        '}' => {
                                            if (is_first) {
                                                _.end = start + iter.i - width3;
                                                return _.syntaxError();
                                            }
                                            break :variableLength;
                                        },
                                        else => {
                                            _.end = start + iter.i - width3;
                                            return _.syntaxError();
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
                                    _.addRangeError(
                                        .{ .loc = .{ .start = @as(i32, @intCast(start + hex_start)) }, .len = @as(i32, @intCast((iter.i - hex_start))) },
                                        "Unicode escape sequence is out of range",
                                        .{},
                                    )?;
                                    return;
                                }

                                // fixed-length
                            } else {
                                let j: usize = 0;
                                while (j < 4) : (j += 1) {
                                    switch (c3) {
                                        '0'..='9' => {
                                            value = value * 16 | @as(i64, c3 - '0');
                                        },
                                        'a'..='f' => {
                                            value = value * 16 | @as(i64, c3 + 10 - 'a');
                                        },
                                        'A'..='F' => {
                                            value = value * 16 | @as(i64, c3 + 10 - 'A');
                                        },
                                        else => {
                                            _.end = start + iter.i - width3;
                                            return _.syntaxError();
                                        },
                                    }

                                    if (j < 3) {
                                        if (!iterator.next(&iter)) return _.syntaxError();
                                        c3 = iter.c;
                                        width3 = iter.width;
                                    }
                                }
                            }

                            if ((value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
                                _.addDefaultError("Escaped code point must be a Unicode scalar value")?;
                            }
                            iter.c = @as(CodePoint, @truncate(value));
                        },
                        'U' => {
                            let value: i64 = 0;
                            let j: usize = 0;
                            while (j < 8) : (j += 1) {
                                if (!iterator.next(&iter)) return _.syntaxError();
                                const c3 = iter.c;
                                const width3 = iter.width;
                                switch (c3) {
                                    '0'..='9' => {
                                        value = value * 16 | @as(i64, c3 - '0');
                                    },
                                    'a'..='f' => {
                                        value = value * 16 | @as(i64, c3 + 10 - 'a');
                                    },
                                    'A'..='F' => {
                                        value = value * 16 | @as(i64, c3 + 10 - 'A');
                                    },
                                    else => {
                                        _.end = start + iter.i - width3;
                                        return _.syntaxError();
                                    },
                                }
                            }
                            if ((value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
                                _.addDefaultError("Escaped code point must be a Unicode scalar value")?;
                            }
                            iter.c = @as(CodePoint, @truncate(value));
                        },
                        '\r' => {
                            if (comptime !allow_multiline) {
                                _.end = start + iter.i - width2;
                                _.addDefaultError("Unexpected end of line")?;
                            }

                            // Ignore line continuations. A line continuation is not an escaped newline.
                            if (iter.i < text.len && text[iter.i + 1] == '\n') {
                                // Make sure Windows CRLF counts as a single newline
                                iter.i += 1;
                            }
                            continue;
                        },
                        '\n', 0x2028, 0x2029 => {
                            // Ignore line continuations. A line continuation is not an escaped newline.
                            if (comptime !allow_multiline) {
                                _.end = start + iter.i - width2;
                                _.addDefaultError("Unexpected end of line")?;
                            }
                            continue;
                        },
                        else => {
                            iter.c = c2;
                        },
                    }
                },
                else => {},
            }

            switch (iter.c) {
                -1 => return _.addDefaultError("Unexpected end of file")?,
                0..=127 => {
                    buf.append(@as(u8, @intCast(iter.c))) catch unreachable;
                },
                else => {
                    let part: [4]u8 = undefined;
                    const len = strings.encodeWTF8Rune(&part, iter.c);
                    buf.appendSlice(part[0..len])?;
                },
            }
        }
    }

    pub fn expected(*_, token: TokenType) !void {
        _.expectedString(@as(string, @tagName(token)))?;
    }

    pub fn unexpected(*_) !void {
        const found = finder: {
            _.start = @min(_.start, _.end);

            if (_.start == _.source.contents.len) {
                break :finder "end of file";
            } else {
                break :finder _.raw();
            }
        };

        _.addRangeError(_.range(), "Unexpected {s}", .{found})?;
    }

    pub fn expectedString(*_, text: string) !void {
        const found = finder: {
            if (_.source.contents.len != _.start) {
                break :finder _.raw();
            } else {
                break :finder "end of file";
            }
        };

        _.addRangeError(_.range(), "Expected {s} but found {s}", .{ text, found })?;
    }

    pub fn range() logger.Range {
        return logger.Range{
            .loc = logger.usize2Loc(_.start),
            .len = std.math.lossyCast(i32, _.end - _.start),
        };
    }

    pub fn init(log: *logger.Log, source: logger.Source, allocator: std.mem.Allocator, redact_logs: bool): Lexer! {
        let lex = Lexer{
            .log = log,
            .source = source,
            .prev_error_loc = logger.Loc.Empty,
            .allocator = allocator,
            .should_redact_logs = redact_logs,
        };
        lex.step();
        lex.next()?;

        return lex;
    }

    pub inline fn toString(*_, loc_: logger.Loc) js_ast.Expr {
        if (_.string_literal_is_ascii) {
            return js_ast.Expr.init(js_ast.E.String, js_ast.E.String{ .data = _.string_literal_slice }, loc_);
        }

        return js_ast.Expr.init(
            js_ast.E.String,
            .{ .data = _.string_literal_slice },
            loc_,
        );
    }

    pub fn raw() []const u8 {
        return _.source.contents[_.start.._.end];
    }
}

pub fn isIdentifierPart(code_point: CodePoint) bool {
    return switch (code_point) {
        '0'..='9',
        'a'..='z',
        'A'..='Z',
        '$',
        '_',
        '-',
        ':' => true,
        else => false,
    };
}

pub fn isLatin1Identifier(comptime Buffer: type, name: Buffer) bool {
    if (name.len == 0) return false;

    switch (name[0]) {
        'a'..='z',
        'A'..='Z',
        '$',
        '1'..='9',
        '_',
        '-' => {},
        else => return false,
    }

    if (name.len > 0) {
        for (c of name[1..]) {
            switch (c) {
                '0'..='9',
                'a'..='z',
                'A'..='Z',
                '$',
                '_',
                '-' => {},
                else => return false,
            }
        }
    }

    return true;
}

inline fn float64(num: anytype): f64 {
    const f: f64 = @floatFromInt(num);
    return f;
}

import bun from "bun";
const CodePoint = bun.CodePoint;
const js_ast = bun.ast;
const logger = bun.logger;
const strings = bun.strings;
