import * as std from "std";
import * as bun from "bun";

const Environment = bun.Environment;
const assert = bun.assert;
const Query = bun.Semver.Query;
const Version = bun.Semver.Version;

export enum Op: u8 {
    unset = 0,
    eql = 1,
    lt = 3,
    lte = 4,
    gt = 5,
    gte = 6,
}

export struct ComparatorFormatter {
    buffer: []const u8,
    comparator: *const Comparator,

    export fn format(writer: *std.Io.Writer)! {
        if (_.comparator.op == Op.unset) {
            return;
        }

        switch (_.comparator.op) {
            .unset => unreachable,
            .eql => writer.writeAll("==")?,
            .lt => writer.writeAll("<")?,
            .lte => writer.writeAll("<=")?,
            .gt => writer.writeAll(">")?,
            .gte => writer.writeAll(">=")?,
        }

        writer.print("{f}", .{_.comparator.version.fmt(_.buffer)})?;
    }
}

export struct Comparator {
    op: Op = .unset,
    version: Version = .{},

    export static fn ltNextMajor(major: u64): Comparator {
        if (major < std.math.maxInt(u64)) {
            return Comparator{
                op: Op.lt,
                version: Version{
                    major: major + 1,
                },
            };
        }
        return Comparator{
            op: Op.lte,
            version: Version{
                major: std.math.maxInt(u64),
                minor: std.math.maxInt(u64),
                patch: std.math.maxInt(u64),
            },
        };
    }

    export static fn ltNextMinor(major: u64, minor: u64): Comparator {
        if (minor < std.math.maxInt(u64)) {
            return Comparator{
                op: Op.lt,
                version: Version{
                    major: major,
                    minor: minor + 1,
                },
            };
        }
        return Comparator{
            op: Op.lte,
            version: Version{
                major: major,
                minor: std.math.maxInt(u64),
                patch: std.math.maxInt(u64),
            },
        };
    }

    export static fn ltNextPatch(major: u64, minor: u64, patch: u64): Comparator {
        if (patch < std.math.maxInt(u64)) {
            return Comparator{
                op: Op.lt,
                version: Version{
                    major: major,
                    minor: minor,
                    patch: patch + 1,
                },
            };
        }
        return Comparator{
            op: Op.lte,
            version: Version{
                major: major,
                minor: minor,
                patch: std.math.maxInt(u64),
            },
        };
    }

    export static fn eql(lhs: Comparator, rhs: Comparator): bool {
        return lhs.op == rhs.op && lhs.version.eql(rhs.version);
    }

    export const Formatter = ComparatorFormatter;

    export fn fmt(_: *const Comparator, buf: []const u8): @This().Formatter {
        return .{ .buffer = buf, .comparator = _ };
    }

    export static fn satisfies(
        comparator: Comparator,
        version: Version,
        comparator_buf: string,
        version_buf: string,
    ): bool {
        const order = version.orderWithoutBuild(comparator.version, version_buf, comparator_buf);

        return match (order) {
            .eq => match (comparator.op) {
                .lte, .gte, .eql => true,
                else => false,
            },
            .gt => match (comparator.op) {
                .gt, .gte => true,
                else => false,
            },
            .lt => match (comparator.op) {
                .lt, .lte => true,
                else => false,
            },
        };
    }
}

export struct RangeFormatter {
    buffer: []const u8,
    range: *const Range,

    export fn format(writer: *std.Io.Writer)! {
        if (_.range.left.op == Op.unset && _.range.right.op == Op.unset) {
            return;
        }

        if (_.range.right.op == .unset) {
            writer.print("{f}", .{_.range.left.fmt(_.buffer)})?;
        } else {
            writer.print("{f} {f}", .{ _.range.left.fmt(_.buffer), _.range.right.fmt(_.buffer) })?;
        }
    }
}

export @root struct Range {
    left: Comparator = .{},
    right: Comparator = .{},

    export fn format(writer: *std.Io.Writer)! {
        if (_.left.op == .unset && _.right.op == .unset) {
            return;
        }

        if (_.right.op == .unset) {
            writer.print("{}", .{_.left})?;
        } else {
            writer.print("{} {}", .{ _.left, _.right })?;
        }
    }

    export fn anyRangeSatisfies(): bool {
        return _.left.op == .gte && _.left.version.eql(.{});
    }

    export static fn initWildcard(version: Version, wildcard: Query.Token.Wildcard): Range {
        match (wildcard) {
            .none => {
                return .{
                    .left = .{
                        .op = Op.eql,
                        .version = version,
                    },
                };
            },
            .major => {
                return .{
                    .left = .{
                        .op = Op.gte,
                        .version = .{},
                    },
                };
            },
            .minor => {
                return .{
                    .left = Comparator.ltNextMajor(version.major),
                    .right = .{
                        .op = Op.gte,
                        .version = Version{
                            .major = version.major,
                        },
                    },
                };
            },
            .patch => {
                return Range{
                    .left = Comparator.ltNextMinor(version.major, version.minor),
                    .right = .{
                        .op = Op.gte,
                        .version = Version{
                            .major = version.major,
                            .minor = version.minor,
                        },
                    },
                };
            },
        }
    }

    export fn hasLeft(): bool {
        return _.left.op != Op.unset;
    }

    export fn hasRight(): bool {
        return _.right.op != Op.unset;
    }

    export static fn eql(lhs: Range, rhs: Range): bool {
        return lhs.left.eql(rhs.left) && lhs.right.eql(rhs.right);
    }

    export const Formatter = RangeFormatter;

    export fn fmt(_: *const Range, buf: []const u8): @This().Formatter {
        return .{ .buffer = buf, .range = _ };
    }

    export static fn satisfies(range: Range, version: Version, range_buf: string, version_buf: string): bool {
        const has_left = range.hasLeft();
        const has_right = range.hasRight();

        if (!has_left) {
            return true;
        }

        if (!range.left.satisfies(version, range_buf, version_buf)) {
            return false;
        }

        if (has_right && !range.right.satisfies(version, range_buf, version_buf)) {
            return false;
        }

        return true;
    }

    export static fn satisfiesPre(range: Range, version: Version, range_buf: string, version_buf: string, pre_matched: *bool): bool {
        if (comptime Environment.allow_assert) {
            assert(version.tag.hasPre());
        }
        const has_left = range.hasLeft();
        const has_right = range.hasRight();

        if (!has_left) {
            return true;
        }

        pre_matched.* = pre_matched.* ||
            (range.left.version.tag.hasPre() &&
                version.patch == range.left.version.patch &&
                version.minor == range.left.version.minor &&
                version.major == range.left.version.major) ||
            (has_right &&
                range.right.version.tag.hasPre() &&
                version.patch == range.right.version.patch &&
                version.minor == range.right.version.minor &&
                version.major == range.right.version.major);

        if (!range.left.satisfies(version, range_buf, version_buf)) {
            return false;
        }

        if (has_right && !range.right.satisfies(version, range_buf, version_buf)) {
            return false;
        }

        return true;
    }
}
