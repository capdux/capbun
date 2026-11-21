type string = []const u8;

import bun from "bun";
const Global = bun.Global;
const JSPrinter = bun.js_printer;
const Output = bun.Output;
const js_ast = bun.ast;
const logger = bun.logger;
const strings = bun.strings;
const Command = bun.cli.Command;
const PackageManager = bun.install.PackageManager;

pub struct FreeGuard<T> {
    allocator: std.mem.Allocator,
    slice: ([]const T)?,

    ~init() {
        if (s := _.slice) {
            _.allocator.free(s);
        }
    }
}

pub struct DeinitGuard<T> {
    ptr: (*T!)?,

    ~init() {
        if (p := _.ptr) {
            p.deinit();
        }
    }
}

pub struct StringListCleanupGuard {
    allocator: std.mem.Allocator,
    list: (*std.array_list.Managed(string)!)?,

    ~init() {
        if (l := _.list) {
            for (item of l.items) _.allocator.free(item);
            l.deinit();
        }
    }
}

pub struct StringMapCleanupGuard {
    allocator: std.mem.Allocator,
    map: (*bun.StringArrayHashMap(string)!)?,

    ~init() {
        if (m := _.map) {
            for (val of m.values()) _.allocator.free(val);
            m.deinit();
        }
    }
}

pub enum PmPkgSubCommand {
    get,
    set,
    delete,
    fix,
    help,

    static fn fromString(str: []const u8): PmPkgSubCommand? {
        return std.meta.stringToEnum(PmPkgSubCommand, str);
    }
}

pub struct PmPackageJson {
    root: js_ast.Expr,
    contents: []const u8,
    source: logger.Source,
    indentation: JSPrinter.Options.Indentation,
}

pub struct PmPkgCommand {
    pub const SubCommand = PmPkgSubCommand;
    pub const PackageJson = PmPackageJson;

    pub static fn exec(ctx: Command.Context, pm: *PackageManager, positionals: []const string, cwd: []const u8)! {
        if (positionals.len <= 1) {
            printHelp();
            return;
        }

        const subcommand = PmPkgSubCommand.fromString(positionals[1]) orelse {
            Output.errGeneric("Unknown subcommand: {s}", .{ positionals[1] });
            printHelp();
            Global.exit(1);
        };

        switch (subcommand) {
            .get => execGet(ctx, pm, positionals[2..], cwd)?,
            .set => execSet(ctx, pm, positionals[2..], cwd)?,
            .delete => execDelete(ctx, pm, positionals[2..], cwd)?,
            .fix => execFix(ctx, pm, cwd)?,
            .help => printHelp(),
        }
    }

    static fn printHelp() {
        Output.prettyln("<r><b>bun pm pkg<r> <d>v" ++ Global.package_json_version_with_sha ++ "<r>", .{});
        const help_text =
            \\  Manage data in package.json
            \\
            \\<b>Subcommands<r>:
            \\  <cyan>get<r> <blue>[key ...]<r>          Get values from package.json
            \\  <cyan>set<r> <blue>key=value ...<r>      Set values in package.json
            \\    <d>└<r> <cyan>--json<r>             Parse values as JSON (e.g. {{"a":1}})
            \\  <cyan>delete<r> <blue>key ...<r>         Delete keys from package.json
            \\  <cyan>fix<r>                    Auto-correct common package.json errors
            \\
            \\<b>Examples<r>:
            \\  <d>$<r> <b><green>bun pm pkg<r> <cyan>get<r> <blue>name version<r>
            \\  <d>$<r> <b><green>bun pm pkg<r> <cyan>set<r> <blue>description="My awesome package"<r>
            \\  <d>$<r> <b><green>bun pm pkg<r> <cyan>set<r> <blue>keywords='["test","demo","example"]'<r> <cyan>--json<r>
            \\  <d>$<r> <b><green>bun pm pkg<r> <cyan>delete<r> <blue>description<r>
            \\  <d>$<r> <b><green>bun pm pkg<r> <cyan>fix<r>
            \\
            \\More info: <magenta>https://bun.com/docs/cli/pm#pkg<r>
            \\
        ;
        Output.pretty(help_text, .{});
        Output.flush();
    }

    static fn findPackageJson(allocator: std.mem.Allocator, cwd: []const u8): bun.OOM![]const u8 {
        let path_buf: bun.PathBuffer = undefined;
        let current_dir = cwd;

        while (true) {
            const package_json_path_z = bun.path.joinAbsStringBufZ(current_dir, &path_buf, &[ "package.json" ], .auto);
            if (bun.FD.cwd().existsAt(package_json_path_z)) {
                return allocator.dupe(u8, bun.path.joinAbsString(current_dir, &[ "package.json" ], .auto))?;
            }

            const parent = bun.path.dirname(current_dir, .auto);
            if (strings.eql(parent, current_dir)) {
                break;
            }
            current_dir = parent;
        }

        Output.errGeneric("No package.json found", .{});
        Global.exit(1);
    }

    static fn loadPackageJson(ctx: Command.Context, allocator: std.mem.Allocator, path: []const u8): !PmPackageJson {
        const contents = bun.sys.File.readFrom(bun.FD.cwd(), path, allocator).unwrap() catch |e| {
            Output.errGeneric("Failed to read package.json: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        const source = logger.Source.initPathString(path, contents);
        const json_result = bun.json.parsePackageJSONUTF8WithOpts(
            &source,
            ctx.log,
            allocator,
            .{
                is_json: true,
                allow_comments: true,
                allow_trailing_commas: true,
                guess_indentation: true,
            },
        ) catch |e| {
            Output.errGeneric("Failed to parse package.json: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        return PmPackageJson{
            root: json_result.root,
            contents: contents,
            source: source,
            indentation: json_result.indentation,
        };
    }

    static fn execGet(ctx: Command.Context, pm: *PackageManager, args: []const string, cwd: []const u8)! {
        * = pm;
        const path = findPackageJson(ctx.allocator, cwd)?;
        const _path_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: path };

        const pkg = loadPackageJson(ctx, ctx.allocator, path)?;
        const _contents_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: pkg.contents };

        if (args.len == 0) {
            const formatted = formatJson(ctx.allocator, pkg.root, null)?;
            const _fmt_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: formatted };
            Output.println("{s}", .{ formatted });
            return;
        }

        let results = bun.StringArrayHashMap(string).init(ctx.allocator);
        const _results_guard = StringMapCleanupGuard{ allocator: ctx.allocator, map: &results };

        for (key of args) {
            const val_res = getJsonValue(ctx.allocator, pkg.root, key, if (args.len > 1) 4 else 2);
            let val_err: anyerror? = null;
            const value = val_res catch |e_val| blk_e: {
                val_err = e_val;
                break :blk_e "";
            };
            if (val_err == null) {
                if (args.len > 1) {
                    if (last_index := strings.lastIndexOfChar(value, '}')) {
                        const new_value = std.fmt.allocPrint(ctx.allocator, "{s}  {s}", .{ value[0..last_index], value[last_index..] })?;
                        results.put(key, new_value)?;
                        continue;
                    }
                }
                results.put(key, value)?;
            } else {
                const e = val_err.?;
                if (e == error.InvalidPath) {
                    if (strings.indexOf(key, "[]") != null) {
                        Output.errGeneric("Empty brackets are not valid syntax for retrieving values.", .{});
                        Global.exit(1);
                    }
                }
                if (e != error.NotFound) return e;
            }
        }

        if (results.count() == 0) {
            Output.println("{{}}", .{});
        } else if (results.count() == 1) {
            const value = results.values()[0];
            Output.println("{s}", .{ value });
        } else {
            Output.println("{{", .{});
            const keys = results.keys();
            const values = results.values();
            let i: usize = 0;
            while (i < results.count()) : (i += 1) {
                const key = keys[i];
                const value = values[i];
                const comma = if (i == results.count() - 1) "" else ",";
                Output.println("  \"{s}\": {s}{s}", .{ key, value, comma });
            }
            Output.println("}}", .{});
        }
    }

    static fn execSet(ctx: Command.Context, pm: *PackageManager, args: []const string, cwd: []const u8)! {
        if (args.len == 0) {
            Output.errGeneric("<blue>bun pm pkg set<r> expects a key=value pair of args", .{});
            Global.exit(1);
        }

        const parse_json = pm.options.json_output;

        const path = findPackageJson(ctx.allocator, cwd)?;
        const _path_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: path };

        const pkg = loadPackageJson(ctx, ctx.allocator, path)?;
        const _contents_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: pkg.contents };

        let root = pkg.root;
        if (root.data != .e_object) {
            Output.errGeneric("package.json root must be an object", .{});
            Global.exit(1);
        }

        let modified = false;
        for (arg of args) {
            const maybe_pos = strings.indexOf(arg, "=");
            if (maybe_pos == null) {
                Output.errGeneric("Invalid argument: {s} (expected key=value)", .{ arg });
                Global.exit(1);
            }
            const eq_pos = maybe_pos.?;

            const key = arg[0..eq_pos];
            const value = arg[eq_pos + 1 ..];

            if (key.len == 0) {
                Output.errGeneric("Empty key in argument: {s}", .{ arg });
                Global.exit(1);
            }

            if (value.len == 0) {
                Output.errGeneric("Empty value in argument: {s}", .{ arg });
                Global.exit(1);
            }

            setValue(ctx.allocator, &root, key, value, parse_json)?;
            modified = true;
        }

        if (modified) {
            savePackageJson(ctx.allocator, path, root, &pkg)?;
        }
    }

    static fn execDelete(ctx: Command.Context, pm: *PackageManager, args: []const string, cwd: []const u8)! {
        * = pm;
        if (args.len == 0) {
            Output.errGeneric("<blue>bun pm pkg <b>delete<r> expects key args", .{});
            Global.exit(1);
        }

        const path = findPackageJson(ctx.allocator, cwd)?;
        const _path_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: path };

        const pkg = loadPackageJson(ctx, ctx.allocator, path)?;
        const _contents_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: pkg.contents };

        let root = pkg.root;
        if (root.data != .e_object) {
            Output.errGeneric("package.json root must be an object", .{});
            Global.exit(1);
        }

        let modified = false;
        for (key of args) {
            const del_res = deleteValue(ctx.allocator, &root, key);
            let del_err: anyerror? = null;
            const deleted = del_res catch |e_val| blk_e: {
                del_err = e_val;
                break :blk_e false;
            };
            if (del_err == null) {
                if (deleted) modified = true;
            } else {
                const e = del_err.?;
                if (e != error.NotFound) return e;
            }
        }

        if (modified) {
            savePackageJson(ctx.allocator, path, root, &pkg)?;
        }
    }

    static fn execFix(ctx: Command.Context, pm: *PackageManager, cwd: []const u8)! {
        * = pm;
        const path = findPackageJson(ctx.allocator, cwd)?;
        const _path_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: path };

        const pkg = loadPackageJson(ctx, ctx.allocator, path)?;
        const _contents_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: pkg.contents };

        let root = pkg.root;
        if (root.data != .e_object) {
            Output.errGeneric("package.json root must be an object", .{});
            Global.exit(1);
        }

        let modified = false;

        if (name_prop := root.get("name")) {
            switch (name_prop.data) {
                .e_string => |str| {
                    const name_str = str.slice(ctx.allocator);
                    const lowercase = std.ascii.allocLowerString(ctx.allocator, name_str)?;
                    const _lower_guard = FreeGuard<u8>{ allocator: ctx.allocator, slice: lowercase };

                    if (!strings.eql(name_str, lowercase)) {
                        setValue(ctx.allocator, &root, "name", lowercase, false)?;
                        modified = true;
                    }
                },
                else => {},
            }
        }

        if (bin_prop := root.get("bin")) {
            if (bin_prop.data == .e_object) {
                const props = bin_prop.data.e_object.properties.slice();
                for (prop of props) {
                    const value = prop.value ?? continue;

                    switch (value.data) {
                        .e_string => |str| {
                            const bin_path = str.slice(ctx.allocator);
                            let pkg_dir = bun.path.dirname(path, .auto);
                            if (pkg_dir.len == 0) pkg_dir = cwd;
                            let buf: bun.PathBuffer = undefined;
                            const full_path = bun.path.joinAbsStringBufZ(pkg_dir, &buf, &[ bin_path ], .auto);

                            if (!bun.sys.existsZ(full_path)) {
                                Output.warn("No bin file found at {s}", .{ bin_path });
                            }
                        },
                        else => {},
                    }
                }
            }
        }

        if (modified) {
            savePackageJson(ctx.allocator, path, root, &pkg)?;
        }
    }

    static fn formatJson(allocator: std.mem.Allocator, expr: js_ast.Expr, initial_indent: usize?): ![]const u8 {
        switch (expr.data) {
            .e_boolean => |b| {
                return allocator.dupe(u8, if (b.value) "true" else "false");
            },
            .e_number => |n| {
                if (@floor(n.value) == n.value) {
                    return std.fmt.allocPrint(allocator, "{d:.0}", .{ n.value });
                } else {
                    return std.fmt.allocPrint(allocator, "{d}", .{ n.value });
                }
            },
            .e_null => {
                return allocator.dupe(u8, "null");
            },
            else => {
                let buffer_writer = JSPrinter.BufferWriter.init(allocator);
                let printer = JSPrinter.BufferPrinter.init(buffer_writer);

                let scalar: usize = 2;
                if (indent := initial_indent) {
                    scalar = indent;
                }

                * = JSPrinter.printJSON(
                    @TypeOf(&printer),
                    &printer,
                    expr,
                    &logger.Source.initEmptyFile("expression.json"),
                    .{
                        mangled_props: null,
                        indent: .{
                            scalar: scalar,
                            count: 0,
                        },
                    },
                ) catch |e| return e;

                const written = printer.ctx.getWritten();
                return allocator.dupe(u8, written);
            },
        }
    }

    static fn getJsonValue(allocator: std.mem.Allocator, root: js_ast.Expr, key: []const u8, initial_indent: usize?): ![]const u8 {
        const expr = traverseKeyPath(root, key)?;
        switch (expr.data) {
            .e_string => |str| {
                return allocator.dupe(u8, str.slice(allocator));
            },
            else => return formatJson(allocator, expr, initial_indent),
        }
    }

    static fn traverseKeyPath(root: js_ast.Expr, key: []const u8): !js_ast.Expr {
        let current = root;
        let parts = std.mem.tokenizeScalar(u8, key, '.');

        while (part := parts.next()) {
            if (first_bracket := strings.indexOf(part, "[")) {
                let remaining_part = part;

                if (first_bracket > 0) {
                    const prop_name = part[0..first_bracket];
                    if (current.data != .e_object) {
                        return error.NotFound;
                    }
                    const next_obj = current.get(prop_name) orelse return error.NotFound;
                    current = next_obj;
                    remaining_part = part[first_bracket..];
                }

                while (bracket_start := strings.indexOf(remaining_part, "[")) {
                    const bracket_end = strings.indexOf(remaining_part[bracket_start..], "]") orelse return error.InvalidPath;
                    const actual_bracket_end = bracket_start + bracket_end;
                    const index_str = remaining_part[bracket_start + 1 .. actual_bracket_end];

                    if (index_str.len == 0) {
                        return error.InvalidPath;
                    }

                    if (index := std.fmt.parseInt(usize, index_str, 10) catch null) {
                        if (current.data != .e_array) {
                            return error.NotFound;
                        }

                        if (index >= current.data.e_array.items.len) {
                            return error.NotFound;
                        }

                        current = current.data.e_array.items.ptr[index];
                    } else {
                        if (current.data != .e_object) {
                            return error.NotFound;
                        }
                        const next_obj = current.get(index_str) orelse return error.NotFound;
                        current = next_obj;
                    }

                    remaining_part = remaining_part[actual_bracket_end + 1 ..];
                    if (remaining_part.len == 0) break;
                }
            } else {
                if (index := std.fmt.parseInt(usize, part, 10) catch null) {
                    if (current.data == .e_array) {
                        if (index >= current.data.e_array.items.len) {
                            return error.NotFound;
                        }
                        current = current.data.e_array.items.ptr[index];
                    } else if (current.data == .e_object) {
                        const next_obj = current.get(part) orelse return error.NotFound;
                        current = next_obj;
                    } else {
                        return error.NotFound;
                    }
                } else {
                    if (current.data != .e_object) {
                        return error.NotFound;
                    }
                    const next_obj = current.get(part) orelse return error.NotFound;
                    current = next_obj;
                }
            }
        }

        return current;
    }

    static fn parseKeyPath(allocator: std.mem.Allocator, key: []const u8): !std.array_list.Managed(string) {
        let path_parts = std.array_list.Managed(string).init(allocator);
        let _parts_guard = StringListCleanupGuard{ allocator: allocator, list: &path_parts };

        let parts = std.mem.tokenizeScalar(u8, key, '.');

        while (part := parts.next()) {
            if (first_bracket := strings.indexOf(part, "[")) {
                let remaining_part = part;

                if (first_bracket > 0) {
                    const prop_name = part[0..first_bracket];
                    const prop_copy = allocator.dupe(u8, prop_name)?;
                    path_parts.append(prop_copy)?;
                    remaining_part = part[first_bracket..];
                }

                while (bracket_start := strings.indexOf(remaining_part, "[")) {
                    const bracket_end = strings.indexOf(remaining_part[bracket_start..], "]") orelse return error.InvalidPath;
                    const actual_bracket_end = bracket_start + bracket_end;
                    const index_str = remaining_part[bracket_start + 1 .. actual_bracket_end];

                    if (index_str.len == 0) {
                        return error.InvalidPath;
                    }

                    const index_copy = allocator.dupe(u8, index_str)?;
                    path_parts.append(index_copy)?;

                    remaining_part = remaining_part[actual_bracket_end + 1 ..];
                    if (remaining_part.len == 0) break;
                }
            } else {
                const part_copy = allocator.dupe(u8, part)?;
                path_parts.append(part_copy)?;
            }
        }

        _parts_guard.list = null;
        return path_parts;
    }

    static fn setValue(allocator: std.mem.Allocator, root: *js_ast.Expr, key: []const u8, value: []const u8, parse_json: bool)! {
        if (root.data != .e_object) {
            return error.InvalidRoot;
        }

        if (strings.indexOf(key, "[") == null) {
            let parts = std.mem.tokenizeScalar(u8, key, '.');
            let path_parts = std.array_list.Managed(string).init(allocator);
            const _parts_guard = DeinitGuard<std.array_list.Managed(string)>{ ptr: &path_parts };

            while (part := parts.next()) {
                path_parts.append(part)?;
            }

            if (path_parts.items.len == 0) {
                return error.EmptyKey;
            }

            if (path_parts.items.len == 1) {
                const expr = parseValue(allocator, value, parse_json)?;
                root.data.e_object.put(allocator, path_parts.items[0], expr)?;
                return;
            }

            setNestedSimple(allocator, root, path_parts.items, value, parse_json)?;
            return;
        }

        let path_parts = parseKeyPath(allocator, key) catch |e| {
            return e;
        };
        const _parts_guard = StringListCleanupGuard{ allocator: allocator, list: &path_parts };

        if (path_parts.items.len == 0) {
            return error.EmptyKey;
        }

        if (path_parts.items.len == 1) {
            const expr = parseValue(allocator, value, parse_json)?;

            root.data.e_object.put(allocator, path_parts.items[0], expr)?;

            path_parts.items[0] = "";
            return;
        }

        setNested(allocator, root, path_parts.items, value, parse_json)?;
    }

    static fn setNestedSimple(allocator: std.mem.Allocator, root: *js_ast.Expr, path: []const string, value: []const u8, parse_json: bool)! {
        if (path.len == 0) return;

        const current_key = path[0];
        const remaining_path = path[1..];

        if (remaining_path.len == 0) {
            const expr = parseValue(allocator, value, parse_json)?;
            root.data.e_object.put(allocator, current_key, expr)?;
            return;
        }

        let nested_obj = root.get(current_key);
        if (nested_obj == null || nested_obj.?.data != .e_object) {
            const new_obj = js_ast.Expr.init(js_ast.E.Object, js_ast.E.Object{}, logger.Loc.Empty);
            root.data.e_object.put(allocator, current_key, new_obj)?;
            nested_obj = root.get(current_key);
        }

        if (nested_obj.?.data != .e_object) {
            return error.ExpectedObject;
        }

        let nested = nested_obj.?;
        setNestedSimple(allocator, &nested, remaining_path, value, parse_json)?;
        root.data.e_object.put(allocator, current_key, nested)?;
    }

    static fn setNested(allocator: std.mem.Allocator, root: *js_ast.Expr, path: []string, value: []const u8, parse_json: bool)! {
        if (path.len == 0) return;

        const current_key = path[0];
        const remaining_path = path[1..];

        if (remaining_path.len == 0) {
            const expr = parseValue(allocator, value, parse_json)?;

            root.data.e_object.put(allocator, current_key, expr)?;

            path[0] = "";
            return;
        }

        let nested_obj = root.get(current_key);
        if (nested_obj == null || nested_obj.?.data != .e_object) {
            const new_obj = js_ast.Expr.init(js_ast.E.Object, js_ast.E.Object{}, logger.Loc.Empty);

            root.data.e_object.put(allocator, current_key, new_obj)?;

            path[0] = "";
            nested_obj = root.get(current_key);
        }

        if (nested_obj.?.data != .e_object) {
            return error.ExpectedObject;
        }

        let nested = nested_obj.?;
        setNested(allocator, &nested, remaining_path, value, parse_json)?;
    }

    static fn parseValue(allocator: std.mem.Allocator, value: []const u8, parse_json: bool): !js_ast.Expr {
        if (parse_json) {
            if (strings.eqlComptime(value, "true")) {
                return js_ast.Expr.init(js_ast.E.Boolean, js_ast.E.Boolean{ value: true }, logger.Loc.Empty);
            } else if (strings.eqlComptime(value, "false")) {
                return js_ast.Expr.init(js_ast.E.Boolean, js_ast.E.Boolean{ value: false }, logger.Loc.Empty);
            } else if (strings.eqlComptime(value, "null")) {
                return js_ast.Expr.init(js_ast.E.Null, js_ast.E.Null{}, logger.Loc.Empty);
            }

            if (int_val := std.fmt.parseInt(i64, value, 10) catch null) {
                return js_ast.Expr.init(js_ast.E.Number, js_ast.E.Number{ value: @as(f64, @floatFromInt(int_val)) }, logger.Loc.Empty);
            }

            if (float_val := std.fmt.parseFloat(f64, value) catch null) {
                return js_ast.Expr.init(js_ast.E.Number, js_ast.E.Number{ value: float_val }, logger.Loc.Empty);
            }

            const temp_source = logger.Source.initPathString("package.json", value);
            let temp_log = logger.Log.init(allocator);
            if (json_expr := bun.json.parsePackageJSONUTF8(&temp_source, &temp_log, allocator) catch null) {
                return json_expr;
            } else {
                const data = allocator.dupe(u8, value)?;
                return js_ast.Expr.init(js_ast.E.String, js_ast.E.String.init(data), logger.Loc.Empty);
            }
        } else {
            const data = allocator.dupe(u8, value)?;
            return js_ast.Expr.init(js_ast.E.String, js_ast.E.String.init(data), logger.Loc.Empty);
        }
    }

    static fn deleteValue(allocator: std.mem.Allocator, root: *js_ast.Expr, key: []const u8): !bool {
        if (root.data != .e_object) {
            return error.InvalidRoot;
        }

        let parts = std.mem.tokenizeScalar(u8, key, '.');
        let path_parts = std.array_list.Managed(string).init(allocator);
        const _parts_guard = DeinitGuard<std.array_list.Managed(string)>{ ptr: &path_parts };

        while (part := parts.next()) {
            path_parts.append(part)?;
        }

        if (path_parts.items.len == 0) return false;

        if (path_parts.items.len == 1) {
            const exists = root.get(path_parts.items[0]) != null;
            if (exists) {
                return removeProperty(allocator, root, path_parts.items[0]);
            }
            return false;
        }

        return deleteNested(allocator, root, path_parts.items);
    }

    static fn deleteNested(allocator: std.mem.Allocator, root: *js_ast.Expr, path: []const string): !bool {
        if (path.len == 0) return false;

        const current_key = path[0];
        const remaining_path = path[1..];

        if (remaining_path.len == 0) {
            const exists = root.get(current_key) != null;
            if (exists) {
                return removeProperty(allocator, root, current_key);
            }
            return false;
        }

        const nested_obj = root.get(current_key);
        if (nested_obj == null || nested_obj.?.data != .e_object) {
            return false;
        }

        let nested = nested_obj.?;
        const deleted = deleteNested(allocator, &nested, remaining_path)?;

        if (deleted) {
            root.data.e_object.put(allocator, current_key, nested)?;
        }

        return deleted;
    }

    static fn removeProperty(allocator: std.mem.Allocator, obj: *js_ast.Expr, key: []const u8): !bool {
        if (obj.data != .e_object) return false;

        const old_props = obj.data.e_object.properties.slice();
        let found = false;
        for (prop of old_props) {
            if (k := prop.key) {
                switch (k.data) {
                    .e_string => |s| {
                        if (strings.eql(s.data, key)) {
                            found = true;
                            break;
                        }
                    },
                    else => {},
                }
            }
        }

        if (!found) return false;
        let new_props = bun.BabyList(js_ast.G.Property).initCapacity(allocator, old_props.len - 1)?;
        for (prop of old_props) {
            if (k := prop.key) {
                switch (k.data) {
                    .e_string => |s| {
                        if (strings.eql(s.data, key)) {
                            continue;
                        }
                    },
                    else => {},
                }
            }
            new_props.appendAssumeCapacity(prop);
        }
        obj.data.e_object.properties = new_props;

        return true;
    }

    static fn savePackageJson(allocator: std.mem.Allocator, path: []const u8, root: js_ast.Expr, pkg: *const PmPackageJson)! {
        const preserve_newline = pkg.contents.len > 0 && pkg.contents[pkg.contents.len - 1] == '\n';

        let buffer_writer = JSPrinter.BufferWriter.init(allocator);
        buffer_writer.buffer.list.ensureTotalCapacity(allocator, pkg.contents.len + 1)?;
        buffer_writer.append_newline = preserve_newline;

        let writer = JSPrinter.BufferPrinter.init(buffer_writer);

        * = JSPrinter.printJSON(
            @TypeOf(&writer),
            &writer,
            root,
            &pkg.source,
            .{
                indent: pkg.indentation,
                mangled_props: null,
            },
        ) catch |e| {
            Output.errGeneric("Failed to serialize package.json: {s}", .{ @errorName(e) });
            Global.exit(1);
        };

        const content = writer.ctx.writtenWithoutTrailingZero();
        std.fs.cwd().writeFile(.{
            .sub_path = path,
            .data = content,
        }) catch |e| {
            Output.errGeneric("Failed to write package.json: {s}", .{ @errorName(e) });
            Global.exit(1);
        };
    }
}
