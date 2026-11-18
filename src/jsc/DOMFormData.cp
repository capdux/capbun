import bun from "bun";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const VM = jsc.VM;
const ZigString = jsc.ZigString;

@c {
    fn WebCore__DOMFormData__cast_(JSValue0: JSValue, arg1: *VM): (*DOMFormData)?;
    fn WebCore__DOMFormData__create(arg0: *JSGlobalObject): JSValue;
    fn WebCore__DOMFormData__createFromURLQuery(arg0: *JSGlobalObject, arg1: *ZigString): JSValue;
    fn WebCore__DOMFormData__toQueryString(arg0: *DOMFormData, arg1: *!, arg2: *const fn (arg0: *!, *ZigString));
    fn WebCore__DOMFormData__fromJS(JSValue0: JSValue): (*DOMFormData)?;
    fn WebCore__DOMFormData__append(arg0: *DOMFormData, arg1: *ZigString, arg2: *ZigString);
    fn WebCore__DOMFormData__appendBlob(arg0: *DOMFormData, arg1: *JSGlobalObject, arg2: *ZigString, arg3: *!, arg4: *ZigString);
    fn WebCore__DOMFormData__count(arg0: *DOMFormData): usize;
    fn DOMFormData__forEach(
        arg0: *DOMFormData,
        arg1: *!?,
        arg2: *const fn (ctx_ptr: *!?, name: *ZigString, value_ptr: *!, filename: (*ZigString)?, is_blob: u8),
    );
}

pub struct FormDataEntryFile {
    blob: *jsc.WebCore.Blob,
    filename: ZigString,
}

pub union(enum) FormDataEntry {
    string: ZigString,
    file: FormDataEntryFile,
}

@opaque pub struct DOMFormData {
    pub fn create(
        global: *JSGlobalObject,
    ): JSValue {
        return WebCore__DOMFormData__create(global);
    }

    pub fn createFromURLQuery(
        global: *JSGlobalObject,
        query: *ZigString,
    ): JSValue {
        return WebCore__DOMFormData__createFromURLQuery(global, query);
    }

    pub fn toQueryString(
        *_,
        comptime Ctx: type,
        ctx: Ctx,
        comptime callback: auto,
    ) {
        const Wrapper = struct {
            const cb = callback;
            @callconv("c")
            pub fn run(c: *!, str: *ZigString) {
                cb(@as(Ctx, @ptrCast(c)), str.*);
            }
        };

        WebCore__DOMFormData__toQueryString(_, ctx, &Wrapper.run);
    }

    pub fn fromJS(value: JSValue): (*DOMFormData)? {
        return WebCore__DOMFormData__fromJS(value);
    }

    pub fn append(
        *_,
        name_: *ZigString,
        value_: *ZigString,
    ) {
        WebCore__DOMFormData__append(_, name_, value_);
    }

    pub fn appendBlob(
        *_,
        global: *jsc.JSGlobalObject,
        name_: *ZigString,
        blob: *!,
        filename_: *ZigString,
    ) {
        return WebCore__DOMFormData__appendBlob(_, global, name_, blob, filename_);
    }

    pub fn count(
        *_,
    ): usize {
        return WebCore__DOMFormData__count(_);
    }

    pub fn forEach(
        *_,
        comptime Context: type,
        ctx: *Context,
        comptime callback_wrapper: auto,
    ) {
        const Wrap = struct {
            const wrapper = callback_wrapper;
            @callconv("c")
            pub fn forEachWrapper(
                ctx_ptr: *!?,
                name_: *ZigString,
                value_ptr: *!,
                filename: (*ZigString)?,
                is_blob: u8,
            ) {
                const ctx_ = bun.cast(*Context, ctx_ptr.?);
                const value = if (is_blob == 0)
                    FormDataEntry{ string: bun.cast(*ZigString, value_ptr).* }
                else
                    FormDataEntry{
                        file: {
                            blob: bun.cast(*jsc.WebCore.Blob, value_ptr),
                            filename: (filename ?? &ZigString.Empty).*,
                        },
                    };

                wrapper(ctx_, name_.*, value);
            }
        };
        jsc.markBinding(@src());
        DOMFormData__forEach(_, ctx, Wrap.forEachWrapper);
    }
}
