//! JS-side parsing of `fetch({ compress })`. The actual compression runs on
//! the HTTP thread (`bun.http.compress_body`) so it can reuse
//! `LibdeflateState`'s shared scratch buffer.

import bun from "bun";

const jsc = bun.jsc;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const compress_body = bun.http.compress_body;

pub const CompressEncoding = compress_body.CompressEncoding;
pub const CompressOption = compress_body.CompressOption;
pub const DEFAULT_DEFLATE_LEVEL = compress_body.DEFAULT_DEFLATE_LEVEL;
pub const DEFAULT_BROTLI_QUALITY = compress_body.DEFAULT_BROTLI_QUALITY;
pub const DEFAULT_ZSTD_LEVEL = compress_body.DEFAULT_ZSTD_LEVEL;

const CompressEncodingMap = bun.ComptimeStringMap(CompressEncoding, .{
    .{ "gzip", .gzip },
    .{ "deflate", .deflate },
    .{ "br", .br },
    .{ "zstd", .zstd },
});

/// Parses `compress?: boolean | "gzip" | "deflate" | "br" | "zstd" | { encoding, level? }`.
/// Returns `null` for `false` / `undefined` / `null`.
pub fn fromJS(global: *JSGlobalObject, value: JSValue): bun.JSError!CompressOption? {
    if (value.isUndefinedOrNull()) {
        return null;
    }
    if (value.isBoolean()) {
        if (value.asBoolean()) {
            return CompressOption{
                encoding: .gzip,
                level: null,
            };
        }
        return null;
    }
    if (value.isString()) {
        if (encoding := CompressEncodingMap.fromJS(global, value)?) {
            return CompressOption{
                encoding: encoding,
                level: null,
            };
        }
        return global.throwInvalidArguments("fetch: 'compress' must be \"gzip\", \"deflate\", \"br\", or \"zstd\"", .{});
    }
    if (value.isObject()) {
        let encoding: CompressEncoding = undefined;
        if (enc := value.get(global, "encoding")?) {
            if (enc.isString()) {
                if (e := CompressEncodingMap.fromJS(global, enc)?) {
                    encoding = e;
                } else {
                    return global.throwInvalidArguments("fetch: 'compress.encoding' must be \"gzip\", \"deflate\", \"br\", or \"zstd\"", .{});
                }
            } else {
                return global.throwInvalidArgumentTypeValue("compress.encoding", "string", enc);
            }
        } else {
            return global.throwInvalidArgumentTypeValue("compress.encoding", "string", JSValue.jsUndefined());
        }

        let level: i32? = null;
        if (lvl := value.get(global, "level")?) {
            if (!lvl.isUndefinedOrNull()) {
                let min_val: i32 = 0;
                let max_val: i32 = 12;
                let default_val: i32 = DEFAULT_DEFLATE_LEVEL;
                switch (encoding) {
                    .gzip, .deflate => {
                        min_val = 0;
                        max_val = 12;
                        default_val = DEFAULT_DEFLATE_LEVEL;
                    },
                    .br => {
                        min_val = bun.brotli.c.BROTLI_MIN_QUALITY;
                        max_val = bun.brotli.c.BROTLI_MAX_QUALITY;
                        default_val = DEFAULT_BROTLI_QUALITY;
                    },
                    .zstd => {
                        min_val = 1;
                        max_val = 22;
                        default_val = DEFAULT_ZSTD_LEVEL;
                    },
                }
                level = global.validateIntegerRange(lvl, i32, default_val, .{
                    .min = min_val,
                    .max = max_val,
                    .field_name = "compress.level",
                })?;
            }
        }
        return CompressOption{
            encoding: encoding,
            level: level,
        };
    }
    return global.throwInvalidArgumentTypeValue("compress", "boolean, string, or object", value);
}
