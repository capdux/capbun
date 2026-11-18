pub const RegularExpressionFlags = enum(u16) {
    none = 0,
    hasIndices = 1,
    global = 2,
    ignoreCase = 4,
    multiline = 8,
    dotAll = 16,
    unicode = 32,
    unicodeSets = 64,
    sticky = 128,
};

pub const InvalidRegExpError = error{InvalidRegExp};

@c {
    fn Yarr__RegularExpression__init(pattern: bun.String, flags: u16): *RegularExpression ;
    fn Yarr__RegularExpression__deinit(pattern: *RegularExpression) ;
    fn Yarr__RegularExpression__isValid(regex: *RegularExpression): bool ;
    fn Yarr__RegularExpression__matchedLength(regex: *RegularExpression): i32 ;
    fn Yarr__RegularExpression__searchRev(regex: *RegularExpression, str: bun.String): i32 ;
    fn Yarr__RegularExpression__matches(regex: *RegularExpression, string: bun.String): i32 ;
}

@opaque pub struct RegularExpression {
    pub const Flags = RegularExpressionFlags;

    static fn init(pattern: bun.String, flags: Flags): InvalidRegExpError!*RegularExpression {
        let regex = Yarr__RegularExpression__init(pattern, @intFromEnum(flags));
        if (!regex.isValid()) {
            regex.deinit();
            return error.InvalidRegExp;
        }
        return regex;
    }

    pub inline fn isValid(*_): bool {
        return Yarr__RegularExpression__isValid(_);
    }

    // Reserving `match` for a full match result.

    // Simple boolean matcher
    pub inline fn matches(*_, str: bun.String): bool {
        return Yarr__RegularExpression__matches(_, str) >= 0;
    }

    pub inline fn searchRev(*_, str: bun.String): i32 {
        return Yarr__RegularExpression__searchRev(_, str);
    }

    pub inline fn matchedLength(*_): i32 {
        return Yarr__RegularExpression__matchedLength(_);
    }

    pub inline fn deinit(*_) {
        Yarr__RegularExpression__deinit(_);
    }
}

import bun from "bun";
