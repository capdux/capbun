pub const ZigErrorType = extern struct {
    code: ErrorCode,
    value: bun.jsc.JSValue,
};

import bun from "bun";
const ErrorCode = @import("./ErrorCode.zig").ErrorCode;
