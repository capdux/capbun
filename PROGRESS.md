# Bun 1.4.5 (CP 版) 阶段交接文档

**当前基线**：`main` 分支，上一推送提交 `5abea36d326`，本交接同步提交 **Batch 64–67** 全部源码修复（已通过 `cpc_brief.py` 与 `bun run zig:check` 5/5 校验，**尚未执行 `bun bd` 重编验收**）。

---

## 1. 当前阻塞与未完成项

1. **待编译与批量验收（Batch 64–67 修改已落盘未重编 `bun-debug`）**：
   - `Batch 64A`：`src/jsc/FetchHeaders.cp`、`src/runtime/webcore/Response.cp`、`src/interchange/yaml.cp`（`response.test.ts`、`yaml-block-scalar-matrix.test.ts`、`yaml-test-suite.test.ts` `9MMW`）
   - `Batch 64B`：`src/shell/builtin/{cp,mv}.cp`、`src/sys/coreutils_error_map.cp`（`cp.test.ts`、`mv.test.ts`）
   - `Batch 64C` + `Batch 65B`：`src/js/bun/sqlite.ts`、`src/jsc/bindings/sqlite/JSSQLStatement.cpp`（`sqlite.test.js` 游标/`toString`/GC/非 UTF-8 列名、`fileControl`）
   - `Batch 65A`：`src/js/node/v8.ts`（`v8-serdes-buffer.test.ts`）
   - `Batch 65C` + 主智能体：`src/js/internal/url.ts`、`src/js/node/url.ts`、`src/jsc/bindings/NodeURL.cpp`（`node/url/` 7 个文件）
   - `Batch 66A`：`src/sourcemap/VLQ.cp`、`src/jsc/modules/NodeModuleModule.cpp`、`src/jsc/bindings/JSCommonJSModule.cpp`（`sourcemap.test.js`、`node-module-module.test.js`、`require-extensions.test.ts`）
   - 主智能体：`src/jsc/bindings/napi.cpp`（`napi_instanceof` `Symbol.hasInstance`）
   - `Batch 66C`：`src/css/` 下 10 个 `.cp`（`printer.cp`, `rules/{supports,unknown,font_face,style}.cp`, `selectors/{selector,parser}.cp`, `media_query.cp`, `properties/{properties_impl,custom}.cp`）
   - `Batch 67A`：`src/sql_jsc/postgres/{DataCell,PostgresSQLConnection,PostgresSQLQuery,types/date}.cp`（5 个 Postgres 失败用例）
   - `Batch 67B`：`src/js/internal/fs/streams.ts`、`src/js/builtins/ProcessObjectInternals.ts`、`src/runtime/node/assert/myers_diff.cp`、`src/js/internal/assert/assertion_error.ts`（`process-stdout-write-after-end`、`assert-typedarray-deepequal`）
   - `Batch 67C`：`src/js/node/{events,async_hooks}.ts`、`src/runtime/node/node_crypto_binding.cp`（`EventEmitterAsyncResource`、`AsyncLocalStorage-tracking`）
2. **尚未系统扫修的测试板块（全仓 1,974 个 `.test.*` 中约余 ~1,200 个）**：
   - `test/cli/`（259 个，跳过 `lint`/`format`/`check`）
   - `test/integration/`（23 个）
   - `test/v8/`（1 个）
   - `test/js/bun/` 其余子目录（`transpiler`、`plugins`、`net`、`fs`、`crypto` 等）
   - `test/bundler/` 除 `css-modules` / `esbuild/` 外的剩余目录
   - `test/js/node/` 其余小目录（`inspector`、`readline`、`domain`、`punycode`、`trace_events`、`zlib`/`vm`/`http`/`net`/`tls` 遗留单点）
   - `test/js/web/` 剩余目录（除 `encoding`/`html`/`url`/`broadcastchannel`/`websocket`/`fetch`/`workers` 外）
   - `test/regression/issue/` 遗留单点（如 `40380` TLS half-close、`40350` HTTP pipeline）

---

## 2. 已排除的死胡同（避坑记录）

以下项目已确认属于**上游 1.4.x 缺失新特性、预编译 WebKit 限制、或缺失外部测试依赖**，**严禁新会话重复投入排查**：
1. **缺失上游新特性 / 存根未实现**：
   - `Bun.FetchSession`（`fetch-connection-header.test.ts` 仅 1 项：`src/` 无实现）
   - `Bun.ModuleGraph`（`require-extensions.test.ts` 后 10 项、`node-tls-duplex-close-throw-uaf` 后 4 项）
   - `db.function()` / `db.aggregate()` / `SQL.handleNumber` / `SQL.updateHook`（`test/js/bun/sqlite/`）
   - `bun:internal-for-testing` 缺失 `socketFaultInjection` / `translateNtStatusToE`（`net-syscall-fault` / `tls-syscall-fault` / `node-http-syscall-fault` / `rm-windows-ntstatus`）
   - `spawn-cgroup`（当前 `spawn` 无 cgroup 实现）、`node:repl` + `--interactive`（`1960936e231`）、`server.maxHeadersCount`、`alpnCallback`/`serverName` TLS selection `stop(true)`、`--no-deprecated-namespace-object-setters`、`util.getCallSites`、`Buffer.transcode`
   - `child_process_ipc_handle.test.ts`（`src/js/builtins/Ipc.ts` FD passing `return null`）
   - `src/jsc/bindings/uv-posix-polyfills.c` 未实现 `uv_cwd` 等 stub（`test/napi/uv*.test.ts`）
2. **预编译 WebKit / ASan / 环境限制**：
   - `32178.test.ts`：`AsyncLocalStorage.enterWith()` + TLA 在预编译 WebKit `JSC::asyncModuleExecutionResume` 触发 `reportZappedCellAndCrash`。
   - `issue-207.test.ts`（fake-timers 负时间戳）：预编译 WebKit 将 `overridenDateNow < 0` 视为 `-1` 哨兵。
   - `password.test.ts` `throws PASSWORD_OUT_OF_MEMORY (address-space limit)`：ASan debug 构建在 `ulimit -v` 下无法预留 shadow memory。
   - `BUN_JSC_validateExceptionChecks=1` 启动即触发 `JSObject.cpp:836` 未检异常断言（影响 `expect.test.js` 等）。
3. **测试外部依赖 / 缓存缺失**：
   - 缺 npm 包：`ws`、`express`、`tunnel`、`@connectrpc/connect-node`、`@grpc/grpc-js`、`happy-dom`、`lodash`、`immutable`、`uuid`、`string-width`、`strip-ansi`、`detect-libc`。
   - `test/napi/node-napi-tests/`（55 个）：`NODE_HEADERS_VERSION = "26.12.1"` 不在本地 `~/.cache/node-gyp/`（仅有 `26.1.0`/`24.22.3`/`24.3.0`）。
4. **C++ / CP 踩坑**：
   - `ResolvePath.joinZSpill` 共享线程局部 `join_buf`，同一函数内拼接多条路径必须使用独立 `ArrayList`（见 `cp.cp` `joinZOwned`）。
   - `JSC::jsDynamicCast` / `JSC::jsCast` / `scope.clearException()` / `obj->getOwnPropertyNames(3 args)` 在当前 WebKit 头文件不可用，应使用 `dynamicDowncast<T>`、`uncheckedDowncast<T>`、`(void)scope.tryClearException()`、`PropertyNameArrayBuilder` + 4 参 `JSObject::getOwnPropertyNames`、`JSC::asString(val)`。
   - `src/js_parser/lexer.cp` 中 `iter.i` 在进入分支时指向当前字节而非下一字节，切勿随意改无符号减法顺序（会直接导致 `harness.ts` 解析 `integer overflow`）。

---

## 3. 关键约定与约束（已全部固化至 `GEMINI.md`）

1. **三路子智能体 + 主智能体并行**：保持 3 路一次性 `invoke_subagent` 在线，主智能体不空闲等待也不陷入单点长任务；子智能体严禁运行 `bun bd` / `bun bd test`。
2. **三段式收敛流水线**：大扫测 → 多路大包集中修正（`cpc_brief.py -n 8` + `bun run zig:check`）→ 主智能体单次 `bun bd -p 'Bun.version'` 重编 → 批量验收 → 写 `test/stats.md`/`SESSION.md`/`CHANGELOG.md`/`DEVLOG.md` → 原子 `git commit && git push origin main`。
3. **已绿不重跑**：`test/stats.md` 标为 `0 fail` 的文件严禁重复测试。
4. **防孤儿进程 & 临时目录**：临时文件放 `tmp/`；所有测试带 `timeout -k 3 <sec>`，且每批结束必须执行 `pkill -9 -x bun-debug || true`（严禁用 `pkill -f`）。
5. **跨 AGY 通信与 3h 审计**：新会话启动后检查 `systemctl is-active agy-comm.service` 与 `task_queue.py unacked --agent bun_agy --json`，并重新注册 `0 */3 * * *` 定时审计 cron。

---

## 4. 下一步行动（Next Step）

新会话启动后第一件事：
1. 读取 `GEMINI.md`、`PROGRESS.md`、`test/stats.md`，并重新注册 3h 定时审计 cron & 检查 `agy-comm.service`。
2. **立即执行 `BUN_DEBUG_QUIET_LOGS=1 bun bd -p 'Bun.version'`**（不设超时），将本交接提交的 **Batch 64–67** 源码改动编译进 `./build/debug/bun-debug`。
3. 批量验收 **Batch 64–67** 涉及的以下文件（记得校验后缀并在批末执行 `pkill -9 -x bun-debug || true`）：
   - `test/js/web/fetch/response.test.ts`
   - `test/js/bun/yaml/{yaml-block-scalar-matrix,yaml-test-suite}.test.ts`
   - `test/js/bun/shell/commands/{cp,mv}.test.ts`
   - `test/js/bun/sqlite/sqlite.test.js`
   - `test/js/node/v8/v8-serdes-buffer.test.ts`
   - `test/js/node/url/{url,url-parse-query,url-parse-format,url-parse-invalid-input,url-parse-ipv6,url-format-whatwg,url-fileurltopathbuffer,url-domain-ascii-unicode}.{test.ts,test.js}`（先 `ls` 精确后缀）
   - `test/js/node/module/{sourcemap,node-module-module,require-extensions}.{test.js,test.ts}`
   - `test/napi/napi.test.ts`（`-t "napi_instanceof"`）
   - `test/js/bun/css/{supports-condition-newline,custom-pseudo-ident-escape,nth-anplusb-ident,selector-list-error-recovery,token-list-backtracking,nested-vendor-prefix-duplication}.test.ts`
   - `test/js/sql/{postgres-binary-float-nan-box,postgres-float-infinity,postgres-binary-array-bounds,postgres-infinity-date,postgres-invalid-message-length,postgres-bind-encode-throw}.test.ts`
   - `test/js/node/process/process-stdout-write-after-end.test.ts`、`test/js/node/assert/assert-typedarray-deepequal.test.ts`
   - `test/js/node/async_hooks/{EventEmitterAsyncResource,AsyncLocalStorage-tracking}.test.ts`
4. 验收结果写入 `test/stats.md` 并 `git commit && git push origin main`，随后立刻派发 **Batch 68 三路并发** 扫修 `test/cli/`（分片）、`test/integration/`、`test/js/bun/transpiler/` 与 `test/bundler/` 剩余目录。

---

## 5. 经验总结

1. **扫描与修复解耦效率最高**：用旧 `bun-debug` 并发扫出目录内全部可修点后一次性改完再 `bun bd`，能把原本数十次 1~3 分钟的 C++/Zig 重链压缩为每 20~40 个修复文件才编 1 次。
2. **先 `zig:check` 再 `bun bd`**：`zig:check` 仅 ~20s 且覆盖 `gen JS modules (bundle-modules)`，能拦截所有 Zig/TS 语法错误；C++ 修改集中在 `bun bd` 前统一核对 `JSC` API，避免反复链接。
3. **跨进程孤儿清理不可少**：`timeout` 默认只杀直系进程，带 `Bun.spawn` / `Worker` 的测试超时后会在后台残留高 CPU 的 `bun-debug`；每轮测试末用 `pkill -9 -x bun-debug || true` 彻底保底。
4. **严格按修改目录隔离并发**：每次同时派发 3 路子智能体时，在 Prompt 明确列出其他批次正在改动的目录/文件清单，实测零合并冲突。
