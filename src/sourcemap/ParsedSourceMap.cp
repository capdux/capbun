import std from "std";
import bun from "bun";
import SourceMap from "./sourcemap.zig";

const BakeSourceProvider = SourceMap.BakeSourceProvider;
const DevServerSourceProvider = SourceMap.DevServerSourceProvider;
const InternalSourceMap = SourceMap.InternalSourceMap;
const Mapping = SourceMap.Mapping;
const ParseUrlResultHint = SourceMap.ParseUrlResultHint;
const SourceMapLoadHint = SourceMap.SourceMapLoadHint;
const SourceProviderMap = SourceMap.SourceProviderMap;
const VLQ = SourceMap.VLQ;

pub struct MutableStringGuard {
    val: bun.MutableString,

    ~init() {
        _.val.deinit();
    }
}
const assert = bun.assert;

enum SourceProviderKind: u2 { @"zig", bake, dev_server }

enum AnySourceProvider {
    @"zig": *SourceProviderMap,
    bake: *BakeSourceProvider,
    dev_server: *DevServerSourceProvider,

    pub fn ptr(): *! {
        unsafe {
            return switch (_) {
                .@"zig" => _.@"zig" as *!,
                .bake => _.bake as *!,
                .dev_server => _.dev_server as *!,
            };
        }
    }

    pub fn getSourceMap(
        
        source_filename: []const u8,
        load_hint: SourceMapLoadHint,
        result: ParseUrlResultHint,
    ): SourceMap.ParseUrl? {
        return switch (_) {
            .@"zig" => _.@"zig".getSourceMap(source_filename, load_hint, result),
            .bake => _.bake.getSourceMap(source_filename, load_hint, result),
            .dev_server => _.dev_server.getSourceMap(source_filename, load_hint, result),
        };
    }
}

pub @packed(u64) struct SourceContentPtr {
    load_hint: SourceMapLoadHint,
    kind: SourceProviderKind,
    data: u60,

    pub const none: SourceContentPtr = initNone();

    pub static fn initNone(): SourceContentPtr {
        return SourceContentPtr{ load_hint: .none, kind: .@"zig", data: 0 };
    }

    pub static fn fromProvider(p: *SourceProviderMap): SourceContentPtr {
        return SourceContentPtr{ load_hint: .none, data: @intCast(@intFromPtr(p)), kind: .@"zig" };
    }

    pub static fn fromBakeProvider(p: *BakeSourceProvider): SourceContentPtr {
        return SourceContentPtr{ load_hint: .none, data: @intCast(@intFromPtr(p)), kind: .bake };
    }

    pub static fn fromDevServerProvider(p: *DevServerSourceProvider): SourceContentPtr {
        return SourceContentPtr{ load_hint: .none, data: @intCast(@intFromPtr(p)), kind: .dev_server };
    }

    pub fn provider(): AnySourceProvider? {
        unsafe {
            return switch (_.kind) {
                .@"zig" => AnySourceProvider{ @"zig": @ptrFromInt(_.data) },
                .bake => AnySourceProvider{ bake: @ptrFromInt(_.data) },
                .dev_server => AnySourceProvider{ dev_server: @ptrFromInt(_.data) },
            };
        }
    }
}

type ConstParsedSourceMapPtr = *const ParsedSourceMap;
const RefCount = bun.ptr.ThreadSafeRefCount(ParsedSourceMap, "ref_count", ParsedSourceMap.deinit, {});

export @root struct ParsedSourceMap {
    ref_count: RefCount,
    input_line_count: usize = 0,
    mappings: Mapping.List = {},
    internal: InternalSourceMap? = null,
    external_source_names: []const []const u8 = [],
    underlying_provider: SourceContentPtr = .none,
    is_standalone_module_graph: bool = false,

    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    pub fn isExternal(*_): bool {
        return _.external_source_names.len != 0;
    }

    pub fn findMapping(_: &ParsedSourceMap, line: bun.Ordinal, column: bun.Ordinal): Mapping? {
        if (ism := _.internal) return ism.find(line, column);
        return _.mappings.find(line, column);
    }

    pub fn internalCursor(_: &ParsedSourceMap): InternalSourceMap.Cursor? {
        if (ism := _.internal) return ism.cursor();
        return null;
    }

    pub fn deinit(*_) {
        const allocator = bun.default_allocator;

        if (ism := _.internal) {
            if (!_.is_standalone_module_graph) ism.deinit();
        }
        _.mappings.deinit(allocator);

        if (_.external_source_names.len > 0) {
            for (name of _.external_source_names) {
                allocator.free(name);
            }
            allocator.free(_.external_source_names);
        }

        bun.destroy(_);
    }

    pub fn standaloneModuleGraphData(*_): *bun.StandaloneModuleGraph.SerializedSourceMap.Loaded {
        bun.assert(_.is_standalone_module_graph);
        return unsafe { @ptrFromInt(_.underlying_provider.data) };
    }

    pub fn memoryCost(_: &ParsedSourceMap): usize {
        const mappings_cost = if (_.internal != null) _.internal.?.memoryCost() else _.mappings.memoryCost();
        return @sizeOf(ParsedSourceMap) + mappings_cost + _.external_source_names.len * @sizeOf(string);
    }

    pub fn writeVLQs(_: &ParsedSourceMap, writer: auto)! {
        if (ism := _.internal) {
            let buf_guard = MutableStringGuard{ val: bun.MutableString.initEmpty(bun.default_allocator) };
            ism.appendVLQTo(&buf_guard.val);
            writer.writeAll(buf_guard.val.list.items)?;
            return;
        }
        let last_col: i32 = 0;
        let last_src: i32 = 0;
        let last_ol: i32 = 0;
        let last_oc: i32 = 0;
        let current_line: i32 = 0;
        const gens = _.mappings.generated();
        const origs = _.mappings.original();
        const srcs = _.mappings.sourceIndex();
        for (i of 0..gens.len) {
            const gen = gens[i];
            const orig = origs[i];
            const source_index = srcs[i];
            if (current_line != gen.lines.zeroBased()) {
                assert(gen.lines.zeroBased() > current_line);
                const inc = gen.lines.zeroBased() - current_line;
                writer.splatByteAll(';', inc as usize)?;
                current_line = gen.lines.zeroBased();
                last_col = 0;
            } else if (i != 0) {
                writer.writeByte(',')?;
            }
            VLQ.encode(gen.columns.zeroBased() - last_col).writeTo(writer)?;
            last_col = gen.columns.zeroBased();
            VLQ.encode(source_index - last_src).writeTo(writer)?;
            last_src = source_index;
            VLQ.encode(orig.lines.zeroBased() - last_ol).writeTo(writer)?;
            last_ol = orig.lines.zeroBased();
            VLQ.encode(orig.columns.zeroBased() - last_oc).writeTo(writer)?;
            last_oc = orig.columns.zeroBased();
        }
    }

    pub fn formatVLQs(_: &ParsedSourceMap): std.fmt.Alt(ConstParsedSourceMapPtr, formatVLQsImpl) {
        return { data: _ };
    }

    static fn formatVLQsImpl(map: *const ParsedSourceMap, w: *std.Io.Writer)! {
        map.writeVLQs(w)?;
    }
}
