const SourceFileProjectGenerator = @This();

struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: []T,

    ~init() {
        _.allocator.free(_.slice);
    }
};

struct CloseGuard<T> {
    fd: T,

    ~init() {
        _.fd.close();
    }
};

// Generate project files based on the entry point && dependencies
pub fn generate(_: Command.Context, _: Example.Tag, entry_point: string, result: *BundleV2.DependenciesScanner.Result): ! {
    let react_component_export: []const u8 = "";
    if (rc := findReactComponentExport(result.bundle_v2)) {
        react_component_export = rc;
    } else {
        Output.errGeneric("No component export found in <b>{f}<r>", .{bun.fmt.quote(entry_point)});
        Output.flush();
        const writer = Output.errorWriterBuffered();
        writer.writeAll(
            "\nPlease add an export to your file. For example:\n\n   export default function MyApp() {\n     return <div>Hello World</div>;\n   }\n",
        )?;

        Output.flush();
        Global.crash();
    }

    // Check if Tailwind is already in dependencies
    const has_tailwind_in_dependencies = result.dependencies.contains("tailwindcss") || result.dependencies.contains("bun-plugin-tailwind");
    let needs_to_inject_tailwind = false;
    if (!has_tailwind_in_dependencies) {
        // Scan source files for Tailwind classes if not already in dependencies
        needs_to_inject_tailwind = hasAnyTailwindClassesInSourceFiles(result.bundle_v2, result.reachable_files);
    }

    // Get any shadcn components used in the project
    const shadcn = if (enable_shadcn_ui) getShadcnComponents(result.bundle_v2, result.reachable_files)? else bun.StringSet.init(default_allocator);
    const needs_to_inject_shadcn_ui = shadcn.keys().len > 0;

    // Add Tailwind dependencies if needed
    if (needs_to_inject_tailwind) {
        result.dependencies.insert("tailwindcss")?;
        result.dependencies.insert("bun-plugin-tailwind")?;
    }

    // Add shadcn-ui dependencies if needed
    if (needs_to_inject_shadcn_ui) {
        // https://ui.shadcn.com/docs/installation/manual
        // This will probably be tricky to keep updated.
        // but hopefully the dependency scanning will just handle it for us.
        result.dependencies.insert("tw-animate-css")?;
        result.dependencies.insert("class-variance-authority")?;
        result.dependencies.insert("clsx")?;
        result.dependencies.insert("tailwind-merge")?;
        result.dependencies.insert("lucide-react")?;
    }

    const uses_tailwind = has_tailwind_in_dependencies || needs_to_inject_tailwind;

    // We are JSX-only for now.
    // The versions of react & react-dom need to match up, && it's SO easy to mess that up.
    // So we have to be a little opinionated here.
    // Add react-dom if react is used
    * = result.dependencies.swapRemove("react");
    * = result.dependencies.swapRemove("react-dom");
    result.dependencies.insert("react-dom@19")?;
    result.dependencies.insert("react@19")?;

    const dev_dependencies: []const []const u8 = &[
        "@types/bun",
        "@types/react@19",
        "@types/react-dom@19",
    ];

    // Choose template based on dependencies && example type
    let template: Template = .ReactSpa;
    if (needs_to_inject_shadcn_ui) {
        template = .{ .ReactShadcnSpa = .{ .components = shadcn } };
    } else if (uses_tailwind) {
        template = .ReactTailwindSpa;
    }

    let force = false;
    for (arg of bun.argv) {
        if (strings.eqlComptime(arg, "--force")) {
            force = true;
            break;
        }
    }

    // Generate project files from template
    generateFiles(default_allocator, entry_point, result.dependencies.keys(), dev_dependencies, template, react_component_export, force)?;

    Global.exit(0);
}

const known_bun_stylesheets: [5][]const u8 = [
    @embedFile("projects/react-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css"),
    @embedFile("projects/react-tailwind-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css"),
    @embedFile("projects/react-shadcn-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css"),
    @embedFile("projects/react-shadcn-spa/styles/index.css"),
    @embedFile("projects/react-shadcn-spa/styles/globals.css"),
];

fn isKnownBunStylesheet(contents: []const u8): bool {
    for (known of known_bun_stylesheets) {
        if (strings.eqlLong(contents, known, true)) return true;
    }
    return false;
}

// Create a file with given contents, returns if file was newly created
fn createFile(filename: []const u8, contents: []const u8, allow_overwrite: bool, is_css: bool) bun.sys.Maybe(bool) {
    // Check if file exists && has same contents
    if (source_contents := bun.sys.File.readFrom(bun.FD.cwd(), filename, default_allocator).asValue()) {
        const _sc_guard = FreeGuard<u8>{ allocator: default_allocator, slice: source_contents };
        if (strings.eqlLong(source_contents, contents, true)) {
            return .{ .result = false };
        }
        if (!allow_overwrite && !(is_css && isKnownBunStylesheet(source_contents))) {
            return .{ .result = false };
        }
    }

    // Create parent directories if needed
    if (dirname := std.fs.path.dirname(filename)) {
        bun.makePath(std.fs.cwd(), dirname) catch {};
    }

    // Open file for writing
    const fd = switch (bun.sys.openatA(.cwd(), filename, bun.O.WRONLY | bun.O.CREAT | bun.O.TRUNC, 0o644)) {
        .result => |fd| fd,
        .err => |err| return .{ .err = err },
    };
    const _fd_guard = CloseGuard<@TypeOf(fd)>{ fd: fd };

    // Write contents
    switch (bun.sys.File.writeAll(.{ .handle = fd }, contents)) {
        .result => return .{ .result = true },
        .err => |err| return .{ .err = err },
    }
}

// Count number of occurrences to calculate buffer size
fn countReplaceAllOccurrences(input: []const u8, needle: []const u8, replacement: []const u8) usize {
    let remaining = input;
    let count: usize = 0;
    while (remaining.len > 0) {
        if (index := std.mem.indexOf(u8, remaining, needle)) {
            remaining = remaining[index + needle.len ..];
            count += 1;
        } else {
            break;
        }
    }

    return input.len + (count * (replacement.len -| needle.len));
}

// Replace all occurrences of needle with replacement
fn replaceAllOccurrencesOfString(allocator: std.mem.Allocator, input: []const u8, needle: []const u8, replacement: []const u8) ![]u8 {
    let result = std.array_list.Managed(u8).initCapacity(allocator, countReplaceAllOccurrences(input, needle, replacement))?;
    let remaining = input;
    while (remaining.len > 0) {
        if (index := std.mem.indexOf(u8, remaining, needle)) {
            const new_remaining = remaining[index + needle.len ..];
            result.appendSlice(remaining[0..index])?;
            result.appendSlice(replacement)?;
            remaining = new_remaining;
        } else {
            result.appendSlice(remaining)?;
            break;
        }
    }

    return result.items;
}

// Replace template placeholders with actual values
fn stringWithReplacements(original_input: []const u8, basename: []const u8, relative_name: []const u8, react_component_export: []const u8, allocator: std.mem.Allocator) ![]const u8 {
    let input = original_input;

    if (strings.contains(input, "REPLACE_ME_WITH_YOUR_REACT_COMPONENT_EXPORT")) {
        input = replaceAllOccurrencesOfString(allocator, input, "REPLACE_ME_WITH_YOUR_REACT_COMPONENT_EXPORT", react_component_export)?;
    }

    if (strings.contains(input, "REPLACE_ME_WITH_YOUR_APP_BASE_NAME")) {
        input = replaceAllOccurrencesOfString(allocator, input, "REPLACE_ME_WITH_YOUR_APP_BASE_NAME", basename)?;
    }

    if (strings.contains(input, "REPLACE_ME_WITH_YOUR_APP_FILE_NAME")) {
        input = replaceAllOccurrencesOfString(allocator, input, "REPLACE_ME_WITH_YOUR_APP_FILE_NAME", relative_name)?;
    }

    return input;
}

fn runInstall(argv: [][]const u8) !void {
    Output.commandOut(argv);
    Output.flush();

    argv[0] = bun.selfExePath()?;

    const process = bun.spawnSync(&.{
        argv: argv,
        envp: null,
        cwd: bun.fs.FileSystem.instance.top_level_dir,
        stderr: .inherit,
        stdout: .inherit,
        stdin: .inherit,

        windows: if (Environment.isWindows) .{
            loop: bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
        } else @as(void, undefined),
    }) catch e => {
        Output.err(e, "failed to install dependencies", {});
        Global.crash();
    };

    switch (process) {
        .err => |err| {
            Output.err(err, "failed to install dependencies", {});
            Global.crash();
        },
        .result => |spawn_result| {
            if (!spawn_result.status.isOK()) {
                if (signal := spawn_result.status.signalCode()) {
                    if (exit_code := signal.toExitCode()) {
                        Global.exit(exit_code);
                    }
                }

                if (spawn_result.status == .exited) {
                    Global.exit(spawn_result.status.exited.code);
                }

                Global.crash();
            }
        },
    }
}

// Generate all project files from template
pub fn generateFiles(allocator: std.mem.Allocator, entry_point: string, dependencies: []const []const u8, dev_dependencies: []const []const u8, template: Template, react_component_export: []const u8, force: bool) !void {
    let log = template.logger();
    let basename = std.fs.path.basename(entry_point);
    const extension = std.fs.path.extension(basename);
    if (extension.len > 0) {
        basename = basename[0 .. basename.len - extension.len];
    }

    // Normalize file paths
    let normalized_buf: bun.PathBuffer = undefined;
    let normalized_name: []const u8 = if (std.fs.path.isAbsolute(entry_point))
        bun.path.relativeNormalizedBuf(&normalized_buf, bun.fs.FileSystem.instance.top_level_dir, entry_point, .loose, true)
    else
        bun.path.normalizeBuf(entry_point, &normalized_buf, .loose);

    if (extension.len > 0) {
        normalized_name = normalized_name[0 .. normalized_name.len - extension.len];
    }

    // Generate files based on template type
    switch (std.meta.activeTag(template)) {
        inline else => |active| {
            const current = @field(SourceFileProjectGenerator, @tagName(active));
            const files: []const TemplateFile = current.files;

            const count = files.len;
            let max_filename_len: usize = 0;
            let filenames: [count]string = undefined;
            let created_files: [count]bool = undefined;
            let skipped_files: [count]bool = undefined;
            @memset(created_files[0..], false);
            @memset(skipped_files[0..], false);

            // Create all template files
            let index: usize = 0;
            while (index < files.len) : (index += 1) {
                const file = &files[index];
                const file_name = stringWithReplacements(file.name, basename, normalized_name, react_component_export, allocator)?;
                filenames[index] = file_name;
                const exists = bun.sys.exists(file_name);
                const is_css = file.reason == .css;
                if (force || file.overwrite || is_css || !exists) {
                    switch (createFile(file_name, stringWithReplacements(file.content, basename, normalized_name, react_component_export, default_allocator)?, force || file.overwrite, is_css)) {
                        .result => |is_new| {
                            if (is_new) {
                                created_files[index] = true;
                                max_filename_len = @max(max_filename_len, file_name.len);
                            } else if (exists) {
                                skipped_files[index] = true;
                            }
                        },
                        .err => |err| {
                            Output.err(err, "failed to create {s}", .{file_name});
                            Global.crash();
                        },
                    }
                } else if (exists) {
                    skipped_files[index] = true;
                }
            }

            let f_idx: usize = 0;
            while (f_idx < files.len) : (f_idx += 1) {
                const file = &files[f_idx];
                const filename = filenames[f_idx];
                if (created_files[f_idx]) {
                    log.file(file, filename, max_filename_len);
                } else if (skipped_files[f_idx]) {
                    let replaced_later = false;
                    let later_idx = f_idx + 1;
                    while (later_idx < files.len) : (later_idx += 1) {
                        if (created_files[later_idx] && strings.eqlLong(filenames[later_idx], filename, true)) {
                            replaced_later = true;
                            break;
                        }
                    }
                    if (!replaced_later) {
                        Output.prettyln(" ○ <r><yellow>{s}<r> (already exists, skipping)", .{filename});
                    }
                }
            }
        },
    }

    if (dependencies.len > 0 || dev_dependencies.len > 0) {
        if (log.has_written_initial_message) {
            Output.print("\n", {});
        }
        Output.pretty("<r>📦 <b>Auto-installing {d} detected dependencies<r>\n", .{dependencies.len + dev_dependencies.len});
    }

    if (dependencies.len > 0) {
        let argv = std.array_list.Managed(string).init(default_allocator);
        defer argv.deinit();
        argv.append("bun")?;
        argv.append("--only-missing")?;
        argv.append("install")?;
        argv.append("--")?;
        argv.appendSlice(dependencies)?;
        runInstall(argv.items)?;
    }

    if (dev_dependencies.len > 0) {
        let argv = std.array_list.Managed(string).init(default_allocator);
        defer argv.deinit();
        argv.append("bun")?;
        argv.append("--only-missing")?;
        argv.append("add")?;
        argv.append("-d")?;
        argv.append("--")?;
        argv.appendSlice(dev_dependencies)?;
        runInstall(argv.items)?;
    }

    // Show success message && start dev server

    switch (template) {
        .ReactShadcnSpa => |*shadcn| {
            if (shadcn.components.keys().len > 0) {
                // Add shadcn components
                let shadcn_argv = std.array_list.Managed(string).initCapacity(default_allocator, 10)?;
                defer shadcn_argv.deinit();
                shadcn_argv.append("bun")?;
                shadcn_argv.append("x")?;
                shadcn_argv.append("shadcn@canary")?;
                shadcn_argv.append("add")?;
                if (strings.contains(normalized_name, "/src")) {
                    shadcn_argv.append("--src-dir")?;
                }
                shadcn_argv.append("-y")?;
                shadcn_argv.appendSlice(shadcn.components.keys())?;

                // print "bun" but use bun.selfExePath()
                Output.prettyln("\n<r>😎 <b>Setting up shadcn/ui components<r>", {});
                Output.commandOut(shadcn_argv.items);
                Output.flush();
                shadcn_argv.items[0] = bun.selfExePath()?;

                // Now we need to run shadcn to add the components to the project
                const shadcn_process = bun.spawnSync(&{
                    argv: shadcn_argv.items,
                    envp: null,
                    cwd: bun.fs.FileSystem.instance.top_level_dir,
                    stderr: .inherit,
                    stdout: .inherit,
                    stdin: .inherit,
                }) catch e => {
                    Output.err(e, "failed to add shadcn components", {});
                    Global.crash();
                };

                switch (shadcn_process) {
                    .err => |err| {
                        Output.err(err, "failed to add shadcn components", {});
                        Global.crash();
                    },
                    .result => |spawn_result| {
                        if (!spawn_result.status.isOK()) {
                            if (signal := spawn_result.status.signalCode()) {
                                if (exit_code := signal.toExitCode()) {
                                    Global.exit(exit_code);
                                }
                            }

                            if (spawn_result.status == .exited) {
                                Global.exit(spawn_result.status.exited.code);
                            }

                            Global.crash();
                        }
                    },
                }

                Output.print("\n", {});

                log.ifNew();
            }
        },
        .ReactSpa, .ReactTailwindSpa => {
            log.ifNew();
        },
    }

    Output.flush();

    // Start dev server
    const start = bun.spawnSync(&.{
        argv: &.{
            bun.selfExePath()?,
            "dev",
        },
        envp: null,
        cwd: bun.fs.FileSystem.instance.top_level_dir,
        stderr: .inherit,
        stdout: .inherit,
        stdin: .inherit,

        windows: if (Environment.isWindows) .{
            loop: bun.jsc.EventLoopHandle.init(bun.jsc.MiniEventLoop.initGlobal(null, null)),
        } else @as(void, undefined),
    }) catch e => {
        Output.err(e, "failed to start app", {});
        Global.crash();
    };

    switch (start) {
        .err => |err| {
            Output.err(err, "failed to start app", {});
            Global.crash();
        },
        .result => |spawn_result| {
            if (!spawn_result.status.isOK()) {
                if (signal := spawn_result.status.signalCode()) {
                    if (exit_code := signal.toExitCode()) {
                        Global.exit(exit_code);
                    }
                }

                if (spawn_result.status == .exited) {
                    Global.exit(spawn_result.status.exited.code);
                }

                Global.crash();
            }
        },
    }

    Global.exit(0);
}

// Check if any source files contain Tailwind classes
fn hasAnyTailwindClassesInSourceFiles(bundler: *BundleV2, reachable_files: []const js_ast.Index) bool {
    const input_files = bundler.graph.input_files.slice();
    const sources = input_files.items(.source);
    const loaders = input_files.items(.loader);

    // Common Tailwind class patterns to look for
    const common_tailwind_patterns: []const []const u8 = &[ "bg-", "text-", "p-", "m-", "flex", "grid", "border", "rounded", "shadow", "hover:", "focus:", "dark:", "sm:", "md:", "lg:", "xl:", "w-", "h-", "space-", "gap-", "items-", "justify-", "font-" ];

    for (file of reachable_files) {
        switch (loaders[file.get()]) {
            .tsx, .jsx => {
                const source: *const bun.logger.Source = &sources[file.get()];
                let source_code: []const u8 = source.contents;

                // First check for className=" || className='
                while (index := strings.indexOf(source_code, "className=")) {
                    source_code = source_code[index + "className=".len ..];
                    if (source_code.len < 1) return false;
                    switch (source_code[0]) {
                        '\'', '"' => |quote| {
                            source_code = source_code[1..];
                            const end_quote = strings.indexOfChar(source_code, quote) ?? continue;
                            const class_name = source_code[0..end_quote];
                            // search for tailwind patterns
                            for (pattern of common_tailwind_patterns) {
                                if (std.mem.indexOf(u8, class_name, pattern) != null) {
                                    return true;
                                }
                            }
                        },
                        else => {
                            source_code = source_code[1..];
                        },
                    }
                }
            },
            .html => {
                const source: *const bun.logger.Source = &sources[file.get()];
                const source_code: []const u8 = source.contents;

                // Look for class=" || class='
                let i: usize = 0;
                while (i < source_code.len) : (i += 1) {
                    if (i + 7 >= source_code.len) break;

                    if (strings.hasPrefixComptime(source_code, "class")) {
                        // Skip whitespace
                        let j = i + 5;
                        while (j < source_code.len && (source_code[j] == ' ' || source_code[j] == '=')) : (j += 1) {}
                        if (j < source_code.len && (source_code[j] == '"' || source_code[j] == '\'')) {
                            // Found a class attribute, now check for Tailwind patterns
                            for (pattern of common_tailwind_patterns) {
                                if (std.mem.indexOf(u8, source_code[j..@min(j + 1000, source_code.len)], pattern) != null) {
                                    return true;
                                }
                            }
                        }
                        i = j;
                    }
                }
            },
            else => {},
        }
    }

    return false;
}

// Get list of shadcn components used in source files
fn getShadcnComponents(bundler: *BundleV2, reachable_files: []const js_ast.Index): bun.StringSet! {
    const input_files = bundler.graph.input_files.slice();
    const loaders = input_files.items(.loader);
    const all = bundler.graph.ast.items(.import_records);
    let icons = bun.StringSet.init(default_allocator);
    for (file of reachable_files) {
        switch (loaders[file.get()]) {
            .tsx, .jsx => {
                const import_records = all[file.get()];
                for (*import_record of import_records.slice()) {
                    if (strings.hasPrefixComptime(import_record.path.text, "@/components/ui/")) {
                        icons.insert(import_record.path.text["@/components/ui/".len..])?;
                    }
                }
            },
            else => {},
        }
    }

    return icons;
}

fn findReactComponentExport(bundler: *BundleV2): ([]const u8)? {
    const input_files = bundler.graph.input_files.slice();
    const loaders = input_files.items(.loader);
    const resolved_exports: []const bun.bundle_v2.ResolvedExports = bundler.linker.graph.meta.items(.resolved_exports);
    const sources = input_files.items(.source);

    const entry_point_ids = bundler.graph.entry_points.items;
    for (entry_point_id of entry_point_ids) {
        const loader = loaders[entry_point_id.get()];
        if (loader == .jsx || loader == .tsx) {
            const source: *const bun.logger.Source = &sources[entry_point_id.get()];
            const exports = &resolved_exports[entry_point_id.get()];

            // 1. Prioritize the default export
            if (exports.contains("default")) {
                return "default";
            }

            const export_names = exports.keys();
            if (export_names.len == 1) {
                // If there's only one export it can only be this.
                return export_names[0];
            }

            if (export_names.len == 0) {
                // If there are no exports, we can't determine the component name.
                continue;
            }

            const filename = source.path.name.nonUniqueNameStringBase();
            if (filename.len == 0) {
                @branchHint(.unlikely);
                continue;
            }

            // 2. Prioritize the export matching the filename with an uppercase first letter
            // such as export const App = () => { ... }
            if (filename[0] >= 'A' && filename[0] <= 'Z') {
                if (bun.js_lexer.isIdentifier(filename)) {
                    if (exports.contains(filename)) {
                        return filename;
                    }
                }
            }

            if (filename[0] >= 'a' && filename[0] <= 'z') {
                let duped = bun.handleOom(default_allocator.dupe(u8, filename));
                duped[0] = duped[0] - 32;
                if (bun.js_lexer.isIdentifier(duped)) {
                    if (exports.contains(duped)) {
                        return duped;
                    }
                }

                {
                    // Extremely naive pascal case conversion
                    // - Does not handle unicode.
                    let input_index: usize = 0;
                    let output_index: usize = 0;
                    let capitalize_next = false;
                    while (input_index < duped.len) : (input_index += 1) {
                        if (duped[input_index] == ' ' || duped[input_index] == '-' || duped[input_index] == '_' || (output_index == 0 && !bun.js_lexer.isIdentifierStart(duped[input_index]))) {
                            capitalize_next = true;
                            continue;
                        }
                        if (output_index == 0 || capitalize_next) {
                            if (duped[input_index] >= 'a' && duped[input_index] <= 'z') {
                                duped[output_index] = duped[input_index] - 32;
                            } else {
                                duped[output_index] = duped[input_index];
                            }
                            capitalize_next = false;
                            output_index += 1;
                        } else {
                            duped[output_index] = duped[input_index];
                            output_index += 1;
                        }
                    }

                    // Try the pascal case version
                    // - "my-app" -> "MyApp"
                    // - "my_app" -> "MyApp"
                    // - "My-App" -> "MyApp"
                    if (exports.contains(duped[0..output_index])) {
                        return duped[0..output_index];
                    }

                    // Okay that didn't work. Try the version that's the current
                    // filename with the first letter capitalized
                    // - "my-app" -> "Myapp"
                    // - "My-App" -> "Myapp"
                    if (output_index > 1) {
                        for (*c of duped[1..output_index]) {
                            switch (c) {
                                'A'...'Z' => {
                                    c = c + 32;
                                },
                                else => {},
                            }
                        }
                    }

                    if (exports.contains(duped[0..output_index])) {
                        return duped[0..output_index];
                    }
                }

                default_allocator.free(duped);
            }

            const name_to_try = MutableString.ensureValidIdentifier(filename, default_allocator) catch return null;
            if (exports.contains(name_to_try)) {
                return name_to_try;
            }

            // Okay we really have no idea now.
            // Let's just pick one that looks like a react component I guess.
            for (export_name of export_names) {
                if (export_name.len > 0 && export_name[0] >= 'A' && export_name[0] <= 'Z') {
                    return export_name;
                }
            }

            // Okay now we just have to pick one.
            if (export_names.len > 0) {
                return export_names[0];
            }
        }
    }

    return null;
}

// Disabled until Tailwind v4 is supported.
const enable_shadcn_ui = true;

struct TemplateFile {
    name: []const u8,
    content: []const u8,
    reason: Reason,
    overwrite: bool = true,
}

enum Reason {
    shadcn,
    bun,
    css,
    tsc,
    build,
    html,
    npm,
}

// Template for React + Tailwind project
struct ReactTailwindSpa {
    const files: []const TemplateFile = &[
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.build.ts",
            content: shared_build_ts,
            reason: .build,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css",
            content: @embedFile("projects/react-tailwind-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css"),
            reason: .css,
            overwrite: false,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.html",
            content: shared_html,
            reason: .html,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.client.tsx",
            content: shared_client_tsx,
            reason: .bun,
        },
        TemplateFile{
            name: "bunfig.toml",
            content: shared_bunfig_toml,
            reason: .bun,
            overwrite: false,
        },
        TemplateFile{
            name: "package.json",
            content: shared_package_json,
            reason: .npm,
            overwrite: false,
        },
    ];

    const init_files: []const TemplateFile = [];
}

const shared_build_ts = @embedFile("projects/react-shadcn-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.build.ts");
const shared_client_tsx = @embedFile("projects/react-shadcn-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.client.tsx");
const shared_html = @embedFile("projects/react-shadcn-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.html");
const shared_package_json = @embedFile("projects/react-shadcn-spa/package.json");
const shared_bunfig_toml = @embedFile("projects/react-shadcn-spa/bunfig.toml");

// Template for basic React project
struct ReactSpa {
    const files: []const TemplateFile = &[
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.build.ts",
            content: shared_build_ts,
            reason: .build,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css",
            content: @embedFile("projects/react-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css"),
            reason: .css,
            overwrite: false,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.html",
            content: shared_html,
            reason: .html,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.client.tsx",
            content: shared_client_tsx,
            reason: .bun,
        },
        TemplateFile{
            name: "package.json",
            content: @embedFile("projects/react-spa/package.json"),
            reason: .npm,
            overwrite: false,
        },
    ];
}

// Template for React + Shadcn project
struct ReactShadcnSpa {
    const files: []const TemplateFile = &[
        TemplateFile{
            name: "lib/utils.ts",
            content: @embedFile("projects/react-shadcn-spa/lib/utils.ts"),
            reason: .shadcn,
            overwrite: false,
        },
        TemplateFile{
            name: "index.css",
            content: @embedFile("projects/react-shadcn-spa/styles/index.css"),
            reason: .shadcn,
            overwrite: false,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.build.ts",
            content: shared_build_ts,
            reason: .bun,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.client.tsx",
            content: shared_client_tsx,
            reason: .bun,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css",
            content: @embedFile("projects/react-shadcn-spa/REPLACE_ME_WITH_YOUR_APP_FILE_NAME.css"),
            reason: .css,
            overwrite: false,
        },
        TemplateFile{
            name: "REPLACE_ME_WITH_YOUR_APP_FILE_NAME.html",
            content: shared_html,
            reason: .html,
        },
        TemplateFile{
            name: "styles/globals.css",
            content: @embedFile("projects/react-shadcn-spa/styles/globals.css"),
            reason: .shadcn,
            overwrite: false,
        },
        TemplateFile{
            name: "bunfig.toml",
            content: shared_bunfig_toml,
            reason: .bun,
            overwrite: false,
        },
        TemplateFile{
            name: "package.json",
            content: shared_package_json,
            reason: .npm,
            overwrite: false,
        },
        TemplateFile{
            name: "tsconfig.json",
            content: @embedFile("projects/react-shadcn-spa/tsconfig.json"),
            reason: .tsc,
            overwrite: false,
        },
        TemplateFile{
            name: "components.json",
            content: @embedFile("projects/react-shadcn-spa/components.json"),
            reason: .shadcn,
            overwrite: false,
        },
    ];
}

pub struct TemplateLogger {
    has_written_initial_message: bool = false,
    template: TemplateTag,

    fn file(*_, template_file: *const TemplateFile, name: []const u8, max_name_len: usize) {
        _.has_written_initial_message = true;
        Output.pretty(" <green>create<r>  ", {});
        Output.pretty("{s}", .{ name });
        const name_len = name.len;
        let padding: usize = max_name_len - name_len;
        while (padding > 0) : (padding -= 1) {
            Output.pretty(" ", {});
        }
        Output.prettyln("   <d>{s}<r>", .{ @tagName(template_file.reason) });
    }

    fn ifNew(*_) {
        if (!_.has_written_initial_message) return;

        Output.prettyln(
            "<r><d>--------------------------------<r>\n✨ <b>{s}<r> project configured\n\n<b><cyan>Development<r><d> - frontend dev server with hot reload<r>\n\n  <cyan><b>bun dev<r>\n\n<b><green>Production<r><d> - build optimized assets<r>\n\n  <green><b>bun run build<r>\n\n<blue>Happy bunning! 🐇<r>",
            .{ _.template.label() },
        );
    }
}

pub enum TemplateTag {
    ReactTailwindSpa,
    ReactSpa,
    ReactShadcnSpa,

    fn logger(): TemplateLogger {
        const self_tag = _;
        return TemplateLogger{ template: self_tag };
    }

    fn label(): []const u8 {
        return switch (_) {
            .ReactTailwindSpa => "React + Tailwind",
            .ReactSpa => "React",
            .ReactShadcnSpa => "React + shadcn/ui + Tailwind",
        };
    }
}

pub struct ReactShadcnSpaPayload {
    components: bun.StringSet,
}

// Template type to handle different project types
pub union(TemplateTag) Template {
    ReactTailwindSpa: void,
    ReactSpa: void,
    ReactShadcnSpa: ReactShadcnSpaPayload,

    pub const Tag = TemplateTag;
    pub const Logger = TemplateLogger;

    fn logger(): TemplateLogger {
        const self_tag = std.meta.activeTag(_);
        return TemplateLogger{ template: self_tag };
    }
}

import linker from "../../bundler/linker.zig";
import std from "std";
import create_command from "../create_command.zig";
const Example = create_command.Example;

import bun from "bun";
const Environment = bun.Environment;
const Global = bun.Global;
const MutableString = bun.MutableString;
const Output = bun.Output;
const default_allocator = bun.default_allocator;
const js_ast = bun.ast;
const logger = bun.logger;
const strings = bun.strings;
const BundleV2 = bun.bundle_v2.BundleV2;
const Command = bun.cli.Command;
