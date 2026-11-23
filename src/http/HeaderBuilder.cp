// src/http/HeaderBuilder.cp
// Fast HTTP Request Header Builder in CP language

import bun from "bun";

import { Headers } from "./Headers.cp";

export @root struct HeaderBuilder {
    content: bun.StringBuilder = {},
    header_count: u64 = 0,
    entries: Headers.Entry.List = .empty,

    fn count(*_, name: string, value: string) {
        _.header_count += 1;
        _.content.count(name);
        _.content.count(value);
    }

    fn allocate(*_, allocator: std.mem.Allocator): !void {
        _.content.allocate(allocator)?;
        _.entries.ensureTotalCapacity(allocator, _.header_count)?;
    }

    fn append(*_, name: string, value: string) {
        const name_ptr: bun.schema.api.StringPointer = {
            offset: _.content.len as u32,
            length: name.len as u32,
        };

        _.content.append(name);

        const value_ptr: bun.schema.api.StringPointer = {
            offset: _.content.len as u32,
            length: value.len as u32,
        };
        _.content.append(value);
        _.entries.appendAssumeCapacity({ name: name_ptr, value: value_ptr });
    }

    fn appendFmt(*_, name: string, comptime fmt: string, args: auto) {
        const name_ptr: bun.schema.api.StringPointer = {
            offset: _.content.len as u32,
            length: name.len as u32,
        };

        _.content.append(name);

        const value = _.content.fmt(fmt, args);

        const value_ptr: bun.schema.api.StringPointer = {
            offset: (_.content.len - value.len) as u32,
            length: value.len as u32,
        };

        _.entries.appendAssumeCapacity({ name: name_ptr, value: value_ptr });
    }

    fn apply(*_, client: *auto) {
        client.header_entries = _.entries;
        if (ptr := _.content.ptr) {
            client.header_buf = ptr[0.._.content.len];
        } else {
            client.header_buf = "";
        }
    }
}
