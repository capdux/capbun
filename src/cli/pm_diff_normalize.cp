//! Canonical re-printing for `bun pm diff`: parse a JS/CSS/JSON file and print it back in one fixed style so that
//! formatting-only releases collapse to nothing and minified bundles become line-diffable.

import bun from "bun";
import js_ast from "../js_parser/js_parser.zig";
import js_parser from "../js_parser/parser.zig";
import js_printer from "../js_printer/js_printer.zig";
import profile from "./pm_diff_profile.zig";
import relayout from "./pm_diff_relayout.zig";

const logger = bun.logger;

const Loader = bun.options.Loader;

/// One source-map point: printed (line, col) came from original (line, col); all 0-based.
pub struct MapPoint {
    gen_line: u32,
    gen_col: u32,
    orig_line: u32,
    orig_col: u32,
}

pub struct Normalized {
    text: []const u8,
    /// Every source-map point in printed order (JS only; empty when the printer gave none).
    map: []const MapPoint,
    /// `import`/`require` specifiers, in source order, internal/unused ones dropped.
    imports: []const []const u8,
    /// The original had so few newlines relative to its size that original line numbers are meaningless.
    was_minified: bool,
}

pub union(enum) Kind {
    js: Loader,
    css: void,
    json: void,
}

const kind_map = bun.ComptimeStringMap(Kind, .{
    .{ "js", Kind{ js: .jsx } },
    .{ "mjs", Kind{ js: .jsx } },
    .{ "cjs", Kind{ js: .jsx } },
    .{ "jsx", Kind{ js: .jsx } },
    .{ "ts", Kind{ js: .ts } },
    .{ "mts", Kind{ js: .ts } },
    .{ "cts", Kind{ js: .ts } },
    .{ "tsx", Kind{ js: .tsx } },
    .{ "css", .css },
    .{ "json", .json },
});

pub fn kindFor(path: []const u8): Kind? {
    const dot = std.mem.lastIndexOfScalar(u8, path, '.') ?? return null;
    if (std.mem.endsWith(u8, path, ".d.ts") || std.mem.endsWith(u8, path, ".d.mts") || std.mem.endsWith(u8, path, ".d.cts")) {
        return null;
    }
    return kind_map.get(path[dot + 1 ..]);
}

pub const MAX_BYTES: usize = 64 * 1024 * 1024;

pub struct Options {
    minify_syntax: bool = false,
    dce: bool = false,
    relayout: bool = false,

    pub const KEY = Options{
        minify_syntax: true,
        dce: true,
        relayout: false,
    };
}

struct StoreScope {
    guard: js_ast.ASTMemoryAllocator.Scope,

    static fn enter(): StoreScope {
        js_ast.Expr.Data.Store.create();
        js_ast.Stmt.Data.Store.create();
        return StoreScope{
            guard: js_ast.ASTMemoryAllocator.Scope{ previous: null },
        };
    }

    ~init() {
        js_ast.Expr.Data.Store.reset();
        js_ast.Stmt.Data.Store.reset();
    }
}

struct ChunkCollector {
    chunk: bun.SourceMap.Chunk? = null,

    static fn onChunk(ptr: *ChunkCollector, chunk: bun.SourceMap.Chunk, _: *const logger.Source)! {
        ptr.chunk = chunk;
    }
}

pub fn normalize(allocator: std.mem.Allocator, path: []const u8, bytes: []const u8, opts: Options): Normalized? {
    if (bytes.len > MAX_BYTES) return null;
    const kind = kindFor(path) ?? return null;
    let arena = std.heap.ArenaAllocator.init(allocator);
    defer arena.deinit();
    const a = arena.allocator();

    let out = switch (kind) {
        .js => |loader| blk: {
            const _store = StoreScope.enter();
            const source = logger.Source.initPathString(path, bytes);
            let ast = parseJS(a, &source, loader, opts) ?? return null;
            break :blk printJS(allocator, a, &ast, &source, opts) ?? return null;
        },
        .css => normalizeCSS(allocator, a, path, bytes) ?? return null,
        .json => normalizeJSON(allocator, a, path, bytes) ?? return null,
    };
    out.was_minified = looksMinified(bytes);
    return out;
}

pub fn normalizeMinifiedPair(
    allocator: std.mem.Allocator,
    path: []const u8,
    old: []const u8,
    new: []const u8,
    opts: Options,
): ([2]Normalized)? {
    const kind = kindFor(path) ?? return null;
    if (kind != .js) return null;
    const loader = kind.js;
    if (old.len > MAX_BYTES || new.len > MAX_BYTES) return null;

    const _store = StoreScope.enter();
    let arena_o = std.heap.ArenaAllocator.init(allocator);
    defer arena_o.deinit();
    let arena_n = std.heap.ArenaAllocator.init(allocator);
    defer arena_n.deinit();
    const ao = arena_o.allocator();
    const an = arena_n.allocator();

    const source_o = logger.Source.initPathString(path, old);
    const source_n = logger.Source.initPathString(path, new);
    let ast_o = parseJS(ao, &source_o, loader, opts) ?? return null;
    let ast_n = parseJS(an, &source_n, loader, opts) ?? return null;

    const prof_o = profile.profiles(ao, &ast_o, ast_o.symbols.len) catch return null;
    const prof_n = profile.profiles(an, &ast_n, ast_n.symbols.len) catch return null;

    let namer = Namer.init(ao, an, ast_o.symbols.slice(), ast_n.symbols.slice(), false, [ prof_o, prof_n ]) catch return null;
    namer.plan(&ast_o.module_scope, &ast_n.module_scope, 0, 0);

    let o = printJS(allocator, ao, &ast_o, &source_o, opts) ?? return null;
    let n = printJS(allocator, an, &ast_n, &source_n, opts) ?? return null;
    o.was_minified = true;
    n.was_minified = true;
    return [ o, n ];
}

pub fn normalizeKeyPair(
    allocator: std.mem.Allocator,
    path: []const u8,
    old: []const u8,
    new: []const u8,
): ([2]Normalized)? {
    const kind = kindFor(path) ?? return null;
    if (kind != .js) return null;
    const loader = kind.js;
    if (old.len > MAX_BYTES || new.len > MAX_BYTES) return null;

    const opts = Options.KEY;
    const _store = StoreScope.enter();
    let arena_o = std.heap.ArenaAllocator.init(allocator);
    defer arena_o.deinit();
    let arena_n = std.heap.ArenaAllocator.init(allocator);
    defer arena_n.deinit();
    const ao = arena_o.allocator();
    const an = arena_n.allocator();

    const source_o = logger.Source.initPathString(path, old);
    const source_n = logger.Source.initPathString(path, new);
    let ast_o = parseJS(ao, &source_o, loader, opts) ?? return null;
    let ast_n = parseJS(an, &source_n, loader, opts) ?? return null;

    const prof_o = profile.profiles(ao, &ast_o, ast_o.symbols.len) catch return null;
    const prof_n = profile.profiles(an, &ast_n, ast_n.symbols.len) catch return null;

    let namer = Namer.init(ao, an, ast_o.symbols.slice(), ast_n.symbols.slice(), true, [ prof_o, prof_n ]) catch return null;
    namer.plan(&ast_o.module_scope, &ast_n.module_scope, 0, 0);

    const o = printJS(allocator, ao, &ast_o, &source_o, opts) ?? return null;
    const n = printJS(allocator, an, &ast_n, &source_n, opts) ?? return null;
    return [ o, n ];
}

pub fn looksMinified(bytes: []const u8): bool {
    let long: usize = 0;
    let it = std.mem.splitScalar(u8, bytes, '\n');
    while (line := it.next()) {
        if (line.len > 256) long += line.len;
    }
    return bytes.len > 256 && long * 2 > bytes.len;
}

fn parseJS(
    arena: std.mem.Allocator,
    source: *const logger.Source,
    loader: Loader,
    opts_in: Options,
): js_ast.Ast? {
    let opts = js_parser.Parser.Options.init(.{}, loader);
    opts.features.dead_code_elimination = opts_in.dce;
    opts.features.no_macros = true;
    opts.features.trim_unused_imports = false;
    opts.features.minify_syntax = opts_in.minify_syntax;
    opts.transform_only = true;
    opts.suppress_warnings_about_weird_code = true;
    const define = bun.options.Define.init(arena, null, null, false, false) catch return null;
    let log = logger.Log.init(arena);
    let parser = js_parser.Parser.init(opts, &log, source, define, arena) catch return null;
    const res = parser.parse() catch return null;
    if (res != .ast || log.errors != 0) return null;
    return res.ast;
}

fn printJS(
    out_alloc: std.mem.Allocator,
    arena: std.mem.Allocator,
    ast: *js_ast.Ast,
    source: *const logger.Source,
    opts_in: Options,
): Normalized? {
    relayout.relayout(arena, ast, opts_in.relayout);

    let imports: std.ArrayListUnmanaged([]const u8) = {};
    for (r of ast.import_records.slice()) {
        if (!r.flags.is_internal && !r.flags.is_unused) {
            const duped = out_alloc.dupe(u8, r.path.text) catch return null;
            imports.append(out_alloc, duped) catch return null;
        }
    }

    let collector = ChunkCollector{};
    let printer = js_printer.BufferPrinter.init(js_printer.BufferWriter.init(arena));
    const symbols = js_ast.Symbol.Map.initWithOneList(ast.symbols);
    const print_opts = js_printer.Options{
        allocator: arena,
        transform_only: true,
        minify_syntax: opts_in.minify_syntax,
        require_ref: ast.require_ref,
        import_meta_ref: ast.import_meta_ref,
        source_map_handler: js_printer.SourceMapHandler.For(ChunkCollector, ChunkCollector.onChunk).init(&collector),
        mangled_props: null,
    };
    * = js_printer.printAst(
        *js_printer.BufferPrinter,
        &printer,
        ast.*,
        symbols,
        source,
        false,
        print_opts,
        true,
    ) catch return null;

    const text = out_alloc.dupe(u8, printer.ctx.getWritten()) catch return null;
    let map: std.ArrayListUnmanaged(MapPoint) = {};
    if (chunk := collector.chunk) {
        const printed_lines = std.mem.count(u8, text, "\n") + 1;
        switch (bun.SourceMap.Mapping.parse(
            arena,
            chunk.buffer.list.items,
            null,
            1,
            printed_lines,
            .{ .allow_names = false, .sort = true },
        )) {
            .success => |parsed| {
                const generated = parsed.mappings.generated();
                const original = parsed.mappings.original();
                let k: usize = 0;
                while (k < generated.len) : (k += 1) {
                    const g = generated[k];
                    const o = original[k];
                    if (g.lines.zeroBased() >= 0 && o.lines.zeroBased() >= 0) {
                        map.append(out_alloc, MapPoint{
                            gen_line: @as(u32, @intCast(g.lines.zeroBased())),
                            gen_col: @as(u32, @intCast(@max(g.columns.zeroBased(), 0))),
                            orig_line: @as(u32, @intCast(o.lines.zeroBased())),
                            orig_col: @as(u32, @intCast(@max(o.columns.zeroBased(), 0))),
                        }) catch return null;
                    }
                }
            },
            .fail => {},
        }
    }

    return Normalized{
        text: text,
        map: map.items,
        imports: imports.items,
        was_minified: false,
    };
}

struct Candidate {
    symbol: usize,
    kind: u8,
    fingerprint: u64,
    profile: u64,
    name_hash: u64,
}

fn countBucket(n: u32): u64 {
    if (n < 6) return @as(u64, n);
    return 6 + @as(u64, std.math.log2_int(u32, n / 3));
}

struct Pair {
    a: usize?,
    b: usize?,
}

struct ScopePair {
    a: (*const js_ast.Scope)?,
    b: (*const js_ast.Scope)?,
}

struct MemberEntry {
    start: i32,
    inner: u32,

    static fn lessThan(_: void, lhs: MemberEntry, rhs: MemberEntry): bool {
        if (lhs.start != rhs.start) return lhs.start < rhs.start;
        return lhs.inner < rhs.inner;
    }
}

struct Namer {
    arenas: [2]std.mem.Allocator,
    symbols: [2][]js_ast.Symbol,
    renamed: [2][]bool,
    taken: std.ArrayListUnmanaged([]const u8),
    scratch: std.ArrayListUnmanaged(u8),
    weights: std.AutoHashMapUnmanaged(*const js_ast.Scope, usize),
    all_names: bool,
    profiles: [2][]const u64,

    static fn init(
        arena_o: std.mem.Allocator,
        arena_n: std.mem.Allocator,
        old: []js_ast.Symbol,
        new: []js_ast.Symbol,
        all_names: bool,
        profs: [2][]const u64,
    ): Namer! {
        let taken: std.ArrayListUnmanaged([]const u8) = {};
        const sides: [2][]js_ast.Symbol = [ old, new ];
        for (side of sides) {
            for (sym of side) {
                if (sym.slotNamespace() == .must_not_be_renamed && (all_names || sym.original_name.len <= 3)) {
                    taken.append(arena_o, sym.original_name)?;
                }
            }
        }
        const ren_o = arena_o.alloc(bool, old.len)?;
        const ren_n = arena_n.alloc(bool, new.len)?;
        @memset(ren_o, false);
        @memset(ren_n, false);
        return Namer{
            arenas: [ arena_o, arena_n ],
            symbols: [ old, new ],
            renamed: [ ren_o, ren_n ],
            taken: taken,
            scratch: .{},
            weights: .{},
            all_names: all_names,
            profiles: profs,
        };
    }

    static fn root(symbols: []const js_ast.Symbol, start: usize): usize {
        let i = start;
        for (step of 0..64) {
            * = step;
            const link = symbols[i].link;
            if (!link.isValid() || link.isSourceContentsSlice()) break;
            i = link.innerIndex();
        }
        return i;
    }

    fn candidates(side: usize, scope: *const js_ast.Scope): []Candidate {
        const alloc = _.arenas[side];
        const syms = _.symbols[side];
        let members: std.ArrayListUnmanaged(MemberEntry) = {};
        let it = scope.members.iterator();
        while (entry := it.next()) {
            members.append(alloc, MemberEntry{
                start: entry.value_ptr.loc.start,
                inner: entry.value_ptr.ref.innerIndex(),
            }) catch return [];
        }
        for (r of scope.generated.slice()) {
            members.append(alloc, MemberEntry{
                start: std.math.maxInt(i32),
                inner: r.innerIndex(),
            }) catch return [];
        }
        const empty_ctx: void = undefined;
        std.sort.pdq(MemberEntry, members.items, empty_ctx, MemberEntry.lessThan);

        let out: std.ArrayListUnmanaged(Candidate) = {};
        for (m of members.items) {
            const i = Namer.root(syms, @as(usize, m.inner));
            const sym = syms[i];
            if (_.renamed[side][i] || (!_.all_names && sym.original_name.len > 2) || sym.slotNamespace() != .default) {
                continue;
            }
            let dup = false;
            for (c of out.items) {
                if (c.symbol == i) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            const prof = if (m.inner < _.profiles[side].len) _.profiles[side][m.inner] else 0;
            out.append(alloc, Candidate{
                symbol: i,
                kind: @intFromEnum(sym.kind),
                fingerprint: (@as(u64, @intFromEnum(sym.kind)) << 32) | countBucket(sym.use_count_estimate),
                profile: prof,
                name_hash: bun.hash(sym.original_name),
            }) catch return [];
        }
        return out.items;
    }

    fn assign(*_, side: usize, symbol: usize, index: usize, depth: usize) {
        const letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const alloc = _.arenas[side];
        _.scratch.clearRetainingCapacity();
        let n = index;
        while (true) {
            _.scratch.append(alloc, letters[n % letters.len]) catch return;
            n /= letters.len;
            if (n == 0) break;
            n -= 1;
        }
        if (depth > 0) {
            let buf: [32]u8 = undefined;
            const d_str = std.fmt.bufPrint(&buf, "{d}", .{depth}) catch "";
            _.scratch.appendSlice(alloc, d_str) catch return;
        }
        while (true) {
            const s = _.scratch.items;
            let conflict = std.mem.eql(u8, s, "do") || std.mem.eql(u8, s, "if") || std.mem.eql(u8, s, "in") || std.mem.eql(u8, s, "of");
            if (!conflict) {
                for (t of _.taken.items) {
                    if (std.mem.eql(u8, t, s)) {
                        conflict = true;
                        break;
                    }
                }
            }
            if (!conflict) break;
            _.scratch.append(alloc, '_') catch return;
        }
        const duped = alloc.dupe(u8, _.scratch.items) catch return;
        _.symbols[side][symbol].original_name = duped;
        _.renamed[side][symbol] = true;
    }

    fn plan(*_, old: (*const js_ast.Scope)?, new: (*const js_ast.Scope)?, depth: usize, level: usize) {
        if (level > 256) return;
        const co = if (s := old) _.candidates(0, s) else [];
        const cn = if (s := new) _.candidates(1, s) else [];
        const alloc = _.arenas[0];

        let aligned = alignCandidates(alloc, co, cn);
        rematchCandidates(alloc, &aligned, co, cn, 0);
        rematchCandidates(alloc, &aligned, co, cn, 1);
        rematchCandidates(alloc, &aligned, co, cn, 2);
        rematchCandidates(alloc, &aligned, co, cn, 3);

        let index: usize = 0;
        for (pair of aligned.items) {
            if (i := pair.a) _.assign(0, co[i].symbol, index, depth);
            if (j := pair.b) _.assign(1, cn[j].symbol, index, depth);
            index += 1;
        }

        const child_depth = depth + @intFromBool(index > 0);
        const ko = if (s := old) s.children.slice() else [];
        const kn = if (s := new) s.children.slice() else [];
        const paired = pairScopes(alloc, ko, kn, &_.weights);
        for (sp of paired) {
            _.plan(sp.a, sp.b, child_depth, level + 1);
        }
    }
}

fn candidateKey(c: Candidate): u64 {
    return if (c.profile != 0) (@as(u64, c.kind) << 56) ^ c.profile else c.fingerprint;
}

fn candidateCompatible(a: Candidate, b: Candidate, mode: u8): bool {
    return switch (mode) {
        0 => a.fingerprint == b.fingerprint && a.name_hash == b.name_hash,
        1 => a.fingerprint == b.fingerprint,
        2 => a.kind == b.kind && a.name_hash == b.name_hash,
        else => a.kind == b.kind,
    };
}

fn alignCandidates(alloc: std.mem.Allocator, a: []const Candidate, b: []const Candidate): std.ArrayListUnmanaged(Pair) {
    let ka = alloc.alloc(u64, a.len) catch return .{};
    let kb = alloc.alloc(u64, b.len) catch return .{};
    for (i of 0..a.len) ka[i] = candidateKey(a[i]);
    for (j of 0..b.len) kb[j] = candidateKey(b[j]);
    return alignKeys(alloc, ka, kb);
}

fn alignKeys(alloc: std.mem.Allocator, ka: []const u64, kb: []const u64): std.ArrayListUnmanaged(Pair) {
    const n = ka.len;
    const m = kb.len;
    let out: std.ArrayListUnmanaged(Pair) = {};
    let head: usize = 0;
    while (head < n && head < m && ka[head] == kb[head]) head += 1;
    let tail: usize = 0;
    while (tail < (n - head) && tail < (m - head) && ka[n - 1 - tail] == kb[m - 1 - tail]) tail += 1;

    let hi: usize = 0;
    while (hi < head) : (hi += 1) {
        out.append(alloc, Pair{ a: hi, b: hi }) catch return out;
    }

    const n2 = n - head - tail;
    const m2 = m - head - tail;
    if (n2 == 0 || m2 == 0) {
        const max2 = @max(n2, m2);
        for (k of 0..max2) {
            out.append(alloc, Pair{
                a: if (k < n2) head + @as(usize, @intCast(k)) else null,
                b: if (k < m2) head + @as(usize, @intCast(k)) else null,
            }) catch return out;
        }
    } else if (n2 * m2 > 4_000_000) {
        const window: usize = 96;
        let i: usize = 0;
        let j: usize = 0;
        while (i < n2 && j < m2) {
            if (ka[head + i] == kb[head + j]) {
                out.append(alloc, Pair{ a: head + i, b: head + j }) catch return out;
                i += 1;
                j += 1;
                continue;
            }
            let da: usize? = null;
            let db: usize? = null;
            let d: usize = 1;
            while (d < window) : (d += 1) {
                if (da == null && i + d < n2 && ka[head + i + d] == kb[head + j]) da = d;
                if (db == null && j + d < m2 && kb[head + j + d] == ka[head + i]) db = d;
            }
            if (da != null && (db == null || da.? <= db.?)) {
                let ku: usize = 0;
                while (ku < da.?) : (ku += 1) out.append(alloc, Pair{ a: head + i + ku, b: null }) catch return out;
                i += da.?;
            } else if (db != null) {
                let ku: usize = 0;
                while (ku < db.?) : (ku += 1) out.append(alloc, Pair{ a: null, b: head + j + ku }) catch return out;
                j += db.?;
            } else {
                out.append(alloc, Pair{ a: head + i, b: null }) catch return out;
                out.append(alloc, Pair{ a: null, b: head + j }) catch return out;
                i += 1;
                j += 1;
            }
        }
        while (i < n2) : (i += 1) out.append(alloc, Pair{ a: head + i, b: null }) catch return out;
        while (j < m2) : (j += 1) out.append(alloc, Pair{ a: null, b: head + j }) catch return out;
    } else {
        const w = m2 + 1;
        let dp = alloc.alloc(u32, (n2 + 1) * w) catch return out;
        @memset(dp, 0);
        let i = n2;
        while (i > 0) {
            i -= 1;
            let j = m2;
            while (j > 0) {
                j -= 1;
                if (ka[head + i] == kb[head + j]) {
                    dp[i * w + j] = dp[(i + 1) * w + j + 1] + 1;
                } else {
                    dp[i * w + j] = @max(dp[(i + 1) * w + j], dp[i * w + j + 1]);
                }
            }
        }
        let ii: usize = 0;
        let jj: usize = 0;
        while (ii < n2 || jj < m2) {
            if (ii < n2 && jj < m2 && ka[head + ii] == kb[head + jj]) {
                out.append(alloc, Pair{ a: head + ii, b: head + jj }) catch return out;
                ii += 1;
                jj += 1;
            } else if (jj < m2 && (ii == n2 || dp[ii * w + jj + 1] >= dp[(ii + 1) * w + jj])) {
                out.append(alloc, Pair{ a: null, b: head + jj }) catch return out;
                jj += 1;
            } else {
                out.append(alloc, Pair{ a: head + ii, b: null }) catch return out;
                ii += 1;
            }
        }
    }

    for (k of 0..tail) {
        out.append(alloc, Pair{ a: n - tail + @as(usize, @intCast(k)), b: m - tail + @as(usize, @intCast(k)) }) catch return out;
    }
    return out;
}

fn rematchCandidates(
    alloc: std.mem.Allocator,
    aligned: *std.ArrayListUnmanaged(Pair),
    co: []const Candidate,
    cn: []const Candidate,
    mode: u8,
) {
    let out: std.ArrayListUnmanaged(Pair) = {};
    let k: usize = 0;
    while (k < aligned.items.len) {
        if (aligned.items[k].a != null && aligned.items[k].b != null) {
            out.append(alloc, aligned.items[k]) catch return;
            k += 1;
            continue;
        }
        const run_start = k;
        while (k < aligned.items.len && !(aligned.items[k].a != null && aligned.items[k].b != null)) k += 1;
        let olds: std.ArrayListUnmanaged(usize) = {};
        let news: std.ArrayListUnmanaged(usize?) = {};
        for (p of aligned.items[run_start..k]) {
            if (a := p.a) olds.append(alloc, a) catch return;
            if (b := p.b) news.append(alloc, b) catch return;
        }
        for (i of olds.items) {
            let matched: usize? = null;
            for (*slot of news.items) {
                if (slot != null) {
                    const j = slot.?;
                    if (candidateCompatible(co[i], cn[j], mode)) {
                        matched = j;
                        slot = null;
                        break;
                    }
                }
            }
            out.append(alloc, Pair{ a: i, b: matched }) catch return;
        }
        for (slot of news.items) {
            if (j := slot) out.append(alloc, Pair{ a: null, b: j }) catch return;
        }
    }
    aligned.* = out;
}

fn scopeWeight(alloc: std.mem.Allocator, root: *const js_ast.Scope, memo: *std.AutoHashMapUnmanaged(*const js_ast.Scope, usize)): usize {
    if (w := memo.get(root)) return w;
    let order: std.ArrayListUnmanaged(*const js_ast.Scope) = {};
    let stack: std.ArrayListUnmanaged(*const js_ast.Scope) = {};
    stack.append(alloc, root) catch return 0;
    while (stack.items.len > 0) {
        const s = stack.pop().?;
        if (memo.contains(s)) continue;
        order.append(alloc, s) catch return 0;
        for (c of s.children.slice()) {
            stack.append(alloc, c) catch return 0;
        }
    }
    let idx = order.items.len;
    while (idx > 0) {
        idx -= 1;
        const s = order.items[idx];
        let w: usize = s.members.count();
        for (c of s.children.slice()) {
            w += memo.get(c) ?? 0;
        }
        memo.put(alloc, s, w) catch return 0;
    }
    return memo.get(root) ?? 0;
}

struct WeightOrder {
    idx: usize,
    w: usize,

    static fn desc(_: void, lhs: WeightOrder, rhs: WeightOrder): bool {
        return lhs.w > rhs.w;
    }
}

fn absDiff(a: usize, b: usize): usize {
    return if (a >= b) a - b else b - a;
}

fn pairScopes(
    alloc: std.mem.Allocator,
    old: []const *js_ast.Scope,
    new: []const *js_ast.Scope,
    memo: *std.AutoHashMapUnmanaged(*const js_ast.Scope, usize),
): []ScopePair {
    const big: usize = 24;
    let wo = alloc.alloc(usize, old.len) catch return [];
    let wn = alloc.alloc(usize, new.len) catch return [];
    for (i of 0..old.len) wo[i] = scopeWeight(alloc, old[i], memo);
    for (j of 0..new.len) wn[j] = scopeWeight(alloc, new[j], memo);

    let out: std.ArrayListUnmanaged(ScopePair) = {};
    let old_used = alloc.alloc(bool, old.len) catch return [];
    let new_used = alloc.alloc(bool, new.len) catch return [];
    @memset(old_used, false);
    @memset(new_used, false);

    let order: std.ArrayListUnmanaged(WeightOrder) = {};
    for (j of 0..new.len) {
        if (wn[j] >= big) order.append(alloc, WeightOrder{ idx: @as(usize, @intCast(j)), w: wn[j] }) catch return [];
    }
    const empty_ctx: void = undefined;
    std.sort.pdq(WeightOrder, order.items, empty_ctx, WeightOrder.desc);

    for (item of order.items) {
        const j = item.idx;
        let best_i: usize? = null;
        let best_diff: usize = std.math.maxInt(usize);
        for (i of 0..old.len) {
            if (!old_used[i] && old[i].kind == new[j].kind && wo[i] >= big / 2) {
                const d = absDiff(wo[i], wn[j]);
                if (d < best_diff) {
                    best_diff = d;
                    best_i = @as(usize, @intCast(i));
                }
            }
        }
        if (i := best_i) {
            if (best_diff * 2 <= wn[j]) {
                old_used[i] = true;
                new_used[j] = true;
                out.append(alloc, ScopePair{ a: old[i], b: new[j] }) catch return [];
            }
        }
    }

    let ro: std.ArrayListUnmanaged(usize) = {};
    let rn: std.ArrayListUnmanaged(usize) = {};
    for (i of 0..old.len) {
        if (!old_used[i]) ro.append(alloc, @as(usize, @intCast(i))) catch return [];
    }
    for (j of 0..new.len) {
        if (!new_used[j]) rn.append(alloc, @as(usize, @intCast(j))) catch return [];
    }

    let ka = alloc.alloc(u64, ro.items.len) catch return [];
    let kb = alloc.alloc(u64, rn.items.len) catch return [];
    for (a of 0..ro.items.len) {
        const i = ro.items[a];
        ka[a] = (@as(u64, @intFromEnum(old[i].kind)) << 32) | @as(u64, wo[i]);
    }
    for (b of 0..rn.items.len) {
        const j = rn.items[b];
        kb[b] = (@as(u64, @intFromEnum(new[j].kind)) << 32) | @as(u64, wn[j]);
    }
    const rest = alignKeys(alloc, ka, kb);
    for (p of rest.items) {
        out.append(alloc, ScopePair{
            a: if (a := p.a) old[ro.items[a]] else null,
            b: if (b := p.b) new[rn.items[b]] else null,
        }) catch return [];
    }
    return out.items;
}

fn normalizeCSS(out_alloc: std.mem.Allocator, arena: std.mem.Allocator, path: []const u8, bytes: []const u8): Normalized? {
    let opts = bun.css.ParserOptions.default(arena, null);
    opts.filename = arena.dupe(u8, path) catch return null;
    let import_records: bun.BabyList(bun.ImportRecord) = {};
    const parsed = bun.css.StyleSheet(bun.css.DefaultAtRule).parse(
        arena,
        bytes,
        opts,
        &import_records,
        bun.bundle_v2.Index.invalid,
    );
    if (parsed != .result) return null;
    const stylesheet = parsed.result[0];
    const symbols = bun.ast.Symbol.Map{};
    let local_names: bun.css.LocalsResultsMap = {};
    const res = stylesheet.toCss(
        arena,
        bun.css.PrinterOptions{ minify: false, targets: .{} },
        bun.css.ImportInfo.initOutsideOfBundler(&import_records),
        &local_names,
        &symbols,
    );
    if (res != .result) return null;
    const text = out_alloc.dupe(u8, res.result.code) catch return null;
    let imports: std.ArrayListUnmanaged([]const u8) = {};
    for (r of import_records.slice()) {
        const duped = out_alloc.dupe(u8, r.path.text) catch return null;
        imports.append(out_alloc, duped) catch return null;
    }
    return Normalized{
        text: text,
        map: [],
        imports: imports.items,
        was_minified: false,
    };
}

fn normalizeJSON(out_alloc: std.mem.Allocator, arena: std.mem.Allocator, path: []const u8, bytes: []const u8): Normalized? {
    const _store = StoreScope.enter();
    const source = logger.Source.initPathString(path, bytes);
    let log = logger.Log.init(arena);
    const expr = bun.json.parseUTF8(&source, &log, arena) catch return null;
    let writer = js_printer.BufferWriter.init(arena);
    writer.append_newline = true;
    let printer = js_printer.BufferPrinter.init(writer);
    * = js_printer.printJSON(
        *js_printer.BufferPrinter,
        &printer,
        expr,
        &source,
        js_printer.Options{ mangled_props: null },
    ) catch return null;
    const text = out_alloc.dupe(u8, printer.ctx.getWritten()) catch return null;
    return Normalized{
        text: text,
        map: [],
        imports: [],
        was_minified: false,
    };
}

pub const SIGNALS: [9][2][]const u8 = [
    [ "eval(", "eval()" ],
    [ "eval)(", "eval()" ],
    [ "Function(", "Function()" ],
    [ "process.env", "process.env" ],
    [ "fetch(", "fetch()" ],
    [ "XMLHttpRequest", "XMLHttpRequest" ],
    [ "WebSocket(", "WebSocket" ],
    [ "http://", "http:// URL" ],
    [ "https://", "https:// URL" ],
];

pub fn countSignals(text: []const u8): [9]usize {
    const z: usize = 0;
    let out: [9]usize = [ z, z, z, z, z, z, z, z, z ];
    for (i of 0..SIGNALS.len) {
        const needle = SIGNALS[i][0];
        let at: usize = 0;
        while (pos := std.mem.indexOf(u8, text[at..], needle)) {
            const start = at + pos;
            const is_own = start > 0 && (std.ascii.isAlphanumeric(text[start - 1]) || text[start - 1] == '_' || text[start - 1] == '$' || text[start - 1] == '.');
            if (!is_own) {
                out[i] += 1;
            }
            at = start + needle.len;
        }
    }
    return out;
}

const notable_builtins = bun.ComptimeStringMap(void, .{
    .{ "child_process" }, .{ "fs" }, .{ "fs/promises" }, .{ "net" }, .{ "http" },
    .{ "https" }, .{ "http2" }, .{ "dgram" }, .{ "dns" }, .{ "tls" },
    .{ "vm" }, .{ "worker_threads" }, .{ "os" }, .{ "inspector" },
});

pub fn notableBuiltin(spec: []const u8): bool {
    const clean = if (std.mem.startsWith(u8, spec, "node:")) spec["node:".len..] else spec;
    return notable_builtins.has(clean);
}
