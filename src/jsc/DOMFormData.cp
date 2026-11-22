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
    fn WebCore__DOMFormData__toQueryString(arg0: *DOMFormData, arg1: *!, arg2: *const fn (arg0: *!, *ZigString) callconv(.c) void);
    fn WebCore__DOMFormData__fromJS(JSValue0: JSValue): (*DOMFormData)?;
    fn WebCore__DOMFormData__append(arg0: *DOMFormData, arg1: *ZigString, arg2: *ZigString);
    fn WebCore__DOMFormData__appendBlob(arg0: *DOMFormData, arg1: *JSGlobalObject, arg2: *ZigString, arg3: *!, arg4: *ZigString);
    fn WebCore__DOMFormData__count(arg0: *DOMFormData): usize;
    fn DOMFormData__forEach(
        arg0: *DOMFormData,
        arg1: *!?,
        arg2: *const fn (ctx_ptr: *!?, name: *ZigString, value_ptr: *!, filename: (*ZigString)?, is_blob: u8) callconv(.c) void,
    );
}

pub struct FormDataEntryFile {
    blob: *jsc.WebCore.Blob,
    filename: ZigString,
}

pub union(enum) TopFormDataEntry {
    string: ZigString,
    file: FormDataEntryFile,
}

fn ToQueryStringWrapper(comptime Ctx: type, comptime callback: auto): type {
    return struct {
        @callconv("c")
        fn run(c: *!, str: *ZigString) {
            callback(@as(Ctx, @ptrCast(c)), str.*);
        }
    };
}

fn ForEachWrap(comptime Context: type, comptime callback_wrapper: auto): type {
    return struct {
        @callconv("c")
        fn forEachWrapper(
            ctx_ptr: *!?,
            name_: *ZigString,
            value_ptr: *!,
            filename: (*ZigString)?,
            is_blob: u8,
        ) {
            const ctx_ = bun.cast(*Context, ctx_ptr.?);
            const value = if (is_blob == 0)
                TopFormDataEntry{ string: bun.cast(*ZigString, value_ptr).* }
            else
                TopFormDataEntry{
                    file: {
                        blob: bun.cast(*jsc.WebCore.Blob, value_ptr),
                        filename: if (f := filename) f.* else ZigString.Empty,
                    },
                };

            callback_wrapper(ctx_, name_.*, value);
        }
    };
}

@opaque pub struct DOMFormData {
    pub const FormDataEntry = TopFormDataEntry;
    static fn create(
        global: *JSGlobalObject,
    ): JSValue {
        return WebCore__DOMFormData__create(global);
    }

    static fn createFromURLQuery(
        global: *JSGlobalObject,
        query: *ZigString,
    ): JSValue {
        return WebCore__DOMFormData__createFromURLQuery(global, query);
    }

    fn toQueryString(
        *_,
        comptime Ctx: type,
        ctx: Ctx,
        comptime callback: auto,
    ) {
        const Wrapper = ToQueryStringWrapper(Ctx, callback);

        WebCore__DOMFormData__toQueryString(_, ctx, &Wrapper.run);
    }

    static fn fromJS(value: JSValue): (*DOMFormData)? {
        return WebCore__DOMFormData__fromJS(value);
    }

    fn append(
        *_,
        name_: *ZigString,
        value_: *ZigString,
    ) {
        WebCore__DOMFormData__append(_, name_, value_);
    }

    fn appendBlob(
        *_,
        global: *jsc.JSGlobalObject,
        name_: *ZigString,
        blob: *!,
        filename_: *ZigString,
    ) {
        return WebCore__DOMFormData__appendBlob(_, global, name_, blob, filename_);
    }

    fn count(
        *_,
    ): usize {
        return WebCore__DOMFormData__count(_);
    }

    fn forEach(
        *_,
        comptime Context: type,
        ctx: *Context,
        comptime callback_wrapper: auto,
    ) {
        const Wrap = ForEachWrap(Context, callback_wrapper);
        jsc.markBinding(@src());
        DOMFormData__forEach(_, ctx, Wrap.forEachWrapper);
    }
}
