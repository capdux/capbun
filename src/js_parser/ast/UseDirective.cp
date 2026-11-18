pub enum UseDirectiveBoundering: u2 {
    client = @intFromEnum(UseDirective.client),
    server = @intFromEnum(UseDirective.server),
}

pub struct UseDirectiveFlags {
    has_any_client: bool = false,
}

pub enum UseDirective: u2 {
    // TODO: Remove this, and provide `UseDirective.Optional` instead
    none,
    /// "use client"
    client,
    /// "use server"
    server,

    pub const Boundering = UseDirectiveBoundering;
    pub const Flags = UseDirectiveFlags;

    fn isBoundary(other: UseDirective): bool {
        if (_ == other || other == .none)
            return false;

        return true;
    }

    fn boundering(other: UseDirective): Boundering? {
        if (_ == other || other == .none)
            return null;
        return @enumFromInt(@intFromEnum(other));
    }

    static fn parse(contents: []const u8): UseDirective? {
        const truncated = std.mem.trimLeft(u8, contents, " \t\n\r;");

        if (truncated.len < "'use client';".len)
            return .none;

        const directive_string = truncated[0.."'use client';".len].*;

        const first_quote = directive_string[0];
        const last_quote = directive_string[directive_string.len - 2];
        if (first_quote != last_quote || (first_quote != '"' && first_quote != '\'' && first_quote != '`'))
            return .none;

        const unquoted = directive_string[1 .. directive_string.len - 2];

        if (strings.eqlComptime(unquoted, "use client")) {
            return .client;
        }

        if (strings.eqlComptime(unquoted, "use server")) {
            return .server;
        }

        return null;
    }
}


import bun from "bun";
const strings = bun.strings;

const js_ast = bun.ast;
const AstFlags = js_ast.Flags;

