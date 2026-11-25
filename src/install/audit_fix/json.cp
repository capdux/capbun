import bun from "bun";
import audit_fix from "../audit_fix.zig";

const Output = bun.Output;
const FixPlan = audit_fix.FixPlan;
const FixOutcome = audit_fix.FixOutcome;
const PackageJsonEdit = audit_fix.PackageJsonEdit;

pub fn write(allocator: std.mem.Allocator, plan: *const FixPlan, outcome: (*const FixOutcome)?, dry_run: bool) {
    let fixed: u32 = plan.fixed_vulnerabilities;
    let remaining: u32 = plan.remaining_vulnerabilities;
    if (o := outcome) {
        fixed = o.fixed_vulnerabilities;
        remaining = o.remaining_vulnerabilities;
    }

    let out = std.array_list.Managed(u8).init(allocator);
    defer out.deinit();
    const w = out.writer();

    w.print("{{\"dryRun\":{},\"fixed\":{d},\"remaining\":{d},\"fixes\":[", [
        dry_run,
        fixed,
        remaining,
    ]) catch {};

    let i: usize = 0;
    for (fix of plan.fixes.items) {
        comma(&out, i);
        out.appendSlice("{\"name\":") catch {};
        s(&out, fix.name);
        out.appendSlice(",\"from\":") catch {};
        s(&out, fix.from_ver);
        out.appendSlice(",\"to\":") catch {};
        s(&out, fix.to);
        w.print(",\"downgrade\":{},\"newerThanMinimumReleaseAge\":{},\"packageJson\":[", [
            fix.downgrade,
            fix.too_recent,
        ]) catch {};
        let j: usize = 0;
        for (edit of fix.edits.items) {
            comma(&out, j);
            writeEdit(&out, &edit);
            j += 1;
        }
        out.appendSlice("]}") catch {};
        i += 1;
    }

    out.appendSlice("],\"blocked\":[") catch {};
    i = 0;
    for (blocked of plan.blocked.items) {
        comma(&out, i);
        out.appendSlice("{\"name\":") catch {};
        s(&out, blocked.name);
        out.appendSlice(",\"from\":") catch {};
        s(&out, blocked.from_ver);
        out.appendSlice(",\"to\":") catch {};
        s(&out, blocked.needs);
        let latest_fixes = false;
        for (blocker of blocked.blockers.items) {
            if (blocker.latest_fixes) {
                latest_fixes = true;
                break;
            }
        }
        w.print(",\"downgrade\":{},\"latestFixes\":{},\"blockers\":[", [
            blocked.needs_is_downgrade,
            latest_fixes,
        ]) catch {};
        let j: usize = 0;
        for (blocker of blocked.blockers.items) {
            comma(&out, j);
            out.appendSlice("{\"dependent\":") catch {};
            s(&out, blocker.dependent);
            out.appendSlice(",\"range\":") catch {};
            s(&out, blocker.range);
            w.print(",\"bundled\":{}}}", [blocker.bundled]) catch {};
            j += 1;
        }
        out.appendSlice("]}") catch {};
        i += 1;
    }

    out.appendSlice("],\"unfixable\":[") catch {};
    i = 0;
    for (unfixable of plan.unfixable.items) {
        comma(&out, i);
        nameAdvisories(&out, "from", unfixable.name, unfixable.from_ver, unfixable.ignore_tokens.items);
        i += 1;
    }

    out.appendSlice("],\"manifestUnavailable\":[") catch {};
    i = 0;
    for (unavailable of plan.manifest_unavailable.items) {
        comma(&out, i);
        out.appendSlice("{\"name\":") catch {};
        s(&out, unavailable.name);
        out.appendSlice(",\"from\":") catch {};
        s(&out, unavailable.from_ver);
        out.appendSlice(",\"error\":") catch {};
        s(&out, unavailable.@"error");
        out.append('}') catch {};
        i += 1;
    }

    out.appendSlice("],\"unmatched\":[") catch {};
    i = 0;
    for (unmatched of plan.unmatched.items) {
        comma(&out, i);
        out.appendSlice("{\"name\":") catch {};
        s(&out, unmatched.name);
        out.appendSlice(",\"range\":") catch {};
        s(&out, unmatched.range);
        out.append('}') catch {};
        i += 1;
    }

    out.appendSlice("],\"unaudited\":[") catch {};
    i = 0;
    for (group of plan.unaudited.items) {
        comma(&out, i);
        let registry = std.array_list.Managed(u8).init(allocator);
        defer registry.deinit();
        registry.writer().print("{f}", [bun.fmt.redactedNpmUrl(group.registry)]) catch {};
        out.appendSlice("{\"registry\":") catch {};
        s(&out, registry.items);
        out.appendSlice(",\"packages\":[") catch {};
        let j: usize = 0;
        for (pkg of group.packages.items) {
            comma(&out, j);
            s(&out, pkg);
            j += 1;
        }
        out.appendSlice("],\"reason\":") catch {};
        if (group.reason.len == 0) {
            out.appendSlice("null") catch {};
        } else {
            s(&out, group.reason);
        }
        out.append('}') catch {};
        i += 1;
    }

    out.appendSlice("],\"vulnerableAfterInstall\":[") catch {};
    if (o := outcome) {
        i = 0;
        for (item of o.still_vulnerable.items) {
            comma(&out, i);
            nameAdvisories(&out, "version", item.name, item.version, item.advisories.items);
            i += 1;
        }
    }
    out.appendSlice("]}\n") catch {};

    Output.writer().writeAll(out.items) catch {};
    Output.flush();
}

fn writeEdit(out: *std.array_list.Managed(u8), edit: *const PackageJsonEdit) {
    out.appendSlice("{\"file\":") catch {};
    s(out, edit.file);
    out.appendSlice(",\"catalog\":") catch {};
    if (cat := edit.catalog) {
        if (cat.len == 0) {
            out.appendSlice("\"default\"") catch {};
        } else {
            s(out, cat);
        }
    } else {
        out.appendSlice("null") catch {};
    }
    out.appendSlice(",\"key\":") catch {};
    s(out, edit.key);
    out.appendSlice(",\"from\":") catch {};
    s(out, edit.old_literal);
    out.appendSlice(",\"to\":") catch {};
    s(out, edit.new_literal);
    out.append('}') catch {};
}

fn nameAdvisories(
    out: *std.array_list.Managed(u8),
    second_key: []const u8,
    name: []const u8,
    second: []const u8,
    advisories: []const []const u8,
) {
    out.appendSlice("{\"name\":") catch {};
    s(out, name);
    out.appendSlice(",\"") catch {};
    out.appendSlice(second_key) catch {};
    out.appendSlice("\":") catch {};
    s(out, second);
    out.appendSlice(",\"advisories\":[") catch {};
    let j: usize = 0;
    for (token of advisories) {
        comma(out, j);
        s(out, token);
        j += 1;
    }
    out.appendSlice("]}") catch {};
}

@inline
fn comma(out: *std.array_list.Managed(u8), index: usize) {
    if (index > 0) {
        out.append(',') catch {};
    }
}

fn s(out: *std.array_list.Managed(u8), bytes: []const u8) {
    out.writer().print("{f}", [bun.fmt.formatJSONStringUTF8(bytes, {})]) catch {};
}
