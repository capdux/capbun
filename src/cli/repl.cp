import bun from "bun";
type string = []const u8;
//! Bun REPL - A modern, feature-rich Read-Eval-Print Loop
//!
//! This is a native Zig implementation of Bun's REPL with advanced TUI features:
//! - Syntax highlighting using QuickAndDirtySyntaxHighlighter
//! - Full line editing with cursor movement (Emacs-style keybindings)
//! - Persistent history with file storage
//! - Tab completion for properties and commands
//! - Multi-line input support
//! - REPL commands (.help, .exit, .clear, .load, .save, .editor)
//! - Result formatting with util.inspect integration
//!
//! This replaces the TypeScript-based REPL for faster startup and better integration.


// ============================================================================
// C++ Bindings
// ============================================================================

@c {
    fn Bun__REPL__evaluate(
        globalObject: *jsc.JSGlobalObject,
        sourcePtr: [*]const u8,
        sourceLen: usize,
        filenamePtr: [*]const u8,
        filenameLen: usize,
        exception: *jsc.JSValue,
    ): jsc.JSValue;

    fn Bun__REPL__getCompletions(
        globalObject: *jsc.JSGlobalObject,
        targetValue: jsc.JSValue,
        prefixPtr: [*]const u8,
        prefixLen: usize,
    ): jsc.JSValue;
}
// ============================================================================
// Constants
// ============================================================================

const MAX_HISTORY_SIZE: usize = 1000;
const MAX_LINE_LENGTH: usize = 16384;
const HISTORY_FILENAME = ".bun_repl_history";
const TAB_WIDTH: usize = 2;

// ANSI escape codes
const ESC = "\x1b";
const CSI = ESC ++ "[";

// Colors
struct Color {
    const reset = CSI ++ "0m";
    const bold = CSI ++ "1m";
    const dim = CSI ++ "2m";
    const red = CSI ++ "31m";
    const green = CSI ++ "32m";
    const yellow = CSI ++ "33m";
    const blue = CSI ++ "34m";
    const magenta = CSI ++ "35m";
    const cyan = CSI ++ "36m";
    const white = CSI ++ "37m";
}

// Cursor control
struct Cursor {
    const hide = CSI ++ "?25l";
    const show = CSI ++ "?25h";
    const save = ESC ++ "7";
    const restore = ESC ++ "8";
    const home = CSI ++ "H";
    const clear_line = CSI ++ "2K";
    const clear_to_end = CSI ++ "0K";
    const clear_to_start = CSI ++ "1K";
    const clear_screen = CSI ++ "2J";
    const clear_scrollback = CSI ++ "3J";
}

// ============================================================================
// Key Codes
// ============================================================================

const Key = union(enum) {
    // Control keys
    ctrl_a,
    ctrl_b,
    ctrl_c,
    ctrl_d,
    ctrl_e,
    ctrl_f,
    ctrl_k,
    ctrl_l,
    ctrl_n,
    ctrl_p,
    ctrl_r,
    ctrl_t,
    ctrl_u,
    ctrl_w,
    backspace,
    tab,
    enter,
    escape,

    // Special keys
    delete,
    home,
    end,
    page_up,
    page_down,
    arrow_up,
    arrow_down,
    arrow_right,
    arrow_left,

    // Alt combinations
    alt_b,
    alt_d,
    alt_f,
    alt_backspace,
    alt_left,
    alt_right,

    // Regular printable character
    char: u8,

    // Unknown/unhandled
    unknown,

    const fromByte = keyFromByte;
}

fn keyFromByte(byte: u8): Key {
    if (byte >= 32 && byte <= 126) {
        return Key{ char: byte };
    }
    return switch (byte) {
        1 => .ctrl_a,
        2 => .ctrl_b,
        3 => .ctrl_c,
        4 => .ctrl_d,
        5 => .ctrl_e,
        6 => .ctrl_f,
        11 => .ctrl_k,
        12 => .ctrl_l,
        14 => .ctrl_n,
        16 => .ctrl_p,
        18 => .ctrl_r,
        20 => .ctrl_t,
        21 => .ctrl_u,
        23 => .ctrl_w,
        8, 127 => .backspace,
        9 => .tab,
        10, 13 => .enter,
        27 => .escape,
        else => .unknown,
    };
}

// ============================================================================
// History
// ============================================================================

struct History {
    entries: ArrayList(string),
    position: usize = 0,
    temp_line: string? = null,
    file_path: string? = null,
    allocator: Allocator,
    modified: bool = false,

    static fn init(allocator: Allocator): History {
        return {
            entries: ArrayList(string).init(allocator),
            allocator: allocator,
        };
    }

    fn deinit(*_) {
        for (entry of _.entries.items) {
            _.allocator.free(entry);
        }
        _.entries.deinit();
        if (line := _.temp_line) {
            _.allocator.free(line);
        }
        if (path := _.file_path) {
            _.allocator.free(path);
        }
    }

    fn load(*_)! {
        const home_path_opt = bun.env_var.HOME.get();
        if (home_path_opt == null) return;
        const home_path = home_path_opt.?;
        if (home_path.len == 0) return;

        let path_buf: bun.PathBuffer = undefined;
        const path_parts: [2]string = [ home_path, HISTORY_FILENAME ];
        const path = bun.path.joinZBuf(&path_buf, &path_parts, .auto);
        _.file_path = _.allocator.dupe(u8, path)?;

        const read_res = bun.sys.File.readFrom(bun.FD.cwd(), path, _.allocator);
        if (read_res == .err) return;
        const content = read_res.result;
        defer _.allocator.free(content);

        let lines = std.mem.splitScalar(u8, content, '\n');
        while (line := lines.next()) {
            if (line.len > 0) {
                const entry = _.allocator.dupe(u8, line)?;
                _.entries.append(entry)?;
            }
        }

        // Trim to max size
        while (_.entries.items.len > MAX_HISTORY_SIZE) {
            const old = _.entries.orderedRemove(0);
            _.allocator.free(old);
        }

        _.position = _.entries.items.len;
    }

    fn save(*_) {
        if (!_.modified) return;
        if (_.file_path == null) return;
        const path = _.file_path.?;

        // Build content
        const start = if (_.entries.items.len > MAX_HISTORY_SIZE)
            _.entries.items.len - MAX_HISTORY_SIZE
        else
            0;

        let content = ArrayList(u8).init(_.allocator);
        defer content.deinit();
        for (entry of _.entries.items[start..]) {
            content.appendSlice(entry) catch return;
            content.append('\n') catch return;
        }

        const open_res = bun.sys.openA(path, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o644);
        if (open_res == .err) return;
        const file = bun.sys.File{ handle: open_res.result };
        defer file.close();
        if (file.writeAll(content.items) == .err) return;

        _.modified = false;
    }

    fn add(*_, line: []const u8)! {
        if (line.len == 0) return;

        // Don't add duplicates of the last entry
        if (_.entries.items.len > 0) {
            const last = _.entries.items[_.entries.items.len - 1];
            if (strings.eqlLong(last, line, true)) {
                _.position = _.entries.items.len;
                return;
            }
        }

        const entry = _.allocator.dupe(u8, line)?;
        _.entries.append(entry)?;
        _.position = _.entries.items.len;
        _.modified = true;

        // Trim if too large
        while (_.entries.items.len > MAX_HISTORY_SIZE) {
            const old = _.entries.orderedRemove(0);
            _.allocator.free(old);
            _.position -|= 1;
        }
    }

    fn prev(*_, current_line: []const u8): ([]const u8)? {
        if (_.entries.items.len == 0) return null;

        // Save current line if at the end
        if (_.position == _.entries.items.len) {
            if (old := _.temp_line) {
                _.allocator.free(old);
            }
            _.temp_line = _.allocator.dupe(u8, current_line) catch null;
        }

        if (_.position > 0) {
            _.position -= 1;
            return _.entries.items[_.position];
        }

        return null;
    }

    fn next(*_): ([]const u8)? {
        if (_.position < _.entries.items.len) {
            _.position += 1;
        }

        if (_.position == _.entries.items.len) {
            // Keep ownership in History; resetPosition() frees temp_line.
            // Caller copies the data via set(), so borrowed reference is safe.
            return _.temp_line;
        }

        if (_.position < _.entries.items.len) {
            return _.entries.items[_.position];
        }

        return null;
    }

    fn resetPosition(*_) {
        _.position = _.entries.items.len;
        if (line := _.temp_line) {
            _.allocator.free(line);
            _.temp_line = null;
        }
    }
}

// ============================================================================
// Line Editor
// ============================================================================

struct LineEditor {
    buffer: ArrayList(u8),
    cursor: usize = 0,
    allocator: Allocator,

    static fn init(allocator: Allocator): LineEditor {
        return {
            buffer: ArrayList(u8).init(allocator),
            allocator: allocator,
        };
    }

    fn deinit(*_) {
        _.buffer.deinit();
    }

    fn clear(*_) {
        _.buffer.clearRetainingCapacity();
        _.cursor = 0;
    }

    fn set(*_, text: []const u8)! {
        _.buffer.clearRetainingCapacity();
        _.buffer.appendSlice(text)?;
        _.cursor = text.len;
    }

    fn insert(*_, char: u8)! {
        if (_.cursor == _.buffer.items.len) {
            _.buffer.append(char)?;
        } else {
            _.buffer.insert(_.cursor, char)?;
        }
        _.cursor += 1;
    }

    fn insertSlice(*_, slice: []const u8)! {
        if (_.cursor == _.buffer.items.len) {
            _.buffer.appendSlice(slice)?;
        } else {
            _.buffer.insertSlice(_.cursor, slice)?;
        }
        _.cursor += slice.len;
    }

    fn deleteChar(*_) {
        if (_.cursor < _.buffer.items.len) {
            * = _.buffer.orderedRemove(_.cursor);
        }
    }

    fn backspace(*_) {
        if (_.cursor > 0) {
            _.cursor -= 1;
            * = _.buffer.orderedRemove(_.cursor);
        }
    }

    fn deleteWord(*_) {
        // Delete word forward
        while (_.cursor < _.buffer.items.len &&
            std.ascii.isWhitespace(_.buffer.items[_.cursor]))
        {
            * = _.buffer.orderedRemove(_.cursor);
        }
        while (_.cursor < _.buffer.items.len &&
            !std.ascii.isWhitespace(_.buffer.items[_.cursor]))
        {
            * = _.buffer.orderedRemove(_.cursor);
        }
    }

    fn backspaceWord(*_) {
        // Delete word backward
        while (_.cursor > 0 &&
            std.ascii.isWhitespace(_.buffer.items[_.cursor - 1]))
        {
            _.cursor -= 1;
            * = _.buffer.orderedRemove(_.cursor);
        }
        while (_.cursor > 0 &&
            !std.ascii.isWhitespace(_.buffer.items[_.cursor - 1]))
        {
            _.cursor -= 1;
            * = _.buffer.orderedRemove(_.cursor);
        }
    }

    fn deleteToEnd(*_) {
        _.buffer.shrinkRetainingCapacity(_.cursor);
    }

    fn deleteToStart(*_) {
        if (_.cursor > 0) {
            std.mem.copyForwards(u8, _.buffer.items[0..], _.buffer.items[_.cursor..]);
            _.buffer.shrinkRetainingCapacity(_.buffer.items.len - _.cursor);
            _.cursor = 0;
        }
    }

    fn moveLeft(*_) {
        if (_.cursor > 0) {
            _.cursor -= 1;
        }
    }

    fn moveRight(*_) {
        if (_.cursor < _.buffer.items.len) {
            _.cursor += 1;
        }
    }

    fn moveWordLeft(*_) {
        while (_.cursor > 0 &&
            std.ascii.isWhitespace(_.buffer.items[_.cursor - 1]))
        {
            _.cursor -= 1;
        }
        while (_.cursor > 0 &&
            !std.ascii.isWhitespace(_.buffer.items[_.cursor - 1]))
        {
            _.cursor -= 1;
        }
    }

    fn moveWordRight(*_) {
        while (_.cursor < _.buffer.items.len &&
            !std.ascii.isWhitespace(_.buffer.items[_.cursor]))
        {
            _.cursor += 1;
        }
        while (_.cursor < _.buffer.items.len &&
            std.ascii.isWhitespace(_.buffer.items[_.cursor]))
        {
            _.cursor += 1;
        }
    }

    fn moveToStart(*_) {
        _.cursor = 0;
    }

    fn moveToEnd(*_) {
        _.cursor = _.buffer.items.len;
    }

    fn swap(*_) {
        if (_.cursor > 0 && _.cursor < _.buffer.items.len) {
            const temp = _.buffer.items[_.cursor - 1];
            _.buffer.items[_.cursor - 1] = _.buffer.items[_.cursor];
            _.buffer.items[_.cursor] = temp;
            _.cursor += 1;
        } else if (_.cursor > 1 && _.cursor == _.buffer.items.len) {
            const temp = _.buffer.items[_.cursor - 2];
            _.buffer.items[_.cursor - 2] = _.buffer.items[_.cursor - 1];
            _.buffer.items[_.cursor - 1] = temp;
        }
    }

    fn getLine(): []const u8 {
        return _.buffer.items;
    }
}

// ============================================================================
// REPL Commands
// ============================================================================

const repl_commands: [9]ReplCommand = [
    ReplCommand{ name: ".help", help: "Print this help message", handler: cmdHelp },
    ReplCommand{ name: ".exit", help: "Exit the REPL", handler: cmdExit },
    ReplCommand{ name: ".clear", help: "Clear the screen", handler: cmdClear },
    ReplCommand{ name: ".copy", help: "Copy result to clipboard (.copy [expr])", handler: cmdCopy },
    ReplCommand{ name: ".load", help: "Load a file into the REPL session", handler: cmdLoad },
    ReplCommand{ name: ".save", help: "Save REPL history to a file", handler: cmdSave },
    ReplCommand{ name: ".editor", help: "Enter multi-line editor mode", handler: cmdEditor },
    ReplCommand{ name: ".break", help: "Cancel current input", handler: cmdBreak },
    ReplCommand{ name: ".history", help: "Show command history", handler: cmdHistory },
];

struct ReplCommand {
    name: []const u8,
    help: []const u8,
    handler: *const fn (*Repl, []const u8) ReplResult,

    const all = repl_commands;

    static fn find(name: []const u8): (*const ReplCommand)? {
        for (i of 0..all.len) {
            const cmd = &all[i];
            if (strings.eqlLong(cmd.name, name, true) ||
                (name.len > 1 && strings.startsWith(cmd.name, name)))
            {
                return cmd;
            }
        }
        return null;
    }
}

enum ReplResult {
    continue_repl,
    exit_repl,
    skip_eval,
}

fn cmdHelp(repl: *Repl, _: []const u8): ReplResult {
    repl.print("\n{s}REPL Commands:{s}\n", .{ Color.bold, Color.reset });
    for (cmd of ReplCommand.all) {
        repl.print("  {s}{s:<12}{s} {s}\n", .{ Color.cyan, cmd.name, Color.reset, cmd.help });
    }
    repl.print("\n{s}Keybindings:{s}\n", .{ Color.bold, Color.reset });
    repl.print("  {s}Ctrl+A{s}       Move to start of line\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Ctrl+E{s}       Move to end of line\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Ctrl+B/F{s}     Move backward/forward one character\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Alt+B/F{s}      Move backward/forward one word\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Ctrl+U{s}       Delete to start of line\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Ctrl+K{s}       Delete to end of line\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Ctrl+W{s}       Delete word backward\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Ctrl+D{s}       Delete character / Exit if line empty\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Ctrl+L{s}       Clear screen\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Ctrl+T{s}       Swap characters\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Up/Down{s}      Navigate history\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}Tab{s}          Auto-complete\n", .{ Color.cyan, Color.reset });
    repl.print("\n{s}Special Variables:{s}\n", .{ Color.bold, Color.reset });
    repl.print("  {s}_{s}            Last expression result\n", .{ Color.cyan, Color.reset });
    repl.print("  {s}_error{s}       Last error\n", .{ Color.cyan, Color.reset });
    repl.print("\n", {});
    return .skip_eval;
}

fn cmdCopy(repl: *Repl, args: []const u8): ReplResult {
    const code = strings.trim(args, " \t");

    if (code.len == 0) {
        // .copy with no args - copy _ (last result) to clipboard
        repl.copyValueToClipboard(repl.last_result) catch |e| {
            if (global := repl.global) {
                const exc = global.takeException(e);
                repl.setLastError(exc);
                repl.printJSError(exc);
            }
        };
        return .skip_eval;
    }

    // .copy <code> - evaluate and copy result to clipboard instead of printing
    repl.evaluateAndCopy(code);
    return .skip_eval;
}

fn cmdExit(_: *Repl, _: []const u8): ReplResult {
    return .exit_repl;
}

fn cmdClear(repl: *Repl, _: []const u8): ReplResult {
    // Clear screen
    repl.write(Cursor.clear_screen);
    repl.write(Cursor.clear_scrollback);
    repl.write(Cursor.home);
    return .skip_eval;
}

fn cmdLoad(repl: *Repl, args: []const u8): ReplResult {
    const filename = strings.trim(args, " \t");
    if (filename.len == 0) {
        repl.printError("Usage: .load <filename>\n", {});
        return .skip_eval;
    }

    let path_buf: bun.PathBuffer = undefined;
    const pathZ = bun.path.z(filename, &path_buf);
    const read_res = bun.sys.File.readFrom(bun.FD.cwd(), pathZ, repl.allocator);
    if (read_res == .err) {
        repl.printError("{f}\n", .{read_res.err});
        return .skip_eval;
    }
    const content = read_res.result;
    defer repl.allocator.free(content);

    repl.print("{s}Loading {s}...{s}\n", .{ Color.dim, filename, Color.reset });
    repl.evaluateAndPrint(content);
    return .skip_eval;
}

fn cmdSave(repl: *Repl, args: []const u8): ReplResult {
    const filename = strings.trim(args, " \t");
    if (filename.len == 0) {
        repl.printError("Usage: .save <filename>\n", {});
        return .skip_eval;
    }

    // Build content
    let content = ArrayList(u8).init(repl.allocator);
    defer content.deinit();
    for (entry of repl.history.entries.items) {
        content.appendSlice(entry) catch return .skip_eval;
        content.append('\n') catch return .skip_eval;
    }

    const open_res = bun.sys.openA(filename, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o644);
    if (open_res == .err) {
        repl.printError("{f}\n", .{open_res.err});
        return .skip_eval;
    }
    const file = bun.sys.File{ handle: open_res.result };
    defer file.close();
    const write_res = file.writeAll(content.items);
    if (write_res == .err) {
        repl.printError("{f}\n", .{write_res.err});
        return .skip_eval;
    }

    repl.print("{s}Session saved to {s}{s}\n", .{ Color.green, filename, Color.reset });
    return .skip_eval;
}

fn cmdEditor(repl: *Repl, _: []const u8): ReplResult {
    repl.print("{s}// Entering editor mode (Ctrl+D to finish, Ctrl+C to cancel){s}\n", .{ Color.dim, Color.reset });
    repl.editor_mode = true;
    repl.editor_buffer.clearRetainingCapacity();
    return .skip_eval;
}

fn cmdBreak(repl: *Repl, _: []const u8): ReplResult {
    repl.line_editor.clear();
    repl.multiline_buffer.clearRetainingCapacity();
    repl.in_multiline = false;
    return .skip_eval;
}

fn cmdHistory(repl: *Repl, _: []const u8): ReplResult {
    repl.print("\n{s}Command History:{s}\n", .{ Color.bold, Color.reset });
    const start = if (repl.history.entries.items.len > 20)
        repl.history.entries.items.len - 20
    else
        0;
    for (i of start..repl.history.entries.items.len) {
        const entry = repl.history.entries.items[i];
        repl.print("  {s}{d:>4}{s}  {s}\n", .{ Color.dim, i + 1, Color.reset, entry });
    }
    repl.print("\n", {});
    return .skip_eval;
}

// ============================================================================
// Main REPL Struct
// ============================================================================

/// Global pointer for signal handler to access the VM
let sigint_vm: (*jsc.VM)? = null;

@root struct Repl {
allocator: Allocator,
line_editor: LineEditor,
history: History,
multiline_buffer: ArrayList(u8),
editor_buffer: ArrayList(u8),

// State
in_multiline: bool = false,
editor_mode: bool = false,
running: bool = false,
is_tty: bool = false,
use_colors: bool = false,
terminal_width: u16 = 80,
terminal_height: u16 = 24,
ctrl_c_pressed: bool = false,

// Buffered stdin
stdin_buf: [256]u8 = .{0} ** 256,
stdin_buf_start: usize = 0,
stdin_buf_end: usize = 0,

// JavaScript VM
vm: (*jsc.VirtualMachine)? = null,
global: (*jsc.JSGlobalObject)? = null,

// Special REPL variables
last_result: jsc.JSValue = .js_undefined,
last_error: jsc.JSValue = .js_undefined,

// Windows: saved console mode for restoration
original_windows_mode: if (Environment.isWindows) bun.windows.DWORD? else void = if (Environment.isWindows) null else {},

static fn init(allocator: Allocator): Repl {
    return {
        allocator: allocator,
        line_editor: LineEditor.init(allocator),
        history: History.init(allocator),
        multiline_buffer: ArrayList(u8).init(allocator),
        editor_buffer: ArrayList(u8).init(allocator),
    };
}

fn deinit(*_) {
    _.restoreTerminal();
    _.history.save();
    _.line_editor.deinit();
    _.history.deinit();
    _.multiline_buffer.deinit();
    _.editor_buffer.deinit();
    if (!_.last_result.isUndefined()) _.last_result.unprotect();
    if (!_.last_error.isUndefined()) _.last_error.unprotect();
}

fn setLastResult(*_, value: jsc.JSValue) {
    if (!_.last_result.isUndefined()) _.last_result.unprotect();
    _.last_result = value;
    if (!value.isUndefined()) value.protect();
}

fn setLastError(*_, value: jsc.JSValue) {
    if (!_.last_error.isUndefined()) _.last_error.unprotect();
    _.last_error = value;
    if (!value.isUndefined()) value.protect();
}

// ============================================================================
// Terminal I/O
// ============================================================================

fn setupTerminal(*_) {
    _.is_tty = Output.isStdoutTTY() && Output.isStdinTTY();

    if (!_.is_tty) {
        _.use_colors = false;
        return;
    }

    // Check for NO_COLOR
    _.use_colors = !bun.env_var.NO_COLOR.get();

    // Get terminal size
    if (Output.terminal_size.col > 0) {
        _.terminal_width = Output.terminal_size.col;
        _.terminal_height = Output.terminal_size.row;
    }

    // Enable raw mode
    if (Environment.isPosix) {
        * = bun.tty.setMode(0, .raw);
    } else if (Environment.isWindows) {
        _.original_windows_mode = bun.windows.updateStdioModeFlags(.std_in, {
            set: bun.windows.ENABLE_VIRTUAL_TERMINAL_INPUT | bun.windows.ENABLE_PROCESSED_INPUT,
            unset: bun.windows.ENABLE_LINE_INPUT | bun.windows.ENABLE_ECHO_INPUT,
        }) catch null;
    }
}

fn restoreTerminal(*_) {
    if (Environment.isPosix) {
        * = bun.tty.setMode(0, .normal);
    } else if (Environment.isWindows) {
        if (mode := _.original_windows_mode) {
            * = bun.c.SetConsoleMode(bun.FD.stdin().native(), mode);
            _.original_windows_mode = null;
        }
    }
}

@callconv("c")
static fn sigintHandler(_: c_int) {
    if (vm := sigint_vm) {
        vm.setExecutionForbidden(true);
    }
}

/// Temporarily enable SIGINT delivery during blocking promise waits
fn enableSignalsDuringWait(*_) {
    if (vm := _.vm) {
        sigint_vm = vm.jsc_vm;
    }

    if (Environment.isPosix) {
        // Switch to normal terminal mode (has ISIG) so Ctrl+C generates SIGINT
        * = bun.tty.setMode(0, .normal);

        // Install SIGINT handler
        const act = bun.sys.Sigaction{
            handler: { handler: sigintHandler },
            mask: bun.sys.sigemptyset(),
            flags: 0,
        };
        bun.sys.sigaction(std.posix.SIG.INT, &act, null);
    }
    // On Windows, ENABLE_PROCESSED_INPUT is already set so Ctrl+C works
}

/// Restore raw terminal mode after promise wait
fn disableSignalsDuringWait(*_) {
    if (_.running) {}
    sigint_vm = null;

    if (Environment.isPosix) {
        // Back to raw mode
        * = bun.tty.setMode(0, .raw);

        // Restore default SIGINT handling
        const act = bun.sys.Sigaction{
            handler: { handler: std.posix.SIG.DFL },
            mask: bun.sys.sigemptyset(),
            flags: 0,
        };
        bun.sys.sigaction(std.posix.SIG.INT, &act, null);
    }
}

fn write(*_, data: []const u8) {
    Output.writer().writeAll(data) catch {};
}

fn print(*_, comptime format: []const u8, args: auto) {
    Output.writer().print(format, args) catch {};
}

fn printError(*_, comptime format: []const u8, args: auto) {
    if (_.use_colors) {
        Output.writer().print(Color.red ++ format ++ Color.reset, args) catch {};
    } else {
        Output.writer().print(format, args) catch {};
    }
}

fn readByte(*_): u8? {
    if (_.stdin_buf_start < _.stdin_buf_end) {
        const b = _.stdin_buf[_.stdin_buf_start];
        _.stdin_buf_start += 1;
        return b;
    }
    // Refill buffer
    const stdin = bun.sys.File{ handle: bun.FD.stdin() };
    const read_res = stdin.read(&_.stdin_buf);
    if (read_res == .err) return null;
    const n = read_res.result;
    if (n == 0) return null;
    _.stdin_buf_start = 1;
    _.stdin_buf_end = n;
    return _.stdin_buf[0];
}

fn readKey(*_): Key? {
    const byte_opt = _.readByte();
    if (byte_opt == null) return null;
    const byte = byte_opt.?;

    // Handle escape sequences
    if (byte == 27) { // ESC
        const second_opt = _.readByte();
        if (second_opt == null) return .escape;
        const second = second_opt.?;

        if (second == '[') { // CSI
            const third_opt = _.readByte();
            if (third_opt == null) return .escape;
            const third = third_opt.?;

            return switch (third) {
                'A' => .arrow_up,
                'B' => .arrow_down,
                'C' => .arrow_right,
                'D' => .arrow_left,
                'H' => .home,
                'F' => .end,
                '1'..'6' => blk: {
                    const fourth_opt = _.readByte();
                    if (fourth_opt == null) break :blk .unknown;
                    const fourth = fourth_opt.?;
                    if (fourth == '~') {
                        break :blk switch (third) {
                            '1' => .home,
                            '2' => .unknown, // insert
                            '3' => .delete,
                            '4' => .end,
                            '5' => .page_up,
                            '6' => .page_down,
                            else => .unknown,
                        };
                    } else if (fourth == ';') {
                        const mod_opt = _.readByte();
                        if (mod_opt == null) break :blk .unknown;
                        const mod = mod_opt.?;
                        const dir_opt = _.readByte();
                        if (dir_opt == null) break :blk .unknown;
                        const dir = dir_opt.?;
                        if (mod == '5' || mod == '3') {
                            break :blk switch (dir) {
                                'C' => .alt_right,
                                'D' => .alt_left,
                                else => .unknown,
                            };
                        }
                        break :blk .unknown;
                    }
                    break :blk .unknown;
                },
                else => .unknown,
            };
        } else if (second == 'O') { // SS3
            const third_opt = _.readByte();
            if (third_opt == null) return .escape;
            const third = third_opt.?;
            return switch (third) {
                'H' => .home,
                'F' => .end,
                else => .unknown,
            };
        } else if (second == 'b') {
            return .alt_b;
        } else if (second == 'd') {
            return .alt_d;
        } else if (second == 'f') {
            return .alt_f;
        } else if (second == 127) {
            return .alt_backspace;
        }

        return .escape;
    }

    return Key.fromByte(byte);
}

// ============================================================================
// Prompt and Display
// ============================================================================

fn getPrompt(*_): []const u8 {
    if (_.in_multiline || _.editor_mode) {
        if (_.use_colors) {
            return Color.dim ++ "... " ++ Color.reset;
        } else {
            return "... ";
        }
    }

    if (_.use_colors) {
        return Color.dim ++ "\xe2\x9d\xaf" ++ Color.reset ++ " ";
    } else {
        return "> ";
    }
}

fn getPromptLength(*_): usize {
    if (_.in_multiline || _.editor_mode) {
        return 4; // "... "
    }
    return 2; // "> " or "\u{276f} "
}

fn refreshLine(*_) {
    // Flush any buffered output (e.g., from console.log in JS) before drawing prompt
    Output.flush();

    const prompt = _.getPrompt();
    const prompt_len = _.getPromptLength();
    const line = _.line_editor.getLine();

    // Move to beginning of line
    _.write("\r");
    _.write(Cursor.clear_line);

    // Write prompt
    _.write(prompt);

    // Write line with syntax highlighting
    if (_.use_colors && line.len > 0 && line.len <= 2048) {
        _.writeHighlighted(line);
    } else {
        _.write(line);
    }

    // Position cursor
    const cursor_pos = prompt_len + _.line_editor.cursor;
    if (cursor_pos < _.terminal_width) {
        _.write("\r");
        if (cursor_pos > 0) {
            let buf: [16]u8 = undefined;
            const seq = std.fmt.bufPrint(&buf, CSI ++ "{d}C", .{cursor_pos}) catch return;
            _.write(seq);
        }
    }

    Output.flush();
}

fn writeHighlighted(*_, text: []const u8) {
    let writer = Output.writer();
    const highlighter = fmt.QuickAndDirtyJavaScriptSyntaxHighlighter{
        text: text,
        opts: {
            enable_colors: true,
            check_for_unhighlighted_write: false,
        },
    };
    highlighter.format(writer) catch {
        writer.writeAll(text) catch {};
    };
}

// ============================================================================
// Code Completion
// ============================================================================

static fn isIncompleteCode(code: []const u8): bool {
    let brace_count: i32 = 0;
    let bracket_count: i32 = 0;
    let paren_count: i32 = 0;
    let in_string: u8 = 0;
    let in_template = false;
    let escaped = false;

    for (char of code) {
        if (escaped) {
            escaped = false;
            continue;
        }

        if (char == '\\') {
            escaped = true;
            continue;
        }

        // Handle strings
        if (in_string == 0 && !in_template) {
            if (char == '"' || char == '\'') {
                in_string = char;
                continue;
            }
            if (char == '`') {
                in_template = true;
                continue;
            }
        } else if (in_string != 0 && char == in_string) {
            in_string = 0;
            continue;
        } else if (in_template && char == '`') {
            in_template = false;
            continue;
        }

        // Skip content inside strings
        if (in_string != 0 || in_template) continue;

        // Count brackets
        switch (char) {
            '{' => brace_count += 1,
            '}' => brace_count -= 1,
            '[' => bracket_count += 1,
            ']' => bracket_count -= 1,
            '(' => paren_count += 1,
            ')' => paren_count -= 1,
            else => {},
        }
    }

    // Incomplete if any unclosed delimiters or unclosed strings
    return in_string != 0 || in_template || brace_count > 0 || bracket_count > 0 || paren_count > 0;
}

// ============================================================================
// JavaScript Evaluation
// ============================================================================

fn evaluateAndPrint(*_, code: []const u8) {
    if (_.global == null) return;
    const global = _.global.?;
    if (_.vm == null) return;
    const vm = _.vm.?;

    // Transform the code using REPL mode (hoists declarations, wraps result in { value: expr })
    const transformed_opt = _.transformForRepl(code);
    if (transformed_opt == null) {
        // Transform failed, try evaluating raw code (for syntax errors, etc.)
        _.evaluateRaw(code);
        return;
    }
    const transformed_code = transformed_opt.?;
    defer _.allocator.free(transformed_code);

    // Evaluate the transformed code
    let exception: jsc.JSValue = .js_undefined;
    let resolved_result = Bun__REPL__evaluate(
        global,
        transformed_code.ptr,
        transformed_code.len,
        "[repl]".ptr,
        "[repl]".len,
        &exception,
    );

    // Check for exception
    if (!exception.isUndefined() && !exception.isNull()) {
        _.setLastError(exception);
        _.printJSError(exception);
        return;
    }

    // Handle async IIFE results - wait for promise to resolve
    if (promise := resolved_result.asPromise()) {
        // Mark as handled BEFORE waiting to prevent unhandled rejection output
        promise.setHandled();

        // Temporarily re-enable signal delivery so Ctrl+C can interrupt
        // the blocking waitForPromise call
        _.enableSignalsDuringWait();
        defer _.disableSignalsDuringWait();

        // Wait for the promise to settle
        vm.waitForPromise({ normal: promise });

        // If execution was forbidden by SIGINT, clear it and report
        if (vm.jsc_vm.executionForbidden()) {
            vm.jsc_vm.setExecutionForbidden(false);
            global.clearTerminationException();
            _.print("\n", {});
            return;
        }

        // Check promise status after waiting
        switch (promise.status()) {
            .fulfilled => {
                resolved_result = promise.result(vm.jsc_vm);
            },
            .rejected => {
                const rejection = promise.result(vm.jsc_vm);
                _.setLastError(rejection);
                // Set _error on the global object
                const global_this = global.toJSValue();
                global_this.put(global, "_error", rejection);
                _.printJSError(rejection);
                return;
            },
            .pending => {
                // Interrupted by signal or timed out
                _.print("\n", {});
                return;
            },
        }
    }

    // Extract the value from the result wrapper { value: expr }
    // The REPL transform wraps the last expression in { value: expr }
    if (resolved_result.isObject()) {
        // Wrapper is REPL-built { __proto__: null, value: ... } so getOwn shouldn't throw,
        // but if it does, propagate as a REPL error.
        const maybe_value = resolved_result.getOwn(global, "value") catch |e| {
            const exc = global.takeException(e);
            _.setLastError(exc);
            _.printJSError(exc);
            vm.tick();
            return;
        };
        if (value := maybe_value) {
            resolved_result = value;
        }
    }

    // Store and print result
    _.setLastResult(resolved_result);

    // Set _ to the last result (only if not undefined)
    // Use the global object as JSValue and put the property on it
    if (!resolved_result.isUndefined()) {
        const global_this = global.toJSValue();
        global_this.put(global, "_", resolved_result);
    }

    if (resolved_result.isUndefined()) {
        if (_.use_colors) {
            _.print("{s}undefined{s}\n", .{ Color.dim, Color.reset });
        } else {
            _.print("undefined\n", {});
        }
    } else {
        _.printFormattedValue(resolved_result);
    }

    // Tick the event loop to handle any pending work
    vm.tick();
}

/// Evaluate a script from `bun repl -e/--eval` or `-p/--print` non-interactively.
/// Uses the REPL transform pipeline (TypeScript/JSX, top-level await, object literal
/// wrapping, declaration hoisting), drains the event loop, and optionally prints the
/// result to stdout. Errors are written to stderr.
/// Returns true if an error occurred (the caller should set exit_code=1 and
/// skip onBeforeExit); false on success (caller preserves process.exitCode).
fn evalScript(*_, code: []const u8, print_result: bool): bool {
    if (_.global == null) return true;
    const global = _.global.?;
    if (_.vm == null) return true;
    const vm = _.vm.?;

    const no_color = bun.env_var.NO_COLOR.get();
    _.use_colors = Output.enable_ansi_colors_stdout && !no_color;
    const stderr_colors = Output.enable_ansi_colors_stderr && !no_color;

    // Empty / whitespace-only script: nothing to do (matches `node -e ""`)
    if (strings.trim(code, " \t\n\r").len == 0) {
        if (print_result) {
            if (_.use_colors) {
                _.print("{s}undefined{s}\n", .{ Color.dim, Color.reset });
            } else {
                _.print("undefined\n", {});
            }
        }
        return false;
    }

    const transformed_opt = _.transformForRepl(code);
    if (transformed_opt == null) {
        // Transform failed — fall back to raw evaluation for the error message
        let exception: jsc.JSValue = .js_undefined;
        * = Bun__REPL__evaluate(global, code.ptr, code.len, "[eval]".ptr, "[eval]".len, &exception);
        if (!exception.isUndefined() && !exception.isNull()) {
            _.printJSErrorTo(exception, Output.errorWriter(), stderr_colors);
        }
        return true;
    }
    const transformed_code = transformed_opt.?;
    defer _.allocator.free(transformed_code);

    let exception: jsc.JSValue = .js_undefined;
    let resolved_result = Bun__REPL__evaluate(
        global,
        transformed_code.ptr,
        transformed_code.len,
        "[eval]".ptr,
        "[eval]".len,
        &exception,
    );

    if (!exception.isUndefined() && !exception.isNull()) {
        _.printJSErrorTo(exception, Output.errorWriter(), stderr_colors);
        return true;
    }

    // If the transform wrapped in an async IIFE (top-level await), wait for it
    if (promise := resolved_result.asPromise()) {
        promise.setHandled();
        vm.waitForPromise({ normal: promise });
        switch (promise.status()) {
            .fulfilled => resolved_result = promise.result(vm.jsc_vm),
            .rejected => {
                const rejection = promise.result(vm.jsc_vm);
                _.printJSErrorTo(rejection, Output.errorWriter(), stderr_colors);
                return true;
            },
            .pending => return true,
        }
    }

    // Unwrap the { value: expr } wrapper produced by transformForRepl
    if (resolved_result.isObject()) {
        const maybe_value = resolved_result.getOwn(global, "value") catch |e| {
            const exc = global.takeException(e);
            _.printJSErrorTo(exc, Output.errorWriter(), stderr_colors);
            return true;
        };
        if (value := maybe_value) resolved_result = value;
    }
    // Protect across tick() in case of GC
    if (!resolved_result.isUndefined()) resolved_result.protect();
    defer {
        if (!resolved_result.isUndefined()) resolved_result.unprotect();
    }

    // Drain the event loop (timers, I/O, etc.) before printing / exiting
    vm.tick();
    while (vm.isEventLoopAlive()) {
        vm.tick();
        vm.eventLoop().autoTickActive();
    }

    if (print_result) {
        if (resolved_result.isUndefined()) {
            if (_.use_colors) {
                _.print("{s}undefined{s}\n", .{ Color.dim, Color.reset });
            } else {
                _.print("undefined\n", {});
            }
        } else {
            _.printFormattedValue(resolved_result);
        }
    }

    return false;
}

/// Evaluate code without REPL transforms (fallback for errors)
/// The C++ Bun__REPL__evaluate handles setting _ and _error
fn evaluateRaw(*_, code: []const u8) {
    if (_.global == null) return;
    const global = _.global.?;

    let exception: jsc.JSValue = .js_undefined;
    const result = Bun__REPL__evaluate(
        global,
        code.ptr,
        code.len,
        "[repl]".ptr,
        "[repl]".len,
        &exception,
    );

    if (!exception.isUndefined() && !exception.isNull()) {
        _.setLastError(exception);
        _.printJSError(exception);
        return;
    }

    _.setLastResult(result);

    if (!result.isUndefined()) {
        _.printFormattedValue(result);
    } else {
        if (_.use_colors) {
            _.print("{s}undefined{s}\n", .{ Color.dim, Color.reset });
        } else {
            _.print("undefined\n", {});
        }
    }

    if (vm := _.vm) {
        vm.tick();
    }
}

/// Evaluate code and copy the result to clipboard instead of printing it
fn evaluateAndCopy(*_, code: []const u8) {
    if (_.global == null) return;
    const global = _.global.?;
    if (_.vm == null) return;
    const vm = _.vm.?;

    const transformed_opt = _.transformForRepl(code);
    if (transformed_opt == null) {
        _.evaluateRaw(code);
        return;
    }
    const transformed_code = transformed_opt.?;
    defer _.allocator.free(transformed_code);

    let exception: jsc.JSValue = .js_undefined;
    let resolved_result = Bun__REPL__evaluate(
        global,
        transformed_code.ptr,
        transformed_code.len,
        "[repl]".ptr,
        "[repl]".len,
        &exception,
    );

    if (!exception.isUndefined() && !exception.isNull()) {
        _.setLastError(exception);
        _.printJSError(exception);
        return;
    }

    if (promise := resolved_result.asPromise()) {
        promise.setHandled();
        _.enableSignalsDuringWait();
        defer _.disableSignalsDuringWait();
        vm.waitForPromise({ normal: promise });
        if (vm.jsc_vm.executionForbidden()) {
            vm.jsc_vm.setExecutionForbidden(false);
            global.clearTerminationException();
            _.print("\n", {});
            return;
        }
        switch (promise.status()) {
            .fulfilled => resolved_result = promise.result(vm.jsc_vm),
            .rejected => {
                const rejection = promise.result(vm.jsc_vm);
                _.setLastError(rejection);
                _.printJSError(rejection);
                return;
            },
            .pending => { return; },
        }
    }

    if (resolved_result.isObject()) {
        const maybe_value = resolved_result.getOwn(global, "value") catch |e| {
            const exc = global.takeException(e);
            _.setLastError(exc);
            _.printJSError(exc);
            vm.tick();
            return;
        };
        if (value := maybe_value) {
            resolved_result = value;
        }
    }

    _.setLastResult(resolved_result);
    if (!resolved_result.isUndefined()) {
        const global_this = global.toJSValue();
        global_this.put(global, "_", resolved_result);
    }

    _.copyValueToClipboard(resolved_result) catch |e| {
        const exc = global.takeException(e);
        _.setLastError(exc);
        _.printJSError(exc);
    };
    vm.tick();
}

/// Format a JS value as a string suitable for clipboard.
/// Returns null on allocator OOM; propagates JS exceptions (e.g. throwing getters).
fn valueToClipboardString(*_, value: jsc.JSValue): bun.JSError!([]const u8)? {
    if (_.global == null) return null;
    const global = _.global.?;

    if (value.isUndefined()) return _.allocator.dupe(u8, "undefined") catch null;
    if (value.isNull()) return _.allocator.dupe(u8, "null") catch null;

    // For strings, copy the raw string value (not quoted/JSON-ified)
    if (value.isString()) {
        const slice = value.toSlice(global, _.allocator)?;
        defer slice.deinit();
        return _.allocator.dupe(u8, slice.slice()) catch null;
    }

    // For everything else, use Bun.inspect without colors
    let array = std.Io.Writer.Allocating.init(_.allocator);
    defer array.deinit();
    jsc.ConsoleObject.format2(.Log, global, @ptrCast(&value), 1, &array.writer, {
        enable_colors: false,
        add_newline: false,
        flush: false,
        quote_strings: true,
        ordered_properties: false,
        max_depth: 4,
    })?;
    array.writer.flush() catch return null;
    return _.allocator.dupe(u8, array.written()) catch null;
}

/// Copy a JS value to the system clipboard via OSC 52.
/// Propagates JS exceptions from value formatting; swallows I/O errors.
fn copyValueToClipboard(*_, value: jsc.JSValue)! {
    const text_opt = _.valueToClipboardString(value)?;
    if (text_opt == null) {
        _.printError("Failed to format value for clipboard\n", {});
        return;
    }
    const text = text_opt.?;
    defer _.allocator.free(text);

    _.copyToClipboardOSC52(text) catch {
        _.printError("Failed to write to clipboard\n", {});
        return;
    };
    if (_.use_colors) {
        _.print("{s}Copied {d} characters to clipboard{s}\n", .{ Color.dim, text.len, Color.reset });
    } else {
        _.print("Copied {d} characters to clipboard\n", .{text.len});
    }
}

/// Write text to clipboard using OSC 52 escape sequence.
fn copyToClipboardOSC52(*_, text: []const u8)! {
    let it = strings.ANSIIterator.init(text);
    const first_opt = it.next();
    if (first_opt == null) return;
    const first = first_opt.?;

    if (first.len == text.len) {
        // No ANSI sequences - encode the original directly
        let encoded = bun.base64.encodeAlloc(_.allocator, text)?;
        defer encoded.deinit(_.allocator);
        _.write("\x1b]52;c;");
        _.write(encoded.slice());
        _.write("\x07");
    } else {
        // Has ANSI sequences - collect clean slices then encode
        let clean = ArrayList(u8).init(_.allocator);
        defer clean.deinit();
        clean.ensureTotalCapacity(text.len)?;
        clean.appendSliceAssumeCapacity(first);
        while (slice := it.next()) {
            clean.appendSliceAssumeCapacity(slice);
        }
        let encoded = bun.base64.encodeAlloc(_.allocator, clean.items)?;
        defer encoded.deinit(_.allocator);
        _.write("\x1b]52;c;");
        _.write(encoded.slice());
        _.write("\x07");
    }
}

/// Transform code using the REPL parser (hoists declarations, wraps expressions)
fn transformForRepl(*_, code: []const u8): ([]const u8)? {
    if (_.vm == null) return null;
    const vm = _.vm.?;

    // Skip empty code
    if (code.len == 0 || strings.trim(code, " \t\n\r").len == 0) {
        return null;
    }

    // Check if code looks like an object literal that would be misinterpreted as a block
    // If code starts with { (after whitespace) and doesn't end with ;
    const is_object_literal = isLikelyObjectLiteral(code);
    const processed_code = if (is_object_literal)
        std.fmt.allocPrint(_.allocator, "({s})", .{code}) catch return null
    else
        code;
    defer {
        if (is_object_literal) _.allocator.free(processed_code);
    }

    // Create arena for parsing
    let arena_ = MimallocArena.init();
    defer arena_.deinit();
    const allocator = arena_.allocator();

    // Set up parser options with repl_mode enabled
    let opts = js_parser.Parser.Options.init(vm.transpiler.options.jsx, .tsx);
    opts.repl_mode = true;
    opts.features.dead_code_elimination = false; // REPL needs all code
    opts.features.top_level_await = true; // Enable top-level await in REPL
    // Keep `lower_using` at its default (true) here even though JavaScriptCore
    // supports `using` / `await using` natively. The REPL transform in
    // `ast/repl_transforms.zig` rewrites every top-level `s_local` into a
    // hoisted `var` + assignment for cross-input persistence, which would
    // silently discard disposal semantics if `using` declarations survived
    // until that pass. Lowering wraps the declaration in `try/finally` first,
    // which the REPL transform passes through intact.

    // Initialize macro context from transpiler (required for import processing)
    if (vm.transpiler.macro_context == null) {
        vm.transpiler.macro_context = bun.ast.Macro.MacroContext.init(&vm.transpiler);
    }
    opts.macro_context = &vm.transpiler.macro_context.?;

    // Create log for errors
    let log = logger.Log.init(arena_.backingAllocator());
    defer log.deinit();

    // Create source
    const source = logger.Source.initPathString("[repl]", processed_code);

    // Parse with REPL transforms
    let parser = js_parser.Parser.init(
        opts,
        &log,
        &source,
        vm.transpiler.options.define,
        allocator,
    ) catch return null;

    const parse_result = parser.parse() catch return null;
    if (parse_result != .ast) return null;
    const ast = parse_result.ast;
    // Don't call ast.deinit() - the arena handles cleanup

    // Check for parse errors
    if (log.errors > 0) return null;
    // Print the transformed AST back to JavaScript
    const buffer_writer = js_printer.BufferWriter.init(_.allocator);
    let buffer_printer = js_printer.BufferPrinter.init(buffer_writer);
    defer buffer_printer.ctx.buffer.deinit();

    // Create symbol map from ast.symbols
    const symbols_nested = bun.ast.Symbol.NestedList.fromBorrowedSliceDangerous(&.{ast.symbols});
    const symbols_map = bun.ast.Symbol.Map.initList(symbols_nested);

    * = js_printer.printAst(
        @TypeOf(&buffer_printer),
        &buffer_printer,
        ast,
        symbols_map,
        &source,
        true, // ascii_only
        { mangled_props: null },
        false, // generate_source_map
    ) catch return null;

    // Get the written buffer
    const written = buffer_printer.ctx.getWritten();
    return _.allocator.dupe(u8, written) catch null;
}

/// Check if code looks like an object literal that would be misinterpreted as a block
static fn isLikelyObjectLiteral(code: []const u8): bool {
    // Skip leading whitespace
    let start: usize = 0;
    while (start < code.len && (code[start] == ' ' || code[start] == '\t' || code[start] == '\n' || code[start] == '\r')) {
        start += 1;
    }

    // Check if starts with {
    if (start >= code.len || code[start] != '{') {
        return false;
    }

    // Skip trailing whitespace
    let end: usize = code.len;
    while (end > 0 && (code[end - 1] == ' ' || code[end - 1] == '\t' || code[end - 1] == '\n' || code[end - 1] == '\r')) {
        end -= 1;
    }

    // Check if ends with semicolon - if so, it's likely a block statement
    if (end > 0 && code[end - 1] == ';') {
        return false;
    }

    return true;
}

fn setReplVariables(*_) {
    // For now, we rely on the C++ evaluation to handle this
    // The C++ code sets _ and _error after each evaluation
    if (_.running) {}
}

fn printJSError(*_, error_value: jsc.JSValue) {
    // Interactive REPL writes everything to stdout (single terminal stream).
    _.printJSErrorTo(error_value, Output.writer(), _.use_colors);
}

fn printJSErrorTo(*_, error_value: jsc.JSValue, writer: *std.Io.Writer, enable_colors: bool) {
    if (_.global == null) return;
    const global = _.global.?;
    // Use .Error level for proper error formatting with Bun.inspect
    jsc.ConsoleObject.format2(.Error, global, @ptrCast(&error_value), 1, writer, {
        enable_colors: enable_colors,
        add_newline: true,
        flush: false,
        quote_strings: true,
        ordered_properties: false,
        max_depth: 4,
    }) catch {
        // Formatting the error itself threw — clear it to avoid recursion and show a fallback.
        global.clearException();
        writer.writeAll("error: [failed to format error]\n") catch {};
    };
}

/// Format and print a JS value using Bun's console formatter (same as console.log)
fn printFormattedValue(*_, value: jsc.JSValue) {
    if (_.global == null) return;
    const global = _.global.?;
    const writer = Output.writer();
    jsc.ConsoleObject.format2(.Log, global, @ptrCast(&value), 1, writer, {
        enable_colors: _.use_colors,
        add_newline: true,
        flush: false,
        quote_strings: true,
        ordered_properties: false,
        max_depth: 4,
    }) catch |e| {
        // A getter on the value threw during inspection — show that error.
        const exc = global.takeException(e);
        _.setLastError(exc);
        _.printJSError(exc);
    };
}

// ============================================================================
// Main Loop
// ============================================================================

fn run(*_)! {
    _.runWithVM(null)?;
}

fn runWithVM(*_, vm: (*jsc.VirtualMachine)?)! {
    _.vm = vm;
    if (v := vm) {
        _.global = v.global;
    }

    _.setupTerminal();
    defer _.restoreTerminal();

    _.history.load()?;

    // Print welcome message
    _.print("Welcome to Bun v{s}\n", .{VERSION});
    _.print("Type {s}.copy [code]{s} to copy to clipboard. {s}.help{s} for more info.\n\n", .{ Color.cyan, Color.reset, Color.cyan, Color.reset });

    _.running = true;
    _.refreshLine();

    while (_.running) {
        const key_opt = _.readKey();
        if (key_opt == null) {
            // EOF
            _.print("\n", {});
            break;
        }
        const key = key_opt.?;

        // Reset double-Ctrl+C state on any other key
        if (key != .ctrl_c) _.ctrl_c_pressed = false;

        switch (key) {
            .enter => _.handleEnter()?,
            .ctrl_c => _.handleCtrlC(),
            .ctrl_d => {
                if (_.editor_mode) {
                    // Finish editor mode
                    _.print("\n", {});
                    const code = _.editor_buffer.items;
                    if (code.len > 0) {
                        _.evaluateAndPrint(code);
                    }
                    _.editor_mode = false;
                    _.editor_buffer.clearRetainingCapacity();
                    _.refreshLine();
                } else if (_.line_editor.buffer.items.len == 0 && !_.in_multiline) {
                    _.print("\n", {});
                    _.running = false;
                } else {
                    _.line_editor.deleteChar();
                    _.refreshLine();
                }
            },
            .ctrl_l => {
                _.write(Cursor.clear_screen);
                _.write(Cursor.home);
                _.refreshLine();
            },
            .ctrl_a => {
                _.line_editor.moveToStart();
                _.refreshLine();
            },
            .ctrl_e => {
                _.line_editor.moveToEnd();
                _.refreshLine();
            },
            .ctrl_b, .arrow_left => {
                _.line_editor.moveLeft();
                _.refreshLine();
            },
            .ctrl_f, .arrow_right => {
                _.line_editor.moveRight();
                _.refreshLine();
            },
            .alt_b, .alt_left => {
                _.line_editor.moveWordLeft();
                _.refreshLine();
            },
            .alt_f, .alt_right => {
                _.line_editor.moveWordRight();
                _.refreshLine();
            },
            .ctrl_u => {
                _.line_editor.deleteToStart();
                _.refreshLine();
            },
            .ctrl_k => {
                _.line_editor.deleteToEnd();
                _.refreshLine();
            },
            .ctrl_w, .alt_backspace => {
                _.line_editor.backspaceWord();
                _.refreshLine();
            },
            .alt_d => {
                _.line_editor.deleteWord();
                _.refreshLine();
            },
            .ctrl_t => {
                _.line_editor.swap();
                _.refreshLine();
            },
            .backspace => {
                _.line_editor.backspace();
                _.refreshLine();
            },
            .delete => {
                _.line_editor.deleteChar();
                _.refreshLine();
            },
            .arrow_up, .ctrl_p => {
                if (prev_line := _.history.prev(_.line_editor.getLine())) {
                    _.line_editor.set(prev_line) catch {};
                    _.refreshLine();
                }
            },
            .arrow_down, .ctrl_n => {
                if (next_line := _.history.next()) {
                    _.line_editor.set(next_line) catch {};
                } else {
                    _.line_editor.clear();
                }
                _.refreshLine();
            },
            .tab => _.handleTab(),
            .home => {
                _.line_editor.moveToStart();
                _.refreshLine();
            },
            .end => {
                _.line_editor.moveToEnd();
                _.refreshLine();
            },
            .char c => {
                _.line_editor.insert(c) catch {};
                _.refreshLine();
            },
            else => {},
        }
    }

    _.history.save();
}

fn handleEnter(*_)! {
    _.print("\n", {});

    const line = _.line_editor.getLine();

    if (_.editor_mode) {
        if (strings.trim(line, " \t").len == 0) {
            _.editor_buffer.appendSlice("\n")?;
        } else {
            _.editor_buffer.appendSlice(line)?;
            _.editor_buffer.append('\n')?;
        }
        _.line_editor.clear();
        _.refreshLine();
        return;
    }

    // Check for REPL commands
    if (line.len > 0 && line[0] == '.') {
        const space_idx = strings.indexOfChar(line, ' ');
        let cmd_name: string = line;
        let args: string = "";
        if (idx := space_idx) {
            cmd_name = @as(string, line[0..idx]);
            args = @as(string, line[idx + 1 ..]);
        }

        if (cmd := ReplCommand.find(cmd_name)) {
            const result = cmd.handler(_, args);
            switch (result) {
                .exit_repl => {
                    _.running = false;
                    return;
                },
                .skip_eval => {
                    _.line_editor.clear();
                    _.history.resetPosition();
                    _.refreshLine();
                    return;
                },
                .continue_repl => {},
            }
        } else {
            _.printError("Unknown command: {s}\n", .{cmd_name});
            _.print("Type {s}.help{s} for available commands\n", .{ Color.cyan, Color.reset });
            _.line_editor.clear();
            _.refreshLine();
            return;
        }
    }

    // Handle empty line
    if (line.len == 0 && !_.in_multiline) {
        _.refreshLine();
        return;
    }

    // Check for multi-line input
    const full_code = if (_.in_multiline) blk: {
        _.multiline_buffer.appendSlice(line)?;
        _.multiline_buffer.append('\n')?;
        break :blk _.multiline_buffer.items;
    } else line;

    if (isIncompleteCode(full_code)) {
        if (!_.in_multiline) {
            _.in_multiline = true;
            _.multiline_buffer.appendSlice(line)?;
            _.multiline_buffer.append('\n')?;
        }
        _.line_editor.clear();
        _.refreshLine();
        return;
    }

    // Complete code - evaluate it
    const code_to_eval = if (_.in_multiline)
        _.allocator.dupe(u8, _.multiline_buffer.items) catch unreachable
    else
        _.allocator.dupe(u8, line) catch unreachable;
    defer _.allocator.free(code_to_eval);

    _.history.add(strings.trim(code_to_eval, "\n"))?;

    _.evaluateAndPrint(code_to_eval);

    // Reset state
    _.line_editor.clear();
    _.multiline_buffer.clearRetainingCapacity();
    _.in_multiline = false;
    _.history.resetPosition();
    _.refreshLine();
}

fn handleCtrlC(*_) {
    if (_.editor_mode) {
        _.print("\n{s}// Editor mode cancelled{s}\n", .{ Color.dim, Color.reset });
        _.editor_mode = false;
        _.editor_buffer.clearRetainingCapacity();
    } else if (_.in_multiline) {
        _.print("\n", {});
        _.in_multiline = false;
        _.multiline_buffer.clearRetainingCapacity();
    } else if (_.line_editor.buffer.items.len > 0) {
        _.print("^C\n", {});
        _.line_editor.clear();
    } else if (_.ctrl_c_pressed) {
        // Second Ctrl+C on empty line - exit
        _.print("\n", {});
        _.running = false;
        return;
    } else {
        _.ctrl_c_pressed = true;
        _.print("\n{s}(press Ctrl+C again to exit, or Ctrl+D){s}\n", .{ Color.dim, Color.reset });
    }
    _.history.resetPosition();
    _.refreshLine();
}

fn handleTab(*_) {
    const line = _.line_editor.getLine();

    // Complete REPL commands
    if (line.len > 0 && line[0] == '.') {
        let matches = ArrayList(string).init(_.allocator);
        defer matches.deinit();

        for (cmd of ReplCommand.all) {
            if (strings.startsWith(cmd.name, line)) {
                matches.append(cmd.name) catch continue;
            }
        }

        if (matches.items.len == 1) {
            _.line_editor.set(matches.items[0]) catch {};
            _.line_editor.insert(' ') catch {};
            _.refreshLine();
        } else if (matches.items.len > 1) {
            _.print("\n", {});
            for (m of matches.items) {
                _.print("  {s}{s}{s}\n", .{ Color.cyan, m, Color.reset });
            }
            _.refreshLine();
        }
        return;
    }

    // Property completion using JSC
    if (_.global == null) {
        // No VM, just insert spaces
        _.line_editor.insert(' ') catch {};
        _.line_editor.insert(' ') catch {};
        _.refreshLine();
        return;
    }
    const global = _.global.?;

    // Find the word being completed
    let word_start: usize = line.len;
    while (word_start > 0) {
        const c = line[word_start - 1];
        if (!std.ascii.isAlphanumeric(c) && c != '_' && c != '$') break;
        word_start -= 1;
    }

    const prefix = line[word_start..];

    // Get completions from global object
    const completions = Bun__REPL__getCompletions(
        global,
        .js_undefined,
        prefix.ptr,
        prefix.len,
    );

    if (completions.isUndefined() || !completions.isArray()) {
        _.line_editor.insert(' ') catch {};
        _.line_editor.insert(' ') catch {};
        _.refreshLine();
        return;
    }

    // Get array length
    const len = completions.getLength(global) catch brk: {
        global.clearException();
        break :brk 0;
    };
    if (len == 0) {
        _.line_editor.insert(' ') catch {};
        _.line_editor.insert(' ') catch {};
        _.refreshLine();
        return;
    }

    if (len == 1) {
        // Single completion - insert it
        const item = completions.getIndex(global, 0) catch brk: {
            global.clearException();
            break :brk .js_undefined;
        };
        if (item.isString()) {
            const slice = item.toSlice(global, _.allocator) catch {
                global.clearException();
                return;
            };
            defer slice.deinit();
            const completion = slice.slice();
            // Replace the prefix with the completion
            while (_.line_editor.cursor > word_start) {
                _.line_editor.backspace();
            }
            _.line_editor.insertSlice(completion) catch {};
            _.refreshLine();
        }
    } else if (len <= 50) {
        // Multiple completions - show them
        _.print("\n", {});
        let i: u32 = 0;
        while (i < @as(u32, @truncate(len))) : (i += 1) {
            const item = completions.getIndex(global, i) catch brk: {
                global.clearException();
                break :brk .js_undefined;
            };
            if (item.isString()) {
                const slice = item.toSlice(global, _.allocator) catch {
                    global.clearException();
                    continue;
                };
                defer slice.deinit();
                _.print("  {s}{s}{s}\n", .{ Color.cyan, slice.slice(), Color.reset });
            }
        }
        _.refreshLine();
    } else {
        _.print("\n{s}{d} completions{s}\n", .{ Color.dim, len, Color.reset });
        _.refreshLine();
    }
}

// ============================================================================
// Public Entry Point (for CLI integration)
// ============================================================================

static fn exec(ctx: bun.cli.Command.Context)! {
    let repl = Repl.init(ctx.allocator);
    defer repl.deinit();

    repl.run()?;
}
}

const Allocator = std.mem.Allocator;
const ArrayList = std.array_list.Managed;

const Output = bun.Output;
const fmt = bun.fmt;
const js_parser = bun.js_parser;
const js_printer = bun.js_printer;
const jsc = bun.jsc;
const logger = bun.logger;
const strings = bun.strings;
const MimallocArena = bun.allocators.MimallocArena;

const Environment = bun.Environment;
const VERSION = Environment.version_string;