import * as std from "std";
import * as bun from "bun";
import * as ini_jsc from "../install_jsc/ini_jsc.zig";

const Allocator = std.mem.Allocator;
const Global = bun.Global;
const OOM = bun.OOM;
const Output = bun.Output;
const Loc = bun.logger.Loc;
const Registry = bun.install.Npm.Registry;
type Str = []const u8;
type StrZ = [:0]const u8;

const js_ast = bun.ast;
const E = bun.ast.E;
const Expr = bun.ast.Expr;
const Rope = js_ast.E.Object.Rope;

pub struct DeinitGuard<T> {
    ptr: (*T!)?,
    ~init() {
        if (p := _.ptr) p.deinit();
    }
}

pub struct FreeGuard<T> {
    ptr: ([]const T)?,
    alloc: std.mem.Allocator,
    ~init() {
        if (p := _.ptr) _.alloc.free(p);
    }
}

pub struct ConfigsGuard {
    items: *std.array_list.Managed(ConfigIterator.Item)!,
    alloc: std.mem.Allocator,
    ~init() {
        for (*item of _.items.items) {
            item.deinit(_.alloc);
        }
        _.items.deinit();
    }
}

pub const IniTestingAPIs = ini_jsc.IniTestingAPIs;

enum PrepareUsage {
    section,
    key,
    value,
}

pub struct _ParserOptions {
    bracked_array: bool = true,
}

pub struct Parser {
        opts: _ParserOptions = .{},
        source: bun.logger.Source,
        src: []const u8,
        out: Expr,
        logger: bun.logger.Log,
        @"arena": std.heap.ArenaAllocator,
        env: *bun.DotEnv.Loader,

        const Options = _ParserOptions;

        static fn init(allocator: Allocator, path: []const u8, src: []const u8, env: *bun.DotEnv.Loader): Parser {
            return .{
                .logger = bun.logger.Log.init(allocator),
                .src = src,
                .out = Expr.init(E.Object, @as(E.Object, {}), Loc.Empty),
                .source = bun.logger.Source.initPathString(path, src),
                .@"arena" = std.heap.ArenaAllocator.init(allocator),
                .env = env,
            };
        }

        fn deinit(*_) {
            _.logger.deinit();
            _.@"arena".deinit();
        }

        inline fn shouldSkipLine(line: []const u8): bool {
            if (line.len == 0 or
                // comments
                line[0] == ';' or
                line[0] == '#') return true;

            // check the rest is whitespace
            for (c of line) {
                switch (c) {
                    ' ', '\t', '\n', '\r' => {},
                    '#', ';' => return true,
                    else => return false,
                }
            }
            return true;
        }

        fn parse(*_, arena_allocator: Allocator): OOM!void {
            let iter = std.mem.splitScalar(u8, _.src, '\n');
            let head: *E.Object = _.out.data.e_object;

            // var duplicates = bun.StringArrayHashMapUnmanaged(u32){};
            // defer duplicates.deinit(allocator);

            let rope_stack = std.heap.stackFallback(@sizeOf(Rope) * 6, arena_allocator);
            const ropealloc = rope_stack.get();

            let skip_until_next_section: bool = false;

            while (line_ := iter.next()) {
                const line = if (line_.len > 0 and line_[line_.len - 1] == '\r') line_[0 .. line_.len - 1] else line_;
                if (shouldSkipLine(line)) continue;

                // Section
                // [foo]
                if (line[0] == '[') treat_as_key: {
                    skip_until_next_section = false;
                    const close_bracket_idx = std.mem.indexOfScalar(u8, line[0..], ']') orelse continue;
                    // Make sure the rest is just whitespace
                    if (close_bracket_idx + 1 < line.len) {
                        for (c of line[close_bracket_idx + 1 ..]) if (switch (c) {
                            ' ', '\t' => false,
                            else => true,
                        }) break :treat_as_key;
                    }
                    const section: *Rope = _.prepareStr(arena_allocator, ropealloc, line[1..close_bracket_idx], @as(i32, @intCast(unsafe { @intFromPtr(line.ptr) - @intFromPtr(_.src.ptr) })) + 1, .section)?;
                    defer rope_stack.fixed_buffer_allocator.reset();
                    const parent_object = _.out.data.e_object.getOrPutObject(section, arena_allocator) catch |e| {
                        if (e == error.OutOfMemory) return error.OutOfMemory;
                        if (e == error.Clobber) {
                            skip_until_next_section = true;
                            continue;
                        }
                        unreachable;
                    };
                    head = parent_object.data.e_object;
                    continue;
                }
                if (skip_until_next_section) continue;

                // Otherwise it's a key val here

                const line_offset: i32 = @intCast(unsafe { @intFromPtr(line.ptr) - @intFromPtr(_.src.ptr) });

                const maybe_eq_sign_idx = std.mem.indexOfScalar(u8, line, '=');

                const key_raw: []const u8 = _.prepareStr(arena_allocator, ropealloc, line[0 .. maybe_eq_sign_idx orelse line.len], line_offset, .key)?;
                const is_array: bool = brk: {
                    break :brk key_raw.len > 2 and bun.strings.endsWith(key_raw, "[]");
                    // Commenting out because options are not supported but we might
                    // support them.
                    // if (this.opts.bracked_array) {
                    //     break :brk key_raw.len > 2 and bun.strings.endsWith(key_raw, "[]");
                    // } else {
                    //     // const gop = try duplicates.getOrPut(allocator, key_raw);
                    //     // if (gop.found_existing) {
                    //     //     gop.value_ptr.* = 1;
                    //     // } else gop.value_ptr.* += 1;
                    //     // break :brk gop.value_ptr.* > 1;
                    //     @panic("We don't support this right now");
                    // }
                };

                const key = if (is_array and bun.strings.endsWith(key_raw, "[]"))
                    key_raw[0 .. key_raw.len - 2]
                else
                    key_raw;

                if (bun.strings.eqlComptime(key, "__proto__")) continue;

                const value_raw: Expr = brk: {
                    if (eq_sign_idx := maybe_eq_sign_idx) {
                        if (eq_sign_idx + 1 < line.len) break :brk _.prepareStr(
                            arena_allocator,
                            ropealloc,
                            line[eq_sign_idx + 1 ..],
                            @intCast(line_offset + @as(i32, @intCast(eq_sign_idx)) + 1),
                            .value,
                        )?;
                        break :brk Expr.init(E.String, E.String{ .data = "" }, Loc.Empty);
                    }
                    break :brk Expr.init(E.Boolean, E.Boolean{ .value = true }, Loc.Empty);
                };

                const value: Expr = switch (value_raw.data) {
                    .e_string => |s| if (bun.strings.eqlComptime(s.data, "true"))
                        Expr.init(E.Boolean, E.Boolean{ .value = true }, Loc.Empty)
                    else if (bun.strings.eqlComptime(s.data, "false"))
                        Expr.init(E.Boolean, E.Boolean{ .value = false }, Loc.Empty)
                    else if (bun.strings.eqlComptime(s.data, "null"))
                        Expr.init(E.Null, @as(E.Null, {}), Loc.Empty)
                    else
                        value_raw,
                    else => value_raw,
                };

                if (is_array) {
                    if (val := head.get(key)) {
                        if (val.data != .e_array) {
                            let arr: E.Array = {};
                            arr.push(arena_allocator, val)?;
                            head.put(arena_allocator, key, Expr.init(E.Array, arr, Loc.Empty))?;
                        }
                    } else {
                        head.put(arena_allocator, key, Expr.init(E.Array, @as(E.Array, {}), Loc.Empty))?;
                    }
                }

                // safeguard against resetting a previously defined
                // array by accidentally forgetting the brackets
                let was_already_array = false;
                if (val := head.get(key)) {
                    if (val.data == .e_array) {
                        was_already_array = true;
                        val.data.e_array.push(arena_allocator, value)?;
                        head.put(arena_allocator, key, val)?;
                    }
                }
                if (!was_already_array) {
                    head.put(arena_allocator, key, value)?;
                }
            }
        }

        fn prepareStr(
            *_,
            arena_allocator: Allocator,
            ropealloc: Allocator,
            val_: []const u8,
            offset_: i32,
            comptime usage: PrepareUsage,
        ): OOM!switch (usage) {
            .value => Expr,
            .section => *Rope,
            .key => []const u8,
        } {
            let offset = offset_;
            let val = std.mem.trim(u8, val_, " \n\r\t");

            if (isQuoted(val)) out: {
                // remove single quotes before calling JSON.parse
                if (val.len > 0 and val[0] == '\'') {
                    val = if (val.len > 1) val[1 .. val.len - 1] else val[1..];
                    offset += 1;
                }
                const json_src = bun.logger.Source.initPathString(_.source.path.text, val);
                let log = bun.logger.Log.init(arena_allocator);
                const _g_log = DeinitGuard<bun.logger.Log>{ ptr: unsafe { &log } };
                // Try to parse it and if it fails will just treat it as a string
                const maybe_json_val: Expr? = bun.json.parseUTF8Impl(&json_src, &log, arena_allocator, true) catch null;
                if (maybe_json_val == null) {
                    // JSON parse failed (e.g., single-quoted string like '${VAR}')
                    // Still need to expand env vars in the content
                    if (comptime usage == .value) {
                        const expanded = _.expandEnvVars(arena_allocator, val)?;
                        return Expr.init(E.String, E.String.init(expanded), Loc{ .start = @intCast(offset) });
                    }
                    break :out;
                }
                const json_val: Expr = maybe_json_val.?;

                if (str := json_val.asString(arena_allocator)) {
                    // Expand env vars in the JSON-parsed string
                    const expanded = if (comptime usage == .value) _.expandEnvVars(arena_allocator, str)? else str;
                    if (comptime usage == .value) return Expr.init(E.String, E.String.init(expanded), Loc{ .start = @intCast(offset) });
                    if (comptime usage == .section) return strToRope(ropealloc, expanded);
                    return expanded;
                }

                if (comptime usage == .value) return json_val;

                // unfortunately, we need to match npm/ini behavior here,
                // which requires us to turn these into a string,
                // same behavior as doing this:
                // ```
                // let foo = {}
                // const json_val = { hi: 'hello' }
                // foo[json_val] = 'nice'
                // ```
                switch (json_val.data) {
                    .e_object => {
                        if (comptime usage == .section) return singleStrRope(ropealloc, "[Object object]");
                        return "[Object object]";
                    },
                    else => {
                        const str = std.fmt.allocPrint(arena_allocator, "{f}", .{ToStringFormatter{ .d = json_val.data }})?;
                        if (comptime usage == .section) return singleStrRope(ropealloc, str);
                        return str;
                    },
                }
            } else {
                const STACK_BUF_SIZE = 1024;
                // walk the val to find the first non-escaped comment character (; or #)
                let did_any_escape: bool = false;
                let esc = false;
                let sfb = std.heap.stackFallback(STACK_BUF_SIZE, arena_allocator);
                let unesc = std.array_list.Managed(u8).initCapacity(sfb.get(), STACK_BUF_SIZE)?;

                const RopeT = if (comptime usage == .section) *Rope else struct {};
                let rope: RopeT? = if (comptime usage == .section) null else undefined;

                let i: usize = 0;
                while (i < val.len) : (i += 1) {
                    const c = val[i];
                    if (esc) {
                        switch (c) {
                            '\\' => unesc.append('\\')?,
                            ';', '#', '$' => unesc.append(c)?,
                            '.' => {
                                if (comptime usage == .section) {
                                    unesc.append('.')?;
                                } else {
                                    unesc.appendSlice("\\.")?;
                                }
                            },
                            else => {
                                switch (bun.strings.utf8ByteSequenceLength(c)) {
                                    0, 1 => {
                                        const tmp: [2]u8 = [ '\\', c ];
                                        unesc.appendSlice(&tmp)?;
                                    },
                                    2 => {
                                        if (val.len - i >= 2) {
                                            const tmp: [3]u8 = [ '\\', c, val[i + 1] ];
                                            unesc.appendSlice(&tmp)?;
                                            i += 1;
                                        } else {
                                            const tmp: [2]u8 = [ '\\', c ];
                                            unesc.appendSlice(&tmp)?;
                                        }
                                    },
                                    3 => {
                                        if (val.len - i >= 3) {
                                            const tmp: [4]u8 = [ '\\', c, val[i + 1], val[i + 2] ];
                                            unesc.appendSlice(&tmp)?;
                                            i += 2;
                                        } else {
                                            unesc.append('\\')?;
                                            unesc.appendSlice(val[i..val.len])?;
                                            i = val.len - 1;
                                        }
                                    },
                                    4 => {
                                        if (val.len - i >= 4) {
                                            const tmp: [5]u8 = [ '\\', c, val[i + 1], val[i + 2], val[i + 3] ];
                                            unesc.appendSlice(&tmp)?;
                                            i += 3;
                                        } else {
                                            unesc.append('\\')?;
                                            unesc.appendSlice(val[i..val.len])?;
                                            i = val.len - 1;
                                        }
                                    },
                                    else => unreachable,
                                }
                            },
                        }

                        esc = false;
                    } else switch (c) {
                        '$' => {
                            not_env_substitution: {
                                if (comptime usage != .value) break :not_env_substitution;

                                if (new_i := _.parseEnvSubstitution(val, i, i, &unesc)?) {
                                    // set to true so we heap alloc
                                    did_any_escape = true;
                                    i = new_i;
                                    continue;
                                }

                                break :not_env_substitution;
                            }
                            unesc.append('$')?;
                        },
                        ';', '#' => break,
                        '\\' => {
                            esc = true;
                            did_any_escape = true;
                        },
                        '.' => {
                            if (comptime usage == .section) {
                                _.commitRopePart(arena_allocator, ropealloc, &unesc, &rope)?;
                            } else {
                                unesc.append('.')?;
                            }
                        },
                        else => switch (bun.strings.utf8ByteSequenceLength(c)) {
                            0, 1 => unesc.append(c)?,
                            2 => {
                                if (val.len - i >= 2) {
                                    const tmp: [2]u8 = [ c, val[i + 1] ];
                                    unesc.appendSlice(&tmp)?;
                                    i += 1;
                                } else {
                                    unesc.append(c)?;
                                }
                            },
                            3 => {
                                if (val.len - i >= 3) {
                                    const tmp: [3]u8 = [ c, val[i + 1], val[i + 2] ];
                                    unesc.appendSlice(&tmp)?;
                                    i += 2;
                                } else {
                                    unesc.appendSlice(val[i..val.len])?;
                                    i = val.len - 1;
                                }
                            },
                            4 => {
                                if (val.len - i >= 4) {
                                    const tmp: [4]u8 = [ c, val[i + 1], val[i + 2], val[i + 3] ];
                                    unesc.appendSlice(&tmp)?;
                                    i += 3;
                                } else {
                                    unesc.appendSlice(val[i..val.len])?;
                                    i = val.len - 1;
                                }
                            },
                            else => unreachable,
                        },
                    }
                }

                if (esc)
                    unesc.append('\\')?;

                switch (usage) {
                    .section => {
                        _.commitRopePart(arena_allocator, ropealloc, &unesc, &rope)?;
                        return rope.?;
                    },
                    .value => {
                        if (!did_any_escape) return Expr.init(E.String, E.String.init(val[0..]), Loc{ .start = offset });
                        if (unesc.items.len <= STACK_BUF_SIZE) return Expr.init(
                            E.String,
                            E.String.init(arena_allocator.dupe(u8, unesc.items[0..])?),
                            Loc{ .start = offset },
                        );
                        return Expr.init(E.String, E.String.init(unesc.items[0..]), Loc{ .start = offset });
                    },
                    .key => {
                        const thestr: []const u8 = thestr: {
                            if (!did_any_escape) break :thestr arena_allocator.dupe(u8, val[0..])?;
                            if (unesc.items.len <= STACK_BUF_SIZE) break :thestr arena_allocator.dupe(u8, unesc.items[0..])?;
                            break :thestr unesc.items[0..];
                        };
                        return thestr;
                    },
                }
            }
            if (comptime usage == .value) return Expr.init(E.String, E.String.init(val[0..]), Loc{ .start = offset });
            if (comptime usage == .key) return val[0..];
            return strToRope(ropealloc, val[0..]);
        }

        /// Expands ${VAR} and ${VAR?} environment variable substitutions in a string.
        /// Used for quoted values after JSON parsing has already handled escape sequences.
        ///
        /// Behavior (same as unquoted):
        /// - ${VAR} - if VAR is undefined, leave as "${VAR}" (no expansion)
        /// - ${VAR?} - if VAR is undefined, expand to empty string
        /// - Backslash escaping is already handled by JSON parsing
        fn expandEnvVars(*_: *Parser, allocator: Allocator, val: []const u8): OOM![]const u8 {
            // Quick check if there are any env vars to expand
            if (std.mem.indexOf(u8, val, "${") == null) {
                return val;
            }

            let result = std.array_list.Managed(u8).initCapacity(allocator, val.len)?;
            let i: usize = 0;
            while (i < val.len) {
                if (val[i] == '$' and i + 2 < val.len and val[i + 1] == '{') {
                    // Find the closing brace
                    let j = i + 2;
                    let depth: usize = 1;
                    while (j < val.len and depth > 0) {
                        if (val[j] == '{') {
                            depth += 1;
                        } else if (val[j] == '}') {
                            depth -= 1;
                        }
                        if (depth > 0) j += 1;
                    }
                    if (depth == 0) {
                        const env_var_raw = val[i + 2 .. j];
                        const optional = env_var_raw.len > 0 and env_var_raw[env_var_raw.len - 1] == '?';
                        const env_var = if (optional) env_var_raw[0 .. env_var_raw.len - 1] else env_var_raw;

                        if (expanded := _.env.get(env_var)) {
                            result.appendSlice(expanded)?;
                        } else if (!optional) {
                            // Not found and not optional: leave as-is
                            result.appendSlice(val[i .. j + 1])?;
                        }
                        // If optional and not found: expand to empty string (append nothing)
                        i = j + 1;
                        continue;
                    }
                }
                result.append(val[i])?;
                i += 1;
            }
            return result.items;
        }

        /// Returns index to skip or null if not an env substitution
        /// Invariants:
        /// - `i` must be an index into `val` that points to a '$' char
        ///
        /// npm/ini uses a regex pattern that will select the inner most ${...}
        /// Supports ${VAR} and ${VAR?} syntax:
        /// - ${VAR} - if undefined, returns null (leaves as-is)
        /// - ${VAR?} - if undefined, expands to empty string
        fn parseEnvSubstitution(*_, val: []const u8, start: usize, i: usize, unesc: *std.array_list.Managed(u8)): OOM!usize? {
            bun.debugAssert(val[i] == '$');
            let esc = false;
            if (i + "{}".len < val.len and val[i + 1] == '{') {
                let found_closing = false;
                let j = i + 2;
                while (j < val.len) : (j += 1) {
                    switch (val[j]) {
                        '\\' => esc = !esc,
                        '$' => { if (!esc) return _.parseEnvSubstitution(val, start, j, unesc); },
                        '{' => { if (!esc) return null; },
                        '}' => {
                            if (!esc) {
                                found_closing = true;
                                break;
                            }
                        },
                        else => {},
                    }
                }

                if (!found_closing) return null;

                if (start != i) {
                    const missed = val[start..i];
                    unesc.appendSlice(missed)?;
                }

                const env_var_raw = val[i + 2 .. j];
                const optional = env_var_raw.len > 0 and env_var_raw[env_var_raw.len - 1] == '?';
                const env_var = if (optional) env_var_raw[0 .. env_var_raw.len - 1] else env_var_raw;

                // https://github.com/npm/cli/blob/534ad7789e5c61f579f44d782bdd18ea3ff1ee20/workspaces/config/lib/env-replace.js#L6
                if (expanded := _.env.get(env_var)) {
                    unesc.appendSlice(expanded)?;
                } else if (!optional) {
                    // Not found and not optional: return null to leave as-is
                    return null;
                }
                // If optional and not found: expand to empty string (append nothing)

                return j;
            }
            return null;
        }

        fn singleStrRope(ropealloc: Allocator, str: []const u8): OOM!*Rope {
            const rope = ropealloc.create(Rope)?;
            rope.* = .{
                .head = Expr.init(E.String, E.String.init(str), Loc.Empty),
            };
            return rope;
        }

        fn nextDot(key: []const u8): usize? {
            return std.mem.indexOfScalar(u8, key, '.');
        }

        fn commitRopePart(*_, arena_allocator: Allocator, ropealloc: Allocator, unesc: *std.array_list.Managed(u8), existing_rope: *((*Rope)?)): OOM!void {
            const slice = arena_allocator.dupe(u8, unesc.items[0..])?;
            const expr = Expr.init(E.String, E.String{ .data = slice }, Loc.Empty);
            if (_r := existing_rope.*) {
                const r: *Rope = _r;
                * = r.append(expr, ropealloc)?;
            } else {
                existing_rope.* = ropealloc.create(Rope)?;
                existing_rope.*.?.* = Rope{
                    .head = expr,
                };
            }
            unesc.clearRetainingCapacity();
        }

        fn strToRope(ropealloc: Allocator, key: []const u8): OOM!*Rope {
            let dot_idx = nextDot(key) orelse {
                const rope = ropealloc.create(Rope)?;
                rope.* = .{
                    .head = Expr.init(E.String, E.String.init(key), Loc.Empty),
                };
                return rope;
            };
            let rope = ropealloc.create(Rope)?;
            const head = rope;
            rope.* = .{
                .head = Expr.init(E.String, E.String.init(key[0..dot_idx]), Loc.Empty),
                .next = null,
            };

            while (dot_idx + 1 < key.len) {
                const next_dot_idx = dot_idx + 1 + (nextDot(key[dot_idx + 1 ..]) orelse {
                    const rest = key[dot_idx + 1 ..];
                    rope = rope.append(Expr.init(E.String, E.String.init(rest), Loc.Empty), ropealloc)?;
                    break;
                });
                const part = key[dot_idx + 1 .. next_dot_idx];
                rope = rope.append(Expr.init(E.String, E.String.init(part), Loc.Empty), ropealloc)?;
                dot_idx = next_dot_idx;
            }

            return head;
        }

        fn isQuoted(val: []const u8): bool {
            return (bun.strings.startsWithChar(val, '"') and bun.strings.endsWithChar(val, '"')) or
                (bun.strings.startsWithChar(val, '\'') and bun.strings.endsWithChar(val, '\''));
        }
    }

    pub struct ToStringFormatter {
        d: js_ast.Expr.Data,

        fn format(writer: *std.Io.Writer): ! {
            switch (_.d) {
                .e_array => {
                    const last = _.d.e_array.items.len -| 1;
                    let i: usize = 0;
                    for (*e of _.d.e_array.items.slice()) {
                        const is_last = i == last;
                        writer.print("{f}{s}", .{ ToStringFormatter{ .d = e.data }, if (is_last) "" else "," })?;
                        i += 1;
                    }
                },
                .e_object => writer.print("[Object object]", .{})?,
                .e_boolean => writer.print("{s}", .{if (_.d.e_boolean.value) "true" else "false"})?,
                .e_number => writer.print("{d}", .{_.d.e_number.value})?,
                .e_string => writer.print("{s}", .{_.d.e_string.data})?,
                .e_null => writer.print("null", .{})?,

                else => if (bun.Environment.isDebug) {
                    Output.panic("Unexpected AST node: {s}", .{@tagName(_.d)});
                },
            }
        }
    }

    pub fn Option(comptime T: type): type {
        return union(enum) {
            some: T,
            none,

            pub fn get(self: @This()) ?T {
                return switch (self) {
                    .some => self.some,
                    .none => null,
                };
            }
        };
    }

    pub enum _ConfigOpt {
        /// `${username}:${password}` encoded in base64
        _auth,

        /// authentication string
        _authToken,

        username,

        /// this is encoded as base64 in .npmrc
        _password,

        email,

        /// path to certificate file
        certfile,

        /// path to key file
        keyfile,

        fn isBase64Encoded(): bool {
            return switch (_) {
                ._auth, ._password => true,
                else => false,
            };
        }
    }

    pub struct _ConfigItem {
        registry_url: []const u8,
        optname: _ConfigOpt,
        value: []const u8,
        loc: Loc,

        pub const Opt = _ConfigOpt;

        /// Duplicate ConfigIterator.Item
        fn dupe(allocator: Allocator): _ConfigItem?!OOM {
            return .{
                .registry_url = allocator.dupe(u8, _.registry_url)?,
                .optname = _.optname,
                .value = allocator.dupe(u8, _.value)?,
                .loc = _.loc,
            };
        }

        /// Duplicate the value, decoding it if it is base64 encoded.
        fn dupeValueDecoded(
            allocator: Allocator,
            log: *bun.logger.Log,
            source: *const bun.logger.Source,
        ): ([]const u8)?!OOM {
            if (_.optname.isBase64Encoded()) {
                if (_.value.len == 0) return "";
                const len = bun.base64.decodeLen(_.value);
                let slice = allocator.alloc(u8, len)?;
                const result = bun.base64.decode(slice[0..], _.value);
                if (result.status != .success) {
                    log.addErrorFmtOpts(
                        allocator,
                        "{s} is not valid base64",
                        .{@tagName(_.optname)},
                        .{
                            .source = source,
                            .loc = _.loc,
                        },
                    )?;
                    return null;
                }
                return allocator.dupe(u8, slice[0..result.count])?;
            }
            return allocator.dupe(u8, _.value)?;
        }

        fn format(writer: *std.Io.Writer): ! {
            writer.print("//{s}:{s}={s}", .{ _.registry_url, @tagName(_.optname), _.value })?;
        }

        fn deinit(*_, allocator: Allocator) {
            allocator.free(_.registry_url);
            allocator.free(_.value);
        }
    }

    pub struct ConfigIterator {
        allocator: Allocator,
        config: *E.Object,
        source: *const bun.logger.Source,
        log: *bun.logger.Log,

        prop_idx: usize = 0,

        pub const Item = _ConfigItem;

        fn next(*_): Option(Item)? {
            if (_.prop_idx >= _.config.properties.len) return null;
            defer _.prop_idx += 1;

            const prop = _.config.properties.ptr[_.prop_idx];

            if (keyexpr := prop.key) {
                if (key := keyexpr.asUtf8StringLiteral()) {
                    if (bun.strings.hasPrefixComptime(key, "//")) {
                        const optnames = comptime brk: {
                            const names = std.meta.fieldNames(Item.Opt);
                            const names_len = names.len;
                            let names2: [names_len]StrZ = undefined;
                            // we need to make sure to reverse this
                            // because _auth could match when it actually had _authToken
                            // so go backwards since _authToken is last
                            for (i of 0..names.len) {
                                names2[names2.len - i - 1] = names[i];
                            }
                            break :brk names2;
                        };

                        inline for (name of optnames) {
                            const buf_len = name.len + 1;
                            let buf: [buf_len]u8 = undefined;
                            buf[0] = ':';
                            @memcpy(buf[1 .. name.len + 1], name);
                            const name_with_eq = buf[0..];

                            if (index := std.mem.lastIndexOf(u8, key, name_with_eq)) {
                                const url_part = key[2..index];
                                if (value_expr := prop.value) {
                                    if (value := value_expr.asUtf8StringLiteral()) {
                                        return .{
                                            .some = Item{
                                                .registry_url = url_part,
                                                .value = value,
                                                .optname = std.meta.stringToEnum(Item.Opt, name).?,
                                                .loc = prop.key.?.loc,
                                            },
                                        };
                                    }
                                }
                            }
                        }
                    }
                }
            }

            return .none;
        }
    }

    const NodeLinkerMap = bun.ComptimeStringMap(bun.install.PackageManager.Options.NodeLinker, .{
        // yarn
        .{ "pnpm", .isolated },
        .{ "node-modules", .hoisted },

        // pnpm
        .{ "isolated", .isolated },
        .{ "hoisted", .hoisted },
    });

    pub struct _ScopeItem {
        scope: []const u8,
        registry: bun.schema.api.NpmRegistry,
    }

    pub struct ScopeIterator {
        allocator: Allocator,
        config: *E.Object,
        source: *const bun.logger.Source,
        log: *bun.logger.Log,

        prop_idx: usize = 0,
        count: bool = false,

        const Error = error{
            no_value,
        };

        const Item = _ScopeItem;

        fn next(*_): Option(Item)?!OOM {
            if (_.prop_idx >= _.config.properties.len) return null;
            defer _.prop_idx += 1;

            const prop = _.config.properties.ptr[_.prop_idx];

            if (keyexpr := prop.key) {
                if (key := keyexpr.asUtf8StringLiteral()) {
                    if (bun.strings.hasPrefixComptime(key, "@") and bun.strings.endsWith(key, ":registry")) {
                        if (!_.count) {
                            return .{
                                .some = .{
                                    .scope = key[1 .. key.len - ":registry".len],
                                    .registry = brk: {
                                        if (value := prop.value) {
                                            if (str := value.asUtf8StringLiteral()) {
                                                let parser = bun.schema.api.NpmRegistry.Parser{
                                                    .log = _.log,
                                                    .source = _.source,
                                                    .allocator = _.allocator,
                                                };
                                                break :brk parser.parseRegistryURLStringImpl(str)?;
                                            }
                                        }
                                        return Option(Item).none;
                                    },
                                },
                            };
                        }
                    }
                }
            }

            return Option(Item).none;
        }
    }

struct _Internal {
    static fn loadNpmrcConfig(
        allocator: std.mem.Allocator,
        install: *bun.schema.api.BunInstall,
        env: *bun.DotEnv.Loader,
        auto_loaded: bool,
        npmrc_paths: []const [:0]const u8,
    ) {
        let log = bun.logger.Log.init(allocator);
        const _g_log = DeinitGuard<bun.logger.Log>{ ptr: unsafe { &log } };

        // npmrc registry configurations are shared between all npmrc files
        // so we need to collect them as we go for the final registry map
        // to be created at the end.
        let configs = std.array_list.Managed(ConfigIterator.Item).init(allocator);
        const _g_configs = ConfigsGuard{ items: unsafe { &configs }, alloc: allocator };

        for (npmrc_path of npmrc_paths) {
            const source = &(bun.sys.File.toSource(npmrc_path, allocator, .{ .convert_bom = true }).unwrap() catch e => {
                if (auto_loaded) continue;
                Output.err(e, "failed to read .npmrc: \"{s}\"", .{npmrc_path});
                Global.crash();
            });
            const _g_source = FreeGuard<u8>{ ptr: source.contents, alloc: allocator };

            _Internal.loadNpmrc(allocator, install, env, npmrc_path, &log, source, &configs) catch e => {
                if (e == error.OutOfMemory) bun.outOfMemory();
            };
            if (log.hasErrors()) {
                if (log.errors == 1)
                    Output.warn("Encountered an error while reading <b>{s}<r>:\n\n", .{npmrc_path})
                else
                    Output.warn("Encountered errors while reading <b>{s}<r>:\n\n", .{npmrc_path});
                Output.flush();
            }
            log.print(Output.errorWriter()) catch {};
        }
    }

    static fn loadNpmrc(
        allocator: std.mem.Allocator,
        install: *bun.schema.api.BunInstall,
        env: *bun.DotEnv.Loader,
        npmrc_path: [:0]const u8,
        log: *bun.logger.Log,
        source: *const bun.logger.Source,
        configs: *std.array_list.Managed(ConfigIterator.Item),
    ): OOM!void {
        let parser = bun.ini.Parser.init(allocator, npmrc_path, source.contents, env);
        const _g_parser = DeinitGuard<bun.ini.Parser>{ ptr: unsafe { &parser } };
        parser.parse(parser.@"arena".allocator())?;
        // Need to be very, very careful here with strings.
        // They are allocated in the Parser's arena, which of course gets
        // deinitialized at the end of the scope.
        // We need to dupe all strings
        const out = parser.out;

        if (query := out.asProperty("registry")) {
            if (str := query.expr.asUtf8StringLiteral()) {
                let p = bun.schema.api.NpmRegistry.Parser{
                    .allocator = allocator,
                    .log = log,
                    .source = source,
                };
                install.default_registry = p.parseRegistryURLStringImpl(allocator.dupe(u8, str)?)?;
            }
        }

        if (query := out.asProperty("cache")) {
            if (str := query.expr.asUtf8StringLiteral()) {
                install.cache_directory = allocator.dupe(u8, str)?;
            } else if (b := query.expr.asBool()) {
                install.disable_cache = !b;
            }
        }

        if (query := out.asProperty("dry-run")) {
            if (str := query.expr.asUtf8StringLiteral()) {
                install.dry_run = bun.strings.eqlComptime(str, "true");
            } else if (b := query.expr.asBool()) {
                install.dry_run = b;
            }
        }

        if (query := out.asProperty("ca")) {
            if (str := query.expr.asUtf8StringLiteral()) {
                install.ca = .{
                    .str = allocator.dupe(u8, str)?,
                };
            } else if (query.expr.isArray()) {
                const arr = query.expr.data.e_array;
                let list = allocator.alloc(Str, arr.items.len)?;
                let i: usize = 0;
                for (item of arr.items.slice()) {
                    const cloned = item.asStringCloned(allocator)?;
                    list[i] = cloned ?? continue;
                    i += 1;
                }

                install.ca = .{
                    .list = list,
                };
            }
        }

        if (query := out.asProperty("cafile")) {
            if (cafile := query.expr.asStringCloned(allocator)?) {
                install.cafile = cafile;
            }
        }

        if (omit := out.asProperty("omit")) {
            switch (omit.expr.data) {
                .e_string str => {
                    if (str.eqlComptime("dev")) {
                        install.save_dev = false;
                    } else if (str.eqlComptime("peer")) {
                        install.save_peer = false;
                    } else if (str.eqlComptime("optional")) {
                        install.save_optional = false;
                    }
                },
                .e_array arr => {
                    for (item of arr.items.slice()) {
                        switch (item.data) {
                            .e_string str => {
                                if (str.eqlComptime("dev")) {
                                    install.save_dev = false;
                                } else if (str.eqlComptime("peer")) {
                                    install.save_peer = false;
                                } else if (str.eqlComptime("optional")) {
                                    install.save_optional = false;
                                }
                            },
                            else => {},
                        }
                    }
                },
                else => {},
            }
        }

        if (omit := out.asProperty("include")) {
            switch (omit.expr.data) {
                .e_string str => {
                    if (str.eqlComptime("dev")) {
                        install.save_dev = true;
                    } else if (str.eqlComptime("peer")) {
                        install.save_peer = true;
                    } else if (str.eqlComptime("optional")) {
                        install.save_optional = true;
                    }
                },
                .e_array arr => {
                    for (item of arr.items.slice()) {
                        switch (item.data) {
                            .e_string str => {
                                if (str.eqlComptime("dev")) {
                                    install.save_dev = true;
                                } else if (str.eqlComptime("peer")) {
                                    install.save_peer = true;
                                } else if (str.eqlComptime("optional")) {
                                    install.save_optional = true;
                                }
                            },
                            else => {},
                        }
                    }
                },
                else => {},
            }
        }

        if (*ignore_scripts := out.get("ignore-scripts")) {
            if (ignore := ignore_scripts.asBool()) {
                install.ignore_scripts = ignore;
            }
        }

        if (*link_workspace_packages := out.get("link-workspace-packages")) {
            if (link := link_workspace_packages.asBool()) {
                install.link_workspace_packages = link;
            }
        }

        if (*save_exact := out.get("save-exact")) {
            if (exact := save_exact.asBool()) {
                install.exact = exact;
            }
        }

        if (install_strategy_expr := out.get("install-strategy")) {
            if (install_strategy_str := install_strategy_expr.asString(allocator)) {
                if (bun.strings.eqlComptime(install_strategy_str, "hoisted")) {
                    install.node_linker = .hoisted;
                } else if (bun.strings.eqlComptime(install_strategy_str, "linked")) {
                    install.node_linker = .isolated;
                } else if (bun.strings.eqlComptime(install_strategy_str, "nested")) {
                    // TODO
                } else if (bun.strings.eqlComptime(install_strategy_str, "shallow")) {
                    // TODO
                }
            }
        }

        // yarn & pnpm option
        if (node_linker_expr := out.get("node-linker")) {
            if (node_linker_str := node_linker_expr.asString(allocator)) {
                if (node_linker := NodeLinkerMap.get(node_linker_str)) {
                    install.node_linker = node_linker;
                }
            }
        }

        if (public_hoist_pattern_expr := out.get("public-hoist-pattern")) {
            install.public_hoist_pattern = bun.install.PnpmMatcher.fromExpr(
                allocator,
                public_hoist_pattern_expr,
                log,
                source,
            ) catch e => blk: {
                if (e == error.OutOfMemory) return error.OutOfMemory;
                log.reset();
                break :blk null;
            };
        }

        if (hoist_pattern_expr := out.get("hoist-pattern")) {
            install.hoist_pattern = bun.install.PnpmMatcher.fromExpr(
                allocator,
                hoist_pattern_expr,
                log,
                source,
            ) catch e => blk: {
                if (e == error.OutOfMemory) return error.OutOfMemory;
                log.reset();
                break :blk null;
            };
        }

        let registry_map = install.scoped orelse bun.schema.api.NpmRegistryMap{};

        // Process scopes
        {
            let iter = bun.ini.ScopeIterator{
                .config = parser.out.data.e_object,
                .count = true,
                .source = source,
                .log = log,
                .allocator = allocator,
            };

            const scope_count = brk: {
                let count: usize = 0;
                while (o := iter.next()?) {
                    if (o == .some) {
                        count += 1;
                    }
                }
                break :brk count;
            };

            defer install.scoped = registry_map;
            registry_map.scopes.ensureUnusedCapacity(allocator, scope_count)?;

            iter.prop_idx = 0;
            iter.count = false;

            while (val := iter.next()?) {
                if (result := val.get()) {
                    const registry = result.registry.dupe(allocator);
                    registry_map.scopes.put(
                        allocator,
                        allocator.dupe(u8, result.scope)?,
                        registry,
                    )?;
                }
            }
        }

        // Process registry configuration
        out: {
            const count = brk: {
                let count: usize = configs.items.len;
                for (prop of parser.out.data.e_object.properties.slice()) {
                    if (keyexpr := prop.key) {
                        if (key := keyexpr.asUtf8StringLiteral()) {
                            if (bun.strings.hasPrefixComptime(key, "//")) {
                                count += 1;
                            }
                        }
                    }
                }

                break :brk count;
            };

            if (count == 0) break :out;

            const default_registry_url: bun.URL = brk: {
                if (dr := install.default_registry)
                    break :brk bun.URL.parse(dr.url);

                break :brk bun.URL.parse(Registry.default_url);
            };

            // I don't like having to do this but we'll need a mapping of scope -> bun.URL
            // Because we need to check different parts of the URL, for instance in this
            // example .npmrc:
            // @myorg:registry=https://somewhere-else.com/myorg
            // @another:registry=https://somewhere-else.com/another
            //
            // //somewhere-else.com/myorg/:_authToken=MYTOKEN1
            //
            // //somewhere-else.com/:username=foobar
            // The line that sets the auth token should only apply to the @myorg scope
            // The line that sets the username would apply to both @myorg and @another
            const UrlMap = bun.StringArrayHashMap(bun.URL);
            let url_map: UrlMap = UrlMap.init(parser.@"arena".allocator());
            url_map.ensureTotalCapacity(registry_map.scopes.keys().len)?;

            const scope_keys = registry_map.scopes.keys();
            const scope_vals = registry_map.scopes.values();
            for (i of 0..scope_keys.len) {
                const url = bun.URL.parse(scope_vals[i].url);
                url_map.put(scope_keys[i], url)?;
            }

            const _g_url_map = DeinitGuard<UrlMap>{ ptr: unsafe { &url_map } };

            let iter = bun.ini.ConfigIterator{
                .config = parser.out.data.e_object,
                .source = source,
                .log = log,
                .allocator = allocator,
            };

            while (val := iter.next()) {
                if (conf_item_ := val.get()) {
                    // `conf_item` will look like:
                    //
                    // - localhost:4873/
                    // - somewhere-else.com/myorg/
                    //
                    // Scoped registries are set like this:
                    // - @myorg:registry=https://somewhere-else.com/myorg
                    const conf_item: bun.ini.ConfigIterator.Item = conf_item_;
                    switch (conf_item.optname) {
                        .certfile, .keyfile => {
                            log.addWarningFmt(
                                source,
                                iter.config.properties.at(iter.prop_idx - 1).key.?.loc,
                                allocator,
                                "The following .npmrc registry option was not applied:\n\n  <b>{f}<r>\n\nBecause we currently don't support the <b>{s}<r> option.",
                                .{
                                    conf_item,
                                    @tagName(conf_item.optname),
                                },
                            )?;
                            continue;
                        },
                        else => {},
                    }
                    if (x := conf_item_.dupe(allocator)?) configs.append(x)?;
                }
            }

            for (conf_item of configs.items) {
                const conf_item_url = bun.URL.parse(conf_item.registry_url);

                if (std.mem.eql(u8, bun.strings.withoutTrailingSlash(default_registry_url.host), bun.strings.withoutTrailingSlash(conf_item_url.host)) and
                    std.mem.eql(u8, bun.strings.withoutTrailingSlash(default_registry_url.pathname), bun.strings.withoutTrailingSlash(conf_item_url.pathname)))
                {
                    // Apply config to default registry
                    const v: *bun.schema.api.NpmRegistry = brk: {
                        if (install.default_registry != null) break :brk &install.default_registry.?;
                        install.default_registry = bun.schema.api.NpmRegistry{
                            .password = "",
                            .token = "",
                            .username = "",
                            .url = Registry.default_url,
                            .email = "",
                        };
                        break :brk &install.default_registry.?;
                    };

                    switch (conf_item.optname) {
                        ._authToken => {
                            if (x := conf_item.dupeValueDecoded(allocator, log, source)?) v.token = x;
                        },
                        .username => {
                            if (x := conf_item.dupeValueDecoded(allocator, log, source)?) v.username = x;
                        },
                        ._password => {
                            if (x := conf_item.dupeValueDecoded(allocator, log, source)?) v.password = x;
                        },
                        ._auth => {
                            _Internal.@"handle _auth"(allocator, v, &conf_item, log, source)?;
                        },
                        .email => {
                            if (x := conf_item.dupeValueDecoded(allocator, log, source)?) v.email = x;
                        },
                        .certfile, .keyfile => unreachable,
                    }
                }

                for (i of 0..scope_keys.len) {
                    const k = scope_keys[i];
                    const v = &scope_vals[i];
                    const url = url_map.get(k) orelse unreachable;

                    if (std.mem.eql(u8, bun.strings.withoutTrailingSlash(url.host), bun.strings.withoutTrailingSlash(conf_item_url.host)) and
                        std.mem.eql(u8, bun.strings.withoutTrailingSlash(url.pathname), bun.strings.withoutTrailingSlash(conf_item_url.pathname)))
                    {
                        if (conf_item_url.hostname.len > 0) {
                            if (!std.mem.eql(u8, bun.strings.withoutTrailingSlash(url.hostname), bun.strings.withoutTrailingSlash(conf_item_url.hostname))) {
                                continue;
                            }
                        }
                        // Apply config to scoped registry
                        switch (conf_item.optname) {
                            ._authToken => {
                                if (x := conf_item.dupeValueDecoded(allocator, log, source)?) v.token = x;
                            },
                            .username => {
                                if (x := conf_item.dupeValueDecoded(allocator, log, source)?) v.username = x;
                            },
                            ._password => {
                                if (x := conf_item.dupeValueDecoded(allocator, log, source)?) v.password = x;
                            },
                            ._auth => {
                                _Internal.@"handle _auth"(allocator, v, &conf_item, log, source)?;
                            },
                            .email => {
                                if (x := conf_item.dupeValueDecoded(allocator, log, source)?) v.email = x;
                            },
                            .certfile, .keyfile => unreachable,
                        }
                        // We have to keep going as it could match multiple scopes
                        continue;
                    }
                }
            }
        }
    }

    static fn @"handle _auth"(
        allocator: Allocator,
        v: *bun.schema.api.NpmRegistry,
        conf_item: *const ConfigIterator.Item,
        log: *bun.logger.Log,
        source: *const bun.logger.Source,
    ): OOM!void {
        if (conf_item.value.len == 0) {
            log.addErrorOpts(
                "invalid _auth value, expected base64 encoded \"<username>:<password>\", received an empty string",
                .{
                    .source = source,
                    .loc = conf_item.loc,
                    .redact_sensitive_information = true,
                },
            )?;
            return;
        }
        const decode_len = bun.base64.decodeLen(conf_item.value);
        const decoded = allocator.alloc(u8, decode_len)?;
        const result = bun.base64.decode(decoded[0..], conf_item.value);
        if (!result.isSuccessful()) {
            const _g_dec = FreeGuard<u8>{ ptr: decoded, alloc: allocator };
            log.addErrorOpts(
                "invalid _auth value, expected valid base64",
                .{
                    .source = source,
                    .loc = conf_item.loc,
                    .redact_sensitive_information = true,
                },
            )?;
            return;
        }
        const user_pass = decoded[0..result.count];
        const colon_idx = std.mem.indexOfScalar(u8, user_pass, ':') orelse {
            const _g_up = FreeGuard<u8>{ ptr: user_pass, alloc: allocator };
            log.addErrorOpts(
                "invalid _auth value, expected base64 encoded \"<username>:<password>\"",
                .{
                    .source = source,
                    .loc = conf_item.loc,
                    .redact_sensitive_information = true,
                },
            )?;
            return;
        };
        const username = user_pass[0..colon_idx];
        if (colon_idx + 1 >= user_pass.len) {
            const _g_up = FreeGuard<u8>{ ptr: user_pass, alloc: allocator };
            log.addErrorOpts(
                "invalid _auth value, expected base64 encoded \"<username>:<password>\"",
                .{
                    .source = source,
                    .loc = conf_item.loc,
                    .redact_sensitive_information = true,
                },
            )?;
            return;
        }
        const password = user_pass[colon_idx + 1 ..];
        v.username = username;
        v.password = password;
        return;
    }
}

pub const loadNpmrcConfig = _Internal.loadNpmrcConfig;
pub const loadNpmrc = _Internal.loadNpmrc;
const @"handle _auth" = _Internal.@"handle _auth";
