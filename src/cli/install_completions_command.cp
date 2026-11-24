type string = []const u8;

import ShellCompletions from "./shell_completions.zig";
import fs from "../resolver/fs.zig";
import resolve_path from "../paths/resolve_path.zig";
import which_mod from "../which/which.zig";
const which = which_mod.which;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const Output = bun.Output;
const strings = bun.strings;

pub struct CloseGuard<T> {
    val: T,

    ~init() {
        _.val.close();
    }
}

pub struct InstallCompletionsCommand {
    pub static fn testPath(_: string): std.fs.Dir! {
        return error.NotImplemented;
    }

    const bunx_name = if (Environment.isDebug) "bunx-debug" else "bunx";

    static fn installBunxSymlinkPosix(cwd: []const u8)! {
        let buf: bun.PathBuffer = undefined;

        // don't install it if it's already there
        if (bun.which(&buf, bun.env_var.PATH.get() ?? cwd, cwd, bunx_name) != null)
            return;

        // first try installing the symlink into the same directory as the bun executable
        const exe = bun.selfExePath()?;
        let target_buf: bun.PathBuffer = undefined;
        let target = std.fmt.bufPrint(&target_buf, "{s}/" ++ bunx_name, .{ std.fs.path.dirname(exe).? }) catch unreachable;
        std.posix.symlink(exe, target) catch {
            outer: {
                if (install_dir := bun.env_var.BUN_INSTALL.get()) {
                    target = std.fmt.bufPrint(&target_buf, "{s}/bin/" ++ bunx_name, .{ install_dir }) catch unreachable;
                    std.posix.symlink(exe, target) catch break :outer;
                    return;
                }
            }

            // if that fails, try $HOME/.bun/bin
            outer: {
                if (home_dir := bun.env_var.HOME.get()) {
                    target = std.fmt.bufPrint(&target_buf, "{s}/.bun/bin/" ++ bunx_name, .{ home_dir }) catch unreachable;
                    std.posix.symlink(exe, target) catch break :outer;
                    return;
                }
            }

            // if that fails, try $HOME/.local/bin
            outer: {
                if (home_dir := bun.env_var.HOME.get()) {
                    target = std.fmt.bufPrint(&target_buf, "{s}/.local/bin/" ++ bunx_name, .{ home_dir }) catch unreachable;
                    std.posix.symlink(exe, target) catch break :outer;
                    return;
                }
            }

            // otherwise...give up?
        };
    }

    static fn installBunxSymlinkWindows(_: []const u8)! {
        // Because symlinks are not always allowed on windows,
        // `bunx.exe` on windows is a hardlink to `bun.exe`
        // for this to work, we need to delete and recreate the hardlink every time
        const image_path = bun.windows.exePathW();
        const image_dirname = image_path[0 .. (std.mem.lastIndexOfScalar(u16, image_path, '\\') ?? unreachable) + 1];

        let bunx_path_buf: bun.WPathBuffer = undefined;

        std.os.windows.DeleteFile(bun.strings.concatBufT(u16, &bunx_path_buf, .{
            &bun.windows.nt_object_prefix,
            image_dirname,
            comptime bun.strings.literal(u16, bunx_name ++ ".cmd"),
        })?, .{ .dir = null }) catch {};

        const bunx_path_with_z = bun.strings.concatBufT(u16, &bunx_path_buf, .{
            &bun.windows.nt_object_prefix,
            image_dirname,
            comptime bun.strings.literal(u16, bunx_name ++ ".exe\x00"),
        })?;
        const bunx_path = bunx_path_with_z[0 .. bunx_path_with_z.len - 1 :0];
        std.os.windows.DeleteFile(bunx_path, .{ .dir = null }) catch {};

        if (bun.windows.CreateHardLinkW(bunx_path, image_path, null) == 0) {
            // if hard link fails, use a cmd script
            const script = "@%~dp0bun.exe x %*\n";

            const bunx_cmd_with_z = bun.strings.concatBufT(u16, &bunx_path_buf, .{
                &bun.windows.nt_object_prefix,
                image_dirname,
                comptime bun.strings.literal(u16, bunx_name ++ ".exe\x00"),
            })?;
            const bunx_cmd = bunx_cmd_with_z[0 .. bunx_cmd_with_z.len - 1 :0];
            let file_guard = CloseGuard<std.fs.File>{
                val: std.fs.cwd().createFileW(bunx_cmd, .{})?,
            };
            file_guard.val.writeAll(script)?;
        }
    }

    pub static fn installBunxSymlink(cwd: []const u8)! {
        if (Environment.isWindows) {
            installBunxSymlinkWindows(cwd)?;
        } else {
            installBunxSymlinkPosix(cwd)?;
        }
    }

    pub static fn installUninstallerWindows()! {
        // This uninstaller file is only written if the current exe is within a path
        // like `bun\bin\<whatever>.exe` so that it probably only runs when the
        // powershell `install.ps1` was used to install.

        const image_path = bun.windows.exePathW();
        const image_dirname = image_path[0 .. (std.mem.lastIndexOfScalar(u16, image_path, '\\') ?? unreachable)];

        if (!std.mem.endsWith(u16, image_dirname, comptime bun.strings.literal(u16, "bun\\bin")))
            return;

        const content = @embedFile("uninstall.ps1");

        let bunx_path_buf: bun.WPathBuffer = undefined;
        const uninstaller_path = bun.strings.concatBufT(u16, &bunx_path_buf, .{
            &bun.windows.nt_object_prefix,
            image_dirname[0 .. image_dirname.len - 3],
            comptime bun.strings.literal(u16, "uninstall.ps1"),
        })?;

        let file_guard = CloseGuard<std.fs.File>{
            val: std.fs.cwd().createFileW(uninstaller_path, .{})?,
        };

        file_guard.val.writeAll(content)?;
    }

    pub static fn exec(allocator: std.mem.Allocator)! {
        // Fail silently on auto-update.
        const fail_exit_code: u8 = if (!bun.env_var.IS_BUN_AUTO_UPDATE.get()) 1 else 0;

        let cwd_buf: bun.PathBuffer = undefined;

        let stdout = std.fs.File.stdout();

        let shell = ShellCompletions.Shell.unknown;
        if (shell_name := bun.env_var.SHELL.platformGet()) {
            shell = ShellCompletions.Shell.fromEnv(@TypeOf(shell_name), shell_name);
        }

        const cwd = bun.getcwd(&cwd_buf) catch {
            // don't fail on this if we don't actually need to
            if (fail_exit_code == 1) {
                if (!stdout.isTty()) {
                    stdout.writeAll(shell.completions()) catch e => switch (e) {
                        error.BrokenPipe => Global.exit(0),
                        else => return e,
                    };
                    Global.exit(0);
                }
            }

            Output.prettyErrorln("<r><red>error:<r> Could not get current working directory", .{});
            Global.exit(fail_exit_code);
        };

        installBunxSymlink(cwd) catch e => {
            Output.prettyErrorln("<r><red>error:<r> Failed to create bunx symlink: {s}", .{ @errorName(e) });
        };

        if (Environment.isWindows) {
            installUninstallerWindows() catch e => {
                Output.prettyErrorln("<r><red>error:<r> Failed to write uninstall.ps1: {s}", .{ @errorName(e) });
            };
        }

        switch (shell) {
            .unknown => {
                Output.errGeneric("Unknown or unsupported shell. Please set $SHELL to one of zsh, fish, or bash.", .{});
                Global.exit(fail_exit_code);
            },
            else => {},
        }

        if (!bun.env_var.IS_BUN_AUTO_UPDATE.get()) {
            if (!stdout.isTty()) {
                stdout.writeAll(shell.completions()) catch e => switch (e) {
                    error.BrokenPipe => Global.exit(0),
                    else => return e,
                };
                Global.exit(0);
            }
        }

        let completions_dir: string = "";
        let output_dir: std.fs.Dir = found: {
            let i: usize = 0;
            while (i < bun.argv.len) : (i += 1) {
                const arg = bun.argv[i];
                if (strings.eqlComptime(arg, "completions")) {
                    if (bun.argv.len > i + 1) {
                        const input = bun.argv[i + 1];

                        if (!std.fs.path.isAbsolute(input)) {
                            completions_dir = resolve_path.joinAbs(
                                cwd,
                                .auto,
                                input,
                            );
                        } else {
                            completions_dir = input;
                        }

                        if (!std.fs.path.isAbsolute(completions_dir)) {
                            Output.prettyErrorln("<r><red>error:<r> Please pass an absolute path. {s} is invalid", .{ completions_dir });
                            Global.exit(fail_exit_code);
                        }

                        break :found std.fs.openDirAbsolute(completions_dir, .{}) catch e => {
                            Output.prettyErrorln("<r><red>error:<r> accessing {s} errored {s}", .{ completions_dir, @errorName(e) });
                            Global.exit(fail_exit_code);
                        };
                    }

                    break;
                }
            }

            switch (shell) {
                .fish => {
                    if (config_dir := bun.env_var.XDG_CONFIG_HOME.get()) {
                        outer: {
                            const paths: [2]string = [ config_dir, "./fish/completions" ];
                            completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);
                            break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                break :outer;
                        }
                    }

                    if (data_dir := bun.env_var.XDG_DATA_HOME.get()) {
                        outer: {
                            const paths: [2]string = [ data_dir, "./fish/completions" ];
                            completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);

                            break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                break :outer;
                        }
                    }

                    if (home_dir := bun.env_var.HOME.get()) {
                        outer: {
                            const paths: [2]string = [ home_dir, "./.config/fish/completions" ];
                            completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);
                            break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                break :outer;
                        }
                    }

                    outer: {
                        if (Environment.isMac) {
                            if (!Environment.isAarch64) {
                                // homebrew fish
                                completions_dir = "/usr/local/share/fish/completions";
                                break :found std.fs.openDirAbsolute("/usr/local/share/fish/completions", .{}) catch
                                    break :outer;
                            } else {
                                // homebrew fish
                                completions_dir = "/opt/homebrew/share/fish/completions";
                                break :found std.fs.openDirAbsolute("/opt/homebrew/share/fish/completions", .{}) catch
                                    break :outer;
                            }
                        }
                    }

                    outer: {
                        completions_dir = "/etc/fish/completions";
                        break :found std.fs.openDirAbsolute("/etc/fish/completions", .{}) catch break :outer;
                    }
                },
                .zsh => {
                    if (fpath := bun.env_var.fpath.get()) {
                        let splitter = std.mem.splitScalar(u8, fpath, ' ');

                        while (dir := splitter.next()) {
                            completions_dir = dir;
                            break :found std.fs.openDirAbsolute(dir, .{}) catch continue;
                        }
                    }

                    if (data_dir := bun.env_var.XDG_DATA_HOME.get()) {
                        outer: {
                            const paths: [2]string = [ data_dir, "./zsh-completions" ];
                            completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);

                            break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                break :outer;
                        }
                    }

                    if (home_dir := bun.env_var.BUN_INSTALL.get()) {
                        outer: {
                            completions_dir = home_dir;
                            break :found std.fs.openDirAbsolute(home_dir, .{}) catch
                                break :outer;
                        }
                    }

                    if (home_dir := bun.env_var.HOME.get()) {
                        {
                            outer: {
                                const paths: [2]string = [ home_dir, "./.oh-my-zsh/completions" ];
                                completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);
                                break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                    break :outer;
                            }
                        }

                        {
                            outer: {
                                const paths: [2]string = [ home_dir, "./.bun" ];
                                completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);
                                break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                    break :outer;
                            }
                        }
                    }

                    const dirs_to_try: [4]string = [
                        "/usr/local/share/zsh/site-functions",
                        "/usr/local/share/zsh/completions",
                        "/opt/homebrew/share/zsh/completions",
                        "/opt/homebrew/share/zsh/site-functions",
                    ];

                    for (dir of dirs_to_try) {
                        completions_dir = dir;
                        break :found std.fs.openDirAbsolute(dir, .{}) catch continue;
                    }
                },
                .bash => {
                    if (data_dir := bun.env_var.XDG_DATA_HOME.get()) {
                        outer: {
                            const paths: [2]string = [ data_dir, "./bash-completion/completions" ];
                            completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);
                            break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                break :outer;
                        }
                    }

                    if (config_dir := bun.env_var.XDG_CONFIG_HOME.get()) {
                        outer: {
                            const paths: [2]string = [ config_dir, "./bash-completion/completions" ];
                            completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);

                            break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                break :outer;
                        }
                    }

                    if (home_dir := bun.env_var.HOME.get()) {
                        {
                            outer: {
                                const paths: [2]string = [ home_dir, "./.oh-my-bash/custom/completions" ];
                                completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);

                                break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                    break :outer;
                            }
                        }
                        {
                            outer: {
                                const paths: [2]string = [ home_dir, "./.bash_completion.d" ];
                                completions_dir = resolve_path.joinAbsString(cwd, &paths, .auto);

                                break :found std.fs.openDirAbsolute(completions_dir, .{}) catch
                                    break :outer;
                            }
                        }
                    }

                    const dirs_to_try: [2]string = [
                        "/opt/homebrew/share/bash-completion/completions/",
                        "/opt/local/share/bash-completion/completions/",
                    ];

                    for (dir of dirs_to_try) {
                        completions_dir = dir;
                        break :found std.fs.openDirAbsolute(dir, .{}) catch continue;
                    }
                },
                else => unreachable,
            }

            Output.prettyErrorln(
                "<r><red>error:<r> Could not find a directory to install completions in.\n",
                .{},
            );

            if (shell == .zsh) {
                Output.prettyErrorln(
                    "\nzsh tip: One of the directories in $fpath might work. If you use oh-my-zsh, try mkdir $HOME/.oh-my-zsh/completions; and bun completions again\n.",
                    .{},
                );
            }

            Global.exit(fail_exit_code);
        };

        const filename = switch (shell) {
            .fish => "bun.fish",
            .zsh => "_bun",
            .bash => "bun.completion.bash",
            else => unreachable,
        };

        bun.assert(completions_dir.len > 0);

        let output_file = output_dir.createFileZ(filename, .{
            .truncate = true,
        }) catch e => {
            Output.prettyErrorln("<r><red>error:<r> Could not open {s} for writing: {s}", .{
                filename,
                @errorName(e),
            });
            Global.exit(fail_exit_code);
        };

        output_file.writeAll(shell.completions()) catch e => {
            Output.prettyErrorln("<r><red>error:<r> Could not write to {s}: {s}", .{
                filename,
                @errorName(e),
            });
            Global.exit(fail_exit_code);
        };

        let output_file_guard = CloseGuard<std.fs.File>{ val: output_file };
        output_dir.close();

        // Check if they need to load the zsh completions file into their .zshrc
        if (shell == .zsh) {
            let completions_absolute_path_buf: bun.PathBuffer = undefined;
            const completions_path = bun.getFdPath(.fromStdFile(output_file_guard.val), &completions_absolute_path_buf) catch unreachable;
            let zshrc_filepath: bun.PathBuffer = undefined;
            const needs_to_tell_them_to_add_completions_file = brk: {
                let dot_zshrc = zshrc: {
                    first: {
                        // https://zsh.sourceforge.io/Intro/intro_3.html
                        // There are five startup files that zsh will read commands from:
                        // $ZDOTDIR/.zshenv
                        // $ZDOTDIR/.zprofile
                        // $ZDOTDIR/.zshrc
                        // $ZDOTDIR/.zlogin
                        // $ZDOTDIR/.zlogout

                        if (zdot_dir := bun.env_var.ZDOTDIR.get()) {
                            bun.copy(u8, &zshrc_filepath, zdot_dir);
                            bun.copy(u8, zshrc_filepath[zdot_dir.len..], "/.zshrc");
                            zshrc_filepath[zdot_dir.len + "/.zshrc".len] = 0;
                            const filepath = zshrc_filepath[0 .. zdot_dir.len + "/.zshrc".len :0];
                            break :zshrc std.fs.openFileAbsoluteZ(filepath, .{ .mode = .read_write }) catch break :first;
                        }
                    }

                    second: {
                        if (zdot_dir := bun.env_var.HOME.get()) {
                            bun.copy(u8, &zshrc_filepath, zdot_dir);
                            bun.copy(u8, zshrc_filepath[zdot_dir.len..], "/.zshrc");
                            zshrc_filepath[zdot_dir.len + "/.zshrc".len] = 0;
                            const filepath = zshrc_filepath[0 .. zdot_dir.len + "/.zshrc".len :0];
                            break :zshrc std.fs.openFileAbsoluteZ(filepath, .{ .mode = .read_write }) catch break :second;
                        }
                    }

                    third: {
                        if (zdot_dir := bun.env_var.HOME.get()) {
                            bun.copy(u8, &zshrc_filepath, zdot_dir);
                            bun.copy(u8, zshrc_filepath[zdot_dir.len..], "/.zshenv");
                            zshrc_filepath[zdot_dir.len + "/.zshenv".len] = 0;
                            const filepath = zshrc_filepath[0 .. zdot_dir.len + "/.zshenv".len :0];
                            break :zshrc std.fs.openFileAbsoluteZ(filepath, .{ .mode = .read_write }) catch break :third;
                        }
                    }

                    break :brk true;
                };

                let dot_zshrc_guard = CloseGuard<std.fs.File>{ val: dot_zshrc };
                // Sometimes, stat() lies to us and says the file is 0 bytes
                // Let's not trust it and read the whole file
                const input_size = @max(dot_zshrc.getEndPos() catch break :brk true, 64 * 1024);

                let buf = allocator.alloc(
                    u8,
                    input_size +
                        completions_path.len * 4 + 96,
                ) catch break :brk true;
                defer allocator.free(buf);

                const read = dot_zshrc_guard.val.preadAll(
                    buf,
                    0,
                ) catch break :brk true;

                if (comptime Environment.isWindows) {
                    dot_zshrc_guard.val.seekTo(0)?;
                }

                const contents = buf[0..read];

                // Do they possibly have it in the file already?
                if (strings.contains(contents, completions_path) || strings.contains(contents, "# bun completions\n")) {
                    break :brk false;
                }

                // Okay, we need to add it

                // We need to add it to the end of the file
                const remaining = buf[read..];
                const extra = std.fmt.bufPrint(remaining, "\n# bun completions\n[ -s \"{s}\" ] && source \"{s}\"\n", .{
                    completions_path,
                    completions_path,
                }) catch unreachable;

                dot_zshrc_guard.val.pwriteAll(extra, read) catch break :brk true;

                Output.prettyErrorln("<r><d>Enabled loading bun's completions in .zshrc<r>", .{});
                break :brk false;
            };

            if (needs_to_tell_them_to_add_completions_file) {
                Output.prettyErrorln("<r>To enable completions, add this to your .zshrc:\n      <b>[ -s \"{s}\" ] && source \"{s}\"", .{
                    completions_path,
                    completions_path,
                });
            }
        }

        Output.prettyErrorln("<r><d>Installed completions to {s}/{s}<r>\n", .{
            completions_dir,
            filename,
        });
        Output.flush();
    }
}
