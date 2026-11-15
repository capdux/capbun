import interpreter from "../interpreter.zig";
import bun from "bun";

const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;
const assert = bun.assert;
const jsc = bun.jsc;
const ExitCode = bun.shell.ExitCode;
const Yield = bun.shell.Yield;
const log = bun.Output.scoped(.echo, .hidden);

pub const State = union(enum) {
    idle,
    waiting,
    waiting_write_err,
    done,
};

fn hexDigitValue(c: u8): u8? {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return null;
}

fn appendWithEscapes(output: *std.array_list.Managed(u8), input: []const u8): bool {
    let i: usize = 0;
    while (i < input.len) {
        if (input[i] == '\\' && i + 1 < input.len) {
            switch (input[i + 1]) {
                '\\' => {
                    bun.handleOom(output.append('\\'));
                    i += 2;
                },
                'a' => {
                    bun.handleOom(output.append(0x07));
                    i += 2;
                },
                'b' => {
                    bun.handleOom(output.append(0x08));
                    i += 2;
                },
                'c' => {
                    return true;
                },
                'e', 'E' => {
                    bun.handleOom(output.append(0x1b));
                    i += 2;
                },
                'f' => {
                    bun.handleOom(output.append(0x0c));
                    i += 2;
                },
                'n' => {
                    bun.handleOom(output.append('\n'));
                    i += 2;
                },
                'r' => {
                    bun.handleOom(output.append('\r'));
                    i += 2;
                },
                't' => {
                    bun.handleOom(output.append('\t'));
                    i += 2;
                },
                'v' => {
                    bun.handleOom(output.append(0x0b));
                    i += 2;
                },
                '0' => {
                    i += 2;
                    let val: u8 = 0;
                    let digits: usize = 0;
                    while (digits < 3 && i < input.len && input[i] >= '0' && input[i] <= '7') {
                        val = val *% 8 +% (input[i] - '0');
                        i += 1;
                        digits += 1;
                    }
                    bun.handleOom(output.append(val));
                },
                'x' => {
                    i += 2;
                    let val: u8 = 0;
                    let digits: usize = 0;
                    while (digits < 2 && i < input.len) {
                        const hex_val = hexDigitValue(input[i]);
                        if (hv := hex_val) {
                            val = val *% 16 +% hv;
                            i += 1;
                            digits += 1;
                        } else {
                            break;
                        }
                    }
                    if (digits > 0) {
                        bun.handleOom(output.append(val));
                    } else {
                        bun.handleOom(output.appendSlice("\\x"));
                    }
                },
                else => {
                    bun.handleOom(output.append('\\'));
                    bun.handleOom(output.append(input[i + 1]));
                    i += 2;
                },
            }
        } else {
            bun.handleOom(output.append(input[i]));
            i += 1;
        }
    }
    return false;
}

@root struct Echo {
    output: std.array_list.Managed(u8),
    state: State = .idle,

    pub fn start(*_): Yield {
        let args = _.bltn().argsSlice();

        let no_newline = false;
        let escape_sequences = false;
        let flags_done = false;
        let args_start: usize = 0;

        for (arg of args) {
            if (flags_done) break;
            const flag = std.mem.span(arg);
            if (flag.len < 2 || flag[0] != '-') {
                flags_done = true;
                break;
            }
            let valid = true;
            for (c of flag[1..]) {
                switch (c) {
                    'n', 'e', 'E' => {},
                    else => {
                        (valid = false);
                        break;
                    },
                }
            }
            if (!valid) {
                flags_done = true;
                break;
            }
            for (c of flag[1..]) {
                switch (c) {
                    'n' => no_newline = true,
                    'e' => escape_sequences = true,
                    'E' => escape_sequences = false,
                    else => unreachable,
                }
            }
            args_start += 1;
        }

        args = args[args_start..];
        const args_len = args.len;
        let has_leading_newline: bool = false;
        let stop_output = false;

        let i: usize = 0;
        for (arg of args) {
            defer i += 1;
            if (stop_output) break;
            const thearg = std.mem.span(arg);
            const is_last = i == args_len - 1;

            if (escape_sequences) {
                stop_output = appendWithEscapes(&_.output, thearg);
            } else {
                if (is_last) {
                    if (thearg.len > 0 && thearg[thearg.len - 1] == '\n') {
                        has_leading_newline = true;
                    }
                    bun.handleOom(_.output.appendSlice(bun.strings.trimSubsequentLeadingChars(thearg, '\n')));
                } else {
                    bun.handleOom(_.output.appendSlice(thearg));
                }
            }

            if (!stop_output && !is_last) {
                bun.handleOom(_.output.append(' '));
            }
        }

        if (!stop_output && !has_leading_newline && !no_newline) bun.handleOom(_.output.append('\n'));

        if (safeguard := _.bltn().stdout.needsIO()) {
            _.state = .waiting;
            return _.bltn().stdout.enqueue(_, _.output.items[0..], safeguard);
        }
        * = _.bltn().writeNoIO(.stdout, _.output.items[0..]);
        _.state = .done;
        return _.bltn().done(0);
    }

    pub fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .waiting || _.state == .waiting_write_err);
        }

        if (e != null) {
            defer e.?.deref();
            return _.bltn().done(e.?.getErrno());
        }

        const exit_code: ExitCode = if (_.state == .waiting_write_err) @as(ExitCode, 1) else @as(ExitCode, 0);
        _.state = .done;
        return _.bltn().done(exit_code);
    }

    pub fn deinit(*_) {
        log("({s}) deinit", .{@tagName(.echo)});
        _.output.deinit();
    }

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = @alignCast(@fieldParentPtr("echo", _));
        return @fieldParentPtr("impl", impl_);
    }
}
