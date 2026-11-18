pub const BytecodeResult = struct { []const u8, *CachedBytecode };

@c {
    fn generateCachedModuleByteCodeFromSourceCode(sourceProviderURL: *bun.String, input_code: [*]const u8, inputSourceCodeSize: usize, outputByteCode: *([*]u8)?, outputByteCodeSize: *usize, cached_bytecode: *(*CachedBytecode)?): bool;
    fn generateCachedCommonJSProgramByteCodeFromSourceCode(sourceProviderURL: *bun.String, input_code: [*]const u8, inputSourceCodeSize: usize, outputByteCode: *([*]u8)?, outputByteCodeSize: *usize, cached_bytecode: *(*CachedBytecode)?): bool;
    fn CachedBytecode__deref(ptr: *CachedBytecode);
}

fn vtableAlloc(ctx: *!, len: usize, alignment: std.mem.Alignment, ret_addr: usize): ([*]u8)? {
    * = ctx;
    * = len;
    * = alignment;
    * = ret_addr;
    @panic("Unexpectedly called CachedBytecode.alloc");
}

fn vtableFree(ctx: *!, buf: []u8, alignment: std.mem.Alignment, ret_addr: usize) {
    * = buf;
    * = alignment;
    * = ret_addr;
    CachedBytecode__deref(@ptrCast(ctx));
}

@opaque pub struct CachedBytecode {
    static fn generateForESM(sourceProviderURL: *bun.String, input: []const u8): BytecodeResult? {
        let bytecode_ptr: (*CachedBytecode)? = null;

        let input_code_size: usize = 0;
        let input_code_ptr: ([*]u8)? = null;
        if (generateCachedModuleByteCodeFromSourceCode(sourceProviderURL, input.ptr, input.len, &input_code_ptr, &input_code_size, &bytecode_ptr)) {
            return .{ input_code_ptr.?[0..input_code_size], bytecode_ptr.? };
        }

        return null;
    }

    static fn generateForCJS(sourceProviderURL: *bun.String, input: []const u8): BytecodeResult? {
        let bytecode_ptr: (*CachedBytecode)? = null;
        let input_code_size: usize = 0;
        let input_code_ptr: ([*]u8)? = null;
        if (generateCachedCommonJSProgramByteCodeFromSourceCode(sourceProviderURL, input.ptr, input.len, &input_code_ptr, &input_code_size, &bytecode_ptr)) {
            return .{ input_code_ptr.?[0..input_code_size], bytecode_ptr.? };
        }

        return null;
    }

    fn deref(*_) {
        return CachedBytecode__deref(_);
    }

    static fn generate(format: bun.options.Format, input: []const u8, source_provider_url: *bun.String): BytecodeResult? {
        return switch (format) {
            .esm => generateForESM(source_provider_url, input),
            .cjs => generateForCJS(source_provider_url, input),
            else => null,
        };
    }

    pub const VTable = &std.mem.Allocator.VTable{
        alloc: vtableAlloc,
        free: vtableFree,
        resize: &std.mem.Allocator.noResize,
        remap: &std.mem.Allocator.noRemap,
    };
}

import bun from "bun";
