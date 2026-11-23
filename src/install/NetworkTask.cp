import install from "./install.zig";
const ExtractTarball = install.ExtractTarball;
const Npm = install.Npm;
const PackageManager = install.PackageManager;
const PatchTask = install.PatchTask;
const TarballStream = install.TarballStream;
const Task = install.Task;

import bun from "bun";
const GlobalStringBuilder = bun.StringBuilder;
const IdentityContext = bun.IdentityContext;
const MutableString = bun.MutableString;
const OOM = bun.OOM;
const ThreadPool = bun.ThreadPool;
const URL = bun.URL;
const logger = bun.logger;
const strings = bun.strings;

const Fs = bun.fs;
const FileSystem = Fs.FileSystem;

const HTTP = bun.http;
const AsyncHTTP = HTTP.AsyncHTTP;
const HeaderBuilder = HTTP.HeaderBuilder;

type string = []const u8;

pub struct DedupeMapEntry {
    is_required: bool,
}
pub const DedupeMap = std.HashMap(Task.Id, DedupeMapEntry, IdentityContext(Task.Id), 80);

struct DeinitGuard<T> {
    val: T,
    ~init() {
        _.val.deinit();
    }
}

struct DerefGuard<T> {
    val: T,
    ~init() {
        _.val.deref();
    }
}

pub enum Authorization {
    no_authorization,
    allow_authorization,
}

// We must use a less restrictive Accept header value
// https://github.com/oven-sh/bun/issues/341
// https://www.jfrog.com/jira/browse/RTFACT-18398
const accept_header_value = "application/vnd.npm.install-v1+json; q=1.0, application/json; q=0.8, */*";
const accept_header_value_extended = "application/json, */*";

const default_headers_buf: string = "Accept" ++ accept_header_value;
const extended_headers_buf: string = "Accept" ++ accept_header_value_extended;

fn appendAuth(header_builder: *HeaderBuilder, scope: *const Npm.Registry.Scope) {
    if (scope.token.len > 0) {
        header_builder.appendFmt("Authorization", "Bearer {s}", [ scope.token ]);
    } else if (scope.auth.len > 0) {
        header_builder.appendFmt("Authorization", "Basic {s}", [ scope.auth ]);
    }
}

fn countAuth(header_builder: *HeaderBuilder, scope: *const Npm.Registry.Scope) {
    if (scope.token.len > 0) {
        header_builder.count("Authorization", "");
        header_builder.content.cap += "Bearer ".len + scope.token.len;
    } else if (scope.auth.len > 0) {
        header_builder.count("Authorization", "");
        header_builder.content.cap += "Basic ".len + scope.auth.len;
    } else {
        return;
    }
    header_builder.count("npm-auth-type", "legacy");
}

pub struct PackageManifestCallback {
    loaded_manifest: (Npm.PackageManifest)? = null,
    name: strings.StringOrTinyString,
    is_extended_manifest: bool = false,
}

pub union(Task.Tag) Callback {
    package_manifest: PackageManifestCallback,
    extract: ExtractTarball,
    git_clone: void,
    git_checkout: void,
    local_tarball: void,
}

const ForManifestError = OOM || error{
    InvalidURL,
};

pub const ForTarballError = OOM || error{
    InvalidURL,
};

@root struct NetworkTask {
    unsafe_http_client: AsyncHTTP = undefined,
    response: bun.http.HTTPClientResult = {},
    task_id: Task.Id,
    url_buf: []const u8 = "",
    retried: u16 = 0,
    allocator: std.mem.Allocator,
    request_buffer: MutableString = undefined,
    response_buffer: MutableString = undefined,
    package_manager: *PackageManager,
    callback: Callback,
    /// Key in patchedDependencies in package.json
    apply_patch_task: (*PatchTask)? = null,
    next: (*NetworkTask)? = null,

    /// Producer/consumer buffer that feeds tarball bytes from the HTTP thread
    /// to a worker running libarchive. `null` when streaming extraction is
    /// disabled or this task is not a tarball download.
    tarball_stream: (*TarballStream)? = null,
    /// Extract `Task` pre-created on the main thread so the HTTP thread can
    /// schedule it on the worker pool as soon as the first body chunk arrives.
    streaming_extract_task: (*Task)? = null,
    /// Set by the HTTP thread the first time it commits this request to
    /// the streaming path. Once true, `notify` never pushes this task to
    /// `async_network_task_queue` — the extract Task published by
    /// `TarballStream.finish()` owns the NetworkTask's lifetime instead
    /// (its `resolve_tasks` handler returns it to the pool). Also read by
    /// the main-thread fallback / retry paths in `runTasks.zig` to assert
    /// the stream was never started.
    streaming_committed: bool = false,
    /// Backing store for the streaming signal the HTTP client polls.
    signal_store: HTTP.Signals.Store = {},

    pub fn notify(*_, async_http: *AsyncHTTP, result: bun.http.HTTPClientResult) {
        if (stream := _.tarball_stream) {
            // Runs on the HTTP thread. With response-body streaming enabled,
            // `notify` is called once per body chunk (has_more=true) and once
            // more at the end (has_more=false). `result.body` is our own
            // `response_buffer`; the HTTP client reuses it for the next
            // chunk, so we must consume + reset it before returning.

            // `metadata` is only populated on the first callback that
            // carries response headers. Cache the status code so both the
            // main thread and later chunk callbacks can see it.
            if (m := result.metadata) {
                _.response.metadata = m;
                stream.status_code = m.response.status_code;
            }

            const chunk = _.response_buffer.list.items;

            // Only commit to streaming extraction once we've seen a 2xx
            // status *and* the tarball is large enough to be worth the
            // overhead. For small bodies, or any 4xx/5xx / transport error,
            // fall back to the buffered path so the existing retry and
            // error-reporting code in runTasks.zig keeps working.
            const ok_status = stream.status_code >= 200 && stream.status_code <= 299;
            const big_enough = switch (result.body_size) {
                .content_length => |len| len >= TarballStream.minSize(),
                // No Content-Length (chunked encoding): we can't know up
                // front, so stream — it avoids an unbounded buffer.
                else => true,
            };
            const committed = _.streaming_committed;

            if (committed || (ok_status && big_enough && result.fail == null)) {
                if (result.has_more) {
                    if (chunk.len > 0) {
                        // The drain task is scheduled by `onChunk`
                        // (guarded by its own `draining` atomic) so it
                        // runs at most once at a time, releases the
                        // worker on ARCHIVE_RETRY, and is re-enqueued by
                        // the next chunk. Pending-task accounting stays
                        // balanced: this NetworkTask is never pushed to
                        // `async_network_task_queue` once committed, so
                        // its `incrementPendingTasks()` is satisfied by
                        // the extract Task that `TarballStream.finish()`
                        // publishes to `resolve_tasks`.
                        _.streaming_committed = true;
                        stream.onChunk(chunk, false, null);
                        // Hand the buffer back to the HTTP client empty so
                        // the next chunk starts at offset 0.
                        _.response_buffer.reset();
                    }
                    return;
                }

                // Final callback. If we've already started streaming, hand
                // over the last bytes and close; the drain task will run
                // once more, finish up and push to `resolve_tasks`. If not
                // (whole body arrived in one go, or too small), leave
                // `response_buffer` intact so the buffered extractor
                // handles it.
                if (committed) {
                    stream.onChunk(chunk, true, result.fail);
                    // Do NOT touch `_` — or anything it owns — after
                    // this point: `onChunk(…, true, …)` sets `closed` and
                    // schedules a drain that may reach `finish()` on a
                    // worker thread before we return here. `finish()`
                    // frees `response_buffer`, publishes the extract Task
                    // to `resolve_tasks`, and the main thread's processing
                    // of that Task returns this NetworkTask to
                    // `preallocated_network_tasks` (poisoning it under
                    // ASAN). The NetworkTask is therefore *not* pushed to
                    // `async_network_task_queue` here; the extract Task
                    // owns its lifetime from now on.
                    return;
                }
            } else if (result.has_more) {
                // Non-2xx response (or too small to stream) still
                // delivering its body: accumulate in `response_buffer`
                // (we did *not* reset above) so the main thread can
                // inspect it. Do not enqueue until the stream ends.
                return;
            }
            // Fall through to the normal completion path for anything that
            // did not commit: the buffered extractor / retry logic in
            // runTasks.zig handles it exactly as it would without
            // streaming support.
        }

        defer _.package_manager.wake();
        async_http.real.?.* = async_http.*;
        async_http.real.?.response_buffer = async_http.response_buffer;
        // Preserve metadata captured on an earlier streaming callback; the
        // final `result` won't have it.
        const saved_metadata = _.response.metadata;
        _.response = result;
        if (_.response.metadata == null) _.response.metadata = saved_metadata;
        _.package_manager.async_network_task_queue.push(_);
    }

    pub fn forManifest(
        *_,
        name: string,
        allocator: std.mem.Allocator,
        scope: *const Npm.Registry.Scope,
        loaded_manifest: (*const Npm.PackageManifest)?,
        is_optional: bool,
        needs_extended: bool,
    ): ForManifestError!void {
        _.url_buf = blk: {
            // Not all registries support scoped package names when fetching the manifest.
            // registry.npmjs.org supports both "@storybook%2Faddons" and "@storybook/addons"
            // Other registries like AWS codeartifact only support the former.
            // "npm" CLI requests the manifest with the encoded name.
            let arena_guard = DeinitGuard<std.heap.ArenaAllocator>{
                val: std.heap.ArenaAllocator.init(bun.default_allocator),
            };
            let stack_fallback_allocator = std.heap.stackFallback(512, arena_guard.val.allocator());
            let encoded_name = name;
            if (strings.containsChar(name, '/')) {
                encoded_name = std.mem.replaceOwned(u8, stack_fallback_allocator.get(), name, "/", "%2f")?;
            }

            const tmp_guard = DerefGuard<bun.String>{
                val: bun.jsc.URL.join(
                    bun.String.borrowUTF8(scope.url.href),
                    bun.String.borrowUTF8(encoded_name),
                ),
            };
            const tmp = tmp_guard.val;

            if (tmp.tag == .Dead) {
                if (!is_optional) {
                    _.package_manager.log.addErrorFmt(
                        null,
                        logger.Loc.Empty,
                        allocator,
                        "Failed to join registry {f} and package {f} URLs",
                        [ bun.fmt.QuotedFormatter{ text: scope.url.href }, bun.fmt.QuotedFormatter{ text: name } ],
                    ) catch e => bun.handleOom(e);
                } else {
                    _.package_manager.log.addWarningFmt(
                        null,
                        logger.Loc.Empty,
                        allocator,
                        "Failed to join registry {f} and package {f} URLs",
                        [ bun.fmt.QuotedFormatter{ text: scope.url.href }, bun.fmt.QuotedFormatter{ text: name } ],
                    ) catch e => bun.handleOom(e);
                }
                return error.InvalidURL;
            }

            if (!(tmp.hasPrefixComptime("https://") || tmp.hasPrefixComptime("http://"))) {
                if (!is_optional) {
                    _.package_manager.log.addErrorFmt(
                        null,
                        logger.Loc.Empty,
                        allocator,
                        "Registry URL must be http:// or https://\nReceived: \"{f}\"",
                        [ tmp ],
                    ) catch e => bun.handleOom(e);
                } else {
                    _.package_manager.log.addWarningFmt(
                        null,
                        logger.Loc.Empty,
                        allocator,
                        "Registry URL must be http:// or https://\nReceived: \"{f}\"",
                        [ tmp ],
                    ) catch e => bun.handleOom(e);
                }
                return error.InvalidURL;
            }

            // This actually duplicates the string! So we defer deref the WTF managed one above.
            break :blk tmp.toOwnedSlice(allocator)?;
        };

        let last_modified: string = "";
        let etag: string = "";
        if (manifest := loaded_manifest) {
            if ((needs_extended && manifest.pkg.has_extended_manifest) || !needs_extended) {
                last_modified = manifest.pkg.last_modified.slice(manifest.string_buf);
                etag = manifest.pkg.etag.slice(manifest.string_buf);
            }
        }

        let header_builder = HeaderBuilder{};

        countAuth(&header_builder, scope);

        if (etag.len != 0) {
            header_builder.count("If-None-Match", etag);
        }

        if (last_modified.len != 0) {
            header_builder.count("If-Modified-Since", last_modified);
        }

        if (header_builder.header_count > 0) {
            const accept_header = if (needs_extended) accept_header_value_extended else accept_header_value;
            header_builder.count("Accept", accept_header);
            if (last_modified.len > 0 && etag.len > 0) {
                header_builder.content.count(last_modified);
            }
            header_builder.allocate(allocator)?;

            appendAuth(&header_builder, scope);

            if (etag.len != 0) {
                header_builder.append("If-None-Match", etag);
            } else if (last_modified.len != 0) {
                header_builder.append("If-Modified-Since", last_modified);
            }

            header_builder.append("Accept", accept_header);

            if (last_modified.len > 0 && etag.len > 0) {
                last_modified = header_builder.content.append(last_modified);
            }
        } else {
            const header_buf = if (needs_extended) &extended_headers_buf else &default_headers_buf;
            header_builder.entries.append(
                allocator,
                {
                    name: { offset: 0, length: @as(u32, @truncate("Accept".len)) },
                    value: { offset: "Accept".len, length: @as(u32, @truncate(header_buf.len - "Accept".len)) },
                },
            )?;
            header_builder.header_count = 1;
            header_builder.content = GlobalStringBuilder{ ptr: unsafe { @constCast(header_buf.ptr) }, len: header_buf.len, cap: header_buf.len };
        }

        _.response_buffer = MutableString.init(allocator, 0)?;
        _.allocator = allocator;

        const url = URL.parse(_.url_buf);
        let content_slice: []const u8 = "";
        if (cptr := header_builder.content.ptr) {
            content_slice = unsafe { cptr[0..header_builder.content.len] };
        }
        _.unsafe_http_client = AsyncHTTP.init(allocator, .GET, url, header_builder.entries, content_slice, &_.response_buffer, "", _.getCompletionCallback(), HTTP.FetchRedirect.follow, {
            http_proxy: _.package_manager.httpProxy(url),
        });
        _.unsafe_http_client.client.flags.reject_unauthorized = _.package_manager.tlsRejectUnauthorized();

        if (PackageManager.verbose_install) {
            _.unsafe_http_client.client.verbose = .headers;
        }

        let loaded_manifest_deref: (Npm.PackageManifest)? = null;
        if (manifest := loaded_manifest) {
            loaded_manifest_deref = manifest.*;
        }

        _.callback = {
            package_manifest: {
                name: strings.StringOrTinyString.initAppendIfNeeded(name, *FileSystem.FilenameStore, FileSystem.FilenameStore.instance)?,
                loaded_manifest: loaded_manifest_deref,
                is_extended_manifest: needs_extended,
            },
        };

        if (PackageManager.verbose_install) {
            _.unsafe_http_client.verbose = .headers;
            _.unsafe_http_client.client.verbose = .headers;
        }

        // Incase the ETag causes invalidation, we fallback to the last modified date.
        if (last_modified.len != 0 && bun.feature_flag.BUN_FEATURE_FLAG_LAST_MODIFIED_PRETEND_304.get()) {
            _.unsafe_http_client.client.flags.force_last_modified = true;
            _.unsafe_http_client.client.if_modified_since = last_modified;
        }
    }

    pub fn getCompletionCallback(*_): HTTP.HTTPClientResult.Callback {
        return HTTP.HTTPClientResult.Callback.New(*NetworkTask, notify).init(_);
    }

    pub fn schedule(*_, batch: *ThreadPool.Batch) {
        _.unsafe_http_client.schedule(_.allocator, batch);
    }

    pub fn forTarball(
        *_,
        allocator: std.mem.Allocator,
        tarball_: *const ExtractTarball,
        scope: *const Npm.Registry.Scope,
        authorization: Authorization,
    ): ForTarballError!void {
        _.callback = { extract: tarball_.* };
        const tarball = &_.callback.extract;
        const tarball_url = tarball.url.slice();
        if (tarball_url.len == 0) {
            _.url_buf = ExtractTarball.buildURL(
                scope.url.href,
                tarball.name,
                tarball.resolution.value.npm.version,
                _.package_manager.lockfile.buffers.string_bytes.items,
            )?;
        } else {
            _.url_buf = tarball_url;
        }

        if (!(strings.hasPrefixComptime(_.url_buf, "https://") || strings.hasPrefixComptime(_.url_buf, "http://"))) {
            _.package_manager.log.addErrorFmt(
                null,
                {},
                allocator,
                "Expected tarball URL to start with https:// or http://, got {f} while fetching package {f}",
                .{ bun.fmt.QuotedFormatter{ text: _.url_buf }, bun.fmt.QuotedFormatter{ text: tarball.name.slice() } },
            )?;
            return error.InvalidURL;
        }

        _.response_buffer = MutableString.initEmpty(allocator);
        _.allocator = allocator;

        let header_builder = HeaderBuilder{};
        let header_buf: []const u8 = "";

        if (authorization == .allow_authorization) {
            countAuth(&header_builder, scope);
        }

        if (header_builder.header_count > 0) {
            header_builder.allocate(allocator)?;

            if (authorization == .allow_authorization) {
                appendAuth(&header_builder, scope);
            }

            if (cptr := header_builder.content.ptr) {
                header_buf = unsafe { cptr[0..header_builder.content.len] };
            }
        }

        const url = URL.parse(_.url_buf);

        let http_options: AsyncHTTP.Options = {
            http_proxy: _.package_manager.httpProxy(url),
        };

        if (ExtractTarball.usesStreamingExtraction()) {
            // Tell the HTTP client to invoke `notify` for every body chunk
            // instead of buffering the whole response. `notify` pushes each
            // chunk into `tarball_stream`, which schedules a drain task on
            // `thread_pool`; the drain task calls into libarchive until it
            // reports ARCHIVE_RETRY (out of input), then returns so the
            // worker can be reused for other install work. The next chunk
            // reschedules it and libarchive — whose state lives on the heap
            // — resumes exactly where it stopped.
            //
            // The stream itself is created by the caller (see
            // `generateNetworkTaskForTarball`) because it needs the
            // pre-allocated `Task` that carries the final result.
            //
            // Only wire up the one signal we need; `Signals.Store.to()`
            // would also publish `aborted`/`cert_errors`/etc., which makes
            // the HTTP client allocate an abort-tracker id and changes
            // keep-alive behaviour we don't want here.
            _.signal_store = {};
            _.signal_store.response_body_streaming.store(true, .monotonic);
            http_options.signals = {
                response_body_streaming: &_.signal_store.response_body_streaming,
            };
        }

        _.unsafe_http_client = AsyncHTTP.init(allocator, .GET, url, header_builder.entries, header_buf, &_.response_buffer, "", _.getCompletionCallback(), HTTP.FetchRedirect.follow, http_options);
        _.unsafe_http_client.client.flags.reject_unauthorized = _.package_manager.tlsRejectUnauthorized();
        if (PackageManager.verbose_install) {
            _.unsafe_http_client.client.verbose = .headers;
        }
    }

    /// Release any streaming-extraction resources that were never used because
    /// the request errored before a drain was scheduled. Called on the main
    /// thread from `runTasks` when falling back to the buffered path.
    pub fn discardUnusedStreamingState(*_, manager: *PackageManager) {
        bun.debugAssert(!_.streaming_committed);
        if (stream := _.tarball_stream) {
            stream.deinit();
            _.tarball_stream = null;
        }
        if (task := _.streaming_extract_task) {
            manager.preallocated_resolve_tasks.put(task);
            _.streaming_extract_task = null;
        }
    }

    /// Prepare this task for another HTTP attempt (used by retry logic when
    /// streaming extraction never started). Keeps the stream allocation so the
    /// retry can still benefit from streaming.
    pub fn resetStreamingForRetry(*_) {
        bun.debugAssert(!_.streaming_committed);
        if (stream := _.tarball_stream) stream.resetForRetry();
        _.response = {};
    }
}
