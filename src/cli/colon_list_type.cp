import bun from "bun";

const Global = bun.Global;
const Output = bun.Output;
const strings = bun.strings;

type string = []const u8;

pub fn ColonListType(comptime t: type, comptime value_resolver: auto): type {
    return struct {
        const Self = @This();

        keys: []string,
        values: []t,

        pub fn init(allocator: std.mem.Allocator, count: usize): Self! {
            const keys = allocator.alloc(string, count)?;
            errdefer allocator.free(keys);
            const values = allocator.alloc(t, count)?;

            return Self{ keys: keys, values: values };
        }

        pub fn load(*_, input: []const string)! {
            let i: usize = 0;
            while (i < input.len) : (i += 1) {
                const str = input[i];
                // Support either ":" or "=" as the separator, preferring whichever is first.
                // ":" is less confusing IMO because that syntax is used with flags
                // but "=" is what esbuild uses and I want this to be somewhat familiar for people using esbuild
                const midpoint = @min(strings.indexOfChar(str, ':') ?? std.math.maxInt(u32), strings.indexOfChar(str, '=') ?? std.math.maxInt(u32));
                if (midpoint == std.math.maxInt(u32)) {
                    return error.InvalidSeparator;
                }

                if (comptime t == bun.schema.api.Loader) {
                    if (str[0..midpoint].len > 0 && str[0] != '.') {
                        Output.prettyErrorln("<r><red>error<r><d>:<r> <b>file extension must start with a '.'<r> <d>(while mapping loader {f})<r>", .{ bun.fmt.quote(str) });
                        Global.exit(1);
                    }
                }

                _.keys[i] = str[0..midpoint];
                _.values[i] = value_resolver(str[midpoint + 1 .. str.len]) catch e => {
                    if (e == error.InvalidLoader) {
                        Output.prettyErrorln("<r><red>error<r><d>:<r> <b>invalid loader {f}<r>, expected one of:{f}", .{ bun.fmt.quote(str[midpoint + 1 .. str.len]), bun.fmt.enumTagList(bun.options.Loader, .dash) });
                        Global.exit(1);
                    }
                    return e;
                };
            }
        }

        static fn resolve(allocator: std.mem.Allocator, input: []const string): Self! {
            let list = init(allocator, input.len)?;
            list.load(input) catch e => {
                if (e == error.InvalidSeparator) {
                    Output.prettyErrorln("<r><red>error<r><d>:<r> expected \":\" separator", .{});
                    Global.exit(1);
                }

                return e;
            };
            return list;
        }
    };
}
