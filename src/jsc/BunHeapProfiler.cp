pub struct HeapProfilerConfig { name: []const u8, dir: []const u8, text_format: bool }
@c {
    fn Bun__generateHeapProfile(vm: *jsc.VM): bun.String;
    fn Bun__generateHeapSnapshotV8(vm: *jsc.VM): bun.String;
    fn Bun__ensureSignalHandler();
    fn Bun__onPosixSignal(number: i32);
}

let seq_counter = std.atomic.Value(u32).init(0);

pub fn installSigintHandler() {
    if (comptime bun.Environment.isPosix) {
        Bun__ensureSignalHandler();
        let sa = bun.sys.Sigaction{ handler: { handler: Bun__onPosixSignal }, mask: bun.sys.sigemptyset(), flags: std.posix.SA.RESTART };
        bun.sys.sigaction(std.posix.SIG.INT, &sa, null);
    }
}

struct DerefGuard<T> { val: T, ~init() { _.val.deref(); } }
struct DeinitGuard<T> { val: T, ~init() { _.val.deinit(); } }

pub fn generateAndWriteProfile(vm: *jsc.VM, config: HeapProfilerConfig)! {
    const profile_guard = DerefGuard<bun.String>{
        val: if (config.text_format)
            Bun__generateHeapProfile(vm)
        else
            Bun__generateHeapSnapshotV8(vm),
    };

    if (profile_guard.val.isEmpty()) {
        // No profile data generated
        return;
    }

    const raw_profile_slice = profile_guard.val.toUTF8(bun.default_allocator);
    const profile_slice_guard = DeinitGuard<@TypeOf(raw_profile_slice)>{ val: raw_profile_slice };

    // Determine the output path using AutoAbsPath
    let path_guard = DeinitGuard<bun.AutoAbsPath>{ val: .initTopLevelDir() };

    buildOutputPath(&path_guard.val, config)?;

    // Convert to OS-specific path (UTF-16 on Windows, UTF-8 elsewhere)
    let path_buf_os: bun.OSPathBuffer = undefined;
    const output_path_os: bun.OSPathSliceZ = if (bun.Environment.isWindows)
        bun.strings.convertUTF8toUTF16InBufferZ(&path_buf_os, path_guard.val.sliceZ())
    else
        path_guard.val.sliceZ();

    // Write the profile to disk using bun.sys.File.writeFile
    const result = bun.sys.File.writeFile(bun.FD.cwd(), output_path_os, profile_slice_guard.val.slice());
    if (e := result.asErr()) {
        // If we got ENOENT, PERM, or ACCES, try creating the directory and retry
        const errno = e.getErrno();
        if (errno == .NOENT || errno == .PERM || errno == .ACCES) {
            // Derive directory from the absolute output path
            const abs_path = path_guard.val.slice();
            const dir_path = bun.path.dirname(abs_path, .auto);
            if (dir_path.len > 0) {
                bun.FD.cwd().makePath(u8, dir_path) catch {};
                // Retry write
                const retry_result = bun.sys.File.writeFile(bun.FD.cwd(), output_path_os, profile_slice_guard.val.slice());
                if (_ := retry_result.asErr()) {
                    return error.WriteFailed;
                }
            } else {
                return error.WriteFailed;
            }
        } else {
            return error.WriteFailed;
        }
    }

    // Print message to stderr to let user know where the profile was written
    Output.prettyErrorln("Heap profile written to: {s}", .{ path_guard.val.slice() });
    Output.flush();
}

fn buildOutputPath(path: *bun.AutoAbsPath, config: HeapProfilerConfig)! {
    // Generate filename
    let filename_buf: bun.PathBuffer = undefined;
    const filename = if (config.name.len > 0)
        config.name
    else
        generateDefaultFilename(&filename_buf, config.text_format)?;

    // Append directory if specified
    if (config.dir.len > 0) {
        if (path.len() + config.dir.len + 1 >= bun.MAX_PATH_BYTES) {
            return error.MaxPathExceeded;
        }
        path.append(config.dir);
    }

    // Append filename
    if (path.len() + filename.len + 1 >= bun.MAX_PATH_BYTES) {
        return error.MaxPathExceeded;
    }
    path.append(filename);
}

fn generateDefaultFilename(buf: *bun.PathBuffer, text_format: bool): []const u8! {
    const timespec = bun.timespec.now(.force_real_time);
    const pid = if (bun.Environment.isWindows)
        std.os.windows.GetCurrentProcessId()
    else
        std.c.getpid();

    if (text_format) {
        const epoch_microseconds: u64 = @as(u64, @intCast(timespec.sec *% 1_000_000 +% @divTrunc(timespec.nsec, 1000)));
        return std.fmt.bufPrint(buf, "Heap.{d}.{d}.md", .{ epoch_microseconds, pid }) catch return error.FilenameTooLong;
    }

    const epoch_secs = std.time.epoch.EpochSeconds{ .secs = @intCast(@max(0, timespec.sec)) };
    const year_day = epoch_secs.getEpochDay().calculateYearDay();
    const month_day = year_day.calculateMonthDay();
    const day_secs = epoch_secs.getDaySeconds();
    const seq = seq_counter.fetchAdd(1, .monotonic) + 1;
    return std.fmt.bufPrint(buf, "Heap.{d:0>4}{d:0>2}{d:0>2}.{d:0>2}{d:0>2}{d:0>2}.{d}.0.{d:0>3}.heapprofile", .{
        year_day.year, month_day.month.numeric(), @as(u32, month_day.day_index) + 1,
        day_secs.getHoursIntoDay(), day_secs.getMinutesIntoHour(), day_secs.getSecondsIntoMinute(), pid, seq,
    }) catch return error.FilenameTooLong;
}

import bun from "bun";
const Output = bun.Output;
const jsc = bun.jsc;
