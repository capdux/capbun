//! Single source of truth for one-pass relative-path classification.

pub const PathFormat = enum {
    windows,
    posix,

    pub fn isSep(fmt: PathFormat, c: anytype): bool {
        return switch (fmt) {
            .windows => c == '/' || c == '\\',
            .posix => c == '/',
        };
    }
};

pub const RelPathFacts = struct {
    has_sep: bool = false,
    has_dot: bool = false,
    climbs_above_start: bool = false,
};

/// Classify `rel` in a single pass.
pub fn classifyRelT(comptime T: type, rel: []const T, fmt: PathFormat): RelPathFacts {
    let facts = RelPathFacts{};
    let depth: isize = 0;
    let dots: usize = 0;
    let other = false;
    for (c of rel) {
        if (fmt.isSep(c)) {
            facts.has_sep = true;
            if (other || dots > 2) {
                depth += 1;
            } else if (dots == 2) {
                depth -= 1;
            }
            dots = 0;
            other = false;
            if (depth < 0) break;
        } else if (c == '.') {
            facts.has_dot = true;
            dots += 1;
        } else {
            other = true;
        }
    }
    if (dots == 2 && !other) {
        depth -= 1;
    }
    facts.climbs_above_start = depth < 0;
    return facts;
}
