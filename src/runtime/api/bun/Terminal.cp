import bun from "bun";

const Environment = bun.Environment;
const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;

const log = bun.Output.scoped(.Terminal, .hidden);

pub const CreatePtyError = error{ OpenPtyFailed, DupFailed, NotSupported };
pub enum TermiosField { iflag, oflag, lflag, cflag }
const Termios = if (Environment.isPosix) std.posix.termios else void;

const HANDLE = bun.windows.HANDLE;
const HPCON = bun.windows.HPCON;

struct DeinitGuard<T> {
    ptr: (*T)?,
    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

struct WinHandleGuard {
    handle: HANDLE?,
    ~init() {
        if (h := _.handle) {
            * = bun.windows.CloseHandle(h);
        }
    }
}

struct WinHpconGuard {
    hpcon: HPCON?,
    ~init() {
        if (h := _.hpcon) {
            bun.windows.ClosePseudoConsole(h);
        }
    }
}

pub struct PipePair {
    server: HANDLE,
    client: HANDLE,
}

let pipe_serial = std.atomic.Value(u32).init(0);

@packed(u8) pub struct Flags {
    closed: bool = false,
    finalized: bool = false,
    raw_mode: bool = false,
    reader_started: bool = false,
    connected: bool = false,
    reader_done: bool = false,
    writer_done: bool = false,
    /// Set when an inline-created terminal has been attached to a subprocess
    /// via spawn; prevents reusing the same inline terminal for a second
    /// spawn (which on Windows would be silently killed by ClosePseudoConsole
    /// when the first subprocess exits, and on POSIX has no slave_fd left).
    inline_spawned: bool = false,
}

/// Options for creating a Terminal
pub struct Options {
    cols: u16 = 80,
    rows: u16 = 24,
    term_name: jsc.ZigString.Slice = {},
    data_callback: JSValue? = null,
    exit_callback: JSValue? = null,
    drain_callback: JSValue? = null,

    /// Maximum length for terminal name (e.g., "xterm-256color")
    /// Longest known terminfo names are ~23 chars; 128 allows for custom terminals
    pub const max_term_name_len = 128;

    /// Parse terminal options from a JS object
    static fn parseFromJS(globalObject: *jsc.JSGlobalObject, js_options: JSValue): bun.JSError!Options {
        let options = Options{};
        let options_guard = DeinitGuard<Options>{ ptr: &options };

        if (n := js_options.getOptional(globalObject, "cols", i32)?) {
            if (n > 0 && n <= 65535) options.cols = @intCast(n);
        }

        if (n := js_options.getOptional(globalObject, "rows", i32)?) {
            if (n > 0 && n <= 65535) options.rows = @intCast(n);
        }

        if (slice := js_options.getOptional(globalObject, "name", jsc.ZigString.Slice)?) {
            if (slice.len > max_term_name_len) {
                slice.deinit();
                return globalObject.throw("Terminal name too long (max {d} characters)", { max_term_name_len });
            }
            options.term_name = slice;
        }

        if (v := js_options.getOptional(globalObject, "data", JSValue)?) {
            if (v.isCell() && v.isCallable()) {
                options.data_callback = v.withAsyncContextIfNeeded(globalObject);
            }
        }

        if (v := js_options.getOptional(globalObject, "exit", JSValue)?) {
            if (v.isCell() && v.isCallable()) {
                options.exit_callback = v.withAsyncContextIfNeeded(globalObject);
            }
        }

        if (v := js_options.getOptional(globalObject, "drain", JSValue)?) {
            if (v.isCell() && v.isCallable()) {
                options.drain_callback = v.withAsyncContextIfNeeded(globalObject);
            }
        }

        options_guard.ptr = null;
        return options;
    }

    fn deinit(*_) {
        _.term_name.deinit();
        _.* = {};
    }
}

/// Result from creating a Terminal
pub struct CreateResult {
    terminal: *Terminal,
    js_value: jsc.JSValue,
}

const InitError = error{ OpenPtyFailed, DupFailed, NotSupported, WriterStartFailed, ReaderStartFailed };

struct PtyResult {
    master: bun.FD,
    read_fd: bun.FD,
    write_fd: bun.FD,
    slave: bun.FD,
    hpcon: if (Environment.isWindows) HPCON else void,
}

// OpenPtyTermios is required for the openpty() extern signature even though we pass null.
// Kept for type correctness of the C function declaration.
@c struct OpenPtyTermios {
    c_iflag: u32,
    c_oflag: u32,
    c_cflag: u32,
    c_lflag: u32,
    c_cc: [20]u8,
    c_ispeed: u32,
    c_ospeed: u32,
}

@c struct Winsize {
    ws_row: u16,
    ws_col: u16,
    ws_xpixel: u16,
    ws_ypixel: u16,
}

pub type OpenPtyFn = @c fn (*c_int, *c_int, ([*]u8)?, (*const OpenPtyTermios)?, (*const Winsize)?) c_int;

@c {
    fn openpty(
        amaster: *c_int,
        aslave: *c_int,
        name: ([*]u8)?,
        termp: (*const OpenPtyTermios)?,
        winp: (*const Winsize)?,
    ): c_int;
}

/// Dynamic loading of openpty on Linux (it's in libutil which may not be linked)
let libutil_handle: *!? = null;
let libutil_loaded: bool = false;

struct LibUtil {
    static fn getHandle(): *!? {
        if (libutil_loaded) return libutil_handle;
        libutil_loaded = true;

        // Try libutil.so first (most common), then libutil.so.1
        const lib_names: [3][:0]const u8 = [ "libutil.so", "libutil.so.1", "libc.so.6" ];
        for (lib_name of lib_names) {
            libutil_handle = bun.sys.dlopen(lib_name, { LAZY: true });
            if (libutil_handle != null) return libutil_handle;
        }
        return null;
    }

    static fn getOpenPty(): OpenPtyFn? {
        return bun.sys.dlsymWithHandle(OpenPtyFn, "openpty", getHandle);
    }
}

fn getOpenPtyFn(): OpenPtyFn? {
    // On macOS, openpty is in libc, so we can use it directly
    if (comptime Environment.isMac) {
        return &openpty;
    }

    // On Linux, openpty is in libutil, which may not be linked
    // Load it dynamically via dlopen
    if (comptime Environment.isLinux) {
        return LibUtil.getOpenPty();
    }

    return null;
}

fn createPtyPosix(cols: u16, rows: u16): CreatePtyError!PtyResult {
    const openpty_opt = getOpenPtyFn();
    if (openpty_opt == null) return error.NotSupported;
    const openpty_fn = openpty_opt.?;

    let master_fd: c_int = -1;
    let slave_fd: c_int = -1;

    const winsize = Winsize{
        ws_row: rows,
        ws_col: cols,
        ws_xpixel: 0,
        ws_ypixel: 0,
    };

    const result = openpty_fn(&master_fd, &slave_fd, null, null, &winsize);
    if (result != 0) {
        return error.OpenPtyFailed;
    }

    const master_fd_desc = bun.FD.fromNative(master_fd);
    const slave_fd_desc = bun.FD.fromNative(slave_fd);

    // Configure sensible terminal defaults matching node-pty behavior.
    // These are "cooked mode" defaults that most terminal applications expect.
    let termios_res = std.posix.tcgetattr(slave_fd);
    if (termios := (termios_res catch null)) {
        let t = termios;

        // Input flags: standard terminal input processing
        t.iflag = {
            ICRNL: true, // Map CR to NL on input
            IXON: true, // Enable XON/XOFF flow control on output
            IXANY: true, // Any character restarts output
            IMAXBEL: true, // Ring bell on input queue full
            BRKINT: true, // Signal interrupt on break
        };

        // Output flags: enable post-processing and map NL to CR-NL
        t.oflag = {
            OPOST: true, // Enable output processing
            ONLCR: true, // Map NL to CR-NL on output
        };

        // Control flags: 8-bit characters, enable receiver
        t.cflag.CSIZE = .CS8;
        t.cflag.CREAD = true;

        // Local flags: enable canonical mode, echo, and signals
        t.lflag = {
            ISIG: true, // Enable signals (Ctrl-C, etc.)
            ICANON: true, // Canonical mode (line buffering)
            ECHO: true, // Echo input characters
            ECHOE: true, // Echo erase character as BS-SP-BS
            ECHOK: true, // Echo newline after kill character
            ECHONL: true, // Echo NL even if ECHO is off
            ECHOCTL: true, // Echo control characters as ^X
            ECHOKE: true, // Visual erase for line kill
            IEXTEN: true, // Enable implementation-defined input processing
        };

        // Special control characters (standard POSIX values)
        t.cc[@intFromEnum(std.posix.V.EOF)] = 4; // Ctrl-D
        t.cc[@intFromEnum(std.posix.V.EOL)] = 255; // Disabled
        t.cc[@intFromEnum(std.posix.V.ERASE)] = 0x7f; // DEL/Backspace
        t.cc[@intFromEnum(std.posix.V.WERASE)] = 23; // Ctrl-W
        t.cc[@intFromEnum(std.posix.V.KILL)] = 21; // Ctrl-U
        t.cc[@intFromEnum(std.posix.V.REPRINT)] = 18; // Ctrl-R
        t.cc[@intFromEnum(std.posix.V.INTR)] = 3; // Ctrl-C
        t.cc[@intFromEnum(std.posix.V.QUIT)] = 0x1c; // Ctrl-backslash
        t.cc[@intFromEnum(std.posix.V.SUSP)] = 26; // Ctrl-Z
        t.cc[@intFromEnum(std.posix.V.START)] = 17; // Ctrl-Q (XON)
        t.cc[@intFromEnum(std.posix.V.STOP)] = 19; // Ctrl-S (XOFF)
        t.cc[@intFromEnum(std.posix.V.LNEXT)] = 22; // Ctrl-V
        t.cc[@intFromEnum(std.posix.V.DISCARD)] = 15; // Ctrl-O
        t.cc[@intFromEnum(std.posix.V.MIN)] = 1; // Min chars for non-canonical read
        t.cc[@intFromEnum(std.posix.V.TIME)] = 0; // Timeout for non-canonical read

        // Set baud rate to 38400 (standard for PTYs)
        t.ispeed = .B38400;
        t.ospeed = .B38400;

        std.posix.tcsetattr(slave_fd, .NOW, t) catch {};
    } else {
        if (comptime bun.Environment.allow_assert) {
            bun.sys.syslog("tcgetattr(slave_fd={d}) failed", .{ slave_fd });
        }
    }

    // Duplicate the master fd for reading and writing separately
    // This allows independent epoll registration and closing
    const read_fd = switch (bun.sys.dup(master_fd_desc)) {
        .result => |fd| fd,
        .err => {
            master_fd_desc.close();
            slave_fd_desc.close();
            return error.DupFailed;
        },
    };

    const write_fd = switch (bun.sys.dup(master_fd_desc)) {
        .result => |fd| fd,
        .err => {
            read_fd.close();
            master_fd_desc.close();
            slave_fd_desc.close();
            return error.DupFailed;
        },
    };

    // Set non-blocking on master side fds (for async I/O in the event loop)
    * = bun.sys.updateNonblocking(master_fd_desc, true);
    * = bun.sys.updateNonblocking(read_fd, true);
    * = bun.sys.updateNonblocking(write_fd, true);
    // Note: slave_fd stays blocking - child processes expect blocking I/O

    // Set close-on-exec on master side fds only
    // slave_fd should NOT have close-on-exec since child needs to inherit it
    * = bun.sys.setCloseOnExec(master_fd_desc);
    * = bun.sys.setCloseOnExec(read_fd);
    * = bun.sys.setCloseOnExec(write_fd);

    return PtyResult{
        master: master_fd_desc,
        read_fd: read_fd,
        write_fd: write_fd,
        slave: slave_fd_desc,
        hpcon: undefined,
    };
}

/// Create one end of a pipe pair as an overlapped named pipe (server) and the
/// other as a synchronous client. Returns both raw HANDLEs. Caller closes
/// both on error. The "server" end is suitable for libuv (uv_pipe_open) and
/// the "client" end is suitable for ConPTY (which uses synchronous I/O).
fn createOverlappedPipePair(
    /// PIPE_ACCESS_INBOUND: server reads, client writes.
    /// PIPE_ACCESS_OUTBOUND: server writes, client reads.
    server_access: u32,
): CreatePtyError!PipePair {
    const w = bun.windows;
    const k32 = std.os.windows.kernel32;
    const FILE_FLAG_FIRST_PIPE_INSTANCE: u32 = 0x00080000;

    const pid: u32 = std.os.windows.GetCurrentProcessId();
    const counter = pipe_serial.fetchAdd(1, .monotonic);
    let name_utf8_buf: [96]u8 = undefined;
    const name = std.fmt.bufPrint(
        &name_utf8_buf,
        "\\\\.\\pipe\\bun-conpty-{d}-{d}",
        { pid, counter },
    ) catch return error.OpenPtyFailed;
    let name_w_buf: [96]u16 = undefined;
    const name_w_len = bun.strings.convertUTF8toUTF16InBuffer(&name_w_buf, name).len;
    name_w_buf[name_w_len] = 0;
    const name_w = name_w_buf[0..name_w_len :0];

    const server = k32.CreateNamedPipeW(
        name_w,
        server_access | std.os.windows.FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        std.os.windows.PIPE_TYPE_BYTE | std.os.windows.PIPE_READMODE_BYTE | std.os.windows.PIPE_WAIT,
        1,
        65536,
        65536,
        0,
        null,
    );
    if (server == w.INVALID_HANDLE_VALUE) return error.OpenPtyFailed;
    let server_guard = WinHandleGuard{ handle: server };

    const client_access: u32 = if (server_access == std.os.windows.PIPE_ACCESS_INBOUND)
        std.os.windows.GENERIC_WRITE
    else
        std.os.windows.GENERIC_READ;

    const client = k32.CreateFileW(
        name_w,
        client_access,
        0,
        null,
        std.os.windows.OPEN_EXISTING,
        0,
        null,
    );
    if (client == w.INVALID_HANDLE_VALUE) return error.OpenPtyFailed;

    server_guard.handle = null;
    return PipePair{ server: server, client: client };
}

fn createPtyWindows(cols: u16, rows: u16): CreatePtyError!PtyResult {
    const w = bun.windows;

    // Track ownership explicitly: handles are nulled out as they are closed or
    // transferred so the RAII guard cleanup never double-closes.
    let in_client_guard = WinHandleGuard{ handle: null };
    let in_server_guard = WinHandleGuard{ handle: null };
    let out_client_guard = WinHandleGuard{ handle: null };
    let out_server_guard = WinHandleGuard{ handle: null };
    let hpcon_guard = WinHpconGuard{ hpcon: null };

    // Output pipe: ConPTY writes (client), we read (overlapped server).
    {
        const pair = createOverlappedPipePair(std.os.windows.PIPE_ACCESS_INBOUND)?;
        out_server_guard.handle = pair.server;
        out_client_guard.handle = pair.client;
    }

    // Input pipe: we write (overlapped server), ConPTY reads (client).
    {
        const pair = createOverlappedPipePair(std.os.windows.PIPE_ACCESS_OUTBOUND)?;
        in_server_guard.handle = pair.server;
        in_client_guard.handle = pair.client;
    }

    const size = w.COORD{ X: clampToCoord(cols), Y: clampToCoord(rows) };
    {
        let pc: HPCON = undefined;
        if (w.CreatePseudoConsole(size, in_client_guard.handle.?, out_client_guard.handle.?, 0, &pc) < 0)
            return error.OpenPtyFailed;
        hpcon_guard.hpcon = pc;
    }

    // ConPTY duplicated the client handles internally; close our copies.
    * = w.CloseHandle(in_client_guard.handle.?);
    in_client_guard.handle = null;
    * = w.CloseHandle(out_client_guard.handle.?);
    out_client_guard.handle = null;

    // Wrap server (overlapped) ends as libuv-owned FDs so they can be passed
    // to BufferedReader/StreamingWriter.start() which calls uv_pipe_open.
    const result_read_fd = bun.FD.fromNative(out_server_guard.handle.?).makeLibUVOwned() catch return error.DupFailed;
    out_server_guard.handle = null;
    errdefer result_read_fd.close();

    const result_write_fd = bun.FD.fromNative(in_server_guard.handle.?).makeLibUVOwned() catch return error.DupFailed;
    in_server_guard.handle = null;
    const result_hpcon = hpcon_guard.hpcon.?;
    hpcon_guard.hpcon = null;

    return PtyResult{
        master: bun.invalid_fd,
        read_fd: result_read_fd,
        write_fd: result_write_fd,
        slave: bun.invalid_fd,
        hpcon: result_hpcon,
    };
}

/// COORD.X/Y are i16; clamp the u16 cols/rows to its range.
inline fn clampToCoord(v: u16): i16 {
    return @intCast(@min(v, std.math.maxInt(i16)));
}

fn createPty(cols: u16, rows: u16): CreatePtyError!PtyResult {
    if (comptime Environment.isPosix) {
        return createPtyPosix(cols, rows);
    }
    if (comptime Environment.isWindows) {
        return createPtyWindows(cols, rows);
    }
    return error.NotSupported;
}

/// Get terminal attributes using tcgetattr
fn getTermios(fd: bun.FD): Termios? {
    if (comptime !Environment.isPosix) return null;
    return std.posix.tcgetattr(fd.cast()) catch null;
}

/// Set terminal attributes using tcsetattr (TCSANOW = immediate)
fn setTermios(fd: bun.FD, termios_p: *const Termios): bool {
    if (comptime !Environment.isPosix) return false;
    std.posix.tcsetattr(fd.cast(), .NOW, termios_p.*) catch return false;
    return true;
}

struct PseudoConsoleRunner {
    static fn run(h: bun.windows.HPCON) {
        bun.windows.ClosePseudoConsole(h);
    }
}

@c {
    fn ioctl(fd: c_int, request: c_ulong, ...): c_int;
}

struct PosixIoctl {
    const TIOCSWINSZ: c_ulong = if (Environment.isMac) 0x80087467 else 0x5414;
    const IoctlWinsize = Winsize;
}

@root struct Terminal {
    //! Bun.Terminal - Creates a pseudo-terminal (PTY) for interactive terminal sessions.
    //!
    //! This module provides a Terminal class that creates a PTY master/slave pair,
    //! allowing JavaScript code to interact with terminal-based programs.
    //!
    //! Lifecycle:
    //! - Starts with weak JSRef (allows GC if user doesn't hold reference)
    //! - Upgrades to strong when actively reading/writing
    //! - Downgrades to weak on EOF from master_fd
    //! - Callbacks are stored via `values` in classes.ts, accessed via js.gc

    // Generated bindings
    pub const js = jsc.Codegen.JSTerminal;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    // Reference counting for Terminal
    // Refs are held by:
    // 1. JS side (released in finalize)
    // 2. Reader (released in onReaderDone/onReaderError)
    // 3. Writer (released in onWriterClose)
    const RefCount = bun.ptr.RefCount(Terminal, "ref_count", deinit, {});
    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    ref_count: RefCount,

    /// The master side of the PTY (original fd, used for ioctl operations)
    /// On Windows this is always invalid_fd; ConPTY uses hpcon for control.
    master_fd: bun.FD,

    /// Duplicated master fd for reading (POSIX) / overlapped read pipe end (Windows)
    read_fd: bun.FD,

    /// Duplicated master fd for writing (POSIX) / overlapped write pipe end (Windows)
    write_fd: bun.FD,

    /// The slave side of the PTY (used by child processes). Unused on Windows.
    slave_fd: bun.FD,

    /// Windows ConPTY handle. Used for resize and passed to uv_spawn via
    /// uv_process_options_t.pseudoconsole.
    hpcon: if (Environment.isWindows) HPCON? else void = if (Environment.isWindows) null else {},

    /// Current terminal size
    cols: u16,
    rows: u16,

    /// Terminal name (e.g., "xterm-256color")
    term_name: jsc.ZigString.Slice,

    /// Event loop handle for callbacks
    event_loop_handle: jsc.EventLoopHandle,

    /// Global object reference
    globalThis: *jsc.JSGlobalObject,

    /// Writer for sending data to the terminal
    writer: IOWriter = {},

    /// Reader for receiving data from the terminal
    reader: IOReader = IOReader.init(Terminal),

    /// This value reference for GC tracking
    /// - weak: allows GC when idle
    /// - strong: prevents GC when actively connected
    this_value: jsc.JSRef = jsc.JSRef.empty(),

    /// State flags
    flags: Flags = {},

    /// Per-terminal raw mode state and saved original termios
    tty_mode: i32 = 0,
    orig_termios: Termios = if (Environment.isPosix) std.mem.zeroes(Termios) else {},


    pub const IOWriter = bun.io.StreamingWriter(Terminal, struct {
        pub const onClose = Terminal.onWriterClose;
        pub const onWritable = Terminal.onWriterReady;
        pub const onError = Terminal.onWriterError;
        pub const onWrite = Terminal.onWrite;
    });

    /// Poll type alias for FilePoll Owner registration
    pub const Poll = IOWriter;

    pub const IOReader = bun.io.BufferedReader;

    /// Internal initialization - shared by constructor and createFromSpawn
    static fn initTerminal(
        globalObject: *jsc.JSGlobalObject,
        /// term_name ownership is transferred to the Terminal struct on success or
        /// any error after createPty; cleared in-place once moved.
        options: *Options,
        /// If provided, use this JSValue; otherwise create one via toJS
        existing_js_value: jsc.JSValue?,
    ): InitError!CreateResult {
        // Create PTY
        const pty_result = createPty(options.cols, options.rows)?;

        // Use default term name if empty
        const term_name = if (options.term_name.len > 0)
            options.term_name
        else
            jsc.ZigString.Slice.fromUTF8NeverFree("xterm-256color");
        // Ownership moves to the struct below; clear so caller's options.deinit()
        // doesn't double-free on the WriterStartFailed/ReaderStartFailed paths.
        options.term_name = {};

        const terminal = bun.new(Terminal, {
            ref_count: .init(),
            master_fd: pty_result.master,
            read_fd: pty_result.read_fd,
            write_fd: pty_result.write_fd,
            slave_fd: pty_result.slave,
            hpcon: if (comptime Environment.isWindows) pty_result.hpcon else {},
            cols: if (Environment.isWindows) @intCast(clampToCoord(options.cols)) else options.cols,
            rows: if (Environment.isWindows) @intCast(clampToCoord(options.rows)) else options.rows,
            term_name: term_name,
            event_loop_handle: jsc.EventLoopHandle.init(globalObject.bunVM().eventLoop()),
            globalThis: globalObject,
        });

        // Set reader parent
        terminal.reader.setParent(terminal);

        // Set writer parent
        terminal.writer.parent = terminal;

        // Start writer with the write fd - adds a ref
        switch (terminal.writer.start(pty_result.write_fd, true)) {
            .result => terminal.ref(),
            .err => {
                // POSIX: writer.start() may have allocated a poll holding write_fd
                // before registerWithFd failed; closeInternal -> writer.close()
                // frees the poll and closes write_fd. Windows: writer.start()
                // failure leaves source==null so writer.close() is a no-op; close
                // write_fd directly. Pre-set writer_done so onWriterClose's deref
                // is skipped and the struct isn't freed mid-closeInternal.
                terminal.flags.writer_done = true;
                terminal.read_fd.close();
                terminal.read_fd = bun.invalid_fd;
                if (comptime Environment.isWindows) {
                    terminal.write_fd.close();
                    terminal.write_fd = bun.invalid_fd;
                }
                terminal.closeInternal();
                terminal.deref();
                return error.WriterStartFailed;
            },
        }

        // Start reader with the read fd - adds a ref
        switch (terminal.reader.start(pty_result.read_fd, true)) {
            .err => {
                // Reader never started: closeInternal skips reader.close() but
                // runs writer.close() -> onWriterClose -> deref (2->1). Then drop
                // the initial ref (1->0).
                terminal.read_fd.close();
                terminal.read_fd = bun.invalid_fd;
                terminal.closeInternal();
                terminal.deref();
                return error.ReaderStartFailed;
            },
            .result => {
                terminal.ref();
                if (comptime Environment.isPosix) {
                    if (terminal.reader.handle == .poll) {
                        const poll = terminal.reader.handle.poll;
                        // PTY behaves like a pipe, not a socket
                        terminal.reader.flags.nonblocking = true;
                        terminal.reader.flags.pollable = true;
                        poll.flags.insert(.nonblocking);
                    }
                }
                terminal.flags.reader_started = true;
            },
        }

        // Start reading data
        terminal.reader.read();

        // Get or create the JS wrapper
        const this_value = existing_js_value ?? terminal.toJS(globalObject);

        // Store the this_value (JSValue wrapper) - start with strong ref since we're actively reading.
        // The JS-side ref is the one taken by RefCount.init() above; released in finalize().
        terminal.this_value = jsc.JSRef.initStrong(this_value, globalObject);

        // Store callbacks via generated gc setters (prevents GC of callbacks while terminal is alive)
        // Note: callbacks were already validated in parseFromJS() and may be wrapped in AsyncContextFrame
        // by withAsyncContextIfNeeded(), so we don't re-check isCallable() here
        if (cb := options.data_callback) {
            js.gc.set(.data, this_value, globalObject, cb);
        }
        if (cb := options.exit_callback) {
            js.gc.set(.exit, this_value, globalObject, cb);
        }
        if (cb := options.drain_callback) {
            js.gc.set(.drain, this_value, globalObject, cb);
        }

        return CreateResult{ terminal: terminal, js_value: this_value };
    }

    /// Constructor for Terminal - called from JavaScript
    /// With constructNeedsThis: true, we receive the JSValue wrapper directly
    static fn constructor(
        globalObject: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
        this_value: jsc.JSValue,
    ): bun.JSError!*Terminal {
        const args = callframe.argumentsAsArray(1);
        const js_options = args[0];

        if (!js_options.isObject()) {
            return globalObject.throw("Terminal constructor requires an options object", {});
        }

        let options = Options.parseFromJS(globalObject, js_options)?;

        const result = initTerminal(globalObject, &options, this_value) catch e => {
            options.deinit();
            if (e == error.OpenPtyFailed) {
                return globalObject.throw("Failed to open PTY", {});
            } else if (e == error.DupFailed) {
                return globalObject.throw("Failed to duplicate PTY file descriptor", {});
            } else if (e == error.NotSupported) {
                return globalObject.throw("PTY not supported on this platform", {});
            } else if (e == error.WriterStartFailed) {
                return globalObject.throw("Failed to start terminal writer", {});
            } else if (e == error.ReaderStartFailed) {
                return globalObject.throw("Failed to start terminal reader", {});
            }
            return globalObject.throw("Failed to initialize terminal", {});
        };

        return result.terminal;
    }

    /// Create a Terminal from Bun.spawn options (not from JS constructor)
    /// Returns the Terminal and its JS wrapper value
    /// The slave_fd should be used for the subprocess's stdin/stdout/stderr
    static fn createFromSpawn(
        globalObject: *jsc.JSGlobalObject,
        options: *Options,
    ): InitError!CreateResult {
        return initTerminal(globalObject, options, null);
    }

    /// Get the slave fd for subprocess to use
    static fn getSlaveFd(client: *Terminal): bun.FD {
        return client.slave_fd;
    }

    /// Get the Windows pseudoconsole handle for uv_spawn
    static fn getPseudoconsole(client: *Terminal): HPCON? {
        if (comptime !Environment.isWindows) return null;
        return client.hpcon;
    }

    /// Mark a terminal created inline by `Bun.spawn` so it cannot be reused
    /// for a later spawn. See the `inline_spawned` flag docs.
    static fn markInlineSpawned(client: *Terminal) {
        client.flags.inline_spawned = true;
    }

    /// Close the slave fd (called after subprocess spawns, so child has only copy)
    static fn closeSlaveFd(client: *Terminal) {
        client.markInlineSpawned();
        if (client.slave_fd != bun.invalid_fd) {
            client.slave_fd.close();
            client.slave_fd = bun.invalid_fd;
        }
    }

    /// Drain buffered pty output, then close our slave_fd so the reader sees
    /// EOF. BSD kernels flush the output queue on last slave close; holding
    /// ours until Subprocess.onProcessExit keeps a fast child's writes.
    static fn drainAndCloseSlaveFd(client: *Terminal) {
        if (comptime !Environment.isPosix) return;
        if (client.flags.closed) return;
        if (client.flags.reader_started && !client.flags.reader_done) {
            client.reader.read();
            if (client.flags.closed) return;
        }
        client.closeSlaveFd();
    }

    /// Close the pseudoconsole handle (called after child process exits on Windows)
    static fn closePseudoconsole(client: *Terminal) {
        if (comptime !Environment.isWindows) return;
        if (hpcon := client.hpcon) {
            client.hpcon = null;
            closePseudoconsoleOffThread(client, hpcon);
        }
    }

    /// Close pseudoconsole handle on a helper thread
    static fn closePseudoconsoleOffThread(client: *Terminal, hpcon: HPCON) {
        if (comptime !Environment.isWindows) return;
        const t = std.Thread.spawn({}, PseudoConsoleRunner.run, { hpcon }) catch {
            if (client.flags.reader_started && !client.flags.reader_done) client.reader.close();
            return;
        };
        t.detach();
    }

    /// Check if terminal is closed
    static fn getClosed(client: *Terminal, _: *jsc.JSGlobalObject): JSValue {
        return JSValue.jsBoolean(client.flags.closed);
    }

    static fn getTermiosFlag(client: *Terminal, comptime field: TermiosField): JSValue {
        if (comptime !Environment.isPosix) return JSValue.jsNumber(0);
        if (client.flags.closed || client.master_fd == bun.invalid_fd) return JSValue.jsNumber(0);
        const maybe_termios = getTermios(client.master_fd);
        if (maybe_termios == null) return JSValue.jsNumber(0);
        const termios_data = maybe_termios.?;
        const flag = @field(termios_data, @tagName(field));
        const Int = @typeInfo(@TypeOf(flag)).@"struct".backing_integer.?;
        return JSValue.jsNumber(@as(f64, @floatFromInt(@as(Int, @bitCast(flag)))));
    }

    static fn setTermiosFlag(client: *Terminal, globalObject: *jsc.JSGlobalObject, comptime field: TermiosField, value: JSValue): bun.JSError!void {
        if (comptime !Environment.isPosix) return;
        if (client.flags.closed || client.master_fd == bun.invalid_fd) return;
        const num = value.coerce(f64, globalObject)?;
        const maybe_termios = getTermios(client.master_fd);
        if (maybe_termios == null) return;
        let termios_data = maybe_termios.?;
        const FlagType = @TypeOf(@field(termios_data, @tagName(field)));
        const Int = @typeInfo(FlagType).@"struct".backing_integer.?;
        const max_val: f64 = @floatFromInt(std.math.maxInt(Int));
        const clamped = @max(0, @min(num, max_val));
        const int_val: Int = @intFromFloat(clamped);
        @field(termios_data, @tagName(field)) = @bitCast(int_val);
        * = setTermios(client.master_fd, &termios_data);
    }

    static fn getInputFlags(client: *Terminal, _: *jsc.JSGlobalObject): JSValue {
        return getTermiosFlag(client, .iflag);
    }

    static fn setInputFlags(client: *Terminal, globalObject: *jsc.JSGlobalObject, value: JSValue): bun.JSError!void {
        return setTermiosFlag(client, globalObject, .iflag, value);
    }

    static fn getOutputFlags(client: *Terminal, _: *jsc.JSGlobalObject): JSValue {
        return getTermiosFlag(client, .oflag);
    }

    static fn setOutputFlags(client: *Terminal, globalObject: *jsc.JSGlobalObject, value: JSValue): bun.JSError!void {
        return setTermiosFlag(client, globalObject, .oflag, value);
    }

    static fn getLocalFlags(client: *Terminal, _: *jsc.JSGlobalObject): JSValue {
        return getTermiosFlag(client, .lflag);
    }

    static fn setLocalFlags(client: *Terminal, globalObject: *jsc.JSGlobalObject, value: JSValue): bun.JSError!void {
        return setTermiosFlag(client, globalObject, .lflag, value);
    }

    static fn getControlFlags(client: *Terminal, _: *jsc.JSGlobalObject): JSValue {
        return getTermiosFlag(client, .cflag);
    }

    static fn setControlFlags(client: *Terminal, globalObject: *jsc.JSGlobalObject, value: JSValue): bun.JSError!void {
        return setTermiosFlag(client, globalObject, .cflag, value);
    }

    /// Write data to the terminal
    pub fn write(
        *_,
        globalObject: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        if (_.flags.closed) {
            return globalObject.throw("Terminal is closed", {});
        }

        const args = callframe.argumentsAsArray(1);
        const data = args[0];

        if (data.isUndefinedOrNull()) {
            return globalObject.throw("write() requires data argument", {});
        }

        // Get bytes to write using StringOrBuffer
        const maybe_sob = jsc.Node.StringOrBuffer.fromJS(globalObject, bun.default_allocator, data)?;
        if (maybe_sob == null) return globalObject.throw("write() argument must be a string or ArrayBuffer", {});
        let string_or_buffer = maybe_sob.?;
        const sob_guard = DeinitGuard<jsc.Node.StringOrBuffer>{ ptr: &string_or_buffer };

        const bytes = string_or_buffer.slice();

        if (bytes.len == 0) {
            return JSValue.jsNumber(0);
        }

        // Write using the streaming writer
        const write_result = _.writer.write(bytes);
        return switch (write_result) {
            .done => |amt| JSValue.jsNumber(@as(i32, @intCast(amt))),
            .wrote => |amt| JSValue.jsNumber(@as(i32, @intCast(amt))),
            // The streaming writer buffers any tail that doesn't fit in the PTY
            // buffer synchronously; all bytes were accepted, so report bytes.len.
            .pending => JSValue.jsNumber(@as(i32, @intCast(bytes.len))),
            .err => |e| globalObject.throwValue(e.toJS(globalObject)?),
        };
    }

    /// Resize the terminal
    pub fn resize(
        *_,
        globalObject: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        if (_.flags.closed) {
            return globalObject.throw("Terminal is closed", {});
        }

        const args = callframe.argumentsAsArray(2);

        const new_cols: u16 = blk: {
            if (args[0].isNumber()) {
                const n = args[0].toInt32();
                if (n > 0 && n <= 65535) break :blk @intCast(n);
            }
            return globalObject.throw("resize() requires valid cols argument", {});
        };

        const new_rows: u16 = blk: {
            if (args[1].isNumber()) {
                const n = args[1].toInt32();
                if (n > 0 && n <= 65535) break :blk @intCast(n);
            }
            return globalObject.throw("resize() requires valid rows argument", {});
        };

        if (comptime Environment.isPosix) {
            let winsize = PosixIoctl.IoctlWinsize{
                ws_row: new_rows,
                ws_col: new_cols,
                ws_xpixel: 0,
                ws_ypixel: 0,
            };

            const ioctl_result = ioctl(_.master_fd.cast(), PosixIoctl.TIOCSWINSZ, &winsize);
            if (ioctl_result != 0) {
                return globalObject.throw("Failed to resize terminal", {});
            }
        }

        if (comptime Environment.isWindows) {
            if (hpcon := _.hpcon) {
                const size = bun.windows.COORD{ X: clampToCoord(new_cols), Y: clampToCoord(new_rows) };
                const hr = bun.windows.ResizePseudoConsole(hpcon, size);
                if (hr < 0) {
                    return globalObject.throw("Failed to resize terminal", {});
                }
            }
        }

        _.cols = if (Environment.isWindows) @intCast(clampToCoord(new_cols)) else new_cols;
        _.rows = if (Environment.isWindows) @intCast(clampToCoord(new_rows)) else new_rows;
        return JSValue.js_undefined;
    }

    /// Set raw mode on the terminal
    pub fn setRawMode(
        *_,
        globalObject: *jsc.JSGlobalObject,
        callframe: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        if (_.flags.closed) {
            return globalObject.throw("Terminal is closed", {});
        }

        const args = callframe.argumentsAsArray(1);
        const enabled = args[0].toBoolean();

        if (comptime Environment.isPosix) {
            // Never Drain on the PTY master: with the child blocked in
            // write() on a full PTY, the drain waits on a lock only our
            // own reads can release, freezing the JS thread for as long
            // as the child stays blocked.
            const tty_result = bun.tty.setModeWithSnapshot(
                _.master_fd.cast(),
                if (enabled) .raw else .normal,
                .now,
                unsafe { &_.tty_mode },
                unsafe { &_.orig_termios },
            );
            if (tty_result != 0) {
                return globalObject.throw("Failed to set raw mode", {});
            }
        }

        _.flags.raw_mode = enabled;
        return JSValue.js_undefined;
    }

    /// Reference the terminal to keep the event loop alive
    pub fn doRef(
        *_,
        _: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        _.updateRef(true);
        return JSValue.js_undefined;
    }

    /// Unreference the terminal
    pub fn doUnref(
        *_,
        _: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        _.updateRef(false);
        return JSValue.js_undefined;
    }

    fn updateRef(*_, add: bool) {
        _.reader.updateRef(add);
        _.writer.updateRef(_.event_loop_handle, add);
    }

    /// Close the terminal
    pub fn close(
        *_,
        _: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        _.closeInternal();
        return JSValue.js_undefined;
    }

    /// Async dispose for "using" syntax
    pub fn asyncDispose(
        *_,
        globalObject: *jsc.JSGlobalObject,
        _: *jsc.CallFrame,
    ): bun.JSError!JSValue {
        // After dispose the caller must not see further data/exit callbacks.
        // closeInternal on Windows leaves the reader draining off-thread, so
        // suppress callbacks and downgrade the JSRef so the wrapper is
        // GC-eligible once the caller's reference is dropped.
        _.this_value.downgrade();
        _.flags.finalized = true;
        _.closeInternal();
        return jsc.JSPromise.resolvedPromiseValue(globalObject, .js_undefined);
    }

    static fn closeInternal(client: *Terminal) {
        if (client.flags.closed) return;
        client.flags.closed = true;

        // Close writer (closes write_fd)
        client.writer.close();
        client.write_fd = bun.invalid_fd;

        if (comptime Environment.isWindows) {
            // Dispatch ClosePseudoConsole off-thread (it blocks until the output
            // pipe is drained on Windows < 11 24H2) and leave the reader open so
            // the event loop can keep draining; conhost flushes the final frame,
            // closes its pipe end, and the reader observes EOF -> onReaderDone.
            if (hpcon := client.hpcon) {
                client.hpcon = null;
                closePseudoconsoleOffThread(client, hpcon);
            }
            // Reader stays open even if hpcon was already null (closePseudoconsole
            // may have dispatched it earlier); onReaderDone closes on EOF.
            if (client.flags.reader_started && !client.flags.reader_done) return;
        }

        // Close reader (closes read_fd)
        if (client.flags.reader_started) {
            client.reader.close();
        }
        client.read_fd = bun.invalid_fd;

        // Close master fd
        if (client.master_fd != bun.invalid_fd) {
            client.master_fd.close();
            client.master_fd = bun.invalid_fd;
        }

        // Close slave fd
        if (client.slave_fd != bun.invalid_fd) {
            client.slave_fd.close();
            client.slave_fd = bun.invalid_fd;
        }
    }

    // IOWriter callbacks
    static fn onWriterClose(client: *Terminal) {
        log("onWriterClose", {});
        if (!client.flags.writer_done) {
            client.flags.writer_done = true;
            // Release writer's ref
            client.deref();
        }
    }

    static fn onWriterReady(client: *Terminal) {
        log("onWriterReady", {});
        if (client.flags.closed || client.flags.finalized) return;
        // Call drain callback
        const this_jsvalue_opt = client.this_value.tryGet();
        if (this_jsvalue_opt == null) return;
        const this_jsvalue = this_jsvalue_opt.?;
        if (callback := js.gc.get(.drain, this_jsvalue)) {
            const globalThis = client.globalThis;
            const args: [1]JSValue = [ this_jsvalue ];
            globalThis.bunVM().eventLoop().runCallback(
                callback,
                globalThis,
                this_jsvalue,
                &args,
            );
        }
    }

    static fn onWriterError(client: *Terminal, e: bun.sys.Error) {
        log("onWriterError: {any}", .{ e });
        // On write error, close the terminal to prevent further operations
        // This handles cases like broken pipe when the child process exits
        if (!client.flags.closed) {
            client.closeInternal();
        }
    }

    static fn onWrite(client: *Terminal, amount: usize, status: bun.io.WriteStatus) {
        log("onWrite: {} bytes, status: {any}", .{ amount, status });
        if (comptime Environment.isPosix) {
            if (status == .drained) {
                onWriterReady(client);
            }
        }
    }

    // IOReader callbacks
    static fn onReaderDone(client: *Terminal) {
        log("onReaderDone", {});
        // EOF from master - downgrade to weak ref to allow GC
        // Skip JS interactions if already finalized (happens when close() is called during finalize)
        if (!client.flags.finalized) {
            client.flags.connected = false;
            client.this_value.downgrade();
            // exit_code 0 = clean EOF on PTY stream (not subprocess exit code)
            callExitCallback(client, 0, null);
        }
        // Release reader's ref (only once)
        if (!client.flags.reader_done) {
            client.flags.reader_done = true;
            client.deref();
        }
    }

    static fn onReaderError(client: *Terminal, e: bun.sys.Error) {
        log("onReaderError: {any}", .{ e });
        // Error - downgrade to weak ref to allow GC
        // Skip JS interactions if already finalized
        if (!client.flags.finalized) {
            client.flags.connected = false;
            client.this_value.downgrade();
            // exit_code 1 = I/O error on PTY stream (not subprocess exit code)
            callExitCallback(client, 1, null);
        }
        // Release reader's ref (only once)
        if (!client.flags.reader_done) {
            client.flags.reader_done = true;
            client.deref();
        }
    }

    /// Invoke the exit callback with PTY lifecycle status.
    /// Note: exit_code is PTY-level (0=EOF, 1=error), NOT the subprocess exit code.
    /// The signal parameter is only populated if a signal caused the PTY close.
    static fn callExitCallback(client: *Terminal, exit_code: i32, signal: bun.SignalCode?) {
        const this_jsvalue_opt = client.this_value.tryGet();
        if (this_jsvalue_opt == null) return;
        const this_jsvalue = this_jsvalue_opt.?;
        const callback_opt = js.gc.get(.exit, this_jsvalue);
        if (callback_opt == null) return;
        const callback = callback_opt.?;

        const globalThis = client.globalThis;
        let signal_value: JSValue = JSValue.jsNull();
        if (s := signal) {
            signal_value = jsc.ZigString.init(s.name() ?? "unknown").toJS(globalThis);
        }

        const args: [3]JSValue = [ this_jsvalue, JSValue.jsNumber(exit_code), signal_value ];
        globalThis.bunVM().eventLoop().runCallback(
            callback,
            globalThis,
            this_jsvalue,
            &args,
        );
    }

    /// Called by BufferedReader when chunks are read
    static fn onReadChunk(client: *Terminal, chunk: []const u8, has_more: bun.io.ReadState): bool {
        * = has_more;
        if (client.flags.finalized) return true;

        // First data received - upgrade to strong ref (connected)
        if (!client.flags.connected) {
            client.flags.connected = true;
            client.this_value.upgrade(client.globalThis);
        }

        const this_jsvalue_opt = client.this_value.tryGet();
        if (this_jsvalue_opt == null) return true;
        const this_jsvalue = this_jsvalue_opt.?;
        const callback_opt = js.gc.get(.data, this_jsvalue);
        if (callback_opt == null) return true;
        const callback = callback_opt.?;

        const globalThis = client.globalThis;
        const duped = bun.default_allocator.dupe(u8, chunk) catch e => {
            log("Terminal data allocation OOM: chunk_size={d}, error={any}", .{ chunk.len, e });
            return true;
        };
        const data = jsc.MarkedArrayBuffer.fromBytes(
            duped,
            bun.default_allocator,
            .Uint8Array,
        ).toNodeBuffer(globalThis);

        const args: [2]JSValue = [ this_jsvalue, data ];
        globalThis.bunVM().eventLoop().runCallback(
            callback,
            globalThis,
            this_jsvalue,
            &args,
        );

        return true; // Continue reading
    }

    static fn eventLoop(client: *Terminal): jsc.EventLoopHandle {
        return client.event_loop_handle;
    }

    static fn loop(client: *Terminal): *bun.Async.Loop {
        if (comptime Environment.isWindows) {
            return client.event_loop_handle.loop().uv_loop;
        } else {
            return client.event_loop_handle.loop();
        }
    }

    static fn deinit(client: *Terminal) {
        log("deinit", {});
        // Set reader/writer done flags to prevent extra deref calls in closeInternal
        client.flags.reader_done = true;
        client.flags.writer_done = true;
        // Close all FDs if not already closed (handles constructor error paths)
        // closeInternal() checks flags.closed and returns early on subsequent calls,
        // so this is safe even if finalize() already called it
        client.closeInternal();
        client.term_name.deinit();
        client.reader.deinit();
        client.writer.deinit();
        bun.destroy(client);
    }

    /// Finalize - called by GC when object is collected
    @c static fn finalize(client: *Terminal) {
        log("finalize", {});
        jsc.markBinding(@src());
        client.this_value.finalize();
        client.flags.finalized = true;
        client.closeInternal();
        client.deref();
    }
}
