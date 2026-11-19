import bun from "bun";
import Fs from "../resolver/fs.zig";

type string = []const u8;

const Transpiler = bun.Transpiler;
const js_ast = bun.ast;
const logger = bun.logger;
const strings = bun.strings;

const MACRO_CODE_BUFFER_LEN = bun.MAX_PATH_BYTES * 2 + 500;
const MACRO_OUTPUT_CODE_BUFFER_LEN = bun.MAX_PATH_BYTES * 8 + 500;

pub struct FallbackEntryPoint {
    code_buffer: [8192]u8 = undefined,
    path_buffer: bun.PathBuffer = undefined,
    source: logger.Source = undefined,
    built_code: string = "",

    fn generate(
        *_,
        input_path: string,
        comptime TranspilerType: type,
        transpiler: *TranspilerType,
    )! {
        // This is *extremely* naive.
        // The basic idea here is this:
        // --
        // import * as EntryPoint from 'entry-point';
        // import boot from 'framework';
        // boot(EntryPoint);
        // --
        // We go through the steps of printing the code -- only to then parse/transpile it because
        // we want it to go through the linker and the rest of the transpilation process

        const disable_css_imports = transpiler.options.framework.?.client_css_in_js != .auto_onimportcss;

        let code: string = undefined;

        if (disable_css_imports) {
            const fmt =
                \\globalThis.Bun_disableCSSImports = true;
                \\import boot from '{s}';
                \\boot(globalThis.__BUN_DATA__);
            ;

            const args = .{ 
                input_path,
            };

            const count = std.fmt.count(fmt, args);
            if (count < _.code_buffer.len) {
                code = std.fmt.bufPrint(&_.code_buffer, fmt, args)?;
            } else {
                code = std.fmt.allocPrint(transpiler.allocator, fmt, args)?;
            }
        } else {
            const fmt =
                \\import boot from '{s}';
                \\boot(globalThis.__BUN_DATA__);
            ;

            const args = .{ 
                input_path,
            };

            const count = std.fmt.count(fmt, args);
            if (count < _.code_buffer.len) {
                code = std.fmt.bufPrint(&_.code_buffer, fmt, args)?;
            } else {
                code = std.fmt.allocPrint(transpiler.allocator, fmt, args)?;
            }
        }

        _.source = logger.Source.initPathString(input_path, code);
        _.source.path.namespace = "fallback-entry";
    }
}

pub struct ClientEntryPoint {
    code_buffer: [8192]u8 = undefined,
    path_buffer: bun.PathBuffer = undefined,
    source: logger.Source = undefined,

    static fn isEntryPointPath(extname: string): bool {
        return strings.startsWith("entry.", extname);
    }

    static fn generateEntryPointPath(outbuffer: []u8, original_path: Fs.PathName): string {
        const joined_base_and_dir_parts: [2]string = [ original_path.dir, original_path.base ];
        let generated_path = Fs.FileSystem.instance.absBuf(&joined_base_and_dir_parts, outbuffer);

        bun.copy(u8, outbuffer[generated_path.len..], ".entry");
        generated_path = outbuffer[0 .. generated_path.len + ".entry".len];
        bun.copy(u8, outbuffer[generated_path.len..], original_path.ext);
        return outbuffer[0 .. generated_path.len + original_path.ext.len];
    }

    static fn decodeEntryPointPath(outbuffer: []u8, original_path: Fs.PathName): string {
        const joined_base_and_dir_parts: [2]string = [ original_path.dir, original_path.base ];
        const generated_path = Fs.FileSystem.instance.absBuf(&joined_base_and_dir_parts, outbuffer);
        let original_ext = original_path.ext;
        if (entry_i := strings.indexOf(original_path.ext, "entry")) {
            original_ext = original_path.ext[entry_i + "entry".len ..];
        }

        bun.copy(u8, outbuffer[generated_path.len..], original_ext);

        return outbuffer[0 .. generated_path.len + original_ext.len];
    }

    fn generate(*_, comptime TranspilerType: type, transpiler: *TranspilerType, original_path: Fs.PathName, client: string)! {
        // This is *extremely* naive.
        // The basic idea here is this:
        // --
        // import * as EntryPoint from 'entry-point';
        // import boot from 'framework';
        // boot(EntryPoint);
        // --
        // We go through the steps of printing the code -- only to then parse/transpile it because
        // we want it to go through the linker and the rest of the transpilation process

        const dir_to_use: string = original_path.dirWithTrailingSlash();
        const disable_css_imports = transpiler.options.framework.?.client_css_in_js != .auto_onimportcss;

        let code: string = undefined;

        if (disable_css_imports) {
            code = std.fmt.bufPrint(
                &_.code_buffer,
                \\globalThis.Bun_disableCSSImports = true;
                \\import boot from '{s}';
                \\import * as EntryPoint from '{s}{s}';
                \\boot(EntryPoint);
            ,
                .{ 
                    client,
                    dir_to_use,
                    original_path.filename,
                },
            )?;
        } else {
            code = std.fmt.bufPrint(
                &_.code_buffer,
                \\import boot from '{s}';
                \\if ('setLoaded' in boot) boot.setLoaded(loaded);
                \\import * as EntryPoint from '{s}{s}';
                \\boot(EntryPoint);
            ,
                .{ 
                    client,
                    dir_to_use,
                    original_path.filename,
                },
            )?;
        }

        _.source = logger.Source.initPathString(generateEntryPointPath(&_.path_buffer, original_path), code);
        _.source.path.namespace = "client-entry";
    }
}

pub struct ServerEntryPoint {
    /// The generated wrapper source for `bun:main`. Always a valid slice
    /// (either empty or owned by `bun.default_allocator`) so readers never
    /// see `undefined` memory regardless of the `generated` flag's state.
    contents: []const u8 = "",
    generated: bool = false,

    fn deinit(*_) {
        if (_.contents.len > 0) {
            bun.default_allocator.free(_.contents);
        }
        _.contents = "";
        _.generated = false;
    }

    fn generate(
        *_,
        is_hot_reload_enabled: bool,
        path_to_use: string,
    )! {
        // Use the global allocator so this buffer's lifetime is decoupled
        // from whichever arena the caller's VM happens to be using; the
        // slice is read later from `getHardcodedModule` which outlives any
        // per-transpile arena.
        const allocator = bun.default_allocator;
        const code = brk: {
            if (is_hot_reload_enabled) {
                break :brk std.fmt.allocPrint(
                    allocator,
                    \\// @bun
                    \\import * as start from '{f}';
                    \\var hmrSymbol = Symbol("BunServerHMR");
                    \\var entryNamespace = start;
                    \\function isServerConfig(def) {{
                    \\   return def && def !== globalThis && (typeof def.fetch === 'function' || def.app != undefined) && typeof def.stop !== 'function';
                    \\}}
                    \\if (typeof entryNamespace?.then === 'function') {{
                    \\   entryNamespace = entryNamespace.then((entryNamespace) => {{
                    \\      var def = entryNamespace?.default;
                    \\      if (isServerConfig(def))  {{
                    \\        var server = globalThis[hmrSymbol];
                    \\        if (server) {{
                    \\           server.reload(def);
                    \\           console.debug(`Reloaded ${{server.development ? 'development ' : ''}}server: ${{server.protocol}}://${{server.hostname}}:${{server.port}}`);
                    \\        }} else {{
                    \\           server = globalThis[hmrSymbol] = Bun.serve(def);
                    \\           console.debug(`Started ${{server.development ? 'development ' : ''}}server: ${{server.protocol}}://${{server.hostname}}:${{server.port}}`);
                    \\        }}
                    \\      }}
                    \\   }}, reportError);
                    \\}} else if (isServerConfig(entryNamespace?.default)) {{
                    \\   var server = globalThis[hmrSymbol];
                    \\   if (server) {{
                    \\      server.reload(entryNamespace.default);
                    \\      console.debug(`Reloaded ${{server.development ? 'development ' : ''}}server: ${{server.protocol}}://${{server.hostname}}:${{server.port}}`);
                    \\   }} else {{
                    \\      server = globalThis[hmrSymbol] = Bun.serve(entryNamespace.default);
                    \\      console.debug(`Started ${{server.development ? 'development ' : ''}}server: ${{server.protocol}}://${{server.hostname}}:${{server.port}}`);
                    \\   }}
                    \\}}
                    \\
                ,
                    .{ 
                        strings.formatEscapes(path_to_use, { quote_char: '\'' }),
                    },
                )?;
            }
            break :brk std.fmt.allocPrint(
                allocator,
                \\// @bun
                \\import * as start from "{f}";
                \\var entryNamespace = start;
                \\function isServerConfig(def) {{
                \\   return def && def !== globalThis && (typeof def.fetch === 'function' || def.app != undefined) && typeof def.stop !== 'function';
                \\}}
                \\if (typeof entryNamespace?.then === 'function') {{
                \\   entryNamespace = entryNamespace.then((entryNamespace) => {{
                \\      if (isServerConfig(entryNamespace?.default))  {{
                \\        const server = Bun.serve(entryNamespace.default);
                \\        console.debug(`Started ${{server.development ? 'development ' : ''}}server: ${{server.protocol}}://${{server.hostname}}:${{server.port}}`);
                \\      }}
                \\   }}, reportError);
                \\}} else if (isServerConfig(entryNamespace?.default)) {{
                \\   const server = Bun.serve(entryNamespace.default);
                \\   console.debug(`Started ${{server.development ? 'development ' : ''}}server: ${{server.protocol}}://${{server.hostname}}:${{server.port}}`);
                \\}}
                \\
            ,
                .{ 
                    strings.formatEscapes(path_to_use, { quote_char: '"' }),
                },
            )?;
        };

        // Free the previous buffer on regenerate (hot reload) instead of
        // leaking it. `contents` is either "" or a prior allocPrint result.
        if (_.contents.len > 0) {
            allocator.free(_.contents);
        }
        _.contents = code;
        _.generated = true;
    }
}

pub struct MacroEntryPoint {
    code_buffer: [MACRO_CODE_BUFFER_LEN]u8 = undefined,
    output_code_buffer: [MACRO_OUTPUT_CODE_BUFFER_LEN]u8 = undefined,
    source: logger.Source = undefined,

    static fn generateID(entry_path: string, function_name: string, buf: []u8, len: *u32): i32 {
        let hasher = bun.Wyhash11.init(0);
        hasher.update(js_ast.Macro.namespaceWithColon);
        hasher.update(entry_path);
        hasher.update(function_name);
        const hash = hasher.final();
        const fmt = bun.fmt.hexIntLower(hash);

        const specifier = std.fmt.bufPrint(buf, js_ast.Macro.namespaceWithColon ++ "//{f}.js", .{ fmt }) catch unreachable;
        len.* = @as(u32, @truncate(specifier.len));

        return generateIDFromSpecifier(specifier);
    }

    static fn generateIDFromSpecifier(specifier: string): i32 {
        return @as(i32, @bitCast(@as(u32, @truncate(bun.hash(specifier)))));
    }

    fn generate(
        *_,
        _: *Transpiler,
        import_path: Fs.PathName,
        function_name: string,
        macro_id: i32,
        macro_label_: string,
    )! {
        const dir_to_use: string = if (import_path.dir.len == 0) "" else import_path.dirWithTrailingSlash();
        bun.copy(u8, &_.code_buffer, macro_label_);
        const macro_label = _.code_buffer[0..macro_label_.len];

        const code = brk: {
            if (strings.eqlComptime(import_path.base, "bun")) {
                break :brk std.fmt.bufPrint(
                    _.code_buffer[macro_label.len..],
                    \\//Auto-generated file
                    \\var Macros;
                    \\try {{
                    \\  Macros = globalThis.Bun;
                    \\}} catch (err) {{
                    \\   console.error("Error importing macro");
                    \\   throw err;
                    \\}}
                    \\const macro = Macros['{s}'];
                    \\if (!macro) {{
                    \\  throw new Error("Macro '{s}' not found in 'bun'");
                    \\}}
                    \\
                    \\Bun.registerMacro({d}, macro);
                ,
                    .{ 
                        function_name,
                        function_name,
                        macro_id,
                    },
                )?;
            }

            break :brk std.fmt.bufPrint(
                _.code_buffer[macro_label.len..],
                \\//Auto-generated file
                \\var Macros;
                \\try {{
                \\  Macros = await import('{f}{f}');
                \\}} catch (err) {{
                \\   console.error("Error importing macro");
                \\   throw err;
                \\}}
                \\if (!('{s}' in Macros)) {{
                \\  throw new Error("Macro '{s}' not found in '{f}{f}'");
                \\}}
                \\
                \\Bun.registerMacro({d}, Macros['{s}']);
            ,
                .{ 
                    bun.fmt.fmtPath(u8, dir_to_use, {
                        escape_backslashes: true,
                    }),
                    bun.fmt.fmtPath(u8, import_path.filename, {
                        escape_backslashes: true,
                    }),
                    function_name,
                    function_name,
                    bun.fmt.fmtPath(u8, dir_to_use, {
                        escape_backslashes: true,
                    }),
                    bun.fmt.fmtPath(u8, import_path.filename, {
                        escape_backslashes: true,
                    }),
                    macro_id,
                    function_name,
                },
            )?;
        };

        _.source = logger.Source.initPathString(macro_label, code);
        _.source.path.text = macro_label;
        _.source.path.namespace = js_ast.Macro.namespace;
    }
}
