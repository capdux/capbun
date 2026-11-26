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
  - **本轮新确认 0 fail 套件**：`test/js/node/buffer/`、`test/js/node/readline/readline.node.test.ts`（`TERM=xterm` 下 81/81）、`test/js/bun/cron/`、`test/js/bun/resolve/`、`test/js/bun/shell/`、`test/js/bun/spawn/`、`test/js/bun/http/`、`test/js/web/streams/`、`test/bundler/transpiler/ts-enum-redecl-panic.test.ts`（6/6）、`test/js/web/console/`（12/12）、`test/js/web/web-globals.test.js`（61/61）、`test/js/web/workers/worker-postmessage-transfer.test.ts`（7/7）、`test/js/node/crypto/crypto.test.ts`（403/403）、`test/js/node/stream/node-stream-uint8array.test.ts`（5/5）、`test/js/node/fs/fs-stats-constructor.test.ts`（7/7）、`test/js/node/dns/`（0 fail）、`test/regression/issue/{28024,11029-crypto-verify-null-algorithm,21274,23183,31002,30963,jsx-template-string-crash,26915}.test.ts`（8 套件全 0 fail）
  - **Batch 40–47 新确认 0 fail 套件（`1.4.5-debug` 已验证，严禁重复测试）**：`test/js/bun/cookie/`、`test/js/bun/udp/`、`test/js/bun/css/`、`test/js/bun/ffi/ffi-viewSource-non-object.test.ts`（28/28）、`test/js/bun/net/socket-dns-error.test.ts`、`test/js/node/path/`、`test/js/web/encoding/`（全部 6 文件 0 fail）、`test/js/web/workers/message-channel.test.ts`、`test/js/web/websocket/websocket-proxy-connect-reply.test.ts`（54/54）、`test/regression/issue/{25589,25589-write-end,31611,36450}.test.ts`、`test/cli/update_interactive_install.test.ts`（7/7 全绿，彻底消除 `dedupe.cp` 死循环）
  - **Batch 48–49 新确认 0 fail 套件（`1.4.5-debug` 已验证，严禁重复测试）**：`test/js/bun/archive.test.ts`、`test/js/bun/terminal/`（`terminal-platform-gaps` + `terminal-spawn`）、`test/js/bun/test/jest-extended.test.js`、`test/js/bun/test/test-timers.test.ts`、`test/js/bun/test/test-test.test.ts`、`test/js/bun/test/mock-fn.test.js`、`test/js/node/dgram/`、`test/js/node/http/{node-http-backpressure,node-http-req-complete,node-http-incoming-socket}.test.ts`、`test/js/web/html/`（全 5 文件）、`test/js/web/fetch/{wasm-streaming,stream-fast-path}.test.ts`、`test/js/web/url/`（全 3 文件）、`test/js/web/broadcastchannel/`（全 2 文件）
  - **显著收敛套件（`1.4.5-debug` 已验证）**：`test/js/bun/image/`（23+ fail -> 仅余 1 fail）、`test/js/bun/patch/patch.test.ts`（全用例通过，仅尾段退出前有 1 处 `drainMicrotasks` 断言）、`test/js/bun/test/expect.test.js`（`resolves`/`rejects` 与 `.not.toThrow` 已修，仅余 2 项 `BUN_JSC_validateExceptionChecks=1` 启动断言）、`test/js/bun/jsonc/`（6 项语法解析通过，仅余 `JSONCObject` 改走 `json.cp` 引发的孤立代理 `\uXXXX`）、`test/js/bun/s3/`（8/9 文件全绿，仅余 `s3-list-objects`）、`test/js/node/v8/capture-stack-trace.test.js`（仅余 1 fail `HTMLRewriter reaction`）、`test/js/node/perf_hooks/`（仅余 1 fail）、`test/js/node/process/process-stdin.test.ts`（仅余 2 fail）、`test/js/node/tls/node-tls-duplex-close-throw-uaf.test.ts`（前 4 项全绿）

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
24. **Batch 27A/B/C（`1bea9e09`, `4ff56393`, `928bb56e` 已完成，`cpc_brief.py` + `zig:check` 全过，待 `bun bd` 构建）**：
    - `Archive.cp`、`Terminal.cp`、`ConsoleObject.cp`、`JSPerformanceEntry.cpp`、`JSDOMURL.cpp`、`inspect.js`、`writeOutputFilesToDisk.cp`、`JSString.cp`、`transpiler.cp`、`JSONCObject.cp`、`NodeSqlite.cpp`、`udp_socket.cp`、`cares_jsc.cp`、`BunBuiltinNames.h`
25. **Batch 28A/B（`de2df7ed`, `fb8a2571` 已完成，`cpc_brief.py` + `zig:check` 全过，待 `bun bd` 构建）**：
    - `tls.ts`、`net.ts`、`socket.cp`、`http2.ts`、`h2_frame_parser.cp`（已裁剪至 `ratio: 1.198`）、`h2/connection.cp`、`prepareCssAstsForChunk.cp`、`js_printer.cp`、`P.cp`、`renamer.cp`、`visit.cp`、`options.cp`、`transpiler.cp`、`ModuleLoader.cp`、`RuntimeTranspilerStore.cp`
26. **Batch 29A/B（`f75e3d17`, `710b9101` 已完成，`cpc_brief.py` + `zig:check` 全过，待 `bun bd` 构建）**：
    - `toml.cp`、`toml/lexer.cp`、`patch.cp`、`url.cp`、`list_objects.cp`、`TextEncoder.cp`、`TextCodecCJK.cpp`、`TextDecoderStream.ts`、`JSBroadcastChannel.cpp`、`SerializedScriptValue.cpp`、`web_worker.cp`、`MessagePort.{h,cpp}`
27. **Batch 30A/B（`0a4750ab`, `6eda4825` 已完成，`cpc_brief.py` + `zig:check` 全过，待 `bun bd` 构建）**：
    - `NodeFSStatBinding.cpp`、`ProcessObjectInternals.ts`、`FileReader.cp`、`JSHash.cpp`、`CryptoSignJob.cpp`、`JSX509Certificate.cpp`、`node_process.cp`、`html_rewriter.cp`
28. **Batch 31A/B + 32A（`032ec648`, `6aa8ef86`, `c6922d2a` 已完成，`cpc_brief.py` + `zig:check` 全过，待 `bun bd` 构建）**：
    - `TextCodecSingleByte.cpp`（16 个单字节编码）、`TextEncodingRegistry.cpp`、`BunBuiltinNames.h`、`ZigGlobalObject.cpp`、`TextDecoderStream.ts`、`MessagePortPipe.{h,cpp}`、`MessagePort.{h,cpp}`、`output.cp`、`Arguments.cp`、`CommandLineArguments.cp`、`sys.cp`、`create_command.cp`、`resolver.cp`、`package_json.cp`、`SideEffects.cp`、`parseProperty.cp`、`lexer.cp`、`RuntimeTranspilerStore.cp`、`h2_frame_parser.cp`
29. **Batch 34–37（`a598cb71`, `84764e1c`, `c4b5d018`, `3ee71534`, `ce09f3db`, `10a6ed22`, `13ababdc`, `6358eb53` 已完成，`cpc_brief.py` + `zig:check` 全过，待 `bun bd` 构建）**：
    - **Batch 34A**：`udp_socket.cp`（NUL 截断、`127.1`/`0x7f000001`/`2130706433`、`parsePort`/`throwGetaddrinfoNotFound`）
    - **Batch 34C**：`run_command.cp`、`pack_command.cp`（`--bun` per-user shim、`PATH_MAX`、`ENOSPC`、`npm_package_*`）
    - **Batch 35A**：`hive_array.cp`、`server.cp`、`Task.cp`、`event_loop.cp`、`PBKDF2.cp`、`AbortSignal.{h,cpp}`、`JSAbortSignal.cpp`、`JSBuffer.cpp`
    - **Batch 35B**：`FormatStackTraceForJS.cpp`（修复 `bool` 重载遮蔽 `Error.prepareStackTrace`、`DontEnum`、`appendStackTrace`）、`ZigGlobalObject.cpp`（`stackTraceLimit = 10`）
    - **Batch 36A**：`web_worker.cp`（`open` 时序前置与 TLA 不阻塞消息）
    - **Batch 36B**：`repl.cp`（非 TTY 管道模式禁用重绘/回显）
    - **Batch 37A**：`Worker.cpp`（`terminate()` 退出码 1）、`FetchTasklet.cp`（`deinitWithoutJSC` 防关闭态空指针）、`worker_destruction.test.ts` 超时
    - **Batch 37B**：`calc.cp`（`parseAtan2` 剩余切片修正）、`color_js.cp`（`ansi-16` SGR 与 `Ansi256` 下溢/数值截断）
30. **主智能体本轮顺手修复（待 `bun bd` 构建）**：
    - `JSCookie.cpp` & `JSCookieMap.cpp`（`getSubclassStructureIfNeeded` 子类化 + `CookieMap` 保持对象键插入顺序）
    - `CookieMap.cpp`（`__Host-`/`__Secure-` 删除时补 `Secure`）
    - `async_hooks.ts`（内联 `ObjectIs` 防 monkeypatch）
    - `YAMLObject.cp`（锚点/别名空集合强制换行 `p:\n  &p\n  {}`）
    - `vendor/libjpeg-turbo/src/turbojpeg.c`（`my_error_exit` 清零 `warning` 标志）
    - `perf_hooks.ts` & `net.ts`（`"net"` 性能条目派发）
    - `ServerWebSocket.cp` & `socket.cp`（`setSocketOptions` 支持 `ServerWebSocket`；`Bun.connect` 仅解包合法 IPv6 字面量）
    - `ReadableStream.ts`（`readableStreamToText` 校验 `$isReadableStreamDisturbed`）
    - `BunObject.cp`（`openInEditor` 对 `undefined`/`null` 路径判空）
    - `JSSQLStatement.cpp`（`declaredTypes` 允许执行前访问）
    - `JSHmac.cpp`（`jsHmacProtoFuncUpdate` 补 `!hmac` 空指针校验）
    - `skipTypescript.cp`（三元条件内箭头函数返回类型回溯支持模板字面量 `${...}` 重扫）
    - `fetch.cp`（`GET`+body 校验优先于 `AbortSignal.aborted`）
    - `WebSocketUpgradeClient.cp`（代理 `CONNECT` 响应支持无 reason-phrase、2xx 与 1xx 循环）

---

## 四、正在运行中的并发修复

- **无（Batch 48–49 已提交推送，即将派发 Batch 50 三路并发）**

---

## 五、下一波待攻克收敛项（Batch 50+）

1. **`test/js/bun/s3/s3-list-objects.test.ts`**（仍超时原因）与 **`test/js/bun/image/` 仅余 1 项**（`metadata()` 警告后 colorspace 检查拒绝）
2. **`test/js/bun/jsonc/`**（保留 `json.cp` 6 项尾部校验同时修复 `\uXXXX` 孤立代理码元保持）与 **`test/js/node/stream/web-stream-state.test.ts`**（为何重编后部分 `Bun.write`/`Bun.spawn`/`HTMLRewriter` 仍失败）
3. **`test/js/bun/patch/patch.test.ts`** 尾段 `drainMicrotasks` pending exception 断言、**`test/js/node/perf_hooks/`** 1 项、**`test/js/node/process/process-stdin.test.ts`** `paused` 退出 1 项
4. **明确冻结（v1.4.4 缺失新特性，不重复排查）**：`Bun.ModuleGraph`（`node-tls-duplex-close-throw-uaf` 后 4 项）、`bun lint`、`server.maxHeadersCount`、`node:repl` + `--interactive`（`1960936e231`）、`alpnCallback`/`serverName` TLS selection `stop(true)`、`sqlite` `db.function()`/`SQL.handleNumber`/`SQL.updateHook`、`happy-dom`/`strip-ansi`/`ws` 缺测试依赖项。
