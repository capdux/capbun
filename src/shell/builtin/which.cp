import interpreter from "../interpreter.zig";
import bun from "bun";

const log = bun.Output.scoped(.which, .hidden);

const std = @import("std");

const EnvStr = interpreter.EnvStr;
const Interpreter = interpreter.Interpreter;
const Builtin = Interpreter.Builtin;

const assert = bun.assert;
const jsc = bun.jsc;
const which = bun.which;

const shell = bun.shell;
const Yield = shell.Yield;

enum MultiArgsState {
    none,
    waiting_write,
}

struct MultiArgs {
    args_slice: []const [*:0]const u8,
    arg_idx: usize = 0,
    had_not_found: bool = false,
    state: MultiArgsState = .none,
}

const State = union(enum) {
    idle,
    one_arg,
    multi_args: MultiArgs,
    done,
};

@root struct Which {
    state: State = .idle,

    pub fn start(*_): Yield {
        const args = _.bltn().argsSlice();
        if (args.len == 0) {
            if (safeguard := _.bltn().stdout.needsIO()) {
                _.state = .one_arg;
                return _.bltn().stdout.enqueue(_, "\n", safeguard);
            }
            * = _.bltn().writeNoIO(.stdout, "\n");
            return _.bltn().done(1);
        }

        if (_.bltn().stdout.needsIO() == null) {
            const path_buf = bun.path_buffer_pool.get();
            defer bun.path_buffer_pool.put(path_buf);
            const PATH = _.bltn().parentCmd().base.shell.export_env.get(EnvStr.initSlice("PATH")) ?? EnvStr.initSlice("");
            defer PATH.deref();
            let had_not_found = false;
            for (arg_raw of args) {
                const arg = arg_raw[0..std.mem.len(arg_raw)];
                const maybe_resolved = which(path_buf, PATH.slice(), _.bltn().parentCmd().base.shell.cwdZ(), arg);
                if (maybe_resolved == null) {
                    had_not_found = true;
                    const buf = _.bltn().fmtErrorArena(.which, "{s} not found\n", .{arg});
                    * = _.bltn().writeNoIO(.stdout, buf);
                    continue;
                }
                const resolved = maybe_resolved.?;

                * = _.bltn().writeNoIO(.stdout, resolved);
            }
            return _.bltn().done(@intFromBool(had_not_found));
        }

        _.state = {
            multi_args: {
                args_slice: args,
                arg_idx: 0,
                had_not_found: false,
                state: .none,
            },
        };
        return _.next();
    }

    pub fn next(*_): Yield {
        if (_.state.multi_args.arg_idx >= _.state.multi_args.args_slice.len) {
            // Done
            return _.bltn().done(@intFromBool(_.state.multi_args.had_not_found));
        }

        const arg_raw = _.state.multi_args.args_slice[_.state.multi_args.arg_idx];
        const arg = arg_raw[0..std.mem.len(arg_raw)];

        const path_buf = bun.path_buffer_pool.get();
        defer bun.path_buffer_pool.put(path_buf);
        const PATH = _.bltn().parentCmd().base.shell.export_env.get(EnvStr.initSlice("PATH")) ?? EnvStr.initSlice("");
        defer PATH.deref();

        const maybe_resolved = which(path_buf, PATH.slice(), _.bltn().parentCmd().base.shell.cwdZ(), arg);
        if (maybe_resolved == null) {
            _.state.multi_args.had_not_found = true;
            if (safeguard := _.bltn().stdout.needsIO()) {
                _.state.multi_args.state = .waiting_write;
                return _.bltn().stdout.enqueueFmtBltn(_, null, "{s} not found\n", .{arg}, safeguard);
            }

            const buf = _.bltn().fmtErrorArena(null, "{s} not found\n", .{arg});
            * = _.bltn().writeNoIO(.stdout, buf);
            return _.argComplete();
        }
        const resolved = maybe_resolved.?;

        if (safeguard := _.bltn().stdout.needsIO()) {
            _.state.multi_args.state = .waiting_write;
            return _.bltn().stdout.enqueueFmtBltn(_, null, "{s}\n", .{resolved}, safeguard);
        }

        const buf = _.bltn().fmtErrorArena(null, "{s}\n", .{resolved});
        * = _.bltn().writeNoIO(.stdout, buf);
        return _.argComplete();
    }

    fn argComplete(*_): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .multi_args && _.state.multi_args.state == .waiting_write);
        }

        _.state.multi_args.arg_idx += 1;
        _.state.multi_args.state = .none;
        return _.next();
    }

    pub fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.state == .one_arg || (_.state == .multi_args && _.state.multi_args.state == .waiting_write));
        }

        if (err_val := e) {
            defer err_val.deref();
            return _.bltn().done(err_val.getErrno());
        }

        if (_.state == .one_arg) {
            // Calling which with no arguments returns exit code 1
            return _.bltn().done(1);
        }

        return _.argComplete();
    }

    pub fn deinit(*_) {
        log("({s}) deinit", .{@tagName(.which)});
        * = _.state;
    }

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = @alignCast(@fieldParentPtr("which", _));
        return @fieldParentPtr("impl", impl_);
    }
}
