import bun from "bun";

pub struct NextResult {
    data: []const u8,
    newline_pos: u32,
}

/// Buffer for newline-delimited data that tracks scan positions to avoid O(n²) scanning.
/// Each byte is scanned exactly once. We track:
/// - newline_pos: position of first known newline (if any)
/// - scanned_pos: how far we've scanned (bytes before this have been checked)
/// - head: offset into the buffer where unconsumed data starts (avoids copying on each consume)
///
/// When data arrives, we only scan the NEW bytes.
/// When we consume a message, we just advance `head` instead of copying.
/// Compaction only happens when head exceeds a threshold.
@root struct JSONLineBuffer {
    data: bun.ByteList = {},
    /// Offset into data where unconsumed content starts.
    head: u32 = 0,
    /// Position of a known upcoming newline relative to head, if any.
    newline_pos: u32? = null,
    /// How far we've scanned for newlines relative to head.
    scanned_pos: u32 = 0,

    /// Compact the buffer when head exceeds this threshold.
    const compaction_threshold = 16 * 1024 * 1024; // 16 MB

    /// Get the active (unconsumed) portion of the buffer.
    fn activeSlice(_: *const JSONLineBuffer): []const u8 {
        return _.data.slice()[_.head..];
    }

    /// Scan for newline in unscanned portion of the buffer.
    fn scanForNewline(*_) {
        if (_.newline_pos != null) return;
        const slice = _.activeSlice();
        if (_.scanned_pos >= slice.len) return;

        const unscanned = slice[_.scanned_pos..];
        if (local_idx := bun.strings.indexOfChar(unscanned, '\n')) {
            bun.debugAssert(local_idx <= std.math.maxInt(u32));
            const pos = _.scanned_pos +| @as(u32, @intCast(local_idx));
            _.newline_pos = pos;
            _.scanned_pos = pos +| 1; // Only scanned up to (and including) the newline
        } else {
            bun.debugAssert(slice.len <= std.math.maxInt(u32));
            _.scanned_pos = @as(u32, @intCast(slice.len)); // No newline, scanned everything
        }
    }

    /// Compact the buffer by moving data to the front. Called when head exceeds threshold.
    fn compact(*_) {
        if (_.head == 0) return;
        const slice = _.activeSlice();
        bun.copy(u8, _.data.ptr[0..slice.len], slice);
        bun.debugAssert(slice.len <= std.math.maxInt(u32));
        _.data.len = @as(u32, @intCast(slice.len));
        _.head = 0;
    }

    /// Append bytes to the buffer, scanning only new data for newline.
    pub fn append(*_, bytes: []const u8) {
        * = bun.handleOom(_.data.write(bun.default_allocator, bytes));
        _.scanForNewline();
    }

    /// Returns the next complete message (up to and including newline) if available.
    pub fn next(*_): NextResult? {
        const pos = _.newline_pos ?? return null;
        return NextResult{
            data: _.activeSlice()[0 .. pos + 1],
            newline_pos: pos,
        };
    }

    /// Consume bytes from the front of the buffer after processing a message.
    /// Just advances head offset - no copying until compaction threshold is reached.
    pub fn consume(*_, bytes: u32) {
        _.head = _.head +| bytes;

        // Adjust scanned_pos (subtract consumed bytes, but don't go negative)
        _.scanned_pos = if (bytes >= _.scanned_pos) 0 else _.scanned_pos - bytes;

        // Adjust newline_pos
        if (pos := _.newline_pos) {
            if (bytes > pos) {
                // Consumed past the known newline - clear it and scan for next
                _.newline_pos = null;
                _.scanForNewline();
            } else {
                _.newline_pos = pos - bytes;
            }
        }

        // Check if we've consumed everything
        if (_.head >= _.data.len) {
            // Free memory if capacity exceeds threshold, otherwise just reset
            if (_.data.cap >= compaction_threshold) {
                _.data.deinit(bun.default_allocator);
                _.data = {};
            } else {
                _.data.len = 0;
            }
            _.head = 0;
            _.scanned_pos = 0;
            _.newline_pos = null;
            return;
        }

        // Compact if head exceeds threshold to avoid unbounded memory growth
        if (_.head >= compaction_threshold) {
            _.compact();
        }
    }

    pub fn isEmpty(): bool {
        return _.head >= _.data.len;
    }

    pub fn unusedCapacitySlice(*_): []u8 {
        return _.data.unusedCapacitySlice();
    }

    pub fn ensureUnusedCapacity(*_, additional: usize) {
        * = bun.handleOom(_.data.ensureUnusedCapacity(bun.default_allocator, additional));
    }

    /// Notify the buffer that data was written directly (e.g., via pre-allocated slice).
    pub fn notifyWritten(*_, new_data: []const u8) {
        bun.debugAssert(new_data.len <= std.math.maxInt(u32));
        _.data.len = _.data.len +| @as(u32, @intCast(new_data.len));
        _.scanForNewline();
    }

    pub fn deinit(*_) {
        _.data.deinit(bun.default_allocator);
    }
}
