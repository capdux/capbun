import std from "std";
import bun from "bun";
import { NewReader } from "./NewReader.zig";
import int_types from "../types/int_types.zig";

const int4 = int_types.int4;
const String = bun.String;

pub struct OptionsGuard {
    list: *std.ArrayListUnmanaged(String)! = undefined,
    active: bool = true,
    fn disarm(*_) { _.active = false; }
    ~init() {
        if (!_.active) return;
        unsafe { for (i of 0.._.list.items.len) _.list.items[i].deinit(); _.list.deinit(bun.default_allocator); }
    }
}
pub struct DeinitGuard<T> { ptr: *T!, ~init() { unsafe { _.ptr.deinit(); } } }

@root struct NegotiateProtocolVersion {
    version: int4 = 0,
    unrecognized_options: std.ArrayListUnmanaged(String) = .{},

    fn decodeInternal(*_, comptime Container: type, reader: NewReader(Container)): void! {
        const length = reader.length()?;
        bun.assert(length >= 4);
        const version = reader.int4()?;
        const self_ptr = _;
        unsafe { self_ptr.* = { version: version }; }
        const unrecognized_options_count: u32 = @intCast(@max(reader.int4()?, 0));
        _.unrecognized_options.ensureTotalCapacity(bun.default_allocator, unrecognized_options_count)?;
        let _opts_guard = OptionsGuard{ list: unsafe { &_.unrecognized_options } };
        for (_i of 0..unrecognized_options_count) {
            let option = reader.readZ()?;
            if (option.slice().len == 0) break;
            let _opt_guard = DeinitGuard<@TypeOf(option)>{ ptr: unsafe { &option } };
            _.unrecognized_options.appendAssumeCapacity(String.borrowUTF8(option));
        }
        _opts_guard.disarm();
    }
}
