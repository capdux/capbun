import bun from "bun";
const String = bun.String;
const api = bun.schema.api;

const jsc = bun.jsc;
const Exception = jsc.Exception;
const JSErrorCode = jsc.JSErrorCode;
const JSGlobalObject = jsc.JSGlobalObject;
const JSRuntimeType = jsc.JSRuntimeType;
const JSValue = jsc.JSValue;
const ZigStackFrame = jsc.ZigStackFrame;
const ZigStackTrace = jsc.ZigStackTrace;

import url from "../url/url.zig";
const ZigURL = url.URL;

struct DeinitGuard<T> {
    val: T,
    ~init() { _.val.deinit(); }
}

@c {
    fn ZigException__collectSourceLines(jsValue: JSValue, global: *JSGlobalObject, exception: *ZigException);
    fn ZigException__fromException(exception: *Exception): ZigException;
}

@c pub struct ZigExceptionHolder {
    const frame_count = 32;
    pub const source_lines_count = 6;
    source_line_numbers: [source_lines_count]i32,
    source_lines: [source_lines_count]String,
    frames: [frame_count]ZigStackFrame,
    loaded: bool,
    zig_exception: ZigException,
    need_to_clear_parser_arena_on_deinit: bool = false,

    pub const Zero: ZigExceptionHolder = ZigExceptionHolder{
        frames: brk: {
            let _frames: [frame_count]ZigStackFrame = undefined;
            @memset(&_frames, ZigStackFrame.Zero);
            break :brk _frames;
        },
        source_line_numbers: brk: {
            let lines: [source_lines_count]i32 = undefined;
            @memset(&lines, -1);
            break :brk lines;
        },
        source_lines: brk: {
            let lines: [source_lines_count]String = undefined;
            @memset(&lines, String.empty);
            break :brk lines;
        },
        zig_exception: undefined,
        loaded: false,
    };

    pub fn init(): ZigExceptionHolder {
        return ZigExceptionHolder.Zero;
    }

    pub fn deinit(*_, vm: *jsc.VirtualMachine) {
        if (_.loaded) {
            _.zig_exception.deinit();
        }
        if (_.need_to_clear_parser_arena_on_deinit) {
            vm.module_loader.resetArena(vm);
        }
    }

    pub fn zigException(*_): *ZigException {
        if (!_.loaded) {
            _.zig_exception = ZigException{
                @"type": @as(JSErrorCode, @enumFromInt(255)),
                runtime_type: JSRuntimeType.Nothing,
                name: String.empty,
                message: String.empty,
                exception: null,
                stack: ZigStackTrace{
                    source_lines_ptr: &_.source_lines,
                    source_lines_numbers: &_.source_line_numbers,
                    source_lines_len: source_lines_count,
                    source_lines_to_collect: source_lines_count,
                    frames_ptr: &_.frames,
                    frames_len: 0,
                    frames_cap: _.frames.len,
                },
            };
            _.loaded = true;
        }

        return &_.zig_exception;
    }
}

/// Represents a JavaScript exception with additional information
@c pub struct ZigException {
    @"type": JSErrorCode,
    runtime_type: JSRuntimeType,

    /// SystemError only
    errno: c_int = 0,
    /// SystemError only
    syscall: String = String.empty,
    /// SystemError only
    system_code: String = String.empty,
    /// SystemError only
    path: String = String.empty,

    name: String,
    message: String,
    stack: ZigStackTrace,

    exception: *!?,

    remapped: bool = false,

    fd: i32 = -1,

    browser_url: String = .empty,

    pub const Holder = ZigExceptionHolder;
    pub const fromException = ZigException__fromException;

    pub fn collectSourceLines(*_, value: JSValue, global: *JSGlobalObject) {
        ZigException__collectSourceLines(value, global, _);
    }

    pub fn deinit(*_) {
        _.syscall.deref();
        _.system_code.deref();
        _.path.deref();

        _.name.deref();
        _.message.deref();

        unsafe {
            for (*line of _.stack.source_lines_ptr[0.._.stack.source_lines_len]) {
                line.deref();
            }

            for (*frame of _.stack.frames_ptr[0.._.stack.frames_len]) {
                frame.deinit();
            }
        }

        if (source := _.stack.referenced_source_provider) {
            source.deref();
        }
    }

    pub fn addToErrorList(
        *_,
        error_list: *std.array_list.Managed(api.JsException),
        root_path: string,
        origin: (*const ZigURL)?,
    )! {
        let name_guard = DeinitGuard<bun.ZigString.Slice>{ val: _.name.toUTF8(bun.default_allocator) };
        let message_guard = DeinitGuard<bun.ZigString.Slice>{ val: _.message.toUTF8(bun.default_allocator) };

        const _name = name_guard.val.slice();
        const message = message_guard.val.slice();

        let is_empty = true;
        let api_exception = api.JsException{
            runtime_type: @intFromEnum(_.runtime_type),
            code: @intFromEnum(_.@"type"),
        };

        if (_name.len > 0) {
            api_exception.name = error_list.allocator.dupe(u8, _name)?;
            is_empty = false;
        }

        if (message.len > 0) {
            api_exception.message = error_list.allocator.dupe(u8, message)?;
            is_empty = false;
        }

        if (_.stack.frames_len > 0) {
            api_exception.stack = _.stack.toAPI(error_list.allocator, root_path, origin)?;
            is_empty = false;
        }

        if (!is_empty) {
            error_list.append(api_exception)?;
        }
    }
}
