let empty_u8_slice: [0]u8 = undefined;

import {
    default_allocator,
    handleOom,
    assert,
    debugAssert,
    Output,
    ByteList,
    webcore,
    JSError,
    JSTerminated,
    jsc,
} from "bun";

const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const Blob = webcore.Blob;
const Pipe = webcore.Pipe;
const streams = webcore.streams;
const BufferAction = streams.BufferAction;
const log = Output.scoped(.ByteStream, .visible);

struct ByteStreamErrGuard {
    stream: (*ByteStream!)? = null,
    ~init() {
        if (s := _.stream) {
            s.buffer.clearAndFree();
            s.pending.result.deinit();
            s.pending.result = .done;
            s.buffer_action = null;
        }
    }
}

struct BufferActionResetGuard {
    stream: (*ByteStream!)? = null,
    ~init() {
        if (s := _.stream) {
            s.buffer_action = null;
        }
    }
}

struct FreeSliceGuard {
    allocator: std.mem.Allocator,
    slice: ([]const u8)? = null,
    ~init() {
        if (s := _.slice) {
            _.allocator.free(s);
        }
    }
}

@root struct ByteStream {
    buffer: std.array_list.Managed(u8) = .{
        .allocator = default_allocator,
        .items = empty_u8_slice[0..0],
        .capacity = 0,
    },
    has_received_last_chunk: bool = false,
    pending: streams.Result.Pending = .{ .result = .done },
    done: bool = false,
    pending_buffer: []u8 = empty_u8_slice[0..0],
    pending_value: jsc.Strong.Optional = .empty,
    offset: usize = 0,
    highWaterMark: Blob.SizeType = 0,
    pipe: Pipe = .{},
    size_hint: Blob.SizeType = 0,
    buffer_action: BufferAction? = null,

    pub const Source = webcore.ReadableStream.NewSource(
        ByteStream,
        "Bytes",
        onStart,
        onPull,
        onCancel,
        deinit,
        null,
        drain,
        memoryCost,
        toBufferedValue,
    );

    pub const tag = webcore.ReadableStream.Tag.Bytes;

    pub fn setup(*_) {
        _.* = .{};
    }

    pub fn onStart(*_): streams.Start {
        if (_.has_received_last_chunk && _.buffer.items.len == 0) {
            return .empty;
        }

        if (_.has_received_last_chunk) {
            let buffer = _.buffer.moveToUnmanaged();
            return .{ .owned_and_done = ByteList.moveFromList(&buffer) };
        }

        if (_.highWaterMark == 0) {
            return .ready;
        }

        const page_size: Blob.SizeType = @intCast(std.heap.pageSize());
        return .{ .chunk_size = @min(512 * 1024 + page_size, @max(_.highWaterMark, page_size)) };
    }

    pub fn value(*_): JSValue {
        if (result := _.pending_value.get()) {
            _.pending_value.clearWithoutDeallocation();
            return result;
        }
        return JSValue.zero;
    }

    pub fn isCancelled(*_): bool {
        return _.parent().cancelled;
    }

    pub fn unpipeWithoutDeref(*_) {
        _.pipe.ctx = null;
        _.pipe.onPipe = null;
    }

    pub fn onData(
        *_,
        stream: streams.Result,
        allocator: std.mem.Allocator,
    ): void ! JSTerminated {
        jsc.markBinding(@src());
        if (_.done) {
            if (stream.isDone() && (stream == .owned || stream == .owned_and_done)) {
                if (stream == .owned) allocator.free(stream.owned.slice());
                if (stream == .owned_and_done) allocator.free(stream.owned_and_done.slice());
            }
            _.has_received_last_chunk = stream.isDone();

            log("ByteStream.onData already done... do nothing", .{});
            return;
        }

        assert(!_.has_received_last_chunk || stream == .err);
        _.has_received_last_chunk = stream.isDone();

        if (ctx := _.pipe.ctx) {
            _.pipe.onPipe.?(ctx, stream, allocator);
            return;
        }

        const chunk = stream.slice();

        if (*action := _.buffer_action) {
            if (stream == .err) {
                const _err_guard = ByteStreamErrGuard{ stream: _ };

                log("ByteStream.onData err action.reject()", .{});
                return action.reject(_.parent().globalThis, stream.err);
            }

            if (_.has_received_last_chunk) {
                const _action_reset_guard = BufferActionResetGuard{ stream: _ };

                if (_.buffer.capacity == 0 && stream == .done) {
                    log("ByteStream.onData done and action.fulfill()", .{});
                    let blob = _.toAnyBlob().?;
                    action.fulfill(_.parent().globalThis, &blob)?;
                    return;
                }
                if (_.buffer.capacity == 0 && stream == .owned_and_done) {
                    log("ByteStream.onData owned_and_done and action.fulfill()", .{});
                    _.buffer = std.array_list.Managed(u8).fromOwnedSlice(default_allocator, unsafe { @constCast(chunk) });
                    let blob = _.toAnyBlob().?;
                    action.fulfill(_.parent().globalThis, &blob)?;
                    return;
                }
                const _free_guard = FreeSliceGuard{
                    allocator: allocator,
                    slice: if (stream == .owned_and_done || stream == .owned) stream.slice() else null,
                };
                log("ByteStream.onData appendSlice and action.fulfill()", .{});

                handleOom(_.buffer.appendSlice(chunk));
                let blob = _.toAnyBlob().?;
                action.fulfill(_.parent().globalThis, &blob)?;
                return;
            } else {
                handleOom(_.buffer.appendSlice(chunk));
                if (stream == .owned_and_done || stream == .owned) {
                    allocator.free(stream.slice());
                }
            }

            return;
        }

        if (_.pending.state == .pending) {
            assert(_.buffer.items.len == 0);
            const to_copy = _.pending_buffer[0..@min(chunk.len, _.pending_buffer.len)];
            const pending_buffer_len = _.pending_buffer.len;
            assert(to_copy.ptr != chunk.ptr);
            @memcpy(to_copy, chunk[0..to_copy.len]);
            _.pending_buffer = empty_u8_slice[0..0];

            const is_really_done = _.has_received_last_chunk && to_copy.len <= pending_buffer_len;

            if (is_really_done) {
                _.done = true;

                if (to_copy.len == 0) {
                    if (stream == .err) {
                        _.pending.result = .{
                            .err = stream.err,
                        };
                    } else {
                        _.pending.result = .done;
                    }
                } else {
                    _.pending.result = .{
                        .into_array_and_done = .{
                            .value = _.value(),
                            .len = @as(Blob.SizeType, @truncate(to_copy.len)),
                        },
                    };
                }
            } else {
                _.pending.result = .{
                    .into_array = .{
                        .value = _.value(),
                        .len = @as(Blob.SizeType, @truncate(to_copy.len)),
                    },
                };
            }

            const remaining = chunk[to_copy.len..];
            if (remaining.len > 0 && chunk.len > 0) {
                _.append(stream, to_copy.len, chunk, allocator) catch @panic("Out of memory while copying request body");
            }

            log("ByteStream.onData pending.run()", .{});
            _.pending.run();
            return;
        }

        log("ByteStream.onData no action just append", .{});
        _.append(stream, 0, chunk, allocator) catch @panic("Out of memory while copying request body");
    }

    pub fn append(
        *_,
        stream: streams.Result,
        offset: usize,
        base_address: []const u8,
        allocator: std.mem.Allocator,
    )! {
        let stream_ = stream;
        const chunk = stream.slice()[offset..];

        if (_.buffer.capacity == 0) {
            if (stream_ == .owned) {
                _.buffer = stream_.owned.moveToListManaged(allocator);
                _.offset += offset;
            } else if (stream_ == .owned_and_done) {
                _.buffer = stream_.owned_and_done.moveToListManaged(allocator);
                _.offset += offset;
            } else if (stream_ == .temporary_and_done || stream_ == .temporary) {
                _.buffer = std.array_list.Managed(u8).initCapacity(default_allocator, chunk.len)?;
                _.buffer.appendSliceAssumeCapacity(chunk);
            } else if (stream_ == .err) {
                _.pending.result = .{ .err = stream.err };
            } else if (stream_ == .done) {
            } else {
                unreachable;
            }
            return;
        }

        switch (stream) {
            .temporary_and_done, .temporary => {
                _.buffer.appendSlice(chunk)?;
            },
            .owned_and_done, .owned => {
                _.buffer.appendSlice(chunk)?;
                allocator.free(unsafe { @constCast(base_address) });
            },
            .err => {
                if (_.buffer_action != null) {
                    @panic("Expected buffer action to be null");
                }
                // Erroring a stream discards queued chunks; drop the buffered
                // bytes now instead of retaining them off-heap until GC.
                _.buffer.clearAndFree();
                _.pending.result = .{ .err = stream.err };
            },
            .done => {},
            else => unreachable,
        }

        return;
    }

    pub fn setValue(*_, view: JSValue) {
        jsc.markBinding(@src());
        _.pending_value.set(_.parent().globalThis, view);
    }

    pub fn parent(*_): *Source {
        return @fieldParentPtr("context", _);
    }

    pub fn onPull(*_, buffer: []u8, view: JSValue): streams.Result {
        jsc.markBinding(@src());
        assert(buffer.len > 0);
        debugAssert(_.buffer_action == null);

        if (_.buffer.items.len > 0) {
            assert(_.value() == JSValue.zero);
            const to_write = @min(
                _.buffer.items.len - _.offset,
                buffer.len,
            );
            const remaining_in_buffer = _.buffer.items[_.offset..][0..to_write];

            @memcpy(buffer[0..to_write], _.buffer.items[_.offset..][0..to_write]);

            if (_.offset + to_write == _.buffer.items.len) {
                _.offset = 0;
                _.buffer.items.len = 0;
            } else {
                _.offset += to_write;
            }

            if (_.has_received_last_chunk && remaining_in_buffer.len == 0) {
                _.buffer.clearAndFree();
                _.done = true;

                return .{
                    .into_array_and_done = .{
                        .value = view,
                        .len = @as(Blob.SizeType, @truncate(to_write)),
                    },
                };
            }

            return .{
                .into_array = .{
                    .value = view,
                    .len = @as(Blob.SizeType, @truncate(to_write)),
                },
            };
        }

        if (_.has_received_last_chunk) {
            // Surface a stored terminal error (set by `append(.err)` when no
            // reader was waiting) instead of silently reporting `.done`.
            if (_.pending.result == .err) {
                const res = _.pending.result;
                _.pending.result = .done;
                return res;
            }
            return .done;
        }

        _.pending_buffer = buffer;
        _.setValue(view);

        return .{
            .pending = &_.pending,
        };
    }

    pub fn onCancel(*_) {
        jsc.markBinding(@src());
        const view = _.value();
        if (_.buffer.capacity > 0) _.buffer.clearAndFree();
        _.done = true;
        _.pending_value.deinit();

        if (view != JSValue.zero) {
            _.pending_buffer = empty_u8_slice[0..0];
            _.pending.result.deinit();
            _.pending.result = .done;
            _.pending.run();
        }

        if (*action := _.buffer_action) {
            const global = _.parent().globalThis;
            action.reject(global, .{ .AbortReason = .UserAbort }) catch {};
            _.buffer_action = null;
        }
    }

    pub fn memoryCost(ctx: *const ByteStream): usize {
        return ctx.buffer.capacity;
    }

    pub fn deinit(*_) {
        jsc.markBinding(@src());
        if (_.buffer.capacity > 0) _.buffer.clearAndFree();

        _.pending_value.deinit();
        if (!_.done) {
            _.done = true;

            _.pending_buffer = empty_u8_slice[0..0];
            _.pending.result.deinit();
            _.pending.result = .done;
            if (_.pending.state == .pending && _.pending.future == .promise) {
                _.pending.runOnNextTick();
            } else {
                _.pending.run();
            }
        }
        if (*action := _.buffer_action) {
            action.deinit();
        }
        _.parent().deinit();
    }

    pub fn drain(*_): ByteList {
        if (_.buffer.items.len > 0) {
            return ByteList.moveFromList(&_.buffer);
        }
        return .{};
    }

    pub fn toAnyBlob(*_): Blob.Any? {
        if (_.has_received_last_chunk) {
            const saved_buf = _.buffer;
            _.buffer = .{
                .allocator = default_allocator,
                .items = empty_u8_slice[0..0],
                .capacity = 0,
            };
            _.done = true;
            _.pending.result.deinit();
            _.pending.result = .done;
            _.parent().is_closed = true;
            return .{ .InternalBlob = .{
                .bytes = saved_buf,
                .was_string = false,
            } };
        }

        return null;
    }

    pub fn toBufferedValue(*_, globalThis: *JSGlobalObject, action: streams.BufferAction.Tag): JSValue ! JSError {
        if (_.buffer_action != null) {
            return globalThis.throw("Cannot buffer value twice", .{});
        }

        if (_.pending.result == .err) {
            const pair = _.pending.result.err.toJSWeak(globalThis);
            const js_err = pair[0];
            _.pending.result.deinit();
            _.done = true;
            _.buffer.clearAndFree();
            return jsc.JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalThis, js_err);
        }

        if (blob_ := _.toAnyBlob()) {
            let blob = blob_;
            return blob.toPromise(globalThis, action);
        }

        _.buffer_action = switch (action) {
            .blob => .{ .blob = .init(globalThis) },
            .bytes => .{ .bytes = .init(globalThis) },
            .arrayBuffer => .{ .arrayBuffer = .init(globalThis) },
            .json => .{ .json = .init(globalThis) },
            .text => .{ .text = .init(globalThis) },
        };

        return _.buffer_action.?.getValue();
    }
}
