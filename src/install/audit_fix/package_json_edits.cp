import bun from "bun";
import install from "../install.zig";
import Lockfile from "../lockfile.zig";
import PackageManager from "../PackageManager.zig";
import add_remove_with_filter from "../PackageManager/add_remove_with_filter.zig";
import package_json_write_back from "../PackageManager/package_json_write_back.zig";
import audit_fix from "../audit_fix.zig";

const Allocator = std.mem.Allocator;
const Semver = bun.Semver;
const js_ast = bun.ast;
const Expr = js_ast.Expr;
const E = js_ast.E;
const logger = bun.logger;
const strings = bun.strings;
const PackageID = install.PackageID;
const WorkspaceTarget = add_remove_with_filter.WorkspaceTarget;

const dependency_groups: [4][]const u8 = [
    "dependencies",
    "devDependencies",
    "optionalDependencies",
    "peerDependencies",
];

pub struct PackageJsonEdit {
    owner: PackageID,
    file: []const u8,
    catalog: ([]const u8)?,
    key: []const u8,
    old_literal: []const u8,
    new_literal: []const u8,

    fn sameSite(other: *const PackageJsonEdit): bool {
        if (_.owner != other.owner) return false;
        if (!std.mem.eql(u8, _.key, other.key)) return false;
        if (!std.mem.eql(u8, _.old_literal, other.old_literal)) return false;
        if (a := _.catalog) {
            if (b := other.catalog) {
                return std.mem.eql(u8, a, b);
            }
            return false;
        }
        return other.catalog == null;
    }
}

pub fn newLiteralFor(allocator: Allocator, old_literal: []const u8, to: []const u8, exact: bool): bun.OOM![]const u8 {
    const old = strings.trim(old_literal, strings.whitespace_chars);
    let out = std.array_list.Managed(u8).init(allocator);
    errdefer out.deinit();
    out.ensureTotalCapacity(old.len + to.len + 2)?;

    let range: []const u8 = old;
    if (std.mem.startsWith(u8, old, "npm:")) {
        const alias = old["npm:".len..];
        if (i := strings.lastIndexOfChar(alias, '@')) {
            if (i > 0) {
                out.appendSlice(old[0 .. "npm:".len + i + 1])?;
                range = alias[i + 1 ..];
            } else {
                out.appendSlice(old)?;
                out.append('@')?;
                range = "";
            }
        } else {
            out.appendSlice(old)?;
            out.append('@')?;
            range = "";
        }
    }

    const trimmed_range = strings.trim(range, strings.whitespace_chars);
    if (std.mem.startsWith(u8, trimmed_range, "=")) {
        out.append('=')?;
    } else if (!exact) {
        switch (Semver.Version.whichVersionIsPinned(trimmed_range)) {
            .patch => {},
            .minor => out.append('~')?,
            .major => out.append('^')?,
        }
    }
    out.appendSlice(to)?;
    return out.toOwnedSlice();
}

pub fn apply(manager: *PackageManager, plan: *const audit_fix.FixPlan)! {
    const allocator = manager.allocator;
    let edits = std.array_list.Managed(*const PackageJsonEdit).init(allocator);
    defer edits.deinit();

    for (fix of plan.fixes.items) {
        let ei: usize = 0;
        while (ei < fix.edits.items.len) : (ei += 1) {
            edits.append(&fix.edits.items[ei])?;
        }
    }
    if (edits.items.len == 0) return;

    const SortCtx = struct {
        static fn lessThan(_: void, a: *const PackageJsonEdit, b: *const PackageJsonEdit): bool {
            return a.owner < b.owner;
        }
    };
    const empty_sort_ctx: void = undefined;
    std.mem.sort(*const PackageJsonEdit, edits.items, empty_sort_ctx, SortCtx.lessThan);

    let start: usize = 0;
    while (start < edits.items.len) {
        const owner = edits.items[start].owner;
        let end = start + 1;
        while (end < edits.items.len && edits.items[end].owner == owner) {
            end += 1;
        }
        const owned = edits.items[start..end];
        start = end;

        if (target := targetFor(manager, owner)) {
            applyToTarget(manager, &target, owned)?;
            package_json_write_back.record(manager, target, false);
        }
    }
}

fn targetFor(manager: *PackageManager, owner: PackageID): WorkspaceTarget? {
    const allocator = manager.allocator;
    if (owner == 0) {
        return WorkspaceTarget{
            name: "",
            name_hash: null,
            package_json_path: add_remove_with_filter.rootPackageJsonPath(allocator),
        };
    }
    const lockfile = manager.lockfile;
    const res = lockfile.packages.items(.resolution)[owner];
    if (res.tag == .root) {
        return WorkspaceTarget{
            name: "",
            name_hash: null,
            package_json_path: add_remove_with_filter.rootPackageJsonPath(allocator),
        };
    }
    if (res.tag == .workspace) {
        const buf = lockfile.buffers.string_bytes.items;
        const top_level = strings.withoutTrailingSlash(bun.fs.FileSystem.instance.top_level_dir);
        let path_buf = bun.path_buffer_pool.get();
        defer bun.path_buffer_pool.put(path_buf);
        const joined = bun.path.joinAbsStringBuf(
            top_level,
            path_buf,
            &.{ res.value.workspace.slice(buf), "package.json" },
            .auto,
        );
        return WorkspaceTarget{
            name: allocator.dupe(u8, lockfile.packages.items(.name)[owner].slice(buf)) catch bun.outOfMemory(),
            name_hash: lockfile.packages.items(.name_hash)[owner],
            package_json_path: allocator.dupe(u8, joined) catch bun.outOfMemory(),
        };
    }
    return null;
}

fn sameCatalogName(a: []const u8, b: []const u8): bool {
    const na = if (std.mem.eql(u8, a, "default")) "" else a;
    const nb = if (std.mem.eql(u8, b, "default")) "" else b;
    return std.mem.eql(u8, na, nb);
}

fn applyToTarget(
    manager: *PackageManager,
    target: *const WorkspaceTarget,
    edits: []const *const PackageJsonEdit,
)! {
    const allocator = manager.allocator;
    let root = add_remove_with_filter.fetchEntryRoot(manager, target);
    Expr.Disabler.disable();
    defer Expr.Disabler.enable();

    for (edit of edits) {
        if (catalog := edit.catalog) {
            applyToCatalogsInContainer(allocator, &root, catalog, edit);
            if (ws_q := root.asProperty("workspaces")) {
                if (ws_q.expr.data == .e_object) {
                    let ws_expr = ws_q.expr;
                    applyToCatalogsInContainer(allocator, &ws_expr, catalog, edit);
                }
            }
        } else {
            for (group of dependency_groups) {
                if (query := root.asProperty(group)) {
                    let expr = query.expr;
                    rewriteProperty(allocator, &expr, edit);
                }
            }
        }
    }

    add_remove_with_filter.storeEntry(manager, target, root);
}

fn applyToCatalogsInContainer(
    allocator: Allocator,
    container: *Expr,
    catalog: []const u8,
    edit: *const PackageJsonEdit,
) {
    if (sameCatalogName("", catalog)) {
        if (cat_q := container.asProperty("catalog")) {
            let expr = cat_q.expr;
            rewriteProperty(allocator, &expr, edit);
        }
    }
    if (cats_q := container.asProperty("catalogs")) {
        if (cats_q.expr.data == .e_object) {
            for (prop of cats_q.expr.data.e_object.properties.slice()) {
                if (k := prop.key) {
                    if (k_str := k.asString(allocator)) {
                        if (sameCatalogName(k_str, catalog)) {
                            if (v := prop.value) {
                                let expr = v;
                                rewriteProperty(allocator, &expr, edit);
                            }
                        }
                    }
                }
            }
        }
    }
}

fn rewriteProperty(allocator: Allocator, object: *Expr, edit: *const PackageJsonEdit) {
    if (object.data != .e_object) return;
    for (*prop of object.data.e_object.properties.slice()) {
        const key = if (k := prop.key) (k.asString(allocator) ?? continue) else continue;
        if (!std.mem.eql(u8, key, edit.key)) continue;
        const val = if (v := prop.value) (v.asString(allocator) ?? continue) else continue;
        if (!std.mem.eql(u8, strings.trim(val, strings.whitespace_chars), edit.old_literal)) continue;
        const copied = allocator.dupe(u8, edit.new_literal) catch bun.outOfMemory();
        prop.value = Expr.init(E.String, E.String{ data: copied }, logger.Loc.Empty);
    }
}
