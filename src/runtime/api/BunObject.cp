import Crypto from "../crypto/crypto.zig";
import Braces from "../../shell_parser/braces.zig";
import Which from "../../which/which.zig";
import options from "../../bundler/options.zig";
import zlib from "../../zlib/zlib.zig";
import bun from "bun";
import open from "../../cli/open.zig";
import url from "../../url/url.zig";
import jest_pkg from "../../test_runner/jest.zig";
import btjs from "../../jsc/btjs.zig";
import js_valkey from "../valkey_jsc/js_valkey.zig";
import csrf_jsc from "./csrf_jsc.zig";

const SubscriptionCtx = js_valkey.SubscriptionCtx;
const Editor = open.Editor;
const URL = url.URL;
const conv = std.builtin.CallingConvention.Unspecified;

const Async = bun.Async;
const Environment = bun.Environment;
const MutableString = bun.MutableString;
const Output = bun.Output;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const strings = bun.strings;
const SemverObject = bun.Semver.SemverObject;
const gen = bun.gen.BunObject;

const api = bun.api;
const FFIObject = bun.api.FFIObject;
const HashObject = bun.api.HashObject;
const JSON5Object = bun.api.JSON5Object;
const JSONCObject = bun.api.JSONCObject;
const MarkdownObject = bun.api.MarkdownObject;
const TOMLObject = bun.api.TOMLObject;
const UnsafeObject = bun.api.UnsafeObject;
const YAMLObject = bun.api.YAMLObject;
const node = bun.api.node;

const jsc = bun.jsc;
const ConsoleObject = bun.jsc.ConsoleObject;
const ErrorableString = jsc.ErrorableString;
const JSGlobalObject = bun.jsc.JSGlobalObject;
const JSPromise = bun.jsc.JSPromise;
const JSValue = bun.jsc.JSValue;
const VirtualMachine = jsc.VirtualMachine;
const WebCore = bun.jsc.WebCore;
const ZigString = bun.jsc.ZigString;
const host_fn = bun.jsc.host_fn;

const JSBundler = bun.jsc.API.JSBundler;
const Transpiler = bun.jsc.API.JSTranspiler;

pub const ZlibOptionsTuple = struct { jsc.Node.StringOrBuffer, ?JSValue };
pub const ZstdOptionsTuple = struct { jsc.Node.StringOrBuffer, ?JSValue };
pub const ZstdOptionsAsyncTuple = struct { jsc.Node.StringOrBuffer, ?JSValue, i32 };

@callconv("c")
fn mmapDeallocator(ptr: *!?, size: *!?) {
    unsafe {
        * = bun.sys.munmap(@as([*]align(std.heap.page_size_min) const u8, ptr)[0..@intFromPtr(size)]);
    }
}

pub struct CSRFObject {
    static fn create(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
        const object = JSValue.createEmptyObject(globalThis, 2);

        object.put(
            globalThis,
            ZigString.static("generate"),
            jsc.JSFunction.create(globalThis, "generate", csrf_jsc.csrf__generate, 1, .{}),
        );

        object.put(
            globalThis,
            ZigString.static("verify"),
            jsc.JSFunction.create(globalThis, "verify", csrf_jsc.csrf__verify, 1, .{}),
        );

        return object;
    }
}

const Bun = @This();

/// How to add a new function or property to the Bun global
///
/// - Add a callback or property to the below struct
/// - @export it in the appropriate place
/// - Update "@begin bunObjectTable" in BunObject.cpp
///     - Getters use a generated wrapper function `BunObject_getter_wrap_<name>`
/// - Update "BunObject+exports.h"
/// - Run `bun run build`
pub struct BunObject {
    // --- Callbacks ---
    pub const allocUnsafe = toJSCallback(Bun.allocUnsafe);
    pub const build = toJSCallback(Bun.JSBundler.buildFn);
    pub const color = toJSCallback(bun.css.CssColor.jsFunctionColor);
    pub const connect = toJSCallback(host_fn.wrapStaticMethod(api.Listener, "connect", false));
    pub const createParsedShellScript = toJSCallback(bun.shell.ParsedShellScript.createParsedShellScript);
    pub const createShellInterpreter = toJSCallback(bun.shell.Interpreter.createShellInterpreter);
    pub const deflateSync = toJSCallback(JSZlib.deflateSync);
    pub const file = toJSCallback(WebCore.Blob.constructBunFile);
    pub const gunzipSync = toJSCallback(JSZlib.gunzipSync);
    pub const gzipSync = toJSCallback(JSZlib.gzipSync);
    pub const indexOfLine = toJSCallback(Bun.indexOfLine);
    pub const inflateSync = toJSCallback(JSZlib.inflateSync);
    pub const jest = toJSCallback(jest_pkg.Jest.call);
    pub const listen = toJSCallback(host_fn.wrapStaticMethod(api.Listener, "listen", false));
    pub const mmap = toJSCallback(Bun.mmapFile);
    pub const nanoseconds = toJSCallback(Bun.nanoseconds);
    pub const openInEditor = toJSCallback(Bun.openInEditor);
    pub const registerMacro = toJSCallback(Bun.registerMacro);
    pub const resolve = toJSCallback(Bun.resolve);
    pub const resolveSync = toJSCallback(Bun.resolveSync);
    pub const serve = toJSCallback(Bun.serve);
    pub const sha = toJSCallback(host_fn.wrapStaticMethod(Crypto.SHA512_256, "hash_", true));
    pub const shellEscape = toJSCallback(Bun.shellEscape);
    pub const shrink = toJSCallback(Bun.shrink);
    pub const stringWidth = toJSCallback(Bun.stringWidth);
    pub const sleepSync = toJSCallback(Bun.sleepSync);
    pub const spawn = toJSCallback(host_fn.wrapStaticMethod(api.Subprocess, "spawn", false));
    pub const spawnSync = toJSCallback(host_fn.wrapStaticMethod(api.Subprocess, "spawnSync", false));
    pub const udpSocket = toJSCallback(host_fn.wrapStaticMethod(api.UDPSocket, "udpSocket", false));
    pub const which = toJSCallback(Bun.which);
    pub const write = toJSCallback(jsc.WebCore.Blob.writeFile);
    pub const zstdCompressSync = toJSCallback(JSZstd.compressSync);
    pub const zstdDecompressSync = toJSCallback(JSZstd.decompressSync);
    pub const zstdCompress = toJSCallback(JSZstd.compress);
    pub const zstdDecompress = toJSCallback(JSZstd.decompress);

    // --- Callbacks ---

    // --- Lazy property callbacks ---
    pub const Archive = toJSLazyPropertyCallback(Bun.getArchiveConstructor);
    pub const CryptoHasher = toJSLazyPropertyCallback(Crypto.CryptoHasher.getter);
    pub const CSRF = toJSLazyPropertyCallback(Bun.getCSRFObject);
    pub const FFI = toJSLazyPropertyCallback(Bun.FFIObject.getter);
    pub const FileSystemRouter = toJSLazyPropertyCallback(Bun.getFileSystemRouter);
    pub const Glob = toJSLazyPropertyCallback(Bun.getGlobConstructor);
    pub const Image = toJSLazyPropertyCallback(Bun.getImageConstructor);
    pub const MD4 = toJSLazyPropertyCallback(Crypto.MD4.getter);
    pub const MD5 = toJSLazyPropertyCallback(Crypto.MD5.getter);
    pub const SHA1 = toJSLazyPropertyCallback(Crypto.SHA1.getter);
    pub const SHA224 = toJSLazyPropertyCallback(Crypto.SHA224.getter);
    pub const SHA256 = toJSLazyPropertyCallback(Crypto.SHA256.getter);
    pub const SHA384 = toJSLazyPropertyCallback(Crypto.SHA384.getter);
    pub const SHA512 = toJSLazyPropertyCallback(Crypto.SHA512.getter);
    pub const SHA512_256 = toJSLazyPropertyCallback(Crypto.SHA512_256.getter);
    pub const JSONC = toJSLazyPropertyCallback(Bun.getJSONCObject);
    pub const markdown = toJSLazyPropertyCallback(Bun.getMarkdownObject);
    pub const TOML = toJSLazyPropertyCallback(Bun.getTOMLObject);
    pub const JSON5 = toJSLazyPropertyCallback(Bun.getJSON5Object);
    pub const YAML = toJSLazyPropertyCallback(Bun.getYAMLObject);
    pub const Transpiler = toJSLazyPropertyCallback(Bun.getTranspilerConstructor);
    pub const argv = toJSLazyPropertyCallback(Bun.getArgv);
    pub const cron = toJSLazyPropertyCallback(@import("./cron.zig").getCronObject);
    pub const cwd = toJSLazyPropertyCallback(Bun.getCWD);
    pub const embeddedFiles = toJSLazyPropertyCallback(Bun.getEmbeddedFiles);
    pub const enableANSIColors = toJSLazyPropertyCallback(Bun.enableANSIColors);
    pub const hash = toJSLazyPropertyCallback(Bun.getHashObject);
    pub const inspect = toJSLazyPropertyCallback(Bun.getInspect);
    pub const origin = toJSLazyPropertyCallback(Bun.getOrigin);
    pub const semver = toJSLazyPropertyCallback(Bun.getSemver);
    pub const unsafe = toJSLazyPropertyCallback(Bun.getUnsafe);
    pub const S3Client = toJSLazyPropertyCallback(Bun.getS3ClientConstructor);
    pub const s3 = toJSLazyPropertyCallback(Bun.getS3DefaultClient);
    pub const ValkeyClient = toJSLazyPropertyCallback(Bun.getValkeyClientConstructor);
    pub const valkey = toJSLazyPropertyCallback(Bun.getValkeyDefaultClient);
    pub const Terminal = toJSLazyPropertyCallback(Bun.getTerminalConstructor);
    // --- Lazy property callbacks ---

    // --- Getters ---
    pub const main = Bun.getMain;
    // --- Getters ---

    // --- Setters ---
    pub const setMain = Bun.setMain;
    // --- Setters ---

    static fn lazyPropertyCallbackName(comptime baseName: auto): [:0]const u8 {
        return "BunObject_lazyPropCb_" ++ baseName;
    }

    static fn callbackName(comptime baseName: auto): [:0]const u8 {
        return "BunObject_callback_" ++ baseName;
    }

    const toJSCallback = jsc.toJSHostFn;

    const LazyPropertyCallback = fn (*jsc.JSGlobalObject, *jsc.JSObject) callconv(jsc.conv) JSValue;

    static fn toJSLazyPropertyCallback(comptime wrapped: auto): LazyPropertyCallback {
        return struct {
            @callconv(jsc.conv)
            fn callback(global: *jsc.JSGlobalObject, object: *jsc.JSObject): JSValue {
                return bun.jsc.toJSHostCall(global, @src(), wrapped, .{ global, object });
            }
        }.callback;
    }

    static fn exportAll() {
        if (!@inComptime()) {
            @compileError("Must be comptime");
        }

        // --- Lazy property callbacks ---
        @export(&@field(BunObject, "Archive"), { name: lazyPropertyCallbackName("Archive") });
        @export(&@field(BunObject, "CryptoHasher"), { name: lazyPropertyCallbackName("CryptoHasher") });
        @export(&@field(BunObject, "CSRF"), { name: lazyPropertyCallbackName("CSRF") });
        @export(&@field(BunObject, "FFI"), { name: lazyPropertyCallbackName("FFI") });
        @export(&@field(BunObject, "FileSystemRouter"), { name: lazyPropertyCallbackName("FileSystemRouter") });
        @export(&@field(BunObject, "MD4"), { name: lazyPropertyCallbackName("MD4") });
        @export(&@field(BunObject, "MD5"), { name: lazyPropertyCallbackName("MD5") });
        @export(&@field(BunObject, "SHA1"), { name: lazyPropertyCallbackName("SHA1") });
        @export(&@field(BunObject, "SHA224"), { name: lazyPropertyCallbackName("SHA224") });
        @export(&@field(BunObject, "SHA256"), { name: lazyPropertyCallbackName("SHA256") });
        @export(&@field(BunObject, "SHA384"), { name: lazyPropertyCallbackName("SHA384") });
        @export(&@field(BunObject, "SHA512"), { name: lazyPropertyCallbackName("SHA512") });
        @export(&@field(BunObject, "SHA512_256"), { name: lazyPropertyCallbackName("SHA512_256") });
        @export(&@field(BunObject, "JSONC"), { name: lazyPropertyCallbackName("JSONC") });
        @export(&@field(BunObject, "markdown"), { name: lazyPropertyCallbackName("markdown") });
        @export(&@field(BunObject, "TOML"), { name: lazyPropertyCallbackName("TOML") });
        @export(&@field(BunObject, "JSON5"), { name: lazyPropertyCallbackName("JSON5") });
        @export(&@field(BunObject, "YAML"), { name: lazyPropertyCallbackName("YAML") });
        @export(&@field(BunObject, "Glob"), { name: lazyPropertyCallbackName("Glob") });
        @export(&@field(BunObject, "Image"), { name: lazyPropertyCallbackName("Image") });
        @export(&@field(BunObject, "Transpiler"), { name: lazyPropertyCallbackName("Transpiler") });
        @export(&@field(BunObject, "argv"), { name: lazyPropertyCallbackName("argv") });
        @export(&@field(BunObject, "cron"), { name: lazyPropertyCallbackName("cron") });
        @export(&@field(BunObject, "cwd"), { name: lazyPropertyCallbackName("cwd") });
        @export(&@field(BunObject, "enableANSIColors"), { name: lazyPropertyCallbackName("enableANSIColors") });
        @export(&@field(BunObject, "hash"), { name: lazyPropertyCallbackName("hash") });
        @export(&@field(BunObject, "inspect"), { name: lazyPropertyCallbackName("inspect") });
        @export(&@field(BunObject, "origin"), { name: lazyPropertyCallbackName("origin") });
        @export(&@field(BunObject, "unsafe"), { name: lazyPropertyCallbackName("unsafe") });
        @export(&@field(BunObject, "semver"), { name: lazyPropertyCallbackName("semver") });
        @export(&@field(BunObject, "embeddedFiles"), { name: lazyPropertyCallbackName("embeddedFiles") });
        @export(&@field(BunObject, "S3Client"), { name: lazyPropertyCallbackName("S3Client") });
        @export(&@field(BunObject, "s3"), { name: lazyPropertyCallbackName("s3") });
        @export(&@field(BunObject, "ValkeyClient"), { name: lazyPropertyCallbackName("ValkeyClient") });
        @export(&@field(BunObject, "valkey"), { name: lazyPropertyCallbackName("valkey") });
        @export(&@field(BunObject, "Terminal"), { name: lazyPropertyCallbackName("Terminal") });
        // --- Lazy property callbacks ---

        // --- Callbacks ---
        @export(&@field(BunObject, "allocUnsafe"), { name: callbackName("allocUnsafe") });
        @export(&@field(BunObject, "build"), { name: callbackName("build") });
        @export(&@field(BunObject, "color"), { name: callbackName("color") });
        @export(&@field(BunObject, "connect"), { name: callbackName("connect") });
        @export(&@field(BunObject, "createParsedShellScript"), { name: callbackName("createParsedShellScript") });
        @export(&@field(BunObject, "createShellInterpreter"), { name: callbackName("createShellInterpreter") });
        @export(&@field(BunObject, "deflateSync"), { name: callbackName("deflateSync") });
        @export(&@field(BunObject, "file"), { name: callbackName("file") });
        @export(&@field(BunObject, "gunzipSync"), { name: callbackName("gunzipSync") });
        @export(&@field(BunObject, "gzipSync"), { name: callbackName("gzipSync") });
        @export(&@field(BunObject, "indexOfLine"), { name: callbackName("indexOfLine") });
        @export(&@field(BunObject, "inflateSync"), { name: callbackName("inflateSync") });
        @export(&@field(BunObject, "jest"), { name: callbackName("jest") });
        @export(&@field(BunObject, "listen"), { name: callbackName("listen") });
        @export(&@field(BunObject, "mmap"), { name: callbackName("mmap") });
        @export(&@field(BunObject, "nanoseconds"), { name: callbackName("nanoseconds") });
        @export(&@field(BunObject, "openInEditor"), { name: callbackName("openInEditor") });
        @export(&@field(BunObject, "registerMacro"), { name: callbackName("registerMacro") });
        @export(&@field(BunObject, "resolve"), { name: callbackName("resolve") });
        @export(&@field(BunObject, "resolveSync"), { name: callbackName("resolveSync") });
        @export(&@field(BunObject, "serve"), { name: callbackName("serve") });
        @export(&@field(BunObject, "sha"), { name: callbackName("sha") });
        @export(&@field(BunObject, "shellEscape"), { name: callbackName("shellEscape") });
        @export(&@field(BunObject, "shrink"), { name: callbackName("shrink") });
        @export(&@field(BunObject, "stringWidth"), { name: callbackName("stringWidth") });
        @export(&@field(BunObject, "sleepSync"), { name: callbackName("sleepSync") });
        @export(&@field(BunObject, "spawn"), { name: callbackName("spawn") });
        @export(&@field(BunObject, "spawnSync"), { name: callbackName("spawnSync") });
        @export(&@field(BunObject, "udpSocket"), { name: callbackName("udpSocket") });
        @export(&@field(BunObject, "which"), { name: callbackName("which") });
        @export(&@field(BunObject, "write"), { name: callbackName("write") });
        @export(&@field(BunObject, "zstdCompressSync"), { name: callbackName("zstdCompressSync") });
        @export(&@field(BunObject, "zstdDecompressSync"), { name: callbackName("zstdDecompressSync") });
        @export(&@field(BunObject, "zstdCompress"), { name: callbackName("zstdCompress") });
        @export(&@field(BunObject, "zstdDecompress"), { name: callbackName("zstdDecompress") });
        // --- Callbacks ---

        // --- LazyProperty initializers ---
        @export(&createBunStdin, { name: "BunObject__createBunStdin" });
        @export(&createBunStderr, { name: "BunObject__createBunStderr" });
        @export(&createBunStdout, { name: "BunObject__createBunStdout" });
        // --- LazyProperty initializers ---

        // --- Getters ---
        @export(&@field(BunObject, "main"), { name: "BunObject_getter_main" });
        // --- Getters ---

        // --- Setters ---
        @export(&@field(BunObject, "setMain"), { name: "BunObject_setter_main" });
        // --- Setters ---
    }
}

pub fn shellEscape(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(1);
    if (arguments.len < 1) {
        return globalThis.throw("shell escape expected at least 1 argument", .{});
    }

    const jsval = arguments.ptr[0];
    const bunstr = jsval.toBunString(globalThis)?;
    if (globalThis.hasException()) return .zero;
    defer bunstr.deref();

    let outbuf = std.array_list.Managed(u8).init(bun.default_allocator);
    defer outbuf.deinit();

    if (bun.shell.needsEscapeBunstr(bunstr)) {
        const result = bun.shell.escapeBunStr(bunstr, &outbuf, true)?;
        if (!result) {
            return globalThis.throw("String has invalid utf-16: {s}", .{ bunstr.byteSlice() });
        }
        let str = bun.String.cloneUTF8(outbuf.items[0..]);
        return str.transferToJS(globalThis);
    }

    return jsval;
}

pub fn braces(global: *jsc.JSGlobalObject, brace_str: bun.String, opts: gen.BracesOptions): bun.JSError!jsc.JSValue {
    const brace_slice = brace_str.toUTF8(bun.default_allocator);
    defer brace_slice.deinit();

    let arena_ = std.heap.ArenaAllocator.init(bun.default_allocator);
    defer arena_.deinit();

    let lexer_output = lexer_output: {
        if (bun.strings.isAllASCII(brace_slice.slice())) {
            break :lexer_output Braces.Lexer.tokenize(arena_.allocator(), brace_slice.slice()) catch |e| {
                return global.throwError(e, "failed to tokenize braces");
            };
        }

        break :lexer_output Braces.NewLexer(.wtf8).tokenize(arena_.allocator(), brace_slice.slice()) catch |e| {
            return global.throwError(e, "failed to tokenize braces");
        };
    };

    const expansion_count = Braces.calculateExpandedAmount(lexer_output.tokens.items[0..]);

    if (opts.tokenize) {
        const str = bun.handleOom(std.fmt.allocPrint(global.bunVM().allocator, "{f}", .{ std.json.fmt(lexer_output.tokens.items[0..], .{}) }));
        defer global.bunVM().allocator.free(str);
        let bun_str = bun.String.fromBytes(str);
        return bun_str.toJS(global);
    }
    if (opts.parse) {
        let parser = Braces.Parser.init(lexer_output.tokens.items[0..], arena_.allocator());
        const ast_node = parser.parse() catch |e| {
            return global.throwError(e, "failed to parse braces");
        };
        const str = bun.handleOom(std.fmt.allocPrint(global.bunVM().allocator, "{f}", .{ std.json.fmt(ast_node, .{}) }));
        defer global.bunVM().allocator.free(str);
        let bun_str = bun.String.fromBytes(str);
        return bun_str.toJS(global);
    }

    if (expansion_count == 0) {
        return bun.String.toJSArray(global, &.{ brace_str });
    }

    let expanded_strings = arena_.allocator().alloc(std.array_list.Managed(u8), expansion_count)?;

    for (i of 0..expansion_count) {
        expanded_strings[i] = std.array_list.Managed(u8).init(arena_.allocator());
    }

    Braces.expand(
        arena_.allocator(),
        lexer_output.tokens.items[0..],
        expanded_strings,
        lexer_output.contains_nested,
    ) catch |e| {
        if (e == error.OutOfMemory) return e;
        if (e == error.UnexpectedToken) return global.throwPretty("Unexpected token while expanding braces", .{});
        return e;
    };

    let out_strings = arena_.allocator().alloc(bun.String, expansion_count)?;
    for (i of 0..expansion_count) {
        out_strings[i] = bun.String.fromBytes(expanded_strings[i].items[0..]);
    }

    return bun.String.toJSArray(global, out_strings[0..]);
}

pub fn which(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments_ = callframe.arguments_old(2);
    const path_buf = bun.path_buffer_pool.get();
    defer bun.path_buffer_pool.put(path_buf);
    let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
    defer arguments.deinit();
    const path_arg_opt = arguments.nextEat();
    if (path_arg_opt == null) {
        return globalThis.throw("which: expected 1 argument, got 0", .{});
    }
    const path_arg = path_arg_opt.?;

    let path_str: ZigString.Slice = ZigString.Slice.empty;
    let bin_str: ZigString.Slice = ZigString.Slice.empty;
    let cwd_str: ZigString.Slice = ZigString.Slice.empty;
    defer {
        path_str.deinit();
        bin_str.deinit();
        cwd_str.deinit();
    }

    if (path_arg.isEmptyOrUndefinedOrNull()) {
        return jsc.JSValue.jsNull();
    }

    bin_str = path_arg.toSlice(globalThis, globalThis.bunVM().allocator)?;
    if (globalThis.hasException()) {
        return .zero;
    }

    if (bin_str.len >= bun.MAX_PATH_BYTES) {
        return globalThis.throw("bin path is too long", .{});
    }

    if (bin_str.len == 0) {
        return jsc.JSValue.jsNull();
    }

    path_str = ZigString.Slice.fromUTF8NeverFree(
        globalThis.bunVM().transpiler.env.get("PATH") ?? "",
    );
    cwd_str = ZigString.Slice.fromUTF8NeverFree(
        globalThis.bunVM().transpiler.fs.top_level_dir,
    );

    if (arg := arguments.nextEat()) {
        if (!arg.isEmptyOrUndefinedOrNull() && arg.isObject()) {
            if (str_ := arg.get(globalThis, "PATH")?) {
                path_str = str_.toSlice(globalThis, globalThis.bunVM().allocator)?;
            }

            if (str_ := arg.get(globalThis, "cwd")?) {
                cwd_str = str_.toSlice(globalThis, globalThis.bunVM().allocator)?;
            }
        }
    }

    if (bin_path := Which.which(
        path_buf,
        path_str.slice(),
        cwd_str.slice(),
        bin_str.slice(),
    )) {
        return ZigString.init(bin_path).withEncoding().toJS(globalThis);
    }

    return jsc.JSValue.jsNull();
}

pub fn inspectTable(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    let args_buf = callframe.argumentsUndef(5);
    let all_arguments = args_buf.mut();
    if (all_arguments[0].isUndefinedOrNull() || !all_arguments[0].isObject())
        return bun.String.empty.toJS(globalThis);

    for (arg of all_arguments) {
        arg.protect();
    }
    defer {
        for (arg of all_arguments) {
            arg.unprotect();
        }
    }

    let arguments = all_arguments[0..];
    const value = arguments[0];

    if (!arguments[1].isArray()) {
        arguments[2] = arguments[1];
        arguments[1] = .js_undefined;
    }

    let formatOptions = ConsoleObject.FormatOptions{
        enable_colors: false,
        add_newline: false,
        flush: false,
        max_depth: 5,
        quote_strings: true,
        ordered_properties: false,
        single_line: true,
    };
    if (arguments[2].isObject()) {
        formatOptions.fromJS(globalThis, arguments[2..])?;
    }

    // very stable memory address
    let array = std.Io.Writer.Allocating.init(bun.default_allocator);
    defer array.deinit();
    const writer = &array.writer;

    const Writer = @TypeOf(writer);
    const properties: JSValue = if (arguments[1].jsType().isArray()) arguments[1] else .js_undefined;
    let table_printer = try ConsoleObject.TablePrinter.init(
        globalThis,
        .Log,
        value,
        properties,
    );
    table_printer.value_formatter.depth = formatOptions.max_depth;
    table_printer.value_formatter.ordered_properties = formatOptions.ordered_properties;
    table_printer.value_formatter.single_line = formatOptions.single_line;

    switch (formatOptions.enable_colors) {
        inline else => |colors| table_printer.printTable(Writer, writer, colors) catch {
            if (!globalThis.hasException())
                return globalThis.throwOutOfMemory();
            return .zero;
        },
    }

    writer.flush() catch |e| {
        if (e == error.WriteFailed) return error.OutOfMemory;
        return e;
    };

    return bun.String.createUTF8ForJS(globalThis, array.written());
}

pub fn inspect(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(4).slice();
    if (arguments.len == 0)
        return bun.String.empty.toJS(globalThis);

    for (arg of arguments) {
        arg.protect();
    }
    defer {
        for (arg of arguments) {
            arg.unprotect();
        }
    }

    let formatOptions = ConsoleObject.FormatOptions{
        enable_colors: false,
        add_newline: false,
        flush: false,
        max_depth: 8,
        quote_strings: true,
        ordered_properties: false,
    };
    if (arguments.len > 1) {
        formatOptions.fromJS(globalThis, arguments[1..])?;
    }

    // very stable memory address
    let array = std.Io.Writer.Allocating.init(bun.default_allocator);
    defer array.deinit();
    const writer = &array.writer;
    // we buffer this because it'll almost always be < 4096
    // when it's under 4096, we want to avoid the dynamic allocation
    try ConsoleObject.format2(
        .Debug,
        globalThis,
        arguments.ptr,
        1,
        writer,
        formatOptions,
    );
    if (globalThis.hasException()) return error.JSError;
    writer.flush() catch return globalThis.throwOutOfMemory();

    // we are going to always clone to keep things simple for now
    // the common case here will be stack-allocated, so it should be fine
    let out = ZigString.init(array.written()).withEncoding();
    const ret = out.toJS(globalThis);

    return ret;
}

export fn Bun__inspect(globalThis: *JSGlobalObject, value: JSValue): bun.String {
    // very stable memory address
    let array = std.Io.Writer.Allocating.init(bun.default_allocator);
    defer array.deinit();
    const writer = &array.writer;

    let formatter = ConsoleObject.Formatter{ globalThis: globalThis };
    defer formatter.deinit();
    writer.print("{f}", .{ value.toFmt(&formatter) }) catch return .empty;
    writer.flush() catch return .empty;
    return bun.String.cloneUTF8(array.written());
}

export fn Bun__inspect_singleline(globalThis: *JSGlobalObject, value: JSValue): bun.String {
    let array = std.Io.Writer.Allocating.init(bun.default_allocator);
    defer array.deinit();
    const writer = &array.writer;
    ConsoleObject.format2(.Debug, globalThis, (&value)[0..1].ptr, 1, writer, {
        enable_colors: false,
        add_newline: false,
        flush: false,
        max_depth: std.math.maxInt(u16),
        quote_strings: true,
        ordered_properties: false,
        single_line: true,
    }) catch return .empty;
    if (globalThis.hasException()) return .empty;
    writer.flush() catch return .empty;
    return bun.String.cloneUTF8(array.written());
}

pub fn getInspect(globalObject: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    const fun = jsc.JSFunction.create(globalObject, "inspect", inspect, 2, .{});
    let str = ZigString.init("nodejs.util.inspect.custom");
    fun.put(globalObject, ZigString.static("custom"), jsc.JSValue.symbolFor(globalObject, &str));
    fun.put(globalObject, ZigString.static("table"), jsc.JSFunction.create(globalObject, "table", inspectTable, 3, .{}));
    return fun;
}

pub fn registerMacro(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments_ = callframe.arguments_old(2);
    const arguments = arguments_.slice();
    if (arguments.len != 2 || !arguments[0].isNumber()) {
        return globalObject.throwInvalidArguments("Internal error registering macros: invalid args", .{});
    }
    const id = arguments[0].toInt32();
    if (id == -1 || id == 0) {
        return globalObject.throwInvalidArguments("Internal error registering macros: invalid id", .{});
    }

    if (!arguments[1].isCell() || !arguments[1].isCallable()) {
        // TODO: add "toTypeOf" helper
        return globalObject.throw("Macro must be a function", .{});
    }

    const get_or_put_result = VirtualMachine.get().macros.getOrPut(id) catch unreachable;
    if (get_or_put_result.found_existing) {
        get_or_put_result.value_ptr.*.?.value().unprotect();
    }

    arguments[1].protect();
    get_or_put_result.value_ptr.* = arguments[1].asObjectRef();

    return .js_undefined;
}

pub fn getCWD(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return ZigString.init(VirtualMachine.get().transpiler.fs.top_level_dir).toJS(globalThis);
}

pub fn getOrigin(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return ZigString.init(VirtualMachine.get().origin.origin).toJS(globalThis);
}

pub fn enableANSIColors(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    * = globalThis;
    return JSValue.jsBoolean(Output.enable_ansi_colors_stdout || Output.enable_ansi_colors_stderr);
}

@callconv(jsc.conv)
fn getMain(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    const vm = globalThis.bunVM();
    // If JS has set it to a custom value, use that one
    if (overridden_main := vm.overridden_main.get()) return overridden_main;

    // Attempt to use the resolved filesystem path
    // This makes `eval('require.main === module')` work when the main module is a symlink.
    // This behavior differs slightly from Node. Node sets the `id` to `.` when the main module is a symlink.
    use_resolved_path: {
        if (vm.main_resolved_path.isEmpty()) {
            // If it's from eval, don't try to resolve it.
            if (strings.hasSuffixComptime(vm.main, "[eval]")) {
                break :use_resolved_path;
            }
            if (strings.hasSuffixComptime(vm.main, "[stdin]")) {
                break :use_resolved_path;
            }

            const fd = bun.sys.openatA(
                if (comptime Environment.isWindows) bun.invalid_fd else bun.FD.cwd(),
                vm.main,

                // Open with the minimum permissions necessary for resolving the file path.
                if (comptime Environment.isLinux) bun.O.PATH else bun.O.RDONLY,

                0,
            ).unwrap() catch break :use_resolved_path;

            defer fd.close();
            if (comptime Environment.isWindows) {
                let wpath: bun.WPathBuffer = undefined;
                const fdpath = bun.getFdPathW(fd, &wpath) catch break :use_resolved_path;
                vm.main_resolved_path = bun.String.cloneUTF16(fdpath);
            } else {
                let path: bun.PathBuffer = undefined;
                const fdpath = bun.getFdPath(fd, &path) catch break :use_resolved_path;

                // Bun.main === otherId will be compared many times, so let's try to create an atom string if we can.
                if (atom := bun.String.tryCreateAtom(fdpath)) {
                    vm.main_resolved_path = atom;
                } else {
                    vm.main_resolved_path = bun.String.cloneUTF8(fdpath);
                }
            }
        }

        return vm.main_resolved_path.toJS(globalThis) catch .zero;
    }

    return ZigString.init(vm.main).toJS(globalThis);
}

@callconv(jsc.conv)
fn setMain(global_this: *jsc.JSGlobalObject, new_value: JSValue): bool {
    global_this.bunVM().overridden_main.set(global_this, new_value);
    return true;
}

pub fn getArgv(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return node.process.getArgv(globalThis);
}

pub fn openInEditor(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
    let edit = &VirtualMachine.get().rareData().editor_context;
    const args = callframe.arguments_old(4);
    let arguments = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), args.slice());
    defer arguments.deinit();
    let path: string = "";
    let editor_choice: Editor? = null;
    let line: string? = null;
    let column: string? = null;

    if (file_path_ := arguments.nextEat()) {
        path = ((file_path_.toSlice(globalThis, arguments.@"arena".allocator()))?).slice();
    }

    if (opts := arguments.nextEat()) {
        if (!opts.isUndefinedOrNull()) {
            if (editor_val := opts.getTruthy(globalThis, "editor")?) {
                let sliced = editor_val.toSlice(globalThis, arguments.@"arena".allocator())?;
                const prev_name = edit.name;

                if (!strings.eqlLong(prev_name, sliced.slice(), true)) {
                    const prev = edit.*;
                    edit.name = sliced.slice();
                    edit.detectEditor(VirtualMachine.get().transpiler.env);
                    editor_choice = edit.editor;
                    if (editor_choice == null) {
                        edit.* = prev;
                        return globalThis.throw("Could not find editor \"{s}\"", .{ sliced.slice() });
                    } else if (edit.name.ptr == edit.path.ptr) {
                        edit.name = arguments.@"arena".allocator().dupe(u8, edit.path) catch unreachable;
                        edit.path = edit.path;
                    }
                }
            }

            if (line_ := opts.getTruthy(globalThis, "line")?) {
                line = ((line_.toSlice(globalThis, arguments.@"arena".allocator()))?).slice();
            }

            if (column_ := opts.getTruthy(globalThis, "column")?) {
                column = ((column_.toSlice(globalThis, arguments.@"arena".allocator()))?).slice();
            }
        }
    }

    let editor_opt = editor_choice ?? edit.editor;
    if (editor_opt == null) {
        edit.autoDetectEditor(VirtualMachine.get().transpiler.env);
        if (edit.editor == null) {
            return globalThis.throw("Failed to auto-detect editor", .{});
        }
        editor_opt = edit.editor;
    }
    const editor = editor_opt.?;

    if (path.len == 0) {
        return globalThis.throw("No file path specified", .{});
    }

    editor.open(edit.path, path, line, column, arguments.@"arena".allocator()) catch |e| {
        return globalThis.throw("Opening editor failed {s}", .{ @errorName(e) });
    };

    return .js_undefined;
}

pub fn getPublicPath(to: string, origin: URL, comptime Writer: type, writer: Writer) {
    return getPublicPathWithAssetPrefix(
        to,
        VirtualMachine.get().transpiler.fs.top_level_dir,
        origin,
        "",
        comptime Writer,
        writer,
        .loose,
    );
}

pub fn getPublicPathWithAssetPrefix(
    to: string,
    dir: string,
    origin: URL,
    asset_prefix: string,
    comptime Writer: type,
    writer: Writer,
    comptime platform: bun.path.Platform,
) {
    const relative_path = if (strings.hasPrefix(to, dir))
        strings.withoutTrailingSlash(to[dir.len..])
    else
        VirtualMachine.get().transpiler.fs.relativePlatform(dir, to, platform);
    if (origin.isAbsolute()) {
        if (strings.hasPrefix(relative_path, "..") || strings.hasPrefix(relative_path, "./")) {
            writer.writeAll(origin.origin) catch return;
            writer.writeAll("/abs:") catch return;
            if (std.fs.path.isAbsolute(to)) {
                writer.writeAll(to) catch return;
            } else {
                writer.writeAll(VirtualMachine.get().transpiler.fs.abs(&.{to})) catch return;
            }
        } else {
            origin.joinWrite(
                Writer,
                writer,
                asset_prefix,
                "",
                relative_path,
                "",
            ) catch return;
        }
    } else {
        writer.writeAll(std.mem.trimLeft(u8, relative_path, "/")) catch unreachable;
    }
}

pub fn sleepSync(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(1);

    // Expect at least one argument.  We allow more than one but ignore them; this
    //  is useful for supporting things like `[1, 2].map(sleepSync)`
    if (arguments.len < 1) {
        return globalObject.throwNotEnoughArguments("sleepSync", 1, 0);
    }
    const arg = arguments.slice()[0];

    // The argument must be a number
    if (!arg.isNumber()) {
        return globalObject.throwInvalidArgumentType("sleepSync", "milliseconds", "number");
    }

    //NOTE: if argument is > max(i32) then it will be truncated
    const milliseconds = arg.coerce(i32, globalObject)?;
    if (milliseconds < 0) {
        return globalObject.throwInvalidArguments("argument to sleepSync must not be negative, got {d}", .{ milliseconds });
    }

    std.Thread.sleep(@as(u64, i32(milliseconds)) * std.time.ns_per_ms);
    return .js_undefined;
}

@callconv("c")
export fn Bun__gc(vm: *jsc.VirtualMachine, sync: bool): usize {
    return vm.garbageCollect(sync);
}

pub const gc = Bun__gc;

pub fn shrink(globalObject: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    globalObject.vm().shrinkFootprint();
    return .js_undefined;
}

fn doResolve(globalThis: *jsc.JSGlobalObject, arguments: []const JSValue): bun.JSError!jsc.JSValue {
    let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
    defer args.deinit();
    const specifier_opt = args.protectEatNext();
    if (specifier_opt == null) {
        return globalThis.throwInvalidArguments("Expected a specifier and a from path", .{});
    }
    const specifier = specifier_opt.?;

    if (specifier.isUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("specifier must be a string", .{});
    }

    const from_opt = args.protectEatNext();
    if (from_opt == null) {
        return globalThis.throwInvalidArguments("Expected a from path", .{});
    }
    const from_val = from_opt.?;

    if (from_val.isUndefinedOrNull()) {
        return globalThis.throwInvalidArguments("from must be a string", .{});
    }

    let is_esm = true;
    if (next := args.nextEat()) {
        if (next.isBoolean()) {
            is_esm = next.toBoolean();
        } else {
            return globalThis.throwInvalidArguments("esm must be a boolean", .{});
        }
    }

    const specifier_str = specifier.toBunString(globalThis)?;
    defer specifier_str.deref();
    const from_str = from_val.toBunString(globalThis)?;
    defer from_str.deref();
    return doResolveWithArgs(
        globalThis,
        specifier_str,
        from_str,
        is_esm,
        false,
        false,
    );
}

fn doResolveWithArgs(ctx: *jsc.JSGlobalObject, specifier: bun.String, from_str: bun.String, is_esm: bool, comptime is_file_path: bool, is_user_require_resolve: bool): bun.JSError!jsc.JSValue {
    let errorable: ErrorableString = undefined;
    let query_string = bun.String.empty;
    defer query_string.deref();

    const specifier_decoded = if (specifier.hasPrefixComptime("file://"))
        bun.jsc.URL.pathFromFileURL(specifier)
    else
        specifier.dupeRef();
    defer specifier_decoded.deref();

    try VirtualMachine.resolveMaybeNeedsTrailingSlash(
        &errorable,
        ctx,
        specifier_decoded,
        from_str,
        &query_string,
        is_esm,
        is_file_path,
        is_user_require_resolve,
    );

    if (!errorable.success) {
        return ctx.throwValue(errorable.result.err.value);
    }
    defer errorable.result.value.deref();

    if (!query_string.isEmpty()) {
        let stack = std.heap.stackFallback(1024, ctx.allocator());
        const allocator = stack.get();
        let arraylist = std.array_list.Managed(u8).initCapacity(allocator, 1024) catch unreachable;
        defer arraylist.deinit();
        try arraylist.writer().print("{f}{f}", .{ 
            errorable.result.value,
            query_string,
        });

        return ZigString.initUTF8(arraylist.items).toJS(ctx);
    }

    return errorable.result.value.toJS(ctx);
}

pub fn resolveSync(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return doResolve(globalObject, callframe.arguments())?;
}

pub fn resolve(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(3);
    const value = doResolve(globalObject, arguments.slice()) catch |e| {
        const err_val = globalObject.takeError(e);
        return jsc.JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(globalObject, err_val);
    };
    return jsc.JSPromise.resolvedPromiseValue(globalObject, value);
}

export fn Bun__resolve(global: *JSGlobalObject, specifier: JSValue, source: JSValue, is_esm: bool): jsc.JSValue {
    const specifier_str = specifier.toBunString(global) catch return .zero;
    defer specifier_str.deref();

    const source_str = source.toBunString(global) catch return .zero;
    defer source_str.deref();

    const value = doResolveWithArgs(global, specifier_str, source_str, is_esm, true, false) catch {
        const e = global.tryTakeException().?;
        return jsc.JSPromise.dangerouslyCreateRejectedPromiseValueWithoutNotifyingVM(global, e);
    };

    return jsc.JSPromise.resolvedPromiseValue(global, value);
}

export fn Bun__resolveSync(global: *JSGlobalObject, specifier: JSValue, source: JSValue, is_esm: bool, is_user_require_resolve: bool): jsc.JSValue {
    const specifier_str = specifier.toBunString(global) catch return .zero;
    defer specifier_str.deref();

    if (specifier_str.length() == 0) {
        return global.ERR(.INVALID_ARG_VALUE, "The argument 'id' must be a non-empty string. Received ''", .{}).throw() catch .zero;
    }

    const source_str = source.toBunString(global) catch return .zero;
    defer source_str.deref();

    return jsc.toJSHostCall(global, @src(), doResolveWithArgs, .{ global, specifier_str, source_str, is_esm, true, is_user_require_resolve });
}

export fn Bun__resolveSyncWithPaths(
    global: *JSGlobalObject,
    specifier: JSValue,
    source: JSValue,
    is_esm: bool,
    is_user_require_resolve: bool,
    paths_ptr: [*]const bun.String?,
    paths_len: usize,
): jsc.JSValue {
    const paths: []const bun.String = if (paths_len == 0) [] else paths_ptr.?[0..paths_len];

    const specifier_str = specifier.toBunString(global) catch return .zero;
    defer specifier_str.deref();

    if (specifier_str.length() == 0) {
        return global.ERR(.INVALID_ARG_VALUE, "The argument 'id' must be a non-empty string. Received ''", .{}).throw() catch .zero;
    }

    const source_str = source.toBunString(global) catch return .zero;
    defer source_str.deref();

    const bun_vm = global.bunVM();
    bun.assert(bun_vm.transpiler.resolver.custom_dir_paths == null);
    bun_vm.transpiler.resolver.custom_dir_paths = paths;
    defer bun_vm.transpiler.resolver.custom_dir_paths = null;

    return jsc.toJSHostCall(global, @src(), doResolveWithArgs, .{ global, specifier_str, source_str, is_esm, true, is_user_require_resolve });
}

export fn Bun__resolveSyncWithStrings(global: *JSGlobalObject, specifier: *bun.String, source: *bun.String, is_esm: bool): jsc.JSValue {
    Output.scoped(.importMetaResolve, .visible)("source: {f}, specifier: {f}", .{ source.*, specifier.* });
    return jsc.toJSHostCall(global, @src(), doResolveWithArgs, .{ global, specifier.*, source.*, is_esm, true, false });
}

export fn Bun__resolveSyncWithSource(global: *JSGlobalObject, specifier: JSValue, source: *bun.String, is_esm: bool, is_user_require_resolve: bool): jsc.JSValue {
    const specifier_str = specifier.toBunString(global) catch return .zero;
    defer specifier_str.deref();
    if (specifier_str.length() == 0) {
        return global.ERR(.INVALID_ARG_VALUE, "The argument 'id' must be a non-empty string. Received ''", .{}).throw() catch .zero;
    }
    return jsc.toJSHostCall(global, @src(), doResolveWithArgs, .{ global, specifier_str, source.*, is_esm, true, is_user_require_resolve });
}

pub fn indexOfLine(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments_ = callframe.arguments_old(2);
    const arguments = arguments_.slice();
    if (arguments.len == 0) {
        return jsc.JSValue.jsNumberFromInt32(-1);
    }

    const buffer_opt = arguments[0].asArrayBuffer(globalThis);
    if (buffer_opt == null) {
        return jsc.JSValue.jsNumberFromInt32(-1);
    }
    let buffer = buffer_opt.?;

    let offset: usize = 0;
    if (arguments.len > 1) {
        const offset_value = arguments[1].coerce(i64, globalThis)?;
        offset = @as(usize, @intCast(@max(offset_value, 0)));
    }

    const bytes = buffer.byteSlice();
    let current_offset = offset;
    const end = @as(u32, @truncate(bytes.len));

    while (current_offset < end) {
        if (i := strings.indexOfNewlineOrNonASCII(bytes, @as(u32, @truncate(current_offset)))) {
            const byte = bytes[i];
            if (byte > 0x7F) {
                current_offset += @max(strings.wtf8ByteSequenceLength(byte), 1);
                continue;
            }

            if (byte == '\n') {
                return jsc.JSValue.jsNumber(i);
            }

            current_offset = i + 1;
        } else {
            break;
        }
    }

    return jsc.JSValue.jsNumberFromInt32(-1);
}


pub fn nanoseconds(globalThis: *jsc.JSGlobalObject, _: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const ns = globalThis.bunVM().origin_timer.read();
    return jsc.JSValue.jsNumberFromUint64(ns);
}

pub fn serve(globalObject: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(2).slice();
    let config: jsc.API.ServerConfig = brk: {
        let args = jsc.CallFrame.ArgumentsSlice.init(globalObject.bunVM(), arguments);
        let config: jsc.API.ServerConfig = {};

        try jsc.API.ServerConfig.fromJS(
            globalObject,
            &config,
            &args,
            {
                allow_bake_config: bun.FeatureFlags.bake(),
                is_fetch_required: true,
                has_user_routes: false,
            },
        );

        if (globalObject.hasException()) {
            config.deinit();
            return .zero;
        }

        break :brk config;
    };

    const vm = globalObject.bunVM();

    if (config.allow_hot) {
        if (hot := vm.hotMap()) {
            if (config.id.len == 0) {
                config.id = config.computeID(globalObject.allocator());
            }

            if (entry := hot.getEntry(config.id)) {
                const tag_val = entry.tag();
                const TagType = @TypeOf(tag_val);
                switch (tag_val) {
                    @field(TagType, @typeName(jsc.API.HTTPServer)) => {
                        let server: *jsc.API.HTTPServer = entry.@"as"(jsc.API.HTTPServer);
                        server.onReloadFromZig(&config, globalObject);
                        return server.js_value.tryGet() ?? .js_undefined;
                    },
                    @field(TagType, @typeName(jsc.API.DebugHTTPServer)) => {
                        let server: *jsc.API.DebugHTTPServer = entry.@"as"(jsc.API.DebugHTTPServer);
                        server.onReloadFromZig(&config, globalObject);
                        return server.js_value.tryGet() ?? .js_undefined;
                    },
                    @field(TagType, @typeName(jsc.API.DebugHTTPSServer)) => {
                        let server: *jsc.API.DebugHTTPSServer = entry.@"as"(jsc.API.DebugHTTPSServer);
                        server.onReloadFromZig(&config, globalObject);
                        return server.js_value.tryGet() ?? .js_undefined;
                    },
                    @field(TagType, @typeName(jsc.API.HTTPSServer)) => {
                        let server: *jsc.API.HTTPSServer = entry.@"as"(jsc.API.HTTPSServer);
                        server.onReloadFromZig(&config, globalObject);
                        return server.js_value.tryGet() ?? .js_undefined;
                    },
                    else => {},
                }
            }
        }
    }

    switch (config.ssl_config != null) {
        inline else => |has_ssl_config| {
            switch (config.isDevelopment()) {
                inline else => |development| {
                    const ServerType = comptime switch (development) {
                        true => switch (has_ssl_config) {
                            true => jsc.API.DebugHTTPSServer,
                            false => jsc.API.DebugHTTPServer,
                        },
                        false => switch (has_ssl_config) {
                            true => jsc.API.HTTPSServer,
                            false => jsc.API.HTTPServer,
                        },
                    };

                    let server = ServerType.init(&config, globalObject)?;
                    if (globalObject.hasException()) {
                        return .zero;
                    }
                    const route_list_object = server.listen();
                    if (globalObject.hasException()) {
                        return .zero;
                    }
                    const obj = server.toJS(globalObject);
                    if (route_list_object != .zero) {
                        ServerType.js.routeListSetCached(obj, globalObject, route_list_object);
                    }
                    server.js_value.setStrong(obj, globalObject);

                    if (config.allow_hot) {
                        if (hot := globalObject.bunVM().hotMap()) {
                            hot.insert(config.id, server);
                        }
                    }

                    if (*debugger := vm.debugger) {
                        debugger.http_server_agent.notifyServerStarted(
                            jsc.API.AnyServer.@"from"(server),
                        );
                        debugger.http_server_agent.notifyServerRoutesUpdated(
                            jsc.API.AnyServer.@"from"(server),
                        ) catch |e| bun.handleOom(e);
                    }

                    return obj;
                },
            }
        },
    }
}

@callconv("c")
pub export fn Bun__escapeHTML16(globalObject: *jsc.JSGlobalObject, input_value: JSValue, ptr: [*]const u16, len: usize): JSValue {
    assert(len > 0);
    const input_slice = ptr[0..len];
    const escaped = strings.escapeHTMLForUTF16Input(globalObject.bunVM().allocator, input_slice) catch {
        return globalObject.throwValue(ZigString.init("Out of memory").toErrorInstance(globalObject)) catch return .zero;
    };

    switch (escaped) {
        .static => |val| {
            return ZigString.init(val).toJS(globalObject);
        },
        .original => {
            return input_value;
        },
        .allocated => |escaped_html| {
            return ZigString.from16(escaped_html.ptr, escaped_html.len).toExternalValue(globalObject);
        },
    }
}

@callconv("c")
pub export fn Bun__escapeHTML8(globalObject: *jsc.JSGlobalObject, input_value: JSValue, ptr: [*]const u8, len: usize): JSValue {
    assert(len > 0);

    const input_slice = ptr[0..len];
    let stack_allocator = std.heap.stackFallback(256, globalObject.bunVM().allocator);
    const allocator = if (input_slice.len <= 32) stack_allocator.get() else stack_allocator.fallback_allocator;

    const escaped = strings.escapeHTMLForLatin1Input(allocator, input_slice) catch {
        return globalObject.throwValue(ZigString.init("Out of memory").toErrorInstance(globalObject)) catch return .zero;
    };

    switch (escaped) {
        .static => |val| {
            return ZigString.init(val).toJS(globalObject);
        },
        .original => {
            return input_value;
        },
        .allocated => |escaped_html| {
            if (comptime Environment.allow_assert) {
                // the output should always be longer than the input
                assert(escaped_html.len > input_slice.len);

                // assert we do not allocate a new string unnecessarily
                assert(
                    !std.mem.eql(
                        u8,
                        input_slice,
                        escaped_html,
                    ),
                );
            }

            if (input_slice.len <= 32) {
                const zig_str = ZigString.init(escaped_html);
                const out = zig_str.toAtomicValue(globalObject);
                return out;
            }

            return ZigString.init(escaped_html).toExternalValue(globalObject);
        },
    }
}

pub fn allocUnsafe(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    const arguments = callframe.arguments_old(1);
    const size = arguments.ptr[0];
    if (!size.isUInt32AsAnyInt()) {
        return globalThis.throwInvalidArguments("Expected a positive number", .{});
    }
    return jsc.JSValue.createUninitializedUint8Array(globalThis, size.toUInt64NoTruncate());
}

pub fn mmapFile(globalThis: *jsc.JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    if (comptime Environment.isWindows) {
        return globalThis.throwTODO("mmapFile is not supported on Windows");
    }

    const arguments_ = callframe.arguments_old(2);
    let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments_.slice());
    defer args.deinit();

    let buf: bun.PathBuffer = undefined;
    const path = brk: {
        if (path := args.nextEat()) {
            if (path.isString()) {
                const path_str = path.toSlice(globalThis, args.@"arena".allocator())?;
                if (path_str.len > bun.MAX_PATH_BYTES) {
                    return globalThis.throwInvalidArguments("Path too long", .{});
                }
                const paths = &.{path_str.slice()};
                break :brk bun.path.joinAbsStringBuf(bun.fs.FileSystem.instance.top_level_dir, &buf, paths, .auto);
            }
        }
        return globalThis.throwInvalidArguments("Expected a path", .{});
    };

    buf[path.len] = 0;

    const buf_z: [:0]const u8 = buf[0..path.len :0];

    let flags: std.c.MAP = { TYPE: .SHARED };

    // Conforming applications must specify either MAP_PRIVATE or MAP_SHARED.
    let offset: usize = 0;
    let map_size: usize? = null;

    if (opts := args.nextEat()) {
        if (opts.isObject()) {
            flags.TYPE = if ((try opts.getBooleanLoose(globalThis, "shared")) ?? true)
                .SHARED
            else
                .PRIVATE;

            if (@hasField(std.c.MAP, "SYNC")) {
                if ((try opts.getBooleanLoose(globalThis, "sync")) ?? false) {
                    flags.TYPE = .SHARED_VALIDATE;
                    flags.SYNC = true;
                }
            }

            if (value := opts.get(globalThis, "size")?) {
                const size_value = value.coerceToInt64(globalThis)?;
                if (size_value < 0) {
                    return globalThis.throwInvalidArguments("size must be a non-negative integer", .{});
                }
                map_size = @as(usize, @intCast(size_value));
            }

            if (value := opts.get(globalThis, "offset")?) {
                const offset_value = value.coerceToInt64(globalThis)?;
                if (offset_value < 0) {
                    return globalThis.throwInvalidArguments("offset must be a non-negative integer", .{});
                }
                offset = @as(usize, @intCast(offset_value));
                offset = std.mem.alignBackwardAnyAlign(usize, offset, std.heap.pageSize());
            }
        } else if (!opts.isUndefinedOrNull()) {
            return globalThis.throwInvalidArguments("Expected options to be an object", .{});
        }
    }

    const map = switch (bun.sys.mmapFile(buf_z, flags, map_size, offset)) {
        .result => |map| map,

        .err => |e| {
            return globalThis.throwValue(try e.toJS(globalThis));
        },
    };

    return bun.jsc.array_buffer.makeTypedArrayWithBytesNoCopy(globalThis, .TypeUint8, map.ptr, map.len, mmapDeallocator, @ptrFromInt(map.len));
}

pub fn getTranspilerConstructor(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return jsc.API.JSTranspiler.js.getConstructor(globalThis);
}

pub fn getFileSystemRouter(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return jsc.API.FileSystemRouter.js.getConstructor(globalThis);
}

pub fn getHashObject(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return HashObject.create(globalThis);
}

pub fn getJSONCObject(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return JSONCObject.create(globalThis);
}
pub fn getMarkdownObject(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return MarkdownObject.create(globalThis);
}
pub fn getTOMLObject(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return TOMLObject.create(globalThis);
}

pub fn getJSON5Object(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return JSON5Object.create(globalThis);
}

pub fn getYAMLObject(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return YAMLObject.create(globalThis);
}

pub fn getArchiveConstructor(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return jsc.API.Archive.js.getConstructor(globalThis);
}

pub fn getGlobConstructor(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return jsc.API.Glob.js.getConstructor(globalThis);
}

pub fn getImageConstructor(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return jsc.API.Image.js.getConstructor(globalThis);
}

pub fn getS3ClientConstructor(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return jsc.WebCore.S3Client.js.getConstructor(globalThis);
}

pub fn getS3DefaultClient(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return globalThis.bunVM().rareData().s3DefaultClient(globalThis);
}

pub fn getTLSDefaultCiphers(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return globalThis.bunVM().rareData().tlsDefaultCiphers();
}

pub fn setTLSDefaultCiphers(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject, ciphers: jsc.JSValue): jsc.JSValue {
    globalThis.bunVM().rareData().setTLSDefaultCiphers(ciphers);
    return .js_undefined;
}

pub fn getValkeyDefaultClient(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    let valkey = jsc.API.Valkey.createNoJsNoPubsub(globalThis, &.{ .js_undefined }) catch |e| {
        if (e != error.JSError) {
            * = globalThis.throwError(e, "Failed to create Redis client") catch {};
            return .zero;
        }
        return .zero;
    };

    const as_js = valkey.toJS(globalThis);

    valkey.this_value = jsc.JSRef.initWeak(as_js);
    valkey._subscription_ctx = SubscriptionCtx.init(valkey) catch |e| {
        if (e != error.JSError) {
            * = globalThis.throwError(e, "Failed to create Redis client") catch {};
            return .zero;
        }
        return .zero;
    };

    return as_js;
}

pub fn getValkeyClientConstructor(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return jsc.API.Valkey.js.getConstructor(globalThis);
}

pub fn getTerminalConstructor(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return api.Terminal.js.getConstructor(globalThis);
}

pub fn getEmbeddedFiles(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): bun.JSError!jsc.JSValue {
    const vm = globalThis.bunVM();
    const graph_opt = vm.standalone_module_graph;
    if (graph_opt == null) {
        return (jsc.JSValue.createEmptyArray(globalThis, 0))?;
    }
    const graph = graph_opt.?;

    const unsorted_files = graph.files.values();
    let sort_indices = bun.handleOom(std.array_list.Managed(u32).initCapacity(bun.default_allocator, unsorted_files.len));
    defer sort_indices.deinit();
    for (index of 0..unsorted_files.len) {
        // Some % of people using `bun build --compile` want to obscure the source code
        // We don't really do that right now, but exposing the output source
        // code here as an easily accessible Blob is even worse for them.
        // So let's omit any source code files from the list.
        if (!unsorted_files[index].appearsInEmbeddedFilesArray()) continue;
        sort_indices.appendAssumeCapacity(@as(u32, @intCast(index)));
    }

    let i: u32 = 0;
    let array = jsc.JSValue.createEmptyArray(globalThis, sort_indices.items.len)?;
    std.mem.sort(u32, sort_indices.items, unsorted_files, bun.StandaloneModuleGraph.File.lessThanByIndex);
    for (index of sort_indices.items) {
        const file = &unsorted_files[index];
        // We call .dupe() on this to ensure that we don't return a blob that might get freed later.
        const input_blob = file.blob(globalThis);
        const blob = jsc.WebCore.Blob.new(input_blob.dupeWithContentType(true));
        blob.name = input_blob.name.dupeRef();
        array.putIndex(globalThis, i, blob.toJS(globalThis))?;
        i += 1;
    }

    return array;
}

pub fn getSemver(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return SemverObject.create(globalThis);
}

pub fn getUnsafe(globalThis: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return UnsafeObject.create(globalThis);
}

pub fn stringWidth(globalObject: *jsc.JSGlobalObject, callFrame: *jsc.CallFrame): bun.JSError!jsc.JSValue {
    return bun.String.jsGetStringWidth(globalObject, callFrame);
}

/// EnvironmentVariables is runtime defined.
/// Also, you can't iterate over process.env normally since it only exists at build-time otherwise
pub fn getCSRFObject(globalObject: *jsc.JSGlobalObject, _: *jsc.JSObject): jsc.JSValue {
    return CSRFObject.create(globalObject);
}

// This is aliased to Bun.env
pub struct EnvironmentVariables {
    pub export fn Bun__getEnvCount(globalObject: *jsc.JSGlobalObject, ptr: *[*][]const u8): usize {
        const bunVM = globalObject.bunVM();
        ptr.* = bunVM.transpiler.env.map.map.keys().ptr;
        return bunVM.transpiler.env.map.map.unmanaged.entries.len;
    }

    pub export fn Bun__getEnvKey(ptr: [*][]const u8, i: usize, data_ptr: *[*]const u8): usize {
        const item = ptr[i];
        data_ptr.* = item.ptr;
        return item.len;
    }

    pub export fn Bun__getEnvValue(globalObject: *jsc.JSGlobalObject, name: *ZigString, value: *ZigString): bool {
        if (val := getEnvValue(globalObject, name.*)) {
            value.* = val;
            return true;
        }

        return false;
    }

    /// BunString variant of Bun__getEnvValue. The returned value borrows from
    /// the env map; caller must copy before the map can mutate.
    pub export fn Bun__getEnvValueBunString(globalObject: *jsc.JSGlobalObject, name: *bun.String, value: *bun.String): bool {
        const vm = globalObject.bunVM();
        let name_slice = name.toUTF8(bun.default_allocator);
        defer name_slice.deinit();
        const val_opt = vm.transpiler.env.get(name_slice.slice());
        if (val_opt == null) return false;
        const val = val_opt.?;
        value.* = bun.String.borrowUTF8(val);
        return true;
    }

    /// Sync a process.env write back to the Zig-side env map so that Zig
    /// consumers (e.g. fetch's proxy resolution via env.getHttpProxyFor)
    /// observe the updated value. Used by custom setters for proxy-related
    /// env vars (HTTP_PROXY, HTTPS_PROXY, NO_PROXY and lowercase variants).
    ///
    /// Values are ref-counted in RareData.proxy_env_storage so that
    /// worker_threads share the parent's strings (refcount bumped at spawn)
    /// rather than cloning. A worker only allocates its own value if it
    /// writes to that var. Parent deref'ing on overwrite won't free the
    /// bytes while a worker still holds a ref.
    pub export fn Bun__setEnvValue(globalObject: *jsc.JSGlobalObject, name: *bun.String, value: *bun.String) {
        const vm = globalObject.bunVM();
        let name_slice = name.toUTF8(bun.default_allocator);
        defer name_slice.deinit();

        const storage = &vm.proxy_env_storage;

        // Synchronize the slot swap + env.map.put against a concurrently
        // spawning worker's cloneFrom + env.map.cloneWithAllocator. Without
        // this, the worker could load the slot pointer between our deref
        // (refcount → 0 → free) and the null write below, then call ref()
        // on freed memory.
        storage.lock.lock();
        defer storage.lock.unlock();

        const slot_opt = storage.slot(name_slice.slice());
        if (slot_opt == null) return;
        const slot = slot_opt.?;

        // Deref our previous value. If a worker still holds a ref, the
        // bytes stay alive; if not, they're freed now.
        if (old := slot.ptr.*) {
            old.deref();
            slot.ptr.* = null;
        }

        if (value.isEmpty()) {
            // Store a static empty string rather than removing, so that
            // process.env.X reads back as "" (Node.js semantics) instead
            // of undefined. isNoProxy treats empty strings the same as
            // absent — no bypass.
            bun.handleOom(vm.transpiler.env.map.put(slot.key, ""));
            return;
        }

        let value_slice = value.toUTF8(bun.default_allocator);
        defer value_slice.deinit();
        const new_val = jsc.RareData.RefCountedEnvValue.create(value_slice.slice());
        slot.ptr.* = new_val;
        // slot.key is a static-lifetime string literal (the struct field
        // name); value bytes live in the ref-counted wrapper. map.put
        // stores both slice headers without duping.
        bun.handleOom(vm.transpiler.env.map.put(slot.key, new_val.bytes));
    }

    static fn getEnvNames(globalObject: *jsc.JSGlobalObject, names: []ZigString): usize {
        let vm = globalObject.bunVM();
        const keys = vm.transpiler.env.map.map.keys();
        const len = @min(names.len, keys.len);
        for (i of 0..len) {
            names[i] = ZigString.initUTF8(keys[i]);
        }
        return len;
    }

    static fn getEnvValue(globalObject: *jsc.JSGlobalObject, name: ZigString): ZigString? {
        let vm = globalObject.bunVM();
        let sliced = name.toSlice(vm.allocator);
        defer sliced.deinit();
        const value_opt = vm.transpiler.env.get(sliced.slice());
        if (value_opt == null) return null;
        const value = value_opt.?;
        return ZigString.initUTF8(value);
    }
}

export fn Bun__reportError(globalObject: *JSGlobalObject, e: jsc.JSValue) {
    * = jsc.VirtualMachine.get().uncaughtException(globalObject, e, false);
}

pub struct JSZlib {
    export fn reader_deallocator(_: *!?, ctx: *!?) {
        let reader: *zlib.ZlibReaderArrayList = bun.cast(*zlib.ZlibReaderArrayList, ctx.?);
        reader.list.deinit(reader.allocator);
        reader.deinit();
    }
    export fn global_deallocator(_: *!?, ctx: *!?) {
        bun.allocators.freeWithoutSize(ctx);
    }
    export fn compressor_deallocator(_: *!?, ctx: *!?) {
        let compressor: *zlib.ZlibCompressorArrayList = bun.cast(*zlib.ZlibCompressorArrayList, ctx.?);
        compressor.list.deinit(compressor.allocator);
        compressor.deinit();
    }

    const Library = enum {
        zlib,
        libdeflate,

        pub const map = bun.ComptimeEnumMap(Library);
    };

    // This has to be `inline` due to the callframe.
    inline fn getOptions(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!ZlibOptionsTuple {
        const arguments = callframe.arguments_old(2).slice();
        const buffer_value: JSValue = if (arguments.len > 0) arguments[0] else .js_undefined;
        const options_val: JSValue? =
            if (arguments.len > 1 && arguments[1].isObject())
                arguments[1]
            else if (arguments.len > 1 && !arguments[1].isUndefined()) {
                return globalThis.throwInvalidArguments("Expected options to be an object", .{});
            } else null;

        if (buffer := jsc.Node.StringOrBuffer.fromJS(globalThis, bun.default_allocator, buffer_value)?) {
            return .{ buffer, options_val };
        }

        return globalThis.throwInvalidArguments("Expected buffer to be a string or buffer", .{});
    }

    static fn gzipSync(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const opts = (getOptions(globalThis, callframe))?;
        const buffer = opts[0];
        const options_val = opts[1];
        defer buffer.deinit();
        return gzipOrDeflateSync(globalThis, buffer, options_val, true);
    }

    static fn inflateSync(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const opts = (getOptions(globalThis, callframe))?;
        const buffer = opts[0];
        const options_val = opts[1];
        defer buffer.deinit();
        return gunzipOrInflateSync(globalThis, buffer, options_val, false);
    }

    static fn deflateSync(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const opts = (getOptions(globalThis, callframe))?;
        const buffer = opts[0];
        const options_val = opts[1];
        defer buffer.deinit();
        return gzipOrDeflateSync(globalThis, buffer, options_val, false);
    }

    static fn gunzipSync(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const opts = (getOptions(globalThis, callframe))?;
        const buffer = opts[0];
        const options_val = opts[1];
        defer buffer.deinit();
        return gunzipOrInflateSync(globalThis, buffer, options_val, true);
    }

    static fn gunzipOrInflateSync(globalThis: *JSGlobalObject, buffer: jsc.Node.StringOrBuffer, options_val_: JSValue?, is_gzip: bool): bun.JSError!JSValue {
        let opts = zlib.Options{
            gzip: is_gzip,
            windowBits: if (is_gzip) 31 else -15,
        };

        let library: Library = .zlib;
        if (options_val := options_val_) {
            if (window := options_val.get(globalThis, "windowBits")?) {
                opts.windowBits = window.coerce(i32, globalThis)?;
                library = .zlib;
            }

            if (level := options_val.get(globalThis, "level")?) {
                opts.level = level.coerce(i32, globalThis)?;
            }

            if (memLevel := options_val.get(globalThis, "memLevel")?) {
                opts.memLevel = memLevel.coerce(i32, globalThis)?;
                library = .zlib;
            }

            if (strategy := options_val.get(globalThis, "strategy")?) {
                opts.strategy = strategy.coerce(i32, globalThis)?;
                library = .zlib;
            }

            if (library_value := options_val.getTruthy(globalThis, "library")?) {
                if (!library_value.isString()) {
                    return globalThis.throwInvalidArguments("Expected library to be a string", .{});
                }

                if (lib_opt := (Library.map.fromJS(globalThis, library_value))?) {
                    library = lib_opt;
                } else {
                    return globalThis.throwInvalidArguments("Expected library to be one of 'zlib' or 'libdeflate'", .{});
                }
            }
        }

        if (globalThis.hasException()) return .zero;

        const compressed = buffer.slice();
        const allocator = jsc.VirtualMachine.get().allocator;

        let list = brk: {
            if (is_gzip && compressed.len > 64) {
                //   0   1   2   3   4   5   6   7
                //  +---+---+---+---+---+---+---+---+
                //  |     CRC32     |     ISIZE     |
                //  +---+---+---+---+---+---+---+---+
                const estimated_size: u32 = @bitCast(compressed[compressed.len - 4 ..][0..4].*);
                // If it's > 256 MB, let's rely on dynamic allocation to minimize the risk of OOM.
                if (estimated_size > 0 && estimated_size < 256 * 1024 * 1024) {
                    break :brk try std.ArrayListUnmanaged(u8).initCapacity(allocator, @max(estimated_size, 64));
                }
            }

            break :brk try std.ArrayListUnmanaged(u8).initCapacity(allocator, if (compressed.len > 512) compressed.len else 32);
        };

        switch (library) {
            .zlib => {
                let reader = zlib.ZlibReaderArrayList.initWithOptions(compressed, &list, allocator, {
                    windowBits: opts.windowBits,
                    level: opts.level,
                }) catch |e| {
                    list.deinit(allocator);
                    if (e == error.InvalidArgument) {
                        return globalThis.throw("Zlib error: Invalid argument", .{});
                    }

                    return globalThis.throwError(e, "Zlib error") catch return .zero;
                };

                reader.readAll(true) catch {
                    defer reader.deinit();
                    return globalThis.throwValue(ZigString.init(reader.errorMessage() ?? "Zlib returned an error").toErrorInstance(globalThis));
                };
                reader.list = { items: reader.list.items };
                reader.list.capacity = reader.list.items.len;
                reader.list_ptr = &reader.list;

                let array_buffer = jsc.ArrayBuffer.fromBytes(reader.list.items, .Uint8Array);
                return array_buffer.toJSWithContext(globalThis, reader, reader_deallocator);
            },
            .libdeflate => {
                const decompressor_opt = bun.libdeflate.Decompressor.alloc();
                if (decompressor_opt == null) {
                    list.deinit(allocator);
                    return globalThis.throwOutOfMemory();
                }
                let decompressor: *bun.libdeflate.Decompressor = decompressor_opt.?;
                defer decompressor.deinit();
                while (true) {
                    const result = decompressor.decompress(compressed, list.allocatedSlice(), if (is_gzip) .gzip else .deflate);

                    list.items.len = result.written;

                    if (result.status == .insufficient_space) {
                        if (list.capacity > 1024 * 1024 * 1024) {
                            list.deinit(allocator);
                            return globalThis.throwOutOfMemory();
                        }

                        list.ensureTotalCapacity(allocator, list.capacity * 2) catch {
                            list.deinit(allocator);
                            return globalThis.throwOutOfMemory();
                        };
                        continue;
                    }

                    if (result.status == .success) {
                        list.items.len = result.written;
                        break;
                    }

                    list.deinit(allocator);
                    return globalThis.throw("libdeflate returned an error: {s}", .{ @tagName(result.status) });
                }

                let array_buffer = jsc.ArrayBuffer.fromBytes(list.items, .Uint8Array);
                return array_buffer.toJSWithContext(globalThis, list.items.ptr, global_deallocator);
            },
        }
    }

    static fn gzipOrDeflateSync(
        globalThis: *JSGlobalObject,
        buffer: jsc.Node.StringOrBuffer,
        options_val_: JSValue?,
        is_gzip: bool,
    ): bun.JSError!JSValue {
        let level: i32? = null;
        let library: Library = .zlib;
        let windowBits: i32 = 0;

        if (options_val := options_val_) {
            if (window := options_val.get(globalThis, "windowBits")?) {
                windowBits = window.coerce(i32, globalThis)?;
                library = .zlib;
            }

            if (library_value := options_val.getTruthy(globalThis, "library")?) {
                if (!library_value.isString()) {
                    return globalThis.throwInvalidArguments("Expected library to be a string", .{});
                }

                if (lib_opt := (Library.map.fromJS(globalThis, library_value))?) {
                    library = lib_opt;
                } else {
                    return globalThis.throwInvalidArguments("Expected library to be one of 'zlib' or 'libdeflate'", .{});
                }
            }

            if (level_value := options_val.get(globalThis, "level")?) {
                level = level_value.coerce(i32, globalThis)?;
                if (globalThis.hasException()) return JSValue.zero;
            }
        }

        if (globalThis.hasException()) return JSValue.zero;

        const compressed = buffer.slice();
        const allocator = bun.default_allocator;

        switch (library) {
            .zlib => {
                let list = try std.ArrayListUnmanaged(u8).initCapacity(
                    allocator,
                    if (compressed.len > 512) compressed.len else 32,
                );

                let reader = zlib.ZlibCompressorArrayList.init(compressed, &list, allocator, {
                    windowBits: 15,
                    gzip: is_gzip,
                    level: level ?? 6,
                }) catch |e| {
                    defer list.deinit(allocator);
                    if (e == error.InvalidArgument) {
                        return globalThis.throw("Zlib error: Invalid argument", .{});
                    }

                    return globalThis.throwError(e, "Zlib error");
                };

                reader.readAll() catch {
                    defer reader.deinit();
                    return globalThis.throwValue(ZigString.init(reader.errorMessage() ?? "Zlib returned an error").toErrorInstance(globalThis));
                };
                reader.list = { items: bun.handleOom(reader.list.toOwnedSlice(allocator)) };
                reader.list.capacity = reader.list.items.len;
                reader.list_ptr = &reader.list;

                let array_buffer = jsc.ArrayBuffer.fromBytes(reader.list.items, .Uint8Array);
                return array_buffer.toJSWithContext(globalThis, reader, reader_deallocator);
            },
            .libdeflate => {
                const compressor_opt = bun.libdeflate.Compressor.alloc(level ?? 6);
                if (compressor_opt == null) {
                    return globalThis.throwOutOfMemory();
                }
                let compressor: *bun.libdeflate.Compressor = compressor_opt.?;
                const encoding: bun.libdeflate.Encoding = if (is_gzip) .gzip else .deflate;
                defer compressor.deinit();

                let list = try std.ArrayListUnmanaged(u8).initCapacity(
                    allocator,
                    // This allocation size is unfortunate, but it's not clear how to avoid it with libdeflate.
                    compressor.maxBytesNeeded(compressed, encoding),
                );

                while (true) {
                    const result = compressor.compress(compressed, list.allocatedSlice(), encoding);

                    list.items.len = result.written;

                    if (result.status == .success) {
                        list.items.len = result.written;
                        break;
                    }

                    list.deinit(allocator);
                    return globalThis.throw("libdeflate error: {s}", .{ @tagName(result.status) });
                }

                let array_buffer = jsc.ArrayBuffer.fromBytes(list.items, .Uint8Array);
                return array_buffer.toJSWithContext(globalThis, list.items.ptr, global_deallocator);
            },
        }
    }
}

pub struct JSZstd {
    export fn deallocator(_: *!?, ctx: *!?) {
        bun.allocators.freeWithoutSize(ctx);
    }

    inline fn getOptions(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!ZstdOptionsTuple {
        const arguments = callframe.arguments();
        const buffer_value: JSValue = if (arguments.len > 0) arguments[0] else .js_undefined;
        const options_val: JSValue? =
            if (arguments.len > 1 && arguments[1].isObject())
                arguments[1]
            else if (arguments.len > 1 && !arguments[1].isUndefined()) {
                return globalThis.throwInvalidArguments("Expected options to be an object", .{});
            } else null;

        if (buffer := jsc.Node.StringOrBuffer.fromJS(globalThis, bun.default_allocator, buffer_value)?) {
            return .{ buffer, options_val };
        }

        return globalThis.throwInvalidArguments("Expected buffer to be a string or buffer", .{});
    }

    static fn getLevel(globalThis: *JSGlobalObject, options_val: JSValue?): bun.JSError!i32 {
        if (option_obj := options_val) {
            if (level_val := option_obj.get(globalThis, "level")?) {
                const value = level_val.coerce(i32, globalThis)?;
                if (globalThis.hasException()) return error.JSError;

                if (value < 1 || value > 22) {
                    return globalThis.throwInvalidArguments("Compression level must be between 1 and 22", .{});
                }

                return value;
            }
        }

        return 3;
    }

    inline fn getOptionsAsync(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!ZstdOptionsAsyncTuple {
        const arguments = callframe.arguments();
        const buffer_value: JSValue = if (arguments.len > 0) arguments[0] else .js_undefined;
        const options_val: JSValue? =
            if (arguments.len > 1 && arguments[1].isObject())
                arguments[1]
            else if (arguments.len > 1 && !arguments[1].isUndefined()) {
                return globalThis.throwInvalidArguments("Expected options to be an object", .{});
            } else null;

        const level = getLevel(globalThis, options_val)?;

        const allow_string_object = true;
        if (buffer := jsc.Node.StringOrBuffer.fromJSMaybeAsync(globalThis, bun.default_allocator, buffer_value, true, allow_string_object)?) {
            return .{ buffer, options_val, level };
        }

        return globalThis.throwInvalidArguments("Expected buffer to be a string or buffer", .{});
    }

    static fn compressSync(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const opts = (getOptions(globalThis, callframe))?;
        const buffer = opts[0];
        const options_val = opts[1];
        defer buffer.deinit();

        const level = getLevel(globalThis, options_val)?;

        const input = buffer.slice();
        const allocator = bun.default_allocator;

        // Calculate max compressed size
        const max_size = bun.zstd.compressBound(input.len);
        let output = allocator.alloc(u8, max_size)?;

        // Perform compression with context
        const compressed_size = switch (bun.zstd.compress(output, input, level)) {
            .success => |size| size,
            .err => |e| {
                allocator.free(output);
                return globalThis.ERR(.ZSTD, "{s}", .{ e }).throw();
            },
        };

        // Resize to actual compressed size
        if (compressed_size < output.len) {
            output = allocator.realloc(output, compressed_size)?;
        }

        return jsc.JSValue.createBuffer(globalThis, output);
    }

    static fn decompressSync(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const opts = (getOptions(globalThis, callframe))?;
        const buffer = opts[0];
        defer buffer.deinit();

        const input = buffer.slice();
        const allocator = bun.default_allocator;

        const output = bun.zstd.decompressAlloc(allocator, input) catch |e| {
            return globalThis.ERR(.ZSTD, "Decompression failed: {s}", .{ @errorName(e) }).throw();
        };

        return jsc.JSValue.createBuffer(globalThis, output);
    }

    // --- Async versions ---

    pub const ZstdJob = struct {
        buffer: jsc.Node.StringOrBuffer = jsc.Node.StringOrBuffer.empty,
        is_compress: bool = true,
        level: i32 = 3,
        task: jsc.WorkPoolTask = { callback: &runTask },
        promise: jsc.JSPromise.Strong = {},
        vm: *jsc.VirtualMachine,
        output: []u8 = [],
        error_message: ([]const u8)? = null,
        any_task: jsc.AnyTask = undefined,
        poll: Async.KeepAlive = {},

        pub const new = bun.TrivialNew(@This());

        static fn runTask(task: *jsc.WorkPoolTask) {
            const job: *ZstdJob = @fieldParentPtr("task", task);
            defer job.vm.enqueueTaskConcurrent(jsc.ConcurrentTask.create(job.any_task.task()));

            const input = job.buffer.slice();
            const allocator = bun.default_allocator;

            if (job.is_compress) {
                // Compression path
                // Calculate max compressed size
                const max_size = bun.zstd.compressBound(input.len);
                job.output = allocator.alloc(u8, max_size) catch {
                    job.error_message = "Out of memory";
                    return;
                };

                // Perform compression
                job.output = switch (bun.zstd.compress(job.output, input, job.level)) {
                    .success => |size| blk: {
                        // Resize to actual compressed size
                        if (size < job.output.len) {
                            break :blk allocator.realloc(job.output, size) catch {
                                job.error_message = "Out of memory";
                                return;
                            };
                        }
                        break :blk job.output;
                    },
                    .err => |e| {
                        allocator.free(job.output);
                        job.output = [];
                        job.error_message = e;
                        return;
                    },
                };
            } else {
                // Decompression path
                job.output = bun.zstd.decompressAlloc(allocator, input) catch {
                    job.error_message = "Decompression failed";
                    return;
                };
            }
        }

        fn runFromJS(*_): bun.JSTerminated! {
            defer _.deinit();

            if (_.vm.isShuttingDown()) {
                return;
            }

            const globalThis = _.vm.global;
            const promise = _.promise.swap();

            if (err_msg := _.error_message) {
                promise.rejectWithAsyncStack(globalThis, globalThis.ERR(.ZSTD, "{s}", .{ err_msg }).toJS())?;
                return;
            }

            const output_slice = _.output;
            const buffer_value = jsc.JSValue.createBuffer(globalThis, output_slice);
            _.output = [];
            promise.resolve(globalThis, buffer_value)?;
        }

        fn deinit(*_) {
            _.poll.unref(_.vm);
            _.buffer.deinitAndUnprotect();
            _.promise.deinit();
            bun.default_allocator.free(_.output);
            bun.destroy(_);
        }

        static fn create(vm: *jsc.VirtualMachine, globalThis: *jsc.JSGlobalObject, buffer: jsc.Node.StringOrBuffer, is_compress: bool, level: i32): *ZstdJob {
            let job = ZstdJob.new({
                buffer: buffer,
                is_compress: is_compress,
                level: level,
                vm: vm,
                any_task: undefined,
            });

            job.promise = jsc.JSPromise.Strong.init(globalThis);
            job.any_task = jsc.AnyTask.New(@This(), &runFromJS).init(job);
            job.poll.ref(vm);
            jsc.WorkPool.schedule(&job.task);

            return job;
        }
    };

    static fn compress(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const opts = (getOptionsAsync(globalThis, callframe))?;
        const buffer = opts[0];
        const level = opts[2];

        const vm = globalThis.bunVM();
        let job = ZstdJob.create(vm, globalThis, buffer, true, level);
        return job.promise.value();
    }

    static fn decompress(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): bun.JSError!JSValue {
        const opts = (getOptionsAsync(globalThis, callframe))?;
        const buffer = opts[0];

        const vm = globalThis.bunVM();
        let job = ZstdJob.create(vm, globalThis, buffer, false, 0); // level is ignored for decompression
        return job.promise.value();
    }
}

// LazyProperty initializers for stdin/stderr/stdout
@callconv("c")
pub fn createBunStdin(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    let rare_data = globalThis.bunVM().rareData();
    let store = rare_data.stdin();
    store.ref();
    let blob = jsc.WebCore.Blob.new(
        jsc.WebCore.Blob.initWithStore(store, globalThis),
    );
    return blob.toJS(globalThis);
}

@callconv("c")
pub fn createBunStderr(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    let rare_data = globalThis.bunVM().rareData();
    let store = rare_data.stderr();
    store.ref();
    let blob = jsc.WebCore.Blob.new(
        jsc.WebCore.Blob.initWithStore(store, globalThis),
    );
    return blob.toJS(globalThis);
}

@callconv("c")
pub fn createBunStdout(globalThis: *jsc.JSGlobalObject): jsc.JSValue {
    let rare_data = globalThis.bunVM().rareData();
    let store = rare_data.stdout();
    store.ref();
    let blob = jsc.WebCore.Blob.new(
        jsc.WebCore.Blob.initWithStore(store, globalThis),
    );
    return blob.toJS(globalThis);
}

comptime {
    * = Bun.Bun__escapeHTML8;
    * = Bun.Bun__escapeHTML16;
    * = Bun.Bun__reportError;
    * = Bun.EnvironmentVariables.Bun__getEnvCount;
    * = Bun.EnvironmentVariables.Bun__getEnvKey;
    * = Bun.EnvironmentVariables.Bun__getEnvValue;
    * = Bun.EnvironmentVariables.Bun__getEnvValueBunString;
    * = Bun.EnvironmentVariables.Bun__setEnvValue;
    * = Crypto.JSPasswordObject.JSPasswordObject__create;
    * = btjs.dumpBtjsTrace;
    Bun.BunObject.exportAll();
}