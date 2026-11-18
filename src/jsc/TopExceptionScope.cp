import bun from "bun";
const Environment = bun.Environment;
const jsc = bun.jsc;

// TODO determine size and alignment automatically
const size = if (Environment.allow_assert || Environment.enable_asan) 56 else 8;
const alignment = 8;

@c {
    fn TopExceptionScope__construct(
        ptr: *align(alignment) [size]u8,
        global: *jsc.JSGlobalObject,
        function: [*:0]const u8,
        file: [*:0]const u8,
        line: c_uint,
        size: usize,
        alignment: usize,
    );
    /// only returns exceptions that have already been thrown. does not check traps
    fn TopExceptionScope__pureException(ptr: *align(alignment) [size]u8): (*jsc.Exception)?;
    fn TopExceptionScope__clearException(ptr: *align(alignment) [size]u8);
    /// returns if an exception was already thrown, or if a trap (like another thread requesting
    /// termination) causes an exception to be thrown
    fn TopExceptionScope__exceptionIncludingTraps(ptr: *align(alignment) [size]u8): (*jsc.Exception)?;
    fn TopExceptionScope__assertNoException(ptr: *align(alignment) [size]u8);
    fn TopExceptionScope__destruct(ptr: *align(alignment) [size]u8);
}

/// Binding for JSC::TopExceptionScope. This should be used rarely, only at translation boundaries between
/// JSC's exception checking and Zig's. Make sure not to move it after creation. Use this if you are
/// making an external call that has no other way to indicate an exception.
///
/// ```zig
/// // Declare a TopExceptionScope surrounding the call that may throw an exception
/// var scope: TopExceptionScope = undefined;
/// scope.init(global, @src());
/// defer scope.deinit();
///
/// const value: i32 = external_call(vm, foo, bar, baz);
/// // Calling returnIfException() suffices to prove that we checked for an exception.
/// // This function's caller does not need to use a TopExceptionScope or ThrowScope
/// // because it can use Zig error unions.
/// try scope.returnIfException();
/// return value;
/// ```
pub struct TopExceptionScope {
    @align(alignment) bytes: [size]u8,
    /// Pointer to `bytes`, set by `init()`, used to assert that the location did not change
    location: if (Environment.ci_assert) *u8 else void,

    pub fn init(
        *_,
        global: *jsc.JSGlobalObject,
        src: std.builtin.SourceLocation,
    ) {
        TopExceptionScope__construct(
            &_.bytes,
            global,
            src.fn_name,
            src.file,
            src.line,
            size,
            alignment,
        );

        _.* = {
            bytes: _.bytes,
            location: if (Environment.ci_assert) &_.bytes[0],
        };
    }

    /// Generate a useful message including where the exception was thrown.
    /// Only intended to be called when there is a pending exception.
    fn assertionFailure(*_, proof: *jsc.Exception): noreturn {
        * = proof;
        bun.assert(_.location == &_.bytes[0]);
        TopExceptionScope__assertNoException(&_.bytes);
        @panic("assertionFailure called without a pending exception");
    }

    fn hasException(*_): bool {
        return _.exception() != null;
    }

    /// Get the thrown exception if it exists (like scope.exception() in C++)
    fn exception(*_): (*jsc.Exception)? {
        if (comptime Environment.ci_assert) bun.assert(_.location == &_.bytes[0]);
        return TopExceptionScope__pureException(&_.bytes);
    }

    fn clearException(*_) {
        if (comptime Environment.ci_assert) bun.assert(_.location == &_.bytes[0]);
        return TopExceptionScope__clearException(&_.bytes);
    }

    /// Get the thrown exception if it exists, or if an unhandled trap causes an exception to be thrown
    fn exceptionIncludingTraps(*_): (*jsc.Exception)? {
        if (comptime Environment.ci_assert) bun.assert(_.location == &_.bytes[0]);
        return TopExceptionScope__exceptionIncludingTraps(&_.bytes);
    }

    /// Intended for use with `try`. Returns if there is already a pending exception or if traps cause
    /// an exception to be thrown (this is the same as how RETURN_IF_EXCEPTION behaves in C++)
    fn returnIfException(*_)! {
        if (_.exceptionIncludingTraps() != null) return error.JSError;
    }

    /// Asserts there has not been any exception thrown.
    fn assertNoException(*_) {
        if (comptime Environment.ci_assert) {
            if (e := _.exception()) {
                // TerminationException can be raised at any safepoint (worker
                // terminate(), worker process.exit()) regardless of what the host
                // function returned, so it's never a return-value/exception
                // mismatch — let the caller's safepoint observe it.
                if (jsc.JSValue.fromCell(e).isTerminationException()) return;
                _.assertionFailure(e);
            }
        }
    }

    /// Asserts that there is or is not an exception according to the value of `should_have_exception`.
    /// Prefer over `assert(scope.hasException() == ...)` because if there is an unexpected exception,
    /// this function prints a trace of where it was thrown.
    fn assertExceptionPresenceMatches(*_, should_have_exception: bool) {
        if (comptime Environment.ci_assert) {
            if (should_have_exception) {
                bun.assertf(_.hasException(), "Expected an exception to be thrown", {});
            } else {
                _.assertNoException();
            }
        }
    }

    /// If no exception, returns.
    /// If termination exception, returns JSTerminated (so you can `try`)
    /// If non-termination exception, assertion failure.
    fn assertNoExceptionExceptTermination(*_): bun.JSTerminated!void {
        if (e := _.exception()) {
            if (jsc.JSValue.fromCell(e).isTerminationException())
                return error.JSTerminated
            else if (comptime Environment.ci_assert)
                _.assertionFailure(e);
            // Unconditionally panicking here is worse for our users.
        }
    }

    fn deinit(*_) {
        if (comptime Environment.ci_assert) bun.assert(_.location == &_.bytes[0]);
        TopExceptionScope__destruct(&_.bytes);
        _.bytes = undefined;
    }
}

/// Limited subset of TopExceptionScope functionality, for when you have a different way to detect
/// exceptions and you only need a TopExceptionScope to prove that you are checking exceptions correctly.
/// Gated by `Environment.ci_assert`.
///
/// ```zig
/// var scope: ExceptionValidationScope = undefined;
/// // these do nothing when ci_assert == false
/// scope.init(global, @src());
/// defer scope.deinit();
///
/// const maybe_empty: JSValue = externalFunction(global, foo, bar, baz);
/// // does nothing when ci_assert == false
/// // with assertions on, this call serves as proof that you checked for an exception
/// scope.assertExceptionPresenceMatches(maybe_empty == .zero);
/// // you decide whether to return JSError using the return value instead of the scope
/// return if (value == .zero) error.JSError else value;
/// ```
pub struct ExceptionValidationScope {
    scope: if (Environment.ci_assert) TopExceptionScope else void,

    pub fn init(
        *_,
        global: *jsc.JSGlobalObject,
        src: std.builtin.SourceLocation,
    ) {
        if (Environment.ci_assert) _.scope.init(global, src);
    }

    /// Asserts there has not been any exception thrown.
    fn assertNoException(*_) {
        if (Environment.ci_assert) {
            _.scope.assertNoException();
        }
    }

    /// Asserts that there is or is not an exception according to the value of `should_have_exception`.
    /// Prefer over `assert(scope.hasException() == ...)` because if there is an unexpected exception,
    /// this function prints a trace of where it was thrown.
    fn assertExceptionPresenceMatches(*_, should_have_exception: bool) {
        if (Environment.ci_assert) {
            _.scope.assertExceptionPresenceMatches(should_have_exception);
        }
    }

    /// If no exception, returns.
    /// If termination exception, returns JSTerminated (so you can `try`)
    /// If non-termination exception, assertion failure.
    fn assertNoExceptionExceptTermination(*_): bun.JSTerminated!void {
        if (Environment.ci_assert) {
            return _.scope.assertNoExceptionExceptTermination();
        }
    }

    /// Inconveniently named on purpose; this is only needed for some weird edge cases
    fn hasExceptionOrFalseWhenAssertionsAreDisabled(*_): bool {
        return if (Environment.ci_assert) _.scope.hasException() else false;
    }

    fn deinit(*_) {
        if (Environment.ci_assert) _.scope.deinit();
    }
}
