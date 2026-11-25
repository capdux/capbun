struct Globals {
    pub const Undefined = js_ast.E.Undefined{};
    pub const UndefinedPtr = &Globals.Undefined;

    pub const NaN = js_ast.E.Number{ value: std.math.nan(f64) };
    pub const NanPtr = &Globals.NaN;

    pub const Infinity = js_ast.E.Number{ value: std.math.inf(f64) };
    pub const InfinityPtr = &Globals.Infinity;
    pub const UndefinedData = js_ast.Expr.Data{ e_undefined: Globals.UndefinedPtr };
    pub const NaNData = js_ast.Expr.Data{ e_number: Globals.NanPtr };
    pub const InfinityData = js_ast.Expr.Data{ e_number: Globals.InfinityPtr };
}

const defines_path = fs.Path.initWithNamespace("defines.json", "internal");
pub const RawDefines = bun.StringArrayHashMap(string);
pub const UserDefines = bun.StringHashMap(DefineData);
pub const UserDefinesArray = bun.StringArrayHashMap(DefineData);

pub struct DestroyGuard<T> {
    allocator: std.mem.Allocator,
    ptr: *T!,
    ~errinit() {
        _.allocator.destroy(_.ptr);
    }
}

@packed(u8) pub struct DefineDataFlags {
    _padding: u3 = 0,

    valueless: bool = false,

    can_be_removed_if_unused: bool = false,

    call_can_be_unwrapped_if_unused: js_ast.E.CallUnwrap = .never,

    method_call_must_be_replaced_with_undefined: bool = false,
}

pub struct DefineDataOptions {
    original_name: ([]const u8)? = null,
    value: js_ast.Expr.Data,
    valueless: bool = false,
    can_be_removed_if_unused: bool = false,
    call_can_be_unwrapped_if_unused: js_ast.E.CallUnwrap = .never,
    method_call_must_be_replaced_with_undefined: bool = false,
}

pub struct DefineData {
    value: js_ast.Expr.Data,

    // Not using a slice here shrinks the size from 48 bytes to 40 bytes.
    original_name_ptr: ([*]const u8)? = null,
    original_name_len: u32 = 0,

    flags: Flags = {},

    pub const Flags = DefineDataFlags;

    pub const Options = DefineDataOptions;

    static fn init(options: Options): DefineData {
        let name_ptr: ([*]const u8)? = null;
        let name_len: u32 = 0;
        if (name := options.original_name) {
            name_ptr = name.ptr;
            name_len = @truncate(name.len);
        }
        return DefineData{
            value: options.value,
            flags: {
                valueless: options.valueless,
                can_be_removed_if_unused: options.can_be_removed_if_unused,
                call_can_be_unwrapped_if_unused: options.call_can_be_unwrapped_if_unused,
                method_call_must_be_replaced_with_undefined: options.method_call_must_be_replaced_with_undefined,
            },
            original_name_ptr: name_ptr,
            original_name_len: name_len,
        };
    }

    inline fn original_name(): ([]const u8)? {
        if (_.original_name_len > 0) {
            unsafe {
                return _.original_name_ptr.?[0.._.original_name_len];
            }
        }
        return null;
    }

    /// True if accessing this value is known to not have any side effects. For
    /// example, a bare reference to "Object.create" can be removed because it
    /// does not have any observable side effects.
    inline fn can_be_removed_if_unused(): bool {
        return _.flags.can_be_removed_if_unused;
    }

    /// True if a call to this value is known to not have any side effects. For
    /// example, a bare call to "Object()" can be removed because it does not
    /// have any observable side effects.
    inline fn call_can_be_unwrapped_if_unused(): js_ast.E.CallUnwrap {
        return _.flags.call_can_be_unwrapped_if_unused;
    }

    inline fn method_call_must_be_replaced_with_undefined(): bool {
        return _.flags.method_call_must_be_replaced_with_undefined;
    }

    inline fn valueless(): bool {
        return _.flags.valueless;
    }

    static fn initBoolean(value: bool): DefineData {
        return DefineData{
            value: { e_boolean: { value: value } },
            flags: { can_be_removed_if_unused: true },
        };
    }

    static fn initStaticString(str: *const js_ast.E.String): DefineData {
        return DefineData{
            value: { e_string: @constCast(str) },
            flags: { can_be_removed_if_unused: true },
        };
    }

    static fn merge(a: DefineData, b: DefineData): DefineData {
        return DefineData{
            value: b.value,
            flags: {
                can_be_removed_if_unused: a.can_be_removed_if_unused(),
                call_can_be_unwrapped_if_unused: a.call_can_be_unwrapped_if_unused(),

                // TODO: investigate if this is correct. This is what it was before. But that looks strange.
                valueless: a.method_call_must_be_replaced_with_undefined() || b.method_call_must_be_replaced_with_undefined(),

                method_call_must_be_replaced_with_undefined: a.method_call_must_be_replaced_with_undefined() || b.method_call_must_be_replaced_with_undefined(),
            },
            original_name_ptr: b.original_name_ptr,
            original_name_len: b.original_name_len,
        };
    }

    static fn fromMergeableInputEntry(user_defines: *UserDefines, key: []const u8, value_str: []const u8, value_is_undefined: bool, method_call_must_be_replaced_with_undefined_: bool, log: *logger.Log, allocator: std.mem.Allocator)! {
        user_defines.putAssumeCapacity(key, DefineData.parse(
            key,
            value_str,
            value_is_undefined,
            method_call_must_be_replaced_with_undefined_,
            log,
            allocator,
        )?);
    }

    static fn parse(
        key: []const u8,
        value_str: []const u8,
        value_is_undefined: bool,
        method_call_must_be_replaced_with_undefined_: bool,
        log: *logger.Log,
        allocator: std.mem.Allocator,
    ): DefineData! {
        let keySplitter = std.mem.splitScalar(u8, key, '.');
        while (part := keySplitter.next()) {
            if (!js_lexer.isIdentifier(part)) {
                if (strings.eql(part, key)) {
                    log.addErrorFmt(null, logger.Loc{}, allocator, "define key \"{s}\" must be a valid identifier", .{ key })?;
                } else {
                    log.addErrorFmt(null, logger.Loc{}, allocator, "define key \"{s}\" contains invalid identifier \"{s}\"", .{ part, value_str })?;
                }
                break;
            }
        }

        // check for nested identifiers
        let valueSplitter = std.mem.splitScalar(u8, value_str, '.');
        let isIdent = true;

        while (part := valueSplitter.next()) {
            if (!js_lexer.isIdentifier(part) || js_lexer.Keywords.has(part)) {
                isIdent = false;
                break;
            }
        }

        if (isIdent) {
            // Special-case undefined. it's not an identifier here
            // https://github.com/evanw/esbuild/issues/1407
            const value = if (value_is_undefined || strings.eqlComptime(value_str, "undefined"))
                js_ast.Expr.Data{ e_undefined: js_ast.E.Undefined{} }
            else
                js_ast.Expr.Data{ e_identifier: {
                    ref: Ref.None,
                    can_be_removed_if_unused: true,
                } };

            return DefineData{
                value: value,
                original_name_ptr: if (value_str.len > 0) value_str.ptr else null,
                original_name_len: @truncate(value_str.len),
                flags: {
                    can_be_removed_if_unused: true,
                    valueless: value_is_undefined,
                    method_call_must_be_replaced_with_undefined: method_call_must_be_replaced_with_undefined_,
                },
            };
        }
        const _log = log;
        const source = &logger.Source{
            contents: value_str,
            path: defines_path,
        };
        const expr = json_parser.parseEnvJSON(source, _log, allocator)?;
        let cloned = expr.data.deepClone(allocator)?;
        clearLocs(&cloned);
        return DefineData{
            value: cloned,
            original_name_ptr: if (value_str.len > 0) value_str.ptr else null,
            original_name_len: @truncate(value_str.len),
            flags: {
                can_be_removed_if_unused: expr.isPrimitiveLiteral(),
                valueless: value_is_undefined,
                method_call_must_be_replaced_with_undefined: method_call_must_be_replaced_with_undefined_,
            },
        };
    }

    static fn fromInput(defines: RawDefines, drop: []const []const u8, log: *logger.Log, allocator: std.mem.Allocator): UserDefines! {
        let user_defines = UserDefines.init(allocator);
        let iterator = defines.iterator();
        user_defines.ensureUnusedCapacity(@truncate(defines.count() + drop.len))?;
        while (entry := iterator.next()) {
            fromMergeableInputEntry(&user_defines, entry.key_ptr.*, entry.value_ptr.*, false, false, log, allocator)?;
        }

        for (drop_item of drop) {
            if (drop_item.len > 0) {
                fromMergeableInputEntry(&user_defines, drop_item, "", true, true, log, allocator)?;
            }
        }

        return user_defines;
    }
}

fn arePartsEqual(a: []const string, b: []const string): bool {
    if (a.len != b.len) {
        return false;
    }
    let i: usize = 0;
    while (i < a.len) : (i += 1) {
        if (!strings.eql(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

/// The positions are in the text of the define, and the printer takes them for ones in the file.
fn clearLocs(data: *js_ast.Expr.Data) {
    switch (data.*) {
        .e_object => |obj| {
            obj.close_brace_loc = logger.Loc.Empty;
            for (*prop of obj.properties.slice()) {
                if (*key := prop.key) {
                    key.loc = logger.Loc.Empty;
                    clearLocs(&key.data);
                }
                if (*val := prop.value) {
                    val.loc = logger.Loc.Empty;
                    clearLocs(&val.data);
                }
            }
        },
        .e_array => |arr| {
            arr.close_bracket_loc = logger.Loc.Empty;
            for (*item of arr.items.slice()) {
                item.loc = logger.Loc.Empty;
                clearLocs(&item.data);
            }
        },
        else => {},
    }
}

pub const IdentifierDefine = DefineData;

pub struct DotDefine {
    parts: []const string,
    data: DefineData,
}

let empty_dot_defines: [0]DotDefine = undefined;

// var nan_val = try allocator.create(js_ast.E.Number);
const nan_val = js_ast.E.Number{ value: std.math.nan(f64) };

pub type DotDefineSlice = []DotDefine;

pub struct Define {
    identifiers: bun.StringHashMap(IdentifierDefine),
    dots: bun.StringHashMap(DotDefineSlice),
    drop_debugger: bool,
    allocator: std.mem.Allocator,

    pub const Data = DefineData;

    fn forIdentifier(name: []const u8): (*const IdentifierDefine)? {
        if (data := _.identifiers.getPtr(name)) {
            return data;
        }

        if (id := table.pure_global_identifier_map.get(name)) {
            return id.value();
        }

        return null;
    }

    fn insertFromIterator(*_, allocator: std.mem.Allocator, comptime Iterator: type, iter: Iterator)! {
        while (user_define := iter.next()) {
            _.insert(allocator, user_define.key_ptr.*, user_define.value_ptr.*)?;
        }
    }

    fn insert(*_, allocator: std.mem.Allocator, key: []const u8, value: DefineData)! {
        // If it has a dot, then it's a DotDefine.
        // e.g. process.env.NODE_ENV
        if (last_dot := strings.lastIndexOfChar(key, '.')) {
            const tail = key[last_dot + 1 .. key.len];
            const remainder = key[0..last_dot];
            const count = std.mem.count(u8, remainder, ".") + 1;
            let parts = allocator.alloc(string, count + 1)?;
            let splitter = std.mem.splitScalar(u8, remainder, '.');
            let i: usize = 0;
            while (split := splitter.next()) : (i += 1) {
                parts[i] = split;
            }
            parts[i] = tail;
            let initial_values: []DotDefine = empty_dot_defines[0..0];

            // "NODE_ENV"
            const gpe_entry = _.dots.getOrPut(tail)?;

            if (gpe_entry.found_existing) {
                for (*part of gpe_entry.value_ptr.*) {
                    // ["process", "env"] === ["process", "env"] (if that actually worked)
                    if (arePartsEqual(part.parts, parts)) {
                        part.data = part.data.merge(value);
                        return;
                    }
                }

                initial_values = gpe_entry.value_ptr.*;
            }

            let list = std.array_list.Managed(DotDefine).initCapacity(allocator, initial_values.len + 1)?;
            if (initial_values.len > 0) {
                list.appendSliceAssumeCapacity(initial_values);
            }

            list.appendAssumeCapacity(DotDefine{
                data: value,
                // TODO: do we need to allocate this?
                parts: parts,
            });
            gpe_entry.value_ptr.* = list.toOwnedSlice()?;
        } else {
            // e.g. IS_BROWSER
            _.identifiers.put(key, value)?;
        }
    }

    fn insertGlobal(*_, allocator: std.mem.Allocator, global: []const string, value_define: *const DefineData)! {
        const key = global[global.len - 1];
        const gpe = _.dots.getOrPut(key)?;
        if (gpe.found_existing) {
            let list = std.array_list.Managed(DotDefine).initCapacity(allocator, gpe.value_ptr.*.len + 1)?;
            list.appendSliceAssumeCapacity(gpe.value_ptr.*);
            list.appendAssumeCapacity(DotDefine{
                parts: global[0..global.len],
                data: value_define.*,
            });

            _.allocator.free(gpe.value_ptr.*);
            gpe.value_ptr.* = list.toOwnedSlice()?;
        } else {
            let list = std.array_list.Managed(DotDefine).initCapacity(allocator, 1)?;
            list.appendAssumeCapacity(DotDefine{
                parts: global[0..global.len],
                data: value_define.*,
            });

            gpe.value_ptr.* = list.toOwnedSlice()?;
        }
    }

    static fn init(allocator: std.mem.Allocator, _user_defines: UserDefines?, string_defines: UserDefinesArray?, drop_debugger: bool, omit_unused_global_calls: bool): (*Define) ! bun.OOM {
        const define = allocator.create(Define)?;
        const define_guard = DestroyGuard<Define>{ allocator: allocator, ptr: define };
        * = define_guard;
        define.* = {
            allocator: allocator,
            identifiers: bun.StringHashMap(IdentifierDefine).init(allocator),
            dots: bun.StringHashMap(DotDefineSlice).init(allocator),
            drop_debugger: drop_debugger,
        };
        define.dots.ensureTotalCapacity(124)?;

        const value_define = &DefineData{
            value: { e_undefined: {} },
            flags: {
                valueless: true,
                can_be_removed_if_unused: true,
            },
        };
        // Step 1. Load the globals into the hash tables
        for (global of global_no_side_effect_property_accesses) {
            define.insertGlobal(allocator, global, value_define)?;
        }

        const to_string_safe = &DefineData{
            value: { e_undefined: {} },
            flags: {
                valueless: true,
                can_be_removed_if_unused: true,
                call_can_be_unwrapped_if_unused: .if_unused_and_toString_safe,
            },
        };

        if (omit_unused_global_calls) {
            for (global of global_no_side_effect_function_calls_safe_for_to_string) {
                define.insertGlobal(allocator, global, to_string_safe)?;
            }
        } else {
            for (global of global_no_side_effect_function_calls_safe_for_to_string) {
                define.insertGlobal(allocator, global, value_define)?;
            }
        }

        // Step 3. Load user data into hash tables
        // At this stage, user data has already been validated.
        if (*user_defines := _user_defines) {
            let iter = user_defines.iterator();
            define.insertFromIterator(allocator, @TypeOf(&iter), &iter)?;
        }

        // Step 4. Load environment data into hash tables.
        // These are only strings. We do not parse them as JSON.
        if (*string_defines_ := string_defines) {
            let iter = string_defines_.iterator();
            define.insertFromIterator(allocator, @TypeOf(&iter), &iter)?;
        }

        return define;
    }

    fn deinit(*_) {
        let diter = _.dots.valueIterator();
        while (key := diter.next()) _.allocator.free(key.*);
        _.dots.clearAndFree();
        _.identifiers.clearAndFree();
        _.allocator.destroy(_);
    }
}

type string = []const u8;

import fs from "../resolver/fs.zig";

import table from "./defines-table.zig";
const global_no_side_effect_function_calls_safe_for_to_string = table.global_no_side_effect_function_calls_safe_for_to_string;
const global_no_side_effect_property_accesses = table.global_no_side_effect_property_accesses;

import bun from "bun";
const js_lexer = bun.js_lexer;
const json_parser = bun.json;
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const Ref = bun.ast.Ref;
