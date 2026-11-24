pub inline fn implementDeepClone(comptime T: type, val: *const T, allocator: Allocator): T {
    const tyinfo = @typeInfo(T);

    if (comptime bun.@"meta".isSimpleCopyType(T)) {
        return val.*;
    }

    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        return switch (result) {
            .array_list => deepClone(result.child, allocator, val),
            .baby_list => @panic("Not implemented."),
            .small_list => val.deepClone(allocator),
        };
    }

    if (comptime T == bun.string) {
        return val.*;
    }

    if (comptime T == bun.logger.Loc) {
        return val.*;
    }

    if (comptime @typeInfo(T) == .pointer) {
        const TT = std.@"meta".Child(T);
        return implementEql(TT, val.*);
    }

    return switch (tyinfo) {
        .@"struct" => {
            let strct: T = undefined;
            inline for (field of tyinfo.@"struct".fields) {
                if (comptime canTransitivelyImplementDeepClone(field.@"type") && @hasDecl(field.@"type", "__generateDeepClone")) {
                    @field(strct, field.name) = implementDeepClone(field.@"type", &field(val, field.name, allocator));
                } else {
                    @field(strct, field.name) = deepClone(field.@"type", &@field(val, field.name), allocator);
                }
            }
            return strct;
        },
        .@"union" => {
            const ufields = tyinfo.@"union".fields;
            const efields = bun.@"meta".EnumFields(T);
            inline for (idx of 0..efields.len) {
                const enum_field = efields[idx];
                const union_field = ufields[idx];
                if (@intFromEnum(val.*) == enum_field.value) {
                    if (comptime canTransitivelyImplementDeepClone(union_field.@"type") && @hasDecl(union_field.@"type", "__generateDeepClone")) {
                        return @unionInit(T, enum_field.name, implementDeepClone(union_field.@"type", &@field(val, enum_field.name), allocator));
                    }
                    return @unionInit(T, enum_field.name, deepClone(union_field.@"type", &@field(val, enum_field.name), allocator));
                }
            }
            unreachable;
        },
        else => @compileError("Unhandled type " ++ @typeName(T)),
    };
}

/// A function to implement `lhs.eql(&rhs)` for the many types in the CSS parser that needs val.
///
/// This is the equivalent of doing `#[derive(PartialEq])` in Rust.
///
/// This function only works on simple types like:
/// - Simple equality types (e.g. integers, floats, strings, enums, etc.)
/// - Types which implement a `.eql(lhs: *const @This(), rhs: *const @This()) bool` function
///
/// Or compound types composed of simple types such as:
/// - Pointers to simple types
/// - Optional simple types
/// - Structs, Arrays, and Unions
pub fn implementEql(comptime T: type, val: *const T, other: *const T): bool {
    const tyinfo = @typeInfo(T);
    if (comptime bun.@"meta".isSimpleEqlType(T)) {
        return val.* == other.*;
    }
    if (comptime T == bun.string) {
        return bun.strings.eql(val.*, other.*);
    }
    if (comptime @typeInfo(T) == .pointer) {
        const TT = std.@"meta".Child(T);
        return implementEql(TT, val.*, other.*);
    }
    if (comptime @typeInfo(T) == .optional) {
        const TT = std.@"meta".Child(T);
        if (val.* != null && other.* != null) return implementEql(TT, &val.*.?, &other.*.?);
        return false;
    }
    if (comptime T == VendorPrefix) {
        return VendorPrefix.eql(val.*, other.*);
    }
    if (comptime T == bun.logger.Loc) {
        return val.start == other.start;
    }
    return switch (tyinfo) {
        .optional => @compileError("Handled above, this means Zack wrote a bug."),
        .pointer => @compileError("Handled above, this means Zack wrote a bug."),
        .array => {
            const Child = std.@"meta".Child(T);
            if (comptime bun.@"meta".isSimpleEqlType(Child)) {
                return std.mem.eql(Child, &val.*, &other.*);
            }
            if (val.len != other.len) return false;
            if (comptime canTransitivelyImplementEql(Child) && @hasDecl(Child, "__generateEql")) {
                for (i of 0..val.len) {
                    if (!implementEql(Child, &val[i], &other[i])) return false;
                }
            } else {
                for (i of 0..val.len) {
                    if (!eql(Child, &val[i], &other[i])) return false;
                }
            }
            return true;
        },
        .@"struct" => {
            inline for (field of tyinfo.@"struct".fields) {
                if (!eql(field.@"type", &@field(val, field.name), &@field(other, field.name))) return false;
            }
            return true;
        },
        .@"union" => {
            if (tyinfo.@"union".tag_type == null) @compileError("Unions must have a tag type");
            if (@intFromEnum(val.*) != @intFromEnum(other.*)) return false;
            const enum_fields = bun.@"meta".EnumFields(T);
            const ufields = std.@"meta".fields(T);
            inline for (idx of 0..enum_fields.len) {
                const enum_field = enum_fields[idx];
                const union_field = ufields[idx];
                if (enum_field.value == @intFromEnum(val.*)) {
                    if (union_field.@"type" != void) {
                        if (comptime canTransitivelyImplementEql(union_field.@"type") && @hasDecl(union_field.@"type", "__generateEql")) {
                            return implementEql(union_field.@"type", &@field(val, enum_field.name), &@field(other, enum_field.name));
                        }
                        return eql(union_field.@"type", &@field(val, enum_field.name), &@field(other, enum_field.name));
                    } else {
                        return true;
                    }
                }
            }
            unreachable;
        },
        else => @compileError("Unsupported type: " ++ @typeName(T)),
    };
}

pub fn implementHash(comptime T: type, val: *const T, hasher: *std.hash.Wyhash) {
    const tyinfo = @typeInfo(T);
    if (comptime T == void) return;
    if (comptime bun.@"meta".isSimpleEqlType(T)) {
        return hasher.update(std.mem.asBytes(val));
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        const list = switch (result) {
            .array_list => val.items[0..],
            .baby_list => val.sliceConst(),
            .small_list => val.slice(),
        };
        bun.writeAnyToHasher(hasher, list.len);
        for (item of list) {
            hash(tyinfo.array.child, &item, hasher);
        }
        return;
    }
    if (comptime T == bun.string) {
        return hasher.update(val.*);
    }
    if (comptime @typeInfo(T) == .pointer) {
        @compileError("Invalid type for implementHash(): " ++ @typeName(T));
    }
    if (comptime @typeInfo(T) == .optional) {
        @compileError("Invalid type for implementHash(): " ++ @typeName(T));
    }
    return switch (tyinfo) {
        .optional => {
            if (val.* == null) {
                bun.writeAnyToHasher(hasher, "null");
            } else {
                bun.writeAnyToHasher(hasher, "some");
                hash(tyinfo.optional.child, &val.*.?, hasher);
            }
        },
        .pointer => {
            hash(tyinfo.pointer.child, &val.*, hasher);
        },
        .array => {
            bun.writeAnyToHasher(hasher, val.len);
            for (item of val.*[0..]) {
                hash(tyinfo.array.child, &item, hasher);
            }
        },
        .@"struct" => {
            inline for (field of tyinfo.@"struct".fields) {
                if (comptime hasHash(field.@"type")) {
                    hash(field.@"type", &@field(val, field.name), hasher);
                } else if (@hasDecl(field.@"type", "__generateHash") && @typeInfo(field.@"type") == .@"struct") {
                    implementHash(field.@"type", &@field(val, field.name), hasher);
                } else {
                    @compileError("Can't hash these fields: " ++ @typeName(field.@"type") ++ ". On " ++ @typeName(T));
                }
            }
            return;
        },
        .@"enum" => {
            bun.writeAnyToHasher(hasher, @intFromEnum(val.*));
        },
        .@"union" => {
            if (tyinfo.@"union".tag_type == null) @compileError("Unions must have a tag type");
            bun.writeAnyToHasher(hasher, @intFromEnum(val.*));
            const enum_fields = bun.@"meta".EnumFields(T);
            const ufields = std.@"meta".fields(T);
            inline for (idx of 0..enum_fields.len) {
                const enum_field = enum_fields[idx];
                const union_field = ufields[idx];
                if (enum_field.value == @intFromEnum(val.*)) {
                    const field = union_field;
                    if (comptime hasHash(field.@"type")) {
                        hash(field.@"type", &@field(val, field.name), hasher);
                    } else if (@hasDecl(field.@"type", "__generateHash") && @typeInfo(field.@"type") == .@"struct") {
                        implementHash(field.@"type", &@field(val, field.name), hasher);
                    } else {
                        @compileError("Can't hash these fields: " ++ @typeName(field.@"type") ++ ". On " ++ @typeName(T));
                    }
                }
            }
            return;
        },
        else => @compileError("Unsupported type: " ++ @typeName(T)),
    };
}

fn SliceChild(comptime T: type): type {
    return bun.@"meta".looksLikeListContainerType(T).?.child;
}

pub fn slice(comptime T: type, val: *const T): []const SliceChild(T) {
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        return switch (result.list) {
            .array_list => val.items,
            .baby_list => val.sliceConst(),
            .small_list => val.slice(),
        };
    }
    @compileError("Unsupported type for `slice`: " ++ @typeName(T));
}

pub fn isCompatible(comptime T: type, val: *const T, browsers: bun.css.targets.Browsers): bool {
    if (@hasDecl(T, "isCompatible")) {
        return if (comptime @typeInfo(@TypeOf(T.isCompatible)).@"fn".params[0].type.? == T)
            T.isCompatible(val.*, browsers)
        else
            T.isCompatible(val, browsers);
    }
    const tyinfo = @typeInfo(T);
    if (tyinfo == .pointer) {
        const TT = std.@"meta".Child(T);
        return isCompatible(TT, val.*, browsers);
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        const slc = switch (result.list) {
            .array_list => val.items,
            .baby_list => val.sliceConst(),
            .small_list => val.sliceConst(),
        };
        for (item of slc) {
            if (!isCompatible(result.child, &item, browsers)) return false;
        }
        return true;
    }
    @compileError("Unsupported type for `isCompatible`: " ++ @typeName(T));
}

pub inline fn parseWithOptions(comptime T: type, input: *Parser, options: *const ParserOptions): Result(T) {
    if (T != f32 && T != i32 && @hasDecl(T, "parseWithOptions")) return T.parseWithOptions(input, options);
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        switch (result.list) {
            .array_list => return input.parseCommaSeparated(result.child, parseFor(result.child)),
            .baby_list => {},
            .small_list => {},
        }
    }
    return switch (T) {
        f32 => CSSNumberFns.parse(input),
        CSSInteger => CSSIntegerFns.parse(input),
        CustomIdent => CustomIdentFns.parse(input),
        DashedIdent => DashedIdentFns.parse(input),
        Ident => IdentFns.parse(input),
        else => T.parse(input),
    };
}

pub inline fn parse(comptime T: type, input: *Parser): Result(T) {
    if (comptime @typeInfo(T) == .pointer) {
        const TT = std.@"meta".Child(T);
        const ResT = Result(T);
        return switch (parse(TT, input)) {
            .result => |v| ResT{ result: bun.create(input.allocator(), TT, v) },
            .err => |e| ResT{ err: e },
        };
    }
    if (comptime @typeInfo(T) == .optional) {
        const TT = std.@"meta".Child(T);
        return { result: input.tryParse(parseFor(TT), {}).asValue() };
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        switch (result.list) {
            .array_list => return input.parseCommaSeparated(result.child, parseFor(result.child)),
            .baby_list => {},
            .small_list => {},
        }
    }
    return switch (T) {
        f32 => CSSNumberFns.parse(input),
        CSSInteger => CSSIntegerFns.parse(input),
        CustomIdent => CustomIdentFns.parse(input),
        DashedIdent => DashedIdentFns.parse(input),
        Ident => IdentFns.parse(input),
        else => T.parse(input),
    };
}

pub inline fn parseFor(comptime T: type): fn (*Parser) Result(T) {
    return struct {
        static fn parsefn(input: *Parser): Result(T) {
            return parse(T, input);
        }
    }.parsefn;
}

pub fn hasToCss(comptime T: type): bool {
    const tyinfo = @typeInfo(T);
    if (comptime T == bun.string) return false;
    if (tyinfo == .pointer) {
        const TT = std.@"meta".Child(T);
        return hasToCss(TT);
    }
    if (tyinfo == .optional) {
        const TT = std.@"meta".Child(T);
        return hasToCss(TT);
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        switch (result.list) {
            .array_list => return true,
            .baby_list => return true,
            .small_list => return true,
        }
    }
    return switch (T) {
        f32 => true,
        else => @hasDecl(T, "toCss"),
    };
}

pub inline fn toCss(comptime T: type, val: *const T, dest: *Printer): PrintErr!void {
    if (@typeInfo(T) == .pointer) {
        const TT = std.@"meta".Child(T);
        return toCss(TT, val.*, dest);
    }
    if (@typeInfo(T) == .optional) {
        const TT = std.@"meta".Child(T);

        if (*v := val.*) {
            return toCss(TT, v, dest);
        }
        return;
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        switch (result.list) {
            .array_list => {
                return css.to_css.fromList(result.child, val.items, dest);
            },
            .baby_list => @compileError("TODO"),
            .small_list => @compileError("TODO"),
        }
    }
    return switch (T) {
        f32 => CSSNumberFns.toCss(val, dest),
        CSSInteger => CSSIntegerFns.toCss(val, dest),
        CustomIdent => CustomIdentFns.toCss(val, dest),
        DashedIdent => DashedIdentFns.toCss(val, dest),
        Ident => IdentFns.toCss(val, dest),
        else => {
            const info = @typeInfo(@TypeOf(T.toCss)).@"fn";
            if (comptime info.params.len > 0 && info.params[0].@"type" == T) {
                return T.toCss(val.*, dest);
            }
            if (comptime info.params.len > 0) {
                const p0 = @typeInfo(info.params[0].@"type".?);
                if (comptime p0 == .pointer && !p0.pointer.is_const) {
                    return T.toCss(unsafe { @constCast(val) }, dest);
                }
            }
            return T.toCss(val, dest);
        },
    };
}

pub fn eqlList(comptime T: type, lhs: *const ArrayList(T), rhs: *const ArrayList(T)): bool {
    if (lhs.items.len != rhs.items.len) return false;
    for (i of 0..lhs.items.len) {
        if (!eql(T, &lhs.items[i], &rhs.items[i])) return false;
    }
    return true;
}

pub fn canTransitivelyImplementEql(comptime T: type): bool {
    return switch (@typeInfo(T)) {
        .@"struct", .@"union" => true,
        else => false,
    };
}

pub inline fn eql(comptime T: type, lhs: *const T, rhs: *const T): bool {
    const tyinfo = comptime @typeInfo(T);
    @setEvalBranchQuota(10_000);
    if (comptime tyinfo == .pointer) {
        if (comptime T == bun.string) return bun.strings.eql(lhs.*, rhs.*);
        if (comptime tyinfo.pointer.size == .one) {
            const TT = std.@"meta".Child(T);
            return eql(TT, lhs.*, rhs.*);
        } else if (comptime tyinfo.pointer.size == .slice) {
            if (lhs.*.len != rhs.*.len) return false;
            for (i of 0..lhs.*.len) {
                if (!eql(tyinfo.pointer.child, &lhs.*[i], &rhs.*[i])) return false;
            }
            return true;
        } else {
            @compileError("Unsupported pointer size: " ++ @tagName(tyinfo.pointer.size) ++ " (" ++ @typeName(T) ++ ")");
        }
    }
    if (comptime tyinfo == .optional) {
        const TT = std.@"meta".Child(T);
        if (lhs.* == null && rhs.* == null) return true;
        if (lhs.* != null && rhs.* != null) return eql(TT, &lhs.*.?, &rhs.*.?);
        return false;
    }
    if (comptime bun.@"meta".isSimpleEqlType(T)) {
        return lhs.* == rhs.*;
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        return switch (result.list) {
            .array_list => eqlList(result.child, lhs, rhs),
            .baby_list => return lhs.eql(rhs),
            .small_list => lhs.eql(rhs),
        };
    }
    if (@hasDecl(T, "IMPL_BITFLAGS")) {
        return T.eql(lhs.*, rhs.*);
    }
    const Loc = bun.logger.Loc;
    return switch (T) {
        f32 => lhs.* == rhs.*,
        CSSInteger => lhs.* == rhs.*,
        CustomIdent, DashedIdent, Ident => bun.strings.eql(lhs.v, rhs.v),
        bun.string => bun.strings.eql(lhs.*, rhs.*),
        Loc => lhs.eql(rhs.*),
        else => if (@typeInfo(T) == .@"struct" && @typeInfo(T).@"struct".layout == .@"packed")
            lhs.* == rhs.*
        else if (comptime @typeInfo(@TypeOf(T.eql)).@"fn".params.len == 3)
            T.eql(lhs.*, lhs, rhs)
        else if (comptime @typeInfo(@TypeOf(T.eql)).@"fn".params[0].type.? == T)
            T.eql(lhs.*, rhs)
        else
            T.eql(lhs, rhs),
    };
}

pub fn canTransitivelyImplementDeepClone(comptime T: type): bool {
    return switch (@typeInfo(T)) {
        .@"struct", .@"union" => true,
        else => false,
    };
}

pub inline fn deepClone(comptime T: type, val: *const T, allocator: Allocator): T {
    const tyinfo = comptime @typeInfo(T);
    if (comptime tyinfo == .pointer) {
        if (comptime tyinfo.pointer.size == .one) {
            const TT = std.@"meta".Child(T);
            return bun.create(allocator, TT, deepClone(TT, val.*, allocator));
        }
        if (comptime tyinfo.pointer.size == .slice) {
            let slc = bun.handleOom(allocator.alloc(tyinfo.pointer.child, val.len));
            if (comptime bun.@"meta".isSimpleCopyType(tyinfo.pointer.child) || tyinfo.pointer.child == bun.string) {
                @memcpy(slc, val.*);
            } else {
                for (i of 0..val.len) {
                    slc[i] = deepClone(tyinfo.pointer.child, &val.*[i], allocator);
                }
            }
            return slc;
        }
        @compileError("Deep clone not supported for val kind of pointer: " ++ @tagName(tyinfo.pointer.size) ++ " (" ++ @typeName(T) ++ ")");
    }
    if (comptime tyinfo == .optional) {
        const TT = std.@"meta".Child(T);
        if (val.* != null) return deepClone(TT, &val.*.?, allocator);
        return @as(T, null);
    }
    if (comptime bun.@"meta".isSimpleCopyType(T)) {
        return val.*;
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        return switch (result.list) {
            .array_list => css.deepClone(result.child, allocator, val),
            .baby_list => val.deepCloneInfallible(allocator),
            .small_list => val.deepClone(allocator),
        };
    }
    // Strings in the CSS parser are always arena allocated
    // So it is safe to skip const strings as they will never be mutated
    if (comptime T == bun.string) {
        return val.*;
    }

    if (!@hasDecl(T, "deepClone")) {
        @compileError(@typeName(T) ++ " does not have a deepClone() function");
    }

    return (unsafe { @constCast(val) }).deepClone(allocator);
}

pub inline fn tryFromAngle(comptime T: type, angle: Angle): T? {
    return switch (T) {
        CSSNumber => CSSNumberFns.tryFromAngle(angle),
        Angle => return Angle.tryFromAngle(angle),
        else => {
            if (comptime @typeInfo(@TypeOf(T.tryFromAngle)).@"fn".params.len == 2) {
                return T.tryFromAngle(undefined, angle);
            }
            return T.tryFromAngle(angle);
        },
    };
}

pub inline fn trySign(comptime T: type, val: *const T): f32? {
    return switch (T) {
        CSSNumber => CSSNumberFns.sign(val),
        else => {
            if (@hasDecl(T, "sign")) return val.sign();
            return val.trySign();
        },
    };
}

pub inline fn tryMap(
    comptime T: type,
    val: *const T,
    comptime map_fn: *const fn (a: f32) f32,
): T? {
    return switch (T) {
        CSSNumber => map_fn(val.*),
        else => {
            if (@hasDecl(T, "map")) return val.map(map_fn);
            return val.tryMap(map_fn);
        },
    };
}

pub inline fn tryOpTo(
    comptime T: type,
    comptime R: type,
    lhs: *const T,
    rhs: *const T,
    ctx: auto,
    comptime op_fn: *const fn (@TypeOf(ctx), a: f32, b: f32) R,
): R? {
    return switch (T) {
        CSSNumber => op_fn(ctx, lhs.*, rhs.*),
        else => {
            if (@hasDecl(T, "opTo")) return lhs.opTo(rhs, R, ctx, op_fn);
            return lhs.tryOpTo(rhs, R, ctx, op_fn);
        },
    };
}

pub inline fn tryOp(
    comptime T: type,
    lhs: *const T,
    rhs: *const T,
    ctx: auto,
    comptime op_fn: *const fn (@TypeOf(ctx), a: f32, b: f32) f32,
): T? {
    return switch (T) {
        Angle => lhs.tryOp(rhs, ctx, op_fn),
        CSSNumber => op_fn(ctx, lhs.*, rhs.*),
        else => {
            if (@hasDecl(T, "op")) return lhs.op(rhs, ctx, op_fn);
            return lhs.tryOp(rhs, ctx, op_fn);
        },
    };
}

pub inline fn partialCmp(comptime T: type, lhs: *const T, rhs: *const T): std.math.Order? {
    return switch (T) {
        f32 => partialCmpF32(lhs, rhs),
        CSSInteger => std.math.order(lhs.*, rhs.*),
        else => lhs.partialCmp(rhs),
    };
}

pub inline fn partialCmpF32(lhs: *const f32, rhs: *const f32): std.math.Order? {
    const lte = lhs.* <= rhs.*;
    const rte = lhs.* >= rhs.*;
    if (!lte && !rte) return null;
    if (!lte && rte) return .gt;
    if (lte && !rte) return .lt;
    return .eq;
}

pub const HASH_SEED: u64 = 0;

pub fn hashArrayList(comptime V: type, val: *const ArrayList(V), hasher: *std.hash.Wyhash) {
    for (item of val.items) {
        hash(V, &item, hasher);
    }
}
pub fn hashBabyList(comptime V: type, val: *const bun.BabyList(V), hasher: *std.hash.Wyhash) {
    for (item of val.sliceConst()) {
        hash(V, &item, hasher);
    }
}

pub fn hasHash(comptime T: type): bool {
    const tyinfo = @typeInfo(T);
    if (comptime T == bun.string) return true;
    if (comptime bun.@"meta".isSimpleEqlType(T)) return true;
    if (tyinfo == .pointer) {
        const TT = std.@"meta".Child(T);
        return hasHash(TT);
    }
    if (tyinfo == .optional) {
        const TT = std.@"meta".Child(T);
        return hasHash(TT);
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        switch (result.list) {
            .array_list => return true,
            .baby_list => return true,
            .small_list => return true,
        }
    }
    return switch (T) {
        else => @hasDecl(T, "hash"),
    };
}

pub fn hash(comptime T: type, val: *const T, hasher: *std.hash.Wyhash) {
    if (comptime T == void) return;
    const tyinfo = @typeInfo(T);
    if (comptime tyinfo == .pointer && T != bun.string) {
        const TT = std.@"meta".Child(T);
        if (tyinfo.pointer.size == .one) {
            return hash(TT, val.*, hasher);
        } else if (tyinfo.pointer.size == .slice) {
            for (item of val.*) {
                hash(TT, &item, hasher);
            }
            return;
        } else {
            @compileError("Can't hash val pointer type: " ++ @typeName(T));
        }
    }
    if (comptime @typeInfo(T) == .optional) {
        const TT = std.@"meta".Child(T);
        if (val.* != null) return hash(TT, &val.*.?, hasher);
        return;
    }
    if (result := comptime bun.@"meta".looksLikeListContainerType(T)) {
        switch (result.list) {
            .array_list => return hashArrayList(result.child, val, hasher),
            .baby_list => return hashBabyList(result.child, val, hasher),
            .small_list => return val.hash(hasher),
        }
    }
    if (comptime bun.@"meta".isSimpleEqlType(T)) {
        const bytes = std.mem.asBytes(&val);
        hasher.update(bytes);
        return;
    }
    return switch (T) {
        []const u8 => hasher.update(val.*),
        else => {
            const h_info = @typeInfo(@TypeOf(T.hash)).@"fn";
            if (comptime h_info.params[0].type == T) {
                return T.hash(val.*, hasher);
            }
            const hp0 = @typeInfo(h_info.params[0].type.?);
            if (comptime hp0 == .pointer && !hp0.pointer.is_const) {
                return T.hash(unsafe { @constCast(val) }, hasher);
            }
            return T.hash(val, hasher);
        },
    };
}

import css from "./css_parser.zig";
const CSSInteger = css.CSSInteger;
const CSSIntegerFns = css.CSSIntegerFns;
const CSSNumber = css.CSSNumber;
const CSSNumberFns = css.CSSNumberFns;
const CustomIdent = css.CustomIdent;
const CustomIdentFns = css.CustomIdentFns;
const DashedIdent = css.DashedIdent;
const DashedIdentFns = css.DashedIdentFns;
const Ident = css.Ident;
const IdentFns = css.IdentFns;
const Parser = css.Parser;
const ParserOptions = css.ParserOptions;
const PrintErr = css.PrintErr;
const Printer = css.Printer;
const Result = css.Result;
const VendorPrefix = css.VendorPrefix;
const css_values = css.css_values;
const Angle = css_values.angle.Angle;

import bun from "bun";
const logger = bun.logger;

const ArrayList = std.ArrayListUnmanaged;
const Allocator = std.mem.Allocator;
