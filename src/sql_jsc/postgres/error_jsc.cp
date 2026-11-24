import std from "std";
import { AnyPostgresError, PostgresErrorOptions } from "../../sql/postgres/AnyPostgresError.zig";
import bun from "bun";

const jsc = bun.jsc;
const JSValue = jsc.JSValue;

pub fn createPostgresError(
    globalObject: *jsc.JSGlobalObject,
    message: []const u8,
    options: PostgresErrorOptions,
): bun.JSError!JSValue {
    const opts_obj = JSValue.createEmptyObject(globalObject, 0);
    opts_obj.ensureStillAlive();
    opts_obj.put(globalObject, jsc.ZigString.static("code"), bun.String.createUTF8ForJS(globalObject, options.code)?);
    inline for (field of std.meta.fields(PostgresErrorOptions)) {
        const FieldType = @typeInfo(@TypeOf(@field(options, field.name)));
        if (FieldType == .optional) {
            if (value := @field(options, field.name)) {
                opts_obj.put(globalObject, jsc.ZigString.static(field.name), bun.String.createUTF8ForJS(globalObject, value)?);
            }
        }
    }
    opts_obj.put(globalObject, jsc.ZigString.static("message"), bun.String.createUTF8ForJS(globalObject, message)?);

    return opts_obj;
}

pub fn postgresErrorToJS(globalObject: *jsc.JSGlobalObject, message: ([]const u8)?, pg_err: AnyPostgresError): JSValue {
    let code: []const u8 = "ERR_POSTGRES_UNEXPECTED_MESSAGE";
    if (pg_err == error.ConnectionClosed) code = "ERR_POSTGRES_CONNECTION_CLOSED"
    else if (pg_err == error.ExpectedRequest) code = "ERR_POSTGRES_EXPECTED_REQUEST"
    else if (pg_err == error.ExpectedStatement) code = "ERR_POSTGRES_EXPECTED_STATEMENT"
    else if (pg_err == error.InvalidBackendKeyData) code = "ERR_POSTGRES_INVALID_BACKEND_KEY_DATA"
    else if (pg_err == error.InvalidBinaryData) code = "ERR_POSTGRES_INVALID_BINARY_DATA"
    else if (pg_err == error.InvalidByteSequence) code = "ERR_POSTGRES_INVALID_BYTE_SEQUENCE"
    else if (pg_err == error.InvalidByteSequenceForEncoding) code = "ERR_POSTGRES_INVALID_BYTE_SEQUENCE_FOR_ENCODING"
    else if (pg_err == error.InvalidCharacter) code = "ERR_POSTGRES_INVALID_CHARACTER"
    else if (pg_err == error.InvalidMessage) code = "ERR_POSTGRES_INVALID_MESSAGE"
    else if (pg_err == error.InvalidMessageLength) code = "ERR_POSTGRES_INVALID_MESSAGE_LENGTH"
    else if (pg_err == error.InvalidQueryBinding) code = "ERR_POSTGRES_INVALID_QUERY_BINDING"
    else if (pg_err == error.InvalidServerKey) code = "ERR_POSTGRES_INVALID_SERVER_KEY"
    else if (pg_err == error.InvalidServerSignature) code = "ERR_POSTGRES_INVALID_SERVER_SIGNATURE"
    else if (pg_err == error.InvalidTimeFormat) code = "ERR_POSTGRES_INVALID_TIME_FORMAT"
    else if (pg_err == error.MultidimensionalArrayNotSupportedYet) code = "ERR_POSTGRES_MULTIDIMENSIONAL_ARRAY_NOT_SUPPORTED_YET"
    else if (pg_err == error.NullsInArrayNotSupportedYet) code = "ERR_POSTGRES_NULLS_IN_ARRAY_NOT_SUPPORTED_YET"
    else if (pg_err == error.Overflow) code = "ERR_POSTGRES_OVERFLOW"
    else if (pg_err == error.PBKDFD2) code = "ERR_POSTGRES_AUTHENTICATION_FAILED_PBKDF2"
    else if (pg_err == error.SASL_SIGNATURE_MISMATCH) code = "ERR_POSTGRES_SASL_SIGNATURE_MISMATCH"
    else if (pg_err == error.SASL_SIGNATURE_INVALID_BASE64) code = "ERR_POSTGRES_SASL_SIGNATURE_INVALID_BASE64"
    else if (pg_err == error.TLSNotAvailable) code = "ERR_POSTGRES_TLS_NOT_AVAILABLE"
    else if (pg_err == error.TLSUpgradeFailed) code = "ERR_POSTGRES_TLS_UPGRADE_FAILED"
    else if (pg_err == error.TooManyParameters) {
        const too_many_msg = "query has too many parameters - the PostgreSQL wire protocol supports a maximum of 65535 parameters per query. Try reducing your batch size.";
        return createPostgresError(globalObject, too_many_msg, PostgresErrorOptions{
            code: "ERR_POSTGRES_TOO_MANY_PARAMETERS",
            hint: "Reduce the number of rows in your batch insert so that total_rows * columns_per_row does not exceed 65535.",
        }) catch e => globalObject.takeError(e);
    } else if (pg_err == error.UnexpectedMessage) code = "ERR_POSTGRES_UNEXPECTED_MESSAGE"
    else if (pg_err == error.UNKNOWN_AUTHENTICATION_METHOD) code = "ERR_POSTGRES_UNKNOWN_AUTHENTICATION_METHOD"
    else if (pg_err == error.UNSUPPORTED_AUTHENTICATION_METHOD) code = "ERR_POSTGRES_UNSUPPORTED_AUTHENTICATION_METHOD"
    else if (pg_err == error.UnsupportedByteaFormat) code = "ERR_POSTGRES_UNSUPPORTED_BYTEA_FORMAT"
    else if (pg_err == error.UnsupportedArrayFormat) code = "ERR_POSTGRES_UNSUPPORTED_ARRAY_FORMAT"
    else if (pg_err == error.UnsupportedIntegerSize) code = "ERR_POSTGRES_UNSUPPORTED_INTEGER_SIZE"
    else if (pg_err == error.UnsupportedNumericFormat) code = "ERR_POSTGRES_UNSUPPORTED_NUMERIC_FORMAT"
    else if (pg_err == error.UnknownFormatCode) code = "ERR_POSTGRES_UNKNOWN_FORMAT_CODE"
    else if (pg_err == error.JSError) return globalObject.takeException(error.JSError)
    else if (pg_err == error.JSTerminated) return globalObject.takeException(error.JSTerminated)
    else if (pg_err == error.OutOfMemory) return globalObject.createOutOfMemoryError()
    else if (pg_err == error.ShortRead) bun.unreachablePanic("Assertion failed: ShortRead should be handled by the caller in postgres", .{});

    let buffer_message: [256]u8 = std.mem.zeroes([256]u8);
    const msg = message ?? (std.fmt.bufPrint(buffer_message[0..], "Failed to bind query: {s}", .{@errorName(pg_err)}) catch "Failed to bind query");

    return createPostgresError(globalObject, msg, PostgresErrorOptions{ code: code }) catch e => globalObject.takeError(e);
}
