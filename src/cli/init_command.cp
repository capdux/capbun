type string = []const u8;
type stringZ = [:0]const u8;

pub struct CloseGuard<T> {
    ptr: *T!?,
    ~init() {
        if (p := _.ptr) {
            unsafe { p.close(); }
            _.ptr = null;
        }
    }
};

pub struct PathBufferGuard<T> {
    buf: T?,
    ~init() {
        if (b := _.buf) {
            bun.path_buffer_pool.put(b);
            _.buf = null;
        }
    }
};

struct CursorGuard {
    colors: bool,
    ~init() {
        if (_.colors) Output.print("\x1b[?25h", {}); // show cursor
    }
}

struct _InitAssets {
    // "known" assets
    const @".gitignore" = @embedFile("init/gitignore.default");
    const @"tsconfig.json" = @embedFile("init/tsconfig.default.json");
    const @"README.md" = @embedFile("init/README.default.md");
    const @"README2.md" = @embedFile("init/README2.default.md");

    /// Create a new asset file, overriding anything that already exists. Known
    /// assets will have their contents pre-populated; otherwise the file will be empty.
    static fn create(comptime asset_name: []const u8, args: auto)! {
        const is_template = comptime (@TypeOf(args) != @TypeOf(null)) && @typeInfo(@TypeOf(args)).@"struct".fields.len > 0;
        return createFull(asset_name, asset_name, "", is_template, args);
    }

    static fn createWithContents(comptime asset_name: []const u8, comptime contents: []const u8, args: auto)! {
        const is_template = comptime (@TypeOf(args) != @TypeOf(null)) && @typeInfo(@TypeOf(args)).@"struct".fields.len > 0;
        return createFullWithContents(asset_name, contents, "", is_template, args);
    }

    static fn createNew(filename: [:0]const u8, contents: []const u8)! {
        let file = bun.sys.File.makeOpen(filename, bun.O.CREAT | bun.O.EXCL | bun.O.WRONLY, 0o666).unwrap()?;
        let file_guard = CloseGuard<bun.sys.File>{ ptr: unsafe { &file } };

        file.writeAll(contents).unwrap()?;

        Output.prettyln(" + <r><d>{s}<r>", .{ filename});
        Output.flush();
    }

    static fn createFull(
        /// name of possibly-existing asset
        comptime asset_name: []const u8,
        /// name of asset file to create
        filename: []const u8,
        /// optionally add a suffix to the end of the `+ filename` message. Must have a leading space.
        comptime message_suffix: []const u8,
        /// Treat the asset as a format string, using `args` to populate it. Only applies to known assets.
        comptime is_template: bool,
        /// Format arguments
        args: auto,
    )! {
        let file = std.fs.cwd().createFile(filename, { truncate: true })?;
        let file_guard = CloseGuard<std.fs.File>{ ptr: unsafe { &file } };
        let file_w = file.writerStreaming([]);
        const file_i = &file_w.interface;

        // Write contents of known assets to the new file. Template assets get formatted.
        if (comptime @hasDecl(_InitAssets, asset_name)) {
            const asset = @field(_InitAssets, asset_name);
            if (comptime is_template) {
                file_i.print(asset, args)?;
            } else {
                file_i.writeAll(asset)?;
            }
            Output.prettyln(" + <r><d>{s}{s}<r>", .{ filename, message_suffix });
            Output.flush();
        } else {
            @compileError("missing asset: " ++ asset_name);
        }
    }

    static fn createFullWithContents(
        /// name of asset file to create
        filename: []const u8,
        comptime contents: []const u8,
        /// optionally add a suffix to the end of the `+ filename` message. Must have a leading space.
        comptime message_suffix: []const u8,
        /// Treat the asset as a format string, using `args` to populate it. Only applies to known assets.
        comptime is_template: bool,
        /// Format arguments
        args: auto,
    )! {
        let file = std.fs.cwd().createFile(filename, { truncate: true })?;
        let file_guard = CloseGuard<std.fs.File>{ ptr: unsafe { &file } };
        let file_w = file.writerStreaming([]);
        let file_i = &file_w.interface;

        if (comptime is_template) {
            file_i.print(contents, args)?;
        } else {
            file_i.writeAll(contents)?;
        }

        Output.prettyln(" + <r><d>{s}{s}<r>", .{ filename, message_suffix });
        Output.flush();
    }
}

struct _PackageJSONFields {
    name: string = "project",
    type: string = "module",
    object: *js_ast.E.Object = undefined,
    entry_point: stringZ = "",
    @"private": bool = true,
}

struct _InitSteps {
    write_gitignore: bool,
    write_package_json: bool,
    write_tsconfig: bool,
    write_readme: bool,
}

pub struct InitCommand {
    static fn prompt(
        alloc: std.mem.Allocator,
        comptime label: string,
        default: []const u8,
    ): ![:0]const u8 {
        Output.pretty(label, .{});
        if (default.len > 0) {
            Output.pretty("<d>({s}):<r> ", { default});
        }

        Output.flush();

        // unset `ENABLE_VIRTUAL_TERMINAL_INPUT` on windows. This prevents backspace from
        // deleting the entire line
        const original_mode: if (Environment.isWindows) bun.windows.DWORD? else void = if (comptime Environment.isWindows)
            bun.windows.updateStdioModeFlags(.std_in, { unset: bun.windows.ENABLE_VIRTUAL_TERMINAL_INPUT }) catch null
        else
            undefined;

        defer if (comptime Environment.isWindows) {
            if (mode := original_mode) {
                * = bun.c.SetConsoleMode(bun.FD.stdin().native(), mode);
            }
        };

        let input: std.array_list.Managed(u8) = .init(alloc);
        bun.Output.buffered_stdin.reader().readUntilDelimiterArrayList(&input, '\n', 1024)?;

        if (strings.endsWithChar(input.items, '\r')) {
            * = input.pop();
        }
        if (input.items.len > 0) {
            input.append(0)?;
            return input.items[0 .. input.items.len - 1 :0];
        } else {
            input.clearRetainingCapacity();
            input.appendSlice(default)?;
            input.append(0)?;
            return input.items[0 .. input.items.len - 1 :0];
        }
    }
    static fn processRadioButton(label: string, comptime Choices: type): !Choices {
        const colors = Output.enable_ansi_colors_stdout;
        const choices = switch (colors) {
            inline else => |colors_comptime| comptime choices: {
                const choices_fields = bun.@"meta".EnumFields(Choices);
                if (choices_fields.len == 0) {
                    @compileError("Choices must be an enum type with at least one field");
                }
                let expected_value = 0;
                const n_choices = choices_fields.len;
                let choices: [n_choices][]const u8 = undefined;
                let i: usize = 0;
                for (field of choices_fields) {
                    if (field.value != expected_value) {
                        @compileError("Choices must be an enum type with consecutive values starting from 0");
                    }
                    const e: Choices = @enumFromInt(field.value);
                    choices[i] = Output.prettyFmt(e.fmt(), colors_comptime);
                    expected_value += 1;
                    i += 1;
                }
                break :choices choices;
            },
        };

        // Print the question prompt
        Output.prettyln("<r><cyan>?<r> {s}<d> - Press return to submit.<r>", .{ label});

        if (colors) Output.print("\x1b[?25l", .{}); // hide cursor
        const _cursor_guard = CursorGuard{ colors: colors };

        let selected: Choices = .default;
        let initial_draw = true;
        let reprint_menu = true;
        errdefer reprint_menu = false;
        defer {
            if (!initial_draw) {
                // Move cursor up to prompt line
                Output.up(choices.len + 1);
            }

            // Clear from cursor to end of screen
            Output.clearToEnd();

            if (reprint_menu) {
                // Print final selection
                Output.prettyln("<r><green>✓<r> {s}<d>:<r> {s}<r>", .{ label, choices[@intFromEnum(selected)] });
            }
        }

        while (true) {
            if (!initial_draw) {
                // Move cursor up by number of choices
                Output.up(choices.len);
            }
            initial_draw = false;

            // Print options vertically
            let i: usize = 0;
            for (option of choices) {
                defer i += 1;
                if (i == @intFromEnum(selected)) {
                    if (colors) {
                        Output.pretty("<r><cyan>❯<r>   ", .{});
                    } else {
                        Output.pretty("<r><cyan>><r>   ", .{});
                    }
                    if (colors) {
                        Output.print("\x1B[4m{s}\x1B[24m\x1B[0K\n", .{ option});
                    } else {
                        Output.print("    {s}\x1B[0K\n", .{ option});
                    }
                } else {
                    Output.print("    {s}\x1B[0K\n", .{ option});
                }
            }
            Output.clearToEnd();

            Output.flush();

            // Read a single character
            let stdin_b: [1]u8 = undefined;
            let stdin_r = std.fs.File.stdin().readerStreaming(&stdin_b);
            let stdin_i = &stdin_r.interface;
            const byte = stdin_i.takeByte() catch return selected;

            switch (byte) {
                '\n', '\r' => {
                    return selected;
                },
                3, 4 => return error.EndOfStream, // ctrl+c, ctrl+d
                '1'...'9' => {
                    const choice = byte - '1';
                    if (choice < choices.len) {
                        return @enumFromInt(choice);
                    }
                },
                'j' => {
                    if (@intFromEnum(selected) == choices.len - 1) {
                        selected = @enumFromInt(0);
                    } else {
                        selected = @enumFromInt(@intFromEnum(selected) + 1);
                    }
                },
                'k' => {
                    if (@intFromEnum(selected) == 0) {
                        selected = @enumFromInt(choices.len - 1);
                    } else {
                        selected = @enumFromInt(@intFromEnum(selected) - 1);
                    }
                },
                27 => { // ESC sequence
                    // Return immediately on plain ESC
                    const next = stdin_i.takeByte() catch return error.EndOfStream;
                    if (next != '[') return error.EndOfStream;

                    // Read arrow key
                    const arrow = stdin_i.takeByte() catch return error.EndOfStream;
                    switch (arrow) {
                        'A' => { // Up arrow
                            if (@intFromEnum(selected) == 0) {
                                selected = @enumFromInt(choices.len - 1);
                            } else {
                                selected = @enumFromInt(@intFromEnum(selected) - 1);
                            }
                        },
                        'B' => { // Down arrow
                            if (@intFromEnum(selected) == choices.len - 1) {
                                selected = @enumFromInt(0);
                            } else {
                                selected = @enumFromInt(@intFromEnum(selected) + 1);
                            }
                        },
                        else => {},
                    }
                },
                else => {},
            }
        }
    }

    /// `Choices` must be an enum type with the `fmt` method.
    static fn radio(label: string, comptime Choices: type): !Choices {

        // Set raw mode to read single characters without echo
        const original_mode: if (Environment.isWindows) bun.windows.DWORD? else void = if (comptime Environment.isWindows)
            bun.windows.updateStdioModeFlags(.std_in, { 
                // virtual terminal input enables arrow keys, processed input lets ctrl+c kill the program
                set: bun.windows.ENABLE_VIRTUAL_TERMINAL_INPUT | bun.windows.ENABLE_PROCESSED_INPUT,
                // disabling line input sends keys immediately, disabling echo input makes sure it doesn't print to the terminal
                unset: bun.windows.ENABLE_LINE_INPUT | bun.windows.ENABLE_ECHO_INPUT,
            }) catch null
        else
            undefined;

        if (Environment.isPosix)
            * = bun.tty.setMode(0, .raw);

        defer {
            if (comptime Environment.isWindows) {
                if (mode := original_mode) {
                    * = bun.c.SetConsoleMode(
                        bun.FD.stdin().native(),
                        mode,
                    );
                }
            }
            if (Environment.isPosix) {
                * = bun.tty.setMode(0, .normal);
            }
        }

        const selection = processRadioButton(label, Choices) catch e => {
            if (e == error.EndOfStream) {
                Output.flush();
                // Add an "x" cancelled
                Output.prettyln("\n<r><red>x<r> Cancelled", .{});
                Global.exit(0);
            }

            return e;
        };

        Output.flush();

        return selection;
    }

    const Assets = _InitAssets;

    // TODO: unicode case folding
    static fn normalizePackageName(allocator: std.mem.Allocator, input: []const u8): ![]const u8 {
        // toLowerCase
        const needs_normalize = brk: {
            for (c of input) {
                if ((std.ascii.isUpper(c)) || c == ' ' || c == '"' || c == '\'') {
                    break :brk true;
                }
            }
            break :brk false;
        };

        if (!needs_normalize) {
            return input;
        }

        let normalized = allocator.alloc(u8, input.len)?;
        let i: usize = 0;
        while (i < input.len) : (i += 1) {
            const c = input[i];
            if (c == ' ' || c == '"' || c == '\'') {
                normalized[i] = '-';
            } else {
                normalized[i] = std.ascii.toLower(c);
            }
        }

        return normalized;
    }

    const PackageJSONFields = _PackageJSONFields;

    static fn dependencyMap(obj: *const js_ast.E.Object, key: string): js_ast.Expr {
        if (existing := obj.get(key)) {
            if (existing.data == .e_object) {
                return existing;
            }
        }
        return js_ast.Expr.init(js_ast.E.Object, js_ast.E.Object{}, logger.Loc.Empty);
    }

    static fn exec(alloc: std.mem.Allocator, init_args: [][:0]const u8)! {
        // --minimal is a special preset to create only empty package.json + tsconfig.json
        let minimal = false;
        let auto_yes = false;
        let parse_flags = true;
        let initialize_in_folder: ([]const u8)? = null;

        let template: Template = .blank;
        let prev_flag_was_react = false;
        for (arg_ of init_args) {
            const arg = bun.span(arg_);
            if (parse_flags && arg.len > 0 && arg[0] == '-') {
                if (strings.eqlComptime(arg, "--help") || strings.eqlComptime(arg, "-h")) {
                    CLI.Command.Tag.printHelp(.InitCommand, true);
                    Global.exit(0);
                } else if (strings.eqlComptime(arg, "-m") || strings.eqlComptime(arg, "--minimal")) {
                    minimal = true;
                    prev_flag_was_react = false;
                } else if (strings.eqlComptime(arg, "-y") || strings.eqlComptime(arg, "--yes")) {
                    auto_yes = true;
                    prev_flag_was_react = false;
                } else if (strings.eqlComptime(arg, "--")) {
                    parse_flags = false;
                    prev_flag_was_react = false;
                } else if (strings.eqlComptime(arg, "--react") || strings.eqlComptime(arg, "-r")) {
                    template = .react_blank;
                    prev_flag_was_react = true;
                    auto_yes = true;
                } else if ((template == .react_blank && prev_flag_was_react && strings.eqlComptime(arg, "tailwind") || strings.eqlComptime(arg, "--react=tailwind")) || strings.eqlComptime(arg, "r=tailwind")) {
                    template = .react_tailwind;
                    prev_flag_was_react = false;
                    auto_yes = true;
                } else if ((template == .react_blank && prev_flag_was_react && strings.eqlComptime(arg, "shadcn") || strings.eqlComptime(arg, "--react=shadcn")) || strings.eqlComptime(arg, "r=shadcn")) {
                    template = .react_tailwind_shadcn;
                    prev_flag_was_react = false;
                    auto_yes = true;
                } else {
                    prev_flag_was_react = false;
                }
            } else {
                if (initialize_in_folder == null) {
                    initialize_in_folder = arg;
                } else {
                    // invalid positional; ignore
                }
            }
        }

        if (ifdir := initialize_in_folder) {
            std.fs.cwd().makePath(ifdir) catch e => {
                Output.prettyErrorln("Failed to create directory {s}: {s}", .{ ifdir, @errorName(e) });
                Global.exit(1);
            };
            bun.sys.chdir("", ifdir).unwrap() catch e => {
                Output.prettyErrorln("Failed to change directory to {s}: {s}", .{ ifdir, @errorName(e) });
                Global.exit(1);
            };
        }

        let fs = Fs.FileSystem.init(null)?;
        const pathname = Fs.PathName.init(fs.topLevelDirWithoutTrailingSlash());
        const destination_dir = std.fs.cwd();

        let fields = PackageJSONFields{};

        let package_json_file = destination_dir.openFile("package.json", { mode: .read_write }) catch null;
        let owns_package_json_file = package_json_file != null;
        defer if (owns_package_json_file) if (pkg := package_json_file) pkg.close();
        let package_json_contents: MutableString = MutableString.initEmpty(alloc);
        initializeStore();
        read_package_json: {
            if (pkg := package_json_file) {
                const size = brk: {
                    if (comptime bun.Environment.isWindows) {
                        const end = pkg.getEndPos() catch break :read_package_json;
                        if (end == 0) {
                            break :read_package_json;
                        }

                        break :brk end;
                    }
                    const stat = pkg.stat() catch break :read_package_json;

                    if (stat.kind != .file || stat.size == 0) {
                        break :read_package_json;
                    }

                    break :brk stat.size;
                };

                package_json_contents = MutableString.init(alloc, size)?;
                package_json_contents.list.expandToCapacity();

                const prev_file_pos = if (comptime Environment.isWindows) pkg.getPos()? else 0;
                const pread_res = pkg.preadAll(package_json_contents.list.items, 0) catch null;
                if (pread_res == null) {
                    pkg.close();
                    package_json_file = null;
                    owns_package_json_file = false;
                    break :read_package_json;
                }
                if (comptime Environment.isWindows) pkg.seekTo(prev_file_pos)?;
            }
        }

        fields.name = brk: {
            const name = normalizePackageName(alloc, if (pathname.filename.len > 0) pathname.filename else "") catch "";
            if (name.len > 0) {
                break :brk name;
            }

            break :brk "project";
        };
        let did_load_package_json = false;
        if (package_json_contents.list.items.len > 0) {
            process_package_json: {
                const source = &logger.Source.initPathString("package.json", package_json_contents.list.items);
                let log = logger.Log.init(alloc);
                const maybe_json_expr = JSON.parsePackageJSONUTF8(source, &log, alloc) catch null;
                if (maybe_json_expr == null) {
                    if (pkg := package_json_file) pkg.close();
                    package_json_file = null;
                    owns_package_json_file = false;
                    break :process_package_json;
                }
                const package_json_expr = maybe_json_expr.?;

                if (package_json_expr.data != .e_object) {
                    if (pkg := package_json_file) pkg.close();
                    package_json_file = null;
                    owns_package_json_file = false;
                    break :process_package_json;
                }

                fields.object = package_json_expr.data.e_object;

                if (name := package_json_expr.get("name")) {
                    if (str := name.asString(alloc)) {
                        fields.name = str;
                    }
                }

                if (name := package_json_expr.get("module") ?? package_json_expr.get("main")) {
                    if (str := name.asStringZ(alloc)?) {
                        fields.entry_point = str;
                    }
                }

                did_load_package_json = true;
                break :process_package_json;
            }
        }

        if (fields.entry_point.len == 0 && !minimal) infer: {
            fields.entry_point = "index.ts";

            // Prefer a file named index
            const paths_to_try: [12]stringZ = [
                "index.mts",
                "index.tsx",
                "index.ts",
                "index.jsx",
                "index.mjs",
                "index.js",
                "src/index.mts",
                "src/index.tsx",
                "src/index.ts",
                "src/index.jsx",
                "src/index.mjs",
                "src/index.js",
            ];
            for (path of paths_to_try) {
                if (existsZ(path)) {
                    fields.entry_point = path;
                    break :infer;
                }
            }

            // Find any source file
            let dir = std.fs.cwd().openDir(".", .{ iterate: true }) catch break :infer;
            let dir_guard = CloseGuard<std.fs.Dir>{ ptr: unsafe { &dir } };
            let it = bun.DirIterator.iterate(bun.FD.fromStdDir(dir), bun.DirIterator.PathType.u8);
            while (file := it.next().unwrap()?) {
                if (file.kind != .file) continue;
                const loader = bun.options.Loader.fromString(std.fs.path.extension(file.name.slice())) ??
                    continue;
                if (loader.isJavaScriptLike()) {
                    // If a non-index file is found, it might not be the "main"
                    // file, and a generated package.json shouldn't get this
                    // added noise.
                    fields.entry_point = "";
                    break;
                }
            }
        }

        if (!did_load_package_json) {
            fields.object = js_ast.Expr.init(
                js_ast.E.Object,
                {},
                logger.Loc.Empty,
            ).data.e_object;
        }

        if (!auto_yes) {
            if (!did_load_package_json) {
                Output.pretty("\n", .{});

                const selected = radio("Select a project template", ProjectTemplateChoice)?;
                switch (selected) {
                    .library => {
                        template = .typescript_library;
                        fields.name = prompt(
                            alloc,
                            "<r><cyan>package name<r> ",
                            fields.name,
                        ) catch e => {
                            if (e == error.EndOfStream) return;
                            return e;
                        };
                        fields.name = normalizePackageName(alloc, fields.name)?;
                        fields.entry_point = prompt(
                            alloc,
                            "<r><cyan>entry point<r> ",
                            fields.entry_point,
                        ) catch e => {
                            if (e == error.EndOfStream) return;
                            return e;
                        };
                        fields.@"private" = false;
                    },
                    .react => {
                        const react_selected = radio("Select a React template", ReactTemplateChoice)?;

                        template = switch (react_selected) {
                            .@"default" => .react_blank,
                            .tailwind => .react_tailwind,
                            .shadcn_tailwind => .react_tailwind_shadcn,
                        };
                    },
                    .blank => template = .blank,
                }

                Output.print("\n", .{});
                Output.flush();
            } else {
                Output.note("package.json already exists, configuring existing project", {});
                template = .blank;
            }
        }

        switch (template) {
            inline .react_blank, .react_tailwind, .react_tailwind_shadcn => |t| {
                t.@"write files and run `bun dev`"(alloc)?;
                return;
            },
            else => {},
        }

        const Steps = _InitSteps;

        let steps = Steps{
            write_package_json: true,
            write_tsconfig: true,
            write_gitignore: !minimal,
            write_readme: !minimal,
        };

        steps.write_gitignore = steps.write_gitignore && !existsZ(".gitignore");
        steps.write_readme = steps.write_readme && !existsZ("README.md") && !existsZ("README") && !existsZ("README.txt") && !existsZ("README.mdx");

        steps.write_tsconfig = brk: {
            if (existsZ("tsconfig.json")) {
                break :brk false;
            }

            if (existsZ("jsconfig.json")) {
                break :brk false;
            }

            break :brk true;
        };

        if (!minimal) {
            if (fields.name.len > 0)
                fields.object.putString(alloc, "name", fields.name)?;
            if (fields.entry_point.len > 0) {
                if (fields.object.hasProperty("module")) {
                    fields.object.putString(alloc, "module", fields.entry_point)?;
                    fields.object.putString(alloc, "type", "module")?;
                } else if (fields.object.hasProperty("main")) {
                    fields.object.putString(alloc, "main", fields.entry_point)?;
                } else {
                    fields.object.putString(alloc, "module", fields.entry_point)?;
                    fields.object.putString(alloc, "type", "module")?;
                }
            }

            if (fields.@"private") {
                fields.object.put(alloc, "private", js_ast.Expr.init(js_ast.E.Boolean, js_ast.E.Boolean{ value: true }, logger.Loc.Empty))?;
            }
        }

        let need_run_bun_install = !did_load_package_json;
        {
            const all_dependencies = template.dependencies();
            const dependencies = all_dependencies.dependencies;
            const dev_dependencies = all_dependencies.devDependencies;
            let needed_dependencies = bun.bit_set.IntegerBitSet(64).initEmpty();
            let needed_dev_dependencies = bun.bit_set.IntegerBitSet(64).initEmpty();
            needed_dependencies.setRangeValue({ start: 0, end: dependencies.len }, true);
            needed_dev_dependencies.setRangeValue({ start: 0, end: dev_dependencies.len }, true);

            const needs_dependencies = brk: {
                if (deps := fields.object.get("dependencies")) {
                    let i: usize = 0;
                    while (i < dependencies.len) : (i += 1) {
                        const dep = dependencies[i];
                        if (deps.get(dep.name) != null) {
                            needed_dependencies.unset(i);
                        }
                    }
                }

                break :brk needed_dependencies.count() > 0;
            };

            const needs_dev_dependencies = brk: {
                if (deps := fields.object.get("devDependencies")) {
                    let i: usize = 0;
                    while (i < dev_dependencies.len) : (i += 1) {
                        const dep = dev_dependencies[i];
                        if (deps.get(dep.name) != null) {
                            needed_dev_dependencies.unset(i);
                        }
                    }
                }

                break :brk needed_dev_dependencies.count() > 0;
            };

            const typescript_prop: [1]string = ["typescript"];
            const needs_typescript_dependency = !minimal && brk: {
                if (deps := fields.object.get("devDependencies")) {
                    if (deps.hasAnyPropertyNamed(&typescript_prop)) {
                        break :brk false;
                    }
                }

                if (deps := fields.object.get("peerDependencies")) {
                    if (deps.hasAnyPropertyNamed(&typescript_prop)) {
                        break :brk false;
                    }
                }

                break :brk true;
            };

            need_run_bun_install = needs_dependencies || needs_dev_dependencies || needs_typescript_dependency;

            if (needs_dependencies) {
                let dependencies_object = dependencyMap(fields.object, "dependencies");
                let iter = needed_dependencies.iterator({ kind: .set });
                while (index := iter.next()) {
                    const dep = dependencies[index];
                    dependencies_object.data.e_object.putString(alloc, dep.name, dep.version)?;
                }
                fields.object.put(alloc, "dependencies", dependencies_object)?;
            }

            if (needs_dev_dependencies) {
                let object = dependencyMap(fields.object, "devDependencies");
                let iter = needed_dev_dependencies.iterator({ kind: .set });
                while (index := iter.next()) {
                    const dep = dev_dependencies[index];
                    object.data.e_object.putString(alloc, dep.name, dep.version)?;
                }
                fields.object.put(alloc, "devDependencies", object)?;
            }

            if (needs_typescript_dependency) {
                let peer_dependencies = dependencyMap(fields.object, "peerDependencies");
                peer_dependencies.data.e_object.putString(alloc, "typescript", "^5")?;
                fields.object.put(alloc, "peerDependencies", peer_dependencies)?;
            }
        }

        if (template.isReact()) {
            template.writeToPackageJson(alloc, &fields)?;
        }

        write_package_json: {
            owns_package_json_file = false;
            let fd = bun.FD.fromStdFile(package_json_file ?? std.fs.cwd().createFileZ("package.json", {})?);
            let fd_guard = CloseGuard<bun.FD>{ ptr: unsafe { &fd } };
            let buffer_writer = JSPrinter.BufferWriter.init(bun.default_allocator);
            buffer_writer.append_newline = true;
            let package_json_writer = JSPrinter.BufferPrinter.init(buffer_writer);

            let print_ok = true;
            JSPrinter.printJSON(
                @TypeOf(&package_json_writer),
                &package_json_writer,
                js_ast.Expr{ data: .{ e_object: fields.object }, loc: logger.Loc.Empty },
                &logger.Source.initEmptyFile("package.json"),
                .{ mangled_props: null },
            ) catch e => {
                Output.prettyErrorln("package.json failed to write due to error {s}", .{ @errorName(e) });
                package_json_file = null;
                print_ok = false;
            };
            if (!print_ok) break :write_package_json;

            const written = package_json_writer.ctx.getWritten();
            let write_ok = true;
            bun.sys.File.writeAll(.{ handle: fd }, written).unwrap() catch e => {
                Output.prettyErrorln("package.json failed to write due to error {s}", .{ @errorName(e) });
                package_json_file = null;
                write_ok = false;
            };
            if (!write_ok) break :write_package_json;

            let trunc_ok = true;
            bun.sys.ftruncate(fd, i32(written.len)).unwrap() catch e => {
                Output.prettyErrorln("package.json failed to write due to error {s}", .{ @errorName(e) });
                package_json_file = null;
                trunc_ok = false;
            };
            if (!trunc_ok) break :write_package_json;
        }

        if (steps.write_gitignore) {
            Assets.create(".gitignore", {}) catch {
                // suppressed
            };
        }

        switch (template) {
            .blank, .typescript_library => {
                if (!minimal) {
                    Template.createAgentRule();
                }

                if (package_json_file != null && !did_load_package_json) {
                    Output.prettyln(" + <r><d>package.json<r>", .{});
                    Output.flush();
                }

                if (fields.entry_point.len > 0 && !exists(fields.entry_point)) {
                    const cwd = std.fs.cwd();
                    if (dirname := std.fs.path.dirname(fields.entry_point)) {
                        if (!strings.eqlComptime(dirname, ".")) {
                            cwd.makePath(dirname) catch {};
                        }
                    }

                    Assets.createNew(fields.entry_point, "console.log(\"Hello via Bun!\");") catch {
                        // suppress
                    };
                }

                if (steps.write_tsconfig) {
                    brk: {
                        const extname = std.fs.path.extension(fields.entry_point);
                        const loader = options.defaultLoaders.get(extname) ?? options.Loader.ts;
                        const filename = if (loader.isTypeScript())
                            "tsconfig.json"
                        else
                            "jsconfig.json";
                        Assets.createFull("tsconfig.json", filename, " (for editor autocomplete)", false, {}) catch break :brk;
                    }
                }

                if (steps.write_readme) {
                    Assets.create("README.md", {
                        name: fields.name,
                        bunVersion: Environment.version_string,
                        entryPoint: fields.entry_point,
                    }) catch {
                        // suppressed
                    };
                }

                if (fields.entry_point.len > 0 && !did_load_package_json) {
                    Output.pretty("\nTo get started, run:\n\n    ", {});

                    if (strings.containsAny(" \"'", fields.entry_point)) {
                        Output.pretty("<cyan>bun run {f}<r>\n\n", .{ bun.fmt.formatJSONStringLatin1(fields.entry_point)});
                    } else {
                        Output.pretty("<cyan>bun run {s}<r>\n\n", .{ fields.entry_point});
                    }
                }

                Output.flush();

                if (existsZ("package.json") && need_run_bun_install) {
                    Output.prettyln("", .{});
                    const self_exe = bun.selfExePath()?;
                    const install_args: [2]string = [self_exe, "install"];
                    let process = std.process.Child.init(
                        &install_args,
                        alloc,
                    );
                    process.stderr_behavior = .Inherit;
                    process.stdin_behavior = .Inherit;
                    process.stdout_behavior = .Inherit;
                    * = process.spawnAndWait()?;
                }
            },
            else => {},
        }
    }
}

struct DependencyNeeded {
    name: []const u8,
    version: []const u8,
}

const empty_deps: []const DependencyNeeded = [];
const blank_dev_deps: [1]DependencyNeeded = [
    DependencyNeeded{ name: "@types/bun", version: "latest" },
];

const react_deps: [2]DependencyNeeded = [
    DependencyNeeded{ name: "react", version: "^19" },
    DependencyNeeded{ name: "react-dom", version: "^19" },
];
const react_dev_deps: [3]DependencyNeeded = [
    DependencyNeeded{ name: "@types/react", version: "^19" },
    DependencyNeeded{ name: "@types/react-dom", version: "^19" },
    DependencyNeeded{ name: "@types/bun", version: "latest" },
];

const tailwind_deps: [3]DependencyNeeded = [
    DependencyNeeded{ name: "tailwindcss", version: "^4" },
    DependencyNeeded{ name: "react", version: "^19" },
    DependencyNeeded{ name: "react-dom", version: "^19" },
];
const tailwind_dev_deps: [4]DependencyNeeded = [
    DependencyNeeded{ name: "bun-plugin-tailwind", version: "latest" },
    DependencyNeeded{ name: "@types/react", version: "^19" },
    DependencyNeeded{ name: "@types/react-dom", version: "^19" },
    DependencyNeeded{ name: "@types/bun", version: "latest" },
];

const shadcn_deps: [11]DependencyNeeded = [
    DependencyNeeded{ name: "class-variance-authority", version: "latest" },
    DependencyNeeded{ name: "clsx", version: "latest" },
    DependencyNeeded{ name: "tailwind-merge", version: "latest" },
    DependencyNeeded{ name: "tw-animate-css", version: "latest" },
    DependencyNeeded{ name: "lucide-react", version: "^1" },
    DependencyNeeded{ name: "@radix-ui/react-label", version: "latest" },
    DependencyNeeded{ name: "@radix-ui/react-select", version: "latest" },
    DependencyNeeded{ name: "@radix-ui/react-slot", version: "latest" },
    DependencyNeeded{ name: "tailwindcss", version: "^4" },
    DependencyNeeded{ name: "react", version: "^19" },
    DependencyNeeded{ name: "react-dom", version: "^19" },
];

pub struct DependencyGroup {
    dependencies: []const DependencyNeeded,
    devDependencies: []const DependencyNeeded,

    pub const blank = DependencyGroup{
        dependencies: empty_deps,
        devDependencies: &blank_dev_deps,
    };

    pub const react = DependencyGroup{
        dependencies: &react_deps,
        devDependencies: &react_dev_deps,
    };

    pub const tailwind = DependencyGroup{
        dependencies: &tailwind_deps,
        devDependencies: &tailwind_dev_deps,
    };

    pub const shadcn = DependencyGroup{
        dependencies: &shadcn_deps,
        devDependencies: &tailwind_dev_deps,
    };
}

pub enum ProjectTemplateChoice {
    blank,
    react,
    library,

    pub const @"default" = .blank;

    fn fmt(): []const u8 {
        return switch (_) {
            .blank => "<yellow>Blank<r>",
            .react => "<cyan>React<r>",
            .library => "<blue>Library<r>",
        };
    }
}

pub enum ReactTemplateChoice {
    @"default",
    tailwind,
    shadcn_tailwind,

    fn fmt(): []const u8 {
        return switch (_) {
            .@"default" => "<blue>Default (blank)<r>",
            .tailwind => "<magenta>TailwindCSS<r>",
            .shadcn_tailwind => "<green>Shadcn + TailwindCSS<r>",
        };
    }
}

pub struct TemplateFile {
    path: [:0]const u8,
    contents: [:0]const u8,
    can_skip_if_exists: bool = false,
}

const empty_template_files: []const TemplateFile = [];

const agent_rule = @embedFile("./init/rule.md");
const cursor_rule = TemplateFile{ path: ".cursor/rules/use-bun-instead-of-node-vite-npm-pnpm.mdc", contents: agent_rule };
const cursor_rule_path_to_claude_md = "../../CLAUDE.md";

const react_tailwind_scripts: [4]string = [
    "dev", "bun './**/*.html'",
    "build", "bun 'REPLACE_ME_WITH_YOUR_APP_FILE_NAME.build.ts'",
];
const react_blank_scripts: [6]string = [
    "dev",
    "bun --hot .",
    "static",
    "bun build ./src/index.html --outdir=dist --sourcemap --target=browser --minify --define:process.env.NODE_ENV='\"production\"' --env='BUN_PUBLIC_*'",
    "build",
    "NODE_ENV=production bun .",
];
const empty_scripts: []const string = [];

enum Template {
    blank,
    react_blank,
    react_tailwind,
    react_tailwind_shadcn,
    typescript_library,

    fn shouldUseSourceFileProjectGenerator(): bool {
        return switch (_) {
            .blank, .typescript_library => false,
            else => true,
        };
    }
    fn isReact(): bool {
        return switch (_) {
            .react_blank, .react_tailwind, .react_tailwind_shadcn => true,
            else => false,
        };
    }
    fn writeToPackageJson(alloc: std.mem.Allocator, fields: *InitCommand.PackageJSONFields)! {
        const Rope = js_ast.E.Object.Rope;
        fields.name = _.templateName();
        const key = alloc.create(Rope)?;
        key.* = Rope{
            head: js_ast.Expr.init(js_ast.E.String, js_ast.E.String{ data: "scripts" }, logger.Loc.Empty),
            next: null,
        };
        let scripts_json = fields.object.getOrPutObject(key, alloc)?;
        const the_scripts = _.scripts();
        let i: usize = 0;
        while (i < the_scripts.len) : (i += 2) {
            const script_name = the_scripts[i];
            const script_command = the_scripts[i + 1];

            scripts_json.data.e_object.putString(alloc, script_name, script_command)?;
        }
    }
    fn dependencies(): DependencyGroup {
        return switch (_) {
            .blank => DependencyGroup.blank,
            .react_blank => DependencyGroup.react,
            .react_tailwind => DependencyGroup.tailwind,
            .react_tailwind_shadcn => DependencyGroup.shadcn,
            .typescript_library => DependencyGroup.blank,
        };
    }
    fn templateName(): []const u8 {
        return switch (_) {
            .blank => "bun-blank-template",
            .typescript_library => "bun-typescript-library-template",
            .react_blank => "bun-react-template",
            .react_tailwind => "bun-react-tailwind-template",
            .react_tailwind_shadcn => "bun-react-tailwind-shadcn-template",
        };
    }
    fn scripts(): []const string {
        return switch (_) {
            .blank, .typescript_library => empty_scripts,
            .react_tailwind, .react_tailwind_shadcn => &react_tailwind_scripts,
            .react_blank => &react_blank_scripts,
        };
    }

    static fn isClaudeCodeInstalled(): bool {
        if (Environment.isWindows) {
            // Claude code is not available on Windows, at the time of writing.
            return false;
        }

        // Give some way to opt out.
        if (bun.env_var.BUN_AGENT_RULE_DISABLED.get() || bun.env_var.CLAUDE_CODE_AGENT_RULE_DISABLED.get()) {
            return false;
        }

        const pathbuffer = bun.path_buffer_pool.get();
        let pathbuffer_guard = PathBufferGuard<@TypeOf(pathbuffer)>{ buf: pathbuffer };

        return bun.which(pathbuffer, bun.env_var.PATH.get() ?? return false, bun.fs.FileSystem.instance.top_level_dir, "claude") != null;
    }

    static fn createAgentRule() {
        let create_claude_md = Template.isClaudeCodeInstalled() &&
            // Never overwrite CLAUDE.md
            !bun.sys.exists("CLAUDE.md");

        if (template_file := Template.getCursorRule()) {
            let did_create_agent_rule = false;

            // If both Cursor & Claude is installed, make the cursor rule a
            // symlink to ../../CLAUDE.md
            const asset_path = if (create_claude_md) "CLAUDE.md" else template_file.path;
            const result = InitCommand.Assets.createNew(asset_path, template_file.contents);
            did_create_agent_rule = true;
            result catch {
                did_create_agent_rule = false;
                if (create_claude_md) {
                    create_claude_md = false;
                    // If installing the CLAUDE.md fails for some reason, fall back to installing the cursor rule.
                    * = InitCommand.Assets.createNew(template_file.path, template_file.contents) catch {};
                }
            };

            if (comptime !Environment.isWindows) {
                // if we did create the CLAUDE.md, then symlinks the
                // .cursor/rules/*.mdc -> CLAUDE.md so it's easier to keep them in
                // sync if you change it locally. we use a symlink for the cursor
                // rule in this case so that the github UI for CLAUDE.md (which may
                // appear prominently in repos) doesn't show a file path.
                if (did_create_agent_rule && create_claude_md) symlink_cursor_rule: {
                    create_claude_md = false;
                    bun.makePath(bun.FD.cwd().stdDir(), ".cursor/rules") catch {};
                    bun.sys.symlinkat(cursor_rule_path_to_claude_md, .cwd(), template_file.path).unwrap() catch break :symlink_cursor_rule;
                    Output.prettyln(" + <r><d>{s} -\\> {s}<r>", .{ template_file.path, asset_path });
                    Output.flush();
                }
            }
        }

        // If cursor is not installed but claude code is installed, then create the CLAUDE.md.
        if (create_claude_md) {
            // In this case, the frontmatter from the cursor rule is not helpful so let's trim it out.
            let end_of_frontmatter: usize = 0;
            if (start := bun.strings.lastIndexOf(agent_rule, "---\n")) {
                end_of_frontmatter = start + "---\n".len;
            }

            * = InitCommand.Assets.createNew("CLAUDE.md", agent_rule[end_of_frontmatter..]) catch {};
        }
    }

    static fn isCursorInstalled(): bool {
        // Give some way to opt-out.
        if (bun.env_var.BUN_AGENT_RULE_DISABLED.get() || bun.env_var.CURSOR_AGENT_RULE_DISABLED.get()) {
            return false;
        }

        // Detect if they're currently using cursor.
        if (bun.env_var.CURSOR_TRACE_ID.get()) {
            return true;
        }

        if (Environment.isMac) {
            if (bun.sys.exists("/Applications/Cursor.app")) {
                return true;
            }
        }

        if (Environment.isWindows) {
            if (user := bun.getenvZAnyCase("USER")) {
                const pathbuf = bun.path_buffer_pool.get();
                let pathbuf_guard = PathBufferGuard<@TypeOf(pathbuf)>{ buf: pathbuf };
                const path = std.fmt.bufPrintZ(pathbuf, "C:\\Users\\{s}\\AppData\\Local\\Programs\\Cursor\\Cursor.exe", { user}) catch {
                    return false;
                };

                if (bun.sys.exists(path)) {
                    return true;
                }
            }
        }

        return false;
    }
    static fn getCursorRule(): (*const TemplateFile)? {
        if (Template.isCursorInstalled()) {
            return &cursor_rule;
        }

        return null;
    }

    fn files(): []const TemplateFile {
        return switch (_) {
            .react_blank => &react_blank_files,
            .react_tailwind => &react_tailwind_files,
            .react_tailwind_shadcn => &react_shadcn_files,
            else => empty_template_files,
        };
    }

    pub fn @"write files and run `bun dev`"(allocator: std.mem.Allocator)! {
        Template.createAgentRule();

        switch (_) {
            inline else => |tpl| {
                inline for (file of comptime tpl.files()) {
                    const path = file.path;
                    const contents = file.contents;

                    const result = if (comptime strings.eqlComptime(path, "README.md"))
                        InitCommand.Assets.createWithContents("README.md", contents, .{
                            name: tpl.templateName(),
                            bunVersion: Environment.version_string,
                        })
                    else
                        InitCommand.Assets.createNew(path, contents);
                    result catch e => {
                        if (e == error.EEXIST) {
                            Output.prettyln(" ○ <r><yellow>{s}<r> (already exists, skipping)", .{ path });
                            Output.flush();
                        } else {
                            Output.err(e, "failed to create file: '{s}'", .{ path });
                            Global.crash();
                        }
                    };
                }
            },
        }

        Output.pretty("\n", .{});
        Output.flush();

        const self_exe = bun.selfExePath()?;
        const install_args: [2]string = [self_exe, "install"];
        let install = std.process.Child.init(
            &install_args,
            allocator,
        );
        install.stderr_behavior = .Inherit;
        install.stdin_behavior = .Ignore;
        install.stdout_behavior = .Inherit;

        * = install.spawnAndWait()?;

        Output.prettyln(
            "\n✨ New project configured!\n\n<b><cyan>Development<r><d> - full-stack dev server with hot reload<r>\n\n    <cyan><b>bun dev<r>\n\n<b><yellow>Static Site<r><d> - build optimized assets to disk (no backend)<r>\n\n    <yellow><b>bun run build<r>\n\n<b><green>Production<r><d> - serve a full-stack production build<r>\n\n    <green><b>bun start<r>\n\n<blue>Happy bunning! 🐇<r>\n",
            .{},
        );

        Output.flush();
    }
}

const react_blank_files: [14]TemplateFile = [
    TemplateFile{ path: "bunfig.toml", contents: @embedFile("./init/react-app/bunfig.toml") },
    TemplateFile{ path: "package.json", contents: @embedFile("./init/react-app/package.json") },
    TemplateFile{ path: "tsconfig.json", contents: @embedFile("./init/react-app/tsconfig.json") },
    TemplateFile{ path: "bun-env.d.ts", contents: @embedFile("./init/react-app/bun-env.d.ts") },
    TemplateFile{ path: "README.md", contents: InitCommand.Assets.@"README2.md" },
    TemplateFile{ path: ".gitignore", contents: InitCommand.Assets.@".gitignore", can_skip_if_exists: true },
    TemplateFile{ path: "src/index.ts", contents: @embedFile("./init/react-app/src/index.ts") },
    TemplateFile{ path: "src/App.tsx", contents: @embedFile("./init/react-app/src/App.tsx") },
    TemplateFile{ path: "src/index.html", contents: @embedFile("./init/react-app/src/index.html") },
    TemplateFile{ path: "src/index.css", contents: @embedFile("./init/react-app/src/index.css") },
    TemplateFile{ path: "src/APITester.tsx", contents: @embedFile("./init/react-app/src/APITester.tsx") },
    TemplateFile{ path: "src/react.svg", contents: @embedFile("./init/react-app/src/react.svg") },
    TemplateFile{ path: "src/frontend.tsx", contents: @embedFile("./init/react-app/src/frontend.tsx") },
    TemplateFile{ path: "src/logo.svg", contents: @embedFile("./init/react-app/src/logo.svg") },
];

const react_tailwind_files: [15]TemplateFile = [
    TemplateFile{ path: "bunfig.toml", contents: @embedFile("./init/react-tailwind/bunfig.toml") },
    TemplateFile{ path: "package.json", contents: @embedFile("./init/react-tailwind/package.json") },
    TemplateFile{ path: "tsconfig.json", contents: @embedFile("./init/react-tailwind/tsconfig.json") },
    TemplateFile{ path: "bun-env.d.ts", contents: @embedFile("./init/react-tailwind/bun-env.d.ts") },
    TemplateFile{ path: "README.md", contents: InitCommand.Assets.@"README2.md" },
    TemplateFile{ path: ".gitignore", contents: InitCommand.Assets.@".gitignore", can_skip_if_exists: true },
    TemplateFile{ path: "src/index.ts", contents: @embedFile("./init/react-tailwind/src/index.ts") },
    TemplateFile{ path: "src/App.tsx", contents: @embedFile("./init/react-tailwind/src/App.tsx") },
    TemplateFile{ path: "src/index.html", contents: @embedFile("./init/react-tailwind/src/index.html") },
    TemplateFile{ path: "src/index.css", contents: @embedFile("./init/react-tailwind/src/index.css") },
    TemplateFile{ path: "src/APITester.tsx", contents: @embedFile("./init/react-tailwind/src/APITester.tsx") },
    TemplateFile{ path: "src/react.svg", contents: @embedFile("./init/react-tailwind/src/react.svg") },
    TemplateFile{ path: "src/frontend.tsx", contents: @embedFile("./init/react-tailwind/src/frontend.tsx") },
    TemplateFile{ path: "src/logo.svg", contents: @embedFile("./init/react-tailwind/src/logo.svg") },
    TemplateFile{ path: "build.ts", contents: @embedFile("./init/react-tailwind/build.ts") },
];

const react_shadcn_files: [24]TemplateFile = [
    TemplateFile{ path: "bunfig.toml", contents: @embedFile("./init/react-shadcn/bunfig.toml") },
    TemplateFile{ path: "styles/globals.css", contents: @embedFile("./init/react-shadcn/styles/globals.css") },
    TemplateFile{ path: "package.json", contents: @embedFile("./init/react-shadcn/package.json") },
    TemplateFile{ path: "components.json", contents: @embedFile("./init/react-shadcn/components.json") },
    TemplateFile{ path: "tsconfig.json", contents: @embedFile("./init/react-shadcn/tsconfig.json") },
    TemplateFile{ path: "bun-env.d.ts", contents: @embedFile("./init/react-shadcn/bun-env.d.ts") },
    TemplateFile{ path: "README.md", contents: InitCommand.Assets.@"README2.md" },
    TemplateFile{ path: ".gitignore", contents: InitCommand.Assets.@".gitignore", can_skip_if_exists: true },
    TemplateFile{ path: "src/index.ts", contents: @embedFile("./init/react-shadcn/src/index.ts") },
    TemplateFile{ path: "src/App.tsx", contents: @embedFile("./init/react-shadcn/src/App.tsx") },
    TemplateFile{ path: "src/index.html", contents: @embedFile("./init/react-shadcn/src/index.html") },
    TemplateFile{ path: "src/index.css", contents: @embedFile("./init/react-shadcn/src/index.css") },
    TemplateFile{ path: "src/components/ui/card.tsx", contents: @embedFile("./init/react-shadcn/src/components/ui/card.tsx") },
    TemplateFile{ path: "src/components/ui/label.tsx", contents: @embedFile("./init/react-shadcn/src/components/ui/label.tsx") },
    TemplateFile{ path: "src/components/ui/button.tsx", contents: @embedFile("./init/react-shadcn/src/components/ui/button.tsx") },
    TemplateFile{ path: "src/components/ui/select.tsx", contents: @embedFile("./init/react-shadcn/src/components/ui/select.tsx") },
    TemplateFile{ path: "src/components/ui/input.tsx", contents: @embedFile("./init/react-shadcn/src/components/ui/input.tsx") },
    TemplateFile{ path: "src/components/ui/textarea.tsx", contents: @embedFile("./init/react-shadcn/src/components/ui/textarea.tsx") },
    TemplateFile{ path: "src/APITester.tsx", contents: @embedFile("./init/react-shadcn/src/APITester.tsx") },
    TemplateFile{ path: "src/lib/utils.ts", contents: @embedFile("./init/react-shadcn/src/lib/utils.ts") },
    TemplateFile{ path: "src/react.svg", contents: @embedFile("./init/react-shadcn/src/react.svg") },
    TemplateFile{ path: "src/frontend.tsx", contents: @embedFile("./init/react-shadcn/src/frontend.tsx") },
    TemplateFile{ path: "src/logo.svg", contents: @embedFile("./init/react-shadcn/src/logo.svg") },
    TemplateFile{ path: "build.ts", contents: @embedFile("./init/react-shadcn/build.ts") },
];

import CLI from "./cli.zig";
import Fs from "../resolver/fs.zig";
import options from "../bundler/options.zig";
import create_command from "./create_command.zig";
const initializeStore = create_command.initializeStore;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const JSON = bun.json;
const JSPrinter = bun.js_printer;
const MutableString = bun.MutableString;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;
const logger = bun.logger;
const strings = bun.strings;

const exists = bun.sys.exists;
const existsZ = bun.sys.existsZ;
