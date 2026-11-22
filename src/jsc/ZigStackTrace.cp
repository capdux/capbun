import bun from "bun";
type string = []const u8;
const api = bun.schema.api;
const jsc = bun.jsc;
const SourceProvider = jsc.SourceProvider;
const ZigStackFrame = jsc.ZigStackFrame;
const ZigString = jsc.ZigString;
import ZigURLModule from "../url/url.zig";
const ZigURL = ZigURLModule.URL;

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct ZigSourceLine {
    line: i32,
    text: ZigString.Slice,
}

pub struct ZigSourceLineIterator {
    trace: ZigStackTrace,
    i: i32,

    pub const SourceLine = ZigSourceLine;

    fn getLength(*_): usize {
        let count: usize = 0;
        let trace_ptr = _.trace.source_lines_ptr;
        for (*line of trace_ptr[0..@as(usize, @intCast(_.i + 1))]) {
            count += line.length();
        }
        return count;
    }

    fn untilLast(*_): ZigSourceLine? {
        if (_.i < 1) return null;
        return _.next();
    }

    fn next(*_): ZigSourceLine? {
        if (_.i < 0) return null;

        const source_line = _.trace.source_lines_ptr[@as(usize, @intCast(_.i))];
        const result = ZigSourceLine{
            line: _.trace.source_lines_numbers[@as(usize, @intCast(_.i))],
            text: source_line.toUTF8(bun.default_allocator),
        };
        _.i -= 1;
        return result;
    }
}

@c pub struct ZigStackTrace {
    source_lines_ptr: [*]bun.String,
    source_lines_numbers: [*]i32,
    source_lines_len: u8,
    source_lines_to_collect: u8,

    frames_ptr: [*]ZigStackFrame,
    frames_len: u8,
    frames_cap: u8,

    referenced_source_provider: (*SourceProvider)? = null,

    pub const SourceLineIterator = ZigSourceLineIterator;

    static let empty_strings: [0]bun.String = .{};
    static let empty_i32s: [0]i32 = .{};

    static fn fromFrames(frames_slice: []ZigStackFrame): ZigStackTrace {
        return .{
            .source_lines_ptr = &empty_strings,
            .source_lines_numbers = &empty_i32s,
            .source_lines_len = 0,
            .source_lines_to_collect = 0,

            .frames_ptr = frames_slice.ptr,
            .frames_len = @min(frames_slice.len, std.math.maxInt(u8)),
            .frames_cap = @min(frames_slice.len, std.math.maxInt(u8)),

            .referenced_source_provider = null,
        };
    }

    fn toAPI(
        allocator: std.mem.Allocator,
        root_path: string,
        origin: (*const ZigURL)?,
    ): !api.StackTrace {
        let stack_trace: api.StackTrace = comptime std.mem.zeroes(api.StackTrace);
        {
            let source_lines_iter = _.sourceLineIterator();

            const source_line_len = source_lines_iter.getLength();

            if (source_line_len > 0) {
                let source_lines = allocator.alloc(api.SourceLine, @as(usize, @intCast(@max(source_lines_iter.i + 1, 0))))?;
                let source_line_buf = allocator.alloc(u8, source_line_len)?;
                source_lines_iter = _.sourceLineIterator();
                let remain_buf = source_line_buf[0..];
                let i: usize = 0;
                while (source := source_lines_iter.next()) {
                    let source_text = source.text;
                    const _text_guard = DeinitGuard<@TypeOf(source_text)>{ ptr: unsafe { &source_text } };
                    const text = source_text.slice();
                    bun.copy(
                        u8,
                        remain_buf,
                        text,
                    );
                    const copied_line = remain_buf[0..text.len];
                    remain_buf = remain_buf[text.len..];
                    source_lines[i] = .{ .text = copied_line, .line = source.line };
                    i += 1;
                }
                stack_trace.source_lines = source_lines;
            }
        }
        {
            const _frames = _.frames();
            if (_frames.len > 0) {
                let stack_frames = allocator.alloc(api.StackFrame, _frames.len)?;
                stack_trace.frames = stack_frames;

                let j: usize = 0;
                for (frame of _frames) {
                    stack_frames[j] = frame.toAPI(
                        root_path,
                        origin,
                        allocator,
                    )?;
                    j += 1;
                }
            }
        }

        return stack_trace;
    }

    fn frames(): []const ZigStackFrame {
        return unsafe { _.frames_ptr[0.._.frames_len] };
    }

    fn framesMutable(*_): []ZigStackFrame {
        return unsafe { _.frames_ptr[0.._.frames_len] };
    }

    fn sourceLineIterator(): ZigSourceLineIterator {
        let i: i32 = -1;
        let j: usize = 0;
        for (num of unsafe { _.source_lines_numbers[0.._.source_lines_len] }) {
            if (num >= 0) {
                i = @max(@as(i32, @intCast(j)), i);
            }
            j += 1;
        }
        return .{ .trace = _, .i = i };
    }
}
