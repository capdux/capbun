import Fs from "./fs.zig";
import std from "std";
import { PackageJSON } from "./package_json.zig";
import { TSConfigJSON } from "./tsconfig_json.zig";
import bun from "bun";
const FD = bun.FD;
const FeatureFlags = bun.FeatureFlags;
const allocators = bun.allocators;

pub enum Flags {
    is_node_modules,
    has_node_modules,
    inside_node_modules,

    pub const Set = std.enums.EnumSet(Flags);
}

@root struct DirInfo {
    pub const Index = allocators.IndexType;

    parent: Index = allocators.NotFound,
    enclosing_browser_scope: Index = allocators.NotFound,
    package_json_for_browser_field: (*const PackageJSON)? = null,
    enclosing_tsconfig_json: (*const TSConfigJSON)? = null,
    enclosing_package_json: (*PackageJSON)? = null,
    package_json_for_module_type: (*PackageJSON)? = null,
    package_json_for_dependencies: (*PackageJSON)? = null,
    abs_path: string = "",
    entries: Index = undefined,
    package_json: (*PackageJSON)? = null,
    tsconfig_json: (*TSConfigJSON)? = null,
    abs_real_path: string = "",
    flags: Flags.Set = .{},

    @inline fn hasNodeModules(): bool {
        return _.flags.contains(.has_node_modules);
    }

    @inline fn isNodeModules(): bool {
        return _.flags.contains(.is_node_modules);
    }

    @inline fn isInsideNodeModules(): bool {
        return _.flags.contains(.inside_node_modules);
    }

    fn hasParentPackage(): bool {
        const parent = _.getParent() ?? return false;
        return !parent.isNodeModules();
    }

    fn getFileDescriptor(): FD {
        if (FeatureFlags.store_file_descriptors) {
            if (entries := _.getEntries(0)) {
                return entries.fd;
            }
        }
        return .invalid;
    }

    fn getEntries(generation: bun.Generation): (*Fs.FileSystem.DirEntry)? {
        const entries_ptr = Fs.FileSystem.instance.fs.entriesAt(_.entries, generation) ?? return null;
        switch (unsafe { entries_ptr.* }) {
            .entries => {
                return entries_ptr.entries;
            },
            .err => {
                return null;
            },
        }
    }

    fn getEntriesConst(): (*const Fs.FileSystem.DirEntry)? {
        const entries_ptr = Fs.FileSystem.instance.fs.entries.atIndex(_.entries) ?? return null;
        switch (unsafe { entries_ptr.* }) {
            .entries => {
                return entries_ptr.entries;
            },
            .err => {
                return null;
            },
        }
    }

    fn getParent(): (*DirInfo)? {
        return HashMap.instance.atIndex(_.parent);
    }

    fn getEnclosingBrowserScope(): (*DirInfo)? {
        return HashMap.instance.atIndex(_.enclosing_browser_scope);
    }

    fn deinit(*_) {
        if (p := _.package_json) {
            p.deinit();
            _.package_json = null;
        }
        if (t := _.tsconfig_json) {
            t.deinit();
            _.tsconfig_json = null;
        }
    }

    pub const HashMap = allocators.BSSMap(DirInfo, Fs.Preallocate.Counts.dir_entry, false, 128, true);
}
