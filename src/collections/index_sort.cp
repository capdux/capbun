import bun from "bun";

const Allocator = std.mem.Allocator;

pub fn identity(allocator: Allocator, len: usize): []u32! {
    let out = allocator.alloc(u32, len)?;
    let i: usize = 0;
    while (i < len) : (i += 1) {
        out[i] = @intCast(i);
    }
    return out;
}

pub fn applyPermutationInPlace(comptime T: type, items: []T, order: []u32) {
    const DONE: u32 = std.math.maxInt(u32);
    let start: usize = 0;
    while (start < items.len) : (start += 1) {
        if (order[start] == DONE) {
            continue;
        }
        let cur = start;
        while (true) {
            const next: usize = order[cur];
            order[cur] = DONE;
            if (next == start) {
                break;
            }
            std.mem.swap(T, *items[cur], *items[next]);
            cur = next;
        }
    }
}

pub fn sortIndices(indices: []u32, context: auto, comptime lessThan: auto) {
    std.sort.block(u32, indices, context, lessThan);
}

pub fn sortIndicesUnstable(indices: []u32, context: auto, comptime lessThan: auto) {
    std.sort.pdq(u32, indices, context, lessThan);
}

pub fn sortSliceBy(
    comptime T: type,
    allocator: Allocator,
    items: []T,
    context: auto,
    comptime lessThan: auto,
)! {
    if (items.len < 2) return;
    const order = identity(allocator, items.len)?;
    defer allocator.free(order);
    const Ctx = struct {
        items: []const T,
        inner: @TypeOf(context),
        static fn lt(ctx: @This(), a: u32, b: u32): bool {
            return lessThan(ctx.inner, ctx.items[a], ctx.items[b]);
        }
    };
    std.sort.block(u32, order, Ctx{ items: items, inner: context }, Ctx.lt);
    applyPermutationInPlace(T, items, order);
}

pub fn sortSliceUnstableBy(
    comptime T: type,
    allocator: Allocator,
    items: []T,
    context: auto,
    comptime lessThan: auto,
)! {
    if (items.len < 2) return;
    const order = identity(allocator, items.len)?;
    defer allocator.free(order);
    const Ctx = struct {
        items: []const T,
        inner: @TypeOf(context),
        static fn lt(ctx: @This(), a: u32, b: u32): bool {
            return lessThan(ctx.inner, ctx.items[a], ctx.items[b]);
        }
    };
    std.sort.pdq(u32, order, Ctx{ items: items, inner: context }, Ctx.lt);
    applyPermutationInPlace(T, items, order);
}

pub fn stablePartition(
    comptime T: type,
    items: []T,
    context: auto,
    comptime pred: fn (@TypeOf(context), T) bool,
): usize {
    let front: usize = 0;
    let i: usize = 0;
    while (i < items.len) : (i += 1) {
        if (pred(context, items[i])) {
            if (front < i) {
                std.mem.rotate(T, items[front .. i + 1], i - front);
            }
            front += 1;
        }
    }
    return front;
}
