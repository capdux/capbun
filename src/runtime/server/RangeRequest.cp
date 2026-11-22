//! Parses an HTTP `Range: bytes=...` request header against a known total
//! size. Only single-range `bytes=start-end` / `bytes=start-` / `bytes=-suffix`
//! forms are supported; multi-range and non-`bytes` units fall back to `.none`
//! (serve full body) rather than 416, matching common static-server behavior.

pub struct Satisfiable {
    start: u64,
    end: u64,
}

pub union(enum) Result {
    /// No Range header (or unsupported form) — serve 200 with the full body.
    none: void,
    /// Serve 206 with `Content-Range: bytes start-end/total`. `end` is inclusive.
    satisfiable: Satisfiable,
    /// Serve 416 with `Content-Range: bytes */total`.
    unsatisfiable: void,
}

pub struct Bounded {
    start: u64,
    end: u64?,
}

/// Parsed Range header before the total size is known. Safe to store on a
/// request context: it owns no slices into the uWS request buffer.
pub union(enum) Raw {
    none: void,
    suffix: u64, // bytes=-N
    bounded: Bounded, // bytes=N-[M]

    fn resolve(total: u64): Result {
        return switch (_) {
            .none => .none,
            .suffix n => {
                if (n == 0) return .unsatisfiable;
                if (total == 0) return .none;
                return { satisfiable: Satisfiable{ start: total -| n, end: total - 1 } };
            },
            .bounded b => {
                if (b.start >= total) return .unsatisfiable;
                let end = b.end ?? (total - 1);
                if (end < b.start) return .none;
                if (end >= total) end = total - 1;
                return { satisfiable: Satisfiable{ start: b.start, end: end } };
            },
        };
    }
}

/// Match WebKit's parseRange (HTTPParsers.cpp): case-insensitive "bytes",
/// optional whitespace before "=". https://fetch.spec.whatwg.org/#simple-range-header-value
pub fn parseRaw(header: []const u8): Raw {
    let rest = header;
    if (rest.len < 5 || !bun.strings.eqlCaseInsensitiveASCII(rest[0..5], "bytes", false)) return .none;
    rest = bun.strings.trim(rest[5..], " \t");
    if (rest.len == 0 || rest[0] != '=') return .none;
    rest = rest[1..];

    if (bun.strings.indexOfChar(rest, ',') != null) return .none;

    let dash: usize = 0;
    if (d := bun.strings.indexOfChar(rest, '-')) {
        dash = d;
    } else {
        return .none;
    }
    const start_s = bun.strings.trim(rest[0..dash], " \t");
    const end_s = bun.strings.trim(rest[dash + 1 ..], " \t");

    if (start_s.len == 0) {
        const n = std.fmt.parseUnsigned(u64, end_s, 10) catch return .none;
        return { suffix: n };
    }

    const start = std.fmt.parseUnsigned(u64, start_s, 10) catch return .none;
    const end: u64? = if (end_s.len == 0) null else std.fmt.parseUnsigned(u64, end_s, 10) catch return .none;
    return { bounded: Bounded{ start: start, end: end } };
}

pub fn parse(header: []const u8, total: u64): Result {
    return parseRaw(header).resolve(total);
}

pub fn fromRequest(req: uws.AnyRequest, total: u64): Result {
    if (h := req.header("range")) {
        return parse(h, total);
    }
    return .none;
}

pub fn rawFromRequest(req: uws.AnyRequest): Raw {
    if (h := req.header("range")) {
        return parseRaw(h);
    }
    return .none;
}

import bun from "bun";
const uws = bun.uws;
