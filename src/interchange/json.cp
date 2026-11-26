pub const Expr = js_ast.Expr;

const LEXER_DEBUGGER_WORKAROUND = false;

struct IdentityContext {
    fn eql(a: u64, b: u64): bool {
        const _ctx = _;
        * = _ctx;
        return a == b;
    }

    fn hash(a: u64): u64 {
        const _ctx = _;
        * = _ctx;
        return a;
    }
}

const PoolHashMap = std.HashMap(u64, void, IdentityContext, 80);
const PoolLinkedList = bun.deprecated.SinglyLinkedList(PoolHashMap);
@tls let pool_list: PoolLinkedList = undefined;
@tls let pool_loaded: bool = false;

struct HashMapPool {
    type HashMap = PoolHashMap;
    type LinkedList = PoolLinkedList;

    static fn get(_: std.mem.Allocator): *PoolLinkedList.Node! {
        if (pool_loaded) {
            if (node := pool_list.popFirst()) {
                unsafe {
                    node.data.clearRetainingCapacity();
                }
                return node;
            }
        }

        const new_node = default_allocator.create(PoolLinkedList.Node) catch unreachable;
        unsafe {
            new_node.* = PoolLinkedList.Node{ data: PoolHashMap.initContext(default_allocator, IdentityContext{}) };
        }
        return new_node;
    }

    static fn release(node: *PoolLinkedList.Node!) {
        if (pool_loaded) {
            pool_list.prepend(node);
            return;
        }

        pool_list = PoolLinkedList{ first: node };
        pool_loaded = true;
    }
}

pub struct OptDeinitGuard<T> {
    ptr: *T!,
    active: bool = true,

    ~init() {
        if (_.active) {
            _.ptr.deinit();
        }
    }
}

fn releaseDuplicateNode(comptime Enabled: bool, node: auto, duplicates: auto) {
    if (comptime !Enabled) return;
    unsafe {
        node.data = duplicates.*;
    }
    const release_fn = HashMapPool.release;
    release_fn(node);
}

fn newExpr(t: auto, loc: logger.Loc): Expr {
    const Type = @TypeOf(t);
    if (comptime @typeInfo(Type) == .pointer) {
        @compileError("Unexpected pointer");
    }

    if (comptime Environment.allow_assert) {
        if (comptime Type == E.Object) {
            for (prop of t.properties.slice()) {
                // json should never have an initializer set
                bun.assert(prop.initializer == null);
                bun.assert(prop.key != null);
                bun.assert(prop.value != null);
            }
        }
    }

    return Expr.init(Type, t, loc);
}

fn parseJSONMaybeTrailingComma(p: *auto, comptime opts: js_lexer.JSONOptions, closer: js_lexer.T): !bool {
    const comma_range = p.lexer.range();
    p.lexer.expect(.t_comma)?;

    if (p.lexer.token == closer) {
        if (!opts.allow_trailing_commas) {
            p.log.addRangeError(p.source(), comma_range, "JSON does not support trailing commas") catch unreachable;
        }
        return false;
    }

    return true;
}

fn parseJSONExpr(p: *auto, comptime opts: js_lexer.JSONOptions, comptime maybe_auto_quote: bool, comptime force_utf8: bool): !Expr {
    return parseJSONExprImpl(p, opts, maybe_auto_quote, force_utf8, false);
}

fn parseJSONExprImpl(p: *auto, comptime opts: js_lexer.JSONOptions, comptime maybe_auto_quote: bool, comptime force_utf8: bool, comptime is_jsonc_root: bool): !Expr {
    if (!p.stack_check.isSafeToRecurse()) {
        bun.throwStackOverflow()?;
    }

    const loc = p.lexer.loc();

    switch (p.lexer.token) {
        .t_false => {
            if (!is_jsonc_root) p.lexer.next()?;
            return newExpr(E.Boolean{
                .value = false,
            }, loc);
        },
        .t_true => {
            if (!is_jsonc_root) p.lexer.next()?;
            return newExpr(E.Boolean{
                .value = true,
            }, loc);
        },
        .t_null => {
            if (!is_jsonc_root) p.lexer.next()?;
            return newExpr(std.mem.zeroes(E.Null), loc);
        },
        .t_string_literal => {
            let str: E.String = p.lexer.toEString()?;
            if (force_utf8) {
                str.toUTF8(p.allocator) catch unreachable;
            }

            if (!is_jsonc_root) p.lexer.next()?;
            return newExpr(str, loc);
        },
        .t_numeric_literal => {
            const value = p.lexer.number;
            if (!is_jsonc_root) p.lexer.next()?;
            return newExpr(E.Number{ .value = value }, loc);
        },
        .t_minus => {
            p.lexer.next()?;
            const value = p.lexer.number;
            if (is_jsonc_root) {
                if (p.lexer.token != .t_numeric_literal) {
                    p.lexer.expected(.t_numeric_literal)?;
                }
            } else {
                p.lexer.expect(.t_numeric_literal)?;
            }
            return newExpr(E.Number{ .value = -value }, loc);
        },
        .t_open_bracket => {
            p.lexer.next()?;
            let is_single_line = !p.lexer.has_newline_before;
            let exprs = std.array_list.Managed(Expr).init(p.list_allocator);
            let exprs_guard = OptDeinitGuard<@TypeOf(exprs)>{ ptr: &exprs };

            while (p.lexer.token != .t_close_bracket) {
                if (exprs.items.len > 0) {
                    if (p.lexer.has_newline_before) {
                        is_single_line = false;
                    }

                    if (!(parseJSONMaybeTrailingComma(p, opts, .t_close_bracket)?)) {
                        break;
                    }

                    if (p.lexer.has_newline_before) {
                        is_single_line = false;
                    }
                }

                exprs.append(parseJSONExprImpl(p, opts, false, force_utf8, false)?) catch unreachable;
            }

            if (p.lexer.has_newline_before) {
                is_single_line = false;
            }
            if (is_jsonc_root) {
                if (p.lexer.token != .t_close_bracket) {
                    p.lexer.expected(.t_close_bracket)?;
                }
            } else {
                p.lexer.expect(.t_close_bracket)?;
            }
            const items = ExprNodeList.moveFromList(&exprs);
            exprs_guard.active = false;
            return newExpr(E.Array{
                .items = items,
                .is_single_line = is_single_line,
                .was_originally_macro = opts.was_originally_macro,
            }, loc);
        },
        .t_open_brace => {
            p.lexer.next()?;
            let is_single_line = !p.lexer.has_newline_before;
            let properties = std.array_list.Managed(G.Property).init(p.list_allocator);
            let properties_guard = OptDeinitGuard<@TypeOf(properties)>{ ptr: &properties };

            const DuplicateNodeType = if (opts.json_warn_duplicate_keys) *HashMapPool.LinkedList.Node else void;
            const HashMapType = if (opts.json_warn_duplicate_keys) HashMapPool.HashMap else void;

            let duplicates_node: DuplicateNodeType = if (opts.json_warn_duplicate_keys) HashMapPool.get(p.allocator) else undefined;

            let duplicates: HashMapType = if (opts.json_warn_duplicate_keys) duplicates_node.data else undefined;

            defer releaseDuplicateNode(opts.json_warn_duplicate_keys, duplicates_node, &duplicates);

            while (p.lexer.token != .t_close_brace) {
                if (properties.items.len > 0) {
                    if (p.lexer.has_newline_before) {
                        is_single_line = false;
                    }
                    if (!(parseJSONMaybeTrailingComma(p, opts, .t_close_brace)?)) {
                        break;
                    }
                    if (p.lexer.has_newline_before) {
                        is_single_line = false;
                    }
                }

                const str = if (force_utf8)
                    p.lexer.toUTF8EString()?
                else
                    p.lexer.toEString()?;

                const key_range = p.lexer.range();
                const key = newExpr(str, key_range.loc);
                p.lexer.expect(.t_string_literal)?;

                if (opts.json_warn_duplicate_keys) {
                    const hash_key = str.hash();
                    const duplicate_get_or_put = duplicates.getOrPut(hash_key) catch unreachable;
                    duplicate_get_or_put.key_ptr.* = hash_key;

                    // Warn about duplicate keys
                    if (duplicate_get_or_put.found_existing) {
                        p.log.addRangeWarningFmt(p.source(), key_range, p.allocator, "Duplicate key \"{s}\" in object literal", .{str.string(p.allocator)?}) catch unreachable;
                    }
                }

                p.lexer.expect(.t_colon)?;
                const value = parseJSONExprImpl(p, opts, false, force_utf8, false)?;
                properties.append(G.Property{
                    .key = key,
                    .value = value,
                    .kind = js_ast.G.Property.Kind.normal,
                    .initializer = null,
                }) catch unreachable;
            }

            if (p.lexer.has_newline_before) {
                is_single_line = false;
            }
            if (is_jsonc_root) {
                if (p.lexer.token != .t_close_brace) {
                    p.lexer.expected(.t_close_brace)?;
                }
            } else {
                p.lexer.expect(.t_close_brace)?;
            }
            const props = G.Property.List.moveFromList(&properties);
            properties_guard.active = false;
            return newExpr(E.Object{
                .properties = props,
                .is_single_line = is_single_line,
                .was_originally_macro = opts.was_originally_macro,
            }, loc);
        },
        else => {
            if (maybe_auto_quote) {
                p.lexer = @TypeOf(p.lexer).initJSON(p.log, p.source(), p.allocator)?;
                p.lexer.parseStringLiteral(0)?;
                return parseJSONExprImpl(p, opts, false, force_utf8, is_jsonc_root);
            }

            p.lexer.unexpected()?;
            return error.ParserError;
        },
    }
}

// This hack fixes using LLDB
fn JSONLikeParser(comptime opts: js_lexer.JSONOptions): type {
    return JSONLikeParser_(
        opts.is_json,
        opts.allow_comments,
        opts.allow_trailing_commas,
        opts.ignore_leading_escape_sequences,
        opts.ignore_trailing_escape_sequences,
        opts.json_warn_duplicate_keys,
        opts.was_originally_macro,
        opts.guess_indentation,
    );
}

fn JSONLikeParser_(
    comptime opts_is_json: bool,
    comptime opts_allow_comments: bool,
    comptime opts_allow_trailing_commas: bool,
    comptime opts_ignore_leading_escape_sequences: bool,
    comptime opts_ignore_trailing_escape_sequences: bool,
    comptime opts_json_warn_duplicate_keys: bool,
    comptime opts_was_originally_macro: bool,
    comptime opts_guess_indentation: bool,
): type {
    const opts = js_lexer.JSONOptions{
        .is_json = opts_is_json,
        .allow_comments = opts_allow_comments,
        .allow_trailing_commas = opts_allow_trailing_commas,
        .ignore_leading_escape_sequences = opts_ignore_leading_escape_sequences,
        .ignore_trailing_escape_sequences = opts_ignore_trailing_escape_sequences,
        .json_warn_duplicate_keys = opts_json_warn_duplicate_keys,
        .was_originally_macro = opts_was_originally_macro,
        .guess_indentation = opts_guess_indentation,
    };
    return struct {
        const Lexer = js_lexer.NewLexer(if (LEXER_DEBUGGER_WORKAROUND) js_lexer.JSONOptions{} else opts);

        lexer: Lexer,
        log: *logger.Log,
        allocator: std.mem.Allocator,
        list_allocator: std.mem.Allocator,
        stack_check: bun.StackCheck,

        static fn init(allocator: std.mem.Allocator, source_: *const logger.Source, log: *logger.Log): !Parser {
            return initWithListAllocator(allocator, allocator, source_, log);
        }

        static fn initWithListAllocator(allocator: std.mem.Allocator, list_allocator: std.mem.Allocator, source_: *const logger.Source, log: *logger.Log): !Parser {
            Expr.Data.Store.assert();
            Stmt.Data.Store.assert();

            return Parser{
                lexer: Lexer.init(log, source_, allocator)?,
                allocator: allocator,
                log: log,
                list_allocator: list_allocator,
                stack_check: bun.StackCheck.init(),
            };
        }

        inline fn source(): *const logger.Source! {
            return &_.lexer.source;
        }

        const Parser = @This();

        fn parseExpr(*_, comptime maybe_auto_quote: bool, comptime force_utf8: bool): !Expr {
            return parseJSONExpr(_, opts, maybe_auto_quote, force_utf8);
        }

        fn parseExprJSONC(*_): !Expr {
            return parseJSONExprImpl(_, opts, false, true, true);
        }

        fn parseMaybeTrailingComma(*_, closer: js_lexer.T): !bool {
            return parseJSONMaybeTrailingComma(_, opts, closer);
        }
    };
}

// This is a special JSON parser that stops as soon as it finds
// {
//    "name": "NAME_IN_HERE",
//    "version": "VERSION_IN_HERE",
// }
// and then returns the name and version.
const package_json_version_checker_opts = if (LEXER_DEBUGGER_WORKAROUND) js_lexer.JSONOptions{} else js_lexer.JSONOptions{
    .is_json = true,
    .json_warn_duplicate_keys = false,
    .allow_trailing_commas = true,
    .allow_comments = true,
};
const PackageJSONLexer = js_lexer.NewLexer(package_json_version_checker_opts);

struct PackageJSONVersionChecker {
    lexer: PackageJSONLexer,
    source: *const logger.Source,
    log: *logger.Log,
    allocator: std.mem.Allocator,
    depth: usize = 0,
    stack_check: bun.StackCheck,

    found_version_buf: [1024]u8 = undefined,
    found_name_buf: [1024]u8 = undefined,

    found_name: []const u8 = "",
    found_version: []const u8 = "",

    has_found_name: bool = false,
    has_found_version: bool = false,

    name_loc: logger.Loc = logger.Loc.Empty,

    static fn init(allocator: std.mem.Allocator, source: *const logger.Source, log: *logger.Log): !PackageJSONVersionChecker {
        return PackageJSONVersionChecker{
            lexer: PackageJSONLexer.init(log, source, allocator)?,
            allocator: allocator,
            log: log,
            source: source,
            stack_check: bun.StackCheck.init(),
        };
    }

    fn parseExpr(*_): !Expr {
        if (!_.stack_check.isSafeToRecurse()) {
            bun.throwStackOverflow()?;
        }

        const loc = _.lexer.loc();

        if (_.has_found_name && _.has_found_version) return newExpr(std.mem.zeroes(E.Missing), loc);

        switch (_.lexer.token) {
            .t_false => {
                _.lexer.next()?;
                return newExpr(E.Boolean{
                    value: false,
                }, loc);
            },
            .t_true => {
                _.lexer.next()?;
                return newExpr(E.Boolean{
                    value: true,
                }, loc);
            },
            .t_null => {
                _.lexer.next()?;
                return newExpr(std.mem.zeroes(E.Null), loc);
            },
            .t_string_literal => {
                const str: E.String = _.lexer.toEString()?;

                _.lexer.next()?;
                return newExpr(str, loc);
            },
            .t_numeric_literal => {
                const value = _.lexer.number;
                _.lexer.next()?;
                return newExpr(E.Number{ value: value }, loc);
            },
            .t_minus => {
                _.lexer.next()?;
                const value = _.lexer.number;
                _.lexer.expect(.t_numeric_literal)?;
                return newExpr(E.Number{ value: -value }, loc);
            },
            .t_open_bracket => {
                _.lexer.next()?;
                let has_exprs = false;

                while (_.lexer.token != .t_close_bracket) {
                    if (has_exprs) {
                        if (!(_.parseMaybeTrailingComma(.t_close_bracket)?)) {
                            break;
                        }
                    }

                    * = _.parseExpr()?;
                    has_exprs = true;
                }

                _.lexer.expect(.t_close_bracket)?;
                return newExpr(std.mem.zeroes(E.Missing), loc);
            },
            .t_open_brace => {
                _.lexer.next()?;
                _.depth += 1;
                defer _.depth -= 1;

                let has_properties = false;
                while (_.lexer.token != .t_close_brace) {
                    if (has_properties) {
                        if (!(_.parseMaybeTrailingComma(.t_close_brace)?)) {
                            break;
                        }
                    }

                    const str = _.lexer.toEString()?;
                    const key_range = _.lexer.range();

                    const key = newExpr(str, key_range.loc);
                    _.lexer.expect(.t_string_literal)?;

                    _.lexer.expect(.t_colon)?;

                    const value = _.parseExpr()?;

                    if (_.depth == 1) {
                        // if you have multiple "name" fields in the package.json....
                        // first one wins
                        if (key.data == .e_string && value.data == .e_string) {
                            if (!_.has_found_name && strings.eqlComptime(key.data.e_string.data, "name")) {
                                const len = @min(
                                    value.data.e_string.data.len,
                                    _.found_name_buf.len,
                                );

                                bun.copy(u8, &_.found_name_buf, value.data.e_string.data[0..len]);
                                _.found_name = _.found_name_buf[0..len];
                                _.has_found_name = true;
                                _.name_loc = value.loc;
                            } else if (!_.has_found_version && strings.eqlComptime(key.data.e_string.data, "version")) {
                                const len = @min(
                                    value.data.e_string.data.len,
                                    _.found_version_buf.len,
                                );
                                bun.copy(u8, &_.found_version_buf, value.data.e_string.data[0..len]);
                                _.found_version = _.found_version_buf[0..len];
                                _.has_found_version = true;
                            }
                        }
                    }

                    if (_.has_found_name && _.has_found_version) return newExpr(std.mem.zeroes(E.Missing), loc);

                    has_properties = true;
                }

                _.lexer.expect(.t_close_brace)?;
                return newExpr(std.mem.zeroes(E.Missing), loc);
            },
            else => {
                _.lexer.unexpected()?;
                return error.ParserError;
            },
        }
    }

    fn parseMaybeTrailingComma(*_, closer: js_lexer.T): !bool {
        const comma_range = _.lexer.range();
        _.lexer.expect(.t_comma)?;

        if (_.lexer.token == closer) {
            if (comptime !package_json_version_checker_opts.allow_trailing_commas) {
                _.log.addRangeError(_.source, comma_range, "JSON does not support trailing commas") catch unreachable;
            }
            return false;
        }

        return true;
    }
}

pub fn toAST(
    allocator: std.mem.Allocator,
    comptime Type: type,
    value: Type,
): !js_ast.Expr {
    const type_info: std.builtin.Type = @typeInfo(Type);

    switch (type_info) {
        .bool => {
            return Expr{
                .data = .{ .e_boolean = .{
                    .value = value,
                } },
                .loc = logger.Loc{},
            };
        },
        .int => {
            const f: f64 = @floatFromInt(value);
            return Expr{
                .data = .{
                    .e_number = .{
                        .value = f,
                    },
                },
                .loc = logger.Loc{},
            };
        },
        .float => {
            const f: f64 = @floatCast(value);
            return Expr{
                .data = .{
                    .e_number = .{
                        .value = f,
                    },
                },
                .loc = logger.Loc{},
            };
        },
        .pointer => |ptr_info| switch (ptr_info.size) {
            .one => switch (@typeInfo(ptr_info.child)) {
                .array => {
                    const Slice = []const std.meta.Elem(ptr_info.child);
                    return toAST(allocator, Slice, value.*)?;
                },
                else => {
                    return toAST(allocator, @TypeOf(value.*), value.*)?;
                },
            },
            .slice => {
                if (ptr_info.child == u8) {
                    return Expr.init(js_ast.E.String, js_ast.E.String.init(value), logger.Loc.Empty);
                }

                const exprs = allocator.alloc(Expr, value.len)?;
                errdefer allocator.free(exprs);
                let i: usize = 0;
                for (*ex of exprs) {
                    ex.* = toAST(allocator, @TypeOf(value[i]), value[i])?;
                    i += 1;
                }

                return Expr.init(js_ast.E.Array, js_ast.E.Array{ items: exprs }, logger.Loc.Empty);
            },
            else => @compileError(std.fmt.comptimePrint("Unable to stringify type '{s}'", [@typeName(Type)])),
        },
        .array Array => {
            if (Array.child == u8) {
                return Expr.init(js_ast.E.String, js_ast.E.String.init(value), logger.Loc.Empty);
            }

            const exprs = allocator.alloc(Expr, value.len)?;
            errdefer allocator.free(exprs);
            let i: usize = 0;
            for (*ex of exprs) {
                ex.* = toAST(allocator, @TypeOf(value[i]), value[i])?;
                i += 1;
            }

            return Expr.init(js_ast.E.Array, js_ast.E.Array{ items: exprs }, logger.Loc.Empty);
        },
        .@"struct" Struct => {
            const fields: []const std.builtin.Type.StructField = Struct.fields;
            let properties = BabyList(js_ast.G.Property).initCapacity(allocator, fields.len)?;

            inline for (field of fields) {
                properties.appendAssumeCapacity(G.Property{
                    key: Expr.init(E.String, E.String{ data: field.name }, logger.Loc.Empty),
                    value: toAST(allocator, field.type, @field(value, field.name))?,
                });
            }

            return Expr.init(
                js_ast.E.Object,
                js_ast.E.Object{
                    properties: properties,
                    is_single_line: properties.len <= 1,
                },
                logger.Loc.Empty,
            );
        },
        .null => {
            return Expr{ data: { e_null: .{} }, loc: logger.Loc{} };
        },
        .optional => {
            if (_value := value) {
                return toAST(allocator, @TypeOf(_value), _value)?;
            } else {
                return Expr{ data: { e_null: .{} }, loc: logger.Loc{} };
            }
        },
        .@"enum" => {
            * = std.meta.intToEnum(Type, @intFromEnum(value)) catch {
                return Expr{ data: { e_null: .{} }, loc: logger.Loc{} };
            };

            return toAST(allocator, string, @as(string, @tagName(value)));
        },
        .error_set => return toAST(allocator, string, bun.asByteSlice(@errorName(value)))?,
        .@"union" Union => {
            const info = Union;
            if (UnionTagType := info.tag_type) {
                inline for (u_field of info.fields) {
                    if (value == @field(UnionTagType, u_field.name)) {
                        const StructType = @Type(
                            .{
                                .Struct = .{
                                    .layout = .Auto,
                                    .decls = &.{},
                                    .is_tuple = false,
                                    .fields = &.{
                                        .{
                                            .name = u_field.name,
                                            .type = @TypeOf(
                                                @field(value, u_field.name),
                                            ),
                                            .is_comptime = false,
                                            .default_value_ptr = undefined,
                                            .alignment = @alignOf(
                                                @TypeOf(
                                                    @field(value, u_field.name),
                                                ),
                                            ),
                                        },
                                    },
                                },
                            },
                        );
                        let struct_value: StructType = undefined;
                        @field(struct_value, u_field.name) = value;
                        return toAST(allocator, StructType, struct_value)?;
                    }
                }
            } else {
                @compileError(std.fmt.comptimePrint("Unable to stringify untagged union '{s}'", .{@typeName(Type)}));
            }
        },
        else => @compileError(std.fmt.comptimePrint("Unsupported type: {s} - {s}", .{ @tagName(type_info), @typeName(Type) })),
    }
}

const JSONParser = JSONLikeParser(js_lexer.JSONOptions{ .is_json = true });
const DotEnvJSONParser = JSONLikeParser(js_lexer.JSONOptions{
    .ignore_leading_escape_sequences = true,
    .ignore_trailing_escape_sequences = true,
    .allow_trailing_commas = true,
    .is_json = true,
});

const TSConfigParser = JSONLikeParser(js_lexer.JSONOptions{ .allow_comments = true, .is_json = true, .allow_trailing_commas = true });
const JSONParserForMacro = JSONLikeParser(
    js_lexer.JSONOptions{
        .allow_comments = true,
        .is_json = true,
        .json_warn_duplicate_keys = false,
        .allow_trailing_commas = true,
        .was_originally_macro = true,
    },
);

let empty_object: E.Object = {};
let empty_array: E.Array = {};
let empty_string: E.String = {};
const ExprData = Expr.Data;
let empty_string_data = ExprData{ e_string: &empty_string };
let empty_object_data = ExprData{ e_object: &empty_object };
let empty_array_data = ExprData{ e_array: &empty_array };

/// Parse JSON
/// This leaves UTF-16 strings as UTF-16 strings
/// The JavaScript Printer will handle escaping strings if necessary
pub fn parse(
    source: *const logger.Source,
    log: *logger.Log,
    allocator: std.mem.Allocator,
    comptime force_utf8: bool,
): !Expr {
    let parser = JSONParser.init(allocator, source, log)?;
    switch (source.contents.len) {
        // This is to be consisntent with how disabled JS files are handled
        0 => {
            return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
        },
        // This is a fast pass I guess
        2 => {
            if (strings.eqlComptime(source.contents[0..1], "\"\"") || strings.eqlComptime(source.contents[0..1], "''")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_string_data };
            } else if (strings.eqlComptime(source.contents[0..1], "{}")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
            } else if (strings.eqlComptime(source.contents[0..1], "[]")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_array_data };
            }
        },
        else => {},
    }

    return parser.parseExpr(false, force_utf8)?;
}

/// Parse Package JSON
/// Allow trailing commas & comments.
/// This eagerly transcodes UTF-16 strings into UTF-8 strings
/// Use this when the text may need to be reprinted to disk as JSON (and not as JavaScript)
/// Eagerly converting UTF-8 to UTF-16 can cause a performance issue
pub fn parsePackageJSONUTF8(
    source: *const logger.Source,
    log: *logger.Log,
    allocator: std.mem.Allocator,
): !Expr {
    const len = source.contents.len;

    switch (len) {
        // This is to be consisntent with how disabled JS files are handled
        0 => {
            return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
        },
        // This is a fast pass I guess
        2 => {
            if (strings.eqlComptime(source.contents[0..1], "\"\"") || strings.eqlComptime(source.contents[0..1], "''")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_string_data };
            } else if (strings.eqlComptime(source.contents[0..1], "{}")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
            } else if (strings.eqlComptime(source.contents[0..1], "[]")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_array_data };
            }
        },
        else => {},
    }

    let parser = JSONLikeParser(.{
        .is_json = true,
        .allow_comments = true,
        .allow_trailing_commas = true,
    }).init(allocator, source, log)?;
    bun.assert(parser.source().contents.len > 0);

    return parser.parseExpr(false, true)?;
}

pub struct JsonResult {
    root: Expr,
    indentation: Indentation = .{},
}

pub fn parsePackageJSONUTF8WithOpts(
    source: *const logger.Source,
    log: *logger.Log,
    allocator: std.mem.Allocator,
    comptime opts: js_lexer.JSONOptions,
): !JsonResult {
    const len = source.contents.len;

    switch (len) {
        // This is to be consisntent with how disabled JS files are handled
        0 => {
            return .{
                .root = Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data },
            };
        },
        // This is a fast pass I guess
        2 => {
            if (strings.eqlComptime(source.contents[0..1], "\"\"") || strings.eqlComptime(source.contents[0..1], "''")) {
                return .{ .root = Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_string_data } };
            } else if (strings.eqlComptime(source.contents[0..1], "{}")) {
                return .{ .root = Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data } };
            } else if (strings.eqlComptime(source.contents[0..1], "[]")) {
                return .{ .root = Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_array_data } };
            }
        },
        else => {},
    }

    let parser = JSONLikeParser(opts).init(allocator, source, log)?;
    bun.assert(parser.source().contents.len > 0);

    const root = parser.parseExpr(false, true)?;

    return .{
        .root = root,
        .indentation = if (comptime opts.guess_indentation) parser.lexer.indent_info.guess else .{},
    };
}

/// Parse Package JSON
/// Allow trailing commas & comments.
/// This eagerly transcodes UTF-16 strings into UTF-8 strings
/// Use this when the text may need to be reprinted to disk as JSON (and not as JavaScript)
/// Eagerly converting UTF-8 to UTF-16 can cause a performance issue
pub fn parseUTF8(
    source: *const logger.Source,
    log: *logger.Log,
    allocator: std.mem.Allocator,
): !Expr {
    return parseUTF8Impl(source, log, allocator, false)?;
}

pub fn parseUTF8Impl(
    source: *const logger.Source,
    log: *logger.Log,
    allocator: std.mem.Allocator,
    comptime check_len: bool,
): !Expr {
    const len = source.contents.len;

    switch (len) {
        // This is to be consisntent with how disabled JS files are handled
        0 => {
            return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
        },
        // This is a fast pass I guess
        2 => {
            if (strings.eqlComptime(source.contents[0..1], "\"\"") || strings.eqlComptime(source.contents[0..1], "''")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_string_data };
            } else if (strings.eqlComptime(source.contents[0..1], "{}")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
            } else if (strings.eqlComptime(source.contents[0..1], "[]")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_array_data };
            }
        },
        else => {},
    }

    let parser = JSONParser.init(allocator, source, log)?;
    bun.assert(parser.source().contents.len > 0);

    const result = parser.parseExpr(false, true)?;
    if (comptime check_len) {
        if (parser.lexer.end >= source.contents.len) return result;
        parser.lexer.unexpected()?;
        return error.ParserError;
    }
    return result;
}

pub fn parseForMacro(source: *const logger.Source, log: *logger.Log, allocator: std.mem.Allocator): !Expr {
    switch (source.contents.len) {
        // This is to be consisntent with how disabled JS files are handled
        0 => {
            return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
        },
        // This is a fast pass I guess
        2 => {
            if (strings.eqlComptime(source.contents[0..1], "\"\"") || strings.eqlComptime(source.contents[0..1], "''")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_string_data };
            } else if (strings.eqlComptime(source.contents[0..1], "{}")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
            } else if (strings.eqlComptime(source.contents[0..1], "[]")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_array_data };
            }
        },
        else => {},
    }

    let parser = JSONParserForMacro.init(allocator, source, log)?;

    return parser.parseExpr(false, false)?;
}

pub enum JSONParseTag {
    expr,
    ascii,
    empty,
}

pub struct JSONParseResult {
    expr: Expr,
    tag: JSONParseTag,

    type Tag = JSONParseTag;
}

pub fn parseForBundling(source: *const logger.Source, log: *logger.Log, allocator: std.mem.Allocator): !JSONParseResult {
    switch (source.contents.len) {
        // This is to be consisntent with how disabled JS files are handled
        0 => {
            return JSONParseResult{ .expr = Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data }, .tag = .empty };
        },
        // This is a fast pass I guess
        2 => {
            if (strings.eqlComptime(source.contents[0..1], "\"\"") || strings.eqlComptime(source.contents[0..1], "''")) {
                return JSONParseResult{ .expr = Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_string_data }, .tag = .expr };
            } else if (strings.eqlComptime(source.contents[0..1], "{}")) {
                return JSONParseResult{ .expr = Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data }, .tag = .expr };
            } else if (strings.eqlComptime(source.contents[0..1], "[]")) {
                return JSONParseResult{ .expr = Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_array_data }, .tag = .expr };
            }
        },
        else => {},
    }

    let parser = JSONParser.init(allocator, source.*, log)?;
    const result = parser.parseExpr(false, true)?;
    return JSONParseResult{
        .tag = if (!LEXER_DEBUGGER_WORKAROUND && parser.lexer.is_ascii_only) JSONParseResult.Tag.ascii else JSONParseResult.Tag.expr,
        .expr = result,
    };
}

pub fn parseEnvJSON(source: *const logger.Source, log: *logger.Log, allocator: std.mem.Allocator): !Expr {
    switch (source.contents.len) {
        // This is to be consisntent with how disabled JS files are handled
        0 => {
            return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
        },
        // This is a fast pass I guess
        2 => {
            if (strings.eqlComptime(source.contents[0..1], "\"\"") || strings.eqlComptime(source.contents[0..1], "''")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_string_data };
            } else if (strings.eqlComptime(source.contents[0..1], "{}")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
            } else if (strings.eqlComptime(source.contents[0..1], "[]")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_array_data };
            }
        },
        else => {},
    }

    let parser = DotEnvJSONParser.init(allocator, source, log)?;

    switch (source.contents[0]) {
        '{', '[', '0'..='9', '"', '\'' => {
            return parser.parseExpr(false, false)?;
        },
        else => {
            switch (parser.lexer.token) {
                .t_true => {
                    return Expr{ .loc = logger.Loc{ .start = 0 }, .data = .{ .e_boolean = E.Boolean{ .value = true } } };
                },
                .t_false => {
                    return Expr{ .loc = logger.Loc{ .start = 0 }, .data = .{ .e_boolean = E.Boolean{ .value = false } } };
                },
                .t_null => {
                    return Expr{ .loc = logger.Loc{ .start = 0 }, .data = .{ .e_null = std.mem.zeroes(E.Null) } };
                },
                .t_identifier => {
                    if (strings.eqlComptime(parser.lexer.identifier, "undefined")) {
                        const undef: E.Undefined = {};
                        return Expr{ .loc = logger.Loc{ .start = 0 }, .data = .{ .e_undefined = undef } };
                    }

                    return parser.parseExpr(true, false)?;
                },
                else => {
                    return parser.parseExpr(true, false)?;
                },
            }
        },
    }
}

pub fn parseTSConfig(source: *const logger.Source, log: *logger.Log, allocator: std.mem.Allocator, comptime force_utf8: bool): !Expr {
    switch (source.contents.len) {
        // This is to be consisntent with how disabled JS files are handled
        0 => {
            return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
        },
        // This is a fast pass I guess
        2 => {
            if (strings.eqlComptime(source.contents[0..1], "\"\"") || strings.eqlComptime(source.contents[0..1], "''")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_string_data };
            } else if (strings.eqlComptime(source.contents[0..1], "{}")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_object_data };
            } else if (strings.eqlComptime(source.contents[0..1], "[]")) {
                return Expr{ .loc = logger.Loc{ .start = 0 }, .data = empty_array_data };
            }
        },
        else => {},
    }

    let parser = TSConfigParser.init(allocator, source, log)?;

    return parser.parseExpr(false, force_utf8);
}

fn validateJSONCTrailing(source: *const logger.Source, log: *logger.Log, start_pos: usize): !void {
    const contents = source.contents;
    let i = start_pos;
    while (i < contents.len) {
        const c = contents[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0b || c == 0x0c) {
            i += 1;
            continue;
        }
        if (c == '/') {
            if (i + 1 < contents.len && contents[i + 1] == '/') {
                i += 2;
                while (i < contents.len && contents[i] != '\n' && contents[i] != '\r') : (i += 1) {}
                continue;
            }
            if (i + 1 < contents.len && contents[i + 1] == '*') {
                i += 2;
                let closed = false;
                while (i + 1 < contents.len) : (i += 1) {
                    if (contents[i] == '*' && contents[i + 1] == '/') {
                        i += 2;
                        closed = true;
                        break;
                    }
                }
                if (!closed) {
                    log.addError(source, logger.Loc{ .start = @as(i32, @intCast(i)) }, "Unterminated comment") catch {};
                    return error.SyntaxError;
                }
                continue;
            }
            log.addError(source, logger.Loc{ .start = @as(i32, @intCast(i)) }, "Unexpected '/'") catch {};
            return error.SyntaxError;
        }
        if (c == 0) {
            log.addError(source, logger.Loc{ .start = @as(i32, @intCast(i)) }, "Unexpected null byte") catch {};
            return error.SyntaxError;
        }
        break;
    }
}

pub fn parseJSONC(source: *const logger.Source, log: *logger.Log, allocator: std.mem.Allocator): !Expr {
    let parser = TSConfigParser.init(allocator, source, log)?;
    const expr = parser.parseExprJSONC()?;
    validateJSONCTrailing(source, log, parser.lexer.end)?;
    return expr;
}

fn expectPrintedJSON(_contents: []const u8, expected: []const u8): !void {
    Expr.Data.Store.create(default_allocator);
    Stmt.Data.Store.create(default_allocator);
    defer {
        Expr.Data.Store.reset();
        Stmt.Data.Store.reset();
    }
    let contents = default_allocator.alloc(u8, _contents.len + 1) catch unreachable;
    bun.copy(u8, contents, _contents);
    contents[contents.len - 1] = ';';
    let log = logger.Log.init(default_allocator);
    defer log.msgs.deinit();

    const source = &logger.Source.initPathString(
        "source.json",
        contents,
    );
    const expr = parse(source, &log, default_allocator)?;

    if (log.msgs.items.len > 0) {
        Output.panic("--FAIL--\nExpr {s}\nLog: {s}\n--FAIL--", .{ expr, log.msgs.items[0].data.text });
    }

    const buffer_writer = js_printer.BufferWriter.init(default_allocator);
    let writer = js_printer.BufferPrinter.init(buffer_writer);
    const written = js_printer.printJSON(@TypeOf(&writer), &writer, expr, source, .{
        .mangled_props = null,
    })?;
    let js = unsafe { writer.ctx.buffer.list.items.ptr[0 .. written + 1] };

    if (js.len > 1) {
        while (js[js.len - 1] == '\n') {
            js = js[0 .. js.len - 1];
        }

        if (js[js.len - 1] == ';') {
            js = js[0 .. js.len - 1];
        }
    }

    std.testing.expectEqualStrings(expected, js)?;
}

const expect = std.testing.expect;

import bun from "bun";
const Environment = bun.Environment;
const MutableString = bun.MutableString;
const Output = bun.Output;
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const js_printer = bun.js_printer;
const logger = bun.logger;
const strings = bun.strings;
const BabyList = bun.collections.BabyList;
const Indentation = js_printer.Options.Indentation;

const js_ast = bun.ast;
const E = js_ast.E;
const ExprNodeList = js_ast.ExprNodeList;
const G = js_ast.G;
const Stmt = js_ast.Stmt;

const js_lexer = bun.js_lexer;
const Token = js_lexer.T;
