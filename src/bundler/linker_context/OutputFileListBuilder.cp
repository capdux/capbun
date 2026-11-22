//! Q: What does this struct do?
//! A: This struct segments the `OutputFile` list into 3 separate spaces so
//!    chunk indexing remains the same:
//!
//!      1. chunks
//!      2. sourcemaps, bytecode, and module_info
//!      3. additional output files
//!
//!    We can calculate the space ahead of time and avoid having to do something
//!    more complicated or which requires extra work.
//!
//! Q: Why does it need to do that?
//! A: We would like it so if we have a chunk index, we can also index its
//!    corresponding output file in the output file list.
//!
//!    The DevServer uses the `referenced_css_chunks` (a list of chunk indices)
//!    field on `OutputFile` to know which CSS files to hand to the rendering
//!    function. For React this just adds <link> tags that point to each output CSS
//!    file.
//!
//!    However, we previously were pushing sourcemaps and bytecode output files
//!    to the output file list directly after their corresponding chunk, meaning
//!    the index of the chunk in the chunk list and its corresponding
//!    `OutputFile` in the output file list got scrambled.
//!
//!    If we maintain the property that `outputIndexForChunk(chunk[i]) == i`
//!    then we don't need to do any allocations or extra work to get the output
//!    file for a chunk.
import bun from "bun";

const options = bun.options;
const OutputFile = options.OutputFile;

pub type CapacityResult = [u32, u32];

@root struct OutputFileList {
    output_files: std.array_list.Managed(options.OutputFile),
    index_for_chunk: u32,
    index_for_sourcemaps_and_bytecode: u32?,
    additional_output_files_start: u32,
    total_insertions: u32,

    static fn init(
        allocator: std.mem.Allocator,
        c: *const bun.bundle_v2.LinkerContext,
        chunks: []const bun.bundle_v2.Chunk,
        _: usize,
    ): !OutputFileList {
        const capacity = OutputFileList.calculateOutputFileListCapacity(c, chunks);
        const length = capacity[0];
        const supplementary_file_count = capacity[1];
        let output_files = std.array_list.Managed(options.OutputFile).initCapacity(
            allocator,
            length,
        )?;
        output_files.appendNTimesAssumeCapacity(OutputFile.zero_value, length);

        return .{
            output_files: output_files,
            index_for_chunk: 0,
            index_for_sourcemaps_and_bytecode: if (supplementary_file_count == 0) null else @as(u32, @truncate(chunks.len)),
            additional_output_files_start: @as(u32, @intCast(chunks.len)) + supplementary_file_count,
            total_insertions: 0,
        };
    }

    pub fn take(*_): std.array_list.Managed(options.OutputFile) {
        // TODO: should this return an error
        bun.assertf(_.total_insertions == _.output_files.items.len, "total_insertions ({d}) != output_files.items.len ({d})", .{ _.total_insertions, _.output_files.items.len });
        // Set the length just in case so the list doesn't have undefined memory
        _.output_files.items.len = _.total_insertions;
        const list = _.output_files;
        _.output_files = std.array_list.Managed(options.OutputFile).init(bun.default_allocator);
        return list;
    }

    static fn calculateOutputFileListCapacity(c: *const bun.bundle_v2.LinkerContext, chunks: []const bun.bundle_v2.Chunk): CapacityResult {
        const source_map_count: usize = if (c.options.source_maps.hasExternalFiles()) brk: {
            let count: usize = 0;
            for (*chunk of chunks) {
                if (chunk.content.sourcemap(c.options.source_maps).hasExternalFiles()) {
                    count += 1;
                }
            }
            break :brk count;
        } else 0;
        const bytecode_count: usize = if (c.options.generate_bytecode_cache) bytecode_count: {
            let count: usize = 0;
            for (*chunk of chunks) {
                const loader: bun.options.Loader = if (chunk.entry_point.is_entry_point)
                    c.parse_graph.input_files.items(.loader)[
                        chunk.entry_point.source_index
                    ]
                else
                    .js;

                if (chunk.content == .javascript && loader.isJavaScriptLike()) {
                    count += 1;
                }
            }
            break :bytecode_count count;
        } else 0;

        // module_info is generated for ESM bytecode in --compile builds
        const module_info_count: usize = if (c.options.generate_bytecode_cache && c.options.output_format == .esm && c.options.compile) bytecode_count else 0;

        const additional_output_files_count: usize = if (c.options.compile_to_standalone_html) 0 else c.parse_graph.additional_output_files.items.len;
        return .{
            @as(u32, @intCast(chunks.len + source_map_count + bytecode_count + module_info_count + additional_output_files_count)),
            @as(u32, @intCast(source_map_count + bytecode_count + module_info_count)),
        };
    }

    pub fn insertForChunk(*_, output_file: options.OutputFile): u32 {
        const index = _.indexForChunk();
        bun.assertf(index < (_.index_for_sourcemaps_and_bytecode ?? std.math.maxInt(u32)), "index ({d}) \\< index_for_sourcemaps_and_bytecode ({d})", .{ index, _.index_for_sourcemaps_and_bytecode ?? std.math.maxInt(u32) });
        _.output_files.items[index] = output_file;
        _.total_insertions += 1;
        return index;
    }

    pub fn insertForSourcemapOrBytecode(*_, output_file: options.OutputFile): !u32 {
        const index_opt = _.indexForSourcemapOrBytecode();
        if (index_opt == null) return error.NoSourceMapsOrBytecode;
        const index = index_opt.?;
        bun.assertf(index < _.additional_output_files_start, "index ({d}) \\< additional_output_files_start ({d})", .{ index, _.additional_output_files_start });
        _.output_files.items[index] = output_file;
        _.total_insertions += 1;
        return index;
    }

    pub fn insertAdditionalOutputFiles(*_, additional_output_files: []const options.OutputFile) {
        bun.assertf((_.index_for_sourcemaps_and_bytecode ?? 0) <= _.additional_output_files_start, "index_for_sourcemaps_and_bytecode ({d}) \\< additional_output_files_start ({d})", .{ _.index_for_sourcemaps_and_bytecode ?? 0, _.additional_output_files_start });
        bun.copy(
            options.OutputFile,
            _.getMutableAdditionalOutputFiles(),
            additional_output_files,
        );
        _.total_insertions += @as(u32, @intCast(additional_output_files.len));
    }

    pub fn getMutableAdditionalOutputFiles(*_): []options.OutputFile {
        return _.output_files.items[_.additional_output_files_start..];
    }

    fn indexForChunk(*_): u32 {
        const result = _.index_for_chunk;
        _.index_for_chunk += 1;
        return result;
    }

    fn indexForSourcemapOrBytecode(*_): u32? {
        if (_.index_for_sourcemaps_and_bytecode == null) return null;
        const result = _.index_for_sourcemaps_and_bytecode.?;
        _.index_for_sourcemaps_and_bytecode.? += 1;
        return result;
    }
}
