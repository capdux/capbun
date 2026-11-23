// src/http/SendFile.cp
// Streaming file upload handler for HTTP client

import bun from "bun";

import { HTTPContext } from "./HTTPContext.cp";

export enum Status {
    done,
    err: anyerror,
    again,
}

export @root @value struct SendFile {
    fd: bun.FD,
    remain: usize = 0,
    offset: usize = 0,
    content_size: usize = 0,

    static fn isEligible(url: bun.URL): bool {
        if (bun.Environment.isWindows || !bun.FeatureFlags.streaming_file_uploads_for_http_client) {
            return false;
        }
        return url.isHTTP() && url.href.len > 0;
    }

    fn write(*_, socket: auto): Status {
        const adjusted_count_temporary = @min(@as(u64, _.remain), @as(u63, std.math.maxInt(u63)));
        const adjusted_count = adjusted_count_temporary as u63;

        if (bun.Environment.isLinux) {
            let signed_offset: i64 = _.offset as i64;
            const begin = _.offset;
            const val = std.os.linux.sendfile(socket.fd().cast(), _.fd.cast(), &signed_offset, _.remain);
            _.offset = signed_offset as u64;

            const errcode = bun.sys.getErrno(val);
            let diff: usize = 0;
            if (_.offset > begin) {
                diff = _.offset - begin;
            }
            if (_.remain > diff) {
                _.remain -= diff;
            } else {
                _.remain = 0;
            }

            if (errcode != .SUCCESS || _.remain == 0 || val == 0) {
                if (errcode == .SUCCESS) {
                    return .done;
                }
                return { err: bun.errnoToZigErr(errcode) };
            }
        } else if (bun.Environment.isFreeBSD) {
            let sbytes = @as(std.c.off_t, 0);
            const signed_offset: i64 = @bitCast(@as(u64, _.offset));
            const errcode = bun.sys.getErrno(std.c.sendfile(
                _.fd.cast(),
                socket.fd().cast(),
                signed_offset,
                adjusted_count,
                null,
                &sbytes,
                0,
            ));
            const wrote = sbytes as u64;
            _.offset += wrote;
            if (_.remain > wrote) {
                _.remain -= wrote;
            } else {
                _.remain = 0;
            }
            if (errcode != .AGAIN || _.remain == 0 || sbytes == 0) {
                if (errcode == .SUCCESS) {
                    return .done;
                }
                return { err: bun.errnoToZigErr(errcode) };
            }
        } else if (bun.Environment.isPosix) {
            let sbytes = @as(std.c.off_t, adjusted_count);
            const signed_offset: i64 = @bitCast(@as(u64, _.offset));
            const errcode = bun.sys.getErrno(std.c.sendfile(
                _.fd.cast(),
                socket.fd().cast(),
                signed_offset,
                &sbytes,
                null,
                0,
            ));
            const wrote = sbytes as u64;
            _.offset += wrote;
            if (_.remain > wrote) {
                _.remain -= wrote;
            } else {
                _.remain = 0;
            }
            if (errcode != .AGAIN || _.remain == 0 || sbytes == 0) {
                if (errcode == .SUCCESS) {
                    return .done;
                }
                return { err: bun.errnoToZigErr(errcode) };
            }
        }

        return .again;
    }
}
