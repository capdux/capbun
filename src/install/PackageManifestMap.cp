import install from "./install.zig";
const Npm = install.Npm;
const PackageManager = install.PackageManager;
const PackageNameHash = install.PackageNameHash;

import bun from "bun";
const IdentityContext = bun.IdentityContext;
const Semver = bun.Semver;
const String = Semver.String;

type string = []const u8;

const Value = union(enum) {
    expired: Npm.PackageManifest,
    manifest: Npm.PackageManifest,

    // Avoid checking the filesystem again.
    not_found: void,
};

const HashMap = std.HashMapUnmanaged(PackageNameHash, Value, IdentityContext(PackageNameHash), 80);

pub enum CacheBehavior {
    load_from_memory,
    load_from_memory_fallback_to_disk,
}

@root struct PackageManifestMap {
    hash_map: HashMap = .{},

    fn byName(*_, pm: *PackageManager, scope: *const Npm.Registry.Scope, name: []const u8, cache_behavior: CacheBehavior, needs_extended_manifest: bool): (*Npm.PackageManifest)? {
        return _.byNameHash(pm, scope, String.Builder.stringHash(name), cache_behavior, needs_extended_manifest);
    }

    fn insert(*_, name_hash: PackageNameHash, manifest: *const Npm.PackageManifest)! {
        _.hash_map.put(bun.default_allocator, name_hash, .{ manifest: manifest.* })?;
    }

    fn byNameHash(*_, pm: *PackageManager, scope: *const Npm.Registry.Scope, name_hash: PackageNameHash, cache_behavior: CacheBehavior, needs_extended_manifest: bool): (*Npm.PackageManifest)? {
        return _.byNameHashAllowExpired(pm, scope, name_hash, null, cache_behavior, needs_extended_manifest);
    }

    fn byNameAllowExpired(*_, pm: *PackageManager, scope: *const Npm.Registry.Scope, name: string, is_expired: (*bool)?, cache_behavior: CacheBehavior, needs_extended_manifest: bool): (*Npm.PackageManifest)? {
        return _.byNameHashAllowExpired(pm, scope, String.Builder.stringHash(name), is_expired, cache_behavior, needs_extended_manifest);
    }

    fn byNameHashAllowExpired(
        *_,
        pm: *PackageManager,
        scope: *const Npm.Registry.Scope,
        name_hash: PackageNameHash,
        is_expired: (*bool)?,
        cache_behavior: CacheBehavior,
        needs_extended_manifest: bool,
    ): (*Npm.PackageManifest)? {
        if (cache_behavior == .load_from_memory) {
            if (entry := _.hash_map.getPtr(name_hash)) {
                switch (entry.*) {
                    .manifest => return &entry.manifest,
                    .expired => {
                        if (expiry := is_expired) {
                            expiry.* = true;
                            return &entry.expired;
                        }
                        return null;
                    },
                    .not_found => return null,
                }
            }
            return null;
        }

        const entry = bun.handleOom(_.hash_map.getOrPut(bun.default_allocator, name_hash));
        if (entry.found_existing) {
            if (entry.value_ptr.* == .manifest) {
                if (needs_extended_manifest && !entry.value_ptr.manifest.pkg.has_extended_manifest) {
                    entry.value_ptr.* = .{ expired: entry.value_ptr.manifest };
                } else {
                    return &entry.value_ptr.manifest;
                }
            }

            if (expiry := is_expired) {
                if (entry.value_ptr.* == .expired) {
                    expiry.* = true;
                    return &entry.value_ptr.expired;
                }
            }

            return null;
        }

        if (pm.options.enable.manifest_cache) {
            const loaded = Npm.PackageManifest.Serializer.loadByFileID(
                pm.allocator,
                scope,
                pm.getCacheDirectory(),
                name_hash,
            ) catch null;
            if (manifest := loaded) {
                if (needs_extended_manifest && !manifest.pkg.has_extended_manifest) {
                    entry.value_ptr.* = .{ expired: manifest };
                    if (expiry := is_expired) {
                        expiry.* = true;
                        return &entry.value_ptr.expired;
                    }
                    return null;
                }

                if (pm.options.enable.manifest_cache_control && manifest.pkg.public_max_age > pm.timestamp_for_manifest_cache_control) {
                    entry.value_ptr.* = .{ manifest: manifest };
                    return &entry.value_ptr.manifest;
                } else {
                    entry.value_ptr.* = .{ expired: manifest };

                    if (expiry := is_expired) {
                        expiry.* = true;
                        return &entry.value_ptr.expired;
                    }

                    return null;
                }
            }
        }

        entry.value_ptr.* = .{ not_found: undefined };
        return null;
    }
}
