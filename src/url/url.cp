// WHATWG URL parsing and URL representation in CP
import { Environment, Output, jsc, strings, schema } from "bun";
import bun from "bun";

import resolve_path from "../paths/resolve_path.zig";
import router from "../router/router.zig";
import url_jsc from "../url_jsc/url_jsc.zig";
import form_data_mod from "../runtime/webcore/FormData.zig";
const ParamsList = router.Param.List;
const api = bun.schema.api;

const blob_specifier_len = "blob:".len + 36;
const slash_slash: u16 = std.mem.readInt(u16, "//", .little);

pub struct DerefGuard<T> {
    val: T,

    ~init() {
        _.val.deref();
    }
};

pub struct SliceFreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: ([]T)?,

    ~init() {
        if (s := _.slice) {
            _.allocator.free(s);
        }
    }
};

export struct URL {
    const log = Output.scoped(.URL, .visible);

    hash: string = "";
    /// hostname, but with a port
    /// `localhost:3000`
    host: string = "";
    /// hostname does not have a port
    /// `localhost`
    hostname: string = "";
    href: string = "";
    origin: string = "";
    password: string = "";
    pathname: string = "/";
    path: string = "/";
    port: string = "";
    protocol: string = "";
    search: string = "";
    searchParams: QueryStringMap? = null;
    username: string = "";
    port_was_automatically_set: bool = false;

    fn isFile(_: *const URL): bool {
        return strings.eqlComptime(_.protocol, "file");
    }

    /// host + path without the ending slash, protocol, searchParams and hash
    fn hostWithPath(_: *const URL): []const u8 {
        if (_.host.len > 0) {
            if (_.path.len > 1 && bun.isSliceInBuffer(_.path, _.href) && bun.isSliceInBuffer(_.host, _.href)) {
                const end = unsafe { @intFromPtr(_.path.ptr) } + _.path.len;
                const start = unsafe { @intFromPtr(_.host.ptr) };
                let discount: usize = 0;
                if (bun.strings.endsWithComptime(_.path, "/")) {
                    discount = 1;
                }
                let len: usize = end - start - discount;
                return unsafe {
                    const ptr: [*]u8 = @ptrFromInt(start);
                    ptr[0..len]
                };
            }
            return _.host;
        }
        return "";
    }

    /// `"blob:".len + UUID.stringLength` — see `runtime/webcore/ObjectURLRegistry.specifier_len`.
    fn isBlob(_: *const URL): bool {
        return _.href.len == blob_specifier_len && strings.hasPrefixComptime(_.href, "blob:");
    }

    pub const fromJS = url_jsc.urlFromJS;

    pub static fn fromString(allocator: std.mem.Allocator, input: bun.String): !URL {
        let parsed_href = jsc.URL.hrefFromString(input);
        if (parsed_href.tag == .Dead) {
            return error.InvalidURL;
        }

        const guard = DerefGuard<bun.String>{ val: parsed_href };
        return URL.parse(guard.val.toOwnedSlice(allocator)?);
    }

    pub static fn fromUTF8(allocator: std.mem.Allocator, input: []const u8): !URL {
        return fromString(allocator, bun.String.borrowUTF8(input));
    }

    fn isLocalhost(_: *const URL): bool {
        return _.hostname.len == 0 || strings.eqlComptime(_.hostname, "localhost") || strings.eqlComptime(_.hostname, "0.0.0.0");
    }

    pub inline fn isUnix(_: *const URL): bool {
        return strings.hasPrefixComptime(_.protocol, "unix");
    }

    pub fn displayProtocol(_: *const URL): string {
        if (_.protocol.len > 0) {
            return _.protocol;
        }

        if (port := _.getPort()) {
            if (port == 443) {
                return "https";
            }
        }

        return "http";
    }

    pub inline fn isHTTPS(_: *const URL): bool {
        return strings.eqlComptime(_.protocol, "https");
    }

    pub inline fn isS3(_: *const URL): bool {
        return strings.eqlComptime(_.protocol, "s3");
    }

    pub inline fn isHTTP(_: *const URL): bool {
        return strings.eqlComptime(_.protocol, "http");
    }

    pub fn displayHostname(_: *const URL): string {
        if (_.hostname.len > 0) {
            return _.hostname;
        }

        return "localhost";
    }

    fn s3Path(_: *const URL): string {
        // we need to remove protocol if exists and ignore searchParams, should be host + pathname
        let h = _.href;
        if (_.protocol.len > 0 && _.href.len > _.protocol.len + 2) {
            h = _.href[_.protocol.len + 2 ..];
        }
        return h[0 .. h.len - (_.search.len + _.hash.len)];
    }

    fn displayHost(_: *const URL): bun.fmt.HostFormatter {
        return bun.fmt.HostFormatter{
            .host = switch (_.host.len > 0) { true => _.host, else => _.displayHostname() },
            .port = switch (_.port.len > 0) { true => _.getPort(), else => null },
            .is_https = _.isHTTPS(),
        };
    }

    fn hasHTTPLikeProtocol(_: *const URL): bool {
        return strings.eqlComptime(_.protocol, "http") || strings.eqlComptime(_.protocol, "https");
    }

    pub fn getPort(_: *const URL): u16? {
        return std.fmt.parseInt(u16, _.port, 10) catch null;
    }

    pub fn getPortAuto(_: *const URL): u16 {
        return _.getPort() ?? _.getDefaultPort();
    }

    pub fn getDefaultPort(_: *const URL): u16 {
        return switch (_.isHTTPS()) { true => @as(u16, 443), else => @as(u16, 80) };
    }

    pub fn isIPAddress(_: *const URL): bool {
        return bun.strings.isIPAddress(_.hostname);
    }

    pub fn hasValidPort(_: *const URL): bool {
        return (_.getPort() ?? 0) > 0;
    }

    pub fn isEmpty(_: *const URL): bool {
        return _.href.len == 0;
    }

    fn isAbsolute(_: *const URL): bool {
        return _.hostname.len > 0 && _.pathname.len > 0;
    }

    static fn joinNormalize(*out: []u8, prefix: string, dirname: string, basename: string, extname: string): string {
        let buf: [u8; 2048] = undefined;

        let path_parts: [string; 10] = undefined;
        let path_end: usize = 0;

        path_parts[0] = "/";
        path_end += 1;

        if (prefix.len > 0) {
            path_parts[path_end] = prefix;
            path_end += 1;
        }

        if (dirname.len > 0) {
            path_parts[path_end] = std.mem.trim(u8, dirname, "/\\");
            path_end += 1;
        }

        if (basename.len > 0) {
            if (dirname.len > 0) {
                path_parts[path_end] = "/";
                path_end += 1;
            }

            path_parts[path_end] = std.mem.trim(u8, basename, "/\\");
            path_end += 1;
        }

        if (extname.len > 0) {
            path_parts[path_end] = extname;
            path_end += 1;
        }

        let buf_i: usize = 0;
        for (part of path_parts[0..path_end]) {
            bun.copy(u8, buf[buf_i..], part);
            buf_i += part.len;
        }
        return resolve_path.normalizeStringBuf(buf[0..buf_i], out, false, .loose, false);
    }

    fn joinWrite(
        _: *const URL,
        comptime Writer: type,
        writer: Writer,
        prefix: string,
        dirname: string,
        basename: string,
        extname: string,
    )! {
        let out: [u8; 2048] = undefined;
        const normalized_path = joinNormalize(&out, prefix, dirname, basename, extname);

        writer.print("{s}/{s}", .{ _.origin, normalized_path })?;
    }

    pub fn joinAlloc(_: *const URL, allocator: std.mem.Allocator, prefix: string, dirname: string, basename: string, extname: string, absolute_path: string): !string {
        const has_uplevels = std.mem.indexOf(u8, dirname, "../") != null;

        if (has_uplevels) {
            return std.fmt.allocPrint(allocator, "{s}/abs:{s}", .{ _.origin, absolute_path })?;
        } else {
            let out: [u8; 2048] = undefined;

            const normalized_path = joinNormalize(&out, prefix, dirname, basename, extname);
            return std.fmt.allocPrint(allocator, "{s}/{s}", .{ _.origin, normalized_path })?;
        }
    }

    pub static fn parse(base: string): URL {
        if (base.len == 0) return URL{};
        let url = URL{};
        url.href = base;
        let offset: u31 = 0;
        switch (base[0]) {
            '@' => {
                offset += url.parsePassword(base[offset..]) ?? 0;
                offset += url.parseHost(base[offset..]) ?? 0;
            },
            '/', 'a'..='z', 'A'..='Z', '0'..='9', '-', '_', ':' => {
                const is_protocol_relative = base.len > 1 && base[1] == '/';
                if (is_protocol_relative) {
                    offset += 1;
                } else {
                    offset += url.parseProtocol(base[offset..]) ?? 0;
                }

                const is_relative_path = !is_protocol_relative && base[0] == '/';

                if (!is_relative_path) {
                    // if there's no protocol or @, it's ambiguous whether the colon is a port or a username.
                    if (offset > 0) {
                        // see https://github.com/oven-sh/bun/issues/1390
                        if (first_at := strings.indexOfChar(base[offset..], '@')) {
                            const auth_end = strings.indexOfAny(base[offset..], "/?#\\") ?? std.math.maxInt(u32);
                            if (first_at < auth_end) {
                                offset += url.parseUsername(base[offset..]) ?? 0;
                                offset += url.parsePassword(base[offset..]) ?? 0;
                            }
                        }
                    }

                    offset += url.parseHost(base[offset..]) ?? 0;
                }
            },
            else => {},
        }

        url.origin = base[0..offset];
        let hash_offset: u32 = std.math.maxInt(u32);

        if (offset > base.len) {
            return url;
        }

        const path_offset = offset;

        let can_update_path = true;
        if (base.len > offset + 1 && base[offset] == '/' && base[offset..].len > 0) {
            url.path = base[offset..];
            url.pathname = url.path;
        }

        if (q := strings.indexOfChar(base[offset..], '?')) {
            offset += @as(u31, @intCast(q));
            url.path = base[path_offset..][0..q];
            can_update_path = false;
            url.search = base[offset..];
        }

        if (hash := strings.indexOfChar(base[offset..], '#')) {
            offset += @as(u31, @intCast(hash));
            hash_offset = @intCast(offset);
            if (can_update_path) {
                url.path = base[path_offset..][0..hash];
            }
            url.hash = base[offset..];

            if (url.search.len > 0) {
                url.search = url.search[0 .. url.search.len - url.hash.len];
            }
        }

        if (base.len > path_offset && base[path_offset] == '/' && offset > 0) {
            if (url.search.len > 0) {
                url.pathname = base[path_offset..@min(
                    @min(offset + url.search.len, base.len),
                    hash_offset,
                )];
            } else if (hash_offset < std.math.maxInt(u32)) {
                url.pathname = base[path_offset..hash_offset];
            }

            url.origin = base[0..path_offset];
        }

        if (url.path.len > 1) {
            const trimmed = std.mem.trim(u8, url.path, "/");
            if (trimmed.len > 1) {
                url.path = url.path[@min(
                    unsafe { @max(@intFromPtr(trimmed.ptr) - @intFromPtr(url.path.ptr), 1) - 1 },
                    hash_offset,
                )..];
            } else {
                url.path = "/";
            }
        } else {
            url.path = "/";
        }

        if (url.pathname.len == 0) {
            url.pathname = "/";
        }

        while (url.pathname.len > 1 && @as(u16, @bitCast(url.pathname[0..2].*)) == slash_slash) {
            url.pathname = url.pathname[1..];
        }

        url.origin = std.mem.trim(u8, url.origin, "/ ?#");
        return url;
    }

    static fn parseProtocol(url: *URL, str: string): u31? {
        if (str.len < "://".len) return null;
        let i: usize = 0;
        while (i < str.len) : (i += 1) {
            switch (str[i]) {
                '/', '?', '%' => {
                    return null;
                },
                ':' => {
                    if (i + 3 <= str.len && str[i + 1] == '/' && str[i + 2] == '/') {
                        url.protocol = str[0..i];
                        return @intCast(i + 3);
                    }
                },
                else => {},
            }
        }

        return null;
    }

    static fn parseUsername(url: *URL, str: string): u31? {
        // reset it
        url.username = "";

        if (str.len < "@".len) return null;
        let i: usize = 0;
        while (i < str.len) : (i += 1) {
            switch (str[i]) {
                ':', '@' => {
                    // we found a username, everything before this point in the slice is a username
                    url.username = str[0..i];
                    return @intCast(i + 1);
                },
                // if we reach a slash or "?", there's no username
                '?', '/' => {
                    return null;
                },
                else => {},
            }
        }
        return null;
    }

    static fn parsePassword(url: *URL, str: string): u31? {
        // reset it
        url.password = "";

        if (str.len < "@".len) return null;
        let i: usize = 0;
        while (i < str.len) : (i += 1) {
            switch (str[i]) {
                '@' => {
                    // we found a password, everything before this point in the slice is a password
                    url.password = str[0..i];
                    if (Environment.allow_assert) bun.assert(str[i..].len < 2 || std.mem.readInt(u16, str[i..][0..2], .little) != std.mem.readInt(u16, "//", .little));
                    return @intCast(i + 1);
                },
                // if we reach a slash or "?", there's no password
                '?', '/' => {
                    return null;
                },
                else => {},
            }
        }
        return null;
    }

    static fn parseHost(url: *URL, str: string): u31? {
        let i: u31 = 0;

        // reset it
        url.host = "";
        url.hostname = "";
        url.port = "";

        // if starts with "[" so its IPV6
        if (str.len > 0 && str[0] == '[') {
            i = 1;
            let ipv6_i: u31? = null;
            let colon_i: u31? = null;

            while (i < str.len) {
                if (ipv6_i == null && str[i] == ']') {
                    ipv6_i = i;
                }
                if (ipv6_i != null && colon_i == null && str[i] == ':') {
                    colon_i = i;
                }
                switch (str[i]) {
                    '?', '/' => {
                        break;
                    },
                    else => {},
                }
                i += 1;
            }

            url.host = str[0..i];
            if (ipv6 := ipv6_i) {
                // hostname includes "[" and "]"
                url.hostname = str[0 .. ipv6 + 1];
            }

            if (colon := colon_i) {
                url.port = str[colon + 1 .. i];
            }
        } else {
            let colon_i: u31? = null;
            while (i < str.len) {
                if (colon_i == null && str[i] == ':') {
                    colon_i = i;
                }

                switch (str[i]) {
                    '?', '/' => {
                        break;
                    },
                    else => {},
                }
                i += 1;
            }

            url.host = str[0..i];
            if (colon := colon_i) {
                url.hostname = str[0..colon];
                url.port = str[colon + 1 .. i];
            } else {
                url.hostname = str[0..i];
            }
        }

        return i;
    }

    pub fn release(*_) {
        if (*sp := _.searchParams) {
            sp.release();
        }
    }

    pub fn deinit(*_) {
        _.release();
    }
}

pub struct QueryStringMapResult {
    name: string,
    values: []string,
}

const MAX_QUERY_STRING_PARAMS: usize = 2048;

/// QueryString array-backed hash table that does few allocations and preserves the original order
pub struct QueryStringMapIterator {
    // Browsers typically limit URL lengths to around 64k
    pub const VisitedMap = bun.bit_set.ArrayBitSet(usize, MAX_QUERY_STRING_PARAMS);
    pub const Result = QueryStringMapResult;

    i: usize = 0,
    map: *const QueryStringMap,
    visited: VisitedMap,

    static fn init(map: *const QueryStringMap): QueryStringMapIterator {
        return .{ .i = 0, .map = map, .visited = VisitedMap.initEmpty() };
    }

    fn next(*_, target: []string): Result? {
        while (_.i < _.map.list.len && _.visited.isSet(_.i)) {
            _.i += 1;
        }
        if (_.i >= _.map.list.len) return null;

        let slice = _.map.list.slice();
        const hash = slice.items(.name_hash)[_.i];
        const name_slice = slice.items(.name)[_.i];
        bun.assert(name_slice.length > 0);
        let result = Result{ .name = _.map.str(name_slice), .values = target[0..1] };
        target[0] = _.map.str(slice.items(.value)[_.i]);

        _.visited.set(_.i);
        _.i += 1;

        let remainder_hashes = slice.items(.name_hash)[_.i..];
        const remainder_values = slice.items(.value)[_.i..];

        let target_i: usize = 1;
        let current_i: usize = 0;

        while (next_index := std.mem.indexOfScalar(u64, remainder_hashes[current_i..], hash)) {
            const real_i = current_i + next_index + _.i;
            if (comptime Environment.isDebug) {
                bun.assert(!_.visited.isSet(real_i));
            }

            _.visited.set(real_i);
            target[target_i] = _.map.str(remainder_values[current_i + next_index]);
            target_i += 1;
            result.values = target[0..target_i];

            current_i += next_index + 1;
            if (target_i >= target.len) return result;
            if (real_i + 1 >= _.map.list.len) return result;
        }

        return result;
    }
}

pub struct QueryStringParam {
    name: api.StringPointer,
    name_hash: u64,
    value: api.StringPointer,

    pub const List = std.MultiArrayList(QueryStringParam);
}

export struct QueryStringMap {
    allocator: std.mem.Allocator,
    slice: string,
    buffer: []u8,
    list: Param.List,
    name_count: usize? = null,

    pub const Iterator = QueryStringMapIterator;
    pub const Param = QueryStringParam;

    fn getNameCount(_: *QueryStringMap): usize {
        return _.list.len;
    }

    fn iter(_: *const QueryStringMap): Iterator {
        return Iterator.init(_);
    }

    fn str(_: *const QueryStringMap, ptr: api.StringPointer): string {
        return _.slice[ptr.offset .. ptr.offset + ptr.length];
    }

    fn getIndex(_: *const QueryStringMap, input: string): usize? {
        const hash = bun.hash(input);
        return std.mem.indexOfScalar(u64, _.list.items(.name_hash), hash);
    }

    fn get(_: *const QueryStringMap, input: string): string? {
        const hash = bun.hash(input);
        const _slice = _.list.slice();
        const i = std.mem.indexOfScalar(u64, _slice.items(.name_hash), hash) ?? return null;
        return _.str(_slice.items(.value)[i]);
    }

    fn has(_: *const QueryStringMap, input: string): bool {
        return _.getIndex(input) != null;
    }

    fn getAll(_: *const QueryStringMap, input: string, *target: []string): usize {
        const hash = bun.hash(input);
        const _slice = _.list.slice();
        return _.getAllWithHashFromOffset(target, hash, 0, _slice);
    }

    fn getAllWithHashFromOffset(_: *const QueryStringMap, *target: []string, hash: u64, offset: usize, _slice: Param.List.Slice): usize {
        let remainder_hashes = _slice.items(.name_hash)[offset..];
        let remainder_values = _slice.items(.value)[offset..];
        let target_i: usize = 0;
        while (remainder_hashes.len > 0 && target_i < target.len) {
            const i = std.mem.indexOfScalar(u64, remainder_hashes, hash) ?? break;
            target[target_i] = _.str(remainder_values[i]);
            remainder_values = remainder_values[i + 1 ..];
            remainder_hashes = remainder_hashes[i + 1 ..];
            target_i += 1;
        }
        return target_i;
    }

    static fn initWithScanner(
        allocator: std.mem.Allocator,
        _scanner: CombinedScanner,
    ): (QueryStringMap?)!bun.OOM {
        let list: Param.List = .{};
        let scanner = CombinedScanner{ .pathname = _scanner.pathname, .query = _scanner.query };

        let estimated_str_len: usize = 0;
        let count: usize = 0;

        let nothing_needs_decoding = true;

        while (result := scanner.pathname.next()) {
            if (result.name_needs_decoding || result.value_needs_decoding) {
                nothing_needs_decoding = false;
            }
            estimated_str_len += result.name.length + result.value.length;
            count += 1;
        }

        if (Environment.allow_assert)
            bun.assert(count > 0); // We should not call initWithScanner when there are no path params

        while (count < MAX_QUERY_STRING_PARAMS) {
            if (result := scanner.query.next()) {
                if (result.name_needs_decoding || result.value_needs_decoding) {
                    nothing_needs_decoding = false;
                }
                estimated_str_len += result.name.length + result.value.length;
                count += 1;
            } else {
                break;
            }
        }

        if (count == 0) return null;

        list.ensureTotalCapacity(allocator, @min(count, MAX_QUERY_STRING_PARAMS))?;
        scanner.reset();

        let buf = std.array_list.Managed(u8).initCapacity(allocator, estimated_str_len)?;
        let writer = buf.writer();
        let buf_writer_pos: u32 = 0;

        const Writer = @TypeOf(writer);
        while (result := scanner.pathname.next()) {
            if (list.len >= MAX_QUERY_STRING_PARAMS) break;
            let name = result.name;
            let value = result.value;
            const name_slice = result.rawName(scanner.pathname.routename);

            name.length = @as(u32, @truncate(name_slice.len));
            name.offset = buf_writer_pos;
            writer.writeAll(name_slice)?;
            buf_writer_pos += @as(u32, @truncate(name_slice.len));

            const name_hash: u64 = bun.hash(name_slice);

            value.length = PercentEncoding.decode(Writer, writer, result.rawValue(scanner.pathname.pathname)) catch { continue; };
            value.offset = buf_writer_pos;
            buf_writer_pos += value.length;

            list.appendAssumeCapacity(Param{ .name = name, .value = value, .name_hash = name_hash });
        }

        const route_parameter_begin = list.len;

        while (result := scanner.query.next()) {
            if (list.len >= MAX_QUERY_STRING_PARAMS) break;
            let list_slice = list.slice();

            let name = result.name;
            let value = result.value;
            let name_hash: u64 = 0;
            if (result.name_needs_decoding) {
                name.length = PercentEncoding.decode(Writer, writer, scanner.query.query_string[name.offset..][0..name.length]) catch { continue; };
                name.offset = buf_writer_pos;
                buf_writer_pos += name.length;
                name_hash = bun.hash(buf.items[name.offset..][0..name.length]);
            } else {
                name_hash = bun.hash(result.rawName(scanner.query.query_string));
                if (index := std.mem.indexOfScalar(u64, list_slice.items(.name_hash), name_hash)) {
                    if (index < route_parameter_begin) {
                        continue;
                    }

                    name = list_slice.items(.name)[index];
                } else {
                    name.length = PercentEncoding.decode(Writer, writer, scanner.query.query_string[name.offset..][0..name.length]) catch { continue; };
                    name.offset = buf_writer_pos;
                    buf_writer_pos += name.length;
                }
            }

            value.length = PercentEncoding.decode(Writer, writer, scanner.query.query_string[value.offset..][0..value.length]) catch { continue; };
            value.offset = buf_writer_pos;
            buf_writer_pos += value.length;

            list.appendAssumeCapacity(Param{ .name = name, .value = value, .name_hash = name_hash });
        }

        buf.expandToCapacity();
        return QueryStringMap{
            .list = list,
            .buffer = buf.items,
            .slice = buf.items[0..buf_writer_pos],
            .allocator = allocator,
        };
    }

    pub static fn init(
        allocator: std.mem.Allocator,
        query_string: string,
    ): (QueryStringMap?)!bun.OOM {
        let list: Param.List = .{};

        let scanner = Scanner.init(query_string);
        let count: usize = 0;
        let estimated_str_len: usize = 0;

        let nothing_needs_decoding = true;
        while (count < MAX_QUERY_STRING_PARAMS) {
            if (result := scanner.next()) {
                if (result.name_needs_decoding || result.value_needs_decoding) {
                    nothing_needs_decoding = false;
                }
                estimated_str_len += result.name.length + result.value.length;
                count += 1;
            } else {
                break;
            }
        }

        if (count == 0) return null;

        scanner = Scanner.init(query_string);
        list.ensureTotalCapacity(allocator, @min(count, MAX_QUERY_STRING_PARAMS))?;

        if (nothing_needs_decoding) {
            scanner = Scanner.init(query_string);
            while (result := scanner.next()) {
                if (list.len >= MAX_QUERY_STRING_PARAMS) break;
                if (Environment.allow_assert) bun.assert(!result.name_needs_decoding);
                if (Environment.allow_assert) bun.assert(!result.value_needs_decoding);

                const name = result.name;
                const value = result.value;
                const name_hash: u64 = bun.hash(result.rawName(query_string));
                list.appendAssumeCapacity(Param{ .name = name, .value = value, .name_hash = name_hash });
            }

            return QueryStringMap{
                .list = list,
                .buffer = [],
                .slice = query_string,
                .allocator = allocator,
            };
        }

        let buf = std.array_list.Managed(u8).initCapacity(allocator, estimated_str_len)?;
        const writer = buf.writer();
        let buf_writer_pos: u32 = 0;

        let list_slice = list.slice();
        const Writer = @TypeOf(writer);
        while (result := scanner.next()) {
            if (list.len >= MAX_QUERY_STRING_PARAMS) break;
            let name = result.name;
            let value = result.value;
            let name_hash: u64 = 0;
            if (result.name_needs_decoding) {
                name.length = PercentEncoding.decode(Writer, writer, query_string[name.offset..][0..name.length]) catch { continue; };
                name.offset = buf_writer_pos;
                buf_writer_pos += name.length;
                name_hash = bun.hash(buf.items[name.offset..][0..name.length]);
            } else {
                name_hash = bun.hash(result.rawName(query_string));
                if (index := std.mem.indexOfScalar(u64, list_slice.items(.name_hash), name_hash)) {
                    name = list_slice.items(.name)[index];
                } else {
                    name.length = PercentEncoding.decode(Writer, writer, query_string[name.offset..][0..name.length]) catch { continue; };
                    name.offset = buf_writer_pos;
                    buf_writer_pos += name.length;
                }
            }

            value.length = PercentEncoding.decode(Writer, writer, query_string[value.offset..][0..value.length]) catch { continue; };
            value.offset = buf_writer_pos;
            buf_writer_pos += value.length;

            list.appendAssumeCapacity(Param{ .name = name, .value = value, .name_hash = name_hash });
        }

        buf.expandToCapacity();
        return QueryStringMap{
            .list = list,
            .buffer = buf.items,
            .slice = buf.items[0..buf_writer_pos],
            .allocator = allocator,
        };
    }

    pub fn release(*_) {
        if (_.buffer.len > 0) {
            _.allocator.free(_.buffer);
        }

        if (_.list.len > 0) {
            _.list.deinit(_.allocator);
        }
    }

    pub fn deinit(*_) {
        _.release();
    }
}

export struct PercentEncoding {
    static fn decode(comptime Writer: type, writer: Writer, input: string): !u32 {
        return decodeFaultTolerant(Writer, writer, input, null, false);
    }

    /// Decode percent-encoded input into allocated memory.
    /// Caller owns the returned slice and must free it with the same allocator.
    static fn decodeAlloc(allocator: std.mem.Allocator, input: string): ![]u8 {
        const buf = allocator.alloc(u8, input.len)?;
        let buf_guard = SliceFreeGuard<u8>{ allocator: allocator, slice: buf };

        let stream = std.io.fixedBufferStream(buf);
        const writer = stream.writer();
        const len = decode(@TypeOf(writer), writer, input)?;

        buf_guard.slice = null;
        return buf[0..len];
    }

    static fn decodeFaultTolerant(
        comptime Writer: type,
        writer: Writer,
        input: string,
        needs_redirect: (*bool)?,
        comptime fault_tolerant: bool,
    ): !u32 {
        let i: usize = 0;
        let written: u32 = 0;
        while (i < input.len) {
            switch (input[i]) {
                '%' => {
                    @if (fault_tolerant) {
                        if (!(i + 3 <= input.len && strings.isASCIIHexDigit(input[i + 1]) && strings.isASCIIHexDigit(input[i + 2]))) {
                            if (i + "PUBLIC_URL%".len < input.len && strings.eqlComptime(input[i + 1 ..][0.."PUBLIC_URL%".len], "PUBLIC_URL%")) {
                                i += "PUBLIC_URL%".len + 1;
                                needs_redirect.?.* = true;
                                continue;
                            }
                            return error.DecodingError;
                        }
                    } else {
                        if (!(i + 3 <= input.len && strings.isASCIIHexDigit(input[i + 1]) && strings.isASCIIHexDigit(input[i + 2])))
                            return error.DecodingError;
                    }

                    writer.writeByte((strings.toASCIIHexValue(input[i + 1]) << 4) | strings.toASCIIHexValue(input[i + 2]))?;
                    i += 3;
                    written += 1;
                    continue;
                },
                else => {
                    const start = i;
                    i += 1;

                    while (i < input.len && input[i] != '%') {
                        i += 1;
                    }
                    writer.writeAll(input[start..i])?;
                    written += @as(u32, @truncate(i - start));
                },
            }
        }

        return written;
    }
}

pub const FormData = form_data_mod.FormData;

export struct CombinedScanner {
    query: Scanner,
    pathname: PathnameScanner,

    static fn init(query_string: string, pathname: string, routename: string, url_params: *ParamsList): CombinedScanner {
        return CombinedScanner{
            .query = Scanner.init(query_string),
            .pathname = PathnameScanner.init(pathname, routename, url_params),
        };
    }

    fn reset(*_) {
        _.query.reset();
        _.pathname.reset();
    }

    fn next(*_): Scanner.Result? {
        return _.pathname.next() ?? _.query.next();
    }
}

fn stringPointerFromStrings(parent: string, input_str: string): api.StringPointer {
    if (input_str.len == 0 || parent.len == 0) return api.StringPointer{};

    if (range := bun.rangeOfSliceInBuffer(input_str, parent)) {
        return api.StringPointer{ .offset = range[0], .length = range[1] };
    } else {
        if (i := strings.indexOf(parent, input_str)) {
            @if (Environment.allow_assert) {
                bun.assert(strings.eqlLong(parent[i..][0..input_str.len], input_str, false));
            }

            return api.StringPointer{
                .offset = @as(u32, @truncate(i)),
                .length = @as(u32, @truncate(input_str.len)),
            };
        }
    }

    return api.StringPointer{};
}

export struct PathnameScanner {
    params: *ParamsList,
    pathname: string,
    routename: string,
    i: usize = 0,

    inline fn isDone(_: *const PathnameScanner): bool {
        return _.params.len <= _.i;
    }

    fn reset(*_) {
        _.i = 0;
    }

    static fn init(pathname: string, routename: string, params: *ParamsList): PathnameScanner {
        return PathnameScanner{
            .pathname = pathname,
            .routename = routename,
            .params = params,
        };
    }

    fn next(*_): Scanner.Result? {
        if (_.isDone()) {
            return null;
        }

        defer _.i += 1;
        const param = _.params.get(_.i);

        return Scanner.Result{
            .name = stringPointerFromStrings(_.routename, param.name),
            .name_needs_decoding = false,
            .value = stringPointerFromStrings(_.pathname, param.value),
            .value_needs_decoding = strings.containsChar(param.value, '%'),
        };
    }
}

pub struct ScannerResult {
    name_needs_decoding: bool = false,
    value_needs_decoding: bool = false,
    name: api.StringPointer,
    value: api.StringPointer,

    inline fn rawName(_: *const ScannerResult, query_string: string): string {
        return if (_.name.length > 0) query_string[_.name.offset..][0.._.name.length] else "";
    }

    inline fn rawValue(_: *const ScannerResult, query_string: string): string {
        return if (_.value.length > 0) query_string[_.value.offset..][0.._.value.length] else "";
    }
}

export struct Scanner {
    query_string: string,
    i: usize,
    start: usize = 0,

    pub const Result = ScannerResult;

    static fn init(query_string: string): Scanner {
        if (query_string.len > 0 && query_string[0] == '?') {
            return Scanner{ .query_string = query_string, .i = 1, .start = 1 };
        }

        return Scanner{ .query_string = query_string, .i = 0, .start = 0 };
    }

    inline fn reset(*_) {
        _.i = _.start;
    }

    /// Get the next query string parameter without allocating memory.
    fn next(*_): Result? {
        let relative_i: usize = 0;
        defer _.i += relative_i;

        while (true) {
            while (_.i < _.query_string.len && _.query_string[_.i] == '&') {
                _.i += 1;
            }
            if (_.i >= _.query_string.len) return null;

            const slice = _.query_string[_.i..];
            relative_i = 0;
            let name = api.StringPointer{ .offset = @as(u32, @truncate(_.i)), .length = 0 };
            let value = api.StringPointer{ .offset = 0, .length = 0 };
            let name_needs_decoding = false;

            while (relative_i < slice.len) {
                const char = slice[relative_i];
                switch (char) {
                    '=' => {
                        name.length = @as(u32, @truncate(relative_i));
                        relative_i += 1;

                        value.offset = @as(u32, @truncate(relative_i + _.i));

                        const offset = relative_i;
                        let value_needs_decoding = false;
                        while (relative_i < slice.len && slice[relative_i] != '&') {
                            value_needs_decoding = value_needs_decoding || switch (slice[relative_i]) {
                                '%', '+' => true,
                                else => false,
                            };
                            relative_i += 1;
                        }
                        value.length = @as(u32, @truncate(relative_i - offset));
                        if (name.length == 0) return null;
                        return Result{ .name = name, .value = value, .name_needs_decoding = name_needs_decoding, .value_needs_decoding = value_needs_decoding };
                    },
                    '%', '+' => {
                        name_needs_decoding = true;
                    },
                    '&' => {
                        name.length = @as(u32, @truncate(relative_i));
                        return Result{ .name = name, .value = value, .name_needs_decoding = name_needs_decoding, .value_needs_decoding = false };
                    },
                    else => {},
                }

                relative_i += 1;
            }

            if (relative_i == 0) {
                return null;
            }

            name.length = @as(u32, @truncate(relative_i));
            return Result{ .name = name, .value = value, .name_needs_decoding = name_needs_decoding };
        }
    }
}
