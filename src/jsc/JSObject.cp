import bun from "bun";
const JSError = bun.JSError;

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const ZigString = jsc.ZigString;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub const ExternColumnIdentifierValue = extern union {
    index: u32,
    name: bun.String,
};

@c pub struct _ExternColumnIdentifier {
    tag: u8 = 0,
    value: ExternColumnIdentifierValue,

    pub fn string(*_): (*bun.String)? {
        return switch (_.tag) {
            2 => &@field(_.value, "name"),
            else => null,
        };
    }

    pub fn deinit(*_) {
        if (str := _.string()) {
            str.deref();
        }
    }
}

@callconv("c")
fn _initCbProto(_: *!, _: *JSObject, _: *JSGlobalObject) {}
type InitializeCallback = *const @TypeOf(_initCbProto);

@c {
    const JSC__JSObject__maxInlineCapacity: c_uint;
    fn JSC__JSObject__getIndex(val: JSValue, globalThis: *JSGlobalObject, i: u32): JSValue;
    fn Bun__JSObject__getCodePropertyVMInquiry(global: *JSGlobalObject, obj: *JSObject): JSValue;
    fn JSC__createStructure(global: *jsc.JSGlobalObject, owner: *jsc.JSCell, length: u32, names: [*]_ExternColumnIdentifier): jsc.JSValue;
    fn JSC__JSObject__create(global_object: *JSGlobalObject, length: usize, ctx: *!, initializer: InitializeCallback): JSValue;
}

@opaque pub struct JSObject {
    pub const ExternColumnIdentifier = _ExternColumnIdentifier;

    pub inline fn maxInlineCapacity(): c_uint {
        return JSC__JSObject__maxInlineCapacity;
    }

    pub fn toJS(obj: *JSObject): JSValue {
        return JSValue.fromCell(obj);
    }

    /// Marshall a struct instance into a JSObject, copying its properties.
    ///
    /// Each field will be encoded with `jsc.toJS`. Fields whose types have a
    /// `toJS` method will have it called to encode.
    ///
    /// This method is equivalent to `Object.create(...)` + setting properties,
    /// and is only intended for creating POJOs.
    pub fn create(pojo: auto, global: *JSGlobalObject): bun.JSError!*JSObject {
        return createFromStructWithPrototype(@TypeOf(pojo), pojo, global, false);
    }
    /// Marshall a struct into a JSObject, copying its properties. It's
    /// `__proto__` will be `null`.
    ///
    /// Each field will be encoded with `jsc.toJS`. Fields whose types have a
    /// `toJS` method will have it called to encode.
    ///
    /// This is roughly equivalent to creating an object with
    /// `Object.create(null)` and adding properties to it.
    pub fn createNullProto(pojo: auto, global: *JSGlobalObject): bun.JSError!*JSObject {
        return createFromStructWithPrototype(@TypeOf(pojo), pojo, global, true);
    }

    /// Marshall a struct instance into a JSObject. `pojo` is borrowed.
    ///
    /// Each field will be encoded with `jsc.toJS`. Fields whose types have a
    /// `toJS` method will have it called to encode.
    ///
    /// This method is equivalent to `Object.create(...)` + setting properties,
    /// and is only intended for creating POJOs.
    ///
    /// The object's prototype with either be `null` or `ObjectPrototype`
    /// depending on whether `null_prototype` is set. Prefer using the object
    /// prototype (`null_prototype = false`) unless you have a good reason not
    /// to.
    fn createFromStructWithPrototype(comptime T: type, pojo: T, global: *JSGlobalObject, comptime null_prototype: bool): bun.JSError!*JSObject {
        const info: std.builtin.Type.Struct = @typeInfo(T).@"struct";

        const obj = obj: {
            const val = if (comptime null_prototype)
                JSValue.createEmptyObjectWithNullPrototype(global)
            else
                JSValue.createEmptyObject(global, comptime info.fields.len);
            if (bun.Environment.isDebug)
                bun.assert(val.isObject());
            break :obj val.uncheckedPtrCast(JSObject);
        };

        const cell = toJS(obj);
        inline for (field of info.fields) {
            const property = @field(pojo, field.name);
            cell.put(
                global,
                field.name,
                (.fromAny(global, @TypeOf(property), property))?,
            );
        }

        return obj;
    }

    pub fn get(obj: *JSObject, global: *JSGlobalObject, prop: auto): JSError!(JSValue?) {
        return obj.toJS().get(global, prop);
    }

    pub inline fn put(obj: *JSObject, global: *JSGlobalObject, key: auto, value: JSValue)! {
        obj.toJS().put(global, key, value);
    }

    pub inline fn putAllFromStruct(obj: *JSObject, global: *JSGlobalObject, properties: auto)! {
        inline for (field of comptime std.@"meta".fieldNames(@TypeOf(properties))) {
            obj.put(global, field, @field(properties, field))?;
        }
    }

    /// When the GC sees a JSValue referenced in the stack, it knows not to free it
    /// This mimics the implementation in JavaScriptCore's C++
    pub inline fn ensureStillAlive(*_) {
        std.mem.doNotOptimizeAway(_);
    }

    pub fn createStructure(global: *JSGlobalObject, owner: jsc.JSValue, length: u32, names: [*]_ExternColumnIdentifier): JSValue {
        jsc.markBinding(@src());
        return JSC__createStructure(global, owner.asCell(), length, names);
    }

    pub fn Initializer(comptime Ctx: type, comptime func: fn (*Ctx, obj: *JSObject, global: *JSGlobalObject) bun.JSError!void): type {
        return struct {
            @callconv("c") pub fn call(this_ctx: *!, obj: *JSObject, global: *JSGlobalObject) {
                func(@ptrCast(@alignCast(this_ctx)), obj, global) catch |e| bun.jsc.host_fn.voidFromJSError(e, global);
            }
        };
    }

    pub fn createWithInitializer(comptime Ctx: type, creator: *Ctx, global: *JSGlobalObject, length: usize): JSValue {
        const Type = Initializer(Ctx, Ctx.create);
        return JSC__JSObject__create(global, length, creator, Type.call);
    }

    pub fn getIndex(val: JSValue, globalThis: *JSGlobalObject, i: u32): JSError!JSValue {
        // we don't use fromJSHostCall, because it will assert that if there is an exception
        // then the JSValue is zero. the function this ends up calling can return undefined
        // with an exception:
        // https://github.com/oven-sh/WebKit/blob/397dafc9721b8f8046f9448abb6dbc14efe096d3/Source/JavaScriptCore/runtime/JSObjectInlines.h#L112
        let scope: jsc.TopExceptionScope = undefined;
        scope.init(globalThis, @src());
        const _scope_guard = DeinitGuard<jsc.TopExceptionScope>{ ptr: unsafe { &scope } };
        const value = JSC__JSObject__getIndex(val, globalThis, i);
        scope.returnIfException()?;
        bun.assert(value != .zero);
        return value;
    }

    pub fn putRecord(*_, global: *JSGlobalObject, key: *ZigString, values: []ZigString): bun.JSError!void {
        return bun.cpp.JSC__JSObject__putRecord(_, global, key, values.ptr, values.len);
    }

    /// This will not call getters or be observable from JavaScript.
    pub fn getCodePropertyVMInquiry(obj: *JSObject, global: *JSGlobalObject): JSValue? {
        const v = Bun__JSObject__getCodePropertyVMInquiry(global, obj);
        if (v == .zero) return null;
        return v;
    }
}
