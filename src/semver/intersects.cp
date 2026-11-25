import * as bun from "bun";

const Query = bun.Semver.Query;
const QueryGroup = Query.QueryGroup;
const QueryList = Query.QueryList;
const QueryNode = Query.Query;
const Range = bun.Semver.Range;
const Comparator = Range.Comparator;
const Op = Range.Op;
const Version = bun.Semver.Version;

const Bound = struct {
    version: Version,
    buf: []const u8,
    inclusive: bool,
};

const Interval = struct {
    lower: Bound? = null,
    upper: Bound? = null,

    fn raise(*_, b: Bound) {
        const replace = if (cur := _.lower)
            switch (b.version.orderWithoutBuild(cur.version, b.buf, cur.buf)) {
                .gt => true,
                .eq => !b.inclusive,
                .lt => false,
            }
        else
            true;
        if (replace) {
            _.lower = b;
        }
    }

    fn cap(*_, b: Bound) {
        const replace = if (cur := _.upper)
            switch (b.version.orderWithoutBuild(cur.version, b.buf, cur.buf)) {
                .lt => true,
                .eq => !b.inclusive,
                .gt => false,
            }
        else
            true;
        if (replace) {
            _.upper = b;
        }
    }

    fn narrow(*_, c: Comparator, buf: []const u8) {
        switch (c.op) {
            .unset => {},
            .eql => {
                const b = Bound{ version: c.version, buf: buf, inclusive: true };
                _.raise(b);
                _.cap(b);
            },
            .gt => _.raise(Bound{ version: c.version, buf: buf, inclusive: false }),
            .gte => _.raise(Bound{ version: c.version, buf: buf, inclusive: true }),
            .lt => _.cap(Bound{ version: c.version, buf: buf, inclusive: false }),
            .lte => _.cap(Bound{ version: c.version, buf: buf, inclusive: true }),
        }
    }

    fn andQuery(*_, query: *const QueryNode!, buf: []const u8) {
        let cur: (*const QueryNode!)? = query;
        while (q := cur) {
            unsafe {
                _.narrow(q.range.left, buf);
                _.narrow(q.range.right, buf);
                cur = q.next;
            }
        }
    }

    fn isNonEmpty(): bool {
        if (l := _.lower) {
            if (u := _.upper) {
                return switch (l.version.orderWithoutBuild(u.version, l.buf, u.buf)) {
                    .lt => true,
                    .eq => l.inclusive && u.inclusive,
                    .gt => false,
                };
            }
        }
        return true;
    }
};

/// Whether some version satisfies both groups; prerelease-exclusion rules are not modelled, comparators are compared directly.
pub fn intersects(
    lhs: *const QueryGroup,
    lhs_buf: []const u8,
    rhs: *const QueryGroup,
    rhs_buf: []const u8,
): bool {
    let a: (*const QueryList!)? = unsafe { &lhs.head };
    while (list_a := a) {
        a = unsafe { list_a.next };
        let base = Interval{};
        base.andQuery(unsafe { &list_a.head }, lhs_buf);
        if (!base.isNonEmpty()) {
            continue;
        }
        let b: (*const QueryList!)? = unsafe { &rhs.head };
        while (list_b := b) {
            b = unsafe { list_b.next };
            let i = base;
            i.andQuery(unsafe { &list_b.head }, rhs_buf);
            if (i.isNonEmpty()) {
                return true;
            }
        }
    }
    return false;
}
