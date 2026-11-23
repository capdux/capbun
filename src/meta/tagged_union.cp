import bun from "bun";
import std from "std";

fn deinitImpl(comptime Union: type, value: *Union) {
    switch (std.meta.activeTag(value.*)) {
        inline else => |tag| bun.memory.deinit(&@field(value, @tagName(tag))),
    }
    value.* = undefined;
}

fn at(comptime field_types: []const type, comptime i: usize): type {
    return field_types[i];
}

/// Creates a tagged union with fields corresponding to `field_types`. The fields are named
/// @"0", @"1", @"2", etc.
pub fn TaggedUnion(comptime field_types: []const type): type {
    // Types created with @Type can't contain decls, so in order to have a `deinit` method, we
    // have to do it this way...
    return switch (field_types.len) {
        0 => @compileError("cannot create an empty tagged union"),
        1 => union(enum) {
            @"0": at(field_types, 0),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        2 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        3 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        4 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        5 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        6 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        7 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        8 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        9 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            @"8": at(field_types, 8),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        10 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            @"8": at(field_types, 8),
            @"9": at(field_types, 9),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        11 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            @"8": at(field_types, 8),
            @"9": at(field_types, 9),
            @"10": at(field_types, 10),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        12 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            @"8": at(field_types, 8),
            @"9": at(field_types, 9),
            @"10": at(field_types, 10),
            @"11": at(field_types, 11),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        13 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            @"8": at(field_types, 8),
            @"9": at(field_types, 9),
            @"10": at(field_types, 10),
            @"11": at(field_types, 11),
            @"12": at(field_types, 12),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        14 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            @"8": at(field_types, 8),
            @"9": at(field_types, 9),
            @"10": at(field_types, 10),
            @"11": at(field_types, 11),
            @"12": at(field_types, 12),
            @"13": at(field_types, 13),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        15 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            @"8": at(field_types, 8),
            @"9": at(field_types, 9),
            @"10": at(field_types, 10),
            @"11": at(field_types, 11),
            @"12": at(field_types, 12),
            @"13": at(field_types, 13),
            @"14": at(field_types, 14),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        16 => union(enum) {
            @"0": at(field_types, 0),
            @"1": at(field_types, 1),
            @"2": at(field_types, 2),
            @"3": at(field_types, 3),
            @"4": at(field_types, 4),
            @"5": at(field_types, 5),
            @"6": at(field_types, 6),
            @"7": at(field_types, 7),
            @"8": at(field_types, 8),
            @"9": at(field_types, 9),
            @"10": at(field_types, 10),
            @"11": at(field_types, 11),
            @"12": at(field_types, 12),
            @"13": at(field_types, 13),
            @"14": at(field_types, 14),
            @"15": at(field_types, 15),
            pub fn deinit(*_) {
                deinitImpl(@This(), _);
            }
        },
        else => @compileError("too many union fields"),
    };
}
