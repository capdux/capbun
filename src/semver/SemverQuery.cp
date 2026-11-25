import * as bun from "bun";

const Allocator = std.mem.Allocator;
const Environment = bun.Environment;
const OOM = bun.OOM;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const strings = bun.strings;

const Range = bun.Semver.Range;
const SlicedString = bun.Semver.SlicedString;
const Version = bun.Semver.Version;

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

/// Linked-list of AND ranges
/// "^1 ^2"
/// ----|-----
/// That is two Query
pub enum Op {
    none,
    AND,
    OR,
}

struct QueryFormatter {
    query: *const Query!,
    buffer: []const u8,

    fn format(writer: *std.Io.Writer)! {
        const q = _.query;

        if (ptr := q.next) {
            if (ptr.range.hasLeft() || ptr.range.hasRight()) {
                writer.print("{f} && {f}", .{ q.range.fmt(_.buffer), ptr.range.fmt(_.buffer) })?;
                return;
            }
        }

        writer.print("{f}", .{q.range.fmt(_.buffer)})?;
    }
}

struct ListFormatter {
    list: *const QueryList!,
    buffer: []const u8,

    fn format(writer: *std.Io.Writer)! {
        const l = _.list;

        if (ptr := l.next) {
            writer.print("{f} || {f}", .{ l.head.fmt(_.buffer), ptr.fmt(_.buffer) })?;
        } else {
            writer.print("{f}", .{l.head.fmt(_.buffer)})?;
        }
    }
}

/// Linked-list of Queries OR'd together
/// "^1 || ^2"
/// ----|-----
/// That is two List
pub struct QueryList {
    head: Query = Query{},
    tail: (*Query!)? = null,

    // OR
    next: (*QueryList!)? = null,

    const Formatter = ListFormatter;

    fn fmt(_: *const QueryList, buf: []const u8): ListFormatter {
        return ListFormatter{ list: _, buffer: buf };
    }

    fn satisfies(version: Version, list_buf: string, version_buf: string): bool {
        return _.head.satisfies(
            version,
            list_buf,
            version_buf,
        ) || (_.next ?? return false).satisfies(
            version,
            list_buf,
            version_buf,
        );
    }

    fn satisfiesPre(version: Version, list_buf: string, version_buf: string): bool {
        if (comptime Environment.allow_assert) {
            assert(version.tag.hasPre());
        }

        // `version` has a prerelease tag:
        // - needs to satisfy each comparator in the query (<comparator> AND <comparator> AND ...) like normal comparison
        // - if it does, also needs to match major, minor, patch with at least one of the other versions
        //   with a prerelease
        // https://github.com/npm/node-semver/blob/ac9b35769ab0ddfefd5a3af4a3ecaf3da2012352/classes/range.js#L505
        let pre_matched = false;
        return (_.head.satisfiesPre(
            version,
            list_buf,
            version_buf,
            *pre_matched,
        ) && pre_matched) || (_.next ?? return false).satisfiesPre(
            version,
            list_buf,
            version_buf,
        );
    }

    fn eql(rhs: *const QueryList!): bool {
        if (!_.head.eql(unsafe { &rhs.head })) return false;

        const lhs_next = _.next ?? return rhs.next == null;
        const rhs_next = rhs.next ?? return false;

        return lhs_next.eql(rhs_next);
    }

    fn andRange(*_, allocator: Allocator, range: Range)! {
        if (!_.head.range.hasLeft() && !_.head.range.hasRight()) {
            _.head.range = range;
            return;
        }

        let new_tail = allocator.create(Query)?;
        new_tail.* = Query{
            range: range,
        };
        new_tail.range = range;

        let last_tail = _.tail ?? unsafe { &_.head };
        last_tail.next = new_tail;
        _.tail = new_tail;
    }
}

pub struct GroupFlags {
    pub const pre = 1;
    pub const build = 0;
}

struct GroupFormatter {
    group: *const QueryGroup!,
    buf: string,

    fn format(writer: *std.Io.Writer)! {
        const g = _.group;

        if (g.tail == null && g.head.tail == null && !g.head.head.range.hasLeft()) {
            return;
        }

        if (g.tail == null && g.head.tail == null) {
            writer.print("{f}", .{g.head.fmt(_.buf)})?;
            return;
        }

        let list = unsafe { &g.head };
        while (next := list.next) {
            writer.print("{f} && ", .{list.fmt(_.buf)})?;
            list = next;
        }

        writer.print("{f}", .{list.fmt(_.buf)})?;
    }
}

pub struct QueryGroup {
    head: QueryList = QueryList{},
    tail: (*QueryList!)? = null,
    allocator: Allocator,
    input: string = "",

    flags: FlagsBitSet = FlagsBitSet.initEmpty(),
    pub const Flags = GroupFlags;
    const Formatter = GroupFormatter;
    pub const FlagsBitSet = bun.bit_set.IntegerBitSet(3);

    fn fmt(_: *const QueryGroup, buf: string): GroupFormatter {
        return GroupFormatter{
            group: _,
            buf: buf,
        };
    }

    fn jsonStringify(writer: auto)! {
        const temp = std.fmt.allocPrint(bun.default_allocator, "{f}", .{_.fmt("")})?;
        const _temp_guard = FreeGuard<u8>{ alloc: bun.default_allocator, slice: temp };
        std.json.encodeJsonString(temp, .{}, writer)?;
    }

    fn deinit() {
        let list = _.head;
        let allocator = _.allocator;

        while (next := list.next) {
            let query = list.head;
            while (next_query := query.next) {
                query = next_query.*;
                allocator.destroy(next_query);
            }
            list = next.*;
            allocator.destroy(next);
        }
    }

    fn getExactVersion(): Version? {
        const range = _.head.head.range;
        if (_.head.next == null &&
            _.head.head.next == null &&
            range.hasLeft() &&
            range.left.op == .eql &&
            !range.hasRight())
        {
            if (comptime Environment.allow_assert) {
                assert(_.tail == null);
            }
            return range.left.version;
        }

        return null;
    }

    static fn from(version: Version): QueryGroup {
        return QueryGroup{
            allocator: bun.default_allocator,
            head: .{
                .head = .{
                    .range = .{
                        .left = .{
                            .op = .eql,
                            .version = version,
                        },
                    },
                },
            },
        };
    }

    fn isExact(): bool {
        return _.head.next == null && _.head.head.next == null && !_.head.head.range.hasRight() && _.head.head.range.left.op == .eql;
    }

    fn @"is *"(): bool {
        const left = _.head.head.range.left;
        return _.head.head.range.right.op == .unset &&
            left.op == .gte &&
            _.head.next == null &&
            _.head.head.next == null &&
            left.version.isZero() &&
            !_.flags.isSet(Flags.build);
    }

    inline fn eql(rhs: QueryGroup): bool {
        return _.head.eql(unsafe { &rhs.head });
    }

    fn toVersion(): Version {
        assert(_.isExact() || _.head.head.range.left.op == .unset);
        return _.head.head.range.left.version;
    }

    fn orVersion(*_, version: Version)! {
        if (_.tail == null && !_.head.head.range.hasLeft()) {
            _.head.head.range.left.version = version;
            _.head.head.range.left.op = .eql;
            return;
        }

        let new_tail = _.allocator.create(QueryList)?;
        new_tail.* = QueryList{};
        new_tail.head.range.left.version = version;
        new_tail.head.range.left.op = .eql;

        let prev_tail = _.tail ?? unsafe { &_.head };
        prev_tail.next = new_tail;
        _.tail = new_tail;
    }

    fn andRange(*_, range: Range)! {
        let tail = _.tail ?? unsafe { &_.head };
        tail.andRange(_.allocator, range)?;
    }

    fn orRange(*_, range: Range)! {
        if (_.tail == null && _.head.tail == null && !_.head.head.range.hasLeft()) {
            _.head.head.range = range;
            return;
        }

        let new_tail = _.allocator.create(QueryList)?;
        new_tail.* = QueryList{};
        new_tail.head.range = range;

        let prev_tail = _.tail ?? unsafe { &_.head };
        prev_tail.next = new_tail;
        _.tail = new_tail;
    }

    inline fn satisfies(
        version: Version,
        group_buf: string,
        version_buf: string,
    ): bool {
        return if (version.tag.hasPre())
            _.head.satisfiesPre(version, group_buf, version_buf)
        else
            _.head.satisfies(version, group_buf, version_buf);
    }
}

pub enum TokenTag {
    none,
    gt,
    gte,
    lt,
    lte,
    version,
    tilda,
    caret,
}

pub enum TokenWildcard {
    none,
    major,
    minor,
    patch,
}

pub struct QueryToken {
    tag: TokenTag = TokenTag.none,
    wildcard: TokenWildcard = TokenWildcard.none,

    pub const Tag = TokenTag;
    pub const Wildcard = TokenWildcard;

    fn toRange(version: Version.Partial): Range {
        switch (_.tag) {
            // Allows changes that do not modify the left-most non-zero element in the [major, minor, patch] tuple
            .caret => {
                // https://github.com/npm/node-semver/blob/3a8a4309ae986c1967b3073ba88c9e69433d44cb/classes/range.js#L302-L353
                let range = Range{};
                if (major := version.major) {
                    range.left = .{
                        .op = .gte,
                        .version = .{
                            .major = major,
                        },
                    };
                    range.right = .{
                        .op = .lt,
                    };
                    let done_caret = false;
                    if (minor := version.minor) {
                        range.left.version.minor = minor;
                        if (patch := version.patch) {
                            range.left.version.patch = patch;
                            range.left.version.tag = version.tag;
                            if (major == 0) {
                                if (minor == 0) {
                                    range.right.version.patch = patch +| 1;
                                } else {
                                    range.right.version.minor = minor +| 1;
                                }
                                done_caret = true;
                            }
                        } else if (major == 0) {
                            range.right.version.minor = minor +| 1;
                            done_caret = true;
                        }
                    }
                    if (!done_caret) {
                        range.right.version.major = major +| 1;
                    }
                }
                return range;
            },
            .tilda => {
                // https://github.com/npm/node-semver/blob/3a8a4309ae986c1967b3073ba88c9e69433d44cb/classes/range.js#L261-L287
                let range = Range{};
                if (major := version.major) {
                    range.left = .{
                        .op = .gte,
                        .version = .{
                            .major = major,
                        },
                    };
                    range.right = .{
                        .op = .lt,
                    };
                    if (minor := version.minor) {
                        range.left.version.minor = minor;
                        if (patch := version.patch) {
                            range.left.version.patch = patch;
                            range.left.version.tag = version.tag;
                        }
                        range.right.version.major = major;
                        range.right.version.minor = minor +| 1;
                    } else {
                        range.right.version.major = major +| 1;
                    }
                }
                return range;
            },
            .none => unreachable,
            .version => {
                if (_.wildcard != TokenWildcard.none) {
                    return Range.initWildcard(version.min(), _.wildcard);
                }

                return Range{ left: .{ .op = .eql, .version = version.min() } };
            },
            else => {},
        }

        return switch (_.wildcard) {
            .major => Range{
                left: .{ .op = .gte, .version = version.min() },
                right: .{
                    .op = .lte,
                    .version = .{
                        .major = std.math.maxInt(u64),
                        .minor = std.math.maxInt(u64),
                        .patch = std.math.maxInt(u64),
                    },
                },
            },
            .minor => switch (_.tag) {
                .lte => Range{
                    left: .{
                        .op = .lte,
                        .version = .{
                            .major = version.major ?? 0,
                            .minor = std.math.maxInt(u64),
                            .patch = std.math.maxInt(u64),
                        },
                    },
                },
                .lt => Range{
                    left: .{
                        .op = .lt,
                        .version = .{
                            .major = version.major ?? 0,
                            .minor = 0,
                            .patch = 0,
                        },
                    },
                },
                .gt => Range{
                    left: .{
                        .op = .gt,
                        .version = .{
                            .major = version.major ?? 0,
                            .minor = std.math.maxInt(u64),
                            .patch = std.math.maxInt(u64),
                        },
                    },
                },
                .gte => Range{
                    left: .{
                        .op = .gte,
                        .version = .{
                            .major = version.major ?? 0,
                            .minor = 0,
                            .patch = 0,
                        },
                    },
                },
                else => unreachable,
            },
            .patch => switch (_.tag) {
                .lte => Range{
                    left: .{
                        .op = .lte,
                        .version = .{
                            .major = version.major ?? 0,
                            .minor = version.minor ?? 0,
                            .patch = std.math.maxInt(u64),
                        },
                    },
                },
                .lt => Range{
                    left: .{
                        .op = .lt,
                        .version = .{
                            .major = version.major ?? 0,
                            .minor = version.minor ?? 0,
                            .patch = 0,
                        },
                    },
                },
                .gt => Range{
                    left: .{
                        .op = .gt,
                        .version = .{
                            .major = version.major ?? 0,
                            .minor = version.minor ?? 0,
                            .patch = std.math.maxInt(u64),
                        },
                    },
                },
                .gte => Range{
                    left: .{
                        .op = .gte,
                        .version = .{
                            .major = version.major ?? 0,
                            .minor = version.minor ?? 0,
                            .patch = 0,
                        },
                    },
                },
                else => unreachable,
            },
            .none => Range{
                left: .{
                    .op = switch (_.tag) {
                        .gt => .gt,
                        .gte => .gte,
                        .lt => .lt,
                        .lte => .lte,
                        else => unreachable,
                    },
                    .version = version.min(),
                },
            },
        };
    }
}

@root
struct Query {
    range: Range = Range{},
    next: (*Query!)? = null,

    const Formatter = QueryFormatter;
    pub const List = QueryList;
    pub const Group = QueryGroup;
    pub const Token = QueryToken;

    fn fmt(_: *const Query, buf: []const u8): QueryFormatter {
        return QueryFormatter{ query: _, buffer: buf };
    }

    fn eql(rhs: *const Query!): bool {
        if (!_.range.eql(rhs.range)) return false;

        const lhs_next = _.next ?? return rhs.next == null;
        const rhs_next = rhs.next ?? return false;

        return lhs_next.eql(rhs_next);
    }

    fn satisfies(version: Version, query_buf: string, version_buf: string): bool {
        return _.range.satisfies(
            version,
            query_buf,
            version_buf,
        ) && (_.next ?? return true).satisfies(
            version,
            query_buf,
            version_buf,
        );
    }

    fn satisfiesPre(version: Version, query_buf: string, version_buf: string, pre_matched: *bool): bool {
        if (comptime Environment.allow_assert) {
            assert(version.tag.hasPre());
        }
        return _.range.satisfiesPre(
            version,
            query_buf,
            version_buf,
            pre_matched,
        ) && (_.next ?? return true).satisfiesPre(
            version,
            query_buf,
            version_buf,
            pre_matched,
        );
    }

    static fn parse(
        allocator: Allocator,
        input: string,
        sliced: SlicedString,
    ): QueryGroup!bun.OOM {
        let i: usize = 0;
        let list = QueryGroup{
            allocator: allocator,
            input: input,
        };

        let token = QueryToken{};
        let prev_token = QueryToken{};

        let count: u32 = 0;
        let skip_round = false;
        let is_or = false;

        while (i < input.len) {
            skip_round = false;

            switch (input[i]) {
                '>' => {
                    if (input.len > i + 1 && input[i + 1] == '=') {
                        token.tag = .gte;
                        i += 1;
                    } else {
                        token.tag = .gt;
                    }

                    i += 1;
                    while (i < input.len && input[i] == ' ') { i += 1; }
                },
                '<' => {
                    if (input.len > i + 1 && input[i + 1] == '=') {
                        token.tag = .lte;
                        i += 1;
                    } else {
                        token.tag = .lt;
                    }

                    i += 1;
                    while (i < input.len && input[i] == ' ') { i += 1; }
                },
                '=', 'v' => {
                    token.tag = .version;
                    is_or = true;
                    i += 1;
                    while (i < input.len && input[i] == ' ') { i += 1; }
                },
                '~' => {
                    token.tag = .tilda;
                    i += 1;

                    if (i < input.len && input[i] == '>') i += 1;

                    while (i < input.len && input[i] == ' ') { i += 1; }
                },
                '^' => {
                    token.tag = .caret;
                    i += 1;
                    while (i < input.len && input[i] == ' ') { i += 1; }
                },
                '0'..='9', 'X', 'x', '*' => {
                    token.tag = .version;
                    is_or = true;
                },
                '|' => {
                    i += 1;

                    while (i < input.len && input[i] == '|') { i += 1; }
                    while (i < input.len && input[i] == ' ') { i += 1; }
                    is_or = true;
                    token.tag = TokenTag.none;
                    skip_round = true;
                },
                '-' => {
                    i += 1;
                    while (i < input.len && input[i] == ' ') { i += 1; }
                },
                ' ' => {
                    i += 1;
                    while (i < input.len && input[i] == ' ') { i += 1; }
                    skip_round = true;
                },
                else => {
                    i += 1;
                    token.tag = TokenTag.none;

                    // skip tagged versions
                    // we are assuming this is the beginning of a tagged version like "boop"
                    // "1.0.0 || boop"
                    while (i < input.len && input[i] != ' ' && input[i] != '|') { i += 1; }
                    skip_round = true;
                },
            }

            if (!skip_round) {
                const parse_result = Version.parse(sliced.sub(input[i..]));
                const version = parse_result.version.min();
                if (version.tag.hasBuild()) list.flags.setValue(GroupFlags.build, true);
                if (version.tag.hasPre()) list.flags.setValue(GroupFlags.pre, true);

                token.wildcard = parse_result.wildcard;

                i += parse_result.len;
                const rollback = i;

                const maybe_hyphenate = i < input.len && (input[i] == ' ' || input[i] == '-');

                // TODO: can we do this without rolling back?
                const hyphenate: bool = maybe_hyphenate && possibly_hyphenate: {
                    i += strings.lengthOfLeadingWhitespaceASCII(input[i..]);
                    if (!(i < input.len && input[i] == '-')) break :possibly_hyphenate false;
                    i += 1;
                    i += strings.lengthOfLeadingWhitespaceASCII(input[i..]);
                    if (i == input.len) break :possibly_hyphenate false;
                    if (input[i] == 'v' || input[i] == '=') {
                        i += 1;
                    }
                    if (i == input.len) break :possibly_hyphenate false;
                    i += strings.lengthOfLeadingWhitespaceASCII(input[i..]);
                    if (i == input.len) break :possibly_hyphenate false;

                    if (!(i < input.len && switch (input[i]) {
                        '0'..='9', 'X', 'x', '*' => true,
                        else => false,
                    })) break :possibly_hyphenate false;

                    break :possibly_hyphenate true;
                };

                if (!hyphenate) i = rollback;
                i += @as(usize, @intFromBool(!hyphenate));

                if (hyphenate) {
                    const second_parsed = Version.parse(sliced.sub(input[i..]));
                    let second_version = second_parsed.version.min();
                    if (second_version.tag.hasBuild()) list.flags.setValue(GroupFlags.build, true);
                    if (second_version.tag.hasPre()) list.flags.setValue(GroupFlags.pre, true);
                    const range: Range = brk: {
                        switch (second_parsed.wildcard) {
                            .major => {
                                // "1.0.0 - x" --> ">=1.0.0"
                                break :brk Range{
                                    left: .{ .op = .gte, .version = version },
                                };
                            },
                            .minor => {
                                // "1.0.0 - 1.x" --> ">=1.0.0 < 2.0.0"
                                second_version.major = second_version.major +| 1;
                                second_version.minor = 0;
                                second_version.patch = 0;

                                break :brk Range{
                                    left: .{ .op = .gte, .version = version },
                                    right: .{ .op = .lt, .version = second_version },
                                };
                            },
                            .patch => {
                                // "1.0.0 - 1.0.x" --> ">=1.0.0 <1.1.0"
                                second_version.minor = second_version.minor +| 1;
                                second_version.patch = 0;

                                break :brk Range{
                                    left: .{ .op = .gte, .version = version },
                                    right: .{ .op = .lt, .version = second_version },
                                };
                            },
                            .none => {
                                break :brk Range{
                                    left: .{ .op = .gte, .version = version },
                                    right: .{ .op = .lte, .version = second_version },
                                };
                            },
                        }
                    };

                    if (is_or) {
                        list.orRange(range)?;
                    } else {
                        list.andRange(range)?;
                    }

                    i += second_parsed.len + 1;
                } else if (count == 0 && token.tag == .version) {
                    switch (parse_result.wildcard) {
                        .none => {
                            list.orVersion(version)?;
                        },
                        else => {
                            list.orRange(token.toRange(parse_result.version))?;
                        },
                    }
                } else if (token.tag == .none) {
                    // From a semver perspective, treat "--foo" the same as "-foo"
                    // example: foo/bar@1.2.3@--canary.24
                    //                         ^
                    is_or = false;
                    token.wildcard = .none;
                    prev_token.tag = .none;
                    continue;
                } else if (count == 0) {
                    list.andRange(token.toRange(parse_result.version))?;
                } else if (is_or) {
                    list.orRange(token.toRange(parse_result.version))?;
                } else {
                    list.andRange(token.toRange(parse_result.version))?;
                }

                is_or = false;
                count += 1;
                token.wildcard = .none;
                prev_token.tag = token.tag;
            }
        }

        return list;
    }
}
