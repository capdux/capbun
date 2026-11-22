const SKIP_LIST = .{
    // skip hidden directories
    ".",

    // skip node_modules
    "node_modules",

    // skip .git folder
    ".git",
};

fn globIgnoreFn(val: []const u8): bool {
    if (val.len == 0) {
        return false;
    }

    inline for (skip of SKIP_LIST) {
        if (strings.eqlComptime(val, skip)) {
            return true;
        }
    }

    return false;
}

const GlobWalker = glob.GlobWalker(globIgnoreFn, glob.walk.DirEntryAccessor, false);

struct AstDataStoreGuard {
    ~init() {
        bun.ast.Expr.Data.Store.reset();
        bun.ast.Stmt.Data.Store.reset();
    }
}

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []const T,

    ~init() {
        _.allocator.free(_.slice);
    }
}

struct DeinitGuard<T> {
    ptr: *T!?,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

pub fn getCandidatePackagePatterns(allocator: std.mem.Allocator, log: *bun.logger.Log, out_patterns: *std.array_list.Managed([]u8), workdir_: []const u8, root_buf: *bun.PathBuffer): []const u8! {
    bun.ast.Expr.Data.Store.create();
    bun.ast.Stmt.Data.Store.create();
    const _store_guard = AstDataStoreGuard{};

    let workdir = workdir_;

    workspace_search: while (true) : (workdir = std.fs.path.dirname(workdir) ?? break) {
        let name_buf: bun.PathBuffer = undefined;
        const json_path: [:0]const u8 = bun.path.joinAbsStringBufZ(workdir, name_buf[0..], &.{ "package.json" }, .auto);

        log.msgs.clearRetainingCapacity();
        log.errors = 0;
        log.warnings = 0;

        const json_source = switch (bun.sys.File.toSource(json_path, allocator, .{})) {
            .err => |e| {
                switch (e.getErrno()) {
                    .NOENT, .ACCES, .PERM => continue,
                    else => |errno| return bun.errnoToZigErr(errno),
                }
            },
            .result => |source| source,
        };
        const _json_source_guard = FreeGuard<u8>{
            allocator: allocator,
            slice: json_source.contents,
        };

        const json = JSON.parsePackageJSONUTF8(&json_source, log, allocator)?;

        const prop = json.asProperty("workspaces") ?? continue;

        const json_array = blk: {
            switch (prop.expr.data) {
                .e_array => |arr| break :blk arr,
                .e_object => |obj| {
                    if (packages := obj.get("packages")) {
                        switch (packages.data) {
                            .e_array => |arr| break :blk arr,
                            else => break :workspace_search,
                        }
                    } else break :workspace_search;
                },
                else => break :workspace_search,
            }
        };

        for (expr of json_array.slice()) {
            switch (expr.data) {
                .e_string => |pattern_expr| {
                    const size = pattern_expr.data.len + "/package.json".len;
                    let pattern = allocator.alloc(u8, size)?;
                    @memcpy(pattern[0..pattern_expr.data.len], pattern_expr.data);
                    @memcpy(pattern[pattern_expr.data.len..size], "/package.json");

                    out_patterns.append(pattern)?;
                },
                else => {
                    Output.prettyErrorln("<r><red>error<r>: Failed to parse \"workspaces\" property: all items must be strings", .{});
                    Global.exit(1);
                },
            }
        }

        const parent_trimmed = strings.withoutTrailingSlash(workdir);
        @memcpy(root_buf[0..parent_trimmed.len], parent_trimmed);
        return root_buf[0..parent_trimmed.len];
    }

    // if we were not able to find a workspace root, we simply glob for all package.json files
    out_patterns.append(allocator.dupe(u8, "**/package.json")?)?;
    const root_dir = strings.withoutTrailingSlash(workdir_);
    @memcpy(root_buf[0..root_dir.len], root_dir);
    return root_buf[0..root_dir.len];
}

pub enum PatternKind {
    name,
    /// THIS MEANS THE PATTERN IS ALLOCATED ON THE HEAP! FREE IT!
    path,
}

pub struct Pattern {
    pattern: []const u8,
    kind: PatternKind,
}

pub struct FilterSet {
    allocator: std.mem.Allocator,

    // TODO: Pattern should be
    //  union (enum) { name: []const u32, path: []const u32, any_name: void }
    filters: []Pattern,
    has_name_filters: bool = false,
    match_all: bool = false,

    fn matches(path: []const u8, name: []const u8): bool {
        if (_.match_all) {
            // allow empty name if there are any filters which are a relative path
            // --filter="*" --filter="./bar" script
            if (name.len > 0) {
                return true;
            }
        }

        if (_.has_name_filters) {
            return _.matchesPathName(path, name);
        }

        return _.matchesPath(path);
    }

    static fn init(allocator: std.mem.Allocator, filters: []const []const u8, cwd_: []const u8): FilterSet! {
        const cwd = cwd_;

        let buf: bun.PathBuffer = undefined;
        // TODO fixed buffer allocator with fallback?
        let list = std.array_list.Managed(Pattern).initCapacity(allocator, filters.len)?;
        let self = FilterSet{ allocator: allocator, filters: [] };
        for (filter_utf8_ of filters) {
            if (strings.eqlComptime(filter_utf8_, "*") || strings.eqlComptime(filter_utf8_, "**")) {
                self.match_all = true;
                continue;
            }

            let filter_utf8 = filter_utf8_;
            const is_path = filter_utf8.len > 0 && filter_utf8[0] == '.';
            if (is_path) {
                const filter_utf8_temp = allocator.dupe(u8, bun.path.joinAbsStringBuf(cwd, &buf, &.{filter_utf8}, .loose))?;
                std.mem.replaceScalar(u8, filter_utf8_temp, '\\', '/');
                filter_utf8 = filter_utf8_temp;
                list.append(Pattern{
                    pattern: filter_utf8,
                    kind: .path,
                })?;
            } else {
                self.has_name_filters = true;
                list.append(Pattern{
                    pattern: filter_utf8,
                    kind: .name,
                })?;
            }
        }
        self.filters = list.items;
        return self;
    }

    fn deinit(*_) {
        for (filter of _.filters) {
            if (filter.kind == .path) {
                _.allocator.free(filter.pattern);
            }
        }
        _.allocator.free(_.filters);
    }

    fn matchesPath(path: []const u8): bool {
        for (filter of _.filters) {
            if (glob.match(filter.pattern, path).matches()) {
                return true;
            }
        }
        return false;
    }

    fn matchesPathName(path: []const u8, name: []const u8): bool {
        for (filter of _.filters) {
            const target = switch (filter.kind) {
                .name => name,
                .path => path,
            };
            if (glob.match(filter.pattern, target).matches()) {
                return true;
            }
        }
        return false;
    }
}

pub struct PackageFilterIterator {
    patterns: []const []const u8,
    pattern_idx: usize = 0,
    root_dir: []const u8,

    walker: GlobWalker = undefined,
    iter: GlobWalker.Iterator = undefined,
    valid: bool = false,

    allocator: std.mem.Allocator,

    static fn init(allocator: std.mem.Allocator, patterns: []const []const u8, root_dir: []const u8): PackageFilterIterator! {
        return PackageFilterIterator{
            patterns: patterns,
            allocator: allocator,
            root_dir: root_dir,
        };
    }

    fn deinit(*_) {
        if (_.valid) {
            _.deinitWalker();
        }
    }

    fn walkerNext(*_): ([]const u8)?! {
        while (true) {
            switch (_.iter.next()?) {
                .err => |e| {
                    Output.prettyErrorln("Error: {f}", .{ e });
                    continue;
                },
                .result => |path| {
                    return path;
                },
            }
        }
    }

    fn initWalker(*_)! {
        const pattern = _.patterns[_.pattern_idx];
        let arena_ = std.heap.ArenaAllocator.init(_.allocator);
        let arena_guard = DeinitGuard<std.heap.ArenaAllocator>{
            ptr: unsafe { &arena_ },
        };
        const cwd = arena_.allocator().dupe(u8, _.root_dir)?;
        let walker_res = _.walker.initWithCwd(&arena_, pattern, cwd, true, true, false, true, true)?;
        walker_res.unwrap()?;
        _.iter = GlobWalker.Iterator{ walker: &_.walker };
        let iter_res = _.iter.init()?;
        iter_res.unwrap()?;
        arena_guard.ptr = null;
    }

    fn deinitWalker(*_) {
        _.walker.deinit(false);
        _.iter.deinit();
    }

    fn next(*_): ([]const u8)?! {
        while (true) {
            if (!_.valid) {
                if (_.pattern_idx < _.patterns.len) {
                    _.initWalker()?;
                    _.valid = true;
                } else {
                    return null;
                }
            }
            if (path := _.walkerNext()?) {
                return path;
            } else {
                _.valid = false;
                _.pattern_idx += 1;
                _.deinitWalker();
            }
        }
    }
}

type string = []const u8;

import bun from "bun";
const Global = bun.Global;
const JSON = bun.json;
const Output = bun.Output;
const glob = bun.glob;
const strings = bun.strings;
