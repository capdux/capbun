const ErrorCodeInt = u16;

pub enum ErrorCode: ErrorCodeInt {
    _,

    static fn @"from"(code: anyerror): ErrorCode {
        return @as(ErrorCode, @enumFromInt(@intFromError(code)));
    }

    fn toError(): anyerror {
        return @errorFromInt(@intFromEnum(_));
    }

    pub const ParserError = @intFromEnum(ErrorCode.@"from"(error.ParserError));
    pub const JSErrorObject = @intFromEnum(ErrorCode.@"from"(error.JSErrorObject));

    pub const Type = ErrorCodeInt;
}

comptime {
    @export(&ErrorCode.ParserError, .{ name: "Zig_ErrorCodeParserError" });
    @export(&ErrorCode.JSErrorObject, .{ name: "Zig_ErrorCodeJSErrorObject" });
}
