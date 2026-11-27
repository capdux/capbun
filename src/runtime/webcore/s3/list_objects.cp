import bun from "bun";
import { default_allocator, ptr, strings, String, JSError } from "bun";
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

    pub fn deinit(*_) {
        if (*etag := _.etag) etag.deinit();
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
    owned_strings: Managed(string)? = null;

    pub fn deinit(*_) {
        if (_.delimiter_owned) {
            if (d := _.delimiter) default_allocator.free(d);
        }
        if (owned := _.owned_strings) {
            for (s of owned.items) default_allocator.free(s);
            owned.deinit();
        }
        if (contents := _.contents) {
            for (*item of contents.items) item.deinit();
            contents.deinit();
        }
        if (common_prefixes := _.common_prefixes) {
            common_prefixes.deinit();
        }
    }

    pub fn toJS(*_, globalObject: *JSGlobalObject): JSError!JSValue {
        let jsResult = JSValue.createEmptyObject(globalObject, 12);

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
                let objectInfo = JSValue.createEmptyObject(globalObject, 8);
                objectInfo.put(globalObject, ZigString.static("key"), BunString.createUTF8ForJS(globalObject, item.key)?);

                if (etag := item.etag) {
                    objectInfo.put(globalObject, ZigString.static("eTag"), BunString.createUTF8ForJS(globalObject, etag.get())?);
                }

                if (checksum_algorithme := item.checksum_algorithme) {
                    const js_alg = BunString.createUTF8ForJS(globalObject, checksum_algorithme)?;
                    objectInfo.put(globalObject, ZigString.static("checksumAlgorithm"), js_alg);
                    const prop_name = JSStringCreateWithUTF8CString("checksumAlgorithme");
                    defer JSStringRelease(prop_name);
                    JSObjectSetProperty(globalObject, objectInfo.asObjectRef(), prop_name, js_alg.asRef(), .kJSPropertyAttributeDontEnum, null);
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
                    let jsOwner = JSValue.createEmptyObject(globalObject, 2);
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
                let jsPrefix = JSValue.createEmptyObject(globalObject, 1);
                jsPrefix.put(globalObject, ZigString.static("prefix"), BunString.createUTF8ForJS(globalObject, cp_prefix)?);
                jsCommonPrefixes.putIndex(globalObject, @intCast(i), jsPrefix)?;
                i += 1;
            }

            jsResult.put(globalObject, ZigString.static("commonPrefixes"), jsCommonPrefixes);
        }

        return jsResult;
    }
}

struct DecodedText {
    text: string,
    owned: bool,
}

fn appendDecodedEntities(out: *Managed(u8), raw: string) {
    let i: usize = 0;
    while (i < raw.len) {
        if (amp := strings.indexOfChar(raw[i..], '&')) {
            if (amp > 0) { bun.handleOom(out.appendSlice(raw[i .. i + amp])); i += amp; }
        } else { bun.handleOom(out.appendSlice(raw[i..])); return; }
        if (semi := strings.indexOfChar(raw[i + 1 ..], ';')) {
            const ent = raw[i + 1 .. i + 1 + semi];
            let ch: u8 = 0;
            if (strings.eqlComptime(ent, "amp")) { ch = '&'; }
            else if (strings.eqlComptime(ent, "lt")) { ch = '<'; }
            else if (strings.eqlComptime(ent, "gt")) { ch = '>'; }
            else if (strings.eqlComptime(ent, "quot")) { ch = '"'; }
            else if (strings.eqlComptime(ent, "apos")) { ch = '\''; }
            if (ch != 0) { bun.handleOom(out.append(ch)); i += semi + 2; continue; }
            if (ent.len >= 2 && ent[0] == '#') {
                const is_hex = ent.len >= 3 && (ent[1] == 'x' || ent[1] == 'X');
                const parsed_cp = std.fmt.parseInt(u21, if (is_hex) ent[2..] else ent[1..], if (is_hex) 16 else 10) catch null;
                if (parsed_cp != null) {
                    let utf8_buf: [4]u8 = undefined;
                    const encoded_len = std.unicode.utf8Encode(parsed_cp.?, &utf8_buf) catch null;
                    if (encoded_len != null) { bun.handleOom(out.appendSlice(utf8_buf[0..encoded_len.?])); i += semi + 2; continue; }
                }
            }
        }
        bun.handleOom(out.append('&'));
        i += 1;
    }
}

struct XmlCursor {
    xml: string,
    pos: usize = 0,

    fn skipWs(*_) {
        while (_.pos < _.xml.len && (_.xml[_.pos] == ' ' || _.xml[_.pos] == '\t' || _.xml[_.pos] == '\r' || _.xml[_.pos] == '\n')) _.pos += 1;
    }

    fn skipComment(*_): !void {
        if (end_idx := strings.indexOf(_.xml[_.pos..], "-->")) { _.pos += end_idx + 3; } else return error.InvalidResponse;
    }

    fn skipProlog(*_): !void {
        while (true) {
            _.skipWs();
            if (_.pos >= _.xml.len) break;
            if (strings.startsWith(_.xml[_.pos..], "<?")) {
                if (end_idx := strings.indexOf(_.xml[_.pos + 2 ..], "?>")) { _.pos += 4 + end_idx; } else return error.InvalidResponse;
            } else if (strings.startsWith(_.xml[_.pos..], "<!--")) {
                _.pos += 4;
                _.skipComment()?;
            } else if (strings.startsWith(_.xml[_.pos..], "<!DOCTYPE")) {
                if (end_idx := strings.indexOfChar(_.xml[_.pos + 9 ..], '>')) { _.pos += 10 + end_idx; } else return error.InvalidResponse;
            } else break;
        }
    }

    fn readTagName(*_): !string {
        const start = _.pos;
        while (_.pos < _.xml.len) {
            const c = _.xml[_.pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '>' || c == '/') break;
            if (c == '<') return error.InvalidResponse;
            _.pos += 1;
        }
        if (_.pos == start) return error.InvalidResponse;
        return _.xml[start.._.pos];
    }

    fn skipAttributes(*_): !bool {
        let quote: u8 = 0;
        while (_.pos < _.xml.len) {
            const c = _.xml[_.pos];
            if (quote != 0) { if (c == quote) quote = 0; _.pos += 1; }
            else if (c == '"' || c == '\'') { quote = c; _.pos += 1; }
            else if (c == '>') { _.pos += 1; return false; }
            else if (c == '/' && _.pos + 1 < _.xml.len && _.xml[_.pos + 1] == '>') { _.pos += 2; return true; }
            else if (c == '<') return error.InvalidResponse
            else _.pos += 1;
        }
        return error.InvalidResponse;
    }

    fn readClosingTag(*_, expected: string): !void {
        if (!strings.eql(_.readTagName()?, expected)) return error.InvalidResponse;
        _.skipWs();
        if (_.pos >= _.xml.len || _.xml[_.pos] != '>') return error.InvalidResponse;
        _.pos += 1;
    }

    fn readLeafText(*_, tag_name: string): !DecodedText {
        const start = _.pos;
        let has_amp = false;
        let has_special = false;
        while (_.pos < _.xml.len) {
            const c = _.xml[_.pos];
            if (c == '&') {
                has_amp = true;
                _.pos += 1;
            } else if (c == '<') {
                if (_.pos + 1 < _.xml.len && _.xml[_.pos + 1] == '/') {
                    const end = _.pos;
                    _.pos += 2;
                    _.readClosingTag(tag_name)?;
                    if (!has_special) {
                        const raw = _.xml[start..end];
                        if (!has_amp) return DecodedText{ text: raw, owned: false };
                        let out = Managed(u8).init(default_allocator);
                        appendDecodedEntities(&out, raw);
                        return DecodedText{ text: bun.handleOom(out.toOwnedSlice()), owned: true };
                    }
                    break;
                } else if (strings.startsWith(_.xml[_.pos..], "<![CDATA[")) {
                    has_special = true;
                    break;
                } else if (strings.startsWith(_.xml[_.pos..], "<!--")) {
                    has_special = true;
                    break;
                } else {
                    return error.InvalidResponse;
                }
            } else {
                _.pos += 1;
            }
        }
        if (!has_special) return error.InvalidResponse;

        _.pos = start;
        let out = Managed(u8).init(default_allocator);
        errdefer out.deinit();
        while (_.pos < _.xml.len) {
            if (strings.startsWith(_.xml[_.pos..], "<![CDATA[")) {
                _.pos += 9;
                if (end_idx := strings.indexOf(_.xml[_.pos..], "]]>")) {
                    bun.handleOom(out.appendSlice(_.xml[_.pos .. _.pos + end_idx]));
                    _.pos += end_idx + 3;
                } else {
                    return error.InvalidResponse;
                }
            } else if (strings.startsWith(_.xml[_.pos..], "<!--")) {
                _.pos += 4;
                _.skipComment()?;
            } else if (strings.startsWith(_.xml[_.pos..], "</")) {
                _.pos += 2;
                _.readClosingTag(tag_name)?;
                return DecodedText{ text: bun.handleOom(out.toOwnedSlice()), owned: true };
            } else if (_.xml[_.pos] == '<') {
                return error.InvalidResponse;
            } else {
                const seg_start = _.pos;
                while (_.pos < _.xml.len && _.xml[_.pos] != '<') {
                    _.pos += 1;
                }
                appendDecodedEntities(&out, _.xml[seg_start.._.pos]);
            }
        }
        return error.InvalidResponse;
    }

    fn skipElement(*_, tag_name: string): !void {
        while (_.pos < _.xml.len) {
            if (_.xml[_.pos] != '<') {
                _.pos += 1;
                continue;
            }
            if (strings.startsWith(_.xml[_.pos..], "<!--")) {
                _.pos += 4;
                _.skipComment()?;
            } else if (strings.startsWith(_.xml[_.pos..], "<![CDATA[")) {
                _.pos += 9;
                if (end_idx := strings.indexOf(_.xml[_.pos..], "]]>")) {
                    _.pos += end_idx + 3;
                } else {
                    return error.InvalidResponse;
                }
            } else if (strings.startsWith(_.xml[_.pos..], "</")) {
                _.pos += 2;
                _.readClosingTag(tag_name)?;
                return;
            } else {
                _.pos += 1;
                const child_name = _.readTagName()?;
                const self_closing = _.skipAttributes()?;
                if (!self_closing) {
                    _.skipElement(child_name)?;
                }
            }
        }
        return error.InvalidResponse;
    }
    fn nextTag(*_, parent_tag: string): !TagEvent? {
        while (_.pos < _.xml.len) {
            _.skipWs();
            if (_.pos >= _.xml.len || _.xml[_.pos] != '<') return error.InvalidResponse;
            if (strings.startsWith(_.xml[_.pos..], "<!--")) {
                _.pos += 4;
                _.skipComment()?;
                continue;
            }
            if (strings.startsWith(_.xml[_.pos..], "</")) {
                _.pos += 2;
                _.readClosingTag(parent_tag)?;
                return null;
            }
            _.pos += 1;
            const name = _.readTagName()?;
            const self_closing = _.skipAttributes()?;
            return TagEvent{ name: name, self_closing: self_closing };
        }
        return error.InvalidResponse;
    }
}

struct TagEvent {
    name: string,
    self_closing: bool,
}

fn keepDecoded(owned_strings: *Managed(string), dt: DecodedText): string {
    if (dt.owned) bun.handleOom(owned_strings.append(dt.text));
    return dt.text;
}

fn readOptionalNonempty(cursor: *XmlCursor, tag: string, owned_strings: *Managed(string)): !string? {
    const dt = cursor.readLeafText(tag)?;
    if (dt.text.len > 0) return keepDecoded(owned_strings, dt);
    if (dt.owned) default_allocator.free(dt.text);
    return null;
}

fn readBoolText(cursor: *XmlCursor, tag: string): !bool? {
    const dt = cursor.readLeafText(tag)?;
    defer if (dt.owned) default_allocator.free(dt.text);
    const trimmed = strings.trim(dt.text, " \t\r\n");
    if (strings.eqlComptime(trimmed, "true")) return true;
    if (strings.eqlComptime(trimmed, "false")) return false;
    return null;
}

fn readIntText(cursor: *XmlCursor, tag: string): !i64? {
    const dt = cursor.readLeafText(tag)?;
    defer if (dt.owned) default_allocator.free(dt.text);
    return std.fmt.parseInt(i64, strings.trim(dt.text, " \t\r\n"), 10) catch null;
}

fn readOwner(cursor: *XmlCursor, owned_strings: *Managed(string)): !ObjectOwner? {
    let owner_id: string? = null;
    let owner_display_name: string? = null;
    while (ev := cursor.nextTag("Owner")?) {
        if (ev.self_closing) continue;
        if (strings.eqlComptime(ev.name, "ID")) {
            if (v := readOptionalNonempty(cursor, ev.name, owned_strings)?) owner_id = v;
        } else if (strings.eqlComptime(ev.name, "DisplayName")) {
            if (v := readOptionalNonempty(cursor, ev.name, owned_strings)?) owner_display_name = v;
        } else {
            cursor.skipElement(ev.name)?;
        }
    }
    if (owner_id != null || owner_display_name != null) {
        return ObjectOwner{ id: owner_id, display_name: owner_display_name };
    }
    return null;
}

fn readRestoreStatus(cursor: *XmlCursor, owned_strings: *Managed(string)): !ObjectRestoreStatus? {
    let in_progress: bool? = null;
    let expiry_date: string? = null;
    while (ev := cursor.nextTag("RestoreStatus")?) {
        if (ev.self_closing) continue;
        if (strings.eqlComptime(ev.name, "IsRestoreInProgress")) {
            if (v := readBoolText(cursor, ev.name)?) in_progress = v;
        } else if (strings.eqlComptime(ev.name, "RestoreExpiryDate")) {
            if (v := readOptionalNonempty(cursor, ev.name, owned_strings)?) expiry_date = v;
        } else {
            cursor.skipElement(ev.name)?;
        }
    }
    if (in_progress != null || expiry_date != null) {
        return ObjectRestoreStatus{ is_restore_in_progress: in_progress, restore_expiry_date: expiry_date };
    }
    return null;
}

fn readContentsItem(cursor: *XmlCursor, owned_strings: *Managed(string)): !S3ListObjectsContents {
    let has_key = false;
    let key: string = "";
    let etag: ptr.OwnedIn(string, bun.allocators.MaybeOwned(bun.DefaultAllocator))? = null;
    let checksum_type: string? = null;
    let checksum_algorithme: string? = null;
    let last_modified: string? = null;
    let object_size: i64? = null;
    let storage_class: string? = null;
    let owner: ObjectOwner? = null;
    let restore_status: ObjectRestoreStatus? = null;
    errdefer if (*e_val := etag) e_val.deinit();

    while (ev := cursor.nextTag("Contents")?) {
        if (ev.self_closing) {
            if (strings.eqlComptime(ev.name, "Key")) { has_key = true; key = ""; }
            else if (strings.eqlComptime(ev.name, "ETag")) { if (*prev_e := etag) prev_e.deinit(); etag = ptr.OwnedIn(string, bun.allocators.MaybeOwned(bun.DefaultAllocator)).fromRawIn("", .initBorrowed()); }
            else if (strings.eqlComptime(ev.name, "LastModified")) last_modified = ""
            else if (strings.eqlComptime(ev.name, "StorageClass")) storage_class = ""
            else if (strings.eqlComptime(ev.name, "ChecksumType")) checksum_type = ""
            else if (strings.eqlComptime(ev.name, "ChecksumAlgorithm")) checksum_algorithme = "";
            continue;
        }
        if (strings.eqlComptime(ev.name, "Key")) { key = keepDecoded(owned_strings, cursor.readLeafText(ev.name)?); has_key = true; }
        else if (strings.eqlComptime(ev.name, "ETag")) {
            const dt = cursor.readLeafText(ev.name)?;
            if (*prev_e := etag) prev_e.deinit();
            etag = if (dt.owned) ptr.OwnedIn(string, bun.allocators.MaybeOwned(bun.DefaultAllocator)).fromRawIn(dt.text, .init()) else ptr.OwnedIn(string, bun.allocators.MaybeOwned(bun.DefaultAllocator)).fromRawIn(dt.text, .initBorrowed());
        } else if (strings.eqlComptime(ev.name, "LastModified")) last_modified = keepDecoded(owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "StorageClass")) storage_class = keepDecoded(owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "ChecksumType")) checksum_type = keepDecoded(owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "ChecksumAlgorithm")) checksum_algorithme = keepDecoded(owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "Size")) object_size = readIntText(cursor, ev.name)?
        else if (strings.eqlComptime(ev.name, "Owner")) owner = readOwner(cursor, owned_strings)?
        else if (strings.eqlComptime(ev.name, "RestoreStatus")) restore_status = readRestoreStatus(cursor, owned_strings)?
        else cursor.skipElement(ev.name)?;
    }
    if (!has_key) return error.InvalidResponse;
    return S3ListObjectsContents {
        key: key,
        etag: etag,
        checksum_type: checksum_type,
        checksum_algorithme: checksum_algorithme,
        last_modified: last_modified,
        object_size: object_size,
        storage_class: storage_class,
        owner: owner,
        restore_status: restore_status,
    };
}

pub fn parseS3ListObjectsResult(xml_body: string): !S3ListObjectsV2Result {
    let cursor = XmlCursor{ xml: xml_body, pos: 0 };
    cursor.skipProlog()?;
    if (cursor.pos >= cursor.xml.len || cursor.xml[cursor.pos] != '<') return error.InvalidResponse;
    cursor.pos += 1;
    const root_name = cursor.readTagName()?;
    if (!strings.eqlComptime(root_name, "ListBucketResult")) return error.InvalidResponse;
    const root_self_closing = cursor.skipAttributes()?;

    let result = S3ListObjectsV2Result {};
    if (root_self_closing) {
        cursor.skipProlog()?;
        if (cursor.pos != cursor.xml.len) return error.InvalidResponse;
        return result;
    }

    let contents = Managed(S3ListObjectsContents).init(default_allocator);
    let common_prefixes = Managed(string).init(default_allocator);
    let owned_strings = Managed(string).init(default_allocator);
    errdefer {
        for (*item of contents.items) item.deinit();
        contents.deinit();
        common_prefixes.deinit();
        for (s of owned_strings.items) default_allocator.free(s);
        owned_strings.deinit();
    }

    while (ev := cursor.nextTag("ListBucketResult")?) {
        if (ev.self_closing) {
            if (strings.eqlComptime(ev.name, "Contents")) return error.InvalidResponse;
            if (strings.eqlComptime(ev.name, "Name")) result.name = "";
            if (strings.eqlComptime(ev.name, "Delimiter")) result.delimiter = "";
            if (strings.eqlComptime(ev.name, "EncodingType")) result.encoding_type = "";
            if (strings.eqlComptime(ev.name, "ContinuationToken")) result.continuation_token = "";
            if (strings.eqlComptime(ev.name, "NextContinuationToken")) result.next_continuation_token = "";
            if (strings.eqlComptime(ev.name, "StartAfter")) result.start_after = "";
            continue;
        }
        if (strings.eqlComptime(ev.name, "Contents")) {
            bun.handleOom(contents.append(readContentsItem(&cursor, &owned_strings)?));
        } else if (strings.eqlComptime(ev.name, "CommonPrefixes")) {
            while (cp_ev := cursor.nextTag("CommonPrefixes")?) {
                if (cp_ev.self_closing) continue;
                if (strings.eqlComptime(cp_ev.name, "Prefix")) {
                    if (v := readOptionalNonempty(&cursor, cp_ev.name, &owned_strings)?) {
                        bun.handleOom(common_prefixes.append(v));
                    }
                } else {
                    cursor.skipElement(cp_ev.name)?;
                }
            }
        } else if (strings.eqlComptime(ev.name, "Name")) result.name = keepDecoded(&owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "Prefix")) { if (v := readOptionalNonempty(&cursor, ev.name, &owned_strings)?) result.prefix = v; }
        else if (strings.eqlComptime(ev.name, "Delimiter")) result.delimiter = keepDecoded(&owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "EncodingType")) result.encoding_type = keepDecoded(&owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "ContinuationToken")) result.continuation_token = keepDecoded(&owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "NextContinuationToken")) result.next_continuation_token = keepDecoded(&owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "StartAfter")) result.start_after = keepDecoded(&owned_strings, cursor.readLeafText(ev.name)?)
        else if (strings.eqlComptime(ev.name, "KeyCount")) result.key_count = readIntText(&cursor, ev.name)?
        else if (strings.eqlComptime(ev.name, "MaxKeys")) result.max_keys = readIntText(&cursor, ev.name)?
        else if (strings.eqlComptime(ev.name, "IsTruncated")) result.is_truncated = readBoolText(&cursor, ev.name)?
        else cursor.skipElement(ev.name)?;
    }

    cursor.skipProlog()?;
    if (cursor.pos != cursor.xml.len) return error.InvalidResponse;

    if (contents.items.len != 0) result.contents = contents else contents.deinit();
    if (common_prefixes.items.len != 0) result.common_prefixes = common_prefixes else common_prefixes.deinit();
    if (owned_strings.items.len != 0) result.owned_strings = owned_strings else owned_strings.deinit();

    return result;
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
