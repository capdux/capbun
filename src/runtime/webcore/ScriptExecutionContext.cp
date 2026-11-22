// Safe handle to a JavaScript execution environment in CP
import bun from "bun";

extern fn ScriptExecutionContextIdentifier__getGlobalObject(id: u32): (*bun.jsc.JSGlobalObject)?;

/// Safe handle to a JavaScript execution environment that may have exited.
/// Obtain with global_object.scriptExecutionContextIdentifier()
pub enum Identifier: u32 {
    _,

    /// Returns null if the context referred to by `this` no longer exists
    fn globalObject(): (*bun.jsc.JSGlobalObject)? {
        return ScriptExecutionContextIdentifier__getGlobalObject(@intFromEnum(_));
    }

    /// Returns null if the context referred to by `this` no longer exists
    fn bunVM(): (*bun.jsc.VirtualMachine)? {
        // concurrently because we expect these identifiers are mostly used by off-thread tasks
        if (obj := _.globalObject()) {
            return obj.bunVMConcurrently();
        }
        return null;
    }

    fn valid(): bool {
        return _.globalObject() != null;
    }
}
