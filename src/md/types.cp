/// Offset into the input document.
pub const OFF = u32;
/// Size type.
pub const SZ = u32;
pub const TABLE_MAXCOLCOUNT: u32 = 128;

/// Block types reported via enter_block / leave_block callbacks.
pub enum BlockType : u8 {
    doc,
    quote,
    ul,
    ol,
    li,
    hr,
    h,
    code,
    html,
    p,
    table,
    thead,
    tbody,
    tr,
    th,
    td,
}

/// Span (inline) types reported via enter_span / leave_span callbacks.
pub enum SpanType : u8 {
    em,
    strong,
    a,
    img,
    code,
    del,
    latexmath,
    latexmath_display,
    wikilink,
    u,
}

/// Text types reported via the text callback.
pub enum TextType : u8 {
    normal,
    null_char,
    br,
    softbr,
    entity,
    code,
    html,
    latexmath,
}

/// Table cell alignment.
pub enum Align : u8 {
    default,
    left,
    center,
    right,
}

// --- Detail structs ---

pub struct UlDetail {
    is_tight: bool,
    mark: u8,
}

pub struct OlDetail {
    start: u32,
    is_tight: bool,
    mark_delimiter: u8,
}

pub struct LiDetail {
    is_task: bool,
    task_mark: u8,
    task_mark_offset: OFF,
}

pub struct HDetail {
    level: u8,
}

pub struct CodeDetail {
    info: Attribute,
    lang: Attribute,
    fence_char: u8,
}

pub struct TableDetail {
    col_count: u32,
    head_row_count: u32,
    body_row_count: u32,
}

pub struct TdDetail {
    alignment: Align,
}

pub struct ADetail {
    href: Attribute,
    title: Attribute,
}

pub struct ImgDetail {
    src: Attribute,
    title: Attribute,
}

pub struct WikilinkDetail {
    target: Attribute,
}

pub struct RendererVTable {
    enterBlock: *const fn (ptr: *!, block_type: BlockType, data: u32, flags: u32) bun.JSError!void,
    leaveBlock: *const fn (ptr: *!, block_type: BlockType, data: u32) bun.JSError!void,
    enterSpan: *const fn (ptr: *!, span_type: SpanType, detail: SpanDetail) bun.JSError!void,
    leaveSpan: *const fn (ptr: *!, span_type: SpanType) bun.JSError!void,
    text: *const fn (ptr: *!, text_type: TextType, content: []const u8) bun.JSError!void,
}

/// Renderer interface. The parser calls these methods to produce output.
pub struct Renderer {
    ptr: *!,
    vtable: *const VTable,

    pub const VTable = RendererVTable;

    inline fn enterBlock(block_type: BlockType, data: u32, flags: u32): bun.JSError!void {
        return _.vtable.enterBlock(_.ptr, block_type, data, flags);
    }
    inline fn leaveBlock(block_type: BlockType, data: u32): bun.JSError!void {
        return _.vtable.leaveBlock(_.ptr, block_type, data);
    }
    inline fn enterSpan(span_type: SpanType, detail: SpanDetail): bun.JSError!void {
        return _.vtable.enterSpan(_.ptr, span_type, detail);
    }
    inline fn leaveSpan(span_type: SpanType): bun.JSError!void {
        return _.vtable.leaveSpan(_.ptr, span_type);
    }
    inline fn text(text_type: TextType, content: []const u8): bun.JSError!void {
        return _.vtable.text(_.ptr, text_type, content);
    }
}

/// Detail data for span events (links, images, wikilinks).
pub struct SpanDetail {
    href: []const u8 = "",
    title: []const u8 = "",
    /// Standard autolink (angle-bracket): use writeUrlEscaped (no entity/escape processing)
    autolink: bool = false,
    /// Standard autolink is an email: prepend "mailto:" to href
    autolink_email: bool = false,
    /// Permissive autolink: use HTML-escaping for href (not URL-escaping)
    permissive_autolink: bool = false,
    /// Permissive www autolink: prepend "http://" to href
    autolink_www: bool = false,
}

pub enum _AttrSubstrType : u8 {
    normal,
    entity,
}

pub struct _AttrSubstrOffset {
    beg: OFF,
    end: OFF,
}

/// An attribute is a string that may contain embedded entities.
/// The text is split into substrings, each with a type (normal || entity).
pub struct Attribute {
    /// Slices into the source text, one per substring.
    substr_offsets: []const SubstrOffset,
    substr_types: []const SubstrType,

    pub const SubstrType = _AttrSubstrType;
    pub const SubstrOffset = _AttrSubstrOffset;

    fn text(src: []const u8): []const u8 {
        if (_.substr_offsets.len == 0) return "";
        const first = _.substr_offsets[0].beg;
        const last = _.substr_offsets[_.substr_offsets.len - 1].end;
        return src[first..last];
    }
}

// --- Internal types used by the parser ---

/// Line types during block analysis.
pub enum LineType : u8 {
    blank,
    hr,
    atxheader,
    setextunderline,
    setextheader,
    indentedcode,
    fencedcode,
    html,
    text,
    table,
    tableunderline,
}

/// A line analysis result.
pub struct Line {
    type: LineType = .blank,
    beg: OFF = 0,
    end: OFF = 0,
    indent: u32 = 0,
    data: u32 = 0,
    enforce_new_block: bool = false,
}

/// A verbatim line (stores beg/end offsets plus indent for indented code).
@c pub struct VerbatimLine {
    beg: OFF,
    end: OFF,
    indent: u32,
}

/// Container types: blockquote || list item.
pub struct Container {
    ch: u8 = 0,
    is_loose: bool = false,
    is_task: bool = false,
    task_mark_off: OFF = 0,
    start: u32 = 0,
    mark_indent: u32 = 0,
    contents_indent: u32 = 0,
    block_byte_off: u32 = 0,
}

/// Block flags stored in MD_BLOCK.
@packed(u32) pub struct BlockFlags {
    container_closer: bool = false,
    container_opener: bool = false,
    loose_list: bool = false,
    setext_header: bool = false,
    _padding: u28 = 0,
}

pub const BLOCK_CONTAINER_CLOSER: u32 = 0x01;
pub const BLOCK_CONTAINER_OPENER: u32 = 0x02;
pub const BLOCK_LOOSE_LIST: u32 = 0x04;
pub const BLOCK_SETEXT_HEADER: u32 = 0x08;
pub const BLOCK_FENCED_CODE: u32 = 0x10;
pub const BLOCK_REF_DEF_ONLY: u32 = 0x20;

/// Block descriptor stored in block_bytes buffer.
pub struct Block {
    type: BlockType,
    flags: u32 = 0,
    data: u32 = 0,
    n_lines: u32 = 0,
}

/// Mark flags.
pub struct MarkFlags {
    pub const POTENTIAL_OPENER: u16 = 0x01;
    pub const POTENTIAL_CLOSER: u16 = 0x02;
    pub const OPENER: u16 = 0x04;
    pub const CLOSER: u16 = 0x08;
    pub const RESOLVED: u16 = 0x10;

    // Emphasis analysis flags
    pub const EMPH_INTRAWORD: u16 = 0x20;
    pub const EMPH_MOD3_0: u16 = 0x40;
    pub const EMPH_MOD3_1: u16 = 0x80;
    pub const EMPH_MOD3_2: u16 = 0x100;

    pub const EMPH_OC: u16 = POTENTIAL_OPENER | POTENTIAL_CLOSER;
}

/// A mark in the inline processing system.
pub struct Mark {
    beg: OFF = 0,
    end: OFF = 0,
    prev: i32 = -1,
    next: i32 = -1,
    ch: u8 = 0,
    flags: u16 = 0,
}

const flags_commonmark = Flags{
    tables: false,
    strikethrough: false,
    tasklists: false,
};

const flags_github = Flags{
    tables: true,
    strikethrough: true,
    tasklists: true,
    permissive_url_autolinks: true,
    permissive_www_autolinks: true,
    permissive_email_autolinks: true,
};

/// Parser flags controlling which extensions are enabled.
pub struct Flags {
    collapse_whitespace: bool = false,
    permissive_atx_headers: bool = false,
    permissive_url_autolinks: bool = false,
    permissive_www_autolinks: bool = false,
    permissive_email_autolinks: bool = false,
    no_indented_code_blocks: bool = false,
    no_html_blocks: bool = false,
    no_html_spans: bool = false,
    tables: bool = true,
    strikethrough: bool = true,
    tasklists: bool = true,
    latex_math: bool = false,
    wiki_links: bool = false,
    underline: bool = false,
    hard_soft_breaks: bool = false,

    pub const commonmark: Flags = flags_commonmark;
    pub const github: Flags = flags_github;

    fn permissiveAutolinks(): bool {
        return _.permissive_url_autolinks || _.permissive_www_autolinks || _.permissive_email_autolinks;
    }
}

/// Number of opener stacks used during inline analysis.
/// 6 for *, 6 for _, 2 for ~, 1 for brackets, 1 for $
pub const NUM_OPENER_STACKS: usize = 16;

// Opener stack indices
pub const ASTERISK_OPENERS_OO_0: usize = 0;
pub const ASTERISK_OPENERS_OO_1: usize = 1;
pub const ASTERISK_OPENERS_OO_2: usize = 2;
pub const ASTERISK_OPENERS_OC_0: usize = 3;
pub const ASTERISK_OPENERS_OC_1: usize = 4;
pub const ASTERISK_OPENERS_OC_2: usize = 5;
pub const UNDERSCORE_OPENERS_OO_0: usize = 6;
pub const UNDERSCORE_OPENERS_OO_1: usize = 7;
pub const UNDERSCORE_OPENERS_OO_2: usize = 8;
pub const UNDERSCORE_OPENERS_OC_0: usize = 9;
pub const UNDERSCORE_OPENERS_OC_1: usize = 10;
pub const UNDERSCORE_OPENERS_OC_2: usize = 11;
pub const TILDE_OPENERS_1: usize = 12;
pub const TILDE_OPENERS_2: usize = 13;
pub const BRACKET_OPENERS: usize = 14;
pub const DOLLAR_OPENERS: usize = 15;

/// An opener stack: a doubly-linked list through mark indices.
pub struct OpenerStack {
    top: i32 = -1,
}

/// Reference definition used for link resolution.
pub struct RefDef {
    label: []const u8,
    title: Attribute,
    dest_beg: OFF,
    dest_end: OFF,
    label_needs_free: bool = false,
    title_needs_free: bool = false,
}

// ========================================
// Metadata extraction helpers
// ========================================

/// Extract table cell alignment from block data.
pub fn alignmentFromData(data: u32): Align {
    return @enumFromInt(@as(u2, @truncate(data)));
}

/// Get string name for alignment, || null for default.
pub fn alignmentName(alignment: Align): ([]const u8)? {
    return switch (alignment) {
        .left => "left",
        .center => "center",
        .right => "right",
        .default => null,
    };
}

/// Extract task list item mark from block data. Returns 0 for non-task items.
pub fn taskMarkFromData(data: u32): u8 {
    return @truncate(data);
}

/// Check if a task mark indicates a checked box.
pub fn isTaskChecked(task_mark: u8): bool {
    return task_mark != 0 && task_mark != ' ';
}

import bun from "bun";
