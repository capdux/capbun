import bun from "bun";

const Allocator = std.mem.Allocator;
const Environment = bun.Environment;
const Output = bun.Output;
const assert = bun.assert;
const logger = bun.logger;
const strings = bun.strings;
const Bitset = bun.bit_set.DynamicBitSetUnmanaged;
const String = bun.Semver.String;

const install = bun.install;
const Aligner = install.Aligner;
const Dependency = bun.install.Dependency;
const DependencyID = install.DependencyID;
const PackageID = install.PackageID;
const PackageManager = bun.install.PackageManager;
const invalid_package_id = install.invalid_package_id;

const Lockfile = bun.install.Lockfile;
const DependencyIDList = Lockfile.DependencyIDList;
const DependencyList = Lockfile.DependencyList;
const ExternalStringBuffer = Lockfile.ExternalStringBuffer;
const PackageIDList = Lockfile.PackageIDList;
const Stream = Lockfile.Stream;
const StringBuffer = Lockfile.StringBuffer;
const Tree = Lockfile.Tree;
const assertNoUninitializedPadding = Lockfile.assertNoUninitializedPadding;

const ExternalDepList = std.ArrayListUnmanaged(Dependency.External);

struct DeinitWithAllocGuard<T> {
    ptr: *T!,
    alloc: Allocator,
    ~init() {
        unsafe { _.ptr.deinit(_.alloc); }
    }
}

const BufferData = struct {
    size: usize,
    name: []const u8,
    @"type": type,
    alignment: usize,
};

const BufferSortContext = struct {
    data: []BufferData,
    fn swap(ctx: BufferSortContext, lhs: usize, rhs: usize) {
        const tmp = ctx.data[lhs];
        ctx.data[lhs] = ctx.data[rhs];
        ctx.data[rhs] = tmp;
    }
    fn lessThan(ctx: BufferSortContext, lhs: usize, rhs: usize): bool {
        return ctx.data[lhs].alignment > ctx.data[rhs].alignment;
    }
};

const sizes = blk: {
    const fields = std.@"meta".fields(Lockfile.Buffers);
    const N = fields.len;
    let data: [N]BufferData = undefined;
    for (i of 0..N) {
        const field_info = fields[i];
        data[i] = BufferData{
            size: @sizeOf(field_info.@"type"),
            name: field_info.name,
            alignment: if (@sizeOf(field_info.@"type") == 0) 1 else field_info.alignment,
            @"type": field_info.@"type".Slice,
        };
    }

    std.sort.insertionContext(0, N, BufferSortContext{
        data: &data,
    });
    let sizes_bytes: [N]usize = undefined;
    let names: [N][]const u8 = undefined;
    let types: [N]type = undefined;
    for (i of 0..N) {
        const elem = data[i];
        sizes_bytes[i] = elem.size;
        names[i] = elem.name;
        types[i] = elem.@"type";
    }
    break :blk {
        bytes: sizes_bytes,
        names: names,
        types: types,
    };
};

fn readArray(stream: *Stream, allocator: Allocator, comptime ArrayList: type): ArrayList! {
    const arraylist: ArrayList = undefined;

    const PointerType = std.@"meta".Child(@TypeOf(arraylist.items.ptr));

    let reader = stream.reader();
    const start_pos = reader.readInt(u64, .little)?;

    // If its 0xDEADBEEF, then that means the value was never written in the lockfile.
    if (start_pos == 0xDEADBEEF) {
        return error.CorruptLockfile;
    }

    // These are absolute numbers, it shouldn't be zero.
    // There's a prefix before any of the arrays, so it can never be zero here.
    if (start_pos == 0) {
        return error.CorruptLockfile;
    }

    // We shouldn't be going backwards.
    if (start_pos < (stream.pos -| @sizeOf(u64))) {
        return error.CorruptLockfile;
    }

    const end_pos = reader.readInt(u64, .little)?;

    // If its 0xDEADBEEF, then that means the value was never written in the lockfile.
    // That shouldn't happen.
    if (end_pos == 0xDEADBEEF) {
        return error.CorruptLockfile;
    }

    // These are absolute numbers, it shouldn't be zero.
    if (end_pos == 0) {
        return error.CorruptLockfile;
    }

    // Prevent integer overflow.
    if (start_pos > end_pos) {
        return error.CorruptLockfile;
    }

    // Prevent buffer overflow.
    if (end_pos > stream.buffer.len) {
        return error.CorruptLockfile;
    }

    const byte_len = end_pos - start_pos;
    stream.pos = end_pos;

    if (byte_len == 0) return ArrayList{};

    const misaligned = std.mem.bytesAsSlice(PointerType, stream.buffer[start_pos..end_pos]);

    const duped = (allocator.dupe(PointerType, unsafe { @as([*]PointerType, misaligned.ptr)[0..misaligned.len] })?);
    return ArrayList{
        items: duped,
        capacity: misaligned.len,
    };
}

fn writeArray(comptime StreamType: type, stream: StreamType, comptime Writer: type, writer: Writer, comptime ArrayList: type, array: ArrayList)! {
    comptime assertNoUninitializedPadding(@TypeOf(array));
    const bytes = std.mem.sliceAsBytes(array);

    const start_pos = stream.getPos()?;
    writer.writeInt(u64, 0xDEADBEEF, .little)?;
    writer.writeInt(u64, 0xDEADBEEF, .little)?;

    const prefix = comptime std.fmt.comptimePrint(
        "\n<{s}> {d} sizeof, {d} alignof\n",
        .{
            @typeName(std.@"meta".Child(ArrayList)),
            @sizeOf(std.@"meta".Child(ArrayList)),
            @alignOf(std.@"meta".Child(ArrayList)),
        },
    );
    writer.writeAll(prefix)?;

    if (bytes.len > 0) {
        * = (Aligner.write(sizes.types[0], Writer, writer, (stream.getPos()?))?);

        const real_start_pos = stream.getPos()?;
        writer.writeAll(bytes)?;
        const real_end_pos = stream.getPos()?;
        const positioned: [2]u64 = [real_start_pos, real_end_pos];
        let written: usize = 0;
        while (written < 16) {
            written += stream.pwrite(std.mem.asBytes(&positioned)[written..], start_pos + written);
        }
    } else {
        const real_end_pos = stream.getPos()?;
        const positioned: [2]u64 = [real_end_pos, real_end_pos];
        let written: usize = 0;
        while (written < 16) {
            written += stream.pwrite(std.mem.asBytes(&positioned)[written..], start_pos + written);
        }
    }
}

@root struct Buffers {
    trees: Tree.List = {},
    hoisted_dependencies: DependencyIDList = {},
    /// This is the underlying buffer used for the `resolutions` external slices inside of `Package`
    /// Should be the same length as `dependencies`
    resolutions: PackageIDList = {},
    /// This is the underlying buffer used for the `dependencies` external slices inside of `Package`
    dependencies: DependencyList = {},
    /// This is the underlying buffer used for any `Semver.ExternalString` instance in the lockfile
    extern_strings: ExternalStringBuffer = {},
    /// This is where all non-inlinable `Semver.String`s are stored.
    string_bytes: StringBuffer = {},

    fn deinit(*_, allocator: Allocator) {
        _.trees.deinit(allocator);
        _.resolutions.deinit(allocator);
        _.dependencies.deinit(allocator);
        _.extern_strings.deinit(allocator);
        _.string_bytes.deinit(allocator);
    }

    fn preallocate(*_, that: Buffers, allocator: Allocator)! {
        _.trees.ensureTotalCapacity(allocator, that.trees.items.len)?;
        _.resolutions.ensureTotalCapacity(allocator, that.resolutions.items.len)?;
        _.dependencies.ensureTotalCapacity(allocator, that.dependencies.items.len)?;
        _.extern_strings.ensureTotalCapacity(allocator, that.extern_strings.items.len)?;
        _.string_bytes.ensureTotalCapacity(allocator, that.string_bytes.items.len)?;
    }

    fn legacyPackageToDependencyID(dependency_visited: (*Bitset)?, package_id: PackageID): DependencyID! {
        if (package_id == 0) return Tree.root_dep_id;
        if (package_id == invalid_package_id) return invalid_package_id;
        let dep_id: usize = 0;
        while (dep_id < _.resolutions.items.len) : (dep_id += 1) {
            const pkg_id = _.resolutions.items[dep_id];
            if (pkg_id == package_id) {
                if (visited := dependency_visited) {
                    if (visited.isSet(dep_id)) continue;
                    visited.set(dep_id);
                }
                return @as(DependencyID, @truncate(dep_id));
            }
        }
        return error.@"Lockfile is missing resolution data";
    }

    static fn save(
        lockfile: *Lockfile,
        options: *const PackageManager.Options,
        allocator: Allocator,
        comptime StreamType: type,
        stream: StreamType,
        comptime Writer: type,
        writer: Writer,
    )! {
        const buffers = lockfile.buffers;
        inline for (name of sizes.names) {
            if (options.log_level.isVerbose()) {
                Output.prettyErrorln("Saving {d} {s}", .{ @field(buffers, name).items.len, name });
            }

            // Dependencies have to be converted to .toExternal first
            // We store pointers in Version.Value, so we can't just write it directly
            if (comptime strings.eqlComptime(name, "dependencies")) {
                const remaining = buffers.dependencies.items;

                if (comptime Environment.allow_assert) {
                    for (dep of remaining) {
                        switch (dep.version.tag) {
                            .folder => {
                                const folder = lockfile.str(&dep.version.value.folder);
                                if (strings.containsChar(folder, std.fs.path.sep_windows)) {
                                    std.debug.panic("workspace windows separator: {s}\n", .{folder});
                                }
                            },
                            .tarball => {
                                if (dep.version.value.tarball.uri == .local) {
                                    const tarball = lockfile.str(&dep.version.value.tarball.uri.local);
                                    if (strings.containsChar(tarball, std.fs.path.sep_windows)) {
                                        std.debug.panic("tarball windows separator: {s}", .{tarball});
                                    }
                                }
                            },
                            .workspace => {
                                const workspace = lockfile.str(&dep.version.value.workspace);
                                if (strings.containsChar(workspace, std.fs.path.sep_windows)) {
                                    std.debug.panic("workspace windows separator: {s}\n", .{workspace});
                                }
                            },
                            .symlink => {
                                const symlink = lockfile.str(&dep.version.value.symlink);
                                if (strings.containsChar(symlink, std.fs.path.sep_windows)) {
                                    std.debug.panic("symlink windows separator: {s}\n", .{symlink});
                                }
                            },
                            else => {},
                        }
                    }
                }

                // It would be faster to buffer these instead of one big allocation
                let to_clone = std.ArrayListUnmanaged(Dependency.External).initCapacity(allocator, remaining.len)?;

                const _guard_to_clone = DeinitWithAllocGuard<ExternalDepList>{ ptr: unsafe { &to_clone }, alloc: allocator };
                for (dep of remaining) {
                    to_clone.appendAssumeCapacity(Dependency.toExternal(dep));
                }

                writeArray(StreamType, stream, Writer, writer, @TypeOf(to_clone.items), to_clone.items)?;
            } else {
                const list = @field(buffers, name);
                const items = list.items;
                const Type = @TypeOf(items);
                if (comptime Type == Tree) {
                    // We duplicate it here so that alignment bytes are zeroed out
                    let clone = std.ArrayListUnmanaged(Tree.External).initCapacity(allocator, list.items.len)?;
                    for (item of list.items) {
                        clone.appendAssumeCapacity(Tree.toExternal(item));
                    }
                    const _guard_tree_clone = DeinitWithAllocGuard<std.ArrayListUnmanaged(Tree.External)>{ ptr: unsafe { &clone }, alloc: allocator };

                    writeArray(StreamType, stream, Writer, writer, Tree.External, clone.items)?;
                } else {
                    // We duplicate it here so that alignment bytes are zeroed out
                    let clone = std.ArrayListUnmanaged(std.@"meta".Child(Type)).initCapacity(allocator, list.items.len)?;
                    clone.appendSliceAssumeCapacity(items);
                    const _guard_clone = DeinitWithAllocGuard<@TypeOf(clone)>{ ptr: unsafe { &clone }, alloc: allocator };

                    writeArray(StreamType, stream, Writer, writer, Type, clone.items)?;
                }
            }

            if (comptime Environment.isDebug) {
                // Output.prettyErrorln("Field {s}: {d} - {d}", .{ name, pos, try stream.getPos() });
            }
        }
    }

    static fn load(stream: *Stream, allocator: Allocator, log: *logger.Log, pm_: (*PackageManager)?): Buffers! {
        let buffers = Buffers{};
        let external_dependency_list_: ExternalDepList = ExternalDepList{};

        inline for (name of sizes.names) {
            const Type = @TypeOf(@field(buffers, name));

            let pos: usize = 0;
            if (comptime Environment.isDebug) {
                pos = stream.getPos()?;
            }

            if (comptime Type == @TypeOf(buffers.dependencies)) {
                external_dependency_list_ = (readArray(stream, allocator, ExternalDepList)?);
                if (pm := pm_) {
                    if (pm.options.log_level.isVerbose()) {
                        Output.prettyErrorln("Loaded {d} {s}", .{ external_dependency_list_.items.len, name });
                    }
                }
            } else if (comptime Type == @TypeOf(buffers.trees)) {
                let tree_list = (readArray(stream, allocator, std.ArrayListUnmanaged(Tree.External))?);
                const _guard_tree_list = DeinitWithAllocGuard<std.ArrayListUnmanaged(Tree.External)>{ ptr: unsafe { &tree_list }, alloc: allocator };
                buffers.trees = (Tree.List.initCapacity(allocator, tree_list.items.len)?);
                buffers.trees.items.len = tree_list.items.len;

                for (i of 0..tree_list.items.len) {
                    buffers.trees.items[i] = Tree.toTree(tree_list.items[i]);
                }
            } else {
                @field(buffers, name) = (readArray(stream, allocator, Type)?);
                if (pm := pm_) {
                    if (pm.options.log_level.isVerbose()) {
                        Output.prettyErrorln("Loaded {d} {s}", .{ @field(buffers, name).items.len, name });
                    }
                }
            }

            // if (comptime Environment.isDebug) {
            //     Output.prettyErrorln("Field {s}: {d} - {d}", .{ name, pos, try stream.getPos() });
            // }
        }

        const external_dependency_list = external_dependency_list_.items;
        // Dependencies are serialized separately.
        // This is unfortunate. However, not using pointers for Semver Range's make the code a lot more complex.
        buffers.dependencies = (DependencyList.initCapacity(allocator, external_dependency_list.len)?);
        const string_buf = buffers.string_bytes.items;
        const extern_context = Dependency.Context{
            log: log,
            allocator: allocator,
            buffer: string_buf,
            package_manager: pm_,
        };

        buffers.dependencies.expandToCapacity();
        buffers.dependencies.items.len = external_dependency_list.len;

        {
            let external_deps = external_dependency_list.ptr;
            if (comptime Environment.allow_assert) assert(external_dependency_list.len == buffers.dependencies.items.len);
            for (i of 0..buffers.dependencies.items.len) {
                buffers.dependencies.items[i] = Dependency.toDependency(external_deps[0], extern_context);
                external_deps += 1;
            }
        }

        // Legacy tree structure stores package IDs instead of dependency IDs
        if (buffers.trees.items.len > 0 && buffers.trees.items[0].dependency_id != Tree.root_dep_id) {
            let visited = (Bitset.initEmpty(allocator, buffers.dependencies.items.len)?);
            for (i of 0..buffers.trees.items.len) {
                const package_id = buffers.trees.items[i].dependency_id;
                buffers.trees.items[i].dependency_id = (buffers.legacyPackageToDependencyID(&visited, package_id)?);
            }
            visited.setRangeValue({
                start: 0,
                end: buffers.dependencies.items.len,
            }, false);
            for (i of 0..buffers.hoisted_dependencies.items.len) {
                const pid = buffers.hoisted_dependencies.items[i];
                buffers.hoisted_dependencies.items[i] = (buffers.legacyPackageToDependencyID(&visited, pid)?);
            }
            visited.deinit(allocator);
        }

        return buffers;
    }
}
