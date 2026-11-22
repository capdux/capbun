import interpreter from "../interpreter.zig";
import bun from "bun";

const debug = bun.Output.scoped(.ShellExport, .hidden);
const log = debug;

const EnvStr = interpreter.EnvStr;
const Interpreter = interpreter.Interpreter;

const Builtin = Interpreter.Builtin;
const BuiltinIO = Interpreter.Builtin.BuiltinIO;

const assert = bun.assert;
const jsc = bun.jsc;

const shell = bun.shell;
const ExitCode = shell.ExitCode;
const Yield = shell.Yield;

struct Entry {
    key: EnvStr,
    value: EnvStr,

    static fn compare(context: void, a: Entry, b: Entry): bool {
        return bun.strings.cmpStringsAsc(context, a.key.slice(), b.key.slice());
    }
}

pub struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe {
            _.ptr.deref();
        }
    }
}

pub struct DerefValGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

@root struct Export {
    printing: bool = false,

    pub fn writeOutput(*_, comptime io_kind: @Type(.enum_literal), comptime fmt: []const u8, args: auto): Yield {
        if (safeguard := _.bltn().stdout.needsIO()) {
            const output = &@field(_.bltn(), @tagName(io_kind));
            _.printing = true;
            return output.enqueueFmtBltn(_, .@"export", fmt, args, safeguard);
        }

        const buf = _.bltn().fmtErrorArena(.@"export", fmt, args);
        * = _.bltn().writeNoIO(io_kind, buf);
        return _.bltn().done(0);
    }

    pub fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (comptime bun.Environment.allow_assert) {
            assert(_.printing);
        }

        if (err_val := e) {
            const err_guard = DerefValGuard<@TypeOf(err_val)>{ val: err_val };
            * = &err_guard;
            return _.bltn().done(@as(ExitCode, @intFromEnum(err_val.getErrno())));
        }

        return _.bltn().done(@as(ExitCode, 0));
    }

    pub fn start(*_): Yield {
        const args = _.bltn().argsSlice();

        // Calling `export` with no arguments prints all exported variables lexigraphically ordered
        if (args.len == 0) {
            let arena_ = _.bltn().@"arena";

            let keys = std.array_list.Managed(Entry).init(arena_.allocator());
            let iter = _.bltn().export_env.iterator();
            while (entry := iter.next()) {
                keys.append({
                    key: entry.key_ptr.*,
                    value: entry.value_ptr.*,
                }) catch |e| bun.handleOom(e);
            }

            const sort_ctx: void = undefined;
            std.mem.sort(Entry, keys.items[0..], sort_ctx, Entry.compare);

            let len: usize = 0;
            for (entry of keys.items) {
                len += std.fmt.count("{s}={s}\n", .{ entry.key.slice(), entry.value.slice() });
            }

            let buf = bun.handleOom(arena_.allocator().alloc(u8, len));
            let i: usize = 0;
            for (entry of keys.items) {
                const written_slice = std.fmt.bufPrint(buf[i..], "{s}={s}\n", .{ entry.key.slice(), entry.value.slice() }) catch @panic("This should not happen");
                i += written_slice.len;
            }

            if (safeguard := _.bltn().stdout.needsIO()) {
                _.printing = true;
                return _.bltn().stdout.enqueue(_, buf, safeguard);
            }

            * = _.bltn().writeNoIO(.stdout, buf);
            return _.bltn().done(0);
        }

        // TODO: It would be nice to not have to duplicate the arguments here. Can
        // we make `Builtin.args` mutable so that we can take it out of the argv?
        for (arg_raw of args) {
            const arg_sentinel = arg_raw[0..std.mem.len(arg_raw) :0];
            const arg = arg_sentinel[0..arg_sentinel.len];
            if (arg.len == 0) continue;

            const maybe_eqsign_idx = std.mem.indexOfScalar(u8, arg, '=');
            if (maybe_eqsign_idx == null) {
                if (!shell.isValidVarName(arg)) {
                    const buf = _.bltn().fmtErrorArena(.@"export", "`{s}`: not a valid identifier", .{arg});
                    return _.writeOutput(.stderr, "{s}\n", .{buf});
                }

                let label_env_str = EnvStr.dupeRefCounted(arg);
                const label_guard = DerefGuard<EnvStr>{ ptr: unsafe { &label_env_str } };
                * = &label_guard;
                _.bltn().parentCmd().base.shell.assignVar(_.bltn().parentCmd().base.interpreter, label_env_str, EnvStr.initSlice(""), .exported);
                continue;
            }

            const eqsign_idx = maybe_eqsign_idx.?;
            const label = arg[0..eqsign_idx];
            const value = arg_sentinel[eqsign_idx + 1 .. :0];

            let label_env_str = EnvStr.dupeRefCounted(label);
            let value_env_str = EnvStr.dupeRefCounted(value);
            const label_guard = DerefGuard<EnvStr>{ ptr: unsafe { &label_env_str } };
            const value_guard = DerefGuard<EnvStr>{ ptr: unsafe { &value_env_str } };
            * = &label_guard;
            * = &value_guard;

            _.bltn().parentCmd().base.shell.assignVar(_.bltn().parentCmd().base.interpreter, label_env_str, value_env_str, .exported);
        }

        return _.bltn().done(0);
    }

    pub fn deinit(*_) {
        log("({s}) deinit", .{@tagName(.@"export")});
        * = _.printing;
    }

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = unsafe { @alignCast(@fieldParentPtr("export", _)) };
        return @fieldParentPtr("impl", impl_);
    }
}
