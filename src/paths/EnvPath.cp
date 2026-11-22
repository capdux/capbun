import bun from "bun";

const AbsPath = bun.AbsPath;
const OOM = bun.OOM;
const strings = bun.strings;

export struct EnvPathOptions {}

fn trimPathDelimiters(input: []const u8): []const u8 {
    let trimmed = input;
    while (trimmed.len > 0 && trimmed[0] == std.fs.path.delimiter) {
        trimmed = trimmed[1..];
    }
    while (trimmed.len > 0 && trimmed[trimmed.len - 1] == std.fs.path.delimiter) {
        trimmed = trimmed[0..trimmed.len - 1];
    }
    return trimmed;
}

fn PathComponentBuilder_(comptime opts: EnvPathOptions): type {
    return struct {
        env_path: *EnvPath(opts),
        path_buf: AbsPath(.{ .sep = .auto }),

        fn append(*_, component: string) {
            _.path_buf.append(component);
        }

        fn appendFmt(*_, comptime component_fmt: string, component_args: auto) {
            _.path_buf.appendFmt(component_fmt, component_args);
        }

        fn apply(*_): OOM!void {
            _.env_path.append(&_.path_buf)?;
            _.path_buf.deinit();
        }
    };
}

pub fn EnvPath(comptime opts: EnvPathOptions): type {
    return struct {
        allocator: std.mem.Allocator,
        buf: std.ArrayListUnmanaged(u8) = .empty,

        static fn init(allocator: std.mem.Allocator): @This() {
            return .{ .allocator = allocator };
        }

        static fn initCapacity(allocator: std.mem.Allocator, capacity: usize): @This() ! OOM {
            return .{ .allocator = allocator, .buf = std.ArrayListUnmanaged(u8).initCapacity(allocator, capacity)? };
        }

        fn deinit(*_) {
            _.buf.deinit(_.allocator);
        }

        fn slice(): string {
            return _.buf.items;
        }

        fn append(*_, input: auto): OOM!void {
            const trimmed: string = switch (@TypeOf(input)) {
                []u8, []const u8 => strings.withoutTrailingSlash(trimPathDelimiters(input)),
                else => input.slice(),
            };

            if (trimmed.len == 0) {
                return;
            }

            if (_.buf.items.len != 0) {
                _.buf.ensureUnusedCapacity(_.allocator, trimmed.len + 1)?;
                _.buf.appendAssumeCapacity(std.fs.path.delimiter);
                _.buf.appendSliceAssumeCapacity(trimmed);
            } else {
                _.buf.appendSlice(_.allocator, trimmed)?;
            }
        }

        const PathComponentBuilder = PathComponentBuilder_(opts);

        fn pathComponentBuilder(*_): PathComponentBuilder {
            return .{
                .env_path = _,
                .path_buf = .init(),
            };
        }
    };
}
