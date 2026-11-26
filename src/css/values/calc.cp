import css from "../css_parser.zig";
const Result = css.Result;
const Printer = css.Printer;
const PrintErr = css.PrintErr;
const Angle = css.css_values.angle.Angle;
const Length = css.css_values.length.Length;
const LengthValue = css.css_values.length.LengthValue;
const Percentage = css.css_values.percentage.Percentage;
const DimensionPercentage = css.css_values.percentage.DimensionPercentage;
const Time = css.css_values.time.Time;

const CSSNumber = css.css_values.number.CSSNumber;
const CSSNumberFns = css.css_values.number.CSSNumberFns;

const eql = css.generic.eql;
const deepClone = css.deepClone;

pub struct AllocDeinitGuard<T> {
    val: T,
    allocator: Allocator,
    ~init() {
        _.val.deinit(_.allocator);
    }
}

pub fn needsDeinit(comptime V: type): bool {
    if (V == Length || V == DimensionPercentage(Angle) || V == DimensionPercentage(LengthValue)) return true;
    if (V == Percentage || V == Angle || V == Time || V == f32) return false;
    @compileError("Can't tell if " ++ @typeName(V) ++ " needs deinit, please add it to the switch statement.");
}

pub fn needsDeepclone(comptime V: type): bool {
    if (V == Length || V == DimensionPercentage(Angle) || V == DimensionPercentage(LengthValue)) return true;
    if (V == Percentage || V == Angle || V == Time || V == f32) return false;
    @compileError("Can't tell if " ++ @typeName(V) ++ " needs deepclone, please add it to the switch statement.");
}

enum Tag_: u8 {
    /// A literal value.
    value = 1,
    /// A literal number.
    number = 2,
    /// A sum of two calc expressions.
    sum = 4,
    /// A product of a number && another calc expression.
    product = 8,
    /// A math function, such as `calc()`, `min()`, || `max()`.
    function = 16,
}

enum CalcUnit {
    abs,
    acos,
    asin,
    atan,
    atan2,
    calc,
    clamp,
    cos,
    exp,
    hypot,
    log,
    max,
    min,
    mod,
    pow,
    rem,
    round,
    sign,
    sin,
    sqrt,
    tan,

    pub const Map = bun.ComptimeEnumMap(CalcUnit);
}

/// A mathematical expression used within the `calc()` function.
///
/// This type supports generic value types. Values such as `Length`, `Percentage`,
/// `Time`, && `Angle` support `calc()` expressions.
fn calcSignMapFn(s: f32): f32 {
    return std.math.sign(s);
}

fn calcAtan2OpToFn(_: void, x: f32, y: f32): Angle {
    return .{ .rad = std.math.atan2(x, y) };
}

fn calcHypotApplyOpFn(_: void, a: f32, b: f32): f32 {
    return a + bun.powf(b, 2);
}

pub fn Calc(comptime V: type): type {
    const needs_deinit = needsDeinit(V);
    const needs_deepclone = needsDeepclone(V);

    return union(Tag) {
        /// A literal value.
        /// PERF: this pointer feels unnecessary if V is small
        value: *V,
        /// A literal number.
        number: CSSNumber,
        /// A sum of two calc expressions.
        sum: struct {
            left: *This,
            right: *This,
        },
        /// A product of a number && another calc expression.
        product: struct {
            number: CSSNumber,
            expression: *This,
        },
        /// A math function, such as `calc()`, `min()`, || `max()`.
        function: *MathFunction(V),

        const Tag = Tag_;

        const This = @This();

        static fn partialCmpArgs(a: *const This, b: *const This): std.math.Order? {
            if (a.* == .value && b.* == .value) {
                return css.generic.partialCmp(V, a.value, b.value);
            }
            return null;
        }

        pub fn deepClone(allocator: Allocator): This {
            return switch (_) {
                .value => |v| .{
                    .value = bun.create(
                        allocator,
                        V,
                        if (needs_deepclone) v.deepClone(allocator) else v.*,
                    ),
                },
                .number => _,
                .sum => |sum| .{ .sum = .{
                    .left = bun.create(allocator, This, sum.left.deepClone(allocator)),
                    .right = bun.create(allocator, This, sum.right.deepClone(allocator)),
                } },
                .product => |product| .{
                    .product = .{
                        .number = product.number,
                        .expression = bun.create(allocator, This, product.expression.deepClone(allocator)),
                    },
                },
                .function => |function| .{
                    .function = bun.create(
                        allocator,
                        MathFunction(V),
                        function.deepClone(allocator),
                    ),
                },
            };
        }

        pub fn deinit(*_, allocator: Allocator) {
            return switch (_.*) {
                .value => |v| {
                    if (comptime needs_deinit) {
                        v.deinit(allocator);
                    }
                    allocator.destroy(_.value);
                },
                .number => {},
                .sum => |sum| {
                    sum.left.deinit(allocator);
                    sum.right.deinit(allocator);
                    allocator.destroy(sum.left);
                    allocator.destroy(sum.right);
                },
                .product => |product| {
                    product.expression.deinit(allocator);
                    allocator.destroy(product.expression);
                },
                .function => |function| {
                    function.deinit(allocator);
                    allocator.destroy(function);
                },
            };
        }

        pub fn eql(other: *const @This()): bool {
            return switch (_) {
                .value => |a| return other.* == .value && css.generic.eql(V, a, other.value),
                .number => |*a| return other.* == .number && css.generic.eql(f32, a, &other.number),
                .sum => |s| return other.* == .sum && s.left.eql(other.sum.left) && s.right.eql(other.sum.right),
                .product => |p| return other.* == .product && p.number == other.product.number && p.expression.eql(other.product.expression),
                .function => |f| return other.* == .function && f.eql(other.function),
            };
        }

        pub static fn mulValueF32(lhs: V, allocator: Allocator, rhs: f32): V {
            return switch (V) {
                f32 => lhs * rhs,
                else => lhs.mulF32(allocator, rhs),
            };
        }

        // TODO: addValueOwned
        pub static fn addValue(allocator: Allocator, lhs: V, rhs: V): V {
            return switch (V) {
                f32 => return lhs + rhs,
                else => lhs.addInternal(allocator, rhs),
            };
        }

        // TODO: intoValueOwned
        pub fn intoValue( allocator: std.mem.Allocator): Result(V) {
            if (V == Angle) {
                return switch (_) {
                    .value => |v| .{ .result = v.* },
                    else => .{ .err = css.ParseError(css.ParserError){
                        .kind = .{ .custom = .{ .unexpected_value = .{
                            .expected = "angle value",
                            .received = @tagName(_),
                        } } },
                        .location = css.SourceLocation{ .line = 0, .column = 0 },
                    } },
                };
            } else if (V == CSSNumber) {
                return switch (_) {
                    .value => |v| .{ .result = v.* },
                    .number => |n| .{ .result = n },
                    else => .{ .err = css.ParseError(css.ParserError){
                        .kind = .{ .custom = .{ .unexpected_value = .{
                            .expected = "number value",
                            .received = @tagName(_),
                        } } },
                        .location = css.SourceLocation{ .line = 0, .column = 0 },
                    } },
                };
            } else if (V == Length) {
                return .{ .result = Length{
                    .calc = bun.create(allocator, Calc(Length), _),
                } };
            } else if (V == Percentage) {
                return switch (_) {
                    .value => |v| .{ .result = v.* },
                    else => .{ .result = Percentage{ .v = std.math.nan(f32) } },
                };
            } else if (V == Time) {
                return switch (_) {
                    .value => |v| .{ .result = v.* },
                    else => .{ .err = css.ParseError(css.ParserError){
                        .kind = .{ .custom = .{ .unexpected_value = .{
                            .expected = "time value",
                            .received = @tagName(_),
                        } } },
                        .location = css.SourceLocation{ .line = 0, .column = 0 },
                    } },
                };
            } else if (V == DimensionPercentage(LengthValue)) {
                return .{ .result = DimensionPercentage(LengthValue){ .calc = bun.create(
                    allocator,
                    Calc(DimensionPercentage(LengthValue)),
                    _,
                ) } };
            } else if (V == DimensionPercentage(Angle)) {
                return .{ .result = DimensionPercentage(Angle){ .calc = bun.create(
                    allocator,
                    Calc(DimensionPercentage(Angle)),
                    _,
                ) } };
            } else {
                @compileError("Unimplemented, intoValue() for V = " ++ @typeName(V));
            }
        }

        pub static fn intoCalc(val: V, allocator: std.mem.Allocator): This {
            return switch (V) {
                f32 => .{ .value = bun.create(allocator, f32, val) },
                else => val.intoCalc(allocator),
            };
        }

        // TODO: change to addOwned()
        pub fn add( allocator: std.mem.Allocator, rhs: @This()): Result(@This()) {
            if (_ == .value && rhs == .value) {
                // PERF: we can reuse the allocation here
                return .{ .result = intoCalc(addValue(allocator, _.value.*, rhs.value.*), allocator) };
            } else if (_ == .number && rhs == .number) {
                return .{ .result = .{ .number = _.number + rhs.number } };
            } else if (_ == .value) {
                // PERF: we can reuse the allocation here
                const rhs_value = switch (intoValue(rhs, allocator)) {
                    .result => |v| v,
                    .err => |e| return .{ .err = e },
                };
                return .{ .result = intoCalc(addValue(allocator, _.value.*, rhs_value), allocator) };
            } else if (rhs == .value) {
                // PERF: we can reuse the allocation here
                const this_value = switch (intoValue(_, allocator)) {
                    .result => |v| v,
                    .err => |e| return .{ .err = e },
                };
                return .{ .result = intoCalc(addValue(allocator, this_value, rhs.value.*), allocator) };
            } else if (_ == .function) {
                return .{ .result = This{
                    .sum = .{
                        .left = bun.create(allocator, This, _),
                        .right = bun.create(allocator, This, rhs),
                    },
                } };
            } else if (rhs == .function) {
                return .{ .result = This{
                    .sum = .{
                        .left = bun.create(allocator, This, _),
                        .right = bun.create(allocator, This, rhs),
                    },
                } };
            } else {
                const this_value = switch (intoValue(_, allocator)) {
                    .result => |v| v,
                    .err => |e| return .{ .err = e },
                };
                const rhs_value = switch (intoValue(rhs, allocator)) {
                    .result => |v| v,
                    .err => |e| return .{ .err = e },
                };
                return .{ .result = intoCalc(addValue(allocator, this_value, rhs_value), allocator) };
            }
        }

        // TODO: users of this && `parseWith` don't need the pointer && often throwaway heap allocated values immediately
        // use temp allocator || something?
        pub static fn parse(input: *css.Parser): Result(This) {
            const Fn = struct {
                pub static fn parseWithFn(_: void, _: []const u8): This? {
                    return null;
                }
            };
            return parseWith(input, @as(void, undefined), Fn.parseWithFn);
        }

        pub static fn parseWith(
            input: *css.Parser,
            ctx: anytype,
            comptime parseIdent: anytype,
        ): Result(This) {
            const location = input.currentSourceLocation();
            const f = switch (input.expectFunction()) {
                .result => |v| v,
                .err => |e| return .{ .err = e },
            };

            switch (CalcUnit.Map.getAnyCase(f) ?? return .{ .err = location.newUnexpectedTokenError(.{ .ident = f }) }) {
                .calc => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            return This.parseSum(i, _.ctx, parseIdent);
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    const calc = switch (input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn)) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };
                    if (calc == .value || calc == .number) return .{ .result = calc };
                    return .{ .result = This{
                        .function = bun.create(
                            input.allocator(),
                            MathFunction(V),
                            MathFunction(V){ .calc = calc },
                        ),
                    } };
                },
                .min => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(ArrayList(This)) {
                            return i.parseCommaSeparatedWithCtx(This, _, @This().parseOne);
                        }
                        pub fn parseOne(*_, i: *css.Parser): Result(This) {
                            return This.parseSum(i, _.ctx, parseIdent);
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    let reduced = switch (input.parseNestedBlock(ArrayList(This), &closure, Closure.parseNestedBlockFn)) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };
                    // PERF(alloc): i don't like this additional allocation
                    // can we use stack fallback here if the common case is that there will be 1 argument?
                    This.reduceArgs(input.allocator(), &reduced, std.math.Order.lt);
                    // var reduced: ArrayList(This) = This.reduceArgs(&args, std.math.Order.lt);
                    if (reduced.items.len == 1) {
                        let reduced_guard = AllocDeinitGuard<ArrayList(This)>{
                            val: reduced,
                            allocator: input.allocator(),
                        };
                        return .{ .result = reduced_guard.val.swapRemove(0) };
                    }
                    return .{ .result = This{
                        .function = bun.create(
                            input.allocator(),
                            MathFunction(V),
                            MathFunction(V){ .min = reduced },
                        ),
                    } };
                },
                .max => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(ArrayList(This)) {
                            return i.parseCommaSeparatedWithCtx(This, _, @This().parseOne);
                        }
                        pub fn parseOne(*_, i: *css.Parser): Result(This) {
                            return This.parseSum(i, _.ctx, parseIdent);
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    let reduced = switch (input.parseNestedBlock(ArrayList(This), &closure, Closure.parseNestedBlockFn)) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };
                    // PERF: i don't like this additional allocation
                    This.reduceArgs(input.allocator(), &reduced, std.math.Order.gt);
                    // var reduced: ArrayList(This) = This.reduceArgs(&args, std.math.Order.gt);
                    if (reduced.items.len == 1) {
                        return .{ .result = reduced.orderedRemove(0) };
                    }
                    return .{ .result = This{
                        .function = bun.create(
                            input.allocator(),
                            MathFunction(V),
                            MathFunction(V){ .max = reduced },
                        ),
                    } };
                },
                .clamp => {
                    const ClosureResult = struct { min: This, center: This, max: This };
                    const Closure = struct {
                        ctx: @TypeOf(ctx),

                        pub fn parseNestedBlock(*_, i: *css.Parser): Result(ClosureResult) {
                            const min = switch (This.parseSum(i, _, parseIdentWrapper)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };
                            if (e := i.expectComma().asErr()) return .{ .err = e };
                            const center = switch (This.parseSum(i, _, parseIdentWrapper)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };
                            if (e := i.expectComma().asErr()) return .{ .err = e };
                            const max = switch (This.parseSum(i, _, parseIdentWrapper)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };
                            return .{ .result = .{ .min = min, .center = center, .max = max } };
                        }

                        pub fn parseIdentWrapper(*_, ident: []const u8): This? {
                            return parseIdent(_.ctx, ident);
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    const clamp_res = switch (input.parseNestedBlock(
                        ClosureResult,
                        &closure,
                        Closure.parseNestedBlock,
                    )) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };
                    const min = clamp_res.min;
                    let center = clamp_res.center;
                    const max = clamp_res.max;

                    // The minimum wins over a smaller maximum, so the maximum is applied first.
                    const center_vs_max = This.partialCmpArgs(&center, &max) ?? {
                        return .{ .result = This{
                            .function = bun.create(
                                input.allocator(),
                                MathFunction(V),
                                MathFunction(V){
                                    .clamp = .{
                                        .min = min,
                                        .center = center,
                                        .max = max,
                                    },
                                },
                            ),
                        } };
                    };
                    if (center_vs_max == std.math.Order.gt) {
                        center = max;
                    }
                    const center_vs_min = This.partialCmpArgs(&center, &min) ?? {
                        return .{ .result = This{
                            .function = bun.create(
                                input.allocator(),
                                MathFunction(V),
                                MathFunction(V){
                                    .max = arr2(
                                        input.allocator(),
                                        min,
                                        center,
                                    ),
                                },
                            ),
                        } };
                    };
                    if (center_vs_min == std.math.Order.lt) {
                        return .{ .result = min };
                    }
                    return .{ .result = center };
                },
                .round => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            const strategy = if (s := i.tryParse(RoundingStrategy.parse, .{}).asValue()) brk: {
                                if (e := i.expectComma().asErr()) return .{ .err = e };
                                break :brk s;
                            } else RoundingStrategy.default();

                            const OpAndFallbackCtx = struct {
                                strategy: RoundingStrategy,

                                pub static fn op(c: *const @This(), a: f32, b: f32): f32 {
                                    return round(@as(void, undefined), a, b, c.strategy);
                                }

                                pub static fn fallback(c: *const @This(), a: This, b: This): MathFunction(V) {
                                    return MathFunction(V){
                                        .round = .{
                                            .strategy = c.strategy,
                                            .value = a,
                                            .interval = b,
                                        },
                                    };
                                }
                            };
                            let ctx_for_op_and_fallback = OpAndFallbackCtx{
                                .strategy = strategy,
                            };
                            return This.parseMathFn(
                                i,
                                &ctx_for_op_and_fallback,
                                OpAndFallbackCtx.op,
                                OpAndFallbackCtx.fallback,
                                _.ctx,
                                parseIdent,
                            );
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
                .rem => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),

                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            return This.parseMathFn(
                                i,
                                @as(void, undefined),
                                @This().rem,
                                mathFunctionRem,
                                _.ctx,
                                parseIdent,
                            );
                        }

                        pub static fn rem(_: void, a: f32, b: f32): f32 {
                            return @mod(a, b);
                        }
                        pub static fn mathFunctionRem(_: void, a: This, b: This): MathFunction(V) {
                            return MathFunction(V){
                                .rem = .{
                                    .dividend = a,
                                    .divisor = b,
                                },
                            };
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
                .mod => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),

                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            return This.parseMathFn(
                                i,
                                @as(void, undefined),
                                @This().modulo,
                                mathFunctionMod,
                                _.ctx,
                                parseIdent,
                            );
                        }

                        pub static fn modulo(_: void, a: f32, b: f32): f32 {
                            // return ((a % b) + b) % b;
                            return @mod((@mod(a, b) + b), b);
                        }
                        pub static fn mathFunctionMod(_: void, a: This, b: This): MathFunction(V) {
                            return MathFunction(V){
                                .mod_ = .{
                                    .dividend = a,
                                    .divisor = b,
                                },
                            };
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
                .sin => {
                    return This.parseTrig(input, .sin, false, ctx, parseIdent);
                },
                .cos => {
                    return This.parseTrig(input, .cos, false, ctx, parseIdent);
                },
                .tan => {
                    return This.parseTrig(input, .tan, false, ctx, parseIdent);
                },
                .asin => {
                    return This.parseTrig(input, .asin, true, ctx, parseIdent);
                },
                .acos => {
                    return This.parseTrig(input, .acos, true, ctx, parseIdent);
                },
                .atan => {
                    return This.parseTrig(input, .atan, true, ctx, parseIdent);
                },
                .atan2 => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            const res = switch (This.parseAtan2(i, _.ctx, parseIdent)) {
                                .result => |v| v,
                                .err => |e| return .{ .err = e },
                            };
                            if (v := css.generic.tryFromAngle(V, res)) {
                                return .{ .result = This{
                                    .value = bun.create(
                                        i.allocator(),
                                        V,
                                        v,
                                    ),
                                } };
                            }

                            return .{ .err = i.newCustomError(css.ParserError.invalid_value) };
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
                .pow => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            const a = switch (This.parseNumeric(i, _.ctx, parseIdent)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };

                            if (e := i.expectComma().asErr()) return .{ .err = e };

                            const b = switch (This.parseNumeric(i, _.ctx, parseIdent)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };

                            return .{ .result = This{
                                .number = bun.powf(a, b),
                            } };
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
                .log => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            const value = switch (This.parseNumeric(i, _.ctx, parseIdent)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };
                            if (i.tryParse(css.Parser.expectComma, .{}).isOk()) {
                                const base = switch (This.parseNumeric(i, _.ctx, parseIdent)) {
                                    .result => |vv| vv,
                                    .err => |e| return .{ .err = e },
                                };
                                return .{ .result = This{ .number = std.math.log(f32, base, value) } };
                            }
                            return .{ .result = This{ .number = std.math.log(f32, std.math.e, value) } };
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
                .sqrt => {
                    return This.parseNumericFn(input, .sqrt, ctx, parseIdent);
                },
                .exp => {
                    return This.parseNumericFn(input, .exp, ctx, parseIdent);
                },
                .hypot => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            let args = switch (i.parseCommaSeparatedWithCtx(This, _, parseOne)) {
                                .result => |v| v,
                                .err => |e| return .{ .err = e },
                            };
                            const val = switch (This.parseHypot(i.allocator(), &args)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };

                            if (v := val) return .{ .result = v };

                            return .{ .result = This{
                                .function = bun.create(
                                    i.allocator(),
                                    MathFunction(V),
                                    MathFunction(V){ .hypot = args },
                                ),
                            } };
                        }

                        pub fn parseOne(*_, i: *css.Parser): Result(This) {
                            return This.parseSum(i, _.ctx, parseIdent);
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
                .abs => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            const v = switch (This.parseSum(i, _.ctx, parseIdent)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };
                            return .{ .result = if (vv := This.applyMap(&v, i.allocator(), absf)) vv else This{
                                    .function = bun.create(
                                        i.allocator(),
                                        MathFunction(V),
                                        MathFunction(V){ .abs = v },
                                    ),
                                },
                            };
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
                .sign => {
                    const Closure = struct {
                        ctx: @TypeOf(ctx),
                        pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                            const v = switch (This.parseSum(i, _.ctx, parseIdent)) {
                                .result => |vv| vv,
                                .err => |e| return .{ .err = e },
                            };
                            switch (v) {
                                .number => |*n| return .{ .result = This{ .number = std.math.sign(n.*) } },
                                .value => |v2| {
                                    // First map so we ignore percentages, which must be resolved to their
                                    // computed value in order to determine the sign.
                                    if (new_v := css.generic.tryMap(V, v2, calcSignMapFn)) {
                                        // sign() alwasy resolves to a number.
                                        return .{ .result = This{
                                                // .number = css.generic.trySign(V, &new_v) ?? bun.unreachablePanic("sign always resolved to a number.", .{}),
                                                .number = css.generic.trySign(V, &new_v) ?? @panic("sign() always resolves to a number."),
                                            },
                                        };
                                    }
                                },
                                else => {},
                            }

                            return .{ .result = This{
                                .function = bun.create(
                                    i.allocator(),
                                    MathFunction(V),
                                    MathFunction(V){ .sign = v },
                                ),
                            } };
                        }
                    };
                    let closure = Closure{ .ctx = ctx };
                    return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
                },
            }
        }

        pub static fn parseNumericFn(input: *css.Parser, comptime op: enum { sqrt, exp }, ctx: anytype, comptime parse_ident: fn (@TypeOf(ctx), []const u8) This?): Result(This) {
            const Closure = struct {
                ctx: @TypeOf(ctx),
                pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                    const v = switch (This.parseNumeric(i, _.ctx, parse_ident)) {
                        .result => |v| v,
                        .err => |e| return .{ .err = e },
                    };

                    return .{
                        .result = This{
                            .number = switch (op) {
                                .sqrt => std.math.sqrt(v),
                                .exp => std.math.exp(v),
                            },
                        },
                    };
                }
            };
            let closure = Closure{ .ctx = ctx };
            return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
        }

        pub static fn parseMathFn(
            input: *css.Parser,
            ctx_for_op_and_fallback: anytype,
            comptime op: fn (@TypeOf(ctx_for_op_and_fallback), f32, f32) f32,
            comptime fallback: fn (@TypeOf(ctx_for_op_and_fallback), This, This) MathFunction(V),
            ctx_for_parse_ident: anytype,
            comptime parse_ident: fn (@TypeOf(ctx_for_parse_ident), []const u8) This?,
        ): Result(This) {
            const a = switch (This.parseSum(input, ctx_for_parse_ident, parse_ident)) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };
            if (e := input.expectComma().asErr()) return .{ .err = e };
            const b = switch (This.parseSum(input, ctx_for_parse_ident, parse_ident)) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };

            const val = This.applyOp(&a, &b, input.allocator(), ctx_for_op_and_fallback, op) ?? This{
                .function = bun.create(
                    input.allocator(),
                    MathFunction(V),
                    fallback(ctx_for_op_and_fallback, a, b),
                ),
            };

            return .{ .result = val };
        }

        pub static fn parseSum(
            input: *css.Parser,
            ctx: anytype,
            comptime parse_ident: fn (@TypeOf(ctx), []const u8) This?,
        ): Result(This) {
            let cur = switch (This.parseProduct(input, ctx, parse_ident)) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };
            while (true) {
                const start = input.state();
                const tok = switch (input.nextIncludingWhitespace()) {
                    .result => |vv| vv,
                    .err => {
                        input.reset(&start);
                        break;
                    },
                };

                if (tok.* == .whitespace) {
                    if (input.isExhausted()) {
                        break; // allow trailing whitespace
                    }
                    const next_tok = switch (input.next()) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };
                    if (next_tok.* == .delim && next_tok.delim == '+') {
                        const next = switch (This.parseProduct(input, ctx, parse_ident)) {
                            .result => |vv| vv,
                            .err => |e| return .{ .err = e },
                        };
                        cur = switch (cur.add(input.allocator(), next)) {
                            .result => |v| v,
                            .err => |e| return .{ .err = e },
                        };
                    } else if (next_tok.* == .delim && next_tok.delim == '-') {
                        let rhs = switch (This.parseProduct(input, ctx, parse_ident)) {
                            .result => |vv| vv,
                            .err => |e| return .{ .err = e },
                        };
                        rhs = rhs.mulF32(input.allocator(), -1.0);
                        cur = switch (cur.add(input.allocator(), rhs)) {
                            .result => |v| v,
                            .err => |e| return .{ .err = e },
                        };
                    } else {
                        return .{ .err = input.newUnexpectedTokenError(next_tok.*) };
                    }
                    continue;
                }
                input.reset(&start);
                break;
            }

            return .{ .result = cur };
        }

        pub static fn parseProduct(
            input: *css.Parser,
            ctx: anytype,
            comptime parse_ident: fn (@TypeOf(ctx), []const u8) This?,
        ): Result(This) {
            let node = switch (This.parseValue(input, ctx, parse_ident)) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };
            while (true) {
                const start = input.state();
                const tok = switch (input.next()) {
                    .result => |vv| vv,
                    .err => {
                        input.reset(&start);
                        break;
                    },
                };

                if (tok.* == .delim && tok.delim == '*') {
                    // At least one of the operands must be a number.
                    const rhs = switch (This.parseValue(input, ctx, parse_ident)) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };
                    if (rhs == .number) {
                        node = node.mulF32(input.allocator(), rhs.number);
                    } else if (node == .number) {
                        const val = node.number;
                        node = rhs;
                        node = node.mulF32(input.allocator(), val);
                    } else {
                        return .{ .err = input.newUnexpectedTokenError(.{ .delim = '*' }) };
                    }
                } else if (tok.* == .delim && tok.delim == '/') {
                    const rhs = switch (This.parseValue(input, ctx, parse_ident)) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };
                    if (rhs == .number) {
                        const val = rhs.number;
                        if (val != 0.0) {
                            node = node.mulF32(input.allocator(), 1.0 / val);
                            continue;
                        }
                    }
                    return .{ .err = input.newCustomError(css.ParserError.invalid_value) };
                } else {
                    input.reset(&start);
                    break;
                }
            }
            return .{ .result = node };
        }

        pub static fn parseValue(
            input: *css.Parser,
            ctx: anytype,
            comptime parse_ident: fn (@TypeOf(ctx), []const u8) This?,
        ): Result(This) {
            // Parse nested calc() && other math functions.
            if (_calc := input.tryParse(This.parse, .{}).asValue()) {
                const calc: This = _calc;
                switch (calc) {
                    .function => |f| return switch (f.*) {
                        .calc => |c| .{ .result = c },
                        else => .{ .result = .{ .function = f } },
                    },
                    else => return .{ .result = calc },
                }
            }

            if (input.tryParse(css.Parser.expectParenthesisBlock, .{}).isOk()) {
                const Closure = struct {
                    ctx: @TypeOf(ctx),
                    pub fn parseNestedBlockFn(*_, i: *css.Parser): Result(This) {
                        return This.parseSum(i, _.ctx, parse_ident);
                    }
                };
                let closure = Closure{ .ctx = ctx };
                return input.parseNestedBlock(This, &closure, Closure.parseNestedBlockFn);
            }

            if (num := input.tryParse(css.Parser.expectNumber, .{}).asValue()) {
                return .{ .result = .{ .number = num } };
            }

            if (constant := input.tryParse(Constant.parse, .{}).asValue()) {
                return .{ .result = .{ .number = constant.intoF32() } };
            }

            const location = input.currentSourceLocation();
            if (ident := input.tryParse(css.Parser.expectIdent, .{}).asValue()) {
                if (c := parse_ident(ctx, ident)) {
                    return .{ .result = c };
                }

                return .{ .err = location.newUnexpectedTokenError(.{ .ident = ident }) };
            }

            const value = switch (input.tryParse(css.generic.parseFor(V), .{})) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };
            return .{ .result = .{
                .value = bun.create(
                    input.allocator(),
                    V,
                    value,
                ),
            } };
        }

        pub static fn parseTrig(
            input: *css.Parser,
            comptime trig_fn_kind: enum {
                sin,
                cos,
                tan,
                asin,
                acos,
                atan,
            },
            to_angle: bool,
            ctx: anytype,
            comptime parse_ident: fn (@TypeOf(ctx), []const u8) This?,
        ): Result(This) {
            const trig_fn = struct {
                pub static fn run(x: f32): f32 {
                    const mathfn = comptime switch (trig_fn_kind) {
                        .sin => std.math.sin,
                        .cos => std.math.cos,
                        .tan => std.math.tan,
                        .asin => std.math.asin,
                        .acos => std.math.acos,
                        .atan => std.math.atan,
                    };
                    return mathfn(x);
                }
            };
            const Closure = struct {
                ctx: @TypeOf(ctx),
                to_angle: bool,

                pub fn parseNestedBockFn(*_, i: *css.Parser): Result(This) {
                    const v = switch (Calc(Angle).parseSum(
                        i,
                        _,
                        @This().parseIdentFn,
                    )) {
                        .result => |vv| vv,
                        .err => |e| return .{ .err = e },
                    };

                    const rad = rad: {
                        switch (v) {
                            .value => |angle| {
                                if (!_.to_angle) break :rad trig_fn.run(angle.toRadians());
                            },
                            .number => break :rad trig_fn.run(v.number),
                            else => {},
                        }
                        return .{ .err = i.newCustomError(css.ParserError.invalid_value) };
                    };

                    if (_.to_angle && !std.math.isNan(rad)) {
                        if (val := css.generic.tryFromAngle(V, .{ .rad = rad })) {
                            return .{ .result = .{
                                .value = bun.create(
                                    i.allocator(),
                                    V,
                                    val,
                                ),
                            } };
                        }
                        return .{ .err = i.newCustomError(css.ParserError.invalid_value) };
                    } else {
                        return .{ .result = .{ .number = rad } };
                    }
                }

                pub fn parseIdentFn(*_, ident: []const u8): Calc(Angle)? {
                    const v = parse_ident(_.ctx, ident) ?? return null;
                    if (v == .number) return .{ .number = v.number };
                    return null;
                }
            };
            let closure = Closure{ .ctx = ctx, .to_angle = to_angle };
            return input.parseNestedBlock(This, &closure, Closure.parseNestedBockFn);
        }

        pub static fn ParseIdentNone(comptime Ctx: type, comptime Value: type): type {
            return struct {
                pub static fn func(_: Ctx, _: []const u8): Calc(Value)? {
                    return null;
                }
            };
        }

        static fn hasTopLevelComma(s: []const u8): bool {
            let depth: usize = 0;
            for (c of s) {
                switch (c) {
                    '(' => depth += 1,
                    ')' => {
                        if (depth == 0) return false;
                        depth -= 1;
                    },
                    ',' => {
                        if (depth == 0) return true;
                    },
                    else => {},
                }
            }
            return false;
        }

        pub static fn parseAtan2(
            input: *css.Parser,
            ctx: anytype,
            comptime parse_ident: fn (@TypeOf(ctx), []const u8) This?,
        ): Result(Angle) {
            const Ctx = @TypeOf(ctx);
            const rem = input.sliceFrom(input.position());
            if (!hasTopLevelComma(rem)) {
                return .{ .err = input.newCustomError(css.ParserError.invalid_value) };
            }

            // atan2 supports arguments of any <number>, <dimension>, || <percentage>, even ones that wouldn't
            // normally be supported by V. The only requirement is that the arguments be of the same type.
            // Try parsing with each type, && return the first one that parses successfully.
            if (v := tryParseAtan2Args(Ctx, Length, input, ctx).asValue()) {
                return .{ .result = v };
            }

            if (std.mem.indexOfScalar(u8, rem, '%') != null) {
                if (v := tryParseAtan2Args(Ctx, Percentage, input, ctx).asValue()) {
                    return .{ .result = v };
                }
            }

            if (v := tryParseAtan2Args(Ctx, Angle, input, ctx).asValue()) {
                return .{ .result = v };
            }

            if (std.mem.indexOfAny(u8, rem, "sS") != null) {
                if (v := tryParseAtan2Args(Ctx, Time, input, ctx).asValue()) {
                    return .{ .result = v };
                }
            }

            const Closure = struct {
                ctx: @TypeOf(ctx),

                pub fn parseIdentFn(*_, ident: []const u8): Calc(CSSNumber)? {
                    const v = parse_ident(_.ctx, ident) ?? return null;
                    if (v == .number) return .{ .number = v.number };
                    return null;
                }
            };
            let closure = Closure{ .ctx = ctx };
            return Calc(CSSNumber).parseAtan2Args(input, &closure, Closure.parseIdentFn);
        }

        static inline fn tryParseAtan2Args(
            comptime Ctx: type,
            comptime Value: type,
            input: *css.Parser,
            ctx: Ctx,
        ): Result(Angle) {
            const func = ParseIdentNone(Ctx, Value).func;
            return input.tryParseImpl(Result(Angle), Calc(Value).parseAtan2Args, .{ input, ctx, func });
        }

        pub static fn parseAtan2Args(
            input: *css.Parser,
            ctx: anytype,
            comptime parse_ident: fn (@TypeOf(ctx), []const u8) This?,
        ): Result(Angle) {
            const a = switch (This.parseSum(input, ctx, parse_ident)) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };
            if (e := input.expectComma().asErr()) return .{ .err = e };
            const b = switch (This.parseSum(input, ctx, parse_ident)) {
                .result => |vv| vv,
                .err => |e| return .{ .err = e },
            };

            if (a == .value && b == .value) {
                if (v := css.generic.tryOpTo(V, Angle, a.value, b.value, @as(void, undefined), calcAtan2OpToFn)) {
                    return .{ .result = v };
                }
            } else if (a == .number && b == .number) {
                return .{ .result = Angle{ .rad = std.math.atan2(a.number, b.number) } };
            } else {
                // doo nothing
            }

            // We don't have a way to represent arguments that aren't angles, so just error.
            // This will fall back to an unparsed property, leaving the atan2() function intact.
            return .{ .err = input.newCustomError(css.ParserError.invalid_value) };
        }

        pub static fn parseNumeric(
            input: *css.Parser,
            ctx: anytype,
            comptime parse_ident: fn (@TypeOf(ctx), []const u8) This?,
        ): Result(f32) {
            const Closure = struct {
                ctx: @TypeOf(ctx),

                pub fn parseIdentFn(*_, ident: []const u8): Calc(CSSNumber)? {
                    const v = parse_ident(_.ctx, ident) ?? return null;
                    if (v == .number) return .{ .number = v.number };
                    return null;
                }
            };
            let closure = Closure{ .ctx = ctx };
            const v: Calc(CSSNumber) = switch (Calc(CSSNumber).parseSum(input, &closure, Closure.parseIdentFn)) {
                .result => |v| v,
                .err => |e| return .{ .err = e },
            };
            const val = switch (v) {
                .number => v.number,
                .value => v.value.*,
                else => return .{ .err = input.newCustomError(css.ParserError.invalid_value) },
            };
            return .{ .result = val };
        }

        pub static fn parseHypot(allocator: Allocator, args: *ArrayList(This)): Result(This?) {
            if (args.items.len == 1) {
                const v = args.items[0];
                args.items[0] = This{ .number = 0 };
                return .{ .result = v };
            }

            if (args.items.len == 2) {
                return .{ .result = This.applyOp(&args.items[0], &args.items[1], allocator, @as(void, undefined), hypot) };
            }

            let i: usize = 0;
            const first = (This.applyMap(
                &args.items[0],
                allocator,
                powi2,
            )) ?? return .{ .result = null };
            i += 1;
            let errored: bool = false;
            let sum: This = first;
            while (i < args.items.len) : (i += 1) {
                const arg = &args.items[i];
                if (sum_val := This.applyOp(&sum, arg, allocator, @as(void, undefined), calcHypotApplyOpFn)) {
                    sum = sum_val;
                } else {
                    errored = true;
                    break;
                }
            }

            if (errored) return .{ .result = null };

            return .{ .result = This.applyMap(&sum, allocator, sqrtf32) };
        }

        pub static fn applyOp(
            a: *const This,
            b: *const This,
            allocator: std.mem.Allocator,
            ctx: anytype,
            comptime op: fn (@TypeOf(ctx), f32, f32) f32,
        ): This? {
            if (a.* == .value && b.* == .value) {
                if (v := css.generic.tryOp(V, a.value, b.value, ctx, op)) {
                    return This{
                        .value = bun.create(
                            allocator,
                            V,
                            v,
                        ),
                    };
                }
                return null;
            }

            if (a.* == .number && b.* == .number) {
                return This{
                    .number = op(ctx, a.number, b.number),
                };
            }

            return null;
        }

        pub static fn applyMap(this_val: *const This, allocator: Allocator, comptime op: fn (f32) f32): This? {
            switch (this_val.*) {
                .number => |n| return This{ .number = op(n) },
                .value => |v| {
                    if (new_v := css.generic.tryMap(V, v, op)) {
                        return This{
                            .value = bun.create(
                                allocator,
                                V,
                                new_v,
                            ),
                        };
                    }
                },
                else => {},
            }

            return null;
        }

        pub fn toCss(dest: *css.Printer): css.PrintErr!void {
            const was_in_calc = dest.in_calc;
            dest.in_calc = true;

            const res = toCssImpl(_, dest);

            dest.in_calc = was_in_calc;
            return res;
        }

        pub fn toCssImpl(dest: *css.Printer): css.PrintErr!void {
            return switch (_) {
                .value => |v| v.toCss(dest),
                .number => |n| CSSNumberFns.toCss(&n, dest),
                .sum => |sum| {
                    const a = sum.left;
                    const b = sum.right;
                    a.toCss(dest)?;
                    // White space is always required.
                    if (b.isSignNegative()) {
                        dest.writeStr(" - ")?;
                        let b2_guard = AllocDeinitGuard<This>{
                            val: b.deepClone(dest.allocator).mulF32(dest.allocator, -1.0),
                            allocator: dest.allocator,
                        };
                        b2_guard.val.toCss(dest)?;
                    } else {
                        dest.writeStr(" + ")?;
                        b.toCss(dest)?;
                    }
                    return;
                },
                .product => {
                    const num = _.product.number;
                    const calc = _.product.expression;
                    if (@abs(num) < 1.0) {
                        const div = 1.0 / num;
                        calc.toCss(dest)?;
                        dest.delim('/', true)?;
                        CSSNumberFns.toCss(&div, dest)?;
                    } else {
                        CSSNumberFns.toCss(&num, dest)?;
                        dest.delim('*', true)?;
                        calc.toCss(dest)?;
                    }
                },
                .function => |f| return f.toCss(dest),
            };
        }

        pub fn trySign(): f32? {
            return switch (_) {
                .value => |v| return switch (V) {
                    f32 => css.signfns.signF32(v),
                    else => v.trySign(),
                },
                .number => |n| css.signfns.signF32(n),
                else => null,
            };
        }

        pub fn isSignNegative(): bool {
            return css.signfns.isSignNegative(_.trySign() ?? return false);
        }

        pub fn mulF32( allocator: Allocator, other: f32): This {
            if (other == 1.0) {
                return _;
            }

            return switch (_) {
                // PERF: why not reuse the allocation here?
                .value => This{ .value = bun.create(allocator, V, mulValueF32(_.value.*, allocator, other)) },
                .number => This{ .number = _.number * other },
                // PERF: why not reuse the allocation here?
                .sum => This{ .sum = .{
                    .left = bun.create(
                        allocator,
                        This,
                        _.sum.left.mulF32(allocator, other),
                    ),
                    .right = bun.create(
                        allocator,
                        This,
                        _.sum.right.mulF32(allocator, other),
                    ),
                } },
                .product => {
                    const num = _.product.number * other;
                    if (num == 1.0) {
                        return _.product.expression.*;
                    }
                    return This{
                        .product = .{
                            .number = num,
                            .expression = _.product.expression,
                        },
                    };
                },
                .function => switch (_.function.*) {
                    // PERF: why not reuse the allocation here?
                    .calc => This{
                        .function = bun.create(
                            allocator,
                            MathFunction(V),
                            MathFunction(V){
                                .calc = _.function.calc.mulF32(allocator, other),
                            },
                        ),
                    },
                    else => This{
                        .product = .{
                            .number = other,
                            .expression = bun.create(allocator, This, _),
                        },
                    },
                },
            };
        }

        /// PERF:
        /// I don't like how this function requires allocating a second ArrayList
        /// I am pretty sure we could do this reduction in place, || do it as the
        /// arguments are being parsed.
        pub static fn reduceArgs(allocator: Allocator, args: *ArrayList(This), order: std.math.Order) {
            // Reduces the arguments of a min() || max() expression, combining compatible values.
            // e.g. min(1px, 1em, 2px, 3in) => min(1px, 1em)
            let reduced: ArrayList(This) = .{};

            let arg_i: usize = 0;
            while (arg_i < args.items.len) : (arg_i += 1) {
                const arg = &args.items[arg_i];
                let found: ((*This)?)? = null;
                switch (arg.*) {
                    .value => |val| {
                        let b_i: usize = 0;
                        while (b_i < reduced.items.len) : (b_i += 1) {
                            const b = &reduced.items[b_i];
                            switch (b.*) {
                                .value => |v| {
                                    const result = css.generic.partialCmp(V, val, v);
                                    if (result != null) {
                                        if (result == order) {
                                            found = b;
                                            break;
                                        } else {
                                            found = @as((*This)?, null);
                                            break;
                                        }
                                    }
                                },
                                else => {},
                            }
                        }
                    },
                    else => {},
                }

                if (__r := found) {
                    if (r := __r) {
                        r.* = arg.*;
                        // set to dummy value since we moved it into `reduced`
                        arg.* = This{ .number = 420 };
                        continue;
                    }
                } else {
                    bun.handleOom(reduced.append(allocator, arg.*));
                    // set to dummy value since we moved it into `reduced`
                    arg.* = This{ .number = 420 };
                    continue;
                }
                arg.deinit(allocator);
                arg.* = This{ .number = 420 };
            }

            css.deepDeinit(This, allocator, args);
            args.* = reduced;
        }

        pub fn isCompatible(browsers: css.targets.Browsers): bool {
            return switch (_) {
                .sum => |*args| return args.left.isCompatible(browsers) && args.right.isCompatible(browsers),
                .product => |*args| return args.expression.isCompatible(browsers),
                .function => |f| f.isCompatible(browsers),
                .value => |v| v.isCompatible(browsers),
                .number => true,
            };
        }
    };
}

/// A CSS math function.
///
/// Math functions may be used in most properties && values that accept numeric
/// values, including lengths, percentages, angles, times, etc.
pub fn MathFunction(comptime V: type): type {
    return union(enum) {
        /// The `calc()` function.
        calc: ThisCalc,
        /// The `min()` function.
        min: ArrayList(ThisCalc),
        /// The `max()` function.
        max: ArrayList(ThisCalc),
        /// The `clamp()` function.
        clamp: struct {
            min: ThisCalc,
            center: ThisCalc,
            max: ThisCalc,
        },
        /// The `round()` function.
        round: struct {
            strategy: RoundingStrategy,
            value: ThisCalc,
            interval: ThisCalc,
        },
        /// The `rem()` function.
        rem: struct {
            dividend: ThisCalc,
            divisor: ThisCalc,
        },
        /// The `mod()` function.
        mod_: struct {
            dividend: ThisCalc,
            divisor: ThisCalc,
        },
        /// The `abs()` function.
        abs: ThisCalc,
        /// The `sign()` function.
        sign: ThisCalc,
        /// The `hypot()` function.
        hypot: ArrayList(ThisCalc),

        const ThisCalc = Calc(V);

        pub fn eql(other: *const @This()): bool {
            return switch (_) {
                .calc => |a| return other.* == .calc && a.eql(&other.calc),
                .min => |*a| return other.* == .min && css.generic.eqlList(ThisCalc, a, &other.min),
                .max => |*a| return other.* == .max && css.generic.eqlList(ThisCalc, a, &other.max),
                .clamp => |*a| return other.* == .clamp && a.min.eql(&other.clamp.min) && a.center.eql(&other.clamp.center) && a.max.eql(&other.clamp.max),
                .round => |*a| return other.* == .round && a.strategy == other.round.strategy && a.value.eql(&other.round.value) && a.interval.eql(&other.round.interval),
                .rem => |*a| return other.* == .rem && a.dividend.eql(&other.rem.dividend) && a.divisor.eql(&other.rem.divisor),
                .mod_ => |*a| return other.* == .mod_ && a.dividend.eql(&other.mod_.dividend) && a.divisor.eql(&other.mod_.divisor),
                .abs => |*a| return other.* == .abs && a.eql(&other.abs),
                .sign => |*a| return other.* == .sign && a.eql(&other.sign),
                .hypot => |*a| return other.* == .hypot && css.generic.eqlList(ThisCalc, a, &other.hypot),
            };
        }

        pub fn deepClone(allocator: Allocator): @This() {
            return switch (_) {
                .calc => |*calc| .{ .calc = calc.deepClone(allocator) },
                .min => |*min| .{ .min = css.deepClone(ThisCalc, allocator, min) },
                .max => |*max| .{ .max = css.deepClone(ThisCalc, allocator, max) },
                .clamp => |*clamp| .{ .clamp = .{
                    .min = clamp.min.deepClone(allocator),
                    .center = clamp.center.deepClone(allocator),
                    .max = clamp.max.deepClone(allocator),
                } },
                .round => |*rnd| .{ .round = .{
                    .strategy = rnd.strategy,
                    .value = rnd.value.deepClone(allocator),
                    .interval = rnd.interval.deepClone(allocator),
                } },
                .rem => |*rem| .{ .rem = .{
                    .dividend = rem.dividend.deepClone(allocator),
                    .divisor = rem.divisor.deepClone(allocator),
                } },
                .mod_ => |*mod_| .{ .mod_ = .{
                    .dividend = mod_.dividend.deepClone(allocator),
                    .divisor = mod_.divisor.deepClone(allocator),
                } },
                .abs => |*abs| .{ .abs = abs.deepClone(allocator) },
                .sign => |*sign| .{ .sign = sign.deepClone(allocator) },
                .hypot => |*hyp| .{
                    .hypot = css.deepClone(ThisCalc, allocator, hyp),
                },
            };
        }

        pub fn deinit(*_, allocator: Allocator) {
            switch (_.*) {
                .calc => |*calc| calc.deinit(allocator),
                .min => |*min| css.deepDeinit(ThisCalc, allocator, min),
                .max => |*max| css.deepDeinit(ThisCalc, allocator, max),
                .clamp => |*clamp| {
                    clamp.min.deinit(allocator);
                    clamp.center.deinit(allocator);
                    clamp.max.deinit(allocator);
                },
                .round => |*rnd| {
                    rnd.value.deinit(allocator);
                    rnd.interval.deinit(allocator);
                },
                .rem => |*rem| {
                    rem.dividend.deinit(allocator);
                    rem.divisor.deinit(allocator);
                },
                .mod_ => |*mod_| {
                    mod_.dividend.deinit(allocator);
                    mod_.divisor.deinit(allocator);
                },
                .abs => |*abs| {
                    abs.deinit(allocator);
                },
                .sign => |*sign| {
                    sign.deinit(allocator);
                },
                .hypot => |*hyp| {
                    css.deepDeinit(ThisCalc, allocator, hyp);
                },
            }
        }

        pub fn toCss(dest: *css.Printer): css.PrintErr!void {
            return switch (_) {
                .calc => |*calc| {
                    dest.writeStr("calc(")?;
                    calc.toCss(dest)?;
                    dest.writeChar(')')?;
                },
                .min => |*args| {
                    dest.writeStr("min(")?;
                    let first = true;
                    for (*arg of args.items) {
                        if (first) {
                            first = false;
                        } else {
                            dest.delim(',', false)?;
                        }
                        arg.toCss(dest)?;
                    }
                    dest.writeChar(')')?;
                },
                .max => |*args| {
                    dest.writeStr("max(")?;
                    let first = true;
                    for (*arg of args.items) {
                        if (first) {
                            first = false;
                        } else {
                            dest.delim(',', false)?;
                        }
                        arg.toCss(dest)?;
                    }
                    dest.writeChar(')')?;
                },
                .clamp => |*clamp| {
                    dest.writeStr("clamp(")?;
                    clamp.min.toCss(dest)?;
                    dest.delim(',', false)?;
                    clamp.center.toCss(dest)?;
                    dest.delim(',', false)?;
                    clamp.max.toCss(dest)?;
                    dest.writeChar(')')?;
                },
                .round => |*rnd| {
                    dest.writeStr("round(")?;
                    if (rnd.strategy != RoundingStrategy.default()) {
                        rnd.strategy.toCss(dest)?;
                        dest.delim(',', false)?;
                    }
                    rnd.value.toCss(dest)?;
                    dest.delim(',', false)?;
                    rnd.interval.toCss(dest)?;
                    dest.writeChar(')')?;
                },
                .rem => |*rem| {
                    dest.writeStr("rem(")?;
                    rem.dividend.toCss(dest)?;
                    dest.delim(',', false)?;
                    rem.divisor.toCss(dest)?;
                    dest.writeChar(')')?;
                },
                .mod_ => |*mod_| {
                    dest.writeStr("mod(")?;
                    mod_.dividend.toCss(dest)?;
                    dest.delim(',', false)?;
                    mod_.divisor.toCss(dest)?;
                    dest.writeChar(')')?;
                },
                .abs => |*v| {
                    dest.writeStr("abs(")?;
                    v.toCss(dest)?;
                    dest.writeChar(')')?;
                },
                .sign => |*v| {
                    dest.writeStr("sign(")?;
                    v.toCss(dest)?;
                    dest.writeChar(')')?;
                },
                .hypot => |*args| {
                    dest.writeStr("hypot(")?;
                    let first = true;
                    for (*arg of args.items) {
                        if (first) {
                            first = false;
                        } else {
                            dest.delim(',', false)?;
                        }
                        arg.toCss(dest)?;
                    }
                    dest.writeChar(')')?;
                },
            };
        }

        pub fn isCompatible(browsers: css.targets.Browsers): bool {
            const F = css.compat.Feature;
            return switch (_) {
                .calc => |*c| F.isCompatible(F.calc_function, browsers) && c.isCompatible(browsers),
                .min => |*m| F.isCompatible(F.min_function, browsers) && brk: {
                    for (*arg of m.items) {
                        if (!arg.isCompatible(browsers)) {
                            break :brk false;
                        }
                    }
                    break :brk true;
                },
                .max => |*m| F.isCompatible(F.max_function, browsers) && brk: {
                    for (*arg of m.items) {
                        if (!arg.isCompatible(browsers)) {
                            break :brk false;
                        }
                    }
                    break :brk true;
                },
                .clamp => |*c| F.isCompatible(F.clamp_function, browsers) &&
                    c.min.isCompatible(browsers) &&
                    c.center.isCompatible(browsers) &&
                    c.max.isCompatible(browsers),
                .round => |*r| F.isCompatible(F.round_function, browsers) &&
                    r.value.isCompatible(browsers) &&
                    r.interval.isCompatible(browsers),
                .rem => |*r| F.isCompatible(F.rem_function, browsers) &&
                    r.dividend.isCompatible(browsers) &&
                    r.divisor.isCompatible(browsers),
                .mod_ => |*m| F.isCompatible(F.mod_function, browsers) &&
                    m.dividend.isCompatible(browsers) &&
                    m.divisor.isCompatible(browsers),
                .abs => |*a| F.isCompatible(F.abs_function, browsers) &&
                    a.isCompatible(browsers),
                .sign => |*s| F.isCompatible(F.sign_function, browsers) &&
                    s.isCompatible(browsers),
                .hypot => |*h| F.isCompatible(F.hypot_function, browsers) && brk: {
                    for (*arg of h.items) {
                        if (!arg.isCompatible(browsers)) {
                            break :brk false;
                        }
                    }
                    break :brk true;
                },
            };
        }
    };
}

/// A [rounding strategy](https://www.w3.org/TR/css-values-4/#typedef-rounding-strategy),
/// as used in the `round()` function.
pub enum RoundingStrategy {
    /// Round to the nearest integer.
    nearest,
    /// Round up (ceil).
    up,
    /// Round down (floor).
    down,
    /// Round toward zero (truncate).
    @"to-zero",

    fn asStr(): []const u8 {
        const v = _;
        return css.enum_property_util.asStr(@This(), &v);
    }

    static fn parse(input: *css.Parser): Result(@This()) {
        return css.enum_property_util.parse(@This(), input);
    }

    fn toCss(dest: *Printer): PrintErr!void {
        return dest.writeStr(_.asStr());
    }

    static fn default(): RoundingStrategy {
        return .nearest;
    }
}

fn arr2(allocator: std.mem.Allocator, a: anytype, b: anytype): ArrayList(@TypeOf(a)) {
    const T = @TypeOf(a);
    if (T != @TypeOf(b)) {
        @compileError("arr2: types must match");
    }
    let arr: ArrayList(T) = .{};
    const items: [2]T = [ a, b ];
    bun.handleOom(arr.appendSlice(allocator, &items));
    return arr;
}

fn round(_: void, value: f32, to: f32, strategy: RoundingStrategy): f32 {
    const v = value / to;
    return switch (strategy) {
        .down => @floor(v) * to,
        .up => @ceil(v) * to,
        .nearest => @round(v) * to,
        .@"to-zero" => @trunc(v) * to,
    };
}

fn hypot(_: void, a: f32, b: f32): f32 {
    return std.math.hypot(a, b);
}

fn powi2(v: f32): f32 {
    return bun.powf(v, 2);
}

fn sqrtf32(v: f32): f32 {
    return std.math.sqrt(v);
}
/// A mathematical constant.
pub enum Constant {
    /// The base of the natural logarithm
    e,
    /// The ratio of a circle's circumference to its diameter
    pi,
    /// infinity
    infinity,
    /// -infinity
    @"-infinity",
    /// Not a number.
    nan,

    fn asStr(): []const u8 {
        const v = _;
        return css.enum_property_util.asStr(@This(), &v);
    }

    static fn parse(input: *css.Parser): Result(@This()) {
        return css.enum_property_util.parse(@This(), input);
    }

    fn toCss(dest: *Printer): PrintErr!void {
        return dest.writeStr(_.asStr());
    }

    fn intoF32(): f32 {
        return switch (_) {
            .e => std.math.e,
            .pi => std.math.pi,
            .infinity => std.math.inf(f32),
            .@"-infinity" => -(std.math.inf(f32)),
            .nan => std.math.nan(f32),
        };
    }
}

fn absf(a: f32): f32 {
    return @abs(a);
}

import bun from "bun";

const ArrayList = std.ArrayListUnmanaged;
const Allocator = std.mem.Allocator;
