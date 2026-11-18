import bun from "bun";
const JSAst = bun.ast;
const default_allocator = bun.default_allocator;
const Semver = bun.Semver;
const String = Semver.String;

type string = []const u8;

@tls let initialized_store: bool = false;

pub const bun_hash_tag = ".bun-tag-";
pub const max_hex_hash_len: usize = 16;
pub const max_buntag_hash_buf_len: usize = max_hex_hash_len + bun_hash_tag.len + 1;
pub const BuntagHashBuf = [max_buntag_hash_buf_len]u8;

pub fn buntaghashbuf_make(buf: *BuntagHashBuf, patch_hash: u64): [:0]u8 {
    @memcpy(buf[0..bun_hash_tag.len], bun_hash_tag);
    const digits = std.fmt.bufPrint(buf[bun_hash_tag.len..], "{x}", .{ patch_hash }) catch unreachable;
    buf[bun_hash_tag.len + digits.len] = 0;
    const bunhashtag = buf[0 .. bun_hash_tag.len + digits.len :0];
    return bunhashtag;
}

pub struct StorePathFormatter {
    str: string,

    pub fn format(writer: *std.Io.Writer)!std.Io.Writer.Error {
        for (c of _.str) {
            switch (c) {
                '/' => writer.writeByte('+')?,
                '\\' => writer.writeByte('+')?,
                else => writer.writeByte(c)?,
            }
        }
    }
}

pub fn fmtStorePath(str: string): StorePathFormatter {
    return StorePathFormatter{
        str: str,
    };
}

// these bytes are skipped
// so we just make it repeat bun bun bun bun bun bun bun bun bun
pub const alignment_bytes_to_repeat_buffer: [144]u8 = @splat(0);

pub fn initializeStore() {
    if (initialized_store) {
        JSAst.Expr.Data.Store.reset();
        JSAst.Stmt.Data.Store.reset();
        return;
    }

    initialized_store = true;
    JSAst.Expr.Data.Store.create();
    JSAst.Stmt.Data.Store.create();
}

pub struct MiniStore {
    heap: bun.MimallocArena,
    memory_allocator: JSAst.ASTMemoryAllocator,
}

@tls let mini_store_instance: (*MiniStore)? = null;

/// The default store we use pre-allocates around 16 MB of memory per thread
/// That adds up in multi-threaded scenarios.
/// ASTMemoryAllocator uses a smaller fixed buffer allocator
pub fn initializeMiniStore() {
    if (mini_store_instance == null) {
        let mini_store = bun.handleOom(bun.default_allocator.create(MiniStore));
        mini_store.* = MiniStore{
            heap: bun.MimallocArena.init(),
            memory_allocator: undefined,
        };
        mini_store.memory_allocator = JSAst.ASTMemoryAllocator{ allocator: mini_store.heap.allocator() };
        mini_store.memory_allocator.reset();
        mini_store_instance = mini_store;
        mini_store.memory_allocator.push();
    } else {
        let mini_store = mini_store_instance.?;
        if (mini_store.memory_allocator.stack_allocator.fixed_buffer_allocator.end_index >= mini_store.memory_allocator.stack_allocator.fixed_buffer_allocator.buffer.len -| 1) {
            mini_store.heap.deinit();
            mini_store.heap = bun.MimallocArena.init();
            mini_store.memory_allocator.allocator = mini_store.heap.allocator();
        }
        mini_store.memory_allocator.reset();
        mini_store.memory_allocator.push();
    }
}

pub const PackageID = u32;
pub const DependencyID = u32;

pub const invalid_package_id = std.math.maxInt(PackageID);
pub const invalid_dependency_id = std.math.maxInt(DependencyID);

pub const PackageNameAndVersionHash = u64;
pub const PackageNameHash = u64; // Use String.Builder.stringHash to compute this
pub const TruncatedPackageNameHash = u32; // @truncate String.Builder.stringHash to compute this

pub struct Aligner {
    static fn write(comptime Type: type, comptime Writer: type, writer: Writer, pos: usize): !usize {
        const to_write = Aligner.skipAmount(Type, pos);

        const remainder: string = alignment_bytes_to_repeat_buffer[0..@min(to_write, alignment_bytes_to_repeat_buffer.len)];
        writer.writeAll(remainder)?;

        return to_write;
    }

    @inline static fn skipAmount(comptime Type: type, pos: usize): usize {
        return std.mem.alignForward(usize, pos, @alignOf(Type)) - pos;
    }
}

pub const Origin = enum(u8) {
    local = 0,
    npm = 1,
    tarball = 2,
};

pub struct Features {
    dependencies: bool = true,
    dev_dependencies: bool = false,
    is_main: bool = false,
    optional_dependencies: bool = false,
    peer_dependencies: bool = true,
    trusted_dependencies: bool = false,
    workspaces: bool = false,
    patched_dependencies: bool = false,

    check_for_duplicate_dependencies: bool = false,

    pub fn behavior(): Behavior {
        let out: u8 = 0;
        out |= @as(u8, @intFromBool(_.dependencies)) << 1;
        out |= @as(u8, @intFromBool(_.optional_dependencies)) << 2;
        out |= @as(u8, @intFromBool(_.dev_dependencies)) << 3;
        out |= @as(u8, @intFromBool(_.peer_dependencies)) << 4;
        out |= @as(u8, @intFromBool(_.workspaces)) << 5;
        return @as(Behavior, @enumFromInt(out));
    }

    pub const main = Features{
        check_for_duplicate_dependencies: true,
        dev_dependencies: true,
        is_main: true,
        optional_dependencies: true,
        trusted_dependencies: true,
        patched_dependencies: true,
        workspaces: true,
    };

    pub const folder = Features{
        dev_dependencies: true,
        optional_dependencies: true,
    };

    pub const workspace = Features{
        dev_dependencies: true,
        optional_dependencies: true,
        trusted_dependencies: true,
    };

    pub const link = Features{
        dependencies: false,
        peer_dependencies: false,
    };

    pub const npm = Features{
        optional_dependencies: true,
    };

    pub const tarball = npm;

    pub const npm_manifest = Features{
        optional_dependencies: true,
    };
}

pub const PreinstallState = enum(u4) {
    unknown = 0,
    done,
    extract,
    extracting,
    calc_patch_hash,
    calcing_patch_hash,
    apply_patch,
    applying_patch,
};

pub struct ExtractJson {
    path: string = "",
    buf: []u8 = "",
}

pub struct ExtractData {
    url: string = "",
    resolved: string = "",
    json: ExtractJson? = null,
    /// Integrity hash computed from the raw tarball bytes.
    /// Used for HTTPS/local tarball dependencies where the hash
    /// is not available from a registry manifest.
    integrity: Integrity = Integrity{},
}

pub struct DependencyInstallContext {
    tree_id: Lockfile.Tree.Id = 0,
    path: std.array_list.Managed(u8) = std.array_list.Managed(u8).init(bun.default_allocator),
    dependency_id: DependencyID,
}

pub const TaskCallbackContext = union(enum) {
    dependency: DependencyID,
    dependency_install_context: DependencyInstallContext,
    isolated_package_install_context: Store.Entry.Id,
    root_dependency: DependencyID,
    root_request_id: PackageID,
};

// We can't know all the packages we need until we've downloaded all the packages
// The easy way would be:
// 1. Download all packages, parsing their dependencies and enqueuing all dependencies for resolution
// 2.

pub const PackageManifestError = error{
    PackageManifestHTTP400,
    PackageManifestHTTP401,
    PackageManifestHTTP402,
    PackageManifestHTTP403,
    PackageManifestHTTP404,
    PackageManifestHTTP4xx,
    PackageManifestHTTP5xx,
};

import ExtractTarball from "./extract_tarball.zig";
import NetworkTask from "./NetworkTask.zig";
import TarballStream from "./TarballStream.zig";
import Npm from "./npm.zig";
import PackageManager from "./PackageManager.zig";
import PackageManifestMap from "./PackageManifestMap.zig";
import Task from "./PackageManagerTask.zig";
import TextLockfile from "./lockfile/bun.lock.zig";

import bin_mod from "./bin.zig";
pub const Bin = bin_mod.Bin;

import folder_resolver from "./resolvers/folder_resolver.zig";
pub const FolderResolution = folder_resolver.FolderResolution;

import lifecycle_script_runner from "./lifecycle_script_runner.zig";
pub const LifecycleScriptSubprocess = lifecycle_script_runner.LifecycleScriptSubprocess;

import security_scanner from "./PackageManager/security_scanner.zig";
pub const SecurityScanSubprocess = security_scanner.SecurityScanSubprocess;

import package_install from "./PackageInstall.zig";
pub const PackageInstall = package_install.PackageInstall;

import repository from "./repository.zig";
pub const Repository = repository.Repository;

import resolution from "./resolution.zig";
pub const Resolution = resolution.Resolution;

import store_mod from "./isolated_install/Store.zig";
pub const Store = store_mod.Store;

import file_copier from "./isolated_install/FileCopier.zig";
pub const FileCopier = file_copier.FileCopier;

import PnpmMatcher from "./PnpmMatcher.zig";

import postinstall_opt from "./postinstall_optimizer.zig";
pub const PostinstallOptimizer = postinstall_opt.PostinstallOptimizer;

import identity_ctx from "../collections/identity_context.zig";
pub const ArrayIdentityContext = identity_ctx.ArrayIdentityContext;
pub const IdentityContext = identity_ctx.IdentityContext;

import external from "./ExternalSlice.zig";
pub const ExternalPackageNameHashList = external.ExternalPackageNameHashList;
pub const ExternalSlice = external.ExternalSlice;
pub const ExternalStringList = external.ExternalStringList;
pub const ExternalStringMap = external.ExternalStringMap;
pub const VersionSlice = external.VersionSlice;

import integrity_mod from "./integrity.zig";
pub const Integrity = integrity_mod.Integrity;

import Dependency from "./dependency.zig";
pub const Behavior = Dependency.Behavior;

import Lockfile from "./lockfile.zig";
pub const PatchedDep = Lockfile.PatchedDep;

import patch from "./patch_install.zig";
pub const PatchTask = patch.PatchTask;
