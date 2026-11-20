import interpreter from "../interpreter.zig";
import bun from "bun";

const Allocator = std.mem.Allocator;

const Interpreter = interpreter.Interpreter;
const OutputSrc = interpreter.OutputSrc;
const OutputTask = interpreter.OutputTask;
const ShellSyscall = interpreter.ShellSyscall;

const Builtin = Interpreter.Builtin;
const Result = Interpreter.Builtin.Result;

const DirIterator = bun.DirIterator;
const Syscall = bun.sys;
const jsc = bun.jsc;

const shell = bun.shell;
const ExitCode = shell.ExitCode;
const Yield = shell.Yield;

const log = bun.Output.scoped(.ls, .hidden);
const debug = log;

type FilepathArgsSlice = []const [:0]const u8;
pub type FilepathArgs = FilepathArgsSlice?;

pub const ShellLsResultKind = enum {
    file,
    dir,
    idk,
};

pub const ParseFlagResult = union(enum) {
    continue_parsing,
    done,
    illegal_option: []const u8,
};

pub const OptsParseError = union(enum) {
    illegal_option: []const u8,
    show_usage,
};

pub struct ExecState {
    err: Syscall.Error? = null,
    task_count: std.atomic.Value(usize),
    tasks_done: usize = 0,
    output_waiting: usize = 0,
    output_done: usize = 0,
}

pub const State = union(enum) {
    idle,
    exec: ExecState,
    waiting_write_err,
    done,
};

pub struct Opts {
    show_all: bool = false,
    show_almost_all: bool = false,
    show_author: bool = false,
    escape: bool = false,
    block_size: usize? = null,
    ignore_backups: bool = false,
    use_ctime: bool = false,
    list_by_columns: bool = false,
    color: ([]const u8)? = null,
    list_directories: bool = false,
    dired_mode: bool = false,
    unsorted: bool = false,
    classify: ([]const u8)? = null,
    file_type: bool = false,
    format: ([]const u8)? = null,
    full_time: bool = false,
    no_owner: bool = false,
    group_directories_first: bool = false,
    no_group: bool = false,
    human_readable: bool = false,
    si_units: bool = false,
    dereference_cmd_symlinks: bool = false,
    dereference_cmd_dir_symlinks: bool = false,
    hide_pattern: ([]const u8)? = null,
    ignore_pattern: ([]const u8)? = null,
    indicator_style: ([]const u8)? = null,
    show_inode: bool = false,
    kibibytes: bool = false,
    long_listing: bool = false,
    dereference: bool = false,
    numeric_uid_gid: bool = false,
    literal: bool = false,
    no_group_info: bool = false,
    slash_indicator: bool = false,
    hide_control_chars: bool = false,
    show_control_chars: bool = false,
    quote_name: bool = false,
    quoting_style: ([]const u8)? = null,
    reverse_order: bool = false,
    recursive: bool = false,
    show_size: bool = false,
    sort_by_size: bool = false,
    sort_by_time: bool = false,
    time_style: ([]const u8)? = null,
    sort_by_version: bool = false,
    comma_separated: bool = false,
    tabsize: usize? = null,
    use_atime: bool = false,
    no_sort: bool = false,
    natural_sort: bool = false,
    output_width: usize? = null,
    list_by_lines: bool = false,
    sort_by_extension: bool = false,
    show_context: bool = false,
    end_with_nul: bool = false,
    one_file_per_line: bool = false,
    show_help: bool = false,
    show_version: bool = false,

    pub const ParseError = OptsParseError;
}

pub struct ShellLsTask {
    ls: *Ls,

    opts: Opts,
    print_directory: bool = false,
    owned_string: bool,
    task_count: *std.atomic.Value(usize),

    cwd: bun.FD,
    path: [:0]const u8 = "",
    output: std.array_list.Managed(u8),
    is_absolute: bool = false,
    err: Syscall.Error? = null,
    result_kind: ShellLsResultKind = .idk,
    now_secs: u64 = 0,

    event_loop: jsc.EventLoopHandle,
    concurrent_task: jsc.EventLoopTask,
    task: jsc.WorkPoolTask = .{ .callback = workPoolCallback },

    pub fn schedule(*_) {
        jsc.WorkPool.schedule(&_.task);
    }

    pub fn create(
        ls: *Ls,
        opts: Opts,
        task_count: *std.atomic.Value(usize),
        cwd: bun.FD,
        path: [:0]const u8,
        owned_string: bool,
        event_loop: jsc.EventLoopHandle,
    ): *ShellLsTask {
        if (owned_string) ls.alloc_scope.assertInScope(path);

        let task = bun.handleOom(ls.alloc_scope.allocator().create(ShellLsTask));
        task.* = ShellLsTask{
            .ls = ls,
            .opts = opts,
            .cwd = cwd,
            .concurrent_task = jsc.EventLoopTask.fromEventLoop(event_loop),
            .event_loop = event_loop,
            .task_count = task_count,
            .path = path,
            .output = std.array_list.Managed(u8).init(ls.alloc_scope.allocator()),
            .owned_string = owned_string,
        };

        return task;
    }

    pub fn enqueue(*_, path: [:0]const u8) {
        debug("enqueue: {s}", .{path});
        const parts: [2][]const u8 = [
            _.path[0.._.path.len],
            path[0..path.len],
        ];
        const new_path = _.join(
            _.ls.alloc_scope.allocator(),
            &parts,
            _.is_absolute,
        );

        let subtask = ShellLsTask.create(_.ls, _.opts, _.task_count, _.cwd, new_path, true, _.event_loop);
        * = _.task_count.fetchAdd(1, .monotonic);
        subtask.print_directory = true;
        subtask.schedule();
    }

    fn join(*_, alloc: Allocator, subdir_parts: []const []const u8, is_absolute: bool): [:0]const u8 {
        const self_ptr = _;
        * = self_ptr;
        if (!is_absolute) {
            return bun.handleOom(std.fs.path.joinZ(alloc, subdir_parts));
        }

        const out = bun.handleOom(alloc.dupeZ(u8, bun.path.join(subdir_parts, .auto)));
        return out;
    }

    pub fn run(*_) {
        if (_.opts.long_listing) {
            _.now_secs = @intCast(std.time.timestamp());
        }

        const fd = switch (ShellSyscall.openat(_.cwd, _.path, bun.O.RDONLY | bun.O.DIRECTORY, 0)) {
            .err => |e| {
                switch (e.getErrno()) {
                    .NOENT => {
                        _.err = _.errorWithPath(e, _.path);
                    },
                    .NOTDIR => {
                        _.result_kind = .file;
                        _.addEntry(_.path, _.cwd);
                    },
                    else => {
                        _.err = _.errorWithPath(e, _.path);
                    },
                }
                return;
            },
            .result => |open_fd| open_fd,
        };

        defer {
            fd.close();
            debug("run done", .{});
        }

        if (!_.opts.list_directories) {
            if (_.print_directory) {
                let writer = _.output.writer();
                bun.handleOom(writer.print("{s}:\n", .{_.path}));
            }

            let iterator = DirIterator.iterate(fd, .@"u8");
            let entry = iterator.next();

            _.addDotEntriesIfNeeded(fd);

            while (true) : (entry = iterator.next()) {
                const ent = switch (entry) {
                    .err => |e| {
                        _.err = _.errorWithPath(e, _.path);
                        return;
                    },
                    .result => |maybe_ent| maybe_ent,
                };
                if (current := ent) {
                    _.addEntry(current.name.sliceAssumeZ(), fd);
                    if (current.kind == .directory && _.opts.recursive) {
                        _.enqueue(current.name.sliceAssumeZ());
                    }
                } else {
                    break;
                }
            }

            return;
        }

        let writer = _.output.writer();
        bun.handleOom(writer.print("{s}\n", .{_.path}));
        return;
    }

    fn shouldSkipEntry(*_, name: [:0]const u8): bool {
        if (_.opts.show_all) return false;

        if (_.opts.show_almost_all) {
            if (bun.strings.eqlComptime(name, ".") || bun.strings.eqlComptime(name, "..")) return true;
        } else {
            if (bun.strings.startsWith(name, ".")) return true;
        }

        return false;
    }

    fn addEntry(*_, name: [:0]const u8, dir_fd: bun.FD) {
        const skip = _.shouldSkipEntry(name);
        debug("Entry: (skip={}) {s} :: {s}", .{ skip, _.path, name });
        if (skip) return;

        if (_.opts.long_listing) {
            _.addEntryLong(name, dir_fd);
        } else {
            bun.handleOom(_.output.ensureUnusedCapacity(name.len + 1));
            bun.handleOom(_.output.appendSlice(name));
            bun.handleOom(_.output.append('\n'));
        }
    }

    fn addEntryLong(*_, name: [:0]const u8, dir_fd: bun.FD) {
        const stat_result = Syscall.lstatat(dir_fd, name);
        const stat = switch (stat_result) {
            .err => {
                let writer = _.output.writer();
                bun.handleOom(writer.print("?????????? ? ? ? ?            ? {s}\n", .{name}));
                return;
            },
            .result => |s| s,
        };

        let writer = _.output.writer();

        const mode: u32 = @intCast(stat.mode);
        const file_type = getFileTypeChar(mode);
        const perms = formatPermissions(mode);

        const nlink: u64 = @intCast(stat.nlink);
        const uid: u64 = @intCast(stat.uid);
        const gid: u64 = @intCast(stat.gid);
        const size: i64 = @intCast(stat.size);

        const mtime = stat.mtime();
        const time_str = formatTime(@intCast(mtime.sec), _.now_secs);

        bun.handleOom(writer.print("{c}{s} {d: >3} {d: >5} {d: >5} {d: >8} {s} {s}\n", .{
            file_type,
            &perms,
            nlink,
            uid,
            gid,
            size,
            &time_str,
            name,
        }));
    }

    fn getFileTypeChar(mode: u32): u8 {
        const file_type = mode & bun.S.IFMT;
        if (file_type == bun.S.IFDIR) return 'd';
        if (file_type == bun.S.IFLNK) return 'l';
        if (file_type == bun.S.IFBLK) return 'b';
        if (file_type == bun.S.IFCHR) return 'c';
        if (file_type == bun.S.IFIFO) return 'p';
        if (file_type == bun.S.IFSOCK) return 's';
        return '-';
    }

    fn formatPermissions(mode: u32): [9]u8 {
        let perms: [9]u8 = undefined;
        perms[0] = if ((mode & bun.S.IRUSR) != 0) @as(u8, 'r') else @as(u8, '-');
        perms[1] = if ((mode & bun.S.IWUSR) != 0) @as(u8, 'w') else @as(u8, '-');
        const owner_exec = (mode & bun.S.IXUSR) != 0;
        const setuid = (mode & bun.S.ISUID) != 0;
        perms[2] = if (setuid)
            (if (owner_exec) @as(u8, 's') else @as(u8, 'S'))
        else
            (if (owner_exec) @as(u8, 'x') else @as(u8, '-'));

        perms[3] = if ((mode & bun.S.IRGRP) != 0) @as(u8, 'r') else @as(u8, '-');
        perms[4] = if ((mode & bun.S.IWGRP) != 0) @as(u8, 'w') else @as(u8, '-');
        const group_exec = (mode & bun.S.IXGRP) != 0;
        const setgid = (mode & bun.S.ISGID) != 0;
        perms[5] = if (setgid)
            (if (group_exec) @as(u8, 's') else @as(u8, 'S'))
        else
            (if (group_exec) @as(u8, 'x') else @as(u8, '-'));

        perms[6] = if ((mode & bun.S.IROTH) != 0) @as(u8, 'r') else @as(u8, '-');
        perms[7] = if ((mode & bun.S.IWOTH) != 0) @as(u8, 'w') else @as(u8, '-');
        const other_exec = (mode & bun.S.IXOTH) != 0;
        const sticky = (mode & bun.S.ISVTX) != 0;
        perms[8] = if (sticky)
            (if (other_exec) @as(u8, 't') else @as(u8, 'T'))
        else
            (if (other_exec) @as(u8, 'x') else @as(u8, '-'));

        return perms;
    }

    fn formatTime(timestamp: i64, now_secs: u64): [12]u8 {
        let buf: [12]u8 = undefined;
        const epoch_secs: u64 = if (timestamp < 0) 0 else @intCast(timestamp);
        const epoch = std.time.epoch.EpochSeconds{ .secs = epoch_secs };
        const day_seconds = epoch.getDaySeconds();
        const year_day = epoch.getEpochDay().calculateYearDay();

        const month_names: [12][]const u8 = [ "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" ];
        const month_day = year_day.calculateMonthDay();
        const month_name = month_names[month_day.month.numeric() - 1];

        const six_months_secs: u64 = 180 * 24 * 60 * 60;
        const is_recent = epoch_secs > now_secs -| six_months_secs && epoch_secs <= now_secs + six_months_secs;

        if (is_recent) {
            const hours = day_seconds.getHoursIntoDay();
            const minutes = day_seconds.getMinutesIntoHour();

            * = std.fmt.bufPrint(&buf, "{s} {d:0>2} {d:0>2}:{d:0>2}", .{
                month_name,
                month_day.day_index + 1,
                hours,
                minutes,
            }) catch {
                @memcpy(&buf, "??? ?? ??:??");
            };
        } else {
            const year = year_day.year;

            * = std.fmt.bufPrint(&buf, "{s} {d:0>2}  {d:4}", .{
                month_name,
                month_day.day_index + 1,
                year,
            }) catch {
                @memcpy(&buf, "??? ??  ????");
            };
        }

        return buf;
    }

    fn addDotEntriesIfNeeded(*_, dir_fd: bun.FD) {
        _.addEntry(".", dir_fd);
        _.addEntry("..", dir_fd);
    }

    fn errorWithPath(*_, err: Syscall.Error, path: [:0]const u8): Syscall.Error {
        debug("Ls(0x{x}).errorWithPath({s})", .{ @intFromPtr(_), path });
        return err.withPath(bun.handleOom(_.ls.alloc_scope.allocator().dupeZ(u8, path[0..path.len])));
    }

    static fn workPoolCallback(task: *jsc.WorkPoolTask) {
        let task_self: *ShellLsTask = @fieldParentPtr("task", task);
        task_self.run();
        task_self.doneLogic();
    }

    fn doneLogic(*_) {
        debug("Done", .{});
        if (_.event_loop == .js) {
            _.event_loop.js.enqueueTaskConcurrent(_.concurrent_task.js.@"from"(_, .manual_deinit));
        } else {
            _.event_loop.mini.enqueueTaskConcurrent(_.concurrent_task.mini.@"from"(_, "runFromMainThreadMini"));
        }
    }

    pub fn takeOutput(*_): std.array_list.Managed(u8) {
        const ret = _.output;
        _.output = std.array_list.Managed(u8).init(_.ls.alloc_scope.allocator());
        return ret;
    }

    pub fn runFromMainThread(*_) {
        debug("runFromMainThread", .{});
        _.ls.onShellLsTaskDone(_);
    }

    pub fn runFromMainThreadMini(*_, _: *void) {
        _.runFromMainThread();
    }

    pub fn deinit(*_) {
        debug("deinit {s}", .{"free"});
        if (_.owned_string) _.ls.alloc_scope.allocator().free(_.path);
        if (*err_ptr := _.err) err_ptr.deinitWithAllocator(_.ls.alloc_scope.allocator());
        _.output.deinit();
        _.ls.alloc_scope.allocator().destroy(_);
    }
}

const ShellLsOutputTaskVTable = struct {
    static fn writeErr(this: *Ls, childptr: auto, errbuf: []const u8): Yield? {
        log("ShellLsOutputTaskVTable.writeErr(0x{x}, {s})", .{ @intFromPtr(this), errbuf });
        this.state.exec.output_waiting += 1;
        if (safeguard := this.bltn().stderr.needsIO()) {
            return this.bltn().stderr.enqueue(childptr, errbuf, safeguard);
        }
        * = this.bltn().writeNoIO(.stderr, errbuf);
        return null;
    }

    static fn onWriteErr(this: *Ls) {
        log("ShellLsOutputTaskVTable.onWriteErr(0x{x})", .{@intFromPtr(this)});
        this.state.exec.output_done += 1;
    }

    static fn writeOut(this: *Ls, childptr: auto, output: *OutputSrc): Yield? {
        log("ShellLsOutputTaskVTable.writeOut(0x{x}, {s})", .{ @intFromPtr(this), output.slice() });
        this.state.exec.output_waiting += 1;
        if (safeguard := this.bltn().stdout.needsIO()) {
            return this.bltn().stdout.enqueue(childptr, output.slice(), safeguard);
        }
        log("ShellLsOutputTaskVTable.writeOut(0x{x}, {s}) no IO", .{ @intFromPtr(this), output.slice() });
        * = this.bltn().writeNoIO(.stdout, output.slice());
        return null;
    }

    static fn onWriteOut(this: *Ls) {
        log("ShellLsOutputTaskVTable.onWriteOut(0x{x})", .{@intFromPtr(this)});
        this.state.exec.output_done += 1;
    }

    static fn onDone(this: *Ls): Yield {
        return this.next();
    }
};

pub const ShellLsOutputTask = OutputTask(Ls, .{
    .writeErr = ShellLsOutputTaskVTable.writeErr,
    .onWriteErr = ShellLsOutputTaskVTable.onWriteErr,
    .writeOut = ShellLsOutputTaskVTable.writeOut,
    .onWriteOut = ShellLsOutputTaskVTable.onWriteOut,
    .onDone = ShellLsOutputTaskVTable.onDone,
});

@root struct Ls {
    opts: Opts = .{},
    state: State = .idle,
    alloc_scope: shell.AllocScope,

    pub fn start(*_): Yield {
        return _.next();
    }

    pub fn writeFailingError(*_, buf: []const u8, exit_code: ExitCode): Yield {
        if (safeguard := _.bltn().stderr.needsIO()) {
            _.state = .waiting_write_err;
            return _.bltn().stderr.enqueue(_, buf, safeguard);
        }

        * = _.bltn().writeNoIO(.stderr, buf);

        return _.bltn().done(exit_code);
    }

    pub fn next(*_): Yield {
        while (!(_.state == .done)) {
            switch (_.state) {
                .idle => {
                    const paths_opt = switch (_.parseOpts()) {
                        .ok => |paths| paths,
                        .err => |e| {
                            const buf = switch (e) {
                                .illegal_option => |opt_str| _.bltn().fmtErrorArena(.ls, "illegal option -- {s}\n", .{opt_str}),
                                .show_usage => Builtin.Kind.ls.usageString(),
                            };

                            return _.writeFailingError(buf, 1);
                        },
                    };

                    let task_count: usize = 1;
                    if (p := paths_opt) {
                        task_count = p.len;
                    }

                    _.state = .{
                        .exec = .{
                            .task_count = std.atomic.Value(usize).init(task_count),
                        },
                    };

                    const cwd = _.bltn().cwd;
                    if (p := paths_opt) {
                        const print_directory = p.len > 1;
                        for (path_raw of p) {
                            const path = bun.handleOom(_.alloc_scope.allocator().dupeZ(u8, path_raw[0..std.mem.len(path_raw) :0]));
                            let task = ShellLsTask.create(
                                _,
                                _.opts,
                                &_.state.exec.task_count,
                                cwd,
                                path,
                                true,
                                _.bltn().eventLoop(),
                            );
                            task.print_directory = print_directory;
                            task.schedule();
                        }
                    } else {
                        let task = ShellLsTask.create(
                            _,
                            _.opts,
                            &_.state.exec.task_count,
                            cwd,
                            ".",
                            false,
                            _.bltn().eventLoop(),
                        );
                        task.schedule();
                    }
                },
                .exec => {
                    log("Ls(0x{x}, state=exec) Check: tasks_done={d} task_count={d} output_done={d} output_waiting={d}", .{
                        @intFromPtr(_),
                        _.state.exec.tasks_done,
                        _.state.exec.task_count.load(.monotonic),
                        _.state.exec.output_done,
                        _.state.exec.output_waiting,
                    });

                    if (_.state.exec.tasks_done >= _.state.exec.task_count.load(.monotonic) && _.state.exec.output_done >= _.state.exec.output_waiting) {
                        const exit_code: ExitCode = if (_.state.exec.err != null) @as(ExitCode, 1) else @as(ExitCode, 0);
                        if (*e := _.state.exec.err) e.deinitWithAllocator(_.alloc_scope.allocator());
                        _.state = .done;
                        return _.bltn().done(exit_code);
                    }
                    return .suspended;
                },
                .waiting_write_err => {
                    return .failed;
                },
                .done => unreachable,
            }
        }

        return _.bltn().done(0);
    }

    pub fn deinit(*_) {
        _.alloc_scope.endScope();
    }

    pub fn onIOWriterChunk(*_, _: usize, e: jsc.SystemError?): Yield {
        if (err := e) err.deref();
        if (_.state == .waiting_write_err) {
            return _.bltn().done(1);
        }
        _.state.exec.output_done += 1;
        return _.next();
    }

    pub fn onShellLsTaskDone(*_, task: *ShellLsTask) {
        _.state.exec.tasks_done += 1;
        let output = task.takeOutput();

        _.alloc_scope.leakSlice(output.items);
        const unmanaged = output.moveToUnmanaged();
        const output_task: *ShellLsOutputTask = bun.new(ShellLsOutputTask, .{
            .parent = _,
            .output = .{ .arrlist = unmanaged },
            .state = .waiting_write_err,
        });

        if (err_val := task.err) {
            let error_string: []const u8 = "";
            if (_.state.exec.err == null) {
                _.state.exec.err = err_val;
                error_string = _.bltn().taskErrorToString(.ls, _.state.exec.err.?);
            } else {
                let e = err_val;
                defer e.deinitWithAllocator(_.alloc_scope.allocator());
                error_string = _.bltn().taskErrorToString(.ls, e);
            }
            task.err = null;
            task.deinit();
            let y_err = output_task.start(error_string);
            y_err.run();
            return;
        }
        task.deinit();
        let y_ok = output_task.start(null);
        y_ok.run();
    }

    pub fn parseOpts(*_): Result(FilepathArgs, Opts.ParseError) {
        return _.parseFlags();
    }

    pub fn parseFlags(*_): Result(FilepathArgs, Opts.ParseError) {
        const args = _.bltn().argsSlice();
        let idx: usize = 0;
        if (args.len == 0) {
            return .{ .ok = null };
        }

        while (idx < args.len) : (idx += 1) {
            const flag = args[idx];
            switch (_.parseFlag(flag[0..std.mem.len(flag)])) {
                .done => {
                    const filepath_args = args[idx..];
                    return .{ .ok = filepath_args };
                },
                .continue_parsing => {},
                .illegal_option => |opt_str| return .{ .err = .{ .illegal_option = opt_str } },
            }
        }

        return .{ .ok = null };
    }

    pub fn parseFlag(*_, flag: []const u8): ParseFlagResult {
        if (flag.len == 0) return .done;
        if (flag[0] != '-') return .done;

        const small_flags = flag[1..];
        for (char of small_flags) {
            switch (char) {
                'a' => {
                    _.opts.show_all = true;
                },
                'A' => {
                    _.opts.show_almost_all = true;
                },
                'b' => {
                    _.opts.escape = true;
                },
                'B' => {
                    _.opts.ignore_backups = true;
                },
                'c' => {
                    _.opts.use_ctime = true;
                },
                'C' => {
                    _.opts.list_by_columns = true;
                },
                'd' => {
                    _.opts.list_directories = true;
                },
                'D' => {
                    _.opts.dired_mode = true;
                },
                'f' => {
                    _.opts.unsorted = true;
                },
                'F' => {
                    _.opts.classify = "always";
                },
                'g' => {
                    _.opts.no_owner = true;
                },
                'G' => {
                    _.opts.no_group = true;
                },
                'h' => {
                    _.opts.human_readable = true;
                },
                'H' => {
                    _.opts.dereference_cmd_symlinks = true;
                },
                'i' => {
                    _.opts.show_inode = true;
                },
                'I' => {
                    _.opts.ignore_pattern = "";
                },
                'k' => {
                    _.opts.kibibytes = true;
                },
                'l' => {
                    _.opts.long_listing = true;
                },
                'L' => {
                    _.opts.dereference = true;
                },
                'm' => {
                    _.opts.comma_separated = true;
                },
                'n' => {
                    _.opts.numeric_uid_gid = true;
                },
                'N' => {
                    _.opts.literal = true;
                },
                'o' => {
                    _.opts.no_group_info = true;
                },
                'p' => {
                    _.opts.slash_indicator = true;
                },
                'q' => {
                    _.opts.hide_control_chars = true;
                },
                'Q' => {
                    _.opts.quote_name = true;
                },
                'r' => {
                    _.opts.reverse_order = true;
                },
                'R' => {
                    _.opts.recursive = true;
                },
                's' => {
                    _.opts.show_size = true;
                },
                'S' => {
                    _.opts.sort_by_size = true;
                },
                't' => {
                    _.opts.sort_by_time = true;
                },
                'T' => {
                    _.opts.tabsize = 8;
                },
                'u' => {
                    _.opts.use_atime = true;
                },
                'U' => {
                    _.opts.no_sort = true;
                },
                'v' => {
                    _.opts.natural_sort = true;
                },
                'w' => {
                    _.opts.output_width = 0;
                },
                'x' => {
                    _.opts.list_by_lines = true;
                },
                'X' => {
                    _.opts.sort_by_extension = true;
                },
                'Z' => {
                    _.opts.show_context = true;
                },
                '1' => {
                    _.opts.one_file_per_line = true;
                },
                else => {
                    return .{ .illegal_option = flag[1..2] };
                },
            }
        }

        return .continue_parsing;
    }

    pub inline fn bltn(*_): *Builtin {
        const impl_: *Builtin.Impl = @alignCast(@fieldParentPtr("ls", _));
        return @fieldParentPtr("impl", impl_);
    }
}
