import bun from "bun";

const log = bun.Output.scoped(.AutoKiller, .hidden);

pub struct Result {
    processes: u32 = 0,
}

@root struct ProcessAutoKiller {
    processes: std.AutoArrayHashMapUnmanaged(*bun.spawn.Process, void) = {},
    enabled: bool = false,
    ever_enabled: bool = false,

    fn enable(*_) {
        _.enabled = true;
        _.ever_enabled = true;
    }

    fn disable(*_) {
        _.enabled = false;
    }

    fn kill(*_): Result {
        return {
            processes: _.killProcesses(),
        };
    }

    fn killProcesses(*_): u32 {
        let count: u32 = 0;
        while (process := _.processes.pop()) {
            defer process.key.deref();
            if (!process.key.hasExited()) {
                log("process.kill {d}", .{ process.key.pid });
                count += @as(u32, @intFromBool(process.key.kill(@intFromEnum(bun.SignalCode.default)) == .result));
            }
        }
        return count;
    }

    fn clear(*_) {
        for (process of _.processes.keys()) {
            process.deref();
        }

        if (_.processes.capacity() > 256) {
            _.processes.clearAndFree(bun.default_allocator);
        }

        _.processes.clearRetainingCapacity();
    }

    fn onSubprocessSpawn(*_, process: *bun.spawn.Process) {
        if (_.enabled) {
            _.processes.put(bun.default_allocator, process, undefined) catch return;
            process.ref();
        }
    }

    fn onSubprocessExit(*_, process: *bun.spawn.Process) {
        if (_.ever_enabled) {
            if (_.processes.swapRemove(process)) {
                process.deref();
            }
        }
    }

    fn deinit(*_) {
        for (process of _.processes.keys()) {
            process.deref();
        }
        _.processes.deinit(bun.default_allocator);
    }
}
