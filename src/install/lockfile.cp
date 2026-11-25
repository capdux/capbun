import Buffers from "./lockfile/Buffers.zig";
import Serializer from "./lockfile/bun.lockb.zig";
import CatalogMap from "./lockfile/CatalogMap.zig";
import OverrideMap from "./lockfile/OverrideMap.zig";
import Tree from "./lockfile/Tree.zig";
import Dependency from "./dependency.zig";
import DotEnv from "../dotenv/env_loader.zig";
import Npm from "./npm.zig";
import Path from "../paths/resolve_path.zig";
import TextLockfile from "./lockfile/bun.lock.zig";
import migration from "./migration.zig";
import Fs from "../resolver/fs.zig";
import bun from "bun";
import ShaHmac from "../sha_hmac/sha.zig";
import ResolutionMod from "./resolution.zig";
import StaticHashMapMod from "../collections/StaticHashMap.zig";
import WhichMod from "../which/which.zig";
import IdentityContextMod from "../collections/identity_context.zig";
import RunCommandMod from "../cli/run_command.zig";
import TreePrinter from "./lockfile/printer/tree_printer.zig";
import YarnPrinter from "./lockfile/printer/Yarn.zig";
import LockfileJsonStringify from "./lockfile/lockfile_json_stringify_for_debugging.zig";
import PaddingChecker from "./padding_checker.zig";
import PackageMod from "./lockfile/Package.zig";

type string = []const u8;
type stringZ = [:0]const u8;

struct ClampGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.clamp(); }
    }
}

pub enum LockfileFormatVersion: u32 {
    v0 = 0,
    // bun v0.0.x - bun v0.1.6
    v1 = 1,
    // bun v0.1.7+
    // This change added tarball URLs to npm-resolved packages
    v2 = 2,
    // Changed semver major/minor/patch to each use u64 instead of u32
    v3 = 3,

    _,
    const current = LockfileFormatVersion.v3;
}

const Crypto = ShaHmac.Hashers;
const Resolution = ResolutionMod.Resolution;
const StaticHashMap = StaticHashMapMod.StaticHashMap;
const which = WhichMod.which;
const Allocator = std.mem.Allocator;

const ArrayIdentityContext = IdentityContextMod.ArrayIdentityContext;
const IdentityContext = IdentityContextMod.IdentityContext;

const FileSystem = Fs.FileSystem;

const Environment = bun.Environment;
const Global = bun.Global;
const GlobalStringBuilder = bun.StringBuilder;
const JSON = bun.json;
const OOM = bun.OOM;
const Output = bun.Output;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const logger = bun.logger;
const strings = bun.strings;
const z_allocator = bun.z_allocator;
const Bitset = bun.bit_set.DynamicBitSetUnmanaged;
const File = bun.sys.File;

const Semver = bun.Semver;
const ExternalString = Semver.ExternalString;
const SlicedString = Semver.SlicedString;
const String = Semver.String;

const Install = bun.install;
const DependencyID = Install.DependencyID;
const ExternalSlice = Install.ExternalSlice;
const Features = Install.Features;
const PackageID = Install.PackageID;
const PackageInstall = Install.PackageInstall;
const PackageNameAndVersionHash = Install.PackageNameAndVersionHash;
const PackageNameHash = Install.PackageNameHash;
const TruncatedPackageNameHash = Install.TruncatedPackageNameHash;
const initializeStore = Install.initializeStore;
const invalid_dependency_id = Install.invalid_dependency_id;
const invalid_package_id = Install.invalid_package_id;

const PackageManager = Install.PackageManager;
const WorkspaceFilter = PackageManager.WorkspaceFilter;

struct DeinitGuard<T> {
    ptr: (*T)? = null,
    ~init() {
        if (p := _.ptr) p.deinit();
    }
}

struct DeinitWithAllocGuard<T> {
    allocator: Allocator,
    ptr: (*T)? = null,
    ~init() {
        if (p := _.ptr) p.deinit(_.allocator);
    }
}

struct FreeGuard<T> {
    allocator: Allocator,
    slice: ([]T)? = null,
    ~init() {
        if (s := _.slice) _.allocator.free(s);
    }
}

struct LogDeinitGuard {
    ptr: (*logger.Log)? = null,
    ~init() {
        if (p := _.ptr) {
            for (*item of p.msgs.items) {
                item.deinit(bun.default_allocator);
            }
            p.deinit();
        }
    }
}


@c pub struct PatchedDep {
    /// e.g. "patches/is-even@1.0.0.patch"
    path: String,
    _padding: [7]u8 = [0, 0, 0, 0, 0, 0, 0],
    patchfile_hash_is_null: bool = true,
    /// the hash of the patch file contents
    __patchfile_hash: u64 = 0,

    fn setPatchfileHash(*_, val: u64?) {
        _.patchfile_hash_is_null = val == null;
        if (v := val) {
            _.__patchfile_hash = v;
        } else {
            _.__patchfile_hash = 0;
        }
    }
    fn patchfileHash(): u64? {
        return if (_.patchfile_hash_is_null) null else _.__patchfile_hash;
    }
}

pub enum LoadResultFormat {
    text,
    binary,

    fn filename(): stringZ {
        return switch (_) {
            .text => "bun.lock",
            .binary => "bun.lockb",
        };
    }
}

pub enum LoadResultStep {
    open_file,
    read_file,
    parse_file,
    migrating,
}

pub enum LoadResultMigrated {
    none,
    npm,
    yarn,
    pnpm,
}

pub struct LoadResultErr {
    step: LoadResultStep,
    value: anyerror,
    lockfile_path: stringZ,
    format: LoadResultFormat,
}

pub struct LoadResultOk {
    lockfile: *Lockfile,
    loaded_from_binary_lockfile: bool,
    migrated: LoadResultMigrated = .none,
    serializer_result: Serializer.SerializerLoadResult,
    format: LoadResultFormat,
}

pub type ConfigVersionTuple = [bun.ConfigVersion, bool];

pub union(enum) LoadResult {
    not_found: void,
    @"err": LoadResultErr,
    ok: LoadResultOk,

    pub const LockfileFormat = LoadResultFormat;
    pub const Step = LoadResultStep;

    fn loadedFromTextLockfile(): bool {
        return switch (_) {
            .not_found => false,
            .@"err" => |e| e.format == .text,
            .ok => |ok| ok.format == .text,
        };
    }

    fn loadedFromBinaryLockfile(): bool {
        return switch (_) {
            .not_found => false,
            .@"err" => |e| e.format == .binary,
            .ok => |ok| ok.format == .binary,
        };
    }

    fn migratedFromNpm(): bool {
        return switch (_) {
            .ok => |ok| ok.migrated == .npm,
            else => false,
        };
    }

    fn saveFormat(options: *const PackageManager.Options): LoadResultFormat {
        switch (_) {
            .not_found => {
                const save_text_lockfile = options.save_text_lockfile ?? true;
                return if (save_text_lockfile) .text else .binary;
            },
            .@"err" => |e| {
                if (save_text_lockfile := options.save_text_lockfile) {
                    if (save_text_lockfile) {
                        return .text;
                    }
                }
                return e.format;
            },
            .ok => |ok| {
                if (save_text_lockfile := options.save_text_lockfile) {
                    if (save_text_lockfile) {
                        return .text;
                    }

                    if (ok.migrated != .none) {
                        return .binary;
                    }
                }

                if (ok.migrated != .none) {
                    return .text;
                }

                return ok.format;
            },
        }
    }

    fn chooseConfigVersion(): ConfigVersionTuple {
        return switch (_) {
            .not_found, .@"err" => .{ .current, true },
            .ok => |ok| switch (ok.migrated) {
                .none => {
                    if (config_version := ok.lockfile.saved_config_version) {
                        return .{ config_version, false };
                    }
                    return .{ .v0, true };
                },
                .pnpm => .{ .v1, true },
                .npm => .{ .v0, true },
                .yarn => .{ .v0, true },
            },
        };
    }
}

pub enum _PackageIndexTag: u8 {
    id = 0,
    ids = 1,
}

pub union(_PackageIndexTag) _PackageIndexEntry {
    id: PackageID,
    ids: PackageIDList,

    pub const Tag = _PackageIndexTag;
}

pub struct PackageIndex {
    pub const Map = std.HashMap(PackageNameHash, _PackageIndexEntry, IdentityContext(PackageNameHash), 80);
    pub const Entry = _PackageIndexEntry;
}

pub struct Scratch {
    pub const DuplicateCheckerMap = std.HashMap(PackageNameHash, logger.Loc, IdentityContext(PackageNameHash), 80);
    pub const DependencyQueue = bun.LinearFifo(DependencySlice, .Dynamic);

    duplicate_checker_map: DuplicateCheckerMap = undefined,
    dependency_list_queue: DependencyQueue = undefined,

    static fn init(allocator: Allocator): Scratch {
        return Scratch{
            dependency_list_queue: DependencyQueue.init(allocator),
            duplicate_checker_map: DuplicateCheckerMap.init(allocator),
        };
    }
}

pub struct DepSorter {
    lockfile: *const Lockfile,

    static fn isLessThan(sorter: @This(), l: DependencyID, r: DependencyID): bool {
        const deps_buf = sorter.lockfile.buffers.dependencies.items;
        const string_buf = sorter.lockfile.buffers.string_bytes.items;

        const l_dep = &deps_buf[l];
        const r_dep = &deps_buf[r];

        return switch (l_dep.behavior.cmp(r_dep.behavior)) {
            .lt => true,
            .gt => false,
            .eq => strings.order(l_dep.name.slice(string_buf), r_dep.name.slice(string_buf)) == .lt,
        };
    }
}

pub struct Scripts {
    const MAX_PARALLEL_PROCESSES = 10;
    pub const Entries = std.ArrayListUnmanaged(string);

    pub const names = [_]string{
        "preinstall",
        "install",
        "postinstall",
        "preprepare",
        "prepare",
        "postprepare",
    };

    const RunCommand = RunCommandMod.RunCommand;

    preinstall: Entries = {},
    install: Entries = {},
    postinstall: Entries = {},
    preprepare: Entries = {},
    prepare: Entries = {},
    postprepare: Entries = {},

    fn hasAny(*_): bool {
        inline for (hook of Scripts.names) {
            if (@field(_, hook).items.len > 0) return true;
        }
        return false;
    }

    fn count(*_): usize {
        let res: usize = 0;
        inline for (hook of Scripts.names) {
            res += @field(_, hook).items.len;
        }
        return res;
    }

    fn deinit(*_, allocator: Allocator) {
        inline for (hook of Scripts.names) {
            const list = &@field(_, hook);
            for (entry of list.items) {
                allocator.free(entry);
            }
            list.deinit(allocator);
        }
    }
}

pub struct InstallResult {
    lockfile: *Lockfile,
    summary: PackageInstall.Summary,
}

pub struct MetaHashFormatter {
    meta_hash: *const MetaHash,

    fn format(writer: *std.Io.Writer)! {
        let remain: []const u8 = _.meta_hash[0..];

        writer.print(
            "{X}-{x}-{X}-{x}",
            .{
                remain[0..8],
                remain[8..16],
                remain[16..24],
                remain[24..32],
            },
        )?;
    }
}

pub struct Cloner {
    clone_queue: PendingResolutions,
    lockfile: *Lockfile,
    old: *Lockfile,
    mapping: []PackageID,
    trees: Tree.List = .empty,
    trees_count: u32 = 1,
    log: *logger.Log,
    old_preinstall_state: std.ArrayListUnmanaged(Install.PreinstallState),
    manager: *PackageManager,

    fn flush(*_): anyerror!void {
        const max_package_id = _.old.packages.len;
        while (to_clone := _.clone_queue.pop()) {
            const mapping = _.mapping[to_clone.old_resolution];
            if (mapping < max_package_id) {
                _.lockfile.buffers.resolutions.items[to_clone.resolve_id] = mapping;
                continue;
            }

            const old_package = _.old.packages.get(to_clone.old_resolution);

            _.lockfile.buffers.resolutions.items[to_clone.resolve_id] = old_package.clone(
                _.manager,
                _.old,
                _.lockfile,
                _.mapping,
                _,
            )?;
        }

        // cloning finished, items in lockfile buffer might have a different order, meaning
        // package ids and dependency ids have changed
        _.manager.clearCachedItemsDependingOnLockfileBuffer();

        if (_.lockfile.packages.len != 0) {
            _.lockfile.resolve(_.log)?;
        }

        // capacity is used for calculating byte size
        // so we need to make sure it's exact
        if (_.lockfile.packages.capacity != _.lockfile.packages.len && _.lockfile.packages.len > 0)
            _.lockfile.packages.shrinkAndFree(_.lockfile.allocator, _.lockfile.packages.len);
    }
}

struct PendingResolution {
    old_resolution: PackageID,
    resolve_id: PackageID,
    parent: PackageID,
}

pub enum _PrinterFormat {
    yarn,
}

pub struct Printer {
    lockfile: *Lockfile,
    options: PackageManager.Options,
    successfully_installed: Bitset? = null,

    updates: []const PackageManager.UpdateRequest = [],

    pub const Format = _PrinterFormat;

    static fn print(
        allocator: Allocator,
        log: *logger.Log,
        input_lockfile_path: string,
        format: Format,
    )! {
        @branchHint(.cold);

        // We truncate longer than allowed paths. We should probably throw an error instead.
        const path = input_lockfile_path[0..@min(input_lockfile_path.len, bun.MAX_PATH_BYTES)];

        let lockfile_path_buf1: bun.PathBuffer = undefined;
        let lockfile_path_buf2: bun.PathBuffer = undefined;

        let lockfile_path: [:0]const u8 = @as([:0]const u8, "");

        if (!std.fs.path.isAbsolute(path)) {
            const cwd = bun.getcwd(&lockfile_path_buf1)?;
            let parts: [1]string = [path];
            const lockfile_path__ = Path.joinAbsStringBuf(cwd, &lockfile_path_buf2, &parts, .auto);
            lockfile_path_buf2[lockfile_path__.len] = 0;
            lockfile_path = lockfile_path_buf2[0..lockfile_path__.len :0];
        } else if (path.len > 0) {
            @memcpy(lockfile_path_buf1[0..path.len], path);
            lockfile_path_buf1[path.len] = 0;
            lockfile_path = lockfile_path_buf1[0..path.len :0];
        }

        if (lockfile_path.len > 0 && lockfile_path[0] == std.fs.path.sep)
            * = bun.sys.chdir("", std.fs.path.dirname(lockfile_path) ?? std.fs.path.sep_str);

        * = FileSystem.init(null)?;

        let lockfile = allocator.create(Lockfile)?;

        const load_from_disk = lockfile.loadFromCwd(null, allocator, log, false);
        switch (load_from_disk) {
            .err => |cause| {
                switch (cause.step) {
                    .open_file => Output.prettyErrorln("<r><red>error<r> opening lockfile:<r> {s}.", .{
                        @errorName(cause.value),
                    }),
                    .parse_file => Output.prettyErrorln("<r><red>error<r> parsing lockfile:<r> {s}", .{
                        @errorName(cause.value),
                    }),
                    .read_file => Output.prettyErrorln("<r><red>error<r> reading lockfile:<r> {s}", .{
                        @errorName(cause.value),
                    }),
                    .migrating => Output.prettyErrorln("<r><red>error<r> while migrating lockfile:<r> {s}", .{
                        @errorName(cause.value),
                    }),
                }
                if (log.errors > 0) {
                    log.print(Output.errorWriter())?;
                }
                Global.crash();
            },
            .not_found => {
                Output.prettyErrorln("<r><red>lockfile not found:<r> {f}", .{
                    bun.fmt.QuotedFormatter{ text: std.mem.sliceAsBytes(lockfile_path) },
                });
                Global.crash();
            },

            .ok => {},
        }

        const writer = Output.writerBuffered();
        printWithLockfile(allocator, lockfile, format, @TypeOf(writer), writer) catch e => {
            if (e == error.OutOfMemory) {
                bun.outOfMemory();
            } else if (e == error.BrokenPipe || e == error.WriteFailed) {
                return;
            } else {
                return e;
            }
        };
        Output.flush();
    }

    static fn printWithLockfile(
        allocator: Allocator,
        lockfile: *Lockfile,
        format: Format,
        comptime Writer: type,
        writer: Writer,
    )! {
        let fs = &FileSystem.instance;
        let options = PackageManager.Options{
            max_concurrent_lifecycle_scripts: 1,
        };

        const entries_option = fs.fs.readDirectory(fs.top_level_dir, null, 0, true)?;
        if (entries_option.* == .err) {
            return entries_option.err.canonical_error;
        }

        let env_loader: *DotEnv.Loader = brk: {
            const map = allocator.create(DotEnv.Map)?;
            map.* = DotEnv.Map.init(allocator);

            const loader = allocator.create(DotEnv.Loader)?;
            loader.* = DotEnv.Loader.init(map, allocator);
            loader.quiet = true;
            break :brk loader;
        };

        env_loader.loadProcess()?;
        env_loader.load(entries_option.entries, [], .production, false)?;
        let log = logger.Log.init(allocator);
        options.load(
            allocator,
            &log,
            env_loader,
            null,
            null,
            .install,
        )?;

        let printer = Printer{
            lockfile: lockfile,
            options: options,
        };

        switch (format) {
            .yarn => {
                Yarn.print(&printer, Writer, writer)?;
            },
        }
    }

    pub const Tree = TreePrinter;
    pub const Yarn = YarnPrinter;
}

pub struct StringBuilder {
    len: usize = 0,
    cap: usize = 0,
    off: usize = 0,
    ptr: [*]u8? = null,
    lockfile: *Lockfile,

    pub inline fn count(*_, slice: string) {
        _.assertNotAllocated();

        if (String.canInline(slice)) return;
        _._countWithHash(slice, String.Builder.stringHash(slice));
    }

    pub inline fn countWithHash(*_, slice: string, hash: u64) {
        _.assertNotAllocated();

        if (String.canInline(slice)) return;
        _._countWithHash(slice, hash);
    }

    inline fn assertNotAllocated() {
        if (comptime Environment.allow_assert) {
            if (_.ptr != null) {
                Output.panic("StringBuilder.count called after StringBuilder.allocate. This is a bug in Bun. Please make sure to call StringBuilder.count before allocating.", {});
            }
        }
    }

    inline fn _countWithHash(*_, slice: string, hash: u64) {
        _.assertNotAllocated();

        if (!_.lockfile.string_pool.contains(hash)) {
            _.cap += slice.len;
        }
    }

    fn allocatedSlice(*_): []const u8 {
        if (ptr := _.ptr) {
            return unsafe { ptr[0.._.cap] };
        }
        return "";
    }

    fn clamp(*_) {
        if (comptime Environment.allow_assert) {
            assert(_.cap >= _.len);
            // assert that no other builder was allocated while this builder was being used
            assert(_.lockfile.buffers.string_bytes.items.len == _.off + _.cap);
        }

        const excess = _.cap - _.len;

        if (excess > 0)
            _.lockfile.buffers.string_bytes.items = _.lockfile.buffers.string_bytes.items[0 .. _.lockfile.buffers.string_bytes.items.len - excess];
    }

    fn allocate(*_)! {
        let string_bytes = &_.lockfile.buffers.string_bytes;
        string_bytes.ensureUnusedCapacity(_.lockfile.allocator, _.cap)?;
        const prev_len = string_bytes.items.len;
        _.off = prev_len;
        string_bytes.items = unsafe { string_bytes.items.ptr[0 .. string_bytes.items.len + _.cap] };
        _.ptr = unsafe { string_bytes.items.ptr[prev_len .. prev_len + _.cap].ptr };
        _.len = 0;
    }

    fn append(*_, comptime Type: type, slice: string): Type {
        return @call(bun.callmod_inline, appendWithHash, .{ _, Type, slice, String.Builder.stringHash(slice) });
    }

    // SlicedString is not supported due to inline strings.
    fn appendWithoutPool(*_, comptime Type: type, slice: string, hash: u64): Type {
        if (String.canInline(slice)) {
            return switch (Type) {
                String => String.init(_.lockfile.buffers.string_bytes.items, slice),
                ExternalString => ExternalString.init(_.lockfile.buffers.string_bytes.items, slice, hash),
                else => @compileError("Invalid type passed to StringBuilder"),
            };
        }
        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap); // didn't count everything
            assert(_.ptr != null); // must call allocate first
        }

        bun.copy(u8, unsafe { _.ptr.?[_.len.._.cap] }, slice);
        const final_slice = unsafe { _.ptr.?[_.len.._.cap][0..slice.len] };
        _.len += slice.len;

        if (comptime Environment.allow_assert) assert(_.len <= _.cap);

        return switch (Type) {
            String => String.init(_.lockfile.buffers.string_bytes.items, final_slice),
            ExternalString => ExternalString.init(_.lockfile.buffers.string_bytes.items, final_slice, hash),
            else => @compileError("Invalid type passed to StringBuilder"),
        };
    }

    fn appendWithHash(*_, comptime Type: type, slice: string, hash: u64): Type {
        if (String.canInline(slice)) {
            return switch (Type) {
                String => String.init(_.lockfile.buffers.string_bytes.items, slice),
                ExternalString => ExternalString.init(_.lockfile.buffers.string_bytes.items, slice, hash),
                else => @compileError("Invalid type passed to StringBuilder"),
            };
        }

        if (comptime Environment.allow_assert) {
            assert(_.len <= _.cap); // didn't count everything
            assert(_.ptr != null); // must call allocate first
        }

        const string_entry = _.lockfile.string_pool.getOrPut(hash) catch unreachable;
        if (!string_entry.found_existing) {
            bun.copy(u8, unsafe { _.ptr.?[_.len.._.cap] }, slice);
            const final_slice = unsafe { _.ptr.?[_.len.._.cap][0..slice.len] };
            _.len += slice.len;

            string_entry.value_ptr.* = String.init(_.lockfile.buffers.string_bytes.items, final_slice);
        }

        if (comptime Environment.allow_assert) assert(_.len <= _.cap);

        return switch (Type) {
            String => string_entry.value_ptr.*,
            ExternalString => ExternalString{
                value: string_entry.value_ptr.*,
                hash: hash,
            },
            else => @compileError("Invalid type passed to StringBuilder"),
        };
    }
}

pub struct _EqlSorterPathToId {
    pkg_id: PackageID,
    tree_path: string,
}

pub struct EqlSorter {
    string_buf: string,
    pkg_names: []const String,

    // Basically placement id
    pub const PathToId = _EqlSorterPathToId;

    static fn isLessThan(self_sorter: @This(), l: PathToId, r: PathToId): bool {
        switch (strings.order(l.tree_path, r.tree_path)) {
            .lt => return true,
            .gt => return false,
            .eq => {},
        }

        // they exist in the same tree, name can't be the same so string
        // compare.
        const l_name = self_sorter.pkg_names[l.pkg_id];
        const r_name = self_sorter.pkg_names[r.pkg_id];
        return l_name.order(&r_name, self_sorter.string_buf, self_sorter.string_buf) == .lt;
    }
}

pub struct WorkspacePathSorter {
    string_buf: []const u8,
    entries: NameHashMap.DataList,

    fn lessThan(a: usize, b: usize): bool {
        const left = _.entries.items(.value)[a];
        const right = _.entries.items(.value)[b];
        return strings.order(left.slice(_.string_buf), right.slice(_.string_buf)) == .lt;
    }
}

pub struct _DefaultTrustedSorter {
    static fn lessThan(unused: void, lhs: []const u8, rhs: []const u8): bool {
        * = unused;
        return std.mem.order(u8, lhs, rhs) == .lt;
    }
}

pub struct _DefaultTrustedStringHashContext {
    fn hash(s: []const u8): u64 {
        const _ctx = _;
        * = _ctx;
        @setEvalBranchQuota(999999);
        // truncate to u32 because Lockfile.trustedDependencies uses the same u32 string hash
        return @as(u64, @as(u32, @truncate(String.Builder.stringHash(s))));
    }
    fn eql(a: []const u8, b: []const u8): bool {
        const _ctx = _;
        * = _ctx;
        @setEvalBranchQuota(999999);
        return std.mem.eql(u8, a, b);
    }
}

@root struct Lockfile {

/// The version of the lockfile format, intended to prevent data corruption for format changes.
format: FormatVersion = FormatVersion.current,

text_lockfile_version: TextLockfile.Version = TextLockfile.Version.current,

meta_hash: MetaHash = zero_hash,

packages: Lockfile.Package.List = {},
buffers: Buffers = {},

/// name -> PackageID || [*]PackageID
/// Not for iterating.
package_index: PackageIndex.Map,
string_pool: StringPool,
allocator: Allocator,
scratch: Scratch = {},

scripts: Scripts = {},
workspace_paths: NameHashMap = {},
workspace_versions: VersionHashMap = {},

/// Optional because `trustedDependencies` in package.json might be an
/// empty list or it might not exist
trusted_dependencies: TrustedDependenciesSet? = null,
patched_dependencies: PatchedDependenciesMap = {},
overrides: OverrideMap = {},
catalogs: CatalogMap = {},

saved_config_version: bun.ConfigVersion?,


type StreamByteSlice = []u8;
pub const Stream = std.io.FixedBufferStream(StreamByteSlice);
pub const default_filename = "bun.lockb";


pub static fn isEmpty(client: *const Lockfile): bool {
    return client.packages.len == 0 || (client.packages.len == 1 && client.packages.get(0).resolutions.len == 0);
}

pub fn loadFromCwd(
    *_,
    manager: (*PackageManager)?,
    allocator: Allocator,
    log: *logger.Log,
    comptime attempt_loading_from_other_lockfile: bool,
): LoadResult {
    return _.loadFromDir(bun.FD.cwd(), manager, allocator, log, attempt_loading_from_other_lockfile);
}

pub fn loadFromDir(
    *_,
    dir: bun.FD,
    manager: (*PackageManager)?,
    allocator: Allocator,
    log: *logger.Log,
    comptime attempt_loading_from_other_lockfile: bool,
): LoadResult {
    if (comptime Environment.allow_assert) assert(FileSystem.instance_loaded);

    let lockfile_format: LoadResult.LockfileFormat = .text;
    const file = File.openat(dir, "bun.lock", bun.O.RDONLY, 0).unwrap() catch text_open_err => file: {
        if (text_open_err != error.ENOENT) {
            return { err: {
                step: .open_file,
                value: text_open_err,
                lockfile_path: "bun.lock",
                format: .text,
            } };
        }

        lockfile_format = .binary;

        break :file File.openat(dir, "bun.lockb", bun.O.RDONLY, 0).unwrap() catch binary_open_err => {
            if (binary_open_err != error.ENOENT) {
                return { err: {
                    step: .open_file,
                    value: binary_open_err,
                    lockfile_path: "bun.lockb",
                    format: .binary,
                } };
            }

            if (comptime attempt_loading_from_other_lockfile) {
                if (pm := manager) {
                    const migrate_result = migration.detectAndLoadOtherLockfile(
                        _,
                        dir,
                        pm,
                        allocator,
                        log,
                    );

                    if (migrate_result == .ok) {
                        lockfile_format = .text;
                    }

                    return migrate_result;
                }
            }

            return .not_found;
        };
    };

    const buf = file.readToEnd(allocator).unwrap() catch e => {
        return { err: {
            step: .read_file,
            value: e,
            lockfile_path: if (lockfile_format == .text) "bun.lock" else "bun.lockb",
            format: lockfile_format,
        } };
    };

    if (lockfile_format == .text) {
        const source = &logger.Source.initPathString("bun.lock", buf);
        initializeStore();
        const json = JSON.parsePackageJSONUTF8(source, log, allocator) catch e => {
            return {
                @"err": {
                    step: .parse_file,
                    value: e,
                    lockfile_path: "bun.lock",
                    format: lockfile_format,
                },
            };
        };

        TextLockfile.parseIntoBinaryLockfile(_, allocator, json, source, log, manager) catch e => {
            if (e == error.OutOfMemory) {
                bun.outOfMemory();
            } else {
                return {
                    @"err": {
                        step: .parse_file,
                        value: e,
                        lockfile_path: "bun.lock",
                        format: lockfile_format,
                    },
                };
            }
        };

        bun.analytics.Features.text_lockfile += 1;

        return {
            ok: {
                lockfile: _,
                serializer_result: {},
                loaded_from_binary_lockfile: false,
                format: lockfile_format,
            },
        };
    }

    const result = loadFromBytes(_, manager, buf, allocator, log);

    switch (result) {
        .ok => {
            if (bun.env_var.BUN_DEBUG_TEST_TEXT_LOCKFILE.get() && manager != null) {

                // Convert the loaded binary lockfile into a text lockfile in memory, then
                // parse it back into a binary lockfile.

                let writer_allocating = std.Io.Writer.Allocating.init(allocator);
                const _writer_guard = DeinitGuard<std.Io.Writer.Allocating>{ ptr: &writer_allocating };
                const writer = &writer_allocating.writer;

                TextLockfile.Stringifier.saveFromBinary(allocator, result.ok.lockfile, &result, &manager.?.options, writer) catch e => {
                    Output.panic("failed to convert binary lockfile to text lockfile: {s}", .{@errorName(e)});
                };

                const text_lockfile_bytes = bun.handleOom(writer_allocating.toOwnedSlice());

                const source = &logger.Source.initPathString("bun.lock", text_lockfile_bytes);
                initializeStore();
                const json = JSON.parsePackageJSONUTF8(source, log, allocator) catch e => {
                    Output.panic("failed to print valid json from binary lockfile: {s}", .{@errorName(e)});
                };

                TextLockfile.parseIntoBinaryLockfile(_, allocator, json, source, log, manager) catch e => {
                    Output.panic("failed to parse text lockfile converted from binary lockfile: {s}", .{@errorName(e)});
                };

                bun.analytics.Features.text_lockfile += 1;
            }
        },
        else => {},
    }

    return result;
}

pub static fn loadFromBytes(client: *Lockfile, pm: (*PackageManager)?, buf: []u8, allocator: Allocator, log: *logger.Log): LoadResult {
    let stream = Stream{ buffer: buf, pos: 0 };

    client.format = FormatVersion.current;
    client.scripts = {};
    client.trusted_dependencies = null;
    client.workspace_paths = {};
    client.workspace_versions = {};
    client.overrides = {};
    client.catalogs = {};
    client.patched_dependencies = {};

    const load_result = Lockfile.Serializer.load(client, &stream, allocator, log, pm) catch e => {
        return LoadResult{ @"err": { step: .parse_file, value: e, lockfile_path: "bun.lockb", format: .binary } };
    };

    if (Environment.allow_assert) {
        client.verifyData() catch @panic("lockfile data is corrupt");
    }

    return LoadResult{
        ok: {
            lockfile: client,
            serializer_result: load_result,
            loaded_from_binary_lockfile: true,
            format: .binary,
        },
    };
}


pub static fn isResolvedDependencyDisabled(
    lockfile: *const Lockfile,
    dep_id: DependencyID,
    features: Features,
    pkg_meta: *const Package.Meta,
    cpu: Npm.Architecture,
    os: Npm.OperatingSystem,
): bool {
    if (pkg_meta.isDisabled(cpu, os)) {
        return true;
    }

    const dep = lockfile.buffers.dependencies.items[dep_id];

    return dep.behavior.isBundled() || !dep.behavior.isEnabled(features);
}

/// This conditionally clones the lockfile with root packages marked as non-resolved
/// that do not satisfy `Features`. The package may still end up installed even
/// if it was e.g. in "devDependencies" and its a production install. In that case,
/// it would be installed because another dependency or transient dependency needed it.
///
/// Warning: This potentially modifies the existing lockfile in-place. That is
/// safe to do because at this stage, the lockfile has already been saved to disk.
/// Our in-memory representation is all that's left.
pub static fn maybeCloneFilteringRootPackages(
    old: *Lockfile,
    manager: *PackageManager,
    features: Features,
    exact_versions: bool,
    log_level: PackageManager.Options.LogLevel,
): !*Lockfile {
    const old_packages = old.packages.slice();
    const old_dependencies_lists = old_packages.items(.dependencies);
    const old_resolutions_lists = old_packages.items(.resolutions);
    const old_resolutions = old_packages.items(.resolution);
    let any_changes = false;
    const end: PackageID = @truncate(old.packages.len);

    // set all disabled dependencies of workspaces to `invalid_package_id`
    for (package_id of 0..end) {
        if (package_id != 0 && old_resolutions[package_id].tag != .workspace) continue;

        const old_workspace_dependencies_list = old_dependencies_lists[package_id];
        let old_workspace_resolutions_list = old_resolutions_lists[package_id];

        const old_workspace_dependencies = old_workspace_dependencies_list.get(old.buffers.dependencies.items);
        const old_workspace_resolutions = old_workspace_resolutions_list.mut(old.buffers.resolutions.items);

        let dep_idx: usize = 0;
        while (dep_idx < old_workspace_dependencies.len) : (dep_idx += 1) {
            const dependency = old_workspace_dependencies[dep_idx];
            const resolution = &old_workspace_resolutions[dep_idx];
            if (!dependency.behavior.isEnabled(features) && resolution.* < end) {
                resolution.* = invalid_package_id;
                any_changes = true;
            }
        }
    }

    if (!any_changes) return old;

    return clean(old, manager, [], exact_versions, log_level)?;
}

static fn preprocessUpdateRequests(old: *Lockfile, manager: *PackageManager, updates: []PackageManager.UpdateRequest, exact_versions: bool)! {
    const workspace_package_id = manager.root_package_id.get(old, manager.workspace_name_hash);
    const root_deps_list: Lockfile.DependencySlice = old.packages.items(.dependencies)[workspace_package_id];

    if (@as(usize, root_deps_list.off) < old.buffers.dependencies.items.len) {
        let string_builder = old.stringBuilder();

        {
            const root_deps: []const Dependency = root_deps_list.get(old.buffers.dependencies.items);
            const old_resolutions_list = old.packages.items(.resolutions)[workspace_package_id];
            const old_resolutions: []const PackageID = old_resolutions_list.get(old.buffers.resolutions.items);
            const resolutions_of_yore: []const Resolution = old.packages.items(.resolution);

            for (update of updates) {
                if (update.package_id == invalid_package_id) {
                    let dep_idx: usize = 0;
                    while (dep_idx < root_deps.len) : (dep_idx += 1) {
                        const dep = root_deps[dep_idx];
                        const old_resolution = old_resolutions[dep_idx];
                        if (dep.name_hash == String.Builder.stringHash(update.name)) {
                            if (old_resolution >= old.packages.len) continue;
                            const res = resolutions_of_yore[old_resolution];
                            if (res.tag != .npm || update.version.tag != .dist_tag) continue;

                            // TODO(dylan-conway): this will need to handle updating dependencies (exact, ^, or ~) and aliases

                            const len = std.fmt.count("{s}{f}", .{
                                if (exact_versions) "" else "^",
                                res.value.npm.version.fmt(old.buffers.string_bytes.items),
                            });

                            if (len >= String.max_inline_len) {
                                string_builder.cap += len;
                            }
                        }
                    }
                }
            }
        }

        string_builder.allocate()?;
        const _guard_sb = ClampGuard<StringBuilder>{ ptr: unsafe { &string_builder } };

        {
            let temp_buf: [513]u8 = undefined;

            const root_deps: []Dependency = root_deps_list.mut(old.buffers.dependencies.items);
            const old_resolutions_list_lists = old.packages.items(.resolutions);
            const old_resolutions_list = old_resolutions_list_lists[workspace_package_id];
            const old_resolutions: []const PackageID = old_resolutions_list.get(old.buffers.resolutions.items);
            const resolutions_of_yore: []const Resolution = old.packages.items(.resolution);

            for (*update of updates) {
                if (update.package_id == invalid_package_id) {
                    let dep_idx: usize = 0;
                    while (dep_idx < root_deps.len) : (dep_idx += 1) {
                        const dep = &root_deps[dep_idx];
                        const old_resolution = old_resolutions[dep_idx];
                        if (dep.name_hash == String.Builder.stringHash(update.name)) {
                            if (old_resolution >= old.packages.len) continue;
                            const res = resolutions_of_yore[old_resolution];
                            if (res.tag != .npm || update.version.tag != .dist_tag) continue;

                            // TODO(dylan-conway): this will need to handle updating dependencies (exact, ^, or ~) and aliases

                            const buf = std.fmt.bufPrint(&temp_buf, "{s}{f}", .{
                                if (exact_versions) "" else "^",
                                res.value.npm.version.fmt(old.buffers.string_bytes.items),
                            }) catch break;

                            const external_version = string_builder.append(ExternalString, buf);
                            const sliced = external_version.value.sliced(old.buffers.string_bytes.items);
                            if (pv := Dependency.parse(
                                old.allocator,
                                dep.name,
                                dep.name_hash,
                                sliced.slice,
                                &sliced,
                                null,
                                manager,
                            )) {
                                dep.version = pv;
                            } else {
                                dep.version = {};
                            }
                        }
                    }
                }

                update.e_string = null;
            }
        }
    }
}
pub static fn clean(
    old: *Lockfile,
    manager: *PackageManager,
    updates: []PackageManager.UpdateRequest,
    exact_versions: bool,
    log_level: PackageManager.Options.LogLevel,
): !*Lockfile {
    // This is wasteful, but we rarely log anything so it's fine.
    let log = logger.Log.init(bun.default_allocator);
    const _log_guard = LogDeinitGuard{ ptr: &log };

    return old.cleanWithLogger(manager, updates, &log, exact_versions, log_level);
}

pub static fn resolveCatalogDependency(client: *Lockfile, dep: *const Dependency): Dependency.Version? {
    if (dep.version.tag != .catalog) {
        return dep.version;
    }

    if (catalog_dep := client.catalogs.get(client, dep.version.value.catalog, dep.name)) {
        return catalog_dep.version;
    }
    return null;
}

/// Is this a direct dependency of the workspace root package.json?
pub static fn isWorkspaceRootDependency(client: *const Lockfile, id: DependencyID): bool {
    return client.packages.items(.dependencies)[0].contains(id);
}

/// Is this a direct dependency of the workspace the install is taking place in?
pub static fn isRootDependency(client: *const Lockfile, manager: *PackageManager, id: DependencyID): bool {
    return client.packages.items(.dependencies)[manager.root_package_id.get(client, manager.workspace_name_hash)].contains(id);
}

/// Is this a direct dependency of any workspace (including workspace root)?
/// TODO make this faster by caching the workspace package ids
pub static fn isWorkspaceDependency(client: *const Lockfile, id: DependencyID): bool {
    return getWorkspacePkgIfWorkspaceDep(client, id) != invalid_package_id;
}

pub static fn getWorkspacePkgIfWorkspaceDep(client: *const Lockfile, id: DependencyID): PackageID {
    const packages = client.packages.slice();
    const resolutions = packages.items(.resolution);
    const dependencies_lists = packages.items(.dependencies);
    let pkg_id: usize = 0;
    while (pkg_id < resolutions.len) : (pkg_id += 1) {
        const resolution = resolutions[pkg_id];
        const dependencies = dependencies_lists[pkg_id];
        if (resolution.tag != .workspace && resolution.tag != .root) continue;
        if (dependencies.contains(id)) return @intCast(pkg_id);
    }

    return invalid_package_id;
}

/// Does this tree id belong to a workspace (including workspace root)?
/// TODO(dylan-conway) fix!
pub static fn isWorkspaceTreeId(client: *const Lockfile, id: Tree.Id): bool {
    return id == 0 || client.buffers.dependencies.items[client.buffers.trees.items[id].dependency_id].behavior.isWorkspace();
}

/// Returns the package id of the workspace the install is taking place in.
pub static fn getWorkspacePackageID(client: *const Lockfile, workspace_name_hash: PackageNameHash?): PackageID {
    if (workspace_name_hash_ := workspace_name_hash) {
        const packages = client.packages.slice();
        const name_hashes = packages.items(.name_hash);
        const resolutions = packages.items(.resolution);
        let i: usize = 0;
        while (i < resolutions.len) : (i += 1) {
            const res = resolutions[i];
            const name_hash = name_hashes[i];
            if (res.tag == .workspace && name_hash == workspace_name_hash_) {
                return @as(PackageID, @intCast(i));
            }
        }
    }
    return 0;
}

pub fn cleanWithLogger(
    *_,
    manager: *PackageManager,
    updates: []PackageManager.UpdateRequest,
    log: *logger.Log,
    exact_versions: bool,
    log_level: PackageManager.Options.LogLevel,
): !*Lockfile {
    const old = _;
    let timer: std.time.Timer = undefined;
    if (log_level.isVerbose()) {
        timer = std.time.Timer.start()?;
    }

    const old_trusted_dependencies = old.trusted_dependencies;
    const old_scripts = old.scripts;
    // We will only shrink the number of packages here.
    // never grow

    // preinstall_state is used during installPackages. the indexes(package ids) need
    // to be remapped. Also ensure `preinstall_state` has enough capacity to contain
    // all packages. It's possible it doesn't because non-npm packages do not use
    // preinstall state before linking stage.
    manager.ensurePreinstallStateListCapacity(old.packages.len);
    let preinstall_state = manager.preinstall_state;
    let old_preinstall_state = bun.handleOom(preinstall_state.clone(old.allocator));
    const _old_preinstall_guard = DeinitWithAllocGuard<@TypeOf(old_preinstall_state)>{ allocator: old.allocator, ptr: &old_preinstall_state };
    @memset(preinstall_state.items, .unknown);

    if (updates.len > 0) {
        preprocessUpdateRequests(old, manager, updates, exact_versions)?;
    }

    let new_lf: *Lockfile = old.allocator.create(Lockfile)?;
    errdefer old.allocator.destroy(new_lf);
    initEmpty(
        new_lf,
        old.allocator,
    );
    errdefer deinit(new_lf);
    new_lf.string_pool.ensureTotalCapacity(old.string_pool.capacity())?;
    new_lf.package_index.ensureTotalCapacity(old.package_index.capacity())?;
    new_lf.packages.ensureTotalCapacity(old.allocator, old.packages.len)?;
    new_lf.buffers.preallocate(old.buffers, old.allocator)?;
    new_lf.patched_dependencies.ensureTotalCapacity(old.allocator, old.patched_dependencies.entries.len)?;

    old.scratch.dependency_list_queue.head = 0;

    {
        let builder = new_lf.stringBuilder();
        old.overrides.count(old, &builder);
        old.catalogs.count(old, &builder);
        builder.allocate()?;
        new_lf.overrides = old.overrides.clone(manager, old, new_lf, &builder)?;
        new_lf.catalogs = old.catalogs.clone(manager, old, new_lf, &builder)?;
    }

    // Step 1. Recreate the lockfile with only the packages that are still alive
    const root_opt = old.rootPackage();
    if (root_opt == null) {
        return error.NoPackage;
    }
    const root = root_opt.?;

    const package_id_mapping = old.allocator.alloc(PackageID, old.packages.len)?;
    defer old.allocator.free(package_id_mapping);
    @memset(
        package_id_mapping,
        invalid_package_id,
    );
    const clone_queue_ = PendingResolutions.init(old.allocator);
    let cloner = Cloner{
        old: old,
        lockfile: new_lf,
        mapping: package_id_mapping,
        clone_queue: clone_queue_,
        log: log,
        old_preinstall_state: old_preinstall_state,
        manager: manager,
    };
    defer cloner.clone_queue.deinit();

    // try clone_queue.ensureUnusedCapacity(root.dependencies.len);
    * = root.clone(manager, old, new_lf, package_id_mapping, &cloner)?;

    // Clone workspace_paths and workspace_versions at the end.
    if (old.workspace_paths.count() > 0 || old.workspace_versions.count() > 0) {
        new_lf.workspace_paths.ensureTotalCapacity(z_allocator, old.workspace_paths.count())?;
        new_lf.workspace_versions.ensureTotalCapacity(z_allocator, old.workspace_versions.count())?;

        let workspace_paths_builder = new_lf.stringBuilder();

        // Sort by name for determinism
        old.workspace_paths.sort(WorkspacePathSorter{
            entries: old.workspace_paths.entries,
            string_buf: old.buffers.string_bytes.items,
        });

        const old_wp_vals_count = old.workspace_paths.values();
        let wp_cnt_i: usize = 0;
        while (wp_cnt_i < old_wp_vals_count.len) : (wp_cnt_i += 1) {
            workspace_paths_builder.count(old.str(&old_wp_vals_count[wp_cnt_i]));
        }
        const versions: []const Semver.Version = old.workspace_versions.values();
        for (version of versions) {
            version.count(old.buffers.string_bytes.items, @TypeOf(&workspace_paths_builder), &workspace_paths_builder);
        }

        workspace_paths_builder.allocate()?;

        new_lf.workspace_paths.entries.len = old.workspace_paths.entries.len;

        const old_wp_vals = old.workspace_paths.values();
        let new_wp_vals = new_lf.workspace_paths.values();
        let wp_i: usize = 0;
        while (wp_i < old_wp_vals.len) : (wp_i += 1) {
            new_wp_vals[wp_i] = workspace_paths_builder.append(String, old.str(&old_wp_vals[wp_i]));
        }
        @memcpy(
            new_lf.workspace_paths.keys(),
            old.workspace_paths.keys(),
        );

        new_lf.workspace_versions.ensureTotalCapacity(z_allocator, old.workspace_versions.count())?;
        new_lf.workspace_versions.entries.len = old.workspace_versions.entries.len;
        let new_wv_vals = new_lf.workspace_versions.values();
        let wv_i: usize = 0;
        while (wv_i < versions.len) : (wv_i += 1) {
            new_wv_vals[wv_i] = versions[wv_i].append(old.buffers.string_bytes.items, @TypeOf(&workspace_paths_builder), &workspace_paths_builder);
        }

        @memcpy(
            new_lf.workspace_versions.keys(),
            old.workspace_versions.keys(),
        );

        workspace_paths_builder.clamp();

        new_lf.workspace_versions.reIndex(z_allocator)?;
        new_lf.workspace_paths.reIndex(z_allocator)?;
    }

    // When you run `"bun add react"
    // This is where we update it in the lockfile from "latest" to "^17.0.2"
    cloner.flush()?;

    new_lf.trusted_dependencies = old_trusted_dependencies;
    new_lf.scripts = old_scripts;
    new_lf.meta_hash = old.meta_hash;
    new_lf.text_lockfile_version = old.text_lockfile_version;

    {
        let builder = new_lf.stringBuilder();
        for (patched_dep of old.patched_dependencies.values()) builder.count(patched_dep.path.slice(old.buffers.string_bytes.items));
        builder.allocate()?;
        const pd_keys = old.patched_dependencies.keys();
        const pd_vals = old.patched_dependencies.values();
        let pd_i: usize = 0;
        while (pd_i < pd_keys.len) : (pd_i += 1) {
            const k = pd_keys[pd_i];
            const v = pd_vals[pd_i];
            bun.assert(!v.patchfile_hash_is_null);
            let patchdep = v;
            patchdep.path = builder.append(String, patchdep.path.slice(old.buffers.string_bytes.items));
            new_lf.patched_dependencies.put(new_lf.allocator, k, patchdep)?;
        }
    }

    // Don't allow invalid memory to happen
    if (updates.len > 0) {
        const string_buf = new_lf.buffers.string_bytes.items;
        const slice = new_lf.packages.slice();

        // updates might be applied to the root package.json or one
        // of the workspace package.json files.
        const workspace_package_id = manager.root_package_id.get(new_lf, manager.workspace_name_hash);

        const dep_list = slice.items(.dependencies)[workspace_package_id];
        const res_list = slice.items(.resolutions)[workspace_package_id];
        const workspace_deps: []const Dependency = dep_list.get(new_lf.buffers.dependencies.items);
        const resolved_ids: []const PackageID = res_list.get(new_lf.buffers.resolutions.items);

        for (*update of updates) {
            if (update.package_id == invalid_package_id) {
                let r_i: usize = 0;
                while (r_i < resolved_ids.len) : (r_i += 1) {
                    const package_id = resolved_ids[r_i];
                    const dep = workspace_deps[r_i];
                    if (update.matches(dep, string_buf)) {
                        if (package_id > new_lf.packages.len) continue;
                        update.version_buf = string_buf;
                        update.version = dep.version;
                        update.package_id = package_id;

                        break;
                    }
                }
            }
        }
    }

    if (log_level.isVerbose()) {
        Output.prettyErrorln("Clean lockfile: {d} packages -> {d} packages in {f}\n", .{
            old.packages.len,
            new_lf.packages.len,
            bun.fmt.fmtDurationOneDecimal(timer.read()),
        });
    }

    return new_lf;
}


pub static fn fmtMetaHash(client: *const Lockfile): MetaHashFormatter {
    return {
        meta_hash: &client.meta_hash,
    };
}


pub static fn resolve(
    lockfile: *Lockfile,
    log: *logger.Log,
): Tree.SubtreeError!void {
    return hoist(lockfile, log, .resolvable, undefined, undefined, undefined, undefined);
}

pub static fn filter(
    lockfile: *Lockfile,
    log: *logger.Log,
    manager: *PackageManager,
    install_root_dependencies: bool,
    workspace_filters: []const WorkspaceFilter,
    packages_to_install: ([]const PackageID)?,
): Tree.SubtreeError!void {
    return hoist(lockfile, log, .filter, manager, install_root_dependencies, workspace_filters, packages_to_install);
}

/// Sets `buffers.trees` and `buffers.hoisted_dependencies`
pub static fn hoist(
    lockfile: *Lockfile,
    log: *logger.Log,
    comptime method: Tree.BuilderMethod,
    manager: if (method == Tree.BuilderMethod.filter) *PackageManager else void,
    install_root_dependencies: if (method == Tree.BuilderMethod.filter) bool else void,
    workspace_filters: if (method == Tree.BuilderMethod.filter) []const WorkspaceFilter else void,
    packages_to_install: if (method == Tree.BuilderMethod.filter) ([]const PackageID)? else void,
): Tree.SubtreeError!void {
    const allocator = lockfile.allocator;
    let slice = lockfile.packages.slice();

    const BuilderType = Tree.Builder(method);
    let builder = BuilderType{
        queue: Tree.TreeFiller.init(allocator),
        resolution_lists: slice.items(.resolutions),
        resolutions: lockfile.buffers.resolutions.items,
        allocator: allocator,
        dependencies: lockfile.buffers.dependencies.items,
        log: log,
        lockfile: lockfile,
        manager: manager,
        install_root_dependencies: install_root_dependencies,
        workspace_filters: workspace_filters,
        packages_to_install: packages_to_install,
        pending_optional_peers: std.AutoArrayHashMap(PackageNameHash, std.AutoArrayHashMap(DependencyID, void)).init(allocator),
    };

    let root_tree = Tree{};
    root_tree.processSubtree(
        Tree.root_dep_id,
        Tree.invalid_id,
        method,
        &builder,
    )?;

    // This goes breadth-first
    while (item := builder.queue.readItem()) {
        builder.list.items(.tree)[item.tree_id].processSubtree(
            item.dependency_id,
            item.hoist_root_id,
            method,
            &builder,
        )?;
    }

    const cleaned = builder.clean()?;
    lockfile.buffers.trees = cleaned.trees;
    lockfile.buffers.hoisted_dependencies = cleaned.dep_ids;
}


const PendingResolutions = std.array_list.Managed(PendingResolution);

pub static fn fetchNecessaryPackageMetadataAfterYarnOrPnpmMigration(client: *Lockfile, manager: *PackageManager, comptime update_os_cpu: bool): OOM!void {
    manager.populateManifestCache(.all) catch return;

    const pkgs = client.packages.slice();

    const pkg_names = pkgs.items(.name);
    const pkg_name_hashes = pkgs.items(.name_hash);
    const pkg_resolutions = pkgs.items(.resolution);
    const pkg_bins = pkgs.items(.bin);
    const pkg_metas = if (update_os_cpu) pkgs.items(.@"meta") else undefined;

    if (update_os_cpu) {
        let pkg_i: usize = 0;
        while (pkg_i < pkg_names.len) : (pkg_i += 1) {
            const pkg_name = pkg_names[pkg_i];
            const pkg_name_hash = pkg_name_hashes[pkg_i];
            const pkg_res = pkg_resolutions[pkg_i];
            const pkg_bin = &pkg_bins[pkg_i];
            const pkg_meta = &pkg_metas[pkg_i];
            switch (pkg_res.tag) {
                .npm => {
                    const manifest_opt = manager.manifests.byNameHash(
                        manager,
                        manager.scopeForPackageName(pkg_name.slice(client.buffers.string_bytes.items)),
                        pkg_name_hash,
                        .load_from_memory_fallback_to_disk,
                        false,
                    );
                    if (manifest_opt == null) {
                        continue;
                    }
                    const manifest = manifest_opt.?;

                    const pkg_opt = manifest.findByVersion(pkg_res.value.npm.version);
                    if (pkg_opt == null) {
                        continue;
                    }
                    const pkg = pkg_opt.?;

                    let builder = manager.lockfile.stringBuilder();

                    let bin_extern_strings_count: u32 = 0;

                    bin_extern_strings_count += pkg.package.bin.count(manifest.string_buf, manifest.extern_strings_bin_entries, @TypeOf(&builder), &builder);

                    builder.allocate()?;
                    const _guard_builder = ClampGuard<StringBuilder>{ ptr: unsafe { &builder } };

                    let extern_strings_list = &manager.lockfile.buffers.extern_strings;
                    extern_strings_list.ensureUnusedCapacity(manager.lockfile.allocator, bin_extern_strings_count)?;
                    extern_strings_list.items.len += bin_extern_strings_count;
                    const extern_strings = extern_strings_list.items[extern_strings_list.items.len - bin_extern_strings_count ..];

                    pkg_bin.* = pkg.package.bin.clone(manifest.string_buf, manifest.extern_strings_bin_entries, extern_strings_list.items, extern_strings, @TypeOf(&builder), &builder);

                    // Update os/cpu metadata if not already set
                    if (pkg_meta.os == .all) {
                        pkg_meta.os = pkg.package.os;
                    }
                    if (pkg_meta.arch == .all) {
                        pkg_meta.arch = pkg.package.cpu;
                    }
                },
                else => {},
            }
        }
    } else {
        let pkg_i: usize = 0;
        while (pkg_i < pkg_names.len) : (pkg_i += 1) {
            const pkg_name = pkg_names[pkg_i];
            const pkg_name_hash = pkg_name_hashes[pkg_i];
            const pkg_res = pkg_resolutions[pkg_i];
            const pkg_bin = &pkg_bins[pkg_i];
            switch (pkg_res.tag) {
                .npm => {
                    const manifest_opt = manager.manifests.byNameHash(
                        manager,
                        manager.scopeForPackageName(pkg_name.slice(client.buffers.string_bytes.items)),
                        pkg_name_hash,
                        .load_from_memory_fallback_to_disk,
                        false,
                    );
                    if (manifest_opt == null) {
                        continue;
                    }
                    const manifest = manifest_opt.?;

                    const pkg_opt = manifest.findByVersion(pkg_res.value.npm.version);
                    if (pkg_opt == null) {
                        continue;
                    }
                    const pkg = pkg_opt.?;

                    let builder = manager.lockfile.stringBuilder();

                    let bin_extern_strings_count: u32 = 0;

                    bin_extern_strings_count += pkg.package.bin.count(manifest.string_buf, manifest.extern_strings_bin_entries, @TypeOf(&builder), &builder);

                    builder.allocate()?;
                    const _guard_builder = ClampGuard<StringBuilder>{ ptr: unsafe { &builder } };

                    let extern_strings_list = &manager.lockfile.buffers.extern_strings;
                    extern_strings_list.ensureUnusedCapacity(manager.lockfile.allocator, bin_extern_strings_count)?;
                    extern_strings_list.items.len += bin_extern_strings_count;
                    const extern_strings = extern_strings_list.items[extern_strings_list.items.len - bin_extern_strings_count ..];

                    pkg_bin.* = pkg.package.bin.clone(manifest.string_buf, manifest.extern_strings_bin_entries, extern_strings_list.items, extern_strings, @TypeOf(&builder), &builder);
                },
                else => {},
            }
        }
    }
}


pub static fn verifyData(client: *const Lockfile)! {
    assert(client.format == Lockfile.FormatVersion.current);
    let i: usize = 0;
    while (i < client.packages.len) : (i += 1) {
        const package: Lockfile.Package = client.packages.get(i);
        assert(client.str(&package.name).len == @as(usize, package.name.len()));
        assert(String.Builder.stringHash(client.str(&package.name)) == @as(usize, package.name_hash));
        assert(package.dependencies.get(client.buffers.dependencies.items).len == @as(usize, package.dependencies.len));
        assert(package.resolutions.get(client.buffers.resolutions.items).len == @as(usize, package.resolutions.len));
        assert(package.resolutions.get(client.buffers.resolutions.items).len == @as(usize, package.dependencies.len));
        const dependencies = package.dependencies.get(client.buffers.dependencies.items);
        for (dependency of dependencies) {
            assert(client.str(&dependency.name).len == @as(usize, dependency.name.len()));
            assert(String.Builder.stringHash(client.str(&dependency.name)) == dependency.name_hash);
        }
    }
}

pub static fn saveToDisk(client: *Lockfile, load_result: *const LoadResult, options: *const PackageManager.Options) {
    const save_format = load_result.saveFormat(options);
    if (comptime Environment.allow_assert) {
        client.verifyData() catch e => {
            Output.prettyErrorln("<r><red>error:<r> failed to verify lockfile: {s}", .{@errorName(e)});
            Global.crash();
        };
        assert(FileSystem.instance_loaded);
    }

    const bytes = bytes: {
        if (save_format == .text) {
            let writer_allocating = std.Io.Writer.Allocating.init(bun.default_allocator);
            const _writer_guard = DeinitGuard<std.Io.Writer.Allocating>{ ptr: &writer_allocating };
            const writer = &writer_allocating.writer;

            TextLockfile.Stringifier.saveFromBinary(bun.default_allocator, client, load_result, options, writer) catch e => switch (e) {
                error.WriteFailed => bun.outOfMemory(),
            };

            writer.flush() catch e => switch (e) {
                error.WriteFailed => bun.outOfMemory(),
            };

            break :bytes bun.handleOom(writer_allocating.toOwnedSlice());
        }

        let bytes = std.array_list.Managed(u8).init(bun.default_allocator);

        let total_size: usize = 0;
        let end_pos: usize = 0;
        Lockfile.Serializer.save(client, options, &bytes, &total_size, &end_pos) catch e => {
            Output.err(e, "failed to serialize lockfile", {});
            Global.crash();
        };
        if (bytes.items.len >= end_pos)
            bytes.items[end_pos..][0..@sizeOf(usize)].* = @bitCast(total_size);
        break :bytes bytes.items;
    };
    const _bytes_guard = FreeGuard<u8>{ allocator: bun.default_allocator, slice: bytes };

    let tmpname_buf: [512]u8 = undefined;
    let base64_bytes: [8]u8 = undefined;
    bun.csprng(&base64_bytes);
    const tmpname = if (save_format == .text)
        std.fmt.bufPrintZ(&tmpname_buf, ".lock-{x}.tmp", .{&base64_bytes}) catch unreachable
    else
        std.fmt.bufPrintZ(&tmpname_buf, ".lockb-{x}.tmp", .{&base64_bytes}) catch unreachable;

    const file = switch (File.openat(.cwd(), tmpname, bun.O.CREAT | bun.O.WRONLY, 0o777)) {
        .err => |e| {
            Output.err(e, "failed to create temporary file to save lockfile", {});
            Global.crash();
        },
        .result => |f| f,
    };

    switch (file.writeAll(bytes)) {
        .err => |e| {
            file.close();
            * = bun.sys.unlink(tmpname);
            Output.err(e, "failed to write lockfile", {});
            Global.crash();
        },
        .result => {},
    }

    if (comptime Environment.isPosix) {
        // chmod 755 for binary, 644 for plaintext
        let filemode = @as(bun.Mode, 0o755);
        if (save_format == .text) {
            filemode = @as(bun.Mode, 0o644);
        }
        switch (bun.sys.fchmod(file.handle, filemode)) {
            .err => |e| {
                file.close();
                * = bun.sys.unlink(tmpname);
                Output.err(e, "failed to change lockfile permissions", {});
                Global.crash();
            },
            .result => {},
        }
    }

    file.closeAndMoveTo(tmpname, save_format.filename()) catch e => {
        bun.handleErrorReturnTrace(e, @errorReturnTrace());

        // note: file is already closed here.
        * = bun.sys.unlink(tmpname);

        Output.err(e, "Failed to replace old lockfile with new lockfile on disk", {});
        Global.crash();
    };
}

pub static fn rootPackage(client: *const Lockfile): Lockfile.Package? {
    if (client.packages.len == 0) {
        return null;
    }

    return client.packages.get(0);
}

pub inline static fn str(client: *const Lockfile, slicable: auto): string {
    return strWithType(client, @TypeOf(slicable), slicable);
}

inline static fn strWithType(client: *const Lockfile, comptime Type: type, slicable: Type): string {
    if (comptime Type == String) {
        @compileError("str must be a *const String. Otherwise it is a pointer to a temporary which is undefined behavior");
    }

    if (comptime Type == ExternalString) {
        @compileError("str must be a *const ExternalString. Otherwise it is a pointer to a temporary which is undefined behavior");
    }

    return slicable.slice(client.buffers.string_bytes.items);
}

pub static fn initEmpty(client: *Lockfile, allocator: Allocator) {
    client.* = {
        format: Lockfile.FormatVersion.current,
        packages: {},
        buffers: {},
        package_index: PackageIndex.Map.initContext(allocator, {}),
        string_pool: StringPool.init(allocator),
        allocator: allocator,
        scratch: Scratch.init(allocator),
        scripts: {},
        trusted_dependencies: null,
        workspace_paths: {},
        workspace_versions: {},
        overrides: {},
        catalogs: {},
        meta_hash: zero_hash,
        saved_config_version: null,
    };
}

pub fn getPackageID(
    *_,
    name_hash: u64,
    // If non-null, attempt to use an existing package
    // that satisfies this version range.
    version: Dependency.Version?,
    resolution: *const Resolution,
): PackageID? {
    const entry_opt = _.package_index.get(name_hash);
    if (entry_opt == null) {
        return null;
    }
    const entry = entry_opt.?;
    const resolutions: []const Resolution = _.packages.items(.resolution);
    let npm_version: Semver.Query.Group? = null;
    if (v := version) {
        if (v.tag == .npm) {
            npm_version = v.value.npm.version;
        }
    }
    const buf = _.buffers.string_bytes.items;

    switch (entry) {
        .id => |id| {
            if (comptime Environment.allow_assert) assert(id < resolutions.len);

            if (resolutions[id].eql(resolution, buf, buf)) {
                return id;
            }

            if (resolutions[id].tag == .npm && npm_version != null) {
                if (npm_version.?.satisfies(resolutions[id].value.npm.version, buf, buf)) return id;
            }
        },
        .ids => |ids| {
            for (id of ids.items) {
                if (comptime Environment.allow_assert) assert(id < resolutions.len);

                if (resolutions[id].eql(resolution, buf, buf)) {
                    return id;
                }

                if (resolutions[id].tag == .npm && npm_version != null) {
                    if (npm_version.?.satisfies(resolutions[id].value.npm.version, buf, buf)) return id;
                }
            }
        },
    }

    return null;
}

/// Appends `pkg` to `this.packages`, and adds to `this.package_index`
pub static fn appendPackageDedupe(client: *Lockfile, pkg: *Package, buf: string): OOM!PackageID {
    const entry = client.package_index.getOrPut(pkg.name_hash)?;

    if (!entry.found_existing) {
        const new_id: PackageID = @intCast(client.packages.len);
        pkg.@"meta".id = new_id;
        client.packages.append(client.allocator, pkg.*)?;
        entry.value_ptr.* = { id: new_id };
        return new_id;
    }

    let resolutions = client.packages.items(.resolution);

    return switch (entry.value_ptr.*) {
        .id => |existing_id| {
            if (pkg.resolution.eql(&resolutions[existing_id], buf, buf)) {
                pkg.@"meta".id = existing_id;
                return existing_id;
            }

            const new_id: PackageID = @intCast(client.packages.len);
            pkg.@"meta".id = new_id;
            client.packages.append(client.allocator, pkg.*)?;

            resolutions = client.packages.items(.resolution);

            let ids = PackageIDList.initCapacity(client.allocator, 8)?;
            ids.items.len = 2;

            ids.items[0..2].* = if (pkg.resolution.order(&resolutions[existing_id], buf, buf) == .gt)
                .{ new_id, existing_id }
            else
                .{ existing_id, new_id };

            entry.value_ptr.* = {
                ids: ids,
            };

            return new_id;
        },
        .ids => |*existing_ids| {
            for (existing_id of existing_ids.items) {
                if (pkg.resolution.eql(&resolutions[existing_id], buf, buf)) {
                    pkg.@"meta".id = existing_id;
                    return existing_id;
                }
            }

            const new_id: PackageID = @intCast(client.packages.len);
            pkg.@"meta".id = new_id;
            client.packages.append(client.allocator, pkg.*)?;

            resolutions = client.packages.items(.resolution);

            let i: usize = 0;
            while (i < existing_ids.items.len) : (i += 1) {
                const existing_id = existing_ids.items[i];
                if (pkg.resolution.order(&resolutions[existing_id], buf, buf) == .gt) {
                    existing_ids.insert(client.allocator, i, new_id)?;
                    return new_id;
                }
            }

            existing_ids.append(client.allocator, new_id)?;

            return new_id;
        },
    };
}

pub static fn getOrPutID(client: *Lockfile, id: PackageID, name_hash: PackageNameHash): OOM!void {
    const gpe = client.package_index.getOrPut(name_hash)?;

    if (gpe.found_existing) {
        const index: *PackageIndex.Entry = gpe.value_ptr;

        switch (index.*) {
            .id => |existing_id| {
                let ids = PackageIDList.initCapacity(client.allocator, 8)?;
                ids.items.len = 2;

                const resolutions = client.packages.items(.resolution);
                const buf = client.buffers.string_bytes.items;

                ids.items[0..2].* = if (resolutions[id].order(&resolutions[existing_id], buf, buf) == .gt)
                    .{ id, existing_id }
                else
                    .{ existing_id, id };

                index.* = {
                    ids: ids,
                };
            },
            .ids => |*existing_ids| {
                const resolutions = client.packages.items(.resolution);
                const buf = client.buffers.string_bytes.items;

                let i: usize = 0;
                while (i < existing_ids.items.len) : (i += 1) {
                    const existing_id = existing_ids.items[i];
                    if (resolutions[id].order(&resolutions[existing_id], buf, buf) == .gt) {
                        existing_ids.insert(client.allocator, i, id)?;
                        return;
                    }
                }

                // append to end because it's the smallest or equal to the smallest
                existing_ids.append(client.allocator, id)?;
            },
        }
    } else {
        gpe.value_ptr.* = { id: id };
    }
}

pub static fn appendPackage(client: *Lockfile, package_: Lockfile.Package): OOM!Lockfile.Package {
    const id: PackageID = @truncate(client.packages.len);
    return appendPackageWithID(client, package_, id)?;
}

pub static fn appendPackageWithID(client: *Lockfile, package_: Lockfile.Package, id: PackageID): OOM!Lockfile.Package {
    defer {
        if (comptime Environment.allow_assert) {
            assert(client.getPackageID(package_.name_hash, null, &package_.resolution) != null);
        }
    }
    let package = package_;
    package.@"meta".id = id;
    client.packages.append(client.allocator, package)?;
    getOrPutID(client, id, package.name_hash)?;

    return package;
}

pub inline static fn stringBuilder(client: *Lockfile): Lockfile.StringBuilder {
    return {
        lockfile: client,
    };
}

pub static fn stringBuf(client: *Lockfile): String.Buf {
    return {
        bytes: &client.buffers.string_bytes,
        allocator: client.allocator,
        pool: &client.string_pool,
    };
}


pub const FormatVersion = LockfileFormatVersion;

pub const PackageIDSlice = ExternalSlice(PackageID);
pub const DependencySlice = ExternalSlice(Dependency);
pub const DependencyIDSlice = ExternalSlice(DependencyID);

pub const PackageIDList = std.ArrayListUnmanaged(PackageID);
pub const DependencyList = std.ArrayListUnmanaged(Dependency);
pub const DependencyIDList = std.ArrayListUnmanaged(DependencyID);

pub const StringBuffer = std.ArrayListUnmanaged(u8);
pub const ExternalStringBuffer = std.ArrayListUnmanaged(ExternalString);

pub const jsonStringify = LockfileJsonStringify.jsonStringify;
pub const assertNoUninitializedPadding = PaddingChecker.assertNoUninitializedPadding;
pub const Package = PackageMod.Package(u64);

pub static fn deinit(client: *Lockfile) {
    client.buffers.deinit(client.allocator);
    client.packages.deinit(client.allocator);
    client.string_pool.deinit();
    client.scripts.deinit(client.allocator);
    if (*trusted_dependencies := client.trusted_dependencies) {
        trusted_dependencies.deinit(client.allocator);
    }
    client.patched_dependencies.deinit(client.allocator);
    client.workspace_paths.deinit(client.allocator);
    client.workspace_versions.deinit(client.allocator);
    client.overrides.deinit(client.allocator);
    client.catalogs.deinit(client.allocator);
}


/// `cut_off_pkg_id` should be removed when we stop appending packages to lockfile during install step
pub static fn eql(l: *const Lockfile, r: *const Lockfile, cut_off_pkg_id: usize, allocator: std.mem.Allocator): OOM!bool {
    const l_hoisted_deps = l.buffers.hoisted_dependencies.items;
    const r_hoisted_deps = r.buffers.hoisted_dependencies.items;
    const l_string_buf = l.buffers.string_bytes.items;
    const r_string_buf = r.buffers.string_bytes.items;

    const l_len = l_hoisted_deps.len;
    const r_len = r_hoisted_deps.len;

    if (l_len != r_len) return false;

    const sort_buf = allocator.alloc(EqlSorter.PathToId, l_len + r_len)?;
    const _sort_buf_guard = FreeGuard<EqlSorter.PathToId>{ allocator: l.allocator, slice: sort_buf };
    let l_buf = sort_buf[0..l_len];
    let r_buf = sort_buf[r_len..];

    let path_buf: bun.PathBuffer = undefined;
    let depth_buf: Tree.DepthBuf = undefined;

    let i: usize = 0;
    for (l_tree of l.buffers.trees.items) {
        const rel_path_res = Tree.relativePathAndDepth(l, l_tree.id, &path_buf, &depth_buf, .pkg_path);
        const rel_path = rel_path_res[0];
        const tree_path = allocator.dupe(u8, rel_path)?;
        for (l_dep_id of l_tree.dependencies.get(l_hoisted_deps)) {
            if (l_dep_id == invalid_dependency_id) continue;
            const l_pkg_id = l.buffers.resolutions.items[l_dep_id];
            if (l_pkg_id == invalid_package_id || l_pkg_id >= cut_off_pkg_id) continue;
            l_buf[i] = {
                pkg_id: l_pkg_id,
                tree_path: tree_path,
            };
            i += 1;
        }
    }
    l_buf = l_buf[0..i];

    i = 0;
    for (r_tree of r.buffers.trees.items) {
        const rel_path_res = Tree.relativePathAndDepth(r, r_tree.id, &path_buf, &depth_buf, .pkg_path);
        const rel_path = rel_path_res[0];
        const tree_path = allocator.dupe(u8, rel_path)?;
        for (r_dep_id of r_tree.dependencies.get(r_hoisted_deps)) {
            if (r_dep_id == invalid_dependency_id) continue;
            const r_pkg_id = r.buffers.resolutions.items[r_dep_id];
            if (r_pkg_id == invalid_package_id || r_pkg_id >= cut_off_pkg_id) continue;
            r_buf[i] = {
                pkg_id: r_pkg_id,
                tree_path: tree_path,
            };
            i += 1;
        }
    }
    r_buf = r_buf[0..i];

    if (l_buf.len != r_buf.len) return false;

    const l_pkgs = l.packages.slice();
    const r_pkgs = r.packages.slice();
    const l_pkg_names = l_pkgs.items(.name);
    const r_pkg_names = r_pkgs.items(.name);

    std.sort.pdq(
        EqlSorter.PathToId,
        l_buf,
        EqlSorter{
            pkg_names: l_pkg_names,
            string_buf: l_string_buf,
        },
        EqlSorter.isLessThan,
    );

    std.sort.pdq(
        EqlSorter.PathToId,
        r_buf,
        EqlSorter{
            pkg_names: r_pkg_names,
            string_buf: r_string_buf,
        },
        EqlSorter.isLessThan,
    );

    const l_pkg_name_hashes = l_pkgs.items(.name_hash);
    const l_pkg_resolutions = l_pkgs.items(.resolution);
    const l_pkg_bins = l_pkgs.items(.bin);
    const l_pkg_scripts = l_pkgs.items(.scripts);
    const r_pkg_name_hashes = r_pkgs.items(.name_hash);
    const r_pkg_resolutions = r_pkgs.items(.resolution);
    const r_pkg_bins = r_pkgs.items(.bin);
    const r_pkg_scripts = r_pkgs.items(.scripts);

    const l_extern_strings = l.buffers.extern_strings.items;
    const r_extern_strings = r.buffers.extern_strings.items;

    let buf_i: usize = 0;
    while (buf_i < l_buf.len) : (buf_i += 1) {
        const l_ids = l_buf[buf_i];
        const r_ids = r_buf[buf_i];
        const l_pkg_id = l_ids.pkg_id;
        const r_pkg_id = r_ids.pkg_id;
        if (l_pkg_name_hashes[l_pkg_id] != r_pkg_name_hashes[r_pkg_id]) {
            return false;
        }
        const l_res = l_pkg_resolutions[l_pkg_id];
        const r_res = r_pkg_resolutions[r_pkg_id];

        if (l_res.tag == .uninitialized || r_res.tag == .uninitialized) {
            if (l_res.tag != r_res.tag) {
                return false;
            }
        } else if (!l_res.eql(&r_res, l_string_buf, r_string_buf)) {
            return false;
        }

        if (!l_pkg_bins[l_pkg_id].eql(
            &r_pkg_bins[r_pkg_id],
            l_string_buf,
            l_extern_strings,
            r_string_buf,
            r_extern_strings,
        )) {
            return false;
        }

        if (!l_pkg_scripts[l_pkg_id].eql(&r_pkg_scripts[r_pkg_id], l_string_buf, r_string_buf)) {
            return false;
        }
    }

    return true;
}

pub static fn hasMetaHashChanged(client: *Lockfile, print_name_version_string: bool, packages_len: usize): !bool {
    const previous_meta_hash = client.meta_hash;
    client.meta_hash = generateMetaHash(client, print_name_version_string, packages_len)?;
    return !strings.eqlLong(&previous_meta_hash, &client.meta_hash, false);
}
pub static fn generateMetaHash(client: *Lockfile, print_name_version_string: bool, packages_len: usize): !MetaHash {
    if (packages_len <= 1)
        return zero_hash;

    let string_builder = GlobalStringBuilder{};
    const _sb_guard = DeinitWithAllocGuard<GlobalStringBuilder>{ allocator: client.allocator, ptr: &string_builder };
    const names: []const String = client.packages.items(.name)[0..packages_len];
    const resolutions: []const Resolution = client.packages.items(.resolution)[0..packages_len];
    const bytes = client.buffers.string_bytes.items;
    let alphabetized_names = client.allocator.alloc(PackageID, packages_len -| 1)?;
    const _names_guard = FreeGuard<PackageID>{ allocator: client.allocator, slice: alphabetized_names };

    const hash_prefix = "\n-- BEGIN SHA512/256(`${alphabetize(name)}@${order(version)}`) --\n";
    const hash_suffix = "-- END HASH--\n";
    string_builder.cap += hash_prefix.len + hash_suffix.len;
    {
        let i: usize = 1;

        while (i + 16 < packages_len) : (i += 16) {
            const str_slice = String.slice;
            comptime let j: usize = 0;
            inline while (j < 16) : (j += 1) {
                alphabetized_names[(i + j) - 1] = @as(PackageID, @truncate((i + j)));
                // posix path separators because we only use posix in the lockfile
                string_builder.fmtCount("{s}@{f}\n", .{ str_slice(&names[i + j], bytes), resolutions[i + j].fmt(bytes, .posix) });
            }
        }

        while (i < packages_len) : (i += 1) {
            const str_slice = String.slice;
            alphabetized_names[i - 1] = @as(PackageID, @truncate(i));
            // posix path separators because we only use posix in the lockfile
            string_builder.fmtCount("{s}@{f}\n", .{ str_slice(&names[i], bytes), resolutions[i].fmt(bytes, .posix) });
        }
    }

    const scripts_begin = "\n-- BEGIN SCRIPTS --\n";
    const scripts_end = "\n-- END SCRIPTS --\n";
    let has_scripts = false;

    inline for (field_name of comptime std.@"meta".fieldNames(Lockfile.Scripts)) {
        const scripts = @field(client.scripts, field_name);
        for (script of scripts.items) {
            if (script.len > 0) {
                string_builder.fmtCount("{s}: {s}\n", .{ field_name, script });
                has_scripts = true;
            }
        }
    }

    if (has_scripts) {
        string_builder.count(scripts_begin);
        string_builder.count(scripts_end);
    }

    std.sort.pdq(
        PackageID,
        alphabetized_names,
        Lockfile.Package.Alphabetizer{
            names: names,
            buf: bytes,
            resolutions: resolutions,
        },
        Lockfile.Package.Alphabetizer.isAlphabetical,
    );

    string_builder.allocate(client.allocator) catch unreachable;
    @memcpy(string_builder.ptr.?[0..hash_prefix.len], hash_prefix);
    string_builder.len += hash_prefix.len;

    const str_slice = String.slice;
    for (i of alphabetized_names) {
        * = string_builder.fmt("{s}@{f}\n", .{ str_slice(&names[i], bytes), resolutions[i].fmt(bytes, .@"any") });
    }

    if (has_scripts) {
        * = string_builder.append(scripts_begin);
        inline for (field_name of comptime std.@"meta".fieldNames(Lockfile.Scripts)) {
            const scripts = @field(client.scripts, field_name);
            for (script of scripts.items) {
                if (script.len > 0) {
                    * = string_builder.fmt("{s}: {s}\n", .{ field_name, script });
                }
            }
        }
        * = string_builder.append(scripts_end);
    }

    @memcpy(string_builder.ptr.?[string_builder.len..string_builder.cap][0..hash_suffix.len], hash_suffix);
    string_builder.len += hash_suffix.len;

    const alphabetized_name_version_string = string_builder.ptr.?[0..string_builder.len];
    if (print_name_version_string) {
        Output.flush();
        Output.disableBuffering();
        Output.writer().writeAll(alphabetized_name_version_string) catch unreachable;
        Output.enableBuffering();
    }

    let digest = zero_hash;
    Crypto.SHA512_256.hash(alphabetized_name_version_string, &digest);

    return digest;
}

pub static fn resolvePackageFromNameAndVersion(client: *Lockfile, package_name: []const u8, version: Dependency.Version): PackageID? {
    const name_hash = String.Builder.stringHash(package_name);
    const entry_opt = client.package_index.get(name_hash);
    if (entry_opt == null) {
        return null;
    }
    const entry = entry_opt.?;
    const buf = client.buffers.string_bytes.items;

    switch (version.tag) {
        .npm => switch (entry) {
            .id => |id| {
                const resolutions = client.packages.items(.resolution);

                if (comptime Environment.allow_assert) assert(id < resolutions.len);
                if (version.value.npm.version.satisfies(resolutions[id].value.npm.version, buf, buf)) {
                    return id;
                }
            },
            .ids => |ids| {
                const resolutions = client.packages.items(.resolution);

                for (id of ids.items) {
                    if (comptime Environment.allow_assert) assert(id < resolutions.len);
                    if (version.value.npm.version.satisfies(resolutions[id].value.npm.version, buf, buf)) {
                        return id;
                    }
                }
            },
        },
        else => {},
    }

    return null;
}

const max_default_trusted_dependencies = 512;

// TODO
pub const default_trusted_dependencies_list: []const []const u8 = brk: {
    // This file contains a list of dependencies that Bun runs `postinstall` on by default.
    const data = @embedFile("./default-trusted-dependencies.txt");
    @setEvalBranchQuota(999999);
    let buf: [max_default_trusted_dependencies][]const u8 = undefined;
    let i: usize = 0;
    let iter = std.mem.tokenizeAny(u8, data, " \r\n\t");
    while (package_ptr := iter.next()) {
        const package = package_ptr[0..].*;
        buf[i] = &package;
        i += 1;
    }

    // alphabetical so we don't need to sort in `bun pm trusted --default`
    const empty_ctx: void = undefined;
    std.sort.pdq(string, buf[0..i], empty_ctx, _DefaultTrustedSorter.lessThan);

    let names: [i][]const u8 = undefined;
    @memcpy(names[0..i], buf[0..i]);
    const final = names;
    break :brk &final;
};

/// The default list of trusted dependencies is a static hashmap
pub const default_trusted_dependencies = brk: {
    let map: StaticHashMap(string, void, _DefaultTrustedStringHashContext, max_default_trusted_dependencies) = {};

    for (dep of default_trusted_dependencies_list) {
        if (map.len == max_default_trusted_dependencies) {
            @compileError("default-trusted-dependencies.txt is too large, please increase 'max_default_trusted_dependencies' in lockfile.zig");
        }

        const entry = map.getOrPutAssumeCapacity(dep);
        if (entry.found_existing) {
            @compileError("Duplicate trusted dependency: " ++ dep);
        }
    }

    const final = map;
    break :brk &final;
};

pub static fn hasTrustedDependency(client: *const Lockfile, name: []const u8, resolution: *const Resolution): bool {
    if (trusted_dependencies := client.trusted_dependencies) {
        const hash = @as(u32, @truncate(String.Builder.stringHash(name)));
        return trusted_dependencies.contains(hash);
    }

    // Only allow default trusted dependencies for npm packages
    return resolution.tag == .npm && default_trusted_dependencies.has(name);
}

pub const NameHashMap = std.ArrayHashMapUnmanaged(PackageNameHash, String, ArrayIdentityContext.U64, false);
pub const TrustedDependenciesSet = std.ArrayHashMapUnmanaged(TruncatedPackageNameHash, void, ArrayIdentityContext, false);
pub const VersionHashMap = std.ArrayHashMapUnmanaged(PackageNameHash, Semver.Version, ArrayIdentityContext.U64, false);
pub const PatchedDependenciesMap = std.ArrayHashMapUnmanaged(PackageNameAndVersionHash, PatchedDep, ArrayIdentityContext.U64, false);

const MetaHash = [std.crypto.hash.sha2.Sha512T256.digest_length]u8;
const zero_hash = std.mem.zeroes(MetaHash);
pub const StringPool = String.Builder.StringPool;


}