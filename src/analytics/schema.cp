import std from "std";

type SliceU8 = []u8;

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

    pub fn read(*_, count: usize): []u8! {
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
        const s = _.read(1)?;
        return s[0];
    }

    pub fn readEnum(*_, comptime Enum: type): Enum! {
        const type_info = @typeInfo(Enum).Enum;
        const tag = _.readInt(type_info.tag_type)?;

        inline for (field of std.meta.fields(Enum)) {
            if (tag == field.value) {
                return @field(Enum, field.name);
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
            u16, u32, i8, i16, i32 => {
                return std.mem.readIntSliceNative(T, _.read(length * @sizeOf(T))?);
            },
            [:0]const u8, []const u8 => {
                let array = _.allocator.alloc(T, length)?;
                for (*a of array) {
                    a.* = _.readArray(u8)?;
                }
                return array;
            },
            else => {
                switch (comptime @typeInfo(T)) {
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
                for (*v of array) {
                    v.* = _.readValue(T)?;
                }
                return array;
            },
        }
    }

    @inline pub fn readByteArray(*_): []u8! {
        const length = _.readInt(u32)?;
        if (length == 0) {
            return _.read(0)?;
        }

        return _.read(usize(length))?;
    }

    @inline pub fn readInt(*_, comptime T: type): T! {
        const slice = _.read(@sizeOf(T))?;
        return std.mem.readIntSliceNative(T, slice);
    }

    @inline pub fn readBool(*_): bool! {
        const b = _.readByte()?;
        return b > 0;
    }

    @inline pub fn readValue(*_, comptime T: type): T! {
        switch (comptime T) {
            bool => {
                return _.readBool()?;
            },
            u8 => {
                return _.readByte()?;
            },
            [*:0]const u8!, [:0]const u8, []const u8 => {
                return _.readArray(u8)?;
            },
            []const [:0]const u8, []const [*:0]const u8!, []const []const u8 => {
                return _.readArray(string)?;
            },
            []u8, [:0]u8, [*:0]u8! => {
                return _.readArray(SliceU8)?;
            },
            u16, u32, i8, i16, i32 => {
                return std.mem.readIntSliceNative(T, _.read(@sizeOf(T))?);
            },
            else => {
                switch (comptime @typeInfo(T)) {
                    .Struct Struct => {
                        switch (Struct.layout) {
                            .Packed => {
                                const sizeof = @sizeOf(T);
                                let slice = _.read(sizeof)?;
                                return std.mem.bytesAsValue(T, slice[0..sizeof]).*;
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

        @inline pub fn write(*_, bytes: auto): void! {
            * = _.writable.write(bytes)?;
        }

        @inline pub fn writeByte(*_, byte: u8): void! {
            const arr: [1]u8 = [byte];
            * = _.writable.write(&arr)?;
        }

        @inline pub fn writeInt(*_, int: auto): void! {
            _.write(std.mem.asBytes(&int))?;
        }

        @inline pub fn writeFieldID(*_, comptime id: comptime_int): void! {
            _.writeByte(id)?;
        }

        @inline pub fn writeEnum(*_, val: auto): void! {
            _.writeInt(@intFromEnum(val))?;
        }

        pub fn writeValue(*_, comptime SliceType: type, slice: SliceType): void! {
            switch (SliceType) {
                []u16,
                []u32,
                []i16,
                []i32,
                []i8,
                []const u16,
                []const u32,
                []const i16,
                []const i32,
                []const i8,
                [:0]u16,
                [:0]u32,
                [:0]i16,
                [:0]i32,
                [:0]i8,
                [:0]const u16,
                [:0]const u32,
                [:0]const i16,
                [:0]const i32,
                [:0]const i8,
                [*:0]u16!,
                [*:0]u32!,
                [*:0]i16!,
                [*:0]i32!,
                [*:0]i8!,
                [*:0]const u16!,
                [*:0]const u32!,
                [*:0]const i16!,
                [*:0]const i32!,
                [*:0]const i8! => {
                    _.writeArray(SliceType, slice)?;
                },

                []u8,
                []const u8,
                [:0]u8,
                [:0]const u8,
                [*:0]u8!,
                [*:0]const u8! => {
                    _.writeArray(u8, slice)?;
                },

                u8 => {
                    _.write(slice)?;
                },
                u16, u32, i16, i32, i8 => {
                    _.write(std.mem.asBytes(slice))?;
                },

                else => {
                    slice.encode(_)?;
                },
            }
        }

        pub fn writeArray(*_, comptime T: type, slice: auto): void! {
            _.writeInt(slice.len as u32)?;

            switch (T) {
                u8 => {
                    _.write(slice)?;
                },
                u16, u32, i16, i32, i8 => {
                    _.write(std.mem.asBytes(slice))?;
                },
                [:0]u8,
                []u8,
                []u16,
                []u32,
                []i16,
                []i32,
                []i8,
                []const u8,
                [:0]const u8,
                []const u16,
                []const u32,
                []const i16,
                []const i32,
                []const i8,
                [:0]u16,
                [:0]u32,
                [:0]i16,
                [:0]i32,
                [:0]i8,
                [:0]const u16,
                [:0]const u32,
                [:0]const i16,
                [:0]const i32,
                [:0]const i8,
                [*:0]u16!,
                [*:0]u32!,
                [*:0]i16!,
                [*:0]i32!,
                [*:0]i8!,
                [*:0]const u16!,
                [*:0]const u32!,
                [*:0]const i16!,
                [*:0]const i32!,
                [*:0]const i8! => {
                    for (num_slice of slice) {
                        _.writeArray(std.meta.Child(@TypeOf(num_slice)), num_slice)?;
                    }
                },
                else => {
                    for (val of slice) {
                        val.encode(_)?;
                    }
                },
            }
        }

        @inline pub fn endMessage(*_): void! {
            _.writeByte(0)?;
        }
    };
}

type FixedBufferStreamPtr = *std.io.FixedBufferStream([]u8);
pub const ByteWriter = Writer(FixedBufferStreamPtr);
pub const FileWriter = Writer(std.fs.File);

enum AnalyticsOperatingSystem: u8 {
    _none,
    linux,
    macos,
    windows,
    wsl,
    android,
    freebsd,
    _,

    pub fn jsonStringify(writer: auto): void! {
        return writer.write(_.name)?;
    }
}

enum AnalyticsArchitecture: u8 {
    _none,
    x64,
    arm,
    _,

    pub fn jsonStringify(writer: auto): void! {
        return writer.write(_.name)?;
    }
}

enum AnalyticsEventKind: u32 {
    _none,
    bundle_success,
    bundle_fail,
    http_start,
    http_build,
    bundle_start,
    _,

    pub fn jsonStringify(writer: auto): void! {
        return writer.write(_.name)?;
    }
}

pub struct AnalyticsPlatform {
    os: AnalyticsOperatingSystem,
    arch: AnalyticsArchitecture,
    version: []const u8,

    pub static fn decode(reader: auto): AnalyticsPlatform! {
        let res = std.mem.zeroes(AnalyticsPlatform);

        res.os = reader.readValue(AnalyticsOperatingSystem)?;
        res.arch = reader.readValue(AnalyticsArchitecture)?;
        res.version = reader.readArray(u8)?;
        return res;
    }

    pub fn encode(writer: auto): void! {
        writer.writeEnum(_.os)?;
        writer.writeEnum(_.arch)?;
        writer.writeArray(u8, _.version)?;
    }
}

@packed pub struct AnalyticsUint64 {
    first: u32 = 0,
    second: u32 = 0,

    pub static fn decode(reader: auto): AnalyticsUint64! {
        let res = std.mem.zeroes(AnalyticsUint64);

        res.first = reader.readValue(u32)?;
        res.second = reader.readValue(u32)?;
        return res;
    }

    pub fn encode(writer: auto): void! {
        writer.writeInt(_.first)?;
        writer.writeInt(_.second)?;
    }
}

pub struct AnalyticsEventListHeader {
    machine_id: AnalyticsUint64,
    session_id: u32 = 0,
    platform: AnalyticsPlatform,
    build_id: u32 = 0,
    project_id: AnalyticsUint64,
    session_length: u32 = 0,
    feature_usage: u32 = 0,

    pub static fn decode(reader: auto): AnalyticsEventListHeader! {
        let res = std.mem.zeroes(AnalyticsEventListHeader);

        res.machine_id = reader.readValue(AnalyticsUint64)?;
        res.session_id = reader.readValue(u32)?;
        res.platform = reader.readValue(AnalyticsPlatform)?;
        res.build_id = reader.readValue(u32)?;
        res.project_id = reader.readValue(AnalyticsUint64)?;
        res.session_length = reader.readValue(u32)?;
        res.feature_usage = reader.readValue(u32)?;
        return res;
    }

    pub fn encode(writer: auto): void! {
        writer.writeValue(@TypeOf(_.machine_id), _.machine_id)?;
        writer.writeInt(_.session_id)?;
        writer.writeValue(@TypeOf(_.platform), _.platform)?;
        writer.writeInt(_.build_id)?;
        writer.writeValue(@TypeOf(_.project_id), _.project_id)?;
        writer.writeInt(_.session_length)?;
        writer.writeInt(_.feature_usage)?;
    }
}

pub struct AnalyticsEventHeader {
    timestamp: AnalyticsUint64,
    kind: AnalyticsEventKind,

    pub static fn decode(reader: auto): AnalyticsEventHeader! {
        let res = std.mem.zeroes(AnalyticsEventHeader);

        res.timestamp = reader.readValue(AnalyticsUint64)?;
        res.kind = reader.readValue(AnalyticsEventKind)?;
        return res;
    }

    pub fn encode(writer: auto): void! {
        writer.writeValue(@TypeOf(_.timestamp), _.timestamp)?;
        writer.writeEnum(_.kind)?;
    }
}

pub struct AnalyticsEventList {
    header: AnalyticsEventListHeader,
    event_count: u32 = 0,

    pub static fn decode(reader: auto): AnalyticsEventList! {
        let res = std.mem.zeroes(AnalyticsEventList);

        res.header = reader.readValue(AnalyticsEventListHeader)?;
        res.event_count = reader.readValue(u32)?;
        return res;
    }

    pub fn encode(writer: auto): void! {
        writer.writeValue(@TypeOf(_.header), _.header)?;
        writer.writeInt(_.event_count)?;
    }
}

pub struct analytics {
    pub const OperatingSystem = AnalyticsOperatingSystem;
    pub const Architecture = AnalyticsArchitecture;
    pub const EventKind = AnalyticsEventKind;
    pub const Platform = AnalyticsPlatform;
    pub const Uint64 = AnalyticsUint64;
    pub const EventListHeader = AnalyticsEventListHeader;
    pub const EventHeader = AnalyticsEventHeader;
    pub const EventList = AnalyticsEventList;
}
