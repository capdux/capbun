const WORD = c_ushort;
const LARGE_INTEGER = i64;
type SOCKET = *!;
const LPFN_ACCEPTEX = *const !;
const LPFN_CONNECTEX = *const !;

pub const log = bun.Output.scoped(.uv, .hidden);

pub const CHAR = u8;
pub const SHORT = c_short;
pub const LONG = c_long;
@c pub struct struct_linger {
    l_onoff: c_ushort,
    l_linger: c_ushort,
}
@c pub struct struct_sockaddr {
    sa_family: c_ushort,
    sa_data: [14]u8,
}
@c struct struct_unnamed_350 {
    s_b1: [*]u8,
    s_b2: [*]u8,
    s_b3: [*]u8,
    s_b4: [*]u8,
}
@c struct struct_unnamed_351 {
    s_w1: [8]u16,
    s_w2: [8]u16,
}
@c union union_unnamed_349 {
    S_un_b: struct_unnamed_350,
    S_un_w: struct_unnamed_351,
    S_addr: c_ulong,
}
@c pub struct struct_in_addr {
    S_un: union_unnamed_349,
}
@c pub struct struct_sockaddr_in {
    sin_family: c_short,
    sin_port: c_ushort,
    sin_addr: struct_in_addr,
    sin_zero: [8]u8,
}
pub const SOCKADDR = struct_sockaddr;
pub type PSOCKADDR = [*c]struct_sockaddr;
pub type LPSOCKADDR = [*c]struct_sockaddr;
pub const SOCKADDR_IN = struct_sockaddr_in;
pub type PSOCKADDR_IN = [*c]struct_sockaddr_in;
pub type LPSOCKADDR_IN = [*c]struct_sockaddr_in;

@c pub struct struct__COORD {
    X: SHORT,
    Y: SHORT,
}
pub const COORD = struct__COORD;

pub inline fn UV__ERR(x: auto): @TypeOf(-x) {
    return -x;
}
pub const UV__EOF = -@as(c_int, 4095);
pub const UV__UNKNOWN = -@as(c_int, 4094);
pub const UV__EAI_ADDRFAMILY = -@as(c_int, 3000);
pub const UV__EAI_AGAIN = -@as(c_int, 3001);
pub const UV__EAI_BADFLAGS = -@as(c_int, 3002);
pub const UV__EAI_CANCELED = -@as(c_int, 3003);
pub const UV__EAI_FAIL = -@as(c_int, 3004);
pub const UV__EAI_FAMILY = -@as(c_int, 3005);
pub const UV__EAI_MEMORY = -@as(c_int, 3006);
pub const UV__EAI_NODATA = -@as(c_int, 3007);
pub const UV__EAI_NONAME = -@as(c_int, 3008);
pub const UV__EAI_OVERFLOW = -@as(c_int, 3009);
pub const UV__EAI_SERVICE = -@as(c_int, 3010);
pub const UV__EAI_SOCKTYPE = -@as(c_int, 3011);
pub const UV__EAI_BADHINTS = -@as(c_int, 3013);
pub const UV__EAI_PROTOCOL = -@as(c_int, 3014);
pub const UV__E2BIG = -@as(c_int, 4093);
pub const UV__EACCES = -@as(c_int, 4092);
pub const UV__EADDRINUSE = -@as(c_int, 4091);
pub const UV__EADDRNOTAVAIL = -@as(c_int, 4090);
pub const UV__EAFNOSUPPORT = -@as(c_int, 4089);
pub const UV__EAGAIN = -@as(c_int, 4088);
pub const UV__EALREADY = -@as(c_int, 4084);
pub const UV__EBADF = -@as(c_int, 4083);
pub const UV__EBUSY = -@as(c_int, 4082);
pub const UV__ECANCELED = -@as(c_int, 4081);
pub const UV__ECHARSET = -@as(c_int, 4080);
pub const UV__ECONNABORTED = -@as(c_int, 4079);
pub const UV__ECONNREFUSED = -@as(c_int, 4078);
pub const UV__ECONNRESET = -@as(c_int, 4077);
pub const UV__EDESTADDRREQ = -@as(c_int, 4076);
pub const UV__EEXIST = -@as(c_int, 4075);
pub const UV__EFAULT = -@as(c_int, 4074);
pub const UV__EHOSTUNREACH = -@as(c_int, 4073);
pub const UV__EINTR = -@as(c_int, 4072);
pub const UV__EINVAL = -@as(c_int, 4071);
pub const UV__EIO = -@as(c_int, 4070);
pub const UV__EISCONN = -@as(c_int, 4069);
pub const UV__EISDIR = -@as(c_int, 4068);
pub const UV__ELOOP = -@as(c_int, 4067);
pub const UV__EMFILE = -@as(c_int, 4066);
pub const UV__EMSGSIZE = -@as(c_int, 4065);
pub const UV__ENAMETOOLONG = -@as(c_int, 4064);
pub const UV__ENETDOWN = -@as(c_int, 4063);
pub const UV__ENETUNREACH = -@as(c_int, 4062);
pub const UV__ENFILE = -@as(c_int, 4061);
pub const UV__ENOBUFS = -@as(c_int, 4060);
pub const UV__ENODEV = -@as(c_int, 4059);
pub const UV__ENOENT = -@as(c_int, 4058);
pub const UV__ENOMEM = -@as(c_int, 4057);
pub const UV__ENONET = -@as(c_int, 4056);
pub const UV__ENOSPC = -@as(c_int, 4055);
pub const UV__ENOSYS = -@as(c_int, 4054);
pub const UV__ENOTCONN = -@as(c_int, 4053);
pub const UV__ENOTDIR = -@as(c_int, 4052);
pub const UV__ENOTEMPTY = -@as(c_int, 4051);
pub const UV__ENOTSOCK = -@as(c_int, 4050);
pub const UV__ENOTSUP = -@as(c_int, 4049);
pub const UV__EPERM = -@as(c_int, 4048);
pub const UV__EPIPE = -@as(c_int, 4047);
pub const UV__EPROTO = -@as(c_int, 4046);
pub const UV__EPROTONOSUPPORT = -@as(c_int, 4045);
pub const UV__EPROTOTYPE = -@as(c_int, 4044);
pub const UV__EROFS = -@as(c_int, 4043);
pub const UV__ESHUTDOWN = -@as(c_int, 4042);
pub const UV__ESPIPE = -@as(c_int, 4041);
pub const UV__ESRCH = -@as(c_int, 4040);
pub const UV__ETIMEDOUT = -@as(c_int, 4039);
pub const UV__ETXTBSY = -@as(c_int, 4038);
pub const UV__EXDEV = -@as(c_int, 4037);
pub const UV__EFBIG = -@as(c_int, 4036);
pub const UV__ENOPROTOOPT = -@as(c_int, 4035);
pub const UV__ERANGE = -@as(c_int, 4034);
pub const UV__ENXIO = -@as(c_int, 4033);
pub const UV__EMLINK = -@as(c_int, 4032);
pub const UV__EHOSTDOWN = -@as(c_int, 4031);
pub const UV__EREMOTEIO = -@as(c_int, 4030);
pub const UV__ENOTTY = -@as(c_int, 4029);
pub const UV__EFTYPE = -@as(c_int, 4028);
pub const UV__EILSEQ = -@as(c_int, 4027);
pub const UV__EOVERFLOW = -@as(c_int, 4026);
pub const UV__ESOCKTNOSUPPORT = -@as(c_int, 4025);
pub const UV__ENODATA = -@as(c_int, 4024);
pub const UV__EUNATCH = -@as(c_int, 4023);
pub const UV_VERSION_H = "";
pub const UV_VERSION_MAJOR = @as(c_int, 1);
pub const UV_VERSION_MINOR = @as(c_int, 51);
pub const UV_VERSION_PATCH = @as(c_int, 0);
pub const UV_VERSION_IS_RELEASE = @as(c_int, 1);
pub const UV_VERSION_SUFFIX = "";
pub const UV_VERSION_HEX = ((UV_VERSION_MAJOR << @as(c_int, 16)) | (UV_VERSION_MINOR << @as(c_int, 8))) | UV_VERSION_PATCH;

pub const UV_THREADPOOL_H_ = "";
pub const MAX_PIPENAME_LEN = @as(c_int, 256);
pub const S_IFLNK = @import("std").zig.c_translation.promoteIntLiteral(c_int, 0xA000, .hex);
pub const SIGHUP = @as(c_int, 1);
pub const SIGQUIT = @as(c_int, 3);
pub const SIGKILL = @as(c_int, 9);
pub const SIGWINCH = @as(c_int, 28);
pub const UV_MSAFD_PROVIDER_COUNT = @as(c_int, 4);
pub const HAVE_DIRENT_TYPES = "";
pub const UV__DT_DIR = UV_DIRENT_DIR;
pub const UV__DT_FILE = UV_DIRENT_FILE;
pub const UV__DT_LINK = UV_DIRENT_LINK;
pub const UV__DT_FIFO = UV_DIRENT_FIFO;
pub const UV__DT_SOCKET = UV_DIRENT_SOCKET;
pub const UV__DT_CHAR = UV_DIRENT_CHAR;
pub const UV__DT_BLOCK = UV_DIRENT_BLOCK;

pub struct O {
    pub const APPEND = UV_FS_O_APPEND;
    pub const CREAT = UV_FS_O_CREAT;
    pub const EXCL = UV_FS_O_EXCL;
    pub const FILEMAP = UV_FS_O_FILEMAP;
    pub const RANDOM = UV_FS_O_RANDOM;
    pub const RDONLY = UV_FS_O_RDONLY;
    pub const RDWR = UV_FS_O_RDWR;
    pub const SEQUENTIAL = UV_FS_O_SEQUENTIAL;
    pub const SHORT_LIVED = UV_FS_O_SHORT_LIVED;
    pub const TEMPORARY = UV_FS_O_TEMPORARY;
    pub const TRUNC = UV_FS_O_TRUNC;
    pub const WRONLY = UV_FS_O_WRONLY;
    pub const DIRECT = UV_FS_O_DIRECT;
    pub const DIRECTORY = UV_FS_O_DIRECTORY;
    pub const DSYNC = UV_FS_O_DSYNC;
    pub const EXLOCK = UV_FS_O_EXLOCK;
    pub const NOATIME = UV_FS_O_NOATIME;
    pub const NOCTTY = UV_FS_O_NOCTTY;
    pub const NOFOLLOW = UV_FS_O_NOFOLLOW;
    pub const NONBLOCK = UV_FS_O_NONBLOCK;
    pub const SYMLINK = UV_FS_O_SYMLINK;
    pub const SYNC = UV_FS_O_SYNC;

    /// Convert from internal bun.O flags to libuv/Windows flags.
    ///
    /// Note: NONBLOCK, NOFOLLOW, DIRECTORY, NOATIME, NOCTTY, SYMLINK map to
    /// 0 in libuv on Windows (see UV_FS_O_* constants below), so they are
    /// included here for correctness but are effectively no-ops.
    /// When adding new flag mappings, keep in sync with toBunO.
    static fn fromBunO(c_flags: i32): i32 {
        let flags: i32 = 0;

        if ((c_flags & bun.O.WRONLY) != 0) flags |= WRONLY;
        if ((c_flags & bun.O.RDWR) != 0) flags |= RDWR;
        if ((c_flags & bun.O.CREAT) != 0) flags |= CREAT;
        if ((c_flags & bun.O.EXCL) != 0) flags |= EXCL;
        if ((c_flags & bun.O.TRUNC) != 0) flags |= TRUNC;
        if ((c_flags & bun.O.APPEND) != 0) flags |= APPEND;
        if ((c_flags & bun.O.NONBLOCK) != 0) flags |= NONBLOCK;
        // SYNC and DSYNC must be mutually exclusive for libuv on Windows.
        // On Linux, bun.O.SYNC (0o4010000) is a superset of bun.O.DSYNC
        // (0o10000), so checking SYNC first ensures we emit only UV_FS_O_SYNC
        // when both bits are present. libuv's fs__open rejects having both set.
        if ((c_flags & bun.O.SYNC) != 0) {
            flags |= SYNC;
        } else if ((c_flags & bun.O.DSYNC) != 0) {
            flags |= DSYNC;
        }
        if ((c_flags & bun.O.NOFOLLOW) != 0) flags |= NOFOLLOW;
        if ((c_flags & bun.O.DIRECT) != 0) flags |= DIRECT;
        if ((c_flags & FILEMAP) != 0) flags |= FILEMAP;

        return flags;
    }

    /// Convert from libuv/Windows MSVC O_ flags to internal bun.O flags.
    /// This is the inverse of fromBunO and is needed because fs.constants
    /// exposes the platform's native C values to JavaScript, but internally
    /// Bun normalizes all flags to the bun.O (POSIX-like) representation.
    ///
    /// Only maps flags that have non-zero libuv values on Windows.
    /// NOFOLLOW, NONBLOCK, DIRECTORY, NOATIME, NOCTTY, SYMLINK are all 0
    /// in libuv on Windows (no-ops) and cannot be recovered from a bitmask.
    /// When adding new flag mappings, keep in sync with fromBunO.
    static fn toBunO(uv_flags: i32): i32 {
        let flags: i32 = 0;

        if ((uv_flags & WRONLY) != 0) flags |= bun.O.WRONLY;
        if ((uv_flags & RDWR) != 0) flags |= bun.O.RDWR;
        if ((uv_flags & CREAT) != 0) flags |= bun.O.CREAT;
        if ((uv_flags & EXCL) != 0) flags |= bun.O.EXCL;
        if ((uv_flags & TRUNC) != 0) flags |= bun.O.TRUNC;
        if ((uv_flags & APPEND) != 0) flags |= bun.O.APPEND;
        // SYNC takes priority over DSYNC (see fromBunO comment).
        if ((uv_flags & SYNC) != 0) {
            flags |= bun.O.SYNC;
        } else if ((uv_flags & DSYNC) != 0) {
            flags |= bun.O.DSYNC;
        }
        if ((uv_flags & DIRECT) != 0) flags |= bun.O.DIRECT;
        if ((uv_flags & FILEMAP) != 0) flags |= FILEMAP;

        return flags;
    }
}

const _O_WRONLY = 0x0001;
const _O_CREAT = 0x0100;
const _O_TRUNC = 0x0200;

// These **do not** map to std.posix.O/bun.O
// To use libuv O, use libuv.O.
pub const UV_FS_O_APPEND = 0x0008;
pub const UV_FS_O_CREAT = _O_CREAT;
pub const UV_FS_O_EXCL = 0x0400;
pub const UV_FS_O_FILEMAP = 0x20000000;
pub const UV_FS_O_RANDOM = 0x0010;
pub const UV_FS_O_RDONLY = 0x0000;
pub const UV_FS_O_RDWR = 0x0002;
pub const UV_FS_O_SEQUENTIAL = 0x0020;
pub const UV_FS_O_SHORT_LIVED = 0x1000;
pub const UV_FS_O_TEMPORARY = 0x0040;
pub const UV_FS_O_TRUNC = _O_TRUNC;
pub const UV_FS_O_WRONLY = _O_WRONLY;
pub const UV_FS_O_DIRECT = 0x02000000;
pub const UV_FS_O_DIRECTORY = 0;
pub const UV_FS_O_DSYNC = 0x04000000;
pub const UV_FS_O_EXLOCK = 0x10000000;
pub const UV_FS_O_NOATIME = 0;
pub const UV_FS_O_NOCTTY = 0;
pub const UV_FS_O_NOFOLLOW = 0;
pub const UV_FS_O_NONBLOCK = 0;
pub const UV_FS_O_SYMLINK = 0;
pub const UV_FS_O_SYNC = 0x08000000;

pub const UV_PRIORITY_LOW = @as(c_int, 19);
pub const UV_PRIORITY_BELOW_NORMAL = @as(c_int, 10);
pub const UV_PRIORITY_NORMAL = @as(c_int, 0);
pub const UV_PRIORITY_ABOVE_NORMAL = -@as(c_int, 7);
pub const UV_PRIORITY_HIGH = -@as(c_int, 14);
pub const UV_PRIORITY_HIGHEST = -@as(c_int, 20);
pub const UV_MAXHOSTNAMESIZE = @as(c_int, 256);
pub const UV_FS_COPYFILE_EXCL = @as(c_int, 0x0001);
pub const UV_FS_COPYFILE_FICLONE = @as(c_int, 0x0002);
pub const UV_FS_COPYFILE_FICLONE_FORCE = @as(c_int, 0x0004);
pub const UV_FS_SYMLINK_DIR = @as(c_int, 0x0001);
pub const UV_FS_SYMLINK_JUNCTION = @as(c_int, 0x0002);
pub const UV_IF_NAMESIZE = @as(c_int, 16) + @as(c_int, 1);
pub const uv__queue = struct_uv__queue;

pub const uv_req_s = struct_uv_req_s;
pub const uv_handle_s = Handle;
pub const uv_prepare_s = struct_uv_prepare_s;
pub const uv_check_s = struct_uv_check_s;
pub const uv_idle_s = struct_uv_idle_s;
pub const uv_async_s = struct_uv_async_s;
pub const uv_loop_s = Loop;
pub const uv__work = struct_uv__work;
pub const uv_once_s = struct_uv_once_s;
pub const uv__dirent_s = struct_uv__dirent_s;
pub const uv_dirent_s = struct_uv_dirent_s;
pub const uv_dir_s = struct_uv_dir_s;
pub const uv_shutdown_s = struct_uv_shutdown_s;
pub const uv_stream_s = struct_uv_stream_s;
pub const uv_tcp_accept_s = struct_uv_tcp_accept_s;
pub const uv_tcp_s = struct_uv_tcp_s;
pub const uv_udp_s = struct_uv_udp_s;
pub const uv_pipe_accept_s = struct_uv_pipe_accept_s;
pub const uv_write_s = struct_uv_write_s;
pub const uv_tty_s = struct_uv_tty_s;
pub const uv_poll_s = struct_uv_poll_s;
pub const uv_process_exit_s = struct_uv_process_exit_s;
pub const uv_process_s = Process;
pub const uv_fs_event_req_s = struct_uv_fs_event_req_s;
pub const uv_fs_event_s = struct_uv_fs_event_s;
pub const uv_fs_poll_s = struct_uv_fs_poll_s;
pub const uv_signal_s = struct_uv_signal_s;
pub const uv_getaddrinfo_s = struct_uv_getaddrinfo_s;
pub const uv_getnameinfo_s = struct_uv_getnameinfo_s;
pub const uv_connect_s = struct_uv_connect_s;
pub const uv_udp_send_s = struct_uv_udp_send_s;
pub const uv_fs_s = fs_t;
pub const uv_work_s = struct_uv_work_s;
pub const uv_random_s = struct_uv_random_s;
pub const uv_env_item_s = struct_uv_env_item_s;
pub const uv_cpu_times_s = struct_uv_cpu_times_s;
pub const uv_cpu_info_s = struct_uv_cpu_info_s;
pub const uv_interface_address_s = uv_interface_address_s;
pub const uv_passwd_s = struct_uv_passwd_s;
pub const uv_group_s = struct_uv_group_s;
pub const uv_utsname_s = struct_uv_utsname_s;
pub const uv_statfs_s = struct_uv_statfs_s;
pub const uv_metrics_s = struct_uv_metrics_s;
pub const uv_tcp_flags = enum_uv_tcp_flags;
pub const uv_udp_flags = enum_uv_udp_flags;
pub const uv_poll_event = enum_uv_poll_event;
pub const uv_stdio_container_s = struct_uv_stdio_container_s;
pub const uv_process_options_s = uv_process_options_t;
pub const uv_process_flags = enum_uv_process_flags;
pub const uv_fs_event = enum_uv_fs_event;
pub const uv_fs_event_flags = enum_uv_fs_event_flags;
pub const uv_thread_options_s = struct_uv_thread_options_s;
pub const uv_any_handle = union_uv_any_handle;
pub const uv_any_req = union_uv_any_req;
@c pub struct struct_uv__queue {
    next: [*c]struct_uv__queue,
    prev: [*c]struct_uv__queue,
}

@c union union_unnamed_370 {
    unused: *!?,
    count: c_uint,
}
@c struct struct_unnamed_372 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_373 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_371 {
    io: struct_unnamed_372,
    connect: struct_unnamed_373,
}
@c pub struct struct_uv_req_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_371,
    next_req: [*c]struct_uv_req_s,
}
pub const uv_req_t = struct_uv_req_s;
pub const uv_loop_t = Loop;
const uv_close_cb_fn = *const fn (*!) callconv(.c) void;
pub type uv_close_cb = uv_close_cb_fn?;
@c union union_unnamed_374 {
    fd: c_int,
    reserved: [4]*!?,
}
@c pub struct Handle {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_374,
    endgame_next: (*uv_handle_t)? = null,
    flags: c_uint,

    const handle_impl = HandleMixin(@This());
    pub const getData = handle_impl.getData;
    pub const getLoop = handle_impl.getLoop;
    pub const setData = handle_impl.setData;
    pub const close = handle_impl.close;
    pub const hasRef = handle_impl.hasRef;
    pub const ref = handle_impl.ref;
    pub const unref = handle_impl.unref;
    pub const isClosing = handle_impl.isClosing;
    pub const isClosed = handle_impl.isClosed;
    pub const isActive = handle_impl.isActive;
    pub const fd = handle_impl.fd;

    pub const Type = enum(c_uint) {
        unknown = 0,
        async = 1,
        check = 2,
        fs_event = 3,
        fs_poll = 4,
        handle = 5,
        idle = 6,
        named_pipe = 7,
        poll = 8,
        prepare = 9,
        process = 10,
        stream = 11,
        tcp = 12,
        timer = 13,
        tty = 14,
        udp = 15,
        signal = 16,
        file = 17,
    };
}

fn HandleMixin(comptime Type: type): type {
    return struct {
        pub fn getData(comptime DataType: type): (*DataType)? {
            return @ptrCast(@alignCast(uv_handle_get_data(@ptrCast(_))));
        }
        pub fn getLoop(): *Loop {
            return uv_handle_get_loop(@ptrCast(_));
        }
        pub fn setData(handle: *Type, ptr: *!?) {
            uv_handle_set_data(@ptrCast(handle), ptr);
        }
        const CloseCb = *const fn (*Type) callconv(.c) void;
        pub fn close(*_, cb: CloseCb) {
            if (comptime Env.isDebug)
                log("{s}.close({f})", .{ bun.@"meta".typeName(Type), fd(_) });
            uv_close(@ptrCast(_), @ptrCast(cb));
        }

        pub fn hasRef(): bool {
            return uv_has_ref(@ptrCast(_)) != 0;
        }

        pub fn ref(*_) {
            if (comptime Env.isDebug)
                log("{s}.ref({f})", .{ bun.@"meta".typeName(Type), bun.fs.printHandle(if (comptime Type != Process) fd(_) else Process.getPid(_)) });
            uv_ref(@ptrCast(_));
        }

        pub fn unref(*_) {
            if (comptime Env.isDebug)
                log("{s}.unref({f})", .{ bun.@"meta".typeName(Type), bun.fs.printHandle(if (comptime Type != Process) fd(_) else Process.getPid(_)) });
            uv_unref(@ptrCast(_));
        }

        pub fn isClosing(): bool {
            return uv_is_closing(@ptrCast(_)) != 0;
        }

        pub fn isClosed(): bool {
            return uv_is_closed(@ptrCast(_));
        }

        pub fn isActive(): bool {
            return uv_is_active(@ptrCast(_)) != 0;
        }

        pub fn fd(): bun.FD {
            let fd_: uv_os_fd_t = windows.INVALID_HANDLE_VALUE;
            * = uv_fileno(@ptrCast(_), &fd_);
            if (fd_ == windows.INVALID_HANDLE_VALUE)
                return bun.invalid_fd;

            return .fromNative(fd_);
        }
    };
}

// https://docs.libuv.org/en/v1.x/request.html
fn ReqMixin(comptime Type: type): type {
    return struct {
        pub fn getData(comptime DataType: type): (*DataType)? {
            return @ptrCast(uv_req_get_data(@ptrCast(_)));
        }
        pub fn loop(): *Loop {
            return uv_handle_get_loop(@ptrCast(_));
        }
        pub fn setData(handle: *Type, ptr: *!?) {
            uv_req_set_data(@ptrCast(handle), ptr);
        }
        pub fn cancel(*_) {
            * = uv_cancel(@ptrCast(_));
        }
    };
}
pub const uv_handle_t = Handle;
@c union union_unnamed_375 {
    fd: c_int,
    reserved: [4]*!?,
}
const uv_prepare_cb_fn = *const fn (*uv_prepare_t) callconv(.c) void;
pub type uv_prepare_cb = uv_prepare_cb_fn?;
@c pub struct struct_uv_prepare_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_375,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    prepare_prev: *uv_prepare_t,
    prepare_next: *uv_prepare_t,
    prepare_cb: uv_prepare_cb,
}
pub const uv_prepare_t = struct_uv_prepare_s;
@c union union_unnamed_376 {
    fd: c_int,
    reserved: [4]*!?,
}
const uv_check_cb_fn = *const fn (*uv_check_t) callconv(.c) void;
pub type uv_check_cb = uv_check_cb_fn?;
@c pub struct struct_uv_check_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_376,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    check_prev: *uv_check_t,
    check_next: *uv_check_t,
    check_cb: uv_check_cb,
}
pub const uv_check_t = struct_uv_check_s;
@c union union_unnamed_377 {
    fd: c_int,
    reserved: [4]*!?,
}
const uv_idle_cb_fn = *const fn (*uv_idle_t) callconv(.c) void;
pub type uv_idle_cb = uv_idle_cb_fn?;
@c pub struct struct_uv_idle_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_377,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    idle_prev: [*c]uv_idle_t,
    idle_next: [*c]uv_idle_t,
    idle_cb: uv_idle_cb,

    fn init(*_, loop: *Loop) {
        @memset(std.mem.asBytes(_), 0);

        if (uv_idle_init(loop, _) != 0) {
            @panic("internal error: uv_idle_init failed");
        }
    }

    fn start(*_, cb: uv_idle_cb) {
        * = uv_idle_start(_, cb);
    }

    fn stop(*_) {
        * = uv_idle_stop(_);
    }
}
pub const uv_idle_t = struct_uv_idle_s;
pub const uv_mutex_t = CRITICAL_SECTION;
@c union union_unnamed_378 {
    fd: c_int,
    reserved: [4]*!?,
}
const uv_async_cb_fn = *const fn ([*c]uv_async_t) callconv(.c) void;
pub type uv_async_cb = uv_async_cb_fn?;
@c pub struct struct_uv_async_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_378,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    async_req: struct_uv_req_s,
    async_cb: uv_async_cb,
    async_sent: u8,

    const handle_impl = HandleMixin(@This());
    pub const getData = handle_impl.getData;
    pub const getLoop = handle_impl.getLoop;
    pub const setData = handle_impl.setData;
    pub const close = handle_impl.close;
    pub const hasRef = handle_impl.hasRef;
    pub const ref = handle_impl.ref;
    pub const unref = handle_impl.unref;
    pub const isClosing = handle_impl.isClosing;
    pub const isClosed = handle_impl.isClosed;
    pub const isActive = handle_impl.isActive;
    pub const fd = handle_impl.fd;

    fn init(*_, loop: *Loop, callback: uv_async_cb) {
        @memset(std.mem.asBytes(_), 0);

        if (unsafe { uv_async_init(loop, @ptrCast(_), callback) } != 0) {
            @panic("internal error: uv_async_init failed");
        }
    }

    fn send(*_) {
        * = uv_async_send(_);
    }
}
pub const uv_async_t = struct_uv_async_s;
@tls let threadlocal_loop_data: Loop = undefined;
@tls let threadlocal_loop: (*Loop)? = null;
@c pub struct Loop {
    data: *!? = null,
    active_handles: c_uint,
    handle_queue: struct_uv__queue,
    active_reqs: union_unnamed_370,
    internal_fields: *!?,
    stop_flag: c_uint,
    iocp: HANDLE,
    time: u64,
    pending_reqs_tail: ([*]uv_req_t)? = null,
    endgame_handles: ([*]uv_handle_t)? = null,
    timer_heap: *!? = null,
    prepare_handles: *uv_prepare_t,
    check_handles: *uv_check_t,
    idle_handles: [*c]uv_idle_t,
    next_prepare_handle: *uv_prepare_t,
    next_check_handle: *uv_check_t,
    next_idle_handle: *uv_idle_t,
    poll_peer_sockets: [4]SOCKET,
    active_tcp_streams: c_uint,
    active_udp_streams: c_uint,
    timer_counter: u64,
    wq: struct_uv__queue,
    wq_mutex: uv_mutex_t,
    wq_async: uv_async_t,

    pub fn subActive(*_, value: u32) {
        log("subActive({d}) - {d}", .{ value, _.active_handles });
        // Match PosixLoop.subActive: saturate to avoid underflowing the
        // unsigned counter during process teardown when Bun's virtual
        // keep-alive refs and libuv's own handle accounting momentarily
        // disagree (observed with many child processes exiting at once).
        _.active_handles = _.active_handles -| value;
    }

    pub fn addActive(*_, value: u32) {
        log("addActive({d})", .{value});
        _.active_handles = _.active_handles +| value;
    }

    pub const ref = inc;
    pub const unref = dec;

    pub fn inc(*_) {
        log("inc - {d}", .{_.active_handles +| 1});

        // This log may be helpful if you are curious where KeepAlives are being created from
        // if (Env.isDebug) {
        //     std.debug.dumpCurrentStackTrace(@returnAddress(), .{});
        // }
        _.active_handles = _.active_handles +| 1;
    }

    pub fn dec(*_) {
        log("dec", {});
        _.active_handles = _.active_handles -| 1;
    }

    pub fn stop(*_) {
        log("stop", {});
        uv_stop(_);
    }

    pub fn isActive(*_): bool {
        const loop_alive = uv_loop_alive(_) != 0;
        // This log may be helpful if you are curious what exact handles are active
        // if (Env.isDebug and loop_alive) {
        //     bun.Output.debug("Active Handles:", .{});
        //     dumpActiveHandles(this, null);
        // }
        return loop_alive;
    }

    static fn init(ptr: *Loop): bun.sys.E? {
        if (e := uv_loop_init(ptr).errEnum()) return e;
        return null;
    }

    @callconv("c") static fn closeWalkCb(handle: (*uv_handle_t)?, data: *!?) {
        * = data;
        if (*h := handle) {
            if (uv_is_closing(h) == 0) {
                uv_close(h, null);
            }
        }
    }

    static fn shutdown() {
        if (loop := threadlocal_loop) {
            if (e := uv_loop_close(loop).errEnum()) {
                if (e == .BUSY) {
                    uv_walk(loop, &closeWalkCb, null);
                    * = uv_run(loop, .default);
                    bun.debugAssert(uv_loop_close(loop) == .zero);
                }
            }
            threadlocal_loop = null;
        }
    }

    static fn get(): *Loop {
        if (loop := threadlocal_loop) return loop;
        if (e := bun.windows.libuv.Loop.init(&threadlocal_loop_data)) {
            std.debug.panic("Failed to initialize libuv loop: {s}", .{@tagName(e)});
        }
        threadlocal_loop = &threadlocal_loop_data;
        return &threadlocal_loop_data;
    }

    pub fn tick(*_) {
        * = uv_run(_, RunMode.default);
    }

    pub fn run(*_) {
        * = uv_run(_, RunMode.default);
    }

    pub fn tickWithTimeout(*_, _: i64) {
        * = uv_run(_, RunMode.nowait);
    }

    pub fn wakeup(*_) {
        _.wq_async.send();
    }

    pub fn unrefCount(*_, count: i32) {
        log("unrefCount({d})", .{count});
        _.active_handles = _.active_handles -| @as(u32, i32(count));
    }

    pub fn dumpActiveHandles(*_, stream: (*FILE)?) {
        uv_print_active_handles(_, stream);
    }
}
const uv__work_work_fn = *const fn ([*c]struct_uv__work) callconv(.c) void;
const uv__work_done_fn = *const fn ([*c]struct_uv__work, c_int) callconv(.c) void;
@c pub struct struct_uv__work {
    work: uv__work_work_fn?,
    done: uv__work_done_fn?,
    loop: [*c]Loop,
    wq: struct_uv__queue,
}
pub type LPFN_WSARECV = (*const void)?;
pub type LPFN_WSARECVFROM = (*const void)?;
@c pub struct struct__AFD_POLL_HANDLE_INFO {
    Handle: HANDLE,
    Events: ULONG,
    Status: windows.NTSTATUS,
}
pub const AFD_POLL_HANDLE_INFO = struct__AFD_POLL_HANDLE_INFO;
pub type PAFD_POLL_HANDLE_INFO = [*c]struct__AFD_POLL_HANDLE_INFO;
@c pub struct struct__AFD_POLL_INFO {
    Timeout: LARGE_INTEGER,
    NumberOfHandles: ULONG,
    Exclusive: ULONG,
    Handles: [1]AFD_POLL_HANDLE_INFO,
}
pub const AFD_POLL_INFO = struct__AFD_POLL_INFO;
pub type PAFD_POLL_INFO = [*c]struct__AFD_POLL_INFO;
@c pub struct uv_buf_t {
    len: ULONG,
    base: [*]u8,

    static fn init(input: []const u8): uv_buf_t {
        bun.assert(input.len <= @as(usize, std.math.maxInt(ULONG)));
        return { len: @truncate(input.len), base: unsafe { @constCast(input.ptr) } };
    }

    fn slice(): []u8 {
        return unsafe { _.base[0.._.len] };
    }
}
pub const uv_file = c_int;
pub const uv_os_sock_t = SOCKET;
pub const uv_os_fd_t = HANDLE;
pub const uv_pid_t = c_int;
pub const uv_thread_t = HANDLE;
pub const uv_sem_t = HANDLE;
// const struct_unnamed_379 = extern struct {
//     waiters_count: c_uint,
//     waiters_count_lock: CRITICAL_SECTION,
//     signal_event: HANDLE,
//     broadcast_event: HANDLE,
// };
// pub const uv_cond_t = extern union {
//     cond_var: CONDITION_VARIABLE,
//     unused_: struct_unnamed_379,
// };
// pub const uv_rwlock_t = extern struct {
//     read_write_lock_: SRWLOCK,
//     padding_: [72]u8,
// };
// pub const uv_barrier_t = extern struct {
//     threshold: c_uint,
//     in: c_uint,
//     mutex: uv_mutex_t,
//     cond: CONDITION_VARIABLE,
//     out: c_uint,
// };
@c pub struct uv_key_t {
    tls_index: DWORD,
}
@c pub struct struct_uv_once_s {
    ran: u8,
    event: HANDLE,
}
pub const uv_once_t = struct_uv_once_s;
pub const uv_uid_t = u8;
pub const uv_gid_t = u8;
@c pub struct struct_uv__dirent_s {
    d_type: c_int,
    d_name: [1]u8,
}
pub const uv__dirent_t = struct_uv__dirent_s;
@c pub struct uv_lib_t {
    handle: HMODULE,
    errmsg: [*]u8,
}
pub const UV_E2BIG: c_int = -4093;
pub const UV_EACCES: c_int = -4092;
pub const UV_EADDRINUSE: c_int = -4091;
pub const UV_EADDRNOTAVAIL: c_int = -4090;
pub const UV_EAFNOSUPPORT: c_int = -4089;
pub const UV_EAGAIN: c_int = -4088;
pub const UV_EAI_ADDRFAMILY: c_int = -3000;
pub const UV_EAI_AGAIN: c_int = -3001;
pub const UV_EAI_BADFLAGS: c_int = -3002;
pub const UV_EAI_BADHINTS: c_int = -3013;
pub const UV_EAI_CANCELED: c_int = -3003;
pub const UV_EAI_FAIL: c_int = -3004;
pub const UV_EAI_FAMILY: c_int = -3005;
pub const UV_EAI_MEMORY: c_int = -3006;
pub const UV_EAI_NODATA: c_int = -3007;
pub const UV_EAI_NONAME: c_int = -3008;
pub const UV_EAI_OVERFLOW: c_int = -3009;
pub const UV_EAI_PROTOCOL: c_int = -3014;
pub const UV_EAI_SERVICE: c_int = -3010;
pub const UV_EAI_SOCKTYPE: c_int = -3011;
pub const UV_EALREADY: c_int = -4084;
pub const UV_EBADF: c_int = -4083;
pub const UV_EBUSY: c_int = -4082;
pub const UV_ECANCELED: c_int = -4081;
pub const UV_ECHARSET: c_int = -4080;
pub const UV_ECONNABORTED: c_int = -4079;
pub const UV_ECONNREFUSED: c_int = -4078;
pub const UV_ECONNRESET: c_int = -4077;
pub const UV_EDESTADDRREQ: c_int = -4076;
pub const UV_EEXIST: c_int = -4075;
pub const UV_EFAULT: c_int = -4074;
pub const UV_EFBIG: c_int = -4036;
pub const UV_EHOSTUNREACH: c_int = -4073;
pub const UV_EINTR: c_int = -4072;
pub const UV_EINVAL: c_int = -4071;
pub const UV_EIO: c_int = -4070;
pub const UV_EISCONN: c_int = -4069;
pub const UV_EISDIR: c_int = -4068;
pub const UV_ELOOP: c_int = -4067;
pub const UV_EMFILE: c_int = -4066;
pub const UV_EMSGSIZE: c_int = -4065;
pub const UV_ENAMETOOLONG: c_int = -4064;
pub const UV_ENETDOWN: c_int = -4063;
pub const UV_ENETUNREACH: c_int = -4062;
pub const UV_ENFILE: c_int = -4061;
pub const UV_ENOBUFS: c_int = -4060;
pub const UV_ENODEV: c_int = -4059;
pub const UV_ENOENT: c_int = -4058;
pub const UV_ENOMEM: c_int = -4057;
pub const UV_ENONET: c_int = -4056;
pub const UV_ENOPROTOOPT: c_int = -4035;
pub const UV_ENOSPC: c_int = -4055;
pub const UV_ENOSYS: c_int = -4054;
pub const UV_ENOTCONN: c_int = -4053;
pub const UV_ENOTDIR: c_int = -4052;
pub const UV_ENOTEMPTY: c_int = -4051;
pub const UV_ENOTSOCK: c_int = -4050;
pub const UV_ENOTSUP: c_int = -4049;
pub const UV_EOVERFLOW: c_int = -4026;
pub const UV_EPERM: c_int = -4048;
pub const UV_EPIPE: c_int = -4047;
pub const UV_EPROTO: c_int = -4046;
pub const UV_EPROTONOSUPPORT: c_int = -4045;
pub const UV_EPROTOTYPE: c_int = -4044;
pub const UV_ERANGE: c_int = -4034;
pub const UV_EROFS: c_int = -4043;
pub const UV_ESHUTDOWN: c_int = -4042;
pub const UV_ESPIPE: c_int = -4041;
pub const UV_ESRCH: c_int = -4040;
pub const UV_ETIMEDOUT: c_int = -4039;
pub const UV_ETXTBSY: c_int = -4038;
pub const UV_EXDEV: c_int = -4037;
pub const UV_UNKNOWN: c_int = -4094;
pub const UV_EOF: c_int = -4095;
pub const UV_ENXIO: c_int = -4033;
pub const UV_EMLINK: c_int = -4032;
pub const UV_EHOSTDOWN: c_int = -4031;
pub const UV_EREMOTEIO: c_int = -4030;
pub const UV_ENOTTY: c_int = -4029;
pub const UV_EFTYPE: c_int = -4028;
pub const UV_EILSEQ: c_int = -4027;
pub const UV_ESOCKTNOSUPPORT: c_int = -4025;
pub const UV_ENODATA: c_int = -4024;
pub const UV_EUNATCH: c_int = -4023;
pub const UV_ENOEXEC: c_int = -4022;
pub const UV_ERRNO_MAX: c_int = -4096;
pub const uv_errno_t = c_int;
pub const UV_UNKNOWN_HANDLE: c_int = 0;
pub const UV_ASYNC: c_int = 1;
pub const UV_CHECK: c_int = 2;
pub const UV_FS_EVENT: c_int = 3;
pub const UV_FS_POLL: c_int = 4;
pub const UV_HANDLE: c_int = 5;
pub const UV_IDLE: c_int = 6;
pub const UV_NAMED_PIPE: c_int = 7;
pub const UV_POLL: c_int = 8;
pub const UV_PREPARE: c_int = 9;
pub const UV_PROCESS: c_int = 10;
pub const UV_STREAM: c_int = 11;
pub const UV_TCP: c_int = 12;
pub const UV_TIMER: c_int = 13;
pub const UV_TTY: c_int = 14;
pub const UV_UDP: c_int = 15;
pub const UV_SIGNAL: c_int = 16;
pub const UV_FILE: c_int = 17;
pub const UV_HANDLE_TYPE_MAX: c_int = 18;
pub const uv_handle_type = Handle.Type;
pub const UV_UNKNOWN_REQ: c_int = 0;
pub const UV_REQ: c_int = 1;
pub const UV_CONNECT: c_int = 2;
pub const UV_WRITE: c_int = 3;
pub const UV_SHUTDOWN: c_int = 4;
pub const UV_UDP_SEND: c_int = 5;
pub const UV_FS: c_int = 6;
pub const UV_WORK: c_int = 7;
pub const UV_GETADDRINFO: c_int = 8;
pub const UV_GETNAMEINFO: c_int = 9;
pub const UV_RANDOM: c_int = 10;
pub const UV_ACCEPT: c_int = 11;
pub const UV_FS_EVENT_REQ: c_int = 12;
pub const UV_POLL_REQ: c_int = 13;
pub const UV_PROCESS_EXIT: c_int = 14;
pub const UV_READ: c_int = 15;
pub const UV_UDP_RECV: c_int = 16;
pub const UV_WAKEUP: c_int = 17;
pub const UV_SIGNAL_REQ: c_int = 18;
pub const UV_REQ_TYPE_MAX: c_int = 19;
pub const uv_req_type = c_uint;
@c pub struct struct_uv_dirent_s {
    name: [*]const u8,
    type: uv_dirent_type_t,
}
pub const uv_dirent_t = struct_uv_dirent_s;
@c pub struct struct_uv_dir_s {
    dirents: [*c]uv_dirent_t,
    nentries: usize,
    reserved: [4]*!?,
    dir_handle: HANDLE,
    find_data: WIN32_FIND_DATAW,
    need_find_call: BOOL,
}
pub const uv_dir_t = struct_uv_dir_s;
@c union union_unnamed_380 {
    fd: c_int,
    reserved: [4]*!?,
}
const uv_alloc_cb_fn = *const fn (*uv_handle_t, usize, *uv_buf_t) callconv(.c) void;
pub type uv_alloc_cb = uv_alloc_cb_fn?;
pub const uv_stream_t = struct_uv_stream_s;
/// *uv.uv_handle_t is actually *uv_stream_t, just changed to avoid dependency loop error on Zig
const uv_read_cb_fn = *const fn (*uv_handle_t, ReturnCodeI64, *const uv_buf_t) callconv(.c) void;
pub type uv_read_cb = uv_read_cb_fn?;
@c struct struct_unnamed_382 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_383 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_381 {
    io: struct_unnamed_382,
    connect: struct_unnamed_383,
}
@c pub struct Read {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_381,
    next_req: [*c]struct_uv_req_s,
    event_handle: HANDLE,
    wait_handle: HANDLE,
}
pub const uv_read_t = Read;
@c struct struct_unnamed_387 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_388 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_386 {
    io: struct_unnamed_387,
    connect: struct_unnamed_388,
}
const uv_shutdown_cb_fn = *const fn (*uv_shutdown_t, c_int) callconv(.c) void;
pub type uv_shutdown_cb = uv_shutdown_cb_fn?;
@c pub struct struct_uv_shutdown_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_386,
    next_req: [*c]struct_uv_req_s,
    handle: *uv_stream_t,
    cb: uv_shutdown_cb,
}
pub const uv_shutdown_t = struct_uv_shutdown_s;
@c struct struct_unnamed_385 {
    write_reqs_pending: c_uint,
    shutdown_req: [*c]uv_shutdown_t,
}
const uv_connection_cb_fn = *const fn (*uv_stream_t, ReturnCode) callconv(.c) void;
pub type uv_connection_cb = uv_connection_cb_fn?;
@c struct struct_unnamed_389 {
    connection_cb: uv_connection_cb,
}
@c union union_unnamed_384 {
    conn: struct_unnamed_385,
    serv: struct_unnamed_389,
}
@c pub struct struct_uv_stream_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_380,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    write_queue_size: usize,
    alloc_cb: uv_alloc_cb,
    read_cb: uv_read_cb,
    reqs_pending: c_uint,
    activecnt: c_int,
    read_req: uv_read_t,
    stream: union_unnamed_384,

    const handle_impl = HandleMixin(@This());
    pub const getData = handle_impl.getData;
    pub const getLoop = handle_impl.getLoop;
    pub const setData = handle_impl.setData;
    pub const close = handle_impl.close;
    pub const hasRef = handle_impl.hasRef;
    pub const ref = handle_impl.ref;
    pub const unref = handle_impl.unref;
    pub const isClosing = handle_impl.isClosing;
    pub const isClosed = handle_impl.isClosed;
    pub const isActive = handle_impl.isActive;
    pub const fd = handle_impl.fd;
    const stream_impl = StreamMixin(@This());
    pub const getWriteQueueSize = stream_impl.getWriteQueueSize;
    pub const listen = stream_impl.listen;
    pub const accept = stream_impl.accept;
    pub const readStart = stream_impl.readStart;
    pub const readStop = stream_impl.readStop;
    pub const write = stream_impl.write;
    pub const tryWrite = stream_impl.tryWrite;
    pub const tryWrite2 = stream_impl.tryWrite2;
    pub const isReadable = stream_impl.isReadable;
    pub const isWritable = stream_impl.isWritable;
}
@c union union_unnamed_390 {
    fd: c_int,
    reserved: [4]*!?,
}
@c struct struct_unnamed_392 {
    write_reqs_pending: c_uint,
    shutdown_req: [*c]uv_shutdown_t,
}
@c struct struct_unnamed_393 {
    connection_cb: uv_connection_cb,
}
@c union union_unnamed_391 {
    conn: struct_unnamed_392,
    serv: struct_unnamed_393,
}
@c struct struct_unnamed_397 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_398 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_396 {
    io: struct_unnamed_397,
    connect: struct_unnamed_398,
}
@c pub struct struct_uv_tcp_accept_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_396,
    next_req: [*c]struct_uv_req_s,
    accept_socket: SOCKET,
    accept_buffer: [288]u8,
    event_handle: HANDLE,
    wait_handle: HANDLE,
    next_pending: [*c]struct_uv_tcp_accept_s,
}
pub const uv_tcp_accept_t = struct_uv_tcp_accept_s;
@c struct struct_unnamed_395 {
    accept_reqs: [*c]uv_tcp_accept_t,
    processed_accepts: c_uint,
    pending_accepts: [*c]uv_tcp_accept_t,
    func_acceptex: LPFN_ACCEPTEX,
}
@c struct struct_unnamed_399 {
    read_buffer: uv_buf_t,
    func_connectex: LPFN_CONNECTEX,
}
@c union union_unnamed_394 {
    serv: struct_unnamed_395,
    conn: struct_unnamed_399,
}
@c pub struct struct_uv_tcp_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_390,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    write_queue_size: usize,
    alloc_cb: uv_alloc_cb,
    read_cb: uv_read_cb,
    reqs_pending: c_uint,
    activecnt: c_int,
    read_req: uv_read_t,
    stream: union_unnamed_391,
    socket: SOCKET,
    delayed_error: c_int,
    tcp: union_unnamed_394,
}
pub const uv_tcp_t = struct_uv_tcp_s;
@c union union_unnamed_400 {
    fd: c_int,
    reserved: [4]*!?,
}
pub const uv_udp_t = struct_uv_udp_s;
const uv_udp_recv_cb_fn = *const fn ([*c]uv_udp_t, isize, [*]const uv_buf_t, [*c]const sockaddr, c_uint) callconv(.c) void;
pub type uv_udp_recv_cb = uv_udp_recv_cb_fn?;
@c pub struct struct_uv_udp_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_400,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    send_queue_size: usize,
    send_queue_count: usize,
    socket: SOCKET,
    reqs_pending: c_uint,
    activecnt: c_int,
    recv_req: uv_req_t,
    recv_buffer: uv_buf_t,
    recv_from: sockaddr_storage,
    recv_from_len: c_int,
    recv_cb: uv_udp_recv_cb,
    alloc_cb: uv_alloc_cb,
    func_wsarecv: LPFN_WSARECV,
    func_wsarecvfrom: LPFN_WSARECVFROM,
}
@c union union_unnamed_401 {
    fd: c_int,
    reserved: [4]*!?,
}
@c struct struct_unnamed_403 {
    write_reqs_pending: c_uint,
    shutdown_req: [*c]uv_shutdown_t,
}
@c struct struct_unnamed_404 {
    connection_cb: uv_connection_cb,
}
@c union union_unnamed_402 {
    conn: struct_unnamed_403,
    serv: struct_unnamed_404,
}
@c struct struct_unnamed_408 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_409 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_407 {
    io: struct_unnamed_408,
    connect: struct_unnamed_409,
}
@c pub struct struct_uv_pipe_accept_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_407,
    next_req: [*c]struct_uv_req_s,
    pipeHandle: HANDLE,
    next_pending: [*c]struct_uv_pipe_accept_s,
}
pub const uv_pipe_accept_t = struct_uv_pipe_accept_s;
@c struct struct_unnamed_406 {
    pending_instances: c_int,
    accept_reqs: [*c]uv_pipe_accept_t,
    pending_accepts: [*c]uv_pipe_accept_t,
}
@c union union_unnamed_411 {
    fd: c_int,
    reserved: [4]*!?,
}
const uv_timer_cb_fn = *const fn (*Timer) callconv(.c) void;
pub type uv_timer_cb = uv_timer_cb_fn?;
@c pub struct Timer {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_411,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    heap_node: [3]*!?,
    unused: c_int,
    timeout: u64,
    repeat: u64,
    start_id: u64,
    timer_cb: uv_timer_cb,

    fn init(*_, loop: *Loop) {
        if (uv_timer_init(loop, _) != 0) {
            @panic("internal error: uv_timer_init failed");
        }
    }

    fn start(*_, timeout: u64, repeat: u64, callback: uv_timer_cb) {
        if (uv_timer_start(_, callback, timeout, repeat) != 0) {
            @panic("internal error: uv_timer_start failed");
        }
    }

    fn stop(*_) {
        if (uv_timer_stop(_) != 0) {
            @panic("internal error: uv_timer_stop failed");
        }
    }

    fn unref(*_) {
        unsafe { uv_unref(@ptrCast(@alignCast(_))); }
    }

    fn ref(*_) {
        unsafe { uv_ref(@ptrCast(@alignCast(_))); }
    }
}
@c struct struct_unnamed_413 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_414 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_412 {
    io: struct_unnamed_413,
    connect: struct_unnamed_414,
}
const uv_write_cb_fn = *const fn (*uv_write_t, ReturnCode) callconv(.c) void;
pub type uv_write_cb = uv_write_cb_fn?;
@c pub struct struct_uv_write_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_412,
    next_req: [*c]struct_uv_req_s,
    cb: uv_write_cb,
    send_handle: *uv_stream_t,
    handle: *uv_stream_t,
    coalesced: c_int,
    write_buffer: uv_buf_t,
    event_handle: HANDLE,
    wait_handle: HANDLE,

    fn write(*_, stream: *uv_stream_t, input: *uv_buf_t, context: auto, comptime onWrite: auto): Maybe(void) {
        if (callback := comptime onWrite) {
            const Wrapper = struct {
                @callconv("c") pub fn uvWriteCb(handler: *uv_write_t, status: ReturnCode) {
                    unsafe { callback(@ptrCast(@alignCast(handler.data)), status); }
                }
            };

            _.data = context;

            const rc = unsafe { uv_write(_, stream, @ptrCast(input), 1, &Wrapper.uvWriteCb) };
            bun.sys.syslog("uv_write({d}) = {d}", .{ input.len, rc.int() });

            if (e := rc.toError(.write)) {
                return { err: e };
            }

            return .success;
        }

        const rc = unsafe { uv_write(_, stream, @ptrCast(input), 1, null) };
        if (e := rc.toError(.write)) {
            return { err: e };
        }
        return .success;
    }
}
pub const uv_write_t = struct_uv_write_s;
@c union union_unnamed_415 {
    payload_remaining: u32,
    dummy: u64,
}
@c struct struct_unnamed_410 {
    eof_timer: [*c]Timer,
    dummy: uv_write_t,
    ipc_remote_pid: DWORD,
    ipc_data_frame: union_unnamed_415,
    ipc_xfer_queue: struct_uv__queue,
    ipc_xfer_queue_length: c_int,
    non_overlapped_writes_tail: [*c]uv_write_t,
    readfile_thread_lock: CRITICAL_SECTION,
    readfile_thread_handle: HANDLE,
}
@c union union_unnamed_405 {
    serv: struct_unnamed_406,
    conn: struct_unnamed_410,
}
@c pub struct Pipe {
    data: *!?,
    loop: (*uv_loop_t)?,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_401,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    write_queue_size: usize,
    alloc_cb: uv_alloc_cb,
    read_cb: uv_read_cb,
    reqs_pending: c_uint,
    activecnt: c_int,
    read_req: uv_read_t,
    stream: union_unnamed_402,
    ipc: c_int,
    handle: HANDLE,
    name: [*]WCHAR,
    pipe: union_unnamed_405,

    const handle_impl = HandleMixin(@This());
    pub const getData = handle_impl.getData;
    pub const getLoop = handle_impl.getLoop;
    pub const setData = handle_impl.setData;
    pub const close = handle_impl.close;
    pub const hasRef = handle_impl.hasRef;
    pub const ref = handle_impl.ref;
    pub const unref = handle_impl.unref;
    pub const isClosing = handle_impl.isClosing;
    pub const isClosed = handle_impl.isClosed;
    pub const isActive = handle_impl.isActive;
    pub const fd = handle_impl.fd;
    const stream_impl = StreamMixin(@This());
    pub const getWriteQueueSize = stream_impl.getWriteQueueSize;
    pub const listen = stream_impl.listen;
    pub const accept = stream_impl.accept;
    pub const readStart = stream_impl.readStart;
    pub const readStop = stream_impl.readStop;
    pub const write = stream_impl.write;
    pub const tryWrite = stream_impl.tryWrite;
    pub const tryWrite2 = stream_impl.tryWrite2;
    pub const isReadable = stream_impl.isReadable;
    pub const isWritable = stream_impl.isWritable;

    pub fn init(*_, loop: *Loop, ipc: bool): Maybe(void) {
        if (e := uv_pipe_init(loop, _, if (ipc) 1 else 0).toError(.pipe)) return { err: e };

        return .success;
    }

    fn open(*_, file: bun.FD): Maybe(void) {
        const uv_fd = file.uv();
        if (e := uv_pipe_open(_, uv_fd).toError(.open)) return { err: e };

        return .success;
    }

    fn listenNamedPipe(*_, named_pipe: []const u8, backlog: i32, context: auto, comptime onClientConnect: *const (fn (@TypeOf(context), ReturnCode) void)): Maybe(void) {
        if (e := _.bind(named_pipe, UV_PIPE_NO_TRUNCATE).asErr()) {
            return { err: e };
        }
        return _.listen(backlog, context, onClientConnect);
    }

    fn bind(*_, named_pipe: []const u8, flags: i32): Maybe(void) {
        if (e := uv_pipe_bind2(_, named_pipe.ptr, named_pipe.len, i32(flags)).toError(.bind2)) {
            return { err: e };
        }
        return .success;
    }

    fn connect(*_, req: *uv_connect_t, name: []const u8, context: auto, comptime onConnect: *const (fn (@TypeOf(context), ReturnCode) void)): Maybe(void) {
        _.data = unsafe { @ptrCast(context) };
        const Wrapper = struct {
            @callconv("c") pub fn uvConnectCb(handle: *uv_connect_t, status: ReturnCode) {
                unsafe { onConnect(@ptrCast(@alignCast(handle.data)), status); }
            }
        };
        if (e := unsafe { uv_pipe_connect2(req, _, @ptrCast(name.ptr), name.len, UV_PIPE_NO_TRUNCATE, &Wrapper.uvConnectCb) }.toError(.connect2)) {
            return { err: e };
        }
        return .success;
    }

    fn setPendingInstancesCount(*_, count: i32) {
        uv_pipe_pending_instances(_, count);
    }

    fn asStream(*_): *uv_stream_t {
        return unsafe { @ptrCast(_) };
    }

    /// Close the pipe handle (if needed) and then free it.
    /// Handles all states: never-initialized (loop == null), already closing,
    /// or active. After uv_pipe_init the handle is in the event loop's
    /// handle_queue; freeing without uv_close corrupts that list.
    fn closeAndDestroy(*_) {
        if (_.loop == null) {
            // Never initialized — safe to free directly.
            bun.destroy(_);
        } else if (!_.isClosing()) {
            // Initialized and not yet closing — must uv_close first.
            _.close(&onCloseDestroy);
        }
        // else: already closing — the pending close callback owns the lifetime.
    }

    @callconv("c") static fn onCloseDestroy(handle: *@This()) {
        bun.destroy(handle);
    }
}
@c union union_unnamed_416 {
    fd: c_int,
    reserved: [4]*!?,
}
@c struct struct_unnamed_418 {
    write_reqs_pending: c_uint,
    shutdown_req: [*c]uv_shutdown_t,
}
@c struct struct_unnamed_419 {
    connection_cb: uv_connection_cb,
}
@c union union_unnamed_417 {
    conn: struct_unnamed_418,
    serv: struct_unnamed_419,
}
@c struct struct_unnamed_421 {
    unused_: HANDLE,
    read_line_buffer: uv_buf_t,
    read_raw_wait: HANDLE,
    last_key: [8]u8,
    last_key_offset: u8,
    last_key_len: u8,
    last_utf16_high_surrogate: WCHAR,
    last_input_record: INPUT_RECORD,
}
@c struct struct_unnamed_422 {
    utf8_codepoint: c_uint,
    utf8_bytes_left: u8,
    previous_eol: u8,
    ansi_parser_state: c_ushort,
    ansi_csi_argc: u8,
    ansi_csi_argv: [4]c_ushort,
    saved_position: COORD,
    saved_attributes: WORD,
}
@c union union_unnamed_420 {
    rd: struct_unnamed_421,
    wr: struct_unnamed_422,
}
@c pub struct struct_uv_tty_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_416,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    write_queue_size: usize,
    alloc_cb: uv_alloc_cb,
    read_cb: uv_read_cb,
    reqs_pending: c_uint,
    activecnt: c_int,
    read_req: uv_read_t,
    stream: union_unnamed_417,
    handle: HANDLE,
    tty: union_unnamed_420,

    pub fn init(*_, loop: *uv_loop_t, file: uv_file): Maybe(void) {
        // last param is ignored
        if (e := uv_tty_init(loop, _, file, 0).toError(.open)) {
            return { err: e };
        }
        return .success;
    }

    const Mode = enum(c_uint) {
        normal = 0,
        raw = 1,
        io = 2,
        vt = 3,
    };

    pub fn setMode(*_, mode: Mode): ReturnCode {
        return uv_tty_set_mode(_, @intFromEnum(mode));
    }

    const handle_impl = HandleMixin(@This());
    pub const getData = handle_impl.getData;
    pub const getLoop = handle_impl.getLoop;
    pub const setData = handle_impl.setData;
    pub const close = handle_impl.close;
    pub const hasRef = handle_impl.hasRef;
    pub const ref = handle_impl.ref;
    pub const unref = handle_impl.unref;
    pub const isClosing = handle_impl.isClosing;
    pub const isClosed = handle_impl.isClosed;
    pub const isActive = handle_impl.isActive;
    pub const fd = handle_impl.fd;
    const stream_impl = StreamMixin(@This());
    pub const getWriteQueueSize = stream_impl.getWriteQueueSize;
    pub const listen = stream_impl.listen;
    pub const accept = stream_impl.accept;
    pub const readStart = stream_impl.readStart;
    pub const readStop = stream_impl.readStop;
    pub const write = stream_impl.write;
    pub const tryWrite = stream_impl.tryWrite;
    pub const tryWrite2 = stream_impl.tryWrite2;
    pub const isReadable = stream_impl.isReadable;
    pub const isWritable = stream_impl.isWritable;
}
pub const uv_tty_t = struct_uv_tty_s;
@c union union_unnamed_423 {
    fd: c_int,
    reserved: [4]*!?,
}
pub const uv_poll_t = struct_uv_poll_s;
const uv_poll_cb_fn = *const fn ([*c]uv_poll_t, c_int, c_int) callconv(.c) void;
pub type uv_poll_cb = uv_poll_cb_fn?;
@c pub struct struct_uv_poll_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_423,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    poll_cb: uv_poll_cb,
    socket: SOCKET,
    peer_socket: SOCKET,
    afd_poll_info_1: AFD_POLL_INFO,
    afd_poll_info_2: AFD_POLL_INFO,
    poll_req_1: uv_req_t,
    poll_req_2: uv_req_t,
    submitted_events_1: u8,
    submitted_events_2: u8,
    mask_events_1: u8,
    mask_events_2: u8,
    events: u8,

    const handle_impl = HandleMixin(@This());
    pub const getData = handle_impl.getData;
    pub const getLoop = handle_impl.getLoop;
    pub const setData = handle_impl.setData;
    pub const close = handle_impl.close;
    pub const hasRef = handle_impl.hasRef;
    pub const ref = handle_impl.ref;
    pub const unref = handle_impl.unref;
    pub const isClosing = handle_impl.isClosing;
    pub const isClosed = handle_impl.isClosed;
    pub const isActive = handle_impl.isActive;
    pub const fd = handle_impl.fd;
}
pub const Poll = struct_uv_poll_s;
@c union union_unnamed_424 {
    fd: c_int,
    reserved: [4]*!?,
}
pub const uv_process_t = Process;
const uv_exit_cb_fn = *const fn (*uv_process_t, i64, c_int) callconv(.c) void;
pub type uv_exit_cb = uv_exit_cb_fn?;
@c struct struct_unnamed_426 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_427 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_425 {
    io: struct_unnamed_426,
    connect: struct_unnamed_427,
}
@c pub struct struct_uv_process_exit_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_425,
    next_req: [*c]struct_uv_req_s,
}
@c pub struct Process {
    data: *!? = null,
    loop: (*uv_loop_t)? = null,
    type: uv_handle_type = std.mem.zeroes(uv_handle_type),
    close_cb: uv_close_cb = null,
    handle_queue: struct_uv__queue = std.mem.zeroes(struct_uv__queue),
    u: union_unnamed_424 = std.mem.zeroes(union_unnamed_424),
    endgame_next: ([*]uv_handle_t)? = null,
    flags: c_uint = 0,
    exit_cb: uv_exit_cb = null,
    pid: c_int = 0,
    exit_req: struct_uv_process_exit_s = std.mem.zeroes(struct_uv_process_exit_s),
    unused: *!? = null,
    exit_signal: c_int = 0,
    wait_handle: HANDLE = windows.INVALID_HANDLE_VALUE,
    process_handle: HANDLE = windows.INVALID_HANDLE_VALUE,
    exit_cb_pending: u8 = 0,

    pub fn spawn(handle: *uv_process_t, loop: *uv_loop_t, options: *const uv_process_options_t): ReturnCode {
        return uv_spawn(loop, handle, options);
    }

    const handle_impl = HandleMixin(@This());
    pub const getData = handle_impl.getData;
    pub const getLoop = handle_impl.getLoop;
    pub const setData = handle_impl.setData;
    pub const close = handle_impl.close;
    pub const hasRef = handle_impl.hasRef;
    pub const ref = handle_impl.ref;
    pub const unref = handle_impl.unref;
    pub const isClosing = handle_impl.isClosing;
    pub const isClosed = handle_impl.isClosed;
    pub const isActive = handle_impl.isActive;
    pub const fd = handle_impl.fd;

    fn kill(*_, signum: c_int): ReturnCode {
        return unsafe { uv_process_kill(@ptrCast(@alignCast(_)), signum) };
    }

    fn getPid(): c_int {
        return unsafe { uv_process_get_pid(@ptrCast(@alignCast(_))) };
    }
}
@c union union_unnamed_428 {
    fd: c_int,
    reserved: [4]*!?,
}
@c struct struct_unnamed_430 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_431 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_429 {
    io: struct_unnamed_430,
    connect: struct_unnamed_431,
}
@c pub struct struct_uv_fs_event_req_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_429,
    next_req: [*c]struct_uv_req_s,
}
pub const uv_fs_event_t = struct_uv_fs_event_s;
const uv_fs_event_cb_fn = *const fn (*uv_fs_event_t, [*c]const u8, c_int, ReturnCode) callconv(.c) void;
pub type uv_fs_event_cb = uv_fs_event_cb_fn?;
@c pub struct struct_uv_fs_event_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_428,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    path: [*:0]u8?,
    req: struct_uv_fs_event_req_s,
    dir_handle: HANDLE,
    req_pending: c_int,
    cb: uv_fs_event_cb,
    filew: [*]WCHAR? = null,
    short_filew: [*]WCHAR? = null,
    dirw: [*]WCHAR? = null,
    buffer: [*]u8,

    pub fn isDir(): bool {
        return _.dirw != null;
    }

    pub fn hash(filename: []const u8, events: c_int, status: ReturnCode): u64 {
        let hasher = std.hash.Wyhash.init(0);
        if (path := _.path) {
            hasher.update(bun.sliceTo(path, 0));
        } else {
            hasher.update("null");
        }
        hasher.update(std.mem.asBytes(&events));
        hasher.update(filename);
        hasher.update(std.mem.asBytes(&status));
        return hasher.final();
    }
}
@c union union_unnamed_432 {
    fd: c_int,
    reserved: [4]*!?,
}
@c pub struct struct_uv_fs_poll_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_432,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    poll_ctx: *!?,
}
pub const uv_fs_poll_t = struct_uv_fs_poll_s;
@c union union_unnamed_433 {
    fd: c_int,
    reserved: [4]*!?,
}
pub const uv_signal_t = struct_uv_signal_s;
const uv_signal_cb_fn = *const fn (*uv_signal_t, c_int) callconv(.c) void;
pub type uv_signal_cb = uv_signal_cb_fn?;
@c struct struct_unnamed_434 {
    rbe_left: [*c]struct_uv_signal_s,
    rbe_right: [*c]struct_uv_signal_s,
    rbe_parent: [*c]struct_uv_signal_s,
    rbe_color: c_int,
}
@c pub struct struct_uv_signal_s {
    data: *!?,
    loop: *uv_loop_t,
    type: uv_handle_type,
    close_cb: uv_close_cb,
    handle_queue: struct_uv__queue,
    u: union_unnamed_433,
    endgame_next: [*c]uv_handle_t,
    flags: c_uint,
    signal_cb: uv_signal_cb,
    signum: c_int,
    tree_entry: struct_unnamed_434,
    signal_req: struct_uv_req_s,
    pending_signum: c_ulong,
}
@c struct struct_unnamed_436 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_437 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_435 {
    io: struct_unnamed_436,
    connect: struct_unnamed_437,
}
pub const uv_getaddrinfo_t = struct_uv_getaddrinfo_s;
type OptAddrinfoPtr = (*addrinfo)?;
const uv_getaddrinfo_cb_fn = *const fn (*uv_getaddrinfo_t, c_int, OptAddrinfoPtr) callconv(.c) void;
pub type uv_getaddrinfo_cb = uv_getaddrinfo_cb_fn?;
@c pub struct struct_uv_getaddrinfo_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_435,
    next_req: [*c]struct_uv_req_s,
    loop: *uv_loop_t,
    work_req: struct_uv__work,
    getaddrinfo_cb: uv_getaddrinfo_cb,
    alloc: *!?,
    node: [*]WCHAR,
    service: [*]WCHAR,
    addrinfow: *!?,
    addrinfo: (*addrinfo)?,
    retcode: ReturnCode,
}
@c struct struct_unnamed_439 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_440 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_438 {
    io: struct_unnamed_439,
    connect: struct_unnamed_440,
}
pub const uv_getnameinfo_t = struct_uv_getnameinfo_s;
const uv_getnameinfo_cb_fn = *const fn ([*c]uv_getnameinfo_t, c_int, [*]const u8, [*]const u8) callconv(.c) void;
pub type uv_getnameinfo_cb = uv_getnameinfo_cb_fn?;
@c pub struct struct_uv_getnameinfo_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_438,
    next_req: [*c]struct_uv_req_s,
    loop: *uv_loop_t,
    work_req: struct_uv__work,
    getnameinfo_cb: uv_getnameinfo_cb,
    storage: sockaddr_storage,
    flags: c_int,
    host: [1025]u8,
    service: [32]u8,
    retcode: c_int,
}
@c struct struct_unnamed_442 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_443 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_441 {
    io: struct_unnamed_442,
    connect: struct_unnamed_443,
}
pub const uv_connect_t = struct_uv_connect_s;
const uv_connect_cb_fn = *const fn (*uv_connect_t, ReturnCode) callconv(.c) void;
pub type uv_connect_cb = uv_connect_cb_fn?;
@c pub struct struct_uv_connect_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_441,
    next_req: [*c]struct_uv_req_s,
    cb: uv_connect_cb,
    handle: *uv_stream_t,
}
@c struct struct_unnamed_445 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_446 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_444 {
    io: struct_unnamed_445,
    connect: struct_unnamed_446,
}
pub const uv_udp_send_t = struct_uv_udp_send_s;
const uv_udp_send_cb_fn = *const fn ([*c]uv_udp_send_t, c_int) callconv(.c) void;
pub type uv_udp_send_cb = uv_udp_send_cb_fn?;
@c pub struct struct_uv_udp_send_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_444,
    next_req: [*c]struct_uv_req_s,
    handle: *uv_udp_t,
    cb: uv_udp_send_cb,
}
@c struct struct_unnamed_448 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_449 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_447 {
    io: struct_unnamed_448,
    connect: struct_unnamed_449,
}
pub const FSCallback = *const fn (*fs_t) callconv(.c) void;
pub type uv_fs_cb = FSCallback?;
@c union union_unnamed_450 {
    pathw: [*]WCHAR,
    fd: c_int,
}
@c struct struct_unnamed_452 {
    mode: c_int,
    new_pathw: [*]WCHAR,
    file_flags: c_int,
    fd_out: c_int,
    nbufs: c_uint,
    bufs: [*c]uv_buf_t,
    offset: i64,
    bufsml: [4]uv_buf_t,
}
@c struct struct_unnamed_453 {
    atime: f64,
    mtime: f64,
}
@c union union_unnamed_451 {
    info: struct_unnamed_452,
    time: struct_unnamed_453,
}
@c pub struct fs_t {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_447,
    next_req: [*c]struct_uv_req_s,
    fs_type: uv_fs_type,
    loop: *uv_loop_t,
    cb: uv_fs_cb,
    result: ReturnCodeI64,
    ptr: *!?,
    path: [*:0]const u8,
    statbuf: uv_stat_t,
    work_req: struct_uv__work,
    flags: c_int,
    sys_errno_: DWORD,
    file: union_unnamed_450,
    fs: union_unnamed_451,

    const req_impl = ReqMixin(@This());
    pub const setData = req_impl.setData;
    pub const getLoop = req_impl.loop;
    pub const getData = req_impl.getData;
    pub const cancel = req_impl.cancel;

    const UV_FS_CLEANEDUP = 0x0010;

    fn deinit(*_) {
        _.assertInitialized();
        uv_fs_req_cleanup(_);
        _.assertCleanedUp();
    }

    // This assertion tripping is a sign that .deinit() is going to cause invalid memory access
    fn assertInitialized() {
        if (bun.Environment.allow_assert) {
            if (unsafe { @intFromPtr(_.loop) } == 0xAAAAAAAAAAAA0000) {
                @panic("uv_fs_t was not initialized");
            }
        }
    }

    // This assertion tripping is a sign that a memory leak may happen
    fn assertCleanedUp() {
        if (bun.Environment.allow_assert) {
            if (unsafe { @intFromPtr(_.loop) } == 0xAAAAAAAAAAAA0000) {
                return;
            }
            if ((_.flags & UV_FS_CLEANEDUP) != 0) {
                return;
            }
            @panic("uv_fs_t was not cleaned up. it is expected to call .deinit() on the fs_t here.");
        }
    }

    fn ptrAs(*_, comptime T: type): T {
        _.assertInitialized();
        return unsafe { @ptrCast(_.ptr) };
    }

    /// This value is designed to to be used as the initial value for libuv fs actions.
    /// In a release build it is uninitialized memory, but in a debug it is guaranteed
    /// to panic if passed to deinit(). If that assertion fails, then it means the uv
    /// function did not overwrite the memory before returning.
    ///
    /// It is assumed that if UV overwrites the .loop, it probably overwrote the rest of the struct.
    pub const uninitialized: fs_t = if (bun.Environment.allow_assert) value: {
        let value = std.mem.zeroes(fs_t);
        value.loop = @ptrFromInt(0xAAAAAAAAAAAA0000);
        break :value value;
    } else undefined;
}
@c struct struct_unnamed_455 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_456 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_454 {
    io: struct_unnamed_455,
    connect: struct_unnamed_456,
}
pub const uv_work_t = struct_uv_work_s;
const uv_work_cb_fn = *const fn ([*c]uv_work_t) callconv(.c) void;
pub type uv_work_cb = uv_work_cb_fn?;
const uv_after_work_cb_fn = *const fn ([*c]uv_work_t, c_int) callconv(.c) void;
pub type uv_after_work_cb = uv_after_work_cb_fn?;
@c pub struct struct_uv_work_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_454,
    next_req: [*c]struct_uv_req_s,
    loop: *uv_loop_t,
    work_cb: uv_work_cb,
    after_work_cb: uv_after_work_cb,
    work_req: struct_uv__work,

    const req_impl = ReqMixin(@This());
    pub const setData = req_impl.setData;
    pub const getLoop = req_impl.loop;
    pub const getData = req_impl.getData;
    pub const cancel = req_impl.cancel;
}
@c struct struct_unnamed_458 {
    overlapped: OVERLAPPED,
    queued_bytes: usize,
}
@c struct struct_unnamed_459 {
    result: ULONG_PTR,
    pipeHandle: HANDLE,
    duplex_flags: DWORD,
    name: [*]WCHAR,
}
@c union union_unnamed_457 {
    io: struct_unnamed_458,
    connect: struct_unnamed_459,
}
pub const uv_random_t = struct_uv_random_s;
const uv_random_cb_fn = *const fn ([*c]uv_random_t, c_int, *!?, usize) callconv(.c) void;
pub type uv_random_cb = uv_random_cb_fn?;
@c pub struct struct_uv_random_s {
    data: *!?,
    type: uv_req_type,
    reserved: [6]*!?,
    u: union_unnamed_457,
    next_req: [*c]struct_uv_req_s,
    loop: *uv_loop_t,
    status: c_int,
    buf: *!?,
    buflen: usize,
    cb: uv_random_cb,
    work_req: struct_uv__work,

    const req_impl = ReqMixin(@This());
    pub const setData = req_impl.setData;
    pub const getLoop = req_impl.loop;
    pub const getData = req_impl.getData;
    pub const cancel = req_impl.cancel;
}
@c pub struct struct_uv_env_item_s {
    name: [*]u8,
    value: [*]u8,
}
pub const uv_env_item_t = struct_uv_env_item_s;
@c pub struct struct_uv_cpu_times_s {
    user: u64,
    nice: u64,
    sys: u64,
    idle: u64,
    irq: u64,
}
@c pub struct struct_uv_cpu_info_s {
    model: [*:0]u8,
    speed: c_int,
    cpu_times: struct_uv_cpu_times_s,
}
pub const uv_cpu_info_t = struct_uv_cpu_info_s;

@c pub union addr_union {
    address4: std.os.linux.sockaddr.@"in",
    address6: std.os.linux.sockaddr.in6,
}
@c union netmask_union {
    netmask4: std.os.linux.sockaddr.@"in",
    netmask6: std.os.linux.sockaddr.in6,
}
@c pub struct uv_interface_address_t {
    name: [*:0]u8,
    phys_addr: [6]u8,
    is_internal: c_int,
    address: addr_union,
    netmask: netmask_union,
}
@c pub struct struct_uv_passwd_s {
    username: [*]u8,
    uid: c_ulong,
    gid: c_ulong,
    shell: [*]u8,
    homedir: [*]u8,
}
pub const uv_passwd_t = struct_uv_passwd_s;
@c pub struct struct_uv_group_s {
    groupname: [*]u8,
    gid: c_ulong,
    members: [*c][*]u8,
}
pub const uv_group_t = struct_uv_group_s;
@c pub struct struct_uv_utsname_s {
    sysname: [256]u8,
    release: [256]u8,
    version: [256]u8,
    machine: [256]u8,
}
comptime {
    bun.assert(@sizeOf(struct_uv_utsname_s) == 256 * 4);
}
pub const uv_utsname_t = struct_uv_utsname_s;
@c pub struct struct_uv_statfs_s {
    f_type: u64,
    f_bsize: u64,
    f_blocks: u64,
    f_bfree: u64,
    f_bavail: u64,
    f_files: u64,
    f_ffree: u64,
    f_spare: [4]u64,

    fn init(*_: *align(1) struct_uv_statfs_s): bun.StatFS {
        return _.*;
    }
}
pub const uv_statfs_t = struct_uv_statfs_s;
@c pub struct struct_uv_metrics_s {
    loop_count: u64,
    events: u64,
    events_waiting: u64,
    reserved: [13][*c]u64,
}
pub const uv_metrics_t = struct_uv_metrics_s;
pub const UV_LOOP_BLOCK_SIGNAL: c_int = 0;
pub const UV_METRICS_IDLE_TIME: c_int = 1;
pub const uv_loop_option = c_uint;
pub const UV_RUN_DEFAULT: c_int = 0;
pub const UV_RUN_ONCE: c_int = 1;
pub const UV_RUN_NOWAIT: c_int = 2;
pub const uv_run_mode = c_uint;
pub enum RunMode: c_uint {
    default = 0,
    once = 1,
    nowait = 2,
}
pub extern fn uv_version(): c_uint;
pub extern fn uv_version_string(): [*:0]const u8;
type OptOpaquePtr = *!?;
const uv_malloc_func_fn = *const fn (usize) callconv(.c) OptOpaquePtr;
pub type uv_malloc_func = uv_malloc_func_fn?;
const uv_realloc_func_fn = *const fn (*!?, usize) callconv(.c) OptOpaquePtr;
pub type uv_realloc_func = uv_realloc_func_fn?;
const uv_calloc_func_fn = *const fn (usize, usize) callconv(.c) OptOpaquePtr;
pub type uv_calloc_func = uv_calloc_func_fn?;
const uv_free_func_fn = *const fn (*!?) callconv(.c) void;
pub type uv_free_func = uv_free_func_fn?;
pub extern fn uv_library_shutdown();
pub extern fn uv_replace_allocator(malloc_func: uv_malloc_func, realloc_func: uv_realloc_func, calloc_func: uv_calloc_func, free_func: uv_free_func): c_int;
pub extern fn uv_loop_init(loop: *uv_loop_t): ReturnCode;
pub extern fn uv_loop_close(loop: *uv_loop_t): ReturnCode;
pub extern fn uv_loop_new(): *uv_loop_t;
pub extern fn uv_loop_delete(arg0: *uv_loop_t);
pub extern fn uv_loop_size(): usize;
pub extern fn uv_loop_alive(loop: *const uv_loop_t): c_int;
pub extern fn uv_loop_configure(loop: *uv_loop_t, option: uv_loop_option, ...): c_int;
pub extern fn uv_loop_fork(loop: *uv_loop_t): c_int;
pub extern fn uv_run(arg0: *uv_loop_t, mode: RunMode): c_int;
pub extern fn uv_stop(arg0: *uv_loop_t);
pub extern fn uv_ref(arg0: *uv_handle_t);
pub extern fn uv_unref(arg0: *uv_handle_t);
pub extern fn uv_has_ref(arg0: *const uv_handle_t): c_int;
pub extern fn uv_update_time(arg0: *uv_loop_t);
pub extern fn uv_now(arg0: [*c]const uv_loop_t): u64;
pub extern fn uv_backend_fd(arg0: [*c]const uv_loop_t): c_int;
pub extern fn uv_backend_timeout(arg0: [*c]const uv_loop_t): c_int;
const uv_walk_cb_fn = *const fn ([*c]uv_handle_t, *!?) callconv(.c) void;
pub type uv_walk_cb = uv_walk_cb_fn?;
pub const UV_CLOCK_MONOTONIC: c_int = 0;
pub const UV_CLOCK_REALTIME: c_int = 1;
pub const uv_clock_id = c_uint;
@c pub struct uv_timespec_t {
    sec: c_long,
    nsec: c_long,
}
@c pub struct uv_timespec64_t {
    sec: i64,
    nsec: i32,
}
@c pub struct uv_timeval_t {
    sec: c_long,
    usec: c_long,
}
@c pub struct uv_timeval64_t {
    sec: i64,
    usec: i32,
}
@c pub struct uv_stat_t {
    dev: u64,
    mode: u64,
    nlink: u64,
    uid: u64,
    gid: u64,
    rdev: u64,
    ino: u64,
    size: u64,
    blksize: u64,
    blocks: u64,
    flags: u64,
    gen: u64,
    atim: uv_timespec_t,
    mtim: uv_timespec_t,
    ctim: uv_timespec_t,
    birthtim: uv_timespec_t,

    fn atime(): uv_timespec_t {
        return _.atim;
    }

    fn mtime(): uv_timespec_t {
        return _.mtim;
    }

    fn ctime(): uv_timespec_t {
        return _.ctim;
    }

    fn birthtime(): uv_timespec_t {
        return _.birthtim;
    }
}
const uv_fs_poll_cb_fn = *const fn ([*c]uv_fs_poll_t, c_int, [*c]const uv_stat_t, [*c]const uv_stat_t) callconv(.c) void;
pub type uv_fs_poll_cb = uv_fs_poll_cb_fn?;
pub const UV_LEAVE_GROUP: c_int = 0;
pub const UV_JOIN_GROUP: c_int = 1;
pub const uv_membership = c_uint;
pub extern fn uv_translate_sys_error(sys_errno: c_int): c_int;
pub extern fn uv_strerror(@"err": c_int): [*c]const u8;
pub extern fn uv_strerror_r(@"err": c_int, buf: [*]u8, buflen: usize): [*]u8;
pub extern fn uv_err_name(@"err": c_int): [*c]const u8;
pub extern fn uv_err_name_r(@"err": c_int, buf: [*]u8, buflen: usize): [*]u8;
pub extern fn uv_shutdown(req: [*c]uv_shutdown_t, handle: *uv_stream_t, cb: uv_shutdown_cb): c_int;
pub extern fn uv_handle_size(@"type": uv_handle_type): usize;
pub extern fn uv_handle_get_type(handle: *const uv_handle_t): uv_handle_type;
pub extern fn uv_handle_type_name(@"type": uv_handle_type): [*]const u8;
pub extern fn uv_handle_get_data(handle: *const uv_handle_t): *!?;
pub extern fn uv_handle_get_loop(handle: *const uv_handle_t): *uv_loop_t;
pub extern fn uv_handle_set_data(handle: *uv_handle_t, data: *!?);
pub extern fn uv_req_size(@"type": uv_req_type): usize;
pub extern fn uv_req_get_data(req: [*c]const uv_req_t): *!?;
pub extern fn uv_req_set_data(req: [*c]uv_req_t, data: *!?);
pub extern fn uv_req_get_type(req: [*c]const uv_req_t): uv_req_type;
pub extern fn uv_req_type_name(@"type": uv_req_type): [*]const u8;
pub extern fn uv_is_active(handle: *const uv_handle_t): c_int;
pub extern fn uv_walk(loop: *uv_loop_t, walk_cb: uv_walk_cb, arg: *!?);
pub extern fn uv_print_all_handles(loop: *uv_loop_t, stream: (*FILE)?);
pub extern fn uv_print_active_handles(loop: *uv_loop_t, stream: (*FILE)?);
pub extern fn uv_close(handle: *uv_handle_t, close_cb: uv_close_cb);
pub extern fn uv_send_buffer_size(handle: *uv_handle_t, value: [*c]c_int): c_int;
pub extern fn uv_recv_buffer_size(handle: *uv_handle_t, value: [*c]c_int): c_int;
pub extern fn uv_fileno(handle: *const uv_handle_t, fd: [*c]uv_os_fd_t): c_int;
pub extern fn uv_buf_init(base: [*]u8, len: c_uint): uv_buf_t;
pub extern fn uv_pipe(fds: *[2]uv_file, read_flags: c_int, write_flags: c_int): ReturnCode;
pub extern fn uv_socketpair(@"type": c_int, protocol: c_int, socket_vector: [*]uv_os_sock_t, flags0: c_int, flags1: c_int): ReturnCode;
pub extern fn uv_stream_get_write_queue_size(stream: [*c]const uv_stream_t): usize;
pub extern fn uv_listen(stream: [*c]uv_stream_t, backlog: c_int, cb: uv_connection_cb): ReturnCode;
pub extern fn uv_accept(server: [*c]uv_stream_t, client: [*c]uv_stream_t): ReturnCode;
pub extern fn uv_read_start(arg0: *uv_stream_t, alloc_cb: uv_alloc_cb, read_cb: uv_read_cb): ReturnCode;
pub extern fn uv_read_stop(arg0: *uv_stream_t): ReturnCode;
pub extern fn uv_write(req: *uv_write_t, handle: *uv_stream_t, bufs: [*]const uv_buf_t, nbufs: c_uint, cb: uv_write_cb): ReturnCode;
pub extern fn uv_write2(req: *uv_write_t, handle: *uv_stream_t, bufs: [*]const uv_buf_t, nbufs: c_uint, send_handle: *uv_stream_t, cb: uv_write_cb): ReturnCode;
pub extern fn uv_try_write(handle: *uv_stream_t, bufs: [*]const uv_buf_t, nbufs: c_uint): ReturnCode;
pub extern fn uv_try_write2(handle: *uv_stream_t, bufs: [*]const uv_buf_t, nbufs: c_uint, send_handle: *uv_stream_t): c_int;
pub extern fn uv_is_readable(handle: *const uv_stream_t): c_int;
pub extern fn uv_is_writable(handle: *const uv_stream_t): c_int;
pub extern fn uv_stream_set_blocking(handle: *uv_stream_t, blocking: c_int): ReturnCode;
pub extern fn uv_is_closing(handle: *const uv_handle_t): c_int;
pub extern fn uv_tcp_init(arg0: *uv_loop_t, handle: *uv_tcp_t): c_int;
pub extern fn uv_tcp_init_ex(arg0: *uv_loop_t, handle: *uv_tcp_t, flags: c_uint): c_int;
pub extern fn uv_tcp_open(handle: *uv_tcp_t, sock: uv_os_sock_t): c_int;
pub extern fn uv_tcp_nodelay(handle: *uv_tcp_t, enable: c_int): c_int;
pub extern fn uv_tcp_keepalive(handle: *uv_tcp_t, enable: c_int, delay: c_uint): c_int;
pub extern fn uv_tcp_simultaneous_accepts(handle: *uv_tcp_t, enable: c_int): c_int;
pub const UV_TCP_IPV6ONLY: c_int = 1;
pub const enum_uv_tcp_flags = c_uint;
pub extern fn uv_tcp_bind(handle: *uv_tcp_t, addr: [*c]const sockaddr, flags: c_uint): c_int;
pub extern fn uv_tcp_getsockname(handle: *const uv_tcp_t, name: [*c]sockaddr, namelen: [*c]c_int): c_int;
pub extern fn uv_tcp_getpeername(handle: *const uv_tcp_t, name: [*c]sockaddr, namelen: [*c]c_int): c_int;
pub extern fn uv_tcp_close_reset(handle: *uv_tcp_t, close_cb: uv_close_cb): c_int;
pub extern fn uv_tcp_connect(req: [*c]uv_connect_t, handle: *uv_tcp_t, addr: [*c]const sockaddr, cb: uv_connect_cb): c_int;
pub const UV_UDP_IPV6ONLY: c_int = 1;
pub const UV_UDP_PARTIAL: c_int = 2;
pub const UV_UDP_REUSEADDR: c_int = 4;
pub const UV_UDP_MMSG_CHUNK: c_int = 8;
pub const UV_UDP_MMSG_FREE: c_int = 16;
pub const UV_UDP_LINUX_RECVERR: c_int = 32;
pub const UV_UDP_RECVMMSG: c_int = 256;
pub const enum_uv_udp_flags = c_uint;
pub extern fn uv_udp_init(arg0: *uv_loop_t, handle: *uv_udp_t): c_int;
pub extern fn uv_udp_init_ex(arg0: *uv_loop_t, handle: *uv_udp_t, flags: c_uint): c_int;
pub extern fn uv_udp_open(handle: *uv_udp_t, sock: uv_os_sock_t): c_int;
pub extern fn uv_udp_bind(handle: *uv_udp_t, addr: [*c]const sockaddr, flags: c_uint): c_int;
pub extern fn uv_udp_connect(handle: *uv_udp_t, addr: [*c]const sockaddr): c_int;
pub extern fn uv_udp_getpeername(handle: *const uv_udp_t, name: [*c]sockaddr, namelen: [*c]c_int): c_int;
pub extern fn uv_udp_getsockname(handle: *const uv_udp_t, name: [*c]sockaddr, namelen: [*c]c_int): c_int;
pub extern fn uv_udp_set_membership(handle: *uv_udp_t, multicast_addr: [*]const u8, interface_addr: [*]const u8, membership: uv_membership): c_int;
pub extern fn uv_udp_set_source_membership(handle: *uv_udp_t, multicast_addr: [*]const u8, interface_addr: [*]const u8, source_addr: [*]const u8, membership: uv_membership): c_int;
pub extern fn uv_udp_set_multicast_loop(handle: *uv_udp_t, on: c_int): c_int;
pub extern fn uv_udp_set_multicast_ttl(handle: *uv_udp_t, ttl: c_int): c_int;
pub extern fn uv_udp_set_multicast_interface(handle: *uv_udp_t, interface_addr: [*]const u8): c_int;
pub extern fn uv_udp_set_broadcast(handle: *uv_udp_t, on: c_int): c_int;
pub extern fn uv_udp_set_ttl(handle: *uv_udp_t, ttl: c_int): c_int;
pub extern fn uv_udp_send(req: [*c]uv_udp_send_t, handle: *uv_udp_t, bufs: [*]const uv_buf_t, nbufs: c_uint, addr: [*c]const sockaddr, send_cb: uv_udp_send_cb): c_int;
pub extern fn uv_udp_try_send(handle: *uv_udp_t, bufs: [*]const uv_buf_t, nbufs: c_uint, addr: [*c]const sockaddr): c_int;
pub extern fn uv_udp_recv_start(handle: *uv_udp_t, alloc_cb: uv_alloc_cb, recv_cb: uv_udp_recv_cb): c_int;
pub extern fn uv_udp_using_recvmmsg(handle: *const uv_udp_t): c_int;
pub extern fn uv_udp_recv_stop(handle: *uv_udp_t): c_int;
pub extern fn uv_udp_get_send_queue_size(handle: *const uv_udp_t): usize;
pub extern fn uv_udp_get_send_queue_count(handle: *const uv_udp_t): usize;
pub const UV_TTY_MODE_NORMAL: c_int = 0;
pub const UV_TTY_MODE_RAW: c_int = 1;
pub const UV_TTY_MODE_IO: c_int = 2;
pub const uv_tty_mode_t = c_uint;
pub const UV_TTY_SUPPORTED: c_int = 0;
pub const UV_TTY_UNSUPPORTED: c_int = 1;
pub const uv_tty_vtermstate_t = c_uint;
pub extern fn uv_tty_init(arg0: *uv_loop_t, arg1: [*c]uv_tty_t, fd: uv_file, readable: c_int): ReturnCode;
pub extern fn uv_tty_set_mode(arg0: *uv_tty_t, mode: uv_tty_mode_t): ReturnCode;
pub extern fn uv_tty_reset_mode(): c_int;
pub extern fn uv_tty_get_winsize(arg0: [*c]uv_tty_t, width: [*c]c_int, height: [*c]c_int): c_int;
pub extern fn uv_tty_set_vterm_state(state: uv_tty_vtermstate_t);
pub extern fn uv_tty_get_vterm_state(state: [*c]uv_tty_vtermstate_t): c_int;
pub extern fn uv_guess_handle(file: uv_file): uv_handle_type;
pub const UV_PIPE_NO_TRUNCATE: c_int = 1;
pub extern fn uv_pipe_init(arg0: *uv_loop_t, handle: *Pipe, ipc: c_int): ReturnCode;
pub extern fn uv_pipe_open(arg0: *Pipe, file: uv_file): ReturnCode;
pub extern fn uv_pipe_bind(handle: *Pipe, name: [*]const u8): c_int;
pub extern fn uv_pipe_bind2(handle: *Pipe, name: [*]const u8, namelen: usize, flags: c_uint): ReturnCode;
pub extern fn uv_pipe_connect(req: [*c]uv_connect_t, handle: *Pipe, name: [*]const u8, cb: uv_connect_cb);
pub extern fn uv_pipe_connect2(req: [*c]uv_connect_t, handle: *Pipe, name: [*]const u8, namelen: usize, flags: c_uint, cb: uv_connect_cb): ReturnCode;
pub extern fn uv_pipe_getsockname(handle: *const Pipe, buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_pipe_getpeername(handle: *const Pipe, buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_pipe_pending_instances(handle: *Pipe, count: c_int);
pub extern fn uv_pipe_pending_count(handle: *Pipe): c_int;
pub extern fn uv_pipe_pending_type(handle: *Pipe): uv_handle_type;
pub extern fn uv_pipe_chmod(handle: *Pipe, flags: c_int): c_int;
pub const UV_READABLE: c_int = 1;
pub const UV_WRITABLE: c_int = 2;
pub const UV_DISCONNECT: c_int = 4;
pub const UV_PRIORITIZED: c_int = 8;
pub const enum_uv_poll_event = c_uint;
pub extern fn uv_poll_init(loop: *uv_loop_t, handle: *uv_poll_t, fd: c_int): c_int;
pub extern fn uv_poll_init_socket(loop: *uv_loop_t, handle: *uv_poll_t, socket: uv_os_sock_t): c_int;
pub extern fn uv_poll_start(handle: *uv_poll_t, events: c_int, cb: uv_poll_cb): c_int;
pub extern fn uv_poll_stop(handle: *uv_poll_t): c_int;
pub extern fn uv_prepare_init(arg0: *uv_loop_t, prepare: *uv_prepare_t): c_int;
pub extern fn uv_prepare_start(prepare: *uv_prepare_t, cb: uv_prepare_cb): c_int;
pub extern fn uv_prepare_stop(prepare: *uv_prepare_t): c_int;
pub extern fn uv_check_init(arg0: *uv_loop_t, check: *uv_check_t): c_int;
pub extern fn uv_check_start(check: *uv_check_t, cb: uv_check_cb): c_int;
pub extern fn uv_check_stop(check: *uv_check_t): c_int;
pub extern fn uv_idle_init(arg0: *uv_loop_t, idle: *uv_idle_t): c_int;
pub extern fn uv_idle_start(idle: *uv_idle_t, cb: uv_idle_cb): c_int;
pub extern fn uv_idle_stop(idle: *uv_idle_t): c_int;
pub extern fn uv_async_init(arg0: *uv_loop_t, async: *uv_async_t, async_cb: uv_async_cb): c_int;
pub extern fn uv_async_send(async: *uv_async_t): c_int;
pub extern fn uv_timer_init(arg0: *uv_loop_t, handle: *Timer): c_int;
pub extern fn uv_timer_start(handle: *Timer, cb: uv_timer_cb, timeout: u64, repeat: u64): c_int;
pub extern fn uv_timer_stop(handle: *Timer): c_int;
pub extern fn uv_timer_again(handle: *Timer): c_int;
pub extern fn uv_timer_set_repeat(handle: *Timer, repeat: u64);
pub extern fn uv_timer_get_repeat(handle: *const Timer): u64;
pub extern fn uv_timer_get_due_in(handle: *const Timer): u64;
pub extern fn uv_getaddrinfo(loop: *uv_loop_t, req: *uv_getaddrinfo_t, getaddrinfo_cb: uv_getaddrinfo_cb, node: [*:0]const u8, service: [*:0]const u8, hints: (*const void)?): ReturnCode;
pub extern fn uv_freeaddrinfo(ai: *!);
pub extern fn uv_getnameinfo(loop: *uv_loop_t, req: [*c]uv_getnameinfo_t, getnameinfo_cb: uv_getnameinfo_cb, addr: [*c]const sockaddr, flags: c_int): c_int;
pub const UV_IGNORE = 0;
pub const UV_CREATE_PIPE = 1;
pub const UV_INHERIT_FD = 2;
pub const UV_INHERIT_STREAM = 4;
pub const UV_READABLE_PIPE = 16;
pub const UV_WRITABLE_PIPE = 32;
pub const UV_NONBLOCK_PIPE = 64;
pub const UV_OVERLAPPED_PIPE = 64;
pub const uv_stdio_flags = c_uint;
pub struct StdioFlags {
    pub const ignore = UV_IGNORE;
    pub const create_pipe = UV_CREATE_PIPE;
    pub const inherit_fd = UV_INHERIT_FD;
    pub const inherit_stream = UV_INHERIT_STREAM;
    pub const readable_pipe = UV_READABLE_PIPE;
    pub const writable_pipe = UV_WRITABLE_PIPE;
    pub const nonblock_pipe = UV_NONBLOCK_PIPE;
    pub const overlapped_pipe = UV_OVERLAPPED_PIPE;
}

pub fn socketpair(stdio_flag_1: uv_stdio_flags, stdio_flag_2: uv_stdio_flags): Maybe([2]SOCKET) {
    let pair: [2]uv_os_sock_t = undefined;
    // https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket
    const SOCK_STREAM = 1;

    if (e := uv_socketpair(0, SOCK_STREAM, &pair, stdio_flag_1, stdio_flag_2).toError(.open)) {
        return { err: e };
    }

    return { result: pair };
}

pub fn pipe(stdio_flag_1: uv_stdio_flags, stdio_flag_2: uv_stdio_flags): Maybe([2]SOCKET) {
    let pair: [2]uv_file = undefined;
    // https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket
    const SOCK_STREAM = 1;

    if (e := uv_socketpair(0, SOCK_STREAM, &pair, stdio_flag_1, stdio_flag_2).toError(.open)) {
        return { err: e };
    }

    return { result: pair };
}

@c union union_unnamed_463 {
    stream: *uv_stream_t,
    fd: c_int,
}
@c pub struct struct_uv_stdio_container_s {
    flags: uv_stdio_flags,
    data: union_unnamed_463,
}
pub const uv_stdio_container_t = struct_uv_stdio_container_s;
@c pub struct uv_process_options_t {
    exit_cb: uv_exit_cb,
    file: [*:0]const u8,
    // In libuv, this is not 'const', but they never mutate it.
    args: [*:null][*:0]const u8?,
    env: [*:null][*:0]const u8?,
    cwd: [*:0]const u8,
    flags: c_uint,
    stdio_count: c_int,
    stdio: [*]uv_stdio_container_t,
    uid: uv_uid_t,
    gid: uv_gid_t,
    /// Windows only: HPCON from CreatePseudoConsole. When non-null, the child
    /// is attached to the pseudoconsole and stdio[] is not inherited.
    pseudoconsole: *!? = null,
}
pub const UV_PROCESS_SETUID: c_int = 1;
pub const UV_PROCESS_SETGID: c_int = 2;
pub const UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS: c_int = 4;
pub const UV_PROCESS_DETACHED: c_int = 8;
pub const UV_PROCESS_WINDOWS_HIDE: c_int = 16;
pub const UV_PROCESS_WINDOWS_HIDE_CONSOLE: c_int = 32;
pub const UV_PROCESS_WINDOWS_HIDE_GUI: c_int = 64;
pub const enum_uv_process_flags = c_uint;
pub extern fn uv_spawn(loop: *uv_loop_t, handle: *uv_process_t, options: *const uv_process_options_t): ReturnCode;
pub extern fn uv_process_kill(arg0: *uv_process_t, signum: c_int): ReturnCode;
pub extern fn uv_kill(pid: c_int, signum: c_int): ReturnCode;
pub extern fn uv_process_get_pid(arg0: *const uv_process_t): uv_pid_t;
pub extern fn uv_queue_work(loop: *uv_loop_t, req: [*c]uv_work_t, work_cb: uv_work_cb, after_work_cb: uv_after_work_cb): c_int;
pub extern fn uv_cancel(req: [*c]uv_req_t): c_int;
pub const UV_DIRENT_UNKNOWN: c_int = 0;
pub const UV_DIRENT_FILE: c_int = 1;
pub const UV_DIRENT_DIR: c_int = 2;
pub const UV_DIRENT_LINK: c_int = 3;
pub const UV_DIRENT_FIFO: c_int = 4;
pub const UV_DIRENT_SOCKET: c_int = 5;
pub const UV_DIRENT_CHAR: c_int = 6;
pub const UV_DIRENT_BLOCK: c_int = 7;
pub const uv_dirent_type_t = c_uint;
pub extern fn uv_setup_args(argc: c_int, argv: [*c][*]u8): [*c][*]u8;
pub extern fn uv_get_process_title(buffer: [*]u8, size: usize): c_int;
pub extern fn uv_set_process_title(title: [*]const u8): c_int;
pub extern fn uv_resident_set_memory(rss: [*c]usize): c_int;
pub extern fn uv_uptime(uptime: [*c]f64): c_int;
@c pub struct uv_rusage_t {
    ru_utime: uv_timeval_t,
    ru_stime: uv_timeval_t,
    ru_maxrss: u64,
    ru_ixrss: u64,
    ru_idrss: u64,
    ru_isrss: u64,
    ru_minflt: u64,
    ru_majflt: u64,
    ru_nswap: u64,
    ru_inblock: u64,
    ru_oublock: u64,
    ru_msgsnd: u64,
    ru_msgrcv: u64,
    ru_nsignals: u64,
    ru_nvcsw: u64,
    ru_nivcsw: u64,
}
pub extern fn uv_getrusage(rusage: [*c]uv_rusage_t): c_int;
pub extern fn uv_os_homedir(buffer: [*]u8, size: *usize): ReturnCode;
pub extern fn uv_os_tmpdir(buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_os_get_passwd(pwd: [*c]uv_passwd_t): c_int;
pub extern fn uv_os_free_passwd(pwd: [*c]uv_passwd_t);
pub extern fn uv_os_get_passwd2(pwd: [*c]uv_passwd_t, uid: uv_uid_t): c_int;
pub extern fn uv_os_get_group(grp: [*c]uv_group_t, gid: uv_uid_t): c_int;
pub extern fn uv_os_free_group(grp: [*c]uv_group_t);
pub extern fn uv_os_getpid(): uv_pid_t;
pub extern fn uv_os_getppid(): uv_pid_t;
pub extern fn uv_os_getpriority(pid: uv_pid_t, priority: [*c]c_int): c_int;
pub extern fn uv_os_setpriority(pid: uv_pid_t, priority: c_int): c_int;
pub extern fn uv_available_parallelism(): c_uint;
pub extern fn uv_cpu_info(cpu_infos: *[*]uv_cpu_info_t, count: *c_int): c_int;
pub extern fn uv_free_cpu_info(cpu_infos: [*c]uv_cpu_info_t, count: c_int);
pub extern fn uv_cpumask_size(): c_int;
pub extern fn uv_interface_addresses(addresses: *[*]uv_interface_address_t, count: [*c]c_int): c_int;
pub extern fn uv_free_interface_addresses(addresses: [*]uv_interface_address_t, count: c_int);
pub extern fn uv_os_environ(envitems: [*c][*c]uv_env_item_t, count: [*c]c_int): c_int;
pub extern fn uv_os_free_environ(envitems: [*c]uv_env_item_t, count: c_int);
pub extern fn uv_os_getenv(name: [*]const u8, buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_os_setenv(name: [*]const u8, value: [*]const u8): c_int;
pub extern fn uv_os_unsetenv(name: [*]const u8): c_int;
pub extern fn uv_os_gethostname(buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_os_uname(buffer: [*c]uv_utsname_t): c_int;
pub extern fn uv_metrics_info(loop: *uv_loop_t, metrics: [*c]uv_metrics_t): c_int;
pub extern fn uv_metrics_idle_time(loop: *uv_loop_t): u64;
pub const UV_FS_UNKNOWN: c_int = -1;
pub const UV_FS_CUSTOM: c_int = 0;
pub const UV_FS_OPEN: c_int = 1;
pub const UV_FS_CLOSE: c_int = 2;
pub const UV_FS_READ: c_int = 3;
pub const UV_FS_WRITE: c_int = 4;
pub const UV_FS_SENDFILE: c_int = 5;
pub const UV_FS_STAT: c_int = 6;
pub const UV_FS_LSTAT: c_int = 7;
pub const UV_FS_FSTAT: c_int = 8;
pub const UV_FS_FTRUNCATE: c_int = 9;
pub const UV_FS_UTIME: c_int = 10;
pub const UV_FS_FUTIME: c_int = 11;
pub const UV_FS_ACCESS: c_int = 12;
pub const UV_FS_CHMOD: c_int = 13;
pub const UV_FS_FCHMOD: c_int = 14;
pub const UV_FS_FSYNC: c_int = 15;
pub const UV_FS_FDATASYNC: c_int = 16;
pub const UV_FS_UNLINK: c_int = 17;
pub const UV_FS_RMDIR: c_int = 18;
pub const UV_FS_MKDIR: c_int = 19;
pub const UV_FS_MKDTEMP: c_int = 20;
pub const UV_FS_RENAME: c_int = 21;
pub const UV_FS_SCANDIR: c_int = 22;
pub const UV_FS_LINK: c_int = 23;
pub const UV_FS_SYMLINK: c_int = 24;
pub const UV_FS_READLINK: c_int = 25;
pub const UV_FS_CHOWN: c_int = 26;
pub const UV_FS_FCHOWN: c_int = 27;
pub const UV_FS_REALPATH: c_int = 28;
pub const UV_FS_COPYFILE: c_int = 29;
pub const UV_FS_LCHOWN: c_int = 30;
pub const UV_FS_OPENDIR: c_int = 31;
pub const UV_FS_READDIR: c_int = 32;
pub const UV_FS_CLOSEDIR: c_int = 33;
pub const UV_FS_STATFS: c_int = 34;
pub const UV_FS_MKSTEMP: c_int = 35;
pub const UV_FS_LUTIME: c_int = 36;
pub const uv_fs_type = c_int;
pub extern fn uv_fs_get_type(arg0: *const fs_t): uv_fs_type;
pub extern fn uv_fs_get_result(arg0: *const fs_t): isize;
pub extern fn uv_fs_get_system_error(arg0: *const fs_t): c_int;
pub extern fn uv_fs_get_ptr(arg0: *const fs_t): *!?;
pub extern fn uv_fs_get_path(arg0: *const fs_t): [*:0]const u8;
pub extern fn uv_fs_get_statbuf(arg0: *fs_t): *uv_stat_t;
pub extern fn uv_fs_req_cleanup(req: *fs_t);
pub extern fn uv_fs_close(loop: *uv_loop_t, req: *fs_t, file: uv_file, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_open(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, flags: c_int, mode: c_int, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_read(loop: *uv_loop_t, req: *fs_t, file: uv_file, bufs: [*]const uv_buf_t, nbufs: c_uint, offset: i64, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_unlink(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_write(loop: *uv_loop_t, req: *fs_t, file: uv_file, bufs: [*]const uv_buf_t, nbufs: c_uint, offset: i64, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_copyfile(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, new_path: [*:0]const u8, flags: c_int, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_mkdir(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, mode: c_int, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_mkdtemp(loop: *uv_loop_t, req: *fs_t, tpl: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_mkstemp(loop: *uv_loop_t, req: *fs_t, tpl: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_rmdir(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_scandir(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, flags: c_int, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_scandir_next(req: *fs_t, ent: *uv_dirent_t): ReturnCode;
pub extern fn uv_fs_opendir(loop: *uv_loop_t, req: *fs_t, path: [*]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_readdir(loop: *uv_loop_t, req: *fs_t, dir: *uv_dir_t, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_closedir(loop: *uv_loop_t, req: *fs_t, dir: *uv_dir_t, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_stat(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_fstat(loop: *uv_loop_t, req: *fs_t, file: uv_file, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_rename(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, new_path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_fsync(loop: *uv_loop_t, req: *fs_t, file: uv_file, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_fdatasync(loop: *uv_loop_t, req: *fs_t, file: uv_file, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_ftruncate(loop: *uv_loop_t, req: *fs_t, file: uv_file, offset: i64, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_sendfile(loop: *uv_loop_t, req: *fs_t, out_fd: uv_file, in_fd: uv_file, in_offset: i64, length: usize, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_access(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, mode: c_int, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_chmod(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, mode: c_int, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_utime(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, atime: f64, mtime: f64, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_futime(loop: *uv_loop_t, req: *fs_t, file: uv_file, atime: f64, mtime: f64, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_lutime(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, atime: f64, mtime: f64, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_lstat(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_link(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, new_path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_symlink(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, new_path: [*:0]const u8, flags: c_int, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_readlink(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_realpath(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_fchmod(loop: *uv_loop_t, req: *fs_t, file: uv_file, mode: c_int, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_chown(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, uid: uv_uid_t, gid: uv_gid_t, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_fchown(loop: *uv_loop_t, req: *fs_t, file: uv_file, uid: uv_uid_t, gid: uv_gid_t, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_lchown(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, uid: uv_uid_t, gid: uv_gid_t, cb: uv_fs_cb): ReturnCode;
pub extern fn uv_fs_statfs(loop: *uv_loop_t, req: *fs_t, path: [*:0]const u8, cb: uv_fs_cb): ReturnCode;
pub const UV_RENAME: c_int = 1;
pub const UV_CHANGE: c_int = 2;
pub const enum_uv_fs_event = c_uint;
pub extern fn uv_fs_poll_init(loop: *uv_loop_t, handle: *uv_fs_poll_t): c_int;
pub extern fn uv_fs_poll_start(handle: *uv_fs_poll_t, poll_cb: uv_fs_poll_cb, path: [*:0]const u8, interval: c_uint): c_int;
pub extern fn uv_fs_poll_stop(handle: *uv_fs_poll_t): c_int;
pub extern fn uv_fs_poll_getpath(handle: *uv_fs_poll_t, buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_signal_init(loop: *uv_loop_t, handle: *uv_signal_t): ReturnCode;
pub extern fn uv_signal_start(handle: *uv_signal_t, signal_cb: uv_signal_cb, signum: c_int): ReturnCode;
pub extern fn uv_signal_start_oneshot(handle: *uv_signal_t, signal_cb: uv_signal_cb, signum: c_int): ReturnCode;
pub extern fn uv_signal_stop(handle: *uv_signal_t): ReturnCode;
pub extern fn uv_loadavg(avg: [*c]f64);
pub const UV_FS_EVENT_WATCH_ENTRY: c_int = 1;
pub const UV_FS_EVENT_STAT: c_int = 2;
pub const UV_FS_EVENT_RECURSIVE: c_int = 4;
pub const enum_uv_fs_event_flags = c_uint;
pub extern fn uv_fs_event_init(loop: *uv_loop_t, handle: *uv_fs_event_t): ReturnCode;
pub extern fn uv_fs_event_start(handle: *uv_fs_event_t, cb: uv_fs_event_cb, path: [*:0]const u8, flags: c_uint): ReturnCode;
/// always returns zero
pub extern fn uv_fs_event_stop(handle: *uv_fs_event_t): c_int;
pub extern fn uv_fs_event_getpath(handle: *uv_fs_event_t, buffer: [*]u8, size: *usize): ReturnCode;
pub extern fn uv_ip4_addr(ip: [*]const u8, port: c_int, addr: [*c]sockaddr_in): c_int;
pub extern fn uv_ip6_addr(ip: [*]const u8, port: c_int, addr: (*sockaddr_in6)?): c_int;
pub extern fn uv_ip4_name(src: [*c]const sockaddr_in, dst: [*]u8, size: usize): c_int;
pub extern fn uv_ip6_name(src: (*const sockaddr_in6)?, dst: [*]u8, size: usize): c_int;
pub extern fn uv_ip_name(src: [*c]const sockaddr, dst: [*]u8, size: usize): c_int;
pub extern fn uv_inet_ntop(af: c_int, src: (*const void)?, dst: [*]u8, size: usize): c_int;
pub extern fn uv_inet_pton(af: c_int, src: [*]const u8, dst: *!?): c_int;
pub extern fn uv_random(loop: *uv_loop_t, req: [*c]uv_random_t, buf: *!?, buflen: usize, flags: c_uint, cb: uv_random_cb): c_int;
pub extern fn uv_if_indextoname(ifindex: c_uint, buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_if_indextoiid(ifindex: c_uint, buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_exepath(buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_cwd(buffer: [*]u8, size: [*c]usize): c_int;
pub extern fn uv_chdir(dir: [*]const u8): c_int;
pub extern fn uv_get_free_memory(): u64;
pub extern fn uv_get_total_memory(): u64;
pub extern fn uv_get_constrained_memory(): u64;
pub extern fn uv_get_available_memory(): u64;
pub extern fn uv_clock_gettime(clock_id: uv_clock_id, ts: [*c]uv_timespec64_t): c_int;
pub extern fn uv_hrtime(): u64;
pub extern fn uv_sleep(msec: c_uint);
pub extern fn uv_disable_stdio_inheritance();
pub extern fn uv_dlopen(filename: [*]const u8, lib: [*c]uv_lib_t): c_int;
pub extern fn uv_dlclose(lib: [*c]uv_lib_t);
pub extern fn uv_dlsym(lib: [*c]uv_lib_t, name: [*]const u8, ptr: [*c]*!?): c_int;
pub extern fn uv_dlerror(lib: [*c]const uv_lib_t): [*]const u8;
// pub extern fn uv_mutex_init(handle: *uv_mutex_t) c_int;
// pub extern fn uv_mutex_init_recursive(handle: *uv_mutex_t) c_int;
// pub extern fn uv_mutex_destroy(handle: *uv_mutex_t) void;
// pub extern fn uv_mutex_lock(handle: *uv_mutex_t) void;
// pub extern fn uv_mutex_trylock(handle: *uv_mutex_t) c_int;
// pub extern fn uv_mutex_unlock(handle: *uv_mutex_t) void;
// pub extern fn uv_rwlock_init(rwlock: [*c]uv_rwlock_t) c_int;
// pub extern fn uv_rwlock_destroy(rwlock: [*c]uv_rwlock_t) void;
// pub extern fn uv_rwlock_rdlock(rwlock: [*c]uv_rwlock_t) void;
// pub extern fn uv_rwlock_tryrdlock(rwlock: [*c]uv_rwlock_t) c_int;
// pub extern fn uv_rwlock_rdunlock(rwlock: [*c]uv_rwlock_t) void;
// pub extern fn uv_rwlock_wrlock(rwlock: [*c]uv_rwlock_t) void;
// pub extern fn uv_rwlock_trywrlock(rwlock: [*c]uv_rwlock_t) c_int;
// pub extern fn uv_rwlock_wrunlock(rwlock: [*c]uv_rwlock_t) void;
// pub extern fn uv_sem_init(sem: [*c]uv_sem_t, value: c_uint) c_int;
// pub extern fn uv_sem_destroy(sem: [*c]uv_sem_t) void;
// pub extern fn uv_sem_post(sem: [*c]uv_sem_t) void;
// pub extern fn uv_sem_wait(sem: [*c]uv_sem_t) void;
// pub extern fn uv_sem_trywait(sem: [*c]uv_sem_t) c_int;
// pub extern fn uv_cond_init(cond: [*c]uv_cond_t) c_int;
// pub extern fn uv_cond_destroy(cond: [*c]uv_cond_t) void;
// pub extern fn uv_cond_signal(cond: [*c]uv_cond_t) void;
// pub extern fn uv_cond_broadcast(cond: [*c]uv_cond_t) void;
// pub extern fn uv_barrier_init(barrier: [*c]uv_barrier_t, count: c_uint) c_int;
// pub extern fn uv_barrier_destroy(barrier: [*c]uv_barrier_t) void;
// pub extern fn uv_barrier_wait(barrier: [*c]uv_barrier_t) c_int;
// pub extern fn uv_cond_wait(cond: [*c]uv_cond_t, mutex: [*c]uv_mutex_t) void;
// pub extern fn uv_cond_timedwait(cond: [*c]uv_cond_t, mutex: [*c]uv_mutex_t, timeout: u64) c_int;
const uv_once_cb_fn = *const fn () callconv(.c) void;
pub extern fn uv_once(guard: [*c]uv_once_t, callback: uv_once_cb_fn?);
pub extern fn uv_key_create(key: [*c]uv_key_t): c_int;
pub extern fn uv_key_delete(key: [*c]uv_key_t);
pub extern fn uv_key_get(key: [*c]uv_key_t): *!?;
pub extern fn uv_key_set(key: [*c]uv_key_t, value: *!?);
pub extern fn uv_gettimeofday(tv: [*c]uv_timeval64_t): c_int;
const uv_thread_cb_fn = *const fn (*!?) callconv(.c) void;
pub type uv_thread_cb = uv_thread_cb_fn?;
pub extern fn uv_thread_create(tid: [*c]uv_thread_t, entry: uv_thread_cb, arg: *!?): c_int;
pub const UV_THREAD_NO_FLAGS: c_int = 0;
pub const UV_THREAD_HAS_STACK_SIZE: c_int = 1;
pub const uv_thread_create_flags = c_uint;
@c pub struct struct_uv_thread_options_s {
    flags: c_uint,
    stack_size: usize,
}
pub const uv_thread_options_t = struct_uv_thread_options_s;
pub extern fn uv_thread_create_ex(tid: [*c]uv_thread_t, params: [*c]const uv_thread_options_t, entry: uv_thread_cb, arg: *!?): c_int;
pub extern fn uv_thread_setaffinity(tid: [*c]uv_thread_t, cpumask: [*]u8, oldmask: [*]u8, mask_size: usize): c_int;
pub extern fn uv_thread_getaffinity(tid: [*c]uv_thread_t, cpumask: [*]u8, mask_size: usize): c_int;
pub extern fn uv_thread_getcpu(): c_int;
pub extern fn uv_thread_self(): uv_thread_t;
pub extern fn uv_thread_join(tid: [*c]uv_thread_t): c_int;
pub extern fn uv_thread_equal(t1: [*c]const uv_thread_t, t2: [*c]const uv_thread_t): c_int;
@c pub union union_uv_any_handle {
    @"async": uv_async_t,
    check: uv_check_t,
    fs_event: uv_fs_event_t,
    fs_poll: uv_fs_poll_t,
    handle: uv_handle_t,
    idle: uv_idle_t,
    pipe: Pipe,
    poll: uv_poll_t,
    prepare: uv_prepare_t,
    process: uv_process_t,
    stream: uv_stream_t,
    tcp: uv_tcp_t,
    timer: Timer,
    tty: uv_tty_t,
    udp: uv_udp_t,
    signal: uv_signal_t,
}
@c pub union union_uv_any_req {
    req: uv_req_t,
    connect: uv_connect_t,
    write: uv_write_t,
    shutdown: uv_shutdown_t,
    udp_send: uv_udp_send_t,
    fs: fs_t,
    work: uv_work_t,
    getaddrinfo: uv_getaddrinfo_t,
    getnameinfo: uv_getnameinfo_t,
    random: uv_random_t,
}
pub extern fn uv_loop_get_data(arg0: [*c]const uv_loop_t): *!?;
pub extern fn uv_loop_set_data(arg0: *uv_loop_t, data: *!?);

pub const UV_HANDLE_CLOSED: c_int = 0x00000002;

// uv_is_closing checks for closing or closed, we need to know if is indeed closed so we can deinit without call uv_close
pub fn uv_is_closed(handle: *const uv_handle_t): bool {
    return ((handle.flags & UV_HANDLE_CLOSED) != 0);
}

pub fn translateUVErrorToE(code_in: auto): bun.sys.E {
    const code: c_int = i32(code_in);

    return switch (code) {
        UV_EPERM => bun.sys.E.PERM,
        UV_ENOENT => bun.sys.E.NOENT,
        UV_ESRCH => bun.sys.E.SRCH,
        UV_EINTR => bun.sys.E.INTR,
        UV_EIO => bun.sys.E.IO,
        UV_ENXIO => bun.sys.E.NXIO,
        UV_E2BIG => bun.sys.E.@"2BIG",
        UV_ENOEXEC => bun.sys.E.NOEXEC,
        UV_EBADF => bun.sys.E.BADF,
        UV_EAGAIN => bun.sys.E.AGAIN,
        UV_ENOMEM => bun.sys.E.NOMEM,
        UV_EACCES => bun.sys.E.ACCES,
        UV_EFAULT => bun.sys.E.FAULT,
        UV_EBUSY => bun.sys.E.BUSY,
        UV_EEXIST => bun.sys.E.EXIST,
        UV_EXDEV => bun.sys.E.XDEV,
        UV_ENODEV => bun.sys.E.NODEV,
        UV_ENOTDIR => bun.sys.E.NOTDIR,
        UV_EISDIR => bun.sys.E.ISDIR,
        UV_EINVAL => bun.sys.E.INVAL,
        UV_ENFILE => bun.sys.E.NFILE,
        UV_EMFILE => bun.sys.E.MFILE,
        UV_ENOTTY => bun.sys.E.NOTTY,
        UV_EFTYPE => bun.sys.E.FTYPE,
        UV_ETXTBSY => bun.sys.E.TXTBSY,
        UV_EFBIG => bun.sys.E.FBIG,
        UV_ENOSPC => bun.sys.E.NOSPC,
        UV_ESPIPE => bun.sys.E.SPIPE,
        UV_EROFS => bun.sys.E.ROFS,
        UV_EMLINK => bun.sys.E.MLINK,
        UV_EPIPE => bun.sys.E.PIPE,
        UV_ERANGE => bun.sys.E.RANGE,
        UV_ENAMETOOLONG => bun.sys.E.NAMETOOLONG,
        UV_ENOSYS => bun.sys.E.NOSYS,
        UV_ENOTEMPTY => bun.sys.E.NOTEMPTY,
        UV_ELOOP => bun.sys.E.LOOP,
        UV_EUNATCH => bun.sys.E.UNATCH,
        UV_ENODATA => bun.sys.E.NODATA,
        UV_ENONET => bun.sys.E.NONET,
        UV_EPROTO => bun.sys.E.PROTO,
        UV_EOVERFLOW => bun.sys.E.OVERFLOW,
        UV_EILSEQ => bun.sys.E.ILSEQ,
        UV_ENOTSOCK => bun.sys.E.NOTSOCK,
        UV_EDESTADDRREQ => bun.sys.E.DESTADDRREQ,
        UV_EMSGSIZE => bun.sys.E.MSGSIZE,
        UV_EPROTOTYPE => bun.sys.E.PROTOTYPE,
        UV_ENOPROTOOPT => bun.sys.E.NOPROTOOPT,
        UV_EPROTONOSUPPORT => bun.sys.E.PROTONOSUPPORT,
        UV_ESOCKTNOSUPPORT => bun.sys.E.SOCKTNOSUPPORT,
        UV_ENOTSUP => bun.sys.E.NOTSUP,
        UV_EAFNOSUPPORT => bun.sys.E.AFNOSUPPORT,
        UV_EADDRINUSE => bun.sys.E.ADDRINUSE,
        UV_EADDRNOTAVAIL => bun.sys.E.ADDRNOTAVAIL,
        UV_ENETDOWN => bun.sys.E.NETDOWN,
        UV_ENETUNREACH => bun.sys.E.NETUNREACH,
        UV_ECONNABORTED => bun.sys.E.CONNABORTED,
        UV_ECONNRESET => bun.sys.E.CONNRESET,
        UV_ENOBUFS => bun.sys.E.NOBUFS,
        UV_EISCONN => bun.sys.E.ISCONN,
        UV_ENOTCONN => bun.sys.E.NOTCONN,
        UV_ESHUTDOWN => bun.sys.E.SHUTDOWN,
        UV_ETIMEDOUT => bun.sys.E.TIMEDOUT,
        UV_ECONNREFUSED => bun.sys.E.CONNREFUSED,
        UV_EHOSTDOWN => bun.sys.E.HOSTDOWN,
        UV_EHOSTUNREACH => bun.sys.E.HOSTUNREACH,
        UV_EALREADY => bun.sys.E.ALREADY,
        UV_EREMOTEIO => bun.sys.E.REMOTEIO,
        UV_ECANCELED => bun.sys.E.CANCELED,
        UV_ECHARSET => bun.sys.E.CHARSET,
        UV_EOF => bun.sys.E.EOF,
        UV_UNKNOWN => bun.sys.E.UNKNOWN,
        // libuv can return codes not explicitly mapped above (e.g. Windows-specific
        // codes in the -4000s). `bun.sys.E` is exhaustive, so a strict @enumFromInt
        // on an unmapped value panics in safe builds. Fall back to UNKNOWN instead.
        // Wrapping negation so minInt(c_int) maps to UNKNOWN instead of overflowing.
        else => std.@"meta".intToEnum(bun.sys.E, 0 -% code) catch bun.sys.E.UNKNOWN,
    };
}

pub enum ReturnCode: c_int {
    zero = 0,
    _,

    pub fn format(writer: *std.Io.Writer)! {
        if (e := _.errEnum()) {
            writer.writeAll(@tagName(e))?;
        } else {
            writer.print("{d}", .{@intFromEnum(_)})?;
        }
    }

    fn int(): c_int {
        return @intFromEnum(_);
    }

    pub fn toError(syscall: bun.sys.Tag): bun.sys.Error? {
        if (e := _.errno()) {
            return {
                errno: e,
                syscall: syscall,
            };
        }

        return null;
    }

    fn errno(): u16? {
        return if (_.int() < 0)
            switch (_.int()) {
                UV_EPERM => @intFromEnum(bun.sys.E.PERM),
                UV_ENOENT => @intFromEnum(bun.sys.E.NOENT),
                UV_ESRCH => @intFromEnum(bun.sys.E.SRCH),
                UV_EINTR => @intFromEnum(bun.sys.E.INTR),
                UV_EIO => @intFromEnum(bun.sys.E.IO),
                UV_ENXIO => @intFromEnum(bun.sys.E.NXIO),
                UV_E2BIG => @intFromEnum(bun.sys.E.@"2BIG"),
                UV_ENOEXEC => @intFromEnum(bun.sys.E.NOEXEC),
                UV_EBADF => @intFromEnum(bun.sys.E.BADF),
                UV_EAGAIN => @intFromEnum(bun.sys.E.AGAIN),
                UV_ENOMEM => @intFromEnum(bun.sys.E.NOMEM),
                UV_EACCES => @intFromEnum(bun.sys.E.ACCES),
                UV_EFAULT => @intFromEnum(bun.sys.E.FAULT),
                UV_EBUSY => @intFromEnum(bun.sys.E.BUSY),
                UV_EEXIST => @intFromEnum(bun.sys.E.EXIST),
                UV_EXDEV => @intFromEnum(bun.sys.E.XDEV),
                UV_ENODEV => @intFromEnum(bun.sys.E.NODEV),
                UV_ENOTDIR => @intFromEnum(bun.sys.E.NOTDIR),
                UV_EISDIR => @intFromEnum(bun.sys.E.ISDIR),
                UV_EINVAL => @intFromEnum(bun.sys.E.INVAL),
                UV_ENFILE => @intFromEnum(bun.sys.E.NFILE),
                UV_EMFILE => @intFromEnum(bun.sys.E.MFILE),
                UV_ENOTTY => @intFromEnum(bun.sys.E.NOTTY),
                UV_EFTYPE => @intFromEnum(bun.sys.E.FTYPE),
                UV_ETXTBSY => @intFromEnum(bun.sys.E.TXTBSY),
                UV_EFBIG => @intFromEnum(bun.sys.E.FBIG),
                UV_ENOSPC => @intFromEnum(bun.sys.E.NOSPC),
                UV_ESPIPE => @intFromEnum(bun.sys.E.SPIPE),
                UV_EROFS => @intFromEnum(bun.sys.E.ROFS),
                UV_EMLINK => @intFromEnum(bun.sys.E.MLINK),
                UV_EPIPE => @intFromEnum(bun.sys.E.PIPE),
                UV_ERANGE => @intFromEnum(bun.sys.E.RANGE),
                UV_ENAMETOOLONG => @intFromEnum(bun.sys.E.NAMETOOLONG),
                UV_ENOSYS => @intFromEnum(bun.sys.E.NOSYS),
                UV_ENOTEMPTY => @intFromEnum(bun.sys.E.NOTEMPTY),
                UV_ELOOP => @intFromEnum(bun.sys.E.LOOP),
                UV_EUNATCH => @intFromEnum(bun.sys.E.UNATCH),
                UV_ENODATA => @intFromEnum(bun.sys.E.NODATA),
                UV_ENONET => @intFromEnum(bun.sys.E.NONET),
                UV_EPROTO => @intFromEnum(bun.sys.E.PROTO),
                UV_EOVERFLOW => @intFromEnum(bun.sys.E.OVERFLOW),
                UV_EILSEQ => @intFromEnum(bun.sys.E.ILSEQ),
                UV_ENOTSOCK => @intFromEnum(bun.sys.E.NOTSOCK),
                UV_EDESTADDRREQ => @intFromEnum(bun.sys.E.DESTADDRREQ),
                UV_EMSGSIZE => @intFromEnum(bun.sys.E.MSGSIZE),
                UV_EPROTOTYPE => @intFromEnum(bun.sys.E.PROTOTYPE),
                UV_ENOPROTOOPT => @intFromEnum(bun.sys.E.NOPROTOOPT),
                UV_EPROTONOSUPPORT => @intFromEnum(bun.sys.E.PROTONOSUPPORT),
                UV_ESOCKTNOSUPPORT => @intFromEnum(bun.sys.E.SOCKTNOSUPPORT),
                UV_ENOTSUP => @intFromEnum(bun.sys.E.NOTSUP),
                UV_EAFNOSUPPORT => @intFromEnum(bun.sys.E.AFNOSUPPORT),
                UV_EADDRINUSE => @intFromEnum(bun.sys.E.ADDRINUSE),
                UV_EADDRNOTAVAIL => @intFromEnum(bun.sys.E.ADDRNOTAVAIL),
                UV_ENETDOWN => @intFromEnum(bun.sys.E.NETDOWN),
                UV_ENETUNREACH => @intFromEnum(bun.sys.E.NETUNREACH),
                UV_ECONNABORTED => @intFromEnum(bun.sys.E.CONNABORTED),
                UV_ECONNRESET => @intFromEnum(bun.sys.E.CONNRESET),
                UV_ENOBUFS => @intFromEnum(bun.sys.E.NOBUFS),
                UV_EISCONN => @intFromEnum(bun.sys.E.ISCONN),
                UV_ENOTCONN => @intFromEnum(bun.sys.E.NOTCONN),
                UV_ESHUTDOWN => @intFromEnum(bun.sys.E.SHUTDOWN),
                UV_ETIMEDOUT => @intFromEnum(bun.sys.E.TIMEDOUT),
                UV_ECONNREFUSED => @intFromEnum(bun.sys.E.CONNREFUSED),
                UV_EHOSTDOWN => @intFromEnum(bun.sys.E.HOSTDOWN),
                UV_EHOSTUNREACH => @intFromEnum(bun.sys.E.HOSTUNREACH),
                UV_EALREADY => @intFromEnum(bun.sys.E.ALREADY),
                UV_EREMOTEIO => @intFromEnum(bun.sys.E.REMOTEIO),
                UV_ECANCELED => @intFromEnum(bun.sys.E.CANCELED),
                UV_ECHARSET => @intFromEnum(bun.sys.E.CHARSET),
                UV_EOF => @intFromEnum(bun.sys.E.EOF),
                UV_UNKNOWN => @intFromEnum(bun.sys.E.UNKNOWN),
                UV_EAI_ADDRFAMILY => @intFromEnum(bun.sys.E.UV_EAI_ADDRFAMILY),
                UV_EAI_AGAIN => @intFromEnum(bun.sys.E.UV_EAI_AGAIN),
                UV_EAI_BADFLAGS => @intFromEnum(bun.sys.E.UV_EAI_BADFLAGS),
                UV_EAI_BADHINTS => @intFromEnum(bun.sys.E.UV_EAI_BADHINTS),
                UV_EAI_CANCELED => @intFromEnum(bun.sys.E.UV_EAI_CANCELED),
                UV_EAI_FAIL => @intFromEnum(bun.sys.E.UV_EAI_FAIL),
                UV_EAI_FAMILY => @intFromEnum(bun.sys.E.UV_EAI_FAMILY),
                UV_EAI_MEMORY => @intFromEnum(bun.sys.E.UV_EAI_MEMORY),
                UV_EAI_NODATA => @intFromEnum(bun.sys.E.UV_EAI_NODATA),
                UV_EAI_NONAME => @intFromEnum(bun.sys.E.UV_EAI_NONAME),
                UV_EAI_OVERFLOW => @intFromEnum(bun.sys.E.UV_EAI_OVERFLOW),
                UV_EAI_PROTOCOL => @intFromEnum(bun.sys.E.UV_EAI_PROTOCOL),
                UV_EAI_SERVICE => @intFromEnum(bun.sys.E.UV_EAI_SERVICE),
                UV_EAI_SOCKTYPE => @intFromEnum(bun.sys.E.UV_EAI_SOCKTYPE),
                else => null,
            }
        else
            null;
    }

    fn errEnum(): bun.sys.E? {
        return if (_.int() < 0)
            (translateUVErrorToE(_.int()))
        else
            null;
    }
}

pub enum ReturnCodeI64: i64 {
    zero = 0,
    _,

    pub fn init(i: i64): ReturnCodeI64 {
        return @enumFromInt(i);
    }

    pub fn format(writer: *std.Io.Writer): std.Io.Writer.Error!void {
        if (e := _.errEnum()) {
            writer.writeAll(@tagName(e))?;
        } else {
            writer.print("{d}", .{@intFromEnum(_)})?;
        }
    }

    pub fn toError(syscall: bun.sys.Tag): bun.sys.Error? {
        if (e := _.errno()) {
            return {
                errno: e,
                syscall: syscall,
            };
        }

        return null;
    }

    fn errno(): u16? {
        return if (@intFromEnum(_) < 0)
            @as(u16, i32(-@intFromEnum(_)))
        else
            null;
    }

    fn errEnum(): bun.sys.E? {
        return if (@intFromEnum(_) < 0)
            (translateUVErrorToE(@intFromEnum(_)))
        else
            null;
    }

    fn int(): i64 {
        return @intFromEnum(_);
    }

    pub fn toFD(): bun.FD {
        return .fromUV(@truncate(_.int()));
    }
}

pub const addrinfo = std.os.windows.ws2_32.addrinfo;

// https://docs.libuv.org/en/v1.x/stream.html
fn StreamMixin(comptime Type: type): type {
    return struct {
        fn getWriteQueueSize(*_): usize {
            return unsafe { uv_stream_get_write_queue_size(@ptrCast(_)) };
        }

        fn listen(*_, backlog: i32, context: auto, comptime onConnect: *const (fn (@TypeOf(context), ReturnCode) void)): Maybe(void) {
            _.data = unsafe { @ptrCast(context) };
            const Wrapper = struct {
                @callconv("c") pub fn uvConnectCb(handle: *uv_stream_t, status: ReturnCode) {
                    unsafe { onConnect(@ptrCast(@alignCast(handle.data)), status); }
                }
            };
            if (e := unsafe { uv_listen(@ptrCast(_), backlog, &Wrapper.uvConnectCb) }.toError(.listen)) {
                return { err: e };
            }
            return .success;
        }

        fn accept(*_, client: *Type): Maybe(void) {
            if (e := unsafe { uv_accept(@ptrCast(_), @ptrCast(client)) }.toError(.accept)) {
                return { err: e };
            }
            return .success;
        }

        fn readStart(
            *_,
            context: auto,
            comptime alloc_cb: *const (fn (@TypeOf(context), suggested_size: usize) []u8),
            comptime error_cb: *const (fn (@TypeOf(context), e: bun.sys.E) void),
            comptime read_cb: *const (fn (@TypeOf(context), data: []const u8) void),
        ): Maybe(void) {
            const Context = @TypeOf(context);
            _.data = unsafe { @ptrCast(context) };
            const Wrapper = struct {
                @callconv("c") pub fn uvAllocb(req: *uv_stream_t, suggested_size: usize, buffer: *uv_buf_t) {
                    const context_data: Context = unsafe { @ptrCast(@alignCast(req.data)) };
                    buffer.* = uv_buf_t.init(alloc_cb(context_data, suggested_size));
                }
                @callconv("c") pub fn uvReadcb(req: *uv_stream_t, nreads: isize, buffer: *uv_buf_t) {
                    const context_data: Context = unsafe { @ptrCast(@alignCast(req.data)) };
                    if (nreads == 0) return; // EAGAIN or EWOULDBLOCK
                    if (nreads < 0) {
                        req.readStop();
                        error_cb(context_data, ReturnCodeI64.init(nreads).errEnum() ?? bun.sys.E.CANCELED);
                    } else {
                        read_cb(context_data, unsafe { buffer.base[0..i32(nreads)] });
                    }
                }
            };

            if (e := unsafe { uv_read_start(@ptrCast(_), @ptrCast(&Wrapper.uvAllocb), @ptrCast(&Wrapper.uvReadcb)) }.toError(.listen)) {
                return { err: e };
            }
            return .success;
        }

        fn readStop(*_) {
            // always succeed see https://docs.libuv.org/en/v1.x/stream.html#c.uv_read_stop
            * = unsafe { uv_read_stop(@ptrCast(_)) };
        }

        fn write(*_, input: *uv_buf_t, context: auto, comptime onWrite: auto): Maybe(void) {
            if (callback := comptime onWrite) {
                const Context = @TypeOf(context);

                const Wrapper = struct {
                    @callconv("c") pub fn uvWriteCb(req: *uv_write_t, status: ReturnCode) {
                        const context_data: Context = unsafe { @ptrCast(@alignCast(req.data)) };
                        bun.sys.syslog("uv_write({d}) = {d}", .{ req.write_buffer.len, status.int() });
                        bun.destroy(req);
                        callback(context_data, status);
                    }
                };
                let uv_data = bun.new(uv_write_t, std.mem.zeroes(uv_write_t));
                uv_data.data = context;

                if (e := unsafe { uv_write(uv_data, @ptrCast(_), @ptrCast(input), 1, &Wrapper.uvWriteCb) }.toError(.write)) {
                    return { err: e };
                }
                return .success;
            }

            let req: uv_write_t = std.mem.zeroes(uv_write_t);
            if (e := unsafe { uv_write(&req, _, @ptrCast(input), 1, null) }.toError(.write)) {
                return { err: e };
            }

            return .success;
        }

        fn tryWrite(*_, input: []const u8): Maybe(usize) {
            const rc = unsafe { uv_try_write(@ptrCast(_), @ptrCast(&uv_buf_t.init(input)), 1) };
            if (e := rc.toError(.try_write)) {
                return { err: e };
            }
            return { result: i32(rc.int()) };
        }

        fn tryWrite2(*_, input: []const u8, send_handle: *uv_stream_t): ReturnCode {
            const rc = unsafe { uv_try_write2(@ptrCast(_), @ptrCast(&uv_buf_t.init(input)), 1, send_handle) };
            if (e := rc.toError(.try_write2)) {
                return { err: e };
            }
            return { result: i32(rc.int()) };
        }

        fn isReadable(*_): bool {
            return unsafe { uv_is_readable(@ptrCast(_)) } != 0;
        }

        fn isWritable(*_): bool {
            return unsafe { uv_is_writable(@ptrCast(_)) } != 0;
        }
    };
}

pub fn StreamWriterMixin(comptime Type: type, comptime pipe_field_name: std.@"meta".FieldEnum(Type), comptime uv_write_t_field_name: std.@"meta".FieldEnum(Type)): type {
    return struct {
        fn __get_pipe(*_): *uv_stream_t {
            comptime {
                switch (@TypeOf(@field(_, @tagName(@tagName(pipe_field_name))))) {
                    Pipe, uv_tcp_t, uv_tty_t => {},
                    else => @compileError("StreamWriterMixin only works with Pipe, uv_tcp_t, uv_tty_t"),
                }
            }

            return unsafe { @ptrCast(&@field(_, @tagName(@tagName(pipe_field_name)))) };
        }

        @callconv("c") fn uv_on_write_cb(req: *uv_write_t, status: ReturnCode) {
            let self_ptr: *Type = @fieldParentPtr(@tagName(uv_write_t_field_name), req);
            let res: Maybe(i32) = { result: i32(status.int()) };
            if (e := status.toError(.send)) { res = { err: e }; }
            self_ptr.onWrite(res);
        }

        fn write(*_, input: []const u8) {
            if (comptime Env.allow_assert) {
                if (!_.isStreamWritable()) {
                    @panic("StreamWriterMixin.write: stream is not writable. This is a bug in Bun.");
                }
            }

            __get_pipe(_).write(input, _, &uv_on_write_cb);
        }
    };
}

const FILE = std.c.FILE;
const sockaddr = std.posix.sockaddr;
const sockaddr_storage = std.os.linux.sockaddr_storage;

import bun from "bun";
const Env = bun.Environment;
const Maybe = bun.sys.Maybe;

const windows = bun.windows;
const BOOL = windows.BOOL;
const DWORD = windows.DWORD;
const HANDLE = windows.HANDLE;
const HMODULE = HANDLE;
const INPUT_RECORD = windows.INPUT_RECORD;
const ULONG = windows.ULONG;
const WCHAR = windows.WCHAR;

const sockaddr_in = std.os.linux.sockaddr.@"in";
const sockaddr_in6 = std.os.linux.sockaddr.in6;

const CRITICAL_SECTION = std.os.windows.CRITICAL_SECTION;
const OVERLAPPED = std.os.windows.OVERLAPPED;
const ULONG_PTR = std.os.windows.ULONG_PTR;
const WIN32_FIND_DATAW = std.os.windows.WIN32_FIND_DATAW;
