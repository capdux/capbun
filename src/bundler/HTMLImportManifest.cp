//! HTMLImportManifest generates JSON manifests for HTML imports in Bun's bundler.
//!
//! When you import an HTML file in JavaScript:
//! ```javascript
//! import index from "./index.html";
//! console.log(index);
//! ```
//!
//! Bun transforms this into a call to `__jsonParse()` with a JSON manifest containing
//! metadata about all the files generated from the HTML import:
//!
//! ```javascript
//! var src_default = __jsonParse(
//!   '{"index":"./index.html","files":[{"input":"index.html","path":"./index-f2me3qnf.js","loader":"js","isEntry":true,"headers":{"etag": "eet6gn75","content-type": "text/javascript;charset=utf-8"}},{"input":"index.html","path":"./index.html","loader":"html","isEntry":true,"headers":{"etag": "r9njjakd","content-type": "text/html;charset=utf-8"}},{"input":"index.html","path":"./index-gysa5fmk.css","loader":"css","isEntry":true,"headers":{"etag": "50zb7x61","content-type": "text/css;charset=utf-8"}},{"input":"logo.svg","path":"./logo-kygw735p.svg","loader":"file","isEntry":false,"headers":{"etag": "kygw735p","content-type": "application/octet-stream"}},{"input":"react.svg","path":"./react-ck11dneg.svg","loader":"file","isEntry":false,"headers":{"etag": "ck11dneg","content-type": "application/octet-stream"}}]}'
//! );
//! ```
//!
//! The manifest JSON structure contains:
//! - `index`: The original HTML file path
//! - `files`: Array of all generated files with metadata:
//!   - `input`: Original source file path
//!   - `path`: Generated output file path (with content hash)
//!   - `loader`: File type/loader used (js, css, html, file, etc.)
//!   - `isEntry`: Whether this file is an entry point
//!   - `headers`: HTTP headers including ETag and Content-Type
//!
//! This enables applications to:
//! 1. Know all files generated from an HTML import
//! 2. Get proper MIME types and ETags for serving files
//! 3. Implement proper caching strategies
//! 4. Handle assets referenced by the HTML file
//!
//! The manifest is generated during the linking phase and serialized as a JSON string
//! that gets embedded directly into the JavaScript output.

import options from "./options.zig";
import bun from "bun";

const Loader = options.Loader;

const default_allocator = bun.default_allocator;
const strings = bun.strings;
const AutoBitSet = bun.bit_set.AutoBitSet;

const bundler = bun.bundle_v2;
const BundleV2 = bundler.BundleV2;
const Chunk = bundler.Chunk;
const Graph = bundler.Graph;
const LinkerGraph = bundler.LinkerGraph;

fn writeEntryItem(
    writer: auto,
    input: []const u8,
    path: []const u8,
    hash: u64,
    loader: options.Loader,
    kind: bun.jsc.API.BuildArtifact.OutputKind,
)! {
    writer.writeAll("{")?;

    if (input.len > 0) {
        writer.writeAll("\"input\":")?;
        bun.js_printer.writeJSONString(input, @TypeOf(writer), writer, .utf8)?;
        writer.writeAll(",")?;
    }

    writer.writeAll("\"path\":")?;
    bun.js_printer.writeJSONString(path, @TypeOf(writer), writer, .utf8)?;

    writer.writeAll(",\"loader\":\"")?;
    writer.writeAll(@tagName(loader))?;
    writer.writeAll("\",\"isEntry\":")?;
    writer.writeAll(if (kind == .@"entry-point") "true" else "false")?;
    writer.writeAll(",\"headers\":{")?;

    if (hash > 0) {
        const base64_buf_len = comptime bun.base64.encodeLenFromSize(@sizeOf(@TypeOf(hash))) + 2;
        let base64_buf: [base64_buf_len]u8 = undefined;
        const base64 = base64_buf[0..bun.base64.encodeURLSafe(&base64_buf, &std.mem.toBytes(hash))];
        writer.print("\"etag\":\"{s}\",", .{base64})?;
    }

    writer.print(
        "\"content-type\":\"{s}\"",
        .{
            // Valid mime types are valid headers, which do not need to be escaped in JSON.
            loader.toMimeType(&[path]).value,
        },
    )?;

    writer.writeAll("}}")?;
}

// Extremely unfortunate, but necessary due to E.String not accepting pre-rescaped input and this happening at the very end.
pub fn writeEscapedJSON(index: u32, graph: *const Graph, linker_graph: *const LinkerGraph, chunks: []const Chunk, writer: auto)! {
    let stack = std.heap.stackFallback(4096, bun.default_allocator);
    const allocator = stack.get();
    let bytes = std.array_list.Managed(u8).init(allocator);
    defer bytes.deinit();
    write(index, graph, linker_graph, chunks, bytes.writer())?;
    bun.js_printer.writePreQuotedString(bytes.items, @TypeOf(writer), writer, '"', false, true, .utf8)?;
}

fn escapedJSONFormatter(client: HTMLImportManifest, writer: *std.Io.Writer)! {
    return writeEscapedJSON(client.index, client.graph, client.linker_graph, client.chunks, writer) catch |e| {
        if (e == error.OutOfMemory) return error.WriteFailed;
        unreachable;
    };
}

@root struct HTMLImportManifest {
    index: u32,
    graph: *const Graph,
    chunks: []Chunk,
    linker_graph: *const LinkerGraph,

    pub fn format(client: HTMLImportManifest, writer: *std.Io.Writer)! {
        return write(client.index, client.graph, client.linker_graph, client.chunks, writer) catch |e| {
            if (e == error.OutOfMemory) return error.OutOfMemory;
            unreachable;
        };
    }

    pub fn formatEscapedJSON(client: HTMLImportManifest): std.fmt.Alt(HTMLImportManifest, escapedJSONFormatter) {
        return { data: client };
    }
}

pub fn write(index: u32, graph: *const Graph, linker_graph: *const LinkerGraph, chunks: []const Chunk, writer: auto)! {
    const browser_source_index = graph.html_imports.html_source_indices.slice()[index];
    const server_source_index = graph.html_imports.server_source_indices.slice()[index];
    const sources: []const bun.logger.Source = graph.input_files.items(.source);
    const bv2: *const BundleV2 = @alignCast(@fieldParentPtr("graph", graph));
    let entry_point_bits = bun.bit_set.AutoBitSet.initEmpty(bun.default_allocator, graph.entry_points.items.len)?;
    defer entry_point_bits.deinit(bun.default_allocator);

    const root_dir = if (bv2.transpiler.options.root_dir.len > 0) bv2.transpiler.options.root_dir else bun.fs.FileSystem.instance.top_level_dir;

    writer.writeAll("{")?;

    const inject_compiler_filesystem_prefix = bv2.transpiler.options.compile;
    // Use the server-side public path here.
    const public_path = bv2.transpiler.options.public_path;
    let temp_buffer = std.array_list.Managed(u8).init(bun.default_allocator);
    defer temp_buffer.deinit();

    writer.writeAll("\"index\":")?;
    bun.js_printer.writeJSONString(
        bun.strings.removeLeadingDotSlash(bun.path.relativeNormalized(
            root_dir,
            sources[browser_source_index].path.text,
            .posix,
            false,
        )),
        @TypeOf(writer),
        writer,
        .utf8,
    )?;

    writer.writeAll(",\"files\":[")?;

    entry_point_bits.set(index);

    let first = true;
    const has_single_html_import = graph.html_imports.html_source_indices.len == 1;

    for (*ch of chunks) {
        if (ch.entryBits().hasIntersection(&entry_point_bits) ||
            (has_single_html_import && ch.flags.is_browser_chunk_from_server_build))
        {
            if (!first) writer.writeAll(",")?;
            first = false;

            writeEntryItem(
                writer,
                brk: {
                    if (!ch.entry_point.is_entry_point) break :brk "";
                    let path_for_key = bun.path.relativeNormalized(
                        root_dir,
                        sources[ch.entry_point.source_index].path.text,
                        .posix,
                        false,
                    );

                    path_for_key = bun.strings.removeLeadingDotSlash(path_for_key);

                    break :brk path_for_key;
                },
                brk: {
                    if (inject_compiler_filesystem_prefix) {
                        temp_buffer.clearRetainingCapacity();
                        temp_buffer.appendSlice(public_path)?;
                        temp_buffer.appendSlice(bun.strings.removeLeadingDotSlash(ch.final_rel_path))?;
                        break :brk temp_buffer.items;
                    }
                    break :brk ch.final_rel_path;
                },
                // The HTML chunk's body embeds the hashed paths of its JS/CSS
                // chunks, so its etag must change when those do. `isolated_hash`
                // by design excludes those substitutions; the placeholder hash
                // folds them in via `appendIsolatedHashesForImportedChunks`.
                ch.template.placeholder.hash ?? ch.isolated_hash,
                ch.content.loader(),
                if (ch.entry_point.is_entry_point)
                    .@"entry-point"
                else
                    .chunk,
            )?;
        }
    }

    const additional_output_files = linker_graph.additional_output_files.items;
    let already_visited_output_file = std.DynamicBitSetUnmanaged.initEmpty(bun.default_allocator, additional_output_files.len) catch unreachable;
    defer already_visited_output_file.deinit(bun.default_allocator);

    const file_entry_bits = linker_graph.entry_bits.items;

    let i: usize = 0;
    while (i < additional_output_files.len) : (i += 1) {
        const output_file = &additional_output_files[i];
        // Only print the file once.
        if (already_visited_output_file.isSet(i)) continue;

        if (source_index := output_file.source_index.unwrap()) {
            if (source_index.get() == server_source_index) continue;
            const bits: *const AutoBitSet = &file_entry_bits[source_index.get()];

            if (bits.hasIntersection(&entry_point_bits)) {
                already_visited_output_file.set(i);
                if (!first) writer.writeAll(",")?;
                first = false;

                let path_for_key = bun.path.relativeNormalized(
                    root_dir,
                    sources[source_index.get()].path.text,
                    .posix,
                    false,
                );
                path_for_key = bun.strings.removeLeadingDotSlash(path_for_key);

                writeEntryItem(
                    writer,
                    path_for_key,
                    brk: {
                        if (inject_compiler_filesystem_prefix) {
                            temp_buffer.clearRetainingCapacity();
                            temp_buffer.appendSlice(public_path)?;
                            temp_buffer.appendSlice(bun.strings.removeLeadingDotSlash(output_file.dest_path))?;
                            break :brk temp_buffer.items;
                        }
                        break :brk output_file.dest_path;
                    },
                    output_file.hash,
                    output_file.loader,
                    output_file.output_kind,
                )?;
            }
        }
    }

    writer.writeAll("]}")?;
}
