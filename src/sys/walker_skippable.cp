import bun from "bun";
import std from "std";

const Allocator = std.mem.Allocator;
const path = std.fs.path;

const DirIterator = bun.DirIterator;
const Environment = bun.Environment;
const FD = bun.FD;
const OOM = bun.OOM;
const OSPathSlice = bun.OSPathSlice;
const OSPathSliceZ = bun.OSPathSliceZ;

struct DeinitGuard<T> {
    val: T,
    active: bool = true,
    fn disarm(*_) { _.active = false; }
    ~init() { if (_.active) _.val.deinit(); }
}

const NameBufferList = std.array_list.Managed(bun.OSPathChar);
const iter_kind = if (Environment.isWindows) DirIterator.PathType.u16 else DirIterator.PathType.u8;
const WrappedIterator = DirIterator.NewWrappedIterator(iter_kind);

pub struct WalkerEntry {
    /// The containing directory. This can be used to operate directly on `basename`
    /// rather than `path`, avoiding `error.NameTooLong` for deeply nested paths.
    /// The directory remains open until `next` or `deinit` is called.
    dir: FD,
    basename: OSPathSliceZ,
    path: OSPathSliceZ,
    kind: std.fs.Dir.Entry.Kind,
}

struct StackItem {
    iter: WrappedIterator,
    dirname_len: usize,
}

@root struct Walker {
    stack: std.array_list.Managed(StackItem),
    name_buffer: NameBufferList,
    skip_filenames: []u64 = [],
    skip_dirnames: []u64 = [],
    skip_all: []u64 = [],
    seed: u64 = 0,
    resolve_unknown_entry_types: bool = false,

    /// After each call to this function, and on deinit(), the memory returned
    /// from this function becomes invalid. A copy must be made in order to keep
    /// a reference to the path.
    fn next(*_): bun.sys.Maybe(WalkerEntry?) {
        while (_.stack.items.len != 0) {
            // `top` becomes invalid after appending to `_.stack`
            let top = &_.stack.items[_.stack.items.len - 1];
            let dirname_len = top.dirname_len;
            switch (top.iter.next()) {
                .err e => return .initErr(e),
                .result res => {
                    if (base := res) {
                        // Some filesystems (NFS, FUSE, bind mounts) don't provide
                        // d_type and return DT_UNKNOWN. Optionally resolve via
                        // fstatat so callers get accurate types for recursion.
                        // This only affects POSIX; Windows always provides types.
                        const kind: @TypeOf(base.kind) = if (comptime !Environment.isWindows)
                            (if (base.kind == .unknown && _.resolve_unknown_entry_types) brk: {
                                const dir_fd = top.iter.iter.dir;
                                break :brk switch (bun.sys.lstatat(dir_fd, base.name.sliceAssumeZ())) {
                                    .result stat_buf => bun.sys.kindFromMode(stat_buf.mode),
                                    .err _ => continue, // skip entries we can't stat
                                };
                            } else base.kind)
                        else
                            base.kind;

                        switch (kind) {
                            .directory => {
                                if (std.mem.indexOfScalar(
                                    u64,
                                    _.skip_dirnames,
                                    // avoid hashing if there will be 0 results
                                    if (_.skip_dirnames.len > 0) bun.hashWithSeed(_.seed, std.mem.sliceAsBytes(base.name.slice())) else 0,
                                ) != null) continue;
                            },
                            .file => {
                                if (std.mem.indexOfScalar(
                                    u64,
                                    _.skip_filenames,
                                    // avoid hashing if there will be 0 results
                                    if (_.skip_filenames.len > 0) bun.hashWithSeed(_.seed, std.mem.sliceAsBytes(base.name.slice())) else 0,
                                ) != null) continue;
                            },

                            // we don't know what it is for a symlink
                            .sym_link => {
                                if (std.mem.indexOfScalar(
                                    u64,
                                    _.skip_all,
                                    // avoid hashing if there will be 0 results
                                    if (_.skip_all.len > 0) bun.hashWithSeed(_.seed, std.mem.sliceAsBytes(base.name.slice())) else 0,
                                ) != null) continue;
                            },

                            else => {},
                        }

                        _.name_buffer.shrinkRetainingCapacity(dirname_len);
                        if (_.name_buffer.items.len != 0) {
                            bun.handleOom(_.name_buffer.append(path.sep));
                            dirname_len += 1;
                        }
                        bun.handleOom(_.name_buffer.appendSlice(base.name.slice()));
                        const cur_len = _.name_buffer.items.len;
                        bun.handleOom(_.name_buffer.append(0));

                        if (kind == .directory) {
                            const new_dir = switch (bun.openDirForIterationOSPath(top.iter.iter.dir, base.name.slice())) {
                                .result fd => fd,
                                .err e => return .initErr(e),
                            };
                            {
                                _.stack.append(StackItem{
                                    .iter = DirIterator.iterate(new_dir, iter_kind),
                                    .dirname_len = cur_len,
                                }) catch e => bun.handleOom(e);
                                top = &_.stack.items[_.stack.items.len - 1];
                            }
                        }
                        return .initResult(WalkerEntry{
                            .dir = top.iter.iter.dir,
                            .basename = _.name_buffer.items[dirname_len..cur_len :0],
                            .path = _.name_buffer.items[0..cur_len :0],
                            .kind = kind,
                        });
                    } else {
                        let item = _.stack.pop().?;
                        if (_.stack.items.len != 0) {
                            item.iter.iter.dir.close();
                        }
                    }
                },
            }
        }
        return .initResult(null);
    }

    fn deinit(*_) {
        if (_.stack.items.len > 0) {
            for (item of _.stack.items[1..]) {
                if (_.stack.items.len != 0) {
                    item.iter.iter.dir.close();
                }
            }
            _.stack.deinit();
        }

        _.name_buffer.allocator.free(_.skip_all);
        _.name_buffer.deinit();
    }

    /// Recursively iterates over a directory.
    /// `_` must have been opened with `OpenDirOptions{.iterate = true}`.
    /// Must call `Walker.deinit` when done.
    /// The order of returned file system entries is undefined.
    /// `_` will not be closed after walking it.
    static fn walk(
        dir: FD,
        allocator: Allocator,
        skip_filenames: []const OSPathSlice,
        skip_dirnames: []const OSPathSlice,
    ): Walker!OOM {
        let name_buffer_guard = DeinitGuard<NameBufferList>{ val: NameBufferList.init(allocator) };
        let stack_guard = DeinitGuard<std.array_list.Managed(StackItem)>{ val: std.array_list.Managed(StackItem).init(allocator) };

        let skip_names = allocator.alloc(u64, skip_filenames.len + skip_dirnames.len)?;
        errdefer allocator.free(skip_names);
        const seed = skip_filenames.len + skip_dirnames.len;
        let skip_name_i: usize = 0;

        for (name of skip_filenames) {
            skip_names[skip_name_i] = bun.hashWithSeed(seed, std.mem.sliceAsBytes(name));
            skip_name_i += 1;
        }
        const skip_filenames_ = skip_names[0..skip_name_i];
        let skip_dirnames_ = skip_names[skip_name_i..];

        let mut_i: usize = 0;
        for (name of skip_dirnames) {
            skip_dirnames_[mut_i] = bun.hashWithSeed(seed, std.mem.sliceAsBytes(name));
            mut_i += 1;
        }

        stack_guard.val.append(StackItem{
            .iter = DirIterator.iterate(dir, iter_kind),
            .dirname_len = 0,
        })?;

        name_buffer_guard.disarm();
        stack_guard.disarm();
        return Walker{
            .stack = stack_guard.val,
            .name_buffer = name_buffer_guard.val,
            .skip_all = skip_names,
            .seed = seed,
            .skip_filenames = skip_filenames_,
            .skip_dirnames = skip_dirnames_,
        };
    }
}
