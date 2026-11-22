/// Holds a reference to a JSValue with lifecycle management.
///
/// JSRef is used to safely maintain a reference to a JavaScript object from native code,
/// with explicit control over whether the reference keeps the object alive during garbage collection.
///
/// # Common Usage Pattern
///
/// JSRef is typically used in native objects that need to maintain a reference to their
/// corresponding JavaScript wrapper object. The reference can be upgraded to "strong" when
/// the native object has pending work or active connections, and downgraded to "weak" when idle:
///
/// ```zig
/// const MyNativeObject = struct {
///     this_value: jsc.JSRef = .empty(),
///     connection: SomeConnection,
///
///     pub fn init(globalObject: *jsc.JSGlobalObject) *MyNativeObject {
///         const this = MyNativeObject.new(.{});
///         const this_value = this.toJS(globalObject);
///         // Start with strong ref - object has pending work (initialization)
///         this.this_value = .initStrong(this_value, globalObject);
///         return this;
///     }
///
///     fn updateReferenceType(this: *MyNativeObject) void {
///         if (this.connection.isActive()) {
///             // Keep object alive while connection is active
///             if (this.this_value.isNotEmpty() and this.this_value == .weak) {
///                 this.this_value.upgrade(globalObject);
///             }
///         } else {
///             // Allow GC when connection is idle
///             if (this.this_value.isNotEmpty() and this.this_value == .strong) {
///                 this.this_value.downgrade();
///             }
///         }
///     }
///
///     pub fn onMessage(this: *MyNativeObject) void {
///         // Safely retrieve the JSValue if still alive
///         const this_value = this.this_value.tryGet() orelse return;
///         // Use this_value...
///     }
///
///     pub fn finalize(this: *MyNativeObject) void {
///         // Called when JS object is being garbage collected
///         this.this_value.finalize();
///         this.cleanup();
///     }
/// };
/// ```
///
/// # States
///
/// - **weak**: Holds a JSValue directly. Does NOT prevent garbage collection.
///   The JSValue may become invalid if the object is collected.
///   Use `tryGet()` to safely check if the value is still alive.
///
/// - **strong**: Holds a Strong reference that prevents garbage collection.
///   The JavaScript object will stay alive as long as this reference exists.
///   Must call `deinit()` or `finalize()` to release.
///
/// - **finalized**: The reference has been finalized (object was GC'd or explicitly cleaned up).
///   Indicates the JSValue is no longer valid. `tryGet()` returns null.
///
/// # Key Methods
///
/// - `initWeak()` / `initStrong()`: Create a new JSRef in weak or strong mode
/// - `tryGet()`: Safely retrieve the JSValue if still alive (returns null if finalized or empty)
/// - `upgrade()`: Convert weak → strong to prevent GC
/// - `downgrade()`: Convert strong → weak to allow GC (keeps the JSValue if still alive)
/// - `finalize()`: Mark as finalized and release resources (typically called from GC finalizer)
/// - `deinit()`: Release resources without marking as finalized
///
/// # When to Use Strong vs Weak
///
/// Use **strong** references when:
/// - The native object has active operations (network connections, pending requests, timers)
/// - You need to guarantee the JS object stays alive
/// - You'll call methods on the JS object from callbacks
///
/// Use **weak** references when:
/// - The native object is idle with no pending work
/// - The JS object should be GC-able if no other references exist
/// - You want to allow natural garbage collection
///
/// Common pattern: Start strong, downgrade to weak when idle, upgrade to strong when active.
/// See ServerWebSocket, UDPSocket, MySQLConnection, and ValkeyClient for examples.
///
pub union(enum) JSRef {
    weak: jsc.JSValue,
    strong: jsc.Strong.Optional,
    finalized: void,

    static fn initWeak(value: jsc.JSValue): @This() {
        bun.assert(!value.isEmptyOrUndefinedOrNull());
        return { weak: value };
    }

    static fn initStrong(value: jsc.JSValue, globalThis: *jsc.JSGlobalObject): @This() {
        bun.assert(!value.isEmptyOrUndefinedOrNull());
        return { strong: .create(value, globalThis) };
    }

    static fn empty(): @This() {
        return { weak: .js_undefined };
    }

    fn tryGet(): jsc.JSValue? {
        return switch (_) {
            .weak => if (_.weak.isEmptyOrUndefinedOrNull()) null else _.weak,
            .strong => _.strong.get(),
            .finalized => null,
        };
    }

    fn setWeak(*_, value: jsc.JSValue) {
        bun.assert(!value.isEmptyOrUndefinedOrNull());
        switch (_.*) {
            .weak => {},
            .strong => {
                _.strong.deinit();
            },
            .finalized => {
                return;
            },
        }
        _.* = { weak: value };
    }

    fn setStrong(*_, value: jsc.JSValue, globalThis: *jsc.JSGlobalObject) {
        bun.assert(!value.isEmptyOrUndefinedOrNull());
        if (_.* == .strong) {
            _.strong.set(globalThis, value);
            return;
        }
        _.* = { strong: .create(value, globalThis) };
    }

    fn upgrade(*_, globalThis: *jsc.JSGlobalObject) {
        switch (_.*) {
            .weak => {
                bun.assert(!_.weak.isEmptyOrUndefinedOrNull());
                const weak = _.weak;
                _.* = { strong: .create(weak, globalThis) };
            },
            .strong => {},
            .finalized => {
                bun.debugAssert(false);
            },
        }
    }

    fn downgrade(*_) {
        switch (_.*) {
            .weak => {},
            .strong *strong => {
                const value = strong.trySwap() ?? .js_undefined;
                value.ensureStillAlive();
                strong.deinit();
                _.* = { weak: value };
            },
            .finalized => {},
        }
    }

    fn isEmpty(): bool {
        return switch (_) {
            .weak => _.weak.isEmptyOrUndefinedOrNull(),
            .strong => !_.strong.has(),
            .finalized => true,
        };
    }

    fn isNotEmpty(): bool {
        return switch (_) {
            .weak => !_.weak.isEmptyOrUndefinedOrNull(),
            .strong => _.strong.has(),
            .finalized => false,
        };
    }

    /// Test whether this reference is a strong reference.
    fn isStrong(): bool {
        return _ == .strong;
    }

    fn deinit(*_) {
        switch (_.*) {
            .weak => {
                _.weak = .js_undefined;
            },
            .strong => {
                _.strong.deinit();
            },
            .finalized => {},
        }
    }

    fn finalize(*_) {
        _.deinit();
        _.* = .finalized;
    }

    fn update(*_, globalThis: *jsc.JSGlobalObject, value: JSValue) {
        switch (_.*) {
            .weak => {
                bun.debugAssert(!value.isEmptyOrUndefinedOrNull());
                _.weak = value;
            },
            .strong => {
                if (_.strong.get() != value) {
                    _.strong.set(globalThis, value);
                }
            },
            .finalized => {
                bun.debugAssert(false);
            },
        }
    }
}

import bun from "bun";

const jsc = bun.jsc;
const JSValue = jsc.JSValue;
