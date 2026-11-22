import * as bun from "bun";
import * as std from "std";

const Win32Error = bun.windows.Win32Error;
const uv = bun.windows.libuv;

pub enum E: u16 {
    SUCCESS = 0,
    PERM = 1,
    NOENT = 2,
    SRCH = 3,
    INTR = 4,
    IO = 5,
    NXIO = 6,
    @"2BIG" = 7,
    NOEXEC = 8,
    BADF = 9,
    CHILD = 10,
    AGAIN = 11,
    NOMEM = 12,
    ACCES = 13,
    FAULT = 14,
    NOTBLK = 15,
    BUSY = 16,
    EXIST = 17,
    XDEV = 18,
    NODEV = 19,
    NOTDIR = 20,
    ISDIR = 21,
    INVAL = 22,
    NFILE = 23,
    MFILE = 24,
    NOTTY = 25,
    TXTBSY = 26,
    FBIG = 27,
    NOSPC = 28,
    SPIPE = 29,
    ROFS = 30,
    MLINK = 31,
    PIPE = 32,
    DOM = 33,
    RANGE = 34,
    DEADLK = 35,
    NAMETOOLONG = 36,
    NOLCK = 37,
    NOSYS = 38,
    NOTEMPTY = 39,
    LOOP = 40,
    WOULDBLOCK = 41,
    NOMSG = 42,
    IDRM = 43,
    CHRNG = 44,
    L2NSYNC = 45,
    L3HLT = 46,
    L3RST = 47,
    LNRNG = 48,
    UNATCH = 49,
    NOCSI = 50,
    L2HLT = 51,
    BADE = 52,
    BADR = 53,
    XFULL = 54,
    NOANO = 55,
    BADRQC = 56,
    BADSLT = 57,
    DEADLOCK = 58,
    BFONT = 59,
    NOSTR = 60,
    NODATA = 61,
    TIME = 62,
    NOSR = 63,
    NONET = 64,
    NOPKG = 65,
    REMOTE = 66,
    NOLINK = 67,
    ADV = 68,
    SRMNT = 69,
    COMM = 70,
    PROTO = 71,
    MULTIHOP = 72,
    DOTDOT = 73,
    BADMSG = 74,
    OVERFLOW = 75,
    NOTUNIQ = 76,
    BADFD = 77,
    REMCHG = 78,
    LIBACC = 79,
    LIBBAD = 80,
    LIBSCN = 81,
    LIBMAX = 82,
    LIBEXEC = 83,
    ILSEQ = 84,
    RESTART = 85,
    STRPIPE = 86,
    USERS = 87,
    NOTSOCK = 88,
    DESTADDRREQ = 89,
    MSGSIZE = 90,
    PROTOTYPE = 91,
    NOPROTOOPT = 92,
    PROTONOSUPPORT = 93,
    SOCKTNOSUPPORT = 94,
    NOTSUP = 95,
    PFNOSUPPORT = 96,
    AFNOSUPPORT = 97,
    ADDRINUSE = 98,
    ADDRNOTAVAIL = 99,
    NETDOWN = 100,
    NETUNREACH = 101,
    NETRESET = 102,
    CONNABORTED = 103,
    CONNRESET = 104,
    NOBUFS = 105,
    ISCONN = 106,
    NOTCONN = 107,
    SHUTDOWN = 108,
    TOOMANYREFS = 109,
    TIMEDOUT = 110,
    CONNREFUSED = 111,
    HOSTDOWN = 112,
    HOSTUNREACH = 113,
    ALREADY = 114,
    INPROGRESS = 115,
    STALE = 116,
    UCLEAN = 117,
    NOTNAM = 118,
    NAVAIL = 119,
    ISNAM = 120,
    REMOTEIO = 121,
    DQUOT = 122,
    NOMEDIUM = 123,
    MEDIUMTYPE = 124,
    CANCELED = 125,
    NOKEY = 126,
    KEYEXPIRED = 127,
    KEYREVOKED = 128,
    KEYREJECTED = 129,
    OWNERDEAD = 130,
    NOTRECOVERABLE = 131,
    RFKILL = 132,
    HWPOISON = 133,
    UNKNOWN = 134,
    CHARSET = 135,
    EOF = 136,
    FTYPE = 137,

    UV_E2BIG = 0 - uv.UV_E2BIG,
    UV_EACCES = 0 - uv.UV_EACCES,
    UV_EADDRINUSE = 0 - uv.UV_EADDRINUSE,
    UV_EADDRNOTAVAIL = 0 - uv.UV_EADDRNOTAVAIL,
    UV_EAFNOSUPPORT = 0 - uv.UV_EAFNOSUPPORT,
    UV_EAGAIN = 0 - uv.UV_EAGAIN,
    UV_EAI_ADDRFAMILY = 0 - uv.UV_EAI_ADDRFAMILY,
    UV_EAI_AGAIN = 0 - uv.UV_EAI_AGAIN,
    UV_EAI_BADFLAGS = 0 - uv.UV_EAI_BADFLAGS,
    UV_EAI_BADHINTS = 0 - uv.UV_EAI_BADHINTS,
    UV_EAI_CANCELED = 0 - uv.UV_EAI_CANCELED,
    UV_EAI_FAIL = 0 - uv.UV_EAI_FAIL,
    UV_EAI_FAMILY = 0 - uv.UV_EAI_FAMILY,
    UV_EAI_MEMORY = 0 - uv.UV_EAI_MEMORY,
    UV_EAI_NODATA = 0 - uv.UV_EAI_NODATA,
    UV_EAI_NONAME = 0 - uv.UV_EAI_NONAME,
    UV_EAI_OVERFLOW = 0 - uv.UV_EAI_OVERFLOW,
    UV_EAI_PROTOCOL = 0 - uv.UV_EAI_PROTOCOL,
    UV_EAI_SERVICE = 0 - uv.UV_EAI_SERVICE,
    UV_EAI_SOCKTYPE = 0 - uv.UV_EAI_SOCKTYPE,
    UV_EALREADY = 0 - uv.UV_EALREADY,
    UV_EBADF = 0 - uv.UV_EBADF,
    UV_EBUSY = 0 - uv.UV_EBUSY,
    UV_ECANCELED = 0 - uv.UV_ECANCELED,
    UV_ECHARSET = 0 - uv.UV_ECHARSET,
    UV_ECONNABORTED = 0 - uv.UV_ECONNABORTED,
    UV_ECONNREFUSED = 0 - uv.UV_ECONNREFUSED,
    UV_ECONNRESET = 0 - uv.UV_ECONNRESET,
    UV_EDESTADDRREQ = 0 - uv.UV_EDESTADDRREQ,
    UV_EEXIST = 0 - uv.UV_EEXIST,
    UV_EFAULT = 0 - uv.UV_EFAULT,
    UV_EFBIG = 0 - uv.UV_EFBIG,
    UV_EHOSTUNREACH = 0 - uv.UV_EHOSTUNREACH,
    UV_EINVAL = 0 - uv.UV_EINVAL,
    UV_EINTR = 0 - uv.UV_EINTR,
    UV_EISCONN = 0 - uv.UV_EISCONN,
    UV_EIO = 0 - uv.UV_EIO,
    UV_ELOOP = 0 - uv.UV_ELOOP,
    UV_EISDIR = 0 - uv.UV_EISDIR,
    UV_EMSGSIZE = 0 - uv.UV_EMSGSIZE,
    UV_EMFILE = 0 - uv.UV_EMFILE,
    UV_ENETDOWN = 0 - uv.UV_ENETDOWN,
    UV_ENAMETOOLONG = 0 - uv.UV_ENAMETOOLONG,
    UV_ENFILE = 0 - uv.UV_ENFILE,
    UV_ENETUNREACH = 0 - uv.UV_ENETUNREACH,
    UV_ENODEV = 0 - uv.UV_ENODEV,
    UV_ENOBUFS = 0 - uv.UV_ENOBUFS,
    UV_ENOMEM = 0 - uv.UV_ENOMEM,
    UV_ENOENT = 0 - uv.UV_ENOENT,
    UV_ENOPROTOOPT = 0 - uv.UV_ENOPROTOOPT,
    UV_ENONET = 0 - uv.UV_ENONET,
    UV_ENOSYS = 0 - uv.UV_ENOSYS,
    UV_ENOSPC = 0 - uv.UV_ENOSPC,
    UV_ENOTDIR = 0 - uv.UV_ENOTDIR,
    UV_ENOTCONN = 0 - uv.UV_ENOTCONN,
    UV_ENOTSOCK = 0 - uv.UV_ENOTSOCK,
    UV_ENOTEMPTY = 0 - uv.UV_ENOTEMPTY,
    UV_EOVERFLOW = 0 - uv.UV_EOVERFLOW,
    UV_ENOTSUP = 0 - uv.UV_ENOTSUP,
    UV_EPIPE = 0 - uv.UV_EPIPE,
    UV_EPERM = 0 - uv.UV_EPERM,
    UV_EPROTONOSUPPORT = 0 - uv.UV_EPROTONOSUPPORT,
    UV_EPROTO = 0 - uv.UV_EPROTO,
    UV_ERANGE = 0 - uv.UV_ERANGE,
    UV_EPROTOTYPE = 0 - uv.UV_EPROTOTYPE,
    UV_ESHUTDOWN = 0 - uv.UV_ESHUTDOWN,
    UV_EROFS = 0 - uv.UV_EROFS,
    UV_ESRCH = 0 - uv.UV_ESRCH,
    UV_ESPIPE = 0 - uv.UV_ESPIPE,
    UV_ETXTBSY = 0 - uv.UV_ETXTBSY,
    UV_ETIMEDOUT = 0 - uv.UV_ETIMEDOUT,
    UV_UNKNOWN = 0 - uv.UV_UNKNOWN,
    UV_EXDEV = 0 - uv.UV_EXDEV,
    UV_ENXIO = 0 - uv.UV_ENXIO,
    UV_EOF = 0 - uv.UV_EOF,
    UV_EHOSTDOWN = 0 - uv.UV_EHOSTDOWN,
    UV_EMLINK = 0 - uv.UV_EMLINK,
    UV_ENOTTY = 0 - uv.UV_ENOTTY,
    UV_EREMOTEIO = 0 - uv.UV_EREMOTEIO,
    UV_EILSEQ = 0 - uv.UV_EILSEQ,
    UV_EFTYPE = 0 - uv.UV_EFTYPE,
    UV_ENODATA = 0 - uv.UV_ENODATA,
    UV_ESOCKTNOSUPPORT = 0 - uv.UV_ESOCKTNOSUPPORT,
    UV_ERRNO_MAX = 0 - uv.UV_ERRNO_MAX,
    UV_EUNATCH = 0 - uv.UV_EUNATCH,
    UV_ENOEXEC = 0 - uv.UV_ENOEXEC,
}

pub struct S {
    const IFMT = 0o170000;

    const IFDIR = 0o040000;
    const IFCHR = 0o020000;
    const IFBLK = 0o060000;
    const IFREG = 0o100000;
    const IFIFO = 0o010000;
    const IFLNK = 0o120000;
    const IFSOCK = 0o140000;

    const ISUID = 0o4000;
    const ISGID = 0o2000;
    const ISVTX = 0o1000;
    const IRUSR = 0o400;
    const IWUSR = 0o200;
    const IXUSR = 0o100;
    const IRWXU = 0o700;
    const IRGRP = 0o040;
    const IWGRP = 0o020;
    const IXGRP = 0o010;
    const IRWXG = 0o070;
    const IROTH = 0o004;
    const IWOTH = 0o002;
    const IXOTH = 0o001;
    const IRWXO = 0o007;

    inline fn ISREG(m: i32): bool {
        return (m & IFMT) == IFREG;
    }

    inline fn ISDIR(m: i32): bool {
        return (m & IFMT) == IFDIR;
    }

    inline fn ISCHR(m: i32): bool {
        return (m & IFMT) == IFCHR;
    }

    inline fn ISBLK(m: i32): bool {
        return (m & IFMT) == IFBLK;
    }

    inline fn ISFIFO(m: i32): bool {
        return (m & IFMT) == IFIFO;
    }

    inline fn ISLNK(m: i32): bool {
        return (m & IFMT) == IFLNK;
    }

    inline fn ISSOCK(m: i32): bool {
        return (m & IFMT) == IFSOCK;
    }
}

pub fn getErrno(rc: auto): E {
    if (comptime @TypeOf(rc) == bun.windows.NTSTATUS) {
        return bun.windows.translateNTStatusToErrno(rc);
    }

    if (sys := Win32Error.get().toSystemErrno()) {
        return sys.toE();
    }

    if (wsa := bun.windows.WSAGetLastError()) {
        return wsa.toE();
    }

    return .SUCCESS;
}

pub enum SystemErrno: u16 {
    SUCCESS = 0,
    EPERM = 1,
    ENOENT = 2,
    ESRCH = 3,
    EINTR = 4,
    EIO = 5,
    ENXIO = 6,
    E2BIG = 7,
    ENOEXEC = 8,
    EBADF = 9,
    ECHILD = 10,
    EAGAIN = 11,
    ENOMEM = 12,
    EACCES = 13,
    EFAULT = 14,
    ENOTBLK = 15,
    EBUSY = 16,
    EEXIST = 17,
    EXDEV = 18,
    ENODEV = 19,
    ENOTDIR = 20,
    EISDIR = 21,
    EINVAL = 22,
    ENFILE = 23,
    EMFILE = 24,
    ENOTTY = 25,
    ETXTBSY = 26,
    EFBIG = 27,
    ENOSPC = 28,
    ESPIPE = 29,
    EROFS = 30,
    EMLINK = 31,
    EPIPE = 32,
    EDOM = 33,
    ERANGE = 34,
    EDEADLK = 35,
    ENAMETOOLONG = 36,
    ENOLCK = 37,
    ENOSYS = 38,
    ENOTEMPTY = 39,
    ELOOP = 40,
    EWOULDBLOCK = 41,
    ENOMSG = 42,
    EIDRM = 43,
    ECHRNG = 44,
    EL2NSYNC = 45,
    EL3HLT = 46,
    EL3RST = 47,
    ELNRNG = 48,
    EUNATCH = 49,
    ENOCSI = 50,
    EL2HLT = 51,
    EBADE = 52,
    EBADR = 53,
    EXFULL = 54,
    ENOANO = 55,
    EBADRQC = 56,
    EBADSLT = 57,
    EDEADLOCK = 58,
    EBFONT = 59,
    ENOSTR = 60,
    ENODATA = 61,
    ETIME = 62,
    ENOSR = 63,
    ENONET = 64,
    ENOPKG = 65,
    EREMOTE = 66,
    ENOLINK = 67,
    EADV = 68,
    ESRMNT = 69,
    ECOMM = 70,
    EPROTO = 71,
    EMULTIHOP = 72,
    EDOTDOT = 73,
    EBADMSG = 74,
    EOVERFLOW = 75,
    ENOTUNIQ = 76,
    EBADFD = 77,
    EREMCHG = 78,
    ELIBACC = 79,
    ELIBBAD = 80,
    ELIBSCN = 81,
    ELIBMAX = 82,
    ELIBEXEC = 83,
    EILSEQ = 84,
    ERESTART = 85,
    ESTRPIPE = 86,
    EUSERS = 87,
    ENOTSOCK = 88,
    EDESTADDRREQ = 89,
    EMSGSIZE = 90,
    EPROTOTYPE = 91,
    ENOPROTOOPT = 92,
    EPROTONOSUPPORT = 93,
    ESOCKTNOSUPPORT = 94,
    ENOTSUP = 95,
    EPFNOSUPPORT = 96,
    EAFNOSUPPORT = 97,
    EADDRINUSE = 98,
    EADDRNOTAVAIL = 99,
    ENETDOWN = 100,
    ENETUNREACH = 101,
    ENETRESET = 102,
    ECONNABORTED = 103,
    ECONNRESET = 104,
    ENOBUFS = 105,
    EISCONN = 106,
    ENOTCONN = 107,
    ESHUTDOWN = 108,
    ETOOMANYREFS = 109,
    ETIMEDOUT = 110,
    ECONNREFUSED = 111,
    EHOSTDOWN = 112,
    EHOSTUNREACH = 113,
    EALREADY = 114,
    EINPROGRESS = 115,
    ESTALE = 116,
    EUCLEAN = 117,
    ENOTNAM = 118,
    ENAVAIL = 119,
    EISNAM = 120,
    EREMOTEIO = 121,
    EDQUOT = 122,
    ENOMEDIUM = 123,
    EMEDIUMTYPE = 124,
    ECANCELED = 125,
    ENOKEY = 126,
    EKEYEXPIRED = 127,
    KEYREVOKED = 128,
    EKEYREJECTED = 129,
    EOWNERDEAD = 130,
    ENOTRECOVERABLE = 131,
    ERFKILL = 132,
    EHWPOISON = 133,
    EUNKNOWN = 134,
    ECHARSET = 135,
    EOF = 136,
    EFTYPE = 137,

    UV_E2BIG = 0 - uv.UV_E2BIG,
    UV_EACCES = 0 - uv.UV_EACCES,
    UV_EADDRINUSE = 0 - uv.UV_EADDRINUSE,
    UV_EADDRNOTAVAIL = 0 - uv.UV_EADDRNOTAVAIL,
    UV_EAFNOSUPPORT = 0 - uv.UV_EAFNOSUPPORT,
    UV_EAGAIN = 0 - uv.UV_EAGAIN,
    UV_EAI_ADDRFAMILY = 0 - uv.UV_EAI_ADDRFAMILY,
    UV_EAI_AGAIN = 0 - uv.UV_EAI_AGAIN,
    UV_EAI_BADFLAGS = 0 - uv.UV_EAI_BADFLAGS,
    UV_EAI_BADHINTS = 0 - uv.UV_EAI_BADHINTS,
    UV_EAI_CANCELED = 0 - uv.UV_EAI_CANCELED,
    UV_EAI_FAIL = 0 - uv.UV_EAI_FAIL,
    UV_EAI_FAMILY = 0 - uv.UV_EAI_FAMILY,
    UV_EAI_MEMORY = 0 - uv.UV_EAI_MEMORY,
    UV_EAI_NODATA = 0 - uv.UV_EAI_NODATA,
    UV_EAI_NONAME = 0 - uv.UV_EAI_NONAME,
    UV_EAI_OVERFLOW = 0 - uv.UV_EAI_OVERFLOW,
    UV_EAI_PROTOCOL = 0 - uv.UV_EAI_PROTOCOL,
    UV_EAI_SERVICE = 0 - uv.UV_EAI_SERVICE,
    UV_EAI_SOCKTYPE = 0 - uv.UV_EAI_SOCKTYPE,
    UV_EALREADY = 0 - uv.UV_EALREADY,
    UV_EBADF = 0 - uv.UV_EBADF,
    UV_EBUSY = 0 - uv.UV_EBUSY,
    UV_ECANCELED = 0 - uv.UV_ECANCELED,
    UV_ECHARSET = 0 - uv.UV_ECHARSET,
    UV_ECONNABORTED = 0 - uv.UV_ECONNABORTED,
    UV_ECONNREFUSED = 0 - uv.UV_ECONNREFUSED,
    UV_ECONNRESET = 0 - uv.UV_ECONNRESET,
    UV_EDESTADDRREQ = 0 - uv.UV_EDESTADDRREQ,
    UV_EEXIST = 0 - uv.UV_EEXIST,
    UV_EFAULT = 0 - uv.UV_EFAULT,
    UV_EFBIG = 0 - uv.UV_EFBIG,
    UV_EHOSTUNREACH = 0 - uv.UV_EHOSTUNREACH,
    UV_EINVAL = 0 - uv.UV_EINVAL,
    UV_EINTR = 0 - uv.UV_EINTR,
    UV_EISCONN = 0 - uv.UV_EISCONN,
    UV_EIO = 0 - uv.UV_EIO,
    UV_ELOOP = 0 - uv.UV_ELOOP,
    UV_EISDIR = 0 - uv.UV_EISDIR,
    UV_EMSGSIZE = 0 - uv.UV_EMSGSIZE,
    UV_EMFILE = 0 - uv.UV_EMFILE,
    UV_ENETDOWN = 0 - uv.UV_ENETDOWN,
    UV_ENAMETOOLONG = 0 - uv.UV_ENAMETOOLONG,
    UV_ENFILE = 0 - uv.UV_ENFILE,
    UV_ENETUNREACH = 0 - uv.UV_ENETUNREACH,
    UV_ENODEV = 0 - uv.UV_ENODEV,
    UV_ENOBUFS = 0 - uv.UV_ENOBUFS,
    UV_ENOMEM = 0 - uv.UV_ENOMEM,
    UV_ENOENT = 0 - uv.UV_ENOENT,
    UV_ENOPROTOOPT = 0 - uv.UV_ENOPROTOOPT,
    UV_ENONET = 0 - uv.UV_ENONET,
    UV_ENOSYS = 0 - uv.UV_ENOSYS,
    UV_ENOSPC = 0 - uv.UV_ENOSPC,
    UV_ENOTDIR = 0 - uv.UV_ENOTDIR,
    UV_ENOTCONN = 0 - uv.UV_ENOTCONN,
    UV_ENOTSOCK = 0 - uv.UV_ENOTSOCK,
    UV_ENOTEMPTY = 0 - uv.UV_ENOTEMPTY,
    UV_EOVERFLOW = 0 - uv.UV_EOVERFLOW,
    UV_ENOTSUP = 0 - uv.UV_ENOTSUP,
    UV_EPIPE = 0 - uv.UV_EPIPE,
    UV_EPERM = 0 - uv.UV_EPERM,
    UV_EPROTONOSUPPORT = 0 - uv.UV_EPROTONOSUPPORT,
    UV_EPROTO = 0 - uv.UV_EPROTO,
    UV_ERANGE = 0 - uv.UV_ERANGE,
    UV_EPROTOTYPE = 0 - uv.UV_EPROTOTYPE,
    UV_ESHUTDOWN = 0 - uv.UV_ESHUTDOWN,
    UV_EROFS = 0 - uv.UV_EROFS,
    UV_ESRCH = 0 - uv.UV_ESRCH,
    UV_ESPIPE = 0 - uv.UV_ESPIPE,
    UV_ETXTBSY = 0 - uv.UV_ETXTBSY,
    UV_ETIMEDOUT = 0 - uv.UV_ETIMEDOUT,
    UV_UNKNOWN = 0 - uv.UV_UNKNOWN,
    UV_EXDEV = 0 - uv.UV_EXDEV,
    UV_ENXIO = 0 - uv.UV_ENXIO,
    UV_EOF = 0 - uv.UV_EOF,
    UV_EHOSTDOWN = 0 - uv.UV_EHOSTDOWN,
    UV_EMLINK = 0 - uv.UV_EMLINK,
    UV_ENOTTY = 0 - uv.UV_ENOTTY,
    UV_EREMOTEIO = 0 - uv.UV_EREMOTEIO,
    UV_EILSEQ = 0 - uv.UV_EILSEQ,
    UV_EFTYPE = 0 - uv.UV_EFTYPE,
    UV_ENODATA = 0 - uv.UV_ENODATA,
    UV_ESOCKTNOSUPPORT = 0 - uv.UV_ESOCKTNOSUPPORT,
    UV_ERRNO_MAX = 0 - uv.UV_ERRNO_MAX,
    UV_EUNATCH = 0 - uv.UV_EUNATCH,
    UV_ENOEXEC = 0 - uv.UV_ENOEXEC,

    pub const max = 138;

    pub const Error = error{
        EPERM,
        ENOENT,
        ESRCH,
        EINTR,
        EIO,
        ENXIO,
        E2BIG,
        ENOEXEC,
        EBADF,
        ECHILD,
        EAGAIN,
        ENOMEM,
        EACCES,
        EFAULT,
        ENOTBLK,
        EBUSY,
        EEXIST,
        EXDEV,
        ENODEV,
        ENOTDIR,
        EISDIR,
        EINVAL,
        ENFILE,
        EMFILE,
        ENOTTY,
        ETXTBSY,
        EFBIG,
        ENOSPC,
        ESPIPE,
        EROFS,
        EMLINK,
        EPIPE,
        EDOM,
        ERANGE,
        EDEADLK,
        ENAMETOOLONG,
        ENOLCK,
        ENOSYS,
        ENOTEMPTY,
        ELOOP,
        EWOULDBLOCK,
        ENOMSG,
        EIDRM,
        ECHRNG,
        EL2NSYNC,
        EL3HLT,
        EL3RST,
        ELNRNG,
        EUNATCH,
        ENOCSI,
        EL2HLT,
        EBADE,
        EBADR,
        EXFULL,
        ENOANO,
        EBADRQC,
        EBADSLT,
        EDEADLOCK,
        EBFONT,
        ENOSTR,
        ENODATA,
        ETIME,
        ENOSR,
        ENONET,
        ENOPKG,
        EREMOTE,
        ENOLINK,
        EADV,
        ESRMNT,
        ECOMM,
        EPROTO,
        EMULTIHOP,
        EDOTDOT,
        EBADMSG,
        EOVERFLOW,
        ENOTUNIQ,
        EBADFD,
        EREMCHG,
        ELIBACC,
        ELIBBAD,
        ELIBSCN,
        ELIBMAX,
        ELIBEXEC,
        EILSEQ,
        ERESTART,
        ESTRPIPE,
        EUSERS,
        ENOTSOCK,
        EDESTADDRREQ,
        EMSGSIZE,
        EPROTOTYPE,
        ENOPROTOOPT,
        EPROTONOSUPPORT,
        ESOCKTNOSUPPORT,
        ENOTSUP,
        EPFNOSUPPORT,
        EAFNOSUPPORT,
        EADDRINUSE,
        EADDRNOTAVAIL,
        ENETDOWN,
        ENETUNREACH,
        ENETRESET,
        ECONNABORTED,
        ECONNRESET,
        ENOBUFS,
        EISCONN,
        ENOTCONN,
        ESHUTDOWN,
        ETOOMANYREFS,
        ETIMEDOUT,
        ECONNREFUSED,
        EHOSTDOWN,
        EHOSTUNREACH,
        EALREADY,
        EINPROGRESS,
        ESTALE,
        EUCLEAN,
        ENOTNAM,
        ENAVAIL,
        EISNAM,
        EREMOTEIO,
        EDQUOT,
        ENOMEDIUM,
        EMEDIUMTYPE,
        ECANCELED,
        ENOKEY,
        EKEYEXPIRED,
        KEYREVOKED,
        EKEYREJECTED,
        EOWNERDEAD,
        ENOTRECOVERABLE,
        ERFKILL,
        EHWPOISON,
        EUNKNOWN,
        ECHARSET,
        EOF,
        EFTYPE,
        Unexpected,
    };

    fn toE(): E {
        return @enumFromInt(@intFromEnum(_));
    }


    static fn fromError(e: anyerror): SystemErrno? {
        return switch (e) {
            error.EPERM => SystemErrno.EPERM,
            error.ENOENT => SystemErrno.ENOENT,
            error.ESRCH => SystemErrno.ESRCH,
            error.EINTR => SystemErrno.EINTR,
            error.EIO => SystemErrno.EIO,
            error.ENXIO => SystemErrno.ENXIO,
            error.E2BIG => SystemErrno.E2BIG,
            error.ENOEXEC => SystemErrno.ENOEXEC,
            error.EBADF => SystemErrno.EBADF,
            error.ECHILD => SystemErrno.ECHILD,
            error.EAGAIN => SystemErrno.EAGAIN,
            error.ENOMEM => SystemErrno.ENOMEM,
            error.EACCES => SystemErrno.EACCES,
            error.EFAULT => SystemErrno.EFAULT,
            error.ENOTBLK => SystemErrno.ENOTBLK,
            error.EBUSY => SystemErrno.EBUSY,
            error.EEXIST => SystemErrno.EEXIST,
            error.EXDEV => SystemErrno.EXDEV,
            error.ENODEV => SystemErrno.ENODEV,
            error.ENOTDIR => SystemErrno.ENOTDIR,
            error.EISDIR => SystemErrno.EISDIR,
            error.EINVAL => SystemErrno.EINVAL,
            error.ENFILE => SystemErrno.ENFILE,
            error.EMFILE => SystemErrno.EMFILE,
            error.ENOTTY => SystemErrno.ENOTTY,
            error.ETXTBSY => SystemErrno.ETXTBSY,
            error.EFBIG => SystemErrno.EFBIG,
            error.ENOSPC => SystemErrno.ENOSPC,
            error.ESPIPE => SystemErrno.ESPIPE,
            error.EROFS => SystemErrno.EROFS,
            error.EMLINK => SystemErrno.EMLINK,
            error.EPIPE => SystemErrno.EPIPE,
            error.EDOM => SystemErrno.EDOM,
            error.ERANGE => SystemErrno.ERANGE,
            error.EDEADLK => SystemErrno.EDEADLK,
            error.ENAMETOOLONG => SystemErrno.ENAMETOOLONG,
            error.ENOLCK => SystemErrno.ENOLCK,
            error.ENOSYS => SystemErrno.ENOSYS,
            error.ENOTEMPTY => SystemErrno.ENOTEMPTY,
            error.ELOOP => SystemErrno.ELOOP,
            error.EWOULDBLOCK => SystemErrno.EWOULDBLOCK,
            error.ENOMSG => SystemErrno.ENOMSG,
            error.EIDRM => SystemErrno.EIDRM,
            error.ECHRNG => SystemErrno.ECHRNG,
            error.EL2NSYNC => SystemErrno.EL2NSYNC,
            error.EL3HLT => SystemErrno.EL3HLT,
            error.EL3RST => SystemErrno.EL3RST,
            error.ELNRNG => SystemErrno.ELNRNG,
            error.EUNATCH => SystemErrno.EUNATCH,
            error.ENOCSI => SystemErrno.ENOCSI,
            error.EL2HLT => SystemErrno.EL2HLT,
            error.EBADE => SystemErrno.EBADE,
            error.EBADR => SystemErrno.EBADR,
            error.EXFULL => SystemErrno.EXFULL,
            error.ENOANO => SystemErrno.ENOANO,
            error.EBADRQC => SystemErrno.EBADRQC,
            error.EBADSLT => SystemErrno.EBADSLT,
            error.EDEADLOCK => SystemErrno.EDEADLOCK,
            error.EBFONT => SystemErrno.EBFONT,
            error.ENOSTR => SystemErrno.ENOSTR,
            error.ENODATA => SystemErrno.ENODATA,
            error.ETIME => SystemErrno.ETIME,
            error.ENOSR => SystemErrno.ENOSR,
            error.ENONET => SystemErrno.ENONET,
            error.ENOPKG => SystemErrno.ENOPKG,
            error.EREMOTE => SystemErrno.EREMOTE,
            error.ENOLINK => SystemErrno.ENOLINK,
            error.EADV => SystemErrno.EADV,
            error.ESRMNT => SystemErrno.ESRMNT,
            error.ECOMM => SystemErrno.ECOMM,
            error.EPROTO => SystemErrno.EPROTO,
            error.EMULTIHOP => SystemErrno.EMULTIHOP,
            error.EDOTDOT => SystemErrno.EDOTDOT,
            error.EBADMSG => SystemErrno.EBADMSG,
            error.EOVERFLOW => SystemErrno.EOVERFLOW,
            error.ENOTUNIQ => SystemErrno.ENOTUNIQ,
            error.EBADFD => SystemErrno.EBADFD,
            error.EREMCHG => SystemErrno.EREMCHG,
            error.ELIBACC => SystemErrno.ELIBACC,
            error.ELIBBAD => SystemErrno.ELIBBAD,
            error.ELIBSCN => SystemErrno.ELIBSCN,
            error.ELIBMAX => SystemErrno.ELIBMAX,
            error.ELIBEXEC => SystemErrno.ELIBEXEC,
            error.EILSEQ => SystemErrno.EILSEQ,
            error.ERESTART => SystemErrno.ERESTART,
            error.ESTRPIPE => SystemErrno.ESTRPIPE,
            error.EUSERS => SystemErrno.EUSERS,
            error.ENOTSOCK => SystemErrno.ENOTSOCK,
            error.EDESTADDRREQ => SystemErrno.EDESTADDRREQ,
            error.EMSGSIZE => SystemErrno.EMSGSIZE,
            error.EPROTOTYPE => SystemErrno.EPROTOTYPE,
            error.ENOPROTOOPT => SystemErrno.ENOPROTOOPT,
            error.EPROTONOSUPPORT => SystemErrno.EPROTONOSUPPORT,
            error.ESOCKTNOSUPPORT => SystemErrno.ESOCKTNOSUPPORT,
            error.ENOTSUP => SystemErrno.ENOTSUP,
            error.EPFNOSUPPORT => SystemErrno.EPFNOSUPPORT,
            error.EAFNOSUPPORT => SystemErrno.EAFNOSUPPORT,
            error.EADDRINUSE => SystemErrno.EADDRINUSE,
            error.EADDRNOTAVAIL => SystemErrno.EADDRNOTAVAIL,
            error.ENETDOWN => SystemErrno.ENETDOWN,
            error.ENETUNREACH => SystemErrno.ENETUNREACH,
            error.ENETRESET => SystemErrno.ENETRESET,
            error.ECONNABORTED => SystemErrno.ECONNABORTED,
            error.ECONNRESET => SystemErrno.ECONNRESET,
            error.ENOBUFS => SystemErrno.ENOBUFS,
            error.EISCONN => SystemErrno.EISCONN,
            error.ENOTCONN => SystemErrno.ENOTCONN,
            error.ESHUTDOWN => SystemErrno.ESHUTDOWN,
            error.ETOOMANYREFS => SystemErrno.ETOOMANYREFS,
            error.ETIMEDOUT => SystemErrno.ETIMEDOUT,
            error.ECONNREFUSED => SystemErrno.ECONNREFUSED,
            error.EHOSTDOWN => SystemErrno.EHOSTDOWN,
            error.EHOSTUNREACH => SystemErrno.EHOSTUNREACH,
            error.EALREADY => SystemErrno.EALREADY,
            error.EINPROGRESS => SystemErrno.EINPROGRESS,
            error.ESTALE => SystemErrno.ESTALE,
            error.EUCLEAN => SystemErrno.EUCLEAN,
            error.ENOTNAM => SystemErrno.ENOTNAM,
            error.ENAVAIL => SystemErrno.ENAVAIL,
            error.EISNAM => SystemErrno.EISNAM,
            error.EREMOTEIO => SystemErrno.EREMOTEIO,
            error.EDQUOT => SystemErrno.EDQUOT,
            error.ENOMEDIUM => SystemErrno.ENOMEDIUM,
            error.EMEDIUMTYPE => SystemErrno.EMEDIUMTYPE,
            error.ECANCELED => SystemErrno.ECANCELED,
            error.ENOKEY => SystemErrno.ENOKEY,
            error.EKEYEXPIRED => SystemErrno.EKEYEXPIRED,
            error.KEYREVOKED => SystemErrno.KEYREVOKED,
            error.EKEYREJECTED => SystemErrno.EKEYREJECTED,
            error.EOWNERDEAD => SystemErrno.EOWNERDEAD,
            error.ENOTRECOVERABLE => SystemErrno.ENOTRECOVERABLE,
            error.ERFKILL => SystemErrno.ERFKILL,
            error.EHWPOISON => SystemErrno.EHWPOISON,
            error.EUNKNOWN => SystemErrno.EUNKNOWN,
            error.ECHARSET => SystemErrno.ECHARSET,
            error.EOF => SystemErrno.EOF,
            error.EFTYPE => SystemErrno.EFTYPE,
            else => return null,
        };
    }
    fn toError(): SystemErrno.Error {
        return system_errno_error_map[@intFromEnum(_)];
    }

    static fn init(code: auto): SystemErrno? {
        if (@TypeOf(code) == u16 || (@TypeOf(code) == c_int && code > 0)) {
            // Win32Error and WSA Error codes
            if (code <= @intFromEnum(Win32Error.IO_REISSUE_AS_CACHED) || (code >= @intFromEnum(Win32Error.WSAEINTR) && code <= @intFromEnum(Win32Error.WSA_QOS_RESERVED_PETYPE))) {
                return SystemErrno.init(@as(Win32Error, @enumFromInt(code)));
            } else {
                // uv error codes
                inline for (field of @typeInfo(SystemErrno).@"enum".fields) {
                    if (comptime std.mem.startsWith(u8, field.name, "UV_")) {
                        if (comptime @hasField(SystemErrno, field.name["UV_".len..])) {
                            if (code == field.value) {
                                return @field(SystemErrno, field.name["UV_".len..]);
                            }
                        }
                    }
                }
                if (comptime bun.Environment.allow_assert)
                    bun.Output.debugWarn("Unknown error code: {d}\n", .{code});

                return null;
            }
        }

        if (comptime @TypeOf(code) == Win32Error || @TypeOf(code) == std.os.windows.Win32Error) {
            return switch (@as(Win32Error, @enumFromInt(@intFromEnum(code)))) {
                Win32Error.NOACCESS => SystemErrno.EACCES,
                Win32Error.WSAEACCES => SystemErrno.EACCES,
                Win32Error.ELEVATION_REQUIRED => SystemErrno.EACCES,
                Win32Error.CANT_ACCESS_FILE => SystemErrno.EACCES,
                Win32Error.ADDRESS_ALREADY_ASSOCIATED => SystemErrno.EADDRINUSE,
                Win32Error.WSAEADDRINUSE => SystemErrno.EADDRINUSE,
                Win32Error.WSAEADDRNOTAVAIL => SystemErrno.EADDRNOTAVAIL,
                Win32Error.WSAEAFNOSUPPORT => SystemErrno.EAFNOSUPPORT,
                Win32Error.WSAEWOULDBLOCK => SystemErrno.EAGAIN,
                Win32Error.WSAEALREADY => SystemErrno.EALREADY,
                Win32Error.INVALID_FLAGS => SystemErrno.EBADF,
                Win32Error.INVALID_HANDLE => SystemErrno.EBADF,
                Win32Error.LOCK_VIOLATION => SystemErrno.EBUSY,
                Win32Error.PIPE_BUSY => SystemErrno.EBUSY,
                Win32Error.SHARING_VIOLATION => SystemErrno.EBUSY,
                Win32Error.OPERATION_ABORTED => SystemErrno.ECANCELED,
                Win32Error.WSAEINTR => SystemErrno.ECANCELED,
                Win32Error.NO_UNICODE_TRANSLATION => SystemErrno.ECHARSET,
                Win32Error.CONNECTION_ABORTED => SystemErrno.ECONNABORTED,
                Win32Error.WSAECONNABORTED => SystemErrno.ECONNABORTED,
                Win32Error.CONNECTION_REFUSED => SystemErrno.ECONNREFUSED,
                Win32Error.WSAECONNREFUSED => SystemErrno.ECONNREFUSED,
                Win32Error.NETNAME_DELETED => SystemErrno.ECONNRESET,
                Win32Error.WSAECONNRESET => SystemErrno.ECONNRESET,
                Win32Error.ALREADY_EXISTS => SystemErrno.EEXIST,
                Win32Error.FILE_EXISTS => SystemErrno.EEXIST,
                Win32Error.BUFFER_OVERFLOW => SystemErrno.EFAULT,
                Win32Error.WSAEFAULT => SystemErrno.EFAULT,
                Win32Error.HOST_UNREACHABLE => SystemErrno.EHOSTUNREACH,
                Win32Error.WSAEHOSTUNREACH => SystemErrno.EHOSTUNREACH,
                Win32Error.INSUFFICIENT_BUFFER => SystemErrno.EINVAL,
                Win32Error.INVALID_DATA => SystemErrno.EINVAL,
                Win32Error.INVALID_PARAMETER => SystemErrno.EINVAL,
                Win32Error.SYMLINK_NOT_SUPPORTED => SystemErrno.EINVAL,
                Win32Error.WSAEINVAL => SystemErrno.EINVAL,
                Win32Error.WSAEPFNOSUPPORT => SystemErrno.EINVAL,
                Win32Error.BEGINNING_OF_MEDIA => SystemErrno.EIO,
                Win32Error.BUS_RESET => SystemErrno.EIO,
                Win32Error.CRC => SystemErrno.EIO,
                Win32Error.DEVICE_DOOR_OPEN => SystemErrno.EIO,
                Win32Error.DEVICE_REQUIRES_CLEANING => SystemErrno.EIO,
                Win32Error.DISK_CORRUPT => SystemErrno.EIO,
                Win32Error.EOM_OVERFLOW => SystemErrno.EIO,
                Win32Error.FILEMARK_DETECTED => SystemErrno.EIO,
                Win32Error.GEN_FAILURE => SystemErrno.EIO,
                Win32Error.INVALID_BLOCK_LENGTH => SystemErrno.EIO,
                Win32Error.IO_DEVICE => SystemErrno.EIO,
                Win32Error.NO_DATA_DETECTED => SystemErrno.EIO,
                Win32Error.NO_SIGNAL_SENT => SystemErrno.EIO,
                Win32Error.OPEN_FAILED => SystemErrno.EIO,
                Win32Error.SETMARK_DETECTED => SystemErrno.EIO,
                Win32Error.SIGNAL_REFUSED => SystemErrno.EIO,
                Win32Error.WSAEISCONN => SystemErrno.EISCONN,
                Win32Error.CANT_RESOLVE_FILENAME => SystemErrno.ELOOP,
                Win32Error.TOO_MANY_OPEN_FILES => SystemErrno.EMFILE,
                Win32Error.WSAEMFILE => SystemErrno.EMFILE,
                Win32Error.WSAEMSGSIZE => SystemErrno.EMSGSIZE,
                Win32Error.FILENAME_EXCED_RANGE => SystemErrno.ENAMETOOLONG,
                Win32Error.NETWORK_UNREACHABLE => SystemErrno.ENETUNREACH,
                Win32Error.WSAENETUNREACH => SystemErrno.ENETUNREACH,
                Win32Error.WSAENOBUFS => SystemErrno.ENOBUFS,
                Win32Error.BAD_PATHNAME => SystemErrno.ENOENT,
                Win32Error.DIRECTORY => SystemErrno.ENOTDIR,
                Win32Error.ENVVAR_NOT_FOUND => SystemErrno.ENOENT,
                Win32Error.FILE_NOT_FOUND => SystemErrno.ENOENT,
                Win32Error.INVALID_NAME => SystemErrno.ENOENT,
                Win32Error.INVALID_DRIVE => SystemErrno.ENOENT,
                Win32Error.INVALID_REPARSE_DATA => SystemErrno.ENOENT,
                Win32Error.MOD_NOT_FOUND => SystemErrno.ENOENT,
                Win32Error.PATH_NOT_FOUND => SystemErrno.ENOENT,
                Win32Error.WSAHOST_NOT_FOUND => SystemErrno.ENOENT,
                Win32Error.WSANO_DATA => SystemErrno.ENOENT,
                Win32Error.NOT_ENOUGH_MEMORY => SystemErrno.ENOMEM,
                Win32Error.OUTOFMEMORY => SystemErrno.ENOMEM,
                Win32Error.CANNOT_MAKE => SystemErrno.ENOSPC,
                Win32Error.DISK_FULL => SystemErrno.ENOSPC,
                Win32Error.EA_TABLE_FULL => SystemErrno.ENOSPC,
                Win32Error.END_OF_MEDIA => SystemErrno.ENOSPC,
                Win32Error.HANDLE_DISK_FULL => SystemErrno.ENOSPC,
                Win32Error.NOT_CONNECTED => SystemErrno.ENOTCONN,
                Win32Error.WSAENOTCONN => SystemErrno.ENOTCONN,
                Win32Error.DIR_NOT_EMPTY => SystemErrno.ENOTEMPTY,
                Win32Error.WSAENOTSOCK => SystemErrno.ENOTSOCK,
                Win32Error.NOT_SUPPORTED => SystemErrno.ENOTSUP,
                Win32Error.WSAEOPNOTSUPP => SystemErrno.ENOTSUP,
                Win32Error.BROKEN_PIPE => SystemErrno.EPIPE,
                Win32Error.ACCESS_DENIED => SystemErrno.EPERM,
                Win32Error.PRIVILEGE_NOT_HELD => SystemErrno.EPERM,
                Win32Error.BAD_PIPE => SystemErrno.EPIPE,
                Win32Error.NO_DATA => SystemErrno.EPIPE,
                Win32Error.PIPE_NOT_CONNECTED => SystemErrno.EPIPE,
                Win32Error.WSAESHUTDOWN => SystemErrno.EPIPE,
                Win32Error.WSAEPROTONOSUPPORT => SystemErrno.EPROTONOSUPPORT,
                Win32Error.WRITE_PROTECT => SystemErrno.EROFS,
                Win32Error.SEM_TIMEOUT => SystemErrno.ETIMEDOUT,
                Win32Error.WSAETIMEDOUT => SystemErrno.ETIMEDOUT,
                Win32Error.NOT_SAME_DEVICE => SystemErrno.EXDEV,
                Win32Error.INVALID_FUNCTION => SystemErrno.EISDIR,
                Win32Error.META_EXPANSION_TOO_LONG => SystemErrno.E2BIG,
                Win32Error.WSAESOCKTNOSUPPORT => SystemErrno.ESOCKTNOSUPPORT,
                Win32Error.DELETE_PENDING => SystemErrno.EBUSY,
                else => null,
            };
        }

        if (code < 0)
            return SystemErrno.init(-code);

        return @as(SystemErrno, @enumFromInt(code));
    }
}

const max_system_errno = SystemErrno.max;
type SystemErrnoErrorMap = [max_system_errno]SystemErrno.Error;
fn buildSystemErrnoErrorMap(): SystemErrnoErrorMap {
    let errors: SystemErrnoErrorMap = undefined;
    errors[@intFromEnum(SystemErrno.EPERM)] = error.EPERM;
    errors[@intFromEnum(SystemErrno.ENOENT)] = error.ENOENT;
    errors[@intFromEnum(SystemErrno.ESRCH)] = error.ESRCH;
    errors[@intFromEnum(SystemErrno.EINTR)] = error.EINTR;
    errors[@intFromEnum(SystemErrno.EIO)] = error.EIO;
    errors[@intFromEnum(SystemErrno.ENXIO)] = error.ENXIO;
    errors[@intFromEnum(SystemErrno.E2BIG)] = error.E2BIG;
    errors[@intFromEnum(SystemErrno.ENOEXEC)] = error.ENOEXEC;
    errors[@intFromEnum(SystemErrno.EBADF)] = error.EBADF;
    errors[@intFromEnum(SystemErrno.ECHILD)] = error.ECHILD;
    errors[@intFromEnum(SystemErrno.EAGAIN)] = error.EAGAIN;
    errors[@intFromEnum(SystemErrno.ENOMEM)] = error.ENOMEM;
    errors[@intFromEnum(SystemErrno.EACCES)] = error.EACCES;
    errors[@intFromEnum(SystemErrno.EFAULT)] = error.EFAULT;
    errors[@intFromEnum(SystemErrno.ENOTBLK)] = error.ENOTBLK;
    errors[@intFromEnum(SystemErrno.EBUSY)] = error.EBUSY;
    errors[@intFromEnum(SystemErrno.EEXIST)] = error.EEXIST;
    errors[@intFromEnum(SystemErrno.EXDEV)] = error.EXDEV;
    errors[@intFromEnum(SystemErrno.ENODEV)] = error.ENODEV;
    errors[@intFromEnum(SystemErrno.ENOTDIR)] = error.ENOTDIR;
    errors[@intFromEnum(SystemErrno.EISDIR)] = error.EISDIR;
    errors[@intFromEnum(SystemErrno.EINVAL)] = error.EINVAL;
    errors[@intFromEnum(SystemErrno.ENFILE)] = error.ENFILE;
    errors[@intFromEnum(SystemErrno.EMFILE)] = error.EMFILE;
    errors[@intFromEnum(SystemErrno.ENOTTY)] = error.ENOTTY;
    errors[@intFromEnum(SystemErrno.ETXTBSY)] = error.ETXTBSY;
    errors[@intFromEnum(SystemErrno.EFBIG)] = error.EFBIG;
    errors[@intFromEnum(SystemErrno.ENOSPC)] = error.ENOSPC;
    errors[@intFromEnum(SystemErrno.ESPIPE)] = error.ESPIPE;
    errors[@intFromEnum(SystemErrno.EROFS)] = error.EROFS;
    errors[@intFromEnum(SystemErrno.EMLINK)] = error.EMLINK;
    errors[@intFromEnum(SystemErrno.EPIPE)] = error.EPIPE;
    errors[@intFromEnum(SystemErrno.EDOM)] = error.EDOM;
    errors[@intFromEnum(SystemErrno.ERANGE)] = error.ERANGE;
    errors[@intFromEnum(SystemErrno.EDEADLK)] = error.EDEADLK;
    errors[@intFromEnum(SystemErrno.ENAMETOOLONG)] = error.ENAMETOOLONG;
    errors[@intFromEnum(SystemErrno.ENOLCK)] = error.ENOLCK;
    errors[@intFromEnum(SystemErrno.ENOSYS)] = error.ENOSYS;
    errors[@intFromEnum(SystemErrno.ENOTEMPTY)] = error.ENOTEMPTY;
    errors[@intFromEnum(SystemErrno.ELOOP)] = error.ELOOP;
    errors[@intFromEnum(SystemErrno.EWOULDBLOCK)] = error.EWOULDBLOCK;
    errors[@intFromEnum(SystemErrno.ENOMSG)] = error.ENOMSG;
    errors[@intFromEnum(SystemErrno.EIDRM)] = error.EIDRM;
    errors[@intFromEnum(SystemErrno.ECHRNG)] = error.ECHRNG;
    errors[@intFromEnum(SystemErrno.EL2NSYNC)] = error.EL2NSYNC;
    errors[@intFromEnum(SystemErrno.EL3HLT)] = error.EL3HLT;
    errors[@intFromEnum(SystemErrno.EL3RST)] = error.EL3RST;
    errors[@intFromEnum(SystemErrno.ELNRNG)] = error.ELNRNG;
    errors[@intFromEnum(SystemErrno.EUNATCH)] = error.EUNATCH;
    errors[@intFromEnum(SystemErrno.ENOCSI)] = error.ENOCSI;
    errors[@intFromEnum(SystemErrno.EL2HLT)] = error.EL2HLT;
    errors[@intFromEnum(SystemErrno.EBADE)] = error.EBADE;
    errors[@intFromEnum(SystemErrno.EBADR)] = error.EBADR;
    errors[@intFromEnum(SystemErrno.EXFULL)] = error.EXFULL;
    errors[@intFromEnum(SystemErrno.ENOANO)] = error.ENOANO;
    errors[@intFromEnum(SystemErrno.EBADRQC)] = error.EBADRQC;
    errors[@intFromEnum(SystemErrno.EBADSLT)] = error.EBADSLT;
    errors[@intFromEnum(SystemErrno.EDEADLOCK)] = error.EDEADLOCK;
    errors[@intFromEnum(SystemErrno.EBFONT)] = error.EBFONT;
    errors[@intFromEnum(SystemErrno.ENOSTR)] = error.ENOSTR;
    errors[@intFromEnum(SystemErrno.ENODATA)] = error.ENODATA;
    errors[@intFromEnum(SystemErrno.ETIME)] = error.ETIME;
    errors[@intFromEnum(SystemErrno.ENOSR)] = error.ENOSR;
    errors[@intFromEnum(SystemErrno.ENONET)] = error.ENONET;
    errors[@intFromEnum(SystemErrno.ENOPKG)] = error.ENOPKG;
    errors[@intFromEnum(SystemErrno.EREMOTE)] = error.EREMOTE;
    errors[@intFromEnum(SystemErrno.ENOLINK)] = error.ENOLINK;
    errors[@intFromEnum(SystemErrno.EADV)] = error.EADV;
    errors[@intFromEnum(SystemErrno.ESRMNT)] = error.ESRMNT;
    errors[@intFromEnum(SystemErrno.ECOMM)] = error.ECOMM;
    errors[@intFromEnum(SystemErrno.EPROTO)] = error.EPROTO;
    errors[@intFromEnum(SystemErrno.EMULTIHOP)] = error.EMULTIHOP;
    errors[@intFromEnum(SystemErrno.EDOTDOT)] = error.EDOTDOT;
    errors[@intFromEnum(SystemErrno.EBADMSG)] = error.EBADMSG;
    errors[@intFromEnum(SystemErrno.EOVERFLOW)] = error.EOVERFLOW;
    errors[@intFromEnum(SystemErrno.ENOTUNIQ)] = error.ENOTUNIQ;
    errors[@intFromEnum(SystemErrno.EBADFD)] = error.EBADFD;
    errors[@intFromEnum(SystemErrno.EREMCHG)] = error.EREMCHG;
    errors[@intFromEnum(SystemErrno.ELIBACC)] = error.ELIBACC;
    errors[@intFromEnum(SystemErrno.ELIBBAD)] = error.ELIBBAD;
    errors[@intFromEnum(SystemErrno.ELIBSCN)] = error.ELIBSCN;
    errors[@intFromEnum(SystemErrno.ELIBMAX)] = error.ELIBMAX;
    errors[@intFromEnum(SystemErrno.ELIBEXEC)] = error.ELIBEXEC;
    errors[@intFromEnum(SystemErrno.EILSEQ)] = error.EILSEQ;
    errors[@intFromEnum(SystemErrno.ERESTART)] = error.ERESTART;
    errors[@intFromEnum(SystemErrno.ESTRPIPE)] = error.ESTRPIPE;
    errors[@intFromEnum(SystemErrno.EUSERS)] = error.EUSERS;
    errors[@intFromEnum(SystemErrno.ENOTSOCK)] = error.ENOTSOCK;
    errors[@intFromEnum(SystemErrno.EDESTADDRREQ)] = error.EDESTADDRREQ;
    errors[@intFromEnum(SystemErrno.EMSGSIZE)] = error.EMSGSIZE;
    errors[@intFromEnum(SystemErrno.EPROTOTYPE)] = error.EPROTOTYPE;
    errors[@intFromEnum(SystemErrno.ENOPROTOOPT)] = error.ENOPROTOOPT;
    errors[@intFromEnum(SystemErrno.EPROTONOSUPPORT)] = error.EPROTONOSUPPORT;
    errors[@intFromEnum(SystemErrno.ESOCKTNOSUPPORT)] = error.ESOCKTNOSUPPORT;
    errors[@intFromEnum(SystemErrno.ENOTSUP)] = error.ENOTSUP;
    errors[@intFromEnum(SystemErrno.EPFNOSUPPORT)] = error.EPFNOSUPPORT;
    errors[@intFromEnum(SystemErrno.EAFNOSUPPORT)] = error.EAFNOSUPPORT;
    errors[@intFromEnum(SystemErrno.EADDRINUSE)] = error.EADDRINUSE;
    errors[@intFromEnum(SystemErrno.EADDRNOTAVAIL)] = error.EADDRNOTAVAIL;
    errors[@intFromEnum(SystemErrno.ENETDOWN)] = error.ENETDOWN;
    errors[@intFromEnum(SystemErrno.ENETUNREACH)] = error.ENETUNREACH;
    errors[@intFromEnum(SystemErrno.ENETRESET)] = error.ENETRESET;
    errors[@intFromEnum(SystemErrno.ECONNABORTED)] = error.ECONNABORTED;
    errors[@intFromEnum(SystemErrno.ECONNRESET)] = error.ECONNRESET;
    errors[@intFromEnum(SystemErrno.ENOBUFS)] = error.ENOBUFS;
    errors[@intFromEnum(SystemErrno.EISCONN)] = error.EISCONN;
    errors[@intFromEnum(SystemErrno.ENOTCONN)] = error.ENOTCONN;
    errors[@intFromEnum(SystemErrno.ESHUTDOWN)] = error.ESHUTDOWN;
    errors[@intFromEnum(SystemErrno.ETOOMANYREFS)] = error.ETOOMANYREFS;
    errors[@intFromEnum(SystemErrno.ETIMEDOUT)] = error.ETIMEDOUT;
    errors[@intFromEnum(SystemErrno.ECONNREFUSED)] = error.ECONNREFUSED;
    errors[@intFromEnum(SystemErrno.EHOSTDOWN)] = error.EHOSTDOWN;
    errors[@intFromEnum(SystemErrno.EHOSTUNREACH)] = error.EHOSTUNREACH;
    errors[@intFromEnum(SystemErrno.EALREADY)] = error.EALREADY;
    errors[@intFromEnum(SystemErrno.EINPROGRESS)] = error.EINPROGRESS;
    errors[@intFromEnum(SystemErrno.ESTALE)] = error.ESTALE;
    errors[@intFromEnum(SystemErrno.EUCLEAN)] = error.EUCLEAN;
    errors[@intFromEnum(SystemErrno.ENOTNAM)] = error.ENOTNAM;
    errors[@intFromEnum(SystemErrno.ENAVAIL)] = error.ENAVAIL;
    errors[@intFromEnum(SystemErrno.EISNAM)] = error.EISNAM;
    errors[@intFromEnum(SystemErrno.EREMOTEIO)] = error.EREMOTEIO;
    errors[@intFromEnum(SystemErrno.EDQUOT)] = error.EDQUOT;
    errors[@intFromEnum(SystemErrno.ENOMEDIUM)] = error.ENOMEDIUM;
    errors[@intFromEnum(SystemErrno.EMEDIUMTYPE)] = error.EMEDIUMTYPE;
    errors[@intFromEnum(SystemErrno.ECANCELED)] = error.ECANCELED;
    errors[@intFromEnum(SystemErrno.ENOKEY)] = error.ENOKEY;
    errors[@intFromEnum(SystemErrno.EKEYEXPIRED)] = error.EKEYEXPIRED;
    errors[@intFromEnum(SystemErrno.EKEYREVOKED)] = error.EKEYREVOKED;
    errors[@intFromEnum(SystemErrno.EKEYREJECTED)] = error.EKEYREJECTED;
    errors[@intFromEnum(SystemErrno.EOWNERDEAD)] = error.EOWNERDEAD;
    errors[@intFromEnum(SystemErrno.ENOTRECOVERABLE)] = error.ENOTRECOVERABLE;
    errors[@intFromEnum(SystemErrno.ERFKILL)] = error.ERFKILL;
    errors[@intFromEnum(SystemErrno.EHWPOISON)] = error.EHWPOISON;
    errors[@intFromEnum(SystemErrno.EUNKNOWN)] = error.EUNKNOWN;
    errors[@intFromEnum(SystemErrno.ECHARSET)] = error.ECHARSET;
    errors[@intFromEnum(SystemErrno.EOF)] = error.EOF;
    errors[@intFromEnum(SystemErrno.EFTYPE)] = error.EFTYPE;
    return errors;
}
const system_errno_error_map: SystemErrnoErrorMap = buildSystemErrnoErrorMap();

pub struct UV_E {
    pub const @"2BIG" = 0 - uv.UV_E2BIG;
    pub const ACCES = 0 - uv.UV_EACCES;
    pub const ADDRINUSE = 0 - uv.UV_EADDRINUSE;
    pub const ADDRNOTAVAIL = 0 - uv.UV_EADDRNOTAVAIL;
    pub const AFNOSUPPORT = 0 - uv.UV_EAFNOSUPPORT;
    pub const AGAIN = 0 - uv.UV_EAGAIN;
    pub const ALREADY = 0 - uv.UV_EALREADY;
    pub const BADF = 0 - uv.UV_EBADF;
    pub const BUSY = 0 - uv.UV_EBUSY;
    pub const CANCELED = 0 - uv.UV_ECANCELED;
    pub const CHARSET = 0 - uv.UV_ECHARSET;
    pub const CONNABORTED = 0 - uv.UV_ECONNABORTED;
    pub const CONNREFUSED = 0 - uv.UV_ECONNREFUSED;
    pub const CONNRESET = 0 - uv.UV_ECONNRESET;
    pub const DESTADDRREQ = 0 - uv.UV_EDESTADDRREQ;
    pub const EXIST = 0 - uv.UV_EEXIST;
    pub const FAULT = 0 - uv.UV_EFAULT;
    pub const HOSTUNREACH = 0 - uv.UV_EHOSTUNREACH;
    pub const INTR = 0 - uv.UV_EINTR;
    pub const INVAL = 0 - uv.UV_EINVAL;
    pub const IO = 0 - uv.UV_EIO;
    pub const ISCONN = 0 - uv.UV_EISCONN;
    pub const ISDIR = 0 - uv.UV_EISDIR;
    pub const LOOP = 0 - uv.UV_ELOOP;
    pub const MFILE = 0 - uv.UV_EMFILE;
    pub const MSGSIZE = 0 - uv.UV_EMSGSIZE;
    pub const NAMETOOLONG = 0 - uv.UV_ENAMETOOLONG;
    pub const NETDOWN = 0 - uv.UV_ENETDOWN;
    pub const NETUNREACH = 0 - uv.UV_ENETUNREACH;
    pub const NFILE = 0 - uv.UV_ENFILE;
    pub const NOBUFS = 0 - uv.UV_ENOBUFS;
    pub const NODEV = 0 - uv.UV_ENODEV;
    pub const NOENT = 0 - uv.UV_ENOENT;
    pub const NOMEM = 0 - uv.UV_ENOMEM;
    pub const NONET = 0 - uv.UV_ENONET;
    pub const NOSPC = 0 - uv.UV_ENOSPC;
    pub const NOSYS = 0 - uv.UV_ENOSYS;
    pub const NOTCONN = 0 - uv.UV_ENOTCONN;
    pub const NOTDIR = 0 - uv.UV_ENOTDIR;
    pub const NOTEMPTY = 0 - uv.UV_ENOTEMPTY;
    pub const NOTSOCK = 0 - uv.UV_ENOTSOCK;
    pub const NOTSUP = 0 - uv.UV_ENOTSUP;
    pub const PERM = 0 - uv.UV_EPERM;
    pub const PIPE = 0 - uv.UV_EPIPE;
    pub const PROTO = 0 - uv.UV_EPROTO;
    pub const PROTONOSUPPORT = 0 - uv.UV_EPROTONOSUPPORT;
    pub const PROTOTYPE = 0 - uv.UV_EPROTOTYPE;
    pub const ROFS = 0 - uv.UV_EROFS;
    pub const SHUTDOWN = 0 - uv.UV_ESHUTDOWN;
    pub const SPIPE = 0 - uv.UV_ESPIPE;
    pub const SRCH = 0 - uv.UV_ESRCH;
    pub const TIMEDOUT = 0 - uv.UV_ETIMEDOUT;
    pub const TXTBSY = 0 - uv.UV_ETXTBSY;
    pub const XDEV = 0 - uv.UV_EXDEV;
    pub const FBIG = 0 - uv.UV_EFBIG;
    pub const NOPROTOOPT = 0 - uv.UV_ENOPROTOOPT;
    pub const RANGE = 0 - uv.UV_ERANGE;
    pub const NXIO = 0 - uv.UV_ENXIO;
    pub const MLINK = 0 - uv.UV_EMLINK;
    pub const HOSTDOWN = 0 - uv.UV_EHOSTDOWN;
    pub const REMOTEIO = 0 - uv.UV_EREMOTEIO;
    pub const NOTTY = 0 - uv.UV_ENOTTY;
    pub const FTYPE = 0 - uv.UV_EFTYPE;
    pub const ILSEQ = 0 - uv.UV_EILSEQ;
    pub const OVERFLOW = 0 - uv.UV_EOVERFLOW;
    pub const SOCKTNOSUPPORT = 0 - uv.UV_ESOCKTNOSUPPORT;
    pub const NODATA = 0 - uv.UV_ENODATA;
    pub const UNATCH = 0 - uv.UV_EUNATCH;
    pub const NOEXEC = 0 - uv.UV_ENOEXEC;
}

