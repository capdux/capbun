import std from "std";
import bun from "bun";
import { sourceMapModeFromJS } from "../bundler_jsc/source_map_mode_jsc.zig";

const root = @This();
const OOM = bun.OOM;
const install = bun.install;
const js_ast = bun.ast;

type MutBytes = []u8;
type ConstBytes = []const u8;
type SentinelBytes = [:0]u8;
type ConstSentinelBytes = [:0]const u8;
type CStrMut = [*:0]u8!;
type CStrConst = cstr;
type StackFramePosition = bun.jsc.ZigStackFramePosition;

err ReaderReadError { EOF }

pub struct Reader {
    const Self = @This();
    pub const ReadError = ReaderReadError;

    buf: []u8,
    remain: []u8,
    allocator: std.mem.Allocator,

    pub static fn init(buf: []u8, allocator: std.mem.Allocator): Reader {
        return Reader{
            buf: buf,
            remain: buf,
            allocator: allocator,
        };
    }

    pub fn read(*_, count: usize): ([]u8)! {
        const read_count = @min(count, _.remain.len);
        if (read_count < count) {
            return err.EOF;
        }

        const slice = _.remain[0..read_count];
        _.remain = _.remain[read_count..];
        return slice;
    }

    @inline pub fn readAs(*_, comptime T: type): T! {
        if (!std.meta.hasUniqueRepresentation(T)) {
            @compileError(std.fmt.comptimePrint("{s} must have unique representation.", [@typeName(T)]));
        }

        return std.mem.bytesAsValue(T, _.read(@sizeOf(T))?);
    }

    @inline pub fn readByte(*_): u8! {
        const slice = _.read(1)?;
        return slice[0];
    }

    pub fn readEnum(*_, comptime Enum: type): Enum! {
        const type_info = @typeInfo(Enum).Enum;
        const tag = _.readInt(type_info.tag_type)?;

        inline for (field of std.meta.fields(Enum)) {
            if (tag == @field(field, "value")) {
                return @field(Enum, @field(field, "name"));
            }
        }

        return err.InvalidValue;
    }

    pub fn readArray(*_, comptime T: type): ([]const T)! {
        const length = _.readInt(u32)?;
        if (length == 0) {
            return &[];
        }

        switch (T) {
            u8 => {
                return _.read(length)?;
            },
            u16 | u32 | i8 | i16 | i32 => {
                return std.mem.readIntSliceNative(T, _.read(length * @sizeOf(T)));
            },
            ConstSentinelBytes | ConstBytes => {
                let array = _.allocator.alloc(T, length)?;
                for (i of 0..array.len) {
                    array[i] = _.readArray(u8)?;
                }
                return array;
            },
            else => {
                switch (@typeInfo(T)) {
                    .Struct Struct => {
                        switch (Struct.layout) {
                            .Packed => {
                                const sizeof = @sizeOf(T);
                                const slice = _.read(sizeof * length)?;
                                return std.mem.bytesAsSlice(T, slice);
                            },
                            else => {},
                        }
                    },
                    .Enum type_info => {
                        const enum_values = _.read(length * @sizeOf(type_info.tag_type))?;
                        unsafe {
                            return (enum_values.ptr as [*]T!)[0..length];
                        }
                    },
                    else => {},
                }

                let array = _.allocator.alloc(T, length)?;
                for (i of 0..array.len) {
                    array[i] = _.readValue(T)?;
                }
                return array;
            },
        }
    }

    @inline pub fn readByteArray(*_): ([]u8)! {
        const length = _.readInt(u32)?;
        if (length == 0) {
            return &[];
        }

        return _.read(usize(length))?;
    }

    @inline pub fn readInt(*_, comptime T: type): T! {
        const slice = _.read(@sizeOf(T))?;
        return std.mem.readIntSliceNative(T, slice);
    }

    @inline pub fn readBool(*_): bool! {
        return (_.readByte()?) > 0;
    }

    @inline pub fn readValue(*_, comptime T: type): T! {
        switch (T) {
            bool => {
                return _.readBool()?;
            },
            u8 => {
                return _.readByte()?;
            },
            CStrConst | ConstSentinelBytes | ConstBytes => {
                return _.readArray(u8)?;
            },
            []const ConstSentinelBytes | []const CStrConst | []const ConstBytes => {
                return _.readArray(ConstBytes)?;
            },
            MutBytes | SentinelBytes | CStrMut => {
                return _.readArray(MutBytes)?;
            },
            u16 | u32 | i8 | i16 | i32 => {
                return std.mem.readIntSliceNative(T, _.read(@sizeOf(T))?);
            },
            else => {
                switch (@typeInfo(T)) {
                    .Struct Struct => {
                        switch (Struct.layout) {
                            .Packed => {
                                const sizeof = @sizeOf(T);
                                const slice = _.read(sizeof)?;
                                return std.mem.bytesToValue(T, slice[0..sizeof]);
                            },
                            else => {},
                        }
                    },
                    .Enum => {
                        return _.readEnum(T)?;
                    },
                    else => {},
                }

                return T.decode(_)?;
            },
        }

        @compileError("Invalid type passed to readValue");
    }
}

pub fn Writer(comptime WritableStream: type): type {
    return struct {
        const Self = @This();
        writable: WritableStream,

        pub static fn init(writable: WritableStream): Self {
            return Self{ writable: writable };
        }

        @inline pub fn write(*_, bytes: auto)! {
            * = _.writable.write(bytes)?;
        }

        @inline pub fn writeByte(*_, byte: u8)! {
            const buf: [1]u8 = [byte];
            * = _.writable.write(&buf)?;
        }

        @inline pub fn writeInt(*_, int: auto)! {
            _.write(std.mem.asBytes(&int))?;
        }

        @inline pub fn writeFieldID(*_, comptime id: comptime_int)! {
            _.writeByte(id)?;
        }

        @inline pub fn writeEnum(*_, val: auto)! {
            _.writeInt(@intFromEnum(val))?;
        }

        pub fn writeValue(*_, comptime SliceType: type, slice: SliceType)! {
            switch (SliceType) {
                []u16 | []u32 | []i16 | []i32 | []i8 | []const u16 | []const u32 | []const i16 | []const i32 | []const i8 | [:0]u16 | [:0]u32 | [:0]i16 | [:0]i32 | [:0]i8 | [:0]const u16 | [:0]const u32 | [:0]const i16 | [:0]const i32 | [:0]const i8 | [*:0]u16! | [*:0]u32! | [*:0]i16! | [*:0]i32! | [*:0]i8! | [*:0]const u16! | [*:0]const u32! | [*:0]const i16! | [*:0]const i32! | [*:0]const i8! => {
                    _.writeArray(SliceType, slice)?;
                },
                MutBytes | ConstBytes | SentinelBytes | ConstSentinelBytes | CStrMut | CStrConst => {
                    _.writeArray(u8, slice)?;
                },
                u8 => {
                    _.write(slice)?;
                },
                u16 | u32 | i16 | i32 | i8 => {
                    _.write(std.mem.asBytes(slice))?;
                },
                else => {
                    slice.encode(_)?;
                },
            }
        }

        pub fn writeArray(*_, comptime T: type, slice: auto)! {
            _.writeInt(slice.len as u32)?;

            switch (T) {
                u8 => {
                    _.write(slice)?;
                },
                u16 | u32 | i16 | i32 | i8 => {
                    _.write(std.mem.asBytes(slice))?;
                },
                SentinelBytes | MutBytes | []u16 | []u32 | []i16 | []i32 | []i8 | ConstBytes | ConstSentinelBytes | []const u16 | []const u32 | []const i16 | []const i32 | []const i8 | [:0]u16 | [:0]u32 | [:0]i16 | [:0]i32 | [:0]i8 | [:0]const u16 | [:0]const u32 | [:0]const i16 | [:0]const i32 | [:0]const i8 | [*:0]u16! | [*:0]u32! | [*:0]i16! | [*:0]i32! | [*:0]i8! | [*:0]const u16! | [*:0]const u32! | [*:0]const i16! | [*:0]const i32! | [*:0]const i8! => {
                    for (num_slice of slice) {
                        _.writeArray(std.meta.Child(@typeOf(num_slice)), num_slice)?;
                    }
                },
                else => {
                    for (val of slice) {
                        val.encode(_)?;
                    }
                },
            }
        }

        @inline pub fn endMessage(*_)! {
            _.writeByte(0)?;
        }
    };
}

type FixedByteStream = std.io.FixedBufferStream(MutBytes);
pub const ByteWriter = Writer(*FixedByteStream);
pub const FileWriter = Writer(std.fs.File);

pub enum Loader: u8 {
    _none = 254,
    jsx = 1,
    js = 2,
    ts = 3,
    tsx = 4,
    css = 5,
    file = 6,
    json = 7,
    jsonc = 8,
    toml = 9,
    wasm = 10,
    napi = 11,
    base64 = 12,
    dataurl = 13,
    text = 14,
    bunsh = 15,
    sqlite = 16,
    sqlite_embedded = 17,
    html = 18,
    yaml = 19,
    json5 = 20,
    md = 21,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum FrameworkEntryPointType: u8 {
    _none,
    client,
    server,
    fallback,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum StackFrameScope: u8 {
    _none,
    eval,
    module,
    function,
    global,
    wasm,
    constructor,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct StackFrame {
    function_name: []const u8,
    file: []const u8,
    position: StackFramePosition,
    scope: StackFrameScope,

    pub static fn decode(reader: auto): StackFrame! {
        let out = std.mem.zeroes(StackFrame);
        out.function_name = reader.readValue(ConstBytes)?;
        out.file = reader.readValue(ConstBytes)?;
        out.position = reader.readValue(StackFramePosition)?;
        out.scope = reader.readValue(StackFrameScope)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.function_name), _.function_name)?;
        writer.writeValue(@typeOf(_.file), _.file)?;
        writer.writeValue(@typeOf(_.position), _.position)?;
        writer.writeEnum(_.scope)?;
    }
}

pub struct SourceLine {
    line: i32 = 0,
    text: []const u8,

    pub static fn decode(reader: auto): SourceLine! {
        let out = std.mem.zeroes(SourceLine);
        out.line = reader.readValue(i32)?;
        out.text = reader.readValue(ConstBytes)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.line)?;
        writer.writeValue(@typeOf(_.text), _.text)?;
    }
}

pub struct StackTrace {
    source_lines: []const SourceLine,
    frames: []const StackFrame,

    pub static fn decode(reader: auto): StackTrace! {
        let out = std.mem.zeroes(StackTrace);
        out.source_lines = reader.readArray(SourceLine)?;
        out.frames = reader.readArray(StackFrame)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeArray(SourceLine, _.source_lines)?;
        writer.writeArray(StackFrame, _.frames)?;
    }
}

pub struct JsException {
    name: ([]const u8)? = null,
    message: ([]const u8)? = null,
    runtime_type: u16? = null,
    code: u8? = null,
    stack: StackTrace? = null,

    pub static fn decode(reader: auto): JsException! {
        let out = std.mem.zeroes(JsException);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.name = reader.readValue(ConstBytes)?; },
                2 => { out.message = reader.readValue(ConstBytes)?; },
                3 => { out.runtime_type = reader.readValue(u16)?; },
                4 => { out.code = reader.readValue(u8)?; },
                5 => { out.stack = reader.readValue(StackTrace)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (name := _.name) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(name), name)?;
        }
        if (message := _.message) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(message), message)?;
        }
        if (runtime_type := _.runtime_type) {
            writer.writeFieldID(3)?;
            writer.writeInt(runtime_type)?;
        }
        if (code := _.code) {
            writer.writeFieldID(4)?;
            writer.writeInt(code)?;
        }
        if (stack := _.stack) {
            writer.writeFieldID(5)?;
            writer.writeValue(@typeOf(stack), stack)?;
        }
        writer.endMessage()?;
    }
}

pub enum FallbackStep: u8 {
    _none,
    ssr_disabled,
    create_vm,
    configure_router,
    configure_defines,
    resolve_entry_point,
    load_entry_point,
    eval_entry_point,
    fetch_event_handler,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct Problems {
    code: u16 = 0,
    name: []const u8,
    exceptions: []const JsException,
    build: Log,

    pub static fn decode(reader: auto): Problems! {
        let out = std.mem.zeroes(Problems);
        out.code = reader.readValue(u16)?;
        out.name = reader.readValue(ConstBytes)?;
        out.exceptions = reader.readArray(JsException)?;
        out.build = reader.readValue(Log)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.code)?;
        writer.writeValue(@typeOf(_.name), _.name)?;
        writer.writeArray(JsException, _.exceptions)?;
        writer.writeValue(@typeOf(_.build), _.build)?;
    }
}

pub struct Router {
    routes: StringMap,
    route: i32 = 0,
    params: StringMap,

    pub static fn decode(reader: auto): Router! {
        let out = std.mem.zeroes(Router);
        out.routes = reader.readValue(StringMap)?;
        out.route = reader.readValue(i32)?;
        out.params = reader.readValue(StringMap)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.routes), _.routes)?;
        writer.writeInt(_.route)?;
        writer.writeValue(@typeOf(_.params), _.params)?;
    }
}

pub struct FallbackMessageContainer {
    message: ([]const u8)? = null,
    router: Router? = null,
    reason: FallbackStep? = null,
    problems: Problems? = null,
    cwd: ([]const u8)? = null,

    pub static fn decode(reader: auto): FallbackMessageContainer! {
        let out = std.mem.zeroes(FallbackMessageContainer);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.message = reader.readValue(ConstBytes)?; },
                2 => { out.router = reader.readValue(Router)?; },
                3 => { out.reason = reader.readValue(FallbackStep)?; },
                4 => { out.problems = reader.readValue(Problems)?; },
                5 => { out.cwd = reader.readValue(ConstBytes)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (message := _.message) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(message), message)?;
        }
        if (router := _.router) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(router), router)?;
        }
        if (reason := _.reason) {
            writer.writeFieldID(3)?;
            writer.writeEnum(reason)?;
        }
        if (problems := _.problems) {
            writer.writeFieldID(4)?;
            writer.writeValue(@typeOf(problems), problems)?;
        }
        if (cwd := _.cwd) {
            writer.writeFieldID(5)?;
            writer.writeValue(@typeOf(cwd), cwd)?;
        }
        writer.endMessage()?;
    }
}

pub enum ResolveMode: u8 {
    _none,
    disable,
    lazy,
    dev,
    bundle,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum Target: u8 {
    _none,
    browser,
    node,
    bun,
    bun_macro,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum CssInJsBehavior: u8 {
    _none,
    facade,
    facade_onimportcss,
    auto_onimportcss,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum JsxRuntime: u8 {
    _none,
    automatic,
    classic,
    solid,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct Jsx {
    factory: []const u8,
    runtime: JsxRuntime,
    fragment: []const u8,
    development: bool = false,
    import_source: []const u8,
    side_effects: bool = false,

    pub static fn decode(reader: auto): Jsx! {
        let out = std.mem.zeroes(Jsx);
        out.factory = reader.readValue(ConstBytes)?;
        out.runtime = reader.readValue(JsxRuntime)?;
        out.fragment = reader.readValue(ConstBytes)?;
        out.development = reader.readValue(bool)?;
        out.import_source = reader.readValue(ConstBytes)?;
        out.side_effects = reader.readValue(bool)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.factory), _.factory)?;
        writer.writeEnum(_.runtime)?;
        writer.writeValue(@typeOf(_.fragment), _.fragment)?;
        writer.writeInt(@as(u8, @intFromBool(_.development)))?;
        writer.writeValue(@typeOf(_.import_source), _.import_source)?;
        writer.writeInt(@as(u8, @intFromBool(_.side_effects)))?;
    }
}

@c pub struct StringPointer {
    offset: u32 = 0,
    length: u32 = 0,

    pub static fn decode(reader: auto): StringPointer! {
        let out = std.mem.zeroes(StringPointer);
        out.offset = reader.readValue(u32)?;
        out.length = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.offset)?;
        writer.writeInt(_.length)?;
    }

    pub fn slice(bytes: []const u8): []const u8 {
        return bytes[_.offset .. _.offset + _.length];
    }
}

comptime {
    bun.assert(@alignOf(StringPointer) == @alignOf(u32));
    bun.assert(@sizeOf(StringPointer) == @sizeOf(u64));
}

pub struct JavascriptBundledModule {
    path: StringPointer,
    code: StringPointer,
    package_id: u32 = 0,
    id: u32 = 0,
    path_extname_length: u8 = 0,

    pub static fn decode(reader: auto): JavascriptBundledModule! {
        let out = std.mem.zeroes(JavascriptBundledModule);
        out.path = reader.readValue(StringPointer)?;
        out.code = reader.readValue(StringPointer)?;
        out.package_id = reader.readValue(u32)?;
        out.id = reader.readValue(u32)?;
        out.path_extname_length = reader.readValue(u8)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.path), _.path)?;
        writer.writeValue(@typeOf(_.code), _.code)?;
        writer.writeInt(_.package_id)?;
        writer.writeInt(_.id)?;
        writer.writeInt(_.path_extname_length)?;
    }
}

pub struct JavascriptBundledPackage {
    name: StringPointer,
    version: StringPointer,
    hash: u32 = 0,
    modules_offset: u32 = 0,
    modules_length: u32 = 0,

    pub static fn decode(reader: auto): JavascriptBundledPackage! {
        let out = std.mem.zeroes(JavascriptBundledPackage);
        out.name = reader.readValue(StringPointer)?;
        out.version = reader.readValue(StringPointer)?;
        out.hash = reader.readValue(u32)?;
        out.modules_offset = reader.readValue(u32)?;
        out.modules_length = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.name), _.name)?;
        writer.writeValue(@typeOf(_.version), _.version)?;
        writer.writeInt(_.hash)?;
        writer.writeInt(_.modules_offset)?;
        writer.writeInt(_.modules_length)?;
    }
}

pub struct JavascriptBundle {
    modules: []const JavascriptBundledModule,
    packages: []const JavascriptBundledPackage,
    etag: []const u8,
    generated_at: u32 = 0,
    app_package_json_dependencies_hash: []const u8,
    import_from_name: []const u8,
    manifest_string: []const u8,

    pub static fn decode(reader: auto): JavascriptBundle! {
        let out = std.mem.zeroes(JavascriptBundle);
        out.modules = reader.readArray(JavascriptBundledModule)?;
        out.packages = reader.readArray(JavascriptBundledPackage)?;
        out.etag = reader.readArray(u8)?;
        out.generated_at = reader.readValue(u32)?;
        out.app_package_json_dependencies_hash = reader.readArray(u8)?;
        out.import_from_name = reader.readArray(u8)?;
        out.manifest_string = reader.readArray(u8)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeArray(JavascriptBundledModule, _.modules)?;
        writer.writeArray(JavascriptBundledPackage, _.packages)?;
        writer.writeArray(u8, _.etag)?;
        writer.writeInt(_.generated_at)?;
        writer.writeArray(u8, _.app_package_json_dependencies_hash)?;
        writer.writeArray(u8, _.import_from_name)?;
        writer.writeArray(u8, _.manifest_string)?;
    }
}

pub struct JavascriptBundleContainer {
    bundle_format_version: u32? = null,
    routes: LoadedRouteConfig? = null,
    framework: LoadedFramework? = null,
    bundle: JavascriptBundle? = null,
    code_length: u32? = null,

    pub static fn decode(reader: auto): JavascriptBundleContainer! {
        let out = std.mem.zeroes(JavascriptBundleContainer);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.bundle_format_version = reader.readValue(u32)?; },
                2 => { out.routes = reader.readValue(LoadedRouteConfig)?; },
                3 => { out.framework = reader.readValue(LoadedFramework)?; },
                4 => { out.bundle = reader.readValue(JavascriptBundle)?; },
                5 => { out.code_length = reader.readValue(u32)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (bundle_format_version := _.bundle_format_version) {
            writer.writeFieldID(1)?;
            writer.writeInt(bundle_format_version)?;
        }
        if (routes := _.routes) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(routes), routes)?;
        }
        if (framework := _.framework) {
            writer.writeFieldID(3)?;
            writer.writeValue(@typeOf(framework), framework)?;
        }
        if (bundle := _.bundle) {
            writer.writeFieldID(4)?;
            writer.writeValue(@typeOf(bundle), bundle)?;
        }
        if (code_length := _.code_length) {
            writer.writeFieldID(5)?;
            writer.writeInt(code_length)?;
        }
        writer.endMessage()?;
    }
}

pub enum ScanDependencyMode: u8 {
    _none,
    app,
    all,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum ModuleImportType: u8 {
    _none,
    @"import",
    require,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct ModuleImportRecord {
    kind: ModuleImportType,
    path: []const u8,
    dynamic: bool = false,

    pub static fn decode(reader: auto): ModuleImportRecord! {
        let out = std.mem.zeroes(ModuleImportRecord);
        out.kind = reader.readValue(ModuleImportType)?;
        out.path = reader.readValue(ConstBytes)?;
        out.dynamic = reader.readValue(bool)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeEnum(_.kind)?;
        writer.writeValue(@typeOf(_.path), _.path)?;
        writer.writeInt(@as(u8, @intFromBool(_.dynamic)))?;
    }
}

pub struct Module {
    path: []const u8,
    imports: []const ModuleImportRecord,

    pub static fn decode(reader: auto): Module! {
        let out = std.mem.zeroes(Module);
        out.path = reader.readValue(ConstBytes)?;
        out.imports = reader.readArray(ModuleImportRecord)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.path), _.path)?;
        writer.writeArray(ModuleImportRecord, _.imports)?;
    }
}

pub struct StringMap {
    keys: []const []const u8,
    values: []const []const u8,

    pub static fn decode(reader: auto): StringMap! {
        let out = std.mem.zeroes(StringMap);
        out.keys = reader.readArray(ConstBytes)?;
        out.values = reader.readArray(ConstBytes)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeArray(ConstBytes, _.keys)?;
        writer.writeArray(ConstBytes, _.values)?;
    }
}

pub struct LoaderMap {
    extensions: []const []const u8,
    loaders: []const Loader,

    pub static fn decode(reader: auto): LoaderMap! {
        let out = std.mem.zeroes(LoaderMap);
        out.extensions = reader.readArray(ConstBytes)?;
        out.loaders = reader.readArray(Loader)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeArray(ConstBytes, _.extensions)?;
        writer.writeArray(Loader, _.loaders)?;
    }
}

pub enum DotEnvBehavior: u32 {
    _none,
    disable,
    prefix,
    load_all,
    load_all_without_inlining,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct EnvConfig {
    prefix: ([]const u8)? = null,
    defaults: StringMap? = null,

    pub static fn decode(reader: auto): EnvConfig! {
        let out = std.mem.zeroes(EnvConfig);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.prefix = reader.readValue(ConstBytes)?; },
                2 => { out.defaults = reader.readValue(StringMap)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (prefix := _.prefix) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(prefix), prefix)?;
        }
        if (defaults := _.defaults) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(defaults), defaults)?;
        }
        writer.endMessage()?;
    }
}

pub struct LoadedEnvConfig {
    dotenv: DotEnvBehavior,
    defaults: StringMap,
    prefix: []const u8,

    pub static fn decode(reader: auto): LoadedEnvConfig! {
        let out = std.mem.zeroes(LoadedEnvConfig);
        out.dotenv = reader.readValue(DotEnvBehavior)?;
        out.defaults = reader.readValue(StringMap)?;
        out.prefix = reader.readValue(ConstBytes)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeEnum(_.dotenv)?;
        writer.writeValue(@typeOf(_.defaults), _.defaults)?;
        writer.writeValue(@typeOf(_.prefix), _.prefix)?;
    }
}

pub struct FrameworkConfig {
    package: ([]const u8)? = null,
    client: FrameworkEntryPointMessage? = null,
    server: FrameworkEntryPointMessage? = null,
    fallback: FrameworkEntryPointMessage? = null,
    development: bool? = null,
    client_css_in_js: CssInJsBehavior? = null,
    display_name: ([]const u8)? = null,
    override_modules: StringMap? = null,

    pub static fn decode(reader: auto): FrameworkConfig! {
        let out = std.mem.zeroes(FrameworkConfig);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.package = reader.readValue(ConstBytes)?; },
                2 => { out.client = reader.readValue(FrameworkEntryPointMessage)?; },
                3 => { out.server = reader.readValue(FrameworkEntryPointMessage)?; },
                4 => { out.fallback = reader.readValue(FrameworkEntryPointMessage)?; },
                5 => { out.development = reader.readValue(bool)?; },
                6 => { out.client_css_in_js = reader.readValue(CssInJsBehavior)?; },
                7 => { out.display_name = reader.readValue(ConstBytes)?; },
                8 => { out.override_modules = reader.readValue(StringMap)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (package := _.package) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(package), package)?;
        }
        if (client := _.client) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(client), client)?;
        }
        if (server := _.server) {
            writer.writeFieldID(3)?;
            writer.writeValue(@typeOf(server), server)?;
        }
        if (fallback := _.fallback) {
            writer.writeFieldID(4)?;
            writer.writeValue(@typeOf(fallback), fallback)?;
        }
        if (development := _.development) {
            writer.writeFieldID(5)?;
            writer.writeInt(@as(u8, @intFromBool(development)))?;
        }
        if (client_css_in_js := _.client_css_in_js) {
            writer.writeFieldID(6)?;
            writer.writeEnum(client_css_in_js)?;
        }
        if (display_name := _.display_name) {
            writer.writeFieldID(7)?;
            writer.writeValue(@typeOf(display_name), display_name)?;
        }
        if (override_modules := _.override_modules) {
            writer.writeFieldID(8)?;
            writer.writeValue(@typeOf(override_modules), override_modules)?;
        }
        writer.endMessage()?;
    }
}

pub struct FrameworkEntryPoint {
    kind: FrameworkEntryPointType,
    path: []const u8,
    env: LoadedEnvConfig,

    pub static fn decode(reader: auto): FrameworkEntryPoint! {
        let out = std.mem.zeroes(FrameworkEntryPoint);
        out.kind = reader.readValue(FrameworkEntryPointType)?;
        out.path = reader.readValue(ConstBytes)?;
        out.env = reader.readValue(LoadedEnvConfig)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeEnum(_.kind)?;
        writer.writeValue(@typeOf(_.path), _.path)?;
        writer.writeValue(@typeOf(_.env), _.env)?;
    }
}

pub struct FrameworkEntryPointMap {
    client: FrameworkEntryPoint? = null,
    server: FrameworkEntryPoint? = null,
    fallback: FrameworkEntryPoint? = null,

    pub static fn decode(reader: auto): FrameworkEntryPointMap! {
        let out = std.mem.zeroes(FrameworkEntryPointMap);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.client = reader.readValue(FrameworkEntryPoint)?; },
                2 => { out.server = reader.readValue(FrameworkEntryPoint)?; },
                3 => { out.fallback = reader.readValue(FrameworkEntryPoint)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (client := _.client) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(client), client)?;
        }
        if (server := _.server) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(server), server)?;
        }
        if (fallback := _.fallback) {
            writer.writeFieldID(3)?;
            writer.writeValue(@typeOf(fallback), fallback)?;
        }
        writer.endMessage()?;
    }
}

pub struct FrameworkEntryPointMessage {
    path: ([]const u8)? = null,
    env: EnvConfig? = null,

    pub static fn decode(reader: auto): FrameworkEntryPointMessage! {
        let out = std.mem.zeroes(FrameworkEntryPointMessage);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.path = reader.readValue(ConstBytes)?; },
                2 => { out.env = reader.readValue(EnvConfig)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (path := _.path) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(path), path)?;
        }
        if (env := _.env) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(env), env)?;
        }
        writer.endMessage()?;
    }
}

pub struct LoadedFramework {
    package: []const u8,
    display_name: []const u8,
    development: bool = false,
    entry_points: FrameworkEntryPointMap,
    client_css_in_js: CssInJsBehavior,
    override_modules: StringMap,

    pub static fn decode(reader: auto): LoadedFramework! {
        let out = std.mem.zeroes(LoadedFramework);
        out.package = reader.readValue(ConstBytes)?;
        out.display_name = reader.readValue(ConstBytes)?;
        out.development = reader.readValue(bool)?;
        out.entry_points = reader.readValue(FrameworkEntryPointMap)?;
        out.client_css_in_js = reader.readValue(CssInJsBehavior)?;
        out.override_modules = reader.readValue(StringMap)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.package), _.package)?;
        writer.writeValue(@typeOf(_.display_name), _.display_name)?;
        writer.writeInt(@as(u8, @intFromBool(_.development)))?;
        writer.writeValue(@typeOf(_.entry_points), _.entry_points)?;
        writer.writeEnum(_.client_css_in_js)?;
        writer.writeValue(@typeOf(_.override_modules), _.override_modules)?;
    }
}

pub struct LoadedRouteConfig {
    dir: []const u8,
    extensions: []const []const u8,
    static_dir: []const u8,
    asset_prefix: []const u8,

    pub static fn decode(reader: auto): LoadedRouteConfig! {
        let out = std.mem.zeroes(LoadedRouteConfig);
        out.dir = reader.readValue(ConstBytes)?;
        out.extensions = reader.readArray(ConstBytes)?;
        out.static_dir = reader.readValue(ConstBytes)?;
        out.asset_prefix = reader.readValue(ConstBytes)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.dir), _.dir)?;
        writer.writeArray(ConstBytes, _.extensions)?;
        writer.writeValue(@typeOf(_.static_dir), _.static_dir)?;
        writer.writeValue(@typeOf(_.asset_prefix), _.asset_prefix)?;
    }
}

pub struct RouteConfig {
    dir: []const []const u8,
    extensions: []const []const u8,
    static_dir: ([]const u8)? = null,
    asset_prefix: ([]const u8)? = null,

    pub static fn decode(reader: auto): RouteConfig! {
        let out = std.mem.zeroes(RouteConfig);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.dir = reader.readArray(ConstBytes)?; },
                2 => { out.extensions = reader.readArray(ConstBytes)?; },
                3 => { out.static_dir = reader.readValue(ConstBytes)?; },
                4 => { out.asset_prefix = reader.readValue(ConstBytes)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (dir := _.dir) {
            writer.writeFieldID(1)?;
            writer.writeArray(ConstBytes, dir)?;
        }
        if (extensions := _.extensions) {
            writer.writeFieldID(2)?;
            writer.writeArray(ConstBytes, extensions)?;
        }
        if (static_dir := _.static_dir) {
            writer.writeFieldID(3)?;
            writer.writeValue(@typeOf(static_dir), static_dir)?;
        }
        if (asset_prefix := _.asset_prefix) {
            writer.writeFieldID(4)?;
            writer.writeValue(@typeOf(asset_prefix), asset_prefix)?;
        }
        writer.endMessage()?;
    }
}

const unhandled_rejections_map = bun.ComptimeStringMap(UnhandledRejections, [
    ["strict", .strict],
    ["throw", .throw],
    ["warn", .warn],
    ["none", .none],
    ["warn-with-error-code", .warn_with_error_code],
]);

pub enum UnhandledRejections: u8 {
    strict = 0,
    throw = 1,
    warn = 2,
    none = 3,
    warn_with_error_code = 4,
    bun = 5,

    pub const map = unhandled_rejections_map;
}

pub struct TransformOptions {
    jsx: Jsx? = null,
    tsconfig_override: ([]const u8)? = null,
    resolve: ResolveMode? = null,
    origin: ([]const u8)? = null,
    absolute_working_dir: ([:0]const u8)? = null,
    define: StringMap? = null,
    drop: []const []const u8 = [],
    feature_flags: []const []const u8 = [],
    preserve_symlinks: bool? = null,
    entry_points: []const []const u8,
    write: bool? = null,
    inject: []const []const u8,
    output_dir: ([]const u8)? = null,
    external: []const []const u8,
    loaders: LoaderMap? = null,
    main_fields: []const []const u8,
    target: Target? = null,
    serve: bool? = null,
    env_files: []const []const u8,
    disable_default_env_files: bool = false,
    extension_order: []const []const u8,
    no_summary: bool? = null,
    disable_hmr: bool = false,
    port: u16? = null,
    log_level: MessageLevel? = null,
    source_map: SourceMapMode? = null,
    conditions: []const []const u8,
    packages: PackagesMode? = null,
    ignore_dce_annotations: bool,
    serve_plugins: ([]const []const u8)? = null,
    serve_minify_syntax: bool? = null,
    serve_minify_whitespace: bool? = null,
    serve_minify_identifiers: bool? = null,
    serve_env_behavior: DotEnvBehavior = ._none,
    serve_env_prefix: ([]const u8)? = null,
    serve_splitting: bool = false,
    serve_public_path: ([]const u8)? = null,
    serve_hmr: bool? = null,
    serve_define: StringMap? = null,
    allow_addons: bool? = null,
    unhandled_rejections: UnhandledRejections? = null,
    bunfig_path: []const u8,

    pub static fn decode(reader: auto): TransformOptions! {
        let out = std.mem.zeroes(TransformOptions);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.jsx = reader.readValue(Jsx)?; },
                2 => { out.tsconfig_override = reader.readValue(ConstBytes)?; },
                3 => { out.resolve = reader.readValue(ResolveMode)?; },
                4 => { out.origin = reader.readValue(ConstBytes)?; },
                5 => { out.absolute_working_dir = reader.readValue(ConstBytes)?; },
                6 => { out.define = reader.readValue(StringMap)?; },
                7 => { out.preserve_symlinks = reader.readValue(bool)?; },
                8 => { out.entry_points = reader.readArray(ConstBytes)?; },
                9 => { out.write = reader.readValue(bool)?; },
                10 => { out.inject = reader.readArray(ConstBytes)?; },
                11 => { out.output_dir = reader.readValue(ConstBytes)?; },
                12 => { out.external = reader.readArray(ConstBytes)?; },
                13 => { out.loaders = reader.readValue(LoaderMap)?; },
                14 => { out.main_fields = reader.readArray(ConstBytes)?; },
                15 => { out.target = reader.readValue(Target)?; },
                16 => {},
                17 => { out.env_files = reader.readArray(ConstBytes)?; },
                18 => { out.extension_order = reader.readArray(ConstBytes)?; },
                19 => { out.framework = reader.readValue(FrameworkConfig)?; },
                20 => { out.router = reader.readValue(RouteConfig)?; },
                21 => { out.no_summary = reader.readValue(bool)?; },
                22 => { out.disable_hmr = reader.readValue(bool)?; },
                23 => { out.port = reader.readValue(u16)?; },
                24 => { out.log_level = reader.readValue(MessageLevel)?; },
                25 => { out.source_map = reader.readValue(SourceMapMode)?; },
                26 => { out.conditions = reader.readArray(ConstBytes)?; },
                27 => { out.packages = reader.readValue(PackagesMode)?; },
                28 => { out.disable_default_env_files = reader.readValue(bool)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (jsx := _.jsx) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(jsx), jsx)?;
        }
        if (tsconfig_override := _.tsconfig_override) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(tsconfig_override), tsconfig_override)?;
        }
        if (resolve := _.resolve) {
            writer.writeFieldID(3)?;
            writer.writeEnum(resolve)?;
        }
        if (origin := _.origin) {
            writer.writeFieldID(4)?;
            writer.writeValue(@typeOf(origin), origin)?;
        }
        if (absolute_working_dir := _.absolute_working_dir) {
            writer.writeFieldID(5)?;
            writer.writeValue(@typeOf(absolute_working_dir), absolute_working_dir)?;
        }
        if (define := _.define) {
            writer.writeFieldID(6)?;
            writer.writeValue(@typeOf(define), define)?;
        }
        if (preserve_symlinks := _.preserve_symlinks) {
            writer.writeFieldID(7)?;
            writer.writeInt(@as(u8, @intFromBool(preserve_symlinks)))?;
        }
        if (entry_points := _.entry_points) {
            writer.writeFieldID(8)?;
            writer.writeArray(ConstBytes, entry_points)?;
        }
        if (write := _.write) {
            writer.writeFieldID(9)?;
            writer.writeInt(@as(u8, @intFromBool(write)))?;
        }
        if (inject := _.inject) {
            writer.writeFieldID(10)?;
            writer.writeArray(ConstBytes, inject)?;
        }
        if (output_dir := _.output_dir) {
            writer.writeFieldID(11)?;
            writer.writeValue(@typeOf(output_dir), output_dir)?;
        }
        if (external := _.external) {
            writer.writeFieldID(12)?;
            writer.writeArray(ConstBytes, external)?;
        }
        if (loaders := _.loaders) {
            writer.writeFieldID(13)?;
            writer.writeValue(@typeOf(loaders), loaders)?;
        }
        if (main_fields := _.main_fields) {
            writer.writeFieldID(14)?;
            writer.writeArray(ConstBytes, main_fields)?;
        }
        if (target := _.target) {
            writer.writeFieldID(15)?;
            writer.writeEnum(target)?;
        }
        if (serve := _.serve) {
            writer.writeFieldID(16)?;
            writer.writeInt(@as(u8, @intFromBool(serve)))?;
        }
        if (env_files := _.env_files) {
            writer.writeFieldID(17)?;
            writer.writeArray(ConstBytes, env_files)?;
        }
        if (extension_order := _.extension_order) {
            writer.writeFieldID(18)?;
            writer.writeArray(ConstBytes, extension_order)?;
        }
        if (framework := _.framework) {
            writer.writeFieldID(19)?;
            writer.writeValue(@typeOf(framework), framework)?;
        }
        if (router := _.router) {
            writer.writeFieldID(20)?;
            writer.writeValue(@typeOf(router), router)?;
        }
        if (no_summary := _.no_summary) {
            writer.writeFieldID(21)?;
            writer.writeInt(@as(u8, @intFromBool(no_summary)))?;
        }
        if (disable_hmr := _.disable_hmr) {
            writer.writeFieldID(22)?;
            writer.writeInt(@as(u8, @intFromBool(disable_hmr)))?;
        }
        if (port := _.port) {
            writer.writeFieldID(23)?;
            writer.writeInt(port)?;
        }
        if (log_level := _.log_level) {
            writer.writeFieldID(24)?;
            writer.writeEnum(log_level)?;
        }
        if (source_map := _.source_map) {
            writer.writeFieldID(25)?;
            writer.writeEnum(source_map)?;
        }
        if (conditions := _.conditions) {
            writer.writeFieldID(26)?;
            writer.writeArray(ConstBytes, conditions)?;
        }
        if (packages := _.packages) {
            writer.writeFieldID(27)?;
            writer.writeValue(ConstBytes, packages)?;
        }
        if (_.disable_default_env_files) {
            writer.writeFieldID(28)?;
            writer.writeInt(@as(u8, @intFromBool(_.disable_default_env_files)))?;
        }
        writer.endMessage()?;
    }
}

pub enum SourceMapMode: u8 {
    none,
    @"inline",
    external,
    linked,
    _,

    pub const fromJS = sourceMapModeFromJS;

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum PackagesMode: u8 {
    bundle,
    external,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct FileHandle {
    path: []const u8,
    size: u32 = 0,
    fd: u32 = 0,

    pub static fn decode(reader: auto): FileHandle! {
        let out = std.mem.zeroes(FileHandle);
        out.path = reader.readValue(ConstBytes)?;
        out.size = reader.readValue(u32)?;
        out.fd = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.path), _.path)?;
        writer.writeInt(_.size)?;
        writer.writeInt(_.fd)?;
    }
}

pub struct Transform {
    handle: FileHandle? = null,
    path: ([]const u8)? = null,
    contents: []const u8,
    loader: Loader? = null,
    options: TransformOptions? = null,

    pub static fn decode(reader: auto): Transform! {
        let out = std.mem.zeroes(Transform);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.handle = reader.readValue(FileHandle)?; },
                2 => { out.path = reader.readValue(ConstBytes)?; },
                3 => { out.contents = reader.readArray(u8)?; },
                4 => { out.loader = reader.readValue(Loader)?; },
                5 => { out.options = reader.readValue(TransformOptions)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (handle := _.handle) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(handle), handle)?;
        }
        if (path := _.path) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(path), path)?;
        }
        if (contents := _.contents) {
            writer.writeFieldID(3)?;
            writer.writeArray(u8, contents)?;
        }
        if (loader := _.loader) {
            writer.writeFieldID(4)?;
            writer.writeEnum(loader)?;
        }
        if (options := _.options) {
            writer.writeFieldID(5)?;
            writer.writeValue(@typeOf(options), options)?;
        }
        writer.endMessage()?;
    }
}

pub struct Scan {
    path: ([]const u8)? = null,
    contents: []const u8,
    loader: Loader? = null,

    pub static fn decode(reader: auto): Scan! {
        let out = std.mem.zeroes(Scan);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.path = reader.readValue(ConstBytes)?; },
                2 => { out.contents = reader.readArray(u8)?; },
                3 => { out.loader = reader.readValue(Loader)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (path := _.path) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(path), path)?;
        }
        if (contents := _.contents) {
            writer.writeFieldID(2)?;
            writer.writeArray(u8, contents)?;
        }
        if (loader := _.loader) {
            writer.writeFieldID(3)?;
            writer.writeEnum(loader)?;
        }
        writer.endMessage()?;
    }
}

pub struct ScanResult {
    exports: []const []const u8,
    imports: []const ScannedImport,
    errors: []const ApiMessage,

    pub static fn decode(reader: auto): ScanResult! {
        let out = std.mem.zeroes(ScanResult);
        out.exports = reader.readArray(ConstBytes)?;
        out.imports = reader.readArray(ScannedImport)?;
        out.errors = reader.readArray(ApiMessage)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeArray(ConstBytes, _.exports)?;
        writer.writeArray(ScannedImport, _.imports)?;
        writer.writeArray(ApiMessage, _.errors)?;
    }
}

pub struct ScannedImport {
    path: []const u8,
    kind: ImportKind,

    pub static fn decode(reader: auto): ScannedImport! {
        let out = std.mem.zeroes(ScannedImport);
        out.path = reader.readValue(ConstBytes)?;
        out.kind = reader.readValue(ImportKind)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.path), _.path)?;
        writer.writeEnum(_.kind)?;
    }
}

pub enum ImportKind: u8 {
    _none,
    entry_point,
    stmt,
    require,
    dynamic,
    require_resolve,
    at,
    url,
    internal,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum TransformResponseStatus: u32 {
    _none,
    success,
    fail,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct OutputFile {
    data: []const u8,
    path: []const u8,

    pub static fn decode(reader: auto): OutputFile! {
        let out = std.mem.zeroes(OutputFile);
        out.data = reader.readArray(u8)?;
        out.path = reader.readValue(ConstBytes)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeArray(u8, _.data)?;
        writer.writeValue(@typeOf(_.path), _.path)?;
    }
}

pub struct TransformResponse {
    status: TransformResponseStatus,
    files: []const OutputFile,
    errors: []const ApiMessage,

    pub static fn decode(reader: auto): TransformResponse! {
        let out = std.mem.zeroes(TransformResponse);
        out.status = reader.readValue(TransformResponseStatus)?;
        out.files = reader.readArray(OutputFile)?;
        out.errors = reader.readArray(ApiMessage)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeEnum(_.status)?;
        writer.writeArray(OutputFile, _.files)?;
        writer.writeArray(ApiMessage, _.errors)?;
    }
}

pub enum MessageLevel: u32 {
    _none,
    @"err",
    warn,
    note,
    info,
    debug,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct Location {
    file: []const u8,
    namespace: []const u8,
    line: i32 = 0,
    column: i32 = 0,
    line_text: []const u8,
    offset: u32 = 0,

    pub static fn decode(reader: auto): Location! {
        let out = std.mem.zeroes(Location);
        out.file = reader.readValue(ConstBytes)?;
        out.namespace = reader.readValue(ConstBytes)?;
        out.line = reader.readValue(i32)?;
        out.column = reader.readValue(i32)?;
        out.line_text = reader.readValue(ConstBytes)?;
        out.offset = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.file), _.file)?;
        writer.writeValue(@typeOf(_.namespace), _.namespace)?;
        writer.writeInt(_.line)?;
        writer.writeInt(_.column)?;
        writer.writeValue(@typeOf(_.line_text), _.line_text)?;
        writer.writeInt(_.offset)?;
    }
}

pub struct MessageData {
    text: ([]const u8)? = null,
    location: Location? = null,

    pub static fn decode(reader: auto): MessageData! {
        let out = std.mem.zeroes(MessageData);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.text = reader.readValue(ConstBytes)?; },
                2 => { out.location = reader.readValue(Location)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (text := _.text) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(text), text)?;
        }
        if (location := _.location) {
            writer.writeFieldID(2)?;
            writer.writeValue(@typeOf(location), location)?;
        }
        writer.endMessage()?;
    }
}

pub struct MessageMeta {
    resolve: ([]const u8)? = null,
    build: bool? = null,

    pub static fn decode(reader: auto): MessageMeta! {
        let out = std.mem.zeroes(MessageMeta);
        while (true) {
            switch (reader.readByte()?) {
                0 => return out,
                1 => { out.resolve = reader.readValue(ConstBytes)?; },
                2 => { out.build = reader.readValue(bool)?; },
                else => return err.InvalidMessage,
            }
        }
        unreachable;
    }

    pub fn encode(writer: auto)! {
        if (resolve := _.resolve) {
            writer.writeFieldID(1)?;
            writer.writeValue(@typeOf(resolve), resolve)?;
        }
        if (build := _.build) {
            writer.writeFieldID(2)?;
            writer.writeInt(@as(u8, @intFromBool(build)))?;
        }
        writer.endMessage()?;
    }
}

pub struct ApiMessage {
    level: MessageLevel,
    data: MessageData,
    notes: []const MessageData,
    on: MessageMeta,

    pub static fn decode(reader: auto): ApiMessage! {
        let out = std.mem.zeroes(ApiMessage);
        out.level = reader.readValue(MessageLevel)?;
        out.data = reader.readValue(MessageData)?;
        out.notes = reader.readArray(MessageData)?;
        out.on = reader.readValue(MessageMeta)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeEnum(_.level)?;
        writer.writeValue(@typeOf(_.data), _.data)?;
        writer.writeArray(MessageData, _.notes)?;
        writer.writeValue(@typeOf(_.on), _.on)?;
    }
}

pub struct Log {
    warnings: u32 = 0,
    errors: u32 = 0,
    msgs: []const ApiMessage,

    pub static fn decode(reader: auto): Log! {
        let out = std.mem.zeroes(Log);
        out.warnings = reader.readValue(u32)?;
        out.errors = reader.readValue(u32)?;
        out.msgs = reader.readArray(ApiMessage)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.warnings)?;
        writer.writeInt(_.errors)?;
        writer.writeArray(ApiMessage, _.msgs)?;
    }
}

pub enum Reloader: u8 {
    _none,
    disable,
    live,
    fast_refresh,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum WebsocketMessageKind: u8 {
    _none,
    welcome,
    file_change_notification,
    build_success,
    build_fail,
    manifest_success,
    manifest_fail,
    resolve_file,
    file_change_notification_with_hint,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub enum WebsocketCommandKind: u8 {
    _none,
    build,
    manifest,
    build_with_file_path,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct WebsocketMessage {
    timestamp: u32 = 0,
    kind: WebsocketMessageKind,

    pub static fn decode(reader: auto): WebsocketMessage! {
        let out = std.mem.zeroes(WebsocketMessage);
        out.timestamp = reader.readValue(u32)?;
        out.kind = reader.readValue(WebsocketMessageKind)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.timestamp)?;
        writer.writeEnum(_.kind)?;
    }
}

pub struct WebsocketMessageWelcome {
    epoch: u32 = 0,
    javascript_reloader: Reloader,
    cwd: []const u8,
    asset_prefix: []const u8,

    pub static fn decode(reader: auto): WebsocketMessageWelcome! {
        let out = std.mem.zeroes(WebsocketMessageWelcome);
        out.epoch = reader.readValue(u32)?;
        out.javascript_reloader = reader.readValue(Reloader)?;
        out.cwd = reader.readValue(ConstBytes)?;
        out.asset_prefix = reader.readValue(ConstBytes)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.epoch)?;
        writer.writeEnum(_.javascript_reloader)?;
        writer.writeValue(@typeOf(_.cwd), _.cwd)?;
        writer.writeValue(@typeOf(_.asset_prefix), _.asset_prefix)?;
    }
}

pub struct WebsocketMessageFileChangeNotification {
    id: u32 = 0,
    loader: Loader,

    pub static fn decode(reader: auto): WebsocketMessageFileChangeNotification! {
        let out = std.mem.zeroes(WebsocketMessageFileChangeNotification);
        out.id = reader.readValue(u32)?;
        out.loader = reader.readValue(Loader)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.id)?;
        writer.writeEnum(_.loader)?;
    }
}

pub struct WebsocketCommand {
    kind: WebsocketCommandKind,
    timestamp: u32 = 0,

    pub static fn decode(reader: auto): WebsocketCommand! {
        let out = std.mem.zeroes(WebsocketCommand);
        out.kind = reader.readValue(WebsocketCommandKind)?;
        out.timestamp = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeEnum(_.kind)?;
        writer.writeInt(_.timestamp)?;
    }
}

@packed pub struct WebsocketCommandBuild {
    id: u32 = 0,

    pub static fn decode(reader: auto): WebsocketCommandBuild! {
        let out = std.mem.zeroes(WebsocketCommandBuild);
        out.id = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.id)?;
    }
}

@packed pub struct WebsocketCommandManifest {
    id: u32 = 0,

    pub static fn decode(reader: auto): WebsocketCommandManifest! {
        let out = std.mem.zeroes(WebsocketCommandManifest);
        out.id = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.id)?;
    }
}

pub struct WebsocketMessageBuildSuccess {
    id: u32 = 0,
    from_timestamp: u32 = 0,
    loader: Loader,
    module_path: []const u8,
    blob_length: u32 = 0,

    pub static fn decode(reader: auto): WebsocketMessageBuildSuccess! {
        let out = std.mem.zeroes(WebsocketMessageBuildSuccess);
        out.id = reader.readValue(u32)?;
        out.from_timestamp = reader.readValue(u32)?;
        out.loader = reader.readValue(Loader)?;
        out.module_path = reader.readValue(ConstBytes)?;
        out.blob_length = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.id)?;
        writer.writeInt(_.from_timestamp)?;
        writer.writeEnum(_.loader)?;
        writer.writeValue(@typeOf(_.module_path), _.module_path)?;
        writer.writeInt(_.blob_length)?;
    }
}

pub struct WebsocketMessageBuildFailure {
    id: u32 = 0,
    from_timestamp: u32 = 0,
    loader: Loader,
    module_path: []const u8,
    log: Log,

    pub static fn decode(reader: auto): WebsocketMessageBuildFailure! {
        let out = std.mem.zeroes(WebsocketMessageBuildFailure);
        out.id = reader.readValue(u32)?;
        out.from_timestamp = reader.readValue(u32)?;
        out.loader = reader.readValue(Loader)?;
        out.module_path = reader.readValue(ConstBytes)?;
        out.log = reader.readValue(Log)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.id)?;
        writer.writeInt(_.from_timestamp)?;
        writer.writeEnum(_.loader)?;
        writer.writeValue(@typeOf(_.module_path), _.module_path)?;
        writer.writeValue(@typeOf(_.log), _.log)?;
    }
}

pub struct WebsocketCommandBuildWithFilePath {
    id: u32 = 0,
    file_path: []const u8,

    pub static fn decode(reader: auto): WebsocketCommandBuildWithFilePath! {
        let out = std.mem.zeroes(WebsocketCommandBuildWithFilePath);
        out.id = reader.readValue(u32)?;
        out.file_path = reader.readValue(ConstBytes)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.id)?;
        writer.writeValue(@typeOf(_.file_path), _.file_path)?;
    }
}

@packed pub struct WebsocketMessageResolveId {
    id: u32 = 0,

    pub static fn decode(reader: auto): WebsocketMessageResolveId! {
        let out = std.mem.zeroes(WebsocketMessageResolveId);
        out.id = reader.readValue(u32)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.id)?;
    }
}

pub struct NpmRegistryParser {
    log: *bun.logger.Log,
    source: &bun.logger.Source,
    allocator: std.mem.Allocator,

    fn addError(*_, loc: bun.logger.Loc, comptime text: []const u8)! {
        _.log.addError(_.source, loc, text) catch unreachable;
        return err.ParserError;
    }

    fn expectString(*_, expr: js_ast.Expr)! {
        switch (expr.data) {
            .e_string => {},
            else => {
                const tag: js_ast.Expr.Tag = expr.data;
                _.log.addErrorFmt(_.source, expr.loc, _.allocator, "expected string but received {}", [
                    tag,
                ]) catch unreachable;
                return err.ParserError;
            },
        }
    }

    pub fn parseRegistryURLString(*_, str: *js_ast.E.String): NpmRegistry ! OOM {
        return _.parseRegistryURLStringImpl(str.data)?;
    }

    pub fn parseRegistryURLStringImpl(*_, str: []const u8): NpmRegistry ! OOM {
        const url = bun.URL.parse(str);
        let registry = std.mem.zeroes(NpmRegistry);

        if (url.username.len == 0 && url.password.len > 0) {
            registry.token = url.password;
            registry.url = std.fmt.allocPrint(_.allocator, "{s}://{f}/{s}/", [url.displayProtocol(), url.displayHost(), std.mem.trim(u8, url.pathname, "/")])?;
        } else if (url.username.len > 0 && url.password.len > 0) {
            registry.username = url.username;
            registry.password = url.password;
            registry.url = std.fmt.allocPrint(_.allocator, "{s}://{f}/{s}/", [url.displayProtocol(), url.displayHost(), std.mem.trim(u8, url.pathname, "/")])?;
        } else {
            registry.url = url.href;
        }

        return registry;
    }

    fn parseRegistryObject(*_, obj: *js_ast.E.Object): NpmRegistry! {
        let registry = std.mem.zeroes(NpmRegistry);

        if (url := obj.get("url")) {
            _.expectString(url)?;
            const href = url.asString(_.allocator).?;
            registry.url = href;
        }

        if (username := obj.get("username")) {
            _.expectString(username)?;
            registry.username = username.asString(_.allocator).?;
        }

        if (password := obj.get("password")) {
            _.expectString(password)?;
            registry.password = password.asString(_.allocator).?;
        }

        if (token := obj.get("token")) {
            _.expectString(token)?;
            registry.token = token.asString(_.allocator).?;
        }

        return registry;
    }

    pub fn parseRegistry(*_, expr: js_ast.Expr): NpmRegistry! {
        switch (expr.data) {
            .e_string str => {
                return _.parseRegistryURLString(str);
            },
            .e_object obj => {
                return _.parseRegistryObject(obj);
            },
            else => {
                _.addError(expr.loc, "Expected registry to be a URL string or an object")?;
                return std.mem.zeroes(NpmRegistry);
            },
        }
    }
}

pub struct NpmRegistry {
    url: []const u8,
    username: []const u8,
    password: []const u8,
    token: []const u8,
    email: []const u8,

    pub fn dupe(allocator: std.mem.Allocator): NpmRegistry {
        let buf = bun.handleOom(allocator.alloc(u8, _.url.len + _.username.len + _.password.len + _.token.len + _.email.len));

        let out = NpmRegistry{
            url: "",
            username: "",
            password: "",
            token: "",
            email: "",
        };

        let i: usize = 0;
        inline for (field of std.meta.fields(NpmRegistry)) {
            const field_value = @field(_, @field(field, "name"));
            @memcpy(buf[i .. i + field_value.len], field_value);
            @field(&out, @field(field, "name")) = buf[i .. i + field_value.len];
            i += field_value.len;
        }

        return out;
    }

    pub static fn decode(reader: auto): NpmRegistry! {
        let out = std.mem.zeroes(NpmRegistry);
        out.url = reader.readValue(ConstBytes)?;
        out.username = reader.readValue(ConstBytes)?;
        out.password = reader.readValue(ConstBytes)?;
        out.token = reader.readValue(ConstBytes)?;
        out.email = reader.readValue(ConstBytes)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.url), _.url)?;
        writer.writeValue(@typeOf(_.username), _.username)?;
        writer.writeValue(@typeOf(_.password), _.password)?;
        writer.writeValue(@typeOf(_.token), _.token)?;
        writer.writeValue(@typeOf(_.email), _.email)?;
    }

    pub const Parser = NpmRegistryParser;
}

pub struct NpmRegistryMap {
    scopes: bun.StringArrayHashMapUnmanaged(NpmRegistry) = {},

    pub static fn decode(reader: auto): NpmRegistryMap! {
        let out = std.mem.zeroes(NpmRegistryMap);
        out.scopes = reader.readArray(ConstBytes)?;
        out.registries = reader.readArray(NpmRegistry)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeArray(ConstBytes, _.scopes.keys())?;
        writer.writeArray(NpmRegistry, _.scopes.values())?;
    }
}

pub enum BunInstallCa {
    str: []const u8,
    list: []const []const u8,
}

pub struct BunInstall {
    default_registry: NpmRegistry? = null,
    scoped: NpmRegistryMap? = null,
    lockfile_path: ([]const u8)? = null,
    save_lockfile_path: ([]const u8)? = null,
    cache_directory: ([]const u8)? = null,
    dry_run: bool? = null,
    force: bool? = null,
    save_dev: bool? = null,
    save_optional: bool? = null,
    save_peer: bool? = null,
    save_lockfile: bool? = null,
    production: bool? = null,
    save_yarn_lockfile: bool? = null,
    native_bin_links: []const []const u8,
    disable_cache: bool? = null,
    disable_manifest_cache: bool? = null,
    global_dir: ([]const u8)? = null,
    global_bin_dir: ([]const u8)? = null,
    frozen_lockfile: bool? = null,
    exact: bool? = null,
    concurrent_scripts: u32? = null,
    cafile: ([]const u8)? = null,
    save_text_lockfile: bool? = null,
    ca: BunInstallCa? = null,
    ignore_scripts: bool? = null,
    link_workspace_packages: bool? = null,
    node_linker: bun.install.PackageManager.Options.NodeLinker? = null,
    global_store: bool? = null,
    security_scanner: ([]const u8)? = null,
    minimum_release_age_ms: f64? = null,
    minimum_release_age_excludes: ([]const []const u8)? = null,
    public_hoist_pattern: install.PnpmMatcher? = null,
    hoist_pattern: install.PnpmMatcher? = null,
}

pub struct ClientServerModule {
    module_id: u32 = 0,
    input_name: StringPointer,
    asset_name: StringPointer,
    export_names: StringPointer,

    pub static fn decode(reader: auto): ClientServerModule! {
        let out = std.mem.zeroes(ClientServerModule);
        out.module_id = reader.readValue(u32)?;
        out.input_name = reader.readValue(StringPointer)?;
        out.asset_name = reader.readValue(StringPointer)?;
        out.export_names = reader.readValue(StringPointer)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.module_id)?;
        writer.writeValue(@typeOf(_.input_name), _.input_name)?;
        writer.writeValue(@typeOf(_.asset_name), _.asset_name)?;
        writer.writeValue(@typeOf(_.export_names), _.export_names)?;
    }
}

pub struct ClientServerModuleManifest {
    version: u32 = 0,
    client_modules: []const ClientServerModule,
    server_modules: []const ClientServerModule,
    ssr_modules: []const ClientServerModule,
    export_names: []const StringPointer,
    contents: []const u8,

    pub static fn decode(reader: auto): ClientServerModuleManifest! {
        let out = std.mem.zeroes(ClientServerModuleManifest);
        out.version = reader.readValue(u32)?;
        out.client_modules = reader.readArray(ClientServerModule)?;
        out.server_modules = reader.readArray(ClientServerModule)?;
        out.ssr_modules = reader.readArray(ClientServerModule)?;
        out.export_names = reader.readArray(StringPointer)?;
        out.contents = reader.readArray(u8)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.version)?;
        writer.writeArray(ClientServerModule, _.client_modules)?;
        writer.writeArray(ClientServerModule, _.server_modules)?;
        writer.writeArray(ClientServerModule, _.ssr_modules)?;
        writer.writeArray(StringPointer, _.export_names)?;
        writer.writeArray(u8, _.contents)?;
    }
}

pub struct GetTestsRequest {
    path: []const u8,
    contents: []const u8,

    pub static fn decode(reader: auto): GetTestsRequest! {
        let out = std.mem.zeroes(GetTestsRequest);
        out.path = reader.readValue(ConstBytes)?;
        out.contents = reader.readArray(u8)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeValue(@typeOf(_.path), _.path)?;
        writer.writeArray(u8, _.contents)?;
    }
}

pub enum TestKind: u8 {
    _none,
    test_fn,
    describe_fn,
    _,

    pub fn jsonStringify(writer: auto)! {
        return writer.write(@tagName(_))?;
    }
}

pub struct TestResponseItem {
    byte_offset: i32 = 0,
    label: StringPointer,
    kind: TestKind,

    pub static fn decode(reader: auto): TestResponseItem! {
        let out = std.mem.zeroes(TestResponseItem);
        out.byte_offset = reader.readValue(i32)?;
        out.label = reader.readValue(StringPointer)?;
        out.kind = reader.readValue(TestKind)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeInt(_.byte_offset)?;
        writer.writeValue(@typeOf(_.label), _.label)?;
        writer.writeEnum(_.kind)?;
    }
}

pub struct GetTestsResponse {
    tests: []const TestResponseItem,
    contents: []const u8,

    pub static fn decode(reader: auto): GetTestsResponse! {
        let out = std.mem.zeroes(GetTestsResponse);
        out.tests = reader.readArray(TestResponseItem)?;
        out.contents = reader.readArray(u8)?;
        return out;
    }

    pub fn encode(writer: auto)! {
        writer.writeArray(TestResponseItem, _.tests)?;
        writer.writeArray(u8, _.contents)?;
    }
}

pub struct api {
    pub const Loader = root.Loader;
    pub const FrameworkEntryPointType = root.FrameworkEntryPointType;
    pub const StackFrameScope = root.StackFrameScope;
    pub const StackFrame = root.StackFrame;
    pub const StackFramePosition = root.StackFramePosition;
    pub const SourceLine = root.SourceLine;
    pub const StackTrace = root.StackTrace;
    pub const JsException = root.JsException;
    pub const FallbackStep = root.FallbackStep;
    pub const Problems = root.Problems;
    pub const Router = root.Router;
    pub const FallbackMessageContainer = root.FallbackMessageContainer;
    pub const ResolveMode = root.ResolveMode;
    pub const Target = root.Target;
    pub const CssInJsBehavior = root.CssInJsBehavior;
    pub const JsxRuntime = root.JsxRuntime;
    pub const Jsx = root.Jsx;
    pub const StringPointer = root.StringPointer;
    pub const JavascriptBundledModule = root.JavascriptBundledModule;
    pub const JavascriptBundledPackage = root.JavascriptBundledPackage;
    pub const JavascriptBundle = root.JavascriptBundle;
    pub const JavascriptBundleContainer = root.JavascriptBundleContainer;
    pub const ScanDependencyMode = root.ScanDependencyMode;
    pub const ModuleImportType = root.ModuleImportType;
    pub const ModuleImportRecord = root.ModuleImportRecord;
    pub const Module = root.Module;
    pub const StringMap = root.StringMap;
    pub const LoaderMap = root.LoaderMap;
    pub const DotEnvBehavior = root.DotEnvBehavior;
    pub const EnvConfig = root.EnvConfig;
    pub const LoadedEnvConfig = root.LoadedEnvConfig;
    pub const FrameworkConfig = root.FrameworkConfig;
    pub const FrameworkEntryPoint = root.FrameworkEntryPoint;
    pub const FrameworkEntryPointMap = root.FrameworkEntryPointMap;
    pub const FrameworkEntryPointMessage = root.FrameworkEntryPointMessage;
    pub const LoadedFramework = root.LoadedFramework;
    pub const LoadedRouteConfig = root.LoadedRouteConfig;
    pub const RouteConfig = root.RouteConfig;
    pub const UnhandledRejections = root.UnhandledRejections;
    pub const TransformOptions = root.TransformOptions;
    pub const SourceMapMode = root.SourceMapMode;
    pub const PackagesMode = root.PackagesMode;
    pub const FileHandle = root.FileHandle;
    pub const Transform = root.Transform;
    pub const Scan = root.Scan;
    pub const ScanResult = root.ScanResult;
    pub const ScannedImport = root.ScannedImport;
    pub const ImportKind = root.ImportKind;
    pub const TransformResponseStatus = root.TransformResponseStatus;
    pub const OutputFile = root.OutputFile;
    pub const TransformResponse = root.TransformResponse;
    pub const MessageLevel = root.MessageLevel;
    pub const Location = root.Location;
    pub const MessageData = root.MessageData;
    pub const MessageMeta = root.MessageMeta;
    pub const Message = root.ApiMessage;
    pub const Log = root.Log;
    pub const Reloader = root.Reloader;
    pub const WebsocketMessageKind = root.WebsocketMessageKind;
    pub const WebsocketCommandKind = root.WebsocketCommandKind;
    pub const WebsocketMessage = root.WebsocketMessage;
    pub const WebsocketMessageWelcome = root.WebsocketMessageWelcome;
    pub const WebsocketMessageFileChangeNotification = root.WebsocketMessageFileChangeNotification;
    pub const WebsocketCommand = root.WebsocketCommand;
    pub const WebsocketCommandBuild = root.WebsocketCommandBuild;
    pub const WebsocketCommandManifest = root.WebsocketCommandManifest;
    pub const WebsocketMessageBuildSuccess = root.WebsocketMessageBuildSuccess;
    pub const WebsocketMessageBuildFailure = root.WebsocketMessageBuildFailure;
    pub const WebsocketCommandBuildWithFilePath = root.WebsocketCommandBuildWithFilePath;
    pub const WebsocketMessageResolveId = root.WebsocketMessageResolveId;
    pub const NpmRegistry = root.NpmRegistry;
    pub const NpmRegistryMap = root.NpmRegistryMap;
    pub const BunInstall = root.BunInstall;
    pub const ClientServerModule = root.ClientServerModule;
    pub const ClientServerModuleManifest = root.ClientServerModuleManifest;
    pub const GetTestsRequest = root.GetTestsRequest;
    pub const TestKind = root.TestKind;
    pub const TestResponseItem = root.TestResponseItem;
    pub const GetTestsResponse = root.GetTestsResponse;
}
