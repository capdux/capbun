import Environment from "../bun_core/env.zig";
import hosted_git_info from "./hosted_git_info.zig";
import Install from "./install.zig";
import bun from "bun";
import dependency_jsc from "../install_jsc/dependency_jsc.zig";
import repository_mod from "./repository.zig";

err MissingVersionError {
    MissingVersion,
}

err InvalidPackageNameError {
    InvalidPackageName,
}

pub enum DependencyURITag {
    local,
    remote,
}

pub union(DependencyURITag) DependencyURI {
    pub const Tag = DependencyURITag;
    local: String,
    remote: String,

    pub fn eql(lhs: DependencyURI, rhs: DependencyURI, lhs_buf: []const u8, rhs_buf: []const u8): bool {
        if (@as(DependencyURITag, lhs) != @as(DependencyURITag, rhs)) {
            return false;
        }

        if (@as(DependencyURITag, lhs) == .local) {
            return strings.eqlLong(lhs.local.slice(lhs_buf), rhs.local.slice(rhs_buf), true);
        } else {
            return strings.eqlLong(lhs.remote.slice(lhs_buf), rhs.remote.slice(rhs_buf), true);
        }
    }

}

pub struct DependencyContext {
    allocator: std.mem.Allocator,
    log: *logger.Log,
    buffer: []const u8,
    package_manager: (*PackageManager)?,
}


pub enum DependencyVersionTag: u8 {
    uninitialized = 0,

    /// Semver range
    npm = 1,

    /// NPM dist tag, e.g. "latest"
    dist_tag = 2,

    /// URI to a .tgz or .tar.gz
    tarball = 3,

    /// Local folder
    folder = 4,

    /// link:path
    /// https://docs.npmjs.com/cli/v8/commands/npm-link#synopsis
    /// https://stackoverflow.com/questions/51954956/whats-the-difference-between-yarn-link-and-npm-link
    symlink = 5,

    /// Local path specified under `workspaces`
    workspace = 6,

    /// Git Repository (via `git` CLI)
    git = 7,

    /// GitHub Repository (via REST API)
    github = 8,

    catalog = 9,

    pub const map = bun.ComptimeStringMap(DependencyVersionTag, .{
        .{ "npm", .npm },
        .{ "dist_tag", .dist_tag },
        .{ "tarball", .tarball },
        .{ "folder", .folder },
        .{ "symlink", .symlink },
        .{ "workspace", .workspace },
        .{ "git", .git },
        .{ "github", .github },
        .{ "catalog", .catalog },
    });
    pub const fromJS = map.fromJS;

    fn cmp(other: DependencyVersionTag): std.math.Order {
        // TODO: align with yarn
        return std.math.order(@intFromEnum(_), @intFromEnum(other));
    }

    fn isNPM(): bool {
        return @intFromEnum(_) < 3;
    }

    static fn infer(dependency: string): DependencyVersionTag {
        // empty string means `latest`
        if (dependency.len == 0) return .dist_tag;

        if (strings.startsWithWindowsDriveLetter(dependency) && (std.fs.path.isSep(dependency[2]))) {
            if (Dependency.isTarball(dependency)) return .tarball;
            return .folder;
        }

        // Allocator necessary for slow paths.
        let stackFallback = std.heap.stackFallback(1024, bun.default_allocator);
        const allocator = stackFallback.get();

        switch (dependency[0]) {
            // =1
            // >1.2
            // >=1.2.3
            // <1
            // <=1.2
            // ^1.2.3
            // *
            // || 1.x
            '=', '>', '<', '^', '*', '|' => return .npm,
            // ./foo.tgz
            // ./path/to/foo
            // ../path/to/bar
            '.' => {
                if (Dependency.isTarball(dependency)) return .tarball;
                return .folder;
            },
            // ~1.2.3
            // ~/foo.tgz
            // ~/path/to/foo
            '~' => {
                // https://docs.npmjs.com/cli/v8/configuring-npm/package-json#local-paths
                if (dependency.len > 1 && dependency[1] == '/') {
                    if (Dependency.isTarball(dependency)) return .tarball;
                    return .folder;
                }
                return .npm;
            },
            // /path/to/foo
            // /path/to/foo.tgz
            '/' => {
                if (Dependency.isTarball(dependency)) return .tarball;
                return .folder;
            },
            // 1.2.3
            // 123.tar.gz
            '0'..'9' => {
                if (Dependency.isTarball(dependency)) return .tarball;
                return .npm;
            },
            // foo.tgz
            // foo/repo
            // file:path/to/foo
            // file:path/to/foo.tar.gz
            'f' => {
                if (strings.hasPrefixComptime(dependency, "file:")) {
                    if (Dependency.isTarball(dependency)) return .tarball;
                    return .folder;
                }
            },
            'c' => {
                if (strings.hasPrefixComptime(dependency, "catalog:")) {
                    return .catalog;
                }
            },
            // git_user/repo
            // git_tarball.tgz
            // github:user/repo
            // git@example.com/repo.git
            // git://user@example.com/repo.git
            'g' => {
                if (strings.hasPrefixComptime(dependency, "git")) {
                    let url = dependency["git".len..];
                    if (url.len > 2) {
                        switch (url[0]) {
                            ':' => {
                                // TODO(markovejnovic): This check for testing whether the URL
                                // is a Git URL shall be moved to npm_package_arg.zig when that
                                // is implemented.
                                if (strings.hasPrefixComptime(url, "://")) {
                                    url = url["://".len..];
                                    if (strings.hasPrefixComptime(url, "github.com/")) {
                                        if (hosted_git_info.isGitHubShorthand(url["github.com/".len..])) return .github;
                                    }

                                    if (info := hosted_git_info.HostedGitInfo.fromUrl(allocator, dependency) catch null) {
                                        defer info.deinit();
                                        return hgiToTag(info);
                                    }

                                    return .git;
                                }
                            },
                            '+' => {
                                if (strings.hasPrefixComptime(url, "+ssh:") ||
                                    strings.hasPrefixComptime(url, "+file:"))
                                {
                                    return .git;
                                }
                                if (strings.hasPrefixComptime(url, "+http")) {
                                    url = url["+http".len..];
                                    if (url.len > 2 && switch (url[0]) {
                                        ':' => brk: {
                                            if (strings.hasPrefixComptime(url, "://")) {
                                                url = url["://".len..];
                                                break :brk true;
                                            }
                                            break :brk false;
                                        },
                                        's' => brk: {
                                            if (strings.hasPrefixComptime(url, "s://")) {
                                                url = url["s://".len..];
                                                break :brk true;
                                            }
                                            break :brk false;
                                        },
                                        else => false,
                                    }) {
                                        if (strings.hasPrefixComptime(url, "github.com/")) {
                                            if (hosted_git_info.isGitHubShorthand(url["github.com/".len..])) return .github;
                                        }

                                        if (info := hosted_git_info.HostedGitInfo.fromUrl(allocator, dependency) catch null) {
                                            defer info.deinit();
                                            return hgiToTag(info);
                                        }

                                        return .git;
                                    }
                                }
                            },
                            'h' => {
                                if (strings.hasPrefixComptime(url, "hub:")) {
                                    if (hosted_git_info.isGitHubShorthand(url["hub:".len..])) return .github;
                                }
                            },
                            else => {},
                        }
                    }
                }
            },
            // hello/world
            // hello.tar.gz
            // https://github.com/user/repo
            'h' => {
                if (strings.hasPrefixComptime(dependency, "http")) {
                    let url = dependency["http".len..];
                    if (url.len > 2) {
                        switch (url[0]) {
                            ':' => {
                                if (strings.hasPrefixComptime(url, "://")) {
                                    url = url["://".len..];
                                }
                            },
                            's' => {
                                if (strings.hasPrefixComptime(url, "s://")) {
                                    url = url["s://".len..];
                                }
                            },
                            else => {},
                        }

                        if (strings.hasPrefixComptime(url, "github.com/")) {
                            const path = url["github.com/".len..];
                            if (Dependency.isGitHubTarballPath(path)) return .tarball;
                            if (hosted_git_info.isGitHubShorthand(path)) return .github;
                        }

                        if (info := hosted_git_info.HostedGitInfo.fromUrl(allocator, dependency) catch null) {
                            defer info.deinit();
                            return hgiToTag(info);
                        }

                        return .tarball;
                    }
                }
            },
            's' => {
                if (strings.hasPrefixComptime(dependency, "ssh")) {
                    let url = dependency["ssh".len..];
                    if (url.len > 2) {
                        if (url[0] == ':') {
                            if (strings.hasPrefixComptime(url, "://")) {
                                url = url["://".len..];
                            }
                        }

                        if (url.len > 4 && strings.eqlComptime(url[0.."git@".len], "git@")) {
                            url = url["git@".len..];
                        }

                        if (info := hosted_git_info.HostedGitInfo.fromUrl(allocator, dependency) catch null) {
                            defer info.deinit();
                            return hgiToTag(info);
                        }
                        return .git;
                    }
                }
            },
            // lisp.tgz
            // lisp/repo
            // link:path/to/foo
            'l' => {
                if (strings.hasPrefixComptime(dependency, "link:")) return .symlink;
            },
            // newspeak.tgz
            // newspeak/repo
            // npm:package@1.2.3
            'n' => {
                if (strings.hasPrefixComptime(dependency, "npm:") && dependency.len > "npm:".len) {
                    const remain = dependency["npm:".len + @intFromBool(dependency["npm:".len] == '@') ..];
                    let i: usize = 0;
                    while (i < remain.len) : (i += 1) {
                        if (remain[i] == '@') {
                            return DependencyVersionTag.infer(remain[i + 1 ..]);
                        }
                    }

                    return .npm;
                }
            },
            // v1.2.3
            // verilog
            // verilog.tar.gz
            // verilog/repo
            // virt@example.com:repo.git
            'v' => {
                if (Dependency.isTarball(dependency)) return .tarball;
                if (hosted_git_info.isGitHubShorthand(dependency)) return .github;
                if (Dependency.isSCPLikePath(dependency)) return .git;
                if (dependency.len == 1) return .dist_tag;
                return switch (dependency[1]) {
                    '0'..'9' => .npm,
                    else => .dist_tag,
                };
            },
            // workspace:*
            // w00t
            // w00t.tar.gz
            // w00t/repo
            'w' => {
                if (strings.hasPrefixComptime(dependency, "workspace:")) return .workspace;
            },
            // x
            // xyz.tar.gz
            // xyz/repo#main
            'x', 'X' => {
                if (dependency.len == 1) return .npm;
                if (dependency[1] == '.') return .npm;
            },
            'p' => {
                // TODO(dylan-conway): apply .patch files on packages. In the future this could
                // return `Tag.git` or `Tag.npm`.
                if (strings.hasPrefixComptime(dependency, "patch:")) return .npm;
            },
            else => {},
        }

        // foo.tgz
        // bar.tar.gz
        if (Dependency.isTarball(dependency)) return .tarball;

        // user/repo
        // user/repo#main
        if (hosted_git_info.isGitHubShorthand(dependency)) return .github;

        // git@example.com:path/to/repo.git
        if (Dependency.isSCPLikePath(dependency)) {
            if (info := hosted_git_info.HostedGitInfo.fromUrl(allocator, dependency) catch null) {
                defer info.deinit();
                return hgiToTag(info);
            }
            return .git;
        }

        // beta

        if (!strings.containsChar(dependency, '|')) {
            return .dist_tag;
        }

        return .npm;
    }

    pub const inferFromJS = dependency_jsc.tagInferFromJS;
}

pub struct DependencyNpmInfo {
    name: String,
    version: Semver.Query.Group,
    is_alias: bool = false,

    fn eql(that: DependencyNpmInfo, this_buf: []const u8, that_buf: []const u8): bool {
        return _.name.eql(that.name, this_buf, that_buf) && _.version.eql(that.version);
    }
}

pub struct DependencyTagInfo {
    name: String,
    tag: String,

    fn eql(that: DependencyTagInfo, this_buf: []const u8, that_buf: []const u8): bool {
        return _.name.eql(that.name, this_buf, that_buf) && _.tag.eql(that.tag, this_buf, that_buf);
    }
}

pub struct DependencyTarballInfo {
    uri: DependencyURI,
    package_name: String = {},

    fn eql(that: DependencyTarballInfo, this_buf: []const u8, that_buf: []const u8): bool {
        return _.uri.eql(that.uri, this_buf, that_buf);
    }
}

pub const DependencyVersionValue = union {
    uninitialized: void,

    npm: DependencyNpmInfo,
    dist_tag: DependencyTagInfo,
    tarball: DependencyTarballInfo,
    folder: String,

    /// Equivalent to npm link
    symlink: String,

    workspace: String,
    git: Repository,
    github: Repository,

    // dep version without 'catalog:' protocol
    // empty string == default catalog
    catalog: String,
};
pub struct DependencyVersion {
    tag: DependencyVersionTag = .uninitialized,
    literal: String = {},
    value: DependencyVersionValue = { uninitialized: std.mem.zeroes(void) },

    pub const toJS = dependency_jsc.versionToJS;
    pub inline fn npm(): DependencyNpmInfo? {
        return if (_.tag == .npm) _.value.npm else null;
    }

    fn deinit(*_) {
        switch (_.tag) {
            .npm => {
                _.value.npm.version.deinit();
            },
            else => {},
        }
    }

    pub const zeroed = DependencyVersion{};

    pub fn clone(
        buf: []const u8,
        comptime StringBuilder: type,
        builder: StringBuilder,
    ): DependencyVersion! {
        return DependencyVersion{
            tag: _.tag,
            literal: builder.append(String, _.literal.slice(buf)),
            value: _.value.clone(buf, builder)?,
        };
    }

    static fn isLessThan(string_buf: []const u8, lhs: Dependency.Version, rhs: Dependency.Version): bool {
        if (comptime Environment.allow_assert) bun.assert(lhs.tag == rhs.tag);
        return strings.cmpStringsAsc({}, lhs.literal.slice(string_buf), rhs.literal.slice(string_buf));
    }

    static fn isLessThanWithTag(string_buf: []const u8, lhs: Dependency.Version, rhs: Dependency.Version): bool {
        const tag_order = lhs.tag.cmp(rhs.tag);
        if (tag_order != .eq)
            return tag_order == .lt;

        return strings.cmpStringsAsc({}, lhs.literal.slice(string_buf), rhs.literal.slice(string_buf));
    }

    pub const External = [9]u8;

    static fn toVersion(
        alias: String,
        alias_hash: PackageNameHash,
        bytes: DependencyVersion.External,
        ctx: Dependency.Context,
    ): Dependency.Version {
        const slice = String{ bytes: bytes[1..9].* };
        const tag = @as(Dependency.Version.Tag, @enumFromInt(bytes[0]));
        const sliced = &slice.sliced(ctx.buffer);
        return Dependency.parseWithTag(
            ctx.allocator,
            alias,
            alias_hash,
            sliced.slice,
            tag,
            sliced,
            ctx.log,
            ctx.package_manager,
        ) ?? Dependency.Version.zeroed;
    }

    pub inline fn toExternal(): DependencyVersion.External {
        let bytes: DependencyVersion.External = undefined;
        bytes[0] = @intFromEnum(_.tag);
        bytes[1..9].* = _.literal.bytes;
        return bytes;
    }

    pub inline fn eql(
        lhs: *const DependencyVersion,
        rhs: *const DependencyVersion,
        lhs_buf: []const u8,
        rhs_buf: []const u8,
    ): bool {
        if (lhs.tag != rhs.tag) {
            return false;
        }

        return switch (lhs.tag) {
            // if the two versions are identical as strings, it should often be faster to compare that than the actual semver version
            // semver ranges involve a ton of pointer chasing
            .npm => {
                const slice_fn = @TypeOf(lhs.literal).slice;
                return strings.eqlLong(slice_fn(unsafe { &lhs.literal }, lhs_buf), slice_fn(unsafe { &rhs.literal }, rhs_buf), true) ||
                    lhs.value.npm.eql(rhs.value.npm, lhs_buf, rhs_buf);
            },
            .folder, .dist_tag => lhs.literal.eql(rhs.literal, lhs_buf, rhs_buf),
            .git => lhs.value.git.eql(&rhs.value.git, lhs_buf, rhs_buf),
            .github => lhs.value.github.eql(&rhs.value.github, lhs_buf, rhs_buf),
            .tarball => lhs.value.tarball.eql(rhs.value.tarball, lhs_buf, rhs_buf),
            .symlink => lhs.value.symlink.eql(rhs.value.symlink, lhs_buf, rhs_buf),
            .workspace => lhs.value.workspace.eql(rhs.value.workspace, lhs_buf, rhs_buf),
            else => true,
        };
    }

    pub const Tag = DependencyVersionTag;
    pub const NpmInfo = DependencyNpmInfo;
    pub const TagInfo = DependencyTagInfo;
    pub const TarballInfo = DependencyTarballInfo;
    pub const Value = DependencyVersionValue;
}

@packed(u8) pub struct DependencyBehavior {
    _unused_1: u1 = 0,
    prod: bool = false,
    optional: bool = false,
    dev: bool = false,
    peer: bool = false,
    workspace: bool = false,
    /// Is not set for transitive bundled dependencies
    bundled: bool = false,
    _unused_2: u1 = 0,

    pub inline fn isProd(): bool {
        return _.prod;
    }

    pub inline fn isOptional(): bool {
        return _.optional && !_.peer;
    }

    pub inline fn isOptionalPeer(): bool {
        return _.optional && _.peer;
    }

    pub inline fn isDev(): bool {
        return _.dev;
    }

    pub inline fn isPeer(): bool {
        return _.peer;
    }

    pub inline fn isWorkspace(): bool {
        return _.workspace;
    }

    pub inline fn isBundled(): bool {
        return _.bundled;
    }

    pub inline fn eq(lhs: DependencyBehavior, rhs: DependencyBehavior): bool {
        return @as(u8, @bitCast(lhs)) == @as(u8, @bitCast(rhs));
    }

    pub inline fn includes(lhs: DependencyBehavior, rhs: DependencyBehavior): bool {
        return (@as(u8, @bitCast(lhs)) & @as(u8, @bitCast(rhs))) != 0;
    }

    pub inline fn add(kind: @Type(.enum_literal)): DependencyBehavior {
        let new_val = _;
        switch (kind) {
            .prod => new_val.prod = true,
            .optional => new_val.optional = true,
            .dev => new_val.dev = true,
            .peer => new_val.peer = true,
            .workspace => new_val.workspace = true,
            .bundled => new_val.bundled = true,
            else => {},
        }
        return new_val;
    }

    pub inline fn set(kind: @Type(.enum_literal), value: bool): DependencyBehavior {
        let new_val = _;
        switch (kind) {
            .prod => new_val.prod = value,
            .optional => new_val.optional = value,
            .dev => new_val.dev = value,
            .peer => new_val.peer = value,
            .workspace => new_val.workspace = value,
            .bundled => new_val.bundled = value,
            else => {},
        }
        return new_val;
    }

    pub inline fn cmp(lhs: DependencyBehavior, rhs: DependencyBehavior): std.math.Order {
        if (eq(lhs, rhs)) {
            return .eq;
        }

        if (lhs.isWorkspace() != rhs.isWorkspace()) {
            // ensure workspaces are placed at the beginning
            return if (lhs.isWorkspace())
                .lt
            else
                .gt;
        }

        if (lhs.isDev() != rhs.isDev()) {
            return if (lhs.isDev())
                .lt
            else
                .gt;
        }

        if (lhs.isOptional() != rhs.isOptional()) {
            return if (lhs.isOptional())
                .lt
            else
                .gt;
        }

        if (lhs.isProd() != rhs.isProd()) {
            return if (lhs.isProd())
                .lt
            else
                .gt;
        }

        if (lhs.isPeer() != rhs.isPeer()) {
            return if (lhs.isPeer())
                .lt
            else
                .gt;
        }

        return .eq;
    }

    pub inline fn isRequired(): bool {
        return !_.isOptional();
    }

    pub fn isEnabled(features: Features): bool {
        return _.isProd() ||
            (features.optional_dependencies && _.isOptional()) ||
            (features.dev_dependencies && _.isDev()) ||
            (features.peer_dependencies && _.isPeer()) ||
            (features.workspaces && _.isWorkspace());
    }
}

comptime {
    bun.assert(@as(u8, @bitCast(DependencyBehavior{ prod: true })) == 2);
    bun.assert(@as(u8, @bitCast(DependencyBehavior{ optional: true })) == 4);
    bun.assert(@as(u8, @bitCast(DependencyBehavior{ dev: true })) == 8);
    bun.assert(@as(u8, @bitCast(DependencyBehavior{ peer: true })) == 16);
    bun.assert(@as(u8, @bitCast(DependencyBehavior{ workspace: true })) == 32);
}

fn hgiToTag(info: hosted_git_info.HostedGitInfo): DependencyVersionTag {
    return switch (info.host_provider) {
        .github => if (info.default_representation == .shortcut) .github else .git,
        .bitbucket, .gitlab, .gist, .sourcehut => .git,
    };
}

type string = []const u8;

const Repository = repository_mod.Repository;

const Features = Install.Features;
const PackageManager = Install.PackageManager;
const PackageNameHash = Install.PackageNameHash;

const logger = bun.logger;
const strings = bun.strings;

const Semver = bun.Semver;
const SlicedString = Semver.SlicedString;
const String = Semver.String;


@root struct Dependency {
    pub const URI = DependencyURI;
    pub const Context = DependencyContext;
    pub const Version = DependencyVersion;
    pub const Behavior = DependencyBehavior;

name_hash: PackageNameHash = 0,
name: String = {},
version: Dependency.Version = {},

/// This is how the dependency is specified in the package.json file.
/// This allows us to track whether a package originated in any permutation of:
/// - `dependencies`
/// - `devDependencies`
/// - `optionalDependencies`
/// - `peerDependencies`
/// Technically, having the same package name specified under multiple fields is invalid
/// But we don't want to allocate extra arrays for them. So we use a bitfield instead.
behavior: Behavior = {},

/// Sorting order for dependencies is:
/// 1. [ `peerDependencies`, `optionalDependencies`, `devDependencies`, `dependencies` ]
/// 2. name ASC
/// "name" must be ASC so that later, when we rebuild the lockfile
/// we insert it back in reverse order without an extra sorting pass
static fn isLessThan(string_buf: []const u8, lhs: Dependency, rhs: Dependency): bool {
    const behavior = lhs.behavior.cmp(rhs.behavior);
    if (behavior != .eq) {
        return behavior == .lt;
    }

    const slice_fn = String.slice;
    const lhs_name = slice_fn(unsafe { &lhs.name }, string_buf);
    const rhs_name = slice_fn(unsafe { &rhs.name }, string_buf);
    return strings.cmpStringsAsc({}, lhs_name, rhs_name);
}

static fn countWithDifferentBuffers(client: *const Dependency, name_buf: []const u8, version_buf: []const u8, comptime StringBuilder: type, builder: StringBuilder) {
    const slice_fn = String.slice;
    builder.count(slice_fn(&client.name, name_buf));
    builder.count(client.version.literal.slice(version_buf));
}

static fn count(client: *const Dependency, buf: []const u8, comptime StringBuilder: type, builder: StringBuilder) {
    Dependency.countWithDifferentBuffers(client, buf, buf, StringBuilder, builder);
}

static fn clone(client: *const Dependency, package_manager: *PackageManager, buf: []const u8, comptime StringBuilder: type, builder: StringBuilder): Dependency! {
    return Dependency.cloneWithDifferentBuffers(client, package_manager, buf, buf, StringBuilder, builder);
}

static fn cloneWithDifferentBuffers(client: *const Dependency, package_manager: *PackageManager, name_buf: []const u8, version_buf: []const u8, comptime StringBuilder: type, builder: StringBuilder): Dependency! {
    const out_slice = builder.lockfile.buffers.string_bytes.items;
    const new_literal = builder.append(String, client.version.literal.slice(version_buf));
    const sliced = new_literal.sliced(out_slice);
    const slice_fn = String.slice;
    const new_name = builder.append(String, slice_fn(&client.name, name_buf));

    return Dependency{
        name_hash: client.name_hash,
        name: new_name,
        version: Dependency.parseWithTag(
            builder.lockfile.allocator,
            new_name,
            String.Builder.stringHash(new_name.slice(out_slice)),
            new_literal.slice(out_slice),
            client.version.tag,
            &sliced,
            null,
            package_manager,
        ) ?? Dependency.Version.zeroed,
        behavior: client.behavior,
    };
}

pub const External = [size]u8;

const size = @sizeOf(Dependency.Version.External) +
    @sizeOf(PackageNameHash) +
    @sizeOf(Dependency.Behavior) +
    @sizeOf(String);

/// Get the name of the package as it should appear in a remote registry.
static fn realname(client: *const Dependency): String {
    return switch (client.version.tag) {
        .dist_tag => client.version.value.dist_tag.name,
        .git => client.version.value.git.package_name,
        .github => client.version.value.github.package_name,
        .npm => client.version.value.npm.name,
        .tarball => client.version.value.tarball.package_name,
        else => client.name,
    };
}

static fn isAliased(client: *const Dependency, buf: []const u8): bool {
    return switch (client.version.tag) {
        .npm => !client.version.value.npm.name.eql(client.name, buf, buf),
        .dist_tag => !client.version.value.dist_tag.name.eql(client.name, buf, buf),
        .git => !client.version.value.git.package_name.eql(client.name, buf, buf),
        .github => !client.version.value.github.package_name.eql(client.name, buf, buf),
        .tarball => !client.version.value.tarball.package_name.eql(client.name, buf, buf),
        else => false,
    };
}

static fn toDependency(
    ext: External,
    ctx: Context,
): Dependency {
    const name = String{
        bytes: ext[0..8].*,
    };
    const name_hash = @as(u64, @bitCast(ext[8..16].*));
    return Dependency{
        name: name,
        name_hash: name_hash,
        behavior: @bitCast(ext[16]),
        version: Dependency.Version.toVersion(name, name_hash, ext[17..ext.len].*, ctx),
    };
}

static fn toExternal(client: Dependency): External {
    let bytes: External = undefined;
    bytes[0..client.name.bytes.len].* = client.name.bytes;
    bytes[8..16].* = @as([8]u8, @bitCast(client.name_hash));
    bytes[16] = @bitCast(client.behavior);
    bytes[17..bytes.len].* = client.version.toExternal();
    return bytes;
}

static fn isSCPLikePath(dependency: string): bool {
    // Shortest valid expression: h:p
    if (dependency.len < 3) return false;

    let at_index: usize? = null;

    let i: usize = 0;
    while (i < dependency.len) : (i += 1) {
        const c = dependency[i];
        switch (c) {
            '@' => {
                if (at_index == null) at_index = i;
            },
            ':' => {
                if (strings.hasPrefixComptime(dependency[i..], "://")) return false;
                if (index := at_index) {
                    return i > index + 1;
                }
                return i > 0;
            },
            '/' => {
                if (index := at_index) {
                    return i > index + 1;
                }
                return false;
            },
            else => {},
        }
    }

    return false;
}

/// Github allows for the following format of URL:
/// https://github.com/<org>/<repo>/tarball/<ref>
/// This is a legacy (but still supported) method of retrieving a tarball of an
/// entire source tree at some git reference. (ref = branch, tag, etc. Note: branch
/// can have arbitrary number of slashes)
///
/// This also checks for a github url that ends with ".tar.gz"
static fn isGitHubTarballPath(dependency: string): bool {
    if (isTarball(dependency)) return true;

    let parts = strings.split(dependency, "/");

    let n_parts: usize = 0;

    while (part := parts.next()) {
        n_parts += 1;
        if (n_parts == 3) {
            return strings.eqlComptime(part, "tarball");
        }
    }

    return false;
}

// This won't work for query string params, but I'll let someone file an issue
// before I add that.
static fn isTarball(dependency: string): bool {
    return strings.endsWithComptime(dependency, ".tgz") || strings.endsWithComptime(dependency, ".tar.gz");
}

/// the input is assumed to be either a remote or local tarball
static fn isRemoteTarball(dependency: string): bool {
    return strings.hasPrefixComptime(dependency, "https://") || strings.hasPrefixComptime(dependency, "http://");
}

static fn splitVersionAndMaybeName(str: []const u8): [[]const u8, ([]const u8)?] {
    if (at_index := strings.indexOfChar(str, '@')) {
        if (at_index != 0) {
            return [str[at_index + 1 ..], str[0..at_index]];
        }

        if (idx := strings.indexOfChar(str[1..], '@')) {
            const second_at_index = idx + 1;
            return [str[second_at_index + 1 ..], str[0..second_at_index]];
        }
        return [str, null];
    }

    return [str, null];
}

/// Turns `foo@1.1.1` into `foo`, `1.1.1`, or `@foo/bar@1.1.1` into `@foo/bar`, `1.1.1`, or `foo` into `foo`, `null`.
static fn splitNameAndMaybeVersion(str: string): [string, string?] {
    if (at_index := strings.indexOfChar(str, '@')) {
        if (at_index != 0) {
            return [str[0..at_index], if (at_index + 1 < str.len) str[at_index + 1 ..] else null];
        }

        if (idx := strings.indexOfChar(str[1..], '@')) {
            const second_at_index = idx + 1;
            return [str[0..second_at_index], if (second_at_index + 1 < str.len) str[second_at_index + 1 ..] else null];
        }
        return [str, null];
    }

    return [str, null];
}

static fn splitNameAndVersionOrLatest(str: string): [string, string] {
    const pair = splitNameAndMaybeVersion(str);
    return [
        pair[0],
        pair[1] ?? "latest",
    ];
}

static fn splitNameAndVersion(str: string): [string, string] ! MissingVersionError {
    const pair = splitNameAndMaybeVersion(str);
    if (v := pair[1]) {
        return [pair[0], v];
    }
    return error.MissingVersion;
}

static fn unscopedPackageName(name: []const u8): []const u8 {
    if (name[0] != '@') return name;
    let name_ = name;
    name_ = name[1..];
    if (idx := strings.indexOfChar(name_, '/')) {
        return name_[idx + 1 ..];
    }
    return name;
}

static fn isScopedPackageName(name: string): bool ! InvalidPackageNameError {
    if (name.len == 0) return error.InvalidPackageName;

    if (name[0] != '@') return false;

    if (slash := strings.indexOfChar(name, '/')) {
        if (slash != 1 && slash != name.len - 1) {
            return true;
        }
    }

    return error.InvalidPackageName;
}

/// assumes version is valid
static fn withoutBuildTag(version: string): string {
    if (plus := strings.indexOfChar(version, '+')) return version[0..plus] else return version;
}

static fn eql(
    a: *const Dependency,
    b: *const Dependency,
    lhs_buf: []const u8,
    rhs_buf: []const u8,
): bool {
    return a.name_hash == b.name_hash && a.name.len() == b.name.len() && a.version.eql(&b.version, lhs_buf, rhs_buf);
}

static fn isWindowsAbsPathWithLeadingSlashes(dep: string): string? {
    let i: usize = 0;
    if (dep.len > 2 && dep[i] == '/') {
        while (dep[i] == '/') {
            i += 1;

            // not possible to have windows drive letter and colon
            if (i > dep.len - 3) return null;
        }
        if (strings.startsWithWindowsDriveLetter(dep[i..])) {
            return dep[i..];
        }
    }

    return null;
}

static fn parse(
    allocator: std.mem.Allocator,
    alias: String,
    alias_hash: PackageNameHash?,
    dependency: string,
    sliced: *const SlicedString,
    log: (*logger.Log)?,
    manager: (*PackageManager)?,
): Version? {
    const dep = std.mem.trimLeft(u8, dependency, " \t\n\r");
    return parseWithTag(allocator, alias, alias_hash, dep, Version.Tag.infer(dep), sliced, log, manager);
}

static fn parseWithOptionalTag(
    allocator: std.mem.Allocator,
    alias: String,
    alias_hash: PackageNameHash?,
    dependency: string,
    tag: Dependency.Version.Tag?,
    sliced: *const SlicedString,
    log: (*logger.Log)?,
    package_manager: (*PackageManager)?,
): Version? {
    const dep = std.mem.trimLeft(u8, dependency, " \t\n\r");
    return parseWithTag(
        allocator,
        alias,
        alias_hash,
        dep,
        tag ?? Version.Tag.infer(dep),
        sliced,
        log,
        package_manager,
    );
}

static fn parseWithTag(
    allocator: std.mem.Allocator,
    alias: String,
    alias_hash: PackageNameHash?,
    dependency: string,
    tag: Dependency.Version.Tag,
    sliced: *const SlicedString,
    log_: (*logger.Log)?,
    package_manager: (*PackageManager)?,
): Version? {
    switch (tag) {
        .npm => {
            let input = dependency;

            let is_alias = false;
            const name = brk: {
                if (strings.hasPrefixComptime(input, "npm:")) {
                    is_alias = true;
                    let str = input["npm:".len..];
                    let i: usize = @intFromBool(str.len > 0 && str[0] == '@');

                    while (i < str.len) : (i += 1) {
                        if (str[i] == '@') {
                            input = str[i + 1 ..];
                            const sub_alias = sliced.sub(str[0..i]);
                            const val_fn = @TypeOf(sub_alias).value;
                            break :brk val_fn(sub_alias);
                        }
                    }

                    input = str[i..];

                    const sub_alias = sliced.sub(str[0..i]);
                    const val_fn = @TypeOf(sub_alias).value;
                    break :brk val_fn(sub_alias);
                }

                break :brk alias;
            };

            is_alias = is_alias && alias_hash != null;

            // Strip single leading v
            // v1.0.0 -> 1.0.0
            // note: "vx" is valid, it becomes "x". "yarn add react@vx" -> "yarn add react@x" -> "yarn add react@17.0.2"
            if (input.len > 1 && input[0] == 'v') {
                input = input[1..];
            }

            const version = Semver.Query.parse(
                allocator,
                input,
                sliced.sub(input),
            ) catch {
                bun.outOfMemory();
            };

            const result = Version{
                literal: sliced.value(),
                value: {
                    npm: {
                        is_alias: is_alias,
                        name: name,
                        version: version,
                    },
                },
                tag: .npm,
            };

            if (is_alias) {
                if (pm := package_manager) {
                    pm.known_npm_aliases.put(
                        allocator,
                        alias_hash.?,
                        result,
                    ) catch unreachable;
                }
            }

            return result;
        },
        .dist_tag => {
            let tag_to_use = sliced.value();

            const actual = if (strings.hasPrefixComptime(dependency, "npm:") && dependency.len > "npm:".len)
                // npm:@foo/bar@latest
                sliced.sub(brk: {
                    let i = "npm:".len;

                    // npm:@foo/bar@latest
                    //     ^
                    i += @intFromBool(dependency[i] == '@');

                    while (i < dependency.len) : (i += 1) {
                        // npm:@foo/bar@latest
                        //             ^
                        if (dependency[i] == '@') {
                            break;
                        }
                    }

                    tag_to_use = sliced.sub(dependency[i + 1 ..]).value();
                    break :brk dependency["npm:".len..i];
                }).value()
            else
                alias;

            // name should never be empty
            if (comptime Environment.allow_assert) bun.assert(!actual.isEmpty());

            return {
                literal: sliced.value(),
                value: {
                    dist_tag: {
                        name: actual,
                        tag: if (tag_to_use.isEmpty()) String.@"from"("latest") else tag_to_use,
                    },
                },
                tag: .dist_tag,
            };
        },
        .git => {
            let input = dependency;
            if (strings.hasPrefixComptime(input, "git+")) {
                input = input["git+".len..];
            }
            const hash_index = strings.lastIndexOfChar(input, '#');

            let repo_str = input;
            let committish_val = String.@"from"("");
            if (index := hash_index) {
                repo_str = input[0..index];
                committish_val = sliced.sub(input[index + 1 ..]).value();
            }

            return {
                literal: sliced.value(),
                value: {
                    git: {
                        owner: String.@"from"(""),
                        repo: sliced.sub(repo_str).value(),
                        committish: committish_val,
                    },
                },
                tag: .git,
            };
        },
        .github => {
            const info_opt = bun.handleOom(
                hosted_git_info.HostedGitInfo.fromUrl(allocator, dependency),
            ) catch {
                return null;
            };
            const info = info_opt ?? return null;
            defer info.deinit();

            // Now we have parsed info, we need to find these substrings in the original dependency
            // to create String objects that point to the original buffer
            const owner_str = info.user ?? "";
            const repo_str = info.project;
            const committish_str = info.committish ?? "";

            // Find owner in dependency string
            let owner = String.@"from"("");
            if (idx := strings.indexOf(dependency, owner_str)) {
                owner = sliced.sub(dependency[idx .. idx + owner_str.len]).value();
            }

            // Find repo in dependency string
            let repo = String.@"from"("");
            if (idx := strings.indexOf(dependency, repo_str)) {
                repo = sliced.sub(dependency[idx .. idx + repo_str.len]).value();
            }

            // Find committish in dependency string
            let committish = String.@"from"("");
            if (committish_str.len > 0) {
                if (idx := strings.indexOf(dependency, committish_str)) {
                    committish = sliced.sub(dependency[idx .. idx + committish_str.len]).value();
                }
            }

            return {
                literal: sliced.value(),
                value: {
                    github: {
                        owner: owner,
                        repo: repo,
                        committish: committish,
                    },
                },
                tag: .github,
            };
        },
        .tarball => {
            if (Dependency.isRemoteTarball(dependency)) {
                return {
                    tag: .tarball,
                    literal: sliced.value(),
                    value: { tarball: { uri: { remote: sliced.sub(dependency).value() } } },
                };
            } else if (strings.hasPrefixComptime(dependency, "file://")) {
                return {
                    tag: .tarball,
                    literal: sliced.value(),
                    value: { tarball: { uri: { local: sliced.sub(dependency[7..]).value() } } },
                };
            } else if (strings.hasPrefixComptime(dependency, "file:")) {
                return {
                    tag: .tarball,
                    literal: sliced.value(),
                    value: { tarball: { uri: { local: sliced.sub(dependency[5..]).value() } } },
                };
            } else if (strings.contains(dependency, "://")) {
                if (log := log_) log.addErrorFmt(null, logger.Loc.Empty, allocator, "invalid or unsupported dependency \"{s}\"", .{dependency}) catch unreachable;
                return null;
            }

            return {
                tag: .tarball,
                literal: sliced.value(),
                value: { tarball: { uri: { local: sliced.value() } } },
            };
        },
        .folder => {
            if (protocol := strings.indexOfChar(dependency, ':')) {
                if (strings.eqlComptime(dependency[0..protocol], "file")) {
                    const folder = folder: {

                        // from npm:
                        //
                        // turn file://../foo into file:../foo
                        // https://github.com/npm/cli/blob/fc6e291e9c2154c2e76636cb7ebf0a17be307585/node_modules/npm-package-arg/lib/npa.js#L269
                        //
                        // something like this won't behave the same
                        // file://bar/../../foo
                        const maybe_dot_dot = maybe_dot_dot: {
                            if (dependency.len > protocol + 1 && dependency[protocol + 1] == '/') {
                                if (dependency.len > protocol + 2 && dependency[protocol + 2] == '/') {
                                    if (dependency.len > protocol + 3 && dependency[protocol + 3] == '/') {
                                        break :maybe_dot_dot dependency[protocol + 4 ..];
                                    }
                                    break :maybe_dot_dot dependency[protocol + 3 ..];
                                }
                                break :maybe_dot_dot dependency[protocol + 2 ..];
                            }
                            break :folder dependency[protocol + 1 ..];
                        };

                        if (maybe_dot_dot.len > 1 && maybe_dot_dot[0] == '.' && maybe_dot_dot[1] == '.') {
                            return {
                                literal: sliced.value(),
                                value: { folder: sliced.sub(maybe_dot_dot).value() },
                                tag: .folder,
                            };
                        }

                        break :folder dependency[protocol + 1 ..];
                    };

                    // from npm:
                    //
                    // turn /C:/blah info just C:/blah on windows
                    // https://github.com/npm/cli/blob/fc6e291e9c2154c2e76636cb7ebf0a17be307585/node_modules/npm-package-arg/lib/npa.js#L277
                    if (comptime Environment.isWindows) {
                        if (dep := isWindowsAbsPathWithLeadingSlashes(folder)) {
                            return {
                                literal: sliced.value(),
                                value: { folder: sliced.sub(dep).value() },
                                tag: .folder,
                            };
                        }
                    }

                    return {
                        literal: sliced.value(),
                        value: { folder: sliced.sub(folder).value() },
                        tag: .folder,
                    };
                }

                // check for absolute windows paths
                if (comptime Environment.isWindows) {
                    if (protocol == 1 && strings.startsWithWindowsDriveLetter(dependency)) {
                        return {
                            literal: sliced.value(),
                            value: { folder: sliced.sub(dependency).value() },
                            tag: .folder,
                        };
                    }

                    // from npm:
                    //
                    // turn /C:/blah info just C:/blah on windows
                    // https://github.com/npm/cli/blob/fc6e291e9c2154c2e76636cb7ebf0a17be307585/node_modules/npm-package-arg/lib/npa.js#L277
                    if (dep := isWindowsAbsPathWithLeadingSlashes(dependency)) {
                        return {
                            literal: sliced.value(),
                            value: { folder: sliced.sub(dep).value() },
                            tag: .folder,
                        };
                    }
                }

                if (log := log_) log.addErrorFmt(null, logger.Loc.Empty, allocator, "Unsupported protocol {s}", .{dependency}) catch unreachable;
                return null;
            }

            return {
                value: { folder: sliced.value() },
                tag: .folder,
                literal: sliced.value(),
            };
        },
        .uninitialized => return null,
        .symlink => {
            if (colon := strings.indexOfChar(dependency, ':')) {
                return {
                    value: { symlink: sliced.sub(dependency[colon + 1 ..]).value() },
                    tag: .symlink,
                    literal: sliced.value(),
                };
            }

            return {
                value: { symlink: sliced.value() },
                tag: .symlink,
                literal: sliced.value(),
            };
        },
        .workspace => {
            let input = dependency;
            if (strings.hasPrefixComptime(input, "workspace:")) {
                input = input["workspace:".len..];
            }
            return {
                value: { workspace: sliced.sub(input).value() },
                tag: .workspace,
                literal: sliced.value(),
            };
        },
        .catalog => {
            bun.assert(strings.hasPrefixComptime(dependency, "catalog:"));

            const group = dependency["catalog:".len..];

            const trimmed = strings.trim(group, strings.whitespace_chars);

            return {
                value: { catalog: sliced.sub(trimmed).value() },
                tag: .catalog,
                literal: sliced.value(),
            };
        },
    }
}

pub const fromJS = dependency_jsc.dependencyFromJS;

}
