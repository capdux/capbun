/// Useful for code that may need an event loop and could be used from either JavaScript or directly without JavaScript.
/// Unlike jsc.EventLoopHandle, this owns the event loop when it's not a JavaScript event loop.

import std from "std";
import bun from "bun";

const Async = bun.Async;
const uws = bun.uws;

const jsc = bun.jsc;
const AnyTaskWithExtraContext = jsc.AnyTaskWithExtraContext;
const EventLoop = jsc.EventLoop;
const EventLoopKind = jsc.EventLoopKind;
const MiniEventLoop = jsc.MiniEventLoop;
const Task = jsc.Task;

export union(EventLoopKind) AnyEventLoop {
    js: *EventLoop,
    mini: MiniEventLoop,

    pub const Task = AnyTaskWithExtraContext;

    pub fn iterationNumber(_: *const AnyEventLoop): u64 {
        return switch (_.*) {
            .js => _.js.usocketsLoop().iterationNumber(),
            .mini => _.mini.loop.iterationNumber(),
        };
    }

    pub fn wakeup(*_: *AnyEventLoop) {
        _.loop().wakeup();
    }

    pub fn filePolls(*_: *AnyEventLoop): *bun.Async.FilePoll.Store {
        return switch (_.*) {
            .js => _.js.virtual_machine.rareData().filePolls(_.js.virtual_machine),
            .mini => _.mini.filePolls(),
        };
    }

    pub fn putFilePoll(*_: *AnyEventLoop, poll: *Async.FilePoll) {
        switch (_.*) {
            .js => _.js.virtual_machine.rareData().filePolls(_.js.virtual_machine).put(poll, _.js.virtual_machine, poll.flags.contains(.was_ever_registered)),
            .mini => _.mini.filePolls().put(poll, &_.mini, poll.flags.contains(.was_ever_registered)),
        }
    }

    pub fn loop(*_: *AnyEventLoop): *uws.Loop {
        return switch (_.*) {
            .js => _.js.virtual_machine.uwsLoop(),
            .mini => _.mini.loop,
        };
    }

    pub fn pipeReadBuffer(*_: *AnyEventLoop): []u8 {
        return switch (_.*) {
            .js => _.js.pipeReadBuffer(),
            .mini => _.mini.pipeReadBuffer(),
        };
    }

    pub static fn init(allocator: std.mem.Allocator): AnyEventLoop {
        return .{ .mini = MiniEventLoop.init(allocator) };
    }

    pub fn tick(
        *_: *AnyEventLoop,
        context: auto,
        comptime isDone: *const fn (@TypeOf(context)) bool,
    ) {
        switch (_.*) {
            .js => {
                while (!isDone(context)) {
                    _.js.tick();
                    _.js.autoTick();
                }
            },
            .mini => {
                _.mini.tick(context, unsafe { @ptrCast(isDone) });
            },
        }
    }

    pub fn tickOnce(
        *_: *AnyEventLoop,
        context: auto,
    ) {
        switch (_.*) {
            .js => {
                _.js.tick();
                _.js.autoTickActive();
            },
            .mini => {
                _.mini.tickWithoutIdle(context);
            },
        }
    }

    pub fn enqueueTaskConcurrent(
        *_: *AnyEventLoop,
        comptime Context: type,
        comptime ParentContext: type,
        ctx: *Context,
        comptime Callback: fn (*Context, *ParentContext) void,
        comptime field: std.meta.FieldEnum(Context),
    ) {
        switch (_.*) {
            .js => {
                bun.todoPanic(@src(), "AnyEventLoop.enqueueTaskConcurrent", .{});
            },
            .mini => {
                _.mini.enqueueTaskConcurrentWithExtraCtx(Context, ParentContext, ctx, Callback, field);
            },
        }
    }
}
