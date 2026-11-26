import bun from "bun";
import { default_allocator, ptr, strings, String, JSError } from "bun";
import * as xml_response from "./xml_response.zig";
const BunString = bun.String;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSValue = bun.jsc.JSValue;
const ZigString = bun.jsc.ZigString;
const Managed = std.array_list.Managed;

type JSStringRef = *!?;

@c {
    fn JSObjectSetProperty(ctx: *JSGlobalObject, object: bun.jsc.C.JSObjectRef, propertyName: JSStringRef, value: bun.jsc.C.JSValueRef, attributes: bun.jsc.C.JSPropertyAttributes, exception: bun.jsc.C.ExceptionRef);
    fn JSStringCreateWithUTF8CString(str: [*:0]const u8): JSStringRef;
    fn JSStringRelease(str: JSStringRef);
}

export struct S3ListObjectsOptions {
    continuation_token: string? = null;
    delimiter: string? = null;
    encoding_type: string? = null;
    fetch_owner: bool? = null;
    max_keys: i64? = null;
    prefix: string? = null;
    start_after: string? = null;

    _continuation_token: ZigString.Slice? = null;
    _delimiter: ZigString.Slice? = null;
    _encoding_type: ZigString.Slice? = null;
    _prefix: ZigString.Slice? = null;
    _start_after: ZigString.Slice? = null;

    pub fn deinit(*_) {
        if (slice := _._continuation_token) slice.deinit();
        if (slice := _._delimiter) slice.deinit();
        if (slice := _._encoding_type) slice.deinit();
        if (slice := _._prefix) slice.deinit();
        if (slice := _._start_after) slice.deinit();
    }
}

export struct ObjectOwner {
    id: string? = null;
    display_name: string? = null;
}

export struct ObjectRestoreStatus {
    is_restore_in_progress: bool? = null;
    restore_expiry_date: string? = null;
}

export struct S3ListObjectsContents {
    key: string;
    etag: ptr.OwnedIn(string, bun.allocators.MaybeOwned(bun.DefaultAllocator))? = null;
    checksum_type: string? = null;
    checksum_algorithme: string? = null;
    last_modified: string? = null;
    object_size: i64? = null;
    storage_class: string? = null;
    owner: ObjectOwner? = null;
    restore_status: ObjectRestoreStatus? = null;
    strings_owned: bool = false;

    pub fn deinit(*_) {
        if (*etag := _.etag) etag.deinit();
        if (_.strings_owned) {
            default_allocator.free(_.key);
            if (s := _.checksum_type) default_allocator.free(s);
            if (s := _.checksum_algorithme) default_allocator.free(s);
            if (s := _.last_modified) default_allocator.free(s);
            if (s := _.storage_class) default_allocator.free(s);
            if (o := _.owner) {
                if (s := o.id) default_allocator.free(s);
                if (s := o.display_name) default_allocator.free(s);
            }
            if (r := _.restore_status) {
                if (s := r.restore_expiry_date) default_allocator.free(s);
            }
        }
    }
}

export struct S3ListObjectsV2Result {
    name: string? = null;
    prefix: string? = null;
    key_count: i64? = null;
    max_keys: i64? = null;
    delimiter: string? = null;
    delimiter_owned: bool = false;
    encoding_type: string? = null;
    is_truncated: bool? = null;
    continuation_token: string? = null;
    next_continuation_token: string? = null;
    start_after: string? = null;
    common_prefixes: Managed(string)? = null;
    contents: Managed(S3ListObjectsContents)? = null;
    strings_owned: bool = false;

    pub fn deinit(*_) {
        if (_.delimiter_owned || _.strings_owned) {
            if (d := _.delimiter) default_allocator.free(d);
        }
        if (_.strings_owned) {
            if (s := _.name) default_allocator.free(s);
            if (s := _.prefix) default_allocator.free(s);
            if (s := _.encoding_type) default_allocator.free(s);
            if (s := _.continuation_token) default_allocator.free(s);
            if (s := _.next_continuation_token) default_allocator.free(s);
            if (s := _.start_after) default_allocator.free(s);
        }
        if (contents := _.contents) {
            for (*item of contents.items) item.deinit();
            contents.deinit();
        }
        if (common_prefixes := _.common_prefixes) {
            if (_.strings_owned) {
                for (cp_prefix of common_prefixes.items) default_allocator.free(cp_prefix);
            }
            common_prefixes.deinit();
        }
    }

    pub fn toJS(*_, globalObject: *JSGlobalObject): JSError!JSValue {
        let jsResult = JSValue.createEmptyObject(globalObject, 0);

        if (name := _.name) {
            jsResult.put(globalObject, ZigString.static("name"), BunString.createUTF8ForJS(globalObject, name)?);
        }

        if (prefix := _.prefix) {
            jsResult.put(globalObject, ZigString.static("prefix"), BunString.createUTF8ForJS(globalObject, prefix)?);
        }

        if (delimiter := _.delimiter) {
            jsResult.put(globalObject, ZigString.static("delimiter"), BunString.createUTF8ForJS(globalObject, delimiter)?);
        }

        if (start_after := _.start_after) {
            jsResult.put(globalObject, ZigString.static("startAfter"), BunString.createUTF8ForJS(globalObject, start_after)?);
        }
        if (encoding_type := _.encoding_type) {
            jsResult.put(globalObject, ZigString.static("encodingType"), BunString.createUTF8ForJS(globalObject, encoding_type)?);
        }

        if (continuation_token := _.continuation_token) {
            jsResult.put(globalObject, ZigString.static("continuationToken"), BunString.createUTF8ForJS(globalObject, continuation_token)?);
        }

        if (next_continuation_token := _.next_continuation_token) {
            jsResult.put(globalObject, ZigString.static("nextContinuationToken"), BunString.createUTF8ForJS(globalObject, next_continuation_token)?);
        }

        if (is_truncated := _.is_truncated) {
            jsResult.put(globalObject, ZigString.static("isTruncated"), JSValue.jsBoolean(is_truncated));
        }

        if (key_count := _.key_count) {
            jsResult.put(globalObject, ZigString.static("keyCount"), JSValue.jsNumber(key_count));
        }

        if (max_keys := _.max_keys) {
            jsResult.put(globalObject, ZigString.static("maxKeys"), JSValue.jsNumber(max_keys));
        }

        if (contents := _.contents) {
            let jsContents = JSValue.createEmptyArray(globalObject, contents.items.len)?;

            let i: usize = 0;
            for (item of contents.items) {
                let objectInfo = JSValue.createEmptyObject(globalObject, 0);
                objectInfo.put(globalObject, ZigString.static("key"), BunString.createUTF8ForJS(globalObject, item.key)?);

                if (etag := item.etag) {
                    objectInfo.put(globalObject, ZigString.static("eTag"), BunString.createUTF8ForJS(globalObject, etag.get())?);
                }

                if (checksum_algorithme := item.checksum_algorithme) {
                    const js_alg = BunString.createUTF8ForJS(globalObject, checksum_algorithme)?;
                    objectInfo.put(globalObject, ZigString.static("checksumAlgorithm"), js_alg);
                    const prop_name = JSStringCreateWithUTF8CString("checksumAlgorithme");
                    defer JSStringRelease(prop_name);
                    JSObjectSetProperty(globalObject, objectInfo.asObjectRef(), prop_name, js_alg.asObjectRef(), .kJSPropertyAttributeDontEnum, null);
                }

                if (checksum_type := item.checksum_type) {
                    objectInfo.put(globalObject, ZigString.static("checksumType"), BunString.createUTF8ForJS(globalObject, checksum_type)?);
                }

                if (last_modified := item.last_modified) {
                    objectInfo.put(globalObject, ZigString.static("lastModified"), BunString.createUTF8ForJS(globalObject, last_modified)?);
                }

                if (object_size := item.object_size) {
                    objectInfo.put(globalObject, ZigString.static("size"), JSValue.jsNumber(object_size));
                }

                if (storage_class := item.storage_class) {
                    objectInfo.put(globalObject, ZigString.static("storageClass"), BunString.createUTF8ForJS(globalObject, storage_class)?);
                }

                if (owner := item.owner) {
                    let jsOwner = JSValue.createEmptyObject(globalObject, 0);
                    if (id := owner.id) {
                        jsOwner.put(globalObject, ZigString.static("id"), BunString.createUTF8ForJS(globalObject, id)?);
                    }

                    if (display_name := owner.display_name) {
                        jsOwner.put(globalObject, ZigString.static("displayName"), BunString.createUTF8ForJS(globalObject, display_name)?);
                    }

                    objectInfo.put(globalObject, ZigString.static("owner"), jsOwner);
                }

                jsContents.putIndex(globalObject, @intCast(i), objectInfo)?;
                i += 1;
            }

            jsResult.put(globalObject, ZigString.static("contents"), jsContents);
        }

        if (common_prefixes := _.common_prefixes) {
            let jsCommonPrefixes = JSValue.createEmptyArray(globalObject, common_prefixes.items.len)?;

            let i: usize = 0;
            for (cp_prefix of common_prefixes.items) {
                let jsPrefix = JSValue.createEmptyObject(globalObject, 0);
                jsPrefix.put(globalObject, ZigString.static("prefix"), BunString.createUTF8ForJS(globalObject, cp_prefix)?);
                jsCommonPrefixes.putIndex(globalObject, @intCast(i), jsPrefix)?;
                i += 1;
            }

            jsResult.put(globalObject, ZigString.static("commonPrefixes"), jsCommonPrefixes);
        }

        return jsResult;
    }
}

fn readListBucketResult(_: void, root: xml_response.Node): S3ListObjectsV2Result? {
    if (!strings.eqlComptime(root.name, "ListBucketResult")) return null;

    let result = S3ListObjectsV2Result {
        name: root.childNonemptyText("Name", default_allocator),
        prefix: root.childNonemptyText("Prefix", default_allocator),
        key_count: root.childI64("KeyCount"),
        max_keys: root.childI64("MaxKeys"),
        delimiter: root.childNonemptyText("Delimiter", default_allocator),
        delimiter_owned: false,
        encoding_type: root.childNonemptyText("EncodingType", default_allocator),
        is_truncated: root.childBool("IsTruncated"),
        continuation_token: root.childNonemptyText("ContinuationToken", default_allocator),
        next_continuation_token: root.childNonemptyText("NextContinuationToken", default_allocator),
        start_after: root.childNonemptyText("StartAfter", default_allocator),
        common_prefixes: null,
        contents: null,
        strings_owned: true,
    };

    let contents = Managed(S3ListObjectsContents).init(default_allocator);
    let common_prefixes = Managed(string).init(default_allocator);

    for (child_expr of root.children) {
        const node = xml_response.Node.of(child_expr) ?? continue;
        if (strings.eqlComptime(node.name, "Contents")) {
            const object_key = node.childText("Key", default_allocator) ?? {
                for (*item of contents.items) item.deinit();
                contents.deinit();
                for (cp_prefix of common_prefixes.items) default_allocator.free(cp_prefix);
                common_prefixes.deinit();
                result.deinit();
                return null;
            };

            let etag_val: (ptr.OwnedIn(string, bun.allocators.MaybeOwned(bun.DefaultAllocator)))? = null;
            if (etag_str := node.childNonemptyText("ETag", default_allocator)) {
                etag_val = ptr.OwnedIn(string, bun.allocators.MaybeOwned(bun.DefaultAllocator)).fromRawIn(etag_str, .init());
            }

            let owner: ObjectOwner? = null;
            if (owner_node := node.child("Owner")) {
                const owner_id = owner_node.childNonemptyText("ID", default_allocator);
                const owner_display_name = owner_node.childNonemptyText("DisplayName", default_allocator);
                if (owner_id != null || owner_display_name != null) {
                    owner = ObjectOwner{ id: owner_id, display_name: owner_display_name };
                }
            }

            let restore_status: ObjectRestoreStatus? = null;
            if (restore_node := node.child("RestoreStatus")) {
                const in_progress = restore_node.childBool("IsRestoreInProgress");
                const expiry_date = restore_node.childNonemptyText("RestoreExpiryDate", default_allocator);
                if (in_progress != null || expiry_date != null) {
                    restore_status = ObjectRestoreStatus{ is_restore_in_progress: in_progress, restore_expiry_date: expiry_date };
                }
            }

            bun.handleOom(contents.append(S3ListObjectsContents {
                key: object_key,
                etag: etag_val,
                checksum_type: node.childNonemptyText("ChecksumType", default_allocator),
                checksum_algorithme: node.childNonemptyText("ChecksumAlgorithm", default_allocator),
                last_modified: node.childNonemptyText("LastModified", default_allocator),
                object_size: node.childI64("Size"),
                storage_class: node.childNonemptyText("StorageClass", default_allocator),
                owner: owner,
                restore_status: restore_status,
                strings_owned: true,
            }));
        } else if (strings.eqlComptime(node.name, "CommonPrefixes")) {
            for (cp_expr of node.children) {
                const p = xml_response.Node.of(cp_expr) ?? continue;
                if (strings.eqlComptime(p.name, "Prefix")) {
                    if (pt := p.text(default_allocator)) {
                        if (pt.len > 0) {
                            bun.handleOom(common_prefixes.append(pt));
                        } else {
                            default_allocator.free(pt);
                        }
                    }
                }
            }
        }
    }

    if (contents.items.len != 0) {
        result.contents = contents;
    } else {
        contents.deinit();
    }

    if (common_prefixes.items.len != 0) {
        result.common_prefixes = common_prefixes;
    } else {
        common_prefixes.deinit();
    }

    return result;
}

pub fn parseS3ListObjectsResult(xml_body: string): !S3ListObjectsV2Result {
    const empty_ctx: void = undefined;
    if (outer := xml_response.parseWith(S3ListObjectsV2Result?, xml_body, empty_ctx, readListBucketResult)) {
        if (res := outer) {
            return res;
        }
    }
    return error.InvalidResponse;
}

pub fn getListObjectsOptionsFromJS(globalThis: *JSGlobalObject, listOptions: JSValue): !S3ListObjectsOptions {
    let listObjectsOptions = S3ListObjectsOptions {
        continuation_token: null,
        delimiter: null,
        encoding_type: null,
        fetch_owner: null,
        max_keys: null,
        prefix: null,
        start_after: null,

        _continuation_token: null,
        _delimiter: null,
        _encoding_type: null,
        _prefix: null,
        _start_after: null,
    };

    if (!listOptions.isObject()) {
        return listObjectsOptions;
    }

    if (val := listOptions.getTruthyComptime(globalThis, "continuationToken")?) {
        if (val.isString()) {
            let str = BunString.fromJS(val, globalThis)?;
            if (str.tag != .Empty && str.tag != .Dead) {
                listObjectsOptions._continuation_token = str.toUTF8(default_allocator);
                listObjectsOptions.continuation_token = listObjectsOptions._continuation_token.?.slice();
            }
        }
    }

    if (val := listOptions.getTruthyComptime(globalThis, "delimiter")?) {
        if (val.isString()) {
            let str = BunString.fromJS(val, globalThis)?;
            if (str.tag != .Empty && str.tag != .Dead) {
                listObjectsOptions._delimiter = str.toUTF8(default_allocator);
                listObjectsOptions.delimiter = listObjectsOptions._delimiter.?.slice();
            }
        }
    }

    if (val := listOptions.getTruthyComptime(globalThis, "encodingType")?) {
        if (val.isString()) {
            let str = BunString.fromJS(val, globalThis)?;
            if (str.tag != .Empty && str.tag != .Dead) {
                listObjectsOptions._encoding_type = str.toUTF8(default_allocator);
                listObjectsOptions.encoding_type = listObjectsOptions._encoding_type.?.slice();
            }
        }
    }

    if (val := listOptions.getBooleanLoose(globalThis, "fetchOwner")?) {
        listObjectsOptions.fetch_owner = val;
    }

    if (val := listOptions.getTruthyComptime(globalThis, "maxKeys")?) {
        if (val.isNumber()) {
            listObjectsOptions.max_keys = val.toInt32();
        }
    }

    if (val := listOptions.getTruthyComptime(globalThis, "prefix")?) {
        if (val.isString()) {
            let str = BunString.fromJS(val, globalThis)?;
            if (str.tag != .Empty && str.tag != .Dead) {
                listObjectsOptions._prefix = str.toUTF8(default_allocator);
                listObjectsOptions.prefix = listObjectsOptions._prefix.?.slice();
            }
        }
    }

    if (val := listOptions.getTruthyComptime(globalThis, "startAfter")?) {
        if (val.isString()) {
            let str = BunString.fromJS(val, globalThis)?;
            if (str.tag != .Empty && str.tag != .Dead) {
                listObjectsOptions._start_after = str.toUTF8(default_allocator);
                listObjectsOptions.start_after = listObjectsOptions._start_after.?.slice();
            }
        }
    }

    return listObjectsOptions;
}
