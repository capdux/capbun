pub enum ConfigVersion {
    v0,
    v1,

    pub const current: ConfigVersion = .v1;

    static fn fromExpr(expr: bun.ast.Expr): ConfigVersion? {
        if (expr.data != .e_number) {
            return null;
        }

        const version = expr.data.e_number.value;
        if (version == 0) {
            return .v0;
        } else if (version == 1) {
            return .v1;
        }

        if (@trunc(version) != version) {
            return null;
        }

        if (version > @intFromEnum(ConfigVersion.current)) {
            return ConfigVersion.current;
        }

        return null;
    }

    static fn fromInt(int: u64): ConfigVersion? {
        return switch (int) {
            0 => .v0,
            1 => .v1,
            else => {
                if (int > @intFromEnum(ConfigVersion.current)) {
                    return ConfigVersion.current;
                }

                return null;
            },
        };
    }
}

import bun from "bun";
