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
| `test/js/bun/` 已测子目录 (`sqlite` 82/0, `crypto` 10248/0, `yaml` 560/0, `ini` 52/0, `jsonl` 269/0, `cookie` 147/0, `dns` 76/0, `md` 1022/0, `transpiler` 43/0, `io` 36/0, `sys` 1/0, `s3` 70/0, `ffi` 2 fail, `websocket` 97/0, `udp` 209/0, `net/tcp-server` 8/0, `webview` 6/0, `console/perf_hooks/sourcemap/ts` 185/1, `wasm/archive/symbols` 102/1, `stream` 269/0, `terminal` 120/1, `http` 已测 857/4, `shell` 20 files 586/2, `globals` 23/0, `fetch/cron/perf/bun-object` 425/0, `binary/jsc` 85/0, `test/` 45+ files 1069/0, `spawn/` 24 files 120/0, `glob/` 3 files 193/2, `util/` 53 files 1757/0, `plugin/` 30/0, `repl/` 105/0, `import-attributes/` 12/0, `image/` 220/0) | release (新版) | 19186 | 12 | - | `js/bun/test/` 45+ 文件全绿、`md/transpiler/io/sys/s3` 24 文件全绿、`util/` 53 文件全绿、`bun-object/perf_hooks/cron` 全绿 |
| `test/cli/watch/` (2) & `test/cli/env/ci-info.test.ts` & `test/cli/init/` (1) | release (新版) | 20 | 0 | - | `--timeout=10000` 全绿 |
| `test/js/web/` (138 files) | release | 5097+ | 6 | 3 timeout | 加 `BUN_GARBAGE_COLLECTOR_LEVEL=0 BUN_FEATURE_FLAG_INTERNAL_FOR_TESTING=1` 后 `text-decoder`+`FormData`+`uv-error` 等 **192/0** |
| `test/js/node/` (248 files，不含 `zlib`/`readline`) | release | 4825+ | 14 | 4 timeout | 补齐 `internal-for-testing` 环境变量后 `v8`/`tls`/`fs` 补测 **65/0**；`tls peer cert`/`dlopen`/`http uaf` 经排查均非 `.cp` 缺陷 |
| `test/js/first_party/` (5 files) & `deno/` (16) & `junit-reporter/` (1) & `workerd/` (3) & `valkey/` (14) | release | 363 | 0 | - | 全绿（`valkey` 外部服务用例 skip） |
| `test/js/sql/` (23 files) | release | 466 | 0 | - | 清理残留 `/tmp/thisisacoolmysql.sock` 并 `--timeout=15000` 后全绿（`adapter`+`sqlite-sql` **269/0**） |
| `test/js/third_party/` (120 files) | release | 1183 | 10 | 9 timeout | `--timeout=5000` 15s 限时扫测 |

| `test/bundler/css/` (8 files) & `esbuild/` (13 files) | release (新版) | 945 | 0 | - | `findImportedFilesInCSSOrder.cp` + `maybe.cp` 修复后 21 文件全绿 |
| `test/bundler/bundler_{cjs2esm,html,css}.test.ts` | release (新版) | 37 | 0 | - | `computeChunks.cp` + `maybe.cp` 修复后全绿 |
| `test/bundler/` 其余 47 文件 (`3f8dcd1c` 并发扫测) | release (新版) | 823+ | 0 | 2 timeout | **45/47 文件 0 失败**；`Blob.cp` 修复后 `bun-build-compile` **11/0**、`compile-wasm` **1/0**、`compile-sourcemap` **9/0**、`bundler_compile` 嵌入式/10344 **9/0** |
| `test/js/bun/css/css-fuzz.test.ts` | release (新版) | 5 | 0 | - | `css_parser.cp` 递归深度保护后 `deepNesting(300)` 无 SIGSEGV 全绿 |
| `test/cli/hot/` (3 files) | release (新版) | 15 | 0 | - | `ModuleLoader.cp` 修复后全绿 |
| `test/cli/install/bun-install-cpu-os.test.ts` & `bun-lock.test.ts` | release (新版) | 22 | 0 | - | `Tree.cp` 修复 + `internal-for-testing` 环境变量后全绿 |
| `test/cli/install/` 其余 51 文件 (加前序 10 文件共 61 文件) | release (新版) | 1979+ | 22 | 2 timeout | `WorkspaceMap.cp`+`Tree.cp`+`bun.lock.cp`+`pm_cmd.cp` 修复后：`lifecycle-scripts` **103/0**、`bun-workspaces`+`registry` **286/0**、`scanner-matrix-without-nm` **688/0**、`scanner-matrix-with-nm` **720/0**、`bun-install` `bin-linking` **1/0** |
| `test/bake/dev/{bundle,plugins,vfile,request-cookies,server-sourcemap,esm,import-meta-inline,react-response}.test.ts` & 顶层 (`deinitialization`, `serve-plugins`, `framework-router`, `dev-and-prod`) | debug / release | 117 | 0 | - | `DevServer.cp` + `SourceMapStore.cp` 修复 + `internal-for-testing` 环境变量后全绿 |
| `test/js/bun/glob/scan.test.ts` & `esbuild-child_process.test.ts` | release (新版) | 171 | 0 | - | `glob.cp` 与 `subprocess.cp` + `child_process.ts` 修复后全绿 |
| `test/js/bun/jsc-stress/` (1) & `cli/{update_interactive_*,heap-prof}` (4) & `internal/` (4) | release / debug | 133 | 0 | - | `jsc-stress` **83/0**、`update_interactive`+`heap-prof` **43/0**、`internal/` 4 文件 **7/0** |
| `test/regression/issue/` (385 files, `cb2eac2e` 97 批扫测) | release (新版) | 1120+ | 0 | - | **385/385 文件全部通过**（`1632`/`16474` 5/0、`22650` 1/0、`24157` 2/0、`25589` 6/0；清理 `/root/.bun/install/cache/lodash@*` 旧缓存后 `patch-bounds-check` 3/0） |

## 二、 剩余非阻塞环境/外网/长耗时差异汇总

- **已排查确认非 `.cp` 迁移缺陷的失败**：
  - `test/js/node/tls/` `peer certificate`：线上 `bun.sh:443` 证书轮换已不再携带 `OCSP - URI`。
  - `test/js/node/process/` `dlopen`（5 项）：宿主机 `node v26.1.0`（ABI 147）与 Bun 1.3.14（ABI 137）不匹配。
  - `test/js/node/http/node-http-uaf`：单次迭代 ~9.9s，在 `--timeout=5000` 下超时（直接运行 0 报错）。
  - `test/js/web/fetch/` `locked blob body`：JS builtin `ReadableStreamInternals.ts:2178` `$disturbed` 既有行为，`.cp` 与 `_.zig` 一致。
  - `test/js/bun/ffi/`（2 项）：宿主机 `cc -shared` 无 `-fPIC` 与缺少 `<node/node_api.h>`。
  - `test/js/bun/secrets*`（8 项）：无系统 D-Bus keyring；`shell > sharp` 需外网安装。
- **待后续按需收敛**：`test/bundler/bun-build-compile-wasm.test.ts`（1 fail）、`test/bake/dev/server-sourcemap.test.ts`（3 fail）、`test/cli/install/` 剩余依赖本地/外网 npm 的 `verdaccio` 用例及 `test/js/third_party/` 10 fail / 9 timeout。
| `test/js/bun/{css,yaml}/` & `brotli-reset-leak` & `text-decoder` (`--timeout=15000/30000`) | debug | 2647 | 6 | 67 skip / 44 todo | 0 逻辑 fail（仅 5 CSS fuzz 与 1 YAML bomb 100ms 限时在 Debug 下超时）；`brotli-reset-leak` 2/0、`text-decoder` 77/0 |
| `test/js/bun/net/` (`--timeout=30000`) | debug | 51 | 3 | 3 skip | 1 timeout + 2 GC 轮询波动（单跑 `tcp-server` 8/0、`socket-retention` 全过、无 socket 引用计数泄漏） |
| `test/js/bun/shell/` & `test/js/bun/test/` | debug | 1849 | 32 | 47 skip / 547 todo | `shell` 753/28（`#11816` 已确认为同步 `gc()` 保守栈根非泄漏）；`test/` 1096/4（75 files） |
| `test/js/bun/crypto/` & `test/js/bun/glob/` (`--timeout=15000`) | debug | 10446 | 7 | - | `crypto` **10248/0** 全绿；`glob` **198/7**（0 逻辑 fail，7 项均为 Debug 下递归扫描/压测超时） |
| `test/js/bun/{dns,udp,websocket}/` (`--timeout=15000`) | debug | 337 | 45 | 3 todo | `dns` **76/0**、`udp` **209/0**、websocket 泄漏/GC 3 文件全绿；`websocket-server` 45 项超时排查中 |
| `test/js/bun/websocket/websocket-server.test.ts` (`--max-concurrency=2`) & `stream/` | debug | 356 | 5 | 268 skip / 3 todo | `websocket-server` 升至 **87 pass / 0 逻辑 fail**（5 项硬编码超时）；`stream` **269/0** |
| `test/js/bun/{sqlite,md,s3}/` (`--timeout=15000`) | debug | 1173 | 1 | 294 skip | `sqlite` **82/0**、`md` **1022/0**、`s3` **69/1**（`s3-stream-cancel-leak.test.ts:88` 收到 `30 <= 5` 排查中） |
| `test/integration/` (17 files, `--timeout=15000`) | release | 20 | 5 | 1 todo | `esbuild`/`jsdom`/`nest`/`sass`/`sharp`/`svelte`/`typegraphql`/`vite-build` 全绿；5 fail 为 `install` 超时或缺失 Chrome |
| `test/js/bun/{http,util}/` (`--max-concurrency=2`) | debug | 2820 | 48 | 29 skip / 4 todo | `http` 1074/14（2 非超时均环境/阈值原因）；`util` 1746/34（26 非超时源于 `fuzzy-wuzzy` 污染，单跑 `password`/`heap-snapshot` 等 **98/0**） |
| `test/js/bun/s3/s3-stream-cancel-leak.test.ts` & `node/zlib/zlib.test.js` & 核心回归 16 文件 | debug (最新 `3898985e`) | 1844 | 1 | 2 skip | `s3-stream-cancel-leak` **1/0**（泄漏根除）；`zlib` **375/1**（0 panic，仅 brotli 15s 超时）；`dns`/`udp`/`brotli`/`text-decoder`/`sqlite`/`md` **1468/0** 全绿 |
| `test/js/web/fetch/` (57 files) & `cli/{test,run}` & `node/{path,buffer,events,crypto,util,vm,child_process,dns,os,module,v8,async_hooks,stream,timers,assert}` | release | 14198 | 6 | 30 skip / 95 todo | `fetch` 10525/2；`cli/test` 200/0；`cli/run` 1037/4（缺 FUSE）；`node` 18+ 目录 **2416/0** 全绿 |
| `test/cli/hot/hot.test.ts` (`sourcemap` 竞态) & `test/bake/dev/bundle.test.ts` | debug (最新 `759c8f1a`) | 22 | 0 | - | `hot_reloader.cp`+`ModuleLoader.cp` 竞态修复后 `hot.test.ts` 2/0（142 expects）；`bundle.test.ts` 20/0 |
| `test/js/web/` (除 `fetch/` 13 子目录) & `node/{fs,http,http2,https,net,tls}/` & `bun/{util,glob,resolve,crypto,cron,dns,udp,sqlite,s3,image,yaml,json5,md,jsonl,ini,cookie,io,transpiler,spawn,plugin,repl,websocket,stream,terminal,console,ts,jsc}/` | release | 20672+ | 5 | 412 skip / 105 todo | `web` 13 目录 **3496/0**（`worker.test.ts` 单跑 23/0）；`node` 6 目录 1122/2（仅 `tls` 证书缺 OCSP）；`util`（除 `fuzzy-wuzzy`）**1633/0**；`glob/resolve/crypto` **10690/0**；`cron/dns/.../json5` **1767/0**；`undici-h2`/`wpt-h2`/`socket.io`/`express`/`grpc-js` 单跑 **155/0** |
| `test/regression/issue/` (12 隔离复验) & `cli/install/` 21 核心文件 (`patch`/`audit`/`pkg`/`why`/`version`/`isolated`/`publish`/`lifecycle`/`registry`/`workspaces` 等) | release | 856 | 0 | 8 todo / 1 skip | 12 隔离 `regression` **58/0** 全绿；21 `cli/install/` 文件 **798/0** 全绿 |
| `test/cli/install/bun-security-scanner-matrix-{without,with}-node-modules` & `test/bake/` 3 核心 & `third_party/` (`body-parser`/`remix`/`prisma`) | release | 1465 | 2 | 37 skip | `security-scanner` 矩阵 **1408/0**；`bake` 3 核心 **50/0**；`express-bun-build-compile` 1/0、`remix` 1/0、`prisma` 5/2 timeout |
| `test/bundler/` 其余 22 文件 (`minify`/`jsx`/`splitting`/`npm`/`edgecase`/`api`/`compile-wasm`/`standalone` 等) & `node/{readline,url,cluster,console,dgram,watch,...}` & `cli/{env,init,create}` | release | 997 | 9 | 37 skip / 30 todo | `bundler` 22 文件 **586/0** 全绿；`node` 35 目录 **393/2**（仅 root `fs.watch` `EACCES`）；`cli/{env,init}` 全绿（`create-jsx` 7 项受外网 `shadcn`/snapshot 影响） |
| `test/napi/{napi-value-ffi,uv_stub,uv}` (最新 `bun-debug`) & `cli/{create,env,init}` & `node/{process,perf_hooks,querystring,tty,worker_threads}` & `internal/jsc-stress/webview/symbols` | debug / release | 871 | 35 | 58 skip / 56 todo | `uv` & `uv_stub` **301/0**；`cli/{env,init}` 全绿；`perf_hooks/querystring/tty/worker_threads` 全绿（`process` 6 项为宿主机 `node@26.1` ABI/版本差异）；`internal/` 除 `ban-words`（`.cp`/`_.zig` 计数与 `_.zig` 备份）外全绿 |
| `test/js/bun/http/proxy.test.ts` & `s3-stream-cancel-leak.test.ts` & `node/process/process.test.js` (单跑隔离) | release | 140 | 1 | 1 skip / 2 todo | `http/proxy` **39/0**（124 expects，30s 限时全绿）；`s3-stream-cancel-leak` **1/0**；`process.test.js` `:1152` 单跑稳定全绿（唯一 1 fail 为宿主机 `node@26.1.0` vs `v24.3.0`） |

| `test/bundler/bun-build-compile-sourcemap.test.ts` (隔离复验) & `test/cli/{hot,watch,inspect}/` | release | 63 | 1 | 4 todo | `compile-sourcemap` **9/0**（6.23s 全绿，`inline` 842ms）；`cli/{hot,watch,inspect}` **54/1**（唯一 1 fail 为旧版 Release 上的 `hot.test.ts` `EBADF`，已在最新源码 `9c6dfc2ca98` 修复） |

| `test/js/bun/shell/commands/dirname.test.ts` & `shell/leak.test.ts` & `bake/dev/server-sourcemap` & `bundler/compile-wasm` | debug (`dirname`/`leak`) / release | 56 | 7 | - | `dirname` **10/0**（`resolve_path.cp` 修复后全绿）；`shell/leak` Debug+ASan 下 **35/0 逻辑通过**（仅 7 项重循环达 100s 限时，此前 Release 已 42/0）；`server-sourcemap` **3/0**；`compile-wasm` **1/0** |