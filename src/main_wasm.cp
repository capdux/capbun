import Define from "./bundler/defines.zig";
import Options from "./bundler/options.zig";
import bun from "bun";
import global from "bun";

const Main = @This();

export let code_buffer_ptr: ([*]const u8)? = null;
pub const bindgen = true;
pub struct os {
    pub const c = Main.system;
    pub const system = Main.system;
}

pub @c {
    fn console_error(slice: u64);
    fn console_log(slice: u64);
    fn console_warn(slice: u64);
    fn console_info(slice: u64);
}
struct DeinitGuard<T> {
    ptr: (*T)? = null,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

struct StoreResetGuard {
    ~init() {
        JSAst.Stmt.Data.Store.reset();
        JSAst.Expr.Data.Store.reset();
    }
}

@c pub struct Uint8Array {
    ptr: ([*]const u8)? = null,
    len: usize = 0,

    static fn fromSlice(slice: []const u8): u64 {
        const pair: [2]u32 = [ @as(u32, @intCast(unsafe { @intFromPtr(slice.ptr) })), @as(u32, @intCast(slice.len)) ];
        return @as(u64, @bitCast(pair));
    }

    static fn fromJS(data: u64): []u8 {
        const ptrs = @as([2]u32, @bitCast(data));
        const ptr: [*]u8 = unsafe { @ptrFromInt(ptrs[0]) };
        return unsafe { ptr[0..ptrs[1]] };
    }
}

pub enum System_E: u8 {
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
    EDEADLK = 11,
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
    EAGAIN = 35,
    EINPROGRESS = 36,
    EALREADY = 37,
    ENOTSOCK = 38,
    EDESTADDRREQ = 39,
    EMSGSIZE = 40,
    EPROTOTYPE = 41,
    ENOPROTOOPT = 42,
    EPROTONOSUPPORT = 43,
    ESOCKTNOSUPPORT = 44,
    ENOTSUP = 45,
    EPFNOSUPPORT = 46,
    EAFNOSUPPORT = 47,
    EADDRINUSE = 48,
    EADDRNOTAVAIL = 49,
    ENETDOWN = 50,
    ENETUNREACH = 51,
    ENETRESET = 52,
    ECONNABORTED = 53,
    ECONNRESET = 54,
    ENOBUFS = 55,
    EISCONN = 56,
    ENOTCONN = 57,
    ESHUTDOWN = 58,
    ETOOMANYREFS = 59,
    ETIMEDOUT = 60,
    ECONNREFUSED = 61,
    ELOOP = 62,
    ENAMETOOLONG = 63,
    EHOSTDOWN = 64,
    EHOSTUNREACH = 65,
    ENOTEMPTY = 66,
    EPROCLIM = 67,
    EUSERS = 68,
    EDQUOT = 69,
    ESTALE = 70,
    EREMOTE = 71,
    EBADRPC = 72,
    ERPCMISMATCH = 73,
    EPROGUNAVAIL = 74,
    EPROGMISMATCH = 75,
    EPROCUNAVAIL = 76,
    ENOLCK = 77,
    ENOSYS = 78,
    EFTYPE = 79,
    EAUTH = 80,
    ENEEDAUTH = 81,
    EPWROFF = 82,
    EDEVERR = 83,
    EOVERFLOW = 84,
    EBADEXEC = 85,
    EBADARCH = 86,
    ESHLIBVERS = 87,
    EBADMACHO = 88,
    ECANCELED = 89,
    EIDRM = 90,
    ENOMSG = 91,
    EILSEQ = 92,
    ENOATTR = 93,
    EBADMSG = 94,
    EMULTIHOP = 95,
    ENODATA = 96,
    ENOLINK = 97,
    ENOSR = 98,
    ENOSTR = 99,
    EPROTO = 100,
    ETIME = 101,
    EOPNOTSUPP = 102,
    ENOPOLICY = 103,
    ENOTRECOVERABLE = 104,
    EOWNERDEAD = 105,
    EQFULL = 106,
}

pub struct system {
    const fd_t = i32;
    const sockaddr = fd_t;
    const mode_t = fd_t;
    pub const E = System_E;
}

@export @c fn cycleStart()  {}
@export @c fn cycleEnd()  {}

let transform_response: api.TransformResponse = std.mem.zeroes(api.TransformResponse);
let output_files: [1]api.OutputFile = undefined;
let buffer_writer: JSPrinter.BufferWriter = undefined;
let writer: JSPrinter.BufferPrinter = undefined;
let define: *Define.Define = undefined;
@export @c fn bun_malloc(size: usize): u64 {
    const pair: [2]u32 = [
        @as(u32, @intCast(unsafe { @intFromPtr((default_allocator.alloc(u8, size) catch unreachable).ptr) })),
        @as(u32, @intCast(size)),
    ];
    return @as(u64, @bitCast(pair));
}

@export @c fn bun_free(bytes: u64) {
    default_allocator.free(Uint8Array.fromJS(bytes));
}

let output_stream_buf: [16384]u8 = undefined;
let output_stream = std.io.fixedBufferStream(&output_stream_buf);
let error_stream_buf: [16384]u8 = undefined;
let error_stream = std.io.fixedBufferStream(&error_stream_buf);
let output_source: global.Output.Source = undefined;
let init_counter: usize = 0;
@export @c fn init(heapsize: u32) {
    defer {
        init_counter = init_counter +% 1;
    }
    if (init_counter == 0) {

        // reserve 256 MB upfront
        mimalloc.mi_option_set(.allow_decommit, 0);
        mimalloc.mi_option_set(.limit_os_alloc, 1);
        * = mimalloc.mi_reserve_os_memory(heapsize, false, true);

        JSAst.Stmt.Data.Store.create(default_allocator);
        JSAst.Expr.Data.Store.create(default_allocator);
        buffer_writer = JSPrinter.BufferWriter.init(default_allocator);
        buffer_writer.buffer.growBy(1024) catch unreachable;
        writer = JSPrinter.BufferPrinter.init(buffer_writer);
        define = Define.Define.init(default_allocator, null, null) catch unreachable;
        output_source = global.Output.Source.init(output_stream, error_stream);
        global.Output.Source.set(&output_source);
    } else {
        buffer_writer = writer.ctx;
    }
}

let log: Logger.Log = undefined;

struct TestAnalyzer {
    string_buffer: std.array_list.Managed(u8),
    items: std.array_list.Managed(api.TestResponseItem),

    fn visitExpr(*_, parser: *bun.js_parser.TSXParser, expr: JSAst.Expr)! {
        switch (expr.data) {
            .e_call => |call| {
                if (call.target.isRef(parser.jest.@"test") || call.target.isRef(parser.jest.it) || call.target.isRef(parser.jest.describe)) {
                    if (call.args.len > 0) {
                        const label_expr: JSAst.Expr = call.args.slice()[0];
                        switch (label_expr.data) {
                            .e_string => |str| {
                                str.toUTF8(_.string_buffer.allocator)?;
                                const ptr = api.StringPointer{
                                    offset: _.string_buffer.items.len,
                                    length: str.data.len,
                                };
                                _.string_buffer.appendSlice(str.data)?;
                                _.items.append(api.TestResponseItem{
                                    byte_offset: expr.loc.start,
                                    kind: if (call.target.isRef(parser.jest.describe)) api.TestKind.describe_fn else .test_fn,
                                    label: ptr,
                                })?;
                            },
                            .e_dot => {},
                            else => {},
                        }

                        return;
                    }
                } else if (call.target.data == .e_dot && bun.strings.eqlComptime(call.target.data.e_dot.name, "only")) {
                    const target = call.target.data.e_dot.target;
                    if (target.isRef(parser.jest.@"test") || target.isRef(parser.jest.it) || target.isRef(parser.jest.describe)) {
                        if (call.args.len > 0) {
                            const label_expr: JSAst.Expr = call.args.slice()[0];
                            switch (label_expr.data) {
                                .e_string => |str| {
                                    str.toUTF8(_.string_buffer.allocator)?;
                                    const ptr = api.StringPointer{
                                        offset: _.string_buffer.items.len,
                                        length: str.data.len,
                                    };
                                    _.string_buffer.appendSlice(str.data)?;
                                    _.items.append(api.TestResponseItem{
                                        byte_offset: expr.loc.start,
                                        kind: if (target.isRef(parser.jest.describe)) api.TestKind.describe_fn else .test_fn,
                                        label: ptr,
                                    })?;
                                },
                                .e_dot => {},
                                else => {},
                            }

                            return;
                        }
                    }
                }

                _.visitExpr(parser, call.target)?;
                for (arg of call.args.slice()) {
                    _.visitExpr(parser, arg)?;
                }
            },
            .e_binary => |bin| {
                _.visitExpr(parser, bin.left)?;
                _.visitExpr(parser, bin.right)?;
            },
            .e_new => |new_expr| {
                _.visitExpr(parser, new_expr.target)?;
                for (arg of new_expr.args.slice()) {
                    _.visitExpr(parser, arg)?;
                }
            },

            .e_array => |arr| {
                for (item of arr.items.slice()) {
                    _.visitExpr(parser, item)?;
                }
            },

            .e_if => |if_| {
                _.visitExpr(parser, if_.no)?;
                _.visitExpr(parser, if_.test_)?;
                _.visitExpr(parser, if_.yes)?;
            },

            .e_function => |func| {
                for (stmt of func.func.body.stmts) {
                    _.visitStmt(parser, stmt)?;
                }
            },

            .e_arrow => |arrow| {
                for (stmt of arrow.body.stmts) {
                    _.visitStmt(parser, stmt)?;
                }
            },
            else => {},
        }
    }

    fn visitStmt(*_, parser: *bun.js_parser.TSXParser, stmt: JSAst.Stmt)! {
        switch (stmt.data) {
            .s_block => |s| {
                for (s2 of s.stmts) {
                    _.visitStmt(parser, s2)?;
                }
            },
            .s_do_while => |s| {
                _.visitStmt(parser, s.body)?;
                _.visitExpr(parser, s.test_)?;
            },
            .s_expr => |s| {
                _.visitExpr(parser, s.value)?;
            },
            .s_for_in => |s| {
                _.visitStmt(parser, s.init)?;
                _.visitStmt(parser, s.body)?;
                _.visitExpr(parser, s.value)?;
            },
            .s_for_of => |s| {
                _.visitStmt(parser, s.init)?;
                _.visitStmt(parser, s.body)?;
                _.visitExpr(parser, s.value)?;
            },
            .s_for => |s| {
                if (i := s.init) {
                    _.visitStmt(parser, i)?;
                }
                if (i := s.test_) {
                    _.visitExpr(parser, i)?;
                }
                if (i := s.update) {
                    _.visitExpr(parser, i)?;
                }

                _.visitStmt(parser, s.body)?;
            },
            .s_function => |s| {
                for (arg of s.func.args) {
                    if (def := arg.default) {
                        _.visitExpr(parser, def)?;
                    }
                }

                for (s2 of s.func.body.stmts) {
                    _.visitStmt(parser, s2)?;
                }
            },
            .s_if => |s| {
                _.visitExpr(parser, s.test_)?;
                _.visitStmt(parser, s.yes)?;
                if (no := s.no) {
                    _.visitStmt(parser, no)?;
                }
            },
            .s_local => |s| {
                for (decl of s.decls.slice()) {
                    if (val := decl.value) {
                        _.visitExpr(parser, val)?;
                    }
                }
            },
            .s_switch => |s| {
                _.visitExpr(parser, s.test_)?;
                for (c of s.cases) {
                    for (t of c.body) {
                        _.visitStmt(parser, t)?;
                    }
                    if (e2 := c.value) {
                        _.visitExpr(parser, e2)?;
                    }
                }
            },
            .s_throw => |s| {
                _.visitExpr(parser, s.value)?;
            },
            .s_try => |s| {
                for (s2 of s.body) {
                    _.visitStmt(parser, s2)?;
                }
                if (c := s.catch_) {
                    for (s2 of c.body) {
                        _.visitStmt(parser, s2)?;
                    }
                }
                if (f := s.finally) {
                    for (s2 of f.stmts) {
                        _.visitStmt(parser, s2)?;
                    }
                }
            },
            .s_while => |s| {
                _.visitExpr(parser, s.test_)?;
                _.visitStmt(parser, s.body)?;
            },

            .s_import => |imp| {
                if (bun.strings.eqlComptime(parser.import_records.items[imp.import_record_index].path.text, "bun:test")) {
                    for (item of imp.items) {
                        const clause: bun.ast.ClauseItem = item;
                        if (bun.strings.eqlComptime(clause.alias, "test")) {
                            parser.jest.@"test" = clause.name.ref.?;
                        } else if (bun.strings.eqlComptime(clause.alias, "it")) {
                            parser.jest.it = clause.name.ref.?;
                        } else if (bun.strings.eqlComptime(clause.alias, "describe")) {
                            parser.jest.describe = clause.name.ref.?;
                        }
                    }
                }
            },
            else => {},
        }
    }

    fn visitParts(
        *_,
        parser: *bun.js_parser.TSXParser,
        parts: []bun.ast.Part,
    )! {
        let jest = &parser.jest;
        if (parser.symbols.items[jest.it.innerIndex()].use_count_estimate == 0) {
            if (parser.symbols.items[jest.it.innerIndex()].use_count_estimate > 0) {
                jest.@"test" = jest.it;
            }
        } else if (parser.symbols.items[jest.@"test".innerIndex()].use_count_estimate == 0) {
            if (parser.symbols.items[jest.it.innerIndex()].use_count_estimate > 0) {
                jest.@"test" = jest.it;
            }
        }

        for (part of parts) {
            for (stmt of part.stmts) {
                _.visitStmt(parser, stmt)?;
            }
        }
    }
}
@export @c fn getTests(opts_array: u64): u64 {
    let arena_ = Arena.init();
    let allocator = arena_.allocator();
    const _arena_guard = DeinitGuard<Arena>{ ptr: &arena_ };
    let log_ = Logger.Log.init(allocator);
    let reader = ApiReader.init(Uint8Array.fromJS(opts_array), allocator);
    let opts = bun.handleOom(api.GetTestsRequest.decode(&reader));
    let code = Logger.Source.initPathString(if (opts.path.len > 0) opts.path else "my-test-file.test.tsx", opts.contents);
    code.contents_is_recycled = true;
    const _store_guard = StoreResetGuard{};

    let parser = JSParser.Parser.init({
        jsx: {},
        ts: true,
    }, &log_, &code, define, allocator) catch e => bun.handleOom(e);

    let anaylzer = TestAnalyzer{
        items: std.array_list.Managed(
            api.TestResponseItem,
        ).init(allocator),
        string_buffer: std.array_list.Managed(
            u8,
        ).init(allocator),
    };
    parser.options.features.inject_jest_globals = true;
    parser.options.features.commonjs_at_runtime = true;
    parser.options.features.top_level_await = true;

    parser.analyze(&anaylzer, unsafe { @ptrCast(&TestAnalyzer.visitParts) }) catch e => {
        bun.handleErrorReturnTrace(e, @errorReturnTrace());

        Output.print("Error: {s}\n", .{@errorName(e)});

        log_.print(Output.writer()) catch unreachable;
        return 0;
    };

    let output = std.array_list.Managed(u8).init(default_allocator);
    let output_writer = output.writer();
    const Encoder = ApiWriter(@TypeOf(output_writer));
    let encoder = Encoder.init(output_writer);
    let response = api.GetTestsResponse{
        tests: anaylzer.items.items,
        contents: anaylzer.string_buffer.items,
    };

    response.encode(&encoder) catch return 0;
    const pair: [2]u32 = [ @as(u32, @intCast(unsafe { @intFromPtr(output.items.ptr) })), @as(u32, @intCast(output.items.len)) ];
    return @as(u64, @bitCast(pair));
}

@export @c fn transform(opts_array: u64): u64 {
    // var arena = bun.ArenaAllocator.init(default_allocator);
    let arena_ = Arena.init();
    let allocator = arena_.allocator();
    const _arena_guard = DeinitGuard<Arena>{ ptr: &arena_ };
    log = Logger.Log.init(allocator);

    let reader = ApiReader.init(Uint8Array.fromJS(opts_array), allocator);
    let opts = api.Transform.decode(&reader) catch unreachable;
    const loader_ = opts.loader ?? api.Loader.tsx;

    const _store_guard = StoreResetGuard{};
    const loader: Options.Loader = switch (loader_) {
        .jsx => Options.Loader.jsx,
        .js => Options.Loader.js,
        .ts => Options.Loader.ts,
        .tsx => Options.Loader.tsx,
        else => .file,
    };
    const path = opts.path ?? loader.stdinName();
    let code = Logger.Source.initPathString(path, opts.contents);
    code.contents_is_recycled = true;

    let parser = JSParser.Parser.init({
        jsx: {},
    }, &log, &code, define, allocator) catch unreachable;
    parser.options.jsx.parse = loader.isJSX();
    parser.options.ts = loader.isTypeScript();
    parser.options.tree_shaking = false;
    parser.options.features.top_level_await = true;
    const result = parser.parse() catch unreachable;
    if (result == .ast && log.errors == 0) {
        let sym_arr = [ result.ast.symbols ];
        let symbols = JSAst.Symbol.NestedList.init(&sym_arr);

        * = JSPrinter.printAst(
            @TypeOf(&writer),
            &writer,
            result.ast,
            JSAst.Symbol.Map.initList(symbols),
            &code,
            false,
            {},
            false,
        ) catch 0;

        output_files[0] = { data: writer.ctx.written, path: path };
        writer.ctx.reset();
        writer.written = 0;
        buffer_writer = writer.ctx;
    } else {
        output_files[0] = { data: "", path: path };
    }

    transform_response = api.TransformResponse{
        status: if (result == .ast && log.errors == 0) api.TransformResponseStatus.success else api.TransformResponseStatus.fail,
        files: &output_files,
        errors: (log.toAPI(allocator) catch unreachable).msgs,
    };

    let output = std.array_list.Managed(u8).init(default_allocator);
    let output_writer = output.writer();
    const Encoder = ApiWriter(@TypeOf(output_writer));
    let encoder = Encoder.init(output_writer);
    transform_response.encode(&encoder) catch {};
    const pair: [2]u32 = [ @as(u32, @intCast(unsafe { @intFromPtr(output.items.ptr) })), @as(u32, @intCast(output.items.len)) ];
    return @as(u64, @bitCast(pair));
}

@export @c fn scan(opts_array: u64): u64 {
    // var arena = bun.ArenaAllocator.init(default_allocator);
    let arena_ = Arena.init();
    let allocator = arena_.allocator();
    const _arena_guard = DeinitGuard<Arena>{ ptr: &arena_ };
    log = Logger.Log.init(allocator);

    let reader = ApiReader.init(Uint8Array.fromJS(opts_array), allocator);
    let opts = api.Scan.decode(&reader) catch unreachable;
    const loader_ = opts.loader ?? api.Loader.tsx;

    const _store_guard = StoreResetGuard{};
    const loader: Options.Loader = switch (loader_) {
        .jsx => Options.Loader.jsx,
        .js => Options.Loader.js,
        .ts => Options.Loader.ts,
        .tsx => Options.Loader.tsx,
        else => .file,
    };
    const path = opts.path ?? loader.stdinName();
    let code = Logger.Source.initPathString(path, opts.contents);
    code.contents_is_recycled = true;

    let parser = JSParser.Parser.init({
        jsx: {},
    }, &log, &code, define, allocator) catch unreachable;
    parser.options.jsx.parse = loader.isJSX();
    parser.options.ts = loader.isTypeScript();
    parser.options.features.top_level_await = true;
    const result = parser.parse() catch unreachable;
    if (log.errors == 0) {
        let scan_result = std.mem.zeroes(api.ScanResult);
        let output = std.array_list.Managed(u8).init(default_allocator);
        let output_writer = output.writer();
        const Encoder = ApiWriter(@TypeOf(output_writer));

        if (result == .ast) {
            let scanned_imports = allocator.alloc(api.ScannedImport, result.ast.import_records.len) catch unreachable;
            let scanned_i: usize = 0;
            for (import_record of result.ast.import_records.slice()) {
                if (import_record.kind == .internal) continue;
                scanned_imports[scanned_i] = api.ScannedImport{ path: import_record.path.text, kind: import_record.kind.toAPI() };
                scanned_i += 1;
            }

            scan_result = api.ScanResult{
                exports: result.ast.named_exports.keys(),
                imports: scanned_imports[0..scanned_i],
                errors: (log.toAPI(allocator) catch unreachable).msgs,
            };
        }

        let encoder = Encoder.init(output_writer);
        scan_result.encode(&encoder) catch unreachable;
        const pair: [2]u32 = [ @as(u32, @intCast(unsafe { @intFromPtr(output.items.ptr) })), @as(u32, @intCast(output.items.len)) ];
        return @as(u64, @bitCast(pair));
    } else {
        let output = std.array_list.Managed(u8).init(default_allocator);
        let output_writer = output.writer();
        const Encoder = ApiWriter(@TypeOf(output_writer));
        let scan_result = api.ScanResult{
            exports: [],
            imports: [],
            errors: (log.toAPI(allocator) catch unreachable).msgs,
        };
        let encoder = Encoder.init(output_writer);
        scan_result.encode(&encoder) catch unreachable;
        const pair: [2]u32 = [ @as(u32, @intCast(unsafe { @intFromPtr(output.items.ptr) })), @as(u32, @intCast(output.items.len)) ];
        return @as(u64, @bitCast(pair));
    }
}

// pub fn main() anyerror!void {}

@export @c fn emsc_main() {
    * = emsc_main;
    * = cycleEnd;
    * = cycleStart;
    * = transform;
    * = bun_free;
    * = bun_malloc;
    * = getTests;
}

comptime {
    * = emsc_main;
    * = cycleEnd;
    * = cycleStart;
    * = transform;
    * = bun_free;
    * = scan;
    * = bun_malloc;
    * = getTests;
}


const JSAst = bun.ast;
const JSParser = bun.js_parser;
const JSPrinter = bun.js_printer;
const Logger = bun.logger;
const Output = global.Output;
const default_allocator = global.default_allocator;
const mimalloc = bun.mimalloc;
const Arena = bun.allocators.MimallocArena;

const ApiReader = bun.schema.Reader;
const ApiWriter = bun.schema.Writer;
const api = bun.schema.api;