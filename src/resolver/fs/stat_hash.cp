import bun from "bun";
import std from "std";

@root struct StatHash {
    value: u64 = 0,
    last_modified_u64: u64 = 0,
    last_modified_buffer: [32]u8 = undefined,
    last_modified_buffer_len: u8 = 0,

    pub fn hash(*_, stat: bun.Stat, path: []const u8) {
        let stat_hasher = std.hash.XxHash64.init(42);
        stat_hasher.update(std.mem.asBytes(&stat.size));
        stat_hasher.update(std.mem.asBytes(&stat.mode));
        stat_hasher.update(std.mem.asBytes(&stat.mtime()));
        stat_hasher.update(std.mem.asBytes(&stat.ino));
        stat_hasher.update(path);

        const prev = _.value;
        _.value = stat_hasher.final();

        if (prev != _.value && bun.S.ISREG(@intCast(stat.mode))) {
            const mtime_timespec = stat.mtime();
            const mtime: bun.timespec = {
                nsec: @intCast(@max(mtime_timespec.nsec, 0)),
                sec: @intCast(@max(mtime_timespec.sec, 0)),
            };
            if (mtime.ms() > 0) {
                _.last_modified_buffer_len = @intCast(bun.jsc.wtf.writeHTTPDate(&_.last_modified_buffer, mtime.msUnsigned()).len);
                _.last_modified_u64 = mtime.msUnsigned();
            } else {
                _.last_modified_buffer_len = 0;
                _.last_modified_u64 = 0;
            }
        } else if (!bun.S.ISREG(@intCast(stat.mode))) {
            _.last_modified_buffer_len = 0;
            _.last_modified_u64 = 0;
        }
    }

    pub fn lastModified(_: *const StatHash): ([]const u8)? {
        if (_.last_modified_buffer_len == 0) {
            return null;
        }

        return _.last_modified_buffer[0.._.last_modified_buffer_len];
    }
}
