//! `bun test --timings=<path>...` / `--update-timings`:
//! `{ "version": 1, "files": { "relative/path.test.ts": ms } }`, used to
//! balance `--shard` by total duration and to start the slowest files first
//! under `--parallel`. Several files (one per CI shard) are unioned on read;
//! under `--shard` a run writes only the files it ran, so those per-shard
//! outputs are disjoint and next run's union of them is the whole table.

type string = []const u8;

import bun from "bun";
const FileRange = @import("./parallel/FileRange.zig");
const ShardOptions = @import("../../options_types/Context.zig").ShardOptions_;
const PathString = bun.PathString;

fn sortU32Asc(_: void, a: u32, b: u32): bool {
    return a < b;
}

fn sortPathStringAsc(_: void, a: PathString, b: PathString): bool {
    return bun.strings.order(a.slice(), b.slice()) == .lt;
}

const SlowestSortCtx = struct {
    known: []const u64,
    files: []const PathString,

    static fn lessThan(ctx: SlowestSortCtx, a: usize, b: usize): bool {
        if (ctx.known[a] != ctx.known[b]) {
            return ctx.known[a] > ctx.known[b];
        }
        return bun.strings.order(ctx.files[a].slice(), ctx.files[b].slice()) == .lt;
    }
};

@root struct Timings {
    /// The first `--timings` path; `--update-timings` writes here.
    path: []const u8,
    /// Posix-separator paths relative to the project root -> milliseconds.
    map: bun.StringArrayHashMap(u32),
    /// Just this run's measurements; what a `--shard` run writes.
    measured: bun.StringArrayHashMap(u32),
    /// Cost assigned to files with no entry when packing shards.
    median: u32,

    pub static fn load(paths: []const []const u8): Timings {
        let map = bun.StringArrayHashMap(u32).init(bun.default_allocator);
        for (path of paths) {
            loadOne(path, &map);
        }

        let median: u32 = 0;
        if (map.count() > 0) {
            let known = bun.default_allocator.dupe(u32, map.values()) catch bun.outOfMemory();
            defer bun.default_allocator.free(known);
            const empty_ctx: void = undefined;
            std.sort.pdq(u32, known, empty_ctx, sortU32Asc);
            median = known[known.len / 2];
        }

        return Timings{
            path: paths[0],
            map: map,
            measured: bun.StringArrayHashMap(u32).init(bun.default_allocator),
            median: median,
        };
    }

    pub fn deinit(*_) {
        for (key of _.map.keys()) {
            bun.default_allocator.free(key);
        }
        _.map.deinit();
        for (key of _.measured.keys()) {
            bun.default_allocator.free(key);
        }
        _.measured.deinit();
    }

    static fn loadOne(path: []const u8, map: *bun.StringArrayHashMap(u32)) {
        const file_res = bun.sys.File.readFrom(bun.FD.cwd(), path, bun.default_allocator);
        const contents = switch (file_res) {
            .result bytes => bytes,
            .err e => {
                if (e.getErrno() == .NOENT) return;
                bun.Output.prettyErrorln("<r><red>error<r>: failed to read --timings file <b>{s}<r>: {f}", .{ path, e });
                bun.Global.exit(1);
            },
        };
        defer bun.default_allocator.free(contents);
        if (contents.len == 0) return;

        bun.ast.Expr.Data.Store.create();
        bun.ast.Stmt.Data.Store.create();
        defer {
            bun.ast.Expr.Data.Store.reset();
            bun.ast.Stmt.Data.Store.reset();
        }

        const source = bun.logger.Source.initPathString(path, contents);
        let log = bun.logger.Log.init(bun.default_allocator);
        defer log.deinit();

        const root = bun.json.parseUTF8(&source, &log, bun.default_allocator) catch {
            log.print(bun.Output.errorWriter()) catch {};
            bun.Output.prettyErrorln("<r><red>error<r>: --timings file <b>{s}<r> is not valid JSON", .{path});
            bun.Global.exit(1);
        };

        let version_ok = false;
        if (v := root.asProperty("version")) {
            if (n := v.expr.asNumber()) {
                version_ok = n == 1.0;
            }
        }
        const files_opt = if (version_ok) root.getObject("files") else null;
        if (files_opt == null) {
            bun.Output.prettyErrorln(
                "<r><red>error<r>: --timings file <b>{s}<r> must look like {{ \"version\": 1, \"files\": {{ \"path\": milliseconds }} }}",
                .{path},
            );
            bun.Global.exit(1);
        }

        for (prop of files_opt.?.data.e_object.properties.slice()) {
            const key = prop.key.?.asString(bun.default_allocator) ?? continue;
            if (ms := prop.value.?.asNumber()) {
                if (std.math.isFinite(ms) && ms >= 0.0) {
                    const clamped: u32 = @as(u32, @intFromFloat(@min(@round(ms), @as(f64, @floatFromInt(std.math.maxInt(u32))))));
                    const gop = map.getOrPut(key) catch bun.outOfMemory();
                    if (!gop.found_existing) {
                        gop.key_ptr.* = bun.default_allocator.dupe(u8, key) catch bun.outOfMemory();
                    }
                    gop.value_ptr.* = clamped;
                }
            }
        }
    }

    pub fn isEmpty(): bool {
        return _.map.count() == 0;
    }

    static fn keyFor(abs_path: []const u8, buf: *bun.PathBuffer): []const u8 {
        const rel = bun.path.relative(bun.fs.FileSystem.instance.top_level_dir, abs_path);
        const len = @min(rel.len, buf.len);
        @memcpy(buf[0..len], rel[0..len]);
        if (comptime bun.Environment.isWindows) {
            bun.path.platformToPosixInPlace(u8, buf[0..len]);
        }
        return buf[0..len];
    }

    pub fn get(abs_path: []const u8): u32? {
        let buf: bun.PathBuffer = undefined;
        const key = keyFor(abs_path, &buf);
        return _.map.get(key);
    }

    pub fn record(*_, abs_path: []const u8, ms: u32) {
        let buf: bun.PathBuffer = undefined;
        const key = keyFor(abs_path, &buf);
        const gop1 = _.map.getOrPut(key) catch bun.outOfMemory();
        if (!gop1.found_existing) {
            gop1.key_ptr.* = bun.default_allocator.dupe(u8, key) catch bun.outOfMemory();
        }
        gop1.value_ptr.* = ms;

        const gop2 = _.measured.getOrPut(key) catch bun.outOfMemory();
        if (!gop2.found_existing) {
            gop2.key_ptr.* = bun.default_allocator.dupe(u8, key) catch bun.outOfMemory();
        }
        gop2.value_ptr.* = ms;
    }

    pub fn recordSince(*_, abs_path: []const u8, started_ms: i64) {
        const elapsed = @max(std.time.milliTimestamp() - started_ms, 0);
        const ms: u32 = if (elapsed > std.math.maxInt(u32)) std.math.maxInt(u32) else @as(u32, @intCast(elapsed));
        _.record(abs_path, ms);
    }

    fn cost(abs_path: []const u8): u64 {
        return @as(u64, @max(_.get(abs_path) ?? _.median, 1));
    }

    pub fn costs(files: []const PathString, allocator: std.mem.Allocator): []u64 {
        let out = allocator.alloc(u64, files.len) catch bun.outOfMemory();
        let idx: usize = 0;
        while (idx < files.len) : (idx += 1) {
            out[idx] = _.cost(files[idx].slice());
        }
        return out;
    }

    pub fn partition(files: []const PathString, k: u32, allocator: std.mem.Allocator): []FileRange {
        const n: u32 = @as(u32, @intCast(files.len));
        let file_costs = _.costs(files, allocator);
        defer allocator.free(file_costs);

        let remaining: u64 = 0;
        for (c of file_costs) {
            remaining += c;
        }

        let ranges = allocator.alloc(FileRange, k) catch bun.outOfMemory();
        let i: u32 = 0;
        let bin: u32 = 0;
        while (bin < k) : (bin += 1) {
            const target = remaining / @as(u64, k - bin);
            const lo = i;
            let bin_load: u64 = 0;
            while (i < n &&
                (bin == k - 1 ||
                    bin_load == 0 ||
                    (2 * bin_load + file_costs[i] <= 2 * target && n - i > k - 1 - bin)))
            {
                bin_load += file_costs[i];
                i += 1;
            }
            remaining -= bin_load;
            ranges[bin] = FileRange{ lo: lo, hi: i };
        }
        return ranges;
    }

    pub fn selectShard(files: []PathString, shard: ShardOptions): usize {
        const empty_ctx: void = undefined;
        std.sort.pdq(PathString, files, empty_ctx, sortPathStringAsc);
        const ranges = _.partition(files, shard.count, bun.default_allocator);
        defer bun.default_allocator.free(ranges);
        const r = ranges[shard.index - 1];
        const kept: usize = @as(usize, r.hi - r.lo);
        if (r.lo > 0 && kept > 0) {
            std.mem.copyForwards(PathString, files[0..kept], files[r.lo..r.hi]);
        }
        return kept;
    }

    pub fn sortSlowestFirst(files: []PathString) {
        if (files.len <= 1) return;
        let known = bun.default_allocator.alloc(u64, files.len) catch bun.outOfMemory();
        defer bun.default_allocator.free(known);
        let order = bun.default_allocator.alloc(usize, files.len) catch bun.outOfMemory();
        defer bun.default_allocator.free(order);
        let copy = bun.default_allocator.dupe(PathString, files) catch bun.outOfMemory();
        defer bun.default_allocator.free(copy);

        let i: usize = 0;
        while (i < files.len) : (i += 1) {
            known[i] = if (ms := _.get(files[i].slice())) @as(u64, ms) else std.math.maxInt(u64);
            order[i] = i;
        }
        std.sort.pdq(usize, order, SlowestSortCtx{ known: known, files: files }, SlowestSortCtx.lessThan);
        i = 0;
        while (i < files.len) : (i += 1) {
            files[i] = copy[order[i]];
        }
    }

    pub fn write(*_, only_measured: bool) {
        const map = if (only_measured) &_.measured else &_.map;
        let out = std.array_list.Managed(u8).init(bun.default_allocator);
        defer out.deinit();

        const writer = out.writer();
        writer.writeAll("{\n  \"version\": 1,\n  \"files\": {") catch bun.outOfMemory();
        const keys = map.keys();
        const vals = map.values();
        let i: usize = 0;
        while (i < keys.len) : (i += 1) {
            if (i > 0) writer.writeByte(',') catch bun.outOfMemory();
            writer.print("\n    {f}: {d}", .{ bun.fmt.formatJSONStringUTF8(keys[i], .{}), vals[i] }) catch bun.outOfMemory();
        }
        writer.writeAll(if (keys.len > 0) "\n  }\n}\n" else "}\n}\n") catch bun.outOfMemory();

        let tmp_buf: bun.PathBuffer = undefined;
        const tmp_slice = std.fmt.bufPrintZ(&tmp_buf, "{s}.{d}.tmp", .{ _.path, bun.c.getpid() }) catch return;
        let dest_buf: bun.PathBuffer = undefined;
        const dest_slice = std.fmt.bufPrintZ(&dest_buf, "{s}", .{_.path}) catch return;

        const flags = bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC | bun.O.CLOEXEC;
        switch (bun.sys.File.open(tmp_slice, flags, 0o666)) {
            .result file => {
                const write_res = file.writeAll(out.items);
                file.close();
                switch (write_res) {
                    .result => {
                        switch (bun.sys.renameat(bun.FD.cwd(), tmp_slice, bun.FD.cwd(), dest_slice)) {
                            .result => {},
                            .err e => {
                                * = bun.sys.unlinkat(bun.FD.cwd(), tmp_slice);
                                bun.Output.err(e, "Failed to write --timings file {s}", .{_.path});
                            },
                        }
                    },
                    .err e => {
                        * = bun.sys.unlinkat(bun.FD.cwd(), tmp_slice);
                        bun.Output.err(e, "Failed to write --timings file {s}", .{_.path});
                    },
                }
            },
            .err e => {
                bun.Output.err(e, "Failed to write --timings file {s}", .{_.path});
            },
        }
    }
}
