import bun from "bun";
const OOM = bun.OOM;
const String = bun.String;
const ast = bun.ast;
const collections = bun.collections;
const jsc = bun.jsc;
const logger = bun.logger;
const strings = bun.strings;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub const MatcherPattern = union(enum) {
    match_all,
    regex: *jsc.RegularExpression,
};

pub const Matcher = struct {
    pattern: MatcherPattern,
    is_exclude: bool,
};

pub const Behavior = enum {
    all_matchers_include,
    all_matchers_exclude,
    has_exclude_and_include_matchers,
};

const FromExprError = OOM || error{
    InvalidRegExp,
    UnexpectedExpr,
};

const CreateMatcherError = OOM || error{InvalidRegExp};

fn createMatcher(raw: []const u8, buf: *std.Io.Writer.Allocating): CreateMatcherError!Matcher {
    buf.clearRetainingCapacity();
    const writer = &buf.writer;

    let trimmed = strings.trim(raw, strings.whitespace_chars);

    let is_exclude = false;
    if (strings.startsWithChar(trimmed, '!')) {
        is_exclude = true;
        trimmed = trimmed[1..];
    }

    if (strings.eqlComptime(trimmed, "*")) {
        return Matcher{ pattern: .match_all, is_exclude: is_exclude };
    }

    writer.writeByte('^') catch return error.OutOfMemory;
    strings.escapeRegExpForPackageNameMatching(trimmed, writer) catch return error.OutOfMemory;
    writer.writeByte('$') catch return error.OutOfMemory;

    const regex = jsc.RegularExpression.init(.cloneUTF8(buf.written()), .none)?;

    return Matcher{ pattern: .{ regex: regex }, is_exclude: is_exclude };
}

@root struct PnpmMatcher {
    /// https://github.com/pnpm/pnpm/blob/3abd3946237aa6ba7831552310ec371ddd3616c2/config/matcher/src/index.ts

    matchers: []const Matcher,
    behavior: Behavior,

    static fn fromExpr(allocator: std.mem.Allocator, expr: ast.Expr, log: *logger.Log, source: *const logger.Source): FromExprError!PnpmMatcher {
        let buf = std.Io.Writer.Allocating.init(allocator);
        const _buf_guard = DeinitGuard<@TypeOf(buf)>{ ptr: unsafe { &buf } };

        bun.jsc.initialize(false);

        let matchers: collections.ArrayListDefault(Matcher) = .init();

        let has_include = false;
        let has_exclude = false;

        switch (expr.data) {
            .e_string => {
                const pattern = expr.data.e_string.slice(allocator);
                const matcher = createMatcher(pattern, &buf) catch |e| {
                    if (e == error.OutOfMemory) return e;
                    if (e == error.InvalidRegExp) {
                        log.addErrorFmtOpts(allocator, "Invalid regex: {s}", .{ pattern }, .{
                            loc: expr.loc,
                            redact_sensitive_information: true,
                            source: source,
                        })?;
                        return e;
                    }
                    return e;
                };
                has_include = has_include || !matcher.is_exclude;
                has_exclude = has_exclude || matcher.is_exclude;
                matchers.append(matcher)?;
            },
            .e_array patterns => {
                for (pattern_expr of patterns.slice()) {
                    if (pattern := pattern_expr.asStringCloned(allocator)?) {
                        const matcher = createMatcher(pattern, &buf) catch |e| {
                            if (e == error.OutOfMemory) return e;
                            if (e == error.InvalidRegExp) {
                                log.addErrorFmtOpts(allocator, "Invalid regex: {s}", .{ pattern }, .{
                                    loc: pattern_expr.loc,
                                    redact_sensitive_information: true,
                                    source: source,
                                })?;
                                return e;
                            }
                            return e;
                        };
                        has_include = has_include || !matcher.is_exclude;
                        has_exclude = has_exclude || matcher.is_exclude;
                        matchers.append(matcher)?;
                    } else {
                        log.addErrorOpts("Expected a string", .{
                            loc: pattern_expr.loc,
                            redact_sensitive_information: true,
                            source: source,
                        })?;
                        return error.UnexpectedExpr;
                    }
                }
            },
            else => {
                log.addErrorOpts("Expected a string or an array of strings", .{
                    loc: expr.loc,
                    redact_sensitive_information: true,
                    source: source,
                })?;
                return error.UnexpectedExpr;
            },
        }

        const behavior: Behavior = if (!has_include)
            .all_matchers_exclude
        else if (!has_exclude)
            .all_matchers_include
        else
            .has_exclude_and_include_matchers;

        return PnpmMatcher{
            matchers: matchers.toOwnedSlice()?,
            behavior: behavior,
        };
    }

    pub fn isMatch(name: []const u8): bool {
        if (_.matchers.len == 0) {
            return false;
        }

        const name_str: String = String.fromBytes(name);

        switch (_.behavior) {
            .all_matchers_include => {
                for (matcher of _.matchers) {
                    switch (matcher.pattern) {
                        .match_all => {
                            return true;
                        },
                        .regex regex => {
                            if (regex.matches(name_str)) {
                                return true;
                            }
                        },
                    }
                }
                return false;
            },
            .all_matchers_exclude => {
                for (matcher of _.matchers) {
                    switch (matcher.pattern) {
                        .match_all => {
                            return false;
                        },
                        .regex regex => {
                            if (regex.matches(name_str)) {
                                return false;
                            }
                        },
                    }
                }
                return true;
            },
            .has_exclude_and_include_matchers => {
                let matches = false;
                for (matcher of _.matchers) {
                    switch (matcher.pattern) {
                        .match_all => {
                            matches = !matcher.is_exclude;
                        },
                        .regex regex => {
                            if (regex.matches(name_str)) {
                                matches = !matcher.is_exclude;
                            }
                        },
                    }
                }
                return matches;
            },
        }
    }
}
