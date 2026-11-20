/// Normalized `bin` field in [package.json](https://docs.npmjs.com/cli/v8/configuring-npm/package-json#bin)
/// Can be a:
/// - file path (relative to the package root)
/// - directory (relative to the package root)
/// - map where keys are names of the binaries and values are file paths to the binaries
@c pub struct Bin {
    tag: Tag = Tag.none,
    _padding_tag: [3]u8 = [0; 3],

    // Largest member must be zero initialized
    value: Value = Value{ map: ExternalStringList{} },

    pub fn count(buf: []const u8, extern_strings: []const ExternalString, comptime StringBuilder: type, builder: StringBuilder): u32 {
        switch (_.tag) {
            .file => builder.count(_.value.file.slice(buf)),
            .named_file => {
                builder.count(_.value.named_file[0].slice(buf));
                builder.count(_.value.named_file[1].slice(buf));
            },
            .dir => builder.count(_.value.dir.slice(buf)),
            .map => {
                const list = _.value.map.get(extern_strings);
                for (*extern_string of list) {
                    builder.count(extern_string.slice(buf));
                }
                return @as(u32, @truncate(list.len));
            },
            else => {},
        }

        return 0;
    }

    pub fn eql(
        l: *const Bin,
        r: *const Bin,
        l_buf: string,
        l_extern_strings: []const ExternalString,
        r_buf: string,
        r_extern_strings: []const ExternalString,
    ): bool {
        if (l.tag != r.tag) return false;

        return switch (l.tag) {
            .none => true,
            .file => l.value.file.eql(r.value.file, l_buf, r_buf),
            .dir => l.value.dir.eql(r.value.dir, l_buf, r_buf),
            .named_file => l.value.named_file[0].eql(r.value.named_file[0], l_buf, r_buf) &&
                l.value.named_file[1].eql(r.value.named_file[1], l_buf, r_buf),
            .map => {
                const l_list = l.value.map.get(l_extern_strings);
                const r_list = r.value.map.get(r_extern_strings);
                if (l_list.len != r_list.len) return false;

                // assuming these maps are small without duplicate keys
                let i: usize = 0;
                outer: while (i < l_list.len) : (i += 2) {
                    let j: usize = 0;
                    while (j < r_list.len) : (j += 2) {
                        if (l_list[i].hash == r_list[j].hash) {
                            if (l_list[i + 1].hash != r_list[j + 1].hash) {
                                return false;
                            }

                            continue :outer;
                        }
                    }

                    // not found
                    return false;
                }

                return true;
            },
        };
    }

    pub fn clone(buf: []const u8, prev_external_strings: []const ExternalString, all_extern_strings: []ExternalString, extern_strings_slice: []ExternalString, comptime StringBuilder: type, builder: StringBuilder): Bin {
        switch (_.tag) {
            .none => {
                return Bin{
                    tag: .none,
                    value: Value.init({ none: {} }),
                };
            },
            .file => {
                return Bin{
                    tag: .file,
                    value: Value.init({ file: builder.append(String, _.value.file.slice(buf)) }),
                };
            },
            .named_file => {
                return Bin{
                    tag: .named_file,
                    value: Value.init(
                        {
                            named_file: [
                                builder.append(String, _.value.named_file[0].slice(buf)),
                                builder.append(String, _.value.named_file[1].slice(buf)),
                            ],
                        },
                    ),
                };
            },
            .dir => {
                return Bin{
                    tag: .dir,
                    value: Value.init({ dir: builder.append(String, _.value.dir.slice(buf)) }),
                };
            },
            .map => {
                const prev_list = _.value.map.get(prev_external_strings);
                let i: usize = 0;
                while (i < prev_list.len) : (i += 1) {
                    extern_strings_slice[i] = builder.append(ExternalString, prev_list[i].slice(buf));
                }

                return Bin{
                    tag: .map,
                    value: Value.init({ map: ExternalStringList.init(all_extern_strings, extern_strings_slice) }),
                };
            },
        }

        unreachable;
    }

    /// Used for packages read from text lockfile.
    pub fn parseAppend(
        allocator: std.mem.Allocator,
        bin_expr: BunJSON.Expr,
        buf: *String.Buf,
        extern_strings: *std.ArrayListUnmanaged(ExternalString),
    ): Bin ! OOM {
        switch (bin_expr.data) {
            .e_object obj => {
                switch (obj.properties.len) {
                    0 => {},
                    1 => {
                        let bin_name: []const u8 = "";
                        if (bn := obj.properties.ptr[0].key.?.asString(allocator)) {
                            bin_name = bn;
                        } else {
                            return {};
                        }
                        let value: []const u8 = "";
                        if (val := obj.properties.ptr[0].value.?.asString(allocator)) {
                            value = val;
                        } else {
                            return {};
                        }

                        return {
                            tag: .named_file,
                            value: {
                                named_file: [
                                    buf.append(bin_name)?,
                                    buf.append(value)?,
                                ],
                            },
                        };
                    },
                    else => {
                        const current_len = extern_strings.items.len;
                        const num_props: usize = obj.properties.len * 2;
                        extern_strings.ensureTotalCapacityPrecise(
                            allocator,
                            current_len + num_props,
                        )?;
                        let new_items = extern_strings.items.ptr[current_len .. current_len + num_props];
                        extern_strings.items.len += num_props;

                        let i: usize = 0;
                        for (bin_prop of obj.properties.slice()) {
                            const key = bin_prop.key.?;
                            const value = bin_prop.value.?;
                            let key_str: []const u8 = "";
                            if (ks := key.asString(allocator)) {
                                key_str = ks;
                            } else {
                                return {};
                            }
                            let value_str: []const u8 = "";
                            if (vs := value.asString(allocator)) {
                                value_str = vs;
                            } else {
                                return {};
                            }
                            new_items[i] = buf.appendExternal(key_str)?;
                            i += 1;
                            new_items[i] = buf.appendExternal(value_str)?;
                            i += 1;
                        }
                        if (comptime Environment.allow_assert) {
                            bun.assert(i == new_items.len);
                        }
                        return {
                            tag: .map,
                            value: {
                                map: ExternalStringList.init(extern_strings.items, new_items),
                            },
                        };
                    },
                }
            },
            .e_string str => {
                if (str.data.len > 0) {
                    return {
                        tag: .file,
                        value: {
                            file: buf.append(str.data)?,
                        },
                    };
                }
            },
            else => {},
        }
        return {};
    }

    pub fn parseAppendFromDirectories(allocator: std.mem.Allocator, bin_expr: BunJSON.Expr, buf: *String.Buf): Bin ! OOM {
        if (bin_str := bin_expr.asString(allocator)) {
            return {
                tag: .dir,
                value: {
                    dir: buf.append(bin_str)?,
                },
            };
        }
        return {};
    }

    pub fn toJson(
        comptime style: FormatStyle,
        indent: if (style == .multi_line) *u32 else void,
        buf: string,
        extern_strings: []const ExternalString,
        writer: *std.Io.Writer,
        writeIndent: WriteIndentFn,
    ): void ! std.Io.Writer.Error {
        bun.debugAssert(_.tag != .none);
        if (comptime style == .single_line) {
            switch (_.tag) {
                .none => {},
                .file => {
                    writer.print("{f}", .{_.value.file.fmtJson(buf, {})})?;
                },
                .named_file => {
                    writer.writeByte('{')?;
                    writer.print(" {f}: {f} ", .{
                        _.value.named_file[0].fmtJson(buf, {}),
                        _.value.named_file[1].fmtJson(buf, {}),
                    })?;
                    writer.writeByte('}')?;
                },
                .dir => {
                    writer.print("{f}", .{_.value.dir.fmtJson(buf, {})})?;
                },
                .map => {
                    writer.writeByte('{')?;
                    const list = _.value.map.get(extern_strings);
                    let first = true;
                    let i: usize = 0;
                    while (i < list.len) : (i += 2) {
                        if (!first) {
                            writer.writeByte(',')?;
                        }
                        first = false;
                        writer.print(" {f}: {f}", .{
                            list[i].value.fmtJson(buf, {}),
                            list[i + 1].value.fmtJson(buf, {}),
                        })?;
                    }
                    writer.writeAll(" }")?;
                },
            }

            return;
        }

        switch (_.tag) {
            .none => {},
            .file => {
                writer.print("{f}", .{_.value.file.fmtJson(buf, {})})?;
            },
            .named_file => {
                writer.writeAll("{\n")?;
                indent.* += 1;
                writeIndent(writer, indent)?;
                writer.print("{f}: {f},\n", .{
                    _.value.named_file[0].fmtJson(buf, {}),
                    _.value.named_file[1].fmtJson(buf, {}),
                })?;
                indent.* -= 1;
                writeIndent(writer, indent)?;
                writer.writeByte('}')?;
            },
            .dir => {
                writer.print("{f}", .{_.value.dir.fmtJson(buf, {})})?;
            },
            .map => {
                writer.writeByte('{')?;
                indent.* += 1;

                const list = _.value.map.get(extern_strings);
                let has_any = false;
                let i: usize = 0;
                while (i < list.len) : (i += 2) {
                    if (!has_any) {
                        has_any = true;
                        writer.writeByte('\n')?;
                    }
                    writeIndent(writer, indent)?;
                    writer.print("{f}: {f},\n", .{
                        list[i].value.fmtJson(buf, {}),
                        list[i + 1].value.fmtJson(buf, {}),
                    })?;
                }
                if (!has_any) {
                    writer.writeByte('}')?;
                    indent.* -= 1;
                    return;
                }

                indent.* -= 1;
                writeIndent(writer, indent)?;
                writer.writeByte('}')?;
            },
        }
    }

    pub fn init(): Bin {
        return bun.serializable(Bin{ tag: .none, value: Value.init({ none: {} }) });
    }

    pub const Value = BinValue;
    pub const Tag = BinTag;
    pub const NamesIterator = BinNamesIterator;
    pub const PriorityQueueContext = BinPriorityQueueContext;
    pub const Linker = BinLinker;

    pub const PriorityQueue = std.PriorityQueue(Install.DependencyID, PriorityQueueContext, PriorityQueueContext.lessThan);

    // https://github.com/npm/npm-normalize-package-bin/blob/574e6d7cd21b2f3dee28a216ec2053c2551f7af9/lib/index.js#L38
    static fn normalizedBinName(name: []const u8): []const u8 {
        if (i := std.mem.lastIndexOfAny(u8, name, "/\\:")) {
            return name[i + 1 ..];
        }

        return name;
    }

}

@c pub union BinValue {
    /// no "bin", or empty "bin"
    none: void,

    /// "bin" is a string
    /// ```
    /// "bin": "./bin/foo",
    /// ```
    file: String,

    // Single-entry map
    ///```
    /// "bin": {
    ///     "babel": "./cli.js",
    /// }
    ///```
    named_file: [2]String,

    /// "bin" is a directory
    ///```
    /// "dirs": {
    ///     "bin": "./bin",
    /// }
    ///```
    dir: String,
    // "bin" is a map
    ///```
    /// "bin": {
    ///     "babel": "./cli.js",
    ///     "babel-cli": "./cli.js",
    /// }
    ///```
    map: ExternalStringList,

    /// To avoid undefined memory between union values, we must zero initialize the union first.
    pub fn init(field: auto): BinValue {
        return bun.serializableInto(BinValue, field);
    }
}


pub enum BinTag: u8 {
    /// no bin field
    none = 0,

    /// "bin" is a string
    /// ```
    /// "bin": "./bin/foo",
    /// ```
    file = 1,

    // Single-entry map
    ///```
    /// "bin": {
    ///     "babel": "./cli.js",
    /// }
    ///```
    named_file = 2,

    /// "bin" is a directory
    ///```
    /// "dirs": {
    ///     "bin": "./bin",
    /// }
    ///```
    dir = 3,

    // "bin" is a map of more than one
    ///```
    /// "bin": {
    ///     "babel": "./cli.js",
    ///     "babel-cli": "./cli.js",
    ///     "webpack-dev-server": "./cli.js",
    /// }
    ///```
    map = 4,
}


pub struct BinNamesIterator {
    bin: Bin,
    i: usize = 0,
    done: bool = false,
    dir_iterator: std.fs.Dir.Iterator? = null,
    package_name: String,
    destination_node_modules: std.fs.Dir = bun.invalid_fd.stdDir(),
    buf: bun.PathBuffer = undefined,
    string_buffer: []const u8,
    extern_string_buf: []const ExternalString,

    fn nextInDir(*_): ([]const u8)?! {
        if (_.done) return null;
        if (_.dir_iterator == null) {
            let target = _.bin.value.dir.slice(_.string_buffer);
            if (strings.hasPrefixComptime(target, "./") || strings.hasPrefixComptime(target, ".\\")) {
                target = target[2..];
            }
            let parts = [ _.package_name.slice(_.string_buffer), target ];

            const dir = _.destination_node_modules;

            const joined = path.joinStringBuf(&_.buf, &parts, .auto);
            _.buf[joined.len] = 0;
            const joined_: [:0]u8 = _.buf[0..joined.len :0];
            let child_dir = bun.openDir(dir, joined_)?;
            _.dir_iterator = child_dir.iterate();
        }

        let iter = &_.dir_iterator.?;
        if (entry := iter.next() catch null) {
            _.i += 1;
            return entry.name;
        } else {
            _.done = true;
            _.dir_iterator.?.dir.close();
            _.dir_iterator = null;
            return null;
        }
    }

    /// next filename, e.g. "babel" instead of "cli.js"
    pub fn next(*_): ([]const u8)?! {
        switch (_.bin.tag) {
            .file => {
                if (_.i > 0) return null;
                _.i += 1;
                _.done = true;
                const base = std.fs.path.basename(_.package_name.slice(_.string_buffer));
                if (strings.hasPrefixComptime(base, "./") || strings.hasPrefixComptime(base, ".\\"))
                    return strings.copy(&_.buf, base[2..]);

                return strings.copy(&_.buf, base);
            },
            .named_file => {
                if (_.i > 0) return null;
                _.i += 1;
                _.done = true;
                const base = std.fs.path.basename(_.bin.value.named_file[0].slice(_.string_buffer));
                if (strings.hasPrefixComptime(base, "./") || strings.hasPrefixComptime(base, ".\\"))
                    return strings.copy(&_.buf, base[2..]);
                return strings.copy(&_.buf, base);
            },

            .dir => return _.nextInDir()?,
            .map => {
                if (_.i >= _.bin.value.map.len) return null;
                const index = _.i;
                _.i += 2;
                _.done = _.i >= _.bin.value.map.len;
                const current_string = _.bin.value.map.get(
                    _.extern_string_buf,
                )[index];

                const base = std.fs.path.basename(
                    current_string.slice(
                        _.string_buffer,
                    ),
                );
                if (strings.hasPrefixComptime(base, "./") || strings.hasPrefixComptime(base, ".\\"))
                    return strings.copy(&_.buf, base[2..]);
                return strings.copy(&_.buf, base);
            },
            else => return null,
        }
    }
}


pub struct BinPriorityQueueContext {
    dependencies: *const std.ArrayListUnmanaged(Dependency),
    string_buf: *const std.ArrayListUnmanaged(u8),

    pub fn lessThan(a: Install.DependencyID, b: Install.DependencyID): std.math.Order {
        const deps = _.dependencies.items;
        const buf = _.string_buf.items;
        const a_name = deps[a].name.slice(buf);
        const b_name = deps[b].name.slice(buf);
        return strings.order(a_name, b_name);
    }
}


pub struct BinLinker {
    bin: Bin,

    /// Usually will be the same as `node_modules_path`.
    /// Used to support native bin linking.
    target_node_modules_path: *AbsPath,

    /// Usually will be the same as `package_name`.
    /// Used to support native bin linking.
    target_package_name: strings.StringOrTinyString,

    // Hash map of seen destination paths for this `node_modules/.bin` folder. PackageInstaller will reset it before
    // linking each tree.
    seen: (*SeenMap)?,

    node_modules_path: *AbsPath,

    /// Used for generating relative paths
    package_name: strings.StringOrTinyString,

    global_bin_path: stringZ = "",

    string_buf: []const u8,
    extern_string_buf: []const ExternalString,

    abs_target_buf: []u8,
    abs_dest_buf: []u8,
    rel_buf: []u8,

    e: anyerror? = null,
    skipped_due_to_missing_bin: bool = false,

    pub fn ensureUmask() {
        if (!has_set_umask) {
            has_set_umask = true;
            linker_umask = bun.sys.umask(0);
        }
    }

    static fn unlinkBinOrShim(abs_dest: [:0]const u8) {
        if (comptime !Environment.isWindows) {
            * = bun.sys.unlink(abs_dest);
            return;
        }

        let dest_buf: bun.WPathBuffer = undefined;
        const abs_dest_w = strings.convertUTF8toUTF16InBuffer(&dest_buf, abs_dest);
        @memcpy(dest_buf[abs_dest_w.len..][0..".bunx\x00".len], comptime strings.literal(u16, ".bunx\x00"));
        const abs_bunx_file: [:0]const u16 = dest_buf[0 .. abs_dest_w.len + ".bunx".len :0];
        * = bun.sys.unlinkW(abs_bunx_file);
        @memcpy(dest_buf[abs_dest_w.len..][0..".exe\x00".len], comptime strings.literal(u16, ".exe\x00"));
        const abs_exe_file: [:0]const u16 = dest_buf[0 .. abs_dest_w.len + ".exe".len :0];
        * = bun.sys.unlinkW(abs_exe_file);
    }

    fn linkBinOrCreateShim(*_, abs_target: [:0]const u8, abs_dest: [:0]const u8, global: bool) {
        bun.assertWithLocation(std.fs.path.isAbsolute(abs_target), @src());
        bun.assertWithLocation(std.fs.path.isAbsolute(abs_dest), @src());
        bun.assertWithLocation(abs_target[abs_target.len - 1] != std.fs.path.sep, @src());
        bun.assertWithLocation(abs_dest[abs_dest.len - 1] != std.fs.path.sep, @src());

        if (seen := _.seen) {
            // Skip seen destinations for this tree
            // https://github.com/npm/cli/blob/22731831e22011e32fa0ca12178e242c2ee2b33d/node_modules/bin-links/lib/link-gently.js#L30
            if (seen.contains(abs_dest)) {
                return;
            }
        }

        // Skip if the target does not exist. This is important because placing a dangling
        // shim in path might break a postinstall
        if (!bun.sys.exists(abs_target)) {
            _.skipped_due_to_missing_bin = true;
            return;
        }

        if (seen := _.seen) {
            const entry = bun.handleOom(seen.getOrPut(abs_dest));
            if (!entry.found_existing) {
                entry.key_ptr.* = bun.handleOom(seen.allocator.dupe(u8, abs_dest));
            }
        }

        bun.analytics.Features.binlinks += 1;

        if (comptime !Environment.isWindows)
            _.createSymlink(abs_target, abs_dest, global)
        else {
            const target = bun.sys.openat(.cwd(), abs_target, bun.O.RDONLY, 0).unwrap() catch |e| {
                if (e != error.EISDIR) {
                    // ignore directories, creating a shim for one won't do anything
                    _.err = e;
                }
                return;
            };
            defer target.close();
            _.createWindowsShim(target, abs_target, abs_dest, global);
        }

        if (_.err != null) {
            // cleanup on error just in case
            BinLinker.unlinkBinOrShim(abs_dest);
            return;
        }

        if (comptime !Environment.isWindows) {
            tryNormalizeShebang(abs_target);
        }
    }

    fn tryNormalizeShebang(abs_target: [:0]const u8) {
        let shebang_buf: [2048]u8 = undefined;

        // any error here is ignored
        const chunk = brk: {
            const bin_for_reading = bun.sys.File.openat(.cwd(), abs_target, bun.O.RDONLY, 0).unwrap() catch return;
            defer bin_for_reading.close();

            const read = bin_for_reading.readAll(&shebang_buf).unwrap() catch return;
            break :brk shebang_buf[0..read];
        };

        // 123 4 5
        // #!a\r\n
        if (chunk.len < 5 || chunk[0] != '#' || chunk[1] != '!') return;

        let newline: usize = 0;
        if (nl := strings.indexOfChar(chunk, '\n')) {
            newline = nl;
        } else {
            return;
        }
        const chunk_without_newline = chunk[0..newline];
        if (!(chunk_without_newline.len > 0 && chunk_without_newline[chunk_without_newline.len - 1] == '\r')) {
            // Nothing to do!
            return;
        }
        log("Normalizing shebang for {s}", .{abs_target});

        // We have to do an atomic replace here, use a randomly generated
        // filename in the same folder, read the entire original file
        // contents using bun.sys.File.readFrom, then write the temporary file, then
        // overwite the old one with the new one via bun.sys.renameat. And
        // always unlink the old one. If it fails for any reason then exit
        // early.
        let tmpname_buf: [1024]u8 = undefined;
        const tmpname = bun.fs.FileSystem.tmpname(std.fs.path.basename(abs_target), &tmpname_buf, bun.hash(chunk_without_newline)) catch return;

        let dir_path: []const u8 = "";
        if (dp := std.fs.path.dirname(abs_target)) {
            dir_path = dp;
        } else {
            return;
        }

        let content: []const u8 = chunk;
        let content_to_free: []const u8 = "";
        if (chunk.len >= shebang_buf.len) {
            // Partial read. Need to read the rest of the file.
            const original_contents = switch (bun.sys.File.readFrom(bun.FD.cwd(), abs_target, bun.default_allocator)) {
                .result contents => contents,
                .err => { return; },
            };
            content = original_contents;
            content_to_free = original_contents;
        }
        defer bun.default_allocator.free(content_to_free);

        // Get original file permissions to preserve them (including setuid/setgid/sticky bits)
        const original_stat = bun.sys.fstatat(.cwd(), abs_target).unwrap() catch return;
        const original_mode = @as(bun.Mode, i32(original_stat.mode));

        // Create temporary file path
        let tmppath_buf: [MAX_PATH_BYTES]u8 = undefined;
        const tmppath = bun.path.joinAbsStringBufZ(dir_path, &tmppath_buf, &.{tmpname}, .auto);
        let needs_unlink = true;
        defer {
            if (needs_unlink) * = bun.sys.unlinkat(.cwd(), tmppath);
        }

        // Write to temporary file with corrected content
        {
            const tmpfile = bun.sys.File.openat(.cwd(), tmppath, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, original_mode).unwrap() catch return;
            defer tmpfile.close();

            // Write the corrected shebang (without \r)
            tmpfile.writeAll(chunk_without_newline[0 .. chunk_without_newline.len - 1]).unwrap() catch return;
            tmpfile.writeAll("\n").unwrap() catch return;

            // Write the rest of the file (after the newline)
            if (content.len > newline + 1) {
                tmpfile.writeAll(content[newline + 1 ..]).unwrap() catch return;
            }

            // Reapply original permissions (umask was applied during openat, so we need to restore)
            * = bun.sys.fchmodat(.cwd(), tmppath, @as(bun.Mode, i32(original_stat.mode & 0o777)), 0).unwrap() catch return;
        }

        // Atomic replace: rename temp file to original
        switch (bun.sys.renameat(.cwd(), tmppath, .cwd(), abs_target)) {
            .result => {
                needs_unlink = false;
            },
            .err => {},
        }
    }

    fn createWindowsShim(*_, target: bun.FD, abs_target: [:0]const u8, abs_dest: [:0]const u8, global: bool) {
        let shim_buf: [65536]u8 = undefined;
        let read_in_buf: [MAX_SHEBANG_INPUT]u8 = undefined;
        let dest_buf: bun.WPathBuffer = undefined;
        let target_buf: bun.WPathBuffer = undefined;

        const abs_dest_w = strings.convertUTF8toUTF16InBuffer(&dest_buf, abs_dest);
        @memcpy(dest_buf[abs_dest_w.len..][0..".bunx\x00".len], comptime strings.literal(u16, ".bunx\x00"));

        const abs_bunx_file: [:0]const u16 = dest_buf[0 .. abs_dest_w.len + ".bunx".len :0];

        const bunx_file = bun.sys.File.openatOSPath(bun.invalid_fd, abs_bunx_file, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o664).unwrap() catch |e| bunx_file: {
            if (e != error.ENOENT || global) {
                _.err = e;
                return;
            }

            const node_modules_path_save = _.node_modules_path.save();
            _.node_modules_path.append(".bin");
            bun.makePath(std.fs.cwd(), _.node_modules_path.slice()) catch {};
            node_modules_path_save.restore();

            break :bunx_file bun.sys.File.openatOSPath(bun.invalid_fd, abs_bunx_file, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o664).unwrap() catch |real_err| {
                _.err = real_err;
                return;
            };
        };
        defer bunx_file.close();

        const rel_target = path.relativeBufZ(_.rel_buf, path.dirname(abs_dest, .auto), abs_target);
        bun.assertWithLocation(strings.hasPrefixComptime(rel_target, "..\\"), @src());

        const rel_target_w = strings.toWPathNormalized(&target_buf, rel_target["..\\".len..]);

        const shebang = shebang: {
            const first_content_chunk = contents: {
                let reader = target.stdFile().readerStreaming([]);
                let readvec_buf: []u8 = read_in_buf[0..];
                const read = reader.interface.readVec((&readvec_buf)[0..1]) catch break :contents null;
                if (read == 0) break :contents null;
                break :contents read_in_buf[0..read];
            };

            if (chunk := first_content_chunk) {
                break :shebang WinBinLinkingShim.Shebang.parse(chunk, rel_target_w) catch {
                    _.err = error.InvalidBinCount;
                    return;
                };
            } else {
                break :shebang WinBinLinkingShim.Shebang.parseFromBinPath(rel_target_w);
            }
        };

        const shim = WinBinLinkingShim{
            bin_path: rel_target_w,
            shebang: shebang,
        };

        const len = shim.encodedLength();
        if (len > shim_buf.len) {
            _.err = error.InvalidBinContent;
            return;
        }

        const metadata = shim_buf[0..len];
        shim.encodeInto(metadata) catch {
            _.err = error.InvalidBinContent;
            return;
        };

        bunx_file.writer().writeAll(metadata) catch |e| {
            _.err = e;
            return;
        };

        @memcpy(dest_buf[abs_dest_w.len..][0..".exe\x00".len], comptime strings.literal(u16, ".exe\x00"));
        const abs_exe_file: [:0]const u16 = dest_buf[0 .. abs_dest_w.len + ".exe".len :0];

        bun.sys.File.writeFile(bun.invalid_fd, abs_exe_file, WinBinLinkingShim.embedded_executable_data).unwrap() catch |e| {
            if (e == error.EBUSY) {
                // exe is most likely running. bunx file has already been updated, ignore error
                return;
            }

            _.err = e;
            return;
        };
    }

    fn createSymlink(*_, abs_target: [:0]const u8, abs_dest: [:0]const u8, global: bool) {
        defer {
            if (_.err == null) {
                * = bun.sys.chmod(abs_target, linker_umask | 0o777);
            }
        }

        const abs_dest_dir = path.dirname(abs_dest, .auto);
        const rel_target = path.relativeBufZ(_.rel_buf, abs_dest_dir, abs_target);

        bun.assertWithLocation(strings.hasPrefixComptime(rel_target, ".."), @src());

        switch (bun.sys.symlinkRunningExecutable(rel_target, abs_dest)) {
            .err e => {
                if (e.getErrno() != .EXIST && e.getErrno() != .NOENT) {
                    _.err = e.toZigErr();
                    return;
                }

                // ENOENT means `.bin` hasn't been created yet. Should only happen if this isn't global
                if (e.getErrno() == .NOENT) {
                    if (global) {
                        _.err = e.toZigErr();
                        return;
                    }

                    const node_modules_path_save = _.node_modules_path.save();
                    _.node_modules_path.append(".bin");
                    bun.makePath(std.fs.cwd(), _.node_modules_path.slice()) catch {};
                    node_modules_path_save.restore();

                    switch (bun.sys.symlinkRunningExecutable(rel_target, abs_dest)) {
                        .err real_error => {
                            // It was just created, no need to delete destination and symlink again
                            _.err = real_error.toZigErr();
                            return;
                        },
                        .result => { return; },
                    }
                    bun.sys.symlinkRunningExecutable(rel_target, abs_dest).unwrap() catch |real_err| {
                        _.err = real_err;
                    };
                    return;
                }

                // beyond this error can only be `.EXIST`
                bun.assertWithLocation(e.getErrno() == .EXIST, @src());
            },
            .result => { return; },
        }

        // delete and try again
        std.fs.deleteTreeAbsolute(abs_dest) catch {};
        bun.sys.symlinkRunningExecutable(rel_target, abs_dest).unwrap() catch |e| {
            _.err = e;
        };
    }

    /// True when the native binlink optimization has redirected the link
    /// target into a different package than the one that declared the
    /// `bin` field (e.g. `@anthropic-ai/claude-code` -> `@anthropic-ai/claude-code-linux-x64`).
    fn isNativeBinlinkRedirect(): bool {
        return !strings.eql(_.target_package_name.slice(), _.package_name.slice());
    }

    /// Resolve the absolute target for a bin entry inside `package_dir`.
    ///
    /// When redirected into a platform-specific optional dependency (native
    /// binlink optimization), the platform package may lay the binary out
    /// differently than the root package's `bin` field expects. esbuild
    /// mirrors the path exactly (`bin/esbuild` in both) but other packages
    /// ship the binary at the package root under the bin name (e.g.
    /// `@anthropic-ai/claude-code` has `bin/claude.exe` in the root package
    /// but `claude` at the root of `@anthropic-ai/claude-code-linux-x64`,
    /// which has no `bin` field of its own).
    ///
    /// Both candidates come from the root package's `bin` entry - its
    /// value (`target`) and its key (`bin_name`):
    ///   1. `<package_dir>/<target>` - the path from the root `bin` field
    ///   2. `<package_dir>/<bin_name>` - the bin name at package root
    ///
    /// Falls through to (1) when nothing exists so the existing
    /// `skipped_due_to_missing_bin` retry-without-redirect path still fires.
    fn resolveBinTarget(package_dir: []const u8, target: []const u8, bin_name: []const u8): [:0]const u8 {
        const primary = path.joinAbsStringZ(package_dir, &.{target}, .auto);

        if (!_.isNativeBinlinkRedirect()) {
            return primary;
        }

        if (bun.sys.exists(primary)) {
            return primary;
        }

        if (bin_name.len > 0) {
            const at_root = path.joinAbsStringZ(package_dir, &.{bin_name}, .auto);
            if (bun.sys.exists(at_root)) {
                return at_root;
            }
        }

        // Nothing found; return the primary so `linkBinOrCreateShim` sets
        // `skipped_due_to_missing_bin` and the caller retries without the
        // redirect.
        return path.joinAbsStringZ(package_dir, &.{target}, .auto);
    }

    /// uses `this.abs_target_buf`
    pub fn buildTargetPackageDir(): []const u8 {
        const dest_dir_without_trailing_slash = strings.withoutTrailingSlash(_.target_node_modules_path.slice());

        let remain = _.abs_target_buf;

        @memcpy(remain[0..dest_dir_without_trailing_slash.len], dest_dir_without_trailing_slash);
        remain = remain[dest_dir_without_trailing_slash.len..];
        remain[0] = std.fs.path.sep;
        remain = remain[1..];

        const package_name = _.target_package_name.slice();
        @memcpy(remain[0..package_name.len], package_name);
        remain = remain[package_name.len..];
        remain[0] = std.fs.path.sep;
        remain = remain[1..];

        return _.abs_target_buf[0 .. @intFromPtr(remain.ptr) - @intFromPtr(_.abs_target_buf.ptr)];
    }

    pub fn buildDestinationDir(global: bool): []u8 {
        const dest_dir_without_trailing_slash = strings.withoutTrailingSlash(_.node_modules_path.slice());

        let remain = _.abs_dest_buf;
        if (global) {
            const global_bin_path_without_trailing_slash = strings.withoutTrailingSlash(_.global_bin_path);
            @memcpy(remain[0..global_bin_path_without_trailing_slash.len], global_bin_path_without_trailing_slash);
            remain = remain[global_bin_path_without_trailing_slash.len..];
            remain[0] = std.fs.path.sep;
            remain = remain[1..];
        } else {
            @memcpy(remain[0..dest_dir_without_trailing_slash.len], dest_dir_without_trailing_slash);
            remain = remain[dest_dir_without_trailing_slash.len..];
            @memcpy(remain[0.."/.bin/".len], std.fs.path.sep_str ++ ".bin" ++ std.fs.path.sep_str);
            remain = remain["/.bin/".len..];
        }

        return remain;
    }

    // target: what the symlink points to
    // destination: where the symlink exists on disk
    pub fn link(*_, global: bool) {
        const package_dir = _.buildTargetPackageDir();
        let abs_dest_buf_remain = _.buildDestinationDir(global);

        bun.assertWithLocation(_.bin.tag != .none, @src());

        switch (_.bin.tag) {
            .none => {},
            .file => {
                const target = _.bin.value.file.slice(_.string_buf);
                if (target.len == 0) return;

                const unscoped_package_name = Dependency.unscopedPackageName(_.package_name.slice());

                // for normalizing `target`
                const abs_target = _.resolveBinTarget(package_dir, target, unscoped_package_name);

                @memcpy(abs_dest_buf_remain[0..unscoped_package_name.len], unscoped_package_name);
                abs_dest_buf_remain = abs_dest_buf_remain[unscoped_package_name.len..];
                abs_dest_buf_remain[0] = 0;
                const abs_dest_len = @intFromPtr(abs_dest_buf_remain.ptr) - @intFromPtr(_.abs_dest_buf.ptr);
                const abs_dest: [:0]const u8 = _.abs_dest_buf[0..abs_dest_len :0];

                _.linkBinOrCreateShim(abs_target, abs_dest, global);
            },
            .named_file => {
                const name = _.bin.value.named_file[0].slice(_.string_buf);
                const normalized_name = Bin.normalizedBinName(name);
                const target = _.bin.value.named_file[1].slice(_.string_buf);
                if (normalized_name.len == 0 || target.len == 0) return;

                // for normalizing `target`
                const abs_target = _.resolveBinTarget(package_dir, target, normalized_name);

                @memcpy(abs_dest_buf_remain[0..normalized_name.len], normalized_name);
                abs_dest_buf_remain = abs_dest_buf_remain[normalized_name.len..];
                abs_dest_buf_remain[0] = 0;
                const abs_dest_len = @intFromPtr(abs_dest_buf_remain.ptr) - @intFromPtr(_.abs_dest_buf.ptr);
                const abs_dest: [:0]const u8 = _.abs_dest_buf[0..abs_dest_len :0];

                _.linkBinOrCreateShim(abs_target, abs_dest, global);
            },
            .map => {
                let i = _.bin.value.map.begin();
                const end = _.bin.value.map.end();

                const abs_dest_dir_end = abs_dest_buf_remain;

                while (i < end) : (i += 2) {
                    const bin_dest = _.extern_string_buf[i].slice(_.string_buf);
                    const normalized_bin_dest = Bin.normalizedBinName(bin_dest);
                    const bin_target = _.extern_string_buf[i + 1].slice(_.string_buf);
                    if (bin_target.len == 0 || normalized_bin_dest.len == 0) continue;

                    const abs_target = _.resolveBinTarget(package_dir, bin_target, normalized_bin_dest);

                    abs_dest_buf_remain = abs_dest_dir_end;
                    @memcpy(abs_dest_buf_remain[0..normalized_bin_dest.len], normalized_bin_dest);
                    abs_dest_buf_remain = abs_dest_buf_remain[normalized_bin_dest.len..];
                    abs_dest_buf_remain[0] = 0;
                    const abs_dest_len = @intFromPtr(abs_dest_buf_remain.ptr) - @intFromPtr(_.abs_dest_buf.ptr);
                    const abs_dest: [:0]const u8 = _.abs_dest_buf[0..abs_dest_len :0];

                    _.linkBinOrCreateShim(abs_target, abs_dest, global);
                }
            },
            .dir => {
                const target = _.bin.value.dir.slice(_.string_buf);
                if (target.len == 0) return;

                // for normalizing `target`
                const abs_target_dir = path.joinAbsStringZ(package_dir, &.{target}, .auto);

                let target_dir = bun.openDirAbsolute(abs_target_dir) catch |e| {
                    if (e == error.ENOENT) {
                        // https://github.com/npm/cli/blob/366c07e2f3cb9d1c6ddbd03e624a4d73fbd2676e/node_modules/bin-links/lib/link-gently.js#L43
                        // avoid erroring when the directory does not exist
                        return;
                    }
                    _.err = e;
                    return;
                };
                defer target_dir.close();

                const abs_dest_dir_end = abs_dest_buf_remain;

                let iter = target_dir.iterate();
                while (entry := iter.next() catch null) {
                    switch (entry.kind) {
                        .sym_link | .file => {
                            // `this.abs_target_buf` is available now because `path.joinAbsStringZ` copied everything into `parse_join_input_buffer`
                            const abs_target = path.joinAbsStringBufZ(abs_target_dir, _.abs_target_buf, &.{entry.name}, .auto);

                            abs_dest_buf_remain = abs_dest_dir_end;
                            @memcpy(abs_dest_buf_remain[0..entry.name.len], entry.name);
                            abs_dest_buf_remain = abs_dest_buf_remain[entry.name.len..];
                            abs_dest_buf_remain[0] = 0;
                            const abs_dest_len = @intFromPtr(abs_dest_buf_remain.ptr) - @intFromPtr(_.abs_dest_buf.ptr);
                            const abs_dest: [:0]const u8 = _.abs_dest_buf[0..abs_dest_len :0];

                            _.linkBinOrCreateShim(abs_target, abs_dest, global);
                        },
                        else => {},
                    }
                }
            },
        }
    }

    pub fn unlink(*_, global: bool) {
        const package_dir = _.buildTargetPackageDir();
        let abs_dest_buf_remain = _.buildDestinationDir(global);

        bun.assertWithLocation(_.bin.tag != .none, @src());

        switch (_.bin.tag) {
            .none => {},
            .file => {
                const unscoped_package_name = Dependency.unscopedPackageName(_.package_name.slice());
                @memcpy(abs_dest_buf_remain[0..unscoped_package_name.len], unscoped_package_name);
                abs_dest_buf_remain = abs_dest_buf_remain[unscoped_package_name.len..];
                abs_dest_buf_remain[0] = 0;
                const abs_dest_len = @intFromPtr(abs_dest_buf_remain.ptr) - @intFromPtr(_.abs_dest_buf.ptr);
                const abs_dest: [:0]const u8 = _.abs_dest_buf[0..abs_dest_len :0];

                BinLinker.unlinkBinOrShim(abs_dest);
            },
            .named_file => {
                const name = _.bin.value.named_file[0].slice(_.string_buf);
                const normalized_name = Bin.normalizedBinName(name);
                if (normalized_name.len == 0) return;

                @memcpy(abs_dest_buf_remain[0..normalized_name.len], normalized_name);
                abs_dest_buf_remain = abs_dest_buf_remain[normalized_name.len..];
                abs_dest_buf_remain[0] = 0;
                const abs_dest_len = @intFromPtr(abs_dest_buf_remain.ptr) - @intFromPtr(_.abs_dest_buf.ptr);
                const abs_dest: [:0]const u8 = _.abs_dest_buf[0..abs_dest_len :0];

                BinLinker.unlinkBinOrShim(abs_dest);
            },
            .map => {
                let i = _.bin.value.map.begin();
                const end = _.bin.value.map.end();

                const abs_dest_dir_end = abs_dest_buf_remain;

                while (i < end) : (i += 2) {
                    const bin_dest = _.extern_string_buf[i].slice(_.string_buf);
                    const normalized_bin_dest = Bin.normalizedBinName(bin_dest);
                    if (normalized_bin_dest.len == 0) continue;

                    abs_dest_buf_remain = abs_dest_dir_end;
                    @memcpy(abs_dest_buf_remain[0..normalized_bin_dest.len], normalized_bin_dest);
                    abs_dest_buf_remain = abs_dest_buf_remain[normalized_bin_dest.len..];
                    abs_dest_buf_remain[0] = 0;
                    const abs_dest_len = @intFromPtr(abs_dest_buf_remain.ptr) - @intFromPtr(_.abs_dest_buf.ptr);
                    const abs_dest: [:0]const u8 = _.abs_dest_buf[0..abs_dest_len :0];

                    BinLinker.unlinkBinOrShim(abs_dest);
                }
            },
            .dir => {
                const target = _.bin.value.dir.slice(_.string_buf);
                if (target.len == 0) return;

                const abs_target_dir = path.joinAbsStringZ(package_dir, &.{target}, .auto);

                let target_dir = bun.openDirAbsolute(abs_target_dir) catch |e| {
                    _.err = e;
                    return;
                };
                defer target_dir.close();

                const abs_dest_dir_end = abs_dest_buf_remain;

                let iter = target_dir.iterate();
                while (entry := iter.next() catch null) {
                    switch (entry.kind) {
                        .sym_link | .file => {
                            abs_dest_buf_remain = abs_dest_dir_end;
                            @memcpy(abs_dest_buf_remain[0..entry.name.len], entry.name);
                            abs_dest_buf_remain = abs_dest_buf_remain[entry.name.len..];
                            abs_dest_buf_remain[0] = 0;
                            const abs_dest_len = @intFromPtr(abs_dest_buf_remain.ptr) - @intFromPtr(_.abs_dest_buf.ptr);
                            const abs_dest: [:0]const u8 = _.abs_dest_buf[0..abs_dest_len :0];

                            BinLinker.unlinkBinOrShim(abs_dest);
                        },
                        else => {},
                    }
                }
            },
        }
    }
}

pub enum FormatStyle {
    single_line,
    multi_line,
}

type WriteIndentFn = *const fn (*std.Io.Writer, *u32) std.Io.Writer.Error!void;
type SeenMap = bun.StringHashMap(void);
const MAX_PATH_BYTES = bun.MAX_PATH_BYTES;
const MAX_SHEBANG_INPUT = WinBinLinkingShim.Shebang.max_shebang_input_length;
let linker_umask: bun.Mode = @as(bun.Mode, 0);
let has_set_umask = false;

const log = bun.Output.scoped(.BinLinker, .hidden);
type string = []const u8;
type stringZ = [:0]const u8;

import Dependency from "./dependency.zig";
import Environment from "../bun_core/env.zig";
import WinBinLinkingShim from "./windows-shim/BinLinkingShim.zig";

import Install from "./install.zig";
const ExternalStringList = Install.ExternalStringList;
const AbsPath = bun.AbsPath({});

import bun from "bun";
const BunJSON = bun.json;
const OOM = bun.OOM;
const path = bun.path;
const strings = bun.strings;

const Semver = bun.Semver;
const ExternalString = Semver.ExternalString;
const String = Semver.String;
