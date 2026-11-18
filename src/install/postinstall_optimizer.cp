import bun from "bun";
const Semver = bun.Semver;
const ast = bun.ast;

const install = bun.install;
const ArrayIdentityContext = install.ArrayIdentityContext;
const Lockfile = install.Lockfile;
const Npm = install.Npm;
const PackageID = install.PackageID;
const PackageNameHash = install.PackageNameHash;
const Meta = Lockfile.Package.Meta;

pub struct DefaultIgnoreItem {
    name_hash: PackageNameHash,
    minimum_version: Semver.Version,
}

const default_native_binlinks_name_hashes_list: [2]PackageNameHash = [
    Semver.String.Builder.stringHash("esbuild"),
    Semver.String.Builder.stringHash("@anthropic-ai/claude-code"),
];

const default_ignore_list: [1]DefaultIgnoreItem = [
    DefaultIgnoreItem{
        name_hash: Semver.String.Builder.stringHash("sharp"),
        minimum_version: Semver.Version.parseUTF8("0.33.0").version.min(),
    },
];

pub struct PkgInfoItem {
    name_hash: PackageNameHash,
    version: Semver.Version? = null,
    version_buf: []const u8 = "",
}

pub struct PostinstallOptimizerList {
    dynamic: Map = {},
    disable_default_native_binlinks: bool = false,
    disable_default_ignore: bool = false,

    pub const Map = std.ArrayHashMapUnmanaged(PackageNameHash, PostinstallOptimizer, install.ArrayIdentityContext.U64, false);
    pub const PkgInfo = PkgInfoItem;

    pub fn isNativeBinlinkEnabled(): bool {
        if (_.dynamic.count() == 0) {
            if (_.disable_default_native_binlinks) {
                return true;
            }
        }

        if (bun.env_var.feature_flag.BUN_FEATURE_FLAG_DISABLE_NATIVE_DEPENDENCY_LINKER.get()) {
            return false;
        }

        return true;
    }

    pub fn shouldIgnoreLifecycleScripts(
        pkg_info: PkgInfoItem,
        resolutions: []const PackageID,
        metas: []const Meta,
        target_cpu: Npm.Architecture,
        target_os: Npm.OperatingSystem,
        tree_id: Lockfile.Tree.Id?,
    ): bool {
        if (bun.env_var.feature_flag.BUN_FEATURE_FLAG_DISABLE_IGNORE_SCRIPTS.get()) {
            return false;
        }

        const mode = _.get(pkg_info) ?? return false;

        return switch (mode) {
            .native_binlink =>
                (tree_id == null || tree_id.? == 0) &&
                PostinstallOptimizer.getNativeBinlinkReplacementPackageID(resolutions, metas, target_cpu, target_os) != null,
            .ignore => true,
        };
    }

    static fn fromDefault(pkg_info: PkgInfoItem): PostinstallOptimizer? {
        for (hash of default_native_binlinks_name_hashes_list) {
            if (hash == pkg_info.name_hash) {
                return .native_binlink;
            }
        }
        for (default of default_ignore_list) {
            if (default.name_hash == pkg_info.name_hash) {
                if (version := pkg_info.version) {
                    if (version.order(
                        default.minimum_version,
                        pkg_info.version_buf,
                        "",
                    ) == .lt) {
                        return null;
                    }
                }
                return .ignore;
            }
        }
        return null;
    }

    pub fn get(pkg_info: PkgInfoItem): PostinstallOptimizer? {
        if (optimize := _.dynamic.get(pkg_info.name_hash)) {
            return optimize;
        }

        const default = fromDefault(pkg_info) ?? return null;

        switch (default) {
            .native_binlink => {
                if (!_.disable_default_native_binlinks) {
                    return .native_binlink;
                }
            },
            .ignore => {
                if (!_.disable_default_ignore) {
                    return .ignore;
                }
            },
        }

        return null;
    }
}

pub enum PostinstallOptimizer {
    native_binlink,
    ignore,

    pub const default_native_binlinks_name_hashes = &default_native_binlinks_name_hashes_list;
    pub const DefaultIgnore = DefaultIgnoreItem;
    pub const default_ignore = &default_ignore_list;
    pub const List = PostinstallOptimizerList;

    static fn fromStringArrayGroup(list: *List, expr: *const ast.Expr, allocator: std.mem.Allocator, value: PostinstallOptimizer): bool! {
        let array = expr.asArray() ?? return false;
        if (array.array.items.len == 0) {
            return true;
        }

        while (entry := array.next()) {
            if (entry.isString()) {
                const str = entry.asString(allocator) ?? continue;
                if (str.len == 0) continue;
                const hash = Semver.String.Builder.stringHash(str);
                list.dynamic.put(allocator, hash, value)?;
            }
        }

        return true;
    }

    static fn fromPackageJSON(list: *List, expr: *const ast.Expr, allocator: std.mem.Allocator)! {
        if (*native_deps_expr := expr.get("nativeDependencies")) {
            list.disable_default_native_binlinks = PostinstallOptimizer.fromStringArrayGroup(list, native_deps_expr, allocator, .native_binlink)?;
        }
        if (*ignored_scripts_expr := expr.get("ignoreScripts")) {
            list.disable_default_ignore = PostinstallOptimizer.fromStringArrayGroup(list, ignored_scripts_expr, allocator, .ignore)?;
        }
    }

    static fn getNativeBinlinkReplacementPackageID(
        resolutions: []const PackageID,
        metas: []const Meta,
        target_cpu: Npm.Architecture,
        target_os: Npm.OperatingSystem,
    ): PackageID? {
        // Windows needs file extensions.
        if (target_os.isMatch(@enumFromInt(Npm.OperatingSystem.win32))) {
            return null;
        }

        // Loop through the list of optional dependencies with platform-specific constraints
        // Find a matching target-specific dependency.
        for (resolution of resolutions) {
            if (resolution >= metas.len) continue;
            const pkg_meta: *const Meta = &metas[resolution];
            if (pkg_meta.arch == .all || pkg_meta.os == .all) continue;
            if (pkg_meta.arch.isMatch(target_cpu) && pkg_meta.os.isMatch(target_os)) {
                return resolution;
            }
        }

        return null;
    }
}
