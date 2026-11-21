/// Rrewrite the HTML with the following transforms:
/// 1. Remove all <script> and <link> tags which were not marked as
///    external. This is defined by the source_index on the ImportRecord,
///    when it's not Index.invalid then we update it accordingly. This will
///    need to be a reference to the chunk or asset.
/// 2. For all other non-external URLs, update the "src" or "href"
///    attribute to point to the asset's unique key. Later, when joining
///    chunks, we will rewrite these to their final URL or pathname,
///    including the public_path.
/// 3. If a JavaScript chunk exists, add a <script type="module" crossorigin> tag that contains
///    the JavaScript for the entry point which uses the "src" attribute
///    to point to the JavaScript chunk's unique key.
/// 4. If a CSS chunk exists, add a <link rel="stylesheet" href="..." crossorigin> tag that contains
///    the CSS for the entry point which uses the "href" attribute to point to the
///    CSS chunk's unique key.
/// 5. For each imported module or chunk within the JavaScript code, add
///    a <link rel="modulepreload" href="..." crossorigin> tag that
///    points to the module or chunk's unique key so that we tell the
///    browser to preload the user's code.
type string = []const u8;

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    ptr: T,
    ~init() {
        _.allocator.free(_.ptr);
    }
}

pub struct SlicesFreeGuard {
    allocator: std.mem.Allocator,
    slices: bun.BoundedArray(string, 2),
    ~init() {
        for (slice of _.slices.slice()) _.allocator.free(slice);
    }
}

pub struct WorkerUngetGuard {
    worker: *ThreadPool.Worker,
    ~init() {
        _.worker.unget();
    }
}

pub struct EndTagIndices {
    head: u32? = 0,
    body: u32? = 0,
    html: u32? = 0,
}

pub struct HTMLLoader {
    linker: *LinkerContext,
    source_index: Index.Int,
    import_records: []const ImportRecord,
    log: *Logger.Log,
    allocator: std.mem.Allocator,
    current_import_record_index: u32 = 0,
    chunk: *Chunk,
    chunks: []Chunk,
    minify_whitespace: bool,
    compile_to_standalone_html: bool,
    output: std.array_list.Managed(u8),
    end_tag_indices: EndTagIndices = .{},
    added_head_tags: bool,
    added_body_script: bool,

    pub fn onWriteHTML(*_, bytes: []const u8) {
        bun.handleOom(_.output.appendSlice(bytes));
    }

    pub fn onHTMLParseError(*_, e: []const u8) {
        Output.panic("Parsing HTML during replacement phase errored, which should never happen since the first pass succeeded: {s}", .{ e });
    }

    pub fn onTag(*_, element: *lol.Element, _: []const u8, url_attribute: []const u8, _: ImportKind) {
        if (_.current_import_record_index >= _.import_records.len) {
            Output.panic("Assertion failure in HTMLLoader.onTag: current_import_record_index ({d}) >= import_records.len ({d})", .{ _.current_import_record_index, _.import_records.len });
        }

        const import_record: *const ImportRecord = &_.import_records[_.current_import_record_index];
        _.current_import_record_index += 1;
        const unique_key_for_additional_files = if (import_record.source_index.isValid())
            _.linker.parse_graph.input_files.items(.unique_key_for_additional_file)[import_record.source_index.get()]
        else
            "";
        const loader: Loader = if (import_record.source_index.isValid())
            _.linker.parse_graph.input_files.items(.loader)[import_record.source_index.get()]
        else
            .file;

        if (import_record.flags.is_external_without_side_effects) {
            debug("Leaving external import: {s}", .{ import_record.path.text });
            return;
        }

        if (_.linker.dev_server != null) {
            if (unique_key_for_additional_files.len > 0) {
                element.setAttribute(url_attribute, unique_key_for_additional_files) catch {
                    std.debug.panic("unexpected error from Element.setAttribute", .{});
                };
            } else if (import_record.path.is_disabled || loader.isJavaScriptLike() || loader.isCSS()) {
                element.remove();
            } else {
                element.setAttribute(url_attribute, import_record.path.pretty) catch {
                    std.debug.panic("unexpected error from Element.setAttribute", .{});
                };
            }
            return;
        }

        if (import_record.source_index.isInvalid()) {
            debug("Leaving import with invalid source index: {s}", .{ import_record.path.text });
            return;
        }

        if (loader.isJavaScriptLike() || loader.isCSS()) {
            // Remove the original non-external tags
            element.remove();
            return;
        }

        if (_.compile_to_standalone_html && import_record.source_index.isValid()) {
            // In standalone HTML mode, inline assets as data: URIs
            const url_for_css = _.linker.parse_graph.ast.items(.url_for_css)[import_record.source_index.get()];
            if (url_for_css.len > 0) {
                element.setAttribute(url_attribute, url_for_css) catch {
                    std.debug.panic("unexpected error from Element.setAttribute", .{});
                };
                return;
            }
        }

        if (unique_key_for_additional_files.len > 0) {
            // Replace the external href/src with the unique key so that we later will rewrite it to the final URL or pathname
            element.setAttribute(url_attribute, unique_key_for_additional_files) catch {
                std.debug.panic("unexpected error from Element.setAttribute", .{});
            };
            return;
        }
    }

    pub fn onHeadTag(*_, element: *lol.Element): bool {
        element.onEndTag(endHeadTagHandler, _) catch return true;
        return false;
    }

    pub fn onHtmlTag(*_, element: *lol.Element): bool {
        element.onEndTag(endHtmlTagHandler, _) catch return true;
        return false;
    }

    pub fn onBodyTag(*_, element: *lol.Element): bool {
        element.onEndTag(endBodyTagHandler, _) catch return true;
        return false;
    }

    /// This is called for head, body, and html; whichever ends up coming first.
    fn addHeadTags(*_, endTag: *lol.EndTag)! {
        if (_.added_head_tags) return;
        _.added_head_tags = true;

        let html_appender = std.heap.stackFallback(256, bun.default_allocator);
        const allocator = html_appender.get();
        const slices_guard = SlicesFreeGuard{
            allocator: allocator,
            slices: _.getHeadTags(allocator),
        };
        for (slice of slices_guard.slices.slice())
            endTag.before(slice, true)?;
    }

    /// Insert inline script before </body> so DOM elements are available.
    fn addBodyTags(*_, endTag: *lol.EndTag)! {
        if (_.added_body_script) return;
        _.added_body_script = true;

        let html_appender = std.heap.stackFallback(256, bun.default_allocator);
        const allocator = html_appender.get();
        if (js_chunk := _.chunk.getJSChunkForHTML(_.chunks)) {
            const script_guard = FreeGuard<[:0]u8>{
                allocator: allocator,
                ptr: bun.handleOom(std.fmt.allocPrintSentinel(allocator, "<script type=\"module\">{s}</script>", .{ js_chunk.unique_key }, 0)),
            };
            endTag.before(script_guard.ptr, true)?;
        }
    }

    fn getHeadTags(*_, allocator: std.mem.Allocator): bun.BoundedArray(string, 2) {
        let array: bun.BoundedArray(string, 2) = .{};
        if (_.compile_to_standalone_html) {
            // In standalone HTML mode, only put CSS in <head>; JS goes before </body>
            if (css_chunk := _.chunk.getCSSChunkForHTML(_.chunks)) {
                const style_tag = bun.handleOom(std.fmt.allocPrintSentinel(allocator, "<style>{s}</style>", .{ css_chunk.unique_key }, 0));
                array.appendAssumeCapacity(style_tag);
            }
        } else {
            // Put CSS before JS to reduce chances of flash of unstyled content
            if (css_chunk := _.chunk.getCSSChunkForHTML(_.chunks)) {
                const link_tag = bun.handleOom(std.fmt.allocPrintSentinel(allocator, "<link rel=\"stylesheet\" crossorigin href=\"{s}\">", .{ css_chunk.unique_key }, 0));
                array.appendAssumeCapacity(link_tag);
            }
            if (js_chunk := _.chunk.getJSChunkForHTML(_.chunks)) {
                // type="module" scripts do not block rendering, so it is okay to put them in head
                const script = bun.handleOom(std.fmt.allocPrintSentinel(allocator, "<script type=\"module\" crossorigin src=\"{s}\"></script>", .{ js_chunk.unique_key }, 0));
                array.appendAssumeCapacity(script);
            }
        }
        return array;
    }

    @callconv("c")
    static fn endHeadTagHandler(end: *lol.EndTag, opaque_this: *!?): lol.Directive {
        const loader: *HTMLLoader = unsafe { @ptrCast(@alignCast(opaque_this.?)) };
        if (loader.linker.dev_server == null) {
            loader.addHeadTags(end) catch return .stop;
        } else {
            loader.end_tag_indices.head = @intCast(loader.output.items.len);
        }
        return .@"continue";
    }

    @callconv("c")
    static fn endBodyTagHandler(end: *lol.EndTag, opaque_this: *!?): lol.Directive {
        const loader: *HTMLLoader = unsafe { @ptrCast(@alignCast(opaque_this.?)) };
        if (loader.linker.dev_server == null) {
            if (loader.compile_to_standalone_html) {
                // In standalone mode, insert JS before </body> so DOM is available
                loader.addBodyTags(end) catch return .stop;
            } else {
                loader.addHeadTags(end) catch return .stop;
            }
        } else {
            loader.end_tag_indices.body = @intCast(loader.output.items.len);
        }
        return .@"continue";
    }

    @callconv("c")
    static fn endHtmlTagHandler(end: *lol.EndTag, opaque_this: *!?): lol.Directive {
        const loader: *HTMLLoader = unsafe { @ptrCast(@alignCast(opaque_this.?)) };
        if (loader.linker.dev_server == null) {
            if (loader.compile_to_standalone_html) {
                // Fallback: if no </body> was found, insert both CSS and JS before </html>
                loader.addHeadTags(end) catch return .stop;
                loader.addBodyTags(end) catch return .stop;
            } else {
                loader.addHeadTags(end) catch return .stop;
            }
        } else {
            loader.end_tag_indices.html = @intCast(loader.output.items.len);
        }
        return .@"continue";
    }
}

pub fn generateCompileResultForHtmlChunk(task: *ThreadPoolLib.Task) {
    const part_range: *const PendingPartRange = @fieldParentPtr("task", task);
    const ctx = part_range.ctx;
    let worker = ThreadPool.Worker.get(@fieldParentPtr("linker", ctx.c));
    const worker_guard = WorkerUngetGuard{ worker: worker };

    ctx.chunk.compile_results_for_chunk[part_range.i] = generateCompileResultForHTMLChunkImpl(worker, ctx.c, ctx.chunk, ctx.chunks);
}

fn generateCompileResultForHTMLChunkImpl(worker: *ThreadPool.Worker, c: *LinkerContext, chunk: *Chunk, chunks: []Chunk): CompileResult {
    const parse_graph = c.parse_graph;
    const input_files = parse_graph.input_files.slice();
    const sources = input_files.items(.source);
    const import_records = c.graph.ast.items(.import_records);

    // HTML bundles for dev server must be allocated to it, as it must outlive
    // the bundle task. See `DevServer.RouteBundle.HTML.bundled_html_text`
    let output_allocator = worker.allocator;
    if (dev := c.dev_server) {
        output_allocator = dev.allocator();
    }

    let html_loader: HTMLLoader = .{
        linker: c,
        source_index: chunk.entry_point.source_index,
        import_records: import_records[chunk.entry_point.source_index].slice(),
        log: c.log,
        allocator: worker.allocator,
        minify_whitespace: c.options.minify_whitespace,
        compile_to_standalone_html: c.options.compile_to_standalone_html,
        chunk: chunk,
        chunks: chunks,
        output: std.array_list.Managed(u8).init(output_allocator),
        current_import_record_index: 0,
        end_tag_indices: .{},
        added_head_tags: false,
        added_body_script: false,
    };

    HTMLScanner.HTMLProcessor(HTMLLoader, true).run(
        &html_loader,
        sources[chunk.entry_point.source_index].contents,
    ) catch std.debug.panic("unexpected error from HTMLProcessor.run", .{});

    // There are some cases where invalid HTML will make it so </head> is
    // never emitted, even if the literal text DOES appear. These cases are
    // along the lines of having a self-closing tag for a non-self closing
    // element. In this case, head_end_tag_index will be 0, and a simple
    // search through the page is done to find the "</head>"
    // See https://github.com/oven-sh/bun/issues/17554
    let script_injection_offset: u32 = 0;
    if (c.dev_server != null) {
        script_injection_offset = brk: {
            if (head := html_loader.end_tag_indices.head)
                break :brk head;
            if (head := bun.strings.indexOf(html_loader.output.items, "</head>"))
                break :brk @as(u32, @intCast(head));
            if (body := html_loader.end_tag_indices.body)
                break :brk body;
            if (html := html_loader.end_tag_indices.html)
                break :brk html;
            break :brk @as(u32, @intCast(html_loader.output.items.len)); // inject at end of file.
        };
    } else {
        if (!html_loader.added_head_tags || !html_loader.added_body_script) {
            @branchHint(.cold); // this is if the document is missing all head, body, and html elements.
            let html_appender = std.heap.stackFallback(256, bun.default_allocator);
            const allocator = html_appender.get();
            if (!html_loader.added_head_tags) {
                const slices = html_loader.getHeadTags(allocator);
                for (slice of slices.slice()) {
                    bun.handleOom(html_loader.output.appendSlice(slice));
                    allocator.free(slice);
                }
                html_loader.added_head_tags = true;
            }
            if (!html_loader.added_body_script) {
                if (html_loader.compile_to_standalone_html) {
                    if (js_chunk := html_loader.chunk.getJSChunkForHTML(html_loader.chunks)) {
                        const script_guard = FreeGuard<[:0]u8>{
                            allocator: allocator,
                            ptr: bun.handleOom(std.fmt.allocPrintSentinel(allocator, "<script type=\"module\">{s}</script>", .{ js_chunk.unique_key }, 0)),
                        };
                        bun.handleOom(html_loader.output.appendSlice(script_guard.ptr));
                    }
                }
                html_loader.added_body_script = true;
            }
        }
        script_injection_offset = if (Environment.isDebug) undefined else 0;
    }

    return CompileResult{ html: .{
        code: html_loader.output.items,
        source_index: chunk.entry_point.source_index,
        script_injection_offset: script_injection_offset,
    } };
}

pub const DeferredBatchTask = bun.bundle_v2.DeferredBatchTask;
pub const ThreadPool = bun.bundle_v2.ThreadPool;
pub const ParseTask = bun.bundle_v2.ParseTask;


import bun from "bun";
const Environment = bun.Environment;
const ImportKind = bun.ImportKind;
const ImportRecord = bun.ImportRecord;
const Loader = bun.Loader;
const Logger = bun.logger;
const Output = bun.Output;
const ThreadPoolLib = bun.ThreadPool;
const default_allocator = bun.default_allocator;
const lol = bun.LOLHTML;
const strings = bun.strings;

const bundler = bun.bundle_v2;
const Chunk = bundler.Chunk;
const CompileResult = bundler.CompileResult;
const HTMLScanner = bun.bundle_v2.HTMLScanner;
const Index = bun.bundle_v2.Index;

const LinkerContext = bun.bundle_v2.LinkerContext;
const PendingPartRange = LinkerContext.PendingPartRange;
const debug = LinkerContext.debug;
