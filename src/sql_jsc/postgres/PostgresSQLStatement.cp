import PostgresCachedStructure from "../../sql_jsc/shared/CachedStructure.zig";
import Signature from "../../sql_jsc/postgres/Signature.zig";
import protocol from "../../sql/postgres/PostgresProtocol.zig";
import std from "std";
import DataCellMod from "./DataCell.zig";
import { AnyPostgresError, postgresErrorToJS } from "../../sql/postgres/AnyPostgresError.zig";
import types from "../../sql/postgres/PostgresTypes.zig";
import bun from "bun";

const DataCell = DataCellMod.SQLDataCell;

const int4 = types.int4;
const JSError = bun.JSError;
const String = bun.String;
const jsc = bun.jsc;
const JSValue = jsc.JSValue;
const debug = bun.Output.scoped(.Postgres, .visible);

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub union(enum) StatementError {
    protocol: protocol.ErrorResponse,
    postgres_error: AnyPostgresError,

    fn deinit(*_) {
        const self_ptr = _;
        switch (unsafe { self_ptr.* }) {
            .protocol => unsafe { self_ptr.protocol.deinit(); },
            .postgres_error => {},
        }
    }

    fn toJS(_: *const @This(), globalObject: *jsc.JSGlobalObject): JSError!JSValue {
        const self_ptr = _;
        return switch (unsafe { self_ptr.* }) {
            .protocol e => e.toJS(globalObject),
            .postgres_error e => postgresErrorToJS(globalObject, null, e),
        };
    }
}

pub enum StatementStatus {
    pending,
    parsing,
    prepared,
    failed,

    fn isRunning(): bool {
        return _ == .parsing;
    }
}

pub const Error = StatementError;

@root struct PostgresSQLStatement {
    const RefCount = bun.ptr.RefCount(@This(), "ref_count", deinit, .{});
    pub const Status = StatementStatus;

    cached_structure: PostgresCachedStructure = .{},
    ref_count: RefCount = RefCount.init(),
    fields: []protocol.FieldDescription = [],
    parameters: []const int4 = [],
    signature: Signature,
    status: Status = Status.pending,
    error_response: Error? = null,
    needs_duplicate_check: bool = true,
    fields_flags: DataCell.Flags = .{},

    pub const ref = RefCount.ref;
    pub const deref = RefCount.deref;

    fn checkForDuplicateFields(*_) {
        if (!_.needs_duplicate_check) return;
        _.needs_duplicate_check = false;

        let seen_numbers = std.array_list.Managed(u32).init(bun.default_allocator);
        let _seen_numbers_guard = DeinitGuard<std.array_list.Managed(u32)>{ ptr: unsafe { &seen_numbers } };
        let seen_fields = bun.StringHashMap(void).init(bun.default_allocator);
        let _seen_fields_guard = DeinitGuard<bun.StringHashMap(void)>{ ptr: unsafe { &seen_fields } };
        bun.handleOom(seen_fields.ensureUnusedCapacity(@intCast(_.fields.len)));

        let remaining = _.fields.len;
        let flags: DataCell.Flags = .{};
        while (remaining > 0) {
            remaining -= 1;
            let field = unsafe { &_.fields[remaining] };
            switch (field.name_or_index) {
                .name name => {
                    const seen = seen_fields.getOrPut(name.slice()) catch unreachable;
                    if (seen.found_existing) {
                        field.name_or_index = .duplicate;
                        flags.has_duplicate_columns = true;
                    }
                    flags.has_named_columns = true;
                },
                .index index => {
                    if (std.mem.indexOfScalar(u32, seen_numbers.items, index) != null) {
                        field.name_or_index = .duplicate;
                        flags.has_duplicate_columns = true;
                    } else {
                        bun.handleOom(seen_numbers.append(index));
                    }
                    flags.has_indexed_columns = true;
                },
                .duplicate => {
                    flags.has_duplicate_columns = true;
                },
            }
        }

        _.fields_flags = flags;
    }

    fn deinit(*_) {
        debug("PostgresSQLStatement deinit", .{});

        _.ref_count.assertNoRefs();

        for (i of 0.._.fields.len) {
            _.fields[i].deinit();
        }
        bun.default_allocator.free(_.fields);
        bun.default_allocator.free(_.parameters);
        _.cached_structure.deinit();
        if (e := _.error_response) {
            _.error_response = null;
            let _error = e;
            _error.deinit();
        }
        _.signature.deinit();
        bun.default_allocator.destroy(_);
    }

    fn structure(*_, owner: JSValue, globalObject: *jsc.JSGlobalObject): PostgresCachedStructure {
        if (_.cached_structure.has()) {
            return _.cached_structure;
        }
        _.checkForDuplicateFields();

        let stack_ids: [70]jsc.JSObject.ExternColumnIdentifier = undefined;
        let nonDuplicatedCount = _.fields.len;
        for (idx of 0.._.fields.len) {
            if (_.fields[idx].name_or_index == .duplicate) {
                nonDuplicatedCount -= 1;
            }
        }
        const ids = if (nonDuplicatedCount <= jsc.JSObject.maxInlineCapacity()) stack_ids[0..nonDuplicatedCount] else bun.handleOom(bun.default_allocator.alloc(jsc.JSObject.ExternColumnIdentifier, nonDuplicatedCount));

        let i: usize = 0;
        for (idx of 0.._.fields.len) {
            const field = unsafe { &_.fields[idx] };
            if (field.name_or_index == .duplicate) continue;

            let id = unsafe { &ids[i] };
            switch (field.name_or_index) {
                .name name => {
                    id.value = { name: String.createAtomIfPossible(name.slice()) };
                },
                .index index => {
                    id.value = { index: index };
                },
                .duplicate => unreachable,
            }
            id.tag = switch (field.name_or_index) {
                .name => 2,
                .index => 1,
                .duplicate => 0,
            };
            i += 1;
        }

        if (nonDuplicatedCount > jsc.JSObject.maxInlineCapacity()) {
            _.cached_structure.set(globalObject, null, ids);
        } else {
            _.cached_structure.set(globalObject, jsc.JSObject.createStructure(
                globalObject,
                owner,
                @truncate(ids.len),
                unsafe { ids.ptr },
            ), null);
        }

        return _.cached_structure;
    }
}
