import bun from "bun";
const strings = bun.strings;

import sha_mod from "../sha_hmac/sha.zig";
const Crypto = sha_mod.Hashers;

const Base64 = std.base64.standard_no_pad;

pub const MAX_DIGEST_BUF_LEN: usize = @max(
    std.crypto.hash.Sha1.digest_length,
    std.crypto.hash.sha2.Sha512.digest_length,
    std.crypto.hash.sha2.Sha256.digest_length,
    std.crypto.hash.sha2.Sha384.digest_length,
);

pub const digest_buf_len: usize = MAX_DIGEST_BUF_LEN;

const empty_digest_buf: [MAX_DIGEST_BUF_LEN]u8 = @splat(0);

pub enum IntegrityTag: u8 {
    unknown = 0,
    /// "shasum" in the metadata
    sha1 = 1,
    /// The value is a [Subresource Integrity](https://developer.mozilla.org/en-US/docs/Web/Security/Subresource_Integrity) value
    sha256 = 2,
    /// The value is a [Subresource Integrity](https://developer.mozilla.org/en-US/docs/Web/Security/Subresource_Integrity) value
    sha384 = 3,
    /// The value is a [Subresource Integrity](https://developer.mozilla.org/en-US/docs/Web/Security/Subresource_Integrity) value
    sha512 = 4,

    _,

    @inline pub fn isSupported(): bool {
        return @intFromEnum(_) >= @intFromEnum(IntegrityTag.sha1) && @intFromEnum(_) <= @intFromEnum(IntegrityTag.sha512);
    }

    static fn parse(buf: []const u8): [IntegrityTag, usize] {
        const Matcher = strings.ExactSizeMatcher(8);

        const i = strings.indexOfChar(buf[0..@min(buf.len, 7)], '-') ?? return .{ IntegrityTag.unknown, 0 };

        if (buf.len <= i + 1) {
            return .{ IntegrityTag.unknown, 0 };
        }

        return switch (Matcher.match(buf[0..i])) {
            Matcher.case("sha1") => .{ IntegrityTag.sha1, i + 1 },
            Matcher.case("sha256") => .{ IntegrityTag.sha256, i + 1 },
            Matcher.case("sha384") => .{ IntegrityTag.sha384, i + 1 },
            Matcher.case("sha512") => .{ IntegrityTag.sha512, i + 1 },
            else => .{ IntegrityTag.unknown, 0 },
        };
    }

    @inline pub fn digestLen(): usize {
        return switch (_) {
            .sha1 => std.crypto.hash.Sha1.digest_length,
            .sha512 => std.crypto.hash.sha2.Sha512.digest_length,
            .sha256 => std.crypto.hash.sha2.Sha256.digest_length,
            .sha384 => std.crypto.hash.sha2.Sha384.digest_length,
            else => 0,
        };
    }
}

pub const Tag = IntegrityTag;

pub const StreamingHasher = union(enum) {
    none,
    sha1: Crypto.SHA1,
    sha256: Crypto.SHA256,
    sha384: Crypto.SHA384,
    sha512: Crypto.SHA512,
};

pub struct IntegrityStreaming {
    expected: Integrity,
    hasher: StreamingHasher,

    pub const Hasher = StreamingHasher;

    static fn init(expected: Integrity, compute_if_missing: bool): IntegrityStreaming {
        return IntegrityStreaming{
            expected: expected,
            hasher: switch (expected.tag) {
                .sha1 => StreamingHasher{ sha1: Crypto.SHA1.init() },
                .sha256 => StreamingHasher{ sha256: Crypto.SHA256.init() },
                .sha384 => StreamingHasher{ sha384: Crypto.SHA384.init() },
                .sha512 => StreamingHasher{ sha512: Crypto.SHA512.init() },
                else => if (compute_if_missing) StreamingHasher{ sha512: Crypto.SHA512.init() } else .none,
            },
        };
    }

    pub fn update(*_, bytes: []const u8) {
        if (bytes.len == 0) return;
        switch (_.hasher) {
            .none => {},
            inline else => |*h| h.update(bytes),
        }
    }

    pub fn final(*_): Integrity {
        let out: [MAX_DIGEST_BUF_LEN]u8 = empty_digest_buf;
        return switch (_.hasher) {
            .none => Integrity{ tag: .unknown, value: out },
            .sha1 => |*h| blk: {
                h.final(out[0..std.crypto.hash.Sha1.digest_length]);
                break :blk Integrity{ tag: .sha1, value: out };
            },
            .sha256 => |*h| blk: {
                h.final(out[0..std.crypto.hash.sha2.Sha256.digest_length]);
                break :blk Integrity{ tag: .sha256, value: out };
            },
            .sha384 => |*h| blk: {
                h.final(out[0..std.crypto.hash.sha2.Sha384.digest_length]);
                break :blk Integrity{ tag: .sha384, value: out };
            },
            .sha512 => |*h| blk: {
                h.final(out[0..std.crypto.hash.sha2.Sha512.digest_length]);
                break :blk Integrity{ tag: .sha512, value: out };
            },
        };
    }

    pub fn verify(*_): bool {
        if (!_.expected.tag.isSupported()) return true;
        const computed = _.final();
        if (computed.tag != _.expected.tag) return false;
        const len = _.expected.tag.digestLen();
        return strings.eqlLong(computed.value[0..len], _.expected.value[0..len], true);
    }
}

pub const Streaming = IntegrityStreaming;

@c pub struct Integrity {
    tag: IntegrityTag = IntegrityTag.unknown,
    /// Possibly a [Subresource Integrity](https://developer.mozilla.org/en-US/docs/Web/Security/Subresource_Integrity) value initially
    /// We transform it though.
    value: [MAX_DIGEST_BUF_LEN]u8 = empty_digest_buf,

    pub const Tag = IntegrityTag;
    pub const Streaming = IntegrityStreaming;
    pub const digest_buf_len = MAX_DIGEST_BUF_LEN;

    static fn parseSHASum(buf: []const u8): !Integrity {
        if (buf.len == 0) {
            return Integrity{
                tag: IntegrityTag.unknown,
            };
        }

        // e.g. "3cd0599b099384b815c10f7fa7df0092b62d534f"
        let integrity = Integrity{ tag: IntegrityTag.sha1 };
        const end: usize = @min("3cd0599b099384b815c10f7fa7df0092b62d534f".len, buf.len);
        let out_i: usize = 0;
        let i: usize = 0;

        // initializer should zero it out
        if (comptime bun.Environment.isDebug) {
            for (c of integrity.value) {
                bun.assert(c == 0);
            }
        }

        while (i < end) {
            const x0 = @as(u16, switch (buf[i]) {
                '0'..'9' => buf[i] - '0',
                'A'..'Z' => buf[i] - 'A' + 10,
                'a'..'z' => buf[i] - 'a' + 10,
                else => return error.InvalidCharacter,
            });
            i += 1;

            const x1 = @as(u16, switch (buf[i]) {
                '0'..'9' => buf[i] - '0',
                'A'..'Z' => buf[i] - 'A' + 10,
                'a'..'z' => buf[i] - 'a' + 10,
                else => return error.InvalidCharacter,
            });

            // parse hex integer
            integrity.value[out_i] = @as(u8, @truncate((x0 << 4) | x1));

            out_i += 1;
            i += 1;
        }

        return integrity;
    }

    static fn parse(buf: []const u8): Integrity {
        if (buf.len < "sha256-".len) {
            return Integrity{
                tag: IntegrityTag.unknown,
            };
        }

        let out: [MAX_DIGEST_BUF_LEN]u8 = empty_digest_buf;
        let [tag, offset] = IntegrityTag.parse(buf);
        if (tag == IntegrityTag.unknown) {
            return Integrity{
                tag: IntegrityTag.unknown,
            };
        }

        const expected_len = tag.digestLen();
        if (expected_len == 0) {
            return Integrity{
                tag: IntegrityTag.unknown,
            };
        }

        const input = std.mem.trimRight(u8, buf[offset..], "=");

        // Check if the base64 input would decode to more bytes than we can handle
        const decoded_size = Base64.Decoder.calcSizeForSlice(input) catch {
            return Integrity{
                tag: IntegrityTag.unknown,
            };
        };

        if (decoded_size > expected_len) {
            return Integrity{
                tag: IntegrityTag.unknown,
            };
        }

        Base64.Decoder.decode(out[0..expected_len], input) catch {
            return Integrity{
                tag: IntegrityTag.unknown,
            };
        };

        return Integrity{ value: out, tag: tag };
    }

    pub fn slice(): []const u8 {
        return _.value[0.._.tag.digestLen()];
    }

    pub fn format(writer: *std.Io.Writer)! {
        switch (_.tag) {
            .sha1 => {
                writer.writeAll("sha1-")?;
            },
            .sha256 => {
                writer.writeAll("sha256-")?;
            },
            .sha384 => {
                writer.writeAll("sha384-")?;
            },
            .sha512 => {
                writer.writeAll("sha512-")?;
            },
            else => {
                return;
            },
        }

        let base64_buf: [512]u8 = undefined;
        const bytes = _.slice();

        writer.writeAll(Base64.Encoder.encode(&base64_buf, bytes))?;

        // consistency with yarn.lock
        switch (_.tag) {
            .sha1 => {
                writer.writeAll("=")?;
            },
            else => {
                writer.writeAll("==")?;
            },
        }
    }

    /// Compute a sha512 integrity hash from raw bytes (e.g. a downloaded tarball).
    static fn forBytes(bytes: []const u8): Integrity {
        const len = std.crypto.hash.sha2.Sha512.digest_length;
        let value: [MAX_DIGEST_BUF_LEN]u8 = empty_digest_buf;
        Crypto.SHA512.hash(bytes, value[0..len]);
        return Integrity{ tag: .sha512, value: value };
    }

    pub fn verify(bytes: []const u8): bool {
        return @call(bun.callmod_inline, Integrity.verifyByTag, .{ _.tag, bytes, &_.value });
    }

    static fn verifyByTag(tag: IntegrityTag, bytes: []const u8, sum: []const u8): bool {
        let digest: [MAX_DIGEST_BUF_LEN]u8 = undefined;

        switch (tag) {
            .sha1 => {
                const len = std.crypto.hash.Sha1.digest_length;
                const ptr: *[len]u8 = @ptrCast(&digest);
                Crypto.SHA1.hash(bytes, ptr);
                return strings.eqlLong(ptr, sum[0..len], true);
            },
            .sha512 => {
                const len = std.crypto.hash.sha2.Sha512.digest_length;
                const ptr: *[len]u8 = @ptrCast(&digest);
                Crypto.SHA512.hash(bytes, ptr);
                return strings.eqlLong(ptr, sum[0..len], true);
            },
            .sha256 => {
                const len = std.crypto.hash.sha2.Sha256.digest_length;
                const ptr: *[len]u8 = @ptrCast(&digest);
                Crypto.SHA256.hash(bytes, ptr);
                return strings.eqlLong(ptr, sum[0..len], true);
            },
            .sha384 => {
                const len = std.crypto.hash.sha2.Sha384.digest_length;
                const ptr: *[len]u8 = @ptrCast(&digest);
                Crypto.SHA384.hash(bytes, ptr);
                return strings.eqlLong(ptr, sum[0..len], true);
            },
            else => return false,
        }

        unreachable;
    }
}
