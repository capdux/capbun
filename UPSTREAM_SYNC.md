# CP-Bun 上游演进同步追踪表

- **基线 Tag**：`cp-v1.3.14-green` (`907c8096077`)
- **目标路线**：`#30412` (`23427dbc12f`) → `a215285063c` (935 commits) → `consolidation-step-1..7` → `bun-v1.4.0` → `bun-v1.4.1..1.4.5`

| 阶段 | 上游区间 | 状态 | 说明 |
| :--- | :--- | :--- | :--- |
| Step 1 | `cp-v1.3.14-green` & `#30412` 坐标系 | **已完成** | 已建 `../bun-ref` 只读 worktree 与 `MODULE_MAP.md` (`1217/1295` 自动匹配) |
| Step 2 | `23427dbc12f..a215285063c` (935 commits) | **已完成** | 已筛选 356 条有效修复；Wave 1 + Wave 2 + Wave 3 已完成全部高/中优安全、泄漏、UAF、边界与 Windows/Linux 修复移植，并通过 `zig:check` 5/5 全绿校验 |
| Step 3 | `a215285063c..bun-v1.4.0` (1097 commits) | **功能补全中** | 高优安全修复已完成；`sql/sqlite`、`valkey/redis`、`install/pm`、`shell/spawn` 常规功能修复及测试已提交；`temp/module_map_100.md` 完成 100% 双向文件盘点；正补全 `http/fetch/ws` 与 `bundler/parser/css` 及缺失模块 |
| Step 4 | `bun-v1.4.0..bun-v1.4.5` (1196 commits) | **Wave 1 已提交** | 已吸收 `1.4.0..1.4.1` (`net/tls/http/server/bake` + `jsc/alloc/gc`)、`1.4.2..1.4.3` (`jsc/alloc/gc`) 与 `1.4.4..1.4.5` (`zlib`/`zstd` 补丁)；待 Step 3 彻底闭合后逐版推进 `1.4.0 -> 1.4.5` |

## Step 2 Wave 1 已吸收修复摘要（`23427dbc12f..a215285063c`）
- **解析/构建/字符串**：`#31010` & `#31234` (`SemverQuery.cp`)、`#31861` (`VLQ.cp`)、`#31700` & `#31435` (`fmt.cp`)、`#31239` (`parseStmt.cp`)、`#31304`/`#31275`/`#31280`/`#31305` (`css/*`)、`#33102` (`ParseTask.cp`)、`#31264` (`immutable.cp`)、`#31221` (`HashedString.cp`)、`#32774` (`LineOffsetTable.cp`)、`#31177` (`vendor/mimalloc/src/threadlocal.c`)、`#30875` (`transpiler.cp`/`ThreadPool.cp`/`bundle_v2.cp`)
- **HTTP/Socket/WebCore/Crypto/S3**：`#32669` (`URLPath.cp`)、`#32661` & `#32735` (`NodeHTTPResponse.cp`)、`#34423` (`HTTPThread.cp`/`dns.cp`)、`#32702` (`http.cp`/`Headers.cp`)、`#33326` & `#33006` (`bun-usockets`/`main.cp`/`main_test.cp`)、`#32660` (`node/net.ts`)、`#33388` (`Handlers.cp`/`socket.cp`)、`#31029` (`s3/client.cp`)、`#34078` (`bsd.c`)、`#31959` (`ssl_wrapper.cp`)、`#33242` (`AsyncHTTP.cp`)、`#32597` (`Sink.cp`/`RequestContext.cp`/`generate-jssink.ts`)、`#32120` & `#31129` & `#31221` (`bun-uws`)、`#30875` (`http`/`HTTPThread`/`AsyncHTTP`/`websocket_client`/`WebSocketUpgradeClient`)、`#31283` (`CryptoHasher.cp`)、`#32758` (`JSCipherPrototype.cpp`)、`#32747` (`AbortSignal`/`JSAbortController`)、`#32590` & `#31221` (`SerializedScriptValue.cpp`)
- **SQL/SQLite/Valkey**：`#34186` (`JSSQLStatement.cpp`)、`#32464` & `#34436` & `#32769` & `#31606` & `#31175` & `#33179` & `#33238` & `#30875` & `#31221`（Postgres/MySQL 协议边界、TLS 握手防注入、日期范围校验、缓存复用）、`#31129` (`valkey_protocol.cp`)
- **Install/Shell/Glob/Patch/Archive/Timer/JSC/NAPI/Zlib**：`#33565` (`BunProcess.cpp` Linux `set*id` 防死锁)、`#31306` (`patch.cp`)、`#34131` (`napi.cpp`/`napi.cp`)、`#34140` (`NodeVMSourceTextModule`/`JSWorker`/`JSFFIFunction`/`Blob.cp`)、`#33131` (`Timer.cp`/`WTFTimer.cp`/`FakeTimers.cp`)、`#33145` & `#31129` & `#31221` (`resolve_path.cp`/`GlobWalker.cp`/`matcher.cp`)、`#32986`/`#32617`/`#33186`/`#33269`/`#32754`/`#31367`/`#34351`/`#31602`/`#32182`/`#32848`/`#31221` (`PipeReader`/`rm`/`seq`/`pm_pkg`/`subproc`/`Expansion`/`macho`/`install`/`TarballStream`/`PackageManager`/`bin`/`repository`/`bun.lock`/`libarchive`)、`#34035` (`DevServerSourceProvider.h`)、`#31646` (`BunClientData`)、`#31624` (`NodeVMSourceTextModule`)、`#31789` (`FormatStackTraceForJS`)、`#32671` (`MarkdownObject.cp`)、`#30875` (`ManagedTask.cp`/`event_loop.cp`)、`#31221` (`node_zlib_binding.cp`/`NativeBrotli`/`NativeZlib`/`NativeZstd`)
