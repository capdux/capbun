import css from "../css_parser.zig";
const Result = css.Result;
const VendorPrefix = css.VendorPrefix;
const Printer = css.Printer;
const PrintErr = css.PrintErr;
const CssColor = css.css_values.color.CssColor;
const CSSNumber = css.css_values.number.CSSNumber;
const CSSNumberFns = css.css_values.number.CSSNumberFns;
const Angle = css.css_values.angle.Angle;
const AnglePercentage = css.css_values.angle.AnglePercentage;
const HorizontalPositionKeyword = css.css_values.position.HorizontalPositionKeyword;
const VerticalPositionKeyword = css.css_values.position.VerticalPositionKeyword;
const Position = css.css_values.position.Position;
const Length = css.css_values.length.Length;
const LengthPercentage = css.css_values.length.LengthPercentage;

pub struct DeinitAllocGuard<T> {
    ptr: *T!,
    allocator: Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.allocator); }
    }
}
const Percentage = css.css_values.percentage.Percentage;
const NumberOrPercentage = css.css_values.percentage.NumberOrPercentage;

struct GradientParseClosure {
    location: css.SourceLocation,
    func: []const u8,
}

struct WebKitColorStopClosure {
    loc: css.SourceLocation,
    function: []const u8,
}

pub struct LineDirectionCorner {
    /// A horizontal position keyword, e.g. `left` or `right`.
    horizontal: HorizontalPositionKeyword,
    /// A vertical position keyword, e.g. `top` or `bottom`.
    vertical: VerticalPositionKeyword,

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const @This()): bool {
        return css.implementEql(@This(), &_, other);
    }
}

pub struct EllipseSize {
    /// The x-radius of the ellipse.
    x: LengthPercentage,
    /// The y-radius of the ellipse.
    y: LengthPercentage,

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const @This()): bool {
        return css.implementEql(@This(), &_, other);
    }
}

/// A CSS [`<gradient>`](https://www.w3.org/TR/css-images-3/#gradients) value.
pub union(enum) Gradient {
    /// A `linear-gradient()`, and its vendor prefix.
    linear: LinearGradient,
    /// A `repeating-linear-gradient()`, and its vendor prefix.
    repeating_linear: LinearGradient,
    /// A `radial-gradient()`, and its vendor prefix.
    radial: RadialGradient,
    /// A `repeating-radial-gradient`, and its vendor prefix.
    repeating_radial: RadialGradient,
    /// A `conic-gradient()`.
    conic: ConicGradient,
    /// A `repeating-conic-gradient()`.
    repeating_conic: ConicGradient,
    /// A legacy `-webkit-gradient()`.
    @"webkit-gradient": WebKitGradient,

    static fn parse(input: *css.Parser): Result(Gradient) {
        const location = input.currentSourceLocation();
        const func = switch (input.expectFunction()) {
            .result => |vv| vv,
            .err => |e| return .{ .err = e },
        };
        return input.parseNestedBlock(Gradient, GradientParseClosure{ location: location, func: func }, struct {
            fn parse(
                closure: GradientParseClosure,
                input_: *css.Parser,
            ): Result(Gradient) {
                const Map = comptime bun.ComptimeEnumMap(enum {
                    @"linear-gradient",
                    @"repeating-linear-gradient",
                    @"radial-gradient",
                    @"repeating-radial-gradient",
                    @"conic-gradient",
                    @"repeating-conic-gradient",
                    @"-webkit-linear-gradient",
                    @"-webkit-repeating-linear-gradient",
                    @"-webkit-radial-gradient",
                    @"-webkit-repeating-radial-gradient",
                    @"-moz-linear-gradient",
                    @"-moz-repeating-linear-gradient",
                    @"-moz-radial-gradient",
                    @"-moz-repeating-radial-gradient",
                    @"-o-linear-gradient",
                    @"-o-repeating-linear-gradient",
                    @"-o-radial-gradient",
                    @"-o-repeating-radial-gradient",
                    @"-webkit-gradient",
                });
                if (matched := Map.getAnyCase(closure.func))
                    switch (matched) {
                        .@"linear-gradient" => {
                            return .{ .result = .{ .linear = switch (LinearGradient.parse(input_, css.VendorPrefix{ .none = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"repeating-linear-gradient" => {
                            return .{ .result = .{ .repeating_linear = switch (LinearGradient.parse(input_, css.VendorPrefix{ .none = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"radial-gradient" => {
                            return .{ .result = .{ .radial = switch (RadialGradient.parse(input_, css.VendorPrefix{ .none = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"repeating-radial-gradient" => {
                            return .{ .result = .{ .repeating_radial = switch (RadialGradient.parse(input_, css.VendorPrefix{ .none = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"conic-gradient" => {
                            return .{ .result = .{ .conic = switch (ConicGradient.parse(input_)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"repeating-conic-gradient" => {
                            return .{ .result = .{ .repeating_conic = switch (ConicGradient.parse(input_)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-webkit-linear-gradient" => {
                            return .{ .result = .{ .linear = switch (LinearGradient.parse(input_, css.VendorPrefix{ .webkit = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-webkit-repeating-linear-gradient" => {
                            return .{ .result = .{ .repeating_linear = switch (LinearGradient.parse(input_, css.VendorPrefix{ .webkit = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-webkit-radial-gradient" => {
                            return .{ .result = .{ .radial = switch (RadialGradient.parse(input_, css.VendorPrefix{ .webkit = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-webkit-repeating-radial-gradient" => {
                            return .{ .result = .{ .repeating_radial = switch (RadialGradient.parse(input_, css.VendorPrefix{ .webkit = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-moz-linear-gradient" => {
                            return .{ .result = .{ .linear = switch (LinearGradient.parse(input_, css.VendorPrefix{ .moz = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-moz-repeating-linear-gradient" => {
                            return .{ .result = .{ .repeating_linear = switch (LinearGradient.parse(input_, css.VendorPrefix{ .moz = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-moz-radial-gradient" => {
                            return .{ .result = .{ .radial = switch (RadialGradient.parse(input_, css.VendorPrefix{ .moz = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-moz-repeating-radial-gradient" => {
                            return .{ .result = .{ .repeating_radial = switch (RadialGradient.parse(input_, css.VendorPrefix{ .moz = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-o-linear-gradient" => {
                            return .{ .result = .{ .linear = switch (LinearGradient.parse(input_, css.VendorPrefix{ .o = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-o-repeating-linear-gradient" => {
                            return .{ .result = .{ .repeating_linear = switch (LinearGradient.parse(input_, css.VendorPrefix{ .o = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-o-radial-gradient" => {
                            return .{ .result = .{ .radial = switch (RadialGradient.parse(input_, css.VendorPrefix{ .o = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-o-repeating-radial-gradient" => {
                            return .{ .result = .{ .repeating_radial = switch (RadialGradient.parse(input_, css.VendorPrefix{ .o = true })) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                        .@"-webkit-gradient" => {
                            return .{ .result = .{ .@"webkit-gradient" = switch (WebKitGradient.parse(input_)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            } } };
                        },
                    }
                else
                    return .{ .err = closure.location.newUnexpectedTokenError(.{ .ident = closure.func }) };
            }
        }.parse);
    }

    fn toCss(dest: *Printer): PrintErr!void {
        let f: []const u8 = undefined;
        let prefix: css.VendorPrefix? = null;
        switch (_) {
            .linear g => { f = "linear-gradient("; prefix = g.vendor_prefix; },
            .repeating_linear g => { f = "repeating-linear-gradient("; prefix = g.vendor_prefix; },
            .radial g => { f = "radial-gradient("; prefix = g.vendor_prefix; },
            .repeating_radial g => { f = "repeating-radial-gradient("; prefix = g.vendor_prefix; },
            .conic => { f = "conic-gradient("; },
            .repeating_conic => { f = "repeating-conic-gradient("; },
            .@"webkit-gradient" => { f = "-webkit-gradient("; },
        }

        if (p := prefix) {
            p.toCss(dest)?;
        }

        dest.writeStr(f)?;

        switch (_) {
            .linear, .repeating_linear => {
                const linear = switch (_) {
                    .linear => &_.linear,
                    .repeating_linear => &_.repeating_linear,
                    else => unreachable,
                };
                linear.toCss(dest, linear.vendor_prefix != css.VendorPrefix{ none: true })?;
            },
            .radial, .repeating_radial => {
                const radial = switch (_) {
                    .radial => &_.radial,
                    .repeating_radial => &_.repeating_radial,
                    else => unreachable,
                };
                radial.toCss(dest)?;
            },
            .conic, .repeating_conic => {
                const conic = switch (_) {
                    .conic => &_.conic,
                    .repeating_conic => &_.repeating_conic,
                    else => unreachable,
                };
                conic.toCss(dest)?;
            },
            .@"webkit-gradient" => {
                _.@"webkit-gradient".toCss(dest)?;
            },
        }

        return dest.writeChar(')');
    }

    /// Attempts to convert the gradient to the legacy `-webkit-gradient()` syntax.
    ///
    /// Returns an error in case the conversion is not possible.
    fn getLegacyWebkit(allocator: Allocator): Gradient? {
        if (wg := WebKitGradient.fromStandard(&_, allocator)) {
            return Gradient{ @"webkit-gradient": wg };
        } else return null;
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const Gradient): bool {
        return css.implementEql(Gradient, &_, other);
    }

    /// Returns the vendor prefix of the gradient.
    fn getVendorPrefix(): VendorPrefix {
        return switch (_) {
            .linear linear => linear.vendor_prefix,
            .repeating_linear linear => linear.vendor_prefix,
            .radial radial => radial.vendor_prefix,
            .repeating_radial radial => radial.vendor_prefix,
            .@"webkit-gradient" => VendorPrefix{ webkit: true },
            else => VendorPrefix{ none: true },
        };
    }

    static fn getPrefixes(tgts: css.targets.Targets, feature: css.prefixes.Feature, prefix: VendorPrefix): VendorPrefix {
        return tgts.prefixes(prefix, feature);
    }

    /// Returns the vendor prefixes needed for the given browser targets.
    fn getNecessaryPrefixes(targets: css.targets.Targets): css.VendorPrefix {
        return switch (_) {
            .linear linear => getPrefixes(targets, .linear_gradient, linear.vendor_prefix),
            .repeating_linear linear => getPrefixes(targets, .repeating_linear_gradient, linear.vendor_prefix),
            .radial radial => getPrefixes(targets, .radial_gradient, radial.vendor_prefix),
            .repeating_radial radial => getPrefixes(targets, .repeating_radial_gradient, radial.vendor_prefix),
            else => VendorPrefix{ none: true },
        };
    }

    /// Returns a copy of the gradient with the given vendor prefix.
    fn getPrefixed(allocator: Allocator, prefix: css.VendorPrefix): Gradient {
        return switch (_) {
            .linear => brk: {
                let x = _.linear.deepClone(allocator);
                x.vendor_prefix = prefix;
                break :brk Gradient{ linear: x };
            },
            .repeating_linear => brk: {
                let x = _.repeating_linear.deepClone(allocator);
                x.vendor_prefix = prefix;
                break :brk Gradient{ repeating_linear: x };
            },
            .radial => brk: {
                let x = _.radial.deepClone(allocator);
                x.vendor_prefix = prefix;
                break :brk Gradient{ radial: x };
            },
            .repeating_radial => brk: {
                let x = _.repeating_radial.deepClone(allocator);
                x.vendor_prefix = prefix;
                break :brk Gradient{ repeating_radial: x };
            },
            else => _.deepClone(allocator),
        };
    }

    /// Returns a fallback gradient for the given color fallback type.
    fn getFallback(allocator: Allocator, kind: css.ColorFallbackKind): Gradient {
        return switch (_) {
            .linear g => Gradient{ linear: g.getFallback(allocator, kind) },
            .repeating_linear g => Gradient{ repeating_linear: g.getFallback(allocator, kind) },
            .radial g => Gradient{ radial: g.getFallback(allocator, kind) },
            .repeating_radial g => Gradient{ repeating_radial: g.getFallback(allocator, kind) },
            .conic g => Gradient{ conic: g.getFallback(allocator, kind) },
            .repeating_conic g => Gradient{ repeating_conic: g.getFallback(allocator, kind) },
            .@"webkit-gradient" g => Gradient{ @"webkit-gradient": g.getFallback(allocator, kind) },
        };
    }

    /// Returns the color fallback types needed for the given browser targets.
    fn getNecessaryFallbacks(targets: css.targets.Targets): css.ColorFallbackKind {
        let fallbacks = css.ColorFallbackKind{};
        switch (_) {
            .linear, .repeating_linear => {
                const linear = switch (_) {
                    .linear => &_.linear,
                    .repeating_linear => &_.repeating_linear,
                    else => unreachable,
                };
                for (*item of linear.items.items) {
                    bun.bits.insert(css.ColorFallbackKind, &fallbacks, item.getNecessaryFallbacks(targets));
                }
            },
            .radial, .repeating_radial => {
                const radial = switch (_) {
                    .radial => &_.radial,
                    .repeating_radial => &_.repeating_radial,
                    else => unreachable,
                };
                for (*item of radial.items.items) {
                    bun.bits.insert(css.ColorFallbackKind, &fallbacks, item.getNecessaryFallbacks(targets));
                }
            },
            .conic, .repeating_conic => {
                const conic = switch (_) {
                    .conic => &_.conic,
                    .repeating_conic => &_.repeating_conic,
                    else => unreachable,
                };
                for (*item of conic.items.items) {
                    bun.bits.insert(css.ColorFallbackKind, &fallbacks, item.getNecessaryFallbacks(targets));
                }
            },
            .@"webkit-gradient" => {},
        }
        return fallbacks;
    }
}

/// A CSS [`linear-gradient()`](https://www.w3.org/TR/css-images-3/#linear-gradients) or `repeating-linear-gradient()`.
pub struct LinearGradient {
    /// The vendor prefixes for the gradient.
    vendor_prefix: VendorPrefix,
    /// The direction of the gradient.
    direction: LineDirection,
    /// The color stops and transition hints for the gradient.
    items: ArrayList(GradientItem(LengthPercentage)),

    static fn parse(input: *css.Parser, vendor_prefix: VendorPrefix): Result(LinearGradient) {
        let direction: LineDirection = { vertical: .bottom };
        if (dir := input.tryParse(LineDirection.parse, .{vendor_prefix != VendorPrefix{ .none = true }}).asValue()) {
            if (e := input.expectComma().asErr()) return .{ .err = e };
            direction = dir;
        }
        const items = switch (parseItems(LengthPercentage, input)) {
            .result => |vv| vv,
            .err => |e| return .{ .err = e },
        };
        return .{ .result = LinearGradient{ .direction = direction, .items = items, .vendor_prefix = vendor_prefix } };
    }

    fn toCss(dest: *Printer, is_prefixed: bool): PrintErr!void {
        const angle: f32 = switch (_.direction) {
            .vertical => |v| switch (v) {
                .bottom => 180.0,
                .top => 0.0,
            },
            .angle => |a| a.toDegrees(),
            else => -1.0,
        };

        // We can omit `to bottom` or `180deg` because it is the default.
        if (angle == 180.0) {
            // todo_stuff.depth
            serializeItems(LengthPercentage, &_.items, dest)?;
        }
        // If we have `to top` or `0deg`, and all of the positions and hints are percentages,
        // we can flip the gradient the other direction and omit the direction.
        else if (angle == 0.0 && dest.minify && brk: {
            for (*item of _.items.items) {
                if (item == .hint && item.hint != .percentage) break :brk false;
                if (item == .color_stop && item.color_stop.position != null && item.color_stop.position.? != .percentage) break :brk false;
            }
            break :brk true;
        }) {
            let flipped_items = ArrayList(GradientItem(LengthPercentage)).initCapacity(
                dest.allocator,
                _.items.items.len,
            ) catch |e| bun.handleOom(e);
            const flipped_guard = DeinitAllocGuard<@TypeOf(flipped_items)>{ ptr: unsafe { &flipped_items }, allocator: dest.allocator };
            * = flipped_guard;

            let i: usize = _.items.items.len;
            while (i > 0) {
                i -= 1;
                const item = &_.items.items[i];
                switch (item.*) {
                    .hint => |*h| switch (h.*) {
                        .percentage => |p| bun.handleOom(flipped_items.append(dest.allocator, { hint: { percentage: { v: 1.0 - p.v } } })),
                        else => unreachable,
                    },
                    .color_stop => |*cs| {
                        let pos: LengthPercentage? = null;
                        if (cs.position != null) {
                            const p = &cs.position.?;
                            pos = switch (p.*) {
                                .percentage => |perc| LengthPercentage{ .percentage = Percentage{ .v = 1.0 - perc.v } },
                                else => unreachable,
                            };
                        }
                        flipped_items.append(dest.allocator, {
                            color_stop: {
                                color: cs.color,
                                position: pos,
                            },
                        }) catch |e| bun.handleOom(e);
                    },
                }
            }

            serializeItems(LengthPercentage, &flipped_items, dest) catch return dest.addFmtError();
        } else {
            if (!_.direction.eql(&LineDirection{ .vertical = .bottom }) &&
                !_.direction.eql(&LineDirection{ .angle = Angle{ .deg = 180.0 } }))
            {
                _.direction.toCss(dest, is_prefixed)?;
                dest.delim(',', false)?;
            }

            serializeItems(LengthPercentage, &_.items, dest) catch return dest.addFmtError();
        }
    }

    fn isCompatible(browsers: css.targets.Browsers): bool {
        for (*item of _.items.items) {
            if (!item.isCompatible(browsers)) return false;
        }
        return true;
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const LinearGradient): bool {
        return css.implementEql(@This(), &_, other);
    }

    fn getFallback(allocator: Allocator, kind: css.ColorFallbackKind): LinearGradient {
        let fallback_items = bun.handleOom(ArrayList(GradientItem(LengthPercentage)).initCapacity(allocator, _.items.items.len));
        fallback_items.items.len = _.items.items.len;
        let i: usize = 0;
        while (i < _.items.items.len) : (i += 1) {
            fallback_items.items[i] = _.items.items[i].getFallback(allocator, kind);
        }

        return LinearGradient{
            direction: _.direction.deepClone(allocator),
            items: fallback_items,
            vendor_prefix: _.vendor_prefix,
        };
    }
}

/// A CSS [`radial-gradient()`](https://www.w3.org/TR/css-images-3/#radial-gradients) or `repeating-radial-gradient()`.
pub struct RadialGradient {
    /// The vendor prefixes for the gradient.
    vendor_prefix: VendorPrefix,
    /// The shape of the gradient.
    shape: EndingShape,
    /// The position of the gradient.
    position: Position,
    /// The color stops and transition hints for the gradient.
    items: ArrayList(GradientItem(LengthPercentage)),

    static fn parse(input: *css.Parser, vendor_prefix: VendorPrefix): Result(RadialGradient) {
        // todo_stuff.depth
        const shape = switch (input.tryParse(EndingShape.parse, {})) {
            .result => |vv| vv,
            .err => null,
        };
        const position = switch (input.tryParse(struct {
            static fn parse(input_: *css.Parser): Result(Position) {
                if (e := input_.expectIdentMatching("at").asErr()) return .{ .err = e };
                return Position.parse(input_);
            }
        }.parse, {})) {
            .result => |v| v,
            .err => null,
        };

        if (shape != null || position != null) {
            if (e := input.expectComma().asErr()) return .{ .err = e };
        }

        const items = switch (parseItems(LengthPercentage, input)) {
            .result => |vv| vv,
            .err => |e| return .{ .err = e },
        };
        return .{ .result = RadialGradient{
                // todo_stuff.depth
                shape: shape ?? EndingShape.default(),
                // todo_stuff.depth
                position: position ?? Position.center(),
                items: items,
                vendor_prefix: vendor_prefix,
            },
        };
    }

    fn toCss(dest: *Printer): PrintErr!void {
        if (!std.@"meta".eql(_.shape, EndingShape.default())) {
            _.shape.toCss(dest)?;
            if (_.position.isCenter()) {
                dest.delim(',', false)?;
            } else {
                dest.writeChar(' ')?;
            }
        }

        if (!_.position.isCenter()) {
            dest.writeStr("at ")?;
            _.position.toCss(dest)?;
            dest.delim(',', false)?;
        }

        serializeItems(LengthPercentage, &_.items, dest)?;
    }

    fn isCompatible(browsers: css.targets.Browsers): bool {
        for (*item of _.items.items) {
            if (!item.isCompatible(browsers)) return false;
        }
        return true;
    }

    fn getFallback(allocator: Allocator, kind: css.ColorFallbackKind): RadialGradient {
        let items = bun.handleOom(ArrayList(GradientItem(LengthPercentage)).initCapacity(allocator, _.items.items.len));
        items.items.len = _.items.items.len;
        let i: usize = 0;
        while (i < _.items.items.len) : (i += 1) {
            items.items[i] = _.items.items[i].getFallback(allocator, kind);
        }

        return RadialGradient{
            shape: _.shape.deepClone(allocator),
            position: _.position.deepClone(allocator),
            items: items,
            vendor_prefix: _.vendor_prefix,
        };
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const RadialGradient): bool {
        return css.implementEql(@This(), &_, other);
    }
}

/// A CSS [`conic-gradient()`](https://www.w3.org/TR/css-images-4/#conic-gradients) or `repeating-conic-gradient()`.
pub struct ConicGradient {
    /// The angle of the gradient.
    angle: Angle,
    /// The position of the gradient.
    position: Position,
    /// The color stops and transition hints for the gradient.
    items: ArrayList(GradientItem(AnglePercentage)),

    static fn parse(input: *css.Parser): Result(ConicGradient) {
        const angle = input.tryParse(struct {
            inline fn parse(i: *css.Parser): Result(Angle) {
                if (e := i.expectIdentMatching("from").asErr()) return .{ .err = e };
                // Spec allows unitless zero angles for gradients.
                // https://w3c.github.io/csswg-drafts/css-images-4/#valdef-conic-gradient-angle
                return Angle.parseWithUnitlessZero(i);
            }
        }.parse, {});

        const position = input.tryParse(struct {
            inline fn parse(i: *css.Parser): Result(Position) {
                if (e := i.expectIdentMatching("at").asErr()) return .{ .err = e };
                return Position.parse(i);
            }
        }.parse, {});

        if (angle.isOk() || position.isOk()) {
            if (e := input.expectComma().asErr()) return .{ .err = e };
        }

        const items = switch (parseItems(AnglePercentage, input)) {
            .result => |vv| vv,
            .err => |e| return .{ .err = e },
        };
        return .{ .result = ConicGradient{
            .angle = angle.unwrapOr(Angle{ .deg = 0.0 }),
            .position = position.unwrapOr(Position.center()),
            .items = items,
        } };
    }

    fn toCss(dest: *Printer): PrintErr!void {
        if (!_.angle.isZero()) {
            dest.writeStr("from ")?;
            _.angle.toCss(dest)?;

            if (_.position.isCenter()) {
                dest.delim(',', false)?;
            } else {
                dest.writeChar(' ')?;
            }
        }

        if (!_.position.isCenter()) {
            dest.writeStr("at ")?;
            _.position.toCss(dest)?;
            dest.delim(',', false)?;
        }

        return serializeItems(AnglePercentage, &_.items, dest)?;
    }

    fn isCompatible(browsers: css.targets.Browsers): bool {
        for (*item of _.items.items) {
            if (!item.isCompatible(browsers)) return false;
        }
        return true;
    }

    fn getFallback(allocator: Allocator, kind: css.ColorFallbackKind): ConicGradient {
        let items = bun.handleOom(ArrayList(GradientItem(AnglePercentage)).initCapacity(allocator, _.items.items.len));
        items.items.len = _.items.items.len;
        let i: usize = 0;
        while (i < _.items.items.len) : (i += 1) {
            items.items[i] = _.items.items[i].getFallback(allocator, kind);
        }

        return ConicGradient{
            angle: _.angle.deepClone(allocator),
            position: _.position.deepClone(allocator),
            items: items,
        };
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const ConicGradient): bool {
        return css.implementEql(@This(), &_, other);
    }
}

pub struct WebKitGradientFromTo {
    p0: [2]f32,
    p1: [2]f32,
}

/// A linear `-webkit-gradient()`.
pub struct WebKitGradientLinear {
    /// The starting point of the gradient.
    @"from": WebKitGradientPoint,
    /// The ending point of the gradient.
    to: WebKitGradientPoint,
    /// The color stops in the gradient.
    stops: ArrayList(WebKitColorStop),

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const @This()): bool {
        return css.implementEql(@This(), &_, other);
    }
}

/// A radial `-webkit-gradient()`.
pub struct WebKitGradientRadial {
    /// The starting point of the gradient.
    @"from": WebKitGradientPoint,
    /// The starting radius of the gradient.
    r0: CSSNumber,
    /// The ending point of the gradient.
    to: WebKitGradientPoint,
    /// The ending radius of the gradient.
    r1: CSSNumber,
    /// The color stops in the gradient.
    stops: ArrayList(WebKitColorStop),

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const @This()): bool {
        return css.implementEql(@This(), &_, other);
    }
}

/// A legacy `-webkit-gradient()`.
pub union(enum) WebKitGradient {
    /// A linear `-webkit-gradient()`.
    linear: WebKitGradientLinear,
    /// A radial `-webkit-gradient()`.
    radial: WebKitGradientRadial,

    static fn parse(input: *css.Parser): Result(WebKitGradient) {
        const location = input.currentSourceLocation();
        const ident = switch (input.expectIdent()) {
            .result vv => vv,
            .err e => return .{ .err = e },
        };
        if (e := input.expectComma().asErr()) return .{ .err = e };

        // todo_stuff.match_ignore_ascii_case
        if (bun.strings.eqlCaseInsensitiveASCIIICheckLength(ident, "linear")) {
            // todo_stuff.depth
            const from_pt = switch (WebKitGradientPoint.parse(input)) {
                .result vv => vv,
                .err e => return .{ .err = e },
            };
            if (e := input.expectComma().asErr()) return .{ .err = e };
            const to_pt = switch (WebKitGradientPoint.parse(input)) {
                .result vv => vv,
                .err e => return .{ .err = e },
            };
            if (e := input.expectComma().asErr()) return .{ .err = e };
            const stops = switch (input.parseCommaSeparated(WebKitColorStop, WebKitColorStop.parse)) {
                .result vv => vv,
                .err e => return .{ .err = e },
            };
            return .{ .result = WebKitGradient{
                .linear = WebKitGradientLinear{
                    @"from": from_pt,
                    to: to_pt,
                    stops: stops,
                },
            } };
        } else if (bun.strings.eqlCaseInsensitiveASCIIICheckLength(ident, "radial")) {
            const from_pt = switch (WebKitGradientPoint.parse(input)) {
                .result vv => vv,
                .err e => return .{ .err = e },
            };
            if (e := input.expectComma().asErr()) return .{ .err = e };
            const r0 = switch (CSSNumberFns.parse(input)) {
                .result vv => vv,
                .err e => return .{ .err = e },
            };
            if (e := input.expectComma().asErr()) return .{ .err = e };
            const to_pt = switch (WebKitGradientPoint.parse(input)) {
                .result vv => vv,
                .err e => return .{ .err = e },
            };
            if (e := input.expectComma().asErr()) return .{ .err = e };
            const r1 = switch (CSSNumberFns.parse(input)) {
                .result vv => vv,
                .err e => return .{ .err = e },
            };
            if (e := input.expectComma().asErr()) return .{ .err = e };
            // todo_stuff.depth
            const stops = switch (input.parseCommaSeparated(WebKitColorStop, WebKitColorStop.parse)) {
                .result vv => vv,
                .err e => return .{ .err = e },
            };
            return .{ .result = WebKitGradient{
                .radial = WebKitGradientRadial{
                    @"from": from_pt,
                    r0: r0,
                    to: to_pt,
                    r1: r1,
                    stops: stops,
                },
            } };
        } else {
            return .{ .err = location.newUnexpectedTokenError(.{ .ident = ident }) };
        }
    }

    fn toCss(dest: *Printer): PrintErr!void {
        switch (_) {
            .linear linear => {
                dest.writeStr("linear")?;
                dest.delim(',', false)?;
                linear.@"from".toCss(dest)?;
                dest.delim(',', false)?;
                linear.to.toCss(dest)?;
                for (*stop of linear.stops.items) {
                    dest.delim(',', false)?;
                    stop.toCss(dest)?;
                }
            },
            .radial radial => {
                dest.writeStr("radial")?;
                dest.delim(',', false)?;
                radial.@"from".toCss(dest)?;
                dest.delim(',', false)?;
                CSSNumberFns.toCss(&radial.r0, dest)?;
                dest.delim(',', false)?;
                radial.to.toCss(dest)?;
                dest.delim(',', false)?;
                CSSNumberFns.toCss(&radial.r1, dest)?;
                for (*stop of radial.stops.items) {
                    dest.delim(',', false)?;
                    stop.toCss(dest)?;
                }
            },
        }
    }

    fn getFallback(allocator: Allocator, kind: css.ColorFallbackKind): WebKitGradient {
        switch (_) {
            .linear linear => {
                let stops = bun.handleOom(ArrayList(WebKitColorStop).initCapacity(allocator, linear.stops.items.len));
                stops.items.len = linear.stops.items.len;
                let idx: usize = 0;
                while (idx < stops.items.len) : (idx += 1) {
                    stops.items[idx] = linear.stops.items[idx].getFallback(allocator, kind);
                }
                return WebKitGradient{
                    .linear = WebKitGradientLinear{
                        @"from": linear.@"from".deepClone(allocator),
                        to: linear.to.deepClone(allocator),
                        stops: stops,
                    },
                };
            },
            .radial radial => {
                let stops = bun.handleOom(ArrayList(WebKitColorStop).initCapacity(allocator, radial.stops.items.len));
                stops.items.len = radial.stops.items.len;
                let idx: usize = 0;
                while (idx < stops.items.len) : (idx += 1) {
                    stops.items[idx] = radial.stops.items[idx].getFallback(allocator, kind);
                }
                return WebKitGradient{
                    .radial = WebKitGradientRadial{
                        @"from": radial.@"from".deepClone(allocator),
                        r0: radial.r0,
                        to: radial.to.deepClone(allocator),
                        r1: radial.r1,
                        stops: stops,
                    },
                };
            },
        }
    }

    static fn fromStandard(gradient: *const Gradient, allocator: Allocator): WebKitGradient? {
        switch (gradient.*) {
            .linear linear => {
                // Convert from line direction to a from and to point, if possible.
                const ft: WebKitGradientFromTo = switch (linear.direction) {
                    .horizontal horizontal => switch (horizontal) {
                        .left => WebKitGradientFromTo{ p0: [ 1.0, 0.0 ], p1: [ 0.0, 0.0 ] },
                        .right => WebKitGradientFromTo{ p0: [ 0.0, 0.0 ], p1: [ 1.0, 0.0 ] },
                    },
                    .vertical vertical => switch (vertical) {
                        .top => WebKitGradientFromTo{ p0: [ 0.0, 1.0 ], p1: [ 0.0, 0.0 ] },
                        .bottom => WebKitGradientFromTo{ p0: [ 0.0, 0.0 ], p1: [ 0.0, 1.0 ] },
                    },
                    .corner corner => switch (corner.horizontal) {
                        .left => switch (corner.vertical) {
                            .top => WebKitGradientFromTo{ p0: [ 1.0, 1.0 ], p1: [ 0.0, 0.0 ] },
                            .bottom => WebKitGradientFromTo{ p0: [ 1.0, 0.0 ], p1: [ 0.0, 1.0 ] },
                        },
                        .right => switch (corner.vertical) {
                            .top => WebKitGradientFromTo{ p0: [ 0.0, 1.0 ], p1: [ 1.0, 0.0 ] },
                            .bottom => WebKitGradientFromTo{ p0: [ 0.0, 0.0 ], p1: [ 1.0, 1.0 ] },
                        },
                    },
                    .angle angle => brk: {
                        const degrees = angle.toDegrees();
                        if (degrees == 0.0) {
                            break :brk WebKitGradientFromTo{ p0: [ 0.0, 1.0 ], p1: [ 0.0, 0.0 ] };
                        } else if (degrees == 90.0) {
                            break :brk WebKitGradientFromTo{ p0: [ 0.0, 0.0 ], p1: [ 1.0, 0.0 ] };
                        } else if (degrees == 180.0) {
                            break :brk WebKitGradientFromTo{ p0: [ 0.0, 0.0 ], p1: [ 0.0, 1.0 ] };
                        } else if (degrees == 270.0) {
                            break :brk WebKitGradientFromTo{ p0: [ 1.0, 0.0 ], p1: [ 0.0, 0.0 ] };
                        } else {
                            return null;
                        }
                    },
                };

                return WebKitGradient{
                    .linear = WebKitGradientLinear{
                        @"from": .{
                            .x = .{ .number = .{ .percentage = .{ .v = ft.p0[0] } } },
                            .y = .{ .number = .{ .percentage = .{ .v = ft.p0[1] } } },
                        },
                        to: .{
                            .x = .{ .number = .{ .percentage = .{ .v = ft.p1[0] } } },
                            .y = .{ .number = .{ .percentage = .{ .v = ft.p1[1] } } },
                        },
                        stops: convertStopsToWebkit(allocator, &linear.items) ?? return null,
                    },
                };
            },
            .radial radial => {
                // Webkit radial gradients are always circles, not ellipses, and must be specified in pixels.
                const radius = switch (radial.shape) {
                    .circle circle => switch (circle) {
                        .radius r => r.toPx() ?? return null,
                        else => return null,
                    },
                    else => return null,
                };

                const x = WebKitGradientPointComponent(HorizontalPositionKeyword).fromPosition(&radial.position.x, allocator) ?? return null;
                const y = WebKitGradientPointComponent(VerticalPositionKeyword).fromPosition(&radial.position.y, allocator) ?? return null;
                const point = WebKitGradientPoint{ x: x, y: y };
                return WebKitGradient{
                    .radial = WebKitGradientRadial{
                        @"from": point.deepClone(allocator),
                        r0: 0.0,
                        to: point,
                        r1: radius,
                        stops: convertStopsToWebkit(allocator, &radial.items) ?? return null,
                    },
                };
            },
            else => return null,
        }
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const WebKitGradient): bool {
        return css.implementEql(@This(), &_, other);
    }
}

/// The direction of a CSS `linear-gradient()`.
///
/// See [LinearGradient](LinearGradient).
pub union(enum) LineDirection {
    /// An angle.
    angle: Angle,
    /// A horizontal position keyword, e.g. `left` or `right`.
    horizontal: HorizontalPositionKeyword,
    /// A vertical position keyword, e.g. `top` or `bottom`.
    vertical: VerticalPositionKeyword,
    /// A corner, e.g. `bottom left` or `top right`.
    corner: LineDirectionCorner,

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const LineDirection): bool {
        return css.implementEql(@This(), &_, other);
    }

    static fn parse(input: *css.Parser, is_prefixed: bool): Result(LineDirection) {
        // Spec allows unitless zero angles for gradients.
        // https://w3c.github.io/csswg-drafts/css-images-3/#linear-gradient-syntax
        if (angle := input.tryParse(Angle.parseWithUnitlessZero, {}).asValue()) {
            return { result: LineDirection{ angle: angle } };
        }

        if (!is_prefixed) {
            if (e := input.expectIdentMatching("to").asErr()) return { err: e };
        }

        if (x := input.tryParse(HorizontalPositionKeyword.parse, {}).asValue()) {
            if (y := input.tryParse(VerticalPositionKeyword.parse, {}).asValue()) {
                return { result: LineDirection{ corner: {
                    horizontal: x,
                    vertical: y,
                } } };
            }
            return { result: LineDirection{ horizontal: x } };
        }

        const y = switch (VerticalPositionKeyword.parse(input)) {
            .result => |vv| vv,
            .err => |e| return { err: e },
        };
        if (x := input.tryParse(HorizontalPositionKeyword.parse, {}).asValue()) {
            return { result: LineDirection{ corner: {
                horizontal: x,
                vertical: y,
            } } };
        }
        return { result: LineDirection{ vertical: y } };
    }

    fn toCss(dest: *Printer, is_prefixed: bool): PrintErr!void {
        switch (_) {
            .angle angle => angle.toCss(dest)?,
            .horizontal k => {
                if (dest.minify) {
                    dest.writeStr(switch (k) {
                        .left => "270deg",
                        .right => "90deg",
                    })?;
                } else {
                    if (!is_prefixed) {
                        dest.writeStr("to ")?;
                    }
                    k.toCss(dest)?;
                }
            },
            .vertical k => {
                if (dest.minify) {
                    dest.writeStr(switch (k) {
                        .top => "0deg",
                        .bottom => "180deg",
                    })?;
                } else {
                    if (!is_prefixed) {
                        dest.writeStr("to ")?;
                    }
                    k.toCss(dest)?;
                }
            },
            .corner c => {
                if (!is_prefixed) {
                    dest.writeStr("to ")?;
                }
                c.vertical.toCss(dest)?;
                dest.writeChar(' ')?;
                c.horizontal.toCss(dest)?;
            },
        }
    }
}

/// Either a color stop or interpolation hint within a gradient.
///
/// This type is generic, and items may be either a [LengthPercentage](super::length::LengthPercentage)
/// or [Angle](super::angle::Angle) depending on what type of gradient it is within.
pub fn GradientItem(comptime D: type): type {
    return union(enum) {
        /// A color stop.
        color_stop: ColorStop(D),
        /// A color interpolation hint.
        hint: D,

        pub fn toCss(this: *const @This(), dest: *Printer): PrintErr!void {
            return switch (this.*) {
                .color_stop => |*c| try c.toCss(dest),
                .hint => |*h| try css.generic.toCss(D, h, dest),
            };
        }

        pub fn eql(this: *const GradientItem(D), other: *const GradientItem(D)): bool {
            return css.implementEql(@This(), this, other);
        }

        pub fn deepClone(this: *const @This(), allocator: std.mem.Allocator): @This() {
            return css.implementDeepClone(@This(), this, allocator);
        }

        pub fn isCompatible(this: *const @This(), browsers: css.targets.Browsers): bool {
            return switch (this.*) {
                .color_stop => |*c| c.color.isCompatible(browsers),
                .hint => css.compat.Feature.isCompatible(.gradient_interpolation_hints, browsers),
            };
        }

        /// Returns a fallback gradient item for the given color fallback type.
        pub fn getFallback(this: *const @This(), allocator: Allocator, kind: css.ColorFallbackKind) GradientItem(D) {
            return switch (this.*) {
                .color_stop => |*stop| blk: {
                    let cs: GradientItem(D) = .{
                        .color_stop = .{
                            .color = stop.color.getFallback(allocator, kind),
                            .position = null,
                        },
                    };
                    if (stop.position != null) {
                        cs.color_stop.position = stop.position.?.deepClone(allocator);
                    }
                    break :blk cs;
                },
                .hint => this.deepClone(allocator),
            };
        }

        /// Returns the color fallback types needed for the given browser targets.
        pub fn getNecessaryFallbacks(this: *const @This(), targets: css.targets.Targets): css.ColorFallbackKind {
            return switch (this.*) {
                .color_stop => |*stop| stop.color.getNecessaryFallbacks(targets),
                .hint => css.ColorFallbackKind{},
            };
        }
    };
}

/// A `radial-gradient()` [ending shape](https://www.w3.org/TR/css-images-3/#valdef-radial-gradient-ending-shape).
///
/// See [RadialGradient](RadialGradient).
pub union(enum) EndingShape {
    /// An ellipse.
    ellipse: Ellipse,
    /// A circle.
    circle: Circle,

    const parse = css.DeriveParse(@This()).parse;
    const toCss = css.DeriveToCss(@This()).toCss;

    static fn default(): EndingShape {
        return { ellipse: { extent: .@"farthest-corner" } };
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const EndingShape): bool {
        return css.implementEql(@This(), &_, other);
    }
}

/// An x/y position within a legacy `-webkit-gradient()`.
pub struct WebKitGradientPoint {
    /// The x-position.
    x: WebKitGradientPointComponent(HorizontalPositionKeyword),
    /// The y-position.
    y: WebKitGradientPointComponent(VerticalPositionKeyword),

    static fn parse(input: *css.Parser): Result(WebKitGradientPoint) {
        const x = switch (WebKitGradientPointComponent(HorizontalPositionKeyword).parse(input)) {
            .result => |v| v,
            .err => |e| return .{ .err = e },
        };
        const y = switch (WebKitGradientPointComponent(VerticalPositionKeyword).parse(input)) {
            .result => |v| v,
            .err => |e| return .{ .err = e },
        };
        return .{ .result = .{ .x = x, .y = y } };
    }

    fn toCss(dest: *Printer): PrintErr!void {
        _.x.toCss(dest)?;
        dest.writeChar(' ')?;
        return _.y.toCss(dest)?;
    }

    fn eql(other: *const WebKitGradientPoint): bool {
        return css.implementEql(@This(), &_, other);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

/// A keyword or number within a [WebKitGradientPoint](WebKitGradientPoint).
pub fn WebKitGradientPointComponent(comptime S: type): type {
    return union(enum) {
        /// The `center` keyword.
        center,
        /// A number or percentage.
        number: NumberOrPercentage,
        /// A side keyword.
        side: S,

        const This = @This();

        pub fn parse(input: *css.Parser) Result(This) {
            if (input.tryParse(css.Parser.expectIdentMatching, .{"center"}).isOk()) {
                return .{ .result = .center };
            }

            if (input.tryParse(NumberOrPercentage.parse, .{}).asValue()) |number| {
                return .{ .result = .{ .number = number } };
            }

            const keyword = switch (css.generic.parse(S, input)) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };
            return .{ .result = .{ .side = keyword } };
        }

        pub fn toCss(this: *const This, dest: *Printer) PrintErr!void {
            switch (this.*) {
                .center => {
                    if (dest.minify) {
                        try dest.writeStr("50%");
                    } else {
                        try dest.writeStr("center");
                    }
                },
                .number => |*lp| {
                    if (lp.* == .percentage and lp.percentage.v == 0.0) {
                        try dest.writeChar('0');
                    } else {
                        try lp.toCss(dest);
                    }
                },
                .side => |*s| {
                    if (dest.minify) {
                        const lp: LengthPercentage = s.intoLengthPercentage();
                        try lp.toCss(dest);
                    } else {
                        try s.toCss(dest);
                    }
                },
            }
        }

        /// Attempts to convert a standard position to a webkit gradient point.
        pub fn fromPosition(this: *const css.css_values.position.PositionComponent(S), allocator: Allocator) ?WebKitGradientPointComponent(S) {
            return switch (this.*) {
                .center => .center,
                .length => |len| .{
                    .number = switch (len) {
                        .percentage => |p| .{ .percentage = p },
                        // Webkit gradient points can only be specified in pixels.
                        .dimension => |*d| if (d.toPx()) |px| .{ .number = px } else return null,
                        else => return null,
                    },
                },
                .side => |s| if (s.offset != null)
                    return null
                else
                    .{
                        .side = s.side.deepClone(allocator),
                    },
            };
        }

        pub fn eql(this: *const This, other: *const This) bool {
            return css.implementEql(@This(), this, other);
        }
    };
}

/// A color stop within a legacy `-webkit-gradient()`.
pub struct WebKitColorStop {
    /// The color of the color stop.
    color: CssColor,
    /// The position of the color stop.
    position: CSSNumber,

    static fn parse(input: *css.Parser): Result(WebKitColorStop) {
        const location = input.currentSourceLocation();
        const function = switch (input.expectFunction()) {
            .result => |vv| vv,
            .err => |e| return .{ .err = e },
        };
        return input.parseNestedBlock(
            WebKitColorStop,
            WebKitColorStopClosure{ loc: location, function: function },
            struct {
                static fn parse(
                    closure: WebKitColorStopClosure,
                    i: *css.Parser,
                ): Result(WebKitColorStop) {
                    // todo_stuff.match_ignore_ascii_case
                    const position: f32 = if (bun.strings.eqlCaseInsensitiveASCIIICheckLength(closure.function, "color-stop")) position: {
                        const p: NumberOrPercentage = switch (@call(.auto, @field(NumberOrPercentage, "parse"), .{i})) {
                            .result => |vv| vv,
                            .err => |e| return .{ .err = e },
                        };
                        if (e := i.expectComma().asErr()) return .{ .err = e };
                        break :position p.intoF32();
                    } else if (bun.strings.eqlCaseInsensitiveASCIIICheckLength(closure.function, "from")) position: {
                        break :position 0.0;
                    } else if (bun.strings.eqlCaseInsensitiveASCIIICheckLength(closure.function, "to")) position: {
                        break :position 1.0;
                    } else {
                        return .{ .err = closure.loc.newUnexpectedTokenError(.{ .ident = closure.function }) };
                    };
                    const color = switch (CssColor.parse(i)) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };
                    return .{ .result = WebKitColorStop{ .color = color, .position = position } };
                }
            }.parse,
        );
    }

    fn toCss(dest: *Printer): PrintErr!void {
        if (_.position == 0.0) {
            dest.writeStr("from(")?;
            _.color.toCss(dest)?;
        } else if (_.position == 1.0) {
            dest.writeStr("to(")?;
            _.color.toCss(dest)?;
        } else {
            dest.writeStr("color-stop(")?;
            css.generic.toCss(CSSNumber, &_.position, dest)?;
            dest.delim(',', false)?;
            _.color.toCss(dest)?;
        }
        dest.writeChar(')')?;
    }

    fn getFallback(allocator: Allocator, kind: css.ColorFallbackKind): WebKitColorStop {
        return WebKitColorStop{
            color: _.color.getFallback(allocator, kind),
            position: _.position,
        };
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const WebKitColorStop): bool {
        return css.implementEql(WebKitColorStop, &_, other);
    }
}

/// A [`<color-stop>`](https://www.w3.org/TR/css-images-4/#color-stop-syntax) within a gradient.
///
/// This type is generic, and may be either a [LengthPercentage](super::length::LengthPercentage)
/// or [Angle](super::angle::Angle) depending on what type of gradient it is within.
pub fn ColorStop(comptime D: type): type {
    return struct {
        /// The color of the color stop.
        color: CssColor,
        /// The position of the color stop.
        position: D?,

        const This = @This();

        pub fn parse(input: *css.Parser): Result(ColorStop(D)) {
            const color = switch (CssColor.parse(input)) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };
            const position = switch (input.tryParse(css.generic.parseFor(D), {})) {
                .result => |v| v,
                .err => null,
            };
            return .{ .result = .{ .color = color, .position = position } };
        }

        pub fn toCss(dest: *Printer): PrintErr!void {
            _.color.toCss(dest)?;
            if (_.position != null) {
                dest.writeChar(' ')?;
                css.generic.toCss(D, &_.position.?, dest)?;
            }
            return;
        }

        pub fn deepClone(this: *const @This(), allocator: std.mem.Allocator): @This() {
            return css.implementDeepClone(@This(), this, allocator);
        }

        pub fn eql(other: *const This): bool {
            return css.implementEql(@This(), &_, other);
        }
    };
}

/// An ellipse ending shape for a `radial-gradient()`.
///
/// See [RadialGradient](RadialGradient).
pub union(enum) Ellipse {
    /// An ellipse with a specified horizontal and vertical radius.
    size: EllipseSize,
    /// A shape extent keyword.
    extent: ShapeExtent,

    static fn parse(input: *css.Parser): Result(Ellipse) {
        if (extent := input.tryParse(ShapeExtent.parse, {}).asValue()) {
            // The `ellipse` keyword is optional, but only if the `circle` keyword is not present.
            // If it is, then we'll re-parse as a circle.
            if (input.tryParse(css.Parser.expectIdentMatching, {"circle"}).isOk()) {
                return { err: input.newErrorForNextToken() };
            }
            * = input.tryParse(css.Parser.expectIdentMatching, {"ellipse"});
            return { result: Ellipse{ extent: extent } };
        }

        if (x := input.tryParse(LengthPercentage.parse, {}).asValue()) {
            const y = switch (LengthPercentage.parse(input)) {
                .result => |vv| vv,
                .err => |e| return { err: e },
            };
            // The `ellipse` keyword is optional if there are two lengths.
            * = input.tryParse(css.Parser.expectIdentMatching, {"ellipse"});
            return { result: Ellipse{ size: { x: x, y: y } } };
        }

        if (input.tryParse(css.Parser.expectIdentMatching, {"ellipse"}).isOk()) {
            if (extent := input.tryParse(ShapeExtent.parse, {}).asValue()) {
                return { result: Ellipse{ extent: extent } };
            }

            if (x := input.tryParse(LengthPercentage.parse, {}).asValue()) {
                const y = switch (LengthPercentage.parse(input)) {
                    .result => |vv| vv,
                    .err => |e| return { err: e },
                };
                return { result: Ellipse{ size: { x: x, y: y } } };
            }

            // Assume `farthest-corner` if only the `ellipse` keyword is present.
            return { result: Ellipse{ extent: .@"farthest-corner" } };
        }

        return { err: input.newErrorForNextToken() };
    }

    fn toCss(dest: *Printer): PrintErr!void {
        // The `ellipse` keyword is optional, so we don't emit it.
        return switch (_) {
            .size s => {
                s.x.toCss(dest)?;
                dest.writeChar(' ')?;
                return s.y.toCss(dest)?;
            },
            .extent e => e.toCss(dest)?,
        };
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const Ellipse): bool {
        return css.implementEql(@This(), &_, other);
    }
}

pub enum ShapeExtent {
    /// The closest side of the box to the gradient's center.
    @"closest-side",
    /// The farthest side of the box from the gradient's center.
    @"farthest-side",
    /// The closest corner of the box to the gradient's center.
    @"closest-corner",
    /// The farthest corner of the box from the gradient's center.
    @"farthest-corner",

    fn eql(other: *const ShapeExtent): bool {
        return _ == other.*;
    }

    fn asStr(): []const u8 {
        return css.enum_property_util.asStr(@This(), &_);
    }

    static fn parse(input: *css.Parser): Result(@This()) {
        return css.enum_property_util.parse(@This(), input);
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn toCss(dest: *Printer): PrintErr!void {
        return css.enum_property_util.toCss(@This(), &_, dest);
    }
}

/// A circle ending shape for a `radial-gradient()`.
///
/// See [RadialGradient](RadialGradient).
pub union(enum) Circle {
    /// A circle with a specified radius.
    radius: Length,
    /// A shape extent keyword.
    extent: ShapeExtent,

    static fn parse(input: *css.Parser): Result(Circle) {
        if (extent := input.tryParse(ShapeExtent.parse, {}).asValue()) {
            // The `circle` keyword is required. If it's not there, then it's an ellipse.
            if (e := input.expectIdentMatching("circle").asErr()) return { err: e };
            return { result: Circle{ extent: extent } };
        }

        if (length := input.tryParse(Length.parse, {}).asValue()) {
            // The `circle` keyword is optional if there is only a single length.
            // We are assuming here that Ellipse.parse ran first.
            * = input.tryParse(css.Parser.expectIdentMatching, {"circle"});
            return { result: Circle{ radius: length } };
        }

        if (input.tryParse(css.Parser.expectIdentMatching, {"circle"}).isOk()) {
            if (extent := input.tryParse(ShapeExtent.parse, {}).asValue()) {
                return { result: Circle{ extent: extent } };
            }

            if (length := input.tryParse(Length.parse, {}).asValue()) {
                return { result: Circle{ radius: length } };
            }

            // If only the `circle` keyword was given, default to `farthest-corner`.
            return { result: Circle{ extent: .@"farthest-corner" } };
        }

        return { err: input.newErrorForNextToken() };
    }

    fn toCss(dest: *Printer): PrintErr!void {
        return switch (_) {
            .radius r => r.toCss(dest)?,
            .extent extent => {
                dest.writeStr("circle")?;
                if (extent != .@"farthest-corner") {
                    dest.writeChar(' ')?;
                    extent.toCss(dest)?;
                }
            },
        };
    }

    fn deepClone(allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const Circle): bool {
        return css.implementEql(@This(), &_, other);
    }
}

pub fn parseItems(comptime D: type, input: *css.Parser): Result(ArrayList(GradientItem(D))) {
    const ItemList = ArrayList(GradientItem(D));
    let items: ItemList = {};
    let seen_stop = false;

    while (true) {
        const Closure = struct { items: *ArrayList(GradientItem(D)), seen_stop: *bool };
        if (e := input.parseUntilBefore(
            css.Delimiters{ .comma = true },
            void,
            Closure{ .items = &items, .seen_stop = &seen_stop },
            struct {
                fn parse(closure: Closure, i: *css.Parser): Result(void) {
                    if (closure.seen_stop.*) {
                        if (hint := i.tryParse(comptime css.generic.parseFor(D), {}).asValue()) {
                            closure.seen_stop.* = false;
                            bun.handleOom(closure.items.append(i.allocator(), { hint: hint }));
                            return .success;
                        }
                    }

                    const stop = switch (ColorStop(D).parse(i)) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };

                    if (position := i.tryParse(comptime css.generic.parseFor(D), {}).asValue()) {
                        const color = stop.color.deepClone(i.allocator());
                        bun.handleOom(closure.items.append(i.allocator(), { color_stop: stop }));
                        closure.items.append(i.allocator(), { color_stop: {
                            color: color,
                            position: position,
                        } }) catch |e| bun.handleOom(e);
                    } else {
                        bun.handleOom(closure.items.append(i.allocator(), { color_stop: stop }));
                    }

                    closure.seen_stop.* = true;
                    return .success;
                }
            }.parse,
        ).asErr()) return .{ .err = e };

        if (tok := input.next().asValue()) {
            if (tok.* == .comma) continue;
            bun.unreachablePanic("expected a comma after parsing a gradient", {});
        } else {
            break;
        }
    }

    return .{ .result = items };
}

pub fn serializeItems(
    comptime D: type,
    items: *const ArrayList(GradientItem(D)),
    dest: *Printer,
): PrintErr!void {
    let first = true;
    const Item = GradientItem(D);
    let last: (*const Item)? = null;
    let i: usize = 0;
    while (i < items.items.len) : (i += 1) {
        const item = &items.items[i];
        // Skip useless hints
        if (item.* == .hint && item.hint == .percentage && item.hint.percentage.v == 0.5) {
            continue;
        }

        // Use double position stop if the last stop is the same color and all targets support it.
        if (prev := last) {
            if (!dest.targets.shouldCompile(.double_position_gradients, { double_position_gradients: true })) {
                if (prev.* == .color_stop && prev.color_stop.position != null &&
                    item.* == .color_stop && item.color_stop.position != null &&
                    prev.color_stop.color.eql(&item.color_stop.color))
                {
                    dest.writeChar(' ')?;
                    item.color_stop.position.?.toCss(dest)?;
                    last = null;
                    continue;
                }
            }
        }

        if (first) {
            first = false;
        } else {
            dest.delim(',', false)?;
        }
        item.toCss(dest)?;
        last = item;
    }
}

pub fn convertStopsToWebkit(allocator: Allocator, items: *const ArrayList(GradientItem(LengthPercentage))): ArrayList(WebKitColorStop)? {
    let stops: ArrayList(WebKitColorStop) = bun.handleOom(ArrayList(WebKitColorStop).initCapacity(allocator, items.items.len));
    let i: usize = 0;
    while (i < items.items.len) : (i += 1) {
        const item = &items.items[i];
        switch (item.*) {
            .color_stop => |*stop| {
                // webkit stops must always be percentage based, not length based.
                let position: f32 = 0.0;
                if (pos := stop.position) {
                    position = switch (pos) {
                        .percentage => |percentage| percentage.v,
                        else => {
                            stops.deinit(allocator);
                            return null;
                        },
                    };
                } else if (i == 0) {
                    position = 0.0;
                } else if (i == items.items.len - 1) {
                    position = 1.0;
                } else {
                    stops.deinit(allocator);
                    return null;
                }

                stops.append(allocator, {
                    color: stop.color.deepClone(allocator),
                    position: position,
                }) catch return null;
            },
            else => return null,
        }
    }

    return stops;
}

import bun from "bun";

const ArrayList = std.ArrayListUnmanaged;
const Allocator = std.mem.Allocator;
