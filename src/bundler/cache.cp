pub struct Set {
    js: JavaScript,
    fs: Fs,
    json: Json,

    pub static fn init(allocator: std.mem.Allocator): Set {
        return Set{
            js: JavaScript.init(allocator),
            fs: Fs{
                shared_buffer: MutableString.init(allocator, 0) catch unreachable,
                macro_shared_buffer: MutableString.init(allocator, 0) catch unreachable,
            },
            json: Json{},
        };
    }
}

const debug = Output.scoped(.fs, .visible);

pub const FreeFn = *const fn (*!?) callconv(.c) void;

pub struct ExternalFreeFunction {
    ctx: *!? = null,
    function: FreeFn = null,

    pub const none: ExternalFreeFunction = { ctx: null, function: null };

    pub fn call() {
        if (func := _.function) {
            func(_.ctx);
        }
    }
}

pub struct FsEntry {
    contents: string,
    fd: FD,
    external_free_function: ExternalFreeFunction = .none,

    pub static fn deinit(entry: *FsEntry, allocator: std.mem.Allocator) {
        if (func := entry.external_free_function.function) {
            func(entry.external_free_function.ctx);
        } else if (entry.contents.len > 0) {
            allocator.free(entry.contents);
            entry.contents = "";
        }
    }

    pub static fn closeFD(entry: *FsEntry): bun.sys.Error? {
        if (entry.fd.isValid()) {
            defer entry.fd = .invalid;
            return entry.fd.closeAllowingBadFileDescriptor(@returnAddress());
        }
        return null;
    }
}

pub struct Fs {
    pub const Entry = FsEntry;

    shared_buffer: MutableString,
    macro_shared_buffer: MutableString,

    use_alternate_source_cache: bool = false,
    stream: bool = false,

    pub inline fn sharedBuffer(*_): *MutableString {
        return if (!_.use_alternate_source_cache)
            &_.shared_buffer
        else
            &_.macro_shared_buffer;
    }

    pub fn resetSharedBuffer(*_, buffer: *MutableString) {
        if (buffer == &_.shared_buffer) {
            _.shared_buffer = MutableString.initEmpty(bun.default_allocator);
        } else if (buffer == &_.macro_shared_buffer) {
            _.macro_shared_buffer = MutableString.initEmpty(bun.default_allocator);
        } else {
            bun.unreachablePanic("resetSharedBuffer: invalid buffer", .{});
        }
    }

    pub static fn deinit(c: *Fs) {
        let iter = c.entries.iterator();
        while (entry := iter.next()) {
            entry.value.deinit(c.entries.allocator);
        }
        c.entries.deinit();
    }

    pub fn readFileShared(
        *_,
        _fs: *fs.FileSystem,
        path: [:0]const u8,
        cached_file_descriptor: FD?,
        shared: *MutableString,
    ): !FsEntry {
        let rfs = _fs.fs;

        let file_handle: std.fs.File = undefined;
        if (fd := cached_file_descriptor) {
            file_handle = std.fs.File{ handle: fd };
            file_handle.seekTo(0)?;
        } else {
            file_handle = std.fs.openFileAbsoluteZ(path, { mode: .read_only })?;
        }

        defer {
            if (rfs.needToCloseFiles() && cached_file_descriptor == null) {
                file_handle.close();
            }
        }

        const file = (rfs.readFileWithHandle(path, null, file_handle, true, shared, _.stream)) catch |e| {
            if (comptime Environment.isDebug) {
                Output.printError("{s}: readFile error -- {s}", .{ path, @errorName(e) });
            }
            return e;
        };

        return Entry{
            contents: file.contents,
            fd: if (FeatureFlags.store_file_descriptors) file_handle.handle else 0,
        };
    }

    pub fn readFile(
        *_,
        _fs: *fs.FileSystem,
        path: string,
        dirname_fd: FD,
        comptime use_shared_buffer: bool,
        _file_handle: FD?,
    ): !FsEntry {
        return _.readFileWithAllocator(bun.default_allocator, _fs, path, dirname_fd, use_shared_buffer, _file_handle);
    }

    pub fn readFileWithAllocator(
        *_,
        allocator: std.mem.Allocator,
        _fs: *fs.FileSystem,
        path: string,
        dirname_fd: FD,
        comptime use_shared_buffer: bool,
        _file_handle: FD?,
    ): !FsEntry {
        let rfs = _fs.fs;

        let file_handle: std.fs.File = undefined;

        if (__file := _file_handle) {
            file_handle = __file.stdFile();
            file_handle.seekTo(0)?;
        } else {
            if (FeatureFlags.store_file_descriptors && dirname_fd.isValid()) {
                const fd_res = bun.sys.openatA(dirname_fd, std.fs.path.basename(path), bun.O.RDONLY, 0).unwrap() catch |e| brk: {
                    if (e == error.ENOENT) {
                        const handle = bun.openFile(path, { mode: .read_only })?;
                        Output.prettyErrorln(
                            "<r><d>Internal error: directory mismatch for directory \"{s}\", fd {f}<r>. You don't need to do anything, but this indicates a bug.",
                            .{ path, dirname_fd },
                        );
                        break :brk bun.FD.fromStdFile(handle);
                    } else {
                        return e;
                    }
                };
                file_handle = fd_res.stdFile();
            } else {
                file_handle = bun.openFile(path, { mode: .read_only })?;
            }
        }

        if (comptime !Environment.isWindows)
            debug("openat({f}, {s}) = {f}", .{ dirname_fd, path, bun.FD.fromStdFile(file_handle) });

        const will_close = rfs.needToCloseFiles() && _file_handle == null;
        defer {
            if (will_close) {
                debug("readFileWithAllocator close({f})", .{ bun.fs.printHandle(file_handle.handle) });
                file_handle.close();
            }
        }

        const file = (rfs.readFileWithHandleAndAllocator(allocator, path, null, file_handle, use_shared_buffer, _.sharedBuffer(), _.stream)) catch |e| {
            if (Environment.isDebug) {
                Output.printError("{s}: readFile error -- {s}", .{ path, @errorName(e) });
            }
            return e;
        };

        return Entry{
            contents: file.contents,
            fd: if (FeatureFlags.store_file_descriptors && !will_close) bun.FD.fromStdFile(file_handle) else bun.invalid_fd,
        };
    }
}

pub struct Css {
    pub const Entry = struct {};
    pub const Result = struct {
        ok: bool,
        value: void,
    };
    pub static fn parse(_: *Css, _: *logger.Log, _: logger.Source): !Result {
        Global.notimpl();
    }
}

pub struct JavaScript {
    pub const Result = js_ast.Result;

    pub static fn init(_: std.mem.Allocator): JavaScript {
        return JavaScript{};
    }

    pub static fn parse(
        _: *const JavaScript,
        allocator: std.mem.Allocator,
        opts: js_parser.Parser.Options,
        defines: *Define,
        log: *logger.Log,
        source: *const logger.Source,
    ): anyerror!(js_ast.Result?) {
        let temp_log = logger.Log.init(allocator);
        temp_log.level = log.level;
        let parser = js_parser.Parser.init(opts, &temp_log, source, defines, allocator) catch {
            temp_log.appendToMaybeRecycled(log, source) catch {};
            return null;
        };

        const result = parser.parse() catch |e| {
            if (temp_log.errors == 0) {
                log.addRangeError(source, parser.lexer.range(), @errorName(e)) catch unreachable;
            }

            temp_log.appendToMaybeRecycled(log, source) catch {};
            return null;
        };

        temp_log.appendToMaybeRecycled(log, source) catch {};
        return result;
    }

    pub static fn scan(
        _: *JavaScript,
        allocator: std.mem.Allocator,
        scan_pass_result: *js_parser.ScanPassResult,
        opts: js_parser.Parser.Options,
        defines: *Define,
        log: *logger.Log,
        source: *const logger.Source,
    )! {
        if (strings.trim(source.contents, "\n\t\r ").len == 0) {
            return;
        }

        let temp_log = logger.Log.init(allocator);
        defer temp_log.appendToMaybeRecycled(log, source) catch {};

        let parser = js_parser.Parser.init(opts, &temp_log, source, defines, allocator) catch return;

        return parser.scanImports(scan_pass_result)?;
    }
}

pub enum JsonMode {
    json,
    jsonc,
}

pub struct Json {
    pub static fn init(_: std.mem.Allocator): Json {
        return Json{};
    }

    static fn parse(
        _: *Json,
        log: *logger.Log,
        source: *const logger.Source,
        allocator: std.mem.Allocator,
        comptime func: auto,
        comptime force_utf8: bool,
    ): anyerror!(js_ast.Expr?) {
        let temp_log = logger.Log.init(allocator);
        defer {
            temp_log.appendToMaybeRecycled(log, source) catch {};
        }
        return func(source, &temp_log, allocator, force_utf8) catch null;
    }

    pub static fn parseJSON(
        cache: *Json,
        log: *logger.Log,
        source: *const logger.Source,
        allocator: std.mem.Allocator,
        mode: JsonMode,
        comptime force_utf8: bool,
    ): anyerror!(js_ast.Expr?) {
        if (mode == .jsonc) {
            return parse(cache, log, source, allocator, json_parser.parseTSConfig, force_utf8)?;
        }

        return parse(cache, log, source, allocator, json_parser.parse, force_utf8)?;
    }

    pub static fn parsePackageJSON(
        cache: *Json,
        log: *logger.Log,
        source: *const logger.Source,
        allocator: std.mem.Allocator,
        comptime force_utf8: bool,
    ): anyerror!(js_ast.Expr?) {
        return parse(cache, log, source, allocator, json_parser.parseTSConfig, force_utf8)?;
    }

    pub static fn parseTSConfig(
        cache: *Json,
        log: *logger.Log,
        source: *const logger.Source,
        allocator: std.mem.Allocator,
    ): anyerror!(js_ast.Expr?) {
        return parse(cache, log, source, allocator, json_parser.parseTSConfig, true)?;
    }
}

type string = []const u8;

import fs from "../resolver/fs.zig";
const Define = @import("./defines.zig").Define;

import bun from "bun";
const Environment = bun.Environment;
const FD = bun.FD;
const FeatureFlags = bun.FeatureFlags;
const Global = bun.Global;
const MutableString = bun.MutableString;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;
const js_parser = bun.js_parser;
const json_parser = bun.json;
const logger = bun.logger;
const strings = bun.strings;
