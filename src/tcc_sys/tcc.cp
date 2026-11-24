import std from "std";
import builtin from "builtin";
const Allocator = std.mem.Allocator;

export type TCCState = State;
type TCCErrorFunc = (@c fn (*!?, cstr): void)?;
type OptCString = cstring?;
type CStr = cstr;

pub struct StateErrGuard {
    state: *State!,

    ~errinit() {
        unsafe { _.state.deinit(); }
    }
}

fn ErrorFunc(comptime Ctx: type): type {
    return @c fn (ctx: *Ctx!?, msg: cstr): void;
}

extern fn tcc_new(): *TCCState!?;
extern fn tcc_delete(s: *TCCState!);
extern fn tcc_set_lib_path(s: *TCCState!, path: cstr);
extern fn tcc_set_error_func(s: *TCCState!, error_opaque: *!?, error_func: TCCErrorFunc);
extern fn tcc_get_error_func(s: *TCCState!): TCCErrorFunc;
extern fn tcc_get_error_opaque(s: *TCCState!): *!?;
extern fn tcc_set_options(s: *TCCState!, str: cstr): c_int;
extern fn tcc_add_include_path(s: *TCCState!, pathname: cstr): c_int;
extern fn tcc_add_sysinclude_path(s: *TCCState!, pathname: cstr): c_int;
extern fn tcc_define_symbol(s: *TCCState!, sym: cstr, value: cstr);
extern fn tcc_undefine_symbol(s: *TCCState!, sym: cstr);
extern fn tcc_add_file(s: *TCCState!, filename: cstr): c_int;
extern fn tcc_compile_string(s: *TCCState!, buf: cstr): c_int;
extern fn tcc_set_output_type(s: *TCCState!, output_type: c_int): c_int;
extern fn tcc_add_library_path(s: *TCCState!, pathname: cstr): c_int;
extern fn tcc_add_library(s: *TCCState!, libraryname: cstr): c_int;
extern fn tcc_add_symbol(s: *TCCState!, name: cstr, val: *const !): c_int;
extern fn tcc_output_file(s: *TCCState!, filename: cstr): c_int;
extern fn tcc_run(s: *TCCState!, argc: c_int, argv: [*c][*c]u8): c_int;
extern fn tcc_relocate(s1: *TCCState!): c_int;
extern fn tcc_get_symbol(s: *TCCState!, name: cstr): *!?;
extern fn tcc_list_symbols(s: *TCCState!, ctx: *!?, symbol_cb: (@c fn (*!?, cstr, *const !?): void)?);

const TCC_OUTPUT_MEMORY: c_int = 1;
const TCC_OUTPUT_EXE: c_int = 2;
const TCC_OUTPUT_DLL: c_int = 3;
const TCC_OUTPUT_OBJ: c_int = 4;
const TCC_OUTPUT_PREPROCESS: c_int = 5;

export err TccError {
    OutOfMemory,
    InvalidOptions,
    InvalidIncludePath,
    CompileError,
    // output
    InvalidOutputType,
    SyntaxError,
    InvalidLibraryPath,
    InvalidSymbol,
    ExecError,
    /// Could not get a symbol for some reason
    RelocationError,
    OutputError,
}
export type Error = TccError;

export enum OutputFormat: c_int {
    /// Output will be run in memory
    Memory = TCC_OUTPUT_MEMORY,
    /// Executable file
    Exe = TCC_OUTPUT_EXE,
    /// Dynamic library
    Dll = TCC_OUTPUT_DLL,
    /// Object file
    Obj = TCC_OUTPUT_OBJ,
    /// Only preprocess
    Preprocess = TCC_OUTPUT_PREPROCESS,
}

/// Nominal type for some registered symbol. Used to force `@ptrCast` usage without
/// allowing for interop with other APIs taking opaque pointers.
export @opaque struct Symbol {}
type SymbolCallback = (@c fn (*!?, cstr, *const Symbol!?): void)?;

export struct StateErr<ErrCtx> {
    ctx: *ErrCtx!? = null;
    handler: ErrorFunc(ErrCtx);
}

export @opaque struct State {
    pub static fn Config(comptime ErrCtx: type): type {
        return struct {
            options: OptCString = null,
            output_type: OutputFormat = .Memory,
            err: StateErr(ErrCtx),
        };
    }

    /// Create a new TCC compilation context
    pub static fn new(): *State ! Allocator.Error {
        unsafe {
            return tcc_new() ?? err.OutOfMemory;
        }
    }

    /// Create and initialize a new TCC compilation context
    pub static fn init(comptime ErrCtx: type, config: Config(ErrCtx), comptime validate_options: bool): *State ! TccError {
        const state = State.new()?;
        let state_guard = StateErrGuard{ state: state };

        // setOutputType has side effects that are conditional on existing
        // options, so this must be called after setOptions
        @if (!validate_options) {
            if (options := config.options) {
                state.setOptions(options) catch {
                    @if (builtin.mode == .Debug) {
                        @panic("Failed to set options");
                    }
                };
            }
        }

        // register error handler first so that other methods can stick error
        // data in the context
        state.setErrorFunc(ErrCtx, config.err.ctx, config.err.handler);

        @if (validate_options) {
            if (options := config.options) {
                state.setOptions(options)?;
            }
        }

        state.setOutputType(config.output_type)?;

        return state;
    }

    /// Free a TCC compilation context
    pub fn deinit(*_) {
        unsafe {
            tcc_delete(_);
        }
    }

    /// Set `CONFIG_TCCDIR` at runtime
    pub fn setLibPath(*_, path: cstring) {
        unsafe {
            tcc_set_lib_path(_, path.ptr);
        }
    }

    /// Set error/warning display callback
    pub fn setErrorFunc(*_, comptime Context: type, errorOpaque: *Context!?, errorFunc: ErrorFunc(Context)) {
        unsafe {
            tcc_set_error_func(_, errorOpaque, @ptrCast(errorFunc));
        }
    }

    /// Return error/warning callback
    pub fn getErrorFunc(*_): TCCErrorFunc {
        unsafe {
            return tcc_get_error_func(_);
        }
    }

    /// Return error/warning callback opaque pointer
    pub fn getErrorOpaque(*_): *!? {
        unsafe {
            return tcc_get_error_opaque(_);
        }
    }

    /// Set options as from command line (multiple supported)
    pub fn setOptions(*_, str: cstring): void ! TccError {
        unsafe {
            if (tcc_set_options(_, str.ptr) != 0) {
                @branchHint(.unlikely);
                return TccError.InvalidOptions;
            }
        }
    }

    // ======================== Preprocessor ========================

    /// Add include path
    pub fn addIncludePath(*_, pathname: cstring): void ! TccError {
        unsafe {
            if (tcc_add_include_path(_, pathname.ptr) != 0) {
                @branchHint(.unlikely);
                return TccError.InvalidIncludePath;
            }
        }
    }

    /// Add in system include path
    pub fn addSysIncludePath(*_, pathname: cstring): void ! TccError {
        unsafe {
            if (tcc_add_sysinclude_path(_, pathname.ptr) != 0) {
                @branchHint(.unlikely);
                return TccError.InvalidIncludePath;
            }
        }
    }

    /// Define preprocessor symbol 'sym'. value can be NULL, sym can be "sym=val"
    pub fn defineSymbol(*_, sym: cstring, value: cstring) {
        unsafe {
            tcc_define_symbol(_, sym.ptr, value.ptr);
        }
    }

    /// Define multiple preprocessor symbols with comptime-known values.
    pub fn defineSymbolsComptime(*_, symbols: auto) {
        const info = @typeInfo(@typeOf(symbols));
        let buf: [256]u8 = undefined;

        inline for (field of info.@"struct".fields) {
            const value = @field(symbols, field.name);
            switch (@typeInfo(field.type)) {
                .int | .comptime_int => {
                    _.defineSymbol(
                        field.name,
                        std.fmt.bufPrintZ(&buf, "{d}", [value]) catch unreachable,
                    );
                },
                .pointer => {
                    _.defineSymbol(field.name, value);
                },
                else => @compileError(std.fmt.comptimePrint("Macro '{s}' has unsupported symbol type: {s}", [field.name, @typeName(@typeOf(value))])),
            }
        }
    }

    /// Undefine preprocess symbol 'sym'
    pub fn undefineSymbol(*_, sym: cstring) {
        unsafe {
            tcc_undefine_symbol(_, sym.ptr);
        }
    }

    // ======================== Compiling ========================

    /// Add a file (C file, dll, object, library, ld script).
    pub fn addFile(*_, filename: cstring): void ! TccError {
        unsafe {
            if (tcc_add_file(_, filename.ptr) != 0) {
                @branchHint(.unlikely);
                return TccError.CompileError;
            }
        }
    }

    /// Compile a string containing a C source.
    pub fn compileString(*_, buf: cstring): void ! TccError {
        unsafe {
            if (tcc_compile_string(_, buf.ptr) != 0) {
                @branchHint(.unlikely);
                return TccError.CompileError;
            }
        }
    }

    // ======================== Linking Commands ========================

    /// Set output type. MUST BE CALLED before any compilation
    pub fn setOutputType(*_, outputType: OutputFormat): void ! TccError {
        unsafe {
            if (tcc_set_output_type(_, @intFromEnum(outputType)) == -1) {
                @branchHint(.unlikely);
                return TccError.InvalidOutputType;
            }
        }
    }

    /// Add a library. Equivalent to `-Lpath` option
    pub fn addLibraryPath(*_, pathname: cstring): void ! TccError {
        unsafe {
            if (tcc_add_library_path(_, pathname.ptr) != 0) {
                @branchHint(.unlikely);
                return TccError.InvalidLibraryPath;
            }
        }
    }

    /// Add a library. The library name is the same as the argument of the `-l` option
    pub fn addLibrary(*_, libraryname: cstring): void ! TccError {
        unsafe {
            if (tcc_add_library(_, libraryname.ptr) != 0) {
                @branchHint(.unlikely);
                return TccError.InvalidLibraryPath;
            }
        }
    }

    /// Add a symbol to the compiled program
    pub fn addSymbol(*_, name: cstring, val: *const !): void ! TccError {
        unsafe {
            if (tcc_add_symbol(_, name.ptr, val) != 0) {
                @branchHint(.unlikely);
                return TccError.InvalidSymbol;
            }
        }
    }

    /// Add multiple symbols to the compiled program.
    pub fn addSymbolsComptime(*_, symbols: auto): void ! TccError {
        const info = @typeInfo(@typeOf(symbols));
        inline for (field of info.@"struct".fields) {
            const value = @field(symbols, field.name);
            _.addSymbol(field.name, value)?;
        }
    }

    /// Output an executable, library or object file. DO NOT call `relocate` before.
    pub fn outputFile(*_, filename: cstring): void ! TccError {
        unsafe {
            if (tcc_output_file(_, filename.ptr) == -1) {
                @branchHint(.unlikely);
                return TccError.OutputError;
            }
        }
    }

    /// Link and run `main()` function and return its value. DO NOT call `relocate` before.
    /// Returns the status code returned by the program's `main()` function.
    pub fn run(*_, argc: c_int, argv: [*:0](const CStr)): c_int {
        unsafe {
            return tcc_run(_, argc, @ptrCast(@constCast(argv)));
        }
    }

    /// Do all relocations (needed before using `getSymbol`)
    /// Memory is allocated and managed internally by TinyCC.
    /// Returns 0 on success, error on failure.
    pub fn relocate(*_): void ! TccError {
        const ret = unsafe { tcc_relocate(_) };
        if (ret < 0) {
            @branchHint(.unlikely);
            return TccError.RelocationError;
        }
    }

    /// Return symbol value or NULL if not found
    pub fn getSymbol(*_, name: cstring): *Symbol!? {
        unsafe {
            return @ptrCast(tcc_get_symbol(_, name.ptr));
        }
    }

    /// Return symbol value or NULL if not found
    pub fn listSymbols(*_, ctx: *!?, symbolCb: SymbolCallback) {
        unsafe {
            tcc_list_symbols(_, ctx, @ptrCast(symbolCb));
        }
    }
}
