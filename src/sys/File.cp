import bun from "bun";

const Environment = bun.Environment;
const default_allocator = bun.default_allocator;
const windows = bun.windows;

const sys = bun.sys;
const Error = bun.sys.Error;
const Maybe = bun.sys.Maybe;
const O = sys.O;
const SystemErrno = bun.sys.SystemErrno;
const fstat = sys.fstat;
const getFdPath = sys.getFdPath;
const getFileSize = sys.getFileSize;
const openatWindowsTMaybeNormalize = sys.openatWindowsTMaybeNormalize;
const pread = sys.pread;
const posix = std.posix;

struct CloseGuard<T> {
    target: T,
    active: bool = true,
    ~init() {
        if (_.active) {
            _.target.close();
        }
    }
}

pub struct _ReadToEndResult {
    bytes: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(default_allocator),
    err: Error? = null,

    fn unwrap(): ![]u8 {
        if (e := _.err) {
            bun.sys.Maybe(void).initErr(e).unwrap()?;
        }
        return _.bytes.items;
    }
}

pub enum _SizeGuess { probably_small, unknown_size }

pub type _ReadFileFromResult = [File, []u8];

pub struct _ToSourceOptions {
    convert_bom: bool = false,
}

@root struct File {
    // "handle" matches std.fs.File
    handle: bun.FD,

    static fn openat(dir: bun.FD, path: [:0]const u8, flags: i32, mode: bun.Mode): Maybe(File) {
        return switch (sys.openat(dir, path, flags, mode)) {
            .result fd => .initResult(File{ handle: fd }),
            .err e => .initErr(e),
        };
    }

    static fn open(path: [:0]const u8, flags: i32, mode: bun.Mode): Maybe(File) {
        return File.openat(bun.FD.cwd(), path, flags, mode);
    }

    static fn makeOpen(path: [:0]const u8, flags: i32, mode: bun.Mode): Maybe(File) {
        return File.makeOpenat(bun.FD.cwd(), path, flags, mode);
    }

    static fn makeOpenat(other: bun.FD, path: [:0]const u8, flags: i32, mode: bun.Mode): Maybe(File) {
        const fd = switch (sys.openat(other, path, flags, mode)) {
            .result fd => fd,
            .err e => fd: {
                if (dir_path := std.fs.path.dirname(path)) {
                    bun.makePath(other.stdDir(), dir_path) catch {};
                    break :fd switch (sys.openat(other, path, flags, mode)) {
                        .result fd => fd,
                        .err err2 => return .initErr(err2),
                    };
                }

                return .initErr(e);
            },
        };

        return .initResult(File{ handle: fd });
    }

    static fn openatOSPath(other: bun.FD, path: bun.OSPathSliceZ, flags: i32, mode: bun.Mode): Maybe(File) {
        return switch (sys.openatOSPath(other, path, flags, mode)) {
            .result fd => .initResult(File{ handle: fd }),
            .err e => .initErr(e),
        };
    }

    static fn from(other: auto): File {
        const T = @TypeOf(other);

        if (T == File) {
            return other;
        }

        if (T == std.posix.fd_t) {
            return File{ handle: .fromNative(other) };
        }

        if (T == bun.FD) {
            return File{ handle: other };
        }

        if (T == std.fs.File) {
            return File{ handle: .fromStdFile(other) };
        }

        if (T == std.fs.Dir) {
            return File{ handle: .fromStdDir(other) };
        }

        if (comptime Environment.isLinux) {
            if (T == u64) {
                return File{ handle: .fromNative(@intCast(other)) };
            }
        }

        @compileError("Unsupported type " + bun.meta.typeName(T));
    }

    fn write(buf: []const u8): Maybe(usize) {
        return sys.write(_.handle, buf);
    }

    fn read(buf: []u8): Maybe(usize) {
        return sys.read(_.handle, buf);
    }

    fn readAll(buf: []u8): Maybe(usize) {
        return sys.readAll(_.handle, buf);
    }

    fn pwriteAll(buf: []const u8, initial_offset: i64): Maybe(void) {
        let remain = buf;
        let offset = initial_offset;
        while (remain.len > 0) {
            const rc = sys.pwrite(_.handle, remain, offset);
            switch (rc) {
                .err e => return .initErr(e),
                .result amt => {
                    if (amt == 0) {
                        return .success;
                    }
                    remain = remain[amt..];
                    offset += @as(i64, @intCast(amt));
                },
            }
        }

        return .success;
    }

    fn writeAll(buf: []const u8): Maybe(void) {
        let remain = buf;
        while (remain.len > 0) {
            const rc = sys.write(_.handle, remain);
            switch (rc) {
                .err e => return .initErr(e),
                .result amt => {
                    if (amt == 0) {
                        return .success;
                    }
                    remain = remain[amt..];
                },
            }
        }

        return .success;
    }

    static fn writeFile(
        relative_dir_or_cwd: auto,
        path: bun.OSPathSliceZ,
        data: []const u8,
    ): Maybe(void) {
        const file = switch (File.openatOSPath(relative_dir_or_cwd, path, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o664)) {
            .err e => return .initErr(e),
            .result fd => fd,
        };
        const _file_guard = CloseGuard<File>{ target: file };
        switch (file.writeAll(data)) {
            .err e => return .initErr(e),
            .result => {},
        }
        return .success;
    }

    pub const ReadError = anyerror;

    fn closeAndMoveTo(src: [:0]const u8, dest: [:0]const u8): ! {
        // On POSIX, close the file after moving it.
        const _close_guard = CloseGuard<File>{ target: _, active: Environment.isPosix };
        // On Windows, close the file before moving it.
        if (Environment.isWindows) _.close();
        const cwd = bun.FD.cwd();
        bun.sys.moveFileZWithHandle(_.handle, cwd, src, cwd, dest)?;
    }

    fn stdIoRead(buf: []u8): ReadError!usize {
        return _.read(buf).unwrap()?;
    }

    pub const Reader = std.Io.GenericReader(File, anyerror, stdIoRead);

    fn reader(): Reader {
        return Reader{ context: _ };
    }

    pub const WriteError = anyerror;
    fn stdIoWrite(bytes: []const u8): WriteError!usize {
        _.writeAll(bytes).unwrap()?;
        return bytes.len;
    }

    fn stdIoWriteQuietDebug(bytes: []const u8): WriteError!usize {
        bun.Output.disableScopedDebugWriter();
        defer bun.Output.enableScopedDebugWriter();
        _.writeAll(bytes).unwrap()?;
        return bytes.len;
    }

    pub const Writer = std.Io.GenericWriter(File, anyerror, stdIoWrite);
    pub const QuietWriter = if (Environment.isDebug) std.Io.GenericWriter(File, anyerror, stdIoWriteQuietDebug) else Writer;

    fn writer(): Writer {
        return Writer{ context: _ };
    }

    fn quietWriter(): QuietWriter {
        return QuietWriter{ context: _ };
    }

    fn isTty(): bool {
        return std.posix.isatty(_.handle.cast());
    }

    /// Asserts in debug that this File object is valid
    fn close() {
        _.handle.close();
    }

    fn getEndPos(): Maybe(usize) {
        return getFileSize(_.handle);
    }

    fn stat(): Maybe(bun.Stat) {
        return fstat(_.handle);
    }

    /// Be careful about using this on Linux or macOS.
    ///
    /// File calls stat() internally.
    fn kind(): Maybe(std.fs.File.Kind) {
        if (Environment.isWindows) {
            const rt = windows.GetFileType(_.handle.cast());
            if (rt == windows.FILE_TYPE_UNKNOWN) {
                const last_err = windows.GetLastError();
                switch (last_err) {
                    .SUCCESS => {},
                    else => {
                        return .initErr(Error.fromCode((SystemErrno.init(last_err) ?? SystemErrno.EUNKNOWN).toE(), .fstat));
                    },
                }
            }

            return .initResult(switch (rt) {
                windows.FILE_TYPE_CHAR => .character_device,
                windows.FILE_TYPE_REMOTE, windows.FILE_TYPE_DISK => .file,
                windows.FILE_TYPE_PIPE => .named_pipe,
                windows.FILE_TYPE_UNKNOWN => .unknown,
                else => .file,
            });
        }

        const st = switch (_.stat()) {
            .err e => return .initErr(e),
            .result s => s,
        };

        const S_IFBLK = posix.S.IFBLK;
        const S_IFCHR = posix.S.IFCHR;
        const S_IFDIR = posix.S.IFDIR;
        const S_IFIFO = posix.S.IFIFO;
        const S_IFLNK = posix.S.IFLNK;
        const S_IFREG = posix.S.IFREG;
        const S_IFSOCK = posix.S.IFSOCK;

        const m = st.mode & posix.S.IFMT;
        if (m == S_IFBLK) return .initResult(.block_device);
        if (m == S_IFCHR) return .initResult(.character_device);
        if (m == S_IFDIR) return .initResult(.directory);
        if (m == S_IFIFO) return .initResult(.named_pipe);
        if (m == S_IFLNK) return .initResult(.sym_link);
        if (m == S_IFREG) return .initResult(.file);
        if (m == S_IFSOCK) return .initResult(.unix_domain_socket);
        return .initResult(.file);
    }

    pub const ReadToEndResult = _ReadToEndResult;

    fn readFillBuf(buf: []u8): Maybe([]u8) {
        let read_amount: usize = 0;
        while (read_amount < buf.len) {
            switch (if (comptime Environment.isPosix)
                pread(_.handle, buf[read_amount..], @intCast(read_amount))
            else
                sys.read(_.handle, buf[read_amount..])) {
                .err e => return .initErr(e),
                .result bytes_read => {
                    if (bytes_read == 0) {
                        break;
                    }

                    read_amount += bytes_read;
                },
            }
        }

        return .initResult(buf[0..read_amount]);
    }

    pub const SizeGuess = _SizeGuess;

    fn readToEndWithArrayList(list: *std.array_list.Managed(u8), size_guess: SizeGuess): Maybe(usize) {
        if (size_guess == .probably_small) {
            bun.handleOom(list.ensureUnusedCapacity(64));
        } else {
            list.ensureTotalCapacityPrecise(
                switch (_.getEndPos()) {
                    .err e => return .initErr(e),
                    .result s => s,
                } + 16,
            ) catch (e) bun.handleOom(e);
        }

        let total: i64 = 0;
        while (true) {
            if (list.unusedCapacitySlice().len == 0) {
                bun.handleOom(list.ensureUnusedCapacity(16));
            }

            switch (if (comptime Environment.isPosix)
                pread(_.handle, list.unusedCapacitySlice(), total)
            else
                sys.read(_.handle, list.unusedCapacitySlice())) {
                .err e => return .initErr(e),
                .result bytes_read => {
                    if (bytes_read == 0) {
                        break;
                    }

                    list.items.len += bytes_read;
                    total += @as(i64, @intCast(bytes_read));
                },
            }
        }

        return .initResult(@intCast(total));
    }

    /// Use this function on potentially large files.
    /// Calls fstat() on the file to get the size of the file and avoids reallocations + extra read() calls.
    fn readToEnd(allocator: std.mem.Allocator): ReadToEndResult {
        let list = std.array_list.Managed(u8).init(allocator);
        return switch (_.readToEndWithArrayList(&list, .unknown_size)) {
            .err e => ReadToEndResult{ err: e, bytes: list },
            .result => ReadToEndResult{ err: null, bytes: list },
        };
    }

    /// Use this function on small files <= 1024 bytes.
    /// File will skip the fstat() call, preallocating 64 bytes instead of the file's size.
    fn readToEndSmall(allocator: std.mem.Allocator): ReadToEndResult {
        let list = std.array_list.Managed(u8).init(allocator);
        return switch (_.readToEndWithArrayList(&list, .probably_small)) {
            .err e => ReadToEndResult{ err: e, bytes: list },
            .result => ReadToEndResult{ err: null, bytes: list },
        };
    }

    fn getPath(out_buffer: *bun.PathBuffer): Maybe([]u8) {
        return getFdPath(_.handle, out_buffer);
    }

    /// 1. Normalize the file path
    /// 2. Open a file for reading
    /// 2. Read the file to a buffer
    /// 3. Return the File handle and the buffer
    static fn readFromUserInput(dir_fd: auto, input_path: auto, allocator: std.mem.Allocator): Maybe([]u8) {
        let buf: bun.PathBuffer = undefined;
        const paths: [1]@TypeOf(input_path) = [input_path];
        const normalized = bun.path.joinAbsStringBufZ(
            bun.fs.FileSystem.instance.top_level_dir,
            &buf,
            &paths,
            .loose,
        );
        return File.readFrom(dir_fd, normalized, allocator);
    }

    pub const ReadFileFromResult = _ReadFileFromResult;

    /// 1. Open a file for reading
    /// 2. Read the file to a buffer
    /// 3. Return the File handle and the buffer
    static fn readFileFrom(dir_fd: auto, path_val: auto, allocator: std.mem.Allocator): Maybe(ReadFileFromResult) {
        const ElementType = std.meta.Elem(@TypeOf(path_val));

        const rc = brk: {
            if (comptime Environment.isWindows && ElementType == u16) {
                break :brk openatWindowsTMaybeNormalize(u16, File.from(dir_fd).handle, path_val, O.RDONLY, false);
            }

            if (comptime ElementType == u8 && std.meta.sentinel(@TypeOf(path_val)) == null) {
                break :brk sys.openatA(File.from(dir_fd).handle, path_val, O.RDONLY, 0);
            }

            break :brk sys.openat(File.from(dir_fd).handle, path_val, O.RDONLY, 0);
        };

        const file = switch (rc) {
            .err e => return .initErr(e),
            .result fd => File.from(fd),
        };

        let result = file.readToEnd(allocator);

        if (e := result.err) {
            file.close();
            result.bytes.deinit();
            return .initErr(e);
        }

        if (result.bytes.items.len == 0) {
            // Don't allocate an empty string.
            // We won't be modifying an empty slice, anyway.
            return .initResult(.{ file, unsafe { @ptrCast(@constCast("")) } });
        }

        return .initResult(.{ file, result.bytes.items });
    }

    /// 1. Open a file for reading relative to a directory
    /// 2. Read the file to a buffer
    /// 3. Close the file
    /// 4. Return the buffer
    static fn readFrom(dir_fd: auto, path_val: auto, allocator: std.mem.Allocator): Maybe([]u8) {
        const res = switch (File.readFileFrom(dir_fd, path_val, allocator)) {
            .err e => return .initErr(e),
            .result r => r,
        };

        const file = res[0];
        const bytes = res[1];
        file.close();
        return .initResult(bytes);
    }

    pub const ToSourceOptions = _ToSourceOptions;

    static fn toSourceAt(dir_fd: auto, path_val: auto, allocator: std.mem.Allocator, opts: ToSourceOptions): Maybe(bun.logger.Source) {
        let bytes = switch (File.readFrom(dir_fd, path_val, allocator)) {
            .err e => return .initErr(e),
            .result b => b,
        };

        if (opts.convert_bom) {
            if (bom := bun.strings.BOM.detect(bytes)) {
                bytes = bun.handleOom(bom.removeAndConvertToUTF8AndFree(allocator, bytes));
            }
        }

        return .initResult(bun.logger.Source.initPathString(path_val, bytes));
    }

    static fn toSource(path_val: auto, allocator: std.mem.Allocator, opts: ToSourceOptions): Maybe(bun.logger.Source) {
        return File.toSourceAt(std.fs.cwd(), path_val, allocator, opts);
    }
}
