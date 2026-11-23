import bun from "bun";
import std from "std";

const strings = bun.strings;
const macho = std.macho;
const BlobIndex = std.macho.BlobIndex;
const CodeDirectory = std.macho.CodeDirectory;
const SuperBlob = std.macho.SuperBlob;
const mem = std.mem;
const Allocator = mem.Allocator;

pub const SEGNAME_BUN: [16]u8 = ['_', '_', 'B', 'U', 'N', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
pub const SECTNAME: [16]u8 = ['_', '_', 'b', 'u', 'n', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

const SEG_LINKEDIT = "__LINKEDIT";

const CSMAGIC_CODEDIRECTORY: u32 = 0xfade0c02;
const CSMAGIC_EMBEDDED_SIGNATURE: u32 = 0xfade0cc0;
const CSSLOT_CODEDIRECTORY: u32 = 0;
const SEC_CODE_SIGNATURE_HASH_SHA256: u8 = 2;
const CS_EXECSEG_MAIN_BINARY: u64 = 0x1;

const IDENTIFIER = "a.out\x00";
const SIGNATURE_PAGE_SIZE: usize = 1 << 12;
const SIGNATURE_HASH_SIZE: usize = 32;

pub struct DeinitGuard<T> {
    val: T,
    active: bool = true,

    ~init() {
        if (_.active) {
            _.val.deinit();
        }
    }
}

pub struct DestroyGuard<T> {
    allocator: Allocator,
    ptr: *T!,
    active: bool = true,

    ~init() {
        if (_.active) {
            _.allocator.destroy(_.ptr);
        }
    }
}

pub struct Shifter {
    start: u64,
    amount: u64,
    linkedit_fileoff: u64,
    linkedit_filesize: u64,

    pub fn do(value: u64): u32! {
        if (value == 0) return 0;
        const range_min = _.start;
        const range_max = _.linkedit_fileoff + _.linkedit_filesize;
        if (value < range_min) return err.OffsetOutOfRange;
        if (value > range_max) return err.OffsetOutOfRange;
        if (value > std.math.maxInt(u64) - _.amount) {
            return err.OffsetOverflow;
        }
        return @as(u32, @intCast(value + _.amount));
    }
}

pub struct MachoSigner {
    data: std.array_list.Managed(u8),
    sig_off: usize,
    sig_sz: usize,
    cs_cmd_off: usize,
    linkedit_off: usize,
    linkedit_seg: macho.segment_command_64,
    text_seg: macho.segment_command_64,
    allocator: Allocator,

    pub static fn init(allocator: Allocator, obj: []const u8): (*MachoSigner!)! {
        let res = allocator.create(MachoSigner)?;
        let res_guard = DestroyGuard<MachoSigner>{ allocator: allocator, ptr: res };

        const header_size: usize = @sizeOf(macho.mach_header_64);
        const header = unsafe { std.mem.bytesAsValue(macho.mach_header_64, obj[0..header_size]).* };

        let sig_off: usize = 0;
        let sig_sz: usize = 0;
        let cs_cmd_off: usize = 0;
        let linkedit_off: usize = 0;

        let text_seg = std.mem.zeroes(macho.segment_command_64);
        let linkedit_seg = std.mem.zeroes(macho.segment_command_64);

        let it = macho.LoadCommandIterator{
            ncmds: header.ncmds,
            buffer: obj[header_size..][0..header.sizeofcmds],
        };

        while (cmd := it.next()) {
            if (cmd.cmd() == .SEGMENT_64) {
                const seg = cmd.cast(macho.segment_command_64).?;
                if (strings.eqlComptime(seg.segName(), SEG_LINKEDIT)) {
                    linkedit_seg = seg;
                    linkedit_off = unsafe { @intFromPtr(cmd.data.ptr) - @intFromPtr(obj.ptr) };
                    if (linkedit_seg.fileoff < text_seg.fileoff + text_seg.filesize) {
                        return err.InvalidLinkeditOffset;
                    }
                } else if (strings.eqlComptime(seg.segName(), "__TEXT")) {
                    text_seg = seg;
                }
            }
        }

        it = macho.LoadCommandIterator{
            ncmds: header.ncmds,
            buffer: obj[header_size..][0..header.sizeofcmds],
        };

        while (cmd := it.next()) {
            switch (cmd.cmd()) {
                .CODE_SIGNATURE => {
                    const cs = cmd.cast(macho.linkedit_data_command).?;
                    sig_off = cs.dataoff;
                    sig_sz = cs.datasize;
                    cs_cmd_off = unsafe { @intFromPtr(cmd.data.ptr) - @intFromPtr(obj.ptr) };
                },
                else => {},
            }
        }

        if (linkedit_off == 0 || sig_off == 0) {
            return err.MissingRequiredSegment;
        }

        let buf_guard = DeinitGuard<std.array_list.Managed(u8)>{
            val: std.array_list.Managed(u8).initCapacity(allocator, obj.len)?,
        };
        buf_guard.val.appendSlice(obj)?;

        unsafe {
            res.* = MachoSigner{
                data: buf_guard.val,
                sig_off: sig_off,
                sig_sz: sig_sz,
                cs_cmd_off: cs_cmd_off,
                linkedit_off: linkedit_off,
                linkedit_seg: linkedit_seg,
                text_seg: text_seg,
                allocator: allocator,
            };
        }
        buf_guard.active = false;
        res_guard.active = false;

        return res;
    }

    pub fn deinit(*_) {
        _.data.deinit();
        _.allocator.destroy(_);
    }

    pub static fn computeSignatureSize(sig_off: u64): usize {
        const page_sz: u64 = SIGNATURE_PAGE_SIZE as u64;
        const total_pages = ((sig_off + page_sz - 1) / page_sz) as usize;
        const super_blob_header_size: usize = @sizeOf(SuperBlob);
        const blob_index_size: usize = @sizeOf(BlobIndex);
        const code_dir_header_size: usize = @sizeOf(CodeDirectory);
        const hash_offset = code_dir_header_size + IDENTIFIER.len;
        const hashes_size = total_pages * SIGNATURE_HASH_SIZE;
        const code_dir_length = hash_offset + hashes_size;
        return super_blob_header_size + blob_index_size + code_dir_length;
    }

    pub fn sign(*_, writer: *std.Io.Writer): void! {
        const PAGE_SIZE: usize = SIGNATURE_PAGE_SIZE;
        const HASH_SIZE: usize = SIGNATURE_HASH_SIZE;

        const total_pages = (_.sig_off + PAGE_SIZE - 1) / PAGE_SIZE;
        const aligned_sig_off = total_pages * PAGE_SIZE;

        const id = IDENTIFIER;
        const super_blob_header_size: usize = @sizeOf(SuperBlob);
        const blob_index_size: usize = @sizeOf(BlobIndex);
        const code_dir_header_size: usize = @sizeOf(CodeDirectory);
        const id_offset = code_dir_header_size;
        const hash_offset = id_offset + id.len;

        const hashes_size = total_pages * HASH_SIZE;
        const code_dir_length = hash_offset + hashes_size;

        const sig_structure_size = super_blob_header_size + blob_index_size + code_dir_length;
        bun.debugAssert(sig_structure_size == MachoSigner.computeSignatureSize(_.sig_off as u64));
        const total_sig_size = alignSize(sig_structure_size as u64, PAGE_SIZE as u64) as usize;

        const super_blob = SuperBlob{
            magic: @byteSwap(CSMAGIC_EMBEDDED_SIGNATURE),
            length: @byteSwap(sig_structure_size as u32),
            count: @byteSwap(1 as u32),
        };

        const blob_index = BlobIndex{
            @"type": @byteSwap(CSSLOT_CODEDIRECTORY),
            offset: @byteSwap((super_blob_header_size + blob_index_size) as u32),
        };

        let code_dir = std.mem.zeroes(CodeDirectory);
        code_dir.magic = @byteSwap(CSMAGIC_CODEDIRECTORY);
        code_dir.length = @byteSwap(code_dir_length as u32);
        code_dir.version = @byteSwap(0x20400 as u32);
        code_dir.flags = @byteSwap(0x20002 as u32);
        code_dir.hashOffset = @byteSwap(hash_offset as u32);
        code_dir.identOffset = @byteSwap(id_offset as u32);
        code_dir.nSpecialSlots = 0;
        code_dir.nCodeSlots = @byteSwap(total_pages as u32);
        code_dir.codeLimit = @byteSwap(_.sig_off as u32);
        code_dir.hashSize = HASH_SIZE as u8;
        code_dir.hashType = SEC_CODE_SIGNATURE_HASH_SHA256;
        code_dir.pageSize = 12;

        const text_base = alignSize(_.text_seg.fileoff, PAGE_SIZE as u64);
        const text_limit = alignSize(_.text_seg.filesize, PAGE_SIZE as u64);
        code_dir.execSegBase = @byteSwap(text_base);
        code_dir.execSegLimit = @byteSwap(text_limit);
        code_dir.execSegFlags = @byteSwap(CS_EXECSEG_MAIN_BINARY);
        const code_dir_const: CodeDirectory = code_dir;

        _.data.resize(aligned_sig_off + total_sig_size)?;
        _.data.items.len = _.sig_off;
        @memset(_.data.unusedCapacitySlice(), 0);

        let sig_writer = _.data.writer();

        sig_writer.writeAll(mem.asBytes(&super_blob))?;
        sig_writer.writeAll(mem.asBytes(&blob_index))?;
        sig_writer.writeAll(mem.asBytes(&code_dir_const))?;
        sig_writer.writeAll(id)?;

        let remaining = _.data.items[0.._.sig_off];
        while (remaining.len >= PAGE_SIZE) {
            const page = remaining[0..PAGE_SIZE];
            let digest: bun.sha.SHA256.Digest = undefined;
            bun.sha.SHA256.hash(page, &digest, null);
            sig_writer.writeAll(&digest)?;
            remaining = remaining[PAGE_SIZE..];
        }

        if (remaining.len > 0) {
            let last_page: [4096]u8 = [0; 4096];
            @memcpy(last_page[0..remaining.len], remaining);
            let digest: bun.sha.SHA256.Digest = undefined;
            bun.sha.SHA256.hash(&last_page, &digest, null);
            sig_writer.writeAll(&digest)?;
        }

        _.data.items.len = (_.linkedit_seg.fileoff + _.linkedit_seg.filesize) as usize;
        writer.writeAll(_.data.items)?;
    }
}

pub struct MachoFile {
    header: macho.mach_header_64,
    data: std.array_list.Managed(u8),
    segment: macho.segment_command_64,
    section: macho.section_64,
    allocator: Allocator,

    pub static fn init(allocator: Allocator, obj_file: []const u8, blob_to_embed_length: usize): (*MachoFile!)! {
        let data_guard = DeinitGuard<std.array_list.Managed(u8)>{
            val: std.array_list.Managed(u8).initCapacity(allocator, obj_file.len + blob_to_embed_length)?,
        };
        data_guard.val.appendSlice(obj_file)?;

        const header = unsafe { std.mem.bytesAsValue(macho.mach_header_64, data_guard.val.items[0..@sizeOf(macho.mach_header_64)]).* };

        let res = allocator.create(MachoFile)?;
        let res_guard = DestroyGuard<MachoFile>{ allocator: allocator, ptr: res };

        unsafe {
            res.* = MachoFile{
                header: header,
                data: data_guard.val,
                segment: std.mem.zeroes(macho.segment_command_64),
                section: std.mem.zeroes(macho.section_64),
                allocator: allocator,
            };
        }
        data_guard.active = false;
        res_guard.active = false;

        return res;
    }

    pub fn deinit(*_) {
        _.data.deinit();
        _.allocator.destroy(_);
    }

    pub fn writeSection(*_, data: []const u8): void! {
        const blob_alignment: u64 = 16 * 1024;
        const PAGE_SIZE: u64 = 1 << 12;
        const HASH_SIZE: usize = 32;

        const header_size: usize = @sizeOf(u64);
        const total_size: usize = header_size + data.len;
        const aligned_size = alignSize(total_size as u64, blob_alignment);

        let original_fileoff: u64 = 0;
        let original_vmaddr: u64 = 0;
        let original_data_end: u64 = 0;
        let original_segsize: u64 = blob_alignment;

        let code_sign_cmd_idx: usize? = null;
        let linkedit_seg_idx: usize? = null;
        let found_bun = false;

        let iter = _.iterator();

        while (entry := iter.next()) {
            const cmd = entry.hdr;
            switch (cmd.cmd) {
                .SEGMENT_64 => {
                    const command = entry.cast(macho.segment_command_64).?;
                    if (strings.eqlComptime(command.segName(), "__BUN")) {
                        if (command.nsects > 0) {
                            const section_offset = unsafe { @intFromPtr(entry.data.ptr) - @intFromPtr(_.data.items.ptr) };
                            const seg_hdr_sz: usize = @sizeOf(macho.segment_command_64);
                            const sect_sz: usize = @sizeOf(macho.section_64);
                            let s_idx: usize = 0;
                            while (s_idx < command.nsects) : (s_idx += 1) {
                                const sect_off = section_offset + seg_hdr_sz + s_idx * sect_sz;
                                const sect_bytes = _.data.items[sect_off..][0..sect_sz];
                                const sect = unsafe { std.mem.bytesAsValue(macho.section_64, sect_bytes).* };
                                if (strings.eqlComptime(sect.sectName(), "__bun")) {
                                    found_bun = true;
                                    original_fileoff = sect.offset;
                                    original_vmaddr = sect.addr;
                                    original_data_end = command.fileoff + command.filesize;
                                    original_segsize = command.filesize;
                                    _.segment = command;
                                    _.section = sect;

                                    _.segment.vmsize = alignVmsize(aligned_size, blob_alignment);
                                    _.segment.filesize = aligned_size;
                                    _.segment.maxprot = macho.PROT.READ | macho.PROT.WRITE;
                                    _.segment.initprot = macho.PROT.READ | macho.PROT.WRITE;

                                    let new_sect = std.mem.zeroes(macho.section_64);
                                    new_sect.sectname = SECTNAME;
                                    new_sect.segname = SEGNAME_BUN;
                                    new_sect.addr = original_vmaddr;
                                    new_sect.size = total_size as u64;
                                    new_sect.offset = original_fileoff as u32;
                                    new_sect.@"align" = 14;
                                    new_sect.flags = macho.S_REGULAR | macho.S_ATTR_NO_DEAD_STRIP;
                                    _.section = new_sect;

                                    const seg_const = _.segment;
                                    const sect_const = _.section;
                                    @memcpy(_.data.items[section_offset..][0..seg_hdr_sz], std.mem.asBytes(&seg_const));
                                    @memcpy(sect_bytes, std.mem.asBytes(&sect_const));
                                }
                            }
                        }
                    } else if (strings.eqlComptime(command.segName(), SEG_LINKEDIT)) {
                        linkedit_seg_idx = unsafe { @intFromPtr(entry.data.ptr) - @intFromPtr(_.data.items.ptr) };
                    }
                },
                .CODE_SIGNATURE => {
                    code_sign_cmd_idx = unsafe { @intFromPtr(entry.data.ptr) - @intFromPtr(_.data.items.ptr) };
                },
                else => {},
            }
        }

        if (!found_bun) {
            return err.InvalidObject;
        }

        const size_diff = (aligned_size as i64) - (original_segsize as i64);
        const num_of_new_pages = @divExact(size_diff, PAGE_SIZE as i64);

        _.data.ensureUnusedCapacity((size_diff + num_of_new_pages * (HASH_SIZE as i64)) as usize)?;

        const linkedit_idx = linkedit_seg_idx ?? return err.MissingLinkeditSegment;

        let sig_size: usize = 0;

        const prev_data_slice = _.data.items[(original_fileoff as usize)..];
        _.data.items.len += size_diff as usize;

        const after_bun_slice = _.data.items[(original_data_end as usize) + (size_diff as usize) ..];
        const prev_after_bun_slice = prev_data_slice[(original_segsize as usize)..];
        bun.memmove(after_bun_slice, prev_after_bun_slice);

        std.mem.writeInt(u64, _.data.items[(original_fileoff as usize)..][0..8], data.len as u64, .little);
        @memcpy(_.data.items[(original_fileoff as usize) + 8 ..][0..data.len], data);

        const padding_bytes = _.data.items[(original_fileoff as usize)..][data.len + 8 .. (aligned_size as usize)];
        @memset(padding_bytes, 0);

        const cs_sz: usize = @sizeOf(macho.linkedit_data_command);
        if (cs_idx := code_sign_cmd_idx) {
            const cs_val = unsafe { std.mem.bytesAsValue(macho.linkedit_data_command, _.data.items[cs_idx..][0..cs_sz]).* };
            sig_size = cs_val.datasize;
        }

        const seg_sz: usize = @sizeOf(macho.segment_command_64);
        let linkedit_seg = unsafe { std.mem.bytesAsValue(macho.segment_command_64, _.data.items[linkedit_idx..][0..seg_sz]).* };

        if (size_diff != 0) {
            linkedit_seg.fileoff += size_diff as u64;
            linkedit_seg.vmaddr += size_diff as u64;
        }

        if (cs_idx := code_sign_cmd_idx) {
            if (_.header.cputype == macho.CPU_TYPE_ARM64 && !bun.feature_flag.BUN_NO_CODESIGN_MACHO_BINARY.get()) {
                let cs_val = unsafe { std.mem.bytesAsValue(macho.linkedit_data_command, _.data.items[cs_idx..][0..cs_sz]).* };
                const new_sig_dataoff: u64 = (cs_val.dataoff as u64) + (size_diff as u64);
                const new_sig_size = MachoSigner.computeSignatureSize(new_sig_dataoff);

                linkedit_seg.filesize = linkedit_seg.filesize - sig_size + new_sig_size;
                linkedit_seg.vmsize = alignSize(linkedit_seg.filesize, PAGE_SIZE);

                cs_val.datasize = new_sig_size as u32;
                const cs_const = cs_val;
                @memcpy(_.data.items[cs_idx..][0..cs_sz], std.mem.asBytes(&cs_const));
                sig_size = new_sig_size;
            }
        }

            const linkedit_const: macho.segment_command_64 = linkedit_seg;
        @memcpy(_.data.items[linkedit_idx..][0..seg_sz], std.mem.asBytes(&linkedit_const));

        if (size_diff != 0) {
            _.updateLoadCommandOffsets(original_fileoff, size_diff as u64, linkedit_seg.fileoff, linkedit_seg.filesize, sig_size)?;
        }

        _.validateSegments()?;
    }

    fn updateLoadCommandOffsets(*_, previous_fileoff: u64, size_diff: u64, new_linkedit_fileoff: u64, new_linkedit_filesize: u64, sig_size: usize): void! {
        if (new_linkedit_fileoff < previous_fileoff) {
            return err.InvalidLinkeditOffset;
        }

        const PAGE_SIZE: u64 = 1 << 12;
        const aligned_previous = alignSize(previous_fileoff, PAGE_SIZE);
        const aligned_linkedit = alignSize(new_linkedit_fileoff, PAGE_SIZE);

        let iter = _.iterator();

        const shifter = Shifter{
            start: aligned_previous,
            amount: size_diff,
            linkedit_fileoff: aligned_linkedit,
            linkedit_filesize: new_linkedit_filesize,
        };

        while (entry := iter.next()) {
            const cmd = entry.hdr;
            const cmd_off = unsafe { @intFromPtr(entry.data.ptr) - @intFromPtr(_.data.items.ptr) };

            switch (cmd.cmd) {
                .SYMTAB => {
                    const sz: usize = @sizeOf(macho.symtab_command);
                    let symtab = unsafe { std.mem.bytesAsValue(macho.symtab_command, _.data.items[cmd_off..][0..sz]).* };
                    symtab.symoff = shifter.do(symtab.symoff)?;
                    symtab.stroff = shifter.do(symtab.stroff)?;
                    const symtab_const = symtab;
                    @memcpy(_.data.items[cmd_off..][0..sz], std.mem.asBytes(&symtab_const));
                },
                .DYSYMTAB => {
                    const sz: usize = @sizeOf(macho.dysymtab_command);
                    let dysymtab = unsafe { std.mem.bytesAsValue(macho.dysymtab_command, _.data.items[cmd_off..][0..sz]).* };
                    dysymtab.tocoff = shifter.do(dysymtab.tocoff)?;
                    dysymtab.modtaboff = shifter.do(dysymtab.modtaboff)?;
                    dysymtab.extrefsymoff = shifter.do(dysymtab.extrefsymoff)?;
                    dysymtab.indirectsymoff = shifter.do(dysymtab.indirectsymoff)?;
                    dysymtab.extreloff = shifter.do(dysymtab.extreloff)?;
                    dysymtab.locreloff = shifter.do(dysymtab.locreloff)?;
                    const dysymtab_const = dysymtab;
                    @memcpy(_.data.items[cmd_off..][0..sz], std.mem.asBytes(&dysymtab_const));
                },
                .DYLD_CHAINED_FIXUPS |
                .CODE_SIGNATURE |
                .FUNCTION_STARTS |
                .DATA_IN_CODE |
                .DYLIB_CODE_SIGN_DRS |
                .LINKER_OPTIMIZATION_HINT |
                .DYLD_EXPORTS_TRIE => {
                    const sz: usize = @sizeOf(macho.linkedit_data_command);
                    let linkedit_cmd = unsafe { std.mem.bytesAsValue(macho.linkedit_data_command, _.data.items[cmd_off..][0..sz]).* };
                    linkedit_cmd.dataoff = shifter.do(linkedit_cmd.dataoff)?;
                    if (cmd.cmd == .CODE_SIGNATURE) {
                        linkedit_cmd.datasize = sig_size as u32;
                    }
                    const linkedit_const = linkedit_cmd;
                    @memcpy(_.data.items[cmd_off..][0..sz], std.mem.asBytes(&linkedit_const));
                },
                .DYLD_INFO | .DYLD_INFO_ONLY => {
                    const sz: usize = @sizeOf(macho.dyld_info_command);
                    let dyld_info = unsafe { std.mem.bytesAsValue(macho.dyld_info_command, _.data.items[cmd_off..][0..sz]).* };
                    dyld_info.rebase_off = shifter.do(dyld_info.rebase_off)?;
                    dyld_info.bind_off = shifter.do(dyld_info.bind_off)?;
                    dyld_info.weak_bind_off = shifter.do(dyld_info.weak_bind_off)?;
                    dyld_info.lazy_bind_off = shifter.do(dyld_info.lazy_bind_off)?;
                    dyld_info.export_off = shifter.do(dyld_info.export_off)?;
                    const dyld_const = dyld_info;
                    @memcpy(_.data.items[cmd_off..][0..sz], std.mem.asBytes(&dyld_const));
                },
                else => {},
            }
        }
    }

    pub fn iterator(): macho.LoadCommandIterator {
        return macho.LoadCommandIterator{
            buffer: _.data.items[@sizeOf(macho.mach_header_64)..][0.._.header.sizeofcmds],
            ncmds: _.header.ncmds,
        };
    }

    pub fn build(*_, writer: auto): void! {
        writer.writeAll(_.data.items)?;
    }

    fn validateSegments(*_): void! {
        let iter = _.iterator();
        let prev_end: u64 = 0;

        while (entry := iter.next()) {
            const cmd = entry.hdr;
            if (cmd.cmd == .SEGMENT_64) {
                const seg = entry.cast(macho.segment_command_64).?;
                if (seg.fileoff < prev_end) {
                    return err.OverlappingSegments;
                }
                prev_end = seg.fileoff + seg.filesize;
            }
        }
    }

    pub fn buildAndSign(*_, writer: *std.Io.Writer): void! {
        if (_.header.cputype == macho.CPU_TYPE_ARM64 && !bun.feature_flag.BUN_NO_CODESIGN_MACHO_BINARY.get()) {
            let out_data_guard = DeinitGuard<std.array_list.Managed(u8)>{
                val: std.array_list.Managed(u8).init(_.allocator),
            };
            _.build(out_data_guard.val.writer())?;
            let signer_guard = DeinitGuard<*MachoSigner!>{
                val: MachoSigner.init(_.allocator, out_data_guard.val.items)?,
            };
            signer_guard.val.sign(writer)?;
        } else {
            _.build(writer)?;
        }
    }
}

fn alignSize(size: u64, base: u64): u64 {
    const over = size % base;
    return if (over == 0) size else size + (base - over);
}

fn alignVmsize(size: u64, page_size: u64): u64 {
    const min_sz: u64 = 0x4000;
    const effective = if (size > min_sz) size else min_sz;
    return alignSize(effective, page_size);
}

pub static struct utils {
    pub fn isElf(data: []const u8): bool {
        if (data.len < 4) return false;
        return mem.readInt(u32, data[0..4], .big) == 0x7f454c46;
    }

    pub fn isMacho(data: []const u8): bool {
        if (data.len < 4) return false;
        return mem.readInt(u32, data[0..4], .little) == macho.MH_MAGIC_64;
    }
}
