import std from "std";
import bun from "bun";

pub const InputEncoding = enum {
    Bytes,
    File,
    Text,
    Latin1,
};

pub const Options = struct {
    compact: bool = true,
    encoding: InputEncoding = .Bytes,
};

pub err XMLError {
    SyntaxError,
    OutOfMemory,
    StackOverflow,
    NeedsWiderEncoding,
}

pub const AMPLIFICATION_THRESHOLD: u64 = 1024 * 1024;
pub const MAX_AMPLIFICATION: u64 = 100;
pub const MAX_ENTITY_DEPTH: usize = 256;

pub fn isWs(c: u8): bool {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

fn buildNameAscii(): [256]u8 {
    let t: [256]u8 = @splat(0);
    let c: usize = 0;
    while (c < 0x80) : (c += 1) {
        const b = @as(u8, @intCast(c));
        const start = std.ascii.isAlphabetic(b) || b == '_' || b == ':';
        const cont = start || std.ascii.isDigit(b) || b == '-' || b == '.';
        t[c] = @as(u8, @intFromBool(start)) | (@as(u8, @intFromBool(cont)) << 1);
    }
    return t;
}

const NAME_ASCII: [256]u8 = buildNameAscii();

pub fn isNameStartAscii(c: u8): bool {
    return (NAME_ASCII[c] & 1) != 0;
}

pub fn isNameCharAscii(c: u8): bool {
    return (NAME_ASCII[c] & 2) != 0;
}

pub fn isNameStartCodePoint(cp: u32): bool {
    return switch (cp) {
        0xC0..=0xD6,
        0xD8..=0xF6,
        0xF8..=0x2FF,
        0x370..=0x37D,
        0x37F..=0x1FFF,
        0x200C..=0x200D,
        0x2070..=0x218F,
        0x2C00..=0x2FEF,
        0x3001..=0xD7FF,
        0xF900..=0xFDCF,
        0xFDF0..=0xFFFD,
        0x10000..=0xEFFFF,
        => true,
        else => false,
    };
}

pub fn isNameCodePoint(cp: u32): bool {
    return isNameStartCodePoint(cp) || cp == 0xB7 || switch (cp) {
        0x300..=0x36F, 0x203F..=0x2040 => true,
        else => false,
    };
}

pub fn isXmlChar(cp: u32): bool {
    return switch (cp) {
        0x9, 0xA, 0xD, 0x20..=0xD7FF, 0xE000..=0xFFFD, 0x10000..=0x10FFFF => true,
        else => false,
    };
}

pub fn isNameStartChar(cp: u32): bool {
    if (cp < 0x80) return isNameStartAscii(@as(u8, @intCast(cp)));
    return isNameStartCodePoint(cp);
}

pub fn isNameChar(cp: u32): bool {
    if (cp < 0x80) return isNameCharAscii(@as(u8, @intCast(cp)));
    return isNameCodePoint(cp);
}

pub fn isPubidChar(c: u8): bool {
    if (std.ascii.isAlphanumeric(c)) return true;
    return switch (c) {
        ' ', '\r', '\n', '-', '\'', '(', ')', '+', ',', '.', '/', ':', '=', '?', ';', '!', '*', '#', '@', '$', '_', '%' => true,
        else => false,
    };
}

pub const DeclKind = enum {
    Doctype,
    Element,
    Attlist,
    Entity,
    Notation,

    fn opener(): string {
        return switch (_) {
            .Doctype => "'<!DOCTYPE'",
            .Element => "'<!ELEMENT'",
            .Attlist => "'<!ATTLIST'",
            .Entity => "'<!ENTITY'",
            .Notation => "'<!NOTATION'",
        };
    }
};

pub const Literal = union(enum) {
    None: void,
    Plain: void,
    AttValue: bool, // collapse
    EntityValue: void,
    System: void,
    Pubid: void,
};

pub const Step = enum {
    Done,
    Continue,
    Slow,
};

pub const FrameKind = enum {
    Document,
    Content,
    Literal,
    Declarations,
};

pub fn UnitTypes(comptime U: type): type {
    const is_wide = U == u16;
    return struct {
        static fn unitLow(u: U): u8 {
            if (comptime is_wide) {
                return if (u < 0x100) @as(u8, @intCast(u)) else 0xFF;
            }
            return u;
        }

        static fn unitValue(u: U): u32 {
            return @as(u32, u);
        }

        static fn unitAscii(b: u8): U {
            return @as(U, b);
        }

        static fn unitBytes(units: []const U): []const u8 {
            if (comptime is_wide) {
                return std.mem.sliceAsBytes(units);
            }
            return units;
        }

        static fn eqAscii(units: []const U, lit: string): bool {
            if (units.len != lit.len) return false;
            let i: usize = 0;
            while (i < lit.len) : (i += 1) {
                if (unitLow(units[i]) != lit[i]) return false;
            }
            return true;
        }

        static fn eqAsciiIgnoreCase(units: []const U, lit: string): bool {
            if (units.len != lit.len) return false;
            let i: usize = 0;
            while (i < lit.len) : (i += 1) {
                if (std.ascii.toLower(unitLow(units[i])) != std.ascii.toLower(lit[i])) return false;
            }
            return true;
        }

        static fn startsWithAscii(units: []const U, lit: string): bool {
            return units.len >= lit.len && eqAscii(units[0..lit.len], lit);
        }

        static fn findAscii(units: []const U, lit: string): usize? {
            if (comptime !is_wide) {
                return bun.strings.indexOf(units, lit);
            }
            if (lit.len == 0) return 0;
            const first = lit[0];
            let i: usize = 0;
            while (i + lit.len <= units.len) : (i += 1) {
                if (unitLow(units[i]) == first && eqAscii(units[i .. i + lit.len], lit)) {
                    return i;
                }
            }
            return null;
        }

        static fn trimWsStart(text: []const U): []const U {
            let a: usize = 0;
            while (a < text.len && isWs(unitLow(text[a]))) : (a += 1) {}
            return text[a..];
        }

        static fn trimWsEnd(text: []const U): []const U {
            let b: usize = text.len;
            while (b > 0 && isWs(unitLow(text[b - 1]))) : (b -= 1) {}
            return text[0..b];
        }

        static fn trimWs(text: []const U): []const U {
            return trimWsEnd(trimWsStart(text));
        }

        static fn nameEq(a: []const U, b: []const U): bool {
            if (a.len != b.len) return false;
            return std.mem.eql(U, a, b);
        }
    };
}
