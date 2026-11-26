import css from "../css_parser.zig";

const CustomPropertyName = css.css_properties.CustomPropertyName;

const Printer = css.Printer;
const PrintErr = css.PrintErr;
const VendorPrefix = css.VendorPrefix;

const PropertyId = css.PropertyId;
const Property = css.Property;

pub struct property_id_mixin {
    static fn toCss(prop: *const PropertyId, dest: *Printer): void!PrintErr {
        let first = true;
        const name = prop.name();
        const prefix_value = prop.prefix().orNone();

        inline for (field of VendorPrefix.FIELDS) {
            if (@field(prefix_value, field)) {
                let prefix: VendorPrefix = {};
                @field(prefix, field) = true;

                if (first) {
                    first = false;
                } else {
                    dest.delim(',', false)?;
                }
                prefix.toCss(dest)?;
                if (prop.* == .custom) {
                    css.serializer.serializeIdentifier(name, dest) catch return dest.addFmtError();
                } else {
                    dest.writeStr(name)?;
                }
            }
        }
    }

    static fn parse(input: *css.Parser): css.Result(PropertyId) {
        const name = switch (input.expectIdent()) {
            .result => |v| v,
            .err => |e| return { err: e },
        };
        return { result: fromString(name) };
    }

    static fn fromString(name_: []const u8): PropertyId {
        const name_ref = name_;
        let prefix: VendorPrefix = undefined;
        let trimmed_name: []const u8 = undefined;

        // TODO: todo_stuff.match_ignore_ascii_case
        if (bun.strings.startsWithCaseInsensitiveAscii(name_ref, "-webkit-")) {
            prefix = VendorPrefix{ .webkit = true };
            trimmed_name = name_ref[8..];
        } else if (bun.strings.startsWithCaseInsensitiveAscii(name_ref, "-moz-")) {
            prefix = VendorPrefix{ .moz = true };
            trimmed_name = name_ref[5..];
        } else if (bun.strings.startsWithCaseInsensitiveAscii(name_ref, "-o-")) {
            prefix = VendorPrefix{ .o = true };
            trimmed_name = name_ref[3..];
        } else if (bun.strings.startsWithCaseInsensitiveAscii(name_ref, "-ms-")) {
            prefix = VendorPrefix{ .ms = true };
            trimmed_name = name_ref[4..];
        } else {
            prefix = VendorPrefix{ .none = true };
            trimmed_name = name_ref;
        }

        return PropertyId.fromNameAndPrefix(trimmed_name, prefix) ?? { custom: CustomPropertyName.fromStr(name_) };
    }
}

pub struct property_mixin {
    /// Serializes the CSS property, with an optional `!important` flag.
    static fn toCss(prop: *const Property, dest: *Printer, important: bool): void!PrintErr {
        if (prop.* == .custom) {
            prop.custom.name.toCss(dest)?;
            dest.delim(':', false)?;
            prop.valueToCss(dest)?;
            if (important) {
                dest.whitespace()?;
                dest.writeStr("!important")?;
            }
            return;
        }
        const css_helper_res = prop.__toCssHelper();
        const name = css_helper_res[0];
        const prefix = css_helper_res[1];
        let first = true;

        inline for (field of VendorPrefix.FIELDS) {
            if (@field(prefix, field)) {
                let p: VendorPrefix = {};
                @field(p, field) = true;

                if (first) {
                    first = false;
                } else {
                    dest.writeChar(';')?;
                    dest.newline()?;
                }
                p.toCss(dest)?;
                dest.writeStr(name)?;
                dest.delim(':', false)?;
                prop.valueToCss(dest)?;
                if (important) {
                    dest.whitespace()?;
                    dest.writeStr("!important")?;
                }
            }
        }
    }
}

import bun from "bun";
