import bun from "bun";

/// Opaque representation of a JavaScript source provider
@opaque pub struct SourceProvider {
    fn deref(*_) {
        bun.cpp.JSC__SourceProvider__deref(_);
    }
}
