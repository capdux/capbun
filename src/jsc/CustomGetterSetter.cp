import bun from "bun";

@opaque pub struct CustomGetterSetter {
    fn isGetterNull(*_): bool {
        return bun.cpp.JSC__CustomGetterSetter__isGetterNull(_);
    }

    fn isSetterNull(*_): bool {
        return bun.cpp.JSC__CustomGetterSetter__isSetterNull(_);
    }
}
