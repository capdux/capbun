#!/usr/bin/env python3
"""Fast, timeout-guarded batch test scanner for CP-Bun.

Usage:
  python3 scripts/scan_tests.py [--workers 4] [--timeout-sec 45] [--out temp/test_scan_report.txt] [targets...]
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys

SKIP_PREFIXES = (
    "test/cli/lint",
    "test/cli/format",
    "test/cli/check",
)

FILTER_RE = re.compile(r"^\(fail\)| pass$| fail$|Ran [0-9]+ tests|panic|AddressSanitizer|SIGSEGV|ASSERTION FAILED")


def collect_groups(targets):
    if targets:
        groups = []
        for t in targets:
            if os.path.isdir(t):
                # Split large directories into chunks of <= 20 files
                files = []
                for root, _, fns in os.walk(t):
                    for fn in sorted(fns):
                        if fn.endswith((".test.ts", ".test.tsx", ".test.js", ".test.jsx")):
                            p = os.path.join(root, fn)
                            if not any(p.startswith(sp) for sp in SKIP_PREFIXES):
                                files.append(p)
                files.sort()
                for i in range(0, len(files), 20):
                    chunk = files[i : i + 20]
                    groups.append((f"{t}[{i}:{i+len(chunk)}]", chunk))
            elif os.path.isfile(t):
                groups.append((t, [t]))
        return groups
    return []


def run_group(item, timeout_sec):
    label, files = item
    env = os.environ.copy()
    env["BUN_DEBUG_QUIET_LOGS"] = "1"
    cmd = ["timeout", str(timeout_sec), "./build/debug/bun-debug", "test", "--timeout=5000", *files]
    res = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    lines = [l for l in res.stdout.splitlines() if FILTER_RE.search(l)]
    return label, res.returncode, lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("targets", nargs="*")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--timeout-sec", type=int, default=45)
    parser.add_argument("--out", default="temp/test_scan_report.txt")
    args = parser.parse_args()

    groups = collect_groups(args.targets)
    if not groups:
        print("No test files found for targets.", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = {ex.submit(run_group, g, args.timeout_sec): g[0] for g in groups}
        for fut in concurrent.futures.as_completed(futs):
            results.append(fut.result())

    results.sort(key=lambda x: x[0])
    with open(args.out, "w") as f:
        for label, rc, lines in results:
            header = f"=== {label} (exit={rc}) ==="
            print(header)
            f.write(header + "\n")
            for l in lines[-25:]:
                print("  " + l)
                f.write("  " + l + "\n")
    print(f"\nReport written to {args.out} ({len(results)} chunks)")


if __name__ == "__main__":
    main()
