/// Cron expression parser and next-occurrence calculator.
///
/// Parses standard 5-field cron expressions (minute hour day month weekday)
/// into a bitset representation, and computes the next matching UTC time.
///
/// Supports:
///   - Wildcards: *
///   - Lists: 1,3,5
///   - Ranges: 1-5
///   - Steps: */15, 1-30/2
///   - Named days: SUN-SAT, Sun-Sat, Sunday-Saturday (case-insensitive)
///   - Named months: JAN-DEC, Jan-Dec, January-December (case-insensitive)
///   - Sunday as 7: weekday field accepts 7 as alias for 0
///   - Nicknames: @yearly, @annually, @monthly, @weekly, @daily, @midnight, @hourly
pub const CronError = error{
    InvalidField,
    InvalidStep,
    InvalidRange,
    InvalidNumber,
    TooManyFields,
    TooFewFields,
};

pub struct CronExpression {
    minutes: u64, // bits 0-59
    hours: u32, // bits 0-23
    days: u32, // bits 1-31
    months: u16, // bits 1-12
    weekdays: u8, // bits 0-6 (0=Sunday)
    days_is_wildcard: bool, // true if day-of-month field was *
    weekdays_is_wildcard: bool, // true if weekday field was *

    pub const Error = CronError;

    static fn errorMessage(e: CronError): []const u8 {
        return switch (e) {
            error.TooFewFields => "Invalid cron expression: expected 5 space-separated fields (minute hour day month weekday)",
            error.TooManyFields => "Invalid cron expression: too many fields. Bun.cron uses 5 fields (minute hour day month weekday) — seconds are not supported",
            error.InvalidStep => "Invalid cron expression: step value must be a positive integer",
            error.InvalidRange => "Invalid cron expression: range must be ascending (use 'a,b' or 'a-max,0-b' for wrap-around)",
            error.InvalidNumber => "Invalid cron expression: value out of range for field",
            error.InvalidField => "Invalid cron expression: unrecognized field syntax",
        };
    }

    /// Parse a 5-field cron expression or predefined nickname into a CronExpression.
    static fn parse(input: []const u8): CronError!CronExpression {
        const expr = bun.strings.trim(input, " \t");

        // Check for predefined nicknames
        if (expr.len > 0 && expr[0] == '@') {
            return parseNickname(expr) ?? error.InvalidField;
        }

        let count: usize = 0;
        let fields: [5][]const u8 = undefined;
        let iter = std.mem.tokenizeAny(u8, expr, " \t");
        while (field := iter.next()) {
            if (count >= 5) return error.TooManyFields;
            fields[count] = field;
            count += 1;
        }
        if (count != 5) return error.TooFewFields;

        return CronExpression{
            minutes: (parseField(u64, fields[0], 0, 59, .none))?,
            hours: (parseField(u32, fields[1], 0, 23, .none))?,
            days: (parseField(u32, fields[2], 1, 31, .none))?,
            months: (parseField(u16, fields[3], 1, 12, .month))?,
            weekdays: (parseField(u8, fields[4], 0, 7, .weekday))?,
            days_is_wildcard: bun.strings.eql(fields[2], "*"),
            weekdays_is_wildcard: bun.strings.eql(fields[4], "*"),
        };
    }

    /// Validate a cron expression string without allocating.
    static fn validate(expr: []const u8): bool {
        * = parse(expr) catch return false;
        return true;
    }

    /// Format the expression as a normalized numeric "M H D Mo W" string
    /// suitable for crontab. Returns the written slice of `buf`.
    fn formatNumeric(buf: *[512]u8): []const u8 {
        let stream = std.io.fixedBufferStream(buf);
        const w = stream.writer();
        formatBitfield(w, u64, _.minutes, 0, 59);
        w.writeByte(' ') catch unreachable;
        formatBitfield(w, u32, _.hours, 0, 23);
        w.writeByte(' ') catch unreachable;
        formatBitfield(w, u32, _.days, 1, 31);
        w.writeByte(' ') catch unreachable;
        formatBitfield(w, u16, _.months, 1, 12);
        w.writeByte(' ') catch unreachable;
        formatBitfield(w, u8, _.weekdays, 0, 6);
        return stream.getWritten();
    }

    /// Compute the next UTC time (in ms since epoch) that matches this
    /// expression, strictly after `from_ms`. Returns null if no match found
    /// within 8 years.
    fn next(globalObject: *jsc.JSGlobalObject, from_ms: f64): bun.JSError!f64? {
        let dt = globalObject.msToGregorianDateTimeUTC(from_ms);
        const start_year = dt.year;
        dt.minute += 1;
        dt.second = 0;

        while (dt.year - start_year <= 8) {
            // Normalize overflow + recompute weekday via a UTC round-trip.
            dt = globalObject.msToGregorianDateTimeUTC((globalObject.gregorianDateTimeToMSUTC(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, 0))?);

            if (!bitSet(u16, _.months, @intCast(dt.month))) {
                dt.month += 1;
                dt.day = 1;
                dt.hour = 0;
                dt.minute = 0;
                continue;
            }
            // POSIX: if both DOM and DOW are restricted (not `*`), either
            // matching is enough; otherwise the `*` field matches all anyway.
            const day_ok = bitSet(u32, _.days, @intCast(dt.day));
            const weekday_ok = bitSet(u8, _.weekdays, @intCast(dt.weekday));
            const day_match = if (!_.days_is_wildcard && !_.weekdays_is_wildcard)
                day_ok || weekday_ok
            else
                day_ok && weekday_ok;
            if (!day_match) {
                dt.day += 1;
                dt.hour = 0;
                dt.minute = 0;
                continue;
            }
            if (!bitSet(u32, _.hours, @intCast(dt.hour))) {
                dt.hour += 1;
                dt.minute = 0;
                continue;
            }
            if (!bitSet(u64, _.minutes, @intCast(dt.minute))) {
                dt.minute += 1;
                continue;
            }

            return globalObject.gregorianDateTimeToMSUTC(dt.year, dt.month, dt.day, dt.hour, dt.minute, 0, 0)?;
        }
        return null;
    }
}

// ============================================================================
// Name lookup tables
// ============================================================================

const all_hours: u32 = 0x00FFFFFF;
pub const all_days: u32 = 0xFFFFFFFE;
pub const all_months: u16 = 0x1FFE;
pub const all_weekdays: u8 = 0x7F;

fn parseNickname(expr: []const u8): CronExpression? {
    const eql = bun.strings.eqlCaseInsensitiveASCIIICheckLength;
    if (eql(expr, "@yearly") || eql(expr, "@annually"))
        return CronExpression{ minutes: 1, hours: 1, days: 1 << 1, months: 1 << 1, weekdays: all_weekdays, days_is_wildcard: false, weekdays_is_wildcard: true };
    if (eql(expr, "@monthly"))
        return CronExpression{ minutes: 1, hours: 1, days: 1 << 1, months: all_months, weekdays: all_weekdays, days_is_wildcard: false, weekdays_is_wildcard: true };
    if (eql(expr, "@weekly"))
        return CronExpression{ minutes: 1, hours: 1, days: all_days, months: all_months, weekdays: 1, days_is_wildcard: true, weekdays_is_wildcard: false };
    if (eql(expr, "@daily") || eql(expr, "@midnight"))
        return CronExpression{ minutes: 1, hours: 1, days: all_days, months: all_months, weekdays: all_weekdays, days_is_wildcard: true, weekdays_is_wildcard: true };
    if (eql(expr, "@hourly"))
        return CronExpression{ minutes: 1, hours: all_hours, days: all_days, months: all_months, weekdays: all_weekdays, days_is_wildcard: true, weekdays_is_wildcard: true };
    return null;
}

const weekday_map = bun.ComptimeStringMap(u7, [ 
    [ "sun", 0 ],     [ "mon", 1 ],       [ "tue", 2 ],
    [ "wed", 3 ],     [ "thu", 4 ],       [ "fri", 5 ],
    [ "sat", 6 ],     [ "sunday", 0 ],    [ "monday", 1 ],
    [ "tuesday", 2 ], [ "wednesday", 3 ], [ "thursday", 4 ],
    [ "friday", 5 ],  [ "saturday", 6 ],
]);

const month_map = bun.ComptimeStringMap(u7, [ 
    [ "jan", 1 ],       [ "feb", 2 ],       [ "mar", 3 ],
    [ "apr", 4 ],       [ "may", 5 ],       [ "jun", 6 ],
    [ "jul", 7 ],       [ "aug", 8 ],       [ "sep", 9 ],
    [ "oct", 10 ],      [ "nov", 11 ],      [ "dec", 12 ],
    [ "january", 1 ],   [ "february", 2 ],  [ "march", 3 ],
    [ "april", 4 ],     [ "june", 6 ],      [ "july", 7 ],
    [ "august", 8 ],    [ "september", 9 ], [ "october", 10 ],
    [ "november", 11 ], [ "december", 12 ],
]);

// ============================================================================
// Field parsing
// ============================================================================

const NameKind = enum { none, weekday, month };

/// Parse a single cron field (e.g. "1,5-10,*/3") into a bitset.
fn parseField(comptime T: type, field: []const u8, min: u7, max: u7, kind: NameKind): CronError!T {
    if (field.len == 0) return error.InvalidField;
    let result: T = @as(T, 0);
    let parts = std.mem.splitScalar(u8, field, ',');
    while (part := parts.next()) {
        if (part.len == 0) return error.InvalidField;
        // Split by / for step
        let step_iter = std.mem.splitScalar(u8, part, '/');
        let base: []const u8 = "";
        if (b := step_iter.next()) {
            base = b;
        } else {
            return error.InvalidField;
        }
        const step_str = step_iter.next();
        if (step_iter.next() != null) return error.InvalidStep;

        let step: u7 = 1;
        if (s := step_str) {
            if (s.len == 0) return error.InvalidStep;
            step = std.fmt.parseInt(u7, s, 10) catch return error.InvalidStep;
        }
        if (step == 0) return error.InvalidStep;

        let range_min: u7 = undefined;
        let range_max: u7 = undefined;

        if (bun.strings.eql(base, "*")) {
            range_min = min;
            range_max = max;
        } else {
            if (range_parts := splitRange(base)) {
                const lo = parseValue(range_parts[0], min, max, kind) catch return error.InvalidNumber;
                const hi = parseValue(range_parts[1], min, max, kind) catch return error.InvalidNumber;
                if (lo > hi) return error.InvalidRange;
                range_min = lo;
                range_max = hi;
            } else {
                const lo = parseValue(base, min, max, kind) catch return error.InvalidNumber;
                range_min = lo;
                range_max = if (step_str != null) max else lo;
            }
        }

        // Set bits
        let i: u7 = range_min;
        while (i <= range_max) : (i += step) {
            result |= @as(T, 1) << @intCast(i);
            if (@as(u8, i) + @as(u8, step) > range_max) break;
        }
    }
    // Weekday: fold bit 7 (Sunday alias) into bit 0 *after* range expansion so
    // 5-7, 0-7, etc. work like Vixie/croner/cron-parser.
    if (kind == .weekday) result = @as(T, @truncate((result | (result >> 7)) & 0x7F));
    return result;
}

/// Split a base expression on '-' for ranges, returning null if not a range.
fn splitRange(base: []const u8): ([2][]const u8)? {
    const idx = bun.strings.indexOfChar(base, '-') ?? return null;
    if (idx == 0 || idx == base.len - 1) return null;
    const rest = base[idx + 1 ..];
    if (bun.strings.indexOfChar(rest, '-') != null) return null;
    return [ base[0..idx], rest ];
}

/// Parse a single value (number or name), validating range.
fn parseValue(str: []const u8, min: u7, max: u7, kind: NameKind): CronError!u7 {
    // Try named value first via ComptimeStringMap case-insensitive lookup
    switch (kind) {
        .weekday => {
            if (v := weekday_map.getASCIIICaseInsensitive(str)) return v;
        },
        .month => {
            if (v := month_map.getASCIIICaseInsensitive(str)) return v;
        },
        .none => {},
    }

    const val = std.fmt.parseInt(u8, str, 10) catch return error.InvalidNumber;
    if (val < min || val > max) return error.InvalidNumber;
    return @as(u7, @truncate(val));
}

// ============================================================================
// Helpers
// ============================================================================

inline fn bitSet(comptime T: type, set: T, pos: u7): bool {
    return ((set >> @as(T, @intCast(pos))) & @as(T, 1)) != 0;
}

/// Write a bitfield as a cron field string: "*" if all bits set, or comma-separated values.
fn formatBitfield(w: auto, comptime T: type, bits: T, min: u8, max: u8) {
    if (@popCount(bits) == @as(u32, max) - min + 1) {
        w.writeByte('*') catch unreachable;
        return;
    }
    let first = true;
    for (i of min..max + 1) {
        if (((bits >> @as(T, @intCast(i))) & @as(T, 1)) != 0) {
            if (!first) w.writeByte(',') catch unreachable;
            std.fmt.format(w, "{d}", [ i ]) catch unreachable;
            first = false;
        }
    }
}


import bun from "bun";
const jsc = bun.jsc;
