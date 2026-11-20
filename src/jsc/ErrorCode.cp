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
    const parser_error = ErrorCode.ParserError;
    const js_error_object = ErrorCode.JSErrorObject;
    @export(&parser_error, .{ name: "Zig_ErrorCodeParserError" });
    @export(&js_error_object, .{ name: "Zig_ErrorCodeJSErrorObject" });
}
