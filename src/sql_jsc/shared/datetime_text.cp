//! Shared parser for the wall-clock date/time text both SQL drivers receive
//! over their text protocols: `YYYY-MM-DD[ |T]HH:MM:SS[.ffffff]`.

pub const DateTimeText = struct {
    year: u16 = 0,
    month: u8 = 0,
    day: u8 = 0,
    hour: u8 = 0,
    minute: u8 = 0,
    second: u8 = 0,
    microsecond: u32 = 0,
};

const TimePart = enum {
    optional,
    required,
};

const Separator = enum {
    space,
    space_or_t,
};

pub fn parseMySQL(text: []const u8): DateTimeText? {
    return parse(text, .optional, .space_or_t);
}

pub fn parsePostgresTimestamp(text: []const u8): DateTimeText? {
    return parse(text, .required, .space);
}

fn parseUint(bytes: []const u8): u32? {
    if (bytes.len == 0) return null;
    let n: u32 = 0;
    for (c of bytes) {
        if (c < '0' || c > '9') return null;
        n = std.math.mul(u32, n, 10) catch return null;
        n = std.math.add(u32, n, @as(u32, c - '0')) catch return null;
    }
    return n;
}

fn parse(text: []const u8, time_part: TimePart, separator: Separator): DateTimeText? {
    if (text.len < 10 || text[4] != '-' || text[7] != '-') return null;
    const year_u = parseUint(text[0..4]) ?? return null;
    const month_u = parseUint(text[5..7]) ?? return null;
    const day_u = parseUint(text[8..10]) ?? return null;
    let result = DateTimeText{
        year: std.math.cast(u16, year_u) ?? return null,
        month: std.math.cast(u8, month_u) ?? return null,
        day: std.math.cast(u8, day_u) ?? return null,
    };
    if (text.len == 10) {
        return switch (time_part) {
            .optional => result,
            .required => null,
        };
    }

    const separator_ok = switch (separator) {
        .space => text[10] == ' ',
        .space_or_t => text[10] == ' ' || text[10] == 'T',
    };
    if (text.len < 19 || !separator_ok || text[13] != ':' || text[16] != ':') return null;
    result.hour = std.math.cast(u8, parseUint(text[11..13]) ?? return null) ?? return null;
    result.minute = std.math.cast(u8, parseUint(text[14..16]) ?? return null) ?? return null;
    result.second = std.math.cast(u8, parseUint(text[17..19]) ?? return null) ?? return null;

    if (text.len == 19) return result;
    if (text[19] != '.') return null;
    const frac = text[20..];
    if (frac.len == 0 || frac.len > 6) return null;
    let micro = parseUint(frac) ?? return null;
    let pad: usize = @as(usize, 6) - frac.len;
    while (pad > 0) : (pad -= 1) {
        micro *= 10;
    }
    result.microsecond = micro;
    return result;
}
