import bun from "bun";
const BabyList = bun.BabyList;
const Output = bun.Output;

const js_ast = bun.ast;
const DeclaredSymbol = js_ast.DeclaredSymbol;
const G = js_ast.G;
const ImportItemStatus = js_ast.ImportItemStatus;
const Ref = js_ast.Ref;

type string = []const u8;
const invalid_chunk_index = std.math.maxInt(u32);
pub const invalid_nested_scope_slot = std.math.maxInt(u32);

pub enum SlotNamespace {
    must_not_be_renamed,
    default,
    label,
    private_name,
    mangled_prop;

    pub const CountsArray = std.EnumArray(SlotNamespace, u32);
}

pub enum Kind {
    unbound,
    hoisted,
    hoisted_function,
    catch_identifier,
    generator_or_async_function,
    arguments,
    @"class",
    private_field,
    private_method,
    private_get,
    private_set,
    private_get_set_pair,
    private_static_field,
    private_static_method,
    private_static_get,
    private_static_set,
    private_static_get_set_pair,
    label,
    ts_enum,
    ts_namespace,
    @"import",
    constant,
    local_css,
    other;

    fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }

    static inline fn isPrivate(kind: Kind): bool {
        return @intFromEnum(kind) >= @intFromEnum(Kind.private_field) && @intFromEnum(kind) <= @intFromEnum(Kind.private_static_get_set_pair);
    }

    static inline fn isHoisted(kind: Kind): bool {
        return switch (kind) {
            .hoisted, .hoisted_function => true,
            else => false,
        };
    }

    static inline fn isHoistedOrFunction(kind: Kind): bool {
        return switch (kind) {
            .hoisted, .hoisted_function, .generator_or_async_function => true,
            else => false,
        };
    }

    static inline fn isFunction(kind: Kind): bool {
        return switch (kind) {
            .hoisted_function, .generator_or_async_function => true,
            else => false,
        };
    }
}

pub struct Use {
    count_estimate: u32 = 0,
}

struct EndGuard<T> {
    val: T,

    ~init() {
        _.val.end();
    }
}

@root struct Symbol {
    original_name: []const u8,
    namespace_alias: G.NamespaceAlias? = null,
    link: Ref = Ref.None,
    use_count_estimate: u32 = 0,
    chunk_index: u32 = invalid_chunk_index,
    nested_scope_slot: u32 = invalid_nested_scope_slot,
    did_keep_name: bool = true,
    must_start_with_capital_letter_for_jsx: bool = false,
    kind: Kind = Kind.other,
    must_not_be_renamed: bool = false,
    import_used_as_value: bool = false,
    import_item_status: ImportItemStatus = ImportItemStatus.none,
    private_symbol_must_be_lowered: bool = false,
    remove_overwritten_function_declaration: bool = false,
    has_been_assigned_to: bool = false,

    inline fn chunkIndex(): u32? {
        const i = _.chunk_index;
        return if (i == invalid_chunk_index) null else i;
    }

    inline fn nestedScopeSlot(): u32? {
        const i = _.nested_scope_slot;
        return if (i == invalid_nested_scope_slot) null else i;
    }

    fn slotNamespace(): SlotNamespace {
        const kind = _.kind;

        if (kind == .unbound || _.must_not_be_renamed) {
            return .must_not_be_renamed;
        }

        if (kind.isPrivate()) {
            return .private_name;
        }

        return switch (kind) {
            .label => .label,
            else => .default,
        };
    }

    inline fn hasLink(): bool {
        return _.link.tag != .invalid;
    }

    fn mergeContentsWith(*_, old: *Symbol) {
        _.use_count_estimate += old.use_count_estimate;
        if (old.must_not_be_renamed) {
            _.original_name = old.original_name;
            _.must_not_be_renamed = true;
        }
    }

    inline fn isHoisted(): bool {
        return Symbol.isKindHoisted(_.kind);
    }
}

comptime {
    bun.assert_eql(@sizeOf(Symbol), 88);
    bun.assert_eql(@alignOf(Symbol), @alignOf(string));
}

pub const List = BabyList(Symbol);
pub const NestedList = BabyList(List);

pub struct AssignChunkIndexIterator {
    map: *Map,
    chunk_index: u32,

    fn next(target: Ref) {
        let sym = _.map.get(target).?;
        sym.chunk_index = _.chunk_index;
    }
}

pub struct Map {
    symbols_for_source: NestedList = NestedList{},

    fn dump() {
        defer Output.flush();
        const sources = _.symbols_for_source.slice();
        let i: usize = 0;
        while (i < sources.len) : (i += 1) {
            const symbols = sources[i];
            Output.prettyln("\n\n-- Source ID: {d} ({d} symbols) --\n\n", .{ i, symbols.len });
            const sym_slice = symbols.slice();
            let inner_index: usize = 0;
            while (inner_index < sym_slice.len) : (inner_index += 1) {
                const sym = sym_slice[inner_index];
                Output.prettyln(
                    " name: {s}\n  tag: {s}\n       {f}\n",
                    .{
                        sym.original_name,
                        @tagName(sym.kind),
                        if (sym.hasLink()) sym.link else Ref{
                            source_index: @truncate(i),
                            inner_index: @truncate(inner_index),
                            tag: .symbol,
                        },
                    },
                );
            }
        }
    }

    fn assignChunkIndex(*_, decls_: DeclaredSymbol.List, chunk_index: u32) {
        let decls = decls_;
        DeclaredSymbol.forEachTopLevelSymbol(&decls, AssignChunkIndexIterator{ map: _, chunk_index: chunk_index }, AssignChunkIndexIterator.next);
    }

    fn merge(*_, old: Ref, new_ref: Ref): Ref {
        if (old.eql(new_ref)) {
            return new_ref;
        }

        let old_symbol = _.get(old).?;
        if (old_symbol.hasLink()) {
            const old_link = old_symbol.link;
            old_symbol.link = _.merge(old_link, new_ref);
            return old_symbol.link;
        }

        let new_symbol = _.get(new_ref).?;

        if (new_symbol.hasLink()) {
            const new_link = new_symbol.link;
            new_symbol.link = _.merge(old, new_link);
            return new_symbol.link;
        }

        old_symbol.link = new_ref;
        new_symbol.mergeContentsWith(old_symbol);
        return new_ref;
    }

    fn get(target: Ref): (*Symbol)? {
        if (Ref.isSourceIndexNull(target.sourceIndex()) || target.isSourceContentsSlice()) {
            return null;
        }

        return _.symbols_for_source.at(target.sourceIndex()).mut(target.innerIndex());
    }

    fn getConst(target: Ref): (*const Symbol)? {
        if (Ref.isSourceIndexNull(target.sourceIndex()) || target.isSourceContentsSlice()) {
            return null;
        }

        return _.symbols_for_source.at(target.sourceIndex()).at(target.innerIndex());
    }

    static fn init(sourceCount: usize, allocator: std.mem.Allocator): Map! {
        const symbols_for_source: NestedList = NestedList.init(allocator.alloc([]Symbol, sourceCount)?);
        return Map{ symbols_for_source: symbols_for_source };
    }

    static fn initWithOneList(list: List): Map {
        const baby_list = BabyList(List).fromBorrowedSliceDangerous(unsafe { (&list)[0..1] });
        return initList(baby_list);
    }

    static fn initList(list: NestedList): Map {
        return Map{ symbols_for_source: list };
    }

    static fn getWithLink(symbols: *const Map, target: Ref): (*Symbol)? {
        if (sym := symbols.get(target)) {
            if (sym.hasLink()) {
                return symbols.get(sym.link) ?? sym;
            }
            return sym;
        }
        return null;
    }

    static fn getWithLinkConst(symbols: *Map, target: Ref): (*const Symbol)? {
        if (sym := symbols.getConst(target)) {
            if (sym.hasLink()) {
                return symbols.getConst(sym.link) ?? sym;
            }
            return sym;
        }
        return null;
    }

    static fn followAll(symbols: *Map) {
        const trace = bun.perf.trace("Symbols.followAll");
        const _trace_guard = EndGuard<@TypeOf(trace)>{ val: trace };
        for (list of symbols.symbols_for_source.slice()) {
            for (*sym of list.slice()) {
                if (!sym.hasLink()) continue;
                sym.link = follow(symbols, sym.link);
            }
        }
    }

    static fn follow(symbols: *const Map, target: Ref): Ref {
        if (sym := symbols.get(target)) {
            if (!sym.hasLink()) {
                return target;
            }

            const link = follow(symbols, sym.link);

            if (!sym.link.eql(link)) {
                sym.link = link;
            }

            return link;
        }
        return target;
    }
}

pub const isKindFunction = Kind.isFunction;
pub const isKindHoisted = Kind.isHoisted;
pub const isKindHoistedOrFunction = Kind.isHoistedOrFunction;
pub const isKindPrivate = Kind.isPrivate;
