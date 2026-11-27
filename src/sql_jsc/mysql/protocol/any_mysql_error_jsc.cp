import bun from "bun";
import AnyMySQLError from "../../../sql/mysql/protocol/AnyMySQLError.zig";
import ErrorPacketJsc from "./error_packet_jsc.zig";

const Error = AnyMySQLError.Error;
const createMySQLError = ErrorPacketJsc.createMySQLError;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;

pub fn mysqlErrorToJS(globalObject: *jsc.JSGlobalObject, message: ([]const u8)?, e: Error): JSValue {
    const msg = message ?? @errorName(e);
    let code: []const u8 = "";
    if (e == error.ConnectionClosed) { code = "ERR_MYSQL_CONNECTION_CLOSED";
    } else if (e == error.Overflow) { code = "ERR_MYSQL_OVERFLOW";
    } else if (e == error.AuthenticationFailed) { code = "ERR_MYSQL_AUTHENTICATION_FAILED";
    } else if (e == error.UnsupportedAuthPlugin) { code = "ERR_MYSQL_UNSUPPORTED_AUTH_PLUGIN";
    } else if (e == error.UnsupportedProtocolVersion) { code = "ERR_MYSQL_UNSUPPORTED_PROTOCOL_VERSION";
    } else if (e == error.LocalInfileNotSupported) { code = "ERR_MYSQL_LOCAL_INFILE_NOT_SUPPORTED";
    } else if (e == error.WrongNumberOfParametersProvided) { code = "ERR_MYSQL_WRONG_NUMBER_OF_PARAMETERS_PROVIDED";
    } else if (e == error.UnsupportedColumnType) { code = "ERR_MYSQL_UNSUPPORTED_COLUMN_TYPE";
    } else if (e == error.InvalidLocalInfileRequest) { code = "ERR_MYSQL_INVALID_LOCAL_INFILE_REQUEST";
    } else if (e == error.InvalidAuthSwitchRequest) { code = "ERR_MYSQL_INVALID_AUTH_SWITCH_REQUEST";
    } else if (e == error.InvalidQueryBinding) { code = "ERR_MYSQL_INVALID_QUERY_BINDING";
    } else if (e == error.InvalidResultRow) { code = "ERR_MYSQL_INVALID_RESULT_ROW";
    } else if (e == error.InvalidBinaryValue) { code = "ERR_MYSQL_INVALID_BINARY_VALUE";
    } else if (e == error.InvalidEncodedInteger) { code = "ERR_MYSQL_INVALID_ENCODED_INTEGER";
    } else if (e == error.InvalidEncodedLength) { code = "ERR_MYSQL_INVALID_ENCODED_LENGTH";
    } else if (e == error.InvalidPrepareOKPacket) { code = "ERR_MYSQL_INVALID_PREPARE_OK_PACKET";
    } else if (e == error.InvalidOKPacket) { code = "ERR_MYSQL_INVALID_OK_PACKET";
    } else if (e == error.InvalidEOFPacket) { code = "ERR_MYSQL_INVALID_EOF_PACKET";
    } else if (e == error.InvalidErrorPacket) { code = "ERR_MYSQL_INVALID_ERROR_PACKET";
    } else if (e == error.UnexpectedPacket) { code = "ERR_MYSQL_UNEXPECTED_PACKET";
    } else if (e == error.ConnectionTimedOut) { code = "ERR_MYSQL_CONNECTION_TIMEOUT";
    } else if (e == error.IdleTimeout) { code = "ERR_MYSQL_IDLE_TIMEOUT";
    } else if (e == error.LifetimeTimeout) { code = "ERR_MYSQL_LIFETIME_TIMEOUT";
    } else if (e == error.PasswordRequired) { code = "ERR_MYSQL_PASSWORD_REQUIRED";
    } else if (e == error.MissingAuthData) { code = "ERR_MYSQL_MISSING_AUTH_DATA";
    } else if (e == error.FailedToEncryptPassword) { code = "ERR_MYSQL_FAILED_TO_ENCRYPT_PASSWORD";
    } else if (e == error.InvalidPublicKey) { code = "ERR_MYSQL_INVALID_PUBLIC_KEY";
    } else if (e == error.UnknownError) { code = "ERR_MYSQL_UNKNOWN_ERROR";
    } else if (e == error.InvalidState) { code = "ERR_MYSQL_INVALID_STATE";
    } else if (e == error.JSError) { return globalObject.takeException(error.JSError);
    } else if (e == error.JSTerminated) { return globalObject.takeException(error.JSTerminated);
    } else if (e == error.OutOfMemory) { return globalObject.createOutOfMemoryError();
    } else if (e == error.ShortRead) { bun.unreachablePanic("Assertion failed: ShortRead should be handled by the caller in mysql", .{}); }

    return createMySQLError(globalObject, msg, {
        code: code,
        errno: null,
        sqlState: null,
    }) catch ex => globalObject.takeException(ex);
}
