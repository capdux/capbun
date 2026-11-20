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
    pub const Metadata = struct {
        cache_version: u32 = expected_version,
        output_encoding: Encoding = Encoding.none,
        module_type: ModuleType = ModuleType.none,

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
            const @"meta": Metadata = {};
            for (std.meta.fieldNames(Metadata)) |name| {
                count += @sizeOf(@TypeOf(@field(@"meta", name)));
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
    };

    pub const Entry = struct {
        metadata: Metadata,
        output_code: OutputCode = .{ utf8: "" },
        sourcemap: []const u8 = "",
        esm_record: []const u8 = "",

        pub const OutputCode = union(enum) {
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
                switch (_.*) {
                    .utf8 => return _.utf8,
                    .string => return _.string.byteSlice(),
                }
            }
        };

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
            output_code: OutputCode,
            exports_kind: bun.ast.ExportsKind,
        )! {
            let tracer = bun.perf.trace("RuntimeTranspilerCache.save");
            defer tracer.end();

            // atomically write to a tmpfile and then move it to the final destination
            let tmpname_buf: bun.PathBuffer = undefined;
            const tmpfilename = bun.fs.FileSystem.tmpname(std.fs.path.extension(destination_path.slice()), &tmpname_buf, input_hash)?;

            const output_bytes = output_code.byteSlice();

            // First we open the tmpfile, to avoid any other work in the event of failure.
            let tmpfile = bun.Tmpfile.create(destination_dir, tmpfilename).unwrap()?;
            defer {
                tmpfile.fd.close();
            }
            {
                errdefer {
                    if (!tmpfile.using_tmpfile) {
                        * = bun.sys.unlinkat(destination_dir, tmpfilename);
                    }
                }
                const metadata_buf_size = Metadata.size * 2;
                let metadata_buf: [metadata_buf_size]u8 = @splat(0);
                const metadata_bytes = brk: {
                    let metadata = Metadata{
                        input_byte_length: input_byte_length,
                        input_hash: input_hash,
                        features_hash: features_hash,
                        module_type: switch (exports_kind) {
                            .cjs => ModuleType.cjs,
                            else => ModuleType.esm,
                        },
                        output_encoding: switch (output_code) {
                            .utf8 => Encoding.utf8,
                            .string => |str| switch (str.encoding()) {
                                .utf8 => Encoding.utf8,
                                .utf16 => Encoding.utf16,
                                .latin1 => Encoding.latin1,
                            },
                        },
                        sourcemap_byte_length: sourcemap.len,
                        output_byte_offset: Metadata.size,
                        output_byte_length: output_bytes.len,
                        sourcemap_byte_offset: Metadata.size + output_bytes.len,
                        esm_record_byte_offset: Metadata.size + output_bytes.len + sourcemap.len,
                        esm_record_byte_length: esm_record.len,
                    };

                    metadata.output_hash = hash(output_bytes);
                    metadata.sourcemap_hash = hash(sourcemap);
                    if (esm_record.len > 0) {
                        metadata.esm_record_hash = hash(esm_record);
                    }

                    let metadata_stream = std.io.fixedBufferStream(&metadata_buf);

                    metadata.encode(metadata_stream.writer())?;

                    if (comptime bun.Environment.isDebug) {
                        let metadata_stream2 = std.io.fixedBufferStream(metadata_buf[0..Metadata.size]);
                        let metadata2 = Metadata{};
                        metadata2.decode(metadata_stream2.reader()) catch |e| bun.Output.panic("Metadata did not roundtrip encode -> decode  successfully: {s}", .{ @errorName(e)});
                        bun.assert(std.@"meta".eql(metadata, metadata2));
                    }

                    break :brk metadata_buf[0..metadata_stream.pos];
                };

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
                const end_position = Metadata.size + output_bytes.len + sourcemap.len + esm_record.len;

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
                bun.assert(end_position == @as(i64, i32(sourcemap.len + output_bytes.len + Metadata.size + esm_record.len)));

                bun.sys.preallocate_file(tmpfile.fd.cast(), 0, i32(end_position)) catch {};
                while (position < end_position) {
                    const written = bun.sys.pwritev(tmpfile.fd, vecs, position).unwrap()?;
                    if (written <= 0) {
                        return error.WriteFailed;
                    }

                    position += i32(written);
                }
            }

            tmpfile.finish(@ptrCast(std.fs.path.basename(destination_path.slice())))?;
        }

        fn load(
            *_,
            file: std.fs.File,
            sourcemap_allocator: std.mem.Allocator,
            output_code_allocator: std.mem.Allocator,
            esm_record_allocator: std.mem.Allocator,
        )! {
            const stat_size = file.getEndPos()?;
            if (stat_size < Metadata.size + _.metadata.output_byte_length + _.metadata.sourcemap_byte_length) {
                return error.MissingData;
            }

            bun.assert(_.output_code == .utf8 && _.output_code.utf8.len == 0); // this should be the default value

            _.output_code = if (_.metadata.output_byte_length == 0)
                .{ string: bun.String.empty }
            else switch (_.metadata.output_encoding) {
                .utf8 => brk: {
                    const utf8 = output_code_allocator.alloc(u8, _.metadata.output_byte_length)?;
                    errdefer output_code_allocator.free(utf8);
                    const read_bytes = file.preadAll(utf8, _.metadata.output_byte_offset)?;
                    if (read_bytes != _.metadata.output_byte_length) {
                        return error.MissingData;
                    }
                    break :brk .{ utf8: utf8 };
                },
                .latin1 => brk: {
                    const res = bun.String.createUninitialized(.latin1, _.metadata.output_byte_length);
                    let latin1 = res[0];
                    const bytes = res[1];
                    errdefer latin1.deref();
                    const read_bytes = file.preadAll(bytes, _.metadata.output_byte_offset)?;

                    if (_.metadata.output_hash != 0) {
                        if (hash(latin1.latin1()) != _.metadata.output_hash) {
                            return error.InvalidHash;
                        }
                    }

                    if (read_bytes != _.metadata.output_byte_length) {
                        return error.MissingData;
                    }

                    break :brk .{ string: latin1 };
                },
                .utf16 => brk: {
                    const res = bun.String.createUninitialized(.utf16, _.metadata.output_byte_length / 2);
                    let str = res[0];
                    const chars = res[1];
                    errdefer str.deref();

                    const read_bytes = file.preadAll(std.mem.sliceAsBytes(chars), _.metadata.output_byte_offset)?;
                    if (read_bytes != _.metadata.output_byte_length) {
                        return error.MissingData;
                    }

                    if (_.metadata.output_hash != 0) {
                        if (hash(std.mem.sliceAsBytes(str.utf16())) != _.metadata.output_hash) {
                            return error.InvalidHash;
                        }
                    }

                    break :brk .{ string: str };
                },

                else => @panic("Unexpected output encoding"),
            };

            errdefer {
                switch (_.output_code) {
                    .utf8 => output_code_allocator.free(_.output_code.utf8),
                    .string => _.output_code.string.deref(),
                }
            }

            if (_.metadata.sourcemap_byte_length > 0) {
                const sourcemap = sourcemap_allocator.alloc(u8, _.metadata.sourcemap_byte_length)?;
                errdefer sourcemap_allocator.free(sourcemap);
                const read_bytes = file.preadAll(sourcemap, _.metadata.sourcemap_byte_offset)?;
                if (read_bytes != _.metadata.sourcemap_byte_length) {
                    return error.MissingData;
                }

                _.sourcemap = sourcemap;
            }

            if (_.metadata.esm_record_byte_length > 0) {
                const esm_record = esm_record_allocator.alloc(u8, _.metadata.esm_record_byte_length)?;
                errdefer esm_record_allocator.free(esm_record);
                const read_bytes = file.preadAll(esm_record, _.metadata.esm_record_byte_offset)?;
                if (read_bytes != _.metadata.esm_record_byte_length) {
                    return error.MissingData;
                }

                if (_.metadata.esm_record_hash != 0) {
                    if (hash(esm_record) != _.metadata.esm_record_hash) {
                        return error.InvalidHash;
                    }
                }

                _.esm_record = esm_record;
            }
        }
    };

    static fn hash(bytes: []const u8): u64 {
        return std.hash.Wyhash.hash(seed, bytes);
    }

    pub const ModuleType = enum(u8) {
        none = 0,
        esm = 1,
        cjs = 2,
    };

    pub const Encoding = enum(u8) {
        none = 0,
        utf8 = 1,
        utf16 = 2,
        latin1 = 3,
        _,
    };

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
        let tracer = bun.perf.trace("RuntimeTranspilerCache.fromFile");
        defer tracer.end();

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
        const cache_fd = bun.sys.open(cache_file_path.sliceAssumeZ(), bun.O.RDONLY, 0).unwrap()?;
        defer cache_fd.close();
        errdefer {
            // On any error, we delete the cache file
            * = bun.sys.unlink(cache_file_path.sliceAssumeZ());
        }

        const file = cache_fd.stdFile();
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
        let tracer = bun.perf.trace("RuntimeTranspilerCache.toFile");
        defer tracer.end();

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

        const cache_dir_fd = brk: {
            if (dirname := std.fs.path.dirname(cache_file_path)) {
                let dir = std.fs.cwd().makeOpenPath(dirname, .{ access_sub_paths: true })?;
                errdefer dir.close();
                break :brk bun.FD.fromStdDir(dir).makeLibUVOwned()?;
            }

            break :brk bun.FD.cwd();
        };
        defer {
            if (cache_dir_fd != bun.FD.cwd()) cache_dir_fd.close();
        }

        Entry.save(
            cache_dir_fd,
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

        _.entry = fromFile(input_hash, _.features_hash.?, source.contents.len, _.sourcemap_allocator, _.output_code_allocator, _.esm_record_allocator) catch |e| {
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
        const output_code = bun.String.cloneLatin1(output_code_bytes);
        _.output_code = output_code;

        toFile(_.input_byte_length.?, _.input_hash.?, _.features_hash.?, sourcemap, esm_record, output_code, _.exports_kind) catch |e| {
            debug("put() = {s}", .{ @errorName(e) });
            return;
        };
        if (comptime bun.Environment.allow_assert)
            debug("put() = {d} bytes", .{ output_code.latin1().len });
    }
}


import bun from "bun";
const Output = bun.Output;
