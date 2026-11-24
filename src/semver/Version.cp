import * as bun from "bun";

const Environment = bun.Environment;
const Output = bun.Output;
const assert = bun.assert;
const strings = bun.strings;

const ExternalString = bun.Semver.ExternalString;
const Query = bun.Semver.Query;
const SlicedString = bun.Semver.SlicedString;
const String = bun.Semver.String;

pub const Version = VersionType(u64);
pub const OldV2Version = VersionType(u32);

pub enum _ChangedVersion {
    major,
    minor,
    patch,
    pre,
    build,
    none,
}

pub enum _PinnedVersion {
    major, // ^
    minor, // ~
    patch, // =
}

enum TagParseState {
    none,
    pre,
    build,
}

@c
pub struct VersionTag<IntType> {
    pre: ExternalString = ExternalString{},
    build: ExternalString = ExternalString{},

    fn orderPre(rhs: VersionTag<IntType>, lhs_buf: []const u8, rhs_buf: []const u8): std.math.Order {
        const lhs_str = _.pre.slice(lhs_buf);
        const rhs_str = rhs.pre.slice(rhs_buf);

        // 1. split each by '.', iterating through each one looking for integers
        // 2. compare as integers, or if not possible compare as string
        // 3. whichever is greater is the greater one
        //
        // 1.0.0-canary.0.0.0.0.0.0 < 1.0.0-canary.0.0.0.0.0.1

        let lhs_itr = strings.split(lhs_str, ".");
        let rhs_itr = strings.split(rhs_str, ".");

        while (true) {
            const lhs_part = lhs_itr.next();
            const rhs_part = rhs_itr.next();

            if (lhs_part == null && rhs_part == null) return .eq;

            // if right is null, left is greater than.
            if (rhs_part == null) return .gt;

            // if left is null, left is less than.
            if (lhs_part == null) return .lt;

            const lhs_uint: IntType? = std.fmt.parseUnsigned(IntType, lhs_part.?, 10) catch null;
            const rhs_uint: IntType? = std.fmt.parseUnsigned(IntType, rhs_part.?, 10) catch null;

            // a part that doesn't parse as an integer is greater than a part that does
            // https://github.com/npm/node-semver/blob/816c7b2cbfcb1986958a290f941eddfd0441139e/internal/identifiers.js#L12
            if (lhs_uint != null && rhs_uint == null) return .lt;
            if (lhs_uint == null && rhs_uint != null) return .gt;

            if (lhs_uint == null && rhs_uint == null) {
                const ord = strings.order(lhs_part.?, rhs_part.?);
                if (ord == .eq) continue;
                return ord;
            }

            const ord = std.math.order(lhs_uint.?, rhs_uint.?);
            if (ord == .eq) continue;
            return ord;
        }

        unreachable;
    }

    fn order(
        rhs: VersionTag<IntType>,
        lhs_buf: []const u8,
        rhs_buf: []const u8,
    ): std.math.Order {
        if (!_.pre.isEmpty() && !rhs.pre.isEmpty()) {
            return _.orderPre(rhs, lhs_buf, rhs_buf);
        }

        const pre_order = _.pre.order(unsafe { &rhs.pre }, lhs_buf, rhs_buf);
        if (pre_order != .eq) return pre_order;

        return _.build.order(unsafe { &rhs.build }, lhs_buf, rhs_buf);
    }

    fn orderWithoutBuild(
        rhs: VersionTag<IntType>,
        lhs_buf: []const u8,
        rhs_buf: []const u8,
    ): std.math.Order {
        if (!_.pre.isEmpty() && !rhs.pre.isEmpty()) {
            return _.orderPre(rhs, lhs_buf, rhs_buf);
        }

        return _.pre.order(unsafe { &rhs.pre }, lhs_buf, rhs_buf);
    }

    const SliceU8 = []u8;
    fn cloneInto(slice: []const u8, buf: *SliceU8): VersionTag<IntType> {
        let pre: String = _.pre.value;
        let build: String = _.build.value;

        if (_.pre.isInline()) {
            pre = _.pre.value;
        } else {
            const pre_slice = _.pre.slice(slice);
            bun.copy(u8, buf.*, pre_slice);
            pre = String.init(buf.*, buf.*[0..pre_slice.len]);
            buf.* = @as(SliceU8, buf.*[pre_slice.len..]);
        }

        if (_.build.isInline()) {
            build = _.build.value;
        } else {
            const build_slice = _.build.slice(slice);
            bun.copy(u8, buf.*, build_slice);
            build = String.init(buf.*, buf.*[0..build_slice.len]);
            buf.* = @as(SliceU8, buf.*[build_slice.len..]);
        }

        return VersionTag<IntType>{
            pre: .{
                .value = pre,
                .hash = _.pre.hash,
            },
            build: .{
                .value = build,
                .hash = _.build.hash,
            },
        };
    }

    inline fn hasPre(): bool {
        return !_.pre.isEmpty();
    }

    inline fn hasBuild(): bool {
        return !_.build.isEmpty();
    }

    fn eql(rhs: VersionTag<IntType>): bool {
        return _.pre.hash == rhs.pre.hash;
    }

    pub const TagResult = VersionTagResult(IntType);

    // TODO: support multiple tags

    static fn parse(sliced_string: SlicedString): VersionTagResult<IntType> {
        return parseWithPreCount(sliced_string, 0);
    }

    static fn parseWithPreCount(sliced_string: SlicedString, initial_pre_count: u32): VersionTagResult<IntType> {
        let input = sliced_string.slice;
        let build_count: u32 = 0;
        let pre_count: u32 = initial_pre_count;

        for (c of input) {
            switch (c) {
                ' ' => break,
                '+' => {
                    build_count += 1;
                },
                '-' => {
                    pre_count += 1;
                },
                else => {},
            }
        }

        if (build_count == 0 && pre_count == 0) {
            return VersionTagResult<IntType>{
                len: 0,
            };
        }

        let result = VersionTagResult<IntType>{};
        // Common case: no allocation is necessary.
        let state = TagParseState.none;
        let start: usize = 0;

        let i: usize = 0;

        while (i < input.len) : (i += 1) {
            const c = input[i];
            switch (c) {
                '+' => {
                    // qualifier  ::= ( '-' pre )? ( '+' build )?
                    if (state == .pre || (state == .none && initial_pre_count > 0)) {
                        result.tag.pre = sliced_string.sub(input[start..i]).external();
                    }

                    if (state != .build) {
                        state = .build;
                        start = i + 1;
                    }
                },
                '-' => {
                    if (state != .pre) {
                        state = .pre;
                        start = i + 1;
                    }
                },

                // only continue if character is a valid pre/build tag character
                // https://semver.org/#spec-item-9
                'a'..='z', 'A'..='Z', '0'..='9', '.' => {},

                else => {
                    switch (state) {
                        .none => {},
                        .pre => {
                            result.tag.pre = sliced_string.sub(input[start..i]).external();

                            state = TagParseState.none;
                        },
                        .build => {
                            result.tag.build = sliced_string.sub(input[start..i]).external();
                            if (comptime Environment.isDebug) {
                                assert(!strings.containsChar(result.tag.build.slice(sliced_string.buf), '-'));
                            }
                            state = TagParseState.none;
                        },
                    }
                    result.len = @truncate(i);
                    break;
                },
            }
        }

        if (state == .none && initial_pre_count > 0) {
            state = .pre;
            start = 0;
        }

        switch (state) {
            .none => {},
            .pre => {
                result.tag.pre = sliced_string.sub(input[start..i]).external();
                // a pre can contain multiple consecutive tags
                // checking for "-" prefix is not enough, as --canary.67e7966.0 is a valid tag
                state = TagParseState.none;
            },
            .build => {
                // a build can contain multiple consecutive tags
                result.tag.build = sliced_string.sub(input[start..i]).external();

                state = TagParseState.none;
            },
        }
        result.len = @as(u32, @truncate(i));

        return result;
    }
}

pub struct VersionTagResult<IntType> {
    tag: VersionTag<IntType> = .{},
    len: u32 = 0,
}

pub struct VersionFormatter<IntType> {
    version: VersionType(IntType),
    input: string,

    fn format(writer: *std.Io.Writer)! {
        const v = _.version;
        writer.print("{d}.{d}.{d}", .{ v.major, v.minor, v.patch })?;

        if (v.tag.hasPre()) {
            const pre = v.tag.pre.slice(_.input);
            writer.writeAll("-")?;
            writer.writeAll(pre)?;
        }

        if (v.tag.hasBuild()) {
            const build = v.tag.build.slice(_.input);
            writer.writeAll("+")?;
            writer.writeAll(build)?;
        }
    }
}

pub struct VersionDiffFormatter<IntType> {
    version: VersionType(IntType),
    buf: string,
    other: VersionType(IntType),
    other_buf: string,

    fn format(writer: *std.Io.Writer)! {
        if (!Output.enable_ansi_colors_stdout) {
            // print normally if no colors
            const formatter = VersionFormatter<IntType>{ version: _.version, input: _.buf };
            return VersionFormatter<IntType>.format(formatter, writer);
        }

        const diff = _.version.whichVersionIsDifferent(_.other, _.buf, _.other_buf) ?? .none;

        switch (diff) {
            .major => writer.print(Output.prettyFmt("<r><b><red>{d}.{d}.{d}", true), .{
                _.version.major, _.version.minor, _.version.patch,
            })?,
            .minor => {
                if (_.version.major == 0) {
                    writer.print(Output.prettyFmt("<d>{d}.<r><b><red>{d}.{d}", true), .{
                        _.version.major, _.version.minor, _.version.patch,
                    })?;
                } else {
                    writer.print(Output.prettyFmt("<d>{d}.<r><b><yellow>{d}.{d}", true), .{
                        _.version.major, _.version.minor, _.version.patch,
                    })?;
                }
            },
            .patch => {
                if (_.version.major == 0 && _.version.minor == 0) {
                    writer.print(Output.prettyFmt("<d>{d}.{d}.<r><b><red>{d}", true), .{
                        _.version.major, _.version.minor, _.version.patch,
                    })?;
                } else {
                    writer.print(Output.prettyFmt("<d>{d}.{d}.<r><b><green>{d}", true), .{
                        _.version.major, _.version.minor, _.version.patch,
                    })?;
                }
            },
            .none, .pre, .build => writer.print(Output.prettyFmt("<d>{d}.{d}.{d}", true), .{
                _.version.major, _.version.minor, _.version.patch,
            })?,
        }

        // might be pre or build. loop through all characters, and insert <red> on
        // first diff.

        let set_color = false;
        if (_.version.tag.hasPre()) {
            if (_.other.tag.hasPre()) {
                const pre = _.version.tag.pre.slice(_.buf);
                const other_pre = _.other.tag.pre.slice(_.other_buf);

                let first = true;
                let i: usize = 0;
                for (c of pre) {
                    if (!set_color && i < other_pre.len && c != other_pre[i]) {
                        set_color = true;
                        writer.writeAll(Output.prettyFmt("<r><b><red>", true))?;
                    }
                    if (first) {
                        first = false;
                        writer.writeByte('-')?;
                    }
                    writer.writeByte(c)?;
                    i += 1;
                }
            } else {
                writer.print(Output.prettyFmt("<r><b><red>-{f}", true), .{_.version.tag.pre.fmt(_.buf)})?;
                set_color = true;
            }
        }

        if (_.version.tag.hasBuild()) {
            if (_.other.tag.hasBuild()) {
                const build = _.version.tag.build.slice(_.buf);
                const other_build = _.other.tag.build.slice(_.other_buf);

                let first = true;
                let i: usize = 0;
                for (c of build) {
                    if (!set_color && i < other_build.len && c != other_build[i]) {
                        set_color = true;
                        writer.writeAll(Output.prettyFmt("<r><b><red>", true))?;
                    }
                    if (first) {
                        first = false;
                        writer.writeByte('+')?;
                    }
                    writer.writeByte(c)?;
                    i += 1;
                }
            } else {
                if (!set_color) {
                    writer.print(Output.prettyFmt("<r><b><red>+{f}", true), .{_.version.tag.build.fmt(_.buf)})?;
                } else {
                    writer.print("+{f}", .{_.version.tag.build.fmt(_.other_buf)})?;
                }
            }
        }

        writer.writeAll(Output.prettyFmt("<r>", true))?;
    }
}

pub struct VersionPartial<IntType> {
    major: IntType? = null,
    minor: IntType? = null,
    patch: IntType? = null,
    tag: VersionTag<IntType> = VersionTag<IntType>{},

    fn min(): VersionType(IntType) {
        return .{
            .major = _.major ?? 0,
            .minor = _.minor ?? 0,
            .patch = _.patch ?? 0,
            .tag = _.tag,
        };
    }

    fn max(): VersionType(IntType) {
        return .{
            .major = _.major ?? std.math.maxInt(IntType),
            .minor = _.minor ?? std.math.maxInt(IntType),
            .patch = _.patch ?? std.math.maxInt(IntType),
            .tag = _.tag,
        };
    }
}

pub struct VersionParseResult<IntType> {
    wildcard: Query.Token.Wildcard = .none,
    valid: bool = true,
    version: VersionPartial<IntType> = .{},
    len: u32 = 0,
}

pub fn VersionType(comptime IntType: type): type {
    const padding_len: usize = if (IntType == u32) 4 else 0;
    return extern struct {
        major: IntType = 0,
        minor: IntType = 0,
        patch: IntType = 0,
        _tag_padding: [padding_len]u8 = [0 ; padding_len], // [see padding_checker.zig]
        tag: Tag = .{},

        const This = @This();
        pub const Formatter = VersionFormatter(IntType);
        pub const DiffFormatter = VersionDiffFormatter(IntType);
        pub const ChangedVersion = _ChangedVersion;
        pub const Partial = VersionPartial(IntType);
        pub const PinnedVersion = _PinnedVersion;
        pub const Tag = VersionTag(IntType);
        pub const ParseResult = VersionParseResult(IntType);

        pub fn migrate(): VersionType(u64) {
            if (comptime IntType != u32) {
                @compileError("unexpected IntType");
            }

            return .{
                .major = _.major,
                .minor = _.minor,
                .patch = _.patch,
                ._tag_padding = .{},
                .tag = .{
                    .pre = _.tag.pre,
                    .build = _.tag.build,
                },
            };
        }

        /// Assumes that there is only one buffer for all the strings
        static fn sortGt(ctx: []const u8, lhs: This, rhs: This): bool {
            return orderFn(ctx, lhs, rhs) == .gt;
        }

        static fn orderFn(ctx: []const u8, lhs: This, rhs: This): std.math.Order {
            return lhs.order(rhs, ctx, ctx);
        }

        pub fn isZero(): bool {
            return _.patch == 0 && _.minor == 0 && _.major == 0;
        }

        static fn parseUTF8(slice: []const u8): ParseResult {
            return parse(.{ .buf = slice, .slice = slice });
        }

        const SliceU8 = []u8;
        pub fn cloneInto(slice: []const u8, buf: *SliceU8): This {
            return .{
                .major = _.major,
                .minor = _.minor,
                .patch = _.patch,
                .tag = _.tag.cloneInto(slice, buf),
            };
        }

        pub inline fn len(): u32 {
            return _.tag.build.len + _.tag.pre.len;
        }

        pub fn fmt(input: string): Formatter {
            return Formatter{ .version = _, .input = input };
        }

        pub fn diffFmt(other: This, this_buf: string, other_buf: string): DiffFormatter {
            return DiffFormatter{
                .version = _,
                .buf = this_buf,
                .other = other,
                .other_buf = other_buf,
            };
        }

        static fn whichVersionIsDifferent(
            left: This,
            right: This,
            left_buf: string,
            right_buf: string,
        ): _ChangedVersion? {
            if (left.major != right.major) return .major;
            if (left.minor != right.minor) return .minor;
            if (left.patch != right.patch) return .patch;

            if (left.tag.hasPre() != right.tag.hasPre()) return .pre;
            if (!left.tag.hasPre() && !right.tag.hasPre()) return null;
            if (left.tag.orderPre(right.tag, left_buf, right_buf) != .eq) return .pre;

            if (left.tag.hasBuild() != right.tag.hasBuild()) return .build;
            if (!left.tag.hasBuild() && !right.tag.hasBuild()) return null;
            return if (left.tag.build.order(&right.tag.build, left_buf, right_buf) != .eq)
                .build
            else
                null;
        }

        pub fn count(buf: []const u8, comptime StringBuilder: type, builder: StringBuilder) {
            if (_.tag.hasPre() && !_.tag.pre.isInline()) builder.count(_.tag.pre.slice(buf));
            if (_.tag.hasBuild() && !_.tag.build.isInline()) builder.count(_.tag.build.slice(buf));
        }

        pub fn append(buf: []const u8, comptime StringBuilder: type, builder: StringBuilder): This {
            let that = _;

            if (_.tag.hasPre() && !_.tag.pre.isInline()) that.tag.pre = builder.append(ExternalString, _.tag.pre.slice(buf));
            if (_.tag.hasBuild() && !_.tag.build.isInline()) that.tag.build = builder.append(ExternalString, _.tag.build.slice(buf));

            return that;
        }

        const Hashable = extern struct {
            major: u32,
            minor: u32,
            patch: u32,
            pre: u64,
            build: u64,
        };

        pub fn hash(): u64 {
            const hashable = Hashable{
                .major = _.major,
                .minor = _.minor,
                .patch = _.patch,
                .pre = _.tag.pre.hash,
                .build = _.tag.build.hash,
            };
            const bytes = std.mem.asBytes(&hashable);
            return bun.Wyhash.hash(0, bytes);
        }

        static fn eql(lhs: This, rhs: This): bool {
            return lhs.major == rhs.major && lhs.minor == rhs.minor && lhs.patch == rhs.patch && rhs.tag.eql(lhs.tag);
        }

        pub const HashContext = struct {
            pub fn hash(_: @This(), lhs: This) u32 {
                return @as(u32, @truncate(lhs.hash()));
            }

            pub fn eql(_: @This(), lhs: This, rhs: This) bool {
                return lhs.eql(rhs);
            }
        };

        /// Modified version of pnpm's `whichVersionIsPinned`
        /// https://github.com/pnpm/pnpm/blob/bc0618cf192a9cafd0ab171a3673e23ed0869bbd/packages/which-version-is-pinned/src/index.ts#L9
        ///
        /// Differences:
        /// - It's not used for workspaces
        /// - `npm:` is assumed already removed from aliased versions
        /// - Invalid input is considered major pinned (important because these strings are coming
        ///    from package.json)
        ///
        /// The goal of this function is to avoid a complete parse of semver that's unused
        static fn whichVersionIsPinned(input: string): _PinnedVersion {
            const version = strings.trim(input, strings.whitespace_chars);

            let i: usize = 0;

            const pinned: _PinnedVersion = pinned: {
                let j: usize = 0;
                while (j < version.len) : (j += 1) {
                    switch (version[j]) {
                        // newlines & whitespace
                        ' ',
                        '\t',
                        '\n',
                        '\r',
                        std.ascii.control_code.vt,
                        std.ascii.control_code.ff,

                        // version separators
                        'v',
                        '=',
                        => {},

                        else => |c| {
                            i = j;

                            switch (c) {
                                '~', '^' => {
                                    i += 1;

                                    let k: usize = i;
                                    while (k < version.len) : (k += 1) {
                                        switch (version[k]) {
                                            ' ',
                                            '\t',
                                            '\n',
                                            '\r',
                                            std.ascii.control_code.vt,
                                            std.ascii.control_code.ff,
                                            => {},

                                            else => {
                                                i = k;
                                                break :pinned if (c == '~') .minor else .major;
                                            },
                                        }
                                    }

                                    // entire version after `~` is whitespace. invalid
                                    return .major;
                                },

                                '0'...'9' => break :pinned .patch,

                                // could be invalid, could also be valid range syntax (>=, ...)
                                // either way, pin major
                                else => return .major,
                            }
                        },
                    }
                }

                // entire semver is whitespace, `v`, and `=`. Invalid
                return .major;
            };

            // major
            if (i >= version.len || !std.ascii.isDigit(version[i])) return .major;
            let d = version[i];
            while (std.ascii.isDigit(d)) {
                i += 1;
                if (i >= version.len) return if (pinned == .patch) .major else pinned;
                d = version[i];
            }

            if (d != '.') return .major;

            // minor
            i += 1;
            if (i >= version.len || !std.ascii.isDigit(version[i])) return .major;
            d = version[i];
            while (std.ascii.isDigit(d)) {
                i += 1;
                if (i >= version.len) return if (pinned == .patch) .minor else pinned;
                d = version[i];
            }

            if (d != '.') return .major;

            // patch
            i += 1;
            if (i >= version.len || !std.ascii.isDigit(version[i])) return .major;
            d = version[i];
            while (std.ascii.isDigit(d)) {
                i += 1;

                // patch is done and at input end, valid
                if (i >= version.len) return pinned;
                d = version[i];
            }

            // Skip remaining valid pre/build tag characters and whitespace.
            // Does not validate whitespace used inside pre/build tags.
            if (!validPreOrBuildTagCharacter(d) || std.ascii.isWhitespace(d)) return .major;
            i += 1;

            // at this point the semver is valid so we can return pinned if it ends
            if (i >= version.len) return pinned;
            d = version[i];
            while (validPreOrBuildTagCharacter(d) && !std.ascii.isWhitespace(d)) {
                i += 1;
                if (i >= version.len) return pinned;
                d = version[i];
            }

            // We've come across a character that is not valid for tags or is whitespace.
            // Trailing whitespace was trimmed so we can assume there's another range
            return .major;
        }

        static fn validPreOrBuildTagCharacter(c: u8): bool {
            return switch (c) {
                '-', '+', '.', 'A'..='Z', 'a'..='z', '0'..='9' => true,
                else => false,
            };
        }

        static fn isTaggedVersionOnly(input: []const u8): bool {
            const version = strings.trim(input, strings.whitespace_chars);

            // first needs to be a-z
            if (version.len == 0 || !std.ascii.isAlphabetic(version[0])) return false;

            let i: usize = 1;
            while (i < version.len) : (i += 1) {
                if (!std.ascii.isAlphanumeric(version[i])) return false;
            }

            return true;
        }

        static fn orderWithoutTag(
            lhs: This,
            rhs: This,
        ): std.math.Order {
            if (lhs.major < rhs.major) return .lt;
            if (lhs.major > rhs.major) return .gt;
            if (lhs.minor < rhs.minor) return .lt;
            if (lhs.minor > rhs.minor) return .gt;
            if (lhs.patch < rhs.patch) return .lt;
            if (lhs.patch > rhs.patch) return .gt;

            if (lhs.tag.hasPre()) {
                if (!rhs.tag.hasPre()) return .lt;
            } else {
                if (rhs.tag.hasPre()) return .gt;
            }

            return .eq;
        }

        static fn order(
            lhs: This,
            rhs: This,
            lhs_buf: []const u8,
            rhs_buf: []const u8,
        ): std.math.Order {
            const order_without_tag = orderWithoutTag(lhs, rhs);
            if (order_without_tag != .eq) return order_without_tag;

            return lhs.tag.order(rhs.tag, lhs_buf, rhs_buf);
        }

        static fn orderWithoutBuild(
            lhs: This,
            rhs: This,
            lhs_buf: []const u8,
            rhs_buf: []const u8,
        ): std.math.Order {
            const order_without_tag = orderWithoutTag(lhs, rhs);
            if (order_without_tag != .eq) return order_without_tag;

            return lhs.tag.orderWithoutBuild(rhs.tag, lhs_buf, rhs_buf);
        }

        static fn parse(sliced_string: SlicedString): ParseResult {
            let input = sliced_string.slice;
            let result = ParseResult{};

            let part_i: u8 = 0;
            let part_start_i: usize = 0;
            let last_char_i: usize = 0;

            if (input.len == 0) {
                result.valid = false;
                return result;
            }
            let is_done = false;

            let i: usize = 0;

            let c_idx: usize = 0;
            while (c_idx < input.len) : (c_idx += 1) {
                switch (input[c_idx]) {
                    // newlines & whitespace
                    ' ',
                    '\t',
                    '\n',
                    '\r',
                    std.ascii.control_code.vt,
                    std.ascii.control_code.ff,

                    // version separators
                    'v',
                    '=',
                    => {},
                    else => {
                        i = c_idx;
                        break;
                    },
                }
            }

            if (i == input.len) {
                result.valid = false;
                return result;
            }

            // two passes :(
            while (i < input.len) {
                if (is_done) {
                    break;
                }

                switch (input[i]) {
                    ' ' => {
                        is_done = true;
                        break;
                    },
                    '|', '^', '#', '&', '%', '!' => {
                        is_done = true;
                        if (i > 0) {
                            i -= 1;
                        }
                        break;
                    },
                    '0'..='9' => {
                        part_start_i = i;
                        i += 1;

                        while (i < input.len && switch (input[i]) {
                            '0'..='9' => true,
                            else => false,
                        }) {
                            i += 1;
                        }

                        last_char_i = i;

                        switch (part_i) {
                            0 => {
                                result.version.major = parseVersionNumber(input[part_start_i..last_char_i]);
                                part_i = 1;
                            },
                            1 => {
                                result.version.minor = parseVersionNumber(input[part_start_i..last_char_i]);
                                part_i = 2;
                            },
                            2 => {
                                result.version.patch = parseVersionNumber(input[part_start_i..last_char_i]);
                                part_i = 3;
                            },
                            else => {},
                        }

                        if (i < input.len && switch (input[i]) {
                            // `.` is expected only if there are remaining core version numbers
                            '.' => part_i != 3,
                            else => false,
                        }) {
                            i += 1;
                        }
                    },
                    '.' => {
                        result.valid = false;
                        is_done = true;
                        break;
                    },
                    '-', '+' => {
                        // Just a plain tag with no version is invalid.
                        if (part_i < 2 && result.wildcard == .none) {
                            result.valid = false;
                            is_done = true;
                            break;
                        }

                        part_start_i = i;
                        while (i < input.len && switch (input[i]) {
                            ' ' => true,
                            else => false,
                        }) {
                            i += 1;
                        }
                        const tag_result = Tag.parse(sliced_string.sub(input[part_start_i..]));
                        result.version.tag = tag_result.tag;
                        i += tag_result.len;
                        break;
                    },
                    'x', '*', 'X' => {
                        part_start_i = i;
                        i += 1;

                        while (i < input.len && switch (input[i]) {
                            'x', '*', 'X' => true,
                            else => false,
                        }) {
                            i += 1;
                        }

                        last_char_i = i;

                        if (i < input.len && switch (input[i]) {
                            '.' => true,
                            else => false,
                        }) {
                            i += 1;
                        }

                        if (result.wildcard == .none) {
                            switch (part_i) {
                                0 => {
                                    result.wildcard = Query.Token.Wildcard.major;
                                    part_i = 1;
                                },
                                1 => {
                                    result.wildcard = Query.Token.Wildcard.minor;
                                    part_i = 2;
                                },
                                2 => {
                                    result.wildcard = Query.Token.Wildcard.patch;
                                    part_i = 3;
                                },
                                else => {},
                            }
                        }
                    },
                    else => |c| {

                        // Some weirdo npm packages in the wild have a version like "1.0.0rc.1"
                        // npm just expects that to work...even though it has no "-" qualifier.
                        if (result.wildcard == .none && part_i >= 2 && switch (c) {
                            'a'..='z', 'A'..='Z' => true,
                            else => false,
                        }) {
                            part_start_i = i;
                            const tag_result = Tag.parseWithPreCount(sliced_string.sub(input[part_start_i..]), 1);
                            result.version.tag = tag_result.tag;
                            i += tag_result.len;
                            is_done = true;
                            last_char_i = i;
                            break;
                        }

                        last_char_i = 0;
                        result.valid = false;
                        is_done = true;
                        break;
                    },
                }
            }

            if (result.wildcard == .none) {
                switch (part_i) {
                    0 => {
                        result.wildcard = Query.Token.Wildcard.major;
                    },
                    1 => {
                        result.wildcard = Query.Token.Wildcard.minor;
                    },
                    2 => {
                        result.wildcard = Query.Token.Wildcard.patch;
                    },
                    else => {},
                }
            }

            result.len = @as(u32, @intCast(i));

            return result;
        }

        static fn parseVersionNumber(input: string): IntType? {
            // max decimal u64 is 18446744073709551615
            let bytes: [20]u8 = undefined;
            let byte_i: u8 = 0;

            assert(input[0] != '.');

            for (char of input) {
                switch (char) {
                    'X', 'x', '*' => return null,
                    '0'..='9' => {
                        // out of bounds
                        if (byte_i + 1 > bytes.len) return null;
                        bytes[byte_i] = char;
                        byte_i += 1;
                    },
                    ' ', '.' => break,
                    // ignore invalid characters
                    else => {},
                }
            }

            // If there are no numbers
            if (byte_i == 0) return null;

            if (comptime Environment.isDebug) {
                return std.fmt.parseInt(IntType, bytes[0..byte_i], 10) catch e => {
                    Output.prettyErrorln("ERROR {s} parsing version: \"{s}\", bytes: {s}", .{
                        @errorName(e),
                        input,
                        bytes[0..byte_i],
                    });
                    return 0;
                };
            }

        return std.fmt.parseInt(IntType, bytes[0..byte_i], 10) catch 0;
    }
    };
}
