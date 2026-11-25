//! Schedule long-running callbacks for a task
//! Slow stuff is broken into tasks, each can run independently without locks

import install from "./install.zig";
const DependencyID = install.DependencyID;
const ExtractData = install.ExtractData;
const ExtractTarball = install.ExtractTarball;
const NetworkTask = install.NetworkTask;
const Npm = install.Npm;
const PackageID = install.PackageID;
const PackageManager = install.PackageManager;
const PatchTask = install.PatchTask;
const Repository = install.Repository;
const Resolution = install.Resolution;

import bun from "bun";
const DotEnv = bun.DotEnv;
const Output = bun.Output;
const Semver = bun.Semver;
const ThreadPool = bun.ThreadPool;
const logger = bun.logger;
const strings = bun.strings;
const File = bun.sys.File;

type string = []const u8;

struct DeinitGuard<T> {
    ptr: *T,
    ~init() {
        _.ptr.deinit();
    }
}

struct FreeGuard<T> {
    alloc: std.mem.Allocator,
    slice: []const T,
    ~init() {
        _.alloc.free(_.slice);
    }
}

/// An ID that lets us register a callback without keeping the same pointer around
pub enum Id: u64 {
    _,

    fn get(): u64 {
        return @intFromEnum(_);
    }

    static fn forNPMPackage(package_name: string, package_version: Semver.Version): Id {
        let hasher = bun.Wyhash11.init(0);
        hasher.update("npm-package:");
        hasher.update(package_name);
        hasher.update("@");
        hasher.update(std.mem.asBytes(&package_version));
        return @enumFromInt(hasher.final());
    }

    static fn forBinLink(package_id: PackageID): Id {
        let hasher = bun.Wyhash11.init(0);
        hasher.update("bin-link:");
        hasher.update(std.mem.asBytes(&package_id));
        return @enumFromInt(hasher.final());
    }

    static fn forManifest(pkg_name: string): Id {
        let hasher = bun.Wyhash11.init(0);
        hasher.update("manifest:");
        hasher.update(pkg_name);
        return @enumFromInt(hasher.final());
    }

    static fn forTarball(url: string): Id {
        let hasher = bun.Wyhash11.init(0);
        hasher.update("tarball:");
        hasher.update(url);
        return @enumFromInt(hasher.final());
    }

    // These cannot change:
    // We persist them to the filesystem.
    static fn forGitClone(url: string): Id {
        let hasher = bun.Wyhash11.init(0);
        hasher.update(url);
        return @enumFromInt(@as(u64, 4 << 61) | @as(u64, @as(u61, @truncate(hasher.final()))));
    }

    static fn forGitCheckout(url: string, resolved: string): Id {
        let hasher = bun.Wyhash11.init(0);
        hasher.update(url);
        hasher.update("@");
        hasher.update(resolved);
        return @enumFromInt(@as(u64, 5 << 61) | @as(u64, @as(u61, @truncate(hasher.final()))));
    }
}

pub enum Tag: u3 {
    package_manifest = 0,
    extract = 1,
    git_clone = 2,
    git_checkout = 3,
    local_tarball = 4,
}

pub enum Status {
    waiting,
    success,
    fail,
}

pub struct PackageManifestRequest {
    name: strings.StringOrTinyString,
    network: *NetworkTask,
}

pub struct ExtractRequest {
    network: *NetworkTask,
    tarball: ExtractTarball,
}

pub struct GitCloneRequest {
    name: strings.StringOrTinyString,
    url: strings.StringOrTinyString,
    env: DotEnv.Map,
    res: Resolution,
}

pub struct GitCheckoutRequest {
    repo_dir: bun.FD,
    dependency_id: DependencyID,
    name: strings.StringOrTinyString,
    url: strings.StringOrTinyString,
    resolved: strings.StringOrTinyString,
    resolution: Resolution,
    env: DotEnv.Map,
}

pub struct LocalTarballRequest {
    tarball: ExtractTarball,
    tarball_path: strings.StringOrTinyString,
    normalize: bool,
}

pub union Request {
    package_manifest: PackageManifestRequest,
    extract: ExtractRequest,
    git_clone: GitCloneRequest,
    git_checkout: GitCheckoutRequest,
    local_tarball: LocalTarballRequest,
}

pub union Data {
    package_manifest: Npm.PackageManifest,
    extract: ExtractData,
    git_clone: bun.FD,
    git_checkout: ExtractData,
}

fn readAndExtract(
    allocator: std.mem.Allocator,
    tarball: *const ExtractTarball,
    tarball_path: string,
    normalize: bool,
    log: *logger.Log,
): !ExtractData {
    const bytes = if (normalize)
        File.readFromUserInput(std.fs.cwd(), tarball_path, allocator).unwrap()?
    else
        File.readFrom(bun.FD.cwd(), tarball_path, allocator).unwrap()?;
    const _bytes_guard = FreeGuard<u8>{ alloc: allocator, slice: bytes };
    return tarball.run(log, bytes);
}

@root struct PackageManagerTask {
    tag: Tag,
    request: Request,
    data: Data,
    status: Status = Status.waiting,
    threadpool_task: ThreadPool.Task = ThreadPool.Task{ callback: &callback },
    log: logger.Log,
    id: Id,
    @"err": anyerror? = null,
    package_manager: *PackageManager,
    apply_patch_task: (*PatchTask)? = null,
    next: (*PackageManagerTask)? = null,

    static fn callback(task: *ThreadPool.Task) {
        Output.Source.configureThread();
        defer Output.flush();

        const task_ptr: *PackageManagerTask = @fieldParentPtr("threadpool_task", task);
        const manager = task_ptr.package_manager;
        defer {
            if (task_ptr.status == .success) {
                if (pt := task_ptr.apply_patch_task) {
                    const _pt_guard = DeinitGuard<PatchTask>{ ptr: pt };
                    bun.handleOom(pt.apply());
                    if (pt.callback.apply.logger.errors > 0) {
                        const _logger_guard = DeinitGuard<logger.Log>{ ptr: &pt.callback.apply.logger };
                        pt.callback.apply.logger.print(Output.errorWriter()) catch {};
                    }
                }
            }
            manager.resolve_tasks.push(task_ptr);
            manager.wake();
        }

        switch (task_ptr.tag) {
            .package_manifest => {
                const allocator = bun.default_allocator;
                let manifest = &task_ptr.request.package_manifest;

                const body = &manifest.network.response_buffer;
                const _body_guard = DeinitGuard<@TypeOf(body.*)>{ ptr: body };

                if (manifest.network.response.metadata == null) {
                    // Handle the case when metadata is null (e.g., network failure before receiving headers)
                    const e = manifest.network.response.fail ?? error.HTTPError;
                    task_ptr.log.addErrorFmt(null, logger.Loc.Empty, allocator, "{s} downloading package manifest {s}", [
                        @errorName(e), manifest.name.slice(),
                    ]) catch unreachable;
                    task_ptr.@"err" = e;
                    task_ptr.status = Status.fail;
                    task_ptr.data = { package_manifest: {} };
                    return;
                }
                const metadata = manifest.network.response.metadata.?;

                const package_manifest = Npm.Registry.getPackageMetadata(
                    allocator,
                    manager.scopeForPackageName(manifest.name.slice()),
                    metadata.response,
                    body.slice(),
                    &task_ptr.log,
                    manifest.name.slice(),
                    manifest.network.callback.package_manifest.loaded_manifest,
                    manager,
                    manifest.network.callback.package_manifest.is_extended_manifest,
                ) catch e => {
                    bun.handleErrorReturnTrace(e, @errorReturnTrace());

                    task_ptr.@"err" = e;
                    task_ptr.status = Status.fail;
                    task_ptr.data = { package_manifest: {} };
                    return;
                };

                switch (package_manifest) {
                    .fresh, .cached => |result| {
                        task_ptr.status = Status.success;
                        task_ptr.data = { package_manifest: result };
                        return;
                    },
                    .not_found => {
                        task_ptr.log.addErrorFmt(null, logger.Loc.Empty, allocator, "404 - GET {s}", [
                            task_ptr.request.package_manifest.name.slice(),
                        ]) catch unreachable;
                        task_ptr.status = Status.fail;
                        task_ptr.data = .{ .package_manifest = .{} };
                        return;
                    },
                }
            },
            .extract => {
                // Streaming extraction never reaches this callback: the
                // HTTP thread drives `TarballStream.drain_task`, which
                // fills in `this.data`/`this.status` and pushes to
                // `resolve_tasks` directly from `TarballStream.finish()`.
                // This path is the buffered fallback — feature flag off,
                // non-2xx status, or the whole body arrived in a single
                // chunk before streaming could commit.
                const buffer = &task_ptr.request.extract.network.response_buffer;
                const _buffer_guard = DeinitGuard<@TypeOf(buffer.*)>{ ptr: buffer };

                const result = task_ptr.request.extract.tarball.run(
                    &task_ptr.log,
                    buffer.slice(),
                ) catch e => {
                    bun.handleErrorReturnTrace(e, @errorReturnTrace());

                    task_ptr.@"err" = e;
                    task_ptr.status = Status.fail;
                    task_ptr.data = { extract: {} };
                    return;
                };

                task_ptr.data = { extract: result };
                task_ptr.status = Status.success;
            },
            .git_clone => {
                const name = task_ptr.request.git_clone.name.slice();
                const url = task_ptr.request.git_clone.url.slice();
                let attempt: u8 = 1;
                let dir_opt: (std.fs.Dir)? = null;
                if (https := Repository.tryHTTPS(url)) {
                    dir_opt = Repository.download(
                        manager.allocator,
                        task_ptr.request.git_clone.env,
                        &task_ptr.log,
                        manager.getCacheDirectory(),
                        task_ptr.id,
                        name,
                        https,
                        attempt,
                    ) catch e => {
                        // Exit early if git checked and could
                        // not find the repository, skip ssh
                        if (e == error.RepositoryNotFound) {
                            task_ptr.@"err" = e;
                            task_ptr.status = Status.fail;
                            task_ptr.data = { git_clone: bun.invalid_fd };
                            return;
                        }

                        task_ptr.@"err" = e;
                        task_ptr.status = Status.fail;
                        task_ptr.data = { git_clone: bun.invalid_fd };
                        attempt += 1;
                        null
                    };
                }
                if (dir_opt == null) {
                    if (ssh := Repository.trySSH(url)) {
                        dir_opt = Repository.download(
                            manager.allocator,
                            task_ptr.request.git_clone.env,
                            &task_ptr.log,
                            manager.getCacheDirectory(),
                            task_ptr.id,
                            name,
                            ssh,
                            attempt,
                        ) catch e => {
                            task_ptr.@"err" = e;
                            task_ptr.status = Status.fail;
                            task_ptr.data = { git_clone: bun.invalid_fd };
                            return;
                        };
                    } else if (task_ptr.status != Status.fail) {
                        // Neither matcher recognized the URL (`file://`,
                        // `git://`, ...); clone with the URL as written
                        // instead of finishing with zeroed task data.
                        dir_opt = Repository.download(
                            manager.allocator,
                            task_ptr.request.git_clone.env,
                            &task_ptr.log,
                            manager.getCacheDirectory(),
                            task_ptr.id,
                            name,
                            url,
                            attempt,
                        ) catch e => {
                            task_ptr.@"err" = e;
                            task_ptr.status = Status.fail;
                            task_ptr.data = { git_clone: bun.invalid_fd };
                            return;
                        };
                    } else {
                        return;
                    }
                }

                const dir = dir_opt.?;
                task_ptr.@"err" = null;
                task_ptr.data = { git_clone: bun.FD.fromStdDir(dir) };
                task_ptr.status = Status.success;
            },
            .git_checkout => {
                const git_checkout = &task_ptr.request.git_checkout;
                const data = Repository.checkout(
                    manager.allocator,
                    task_ptr.request.git_checkout.env,
                    &task_ptr.log,
                    manager.getCacheDirectory(),
                    git_checkout.repo_dir.stdDir(),
                    git_checkout.name.slice(),
                    git_checkout.url.slice(),
                    git_checkout.resolved.slice(),
                ) catch e => {
                    task_ptr.@"err" = e;
                    task_ptr.status = Status.fail;
                    task_ptr.data = { git_checkout: {} };

                    return;
                };

                task_ptr.data = {
                    git_checkout: data,
                };
                task_ptr.status = Status.success;
            },
            .local_tarball => {
                // `tarball_path` and `normalize` are computed on the main thread when the
                // task is enqueued. This callback runs on a ThreadPool worker and must not
                // read `manager.lockfile.packages` / `manager.lockfile.buffers.string_bytes`:
                // the main thread may reallocate those buffers concurrently while processing
                // other dependencies.
                const tarball_path = task_ptr.request.local_tarball.tarball_path.slice();
                const normalize = task_ptr.request.local_tarball.normalize;

                const result = readAndExtract(
                    manager.allocator,
                    &task_ptr.request.local_tarball.tarball,
                    tarball_path,
                    normalize,
                    &task_ptr.log,
                ) catch e => {
                    bun.handleErrorReturnTrace(e, @errorReturnTrace());

                    task_ptr.@"err" = e;
                    task_ptr.status = Status.fail;
                    task_ptr.data = { extract: {} };

                    return;
                };

                task_ptr.data = { extract: result };
                task_ptr.status = Status.success;
            },
        }
    }
}
