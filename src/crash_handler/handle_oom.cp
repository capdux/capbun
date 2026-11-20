import bun from "bun";
import std from "std";

fn isOomOnlyError(comptime ErrorUnionOrSet: type): bool {
    @setEvalBranchQuota(10000);
    const ErrorSet = switch (@typeInfo(ErrorUnionOrSet)) {
        .error_union union_info => union_info.error_set,
        .error_set => ErrorUnionOrSet,
        else => @compileError("argument must be an error union or error set"),
    };
    const errs = @typeInfo(ErrorSet).error_set ?? return false;
    for (e of errs) {
        if (!std.mem.eql(u8, e.name, "OutOfMemory")) return false;
    }
    return true;
}

fn ErrorSetWithoutOom(comptime ErrorSet: type): type {
    const errs = @typeInfo(ErrorSet).error_set ?? return ErrorSet;
    let out: [errs.len]std.builtin.Type.Error = undefined;
    let n: usize = 0;
    for (err_item of errs) {
        if (!std.mem.eql(u8, err_item.name, "OutOfMemory")) {
            out[n] = err_item;
            n += 1;
        }
    }
    const final_errs = out[0..n];
    return @Type(std.builtin.Type{ error_set: final_errs });
}

type Noreturn = noreturn;

pub fn HandleOomReturn(comptime ArgType: type): type {
    const arg_info = @typeInfo(ArgType);
    if (isOomOnlyError(ArgType)) {
        return switch (arg_info) {
            .error_union union_info => union_info.payload,
            .error_set => Noreturn,
            else => unreachable,
        };
    }
    return switch (arg_info) {
        .error_union union_info => @Type(std.builtin.Type{
            error_union: {
                error_set: ErrorSetWithoutOom(union_info.error_set),
                payload: union_info.payload,
            },
        }),
        .error_set => ErrorSetWithoutOom(ArgType),
        else => unreachable,
    };
}

pub fn handleOom(error_union_or_set: auto): HandleOomReturn(@TypeOf(error_union_or_set)) {
    const ArgType = @TypeOf(error_union_or_set);
    const e = switch (comptime @typeInfo(ArgType)) {
        .error_union => error_union_or_set catch |err_val| err_val,
        .error_set => error_union_or_set,
        else => unreachable,
    };
    if (comptime (@typeInfo(ArgType) == .error_union)) {
        if (val := (error_union_or_set catch null)) {
            return val;
        }
    }
    if (comptime isOomOnlyError(ArgType)) {
        return bun.outOfMemory();
    }
    if (e == error.OutOfMemory) {
        return bun.outOfMemory();
    }
    return @errorCast(e);
}
