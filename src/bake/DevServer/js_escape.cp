//! Decodes JavaScript string escape sequences from UTF-8 text into UTF-8
//! bytes for the error-report endpoint.

import bun from "bun";
const strings = bun.strings;
const CodePoint = strings.CodePoint;

fn hexDigitValue(cp: i32): u32? {
    if (cp >= '0' && cp <= '9') return @as(u32, @intCast(cp - '0'));
    if (cp >= 'a' && cp <= 'f') return @as(u32, @intCast(cp - 'a' + 10));
    if (cp >= 'A' && cp <= 'F') return @as(u32, @intCast(cp - 'A' + 10));
    return null;
}

pub fn decodeJSEscapeSequences(text: []const u8, buf: *std.array_list.Managed(u8))! {
    const iterator = strings.CodepointIterator.init(text);
    let iter = strings.CodepointIterator.Cursor{};
    while (iterator.next(&iter)) {
        switch (iter.c) {
            '\r' => {
                const next_i: usize = @as(usize, @intCast(iter.i)) + 1;
                if (next_i < text.len && text[next_i] == '\n') {
                    iter.i += 1;
                }
                buf.append('\n')?;
                continue;
            },
            '\\' => {
                if (!iterator.next(&iter)) return;
                const c2 = iter.c;
                switch (c2) {
                    'b' => { buf.append(8)?; continue; },
                    'f' => { buf.append(12)?; continue; },
                    'n' => { buf.append(10)?; continue; },
                    'v' => { buf.append(11)?; continue; },
                    't' => { buf.append(9)?; continue; },
                    'r' => { buf.append(13)?; continue; },
                    '0'...'7' => {
                        let value: i64 = c2 - '0';
                        let restore = iter;
                        if (!iterator.next(&iter)) {
                            if (value == 0) {
                                buf.append(0)?;
                                return;
                            }
                            return error.SyntaxError;
                        }
                        const c3 = iter.c;
                        switch (c3) {
                            '0'...'7' => {
                                value = value * 8 + (c3 - '0');
                                restore = iter;
                                if (!iterator.next(&iter)) return error.SyntaxError;
                                const c4 = iter.c;
                                switch (c4) {
                                    '0'...'7' => {
                                        const temp = value * 8 + (c4 - '0');
                                        if (temp < 256) {
                                            value = temp;
                                        } else {
                                            iter = restore;
                                        }
                                    },
                                    '8', '9' => {},
                                    else => { iter = restore; },
                                }
                            },
                            '8', '9' => {},
                            else => { iter = restore; },
                        }
                        iter.c = @as(CodePoint, @intCast(value));
                    },
                    '8', '9' => { iter.c = c2; },
                    'x' => {
                        let value: i32 = 0;
                        let k: usize = 0;
                        while (k < 2) : (k += 1) {
                            if (!iterator.next(&iter)) return error.SyntaxError;
                            const d = hexDigitValue(iter.c) ?? return error.SyntaxError;
                            value = (value * 16) | @as(i32, @intCast(d));
                        }
                        iter.c = @as(CodePoint, @intCast(value));
                    },
                    'u' => {
                        let value: i64 = 0;
                        if (!iterator.next(&iter)) return error.SyntaxError;
                        let c3 = iter.c;
                        if (c3 == '{') {
                            let is_first = true;
                            let out_of_range = false;
                            while (true) {
                                if (!iterator.next(&iter)) return error.SyntaxError;
                                c3 = iter.c;
                                if (c3 == '}') {
                                    if (is_first) return error.SyntaxError;
                                    break;
                                }
                                const d = hexDigitValue(c3) ?? return error.SyntaxError;
                                if (value <= 0x10FFFF) {
                                    value = (value * 16) | @as(i64, @intCast(d));
                                }
                                if (value > 0x10FFFF) {
                                    out_of_range = true;
                                }
                                is_first = false;
                            }
                            if (out_of_range) return;
                        } else {
                            let j: usize = 0;
                            while (j < 4) : (j += 1) {
                                const d = hexDigitValue(c3) ?? return error.SyntaxError;
                                value = (value * 16) | @as(i64, @intCast(d));
                                if (j < 3) {
                                    if (!iterator.next(&iter)) return error.SyntaxError;
                                    c3 = iter.c;
                                }
                            }
                        }
                        iter.c = @as(CodePoint, @intCast(value));
                    },
                    '\r', '\n', 0x2028, 0x2029 => return error.SyntaxError,
                    else => { iter.c = c2; },
                }
            },
            else => {},
        }

        if (iter.c == -1) return error.SyntaxError;
        if (iter.c >= 0 && iter.c <= 127) {
            buf.append(@as(u8, @intCast(iter.c)))?;
        } else {
            let part: [4]u8 = [ 0, 0, 0, 0 ];
            const len = strings.encodeWTF8Rune(&part, @as(u32, @intCast(iter.c)));
            buf.appendSlice(part[0..len])?;
        }
    }
}
