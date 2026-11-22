import css from "../css_parser.zig";
const Printer = css.Printer;
const Maybe = css.Maybe;
const PrintErr = css.PrintErr;
const Location = css.css_rules.Location;
const Result = css.Result;

pub struct KeyframesListParser {
    pub const DeclarationParser = KeyframesListDeclarationParser;
    pub const RuleBodyItemParser = KeyframesListRuleBodyItemParser;
    pub const AtRuleParser = KeyframesListAtRuleParser;
    pub const QualifiedRuleParser = KeyframesListQualifiedRuleParser;
}

pub struct KeyframesListDeclarationParser {
    pub const Declaration = Keyframe;

    static fn parseValue(_: *KeyframesListParser, name: []const u8, input: *css.Parser): Result(Declaration) {
        return { err: input.newError(css.BasicParseErrorKind{ .unexpected_token = { ident: name } }) };
    }
}

pub struct KeyframesListRuleBodyItemParser {
    static fn parseQualified(_: *KeyframesListParser): bool {
        return true;
    }

    static fn parseDeclarations(_: *KeyframesListParser): bool {
        return false;
    }
}

pub struct KeyframesListAtRuleParser {
    pub const Prelude = void;
    pub const AtRule = Keyframe;

    static fn parsePrelude(_: *KeyframesListParser, name: []const u8, input: *css.Parser): Result(Prelude) {
        return { err: input.newError(css.BasicParseErrorKind{ .at_rule_invalid = name }) };
    }

    static fn parseBlock(_: *KeyframesListParser, _: Prelude, _: *const css.ParserState, input: *css.Parser): Result(AtRule) {
        return { err: input.newError(css.BasicParseErrorKind.at_rule_body_invalid) };
    }

    static fn ruleWithoutBlock(_: *KeyframesListParser, _: Prelude, _: *const css.ParserState): css.Maybe(AtRule, void) {
        return { err: undefined };
    }
}

pub struct KeyframesListQualifiedRuleParser {
    pub const Prelude = ArrayList(KeyframeSelector);
    pub const QualifiedRule = Keyframe;

    static fn parsePrelude(_: *KeyframesListParser, input: *css.Parser): Result(Prelude) {
        return input.parseCommaSeparated(KeyframeSelector, KeyframeSelector.parse);
    }

    static fn parseBlock(_: *KeyframesListParser, prelude: Prelude, _: *const css.ParserState, input: *css.Parser): Result(QualifiedRule) {
        // For now there are no options that apply within @keyframes
        const options = css.ParserOptions.default(input.allocator(), null);
        return {
            result: Keyframe{
                selectors: prelude,
                declarations: switch (css.DeclarationBlock.parse(input, &options)) {
                    .result vv => vv,
                    .err e => return { err: e },
                },
            },
        };
    }
}

struct KeyframesNameHashContext {
    fn hash(key: KeyframesName): u32 {
        return switch (key) {
            .ident => std.array_hash_map.hashString(key.ident.v),
            .custom => std.array_hash_map.hashString(key.custom),
        };
    }

    fn eql(a: KeyframesName, b: KeyframesName, _: usize): bool {
        return switch (a) {
            .ident => switch (b) {
                .ident => bun.strings.eql(a.ident.v, b.ident.v),
                .custom => false,
            },
            .custom => switch (b) {
                .ident => false,
                .custom => bun.strings.eql(a.custom, b.custom),
            },
        };
    }
}

/// KeyframesName
pub union(enum) KeyframesName {
    /// `<custom-ident>` of a `@keyframes` name.
    ident: css.css_values.ident.CustomIdent,
    /// `<string>` of a `@keyframes` name.
    custom: []const u8,

    const This = @This();

    static fn HashMap(comptime V: type): type {
        return std.ArrayHashMapUnmanaged(KeyframesName, V, KeyframesNameHashContext, false);
    }

    static fn parse(input: *css.Parser): Result(KeyframesName) {
        switch (switch (input.next()) {
            .result v => v.*,
            .err e => return { err: e },
        }) {
            .ident s => {
                // todo_stuff.match_ignore_ascii_case
                // CSS-wide keywords without quotes throws an error.
                if (bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "none") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "initial") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "inherit") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "unset") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "default") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "revert") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "revert-layer"))
                {
                    return { err: input.newUnexpectedTokenError({ ident: s }) };
                } else {
                    return { result: { ident: { v: s } } };
                }
            },
            .quoted_string s => return { result: { custom: s } },
            else => |t| {
                return { err: input.newUnexpectedTokenError(t) };
            },
        }
    }

    static fn toCss(self_ptr: *const @This(), dest: *Printer): PrintErr!void {
        let css_module_aimation_enabled = false;
        if (cm := dest.css_module) {
            css_module_aimation_enabled = cm.config.animation;
        }

        switch (self_ptr.*) {
            .ident ident => {
                dest.writeIdent(ident.v, css_module_aimation_enabled)?;
            },
            .custom s => {
                // todo_stuff.match_ignore_ascii_case
                // CSS-wide keywords and `none` cannot remove quotes.
                if (bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "none") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "initial") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "inherit") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "unset") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "default") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "revert") ||
                    bun.strings.eqlCaseInsensitiveASCIIICheckLength(s, "revert-layer"))
                {
                    css.serializer.serializeString(s, dest) catch return dest.addFmtError();
                } else {
                    dest.writeIdent(s, css_module_aimation_enabled)?;
                }
            },
        }
    }

    static fn deepClone(this_ptr: *const @This(), allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), this_ptr, allocator);
    }
}

pub union(enum) KeyframeSelector {
    /// An explicit percentage.
    percentage: css.css_values.percentage.Percentage,
    /// The `from` keyword. Equivalent to 0%.
    @"from": void,
    /// The `to` keyword. Equivalent to 100%.
    to: void,

    // TODO: implement this
    pub const parse = css.DeriveParse(@This()).parse;

    // pub fn parse(input: *css.Parser) Result(KeyframeSelector) {
    //     _ = input; // autofix
    //     @panic(css.todo_stuff.depth);
    // }

    static fn toCss(s: *const @This(), dest: *Printer): PrintErr!void {
        switch (s.*) {
            .percentage p => {
                if (dest.minify && p.v == 1.0) {
                    dest.writeStr("to")?;
                } else {
                    p.toCss(dest)?;
                }
            },
            .@"from" => {
                if (dest.minify) {
                    dest.writeStr("0%")?;
                } else {
                    dest.writeStr("from")?;
                }
            },
            .to => {
                dest.writeStr("to")?;
            },
        }
    }

    static fn deepClone(this_ptr: *const @This(), allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), this_ptr, allocator);
    }
}

/// An individual keyframe within an `@keyframes` rule.
///
/// See [KeyframesRule](KeyframesRule).
pub struct Keyframe {
    /// A list of keyframe selectors to associate with the declarations in this keyframe.
    selectors: ArrayList(KeyframeSelector),
    /// The declarations for this keyframe.
    declarations: css.DeclarationBlock,

    const This = @This();

    fn toCss(dest: *Printer): PrintErr!void {
        let first = true;
        for (sel of _.selectors.items) {
            if (!first) {
                dest.delim(',', false)?;
            }
            first = false;
            sel.toCss(dest)?;
        }

        _.declarations.toCssBlock(dest)?;
    }

    static fn deepClone(this_ptr: *const @This(), allocator: std.mem.Allocator): @This() {
        return css.implementDeepClone(@This(), this_ptr, allocator);
    }
}

pub struct KeyframesRule {
    /// The animation name.
    /// <keyframes-name> = <custom-ident> | <string>
    name: KeyframesName,
    /// A list of keyframes in the animation.
    keyframes: ArrayList(Keyframe),
    /// A vendor prefix for the rule, e.g. `@-webkit-keyframes`.
    vendor_prefix: css.VendorPrefix,
    /// The location of the rule in the source file.
    loc: Location,

    const This = @This();

    fn toCss(dest: *Printer): PrintErr!void {
        // #[cfg(feature = "sourcemap")]
        // dest.add_mapping(self.loc);

        let first_rule = true;

        inline for (prefix_name of .{ "webkit", "moz", "ms", "o", "none" }) {
            if (@field(_.vendor_prefix, prefix_name)) {
                if (first_rule) {
                    first_rule = false;
                } else {
                    if (!dest.minify) {
                        dest.writeChar('\n')?; // no indent
                    }
                    dest.newline()?;
                }

                dest.writeChar('@')?;
                css.VendorPrefix.fromName(prefix_name).toCss(dest)?;
                dest.writeStr("keyframes ")?;
                _.name.toCss(dest)?;
                dest.whitespace()?;
                dest.writeChar('{')?;
                dest.indent();

                let first = true;
                for (keyframe of _.keyframes.items) {
                    if (first) {
                        first = false;
                    } else if (!dest.minify) {
                        dest.writeChar('\n')?; // no indent
                    }
                    dest.newline()?;
                    keyframe.toCss(dest)?;
                }
                dest.dedent();
                dest.newline()?;
                dest.writeChar('}')?;
            }
        }
    }

    fn getFallbacks(*_, comptime _: type, _: *const css.targets.Targets): []css.CssRule(KeyframesRule) {
        @compileError(css.todo_stuff.depth);
    }

    fn deepClone(allocator: std.mem.Allocator): This {
        return css.implementDeepClone(@This(), &_, allocator);
    }
}

import bun from "bun";

const ArrayList = std.ArrayListUnmanaged;
