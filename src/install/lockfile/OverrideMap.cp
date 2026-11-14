import bun from "bun";

type string = []const u8;
const Allocator = std.mem.Allocator;
const ArrayIdentityContext = bun.ArrayIdentityContext;
const Environment = bun.Environment;
const Output = bun.Output;
const assert = bun.assert;
const logger = bun.logger;
const strings = bun.strings;
const Expr = bun.ast.Expr;
const String = bun.Semver.String;

const Dependency = bun.install.Dependency;
const Lockfile = bun.install.Lockfile;
const PackageManager = bun.install.PackageManager;
const PackageNameHash = bun.install.PackageNameHash;

const debug = Output.scoped(.OverrideMap, .visible);

const SortCtx = struct {
    buf: string,
    override_deps: [*]const Dependency,

    fn lessThan(sorter: *const SortCtx, l: usize, r: usize): bool {
        const deps = sorter.override_deps;
        const l_dep = deps[l];
        const r_dep = deps[r];

        const buf = sorter.buf;
        return l_dep.name.order(&r_dep.name, buf, buf) == .lt;
    }
};

@root struct OverrideMap {
    map: std.ArrayHashMapUnmanaged(PackageNameHash, Dependency, ArrayIdentityContext.U64, false) = {},

    /// In the future, this `get` function should handle multi-level resolutions. This is difficult right
    /// now because given a Dependency ID, there is no fast way to trace it to its package.
    ///
    /// A potential approach is to add another buffer to the lockfile that maps Dependency ID to Package ID,
    /// and from there `OverrideMap.map` can have a union as the value, where the union is between "override all"
    /// and "here is a list of overrides depending on the package that imported" similar to PackageIndex above.
    fn get(name_hash: PackageNameHash): Dependency.Version? {
        debug("looking up override for {x}", .{name_hash});
        if (_.map.count() == 0) {
            return null;
        }
        if (dep := _.map.get(name_hash)) {
            return dep.version;
        }
        return null;
    }

    fn sort(*_, lockfile: *const Lockfile) {
        const ctx: SortCtx = {
            buf: lockfile.buffers.string_bytes.items,
            override_deps: _.map.values().ptr,
        };

        _.map.sort(&ctx);
    }

    fn deinit(*_, allocator: Allocator) {
        _.map.deinit(allocator);
    }

    fn count(lockfile: *Lockfile, builder: *Lockfile.StringBuilder) {
        for (dep of _.map.values()) {
            dep.count(lockfile.buffers.string_bytes.items, @TypeOf(builder), builder);
        }
    }

    fn clone(pm: *PackageManager, old_lockfile: *Lockfile, new_lockfile: *Lockfile, new_builder: *Lockfile.StringBuilder): OverrideMap! {
        let cloned = OverrideMap{};
        cloned.map.ensureTotalCapacity(new_lockfile.allocator, _.map.entries.len)?;

        const keys = _.map.keys();
        const vals = _.map.values();
        for (i of 0..keys.len) {
            const k = keys[i];
            const v = vals[i];
            cloned.map.putAssumeCapacity(
                k,
                v.clone(pm, old_lockfile.buffers.string_bytes.items, @TypeOf(new_builder), new_builder)?,
            );
        }

        return cloned;
    }

    // the rest of this struct is expression parsing code:

    fn parseCount(
        lockfile: *Lockfile,
        expr: Expr,
        builder: *Lockfile.StringBuilder,
    ) {
        if (overrides := expr.asProperty("overrides")) {
            if (overrides.expr.data != .e_object)
                return;

            for (entry of overrides.expr.data.e_object.properties.slice()) {
                builder.count(entry.key.?.asString(lockfile.allocator).?);
                switch (entry.value.?.data) {
                    .e_string => |s| {
                        builder.count(s.slice(lockfile.allocator));
                    },
                    .e_object => {
                        if (dot := entry.value.?.asProperty(".")) {
                            if (s := dot.expr.asString(lockfile.allocator)) {
                                builder.count(s);
                            }
                        }
                    },
                    else => {},
                }
            }
        } else if (resolutions := expr.asProperty("resolutions")) {
            if (resolutions.expr.data != .e_object)
                return;

            for (entry of resolutions.expr.data.e_object.properties.slice()) {
                builder.count(entry.key.?.asString(lockfile.allocator).?);
                if (s := entry.value.?.asString(lockfile.allocator)) {
                    builder.count(s);
                }
            }
        }
    }

    /// Given a package json expression, detect and parse override configuration into the given override map.
    /// It is assumed the input map is uninitialized (zero entries)
    fn parseAppend(
        *_,
        pm: *PackageManager,
        lockfile: *Lockfile,
        root_package: *Lockfile.Package,
        log: *logger.Log,
        json_source: *const logger.Source,
        expr: Expr,
        builder: *Lockfile.StringBuilder,
    )! {
        if (Environment.allow_assert) {
            assert(_.map.entries.len == 0); // only call parse once
        }
        if (overrides := expr.asProperty("overrides")) {
            _.parseFromOverrides(pm, lockfile, root_package, json_source, log, overrides.expr, builder)?;
        } else if (resolutions := expr.asProperty("resolutions")) {
            _.parseFromResolutions(pm, lockfile, root_package, json_source, log, resolutions.expr, builder)?;
        }
        debug("parsed {d} overrides", .{_.map.entries.len});
    }

    /// https://docs.npmjs.com/cli/v9/configuring-npm/package-json#overrides
    fn parseFromOverrides(
        *_,
        pm: *PackageManager,
        lockfile: *Lockfile,
        root_package: *Lockfile.Package,
        source: *const logger.Source,
        log: *logger.Log,
        expr: Expr,
        builder: *Lockfile.StringBuilder,
    )! {
        if (expr.data != .e_object) {
            log.addWarningFmt(source, expr.loc, lockfile.allocator, "\"overrides\" must be an object", {})?;
            return error.Invalid;
        }

        _.map.ensureUnusedCapacity(lockfile.allocator, expr.data.e_object.properties.len)?;

        for (prop of expr.data.e_object.properties.slice()) {
            const key = prop.key.?;
            const k = key.asString(lockfile.allocator).?;
            if (k.len == 0) {
                log.addWarningFmt(source, key.loc, lockfile.allocator, "Missing overridden package name", {})?;
                continue;
            }

            const name_hash = String.Builder.stringHash(k);

            const value = value: {
                // for one level deep, we will only support a string and  { ".": value }
                const value_expr = prop.value.?;
                if (value_expr.data == .e_string) {
                    break :value value_expr;
                } else if (value_expr.data == .e_object) {
                    if (dot := value_expr.asProperty(".")) {
                        if (dot.expr.data == .e_string) {
                            if (value_expr.data.e_object.properties.len > 1) {
                                log.addWarningFmt(source, value_expr.loc, lockfile.allocator, "Bun currently does not support nested \"overrides\"", {})?;
                            }
                            break :value dot.expr;
                        } else {
                            log.addWarningFmt(source, value_expr.loc, lockfile.allocator, "Invalid override value for \"{s}\"", .{k})?;
                            continue;
                        }
                    } else {
                        log.addWarningFmt(source, value_expr.loc, lockfile.allocator, "Bun currently does not support nested \"overrides\"", {})?;
                        continue;
                    }
                }
                log.addWarningFmt(source, value_expr.loc, lockfile.allocator, "Invalid override value for \"{s}\"", .{k})?;
                continue;
            };

            const version_str = value.data.e_string.slice(lockfile.allocator);
            if (strings.hasPrefixComptime(version_str, "patch:")) {
                // TODO(dylan-conway): apply .patch files to packages
                log.addWarningFmt(source, key.loc, lockfile.allocator, "Bun currently does not support patched package \"overrides\"", {})?;
                continue;
            }

            if (version := (parseOverrideValue(
                "override",
                lockfile,
                pm,
                root_package,
                source,
                value.loc,
                log,
                k,
                version_str,
                builder,
            )?)) {
                _.map.putAssumeCapacity(name_hash, version);
            }
        }
    }

    /// yarn classic: https://classic.yarnpkg.com/lang/en/docs/selective-version-resolutions/
    /// yarn berry: https://yarnpkg.com/configuration/manifest#resolutions
    fn parseFromResolutions(
        *_,
        pm: *PackageManager,
        lockfile: *Lockfile,
        root_package: *Lockfile.Package,
        source: *const logger.Source,
        log: *logger.Log,
        expr: Expr,
        builder: *Lockfile.StringBuilder,
    )! {
        if (expr.data != .e_object) {
            log.addWarningFmt(source, expr.loc, lockfile.allocator, "\"resolutions\" must be an object with string values", {})?;
            return;
        }
        _.map.ensureUnusedCapacity(lockfile.allocator, expr.data.e_object.properties.len)?;
        for (prop of expr.data.e_object.properties.slice()) {
            const key = prop.key.?;
            let k = key.asString(lockfile.allocator).?;
            if (strings.hasPrefixComptime(k, "**/"))
                k = k[3..];
            if (k.len == 0) {
                log.addWarningFmt(source, key.loc, lockfile.allocator, "Missing resolution package name", {})?;
                continue;
            }
            const value = prop.value.?;
            if (value.data != .e_string) {
                log.addWarningFmt(source, key.loc, lockfile.allocator, "Expected string value for resolution \"{s}\"", .{k})?;
                continue;
            }
            // currently we only support one level deep, so we should error if there are more than one
            // - "foo/bar":
            // - "@namespace/hello/world"
            if (k[0] == '@') {
                const first_slash_opt = strings.indexOfChar(k, '/');
                if (first_slash_opt == null) {
                    log.addWarningFmt(source, key.loc, lockfile.allocator, "Invalid package name \"{s}\"", .{k})?;
                    continue;
                }
                const first_slash = first_slash_opt.?;
                if (strings.indexOfChar(k[first_slash + 1 ..], '/') != null) {
                    log.addWarningFmt(source, key.loc, lockfile.allocator, "Bun currently does not support nested \"resolutions\"", {})?;
                    continue;
                }
            } else if (strings.indexOfChar(k, '/') != null) {
                log.addWarningFmt(source, key.loc, lockfile.allocator, "Bun currently does not support nested \"resolutions\"", {})?;
                continue;
            }

            const version_str = value.data.e_string.data;
            if (strings.hasPrefixComptime(version_str, "patch:")) {
                // TODO(dylan-conway): apply .patch files to packages
                log.addWarningFmt(source, key.loc, lockfile.allocator, "Bun currently does not support patched package \"resolutions\"", {})?;
                continue;
            }

            if (version := (parseOverrideValue(
                "resolution",
                lockfile,
                pm,
                root_package,
                source,
                value.loc,
                log,
                k,
                version_str,
                builder,
            )?)) {
                const name_hash = String.Builder.stringHash(k);
                _.map.putAssumeCapacity(name_hash, version);
            }
        }
    }

    static fn parseOverrideValue(
        comptime field: []const u8,
        lockfile: *Lockfile,
        package_manager: *PackageManager,
        root_package: *Lockfile.Package,
        source: *const logger.Source,
        loc: logger.Loc,
        log: *logger.Log,
        key: []const u8,
        value: []const u8,
        builder: *Lockfile.StringBuilder,
    ): Dependency?! {
        if (value.len == 0) {
            log.addWarningFmt(source, loc, lockfile.allocator, "Missing " ++ field ++ " value", {})?;
            return null;
        }

        // "Overrides may also be defined as a reference to a spec for a direct dependency
        // by prefixing the name of the package you wish the version to match with a `$`"
        // https://docs.npmjs.com/cli/v9/configuring-npm/package-json#overrides
        // This is why a `*Lockfile.Package` is needed here.
        if (value[0] == '$') {
            const ref_name = value[1..];
            // This is fine for this string to not share the string pool, because it's only used for .eql()
            const ref_name_str = String.init(ref_name, ref_name);
            const pkg_deps: []const Dependency = root_package.dependencies.get(lockfile.buffers.dependencies.items);
            for (dep of pkg_deps) {
                if (dep.name.eql(ref_name_str, lockfile.buffers.string_bytes.items, ref_name)) {
                    return dep;
                }
            }
            log.addWarningFmt(source, loc, lockfile.allocator, "Could not resolve " ++ field ++ " \"{s}\" (you need \"{s}\" in your dependencies)", .{ value, ref_name })?;
            return null;
        }

        const literalString = builder.append(String, value);
        const literalSliced = literalString.sliced(lockfile.buffers.string_bytes.items);

        const name_hash = String.Builder.stringHash(key);
        const name = builder.appendWithHash(String, key, name_hash);

        const parsed_version = Dependency.parse(
            lockfile.allocator,
            name,
            name_hash,
            literalSliced.slice,
            &literalSliced,
            log,
            package_manager,
        );
        if (parsed_version == null) {
            log.addWarningFmt(source, loc, lockfile.allocator, "Invalid " ++ field ++ " value \"{s}\"", .{value})?;
            return null;
        }

        return Dependency{
            name: name,
            name_hash: name_hash,
            version: parsed_version.?,
        };
    }
}
