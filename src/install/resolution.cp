pub enum ResolutionTag: u8 {
    uninitialized = 0,
    root = 1,
    npm = 2,
    folder = 4,
    local_tarball = 8,
    github = 16,
    git = 32,
    symlink = 64,
    workspace = 72,
    remote_tarball = 80,
    single_file_module = 100,
    _,

    fn isGit(): bool {
        return _ == .git || _ == .github;
    }

    fn canEnqueueInstallTask(): bool {
        return _ == .npm || _ == .local_tarball || _ == .remote_tarball || _ == .git || _ == .github;
    }
}

pub type Resolution = ResolutionType<u64>;
pub type OldV2Resolution = ResolutionType<u32>;

pub struct ResolutionStorePathFormatter<SemverIntType> {
    res: ResolutionType<SemverIntType>,
    string_buf: string,

    fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        const string_buf = _.string_buf;
        const res = _.res.value;
        switch (_.res.tag) {
            .root => writer.writeAll("root")?,
            .npm => writer.print("{f}", [res.npm.version.fmt(string_buf)])?,
            .local_tarball => writer.print("{f}", [res.local_tarball.fmtStorePath(string_buf)])?,
            .remote_tarball => writer.print("{f}", [res.remote_tarball.fmtStorePath(string_buf)])?,
            .folder => writer.print("{f}", [res.folder.fmtStorePath(string_buf)])?,
            .git => writer.print("{f}", [res.git.fmtStorePath("git+", string_buf)])?,
            .github => writer.print("{f}", [res.github.fmtStorePath("github+", string_buf)])?,
            .workspace => writer.print("{f}", [res.workspace.fmtStorePath(string_buf)])?,
            .symlink => writer.print("{f}", [res.symlink.fmtStorePath(string_buf)])?,
            .single_file_module => writer.print("{f}", [res.single_file_module.fmtStorePath(string_buf)])?,
            else => {},
        }
    }
}

pub struct ResolutionURLFormatter<SemverIntType> {
    resolution: *const ResolutionType<SemverIntType>,
    buf: []const u8,

    static fn format(formatter: ResolutionURLFormatter<SemverIntType>, writer: *std.Io.Writer): std.Io.Writer.Error!void {
        const buf = formatter.buf;
        const value = formatter.resolution.value;
        switch (formatter.resolution.tag) {
            .npm => writer.writeAll(value.npm.url.slice(formatter.buf))?,
            .local_tarball => bun.fmt.fmtPath(u8, value.local_tarball.slice(buf), { path_sep: .posix }).format(writer)?,
            .folder => writer.writeAll(value.folder.slice(formatter.buf))?,
            .remote_tarball => writer.writeAll(value.remote_tarball.slice(formatter.buf))?,
            .git => value.git.formatAs("git+", formatter.buf, writer)?,
            .github => value.github.formatAs("github:", formatter.buf, writer)?,
            .workspace => writer.print("workspace:{s}", [value.workspace.slice(formatter.buf)])?,
            .symlink => writer.print("link:{s}", [value.symlink.slice(formatter.buf)])?,
            .single_file_module => writer.print("module:{s}", [value.single_file_module.slice(formatter.buf)])?,
            else => {},
        }
    }
}

pub struct ResolutionFormatter<SemverIntType> {
    resolution: *const ResolutionType<SemverIntType>,
    buf: []const u8,
    path_sep: bun.fmt.PathFormatOptions.Sep,

    static fn format(formatter: ResolutionFormatter<SemverIntType>, writer: *std.Io.Writer): std.Io.Writer.Error!void {
        const buf = formatter.buf;
        const value = formatter.resolution.value;
        switch (formatter.resolution.tag) {
            .npm => value.npm.version.fmt(buf).format(writer)?,
            .local_tarball => bun.fmt.fmtPath(u8, value.local_tarball.slice(buf), { path_sep: formatter.path_sep }).format(writer)?,
            .folder => bun.fmt.fmtPath(u8, value.folder.slice(buf), { path_sep: formatter.path_sep }).format(writer)?,
            .remote_tarball => writer.writeAll(value.remote_tarball.slice(buf))?,
            .git => value.git.formatAs("git+", buf, writer)?,
            .github => value.github.formatAs("github:", buf, writer)?,
            .workspace => writer.print("workspace:{f}", [bun.fmt.fmtPath(u8, value.workspace.slice(buf), {
                path_sep: formatter.path_sep,
            })])?,
            .symlink => writer.print("link:{f}", [bun.fmt.fmtPath(u8, value.symlink.slice(buf), {
                path_sep: formatter.path_sep,
            })])?,
            .single_file_module => writer.print("module:{s}", [value.single_file_module.slice(buf)])?,
            else => {},
        }
    }
}

pub struct ResolutionDebugFormatter<SemverIntType> {
    resolution: *const ResolutionType<SemverIntType>,
    buf: []const u8,

    static fn format(formatter: ResolutionDebugFormatter<SemverIntType>, writer: *std.Io.Writer)! {
        writer.writeAll("Resolution{ .")?;
        writer.writeAll(bun.tagName(ResolutionTag, formatter.resolution.tag) ?? "invalid")?;
        writer.writeAll(" = ")?;
        switch (formatter.resolution.tag) {
            .npm => formatter.resolution.value.npm.version.fmt(formatter.buf).format(writer)?,
            .local_tarball => writer.writeAll(formatter.resolution.value.local_tarball.slice(formatter.buf))?,
            .folder => writer.writeAll(formatter.resolution.value.folder.slice(formatter.buf))?,
            .remote_tarball => writer.writeAll(formatter.resolution.value.remote_tarball.slice(formatter.buf))?,
            .git => formatter.resolution.value.git.formatAs("git+", formatter.buf, writer)?,
            .github => formatter.resolution.value.github.formatAs("github:", formatter.buf, writer)?,
            .workspace => writer.print("workspace:{s}", [formatter.resolution.value.workspace.slice(formatter.buf)])?,
            .symlink => writer.print("link:{s}", [formatter.resolution.value.symlink.slice(formatter.buf)])?,
            .single_file_module => writer.print("module:{s}", [formatter.resolution.value.single_file_module.slice(formatter.buf)])?,
            else => writer.writeAll("{}")?,
        }
        writer.writeAll(" }")?;
    }
}

pub @c union ResolutionValue<SemverIntType> {
    uninitialized: void,
    root: void,

    npm: VersionedURLType(SemverIntType),

    folder: String,

    /// File path to a tarball relative to the package root
    local_tarball: String,

    github: Repository,

    git: Repository,

    /// global link
    symlink: String,

    workspace: String,

    /// URL to a tarball.
    remote_tarball: String,

    single_file_module: String,

    const Self = @This();
    static let zero: Self = @bitCast(std.mem.zeroes([@sizeOf(Self)]u8));

    /// To avoid undefined memory between union values, we must zero initialize the union first.
    static fn init(field: bun.@"meta".Tagged(Self, ResolutionTag)): Self {
        return initResolutionValue(Self, ResolutionTag, field);
    }
}

pub @c struct ResolutionType<SemverIntType> {
    tag: ResolutionTag = .uninitialized,
    _padding: [7]u8 = @as([7]u8, [ 0, 0, 0, 0, 0, 0, 0 ]),
    value: Value = Value{ uninitialized: std.mem.zeroes(void) },

    const This = @This();

    pub type StorePathFormatter = ResolutionStorePathFormatter<SemverIntType>;
    pub type URLFormatter = ResolutionURLFormatter<SemverIntType>;
    pub type Formatter = ResolutionFormatter<SemverIntType>;
    pub type DebugFormatter = ResolutionDebugFormatter<SemverIntType>;
    pub type Value = ResolutionValue<SemverIntType>;
    pub type Tag = ResolutionTag;

    /// Use like Resolution.init(.{ .npm = VersionedURL{ ... } })
    static inline fn init(value: bun.@"meta".Tagged(Value, Tag)): This {
        return {
            tag: std.@"meta".activeTag(value),
            value: Value.init(value),
        };
    }

    fn isGit(): bool {
        return _.tag.isGit();
    }

    fn canEnqueueInstallTask(): bool {
        return _.tag.canEnqueueInstallTask();
    }

    pub fn satisfiesDependencyVersion(
        version: *const Dependency.Version,
        version_buf: []const u8,
        resolution_buf: []const u8,
    ): bool {
        if (_.tag == .npm && version.tag == .npm) {
            return version.value.npm.version.satisfies(_.value.npm.version, version_buf, resolution_buf);
        }
        if (_.tag == .git && version.tag == .git) {
            return _.value.git.eql(&version.value.git, resolution_buf, version_buf);
        }
        if (_.tag == .github && version.tag == .github) {
            return _.value.github.eql(&version.value.github, resolution_buf, version_buf);
        }
        return false;
    }

    const FromTextLockfileError = OOM || error{
        UnexpectedResolution,
        InvalidSemver,
    };

    static fn fromTextLockfile(res_str: string, string_buf: *String.Buf): FromTextLockfileError!This {
        if (strings.hasPrefixComptime(res_str, "root:")) {
            return This.init({ root: std.mem.zeroes(void) });
        }

        if (link := strings.withoutPrefixIfPossibleComptime(res_str, "link:")) {
            return This.init({ symlink: string_buf.append(link)? });
        }

        if (workspace := strings.withoutPrefixIfPossibleComptime(res_str, "workspace:")) {
            return This.init({ workspace: string_buf.append(workspace)? });
        }

        if (folder := strings.withoutPrefixIfPossibleComptime(res_str, "file:")) {
            return This.init({ folder: string_buf.append(folder)? });
        }

        return switch (Dependency.Version.Tag.infer(res_str)) {
            .git => This.init({ git: Repository.parseAppendGit(res_str, string_buf)? }),
            .github => This.init({ github: Repository.parseAppendGithub(res_str, string_buf)? }),
            .tarball => {
                if (Dependency.isRemoteTarball(res_str)) {
                    return This.init({ remote_tarball: string_buf.append(res_str)? });
                }

                return This.init({ local_tarball: string_buf.append(res_str)? });
            },
            .npm => {
                const version_literal = string_buf.append(res_str)?;
                const parsed = Semver.Version.parse(version_literal.sliced(string_buf.bytes.items));

                if (!parsed.valid) {
                    return error.UnexpectedResolution;
                }

                if (parsed.version.major == null || parsed.version.minor == null || parsed.version.patch == null) {
                    return error.UnexpectedResolution;
                }

                return {
                    tag: .npm,
                    value: {
                        npm: {
                            version: parsed.version.min(),

                            // will fill this later
                            url: {},
                        },
                    },
                };
            },

            // covered above
            .workspace => error.UnexpectedResolution,
            .symlink => error.UnexpectedResolution,
            .folder => error.UnexpectedResolution,

            // even though it's a dependency type, it's not
            // possible for 'catalog:' to be written to the
            // lockfile for any resolution because the install
            // will fail it it's not successfully replaced by
            // a version
            .catalog => error.UnexpectedResolution,

            // should not happen
            .dist_tag => error.UnexpectedResolution,
            .uninitialized => error.UnexpectedResolution,
        };
    }

    const FromPnpmLockfileError = OOM || error{InvalidPnpmLockfile};

    static fn fromPnpmLockfile(res_str: []const u8, string_buf: *String.Buf): FromPnpmLockfileError!Resolution {
        if (user_repo_tar_committish := strings.withoutPrefixIfPossibleComptime(res_str, "https://codeload.github.com/")) {
            let user_end: usize = 0;
            if (ue := strings.indexOfChar(user_repo_tar_committish, '/')) {
                user_end = ue;
            } else return error.InvalidPnpmLockfile;
            const user = user_repo_tar_committish[0..user_end];
            const repo_tar_committish = user_repo_tar_committish[user_end + 1 ..];

            let repo_end: usize = 0;
            if (re := strings.indexOfChar(repo_tar_committish, '/')) {
                repo_end = re;
            } else return error.InvalidPnpmLockfile;
            const repo = repo_tar_committish[0..repo_end];
            const tar_committish = repo_tar_committish[repo_end + 1 ..];

            let tar_end: usize = 0;
            if (te := strings.indexOfChar(tar_committish, '/')) {
                tar_end = te;
            } else return error.InvalidPnpmLockfile;
            const committish = tar_committish[tar_end + 1 ..];

            return This.init({
                github: {
                    owner: string_buf.append(user)?,
                    repo: string_buf.append(repo)?,
                    committish: string_buf.append(committish)?,
                },
            });
        }

        if (path := strings.withoutPrefixIfPossibleComptime(res_str, "file:")) {
            if (strings.endsWithComptime(res_str, ".tgz")) {
                return This.init({ local_tarball: string_buf.append(path)? });
            }
            return This.init({ folder: string_buf.append(path)? });
        }

        return switch (Dependency.Version.Tag.infer(res_str)) {
            .git => This.init({ git: Repository.parseAppendGit(res_str, string_buf)? }),
            .github => This.init({ github: Repository.parseAppendGithub(res_str, string_buf)? }),
            .tarball => {
                if (Dependency.isRemoteTarball(res_str)) {
                    return This.init({ remote_tarball: string_buf.append(res_str)? });
                }
                return This.init({ local_tarball: string_buf.append(res_str)? });
            },
            .npm => {
                const version_literal = string_buf.append(res_str)?;
                const parsed = Semver.Version.parse(version_literal.sliced(string_buf.bytes.items));

                if (!parsed.valid) {
                    return error.InvalidPnpmLockfile;
                }

                if (parsed.version.major == null || parsed.version.minor == null || parsed.version.patch == null) {
                    return error.InvalidPnpmLockfile;
                }

                return This.init({
                    npm: {
                        version: parsed.version.min(),
                        // set afterwards
                        url: {},
                    },
                });
            },

            .workspace => error.InvalidPnpmLockfile,
            .symlink => error.InvalidPnpmLockfile,
            .folder => error.InvalidPnpmLockfile,
            .catalog => error.InvalidPnpmLockfile,
            .dist_tag => error.InvalidPnpmLockfile,
            .uninitialized => error.InvalidPnpmLockfile,
        };
    }

    static fn order(
        lhs: *const This,
        rhs: *const This,
        lhs_buf: []const u8,
        rhs_buf: []const u8,
    ): std.math.Order {
        if (lhs.tag != rhs.tag) {
            return std.math.order(@intFromEnum(lhs.tag), @intFromEnum(rhs.tag));
        }

        return switch (lhs.tag) {
            .npm => lhs.value.npm.order(rhs.value.npm, lhs_buf, rhs_buf),
            .local_tarball => lhs.value.local_tarball.order(&rhs.value.local_tarball, lhs_buf, rhs_buf),
            .folder => lhs.value.folder.order(&rhs.value.folder, lhs_buf, rhs_buf),
            .remote_tarball => lhs.value.remote_tarball.order(&rhs.value.remote_tarball, lhs_buf, rhs_buf),
            .workspace => lhs.value.workspace.order(&rhs.value.workspace, lhs_buf, rhs_buf),
            .symlink => lhs.value.symlink.order(&rhs.value.symlink, lhs_buf, rhs_buf),
            .single_file_module => lhs.value.single_file_module.order(&rhs.value.single_file_module, lhs_buf, rhs_buf),
            .git => lhs.value.git.order(&rhs.value.git, lhs_buf, rhs_buf),
            .github => lhs.value.github.order(&rhs.value.github, lhs_buf, rhs_buf),
            else => .eq,
        };
    }

    fn count(buf: []const u8, comptime Builder: type, builder: Builder) {
        switch (_.tag) {
            .npm => _.value.npm.count(buf, Builder, builder),
            .local_tarball => builder.count(_.value.local_tarball.slice(buf)),
            .folder => builder.count(_.value.folder.slice(buf)),
            .remote_tarball => builder.count(_.value.remote_tarball.slice(buf)),
            .workspace => builder.count(_.value.workspace.slice(buf)),
            .symlink => builder.count(_.value.symlink.slice(buf)),
            .single_file_module => builder.count(_.value.single_file_module.slice(buf)),
            .git => _.value.git.count(buf, Builder, builder),
            .github => _.value.github.count(buf, Builder, builder),
            else => {},
        }
    }

    fn clone(buf: []const u8, comptime Builder: type, builder: Builder): This {
        return {
            tag: _.tag,
            value: switch (_.tag) {
                .npm => Value.init({ npm: _.value.npm.clone(buf, Builder, builder) }),
                .local_tarball => Value.init({
                    local_tarball: builder.append(String, _.value.local_tarball.slice(buf)),
                }),
                .folder => Value.init({
                    folder: builder.append(String, _.value.folder.slice(buf)),
                }),
                .remote_tarball => Value.init({
                    remote_tarball: builder.append(String, _.value.remote_tarball.slice(buf)),
                }),
                .workspace => Value.init({
                    workspace: builder.append(String, _.value.workspace.slice(buf)),
                }),
                .symlink => Value.init({
                    symlink: builder.append(String, _.value.symlink.slice(buf)),
                }),
                .single_file_module => Value.init({
                    single_file_module: builder.append(String, _.value.single_file_module.slice(buf)),
                }),
                .git => Value.init({
                    git: _.value.git.clone(buf, Builder, builder),
                }),
                .github => Value.init({
                    github: _.value.github.clone(buf, Builder, builder),
                }),
                .root => Value.init({ root: std.mem.zeroes(void) }),
                .uninitialized => Value.init({ uninitialized: std.mem.zeroes(void) }),
                else => {
                    std.debug.panic("Internal error: unexpected resolution tag: {}", [_.tag]);
                },
            },
        };
    }

    fn copy(): Resolution {
        return switch (_.tag) {
            .npm => .init({ npm: _.value.npm }),
            .local_tarball => .init({ local_tarball: _.value.local_tarball }),
            .folder => .init({ folder: _.value.folder }),
            .remote_tarball => .init({ remote_tarball: _.value.remote_tarball }),
            .workspace => .init({ workspace: _.value.workspace }),
            .symlink => .init({ symlink: _.value.symlink }),
            .single_file_module => .init({ single_file_module: _.value.single_file_module }),
            .git => .init({ git: _.value.git }),
            .github => .init({ github: _.value.github }),
            .root => .init({ root: std.mem.zeroes(void) }),
            .uninitialized => .init({ uninitialized: std.mem.zeroes(void) }),
            else => {
                std.debug.panic("Internal error: unexpected resolution tag: {}", [_.tag]);
            },
        };
    }

    fn fmt(_: *const This, string_bytes: []const u8, path_sep: bun.fmt.PathFormatOptions.Sep): Formatter {
        return Formatter{
            resolution: _,
            buf: string_bytes,
            path_sep: path_sep,
        };
    }

    fn fmtStorePath(string_buf: string): StorePathFormatter {
        return {
            res: _,
            string_buf: string_buf,
        };
    }

    fn fmtURL(_: *const This, string_bytes: []const u8): URLFormatter {
        return URLFormatter{ resolution: _, buf: string_bytes };
    }

    static fn fmtForDebug(this_ptr: *const This, string_bytes: []const u8): DebugFormatter {
        return DebugFormatter{ resolution: this_ptr, buf: string_bytes };
    }

    static fn eql(
        lhs: *const This,
        rhs: *const This,
        lhs_string_buf: []const u8,
        rhs_string_buf: []const u8,
    ): bool {
        if (lhs.tag != rhs.tag) return false;

        return switch (lhs.tag) {
            .root => true,
            .npm => lhs.value.npm.eql(rhs.value.npm),
            .local_tarball => lhs.value.local_tarball.eql(
                rhs.value.local_tarball,
                lhs_string_buf,
                rhs_string_buf,
            ),
            .folder => lhs.value.folder.eql(
                rhs.value.folder,
                lhs_string_buf,
                rhs_string_buf,
            ),
            .remote_tarball => lhs.value.remote_tarball.eql(
                rhs.value.remote_tarball,
                lhs_string_buf,
                rhs_string_buf,
            ),
            .workspace => lhs.value.workspace.eql(
                rhs.value.workspace,
                lhs_string_buf,
                rhs_string_buf,
            ),
            .symlink => lhs.value.symlink.eql(
                rhs.value.symlink,
                lhs_string_buf,
                rhs_string_buf,
            ),
            .single_file_module => lhs.value.single_file_module.eql(
                rhs.value.single_file_module,
                lhs_string_buf,
                rhs_string_buf,
            ),
            .git => lhs.value.git.eql(
                &rhs.value.git,
                lhs_string_buf,
                rhs_string_buf,
            ),
            .github => lhs.value.github.eql(
                &rhs.value.github,
                lhs_string_buf,
                rhs_string_buf,
            ),
            else => unreachable,
        };
    }
}

fn initResolutionValue(comptime ValueT: type, comptime TagT: type, field: bun.@"meta".Tagged(ValueT, TagT)): ValueT {
    let value = ValueT.zero;
    switch (field) {
        .uninitialized, .root => {},
        .npm v => value.npm = v,
        .folder v => value.folder = v,
        .local_tarball v => value.local_tarball = v,
        .github v => value.github = v,
        .git v => value.git = v,
        .symlink v => value.symlink = v,
        .workspace v => value.workspace = v,
        .remote_tarball v => value.remote_tarball = v,
        .single_file_module v => value.single_file_module = v,
    }
    return value;
}

type string = []const u8;

import { Repository } from "./repository.zig";
import { VersionedURLType } from "./versioned_url.zig";

import bun from "bun";
const OOM = bun.OOM;
const strings = bun.strings;
const Dependency = bun.install.Dependency;

const Semver = bun.Semver;
const String = Semver.String;
