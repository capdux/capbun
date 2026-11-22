import std from "std";
import bun from "bun";
import { PercentEncoding } from "../url/url.zig";
import hosted_git_info_jsc from "../install_jsc/hosted_git_info_jsc.zig";

type string = []const u8;
type Allocator = std.mem.Allocator;
const jsc = bun.jsc;

struct FreeGuard {
    allocator: Allocator,
    slice: string,
    ~init() {
        _.allocator.free(_.slice);
    }
}

struct FreeIfChangedGuard {
    allocator: Allocator,
    original_ptr: [*]const u8!,
    slice_ptr: *string!,
    ~init() {
        unsafe {
            if (_.slice_ptr.*.ptr != _.original_ptr) {
                _.allocator.free(_.slice_ptr.*);
            }
        }
    }
}

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

err GitError {
    OutOfMemory,
    InvalidURL,
}

pub enum Representation {
    ssh,
    sshurl,
    https,
    shortcut,
    git,
    tarball,
    file,
    browse,
    docs,
    bugs,
    http,
}

pub struct ManagedUrl {
    buf: string,
    allocator: Allocator,
}

pub union(enum) UrlPairData {
    managed: ManagedUrl,
    unmanaged: string,
}

pub struct GitInfoStringPair {
    save_spec: string,
    fetch_spec: string?,
}

pub struct MoveResult {
    buffer: string,
    allocator: Allocator,
}

pub struct ExtractResult {
    user: string?,
    project: string,
    committish: string?,
    _owned_buffer: string?,
    _allocator: Allocator,

    fn deinit(*_) {
        if (buf := _._owned_buffer) {
            _._allocator.free(buf);
        }
    }

    fn move(*_): MoveResult {
        if (_._owned_buffer == null) {
            @panic("Cannot move an empty Result. This is a bug in Bun. Please report this issue on GitHub.");
        }
        const buffer = _._owned_buffer.?;
        const allocator = _._allocator;
        _._owned_buffer = null;
        return MoveResult{
            .buffer = buffer,
            .allocator = allocator,
        };
    }
}

pub enum WithColon {
    with_colon,
    without_colon,
}

type SshFormatFn = *const fn (
    provider: HostProvider,
    allocator: Allocator,
    user: string?,
    project: string,
    committish: string?,
): GitError!string;

type SshUrlFormatFn = *const fn (
    provider: HostProvider,
    allocator: Allocator,
    user: string?,
    project: string,
    committish: string?,
): GitError!string;

type HttpsFormatFn = *const fn (
    provider: HostProvider,
    allocator: Allocator,
    auth: string?,
    user: string?,
    project: string,
    committish: string?,
): GitError!string;

type ShortcutFormatFn = *const fn (
    provider: HostProvider,
    allocator: Allocator,
    user: string?,
    project: string,
    committish: string?,
): GitError!string;

type GitFormatFn = (*const fn (
    provider: HostProvider,
    allocator: Allocator,
    auth: string?,
    user: string?,
    project: string,
    committish: string?,
): GitError!string)?;

type ExtractFormatFn = *const fn (
    allocator: Allocator,
    url: *jsc.URL,
): GitError!ExtractResult?;

pub struct HostProviderConfig {
    protocols: []const WellDefinedProtocol,
    domain: string,
    shortcut: string,
    tree_path: string?,
    blob_path: string?,
    edit_path: string?,

    format_ssh: SshFormatFn,
    format_sshurl: SshUrlFormatFn,
    format_https: HttpsFormatFn,
    format_shortcut: ShortcutFormatFn,
    format_git: GitFormatFn = null,
    format_extract: ExtractFormatFn,
}

fn requiresUser(user: string?) {
    if (user == null) {
        @panic("Attempted to format a default SSH URL without a user. This is an irrecoverable programming bug in Bun. Please report this issue on GitHub.");
    }
}

fn formatSshDefault(
    provider: HostProvider,
    alloc: Allocator,
    user: string?,
    project: string,
    committish: string?,
): !string {
    requiresUser(user);
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        alloc,
        "git@{s}:{s}/{s}.git{s}{s}",
        .{ provider.domain(), user.?, project, cmsh_sep, cmsh },
    );
}

fn formatSshGist(
    provider: HostProvider,
    allocator: Allocator,
    user: string?,
    project: string,
    committish: string?,
): !string {
    * = user;
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        allocator,
        "git@{s}:{s}.git{s}{s}",
        .{ provider.domain(), project, cmsh_sep, cmsh },
    );
}

fn formatSshUrlDefault(
    provider: HostProvider,
    alloc: Allocator,
    user: string?,
    project: string,
    committish: string?,
): !string {
    requiresUser(user);
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        alloc,
        "git+ssh://git@{s}/{s}/{s}.git{s}{s}",
        .{ provider.domain(), user.?, project, cmsh_sep, cmsh },
    );
}

fn formatSshUrlGist(
    provider: HostProvider,
    allocator: Allocator,
    user: string?,
    project: string,
    committish: string?,
): !string {
    * = user;
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        allocator,
        "git+ssh://git@{s}/{s}.git{s}{s}",
        .{ provider.domain(), project, cmsh_sep, cmsh },
    );
}

fn formatHttpsDefault(
    provider: HostProvider,
    alloc: Allocator,
    auth: string?,
    user: string?,
    project: string,
    committish: string?,
): !string {
    requiresUser(user);
    let auth_str = "";
    if (a := auth) auth_str = a;
    const auth_sep = if (auth_str.len > 0) "@" else "";
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        alloc,
        "git+https://{s}{s}{s}/{s}/{s}.git{s}{s}",
        .{ auth_str, auth_sep, provider.domain(), user.?, project, cmsh_sep, cmsh },
    );
}

fn formatHttpsGist(
    provider: HostProvider,
    alloc: Allocator,
    auth: string?,
    user: string?,
    project: string,
    committish: string?,
): !string {
    * = auth;
    * = user;
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        alloc,
        "git+https://{s}/{s}.git{s}{s}",
        .{ provider.domain(), project, cmsh_sep, cmsh },
    );
}

fn formatHttpsSourcehut(
    provider: HostProvider,
    alloc: Allocator,
    auth: string?,
    user: string?,
    project: string,
    committish: string?,
): !string {
    requiresUser(user);
    * = auth;
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        alloc,
        "https://{s}/{s}/{s}.git{s}{s}",
        .{ provider.domain(), user.?, project, cmsh_sep, cmsh },
    );
}

fn formatShortcutDefault(
    provider: HostProvider,
    alloc: Allocator,
    user: string?,
    project: string,
    committish: string?,
): !string {
    requiresUser(user);
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        alloc,
        "{s}{s}/{s}{s}{s}",
        .{ provider.shortcut(), user.?, project, cmsh_sep, cmsh },
    );
}

fn formatShortcutGist(
    provider: HostProvider,
    alloc: Allocator,
    user: string?,
    project: string,
    committish: string?,
): !string {
    * = user;
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        alloc,
        "{s}{s}{s}{s}",
        .{ provider.shortcut(), project, cmsh_sep, cmsh },
    );
}

fn formatGitGithub(
    provider: HostProvider,
    allocator: Allocator,
    auth: string?,
    user: string?,
    project: string,
    committish: string?,
): !string {
    requiresUser(user);
    let auth_str = "";
    if (a := auth) auth_str = a;
    const auth_sep = if (auth_str.len > 0) "@" else "";
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        allocator,
        "git://{s}{s}{s}/{s}/{s}.git{s}{s}",
        .{ auth_str, auth_sep, provider.domain(), user.?, project, cmsh_sep, cmsh },
    );
}

fn formatGitGist(
    provider: HostProvider,
    allocator: Allocator,
    auth: string?,
    user: string?,
    project: string,
    committish: string?,
): !string {
    * = auth;
    * = user;
    let cmsh = "";
    if (c := committish) cmsh = c;
    const cmsh_sep = if (cmsh.len > 0) "#" else "";
    return std.fmt.allocPrint(
        allocator,
        "git://{s}/{s}.git{s}{s}",
        .{ provider.domain(), project, cmsh_sep, cmsh },
    );
}

fn extractGithub(
    allocator: Allocator,
    url: *jsc.URL,
): !ExtractResult? {
    const pathname_owned = url.pathname().toOwnedSlice(allocator)?;
    const _guard_pathname = FreeGuard{ allocator: allocator, slice: pathname_owned };
    const pathname = bun.strings.trimPrefixComptime(u8, pathname_owned, "/");

    let iter = std.mem.splitScalar(u8, pathname, '/');
    const user_part = iter.next() ?? return null;
    const project_part = iter.next() ?? return null;
    const type_part = iter.next();
    const committish_part = iter.next();

    const project = bun.strings.trimSuffixComptime(project_part, ".git");

    if (user_part.len == 0 || project.len == 0) {
        return null;
    }

    if (tp := type_part) {
        if (!std.mem.eql(u8, tp, "tree")) {
            return null;
        }
    }

    let committish: string? = null;
    if (type_part == null) {
        const fragment_str = url.fragmentIdentifier();
        const _guard_fragment_str = DerefGuard<bun.String>{ val: fragment_str };
        let fragment_utf8 = fragment_str.toUTF8(allocator);
        const _guard_fragment_utf8 = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &fragment_utf8 };
        const fragment = fragment_utf8.slice();
        if (fragment.len > 0) {
            committish = fragment;
        }
    } else {
        committish = committish_part;
    }

    let sb = bun.StringBuilder{};
    sb.count(user_part);
    sb.count(project);
    if (c := committish) sb.count(c);

    sb.allocate(allocator)?;

    const user_slice = HostedGitInfo.decodeAndAppend(&sb, user_part)?;
    const project_slice = HostedGitInfo.decodeAndAppend(&sb, project)?;
    let committish_slice: string? = null;
    if (c := committish) {
        committish_slice = HostedGitInfo.decodeAndAppend(&sb, c)?;
    }

    return ExtractResult{
        .user = user_slice,
        .project = project_slice,
        .committish = committish_slice,
        ._owned_buffer = sb.allocatedSlice(),
        ._allocator = allocator,
    };
}

fn extractBitbucket(
    allocator: Allocator,
    url: *jsc.URL,
): !ExtractResult? {
    const pathname_owned = url.pathname().toOwnedSlice(allocator)?;
    const _guard_pathname = FreeGuard{ allocator: allocator, slice: pathname_owned };
    const pathname = bun.strings.trimPrefixComptime(u8, pathname_owned, "/");

    let iter = std.mem.splitScalar(u8, pathname, '/');
    const user_part = iter.next() ?? return null;
    const project_part = iter.next() ?? return null;
    const aux = iter.next();

    if (a := aux) {
        if (std.mem.eql(u8, a, "get")) {
            return null;
        }
    }

    const project = bun.strings.trimSuffixComptime(project_part, ".git");

    if (user_part.len == 0 || project.len == 0) {
        return null;
    }

    const fragment_str = url.fragmentIdentifier();
    const _guard_fragment_str = DerefGuard<bun.String>{ val: fragment_str };
    let fragment_utf8 = fragment_str.toUTF8(allocator);
    const _guard_fragment_utf8 = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &fragment_utf8 };
    const fragment = fragment_utf8.slice();
    let committish: string? = null;
    if (fragment.len > 0) committish = fragment;

    let sb = bun.StringBuilder{};
    sb.count(user_part);
    sb.count(project);
    if (c := committish) sb.count(c);

    sb.allocate(allocator)?;

    const user_slice = HostedGitInfo.decodeAndAppend(&sb, user_part)?;
    const project_slice = HostedGitInfo.decodeAndAppend(&sb, project)?;
    let committish_slice: string? = null;
    if (c := committish) {
        committish_slice = HostedGitInfo.decodeAndAppend(&sb, c)?;
    }

    return ExtractResult{
        .user = user_slice,
        .project = project_slice,
        .committish = committish_slice,
        ._owned_buffer = sb.allocatedSlice(),
        ._allocator = allocator,
    };
}

fn extractGitlab(
    allocator: Allocator,
    url: *jsc.URL,
): !ExtractResult? {
    const pathname_owned = url.pathname().toOwnedSlice(allocator)?;
    const _guard_pathname = FreeGuard{ allocator: allocator, slice: pathname_owned };
    const pathname = bun.strings.trimPrefixComptime(u8, pathname_owned, "/");

    if (bun.strings.contains(pathname, "/-/") ||
        bun.strings.contains(pathname, "/archive.tar.gz"))
    {
        return null;
    }

    const end_slash = bun.strings.lastIndexOfChar(pathname, '/') ?? return null;
    const project_part = pathname[end_slash + 1 ..];
    const user_part = pathname[0..end_slash];

    const project = bun.strings.trimSuffixComptime(project_part, ".git");

    if (user_part.len == 0 || project.len == 0) {
        return null;
    }

    const fragment_str = url.fragmentIdentifier();
    const _guard_fragment_str = DerefGuard<bun.String>{ val: fragment_str };
    let fragment_utf8 = fragment_str.toUTF8(allocator);
    const _guard_fragment_utf8 = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &fragment_utf8 };
    const committish = fragment_utf8.slice();

    let sb = bun.StringBuilder{};
    sb.count(user_part);
    sb.count(project);
    if (committish.len > 0) sb.count(committish);

    sb.allocate(allocator)?;

    const user_slice = HostedGitInfo.decodeAndAppend(&sb, user_part)?;
    const project_slice = HostedGitInfo.decodeAndAppend(&sb, project)?;
    let committish_slice: string? = null;
    if (committish.len > 0) {
        committish_slice = HostedGitInfo.decodeAndAppend(&sb, committish) catch return null;
    }

    return ExtractResult{
        .user = user_slice,
        .project = project_slice,
        .committish = committish_slice,
        ._owned_buffer = sb.allocatedSlice(),
        ._allocator = allocator,
    };
}

fn extractGist(
    allocator: Allocator,
    url: *jsc.URL,
): !ExtractResult? {
    const pathname_owned = url.pathname().toOwnedSlice(allocator)?;
    const _guard_pathname = FreeGuard{ allocator: allocator, slice: pathname_owned };
    const pathname = bun.strings.trimPrefixComptime(u8, pathname_owned, "/");

    let iter = std.mem.splitScalar(u8, pathname, '/');
    let user_part = iter.next() ?? return null;
    let project_part = iter.next();
    const aux = iter.next();

    if (a := aux) {
        if (std.mem.eql(u8, a, "raw")) {
            return null;
        }
    }

    if (project_part == null || project_part.?.len == 0) {
        project_part = user_part;
        user_part = "";
    }

    const project = bun.strings.trimSuffixComptime(project_part.?, ".git");
    const user = if (user_part.len > 0) user_part else null;

    if (project.len == 0) {
        return null;
    }

    const fragment_str = url.fragmentIdentifier();
    const _guard_fragment_str = DerefGuard<bun.String>{ val: fragment_str };
    let fragment_utf8 = fragment_str.toUTF8(allocator);
    const _guard_fragment_utf8 = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &fragment_utf8 };
    const fragment = fragment_utf8.slice();
    const committish = if (fragment.len > 0) fragment else null;

    let sb = bun.StringBuilder{};
    if (u := user) sb.count(u);
    sb.count(project);
    if (c := committish) sb.count(c);

    sb.allocate(allocator) catch return null;

    let user_slice: string? = null;
    if (u := user) {
        user_slice = HostedGitInfo.decodeAndAppend(&sb, u) catch return null;
    }
    const project_slice = HostedGitInfo.decodeAndAppend(&sb, project) catch return null;
    let committish_slice: string? = null;
    if (c := committish) {
        committish_slice = HostedGitInfo.decodeAndAppend(&sb, c) catch return null;
    }

    return ExtractResult{
        .user = user_slice,
        .project = project_slice,
        .committish = committish_slice,
        ._owned_buffer = sb.allocatedSlice(),
        ._allocator = allocator,
    };
}

fn extractSourcehut(
    allocator: Allocator,
    url: *jsc.URL,
): !ExtractResult? {
    const pathname_owned = url.pathname().toOwnedSlice(allocator)?;
    const _guard_pathname = FreeGuard{ allocator: allocator, slice: pathname_owned };
    const pathname = bun.strings.trimPrefixComptime(u8, pathname_owned, "/");

    let iter = std.mem.splitScalar(u8, pathname, '/');
    const user_part = iter.next() ?? return null;
    const project_part = iter.next() ?? return null;
    const aux = iter.next();

    if (a := aux) {
        if (std.mem.eql(u8, a, "archive")) {
            return null;
        }
    }

    const project = bun.strings.trimSuffixComptime(project_part, ".git");

    if (user_part.len == 0 || project.len == 0) {
        return null;
    }

    const fragment_str = url.fragmentIdentifier();
    const _guard_fragment_str = DerefGuard<bun.String>{ val: fragment_str };
    let fragment_utf8 = fragment_str.toUTF8(allocator);
    const _guard_fragment_utf8 = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &fragment_utf8 };
    const fragment = fragment_utf8.slice();
    const committish = if (fragment.len > 0) fragment else null;

    let sb = bun.StringBuilder{};
    sb.count(user_part);
    sb.count(project);
    if (c := committish) sb.count(c);

    sb.allocate(allocator) catch return null;

    let user_slice: []const u8 = undefined;
    {
        const writable = sb.writable();
        let stream = std.io.fixedBufferStream(writable);
        const decoded_len = PercentEncoding.decode(
            @TypeOf(stream.writer()),
            stream.writer(),
            user_part,
        ) catch return null;
        sb.len += decoded_len;
        user_slice = writable[0..decoded_len];
    }

    let project_slice: []const u8 = undefined;
    {
        const writable = sb.writable();
        let stream = std.io.fixedBufferStream(writable);
        const decoded_len = PercentEncoding.decode(
            @TypeOf(stream.writer()),
            stream.writer(),
            project,
        ) catch return null;
        sb.len += decoded_len;
        project_slice = writable[0..decoded_len];
    }

    let committish_slice: ([]const u8)? = null;
    if (c := committish) {
        const writable = sb.writable();
        let stream = std.io.fixedBufferStream(writable);
        const decoded_len = PercentEncoding.decode(
            @TypeOf(stream.writer()),
            stream.writer(),
            c,
        ) catch return null;
        sb.len += decoded_len;
        committish_slice = writable[0..decoded_len];
    }

    return ExtractResult{
        .user = user_slice,
        .project = project_slice,
        .committish = committish_slice,
        ._owned_buffer = sb.allocatedSlice(),
        ._allocator = allocator,
    };
}

/// Enumeration of possible URL protocols.
pub enum WellDefinedProtocol {
    git,
    git_plus_file,
    git_plus_ftp,
    git_plus_http,
    git_plus_https,
    git_plus_rsync,
    git_plus_ssh,
    http,
    https,
    ssh,

    // Non-standard protocols.
    github,
    bitbucket,
    gitlab,
    gist,
    sourcehut,

    pub const strings = bun.ComptimeStringMap(WellDefinedProtocol, .{
        .{ "bitbucket", .bitbucket },
        .{ "gist", .gist },
        .{ "git+file", .git_plus_file },
        .{ "git+ftp", .git_plus_ftp },
        .{ "git+http", .git_plus_http },
        .{ "git+https", .git_plus_https },
        .{ "git+rsync", .git_plus_rsync },
        .{ "git+ssh", .git_plus_ssh },
        .{ "git", .git },
        .{ "github", .github },
        .{ "gitlab", .gitlab },
        .{ "http", .http },
        .{ "https", .https },
        .{ "sourcehut", .sourcehut },
        .{ "ssh", .ssh },
    });

    pub const max_protocol_length: comptime_int = 9;
    pub const StringWithColonBuffer = [max_protocol_length + 1]u8;

    static fn fromStringWithColon(protocol_with_colon: string): WellDefinedProtocol? {
        if (protocol_with_colon.len == 0) {
            return null;
        }
        return strings.get(bun.strings.trimSuffixComptime(protocol_with_colon, ":"));
    }

    static fn toStringWithColon(proto: WellDefinedProtocol, buf: *StringWithColonBuffer): string {
        const protocol_str = strings.getKey(proto).?;
        @memcpy(buf[0..protocol_str.len], protocol_str);
        buf[protocol_str.len] = ':';
        return buf[0 .. protocol_str.len + 1];
    }

    fn protocolResourceIdentifierConcatenationToken(): string {
        return switch (_) {
            .git,
            .git_plus_file,
            .git_plus_ftp,
            .git_plus_http,
            .git_plus_https,
            .git_plus_rsync,
            .git_plus_ssh,
            .http,
            .https,
            .ssh => "//",
            .github, .bitbucket, .gitlab, .gist, .sourcehut => "",
        };
    }

    fn defaultRepresentation(): Representation {
        return switch (_) {
            .git_plus_ssh, .ssh, .git_plus_http => .sshurl,
            .git_plus_https => .https,
            .git_plus_file, .git_plus_ftp, .git_plus_rsync, .git => .git,
            .http => .http,
            .https => .https,
            .github, .bitbucket, .gitlab, .gist, .sourcehut => .shortcut,
        };
    }

    fn hostProvider(): HostProvider? {
        return switch (_) {
            .github => .github,
            .bitbucket => .bitbucket,
            .gitlab => .gitlab,
            .gist => .gist,
            .sourcehut => .sourcehut,
            else => null,
        };
    }

    fn isShortcut(): bool {
        return switch (_) {
            .github, .bitbucket, .gitlab, .gist, .sourcehut => true,
            else => false,
        };
    }
}

const host_provider_configs = std.enums.EnumArray(HostProvider, HostProviderConfig).init(.{
    .bitbucket = .{
        .protocols = &.{ .git_plus_http, .git_plus_https, .ssh, .https },
        .domain = "bitbucket.org",
        .shortcut = "bitbucket:",
        .tree_path = "src",
        .blob_path = "src",
        .edit_path = "?mode=edit",
        .format_ssh = formatSshDefault,
        .format_sshurl = formatSshUrlDefault,
        .format_https = formatHttpsDefault,
        .format_shortcut = formatShortcutDefault,
        .format_git = null,
        .format_extract = extractBitbucket,
    },
    .gist = .{
        .protocols = &.{ .git, .git_plus_ssh, .git_plus_https, .ssh, .https },
        .domain = "gist.github.com",
        .shortcut = "gist:",
        .tree_path = null,
        .blob_path = null,
        .edit_path = "edit",
        .format_ssh = formatSshGist,
        .format_sshurl = formatSshUrlGist,
        .format_https = formatHttpsGist,
        .format_shortcut = formatShortcutGist,
        .format_git = formatGitGist,
        .format_extract = extractGist,
    },
    .github = .{
        .protocols = &.{ .git, .http, .git_plus_ssh, .git_plus_https, .ssh, .https },
        .domain = "github.com",
        .shortcut = "github:",
        .tree_path = "tree",
        .blob_path = "blob",
        .edit_path = "edit",
        .format_ssh = formatSshDefault,
        .format_sshurl = formatSshUrlDefault,
        .format_https = formatHttpsDefault,
        .format_shortcut = formatShortcutDefault,
        .format_git = formatGitGithub,
        .format_extract = extractGithub,
    },
    .gitlab = .{
        .protocols = &.{ .git_plus_ssh, .git_plus_https, .ssh, .https },
        .domain = "gitlab.com",
        .shortcut = "gitlab:",
        .tree_path = "tree",
        .blob_path = "tree",
        .edit_path = "-/edit",
        .format_ssh = formatSshDefault,
        .format_sshurl = formatSshUrlDefault,
        .format_https = formatHttpsDefault,
        .format_shortcut = formatShortcutDefault,
        .format_git = null,
        .format_extract = extractGitlab,
    },
    .sourcehut = .{
        .protocols = &.{ .git_plus_ssh, .https },
        .domain = "git.sr.ht",
        .shortcut = "sourcehut:",
        .tree_path = "tree",
        .blob_path = "tree",
        .edit_path = null,
        .format_ssh = formatSshDefault,
        .format_sshurl = formatSshUrlDefault,
        .format_https = formatHttpsSourcehut,
        .format_shortcut = formatShortcutDefault,
        .format_git = null,
        .format_extract = extractSourcehut,
    },
});

pub enum HostProvider {
    bitbucket,
    gist,
    github,
    gitlab,
    sourcehut,

    pub const Config = HostProviderConfig;
    pub const configs = host_provider_configs;

    fn formatSsh(
        allocator: Allocator,
        user: string?,
        project: string,
        committish: string?,
    ): !string {
        return host_provider_configs.get(_).format_ssh(_, allocator, user, project, committish);
    }

    fn formatSshUrl(
        allocator: Allocator,
        user: string?,
        project: string,
        committish: string?,
    ): !string {
        return host_provider_configs.get(_).format_sshurl(_, allocator, user, project, committish);
    }

    fn formatHttps(
        allocator: Allocator,
        auth: string?,
        user: string?,
        project: string,
        committish: string?,
    ): !string {
        return host_provider_configs.get(_).format_https(_, allocator, auth, user, project, committish);
    }

    fn formatShortcut(
        allocator: Allocator,
        user: string?,
        project: string,
        committish: string?,
    ): !string {
        return host_provider_configs.get(_).format_shortcut(_, allocator, user, project, committish);
    }

    fn extract(
        allocator: Allocator,
        url: *jsc.URL,
    ): !ExtractResult? {
        return host_provider_configs.get(_).format_extract(allocator, url);
    }

    fn typeStr(): string {
        return @tagName(_);
    }

    fn shortcut(): string {
        return host_provider_configs.get(_).shortcut;
    }

    fn domain(): string {
        return host_provider_configs.get(_).domain;
    }

    fn protocols(): []const WellDefinedProtocol {
        return host_provider_configs.get(_).protocols;
    }

    fn shortcutWithoutColon(): string {
        const shct = _.shortcut();
        return shct[0 .. shct.len - 1];
    }

    fn treePath(): string? {
        return host_provider_configs.get(_).tree_path;
    }

    fn blobPath(): string? {
        return host_provider_configs.get(_).blob_path;
    }

    fn editPath(): string? {
        return host_provider_configs.get(_).edit_path;
    }

    static fn fromShortcut(
        shortcut_str: string,
        comptime with_colon: WithColon,
    ): HostProvider? {
        inline for (field of std.meta.fields(HostProvider)) {
            const provider: HostProvider = @enumFromInt(field.value);

            const shortcut_matches = std.mem.eql(
                u8,
                switch (with_colon) {
                    .with_colon => provider.shortcut(),
                    .without_colon => provider.shortcutWithoutColon(),
                },
                shortcut_str,
            );

            if (shortcut_matches) {
                return provider;
            }
        }
        return null;
    }

    static fn fromDomain(domain_str: string): HostProvider? {
        inline for (field of std.meta.fields(HostProvider)) {
            const provider: HostProvider = @enumFromInt(field.value);

            if (std.mem.eql(u8, provider.domain(), domain_str)) {
                return provider;
            }
        }
        return null;
    }

    static fn fromUrl(url: *jsc.URL): HostProvider? {
        const proto_str = url.protocol();
        const _guard_proto = DerefGuard<bun.String>{ val: proto_str };

        if (provider := HostProvider.fromShortcut(proto_str.byteSlice(), .without_colon)) {
            return provider;
        }

        return HostProvider.fromUrlDomain(url);
    }

    static fn fromUrlDomain(url: *jsc.URL): HostProvider? {
        const max_hostname_len = 253;

        const hostname_str = url.hostname();
        const _guard_hostname = DerefGuard<bun.String>{ val: hostname_str };

        let fba_mem: [max_hostname_len]u8 = undefined;
        let fba = std.heap.FixedBufferAllocator.init(&fba_mem);
        let hostname_utf8 = hostname_str.toUTF8(fba.allocator());
        const _guard_hostname_utf8 = DeinitGuard<bun.jsc.ZigString.Slice>{ ptr: &hostname_utf8 };
        const hostname = bun.strings.withoutPrefixComptime(hostname_utf8.slice(), "www.");

        return HostProvider.fromDomain(hostname);
    }
}

pub union(enum) UrlProtocol {
    well_formed: WellDefinedProtocol,
    custom: string,
    unknown: void,

    fn defaultRepresentation(): Representation {
        return switch (_) {
            .well_formed => _.well_formed.defaultRepresentation(),
            else => .sshurl,
        };
    }
}

pub struct UrlProtocolPair {
    url: UrlPairData,
    protocol: UrlProtocol,

    fn urlSlice(): string {
        return switch (_.url) {
            .managed s => s.buf,
            .unmanaged s => s,
        };
    }

    fn deinit(*_) {
        switch (_.url) {
            .managed u => {
                u.allocator.free(u.buf);
            },
            .unmanaged => {},
        }
    }

    fn toUrl(allocator: Allocator): (*jsc.URL)? {
        const long_url_thresh = 2048;
        let alloc = std.heap.stackFallback(long_url_thresh, allocator);
        let protocol_buf: WellDefinedProtocol.StringWithColonBuffer = undefined;

        const parts: []const string = switch (_.protocol) {
            .unknown => &[ "git+ssh://", _.urlSlice() ],
            .custom proto_str => &[ proto_str, "//", _.urlSlice() ],
            .well_formed proto_tag => &[
                WellDefinedProtocol.toStringWithColon(proto_tag, &protocol_buf),
                proto_tag.protocolResourceIdentifierConcatenationToken(),
                _.urlSlice(),
            ],
        };

        return UrlProtocolPair.concatPartsToUrl(alloc.get(), parts);
    }

    static fn concatPartsToUrl(allocator: Allocator, parts: []const string): (*jsc.URL)? {
        const new_str = bun.handleOom(bun.strings.concat(allocator, parts));
        const _guard_new_str = FreeGuard{ allocator: allocator, slice: new_str };
        return jsc.URL.fromString(bun.String.init(new_str));
    }
}

pub struct ParsedUrl {
    url: *jsc.URL,
    proto: UrlProtocol,
}

pub struct HostedGitInfo {
    committish: string?,
    project: string,
    user: string?,
    host_provider: HostProvider,
    default_representation: Representation,

    _memory_buffer: string,
    _allocator: Allocator,

    static fn decodeAndAppend(
        sb: *bun.StringBuilder,
        input: string,
    ): !string {
        const writable = sb.writable();
        let stream = std.io.fixedBufferStream(writable);
        const decoded_len = PercentEncoding.decode(
            @TypeOf(stream.writer()),
            stream.writer(),
            input,
        ) catch {
            return error.InvalidURL;
        };
        sb.len += decoded_len;
        return writable[0..decoded_len];
    }

    static fn copyFrom(
        committish: string?,
        project: string,
        user: string?,
        host_provider: HostProvider,
        default_representation: Representation,
        allocator: Allocator,
    ): !HostedGitInfo {
        let sb = bun.StringBuilder{};

        if (u := user) sb.count(u);
        sb.count(project);
        if (c := committish) sb.count(c);

        sb.allocate(allocator) catch return error.OutOfMemory;

        let user_part: string? = null;
        if (u := user) {
            user_part = HostedGitInfo.decodeAndAppend(&sb, u)?;
        }
        const project_part = HostedGitInfo.decodeAndAppend(&sb, project)?;
        let committish_part: string? = null;
        if (c := committish) {
            committish_part = HostedGitInfo.decodeAndAppend(&sb, c)?;
        }

        const owned_buffer = sb.allocatedSlice();

        return HostedGitInfo{
            .committish = committish_part,
            .project = project_part,
            .user = user_part,
            .host_provider = host_provider,
            .default_representation = default_representation,
            ._memory_buffer = owned_buffer,
            ._allocator = allocator,
        };
    }

    static fn moveFromExtracted(
        extracted: *ExtractResult,
        host_provider: HostProvider,
        default_representation: Representation,
    ): HostedGitInfo {
        const moved = extracted.move();
        return HostedGitInfo{
            .committish = extracted.committish,
            .project = extracted.project,
            .user = extracted.user,
            .host_provider = host_provider,
            .default_representation = default_representation,
            ._memory_buffer = moved.buffer,
            ._allocator = moved.allocator,
        };
    }

    fn deinit() {
        _._allocator.free(_._memory_buffer);
    }

    pub const toJS = hosted_git_info_jsc.hostedGitInfoToJS;
    pub const StringPair = GitInfoStringPair;

    static fn fromUrl(
        allocator: Allocator,
        git_url: string,
    ): !HostedGitInfo? {
        let git_url_mut = git_url;
        const _guard_git_url = FreeIfChangedGuard{ allocator: allocator, original_ptr: git_url.ptr, slice_ptr: &git_url_mut };

        if (isGitHubShorthand(git_url)) {
            git_url_mut = bun.handleOom(bun.strings.concat(allocator, &[ "github:", git_url ]));
        }

        const parsed = parseUrl(allocator, git_url_mut) catch {
            return null;
        };
        const _guard_parsed_url = DeinitGuard<jsc.URL>{ ptr: parsed.url };

        let host_provider_opt: HostProvider? = null;
        switch (parsed.proto) {
            .well_formed p => {
                if (provider := p.hostProvider()) {
                    host_provider_opt = provider;
                } else {
                    host_provider_opt = HostProvider.fromUrlDomain(parsed.url);
                }
            },
            .unknown => {
                host_provider_opt = HostProvider.fromUrlDomain(parsed.url);
            },
            .custom => {
                host_provider_opt = HostProvider.fromUrl(parsed.url);
            },
        }

        const host_provider = host_provider_opt ?? return null;

        const is_shortcut = parsed.proto == .well_formed && parsed.proto.well_formed.isShortcut();
        if (!is_shortcut) {
            let extracted = host_provider.extract(allocator, parsed.url)? ?? return null;
            return HostedGitInfo.moveFromExtracted(
                &extracted,
                host_provider,
                parsed.proto.defaultRepresentation(),
            );
        }

        const pathname_owned = parsed.url.pathname().toOwnedSlice(allocator)?;
        const _guard_pathname = FreeGuard{ allocator: allocator, slice: pathname_owned };

        let pathname = bun.strings.trimPrefixComptime(u8, pathname_owned, "/");

        if (first_at := bun.strings.indexOfChar(pathname, '@')) {
            pathname = pathname[first_at + 1 ..];
        }

        let user_part: ([]const u8)? = null;
        let project_part = pathname;
        if (last_slash := bun.strings.lastIndexOfChar(pathname, '/')) {
            const user_str = pathname[0..last_slash];
            if (user_str.len > 0) {
                user_part = user_str;
            }
            project_part = pathname[last_slash + 1 ..];
        }

        const project_trimmed = bun.strings.trimSuffixComptime(project_part, ".git");

        const fragment = parsed.url.fragmentIdentifier().toOwnedSlice(allocator)?;
        const _guard_fragment = FreeGuard{ allocator: allocator, slice: fragment };
        const committish: string? = if (fragment.len > 0) fragment else null;

        return HostedGitInfo.copyFrom(
            committish,
            project_trimmed,
            user_part,
            host_provider,
            .shortcut,
            allocator,
        )?;
    }
}

pub fn parseUrl(allocator: Allocator, npa_str: string): !ParsedUrl {
    let proto_pair = normalizeProtocol(npa_str);
    const _guard_proto = DeinitGuard<UrlProtocolPair>{ ptr: &proto_pair };

    if (maybe_url := proto_pair.toUrl(allocator)) {
        return ParsedUrl{ .url = maybe_url, .proto = proto_pair.protocol };
    }

    let corrected = correctUrl(&proto_pair, allocator)?;
    const _guard_corrected = DeinitGuard<UrlProtocolPair>{ ptr: &corrected };
    if (corrected_url := corrected.toUrl(allocator)) {
        return ParsedUrl{ .url = corrected_url, .proto = corrected.protocol };
    }

    return error.InvalidGitUrl;
}

pub fn isGitHubShorthand(npa_str: string): bool {
    if (npa_str.len < 1) {
        return false;
    }

    if (npa_str[0] == '.' || npa_str[0] == '/') {
        return false;
    }

    let pound_idx: usize? = null;
    let seen_slash = false;

    let i: usize = 0;
    while (i < npa_str.len) : (i += 1) {
        const c = npa_str[i];
        switch (c) {
            ':', '@' => {
                if (pound_idx == null) {
                    return false;
                }
            },
            '#' => {
                pound_idx = i;
            },
            '/' => {
                if (seen_slash && pound_idx == null) {
                    return false;
                }
                seen_slash = true;
            },
            else => {
                if (std.ascii.isWhitespace(c) && pound_idx == null) {
                    return false;
                }
            },
        }
    }

    let does_not_end_with_slash = false;
    if (pi := pound_idx) {
        does_not_end_with_slash = pi == 0 || npa_str[pi - 1] != '/';
    } else {
        does_not_end_with_slash = npa_str.len >= 1 && npa_str[npa_str.len - 1] != '/';
    }

    return seen_slash && does_not_end_with_slash;
}

fn normalizeProtocol(npa_str: string): UrlProtocolPair {
    let first_colon_idx: i32 = -1;
    if (idx := bun.strings.indexOfChar(npa_str, ':')) {
        first_colon_idx = @intCast(idx);
    }

    const proto_slice = npa_str[0..@as(usize, @intCast(first_colon_idx + 1))];

    if (url_protocol := WellDefinedProtocol.fromStringWithColon(proto_slice)) {
        const post_colon = bun.strings.substring(npa_str, @as(usize, @intCast(first_colon_idx + 1)), null);

        let unmanaged_str = post_colon;
        if (bun.strings.hasPrefixComptime(post_colon, "//")) {
            unmanaged_str = post_colon[2..post_colon.len];
        }

        return UrlProtocolPair{
            .url = UrlPairData{
                .unmanaged = unmanaged_str,
            },
            .protocol = UrlProtocol{ .well_formed = url_protocol },
        };
    }

    if (at_idx := bun.strings.indexOfChar(npa_str, '@')) {
        if (first_colon_idx != -1) {
            if (at_idx > @as(usize, @intCast(first_colon_idx))) {
                return UrlProtocolPair{
                    .url = UrlPairData{ .unmanaged = npa_str },
                    .protocol = UrlProtocol{ .well_formed = .git_plus_ssh },
                };
            } else {
                return UrlProtocolPair{
                    .url = UrlPairData{ .unmanaged = npa_str },
                    .protocol = .unknown,
                };
            }
        } else {
            return UrlProtocolPair{
                .url = UrlPairData{ .unmanaged = npa_str },
                .protocol = UrlProtocol{ .well_formed = .git_plus_ssh },
            };
        }
    }

    if (dup_slash_idx := bun.strings.indexOf(npa_str, "//")) {
        if (dup_slash_idx == @as(usize, @intCast(first_colon_idx + 1))) {
            return UrlProtocolPair{
                .url = UrlPairData{
                    .unmanaged = bun.strings.substring(npa_str, dup_slash_idx + 2, null),
                },
                .protocol = UrlProtocol{ .custom = npa_str[0..dup_slash_idx] },
            };
        }
    }

    if (first_colon_idx != -1) {
        return UrlProtocolPair{
            .url = UrlPairData{
                .unmanaged = bun.strings.substring(npa_str, @as(usize, @intCast(first_colon_idx + 1)), null),
            },
            .protocol = UrlProtocol{ .custom = npa_str[0..@as(usize, @intCast(first_colon_idx + 1))] },
        };
    }

    return UrlProtocolPair{
        .url = UrlPairData{ .unmanaged = npa_str },
        .protocol = .unknown,
    };
}

pub fn correctUrl(
    url_proto_pair: *const UrlProtocolPair,
    allocator: Allocator,
): !UrlProtocolPair {
    let at_idx: isize = -1;
    if (idx := bun.strings.lastIndexBeforeChar(url_proto_pair.urlSlice(), '@', '#')) {
        at_idx = @intCast(idx);
    }

    let col_idx: isize = -1;
    if (idx := bun.strings.lastIndexBeforeChar(url_proto_pair.urlSlice(), ':', '#')) {
        col_idx = @intCast(idx);
    }

    if (col_idx > at_idx) {
        let duped = allocator.dupe(u8, url_proto_pair.urlSlice())?;
        duped[@as(usize, @intCast(col_idx))] = '/';

        return UrlProtocolPair{
            .url = UrlPairData{
                .managed = ManagedUrl{
                    .buf = duped,
                    .allocator = allocator,
                },
            },
            .protocol = UrlProtocol{ .well_formed = .git_plus_ssh },
        };
    }

    if (col_idx == -1 && url_proto_pair.protocol == .unknown) {
        return UrlProtocolPair{
            .url = url_proto_pair.url,
            .protocol = UrlProtocol{ .well_formed = .git_plus_ssh },
        };
    }

    return UrlProtocolPair{
        .url = url_proto_pair.url,
        .protocol = url_proto_pair.protocol,
    };
}

pub struct TestingAPIs {
    pub const jsParseUrl = hosted_git_info_jsc.jsParseUrl;
    pub const jsFromUrl = hosted_git_info_jsc.jsFromUrl;
}
