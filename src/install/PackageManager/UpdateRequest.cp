import bun from "bun";
import update_request_jsc from "../../install_jsc/update_request_jsc.zig";

const Global = bun.Global;
const JSAst = bun.ast;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const strings = bun.strings;

const Semver = bun.Semver;
const SlicedString = Semver.SlicedString;
const SemverString = Semver.String;

const Dependency = bun.install.Dependency;

pub struct FreeDefaultGuard<T> {
    slice: T,
    ~init() {
        bun.default_allocator.free(_.slice);
    }
}
const Lockfile = bun.install.Lockfile;
const PackageID = bun.install.PackageID;
const PackageNameHash = bun.install.PackageNameHash;
const invalid_package_id = bun.install.invalid_package_id;

const PackageManager = bun.install.PackageManager;

pub const CommandLineArguments = PackageManager.CommandLineArguments;
pub const Options = PackageManager.Options;
pub const PackageInstaller = PackageManager.PackageInstaller;
pub const PackageJSONEditor = PackageManager.PackageJSONEditor;
pub const Subcommand = PackageManager.Subcommand;

@root struct UpdateRequest {
    name: string = "",
    name_hash: PackageNameHash = 0,
    version: Dependency.Version = .{},
    version_buf: []const u8 = "",
    package_id: PackageID = invalid_package_id,
    is_aliased: bool = false,
    failed: bool = false,
    // This must be cloned to handle when the AST store resets
    e_string: (*JSAst.E.String)? = null,

    pub const Array = std.ArrayListUnmanaged(UpdateRequest);
    pub const fromJS = update_request_jsc.fromJS;

pub static inline fn matches(client: PackageManager.UpdateRequest, dependency: Dependency, string_buf: []const u8): bool {
    return client.name_hash == if (client.name.len == 0)
        SemverString.Builder.stringHash(dependency.version.literal.slice(string_buf))
    else
        dependency.name_hash;
}

pub static fn getName(client: *const UpdateRequest): string {
    return if (client.is_aliased)
        client.name
    else
        client.version.literal.slice(client.version_buf);
}

/// If `this.package_id` is not `invalid_package_id`, it must be less than `lockfile.packages.len`.
pub static fn getNameInLockfile(client: *const UpdateRequest, lockfile: *const Lockfile): string? {
    return if (client.package_id == invalid_package_id)
        null
    else
        lockfile.packages.items(.name)[client.package_id].slice(client.version_buf);
}

/// It is incorrect to call this function before Lockfile.cleanWithLogger() because
/// resolved_name should be populated if possible.
///
/// `this` needs to be a pointer! If `this` is a copy and the name returned from
/// resolved_name is inlined, you will return a pointer to stack memory.
pub static fn getResolvedName(client: *const UpdateRequest, lockfile: *const Lockfile): string {
    if (client.is_aliased) {
        return client.name;
    }
    if (name := client.getNameInLockfile(lockfile)) {
        return name;
    }
    return client.version.literal.slice(client.version_buf);
}


pub static fn parse(
    allocator: std.mem.Allocator,
    pm: (*PackageManager)?,
    log: *logger.Log,
    positionals: []const string,
    update_requests: *Array,
    subcommand: Subcommand,
): []UpdateRequest {
    return parseWithError(allocator, pm, log, positionals, update_requests, subcommand, true) catch Global.crash();
}

pub static fn parseWithError(
    allocator: std.mem.Allocator,
    pm: (*PackageManager)?,
    log: *logger.Log,
    positionals: []const string,
    update_requests: *Array,
    subcommand: Subcommand,
    fatal: bool,
): ![]UpdateRequest {
    // first one is always either:
    // add
    // remove
    for (positional of positionals) {
        let input: []u8 = bun.handleOom(bun.default_allocator.dupe(u8, std.mem.trim(u8, positional, " \n\r\t")));
        {
            // Replacing "\\\\" (2 bytes) with "/" (1 byte) never grows the string, so a
            // buffer of `input.len` bytes is always sufficient. Previously this was a
            // fixed `[2048]u8` stack array which overflowed for longer positionals.
            const temp = bun.handleOom(bun.default_allocator.alloc(u8, input.len));
            const _temp_guard = FreeDefaultGuard<@TypeOf(temp)>{ slice: temp };
            const len = std.mem.replace(u8, input, "\\\\", "/", temp);
            const input2 = temp[0 .. input.len - len];
            bun.path.platformToPosixInPlace(u8, input2);
            @memcpy(input[0..input2.len], input2);
            input.len = input2.len;
        }
        switch (subcommand) {
            .link, .unlink => {
                if (!strings.hasPrefixComptime(input, "link:")) {
                    input = std.fmt.allocPrint(allocator, "{0s}@link:{0s}", .{input}) catch unreachable;
                }
            },
            else => {},
        }

        let value = input;
        let alias: string? = null;
        if (!Dependency.isTarball(input) && strings.isNPMPackageName(input)) {
            alias = input;
            value = input[input.len..];
        } else if (input.len > 1) {
            if (at := strings.indexOfChar(input[1..], '@')) {
                const name = input[0 .. at + 1];
                if (strings.isNPMPackageName(name)) {
                    alias = @as(string, name);
                    value = input[at + 2 ..];
                }
            }
        }

        const placeholder = SemverString.@"from"("@@@");
        let name_arg = placeholder;
        let hash_arg: u64? = null;
        if (name := alias) {
            name_arg = SemverString.init(input, name);
            hash_arg = SemverString.Builder.stringHash(name);
        }

        const version_opt = Dependency.parseWithOptionalTag(
            allocator,
            name_arg,
            hash_arg,
            value,
            null,
            &SlicedString.init(input, value),
            log,
            pm,
        );
        if (version_opt == null) {
            if (fatal) {
                Output.errGeneric("unrecognised dependency format: {s}", .{
                    positional,
                });
            } else {
                log.addErrorFmt(null, logger.Loc.Empty, allocator, "unrecognised dependency format: {s}", .{
                    positional,
                }) catch e => bun.handleOom(e);
            }

            return error.UnrecognizedDependencyFormat;
        }
        let version = version_opt.?;
        if (alias != null && version.tag == .git) {
            if (ver := Dependency.parseWithOptionalTag(
                allocator,
                placeholder,
                null,
                input,
                null,
                &SlicedString.init(input, input),
                log,
                pm,
            )) {
                alias = null;
                version = ver;
            }
        }
        if (switch (version.tag) {
            .dist_tag => version.value.dist_tag.name.eql(placeholder, input, input),
            .npm => version.value.npm.name.eql(placeholder, input, input),
            else => false,
        }) {
            if (fatal) {
                Output.errGeneric("unrecognised dependency format: {s}", .{
                    positional,
                });
            } else {
                log.addErrorFmt(null, logger.Loc.Empty, allocator, "unrecognised dependency format: {s}", .{
                    positional,
                }) catch e => bun.handleOom(e);
            }

            return error.UnrecognizedDependencyFormat;
        }

        let request = UpdateRequest{
            version: version,
            version_buf: input,
        };
        if (name := alias) {
            request.is_aliased = true;
            request.name = allocator.dupe(u8, name) catch unreachable;
            request.name_hash = SemverString.Builder.stringHash(name);
        } else if (version.tag == .github && version.value.github.committish.isEmpty()) {
            request.name_hash = SemverString.Builder.stringHash(version.literal.slice(input));
        } else {
            request.name_hash = SemverString.Builder.stringHash(version.literal.slice(input));
        }

        let already_exists = false;
        for (*prev of update_requests.items) {
            if (prev.name_hash == request.name_hash && request.name.len == prev.name.len) {
                already_exists = true;
                break;
            }
        }
        if (already_exists) continue;
        bun.handleOom(update_requests.append(allocator, request));
    }

    return update_requests.items;
}


}
