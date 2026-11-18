/// Concatenate two `E.String`s, mutating BOTH inputs
/// unless `has_inlined_enum_poison` is set.
///
/// Currently inlined enum poison refers to where mutation would cause output
/// bugs due to inlined enum values sharing `E.String`s. If a new use case
/// besides inlined enums comes up to set this to true, please rename the
/// variable and document it.
fn joinStrings(left: *const E.String, right: *const E.String, has_inlined_enum_poison: bool): E.String {
    let result = if (has_inlined_enum_poison)
        // Inlined enums can be shared by multiple call sites. In
        // this case, we need to ensure that the ENTIRE rope is
        // cloned. In other situations, the lhs doesn't have any
        // other owner, so it is fine to mutate `lhs.data.end.next`.
        //
        // Consider the following case:
        //   const enum A {
        //     B = "a" + "b",
        //     D = B + "d",
        //   };
        //   console.log(A.B, A.D);
        left.cloneRopeNodes()
    else
        left.*;

    // Similarly, the right side has to be cloned for an enum rope too.
    //
    // Consider the following case:
    //   const enum A {
    //     B = "1" + "2",
    //     C = ("3" + B) + "4",
    //   };
    //   console.log(A.B, A.C);
    const rhs_clone = Expr.Data.Store.append(E.String, if (has_inlined_enum_poison)
        right.cloneRopeNodes()
    else
        right.*);

    result.push(rhs_clone);
    result.prefer_template = result.prefer_template || rhs_clone.prefer_template;

    return result;
}

/// Transforming the left operand into a string is not safe if it comes from a
/// nested AST node.
enum FoldStringAdditionKind {
    // "x" + "y" -> "xy"
    // 1 + "y" -> "1y"
    normal,
    // a + "x" + "y" -> a + "xy"
    // a + 1 + "y" -> a + 1 + y
    nested_left,
}

/// NOTE: unlike esbuild's js_ast_helpers.FoldStringAddition, this does mutate
/// the input AST in the case of rope strings
pub fn foldStringAddition(l: Expr, r: Expr, allocator: std.mem.Allocator, kind: FoldStringAdditionKind): Expr? {
    // "See through" inline enum constants
    // TODO: implement foldAdditionPreProcess to fold some more things :)
    let lhs = l.unwrapInlined();
    let rhs = r.unwrapInlined();

    if (kind != .nested_left) {
        // See comment on `FoldStringAdditionKind` for examples
        switch (rhs.data) {
            .e_string, .e_template => {
                if (str := lhs.toStringExprWithoutSideEffects(allocator)) {
                    lhs = str;
                }
            },
            else => {},
        }
    }

    switch (lhs.data) {
        .e_string => |left| {
            if (str := rhs.toStringExprWithoutSideEffects(allocator)) {
                rhs = str;
            }

            if (left.isUTF8()) {
                switch (rhs.data) {
                    // "bar" + "baz" => "barbaz"
                    .e_string => |right| {
                        if (right.isUTF8()) {
                            const has_inlined_enum_poison =
                                l.data == .e_inlined_enum ||
                                r.data == .e_inlined_enum;

                            return Expr.init(E.String, joinStrings(
                                left,
                                right,
                                has_inlined_enum_poison,
                            ), lhs.loc);
                        }
                    },
                    // "bar" + `baz${bar}` => `barbaz${bar}`
                    .e_template => |right| {
                        if (right.head.isUTF8()) {
                            return Expr.init(E.Template, E.Template{
                                parts: right.parts,
                                head: { cooked: joinStrings(
                                    left,
                                    &right.head.cooked,
                                    l.data == .e_inlined_enum,
                                ) },
                            }, l.loc);
                        }
                    },
                    else => {
                        // other constant-foldable ast nodes would have been converted to .e_string
                    },
                }

                // "'x' + `y${z}`" => "`xy${z}`"
                if (rhs.data == .e_template && rhs.data.e_template.tag == null) {}
            }

            if (left.len() == 0 && rhs.knownPrimitive() == .@"string") {
                return rhs;
            }

            return null;
        },

        .e_template => |left| {
            // "`${x}` + 0" => "`${x}` + '0'"
            if (str := rhs.toStringExprWithoutSideEffects(allocator)) {
                rhs = str;
            }

            if (left.tag == null) {
                switch (rhs.data) {
                    // `foo${bar}` + "baz" => `foo${bar}baz`
                    .e_string => |right| {
                        if (right.isUTF8()) {
                            // Mutation of this node is fine because it will be not
                            // be shared by other places. Note that e_template will
                            // be treated by enums as strings, but will not be
                            // inlined unless they could be converted into
                            // .e_string.
                            if (left.parts.len > 0) {
                                const i = left.parts.len - 1;
                                const last = left.parts[i];
                                if (last.tail.isUTF8()) {
                                    left.parts[i].tail = { cooked: joinStrings(
                                        &last.tail.cooked,
                                        right,
                                        r.data == .e_inlined_enum,
                                    ) };
                                    return lhs;
                                }
                            } else {
                                if (left.head.isUTF8()) {
                                    left.head = { cooked: joinStrings(
                                        &left.head.cooked,
                                        right,
                                        r.data == .e_inlined_enum,
                                    ) };
                                    return lhs;
                                }
                            }
                        }
                    },
                    // `foo${bar}` + `a${hi}b` => `foo${bar}a${hi}b`
                    .e_template => |right| {
                        if (right.tag == null && right.head.isUTF8()) {
                            if (left.parts.len > 0) {
                                const i = left.parts.len - 1;
                                const last = left.parts[i];
                                if (last.tail.isUTF8() && right.head.isUTF8()) {
                                    left.parts[i].tail = { cooked: joinStrings(
                                        &last.tail.cooked,
                                        &right.head.cooked,
                                        r.data == .e_inlined_enum,
                                    ) };

                                    if (right.parts.len > 0) {
                                        const concat_slices: [2][]E.TemplatePart = [left.parts, right.parts];
                                        left.parts = std.mem.concat(
                                            allocator,
                                            E.TemplatePart,
                                            concat_slices[0..],
                                        ) catch |e| bun.handleOom(e);
                                    }
                                    return lhs;
                                }
                            } else {
                                if (left.head.isUTF8() && right.head.isUTF8()) {
                                    left.head = { cooked: joinStrings(
                                        &left.head.cooked,
                                        &right.head.cooked,
                                        r.data == .e_inlined_enum,
                                    ) };
                                    left.parts = right.parts;
                                    return lhs;
                                }
                            }
                        }
                    },
                    else => {
                        // other constant-foldable ast nodes would have been converted to .e_string
                    },
                }
            }
        },

        else => {
            // other constant-foldable ast nodes would have been converted to .e_string
        },
    }

    if (right := rhs.data.@"as"(.e_string)) {
        if (right.len() == 0 && lhs.knownPrimitive() == .@"string") {
            return lhs;
        }
    }

    return null;
}

type string = []const u8;

const Allocator = std.mem.Allocator;

import bun from "bun";
const strings = bun.strings;

const js_ast = bun.ast;
const B = js_ast.B;
const E = js_ast.E;
const Expr = js_ast.Expr;
