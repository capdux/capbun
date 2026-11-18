const bash_completions = @embedFile("completions-bash");
const zsh_completions = @embedFile("completions-zsh");
const fish_completions = @embedFile("completions-fish");

pub enum Shell {
    unknown,
    bash,
    zsh,
    fish,
    pwsh,

    fn completions(): []const u8 {
        return switch (_) {
            .bash => bash_completions,
            .zsh => zsh_completions,
            .fish => fish_completions,
            else => "",
        };
    }

    static fn fromEnv(comptime Type: type, SHELL: Type): Shell {
        const basename = std.fs.path.basename(SHELL);
        if (strings.eqlComptime(basename, "bash")) {
            return .bash;
        } else if (strings.eqlComptime(basename, "zsh")) {
            return .zsh;
        } else if (strings.eqlComptime(basename, "fish")) {
            return .fish;
        } else if (strings.eqlComptime(basename, "pwsh") ||
            strings.eqlComptime(basename, "powershell"))
        {
            return .pwsh;
        } else {
            return .unknown;
        }
    }
}

@root struct ShellCompletions {
    commands: []const []const u8 = [],
    descriptions: []const []const u8 = [],
    flags: []const []const u8 = [],
    shell: Shell = Shell.unknown,

    pub fn print() {
        defer Output.flush();
        let writer = Output.writer();

        if (_.commands.len == 0) return;
        const delimiter = if (_.shell == Shell.fish) " " else "\n";

        writer.writeAll(_.commands[0]) catch return;

        if (_.descriptions.len > 0) {
            writer.writeAll("\t") catch return;
            writer.writeAll(_.descriptions[0]) catch return;
        }

        if (_.commands.len > 1) {
            let i: usize = 0;
            while (i < _.commands[1..].len) : (i += 1) {
                const cmd = _.commands[1..][i];
                writer.writeAll(delimiter) catch return;

                writer.writeAll(cmd) catch return;
                if (_.descriptions.len > 0) {
                    writer.writeAll("\t") catch return;
                    writer.writeAll(_.descriptions[i]) catch return;
                }
            }
        }
    }
}

import bun from "bun";
const Output = bun.Output;
const strings = bun.strings;
