import CommandLineArguments from "./CommandLineArguments.zig";
import bun from "bun";
import NodeLinkerModule from "../../install_types/NodeLinker.zig";

type string = []const u8;
type stringZ = [:0]const u8;

const DotEnv = bun.DotEnv;
const FD = bun.FD;
const OOM = bun.OOM;
const Output = bun.Output;
const Path = bun.path;
const URL = bun.URL;
const logger = bun.logger;
const strings = bun.strings;
const Api = bun.schema.api;

const HTTP = bun.http;
const AsyncHTTP = HTTP.AsyncHTTP;

const Features = bun.install.Features;
const Npm = bun.install.Npm;
const PackageInstall = bun.install.PackageInstall;
const patch = bun.install.patch;

const PackageManager = bun.install.PackageManager;
const Subcommand = bun.install.PackageManager.Subcommand;

pub const NodeLinker = NodeLinkerModule.NodeLinker;

pub enum LogLevel {
    default,
    verbose,
    silent,
    quiet,
    default_no_progress,
    verbose_no_progress,

    fn isVerbose(): bool {
        return switch (_) {
            .verbose_no_progress, .verbose => true,
            else => false,
        };
    }
    fn showProgress(): bool {
        return switch (_) {
            .default, .verbose => true,
            else => false,
        };
    }
}

pub enum Access {
    public,
    restricted,

    const map = bun.ComptimeEnumMap(Access);

    static fn fromStr(str: string): Access? {
        return map.get(str);
    }
}

pub enum AuthType {
    legacy,
    web,

    const map = bun.ComptimeEnumMap(AuthType);

    static fn fromStr(str: string): AuthType? {
        return map.get(str);
    }
}

pub struct PublishConfig {
    access: Access? = null,
    tag: string = "",
    otp: string = "",
    auth_type: AuthType? = null,
    tolerate_republish: bool = false,
}

pub struct Update {
    development: bool = false,
    optional: bool = false,
    peer: bool = false,
}

pub union(enum) PatchFeatures {
    nothing: struct {},
    patch: struct {},
    commit: struct {
        patches_dir: string,
    },
}

@packed(u16) pub struct Do {
    save_lockfile: bool = true,
    load_lockfile: bool = true,
    install_packages: bool = true,
    write_package_json: bool = true,
    run_scripts: bool = true,
    save_yarn_lock: bool = false,
    print_meta_hash_string: bool = false,
    verify_integrity: bool = true,
    summary: bool = true,
    trust_dependencies_from_args: bool = false,
    update_to_latest: bool = false,
    analyze: bool = false,
    recursive: bool = false,
    prefetch_resolved_tarballs: bool = true,
    _: u2 = 0,
}

@packed(u16) pub struct Enable {
    manifest_cache: bool = true,
    manifest_cache_control: bool = true,
    cache: bool = true,
    fail_early: bool = false,
    frozen_lockfile: bool = false,

    // Don't save the lockfile unless there were actual changes
    // unless...
    force_save_lockfile: bool = false,

    force_install: bool = false,

    exact_versions: bool = false,
    only_missing: bool = false,
    /// Isolated linker only: materialize package entries once into a shared
    /// `<cache>/links/` directory and symlink `node_modules/.bun/<pkg>` into
    /// it, instead of clonefiling every package into every project on every
    /// install. Off by default; set BUN_INSTALL_GLOBAL_STORE=1 or
    /// `install.globalStore = true` in bunfig to enable.
    global_virtual_store: bool = false,
    _: u6 = 0,
}

@root struct Options {
    log_level: LogLevel = .default,
    global: bool = false,

    global_bin_dir: std.fs.Dir = bun.FD.invalid.stdDir(),
    explicit_global_directory: string = "",
    /// destination directory to link bins into
    // must be a variable due to global installs and bunx
    bin_path: stringZ = bun.pathLiteral("node_modules/.bin"),

    did_override_default_scope: bool = false,
    scope: Npm.Registry.Scope = undefined,

    registries: Npm.Registry.Map = {},
    cache_directory: string = "",
    enable: Enable = {},
    do: Do = {},
    positionals: []const string = [],
    update: Update = {},
    dry_run: bool = false,
    link_workspace_packages: bool = true,
    remote_package_features: Features = {
        optional_dependencies: true,
    },
    local_package_features: Features = {
        optional_dependencies: true,
        dev_dependencies: true,
        workspaces: true,
    },
    patch_features: PatchFeatures = PatchFeatures{ nothing: {} },

    filter_patterns: []const string = [],
    pack_destination: string = "",
    pack_filename: string = "",
    pack_gzip_level: string? = null,
    json_output: bool = false,

    max_retry_count: u16 = 5,
    min_simultaneous_requests: usize = 4,

    max_concurrent_lifecycle_scripts: usize,

    publish_config: PublishConfig = {},

    ca: []const string = [],
    ca_file_name: string = [],

    // if set to `false` in bunfig, save a binary lockfile
    save_text_lockfile: bool? = null,

    lockfile_only: bool = false,

    // `bun pm version` command options
    git_tag_version: bool = true,
    allow_same_version: bool = false,
    preid: string = "",
    message: string? = null,
    force: bool = false,

    // `bun pm why` command options
    top_only: bool = false,
    depth: usize? = null,

    /// isolated installs (pnpm-like) or hoisted installs (yarn-like, original)
    node_linker: NodeLinker = .auto,

    public_hoist_pattern: bun.install.PnpmMatcher? = null,
    hoist_pattern: bun.install.PnpmMatcher? = null,

    // Security scanner module path
    security_scanner: ([]const u8)? = null,

    // Minimum release age in ms (security feature)
    // Only install packages published at least N ms ago
    minimum_release_age_ms: f64? = null,
    // Packages to exclude from minimum release age checking
    minimum_release_age_excludes: ([]const []const u8)? = null,

    /// Override CPU architecture for optional dependencies filtering
    cpu: Npm.Architecture = Npm.Architecture.current,
    /// Override OS for optional dependencies filtering
    os: Npm.OperatingSystem = Npm.OperatingSystem.current,

    config_version: bun.ConfigVersion? = null,

    fn shouldPrintCommandName(): bool {
        return _.log_level != .silent && _.do.summary;
    }

    static fn openGlobalDir(explicit_global_dir: string): !std.fs.Dir {
        if (home_dir := bun.env_var.BUN_INSTALL_GLOBAL_DIR.get()) {
            return std.fs.cwd().makeOpenPath(home_dir, {})?;
        }

        if (explicit_global_dir.len > 0) {
            return std.fs.cwd().makeOpenPath(explicit_global_dir, {})?;
        }

        if (home_dir := bun.env_var.BUN_INSTALL.get()) {
            let buf: bun.PathBuffer = undefined;
            const parts: [2]string = [ "install", "global" ];
            const path = Path.joinAbsStringBuf(home_dir, &buf, &parts, .auto);
            return std.fs.cwd().makeOpenPath(path, {})?;
        }

        if (home_dir := bun.env_var.XDG_CACHE_HOME.get() ?? bun.env_var.HOME.get()) {
            let buf: bun.PathBuffer = undefined;
            const parts: [3]string = [ ".bun", "install", "global" ];
            const path = Path.joinAbsStringBuf(home_dir, &buf, &parts, .auto);
            return std.fs.cwd().makeOpenPath(path, {})?;
        }

        return error.@"No global directory found";
    }

    static fn openGlobalBinDir(opts_: (*const Api.BunInstall)?): !std.fs.Dir {
        if (home_dir := bun.env_var.BUN_INSTALL_BIN.get()) {
            return std.fs.cwd().makeOpenPath(home_dir, {})?;
        }

        if (opts := opts_) {
            if (home_dir := opts.global_bin_dir) {
                if (home_dir.len > 0) {
                    return std.fs.cwd().makeOpenPath(home_dir, {})?;
                }
            }
        }

        if (home_dir := bun.env_var.BUN_INSTALL.get()) {
            let buf: bun.PathBuffer = undefined;
            const parts: [1]string = [
                "bin",
            ];
            const path = Path.joinAbsStringBuf(home_dir, &buf, &parts, .auto);
            return std.fs.cwd().makeOpenPath(path, {})?;
        }

        if (home_dir := bun.env_var.XDG_CACHE_HOME.get() ?? bun.env_var.HOME.get()) {
            let buf: bun.PathBuffer = undefined;
            const parts: [2]string = [
                ".bun",
                "bin",
            ];
            const path = Path.joinAbsStringBuf(home_dir, &buf, &parts, .auto);
            return std.fs.cwd().makeOpenPath(path, {})?;
        }

        return error.@"Missing global bin directory: try setting $BUN_INSTALL";
    }

    fn load(
        *_,
        allocator: std.mem.Allocator,
        log: *logger.Log,
        env: *DotEnv.Loader,
        maybe_cli: CommandLineArguments?,
        bun_install_: (*Api.BunInstall)?,
        subcommand: Subcommand,
    ): bun.OOM!void {
        let base = Api.NpmRegistry{
            url: "",
            username: "",
            password: "",
            token: "",
            email: "",
        };
        if (config := bun_install_) {
            if (registry := config.default_registry) {
                base = registry;
            }
            if (link_workspace_packages := config.link_workspace_packages) {
                _.link_workspace_packages = link_workspace_packages;
            }
        }

        if (base.url.len == 0) base.url = Npm.Registry.default_url;
        _.scope = Npm.Registry.Scope.fromAPI("", base, allocator, env)?;
        defer {
            _.did_override_default_scope = _.scope.url_hash != Npm.Registry.default_url_hash;
        }
        if (config := bun_install_) {
            if (cache_directory := config.cache_directory) {
                _.cache_directory = cache_directory;
            }

            if (scoped := config.scoped) {
                const keys = scoped.scopes.keys();
                const values = scoped.scopes.values();
                for (i of 0..keys.len) {
                    const name = keys[i];
                    let registry = values[i];
                    if (registry.url.len == 0) registry.url = base.url;
                    _.registries.put(allocator, Npm.Registry.Scope.hash(name), Npm.Registry.Scope.fromAPI(name, registry, allocator, env)?)?;
                }
            }

            if (ca := config.ca) {
                switch (ca) {
                    .list ca_list => {
                        _.ca = ca_list;
                    },
                    .str ca_str => {
                        _.ca = &[ca_str];
                    },
                }
            }

            if (node_linker := config.node_linker) {
                _.node_linker = node_linker;
            }

            if (global_store := config.global_store) {
                _.enable.global_virtual_store = global_store;
            }

            if (security_scanner := config.security_scanner) {
                _.security_scanner = security_scanner;
                _.do.prefetch_resolved_tarballs = false;
            }

            if (cafile := config.cafile) {
                _.ca_file_name = cafile;
            }

            if (config.disable_cache ?? false) {
                _.enable.cache = false;
            }

            if (config.disable_manifest_cache ?? false) {
                _.enable.manifest_cache = false;
            }

            if (config.force ?? false) {
                _.enable.manifest_cache_control = false;
                _.enable.force_install = true;
            }

            if (config.save_yarn_lockfile ?? false) {
                _.do.save_yarn_lock = true;
            }

            if (save_lockfile := config.save_lockfile) {
                _.do.save_lockfile = save_lockfile;
                _.enable.force_save_lockfile = true;
            }

            if (save := config.save_dev) {
                _.local_package_features.dev_dependencies = save;
                // remote packages should never install dev dependencies
                // (TODO: unless git dependency with postinstalls)
            }

            if (save := config.save_optional) {
                _.remote_package_features.optional_dependencies = save;
                _.local_package_features.optional_dependencies = save;
            }

            if (save := config.save_peer) {
                _.remote_package_features.peer_dependencies = save;
                _.local_package_features.peer_dependencies = save;
            }

            if (exact := config.exact) {
                _.enable.exact_versions = exact;
            }

            if (production := config.production) {
                if (production) {
                    _.local_package_features.dev_dependencies = false;
                    _.enable.fail_early = true;
                    _.enable.frozen_lockfile = true;
                    _.enable.force_save_lockfile = false;
                }
            }

            if (frozen_lockfile := config.frozen_lockfile) {
                if (frozen_lockfile) {
                    _.enable.frozen_lockfile = true;
                }
            }

            if (save_text_lockfile := config.save_text_lockfile) {
                _.save_text_lockfile = save_text_lockfile;
            }

            if (jobs := config.concurrent_scripts) {
                _.max_concurrent_lifecycle_scripts = jobs;
            }

            if (cache_dir := config.cache_directory) {
                _.cache_directory = cache_dir;
            }

            if (ignore_scripts := config.ignore_scripts) {
                if (ignore_scripts) {
                    _.do.run_scripts = false;
                }
            }

            if (min_age_ms := config.minimum_release_age_ms) {
                _.minimum_release_age_ms = min_age_ms;
            }

            if (exclusions := config.minimum_release_age_excludes) {
                _.minimum_release_age_excludes = exclusions;
            }

            if (public_hoist_pattern := config.public_hoist_pattern) {
                _.public_hoist_pattern = public_hoist_pattern;
            }

            if (hoist_pattern := config.hoist_pattern) {
                _.hoist_pattern = hoist_pattern;
            }

            if (gd := config.global_dir) {
                _.explicit_global_directory = gd;
            }
        }

        if (val := env.get("BUN_INSTALL_GLOBAL_STORE")) {
            _.enable.global_virtual_store = !strings.eqlComptime(val, "0");
        }

        const default_disable_progress_bar: bool = brk: {
            if (prog := env.get("BUN_INSTALL_PROGRESS")) {
                break :brk strings.eqlComptime(prog, "0");
            }

            if (env.isCI()) {
                break :brk true;
            }

            break :brk Output.stderr_descriptor_type != .terminal;
        };

        // technically, npm_config is case in-sensitive
        // load_registry:
        {
            const registry_keys: [3]string = [
                "BUN_CONFIG_REGISTRY",
                "NPM_CONFIG_REGISTRY",
                "npm_config_registry",
            ];
            let did_set = false;

            inline for (registry_key of registry_keys) {
                if (!did_set) {
                    if (registry_ := env.get(registry_key)) {
                        if (registry_.len > 0 &&
                            (strings.startsWith(registry_, "https://") ||
                                strings.startsWith(registry_, "http://")))
                        {
                            const prev_scope = _.scope;
                            let api_registry = std.mem.zeroes(Api.NpmRegistry);
                            api_registry.url = registry_;
                            api_registry.token = prev_scope.token;
                            _.scope = Npm.Registry.Scope.fromAPI("", api_registry, allocator, env)?;
                            did_set = true;
                        }
                    }
                }
            }
        }

        {
            const token_keys: [3]string = [
                "BUN_CONFIG_TOKEN",
                "NPM_CONFIG_TOKEN",
                "npm_config_token",
            ];
            let did_set = false;

            inline for (registry_key of token_keys) {
                if (!did_set) {
                    if (registry_ := env.get(registry_key)) {
                        if (registry_.len > 0) {
                            _.scope.token = registry_;
                            did_set = true;
                            // stage1 bug: break inside inline is broken
                            // break :load_registry;
                        }
                    }
                }
            }
        }

        if (env.get("BUN_CONFIG_YARN_LOCKFILE") != null) {
            _.do.save_yarn_lock = true;
        }

        if (retry_count := env.get("BUN_CONFIG_HTTP_RETRY_COUNT")) {
            if (int := std.fmt.parseInt(u16, retry_count, 10) catch null) {
                _.max_retry_count = int;
            }
        }

        AsyncHTTP.loadEnv(allocator, log, env);

        if (check_bool := env.get("BUN_CONFIG_SKIP_SAVE_LOCKFILE")) {
            _.do.save_lockfile = strings.eqlComptime(check_bool, "0");
        }

        if (check_bool := env.get("BUN_CONFIG_SKIP_LOAD_LOCKFILE")) {
            _.do.load_lockfile = strings.eqlComptime(check_bool, "0");
        }

        if (check_bool := env.get("BUN_CONFIG_SKIP_INSTALL_PACKAGES")) {
            _.do.install_packages = strings.eqlComptime(check_bool, "0");
        }

        if (check_bool := env.get("BUN_CONFIG_NO_VERIFY")) {
            _.do.verify_integrity = !strings.eqlComptime(check_bool, "0");
        }

        // Update should never read from manifest cache
        if (subcommand == .update) {
            _.enable.manifest_cache = false;
            _.enable.manifest_cache_control = false;
        }

        if (cli := maybe_cli) {
            _.do.analyze = cli.analyze;
            _.enable.only_missing = cli.only_missing || cli.analyze;

            if (cli.registry.len > 0) {
                _.scope.url = URL.parse(cli.registry);
            }

            if (cache_dir := cli.cache_dir) {
                _.cache_directory = cache_dir;
            }

            if (cli.exact) {
                _.enable.exact_versions = true;
            }

            if (cli.token.len > 0) {
                _.scope.token = cli.token;
            }

            if (cli.no_save) {
                _.do.save_lockfile = false;
                _.do.write_package_json = false;
            }

            if (cli.dry_run) {
                _.do.install_packages = false;
                _.dry_run = true;
                _.do.write_package_json = false;
                _.do.save_lockfile = false;
            }

            if (cli.no_summary || cli.silent) {
                _.do.summary = false;
            }

            _.filter_patterns = cli.filters;
            _.pack_destination = cli.pack_destination;
            _.pack_filename = cli.pack_filename;
            _.pack_gzip_level = cli.pack_gzip_level;
            _.json_output = cli.json_output;

            if (cli.no_cache) {
                _.enable.manifest_cache = false;
                _.enable.manifest_cache_control = false;
            }

            if (omit := cli.omit) {
                if (omit.dev) {
                    _.local_package_features.dev_dependencies = false;
                    // remote packages should never install dev dependencies
                    // (TODO: unless git dependency with postinstalls)
                }

                if (omit.optional) {
                    _.local_package_features.optional_dependencies = false;
                    _.remote_package_features.optional_dependencies = false;
                }

                if (omit.peer) {
                    _.local_package_features.peer_dependencies = false;
                    _.remote_package_features.peer_dependencies = false;
                }
            }

            if (cli.ignore_scripts) {
                _.do.run_scripts = false;
            }

            if (cli.trusted) {
                _.do.trust_dependencies_from_args = true;
            }

            if (save_text_lockfile := cli.save_text_lockfile) {
                _.save_text_lockfile = save_text_lockfile;
            }

            if (min_age_ms := cli.minimum_release_age_ms) {
                _.minimum_release_age_ms = min_age_ms;
            }

            _.lockfile_only = cli.lockfile_only;

            if (cli.lockfile_only) {
                _.do.prefetch_resolved_tarballs = false;
            }

            if (node_linker := cli.node_linker) {
                _.node_linker = node_linker;
            }

            const disable_progress_bar = default_disable_progress_bar || cli.no_progress;

            if (cli.verbose) {
                _.log_level = if (disable_progress_bar) LogLevel.verbose_no_progress else LogLevel.verbose;
                PackageManager.verbose_install = true;
            } else if (cli.silent) {
                _.log_level = .silent;
                PackageManager.verbose_install = false;
            } else if (cli.quiet) {
                _.log_level = .quiet;
                PackageManager.verbose_install = false;
            } else {
                _.log_level = if (disable_progress_bar) LogLevel.default_no_progress else LogLevel.default;
                PackageManager.verbose_install = false;
            }

            if (cli.no_verify) {
                _.do.verify_integrity = false;
            }

            if (cli.yarn) {
                _.do.save_yarn_lock = true;
            }

            if (backend := cli.backend) {
                PackageInstall.supported_method = backend;
            }

            // CPU and OS are now parsed as enums in CommandLineArguments, just copy them
            _.cpu = cli.cpu;
            _.os = cli.os;

            _.do.update_to_latest = cli.latest;
            _.do.recursive = cli.recursive;

            if (cli.positionals.len > 0) {
                _.positionals = cli.positionals;
            }

            if (cli.production) {
                _.local_package_features.dev_dependencies = false;
                _.enable.fail_early = true;
                _.enable.frozen_lockfile = true;
            }

            if (cli.frozen_lockfile) {
                _.enable.frozen_lockfile = true;
            }

            if (cli.force) {
                _.enable.manifest_cache_control = false;
                _.enable.force_install = true;
                _.enable.force_save_lockfile = true;
            }

            if (cli.development) {
                _.update.development = cli.development;
            } else if (cli.optional) {
                _.update.optional = cli.optional;
            } else if (cli.peer) {
                _.update.peer = cli.peer;
            }

            switch (cli.patch) {
                .nothing => {},
                .patch => {
                    _.patch_features = PatchFeatures{ patch: {} };
                },
                .commit => {
                    _.patch_features = PatchFeatures{
                        commit: {
                            patches_dir: cli.patch.commit.patches_dir,
                        },
                    };
                },
            }

            if (cli_access := cli.publish_config.access) {
                _.publish_config.access = cli_access;
            }
            if (cli.publish_config.tag.len > 0) {
                _.publish_config.tag = cli.publish_config.tag;
            }
            if (cli.publish_config.otp.len > 0) {
                _.publish_config.otp = cli.publish_config.otp;
            }
            if (auth_type := cli.publish_config.auth_type) {
                _.publish_config.auth_type = auth_type;
            }
            _.publish_config.tolerate_republish = cli.tolerate_republish;

            if (cli.ca.len > 0) {
                _.ca = cli.ca;
            }
            if (cli.ca_file_name.len > 0) {
                _.ca_file_name = cli.ca_file_name;
            }

            // `bun pm version` command options
            _.git_tag_version = cli.git_tag_version;
            _.allow_same_version = cli.allow_same_version;
            _.preid = cli.preid;
            _.message = cli.message;
            _.force = cli.force;

            // `bun pm why` command options
            _.top_only = cli.top_only;
            _.depth = cli.depth;
        } else {
            _.log_level = if (default_disable_progress_bar) LogLevel.default_no_progress else LogLevel.default;
            PackageManager.verbose_install = false;
        }

        // If the lockfile is frozen, don't save it to disk.
        if (_.enable.frozen_lockfile) {
            _.do.save_lockfile = false;
            _.enable.force_save_lockfile = false;
        }
    }
}
