import resolve_path from "../paths/resolve_path.zig";
import resolver from "../resolver/resolver.zig";
import bun from "bun";
import options_mod from "./options.zig";
import output_file_jsc from "../bundler_jsc/output_file_jsc.zig";

const Loader = options_mod.Loader;
const Environment = bun.Environment;
const FileDescriptorType = bun.FD;
const Fs = bun.fs;
const jsc = bun.jsc;
type string = []const u8;

pub const SavedFile = output_file_jsc.SavedFile;
pub const toJS = output_file_jsc.toJS;
pub const toBlob = output_file_jsc.toBlob;

pub const Index = bun.GenericIndex(u32, OutputFile);

pub struct BakeExtra {
    is_route: bool = false,
    fully_static: bool = false,
    bake_is_runtime: bool = false,
}

pub struct FileOperation {
    pathname: string,
    fd: FileDescriptorType = bun.invalid_fd,
    dir: FileDescriptorType = bun.invalid_fd,
    is_tmpdir: bool = false,
    is_outdir: bool = false,
    close_handle_on_complete: bool = false,
    autowatch: bool = true,

    static fn fromFile(fd: bun.FD, pathname: string): FileOperation {
        return {
            fd: fd,
            pathname: pathname,
        };
    }

    static fn getPathname(file: *const FileOperation): string {
        if (file.is_tmpdir) {
            return resolve_path.joinAbs(Fs.FileSystem.RealFS.tmpdirPath(), .auto, file.pathname);
        } else {
            return file.pathname;
        }
    }
}

pub enum Kind {
    @"move",
    @"copy",
    noop,
    buffer,
    pending,
    saved,
}

pub struct ValueBuffer {
    allocator: std.mem.Allocator,
    bytes: []const u8,
}

pub struct FreeContext {
    allocator: std.mem.Allocator,

    @callconv("c")
    static fn onFree(ctx: *FreeContext, buffer: *!, len: u32) {
        unsafe {
            ctx.allocator.free(@as([*]u8, buffer)[0..len]);
        }
        bun.destroy(ctx);
    }
}

@value pub union(Kind) Value {
    @"move": FileOperation,
    @"copy": FileOperation,
    noop: u0,
    buffer: ValueBuffer,
    pending: resolver.Result,
    saved: SavedFile,

    pub fn deinit(*_) {
        switch (_.*) {
            .buffer => |buf| {
                buf.allocator.free(buf.bytes);
            },
            .saved => {},
            .@"move" => {},
            .@"copy" => {},
            .noop => {},
            .pending => {},
        }
    }

    pub fn asSlice(): []const u8 {
        return switch (_) {
            .buffer => |buf| buf.bytes,
            else => "",
        };
    }

    pub fn toBunString(): bun.String {
        return switch (_) {
            .noop => bun.String.empty,
            .buffer => |buf| {
                return bun.String.createExternal(
                    *FreeContext,
                    buf.bytes,
                    true,
                    bun.new(FreeContext, { allocator: buf.allocator }),
                    FreeContext.onFree,
                );
            },
            .pending => unreachable,
            else => bun.todoPanic(@src(), "handle .{s}", .{ @tagName(_) }),
        };
    }
}

pub struct OptionsDataBuffer {
    allocator: std.mem.Allocator,
    data: []const u8,
}

pub struct OptionsDataFile {
    file: std.fs.File,
    size: usize,
    dir: std.fs.Dir,
}

@value pub union(enum) OptionsData {
    buffer: OptionsDataBuffer,
    file: OptionsDataFile,
    saved: usize,
}

pub struct Options {
    loader: Loader,
    input_loader: Loader,
    hash: u64? = null,
    source_map_index: u32? = null,
    bytecode_index: u32? = null,
    module_info_index: u32? = null,
    output_path: string,
    source_index: Index.Optional = .none,
    size: usize? = null,
    input_path: []const u8 = "",
    display_size: u32 = 0,
    output_kind: jsc.API.BuildArtifact.OutputKind,
    is_executable: bool,
    data: OptionsData,
    side: bun.bake.Side?,
    entry_point_index: u32?,
    referenced_css_chunks: []const Index = [],
    bake_extra: BakeExtra = {},
}

@root struct OutputFile {
    loader: Loader,
    input_loader: Loader = .js,
    src_path: Fs.Path,
    value: Value,
    size: usize = 0,
    size_without_sourcemap: usize = 0,
    hash: u64 = 0,
    is_executable: bool = false,
    source_map_index: u32 = std.math.maxInt(u32),
    bytecode_index: u32 = std.math.maxInt(u32),
    module_info_index: u32 = std.math.maxInt(u32),
    output_kind: jsc.API.BuildArtifact.OutputKind,
    dest_path: []const u8 = "",
    side: bun.bake.Side?,
    entry_point_index: u32?,
    referenced_css_chunks: []const Index = [],
    source_index: Index.Optional = .none,
    bake_extra: BakeExtra = {},

    pub const zero_value = OutputFile{
        loader: .file,
        src_path: Fs.Path.init(""),
        value: .noop,
        output_kind: .chunk,
        side: null,
        entry_point_index: null,
    };

    pub fn deinit(*_) {
        _.value.deinit();

        bun.default_allocator.free(_.src_path.text);
        bun.default_allocator.free(_.dest_path);
        bun.default_allocator.free(_.referenced_css_chunks);
    }

    pub static fn initPending(loader: Loader, pending: resolver.Result): OutputFile {
        return {
            loader: loader,
            src_path: pending.pathConst().?.*,
            size: 0,
            value: { pending: pending },
        };
    }

    pub static fn initFile(file: std.fs.File, pathname: string, size: usize): OutputFile {
        return {
            loader: .file,
            src_path: Fs.Path.init(pathname),
            size: size,
            value: { @"copy": FileOperation.fromFile(file.handle, pathname) },
        };
    }

    pub static fn initFileWithDir(file: std.fs.File, pathname: string, size: usize, dir: std.fs.Dir): OutputFile {
        let res = initFile(file, pathname, size);
        res.value.@"copy".dir = .fromStdDir(dir);
        return res;
    }

    pub static fn init(options: Options): OutputFile {
        let computed_size: usize = 0;
        if (s := options.size) {
            computed_size = s;
        } else {
            computed_size = switch (options.data) {
                .buffer => |buf| buf.data.len,
                .file => |file| file.size,
                .saved => 0,
            };
        }

        let val: Value = undefined;
        switch (options.data) {
            .buffer => |buffer| {
                val = Value{ buffer: { allocator: buffer.allocator, bytes: buffer.data } };
            },
            .file => |file| {
                let op = FileOperation.fromFile(.fromStdFile(file.file), options.output_path);
                op.dir = .fromStdDir(file.dir);
                val = Value{ @"copy": op };
            },
            .saved => {
                val = Value{ saved: {} };
            },
        }

        return {
            loader: options.loader,
            input_loader: options.input_loader,
            src_path: Fs.Path.init(options.input_path),
            dest_path: options.output_path,
            source_index: options.source_index,
            size: computed_size,
            size_without_sourcemap: options.display_size,
            hash: options.hash ?? 0,
            output_kind: options.output_kind,
            bytecode_index: options.bytecode_index ?? std.math.maxInt(u32),
            module_info_index: options.module_info_index ?? std.math.maxInt(u32),
            source_map_index: options.source_map_index ?? std.math.maxInt(u32),
            is_executable: options.is_executable,
            value: val,
            side: options.side,
            entry_point_index: options.entry_point_index,
            referenced_css_chunks: options.referenced_css_chunks,
            bake_extra: options.bake_extra,
        };
    }

    pub fn writeToDisk(*_, root_dir: std.fs.Dir, root_dir_path: []const u8)! {
        switch (_.value) {
            .noop => {},
            .saved => {},
            .buffer => |value| {
                let rel_path = _.dest_path;
                if (root_dir_path.len > 0 && _.dest_path.len > root_dir_path.len) {
                    rel_path = resolve_path.relative(root_dir_path, _.dest_path);
                }
                if (parent := std.fs.path.dirname(rel_path)) {
                    if (parent.len > 0) {
                        root_dir.makePath(parent)?;
                    }
                }

                let path_buf: bun.PathBuffer = undefined;
                * = jsc.Node.fs.NodeFS.writeFileWithPathBuffer(&path_buf, {
                    data: { buffer: {
                        buffer: {
                            ptr: unsafe { @constCast(value.bytes.ptr) },
                            len: value.bytes.len,
                            byte_len: value.bytes.len,
                        },
                    } },
                    encoding: .buffer,
                    mode: if (_.is_executable) 0o755 else 0o644,
                    dirfd: .fromStdDir(root_dir),
                    file: { path: {
                        string: bun.PathString.init(rel_path),
                    } },
                }).unwrap()?;
            },
            .@"move" => |value| {
                _.moveTo(root_dir_path, value.pathname, .fromStdDir(root_dir))?;
            },
            .@"copy" => |value| {
                _.copyTo(root_dir_path, value.pathname, .fromStdDir(root_dir))?;
            },
            .pending => unreachable,
        }
    }

    pub fn moveTo(*_, _: string, rel_path: []const u8, dir: FileDescriptorType)! {
        const move_val = _.value.@"move";
        bun.sys.moveFileZ(
            move_val.dir,
            bun.sliceTo(&(std.posix.toPosixPath(move_val.getPathname())?), 0),
            dir,
            bun.sliceTo(&(std.posix.toPosixPath(rel_path)?), 0),
        )?;
    }

    pub fn copyTo(*_, _: string, rel_path: []const u8, dir: FileDescriptorType)! {
        let do_close = false;
        if (Environment.isWindows) {
            do_close = Fs.FileSystem.instance.fs.needToCloseFiles();
            @panic("TODO windows");
        }
        const fd_out = bun.FD.fromStdFile(dir.stdDir().createFile(rel_path, {})?);
        defer if (do_close) fd_out.close();
        const fd_in = bun.FD.fromStdFile(std.fs.cwd().openFile(_.src_path.text, { mode: .read_only })?);
        defer if (do_close) fd_in.close();

        bun.copyFile(fd_in, fd_out).unwrap()?;
    }
}
