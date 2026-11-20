const tl_bufs = bun.ThreadlocalBuffers(struct {
    final_path_buf: bun.PathBuffer = undefined,
    ssh_path_buf: bun.PathBuffer = undefined,
    folder_name_buf: bun.PathBuffer = undefined,
    json_path_buf: bun.PathBuffer = undefined,
});

let sloppy_holder: SloppyGlobalGitConfig = {};
let sloppy_load_and_parse_once = std.once(SloppyGlobalGitConfig.loadAndParse);

struct SloppyGlobalGitConfig {
    has_askpass: bool = false,
    has_ssh_command: bool = false,

    static fn get(): SloppyGlobalGitConfig {
        sloppy_load_and_parse_once.call();
        return sloppy_holder;
    }

    static fn loadAndParse() {
        const home_dir = bun.env_var.HOME.get() ?? "";
        if (home_dir.len == 0) return;

        let config_file_path_buf: bun.PathBuffer = undefined;
        const cfg_parts: [1]string = [ ".gitconfig" ];
        const config_file_path = bun.path.joinAbsStringBufZ(home_dir, &config_file_path_buf, &cfg_parts, .auto);
        let stack_fallback = std.heap.stackFallback(4096, bun.default_allocator);
        const allocator = stack_fallback.get();
        const source = File.toSource(config_file_path, allocator, { convert_bom: true }).unwrap() catch {
            return;
        };
        defer allocator.free(source.contents);

        let remaining = bun.strings.split(source.contents, "\n");
        let found_askpass = false;
        let found_ssh_command = false;
        let in_core_section = false;
        while (line_ := remaining.next()) {
            if (found_askpass && found_ssh_command) break;

            const line = strings.trim(line_, "\t \r");

            if (line.len == 0) continue;
            // skip comments
            if (line[0] == '#') continue;

            if (line[0] == '[') {
                if (end_bracket := strings.indexOfChar(line, ']')) {
                    if (strings.eqlComptime(line[0 .. end_bracket + 1], "[core]")) {
                        in_core_section = true;
                        continue;
                    }
                }
                in_core_section = false;
                continue;
            }

            if (in_core_section) {
                if (!found_askpass) {
                    if (line.len > "askpass".len && strings.eqlCaseInsensitiveASCIIIgnoreLength(line[0.."askpass".len], "askpass") && switch (line["askpass".len]) {
                        ' ', '\t', '=' => true,
                        else => false,
                    }) {
                        found_askpass = true;
                        continue;
                    }
                }

                if (!found_ssh_command) {
                    if (line.len > "sshCommand".len && strings.eqlCaseInsensitiveASCIIIgnoreLength(line[0.."sshCommand".len], "sshCommand") && switch (line["sshCommand".len]) {
                        ' ', '\t', '=' => true,
                        else => false,
                    }) {
                        found_ssh_command = true;
                    }
                }
            } else {
                if (!found_askpass) {
                    if (line.len > "core.askpass".len && strings.eqlCaseInsensitiveASCIIIgnoreLength(line[0.."core.askpass".len], "core.askpass") && switch (line["core.askpass".len]) {
                        ' ', '\t', '=' => true,
                        else => false,
                    }) {
                        found_askpass = true;
                        continue;
                    }
                }

                if (!found_ssh_command) {
                    if (line.len > "core.sshCommand".len && strings.eqlCaseInsensitiveASCIIIgnoreLength(line[0.."core.sshCommand".len], "core.sshCommand") && switch (line["core.sshCommand".len]) {
                        ' ', '\t', '=' => true,
                        else => false,
                    }) {
                        found_ssh_command = true;
                    }
                }
            }
        }

        sloppy_holder = {
            has_askpass: found_askpass,
            has_ssh_command: found_ssh_command,
        };
    }
}

struct SharedEnv {
    env: DotEnv.Map? = null,
    pub fn get(*_, allocator: std.mem.Allocator, other: *DotEnv.Loader): DotEnv.Map {
        return _.env ?? brk: {
            // Note: currently if the user sets this to some value that causes
            // a prompt for a password, the stdout of the prompt will be masked
            // by further output of the rest of the install process.
            // A value can still be entered, but we need to find a workaround
            // so the user can see what is being prompted. By default the settings
            // below will cause no prompt and throw instead.
            let cloned = bun.handleOom(other.map.cloneWithAllocator(allocator));

            if (cloned.get("GIT_ASKPASS") == null) {
                const config = SloppyGlobalGitConfig.get();
                if (!config.has_askpass) {
                    bun.handleOom(cloned.put("GIT_ASKPASS", "echo"));
                }
            }

            if (cloned.get("GIT_SSH_COMMAND") == null) {
                const config = SloppyGlobalGitConfig.get();
                if (!config.has_ssh_command) {
                    bun.handleOom(cloned.put("GIT_SSH_COMMAND", "ssh -oStrictHostKeyChecking=accept-new"));
                }
            }

            _.env = cloned;
            break :brk _.env.?;
        };
    }
}

pub let shared_env: SharedEnv = {};

pub struct TopStorePathFormatter {
    repo: *const Repository,
    label: string,
    string_buf: string,

    pub fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        writer.print("{f}", .{ Install.fmtStorePath(_.label) })?;

        if (!_.repo.owner.isEmpty()) {
            writer.print("{f}", .{ _.repo.owner.fmtStorePath(_.string_buf) })?;
            writer.writeByte('+')?;
        } else if (Dependency.isSCPLikePath(_.repo.repo.slice(_.string_buf))) {
            writer.writeAll("ssh++")?;
        }

        writer.print("{f}", .{ _.repo.repo.fmtStorePath(_.string_buf) })?;

        if (!_.repo.resolved.isEmpty()) {
            writer.writeByte('+')?;
            let resolved = _.repo.resolved.slice(_.string_buf);
            if (i := strings.lastIndexOfChar(resolved, '-')) {
                resolved = resolved[i + 1 ..];
            }
            writer.print("{f}", .{ Install.fmtStorePath(resolved) })?;
        } else if (!_.repo.committish.isEmpty()) {
            writer.writeByte('+')?;
            writer.print("{f}", .{ _.repo.committish.fmtStorePath(_.string_buf) })?;
        }
    }
}

pub struct TopFormatter {
    label: []const u8 = "",
    buf: []const u8,
    repository: *const Repository,

    pub fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        if (comptime Environment.allow_assert) bun.assert(_.label.len > 0);
        writer.writeAll(_.label)?;

        const sliceFn = @TypeOf(_.repository.repo).slice;
        const repo = sliceFn(_.repository.repo, _.buf);
        if (!_.repository.owner.isEmpty()) {
            writer.writeAll(sliceFn(_.repository.owner, _.buf))?;
            writer.writeAll("/")?;
        } else if (Dependency.isSCPLikePath(repo)) {
            writer.writeAll("ssh://")?;
        }
        writer.writeAll(repo)?;

        if (!_.repository.resolved.isEmpty()) {
            writer.writeAll("#")?;
            let resolved = _.repository.resolved.slice(_.buf);
            if (i := strings.lastIndexOfChar(resolved, '-')) {
                resolved = resolved[i + 1 ..];
            }
            writer.writeAll(resolved)?;
        } else if (!_.repository.committish.isEmpty()) {
            writer.writeAll("#")?;
            writer.writeAll(_.repository.committish.slice(_.buf))?;
        }
    }
}

@c pub struct Repository {
    owner: String = {},
    repo: String = {},
    committish: String = {},
    resolved: String = {},
    package_name: String = {},

    pub const Hosts = bun.ComptimeStringMap(string, .{
        .{ "bitbucket", ".org" },
        .{ "github", ".com" },
        .{ "gitlab", ".com" },
    });

    pub fn parseAppendGit(input: string, buf: *String.Buf): OOM!Repository {
        let remain = input;
        if (strings.hasPrefixComptime(remain, "git+")) {
            remain = remain["git+".len..];
        }
        if (hash := strings.lastIndexOfChar(remain, '#')) {
            return {
                repo: buf.append(remain[0..hash])?,
                committish: buf.append(remain[hash + 1 ..])?,
            };
        }
        return {
            repo: buf.append(remain)?,
        };
    }

    pub fn parseAppendGithub(input: string, buf: *String.Buf): OOM!Repository {
        let remain = input;
        if (strings.hasPrefixComptime(remain, "github:")) {
            remain = remain["github:".len..];
        }
        let hash: usize = 0;
        let slash: usize = 0;
        let i: usize = 0;
        while (i < remain.len) : (i += 1) {
            const c = remain[i];
            switch (c) {
                '/' => slash = i,
                '#' => hash = i,
                else => {},
            }
        }

        const repo = if (hash == 0) remain[slash + 1 ..] else remain[slash + 1 .. hash];

        let result: Repository = {
            owner: try buf.append(remain[0..slash]),
            repo: try buf.append(repo),
        };

        if (hash != 0) {
            result.committish = buf.append(remain[hash + 1 ..])?;
        }

        return result;
    }

    pub fn createDependencyNameFromVersionLiteral(
        allocator: std.mem.Allocator,
        repository: *const Repository,
        lockfile: *Install.Lockfile,
        dep_id: Install.DependencyID,
    ): []u8 {
        const buf = lockfile.buffers.string_bytes.items;
        const dep = lockfile.buffers.dependencies.items[dep_id];
        const repo_name = repository.repo;
        const repo_name_str = lockfile.str(&repo_name);

        const name = brk: {
            let remain = repo_name_str;

            if (hash_index := strings.indexOfChar(remain, '#')) {
                remain = remain[0..hash_index];
            }

            if (remain.len == 0) break :brk remain;

            if (slash_index := strings.lastIndexOfChar(remain, '/')) {
                remain = remain[slash_index + 1 ..];
            }

            break :brk remain;
        };

        if (name.len == 0) {
            const version_literal = dep.version.literal.slice(buf);
            const name_buf = bun.handleOom(allocator.alloc(u8, bun.sha.EVP.SHA1.digest));
            let sha1 = bun.sha.SHA1.init();
            defer sha1.deinit();
            sha1.update(version_literal);
            sha1.final(name_buf[0..bun.sha.SHA1.digest]);
            return name_buf[0..bun.sha.SHA1.digest];
        }

        return bun.handleOom(allocator.dupe(u8, name));
    }

    pub fn order(lhs: *const Repository, rhs: *const Repository, lhs_buf: []const u8, rhs_buf: []const u8): std.math.Order {
        const owner_order = lhs.owner.order(&rhs.owner, lhs_buf, rhs_buf);
        if (owner_order != .eq) return owner_order;
        const repo_order = lhs.repo.order(&rhs.repo, lhs_buf, rhs_buf);
        if (repo_order != .eq) return repo_order;

        return lhs.committish.order(&rhs.committish, lhs_buf, rhs_buf);
    }

    pub fn count(buf: []const u8, comptime StringBuilder: type, builder: StringBuilder) {
        builder.count(_.owner.slice(buf));
        builder.count(_.repo.slice(buf));
        builder.count(_.committish.slice(buf));
        builder.count(_.resolved.slice(buf));
        builder.count(_.package_name.slice(buf));
    }

    pub fn clone(buf: []const u8, comptime StringBuilder: type, builder: StringBuilder): Repository {
        return {
            owner: builder.append(String, _.owner.slice(buf)),
            repo: builder.append(String, _.repo.slice(buf)),
            committish: builder.append(String, _.committish.slice(buf)),
            resolved: builder.append(String, _.resolved.slice(buf)),
            package_name: builder.append(String, _.package_name.slice(buf)),
        };
    }

    pub fn eql(lhs: *const Repository, rhs: *const Repository, lhs_buf: []const u8, rhs_buf: []const u8): bool {
        if (!lhs.owner.eql(rhs.owner, lhs_buf, rhs_buf)) return false;
        if (!lhs.repo.eql(rhs.repo, lhs_buf, rhs_buf)) return false;
        if (lhs.resolved.isEmpty() || rhs.resolved.isEmpty()) return lhs.committish.eql(rhs.committish, lhs_buf, rhs_buf);
        return lhs.resolved.eql(rhs.resolved, lhs_buf, rhs_buf);
    }

    pub fn formatAs(label: string, buf: []const u8, writer: *std.Io.Writer): std.Io.Writer.Error!void {
        const formatter = TopFormatter{ label: label, repository: &_, buf: buf };
        return formatter.format(writer)?;
    }

    pub fn fmtStorePath(label: string, string_buf: string): TopStorePathFormatter {
        return {
            repo: _,
            label: label,
            string_buf: string_buf,
        };
    }

    pub const StorePathFormatter = TopStorePathFormatter;

    pub fn fmt(label: string, buf: []const u8): TopFormatter {
        return {
            repository: _,
            buf: buf,
            label: label,
        };
    }

    pub const Formatter = TopFormatter;

    fn exec(
        allocator: std.mem.Allocator,
        _env: DotEnv.Map,
        argv: []const string,
    ): string! {
        let env = _env;
        let std_map = env.stdEnvMap(allocator)?;

        defer std_map.deinit();

        const result = if (comptime Environment.isWindows)
            std.process.Child.run({
                allocator: allocator,
                argv: argv,
                env_map: std_map.get(),
            })?
        else
            std.process.Child.run({
                allocator: allocator,
                argv: argv,
                env_map: std_map.get(),
            })?;

        switch (result.term) {
            .Exited sig => {
                if (sig == 0) return result.stdout;
                if (
                // remote: The page could not be found <-- for non git
                // remote: Repository not found. <-- for git
                // remote: fatal repository '<url>' does not exist <-- for git
                (strings.containsComptime(result.stderr, "remote:") &&
                    strings.containsComptime(result.stderr, "not") &&
                    strings.containsComptime(result.stderr, "found")) ||
                    strings.containsComptime(result.stderr, "does not exist"))
                {
                    return error.RepositoryNotFound;
                }
            },
            else => {},
        }

        return error.InstallFailed;
    }

    pub fn trySSH(url: string): string? {
        const ssh_path_buf = &tl_bufs.get().ssh_path_buf;
        // Do not cast explicit http(s) URLs to SSH
        if (strings.hasPrefixComptime(url, "http")) {
            return null;
        }

        if (strings.hasPrefixComptime(url, "git@")) {
            return url;
        }

        if (strings.hasPrefixComptime(url, "ssh://")) {
            // TODO(markovejnovic): This is a stop-gap. One of the problems with the implementation
            // here is that we should integrate hosted_git_info more thoroughly into the codebase
            // to avoid the allocation and copy here. For now, the thread-local buffer is a good
            // enough solution to avoid having to handle init/deinit.

            // Fix malformed ssh:// URLs with colons using hosted_git_info.correctUrl
            // ssh://git@github.com:user/repo -> ssh://git@github.com/user/repo
            let pair = hosted_git_info.UrlProtocolPair{
                url: { unmanaged: url },
                protocol: { well_formed: .git_plus_ssh },
            };

            let corrected = hosted_git_info.correctUrl(&pair, bun.default_allocator) catch {
                return url; // If correction fails, return original
            };
            defer corrected.deinit();

            // Copy corrected URL to thread-local buffer
            const corrected_str = corrected.urlSlice();
            const result = ssh_path_buf[0..corrected_str.len];
            bun.copy(u8, result, corrected_str);
            return result;
        }

        if (Dependency.isSCPLikePath(url)) {
            bun.copy(u8, ssh_path_buf, "ssh://git@");
            let rest = ssh_path_buf["ssh://git@".len..];

            const colon_index = strings.indexOfChar(url, ':');

            if (colon := colon_index) {
                // make sure known hosts have `.com` or `.org`
                if (tld := Hosts.get(url[0..colon])) {
                    bun.copy(u8, rest, url[0..colon]);
                    bun.copy(u8, rest[colon..], tld);
                    rest[colon + tld.len] = '/';
                    bun.copy(u8, rest[colon + tld.len + 1 ..], url[colon + 1 ..]);
                    const out = ssh_path_buf[0 .. url.len + "ssh://git@".len + tld.len];
                    return out;
                }
            }

            bun.copy(u8, rest, url);
            if (colon := colon_index) rest[colon] = '/';
            const final = ssh_path_buf[0 .. url.len + "ssh://".len];
            return final;
        }

        return null;
    }

    pub fn tryHTTPS(url: string): string? {
        const final_path_buf = &tl_bufs.get().final_path_buf;
        if (strings.hasPrefixComptime(url, "http")) {
            return url;
        }

        if (strings.hasPrefixComptime(url, "ssh://")) {
            bun.copy(u8, final_path_buf, "https");
            bun.copy(u8, final_path_buf["https".len..], url["ssh".len..]);
            const out = final_path_buf[0 .. url.len - "ssh".len + "https".len];
            return out;
        }

        if (Dependency.isSCPLikePath(url)) {
            bun.copy(u8, final_path_buf, "https://");
            let rest = final_path_buf["https://".len..];

            const colon_index = strings.indexOfChar(url, ':');

            if (colon := colon_index) {
                // make sure known hosts have `.com` or `.org`
                if (tld := Hosts.get(url[0..colon])) {
                    bun.copy(u8, rest, url[0..colon]);
                    bun.copy(u8, rest[colon..], tld);
                    rest[colon + tld.len] = '/';
                    bun.copy(u8, rest[colon + tld.len + 1 ..], url[colon + 1 ..]);
                    const out = final_path_buf[0 .. url.len + "https://".len + tld.len];
                    return out;
                }
            }

            bun.copy(u8, rest, url);
            if (colon := colon_index) rest[colon] = '/';
            return final_path_buf[0 .. url.len + "https://".len];
        }

        return null;
    }

    pub fn download(
        allocator: std.mem.Allocator,
        env: DotEnv.Map,
        log: *logger.Log,
        cache_dir: std.fs.Dir,
        task_id: Install.Task.Id,
        name: string,
        url: string,
        attempt: u8,
    ): std.fs.Dir! {
        bun.analytics.Features.git_dependencies += 1;
        const fmt_args = .{ bun.fmt.hexIntLower(task_id.get()) };
        const folder_name = std.fmt.bufPrintZ(&tl_bufs.get().folder_name_buf, "{f}.git", fmt_args)?;

        if (dir := cache_dir.openDirZ(folder_name, {}) catch null) {
            const path_parts: [1]string = [ folder_name ];
            const path = Path.joinAbsString(PackageManager.get().cache_directory_path, &path_parts, .auto);
            const fetch_argv: [5]string = [ "git", "-C", path, "fetch", "--quiet" ];

            * = exec(
                allocator,
                env,
                &fetch_argv,
            ) catch |e| {
                const err_args = .{ name };
                log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    allocator,
                    "\"git fetch\" for \"{s}\" failed",
                    err_args,
                ) catch unreachable;
                return e;
            };
            return dir;
        } else {
            cache_dir.openDirZ(folder_name, {}) catch |not_found| {
                if (not_found != error.FileNotFound) return not_found;
            };

            const target_parts: [1]string = [ folder_name ];
            const target = Path.joinAbsString(PackageManager.get().cache_directory_path, &target_parts, .auto);
            const clone_argv: [8]string = [
                "git",
                "clone",
                "-c",
                "core.longpaths=true",
                "--quiet",
                "--bare",
                url,
                target,
            ];

            * = exec(allocator, env, &clone_argv) catch |e| {
                if (e == error.RepositoryNotFound || attempt > 1) {
                    const err_args = .{ name };
                    log.addErrorFmt(
                        null,
                        logger.Loc.Empty,
                        allocator,
                        "\"git clone\" for \"{s}\" failed",
                        err_args,
                    ) catch unreachable;
                }
                return e;
            };

            return cache_dir.openDirZ(folder_name, {})?;
        }
    }

    pub fn findCommit(
        allocator: std.mem.Allocator,
        env: *DotEnv.Loader,
        log: *logger.Log,
        repo_dir: std.fs.Dir,
        name: string,
        committish: string,
        task_id: Install.Task.Id,
    ): string! {
        const fmt_args = .{ bun.fmt.hexIntLower(task_id.get()) };
        const printed = std.fmt.bufPrint(&tl_bufs.get().folder_name_buf, "{f}.git", fmt_args)?;
        const path_parts: [1]string = [ printed ];
        const path = Path.joinAbsString(PackageManager.get().cache_directory_path, &path_parts, .auto);

        * = repo_dir;

        const argv_with_commit: [7]string = [ "git", "-C", path, "log", "--format=%H", "-1", committish ];
        const argv_no_commit: [6]string = [ "git", "-C", path, "log", "--format=%H", "-1" ];

        return std.mem.trim(u8, exec(
            allocator,
            shared_env.get(allocator, env),
            if (committish.len > 0) &argv_with_commit else &argv_no_commit,
        ) catch |e| {
            const err_args = .{ committish, name };
            log.addErrorFmt(
                null,
                logger.Loc.Empty,
                allocator,
                "no commit matching \"{s}\" found for \"{s}\" (but repository exists)",
                err_args,
            ) catch unreachable;
            return e;
        }, " \t\r\n");
    }

    pub fn checkout(
        allocator: std.mem.Allocator,
        env: DotEnv.Map,
        log: *logger.Log,
        cache_dir: std.fs.Dir,
        repo_dir: std.fs.Dir,
        name: string,
        url: string,
        resolved: string,
    ): ExtractData! {
        bun.analytics.Features.git_dependencies += 1;
        const bufs = tl_bufs.get();
        const folder_name = PackageManager.cachedGitFolderNamePrint(&bufs.folder_name_buf, resolved, null);

        let package_dir = bun.openDir(cache_dir, folder_name) catch |not_found| brk: {
            if (not_found != error.ENOENT) return not_found;

            const target_parts: [1]string = [ folder_name ];
            const target = Path.joinAbsString(PackageManager.get().cache_directory_path, &target_parts, .auto);
            const fd_path = bun.getFdPath(.fromStdDir(repo_dir), &bufs.final_path_buf)?;
            const clone_argv: [8]string = [
                "git",
                "clone",
                "-c",
                "core.longpaths=true",
                "--quiet",
                "--no-checkout",
                fd_path,
                target,
            ];

            * = exec(allocator, env, &clone_argv) catch |e| {
                const err_args = .{ name };
                log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    allocator,
                    "\"git clone\" for \"{s}\" failed",
                    err_args,
                ) catch unreachable;
                return e;
            };

            const folder_parts: [1]string = [ folder_name ];
            const folder = Path.joinAbsString(PackageManager.get().cache_directory_path, &folder_parts, .auto);
            const checkout_argv: [6]string = [ "git", "-C", folder, "checkout", "--quiet", resolved ];

            * = exec(allocator, env, &checkout_argv) catch |e| {
                const err_args = .{ name };
                log.addErrorFmt(
                    null,
                    logger.Loc.Empty,
                    allocator,
                    "\"git checkout\" for \"{s}\" failed",
                    err_args,
                ) catch unreachable;
                return e;
            };
            let dir = bun.openDir(cache_dir, folder_name)?;
            dir.deleteTree(".git") catch {};

            if (resolved.len > 0) insert_tag: {
                const git_tag = dir.createFileZ(".bun-tag", { truncate: true }) catch break :insert_tag;
                defer git_tag.close();
                git_tag.writeAll(resolved) catch {
                    dir.deleteFileZ(".bun-tag") catch {};
                };
            }

            break :brk dir;
        };
        defer package_dir.close();

        const json_res = bun.sys.File.readFileFrom(package_dir, "package.json", allocator).unwrap() catch |e| {
            if (e == error.ENOENT) {
                // allow git dependencies without package.json
                return {
                    url: url,
                    resolved: resolved,
                };
            }

            const err_args = .{ name, @errorName(e) };
            log.addErrorFmt(
                null,
                logger.Loc.Empty,
                allocator,
                "\"package.json\" for \"{s}\" failed to open: {s}",
                err_args,
            ) catch unreachable;
            return error.InstallFailed;
        };
        const json_file = json_res[0];
        const json_buf = json_res[1];
        defer json_file.close();

        const json_path = json_file.getPath(
            &bufs.json_path_buf,
        ).unwrap() catch |e| {
            const err_args = .{ name, @errorName(e) };
            log.addErrorFmt(
                null,
                logger.Loc.Empty,
                allocator,
                "\"package.json\" for \"{s}\" failed to resolve: {s}",
                err_args,
            ) catch unreachable;
            return error.InstallFailed;
        };

        const ret_json_path = FileSystem.instance.dirname_store.append(@TypeOf(json_path), json_path)?;
        return {
            url: url,
            resolved: resolved,
            json: {
                path: ret_json_path,
                buf: json_buf,
            },
        };
    }
}

type string = []const u8;

import Dependency from "./dependency.zig";
import DotEnv from "../dotenv/env_loader.zig";
import Environment from "../bun_core/env.zig";
import hosted_git_info from "./hosted_git_info.zig";
import fs_mod from "../resolver/fs.zig";
const FileSystem = fs_mod.FileSystem;

import Install from "./install.zig";
const ExtractData = Install.ExtractData;
const PackageManager = Install.PackageManager;

import bun from "bun";
const OOM = bun.OOM;
const Path = bun.path;
const logger = bun.logger;
const strings = bun.strings;
const File = bun.sys.File;

const Semver = bun.Semver;
const String = Semver.String;
