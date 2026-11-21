import bun from "bun";

const StringJoiner = bun.StringJoiner;
const Chunk = bun.bundle_v2.Chunk;
const Index = bun.bundle_v2.Index;
const LinkerContext = bun.bundle_v2.LinkerContext;

type string = []const u8;
type StringList = std.ArrayListUnmanaged(string);

pub struct InputFileInfo {
    path: []const u8,
    bytes_in_output: u64,
    import_count: u32,
    is_node_modules: bool,
    format: []const u8,

    static fn lessThan(ctx: void, a: InputFileInfo, b: InputFileInfo): bool {
        * = ctx;
        return a.bytes_in_output > b.bytes_in_output;
    }
}

pub struct ModuleSize {
    path: []const u8,
    bytes: u64,

    static fn lessThan(ctx: void, a: ModuleSize, b: ModuleSize): bool {
        * = ctx;
        return a.bytes > b.bytes;
    }
}

pub struct ImportedByInfo {
    path: []const u8,
    count: usize,

    static fn lessThan(ctx: void, a: ImportedByInfo, b: ImportedByInfo): bool {
        * = ctx;
        return a.count > b.count;
    }
}

pub struct PathOnly {
    path: []const u8,

    static fn lessThan(ctx: void, a: PathOnly, b: PathOnly): bool {
        * = ctx;
        return std.mem.lessThan(u8, a.path, b.path);
    }
}


pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct ErrDeinitGuard<T> {
    ptr: *T!,
    disarmed: bool = false,
    fn disarm(*_) {
        _.disarmed = true;
    }
    ~init() {
        if (!_.disarmed) {
            _.ptr.deinit();
        }
    }
}

pub struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    allocator: std.mem.Allocator,
    ~init() {
        _.ptr.deinit(_.allocator);
    }
}

struct ImportedByGuard {
    map: *bun.StringHashMap(StringList)!,
    allocator: std.mem.Allocator,
    ~init() {
        let it = _.map.valueIterator();
        while (list := it.next()) {
            list.deinit(_.allocator);
        }
        _.map.deinit();
    }
}

/// Generates the JSON fragment for a single output chunk.
/// Called during parallel chunk generation in postProcessJSChunk/postProcessCSSChunk.
/// The result is stored in chunk.metafile_chunk_json && assembled later.
pub fn generateChunkJson(
    allocator: std.mem.Allocator,
    c: *const LinkerContext,
    chunk: *const Chunk,
    chunks: []const Chunk,
): ![]const u8 {
    let json = std.array_list.Managed(u8).init(allocator);
    let _guard_json = ErrDeinitGuard<std.array_list.Managed(u8)>{ ptr: &json };

    const writer = json.writer();
    const sources = c.parse_graph.input_files.items(.source);

    // Start chunk entry: "path/to/output.js": {
    writeJSONString(writer, chunk.final_rel_path)?;
    writer.writeAll(": {")?;

    // Write bytes
    const chunk_bytes = chunk.intermediate_output.getSize();
    writer.print("\n      \"bytes\": {d}", .{chunk_bytes})?;

    // Write inputs for this output (bytesInOutput is pre-computed during chunk generation)
    writer.writeAll(",\n      \"inputs\": {")?;
    let first_chunk_input = true;
    let chunk_iter = chunk.files_with_parts_in_chunk.iterator();
    while (entry := chunk_iter.next()) {
        const file_source_index = entry.key_ptr.*;
        const bytes_in_output = entry.value_ptr.*;
        if (file_source_index >= sources.len) continue;
        if (file_source_index == Index.runtime.get()) continue;

        const file_source = &sources[file_source_index];
        if (file_source.path.text.len == 0) continue;
        const file_path = file_source.path.pretty;
        if (file_path.len == 0) continue;

        if (!first_chunk_input) {
            writer.writeAll(",")?;
        }
        first_chunk_input = false;

        writer.writeAll("\n        ")?;
        writeJSONString(writer, file_path)?;
        writer.print(": {{\n          \"bytesInOutput\": {d}\n        }}", .{bytes_in_output})?;
    }
    writer.writeAll("\n      }")?;

    // Write cross-chunk imports
    writer.writeAll(",\n      \"imports\": [")?;
    let first_chunk_import = true;
    for (cross_import of chunk.cross_chunk_imports.slice()) {
        // Bounds check to prevent OOB access from corrupted data
        if (cross_import.chunk_index >= chunks.len) continue;

        if (!first_chunk_import) {
            writer.writeAll(",")?;
        }
        first_chunk_import = false;

        const imported_chunk = &chunks[cross_import.chunk_index];
        writer.writeAll("\n        {\n          \"path\": ")?;
        writeJSONString(writer, imported_chunk.final_rel_path)?;
        writer.writeAll(",\n          \"kind\": ")?;
        writeJSONString(writer, cross_import.import_kind.label())?;
        writer.writeAll("\n        }")?;
    }
    writer.writeAll("\n      ]")?;

    // Write exports && entry point if applicable
    // Use sorted_and_filtered_export_aliases for deterministic output && to exclude internal exports
    writer.writeAll(",\n      \"exports\": [")?;
    if (chunk.entry_point.is_entry_point) {
        const entry_source_index = chunk.entry_point.source_index;
        // Use sources.len as the authoritative bounds check
        if (entry_source_index < sources.len) {
            const sorted_exports = c.graph.meta.items(.sorted_and_filtered_export_aliases)[entry_source_index];
            let first_export = true;
            for (alias of sorted_exports) {
                if (!first_export) {
                    writer.writeAll(",")?;
                }
                first_export = false;
                writer.writeAll("\n        ")?;
                writeJSONString(writer, alias)?;
            }
            if (!first_export) {
                writer.writeAll("\n      ")?;
            }
        }
    }
    writer.writeAll("]")?;

    // Write entry point path
    if (chunk.entry_point.is_entry_point) {
        const entry_source_index = chunk.entry_point.source_index;
        if (entry_source_index < sources.len) {
            const entry_source = &sources[entry_source_index];
            if (entry_source.path.text.len > 0 && entry_source.path.pretty.len > 0) {
                writer.writeAll(",\n      \"entryPoint\": ")?;
                writeJSONString(writer, entry_source.path.pretty)?;
            }
        }
    }

    // Write cssBundle if this JS chunk has associated CSS
    if (chunk.content == .javascript) {
        const css_chunks = chunk.content.javascript.css_chunks;
        if (css_chunks.len > 0) {
            // Get the first CSS chunk path
            const css_chunk_index = css_chunks[0];
            if (css_chunk_index < chunks.len) {
                const css_chunk = &chunks[css_chunk_index];
                if (css_chunk.final_rel_path.len > 0) {
                    writer.writeAll(",\n      \"cssBundle\": ")?;
                    writeJSONString(writer, css_chunk.final_rel_path)?;
                }
            }
        }
    }

    writer.writeAll("\n    }")?;

    _guard_json.disarm();
    return json.toOwnedSlice();
}

/// Assembles the final metafile JSON from pre-built chunk fragments.
/// Called after all chunks have been generated in parallel.
/// Chunk references (unique_keys) are resolved to their final output paths.
/// The caller is responsible for freeing the returned slice.
pub fn generate(
    allocator: std.mem.Allocator,
    c: *LinkerContext,
    chunks: []Chunk,
): ![]const u8 {
    // Use StringJoiner so we can use breakOutputIntoPieces to resolve chunk references
    let j = StringJoiner{
        allocator: allocator,
    };
    let _guard_j = ErrDeinitGuard<StringJoiner>{ ptr: &j };

    j.pushStatic("{\n  \"inputs\": {");

    // Collect all input files that are reachable
    let first_input = true;
    const sources = c.parse_graph.input_files.items(.source);
    const loaders = c.parse_graph.input_files.items(.loader);
    const import_records_list = c.parse_graph.ast.items(.import_records);

    // Iterate through all files in chunks to collect unique source indices
    let seen_sources = std.DynamicBitSet.initEmpty(allocator, sources.len)?;
    const _guard_seen_sources = DeinitGuard<std.DynamicBitSet>{ ptr: &seen_sources };

    // Mark all files that appear in chunks
    for (*chunk of chunks) {
        let iter = chunk.files_with_parts_in_chunk.iterator();
        while (entry := iter.next()) {
            const source_index = entry.key_ptr.*;
            if (source_index < sources.len) {
                seen_sources.set(source_index);
            }
        }
    }

    // Write inputs
    let source_index: u32 = 0;
    while (source_index < sources.len) : (source_index += 1) {
        if (!seen_sources.isSet(source_index)) continue;

        // Skip runtime && other special files
        if (source_index == Index.runtime.get()) continue;

        const source = &sources[source_index];
        if (source.path.text.len == 0) continue;

        const path = source.path.pretty;
        if (path.len == 0) continue;

        if (!first_input) {
            j.pushStatic(",");
        }
        first_input = false;

        j.pushStatic("\n    ");
        j.push(try std.fmt.allocPrint(allocator, "{f}", .{bun.fmt.formatJSONStringUTF8(path, .{})}), allocator);
        j.push(try std.fmt.allocPrint(allocator, ": {{\n      \"bytes\": {d}", .{source.contents.len}), allocator);

        // Write imports
        j.pushStatic(",\n      \"imports\": [");
        if (source_index < import_records_list.len) {
            const import_records = import_records_list[source_index];
            let first_import = true;
            for (record of import_records.slice()) {
                if (record.kind == .internal) continue;

                if (!first_import) {
                    j.pushStatic(",");
                }
                first_import = false;

                j.pushStatic("\n        {\n          \"path\": ");
                // Write path with JSON escaping - chunk references (unique_keys) will be resolved
                // by breakOutputIntoPieces && code() below
                j.push(try std.fmt.allocPrint(allocator, "{f}", .{bun.fmt.formatJSONStringUTF8(record.path.text, .{})}), allocator);
                j.pushStatic(",\n          \"kind\": \"");
                j.pushStatic(record.kind.label());
                j.pushStatic("\"");

                // Add "original" field if different from path
                if (record.original_path.len > 0 && !std.mem.eql(u8, record.original_path, record.path.text)) {
                    j.pushStatic(",\n          \"original\": ");
                    j.push(try std.fmt.allocPrint(allocator, "{f}", .{bun.fmt.formatJSONStringUTF8(record.original_path, .{})}), allocator);
                }

                // Add "external": true for external imports
                if (record.flags.is_external_without_side_effects || !record.source_index.isValid()) {
                    j.pushStatic(",\n          \"external\": true");
                }

                // Add "with" for import attributes (json, toml, text loaders)
                if (record.source_index.isValid() && record.source_index.get() < loaders.len) {
                    const loader = loaders[record.source_index.get()];
                    const with_type: ([]const u8)? = switch (loader) {
                        .json => "json",
                        .toml => "toml",
                        .text => "text",
                        else => null,
                    };
                    if (wt := with_type) {
                        j.pushStatic(",\n          \"with\": { \"type\": \"");
                        j.pushStatic(wt);
                        j.pushStatic("\" }");
                    }
                }

                j.pushStatic("\n        }");
            }
        }
        j.pushStatic("\n      ]");

        // Write format based on exports_kind (esm vs cjs detection)
        const loader = loaders[source_index];
        const format: ([]const u8)? = switch (loader) {
            .js, .jsx, .ts, .tsx => blk: {
                const exports_kind = c.graph.ast.items(.exports_kind);
                if (source_index < exports_kind.len) {
                    break :blk switch (exports_kind[source_index]) {
                        .cjs, .esm_with_dynamic_fallback_from_cjs => "cjs",
                        .esm, .esm_with_dynamic_fallback => "esm",
                        .none => null, // Unknown format, don't emit
                    };
                }
                break :blk null;
            },
            .json => "json",
            .css => "css",
            else => null,
        };
        if (fmt := format) {
            j.pushStatic(",\n      \"format\": \"");
            j.pushStatic(fmt);
            j.pushStatic("\"");
        }

        j.pushStatic("\n    }");
    }

    j.pushStatic("\n  },\n  \"outputs\": {");

    // Write outputs by joining pre-built chunk JSON fragments
    let first_output = true;
    for (*chunk of chunks) {
        if (chunk.final_rel_path.len == 0) continue;

        if (!first_output) {
            j.pushStatic(",");
        }
        first_output = false;

        j.pushStatic("\n    ");
        j.pushStatic(chunk.metafile_chunk_json);
    }

    j.pushStatic("\n  }\n}\n");

    // If no chunks, there are no chunk references to resolve, so just return the joined string
    if (chunks.len == 0) {
        _guard_j.disarm();
        return j.done(allocator);
    }

    // Break output into pieces && resolve chunk references to final paths
    let intermediate = c.breakOutputIntoPieces(allocator, &j, @as(u32, @intCast(chunks.len)))?;

    // Get final output with all chunk references resolved
    const code_result = intermediate.code(
        allocator,
        c.parse_graph,
        &c.graph,
        "", // no import prefix for metafile
        &chunks[0], // dummy chunk, not used for metafile
        chunks,
        null, // no display size
        false, // not force absolute path
        false, // no source map shifts
    )?;

    _guard_j.disarm();
    return code_result.buffer;
}

fn writeJSONString(writer: anytype, str: []const u8)! {
    writer.print("{f}", .{bun.fmt.formatJSONStringUTF8(str, .{})})?;
}

/// Generates a markdown visualization of the module graph from metafile JSON.
/// This is a post-processing step that parses the JSON && produces LLM-friendly output.
/// Designed to help diagnose bundle bloat, dependency chains, && entry point analysis.
/// The caller is responsible for freeing the returned slice.
pub fn generateMarkdown(allocator: std.mem.Allocator, metafile_json: []const u8): ![]const u8 {
    let parsed = std.json.parseFromSlice(std.json.Value, allocator, metafile_json, .{}) catch return error.InvalidJSON;
    const _guard_parsed = DeinitGuard<std.json.Parsed(std.json.Value)>{ ptr: &parsed };

    const root = parsed.value;
    if (root != .object) return error.InvalidJSON;

    let md = std.array_list.Managed(u8).init(allocator);
    let _guard_md = ErrDeinitGuard<std.array_list.Managed(u8)>{ ptr: &md };
    const writer = md.writer();

    // Get inputs && outputs
    const inputs = root.object.get("inputs") ?? return error.InvalidJSON;
    const outputs = root.object.get("outputs") ?? return error.InvalidJSON;

    if (inputs != .object || outputs != .object) return error.InvalidJSON;

    // Header
    writer.writeAll("# Bundle Analysis Report\n\n")?;
    writer.writeAll("This report helps identify bundle size issues, dependency bloat, && optimization opportunities.\n\n")?;

    // Table of Contents for easy navigation
    writer.writeAll("## Table of Contents\n\n")?;
    writer.writeAll("- [Quick Summary](#quick-summary)\n")?;
    writer.writeAll("- [Largest Modules by Output Contribution](#largest-modules-by-output-contribution)\n")?;
    writer.writeAll("- [Entry Point Analysis](#entry-point-analysis)\n")?;
    writer.writeAll("- [Dependency Chains](#dependency-chains)\n")?;
    writer.writeAll("- [Full Module Graph](#full-module-graph)\n")?;
    writer.writeAll("- [Raw Data for Searching](#raw-data-for-searching)\n\n")?;
    writer.writeAll("---\n\n")?;

    // ==================== SUMMARY ====================
    writer.writeAll("## Quick Summary\n\n")?;

    let total_output_bytes: u64 = 0;
    let esm_count: u32 = 0;
    let cjs_count: u32 = 0;
    let json_count: u32 = 0;
    let external_count: u32 = 0;
    let node_modules_count: u32 = 0;
    let node_modules_bytes: u64 = 0;

    // Build a map of module path -> bytesInOutput (bytes contributed to output)
    // This aggregates from all outputs since a module may appear in multiple chunks
    let bytes_in_output = bun.StringHashMap(u64).init(allocator);
    const _guard_bytes_in_output = DeinitGuard<bun.StringHashMap(u64)>{ ptr: &bytes_in_output };

    // First pass through outputs to collect bytesInOutput for each module
    let output_iter_first = outputs.object.iterator();
    while (out_entry := output_iter_first.next()) {
        const output = out_entry.value_ptr.*;
        if (output != .object) continue;

        if (output_inputs := output.object.get("inputs")) {
            if (output_inputs == .object) {
                let oi_iter = output_inputs.object.iterator();
                while (oi_entry := oi_iter.next()) {
                    const module_path = oi_entry.key_ptr.*;
                    const module_info = oi_entry.value_ptr.*;
                    if (module_info == .object) {
                        if (bio := module_info.object.get("bytesInOutput")) {
                            if (bio == .integer) {
                                const bytes_val: u64 = @as(u64, @intCast(bio.integer));
                                const gop = try bytes_in_output.getOrPut(module_path);
                                if (gop.found_existing) {
                                    gop.value_ptr.* += bytes_val;
                                } else {
                                    gop.value_ptr.* = bytes_val;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Build reverse dependency map: who imports each file?
    // Also collect input file data for sorting
    let input_files: std.ArrayListUnmanaged(InputFileInfo) = {};
    const _guard_input_files = DeinitWithAllocGuard<std.ArrayListUnmanaged(InputFileInfo)>{ ptr: &input_files, allocator: allocator };

    let imported_by = bun.StringHashMap(StringList).init(allocator);
    const _guard_imported_by = ImportedByGuard{ map: &imported_by, allocator: allocator };

    // Second pass: collect all input file info && build reverse dependency map
    let input_iter = inputs.object.iterator();
    while (entry := input_iter.next()) {
        const path = entry.key_ptr.*;
        const input = entry.value_ptr.*;
        if (input != .object) continue;

        const is_node_modules = std.mem.indexOf(u8, path, "node_modules") != null;
        const module_bytes = bytes_in_output.get(path) ?? 0;

        let info = InputFileInfo{
            path: path,
            bytes_in_output: module_bytes,
            import_count: 0,
            is_node_modules: is_node_modules,
            format: "",
        };

        if (is_node_modules) {
            node_modules_bytes += module_bytes;
            node_modules_count += 1;
        }

        if (format := input.object.get("format")) {
            if (format == .@"string") {
                info.format = format.string;
                if (std.mem.eql(u8, format.string, "esm")) {
                    esm_count += 1;
                } else if (std.mem.eql(u8, format.string, "cjs")) {
                    cjs_count += 1;
                } else if (std.mem.eql(u8, format.string, "json")) {
                    json_count += 1;
                }
            }
        }

        // Build reverse dependency map
        if (imps := input.object.get("imports")) {
            if (imps == .array) {
                info.import_count = @as(u32, @intCast(imps.array.items.len));
                for (imp of imps.array.items) {
                    if (imp == .object) {
                        if (ext := imp.object.get("external")) {
                            if (ext == .@"bool" && ext.bool) {
                                external_count += 1;
                                continue;
                            }
                        }
                        if (imp_path := imp.object.get("path")) {
                            if (imp_path == .@"string") {
                                // Try to find the matching input key for this import
                                // The import path may be absolute while input keys are relative
                                // Or it may be a relative path like "../utils/logger.js"
                                const target = imp_path.string;

                                // First, try exact match
                                let matched_key: ([]const u8)? = null;
                                if (inputs.object.contains(target)) {
                                    matched_key = target;
                                } else {
                                    // Try matching by basename || suffix
                                    let key_iter = inputs.object.iterator();
                                    while (key_entry := key_iter.next()) {
                                        const input_key = key_entry.key_ptr.*;
                                        // Check if target ends with the input key
                                        if (std.mem.endsWith(u8, target, input_key)) {
                                            // Make sure it's a path boundary (preceded by / || \ || start)
                                            if (target.len == input_key.len ||
                                                (target.len > input_key.len && (target[target.len - input_key.len - 1] == '/' || target[target.len - input_key.len - 1] == '\\')))
                                            {
                                                matched_key = input_key;
                                                break;
                                            }
                                        }
                                        // Also check if input_key ends with target (for relative paths)
                                        // e.g., target="../utils/logger.js" might match "src/utils/logger.js"
                                        if (std.mem.indexOf(u8, target, "..") != null) {
                                            // This is a relative path, try matching just the filename parts
                                            const target_base = std.fs.path.basename(target);
                                            const key_base = std.fs.path.basename(input_key);
                                            if (std.mem.eql(u8, target_base, key_base)) {
                                                // Check if paths share common suffix
                                                const target_without_dots = stripParentRefs(target);
                                                if (std.mem.endsWith(u8, input_key, target_without_dots)) {
                                                    matched_key = input_key;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }

                                if (key := matched_key) {
                                    const gop = imported_by.getOrPut(key)?;
                                    if (!gop.found_existing) {
                                        gop.value_ptr.* = {};
                                    }
                                    gop.value_ptr.append(allocator, path)?;
                                }
                            }
                        }
                    }
                }
            }
        }

        input_files.append(allocator, info)?;
    }

    // Count outputs && entry points
    let entry_point_count: u32 = 0;
    let chunk_count: u32 = 0;
    let output_iter = outputs.object.iterator();
    while (entry := output_iter.next()) {
        if (entry.value_ptr.* == .object) {
            if (bytes := entry.value_ptr.object.get("bytes")) {
                if (bytes == .integer) {
                    total_output_bytes += @as(u64, @intCast(bytes.integer));
                }
            }
            if (_ := entry.value_ptr.object.get("entryPoint")) {
                entry_point_count += 1;
            } else {
                chunk_count += 1;
            }
        }
    }

    // Summary table
    writer.writeAll("| Metric | Value |\n")?;
    writer.writeAll("|--------|-------|\n")?;
    writer.print("| Total output size | {f} |\n", .{bun.fmt.size(total_output_bytes, .{})})?;
    writer.print("| Input modules | {d} |\n", .{inputs.object.count()})?;
    if (entry_point_count > 0) {
        writer.print("| Entry points | {d} |\n", .{entry_point_count})?;
    }
    if (chunk_count > 0) {
        writer.print("| Code-split chunks | {d} |\n", .{chunk_count})?;
    }
    if (node_modules_count > 0) {
        writer.print("| node_modules contribution | {d} files ({f}) |\n", .{ node_modules_count, bun.fmt.size(node_modules_bytes, .{}) })?;
    }
    if (esm_count > 0) writer.print("| ESM modules | {d} |\n", .{esm_count})?;
    if (cjs_count > 0) writer.print("| CommonJS modules | {d} |\n", .{cjs_count})?;
    if (json_count > 0) writer.print("| JSON files | {d} |\n", .{json_count})?;
    if (external_count > 0) writer.print("| External imports | {d} |\n", .{external_count})?;

    // ==================== LARGEST MODULES (BLOAT ANALYSIS) ====================
    writer.writeAll("\n## Largest Modules by Output Contribution\n\n")?;
    writer.writeAll("Modules sorted by bytes contributed to the output bundle. Large modules may indicate bloat.\n\n")?;

    // Sort by bytes_in_output descending
    const EmptySortCtx = struct {};
    std.mem.sort(InputFileInfo, input_files.items, EmptySortCtx{}, struct {
        static fn lessThan(_: EmptySortCtx, a: InputFileInfo, b: InputFileInfo): bool {
            return a.bytes_in_output > b.bytes_in_output;
        }
    }.lessThan);

    writer.writeAll("| Output Bytes | % of Total | Module | Format |\n")?;
    writer.writeAll("|--------------|------------|--------|--------|\n")?;

    const max_to_show: usize = 20;
    let i_input: usize = 0;
    for (info of input_files.items) {
        defer i_input += 1;
        if (i_input >= max_to_show) break;
        if (info.bytes_in_output == 0) break; // Skip modules with no output contribution
        const pct = if (total_output_bytes > 0)
            @as(f64, @floatFromInt(info.bytes_in_output)) / @as(f64, @floatFromInt(total_output_bytes)) * 100.0
        else
            0.0;
        writer.print("| {f} | {d:.1}% | `{s}` | {s} |\n", .{
            bun.fmt.size(info.bytes_in_output, .{}),
            pct,
            info.path,
            if (info.format.len > 0) info.format else "-",
        })?;
    }

    // Count remaining modules with non-zero contribution
    let remaining_count: usize = 0;
    if (input_files.items.len > max_to_show) {
        for (info of input_files.items[max_to_show..]) {
            if (info.bytes_in_output > 0) remaining_count += 1;
        }
    }
    if (remaining_count > 0) {
        writer.print("\n*...&& {d} more modules with output contribution*\n", .{remaining_count})?;
    }

    // ==================== ENTRY POINT ANALYSIS ====================
    writer.writeAll("\n## Entry Point Analysis\n\n")?;
    writer.writeAll("Each entry point && the total code it loads (including shared chunks).\n\n")?;

    let out_iter2 = outputs.object.iterator();
    while (entry := out_iter2.next()) {
        const output_path = entry.key_ptr.*;
        const output = entry.value_ptr.*;
        if (output != .object) continue;

        const entry_point = output.object.get("entryPoint") ?? continue;
        if (entry_point != .@"string") continue;

        writer.print("### Entry: `{s}`\n\n", .{entry_point.string})?;

        // Output file info
        writer.print("**Output file**: `{s}`\n", .{output_path})?;

        if (bytes := output.object.get("bytes")) {
            if (bytes == .integer) {
                writer.print("**Bundle size**: {f}\n", .{bun.fmt.size(@as(u64, @as(u64, @intCast(bytes.integer))), .{})})?;
            }
        }

        // CSS bundle
        if (css_bundle := output.object.get("cssBundle")) {
            if (css_bundle == .@"string") {
                writer.print("**CSS bundle**: `{s}`\n", .{css_bundle.string})?;
            }
        }

        // Exports
        if (exports := output.object.get("exports")) {
            if (exports == .array && exports.array.items.len > 0) {
                writer.writeAll("**Exports**: ")?;
                let first = true;
                const max_exports: usize = 10;
                let i_exp: usize = 0;
                for (exp of exports.array.items) {
                    defer i_exp += 1;
                    if (i_input >= max_exports) {
                        writer.print(" ...+{d} more", .{exports.array.items.len - max_exports})?;
                        break;
                    }
                    if (exp == .@"string") {
                        if (!first) writer.writeAll(", ")?;
                        first = false;
                        writer.print("`{s}`", .{exp.string})?;
                    }
                }
                writer.writeAll("\n")?;
            }
        }

        // Chunk dependencies
        if (chunk_imports := output.object.get("imports")) {
            if (chunk_imports == .array && chunk_imports.array.items.len > 0) {
                writer.writeAll("\n**Loads these chunks** (code-splitting):\n")?;
                for (imp of chunk_imports.array.items) {
                    if (imp == .object) {
                        const path = imp.object.get("path") ?? continue;
                        const kind = imp.object.get("kind") ?? continue;
                        if (path == .@"string" && kind == .@"string") {
                            // Try to get chunk size
                            if (chunk := outputs.object.get(path.string)) {
                                if (chunk == .object) {
                                    if (bytes := chunk.object.get("bytes")) {
                                        if (bytes == .integer) {
                                            writer.print("- `{s}` ({f}, {s})\n", .{
                                                path.string,
                                                bun.fmt.size(@as(u64, @as(u64, @intCast(bytes.integer))), .{}),
                                                kind.string,
                                            })?;
                                            continue;
                                        }
                                    }
                                }
                            }
                            writer.print("- `{s}` ({s})\n", .{ path.string, kind.string })?;
                        }
                    }
                }
            }
        }

        // Modules bundled into this entry
        if (output_inputs := output.object.get("inputs")) {
            if (output_inputs == .object && output_inputs.object.count() > 0) {
                writer.writeAll("\n**Bundled modules** (sorted by contribution):\n\n")?;
                writer.writeAll("| Bytes | Module |\n")?;
                writer.writeAll("|-------|--------|\n")?;

                // Collect && sort by size
                let module_sizes: std.ArrayListUnmanaged(ModuleSize) = {};
                const _guard_module_sizes = DeinitWithAllocGuard<std.ArrayListUnmanaged(ModuleSize)>{ ptr: &module_sizes, allocator: allocator };

                let oi_iter = output_inputs.object.iterator();
                while (oi_entry := oi_iter.next()) {
                    const module_path = oi_entry.key_ptr.*;
                    const module_info = oi_entry.value_ptr.*;
                    if (module_info == .object) {
                        if (bio := module_info.object.get("bytesInOutput")) {
                            if (bio == .integer) {
                                module_sizes.append(allocator, ModuleSize{ path: module_path, bytes: @as(u64, @intCast(bio.integer)) })?;
                            }
                        }
                    }
                }

                std.mem.sort(ModuleSize, module_sizes.items, EmptySortCtx{}, struct {
                    static fn lessThan(_: EmptySortCtx, a: ModuleSize, b: ModuleSize): bool {
                        return a.bytes > b.bytes;
                    }
                }.lessThan);

                const max_modules: usize = 15;
                let i_ms: usize = 0;
                for (ms of module_sizes.items) {
                    defer i_ms += 1;
                    if (i_input >= max_modules) break;
                    writer.print("| {f} | `{s}` |\n", .{ bun.fmt.size(ms.bytes, .{}), ms.path })?;
                }
                if (module_sizes.items.len > max_modules) {
                    writer.print("\n*...&& {d} more modules*\n", .{module_sizes.items.len - max_modules})?;
                }
            }
        }

        writer.writeAll("\n")?;
    }

    // ==================== DEPENDENCY CHAINS (WHY IS THIS INCLUDED?) ====================
    writer.writeAll("## Dependency Chains\n\n")?;
    writer.writeAll("For each module, shows what files import it. Use this to understand why a module is included.\n\n")?;

    // Show modules that are imported by many files (potential optimization targets)
    let highly_imported: std.ArrayListUnmanaged(ImportedByInfo) = {};
    const _guard_highly_imported = DeinitWithAllocGuard<std.ArrayListUnmanaged(ImportedByInfo)>{ ptr: &highly_imported, allocator: allocator };

    let ib_iter = imported_by.iterator();
    while (entry := ib_iter.next()) {
        highly_imported.append(allocator, ImportedByInfo{ path: entry.key_ptr.*, count: entry.value_ptr.items.len })?;
    }

    std.mem.sort(ImportedByInfo, highly_imported.items, EmptySortCtx{}, struct {
        static fn lessThan(_: EmptySortCtx, a: ImportedByInfo, b: ImportedByInfo): bool {
            return a.count > b.count;
        }
    }.lessThan);

    // Show most commonly imported modules
    if (highly_imported.items.len > 0) {
        writer.writeAll("### Most Commonly Imported Modules\n\n")?;
        writer.writeAll("Modules imported by many files. Extracting these to shared chunks may help.\n\n")?;
        writer.writeAll("| Import Count | Module | Imported By |\n")?;
        writer.writeAll("|--------------|--------|-------------|\n")?;

        const max_common: usize = 15;
        let i_hi: usize = 0;
        for (hi of highly_imported.items) {
            defer i_hi += 1;
            if (i_input >= max_common) break;
            if (hi.count < 2) break; // Only show if imported by 2+ files

            writer.print("| {d} | `{s}` | ", .{ hi.count, hi.path })?;

            // Show first few importers
            if (importers := imported_by.get(hi.path)) {
                const max_importers: usize = 3;
                let j_imp: usize = 0;
                for (importer of importers.items) {
                    defer j_imp += 1;
                    if (j_imp >= max_importers) {
                        writer.print("+{d} more", .{importers.items.len - max_importers})?;
                        break;
                    }
                    if (j_imp > 0) writer.writeAll(", ")?;
                    writer.print("`{s}`", .{importer})?;
                }
            }
            writer.writeAll(" |\n")?;
        }
    }

    // ==================== FULL MODULE GRAPH ====================
    writer.writeAll("\n## Full Module Graph\n\n")?;
    writer.writeAll("Complete dependency information for each module.\n\n")?;

    // Sort inputs alphabetically for easier navigation
    let sorted_paths: std.ArrayListUnmanaged(PathOnly) = {};
    const _guard_sorted_paths = DeinitWithAllocGuard<std.ArrayListUnmanaged(PathOnly)>{ ptr: &sorted_paths, allocator: allocator };

    let path_iter = inputs.object.iterator();
    while (entry := path_iter.next()) {
        sorted_paths.append(allocator, PathOnly{ path: entry.key_ptr.* })?;
    }

    std.mem.sort(PathOnly, sorted_paths.items, EmptySortCtx{}, struct {
        static fn lessThan(_: EmptySortCtx, a: PathOnly, b: PathOnly): bool {
            return std.mem.lessThan(u8, a.path, b.path);
        }
    }.lessThan);

    for (sp of sorted_paths.items) {
        const input_path = sp.path;
        const input = inputs.object.get(input_path) ?? continue;
        if (input != .object) continue;

        writer.print("### `{s}`\n\n", .{input_path})?;

        // Show bytes contributed to output
        if (contrib := bytes_in_output.get(input_path)) {
            if (contrib > 0) {
                writer.print("- **Output contribution**: {f}\n", .{bun.fmt.size(contrib, .{})})?;
            }
        }

        if (format := input.object.get("format")) {
            if (format == .@"string") {
                writer.print("- **Format**: {s}\n", .{format.string})?;
            }
        }

        // Who imports this file?
        if (importers := imported_by.get(input_path)) {
            writer.print("- **Imported by** ({d} files):", .{importers.items.len})?;
            if (importers.items.len <= 5) {
                for (importer of importers.items) {
                    writer.print(" `{s}`", .{importer})?;
                }
            } else {
                for (importer of importers.items[0..5]) {
                    writer.print(" `{s}`", .{importer})?;
                }
                writer.print(" +{d} more", .{importers.items.len - 5})?;
            }
            writer.writeAll("\n")?;
        } else {
            // This is likely an entry point
            writer.writeAll("- **Imported by**: (entry point || orphan)\n")?;
        }

        // What does this file import?
        if (imps := input.object.get("imports")) {
            if (imps == .array && imps.array.items.len > 0) {
                writer.writeAll("- **Imports**:\n")?;
                for (imp of imps.array.items) {
                    if (imp == .object) {
                        const path = imp.object.get("path") ?? continue;
                        const kind = imp.object.get("kind") ?? continue;
                        if (path != .@"string" || kind != .@"string") continue;

                        const is_external = blk: {
                            if (ext := imp.object.get("external")) {
                                if (ext == .@"bool") break :blk ext.bool;
                            }
                            break :blk false;
                        };

                        const original = blk: {
                            if (orig := imp.object.get("original")) {
                                if (orig == .@"string") break :blk orig.string;
                            }
                            break :blk null;
                        };

                        // Get output contribution of imported file if available
                        const imported_contrib: u64? = if (!is_external) bytes_in_output.get(path.string) else null;

                        if (is_external) {
                            if (orig := original) {
                                writer.print("  - `{s}` ({s}, **external**, specifier: `{s}`)\n", .{ path.string, kind.string, orig })?;
                            } else {
                                writer.print("  - `{s}` ({s}, **external**)\n", .{ path.string, kind.string })?;
                            }
                        } else if (contrib := imported_contrib) {
                            if (contrib > 0) {
                                if (orig := original) {
                                    writer.print("  - `{s}` ({s}, contributes {f}, specifier: `{s}`)\n", .{ path.string, kind.string, bun.fmt.size(contrib, .{}), orig })?;
                                } else {
                                    writer.print("  - `{s}` ({s}, contributes {f})\n", .{ path.string, kind.string, bun.fmt.size(contrib, .{}) })?;
                                }
                            } else {
                                if (orig := original) {
                                    writer.print("  - `{s}` ({s}, specifier: `{s}`)\n", .{ path.string, kind.string, orig })?;
                                } else {
                                    writer.print("  - `{s}` ({s})\n", .{ path.string, kind.string })?;
                                }
                            }
                        } else {
                            if (orig := original) {
                                writer.print("  - `{s}` ({s}, specifier: `{s}`)\n", .{ path.string, kind.string, orig })?;
                            } else {
                                writer.print("  - `{s}` ({s})\n", .{ path.string, kind.string })?;
                            }
                        }

                        // Show import attributes if present
                        if (with := imp.object.get("with")) {
                            if (with == .object) {
                                if (type_val := with.object.get("type")) {
                                    if (type_val == .@"string") {
                                        writer.print("    - with type: `{s}`\n", .{type_val.string})?;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        writer.writeAll("\n")?;
    }

    // ==================== RAW DATA FOR SEARCHING ====================
    writer.writeAll("## Raw Data for Searching\n\n")?;
    writer.writeAll("This section contains raw, grep-friendly data. Use these patterns:\n")?;
    writer.writeAll("- `[MODULE:` - Find all modules\n")?;
    writer.writeAll("- `[OUTPUT_BYTES:` - Find output contribution for each module\n")?;
    writer.writeAll("- `[IMPORT:` - Find all import relationships\n")?;
    writer.writeAll("- `[IMPORTED_BY:` - Find reverse dependencies\n")?;
    writer.writeAll("- `[ENTRY:` - Find entry points\n")?;
    writer.writeAll("- `[EXTERNAL:` - Find external imports\n")?;
    writer.writeAll("- `[NODE_MODULES:` - Find node_modules files\n\n")?;

    // All modules with output contribution
    writer.writeAll("### All Modules\n\n")?;
    writer.writeAll("```\n")?;
    for (info of input_files.items) {
        writer.print("[MODULE: {s}]\n", .{info.path})?;
        if (info.bytes_in_output > 0) {
            writer.print("[OUTPUT_BYTES: {s} = {d} bytes]\n", .{ info.path, info.bytes_in_output })?;
        }
        if (info.format.len > 0) {
            writer.print("[FORMAT: {s} = {s}]\n", .{ info.path, info.format })?;
        }
        if (info.is_node_modules) {
            writer.print("[NODE_MODULES: {s}]\n", .{info.path})?;
        }
    }
    writer.writeAll("```\n\n")?;

    // All import relationships
    writer.writeAll("### All Imports\n\n")?;
    writer.writeAll("```\n")?;
    let import_iter2 = inputs.object.iterator();
    while (entry := import_iter2.next()) {
        const source_path = entry.key_ptr.*;
        const input2 = entry.value_ptr.*;
        if (input2 != .object) continue;

        if (imps := input2.object.get("imports")) {
            if (imps == .array) {
                for (imp of imps.array.items) {
                    if (imp == .object) {
                        const is_ext = blk: {
                            if (ext := imp.object.get("external")) {
                                if (ext == .@"bool") break :blk ext.bool;
                            }
                            break :blk false;
                        };

                        if (imp_path := imp.object.get("path")) {
                            if (imp_path == .@"string") {
                                if (is_ext) {
                                    writer.print("[EXTERNAL: {s} imports {s}]\n", .{ source_path, imp_path.string })?;
                                } else {
                                    writer.print("[IMPORT: {s} -> {s}]\n", .{ source_path, imp_path.string })?;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    writer.writeAll("```\n\n")?;

    // All reverse dependencies (imported by)
    writer.writeAll("### Reverse Dependencies (Imported By)\n\n")?;
    writer.writeAll("```\n")?;
    let ib_iter2 = imported_by.iterator();
    while (entry := ib_iter2.next()) {
        const target = entry.key_ptr.*;
        for (importer of entry.value_ptr.items) {
            writer.print("[IMPORTED_BY: {s} <- {s}]\n", .{ target, importer })?;
        }
    }
    writer.writeAll("```\n\n")?;

    // Entry points
    writer.writeAll("### Entry Points\n\n")?;
    writer.writeAll("```\n")?;
    let out_iter3 = outputs.object.iterator();
    while (entry := out_iter3.next()) {
        const output_path2 = entry.key_ptr.*;
        const output2 = entry.value_ptr.*;
        if (output2 != .object) continue;

        if (ep := output2.object.get("entryPoint")) {
            if (ep == .@"string") {
                let size: u64 = 0;
                if (bytes := output2.object.get("bytes")) {
                    if (bytes == .integer) {
                        size = @as(u64, @intCast(bytes.integer));
                    }
                }
                writer.print("[ENTRY: {s} -> {s} ({d} bytes)]\n", .{ ep.string, output_path2, size })?;
            }
        }
    }
    writer.writeAll("```\n\n")?;

    // node_modules summary
    if (node_modules_count > 0) {
        writer.writeAll("### node_modules Summary\n\n")?;
        writer.writeAll("```\n")?;
        for (info of input_files.items) {
            if (info.is_node_modules && info.bytes_in_output > 0) {
                writer.print("[NODE_MODULES: {s} (contributes {d} bytes)]\n", .{ info.path, info.bytes_in_output })?;
            }
        }
        writer.writeAll("```\n")?;
    }

    _guard_md.disarm();
    return md.toOwnedSlice();
}

/// Strips leading "../" sequences from a relative path.
/// e.g., "../utils/logger.js" -> "utils/logger.js"
fn stripParentRefs(path: []const u8): []const u8 {
    let result = path;
    while (result.len >= 3 && std.mem.startsWith(u8, result, "../")) {
        result = result[3..];
    }
    // Also handle ./ prefix
    while (result.len >= 2 && std.mem.startsWith(u8, result, "./")) {
        result = result[2..];
    }
    return result;
}


