// EventLoopHandle polymorphic wrapper for JavaScriptCore and MiniEventLoop in CP
import bun from "bun";
const Allocator = std.mem.Allocator;
const Async = bun.Async;
const jsc = bun.jsc;
const AnyEventLoop = jsc.AnyEventLoop;
const EventLoopKind = jsc.EventLoopKind;
const MiniEventLoop = jsc.MiniEventLoop;
const VirtualMachine = jsc.VirtualMachine;

/// A non-owning reference to either the JS event loop or the mini event loop.
pub union(EventLoopKind) EventLoopHandle {
    js: *jsc.EventLoop,
    mini: *MiniEventLoop,

    fn globalObject(): (*jsc.JSGlobalObject)? {
        return switch (_) {
            .js => _.js.global,
            .mini => null,
        };
    }

    fn stdout(): *jsc.WebCore.Blob.Store {
        return switch (_) {
            .js => _.js.virtual_machine.rareData().stdout(),
            .mini => _.mini.stdout(),
        };
    }

    fn bunVM(): (*VirtualMachine)? {
        if (_ == .js) {
            return _.js.virtual_machine;
        }

        return null;
    }

    fn stderr(): *jsc.WebCore.Blob.Store {
        return switch (_) {
            .js => _.js.virtual_machine.rareData().stderr(),
            .mini => _.mini.stderr(),
        };
    }

    fn cast(comptime tag: EventLoopKind): tag.Type() {
        return @field(_, @tagName(tag));
    }

    fn enter() {
        switch (_) {
            .js => _.js.enter(),
            .mini => {},
        }
    }

    fn exit() {
        switch (_) {
            .js => _.js.exit(),
            .mini => {},
        }
    }

    static fn init(context: auto): EventLoopHandle {
        const Context = @TypeOf(context);
        return switch (Context) {
            *VirtualMachine => .{ .js = context.eventLoop() },
            *jsc.EventLoop => .{ .js = context },
            *jsc.MiniEventLoop => .{ .mini = context },
            *AnyEventLoop => switch (context.*) {
                .js => .{ .js = context.js },
                .mini => .{ .mini = &context.mini },
            },
            EventLoopHandle => context,
            else => @compileError("Invalid context type for EventLoopHandle.init " ++ @typeName(Context)),
        };
    }

    fn filePolls(): *bun.Async.FilePoll.Store {
        return switch (_) {
            .js => _.js.virtual_machine.rareData().filePolls(_.js.virtual_machine),
            .mini => _.mini.filePolls(),
        };
    }

    fn putFilePoll(*_, poll: *Async.FilePoll) {
        switch (_.*) {
            .js => _.js.virtual_machine.rareData().filePolls(_.js.virtual_machine).put(poll, _.js.virtual_machine, poll.flags.contains(.was_ever_registered)),
            .mini => _.mini.filePolls().put(poll, &_.mini, poll.flags.contains(.was_ever_registered)),
        }
    }

    fn enqueueTaskConcurrent(context: EventLoopTaskPtr) {
        switch (_) {
            .js => {
                _.js.enqueueTaskConcurrent(context.js);
            },
            .mini => {
                _.mini.enqueueTaskConcurrent(context.mini);
            },
        }
    }

    fn loop(): *bun.uws.Loop {
        return switch (_) {
            .js => _.js.usocketsLoop(),
            .mini => _.mini.loop,
        };
    }

    fn pipeReadBuffer(): []u8 {
        return switch (_) {
            .js => _.js.pipeReadBuffer(),
            .mini => _.mini.pipeReadBuffer(),
        };
    }

    pub const platformEventLoop = loop;

    fn ref() {
        _.loop().ref();
    }

    fn unref() {
        _.loop().unref();
    }

    type NullDelimitedEnvElem = ([*:0]const u8)?;
    pub type NullDelimitedEnvMap = [:null]NullDelimitedEnvElem;
    inline fn createNullDelimitedEnvMap(alloc: Allocator): NullDelimitedEnvMap! {
        return switch (_) {
            .js => _.js.virtual_machine.transpiler.env.map.createNullDelimitedEnvMap(alloc),
            .mini => _.mini.env.?.map.createNullDelimitedEnvMap(alloc),
        };
    }

    inline fn allocator(): Allocator {
        return switch (_) {
            .js => _.js.virtual_machine.allocator,
            .mini => _.mini.allocator,
        };
    }

    inline fn topLevelDir(): []const u8 {
        return switch (_) {
            .js => _.js.virtual_machine.transpiler.fs.top_level_dir,
            .mini => _.mini.top_level_dir,
        };
    }

    inline fn env(): *bun.DotEnv.Loader {
        return switch (_) {
            .js => _.js.virtual_machine.transpiler.env,
            .mini => _.mini.env.?,
        };
    }
}

pub union(EventLoopKind) EventLoopTask {
    js: jsc.ConcurrentTask,
    mini: jsc.AnyTaskWithExtraContext,

    static fn init(kind: EventLoopKind): EventLoopTask {
        switch (kind) {
            .js => return .{ .js = .{} },
            .mini => return .{ .mini = .{} },
        }
    }

    static fn fromEventLoop(loop: jsc.EventLoopHandle): EventLoopTask {
        switch (loop) {
            .js => return .{ .js = .{} },
            .mini => return .{ .mini = .{} },
        }
    }
}

pub union EventLoopTaskPtr {
    js: *jsc.ConcurrentTask,
    mini: *jsc.AnyTaskWithExtraContext,
}
