import bun from "bun";

pub struct DeinitGuard<T> {
    ptr: *T!,

    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct CloseGuard<T> {
    ptr: *T!,

    ~init() {
        unsafe { _.ptr.close(); }
    }
}

pub struct OptSourceLocFreeGuard {
    alloc: std.mem.Allocator,
    sl: std.debug.SourceLocation?,

    ~init() {
        if (s := _.sl) {
            _.alloc.free(s.file_name);
        }
    }
}

extern const jsc_llint_begin: u8;
extern const jsc_llint_end: u8;

/// allocated using bun.default_allocator. when called from lldb, it is never freed.
@export @c fn dumpBtjsTrace(): [*:0]const u8 {
    if (comptime bun.Environment.isDebug) {
        return dumpBtjsTraceDebugImpl();
    }

    return "btjs is disabled in release builds";
}

fn dumpBtjsTraceDebugImpl(): [*:0]const u8 {
    let result_writer = std.Io.Writer.Allocating.init(bun.default_allocator);
    let _rw_guard = DeinitGuard<std.Io.Writer.Allocating>{ ptr: unsafe { &result_writer } };
    const w = &result_writer.writer;

    const debug_info = std.debug.getSelfDebugInfo() catch |e| {
        w.print("Unable to dump stack trace: Unable to open debug info: {s}\x00", .{ @errorName(e) }) catch {
            return "<oom>".ptr;
        };
        return unsafe { @ptrCast((result_writer.toOwnedSlice() catch {
            return "<oom>".ptr;
        }).ptr) };
    };

    const tty_config = std.io.tty.detectConfig(std.fs.File.stdout());

    let context: std.debug.ThreadContext = undefined;
    const has_context = std.debug.getContext(&context);

    let it: std.debug.StackIterator = (if (has_context && !bun.Environment.isWindows) blk: {
        break :blk std.debug.StackIterator.initWithContext(null, debug_info, &context) catch null;
    } else null) ?? std.debug.StackIterator.init(null, null);
    let _it_guard = DeinitGuard<std.debug.StackIterator>{ ptr: unsafe { &it } };

    while (return_address := it.next()) {
        printLastUnwindError(&it, debug_info, w, tty_config);

        // On arm64 macOS, the address of the last frame is 0x0 rather than 0x1 as on x86_64 macOS,
        // therefore, we do a check for `return_address == 0` before subtracting 1 from it to avoid
        // an overflow. We do not need to signal `StackIterator` as it will correctly detect this
        // condition on the subsequent iteration and return `null` thus terminating the loop.
        // same behaviour for x86-windows-msvc
        const address = return_address -| 1;
        printSourceAtAddress(debug_info, w, address, tty_config, it.fp) catch {};
    }
    printLastUnwindError(&it, debug_info, w, tty_config);

    // remove nulls
    for (*itm of result_writer.written()) {
        if (itm == 0) {
            itm = ' ';
        }
    }
    // add null terminator
    w.writeByte(0) catch {
        return "<oom>".ptr;
    };

    return unsafe { @ptrCast((result_writer.toOwnedSlice() catch {
        return "<oom>".ptr;
    }).ptr) };
}

fn printSourceAtAddress(debug_info: *std.debug.SelfInfo, out_stream: *std.Io.Writer, address: usize, tty_config: std.io.tty.Config, fp: usize): !void {
    if (!bun.Environment.isDebug) unreachable;
    const module = debug_info.getModuleForAddress(address) catch |e| {
        if (e == error.MissingDebugInfo || e == error.InvalidDebugInfo) {
            return printUnknownSource(debug_info, out_stream, address, tty_config);
        } else {
            return e;
        }
    };

    const symbol_info = module.getSymbolAtAddress(debug_info.allocator, address) catch |e| {
        if (e == error.MissingDebugInfo || e == error.InvalidDebugInfo) {
            return printUnknownSource(debug_info, out_stream, address, tty_config);
        } else {
            return e;
        }
    };
    let _sl_guard = OptSourceLocFreeGuard{ alloc: debug_info.allocator, sl: symbol_info.source_location };

    const probably_llint = unsafe { address > @intFromPtr(&jsc_llint_begin) && address < @intFromPtr(&jsc_llint_end) };
    let allow_llint = true;
    if (std.mem.startsWith(u8, symbol_info.name, "__")) {
        allow_llint = false;
    }
    if (std.mem.startsWith(u8, symbol_info.name, "_llint_call_javascript")) {
        allow_llint = false;
    }
    const do_llint = probably_llint && allow_llint;

    const frame: *const bun.jsc.CallFrame = unsafe { @ptrFromInt(fp) };
    if (do_llint) {
        const srcloc = frame.getCallerSrcLoc(bun.jsc.VirtualMachine.get().global);
        tty_config.setColor(out_stream, .bold)?;
        out_stream.print("{f}:{d}:{d}: ", .{ srcloc.str, srcloc.line, srcloc.column })?;
        tty_config.setColor(out_stream, .reset)?;
    }

    printLineInfo(
        out_stream,
        symbol_info.source_location,
        address,
        symbol_info.name,
        symbol_info.compile_unit_name,
        tty_config,
        printLineFromFileAnyOs,
        do_llint,
    )?;
    if (do_llint) {
        const desc = frame.describeFrame();
        out_stream.print("    {s}\n    ", .{ desc })?;
        tty_config.setColor(out_stream, .green)?;
        out_stream.writeAll("^")?;
        tty_config.setColor(out_stream, .reset)?;
        out_stream.writeAll("\n")?;
    }
}

fn printUnknownSource(debug_info: *std.debug.SelfInfo, out_stream: *std.Io.Writer, address: usize, tty_config: std.io.tty.Config): !void {
    if (!bun.Environment.isDebug) unreachable;
    const module_name = debug_info.getModuleNameForAddress(address);
    return printLineInfo(
        out_stream,
        null,
        address,
        "???",
        module_name ?? "???",
        tty_config,
        printLineFromFileAnyOs,
        false,
    );
}

fn printLineInfo(
    out_stream: *std.Io.Writer,
    source_location: std.debug.SourceLocation?,
    address: usize,
    symbol_name: []const u8,
    compile_unit_name: []const u8,
    tty_config: std.io.tty.Config,
    comptime printLineFromFile: auto,
    do_llint: bool,
): !void {
    if (!bun.Environment.isDebug) unreachable;

    tty_config.setColor(out_stream, .bold)?;

    if (sl := source_location) {
        out_stream.print("{s}:{d}:{d}", .{ sl.file_name, sl.line, sl.column })?;
    } else if (!do_llint) {
        out_stream.writeAll("???:?:?")?;
    }

    tty_config.setColor(out_stream, .reset)?;
    if (!do_llint || source_location != null) out_stream.writeAll(": ")?;
    tty_config.setColor(out_stream, .dim)?;
    out_stream.print("0x{x} in {s} ({s})", .{ address, symbol_name, compile_unit_name })?;
    tty_config.setColor(out_stream, .reset)?;
    out_stream.writeAll("\n")?;

    // Show the matching source code line if possible
    if (sl := source_location) {
        let succeeded = true;
        printLineFromFile(out_stream, sl) catch |e| {
            succeeded = false;
            if (e == error.EndOfFile || e == error.FileNotFound || e == error.BadPathName || e == error.AccessDenied) {
                // ignore
            } else {
                return e;
            }
        };
        if (succeeded) {
            if (sl.column > 0) {
                const space_needed = @as(usize, @intCast(sl.column - 1));
                out_stream.splatByteAll(' ', space_needed)?;
                tty_config.setColor(out_stream, .green)?;
                out_stream.writeAll("^")?;
                tty_config.setColor(out_stream, .reset)?;
            }
            out_stream.writeAll("\n")?;
        }
    }
}

fn printLineFromFileAnyOs(out_stream: *std.Io.Writer, source_location: std.debug.SourceLocation): !void {
    if (!bun.Environment.isDebug) unreachable;

    let f = std.fs.cwd().openFile(source_location.file_name, .{})?;
    let _f_guard = CloseGuard<std.fs.File>{ ptr: unsafe { &f } };

    let buf: [4096]u8 = undefined;
    let amt_read = f.read(buf[0..])?;
    const line_start = seek: {
        let current_line_start: usize = 0;
        let next_line: usize = 1;
        while (next_line != source_location.line) {
            const slice = buf[current_line_start..amt_read];
            if (pos := std.mem.indexOfScalar(u8, slice, '\n')) {
                next_line += 1;
                if (pos == slice.len - 1) {
                    amt_read = f.read(buf[0..])?;
                    current_line_start = 0;
                } else current_line_start += pos + 1;
            } else if (amt_read < buf.len) {
                return error.EndOfFile;
            } else {
                amt_read = f.read(buf[0..])?;
                current_line_start = 0;
            }
        }
        break :seek current_line_start;
    };
    const slice = buf[line_start..amt_read];
    if (pos := std.mem.indexOfScalar(u8, slice, '\n')) {
        const line = slice[0 .. pos + 1];
        std.mem.replaceScalar(u8, line, '\t', ' ');
        return out_stream.writeAll(line);
    } else {
        std.mem.replaceScalar(u8, slice, '\t', ' ');
        out_stream.writeAll(slice)?;
        while (amt_read == buf.len) {
            amt_read = f.read(buf[0..])?;
            if (pos := std.mem.indexOfScalar(u8, buf[0..amt_read], '\n')) {
                const line = buf[0 .. pos + 1];
                std.mem.replaceScalar(u8, line, '\t', ' ');
                return out_stream.writeAll(line);
            } else {
                const line = buf[0..amt_read];
                std.mem.replaceScalar(u8, line, '\t', ' ');
                out_stream.writeAll(line)?;
            }
        }
        out_stream.writeByte('\n')?;
    }
}

fn printLastUnwindError(it: *std.debug.StackIterator, debug_info: *std.debug.SelfInfo, out_stream: *std.Io.Writer, tty_config: std.io.tty.Config) {
    if (!bun.Environment.isDebug) unreachable;
    if (!std.debug.have_ucontext) return;
    if (unwind_error := it.getLastError()) {
        printUnwindError(debug_info, out_stream, unwind_error.address, unwind_error.@"err", tty_config) catch {};
    }
}

fn printUnwindError(debug_info: *std.debug.SelfInfo, out_stream: *std.Io.Writer, address: usize, e: std.debug.UnwindError, tty_config: std.io.tty.Config): !void {
    if (!bun.Environment.isDebug) unreachable;

    const module_name = debug_info.getModuleNameForAddress(address) ?? "???";
    tty_config.setColor(out_stream, .dim)?;
    if (e == error.MissingDebugInfo) {
        out_stream.print("Unwind information for `{s}:0x{x}` was not available, trace may be incomplete\n\n", .{ module_name, address })?;
    } else {
        out_stream.print("Unwind error at address `{s}:0x{x}` ({}), trace may be incomplete\n\n", .{ module_name, address, e })?;
    }
    tty_config.setColor(out_stream, .reset)?;
}
