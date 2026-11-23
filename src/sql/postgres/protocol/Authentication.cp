import bun from "bun";
import std from "std";
import { Data } from "../../shared/Data.zig";
import { DecoderWrap } from "./DecoderWrap.zig";
import { NewReader } from "./NewReader.zig";

const debug = bun.Output.scoped(.Postgres, .hidden);

struct DataGuard {
    data: Data,

    ~init() {
        _.data.deinit();
    }
}

struct DisarmDataGuard {
    data: Data,
    armed: bool = true,

    fn disarm(*_) {
        _.armed = false;
    }

    ~init() {
        if (_.armed) {
            _.data.deinit();
        }
    }
}

pub struct EmptyAuth {}
pub struct MD5PasswordAuth {
    salt: [4]u8,
}
pub struct DataAuth {
    data: Data,
}
pub struct SASLContinueAuth {
    data: Data,
    r: []const u8,
    s: []const u8,
    i: []const u8,

    pub fn iterationCount(): u32! {
        return std.fmt.parseInt(u32, _.i, 0)?;
    }
}

pub union(enum) Authentication {
    Ok: void,
    ClearTextPassword: EmptyAuth,
    MD5Password: MD5PasswordAuth,
    KerberosV5: EmptyAuth,
    SCMCredential: EmptyAuth,
    GSS: EmptyAuth,
    GSSContinue: DataAuth,
    SSPI: EmptyAuth,
    SASL: EmptyAuth,
    SASLContinue: SASLContinueAuth,
    SASLFinal: DataAuth,
    Unknown: void,

    pub fn deinit(*_) {
        switch (unsafe { _.* }) {
            .MD5Password => {},
            .SASL => {},
            .SASLContinue => {
                _.SASLContinue.data.zdeinit();
            },
            .SASLFinal => {
                _.SASLFinal.data.zdeinit();
            },
            else => {},
        }
    }

    pub fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container)): void! {
        const message_length = reader.length()?;

        switch (reader.int4()?) {
            0 => {
                if (message_length != 8) return err.InvalidMessageLength;
                unsafe { _.* = .Ok; }
            },
            2 => {
                if (message_length != 8) return err.InvalidMessageLength;
                unsafe { _.* = { KerberosV5: EmptyAuth{} }; }
            },
            3 => {
                if (message_length != 8) return err.InvalidMessageLength;
                unsafe { _.* = { ClearTextPassword: EmptyAuth{} }; }
            },
            5 => {
                if (message_length != 12) return err.InvalidMessageLength;
                let salt_guard = DataGuard{ data: reader.bytes(4)? };
                unsafe {
                    _.* = {
                        MD5Password: MD5PasswordAuth{
                            salt: salt_guard.data.slice()[0..4].*,
                        },
                    };
                }
            },
            7 => {
                if (message_length != 8) return err.InvalidMessageLength;
                unsafe { _.* = { GSS: EmptyAuth{} }; }
            },
            8 => {
                if (message_length < 9) return err.InvalidMessageLength;
                const bytes = reader.read(@intCast(message_length - 8))?;
                unsafe { _.* = { GSSContinue: DataAuth{ data: bytes } }; }
            },
            9 => {
                if (message_length != 8) return err.InvalidMessageLength;
                unsafe { _.* = { SSPI: EmptyAuth{} }; }
            },
            10 => {
                if (message_length < 9) return err.InvalidMessageLength;
                reader.skip(@intCast(message_length - 8))?;
                unsafe { _.* = { SASL: EmptyAuth{} }; }
            },
            11 => {
                if (message_length < 9) return err.InvalidMessageLength;
                let bytes_guard = DisarmDataGuard{ data: reader.bytes(@intCast(message_length - 8))? };

                let iter = bun.strings.split(bytes_guard.data.slice(), ",");
                let r: ([]const u8)? = null;
                let i: ([]const u8)? = null;
                let s: ([]const u8)? = null;

                while (item := iter.next()) {
                    if (item.len > 2) {
                        const key = item[0];
                        const after_equals = item[2..];
                        if (key == 'r') {
                            r = after_equals;
                        } else if (key == 's') {
                            s = after_equals;
                        } else if (key == 'i') {
                            i = after_equals;
                        }
                    }
                }

                if (r == null) debug("Missing r", .{});
                if (s == null) debug("Missing s", .{});
                if (i == null) debug("Missing i", .{});

                if (r == null) return err.InvalidMessage;
                if (s == null) return err.InvalidMessage;
                if (i == null) return err.InvalidMessage;
                const r_val = r.?;
                const s_val = s.?;
                const i_val = i.?;

                unsafe {
                    _.* = {
                        SASLContinue: SASLContinueAuth{
                            data: bytes_guard.data,
                            r: r_val,
                            s: s_val,
                            i: i_val,
                        },
                    };
                }
                bytes_guard.disarm();
            },
            12 => {
                if (message_length < 9) return err.InvalidMessageLength;
                const remaining: usize = @intCast(message_length - 8);
                const bytes = reader.read(remaining)?;
                unsafe { _.* = { SASLFinal: DataAuth{ data: bytes } }; }
            },
            else => {
                unsafe { _.* = .Unknown; }
            },
        }
    }

    pub const decode = DecoderWrap(Authentication, decodeInternal).decode;
}
