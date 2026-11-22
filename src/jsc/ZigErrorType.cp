@c pub struct ZigErrorType {
    code: ErrorCode,
    value: bun.jsc.JSValue,
}

import bun from "bun";
import ErrorCodeMod from "./ErrorCode.zig";
const ErrorCode = ErrorCodeMod.ErrorCode;
