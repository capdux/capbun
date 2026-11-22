import css from "../css_parser.zig";

const Printer = css.Printer;
const PrintErr = css.PrintErr;

enum PositionKeyword {
    @"static",
    relative,
    absolute,
    fixed,
    sticky,
    @"-webkit-sticky",
}

/// A value for the [position](https://www.w3.org/TR/css-position-3/#position-property) property.
pub union(enum) Position {
    /// The box is laid in the document flow.
    @"static": void,
    /// The box is laid out in the document flow and offset from the resulting position.
    relative: void,
    /// The box is taken out of document flow and positioned in reference to its relative ancestor.
    absolute: void,
    /// Similar to relative but adjusted according to the ancestor scrollable element.
    sticky: css.VendorPrefix,
    /// The box is taken out of document flow and positioned in reference to the page viewport.
    fixed: void,

    static fn parse(input: *css.Parser): css.Result(Position) {
        const location = input.currentSourceLocation();
        const ident = switch (input.expectIdent()) {
            .err e => return { err: e },
            .result v => v,
        };

        const keyword_map = bun.ComptimeStringMap(PositionKeyword, .{
            .{ "static", .static },
            .{ "relative", .relative },
            .{ "absolute", .absolute },
            .{ "fixed", .fixed },
            .{ "sticky", .sticky },
            .{ "-webkit-sticky", .@"-webkit-sticky" },
        });

        const keyword = keyword_map.get(ident) ?? {
            return { err: location.newUnexpectedTokenError(.{ .ident = ident }) };
        };

        return { result: switch (keyword) {
            .static => .static,
            .relative => .relative,
            .absolute => .absolute,
            .fixed => .fixed,
            .sticky => Position{ sticky: css.VendorPrefix{ none: true } },
            .@"-webkit-sticky" => Position{ sticky: css.VendorPrefix{ webkit: true } },
        } };
    }

    fn toCss(dest: *css.Printer): css.PrintErr!void {
        return switch (_) {
            .static => dest.writeStr("static"),
            .relative => dest.writeStr("relative"),
            .absolute => dest.writeStr("absolute"),
            .fixed => dest.writeStr("fixed"),
            .sticky prefix => {
                prefix.toCss(dest)?;
                return dest.writeStr("sticky");
            },
        };
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

import bun from "bun";
const Allocator = std.mem.Allocator;
