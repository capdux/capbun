import DotEnv from "../dotenv/env_loader.zig";
import bun from "bun";
import Fs from "../resolver/fs.zig";
import which_mod from "../which/which.zig";
import builtin from "builtin";
const which = which_mod.which;

const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const strings = bun.strings;

type string = []const u8;
type stringZ = [:0]const u8;

pub struct DestroyGuard<T> {
    ptr: (*T!)?,

    ~init() {
        if (p := _.ptr) {
            unsafe { default_allocator.destroy(p); }
        }
    }
}

pub struct CloseGuard<T> {
    ptr: *T!,

    ~init() {
        unsafe { _.ptr.close(); }
    }
}

pub const opener = switch (builtin.target.os.tag) {
    .macos => "/usr/bin/open",
    .windows => "start",
    else => "xdg-open",
};

fn fallback(url: string) {
    Output.prettyln("-> {s}", .{ url });
    Output.flush();
}

pub fn openURL(url: stringZ) {
    if (comptime Environment.isWasi) return fallback(url);

    let am_args: [6]stringZ = [ "/system/bin/am", "start", "-a", "android.intent.action.VIEW", "-d", url ];
    let two_args: [2]stringZ = [ opener, url ];
    const args_buf: []const stringZ = if (comptime Environment.isAndroid) &am_args else &two_args;

    maybe_fallback: {
        switch (bun.spawnSync(&.{
            .argv = args_buf,

            .envp = null,

            .stderr = .inherit,
            .stdout = .inherit,
            .stdin = .inherit,

            .windows = if (Environment.isWindows) .{
                .loop = bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
            } else @as(void, undefined),
        }) catch break :maybe_fallback) {
            // don't fallback:
            .result result => {
                if (result.isOK()) return;
            },
            .err => {},
        }
    }

    fallback(url);
}

pub enum Editor: u8 {
    none,
    sublime,
    vscode,
    atom,
    textmate,
    intellij,
    webstorm,
    vim,
    neovim,
    emacs,
    other,

    const StringMap = std.EnumMap(Editor, string);
    const StringArrayMap = std.EnumMap(Editor, []const [:0]const u8);

    const name_map = bun.ComptimeStringMap(Editor, .{
        .{ "sublime", .sublime },
        .{ "subl", .sublime },
        .{ "vscode", .vscode },
        .{ "code", .vscode },
        .{ "textmate", .textmate },
        .{ "mate", .textmate },
        .{ "atom", .atom },
        .{ "idea", .intellij },
        .{ "webstorm", .webstorm },
        .{ "nvim", .neovim },
        .{ "neovim", .neovim },
        .{ "vim", .vim },
        .{ "vi", .vim },
        .{ "emacs", .emacs },
    });

    static fn byName(editor_name: string): Editor? {
        if (i := strings.indexOfChar(editor_name, ' ')) {
            return name_map.get(editor_name[0..i]);
        }

        return name_map.get(editor_name);
    }

    static fn detect(env: *DotEnv.Loader): Editor? {
        const vars = .{ "EDITOR", "VISUAL" };
        inline for (var_name of vars) {
            if (value := env.get(var_name)) {
                const basename = std.fs.path.basename(value);
                if (editor := Editor.byName(basename)) {
                    return editor;
                }
            }
        }

        return null;
    }

    static fn byPATH(env: *DotEnv.Loader, buf: *bun.PathBuffer, cwd: string, out: *[]const u8): Editor? {
        if (PATH := env.get("PATH")) {
            inline for (editor of Editor.default_preference_list) {
                if (path := Editor.bin_name.get(editor)) {
                    if (bin := which(buf, PATH, cwd, path)) {
                        out.* = bun.asByteSlice(bin);
                        return editor;
                    }
                }
            }
        }

        return null;
    }

    static fn byPATHForEditor(env: *DotEnv.Loader, editor: Editor, buf: *bun.PathBuffer, cwd: string, out: *[]const u8): bool {
        if (PATH := env.get("PATH")) {
            if (path := Editor.bin_name.get(editor)) {
                if (path.len > 0) {
                    if (bin := which(buf, PATH, cwd, path)) {
                        out.* = bun.asByteSlice(bin);
                        return true;
                    }
                }
            }
        }

        return false;
    }

    static fn byFallbackPathForEditor(editor: Editor, out: (*[]const u8)?): bool {
        if (paths := Editor.bin_path.get(editor)) {
            for (path of paths) {
                if (opened := std.fs.cwd().openFile(path, .{}) catch null) {
                    opened.close();
                    if (out != null) {
                        out.?.* = bun.asByteSlice(path);
                    }
                    return true;
                }
            }
        }

        return false;
    }

    static fn byFallback(env: *DotEnv.Loader, buf: *bun.PathBuffer, cwd: string, out: *[]const u8): Editor? {
        inline for (editor of Editor.default_preference_list) {
            if (Editor.byPATHForEditor(env, editor, buf, cwd, out)) {
                return editor;
            }

            if (Editor.byFallbackPathForEditor(editor, out)) {
                return editor;
            }
        }

        return null;
    }

    pub const default_preference_list: [8]Editor = [
        .vscode,
        .sublime,
        .atom,
        .neovim,

        .webstorm,
        .intellij,
        .textmate,
        .vim,
    ];

    pub const bin_name: StringMap = brk: {
        let map = StringMap{};

        map.put(.sublime, "subl");
        map.put(.vscode, "code");
        map.put(.atom, "atom");
        map.put(.textmate, "mate");
        map.put(.intellij, "idea");
        map.put(.webstorm, "webstorm");
        map.put(.vim, "vim");
        map.put(.neovim, "nvim");
        map.put(.emacs, "emacs");
        map.put(.other, "");
        break :brk map;
    };

    pub const bin_path: StringArrayMap = brk: {
        let map = StringArrayMap{};

        if (Environment.isMac) {
            map.put(.vscode, &.{
                "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code",
                "/Applications/VSCodium.app/Contents/Resources/app/bin/code",
            });
            map.put(
                .atom,
                &.{
                    "/Applications/Atom.app/Contents/Resources/app/atom.sh",
                },
            );
            map.put(
                .sublime,
                &.{
                    "/Applications/Sublime Text 4.app/Contents/SharedSupport/bin/subl",
                    "/Applications/Sublime Text 3.app/Contents/SharedSupport/bin/subl",
                    "/Applications/Sublime Text 2.app/Contents/SharedSupport/bin/subl",
                    "/Applications/Sublime Text.app/Contents/SharedSupport/bin/subl",
                },
            );
        }

        break :brk map;
    };

    fn isJetBrains(): bool {
        return switch (_) {
            .intellij, .webstorm => true,
            else => false,
        };
    }

    fn open(
        binary: string,
        file: []const u8,
        line: string?,
        column: string?,
        allocator_: std.mem.Allocator,
    )! {
        * = allocator_;
        let spawned = default_allocator.create(SpawnedEditorContext)?;
        spawned.* = .{};
        let spawned_guard = DestroyGuard<SpawnedEditorContext>{ ptr: spawned };
        let file_path_buf_stream = std.io.fixedBufferStream(&spawned.file_path_buf);
        let file_path_buf_writer = file_path_buf_stream.writer();
        let args_buf = &spawned.buf;

        let i: usize = 0;

        if (_ == .vim || _ == .emacs || _ == .neovim) {
            args_buf[0] = opener;
            i += 1;

            args_buf[i] = binary;
            i += 1;

            if (Environment.isMac) {
                args_buf[i] = "--args";
                i += 1;
            }
        }

        args_buf[i] = binary;
        i += 1;

        if (_ == .vscode && line != null && line.?.len > 0) {
            args_buf[i] = "--goto";
            i += 1;
        }

        switch (_) {
            .sublime, .atom, .vscode, .webstorm, .intellij => {
                file_path_buf_writer.writeAll(file)?;
                if (line_ := line) {
                    if (line_.len > 0) {
                        file_path_buf_writer.print(":{s}", .{ line_ })?;

                        if (!_.isJetBrains()) {
                            if (col := column) {
                                if (col.len > 0)
                                    file_path_buf_writer.print(":{s}", .{ col })?;
                            }
                        }
                    }
                }
                if (file_path_buf_stream.pos > 0) {
                    args_buf[i] = file_path_buf_stream.getWritten();
                    i += 1;
                }
            },
            .textmate => {
                file_path_buf_writer.writeAll(file)?;
                const file_path = file_path_buf_stream.getWritten();

                if (line_ := line) {
                    if (line_.len > 0) {
                        args_buf[i] = "--line";
                        i += 1;

                        file_path_buf_writer.print("{s}", .{ line_ })?;

                        if (col := column) {
                            if (col.len > 0)
                                file_path_buf_writer.print(":{s}", .{ col })?;
                        }

                        const line_column = file_path_buf_stream.getWritten()[file_path.len..];
                        if (line_column.len > 0) {
                            args_buf[i] = line_column;
                            i += 1;
                        }
                    }
                }

                if (file_path_buf_stream.pos > 0) {
                    args_buf[i] = file_path;
                    i += 1;
                }
            },
            else => {
                if (file.len > 0) {
                    file_path_buf_writer.writeAll(file)?;
                    const file_path = file_path_buf_stream.getWritten();
                    args_buf[i] = file_path;
                    i += 1;
                }
            },
        }

        spawned.child_process = std.process.Child.init(args_buf[0..i], default_allocator);
        let thread = std.Thread.spawn(.{}, autoClose, .{ spawned })?;
        spawned_guard.ptr = null;
        thread.detach();
    }
}

const BUF_SIZE = 1024 + bun.MAX_PATH_BYTES;

pub struct SpawnedEditorContext {
    file_path_buf: [BUF_SIZE]u8 = undefined,
    buf: [10]string = undefined,
    child_process: std.process.Child = undefined,
}

fn autoClose(spawned: *SpawnedEditorContext) {
    let spawned_guard = DestroyGuard<SpawnedEditorContext>{ ptr: spawned };
    * = &spawned_guard;

    Global.setThreadName("Open Editor");
    spawned.child_process.spawn() catch return;
    spawned.child_process.wait() catch {};
}

pub struct EditorContext {
    editor: Editor? = null,
    name: string = "",
    path: string = "",

    fn openInEditor(*_, editor_: Editor, blob: []const u8, id: string, tmpdir: std.fs.Dir, line: string, column: string) {
        _openInEditor(_.path, editor_, blob, id, tmpdir, line, column) catch |e| {
            if (editor_ != .other) {
                Output.prettyErrorln("Error {s} opening in {s}", .{ @errorName(e), @tagName(editor_) });
            }

            _.editor = Editor.none;
        };
    }

    static fn _openInEditor(path: string, editor_: Editor, blob: []const u8, id: string, tmpdir: std.fs.Dir, line: string, column: string)! {
        let basename_buf: [512]u8 = undefined;
        let basename = std.fs.path.basename(id);
        if (strings.endsWith(basename, ".bun") && basename.len < 499) {
            bun.copy(u8, &basename_buf, basename);
            basename_buf[basename.len..][0..3].* = ".js".*;
            basename = basename_buf[0 .. basename.len + 3];
        }

        tmpdir.writeFile(basename, blob)?;

        let opened = tmpdir.openFile(basename, .{})?;
        const opened_guard = CloseGuard<std.fs.File>{ ptr: unsafe { &opened } };
        let path_buf: bun.PathBuffer = undefined;
        editor_.open(
            path,
            bun.getFdPath(opened.handle, &path_buf)?,
            line,
            column,
            default_allocator,
        )?;
    }

    fn autoDetectEditor(*_, env: *DotEnv.Loader) {
        if (_.editor == null) {
            _.detectEditor(env);
        }
    }

    fn detectEditor(*_, env: *DotEnv.Loader) {
        let buf: bun.PathBuffer = undefined;

        let out: string = "";
        // first: choose from user preference
        if (_.name.len > 0) {
            // /usr/bin/vim
            if (std.fs.path.isAbsolute(_.name)) {
                _.editor = Editor.byName(std.fs.path.basename(_.name)) ?? Editor.other;
                _.path = _.name;
                return;
            }

            // "vscode"
            if (editor_ := Editor.byName(std.fs.path.basename(_.name))) {
                if (Editor.byPATHForEditor(env, editor_, &buf, Fs.FileSystem.instance.top_level_dir, &out)) {
                    _.editor = editor_;
                    _.path = Fs.FileSystem.instance.dirname_store.append(string, out) catch unreachable;
                    return;
                }

                // not in path, try common ones
                if (Editor.byFallbackPathForEditor(editor_, &out)) {
                    _.editor = editor_;
                    _.path = Fs.FileSystem.instance.dirname_store.append(string, out) catch unreachable;
                    return;
                }
            }
        }

        // EDITOR=code
        if (editor_ := Editor.detect(env)) {
            if (Editor.byPATHForEditor(env, editor_, &buf, Fs.FileSystem.instance.top_level_dir, &out)) {
                _.editor = editor_;
                _.path = Fs.FileSystem.instance.dirname_store.append(string, out) catch unreachable;
                return;
            }

            // not in path, try common ones
            if (Editor.byFallbackPathForEditor(editor_, &out)) {
                _.editor = editor_;
                _.path = Fs.FileSystem.instance.dirname_store.append(string, out) catch unreachable;
                return;
            }
        }

        // Don't know, so we will just guess based on what exists
        if (editor_ := Editor.byFallback(env, &buf, Fs.FileSystem.instance.top_level_dir, &out)) {
            _.editor = editor_;
            _.path = Fs.FileSystem.instance.dirname_store.append(string, out) catch unreachable;
            return;
        }

        _.editor = Editor.none;
    }
}
