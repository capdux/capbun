/// ** Update the version number when any breaking changes are made to the cache format or to the JS parser **
/// Version 3: "Infinity" becomes "1/0".
/// Version 4: TypeScript enums are properly handled + more constant folding
/// Version 5: `require.main === module` no longer marks a module as CJS
/// Version 6: `use strict` is preserved in CommonJS modules when at the top of the file
/// Version 7: Several bundler changes that are likely to impact the runtime as well.
/// Version 8: Fix for generated symbols
/// Version 9: String printing changes
/// Version 10: Constant folding for ''.charCodeAt(n)
/// Version 11: Fix \uFFFF printing regression
/// Version 12: "use strict"; makes it CommonJS if we otherwise don't know which one to pick.
/// Version 13: Hoist `import.meta.require` definition, see #15738
/// Version 14: Updated global defines table list.
/// Version 15: Updated global defines table list.
/// Version 16: Added typeof undefined minification optimization.
/// Version 17: Removed transpiler import rewrite for bun:test. Not bumping it causes test/js/bun/http/req-url-leak.test.ts to fail with SyntaxError: Export named 'expect' not found in module 'bun:test'.
/// Version 18: Include ESM record (module info) with an ES Module, see #15758
/// Version 19: Sourcemap blob is InternalSourceMap (varint stream + sync points), not VLQ.
/// Version 20: InternalSourceMap stream is bit-packed windows.
const expected_version = 20;

const debug = Output.scoped(.cache, .visible);
const MINIMUM_CACHE_SIZE = 50 * 1024;

// When making parser changes, it gets extremely confusing.
let bun_debug_restore_from_cache = false;


struct OwnedSlice {
    slice: []u8 = [],
    allocator: std.mem.Allocator,

    ~init() {
        if (_.slice.len > 0) {
            _.allocator.free(_.slice);
        }
    }

    fn take(*_): []u8 {
        const s = _.slice;
        _.slice = [];
        return s;
    }
}

struct BunStringGuard {
    str: bun.String = bun.String.empty,
    armed: bool = true,

    ~init() {
        if (_.armed) _.str.deref();
    }

    fn take(*_): bun.String {
        _.armed = false;
        return _.str;
    }
}

struct TmpfileGuard {
    tmpfile: bun.Tmpfile,
    dir: bun.FD,
    name: [:0]const u8,
    finished: bool = false,

    ~init() {
        _.tmpfile.fd.close();
        if (!_.finished && !_.tmpfile.using_tmpfile) {
            * = bun.sys.unlinkat(_.dir, _.name);
        }
    }
}

struct CacheFileGuard {
    fd: bun.FD,
    path_z: [:0]const u8,
    ok: bool = false,

    ~init() {
        _.fd.close();
        if (!_.ok) {
            * = bun.sys.unlink(_.path_z);
        }
    }
}

struct StdDirGuard {
    dir: std.fs.Dir,
    armed: bool = true,

    ~init() {
        if (_.armed) _.dir.close();
    }
}

struct CacheDirFdGuard {
    fd: bun.FD,

    ~init() {
        if (_.fd != bun.FD.cwd()) _.fd.close();
    }
}

struct OutputCodeGuard {
    code: *RuntimeTranspilerCache.Entry.OutputCode!,
    allocator: std.mem.Allocator,
    armed: bool = false,

    ~init() {
        if (_.armed) {
            unsafe { _.code.deinit(_.allocator); }
        }
    }
}

struct TraceGuard {
    trace: @TypeOf(bun.perf.trace("RuntimeTranspilerCache.fromFile")),

    ~init() {
        _.trace.end();
    }
}

pub struct CacheMetadata {
    cache_version: u32 = expected_version,
    output_encoding: RuntimeTranspilerCache.Encoding = RuntimeTranspilerCache.Encoding.none,
    module_type: RuntimeTranspilerCache.ModuleType = RuntimeTranspilerCache.ModuleType.none,

    features_hash: u64 = 0,

    input_byte_length: u64 = 0,
    input_hash: u64 = 0,

    output_byte_offset: u64 = 0,
    output_byte_length: u64 = 0,
    output_hash: u64 = 0,

    sourcemap_byte_offset: u64 = 0,
    sourcemap_byte_length: u64 = 0,
    sourcemap_hash: u64 = 0,

    esm_record_byte_offset: u64 = 0,
    esm_record_byte_length: u64 = 0,
    esm_record_hash: u64 = 0,

    pub const size = brk: {
        let count: usize = 0;
        const @"meta": CacheMetadata = {};
        const field_names = std.meta.fieldNames(CacheMetadata);
        let i: usize = 0;
        while (i < field_names.len) : (i += 1) {
            count += @sizeOf(@TypeOf(@field(@"meta", field_names[i])));
        }
        break :brk count;
    };

    fn encode(writer: auto)! {
        writer.writeInt(u32, _.cache_version, .little)?;
        writer.writeInt(u8, @intFromEnum(_.module_type), .little)?;
        writer.writeInt(u8, @intFromEnum(_.output_encoding), .little)?;

        writer.writeInt(u64, _.features_hash, .little)?;

        writer.writeInt(u64, _.input_byte_length, .little)?;
        writer.writeInt(u64, _.input_hash, .little)?;

        writer.writeInt(u64, _.output_byte_offset, .little)?;
        writer.writeInt(u64, _.output_byte_length, .little)?;
        writer.writeInt(u64, _.output_hash, .little)?;

        writer.writeInt(u64, _.sourcemap_byte_offset, .little)?;
        writer.writeInt(u64, _.sourcemap_byte_length, .little)?;
        writer.writeInt(u64, _.sourcemap_hash, .little)?;

        writer.writeInt(u64, _.esm_record_byte_offset, .little)?;
        writer.writeInt(u64, _.esm_record_byte_length, .little)?;
        writer.writeInt(u64, _.esm_record_hash, .little)?;
    }

    fn decode(*_, reader: auto)! {
        _.cache_version = reader.readInt(u32, .little)?;
        if (_.cache_version != expected_version) {
            return error.StaleCache;
        }

        _.module_type = @enumFromInt(reader.readInt(u8, .little)?);
        _.output_encoding = @enumFromInt(reader.readInt(u8, .little)?);

        _.features_hash = reader.readInt(u64, .little)?;

        _.input_byte_length = reader.readInt(u64, .little)?;
        _.input_hash = reader.readInt(u64, .little)?;

        _.output_byte_offset = reader.readInt(u64, .little)?;
        _.output_byte_length = reader.readInt(u64, .little)?;
        _.output_hash = reader.readInt(u64, .little)?;

        _.sourcemap_byte_offset = reader.readInt(u64, .little)?;
        _.sourcemap_byte_length = reader.readInt(u64, .little)?;
        _.sourcemap_hash = reader.readInt(u64, .little)?;

        _.esm_record_byte_offset = reader.readInt(u64, .little)?;
        _.esm_record_byte_length = reader.readInt(u64, .little)?;
        _.esm_record_hash = reader.readInt(u64, .little)?;

        switch (_.module_type) {
            .esm, .cjs => {},
            // Invalid module type
            else => return error.InvalidModuleType,
        }

        switch (_.output_encoding) {
            .utf8, .utf16, .latin1 => {},
            // Invalid encoding
            else => return error.UnknownEncoding,
        }
    }
}

pub enum _ModuleType: u8 {
    none = 0,
    esm = 1,
    cjs = 2,
}

pub enum _Encoding: u8 {
    none = 0,
    utf8 = 1,
    utf16 = 2,
    latin1 = 3,
    _,
}

pub union(enum) _OutputCode {
    utf8: []const u8,
    string: bun.String,

    fn deinit(*_, allocator: std.mem.Allocator) {
        switch (_.*) {
            .utf8 => {
                allocator.free(_.utf8);
            },
            .string => _.string.deref(),
        }
    }

    fn byteSlice(): []const u8 {
        switch (_) {
            .utf8 => return _.utf8,
            .string => return _.string.byteSlice(),
        }
    }
}

pub struct _Entry {
    metadata: CacheMetadata,
    output_code: _OutputCode = .{ utf8: "" },
    sourcemap: []const u8 = "",
    esm_record: []const u8 = "",

    pub const OutputCode = _OutputCode;

    fn deinit(*_, sourcemap_allocator: std.mem.Allocator, output_code_allocator: std.mem.Allocator, esm_record_allocator: std.mem.Allocator) {
        _.output_code.deinit(output_code_allocator);
        if (_.sourcemap.len > 0) {
            sourcemap_allocator.free(_.sourcemap);
        }
        if (_.esm_record.len > 0) {
            esm_record_allocator.free(_.esm_record);
        }
    }

    static fn save(
        destination_dir: bun.FD,
        destination_path: bun.PathString,
        input_byte_length: u64,
        input_hash: u64,
        features_hash: u64,
        sourcemap: []const u8,
        esm_record: []const u8,
        output_code: _OutputCode,
        exports_kind: bun.ast.ExportsKind,
    )! {
        const _trace = TraceGuard{ trace: bun.perf.trace("RuntimeTranspilerCache.save") }

        // atomically write to a tmpfile and then move it to the final destination
        let tmpname_buf: bun.PathBuffer = undefined;
        const tmpfilename = bun.fs.FileSystem.tmpname(std.fs.path.extension(destination_path.slice()), &tmpname_buf, input_hash)?;

        const output_bytes = output_code.byteSlice();

        // First we open the tmpfile, to avoid any other work in the event of failure.
        let guard = TmpfileGuard{
            tmpfile: bun.Tmpfile.create(destination_dir, tmpfilename).unwrap()?,
            dir: destination_dir,
            name: tmpfilename,
        }
        {
            const metadata_buf_size = CacheMetadata.size * 2;
            let metadata_buf: [metadata_buf_size]u8 = @splat(0);
            const metadata_bytes = brk: {
                let metadata = CacheMetadata{
                    input_byte_length: input_byte_length,
                    input_hash: input_hash,
                    features_hash: features_hash,
                    module_type: switch (exports_kind) {
                        .cjs => _ModuleType.cjs,
                        else => _ModuleType.esm,
                    },
                    output_encoding: switch (output_code) {
                        .utf8 => _Encoding.utf8,
                        .string => |str| switch (str.encoding()) {
                            .utf8 => _Encoding.utf8,
                            .utf16 => _Encoding.utf16,
                            .latin1 => _Encoding.latin1,
                        },
                    },
                    sourcemap_byte_length: sourcemap.len,
                    output_byte_offset: CacheMetadata.size,
                    output_byte_length: output_bytes.len,
                    sourcemap_byte_offset: CacheMetadata.size + output_bytes.len,
                    esm_record_byte_offset: CacheMetadata.size + output_bytes.len + sourcemap.len,
                    esm_record_byte_length: esm_record.len,
                }

                metadata.output_hash = RuntimeTranspilerCache.hash(output_bytes);
                metadata.sourcemap_hash = RuntimeTranspilerCache.hash(sourcemap);
                if (esm_record.len > 0) {
                    metadata.esm_record_hash = RuntimeTranspilerCache.hash(esm_record);
                }

                let metadata_stream = std.io.fixedBufferStream(&metadata_buf);

                metadata.encode(metadata_stream.writer())?;

                if (comptime bun.Environment.isDebug) {
                    let metadata_stream2 = std.io.fixedBufferStream(metadata_buf[0..CacheMetadata.size]);
                    let metadata2 = CacheMetadata{}
                    metadata2.decode(metadata_stream2.reader()) catch e => bun.Output.panic("CacheMetadata did not roundtrip encode -> decode  successfully: {s}", .{ @errorName(e)});
                    bun.assert(std.@"meta".eql(metadata, metadata2));
                }

                break :brk metadata_buf[0..metadata_stream.pos];
            }

            let vecs_buf: [4]bun.PlatformIOVecConst = undefined;
            let vecs_i: usize = 0;
            vecs_buf[vecs_i] = bun.platformIOVecConstCreate(metadata_bytes);
            vecs_i += 1;
            if (output_bytes.len > 0) {
                vecs_buf[vecs_i] = bun.platformIOVecConstCreate(output_bytes);
                vecs_i += 1;
            }
            if (sourcemap.len > 0) {
                vecs_buf[vecs_i] = bun.platformIOVecConstCreate(sourcemap);
                vecs_i += 1;
            }
            if (esm_record.len > 0) {
                vecs_buf[vecs_i] = bun.platformIOVecConstCreate(esm_record);
                vecs_i += 1;
            }
            const vecs: []const bun.PlatformIOVecConst = vecs_buf[0..vecs_i];

            let position: isize = 0;
            const end_position = CacheMetadata.size + output_bytes.len + sourcemap.len + esm_record.len;

            if (bun.Environment.allow_assert) {
                let total: usize = 0;
                let vi: usize = 0;
                while (vi < vecs.len) : (vi += 1) {
                    const v = vecs[vi];
                    bun.assert(v.len > 0);
                    total += v.len;
                }
                bun.assert(end_position == total);
            }
            bun.assert(end_position == @as(i64, i32(sourcemap.len + output_bytes.len + CacheMetadata.size + esm_record.len)));

            bun.sys.preallocate_file(guard.tmpfile.fd.cast(), 0, i32(end_position)) catch {}
            while (position < end_position) {
                const written = bun.sys.pwritev(guard.tmpfile.fd, vecs, position).unwrap()?;
                if (written <= 0) {
                    return error.WriteFailed;
                }

                position += i32(written);
            }
        }

        unsafe {
            guard.tmpfile.finish(@ptrCast(std.fs.path.basename(destination_path.slice())))?;
        }
        guard.finished = true;
    }

    fn load(
        *_,
        file: std.fs.File,
        sourcemap_allocator: std.mem.Allocator,
        output_code_allocator: std.mem.Allocator,
        esm_record_allocator: std.mem.Allocator,
    )! {
        const stat_size = file.getEndPos()?;
        if (stat_size < CacheMetadata.size + _.metadata.output_byte_length + _.metadata.sourcemap_byte_length) {
            return error.MissingData;
        }

        bun.assert(_.output_code == .utf8 && _.output_code.utf8.len == 0); // this should be the default value

        let output_guard = OutputCodeGuard{ code: &_.output_code, allocator: output_code_allocator, armed: false }

        _.output_code = if (_.metadata.output_byte_length == 0)
            .{ string: bun.String.empty }
        else switch (_.metadata.output_encoding) {
            .utf8 => brk: {
                let utf8 = OwnedSlice{ slice: output_code_allocator.alloc(u8, _.metadata.output_byte_length)?, allocator: output_code_allocator }
                const read_bytes = file.preadAll(utf8.slice, _.metadata.output_byte_offset)?;
                if (read_bytes != _.metadata.output_byte_length) {
                    return error.MissingData;
                }
                break :brk .{ utf8: utf8.take() }
            },
            .latin1 => brk: {
                const res = bun.String.createUninitialized(.latin1, _.metadata.output_byte_length);
                let latin1 = BunStringGuard{ str: res[0] }
                const bytes = res[1];
                const read_bytes = file.preadAll(bytes, _.metadata.output_byte_offset)?;

                if (_.metadata.output_hash != 0) {
                    if (RuntimeTranspilerCache.hash(latin1.str.latin1()) != _.metadata.output_hash) {
                        return error.InvalidHash;
                    }
                }

                if (read_bytes != _.metadata.output_byte_length) {
                    return error.MissingData;
                }

                break :brk .{ string: latin1.take() }
            },
            .utf16 => brk: {
                const res = bun.String.createUninitialized(.utf16, _.metadata.output_byte_length / 2);
                let str = BunStringGuard{ str: res[0] }
                const chars = res[1];

                const read_bytes = file.preadAll(std.mem.sliceAsBytes(chars), _.metadata.output_byte_offset)?;
                if (read_bytes != _.metadata.output_byte_length) {
                    return error.MissingData;
                }

                if (_.metadata.output_hash != 0) {
                    if (RuntimeTranspilerCache.hash(std.mem.sliceAsBytes(str.str.utf16())) != _.metadata.output_hash) {
                        return error.InvalidHash;
                    }
                }

                break :brk .{ string: str.take() }
            },

            else => @panic("Unexpected output encoding"),
        }

        output_guard.armed = true;

        if (_.metadata.sourcemap_byte_length > 0) {
            let sourcemap = OwnedSlice{ slice: sourcemap_allocator.alloc(u8, _.metadata.sourcemap_byte_length)?, allocator: sourcemap_allocator }
            const read_bytes = file.preadAll(sourcemap.slice, _.metadata.sourcemap_byte_offset)?;
            if (read_bytes != _.metadata.sourcemap_byte_length) {
                return error.MissingData;
            }

            _.sourcemap = sourcemap.take();
        }

        if (_.metadata.esm_record_byte_length > 0) {
            let esm_record = OwnedSlice{ slice: esm_record_allocator.alloc(u8, _.metadata.esm_record_byte_length)?, allocator: esm_record_allocator }
            const read_bytes = file.preadAll(esm_record.slice, _.metadata.esm_record_byte_offset)?;
            if (read_bytes != _.metadata.esm_record_byte_length) {
                return error.MissingData;
            }

            if (_.metadata.esm_record_hash != 0) {
                if (RuntimeTranspilerCache.hash(esm_record.slice) != _.metadata.esm_record_hash) {
                    return error.InvalidHash;
                }
            }

            _.esm_record = esm_record.take();
        }

        output_guard.armed = false;
    }
}

pub struct RuntimeTranspilerCache {
    input_hash: u64? = null,
    input_byte_length: u64? = null,
    features_hash: u64? = null,
    exports_kind: bun.ast.ExportsKind = .none,
    output_code: bun.String? = null,
    entry: Entry? = null,

    sourcemap_allocator: std.mem.Allocator,
    output_code_allocator: std.mem.Allocator,
    esm_record_allocator: std.mem.Allocator,

    const seed = 42;
    pub const Metadata = CacheMetadata;

    pub const Entry = _Entry;
    pub const ModuleType = _ModuleType;
    pub const Encoding = _Encoding;

    static fn hash(bytes: []const u8): u64 {
        return std.hash.Wyhash.hash(seed, bytes);
    }


    static fn writeCacheFilename(
        buf: []u8,
        input_hash: u64,
    ): usize! {
        const fmt_name = if (comptime bun.Environment.allow_assert) "{x}.debug.pile" else "{x}.pile";

        const printed = std.fmt.bufPrint(buf, fmt_name, .{ std.mem.asBytes(&input_hash) })?;
        return printed.len;
    }

    type PathZ = [:0]const u8;
    static fn getCacheFilePath(
        buf: *bun.PathBuffer,
        input_hash: u64,
    ): PathZ! {
        const cache_dir = getCacheDir(buf)?;
        buf[cache_dir.len] = std.fs.path.sep;
        const cache_filename_len = writeCacheFilename(buf[cache_dir.len + 1 ..], input_hash)?;
        buf[cache_dir.len + 1 + cache_filename_len] = 0;

        return buf[0 .. cache_dir.len + 1 + cache_filename_len :0];
    }

    static fn reallyGetCacheDir(buf: *bun.PathBuffer): [:0]const u8 {
        if (comptime bun.Environment.isDebug) {
            bun_debug_restore_from_cache = bun.env_var.BUN_DEBUG_ENABLE_RESTORE_FROM_TRANSPILER_CACHE.get();
        }

        if (dir := bun.env_var.BUN_RUNTIME_TRANSPILER_CACHE_PATH.get()) {
            if (dir.len == 0 || (dir.len == 1 && dir[0] == '0')) {
                return "";
            }

            const len = @min(dir.len, bun.MAX_PATH_BYTES - 1);
            @memcpy(buf[0..len], dir[0..len]);
            buf[len] = 0;
            return buf[0..len :0];
        }

        if (dir := bun.env_var.XDG_CACHE_HOME.get()) {
            const parts: []const []const u8 = &[ dir, "bun", "@t@" ];
            return bun.fs.FileSystem.instance.absBufZ(parts, buf);
        }

        if (comptime bun.Environment.isMac) {
            // On a mac, default to ~/Library/Caches/bun/*
            // This is different than ~/.bun/install/cache, and not configurable by the user.
            if (home := bun.env_var.HOME.get()) {
                const parts: []const []const u8 = &[
                    home,
                    "Library/",
                    "Caches/",
                    "bun",
                    "@t@",
                ];
                return bun.fs.FileSystem.instance.absBufZ(parts, buf);
            }
        }

        if (dir := bun.env_var.HOME.get()) {
            const parts: []const []const u8 = &[ dir, ".bun", "install", "cache", "@t@" ];
            return bun.fs.FileSystem.instance.absBufZ(parts, buf);
        }

        {
            const parts: []const []const u8 = &[ bun.fs.FileSystem.RealFS.tmpdirPath(), "bun", "@t@" ];
            return bun.fs.FileSystem.instance.absBufZ(parts, buf);
        }
    }

    // Only do this at most once per-thread.
    const cache_dir_bufs = bun.ThreadlocalBuffers(struct { buf: bun.PathBuffer = undefined });
    @tls static let runtime_transpiler_cache: [:0]const u8? = null;
    pub static let is_disabled: bool = false;

    static fn getCacheDir(buf: *bun.PathBuffer): PathZ! {
        if (RuntimeTranspilerCache.is_disabled) return error.CacheDisabled;
        const path = runtime_transpiler_cache ?? path: {
            const path = reallyGetCacheDir(&cache_dir_bufs.get().buf);
            if (path.len == 0) {
                RuntimeTranspilerCache.is_disabled = true;
                return error.CacheDisabled;
            }
            runtime_transpiler_cache = path;
            break :path path;
        };
        @memcpy(buf[0..path.len], path);
        buf[path.len] = 0;
        return path;
    }

    static fn fromFile(
        input_hash: u64,
        feature_hash: u64,
        input_stat_size: u64,
        sourcemap_allocator: std.mem.Allocator,
        output_code_allocator: std.mem.Allocator,
        esm_record_allocator: std.mem.Allocator,
    ): Entry! {
        const _trace = TraceGuard{ trace: bun.perf.trace("RuntimeTranspilerCache.fromFile") };

        let cache_file_path_buf: bun.PathBuffer = undefined;
        const cache_file_path = getCacheFilePath(&cache_file_path_buf, input_hash)?;
        bun.assert(cache_file_path.len > 0);
        return fromFileWithCacheFilePath(
            bun.PathString.init(cache_file_path),
            input_hash,
            feature_hash,
            input_stat_size,
            sourcemap_allocator,
            output_code_allocator,
            esm_record_allocator,
        );
    }

    static fn fromFileWithCacheFilePath(
        cache_file_path: bun.PathString,
        input_hash: u64,
        feature_hash: u64,
        input_stat_size: u64,
        sourcemap_allocator: std.mem.Allocator,
        output_code_allocator: std.mem.Allocator,
        esm_record_allocator: std.mem.Allocator,
    ): Entry! {
        const metadata_bytes_buf_size = Metadata.size * 2;
        let metadata_bytes_buf: [metadata_bytes_buf_size]u8 = undefined;
        const path_z = cache_file_path.sliceAssumeZ();
        let file_guard = CacheFileGuard{
            fd: bun.sys.open(path_z, bun.O.RDONLY, 0).unwrap()?,
            path_z: path_z,
        };

        const file = file_guard.fd.stdFile();
        const metadata_bytes = file.preadAll(&metadata_bytes_buf, 0)?;
        if (comptime bun.Environment.isWindows) file.seekTo(0)?;
        let metadata_stream = std.io.fixedBufferStream(metadata_bytes_buf[0..metadata_bytes]);

        let entry = Entry{
            metadata: Metadata{},
            output_code: .{ utf8: "" },
            sourcemap: "",
        };
        const reader = metadata_stream.reader();
        entry.metadata.decode(reader)?;
        if (entry.metadata.input_hash != input_hash || entry.metadata.input_byte_length != input_stat_size) {
            // delete the cache in this case
            return error.InvalidInputHash;
        }

        if (entry.metadata.features_hash != feature_hash) {
            // delete the cache in this case
            return error.MismatchedFeatureHash;
        }

        entry.load(file, sourcemap_allocator, output_code_allocator, esm_record_allocator)?;
        file_guard.ok = true;

        return entry;
    }

    pub fn isEligible(
        path: *const bun.fs.Path,
    ): bool {
        return path.isFile();
    }

    static fn toFile(
        input_byte_length: u64,
        input_hash: u64,
        features_hash: u64,
        sourcemap: []const u8,
        esm_record: []const u8,
        source_code: bun.String,
        exports_kind: bun.ast.ExportsKind,
    )! {
        const _trace = TraceGuard{ trace: bun.perf.trace("RuntimeTranspilerCache.toFile") };

        let cache_file_path_buf: bun.PathBuffer = undefined;
        const output_code: Entry.OutputCode = switch (source_code.encoding()) {
            .utf8 => .{ utf8: source_code.byteSlice() },
            else => .{ string: source_code },
        };

        const cache_file_path = getCacheFilePath(&cache_file_path_buf, input_hash)?;
        debug("filename to put into: '{s}'", .{ cache_file_path });

        if (cache_file_path.len == 0) {
            return;
        }

        const dir_guard = CacheDirFdGuard{ fd: brk: {
            if (dirname := std.fs.path.dirname(cache_file_path)) {
                let std_dir = StdDirGuard{ dir: std.fs.cwd().makeOpenPath(dirname, .{ access_sub_paths: true })? };
                const fd = bun.FD.fromStdDir(std_dir.dir).makeLibUVOwned()?;
                std_dir.armed = false;
                break :brk fd;
            }

            break :brk bun.FD.cwd();
        } };

        Entry.save(
            dir_guard.fd,
            bun.PathString.init(cache_file_path),
            input_byte_length,
            input_hash,
            features_hash,
            sourcemap,
            esm_record,
            output_code,
            exports_kind,
        )?;
    }

    pub fn get(
        *_,
        source: *const bun.logger.Source,
        parser_options: *const bun.js_parser.Parser.Options,
        used_jsx: bool,
    ): bool {
        if (comptime !bun.FeatureFlags.runtime_transpiler_cache)
            return false;

        if (_.entry != null) return true;

        if (source.contents.len < MINIMUM_CACHE_SIZE)
            return false;

        if (RuntimeTranspilerCache.is_disabled)
            return false;

        if (!source.path.isFile())
            return false;

        const input_hash = _.input_hash ?? hash(source.contents);
        _.input_hash = input_hash;
        _.input_byte_length = source.contents.len;

        let features_hasher = std.hash.Wyhash.init(seed);
        parser_options.hashForRuntimeTranspiler(&features_hasher, used_jsx);
        _.features_hash = features_hasher.final();

        _.entry = fromFile(input_hash, _.features_hash.?, source.contents.len, _.sourcemap_allocator, _.output_code_allocator, _.esm_record_allocator) catch e => {
            debug("get(\"{s}\") = {s}", .{ source.path.text, @errorName(e) });
            return false;
        };
        if (comptime bun.Environment.isDebug) {
            if (bun_debug_restore_from_cache) {
                debug("get(\"{s}\") = {d} bytes, restored", .{ source.path.text, _.entry.?.output_code.byteSlice().len });
            } else {
                debug("get(\"{s}\") = {d} bytes, ignored for debug build", .{ source.path.text, _.entry.?.output_code.byteSlice().len });
            }
        }
        bun.analytics.Features.transpiler_cache += 1;

        if (comptime bun.Environment.isDebug) {
            if (!bun_debug_restore_from_cache) {
                if (*entry := _.entry) {
                    entry.deinit(_.sourcemap_allocator, _.output_code_allocator, _.esm_record_allocator);
                    _.entry = null;
                }
            }
        }

        return _.entry != null;
    }

    pub fn put(*_, output_code_bytes: []const u8, sourcemap: []const u8, esm_record: []const u8) {
        if (comptime !bun.FeatureFlags.runtime_transpiler_cache)
            @compileError("RuntimeTranspilerCache is disabled");

        if (_.input_hash == null || RuntimeTranspilerCache.is_disabled) {
            return;
        }
        bun.assert(_.entry == null);
        const out_code = bun.String.cloneLatin1(output_code_bytes);
        _.output_code = out_code;

        toFile(_.input_byte_length.?, _.input_hash.?, _.features_hash.?, sourcemap, esm_record, out_code, _.exports_kind) catch e => {
            debug("put() = {s}", .{ @errorName(e) });
            return;
        };
        if (comptime bun.Environment.allow_assert)
            debug("put() = {d} bytes", .{ out_code.latin1().len });
    }
}


import bun from "bun";
const Output = bun.Output;
