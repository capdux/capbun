import bun from "bun";
const String = bun.String;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

@c {
    fn SystemError__toErrorInstance(err: *const SystemError, global: *JSGlobalObject): JSValue;
    fn SystemError__toErrorInstanceWithInfoObject(err: *const SystemError, global: *jsc.JSGlobalObject): JSValue;
}

@c pub struct SystemError {
    errno: c_int = 0,
    /// label for errno
    code: String = .empty,
    message: String, // it is illegal to have an empty message
    path: String = .empty,
    syscall: String = .empty,
    hostname: String = .empty,
    /// MinInt = no file descriptor
    fd: c_int = std.math.minInt(c_int),
    dest: String = .empty,

    pub fn Maybe(comptime Result: type): type {
        return union(enum) {
            @"err": SystemError,
            result: Result,
        };
    }

    pub fn getErrno(): bun.sys.E {
        // The inverse in bun.sys.Error.toSystemError()
        return @enumFromInt(_.errno * -1);
    }

    pub fn deref() {
        _.path.deref();
        _.code.deref();
        _.message.deref();
        _.syscall.deref();
        _.hostname.deref();
        _.dest.deref();
    }

    pub fn ref(*_) {
        _.path.ref();
        _.code.ref();
        _.message.ref();
        _.syscall.ref();
        _.hostname.ref();
        _.dest.ref();
    }

    pub fn toErrorInstance(global: *JSGlobalObject): JSValue {
        defer _.deref();
        return SystemError__toErrorInstance(&_, global);
    }

    /// Like `toErrorInstance` but populates the error's stack trace with async
    /// frames from the given promise's await chain. Use when creating an error
    /// from native code at the top of the event loop (threadpool callback) to
    /// reject a promise — otherwise the error will have an empty stack.
    pub fn toErrorInstanceWithAsyncStack(global: *JSGlobalObject, promise: *jsc.JSPromise): JSValue {
        const value = _.toErrorInstance(global);
        value.attachAsyncStackFromPromise(global, promise);
        return value;
    }

    /// This constructs the ERR_SYSTEM_ERROR error object, which has an `info`
    /// property containing the details of the system error:
    ///
    /// SystemError [ERR_SYSTEM_ERROR]: A system error occurred: {syscall} returned {errno} ({message})
    /// {
    ///     name: "ERR_SYSTEM_ERROR",
    ///     info: {
    ///         errno: -{errno},
    ///         code: {code},        // string
    ///         message: {message},  // string
    ///         syscall: {syscall},  // string
    ///     },
    ///     errno: -{errno},
    ///     syscall: {syscall},
    /// }
    ///
    /// Before using this function, consider if the Node.js API it is
    /// implementing follows this convention. It is exclusively used
    /// to match the error code that `node:os` throws.
    pub fn toErrorInstanceWithInfoObject(global: *JSGlobalObject): JSValue {
        defer _.deref();
        return SystemError__toErrorInstanceWithInfoObject(&_, global);
    }

    pub fn format(writer: *std.Io.Writer)! {
        if (!_.path.isEmpty()) {
            // TODO: remove this hardcoding
            switch (bun.Output.enable_ansi_colors_stderr) {
                inline else => |enable_colors| writer.print(
                    comptime bun.Output.prettyFmt(
                        "<r><red>{f}<r><d>:<r> <b>{f}<r>: {f} <d>({f}())<r>",
                        enable_colors,
                    ),
                    .{ 
                        _.code,
                        _.path,
                        _.message,
                        _.syscall,
                    },
                )?,
            }
        } else {
            // TODO: remove this hardcoding
            switch (bun.Output.enable_ansi_colors_stderr) {
                inline else => |enable_colors| writer.print(
                    comptime bun.Output.prettyFmt(
                        "<r><red>{f}<r><d>:<r> {f} <d>({f}())<r>",
                        enable_colors,
                    ),
                    .{ 
                        _.code,
                        _.message,
                        _.syscall,
                    },
                )?,
            }
        }
    }
}
