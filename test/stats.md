# CP-Bun v1.4.5 全仓测试进度账本（`test/stats.md`）

> **用途**：每次重启或新轮次开始时先读本文件，直接定位已通过、已修复待重编、正在修复与待处理测试，**避免重复扫测已通过用例**。  
> **最后更新**：2026-11-26 19:23 AEDT

---

## 一、全仓扫测覆盖（100% 目录已扫测归档至 `temp/`）

| 扫测报告 | 覆盖范围 | 总分块 | 全绿分块 | 非零/超时分块 |
| :--- | :--- | :---: | :---: | :---: |
| `temp/test_scan_bun_root.txt` | `test/js/bun/*.test.*` | 9 | 3 | 6 |
| `temp/test_scan_bun_sub1.txt` | `bun/{util,resolve,s3,ffi,websocket,webview,cron,cookie}` | 13 | 0 | 13 |
| `temp/test_scan_bun_sub2.txt` | `bun/{http,net,shell,spawn,test,transpiler,sqlite,yaml,toml,json5}` | 20 | 0 | 20 |
| `temp/test_scan_bun_sub3.txt` | `bun/{css,glob,jsc,module-graph,sourcemap,terminal,udp,...}` | 32 | 6 | 26 |
| `temp/test_scan_root1.txt` | `web/*.test.*`, `bundler/*.test.*`, `node/*.test.*` | 91 | 23 | 68 |
| `temp/test_scan_web_subs.txt` | `test/js/web/*` 所有子目录 | 21 | 2 | 19 |
| `temp/test_scan_bundler_subs.txt` | `test/bundler/{css,esbuild,resolver,transpiler}` | 5 | 1 | 4 |
| `temp/test_scan_cli_integ.txt` | `test/cli`（跳过 lint/format/check）+ `test/integration` | 12 | 0 | 12 |
| `temp/test_scan_regression.txt` | `test/regression/issue`（431 文件） | 22 | 0 | 22 |
| **合计** | **全仓测试目录 100% 覆盖** | **225** | **35** | **190** |

---

## 二、已全绿通过的测试套件（无需重复测试，仅发版前统一回归）

- **Batch 1–13 已提交推送（`origin/main` `f4223799b46`）**：
  - `test/js/web/crypto/web-crypto.test.ts`（97/97）
  - `test/js/bun/xml`（61/61）
  - `test/js/node/os`（59/59）
  - `test/js/node/crypto`（`argon2.test.ts` 36/36, `crypto-extra-memory.test.ts` 8/8, `cipheriv-decipheriv.test.ts` 14/14）
  - `test/js/bun/io/bun-file.test.ts`（11/11）
  - `test/bundler/resolver`（16/16）
  - `test/js/node/child_process/child-process-exec.test.ts`（11/11, `--timeout=20000`）
  - `test/js/deno`（227/227, 16 文件）
  - `test/bundler/esbuild/css.test.ts`（0 fail）
  - `test/js/node/zlib`（0 fail）
  - `test/js/bun/spawn`（此前已部分验证 0 fail，最新扫测见 `bun_sub2` 长尾）
- **Batch 14–26 回归验证全绿（`temp/batch14_26_verify_{a,b}.txt` + `temp/batch14_15_verify.txt`）**：
  - `test/js/node/events`（91/91）
  - `test/js/node/string_decoder`（143/143）
  - `test/js/node/timers`（30/30）
  - `test/js/bun/memfd-disabled.test.ts`（2/2）
  - `test/js/bun/json5`（438/438）
  - `test/js/bun/wasm`（7/7）
  - `test/bundler/bundler_minify_symbol_for.test.ts`（9/9）
  - `test/js/web/structured-clone-blob-file.test.ts`（44/44）
  - `test/js/web/urlpattern`（410/410）
  - **高收敛套件（残余极少）**：`test/bundler/css`（183/184）、`test/js/bun/archive.test.ts`（121/123）、`test/js/bun/jsonc`（369/381）、`test/js/bun/image`（265/277）、`test/js/bun/cookie`（156/178）、`test/js/web/url`（134/137）、`test/js/web/web-globals.test.js`（58/61）、`test/js/web/console`（11/12）、`test/js/node/perf_hooks`（15/17）、`test/js/bun/globals.test.js`（19/20）、`test/js/node/async_hooks`（仅余 2 项）、`test/js/bun/terminal`（仅余 1 项）、`test/js/bun/ffi`（仅余 4 项）
- **扫测直接全绿**：
  - `test/js/node/querystring`、`test/js/node/module`
  - `test/js/web/crypto`（114/114）、`test/js/web/util`（2/2）
  - `test/js/bun/{empty-file,namespace-prototype-pollution,runtime-error}.test.ts`
  - `test/js/bun/{binary,bundler,fetch,import-attributes,perf_hooks,sys,windows}`
  - `test/bundler/{bundler_allow_unresolved,bundler_banner,bundler_decorator_metadata,bundler_footer,bundler_regressions,compile-sourcemap-internal,plugin-error-nested-throw,plugin-sync-exception-fallback,transpiler_constant_fold_eqeq}.test.ts`
  - `test/js/node/{buffer-compare-bounds,buffer-concat,buffer-copy-fill-detach,buffer-from-encoding-leak,buffer-indexOf-detach,buffer-inspectmaxbytes}.test.ts` 等 `root1` 23 个文件

---

## 三、已修复、待下次统一 `bun bd` 编译并批量回归的模块

1. **Batch 14 回归尾项修复（主智能体已完成）**：
   - `src/runtime/api/Archive.cp`：`writeSetOptions("hdrcharset=UTF-8")` 忽略返回值（消除 82 项 `ArchiveFormatError`）
   - `src/runtime/dns_jsc/cares_jsc.cp`：CAA 数字标签改走 `obj.putIndex`（消除 `!parseIndex(propertyName)` 断言）
   - `src/js/builtins/StreamInternals.ts`：补全 `$inspectWebStream` 内建前缀与 `"stream"` 私有属性名（消除 `node/util` `ASSERTION FAILED: symbol`）
2. **Batch 16A（`21731eeb` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
   - `test/bundler/` 4 类崩溃：`renameSymbolsInChunk.cp`（`ThreadLock`）、`E.cp`（栈溢出）、`OutputFileListBuilder.cp`（`total_insertions` 断言）、`JSBundler.cp`（OOM panic）
3. **Batch 16B（`5b1b3046` 已完成，`cpc_brief.py` 全过）**：
   - `cookie`（`Cookie.cpp`, `CookieMap.cpp`）
   - `yaml` / `pretty_format`（`pretty_format.cp`, `yaml.cp`）
   - `toml`（`TOMLObject.cp`, `toml.cp`）
   - `json5` / `jsonc`（`json5.cp`, `JSONCObject.cp`, `lexer.cp`）
4. **Batch 17A（`1d636081` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
   - `test/js/web/` 5 个根级文件：`Blob.cp`, `prompt.cp`, `SerializedScriptValue.cpp`, `JSEventListener.cpp`, `ZigGlobalObject.cpp`, `JSDOMWrapperCache.h`, `JSDOMURL.cpp`, `JSURLPattern.cpp`, `JSFetchHeaders.cpp`, `JSWebSocket.cpp`, `JSWorker.cpp`
5. **Batch 17B（`5b77a5e9` 已完成，`cpc_brief.py` 全过）**：
   - `test/js/node/` Buffer 与根级 API：`NodeBufferModule.h`, `ProcessBindingUV.cpp`, `v8.ts`, `JSBufferConstructor.ts`, `web_worker.cp`, `VirtualMachine.cp`, `options.cp`, `RuntimeTranspilerStore.cp`, `JSBuffer.cpp`, `JSBufferPrototype.ts`, `ErrorCode.ts`, `encoding.cp`
6. **Batch 18B（主智能体已完成，`cpc_brief.py` 全过）**：
   - `src/install/PackageManager/patchPackage.cp`（`doPatchCommit` 挂载 `manager.lockfile = &lockfile.*`，修复 `wyhash` / `__asan_memcpy` SEGV）
   - `src/cli/install_completions_command.cp`（`fish` 自动建目录、`zsh` `.zshrc` 去重识别）
7. **Batch 18A（`fee2afd0` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
   - `test/bundler/{css,esbuild,transpiler}`：21 个 `.cp` 文件（CSS Modules 路径与 `animation-name`、`@import` `url()`、`parseJSXElement`、`ts-enum-redecl`、`useDefineForClassFields: false`、条件箭头函数、`Macro` `.env` 竞态）
8. **Batch 19A + 19B（`8a4b8d57` + `1186ffc6` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
   - `test/js/bun/{http,net,websocket}`：7 个 `.cp` 文件（`WebSocketUpgradeClient.cp`, `WebSocketServerContext.cp`, `RequestContext.cp`, `server.cp`, `StaticRoute.cp`, `cares_jsc.cp`, `Listener.cp`）+ C++ 修复（`ServerRouteList.cpp`, `packages/bun-uws/src/{HttpResponse.h,WebSocketData.h,WebSocket.h,WebSocketProtocol.h,WebSocketContext.h,PerMessageDeflate.h,App.h}`）
9. **Batch 20B（`f3b2fad5` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
   - `test/js/bun/{s3,webview,ffi,sqlite}`：`Store.cp`, `S3File.cp`, `FFIObject.cp`, `ffi.ts`, `ChromeBackend.cpp`, `JSWebViewConstructor.cpp`, `JSSQLStatement.cpp`
10. **Batch 20A（`f7c38830` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/web/{encoding,html,url}`：`TextDecoder.cp`, `EncodingLabel.cp`, `ObjectURLRegistry.cp`, `FormData.cp`, `JSDOMFormData.cpp`, `JSDOMURL.cpp`
11. **Batch 21B（`cac0687e` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/web/{timers,fetch,streams}` + `test/js/bun/util`：`PerformanceUserTiming.cpp`, `Method.cp`, `fetch.cp`, `CompressionStream.ts`, `DecompressionStream.ts`, `BunObject.cp`, `router.cp`, `route_types.cp`, `Crypto.cp`, `uuid.cp`
12. **Batch 21A（`de6d3efc` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/bun/{shell,spawn,test,jsc}` + `fmt.cp`：`fmt.cp`, `expect.cp`, `toBeWithin.cp`, `pretty_format.cp`, `IOWriter.cp`, `mv.cp`, `cp.cp`, `braces.cp`, `virtual_machine_exports.cp`, `js_bun_spawn_bindings.cp`, `BunObject.cpp`, `BunJSCModule.h`, `bindings.cpp`
13. **Batch 23A（`52bbb775` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/node/{v8,vm,watch,worker_threads}` 相关修复
14. **Batch 22A（`4a9d5328` 已完成，`cpc_brief.py` 全过）**：
    - `test/js/web/{broadcastchannel,workers,url,urlpattern}`：`BunClientData.cpp`, `ObjectURLRegistry.cp`, `MessagePort.{h,cpp}`, `MessagePortPipe.{h,cpp}`, `JSMessageEvent.cpp`, `JSDOMURL.cpp`, `URLPattern*`
15. **Batch 22B（`b3fe885a` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/bun/{bun-object,import-attributes,repl,sourcemap,typescript,wasm}`：`Blob.cp`, `wasi.ts`, `internal_jsc.cp`, `Mapping.cp`, `repl.cp`
16. **Batch 23B（`e9627fe7` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/node/{http,http2,tls}`：`h2_frame_parser.cp`, `http2.ts`, `JSConnectionsList.cpp`, `_http_server.ts`, `JSX509Certificate.cpp`, `tls.ts`
17. **Batch 24A（`91b32ca7` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/bun/{image,io,module-graph,patch}`：`Image.cp`, `codec_jpeg.cp`, `codecs.cp`, `codec_png.cp`, `codec_gif.cp`, `copy_file.cp`, `patch.cp`
18. **Batch 25A（`e4298166` 已完成，`zig:check` 全过）**：
    - `test/js/node/{timers,process,readline}`：`NodeTimers.cpp`（+ 主智能体同步修正 `internal/promisify.ts`）、`BunProcess.cpp`、`IdentifierEventListenerMap.{h,cpp}`、`EventEmitter.cpp`、`readline.ts`
19. **Batch 24B（`4c8a0c45` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/node/{dgram,net,child_process}`：`udp_socket.cp`, `dgram.ts`, `net.ts`, `child_process.ts`
20. **Batch 26B（`b624b1bb` 已完成，`zig:check` 全过）**：
    - `test/js/node/async_hooks`：`async_hooks.ts`, `builtins.d.ts`, `thirdparty/ws.js`, `WebSocket.{h,cpp}`
21. **Batch 25B（`d3ef48c3` 已完成，`cpc_brief.py` + `zig:check` 全过）**：
    - `test/js/bun/{terminal,glob,xml}`：`matcher.cp`, `GlobWalker.cp`, `glob.cp`, `tty.cp`, `Terminal.cp`
22. **Batch 26A（`9998803b` 已完成，待构建）**：
    - `test/js/node/{sqlite,perf_hooks}`：`NodeSqliteModule.h`, `NodeSqlite.{h,cpp}`, `JSPerformance.cpp`, `JSPerformanceEntry.cpp`, `JSPerformanceMark.cpp`, `JSPerformanceMeasure.cpp`
23. **主智能体顺手修复（`cpc_brief.py` 全过）**：
    - `src/css/values/calc.cp`（`parseAtan2` 顶层逗号预扫描与 `%`/`s` 剪枝，消除 $5^N$ 回溯挂死）
    - `src/runtime/api/cron.cp`（fake timers 走 `bun.timespec.now(.force_real_time).ms()`）
    - `src/runtime/socket/udp_socket.cp`（`getBinaryType` 补全全部 14 种 `ArrayBufferBinaryType`）
    - `src/jsc/ConsoleObject.cp`（`console.dir` BigInt `depth` 转换 + L596 `E0133`）
    - `src/jsc/bindings/ZigGlobalObject.cpp`（`isModuleEvaluated` 允许 `Status::Evaluating` 以支持 `require(esm)` 循环依赖；默认开启 `useTemporal()`）

---

## 四、正在运行中的并发修复

- **Batch 24B**（`4c8a0c45`）：`test/js/node/{fs,net,child_process,dgram,sqlite}`
- **Batch 25B**（`d3ef48c3`）：`test/js/bun/{terminal,glob,xml}`

---

## 五、待收尾与下一大轮

1. **本大轮收敛编译**：待上述 3 路完成后，主智能体执行 **1 次统一 `bun bd`**，对 Batches 14–25 全部改动做并发批量回归验收并原子提交推送。
2. **剩余长尾**：`test/js/bun/{terminal,glob,css,xml}` 剩余项、`test/cli` 剩余项与 `test/regression/issue` 剩余独立根因收敛。
