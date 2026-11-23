import bun from "bun";
const Environment = bun.Environment;
const c = bun.c;
const jsc = bun.jsc;
const strings = bun.strings;
const sys = bun.sys;
const gen = bun.gen.node_os;
const libuv = bun.windows.libuv;
const HOST_NAME_MAX = bun.HOST_NAME_MAX;

extern fn bun_sysconf__SC_NPROCESSORS_ONLN(): i32;
extern fn bun_sysconf__SC_CLK_TCK(): isize;
extern fn get_process_priority(pid: i32): i32;
extern fn set_process_priority(pid: i32, priority: i32): i32;
extern fn Bun__Os__getFreeMemory(): u64;

struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

struct ClearRetainingCapacityGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.clearRetainingCapacity(); }
    }
}

struct CloseGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.close(); }
    }
}

struct DerefGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deref(); }
    }
}

struct FreeGuard<T> {
    slice: []T,
    ~init() {
        bun.default_allocator.free(_.slice);
    }
}

struct FallbackFreeGuard {
    slice_ptr: *([]u8)!,
    stack_ptr: [*]const u8!,
    ~init() {
        unsafe {
            if (_.slice_ptr.*.ptr != _.stack_ptr) {
                bun.default_allocator.free(_.slice_ptr.*);
            }
        }
    }
}

struct VmDeallocateGuard {
    addr: usize,
    size: usize,
    ~init() {
        * = std.c.vm_deallocate(std.c.mach_task_self(), _.addr, _.size);
    }
}

struct UvCpuInfoGuard {
    ptr: [*]libuv.uv_cpu_info_t!,
    count: c_int,
    ~init() {
        libuv.uv_free_cpu_info(_.ptr, _.count);
    }
}

struct FreeIfAddrsGuard {
    ptr: (*c.ifaddrs)?,
    ~init() {
        c.freeifaddrs(_.ptr);
    }
}

struct UvIfAddrsGuard {
    ptr: [*]libuv.uv_interface_address_t!,
    count: c_int,
    ~init() {
        libuv.uv_free_interface_addresses(_.ptr, _.count);
    }
}

pub fn createNodeOsBinding(global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    const obj = jsc.JSObject.create({
        hostCpuCount: @max(1, bun_sysconf__SC_NPROCESSORS_ONLN()),
        cpus: gen.createCpusCallback(global),
        freemem: gen.createFreememCallback(global),
        getPriority: gen.createGetPriorityCallback(global),
        homedir: gen.createHomedirCallback(global),
        hostname: gen.createHostnameCallback(global),
        loadavg: gen.createLoadavgCallback(global),
        networkInterfaces: gen.createNetworkInterfacesCallback(global),
        release: gen.createReleaseCallback(global),
        totalmem: gen.createTotalmemCallback(global),
        uptime: gen.createUptimeCallback(global),
        userInfo: gen.createUserInfoCallback(global),
        version: gen.createVersionCallback(global),
        setPriority: gen.createSetPriorityCallback(global),
    }, global)?;
    return obj.toJS();
}

struct CPUTimes {
    user: u64 = 0,
    nice: u64 = 0,
    sys: u64 = 0,
    idle: u64 = 0,
    irq: u64 = 0,

    fn toValue(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        const fields = comptime std.@"meta".fieldNames(CPUTimes);
        const ret = jsc.JSValue.createEmptyObject(globalThis, fields.len);
        inline for (fieldName of fields) {
            ret.put(globalThis, jsc.ZigString.static(fieldName), jsc.JSValue.jsNumberFromUint64(@field(_, fieldName)));
        }
        return ret;
    }
}

pub fn cpus(global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    const cpusImpl = switch (Environment.os) {
        .linux => cpusImplLinux,
        .mac => cpusImplDarwin,
        .freebsd => cpusImplFreeBSD,
        .windows => cpusImplWindows,
        .wasm => @compileError("Unsupported OS"),
    };

    return cpusImpl(global) catch {
        let e = jsc.SystemError{
            message: bun.String.static("Failed to get CPU information"),
            code: bun.String.static(@tagName(jsc.Node.ErrorCode.ERR_SYSTEM_ERROR)),
        };
        return global.throwValue(e.toErrorInstance(global));
    };
}

fn cpusImplLinux(globalThis: *jsc.JSGlobalObject): !jsc.JSValue {
    // Create the return array
    const values = jsc.JSValue.createEmptyArray(globalThis, 0)?;
    let num_cpus: u32 = 0;

    let stack_fallback = std.heap.stackFallback(1024 * 8, bun.default_allocator);
    let file_buf = std.array_list.Managed(u8).init(stack_fallback.get());
    const _guard_file_buf = DeinitGuard<std.array_list.Managed(u8)>{ ptr: &file_buf };

    // Read /proc/stat to get number of CPUs and times
    {
        let file = std.fs.cwd().openFile("/proc/stat", {}) catch {
            // hidepid mounts (common on Android) deny /proc/stat. lazyCpus in os.ts
            // pre-creates hostCpuCount lazy proxies, so return that many stub
            // entries (zeroed times / unknown model / speed 0) — matches Node.
            const count: u32 = @as(u32, @intCast(@max(1, bun_sysconf__SC_NPROCESSORS_ONLN())));
            const stubs = jsc.JSValue.createEmptyArray(globalThis, count)?;
            const empty_times = CPUTimes{};
            let i: u32 = 0;
            while (i < count) : (i += 1) {
                const cpu = jsc.JSValue.createEmptyObject(globalThis, 3);
                cpu.put(globalThis, jsc.ZigString.static("times"), empty_times.toValue(globalThis));
                cpu.put(globalThis, jsc.ZigString.static("model"), jsc.ZigString.static("unknown").withEncoding().toJS(globalThis));
                cpu.put(globalThis, jsc.ZigString.static("speed"), jsc.JSValue.jsNumber(0));
                stubs.putIndex(globalThis, i, cpu)?;
            }
            return stubs;
        };
        const _guard_file = CloseGuard<std.fs.File>{ ptr: &file };

        const file_buf_ptr = &file_buf;
        const read = bun.sys.File.@"from"(file).readToEndWithArrayList(file_buf_ptr, .probably_small).unwrap()?;
        const _guard_clear = ClearRetainingCapacityGuard<@TypeOf(file_buf)>{ ptr: file_buf_ptr };
        const contents = file_buf.items[0..read];

        let line_iter = std.mem.tokenizeScalar(u8, contents, '\n');

        // Skip the first line (aggregate of all CPUs)
        * = line_iter.next();

        // Read each CPU line
        while (line := line_iter.next()) {
            // CPU lines are formatted as `cpu0 user nice sys idle iowait irq softirq`
            let toks = std.mem.tokenizeAny(u8, line, " \t");
            const cpu_name = toks.next();
            if (cpu_name == null || !std.mem.startsWith(u8, cpu_name.?, "cpu")) break; // done with CPUs

            //NOTE: libuv assumes this is fixed on Linux, not sure that's actually the case
            const scale: u64 = 10;

            let times = CPUTimes{};
            const u_tok = toks.next();
            if (u_tok == null) return error.eol;
            const n_tok = toks.next();
            if (n_tok == null) return error.eol;
            const s_tok = toks.next();
            if (s_tok == null) return error.eol;
            const i_tok = toks.next();
            if (i_tok == null) return error.eol;
            if (toks.next() == null) return error.eol; // skip iowait
            const irq_tok = toks.next();
            if (irq_tok == null) return error.eol;

            times.user = scale * (std.fmt.parseInt(u64, u_tok.?, 10)?);
            times.nice = scale * (std.fmt.parseInt(u64, n_tok.?, 10)?);
            times.sys = scale * (std.fmt.parseInt(u64, s_tok.?, 10)?);
            times.idle = scale * (std.fmt.parseInt(u64, i_tok.?, 10)?);
            times.irq = scale * (std.fmt.parseInt(u64, irq_tok.?, 10)?);

            // Actually create the JS object representing the CPU
            const cpu = jsc.JSValue.createEmptyObject(globalThis, 1);
            cpu.put(globalThis, jsc.ZigString.static("times"), times.toValue(globalThis));
            values.putIndex(globalThis, num_cpus, cpu)?;

            num_cpus += 1;
        }
    }

    // Read /proc/cpuinfo to get model information (optional)
    const maybe_cpuinfo = std.fs.cwd().openFile("/proc/cpuinfo", {}) catch null;
    if (maybe_cpuinfo != null) {
        let file = maybe_cpuinfo.?;
        const _guard_file = CloseGuard<std.fs.File>{ ptr: &file };

        const file_buf_ptr = &file_buf;
        const read = bun.sys.File.@"from"(file).readToEndWithArrayList(file_buf_ptr, .probably_small).unwrap()?;
        const _guard_clear = ClearRetainingCapacityGuard<@TypeOf(file_buf)>{ ptr: file_buf_ptr };
        const contents = file_buf.items[0..read];

        let line_iter = std.mem.tokenizeScalar(u8, contents, '\n');

        const key_processor = "processor\t: ";
        const key_model_name = "model name\t: ";

        let cpu_index: u32 = 0;
        let has_model_name = true;
        while (line := line_iter.next()) {
            if (strings.hasPrefixComptime(line, key_processor)) {
                if (!has_model_name) {
                    const cpu = values.getIndex(globalThis, cpu_index)?;
                    cpu.put(globalThis, jsc.ZigString.static("model"), jsc.ZigString.static("unknown").withEncoding().toJS(globalThis));
                }
                // If this line starts a new processor, parse the index from the line
                const digits = std.mem.trim(u8, line[key_processor.len..], " \t\n");
                cpu_index = std.fmt.parseInt(u32, digits, 10)?;
                if (cpu_index >= num_cpus) return error.too_may_cpus;
                has_model_name = false;
            } else if (strings.hasPrefixComptime(line, key_model_name)) {
                // If this is the model name, extract it and store on the current cpu
                const model_name = line[key_model_name.len..];
                const cpu = values.getIndex(globalThis, cpu_index)?;
                cpu.put(globalThis, jsc.ZigString.static("model"), jsc.ZigString.init(model_name).withEncoding().toJS(globalThis));
                has_model_name = true;
            }
        }
        if (!has_model_name) {
            const cpu = values.getIndex(globalThis, cpu_index)?;
            cpu.put(globalThis, jsc.ZigString.static("model"), jsc.ZigString.static("unknown").withEncoding().toJS(globalThis));
        }
    } else {
        // Initialize model name to "unknown"
        let it = values.arrayIterator(globalThis)?;
        while (cpu := it.next()?) {
            cpu.put(globalThis, jsc.ZigString.static("model"), jsc.ZigString.static("unknown").withEncoding().toJS(globalThis));
        }
    }

    // Read /sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq to get current frequency (optional)
    let freq_cpu_index: usize = 0;
    while (freq_cpu_index < num_cpus) : (freq_cpu_index += 1) {
        const cpu = values.getIndex(globalThis, @as(u32, @truncate(freq_cpu_index)))?;

        let path_buf: [128]u8 = undefined;
        const path = std.fmt.bufPrint(&path_buf, "/sys/devices/system/cpu/cpu{d}/cpufreq/scaling_cur_freq", .{ freq_cpu_index })?;
        const maybe_freq_file = std.fs.cwd().openFile(path, {}) catch null;
        if (maybe_freq_file != null) {
            let file = maybe_freq_file.?;
            const _guard_file = CloseGuard<std.fs.File>{ ptr: &file };

            const file_buf_ptr = &file_buf;
        const read = bun.sys.File.@"from"(file).readToEndWithArrayList(file_buf_ptr, .probably_small).unwrap()?;
            const _guard_clear = ClearRetainingCapacityGuard<@TypeOf(file_buf)>{ ptr: file_buf_ptr };
            const contents = file_buf.items[0..read];

            const digits = std.mem.trim(u8, contents, " \n");
            const speed = (std.fmt.parseInt(u64, digits, 10) catch 0) / 1000;

            cpu.put(globalThis, jsc.ZigString.static("speed"), jsc.JSValue.jsNumber(speed));
        } else {
            // Initialize CPU speed to 0
            cpu.put(globalThis, jsc.ZigString.static("speed"), jsc.JSValue.jsNumber(0));
        }
    }

    return values;
}

fn cpusImplFreeBSD(globalThis: *jsc.JSGlobalObject): !jsc.JSValue {
    let ncpu: c_uint = 0;
    let ncpu_len: usize = @sizeOf(c_uint);
    std.posix.sysctlbynameZ("hw.ncpu", &ncpu, &ncpu_len, null, 0)?;
    if (ncpu == 0) return error.no_processor_info;

    let model_buf: [512]u8 = undefined;
    let model_len: usize = model_buf.len;
    let model = jsc.ZigString.static("unknown").withEncoding().toJS(globalThis);
    if ((std.posix.sysctlbynameZ("hw.model", &model_buf, &model_len, null, 0) catch null) != null) {
        model = jsc.ZigString.init(std.mem.sliceTo(&model_buf, 0)).withEncoding().toJS(globalThis);
    }

    let speed_mhz: c_uint = 0;
    let speed_len: usize = @sizeOf(c_uint);
    * = std.posix.sysctlbynameZ("hw.clockrate", &speed_mhz, &speed_len, null, 0) catch {};

    const cpu_states = 5; // user, nice, sys, intr, idle
    const times_buf = bun.default_allocator.alloc(c_long, @as(usize, ncpu) * cpu_states)?;
    const _guard_times_buf = FreeGuard<c_long>{ slice: times_buf };
    let times_len: usize = times_buf.len * @sizeOf(c_long);
    std.posix.sysctlbynameZ("kern.cp_times", times_buf.ptr, &times_len, null, 0)?;

    const ticks: i64 = bun_sysconf__SC_CLK_TCK();
    const mult: u64 = if (ticks > 0) @as(u64, 1000) / @as(u64, @intCast(ticks)) else @as(u64, 1);

    const values = jsc.JSValue.createEmptyArray(globalThis, @as(u32, @intCast(ncpu)))?;
    let i: u32 = 0;
    while (i < ncpu) : (i += 1) {
        const off = @as(usize, i) * cpu_states;
        const times = CPUTimes{
            user: @as(u64, @intCast(@max(times_buf[off + 0], 0))) * mult,
            nice: @as(u64, @intCast(@max(times_buf[off + 1], 0))) * mult,
            sys: @as(u64, @intCast(@max(times_buf[off + 2], 0))) * mult,
            irq: @as(u64, @intCast(@max(times_buf[off + 3], 0))) * mult,
            idle: @as(u64, @intCast(@max(times_buf[off + 4], 0))) * mult,
        };
        const cpu = jsc.JSValue.createEmptyObject(globalThis, 3);
        cpu.put(globalThis, jsc.ZigString.static("model"), model);
        cpu.put(globalThis, jsc.ZigString.static("speed"), jsc.JSValue.jsNumber(speed_mhz));
        cpu.put(globalThis, jsc.ZigString.static("times"), times.toValue(globalThis));
        values.putIndex(globalThis, i, cpu)?;
    }
    return values;
}

fn cpusImplDarwin(globalThis: *jsc.JSGlobalObject): !jsc.JSValue {
    // Fetch the CPU info structure
    let num_cpus: c.natural_t = @as(c.natural_t, 0);
    let info: [*]bun.c.processor_cpu_load_info = undefined;
    let info_size: std.c.mach_msg_type_number_t = @as(std.c.mach_msg_type_number_t, 0);
    if (bun.c.host_processor_info(
        std.c.mach_host_self(),
        bun.c.PROCESSOR_CPU_LOAD_INFO,
        &num_cpus,
        unsafe { @ptrCast(&info) },
        &info_size,
    ) != 0) {
        return error.no_processor_info;
    }
    const _guard_info = VmDeallocateGuard{ addr: unsafe { @intFromPtr(info) }, size: info_size };

    // Ensure we got the amount of data we expected to guard against buffer overruns
    if (info_size != bun.c.PROCESSOR_CPU_LOAD_INFO_COUNT * num_cpus) {
        return error.broken_process_info;
    }

    // Get CPU model name
    let model_name_buf: [512]u8 = undefined;
    let len: usize = model_name_buf.len;
    // Try brand_string first and if it fails try hw.model
    if (!(std.c.sysctlbyname("machdep.cpu.brand_string", &model_name_buf, &len, null, 0) == 0 ||
        std.c.sysctlbyname("hw.model", &model_name_buf, &len, null, 0) == 0))
    {
        return error.no_processor_info;
    }
    // NOTE: sysctlbyname doesn't update len if it was large enough, so we
    // still have to find the null terminator.  All cpus can share the same
    // model name.
    const model_name = jsc.ZigString.init(std.mem.sliceTo(&model_name_buf, 0)).withEncoding().toJS(globalThis);

    // Get CPU speed
    let speed: u64 = 0;
    len = @sizeOf(@TypeOf(speed));
    * = std.c.sysctlbyname("hw.cpufrequency", &speed, &len, null, 0);
    if (speed == 0) {
        // Suggested by Node implementation:
        // If sysctl hw.cputype == CPU_TYPE_ARM64, the correct value is unavailable
        // from Apple, but we can hard-code it here to a plausible value.
        speed = 2_400_000_000;
    }

    // Get the multiplier; this is the number of ms/tick
    const ticks: i64 = bun_sysconf__SC_CLK_TCK();
    const multiplier = 1000 / @as(u64, @intCast(ticks));

    // Set up each CPU value in the return
    const values = jsc.JSValue.createEmptyArray(globalThis, @as(u32, @intCast(num_cpus)))?;
    let cpu_index: u32 = 0;
    while (cpu_index < num_cpus) : (cpu_index += 1) {
        const times = CPUTimes{
            user: info[cpu_index].cpu_ticks[0] * multiplier,
            nice: info[cpu_index].cpu_ticks[3] * multiplier,
            sys: info[cpu_index].cpu_ticks[1] * multiplier,
            idle: info[cpu_index].cpu_ticks[2] * multiplier,
            irq: 0, // not available
        };

        const cpu = jsc.JSValue.createEmptyObject(globalThis, 3);
        cpu.put(globalThis, jsc.ZigString.static("speed"), jsc.JSValue.jsNumber(speed / 1_000_000));
        cpu.put(globalThis, jsc.ZigString.static("model"), model_name);
        cpu.put(globalThis, jsc.ZigString.static("times"), times.toValue(globalThis));

        values.putIndex(globalThis, cpu_index, cpu)?;
    }
    return values;
}

pub fn cpusImplWindows(globalThis: *jsc.JSGlobalObject): !jsc.JSValue {
    let cpu_infos: [*]libuv.uv_cpu_info_t = undefined;
    let count: c_int = undefined;
    const e = libuv.uv_cpu_info(&cpu_infos, &count);
    if (e != 0) {
        return error.NoProcessorInfo;
    }
    const _guard_cpu_infos = UvCpuInfoGuard{ ptr: cpu_infos, count: count };

    const values = jsc.JSValue.createEmptyArray(globalThis, @as(u32, @intCast(count)))?;
    const count_usize: usize = @as(usize, @intCast(count));

    let i: u32 = 0;
    for (cpu_info of unsafe { cpu_infos[0..count_usize] }) {
        const times = CPUTimes{
            user: cpu_info.cpu_times.user,
            nice: cpu_info.cpu_times.nice,
            sys: cpu_info.cpu_times.sys,
            idle: cpu_info.cpu_times.idle,
            irq: cpu_info.cpu_times.irq,
        };

        const cpu = jsc.JSValue.createEmptyObject(globalThis, 3);
        cpu.put(globalThis, jsc.ZigString.static("model"), jsc.ZigString.init(bun.span(cpu_info.model)).withEncoding().toJS(globalThis));
        cpu.put(globalThis, jsc.ZigString.static("speed"), jsc.JSValue.jsNumber(cpu_info.speed));
        cpu.put(globalThis, jsc.ZigString.static("times"), times.toValue(globalThis));

        values.putIndex(globalThis, i, cpu)?;
        i += 1;
    }

    return values;
}

pub fn freemem(): u64 {
    return Bun__Os__getFreeMemory();
}

pub fn getPriority(global: *jsc.JSGlobalObject, pid: i32): bun.JSError!i32 {
    const result = get_process_priority(pid);
    if (result == std.math.maxInt(i32)) {
        let e = jsc.SystemError{
            message: bun.String.static("no such process"),
            code: bun.String.static("ESRCH"),
            errno: switch (bun.Environment.os) {
                else => -@as(c_int, @intFromEnum(std.posix.E.SRCH)),
                .windows => libuv.UV_ESRCH,
            },
            syscall: bun.String.static("uv_os_getpriority"),
        };
        return global.throwValue(e.toErrorInstanceWithInfoObject(global));
    }
    return result;
}

pub fn homedir(global: *jsc.JSGlobalObject): !bun.String {
    // In Node.js, this is a wrapper around uv_os_homedir.
    if (Environment.isWindows) {
        let out: bun.PathBuffer = undefined;
        let size: usize = out.len;
        if (e := libuv.uv_os_homedir(&out, &size).toError(.uv_os_homedir)) {
            return global.throwValue((e.toJS(global))?);
        }
        return bun.String.cloneUTF8(out[0..size]);
    } else {
        // The posix implementation of uv_os_homedir first checks the HOME
        // environment variable, then falls back to reading the passwd entry.
        if (home := bun.env_var.HOME.get()) {
            if (home.len > 0)
                return bun.String.init(home);
        }

        // From libuv:
        // > Calling sysconf(_SC_GETPW_R_SIZE_MAX) would get the suggested size, but it
        // > is frequently 1024 or 4096, so we can just use that directly. The pwent
        // > will not usually be large.
        // Instead of always using an allocation, first try a stack allocation
        // of 4096, then fallback to heap.
        let stack_string_bytes: [4096]u8 = undefined;
        let string_bytes: []u8 = &stack_string_bytes;
        const _guard_string_bytes = FallbackFreeGuard{ slice_ptr: &string_bytes, stack_ptr: &stack_string_bytes };

        let pw: bun.c.passwd = undefined;
        let result: (*bun.c.passwd)? = null;

        let ret: c_int = 0;
        while (true) {
            ret = bun.c.getpwuid_r(
                bun.c.geteuid(),
                &pw,
                string_bytes.ptr,
                string_bytes.len,
                &result,
            );

            if (ret == @intFromEnum(bun.sys.E.INTR))
                continue;

            // If the system call wants more memory, double it.
            if (ret == @intFromEnum(bun.sys.E.RANGE)) {
                const len = string_bytes.len;
                bun.default_allocator.free(string_bytes);
                string_bytes = [];
                string_bytes = bun.default_allocator.alloc(u8, len * 2)?;
                continue;
            }

            break;
        }

        if (ret != 0) {
            return global.throwValue((bun.sys.Error.fromCode(
                @enumFromInt(ret),
                .uv_os_homedir,
            ).toJS(global))?);
        }

        if (result == null) {
            // bionic has no passwd entries for app uids; with HOME also unset
            // (zygote/run-as), return a usable default rather than throwing.
            if (comptime Environment.isAndroid) {
                return bun.String.static("/data/local/tmp");
            }
            // in uv__getpwuid_r, null result throws UV_ENOENT.
            return global.throwValue((bun.sys.Error.fromCode(
                .NOENT,
                .uv_os_homedir,
            ).toJS(global))?);
        }

        if (dir := pw.pw_dir) {
            return bun.String.cloneUTF8(bun.span(dir));
        } else {
            return bun.String.empty;
        }
    }
}

pub fn hostname(global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    if (Environment.isWindows) {
        let name_buffer: [129]u16 = undefined;
        const name_ptr = unsafe { @as(bun.windows.PWSTR, @ptrCast(&name_buffer)) };
        if (bun.windows.GetHostNameW(name_ptr, name_buffer.len) == 0) {
            let str = bun.String.cloneUTF16(bun.sliceTo(&name_buffer, 0));
            const _guard_str = DerefGuard<bun.String>{ ptr: &str };
            return str.toJS(global);
        }

        let result: std.os.windows.ws2_32.WSADATA = undefined;
        if (std.os.windows.ws2_32.WSAStartup(0x202, &result) == 0) {
            if (bun.windows.GetHostNameW(name_ptr, name_buffer.len) == 0) {
                let y = bun.String.cloneUTF16(bun.sliceTo(&name_buffer, 0));
                const _guard_y = DerefGuard<bun.String>{ ptr: &y };
                return y.toJS(global);
            }
        }

        return jsc.ZigString.init("unknown").withEncoding().toJS(global);
    } else {
        let name_buffer: [HOST_NAME_MAX]u8 = undefined;
        return jsc.ZigString.init(std.posix.gethostname(&name_buffer) catch "unknown").withEncoding().toJS(global);
    }
}

pub fn loadavg(global: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    const result = switch (bun.Environment.os) {
        .mac => loadavg: {
            let avg: c.struct_loadavg = undefined;
            let size: usize = @sizeOf(@TypeOf(avg));

            std.posix.sysctlbynameZ(
                "vm.loadavg",
                &avg,
                &size,
                null,
                0,
            ) catch e => switch (e) {
                else => break :loadavg [0.0, 0.0, 0.0],
            };

            const scale: f64 = @floatFromInt(avg.fscale);
            const v0: f64 = if (scale == 0.0) 0.0 else @as(f64, @floatFromInt(avg.ldavg[0])) / scale;
            const v1: f64 = if (scale == 0.0) 0.0 else @as(f64, @floatFromInt(avg.ldavg[1])) / scale;
            const v2: f64 = if (scale == 0.0) 0.0 else @as(f64, @floatFromInt(avg.ldavg[2])) / scale;
            break :loadavg [v0, v1, v2];
        },
        .linux => loadavg: {
            let info: c.struct_sysinfo = undefined;
            if (c.sysinfo(&info) == @as(c_int, 0)) {
                break :loadavg [
                    std.math.ceil((@as(f64, @floatFromInt(info.loads[0])) / 65536.0) * 100.0) / 100.0,
                    std.math.ceil((@as(f64, @floatFromInt(info.loads[1])) / 65536.0) * 100.0) / 100.0,
                    std.math.ceil((@as(f64, @floatFromInt(info.loads[2])) / 65536.0) * 100.0) / 100.0,
                ];
            }
            break :loadavg [0.0, 0.0, 0.0];
        },
        .freebsd => loadavg: {
            let avg: [3]f64 = [0.0, 0.0, 0.0];
            if (c.getloadavg(&avg, 3) != 3) break :loadavg [0.0, 0.0, 0.0];
            break :loadavg avg;
        },
        .windows => [0.0, 0.0, 0.0],
        .wasm => @compileError("TODO"),
    };

    return jsc.JSArray.create(global, &[
        jsc.JSValue.jsNumber(result[0]),
        jsc.JSValue.jsNumber(result[1]),
        jsc.JSValue.jsNumber(result[2]),
    ]);
}

/// Given a netmask returns a CIDR suffix.  Returns null if the mask is not valid.
/// `@TypeOf(mask)` must be one of u32 (IPv4) or u128 (IPv6)
fn netmaskToCIDRSuffix(mask: auto): u8? {
    const T = @TypeOf(mask);
    comptime bun.assert(T == u32 || T == u128);

    const mask_bits = @byteSwap(mask);

    // Validity check: set bits should be left-contiguous
    const first_zero = @clz(~mask_bits);
    const last_one = @bitSizeOf(T) - @ctz(mask_bits);
    if (first_zero < @bitSizeOf(T) && first_zero < last_one) return null;
    return first_zero;
}

struct NetworkInterfaceHelpers {
    // We'll skip interfaces that aren't actually available
    static fn skip(iface: *c.ifaddrs): bool {
        // Skip interfaces that aren't actually available
        if ((iface.ifa_flags & c.IFF_RUNNING) == 0) return true;
        if ((iface.ifa_flags & c.IFF_UP) == 0) return true;
        if (iface.ifa_addr == null) return true;

        return false;
    }

    // We won't actually return link-layer interfaces but we need them for
    //  extracting the MAC address
    static fn isLinkLayer(iface: *c.ifaddrs): bool {
        if (iface.ifa_addr == null) return false;
        if (comptime Environment.isLinux) {
            unsafe {
                return iface.ifa_addr.*.sa_family == std.posix.AF.PACKET;
            }
        } else if (comptime Environment.isMac || Environment.isFreeBSD) {
            unsafe {
                return iface.ifa_addr.?.*.sa_family == std.posix.AF.LINK;
            }
        } else {
            @compileError("unreachable");
        }
    }

    static fn isLoopback(iface: *c.ifaddrs): bool {
        return (iface.ifa_flags & c.IFF_LOOPBACK) == c.IFF_LOOPBACK;
    }
}

pub const networkInterfaces = switch (Environment.os) {
    .linux, .mac, .freebsd => networkInterfacesPosix,
    .windows => networkInterfacesWindows,
    .wasm => @compileError("Unsupported OS"),
};

fn networkInterfacesPosix(globalThis: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    // getifaddrs sets a pointer to a linked list
    let interface_start: (*c.ifaddrs)? = null;
    const rc = c.getifaddrs(&interface_start);
    if (rc != 0) {
        const errno = std.posix.errno(rc);
        // Android API 30+: SELinux denies the netlink socket getifaddrs uses.
        // Node returns {} rather than throwing.
        if (comptime Environment.isAndroid) {
            if (errno == .ACCES || errno == .PERM) {
                return jsc.JSValue.createEmptyObject(globalThis, 0);
            }
        }
        let e = jsc.SystemError{
            message: bun.String.static("A system error occurred: getifaddrs returned an error"),
            code: bun.String.static("ERR_SYSTEM_ERROR"),
            errno: @intFromEnum(errno),
            syscall: bun.String.static("getifaddrs"),
        };

        return globalThis.throwValue(e.toErrorInstance(globalThis));
    }
    const _guard_interface_start = FreeIfAddrsGuard{ ptr: interface_start };

    // The list currently contains entries for link-layer interfaces
    //  and the IPv4, IPv6 interfaces.  We only want to return the latter two
    //  but need the link-layer entries to determine MAC address.
    // So, on our first pass through the linked list we'll count the number of
    //  INET interfaces only.
    let num_inet_interfaces: usize = 0;
    let it = interface_start;
    while (iface := it) : (it = iface.ifa_next) {
        if (NetworkInterfaceHelpers.skip(iface) || NetworkInterfaceHelpers.isLinkLayer(iface)) continue;
        num_inet_interfaces += 1;
    }

    let ret = jsc.JSValue.createEmptyObject(globalThis, 0);

    // Second pass through, populate each interface object
    it = interface_start;
    while (iface := it) : (it = iface.ifa_next) {
        if (NetworkInterfaceHelpers.skip(iface) || NetworkInterfaceHelpers.isLinkLayer(iface)) continue;

        const interface_name = std.mem.sliceTo(iface.ifa_name, 0);
        const addr = unsafe { std.net.Address.initPosix(@alignCast(@ptrCast(iface.ifa_addr))) };
        const netmask = unsafe { std.net.Address.initPosix(@alignCast(@ptrCast(iface.ifa_netmask))) };

        let iface_obj = jsc.JSValue.createEmptyObject(globalThis, 0);

        // address <string> The assigned IPv4 or IPv6 address
        // cidr <string> The assigned IPv4 or IPv6 address with the routing prefix in CIDR notation. If the netmask is invalid, this property is set to null.
        {
            // Compute the CIDR suffix; returns null if the netmask cannot
            //  be converted to a CIDR suffix
            let maybe_suffix: u8? = null;
            if (addr.any.family == std.posix.AF.INET) {
                maybe_suffix = netmaskToCIDRSuffix(netmask.@"in".sa.addr);
            } else if (addr.any.family == std.posix.AF.INET6) {
                maybe_suffix = netmaskToCIDRSuffix(@as(u128, @bitCast(netmask.in6.sa.addr)));
            }

            // Format the address and then, if valid, the CIDR suffix; both
            //  the address and cidr values can be slices into this same buffer
            // e.g. addr_str = "192.168.88.254", cidr_str = "192.168.88.254/24"
            let buf: [64]u8 = undefined;
            const addr_str = bun.fmt.formatIp(addr, &buf) catch unreachable;
            let cidr = jsc.JSValue.null;
            if (suffix := maybe_suffix) {
                //NOTE addr_str might not start at buf[0] due to slicing in formatIp
                const start = unsafe { @intFromPtr(addr_str.ptr) - @intFromPtr(&buf[0]) };
                // Start writing the suffix immediately after the address
                const suffix_str = std.fmt.bufPrint(buf[start + addr_str.len ..], "/{}", .{ suffix }) catch unreachable;
                // The full cidr value is the address + the suffix
                const cidr_str = buf[start .. start + addr_str.len + suffix_str.len];
                cidr = jsc.ZigString.init(cidr_str).withEncoding().toJS(globalThis);
            }

            iface_obj.put(globalThis, jsc.ZigString.static("address"), jsc.ZigString.init(addr_str).withEncoding().toJS(globalThis));
            iface_obj.put(globalThis, jsc.ZigString.static("cidr"), cidr);
        }

        // netmask <string> The IPv4 or IPv6 network mask
        {
            let buf: [64]u8 = undefined;
            const str = bun.fmt.formatIp(netmask, &buf) catch unreachable;
            iface_obj.put(globalThis, jsc.ZigString.static("netmask"), jsc.ZigString.init(str).withEncoding().toJS(globalThis));
        }

        // family <string> Either IPv4 or IPv6
        const family_val = if (addr.any.family == std.posix.AF.INET)
            globalThis.commonStrings().IPv4()
        else if (addr.any.family == std.posix.AF.INET6)
            globalThis.commonStrings().IPv6()
        else
            jsc.ZigString.static("unknown").toJS(globalThis);
        iface_obj.put(globalThis, jsc.ZigString.static("family"), family_val);

        // mac <string> The MAC address of the network interface
        {
            if (comptime Environment.isLinux) {
                let maybe_ll: (*std.posix.sockaddr.ll)? = null;
                let ll_it = interface_start;
                while (ll_iface := ll_it) : (ll_it = ll_iface.ifa_next) {
                    if (NetworkInterfaceHelpers.skip(ll_iface) || !NetworkInterfaceHelpers.isLinkLayer(ll_iface)) continue;
                    const ll_name = bun.sliceTo(ll_iface.ifa_name, 0);
                    if (!strings.hasPrefix(ll_name, interface_name)) continue;
                    if (ll_name.len > interface_name.len && ll_name[interface_name.len] != ':') continue;
                    maybe_ll = @as((*std.posix.sockaddr.ll)?, unsafe { @ptrCast(@alignCast(ll_iface.ifa_addr)) });
                    break;
                }
                if (ll := maybe_ll) {
                    const addr_data = ll.addr;
                    if (addr_data.len < 6) {
                        const mac = "00:00:00:00:00:00";
                        iface_obj.put(globalThis, jsc.ZigString.static("mac"), jsc.ZigString.init(mac).withEncoding().toJS(globalThis));
                    } else {
                        let mac_buf: [17]u8 = undefined;
                        const mac = std.fmt.bufPrint(&mac_buf, "{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}", .{
                            addr_data[0], addr_data[1], addr_data[2],
                            addr_data[3], addr_data[4], addr_data[5],
                        }) catch unreachable;
                        iface_obj.put(globalThis, jsc.ZigString.static("mac"), jsc.ZigString.init(mac).withEncoding().toJS(globalThis));
                    }
                } else {
                    const mac = "00:00:00:00:00:00";
                    iface_obj.put(globalThis, jsc.ZigString.static("mac"), jsc.ZigString.init(mac).withEncoding().toJS(globalThis));
                }
            } else if (comptime Environment.isMac || Environment.isFreeBSD) {
                let maybe_ll: (*c.sockaddr_dl)? = null;
                let ll_it = interface_start;
                while (ll_iface := ll_it) : (ll_it = ll_iface.ifa_next) {
                    if (NetworkInterfaceHelpers.skip(ll_iface) || !NetworkInterfaceHelpers.isLinkLayer(ll_iface)) continue;
                    const ll_name = bun.sliceTo(ll_iface.ifa_name, 0);
                    if (!strings.hasPrefix(ll_name, interface_name)) continue;
                    if (ll_name.len > interface_name.len && ll_name[interface_name.len] != ':') continue;
                    maybe_ll = @as((*c.sockaddr_dl)?, unsafe { @ptrCast(@alignCast(ll_iface.ifa_addr)) });
                    break;
                }
                if (ll := maybe_ll) {
                    const addr_data = ll.sdl_data[ll.sdl_nlen..];
                    if (addr_data.len < 6) {
                        const mac = "00:00:00:00:00:00";
                        iface_obj.put(globalThis, jsc.ZigString.static("mac"), jsc.ZigString.init(mac).withEncoding().toJS(globalThis));
                    } else {
                        let mac_buf: [17]u8 = undefined;
                        const mac = std.fmt.bufPrint(&mac_buf, "{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}", .{
                            addr_data[0], addr_data[1], addr_data[2],
                            addr_data[3], addr_data[4], addr_data[5],
                        }) catch unreachable;
                        iface_obj.put(globalThis, jsc.ZigString.static("mac"), jsc.ZigString.init(mac).withEncoding().toJS(globalThis));
                    }
                } else {
                    const mac = "00:00:00:00:00:00";
                    iface_obj.put(globalThis, jsc.ZigString.static("mac"), jsc.ZigString.init(mac).withEncoding().toJS(globalThis));
                }
            } else {
                const mac = "00:00:00:00:00:00";
                iface_obj.put(globalThis, jsc.ZigString.static("mac"), jsc.ZigString.init(mac).withEncoding().toJS(globalThis));
            }
        }

        // internal <boolean> true if the network interface is a loopback or similar interface that is not remotely accessible; otherwise false
        iface_obj.put(globalThis, jsc.ZigString.static("internal"), jsc.JSValue.jsBoolean(NetworkInterfaceHelpers.isLoopback(iface)));

        // scopeid <number> The numeric IPv6 scope ID (only specified when family is IPv6)
        if (addr.any.family == std.posix.AF.INET6) {
            iface_obj.put(globalThis, jsc.ZigString.static("scopeid"), jsc.JSValue.jsNumber(addr.in6.sa.scope_id));
        }

        // Does this entry already exist?
        if (array := (ret.get(globalThis, interface_name))?) {
            // Add this interface entry to the existing array
            const next_index: u32 = @as(u32, @intCast(array.getLength(globalThis)?));
            array.putIndex(globalThis, next_index, iface_obj)?;
        } else {
            // Add it as an array with this interface as an element
            const member_name = jsc.ZigString.init(interface_name);
            let array = jsc.JSValue.createEmptyArray(globalThis, 1)?;
            array.putIndex(globalThis, 0, iface_obj)?;
            ret.put(globalThis, &member_name, array);
        }
    }

    return ret;
}

fn networkInterfacesWindows(globalThis: *jsc.JSGlobalObject): bun.JSError!jsc.JSValue {
    let ifaces: [*]libuv.uv_interface_address_t = undefined;
    let count: c_int = undefined;
    const e = libuv.uv_interface_addresses(&ifaces, &count);
    if (e != 0) {
        let sys_err = jsc.SystemError{
            message: bun.String.static("uv_interface_addresses failed"),
            code: bun.String.static("ERR_SYSTEM_ERROR"),
            //.info = info,
            errno: e,
            syscall: bun.String.static("uv_interface_addresses"),
        };
        return globalThis.throwValue(sys_err.toErrorInstance(globalThis));
    }
    const _guard_ifaces = UvIfAddrsGuard{ ptr: ifaces, count: count };

    let ret = jsc.JSValue.createEmptyObject(globalThis, 8);

    // 65 comes from: https://stackoverflow.com/questions/39443413/why-is-inet6-addrstrlen-defined-as-46-in-c
    let ip_buf: [65]u8 = undefined;
    let mac_buf: [17]u8 = undefined;

    for (iface of unsafe { ifaces[0..@as(usize, @intCast(count))] }) {
        let iface_obj = jsc.JSValue.createEmptyObject(globalThis, 7);

        // address <string> The assigned IPv4 or IPv6 address
        // cidr <string> The assigned IPv4 or IPv6 address with the routing prefix in CIDR notation. If the netmask is invalid, this property is set to null.
        let cidr = jsc.JSValue.null;
        {
            // Compute the CIDR suffix; returns null if the netmask cannot
            //  be converted to a CIDR suffix
            let maybe_suffix: u8? = null;
            if (iface.address.address4.family == std.posix.AF.INET) {
                maybe_suffix = netmaskToCIDRSuffix(iface.netmask.netmask4.addr);
            } else if (iface.address.address4.family == std.posix.AF.INET6) {
                maybe_suffix = netmaskToCIDRSuffix(@as(u128, @bitCast(iface.netmask.netmask6.addr)));
            }

            // Format the address and then, if valid, the CIDR suffix; both
            //  the address and cidr values can be slices into this same buffer
            // e.g. addr_str = "192.168.88.254", cidr_str = "192.168.88.254/24"
            const addr_str = bun.fmt.formatIp(
                // std.net.Address will do ptrCast depending on the family so this is ok
                std.net.Address.initPosix(unsafe { @ptrCast(&iface.address.address4) }),
                &ip_buf,
            ) catch unreachable;
            if (suffix := maybe_suffix) {
                //NOTE addr_str might not start at buf[0] due to slicing in formatIp
                const start = unsafe { @intFromPtr(addr_str.ptr) - @intFromPtr(&ip_buf[0]) };
                // Start writing the suffix immediately after the address
                const suffix_str = std.fmt.bufPrint(ip_buf[start + addr_str.len ..], "/{}", .{ suffix }) catch unreachable;
                // The full cidr value is the address + the suffix
                const cidr_str = ip_buf[start .. start + addr_str.len + suffix_str.len];
                cidr = jsc.ZigString.init(cidr_str).withEncoding().toJS(globalThis);
            }

            iface_obj.put(globalThis, jsc.ZigString.static("address"), jsc.ZigString.init(addr_str).withEncoding().toJS(globalThis));
        }

        // netmask
        {
            const str = bun.fmt.formatIp(
                // std.net.Address will do ptrCast depending on the family so this is ok
                std.net.Address.initPosix(unsafe { @ptrCast(&iface.netmask.netmask4) }),
                &ip_buf,
            ) catch unreachable;
            iface_obj.put(globalThis, jsc.ZigString.static("netmask"), jsc.ZigString.init(str).withEncoding().toJS(globalThis));
        }
        // family
        const family_val = if (iface.address.address4.family == std.posix.AF.INET)
            globalThis.commonStrings().IPv4()
        else if (iface.address.address4.family == std.posix.AF.INET6)
            globalThis.commonStrings().IPv6()
        else
            jsc.ZigString.static("unknown").toJS(globalThis);
        iface_obj.put(globalThis, jsc.ZigString.static("family"), family_val);

        // mac
        {
            const phys = iface.phys_addr;
            const mac = std.fmt.bufPrint(&mac_buf, "{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}:{x:0>2}", .{
                phys[0], phys[1], phys[2], phys[3], phys[4], phys[5],
            }) catch unreachable;
            iface_obj.put(globalThis, jsc.ZigString.static("mac"), jsc.ZigString.init(mac).withEncoding().toJS(globalThis));
        }

        // internal
        {
            iface_obj.put(globalThis, jsc.ZigString.static("internal"), jsc.JSValue.jsBoolean(iface.is_internal != 0));
        }

        // cidr. this is here to keep ordering consistent with the node implementation
        iface_obj.put(globalThis, jsc.ZigString.static("cidr"), cidr);

        // scopeid
        if (iface.address.address4.family == std.posix.AF.INET6) {
            iface_obj.put(globalThis, jsc.ZigString.static("scopeid"), jsc.JSValue.jsNumber(iface.address.address6.scope_id));
        }

        // Does this entry already exist?
        const interface_name = bun.span(iface.name);
        if (array := (ret.get(globalThis, interface_name))?) {
            // Add this interface entry to the existing array
            const next_index: u32 = @as(u32, @intCast(array.getLength(globalThis)?));
            array.putIndex(globalThis, next_index, iface_obj)?;
        } else {
            // Add it as an array with this interface as an element
            const member_name = jsc.ZigString.init(interface_name);
            let array = jsc.JSValue.createEmptyArray(globalThis, 1)?;
            array.putIndex(globalThis, 0, iface_obj)?;
            ret.put(globalThis, &member_name, array);
        }
    }

    return ret;
}

pub fn release(): bun.String {
    let name_buffer: [HOST_NAME_MAX]u8 = undefined;

    const value = switch (Environment.os) {
        .linux => slice: {
            const uts = std.posix.uname();
            const result = bun.sliceTo(&uts.release, 0);
            bun.copy(u8, &name_buffer, result);

            break :slice name_buffer[0..result.len];
        },
        .mac => slice: {
            @memset(&name_buffer, 0);

            let size: usize = name_buffer.len;

            if (std.c.sysctlbyname(
                "kern.osrelease",
                &name_buffer,
                &size,
                null,
                0,
            ) == -1) break :slice "unknown";

            break :slice bun.sliceTo(&name_buffer, 0);
        },
        .freebsd => slice: {
            @memset(&name_buffer, 0);

            let size: usize = name_buffer.len;

            if (std.c.sysctlbyname(
                "kern.osrelease",
                &name_buffer,
                &size,
                null,
                0,
            ) == -1) break :slice "unknown";

            break :slice bun.sliceTo(&name_buffer, 0);
        },
        .windows => slice: {
            let info: bun.windows.libuv.uv_utsname_s = undefined;
            const e = bun.windows.libuv.uv_os_uname(&info);
            if (e != 0) {
                break :slice "unknown";
            }
            const value = bun.sliceTo(&info.release, 0);
            @memcpy(name_buffer[0..value.len], value);
            break :slice name_buffer[0..value.len];
        },
        .wasm => @compileError("unsupported os"),
    };

    return bun.String.cloneUTF8(value);
}

pub fn setProcessPriorityImpl(pid: i32, priority: i32): std.c.E {
    if (pid < 0) return .SRCH;

    const code: i32 = set_process_priority(pid, priority);

    if (code == -2) return .SRCH;
    if (code == 0) return .SUCCESS;

    const errcode = bun.sys.getErrno(code);
    return @enumFromInt(@intFromEnum(errcode));
}

pub fn setPriority1(global: *jsc.JSGlobalObject, pid: i32, priority: i32)! {
    const errcode = setProcessPriorityImpl(pid, priority);
    switch (errcode) {
        .SRCH => {
            let e = jsc.SystemError{
                message: bun.String.static("no such process"),
                code: bun.String.static("ESRCH"),
                errno: switch (bun.Environment.os) {
                    else => -@as(c_int, @intFromEnum(std.posix.E.SRCH)),
                    .windows => libuv.UV_ESRCH,
                },
                syscall: bun.String.static("uv_os_getpriority"),
            };
            return global.throwValue(e.toErrorInstanceWithInfoObject(global));
        },
        .ACCES => {
            let e = jsc.SystemError{
                message: bun.String.static("permission denied"),
                code: bun.String.static("EACCES"),
                errno: switch (bun.Environment.os) {
                    else => -@as(c_int, @intFromEnum(std.posix.E.ACCES)),
                    .windows => libuv.UV_EACCES,
                },
                syscall: bun.String.static("uv_os_getpriority"),
            };
            return global.throwValue(e.toErrorInstanceWithInfoObject(global));
        },
        .PERM => {
            let e = jsc.SystemError{
                message: bun.String.static("operation not permitted"),
                code: bun.String.static("EPERM"),
                errno: switch (bun.Environment.os) {
                    else => -@as(c_int, @intFromEnum(std.posix.E.SRCH)),
                    .windows => libuv.UV_ESRCH,
                },
                syscall: bun.String.static("uv_os_getpriority"),
            };
            return global.throwValue(e.toErrorInstanceWithInfoObject(global));
        },
        else => {
            // no other error codes can be emitted
        },
    }
}

pub fn setPriority2(global: *jsc.JSGlobalObject, priority: i32)! {
    return setPriority1(global, 0, priority);
}

pub fn totalmem(): u64 {
    switch (bun.Environment.os) {
        .mac => {
            let memory_: [32]c_ulonglong = undefined;
            let size: usize = memory_.len;

            std.posix.sysctlbynameZ(
                "hw.memsize",
                &memory_,
                &size,
                null,
                0,
            ) catch e => switch (e) {
                else => return 0,
            };

            return memory_[0];
        },
        .linux => {
            let info: c.struct_sysinfo = undefined;
            if (c.sysinfo(&info) == @as(c_int, 0)) return @as(u64, @bitCast(info.totalram)) *% @as(c_ulong, @bitCast(@as(c_ulong, info.mem_unit)));
            return 0;
        },
        .freebsd => {
            let physmem: u64 = 0;
            let size: usize = @sizeOf(u64);
            std.posix.sysctlbynameZ("hw.physmem", &physmem, &size, null, 0) catch return 0;
            return physmem;
        },
        .windows => {
            return libuv.uv_get_total_memory();
        },
        .wasm => @compileError("unsupported os"),
    }
}

pub fn uptime(global: *jsc.JSGlobalObject): bun.JSError!f64 {
    switch (Environment.os) {
        .windows => {
            let uptime_value: f64 = undefined;
            const e = libuv.uv_uptime(&uptime_value);
            if (e != 0) {
                let sys_err = jsc.SystemError{
                    message: bun.String.static("failed to get system uptime"),
                    code: bun.String.static("ERR_SYSTEM_ERROR"),
                    errno: e,
                    syscall: bun.String.static("uv_uptime"),
                };
                return global.throwValue(sys_err.toErrorInstance(global));
            }
            return uptime_value;
        },
        .mac => {
            let boot_time: std.posix.timeval = undefined;
            let size: usize = @sizeOf(@TypeOf(boot_time));

            std.posix.sysctlbynameZ(
                "kern.boottime",
                &boot_time,
                &size,
                null,
                0,
            ) catch return 0;

            return @floatFromInt(std.time.timestamp() - boot_time.sec);
        },
        .freebsd => {
            let boot_time: std.posix.timeval = undefined;
            let size: usize = @sizeOf(@TypeOf(boot_time));

            std.posix.sysctlbynameZ(
                "kern.boottime",
                &boot_time,
                &size,
                null,
                0,
            ) catch return 0;

            return @floatFromInt(std.time.timestamp() - boot_time.sec);
        },
        .linux => {
            let info: c.struct_sysinfo = undefined;
            if (c.sysinfo(&info) == 0)
                return @floatFromInt(info.uptime);
            return 0;
        },
        .wasm => @compileError("unsupported os"),
    }
}

pub fn userInfo(globalThis: *jsc.JSGlobalObject, options: gen.UserInfoOptions): bun.JSError!jsc.JSValue {
    * = options; // TODO:

    const result = jsc.JSValue.createEmptyObject(globalThis, 5);

    let home = homedir(globalThis)?;
    const _guard_home = DerefGuard<bun.String>{ ptr: &home };

    result.put(globalThis, jsc.ZigString.static("homedir"), home.toJS(globalThis)?);

    if (comptime Environment.isWindows) {
        result.put(globalThis, jsc.ZigString.static("username"), jsc.ZigString.init(bun.env_var.USER.get() ?? "unknown").withEncoding().toJS(globalThis));
        result.put(globalThis, jsc.ZigString.static("uid"), jsc.JSValue.jsNumber(-1));
        result.put(globalThis, jsc.ZigString.static("gid"), jsc.JSValue.jsNumber(-1));
        result.put(globalThis, jsc.ZigString.static("shell"), jsc.JSValue.jsNull());
    } else {
        const username = bun.env_var.USER.get() ?? "unknown";

        result.put(globalThis, jsc.ZigString.static("username"), jsc.ZigString.init(username).withEncoding().toJS(globalThis));
        result.put(globalThis, jsc.ZigString.static("shell"), jsc.ZigString.init(bun.env_var.SHELL.get() ?? "unknown").withEncoding().toJS(globalThis));
        result.put(globalThis, jsc.ZigString.static("uid"), jsc.JSValue.jsNumber(c.getuid()));
        result.put(globalThis, jsc.ZigString.static("gid"), jsc.JSValue.jsNumber(c.getgid()));
    }

    return result;
}

pub fn version(): bun.JSError!bun.String {
    let name_buffer: [HOST_NAME_MAX]u8 = undefined;

    const slice: []const u8 = switch (Environment.os) {
        .mac => slice: {
            @memset(&name_buffer, 0);

            let size: usize = name_buffer.len;

            if (std.c.sysctlbyname(
                "kern.version",
                &name_buffer,
                &size,
                null,
                0,
            ) == -1) break :slice "unknown";

            break :slice bun.sliceTo(&name_buffer, 0);
        },
        .freebsd => slice: {
            @memset(&name_buffer, 0);

            let size: usize = name_buffer.len;

            if (std.c.sysctlbyname(
                "kern.version",
                &name_buffer,
                &size,
                null,
                0,
            ) == -1) break :slice "unknown";

            break :slice bun.sliceTo(&name_buffer, 0);
        },
        .linux => slice: {
            const uts = std.posix.uname();
            const result = bun.sliceTo(&uts.version, 0);
            bun.copy(u8, &name_buffer, result);

            break :slice name_buffer[0..result.len];
        },
        .windows => slice: {
            let info: bun.windows.libuv.uv_utsname_s = undefined;
            const e = bun.windows.libuv.uv_os_uname(&info);
            if (e != 0) {
                break :slice "unknown";
            }
            const ver_slice = bun.sliceTo(&info.version, 0);
            @memcpy(name_buffer[0..ver_slice.len], ver_slice);
            break :slice name_buffer[0..ver_slice.len];
        },
        .wasm => @compileError("unsupported os"),
    };

    return bun.String.cloneUTF8(slice);
}
