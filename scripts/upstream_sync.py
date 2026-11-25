#!/usr/bin/env python3
"""Upstream Rust Bun -> CP-Bun incremental sync helper.

Usage:
  python3 scripts/upstream_sync.py plan <from_ref> <to_ref> [--ref-repo ../bun-ref]
  python3 scripts/upstream_sync.py sync-tests <to_ref> [--ref-repo ../bun-ref]
  python3 scripts/upstream_sync.py verify <cp_files...>
  python3 scripts/upstream_sync.py tag <cp_tag>
"""

import argparse
import os
import subprocess
import sys
from collections import Counter

SKIP_PREFIXES = ("ci:", "ci-images:", "docs:", "test:", "release:")
SKIP_CRATES = ("`bun check`", "`bun lint`", "`bun fmt`", "bun format")

DOMAIN_GROUPS = {
    "subagent1_install_cli_sys": (
        "src/install/",
        "src/cli/",
        "src/resolver/",
        "src/sys/",
        "src/io/",
        "src/glob/",
        "src/semver/",
    ),
    "subagent2_bundler_parser_css": (
        "src/bundler/",
        "src/js_parser/",
        "src/js_printer/",
        "src/css/",
        "src/sourcemap/",
        "src/ast/",
        "src/bake/",
    ),
}


def run(cmd, cwd=None, check=True):
    res = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)
    if check and res.returncode != 0:
        sys.stderr.write(res.stderr)
        sys.exit(res.returncode)
    return res.stdout


def cmd_plan(args):
    ref = args.ref_repo
    run(["git", "-C", ref, "checkout", args.to_ref])
    log = run(["git", "-C", ref, "log", "--oneline", f"{args.from_ref}..{args.to_ref}"])
    lines = [l for l in log.splitlines() if l.strip()]
    kept = []
    for l in lines:
        msg = l.split(" ", 1)[1] if " " in l else l
        if any(msg.startswith(p) for p in SKIP_PREFIXES):
            continue
        if any(k in msg for k in SKIP_CRATES):
            continue
        kept.append(l)

    files = run(["git", "-C", ref, "diff", "--name-only", f"{args.from_ref}..{args.to_ref}", "--", "src/", "packages/", "patches/"])
    dir_counts = Counter()
    for f in files.splitlines():
        parts = f.split("/")
        if len(parts) >= 2:
            dir_counts["/".join(parts[:2])] += 1

    print(f"Range: {args.from_ref}..{args.to_ref}")
    print(f"Total commits: {len(lines)} | Actionable commits: {len(kept)}")
    print("\nTop changed directories:")
    for d, c in dir_counts.most_common(25):
        print(f"  {c:4d}  {d}")
    print("\nActionable commits (first 80):")
    for l in kept[:80]:
        print(f"  {l}")


def cmd_sync_tests(args):
    ref = args.ref_repo
    run(["git", "-C", ref, "checkout", args.to_ref])
    subprocess.run(["rsync", "-a", "--delete", f"{ref}/test/", "test/"], check=True)
    subprocess.run(["rsync", "-a", "--delete", f"{ref}/packages/bun-types/", "packages/bun-types/"], check=True)
    print(f"Synced test/ and packages/bun-types/ to {args.to_ref}")


def cmd_verify(args):
    if args.cp_files:
        subprocess.run(["python3", "scripts/cpc_brief.py", "-n", "8", *args.cp_files], check=False)
    subprocess.run(["bun", "run", "zig:check"], check=True)


def cmd_tag(args):
    subprocess.run(["git", "add", "-A"], check=True)
    subprocess.run(["git", "reset", "-q", "--", "*.zig.map"], check=True)
    subprocess.run(["git", "commit", "-m", f"sync: absorb upstream ({args.cp_tag})"], check=True)
    subprocess.run(["git", "tag", "-f", args.cp_tag], check=True)
    print(f"Committed and tagged {args.cp_tag}")


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)

    p_plan = sub.add_parser("plan")
    p_plan.add_argument("from_ref")
    p_plan.add_argument("to_ref")
    p_plan.add_argument("--ref-repo", default="../bun-ref")
    p_plan.set_defaults(func=cmd_plan)

    p_sync = sub.add_parser("sync-tests")
    p_sync.add_argument("to_ref")
    p_sync.add_argument("--ref-repo", default="../bun-ref")
    p_sync.set_defaults(func=cmd_sync_tests)

    p_ver = sub.add_parser("verify")
    p_ver.add_argument("cp_files", nargs="*")
    p_ver.set_defaults(func=cmd_verify)

    p_tag = sub.add_parser("tag")
    p_tag.add_argument("cp_tag")
    p_tag.set_defaults(func=cmd_tag)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
