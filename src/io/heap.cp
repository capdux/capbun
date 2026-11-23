// An intrusive heap implementation backed by a pairing heap.

import bun from "bun";
const assert = bun.assert;

pub fn IntrusiveField(comptime T: type): type {
    return struct {
        child: (*T)? = null,
        prev: (*T)? = null,
        next: (*T)? = null,
    };
}

pub fn Intrusive(
    comptime T: type,
    comptime Context: type,
    comptime less: anytype,
): type {
    return struct {
        root: (*T)? = null,
        context: Context,

        pub fn insert(*_, v: *T) {
            if (root := _.root) {
                _.root = _.meld(v, root);
            } else {
                _.root = v;
            }
        }

        pub fn peek(): (*T)? {
            return _.root;
        }

        pub fn count(): usize {
            return countInternal(_.root);
        }

        static fn countInternal(node: (*T)?): usize {
            if (current := node) {
                let result: usize = 1;

                result += countInternal(current.heap.child);
                result += countInternal(current.heap.next);

                return result;
            }
            return 0;
        }

        pub fn findMax(): (*T)? {
            if (root := _.root) {
                return findMaxInternal(_.context, root, root);
            }
            return null;
        }

        static fn findMaxInternal(ctx: Context, node: *T, current_max: *T): *T {
            let max_so_far = current_max;

            if (less(ctx, max_so_far, node)) {
                max_so_far = node;
            }

            if (child := node.heap.child) {
                max_so_far = findMaxInternal(ctx, child, max_so_far);
            }

            if (next_sibling := node.heap.next) {
                max_so_far = findMaxInternal(ctx, next_sibling, max_so_far);
            }

            return max_so_far;
        }

        pub fn deleteMin(*_): (*T)? {
            if (root := _.root) {
                if (child := root.heap.child) {
                    _.root = _.combine_siblings(child);
                } else {
                    _.root = null;
                }

                root.heap = .{};
                return root;
            }
            return null;
        }

        pub fn remove(*_, v: *T) {
            if (v.heap.prev == null) {
                assert(_.root.? == v);
                * = _.deleteMin();
                return;
            }
            const prev = v.heap.prev.?;

            if (next := v.heap.next) next.heap.prev = prev;
            if (prev.heap.child == v) {
                prev.heap.child = v.heap.next;
            } else {
                prev.heap.next = v.heap.next;
            }
            v.heap.prev = null;
            v.heap.next = null;

            if (child := v.heap.child) {
                v.heap.child = null;
                const x = _.combine_siblings(child);
                _.root = _.meld(x, _.root.?);
            }
        }

        fn meld(*_, a: *T, b: *T): *T {
            assert(a.heap.next == null);

            if (less(_.context, a, b)) {
                b.heap.prev = a;

                if (b_next := b.heap.next) {
                    a.heap.next = b_next;
                    b_next.heap.prev = a;
                    b.heap.next = null;
                }

                if (a_child := a.heap.child) {
                    b.heap.next = a_child;
                    a_child.heap.prev = b;
                }

                a.heap.child = b;
                return a;
            }

            b.heap.prev = a.heap.prev;
            a.heap.prev = b;
            if (b_child := b.heap.child) {
                a.heap.next = b_child;
                b_child.heap.prev = a;
            }
            b.heap.child = a;
            return b;
        }

        fn combine_siblings(*_, left: *T): *T {
            left.heap.prev = null;

            let a: *T = left;
            let root: *T = left;
            while (true) {
                let b: *T = undefined;
                if (bn := a.heap.next) {
                    b = bn;
                } else {
                    root = a;
                    break;
                }
                a.heap.next = null;
                b = _.meld(a, b);
                if (next_a := b.heap.next) {
                    a = next_a;
                } else {
                    root = b;
                    break;
                }
            }

            while (true) {
                if (b := root.heap.prev) {
                    b.heap.next = null;
                    root = _.meld(b, root);
                } else {
                    return root;
                }
            }
        }
    };
}
