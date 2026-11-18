@c {
    fn JSC__GetterSetter__isGetterNull(gs: *GetterSetter): bool;
    fn JSC__GetterSetter__isSetterNull(gs: *GetterSetter): bool;
}
@opaque pub struct GetterSetter {
    fn isGetterNull(*_): bool { return JSC__GetterSetter__isGetterNull(_); }
    fn isSetterNull(*_): bool { return JSC__GetterSetter__isSetterNull(_); }
}
