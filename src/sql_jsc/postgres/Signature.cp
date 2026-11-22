import bun from "bun";
import std from "std";
import { QueryBindingIterator } from "../shared/QueryBindingIterator.zig";
import types from "../../sql/postgres/PostgresTypes.zig";

const int4 = types.int4;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;

pub struct ErrDeinitGuard<T> {
    val: T,

    ~errinit() {
        _.val.deinit();
    }
}

@root struct Signature {
    fields: []const int4,
    name: []const u8,
    query: []const u8,
    prepared_statement_name: []const u8,

    pub static fn empty(): @This() {
        const empty_fields = std.mem.zeroes([0]int4);
        return Signature{
            fields: empty_fields[0..0],
            name: "",
            query: "",
            prepared_statement_name: "",
        };
    }

    pub fn deinit(*_) {
        if (this.prepared_statement_name.len > 0) {
            bun.default_allocator.free(this.prepared_statement_name);
        }
        if (this.name.len > 0) {
            bun.default_allocator.free(this.name);
        }
        if (this.fields.len > 0) {
            bun.default_allocator.free(this.fields);
        }
        if (this.query.len > 0) {
            bun.default_allocator.free(this.query);
        }
    }

    pub fn hash(_: *const @This()): u64 {
        let hasher = std.hash.Wyhash.init(0);
        hasher.update(this.name);
        hasher.update(std.mem.sliceAsBytes(this.fields));
        return hasher.final();
    }

    pub static fn generate(globalObject: *jsc.JSGlobalObject, query: []const u8, array_value: JSValue, columns: JSValue, prepared_statement_id: u64, unnamed: bool): @This()! {
        let fields_guard = ErrDeinitGuard<std.array_list.Managed(int4)>{ val: std.array_list.Managed(int4).init(bun.default_allocator) };
        let name_guard = ErrDeinitGuard<std.array_list.Managed(u8)>{ val: std.array_list.Managed(u8).initCapacity(bun.default_allocator, query.len)? };

        name_guard.val.appendSliceAssumeCapacity(query);

        let iter = QueryBindingIterator.init(array_value, columns, globalObject)?;

        while (value := iter.next()?) {
            if (value.isEmptyOrUndefinedOrNull()) {
                fields_guard.val.append(0)?;
                name_guard.val.appendSlice(".null")?;
                continue;
            }

            const tag = types.Tag.fromJS(globalObject, value)?;

            switch (tag) {
                .int8 => name_guard.val.appendSlice(".int8")?,
                .int4 => name_guard.val.appendSlice(".int4")?,
                .int2 => name_guard.val.appendSlice(".int2")?,
                .float8 => name_guard.val.appendSlice(".float8")?,
                .float4 => name_guard.val.appendSlice(".float4")?,
                .numeric => name_guard.val.appendSlice(".numeric")?,
                .json, .jsonb => name_guard.val.appendSlice(".json")?,
                .@"bool" => name_guard.val.appendSlice(".bool")?,
                .timestamp => name_guard.val.appendSlice(".timestamp")?,
                .timestamptz => name_guard.val.appendSlice(".timestamptz")?,
                .bytea => name_guard.val.appendSlice(".bytea")?,
                else => name_guard.val.appendSlice(".string")?,
            }

            switch (tag) {
                .@"bool", .int4, .int8, .float8, .int2, .numeric, .float4, .bytea => {
                    fields_guard.val.append(@intFromEnum(tag))?;
                },
                else => {
                    fields_guard.val.append(0)?;
                },
            }
        }

        if (iter.anyFailed()) {
            return err.InvalidQueryBinding;
        }
        const prepared_statement_name = if (unnamed) "" else std.fmt.allocPrint(bun.default_allocator, "P{s}${d}", .{ name_guard.val.items[0..@min(40, name_guard.val.items.len)], prepared_statement_id })?;

        return Signature{
            prepared_statement_name: prepared_statement_name,
            name: name_guard.val.items,
            fields: fields_guard.val.items,
            query: bun.default_allocator.dupe(u8, query)?,
        };
    }
}
