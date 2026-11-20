pub const DataURL = @import("./data_url.zig").DataURL;
import DirInfo from "./dir_info.zig";

const debuglog = Output.scoped(.Resolver, .hidden);

pub fn isPackagePath(path: string): bool {
    // Always check for posix absolute paths (starts with "/")
    // But don't check window's style on posix
    // For a more in depth explanation, look above where `isPackagePathNotAbsolute` is used.
    return !std.fs.path.isAbsolute(path) && @call(bun.callmod_inline, isPackagePathNotAbsolute, .{path});
}

pub fn isPackagePathNotAbsolute(non_absolute_path: string): bool {
    if (Environment.allow_assert) {
        assert(!std.fs.path.isAbsolute(non_absolute_path));
        assert(!strings.startsWith(non_absolute_path, "/"));
    }

    return !strings.startsWith(non_absolute_path, "./") &&
        !strings.startsWith(non_absolute_path, "../") &&
        !strings.eql(non_absolute_path, ".") &&
        !strings.eql(non_absolute_path, "..") && if (Environment.isWindows)
        (!strings.startsWith(non_absolute_path, ".\\") &&
            !strings.startsWith(non_absolute_path, "..\\"))
    else
        true;
}

pub struct SideEffectsData {
    source: *logger.Source,
    range: logger.Range,

    // If true, "sideEffects" was an array. If false, "sideEffects" was false.
    is_side_effects_array_in_json: bool = false,
}

/// A temporary threadlocal buffer with a lifetime more than the current
/// function call.
///
/// These used to be individual `threadlocal var x: bun.PathBuffer = undefined`
/// declarations. On Windows each `PathBuffer` is 96 KB (vs 4 KB on POSIX) and
/// PE/COFF has no TLS-BSS, so 25 of them here cost ~2.5 MB of raw zeros in
/// bun.exe and in every thread's TLS block. Grouping them behind a lazily
/// allocated pointer brings that down to 8 bytes. See `bun.ThreadlocalBuffers`.
///
/// Experimenting with making this one struct instead of a bunch of different
/// threadlocal vars yielded no performance improvement on macOS when bundling
/// 10 copies of Three.js. Potentially revisit after https://github.com/oven-sh/bun/issues/2716
const WIN32_CACHE_LEN = bun.MAX_PATH_BYTES * 2;

struct Bufs {
    extension_path: bun.PathBuffer = undefined,
    tsconfig_match_full_buf: bun.PathBuffer = undefined,
    tsconfig_match_full_buf2: bun.PathBuffer = undefined,
    tsconfig_match_full_buf3: bun.PathBuffer = undefined,

    esm_subpath: [512]u8 = undefined,
    esm_absolute_package_path: bun.PathBuffer = undefined,
    esm_absolute_package_path_joined: bun.PathBuffer = undefined,

    dir_entry_paths_to_resolve: [256]DirEntryResolveQueueItem = undefined,
    open_dirs: [256]FD = undefined,
    resolve_without_remapping: bun.PathBuffer = undefined,
    index: bun.PathBuffer = undefined,
    dir_info_uncached_filename: bun.PathBuffer = undefined,
    node_bin_path: bun.PathBuffer = undefined,
    dir_info_uncached_path: bun.PathBuffer = undefined,
    tsconfig_base_url: bun.PathBuffer = undefined,
    relative_abs_path: bun.PathBuffer = undefined,
    load_as_file_or_directory_via_tsconfig_base_path: bun.PathBuffer = undefined,
    node_modules_check: bun.PathBuffer = undefined,
    field_abs_path: bun.PathBuffer = undefined,
    tsconfig_path_abs: bun.PathBuffer = undefined,
    check_browser_map: bun.PathBuffer = undefined,
    remap_path: bun.PathBuffer = undefined,
    load_as_file: bun.PathBuffer = undefined,
    remap_path_trailing_slash: bun.PathBuffer = undefined,
    path_in_global_disk_cache: bun.PathBuffer = undefined,
    abs_to_rel: bun.PathBuffer = undefined,
    node_modules_paths_buf: bun.PathBuffer = undefined,
    import_path_for_standalone_module_graph: bun.PathBuffer = undefined,

    win32_normalized_dir_info_cache: if (Environment.isWindows) [WIN32_CACHE_LEN]u8 else void = undefined,
}

const bufs_storage = bun.ThreadlocalBuffers(Bufs);
const css_extension_order: [1]string = [".css"];

inline fn bufs(comptime field: std.@"meta".FieldEnum(Bufs)): *@FieldType(Bufs, @tagName(field)) {
    return &@field(bufs_storage.get(), @tagName(field));
}

pub struct PathPairIter {
    index: u2,
    ctx: *PathPair,
    fn next(*_): (*Path)? {
        if (path_ := _.next_()) {
            if (path_.is_disabled) {
                return _.next();
            }
            return path_;
        }

        return null;
    }
    fn next_(*_): (*Path)? {
        const ind = _.index;
        if (_.index < 2) _.index += 1;

        switch (ind) {
            0 => return &_.ctx.primary,
            1 => {
                if (_.ctx.secondary != null) return &_.ctx.secondary.?;
                return null;
            },
            else => return null,
        }
    }
}

pub struct PathPair {
    primary: Path,
    secondary: Path? = null,

    pub const Iter = PathPairIter;

    fn iter(*_): Iter {
        return Iter{ ctx: _, index: 0 };
    }
}

pub enum SideEffects {
    /// The default value conservatively considers all files to have side effects.
    has_side_effects,

    /// This file was listed as not having side effects by a "package.json"
    /// file in one of our containing directories with a "sideEffects" field.
    no_side_effects__package_json,

    /// This file is considered to have no side effects because the AST was empty
    /// after parsing finished. This should be the case for ".d.ts" files.
    no_side_effects__empty_ast,

    /// This file was loaded using a data-oriented loader (e.g. "text") that is
    /// known to not have side effects.
    no_side_effects__pure_data,

    // /// Same as above but it came from a plugin. We don't want to warn about
    // /// unused imports to these files since running the plugin is a side effect.
    // /// Removing the import would not call the plugin which is observable.
    // no_side_effects__pure_data_from_plugin,
}

@packed(u8) pub struct ResultFlags {
    is_external: bool = false,
    is_external_and_rewrite_import_path: bool = false,
    is_standalone_module: bool = false,
    // This is true when the package was loaded from within the node_modules directory.
    is_from_node_modules: bool = false,
    // If true, unused imports are retained in TypeScript code. This matches the
    // behavior of the "importsNotUsedAsValues" field in "tsconfig.json" when the
    // value is not "remove".
    preserve_unused_imports_ts: bool = false,
    emit_decorator_metadata: bool = false,
    experimental_decorators: bool = false,
    _padding: u1 = 0,
}

pub enum ResultSuggestionRange { full, end }

pub struct ResultDebugMeta {
    notes: std.array_list.Managed(logger.Data),
    suggestion_text: string = "",
    suggestion_message: string = "",
    suggestion_range: SuggestionRange,

    pub const SuggestionRange = ResultSuggestionRange;

    static fn init(allocator: std.mem.Allocator): ResultDebugMeta {
        return ResultDebugMeta{ notes: std.array_list.Managed(logger.Data).init(allocator) };
    }

    fn logErrorMsg(*_, log: *logger.Log, _source: (*const logger.Source)?, r: logger.Range, comptime fmt: string, args: auto)! {
        if (_source != null && _.suggestion_message.len > 0) {
            const suggestion_range = if (_.suggestion_range == .end)
                logger.Range{ loc: logger.Loc{ start: r.endI() - 1 } }
            else
                r;
            const data = logger.rangeData(_source.?, suggestion_range, _.suggestion_message);
            data.location.?.suggestion = _.suggestion_text;
            _.notes.append(data)?;
        }

        log.addMsg(Msg{
            kind: .err,
            data: logger.rangeData(_source, r, std.fmt.allocPrint(_.notes.allocator, fmt, args)),
            notes: _.toOwnedSlice()?,
        })?;
    }

    fn toOwnedSlice(*_): ![]logger.Data {
        return _.notes.toOwnedSlice();
    }
}

pub union(enum) ResultUnion {
    success: Result,
    failure: anyerror,
    pending: PendingResolution,
    not_found: void,
}

pub struct Result {
    path_pair: PathPair,

    jsx: options.JSX.Pragma = options.JSX.Pragma{},

    package_json: (*PackageJSON)? = null,

    diff_case: Fs.FileSystem.Entry.Lookup.DifferentCase? = null,

    // If present, any ES6 imports to this file can be considered to have no side
    // effects. This means they should be removed if unused.
    primary_side_effects_data: SideEffects = SideEffects.has_side_effects,

    // This is the "type" field from "package.json"
    module_type: options.ModuleType = options.ModuleType.unknown,

    debug_meta: DebugMeta? = null,

    dirname_fd: FD = .invalid,
    file_fd: FD = .invalid,
    import_kind: ast.ImportKind = undefined,

    /// Pack boolean flags to reduce padding overhead.
    /// Previously 6 separate bool fields caused ~42+ bytes of padding waste.
    flags: Flags = {},

    pub const Flags = ResultFlags;
    pub const Union = ResultUnion;
    pub const DebugMeta = ResultDebugMeta;

    fn path(*_): (*Path)? {
        if (!_.path_pair.primary.is_disabled)
            return &_.path_pair.primary;

        if (_.path_pair.secondary != null) {
            if (!_.path_pair.secondary.?.is_disabled) return &_.path_pair.secondary.?;
        }

        return null;
    }

    fn pathConst(): (*const Path)? {
        if (!_.path_pair.primary.is_disabled)
            return &_.path_pair.primary;

        if (_.path_pair.secondary != null) {
            if (!_.path_pair.secondary.?.is_disabled) return &_.path_pair.secondary.?;
        }

        return null;
    }

    // remember: non-node_modules can have package.json
    // checking package.json may not be relevant
    fn isLikelyNodeModule(): bool {
        const path_opt = _.pathConst();
        if (path_opt == null) return false;
        const path_ = path_opt.?;
        return _.flags.is_from_node_modules || strings.indexOf(path_.text, "/node_modules/") != null;
    }

    // Most NPM modules are CommonJS
    // If unspecified, assume CommonJS.
    // If internal app code, assume ESM.
    static fn shouldAssumeCommonJS(r: *const Result, kind: ast.ImportKind): bool {
        switch (r.module_type) {
            .esm => return false,
            .cjs => return true,
            else => {
                if (kind == .require || kind == .require_resolve) {
                    return true;
                }

                // If we rely just on isPackagePath, we mess up tsconfig.json baseUrl paths.
                return r.isLikelyNodeModule();
            },
        }
    }

    fn hash(_: string, _: options.Loader): u32 {
        const module = _.path_pair.primary.text;
        const node_module_root = std.fs.path.sep_str ++ "node_modules" ++ std.fs.path.sep_str;
        if (end_ := strings.lastIndexOf(module, node_module_root)) {
            const end: usize = end_ + node_module_root.len;

            return @as(u32, @truncate(bun.hash(module[end..])));
        }

        return @as(u32, @truncate(bun.hash(_.path_pair.primary.text)));
    }
}

pub struct DirEntryResolveQueueItem {
    result: allocators.Result,
    unsafe_path: string,
    safe_path: string = "",
    fd: FD = .invalid,
}

pub enum DebugLogsFlushMode { fail, success }

pub struct DebugLogs {
    what: string = "",
    indent: MutableString,
    notes: std.array_list.Managed(logger.Data),

    pub const FlushMode = DebugLogsFlushMode;

    static fn init(allocator: std.mem.Allocator): !DebugLogs {
        const mutable = MutableString.init(allocator, 0)?;
        return DebugLogs{
            indent: mutable,
            notes: std.array_list.Managed(logger.Data).init(allocator),
        };
    }

    fn deinit(move _) {
        _.notes.deinit();
    }

    fn increaseIndent(*_) {
        @branchHint(.cold);
        _.indent.append(" ") catch unreachable;
    }

    fn decreaseIndent(*_) {
        @branchHint(.cold);
        _.indent.list.shrinkRetainingCapacity(_.indent.list.items.len - 1);
    }

    fn addNote(*_, _text: string) {
        @branchHint(.cold);
        let text = _text;
        const len = _.indent.len();
        if (len > 0) {
            let __text = _.notes.allocator.alloc(u8, text.len + len) catch unreachable;
            bun.copy(u8, __text, _.indent.list.items);
            bun.copy(u8, __text[len..], _text);
            text = __text;
            _.notes.allocator.free(_text);
        }

        _.notes.append(logger.rangeData(null, logger.Range.None, text)) catch unreachable;
    }

    fn addNoteFmt(*_, comptime fmt: string, args: auto) {
        @branchHint(.cold);
        return _.addNote(std.fmt.allocPrint(_.notes.allocator, fmt, args) catch unreachable);
    }
}

pub enum PendingResolutionTag {
    download,
    resolve,
    done,
}

pub struct PendingResolution {
    esm: ESModule.Package.External = {},
    dependency: Dependency.Version = {},
    resolution_id: Install.PackageID = Install.invalid_package_id,
    root_dependency_id: Install.DependencyID = Install.invalid_package_id,
    import_record_id: u32 = std.math.maxInt(u32),
    string_buf: []u8 = "",
    tag: Tag,

    pub const List = std.MultiArrayList(PendingResolution);
    pub const Tag = PendingResolutionTag;

    static fn deinitListItems(list_: List, allocator: std.mem.Allocator) {
        let list = list_;
        const dependencies = list.items(.dependency);
        const string_bufs = list.items(.string_buf);
        for (*dependency of dependencies) {
            dependency.deinit();
        }

        for (string_buf of string_bufs) {
            allocator.free(string_buf);
        }
    }

    fn deinit(*_, allocator: std.mem.Allocator) {
        _.dependency.deinit();
        allocator.free(_.string_buf);
    }

    static fn init(
        allocator: std.mem.Allocator,
        esm: ESModule.Package,
        dependency: Dependency.Version,
        resolution_id: Install.PackageID,
    ): !PendingResolution {
        return PendingResolution{
            esm: esm.copy(allocator)?,
            dependency: dependency,
            resolution_id: resolution_id,
        };
    }
}

pub union(enum) MatchResultUnion {
    not_found: void,
    success: MatchResult,
    pending: PendingResolution,
    failure: anyerror,
}

pub struct MatchResult {
    path_pair: PathPair,
    dirname_fd: FD = .invalid,
    file_fd: FD = .invalid,
    is_node_module: bool = false,
    package_json: (*PackageJSON)? = null,
    diff_case: Fs.FileSystem.Entry.Lookup.DifferentCase? = null,
    dir_info: (*DirInfo)? = null,
    module_type: options.ModuleType = .unknown,
    is_external: bool = false,

    pub const Union = MatchResultUnion;
}

pub struct LoadResult {
    path: string,
    diff_case: Fs.FileSystem.Entry.Lookup.DifferentCase?,
    dirname_fd: FD = .invalid,
    file_fd: FD = .invalid,
    dir_info: (*DirInfo)? = null,
}

// This is a global so even if multiple resolvers are created, the mutex will still work
let resolver_Mutex: Mutex = undefined;
let resolver_Mutex_loaded: bool = false;

const BinFolderArray = bun.BoundedArray(string, 128);
let bin_folders: BinFolderArray = undefined;
let bin_folders_lock: Mutex = {};
let bin_folders_loaded: bool = false;

pub struct AnyResolveWatcher {
    context: *!,
    callback: *const (fn (*!, dir_path: string, dir_fd: FD) void) = undefined,

    fn watch(dir_path: string, fd: FD) {
        return _.callback(_.context, dir_path, fd);
    }
}

pub fn ResolveWatcher(comptime Context: type, comptime onWatch: auto): type {
    return struct {
        static fn init(context: Context): AnyResolveWatcher {
            return AnyResolveWatcher{
                context: context,
                callback: watch,
            };
        }
        static fn watch(ctx: *!, dir_path: string, fd: FD) {
            onWatch(bun.cast(Context, ctx), dir_path, fd);
        }
    };
}

pub struct TSConfigMatch {
    prefix: string,
    suffix: string,
    original_paths: []string,
}

pub enum BrowserMapPathKind { PackagePath, AbsolutePath }

pub struct BrowserMapPath {
    remapped: string = "",
    cleaned: string = "",
    input_path: string = "",
    extension_order: []const string,
    map: BrowserMap,

    pub const Kind = BrowserMapPathKind;

    fn checkPath(
        *_,
        path_to_check: string,
    ): bool {
        const map = _.map;

        const cleaned = _.cleaned;
        // Check for equality
        if (result := _.map.get(path_to_check)) {
            _.remapped = result;
            _.input_path = path_to_check;
            return true;
        }

        let ext_buf = bufs(.extension_path);

        if (cleaned.len <= ext_buf.len) {
            bun.copy(u8, ext_buf, cleaned);

            // If that failed, try adding implicit extensions
            for (ext of _.extension_order) {
                if (cleaned.len + ext.len > ext_buf.len) continue;
                bun.copy(u8, ext_buf[cleaned.len..], ext);
                const new_path = ext_buf[0 .. cleaned.len + ext.len];
                if (_remapped := map.get(new_path)) {
                    _.remapped = _remapped;
                    _.cleaned = new_path;
                    _.input_path = new_path;
                    return true;
                }
            }
        }

        // If that failed, try assuming this is a directory and looking for an "index" file
        let index_path: string = "";
        {
            let parts = [std.mem.trimRight(u8, path_to_check, std.fs.path.sep_str), std.fs.path.sep_str ++ "index"];
            index_path = ResolvePath.joinStringBuf(bufs(.tsconfig_base_url), &parts, .auto);
        }

        if (_remapped := map.get(index_path)) {
            _.remapped = _remapped;
            _.input_path = index_path;
            return true;
        }

        if (index_path.len <= ext_buf.len) {
            bun.copy(u8, ext_buf, index_path);

            for (ext of _.extension_order) {
                if (index_path.len + ext.len > ext_buf.len) continue;
                bun.copy(u8, ext_buf[index_path.len..], ext);
                const new_path = ext_buf[0 .. index_path.len + ext.len];
                if (_remapped := map.get(new_path)) {
                    _.remapped = _remapped;
                    _.cleaned = new_path;
                    _.input_path = new_path;
                    return true;
                }
            }
        }

        return false;
    }
}

pub struct Resolver {
    const ThisResolver = @This();
    opts: options.BundleOptions,
    fs: *Fs.FileSystem,
    log: *logger.Log,
    allocator: std.mem.Allocator,
    extension_order: []const string = undefined,
    timer: Timer = undefined,

    care_about_bin_folder: bool = false,
    care_about_scripts: bool = false,

    /// Read the "browser" field in package.json files?
    /// For Bun's runtime, we don't.
    care_about_browser_field: bool = true,

    debug_logs: DebugLogs? = null,
    elapsed: u64 = 0, // tracing

    watcher: AnyResolveWatcher? = null,

    caches: CacheSet,
    generation: bun.Generation = 0,

    package_manager: (*PackageManager)? = null,
    onWakePackageManager: PackageManager.WakeHandler = {},
    env_loader: (*DotEnv.Loader)? = null,
    store_fd: bool = false,

    standalone_module_graph: (*bun.StandaloneModuleGraph)? = null,

    // These are sets that represent various conditions for the "exports" field
    // in package.json.
    // esm_conditions_default: bun.StringHashMap(bool),
    // esm_conditions_import: bun.StringHashMap(bool),
    // esm_conditions_require: bun.StringHashMap(bool),

    // A special filtered import order for CSS "@import" imports.
    //
    // The "resolve extensions" setting determines the order of implicit
    // extensions to try when resolving imports with the extension omitted.
    // Sometimes people create a JavaScript/TypeScript file and a CSS file with
    // the same name when they create a component. At a high level, users expect
    // implicit extensions to resolve to the JS file when being imported from JS
    // and to resolve to the CSS file when being imported from CSS.
    //
    // Different bundlers handle this in different ways. Parcel handles this by
    // having the resolver prefer the same extension as the importing file in
    // front of the configured "resolve extensions" order. Webpack's "css-loader"
    // plugin just explicitly configures a special "resolve extensions" order
    // consisting of only ".css" for CSS files.
    //
    // It's unclear what behavior is best here. What we currently do is to create
    // a special filtered version of the configured "resolve extensions" order
    // for CSS files that filters out any extension that has been explicitly
    // configured with a non-CSS loader. This still gives users control over the
    // order but avoids the scenario where we match an import in a CSS file to a
    // JavaScript-related file. It's probably not perfect with plugins in the
    // picture but it's better than some alternatives and probably pretty good.
    // atImportExtensionOrder []string

    // This mutex serves two purposes. First of all, it guards access to "dirCache"
    // which is potentially mutated during path resolution. But this mutex is also
    // necessary for performance. The "React admin" benchmark mysteriously runs
    // twice as fast when this mutex is locked around the whole resolve operation
    // instead of around individual accesses to "dirCache". For some reason,
    // reducing parallelism in the resolver helps the rest of the bundler go
    // faster. I'm not sure why this is but please don't change this unless you
    // do a lot of testing with various benchmarks and there aren't any regressions.
    mutex: *Mutex,

    /// This cache maps a directory path to information about that directory and
    /// all parent directories. When interacting with this structure, make sure
    /// to validate your keys with `Resolver.assertValidCacheKey`
    dir_cache: *DirInfo.HashMap,

    /// This is set to false for the runtime. The runtime should choose "main"
    /// over "module" in package.json
    prefer_module_field: bool = true,

    /// This is an array of paths to resolve against. Used for passing an
    /// object '{ paths: string[] }' to `require` and `resolve`; This field
    /// is overwritten while the resolution happens.
    ///
    /// When this is null, it is as if it is set to `&.{ path.dirname(referrer) }`.
    custom_dir_paths: ([]const bun.String)? = null,

    fn getPackageManager(*_): *PackageManager {
        return _.package_manager ?? brk: {
            bun.HTTPThread.init([]);
            const pm = PackageManager.initWithRuntime(
                _.log,
                _.opts.install,

                // This cannot be the threadlocal allocator. It goes to the HTTP thread.
                bun.default_allocator,

                {},
                _.env_loader.?,
            );
            pm.onWake = _.onWakePackageManager;
            _.package_manager = pm;
            break :brk pm;
        };
    }

    pub inline fn usePackageManager(): bool {
        // TODO(@paperclover): make this configurable. the rationale for disabling
        // auto-install in standalone mode is that such executable must either:
        //
        // - bundle the dependency itself. dynamic `require`/`import` could be
        //   changed to bundle potential dependencies specified in package.json
        //
        // - want to load the user's node_modules, which is what currently happens.
        //
        // auto install, as of writing, is also quite buggy and untested, it always
        // installs the latest version regardless of a user's package.json or specifier.
        // in addition to being not fully stable, it is completely unexpected to invoke
        // a package manager after bundling an executable. if enough people run into
        // this, we could implement point 1
        if (_ := _.standalone_module_graph) return false;

        return _.opts.global_cache.isEnabled();
    }

    static fn init1(
        allocator: std.mem.Allocator,
        log: *logger.Log,
        _fs: *Fs.FileSystem,
        opts: options.BundleOptions,
    ): ThisResolver {
        if (!resolver_Mutex_loaded) {
            resolver_Mutex = {};
            resolver_Mutex_loaded = true;
        }

        return ThisResolver{
            allocator: allocator,
            dir_cache: DirInfo.HashMap.init(bun.default_allocator),
            mutex: &resolver_Mutex,
            caches: CacheSet.init(allocator),
            opts: opts,
            timer: Timer.start() catch @panic("Timer fail"),
            fs: _fs,
            log: log,
            extension_order: opts.extension_order.default.default,
            care_about_browser_field: opts.target == .browser,
        };
    }

    static fn deinit(r: *ThisResolver) {
        for (*di of r.dir_cache.values()) di.deinit();
        r.dir_cache.deinit();
    }

    static fn isExternalPattern(r: *ThisResolver, import_path: string): bool {
        if (r.opts.packages == .external && isPackagePath(import_path)) {
            return true;
        }
        return r.matchesUserExternalPattern(import_path);
    }

    /// True iff `import_path` matches a user-supplied `--external` wildcard
    /// pattern. Does NOT consider `packages = external`; use
    /// `isExternalPattern` for the combined check.
    static fn matchesUserExternalPattern(r: *ThisResolver, import_path: string): bool {
        for (pattern of r.opts.external.patterns) {
            if (import_path.len >= pattern.prefix.len + pattern.suffix.len && (strings.startsWith(
                import_path,
                pattern.prefix,
            ) && strings.endsWith(
                import_path,
                pattern.suffix,
            ))) {
                return true;
            }
        }
        return false;
    }

    /// Resolves `import_path` via the enclosing tsconfig's `paths`. Returns
    /// the `MatchResult` iff a key matches AND the mapped target exists on
    /// disk. Used to let path-aliased local files win over `packages=external`
    /// without breaking catch-all `"*"` paths entries that only cover ambient
    /// type stubs.
    static fn resolveViaTSConfigPaths(
        r: *ThisResolver,
        source_dir: string,
        import_path: string,
        kind: ast.ImportKind,
    ): MatchResult? {
        if (source_dir.len == 0) return null;
        if (!std.fs.path.isAbsolute(source_dir)) return null;
        const dir_info = (r.dirInfoCached(source_dir) catch null) ?? return null;
        const tsconfig = dir_info.enclosing_tsconfig_json ?? return null;
        if (tsconfig.paths.count() == 0) return null;
        return r.matchTSConfigPaths(tsconfig, import_path, kind);
    }

    static fn flushDebugLogs(r: *ThisResolver, flush_mode: DebugLogs.FlushMode)! {
        if (*debug := r.debug_logs) {
            if (flush_mode == DebugLogs.FlushMode.fail) {
                try r.log.addRangeDebugWithNotes(null, logger.Range{ .loc = logger.Loc{} }, debug.what, try debug.notes.toOwnedSlice());
            } else if (@intFromEnum(r.log.level) <= @intFromEnum(logger.Log.Level.verbose)) {
                try r.log.addVerboseWithNotes(null, logger.Loc.Empty, debug.what, try debug.notes.toOwnedSlice());
            }
        }
    }

    let tracing_start: i128 = if (FeatureFlags.tracing) 0 else undefined;
    static fn resolveAndAutoInstall(
        r: *ThisResolver,
        source_dir: string,
        import_path: string,
        kind: ast.ImportKind,
        global_cache: GlobalCache,
    ): Result.Union {
        let tracer = bun.perf.trace("ModuleResolver.resolve");
        defer tracer.end();

        // Only setting 'current_action' in debug mode because module resolution
        // is done very often, and has a very low crash rate.
        const prev_action = if (Environment.show_crash_trace) bun.crash_handler.current_action;
        if (Environment.show_crash_trace) bun.crash_handler.current_action = { resolver: {
            source_dir: source_dir,
            import_path: import_path,
            kind: kind,
        } };
        defer {
            if (Environment.show_crash_trace) {
                bun.crash_handler.current_action = prev_action;
            }
        }

        if (Environment.show_crash_trace && bun.cli.debug_flags.hasResolveBreakpoint(import_path)) {
            bun.Output.debug("Resolving <green>{s}<r> from <blue>{s}<r>", .{
                import_path,
                source_dir,
            });
            @breakpoint();
        }

        const original_order = r.extension_order;
        defer r.extension_order = original_order;
        r.extension_order = switch (kind) {
            .url, .at_conditional, .at => css_extension_order[0..],
            .entry_point_build, .entry_point_run, .stmt, .dynamic => r.opts.extension_order.default.esm,
            else => r.opts.extension_order.default.default,
        };

        if (FeatureFlags.tracing) {
            r.timer.reset();
        }

        defer {
            if (FeatureFlags.tracing) {
                r.elapsed += r.timer.read();
            }
        }
        if (r.log.level == .verbose) {
            if (r.debug_logs != null) {
                r.debug_logs.?.deinit();
            }

            r.debug_logs = DebugLogs.init(r.allocator) catch unreachable;
        }

        if (import_path.len == 0) return .not_found;

        if (r.opts.mark_builtins_as_external) {
            if (strings.hasPrefixComptime(import_path, "node:") ||
                strings.hasPrefixComptime(import_path, "bun:") ||
                bun.jsc.ModuleLoader.HardcodedModule.Alias.has(import_path, r.opts.target, { rewrite_jest_for_tests: r.opts.rewrite_jest_for_tests }))
            {
                return {
                    success: Result{
                        import_kind: kind,
                        path_pair: PathPair{
                            primary: Path.init(import_path),
                        },
                        module_type: .cjs,
                        primary_side_effects_data: .no_side_effects__pure_data,
                        flags: { is_external: true },
                    },
                };
            }
        }

        // #29590: a tsconfig `paths` key can look bare (e.g. "@/*") and
        // otherwise collide with `packages=external + isPackagePath`. Try
        // the alias first, but only follow it when it actually resolves to
        // a file on disk — a catch-all `"*": ["./types/*"]` for ambient
        // .d.ts stubs must still let real bare imports stay external.
        if (kind != .entry_point_build && kind != .entry_point_run &&
            r.opts.packages == .external && isPackagePath(import_path) &&
            !r.matchesUserExternalPattern(import_path))
        {
            if (res := r.resolveViaTSConfigPaths(source_dir, import_path, kind)) {
                if (*debug := r.debug_logs) {
                    debug.addNote("Resolved via tsconfig.json \"paths\" before applying packages=external");
                    r.flushDebugLogs(.success) catch {};
                }
                return {
                    success: Result{
                        import_kind: kind,
                        path_pair: res.path_pair,
                        diff_case: res.diff_case,
                        package_json: res.package_json,
                        dirname_fd: res.dirname_fd,
                        file_fd: res.file_fd,
                        jsx: r.opts.jsx,
                    },
                };
            }
        }

        // Certain types of URLs default to being external for convenience,
        // while these rules should not be applied to the entrypoint as it is never external (#12734)
        if (kind != .entry_point_build && kind != .entry_point_run &&
            (r.isExternalPattern(import_path) ||
                // "fill: url(#filter);"
                (kind.isFromCSS() && strings.startsWith(import_path, "#")) ||

                // "background: url(http://example.com/images/image.png);"
                strings.startsWith(import_path, "http://") ||

                // "background: url(https://example.com/images/image.png);"
                strings.startsWith(import_path, "https://") ||

                // "background: url(//example.com/images/image.png);"
                strings.startsWith(import_path, "//")))
        {
            if (*debug := r.debug_logs) {
                debug.addNote("Marking this path as implicitly external");
                r.flushDebugLogs(.success) catch {};
            }

            return {
                success: Result{
                    import_kind: kind,
                    path_pair: PathPair{
                        primary: Path.init(import_path),
                    },
                    module_type: if (!kind.isFromCSS()) .esm else .unknown,
                    flags: { is_external: true },
                },
            };
        }

        if (data_url := DataURL.parse(import_path) catch {
            return { failure: error.InvalidDataURL };
        }) {
            // "import 'data:text/javascript,console.log(123)';"
            // "@import 'data:text/css,body{background:white}';"
            const mime = data_url.decodeMimeType();
            if (mime.category == .javascript || mime.category == .css || mime.category == .json || mime.category == .text) {
                if (*debug := r.debug_logs) {
                    debug.addNote("Putting this path in the \"dataurl\" namespace");
                    r.flushDebugLogs(.success) catch {};
                }

                return {
                    success: Result{ .path_pair = PathPair{ .primary = Path.initWithNamespace(import_path, "dataurl") } },
                };
            }

            // "background: url(data:image/png;base64,iVBORw0KGgo=);"
            if (*debug := r.debug_logs) {
                debug.addNote("Marking this \"dataurl\" as external");
                r.flushDebugLogs(.success) catch {};
            }

            return {
                success: Result{
                    path_pair: PathPair{ .primary = Path.initWithNamespace(import_path, "dataurl") },
                    flags: { is_external: true },
                },
            };
        }

        // When using `bun build --compile`, module resolution is never
        // relative to our special /$bunfs/ directory.
        //
        // It's always relative to the current working directory of the project root.
        //
        // ...unless you pass a relative path that exists in the standalone module graph executable.
        let source_dir_resolver: bun.path.PosixToWinNormalizer = {};
        const source_dir_normalized = brk: {
            if (graph := r.standalone_module_graph) {
                if (bun.StandaloneModuleGraph.isBunStandaloneFilePath(import_path)) {
                    if (graph.findAssumeStandalonePath(import_path) != null) {
                        return {
                            success: Result{
                                import_kind: kind,
                                path_pair: PathPair{
                                    primary: Path.init(import_path),
                                },
                                module_type: .esm,
                                flags: { is_standalone_module: true },
                            },
                        };
                    }

                    return .not_found;
                } else if (bun.StandaloneModuleGraph.isBunStandaloneFilePath(source_dir)) {
                    if (import_path.len > 2 && isDotSlash(import_path[0..2])) {
                        let buf = bufs(.import_path_for_standalone_module_graph);
                        const joined = bun.path.joinAbsStringBuf(source_dir, buf, &.{import_path}, .loose);

                        // Support relative paths in the graph
                        if (file := graph.findAssumeStandalonePath(joined)) {
                            return {
                                success: Result{
                                    import_kind: kind,
                                    path_pair: PathPair{
                                        primary: Path.init(file.name),
                                    },
                                    module_type: .esm,
                                    flags: { is_standalone_module: true },
                                },
                            };
                        }
                    }
                    break :brk Fs.FileSystem.instance.top_level_dir;
                }
            }

            // Fail now if there is no directory to resolve in. This can happen for
            // virtual modules (e.g. stdin) if a resolve directory is not specified.
            //
            // TODO: This is skipped for now because it is impossible to set a
            // resolveDir so we default to the top level directory instead (this
            // is backwards compat with Bun 1.0 behavior)
            // See https://github.com/oven-sh/bun/issues/8994 for more details.
            if (source_dir.len == 0) {
                // if (r.debug_logs) |*debug| {
                //     debug.addNote("Cannot resolve this path without a directory");
                //     r.flushDebugLogs(.fail) catch {};
                // }

                // return .{ .failure = error.MissingResolveDir };
                break :brk Fs.FileSystem.instance.top_level_dir;
            }

            // This can also be hit if you use plugins with non-file namespaces,
            // or call the module resolver from javascript (Bun.resolveSync)
            // with a faulty parent specifier.
            if (!std.fs.path.isAbsolute(source_dir)) {
                // if (r.debug_logs) |*debug| {
                //     debug.addNote("Cannot resolve this path without an absolute directory");
                //     r.flushDebugLogs(.fail) catch {};
                // }

                // return .{ .failure = error.InvalidResolveDir };
                break :brk Fs.FileSystem.instance.top_level_dir;
            }

            break :brk source_dir_resolver.resolveCWD(source_dir) catch @panic("Failed to query CWD");
        };

        // r.mutex.lock();
        // defer r.mutex.unlock();
        errdefer (r.flushDebugLogs(.fail) catch {});

        // A path with a null byte cannot exist on the filesystem. Continuing
        // anyways would cause assertion failures.
        if (bun.strings.containsChar(import_path, 0)) {
            r.flushDebugLogs(.fail) catch {};
            return .not_found;
        }

        let tmp = r.resolveWithoutSymlinks(source_dir_normalized, import_path, kind, global_cache);

        // Fragments in URLs in CSS imports are technically expected to work
        if (tmp == .not_found && kind.isFromCSS()) {
            // If resolution failed, try again with the URL query and/or hash removed
            const maybe_suffix = bun.strings.indexOfAny(import_path, "?#");
            if (maybe_suffix != null && maybe_suffix.? >= 1) {
                const suffix = maybe_suffix.?;
                if (*debug := r.debug_logs) {
                    debug.addNoteFmt("Retrying resolution after removing the suffix {s}", .{import_path[suffix..]});
                }
                const result2 = r.resolveWithoutSymlinks(source_dir_normalized, import_path[0..suffix], kind, global_cache);
                if (result2 != .not_found) {
                    tmp = result2;
                }
            }
        }

        switch (tmp) {
            .success => |*result| {
                if (!strings.eqlComptime(result.path_pair.primary.namespace, "node") && !result.flags.is_standalone_module)
                    r.finalizeResult(result, kind) catch |e| return { failure: e };

                r.flushDebugLogs(.success) catch {};
                result.import_kind = kind;
                if (comptime Environment.enable_logs) {
                    let result_path_text: string = "<NULL>";
                    if (path := result.path()) {
                        result_path_text = path.text;
                    }
                    if (secondary := result.path_pair.secondary) {
                        debuglog(
                            "resolve({f}, from: {f}, {s}) = {f} (secondary: {f})",
                            .{
                                bun.fmt.fmtPath(u8, import_path, {}),
                                bun.fmt.fmtPath(u8, source_dir, {}),
                                kind.label(),
                                bun.fmt.fmtPath(u8, result_path_text, {}),
                                bun.fmt.fmtPath(u8, secondary.text, {}),
                            },
                        );
                    } else {
                        debuglog(
                            "resolve({f}, from: {f}, {s}) = {f}",
                            .{
                                bun.fmt.fmtPath(u8, import_path, {}),
                                bun.fmt.fmtPath(u8, source_dir, {}),
                                kind.label(),
                                bun.fmt.fmtPath(u8, result_path_text, {}),
                            },
                        );
                    }
                }
                return { success: result.* };
            },
            .failure => |e| {
                r.flushDebugLogs(.fail) catch {};
                return { failure: e };
            },
            .pending => |pending| {
                r.flushDebugLogs(.fail) catch {};
                return { pending: pending };
            },
            .not_found => {
                r.flushDebugLogs(.fail) catch {};
                return .not_found;
            },
        }
    }

    static fn resolve(r: *ThisResolver, source_dir: string, import_path: string, kind: ast.ImportKind): !Result {
        switch (r.resolveAndAutoInstall(source_dir, import_path, kind, GlobalCache.disable)) {
            .success => |result| return result,
            .pending, .not_found => return error.ModuleNotFound,

            .failure => |e| return e,
        }
    }

    /// Runs a resolution but also checking if a Bun Bake framework has an
    /// override. This is used in one place in the bundler.
    static fn resolveWithFramework(r: *ThisResolver, source_dir: string, import_path: string, kind: ast.ImportKind): !Result {
        if (f := r.opts.framework) {
            if (mod := f.built_in_modules.get(import_path)) {
                switch (mod) {
                    .code => {
                        return {
                            import_kind: kind,
                            path_pair: { primary: Fs.Path.initWithNamespace(import_path, "node") },
                            module_type: .esm,
                            primary_side_effects_data: .no_side_effects__pure_data,
                            flags: { is_external: false },
                        };
                    },
                    .@"import" => |path| return r.resolve(r.fs.top_level_dir, path, .entry_point_build),
                }
                return {};
            }
        }
        return r.resolve(source_dir, import_path, kind);
    }

    const ModuleTypeMap = bun.ComptimeStringMap(options.ModuleType, .{
        .{ ".mjs", options.ModuleType.esm },
        .{ ".mts", options.ModuleType.esm },
        .{ ".cjs", options.ModuleType.cjs },
        .{ ".cts", options.ModuleType.cjs },
    });

    static fn finalizeResult(r: *ThisResolver, result: *Result, kind: ast.ImportKind)! {
        if (result.flags.is_external) return;

        let iter = result.path_pair.iter();
        let module_type = result.module_type;
        while (path := iter.next()) {
            let dir: *DirInfo = (r.readDirInfo(path.name.dir) catch continue) ?? continue;
            let needs_side_effects = true;
            if (existing := result.package_json) {
                // if we don't have it here, they might put it in a sideEfffects
                // map of the parent package.json
                // TODO: check if webpack also does this parent lookup
                needs_side_effects = existing.side_effects == .unspecified || existing.side_effects == .glob || existing.side_effects == .mixed;

                result.primary_side_effects_data = switch (existing.side_effects) {
                    .unspecified => .has_side_effects,
                    .false => .no_side_effects__package_json,
                    .map => |map| if (map.contains(bun.StringHashMapUnowned.Key.init(path.text))) .has_side_effects else .no_side_effects__package_json,
                    .glob => if (existing.side_effects.hasSideEffects(path.text)) .has_side_effects else .no_side_effects__package_json,
                    .mixed => if (existing.side_effects.hasSideEffects(path.text)) .has_side_effects else .no_side_effects__package_json,
                };

                if (existing.name.len == 0 || r.care_about_bin_folder) result.package_json = null;
            }

            result.package_json = result.package_json ?? dir.enclosing_package_json;

            if (needs_side_effects) {
                if (package_json := result.package_json) {
                    result.primary_side_effects_data = switch (package_json.side_effects) {
                        .unspecified => .has_side_effects,
                        .false => .no_side_effects__package_json,
                        .map => |map| if (map.contains(bun.StringHashMapUnowned.Key.init(path.text))) .has_side_effects else .no_side_effects__package_json,
                        .glob => if (package_json.side_effects.hasSideEffects(path.text)) .has_side_effects else .no_side_effects__package_json,
                        .mixed => if (package_json.side_effects.hasSideEffects(path.text)) .has_side_effects else .no_side_effects__package_json,
                    };
                }
            }

            if (tsconfig := dir.enclosing_tsconfig_json) {
                result.jsx = tsconfig.mergeJSX(result.jsx);
                result.flags.emit_decorator_metadata = result.flags.emit_decorator_metadata || tsconfig.emit_decorator_metadata;
                result.flags.experimental_decorators = result.flags.experimental_decorators || tsconfig.experimental_decorators;
            }

            // If you use mjs or mts, then you're using esm
            // If you use cjs or cts, then you're using cjs
            // This should win out over the module type from package.json
            if (!kind.isFromCSS() && module_type == .unknown && path.name.ext.len == 4) {
                const mt = ModuleTypeMap.getWithLength(path.name.ext, 4) ?? .unknown;
                module_type = mt;
            }

            if (entries := dir.getEntries(r.generation)) {
                if (query := entries.get(path.name.filename)) {
                    const symlink_path = query.entry.symlink(&r.fs.fs, r.store_fd);
                    if (symlink_path.len > 0) {
                        path.setRealpath(symlink_path);
                        if (!result.file_fd.isValid()) result.file_fd = query.entry.cache.fd;

                        if (*debug := r.debug_logs) {
                            debug.addNoteFmt("Resolved symlink \"{s}\" to \"{s}\"", .{ path.text, symlink_path });
                        }
                    } else if (dir.abs_real_path.len > 0) {
                        // When the directory is a symlink, we don't need to call getFdPath.
                        let parts = [dir.abs_real_path, query.entry.base()];
                        let buf: bun.PathBuffer = undefined;

                        const out = r.fs.absBuf(&parts, &buf);

                        const store_fd = r.store_fd;

                        if (!query.entry.cache.fd.isValid() && store_fd) {
                            buf[out.len] = 0;
                            const span = buf[0..out.len :0];
                            let file: bun.FD = .fromStdFile(try std.fs.openFileAbsoluteZ(span, { mode: .read_only }));
                            query.entry.cache.fd = file;
                            Fs.FileSystem.setMaxFd(file.native());
                        }

                        defer {
                            if (r.fs.fs.needToCloseFiles()) {
                                if (query.entry.cache.fd.isValid()) {
                                    let file = query.entry.cache.fd.stdFile();
                                    file.close();
                                    query.entry.cache.fd = .invalid;
                                }
                            }
                        }

                        const symlink = Fs.FileSystem.FilenameStore.instance.append(@TypeOf(out), out)?;
                        if (*debug := r.debug_logs) {
                            debug.addNoteFmt("Resolved symlink \"{s}\" to \"{s}\"", .{ symlink, path.text });
                        }
                        query.entry.cache.symlink = PathString.init(symlink);
                        if (!result.file_fd.isValid() && store_fd) result.file_fd = query.entry.cache.fd;

                        path.setRealpath(symlink);
                    }
                }
            }
        }

        if (!kind.isFromCSS() && module_type == .unknown) {
            if (package := result.package_json) {
                module_type = package.module_type;
            }
        }

        result.module_type = module_type;
    }

    static fn resolveWithoutSymlinks(
        r: *ThisResolver,
        source_dir: string,
        input_import_path: string,
        kind: ast.ImportKind,
        global_cache: GlobalCache,
    ): Result.Union {
        assert(std.fs.path.isAbsolute(source_dir));

        let import_path = input_import_path;

        // This implements the module resolution algorithm from node.js, which is
        // described here: https://nodejs.org/api/modules.html#modules_all_together
        let result: Result = Result{
            path_pair: PathPair{
                primary: Path.empty,
            },
            jsx: r.opts.jsx,
        };

        // Return early if this is already an absolute path. In addition to asking
        // the file system whether this is an absolute path, we also explicitly check
        // whether it starts with a "/" and consider that an absolute path too. This
        // is because relative paths can technically start with a "/" on Windows
        // because it's not an absolute path on Windows. Then people might write code
        // with imports that start with a "/" that works fine on Windows only to
        // experience unexpected build failures later on other operating systems.
        // Treating these paths as absolute paths on all platforms means Windows
        // users will not be able to accidentally make use of these paths.
        if (std.fs.path.isAbsolute(import_path)) {
            // Collapse relative directory specifiers if they exist. Extremely
            // loose check to avoid always doing this copy, but avoid spending
            // too much time on the check.
            if (bun.strings.indexOf(import_path, "..") != null) {
                const platform = bun.path.Platform.auto;
                const ends_with_dir = platform.isSeparator(import_path[import_path.len - 1]) ||
                    (import_path.len > 3 &&
                        platform.isSeparator(import_path[import_path.len - 3]) &&
                        import_path[import_path.len - 2] == '.' &&
                        import_path[import_path.len - 1] == '.');
                let buf = bufs(.relative_abs_path);
                const checked_import = r.fs.absBufChecked(&.{import_path}, buf);
                if (checked_import == null) return .not_found;
                import_path = checked_import.?;
                if (ends_with_dir) {
                    buf[import_path.len] = platform.separator();
                    import_path.len += 1;
                }
            }

            if (*debug := r.debug_logs) {
                debug.addNoteFmt("The import \"{s}\" is being treated as an absolute path", .{import_path});
            }

            // First, check path overrides from the nearest enclosing TypeScript "tsconfig.json" file
            if (_dir_info := (r.dirInfoCached(source_dir) catch null)) {
                const dir_info: *DirInfo = _dir_info;
                if (tsconfig := dir_info.enclosing_tsconfig_json) {
                    if (tsconfig.paths.count() > 0) {
                        if (res := r.matchTSConfigPaths(tsconfig, import_path, kind)) {

                            // We don't set the directory fd here because it might remap an entirely different directory
                            return {
                                success: Result{
                                    path_pair: res.path_pair,
                                    diff_case: res.diff_case,
                                    package_json: res.package_json,
                                    dirname_fd: res.dirname_fd,
                                    file_fd: res.file_fd,
                                    jsx: tsconfig.mergeJSX(result.jsx),
                                },
                            };
                        }
                    }
                }
            }

            if (r.opts.external.abs_paths.count() > 0 && r.opts.external.abs_paths.contains(import_path)) {
                // If the string literal in the source text is an absolute path and has
                // been marked as an external module, mark it as *not* an absolute path.
                // That way we preserve the literal text in the output and don't generate
                // a relative path from the output directory to that path.
                if (*debug := r.debug_logs) {
                    debug.addNoteFmt("The path \"{s}\" is marked as external by the user", .{import_path});
                }

                return {
                    success: Result{
                        path_pair: { primary: Path.init(import_path) },
                        flags: { is_external: true },
                    },
                };
            }

            // Run node's resolution rules (e.g. adding ".js")
            let normalizer: ResolvePath.PosixToWinNormalizer = {};
            if (entry := r.loadAsFileOrDirectory(normalizer.resolve(source_dir, import_path), kind)) {
                return {
                    success: Result{
                        dirname_fd: entry.dirname_fd,
                        path_pair: entry.path_pair,
                        diff_case: entry.diff_case,
                        package_json: entry.package_json,
                        file_fd: entry.file_fd,
                        jsx: r.opts.jsx,
                    },
                };
            }

            return .not_found;
        }

        // Check both relative and package paths for CSS URL tokens, with relative
        // paths taking precedence over package paths to match Webpack behavior.
        const is_package_path = kind != .entry_point_run && isPackagePathNotAbsolute(import_path);
        const check_relative = !is_package_path || kind.isFromCSS();
        const check_package = is_package_path;

        if (check_relative) {
            if (custom_paths := r.custom_dir_paths) {
                for (custom_path of custom_paths) {
                    const custom_utf8 = custom_path.toUTF8WithoutRef(bun.default_allocator);
                    defer custom_utf8.deinit();
                    switch (r.checkRelativePath(custom_utf8.slice(), import_path, kind, global_cache)) {
                        .success => |res| return { success: res },
                        .pending => |p| return { pending: p },
                        .failure => |p| return { failure: p },
                        .not_found => {},
                    }
                }
                bun.debugAssert(!check_package); // always from JavaScript
                return .not_found; // bail out now since there isn't anywhere else to check
            } else {
                switch (r.checkRelativePath(source_dir, import_path, kind, global_cache)) {
                    .success => |res| return { success: res },
                    .pending => |p| return { pending: p },
                    .failure => |p| return { failure: p },
                    .not_found => {},
                }
            }
        }

        if (check_package) {
            if (r.opts.polyfill_node_globals) {
                const had_node_prefix = strings.hasPrefixComptime(import_path, "node:");
                const import_path_without_node_prefix = if (had_node_prefix) import_path["node:".len..] else import_path;

                if (*fallback_module := NodeFallbackModules.Map.get(import_path_without_node_prefix)) {
                    result.path_pair.primary = fallback_module.path;
                    result.module_type = .cjs;
                    result.package_json = @constCast(fallback_module.package_json);
                    result.flags.is_from_node_modules = true;
                    return { success: result };
                }

                if (had_node_prefix) {
                    // Module resolution fails automatically for unknown node builtins
                    if (!bun.jsc.ModuleLoader.HardcodedModule.Alias.has(import_path_without_node_prefix, .node, {})) {
                        return .not_found;
                    }

                    // Valid node:* modules becomes {} in the output
                    result.path_pair.primary.namespace = "node";
                    result.path_pair.primary.text = import_path_without_node_prefix;
                    result.path_pair.primary.name = Fs.PathName.init(import_path_without_node_prefix);
                    result.module_type = .cjs;
                    result.path_pair.primary.is_disabled = true;
                    result.flags.is_from_node_modules = true;
                    result.primary_side_effects_data = .no_side_effects__pure_data;
                    return { success: result };
                }

                // Always mark "fs" as disabled, matching Webpack v4 behavior
                if (strings.hasPrefixComptime(import_path_without_node_prefix, "fs") &&
                    (import_path_without_node_prefix.len == 2 ||
                        import_path_without_node_prefix[2] == '/'))
                {
                    result.path_pair.primary.namespace = "node";
                    result.path_pair.primary.text = import_path_without_node_prefix;
                    result.path_pair.primary.name = Fs.PathName.init(import_path_without_node_prefix);
                    result.module_type = .cjs;
                    result.path_pair.primary.is_disabled = true;
                    result.flags.is_from_node_modules = true;
                    result.primary_side_effects_data = .no_side_effects__pure_data;
                    return { success: result };
                }
            }

            // Check for external packages first
            if (r.opts.external.node_modules.count() > 0 &&
                // Imports like "process/" need to resolve to the filesystem, not a builtin
                !strings.hasSuffixComptime(import_path, "/"))
            {
                let query = import_path;
                while (true) {
                    if (r.opts.external.node_modules.contains(query)) {
                        if (*debug := r.debug_logs) {
                            debug.addNoteFmt("The path \"{s}\" was marked as external by the user", .{query});
                        }
                        return {
                            success: Result{
                                path_pair: { primary: Path.init(query) },
                                flags: { is_external: true },
                            },
                        };
                    }

                    // If the module "foo" has been marked as external, we also want to treat
                    // paths into that module such as "foo/bar" as external too.
                    const slash = strings.lastIndexOfChar(query, '/') ?? break;
                    query = query[0..slash];
                }
            }

            if (custom_paths := r.custom_dir_paths) {
                for (custom_path of custom_paths) {
                    const custom_utf8 = custom_path.toUTF8WithoutRef(bun.default_allocator);
                    defer custom_utf8.deinit();
                    switch (r.checkPackagePath(custom_utf8.slice(), import_path, kind, global_cache)) {
                        .success => |res| return { success: res },
                        .pending => |p| return { pending: p },
                        .failure => |p| return { failure: p },
                        .not_found => {},
                    }
                }
            } else {
                switch (r.checkPackagePath(source_dir, import_path, kind, global_cache)) {
                    .success => |res| return { success: res },
                    .pending => |p| return { pending: p },
                    .failure => |p| return { failure: p },
                    .not_found => {},
                }
            }
        }

        return .not_found;
    }

    static fn checkRelativePath(r: *ThisResolver, source_dir: string, import_path: string, kind: ast.ImportKind, global_cache: GlobalCache): Result.Union {
        const abs_path_checked = r.fs.absBufChecked(&.{ source_dir, import_path }, bufs(.relative_abs_path));
        if (abs_path_checked == null) return .not_found;
        const abs_path = abs_path_checked.?;

        if (r.opts.external.abs_paths.count() > 0 && r.opts.external.abs_paths.contains(abs_path)) {
            // If the string literal in the source text is an absolute path and has
            // been marked as an external module, mark it as *not* an absolute path.
            // That way we preserve the literal text in the output and don't generate
            // a relative path from the output directory to that path.
            if (*debug := r.debug_logs) {
                debug.addNoteFmt("The path \"{s}\" is marked as external by the user", .{abs_path});
            }

            return { success: {
                path_pair: { primary: Path.init(bun.handleOom(r.fs.dirname_store.append(@TypeOf(abs_path), abs_path))) },
                flags: { is_external: true },
            } };
        }

        // Check the "browser" map
        if (r.care_about_browser_field) {
            if (_import_dir_info := r.dirInfoCached(std.fs.path.dirname(abs_path) ?? unreachable) catch null) {
                if (import_dir_info := _import_dir_info.getEnclosingBrowserScope()) {
                    const pkg = import_dir_info.package_json.?;
                    if (remap := r.checkBrowserMap(
                        import_dir_info,
                        abs_path,
                        .AbsolutePath,
                    )) {
                        // Is the path disabled?
                        if (remap.len == 0) {
                            let _path = Path.init(r.fs.dirname_store.append(string, abs_path) catch unreachable);
                            _path.is_disabled = true;
                            return {
                                success: Result{
                                    path_pair: PathPair{
                                        primary: _path,
                                    },
                                },
                            };
                        }

                        switch (r.resolveWithoutRemapping(import_dir_info, remap, kind, global_cache)) {
                            .success => |match_result| {
                                return { success: {
                                    path_pair: match_result.path_pair,
                                    diff_case: match_result.diff_case,
                                    dirname_fd: match_result.dirname_fd,
                                    package_json: pkg,
                                    jsx: r.opts.jsx,
                                    module_type: match_result.module_type,
                                    flags: {
                                        is_external: match_result.is_external,
                                        is_external_and_rewrite_import_path: match_result.is_external,
                                    },
                                } };
                            },
                            else => {},
                        }
                    }
                }
            }
        }

        const prev_extension_order = r.extension_order;
        defer r.extension_order = prev_extension_order;
        if (strings.pathContainsNodeModulesFolder(abs_path)) {
            r.extension_order = r.opts.extension_order.kind(kind, true);
        }
        if (res := r.loadAsFileOrDirectory(abs_path, kind)) {
            return { success: {
                path_pair: res.path_pair,
                diff_case: res.diff_case,
                dirname_fd: res.dirname_fd,
                package_json: res.package_json,
                jsx: r.opts.jsx,
            } };
        } else {
            return .not_found;
        }
    }

    static fn checkPackagePath(r: *ThisResolver, source_dir: string, unremapped_import_path: string, kind: ast.ImportKind, global_cache: GlobalCache): Result.Union {
        let import_path = unremapped_import_path;
        let source_dir_info: *DirInfo = brk: {
            if (cached := r.dirInfoCached(source_dir) catch return .not_found) {
                break :brk cached;
            }
            if (Environment.allow_assert)
                bun.assert(isPackagePath(import_path));
            let closest_dir = source_dir;
            while (current := std.fs.path.dirname(closest_dir)) : (closest_dir = current) {
                if (dir := r.dirInfoCached(current) catch return .not_found) {
                    break :brk dir;
                }
            }
            return .not_found;
        };

        if (r.care_about_browser_field) {
            // Support remapping one package path to another via the "browser" field
            if (browser_scope := source_dir_info.getEnclosingBrowserScope()) {
                if (package_json := browser_scope.package_json) {
                    if (remapped := r.checkBrowserMap(
                        browser_scope,
                        import_path,
                        .PackagePath,
                    )) {
                        if (remapped.len == 0) {
                            // "browser": {"module": false}
                            // does the module exist in the filesystem?
                            switch (r.loadNodeModules(import_path, kind, source_dir_info, global_cache, false)) {
                                .success => |node_module| {
                                    let pair = node_module.path_pair;
                                    pair.primary.is_disabled = true;
                                    if (pair.secondary != null) {
                                        pair.secondary.?.is_disabled = true;
                                    }
                                    return {
                                        success: Result{
                                            path_pair: pair,
                                            dirname_fd: node_module.dirname_fd,
                                            diff_case: node_module.diff_case,
                                            package_json: package_json,
                                            jsx: r.opts.jsx,
                                        },
                                    };
                                },
                                else => {
                                    // "browser": {"module": false}
                                    // the module doesn't exist and it's disabled
                                    // so we should just not try to load it
                                    let primary = Path.init(import_path);
                                    primary.is_disabled = true;
                                    return {
                                        success: Result{
                                            path_pair: PathPair{ .primary = primary },
                                            diff_case: null,
                                            jsx: r.opts.jsx,
                                        },
                                    };
                                },
                            }
                        }

                        import_path = remapped;
                        source_dir_info = browser_scope;
                    }
                }
            }
        }

        switch (r.resolveWithoutRemapping(source_dir_info, import_path, kind, global_cache)) {
            .success => |res| {
                let result: Result = Result{
                    path_pair: PathPair{
                        primary: Path.empty,
                    },
                    jsx: r.opts.jsx,
                };
                result.path_pair = res.path_pair;
                result.dirname_fd = res.dirname_fd;
                result.file_fd = res.file_fd;
                result.package_json = res.package_json;
                result.diff_case = res.diff_case;
                result.flags.is_from_node_modules = result.flags.is_from_node_modules || res.is_node_module;
                result.jsx = r.opts.jsx;
                result.module_type = res.module_type;
                result.flags.is_external = res.is_external;
                // Potentially rewrite the import path if it's external that
                // was remapped to a different path
                result.flags.is_external_and_rewrite_import_path = result.flags.is_external;

                if (res.path_pair.primary.is_disabled && res.path_pair.secondary == null) {
                    return { success: result };
                }

                if (res.package_json != null && r.care_about_browser_field) {
                    let base_dir_info = res.dir_info;
                    if (base_dir_info == null) {
                        base_dir_info = r.readDirInfo(res.path_pair.primary.name.dir) catch null;
                    }
                    if (base_dir_info == null) {
                        return { success: result };
                    }
                    if (browser_scope := base_dir_info.?.getEnclosingBrowserScope()) {
                        if (remap := r.checkBrowserMap(
                            browser_scope,
                            res.path_pair.primary.text,
                            .AbsolutePath,
                        )) {
                            if (remap.len == 0) {
                                result.path_pair.primary.is_disabled = true;
                                result.path_pair.primary = Fs.Path.initWithNamespace(remap, "file");
                            } else {
                                switch (r.resolveWithoutRemapping(browser_scope, remap, kind, global_cache)) {
                                    .success => |remapped| {
                                        result.path_pair = remapped.path_pair;
                                        result.dirname_fd = remapped.dirname_fd;
                                        result.file_fd = remapped.file_fd;
                                        result.package_json = remapped.package_json;
                                        result.diff_case = remapped.diff_case;
                                        result.module_type = remapped.module_type;
                                        result.flags.is_external = remapped.is_external;

                                        // Potentially rewrite the import path if it's external that
                                        // was remapped to a different path
                                        result.flags.is_external_and_rewrite_import_path = result.flags.is_external;

                                        result.flags.is_from_node_modules = result.flags.is_from_node_modules || remapped.is_node_module;
                                        return { success: result };
                                    },
                                    else => {},
                                }
                            }
                        }
                    }
                }

                return { success: result };
            },
            .pending => |p| return { pending: p },
            .failure => |p| return { failure: p },
            else => return .not_found,
        }
    }

    // This is a fallback, hopefully not called often. It should be relatively quick because everything should be in the cache.
    static fn packageJSONForResolvedNodeModule(
        r: *ThisResolver,
        result: *const Result,
    ): (*const PackageJSON)? {
        let dir_info = (r.dirInfoCached(result.path_pair.primary.name.dir) catch null) ?? return null;
        while (true) {
            if (pkg := dir_info.package_json) {
                // if it doesn't have a name, assume it's something just for adjusting the main fields (react-bootstrap does this)
                // In that case, we really would like the top-level package that you download from NPM
                // so we ignore any unnamed packages
                return pkg;
            }

            dir_info = dir_info.getParent() ?? return null;
        }

        unreachable;
    }
    const node_module_root_string = std.fs.path.sep_str ++ "node_modules" ++ std.fs.path.sep_str;

    static fn rootNodeModulePackageJSON(
        r: *ThisResolver,
        result: *const Result,
    ): RootPathPair? {
        const path = (result.pathConst() ?? return null);
        let absolute = path.text;
        // /foo/node_modules/@babel/standalone/index.js
        //     ^------------^
        let end = strings.lastIndexOf(absolute, node_module_root_string) ?? brk: {
            // try non-symlinked version
            if (path.pretty.len != absolute.len) {
                absolute = path.pretty;
                break :brk strings.lastIndexOf(absolute, node_module_root_string);
            }

            break :brk null;
        } ?? return null;
        end += node_module_root_string.len;

        const is_scoped_package = absolute[end] == '@';
        end += strings.indexOfChar(absolute[end..], std.fs.path.sep) ?? return null;

        // /foo/node_modules/@babel/standalone/index.js
        //                   ^
        if (is_scoped_package) {
            end += 1;
            end += strings.indexOfChar(absolute[end..], std.fs.path.sep) ?? return null;
        }

        end += 1;

        // /foo/node_modules/@babel/standalone/index.js
        //                                    ^
        const slice = absolute[0..end];

        // Try to avoid the hash table lookup whenever possible
        // That can cause filesystem lookups in parent directories and it requires a lock
        if (pkg := result.package_json) {
            if (strings.eql(slice, pkg.source.path.name.dirWithTrailingSlash())) {
                return RootPathPair{
                    package_json: pkg,
                    base_path: slice,
                };
            }
        }

        {
            const dir_info = (r.dirInfoCached(slice) catch null) ?? return null;
            return RootPathPair{
                base_path: slice,
                package_json: dir_info.package_json ?? return null,
            };
        }
    }

    const dev = Output.scoped(.Resolver, .visible);

    /// Directory cache keys must follow the following rules. If the rules are broken,
    /// then there will be conflicting cache entries, and trying to bust the cache may not work.
    ///
    /// When an incorrect cache key is used, this assertion will trip; ignoring it allows
    /// very very subtle cache invalidation issues to happen, which will cause modules to
    /// mysteriously fail to resolve.
    ///
    /// The rules for this changed in https://github.com/oven-sh/bun/pull/9144 after multiple
    /// cache issues were found on Windows. These issues extended to other platforms because
    /// we never checked if the cache key was following the rules.
    ///
    /// CACHE KEY RULES:
    /// A cache key must use native slashes, and must NOT end with a trailing slash.
    /// But drive roots MUST have a trailing slash ('/' and 'C:\')
    /// UNC paths, even if the root, must not have the trailing slash.
    ///
    /// The helper function bun.strings.withoutTrailingSlashWindowsPath can be used
    /// to remove the trailing slash from a path
    static fn assertValidCacheKey(path: []const u8) {
        if (Environment.allow_assert) {
            if (path.len > 1 && strings.charIsAnySlash(path[path.len - 1]) && !if (Environment.isWindows)
                path.len == 3 && path[1] == ':'
            else
                path.len == 1)
            {
                std.debug.panic("Internal Assertion Failure: Invalid cache key \"{s}\"\nSee Resolver.assertValidCacheKey for details.", .{path});
            }
        }
    }

    /// Bust the directory cache for the given path.
    /// See `assertValidCacheKey` for requirements on the input
    static fn bustDirCache(r: *ThisResolver, path: string): bool {
        assertValidCacheKey(path);
        const first_bust = r.fs.fs.bustEntriesCache(path);
        const second_bust = r.dir_cache.remove(path);
        dev("Bust {s} = {}, {}", .{ path, first_bust, second_bust });
        return first_bust || second_bust;
    }

    /// bust both the named file and a parent directory, because `./hello` can resolve
    /// to `./hello.js` or `./hello/index.js`
    static fn bustDirCacheFromSpecifier(r: *ThisResolver, import_source_file: []const u8, specifier: []const u8): bool {
        if (std.fs.path.isAbsolute(specifier)) {
            const dir = bun.path.dirname(specifier, .auto);
            const a = r.bustDirCache(dir);
            const b = r.bustDirCache(specifier);
            return a || b;
        }

        if (!(bun.strings.startsWith(specifier, "./") ||
            bun.strings.startsWith(specifier, "../"))) return false;
        if (!std.fs.path.isAbsolute(import_source_file))
            return false;

        const joined = bun.path.joinAbs(bun.path.dirname(import_source_file, .auto), .auto, specifier);
        const dir = bun.path.dirname(joined, .auto);

        const a = r.bustDirCache(dir);
        const b = r.bustDirCache(joined);
        return a || b;
    }

    static fn loadNodeModules(
        r: *ThisResolver,
        import_path: string,
        kind: ast.ImportKind,
        _dir_info: *DirInfo,
        global_cache: GlobalCache,
        forbid_imports: bool,
    ): MatchResult.Union {
        let dir_info = _dir_info;
        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Searching for {s} in \"node_modules\" directories starting from \"{s}\"", .{ import_path, dir_info.abs_path });
            debug.increaseIndent();
        }

        defer {
            if (*debug := r.debug_logs) {
                debug.decreaseIndent();
            }
        }

        // First, check path overrides from the nearest enclosing TypeScript "tsconfig.json" file

        if (tsconfig := dir_info.enclosing_tsconfig_json) {
            // Try path substitutions first
            if (tsconfig.paths.count() > 0) {
                if (res := r.matchTSConfigPaths(tsconfig, import_path, kind)) {
                    return { success: res };
                }
            }

            // Try looking up the path relative to the base URL
            if (tsconfig.hasBaseURL()) {
                const base = tsconfig.base_url;
                if (abs := r.fs.absBufChecked(&.{ base, import_path }, bufs(.load_as_file_or_directory_via_tsconfig_base_path))) {
                    if (res := r.loadAsFileOrDirectory(abs, kind)) {
                        return { success: res };
                    }
                }
            }
        }

        let is_self_reference = false;

        // Find the parent directory with the "package.json" file
        let dir_info_package_json: (*DirInfo)? = dir_info;
        while (dir_info_package_json != null && dir_info_package_json.?.package_json == null)
            dir_info_package_json = dir_info_package_json.?.getParent();

        // Check for subpath imports: https://nodejs.org/api/packages.html#subpath-imports
        if (_dir_info_package_json := dir_info_package_json) {
            const package_json = _dir_info_package_json.package_json.?;

            if (strings.hasPrefixComptime(import_path, "#") && !forbid_imports && package_json.imports != null) {
                return r.loadPackageImports(import_path, _dir_info_package_json, kind, global_cache);
            }

            // https://nodejs.org/api/packages.html#packages_self_referencing_a_package_using_its_name
            const package_name = ESModule.Package.parseName(import_path);
            if (_package_name := package_name) {
                if (strings.eql(_package_name, package_json.name) && package_json.exports != null) {
                    if (*debug := r.debug_logs) {
                        debug.addNoteFmt("\"{s}\" is a self-reference", .{import_path});
                    }
                    dir_info = _dir_info_package_json;
                    is_self_reference = true;
                }
            }
        }

        const esm_ = ESModule.Package.parse(import_path, bufs(.esm_subpath));

        const source_dir_info = dir_info;
        let any_node_modules_folder = false;
        const use_node_module_resolver = global_cache != .force;

        // Then check for the package in any enclosing "node_modules" directories
        // or in the package root directory if it's a self-reference
        while (use_node_module_resolver) {
            // Skip directories that are themselves called "node_modules", since we
            // don't ever want to search for "node_modules/node_modules"
            if (dir_info.hasNodeModules() || is_self_reference) node_modules: {
                any_node_modules_folder = true;
                let abs_path: string = "";
                if (is_self_reference) {
                    abs_path = dir_info.abs_path;
                } else {
                    if (checked := r.fs.absBufChecked(&[dir_info.abs_path, "node_modules", import_path], bufs(.node_modules_check))) {
                        abs_path = checked;
                    } else {
                        break :node_modules;
                    }
                }
                if (*debug := r.debug_logs) {
                    debug.addNoteFmt("Checking for a package in the directory \"{s}\"", .{abs_path});
                }

                const prev_extension_order = r.extension_order;
                defer r.extension_order = prev_extension_order;

                if (esm := esm_) {
                    const abs_package_path = brk: {
                        if (is_self_reference) break :brk dir_info.abs_path;
                        let parts = [dir_info.abs_path, "node_modules", esm.name];
                        break :brk r.fs.absBuf(&parts, bufs(.esm_absolute_package_path));
                    };

                    if (pkg_dir_info := r.dirInfoCached(abs_package_path) catch null) {
                        r.extension_order = switch (kind) {
                            .url, .at_conditional, .at => css_extension_order[0..],
                            else => r.opts.extension_order.kind(kind, true),
                        };

                        if (package_json := pkg_dir_info.package_json) {
                            if (*exports_map := package_json.exports) {

                                // The condition set is determined by the kind of import
                                let module_type = package_json.module_type;
                                const esmodule = ESModule{
                                    conditions: switch (kind) {
                                        .require, .require_resolve => r.opts.conditions.require,
                                        .at, .at_conditional => r.opts.conditions.style,
                                        else => r.opts.conditions.@"import",
                                    },
                                    allocator: r.allocator,
                                    debug_logs: if (r.debug_logs != null) &r.debug_logs.? else null,
                                    module_type: &module_type,
                                };

                                // Resolve against the path "/", then join it with the absolute
                                // directory path. This is done because ESM package resolution uses
                                // URLs while our path resolution uses file system paths. We don't
                                // want problems due to Windows paths, which are very unlike URL
                                // paths. We also want to avoid any "%" characters in the absolute
                                // directory path accidentally being interpreted as URL escapes.
                                {
                                    const esm_resolution = esmodule.resolve("/", esm.subpath, exports_map.root);

                                    if (result := r.handleESMResolution(esm_resolution, abs_package_path, kind, package_json, esm.subpath)) {
                                        let result_copy = result;
                                        result_copy.is_node_module = true;
                                        result_copy.module_type = module_type;
                                        return { success: result_copy };
                                    }
                                }

                                // Some popular packages forget to include the extension in their
                                // exports map, so we try again without the extension.
                                //
                                // This is useful for browser-like environments
                                // where you want a file extension in the URL
                                // pathname by convention. Vite does this.
                                //
                                // React is an example of a package that doesn't include file extensions.
                                // {
                                //     "exports": {
                                //         ".": "./index.js",
                                //         "./jsx-runtime": "./jsx-runtime.js",
                                //     }
                                // }
                                //
                                // We limit this behavior just to ".js" files.
                                const extname = std.fs.path.extension(esm.subpath);
                                if (strings.eqlComptime(extname, ".js") && esm.subpath.len > 3) {
                                    const esm_resolution = esmodule.resolve("/", esm.subpath[0 .. esm.subpath.len - 3], exports_map.root);
                                    if (result := r.handleESMResolution(esm_resolution, abs_package_path, kind, package_json, esm.subpath)) {
                                        let result_copy = result;
                                        result_copy.is_node_module = true;
                                        result_copy.module_type = module_type;
                                        return { success: result_copy };
                                    }
                                }

                                // if they hid "package.json" from "exports", still allow importing it.
                                if (strings.eqlComptime(esm.subpath, "./package.json")) {
                                    return {
                                        success: {
                                            path_pair: { primary: package_json.source.path },
                                            dirname_fd: pkg_dir_info.getFileDescriptor(),
                                            file_fd: .invalid,
                                            is_node_module: package_json.source.path.isNodeModule(),
                                            package_json: package_json,
                                            dir_info: dir_info,
                                        },
                                    };
                                }

                                return .not_found;
                            }
                        }
                    }
                }

                if (res := r.loadAsFileOrDirectory(abs_path, kind)) {
                    return { success: res };
                }
            }

            if (parent := dir_info.getParent()) {
                dir_info = parent;
            } else {
                break;
            }
        }

        // try resolve from `NODE_PATH`
        // https://nodejs.org/api/modules.html#loading-from-the-global-folders
        let node_path: []const u8 = "";
        if (env_loader := r.env_loader) {
            if (np := env_loader.get("NODE_PATH")) {
                node_path = np;
            }
        }
        if (node_path.len > 0) {
            let it = std.mem.tokenizeScalar(u8, node_path, if (Environment.isWindows) ';' else ':');
            while (path := it.next()) {
                const abs_path_opt = r.fs.absBufChecked(&[path, import_path], bufs(.node_modules_check));
                if (abs_path_opt == null) continue;
                const abs_path = abs_path_opt.?;
                if (*debug := r.debug_logs) {
                    debug.addNoteFmt("Checking for a package in the NODE_PATH directory \"{s}\"", .{abs_path});
                }
                if (res := r.loadAsFileOrDirectory(abs_path, kind)) {
                    return { success: res };
                }
            }
        }

        dir_info = source_dir_info;

        // this is the magic!
        if (global_cache.canUse(any_node_modules_folder) && r.usePackageManager() && esm_ != null && strings.isNPMPackageName(esm_.?.name)) {
            const esm = esm_.?.withAutoVersion();
            load_module_from_cache: {
                // If the source directory doesn't have a node_modules directory, we can
                // check the global cache directory for a package.json file.
                const manager = r.getPackageManager();
                let dependency_version: Dependency.Version = {};
                let dependency_behavior: Dependency.Behavior = { prod: true };
                let string_buf = esm.version;

                // const initial_pending_tasks = manager.pending_tasks;
                let resolved_package_id: Install.PackageID = brk: {
                    // check if the package.json in the source directory was already added to the lockfile
                    // and try to look up the dependency from there
                    if (package_json := dir_info.package_json_for_dependencies) {
                        let dependencies_list: []const Dependency = [];
                        const resolve_from_lockfile = package_json.package_manager_package_id != Install.invalid_package_id;

                        if (resolve_from_lockfile) {
                            const dependencies = &manager.lockfile.packages.items(.dependencies)[package_json.package_manager_package_id];

                            // try to find this package name in the dependencies of the enclosing package
                            dependencies_list = dependencies.get(manager.lockfile.buffers.dependencies.items);
                            string_buf = manager.lockfile.buffers.string_bytes.items;
                        } else if (esm_.?.version.len == 0) {
                            // If you don't specify a version, default to the one chosen in your package.json
                            dependencies_list = package_json.dependencies.map.values();
                            string_buf = package_json.dependencies.source_buf;
                        }

                        let dependency_id: usize = 0;
                        for (dependency of dependencies_list) {
                            defer dependency_id += 1;
                            if (!strings.eqlLong(dependency.name.slice(string_buf), esm.name, true)) {
                                continue;
                            }

                            dependency_version = dependency.version;
                            dependency_behavior = dependency.behavior;

                            if (resolve_from_lockfile) {
                                const resolutions = &manager.lockfile.packages.items(.resolutions)[package_json.package_manager_package_id];

                                // found it!
                                break :brk resolutions.get(manager.lockfile.buffers.resolutions.items)[dependency_id];
                            }

                            break;
                        }
                    }

                    // If we get here, it means that the lockfile doesn't have this package at all.
                    // we know nothing
                    break :brk Install.invalid_package_id;
                };

                // Now, there are two possible states:
                // 1) We have resolved the package ID, either from the
                //    lockfile globally OR from the particular package.json
                //    dependencies list
                //
                // 2) We parsed the Dependency.Version but there is no
                //    existing resolved package ID

                // If its an exact version, we can just immediately look it up in the global cache and resolve from there
                // If the resolved package ID is _not_ invalid, we can just check

                // If this returns null, then it means we need to *resolve* the package
                // Even after resolution, we might still need to download the package
                // There are two steps here! Two steps!
                const resolution: Resolution = brk: {
                    if (resolved_package_id == Install.invalid_package_id) {
                        if (dependency_version.tag == .uninitialized) {
                            const sliced_string = Semver.SlicedString.init(esm.version, esm.version);
                            if (esm_.?.version.len > 0 && dir_info.enclosing_package_json != null && global_cache.allowVersionSpecifier()) {
                                return { failure: error.VersionSpecifierNotAllowedHere };
                            }
                            string_buf = esm.version;
                            const parsed_dep = Dependency.parse(
                                r.allocator,
                                Semver.String.init(esm.name, esm.name),
                                null,
                                esm.version,
                                &sliced_string,
                                r.log,
                                manager,
                            );
                            if (parsed_dep == null) break :load_module_from_cache;
                            dependency_version = parsed_dep.?;
                        }

                        if (id := manager.lockfile.resolvePackageFromNameAndVersion(esm.name, dependency_version)) {
                            resolved_package_id = id;
                        }
                    }

                    if (resolved_package_id != Install.invalid_package_id) {
                        break :brk manager.lockfile.packages.items(.resolution)[resolved_package_id];
                    }

                    // unsupported or not found dependency, we might need to install it to the cache
                    switch (r.enqueueDependencyToResolve(
                        dir_info.package_json_for_dependencies ?? dir_info.package_json,
                        esm,
                        dependency_behavior,
                        &resolved_package_id,
                        dependency_version,
                        string_buf,
                    )) {
                        .resolution => |res| break :brk res,
                        .pending => |pending| return { pending: pending },
                        .failure => |e| return { failure: e },
                        // this means we looked it up in the registry and the package doesn't exist or the version doesn't exist
                        .not_found => return .not_found,
                    }
                };

                const dir_path_for_resolution = manager.pathForResolution(resolved_package_id, resolution, bufs(.path_in_global_disk_cache)) catch |e| {
                    // if it's missing, we need to install it
                    if (e == error.FileNotFound) {
                        switch (manager.getPreinstallState(resolved_package_id)) {
                            .done => {
                                let path = Fs.Path.init(import_path);
                                path.is_disabled = true;
                                // this might mean the package is disabled
                                return {
                                    success: {
                                        path_pair: {
                                            primary: path,
                                        },
                                    },
                                };
                            },
                            .extract, .extracting => |st| {
                                if (!global_cache.canInstall()) {
                                    return .not_found;
                                }
                                let builder = Semver.String.Builder{};
                                esm.count(&builder);
                                builder.allocate(manager.allocator) catch unreachable;
                                const cloned = esm.clone(&builder);

                                if (st == .extract)
                                    manager.enqueuePackageForDownload(
                                        esm.name,
                                        manager.lockfile.buffers.legacyPackageToDependencyID(null, resolved_package_id) catch unreachable,
                                        resolved_package_id,
                                        resolution.value.npm.version,
                                        manager.lockfile.str(&resolution.value.npm.url),
                                        {
                                            root_request_id: 0,
                                        },
                                        null,
                                    ) catch |enqueue_download_err| return { failure: enqueue_download_err };

                                return {
                                    pending: {
                                        esm: cloned,
                                        dependency: dependency_version,
                                        resolution_id: resolved_package_id,

                                        string_buf: builder.allocatedSlice(),
                                        tag: .download,
                                    },
                                };
                            },
                            else => {},
                        }
                    }

                    return { failure: e };
                };

                const dir_info_to_use_ = r.dirInfoForResolution(dir_path_for_resolution, resolved_package_id) catch |e| {
                    return { failure: e };
                };
                if (pkg_dir_info := dir_info_to_use_) {
                        const abs_package_path = pkg_dir_info.abs_path;
                        let module_type = options.ModuleType.unknown;
                        if (package_json := pkg_dir_info.package_json) {
                            if (exports_map := package_json.exports) {
                                // The condition set is determined by the kind of import
                                const esmodule = ESModule{
                                    conditions: switch (kind) {
                                        .require, .require_resolve => r.opts.conditions.require,
                                        else => r.opts.conditions.@"import",
                                    },
                                    allocator: r.allocator,
                                    module_type: &module_type,
                                    debug_logs: if (r.debug_logs != null) &r.debug_logs.? else null,
                                };

                                // Resolve against the path "/", then join it with the absolute
                                // directory path. This is done because ESM package resolution uses
                                // URLs while our path resolution uses file system paths. We don't
                                // want problems due to Windows paths, which are very unlike URL
                                // paths. We also want to avoid any "%" characters in the absolute
                                // directory path accidentally being interpreted as URL escapes.
                                {
                                    const esm_resolution = esmodule.resolve("/", esm.subpath, exports_map.root);

                                    if (result := r.handleESMResolution(esm_resolution, abs_package_path, kind, package_json, esm.subpath)) {
                                        let result_copy = result;
                                        result_copy.is_node_module = true;
                                        return { success: result_copy };
                                    }
                                }

                                // Some popular packages forget to include the extension in their
                                // exports map, so we try again without the extension.
                                //
                                // This is useful for browser-like environments
                                // where you want a file extension in the URL
                                // pathname by convention. Vite does this.
                                //
                                // React is an example of a package that doesn't include file extensions.
                                // {
                                //     "exports": {
                                //         ".": "./index.js",
                                //         "./jsx-runtime": "./jsx-runtime.js",
                                //     }
                                // }
                                //
                                // We limit this behavior just to ".js" files.
                                const extname = std.fs.path.extension(esm.subpath);
                                if (strings.eqlComptime(extname, ".js") && esm.subpath.len > 3) {
                                    const esm_resolution = esmodule.resolve("/", esm.subpath[0 .. esm.subpath.len - 3], exports_map.root);
                                    if (result := r.handleESMResolution(esm_resolution, abs_package_path, kind, package_json, esm.subpath)) {
                                        let result_copy = result;
                                        result_copy.is_node_module = true;
                                        return { success: result_copy };
                                    }
                                }

                                // if they hid "package.json" from "exports", still allow importing it.
                                if (strings.eqlComptime(esm.subpath, "./package.json")) {
                                    return {
                                        success: {
                                            path_pair: { primary: package_json.source.path },
                                            dirname_fd: pkg_dir_info.getFileDescriptor(),
                                            file_fd: .invalid,
                                            is_node_module: package_json.source.path.isNodeModule(),
                                            package_json: package_json,
                                            dir_info: dir_info,
                                        },
                                    };
                                }

                                return .not_found;
                            }
                        }

                        const abs_path_opt = r.fs.absBufChecked(&[pkg_dir_info.abs_path, esm.subpath], bufs(.node_modules_check));
                        if (abs_path_opt == null) return .not_found;
                        const abs_path = abs_path_opt.?;
                        if (*debug := r.debug_logs) {
                            debug.addNoteFmt("Checking for a package in the directory \"{s}\"", .{abs_path});
                        }

                        let tmp = r.loadAsFileOrDirectory(abs_path, kind);
                        if (tmp != null) {
                            tmp.?.is_node_module = true;
                            return { success: tmp.? };
                        }
                    }
            }
        }

        return .not_found;
    }
    static fn dirInfoForResolution(
        r: *ThisResolver,
        dir_path_maybe_trail_slash: string,
        package_id: Install.PackageID,
    ): !(*DirInfo)? {
        assert(r.package_manager != null);

        const dir_path = strings.withoutTrailingSlashWindowsPath(dir_path_maybe_trail_slash);

        ThisResolver.assertValidCacheKey(dir_path);
        let dir_cache_info_result = bun.handleOom(r.dir_cache.getOrPut(dir_path));
        if (dir_cache_info_result.status == .exists) {
            // we've already looked up this package before
            return r.dir_cache.atIndex(dir_cache_info_result.index).?;
        }
        let rfs = &r.fs.fs;
        let cached_dir_entry_result = bun.handleOom(rfs.entries.getOrPut(dir_path));

        let dir_entries_option: *Fs.FileSystem.RealFS.EntriesOption = undefined;
        let needs_iter = true;
        let in_place: (*Fs.FileSystem.DirEntry)? = null;
        const open_dir = bun.openDirForIteration(FD.cwd(), dir_path).unwrap() catch |e| {
            // TODO: handle this error better
            r.log.addErrorFmt(
                null,
                logger.Loc.Empty,
                r.allocator,
                "Unable to open directory: {s}",
                .{bun.asByteSlice(@errorName(e))},
            ) catch unreachable;
            return e;
        };

        if (cached_entry := rfs.entries.atIndex(cached_dir_entry_result.index)) {
            if (cached_entry.* == .entries) {
                if (cached_entry.entries.generation >= r.generation) {
                    dir_entries_option = cached_entry;
                    needs_iter = false;
                } else {
                    in_place = cached_entry.entries;
                }
            }
        }

        if (needs_iter) {
            const allocator = bun.default_allocator;
            let dir_name_str: string = "";
            let existing_data_ptr: (*@FieldType(Fs.FileSystem.DirEntry, "data"))? = null;
            if (existing := in_place) {
                dir_name_str = existing.dir;
                existing_data_ptr = &existing.data;
            } else {
                dir_name_str = Fs.FileSystem.DirnameStore.instance.append(string, dir_path) catch unreachable;
            }

            let new_entry = Fs.FileSystem.DirEntry.init(
                dir_name_str,
                r.generation,
            );

            let dir_iterator = bun.iterateDir(open_dir);
            while (_value := dir_iterator.next().unwrap() catch null) {
                new_entry.addEntry(
                    existing_data_ptr,
                    &_value,
                    allocator,
                    void,
                    undefined,
                ) catch unreachable;
            }
            if (existing := in_place) {
                existing.data.clearAndFree(allocator);
            }

            let dir_entries_ptr = in_place ?? allocator.create(Fs.FileSystem.DirEntry) catch unreachable;
            dir_entries_ptr.* = new_entry;

            if (r.store_fd) {
                dir_entries_ptr.fd = open_dir;
            }

            bun.fs.debug("readdir({f}, {s}) = {d}", .{ open_dir, dir_path, dir_entries_ptr.data.count() });

            dir_entries_option = rfs.entries.put(&cached_dir_entry_result, {
                entries: dir_entries_ptr,
            }) catch unreachable;
        }

        // We must initialize it as empty so that the result index is correct.
        // This is important so that browser_scope has a valid index.
        const dir_info_ptr = r.dir_cache.put(&dir_cache_info_result, {}) catch unreachable;

        // `dir_path` is a slice into the threadlocal `bufs(.path_in_global_disk_cache)` buffer,
        // which gets overwritten on the next auto-install resolution. `dirInfoUncached` stores
        // its `path` argument directly as `DirInfo.abs_path` in the permanent `dir_cache`, so
        // pass the interned copy from `DirEntry.dir` (always backed by `DirnameStore`) instead.
        try r.dirInfoUncached(
            dir_info_ptr,
            dir_entries_option.entries.dir,
            dir_entries_option,
            dir_cache_info_result,
            cached_dir_entry_result.index,
            // Packages in the global disk cache are top-level, we shouldn't try
            // to check for a parent package.json
            null,
            allocators.NotFound,
            open_dir,
            package_id,
        );
        return dir_info_ptr;
    }

    const DependencyToResolve = union(enum) {
        not_found: void,
        pending: PendingResolution,
        failure: anyerror,
        resolution: Resolution,
    };

    static fn enqueueDependencyToResolve(
        r: *ThisResolver,
        package_json_: (*PackageJSON)?,
        esm: ESModule.Package,
        behavior: Dependency.Behavior,
        input_package_id_: *Install.PackageID,
        version: Dependency.Version,
        version_buf: []const u8,
    ): DependencyToResolve {
        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Enqueueing pending dependency \"{s}@{s}\"", .{ esm.name, esm.version });
        }

        const input_package_id = input_package_id_.*;
        let pm = r.getPackageManager();
        if (comptime Environment.allow_assert) {
            // we should never be trying to resolve a dependency that is already resolved
            assert(pm.lockfile.resolvePackageFromNameAndVersion(esm.name, version) == null);
        }

        // Add the containing package to the lockfile

        let package: Package = {};

        const is_main = pm.lockfile.packages.len == 0 && input_package_id == Install.invalid_package_id;
        if (is_main) {
            if (package_json := package_json_) {
                package = Package.fromPackageJSON(
                    pm.lockfile,
                    pm,
                    package_json,
                    Install.Features{
                        dev_dependencies: true,
                        is_main: true,
                        dependencies: true,
                        optional_dependencies: true,
                    },
                ) catch |e| {
                    return { failure: e };
                };
                package.@"meta".setHasInstallScript(package.scripts.hasAny());
                package = pm.lockfile.appendPackage(package) catch |e| {
                    return { failure: e };
                };
                package_json.package_manager_package_id = package.@"meta".id;
            } else {
                // we're resolving an unknown package
                // the unknown package is the root package
                package = Package{
                    name: Semver.String.@"from"(""),
                    resolution: {
                        tag: .root,
                        value: { root: undefined },
                    },
                };
                package.@"meta".setHasInstallScript(package.scripts.hasAny());
                package = pm.lockfile.appendPackage(package) catch |e| {
                    return { failure: e };
                };
            }
        }

        if (r.opts.prefer_offline_install) {
            if (package_id := pm.resolveFromDiskCache(esm.name, version)) {
                input_package_id_.* = package_id;
                return { resolution: pm.lockfile.packages.items(.resolution)[package_id] };
            }
        }

        if (input_package_id == Install.invalid_package_id || input_package_id == 0) {

            // All packages are enqueued to the root
            // because we download all the npm package dependencies
            switch (pm.enqueueDependencyToRoot(esm.name, &version, version_buf, behavior)) {
                .resolution => |result| {
                    input_package_id_.* = result.package_id;
                    return { resolution: result.resolution };
                },
                .pending => |id| {
                    let builder = Semver.String.Builder{};
                    esm.count(&builder);
                    builder.allocate(pm.allocator) catch unreachable;
                    const cloned = esm.clone(&builder);

                    return {
                        pending: {
                            esm: cloned,
                            dependency: version,
                            root_dependency_id: id,
                            string_buf: builder.allocatedSlice(),
                            tag: .resolve,
                        },
                    };
                },
                .not_found => {
                    return .not_found;
                },
                .failure => |e| {
                    return { failure: e };
                },
            }
        }

        bun.unreachablePanic("TODO: implement enqueueDependencyToResolve for non-root packages", {});
    }

    static fn handleESMResolution(r: *ThisResolver, esm_resolution_: ESModule.Resolution, abs_package_path: string, kind: ast.ImportKind, package_json: *PackageJSON, package_subpath: string): MatchResult? {
        let esm_resolution = esm_resolution_;
        if (!((esm_resolution.status == .Inexact || esm_resolution.status == .Exact || esm_resolution.status == .ExactEndsWithStar) &&
            esm_resolution.path.len > 0 && esm_resolution.path[0] == std.fs.path.sep))
            return null;

        const abs_esm_path_opt = r.fs.absBufChecked(
            &[abs_package_path, strings.withoutLeadingPathSeparator(esm_resolution.path)],
            bufs(.esm_absolute_package_path_joined),
        );
        if (abs_esm_path_opt == null) {
            esm_resolution.status = .ModuleNotFound;
            return null;
        }
        const abs_esm_path: string = abs_esm_path_opt.?;

        let missing_suffix: string = "";

        switch (esm_resolution.status) {
            .Exact, .ExactEndsWithStar => {
                const resolved_dir_info_opt = r.dirInfoCached(std.fs.path.dirname(abs_esm_path).?) catch null;
                if (resolved_dir_info_opt == null) {
                    esm_resolution.status = .ModuleNotFound;
                    return null;
                }
                const resolved_dir_info = resolved_dir_info_opt.?;

                const entries_opt = resolved_dir_info.getEntries(r.generation);
                if (entries_opt == null) {
                    esm_resolution.status = .ModuleNotFound;
                    return null;
                }
                const entries = entries_opt.?;

                const extension_order = if (kind == .at || kind == .at_conditional)
                    r.extension_order
                else
                    r.opts.extension_order.kind(kind, resolved_dir_info.isInsideNodeModules());

                const base = std.fs.path.basename(abs_esm_path);
                const entry_query_opt = entries.get(base);
                if (entry_query_opt == null) {
                    const ends_with_star = esm_resolution.status == .ExactEndsWithStar;
                    esm_resolution.status = .ModuleNotFound;

                    // Try to have a friendly error message if people forget the extension
                    if (ends_with_star) {
                        bun.copy(u8, bufs(.load_as_file), base);
                        for (ext of extension_order) {
                            let file_name = bufs(.load_as_file)[0 .. base.len + ext.len];
                            bun.copy(u8, file_name[base.len..], ext);
                            if (entries.get(file_name) != null) {
                                if (*debug := r.debug_logs) {
                                    const parts = [package_json.name, package_subpath];
                                    debug.addNoteFmt("The import {s} is missing the extension {s}", .{ ResolvePath.join(parts, .auto), ext });
                                }
                                esm_resolution.status = .ModuleNotFoundMissingExtension;
                                missing_suffix = ext;
                                break;
                            }
                        }
                    }
                    return null;
                }
                const entry_query = entry_query_opt.?;

                if (entry_query.entry.kind(&r.fs.fs, r.store_fd) == .dir) {
                    const ends_with_star = esm_resolution.status == .ExactEndsWithStar;
                    esm_resolution.status = .UnsupportedDirectoryImport;

                    // Try to have a friendly error message if people forget the "/index.js" suffix
                    if (ends_with_star) {
                        if (dir_info := r.dirInfoCached(abs_esm_path) catch null) {
                            if (dir_entries := dir_info.getEntries(r.generation)) {
                                const index = "index";
                                bun.copy(u8, bufs(.load_as_file), index);
                                for (ext of extension_order) {
                                    let file_name = bufs(.load_as_file)[0 .. index.len + ext.len];
                                    bun.copy(u8, file_name[index.len..], ext);
                                    const index_query = dir_entries.get(file_name);
                                    if (index_query != null && index_query.?.entry.kind(&r.fs.fs, r.store_fd) == .file) {
                                        if (*debug := r.debug_logs) {
                                            missing_suffix = std.fmt.allocPrint(r.allocator, "/{s}", .{file_name}) catch unreachable;
                                            defer r.allocator.free(missing_suffix);
                                            const parts = [package_json.name, package_subpath];
                                            debug.addNoteFmt("The import {s} is missing the suffix {s}", .{ ResolvePath.join(parts, .auto), missing_suffix });
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    return null;
                }

                const absolute_out_path = brk: {
                    if (entry_query.entry.abs_path.isEmpty()) {
                        entry_query.entry.abs_path =
                            PathString.init(r.fs.dirname_store.append(@TypeOf(abs_esm_path), abs_esm_path) catch unreachable);
                    }
                    break :brk entry_query.entry.abs_path.slice();
                };
                let module_type = options.ModuleType.unknown;
                if (pkg := resolved_dir_info.package_json) {
                    module_type = pkg.module_type;
                }

                return MatchResult{
                    path_pair: PathPair{
                        primary: Path.initWithNamespace(absolute_out_path, "file"),
                    },
                    dirname_fd: entries.fd,
                    file_fd: entry_query.entry.cache.fd,
                    dir_info: resolved_dir_info,
                    diff_case: entry_query.diff_case,
                    is_node_module: true,
                    package_json: resolved_dir_info.package_json ?? package_json,
                    module_type: module_type,
                };
            },
            .Inexact => {
                // If this was resolved against an expansion key ending in a "/"
                // instead of a "*", we need to try CommonJS-style implicit
                // extension and/or directory detection.
                if (res := r.loadAsFileOrDirectory(abs_esm_path, kind)) {
                    let res_copy = res;
                    res_copy.is_node_module = true;
                    res_copy.package_json = res.package_json ?? package_json;
                    return res_copy;
                }
                esm_resolution.status = .ModuleNotFound;
                return null;
            },
            else => unreachable,
        }
    }

    static fn resolveWithoutRemapping(
        r: *ThisResolver,
        source_dir_info: *DirInfo,
        import_path: string,
        kind: ast.ImportKind,
        global_cache: GlobalCache,
    ): MatchResult.Union {
        if (isPackagePath(import_path)) {
            return r.loadNodeModules(import_path, kind, source_dir_info, global_cache, false);
        } else {
            const resolved_opt = r.fs.absBufChecked(&[source_dir_info.abs_path, import_path], bufs(.resolve_without_remapping));
            if (resolved_opt == null) return .not_found;
            const resolved = resolved_opt.?;
            if (result := r.loadAsFileOrDirectory(resolved, kind)) {
                return { success: result };
            }
            return .not_found;
        }
    }

    static fn parseTSConfig(
        r: *ThisResolver,
        file: string,
        dirname_fd: FD,
    ): !(*TSConfigJSON)? {
        // Since tsconfig.json is cached permanently, in our DirEntries cache
        // we must use the global allocator
        let entry = try r.caches.fs.readFileWithAllocator(
            bun.default_allocator,
            r.fs,
            file,
            dirname_fd,
            false,
            null,
        );
        defer * = entry.closeFD();

        // The file name needs to be persistent because it can have errors
        // and if those errors need to print the filename
        // then it will be undefined memory if we parse another tsconfig.json late
        const key_path = Fs.Path.init(r.fs.dirname_store.append(string, file) catch unreachable);

        const source = &logger.Source.initPathString(key_path.text, entry.contents);
        const file_dir = source.path.sourceDir();

        const parsed_ts = TSConfigJSON.parse(bun.default_allocator, r.log, source, &r.caches.json)?;
        if (parsed_ts == null) return null;
        let result = parsed_ts.?;

        if (result.hasBaseURL()) {

            // this might leak
            if (!std.fs.path.isAbsolute(result.base_url)) {
                const paths = [file_dir, result.base_url];
                result.base_url = r.fs.dirname_store.append(string, r.fs.absBuf(&paths, bufs(.tsconfig_base_url))) catch unreachable;
            }
        }

        if (result.paths.count() > 0 && (result.base_url_for_paths.len == 0 || !std.fs.path.isAbsolute(result.base_url_for_paths))) {
            // this might leak
            const paths = [file_dir, result.base_url];
            result.base_url_for_paths = r.fs.dirname_store.append(string, r.fs.absBuf(&paths, bufs(.tsconfig_base_url))) catch unreachable;
        }

        return result;
    }

    fn binDirs(): []const string {
        if (@sizeOf(@TypeOf(_)) == 0) unreachable;
        if (!bin_folders_loaded) return [];
        return bin_folders.constSlice();
    }

    static fn parsePackageJSON(
        r: *ThisResolver,
        file: string,
        dirname_fd: FD,
        package_id: Install.PackageID?,
        comptime allow_dependencies: bool,
    ): !(*PackageJSON)? {
        let pkg: PackageJSON = undefined;
        if (!r.care_about_scripts) {
            const parsed = PackageJSON.parse(
                r,
                file,
                dirname_fd,
                package_id,
                .ignore_scripts,
                if (allow_dependencies) .local else .none,
            );
            if (p := parsed) {
                pkg = p;
            } else {
                return null;
            }
        } else {
            const parsed = PackageJSON.parse(
                r,
                file,
                dirname_fd,
                package_id,
                .include_scripts,
                if (allow_dependencies) .local else .none,
            );
            if (p := parsed) {
                pkg = p;
            } else {
                return null;
            }
        }

        return PackageJSON.new(pkg);
    }

    static fn dirInfoCached(r: *ThisResolver, path: string): !(*DirInfo)? {
        return r.dirInfoCachedMaybeLog(path, true, true)?;
    }

    static fn readDirInfo(r: *ThisResolver, path: string): !(*DirInfo)? {
        return r.dirInfoCachedMaybeLog(path, false, true)?;
    }

    /// Like `readDirInfo`, but returns `null` instead of throwing an error.
    static fn readDirInfoIgnoreError(r: *ThisResolver, path: string): (*const DirInfo)? {
        return r.dirInfoCachedMaybeLog(path, false, true) catch null;
    }

    inline fn isDotSlash(path: string): bool {
        return switch (Environment.os) {
            else => strings.eqlComptime(path, "./"),
            .windows => path.len == 2 && path[0] == '.' && strings.charIsAnySlash(path[1]),
        };
    }

    static fn dirInfoCachedMaybeLog(r: *ThisResolver, raw_input_path: string, comptime enable_logging: bool, comptime follow_symlinks: bool): !(*DirInfo)? {
        r.mutex.lock();
        defer r.mutex.unlock();
        let input_path = raw_input_path;

        if (isDotSlash(input_path) || strings.eqlComptime(input_path, ".")) {
            input_path = r.fs.top_level_dir;
        }

        // A path longer than MAX_PATH_BYTES cannot name a real directory.
        // Bailing here also prevents overflowing `dir_info_uncached_path`
        // below when called with user-controlled absolute import paths.
        if (input_path.len > bun.MAX_PATH_BYTES) return null;

        if (comptime Environment.isWindows) {
            let win32_normalized_dir_info_cache_buf = bufs(.win32_normalized_dir_info_cache);
            input_path = r.fs.normalizeBuf(win32_normalized_dir_info_cache_buf, input_path);
            // kind of a patch on the fact normalizeBuf isn't 100% perfect what we want
            if ((input_path.len == 2 && input_path[1] == ':') ||
                (input_path.len == 3 && input_path[1] == ':' && input_path[2] == '.'))
            {
                bun.unsafeAssert(input_path.ptr == win32_normalized_dir_info_cache_buf);
                win32_normalized_dir_info_cache_buf[2] = '\\';
                input_path.len = 3;
            }

            // Filter out \\hello\, a UNC server path but without a share.
            // When there isn't a share name, such path is not considered to exist.
            if (bun.strings.hasPrefixComptime(input_path, "\\\\")) {
                const first_slash = bun.strings.indexOfChar(input_path[2..], '\\') ??
                    return null;
                * = bun.strings.indexOfChar(input_path[2 + first_slash ..], '\\') ??
                    return null;
            }
        }

        bun.assertf(std.fs.path.isAbsolute(input_path), "cannot resolve DirInfo for non-absolute path: {s}", .{input_path});

        const path_without_trailing_slash = strings.withoutTrailingSlashWindowsPath(input_path);
        ThisResolver.assertValidCacheKey(path_without_trailing_slash);
        const top_result = r.dir_cache.getOrPut(path_without_trailing_slash)?;
        if (top_result.status != .unknown) {
            return r.dir_cache.atIndex(top_result.index);
        }

        let dir_info_uncached_path_buf = bufs(.dir_info_uncached_path);

        let i: i32 = 1;
        bun.copy(u8, dir_info_uncached_path_buf, input_path);
        let path = dir_info_uncached_path_buf[0..input_path.len];

        bufs(.dir_entry_paths_to_resolve)[0] = DirEntryResolveQueueItem{ .result = top_result, .unsafe_path = path, .safe_path = "" };
        let top = Dirname.dirname(path);

        let top_parent: allocators.Result = allocators.Result{
            index: allocators.NotFound,
            hash: 0,
            status: .not_found,
        };
        const root_path = if (Environment.isWindows)
            bun.strings.withoutTrailingSlashWindowsPath(ResolvePath.windowsFilesystemRoot(path))
        else
            // we cannot just use "/"
            // we will write to the buffer past the ptr len so it must be a non-const buffer
            path[0..1];
        ThisResolver.assertValidCacheKey(root_path);

        const rfs = &r.fs.fs;

        rfs.entries_mutex.lock();
        defer rfs.entries_mutex.unlock();

        while (top.len > root_path.len) : (top = Dirname.dirname(top)) {
            assert(top.ptr == root_path.ptr);
            const result = r.dir_cache.getOrPut(top)?;

            if (result.status != .unknown) {
                top_parent = result;
                break;
            }
            // Path has more uncached components than our fixed queue can hold.
            // This only happens for user-controlled absolute import paths with
            // hundreds of short components — no real directory is this deep.
            if (@as(usize, @intCast(i)) >= bufs(.dir_entry_paths_to_resolve).len) return null;
            bufs(.dir_entry_paths_to_resolve)[@as(usize, @intCast(i))] = DirEntryResolveQueueItem{
                unsafe_path: top,
                result: result,
                fd: .invalid,
            };

            if (top_entry := rfs.entries.get(top)) {
                switch (top_entry.*) {
                    .entries => {
                        bufs(.dir_entry_paths_to_resolve)[@as(usize, @intCast(i))].safe_path = top_entry.entries.dir;
                        bufs(.dir_entry_paths_to_resolve)[@as(usize, @intCast(i))].fd = top_entry.entries.fd;
                    },
                    .err => |e| {
                        debuglog("Failed to load DirEntry {s}  {s} - {s}", .{ top, @errorName(e.original_err), @errorName(e.canonical_error) });
                        break;
                    },
                }
            }
            i += 1;
        }

        if (strings.eql(top, root_path)) {
            const result = r.dir_cache.getOrPut(root_path)?;
            if (result.status != .unknown) {
                top_parent = result;
            } else {
                bufs(.dir_entry_paths_to_resolve)[@as(usize, @intCast(i))] = DirEntryResolveQueueItem{
                    unsafe_path: root_path,
                    result: result,
                    fd: .invalid,
                };
                if (top_entry := rfs.entries.get(top)) {
                    switch (top_entry.*) {
                        .entries => {
                            bufs(.dir_entry_paths_to_resolve)[@as(usize, @intCast(i))].safe_path = top_entry.entries.dir;
                            bufs(.dir_entry_paths_to_resolve)[@as(usize, @intCast(i))].fd = top_entry.entries.fd;
                        },
                        .err => |e| {
                            debuglog("Failed to load DirEntry {s}  {s} - {s}", .{ top, @errorName(e.original_err), @errorName(e.canonical_error) });
                            return e.canonical_error;
                        },
                    }
                }

                i += 1;
            }
        }

        let queue_slice: []DirEntryResolveQueueItem = bufs(.dir_entry_paths_to_resolve)[0..@as(usize, @intCast(i))];
        if (Environment.allow_assert) assert(queue_slice.len > 0);
        let open_dir_count: usize = 0;

        // When this function halts, any item not processed means it's not found.
        defer {
            if (open_dir_count > 0 && (!r.store_fd || r.fs.fs.needToCloseFiles())) {
                const open_dirs = bufs(.open_dirs)[0..open_dir_count];
                for (open_dir of open_dirs) {
                    open_dir.close();
                }
            }
        }

        // We want to walk in a straight line from the topmost directory to the desired directory
        // For each directory we visit, we get the entries, but not traverse into child directories
        // (unless those child directories are in the queue)
        // We go top-down instead of bottom-up to increase odds of reusing previously open file handles
        // "/home/jarred/Code/node_modules/react/cjs/react.development.js"
        //       ^
        // If we start there, we will traverse all of /home/jarred, including e.g. /home/jarred/Downloads
        // which is completely irrelevant.

        // After much experimentation...
        // - fts_open is not the fastest way to read directories. fts actually just uses readdir!!
        // - remember
        let _safe_path: string? = null;

        // Start at the top.
        while (queue_slice.len > 0) {
            let queue_top = queue_slice[queue_slice.len - 1];
            defer top_parent = queue_top.result;
            queue_slice.len -= 1;

            const open_dir: FD = if (queue_top.fd.isValid())
                queue_top.fd
            else open_dir: {
                // This saves us N copies of .toPosixPath
                // which was likely the perf gain from resolving directories relative to the parent directory, anyway.
                const prev_char = path.ptr[queue_top.unsafe_path.len];
                path.ptr[queue_top.unsafe_path.len] = 0;
                defer path.ptr[queue_top.unsafe_path.len] = prev_char;
                const sentinel = path.ptr[0..queue_top.unsafe_path.len :0];

                const open_req = if (comptime Environment.isPosix) open_req: {
                    const dir_result = std.fs.openDirAbsoluteZ(
                        sentinel,
                        { no_follow: !follow_symlinks, iterate: true },
                    ) catch |e| break :open_req e;
                    break :open_req FD.fromStdDir(dir_result);
                } else if (comptime Environment.isWindows) open_req: {
                    const dirfd_result = bun.sys.openDirAtWindowsA(bun.invalid_fd, sentinel, {
                        iterable: true,
                        no_follow: !follow_symlinks,
                        read_only: true,
                    });
                    const res = dirfd_result.unwrap() catch |e| break :open_req e;
                    break :open_req res;
                };

                bun.fs.debug("open({s})", .{sentinel});

                break :open_dir open_req catch |e| switch (@as(anyerror, e)) {
                    // Ignore "ENOTDIR" here so that calling "ReadDirectory" on a file behaves
                    // as if there is nothing there at all instead of causing an error due to
                    // the directory actually being a file. This is a workaround for situations
                    // where people try to import from a path containing a file as a parent
                    // directory. The "pnpm" package manager generates a faulty "NODE_PATH"
                    // list which contains such paths and treating them as missing means we just
                    // ignore them during path resolution.
                    error.ENOTDIR, error.IsDir, error.NotDir => return null,
                    else => {
                        const cached_dir_entry_result = rfs.entries.getOrPut(queue_top.unsafe_path) catch unreachable;
                        // If we don't properly cache not found, then we repeatedly attempt to open the same directories, which causes a perf trace that looks like this stupidity;
                        //
                        //   openat(dfd: CWD, filename: "node_modules/react", flags: RDONLY|DIRECTORY) = -1 ENOENT (No such file or directory)
                        //   openat(dfd: CWD, filename: "node_modules/react/", flags: RDONLY|CLOEXEC|DIRECTORY) = -1 ENOENT (No such file or directory)
                        //   openat(dfd: CWD, filename: "node_modules/react", flags: RDONLY|CLOEXEC|DIRECTORY) = -1 ENOENT (No such file or directory)
                        //   openat(dfd: CWD, filename: "node_modules/react", flags: RDONLY|CLOEXEC|DIRECTORY) = -1 ENOENT (No such file or directory)
                        //
                        r.dir_cache.markNotFound(queue_top.result);
                        rfs.entries.markNotFound(cached_dir_entry_result);
                        switch (@as(anyerror, e)) {
                            error.ENOENT, error.FileNotFound => {},
                            else => {
                                if (comptime enable_logging) {
                                    const pretty = queue_top.unsafe_path;

                                    r.log.addErrorFmt(
                                        null,
                                        logger.Loc{},
                                        r.allocator,
                                        "Cannot read directory \"{s}\": {s}",
                                        .{
                                            pretty,
                                            @errorName(e),
                                        },
                                    ) catch {};
                                }
                            },
                        }

                        return null;
                    },
                };
            };

            if (!queue_top.fd.isValid()) {
                Fs.FileSystem.setMaxFd(open_dir.cast());
                // these objects mostly just wrap the file descriptor, so it's fine to keep it.
                bufs(.open_dirs)[open_dir_count] = open_dir;
                open_dir_count += 1;
            }

            const dir_path = if (queue_top.safe_path.len > 0) queue_top.safe_path else brk: {

                // ensure trailing slash
                if (_safe_path == null) {
                    // Now that we've opened the topmost directory successfully, it's reasonable to store the slice.
                    if (path[path.len - 1] != std.fs.path.sep) {
                        _safe_path = r.fs.dirname_store.print("{s}{s}", .{ path, std.fs.path.sep_str })?;
                    } else {
                        _safe_path = r.fs.dirname_store.append(string, path)?;
                    }
                }

                const safe_path = _safe_path.?;

                const dir_path_i = std.mem.indexOf(u8, safe_path, queue_top.unsafe_path) ?? unreachable;
                let end = dir_path_i +
                    queue_top.unsafe_path.len;

                // Directories must always end in a trailing slash or else various bugs can occur.
                // This covers "what happens when the trailing"
                end += @as(usize, @intCast(@intFromBool(safe_path.len > end && end > 0 && safe_path[end - 1] != std.fs.path.sep && safe_path[end] == std.fs.path.sep)));
                break :brk safe_path[dir_path_i..end];
            };

            let cached_dir_entry_result = rfs.entries.getOrPut(dir_path) catch unreachable;

            let dir_entries_option: *Fs.FileSystem.RealFS.EntriesOption = undefined;
            let needs_iter: bool = true;
            let in_place: (*Fs.FileSystem.DirEntry)? = null;

            if (cached_entry := rfs.entries.atIndex(cached_dir_entry_result.index)) {
                if (cached_entry.* == .entries) {
                    if (cached_entry.entries.generation >= r.generation) {
                        dir_entries_option = cached_entry;
                        needs_iter = false;
                    } else {
                        in_place = cached_entry.entries;
                    }
                }
            }

            if (needs_iter) {
                const allocator = bun.default_allocator;
                let dir_name_str: string = "";
                let existing_data_ptr: (*@FieldType(Fs.FileSystem.DirEntry, "data"))? = null;
                if (existing := in_place) {
                    dir_name_str = existing.dir;
                    existing_data_ptr = &existing.data;
                } else {
                    dir_name_str = Fs.FileSystem.DirnameStore.instance.append(string, dir_path) catch unreachable;
                }

                let new_entry = Fs.FileSystem.DirEntry.init(
                    dir_name_str,
                    r.generation,
                );

                let dir_iterator = bun.iterateDir(open_dir);
                while (_value := dir_iterator.next().unwrap() catch null) {
                    new_entry.addEntry(
                        existing_data_ptr,
                        &_value,
                        allocator,
                        void,
                        undefined,
                    ) catch unreachable;
                }
                if (existing := in_place) {
                    existing.data.clearAndFree(allocator);
                }
                new_entry.fd = if (r.store_fd) open_dir else .invalid;
                let dir_entries_ptr = in_place ?? allocator.create(Fs.FileSystem.DirEntry) catch unreachable;
                dir_entries_ptr.* = new_entry;
                dir_entries_option = try rfs.entries.put(&cached_dir_entry_result, {
                    entries: dir_entries_ptr,
                });
                bun.fs.debug("readdir({f}, {s}) = {d}", .{ open_dir, dir_path, dir_entries_ptr.data.count() });
            }

            // We must initialize it as empty so that the result index is correct.
            // This is important so that browser_scope has a valid index.
            const dir_info_ptr = r.dir_cache.put(&queue_top.result, DirInfo{})?;

            try r.dirInfoUncached(
                dir_info_ptr,
                dir_path,
                dir_entries_option,
                queue_top.result,
                cached_dir_entry_result.index,
                r.dir_cache.atIndex(top_parent.index),
                top_parent.index,
                open_dir,
                null,
            );

            if (queue_slice.len == 0) {
                return dir_info_ptr;

                // Is the directory we're searching for actually a file?
            } else if (queue_slice.len == 1) {
                // const next_in_queue = queue_slice[0];
                // const next_basename = std.fs.path.basename(next_in_queue.unsafe_path);
                // if (dir_info_ptr.getEntries(r.generation)) |entries| {
                //     if (entries.get(next_basename) != null) {
                //         return null;
                //     }
                // }
            }
        }

        unreachable;
    }

    // This closely follows the behavior of "tryLoadModuleUsingPaths()" in the
    // official TypeScript compiler
    static fn matchTSConfigPaths(r: *ThisResolver, tsconfig: *const TSConfigJSON, path: string, kind: ast.ImportKind): MatchResult? {
        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Matching \"{s}\" against \"paths\" in \"{s}\"", .{ path, tsconfig.abs_path });
        }

        let abs_base_url = tsconfig.base_url_for_paths;

        // The explicit base URL should take precedence over the implicit base URL
        // if present. This matters when a tsconfig.json file overrides "baseUrl"
        // from another extended tsconfig.json file but doesn't override "paths".
        if (tsconfig.hasBaseURL()) {
            abs_base_url = tsconfig.base_url;
        }

        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Using \"{s}\" as \"baseURL\"", .{abs_base_url});
        }

        // Check for exact matches first
        {
            let iter = tsconfig.paths.iterator();
            while (entry := iter.next()) {
                const key = entry.key_ptr.*;

                if (strings.eqlLong(key, path, true)) {
                    for (original_path of entry.value_ptr.*) {
                        let absolute_original_path = original_path;

                        if (!std.fs.path.isAbsolute(absolute_original_path)) {
                            const parts = [abs_base_url, original_path];
                            absolute_original_path = r.fs.absBuf(&parts, bufs(.tsconfig_path_abs));
                        }

                        if (res := r.loadAsFileOrDirectory(absolute_original_path, kind)) {
                            return res;
                        }
                    }
                }
            }
        }

        // TSConfigMatch defined at top level

        let longest_match: TSConfigMatch = undefined;
        let longest_match_prefix_length: i32 = -1;
        let longest_match_suffix_length: i32 = -1;

        let iter = tsconfig.paths.iterator();
        while (entry := iter.next()) {
            const key = entry.key_ptr.*;
            const original_paths = entry.value_ptr.*;

            if (star := strings.indexOfChar(key, '*')) {
                const prefix = if (star == 0) "" else key[0..star];
                const suffix = if (star == key.len - 1) "" else key[star + 1 ..];

                // Find the match with the longest prefix. If two matches have the same
                // prefix length, pick the one with the longest suffix. This second edge
                // case isn't handled by the TypeScript compiler, but we handle it
                // because we want the output to always be deterministic
                if (strings.startsWith(path, prefix) &&
                    strings.endsWith(path, suffix) &&
                    (prefix.len > longest_match_prefix_length ||
                        (prefix.len == longest_match_prefix_length && suffix.len > longest_match_suffix_length)))
                {
                    longest_match_prefix_length = @as(i32, @intCast(prefix.len));
                    longest_match_suffix_length = @as(i32, @intCast(suffix.len));
                    longest_match = TSConfigMatch{ .prefix = prefix, .suffix = suffix, .original_paths = original_paths };
                }
            }
        }

        // If there is at least one match, only consider the one with the longest
        // prefix. This matches the behavior of the TypeScript compiler.
        if (longest_match_prefix_length != -1) {
            if (*debug := r.debug_logs) {
                debug.addNoteFmt("Found a fuzzy match for \"{s}*{s}\" in \"paths\"", .{ longest_match.prefix, longest_match.suffix });
            }

            for (original_path of longest_match.original_paths) {
                // Swap out the "*" in the original path for whatever the "*" matched
                const matched_text = path[longest_match.prefix.len .. path.len - longest_match.suffix.len];

                const total_length: u32? = strings.indexOfChar(original_path, '*');
                const prefix_len = total_length ?? original_path.len;
                let prefix_parts = [abs_base_url, original_path[0..prefix_len]];

                // Concatenate the matched text with the suffix from the wildcard path
                let matched_text_with_suffix = bufs(.tsconfig_match_full_buf3);
                let matched_text_with_suffix_len: usize = 0;
                if (total_length != null) {
                    const suffix = std.mem.trimLeft(u8, original_path[total_length.? ..], "*");
                    matched_text_with_suffix_len = matched_text.len + suffix.len;
                    if (matched_text_with_suffix_len > matched_text_with_suffix.len) continue;
                    bun.concat(u8, matched_text_with_suffix, &.{ matched_text, suffix });
                }

                // 1. Normalize the base path
                // so that "/Users/foo/project/", "../components/*" => "/Users/foo/components/""
                const prefix_opt = r.fs.absBufChecked(&prefix_parts, bufs(.tsconfig_match_full_buf2));
                if (prefix_opt == null) continue;
                const prefix = prefix_opt.?;

                // 2. Join the new base path with the matched result
                // so that "/Users/foo/components/", "/foo/bar" => /Users/foo/components/foo/bar
                let parts: [3][]const u8 = [
                    prefix,
                    if (matched_text_with_suffix_len > 0) std.mem.trimLeft(u8, matched_text_with_suffix[0..matched_text_with_suffix_len], "/") else "",
                    std.mem.trimLeft(u8, longest_match.suffix, "/"),
                ];
                const absolute_original_path_opt = r.fs.absBufChecked(
                    &parts,
                    bufs(.tsconfig_match_full_buf),
                );
                if (absolute_original_path_opt == null) continue;
                const absolute_original_path = absolute_original_path_opt.?;

                if (res := r.loadAsFileOrDirectory(absolute_original_path, kind)) {
                    return res;
                }
            }
        }

        return null;
    }

    static fn loadPackageImports(r: *ThisResolver, import_path: string, dir_info: *DirInfo, kind: ast.ImportKind, global_cache: GlobalCache): MatchResult.Union {
        const package_json = dir_info.package_json.?;
        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Looking for {s} in \"imports\" map in {s}", .{ import_path, package_json.source.path.text });
            debug.increaseIndent();
            defer debug.decreaseIndent();
        }
        const imports_map = package_json.imports.?;

        if (import_path.len == 1 || strings.hasPrefix(import_path, "#/")) {
            if (*debug := r.debug_logs) {
                debug.addNoteFmt("The path \"{s}\" must not equal \"#\" and must not start with \"#/\"", .{import_path});
            }
            return .not_found;
        }
        let module_type = options.ModuleType.unknown;

        const esmodule = ESModule{
            conditions: switch (kind) {
                .require, .require_resolve => r.opts.conditions.require,
                else => r.opts.conditions.@"import",
            },
            allocator: r.allocator,
            debug_logs: if (r.debug_logs != null) &r.debug_logs.? else null,
            module_type: &module_type,
        };

        const esm_resolution = esmodule.resolveImports(import_path, imports_map.root);

        if (esm_resolution.status == .PackageResolve) {
            // https://github.com/oven-sh/bun/issues/4972
            // Resolve a subpath import to a Bun or Node.js builtin
            //
            // Code example:
            //
            //     import { readFileSync } from '#fs';
            //
            // package.json:
            //
            //     "imports": {
            //       "#fs": "node:fs"
            //     }
            //
            if (r.opts.mark_builtins_as_external || r.opts.target.isBun()) {
                if (alias := jsc.ModuleLoader.HardcodedModule.Alias.get(esm_resolution.path, r.opts.target, {})) {
                    return {
                        success: {
                            path_pair: { primary: bun.fs.Path.init(alias.path) },
                            is_external: true,
                        },
                    };
                }
            }

            return r.loadNodeModules(
                esm_resolution.path,
                kind,
                dir_info,
                global_cache,
                true,
            );
        }

        if (result := r.handleESMResolution(esm_resolution, package_json.source.path.name.dir, kind, package_json, "")) {
            return { success: result };
        }

        return .not_found;
    }

    static fn checkBrowserMap(
        r: *ThisResolver,
        dir_info: *const DirInfo,
        input_path_: string,
        comptime kind: BrowserMapPath.Kind,
    ): string? {
        if (dir_info.package_json == null) return null;
        const package_json = dir_info.package_json.?;
        const browser_map = package_json.browser_map;

        if (browser_map.count() == 0) return null;

        let input_path = input_path_;

        if (comptime kind == .AbsolutePath) {
            const abs_path = dir_info.abs_path;
            // Turn absolute paths into paths relative to the "browser" map location
            if (!strings.startsWith(input_path, abs_path)) {
                return null;
            }

            input_path = input_path[abs_path.len..];
        }

        if (input_path.len == 0 || (input_path.len == 1 && (input_path[0] == '.' || input_path[0] == std.fs.path.sep))) {
            // No bundler supports remapping ".", so we don't either
            return null;
        }

        // Normalize the path so we can compare against it without getting confused by "./"
        const cleaned = r.fs.normalizeBuf(bufs(.check_browser_map), input_path);

        if (cleaned.len == 1 && cleaned[0] == '.') {
            // No bundler supports remapping ".", so we don't either
            return null;
        }

        let checker = BrowserMapPath{
            remapped: "",
            cleaned: cleaned,
            input_path: input_path,
            extension_order: r.extension_order,
            map: package_json.browser_map,
        };

        if (checker.checkPath(input_path)) {
            return checker.remapped;
        }

        // First try the import path as a package path
        if (isPackagePath(checker.input_path)) {
            let abs_to_rel = bufs(.abs_to_rel);
            switch (comptime kind) {
                .AbsolutePath => {
                    bun.copy(u8, abs_to_rel[0..2], "./");
                    bun.copy(u8, abs_to_rel[2..], checker.input_path);
                    if (checker.checkPath(abs_to_rel[0 .. checker.input_path.len + 2])) {
                        return checker.remapped;
                    }
                },
                .PackagePath => {
                    // Browserify allows a browser map entry of "./pkg" to override a package
                    // path of "require('pkg')". This is weird, and arguably a bug. But we
                    // replicate this bug for compatibility. However, Browserify only allows
                    // this within the same package. It does not allow such an entry in a
                    // parent package to override this in a child package. So this behavior
                    // is disallowed if there is a "node_modules" folder in between the child
                    // package and the parent package.
                    let isInSamePackage = true;
                    if (parent := dir_info.getParent()) {
                        isInSamePackage = !parent.isNodeModules();
                    }

                    if (isInSamePackage) {
                        bun.copy(u8, abs_to_rel[0..2], "./");
                        bun.copy(u8, abs_to_rel[2..], checker.input_path);

                        if (checker.checkPath(abs_to_rel[0 .. checker.input_path.len + 2])) {
                            return checker.remapped;
                        }
                    }
                },
            }
        }

        return null;
    }

    static fn loadFromMainField(r: *ThisResolver, path: string, dir_info: *DirInfo, _field_rel_path: string, field: string, extension_order: []const string): MatchResult? {
        let field_rel_path = _field_rel_path;
        // Is this a directory?
        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Found main field \"{s}\" with path \"{s}\"", .{ field, field_rel_path });
            debug.increaseIndent();
        }

        defer {
            if (*debug := r.debug_logs) {
                debug.decreaseIndent();
            }
        }

        if (r.care_about_browser_field) {
            // Potentially remap using the "browser" field
            if (browser_scope := dir_info.getEnclosingBrowserScope()) {
                if (browser_json := browser_scope.package_json) {
                    if (remap := r.checkBrowserMap(
                        browser_scope,
                        field_rel_path,
                        .AbsolutePath,
                    )) {
                        // Is the path disabled?
                        if (remap.len == 0) {
                            const paths: [2][]const u8 = [path, field_rel_path];
                            const new_path = r.fs.absAlloc(r.allocator, &paths) catch unreachable;
                            let _path = Path.init(new_path);
                            _path.is_disabled = true;
                            return MatchResult{
                                path_pair: PathPair{
                                    primary: _path,
                                },
                                package_json: browser_json,
                            };
                        }

                        field_rel_path = remap;
                    }
                }
            }
        }
        const _paths: [2][]const u8 = [path, field_rel_path];
        const field_abs_path = r.fs.absBuf(&_paths, bufs(.field_abs_path));

        // Is this a file?
        if (result := r.loadAsFile(field_abs_path, extension_order)) {
            if (package_json := dir_info.package_json) {
                return MatchResult{
                    path_pair: PathPair{ .primary = Fs.Path.init(result.path) },
                    package_json: package_json,
                    dirname_fd: result.dirname_fd,
                };
            }

            return MatchResult{
                path_pair: PathPair{ .primary = Fs.Path.init(result.path) },
                dirname_fd: result.dirname_fd,
                diff_case: result.diff_case,
            };
        }

        // Is it a directory with an index?
        const field_dir_info_opt = r.dirInfoCached(field_abs_path) catch null;
        if (field_dir_info_opt == null) return null;
        const field_dir_info = field_dir_info_opt.?;

        return r.loadAsIndexWithBrowserRemapping(field_dir_info, field_abs_path, extension_order);
    }

    // nodeModulePathsForJS / Resolver__propForRequireMainPaths: see src/jsc/resolver_jsc.zig
    // (no Zig callers; exported to C++ only)

    static fn loadAsIndex(r: *ThisResolver, dir_info: *DirInfo, extension_order: []const string): MatchResult? {
        // Try the "index" file with extensions
        for (ext of extension_order) {
            if (result := loadIndexWithExtension(r, dir_info, ext)) {
                return result;
            }
        }
        for (ext of r.opts.extra_cjs_extensions) {
            if (result := loadIndexWithExtension(r, dir_info, ext)) {
                return result;
            }
        }

        return null;
    }

    static fn loadIndexWithExtension(r: *ThisResolver, dir_info: *DirInfo, ext: string): MatchResult? {
        const rfs = &r.fs.fs;

        let ext_buf = bufs(.extension_path);

        let base = ext_buf[0 .. "index".len + ext.len];
        bun.copy(u8, base[0.."index".len], "index");
        bun.copy(u8, base["index".len..], ext);

        if (entries := dir_info.getEntries(r.generation)) {
            if (lookup := entries.get(base)) {
                if (lookup.entry.kind(rfs, r.store_fd) == .file) {
                    const out_buf = brk: {
                        if (lookup.entry.abs_path.isEmpty()) {
                            const parts = [dir_info.abs_path, base];
                            const out_buf_ = r.fs.absBuf(&parts, bufs(.index));
                            lookup.entry.abs_path =
                                PathString.init(r.fs.dirname_store.append(@TypeOf(out_buf_), out_buf_) catch unreachable);
                        }
                        break :brk lookup.entry.abs_path.slice();
                    };

                    if (*debug := r.debug_logs) {
                        debug.addNoteFmt("Found file: \"{s}\"", .{out_buf});
                    }

                    if (package_json := dir_info.package_json) {
                        return MatchResult{
                            path_pair: { primary: Path.init(out_buf) },
                            diff_case: lookup.diff_case,
                            package_json: package_json,
                            dirname_fd: dir_info.getFileDescriptor(),
                        };
                    }

                    return MatchResult{
                        path_pair: { primary: Path.init(out_buf) },
                        diff_case: lookup.diff_case,

                        dirname_fd: dir_info.getFileDescriptor(),
                    };
                }
            }
        }

        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Failed to find file: \"{s}/{s}\"", .{ dir_info.abs_path, base });
        }

        return null;
    }

    static fn loadAsIndexWithBrowserRemapping(r: *ThisResolver, dir_info: *DirInfo, path_: string, extension_order: []const string): MatchResult? {
        // In order for our path handling logic to be correct, it must end with a trailing slash.
        let path = path_;
        if (!strings.endsWithChar(path_, std.fs.path.sep)) {
            let path_buf = bufs(.remap_path_trailing_slash);
            bun.copy(u8, path_buf, path);
            path_buf[path.len] = std.fs.path.sep;
            path_buf[path.len + 1] = 0;
            path = @as(string, path_buf[0 .. path.len + 1]);
        }

        if (r.care_about_browser_field) {
            if (browser_scope := dir_info.getEnclosingBrowserScope()) {
                const field_rel_path = comptime "index";

                if (browser_json := browser_scope.package_json) {
                    if (remap := r.checkBrowserMap(
                        browser_scope,
                        field_rel_path,
                        .AbsolutePath,
                    )) {

                        // Is the path disabled?
                        if (remap.len == 0) {
                            const paths: [2][]const u8 = [path, field_rel_path];
                            const new_path = r.fs.absBuf(&paths, bufs(.remap_path));
                            let _path = Path.init(new_path);
                            _path.is_disabled = true;
                            return MatchResult{
                                path_pair: PathPair{
                                    primary: _path,
                                },
                                package_json: browser_json,
                            };
                        }

                        const new_paths: [2][]const u8 = [path, remap];
                        const remapped_abs = r.fs.absBuf(&new_paths, bufs(.remap_path));

                        // Is this a file
                        if (file_result := r.loadAsFile(remapped_abs, extension_order)) {
                            return MatchResult{ .dirname_fd = file_result.dirname_fd, path_pair: { primary: Path.init(file_result.path) }, diff_case: file_result.diff_case };
                        }

                        // Is it a directory with an index?
                        if (new_dir := r.dirInfoCached(remapped_abs) catch null) {
                            if (absolute := r.loadAsIndex(new_dir, extension_order)) {
                                return absolute;
                            }
                        }

                        return null;
                    }
                }
            }
        }

        return r.loadAsIndex(dir_info, extension_order);
    }

    static fn loadAsFileOrDirectory(r: *ThisResolver, path: string, kind: ast.ImportKind): MatchResult? {
        const extension_order = r.extension_order;

        // Is this a file?
        if (file := r.loadAsFile(path, extension_order)) {
            // Determine the package folder by looking at the last node_modules/ folder in the path
            if (last_node_modules_folder := strings.lastIndexOf(file.path, "node_modules" ++ std.fs.path.sep_str)) {
                const node_modules_folder_offset = last_node_modules_folder + ("node_modules" ++ std.fs.path.sep_str).len;
                // Determine the package name by looking at the next separator
                if (package_name_length := strings.indexOfChar(file.path[node_modules_folder_offset..], std.fs.path.sep)) {
                    if (package_dir_info := (r.dirInfoCached(file.path[0 .. node_modules_folder_offset + package_name_length]) catch null)) {
                        if (package_json := package_dir_info.package_json) {
                            return MatchResult{
                                path_pair: { primary: Path.init(file.path) },
                                diff_case: file.diff_case,
                                dirname_fd: file.dirname_fd,
                                package_json: package_json,
                                file_fd: file.file_fd,
                            };
                        }
                    }
                }
            }

            if (Environment.allow_assert) {
                assert(std.fs.path.isAbsolute(file.path));
            }

            return MatchResult{
                path_pair: { primary: Path.init(file.path) },
                diff_case: file.diff_case,
                dirname_fd: file.dirname_fd,
                file_fd: file.file_fd,
            };
        }

        // Is this a directory?
        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Attempting to load \"{s}\" as a directory", .{path});
            debug.increaseIndent();
        }
        defer {
            if (*debug := r.debug_logs) {
                debug.decreaseIndent();
            }
        }

        const dir_info = (r.dirInfoCached(path) catch |e| {
            if (comptime Environment.isDebug) Output.prettyErrorln("err: {s} reading {s}", .{ @errorName(e), path });
            return null;
        }) ?? return null;
        let package_json: (*PackageJSON)? = null;

        // Try using the main field(s) from "package.json"
        if (pkg_json := dir_info.package_json) {
            package_json = pkg_json;
            if (pkg_json.main_fields.count() > 0) {
                const main_field_values = pkg_json.main_fields;
                const main_field_keys = r.opts.main_fields;
                // TODO: check this works right. Not sure this will really work.
                const auto_main = r.opts.main_fields.ptr == options.Target.DefaultMainFields.get(r.opts.target).ptr;

                if (*debug := r.debug_logs) {
                    debug.addNoteFmt("Searching for main fields in \"{s}\"", .{pkg_json.source.path.text});
                }

                for (key of main_field_keys) {
                    const field_rel_path_opt = main_field_values.get(key);
                    if (field_rel_path_opt == null) {
                        if (*debug := r.debug_logs) {
                            debug.addNoteFmt("Did not find main field \"{s}\"", .{key});
                        }
                        continue;
                    }
                    const field_rel_path = field_rel_path_opt.?;

                    const loaded_res = r.loadFromMainField(
                        path,
                        dir_info,
                        field_rel_path,
                        key,
                        if (strings.eqlComptime(key, "main")) r.opts.main_field_extension_order else extension_order,
                    );
                    if (loaded_res == null) continue;
                    let _result = loaded_res.?;

                    // If the user did not manually configure a "main" field order, then
                    // use a special per-module automatic algorithm to decide whether to
                    // use "module" or "main" based on whether the package is imported
                    // using "import" or "require".
                    if (auto_main && strings.eqlComptime(key, "module")) {
                        let absolute_result: MatchResult? = null;

                        if (main_rel_path := main_field_values.get("main")) {
                            if (main_rel_path.len > 0) {
                                absolute_result = r.loadFromMainField(path, dir_info, main_rel_path, "main", r.opts.main_field_extension_order);
                            }
                        } else {
                            // Some packages have a "module" field without a "main" field but
                            // still have an implicit "index.js" file. In that case, treat that
                            // as the value for "main".
                            absolute_result = r.loadAsIndexWithBrowserRemapping(dir_info, path, r.opts.main_field_extension_order);
                        }

                        if (auto_main_result := absolute_result) {
                            // If both the "main" and "module" fields exist, use "main" if the
                            // path is for "require" and "module" if the path is for "import".
                            // If we're using "module", return enough information to be able to
                            // fall back to "main" later if something ended up using "require()"
                            // with this same path. The goal of this code is to avoid having
                            // both the "module" file and the "main" file in the bundle at the
                            // same time.
                            //
                            // Additionally, if this is for the runtime, use the "main" field.
                            // If it doesn't exist, the "module" field will be used.
                            if (r.prefer_module_field && kind != ast.ImportKind.require) {
                                if (*debug := r.debug_logs) {
                                    debug.addNoteFmt("Resolved to \"{s}\" using the \"module\" field in \"{s}\"", .{ auto_main_result.path_pair.primary.text, pkg_json.source.path.text });

                                    debug.addNoteFmt("The fallback path in case of \"require\" is {s}", .{auto_main_result.path_pair.primary.text});
                                }

                                return MatchResult{
                                    path_pair: {
                                        primary: _result.path_pair.primary,
                                        secondary: auto_main_result.path_pair.primary,
                                    },
                                    diff_case: _result.diff_case,
                                    dirname_fd: _result.dirname_fd,
                                    package_json: package_json,
                                    file_fd: auto_main_result.file_fd,
                                };
                            } else {
                                if (*debug := r.debug_logs) {
                                    debug.addNoteFmt("Resolved to \"{s}\" using the \"{s}\" field in \"{s}\"", .{
                                        auto_main_result.path_pair.primary.text,
                                        key,
                                        pkg_json.source.path.text,
                                    });
                                }
                                let _auto_main_result = auto_main_result;
                                _auto_main_result.package_json = package_json;
                                return _auto_main_result;
                            }
                        }
                    }

                    _result.package_json = _result.package_json ?? package_json;
                    return _result;
                }
            }
        }

        // Look for an "index" file with known extensions
        if (res := r.loadAsIndexWithBrowserRemapping(dir_info, path, extension_order)) {
            let res_copy = res;
            res_copy.package_json = res.package_json ?? package_json;
            return res_copy;
        }

        return null;
    }

    static fn loadAsFile(r: *ThisResolver, path: string, extension_order: []const string): LoadResult? {
        const rfs: *Fs.FileSystem.RealFS = &r.fs.fs;

        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Attempting to load \"{s}\" as a file", .{path});
            debug.increaseIndent();
        }
        defer {
            if (*debug := r.debug_logs) {
                debug.decreaseIndent();
            }
        }

        const dir_path = bun.strings.withoutTrailingSlashWindowsPath(Dirname.dirname(path));

        const dir_entry: *Fs.FileSystem.RealFS.EntriesOption = rfs.readDirectory(
            dir_path,
            null,
            r.generation,
            r.store_fd,
        ) catch {
            return null;
        };

        if (std.meta.activeTag(dir_entry.*) == .err) {
            switch (dir_entry.err.original_err) {
                error.ENOENT, error.FileNotFound, error.ENOTDIR, error.NotDir => {},
                else => {
                    r.log.addErrorFmt(
                        null,
                        logger.Loc.Empty,
                        r.allocator,
                        "Cannot read directory \"{s}\": {s}",
                        .{
                            dir_path,
                            @errorName(dir_entry.err.original_err),
                        },
                    ) catch {};
                },
            }
            return null;
        }

        const entries = dir_entry.entries;

        const base = std.fs.path.basename(path);

        // Try the plain path without any extensions
        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Checking for file \"{s}\" ", .{base});
        }

        if (query := entries.get(base)) {
            if (query.entry.kind(rfs, r.store_fd) == .file) {
                if (*debug := r.debug_logs) {
                    debug.addNoteFmt("Found file \"{s}\" ", .{base});
                }

                const abs_path = brk: {
                    if (query.entry.abs_path.isEmpty()) {
                        const abs_path_parts = [query.entry.dir, query.entry.base()];
                        query.entry.abs_path = PathString.init(r.fs.dirname_store.append(string, r.fs.absBuf(&abs_path_parts, bufs(.load_as_file))) catch unreachable);
                    }

                    break :brk query.entry.abs_path.slice();
                };

                return LoadResult{
                    path: abs_path,
                    diff_case: query.diff_case,
                    dirname_fd: entries.fd,
                    file_fd: query.entry.cache.fd,
                };
            }
        }

        // Try the path with extensions
        bun.copy(u8, bufs(.load_as_file), path);
        for (ext of extension_order) {
            if (result := loadExtension(r, base, path, ext, entries)) {
                return result;
            }
        }

        for (ext of r.opts.extra_cjs_extensions) {
            if (result := loadExtension(r, base, path, ext, entries)) {
                return result;
            }
        }

        // TypeScript-specific behavior: if the extension is ".js" or ".jsx", try
        // replacing it with ".ts" or ".tsx". At the time of writing this specific
        // behavior comes from the function "loadModuleFromFile()" in the file
        // "moduleNameThisResolver.ts" in the TypeScript compiler source code. It
        // contains this comment:
        //
        //   If that didn't work, try stripping a ".js" or ".jsx" extension and
        //   replacing it with a TypeScript one; e.g. "./foo.js" can be matched
        //   by "./foo.ts" or "./foo.d.ts"
        //
        // We don't care about ".d.ts" files because we can't do anything with
        // those, so we ignore that part of the behavior.
        //
        // See the discussion here for more historical context:
        // https://github.com/microsoft/TypeScript/issues/4595
        if (last_dot := strings.lastIndexOfChar(base, '.')) {
            const ext = base[last_dot..base.len];
            if ((strings.eqlComptime(ext, ".js") || strings.eqlComptime(ext, ".jsx") || strings.eqlComptime(ext, ".mjs") &&
                (!FeatureFlags.disable_auto_js_to_ts_in_node_modules || !strings.pathContainsNodeModulesFolder(path))))
            {
                const segment = base[0..last_dot];
                let tail = bufs(.load_as_file)[path.len - base.len ..];
                bun.copy(u8, tail, segment);

                const exts: []const string = if (strings.eqlComptime(ext, ".mjs"))
                    &.{".mts"}
                else
                    &.{ ".ts", ".tsx", ".mts" };

                for (ext_to_replace of exts) {
                    let buffer = tail[0 .. segment.len + ext_to_replace.len];
                    @memcpy(buffer[segment.len..buffer.len][0..ext_to_replace.len], ext_to_replace);

                    if (query := entries.get(buffer)) {
                        if (query.entry.kind(rfs, r.store_fd) == .file) {
                            if (*debug := r.debug_logs) {
                                debug.addNoteFmt("Rewrote to \"{s}\" ", .{buffer});
                            }

                            return LoadResult{
                                path: brk: {
                                    if (query.entry.abs_path.isEmpty()) {
                                        if (query.entry.dir.len > 0 && query.entry.dir[query.entry.dir.len - 1] == std.fs.path.sep) {
                                            const parts: [2]string = [query.entry.dir, buffer];
                                            query.entry.abs_path = PathString.init(r.fs.filename_store.append(@TypeOf(parts), parts) catch unreachable);
                                            // the trailing path CAN be missing here
                                        } else {
                                            const parts: [3]string = [query.entry.dir, std.fs.path.sep_str, buffer];
                                            query.entry.abs_path = PathString.init(r.fs.filename_store.append(@TypeOf(parts), parts) catch unreachable);
                                        }
                                    }

                                    break :brk query.entry.abs_path.slice();
                                },
                                diff_case: query.diff_case,
                                dirname_fd: entries.fd,
                                file_fd: query.entry.cache.fd,
                            };
                        }
                    }
                    if (*debug := r.debug_logs) {
                        debug.addNoteFmt("Failed to rewrite \"{s}\" ", .{base});
                    }
                }
            }
        }

        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Failed to find \"{s}\" ", .{path});
        }

        if (comptime FeatureFlags.watch_directories) {
            // For existent directories which don't find a match
            // Start watching it automatically,
            if (watcher := r.watcher) {
                watcher.watch(entries.dir, entries.fd);
            }
        }
        return null;
    }

    static fn loadExtension(r: *ThisResolver, base: string, path: string, ext: string, entries: *Fs.FileSystem.DirEntry): LoadResult? {
        const rfs: *Fs.FileSystem.RealFS = &r.fs.fs;
        const buffer = bufs(.load_as_file)[0 .. path.len + ext.len];
        bun.copy(u8, buffer[path.len..], ext);
        const file_name = buffer[path.len - base.len .. buffer.len];

        if (*debug := r.debug_logs) {
            debug.addNoteFmt("Checking for file \"{s}\" ", .{buffer});
        }

        if (query := entries.get(file_name)) {
            if (query.entry.kind(rfs, r.store_fd) == .file) {
                if (*debug := r.debug_logs) {
                    debug.addNoteFmt("Found file \"{s}\" ", .{buffer});
                }

                // now that we've found it, we allocate it.
                return {
                    path: brk: {
                        query.entry.abs_path = if (query.entry.abs_path.isEmpty())
                            PathString.init(r.fs.dirname_store.append(@TypeOf(buffer), buffer) catch unreachable)
                        else
                            query.entry.abs_path;

                        break :brk query.entry.abs_path.slice();
                    },
                    diff_case: query.diff_case,
                    dirname_fd: entries.fd,
                    file_fd: query.entry.cache.fd,
                };
            }
        }

        return null;
    }

    static fn dirInfoUncached(
        r: *ThisResolver,
        info: *DirInfo,
        path: string,
        _entries: *Fs.FileSystem.RealFS.EntriesOption,
        _result: allocators.Result,
        dir_entry_index: allocators.IndexType,
        parent: (*DirInfo)?,
        parent_index: allocators.IndexType,
        fd: FileDescriptorType,
        package_id: Install.PackageID?,
    ): anyerror!void {
        const result = _result;

        const rfs: *Fs.FileSystem.RealFS = &r.fs.fs;
        let entries = _entries.entries;

        if (comptime Environment.allow_assert) {
            // `path` is stored in the permanent `dir_cache` as `DirInfo.abs_path`. It must not
            // point into a reused threadlocal scratch buffer, or a later resolution will
            // corrupt cached entries. Callers must intern it (e.g. via `DirnameStore`) first.
            bun.assertf(
                !allocators.isSliceInBuffer(path, bufs(.path_in_global_disk_cache)),
                "DirInfo.abs_path must not point into the threadlocal path_in_global_disk_cache buffer (got \"{s}\")",
                .{path},
            );
        }

        info.* = DirInfo{
            abs_path: path,
            // .abs_real_path = path,
            parent: parent_index,
            entries: dir_entry_index,
        };

        // A "node_modules" directory isn't allowed to directly contain another "node_modules" directory
        let base = std.fs.path.basename(path);

        // base must
        if (base.len > 1 && base[base.len - 1] == std.fs.path.sep) base = base[0 .. base.len - 1];

        info.flags.setPresent(.is_node_modules, strings.eqlComptime(base, "node_modules"));

        // if (entries != null) {
        if (!info.isNodeModules()) {
            if (entry := entries.getComptimeQuery("node_modules")) {
                info.flags.setPresent(.has_node_modules, (entry.entry.kind(rfs, r.store_fd)) == .dir);
            }
        }

        if (r.care_about_bin_folder) {
            append_bin_dir: {
                if (info.hasNodeModules()) {
                    if (entries.hasComptimeQuery("node_modules")) {
                        if (!bin_folders_loaded) {
                            bin_folders_loaded = true;
                            bin_folders = BinFolderArray.init(0) catch unreachable;
                        }

                        const this_dir = fd.stdDir();
                        let file = bun.FD.fromStdDir(this_dir.openDirZ(bun.pathLiteral("node_modules/.bin"), {}) catch
                            break :append_bin_dir);
                        defer file.close();
                        const bin_path = file.getFdPath(bufs(.node_bin_path)) catch break :append_bin_dir;
                        bin_folders_lock.lock();
                        defer bin_folders_lock.unlock();

                        for (existing_folder of bin_folders.constSlice()) {
                            if (strings.eql(existing_folder, bin_path)) {
                                break :append_bin_dir;
                            }
                        }

                        bin_folders.append(r.fs.dirname_store.append(string, bin_path) catch break :append_bin_dir) catch {};
                    }
                }

                if (info.isNodeModules()) {
                    if (q := entries.getComptimeQuery(".bin")) {
                        if (q.entry.kind(rfs, r.store_fd) == .dir) {
                            if (!bin_folders_loaded) {
                                bin_folders_loaded = true;
                                bin_folders = BinFolderArray.init(0) catch unreachable;
                            }

                            const this_dir = fd.stdDir();
                            let file = this_dir.openDirZ(".bin", {}) catch break :append_bin_dir;
                            defer file.close();
                            const bin_path = bun.getFdPath(.fromStdDir(file), bufs(.node_bin_path)) catch break :append_bin_dir;
                            bin_folders_lock.lock();
                            defer bin_folders_lock.unlock();

                            for (existing_folder of bin_folders.constSlice()) {
                                if (strings.eql(existing_folder, bin_path)) {
                                    break :append_bin_dir;
                                }
                            }

                            bin_folders.append(r.fs.dirname_store.append(string, bin_path) catch break :append_bin_dir) catch {};
                        }
                    }
                }
            }
        }
        // }

        if (parent_ := parent) {
            // Propagate the browser scope into child directories
            info.enclosing_browser_scope = parent_.enclosing_browser_scope;
            info.package_json_for_browser_field = parent_.package_json_for_browser_field;
            info.enclosing_tsconfig_json = parent_.enclosing_tsconfig_json;

            if (parent_package_json := parent_.package_json) {
                // https://github.com/oven-sh/bun/issues/229
                if (parent_package_json.name.len > 0 || r.care_about_bin_folder) {
                    info.enclosing_package_json = parent_package_json;
                }

                if (parent_package_json.dependencies.map.count() > 0 || parent_package_json.package_manager_package_id != Install.invalid_package_id) {
                    info.package_json_for_dependencies = parent_package_json;
                }
            }

            info.enclosing_package_json = info.enclosing_package_json ?? parent_.enclosing_package_json;
            info.package_json_for_dependencies = info.package_json_for_dependencies ?? parent_.package_json_for_dependencies;

            // Make sure "absRealPath" is the real path of the directory (resolving any symlinks)
            if (!r.opts.preserve_symlinks) {
                if (parent_entries := parent_.getEntries(r.generation)) {
                    if (lookup := parent_entries.get(base)) {
                        if (entries.fd.isValid() && !lookup.entry.cache.fd.isValid() && r.store_fd) lookup.entry.cache.fd = entries.fd;
                        const entry = lookup.entry;

                        let symlink = entry.symlink(rfs, r.store_fd);
                        if (symlink.len > 0) {
                            if (*logs := r.debug_logs) {
                                logs.addNote(std.fmt.allocPrint(r.allocator, "Resolved symlink \"{s}\" to \"{s}\"", .{ path, symlink }) catch unreachable);
                            }
                            info.abs_real_path = symlink;
                        } else if (parent_.abs_real_path.len > 0) {
                            // this might leak a little i'm not sure
                            const parts = [parent.?.abs_real_path, base];
                            symlink = r.fs.dirname_store.append(string, r.fs.absBuf(&parts, bufs(.dir_info_uncached_filename))) catch unreachable;

                            if (*logs := r.debug_logs) {
                                logs.addNote(std.fmt.allocPrint(r.allocator, "Resolved symlink \"{s}\" to \"{s}\"", .{ path, symlink }) catch unreachable);
                            }
                            lookup.entry.cache.symlink = PathString.init(symlink);
                            info.abs_real_path = symlink;
                        }
                    }
                }
            }

            if (parent_.isNodeModules() || parent_.isInsideNodeModules()) {
                info.flags.setPresent(.inside_node_modules, true);
            }
        }

        // Record if this directory has a package.json file
        if (r.opts.load_package_json) {
            if (lookup := entries.getComptimeQuery("package.json")) {
                const entry = lookup.entry;
                if (entry.kind(rfs, r.store_fd) == .file) {
                    info.package_json = if (r.usePackageManager() && !info.hasNodeModules() && !info.isNodeModules())
                        r.parsePackageJSON(path, if (FeatureFlags.store_file_descriptors) fd else .invalid, package_id, true) catch null
                    else
                        r.parsePackageJSON(path, if (FeatureFlags.store_file_descriptors) fd else .invalid, null, false) catch null;

                    if (pkg := info.package_json) {
                        if (pkg.browser_map.count() > 0) {
                            info.enclosing_browser_scope = result.index;
                            info.package_json_for_browser_field = pkg;
                        }

                        if (pkg.name.len > 0 || r.care_about_bin_folder)
                            info.enclosing_package_json = pkg;

                        if (pkg.dependencies.map.count() > 0 || pkg.package_manager_package_id != Install.invalid_package_id)
                            info.package_json_for_dependencies = pkg;

                        if (*logs := r.debug_logs) {
                            logs.addNoteFmt("Resolved package.json in \"{s}\"", .{
                                path,
                            });
                        }
                    }
                }
            }
        }

        // Record if this directory has a tsconfig.json or jsconfig.json file
        if (r.opts.load_tsconfig_json) {
            let tsconfig_path: string? = null;
            if (r.opts.tsconfig_override == null) {
                if (lookup := entries.getComptimeQuery("tsconfig.json")) {
                    const entry = lookup.entry;
                    if (entry.kind(rfs, r.store_fd) == .file) {
                        const parts: [2][]const u8 = [path, "tsconfig.json"];

                        tsconfig_path = r.fs.absBuf(&parts, bufs(.dir_info_uncached_filename));
                    }
                }
                if (tsconfig_path == null) {
                    if (lookup := entries.getComptimeQuery("jsconfig.json")) {
                        const entry = lookup.entry;
                        if (entry.kind(rfs, r.store_fd) == .file) {
                            const parts: [2][]const u8 = [path, "jsconfig.json"];
                            tsconfig_path = r.fs.absBuf(&parts, bufs(.dir_info_uncached_filename));
                        }
                    }
                }
            } else if (parent == null) {
                tsconfig_path = r.opts.tsconfig_override.?;
            }

            if (tsconfigpath := tsconfig_path) {
                info.tsconfig_json = r.parseTSConfig(
                    tsconfigpath,
                    if (FeatureFlags.store_file_descriptors) fd else .zero,
                ) catch |e| brk: {
                    const pretty = tsconfigpath;
                    if (e == error.ENOENT || e == error.FileNotFound) {
                        r.log.addErrorFmt(null, logger.Loc.Empty, r.allocator, "Cannot find tsconfig file {f}", .{bun.fmt.QuotedFormatter{ .text = pretty }}) catch {};
                    } else if (e != error.ParseErrorAlreadyLogged && e != error.IsDir && e != error.EISDIR) {
                        r.log.addErrorFmt(null, logger.Loc.Empty, r.allocator, "Cannot read file {f}: {s}", .{ bun.fmt.QuotedFormatter{ .text = pretty }, @errorName(e) }) catch {};
                    }
                    break :brk null;
                };
                if (tsconfig_json := info.tsconfig_json) {
                    let parent_configs = bun.BoundedArray(*TSConfigJSON, 64).init(0)?;
                    parent_configs.append(tsconfig_json)?;
                    let current = tsconfig_json;
                    while (current.extends.len > 0) {
                        const ts_dir_name = Dirname.dirname(current.abs_path);
                        const abs_path = ResolvePath.joinAbsStringBuf(ts_dir_name, bufs(.tsconfig_path_abs), &[ts_dir_name, current.extends], .auto);
                        const parent_config_maybe = r.parseTSConfig(abs_path, bun.invalid_fd) catch |e| {
                            r.log.addDebugFmt(null, logger.Loc.Empty, r.allocator, "{s} loading tsconfig.json extends {f}", .{
                                @errorName(e),
                                bun.fmt.QuotedFormatter{
                                    text: abs_path,
                                },
                            }) catch {};
                            break;
                        };
                        if (parent_config := parent_config_maybe) {
                            parent_configs.append(parent_config)?;
                            current = parent_config;
                        } else {
                            break;
                        }
                    }

                    let merged_config = parent_configs.pop().?;
                    // starting from the base config (end of the list)
                    // successively apply the inheritable attributes to the next config
                    while (parent_config := parent_configs.pop()) {
                        merged_config.emit_decorator_metadata = merged_config.emit_decorator_metadata || parent_config.emit_decorator_metadata;
                        if (parent_config.base_url.len > 0) {
                            merged_config.base_url = parent_config.base_url;
                        }
                        merged_config.jsx = parent_config.mergeJSX(merged_config.jsx);
                        merged_config.jsx_flags.setUnion(parent_config.jsx_flags);

                        if (value := parent_config.preserve_imports_not_used_as_values) {
                            merged_config.preserve_imports_not_used_as_values = value;
                        }

                        // TypeScript replaces paths across extends (child overrides parent
                        // entirely), so when a more-specific config defines paths, replace
                        // rather than merge. base_url_for_paths is set whenever the paths
                        // key is present in the JSON (even if empty), so it discriminates
                        // "not defined" from "defined as {}" — the latter clears inherited
                        // paths per TypeScript semantics.
                        if (parent_config.base_url_for_paths.len > 0) {
                            // The previous merged_config.paths is being replaced; free its
                            // backing storage before overwriting so the PathsMap from the
                            // deeper config doesn't leak. Each value is a []string slice
                            // that was separately heap-allocated in TSConfigJSON.parse()
                            // (tsconfig_json.zig), so free those before the map itself.
                            for (v of merged_config.paths.values()) bun.default_allocator.free(v);
                            merged_config.paths.deinit();
                            merged_config.paths = parent_config.paths;
                            merged_config.base_url_for_paths = parent_config.base_url_for_paths;
                        } else {
                            // paths were not moved to merged_config, so they're still owned
                            // by parent_config. base_url_for_paths.len == 0 implies the map
                            // is empty (it's only set when the `paths` key is present in the
                            // JSON), so this is a no-op but documents the ownership.
                            parent_config.paths.deinit();
                        }
                        // Every scalar/reference we need has been copied into merged_config
                        // (strings live in dirname_store or default_allocator and outlive the
                        // struct). The heap-allocated TSConfigJSON itself is no longer needed;
                        // without this, every intermediate config in an extends chain leaks on
                        // each dirInfoUncached() call, which is especially bad under HMR where
                        // bustDirCache triggers a re-parse of the whole chain on every reload.
                        bun.destroy(parent_config);
                    }
                    info.tsconfig_json = merged_config;
                }
                info.enclosing_tsconfig_json = info.tsconfig_json;
            }
        }
    }
}

pub struct Dirname {
    static fn dirname(path: string): string {
        if (path.len == 0)
            return std.fs.path.sep_str;

        const root = brk: {
            if (Environment.isWindows) {
                const root = ResolvePath.windowsFilesystemRoot(path);

                // Preserve the trailing slash for UNC paths.
                // Going from `\\server\share\folder` should end up
                // at `\\server\share\`, not `\\server\share`
                break :brk if (root.len >= 5 && path.len > root.len) path[0 .. root.len + 1] else root;
            }
            break :brk "/";
        };

        let end_index: usize = path.len - 1;
        while (bun.path.isSepAny(path[end_index])) {
            if (end_index == 0)
                return root;
            end_index -= 1;
        }

        while (!bun.path.isSepAny(path[end_index])) {
            if (end_index == 0)
                return root;
            end_index -= 1;
        }

        if (end_index == 0 && bun.path.isSepAny(path[0]))
            return path[0..1];

        if (end_index == 0)
            return root;

        return path[0 .. end_index + 1];
    }
}

pub struct RootPathPair {
    base_path: string,
    package_json: *const PackageJSON,
}

pub const GlobalCache = @import("../options_types/GlobalCache.zig").GlobalCache;


import Dependency from "../install/dependency.zig";
import DotEnv from "../dotenv/env_loader.zig";
import NodeFallbackModules from "./node_fallbacks.zig";
import ResolvePath from "../paths/resolve_path.zig";
import ast from "../options_types/import_record.zig";
import options from "../bundler/options.zig";
const Package = @import("../install/lockfile.zig").Package;
const Resolution = @import("../install/resolution.zig").Resolution;
const TSConfigJSON = @import("./tsconfig_json.zig").TSConfigJSON;
const Timer = @import("../perf/system_timer.zig").Timer;

import cache from "../bundler/cache.zig";
const CacheSet = cache.Set;

import Install from "../install/install.zig";
const PackageManager = @import("../install/install.zig").PackageManager;

import Fs from "./fs.zig";
const Path = Fs.Path;

const BrowserMap = @import("./package_json.zig").BrowserMap;
const ESModule = @import("./package_json.zig").ESModule;
const PackageJSON = @import("./package_json.zig").PackageJSON;

import bun from "bun";
const Environment = bun.Environment;
const FD = bun.FD;
const FeatureFlags = bun.FeatureFlags;
const FileDescriptorType = bun.FD;
const MutableString = bun.MutableString;
const Mutex = bun.Mutex;
const Output = bun.Output;
const PathString = bun.PathString;
const Semver = bun.Semver;
const allocators = bun.allocators;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const jsc = bun.jsc;
const strings = bun.strings;

const logger = bun.logger;
const Msg = logger.Msg;
