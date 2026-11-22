// S3 error JavaScript bridges in CP
const s3_error = @import("../../../s3_signing/error.zig");
const getSignErrorMessage = s3_error.getSignErrorMessage;

const bun = @import("bun");
const jsc = bun.jsc;

pub const S3Error = s3_error.S3Error;

pub fn getJSSignError(err: anyerror, globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    return switch (err) {
        error.MissingCredentials => globalThis.ERR(.S3_MISSING_CREDENTIALS, getSignErrorMessage(error.MissingCredentials), .{}).toJS(),
        error.InvalidMethod => globalThis.ERR(.S3_INVALID_METHOD, getSignErrorMessage(error.InvalidMethod), .{}).toJS(),
        error.InvalidPath => globalThis.ERR(.S3_INVALID_PATH, getSignErrorMessage(error.InvalidPath), .{}).toJS(),
        error.InvalidEndpoint => globalThis.ERR(.S3_INVALID_ENDPOINT, getSignErrorMessage(error.InvalidEndpoint), .{}).toJS(),
        error.InvalidSessionToken => globalThis.ERR(.S3_INVALID_SESSION_TOKEN, getSignErrorMessage(error.InvalidSessionToken), .{}).toJS(),
        else => globalThis.ERR(.S3_INVALID_SIGNATURE, getSignErrorMessage(error.SignError), .{}).toJS(),
    };
}

pub fn throwSignError(err: anyerror, globalThis: *jsc.JSGlobalObject): bun.JSError {
    return switch (err) {
        error.MissingCredentials => globalThis.ERR(.S3_MISSING_CREDENTIALS, getSignErrorMessage(error.MissingCredentials), .{}).throw(),
        error.InvalidMethod => globalThis.ERR(.S3_INVALID_METHOD, getSignErrorMessage(error.InvalidMethod), .{}).throw(),
        error.InvalidPath => globalThis.ERR(.S3_INVALID_PATH, getSignErrorMessage(error.InvalidPath), .{}).throw(),
        error.InvalidEndpoint => globalThis.ERR(.S3_INVALID_ENDPOINT, getSignErrorMessage(error.InvalidEndpoint), .{}).throw(),
        error.InvalidSessionToken => globalThis.ERR(.S3_INVALID_SESSION_TOKEN, getSignErrorMessage(error.InvalidSessionToken), .{}).throw(),
        else => globalThis.ERR(.S3_INVALID_SIGNATURE, getSignErrorMessage(error.SignError), .{}).throw(),
    };
}

struct JSS3ErrorGuard {
    err: *const JSS3Error!,
    ~init() { _.err.deinit(); }
}

@c pub struct JSS3Error {
    code: bun.String = bun.String.empty,
    message: bun.String = bun.String.empty,
    path: bun.String = bun.String.empty,

    static fn init(code: []const u8, message: []const u8, path: ([]const u8)?): JSS3Error {
        let path_str = bun.String.empty;
        if (p := path) { path_str = bun.String.init(p); }
        return JSS3Error{
            code: bun.String.createAtomIfPossible(code),
            message: bun.String.createAtomIfPossible(message),
            path: path_str,
        };
    }
    fn release(*_) {
        _.path.deref();
        _.code.deref();
        _.message.deref();
    }
    fn deinit() {
        _.path.deref();
        _.code.deref();
        _.message.deref();
    }
    fn toErrorInstance(global: *jsc.JSGlobalObject): jsc.JSValue {
        const _guard = JSS3ErrorGuard{ err: unsafe { &_ } };
        return S3Error__toErrorInstance(unsafe { &_ }, global);
    }
    extern fn S3Error__toErrorInstance(err_ptr: *const JSS3Error, global: *jsc.JSGlobalObject) callconv(jsc.conv): jsc.JSValue;
}

pub fn s3ErrorToJS(s3_err: *const S3Error, globalObject: *jsc.JSGlobalObject, path: ([]const u8)?): jsc.JSValue {
    const value = JSS3Error.init(s3_err.code, s3_err.message, path).toErrorInstance(globalObject);
    bun.assert(!globalObject.hasException());
    return value;
}

pub fn s3ErrorToJSWithAsyncStack(
    s3_err: *const S3Error,
    globalObject: *jsc.JSGlobalObject,
    path: ([]const u8)?,
    promise: *jsc.JSPromise,
): jsc.JSValue {
    const value = s3ErrorToJS(s3_err, globalObject, path);
    value.attachAsyncStackFromPromise(globalObject, promise);
    return value;
}
