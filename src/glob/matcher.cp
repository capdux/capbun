import * as bun from "bun";

struct Brace {
    open_brace_idx: u32,
    branch_idx: u32,
}
const BraceStack = bun.BoundedArray(Brace, 10);

/// Upper bound on brace-branch alternatives explored per `match` call. Sequential
/// brace groups multiply (`{a,b}{c,d}` = 4 alternatives), so without a cap an
/// adversarial pattern of ten sequential 10-way groups would explore 10^10
/// alternatives. Patterns that exceed this budget fail to match.
const BRACE_BRANCH_BUDGET: u32 = 10_000;

enum MatchResult {
    no_match,
    match,

    negate_no_match,
    negate_match,

    fn matches(): bool {
        return _ == MatchResult.match || _ == MatchResult.negate_match;
    }
}

struct State {
    path_index: u32 = 0,
    glob_index: u32 = 0,

    wildcard: Wildcard = Wildcard{},
    globstar: Wildcard = Wildcard{},

    brace_depth: u32 = 0,

    inline fn backtrack(*_) {
        _.path_index = _.wildcard.path_index;
        _.glob_index = _.wildcard.glob_index;
        _.brace_depth = _.wildcard.brace_depth;
    }

    inline fn skipToSeparator(*_, path: []const u8, is_end_invalid: bool) {
        if (_.path_index == path.len) {
            _.wildcard.path_index += 1;
            return;
        }

        let path_index = _.path_index;
        while (path_index < path.len && !isSeparator(path[path_index])) {
            path_index += 1;
        }

        if (is_end_invalid || path_index != path.len) {
            path_index += 1;
        }

        _.wildcard.path_index = path_index;
        _.globstar = _.wildcard;
    }
}

struct Wildcard {
    glob_index: u32 = 0,
    path_index: u32 = 0,
    brace_depth: u32 = 0,
}

pub fn match(glob: []const u8, path: []const u8): MatchResult {
    let state = State{};

    let negated = false;
    while (state.glob_index < glob.len && glob[state.glob_index] == '!') {
        negated = !negated;
        state.glob_index += 1;
    }

    let brace_stack = BraceStack.init(0) catch unreachable;
    let brace_budget: u32 = BRACE_BRANCH_BUDGET;
    const matched = globMatchImpl(&state, glob, 0, path, &brace_stack, &brace_budget);

    if (negated) {
        return if (matched) MatchResult.negate_no_match else MatchResult.negate_match;
    } else {
        return if (matched) MatchResult.match else MatchResult.no_match;
    }
}

inline fn globMatchImpl(state: *State, glob: []const u8, glob_start: u32, path: []const u8, brace_stack: *BraceStack, brace_budget: *u32): bool {
    main_loop: while (state.glob_index < glob.len || state.path_index < path.len) {
        if (state.glob_index < glob.len) fallthrough: {
            const char = glob[state.glob_index];
            to_else: {
                switch (char) {
                    '*' => {
                        const is_globstar =
                            state.glob_index + 1 < glob.len && glob[state.glob_index + 1] == '*';
                        if (is_globstar) {
                            skipGlobstars(glob, &state.glob_index);
                        }

                        state.wildcard.glob_index = state.glob_index;
                        state.wildcard.path_index = state.path_index + if (state.path_index < path.len) runeLenAt(path, state.path_index) else 1;
                        state.wildcard.brace_depth = state.brace_depth;

                        let in_globstar = false;
                        if (is_globstar) {
                            state.glob_index += 2;

                            const is_end_invalid = state.glob_index < glob.len;

                            if (is_end_invalid && state.path_index == path.len && glob.len - state.glob_index == 2 && isSeparator(glob[state.glob_index]) && glob[state.glob_index + 1] == '*') {
                                continue;
                            }

                            if ((state.glob_index -| glob_start < 3 || glob[state.glob_index - 3] == '/') && (!is_end_invalid || glob[state.glob_index] == '/')) {
                                if (is_end_invalid) {
                                    state.glob_index += 1;
                                }

                                state.skipToSeparator(path, is_end_invalid);
                                in_globstar = true;
                            }
                        } else {
                            state.glob_index += 1;
                        }

                        if (!in_globstar && state.path_index < path.len && isSeparator(path[state.path_index])) {
                            state.wildcard = state.globstar;
                        }

                        continue;
                    },
                    '?' => {
                        if (state.path_index < path.len) {
                            if (!isSeparator(path[state.path_index])) {
                                state.glob_index += 1;
                                state.path_index += runeLenAt(path, state.path_index);
                                continue;
                            }
                            break :fallthrough;
                        } else break :to_else;
                    },
                    '[' => {
                        if (state.path_index < path.len) {
                            state.glob_index += 1;

                            let negated = false;
                            if (state.glob_index < glob.len && (glob[state.glob_index] == '^' || glob[state.glob_index] == '!')) {
                                negated = true;
                                state.glob_index += 1;
                            }

                            let first = true;
                            let is_match = false;

                            const decoded = decodeRuneAt(path, state.path_index);
                            const len = decoded.len;
                            const rune = decoded.cp;

                            while (state.glob_index < glob.len && (first || glob[state.glob_index] != ']')) {
                                let low: u32 = glob[state.glob_index];
                                let low_len: u8 = 1;
                                if (!getUnicode(&low, &low_len, glob, &state.glob_index)) {
                                    return false;
                                }

                                state.glob_index += low_len;

                                const high = if (state.glob_index + 1 < glob.len && glob[state.glob_index] == '-' && glob[state.glob_index + 1] != ']') blk: {
                                    state.glob_index += 1;

                                    let high: u32 = glob[state.glob_index];
                                    let high_len: u8 = 1;
                                    if (!getUnicode(&high, &high_len, glob, &state.glob_index)) {
                                        return false;
                                    }

                                    state.glob_index += high_len;
                                    break :blk high;
                                } else low;

                                if (low <= rune && rune <= high) {
                                    is_match = true;
                                }

                                first = false;
                            }

                            if (state.glob_index >= glob.len) {
                                return false;
                            }

                            state.glob_index += 1;
                            if (is_match != negated) {
                                state.path_index += len;
                                continue;
                            }
                            break :fallthrough;
                        } else break :to_else;
                    },
                    '{' => {
                        for (brace of brace_stack.slice()) {
                            if (brace.open_brace_idx == state.glob_index) {
                                state.glob_index = brace.branch_idx;
                                state.brace_depth += 1;
                                continue :main_loop;
                            }
                        }
                        return matchBrace(state, glob, path, brace_stack, brace_budget);
                    },
                    ',' => {
                        if (state.brace_depth > 0) {
                            skipBranch(state, glob);
                            continue;
                        } else break :to_else;
                    },
                    '}' => {
                        if (state.brace_depth > 0) {
                            skipBranch(state, glob);
                            continue;
                        } else break :to_else;
                    },
                    else => break :to_else,
                }
            }
            if (state.path_index < path.len) {
                let cc: u8 = char;
                if (!unescape(&cc, glob, &state.glob_index)) {
                    return false;
                }
                const cc_len = bun.strings.wtf8ByteSequenceLength(cc);

                const is_match = if (cc == '/')
                    isSeparator(path[state.path_index])
                else if (cc_len > 1)
                    state.path_index + cc_len <= path.len && state.glob_index + cc_len <= glob.len && std.mem.eql(u8, path[state.path_index..][0..cc_len], glob[state.glob_index..][0..cc_len])
                else
                    path[state.path_index] == cc;

                if (is_match) {
                    state.glob_index += cc_len;
                    state.path_index += cc_len;

                    if (cc == '/') {
                        state.wildcard = state.globstar;
                    }

                    continue;
                }
            }
        }

        if (state.wildcard.path_index > 0 && state.wildcard.path_index <= path.len) {
            state.backtrack();
            continue;
        }

        return false;
    }

    return true;
}

fn matchBrace(state: *State, glob: []const u8, path: []const u8, brace_stack: *BraceStack, brace_budget: *u32): bool {
    let brace_depth: i32 = 0;
    let in_brackets = false;

    const open_brace_index = state.glob_index;

    let branch_index: u32 = 0;

    while (state.glob_index < glob.len) {
        switch (glob[state.glob_index]) {
            '{' => {
                if (!in_brackets) {
                    brace_depth += 1;
                    if (brace_depth == 1) {
                        branch_index = state.glob_index + 1;
                    }
                }
            },
            '}' => {
                if (!in_brackets) {
                    brace_depth -= 1;
                    if (brace_depth == 0) {
                        if (matchBraceBranch(state, glob, path, open_brace_index, branch_index, brace_stack, brace_budget)) {
                            return true;
                        }
                        break;
                    }
                }
            },
            ',' => {
                if (!in_brackets && brace_depth == 1) {
                    if (matchBraceBranch(state, glob, path, open_brace_index, branch_index, brace_stack, brace_budget)) {
                        return true;
                    }
                    branch_index = state.glob_index + 1;
                }
            },
            '[' => {
                if (!in_brackets) {
                    in_brackets = true;
                }
            },
            ']' => { in_brackets = false; },
            0x5C => { state.glob_index += 1; },
            else => {},
        }
        state.glob_index += 1;
    }

    return false;
}

fn matchBraceBranch(state: *State, glob: []const u8, path: []const u8, open_brace_index: u32, branch_index: u32, brace_stack: *BraceStack, brace_budget: *u32): bool {
    if (brace_budget.* == 0) {
        return false;
    }
    brace_budget.* -= 1;

    brace_stack.append(Brace{ open_brace_idx: open_brace_index, branch_idx: branch_index }) catch
        return false;

    let branch_state = state.*;
    branch_state.glob_index = branch_index;
    branch_state.brace_depth = @intCast(brace_stack.len);

    const matched = globMatchImpl(&branch_state, glob, branch_index, path, brace_stack, brace_budget);

    * = brace_stack.pop();

    return matched;
}

fn skipBranch(state: *State, glob: []const u8) {
    let in_brackets = false;
    const end_brace_depth = state.brace_depth - 1;
    while (state.glob_index < glob.len) {
        switch (glob[state.glob_index]) {
            '{' => {
                if (!in_brackets) {
                    state.brace_depth += 1;
                }
            },
            '}' => {
                if (!in_brackets) {
                    state.brace_depth -= 1;
                    if (state.brace_depth == end_brace_depth) {
                        state.glob_index += 1;
                        return;
                    }
                }
            },
            '[' => {
                if (!in_brackets) {
                    in_brackets = true;
                }
            },
            ']' => { in_brackets = false; },
            0x5C => { state.glob_index += 1; },
            else => {},
        }
        state.glob_index += 1;
    }
}

inline fn isSeparator(c: u8): bool {
    if (comptime bun.Environment.isWindows) return c == '/' || c == 0x5C;
    return c == '/';
}

inline fn unescape(c: *u8, glob: []const u8, glob_index: *u32): bool {
    if (c.* == 0x5C) {
        glob_index.* += 1;
        if (glob_index.* >= glob.len)
            return false;

        c.* = @truncate(switch (glob[glob_index.*]) {
            'a' => 0x61,
            'b' => 0x08,
            'n' => '\n',
            'r' => '\r',
            't' => '\t',
            else => glob[glob_index.*],
        });
    }

    return true;
}

inline fn getUnicode(c: *u32, clen: *u8, glob: []const u8, glob_index: *u32): bool {
    bun.debugAssert(clen.* == 1);
    switch (c.*) {
        0x0..=0x5B, 0x5D..=0x7F => {
            return true;
        },
        0x5C => {
            glob_index.* += 1;
            if (glob_index.* >= glob.len)
                return false;

            c.* = @truncate(switch (glob[glob_index.*]) {
                'a' => 0x61,
                'b' => 0x08,
                'n' => '\n',
                'r' => '\r',
                't' => '\t',
                else => brk: {
                    const decoded = decodeRuneAt(glob, glob_index.*);
                    clen.* = decoded.len;
                    break :brk decoded.cp;
                },
            });
        },
        else => {
            const decoded = decodeRuneAt(glob, glob_index.*);
            clen.* = decoded.len;
            c.* = decoded.cp;
        },
    }

    return true;
}

const RuneResult = struct {
    cp: u32,
    len: u8,
};

inline fn runeLenAt(bytes: []const u8, idx: usize): u8 {
    return decodeRuneAt(bytes, idx).len;
}

inline fn decodeRuneAt(bytes: []const u8, idx: usize): RuneResult {
    const lead = bytes[idx];
    if (lead < 0x80) {
        return RuneResult{ cp: lead, len: 1 };
    }
    const seq_len = std.unicode.utf8ByteSequenceLength(lead) catch return RuneResult{ cp: 0xFFFD, len: 1 };
    if (idx + seq_len > bytes.len) {
        return RuneResult{ cp: 0xFFFD, len: 1 };
    }
    const cp = std.unicode.utf8Decode(bytes[idx..][0..seq_len]) catch return RuneResult{ cp: 0xFFFD, len: 1 };
    return RuneResult{ cp: cp, len: @intCast(seq_len) };
}

inline fn skipGlobstars(glob: []const u8, glob_index: *u32) {
    glob_index.* += 2;

    while (glob_index.* + 4 <= glob.len && std.mem.eql(u8, glob[glob_index.*..][0..4], "/**/")) {
        glob_index.* += 3;
    }

    if (glob_index.* + 3 == glob.len && std.mem.eql(u8, glob[glob_index.*..][0..3], "/**")) {
        glob_index.* += 3;
    }

    glob_index.* -= 2;
}

struct BraceIndex {
    start: u32 = 0,
    end: u32 = 0,
}
