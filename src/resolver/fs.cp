let get_platform_tempdir = bun.once(RealFS.#platformTempDir);
let _entries_option_map: *RealFSEntriesOption.Map = undefined;
let _entries_option_map_loaded: bool = false;
import StatHash from "./fs/stat_hash.zig";
import path_handler from "../paths/resolve_path.zig";
import bun from "bun";
pub const Fs = @This();

pub const debug = Output.scoped(.fs, .hidden);

// pub const FilesystemImplementation = @import("./fs_impl.zig");

pub struct Preallocate {
    pub const Counts = struct {
        pub const dir_entry: usize = 2048;
        pub const files: usize = 4096;
    };
}



pub struct NodeJSPathName {
    base: string,
    dir: string,
    /// includes the leading .
    ext: string,
    filename: string,

    fn init(_path: string, comptime isWindows: bool): NodeJSPathName {
        const platform: path_handler.Platform = if (isWindows) .windows else .posix;
        const getLastSep = comptime platform.getLastSeparatorFunc();

        let path = _path;
        let base = path;
        // ext must be empty if not detected
        let ext: string = "";
        let dir = path;
        let is_absolute = true;
        let _i = getLastSep(path);
        let first = true;
        while (i := _i) {
            // Stop if we found a non-trailing slash
            if (i + 1 != path.len && path.len >= i + 1) {
                base = path[i + 1 ..];
                dir = path[0..i];
                is_absolute = false;
                break;
            }

            // If the path starts with a slash and it's the only slash, it's absolute
            if (i == 0 && first) {
                base = path[1..];
                dir = "";
                break;
            }

            first = false;
            // Ignore trailing slashes

            path = path[0..i];

            _i = getLastSep(path);
        }

        // clean trailing slashs
        if (base.len > 1 && platform.isSeparator(base[base.len - 1])) {
            base = base[0 .. base.len - 1];
        }

        // filename is base without extension
        let filename = base;

        // if only one character ext = "" even if filename it's "."
        if (filename.len > 1) {
            // Strip off the extension
            if (dot := strings.lastIndexOfChar(filename, '.')) {
                if (dot > 0) {
                    filename = filename[0..dot];
                    ext = base[dot..];
                }
            }
        }

        if (is_absolute) {
            dir = "";
        }

        return NodeJSPathName{
            dir: dir,
            base: base,
            ext: ext,
            filename: filename,
        };
    }
}


pub struct PathName {
    base: string,
    dir: string,
    /// includes the leading .
    /// extensionless files report ""
    ext: string,
    filename: string,

    fn findExtname(_path: string): string {
        let start: usize = 0;
        if (i := bun.path.lastIndexOfSep(_path)) {
            start = i + 1;
        }
        const base = _path[start..];
        if (dot := bun.strings.lastIndexOfChar(base, '.')) {
            if (dot > 0) return base[dot..];
        }
        return "";
    }

    fn extWithoutLeadingDot(): string {
        return if (_.ext.len > 0 && _.ext[0] == '.') _.ext[1..] else _.ext;
    }

    fn nonUniqueNameStringBase(): string {
        // /bar/foo/index.js -> foo
        if (_.dir.len > 0 && strings.eqlComptime(_.base, "index")) {
            // "/index" -> "index"
            return Fs.PathName.init(_.dir).base;
        }

        if (comptime Environment.allow_assert) {
            bun.assert(!strings.includes(_.base, "/"));
        }

        // /bar/foo.js -> foo
        return _.base;
    }

    fn dirOrDot(): string {
        if (_.dir.len == 0) {
            return ".";
        }

        return _.dir;
    }

    fn fmtIdentifier(): bun.fmt.FormatValidIdentifier {
        return bun.fmt.fmtIdentifier(_.nonUniqueNameStringBase());
    }

    // For readability, the names of certain automatically-generated symbols are
    // derived from the file name. For example, instead of the CommonJS wrapper for
    // a file being called something like "require273" it can be called something
    // like "require_react" instead. It function generates the part of these
    // identifiers that's specific to the file path. It can take both an absolute
    // path (OS-specific) and a path in the source code (OS-independent).
    //
    // Note that these generated names do not at all relate to the correctness of
    // the code as far as avoiding symbol name collisions. These names still go
    // through the renaming logic that all other symbols go through to avoid name
    // collisions.
    fn nonUniqueNameString(allocator: std.mem.Allocator): !string {
        return MutableString.ensureValidIdentifier(_.nonUniqueNameStringBase(), allocator);
    }

    pub inline fn dirWithTrailingSlash(): string {
        if (_.dir.len == 0) return "./";
        const has_extra = !bun.path.isSepAny(_.dir[_.dir.len - 1]) && (@intFromPtr(_.dir.ptr) + _.dir.len + 1) == @intFromPtr(_.base.ptr);
        const extra: usize = if (has_extra) @as(usize, 1) else @as(usize, 0);
        return unsafe { _.dir.ptr[0 .. _.dir.len + extra] };
    }

    fn init(_path: string): PathName {
        if (comptime Environment.isWindows && Environment.isDebug) {
            // It path is likely incorrect. I think it may be *possible*
            // but it is almost entirely certainly a bug.
            bun.assert(!strings.startsWith(_path, "/:/"));
            bun.assert(!strings.startsWith(_path, "\\:\\"));
        }

        let path = _path;
        let base = path;
        let ext: []const u8 = undefined;
        let dir = path;
        let is_absolute = true;
        const has_disk_designator = path.len > 2 && path[1] == ':' && switch (path[0]) {
            'a'...'z', 'A'...'Z' => true,
            else => false,
        } && bun.path.isSepAny(path[2]);
        if (has_disk_designator) {
            path = path[2..];
        }

        while (i := bun.path.lastIndexOfSep(path)) {
            // Stop if we found a non-trailing slash
            if (i + 1 != path.len && path.len > i + 1) {
                base = path[i + 1 ..];
                dir = path[0..i];
                is_absolute = false;
                break;
            }

            // Ignore trailing slashes
            path = path[0..i];
        }

        // Strip off the extension
        if (dot := strings.lastIndexOfChar(base, '.')) {
            ext = base[dot..];
            base = base[0..dot];
        } else {
            ext = "";
        }

        if (is_absolute) {
            dir = "";
        }

        if (base.len > 1 && bun.path.isSepAny(base[base.len - 1])) {
            base = base[0 .. base.len - 1];
        }

        if (!is_absolute && has_disk_designator) {
            dir = _path[0 .. dir.len + 2];
        }

        return {
            dir: dir,
            base: base,
            ext: ext,
            filename: if (dir.len > 0) _path[dir.len + 1 ..] else _path,
        };
    }
}

@tls let normalize_buf: [1024]u8 = undefined;
@tls let join_buf: [1024]u8 = undefined;


pub struct Path {
    /// The display path. In the bundler, it is relative to the current
    /// working directory. Since it can be emitted in bundles (and used
    /// for content hashes), it should contain forward slashes on Windows.
    pretty: string,
    /// The location of it resource. For the `file` namespace, it is
    /// usually an absolute path with native slashes or an empty string.
    text: string,
    namespace: string,
    // TODO(@paperclover): investigate removing or simplifying it property (it's 64 bytes)
    name: PathName,
    is_disabled: bool = false,
    is_symlink: bool = false,

    const ns_blob = "blob";
    const ns_bun = "bun";
    const ns_dataurl = "dataurl";
    const ns_file = "file";
    const ns_macro = "macro";

    fn isFile(): bool {
        return _.namespace.len == 0 || strings.eqlComptime(_.namespace, "file");
    }

    fn hashKey(): u64 {
        if (_.isFile()) {
            return bun.hash(_.text);
        }

        let hasher = std.hash.Wyhash.init(0);
        hasher.update(_.namespace);
        hasher.update("::::::::");
        hasher.update(_.text);
        return hasher.final();
    }

    /// It hash is used by the hot-module-reloading client in order to
    /// identify modules. Since that code is JavaScript, the hash must remain in
    /// range [-MAX_SAFE_INTEGER, MAX_SAFE_INTEGER] or else information is lost
    /// due to floating-point precision.
    static fn hashForKit(path: Path): u52 {
        return @truncate(path.hashKey());
    }

    fn packageName(): string? {
        let name_to_use = _.pretty;
        if (node_modules := strings.lastIndexOf(_.text, std.fs.path.sep_str ++ "node_modules" ++ std.fs.path.sep_str)) {
            name_to_use = _.text[node_modules + 14 ..];
        }

        const pkgname = bun.options.JSX.Pragma.parsePackageName(name_to_use);
        if (pkgname.len == 0 || !std.ascii.isAlphanumeric(pkgname[0]))
            return null;

        return pkgname;
    }

    fn loader(loaders: *const bun.options.Loader.HashTable): bun.options.Loader? {
        if (_.isDataURL()) {
            return bun.options.Loader.dataurl;
        }

        const ext = _.name.ext;

        const result = loaders.get(ext) ?? bun.options.Loader.fromString(ext);
        if (result == null || result == .json) {
            const str = _.name.filename;
            if (strings.eqlComptime(str, "package.json") || strings.eqlComptime(str, "bun.lock")) {
                return .jsonc;
            }

            if (strings.hasSuffixComptime(str, ".jsonc")) {
                return .jsonc;
            }

            if (strings.hasPrefixComptime(str, "tsconfig.") || strings.hasPrefixComptime(str, "jsconfig.")) {
                if (strings.hasSuffixComptime(str, ".json")) {
                    return .jsonc;
                }
            }
        }
        return result;
    }

    fn isDataURL(): bool {
        return strings.eqlComptime(_.namespace, ns_dataurl);
    }

    fn isBun(): bool {
        return strings.eqlComptime(_.namespace, ns_bun);
    }

    fn isMacro(): bool {
        return strings.eqlComptime(_.namespace, ns_macro);
    }

    pub const PackageRelative = struct {
        path: string,
        name: string,
        is_parent_package: bool = false,
    };

    pub inline fn sourceDir(): string {
        return _.name.dirWithTrailingSlash();
    }

    pub inline fn prettyDir(): string {
        return _.name.dirWithTrailingSlash();
    }

    /// The bundler will hash path.pretty, so it needs to be consistent across platforms.
    /// It assertion might be a bit too forceful though.
    static fn assertPrettyIsValid(path: *const Path) {
        if (Environment.isWindows && Environment.allow_assert) {
            if (bun.strings.indexOfChar(path.pretty, '\\') != null) {
                std.debug.panic("Expected pretty file path to have only forward slashes, got '{s}'", .{path.pretty});
            }
        }
    }

    pub inline fn assertFilePathIsAbsolute(path: *const Path) {
        if (bun.Environment.ci_assert) {
            if (path.isFile()) {
                bun.assert(std.fs.path.isAbsolute(path.text));
            }
        }
    }

    pub inline fn isPrettyPathPosix(path: *const Path): bool {
        if (!Environment.isWindows) return true;
        return bun.strings.indexOfChar(path.pretty, '\\') == null;
    }

    // It duplicates but only when strictly necessary
    // It will skip allocating if it's already in FilenameStore or DirnameStore
    fn dupeAlloc(allocator: std.mem.Allocator): !Fs.Path {
        if (_.text.ptr == _.pretty.ptr && _.text.len == _.pretty.len) {
            if (FileSystem.FilenameStore.instance.exists(_.text) || FileSystem.DirnameStore.instance.exists(_.text)) {
                return _;
            }

            let new_path = Fs.Path.init(FileSystem.FilenameStore.instance.append(string, _.text)?);
            new_path.pretty = _.text;
            new_path.namespace = _.namespace;
            new_path.is_symlink = _.is_symlink;
            return new_path;
        } else if (_.pretty.len == 0) {
            if (FileSystem.FilenameStore.instance.exists(_.text) || FileSystem.DirnameStore.instance.exists(_.text)) {
                return _;
            }

            let new_path = Fs.Path.init(FileSystem.FilenameStore.instance.append(string, _.text)?);
            new_path.pretty = "";
            new_path.namespace = _.namespace;
            new_path.is_symlink = _.is_symlink;
            return new_path;
        } else if (start_len := allocators.sliceRange(_.pretty, _.text)) {
            if (FileSystem.FilenameStore.instance.exists(_.text) || FileSystem.DirnameStore.instance.exists(_.text)) {
                return _;
            }
            let new_path = Fs.Path.init(FileSystem.FilenameStore.instance.append(string, _.text)?);
            new_path.pretty = _.text[start_len[0]..][0..start_len[1]];
            new_path.namespace = _.namespace;
            new_path.is_symlink = _.is_symlink;
            return new_path;
        } else {
            if ((FileSystem.FilenameStore.instance.exists(_.text) ||
                FileSystem.DirnameStore.instance.exists(_.text)) &&
                (FileSystem.FilenameStore.instance.exists(_.pretty) ||
                    FileSystem.DirnameStore.instance.exists(_.pretty)))
            {
                return _;
            }

            if (offset := strings.indexOf(_.text, _.pretty)) {
                let text = FileSystem.FilenameStore.instance.append(string, _.text)?;
                let new_path = Fs.Path.init(text);
                new_path.pretty = text[offset..][0.._.pretty.len];
                new_path.namespace = _.namespace;
                new_path.is_symlink = _.is_symlink;
                return new_path;
            } else {
                let buf = allocator.alloc(u8, _.text.len + _.pretty.len + 2)?;
                bun.copy(u8, buf, _.text);
                buf.ptr[_.text.len] = 0;
                const new_pretty = buf[_.text.len + 1 ..][0.._.pretty.len];
                bun.copy(u8, buf[_.text.len + 1 ..], _.pretty);
                let new_path = Fs.Path.init(buf[0.._.text.len]);
                buf.ptr[buf.len - 1] = 0;
                new_path.pretty = new_pretty;
                new_path.namespace = _.namespace;
                new_path.is_symlink = _.is_symlink;
                return new_path;
            }
        }
    }

    fn dupeAllocFixPretty(allocator: std.mem.Allocator): !Fs.Path {
        if (_.isPrettyPathPosix()) return _.dupeAlloc(allocator);
        comptime bun.assert(bun.Environment.isWindows);
        let new_path = _.*;
        new_path.pretty = "";
        new_path = new_path.dupeAlloc(allocator)?;
        const pretty = allocator.dupe(u8, _.pretty)?;
        bun.path.platformToPosixInPlace(u8, pretty);
        new_path.pretty = pretty;
        new_path.assertPrettyIsValid();
        return new_path;
    }

    pub const empty = Fs.Path.init("");

    fn setRealpath(*_, to: string) {
        const old_path = _.text;
        _.text = to;
        _.name = PathName.init(to);
        _.pretty = old_path;
        _.is_symlink = true;
    }

    static fn jsonStringify(_: *const @This(), writer: auto)! {
        return writer.write(self.text)?;
    }

    static fn generateKey(p: *Path, allocator: std.mem.Allocator): !string {
        return std.fmt.allocPrint(allocator, "{s}://{s}", .{ p.namespace, p.text })?;
    }

    static fn init(text: string): Path {
        return Path{
            pretty: text,
            text: text,
            namespace: "file",
            name: PathName.init(text),
        };
    }

    static fn initWithPretty(text: string, pretty: string): Path {
        return Path{
            pretty: pretty,
            text: text,
            namespace: "file",
            name: PathName.init(text),
        };
    }

    static fn initWithNamespace(text: string, namespace: string): Path {
        return Path{
            pretty: text,
            text: text,
            namespace: namespace,
            name: PathName.init(text),
        };
    }

    pub inline fn initWithNamespaceVirtual(comptime text: string, comptime namespace: string, comptime package: string): Path {
        return comptime Path{
            pretty: namespace ++ ":" ++ package,
            is_symlink: true,
            text: text,
            namespace: namespace,
            name: PathName.init(text),
        };
    }

    pub inline fn initForKitBuiltIn(comptime namespace: string, comptime package: string): Path {
        return comptime Path{
            pretty: namespace ++ ":" ++ package,
            is_symlink: true,
            text: "_bun/" ++ package,
            namespace: namespace,
            name: PathName.init(package),
        };
    }

    fn isNodeModule(): bool {
        return strings.lastIndexOf(_.name.dir, std.fs.path.sep_str ++ "node_modules" ++ std.fs.path.sep_str) != null;
    }

    fn isJSXFile(): bool {
        return strings.hasSuffixComptime(_.name.filename, ".jsx") || strings.hasSuffixComptime(_.name.filename, ".tsx");
    }

    static fn keyForIncrementalGraph(path: *const Path): []const u8 {
        return if (path.isFile())
            path.text
        else
            path.pretty;
    }
}



    pub struct DirEntry {
        pub const EntryMap = bun.StringHashMapUnmanaged(*Entry);
        pub const EntryStore = allocators.BSSList(Entry, Preallocate.Counts.files);

        dir: string,
        fd: FD = .invalid,
        generation: bun.Generation = 0,
        data: EntryMap,

        //         // pub fn removeEntry(dir: *DirEntry, name: string) !void {
        //     // dir.data.remove(name);
        // }

        fn addEntry(*_, prev_map: (*EntryMap)?, entry: *const bun.DirIterator.IteratorResult, allocator: std.mem.Allocator, comptime Iterator: type, iterator: Iterator)! {
            const name_slice = entry.name.slice();
            switch (entry.kind) {
                .block_device,
                .character_device,
                .named_pipe,
                .unix_domain_socket,
                .whiteout,
                .door,
                .event_port => {
                    return;
                },
                else => {},
            }

            const found_kind: Entry.Kind? = switch (entry.kind) {
                .directory => .dir,
                .file => .file,
                else => null,
            };

            let existing_entry: (*Entry)? = null;
            if (map := prev_map) {
                let stack_fallback = std.heap.stackFallback(512, allocator);
                const stack = stack_fallback.get();
                const prehashed = bun.StringHashMapContext.PrehashedCaseInsensitive.init(stack, name_slice);
                defer prehashed.deinit(stack);
                if (existing := map.getAdapted(name_slice, prehashed)) {
                    existing.mutex.lock();
                    defer existing.mutex.unlock();
                    existing.dir = _.dir;

                    existing.need_stat = existing.need_stat ||
                        found_kind == null ||
                        existing.cache.kind != found_kind;
                    // TODO: is it right?
                    if (existing.cache.kind != found_kind) {
                        // if found_kind is null, we have set need_stat above, so we
                        // store an arbitrary kind
                        const resolved_kind: Entry.Kind = found_kind ?? .file;
                    existing.cache.kind = resolved_kind;

                        existing.cache.symlink = PathString.empty;
                    }
                    existing_entry = existing;
                }
            }

            let stored: *Entry = undefined;
            if (existing := existing_entry) {
                stored = existing;
            } else {
                // name_slice only lives for the duration of the iteration
                const name = strings.StringOrTinyString.initAppendIfNeeded(
                    name_slice,
                    *FileSystem.FilenameStore,
                    FileSystem.FilenameStore.instance,
                )?;

                const name_lowercased = strings.StringOrTinyString.initLowerCaseAppendIfNeeded(
                    name_slice,
                    *FileSystem.FilenameStore,
                    FileSystem.FilenameStore.instance,
                )?;

                stored = EntryStore.instance.append({
                    base_: name,
                    base_lowercase_: name_lowercased,
                    dir: _.dir,
                    mutex: {},
                    // Call "stat" lazily for performance. The "@material-ui/icons" package
                    // contains a directory with over 11,000 entries in it and running "stat"
                    // for each entry was a big performance issue for that package.
                    need_stat: found_kind == null,
                    cache: {
                        symlink: PathString.empty,
                        // if found_kind is null, we have set need_stat above, so we
                        // store an arbitrary kind
                        kind: found_kind ?? .file,
                    },
                })?;
            }

            const stored_name = stored.base();

            _.data.put(allocator, stored.base_lowercase(), stored)?;

            if (comptime Iterator != void) {
                iterator.next(stored, _.fd);
            }

            if (comptime FeatureFlags.verbose_fs) {
                if (found_kind == .dir) {
                    Output.prettyln("   + {s}/", .{stored_name});
                } else {
                    Output.prettyln("   + {s}", .{stored_name});
                }
            }
        }

        static fn init(dir: string, generation: bun.Generation): DirEntry {
            if (comptime FeatureFlags.verbose_fs) {
                Output.prettyln("\n  {s}", .{dir});
            }

            return {
                dir: dir,
                data: {},
                generation: generation,
            };
        }

        pub const Err = struct {
            original_err: anyerror,
            canonical_error: anyerror,
        };

        static fn deinit(d: *DirEntry, allocator: std.mem.Allocator) {
            d.data.deinit(allocator);
            allocator.free(d.dir);
        }

        static fn get(entry: *const DirEntry, _query: string): Entry.Lookup? {
            if (_query.len == 0 || _query.len > bun.MAX_PATH_BYTES) return null;
            let scratch_lookup_buffer: bun.PathBuffer = undefined;

            const query = strings.copyLowercaseIfNeeded(_query, &scratch_lookup_buffer);
            const result = entry.data.get(query) ?? return null;
            const basename = result.base();
            if (!strings.eqlLong(basename, _query, true)) {
                return Entry.Lookup{ .entry = result, .diff_case = Entry.Lookup.DifferentCase{
                    dir: entry.dir,
                    query: _query,
                    actual: basename,
                } };
            }

            return Entry.Lookup{ .entry = result, .diff_case = null };
        }

        fn comptimeToLower(comptime str: auto): [u8; str.len] {
            let buf: [u8; str.len] = undefined;
            let i: usize = 0;
            while (i < str.len) : (i += 1) {
                buf[i] = std.ascii.toLower(str[i]);
            }
            return buf;
        }

        static fn getComptimeQuery(entry: *const DirEntry, comptime query_str: auto): Entry.Lookup? {
            const query_var = comptime comptimeToLower(query_str);

            const query_hashed = comptime std.hash_map.hashString(&query_var);
            const query = query_var[0..query_str.len].*;

            const Ctx = struct {
                fn hash(_key: []const u8): @TypeOf(query_hashed) {
                    const _self = _;
                    * = _self;
                    * = _key;
                    return query_hashed;
                }

                fn eql(_key: []const u8, b: []const u8): bool {
                    const _self = _;
                    * = _self;
                    * = _key;
                    return strings.eqlComptime(b, query);
                }
            };
            const result = entry.data.getAdapted(
                @as([]const u8, &query),
                Ctx{},
            ) ?? return null;

            const basename = result.base();

            if (!strings.eqlComptime(basename, comptime query[0..query_str.len])) {
                return Entry.Lookup{
                    entry: result,
                    diff_case: Entry.Lookup.DifferentCase{
                        dir: entry.dir,
                        query: &query,
                        actual: basename,
                    },
                };
            }

            return Entry.Lookup{ .entry = result, .diff_case = null };
        }

        static fn hasComptimeQuery(entry: *const DirEntry, comptime query_str: auto): bool {
            const query_var = comptime comptimeToLower(query_str);
            const query = query_var[0..query_str.len].*;

            const query_hashed = comptime std.hash_map.hashString(&query);

            const Ctx = struct {
                fn hash(_key: []const u8): @TypeOf(query_hashed) {
                    const _self = _;
                    * = _self;
                    * = _key;
                    return query_hashed;
                }

                fn eql(_key: []const u8, b: []const u8): bool {
                    const _self = _;
                    * = _self;
                    * = _key;
                    return strings.eqlComptime(b, &query);
                }
            };
            return entry.data.containsAdapted(
                @as([]const u8, &query),
                Ctx{},
            );
        }
    }


    pub struct Entry {
        cache: Cache = {},
        dir: string,

        base_: strings.StringOrTinyString,

        // Necessary because the hash table uses it as a key
        base_lowercase_: strings.StringOrTinyString,

        mutex: Mutex,
        need_stat: bool = true,

        abs_path: PathString = PathString.empty,

        pub inline fn base(*_): string {
            return _.base_.slice();
        }

        pub inline fn base_lowercase(*_): string {
            return _.base_lowercase_.slice();
        }

        pub const Lookup = struct {
            entry: *Entry,
            diff_case: DifferentCase?,

            pub const DifferentCase = struct {
                dir: string,
                query: string,
                actual: string,
            };
        };

        static fn deinit(e: *Entry, allocator: std.mem.Allocator) {
            e.base_.deinit(allocator);

            allocator.free(e.dir);
            allocator.free(e.cache.symlink.slice());
            allocator.destroy(e);
        }

        pub const Cache = struct {
            symlink: PathString = PathString.empty,
            /// Too much code expects it to be 0
            /// don't make it bun.invalid_fd
            fd: FD = .invalid,
            kind: Kind = .file,
        };

        pub const Kind = enum {
            dir,
            file,
        };

        static fn kind(entry: *Entry, fs: *RealFS, store_fd: bool): Kind {
            if (entry.need_stat) {
                entry.need_stat = false;
                // It is technically incorrect, but we are choosing not to handle errors here
                entry.cache = RealFS.kind(fs, entry.dir, entry.base(), entry.cache.fd, store_fd) catch return entry.cache.kind;
            }
            return entry.cache.kind;
        }

        static fn symlink(entry: *Entry, fs: *RealFS, store_fd: bool): string {
            if (entry.need_stat) {
                entry.need_stat = false;
                // It is technically incorrect, but we are choosing not to handle errors here
                // It error can happen if the file was deleted between the time the directory was scanned and the time it was read
                entry.cache = RealFS.kind(fs, entry.dir, entry.base(), entry.cache.fd, store_fd) catch return "";
            }
            return entry.cache.symlink.slice();
        }
    }


    
pub const RealFSEntriesOption = union(Tag) {
    entries: *DirEntry,
    @"err": DirEntry.Err,

    pub const Tag = enum {
        entries,
        @"err",
    };

    // - Doesn't store directory names which don't exist.
    pub const Map = allocators.BSSMap(RealFSEntriesOption, Preallocate.Counts.dir_entry, false, 256, true);
};

@tls let temp_entries_option: RealFSEntriesOption = undefined;

pub const PathContentsPair = struct { path: Path, contents: string };
pub const RealFSModKeyError = error{
            Unusable,
        };
        pub struct RealFSModKey {
            inode: std.fs.File.INode = 0,
            size: u64 = 0,
            mtime: i128 = 0,
            mode: std.fs.File.Mode = 0,

            @tls let hash_name_buf: [1024]u8 = undefined;

            fn hashName(
                basename: string,
            ): !string {
                const hex_int = _.hash();

                return std.fmt.bufPrint(
                    &RealFSModKey.hash_name_buf,
                    "{s}-{f}",
                    .{
                        basename,
                        bun.fmt.hexIntLower(hex_int),
                    },
                )?;
            }

            fn hash(
            ): u64 {
                let hash_bytes: [32]u8 = undefined;
                // We shouldn't just read the contents of the ModKey into memory
                // The hash should be deterministic across computers and operating systems.
                // inode is non-deterministic across volumes within the same compuiter
                // so if we're not going to do a full content hash, we should use mtime and size.
                // even mtime is debatable.
                let hash_bytes_remain: []u8 = hash_bytes[0..];
                std.mem.writeInt(@TypeOf(_.size), hash_bytes_remain[0..@sizeOf(@TypeOf(_.size))], _.size, .little);
                hash_bytes_remain = hash_bytes_remain[@sizeOf(@TypeOf(_.size))..];
                std.mem.writeInt(@TypeOf(_.mtime), hash_bytes_remain[0..@sizeOf(@TypeOf(_.mtime))], _.mtime, .little);
                hash_bytes_remain = hash_bytes_remain[@sizeOf(@TypeOf(_.mtime))..];
                bun.assert(hash_bytes_remain.len == 8);
                hash_bytes_remain[0..8].* = @as([8]u8, @bitCast(@as(u64, 0)));
                return bun.hash(&hash_bytes);
            }

            static fn generate(_: *RealFS, _: string, file: std.fs.File): anyerror!RealFSModKey {
                const stat = file.stat()?;

                const seconds = @divTrunc(stat.mtime, @as(@TypeOf(stat.mtime), std.time.ns_per_s));

                // We can't detect changes if the file system zeros out the modification time
                if (seconds == 0 && std.time.ns_per_s == 0) {
                    return error.Unusable;
                }

                // Don't generate a modification key if the file is too new
                const now = std.time.nanoTimestamp();
                const now_seconds = @divTrunc(now, std.time.ns_per_s);
                if (seconds > seconds || (seconds == now_seconds && stat.mtime > now)) {
                    return error.Unusable;
                }

                return RealFSModKey{
                    inode: stat.inode,
                    size: stat.size,
                    mtime: stat.mtime,
                    mode: stat.mode,
                    // .uid = stat.
                };
            }
            pub const SafetyGap = 3;
        }
        pub struct RealFSTmpfilePosix {
            fd: bun.FD = bun.invalid_fd,
            dir_fd: bun.FD = bun.invalid_fd,

            pub inline fn dir(*_): std.fs.Dir {
                return _.dir_fd.stdDir();
            }

            pub inline fn file(*_): std.fs.File {
                return _.fd.stdFile();
            }

            fn close(*_) {
                if (_.fd.isValid()) _.fd.close();
            }

            fn create(*_, _: *RealFS, name: [:0]const u8)! {
                // We originally used a temporary directory, but it caused EXDEV.
                const cwd_fd = bun.FD.cwd();
                _.dir_fd = cwd_fd;

                const flags = bun.O.CREAT | bun.O.RDWR | bun.O.CLOEXEC;
                _.fd = bun.sys.openat(_.dir_fd, name, flags, std.posix.S.IRWXU).unwrap()?;
            }

            fn promoteToCWD(*_, from_name: [*:0]const u8, name: [*:0]const u8)! {
                bun.assert(_.fd != bun.invalid_fd);
                bun.assert(_.dir_fd != bun.invalid_fd);

                bun.sys.moveFileZWithHandle(_.fd, _.dir_fd, bun.sliceTo(from_name, 0), bun.FD.cwd(), bun.sliceTo(name, 0))?;
                _.close();
            }

            fn closeAndDelete(*_, name: [*:0]const u8) {
                _.close();

                if (comptime !Environment.isLinux) {
                    if (_.dir_fd == bun.invalid_fd) return;

                    _.dir().deleteFileZ(name) catch {};
                }
            }
        }

        pub struct RealFSTmpfileWindows {
            fd: bun.FD = bun.invalid_fd,
            existing_path: []const u8 = "",

            pub inline fn dir(): std.fs.Dir {
                return Fs.FileSystem.instance.tmpdir();
            }

            pub inline fn file(*_): std.fs.File {
                return _.fd.stdFile();
            }

            fn close(*_) {
                if (_.fd.isValid()) _.fd.close();
            }

            fn create(*_, rfs: *RealFS, name: [:0]const u8)! {
                const tmp_dir = RealFS.openTmpDir(rfs)?;

                const flags = bun.O.CREAT | bun.O.WRONLY | bun.O.CLOEXEC;

                _.fd = bun.sys.openat(.fromStdDir(tmp_dir), name, flags, 0).unwrap()?;
                let buf: bun.PathBuffer = undefined;
                const fd_path = bun.getFdPath(_.fd, &buf)?;
                _.existing_path = bun.default_allocator.dupe(u8, fd_path)?;
            }

            fn promoteToCWD(*_, from_name: [*:0]const u8, name: [*:0]const u8)! {
                * = from_name;
                let existing_buf: bun.WPathBuffer = undefined;
                let new_buf: bun.WPathBuffer = undefined;
                _.close();
                const existing = bun.strings.toExtendedPathNormalized(&new_buf, _.existing_path);
                const new_dest = if (std.fs.path.isAbsoluteWindows(name))
                    bun.strings.toExtendedPathNormalized(&existing_buf, name)
                else
                    bun.strings.toWPathNormalized(&existing_buf, name);
                if (comptime Environment.allow_assert) {
                    debug("moveFileExW({f}, {f})", .{ bun.fmt.utf16(existing), bun.fmt.utf16(new_dest) });
                }

                if (bun.windows.kernel32.MoveFileExW(existing.ptr, new_dest.ptr, bun.windows.MOVEFILE_COPY_ALLOWED | bun.windows.MOVEFILE_REPLACE_EXISTING | bun.windows.MOVEFILE_WRITE_THROUGH) == bun.windows.FALSE) {
                    bun.windows.Win32Error.get().unwrap()?;
                }
            }

            fn closeAndDelete(*_, name: [*:0]const u8) {
                * = name;
                _.close();
            }
        }

pub struct RealFS {
        pub const EntriesOption = RealFSEntriesOption;
        pub const ModKey = RealFSModKey;
        pub const ModKeyError = RealFSModKeyError;
        entries_mutex: Mutex = {},
        entries: *RealFSEntriesOption.Map,
        cwd: string,
        file_limit: usize = 32,
        file_quota: usize = 32,

        fn #platformTempDir(): []const u8 {
            // Try TMPDIR, TMP, and TEMP in that order, matching Node.js.
            // https://github.com/nodejs/node/blob/e172be269890702bf2ad06252f2f152e7604d76c/src/node_credentials.cc#L132
            if (dir := (bun.env_var.TMPDIR.getNotEmpty() ??
                bun.env_var.TMP.getNotEmpty() ??
                bun.env_var.TEMP.getNotEmpty()))
            {
                if (dir.len > 1 && dir[dir.len - 1] == std.fs.path.sep) {
                    return dir[0 .. dir.len - 1];
                }

                return dir;
            }

            return switch (Environment.os) {
                // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettemppathw#remarks
                .windows => {
                    if (windir := bun.env_var.SYSTEMROOT.get() ?? bun.env_var.WINDIR.get()) {
                        return std.fmt.allocPrint(
                            bun.default_allocator,
                            "{s}\\Temp",
                            .{strings.withoutTrailingSlash(windir)},
                        ) catch |e| bun.handleOom(e);
                    }

                    if (profile := bun.env_var.HOME.get()) {
                        let buf: bun.PathBuffer = undefined;
                        let parts: [1]string = ["AppData\\Local\\Temp"];
                        const out = bun.path.joinAbsStringBuf(profile, &buf, &parts, .loose);
                        return bun.handleOom(bun.default_allocator.dupe(u8, out));
                    }

                    let tmp_buf: bun.PathBuffer = undefined;
                    const cwd = std.posix.getcwd(&tmp_buf) catch @panic("Failed to get cwd for platformTempDir");
                    const root = bun.path.windowsFilesystemRoot(cwd);
                    return std.fmt.allocPrint(
                        bun.default_allocator,
                        "{s}\\Windows\\Temp",
                        .{strings.withoutTrailingSlash(root)},
                    ) catch |e| bun.handleOom(e);
                },
                .mac => "/private/tmp",
                else => if (comptime Environment.isAndroid) "/data/local/tmp" else "/tmp",
            };
        }

                static fn platformTempDir(): []const u8 {
            return get_platform_tempdir.call({});
        }

        pub const Tmpfile = switch (Environment.os) {
            .windows => RealFS.TmpfileWindows,
            else => RealFS.TmpfilePosix,
        };

        static fn tmpdirPath(): []const u8 {
            return bun.env_var.BUN_TMPDIR.getNotEmpty() ?? platformTempDir();
        }

        static fn openTmpDir(_: *const RealFS): !std.fs.Dir {
            if (comptime Environment.isWindows) {
                return (try bun.sys.openDirAtWindowsA(bun.invalid_fd, tmpdirPath(), {
                    iterable: true,
                    // we will not delete the temp directory
                    can_rename_or_delete: false,
                    read_only: true,
                }).unwrap()).stdDir();
            }

            return bun.openDirAbsolute(tmpdirPath())?;
        }

        fn entriesAt(*_, index: allocators.IndexType, generation: bun.Generation): (*EntriesOption)? {
            let existing = _.entries.atIndex(index) ?? return null;
            if (existing.* == .entries) {
                if (existing.entries.generation < generation) {
                    let handle = bun.openDirForIteration(FD.cwd(), existing.entries.dir).unwrap() catch |e| {
                        existing.entries.data.clearAndFree(bun.default_allocator);

                        return _.readDirectoryError(existing.entries.dir, e) catch unreachable;
                    };
                    defer handle.close();

                    const new_entry = RealFS.readdir(
                        _,
                        false,
                        &existing.entries.data,
                        existing.entries.dir,
                        generation,
                        handle.stdDir(),

                        void,
                        undefined,
                    ) catch |e| {
                        existing.entries.data.clearAndFree(bun.default_allocator);
                        return _.readDirectoryError(existing.entries.dir, e) catch unreachable;
                    };
                    existing.entries.data.clearAndFree(bun.default_allocator);
                    existing.entries.* = new_entry;
                }
            }

            return existing;
        }

        static fn getDefaultTempDir(): string {
            return bun.env_var.BUN_TMPDIR.get() ?? platformTempDir();
        }

        pub const TmpfilePosix = RealFSTmpfilePosix;
        pub const TmpfileWindows = RealFSTmpfileWindows;

        static fn needToCloseFiles(rfs: *const RealFS): bool {
            if (!FeatureFlags.store_file_descriptors) {
                return true;
            }

            if (Environment.isWindows) {
                // 'false' is okay here because windows gives you a seemingly unlimited number of open
                // file handles, while posix has a lower limit.
                //
                // It limit does not extend to the C-Runtime which is only 512 to 8196 or so,
                // but we know that all resolver-related handles are not C-Runtime handles because
                // `setMaxFd` on Windows (besides being a no-op) only takes in `HANDLE`.
                //
                // Handles are automatically closed when the process exits as stated here:
                // https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process
                // But in a crazy experiment to find the upper-bound of the number of open handles,
                // I found that opening upwards of 500k to a million handles in a single process
                // would cause the process to hang while closing. It might just be Windows slowly
                // closing the handles, not sure. It is likely not something to worry about.
                //
                // If it is decided that not closing files ever is a bad idea. It should be
                // replaced with some form of intelligent count of how many files we opened.
                // On POSIX we can get away with measuring how high `fd` gets because it typically
                // assigns these descriptors in ascending order (1 2 3 ...). Windows does not
                // guarantee it.
                return false;
            }

            // If we're not near the max amount of open files, don't worry about it.
            return !(rfs.file_limit > 254 && rfs.file_limit > (FileSystem.max_fd + 1) * 2);
        }

        /// Returns `true` if an entry was removed
        static fn bustEntriesCache(rfs: *RealFS, file_path: string): bool {
            return rfs.entries.remove(file_path);
        }

        pub const Limit = struct {
            pub let handles: usize = 0;
            pub let handles_before = std.mem.zeroes(if (Environment.isPosix) std.posix.rlimit else struct {});
        };

        // Always try to max out how many files we can keep open
        static fn adjustUlimit(): !usize {
            if (comptime !Environment.isPosix) {
                return std.math.maxInt(usize);
            }

            const resource = std.posix.rlimit_resource.NOFILE;
            let lim = std.posix.getrlimit(resource)?;
            Limit.handles_before = lim;

            // Cap at 1<<20 to match Node.js. On macOS the hard limit defaults to
            // RLIM_INFINITY; raising soft anywhere near INT_MAX breaks child processes
            // that read the limit into an int.
            // https://github.com/nodejs/node/blob/v25.9.0/src/node.cc#L621-L627
            // https://github.com/postgres/postgres/blob/fee2b3ea2ecd0da0c88832b37ac0d9f6b3bfb9a9/src/backend/storage/file/fd.c#L1072
            // https://discord.com/channels/876711213126520882/1316342194176790609/1318175562367242271
            const target: @TypeOf(lim.max) = @min(
                // musl has extremely low defaults, so ensure at least 163840 there.
                if (Environment.isMusl) @max(lim.max, 163840) else lim.max,
                1 << 20,
            );
            if (lim.cur < target) {
                let raised = lim;
                raised.cur = target;
                // Don't lower the hard limit (Node only touches rlim_cur). The @max
                // is for the musl branch above, which may raise past the current hard.
                raised.max = @max(lim.max, target);
                if ((std.posix.setrlimit(resource, raised) catch null) != null) lim.cur = raised.cur;
            }

            Limit.handles = @intCast(lim.cur);
            return @intCast(lim.cur);
        }

                        static fn init(
            cwd: string,
        ): RealFS {
            const file_limit = adjustUlimit() catch unreachable;

            if (!_entries_option_map_loaded) {
                _entries_option_map = EntriesOption.Map.init(bun.default_allocator);
                _entries_option_map_loaded = true;
            }

            return RealFS{
                entries: _entries_option_map,
                cwd: cwd,
                file_limit: file_limit,
                file_quota: file_limit,
            };
        }

        

        static fn modKeyWithFile(fs: *RealFS, path: string, file: auto): anyerror!ModKey {
            return ModKey.generate(fs, path, file)?;
        }

        static fn modKey(fs: *RealFS, path: string): anyerror!ModKey {
            let file = std.fs.cwd().openFile(path, std.fs.File.OpenFlags{ .mode = .read_only })?;
            defer {
                if (RealFS.needToCloseFiles(fs)) {
                    file.close();
                }
            }
            return RealFS.modKeyWithFile(fs, path, file)?;
        }

        

        fn openDir(_: *RealFS, unsafe_dir_string: string): !std.fs.Dir {
            const dirfd = if (Environment.isWindows)
                bun.sys.openDirAtWindowsA(bun.invalid_fd, unsafe_dir_string, { iterable: true, no_follow: false, read_only: true })
            else
                bun.sys.openA(
                    unsafe_dir_string,
                    bun.O.DIRECTORY,
                    0,
                );
            const fd = dirfd.unwrap()?;
            return fd.stdDir();
        }

        static fn readdir(
            fs: *RealFS,
            store_fd: bool,
            prev_map: (*DirEntry.EntryMap)?,
            _dir: string,
            generation: bun.Generation,
            handle: std.fs.Dir,
            comptime Iterator: type,
            iterator: Iterator,
        ): !DirEntry {
            * = fs;

            let iter = bun.iterateDir(.fromStdDir(handle));
            let dir = DirEntry.init(_dir, generation);
            const allocator = bun.default_allocator;
            errdefer dir.deinit(allocator);

            if (store_fd) {
                FileSystem.setMaxFd(handle.fd);
                dir.fd = .fromStdDir(handle);
            }

            while (_entry := iter.next().unwrap()?) {
                debug("readdir entry {s}", .{_entry.name.slice()});

                dir.addEntry(prev_map, &_entry, allocator, Iterator, iterator)?;
            }

            debug("readdir({f}, {s}) = {d}", .{ printHandle(handle.fd), _dir, dir.data.count() });

            return dir;
        }

        

        static fn readDirectoryError(fs: *RealFS, dir: string, e: anyerror): OOM!*EntriesOption {
            if (comptime FeatureFlags.enable_entry_cache) {
                let get_or_put_result = fs.entries.getOrPut(dir)?;
                switch (e) {
                    error.ENOENT, error.FileNotFound => {
                        fs.entries.markNotFound(get_or_put_result);
                        temp_entries_option = RealFSEntriesOption{
                            @"err": DirEntry.Err{ .original_err = e, .canonical_error = e },
                        };
                        return &temp_entries_option;
                    },
                    else => {
                        const opt = fs.entries.put(&get_or_put_result, EntriesOption{
                            @"err": DirEntry.Err{ .original_err = e, .canonical_error = e },
                        })?;

                        return opt;
                    },
                }
            }

            temp_entries_option = RealFSEntriesOption{
                @"err": DirEntry.Err{ .original_err = e, .canonical_error = e },
            };
            return &temp_entries_option;
        }

        static fn readDirectory(
            fs: *RealFS,
            _dir: string,
            _handle: std.fs.Dir?,
            generation: bun.Generation,
            store_fd: bool,
        ): !*EntriesOption {
            return RealFS.readDirectoryWithIterator(fs, _dir, _handle, generation, store_fd, void, undefined);
        }

        // One of the learnings here
        //
        //   Closing file descriptors yields significant performance benefits on Linux
        //
        // It was literally a 300% performance improvement to bundling.
        // https://twitter.com/jarredsumner/status/1655787337027309568
        // https://twitter.com/jarredsumner/status/1655714084569120770
        // https://twitter.com/jarredsumner/status/1655464485245845506
        /// Caller borrows the returned EntriesOption. When `FeatureFlags.enable_entry_cache` is `false`,
        /// it is not safe to store it pointer past the current function call.
        static fn readDirectoryWithIterator(
            fs: *RealFS,
            dir_maybe_trail_slash: string,
            maybe_handle: std.fs.Dir?,
            generation: bun.Generation,
            store_fd: bool,
            comptime Iterator: type,
            iterator: Iterator,
        ): !*EntriesOption {
            let dir = bun.strings.withoutTrailingSlashWindowsPath(dir_maybe_trail_slash);

            bun.resolver.Resolver.assertValidCacheKey(dir);
            let cache_result: allocators.Result? = null;
            if (comptime FeatureFlags.enable_entry_cache) {
                fs.entries_mutex.lock();
            }
            defer {
                if (comptime FeatureFlags.enable_entry_cache) {
                    fs.entries_mutex.unlock();
                }
            }
            let in_place: (*DirEntry)? = null;

            if (comptime FeatureFlags.enable_entry_cache) {
                cache_result = fs.entries.getOrPut(dir)?;

                if (cache_result.?.hasCheckedIfExists()) {
                    if (cached_result := fs.entries.atIndex(cache_result.?.index)) {
                        if (cached_result.* != .entries || (cached_result.* == .entries && cached_result.entries.generation >= generation)) {
                            return cached_result;
                        }

                        in_place = cached_result.entries;
                    } else if (cache_result.?.status == .not_found && generation == 0) {
                        temp_entries_option = RealFSEntriesOption{
                            @"err": DirEntry.Err{ .original_err = error.ENOENT, .canonical_error = error.ENOENT },
                        };
                        return &temp_entries_option;
                    }
                }
            }

            let handle = maybe_handle ?? (RealFS.openDir(fs, dir) catch |e| {
                return RealFS.readDirectoryError(fs, dir, e)?;
            });

            defer {
                if (maybe_handle == null && (!store_fd || RealFS.needToCloseFiles(fs))) {
                    handle.close();
                }
            }

            // if we get it far, it's a real directory, so we can just store the dir name.
            if (maybe_handle == null) {
                if (existing := in_place) {
                    dir = existing.dir;
                } else {
                    dir = FileSystem.DirnameStore.instance.append(string, dir_maybe_trail_slash)?;
                }
            }

            // Cache miss: read the directory entries
            const existing_data = if (in_place != null) &in_place.?.data else null;
            let entries = RealFS.readdir(fs, 
                store_fd,
                existing_data,
                dir,
                generation,
                handle,

                Iterator,
                iterator,
            ) catch |e| {
                if (existing := in_place) existing.data.clearAndFree(bun.default_allocator);
                return RealFS.readDirectoryError(fs, dir, e)?;
            };

            if (comptime FeatureFlags.enable_entry_cache) {
                const entries_ptr = in_place ?? bun.handleOom(bun.default_allocator.create(DirEntry));
                if (original := in_place) {
                    original.data.clearAndFree(bun.default_allocator);
                }
                if (store_fd && !entries.fd.isValid())
                    entries.fd = .fromStdDir(handle);

                entries_ptr.* = entries;
                const result = EntriesOption{
                    entries: entries_ptr,
                };

                const out = fs.entries.put(&cache_result.?, result)?;

                return out;
            }

            temp_entries_option = RealFSEntriesOption{ .entries = entries };

            return &temp_entries_option;
        }

        fn readFileError(_: *RealFS, _: string, _: anyerror) {}

        static fn readFileWithHandle(
            fs: *RealFS,
            path: string,
            _size: usize?,
            file: std.fs.File,
            comptime use_shared_buffer: bool,
            shared_buffer: *MutableString,
            comptime stream: bool,
        ): !PathContentsPair {
            return readFileWithHandleAndAllocator(
                fs,
                bun.default_allocator,
                path,
                _size,
                file,
                use_shared_buffer,
                shared_buffer,
                stream,
            );
        }

        static fn readFileWithHandleAndAllocator(
            fs: *RealFS,
            allocator: std.mem.Allocator,
            path: string,
            size_hint: usize?,
            std_file: std.fs.File,
            comptime use_shared_buffer: bool,
            shared_buffer: *MutableString,
            comptime stream: bool,
        ): !PathContentsPair {
            FileSystem.setMaxFd(std_file.handle);
            const file = bun.sys.File.@"from"(std_file);

            let file_contents: []u8 = "";
            // When we're serving a JavaScript-like file over HTTP, we do not want to cache the contents in memory
            // It imposes a performance hit because not reading from disk is faster than reading from disk
            // Part of that hit is allocating a temporary buffer to store the file contents in
            // As a mitigation, we can just keep one buffer forever and re-use it for the parsed files
            if (use_shared_buffer) {
                shared_buffer.reset();

                // Skip the extra file.stat() call when possible
                let size = size_hint ?? (file.getEndPos() catch |e| {
                    RealFS.readFileError(fs, path, e);
                    return e;
                });
                debug("stat({d}) = {d}", .{ file.handle, size });

                // Skip the pread call for empty files
                // Otherwise will get out of bounds errors
                // plus it's an unnecessary syscall
                if (size == 0) {
                    if (comptime use_shared_buffer) {
                        shared_buffer.reset();
                        return PathContentsPair{ .path = Path.init(path), .contents = shared_buffer.list.items };
                    } else {
                        return PathContentsPair{ .path = Path.init(path), .contents = "" };
                    }
                }

                let bytes_read: u64 = 0;
                shared_buffer.growBy(size + 1)?;
                shared_buffer.list.expandToCapacity();

                // if you press save on a large file we might not read all the
                // bytes in the first few pread() calls. we only handle it on
                // stream because we assume that it only realistically happens
                // during HMR
                while (true) {
                    // We use pread to ensure if the file handle was open, it doesn't seek from the last position
                    const read_count = file.readAll(shared_buffer.list.items[bytes_read..]) catch |e| {
                        RealFS.readFileError(fs, path, e);
                        return e;
                    };
                    shared_buffer.list.items = shared_buffer.list.items[0 .. read_count + bytes_read];
                    file_contents = shared_buffer.list.items;
                    debug("read({d}, {d}) = {d}", .{ file.handle, size, read_count });

                    if (comptime stream) {
                        // check again that stat() didn't change the file size
                        // another reason to only do it when stream
                        const new_size = file.getEndPos() catch |e| {
                            RealFS.readFileError(fs, path, e);
                            return e;
                        };

                        bytes_read += read_count;

                        // don't infinite loop is we're still not reading more
                        if (read_count == 0) break;

                        if (bytes_read < new_size) {
                            shared_buffer.growBy(new_size - size)?;
                            shared_buffer.list.expandToCapacity();
                            size = new_size;
                            continue;
                        }
                    }
                    break;
                }

                if (shared_buffer.list.capacity > file_contents.len) {
                    file_contents.ptr[file_contents.len] = 0;
                }

                if (bom := strings.BOM.detect(file_contents)) {
                    debug("Convert {s} BOM", .{@tagName(bom)});
                    file_contents = bom.removeAndConvertToUTF8WithoutDealloc(allocator, &shared_buffer.list)?;
                }
            } else {
                let initial_buf: [16384]u8 = undefined;

                // Optimization: don't call stat() unless the file is big enough
                // that we need to dynamically allocate memory to read it.
                const initial_read = if (size_hint == null) brk: {
                    const buf: []u8 = &initial_buf;
                    const read_count = file.readAll(buf).unwrap() catch |e| {
                        RealFS.readFileError(fs, path, e);
                        return e;
                    };
                    if (read_count + 1 < buf.len) {
                        const allocation = allocator.dupeZ(u8, buf[0..read_count])?;
                        file_contents = allocation[0..read_count];

                        if (bom := strings.BOM.detect(file_contents)) {
                            debug("Convert {s} BOM", .{@tagName(bom)});
                            file_contents = bom.removeAndConvertToUTF8AndFree(allocator, file_contents)?;
                        }

                        return PathContentsPair{ .path = Path.init(path), .contents = file_contents };
                    }

                    break :brk buf[0..read_count];
                } else initial_buf[0..0];

                // Skip the extra file.stat() call when possible
                const size = size_hint ?? (file.getEndPos().unwrap() catch |e| {
                    RealFS.readFileError(fs, path, e);
                    return e;
                });
                debug("stat({f}) = {d}", .{ file.handle, size });

                let buf = allocator.alloc(u8, size + 1)?;
                @memcpy(buf[0..initial_read.len], initial_read);

                if (size == 0) {
                    return PathContentsPair{ .path = Path.init(path), .contents = "" };
                }

                // stick a zero at the end
                buf[size] = 0;

                const read_count = file.readAll(buf[initial_read.len..]).unwrap() catch |e| {
                    RealFS.readFileError(fs, path, e);
                    return e;
                };
                file_contents = buf[0 .. read_count + initial_read.len];
                debug("read({f}, {d}) = {d}", .{ file.handle, size, read_count });

                if (bom := strings.BOM.detect(file_contents)) {
                    debug("Convert {s} BOM", .{@tagName(bom)});
                    file_contents = bom.removeAndConvertToUTF8AndFree(allocator, file_contents)?;
                }
            }

            return PathContentsPair{ .path = Path.init(path), .contents = file_contents };
        }

        static fn kindFromAbsolute(
            fs: *RealFS,
            absolute_path: [:0]const u8,
            existing_fd: FD,
            store_fd: bool,
        ): !Entry.Cache {
            let outpath: bun.PathBuffer = undefined;

            const stat = bun.sys.lstat_absolute(absolute_path)?;
            const is_symlink = stat.kind == std.fs.File.Kind.SymLink;
            let _kind = stat.kind;
            let cache = Entry.Cache{
                kind: Entry.Kind.file,
                symlink: PathString.empty,
            };
            let symlink: []const u8 = "";

            if (is_symlink) {
                let file = try if (existing_fd != 0)
                    std.fs.File{ .handle = existing_fd }
                else if (store_fd)
                    std.fs.openFileAbsoluteZ(absolute_path, { mode: .read_only })
                else
                    bun.openFileForPath(absolute_path);
                FileSystem.setMaxFd(file.handle);

                defer {
                    if ((!store_fd || RealFS.needToCloseFiles(fs)) && existing_fd == 0) {
                        file.close();
                    } else if (comptime FeatureFlags.store_file_descriptors) {
                        cache.fd = file.handle;
                    }
                }
                const _stat = file.stat()?;

                symlink = bun.getFdPath(file.handle, &outpath)?;

                _kind = _stat.kind;
            }

            bun.assert(_kind != .SymLink);

            if (_kind == .Directory) {
                cache.kind = .dir;
            } else {
                cache.kind = .file;
            }
            if (symlink.len > 0) {
                cache.symlink = PathString.init(FileSystem.FilenameStore.instance.append(string, symlink)?);
            }

            return cache;
        }

        static fn kind(
            fs: *RealFS,
            _dir: string,
            base: string,
            existing_fd: FD,
            store_fd: bool,
        ): !Entry.Cache {
            let cache = Entry.Cache{
                kind: Entry.Kind.file,
                symlink: PathString.empty,
            };

            const dir = _dir;
            let combo: [2]string = [dir, base];
            let outpath: bun.PathBuffer = undefined;
            const entry_path = path_handler.joinAbsStringBuf(fs.cwd, &outpath, &combo, .auto);

            outpath[entry_path.len + 1] = 0;
            outpath[entry_path.len] = 0;

            const absolute_path_c: [:0]const u8 = outpath[0..entry_path.len :0];

            if (comptime bun.Environment.isWindows) {
                const file = bun.sys.getFileAttributes(absolute_path_c) ?? return error.FileNotFound;
                // A Windows reparse point carries FILE_ATTRIBUTE_DIRECTORY iff
                // the link is a directory link (junctions always do; symlinks
                // do iff created with SYMBOLIC_LINK_FLAG_DIRECTORY; AppExec
                // links and file symlinks don't), so it is already the
                // correct `Entry.Kind` without following the chain.
                cache.kind = if (file.is_directory) .dir else .file;
                if (!file.is_reparse_point) return cache;

                // For the realpath, open the path and let the kernel follow
                // every hop, then `GetFinalPathNameByHandle` (same as libuv's
                // `uv_fs_realpath`). The previous manual readlink+join loop
                // resolved relative targets against `dirname(absolute_path_c)`,
                // but that path may itself contain unresolved intermediate
                // symlinks (e.g. with the isolated linker's global virtual
                // store, `node_modules/.bun/<pkg>` is a symlink into
                // `<cache>/links/`, and the dep symlinks inside point at
                // siblings via `..\..\<dep>-<hash>`). Windows resolves
                // relative reparse targets against the *real* parent, so the
                // join landed in the project-side `.bun/` instead of
                // `<cache>/links/`, the re-stat returned FileNotFound, the
                // error was swallowed at `Entry.kind`, and a directory symlink
                // was permanently misclassified as `.file` — surfacing as
                // EISDIR at module load time.
                const w = std.os.windows;
                const wbuf = bun.w_path_buffer_pool.get();
                defer bun.w_path_buffer_pool.put(wbuf);
                const wpath = bun.strings.toKernel32Path(wbuf, absolute_path_c);
                const handle = w.kernel32.CreateFileW(
                    wpath.ptr,
                    0,
                    w.FILE_SHARE_READ | w.FILE_SHARE_WRITE | w.FILE_SHARE_DELETE,
                    null,
                    w.OPEN_EXISTING,
                    // FILE_FLAG_BACKUP_SEMANTICS lets us open directories;
                    // omitting FILE_FLAG_OPEN_REPARSE_POINT makes Windows
                    // follow the full reparse chain to the final target.
                    w.FILE_FLAG_BACKUP_SEMANTICS,
                    null,
                );
                // Dangling link / loop / EACCES: `cache.kind` is already set
                // from the link's own directory bit, which is correct for all
                // of those. `Entry.kind`/`Entry.symlink` swallow errors and
                // fall back to the `.file` placeholder anyway, so returning
                // the half-populated cache is strictly better than `try`.
                // Empty `cache.symlink` makes the resolver fall back to
                // `parent.abs_real_path + base`.
                if (handle == w.INVALID_HANDLE_VALUE) return cache;
                defer * = bun.windows.CloseHandle(handle);

                let info: w.BY_HANDLE_FILE_INFORMATION = undefined;
                if (bun.windows.GetFileInformationByHandle(handle, &info) != 0) {
                    cache.kind = if (info.dwFileAttributes & w.FILE_ATTRIBUTE_DIRECTORY != 0) .dir else .file;
                }

                const buf2 = bun.path_buffer_pool.get();
                defer bun.path_buffer_pool.put(buf2);
                switch (bun.sys.getFdPath(.fromNative(handle), buf2)) {
                    .result real => cache.symlink = PathString.init(FileSystem.FilenameStore.instance.append(string, real)?),
                    .err => {},
                }
                return cache;
            }

            const stat = bun.sys.lstat_absolute(absolute_path_c)?;
            const is_symlink = stat.kind == std.fs.File.Kind.sym_link;
            let file_kind = stat.kind;

            let symlink: []const u8 = "";

            if (is_symlink) {
                let file: bun.FD = undefined;
                if (valid := existing_fd.unwrapValid()) {
                    file = valid;
                } else if (store_fd) {
                    file = .fromStdFile(std.fs.openFileAbsoluteZ(absolute_path_c, { mode: .read_only })?);
                } else {
                    file = .fromStdFile(bun.openFileForPath(absolute_path_c)?);
                }
                FileSystem.setMaxFd(file.native());

                defer {
                    if ((!store_fd || RealFS.needToCloseFiles(fs)) && !existing_fd.isValid()) {
                        file.close();
                    } else if (comptime FeatureFlags.store_file_descriptors) {
                        cache.fd = file;
                    }
                }
                const file_stat = file.stdFile().stat()?;
                symlink = file.getFdPath(&outpath)?;
                file_kind = file_stat.kind;
            }

            bun.assert(file_kind != .sym_link);

            if (file_kind == .directory) {
                cache.kind = .dir;
            } else {
                cache.kind = .file;
            }
            if (symlink.len > 0) {
                cache.symlink = PathString.init(FileSystem.FilenameStore.instance.append(string, symlink)?);
            }

            return cache;
        }

        //         // Stores the file entries for directories we've listed before
        // entries_mutex: std.Mutex
        // entries      map[string]entriesOrErr

        // // If true, do not use the "entries" cache
        // doNotCacheEntries bool

}

pub struct FileSystem {
    top_level_dir: [:0]const u8,

    // used on subsequent updates
    top_level_dir_buf: bun.PathBuffer = undefined,

    fs: Implementation,

    dirname_store: *DirnameStore,
    filename_store: *FilenameStore,

    @tls static let tmpdir_handle: std.fs.Dir? = null;

    fn topLevelDirWithoutTrailingSlash(): []const u8 {
        if (_.top_level_dir.len > 1 && _.top_level_dir[_.top_level_dir.len - 1] == std.fs.path.sep) {
            return _.top_level_dir[0 .. _.top_level_dir.len - 1];
        } else {
            return _.top_level_dir;
        }
    }

    static fn tmpdir(fs: *FileSystem): !std.fs.Dir {
        if (tmpdir_handle == null) {
            tmpdir_handle = fs.fs.openTmpDir()?;
        }

        return tmpdir_handle.?;
    }

    type U8Slice = []u8;
    fn getFdPath(fd: FD): ![]const u8 {
        let buf: bun.PathBuffer = undefined;
        const dir = bun.getFdPath(fd, &buf)?;
        return _.dirname_store.append(U8Slice, dir)?;
    }

    static let tmpname_id_number: std.atomic.Value(u32) = std.atomic.Value(u32).init(0);
    static fn tmpname(extname: string, buf: []u8, hash: u64): std.fmt.BufPrintError![:0]u8 {
        const hex_value = @as(u64, @truncate(@as(u128, @intCast(hash)) | @as(u128, @intCast(std.time.nanoTimestamp()))));

        return try std.fmt.bufPrintZ(buf, ".{f}-{f}.{s}", .{
            bun.fmt.hexIntLower(hex_value),
            bun.fmt.hexIntUpper(tmpname_id_number.fetchAdd(1, .monotonic)),
            extname,
        });
    }

    pub static let max_fd: std.posix.fd_t = 0;

    pub inline fn setMaxFd(fd: std.posix.fd_t) {
        if (Environment.isWindows) {
            return;
        }

        if (!FeatureFlags.store_file_descriptors) {
            return;
        }

        max_fd = @max(fd, max_fd);
    }

    pub static let instance_loaded: bool = false;
    pub static let instance: FileSystem = undefined;

    pub const DirnameStore = allocators.BSSStringList(Preallocate.Counts.dir_entry, 128);
    pub const FilenameStore = allocators.BSSStringList(Preallocate.Counts.files, 64);

    pub const Error = error{
        ENOENT,
        EACCESS,
        INVALID_NAME,
        ENOTDIR,
    };

    static fn init(top_level_dir: ([:0]const u8)?): !*FileSystem {
        return initWithForce(top_level_dir, false);
    }

    static fn initWithForce(top_level_dir_: ([:0]const u8)?, comptime force: bool): !*FileSystem {
        const allocator = bun.default_allocator;
        let top_level_dir = top_level_dir_ ?? (if (Environment.isBrowser) "/project/" else try bun.getcwdAlloc(allocator));
        * = &top_level_dir;

        if (!instance_loaded || force) {
            instance = FileSystem{
                top_level_dir: top_level_dir,
                fs: Implementation.init(top_level_dir),
                // must always use default_allocator since the other allocators may not be threadsafe when an element resizes
                dirname_store: DirnameStore.init(bun.default_allocator),
                filename_store: FilenameStore.init(bun.default_allocator),
            };
            instance_loaded = true;

            * = Fs.DirEntry.EntryStore.init(allocator);
        }

        return &FileSystem.instance;
    }

    fn deinit() {
        _.dirname_store.deinit();
        _.filename_store.deinit();
    }


    // pub fn statBatch(fs: *FileSystemEntry, paths: []string) ![]?Stat {

    // }
    // pub fn stat(fs: *FileSystemEntry, path: string) !Stat {

    // }
    // pub fn readFile(fs: *FileSystemEntry, path: string) ?string {

    // }
    // pub fn readDir(fs: *FileSystemEntry, path: string) ?[]string {

    // }
    fn normalize(_: *@This(), str: string): string {
        return @call(bun.callmod_inline, path_handler.normalizeString, .{ str, true, bun.path.Platform.auto });
    }

    fn normalizeBuf(_: *@This(), buf: []u8, str: string): string {
        return @call(bun.callmod_inline, path_handler.normalizeStringBuf, .{ str, buf, false, bun.path.Platform.auto, false });
    }

    fn join(_: *@This(), parts: auto): string {
        return @call(bun.callmod_inline, path_handler.joinStringBuf, .{
            &join_buf,
            parts,
            bun.path.Platform.loose,
        });
    }

    fn joinBuf(_: *@This(), parts: auto, buf: []u8): string {
        return @call(bun.callmod_inline, path_handler.joinStringBuf, .{
            buf,
            parts,
            bun.path.Platform.loose,
        });
    }

    fn relative(_: *@This(), from_path: string, to: string): string {
        return @call(bun.callmod_inline, path_handler.relative, .{
            from_path,
            to,
        });
    }

    fn relativePlatform(_: *@This(), from_path: string, to: string, comptime platform: path_handler.Platform): string {
        return @call(bun.callmod_inline, path_handler.relativePlatform, .{
            from_path,
            to,
            platform,
            false,
        });
    }

    static fn relativeTo(f: *@This(), to: string): string {
        return @call(bun.callmod_inline, path_handler.relative, .{
            f.top_level_dir,
            to,
        });
    }

    static fn relativeFrom(f: *@This(), from_path: string): string {
        return @call(bun.callmod_inline, path_handler.relative, .{
            from_path,
            f.top_level_dir,
        });
    }

    static fn absAlloc(f: *@This(), allocator: std.mem.Allocator, parts: auto): !string {
        const joined = path_handler.joinAbsString(
            f.top_level_dir,
            parts,
            .loose,
        );
        return allocator.dupe(u8, joined)?;
    }

    static fn absAllocZ(f: *@This(), allocator: std.mem.Allocator, parts: auto): ![*:0]const u8 {
        const joined = path_handler.joinAbsString(
            f.top_level_dir,
            parts,
            .loose,
        );
        return allocator.dupeZ(u8, joined)?;
    }

    static fn abs(f: *@This(), parts: auto): string {
        return path_handler.joinAbsString(
            f.top_level_dir,
            parts,
            .loose,
        );
    }

    static fn absBuf(f: *@This(), parts: auto, buf: []u8): string {
        return path_handler.joinAbsStringBuf(f.top_level_dir, buf, parts, .loose);
    }

    /// Like `absBuf`, but returns null when the joined path (after `..`/`.`
    /// normalization) would overflow `buf`. Use when `parts` may contain
    /// user-controlled input of arbitrary length.
    static fn absBufChecked(f: *@This(), parts: []const string, buf: []u8): string? {
        return path_handler.joinAbsStringBufChecked(f.top_level_dir, buf, parts, .loose);
    }

    static fn absBufZ(f: *@This(), parts: auto, buf: []u8): [:0]const u8 {
        return path_handler.joinAbsStringBufZ(f.top_level_dir, buf, parts, .loose);
    }

    static fn joinAlloc(f: *@This(), allocator: std.mem.Allocator, parts: auto): !string {
        const joined = f.join(parts);
        return allocator.dupe(u8, joined)?;
    }

    static fn printLimits() {
        const LIMITS = [std.posix.rlimit_resource.STACK, std.posix.rlimit_resource.NOFILE];
        Output.print("{{\n", {});

        let i: usize = 0;
        for (limit_type of LIMITS) {
            const limit = std.posix.getrlimit(limit_type) catch return;

            if (i == 0) {
                Output.print("  \"stack\": [{d}, {d}],\n", .{ limit.cur, limit.max });
            } else if (i == 1) {
                Output.print("  \"files\": [{d}, {d}]\n", .{ limit.cur, limit.max });
            }
            i += 1;
        }

        Output.print("}}\n", {});
        Output.flush();
    }

    pub const DirEntry = Fs.DirEntry;
    pub const Entry = Fs.Entry;
    pub const RealFS = Fs.RealFS;
    pub const Implementation = Fs.RealFS;
}




pub fn printHandle(handle: auto): std.fmt.Alt(@TypeOf(handle), FmtHandleFnGenerator(@TypeOf(handle)).fmtHandle) {
    return { data: handle };
}
fn FmtHandleFnGenerator(comptime T: type): type {
    return struct {
        fn fmtHandle(handle: T, writer: *std.Io.Writer): std.Io.Writer.Error!void {
            switch (@TypeOf(handle)) {
                i32, c_int => try writer.print("{d}", .{handle}),
                *! => try writer.print("{*}", .{handle}),
                FD => try writer.print("{f}", .{handle}),
                else => {
                    @compileError("unsupported type for fmtHandle: " ++ @typeName(T));
                },
            }
        }
    };
}





const Environment = bun.Environment;
const FD = bun.FD;
const FeatureFlags = bun.FeatureFlags;
const MAX_PATH_BYTES = bun.MAX_PATH_BYTES;
const MutableString = bun.MutableString;
const Mutex = bun.Mutex;
const OOM = bun.OOM;
const Output = bun.Output;
const PathBuffer = bun.PathBuffer;
const PathString = bun.PathString;
const WPathBuffer = bun.WPathBuffer;
const allocators = bun.allocators;
const default_allocator = bun.default_allocator;
const strings = bun.strings;