// JS testing bindings for `InternalSourceMap`. Keeps `src/sourcemap/` free of JSC types.
import bun from "bun";

const MutableString = bun.MutableString;

pub static struct TestingAPIs {
    pub fn fromVLQ(globalThis: *bun.jsc.JSGlobalObject, callframe: *bun.jsc.CallFrame): bun.jsc.JSValue ! bun.JSError {
        const vlq_str = callframe.argument(0).toBunString(globalThis)?;
        const vlq = vlq_str.toUTF8(bun.default_allocator);
        const blob = bun.SourceMap.InternalSourceMap.fromVLQ(bun.default_allocator, vlq.slice(), 0) catch {
            vlq.deinit();
            vlq_str.deref();
            return globalThis.throw("InternalSourceMap.fromVLQ: invalid VLQ input", {});
        };
        vlq.deinit();
        vlq_str.deref();
        const res = bun.jsc.ArrayBuffer.createUint8Array(globalThis, blob);
        bun.default_allocator.free(blob);
        return res;
    }

    pub fn toVLQ(globalThis: *bun.jsc.JSGlobalObject, callframe: *bun.jsc.CallFrame): bun.jsc.JSValue ! bun.JSError {
        const ab_opt = callframe.argument(0).asArrayBuffer(globalThis);
        if (ab_opt == null) {
            return globalThis.throw("InternalSourceMap.toVLQ: expected Uint8Array", {});
        }
        const ab = ab_opt.?;
        if (!bun.SourceMap.InternalSourceMap.isValidBlob(ab.byteSlice())) return globalThis.throw("InternalSourceMap.toVLQ: invalid blob", {});
        const ism = bun.SourceMap.InternalSourceMap{ data: ab.byteSlice().ptr };
        let out: MutableString = MutableString.initEmpty(bun.default_allocator);
        ism.appendVLQTo(&out);
        const res = bun.String.createUTF8ForJS(globalThis, out.list.items);
        out.deinit();
        return res;
    }

    pub fn find(globalThis: *bun.jsc.JSGlobalObject, callframe: *bun.jsc.CallFrame): bun.jsc.JSValue ! bun.JSError {
        const ab_opt = callframe.argument(0).asArrayBuffer(globalThis);
        if (ab_opt == null) {
            return globalThis.throw("InternalSourceMap.find: expected Uint8Array", {});
        }
        const ab = ab_opt.?;
        if (!bun.SourceMap.InternalSourceMap.isValidBlob(ab.byteSlice())) return globalThis.throw("InternalSourceMap.find: invalid blob", {});
        const line = callframe.argument(1).toInt32();
        const col = callframe.argument(2).toInt32();
        if (line < 0 || col < 0) return bun.jsc.JSValue.@"null";
        const max_ord: i32 = std.math.maxInt(i32) - 1;
        const safe_line: i32 = if (line > max_ord) max_ord else line;
        const safe_col: i32 = if (col > max_ord) max_ord else col;
        const ism = bun.SourceMap.InternalSourceMap{ data: ab.byteSlice().ptr };
        const mapping_opt = ism.find(.fromZeroBased(safe_line), .fromZeroBased(safe_col));
        if (mapping_opt == null) {
            return bun.jsc.JSValue.@"null";
        }
        const mapping = mapping_opt.?;
        const obj = bun.jsc.JSValue.createEmptyObject(globalThis, 5);
        obj.put(globalThis, bun.jsc.ZigString.static("generatedLine"), .jsNumber(mapping.generated.lines.zeroBased()));
        obj.put(globalThis, bun.jsc.ZigString.static("generatedColumn"), .jsNumber(mapping.generated.columns.zeroBased()));
        obj.put(globalThis, bun.jsc.ZigString.static("originalLine"), .jsNumber(mapping.original.lines.zeroBased()));
        obj.put(globalThis, bun.jsc.ZigString.static("originalColumn"), .jsNumber(mapping.original.columns.zeroBased()));
        obj.put(globalThis, bun.jsc.ZigString.static("sourceIndex"), .jsNumber(mapping.source_index));
        return obj;
    }
}
