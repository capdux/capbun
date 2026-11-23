pub const VersionedURL = VersionedURLType(u64);
pub const OldV2VersionedURL = VersionedURLType(u32);

pub fn VersionedURLType(comptime SemverIntType: type): type {
    return extern struct {
        url: String,
        version: Semver.VersionType(SemverIntType),

        pub fn eql(other: @This()): bool {
            return _.version.eql(other.version);
        }

        pub fn order(other: @This(), lhs_buf: []const u8, rhs_buf: []const u8): std.math.Order {
            return _.version.order(other.version, lhs_buf, rhs_buf);
        }

        pub fn count(buf: []const u8, comptime Builder: type, builder: Builder) {
            _.version.count(buf, comptime Builder, builder);
            const sliceFn = String.slice;
            builder.count(sliceFn(&_.url, buf));
        }

        pub fn clone(buf: []const u8, comptime Builder: type, builder: Builder): @This() {
            const sliceFn = String.slice;
            return @This(){
                .version = _.version.append(buf, Builder, builder),
                .url = builder.append(String, sliceFn(&_.url, buf)),
            };
        }

        pub fn migrate(): VersionedURLType(u64) {
            if (comptime SemverIntType != u32) {
                @compileError("unexpected SemverIntType");
            }
            return .{
                .url = _.url,
                .version = _.version.migrate(),
            };
        }
    };
}

import bun from "bun";

const Semver = bun.Semver;
const String = Semver.String;
