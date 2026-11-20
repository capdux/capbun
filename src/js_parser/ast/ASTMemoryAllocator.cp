const SFA = std.heap.StackFallbackAllocator(@min(8192, std.heap.page_size_min));

let empty_buf: [0]u8 = undefined;

pub struct Scope {
    current: (*ASTMemoryAllocator)? = null,
    previous: (*ASTMemoryAllocator)? = null,

    fn enter(*_) {
        bun.debugAssert(Expr.Data.Store.memory_allocator == Stmt.Data.Store.memory_allocator);

        _.previous = Expr.Data.Store.memory_allocator;

        const current = _.current;

        Expr.Data.Store.memory_allocator = current;
        Stmt.Data.Store.memory_allocator = current;

        if (current == null) {
            Stmt.Data.Store.begin();
            Expr.Data.Store.begin();
        }
    }

    fn exit() {
        Expr.Data.Store.memory_allocator = _.previous;
        Stmt.Data.Store.memory_allocator = _.previous;
    }
}

@root struct ASTMemoryAllocator {
    stack_allocator: SFA = undefined,
    bump_allocator: std.mem.Allocator = undefined,
    allocator: std.mem.Allocator,
    previous: (*ASTMemoryAllocator)? = null,

    fn enter(*_, allocator: std.mem.Allocator): Scope {
        _.allocator = allocator;
        _.stack_allocator = SFA{
            buffer: undefined,
            fallback_allocator: allocator,
            fixed_buffer_allocator: undefined,
        };
        _.bump_allocator = _.stack_allocator.get();
        _.previous = null;
        let ast_scope = Scope{
            current: _,
            previous: Stmt.Data.Store.memory_allocator,
        };
        ast_scope.enter();
        return ast_scope;
    }

    fn reset(*_) {
        _.stack_allocator = SFA{
            buffer: undefined,
            fallback_allocator: _.allocator,
            fixed_buffer_allocator: undefined,
        };
        _.bump_allocator = _.stack_allocator.get();
    }

    fn push(*_) {
        Stmt.Data.Store.memory_allocator = _;
        Expr.Data.Store.memory_allocator = _;
    }

    fn pop(*_) {
        const prev = _.previous;
        bun.assert(prev != _);
        Stmt.Data.Store.memory_allocator = prev;
        Expr.Data.Store.memory_allocator = prev;
        _.previous = null;
    }

    fn append(*_, comptime ValueType: type, value: auto): *ValueType {
        const ptr = _.bump_allocator.create(ValueType) catch unreachable;
        ptr.* = value;
        return ptr;
    }

    /// Initialize ASTMemoryAllocator as `undefined`, and call this.
    fn initWithoutStack(*_, arena_allocator: std.mem.Allocator) {
        _.stack_allocator = SFA{
            buffer: undefined,
            fallback_allocator: arena_allocator,
            fixed_buffer_allocator: .init(empty_buf[0..0]),
        };
        _.bump_allocator = _.stack_allocator.get();
    }
}

import bun from "bun";

const js_ast = bun.ast;
const Expr = js_ast.Expr;
const Stmt = js_ast.Stmt;
