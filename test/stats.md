# Bun CP 重构测试全量统计 (`test/stats.md`)

> 更新时间：2026-11-24 | 测试二进制：`./build/release/bun` (`037aa2dd`) & `./build/debug/bun-debug` | 全仓总测试文件：**1,728**

## 一、 已完成验证测试统计

| 测试文件 / 目录 | 二进制 | Pass | Fail | Skip / Todo | 备注 |
| :--- | :---: | :---: | :---: | :---: | :--- |
| `test/cli/install/bun-pm-pkg.test.ts` | release | 73 | 0 | 3 todo | 已修复引号/help/root检查 |
| `test/cli/install/bun-patch.test.ts` | release | 26 | 0 | - | 已修复 `Installer.cp` & `bit_set.cp` |
| `test/cli/install/bun-add.test.ts` | release | 52 | 0 | - | 已修复 `--only-missing` / `--analyze` |
| `test/cli/install/bun-pm.test.ts` | release | 12 | 0 | - | 已修复 `pm ls` & `Version.cp` `..=` |
| `test/cli/install/bun-remove.test.ts` & `bun-link.test.ts` | release | 9 | 0 | - | 全绿 (`remove` 5/0, `link` 4/0) |
| `test/cli/install/semver.test.ts` & `bun-audit.test.ts` | release | 38 | 0 | - | `semver` 22/0, `bun-audit` 16/0 |
| `test/cli/install/bun-run-dir.test.ts` & `minimum-release-age.test.ts` & `bun-pm-scan.test.ts` | release | 68 | 0 | - | 全绿 (`run-dir` 3/0, `min-age` 48/0, `pm-scan` 17/0) |
| `test/cli/install/` (`isolated-install` 46/0, `bun-publish` 34/0, `public-hoist-pattern` 14/0, `catalogs` 6/0, `native-binlink` 6/0, `config-version` 3/0, `bun-lockb` 2/0, `hoist` 1/0) | release | 112 | 0 | - | `verdaccio` 补齐后 8 文件全绿，0 崩溃 |
| `test/bake/` (24 files: 10 全绿, 9 segfault `0x23..0x32`, 5 超时/配置) | release | 89 | 49 | - | 10 文件全绿 (`dev-and-prod` 12/0, `dev/hot` 9/0 等)；9 文件共现 `0x23..0x32` segfault 排查中 |
| `test/cli/install/bunx.test.ts` | release | 29 | 3 | 1 skip | 3 timeout (`node 24` / `postinstall` / `--version` 69s~96s) |
| `test/cli/install/bun-pm-why.test.ts` & `bun-pm-version.test.ts` | debug | 52 | 0 | - | `why` 28/0, `version` 24/0 |
| `test/cli/install/bun-install-security-provider.test.ts` & `bun-security-scanner-workspaces.test.ts` | release | 46 | 0 | - | 全绿 (`provider` 43/0, `workspaces` 3/0) |
| `test/cli/install/bun-update-security-provider.test.ts` | debug | 2 | 0 | - | 全绿 |
| `test/cli/test/` (200/0), `cli/run/` (1004+, `no-orphans` 18/0), `cli/watch/` (6/0), `cli/inspect/` (7/0) | release | 1235+ | - | - | 无 `.cp` 缺陷 |
| `test/bundler/css/` + `test/bundler/esbuild/` (21 files) | release | 943 | 2 | 116 skip | 补齐 `filenamify` 后从 0/21 跃升至 943/2 |
| `test/bundler/*.test.*` (49 files) | release | 889 | 16 | 55 skip | 含 `wasm` compile SIGSEGV（排查中）及 `compile/10344` 等 |
| `test/bundler/resolver/` (3 files) | release | 16 | 0 | - | 全绿 |
| `test/js/bun/css/css.test.ts` | debug | 1032 | 0 | 67 skip | 已修复 `selector.cp` & `generics.cp` |
| `test/js/bun/css/doesnt_crash.test.ts` + `small-list-grow.test.ts` + `21907` | debug | 62 | 0 | - | 已修复 `getPrefix`/`isCompatible` 死循环 |
| `test/js/bun/css/color.test.ts` (除 fuzz) | debug | 916 | 0 | - | `fuzz ansi256` 1677万次循环 |
| `test/js/bun/json5/` + `jsonc/` | debug | 444 | 0 | - | 已修复 `json5.zig` `-0.0` & `json.cp` RAII |
| `test/js/bun/terminal/terminal-spawn.test.ts` | debug | 15 | 0 | - | 已修复 `Terminal.cp` `O_NONBLOCK` |
| `test/js/bun/shell/bunshell.test.ts` + `mock-fn.test.js` | debug | 425 | 2 | 83 todo | `0 SIGSEGV`（2 fail 为 `root` `EACCES` 与 `sharp` 超时） |
| `test/js/bun/shell/lex.test.ts` & `parse.test.ts` | debug | 46 | 0 | - | `bun:internal-for-testing` 全绿 |
| `test/js/bun/resolve/` + `expect-label` + `spyMatchers` (36 files) | release | 353 | 1 | 13 skip | 唯一 fail 为 release 无 `bun:internal-for-testing`（debug 32/0） |
| `test/js/bun/util/` (53 files) & `crypto/` (3 files) | release | 11588+ | 0 | - | `util` ~1340/0, `crypto` 10248/0 |
| `test/js/bun/md/` (1022/0), `yaml/` (560/0), `jsonl/` (269/0), `stream/` (269/0), `image/` (220/0), `udp/` (209/0), `s3/` (214/0), `sql/` (151/0), `cookie/` (147/0), `cron/` (119/0), `jsc-stress/` (111/0), `bun-object/` (106/0), `websocket/` (97/0), `sqlite/` (82/0), `jsc/` (84/0), `console/` (73/0), `typescript/` (70/0), `spawn/` (67/0), `transpiler/` (43/0), `perf*` (76/0) | release | 4096+ | 0 | - | 全部全绿 |
| `test/js/web/fetch/` (57 files) | release | 10563 | 0 | - | 全绿 |
| `test/js/web/` 其余 14 子目录 (`encoding/` 2390, `urlpattern/` 408, `workers/` 195, `html/` 123, `websocket/` 97, `streams/` 83 等) | release | 3445+ | 6 | - | 仅缺 `bun:internal-for-testing`（debug 下已验 6/6 全通）与并发 GC 波动 |
| `test/js/web/html/FormData.test.ts` | debug | 110 | 1 | - | 仅 L652 10万次同步循环在 ASan Debug 下耗时 134s 超 90s |
| `test/js/node/path/` (120), `buffer*` (532), `crypto/` (805), `util/` (513), `vm/` (208), `net/` (158), `async_hooks/` (110), `assert/` (92), `string_decoder/` (84), `child_process/` (80), `dns/` (68), `events/` (63), `module/` (60), `os/` (52), `inspector/` (43), `v8/` (43), `stream/` (41), `timers/` (24), `fs/glob` (26) | release | 3122+ | 0 | - | 0 `.cp` 缺陷 |
| `test/js/node/zlib/zlib.test.js` | release | 376 | 0 | 2 skip | `--timeout=60000` 全绿 |
| `test/js/node/url/pathToFileURL.test.ts` & `fs/abort-signal-leak` | release | 3 | 0 | - | `pathToFileURL` 2/0, `abort-signal-leak` 1/0 |
| `test/js/node/readline/` (`readline_never_unrefs` 1/0, `stdin-pause-pty` 1/0, `readline.node` 79/0) | release | 90 | 0 | - | `TERM=xterm-256color` 全绿 |
| `test/napi/` (59 files) | release | 527 | 0 | 25 todo | 0 panic / 0 SIGSEGV |
| `test/regression/issue/` (385 files) + 非 issue (2) | release | 1100+ | - | - | 0 panic / 0 SIGSEGV；`21907`/`26286`/`text-chunk` 已修复并全过 |
| `test/cli/install/bun-install-cpu-os.test.ts` | debug | 13 | 0 | - | `Tree.cp` 修复后 `bun bd test` 全绿 |
| `test/cli/run/` (31 files, 含 `require-cache` 6/0, `no-orphans` 18/0) | release | 1021 | 0 | 6 skip | `--timeout=5000` 分批全绿 |
| `test/cli/test/` (12 files) | release | 200 | 0 | 6 skip | `--timeout=5000` 全绿 |
| `test/cli/{bunfig-test-options,bun,console-depth,user-agent}.test.ts` | release | 24 | 0 | 2 skip | `--timeout=5000` 全绿 |
| `test/cli/inspect/` (4 files) | release | 34 | 0 | - | `--timeout=5000` 全绿 |
| `test/js/bun/` 已测子目录 (`sqlite` 82/0, `crypto` 10248/0, `ini/yaml/jsonl` 829/1, `cookie` 147/0, `dns` 76/0, `md/transpiler/ffi` 1080/2, `websocket/udp/net` 326/2, `console/perf_hooks/sourcemap/ts` 185/1, `wasm/archive/symbols` 102/1, `stream` 269/0, `terminal` 120/1, `http` 部分 250/0, `shell` 20 files 540/4, `s3/globals` 93/0, `fetch/cron/perf/bun-object` 226/0, `binary/jsc` 85/0, `test/` 14 files 529/1) | release | 15187 | 13 | - | `--timeout=5000` 分批扫测；`secrets` 8 项需系统密钥环 |
| `test/js/web/` (138 files) | release | 5097+ | 6 | 3 timeout | 加 `BUN_GARBAGE_COLLECTOR_LEVEL=0 BUN_FEATURE_FLAG_INTERNAL_FOR_TESTING=1` 后 `text-decoder`+`FormData`+`uv-error` 等 **192/0** |
| `test/js/node/` (248 files，不含 `zlib`/`readline`) | release | 4825+ | 14 | 4 timeout | 补齐 `internal-for-testing` 环境变量后 `v8`/`tls`/`fs` 补测 **65/0**；`tls peer cert`/`dlopen`/`http uaf` 经排查均非 `.cp` 缺陷 |
| `test/js/first_party/` (5 files) & `deno/` (16) & `junit-reporter/` (1) & `workerd/` (3) & `valkey/` (14) | release | 363 | 0 | - | 全绿（`valkey` 外部服务用例 skip） |
| `test/js/sql/` (23 files) | release | 464 | 2 | - | `unix://` 协议校验与 exotic SQL fuzz 各 1 处 |
| `test/js/third_party/` (120 files) | release | 1183 | 10 | 9 timeout | `--timeout=5000` 15s 限时扫测 |

## 二、 正在收敛中

- **`test/bake/dev/`**（9 文件）：`DevServer.cp` `JSValue.null` 已修复，即将用 `bun bd test` 验证 `bundle`/`plugins`/`vfile`/`request-cookies`。
- **`test/cli/install/` 12 个 `verdaccio` 依赖测试**：8/12 已绿，剩余 `bun-workspaces`/`npmrc`/`bun-lock` 待收敛。
