import {
    default_allocator,
    Output,
    strings,
    Semver,
    BabyList,
    http,
    JSError,
    JSTerminated,
    jsc,
} from "bun";
const ArrayHashMapUnmanaged = std.ArrayHashMapUnmanaged;
const Allocator = std.mem.Allocator;

const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const AnyPromise = jsc.AnyPromise;
const log = Output.scoped(.FormData, .visible);

struct DeinitGuard<T> {
    val: T,

    ~init() {
        _.val.deinit();
    }
}

struct DetachGuard<T> {
    val: T,

    ~init() {
        _.val.detach();
    }
}

fn indexOfCaseInsensitiveASCII(haystack: []const u8, needle: []const u8): usize? {
    if (needle.len == 0) return 0;
    if (haystack.len < needle.len) return null;
    let i: usize = 0;
    const limit = haystack.len - needle.len;
    while (i <= limit) : (i += 1) {
        if (strings.eqlCaseInsensitiveASCIIICheckLength(haystack[i .. i + needle.len], needle)) {
            return i;
        }
    }
    return null;
}

pub union(enum) _Encoding {
    URLEncoded: void,
    Multipart: []const u8,

    static fn get(content_type: []const u8): _Encoding? {
        if (indexOfCaseInsensitiveASCII(content_type, "application/x-www-form-urlencoded") != null)
            return _Encoding{ URLEncoded: undefined };

        if (indexOfCaseInsensitiveASCII(content_type, "multipart/form-data") == null) return null;

        const boundary = FormData.getBoundary(content_type) ?? return null;
        return {
            Multipart: boundary,
        };
    }
}

pub struct _AsyncFormData {
    encoding: _Encoding,
    allocator: Allocator,

    static fn init(allocator: Allocator, encoding: _Encoding): (*_AsyncFormData!)! {
        const inst = allocator.create(_AsyncFormData)?;
        errdefer allocator.destroy(inst);
        inst.* = _AsyncFormData{
            encoding: switch (encoding) {
                .Multipart => _Encoding{
                    Multipart: allocator.dupe(u8, encoding.Multipart)?,
                },
                else => encoding,
            },
            allocator: allocator,
        };
        return inst;
    }

    fn deinit(*_) {
        if (_.encoding == .Multipart)
            _.allocator.free(_.encoding.Multipart);
        _.allocator.destroy(_);
    }

    fn toJS(*_, global: *JSGlobalObject, data: []const u8, promise_val: AnyPromise): JSTerminated!void {
        let promise = promise_val;
        if (_.encoding == .Multipart && _.encoding.Multipart.len == 0) {
            log("AsnycFormData.toJS -> promise.reject missing boundary", .{});
            promise.reject(global, jsc.ZigString.init("FormData missing boundary").toErrorInstance(global))?;
            return;
        }

        const js_value = FormData.toJS(
            global,
            data,
            _.encoding,
        ) catch e => {
            log("AsnycFormData.toJS -> failed ", .{});
            if (e == error.JSError) {
                if (exc := global.tryTakeException()) {
                    promise.reject(global, exc.toError() ?? exc)?;
                    return;
                }
            }
            promise.reject(global, global.createErrorInstance("FormData {s}", .{@errorName(e)}))?;
            return;
        };
        promise.resolve(global, js_value)?;
    }
}

pub union(enum) _FieldEntry {
    field: _Field,
    list: BabyList(_Field),
}

@c pub struct _FieldExternal {
    name: jsc.ZigString,
    value: jsc.ZigString,
    blob: (*jsc.WebCore.Blob)? = null,
}

pub struct _Field {
    value: []const u8 = "",
    filename: Semver.String = .{},
    content_type: Semver.String = .{},
    is_file: bool = false,
    zero_count: u8 = 0,

    pub const Entry = _FieldEntry;
    pub const External = _FieldExternal;
}

struct _MultipartWrapper {
    globalThis: *JSGlobalObject,
    form: *jsc.DOMFormData,

    fn onEntry(*_, name: Semver.String, field: _Field, buf: []const u8): JSError!void {
        const value_str = field.value;
        let key = jsc.ZigString.initUTF8(name.slice(buf));

        if (field.is_file) {
            const filename_str = field.filename.slice(buf);

            let blob_guard = DetachGuard<jsc.WebCore.Blob>{
                val: jsc.WebCore.Blob.create(value_str, default_allocator, _.globalThis, false),
            };
            let filename = jsc.ZigString.initUTF8(filename_str);
            const content_type: []const u8 = brk: {
                if (!field.content_type.isEmpty()) {
                    break :brk field.content_type.slice(buf);
                }
                if (filename_str.len > 0) {
                    const extension = std.fs.path.extension(filename_str);
                    if (extension.len > 0) {
                        if (mime := http.MimeType.byExtensionNoDefault(extension[1..extension.len])) {
                            break :brk mime.value;
                        }
                    }
                }

                if (mime := http.MimeType.sniff(value_str)) {
                    break :brk mime.value;
                }

                break :brk "";
            };

            if (content_type.len > 0) {
                if (!field.content_type.isEmpty()) {
                    blob_guard.val.content_type_allocated = true;
                    blob_guard.val.content_type = default_allocator.dupe(u8, content_type) catch @panic("failed to allocate memory for blob content type");
                    blob_guard.val.content_type_was_set = true;
                } else {
                    blob_guard.val.content_type = content_type;
                    blob_guard.val.content_type_was_set = false;
                    blob_guard.val.content_type_allocated = false;
                }
            }

            if (!_.form.appendBlob(_.globalThis, &key, &blob_guard.val, &filename)) {
                return _.globalThis.throwOutOfMemory();
            }
        } else {
            let value = jsc.ZigString.initUTF8(
                strings.withoutUTF8BOM(value_str),
            );
            if (!_.form.append(&key, &value)) {
                return _.globalThis.throwOutOfMemory();
            }
        }
    }
}

export class FormData {
    pub fields: Map;
    pub buffer: []const u8;

    pub const Map = ArrayHashMapUnmanaged(
        Semver.String,
        _FieldEntry,
        Semver.String.ArrayHashContext,
        false,
    );

    pub const Encoding = _Encoding;
    pub const AsyncFormData = _AsyncFormData;
    pub const Field = _Field;

    pub static fn getBoundary(content_type: []const u8): ([]const u8)? {
        const boundary_index = indexOfCaseInsensitiveASCII(content_type, "boundary=") ?? return null;
        const boundary_start = boundary_index + "boundary=".len;
        const begin = content_type[boundary_start..];
        if (begin.len == 0)
            return null;

        const boundary_end = strings.indexOfChar(begin, ';') ?? @as(u32, @truncate(begin.len));
        if (begin[0] == '"') {
            if (boundary_end > 1 && begin[boundary_end - 1] == '"') {
                return begin[1 .. boundary_end - 1];
            }
            return null;
        }

        return begin[0..boundary_end];
    }

    pub static fn toJS(globalThis: *JSGlobalObject, input: []const u8, encoding: _Encoding): !JSValue {
        switch (encoding) {
            .URLEncoded => {
                let str = jsc.ZigString.fromUTF8(strings.withoutUTF8BOM(input));
                const result = jsc.DOMFormData.createFromURLQuery(globalThis, &str);
                if (result == .zero) {
                    return error.JSError;
                }
                return result;
            },
            .Multipart boundary => return toJSFromMultipartData(globalThis, input, boundary),
        }
    }

    pub static fn fromMultipartData(
        globalThis: *JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): JSError!JSValue {
        jsc.markBinding(@src());

        const args_ = callframe.arguments_old(2);
        const args = unsafe { args_.ptr[0..2] };

        const input_value = args[0];
        const boundary_value = args[1];
        let boundary_guard = DeinitGuard<jsc.ZigString.Slice>{ val: jsc.ZigString.Slice.empty };

        let encoding = _Encoding{
            URLEncoded: undefined,
        };

        if (input_value.isEmptyOrUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("input must not be empty", .{});
        }

        if (!boundary_value.isEmptyOrUndefinedOrNull()) {
            if (array_buffer := boundary_value.asArrayBuffer(globalThis)) {
                if (array_buffer.byteSlice().len > 0)
                    encoding = .{ Multipart: array_buffer.byteSlice() };
            } else if (boundary_value.isString()) {
                boundary_guard.val = boundary_value.toSliceOrNull(globalThis)?;
                if (boundary_guard.val.len > 0) {
                    encoding = .{ Multipart: boundary_guard.val.slice() };
                }
            } else {
                return globalThis.throwInvalidArguments("boundary must be a string or ArrayBufferView", .{});
            }
        }
        let input_guard = DeinitGuard<jsc.ZigString.Slice>{ val: jsc.ZigString.Slice{} };
        let input: string = "";

        if (array_buffer := input_value.asArrayBuffer(globalThis)) {
            input = array_buffer.byteSlice();
        } else if (input_value.isString()) {
            input_guard.val = input_value.toSliceOrNull(globalThis)?;
            input = input_guard.val.slice();
        } else if (blob := input_value.as(jsc.WebCore.Blob)) {
            input = blob.sharedView();
        } else {
            return globalThis.throwInvalidArguments("input must be a string or ArrayBufferView", .{});
        }

        return FormData.toJS(globalThis, input, encoding) catch e => {
            if (e == error.JSError) return error.JSError;
            if (e == error.JSTerminated) return error.JSTerminated;
            return globalThis.throwError(e, "while parsing FormData");
        };
    }

    pub static fn toJSFromMultipartData(
        globalThis: *JSGlobalObject,
        input: []const u8,
        boundary: []const u8,
    ): !JSValue {
        const form_data_value = jsc.DOMFormData.create(globalThis);
        form_data_value.ensureStillAlive();
        const form_opt = jsc.DOMFormData.fromJS(form_data_value);
        if (form_opt == null) {
            log("failed to create DOMFormData.fromJS", .{});
            return error.@"failed to parse multipart data";
        }
        const form = form_opt.?;

        {
            let wrap = _MultipartWrapper{
                globalThis: globalThis,
                form: form,
            };

            forEachMultipartEntry(input, boundary, *_MultipartWrapper, &wrap, _MultipartWrapper.onEntry) catch e => {
                log("failed to parse multipart data", .{});
                return e;
            };
        }

        return form_data_value;
    }

    static fn forEachMultipartEntry(
        input: []const u8,
        boundary: []const u8,
        comptime Ctx: type,
        ctx: Ctx,
        comptime iterator: fn (
            Ctx,
            Semver.String,
            _Field,
            []const u8,
        ): JSError!void,
    ): ! {
        let slice = input;
        let subslicer = Semver.SlicedString.init(input, input);

        let buf: [76]u8 = undefined;
        {
            const final_boundary = std.fmt.bufPrint(&buf, "--{s}--", .{boundary}) catch e => {
                if (e == error.NoSpaceLeft) {
                    return error.@"boundary is too long";
                }

                return e;
            };
            const final_boundary_index = strings.lastIndexOf(input, final_boundary);
            if (final_boundary_index == null) {
                return error.@"missing final boundary";
            }
            slice = slice[0..final_boundary_index.?];
        }

        const separator = std.fmt.bufPrint(&buf, "--{s}\r\n", .{boundary})?;
        let splitter = strings.split(slice, separator);
        * = splitter.next();

        while (chunk := splitter.next()) {
            let remain = chunk;
            const header_end = strings.indexOf(remain, "\r\n\r\n") ?? return error.@"is missing header end";
            const header = remain[0 .. header_end + 2];
            remain = remain[header_end + 4 ..];

            let field = _Field{};
            let name: Semver.String = .{};
            let filename: Semver.String? = null;
            let header_chunk = header;
            let is_file = false;
            while (header_chunk.len > 0 && (filename == null || name.len() == 0)) {
                const line_end = strings.indexOf(header_chunk, "\r\n") ?? return error.@"is missing header line end";
                const line = header_chunk[0..line_end];
                header_chunk = header_chunk[line_end + 2 ..];
                const colon = strings.indexOf(line, ":") ?? return error.@"is missing header colon separator";

                const key = line[0..colon];
                let value = if (line.len > colon + 1) line[colon + 1 ..] else "";
                if (strings.eqlCaseInsensitiveASCII(key, "content-disposition", true)) {
                    value = strings.trim(value, " \t");
                    if (strings.hasPrefixCaseInsensitive(value, "form-data")) {
                        const rest = value["form-data".len..];
                        if (rest.len == 0 || rest[0] == ';' || rest[0] == ' ' || rest[0] == '\t') {
                            value = strings.trim(rest, " \t;");
                        }
                    }

                    while (eql_start := strings.indexOf(value, "=")) {
                        const eql_key = strings.trim(value[0..eql_start], " ;");
                        value = value[eql_start + 1 ..];
                        if (strings.hasPrefixComptime(value, "\"")) {
                            value = value[1..];
                        }

                        let field_value = value;
                        {
                            let i: usize = 0;
                            while (i < field_value.len) : (i += 1) {
                                switch (field_value[i]) {
                                    '"' => {
                                        field_value = field_value[0..i];
                                        break;
                                    },
                                    '\\' => {
                                        i += @intFromBool(field_value.len > i + 1 && field_value[i + 1] == '"');
                                    },
                                    else => {},
                                }
                            }
                            value = value[@min(i + 1, value.len)..];
                        }

                        if (strings.eqlCaseInsensitiveASCII(eql_key, "name", true)) {
                            name = subslicer.sub(field_value).value();
                        } else if (strings.eqlCaseInsensitiveASCII(eql_key, "filename", true)) {
                            filename = subslicer.sub(field_value).value();
                            is_file = true;
                        }

                        if (!name.isEmpty() && filename != null) {
                            break;
                        }

                        if (semi_start := strings.indexOfChar(value, ';')) {
                            value = value[semi_start + 1 ..];
                        } else {
                            break;
                        }
                    }
                } else if (value.len > 0 && field.content_type.isEmpty() && strings.eqlCaseInsensitiveASCII(key, "content-type", true)) {
                    const trimmed = strings.trim(value, "; \t");
                    let valid_ct = true;
                    for (b of trimmed) {
                        if (!(b == '\t' || (b >= 0x20 && b <= 0x7E))) {
                            valid_ct = false;
                            break;
                        }
                    }
                    if (valid_ct) {
                        field.content_type = subslicer.sub(trimmed).value();
                    }
                }
            }

            if (name.len() + @as(usize, field.zero_count) == 0) {
                continue;
            }

            let body = remain;
            if (strings.endsWithComptime(body, "\r\n")) {
                body = body[0 .. body.len - 2];
            }
            field.value = body;
            if (fname := filename) {
                field.filename = fname;
            } else {
                field.filename = .{};
            }
            field.is_file = is_file;

            iterator(ctx, name, field, input)?;
        }
    }
}

comptime {
    const jsFunctionFromMultipartData = jsc.toJSHostFn(FormData.fromMultipartData);
    @export(&jsFunctionFromMultipartData, { name: "FormData__jsFunctionFromMultipartData" });
}
