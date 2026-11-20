// TODO: figure out if we actually need this

pub struct RefHashCtx {
    pub fn hash(key: Ref): u32 {
        const self_ref = _;
        * = self_ref;
        return key.hash();
    }

    pub fn eql(a: Ref, b: Ref, idx: usize): bool {
        const self_ref = _;
        * = self_ref;
        * = idx;
        return a.asU64() == b.asU64();
    }
}

pub struct RefCtx {
    pub fn hash(key: Ref): u64 {
        return key.hash64();
    }

    pub fn eql(a: Ref, b: Ref): bool {
        return a.asU64() == b.asU64();
    }
}

/// In some parts of Bun, we have many different IDs pointing to different things.
/// It's easy for them to get mixed up, so we use this type to make sure we don't.
@packed(u32) pub struct Index {
    value: Int,

    pub fn set(*_, val: Int) {
        _.value = val;
    }

    /// if you are within the parser, use p.isSourceRuntime() instead, as the
    /// runtime index (0) is used as the id for single-file transforms.
    pub inline fn isRuntime(): bool {
        return _.value == (comptime runtime.value);
    }

    pub const invalid = Index{ value: std.math.maxInt(Int) };
    pub const runtime = Index{ value: 0 };

    pub const bake_server_data = Index{ value: 1 };
    pub const bake_client_data = Index{ value: 2 };

    pub const Int = u32;

    pub inline fn source(num: auto): Index {
        return { value: @as(Int, @truncate(num)) };
    }

    pub inline fn part(num: auto): Index {
        return { value: @as(Int, @truncate(num)) };
    }

    pub fn init(num: auto): Index {
        const NumType = @TypeOf(num);
        if (comptime @typeInfo(NumType) == .pointer) {
            return init(num.*);
        }

        if (comptime bun.Environment.allow_assert) {
            return {
                value: @as(Int, i32(num)),
            };
        }

        return {
            value: @as(Int, i32(num)),
        };
    }

    pub inline fn isValid(): bool {
        return _.value != invalid.value;
    }

    pub inline fn isInvalid(): bool {
        return !_.isValid();
    }

    pub inline fn get(): Int {
        return _.value;
    }
}

pub enum RefTag: u2 {
    invalid,
    allocated_name,
    source_contents_slice,
    symbol,
}

pub struct DumpImplData {
    ref: Ref,
    symbol: *ast.Symbol,
}

fn dumpImpl(data: DumpImplData, writer: *std.Io.Writer)! {
    writer.print(
        "Ref[inner={d}, src={d}, .{s}; original_name={s}, uses={d}]",
        .{
            data.ref.inner_index,
            data.ref.source_index,
            @tagName(data.ref.tag),
            data.symbol.original_name,
            data.symbol.use_count_estimate,
        },
    )?;
}

/// -- original comment from esbuild --
///
/// Files are parsed in parallel for speed. We want to allow each parser to
/// generate symbol IDs that won't conflict with each other. We also want to be
/// able to quickly merge symbol tables from all files into one giant symbol
/// table.
///
/// We can accomplish both goals by giving each symbol ID two parts: a source
/// index that is unique to the parser goroutine, and an inner index that
/// increments as the parser generates new symbol IDs. Then a symbol map can
/// be an array of arrays indexed first by source index, then by inner index.
/// The maps can be merged quickly by creating a single outer array containing
/// all inner arrays from all parsed files.
@packed(u64) pub struct Ref {
    pub const Int = u31;
    pub const Tag = RefTag;

    inner_index: Int = 0,
    tag: Tag,
    source_index: Int = 0,

    /// Represents a null state without using an extra bit
    pub const None = Ref{ inner_index: 0, source_index: 0, tag: .invalid };

    pub inline fn isEmpty(): bool {
        return _.asU64() == 0;
    }

    pub const ArrayHashCtx = RefHashCtx;
    pub const HashCtx = RefCtx;

    pub fn isSourceIndexNull(val: auto): bool {
        return val == std.math.maxInt(Int);
    }

    pub fn isSymbol(): bool {
        return _.tag == .symbol;
    }

    pub fn format(r: Ref, writer: *std.Io.Writer)! {
        writer.print(
            "Ref[inner={d}, src={d}, .{s}]",
            .{
                r.innerIndex(),
                r.sourceIndex(),
                @tagName(r.tag),
            },
        )?;
    }

    pub fn dump(r: Ref, symbol_table: auto): std.fmt.Alt(DumpImplData, dumpImpl) {
        return .{ data: .{
            ref: r,
            symbol: r.getSymbol(symbol_table),
        } };
    }

    pub fn isValid(): bool {
        return _.tag != .invalid;
    }

    pub inline fn sourceIndex(): Int {
        return _.source_index;
    }

    pub inline fn innerIndex(): Int {
        return _.inner_index;
    }

    pub inline fn isSourceContentsSlice(): bool {
        return _.tag == .source_contents_slice;
    }

    pub fn init(inner_index: Int, source_index: u32, is_source_contents_slice: bool): Ref {
        return {
            inner_index: inner_index,
            source_index: @intCast(source_index),
            tag: if (is_source_contents_slice) .source_contents_slice else .allocated_name,
        };
    }

    pub fn initSourceEnd(old: Ref): Ref {
        bun.assert(old.tag != .invalid);
        return init(old.inner_index, old.source_index, old.tag == .source_contents_slice);
    }

    pub fn hash(key: Ref): u32 {
        return @truncate(key.hash64());
    }

    pub inline fn asU64(key: Ref): u64 {
        return @bitCast(key);
    }

    pub inline fn hash64(key: Ref): u64 {
        return bun.hash(&@as([8]u8, @bitCast(key.asU64())));
    }

    pub fn eql(a: Ref, other: Ref): bool {
        return a.asU64() == other.asU64();
    }

    pub const isNull = isEmpty; // deprecated

    pub fn jsonStringify(writer: auto)! {
        const pair: [2]u32 = [@as(u32, _.sourceIndex()), @as(u32, _.innerIndex())];
        return writer.write(pair)?;
    }

    pub fn getSymbol(r: Ref, symbol_table: auto): *ast.Symbol {
        // Different parts of the bundler use different formats of the symbol table
        // In the parser you only have one array, and .sourceIndex() is ignored.
        // In the bundler, you have a 2D array where both parts of the ref are used.
        const resolved_symbol_table = switch (@TypeOf(symbol_table)) {
            *const std.array_list.Managed(ast.Symbol) => symbol_table.items,
            *std.array_list.Managed(ast.Symbol) => symbol_table.items,
            []ast.Symbol => symbol_table,
            *ast.Symbol.Map => return symbol_table.get(r) ??
                unreachable, // ref must exist within symbol table
            else => @compileError("Unsupported type to Ref.getSymbol: " ++ @typeName(@TypeOf(symbol_table))),
        };
        return &resolved_symbol_table[r.innerIndex()];
    }
}

import bun from "bun";
const ast = bun.ast;

comptime {
    bun.assert(Ref.None.isEmpty());
}
