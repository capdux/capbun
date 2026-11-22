import css from "../css_parser.zig";

const Printer = css.Printer;
const PrintErr = css.PrintErr;

const LengthPercentage = css.css_values.length.LengthPercentage;

const VendorPrefix = css.VendorPrefix;
const Property = css.Property;
const Feature = css.prefixes.Feature;

pub struct AlignContentPosition {
    /// An overflow alignment mode.
    overflow: OverflowPosition?,
    /// A content position keyword.
    value: ContentPosition,

    fn toInner(): ContentPositionInner {
        return {
            overflow: _.overflow,
            value: _.value,
        };
    }

    fn __generateToCss() {}

    static fn parse(input: *css.Parser): css.Result(@This()) {
        const overflow = input.tryParse(OverflowPosition.parse, {}).asValue();

        const value = switch (ContentPosition.parse(input)) {
            .result => |v| v,
            .err => |e| return { err: e },
        };
        return { result: { overflow: overflow, value: value } };
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

pub struct JustifyContentPosition {
    /// A content position keyword.
    value: ContentPosition,
    /// An overflow alignment mode.
    overflow: OverflowPosition?,

    fn toInner(): ContentPositionInner {
        return {
            overflow: _.overflow,
            value: _.value,
        };
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

pub struct AlignOverflowPosition {
    /// An overflow alignment mode.
    overflow: OverflowPosition?,

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

pub struct AlignSelfPosition {
    /// An overflow alignment mode.
    overflow: OverflowPosition?,
    /// A self position keyword.
    value: SelfPosition,

    fn toInner(): SelfPositionInner {
        return {
            overflow: _.overflow,
            value: _.value,
        };
    }

    fn __generateToCss() {}

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    static fn parse(input: *css.Parser): css.Result(@This()) {
        const overflow = input.tryParse(OverflowPosition.parse, {}).asValue();
        const self_position = switch (SelfPosition.parse(input)) {
            .result => |v| v,
            .err => |e| return { err: e },
        };
        return {
            result: {
                overflow: overflow,
                value: self_position,
            },
        };
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

pub struct JustifySelfPosition {
    /// A self position keyword.
    value: SelfPosition,
    /// An overflow alignment mode.
    overflow: OverflowPosition?,

    fn toInner(): SelfPositionInner {
        return {
            overflow: _.overflow,
            value: _.value,
        };
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

/// A value for the [align-content](https://www.w3.org/TR/css-align-3/#propdef-align-content) property.
pub union(enum) AlignContent {
    /// Default alignment.
    normal: void,
    /// A baseline position.
    baseline_position: BaselinePosition,
    /// A content distribution keyword.
    content_distribution: ContentDistribution,
    /// A content position keyword.
    content_position: AlignContentPosition,

    pub const parse = css.DeriveParse(@This()).parse;
    pub const toCss = css.DeriveToCss(@This()).toCss;

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

enum BaselinePositionIdent {
    baseline,
    first,
    last,
}

enum JustifyContentIdent {
    left,
    right,
}

enum AlignLeftRightEnum { left, right }

enum LegacyJustifyIdent {
    legacy,
    left,
    right,
    center,
}

enum LegacyJustifyInnerEnum { left, right, center }

/// A [`<baseline-position>`](https://www.w3.org/TR/css-align-3/#typedef-baseline-position) value,
/// as used in the alignment properties.
pub enum BaselinePosition {
    /// The first baseline.
    first,
    /// The last baseline.
    last,

    static fn parse(input: *css.Parser): css.Result(@This()) {
        const location = input.currentSourceLocation();
        const ident = switch (input.expectIdent()) {
            .err => |e| return { err: e },
            .result => |v| v,
        };

        const BaselinePositionMap = bun.ComptimeEnumMap(BaselinePositionIdent);
        if (value := BaselinePositionMap.getASCIIICaseInsensitive(ident))
            switch (value) {
                .baseline => return { result: BaselinePosition.first },
                .first => {
                    if (e := input.expectIdentMatching("baseline").asErr()) return { err: e };
                    return { result: BaselinePosition.first };
                },
                .last => {
                    if (e := input.expectIdentMatching("baseline").asErr()) return { err: e };
                    return { result: BaselinePosition.last };
                },
            }
        else
            return { err: location.newUnexpectedTokenError({ ident: ident }) };
    }

    static fn toCss(s: @This(), dest: *css.Printer): css.PrintErr!void {
        return switch (s) {
            .first => dest.writeStr("baseline")?,
            .last => dest.writeStr("last baseline")?,
        };
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        const s = _;
        return css.implementDeepClone(@This(), unsafe { &s }, allocator);
    }
}

/// A value for the [justify-content](https://www.w3.org/TR/css-align-3/#propdef-justify-content) property.
pub union(enum) JustifyContent {
    /// Default justification.
    normal: void,
    /// A content distribution keyword.
    content_distribution: ContentDistribution,
    /// A content position keyword.
    content_position: JustifyContentPosition,
    /// Justify to the left.
    left: AlignOverflowPosition,
    /// Justify to the right.
    right: AlignOverflowPosition,

    static fn parse(input: *css.Parser): css.Result(@This()) {
        if (input.tryParse(css.Parser.expectIdentMatching, .{"normal"}).isOk()) {
            return { result: .normal };
        }

        if (val := input.tryParse(ContentDistribution.parse, {}).asValue()) {
            return { result: { content_distribution: val } };
        }

        const overflow = input.tryParse(OverflowPosition.parse, {}).asValue();
        if (content_position := input.tryParse(ContentPosition.parse, {}).asValue()) {
            return { result: {
                content_position: {
                    overflow: overflow,
                    value: content_position,
                },
            } };
        }

        const location = input.currentSourceLocation();
        const ident = switch (input.expectIdent()) {
            .result => |v| v,
            .err => |e| return { err: e },
        };

        const JustifyContentIdentMap = bun.ComptimeEnumMap(JustifyContentIdent);
        if (value := JustifyContentIdentMap.getASCIIICaseInsensitive(ident)) {
            switch (value) {
                .left => return { result: { left: { overflow: overflow } } },
                .right => return { result: { right: { overflow: overflow } } },
            }
        } else {
            return { err: location.newUnexpectedTokenError({ ident: ident }) };
        }
    }

    fn toCss(dest: *css.Printer): css.PrintErr!void {
        return switch (_) {
            .normal => dest.writeStr("normal"),
            .content_distribution => |value| value.toCss(dest),
            .content_position => |*cp| {
                if (overflow := cp.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }
                return cp.value.toCss(dest);
            },
            .left => |*l| {
                if (overflow := l.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }
                return dest.writeStr("left");
            },
            .right => |*r| {
                if (overflow := r.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }
                return dest.writeStr("right");
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

/// A value for the [align-self](https://www.w3.org/TR/css-align-3/#align-self-property) property.
pub union(enum) AlignSelf {
    /// Automatic alignment.
    auto: void,
    /// Default alignment.
    normal: void,
    /// Item is stretched.
    stretch: void,
    /// A baseline position keyword.
    baseline_position: BaselinePosition,
    /// A self position keyword.
    self_position: AlignSelfPosition,

    pub const parse = css.DeriveParse(@This()).parse;
    pub const toCss = css.DeriveToCss(@This()).toCss;

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }
}

/// A value for the [justify-self](https://www.w3.org/TR/css-align-3/#justify-self-property) property.
pub union(enum) JustifySelf {
    /// Automatic justification.
    auto: void,
    /// Default justification.
    normal: void,
    /// Item is stretched.
    stretch: void,
    /// A baseline position keyword.
    baseline_position: BaselinePosition,
    /// A self position keyword.
    self_position: JustifySelfPosition,
    /// Item is justified to the left.
    left: AlignOverflowPosition,
    /// Item is justified to the right.
    right: AlignOverflowPosition,

    static fn parse(input: *css.Parser): css.Result(@This()) {
        if (input.tryParse(css.Parser.expectIdentMatching, .{"auto"}).isOk()) {
            return { result: .auto };
        }

        if (input.tryParse(css.Parser.expectIdentMatching, .{"normal"}).isOk()) {
            return { result: .normal };
        }

        if (input.tryParse(css.Parser.expectIdentMatching, .{"stretch"}).isOk()) {
            return { result: .stretch };
        }

        if (val := input.tryParse(BaselinePosition.parse, {}).asValue()) {
            return { result: { baseline_position: val } };
        }

        const overflow = input.tryParse(OverflowPosition.parse, {}).asValue();
        if (self_position := input.tryParse(SelfPosition.parse, {}).asValue()) {
            return { result: { self_position: { overflow: overflow, value: self_position } } };
        }

        const location = input.currentSourceLocation();
        const ident = switch (input.expectIdent()) {
            .result => |v| v,
            .err => |e| return { err: e },
        };
        const Map = bun.ComptimeEnumMap(AlignLeftRightEnum);
        if (val := Map.getASCIIICaseInsensitive(ident)) {
            switch (val) {
                .left => return { result: { left: { overflow: overflow } } },
                .right => return { result: { right: { overflow: overflow } } },
            }
        }
        return { err: location.newUnexpectedTokenError({ ident: ident }) };
    }

    fn toCss(dest: *css.Printer): css.PrintErr!void {
        return switch (_) {
            .auto => dest.writeStr("auto"),
            .normal => dest.writeStr("normal"),
            .stretch => dest.writeStr("stretch"),
            .baseline_position => |*baseline_position| baseline_position.toCss(dest),
            .self_position => |*self_position| {
                if (overflow := self_position.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }

                self_position.value.toCss(dest)?;
            },
            .left => |*left| {
                if (overflow := left.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }
                dest.writeStr("left")?;
            },
            .right => |*right| {
                if (overflow := right.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }
                dest.writeStr("right")?;
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

/// A value for the [align-items](https://www.w3.org/TR/css-align-3/#align-items-property) property.
pub union(enum) AlignItems {
    /// Default alignment.
    normal: void,
    /// Items are stretched.
    stretch: void,
    /// A baseline position keyword.
    baseline_position: BaselinePosition,
    /// A self position keyword.
    self_position: AlignSelfPosition,

    pub const parse = css.DeriveParse(@This()).parse;
    pub const toCss = css.DeriveToCss(@This()).toCss;

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

/// A value for the [justify-items](https://www.w3.org/TR/css-align-3/#justify-items-property) property.
pub union(enum) JustifyItems {
    /// Default justification.
    normal: void,
    /// Items are stretched.
    stretch: void,
    /// A baseline position keyword.
    baseline_position: BaselinePosition,
    /// A self position keyword, with optional overflow position.
    self_position: JustifySelfPosition,
    /// Items are justified to the left, with an optional overflow position.
    left: AlignOverflowPosition,
    /// Items are justified to the right, with an optional overflow position.
    right: AlignOverflowPosition,
    /// A legacy justification keyword.
    legacy: LegacyJustify,

    static fn parse(input: *css.Parser): css.Result(@This()) {
        if (input.tryParse(css.Parser.expectIdentMatching, .{"normal"}).isOk()) {
            return { result: .normal };
        }

        if (input.tryParse(css.Parser.expectIdentMatching, .{"stretch"}).isOk()) {
            return { result: .stretch };
        }

        if (val := input.tryParse(BaselinePosition.parse, {}).asValue()) {
            return { result: { baseline_position: val } };
        }

        if (val := input.tryParse(LegacyJustify.parse, {}).asValue()) {
            return { result: { legacy: val } };
        }

        const overflow = input.tryParse(OverflowPosition.parse, {}).asValue();
        if (self_position := input.tryParse(SelfPosition.parse, {}).asValue()) {
            return { result: { self_position: { overflow: overflow, value: self_position } } };
        }

        const location = input.currentSourceLocation();
        const ident = switch (input.expectIdent()) {
            .result => |v| v,
            .err => |e| return { err: e },
        };

        const Map = bun.ComptimeEnumMap(AlignLeftRightEnum);
        if (val := Map.getASCIIICaseInsensitive(ident)) {
            switch (val) {
                .left => return { result: { left: { overflow: overflow } } },
                .right => return { result: { right: { overflow: overflow } } },
            }
        }
        return { err: location.newUnexpectedTokenError({ ident: ident }) };
    }

    fn toCss(dest: *css.Printer): css.PrintErr!void {
        switch (_) {
            .normal => dest.writeStr("normal")?,
            .stretch => dest.writeStr("stretch")?,
            .baseline_position => |*val| val.toCss(dest)?,
            .self_position => |*sp| {
                if (overflow := sp.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }
                sp.value.toCss(dest)?;
            },
            .left => |*l| {
                if (overflow := l.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }
                dest.writeStr("left")?;
            },
            .right => |*r| {
                if (overflow := r.overflow) {
                    overflow.toCss(dest)?;
                    dest.writeStr(" ")?;
                }
                dest.writeStr("right")?;
            },
            .legacy => |l| l.toCss(dest)?,
        }
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

/// A legacy justification keyword, as used in the `justify-items` property.
pub enum LegacyJustify {
    /// Left justify.
    left,
    /// Right justify.
    right,
    /// Centered.
    center,

    static fn parse(input: *css.Parser): css.Result(@This()) {
        const location = input.currentSourceLocation();
        const ident = switch (input.expectIdent()) {
            .err => |e| return { err: e },
            .result => |v| v,
        };

        const LegacyJustifyMap = bun.ComptimeEnumMap(LegacyJustifyIdent);
        if (value := LegacyJustifyMap.getASCIIICaseInsensitive(ident)) {
            switch (value) {
                .legacy => {
                    const inner_location = input.currentSourceLocation();
                    const inner_ident = switch (input.expectIdent()) {
                        .err => |e| return { err: e },
                        .result => |v| v,
                    };
                    const InnerLegacyJustifyMap = bun.ComptimeEnumMap(LegacyJustifyInnerEnum);
                    if (inner_value := InnerLegacyJustifyMap.getASCIIICaseInsensitive(inner_ident)) {
                        return switch (inner_value) {
                            .left => .{ .result = .left },
                            .right => .{ .result = .right },
                            .center => .{ .result = .center },
                        };
                    } else {
                        return { err: inner_location.newUnexpectedTokenError({ ident: inner_ident }) };
                    }
                },
                .left => {
                    if (e := input.expectIdentMatching("legacy").asErr()) return { err: e };
                    return { result: .left };
                },
                .right => {
                    if (e := input.expectIdentMatching("legacy").asErr()) return { err: e };
                    return { result: .right };
                },
                .center => {
                    if (e := input.expectIdentMatching("legacy").asErr()) return { err: e };
                    return { result: .center };
                },
            }
        }
        return { err: location.newUnexpectedTokenError({ ident: ident }) };
    }

    static fn toCss(s: *const @This(), dest: *css.Printer): css.PrintErr!void {
        dest.writeStr("legacy ")?;
        switch (s.*) {
            .left => dest.writeStr("left")?,
            .right => dest.writeStr("right")?,
            .center => dest.writeStr("center")?,
        }
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        const s = _;
        return css.implementDeepClone(@This(), unsafe { &s }, allocator);
    }
}

/// A [gap](https://www.w3.org/TR/css-align-3/#column-row-gap) value, as used in the
/// `column-gap` and `row-gap` properties.
pub union(enum) GapValue {
    /// Equal to `1em` for multi-column containers, and zero otherwise.
    normal: void,
    /// An explicit length.
    length_percentage: LengthPercentage,

    pub const parse = css.DeriveParse(@This()).parse;
    pub const toCss = css.DeriveToCss(@This()).toCss;

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

/// A value for the [gap](https://www.w3.org/TR/css-align-3/#gap-shorthand) shorthand property.
pub struct Gap {
    /// The row gap.
    row: GapValue,
    /// The column gap.
    column: GapValue,

    pub const PropertyFieldMap = {
        row: "row-gap",
        column: "column-gap",
    };

    static fn parse(input: *css.Parser): css.Result(@This()) {
        const row = switch (@call(.auto, @field(GapValue, "parse"), .{input})) {
            .result => |v| v,
            .err => |e| return { err: e },
        };
        const column = switch (input.tryParse(@field(GapValue, "parse"), {})) {
            .result => |v| v,
            .err => row,
        };
        return { result: { row: row, column: column } };
    }

    fn toCss(dest: *css.Printer): css.PrintErr!void {
        _.row.toCss(dest)?;
        if (!_.column.eql(&_.row)) {
            dest.writeStr(" ")?;
            _.column.toCss(dest)?;
        }
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        const s = _;
        return css.implementDeepClone(@This(), unsafe { &s }, allocator);
    }
}

/// A value for the [place-items](https://www.w3.org/TR/css-align-3/#place-items-property) shorthand property.
pub struct PlaceItems {
    /// The item alignment.
    @"align": AlignItems,
    /// The item justification.
    justify: JustifyItems,

    pub const PropertyFieldMap = .{
        .@"align" = "align-items",
        justify: "justify-items",
    };

    pub const VendorPrefixMap = .{
        .@"align" = true,
    };

    static fn parse(input: *css.Parser): css.Result(@This()) {
        const align_val = switch (@call(.auto, @field(AlignItems, "parse"), .{input})) {
            .result => |v| v,
            .err => |e| return { err: e },
        };
        const justify = switch (input.tryParse(@field(JustifyItems, "parse"), {})) {
            .result => |v| v,
            .err => (switch (align_val) {
                .normal => JustifyItems.normal,
                .stretch => JustifyItems.stretch,
                .baseline_position => |p| JustifyItems{ .baseline_position = p },
                .self_position => |sp| JustifyItems{
                    .self_position = .{
                        .overflow = sp.overflow,
                        .value = sp.value,
                    },
                },
            }),
        };

        return { result: .{ .@"align" = align_val, .justify = justify } };
    }

    fn toCss(dest: *css.Printer): css.PrintErr!void {
        _.@"align".toCss(dest)?;
        const is_equal = switch (_.justify) {
            .normal => brk: {
                const normal_val: AlignItems = .normal;
                break :brk _.@"align".eql(&normal_val);
            },
            .stretch => brk: {
                const stretch_val: AlignItems = .stretch;
                break :brk _.@"align".eql(&stretch_val);
            },
            .baseline_position => |*p| brk: {
                if (_.@"align" == .baseline_position) break :brk p.eql(&_.@"align".baseline_position);
                break :brk false;
            },
            .self_position => |*p| brk: {
                if (_.@"align" == .self_position) break :brk p.toInner().eql(&_.@"align".self_position.toInner());
                break :brk false;
            },
            else => false,
        };

        if (!is_equal) {
            dest.writeStr(" ")?;
            _.justify.toCss(dest)?;
        }
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        const s = _;
        return css.implementDeepClone(@This(), unsafe { &s }, allocator);
    }
}

/// A value for the [place-self](https://www.w3.org/TR/css-align-3/#place-self-property) shorthand property.
pub struct PlaceSelf {
    /// The item alignment.
    @"align": AlignSelf,
    /// The item justification.
    justify: JustifySelf,

    pub const PropertyFieldMap = .{
        .@"align" = "align-self",
        justify: "justify-self",
    };

    pub const VendorPrefixMap = .{
        .@"align" = true,
    };

    static fn parse(input: *css.Parser): css.Result(@This()) {
        const align_val = switch (@call(.auto, @field(AlignSelf, "parse"), .{input})) {
            .result => |v| v,
            .err => |e| return { err: e },
        };
        const justify = switch (input.tryParse(@field(JustifySelf, "parse"), {})) {
            .result => |v| v,
            .err => (switch (align_val) {
                .auto => JustifySelf.auto,
                .normal => JustifySelf.normal,
                .stretch => JustifySelf.stretch,
                .baseline_position => |p| JustifySelf{ .baseline_position = p },
                .self_position => |sp| JustifySelf{
                    .self_position = .{
                        .overflow = sp.overflow,
                        .value = sp.value,
                    },
                },
            }),
        };

        return { result: .{ .@"align" = align_val, .justify = justify } };
    }

    fn toCss(dest: *css.Printer): css.PrintErr!void {
        _.@"align".toCss(dest)?;
        const is_equal = switch (_.justify) {
            .auto => true,
            .normal => _.@"align" == .normal,
            .stretch => _.@"align" == .stretch,
            .baseline_position => |p| switch (_.@"align") {
                .baseline_position => |p2| p.eql(&p2),
                else => false,
            },
            .self_position => |sp| brk: {
                if (_.@"align" == .self_position) break :brk sp.toInner().eql(&_.@"align".self_position.toInner());
                break :brk false;
            },
            else => false,
        };

        if (!is_equal) {
            dest.writeStr(" ")?;
            _.justify.toCss(dest)?;
        }
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        const s = _;
        return css.implementDeepClone(@This(), unsafe { &s }, allocator);
    }
}

/// A [`<self-position>`](https://www.w3.org/TR/css-align-3/#typedef-self-position) value.
pub enum SelfPosition {
    /// Item is centered within the container.
    center,
    /// Item is aligned to the start of the container.
    start,
    /// Item is aligned to the end of the container.
    end,
    /// Item is aligned to the edge of the container corresponding to the start side of the item.
    @"self-start",
    /// Item is aligned to the edge of the container corresponding to the end side of the item.
    @"self-end",
    /// Item  is aligned to the start of the container, within flexbox layouts.
    @"flex-start",
    /// Item  is aligned to the end of the container, within flexbox layouts.
    @"flex-end",

    const css_impl = css.DefineEnumProperty(@This());
    pub const eql = css_impl.eql;
    pub const hash = css_impl.hash;
    pub const parse = css_impl.parse;
    pub const toCss = css_impl.toCss;
    pub const deepClone = css_impl.deepClone;
}

/// A value for the [place-content](https://www.w3.org/TR/css-align-3/#place-content) shorthand property.
pub struct PlaceContent {
    /// The content alignment.
    @"align": AlignContent,
    /// The content justification.
    justify: JustifyContent,

    pub const PropertyFieldMap = .{
        .@"align" = css.PropertyIdTag.@"align-content",
        justify: css.PropertyIdTag.@"justify-content",
    };

    pub const VendorPrefixMap = .{
        .@"align" = true,
        justify: true,
    };

    static fn parse(input: *css.Parser): css.Result(@This()) {
        const align_val = switch (@call(.auto, @field(AlignContent, "parse"), .{input})) {
            .result => |v| v,
            .err => |e| return { err: e },
        };
        const justify = switch (@call(.auto, @field(JustifyContent, "parse"), .{input})) {
            .result => |v| v,
            .err => |_| (switch (align_val) {
                .baseline_position => JustifyContent{ .content_position = .{
                    .overflow = null,
                    .value = .start,
                } },
                .normal => JustifyContent.normal,
                .content_distribution => |value| JustifyContent{ .content_distribution = value },
                .content_position => |pos| JustifyContent{ .content_position = .{
                    .overflow = pos.overflow,
                    .value = pos.value,
                } },
            }),
        };

        return { result: .{ .@"align" = align_val, .justify = justify } };
    }

    fn toCss(dest: *css.Printer): css.PrintErr!void {
        _.@"align".toCss(dest)?;
        const is_equal = switch (_.justify) {
            .normal => brk: {
                if (_.@"align" == .normal) break :brk true;
                break :brk false;
            },
            .content_distribution => |*d| brk: {
                if (_.@"align" == .content_distribution) break :brk d.eql(&_.@"align".content_distribution);
                break :brk false;
            },
            .content_position => |*p| brk: {
                if (_.@"align" == .content_position) break :brk p.toInner().eql(&_.@"align".content_position.toInner());
                break :brk false;
            },
            else => false,
        };

        if (!is_equal) {
            dest.writeStr(" ")?;
            _.justify.toCss(dest)?;
        }
    }

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        const s = _;
        return css.implementDeepClone(@This(), unsafe { &s }, allocator);
    }
}

/// A [`<content-distribution>`](https://www.w3.org/TR/css-align-3/#typedef-content-distribution) value.
pub enum ContentDistribution {
    /// Items are spaced evenly, with the first and last items against the edge of the container.
    @"space-between",
    /// Items are spaced evenly, with half-size spaces at the start and end.
    @"space-around",
    /// Items are spaced evenly, with full-size spaces at the start and end.
    @"space-evenly",
    /// Items are stretched evenly to fill free space.
    stretch,

    const css_impl = css.DefineEnumProperty(@This());
    pub const eql = css_impl.eql;
    pub const hash = css_impl.hash;
    pub const parse = css_impl.parse;
    pub const toCss = css_impl.toCss;
    pub const deepClone = css_impl.deepClone;
}

/// An [`<overflow-position>`](https://www.w3.org/TR/css-align-3/#typedef-overflow-position) value.
pub enum OverflowPosition {
    /// If the size of the alignment subject overflows the alignment container,
    /// the alignment subject is instead aligned as if the alignment mode were start.
    safe,
    /// Regardless of the relative sizes of the alignment subject and alignment
    /// container, the given alignment value is honored.
    @"unsafe",

    const css_impl = css.DefineEnumProperty(@This());
    pub const eql = css_impl.eql;
    pub const hash = css_impl.hash;
    pub const parse = css_impl.parse;
    pub const toCss = css_impl.toCss;
    pub const deepClone = css_impl.deepClone;
}

/// A [`<content-position>`](https://www.w3.org/TR/css-align-3/#typedef-content-position) value.
pub enum ContentPosition {
    /// Content is centered within the container.
    center,
    /// Content is aligned to the start of the container.
    start,
    /// Content is aligned to the end of the container.
    end,
    /// Same as `start` when within a flexbox container.
    @"flex-start",
    /// Same as `end` when within a flexbox container.
    @"flex-end",

    const css_impl = css.DefineEnumProperty(@This());
    pub const eql = css_impl.eql;
    pub const hash = css_impl.hash;
    pub const parse = css_impl.parse;
    pub const toCss = css_impl.toCss;
    pub const deepClone = css_impl.deepClone;
}

pub struct SelfPositionInner {
    /// An overflow alignment mode.
    overflow: OverflowPosition?,
    /// A self position keyword.
    value: SelfPosition,

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }
}

pub struct ContentPositionInner {
    /// An overflow alignment mode.
    overflow: OverflowPosition?,
    /// A content position keyword.
    value: ContentPosition,

    static fn eql(lhs: *const @This(), rhs: *const @This()): bool {
        return css.implementEql(@This(), lhs, rhs);
    }
}

const FlexLinePack = css.css_properties.flex.FlexLinePack;
const BoxPack = css.css_properties.flex.BoxPack;
const FlexPack = css.css_properties.flex.FlexPack;
const BoxAlign = css.css_properties.flex.BoxAlign;
const FlexAlign = css.css_properties.flex.FlexAlign;
const FlexItemAlign = css.css_properties.flex.FlexItemAlign;

type AlignContentAndPrefix = [AlignContent, VendorPrefix];
type FlexLinePackAndPrefix = [FlexLinePack, VendorPrefix];
type JustifyContentAndPrefix = [JustifyContent, VendorPrefix];
type BoxPackAndPrefix = [BoxPack, VendorPrefix];
type FlexPackAndPrefix = [FlexPack, VendorPrefix];
type AlignSelfAndPrefix = [AlignSelf, VendorPrefix];
type FlexItemAlignAndPrefix = [FlexItemAlign, VendorPrefix];
type AlignItemsAndPrefix = [AlignItems, VendorPrefix];
type BoxAlignAndPrefix = [BoxAlign, VendorPrefix];
type FlexAlignAndPrefix = [FlexAlign, VendorPrefix];

pub struct AlignHandler {
    align_content: AlignContentAndPrefix? = null,
    flex_line_pack: FlexLinePackAndPrefix? = null,
    justify_content: JustifyContentAndPrefix? = null,
    box_pack: BoxPackAndPrefix? = null,
    flex_pack: FlexPackAndPrefix? = null,
    align_self: AlignSelfAndPrefix? = null,
    flex_item_align: FlexItemAlignAndPrefix? = null,
    justify_self: JustifySelf? = null,
    align_items: AlignItemsAndPrefix? = null,
    box_align: BoxAlignAndPrefix? = null,
    flex_align: FlexAlignAndPrefix? = null,
    justify_items: JustifyItems? = null,
    row_gap: GapValue? = null,
    column_gap: GapValue? = null,
    has_any: bool = false,

    fn handleProperty(*_, property: *const Property, dest: *css.DeclarationList, context: *css.PropertyHandlerContext): bool {
        switch (property.*) {
            .@"align-content" => |*val| {
                _.flex_line_pack = null;
                _.handlePropertyHelper(dest, context, "align_content", &val.*[0], val.*[1]);
            },
            .@"flex-line-pack" => |*val| _.handlePropertyHelper(dest, context, "flex_line_pack", &val.*[0], val.*[1]),
            .@"justify-content" => |*val| {
                _.box_pack = null;
                _.flex_pack = null;
                _.handlePropertyHelper(dest, context, "justify_content", &val.*[0], val.*[1]);
            },
            .@"box-pack" => |*val| _.handlePropertyHelper(dest, context, "box_pack", &val.*[0], val.*[1]),
            .@"flex-pack" => |*val| _.handlePropertyHelper(dest, context, "flex_pack", &val.*[0], val.*[1]),
            .@"place-content" => |*val| {
                _.flex_line_pack = null;
                _.box_pack = null;
                _.flex_pack = null;
                _.handlePropertyMaybeFlush(dest, context, "align_content", &val.@"align", VendorPrefix.NONE);
                _.handlePropertyMaybeFlush(dest, context, "justify_content", &val.justify, VendorPrefix.NONE);
                _.handlePropertyHelper(dest, context, "align_content", &val.@"align", VendorPrefix.NONE);
                _.handlePropertyHelper(dest, context, "justify_content", &val.justify, VendorPrefix.NONE);
            },
            .@"align-self" => |*val| {
                _.flex_item_align = null;
                _.handlePropertyHelper(dest, context, "align_self", &val.*[0], val.*[1]);
            },
            .@"flex-item-align" => |*val| _.handlePropertyHelper(dest, context, "flex_item_align", &val.*[0], val.*[1]),
            .@"justify-self" => |*val| {
                _.justify_self = css.generic.deepClone(@TypeOf(val.*), val, context.allocator);
                _.has_any = true;
            },
            .@"place-self" => |*val| {
                _.flex_item_align = null;
                _.handlePropertyHelper(dest, context, "align_self", &val.@"align", VendorPrefix.NONE);
                _.justify_self = css.generic.deepClone(@TypeOf(val.justify), &val.justify, context.allocator);
            },
            .@"align-items" => |*val| {
                _.box_align = null;
                _.flex_align = null;
                _.handlePropertyHelper(dest, context, "align_items", &val.*[0], val.*[1]);
            },
            .@"box-align" => |*val| _.handlePropertyHelper(dest, context, "box_align", &val.*[0], val.*[1]),
            .@"flex-align" => |*val| _.handlePropertyHelper(dest, context, "flex_align", &val.*[0], val.*[1]),
            .@"justify-items" => |*val| {
                _.justify_items = css.generic.deepClone(@TypeOf(val.*), val, context.allocator);
                _.has_any = true;
            },
            .@"place-items" => |*val| {
                _.box_align = null;
                _.flex_align = null;
                _.handlePropertyHelper(dest, context, "align_items", &val.@"align", VendorPrefix.NONE);
                _.justify_items = css.generic.deepClone(@TypeOf(val.justify), &val.justify, context.allocator);
            },
            .@"row-gap" => |*val| {
                _.row_gap = css.generic.deepClone(@TypeOf(val.*), val, context.allocator);
                _.has_any = true;
            },
            .@"column-gap" => |*val| {
                _.column_gap = css.generic.deepClone(@TypeOf(val.*), val, context.allocator);
                _.has_any = true;
            },
            .gap => |*val| {
                _.row_gap = css.generic.deepClone(@TypeOf(val.row), &val.row, context.allocator);
                _.column_gap = css.generic.deepClone(@TypeOf(val.column), &val.column, context.allocator);
                _.has_any = true;
            },
            .unparsed => |*val| {
                if (isAlignProperty(val.property_id)) {
                    _.flush(dest, context);
                    bun.handleOom(dest.append(context.allocator, property.*));
                } else {
                    return false;
                }
            },
            else => return false,
        }

        return true;
    }

    fn finalize(*_, dest: *css.DeclarationList, context: *css.PropertyHandlerContext) {
        _.flush(dest, context);
    }

    fn flush(*_, dest: *css.DeclarationList, context: *css.PropertyHandlerContext) {
        if (!_.has_any) {
            return;
        }

        _.has_any = false;

        let align_content = bun.take(&_.align_content);
        let justify_content = bun.take(&_.justify_content);
        let align_self = bun.take(&_.align_self);
        let justify_self = bun.take(&_.justify_self);
        let align_items = bun.take(&_.align_items);
        let justify_items = bun.take(&_.justify_items);
        const row_gap = bun.take(&_.row_gap);
        const column_gap = bun.take(&_.column_gap);
        let box_align = bun.take(&_.box_align);
        let box_pack = bun.take(&_.box_pack);
        let flex_line_pack = bun.take(&_.flex_line_pack);
        let flex_pack = bun.take(&_.flex_pack);
        let flex_align = bun.take(&_.flex_align);
        let flex_item_align = bun.take(&_.flex_item_align);

        // 2009 properties
        _.flushPrefixedProperty(dest, context, "box-align", bun.take(&box_align));
        _.flushPrefixedProperty(dest, context, "box-pack", bun.take(&box_pack));

        // 2012 properties
        _.flushPrefixedProperty(dest, context, "flex-pack", bun.take(&flex_pack));
        _.flushPrefixedProperty(dest, context, "flex-align", bun.take(&flex_align));
        _.flushPrefixedProperty(dest, context, "flex-item-align", bun.take(&flex_item_align));
        _.flushPrefixedProperty(dest, context, "flex-line-pack", bun.take(&flex_line_pack));

        _.flushLegacyProperty(dest, context, Feature.align_content, &align_content, null, .{ FlexLinePack, "flex-line-pack" });
        _.flushLegacyProperty(dest, context, Feature.justify_content, &justify_content, .{ BoxPack, "box-pack" }, .{ FlexPack, "flex-pack" });
        if (context.targets.isCompatible(.place_content)) {
            flushShorthandHelper(
                dest,
                context,
                { prop: "place-content", ty: PlaceContent },
                { feature: Feature.align_content, prop: "align-content" },
                &align_content,
                &justify_content,
                { feature: Feature.justify_content, prop: "justify-content" },
            );
        }
        flushStandardPropertyHelper(dest, context, "align-content", bun.take(&align_content), Feature.align_content);
        flushStandardPropertyHelper(dest, context, "justify-content", bun.take(&justify_content), Feature.justify_content);

        _.flushLegacyProperty(dest, context, Feature.align_self, &align_self, null, .{ FlexItemAlign, "flex-item-align" });
        if (context.targets.isCompatible(.place_self)) {
            flushShorthandHelper(dest, context, { prop: "place-self", ty: PlaceSelf }, { feature: Feature.align_self, prop: "align-self" }, &align_self, &justify_self, null);
        }
        flushStandardPropertyHelper(dest, context, "align-self", bun.take(&align_self), Feature.align_self);
        _.flushUnprefixProperty(dest, context, "justify-self", bun.take(&justify_self));

        _.flushLegacyProperty(dest, context, Feature.align_items, &align_items, .{ BoxAlign, "box-align" }, .{ FlexAlign, "flex-align" });
        if (context.targets.isCompatible(css.compat.Feature.place_items)) {
            flushShorthandHelper(dest, context, { prop: "place-items", ty: PlaceItems }, { feature: Feature.align_items, prop: "align-items" }, &align_items, &justify_items, null);
        }
        flushStandardPropertyHelper(dest, context, "align-items", bun.take(&align_items), Feature.align_items);
        _.flushUnprefixProperty(dest, context, "justify-items", bun.take(&justify_items));

        if (row_gap != null && column_gap != null) {
            dest.append(context.allocator, Property{ .gap = Gap{
                row: row_gap.?,
                column: column_gap.?,
            } }) catch |e| bun.handleOom(e);
        } else {
            if (row_gap != null) {
                bun.handleOom(dest.append(context.allocator, Property{ .@"row-gap" = row_gap.? }));
            }

            if (column_gap != null) {
                bun.handleOom(dest.append(context.allocator, Property{ .@"column-gap" = column_gap.? }));
            }
        }
    }

    fn handlePropertyMaybeFlush(*_, dest: *css.DeclarationList, context: *css.PropertyHandlerContext, comptime prop: []const u8, val: auto, vp: VendorPrefix) {
        // If two vendor prefixes for the same property have different
        // values, we need to flush what we have immediately to preserve order.
        if (*v := @field(_, prop)) {
            if (!val.eql(&v[0]) && !bun.bits.contains(VendorPrefix, v[1], vp)) {
                _.flush(dest, context);
            }
        }
    }

    fn handlePropertyHelper(*_, dest: *css.DeclarationList, context: *css.PropertyHandlerContext, comptime prop: []const u8, val: auto, vp: VendorPrefix) {
        _.handlePropertyMaybeFlush(dest, context, prop, val, vp);
        // Otherwise, update the value and add the prefix.
        if (@field(_, prop) != null) {
            const tuple = &@field(_, prop).?;
            @field(tuple.*, "0") = css.generic.deepClone(@TypeOf(val.*), val, context.allocator);
            bun.bits.insert(VendorPrefix, &@field(tuple.*, "1"), vp);
        } else {
            @field(_, prop) = .{ css.generic.deepClone(@TypeOf(val.*), val, context.allocator), vp };
            _.has_any = true;
        }
    }

    // Gets prefixes for standard properties.
    static fn flushPrefixesHelper(context: *css.PropertyHandlerContext, comptime feature: Feature): VendorPrefix {
        let prefix = context.targets.prefixes(VendorPrefix.NONE, feature);
        // Firefox only implemented the 2009 spec prefixed.
        // Microsoft only implemented the 2012 spec prefixed.
        prefix.moz = false;
        prefix.ms = false;
        return prefix;
    }

    static fn flushStandardPropertyHelper(dest: *css.DeclarationList, context: *css.PropertyHandlerContext, comptime prop: []const u8, key: auto, comptime feature: Feature) {
        if (v := key) {
            const val = v[0];
            let prefix = v[1];
            // If we have an unprefixed property, override necessary prefixes.
            prefix = if (prefix.none) flushPrefixesHelper(context, feature) else prefix;
            bun.handleOom(dest.append(context.allocator, @unionInit(Property, prop, .{ val, prefix })));
        }
    }

    fn flushLegacyProperty(
        *_,
        dest: *css.DeclarationList,
        context: *css.PropertyHandlerContext,
        comptime feature: Feature,
        key: auto,
        comptime prop_2009: auto,
        comptime prop_2012: auto,
    ) {
        if (v := key.*) {
            const val = v[0];
            let prefix = v[1];
            // If we have an unprefixed standard property, generate legacy prefixed versions.
            prefix = context.targets.prefixes(prefix, feature);

            if (prefix.none) {
                if (comptime @TypeOf(prop_2009) != @TypeOf(null)) {
                    const p2009 = prop_2009;
                    // 2009 spec, implemented by webkit and firefox.
                    if (targets := context.targets.browsers) {
                        let prefixes_2009 = VendorPrefix{};
                        if (Feature.isFlex2009(targets)) {
                            prefixes_2009.webkit = true;
                        }
                        if (prefix.moz) {
                            prefixes_2009.moz = true;
                        }
                        if (!prefixes_2009.isEmpty()) {
                            const s = brk: {
                                const T = comptime p2009[0];
                                if (comptime T == css.css_properties.flex.BoxOrdinalGroup) break :brk @as(i32?, val);
                                break :brk p2009[0].fromStandard(&val);
                            };
                            if (a := s) {
                                dest.append(context.allocator, @unionInit(Property, p2009[1], .{
                                    a,
                                    prefixes_2009,
                                })) catch |e| bun.handleOom(e);
                            }
                        }
                    }
                }
            }

            // 2012 spec, implemented by microsoft.
            if (prefix.ms) {
                if (comptime @TypeOf(prop_2012) != @TypeOf(null)) {
                    const p2012 = prop_2012;
                    const s = brk: {
                        const T = comptime p2012[0];
                        if (comptime T == css.css_properties.flex.BoxOrdinalGroup) break :brk @as(i32?, val);
                        break :brk p2012[0].fromStandard(&val);
                    };
                    if (q := s) {
                        dest.append(context.allocator, @unionInit(Property, p2012[1], .{
                            q,
                            VendorPrefix.MS,
                        })) catch |e| bun.handleOom(e);
                    }
                }
            }

            // Remove Firefox and IE from standard prefixes.
            prefix.moz = false;
            prefix.ms = false;
        }
    }

    fn flushPrefixedProperty(*_, dest: *css.DeclarationList, context: *css.PropertyHandlerContext, comptime prop: []const u8, key: auto) {
        * = _.has_any;
        if (v := key) {
            const val = v[0];
            const prefix = v[1];
            bun.handleOom(dest.append(context.allocator, @unionInit(Property, prop, .{ val, prefix })));
        }
    }

    fn flushUnprefixProperty(*_, dest: *css.DeclarationList, context: *css.PropertyHandlerContext, comptime prop: []const u8, key: auto) {
        * = _.has_any;
        if (v := key) {
            const val = v;
            bun.handleOom(dest.append(context.allocator, @unionInit(Property, prop, val)));
        }
    }

    static fn flushShorthandHelper(
        dest: *css.DeclarationList,
        context: *css.PropertyHandlerContext,
        comptime prop: auto,
        comptime align_prop: auto,
        align_field: auto,
        justify_val: auto,
        comptime justify_prop: auto,
    ) {
        // Only use shorthand if both align and justify are present
        if (align_field.* != null) {
            const __v1 = &align_field.*.?;
            const align_val = &__v1[0];
            let align_prefix = &__v1[1];
            if (justify_val.* != null) {
                const __v2 = &justify_val.*.?;
                const justify = __v2;

                const intersection = VendorPrefix.bitwiseAnd(align_prefix.*, if (comptime @TypeOf(justify_prop) != @TypeOf(null)) __v2.*[1] else align_prefix.*);
                // Only use shorthand if unprefixed.
                if (intersection.none) {
                    // Add prefixed longhands if needed.
                    align_prefix.* = flushPrefixesHelper(context, align_prop.feature);
                    align_prefix.none = false;
                    if (!align_prefix.isEmpty()) {
                        dest.append(
                            context.allocator,
                            @unionInit(Property, align_prop.prop, .{ css.generic.deepClone(@TypeOf(align_val.*), align_val, context.allocator), align_prefix.* }),
                        ) catch |e| bun.handleOom(e);
                    }

                    if (comptime @TypeOf(justify_prop) != @TypeOf(null)) {
                        const justify_actual = &__v2.*[0];
                        let justify_prefix = &__v2.*[1];
                        justify_prefix.* = flushPrefixesHelper(context, justify_prop.feature);
                        justify_prefix.none = false;

                        if (!justify_prefix.isEmpty()) {
                            dest.append(
                                context.allocator,
                                @unionInit(Property, justify_prop.prop, .{ css.generic.deepClone(@TypeOf(justify_actual.*), justify_actual, context.allocator), justify_prefix.* }),
                            ) catch |e| bun.handleOom(e);
                        }

                        // Add shorthand.
                        const PropTy = prop.ty;
                        const payload = PropTy{
                            @"align": css.generic.deepClone(@TypeOf(align_val.*), align_val, context.allocator),
                            justify: css.generic.deepClone(@TypeOf(justify_actual.*), justify_actual, context.allocator),
                        };
                        dest.append(
                            context.allocator,
                            @unionInit(Property, prop.prop, payload),
                        ) catch |e| bun.handleOom(e);
                    } else {

                        // Add shorthand.
                        const PropTy = prop.ty;
                        const payload = PropTy{
                            @"align": css.generic.deepClone(@TypeOf(align_val.*), align_val, context.allocator),
                            justify: css.generic.deepClone(@TypeOf(justify.*), justify, context.allocator),
                        };
                        dest.append(
                            context.allocator,
                            @unionInit(Property, prop.prop, payload),
                        ) catch |e| bun.handleOom(e);
                    }

                    align_field.* = null;
                    justify_val.* = null;
                }
            }
        }
    }
}

fn isAlignProperty(property_id: css.PropertyId): bool {
    return switch (property_id) {
        .@"align-content",
        .@"flex-line-pack",
        .@"justify-content",
        .@"box-pack",
        .@"flex-pack",
        .@"place-content",
        .@"align-self",
        .@"flex-item-align",
        .@"justify-self",
        .@"place-self",
        .@"align-items",
        .@"box-align",
        .@"flex-align",
        .@"justify-items",
        .@"place-items",
        .@"row-gap",
        .@"column-gap",
        .gap => true,
        else => false,
    };
}

import bun from "bun";
const Allocator = std.mem.Allocator;
