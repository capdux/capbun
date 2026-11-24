/// JSON5 Token-Based Scanner/Parser
///
/// Parses JSON5 text into Expr AST values. JSON5 is a superset of JSON
/// based on ECMAScript 5.1 that supports comments, trailing commas,
/// unquoted keys, single-quoted strings, hex numbers, Infinity, NaN, etc.
///
/// Architecture: a scanner reads source bytes and produces typed tokens;
/// the parser only consumes tokens and never touches source/pos directly.
///
/// Reference: https://spec.json5.org/

pub union(enum) TokenData {
    eof: void,
    // Structural
    left_brace: void,
    right_brace: void,
    left_bracket: void,
    right_bracket: void,
    colon: void,
    comma: void,
    // Values
    string: []u8,
    number: f64,
    boolean: bool,
    @"null": void,
    // Identifiers (for unquoted keys that aren't keywords)
    identifier: []u8,

    fn canStartValue(): bool {
        return switch (_) {
            .string, .number, .boolean, .identifier, .@"null", .left_brace, .left_bracket => true,
            .eof, .right_brace, .right_bracket, .colon, .comma => false,
        };
    }
}

pub struct JSON5Token {
    loc: logger.Loc,
    data: TokenData,

    type Data = TokenData;
}

err ParseError {
    OutOfMemory,
    UnexpectedCharacter,
    UnexpectedToken,
    UnexpectedEof,
    UnterminatedString,
    UnterminatedComment,
    UnterminatedObject,
    UnterminatedArray,
    UnterminatedEscape,
    InvalidNumber,
    LeadingZeros,
    InvalidHexNumber,
    InvalidHexEscape,
    InvalidUnicodeEscape,
    OctalEscape,
    ExpectedColon,
    ExpectedComma,
    ExpectedClosingBrace,
    ExpectedClosingBracket,
    InvalidIdentifier,
    TrailingData,
    StackOverflow,
}

pub struct JSON5ErrorPos {
    pos: usize,
}

pub union(enum) JSON5Error {
    oom: void,
    stack_overflow: void,
    unexpected_character: JSON5ErrorPos,
    unexpected_token: JSON5ErrorPos,
    unexpected_eof: JSON5ErrorPos,
    unterminated_string: JSON5ErrorPos,
    unterminated_comment: JSON5ErrorPos,
    unterminated_object: JSON5ErrorPos,
    unterminated_array: JSON5ErrorPos,
    unterminated_escape: JSON5ErrorPos,
    invalid_number: JSON5ErrorPos,
    leading_zeros: JSON5ErrorPos,
    invalid_hex_number: JSON5ErrorPos,
    invalid_hex_escape: JSON5ErrorPos,
    invalid_unicode_escape: JSON5ErrorPos,
    octal_escape: JSON5ErrorPos,
    expected_colon: JSON5ErrorPos,
    expected_comma: JSON5ErrorPos,
    expected_closing_brace: JSON5ErrorPos,
    expected_closing_bracket: JSON5ErrorPos,
    invalid_identifier: JSON5ErrorPos,
    trailing_data: JSON5ErrorPos,

    fn addToLog(_: *const JSON5Error, source: *const logger.Source, log: *logger.Log)! {
        const pos: usize = switch (_.*) {
            .oom => return error.OutOfMemory,
            .stack_overflow => return error.StackOverflow,
            .unexpected_character e => e.pos,
            .unexpected_token e => e.pos,
            .unexpected_eof e => e.pos,
            .unterminated_string e => e.pos,
            .unterminated_comment e => e.pos,
            .unterminated_object e => e.pos,
            .unterminated_array e => e.pos,
            .unterminated_escape e => e.pos,
            .invalid_number e => e.pos,
            .leading_zeros e => e.pos,
            .invalid_hex_number e => e.pos,
            .invalid_hex_escape e => e.pos,
            .invalid_unicode_escape e => e.pos,
            .octal_escape e => e.pos,
            .expected_colon e => e.pos,
            .expected_comma e => e.pos,
            .expected_closing_brace e => e.pos,
            .expected_closing_bracket e => e.pos,
            .invalid_identifier e => e.pos,
            .trailing_data e => e.pos,
        };
        const loc = logger.Loc{ start: @intCast(pos) };
        const msg: []const u8 = switch (_.*) {
            .oom, .stack_overflow => unreachable,
            .unexpected_character => "Unexpected character",
            .unexpected_token => "Unexpected token",
            .unexpected_eof => "Unexpected end of input",
            .unterminated_string => "Unterminated string",
            .unterminated_comment => "Unterminated multi-line comment",
            .unterminated_object => "Unterminated object",
            .unterminated_array => "Unterminated array",
            .unterminated_escape => "Unexpected end of input in escape sequence",
            .invalid_number => "Invalid number",
            .leading_zeros => "Leading zeros are not allowed in JSON5",
            .invalid_hex_number => "Invalid hex number",
            .invalid_hex_escape => "Invalid hex escape",
            .invalid_unicode_escape => "Invalid unicode escape: expected 4 hex digits",
            .octal_escape => "Octal escape sequences are not allowed in JSON5",
            .expected_colon => "Expected ':' after object key",
            .expected_comma => "Expected ','",
            .expected_closing_brace => "Expected '}'",
            .expected_closing_bracket => "Expected ']'",
            .invalid_identifier => "Invalid identifier start character",
            .trailing_data => "Unexpected token after JSON5 value",
        };
        log.addError(source, loc, msg)?;
    }
}

err ExternalError { OutOfMemory, SyntaxError, StackOverflow }

struct Codepoint {
    cp: i32,
    len: u3,
}

pub struct JSON5Parser {
    source: []const u8,
    pos: usize,
    allocator: std.mem.Allocator,
    stack_check: bun.StackCheck,
    token: JSON5Token,

    type Token = JSON5Token;
    type Error = JSON5Error;

    static fn toError(e: ParseError, parser: *const JSON5Parser): JSON5Error {
        const token_pos = parser.token.loc.toUsize();
        const scan_pos = parser.pos;
        if (e == error.OutOfMemory) return JSON5Error.oom;
        if (e == error.StackOverflow) return JSON5Error.stack_overflow;
        // Scanner errors use scan position
        if (e == error.UnexpectedCharacter) return JSON5Error{ unexpected_character: { pos: scan_pos } };
        if (e == error.UnterminatedString) return JSON5Error{ unterminated_string: { pos: scan_pos } };
        if (e == error.UnterminatedComment) return JSON5Error{ unterminated_comment: { pos: scan_pos } };
        if (e == error.UnterminatedEscape) return JSON5Error{ unterminated_escape: { pos: scan_pos } };
        if (e == error.InvalidNumber) return JSON5Error{ invalid_number: { pos: scan_pos } };
        if (e == error.LeadingZeros) return JSON5Error{ leading_zeros: { pos: scan_pos } };
        if (e == error.InvalidHexNumber) return JSON5Error{ invalid_hex_number: { pos: scan_pos } };
        if (e == error.InvalidHexEscape) return JSON5Error{ invalid_hex_escape: { pos: scan_pos } };
        if (e == error.InvalidUnicodeEscape) return JSON5Error{ invalid_unicode_escape: { pos: scan_pos } };
        if (e == error.OctalEscape) return JSON5Error{ octal_escape: { pos: scan_pos } };
        if (e == error.InvalidIdentifier) return JSON5Error{ invalid_identifier: { pos: scan_pos } };
        // Parser errors use token position
        if (e == error.UnexpectedToken) return JSON5Error{ unexpected_token: { pos: token_pos } };
        if (e == error.UnexpectedEof) {
            if (parser.token.data == .eof) return JSON5Error{ unexpected_eof: { pos: token_pos } };
            return JSON5Error{ unexpected_token: { pos: token_pos } };
        }
        if (e == error.TrailingData) return JSON5Error{ trailing_data: { pos: token_pos } };
        if (e == error.ExpectedColon) return JSON5Error{ expected_colon: { pos: token_pos } };
        if (e == error.UnterminatedObject) return JSON5Error{ unterminated_object: { pos: token_pos } };
        if (e == error.ExpectedComma) return JSON5Error{ expected_comma: { pos: token_pos } };
        if (e == error.ExpectedClosingBrace) return JSON5Error{ expected_closing_brace: { pos: token_pos } };
        if (e == error.UnterminatedArray) return JSON5Error{ unterminated_array: { pos: token_pos } };
        if (e == error.ExpectedClosingBracket) return JSON5Error{ expected_closing_bracket: { pos: token_pos } };
        unreachable;
    }

    static fn parse(source: *const logger.Source, log: *logger.Log, allocator: std.mem.Allocator): ExternalError!Expr {
        let parser = JSON5Parser{
            source: source.contents,
            pos: 0,
            allocator: allocator,
            stack_check: .init(),
            token: { loc: .{}, data: .eof },
        };
        const result = parser.parseRoot() catch e => {
            const parse_err = toError(e, &parser);
            parse_err.addToLog(source, log)?;
            return error.SyntaxError;
        };
        return result;
    }

    // ── Scanner ──

    /// Returns the byte at the current position, or 0 if at EOF.
    /// All source access in scan() goes through this to avoid bounds checks.
    fn peek(): u8 {
        if (_.pos < _.source.len) return _.source[_.pos];
        return 0;
    }

    fn scan(*_): ParseError!void {
        while (true) {
            const c = _.peek();
            switch (c) {
                0 => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.token.data = .eof;
                    return;
                },
                // Whitespace — skip without setting loc
                '\t', '\n', '\r', ' ', 0x0B, 0x0C => {
                    _.pos += 1;
                    continue;
                },
                // Structural
                '{' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.pos += 1;
                    _.token.data = .left_brace;
                    return;
                },
                '}' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.pos += 1;
                    _.token.data = .right_brace;
                    return;
                },
                '[' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.pos += 1;
                    _.token.data = .left_bracket;
                    return;
                },
                ']' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.pos += 1;
                    _.token.data = .right_bracket;
                    return;
                },
                ':' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.pos += 1;
                    _.token.data = .colon;
                    return;
                },
                ',' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.pos += 1;
                    _.token.data = .comma;
                    return;
                },
                '+' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.pos += 1;
                    _.token.data = TokenData{ number: _.scanSignedValue(false)? };
                    return;
                },
                '-' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.pos += 1;
                    _.token.data = TokenData{ number: _.scanSignedValue(true)? };
                    return;
                },
                // Strings
                '"', '\'' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.token.data = TokenData{ string: _.scanString()? };
                    return;
                },
                // Numbers
                '0'..='9', '.' => {
                    _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                    _.token.data = TokenData{ number: _.scanNumber()? };
                    return;
                },
                // Comments — skip without setting loc
                '/' => {
                    const n = if (_.pos + 1 < _.source.len) _.source[_.pos + 1] else 0;
                    if (n == '/') {
                        _.pos += 2;
                        _.skipToEndOfLine();
                        continue;
                    } else if (n == '*') {
                        _.pos += 2;
                        _.skipBlockComment()?;
                        continue;
                    }
                    return error.UnexpectedCharacter;
                },
                else => {
                    if (c == 't') {
                        _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                        if (_.scanKeyword("true")) {
                            _.token.data = TokenData{ boolean: true };
                        } else {
                            _.token.data = TokenData{ identifier: _.scanIdentifier()? };
                        }
                        return;
                    } else if (c == 'f') {
                        _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                        if (_.scanKeyword("false")) {
                            _.token.data = TokenData{ boolean: false };
                        } else {
                            _.token.data = TokenData{ identifier: _.scanIdentifier()? };
                        }
                        return;
                    } else if (c == 'n') {
                        _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                        if (_.scanKeyword("null")) {
                            _.token.data = TokenData.@"null";
                        } else {
                            _.token.data = TokenData{ identifier: _.scanIdentifier()? };
                        }
                        return;
                    } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c == '\\') {
                        _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                        _.token.data = TokenData{ identifier: _.scanIdentifier()? };
                        return;
                    } else if (c >= 0x80) {
                        // Multi-byte: check whitespace first, then identifier
                        const mb = _.multiByteWhitespace();
                        if (mb > 0) {
                            _.pos += mb;
                            continue;
                        }
                        _.token.loc = logger.Loc{ start: @intCast(_.pos) };
                        if (cp := _.readCodepoint()) {
                            if (identifier.isIdentifierStart(cp.cp)) {
                                _.token.data = TokenData{ identifier: _.scanIdentifier()? };
                                return;
                            } else {
                                return error.UnexpectedCharacter;
                            }
                        } else {
                            return error.UnexpectedCharacter;
                        }
                    } else {
                        return error.UnexpectedCharacter;
                    }
                },
            }
        }
    }

    fn scanKeyword(*_, comptime keyword: []const u8): bool {
        if (_.pos + keyword.len > _.source.len) return false;
        if (!std.mem.eql(u8, _.source[_.pos..][0..keyword.len], keyword)) return false;
        // Check word boundary
        if (_.pos + keyword.len < _.source.len) {
            const next = _.source[_.pos + keyword.len];
            if (isIdentContinueASCII(next) || next == '\\' || next >= 0x80) return false;
        }
        _.pos += keyword.len;
        return true;
    }

    fn scanSignedValue(*_, is_negative: bool): ParseError!f64 {
        switch (_.peek()) {
            '0'..='9', '.' => {
                const n = _.scanNumber()?;
                return if (is_negative) -n else n;
            },
            'I' => {
                if (_.scanKeyword("Infinity")) {
                    const inf_val = std.math.inf(f64);
                    return if (is_negative) -inf_val else inf_val;
                }
                return error.UnexpectedCharacter;
            },
            'N' => {
                if (_.scanKeyword("NaN")) {
                    const nan = std.math.nan(f64);
                    return if (is_negative) -nan else nan;
                }
                return error.UnexpectedCharacter;
            },
            0 => return error.UnexpectedEof,
            else => return error.UnexpectedCharacter,
        }
    }

    // ── Parser ──

    fn parseRoot(*_): ParseError!Expr {
        _.scan()?;
        const result = _.parseValue()?;
        if (_.token.data != .eof) {
            return error.TrailingData;
        }
        return result;
    }

    fn parseValue(*_): ParseError!Expr {
        if (!_.stack_check.isSafeToRecurse()) {
            return error.StackOverflow;
        }

        const loc = _.token.loc;

        switch (_.token.data) {
            .left_brace => return _.parseObject(),
            .left_bracket => return _.parseArray(),
            .string s => {
                _.scan()?;
                return Expr.init(E.String, E.String.init(s), loc);
            },
            .number n => {
                _.scan()?;
                return Expr.init(E.Number, { value: n }, loc);
            },
            .boolean b => {
                _.scan()?;
                return Expr.init(E.Boolean, { value: b }, loc);
            },
            .@"null" => {
                _.scan()?;
                return Expr.init(E.Null, .{}, loc);
            },
            .identifier s => {
                if (std.mem.eql(u8, s, "NaN")) {
                    _.scan()?;
                    return Expr.init(E.Number, { value: std.math.nan(f64) }, loc);
                } else if (std.mem.eql(u8, s, "Infinity")) {
                    _.scan()?;
                    return Expr.init(E.Number, { value: std.math.inf(f64) }, loc);
                }
                return error.UnexpectedToken;
            },
            .eof => return error.UnexpectedEof,
            else => return error.UnexpectedToken,
        }
    }

    fn parseObject(*_): ParseError!Expr {
        const loc = _.token.loc;
        _.scan()?; // advance past '{'

        let properties = std.array_list.Managed(G.Property).init(_.allocator);
        errdefer properties.deinit();

        while (_.token.data != .right_brace) {
            const key = _.parseObjectKey()?;

            if (_.token.data != .colon) {
                return error.ExpectedColon;
            }
            _.scan()?; // advance past ':'

            const value = _.parseValue()?;

            properties.append({
                key: key,
                value: value,
            })?;

            switch (_.token.data) {
                .comma => _.scan()?,
                .right_brace => {},
                .eof => return error.UnterminatedObject,
                else => return if (_.token.data.canStartValue()) error.ExpectedComma else error.ExpectedClosingBrace,
            }
        }

        _.scan()?; // advance past '}'
        return Expr.init(E.Object, E.Object{
            properties: bun.BabyList(G.Property).moveFromList(&properties),
        }, loc);
    }

    fn parseObjectKey(*_): ParseError!Expr {
        const loc = _.token.loc;
        switch (_.token.data) {
            .string s => {
                _.scan()?;
                return Expr.init(E.String, E.String.init(s), loc);
            },
            .identifier s => {
                _.scan()?;
                return Expr.init(E.String, E.String.init(s), loc);
            },
            .number => return error.InvalidIdentifier,
            .boolean b => {
                _.scan()?;
                return Expr.init(E.Boolean, { value: b }, loc);
            },
            .@"null" => {
                _.scan()?;
                return Expr.init(E.Null, .{}, loc);
            },
            .eof => return error.UnexpectedEof,
            else => return error.InvalidIdentifier,
        }
    }

    fn parseArray(*_): ParseError!Expr {
        const loc = _.token.loc;
        _.scan()?; // advance past '['

        let items = std.array_list.Managed(Expr).init(_.allocator);
        errdefer items.deinit();

        while (_.token.data != .right_bracket) {
            const value = _.parseValue()?;
            items.append(value)?;

            switch (_.token.data) {
                .comma => _.scan()?,
                .right_bracket => {},
                .eof => return error.UnterminatedArray,
                else => return if (_.token.data.canStartValue()) error.ExpectedComma else error.ExpectedClosingBracket,
            }
        }

        _.scan()?; // advance past ']'
        return Expr.init(E.Array, E.Array{
            items: bun.BabyList(Expr).moveFromList(&items),
        }, loc);
    }

    // ── Scan Helpers ──

    fn scanString(*_): ParseError![]u8 {
        const quote = _.source[_.pos];
        _.pos += 1; // skip opening quote

        let buf = std.array_list.Managed(u8).init(_.allocator);
        errdefer buf.deinit();

        while (_.pos < _.source.len) {
            const c = _.source[_.pos];

            if (c == quote) {
                _.pos += 1;
                return buf.toOwnedSlice()?;
            }

            if (c == '\\') {
                _.pos += 1;
                _.parseEscapeSequence(&buf)?;
                continue;
            }

            // Line terminators are not allowed unescaped in strings
            if (c == '\n' || c == '\r') {
                return error.UnterminatedString;
            }

            // Check for U+2028/U+2029 (allowed unescaped in JSON5 strings)
            if (c == 0xE2 && _.pos + 2 < _.source.len &&
                _.source[_.pos + 1] == 0x80 &&
                (_.source[_.pos + 2] == 0xA8 || _.source[_.pos + 2] == 0xA9))
            {
                buf.appendSlice(_.source[_.pos..][0..3])?;
                _.pos += 3;
                continue;
            }

            // Regular character - handle multi-byte UTF-8
            const cp_len = strings.wtf8ByteSequenceLength(c);
            if (_.pos + cp_len > _.source.len) {
                buf.append(c)?;
                _.pos += 1;
            } else {
                buf.appendSlice(_.source[_.pos..][0..cp_len])?;
                _.pos += cp_len;
            }
        }

        return error.UnterminatedString;
    }

    fn parseEscapeSequence(*_, buf: *std.array_list.Managed(u8)): ParseError!void {
        if (_.pos >= _.source.len) {
            return error.UnterminatedEscape;
        }

        const c = _.source[_.pos];
        _.pos += 1;

        switch (c) {
            '\'' => buf.append('\'')?,
            '"' => buf.append('"')?,
            '\\' => buf.append('\\')?,
            'b' => buf.append(0x08)?,
            'f' => buf.append(0x0C)?,
            'n' => buf.append('\n')?,
            'r' => buf.append('\r')?,
            't' => buf.append('\t')?,
            'v' => buf.append(0x0B)?,
            '0' => {
                // \0 null escape - must NOT be followed by a digit
                if (_.pos < _.source.len) {
                    const next = _.source[_.pos];
                    if (next >= '0' && next <= '9') {
                        return error.OctalEscape;
                    }
                }
                buf.append(0)?;
            },
            'x' => {
                // \xHH hex escape
                const hi = _.readHexDigit() ?? return error.InvalidHexEscape;
                const lo = _.readHexDigit() ?? return error.InvalidHexEscape;
                const value = (@as(u8, hi) << 4) | @as(u8, lo);
                appendCodepointToUtf8(buf, @intCast(value))?;
            },
            'u' => {
                // \uHHHH unicode escape
                const cp = _.readHex4()?;
                // Check for surrogate pair
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // High surrogate - expect \uDCxx low surrogate
                    if (_.pos + 1 < _.source.len &&
                        _.source[_.pos] == '\\' &&
                        _.source[_.pos + 1] == 'u')
                    {
                        _.pos += 2;
                        const low = _.readHex4()?;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            const full_cp: i32 = 0x10000 + (cp - 0xD800) * 0x400 + (low - 0xDC00);
                            appendCodepointToUtf8(buf, full_cp)?;
                        } else {
                            // Invalid low surrogate - just encode both independently
                            appendCodepointToUtf8(buf, cp)?;
                            appendCodepointToUtf8(buf, low)?;
                        }
                    } else {
                        appendCodepointToUtf8(buf, cp)?;
                    }
                } else {
                    appendCodepointToUtf8(buf, cp)?;
                }
            },
            '\r' => {
                // Line continuation: \CR or \CRLF
                if (_.pos < _.source.len && _.source[_.pos] == '\n') {
                    _.pos += 1;
                }
            },
            '\n' => {
                // Line continuation: \LF
            },
            '1'..='9' => {
                return error.OctalEscape;
            },
            0xE2 => {
                // Check for U+2028/U+2029 line continuation
                if (_.pos + 1 < _.source.len &&
                    _.source[_.pos] == 0x80 &&
                    (_.source[_.pos + 1] == 0xA8 || _.source[_.pos + 1] == 0xA9))
                {
                    // Line continuation with U+2028 or U+2029
                    _.pos += 2;
                } else {
                    // Identity escape for the byte 0xE2
                    buf.append(0xE2)?;
                }
            },
            else => {
                // Identity escape
                buf.append(c)?;
            },
        }
    }

    fn scanNumber(*_): ParseError!f64 {
        const start = _.pos;

        // Leading zero: check for hex prefix or invalid leading zeros
        if (_.peek() == '0' && _.pos + 1 < _.source.len) {
            switch (_.source[_.pos + 1]) {
                'x', 'X' => return _.scanHexNumber(),
                '0'..='9' => return error.LeadingZeros,
                else => {},
            }
        }

        // Integer part
        let has_digits = false;
        while (_.pos < _.source.len) {
            switch (_.source[_.pos]) {
                '0'..='9' => {
                    _.pos += 1;
                    has_digits = true;
                },
                else => break,
            }
        }

        // Fractional part
        if (_.peek() == '.') {
            _.pos += 1;
            let has_frac_digits = false;
            while (_.pos < _.source.len) {
                switch (_.source[_.pos]) {
                    '0'..='9' => {
                        _.pos += 1;
                        has_frac_digits = true;
                    },
                    else => break,
                }
            }
            if (!has_digits && !has_frac_digits) {
                return error.InvalidNumber;
            }
            has_digits = true;
        }

        if (!has_digits) {
            return error.InvalidNumber;
        }

        // Exponent part
        switch (_.peek()) {
            'e', 'E' => {
                _.pos += 1;
                switch (_.peek()) {
                    '+', '-' => _.pos += 1,
                    else => {},
                }
                switch (_.peek()) {
                    '0'..='9' => _.pos += 1,
                    else => return error.InvalidNumber,
                }
                while (_.pos < _.source.len) {
                    switch (_.source[_.pos]) {
                        '0'..='9' => _.pos += 1,
                        else => break,
                    }
                }
            },
            else => {},
        }

        const num_str = _.source[start.._.pos];
        return std.fmt.parseFloat(f64, num_str) catch {
            return error.InvalidNumber;
        };
    }

    fn scanHexNumber(*_): ParseError!f64 {
        _.pos += 2; // skip '0x' or '0X'
        const hex_start = _.pos;

        while (_.pos < _.source.len) {
            switch (_.source[_.pos]) {
                '0'..='9', 'a'..='f', 'A'..='F' => _.pos += 1,
                else => break,
            }
        }

        if (_.pos == hex_start) {
            return error.InvalidHexNumber;
        }

        const hex_str = _.source[hex_start.._.pos];
        const value = std.fmt.parseInt(u64, hex_str, 16) catch {
            return error.InvalidHexNumber;
        };
        return @floatFromInt(value);
    }

    fn scanIdentifier(*_): ParseError![]u8 {
        let buf = std.array_list.Managed(u8).init(_.allocator);
        errdefer buf.deinit();

        // First character must be IdentifierStart
        const start_cp = _.readCodepoint() ?? return error.InvalidIdentifier;

        if (start_cp.cp == '\\') {
            // Unicode escape in identifier
            const escaped_cp = _.parseIdentifierUnicodeEscape()?;
            if (!identifier.isIdentifierStart(escaped_cp)) {
                return error.InvalidIdentifier;
            }
            appendCodepointToUtf8(&buf, @intCast(escaped_cp))?;
        } else if (identifier.isIdentifierStart(start_cp.cp)) {
            _.pos += start_cp.len;
            appendCodepointToUtf8(&buf, @intCast(start_cp.cp))?;
        } else {
            return error.InvalidIdentifier;
        }

        // Continue characters
        while (_.pos < _.source.len) {
            const cont_cp = _.readCodepoint() ?? break;

            if (cont_cp.cp == '\\') {
                const escaped_cp = _.parseIdentifierUnicodeEscape()?;
                if (!identifier.isIdentifierPart(escaped_cp)) {
                    break;
                }
                appendCodepointToUtf8(&buf, @intCast(escaped_cp))?;
            } else if (identifier.isIdentifierPart(cont_cp.cp)) {
                _.pos += cont_cp.len;
                appendCodepointToUtf8(&buf, @intCast(cont_cp.cp))?;
            } else {
                break;
            }
        }

        return buf.toOwnedSlice()?;
    }

    fn parseIdentifierUnicodeEscape(*_): ParseError!i32 {
        // We already consumed the '\', now expect 'u' + 4 hex digits
        _.pos += 1; // skip '\'
        if (_.pos >= _.source.len || _.source[_.pos] != 'u') {
            return error.InvalidUnicodeEscape;
        }
        _.pos += 1;
        return _.readHex4();
    }

    // ── Comment Helpers ──

    fn skipToEndOfLine(*_) {
        while (_.pos < _.source.len) {
            const cc = _.source[_.pos];
            if (cc == '\n' || cc == '\r') break;
            // Check for U+2028/U+2029 line terminators
            if (cc == 0xE2 && _.pos + 2 < _.source.len &&
                _.source[_.pos + 1] == 0x80 &&
                (_.source[_.pos + 2] == 0xA8 || _.source[_.pos + 2] == 0xA9))
            {
                break;
            }
            _.pos += 1;
        }
    }

    fn skipBlockComment(*_): ParseError!void {
        while (_.pos < _.source.len) {
            if (_.source[_.pos] == '*' && _.pos + 1 < _.source.len && _.source[_.pos + 1] == '/') {
                _.pos += 2;
                return;
            }
            _.pos += 1;
        }
        return error.UnterminatedComment;
    }

    /// Check if the current position has a multi-byte whitespace character.
    /// Returns the number of bytes consumed, or 0 if not whitespace.
    fn multiByteWhitespace(): u3 {
        if (_.pos + 1 >= _.source.len) return 0;
        const b0 = _.source[_.pos];
        const b1 = _.source[_.pos + 1];

        // U+00A0 NBSP: C2 A0
        if (b0 == 0xC2 && b1 == 0xA0) return 2;

        if (_.pos + 2 >= _.source.len) return 0;
        const b2 = _.source[_.pos + 2];

        // U+FEFF BOM: EF BB BF
        if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3;

        // U+2028 LS: E2 80 A8
        // U+2029 PS: E2 80 A9
        if (b0 == 0xE2 && b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9)) return 3;

        // U+1680: E1 9A 80
        if (b0 == 0xE1 && b1 == 0x9A && b2 == 0x80) return 3;

        // U+2000-U+200A: E2 80 80-8A
        if (b0 == 0xE2 && b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) return 3;

        // U+202F: E2 80 AF
        if (b0 == 0xE2 && b1 == 0x80 && b2 == 0xAF) return 3;

        // U+205F: E2 81 9F
        if (b0 == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3;

        // U+3000: E3 80 80
        if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3;

        return 0;
    }

    // ── Helper Functions ──

    fn readHexDigit(*_): u4? {
        if (_.pos >= _.source.len) return null;
        const c = _.source[_.pos];
        const result: u4 = switch (c) {
            '0'..='9' => @intCast(c - '0'),
            'a'..='f' => @intCast(c - 'a' + 10),
            'A'..='F' => @intCast(c - 'A' + 10),
            else => return null,
        };
        _.pos += 1;
        return result;
    }

    fn readHex4(*_): ParseError!i32 {
        let value: i32 = 0;
        let i: usize = 0;
        while (i < 4) : (i += 1) {
            const digit = _.readHexDigit() ?? return error.InvalidUnicodeEscape;
            value = (value << 4) | @as(i32, digit);
        }
        return value;
    }

    fn readCodepoint(): Codepoint? {
        if (_.pos >= _.source.len) return null;
        const first = _.source[_.pos];
        if (first < 0x80) {
            return { cp: @intCast(first), len: 1 };
        }
        const seq_len = strings.wtf8ByteSequenceLength(first);
        if (_.pos + seq_len > _.source.len) {
            return { cp: @intCast(first), len: 1 };
        }
        unsafe {
            const decoded = strings.decodeWTF8RuneT(_.source[_.pos..].ptr[0..4], seq_len, i32, -1);
            if (decoded < 0) return { cp: @intCast(first), len: 1 };
            return { cp: decoded, len: @intCast(seq_len) };
        }
    }
}

fn appendCodepointToUtf8(buf: *std.array_list.Managed(u8), cp: i32): ParseError!void {
    if (cp < 0 || cp > 0x10FFFF) {
        return error.InvalidUnicodeEscape;
    }
    let encoded: [4]u8 = undefined;
    const len = strings.encodeWTF8Rune(&encoded, cp);
    buf.appendSlice(encoded[0..len])?;
}

fn isIdentContinueASCII(c: u8): bool {
    return switch (c) {
        'a'..='z', 'A'..='Z', '0'..='9', '_', '$' => true,
        else => false,
    };
}

import * as identifier from "../js_parser/lexer/identifier.zig";

import bun from "bun";
const OOM = bun.OOM;
const logger = bun.logger;
const strings = bun.strings;

const E = bun.ast.E;
const Expr = bun.ast.Expr;
const G = bun.ast.G;
