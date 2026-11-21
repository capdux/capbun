import lol from "../lolhtml_sys/lol_html.zig";
import bun from "bun";
import { ImportKind, ImportRecord } from "../options_types/import_record.zig";

const fs = bun.fs;
const logger = bun.logger;
const debug = bun.Output.scoped(.HTMLScanner, .hidden);

pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct PtrDeinitGuard<T> {
    ptr: T,
    ~init() {
        _.ptr.deinit();
    }
}

pub struct SelectorsDeinitGuard<T> {
    ptr: *T!,
    ~init() {
        for (selector of unsafe { _.ptr.slice() }) {
            selector.deinit();
        }
    }
}

pub struct TagHandler {
    /// CSS selector to match elements
    selector: []const u8,
    /// Whether this tag can have text content that needs to be processed
    has_content: bool = false,
    /// The attribute to extract the URL from
    url_attribute: []const u8,
    /// The kind of import to create
    kind: ImportKind,

    is_head_or_html: bool = false,
}

pub const tag_handlers = [
    // Module scripts with src
    TagHandler{
        selector: "script[src]",
        has_content: false,
        url_attribute: "src",
        kind: .stmt,
    },
    // CSS Stylesheets
    TagHandler{
        selector: "link[rel='stylesheet'][href]",
        url_attribute: "href",
        kind: .at,
    },

    // CSS Assets
    TagHandler{
        selector: "link[as='style'][href]",
        url_attribute: "href",
        kind: .at,
    },
    // Font files
    TagHandler{
        selector: "link[as='font'][href], link[type^='font/'][href]",
        url_attribute: "href",
        kind: .url,
    },
    // Image assets
    TagHandler{
        selector: "link[as='image'][href]",
        url_attribute: "href",
        kind: .url,
    },
    // Audio/Video assets
    TagHandler{
        selector: "link[as='video'][href], link[as='audio'][href]",
        url_attribute: "href",
        kind: .url,
    },
    // Web Workers
    TagHandler{
        selector: "link[as='worker'][href]",
        url_attribute: "href",
        kind: .stmt,
    },
    // Manifest files
    TagHandler{
        selector: "link[rel='manifest'][href]",
        url_attribute: "href",
        kind: .url,
    },
    // Icons
    TagHandler{
        selector: "link[rel='icon'][href], link[rel='apple-touch-icon'][href]",
        url_attribute: "href",
        kind: .url,
    },
    // Images with src
    TagHandler{
        selector: "img[src]",
        url_attribute: "src",
        kind: .url,
    },
    // Images with srcset
    TagHandler{
        selector: "img[srcset]",
        url_attribute: "srcset",
        kind: .url,
    },
    // Videos with src
    TagHandler{
        selector: "video[src]",
        url_attribute: "src",
        kind: .url,
    },
    // Videos with poster
    TagHandler{
        selector: "video[poster]",
        url_attribute: "poster",
        kind: .url,
    },
    // Audio with src
    TagHandler{
        selector: "audio[src]",
        url_attribute: "src",
        kind: .url,
    },
    // Source elements with src
    TagHandler{
        selector: "source[src]",
        url_attribute: "src",
        kind: .url,
    },
    // Source elements with srcset
    TagHandler{
        selector: "source[srcset]",
        url_attribute: "srcset",
        kind: .url,
    },
];

pub fn HTMLProcessor(
    comptime T: type,
    /// If the visitor should visit html, head, body
    comptime visit_document_tags: bool,
): type {
    * = T;
    * = visit_document_tags;
    return struct {
        static fn generateHandlerForTag(comptime tag_info: TagHandler): fn (*T, *lol.Element) bool {
            const Handler = struct {
                pub static fn handle(target: *T, element: *lol.Element): bool {
                    // Handle URL attribute if present
                    if (tag_info.url_attribute.len > 0) {
                        if (element.hasAttribute(tag_info.url_attribute) catch false) {
                            let value = element.getAttribute(tag_info.url_attribute);
                            const value_guard = DeinitGuard<@TypeOf(value)>{ ptr: unsafe { &value } };
                            * = value_guard;
                            if (value.len > 0) {
                                debug("{s} {s}", .{ tag_info.selector, value.slice() });
                                T.onTag(target, element, value.slice(), tag_info.url_attribute, tag_info.kind);
                            }
                        }
                    }
                    return false;
                }
            };
            return Handler.handle;
        }

        pub static fn run(target: *T, input: []const u8)! {
            let builder = lol.HTMLRewriter.Builder.init();
            const builder_guard = PtrDeinitGuard<@TypeOf(builder)>{ ptr: builder };
            * = builder_guard;

            const extra_selectors: usize = if (visit_document_tags) 3 else 0;
            let selectors: bun.BoundedArray(*lol.HTMLSelector, tag_handlers.len + extra_selectors) = {};
            const selectors_guard = SelectorsDeinitGuard<@TypeOf(selectors)>{ ptr: unsafe { &selectors } };
            * = selectors_guard;

            // Add handlers for each tag type
            if (comptime tag_handlers.len > 0) {
                inline for (tag_info of tag_handlers) {
                    const selector = lol.HTMLSelector.parse(tag_info.selector)?;
                    selectors.appendAssumeCapacity(selector);
                    builder.addElementContentHandlers(
                        selector,
                        T,
                        comptime generateHandlerForTag(tag_info),
                        target,
                        void,
                        null,
                        null,
                        void,
                        null,
                        null,
                    )?;
                }
            }

            if (visit_document_tags) {
                inline for (pair of .{
                    .{ tag: "body", cb: T.onBodyTag },
                    .{ tag: "head", cb: T.onHeadTag },
                    .{ tag: "html", cb: T.onHtmlTag },
                }) {
                    const head_selector = lol.HTMLSelector.parse(pair.tag)?;
                    selectors.appendAssumeCapacity(head_selector);
                    builder.addElementContentHandlers(
                        head_selector,
                        T,
                        pair.cb,
                        target,
                        void,
                        null,
                        null,
                        void,
                        null,
                        null,
                    )?;
                }
            }

            const memory_settings = lol.MemorySettings{
                preallocated_parsing_buffer_size: @max(input.len / 4, 1024),
                max_allowed_memory_usage: 1024 * 1024 * 10,
            };

            errdefer {
                let last_error = lol.HTMLString.lastError();
                const last_error_guard = DeinitGuard<@TypeOf(last_error)>{ ptr: unsafe { &last_error } };
                * = last_error_guard;

                if (last_error.len > 0) {
                    target.onHTMLParseError(last_error.slice());
                }
            }

            const DoneCallback = struct {
                static fn done(_unused: *T) {}
            };

            let rewriter = builder.build(
                .UTF8,
                memory_settings,
                false,
                T,
                target,
                T.onWriteHTML,
                DoneCallback.done,
            )?;
            const rewriter_guard = PtrDeinitGuard<@TypeOf(rewriter)>{ ptr: rewriter };
            * = rewriter_guard;

            rewriter.write(input)?;
            rewriter.end()?;
        }
    };
}

const processor = HTMLProcessor(HTMLScanner, false);

@root struct HTMLScanner {
    allocator: std.mem.Allocator,
    import_records: ImportRecord.List = {},
    log: *logger.Log,
    source: *const logger.Source,

    pub fn init(allocator: std.mem.Allocator, log: *logger.Log, source: *const logger.Source): HTMLScanner {
        return {
            allocator: allocator,
            import_records: {},
            log: log,
            source: source,
        };
    }

    pub fn deinit(*_) {
        for (*record of _.import_records.slice()) {
            _.allocator.free(record.path.text);
        }
        _.import_records.deinit(_.allocator);
    }

    fn createImportRecord(*_, input_path: []const u8, kind: ImportKind)! {
        // In HTML, sometimes people do /src/index.js
        // In that case, we don't want to use the absolute filesystem path, we want to use the path relative to the project root
        const path_to_use = if (input_path.len > 1 && input_path[0] == '/')
            bun.path.joinAbsString(bun.fs.FileSystem.instance.top_level_dir, &[input_path[1..]], .auto)

            // Check if imports to (e.g) "App.tsx" are actually relative imoprts w/o the "./"
        else if (input_path.len > 2 && input_path[0] != '.' && input_path[1] != '/') blk: {
            if (dot_idx := std.mem.lastIndexOfScalar(u8, input_path, '.')) {
                const ext = input_path[dot_idx..];
                if (ext.len <= 4) {
                    if (dirname := std.fs.path.dirname(_.source.path.text)) {
                        const resolved = bun.path.joinAbsString(dirname, &[input_path], .auto);
                        if (bun.sys.exists(resolved)) {
                            break :blk resolved;
                        }
                    }
                }
            }
            break :blk input_path;
        } else input_path;

        const record = ImportRecord{
            path: fs.Path.init(_.allocator.dupeZ(u8, path_to_use)?),
            kind: kind,
            range: logger.Range.None,
        };

        _.import_records.append(_.allocator, record)?;
    }

    pub fn onWriteHTML(*_, bytes: []const u8) {
        * = _.log;
        * = bytes; // bytes are not written in scan phase
    }

    pub fn onHTMLParseError(*_, message: []const u8) {
        _.log.addError(
            _.source,
            logger.Loc.Empty,
            message,
        ) catch |e| bun.handleOom(e);
    }

    pub fn onTag(*_, element: *lol.Element, path: []const u8, url_attribute: []const u8, kind: ImportKind) {
        * = element;
        * = url_attribute;
        _.createImportRecord(path, kind) catch {};
    }

    pub fn scan(*_, input: []const u8)! {
        processor.run(_, input)?;
    }
}
