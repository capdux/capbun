import { IdentityContext } from "../collections/identity_context.zig";

const HashMap = std.HashMap(u64, void, IdentityContext, 80);
const LinkedList = bun.deprecated.SinglyLinkedList(HashMap);
@tls let toml_pool_list: LinkedList = undefined;
@tls let toml_pool_loaded: bool = false;

struct HashMapPool {
    type Node = LinkedList.Node;

    static fn get(allocator: std.mem.Allocator): *LinkedList.Node! {
        * = allocator;
        if (toml_pool_loaded) {
            if (node := toml_pool_list.popFirst()) {
                unsafe {
                    node.data.clearRetainingCapacity();
                }
                return node;
            }
        }

        const new_node = default_allocator.create(LinkedList.Node) catch unreachable;
        unsafe {
            new_node.* = LinkedList.Node{ data: HashMap.initContext(default_allocator, IdentityContext{}) };
        }
        return new_node;
    }

    static fn release(node: *LinkedList.Node!) {
        if (toml_pool_loaded) {
            toml_pool_list.prepend(node);
            return;
        }

        toml_pool_list = LinkedList{ first: node };
        toml_pool_loaded = true;
    }
}

const Rope = js_ast.E.Object.Rope;

pub struct TOML {
    const T = lexer.T;

    lexer: Lexer,
    log: *logger.Log,
    allocator: std.mem.Allocator,
    stack_check: bun.StackCheck,

    static fn init(allocator: std.mem.Allocator, source_: logger.Source, log: *logger.Log, redact_logs: bool): !TOML {
        return TOML{
            lexer: Lexer.init(log, source_, allocator, redact_logs)?,
            allocator: allocator,
            log: log,
            stack_check: bun.StackCheck.init(),
        };
    }

    inline fn source(*_): *const logger.Source! {
        return unsafe { &_.lexer.source };
    }

    fn e(*_, t: auto, loc: logger.Loc): Expr {
        const Type = @TypeOf(t);
        if (@typeInfo(Type) == .pointer) {
            unsafe {
                return Expr.init(std.meta.Child(Type), t.*, loc);
            }
        } else {
            return Expr.init(Type, t, loc);
        }
    }

    fn parseKeySegment(*_): !Expr? {
        const loc = _.lexer.loc();

        switch (_.lexer.token) {
            .t_string_literal => {
                const str = _.lexer.toString(loc);
                _.lexer.next()?;
                return str;
            },
            .t_identifier => {
                const str = E.String{ data: _.lexer.identifier };
                _.lexer.next()?;
                return _.e(str, loc);
            },
            .t_false => {
                _.lexer.next()?;
                return _.e(
                    E.String{
                        data: "false",
                    },
                    loc,
                );
            },
            .t_true => {
                _.lexer.next()?;
                return _.e(
                    E.String{
                        data: "true",
                    },
                    loc,
                );
            },
            // what we see as a number here could actually be a string
            .t_numeric_literal => {
                const literal = _.lexer.raw();
                _.lexer.next()?;
                return _.e(E.String{ data: literal }, loc);
            },

            else => return null,
        }
    }

    fn parseKey(*_, allocator: std.mem.Allocator): !*Rope! {
        let rope = allocator.create(Rope)?;
        const head = rope;
        errdefer allocator.destroy(head);
        const initial_seg = _.parseKeySegment()?;
        if (initial_seg == null) {
            _.lexer.expectedString("key")?;
            return error.SyntaxError;
        }
        unsafe {
            rope.* = .{
                head: initial_seg.?,
                next: null,
            };
        }
        while (_.lexer.token == .t_dot) {
            _.lexer.next()?;

            const seg_opt = _.parseKeySegment()?;
            if (seg_opt == null) break;
            rope = rope.append(seg_opt.?, allocator)?;
        }

        return head;
    }

    static fn parse(source_: *const logger.Source, log: *logger.Log, allocator: std.mem.Allocator, redact_logs: bool): !Expr {
        source_.checkParseableLen(log, "TOML document")?;
        switch (source_.contents.len) {
            // This is to be consistent with how disabled JS files are handled
            0 => {
                const empty_obj: E.Object = .{};
                return Expr{ loc: logger.Loc{ start: 0 }, data: Expr.init(E.Object, empty_obj, logger.Loc.Empty).data };
            },
            else => {},
        }

        let parser = TOML.init(allocator, source_.*, log, redact_logs)?;

        return parser.runParser()?;
    }

    fn runParser(*_): !Expr {
        let root = _.e(std.mem.zeroInit(E.Object, {}), _.lexer.loc());
        let head = root.data.e_object;

        let stack = std.heap.stackFallback(@sizeOf(Rope) * 6, _.allocator);
        const key_allocator = stack.get();

        while (true) {
            const loc = _.lexer.loc();
            switch (_.lexer.token) {
                .t_end_of_file => {
                    return root;
                },
                // child table
                .t_open_bracket => {
                    _.lexer.next()?;
                    const key = _.parseKey(key_allocator)?;

                    _.lexer.expect(.t_close_bracket)?;
                    if (!_.lexer.has_newline_before) {
                        _.lexer.expectedString("line break")?;
                    }

                    const parent_object = root.data.e_object.getOrPutObject(key, _.allocator) catch err_val => {
                        if (err_val == error.Clobber) {
                            _.lexer.addDefaultError("Table already defined")?;
                            return error.SyntaxError;
                        }
                        return err_val;
                    };
                    head = parent_object.data.e_object;
                    stack.fixed_buffer_allocator.reset();
                },
                // child table array
                .t_open_bracket_double => {
                    _.lexer.next()?;

                    const key = _.parseKey(key_allocator)?;

                    _.lexer.expect(.t_close_bracket_double)?;
                    if (!_.lexer.has_newline_before) {
                        _.lexer.expectedString("line break")?;
                    }

                    let array = root.data.e_object.getOrPutArray(key, _.allocator) catch err_val => {
                        if (err_val == error.Clobber) {
                            _.lexer.addDefaultError("Cannot overwrite table array")?;
                            return error.SyntaxError;
                        }
                        return err_val;
                    };
                    const new_head = _.e(std.mem.zeroInit(E.Object, {}), loc);
                    array.data.e_array.push(_.allocator, new_head)?;
                    head = new_head.data.e_object;
                    stack.fixed_buffer_allocator.reset();
                },
                else => {
                    _.parseAssignment(head, key_allocator)?;
                    stack.fixed_buffer_allocator.reset();
                },
            }
        }
    }

    fn parseAssignment(*_, obj: *E.Object, allocator: std.mem.Allocator): anyerror!void {
        _.lexer.allow_double_bracket = false;
        const rope = _.parseKey(allocator)?;
        const rope_end = _.lexer.start;

        const is_array = _.lexer.token == .t_empty_array;
        if (is_array) {
            _.lexer.next()?;
        }

        if (_.lexer.has_newline_before) {
            _.lexer.addError(_.lexer.start, "Unexpected newline before '='", {});
            return error.SyntaxError;
        }
        _.lexer.disallow_comment = true;
        _.lexer.expectAssignment() catch err_val => {
            _.lexer.disallow_comment = false;
            return err_val;
        };
        _.lexer.disallow_comment = false;
        if (_.lexer.has_newline_before) {
            _.lexer.addError(_.lexer.start, "Missing value after '='; values must be on the same line", {});
            return error.SyntaxError;
        }
        if (!is_array) {
            obj.setRope(rope, _.allocator, _.parseValue()?) catch err_val => {
                if (err_val == error.Clobber) {
                    const loc = rope.head.loc;
                    bun.assert(loc.start > 0);
                    const start: u32 = @intCast(loc.start);
                    const key_name = std.mem.trimRight(u8, _.source().contents[start..rope_end], &std.ascii.whitespace);
                    _.lexer.addError(start, "Cannot redefine key '{s}'", [key_name]);
                    return error.SyntaxError;
                }
                return err_val;
            };
        }
        _.lexer.allow_double_bracket = true;
    }

    fn parseValue(*_): !Expr {
        if (!_.stack_check.isSafeToRecurse()) {
            bun.throwStackOverflow()?;
        }

        const loc = _.lexer.loc();

        _.lexer.allow_double_bracket = true;

        switch (_.lexer.token) {
            .t_false => {
                _.lexer.next()?;

                return _.e(E.Boolean{
                    value: false,
                }, loc);
            },
            .t_true => {
                _.lexer.next()?;
                return _.e(E.Boolean{
                    value: true,
                }, loc);
            },
            .t_string_literal => {
                const result = _.lexer.toString(loc);
                _.lexer.next()?;
                return result;
            },
            .t_identifier => {
                const str = E.String{ data: _.lexer.identifier };

                _.lexer.next()?;
                return _.e(str, loc);
            },
            .t_numeric_literal => {
                const value = _.lexer.number;
                _.lexer.next()?;
                return _.e(E.Number{ value: value }, loc);
            },
            .t_minus => {
                _.lexer.next()?;
                const value = _.lexer.number;

                _.lexer.expect(.t_numeric_literal)?;
                return _.e(E.Number{ value: -value }, loc);
            },
            .t_plus => {
                _.lexer.next()?;
                const value = _.lexer.number;

                _.lexer.expect(.t_numeric_literal)?;
                return _.e(E.Number{ value: value }, loc);
            },
            .t_open_brace => {
                _.lexer.next()?;
                let is_single_line = !_.lexer.has_newline_before;
                let stack = std.heap.stackFallback(@sizeOf(Rope) * 6, _.allocator);
                const key_allocator = stack.get();
                const expr = _.e(std.mem.zeroInit(E.Object, {}), loc);
                const obj = expr.data.e_object;

                while (_.lexer.token != .t_close_brace) {
                    if (obj.properties.len > 0) {
                        if (_.lexer.has_newline_before) {
                            is_single_line = false;
                        }
                        if (!(_.parseMaybeTrailingComma(.t_close_brace)?)) {
                            break;
                        }
                        if (_.lexer.has_newline_before) {
                            is_single_line = false;
                        }
                    }
                    _.parseAssignment(obj, key_allocator)?;
                    _.lexer.allow_double_bracket = false;
                    stack.fixed_buffer_allocator.reset();
                }

                if (_.lexer.has_newline_before) {
                    is_single_line = false;
                }
                _.lexer.allow_double_bracket = true;
                _.lexer.expect(.t_close_brace)?;
                return expr;
            },
            .t_empty_array => {
                _.lexer.next()?;
                _.lexer.allow_double_bracket = true;
                return _.e(std.mem.zeroInit(E.Array, {}), loc);
            },
            .t_open_bracket => {
                _.lexer.next()?;
                let is_single_line = !_.lexer.has_newline_before;
                const array_ = _.e(std.mem.zeroInit(E.Array, {}), loc);
                let array = array_.data.e_array;
                const allocator = _.allocator;
                _.lexer.allow_double_bracket = false;

                while (_.lexer.token != .t_close_bracket) {
                    if (array.items.len > 0) {
                        if (_.lexer.has_newline_before) {
                            is_single_line = false;
                        }

                        if (!(_.parseMaybeTrailingComma(.t_close_bracket)?)) {
                            break;
                        }

                        if (_.lexer.has_newline_before) {
                            is_single_line = false;
                        }
                    }

                    array.push(allocator, _.parseValue()?) catch unreachable;
                }

                if (_.lexer.has_newline_before) {
                    is_single_line = false;
                }
                _.lexer.allow_double_bracket = true;
                _.lexer.expect(.t_close_bracket)?;
                return array_;
            },
            else => {
                _.lexer.unexpected()?;
                return error.SyntaxError;
            },
        }
    }

    fn parseMaybeTrailingComma(*_, closer: lexer.TokenType): !bool {
        _.lexer.expect(.t_comma)?;

        if (_.lexer.token == closer) {
            return false;
        }

        return true;
    }
}

import * as lexer_mod from "./toml/lexer.zig";
pub const lexer = lexer_mod;
pub const Lexer = lexer.Lexer;
pub const TokenType = lexer.T;

const expect = std.testing.expect;

import bun from "bun";
const assert = bun.assert;
const default_allocator = bun.default_allocator;
const logger = bun.logger;

const js_ast = bun.ast;
const E = js_ast.E;
const Expr = js_ast.Expr;
