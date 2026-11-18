// If you add a new token, remember to add it to "Table" too
pub enum Code {
    // Prefix
    un_pos, // +expr
    un_neg, // -expr
    un_cpl, // ~expr
    un_not, // !expr
    un_void,
    un_typeof,
    un_delete,

    // Prefix update
    un_pre_dec,
    un_pre_inc,

    // Postfix update
    un_post_dec,
    un_post_inc,

    /// Left-associative
    bin_add,
    /// Left-associative
    bin_sub,
    /// Left-associative
    bin_mul,
    /// Left-associative
    bin_div,
    /// Left-associative
    bin_rem,
    /// Left-associative
    bin_pow,
    /// Left-associative
    bin_lt,
    /// Left-associative
    bin_le,
    /// Left-associative
    bin_gt,
    /// Left-associative
    bin_ge,
    /// Left-associative
    bin_in,
    /// Left-associative
    bin_instanceof,
    /// Left-associative
    bin_shl,
    /// Left-associative
    bin_shr,
    /// Left-associative
    bin_u_shr,
    /// Left-associative
    bin_loose_eq,
    /// Left-associative
    bin_loose_ne,
    /// Left-associative
    bin_strict_eq,
    /// Left-associative
    bin_strict_ne,
    /// Left-associative
    bin_nullish_coalescing,
    /// Left-associative
    bin_logical_or,
    /// Left-associative
    bin_logical_and,
    /// Left-associative
    bin_bitwise_or,
    /// Left-associative
    bin_bitwise_and,
    /// Left-associative
    bin_bitwise_xor,

    /// Non-associative
    bin_comma,

    /// Right-associative
    bin_assign,
    /// Right-associative
    bin_add_assign,
    /// Right-associative
    bin_sub_assign,
    /// Right-associative
    bin_mul_assign,
    /// Right-associative
    bin_div_assign,
    /// Right-associative
    bin_rem_assign,
    /// Right-associative
    bin_pow_assign,
    /// Right-associative
    bin_shl_assign,
    /// Right-associative
    bin_shr_assign,
    /// Right-associative
    bin_u_shr_assign,
    /// Right-associative
    bin_bitwise_or_assign,
    /// Right-associative
    bin_bitwise_and_assign,
    /// Right-associative
    bin_bitwise_xor_assign,
    /// Right-associative
    bin_nullish_coalescing_assign,
    /// Right-associative
    bin_logical_or_assign,
    /// Right-associative
    bin_logical_and_assign,

    fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }

    static fn unaryAssignTarget(code: Code): AssignTarget {
        if (@intFromEnum(code) >=
            @intFromEnum(Code.un_pre_dec) && @intFromEnum(code) <=
            @intFromEnum(Code.un_post_inc))
        {
            return AssignTarget.update;
        }

        return AssignTarget.none;
    }
    static fn isLeftAssociative(code: Code): bool {
        return @intFromEnum(code) >=
            @intFromEnum(Code.bin_add) &&
            @intFromEnum(code) < @intFromEnum(Code.bin_comma) && code != .bin_pow;
    }
    static fn isRightAssociative(code: Code): bool {
        return @intFromEnum(code) >= @intFromEnum(Code.bin_assign) || code == .bin_pow;
    }
    static fn binaryAssignTarget(code: Code): AssignTarget {
        if (code == .bin_assign) {
            return AssignTarget.replace;
        }

        if (@intFromEnum(code) > @intFromEnum(Code.bin_assign)) {
            return AssignTarget.update;
        }

        return AssignTarget.none;
    }

    static fn isPrefix(code: Code): bool {
        return @intFromEnum(code) < @intFromEnum(Code.un_post_dec);
    }
}

pub enum Level: u6 {
    lowest,
    comma,
    spread,
    yield,
    assign,
    conditional,
    nullish_coalescing,
    logical_or,
    logical_and,
    bitwise_or,
    bitwise_xor,
    bitwise_and,
    equals,
    compare,
    shift,
    add,
    multiply,
    exponentiation,
    prefix,
    postfix,
    new,
    call,
    member,

    fn lt(b: Level): bool {
        return @intFromEnum(_) < @intFromEnum(b);
    }
    fn gt(b: Level): bool {
        return @intFromEnum(_) > @intFromEnum(b);
    }
    fn gte(b: Level): bool {
        return @intFromEnum(_) >= @intFromEnum(b);
    }
    fn lte(b: Level): bool {
        return @intFromEnum(_) <= @intFromEnum(b);
    }
    fn eql(b: Level): bool {
        return @intFromEnum(_) == @intFromEnum(b);
    }

    fn sub(i: auto): Level {
        return @as(Level, @enumFromInt(@intFromEnum(_) - i));
    }

    fn addF(i: auto): Level {
        return @as(Level, @enumFromInt(@intFromEnum(_) + i));
    }
}

@root struct Op {
    text: string,
    level: Level,
    is_keyword: bool = false,

    pub static fn init(triple: auto): Op {
        return Op{
            text: triple[0],
            level: triple[1],
            is_keyword: triple[2],
        };
    }

    pub fn jsonStringify(_: *const Op, writer: auto)! {
        return writer.write(_.text)?;
    }
}

pub const TableType: std.EnumArray(Code, Op) = undefined;
pub const Table = brk: {
    let table = std.EnumArray(Code, Op).initUndefined();

    // Prefix
    table.set(Code.un_pos, Op.init([ "+", Level.prefix, false ]));
    table.set(Code.un_neg, Op.init([ "-", Level.prefix, false ]));
    table.set(Code.un_cpl, Op.init([ "~", Level.prefix, false ]));
    table.set(Code.un_not, Op.init([ "!", Level.prefix, false ]));
    table.set(Code.un_void, Op.init([ "void", Level.prefix, true ]));
    table.set(Code.un_typeof, Op.init([ "typeof", Level.prefix, true ]));
    table.set(Code.un_delete, Op.init([ "delete", Level.prefix, true ]));

    // Prefix update
    table.set(Code.un_pre_dec, Op.init([ "--", Level.prefix, false ]));
    table.set(Code.un_pre_inc, Op.init([ "++", Level.prefix, false ]));

    // Postfix update
    table.set(Code.un_post_dec, Op.init([ "--", Level.postfix, false ]));
    table.set(Code.un_post_inc, Op.init([ "++", Level.postfix, false ]));

    // Left-associative
    table.set(Code.bin_add, Op.init([ "+", Level.add, false ]));
    table.set(Code.bin_sub, Op.init([ "-", Level.add, false ]));
    table.set(Code.bin_mul, Op.init([ "*", Level.multiply, false ]));
    table.set(Code.bin_div, Op.init([ "/", Level.multiply, false ]));
    table.set(Code.bin_rem, Op.init([ "%", Level.multiply, false ]));
    table.set(Code.bin_pow, Op.init([ "**", Level.exponentiation, false ]));
    table.set(Code.bin_lt, Op.init([ "<", Level.compare, false ]));
    table.set(Code.bin_le, Op.init([ "<=", Level.compare, false ]));
    table.set(Code.bin_gt, Op.init([ ">", Level.compare, false ]));
    table.set(Code.bin_ge, Op.init([ ">=", Level.compare, false ]));
    table.set(Code.bin_in, Op.init([ "in", Level.compare, true ]));
    table.set(Code.bin_instanceof, Op.init([ "instanceof", Level.compare, true ]));
    table.set(Code.bin_shl, Op.init([ "<<", Level.shift, false ]));
    table.set(Code.bin_shr, Op.init([ ">>", Level.shift, false ]));
    table.set(Code.bin_u_shr, Op.init([ ">>>", Level.shift, false ]));
    table.set(Code.bin_loose_eq, Op.init([ "==", Level.equals, false ]));
    table.set(Code.bin_loose_ne, Op.init([ "!=", Level.equals, false ]));
    table.set(Code.bin_strict_eq, Op.init([ "===", Level.equals, false ]));
    table.set(Code.bin_strict_ne, Op.init([ "!==", Level.equals, false ]));
    table.set(Code.bin_nullish_coalescing, Op.init([ "??", Level.nullish_coalescing, false ]));
    table.set(Code.bin_logical_or, Op.init([ "||", Level.logical_or, false ]));
    table.set(Code.bin_logical_and, Op.init([ "&&", Level.logical_and, false ]));
    table.set(Code.bin_bitwise_or, Op.init([ "|", Level.bitwise_or, false ]));
    table.set(Code.bin_bitwise_and, Op.init([ "&", Level.bitwise_and, false ]));
    table.set(Code.bin_bitwise_xor, Op.init([ "^", Level.bitwise_xor, false ]));

    // Non-associative
    table.set(Code.bin_comma, Op.init([ ",", Level.comma, false ]));

    // Right-associative
    table.set(Code.bin_assign, Op.init([ "=", Level.assign, false ]));
    table.set(Code.bin_add_assign, Op.init([ "+=", Level.assign, false ]));
    table.set(Code.bin_sub_assign, Op.init([ "-=", Level.assign, false ]));
    table.set(Code.bin_mul_assign, Op.init([ "*=", Level.assign, false ]));
    table.set(Code.bin_div_assign, Op.init([ "/=", Level.assign, false ]));
    table.set(Code.bin_rem_assign, Op.init([ "%=", Level.assign, false ]));
    table.set(Code.bin_pow_assign, Op.init([ "**=", Level.assign, false ]));
    table.set(Code.bin_shl_assign, Op.init([ "<<=", Level.assign, false ]));
    table.set(Code.bin_shr_assign, Op.init([ ">>=", Level.assign, false ]));
    table.set(Code.bin_u_shr_assign, Op.init([ ">>>=", Level.assign, false ]));
    table.set(Code.bin_bitwise_or_assign, Op.init([ "|=", Level.assign, false ]));
    table.set(Code.bin_bitwise_and_assign, Op.init([ "&=", Level.assign, false ]));
    table.set(Code.bin_bitwise_xor_assign, Op.init([ "^=", Level.assign, false ]));
    table.set(Code.bin_nullish_coalescing_assign, Op.init([ "??=", Level.assign, false ]));
    table.set(Code.bin_logical_or_assign, Op.init([ "||=", Level.assign, false ]));
    table.set(Code.bin_logical_and_assign, Op.init([ "&&=", Level.assign, false ]));

    break :brk table;
};

import bun from "bun";

const js_ast = bun.ast;
const AssignTarget = js_ast.AssignTarget;
