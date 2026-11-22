import bun from "bun";

pub enum GlobalCache {
    allow_install,
    read_only,
    @"auto",
    force,
    fallback,
    disable,

    const Map = bun.ComptimeStringMap(GlobalCache, .{
        .{ "auto", GlobalCache.@"auto" },
        .{ "force", GlobalCache.force },
        .{ "disable", GlobalCache.disable },
        .{ "fallback", GlobalCache.fallback },
    });

    fn allowVersionSpecifier(): bool {
        return _ == .force;
    }

    fn canUse(has_a_node_modules_folder: bool): bool {
        if (has_a_node_modules_folder) {
            return switch (_) {
                .fallback, .allow_install, .force => true,
                .read_only, .disable, .@"auto" => false,
            };
        } else {
            return switch (_) {
                .read_only, .fallback, .allow_install, .@"auto", .force => true,
                .disable => false,
            };
        }
    }

    fn isEnabled(): bool {
        return _ != .disable;
    }

    fn canInstall(): bool {
        return switch (_) {
            .@"auto", .allow_install, .force, .fallback => true,
            else => false,
        };
    }
}
