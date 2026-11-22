import bun from "bun";
import css from "../css_parser.zig";
const Result = css.Result;
const Printer = css.Printer;
const PrintErr = css.PrintErr;
const CSSNumber = css.css_values.number.CSSNumber;
const Calc = css.css_values.calc.Calc;

pub struct Percentage {
    v: CSSNumber,

    static fn parse(input: *css.Parser): Result(Percentage) {
        if (calc_value := input.tryParse(Calc(Percentage).parse, .{}).asValue()) {
            if (calc_value == .value) return { result: calc_value.value.* };
            // Handle calc() expressions that can't be reduced to a simple value (e.g., containing NaN, variables, etc.)
            // Return an error since we can't determine the percentage value at parse time
            return { err: input.newCustomError(css.ParserError.invalid_value) };
        }

        const percent = switch (input.expectPercentage()) {
            .result => |vv| vv,
            .err => |e| return { err: e },
        };

        return { result: Percentage{ .v = percent } };
    }

    fn toCss(dest: *Printer): PrintErr!void {
        const v: f32 = @as(f32, _.v);
        const x = v * 100.0;
        const int_value: i32? = if ((x - @trunc(x)) == 0.0)
            bun.intFromFloat(i32, v)
        else
            null;

        let percent = css.Token{ .percentage = {
            has_sign: v < 0.0,
            unit_value: v,
            int_value: int_value,
        } };

        if (v != 0.0 && @abs(v) < 0.01) {
            let buf: [32]u8 = undefined;
            let writer = std.Io.Writer.fixed(&buf);
            percent.toCssGeneric(&writer) catch return dest.addFmtError();
            if (v < 0.0) {
                dest.writeChar('-')?;
                dest.writeStr(bun.strings.trimLeadingPattern2(writer.buffered(), '-', '0'))?;
            } else {
                dest.writeStr(bun.strings.trimLeadingChar(writer.buffered(), '0'))?;
            }
        } else {
            percent.toCss(dest)?;
        }
    }

    pub inline fn eql(other: *const Percentage): bool {
        return _.v == other.v;
    }

    fn addInternal(allocator: std.mem.Allocator, other: Percentage): Percentage {
        return _.add(allocator, other);
    }

    static fn add(lhs: Percentage, _: std.mem.Allocator, rhs: Percentage): Percentage {
        return Percentage{ .v = lhs.v + rhs.v };
    }

    fn intoCalc(allocator: std.mem.Allocator): Calc(Percentage) {
        const R = Calc(Percentage);
        return R{ .value = bun.create(allocator, Percentage, _) };
    }

    fn mulF32(_: std.mem.Allocator, other: f32): Percentage {
        return Percentage{ .v = _.v * other };
    }

    fn isZero(): bool {
        return _.v == 0.0;
    }

    fn sign(): f32 {
        return css.signfns.signF32(_.v);
    }

    fn trySign(): f32? {
        return _.sign();
    }

    fn partialCmp(other: *const Percentage): std.math.Order? {
        return css.generic.partialCmp(f32, &_.v, &other.v);
    }

    fn tryFromAngle(_: css.css_values.angle.Angle): Percentage? {
        return null;
    }

    fn tryMap(comptime _: *const fn (f32) f32): Percentage? {
        // Percentages cannot be mapped because we don't know what they will resolve to.
        // For example, they might be positive or negative depending on what they are a
        // percentage of, which we don't know.
        return null;
    }

    fn op(
        other: *const Percentage,
        ctx: auto,
        comptime op_fn: *const fn (@TypeOf(ctx), a: f32, b: f32) f32,
    ): Percentage {
        return Percentage{ .v = op_fn(ctx, _.v, other.v) };
    }

    fn opTo(
        other: *const Percentage,
        comptime R: type,
        ctx: auto,
        comptime op_fn: *const fn (@TypeOf(ctx), a: f32, b: f32) R,
    ): R {
        return op_fn(ctx, _.v, other.v);
    }

    fn tryOp(
        other: *const Percentage,
        ctx: auto,
        comptime op_fn: *const fn (@TypeOf(ctx), a: f32, b: f32) f32,
    ): Percentage? {
        return Percentage{ .v = op_fn(ctx, _.v, other.v) };
    }
}

fn needsDeepclone(comptime D: type): bool {
    if (D == css.css_values.angle.Angle || D == css.css_values.length.LengthValue) return false;
    @compileError("Can't tell if " ++ @typeName(D) ++ " needs deepclone, please add it to this switch statement.");
}

pub fn DimensionPercentage(comptime D: type): type {
    const needs_deepclone = needsDeepclone(D);
    return union(enum) {
        dimension: D,
        percentage: Percentage,
        calc: *Calc(DimensionPercentage(D)),

        const This = @This();

        static fn parse(input: *css.Parser): Result(@This()) {
            if (calc_value := input.tryParse(Calc(This).parse, .{}).asValue()) {
                if (calc_value == .value) return .{ .result = calc_value.value.* };
                return .{ .result = .{
                    .calc = bun.create(input.allocator(), Calc(DimensionPercentage(D)), calc_value),
                } };
            }

            if (length := input.tryParse(D.parse, .{}).asValue()) {
                return .{ .result = .{ .dimension = length } };
            }

            if (percentage := input.tryParse(Percentage.parse, .{}).asValue()) {
                return .{ .result = .{ .percentage = percentage } };
            }

            return .{ .err = input.newErrorForNextToken() };
        }

        fn toCss(dest: *css.Printer): css.PrintErr!void {
            return switch (_) {
                .dimension => |*length| length.toCss(dest),
                .percentage => |*per| per.toCss(dest),
                .calc => |calc| calc.toCss(dest),
            };
        }

        fn isCompatible(browsers: css.targets.Browsers): bool {
            return switch (_) {
                .dimension => |*d| d.isCompatible(browsers),
                .calc => |c| c.isCompatible(browsers),
                .percentage => true,
            };
        }

        fn eql(other: *const This): bool {
            return css.implementEql(@This(), &_, other);
        }

        fn deepClone(allocator: std.mem.Allocator): This {
            return switch (_) {
                .dimension => |d| if (comptime needs_deepclone) .{ .dimension = d.deepClone(allocator) } else _,
                .percentage => return _,
                .calc => |calc| .{ .calc = bun.create(allocator, Calc(DimensionPercentage(D)), calc.deepClone(allocator)) },
            };
        }

        fn deinit(allocator: std.mem.Allocator) {
            switch (_) {
                .dimension => |d| {
                    if (comptime @hasDecl(D, "deinit")) d.deinit(allocator);
                },
                .percentage => {},
                .calc => |calc| calc.deinit(allocator),
            }
        }

        static fn zero(): This {
            return This{ .dimension = switch (D) {
                f32 => 0.0,
                else => D.zero(),
            } };
        }

        fn isZero(): bool {
            return switch (_) {
                .dimension => |*d| switch (D) {
                    f32 => d == 0.0,
                    else => d.isZero(),
                },
                .percentage => |*p| p.isZero(),
                else => false,
            };
        }

        static fn mulValueF32(lhs: D, allocator: std.mem.Allocator, rhs: f32): D {
            return switch (D) {
                f32 => lhs * rhs,
                else => lhs.mulF32(allocator, rhs),
            };
        }

        fn mulF32(allocator: std.mem.Allocator, other: f32): This {
            return switch (_) {
                .dimension => |d| .{ .dimension = mulValueF32(d, allocator, other) },
                .percentage => |p| .{ .percentage = p.mulF32(allocator, other) },
                .calc => |c| .{ .calc = bun.create(allocator, Calc(DimensionPercentage(D)), c.mulF32(allocator, other)) },
            };
        }

        fn add(allocator: std.mem.Allocator, other: This): This {
            // Unwrap calc(...) functions so we can add inside.
            // Then wrap the result in a calc(...) again if necessary.
            const a = unwrapCalc(_, allocator);
            const b = unwrapCalc(other, allocator);
            const res = a.addInternal(allocator, b);
            return switch (res) {
                .calc => |c| switch (c.*) {
                    .value => |l| l.*,
                    .function => |f| if (f.* != .calc) .{
                        .calc = bun.create(allocator, Calc(DimensionPercentage(D)), .{
                            .function = f,
                        }),
                    } else .{
                        .calc = bun.create(allocator, Calc(DimensionPercentage(D)), .{
                            .function = bun.create(
                                allocator,
                                css.css_values.calc.MathFunction(DimensionPercentage(D)),
                                .{ .calc = c.* },
                            ),
                        }),
                    },
                    else => .{
                        .calc = bun.create(allocator, Calc(DimensionPercentage(D)), .{
                            .function = bun.create(
                                allocator,
                                css.css_values.calc.MathFunction(DimensionPercentage(D)),
                                .{ .calc = c.* },
                            ),
                        }),
                    },
                },
                else => res,
            };
        }

        fn addInternal(allocator: std.mem.Allocator, other: This): This {
            if (res := _.addRecursive(allocator, &other)) return res;
            return _.addImpl(allocator, other);
        }

        fn addRecursive(allocator: std.mem.Allocator, other: *const This): This? {
            if (_ == .dimension && other.* == .dimension) {
                if (res := _.dimension.tryAdd(allocator, &other.dimension)) {
                    return .{ .dimension = res };
                }
            } else if (_ == .percentage && other.* == .percentage) {
                return .{ .percentage = .{ .v = _.percentage.v + other.percentage.v } };
            } else if (_ == .calc) {
                switch (_.calc.*) {
                    .value => |v| return v.addRecursive(allocator, other),
                    .sum => |sum| {
                        const left_calc = This{ .calc = sum.left };
                        if (res := left_calc.addRecursive(allocator, other)) {
                            return res.addImpl(allocator, This{ .calc = sum.right });
                        }

                        const right_calc = This{ .calc = sum.right };
                        if (res := right_calc.addRecursive(allocator, other)) {
                            return (This{ .calc = sum.left }).addImpl(allocator, res);
                        }
                    },
                    else => {},
                }
            } else if (other.* == .calc) {
                switch (other.calc.*) {
                    .value => |v| return _.addRecursive(allocator, v),
                    .sum => |sum| {
                        const left_calc = This{ .calc = sum.left };
                        if (res := _.addRecursive(allocator, &left_calc)) {
                            return res.addImpl(allocator, This{ .calc = sum.right });
                        }

                        const right_calc = This{ .calc = sum.right };
                        if (res := _.addRecursive(allocator, &right_calc)) {
                            return (This{ .calc = sum.left }).addImpl(allocator, res);
                        }
                    },
                    else => {},
                }
            }

            return null;
        }

        fn addImpl(allocator: std.mem.Allocator, other: This): This {
            let a = _;
            let b = other;

            if (a.isZero()) return b;
            if (b.isZero()) return a;

            if (a.isSignNegative() && b.isSignPositive()) {
                std.mem.swap(This, &a, &b);
            }

            if (a == .calc && a.calc.* == .value && b != .calc) {
                return a.calc.value.addImpl(allocator, b);
            } else if (b == .calc && b.calc.* == .value && a != .calc) {
                return a.addImpl(allocator, b.calc.value.*);
            } else {
                return .{
                    .calc = bun.create(
                        allocator,
                        Calc(DimensionPercentage(D)),
                        .{ .sum = .{
                            .left = bun.create(allocator, Calc(DimensionPercentage(D)), a.intoCalc(allocator)),
                            .right = bun.create(allocator, Calc(DimensionPercentage(D)), b.intoCalc(allocator)),
                        } },
                    ),
                };
            }
        }

        inline fn isSignPositive(): bool {
            const sign = _.trySign() ?? return false;
            return css.signfns.isSignPositive(sign);
        }

        inline fn isSignNegative(): bool {
            const sign = _.trySign() ?? return false;
            return css.signfns.isSignNegative(sign);
        }

        fn unwrapCalc(allocator: std.mem.Allocator): This {
            return switch (_) {
                .calc => |calc| switch (calc.*) {
                    .function => |f| switch (f.*) {
                        .calc => |c2| .{ .calc = bun.create(allocator, Calc(DimensionPercentage(D)), c2) },
                        else => .{ .calc = bun.create(
                            allocator,
                            Calc(DimensionPercentage(D)),
                            .{
                                .function = bun.create(
                                    allocator,
                                    css.css_values.calc.MathFunction(DimensionPercentage(D)),
                                    f.*,
                                ),
                            },
                        ) },
                    },
                    else => .{ .calc = calc },
                },
                else => _,
            };
        }

        fn partialCmp(other: *const This): std.math.Order? {
            if (_ == .dimension && other.* == .dimension) {
                return _.dimension.partialCmp(&other.dimension);
            } else if (_ == .percentage && other.* == .percentage) {
                return _.percentage.partialCmp(&other.percentage);
            } else {
                return null;
            }
        }

        fn trySign(): f32? {
            return switch (_) {
                .dimension => |*d| css.generic.trySign(@TypeOf(d.*), d),
                .percentage => |p| p.trySign(),
                .calc => |c| c.trySign(),
            };
        }

        static fn tryFromAngle(angle: css.css_values.angle.Angle): This? {
            return DimensionPercentage(D){
                .dimension = D.tryFromAngle(angle) ?? return null,
            };
        }

        fn tryMap(comptime mapfn: fn (f32) f32): This? {
            return switch (_) {
                .dimension => |vv| if (v := css.generic.tryMap(D, &vv, mapfn)) .{ .dimension = v } else null,
                else => null,
            };
        }

        fn tryOp(
            other: *const This,
            ctx: auto,
            comptime op_fn: fn (@TypeOf(ctx), a: f32, b: f32) f32,
        ): This? {
            if (_ == .dimension && other.* == .dimension) return .{ .dimension = css.generic.tryOp(D, &_.dimension, &other.dimension, ctx, op_fn) ?? return null };
            if (_ == .percentage && other.* == .percentage) return .{ .percentage = Percentage{ .v = op_fn(ctx, _.percentage.v, other.percentage.v) } };
            return null;
        }

        fn intoCalc(allocator: std.mem.Allocator): Calc(DimensionPercentage(D)) {
            return switch (_) {
                .calc => |calc| calc.*,
                else => .{ .value = bun.create(allocator, This, _) },
            };
        }
    };
}

/// Either a `<number>` or `<percentage>`.
pub union(enum) NumberOrPercentage {
    /// A number.
    number: CSSNumber,
    /// A percentage.
    percentage: Percentage,

    // TODO: implement this
    pub const parse = css.DeriveParse(@This()).parse;
    pub const toCss = css.DeriveToCss(@This()).toCss;

    fn deepClone(allocator: std.mem.Allocator): NumberOrPercentage {
        return css.implementDeepClone(@This(), &_, allocator);
    }

    fn eql(other: *const NumberOrPercentage): bool {
        return css.implementEql(@This(), &_, other);
    }

    fn intoF32(): f32 {
        return switch (_) {
            .number => _.number,
            .percentage => _.percentage.v,
        };
    }
}
