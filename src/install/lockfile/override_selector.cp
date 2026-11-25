import bun from "bun";
import DependencyMod from "../dependency.zig";

type string = []const u8;
const strings = bun.strings;
const Dependency = DependencyMod.Dependency;

pub struct PackageSelector {
    name: string,
    /// Empty when the rule applies to every version of `name`.
    range: string,
}

pub struct Selector {
    parent: PackageSelector?,
    target: PackageSelector,
}

pub err SelectorError {
    EmptyName,
    InvalidName,
    TooDeep,
    EmptyRange,
}

fn isBareScope(name: string): bool {
    return strings.startsWithChar(name, '@') && !strings.containsChar(name, '/');
}

pub fn parsePackageSegment(segment: string): PackageSelector ! SelectorError {
    if (segment.len == 0) {
        return error.EmptyName;
    }
    const split = Dependency.splitNameAndMaybeVersion(segment);
    const name = split[0];
    const maybe_range = split[1];
    if (name.len == 0) {
        return error.EmptyName;
    }
    if (isBareScope(name)) {
        return error.InvalidName;
    }
    const range = strings.withoutPrefixComptime(maybe_range ?? "", "npm:");
    if (range.len == 0 && segment.len > name.len) {
        return error.EmptyRange;
    }
    return PackageSelector{ name: name, range: range };
}

pub fn parseSelector(key: string): Selector ! SelectorError {
    if (strings.startsWith(key, "//")) {
        return Selector{
            parent: null,
            target: PackageSelector{
                name: key,
                range: "",
            },
        };
    }
    if (delimiter := pnpmDelimiter(key)) {
        return parsePnpm(key, delimiter);
    }
    return parseYarnPath(key);
}

/// pnpm's `parent>child` delimiter (`/[^ |@]>/`): the first `>` not preceded by a space, `|` or `@`.
fn pnpmDelimiter(key: string): usize? {
    let pos: usize = 1;
    while (pos < key.len) {
        const rel = strings.indexOfCharUsize(key[pos..], '>') ?? return null;
        const i = pos + rel;
        const prev = key[i - 1];
        if (prev == ' ' || prev == '|' || prev == '@') {
            pos = i + 1;
        } else {
            return i;
        }
    }
    return null;
}

fn parsePnpm(key: string, delimiter: usize): Selector ! SelectorError {
    const parent_raw = std.mem.trim(u8, key[0..delimiter], &std.ascii.whitespace);
    const name = std.mem.trim(u8, key[delimiter + 1 ..], &std.ascii.whitespace);
    if (name.len == 0) {
        return error.EmptyName;
    }
    if (pnpmDelimiter(name) != null) {
        return error.TooDeep;
    }
    const parent = parsePackageSegment(parent_raw)?;
    return Selector{
        parent: parent,
        target: parsePackageSegment(name)?,
    };
}

struct TokenNext {
    token: string,
    next: usize?,
}

/// Next `/`-delimited token of `key` starting at `pos`, and the position after its delimiter.
fn nextToken(key: string, pos: usize): TokenNext {
    if (i := strings.indexOfCharUsize(key[pos..], '/')) {
        return TokenNext{
            token: key[pos .. pos + i],
            next: pos + i + 1,
        };
    }
    return TokenNext{
        token: key[pos..],
        next: null,
    };
}

fn parseYarnPath(key: string): Selector ! SelectorError {
    let segments: [2]string = [ "", "" ];
    let count: usize = 0;
    let cursor: usize? = 0;
    while (start := cursor) {
        const tn = nextToken(key, start);
        const token = tn.token;
        const next = tn.next;
        cursor = next;
        if (token.len == 0) {
            return error.EmptyName;
        }
        if (strings.eqlComptime(token, "**")) {
            if (next == null) {
                return error.EmptyName;
            }
            continue;
        }
        let end = start + token.len;
        if (strings.startsWithChar(token, '@')) {
            const name_start = next ?? return error.InvalidName;
            const tn2 = nextToken(key, name_start);
            const name = tn2.token;
            cursor = tn2.next;
            if (name.len == 0) {
                return error.EmptyName;
            }
            end = name_start + name.len;
        }
        if (count == segments.len) {
            return error.TooDeep;
        }
        segments[count] = key[start..end];
        count += 1;
    }

    if (count == 0) {
        return error.EmptyName;
    }
    if (count == 1) {
        return Selector{
            parent: null,
            target: parsePackageSegment(segments[0])?,
        };
    }
    const parent = parsePackageSegment(segments[0])?;
    return Selector{
        parent: parent,
        target: parsePackageSegment(segments[1])?,
    };
}
