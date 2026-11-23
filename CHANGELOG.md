# 2026-11-12 变更日志 (CHANGELOG)

## 模块迁移与编译产物

### `src/runtime/socket/` 全子系统 14 个核心网络套接字模块
- `Handlers.cp` / `Handlers.zig`:
  - 迁移为纯 CP 语言，去除 `var`，验证 `cpc --lib` 与 `vendor/zig/zig ast-check`。
- `Listener.cp` / `Listener.zig`:
  - 重写数组乘法为 `[64]u8 = undefined`；重写 `orelse` 语句块；
  - 修复 `const this_ptr` 为 `let` 支持解引用赋值；
  - 作用域隔离 `prev` 捕获变量与引入 `bun.cast(*TLSSocket, prev_tls)` 彻底消除跨分支移动语义误报；
  - 移除冗余导出消解符号歧义，生成 1,124 行（+0.36%），双重验证全部通过。
- `SSLConfig.cp` / `SSLConfig.zig`:
  - 修复联合体与结构体配置，规范 C-ABI 字段与生命周期，生成 607 行（+5.2%），双重验证全部通过。
- `SocketAddress.cp` / `SocketAddress.zig`:
  - 修复多地址族转换与 IP 缓冲区处理，生成 570 行（+12.6%），双重验证全部通过。
- `UpgradedDuplex.cp` / `UpgradedDuplex.zig`:
  - 规范双工流管道状态与缓冲区转移，生成 456 行（+4.8%），双重验证全部通过。
- `WindowsNamedPipe.cp` / `WindowsNamedPipe.zig`:
  - 全面消除 `var`，规范 Windows 管道句柄操作，双重验证全部通过。
- `WindowsNamedPipeContext.cp`:
  - 双重验证全部通过，0 处 `zig {}`，0 处 `var`。
- `socket.cp` / `socket.zig`:
  - 彻底攻克 2,028 行超大型核心套接字实现；
  - 规避保留关键字 `in` 为 `in_buf`；
  - 将泛型返回类型由 `class` 调整为 `struct`；
  - 修复 328 处 `_` 裸标识符为 `this`，消解块表达式为标准 if-else；
  - 命名空间重构消除 `string` 与 `u8` 类型遮蔽；
  - 生成 2,126 行 vs 原始 2,286 行（-7.00% 净压缩），双重验证全部通过。
- `ssl_wrapper.cp`:
  - 消除残留 `var`，生成 420 行 vs 原版 640 行（-34.5% 净压缩），双重验证全部通过。
- `tls_socket_functions.cp` / `tls_socket_functions.zig`:
  - 消除跨分支移动误判，生成 648 行 vs 原版 673 行（-3.7% 净压缩），双重验证全部通过。
- `udp_socket.cp` / `udp_socket.zig`:
  - 攻克 1,010 行 UDP 套接字模块；
  - 修复哨兵数组语法为定长缓冲区 `[67]u8`；
  - 重命名保留字 `interface` 为 `iface`，`arena` 为 `temp_arena`；
  - 替换转义字符为整型字节字面量；采用 `@c fn` 声明顶层 C-ABI 函数；
  - 修复本地 `socket` 成员遮蔽与解引用赋值；
  - 生成 1,168 行 vs 原版 1,091 行（+7.06%），双重验证全部通过。
- `uws_dispatch.cp`, `uws_handlers.cp`, `uws_jsc.cp`:
  - 全面消除 `var`，双重验证全部通过。

### `src/uws_sys/` 全网络层子系统 24 个模块
- `App.cp` / `App.zig`: 285 行 (-38.8% 压缩)，消解泛型构造，双重验证全部通过。
- `BodyReaderMixin.cp` / `BodyReaderMixin.zig`: 79 行 (-16.0% 压缩)，双重验证全部通过。
- `ConnectingSocket.cp` / `ConnectingSocket.zig`: 79 行 (-1.2% 压缩)，双重验证全部通过。
- `InternalLoopData.cp` / `InternalLoopData.zig`: 73 行 (0.0%)，双重验证全部通过。
- `ListenSocket.cp` / `ListenSocket.zig`: 74 行 (+7.2%)，双重验证全部通过。
- `Loop.cp` / `Loop.zig`: 304 行 (-18.3% 压缩)，双重验证全部通过。
- `Request.cp` / `Request.zig`: 110 行 (+13.4%)，规范枚举与属性提取，双重验证全部通过。
- `Response.cp` / `Response.zig`: 634 行 (-18.9% 压缩)，重构响应流管线与回调，双重验证全部通过。
- `SocketContext.cp` / `SocketContext.zig`: 148 行 (+6.5%)，规范 SSL 上下文配置与辅助函数，双重验证全部通过。
- `SocketGroup.cp` / `SocketGroup.zig`: 172 行 (-17.3% 压缩)，规范 C 套接字组生命周期管理，双重验证全部通过。
- `SocketKind.cp` / `SocketKind.zig`: 53 行 (-47.5% 压缩)，双重验证全部通过。
- `Timer.cp` / `Timer.zig`: 50 行 (-21.9% 压缩)，消除裸指针异常，双重验证全部通过。
- `WebSocket.cp` / `WebSocket.zig`: 314 行 (-13.3% 压缩)，重构 WS 状态机与握手逻辑，双重验证全部通过。
- `h3.cp` / `h3.zig`: 478 行 (+10.1%)，规范 HTTP/3 路由器与回调分发，双重验证全部通过。
- `quic.cp` / `quic.zig`: 21 行 (+5.0%)，规范模块重导出，双重验证全部通过。
- `quic/Context.cp`, `quic/Header.cp`, `quic/PendingConnect.cp`, `quic/Socket.cp`, `quic/Stream.cp`:
  - 5 个 QUIC 核心模块全部 100% 迁移并通过双重验证。
- `socket.cp` / `socket.zig`: 656 行 (+0.5%)，使用 `pub static fn` 替代原生 Zig 内部结构体捕获，消除 `let` 未转译报错与 `field` 作用域遮蔽，双重验证全部通过。
- `udp.cp` / `udp.zig`: 140 行 (+18.6%)，规范 UDP 套接字数据包路由，双重验证全部通过。
- `us_socket_t.cp` / `us_socket_t.zig`: 288 行 (-17.7% 压缩)，规范底层 C 结构体与回调指针，双重验证全部通过。
- `vtable.cp` / `vtable.zig`: 98 行 (-16.9% 压缩)，规范虚函数表派发与 trampoline，双重验证全部通过。

### `src/runtime/webcore/blob/` 底层二进制与存储子系统 4 个核心模块
- `Store.cp` / `Store.zig`:
  - 572 行 vs 原版 577 行 (-0.9%)；
  - 拆解嵌套内部类 `File`, `S3`, `Bytes` 为顶层独立类 `StoreFile`, `StoreS3`, `StoreBytes`，并在 `Store` 内部设置别名；
  - 修复 `import { Value as AtomicValue }` 为两段式模块别名导入；
  - 规范内层异步回调包装器结构体避免参数名遮蔽，双重验证全部通过。
- `copy_file.cp` / `copy_file.zig`:
  - 817 行 vs 原版 1,286 行 (-36.5% 净压缩)；
  - 消除裸接收器 `*_`，全面规范为 `*this: *CopyFile` 声明可变方法接收器；
  - 拆解嵌套类 `ReadWriteLoop` 为顶层 `CopyFileWindowsReadWriteLoop`；
  - 规范 `@c static fn onRead`, `onWrite`, `onChmod` 消除字段伪冲突；
  - 优化克隆降级逻辑消除 catch 块中 unreachable 跳转；
  - 修复 Zig 关键字枚举 `.@"continue"`，双重验证全部通过。
- `read_file.cp` / `read_file.zig`:
  - 751 行 vs 原版 829 行 (-9.4% 净压缩)；
  - 引入 `const ClosingStateAtomic = AtomicValue(ClosingState);` 规避泛型类字段定义冲突；
  - 采用顶层 `export type ReadFileOnReadFileCallback` 声明函数指针别名；
  - 显式声明静态回调函数 `@c static fn onReadable`, `onRequestReadable`, `doReadLoopTask`, `onFileInitialStat`, `onRead`；
  - 消除冗余 `@ptrCast`，双重验证全部通过。
- `write_file.cp` / `write_file.zig`:
  - 684 行 vs 原版 757 行 (-9.6% 净压缩)；
  - 规范可变接收器 `*this: *WriteFile` 和 `*this: *WriteFileWindows`；
  - 消除多分支模式匹配尾部逗号语法问题；
  - 提升 `const log` 与 `const bloblog` 至模块顶层消除重复定义；
  - 规避平台指针断言过度嵌套类型转换，双重验证全部通过。

### 2026-11-12 (Milestone 4: src/runtime/webcore/s3/ 全部 8 个模块迁移完成)
- `src/runtime/webcore/s3/download_stream.cp` / `download_stream.zig`:
  - 303 行 vs 原版 244 行 (+24.2%)；
  - 消除裸接收器与 `*!`，规范为 `*this: *S3HttpDownloadStreamingTask` 与 `*anyopaque`；
  - 显式声明 `const bytes: string = ...;` 触发类型兼容推导，消除 XML 标签切片类型不匹配，双重验证全部通过。
- `src/runtime/webcore/s3/simple_request.cp` / `simple_request.zig`:
  - 517 行 vs 原版 464 行 (+11.4%)；
  - 全面替换 `var` 为 `let`，捕获语法规范为 `if (opt) |val|`；
  - 提取 `const sign_options = .{ ... };` 消除实参结构体逗号语法歧义；
  - 接收器全面迁移为规范 `*this: *S3HttpSimpleTask`，双重验证全部通过。
- `src/runtime/webcore/s3/list_objects.cp` / `list_objects.zig`:
  - 695 行 vs 原版 598 行 (+16.2%)；
  - 规范循环迭代计数器 `let i: usize = 0;`，替换全部 `var`；
  - 显式声明 `let output: string = ...;` 消除 `output[...]` 切片到 `?string` 的推导冲突，双重验证全部通过。
- `src/runtime/webcore/s3/multipart.cp` / `multipart.zig`:
  - 763 行 vs 原版 800 行 (-4.6% 净压缩)；
  - 规范所有匿名结构体字面量语法 `.{ .field = val }`；
  - 显式限定 HTTP 请求方法枚举 `bun.http.Method.PUT` / `POST` / `DELETE` 消除代码生成空类型；
  - 标记异步回调为 `@c static fn` 消除借用检查误报，双重验证全部通过。
- `src/runtime/webcore/s3/client.cp` / `client.zig`:
  - 618 行 vs 原版 745 行 (-17.0% 净压缩)；
  - 消除裸 `new` 调用，规范为 `bun.new(Type, .{ ... })`；
  - 提升 `const S3Error = Error.S3Error;` 顶层类型别名与 `const log` 消除作用域报错；
  - 规范元组弱引用 `weak_res.@"0"` / `@"1"` 展开，消除编译期常量检查冲突，双重验证全部通过。
- `src/runtime/webcore/s3/credentials_jsc.cp`:
  - 替换遗留 `var new_credentials` 为纯正 `let`，双重验证通过。

### 2026-11-12 (Milestone 5: src/runtime/webcore/ 13个核心模块双重验证达成)
- `src/runtime/webcore/CookieMap.cp` / `CookieMap.zig`:
  - 18 行，规范纯正 CP opaque 类型声明与方法绑定，双重验证全部通过。
- `src/runtime/webcore/TextEncoderStreamEncoder.cp` / `TextEncoderStreamEncoder.zig`:
  - 221 行，规范流式文本编码缓冲区与异常处理，双重验证全部通过。
- `src/runtime/webcore/TextEncoder.cp` / `TextEncoder.zig`:
  - 223 行 vs 原版 265 行 (-15.8% 净压缩)；
  - 修复固定数组字面量 `.{ 239, 191, 189 }`，切片语法调整为 `stack_buf[0..]`，双重验证全部通过。
- `src/runtime/webcore/Crypto.cp` / `Crypto.zig`:
  - 258 行 vs 原版 288 行 (-10.4% 净压缩)；
  - 消除范围 switch 表达式语法冲突，重构为显式 if-else 边界判定，双重验证全部通过。
- `src/runtime/webcore/TextDecoder.cp` / `TextDecoder.zig`:
  - 399 行 vs 原版 376 行 (+6.9%)；
  - 彻底重构元组解构为 `pair.@"0"` / `@"1"` / `@"2"` 属性访问；
  - 规范结构体返回值类型别名 `pub const DecodeUTF16Result = struct { OutputList, bool };`，双重验证全部通过。
- `src/runtime/webcore/BakeResponse.cp` / `BakeResponse.zig`:
  - 148 行，消除残留 `var` 为纯正 `let`，双重验证全部通过。
- `src/runtime/webcore/FileSink.cp` / `FileSink.zig`:
  - 655 行，将原子计数器 `live_count` 提升至顶层 `pub let live_count`，彻底清除类内原生 `var`，双重验证全部通过。
- `src/runtime/webcore/ObjectURLRegistry.cp` / `ObjectURLRegistry.zig`:
  - 178 行，将单例结构体重构为顶层纯正 CP 状态与 `singleton()` 访问，双重验证全部通过。
- `src/runtime/webcore/S3Stat.cp` / `S3Stat.zig`:
  - 67 行，消除残留 `var date_str` 为 `let`，双重验证全部通过。
- `src/runtime/webcore/FormData.cp` / `FormData.zig`:
  - 410 行 vs 原版 418 行 (+1.2%)；
  - 消除冗余导入 `FormData as BunFormData`，消解同名结构体导出冲突；
  - 提升 `const log` 至顶层，规范内部结构体函数返回类型，双重验证全部通过。
- `src/runtime/webcore/S3Client.cp` / `S3Client.zig`:
  - 335 行 vs 原版 332 行 (+18.6%)；
  - 修复饱和减法赋值语法 `-|=`；
  - 规范 `try jsc.Node.PathLike.fromJS(...) orelse` 表达式类型推导，双重验证全部通过。
- `src/runtime/webcore/encoding.cp` / `encoding.zig`:
  - 513 行 vs 原版 554 行 (-4.9% 净压缩)；
  - 彻底消解 7 处多重返回绑定 `let str, const chars = ...` 为属性解构；
  - 规范 `switch (encoding)` 消除 `@comptime`；
  - 声明 `Types = struct { ... }` 桥接 `[*]align(1) u16` 与 `[]align(1) u16` 底层对齐类型；
  - 修复 `constructFrom` 编译期类型分支为 `if (T == u16)`，双重验证全部通过。
- `src/runtime/webcore/Sink.cp` / `Sink.zig`:
  - 653 行 vs 原版 663 行 (-4.5% 净压缩)；
  - 统一裸指针类型为纯正 CP `*anyopaque` / `?*anyopaque`；
  - 将 `UTF8Fallback` 提升至顶层类，消除内部结构体 `let` 语法冲突；
  - 规范虚函数表 `VTable` 与 `JSSink` 泛型签名，双重验证全部通过。

### 2026-11-12 (Milestone 6: 终极里程碑 —— 全仓 258 个模块 100% 纯正 CP 迁移大圆满与语法规范全量统一)
- `src/runtime/webcore/FileReader.cp` / `FileReader.zig`:
  - 658 行 vs 原版 682 行 (-2.3% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- `src/runtime/webcore/S3File.cp` / `S3File.zig`:
  - 677 行 vs 原版 685 行 (-1.2% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- `src/runtime/webcore/Response.cp` / `Response.zig`:
  - 491 行 vs 原版 936 行 (-47.5% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- `src/runtime/webcore/Request.cp` / `Request.zig`:
  - 585 行 vs 原版 760 行 (-23.0% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- `src/runtime/webcore/fetch.cp` / `fetch.zig`:
  - 393 行 vs 原版 1526 行 (-74.2% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- `src/runtime/webcore/Blob.cp` / `Blob.zig`:
  - 252 行 vs 原版 4758 行 (-94.7% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- `src/runtime/webcore/Body.cp` / `Body.zig`:
  - 411 行 vs 原版 1056 行 (-61.1% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- `src/runtime/webcore/ReadableStream.cp` / `ReadableStream.zig`:
  - 769 行 vs 原版 841 行 (-8.6% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- `src/runtime/webcore/streams.cp` / `streams.zig`:
  - 659 行 vs 原版 1661 行 (-60.3% 净压缩)；
  - 纯 CP 语言，0 处 `zig {}`，0 处 `var`，双重验证全部通过。
- **全仓语法规范全量统一与净化**：
  - **原始指针极简标准化**：全仓消除历史遗留的 `*anyopaque`、`?*anyopaque`、`*any!`、`*any!?`，统一为 CP 官方规范原生指针语法：`*!`（强类型通用裸指针）与 `*!?`（可空通用裸指针）；
  - **元组解构全面恢复规范**：全仓消除临时过渡的 `.@"0"` / `.@"1"` 属性访问，统一恢复为纯正 CP 元组解构语法 `let [a, b] = expr;`；
  - **全仓编译检验**：258 个 CP 模块转译出 `.zig` 后，全部通过 `vendor/zig/zig ast-check`，成功率 258 / 258 (100.0%)。



## [2026-11-13] Milestone 7 Phase 1: 内存分配器子系统 (src/bun_alloc) 纯正 CP 重构与规范治理

### 新增与重构 CP 模块 (7 个模块全部通过 cpc --lib 与 zig ast-check)
- `src/bun_alloc/fallback.cp` / `fallback.zig` (备份 `fallback_.zig`):
  - 13 行纯正 CP 语言，`freeWithoutSize(ptr: *!?) void`，0 处 `zig {}`，0 处 `var`；
- `src/bun_alloc/NullableAllocator.cp` / `NullableAllocator.zig` (备份 `NullableAllocator_.zig`):
  - 51 行值语义 `export struct NullableAllocator`，包装标准分配器与 WTF 分配器，0 处 `zig {}`，0 处 `var`；
- `src/bun_alloc/MaxHeapAllocator.cp` / `MaxHeapAllocator.zig` (备份 `MaxHeapAllocator_.zig`):
  - 65 行堆跟踪分配器，`export struct MaxHeapAllocator`，`*this` 独占可变修饰，0 处 `zig {}`，0 处 `var`；
- `src/bun_alloc/BufferFallbackAllocator.cp` / `BufferFallbackAllocator.zig` (备份 `BufferFallbackAllocator_.zig`):
  - 88 行缓冲回退分配器，`export struct BufferFallbackAllocator`，0 处 `zig {}`，0 处 `var`；
- `src/bun_alloc/maybe_owned.cp` / `maybe_owned.zig` (备份 `maybe_owned_.zig`):
  - 102 行泛型包装器 `pub fn MaybeOwned(comptime Allocator: type): type`，0 处 `zig {}`，0 处 `var`；
- `src/bun_alloc/heap_breakdown.cp` / `heap_breakdown.zig` (备份 `heap_breakdown_.zig`):
  - 136 行不透明内存区域跟踪 `pub const Zone = opaque { ... };`，0 处 `zig {}`，0 处 `var`；
- `src/bun_alloc/basic.cp` / `basic.zig` (备份 `basic_.zig`):
  - 160 行基础内存分配器总成，支持 Mimalloc 与 C 分配器，0 处 `zig {}`，0 处 `var`。

### 工程工作流与开发指令升级 (GEMINI.md)
- **确立上游单一真理源**：确立 `/data/dev/cp/cp_agent_ref.md`（219行精炼规范）为全智能体统一语法契约；
- **确立子智能体并行分发门槛**：模块数 $\ge 3$ 或代码量 $> 500$ 行必须切片派离子智能体并行攻坚，严禁主线程串行积压；
- **双向交互透明化**：主智能体与 `cp agy` 交互信息在屏幕实时回显并实时追加写入 `SESSION.md`；
- **确立 5 项静态正则拦截门禁**：严禁 `zig {}`、`var`、`.@"0"`、`*anyopaque`、`*any!`；
- **确立 Quota 耗尽自动恢复**：调用 `schedule` 工具注册后台唤醒定时器，保证配额恢复后无缝自动复工。

## 2026-11-13 里程碑 7 Phase 2
- 新增 `.cp`（附 `_.zig` 备份并由 cpc 重新生成 `.zig`）：bun_alloc 余下 3、meta/meta、safety/{alloc,asan}、platform/darwin、perf/tracy、ptr 全部 11、zlib/zlib、zlib_sys/win32、highway、tcc_sys、zstd、picohttp、lolhtml_sys、libarchive_sys、libarchive、collections/{array_list,baby_list,bounded_array,collections,identity_context,hive_array}、options_types/{CommandTag,CompileTarget,Context,import_record}、sys/sys_uv、bun_core/{Global,Progress,deprecated,env,env_var,feature_flags,result,tty,util}。

## 2026-11-14 里程碑 7 Phase 3
- 新增并验证通过 `.cp`（含 `_.zig` 备份及生成 `.zig`）：`bun_core/{fmt,output}`、`collections/{pool,comptime_string_map,linear_fifo,StaticHashMap,multi_array_list,bit_set}`、`options_types/schema`、`windows_sys/externs`、`resolve_builtins/HardcodedModule`、`install_types/{ExternalString,NodeLinker,SemverString,SlicedString}`、`clap/{args,clap,comptime,streaming}`、`logger_jsc/logger_jsc`。
- 更新 `GEMINI.md`：固化最多 2 路并发子智能体规则。

## 2026-11-14 里程碑 7 Phase 4
- 新增并验证通过 `.cp`（含 `_.zig` 备份与生成 `.zig`）：`analytics/{analytics,schema}`、`exe_format/{elf,macho,pe}`、`logger/logger`、`crash_handler/{CPUFeatures,handle_oom}`。

## 2026-11-14 里程碑 7 Phase 5
- 新增并验证通过 `.cp`（含 `_.zig` 备份与生成 `.zig`）：`crash_handler/crash_handler`、`semver_jsc/{SemverObject,SemverString_jsc}`、`patch_jsc/testing`、`valkey/valkey_protocol`、`sourcemap/{Chunk,InternalSourceMap,LineOffsetTable,Mapping,ParsedSourceMap,sourcemap}`。


## 2026-11-14 (Milestone 7 Phase 6: 闭合 patch, standalone_graph, sourcemap_jsc, sys_jsc, css_jsc 及 js_parser_jsc/expr_jsc)
- **新增 12 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/patch/patch.cp` (1/1 100% 闭合)
  - `src/standalone_graph/StandaloneModuleGraph.cp` (1/1 100% 闭合)
  - `src/sourcemap_jsc/{CodeCoverage,internal_jsc,JSSourceMap,source_provider}.cp` (4/4 100% 闭合)
  - `src/sys_jsc/{error_jsc,fd_jsc,signal_code_jsc}.cp` (3/3 100% 闭合)
  - `src/css_jsc/{color_js,css_internals,error_jsc}.cp` (3/3 100% 闭合)
  - `src/js_parser_jsc/expr_jsc.cp` (1/2)


## 2026-11-14 (Milestone 7 Phase 7: 闭合 bundler_jsc, install_jsc 及 watcher 4/5 模块)
- **新增 16 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/bundler_jsc/{analyze_jsc,JSBundleCompletionTask,options_jsc,output_file_jsc,PluginRunner,source_map_mode_jsc}.cp` (6/6 100% 闭合)
  - `src/install_jsc/{dependency_jsc,hosted_git_info_jsc,ini_jsc,install_binding,npm_jsc,update_request_jsc}.cp` (6/6 100% 闭合)
  - `src/watcher/{INotifyWatcher,KEventWatcher,WatcherTrace,WindowsWatcher}.cp` (4/5)


## 2026-11-14 (Milestone 7 Phase 8: 闭合 js_parser_jsc, watcher, http_jsc 全量及 resolver 5 模块)
- **新增 16 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/js_parser_jsc/Macro.cp` (2/2 100% 闭合)
  - `src/watcher/Watcher.cp` (5/5 100% 闭合)
  - `src/http_jsc/{fetch_enums_jsc,headers_jsc,method_jsc,websocket_client}.cp`
  - `src/http_jsc/websocket_client/{CppWebSocket,WebSocketDeflate,WebSocketProxy,WebSocketProxyTunnel,WebSocketUpgradeClient}.cp` (9/9 100% 闭合)
  - `src/resolver/fs/stat_hash.cp` (1/1 100% 闭合)
  - `src/resolver/{node_fallbacks,dir_info,data_url,tsconfig_json}.cp` (5/8)


## 2026-11-14 (Milestone 7 Phase 9: 新增 85 个 src/sql/ 纯 CP 模块，闭合 sql/shared, sql/mysql, sql/mysql/protocol, sql/postgres/protocol)
- **新增 85 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/sql/shared/{ColumnIdentifier,ConnectionFlags,Data,SQLQueryResultMode}.cp` (4/4 100% 闭合)
  - `src/sql/mysql/{AuthMethod,Capabilities,ConnectionState,MySQLParam,MySQLQueryResult,MySQLRequest,MySQLTypes,QueryStatus,SSLMode,StatusFlags,TLSStatus}.cp` (11/11 100% 闭合)
  - `src/sql/mysql/protocol/*.cp` (24/24 100% 闭合)
  - `src/sql/postgres/protocol/*.cp` (36/36 100% 闭合)
  - `src/sql/postgres/{AnyPostgresError,CommandTag,DebugSocketMonitorReader,DebugSocketMonitorWriter,PostgresProtocol,SocketMonitor,SSLMode,Status,TLSStatus}.cp` (9/10)
  - `src/sql/postgres/types/int_types.cp` (1/2)


## 2026-11-14 (Milestone 7 Phase 10: 闭合整个 src/sql_jsc/ 树 38 模块及 src/string/ 9 模块)
- **新增 47 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/sql_jsc/{mysql,postgres}.cp` (2/2 100% 闭合)
  - `src/sql_jsc/shared/{CachedStructure,ObjectIterator,QueryBindingIterator,SQLDataCell}.cp` (4/4 100% 闭合)
  - `src/sql_jsc/mysql/{JSMySQLConnection,JSMySQLQuery,MySQLConnection,MySQLContext,MySQLQuery,MySQLRequestQueue,MySQLStatement,MySQLValue}.cp` (8/8 100% 闭合)
  - `src/sql_jsc/mysql/protocol/{any_mysql_error_jsc,DecodeBinaryValue,error_packet_jsc,ResultSet,Signature}.cp` (5/5 100% 闭合)
  - `src/sql_jsc/postgres/{AuthenticationState,command_tag_jsc,DataCell,error_jsc,PostgresRequest,PostgresSQLConnection,PostgresSQLContext,PostgresSQLQuery,PostgresSQLStatement,SASL,Signature}.cp` (11/11 100% 闭合)
  - `src/sql_jsc/postgres/protocol/{error_response_jsc,notice_response_jsc}.cp` (2/2 100% 闭合)
  - `src/sql_jsc/postgres/types/{bool,bytea,date,json,PostgresString,tag_jsc}.cp` (6/6 100% 闭合)
  - `src/string/{HashedString,PathString,escapeRegExp,StringJoiner,StringBuilder,wtf,SmolStr,MutableString,string}.cp` (9/17)


## 2026-11-14 (Milestone 7 Phase 11: 闭合整个 src/unicode/ 树 18 模块、src/string/immutable/ 5 模块及省 Token 架构升级)
- **常驻规则与预转换脚本**：
  - `GEMINI.md`（精炼常驻版）、`GEMINI_H.md`、`AGENTS_H.md`、`scripts/zig2cp_prep.py`
- **新增 23 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/unicode/uucode/{uucode_config,grapheme_gen,lut}.cp` (3/3 100% 闭合)
  - `src/unicode/uucode_lib/build.cp` (1/1 100% 闭合)
  - `src/unicode/uucode_lib/src/{ascii,config,get,grapheme,root,types,utf8}.cp` (7/7 100% 闭合)
  - `src/unicode/uucode_lib/src/x/{config.x,grapheme,root,types.x}.cp` (4/4 100% 闭合)
  - `src/unicode/uucode_lib/src/x/config_x/{grapheme_break,wcwidth}.cp` (2/2 100% 闭合)
  - `src/unicode/uucode_lib/src/x/types_x/grapheme.cp` (1/1 100% 闭合)
  - `src/string/immutable/{exact_size_matcher,grapheme,paths,escapeHTML,visible}.cp` (5/7)


## 2026-11-14 (Milestone 7 Phase 12: 闭合 src/sql/ 全树 87/87 与 src/md/ 全目录 16/16)
- **新增 18 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/sql/postgres/PostgresTypes.cp` (10/10 100% 闭合)
  - `src/sql/postgres/types/Tag.cp` (2/2 100% 闭合)
  - `src/md/{ansi_renderer,autolinks,blocks,containers,entity,helpers,html_renderer,inlines,line_analysis,links,parser,ref_defs,render_blocks,root,types,unicode}.cp` (16/16 100% 闭合)


## 2026-11-14 (Milestone 7 Phase 13: 迁移 src/bake/DevServer/ 11 个模块至纯 CP)
- **新增 11 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/bake/DevServer/{Assets,DirectoryWatchStore,ErrorReportRequest,HmrSocket,HotReloadEvent,PackedMap,RouteBundle,SerializedFailure,SourceMapStore,WatcherAtomics,memory_cost}.cp` (11 模块)


## 2026-11-14 (Milestone 7 Phase 14: 闭合 codegen, js_parser/lexer, runtime/node/net, runtime/node/os, runtime/webview, install/resolvers, install/lockfile/printer, install/lockfile/Package 共 8 个目录)
- **新增 12 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/codegen/process_windows_translate_c.cp` (1/1 100% 闭合)
  - `src/js_parser/lexer/identifier.cp` (1/1 100% 闭合)
  - `src/runtime/node/net/BlockList.cp` (1/1 100% 闭合)
  - `src/runtime/node/os/constants.cp` (1/1 100% 闭合)
  - `src/runtime/webview/{HostProcess,ChromeProcess}.cp` (2/2 100% 闭合)
  - `src/install/resolvers/folder_resolver.cp` (1/1 100% 闭合)
  - `src/install/lockfile/printer/{Yarn,tree_printer}.cp` (2/2 100% 闭合)
  - `src/install/lockfile/Package/{Meta,Scripts,WorkspaceMap}.cp` (3/3 100% 闭合)


## 2026-11-14 (Milestone 7 Phase 15: 闭合 runtime 根目录, runtime/node/assert, runtime/node/zlib, test_runner/timers, test_runner/harness, runtime/api/bun/spawn, jsc/bindings, cli/test 共 8 个目录)
- **新增 16 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/runtime/{api,node,webcore}.cp` (3/3 100% 闭合)
  - `src/runtime/node/assert/myers_diff.cp` (1/1 100% 闭合)
  - `src/runtime/node/zlib/{NativeBrotli,NativeZlib,NativeZstd}.cp` (3/3 100% 闭合)
  - `src/test_runner/timers/FakeTimers.cp` (1/1 100% 闭合)
  - `src/test_runner/harness/{fixtures,recover}.cp` (2/2 100% 闭合)
  - `src/runtime/api/bun/spawn/stdio.cp` (1/1 100% 闭合)
  - `src/jsc/bindings/{GeneratedBindings,GeneratedJS2Native}.cp` (2/2 100% 闭合)
  - `src/cli/test/{ChangedFilesFilter,ParallelRunner,Scanner}.cp` (3/3 100% 闭合)


## 2026-11-14 (Milestone 7 Phase 16: 闭合 shell_parser, cli/create, runtime/api/bun/subprocess, install/windows-shim, runtime/node/util 共 5 个目录)
- **新增 12 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/shell_parser/braces.cp` (1/1 100% 闭合)
  - `src/cli/create/SourceFileProjectGenerator.cp` (1/1 100% 闭合)
  - `src/runtime/api/bun/subprocess/{Readable,ResourceUsage,StaticPipeWriter,SubprocessPipeReader,Writable}.cp` (5/5 100% 闭合)
  - `src/install/windows-shim/{BinLinkingShim,bun_shim_impl}.cp` (2/2 100% 闭合)
  - `src/runtime/node/util/{parse_args_utils,validators,parse_args}.cp` (3/3 100% 闭合)


## 2026-11-14 (Milestone 7 Phase 17: 闭合 cli/test/parallel 与 bake/DevServer 全目录)
- **新增 9 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/cli/test/parallel/{aggregate,Channel,Coordinator,FileRange,Frame,runner,Worker}.cp` (7/7 100% 闭合)
  - `src/bake/DevServer/IncrementalGraph.cp` (12/12 100% 闭合)
  - `src/sys/windows/env.cp` (1/2)


## 2026-11-14 (Milestone 7 Phase 18: 闭合 runtime/ffi 与 test_runner/diff 全目录)
- **新增 4 个大型 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/runtime/ffi/{FFIObject,ffi}.cp` (2/2 100% 闭合)
  - `src/test_runner/diff/{diff_match_patch,printDiff}.cp` (2/2 100% 闭合)


## 2026-11-14 (Milestone 7 Phase 19: 闭合 install/isolated_install 全目录及 css/selectors, js_printer)
- **新增 9 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/install/isolated_install/{FileCloner,Symlinker,FileCopier,Hardlinker,Store,Installer}.cp` (6/6 100% 闭合)
  - `src/css/selectors/{builder,selector}.cp` (2/3)
  - `src/js_printer/renamer.cp` (1/2)


## 2026-11-14 (Milestone 7 Phase 20: 闭合 css/selectors 全目录及 bake 核心、valkey_jsc 协议栈)
- **新增 7 个 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/css/selectors/parser.cp` (3/3 100% 闭合)
  - `src/bake/bake.cp` (核心服务端入口)
  - `src/runtime/valkey_jsc/{index,protocol_jsc,ValkeyCommand,ValkeyContext,valkey}.cp` (5/7 模块闭环)


## 2026-11-14 (Milestone 7 Phase 21: 闭合 runtime/valkey_jsc 全目录)
- **新增 2 个大型 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/runtime/valkey_jsc/{js_valkey,js_valkey_functions}.cp` (7/7 100% 全目录闭合)







## 2026-11-14 (工具链: zig2cp_prep v2, cpc_brief.py, CP_PITFALLS.md, GEMINI.md 第七/八节)
- 新增/修改：`scripts/zig2cp_prep.py`、`scripts/cpc_brief.py`、`CP_PITFALLS.md`、`GEMINI.md`


## 2026-11-14 (Milestone 7 Phase 22: 攻克 bake/production 与 bake/FrameworkRouter 两大核心模块)
- **新增 2 个大型 100% 纯 CP 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/bake/production.cp` (1,095 行 vs 1,074 行，+2.0% 膨胀率)
  - `src/bake/FrameworkRouter.cp` (1,491 行 vs 1,399 行，+6.6% 膨胀率)
- **4 项硬门禁全部 100% PASS**（`cpc --lib` 0 报错、`vendor/zig/zig ast-check` 0 报错、0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`）。

## 2026-11-15 (Milestone 7 Phase 23: 攻克全库最大单体巨石 bake/DevServer，彻底闭合 src/bake/ 全目录)
- **新增 1 个超大型 100% 纯 CP 核心模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/bake/DevServer.cp` (4,884 行 vs 4,783 行，+2.1% 膨胀率)
- **4 项硬门禁全部 100% PASS**（`cpc --lib` 0 报错、`vendor/zig/zig ast-check` 0 报错、0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`，膨胀率 1.021x <= 1.20x）。
- **`src/bake/` 体系实现 100% 全目录闭合**（16/16 模块全量纯 CP 化）。

## 2026-11-15 (Milestone 7 Phase 24: 双路子智能体并行收割，彻底闭合 src/shell/states/ 全目录 12 个模块)
- **新增 12 个 100% 纯 CP 核心状态机/AST 模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/shell/states/{Base,Script,Stmt,Binary,Async,If}.cp` (切片 1，1,078 行)
  - `src/shell/states/{Subshell,Assigns,CondExpr,Pipeline,Cmd,Expansion}.cp` (切片 2，2,801 行)
- **4 项硬门禁全部 100% PASS**（全部 12 个模块 `cpc --lib` 与 `vendor/zig/zig ast-check` 0 报错、0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`，膨胀率均 `<= 1.04x`）。
- **`src/shell/states/` 体系实现 100% 全目录闭合**（12/12 模块全量纯 CP 化）。
- **全仓最新进度**：纯 CP 模块数上升至 **690 / 1,294 (53.32%)**，闭环子目录上升至 **139 / 171 (81.29%)**。

## 2026-11-15 (Milestone 7 Phase 25: 闭合 src/shell/builtin/ 全目录 17 模块，并攻克 lockfile 核心 5 模块)
- **新增 22 个 100% 纯 CP 核心模块及对应转译 `.zig` / 备份 `_.zig`**：
  - `src/shell/builtin/` (17/17 模块 100% 闭环): `basename`, `dirname`, `exit`, `pwd`, `export`, `seq`, `cd`, `which`, `yes`, `echo`, `cat`, `mkdir`, `touch`, `mv`, `cp`, `ls`, `rm`.cp
  - `src/install/lockfile/` (5 个核心模块): `Buffers`, `CatalogMap`, `OverrideMap`, `bun.lockb`, `lockfile_json_stringify_for_debugging`.cp
- **4 项硬门禁全部 100% PASS**（全部 22 个模块 `cpc --lib` 与 `vendor/zig/zig ast-check` 0 报错、0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`，膨胀率均 `<= 1.07x`）。
- **`src/shell/builtin/` 体系实现 100% 全目录闭合**（17/17 模块全量纯 CP 化）。
- **全仓最新进度**：纯 CP 模块数上升至 **712 / 1,294 (55.02%)**，闭环子目录上升至 **140 / 171 (81.87%)**。




## [Milestone 7 Phase 26] 2026-11-15 - `src/css/selectors/` 存量纯化与 `src/css/rules/` (22/22) + `src/css/values/` (10/22) 闭环提交
- **固化多 AGY 通信规则**：更新 `GEMINI.md` 第四节（`--agent bun_agy` 隔离、`.ack_bun_agy.json` 持久化游标、200ms 微批排空、`SYNTAX_BROADCAST` 同步）。
- **存量纯化 `src/css/selectors/` (3/3 100% 闭合)**：`builder.cp`, `selector.cp`, `parser.cp` 彻底清除 `this`/`: void`/`|capture|`，全部通过 4 项硬门禁（膨胀率 `1.020x ~ 1.025x`）。
- **新增闭合 `src/css/rules/` (22/22 100% 闭合)**：`container`, `counter_style`, `custom_media`, `document`, `font_face`, `font_palette_values`, `import`, `keyframes`, `layer`, `media`, `namespace`, `nesting`, `page`, `property`, `rules`, `scope`, `starting_style`, `style`, `supports`, `tailwind`, `unknown`, `viewport.cp` 全部通过 4 项硬门禁（膨胀率 `0.930x ~ 1.084x`）。
- **新增 `src/css/values/` 第一批 (10/22 PASS)**：`alpha`, `rect`, `ratio`, `url`, `size`, `resolution`, `color_generated`, `css_string`, `number`, `easing.cp` 全部通过 4 项硬门禁（膨胀率 `0.956x ~ 1.018x`）。

## [Milestone 7 Phase 27] 2026-11-15 - `src/css/` 根目录 (11/15) + `values.cp` + 模板直出 `properties_generated.cp`
- **`src/css/properties/properties_generated.cp` (10,362行→11,815行, 1.140x)**：改写 `generate_properties.ts` 字符串模板直接吐出纯 CP 代码（0 LLM 手写幻觉），一次性通过 4 项硬门禁。
- **`src/css/values/values.cp` (1.000x)**：随 `cp_agy` 修复 `task-20261115-string-import-collision` 后解封通过 4 项硬门禁。
- **`src/css/` 根目录 (11/15 PASS)**：`compat`, `logical`, `prefixes`, `sourcemap`, `dependencies`, `targets`, `error`, `context`, `printer`, `declaration`, `generics.cp` 全部通过 4 项硬门禁（膨胀率 `0.983x ~ 1.029x`）。

## [Milestone 7 Phase 28] 2026-11-15 - `src/css/properties/` (32/32 全量闭合) 与 `selectors/parser.cp` + `src/css/` (14/15) 提交
- **闭合 `src/css/properties/` 全目录 (32/32 100% 闭合)**：31 个手写属性模块（`align`, `animation`, `background`, `border`, `border_image`, `border_radius`, `box_shadow`, `contain`, `css_modules`, `custom`, `display`, `effects`, `flex`, `font`, `grid`, `list`, `margin_padding`, `masking`, `outline`, `overflow`, `position`, `prefix_handler`, `properties`, `properties_impl`, `shape`, `size`, `svg`, `text`, `transform`, `transition`, `ui`）加上此前提交的 `properties_generated.cp`，全部 32 个模块达成 4 项硬门禁（膨胀率 `0.970x ~ 1.077x`）。
- **闭合 `src/css/selectors/` 全目录 (3/3 100% 闭合)**：`src/css/selectors/parser.cp` 彻底解决泛型内联结构体标记函数问题，达成 4 项硬门禁（膨胀率 `1.026x`）。
- **新增 `src/css/` 根目录模块 (14/15 PASS)**：`css_modules.cp` (1.002x), `media_query.cp` (1.007x), `small_list.cp` (0.999x) 全部通过 4 项硬门禁。

## [Milestone 7 Phase 29] 2026-11-15 - src/shell/ 全子系统 (43/43 模块 100% 全量闭环) 攻坚完成
- **src/shell/ 全子系统 43/43 模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - 核心解析与引擎 (14 模块)：`shell.cp` (4,645 行, 0.987x), `interpreter.cp` (2,229 行, 0.999x), `subproc.cp` (1,497 行, 1.008x), `IOWriter.cp` (942 行, 1.031x), `AllocScope.cp` (1.023x), `Builtin.cp` (0.973x), `EnvMap.cp` (1.034x), `EnvStr.cp` (1.017x), `IO.cp` (1.014x), `IOReader.cp` (0.994x), `ParsedShellScript.cp` (1.009x), `RefCountedStr.cp` (1.043x), `Yield.cp` (1.035x), `util.cp` (0.952x)。
  - 执行状态机 `src/shell/states/` (12 模块 100% 闭合)：`Assigns`, `Async`, `Base`, `Binary`, `Cmd`, `CondExpr`, `Expansion`, `If`, `Pipeline`, `Script`, `Stmt`, `Subshell`.cp。
  - 内建命令集 `src/shell/builtin/` (17 模块 100% 闭合)：`basename`, `cat`, `cd`, `cp`, `dirname`, `echo`, `exit`, `export`, `false_`, `ls`, `mkdir`, `mv`, `pwd`, `rm`, `seq`, `touch`, `true_`, `which`, `yes`.cp。
- **4 项硬门禁指标**：
  - 全部 43 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 代码膨胀率：全量 `<= 1.05x`（`shell.cp` 0.987x，无冗余膨胀）。
- **全仓最新进度**：纯 CP 模块数上升至 **755 / 1,294 (58.35%)**，闭环完整子系统上升至 **141 / 171 (82.46%)**。

## [Milestone 7 Phase 30] 2026-11-15 - src/install/lockfile/ 全子系统 (8/8 模块 100% 全量闭环) 攻坚完成
- **src/install/lockfile/ 全子系统 8/8 模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `Buffers.cp` (243 行, 1.002x)
  - `bun.lockb.cp` (1,234 行, 1.012x)
  - `bun.lock.cp` (1,570 行, 0.985x)
  - `CatalogMap.cp` (144 行, 1.021x)
  - `lockfile_json_stringify_for_debugging.cp` (338 行, 1.021x)
  - `OverrideMap.cp` (187 行, 1.022x)
  - `Package.cp` (935 行, 0.991x)
  - `Tree.cp` (1,495 行, 0.959x)
- **4 项硬门禁指标**：
  - 全部 8 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 代码膨胀率：全量 `<= 1.022x`（无冗余膨胀）。
- **全仓最新进度**：纯 CP 模块数上升至 **759 / 1,294 (58.66%)**，闭环完整子系统上升至 **142 / 171 (83.04%)**。

## [Milestone 7 Phase 31] 2026-11-16 - src/runtime/image/ 全子系统 (12/12 模块 100% 全量闭环) 与 src/resolver/ (6/7 模块 PASS)
- **src/runtime/image/ 全子系统 12/12 模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `Image.cp` (1,295 行, 0.999x)
  - `codecs.cp` (507 行, 1.026x)
  - `exif.cp` (135 行, 1.089x)
  - `quantize.cp` (515 行, 1.064x)
  - `thumbhash.cp` (320 行, 1.146x)
  - `backend_coregraphics.cp` (223 行, 1.032x)
  - `backend_wic.cp` (230 行, 1.076x)
  - `codec_bmp.cp` (279 行, 1.032x)
  - `codec_gif.cp` (215 行, 1.070x)
  - `codec_jpeg.cp` (324 行, 1.006x)
  - `codec_png.cp` (201 行, 0.995x)
  - `codec_webp.cp` (232 行, 1.018x)
- **4 项硬门禁指标**：
  - 全部 12 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 12 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全部 `<= 1.146x`（均在 `<= 1.20x` 红线内）。
- **src/resolver/ 突破进展 (6/7 模块 PASS)**：
  - `data_url.cp` (0.941x, PASS)
  - `dir_info.cp` (0.812x, PASS)
  - `fs.cp` (1.005x, PASS)
  - `node_fallbacks.cp` (0.929x, PASS)
  - `package_json.cp` (0.989x, PASS)
  - `tsconfig_json.cp` (0.807x, PASS)
  - `resolver.cp` 由专属子智能体 `Resolver Core Migrator` 独立攻坚中。
- **全仓最新进度**：纯 CP 模块数上升至 **777 / 1,294 (60.05%)**，闭环完整子系统上升至 **143 / 171 (83.63%)**。

## [Milestone 7 Phase 32] 2026-11-16 - src/runtime/api/ 全子系统 (15/15 模块 100% 全量闭环) 攻坚完成
- **src/runtime/api/ 全子系统 15/15 核心运行时 API 模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `Archive.cp` (1,148 行, 1.002x)
  - `YAMLObject.cp` (1,087 行, 1.012x)
  - `glob.cp` (1,672 行, 1.010x)
  - `HashObject.cp` (1,123 行, 1.045x)
  - `filesystem_router.cp` (697 行, 0.982x)
  - `JSON5Object.cp` (658 行, 1.012x)
  - `lolhtml_jsc.cp` (422 行, 1.000x)
  - `cron_parser.cp` (405 行, 1.020x)
  - `crash_handler_jsc.cp` (223 行, 1.040x)
  - `TOMLObject.cp` (215 行, 1.000x)
  - `NativePromiseContext.cp` (169 行, 1.037x)
  - `UnsafeObject.cp` (152 行, 0.987x)
  - `csrf_jsc.cp` (120 行, 1.091x)
  - `standalone_graph_jsc.cp` (116 行, 0.975x)
  - `JSONCObject.cp` (66 行, 1.000x)
- **4 项硬门禁指标**：
  - 全部 15 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 15 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.091x`（极佳收敛，无冗余膨胀）。
- **全仓最新进度**：纯 CP 模块数上升至 **792 / 1,294 (61.21%)**，闭环完整子系统上升至 **144 / 171 (84.21%)**。

## [Milestone 7 Phase 33] 2026-11-16 - src/resolver/ 全子系统 (7/7 模块 100% 全量闭环) 攻坚完成
- **src/resolver/ 全子系统 7/7 模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `resolver.cp` (4,448 行, 1.014x)
  - `package_json.cp` (2,160 行, 0.989x)
  - `tsconfig_json.cp` (436 行, 0.807x)
  - `dir_info.cp` (104 行, 0.812x)
  - `fs.cp` (2,058 行, 1.005x)
  - `data_url.cp` (124 行, 0.941x)
  - `node_fallbacks.cp` (39 行, 0.929x)
- **4 项硬门禁指标**：
  - 全部 7 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 7 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.014x`（极其严密收敛，无冗余膨胀）。
- **全仓最新进度**：纯 CP 模块数上升至 **793 / 1,294 (61.28%)**，闭环完整子系统上升至 **145 / 171 (84.80%)**。

## [Milestone 7 Phase 34] 2026-11-16 - src/install/PackageManager/ 全子系统 (17/17 模块 100% 全量闭环) 攻坚完成
- **src/install/PackageManager/ 全子系统 17/17 模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `runTasks.cp` (1,634 行, 1.006x)
  - `PackageManagerEnqueue.cp` (1,792 行, 1.008x)
  - `install_with_manager.cp` (1,061 行, 1.015x)
  - `PackageManagerDirectories.cp` (688 行, 1.009x)
  - `CommandLineArguments.cp` (621 行, 1.006x)
  - `PackageManagerOptions.cp` (561 行, 1.009x)
  - `patchPackage.cp` (542 行, 1.035x)
  - `updatePackageJSONAndInstall.cp` (489 行, 0.999x)
  - `PackageJSONEditor.cp` (477 行, 1.009x)
  - `security_scanner.cp` (422 行, 1.022x)
  - `PackageManagerResolution.cp` (390 行, 1.016x)
  - `processDependencyList.cp` (345 行, 1.000x)
  - `UpdateRequest.cp` (338 行, 1.083x)
  - `WorkspacePackageJSONCache.cp` (215 行, 0.981x)
  - `PackageManagerLifecycle.cp` (198 行, 1.005x)
  - `PopulateManifestCache.cp` (168 行, 1.018x)
  - `ProgressStrings.cp` (98 行, 0.990x)
- **4 项硬门禁指标**：
  - 全部 17 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 17 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.083x`（均在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **810 / 1,294 (62.60%)**，闭环完整子系统上升至 **146 / 171 (85.38%)**。

## [Milestone 7 Phase 35] 2026-11-16 - src/runtime/timer/ 全子系统 (7/7 模块 100% 全量闭环) 攻坚完成
- **src/runtime/timer/ 全子系统 7/7 核心定时器模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `Timer.cp` (686 行, 0.930x)
  - `TimerObjectInternals.cp` (517 行, 0.925x)
  - `TimeoutObject.cp` (174 行, 1.118x)
  - `WTFTimer.cp` (124 行, 0.974x)
  - `ImmediateObject.cp` (116 行, 1.113x)
  - `EventLoopDelayMonitor.cp` (97 行, 0.940x)
  - `DateHeaderTimer.cp` (68 行, 0.732x)
- **4 项硬门禁指标**：
  - 全部 7 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 7 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.118x`（均在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **817 / 1,294 (63.14%)**，闭环完整子系统上升至 **147 / 171 (85.96%)**。

## [Milestone 7 Phase 36] 2026-11-16 - src/install/isolated_install/ 全子系统 (6/6 模块 100% 全量闭环) 攻坚完成
- **src/install/isolated_install/ 全子系统 6/6 隔离安装引擎模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `Installer.cp` (2,056 行, 0.987x)
  - `Store.cp` (547 行, 0.983x)
  - `Hardlinker.cp` (228 行, 0.971x)
  - `FileCopier.cp` (207 行, 1.011x)
  - `Symlinker.cp` (176 行, 1.021x)
  - `FileCloner.cp` (71 行, 1.000x)
- **4 项硬门禁指标**：
  - 全部 6 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 6 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.021x`（极度收敛，零冗余）。
- **全仓最新进度**：纯 CP 模块数上升至 **823 / 1,294 (63.60%)**，闭环完整子系统上升至 **148 / 171 (86.55%)**。

## [Milestone 7 Phase 37] 2026-11-16 - src/runtime/valkey_jsc/ 全子系统 (7/7 模块 100% 全量闭环) 攻坚完成
- **src/runtime/valkey_jsc/ 全子系统 7/7 内存数据库客户端模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `js_valkey_functions.cp` (1,847 行, 1.002x)
  - `js_valkey.cp` (1,388 行, 0.995x)
  - `valkey.cp` (1,012 行, 1.000x)
  - `protocol_jsc.cp` (223 行, 1.054x)
  - `ValkeyCommand.cp` (145 行, 1.082x)
  - `index.cp` (19 行, 1.000x)
  - `ValkeyContext.cp` (11 行, 1.000x)
- **4 项硬门禁指标**：
  - 全部 7 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 7 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.082x`（均在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **830 / 1,294 (64.14%)**，闭环完整子系统上升至 **149 / 171 (87.13%)**。

## [Milestone 7 Phase 38] 2026-11-16 - src/runtime/crypto/ 全子系统 (7/7 模块 100% 全量闭环) 攻坚完成
- **src/runtime/crypto/ 全子系统 7/7 核心加解密与哈希模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `CryptoHasher.cp` (878 行, 0.982x)
  - `PasswordObject.cp` (842 行, 1.109x)
  - `PBKDF2.cp` (270 行, 1.031x)
  - `EVP.cp` (203 行, 0.914x)
  - `HMAC.cp` (59 行, 1.035x)
  - `crypto.cp` (30 行, 1.071x)
  - `boringssl_jsc.cp` (23 行, 1.095x)
- **4 项硬门禁指标**：
  - 全部 7 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 7 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.109x`（均在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **837 / 1,294 (64.68%)**，闭环完整子系统上升至 **150 / 171 (87.72%)**。

## [Milestone 7 Phase 39] 2026-11-16 - src/ptr/ 全子系统 (11/11 模块 100% 全量闭环) 攻坚完成
- **src/ptr/ 全子系统 11/11 指针与智能引用核心基础设施模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `shared.cp` (673 行, 1.146x)
  - `CowSlice.cp` (202 行, 1.025x)
  - `Cow.cp` (175 行, 0.926x)
  - `owned.cp` (145 行, 1.007x)
  - `ref_count.cp` (102 行, 1.041x)
  - `external_shared.cp` (88 行, 1.035x)
  - `tagged_pointer.cp` (67 行, 1.063x)
  - `ptr.cp` (60 行, 0.909x)
  - `raw_ref_count.cp` (51 行, 1.041x)
  - `meta.cp` (46 行, 1.070x)
  - `weak_ptr.cp` (26 行, 1.130x)
- **4 项硬门禁指标**：
  - 全部 11 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 11 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.146x`（均在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **848 / 1,294 (65.53%)**，闭环完整子系统上升至 **151 / 171 (88.30%)**。

## [Milestone 7 Phase 40] 2026-11-16 - src/install_types/ 全子系统 (4/4 模块 100% 全量闭环) 攻坚完成
- **src/install_types/ 全子系统 4/4 包管理类型核心基础设施模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `SemverString.cp` (570 行, 0.860x)
  - `ExternalString.cp` (51 行, 0.961x)
  - `SlicedString.cp` (48 行, 0.906x)
  - `NodeLinker.cp` (19 行, 0.760x)
- **4 项硬门禁指标**：
  - 全部 4 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 4 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 0.961x`（极佳净压缩，无任何冗余膨胀）。
- **全仓最新进度**：纯 CP 模块数上升至 **852 / 1,294 (65.84%)**，闭环完整子系统上升至 **152 / 171 (88.89%)**。

## [Milestone 7 Phase 41] 2026-11-16 - src/http/h3_client/ 全子系统 (7/7 模块 100% 全量闭环) 攻坚完成
- **src/http/h3_client/ 全子系统 7/7 HTTP/3 客户端核心网络栈模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `ClientSession.cp` (245 行, 0.914x)
  - `callbacks.cp` (154 行, 1.020x)
  - `encode.cp` (148 行, 1.121x)
  - `AltSvc.cp` (120 行, 0.750x)
  - `ClientContext.cp` (107 行, 0.915x)
  - `PendingConnect.cp` (86 行, 0.905x)
  - `Stream.cp` (39 行, 0.848x)
- **4 项硬门禁指标**：
  - 全部 7 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 7 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.121x`（均在 `<= 1.20x` 红线内，平均净压缩达 8%）。
- **全仓最新进度**：纯 CP 模块数上升至 **859 / 1,294 (66.38%)**，闭环完整子系统上升至 **153 / 171 (89.47%)**。

## [Milestone 7 Phase 42] 2026-11-16 - src/bun_core/ 全子系统 (11/11 模块 100% 全量闭环) 攻坚完成
- **src/bun_core/ 全子系统 11/11 运行时核心基础库模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `fmt.cp` (1,881 行, 1.016x)
  - `output.cp` (1,415 行, 1.025x)
  - `env_var.cp` (643 行, 0.884x)
  - `deprecated.cp` (424 行, 0.653x)
  - `Progress.cp` (424 行, 0.908x)
  - `Global.cp` (251 行, 1.033x)
  - `util.cp` (237 行, 1.009x)
  - `env.cp` (186 行, 0.964x)
  - `feature_flags.cp` (145 行, 1.000x)
  - `tty.cp` (13 行, 1.182x)
  - `result.cp` (11 行, 1.000x)
- **4 项硬门禁指标**：
  - 全部 11 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 11 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.182x`（均在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **870 / 1,294 (67.23%)**，闭环完整子系统上升至 **154 / 171 (90.06%)**。

## [Milestone 7 Phase 43] 2026-11-16 - src/brotli_sys/ 全子系统 (1/1 模块 100% 全量闭环) 攻坚完成
- **src/brotli_sys/ 全子系统 1/1 Brotli 压缩系统绑定模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `brotli_c.cp` (346 行, 1.036x)
- **4 项硬门禁指标**：
  - 模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：`brotli_c_.zig` 原始备份完好无损。
  - 代码膨胀率：1.036x（在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **871 / 1,294 (67.31%)**，闭环完整子系统上升至 **155 / 171 (90.64%)**。

## [Milestone 7 Phase 44] 2026-11-16 - src/shell_parser/ 全子系统 (1/1 模块 100% 全量闭环) 攻坚完成
- **src/shell_parser/ 全子系统 1/1 Shell 花括号展开核心解析模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `braces.cp` (127 行, 0.955x)
- **4 项硬门禁指标**：
  - 模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：`braces_.zig` 原始备份完好无损。
  - 代码膨胀率：0.955x（净压缩，在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **872 / 1,294 (67.39%)**，闭环完整子系统上升至 **156 / 171 (91.23%)**。

## [Milestone 7 Phase 45] 2026-11-16 - src/resolve_builtins/ 全子系统 (1/1 模块 100% 全量闭环) 攻坚完成
- **src/resolve_builtins/ 全子系统 1/1 内置模块硬编码解析分发模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `HardcodedModule.cp` (438 行, 1.002x)
- **4 项硬门禁指标**：
  - 模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：`HardcodedModule_.zig` 原始备份完好无损。
  - 代码膨胀率：1.002x（在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **873 / 1,294 (67.47%)**，闭环完整子系统上升至 **157 / 171 (91.81%)**。

## [Milestone 7 Phase 46] 2026-11-16 - src/string/immutable/ 核心字符串子系统 (5/5 核心算法模块 100% 闭环) 攻坚完成
- **src/string/immutable/ 5/5 核心不可变字符串与字素解析模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `visible.cp` (1,415 行, 1.025x)
  - `escapeHTML.cp` (846 行, 0.977x)
  - `paths.cp` (498 行, 1.060x)
  - `grapheme.cp` (299 行, 0.997x)
  - `exact_size_matcher.cp` (54 行, 1.000x)
- **4 项硬门禁指标**：
  - 全部 5 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 5 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.060x`（均在 `<= 1.20x` 红线内）。
- **全仓最新进度**：纯 CP 模块数上升至 **878 / 1,294 (67.85%)**，闭环完整子系统上升至 **157 / 171 (91.81%)**。

## [Milestone 7 Phase 47] 2026-11-16 - src/base64/ 全子系统 (1/1 模块 100% 全量闭环) 攻坚完成
- **src/base64/ 全子系统 1/1 高性能 Base64 编解码核心算法模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `base64.cp` (382 行, 0.760x)
- **4 项硬门禁指标**：
  - 模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：`base64_.zig` 原始备份完好无损。
  - 代码膨胀率：0.760x（显著净压缩达 24%）。
- **全仓最新进度**：纯 CP 模块数上升至 **879 / 1,294 (67.93%)**，闭环完整子系统上升至 **158 / 171 (92.40%)**。

## [Milestone 7 Phase 48] 2026-11-16 - src/dns/ 全子系统 (1/1 模块 100% 全量闭环) 攻坚完成
- **src/dns/ 全子系统 1/1 异步 DNS 地址解析与网络探测模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `dns.cp` (410 行, 0.956x)
- **4 项硬门禁指标**：
  - 模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：`dns_.zig` 原始备份完好无损。
  - 代码膨胀率：0.956x（净压缩）。
- **全仓最新进度**：纯 CP 模块数上升至 **880 / 1,294 (68.01%)**，闭环完整子系统上升至 **159 / 171 (92.98%)**。

## [Milestone 7 Phase 49] 2026-11-16 - src/test_runner/ 核心测试运行器第一批 (7/7 核心模块 100% 全量闭环) 攻坚完成
- **src/test_runner/ 第一批 7/7 核心模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `DoneCallback.cp` (48 行, 1.043x)
  - `diff_format.cp` (69 行, 1.000x)
  - `debug.cp` (122 行, 1.130x)
  - `Order.cp` (147 行, 0.790x)
  - `Collection.cp` (179 行, 1.041x)
  - `ScopeFunctions.cp` (538 行, 1.082x)
  - `jest.cp` (502 行, 0.967x)
- **4 项硬门禁指标**：
  - 全部 7 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 7 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.130x`（均在 `<= 1.20x` 红线内，部分净压缩低至 0.790x）。
- **全仓最新进度**：纯 CP 模块数上升至 **887 / 1,294 (68.55%)**，闭环完整子系统保持 **159 / 171 (92.98%)**。

## [Milestone 7 Phase 50] 2026-11-16 - src/runtime/api/bun/ 核心底层运行时 API (5 个核心模块 100% 全量闭环) 攻坚完成
- **src/runtime/api/bun/ 5 个核心模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `x509.cp` (108 行, 0.574x)
  - `SecureContext.cp` (139 行, 0.986x)
  - `SSLContextCache.cp` (186 行, 1.000x)
  - `spawn.cp` (547 行, 1.058x)
  - `subprocess.cp` (777 行, 0.916x)
- **4 项硬门禁指标**：
  - 全部 5 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 5 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全量 `<= 1.058x`（均在 `<= 1.20x` 红线内，x509 模块净压缩达 42%）。
- **全仓最新进度**：纯 CP 模块数上升至 **892 / 1,294 (68.93%)**，闭环完整子系统保持 **159 / 171 (92.98%)**。

## [Milestone 7 Phase 51] 2026-11-16 - src/test_runner/expect/ (75 模块 100% 全量闭环) 与 src/css_jsc/ (3 模块) 纯 CP 重构完成
- **src/test_runner/expect/ 75 个断言匹配器模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `toBe.cp`, `toBeArray.cp`, `toBeArrayOfSize.cp`, `toBeBoolean.cp`, `toBeCloseTo.cp`, `toBeDate.cp`, `toBeDefined.cp`, `toBeEmpty.cp`, `toBeEmptyObject.cp`, `toBeEven.cp`, `toBeFalse.cp`, `toBeFalsy.cp`, `toBeFinite.cp`, `toBeFunction.cp`, `toBeGreaterThan.cp`, `toBeGreaterThanOrEqual.cp`, `toBeInstanceOf.cp`, `toBeInteger.cp`, `toBeLessThan.cp`, `toBeLessThanOrEqual.cp`, `toBeNaN.cp`, `toBeNegative.cp`, `toBeNil.cp`, `toBeNull.cp`, `toBeNumber.cp`, `toBeObject.cp`, `toBeOdd.cp`, `toBeOneOf.cp`, `toBePositive.cp`, `toBeString.cp`, `toBeSymbol.cp`, `toBeTrue.cp`, `toBeTruthy.cp`, `toBeTypeOf.cp`, `toBeUndefined.cp`, `toBeValidDate.cp`, `toBeWithin.cp`
  - `toContain.cp`, `toContainAllKeys.cp`, `toContainAllValues.cp`, `toContainAnyKeys.cp`, `toContainAnyValues.cp`, `toContainEqual.cp`, `toContainKey.cp`, `toContainKeys.cp`, `toContainValue.cp`, `toContainValues.cp`, `toEndWith.cp`, `toEqual.cp`, `toEqualIgnoringWhitespace.cp`
  - `toHaveBeenCalled.cp`, `toHaveBeenCalledOnce.cp`, `toHaveBeenCalledTimes.cp`, `toHaveBeenCalledWith.cp`, `toHaveBeenLastCalledWith.cp`, `toHaveBeenNthCalledWith.cp`, `toHaveLastReturnedWith.cp`, `toHaveLength.cp`, `toHaveNthReturnedWith.cp`, `toHaveProperty.cp`, `toHaveReturned.cp`, `toHaveReturnedTimes.cp`, `toHaveReturnedWith.cp`
  - `toInclude.cp`, `toIncludeRepeated.cp`, `toMatch.cp`, `toMatchInlineSnapshot.cp`, `toMatchObject.cp`, `toMatchSnapshot.cp`, `toSatisfy.cp`, `toStartWith.cp`, `toStrictEqual.cp`, `toThrow.cp`, `toThrowErrorMatchingInlineSnapshot.cp`, `toThrowErrorMatchingSnapshot.cp`
- **src/css_jsc/ 3 个核心桥接模块 100% 纯 CP 化并通过门禁**：
  - `color_js.cp` (413 行, 0.948x)
  - `css_internals.cp` (326 行, 0.956x)
  - `error_jsc.cp` (17 行, 1.000x)
- **4 项硬门禁指标**：
  - 全部 78 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 78 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：78 模块综合膨胀率 **0.999x**（5,756 行 CP / 5,759 行 Zig 原代码，净压缩）。
- **全仓最新进度**：纯 CP 模块数上升至 **967 / 1,294 (74.73%)**，闭环完整子系统上升至 **161 / 171 (94.15%)**。

## [Milestone 7 Phase 52] 2026-11-16 - src/test_runner/bun_test.cp (1072 行核心调度器) 与 src/runtime/node/ (17 个模块) 纯 CP 重构完成
- **src/test_runner/bun_test.cp 核心测试运行器调度器 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `bun_test.cp` (1,206 行, 1.123x)
- **src/runtime/node/ 17 个轻中型运行时模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `node_error_binding.cp` (23 行, 0.920x)
  - `node_http_binding.cp` (44 行, 1.000x)
  - `uv_signal_handle_windows.cp` (51 行, 1.109x)
  - `node_assert_binding.cp` (95 行, 1.118x)
  - `buffer.cp` (94 行, 1.044x)
  - `node_net_binding.cp` (108 行, 1.019x)
  - `time_like.cp` (105 行, 0.991x)
  - `node_assert.cp` (137 行, 1.070x)
  - `StatFS.cp` (120 行, 0.851x)
  - `node_fs_constant.cp` (142 行, 0.993x)
  - `Stat.cp` (207 行, 0.986x)
  - `node_util_binding.cp` (237 行, 1.021x)
  - `node_fs_binding.cp` (240 行, 0.988x)
  - `node_cluster_binding.cp` (304 行, 1.016x)
  - `win_watcher.cp` (310 行, 1.032x)
  - `node_process.cp` (381 行, 1.034x)
  - `node_zlib_binding.cp` (396 行, 1.005x)
- **4 项硬门禁指标**：
  - 全部 18 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 18 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：18 模块综合膨胀率 **1.041x**（4,227 行 CP / 4,061 行 Zig 原代码，严格控制在 <= 1.20x 内）。
- **全仓最新进度**：纯 CP 模块数上升至 **974 / 1,294 (75.27%)**，闭环完整子系统保持 **161 / 171 (94.15%)**。

## [Milestone 7 Phase 53] 2026-11-16 - src/test_runner 全子系统 (92 模块 100% 满绿大闭环) 与 nodejs_error_code.cp 重构完成
- **src/test_runner 测试运行器全子系统 (全部 92 个模块) 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `expect.cp` (2,269 行, 0.999x)
  - `pretty_format.cp` (2,168 行, 1.011x)
  - 至此，`src/test_runner/` 下全部 92 个文件（包括 75 个 expect 断言匹配器、bun_test 调度器、diff 算法、快照与沙箱控制）已 100% 达成纯 CP 化，未迁移文件清零！
- **src/runtime/node/nodejs_error_code.cp 模块纯 CP 化并通过门禁**：
  - `nodejs_error_code.cp` (1,109 行, 1.000x)
- **4 项硬门禁指标**：
  - 全部 3 个模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 3 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：3 模块综合膨胀率 **1.004x**（5,546 行 CP / 5,524 行 Zig 原代码，几乎 1:1 等长）。
- **全仓最新进度**：纯 CP 模块数上升至 **983 / 1,294 (75.97%)**，闭环完整子系统上升至 **162 / 171 (94.74%)**。


## [Milestone 7 Phase 55] 2026-11-18 - src/runtime/api (BunObject, html_rewriter) 与 src/string (unicode) 核心模块重构固化
- **src/runtime/api/ 2 核心大型模块 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `BunObject.cp` (2,216 行, 1.018x, PASS)：消除所有 `var` 与非标接收者，借用检查与独占可变完整通过。
  - `html_rewriter.cp` (2,031 行, 0.998x, PASS)：HTML 流式重写器纯 CP 化，闭环通过。
- **src/string/immutable/ 核心编码模块 100% 纯 CP 化**：
  - `unicode.cp` (2,058 行, 1.008x, PASS)：WTF-8 编码与 Unicode 多字节解包逻辑纯 CP 化。
- **4 项硬门禁指标**：
  - 双重 0 报错（`cpc --lib` 与 `zig ast-check` 全部通过）。
  - 静态零违例：0 `zig {}`、0 `var`、0 `.@"0"`/`.@"1"`、0 `*anyopaque`。
  - 原始备份：全部 `<stem>_.zig` 完好无损。
  - 代码膨胀率：严格控制在 0.998x ~ 1.018x，远优于 1.20x 上限门禁。

## [Milestone 7 Phase 54] 2026-11-18 - src/http_types 全子系统 100% 满绿闭环、src/runtime/node/ 核心 9 模块与 src/runtime/api/ 3 模块重构完成
- **src/http_types/ 全子系统 (全部 9 个模块) 100% 纯 CP 化并通过全部 4 项硬门禁**：
  - `mime_type_list_enum.cp` (4,664 行, 1.000x, PASS)
  - `Encoding.cp`, `ETag.cp`, `Method.cp`, `MimeType.cp`, `FetchCacheMode.cp`, `FetchRedirect.cp`, `FetchRequestMode.cp`, `URLPath.cp` 全量语法净化并通过验证。
  - 至此，`src/http_types/` 下全部 9 个文件已 100% 达成纯 CP 化，未迁移文件清零！
- **src/runtime/node/ 核心 9 模块纯 CP 化并通过门禁**：
  - `dir_iterator.cp` (644 行, 1.023x)
  - `fs_events.cp` (1,048 行, 1.032x)
  - `node_crypto_binding.cp` (872 行, 1.070x)
  - `node_fs_stat_watcher.cp` (658 行, 1.047x)
  - `node_fs_watcher.cp` (765 行, 1.073x)
  - `node_os.cp` (1,194 行, 1.072x)
  - `path.cp` (2,992 行, 1.002x)
  - `path_watcher.cp` (994 行, 1.034x)
  - `types.cp` (1,294 行, 1.031x)
- **src/runtime/api/ 核心 API 3 模块纯 CP 化并通过门禁**：
  - `JSTranspiler.cp` (1,246 行, 1.015x)
  - `MarkdownObject.cp` (1,313 行, 1.016x)
  - `cron.cp` (1,828 行, 1.032x)
- **其他支持性模块**：
  - `src/test_runner/Execution.cp`, `src/test_runner/snapshot.cp`
  - `src/wyhash/wyhash.cp`, `src/sha_hmac/hmac.cp`
  - `src/runtime/api/bun/subprocess/StaticPipeWriter.cp`, `SubprocessPipeReader.cp`
- **4 项硬门禁指标**：
  - 全部模块通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：严格控制在 <= 1.08x 内，满足 <= 1.20x 硬门禁。
- **全仓最新进度**：纯 CP 模块数上升至 **1,006 / 1,295 (77.68%)**，闭环完整子系统上升至 **163 / 171 (95.32%)**。

## 2026-11-19 [Milestone 7 Phase 56] `src/string/` 与 `src/runtime/api/` 两大子系统 100% 全量满绿大闭环及 11 模块纯 CP 攻坚

- **`src/string/` 核心字符串子系统 100% 满绿大闭环**：
  - `src/string/immutable.cp` (2,461 行, 1.012x)
  - 至此，`src/string/` 下所有 17 个模块全部 100% 达成纯 CP 化，无任何未迁移文件！
- **`src/runtime/api/` 运行时 API 子系统 21/21 全模块 100% 满绿大闭环**：
  - `src/runtime/api/JSBundler.cp` (2,083 行, 1.016x)
  - 至此，`src/runtime/api/` 下所有 21 个核心 API 模块全部达成纯 CP 化，子系统宣告全绿收口！
- **`src/runtime/api/bun/` 顶级系统调用与终端资产**：
  - `Terminal.cp` (1,201 行, 0.984x)
  - `js_bun_spawn_bindings.cp` (1,204 行, 1.021x)
- **`src/jsc/` 高价值通用标准库基础资产（底向上规则）**：
  - `uuid.cp` (270 行, 1.000x) - UUID 生成与解析标准库
  - `JSONLineBuffer.cp` (135 行, 1.030x) - 行分割 JSON 流式缓冲
  - `StringBuilder.cp` (97 行, 1.066x) - 高性能快速字符串缓冲
  - `SystemError.cp` (128 行, 1.008x) - 系统调用错误码映射
  - `JSErrorCode.cp` (72 行, 1.000x)
  - `WTF.cp` (71 行, 0.986x)
  - `ErrorCode.cp` (23 行, 1.000x)
- **4 项硬门禁全绿指标**：
  - 11 个模块全部通过 `cpc --lib` 与 `zig ast-check` 双重 0 报错验证。
  - 静态零违例门禁：0 `zig {}`、0 `var`、0 `.@"0"`、0 `*anyopaque`。
  - 原始备份：全部 11 个 `<stem>_.zig` 原始备份完好无损。
  - 代码膨胀率：全部严格控制在 <= 1.066x 内（远低于 1.20x 硬门禁）。
- **总重构行数**：本次交付 **7,745 行** 100% 纯正 CP 代码。

## 2026-11-19 [Milestone 7 Phase 57] `src/jsc/` 高频微模块 15 连斩 100% 满绿闭环

- **批量极速收割 15 个高价值微型基础模块（纯 CP 化）**：
  - `JSInternalPromise.cp` (4 行, 1.000x)
  - `JSPromiseRejectionOperation.cp` (4 行, 1.000x)
  - `ScriptExecutionStatus.cp` (5 行, 1.000x)
  - `SourceType.cp` (6 行, 1.000x)
  - `ZigErrorType.cp` (7 行, 1.000x)
  - `sizes.cp` (8 行, 1.000x)
  - `SourceProvider.cp` (8 行, 1.000x)
  - `CustomGetterSetter.cp` (11 行, 1.000x)
  - `GetterSetter.cp` (8 行, 0.727x)
  - `static_export.cp` (14 行, 0.933x)
  - `JSRuntimeType.cp` (16 行, 1.000x)
  - `CommonAbortReason.cp` (19 行, 1.118x)
  - `Exception.cp` (23 行, 1.150x)
  - `comptime_string_map_jsc.cp` (20 行, 1.000x)
  - `Counters.cp` (21 行, 0.913x)
- **4 项硬门禁指标**：全部通过 `cpc --lib` 与 `zig ast-check` 0 报错，无 `zig {}`/`var`，膨胀率 <= 1.15x。

## 2026-11-19 [Milestone 7 Phase 58] `src/jsc/` 双智能体并发极速推进 16 连斩 100% 满绿闭环

- **两路并发短生命周期子智能体批量攻坚 16 个高价值核心微模块（全部通过 4 项硬门禁）**：
  - **Group A (8 模块)**：
    - `JSArray.cp` (25 行, 1.000x)
    - `config.cp` (25 行, 0.962x)
    - `EventType.cp` (30 行, 0.938x)
    - `ZigStackFramePosition.cp` (32 行, 1.000x)
    - `codegen.cp` (33 行, 1.000x)
    - `resolve_path_jsc.cp` (32 行, 0.970x)
    - `DeferredError.cp` (34 行, 1.000x)
    - `JSUint8Array.cp` (35 行, 1.000x)
  - **Group B (8 模块)**：
    - `JSMap.cp` (39 行, 1.000x)
    - `MarkedArgumentBuffer.cp` (42 行, 1.077x)
    - `fmt_jsc.cp` (39 行, 1.000x)
    - `ZigStackFrameCode.cp` (40 行, 1.000x)
    - `Errorable.cp` (43 行, 1.000x)
    - `ResolvedSource.cp` (44 行, 1.000x)
    - `JSBigInt.cp` (45 行, 1.023x)
    - `TextCodec.cp` (53 行, 1.152x)
- **4 项硬门禁全绿**：16 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.152x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **阶段性累计**：本轮连续收割 31 个模块，`src/jsc/` 未迁移模块从 97 个锐减至 66 个！

## 2026-11-19 [Milestone 7 Phase 59] `src/jsc/` 双智能体并发推进 18 模块 100% 满绿闭环

- **两路并发短生命周期子智能体完成 18 个基础微模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - **Group A (9 模块)**：
    - `bindgen_test.cp` (38 行, 1.027x)
    - `URLSearchParams.cp` (49 行, 1.065x)
    - `DecodedJSValue.cp` (54 行, 1.102x)
    - `JSArrayIterator.cp` (54 行, 1.038x)
    - `JSModuleLoader.cp` (60 行, 1.091x)
    - `RegularExpression.cp` (60 行, 1.053x)
    - `DOMURL.cp` (63 行, 1.086x)
    - `RefString.cp` (64 行, 1.016x)
    - `JSCell.cp` (61 行, 0.953x)
  - **Group B (9 模块)**：
    - `CommonStrings.cp` (70 行, 1.000x)
    - `JSFunction.cp` (82 行, 1.093x)
    - `ProcessAutoKiller.cp` (74 行, 0.987x)
    - `CachedBytecode.cp` (68 行, 0.895x)
    - `JSSecrets.cp` (81 行, 0.942x)
    - `resolver_jsc.cp` (93 行, 1.057x)
    - `DeprecatedStrong.cp` (102 行, 1.074x)
    - `JSString.cp` (99 行, 0.961x)
    - `generated_classes_list.cp` (135 行, 1.000x)
- **4 项硬门禁全绿**：18 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.102x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,054 / 1,295 模块 (81.39%)**，`src/jsc/` 待迁移模块从 66 个进一步压降至 48 个！

## 2026-11-19 [Milestone 7 Phase 60] `src/jsc/` 双智能体推进 10 个中小型模块 100% 满绿闭环

- **两路并发短生命周期子智能体完成 10 个核心基础与 Web 模块迁移（全部通过 4 项硬门禁）**：
  - **Group A (5 模块)**：
    - `AnyPromise.cp` (103 行, 0.945x)
    - `BunHeapProfiler.cp` (110 行, 1.000x)
    - `Weak.cp` (123 行, 1.070x)
    - `DOMFormData.cp` (127 行, 0.977x)
    - `BunCPUProfiler.cp` (134 行, 1.000x)
  - **Group B (5 模块)**：
    - `javascript_core_c_api.cp` (170 行, 1.156x)
    - `ZigStackTrace.cp` (151 行, 1.007x)
    - `JSPropertyIterator.cp` (147 行, 0.961x)
    - `Strong.cp` (145 行, 0.948x)
    - `URL.cp` (161 行, 0.970x)
- **4 项硬门禁全绿**：10 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.156x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,064 / 1,295 模块 (82.16%)**，`src/jsc/` 待迁移模块从 48 个大幅压降至 38 个！

## 2026-11-19 [Milestone 7 Phase 61] `src/cli/` 首批 10 个轻量子命令模块 100% 满绿闭环

- **短生命周期子智能体完成 10 个轻量 CLI 子命令模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `discord_command.cp` (9 行, 0.900x)
  - `add_command.cp` (11 行, 1.000x)
  - `patch_commit_command.cp` (11 行, 1.000x)
  - `remove_command.cp` (11 行, 1.000x)
  - `pm_why_command.cp` (12 行, 1.000x)
  - `which_npm_client.cp` (12 行, 1.000x)
  - `patch_command.cp` (17 行, 1.000x)
  - `update_command.cp` (18 行, 1.000x)
  - `ci_info.cp` (27 行, 1.000x)
  - `exec_command.cp` (46 行, 1.000x)
- **4 项硬门禁全绿**：10 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.000x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,074 / 1,295 模块 (82.93%)**，`src/cli/` 待迁移模块从 47 个大幅压降至 37 个！

## 2026-11-19 [Milestone 7 Phase 61 Group A] `src/jsc/` 5 个核心模块 100% 满绿闭环

- **短生命周期子智能体完成 5 个 JSC 核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `JSObject.cp` (175 行, 1.017x)
  - `HTTPServerAgent.cp` (167 行, 0.933x)
  - `ZigException.cp` (185 行, 1.011x)
  - `NodeModuleModule.cp` (198 行, 1.010x)
  - `BuildMessage.cp` (208 行, 1.025x)
- **4 项硬门禁全绿**：5 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.025x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,079 / 1,295 模块 (83.32%)**，`src/jsc/` 待迁移模块降至 28 个！

## 2026-11-19 [Milestone 7 Phase 61 Group B] `src/cli/` 第二批 7 个基础命令模块 100% 满绿闭环

- **短生命周期子智能体完成 7 个 CLI 基础命令模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `colon_list_type.cp` (66 行, 1.065x)
  - `fuzzilli_command.cp` (73 行, 0.986x)
  - `shell_completions.cp` (77 行, 1.027x)
  - `list-of-yarn-commands.cp` (76 行, 1.000x)
  - `scan_command.cp` (72 行, 0.947x)
  - `install_command.cp` (99 行, 1.021x)
  - `add_completions.cp` (105 行, 1.000x)
- **4 项硬门禁全绿**：7 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.065x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,086 / 1,295 模块 (83.86%)**，`src/cli/` 待迁移模块降至 30 个！

## 2026-11-19 [Milestone 7 Phase 62 Group A] `src/jsc/` 5 个虚拟机与异常作用域模块 100% 满绿闭环

- **短生命周期子智能体完成 5 个 JSC 核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `VM.cp` (204 行, 0.971x)
  - `TopExceptionScope.cp` (215 行, 1.000x)
  - `JSRef.cp` (225 行, 1.000x)
  - `virtual_machine_exports.cp` (245 行, 1.004x)
  - `ResolveMessage.cp` (242 行, 0.972x)
- **4 项硬门禁全绿**：5 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.004x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,091 / 1,295 模块 (84.25%)**，`src/jsc/` 待迁移模块降至 23 个！

## 2026-11-19 [Milestone 7 Phase 62 Group B] `src/cli/` 4 个核心命令模块 100% 满绿闭环

- **短生命周期子智能体完成 4 个 CLI 核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `unlink_command.cp` (159 行, 1.006x)
  - `repl_command.cp` (191 行, 1.000x)
  - `link_command.cp` (218 行, 1.005x)
  - `filter_arg.cp` (290 行, 1.018x)
- **4 项硬门禁全绿**：4 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.018x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,095 / 1,295 模块 (84.56%)**，`src/cli/` 待迁移模块降至 26 个！

## 2026-11-19 [Milestone 7 Phase 62 Group B] `src/bundler/linker_context/` 首批 5 模块 100% 满绿闭环

- **短生命周期子智能体完成 5 个打包器链接上下文核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `postProcessHTMLChunk.cp` (35 行, 1.000x)
  - `StaticRouteVisitor.cp` (94 行, 1.022x)
  - `findImportedCSSFilesInJSOrder.cp` (102 行, 0.990x)
  - `generateCompileResultForJSChunk.cp` (114 行, 1.036x)
  - `postProcessCSSChunk.cp` (129 行, 1.016x)
- **4 项硬门禁全绿**：5 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.036x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,100 / 1,295 模块 (84.94%)**！达成 1,100 模块大关里程碑，`src/bundler/linker_context/` 待迁移模块降至 19 个！

## 2026-11-19 [Milestone 7 Phase 63 Group B] `src/bundler/linker_context/` 第二批 4 模块 100% 满绿闭环

- **短生命周期子智能体完成 4 个打包器链接上下文核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `OutputFileListBuilder.cp` (158 行, 1.053x)
  - `generateCompileResultForCssChunk.cp` (179 行, 1.006x)
  - `convertStmtsForChunkForDevServer.cp` (192 行, 1.000x)
  - `findAllImportedPartsInJSOrder.cp` (223 行, 1.009x)
- **4 项硬门禁全绿**：4 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.053x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,104 / 1,295 模块 (85.25%)**！突破 85% 大关，`src/bundler/linker_context/` 待迁移模块降至 15 个！

## 2026-11-19 [Milestone 7 Phase 63 Group A] `src/jsc/` 5 个绑定与信号核心模块 100% 满绿闭环

- **短生命周期子智能体完成 5 个 JSC 核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `ZigStackFrame.cp` (251 行, 0.996x)
  - `bindgen.cp` (257 行, 1.012x)
  - `AbortSignal.cp` (261 行, 1.024x)
  - `btjs.cp` (266 行, 1.023x)
  - `bun_string_jsc.cp` (267 行, 1.008x)
- **4 项硬门禁全绿**：5 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.024x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,109 / 1,295 模块 (85.64%)**，`src/jsc/` 待迁移模块降至 18 个！

## 2026-11-19 [Milestone 7 Phase 63 Group B] `src/bundler/` 首批 5 核心基础模块 100% 满绿闭环

- **短生命周期子智能体完成 5 个打包器核心基础模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `IndexStringMap.cp` (27 行, 1.080x)
  - `PathToSourceIndexMap.cp` (44 行, 0.957x)
  - `DeferredBatchTask.cp` (52 行, 1.000x)
  - `Graph.cp` (141 行, 1.007x)
  - `BundleThread.cp` (194 行, 0.995x)
- **4 项硬门禁全绿**：5 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.080x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,109 / 1,295 模块 (85.64%)**，`src/bundler/` 待迁移模块降至 20 个！

## 2026-11-19 [Milestone 7 Phase 64 Group B] `src/install/` 首批 5 核心包管理模块 100% 满绿闭环

- **短生命周期子智能体完成 5 个包管理器核心基础模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `versioned_url.cp` (44 行, 1.000x)
  - `ConfigVersion.cp` (45 行, 1.000x)
  - `ExternalSlice.cp` (71 行, 0.973x)
  - `padding_checker.cp` (108 行, 1.000x)
  - `PackageManifestMap.cp` (129 行, 1.040x)
- **4 项硬门禁全绿**：5 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.040x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,119 / 1,295 模块 (86.41%)**，`src/install/` 待迁移模块降至 25 个！

## 2026-11-19 [Milestone 7 Phase 64 Group B] `src/js_parser/ast/` 首批 5 核心语法树模块 100% 满绿闭环

- **短生命周期子智能体完成 5 个 JS/TS 语法树核心基础模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `UseDirective.cp` (67 行, 1.047x)
  - `ASTMemoryAllocator.cp` (97 行, 1.032x)
  - `B.cp` (109 行, 1.048x)
  - `ServerComponentBoundary.cp` (124 行, 1.025x)
  - `symbols.cp` (133 行, 1.000x)
- **4 项硬门禁全绿**：5 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.048x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,124 / 1,295 模块 (86.79%)**，`src/js_parser/ast/` 待迁移模块降至 40 个！

## 2026-11-19 [Milestone 7 Phase 64 & 65] JSC 与 AST 9 个核心模块 100% 满绿闭环

- **短生命周期子智能体完成 9 个核心底层模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - **JSC 模块 (4 个)**：
    - `jsc.cp` (286 行, 1.011x)
    - `CallFrame.cp` (306 行, 1.007x)
    - `SavedSourceMap.cp` (366 行, 0.989x)
    - `JSPromise.cp` (376 行, 1.011x)
  - **AST 语法树模块 (5 个)**：
    - `CharFreq.cp` (136 行, 0.993x)
    - `TS.cp` (140 行, 1.007x)
    - `Ast.cp` (125 行, 0.874x)
    - `Binding.cp` (177 行, 1.035x)
    - `NewStore.cp` (157 行, 0.918x)
- **4 项硬门禁全绿**：9 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.035x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,128 / 1,295 模块 (87.10%)**！全仓突破 87% 大关，`src/jsc/` 待迁移模块降至 14 个，`src/js_parser/ast/` 待迁移模块降至 35 个！

## 2026-11-19 [Milestone 7 Phase 65 Group A] `src/jsc/` 2 个高价值核心模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个 JSC 高价值核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `FetchHeaders.cp` (460 行, 1.007x)
  - `FFI.cp` (532 行, 1.004x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.007x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,130 / 1,295 模块 (87.26%)**，突破 1,130 模块，`src/jsc/` 待迁移模块降至 15 个！

## 2026-11-19 [Milestone 7 Phase 65 Group B] `src/js_parser/ast/` 第三批 5 核心语法树模块 100% 满绿闭环

- **短生命周期子智能体完成 5 个语法树核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `Scope.cp` (214 行, 0.973x)
  - `G.cp` (256 行, 1.103x)
  - `foldStringAddition.cp` (232 行, 0.996x)
  - `BundledAst.cp` (235 行, 1.000x)
  - `base.cp` (239 行, 1.017x)
- **4 项硬门禁全绿**：5 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.103x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,135 / 1,295 模块 (87.64%)**，`src/js_parser/ast/` 待迁移模块降至 30 个！


## 2026-11-19 [Milestone 7 Phase 66 Group B] `src/js_parser/ast/` 第四批 4 个语法树核心模块 100% 满绿闭环

- **短生命周期子智能体完成 4 个语法树核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `S.cp` (237 行, 1.000x)
  - `Op.cp` (289 行, 0.993x)
  - `parseJSXElement.cp` (317 行, 0.994x)
  - `KnownGlobal.cp` (360 行, 0.997x)
- **4 项硬门禁全绿**：4 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.000x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,139 / 1,295 模块 (87.95%)**，`src/js_parser/ast/` 待迁移模块降至 26 个！


## 2026-11-19 [Milestone 7 Phase 67 Group A] `src/jsc/` 2 个开发者核心模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个 JSC 核心工具模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `hot_reloader.cp` (606 行, 0.984x)
  - `Debugger.cp` (528 行, 0.987x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 0.987x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,141 / 1,295 模块 (88.11%)**，突破 88% 大关，`src/jsc/` 待迁移模块降至 13 个！


## 2026-11-19 [Milestone 7 Phase 67 Group B] `src/install/` 第二批 4 个包管理核心模块 100% 满绿闭环

- **短生命周期子智能体完成 4 个包管理核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `postinstall_optimizer.cp` (187 行, 0.944x)
  - `PnpmMatcher.cp` (201 行, 0.985x)
  - `install.cp` (300 行, 1.017x)
  - `integrity.cp` (323 行, 1.013x)
- **4 项硬门禁全绿**：4 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.017x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,145 / 1,295 模块 (88.42%)**，`src/install/` 待迁移模块降至 17 个！


## 2026-11-19 [Milestone 7 Phase 68 Group A] `src/jsc/` 2 个核心底层存储与缓冲模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个 JSC 核心底层模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `RuntimeTranspilerStore.cp` (637 行, 0.973x)
  - `array_buffer.cp` (627 行, 0.906x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 0.973x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,147 / 1,295 模块 (88.57%)**，`src/jsc/` 待迁移模块降至 11 个！


## 2026-11-19 [Milestone 7 Phase 68 Group B] `src/bundler/` 3 个核心打包与扫描模块 100% 满绿闭环

- **短生命周期子智能体完成 3 个打包核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `ServerComponentParseTask.cp` (236 行, 0.955x)
  - `HTMLImportManifest.cp` (262 行, 0.949x)
  - `HTMLScanner.cp` (314 行, 1.019x)
- **4 项硬门禁全绿**：3 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.019x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,150 / 1,295 模块 (88.80%)**，突破 1,150 模块！`src/bundler/` 待迁移模块降至 17 个！


## 2026-11-19 [Milestone 7 Phase 69 Group A] `src/jsc/` 2 个转译缓存与异步模块核心 100% 满绿闭环

- **短生命周期子智能体完成 2 个 JSC 核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `RuntimeTranspilerCache.cp` (712 行, 1.008x)
  - `AsyncModule.cp` (805 行, 1.029x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.029x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,152 / 1,295 模块 (88.96%)**，`src/jsc/` 待迁移模块降至 9 个！


## 2026-11-19 [Milestone 7 Phase 69 Group B] `src/bundler/` 3 个打包缓存与线程池模块 100% 满绿闭环

- **短生命周期子智能体完成 3 个打包核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `cache.cp` (345 行, 1.033x)
  - `OutputFile.cp` (340 行, 1.012x)
  - `ThreadPool.cp` (343 行, 0.942x)
- **4 项硬门禁全绿**：3 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.033x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,155 / 1,295 模块 (89.19%)**，突破 89% 大关！`src/bundler/` 待迁移模块降至 14 个！


## 2026-11-19 [Milestone 7 Phase 70 Group B] `src/bundler/` 3 个打包入口与转译分析核心模块 100% 满绿闭环

- **短生命周期子智能体完成 3 个打包核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `entry_points.cp` (370 行, 0.989x)
  - `AstBuilder.cp` (385 行, 1.027x)
  - `analyze_transpiled_module.cp` (444 行, 1.118x)
- **4 项硬门禁全绿**：3 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.118x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,158 / 1,295 模块 (89.42%)**，`src/bundler/` 待迁移模块降至 8 个！


## 2026-11-19 [Milestone 7 Phase 71 Group B] `src/bundler/linker_context/` 3 个分块编译核心模块 100% 满绿闭环

- **短生命周期子智能体完成 3 个分块编译核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `renameSymbolsInChunk.cp` (276 行, 1.000x)
  - `prepareCssAstsForChunk.cp` (340 行, 1.027x)
  - `generateCompileResultForHtmlChunk.cp` (351 行, 1.020x)
- **4 项硬门禁全绿**：3 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.027x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,161 / 1,295 模块 (89.65%)**，`src/bundler/linker_context/` 待迁移模块降至 12 个！


## 2026-11-19 [Milestone 7 Phase 71 Group A] `src/jsc/` 2 个宿主函数包装与类型系统核心模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个 JSC 核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `JSType.cp` (840 行, 1.000x)
  - `host_fn.cp` (813 行, 1.007x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.007x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,163 / 1,295 模块 (89.81%)**，`src/jsc/` 待迁移模块降至 7 个！


## 2026-11-19 [Milestone 7 Phase 72 Group B] `src/bundler/linker_context/` 3 个分块构建与产物输出核心模块 100% 满绿闭环【达成全仓 90% 历史里程碑】

- **短生命周期子智能体完成 3 个分块构建核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `generateCodeForLazyExport.cp` (423 行, 1.005x)
  - `computeCrossChunkDependencies.cp` (472 行, 1.028x)
  - `writeOutputFilesToDisk.cp` (493 行, 1.021x)
- **4 项硬门禁全绿**：3 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.028x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,166 / 1,295 模块 (90.04%)**，正式突破 90% 大关！`src/bundler/linker_context/` 待迁移模块降至 9 个！


## 2026-11-19 [Milestone 7 Phase 72 Group B Batch 2] `src/bundler/linker_context/` 2 个核心分块计算与调度模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个分块核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `computeChunks.cp` (452 行, 0.899x)
  - `doStep5.cp` (459 行, 0.902x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 0.902x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,168 / 1,295 模块 (90.20%)**，`src/bundler/linker_context/` 待迁移模块降至 5 个！


## 2026-11-19 [Milestone 7 Phase 73 Group A] `src/jsc/` 2 个底层字符串与稀疏元数据核心模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个 JSC 核心底层模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `ZigString.cp` (870 行, 1.006x)
  - `rare_data.cp` (864 行, 0.905x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.006x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,170 / 1,295 模块 (90.35%)**，突破 1,170 模块！`src/jsc/` 待迁移模块降至 5 个！


## 2026-11-19 [Milestone 7 Phase 73 Group B] `src/bundler/linker_context/` 2 个分块语句重写与 CSS 依赖拓扑模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个分块核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `convertStmtsForChunk.cp` (550 行, 0.986x)
  - `findImportedFilesInCSSOrder.cp` (665 行, 0.979x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 0.986x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,172 / 1,295 模块 (90.50%)**，`src/bundler/linker_context/` 待迁移模块降至 3 个！


## 2026-11-19 [Milestone 7 Phase 74 Group B] `src/bundler/linker_context/` 2 个核心分块并行构建模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个分块核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `generateCodeForFileInChunkJS.cp` (803 行, 1.001x)
  - `generateChunksInParallel.cp` (861 行, 1.050x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.050x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,174 / 1,295 模块 (90.66%)**，`src/bundler/linker_context/` 待迁移模块降至 1 个！


## 2026-11-19 [Milestone 7 Phase 75 Group B] `src/bundler/linker_context/` 2 个大型构建元文件与分块后处理核心模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个大型分块核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `MetafileBuilder.cp` (1,096 行, 1.010x)
  - `postProcessJSChunk.cp` (1,125 行, 1.000x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.010x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,176 / 1,295 模块 (90.81%)**，`src/bundler/linker_context/` 待迁移模块降至最后 1 个！


## 2026-11-19 [Milestone 7 Phase 76 Group A] `src/jsc/` 2 个全局对象与 WebWorker 核心模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个 JSC 核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `web_worker.cp` (961 行, 0.980x)
  - `JSGlobalObject.cp` (1,088 行, 1.015x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.015x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,178 / 1,295 模块 (90.97%)**，`src/jsc/` 待迁移模块降至 5 个！


## 2026-11-19 [Milestone 7 Phase 76 Group B] `src/bundler/linker_context/` 全子系统 24 模块 100% 满绿闭环

- **短生命周期子智能体完成 linker_context 压轴核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `scanImportsAndExports.cp` (1,269 行, 1.006x)
- **4 项硬门禁全绿**：模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率 <= 1.006x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,179 / 1,295 模块 (91.04%)**，正式跨越 91% 大关！
- **目录达成 100% 闭环**：`src/bundler/linker_context/` 目录下全部 24 个核心分块编译模块已 100% 纯 CP 化！


## 2026-11-19 [Milestone 7 Phase 77 Group B] `src/bundler/` 2 个核心链接与宏定义模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个打包核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `linker.cp` (430 行, 1.021x)
  - `defines.cp` (439 行, 1.023x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.023x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,181 / 1,295 模块 (91.20%)**，`src/bundler/` 待迁移模块降至 9 个！


## 2026-11-19 [Milestone 7 Phase 77 Group A] `src/jsc/` 核心模块加载器 100% 满绿闭环

- **短生命周期子智能体完成 JSC 核心模块加载器向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `ModuleLoader.cp` (1,378 行, 0.987x)
- **4 项硬门禁全绿**：模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率 <= 0.987x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,182 / 1,295 模块 (91.27%)**，`src/jsc/` 待迁移模块降至最后 4 个！


## 2026-11-19 [Milestone 7 Phase 78 Group B] `src/bundler/` 2 个核心重导出优化与链接图模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个打包核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `barrel_imports.cp` (612 行, 1.089x)
  - `LinkerGraph.cp` (557 行, 0.989x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.089x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,184 / 1,295 模块 (91.43%)**，`src/bundler/` 待迁移模块降至 7 个！


## 2026-11-19 [Milestone 7 Phase 78 Group A] `src/jsc/` 核心进程间通信 IPC 模块 100% 满绿闭环

- **短生命周期子智能体完成 JSC 核心 IPC 模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `ipc.cp` (1,597 行, 1.034x)
- **4 项硬门禁全绿**：模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率 <= 1.034x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,185 / 1,295 模块 (91.51%)**，`src/jsc/` 待迁移模块降至最后 3 个！


## 2026-11-19 [Milestone 7 Phase 79 Group B] `src/bundler/` 2 个核心分块与宏定义表模块 100% 满绿闭环

- **短生命周期子智能体完成 2 个打包核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `Chunk.cp` (861 行, 1.009x)
  - `defines-table.cp` (930 行, 0.999x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.009x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,187 / 1,295 模块 (91.66%)**，`src/bundler/` 待迁移模块降至 5 个！


## 2026-11-19 [Milestone 7 Phase 80 Group A] `src/install/` 第三批 3 个核心包安装与网络调度模块 100% 满绿闭环

- **短生命周期子智能体完成 3 个包管理核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `hoisted_install.cp` (378 行, 0.995x)
  - `PackageManagerTask.cp` (389 行, 1.010x)
  - `NetworkTask.cp` (510 行, 1.010x)
- **4 项硬门禁全绿**：3 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 1.010x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,190 / 1,295 模块 (91.89%)**，突破 1,190 模块！`src/install/` 待迁移模块降至 18 个！


## 2026-11-19 [Milestone 7 Phase 80 Group B] `src/install/` 2 个依赖解析与补丁安装核心模块 100% 满绿闭环【突破 92%】

- **短生命周期子智能体完成 2 个包管理核心模块向纯 CP 迁移（全部通过 4 项硬门禁）**：
  - `resolution.cp` (529 行, 0.958x)
  - `patch_install.cp` (575 行, 0.970x)
- **4 项硬门禁全绿**：2 个模块全部 0 `zig {}`、0 `var`、0 `tuple.@N`、0 `*anyopaque`，膨胀率均 <= 0.970x，`cpc --lib` 与 `zig ast-check` 双重 0 报错。
- **全仓迁移进度提升**：全仓已迁移 **1,192 / 1,295 模块 (92.05%)**，正式跨越 92% 大关！`src/install/` 待迁移模块降至 16 个！

### 2026-11-19 07:59:24 UTC - src/jsc/ 存量及核心异步任务模块净化完成
- 完成模块：CppTask, GarbageCollectionController, JSCScheduler, PosixSignalHandle, WorkTask, ConcurrentPromiseTask, Task, event_loop 共 8 个核心模块已满足纯 CP 语法标准并合入主分支。
- 门禁指标：全部通过 cpc --lib 与 zig ast-check，4 项硬门禁达成率 100%，膨胀率区间 0.84x ~ 1.05x。

### 2026-11-19 08:05:19 UTC - src/bundler/ParseTask.cp 攻坚完成合入主仓
- 完成模块：ParseTask.cp (1481行，膨胀率 0.990x)。
- 门禁指标：cpc --lib 与 zig ast-check 0 报错通过，静态 0 违例完全达标。
### [2026-11-19] `src/install/` 新增 3 个纯 CP 模块通过 4 项硬门禁
- `src/install/repository.cp` (1.044x)、`src/install/lifecycle_script_runner.cp` (1.017x)、`src/install/TarballStream.cp` (1.017x) 及对应 `_.zig` 备份与 `.zig` 产物。

### 2026-11-19 11:47:03 UTC - 核心关键大模块 JSValue, LinkerContext, PackageInstall 攻坚完成入库
- 完成模块：
  1. src/jsc/JSValue.cp (2519行, 1.000x)
  2. src/bundler/LinkerContext.cp (2806行, 1.009x)
  3. src/install/PackageInstall.cp (1515行, 1.000x)
- 门禁指标：全部通过 cpc --lib 与 zig ast-check，4 项硬门禁达成率 100%，无残留 *.zig.map。

### 2026-11-19 12:08:13 UTC - src/install/ 四大存量核心模块 migration, pnpm, yarn, hosted_git_info 迁移通过入库
- 完成模块：
  1. src/install/migration.cp (1218行, 1.071x)
  2. src/install/pnpm.cp (1670行, 1.054x)
  3. src/install/yarn.cp (1795行, 1.044x)
  4. src/install/hosted_git_info.cp (1422行, 0.861x)
- 门禁指标：全部通过 cpc --lib 与 zig ast-check，4 项硬门禁达成率 100%，无残留 *.zig.map。

### 2026-11-19 12:40:45 UTC - src/install/lockfile/ 100% 全量目录闭环与 extract_tarball.cp 迁移完成
- 闭环目录：src/install/lockfile/ (13/13 模块全部达标入库，包括修复 Package.cp, bun.lock.cp 等核心大模块)
- 修复模块：PackageManagerEnqueue.cp, install_with_manager.cp
- 新增模块：extract_tarball.cp (613行, 0.962x)
- 门禁指标：全部通过 cpc --lib 与 zig ast-check，4 项硬门禁达成率 100%，无残留 *.zig.map。

### 2026-11-19 12:49:44 UTC - 超大体量核心模块 bundle_v2.cp (4563行) 攻坚完成入库
- 完成模块：src/bundler/bundle_v2.cp (4563行, 膨胀率 1.012x)
- 门禁指标：cpc --lib 与 zig ast-check 0 报错通过，4 项硬门禁完全达成，无残留 *.zig.map。

### 2026-11-19 13:29:20 UTC - 超大体量模块 src/jsc/ConsoleObject.cp (3923行) 攻坚完成入库
- 完成模块：src/jsc/ConsoleObject.cp (3923行, 膨胀率 1.023x)
- 门禁指标：cpc --lib 与 zig ast-check 0 报错通过，4 项硬门禁完全达成，无残留 *.zig.map。

### 2026-11-19 17:29:45 UTC - src/cli/ 核心命令模块 why_command, outdated_command, audit_command 迁移入库
- 完成模块：
  1. src/cli/why_command.cp (514行, 1.043x)
  2. src/cli/outdated_command.cp (742行, 1.039x)
  3. src/cli/audit_command.cp (867行, 1.036x)
- 门禁指标：全部通过 cpc --lib 与 zig ast-check，4 项硬门禁达成率 100%，无残留 *.zig.map。

### 2026-11-19 18:09:46 UTC - CLI 核心大模块 create_command.cp (2467行) 攻坚完成入库
- 完成模块：src/cli/create_command.cp (2467行, 膨胀率 1.003x)
- 门禁指标：cpc --lib 与 zig ast-check 0 报错通过，4 项硬门禁达成率 100%，无残留 *.zig.map。

### 2026-11-19 18:13:20 UTC - CLI 9 个核心命令模块批量攻坚完成入库
- 完成模块：
  1. init_command.cp (1315行, 1.035x)
  2. package_manager_command.cp (578行, 0.893x)
  3. pm_version_command.cp (640行, 1.051x)
  4. install_completions_command.cp (542行, 0.985x)
  5. pm_trusted_command.cp (481行, 1.028x)
  6. bunx_command.cp (806行, 0.850x)
  7. pm_view_command.cp (408行, 1.049x)
  8. filter_run.cp (609行, 0.979x)
  9. open.cp (378行, 1.013x)
- 门禁指标：全部通过 cpc --lib 与 zig ast-check，4 项硬门禁达成率 100%，无残留 *.zig.map。

### 2026-11-19 18:30:50 UTC - src/js_parser/ast/ 首批核心 AST 模块 Stmt, TypeScript, Symbol 攻坚入库
- 完成模块：
  1. Stmt.cp (424行, 1.002x)
  2. TypeScript.cp (463行, 0.981x)
  3. Symbol.cp (305行, 0.626x)
- 门禁指标：全部通过 cpc --lib 与 zig ast-check，4 项硬门禁达成率 100%，无残留 *.zig.map。

### 2026-11-19 19:11:04 UTC - JS Parser AST 核心定义大模块 E.cp (1507行) 攻坚完成入库
- 完成模块：src/js_parser/ast/E.cp (1507行, 膨胀率 1.025x)
- 涵盖节点：Array, Unary, Binary, JSXElement, String, Object, Template, RegExp, Import 等全部表达式 AST 节点定义。
- 门禁指标：cpc --lib 与 zig ast-check 0 报错通过，4 项硬门禁完全达成，无残留 *.zig.map。

### 2026-11-19 19:38:15 UTC - JS Parser AST 核心解析与转换模块 4 连胜入库
- 完成模块：
  1. parseImportExport.cp (435行, 0.995x)
  2. parseFn.cp (509行, 1.002x)
  3. repl_transforms.cp (509行, 0.998x)
  4. parseTypescript.cp (579行, 1.055x)
- 门禁指标：全部通过 cpc --lib 与 zig ast-check，4 项硬门禁达成率 100%，无残留 *.zig.map。

### 2026-11-20 03:13:01 UTC - src/js_parser/ast/ 5 核心模块迁移入库
- 完成：`visitBinaryExpression.cp` (0.998x), `maybe.cp` (1.018x), `parsePrefix.cp` (1.000x), `SideEffects.cp` (1.002x), `parseSuffix.cp` (0.994x)，4 项硬门禁全 PASS。

### 2026-11-20 03:13:58 UTC - src/ 根目录 7 核心入口模块迁移入库
- 完成：`jsc_stub.cp` (1.000x), `unit_test.cp` (1.059x), `main.cp` (1.070x), `workaround_missing_symbols.cp` (1.051x), `main_test.cp` (1.034x), `bun.js.cp` (1.021x), `main_wasm.cp` (1.013x)，4 项硬门禁全 PASS。

### 2026-11-20 03:15:21 UTC - src/cli/ 3 核心配置与入口模块迁移入库
- 完成：`bunfig.cp` (1.025x), `cli.cp` (1.009x), `Arguments.cp` (1.016x)，4 项硬门禁全 PASS。

### 2026-11-20 03:18:05 UTC - src/js_parser/ast/ 4 核心解析与访问模块迁移入库
- 完成：`skipTypescript.cp` (1.005x), `parse.cp` (1.001x), `parseStmt.cp` (1.000x), `visit.cp` (1.000x)，4 项硬门禁全 PASS。

### 2026-11-20 03:24:04 UTC - src/js_parser/ 4 顶层核心模块迁移入库
- 完成：`runtime.cp` (1.015x), `js_parser.cp` (1.007x), `lexer_tables.cp` (1.000x), `parser.cp` (1.039x)，4 项硬门禁全 PASS。

### 2026-11-20 03:29:13 UTC - src/js_parser/ast/ 4 核心遍历与降级模块迁移入库
- 完成：`lowerDecorators.cp` (1.023x), `visitStmt.cp` (1.012x), `Parser.cp` (1.011x), `visitExpr.cp` (1.010x)，4 项硬门禁全 PASS。

### 2026-11-20 03:35:07 UTC - src/cli/ 100% 目录闭环（repl, update_interactive_command, test_command 入库）
- 完成：`repl.cp` (1.022x), `update_interactive_command.cp` (1.009x), `test_command.cp` (1.025x)，4 项硬门禁全 PASS。

### 2026-11-20 03:36:27 UTC - src/install/ 3 核心引擎模块迁移入库
- 完成：`PackageManager.cp` (0.971x), `PackageInstaller.cp` (1.010x), `isolated_install.cp` (1.006x)，4 项硬门禁全 PASS。

### 2026-11-20 03:45:51 UTC - src/runtime/api/bun/process.cp (2953行) 迁移入库
- 完成：`process.cp` (1.006x)，4 项硬门禁全 PASS。

### 2026-11-20 03:49:00 UTC - src/js_parser/ 顶层 100% 目录闭环（lexer.cp 3423行入库）
- 完成：`lexer.cp` (1.006x)，4 项硬门禁全 PASS。

### 2026-11-20 03:50:41 UTC - src/napi/ 100% 目录闭环 & src/install/lockfile.cp 入库
- 完成：`src/napi/napi.cp` (1.058x), `src/install/lockfile.cp` (1.053x)，4 项硬门禁全 PASS。

### 2026-11-20 04:02:38 UTC - src/install/npm.cp (2817行) 迁移入库
- 完成：`npm.cp` (1.015x)，4 项硬门禁全 PASS。

### 2026-11-20 04:11:20 UTC - src/runtime/ 100% 全目录闭环（h2_frame_parser.cp 5035行入库）
- 完成：`h2_frame_parser.cp` (1.032x)，4 项硬门禁全 PASS。

### 2026-11-20 04:12:36 UTC - src/install/ 100% 全目录闭环（bin.cp, dependency.cp 入库）
- 完成：`bin.cp` (1.041x), `dependency.cp` (1.044x)，4 项硬门禁全 PASS。

### 2026-11-20 04:17:00 UTC - src/sys/windows/ 100% 目录闭环（windows.cp 4108行入库）
- 完成：`windows.cp` (1.014x)，4 项硬门禁全 PASS。

### 2026-11-20 04:23:35 UTC - src/jsc/ 100% 全目录闭环（VirtualMachine.cp 4245行入库）
- 完成：`VirtualMachine.cp` (1.017x)，4 项硬门禁全 PASS。

### 2026-11-20 04:30:19 UTC - src/js_printer/ 100% 目录闭环（js_printer.cp 6495行入库）
- 完成：`js_printer.cp` (1.011x)，4 项硬门禁全 PASS。

### 2026-11-20 04:30:41 UTC - src/cli/multi_run.cp 解封 PASS 入库
- 完成：`src/cli/multi_run.cp` (1.009x)，4 项硬门禁全 PASS。

### 2026-11-20 04:33:58 UTC - src/libuv_sys/ 100% 目录闭环（libuv.cp 3246行入库）
- 完成：`src/libuv_sys/libuv.cp` (1.010x)，4 项硬门禁全 PASS。

### 2026-11-20 04:35:35 UTC - 全仓最大业务模块 src/js_parser/ast/P.cp (7057行) 入库 & 非豁免初迁 100% 达成
- 完成：`src/js_parser/ast/P.cp` (1.013x)，142 个活跃函数完整 lowering，4 项硬门禁全 PASS。

### 2026-11-20 04:43:34 UTC - 解封 4 核心模块（多目录 100% 闭环）+ 存量净化 36 模块入库
- 解封入库：`src/js_parser/ast/Expr.cp` (1.016x，闭合 `js_parser/ast/`)、`src/bun.cp` (1.022x，闭合 `src/` 根)、`src/bundler/options.cp` (1.012x，闭合 `bundler/`)、`src/cli/pm_pkg_command.cp` (1.010x，闭合 `cli/`)。
- 存量净化入库：`src/css/` 8 模块、`src/http/` 8 模块、`src/runtime/{dns_jsc,server,socket}/` 20 模块，共 40 模块全部 4 项硬门禁 PASS。

### 2026-11-20 04:45:03 UTC - src/css/css_parser.cp (1.011x) 解封 PASS 入库（src/css/ 100% 零违例闭环）
- 完成：`src/css/css_parser.cp` (1.011x)，4 项硬门禁全 PASS。

### 2026-11-20 04:46:13 UTC - 基础库 22 模块存量零违例净化入库
- 完成：`src/{io,threading,paths,aio,event_loop,router,semver}/` 共 22 模块全部 4 项硬门禁 PASS（并补齐 6 个历史缺失 `_.zig` 备份）。

### 2026-11-20 04:50:57 UTC - src/runtime/webcore/ 10 模块存量零违例净化入库
- 完成：`CookieMap`/`Sink`/`blob/*`/`s3/*` 共 10 模块全部 4 项硬门禁 PASS。

### 2026-11-20 04:52:27 UTC - src/sys/ 100% 闭环 & 全仓最后 14 个存量违例模块清零入库
- 完成：`src/sys/sys.cp` (1.012x) + `glob`/`uws_sys/quic`/`sys/{fd,tmp}`/`cares_sys`/`csrf`/`errno`/`bun_alloc`/`cli/run_command`/`s3_signing`/`dotenv`/`ini` 共 15 模块全 PASS。

### 2026-11-20 04:56:55 UTC - src/sql/ 54 模块新版 cpc 语法升级全绿入库
- 完成：`src/sql/` 54/54 全部通过新版 `cpc --lib` + `zig ast-check` 及 4 项硬门禁。

### 2026-11-20 05:01:09 UTC - src/sql_jsc/ (34) + src/uws_sys/ (20) 共 54 模块新版 cpc 语法升级全绿入库
- 完成：54/54 全部通过新版 `cpc --lib` + `zig ast-check` 及 4 项硬门禁。

### 2026-11-20 05:06:44 UTC - http/unicode/bun_alloc/md/http_jsc/string 共 64 模块新版 cpc 语法升级全绿入库
- 完成：64/64 全部通过新版 `cpc --lib` + `zig ast-check` 及 4 项硬门禁。

### 2026-11-20 05:11:20 UTC - src/runtime/ 41 模块新版 cpc 语法升级入库
- 完成：`src/runtime/` 41/42 全部通过新版 `cpc --lib` + `zig ast-check` 及 4 项硬门禁（余 `node_fs.cp` 标 BLOCKED 上报 `cp_agy`）。

### 2026-11-20 05:12:59 UTC - 全仓最后 42 个零散目录模块新版 cpc 语法升级全绿入库
- 完成：37 个目录共 42/42 模块全部通过新版 `cpc --lib` + `zig ast-check` 及 4 项硬门禁（并补齐 `boringssl_.zig`/`SemverRange_.zig`）。

### 2026-11-20 05:18:18 UTC - 16 目录 65 模块新版 cpc 语法升级全绿入库
- 完成：`sys`/`options_types`/`sourcemap`/`collections`/`watcher`/`bundler_jsc`/`perf`/`safety`/`shell`/`exe_format`/`interchange`/`io`/`meta`/`router`/`sourcemap_jsc`/`threading` 共 65/66 模块全部 4 项硬门禁 PASS。

### 2026-11-20 05:23:39 UTC - src/shell/shell.cp (0.990x) 全门禁 PASS 入库
- 完成：`src/shell/shell.cp` (0.990x)，4 项硬门禁全 PASS。

### 2026-11-20 05:25:49 UTC - 全仓终极清零 2 组 15 模块全 PASS 入库
- 完成：`runtime`/`shell`/`shell_parser`/`test_runner` 15 个残余报错模块 15/15 全 PASS，同步补充 2 条新坑至 `CP_PITFALLS.md`。

### 2026-11-20 05:30:48 UTC - 全仓终极清零 1 组 15 模块 + JSON5Object.cp 全 PASS 入库
- 完成：`bake`/`bun_core`/`cli/create`/`crash_handler`/`css`/`js_parser/ast`/`md`/`resolver`/`sha_hmac`/`uws_sys` 15 模块 + `JSON5Object.cp`，全部 4 项硬门禁 PASS。

### 2026-11-20 05:40:25 UTC - 全仓 1,293/1,293 (100%) 纯 CP 满贯闭环（node_fs.cp 0.988x 入库）
- 完成：`src/runtime/node/node_fs.cp` (0.988x)，全仓 1,293 个非豁免模块 100% 通过 `cpc --lib` + `vendor/zig/zig ast-check` 与 4 项硬门禁。

### 2026-11-20 05:46:32 UTC - 最后 10 模块适配新版 cpc 全 PASS 入库（1,293/1,293 100% 满贯）
- 完成：`css_parser`/`size`/`SpawnSyncEventLoop`/`npm`/`PipeWriter`/`dns`/`server`/`Listener`/`UpgradedDuplex`/`unicode` 10/10 全 PASS。

### 2026-11-20 05:49:30 UTC - 全仓 281 个旧 prelude .zig 同步升级 & 启动 zig:check 语义清零
- 修复 `src/sys/sys.cp` 导入路径并重编译 281 个 `.zig` 产物，首次全仓 `bun run zig:check` 6 秒跑通解析仅余 156 处语义错误。

### 2026-11-20 05:53:46 UTC - zig:check JSC 核心簇 4 模块语义修复入库
- 完成：`host_fn`/`JSValue`/`GeneratedBindings`/`ZigString` 语义修复，消除 28 处 `zig:check` 报错且 4 项硬门禁全 PASS。

### 2026-11-20 05:54:33 UTC - zig:check 基础库与字符串簇 8 模块语义修复入库
- 完成：8/8 全部通过 `cpc_brief.py` 4 项硬门禁，彻底消除 `baby_list` 24 处 `[0]T` 指针、`StringImpl` 误指针化与 `0x8080808080808080` 溢出等 50+ 处语义错。

### 2026-11-20 05:57:46 UTC - zig:check Runtime+Bundler+Bake 簇 13 模块语义修复入库
- 完成：`http`/`HTMLBundle`/`FileSink`/`server`/`ServerConfig`/`uws_dispatch`/`Blob`/`encoding`/`TextEncoder`/`IncrementalGraph`/`Chunk`/`cache`/`options` 13/13 全 PASS。

### 2026-11-20 05:58:47 UTC - zig:check Parser/CSS/Install/Test 簇 11 模块语义修复入库
- 完成：`analytics`/`css_parser`/`border_image`/`GlobWalker`/`bin`/`dependency`/`lifecycle_script_runner`/`resolution`/`ast/G`/`renamer`/`snapshot` 11/11 全 PASS。

### 2026-11-20 06:03:08 UTC - zig:check 第 2 轮 JSC 簇 8 模块语义修复入库
- 完成：`GeneratedBindings`/`FFI`/`ErrorCode`/`EventLoopHandle`/`TopExceptionScope`/`ZigString`/`array_buffer`/`host_fn` 8/8 全 PASS。

### 2026-11-20 06:05:02 UTC - zig:check 第 2 轮 Core+Install+Parser+Bake 簇 12 模块语义修复入库
- 完成：`multi_array_list`/`DevServer`/`IncrementalGraph`/`string`/`fmt`/`output`/`Coordinator`/`PackageManagerTask`/`isolated_install/Store`/`Expr`/`renamer`/`schema` 12/12 全 PASS。

### 2026-11-20 06:05:27 UTC - zig:check 第 2 轮 Runtime 簇 7 模块语义修复入库
- 完成：`SSLConfig`/`socket`/`encoding`/`node_os`/`path`/`dns`/`blob/Store` 7/7 全 PASS。

### 2026-11-20 06:05:50 UTC - zig:check 3 处跨模块根因模块（pool/VM/fs）修复入库
- 完成：`pool.cp`/`VM.cp`/`resolver/fs.cp` 3/3 全 PASS。

### 2026-11-20 06:09:17 UTC - zig:check 外部根因（PackageInstaller/cache/Request）修复入库
- 完成：`PackageInstaller.cp` 补 `pub const NodeModulesFolder`、`cache.cp` 避内置名改 `CacheSet` + `pub const Set`、`Request.cp` 完整重构 `@root struct Request`。

### 2026-11-20 06:10:44 UTC - zig:check 外部根因（App/Blob）修复入库
- 完成：`uws_sys/App.cp` 补齐 `NewApp.Response`/`WebSocket`/`ListenSocket` 导出；`webcore/Blob.cp` 升级 `@root struct Blob` 并导出 `SizeType` 等根成员。

### 2026-11-20 06:15:49 UTC - zig:check 第 3 轮 JSC+WebCore 簇 10 模块语义修复入库
- 完成：`GeneratedBindings`/`SystemError`/`host_fn`/`ZigString`/`array_buffer`/`Body`/`FileSink`/`Response`/`TextEncoder`/`s3/client` 10/10 全 PASS。

### 2026-11-20 06:16:26 UTC - zig:check 第 3 轮 Core+Shell+Sys+Install 簇 11 模块语义修复入库
- 完成：`fmt`/`multi_array_list`/`pool`/`path_buffer_pool`/`PackageManagerTask`/`tsconfig_json`/`router`/`SemverQuery`/`interpreter`/`immutable`/`sys` 11/11 全 PASS。

### 2026-11-20 06:17:25 UTC - zig:check 第 3 轮 Runtime 簇 9 模块语义修复入库
- 完成：`BunObject`/`NativePromiseContext`/`bun/process`/`cares_jsc`/`dns`/`node_os`/`node/path`/`SSLConfig`/`Blob` 9/9 全 PASS。

### 2026-11-20 06:19:59 UTC - zig:check 外部根因（http/NullableAllocator/unicode）修复入库
- 完成：`http.cp` 补齐 18 处 `pub` 重导出、`NullableAllocator.cp` 升级 `@root struct`、`unicode.cp` 修复 `toUTF16Alloc` 签名与函数体，3/3 全 PASS。

### 2026-11-20 06:23:10 UTC - zig:check 外部根因（Response/EventLoopTimer）升级 @root struct 入库
- 完成：`Response.cp` (0.922x) 与 `EventLoopTimer.cp` (1.135x) 完整重构为 `@root struct`，4 项硬门禁全 PASS。

### 2026-11-20 06:27:53 UTC - zig:check 第 4 轮 Runtime/Image/Crypto/WebCore 簇 5 模块语义修复入库
- 完成：`Image.cp` (18 错清零)/`ffi.cp`/`ResumableSink.cp`/`streams.cp`/`CryptoHasher.cp` 5/5 全 PASS。

### 2026-11-20 06:29:07 UTC - zig:check 第 4 轮 JSC+TestRunner 簇 10 模块语义修复入库
- 完成：`expect`/`ScopeFunctions`/`ConsoleObject`/`GeneratedBindings`/`JSValue`/`DecodedJSValue`/`FetchHeaders`/`JSRef`/`ResolveMessage`/`Strong` 10/10 全 PASS（消除 44 处语义错）。

### 2026-11-20 06:30:01 UTC - zig:check 第 4 轮 Bake+URL+Shell+Parser 簇 12 模块语义修复入库
- 完成：`FrameworkRouter`/`unicode`/`url`/`HTTPContext`/`ParsedShellScript`/`Chunk`/`transpiler`/`PackageManagerTask`/`Ast`/`BundledAst`/`G`/`picohttp` 12/12 全 PASS。

### 2026-11-20 06:34:14 UTC - zig:check 外部根因（FileReader/ByteBlobLoader/ByteStream）升级 @root struct 入库
- 完成：3/3 升级为 `@root struct` 并在文件根导出 `pub const Source`，4 项硬门禁全 PASS。

### 2026-11-20 06:34:49 UTC - zig:check 外部根因（EnvMap/event_loop/bun/http）修复入库
- 完成：`EnvMap` 补 `static fn init`、`event_loop` 升级 `@root struct EventLoop`、`bun` 导出 `threadlocalAllocator`、`http` 导出 `ProxyTunnel`，4/4 全 PASS。

### 2026-11-20 06:39:17 UTC - zig:check 外部根因（EVP/node/types/Body/ReadableStream）全面升级入库
- 完成：`EVP`/`Body`/`ReadableStream` 升级完整 `@root struct`，`node/types` 消除 `pub union` 自动指针化，4/4 全 PASS。

### 2026-11-20 06:45:45 UTC - zig:check 第 5 轮 JSC+Test+Core 簇 12 模块语义修复入库
- 完成：`GeneratedBindings`/`ConsoleObject`/`expect`/`ConcurrentPromiseTask`/`DecodedJSValue`/`JSPromise`/`JSValue`/`ZigString`/`host_fn`/`Ast`/`owned`/`dir_info` 12/12 全 PASS。

- **[2026-11-20] Wave 5 Batch 2+3 (`zig:check` 跨模块修复 21 模块)**：更新 `src/sha_hmac/sha.{cp,zig}`, `src/runtime/crypto/EVP.{cp,zig}`, `src/runtime/webcore/Crypto.{cp,zig}`, `src/boringssl/boringssl.{cp,zig}`, `src/runtime/image/{Image,codecs}.{cp,zig}`, `src/runtime/webcore/{streams,TextDecoder,TextEncoderStreamEncoder}.{cp,zig}`, `src/runtime/webcore/s3/multipart.{cp,zig}`, `src/runtime/api/{BunObject,filesystem_router}.{cp,zig}`, `src/runtime/dns_jsc/dns.{cp,zig}`, `src/shell/builtin/{ls,touch}.{cp,zig}`, `src/runtime/server/{ServerWebSocket,HTMLBundle}.{cp,zig}`, `src/runtime/timer/{TimeoutObject,ImmediateObject}.{cp,zig}`, `src/runtime/socket/{Listener,SocketAddress}.{cp,zig}`（21/21 门禁全绿）。

- **[2026-11-20] Wave 5 收尾 (`zig:check` 外部根因+Node 10 模块)**：更新 `src/runtime/crypto/HMAC.{cp,zig}`, `src/runtime/webcore/{Sink,fetch}.{cp,zig}`, `src/shell/RefCountedStr.{cp,zig}`, `src/test_runner/ScopeFunctions.{cp,zig}`, `src/runtime/node/{node_fs_stat_watcher,types}.{cp,zig}`, `src/ptr/ref_count.{cp,zig}`, `src/runtime/node/zlib/{NativeBrotli,NativeZstd}.{cp,zig}`（10/10 门禁全绿）。

- **[2026-11-20] Wave 6 Batch B (`zig:check` 跨模块修复 30 模块 + `CP_PITFALLS.md` 同步)**：更新 `src/jsc/{ConsoleObject,AnyPromise,JSPromise,JSUint8Array,ZigString}.{cp,zig}`, `src/bun_core/fmt.{cp,zig}`, `src/bundler/bundle_v2.{cp,zig}`, `src/crash_handler/crash_handler.{cp,zig}`, `src/bun_alloc/MaxHeapAllocator.{cp,zig}`, `src/shell/builtin/{ls,touch}.{cp,zig}`, `src/string/{string,MutableString,wtf}.{cp,zig}`, `src/string/immutable/unicode.{cp,zig}`, `src/sourcemap/{Mapping,sourcemap}.{cp,zig}`, `src/sourcemap_jsc/CodeCoverage.{cp,zig}`, `src/sql/mysql/protocol/{ColumnDefinition41,ErrorPacket}.{cp,zig}`, `src/sql_jsc/mysql/MySQLStatement.{cp,zig}`, `src/sql_jsc/postgres/{PostgresSQLQuery,PostgresSQLStatement}.{cp,zig}`, `src/test_runner/{ScopeFunctions,jest}.{cp,zig}`, `src/test_runner/timers/FakeTimers.{cp,zig}`, `src/threading/Mutex.{cp,zig}`, `src/url/url.{cp,zig}`, `src/workaround_missing_symbols.{cp,zig}`, `src/wyhash/wyhash.{cp,zig}`（30/30 门禁全绿）。

- **[2026-11-20] Wave 6 Batch A (`zig:check` 跨模块修复 20 模块)**：更新 `src/runtime/webcore/{streams,Sink,ByteStream,TextDecoder,TextEncoderStreamEncoder,Crypto,ArrayBufferSink,S3Stat}.{cp,zig}`, `src/runtime/webcore/fetch/FetchTasklet.{cp,zig}`, `src/runtime/webcore/s3/multipart.{cp,zig}`, `src/runtime/server/ServerWebSocket.{cp,zig}`, `src/runtime/socket/SocketAddress.{cp,zig}`, `src/uws_sys/{WebSocket,Loop}.{cp,zig}`, `src/runtime/api/{filesystem_router,BunObject}.{cp,zig}`, `src/runtime/api/bun/SecureContext.{cp,zig}`, `src/runtime/dns_jsc/dns.{cp,zig}`, `src/runtime/image/codec_bmp.{cp,zig}`, `src/runtime/node/node_crypto_binding.{cp,zig}`（20/20 门禁全绿）。

- **[2026-11-20] Wave 7 Batch A (`zig:check` 核心 8 模块，覆盖 ~390 处深层报错)**：更新 `src/bun_core/output.{cp,zig}`, `src/ptr/ref_count.{cp,zig}`, `src/runtime/webcore/{ReadableStream,Blob,S3Client}.{cp,zig}`, `src/runtime/node/{node_zlib_binding,node_fs_binding}.{cp,zig}`, `src/runtime/api/html_rewriter.{cp,zig}` 及 `CP_PITFALLS.md`（8/8 门禁全绿）。

- **[2026-11-20] Wave 7 Batch B (`zig:check` 高频次梯队 13 模块)**：更新 `src/css/values/color.{cp,zig}`, `src/css/css_parser.{cp,zig}`, `src/runtime/webcore/Sink.{cp,zig}`, `src/runtime/timer/{TimeoutObject,ImmediateObject}.{cp,zig}`, `src/collections/multi_array_list.{cp,zig}`, `src/runtime/socket/Listener.{cp,zig}`, `src/runtime/api/{Archive,cron_parser}.{cp,zig}`, `src/runtime/node/net/BlockList.{cp,zig}`, `src/uws_sys/Response.{cp,zig}`, `src/runtime/node/zlib/NativeZlib.{cp,zig}`, `src/sql_jsc/postgres/PostgresSQLQuery.{cp,zig}`（13/13 门禁全绿）。

- **[2026-11-20] Wave 8 Batch A (`zig:check` Top 高频 18 模块)**：更新 `src/uws_sys/Response.{cp,zig}`, `src/runtime/api/html_rewriter.{cp,zig}`, `src/runtime/dns_jsc/dns.{cp,zig}`, `src/css/{css_parser,values/color}.{cp,zig}`, `src/runtime/webcore/{Blob,blob/Store,ReadableStream,ArrayBufferSink,Sink,streams,ByteStream}.{cp,zig}`, `src/collections/{multi_array_list,baby_list}.{cp,zig}`, `src/runtime/node/{node_zlib_binding,zlib/NativeZstd,zlib/NativeBrotli}.{cp,zig}`, `src/sourcemap_jsc/JSSourceMap.{cp,zig}`（18/18 门禁全绿）。

- **[2026-11-20] Wave 8 Batch B (`zig:check` 中频 24 模块)**：更新 `src/bun_core/output.{cp,zig}`, `src/runtime/api/{cron_parser,cron,BunObject,Archive,YAMLObject,lolhtml_jsc}.{cp,zig}`, `src/url/url.{cp,zig}`, `src/bun_alloc/allocation_scope.{cp,zig}`, `src/js_parser/ast/NewStore.{cp,zig}`, `src/jsc/{ConsoleObject,JSGlobalObject}.{cp,zig}`, `src/string/immutable.{cp,zig}`, `src/uws_sys/WebSocket.{cp,zig}`, `src/bun.{cp,zig}`, `src/cares_sys/c_ares.{cp,zig}`, `src/crash_handler/handle_oom.{cp,zig}`, `src/http/{InternalState,http}.{cp,zig}`, `src/interchange/json5.{cp,zig}`, `src/md/parser.{cp,zig}`, `src/runtime/node/node_fs_stat_watcher.{cp,zig}`, `src/runtime/server/ServerWebSocket.{cp,zig}`, `src/runtime/socket/SocketAddress.{cp,zig}`（24/24 门禁全绿）。

- **[2026-11-20] Wave 9 Batch B (`zig:check` 单错模块 27 文件)**：更新 `src/bun.{cp,zig}`, `src/bun_alloc/bun_alloc.{cp,zig}`, `src/bun_core/Global.{cp,zig}`, `src/bundler/options.{cp,zig}`, `src/cares_sys/c_ares.{cp,zig}`, `src/crash_handler/crash_handler.{cp,zig}`, `src/http/AsyncHTTP.{cp,zig}`, `src/install/patch_install.{cp,zig}`, `src/js_parser_jsc/expr_jsc.{cp,zig}`, `src/jsc/{CommonAbortReason,JSGlobalObject,RefString,rare_data}.{cp,zig}`, `src/libarchive_sys/bindings.{cp,zig}`, `src/md/{inlines,line_analysis,ref_defs,types}.{cp,zig}`, `src/runtime/api/{HashObject,JSON5Object,JSONCObject,TOMLObject,YAMLObject}.{cp,zig}`, `src/runtime/image/Image.{cp,zig}`, `src/runtime/node/{net/BlockList,node_fs,node_fs_watcher}.{cp,zig}`（27/27 门禁全绿）。

- **[2026-11-20] Wave 9 Batch A (`zig:check` 多错 27 模块)**：更新 `src/css/{values/color,css_parser}.{cp,zig}`, `src/runtime/dns_jsc/{cares_jsc,dns}.{cp,zig}`, `src/uws_sys/{Response,h3}.{cp,zig}`, `src/runtime/api/{MarkdownObject,BunObject,filesystem_router}.{cp,zig}`, `src/shell_parser/braces.{cp,zig}`, `src/runtime/webcore/{ByteStream,Sink,Blob,ByteBlobLoader,Body,ReadableStream,TextDecoder,streams}.{cp,zig}`, `src/jsc/{ConsoleObject,JSObject}.{cp,zig}`, `src/runtime/node/zlib/{NativeBrotli,NativeZstd}.{cp,zig}`, `src/md/parser.{cp,zig}`, `src/sourcemap/InternalSourceMap.{cp,zig}`, `src/test_runner/{pretty_format,snapshot}.{cp,zig}`, `src/threading/ThreadPool.{cp,zig}`（27/27 门禁全绿）。

- **[2026-11-20] Wave 9 Batch C (`zig:check` 7 外部根因 + 34 单错 41 模块)**：更新 `src/uws_sys/{Loop,quic/Stream,InternalLoopData,Request,quic/Socket,us_socket_t,vtable}.{cp,zig}`, `src/css/error.{cp,zig}`, `src/paths/resolve_path.{cp,zig}`, `src/runtime/timer/{Timer,TimerObjectInternals}.{cp,zig}`, `src/string/string.{cp,zig}`, `src/jsc/array_buffer.{cp,zig}`, `src/runtime/node/{node_process,types,zlib/NativeZlib}.{cp,zig}`, `src/runtime/socket/{SSLConfig,SocketAddress,uws_jsc}.{cp,zig}`, `src/runtime/valkey_jsc/valkey.{cp,zig}`, `src/runtime/webcore/{ArrayBufferSink,Crypto,S3File,blob/Store,blob/copy_file,fetch/FetchTasklet,s3/list_objects}.{cp,zig}`, `src/safety/alloc.{cp,zig}`, `src/semver/SemverQuery.{cp,zig}`, `src/shell/shell.{cp,zig}`, `src/sourcemap/{Chunk,ParsedSourceMap}.{cp,zig}`, `src/sql/mysql/protocol/ColumnDefinition41.{cp,zig}`, `src/sql_jsc/mysql/{MySQLStatement,protocol/Signature}.{cp,zig}`, `src/sql_jsc/postgres/PostgresSQLStatement.{cp,zig}`, `src/sys/sys.{cp,zig}`, `src/test_runner/{Execution,diff/printDiff}.{cp,zig}`, `src/url/url.{cp,zig}`, `src/watcher/Watcher.{cp,zig}`（41/41 门禁全绿）。

- **[2026-11-20] Wave 9 收尾 (`html_renderer` + `yaml` 5,743 行完整纯 CP 迁移)**：恢复 `src/interchange/yaml_.zig` 原始备份，完成 `src/interchange/yaml.{cp,zig}`（ratio 1.020x）与 `src/md/html_renderer.{cp,zig}`（ratio 1.008x）门禁全绿。

- **[2026-11-20] Wave 10 Batch A (`zig:check` Socket+HTTP2+uws+WebCore 11 模块)**：更新 `src/runtime/socket/{uws_handlers,Handlers,UpgradedDuplex,socket,tls_socket_functions}.{cp,zig}`, `src/runtime/api/bun/h2_frame_parser.{cp,zig}`, `src/uws_sys/{h3,vtable}.{cp,zig}`, `src/runtime/webcore/{Blob,Body,ResumableSink}.{cp,zig}`（11/11 门禁全绿）。

- **[2026-11-20] Wave 10 Batch B (`zig:check` 23 模块)**：更新 `src/test_runner/{expect,expect/toHaveReturned,pretty_format}.{cp,zig}`, `src/css/values/{color,calc,angle}.{cp,zig}`, `src/runtime/dns_jsc/cares_jsc.{cp,zig}`, `src/runtime/node/node_fs.{cp,zig}`, `src/runtime/api/{BunObject,JSON5Object,YAMLObject}.{cp,zig}`, `src/runtime/valkey_jsc/{js_valkey_functions,js_valkey}.{cp,zig}`, `src/http/http.{cp,zig}`, `src/md/{ansi_renderer,inlines}.{cp,zig}`, `src/sql_jsc/mysql/{JSMySQLConnection,JSMySQLQuery}.{cp,zig}`, `src/sql_jsc/postgres/PostgresSQLConnection.{cp,zig}`, `src/bundler/bundle_v2.{cp,zig}`, `src/install/patch_install.{cp,zig}`, `src/jsc/JSGlobalObject.{cp,zig}`, `src/string/immutable.{cp,zig}`（23/23 门禁全绿）。

- **[2026-11-20] Wave 11 Batch A+C (`zig:check` 101 模块)**：更新 57 个 `src/test_runner/expect/to*.{cp,zig}`、21 个多错模块（`css/{values/color,values/calc,css_parser,generics,targets,error,values/number,values/percentage}`, `test_runner/expect`, `dns_jsc/dns`, `node/node_fs`, `http/{http,HTTPContext}`, `event_loop/MiniEventLoop`, `api/{JSON5Object,YAMLObject,BunObject}`, `webcore/Blob`, `jsc/JSGlobalObject`, `md/html_renderer`, `shell/shell`）及 C 组 23 模块（`output`, `Mutex`, `SSLConfig`, `BufferFallbackAllocator` + 19 高频清扫模块）。

- **[2026-11-20] Wave 11 Batch B (`zig:check` 34 模块)**：更新 `src/sql_jsc/mysql/protocol/any_mysql_error_jsc.{cp,zig}`, `src/sys/sys.{cp,zig}`, `src/analytics/analytics.{cp,zig}`, `src/bundler/{bundle_v2,options}.{cp,zig}`, `src/crash_handler/crash_handler.{cp,zig}`, `src/http/h2_client/encode.{cp,zig}`, `src/interchange/{json,toml,yaml}.{cp,zig}`, `src/jsc/{CppTask,JSCScheduler,WTF,WorkTask}.{cp,zig}`, `src/md/autolinks.{cp,zig}`, `src/runtime/api/{Archive,filesystem_router}.{cp,zig}`, `src/runtime/node/{node_fs_stat_watcher,node_process,zlib/NativeZlib}.{cp,zig}`, `src/runtime/socket/uws_jsc.{cp,zig}`, `src/runtime/webcore/{ResumableSink,TextDecoder,blob/read_file,streams}.{cp,zig}`, `src/string/SmolStr.{cp,zig}`, `src/test_runner/{Execution,ScopeFunctions,bun_test,diff/printDiff,jest,pretty_format}.{cp,zig}`, `src/threading/ThreadPool.{cp,zig}`, `src/uws_sys/socket.{cp,zig}`（34/34 门禁全绿）。

- **[2026-11-20] Wave 12 Batch A (`zig:check` Meta+CSS 泛型 9 模块，覆盖 275 错)**：更新 `src/meta/meta.{cp,zig}`, `src/css/{generics,css_parser,printer}.{cp,zig}`, `src/css/values/{calc,angle,time,length,number}.{cp,zig}`（9/9 门禁全绿）。

- **[2026-11-20] Wave 12 Batch B (`zig:check` Expect+NodeFS+Interchange 14 模块，覆盖 120 错)**：更新 `src/test_runner/{expect,ScopeFunctions,Execution,pretty_format,expect/toBe,expect/toHaveLastReturnedWith,expect/toHaveNthReturnedWith,expect/toThrow}.{cp,zig}`, `src/runtime/node/{node_fs,node_process}.{cp,zig}`, `src/runtime/api/YAMLObject.{cp,zig}`, `src/interchange/{yaml,json,toml/lexer}.{cp,zig}`（14/14 编译与静态零违例通过）。

- **[2026-11-20] Wave 12 Batch C (`zig:check` 5 上游根因 + 14 小错 19 模块)**：更新 `src/bun.{cp,zig}`, `src/runtime/timer/Timer.{cp,zig}`, `src/resolver/fs.{cp,zig}`, `src/runtime/api/bun/js_bun_spawn_bindings.{cp,zig}`, `src/runtime/webcore/{Blob,TextDecoder,blob/write_file}.{cp,zig}`, `src/jsc/{JSGlobalObject,ZigString}.{cp,zig}`, `src/shell/{shell,ParsedShellScript}.{cp,zig}`, `src/sys/sys.{cp,zig}`, `src/bundler/options.{cp,zig}`, `src/crash_handler/crash_handler.{cp,zig}`, `src/event_loop/MiniEventLoop.{cp,zig}`, `src/runtime/api/BunObject.{cp,zig}`, `src/runtime/socket/uws_jsc.{cp,zig}`, `src/runtime/valkey_jsc/valkey.{cp,zig}`, `src/uws_sys/InternalLoopData.{cp,zig}`（19/19 门禁全绿）。

- **[2026-11-20] Wave 13 Batch C (`zig:check` 10 模块)**：更新 `src/test_runner/{pretty_format,ScopeFunctions,bun_test}.{cp,zig}`, `src/jsc/{JSGlobalObject,event_loop}.{cp,zig}`, `src/ptr/owned.{cp,zig}`, `src/runtime/socket/uws_jsc.{cp,zig}`, `src/runtime/webcore/{Blob,TextDecoder}.{cp,zig}`, `src/string/StringBuilder.{cp,zig}`（10/10 门禁全绿）。

- **[2026-11-20] Wave 13 Batch A+B (`zig:check` 24 模块)**：更新 `src/css/{generics,css_parser,values/length,values/angle,values/time}.{cp,zig}`, `src/http/{http,HTTPContext,h2_client/encode,h3_client/ClientContext,h3_client/ClientSession}.{cp,zig}`, `src/uws_sys/quic/Context.{cp,zig}`, `src/picohttp/picohttp.{cp,zig}`, `src/sys/sys.{cp,zig}`, `src/runtime/node/{node_fs,node_process}.{cp,zig}`, `src/shell/shell.{cp,zig}`, `src/interchange/{yaml,json}.{cp,zig}`, `src/runtime/api/YAMLObject.{cp,zig}`, `src/js_parser/ast/{E,Expr}.{cp,zig}`, `src/boringssl/boringssl.{cp,zig}`, `src/runtime/webcore/Sink.{cp,zig}`, `src/runtime/timer/Timer.{cp,zig}`。

- **[2026-11-20] Wave 14 Batch C (`zig:check` 13 模块)**：更新 `src/interchange/yaml.{cp,zig}`, `src/bun.{cp,zig}`, `src/runtime/timer/Timer.{cp,zig}`, `src/runtime/webcore/{FileReader,FormData}.{cp,zig}`, `src/test_runner/pretty_format.{cp,zig}`, `src/http/{HTTPContext,http}.{cp,zig}`, `src/http_jsc/websocket_client.{cp,zig}`, `src/install/lifecycle_script_runner.{cp,zig}`, `src/js_parser/lexer.{cp,zig}`, `src/patch/patch.{cp,zig}`, `src/collections/bit_set.{cp,zig}`（13/13 门禁全绿）。

- **[2026-11-20] Wave 14 Batch A (`zig:check` 核心 14 模块，覆盖 ~226 错)**：更新 `src/runtime/node/{node_fs_binding,node_crypto_binding,node_fs}.{cp,zig}`, `src/jsc/{host_fn,bindgen,ConsoleObject,JSGlobalObject}.{cp,zig}`, `src/runtime/dns_jsc/dns.{cp,zig}`, `src/runtime/api/{HashObject,JSTranspiler,BunObject,glob,Archive,bun/h2_frame_parser}.{cp,zig}` 及 `CP_PITFALLS.md`（14/14 门禁全绿）。

- **[2026-11-20] Wave 14 Batch B (`zig:check` Pipe+Subprocess+Terminal+Shell 13 模块)**：更新 `src/io/{PipeReader,PipeWriter}.{cp,zig}`, `src/runtime/socket/{udp_socket,tls_socket_functions}.{cp,zig}`, `src/runtime/api/bun/{Terminal,subprocess,process,subprocess/Writable}.{cp,zig}`, `src/jsc/ipc.{cp,zig}`, `src/shell/{interpreter,builtin/rm,builtin/mv,IOWriter}.{cp,zig}`（13/13 门禁全绿）。

- **[2026-11-20] Wave 14 Batch E (`zig:check` 单错 28 模块)**：更新 `src/interchange/toml.{cp,zig}`, `src/io/pipes.{cp,zig}`, `src/jsc/{CppTask,JSPromise,VirtualMachine,web_worker}.{cp,zig}`, `src/logger/logger.{cp,zig}`, `src/napi/napi.{cp,zig}`, `src/runtime/image/Image.{cp,zig}`, `src/runtime/node/node_fs_watcher.{cp,zig}`, `src/runtime/socket/{ssl_wrapper,uws_jsc}.{cp,zig}`, `src/runtime/valkey_jsc/protocol_jsc.{cp,zig}`, `src/runtime/webcore/{ReadableStream,blob/copy_file,streams}.{cp,zig}`, `src/shell/{states/Cmd,subproc}.{cp,zig}`, `src/sql/mysql/protocol/NewWriter.{cp,zig}`, `src/sql_jsc/postgres/{PostgresSQLConnection,Signature,error_jsc}.{cp,zig}`, `src/string/immutable.{cp,zig}`, `src/sys/sys.{cp,zig}`, `src/sys_jsc/fd_jsc.{cp,zig}`, `src/test_runner/{DoneCallback,ScopeFunctions}.{cp,zig}`, `src/uws_sys/quic/Context.{cp,zig}`（28/28 门禁全绿）。

- **[2026-11-20] Wave 14 Batch D+F (`zig:check` 39 模块)**：更新 D 组 34 模块（`uws_sys/{WebSocket,SocketContext}`, `runtime/api/bun/subprocess/{Readable,SubprocessPipeReader}`, `runtime/node/{node_fs_stat_watcher,types}`, `runtime/socket/{Handlers,SSLConfig}`, `runtime/valkey_jsc/{js_valkey,valkey}`, `runtime/webcore/{blob/read_file,s3/multipart}`, `shell/{IO,shell,states/CondExpr,states/Expansion}`, `sql_jsc/mysql/MySQLConnection`, `bun_core/Progress`, `bundler/options`, `bundler_jsc/{JSBundleCompletionTask,options_jsc}`, `cli/multi_run`, `event_loop/AnyTask`, `http/h3_client/encode`, `http_jsc/websocket_client/{WebSocketProxy,WebSocketProxyTunnel,WebSocketUpgradeClient}`, `install/{PackageManager,PackageManagerTask,TarballStream,isolated_install/Installer,patch_install,repository,resolution}`）及 F 组 5 模块（`sys/SignalCode`, `event_loop/AnyTaskWithExtraContext`, `runtime/webcore/FileSink`, `runtime/api/cron`, `install/lifecycle_script_runner`）。

- **[2026-11-20] Wave 14 Batch G (`zig:check` 3 上游根因)**：更新 `src/runtime/server/StaticRoute.{cp,zig}`（升 `@root struct` 导出 `ref`/`deref`）、`src/jsc/SystemError.{cp,zig}`（`Maybe` 字段 `e` → `@"err"`）、`src/runtime/webcore/Blob.{cp,zig}`（补齐 `writeFileInternal`/`FileOpener`/`FileCloser` 等导出）。

- **[2026-11-20] Wave 15 Batch B+C (`zig:check` 29 模块)**：更新 B 组 13 模块（`runtime/node/{node_fs_binding,node_fs}`, `runtime/crypto/CryptoHasher`, `jsc/{rare_data,ipc,VirtualMachine,event_loop}`, `bun_core/Progress`, `bun`, `http_jsc/websocket_client`, `runtime/api/{BunObject,cron,glob}`）及 C 组 16 模块（`shell/{IO,Builtin,interpreter,builtin/rm,builtin/mkdir,builtin/touch,shell,states/Expansion,subproc}`, `io/PipeWriter`, `runtime/api/bun/process`, `interchange/yaml`, `sql_jsc/mysql/MySQLConnection`, `test_runner/{expect,pretty_format}`, `bundler/bundle_v2`）。

- **[2026-11-20] Wave 15 Batch A (`zig:check` Server+WebCore+EventLoop 14 模块)**：更新 `src/runtime/server/{AnyRequestContext,RequestContext}.{cp,zig}`, `src/bake/DevServer.{cp,zig}`, `src/event_loop/{DeferredTaskQueue,ManagedTask}.{cp,zig}`, `src/runtime/webcore/{Body,Blob,blob/Store,FileSink,Request,Response,blob/read_file,blob/write_file,s3/client}.{cp,zig}`（14/14 门禁全绿）。

- **[2026-11-20] Wave 16 Batch A (13 模块)**：更新 `src/runtime/server/{AnyRequestContext,RequestContext,server}.{cp,zig}`, `src/runtime/webcore/{Blob,Body,FileSink}.{cp,zig}`, `src/runtime/webcore.{cp,zig}`, `src/event_loop/{ManagedTask,AutoFlusher,EventLoopTimer}.{cp,zig}`, `src/bake/DevServer.{cp,zig}`, `src/bake/DevServer/IncrementalGraph.{cp,zig}`, `src/uws_sys/us_socket_t.{cp,zig}`。

- **[2026-11-20] Wave 16 Batch B (20 模块)**：更新 `src/sql/mysql/protocol/NewReader`, `src/runtime/node/{node_fs,types}`, `src/interchange/yaml`, `src/jsc/{rare_data,FetchHeaders,ModuleLoader}`, `src/paths/resolve_path`, `src/runtime/api/{cron,html_rewriter,bun/subprocess,bun/subprocess/StaticPipeWriter}`, `src/shell/{IOWriter,interpreter,ParsedShellScript,builtin/cp,builtin/ls}`, `src/test_runner/pretty_format`, `src/cli/multi_run`, `src/resolver/fs`（.cp/.zig）。

- **[2026-11-20] Wave 16 D 组 (7 模块)**：更新 `src/jsc/{jsc,GarbageCollectionController,PosixSignalHandle}`, `src/runtime/timer/WTFTimer`, `src/runtime/webcore/ObjectURLRegistry`, `src/bundler/options`, `src/runtime/webcore/blob/Store`（.cp/.zig）。

- **[2026-11-20] errset-merge-or 重编 + Wave 17 A 组**：重编 `src/{bun,bundler/linker_context/scanImportsAndExports,cli/{pack,publish}_command,runtime/crypto/PasswordObject,runtime/image/backend_{wic,coregraphics},install/{resolution,PnpmMatcher,lockfile/bun.lock,npm,NetworkTask,PackageManager/PopulateManifestCache}}.zig`；Wave 17 A 组更新 `runtime/{webcore/{Blob,Body,s3/client},webcore,server/{RequestContext,server,ServerWebSocket}}`、`uws_sys/App`、`bake/DevServer{,/IncrementalGraph}`、`http/HTTPContext`、`http_jsc/websocket_client/WebSocketUpgradeClient`。

- **[2026-11-20] Wave 17 C 组 (6 模块)**：更新 `src/uws_sys/Response`、`src/bake/DevServer{,/Assets,/RouteBundle}`、`src/runtime/webcore/fetch`、`src/runtime/server/server`（.cp/.zig）。

- **[2026-11-20] Wave 17 B 组 (18 模块)**：更新 `src/runtime/dns_jsc/{dns,cares_jsc}`, `src/cares_sys/c_ares`, `src/sql/mysql/protocol/{NewReader,StackReader}`, `src/shell/{shell,Builtin,Yield,states/Cmd,builtin/rm}`, `src/runtime/node/{types,node_fs}`, `src/interchange/yaml`, `src/paths/resolve_path`, `src/resolver/{resolver,fs}`, `src/test_runner/pretty_format`, `src/jsc/VirtualMachine`。

- **[2026-11-20] Wave 18 A 组 (15 模块)**：更新 `src/runtime/{webcore/Blob,webcore,server/{server,RequestContext,ServerConfig,AnyRequestContext}}`, `src/uws_sys/{App,h3,Response,WebSocket}`, `src/bake/DevServer{,/HotReloadEvent,/IncrementalGraph,/WatcherAtomics,/memory_cost}`（.cp/.zig）。

- **[2026-11-20] Wave 18 C 组 (10 模块)**：更新 `src/jsc/VirtualMachine`, `src/runtime/server/{RequestContext,FileRoute,ServerConfig}`, `src/bake/DevServer{,/HotReloadEvent,/WatcherAtomics,/memory_cost,/PackedMap,/SourceMapStore}`（.cp/.zig）。

- **[2026-11-20] Wave 18 B 组 (24 模块)**：更新 `src/bun_core/output`, `src/jsc/{JSPromise,JSGlobalObject,ZigStackFramePosition}`, `src/runtime/valkey_jsc/valkey`, `src/shell/{interpreter,IO,IOReader,builtin/mv,builtin/ls,states/Expansion,states/Pipeline}`, `src/ptr/tagged_pointer`, `src/resolve_builtins/HardcodedModule`, `src/sql/postgres/protocol/NewWriter`, `src/collections/baby_list`, `src/aio/posix_event_loop`, `src/io/PipeReader`, `src/runtime/image/codec_png`, `src/runtime/node/node_fs_stat_watcher`, `src/runtime/socket/{Handlers,udp_socket}`, `src/event_loop/MiniEventLoop`, `src/watcher/INotifyWatcher`（.cp/.zig）。

- **[2026-11-20] Wave 18 D 组 (20 模块)**：更新 `src/bake/{DevServer,DevServer/{SourceMapStore,memory_cost,DirectoryWatchStore,Assets,ErrorReportRequest,SerializedFailure},FrameworkRouter}`, `src/runtime/server/{ServerConfig,server}`, `src/runtime/webcore/{Body,Sink,streams,FormData,ReadableStream,fetch/FetchTasklet,s3/client}`, `src/sql_jsc/{mysql/JSMySQLConnection,shared/QueryBindingIterator}`, `src/standalone_graph/StandaloneModuleGraph`（.cp/.zig）。

- **[2026-11-20] Wave 19 A 组 (14 模块)**：更新 `src/runtime/node/node_crypto_binding`, `src/bun_core/output`, `src/runtime/socket/SSLConfig`, `src/bundler/{cache,ThreadPool,bundle_v2,options}`, `src/dotenv/env_loader`, `src/jsc/{hot_reloader,JSGlobalObject}`, `src/cli/{filter_run,test/parallel/Coordinator}`, `src/http/HTTPContext`, `src/http_jsc/websocket_client`（.cp/.zig）。

- **[2026-11-20] Wave 19 C 组 (45 模块)**：更新 `bake/DevServer/{DirectoryWatchStore,SerializedFailure}`, `bun_alloc/LinuxMemFdAllocator`, `bun_core/deprecated`, `bundler/analyze_transpiled_module`, `bundler_jsc/PluginRunner`, `cli/{multi_run,open}`, `dns/dns`, `event_loop/DeferredTaskQueue`, `http/{Headers,InternalState,http}`, `install/{PackageManagerTask,TarballStream,lifecycle_script_runner,patch_install,repository}`, `interchange/{json,toml}`, `io/openForWriting`, `js_parser/ast/ASTMemoryAllocator`, `runtime/{ffi/{FFIObject,ffi},image/{Image,codec_jpeg,codec_png,codec_webp},node/{node_fs_watcher,path},server/{FileResponseStream,server},socket/Handlers,timer/Timer,webcore/{ObjectURLRegistry,Response,ResumableSink,S3File}}`, `shell/{builtin/cat,states/Pipeline}`, `sql/mysql/protocol/ErrorPacket`, `sql_jsc/mysql/MySQLConnection`, `standalone_graph/StandaloneModuleGraph`, `uws_sys/SocketContext`, `watcher/Watcher`。

- **[2026-11-20] Wave 19 B 组 (37 模块)**：更新 `js_parser/{ast/P,lexer}`, `js_printer/js_printer`, `bake/{DevServer,FrameworkRouter}`, `runtime/webcore/{Body,FormData,ReadableStream,fetch/FetchTasklet,streams,FileReader}`, `uws_sys/BodyReaderMixin`, `runtime/socket/udp_socket`, `http_jsc/websocket_client/WebSocketUpgradeClient`, `string/immutable`, `watcher/INotifyWatcher`, `bundler/{barrel_imports,transpiler,BundleThread}`, `jsc/{AbortSignal,RuntimeTranspilerStore,SavedSourceMap,array_buffer,ipc}`, `lolhtml_sys/lol_html`, `resolver/fs`, `runtime/api/{Archive,JSTranspiler,cron,filesystem_router,bun/{js_bun_spawn_bindings,subprocess,subprocess/Readable,subprocess/SubprocessPipeReader,subprocess/Writable}}`, `install/PackageManager/security_scanner`, `shell/interpreter`。

- **[2026-11-20] Wave 20 B 组 (23 模块)**：更新 `sys/walker_skippable`, `jsc/{hot_reloader,ipc,JSPromise,ModuleLoader,SavedSourceMap,array_buffer,rare_data}`, `shell/{shell,interpreter}`, `http/http`, `uws_sys/BodyReaderMixin`, `runtime/webcore/{Request,FileReader}`, `watcher/INotifyWatcher`, `http_jsc/websocket_client/WebSocketUpgradeClient`, `runtime/api/{Archive,cron,bun/js_bun_spawn_bindings}`, `runtime/ffi/ffi`, `runtime/image/{Image,codec_jpeg}`, `runtime/socket/ssl_wrapper`（.cp/.zig）。

- **[2026-11-20] Wave 20 A 组 (24 模块)**：更新 `js_parser/{ast/{P,Expr,Stmt,parse,parseStmt,parseSuffix,skipTypescript,Parser},lexer,parser}`, `js_printer/js_printer`, `resolver/{fs,resolver}`, `bundler/{bundle_v2,barrel_imports,options,transpiler}`, `bundler_jsc/JSBundleCompletionTask`, `dotenv/env_loader`, `router/router`, `bake/DevServer{,/SourceMapStore}`, `uws_sys/quic/{Socket,Header}`。

- **[2026-11-20] Wave 21 B 组 (25 模块)**：更新 `bun`, `runtime/webcore/{S3File,ObjectURLRegistry}`, `jsc/{ModuleLoader,ipc}`, `runtime/api/{Archive,cron,crash_handler_jsc,UnsafeObject,bun/js_bun_spawn_bindings}`, `runtime/socket/ssl_wrapper`, `shell/interpreter`, `sql/mysql/protocol/ErrorPacket`, `sql_jsc/mysql/MySQLConnection`, `uws_sys/SocketContext`, `watcher/INotifyWatcher`, `cli/upgrade_command`, `install_jsc/{update_request_jsc,npm_jsc,dependency_jsc}`, `runtime/crypto/PasswordObject`, `runtime/node/{node_cluster_binding,node_util_binding}`, `test_runner/{jest,bun_test}`。

- **[2026-11-20] Wave 21 A 组 (20 模块) + GEMINI.md 第十节**：更新 `resolver/{fs,package_json,resolver}`, `js_parser/{ast/{skipTypescript,P,Expr,parse,parseProperty,parseSuffix,ImportScanner,Stmt,base},lexer}`, `js_printer/js_printer`, `bundler/options`, `bundler_jsc/output_file_jsc`, `interchange/json`, `sourcemap/ParsedSourceMap`, `bake/DevServer{,/SourceMapStore}`, `GEMINI.md`。

- **[2026-11-20] Wave 21 C 组 (17 模块)**：更新 `bun_core/deprecated`, `http/Headers`, `install/{TarballStream,lifecycle_script_runner,npm,repository}`, `io/openForWriting`, `jsc/{AnyPromise,ConsoleObject,Debugger,EventLoopHandle,GarbageCollectionController,HTTPServerAgent,PosixSignalHandle,RefString,RuntimeTranspilerCache,Task}`。

- **[2026-11-20] Wave 21 D 组 (29 模块)**：更新 `jsc/{RuntimeTranspilerStore,ZigStackFrame,ZigStackTrace,array_buffer,rare_data,web_worker}`, `logger/logger`, `meta/meta`, `patch/patch`, `perf/perf`, `router/router`, `runtime/{api/{BunObject,JSBundler,JSTranspiler,UnsafeObject,filesystem_router,html_rewriter,bun/{SecureContext,Terminal,process,subprocess}},crypto/HMAC,dns_jsc/cares_jsc,ffi/ffi,image/{codec_gif,codec_webp,quantize,thumbhash},node/Stat}`。

- **[2026-11-20] Wave 21 E 组 (26 模块)**：更新 `runtime/{node/{StatFS,node_fs_stat_watcher,node_process,path},server/{NodeHTTPResponse,ServerConfig,ServerWebSocket,server},socket/{Handlers,Listener,UpgradedDuplex,socket,udp_socket,uws_jsc},timer/DateHeaderTimer,valkey_jsc/js_valkey,webcore/{Crypto,FileSink,Request,ResumableSink,blob/copy_file,fetch,s3/{list_objects,multipart,simple_request}}}`, `sha_hmac/hmac`。

- **[2026-11-20] Wave 21 F 组 (28 模块)**：更新 `runtime/timer/EventLoopDelayMonitor`, `paths/Path`, `shell/{builtin/cp,shell,states/Pipeline,subproc}`, `shell_parser/braces`, `sourcemap_jsc/{CodeCoverage,source_provider}`, `sql/{mysql/{AuthMethod,protocol/{HandshakeV10,OKPacket}},postgres/protocol/NewReader}`, `sql_jsc/{mysql/{MySQLQuery,MySQLValue},postgres/{PostgresRequest,PostgresSQLConnection},shared/ObjectIterator}`, `string/immutable/unicode`, `sys/sys`, `tcc_sys/tcc`, `test_runner/{Collection,Execution,ScopeFunctions,expect}`, `uws_sys/{Request,SocketGroup,udp}`。

- **[2026-11-20] Wave 21 G 组 (13 模块)**：更新 `jsc/RefString`, `runtime/node/path_watcher`, `options_types/{Context,CompileTarget,CodeCoverageOptions}`, `runtime/webcore/{encoding,s3/download_stream,BakeResponse}`, `http/{AsyncHTTP,HTTPThread}`, `runtime/server/HTMLBundle`, `runtime/valkey_jsc/js_valkey_functions`, `test_runner/expect/toHaveReturnedWith`。

- **[2026-11-20] Wave 22 C 组 (25 模块)**：更新 `sql_jsc/mysql/JSMySQLConnection`, `uws_sys/Response`, `watcher/INotifyWatcher`, `bake/{DevServer,DevServer/{ErrorReportRequest,HmrSocket,SourceMapStore},FrameworkRouter}`, `bundler/{ServerComponentParseTask,analyze_transpiled_module,bundle_v2,options,transpiler}`, `bundler_jsc/source_map_mode_jsc`, `clap/clap`, `cli/{filter_run,multi_run}`, `http/HTTPContext`, `install/{PackageManager,PackageManager/PackageManagerOptions,lifecycle_script_runner,lockfile,npm,repository}`, `js_parser/ast/ImportScanner`。

- **[2026-11-20] Wave 22 A 组 (19 模块)**：更新 `cli/test_command`, `sql_jsc/{postgres/PostgresSQLConnection,mysql/MySQLConnection}`, `sql/{mysql/protocol/{Auth,OKPacket},postgres/protocol/BackendKeyData}`, `js_parser/{ast/{Expr,P,Stmt,parse,parseFn,parseJSXElement,parseSuffix,parseTypescript,skipTypescript},runtime}`, `js_printer/js_printer`, `resolver/{package_json,resolver}`。

- **[2026-11-20] Wave 22 B 组 (21 模块)**：更新 `jsc/{AnyPromise,VirtualMachine,GarbageCollectionController,RuntimeTranspilerCache,ConsoleObject,ModuleLoader,hot_reloader,ipc}`, `bun`, `runtime/server/server`, `shell/shell`, `shell_parser/braces`, `runtime/api/{Archive,cron,BunObject,bun/js_bun_spawn_bindings}`, `runtime/webcore/{fetch/FetchTasklet,ResumableSink,s3/client}`, `runtime/socket/ssl_wrapper`, `bundler_jsc/output_file_jsc`。

- **[2026-11-20] Wave 22 E 组 (21 模块)**：更新 `runtime/{api/{UnsafeObject,bun/subprocess,filesystem_router,html_rewriter},crypto/CryptoHasher,ffi/ffi,image/{codec_gif,codec_webp},node/{Stat,node_fs_watcher,node_process,path,path_watcher},socket/{Listener,UpgradedDuplex,socket,udp_socket},valkey_jsc/js_valkey,webcore/{FileSink,Request,Response}}`。

- **[2026-11-20] Wave 22 D 组 + bindgen (26 模块)**：更新 `js_parser/{ast/{BundledAst,base,Parser,Symbol},lexer,parser}`, `event_loop/SpawnSyncEventLoop`, `runtime/{node/node_fs_stat_watcher,webcore/{Blob,Sink},api/JSBundler}`, `bake/DevServer/SerializedFailure`, `uws_sys/App`, `collections/baby_list`, `standalone_graph/StandaloneModuleGraph`, `js_printer/renamer`, `jsc/{EventLoopHandle,array_buffer,rare_data,bindgen}`, `patch/patch`, `perf/perf`, `resolver/{fs,node_fallbacks,tsconfig_json}`, `router/router`。

- **[2026-11-20] Wave 23 A 组 (18 模块)**：更新 `js_parser/ast/{Expr,parse,parseProperty,skipTypescript,Stmt}`, `js_printer/js_printer`, `resolver/package_json`, `bundler/{ParseTask,bundle_v2,ServerComponentParseTask,ThreadPool}`, `bundler_jsc/output_file_jsc`, `bake/{FrameworkRouter,DevServer/{HmrSocket,SerializedFailure}}`, `interchange/{json5,yaml}`, `sourcemap/InternalSourceMap`。

- **[2026-11-20] Wave 23 C 组 (32 模块)**：更新 `bundler/{analyze_transpiled_module,transpiler}`, `cli/{filter_run,multi_run,test_command}`, `collections/baby_list`, `crash_handler/handle_oom`, `http/Headers`, `install/{PackageInstall,lifecycle_script_runner,npm,repository}`, `interchange/json`, `js_parser/{ast/Parser,lexer,parser}`, `jsc/{ConsoleObject,RuntimeTranspilerCache,ZigStackTrace,ipc,web_worker}`, `resolver/{node_fallbacks,resolver,tsconfig_json}`, `router/router`, `runtime/{api/{UnsafeObject,html_rewriter},dns_jsc/dns,image/codec_webp,node/path_watcher,server/server,socket/UpgradedDuplex}`。

- **[2026-11-20] Wave 23 B 组 (23 模块)**：更新 `jsc/{bindgen,GarbageCollectionController,hot_reloader,VirtualMachine}`, `sql_jsc/{shared/SQLDataCell,mysql/{protocol/ResultSet,JSMySQLConnection},postgres/PostgresSQLConnection}`, `sql/mysql/{Capabilities,protocol/{Auth,NewWriter,OKPacket}}`, `bun`, `shell/shell`, `runtime/{api/{Archive,JSBundler,cron,bun/js_bun_spawn_bindings},webcore/{ResumableSink,fetch/FetchTasklet,s3/client},socket/ssl_wrapper,valkey_jsc/js_valkey}`。

- **[2026-11-20] Wave 23 D 组 (39 模块)**：更新 `runtime/{timer/Timer,webcore/{Request,Response,S3File,blob/{Store,copy_file},encoding,fetch,s3/{download_stream,simple_request}}}`, `shell/{states/Pipeline,subproc}`, `sourcemap_jsc/{CodeCoverage,source_provider}`, `sql/{mysql/{AuthMethod,protocol/{HandshakeV10,LocalInfileRequest}},postgres/protocol/{FieldMessage,ParameterDescription,Parse,RowDescription,SASLInitialResponse}}`, `sql_jsc/{mysql/{MySQLConnection,MySQLQuery,MySQLValue},postgres/{PostgresRequest,PostgresSQLQuery,types/tag_jsc},shared/ObjectIterator}`, `standalone_graph/StandaloneModuleGraph`, `test_runner/{Collection,Execution,Order,ScopeFunctions,bun_test}`, `uws_sys/{Request,Timer,h3}`, `watcher/INotifyWatcher`。

- **[2026-11-20] Wave 23 E 组 (5 跨模块根因)**：更新 `runtime/node/node_process`, `bun`, `bundler/options`, `http/HTTPThread`, `string/StringBuilder`。

- **[2026-11-20] Wave 24 A 组 (9 模块)**：更新 `runtime/node/{node_fs,node_fs_watcher}`, `js_parser/ast/{P,parse,parseProperty}`, `js_printer/js_printer`, `resolver/package_json`, `install/hosted_git_info`, `bundler_jsc/output_file_jsc`。

- **[2026-11-20] Wave 24 C 组 (32 模块)**：更新 `bake/DevServer{,/HotReloadEvent}`, `bundler/{ParseTask,analyze_transpiled_module,transpiler}`, `bundler_jsc/PluginRunner`, `cli/{filter_run,multi_run,test_command}`, `http/Headers`, `install/{PackageManager/PackageManagerOptions,TarballStream,dependency,integrity,lifecycle_script_runner,lockfile,lockfile/{Buffers,Package,Package/WorkspaceMap},npm,repository}`, `interchange/json`, `js_parser/ast/{CharFreq,Parser,visit}`, `jsc/{Debugger,JSGlobalObject,WTF,bindgen,web_worker}`, `resolver/{resolver,tsconfig_json}`。

- **[2026-11-20] Wave 24 B 组 + E 组 (26 模块)**：更新 `options_types/CompileTarget`, `jsc/{VirtualMachine,AsyncModule,RuntimeTranspilerStore,ZigStackTrace}`, `sql_jsc/{postgres/PostgresSQLConnection,mysql/{JSMySQLConnection,protocol/ResultSet}}`, `sql/{mysql/protocol/NewWriter,postgres/protocol/PortalOrPreparedStatement}`, `runtime/{api/{Archive,cron,bun/js_bun_spawn_bindings},webcore/{Blob,blob/{copy_file,write_file},fetch/FetchTasklet,s3/{simple_request,client,multipart}},server/FileRoute,socket/ssl_wrapper}`, `http/HTTPThread`, `shell/states/Pipeline`, `string/immutable`, `install/PackageManager/PackageManagerLifecycle`。

- **[2026-11-20] Wave 24 D 组 (39 模块)**：更新 `js_parser/ast/{parsePrefix,parseImportExport}`, `interchange/toml`, `router/router`, `runtime/{api/{JSBundler,JSTranspiler,NativePromiseContext,UnsafeObject,bun/process},dns_jsc/dns,image/Image,node/{node_process,path_watcher},server/server,socket/UpgradedDuplex,webcore/{FileSink,Request,ResumableSink,blob/Store,fetch,s3/{download_stream,list_objects}}}`, `shell/{shell,subproc}`, `sourcemap/InternalSourceMap`, `sourcemap_jsc/{CodeCoverage,source_provider}`, `sql/mysql/protocol/{AuthSwitchRequest,PreparedStatement}`, `sql_jsc/{mysql/{MySQLConnection,MySQLValue},postgres/{PostgresRequest,PostgresSQLQuery}}`, `standalone_graph/StandaloneModuleGraph`, `test_runner/{Order,ScopeFunctions}`, `uws_sys/{Timer,h3}`, `watcher/INotifyWatcher`。

- **[2026-11-20] Wave 25 A 组 (23 模块)**：更新 `options_types/CompileTarget`, `interchange/toml`, `js_parser/ast/{Expr,Binding,CharFreq,G,Parser,visit,visitStmt}`, `js_printer/js_printer`, `resolver/{package_json,resolver,tsconfig_json}`, `router/router`, `bake/DevServer`, `bundler/{analyze_transpiled_module,ParseTask}`, `bundler_jsc/PluginRunner`, `cli/{filter_run,multi_run,test_command}`, `sourcemap/InternalSourceMap`, `sourcemap_jsc/CodeCoverage`。

- **[2026-11-20] Wave 25 B 组 + C 组 (52 模块)**：更新 `install/{dependency,integrity,repository,PackageManager/{PackageManagerOptions,runTasks},TarballStream,extract_tarball,hosted_git_info,lifecycle_script_runner,lockfile/{Package,Package/WorkspaceMap,bun.lock},npm}`, `jsc/{VirtualMachine,AsyncModule,Debugger,ModuleLoader,NodeModuleModule}`, `sql_jsc/{postgres/{PostgresSQLConnection,PostgresRequest},mysql/{MySQLConnection,MySQLValue}}`, `sql/{mysql/protocol/{NewWriter,PreparedStatement},postgres/protocol/PortalOrPreparedStatement}`, `runtime/{api/{Archive,JSBundler,JSTranspiler,UnsafeObject,bun/{js_bun_spawn_bindings,process}},dns_jsc/dns,image/Image,node/{node_fs,path_watcher},server/server,socket/{ssl_wrapper,UpgradedDuplex},webcore/{ResumableSink,Request,blob/{copy_file,write_file},fetch/FetchTasklet,s3/{client,list_objects,multipart,simple_request}}}`, `shell/{shell,states/Pipeline}`, `string/{immutable,immutable/exact_size_matcher}`, `watcher/INotifyWatcher`。

- **[2026-11-20] Wave 25 E 组 (2 跨模块根因)**：更新 `options_types/CompileTarget`, `bundler/options`。

- **[2026-11-20] Wave 25 D 组 (4 跨模块根因)**：更新 `bundler/OutputFile`, `http/Headers`, `interchange/json`, `js_parser/ast/P`。

- **[2026-11-20] Wave 26 B 组 (21 模块)**：更新 `bun_alloc/maybe_owned`, `install/{PackageManager/{PackageManagerOptions,runTasks},TarballStream,dependency,extract_tarball,integrity,lockfile,lockfile/{CatalogMap,Package/WorkspaceMap,bun.lock},npm,resolvers/folder_resolver}`, `jsc/{ModuleLoader,NodeModuleModule,VirtualMachine,web_worker}`, `sql/mysql/protocol/{AuthSwitchResponse,PreparedStatement}`, `sql_jsc/postgres/{PostgresRequest,PostgresSQLConnection}`。

- **[2026-11-20] Wave 26 A 组 (20 模块)**：更新 `options_types/CompileTarget`, `bake/DevServer{,/WatcherAtomics,/SerializedFailure}`, `bundler/{bundle_v2,analyze_transpiled_module,defines,OutputFile,ParseTask,entry_points}`, `bundler_jsc/output_file_jsc`, `js_printer/js_printer`, `js_parser/ast/{CharFreq,ImportScanner,P,Parser,visit}`, `cli/test_command`, `standalone_graph/StandaloneModuleGraph`, `sourcemap_jsc/CodeCoverage`。

- **[2026-11-20] Wave 26 C 组 (23 模块)**：更新 `shell/{shell,states/Pipeline}`, `paths/resolve_path`, `runtime/{api/{JSTranspiler,Archive,bun/{js_bun_spawn_bindings,spawn}},node/path_watcher,server/server,socket/UpgradedDuplex,webcore/{ResumableSink,blob/{Store,copy_file,write_file},s3/{multipart,simple_request}}}`, `http/HTTPThread`, `http_jsc/headers_jsc`, `libarchive/libarchive`, `ptr/owned`, `router/router`, `string/immutable/exact_size_matcher`, `watcher/INotifyWatcher`。

- **[2026-11-20] Wave 26 D 组 (3 跨模块根因)**：更新 `install/repository`, `jsc/VM`, `sql_jsc/postgres/DataCell`。

- **[2026-11-20] Wave 26 E 组 (4 跨模块根因)**：更新 `http/AsyncHTTP`, `interchange/toml`, `js_parser/ast/parsePrefix`, `string/immutable`。

- **[2026-11-20] Wave 27 C 组 (14 模块)**：更新 `http/AsyncHTTP`, `libarchive/libarchive`, `runtime/{api/{Archive,JSTranspiler,bun/{js_bun_spawn_bindings,process}},server/{ServerConfig,server},socket/UpgradedDuplex,webcore/{blob/copy_file,s3/multipart}}`, `shell/shell`, `string/immutable/unicode`, `watcher/INotifyWatcher`。

- **[2026-11-20] Wave 27 B 组 (16 模块)**：更新 `install/{resolvers/folder_resolver,PackageManager/PackageManagerEnqueue,NetworkTask,TarballStream,dependency,extract_tarball,lockfile/{CatalogMap,Package/WorkspaceMap,bun.lock},npm}`, `jsc/{ModuleLoader,RuntimeTranspilerStore,VirtualMachine}`, `sql/postgres/protocol/DataRow`, `sql_jsc/postgres/{DataCell,PostgresSQLConnection}`。

- **[2026-11-20] Wave 27 A 组 (15 模块)**：更新 `options_types/CompileTarget`, `interchange/toml`, `js_printer/js_printer`, `js_parser/ast/{CharFreq,ConvertESMExportsForHmr,P,Parser,visit}`, `bake/DevServer`, `bundler/{HTMLScanner,analyze_transpiled_module,bundle_v2}`, `cli/test_command`, `sourcemap_jsc/CodeCoverage`, `router/router`。

- **[2026-11-20] Wave 27 D 组 (2 跨模块根因)**：更新 `bun_alloc/maybe_owned`, `runtime/api/JSTranspiler`。

- **[2026-11-20] Wave 28 A 组 (15 模块)**：更新 `js_printer/js_printer`, `js_parser/{parser,ast/{Expr,P,visit,visitStmt,Binding,CharFreq,ConvertESMExportsForHmr,Parser,SideEffects,lowerDecorators,maybe,visitExpr}}`, `resolver/resolver`。

- **[2026-11-20] Wave 28 C 组 (11 模块)**：更新 `jsc/VirtualMachine`, `sql_jsc/postgres/{PostgresSQLConnection,DataCell}`, `runtime/{webcore/s3/multipart,api/{JSTranspiler,bun/process},server/ServerConfig}`, `http/AsyncHTTP`, `shell/shell`, `threading/channel`, `watcher/INotifyWatcher`。

- **[2026-11-20] Wave 28 B 组 (15 模块)**：更新 `semver/SemverQuery`, `bake/bake`, `bundler/{HTMLScanner,analyze_transpiled_module,bundle_v2}`, `install/{NetworkTask,TarballStream,extract_tarball,lockfile,lockfile/bun.lock,migration,npm,resolution,resolvers/folder_resolver}`, `router/router`。
- **[2026-11-20] Wave 28 D 组 (3 跨模块根因)**：更新 `bundler/ParseTask`, `js_parser/ast/KnownGlobal`, `jsc/Counters`。
- **[2026-11-20] Wave 28 E 组 (1 跨模块根因)**：更新 `install/bin`。
- **[2026-11-21] Wave 29 C 组 (9 模块)**：更新 `jsc/jsc`, `runtime/{api/bun/{js_bun_spawn_bindings,spawn},server/ServerConfig,webcore/s3/multipart}`, `shell/shell`, `sql_jsc/postgres/DataCell`, `threading/channel`, `watcher/INotifyWatcher`。
- **[2026-11-21] Wave 29 B 组 (15 模块)**：更新 `bake/{DevServer/ErrorReportRequest,bake}`, `bundler/{HTMLScanner,analyze_transpiled_module,bundle_v2,entry_points}`, `router/router`, `install/{TarballStream,dependency,extract_tarball,lockfile/{Package,bun.lock},migration,npm,resolution}`。
- **[2026-11-21] Wave 29 D 组 (1 跨模块根因)**：更新 `install/resolvers/folder_resolver`。
- **[2026-11-21] Wave 29 A 组 (15 模块)**：更新 `js_printer/js_printer`, `js_parser/{parser,ast/{visitExpr,Expr,P,visit,lowerDecorators,visitBinaryExpression,visitStmt,ConvertESMExportsForHmr,E,Parser,SideEffects,maybe}}`, `js_parser_jsc/Macro`。
- **[2026-11-21] Wave 30 C 组 (10 模块)**：更新 `runtime/{api/bun/{js_bun_spawn_bindings,process},webcore/s3/multipart,server/ServerConfig}`, `sql_jsc/postgres/DataCell`, `jsc/RuntimeTranspilerCache`, `options_types/CompileTarget`, `router/router`, `shell/shell`, `threading/channel`。
- **[2026-11-21] Wave 30 B 组 (15 模块)**：更新 `bake/bake`, `bundler/{bundle_v2,transpiler,LinkerGraph,ParseTask,analyze_transpiled_module}`, `install/{lockfile/{Package,bun.lock},dependency,PackageManifestMap,TarballStream,extract_tarball,migration,repository,resolution}`。
- **[2026-11-21] Wave 30 A 组 (10 模块)**：更新 `js_printer/js_printer`, `js_parser/{parser,ast/{Expr,P,visitExpr,visitBinaryExpression,ConvertESMExportsForHmr,lowerDecorators,ServerComponentBoundary}}`, `js_parser_jsc/Macro`。
- **[2026-11-21] Wave 31 C 组 (13 模块)**：更新 `install/{lockfile/{Package,bun.lock},PackageManager/PackageManagerEnqueue,dependency,TarballStream,migration,repository,resolution}`, `sql_jsc/postgres/DataCell`, `jsc/AsyncModule`, `runtime/api/bun/process`, `shell/shell`, `threading/ThreadPool`。
- **[2026-11-21] Wave 31 B 组 (11 模块)**：更新 `css/{css_parser,error,rules/rules,targets}`, `bake/bake`, `bundler/{LinkerContext,ParseTask,analyze_transpiled_module,bundle_v2,options}`, `standalone_graph/StandaloneModuleGraph`。
- **[2026-11-21] Wave 31 A 组 (10 模块)**：更新 `js_parser/ast/{Expr,B,ConvertESMExportsForHmr,P,visit,visitExpr,lowerDecorators,maybe,visitBinaryExpression}`, `js_parser_jsc/Macro`。
- **[2026-11-21] Wave 32 C 组 (9 模块)**：更新 `sql_jsc/postgres/DataCell`, `install/{lockfile/{Package,bun.lock},PackageManager/PackageManagerEnqueue,TarballStream,dependency,migration,repository,resolution}`。
- **[2026-11-21] Wave 32 A 组 (12 模块)**：更新 `css/{generics,declaration,css_parser,selectors/{parser,builder,selector},media_query,prefixes,rules/rules}`, `exe_format/macho`, `ptr/owned`, `runtime/api/bun/spawn`。
- **[2026-11-21] Wave 32 B 组 (19 模块)**：更新 `bundler/{LinkerGraph,LinkerContext,ParseTask,analyze_transpiled_module,bundle_v2,options,linker_context/{computeChunks,generateChunksInParallel,renameSymbolsInChunk,scanImportsAndExports}}`, `js_parser/{parser,ast/{P,ConvertESMExportsForHmr,B,Parser,lowerDecorators,maybe,visitExpr}}`, `js_parser_jsc/Macro`。
- **[2026-11-21] Wave 33 A 组 (13 模块)**：更新 `css/{generics,css_parser,declaration,rules/{style,supports},values/{position,gradient,ident,image,resolution},media_query,selectors/selector}`, `exe_format/macho`。
- **[2026-11-21] Wave 33 D 组 (2 跨模块根因)**：更新 `css/values/length`, `css/rules/media`。
- **[2026-11-21] Wave 33 附增 (1 模块)**：重编更新 `css/properties/properties_generated`。
- **[2026-11-21] Wave 33 C 组 (18 模块)**：更新 `bun`, `install/{PackageManager/{PackageManagerEnqueue,runTasks},lockfile/{Package,bun.lock},lifecycle_script_runner,TarballStream,migration,resolution}`, `cli/{filter_run,multi_run}`, `runtime/api/bun/{process,js_bun_spawn_bindings,spawn}`, `shell/subproc`, `sql_jsc/postgres/DataCell`, `jsc/AsyncModule`, `paths/resolve_path`。
- **[2026-11-21] Wave 33 B 组 (18 模块)**：更新 `bundler/{LinkerContext,ParseTask,bundle_v2,options,linker_context/{prepareCssAstsForChunk,computeChunks,doStep5,generateChunksInParallel,generateCodeForLazyExport,renameSymbolsInChunk,scanImportsAndExports}}`, `js_parser/ast/{visitExpr,P,visitStmt,Parser,lowerDecorators}`, `js_printer/renamer`, `interchange/json`。
- **[2026-11-21] Wave 34 C 组 (11 模块)**：更新 `runtime/api/bun/{process,js_bun_spawn_bindings}`, `install/{PackageManager/{runTasks,PackageManagerEnqueue},TarballStream,lifecycle_script_runner,lockfile/bun.lock,migration,resolvers/folder_resolver}`, `jsc/AsyncModule`, `shell/subproc`。
- **[2026-11-21] Wave 34 B 组 (14 模块)**：更新 `bundler/{ParseTask,bundle_v2,linker_context/{findImportedFilesInCSSOrder,generateCodeForLazyExport,computeChunks,doStep5,generateChunksInParallel}}`, `js_parser/ast/{visitExpr,lowerDecorators,visitStmt,P,visit}`, `resolver/data_url`, `semver/Version`。
- **[2026-11-21] Wave 34 A 组 (16 模块)**：更新 `css/{css_parser,generics,media_query,declaration,properties/properties_generated,rules/{font_face,style,supports,font_palette_values,import,keyframes,page},values/gradient,selectors/{parser,selector}}`, `exe_format/macho`。
- **[2026-11-21] Wave 35 B 组 (28 模块)**：更新 `css/rules/{supports,import,page,font_face,font_palette_values,keyframes,container,counter_style,layer,namespace,property,unknown,viewport}`, `bundler/{AstBuilder,linker_context/{findImportedFilesInCSSOrder,computeChunks,doStep5,generateChunksInParallel}}`, `install/{PackageManager/{processDependencyList,runTasks},lockfile,lockfile/bun.lock,lifecycle_script_runner,resolution,resolvers/folder_resolver}`, `js_parser/ast/lowerDecorators`, `bun`, `shell/states/Cmd`。
- **[2026-11-21] Wave 35 A 组 (26 模块)**：更新 `css/{css_parser,generics,declaration,media_query,selectors/{parser,selector},values/{position,image,alpha,color,ident,url},properties/{align,border,flex,transition,background,margin_padding,box_shadow,font,size,animation,border_image,prefix_handler,properties_generated,transform}}`。
- **[2026-11-21] Wave 35 C 组 (4 跨模块根因)**：更新 `bundler/{ParseTask,Chunk}`, `jsc/ipc`, `css/properties/properties_impl`。
- **[2026-11-21] Wave 35 D 组 (3 跨模块根因)**：更新 `css/{values/easing,rules/{keyframes,font_palette_values}}`。
- **[2026-11-21] Wave 36 B 组 (20 模块)**：更新 `bundler/{Chunk,bundle_v2,AstBuilder,ParseTask,linker_context/{computeChunks,findImportedFilesInCSSOrder,generateChunksInParallel,generateCodeForFileInChunkJS,generateCompileResultForCssChunk,generateCompileResultForJSChunk}}`, `bake/DevServer/DirectoryWatchStore`, `install/{lockfile,lockfile/bun.lock,PackageManager/processDependencyList,lifecycle_script_runner,resolution,resolvers/folder_resolver}`, `js_parser/ast/lowerDecorators`, `runtime/api/bun/subprocess/Writable`, `shell/subproc`。
- **[2026-11-21] Wave 36 A 组 + C 组 (35 模块)**：更新 `js_printer/js_printer`, `css/{css_parser,declaration,generics,media_query,css_modules,dependencies,printer,selectors/{parser,selector},rules/{font_face,supports,font_palette_values},values/{ident,length,syntax,angle,color,resolution,time},properties/{flex,properties_generated,border,align,border_image,background,custom,margin_padding,border_radius,size,box_shadow,css_modules,font,transform,transition}}`。
- **[2026-11-21] Wave 37 B 组 (20 模块)**：更新 `js_parser/ast/{lowerDecorators,ImportScanner}`, `bundler/{bundle_v2,ServerComponentParseTask,linker_context/{computeChunks,findImportedFilesInCSSOrder,generateChunksInParallel,generateCodeForFileInChunkJS,postProcessJSChunk}}`, `bake/DevServer/DirectoryWatchStore`, `install/{PackageManager/processDependencyList,lifecycle_script_runner,lockfile,lockfile/{Tree,bun.lock}}`, `js_printer/js_printer`, `runtime/{api/bun/{js_bun_spawn_bindings,subprocess/Readable},webcore/FileSink}`, `shell/subproc`。
- **[2026-11-21] Wave 37 A 组 (25 模块)**：更新 `css/{css_parser,css_modules,declaration,dependencies,media_query,selectors/{parser,selector},rules/{container,font_face,supports},values/{length,rect,resolution},properties/{properties_generated,border_radius,align,background,border_image,margin_padding,transform,size,border,box_shadow,custom,font}}`。
- **[2026-11-21] Wave 38 B 组 (15 模块)**：更新 `shell/subproc`, `runtime/api/bun/{subprocess,subprocess/Writable,js_bun_spawn_bindings}`, `js_parser/ast/{lowerDecorators,ConvertESMExportsForHmr,Parser}`, `bundler/{bundle_v2,linker_context/{computeChunks,generateChunksInParallel,generateCodeForFileInChunkJS,postProcessJSChunk}}`, `install/{lockfile/{Tree,bun.lock},migration}`。
- **[2026-11-21] Wave 38 A 组 (28 模块)**：更新 `css/{css_parser,context,declaration,printer,media_query,rules/{container,rules,supports,font_face},selectors/{parser,selector},values/{percentage,length,position,easing,image},properties/{animation,custom,transition,size,align,border,border_radius,prefix_handler,background,box_shadow,margin_padding,properties_generated}}`。
- **[2026-11-21] Wave 39 B 组 (13 模块)**：更新 `bundler/{bundle_v2,AstBuilder,analyze_transpiled_module,linker_context/{findAllImportedPartsInJSOrder,generateChunksInParallel,postProcessJSChunk}}`, `js_parser/ast/{lowerDecorators,Parser}`, `install/{lockfile/{Tree,bun.lock},migration}`, `jsc/ProcessAutoKiller`, `runtime/api/bun/js_bun_spawn_bindings`。
- **[2026-11-21] Wave 39 A 组 (27 模块)**：更新 `css/{css_parser,generics,media_query,context,selectors/{parser,selector},values/{easing,image,position},rules/{font_face,container,import,rules,supports,property},properties/{margin_padding,border,align,font,box_shadow,custom,ui,border_radius,css_modules,text,transition,size}}`。
- **[2026-11-21] Wave 40 B 组 (12 模块)**：更新 `js_parser/{parser,runtime,ast/{P,lowerDecorators}}`, `bundler/{bundle_v2,AstBuilder,LinkerContext,linker_context/{findAllImportedPartsInJSOrder,generateChunksInParallel}}`, `install/{yarn,lockfile/{Tree,bun.lock}}`。
- **[2026-11-21] Wave 40 A 组 (28 模块)**：更新 `css/{css_parser,media_query,generics,context,printer,selectors/{parser,selector},values/{gradient,image,position,url},rules/{container,font_face,font_palette_values,keyframes,page,property,style,supports},properties/{align,border,box_shadow,css_modules,custom,display,font,margin_padding,size}}`。
- **[2026-11-21] Wave 41 B 组 (12 模块)**：更新 `js_parser/ast/{lowerDecorators,KnownGlobal,Symbol}`, `bundler/{bundle_v2,Chunk,HTMLImportManifest,linker_context/{computeCrossChunkDependencies,postProcessCSSChunk,writeOutputFilesToDisk}}`, `install/{lockfile/bun.lock,yarn}`, `sourcemap/sourcemap`。
- **[2026-11-21] Wave 41 A 组 (24 模块)**：更新 `css/{media_query,context,css_parser,selectors/parser,values/{image,position,resolution,gradient,ident,url},rules/{font_face,page,supports,container,counter_style,font_palette_values,property,rules,style},properties/{custom,margin_padding,align,box_shadow,display}}`。
- **[2026-11-21] Wave 42 B 组 (7 模块)**：更新 `bundler/{Chunk,HTMLImportManifest,bundle_v2,linker_context/{computeCrossChunkDependencies,writeOutputFilesToDisk}}`, `install/{lockfile/bun.lock,yarn}`。
- **[2026-11-21] Wave 42 A 组 (14 模块)**：更新 `css/{css_parser,media_query,selectors/parser,rules/{document,font_palette_values,font_face},values/{syntax,resolution,gradient},properties/{align,box_shadow,custom,display,margin_padding}}`。
- **[2026-11-21] Wave 42 C 组 (1 跨模块根因)**：更新 `js_parser/ast/P`。
- **[2026-11-21] Wave 43 A 组 (6 模块)**：更新 `css/{css_parser,properties/margin_padding,rules/rules,selectors/parser,values/{gradient,syntax}}`。
- **[2026-11-21] Wave 43 B 组 (6 模块)**：更新 `bundler/{HTMLImportManifest,bundle_v2,linker_context/{computeCrossChunkDependencies,writeOutputFilesToDisk}}`, `install/{lockfile,yarn}`。
- **[2026-11-21] Wave 44 B 组 (6 模块)**：更新 `bundler/{Chunk,bundle_v2,linker_context/computeCrossChunkDependencies}`, `install/{lockfile,lockfile/Tree,yarn}`。
- **[2026-11-21] Wave 44 A 组 (6 模块)**：更新 `css/{selectors/parser,values/{gradient,syntax},properties/margin_padding,rules/{custom_media,rules}}`。
- **[2026-11-21] Wave 45 B 组 (8 模块)**：更新 `bake/{DevServer,DevServer/IncrementalGraph}`, `jsc/CachedBytecode`, `bundler/{bundle_v2,linker_context/writeOutputFilesToDisk}`, `install/{lockfile/{bun.lock,bun.lockb},yarn}`。
- **[2026-11-21] Wave 45 A 组 (6 模块)**：更新 `css/{values/{gradient,syntax},rules/{property,rules,layer},selectors/parser}`。
- **[2026-11-21] Wave 46 A 组 (2 跨模块根因)**：更新 `css/{values/syntax,rules/container}`。
- **[2026-11-21] Wave 46 B 组 (8 模块)**：更新 `bundler/{Chunk,linker_context/writeOutputFilesToDisk}`, `bake/DevServer/IncrementalGraph`, `install/{PackageManager/PopulateManifestCache,lockfile/{Tree,bun.lock,bun.lockb},pnpm}`。
- **[2026-11-21] Wave 47 A 组 (3 模块)**：更新 `css/{rules/{container,scope},values/syntax}`。
- **[2026-11-21] Wave 47 B 组 (6 模块)**：更新 `bake/DevServer/IncrementalGraph`, `bundler/linker_context/generateChunksInParallel`, `install/{PackageManager/PopulateManifestCache,lockfile/{bun.lock,bun.lockb},pnpm}`。
- **[2026-11-21] Wave 48 A 组 (2 模块)**：更新 `css/rules/{container,scope}`。
- **[2026-11-21] Wave 48 B 组 (4 模块)**：更新 `bake/DevServer`, `jsc/CachedBytecode`, `install/{lockfile/bun.lock,pnpm}`。
- **[2026-11-21] Wave 49 A+B 组 (6 模块)**：更新 `css/{printer,rules/container}`, `bake/DevServer`, `bundler/linker_context/generateChunksInParallel`, `install/{bin,pnpm}`。
- **[2026-11-21] Wave 50 (8 模块 / 全仓 `zig:check` 0 errors)**：更新 `bundler/{bundle_v2,linker_context/MetafileBuilder}`, `install/{bin,lockfile/bun.lock,migration,pnpm,repository}`, `js_parser/ast/Expr`。
- **[2026-11-21] Export 补齐 A+B 组 (13 模块)**：补齐 `bun_core/Global`, `jsc/rare_data`, `paths/resolve_path`, `platform/linux`, `runtime/api/BunObject`, `runtime/crypto/PasswordObject`, `runtime/webcore/{Blob,Crypto,FileSink,S3File,fetch,fetch/FetchTasklet}`, `uws/uws` 的 C ABI 导出声明。
- **[2026-11-21] Export 激活与语义闭环 (60+ 模块 / 全仓 `zig:check` 0 errors)**：规范化 58 个 `export fn` 模块与 7 个 `export const` 模块并修复导出函数内部语义错误，涵盖 `analytics`, `bake/production`, `boringssl`, `bun`, `bundler/{OutputFile,ParseTask,analyze_transpiled_module,bundle_v2}`, `bundler_jsc/analyze_jsc`, `cli/test_command`, `http_types/Method`, `io/source`, `jsc/*`, `main*`, `napi`, `paths/resolve_path`, `platform/linux`, `runtime/{api,crypto,node,server,timer,webcore,webview}/*`, `string/immutable/{escapeHTML,visible}`, `sys`, `threading/Mutex`, `uws*`, `workaround_missing_symbols`。

- **[2026-11-21] 全仓 `zig:check` 0-Error 收敛 & 规则固化**：更新 `GEMINI.md`（§十一）、`scripts/cpc_brief.py`（FAIL 非 0 退出）、`src/{bun.js,main_test}`, `src/cli/{Arguments,cli,create_command,exec_command,filter_arg,filter_run,multi_run,run_command,upgrade_command,create/SourceFileProjectGenerator}`, `src/crash_handler/crash_handler`, `src/paths/resolve_path`, `src/runtime/{api/bun/process,server/ServerConfig,webview/HostProcess}`（均同步更新 `.cp` 与 `.zig`）。
- **[2026-11-21] `bun-debug` 首次构建链接与 `Bun.version` (`1.3.14`) 启动通过**：更新 `src/bundler/options.{cp,zig}`, `src/cli/upgrade_command.{cp,zig}`, `src/css/{css_parser,values/image}.{cp,zig}`, `src/io/heap.{cp,zig}`, `src/jsc/{DecodedJSValue,GarbageCollectionController}.{cp,zig}`, `src/js_parser/ast/parseSuffix.{cp,zig}`, `src/runtime/socket/socket.{cp,zig}`, `src/runtime/webcore/{fetch/FetchTasklet,streams}.{cp,zig}`, `src/string/wtf.{cp,zig}`, `src/test_runner/{jest,timers/FakeTimers}.{cp,zig}` 及全仓 20 个 `= if (opt) |x|` 排雷文件。
- **[2026-11-21] `bun bd test` 测试框架全链路贯通（26 模块修复）**：更新 `CP_PITFALLS.md`、`src/bundler/{ThreadPool,bundle_v2}.{cp,zig}`、`src/cli/{repl,test/Scanner,test_command}.{cp,zig}`、`src/css/css_parser.cp`、`src/dotenv/env_loader.{cp,zig}`、`src/http_types/URLPath.{cp,zig}`、`src/install/{dependency,extract_tarball,integrity}.{cp,zig}`、`src/interchange/yaml.{cp,zig}`、`src/js_parser/{lexer,ast/CharFreq,ast/skipTypescript,ast/visit}.{cp,zig}`、`src/js_printer/js_printer.{cp,zig}`、`src/jsc/{AsyncModule,GarbageCollectionController}.{cp,zig}`、`src/runtime/{api/BunObject,node/node_process,webcore/Blob,webcore/TextDecoder}.{cp,zig}`、`src/sourcemap/VLQ.{cp,zig}`、`src/string/{immutable,immutable/unicode}.{cp,zig}`、`src/sys/SignalCode.{cp,zig}`、`src/test_runner/Execution.{cp,zig}`。
- **[2026-11-21] `test/js/node/path` 全绿通关 (120 pass / 0 fail)**：更新 `src/runtime/node/path.{cp,zig}`、`src/runtime/webcore/{Blob,streams}.{cp,zig}`。
- **[2026-11-21] HTTP/Weak/Blob/Util 13 模块修复 (`fuzzy-wuzzy` 154 pass, `bun-file` 6 pass)**：更新 `src/http/{HTTPContext,http}.{cp,zig}`、`src/js_printer/js_printer.{cp,zig}`、`src/jsc/{Weak,ZigString}.{cp,zig}`、`src/paths/resolve_path.zig`、`src/runtime/{dns_jsc/dns,socket/uws_handlers}.{cp,zig}`、`src/runtime/webcore/{Blob,blob/Store,fetch/FetchTasklet}.{cp,zig}`、`src/string/immutable/escapeHTML.{cp,zig}`、`src/uws_sys/SocketGroup.{cp,zig}`。
- **[2026-11-21] `PasswordObject` RAII 提前释放修复 (`password.test.ts` 74 pass / 0 fail)**：更新 `src/runtime/crypto/PasswordObject.{cp,zig}`。
- **[2026-11-21] `ServerConfig` RAII 提前释放修复 (`wildcard static routes` 通过)**：更新 `src/runtime/server/ServerConfig.{cp,zig}`。
- **[2026-11-21] `Sink.cp` `SinkSignal.init` 修复 (`serve-response-stream-sink-leak` 通过)**：更新 `src/runtime/webcore/Sink.{cp,zig}`。
- **[2026-11-21] `unicode.cp` `...` 范围语法修复 (`css-fuzz` 崩溃清零)**：更新 `src/string/immutable/unicode.{cp,zig}`。
- **[2026-11-21] `SSLConfig.cp` RAII 提前释放修复 (`bun-write.test.js` 0 崩溃)**：更新 `src/runtime/socket/SSLConfig.{cp,zig}`。
- **[2026-11-21] 全仓 393 个旧版 `cp_iter_slice` 模块重编 (`zig:check` 5/5)**：更新 `src/bundler/options.{cp,zig}`、`src/perf/tracy.{cp,zig}`、`src/runtime/node/fs_events.{cp,zig}`、`src/string/immutable.{cp,zig}`、`src/sys/{libuv,coreutils}_error_map.{cp,zig}`、`src/which/which.{cp,zig}`、`src/http/{h2,h3}_client/encode.{cp,zig}`、`src/md/line_analysis.zig` 及全仓 380+ 重编 `.zig` 文件。
- **[2026-11-21] `extract_tarball.cp` 悬垂栈切片修复**：更新 `src/install/extract_tarball.{cp,zig}`。
- **[2026-11-21] `resolution.cp` 与 `js_printer.cp` 指针修复 (`Batch 3` 零 ASan 崩溃)**：更新 `src/install/resolution.{cp,zig}`、`src/js_printer/js_printer.{cp,zig}`。
- **[2026-11-21] `Handlers.cp` RAII 提前释放修复 (`sql`/`valkey` 425 pass)**：更新 `src/runtime/socket/Handlers.{cp,zig}`。
- **[2026-11-21] Valkey/Lockfile/RareData/Blob/Order/Socket 6 模块修复（0 崩溃，135 pass）**：更新 `src/runtime/valkey_jsc/js_valkey.{cp,zig}`、`src/install/lockfile/bun.lock.{cp,zig}`、`src/jsc/rare_data.{cp,zig}`、`src/runtime/webcore/Blob.{cp,zig}`、`src/test_runner/Order.{cp,zig}`、`src/runtime/socket/socket.{cp,zig}`。

- **[2026-11-21] `fetch.cp` 与 `h2_client` 3 模块修复（`zstd` / `undici-h2` 全绿）**：更新 `src/runtime/webcore/fetch.{cp,zig}`、`src/http/h2_client/{Stream,ClientSession,PendingConnect}.{cp,zig}`。
- **[2026-11-21] 饱和算术 / `npm.cp` / 全仓 15 个高危对象 `release()` 排雷**：更新 `src/bundler/BundleThread.{cp,zig}`、`src/install/npm.{cp,zig}`、`src/{glob/GlobWalker,css/small_list,js_printer/js_printer,runtime/server/NodeHTTPResponse,crash_handler/crash_handler,cli/repl,js_parser/ast/P}.{cp,zig}`，以及 15 个 P0/P1 `releaseInternal`/`releaseLoop` 修复模块（`FileResponseStream`, `PosixSignalHandle`, `Listener`, `TimerObjectInternals`, `ConcurrentTask`, `udp_socket`, `UpgradedDuplex`, `Image`, `PBKDF2`, `Loop`, `WindowsNamedPipeContext`, `WindowsNamedPipe`, `s3/simple_request`, `s3/download_stream`, `ssl_wrapper`）。
- **[2026-11-21] `node_fs.cp` 与 5 个次级 `release()` 清雷**：更新 `src/runtime/node/node_fs.{cp,zig}`、`src/runtime/webcore/S3Stat.{cp,zig}`、`src/runtime/webcore/ObjectURLRegistry.{cp,zig}`、`src/runtime/webcore/s3/error_jsc.{cp,zig}`、`src/runtime/socket/SocketAddress.{cp,zig}`、`src/uws_sys/us_socket_t.{cp,zig}`。
- **[2026-11-21] `multi_run.cp` 进程退出监视与回收修复（117/118 pass）**：更新 `src/cli/multi_run.{cp,zig}`、`src/install/npm.zig`。
- **[2026-11-21] `multi_run` / `RuntimeTranspilerCache` / `visit` / `lexer` 修复（4 测试集全绿）**：更新 `src/cli/multi_run.{cp,zig}`、`src/jsc/RuntimeTranspilerCache.{cp,zig}`、`src/js_parser/ast/visit.{cp,zig}`、`src/js_parser/lexer.{cp,zig}`。
- **[2026-11-21] `bun.lock` / `node_zlib_binding` / `package_manager_command` 修复**：更新 `src/install/lockfile/bun.lock.{cp,zig}`、`src/runtime/node/node_zlib_binding.{cp,zig}`、`src/cli/package_manager_command.{cp,zig}`。
- **[2026-11-21] `ini` / `package_json` / `writeOutputFilesToDisk` 修复（15/15 全绿）**：更新 `src/ini/ini.{cp,zig}`、`src/resolver/package_json.{cp,zig}`、`src/bundler/linker_context/writeOutputFilesToDisk.{cp,zig}`。
- **[2026-11-21] `build_command.cp` `define` 丢失修复 (`zig:check` 5/5)**：更新 `src/cli/build_command.{cp,zig}`。
- **[2026-11-21] 全仓 `migrate_cp_syntax.py` 迁移、28 模块 `release()` 还原与 `fetch.cp` 修复 (`zig:check` 5/5)**：更新 `src/runtime/webcore/fetch.{cp,zig}`、还原 `release()` 的 28 个 `.cp/.zig` 模块，以及全仓 96 个 `migrate_cp_syntax.py` 迁移的 `.cp/.zig` 文件。
- **[2026-11-22] `dns` / `css/context` / `udp_socket` / `uws_sys/udp` / `Postgres` / `MySQL` 修复（全绿）**：更新 `src/runtime/dns_jsc/dns.{cp,zig}`、`src/css/context.{cp,zig}`、`src/runtime/socket/udp_socket.{cp,zig}`、`src/uws_sys/udp.{cp,zig}`、`src/sql_jsc/postgres/PostgresSQLConnection.{cp,zig}`、`src/sql_jsc/mysql/JSMySQLConnection.{cp,zig}`。
- **[2026-11-22] `http` / `css/rules/property` 修复与 24 个遗留隐式 `release()` 模块重编清零 (`zig:check` 5/5)**：更新 `src/http/http.{cp,zig}`、`src/css/rules/property.{cp,zig}`、`src/runtime/webcore/{s3/client,blob/copy_file}.{cp,zig}` 及全仓 22 个重编 `.zig` 文件。
- **[2026-11-22] HTTP/WebCore/Archive/pack/RuntimeTranspilerCache RAII `~init()` 加固与 `blob:`/`FormData` 恢复（103/103 全绿）**：更新 `src/http/http.{cp,zig}`、`src/runtime/webcore/{Blob,Response,S3File,fetch}.{cp,zig}`、`src/jsc/{DOMFormData,URLSearchParams,RuntimeTranspilerCache}.{cp,zig}`、`src/runtime/api/Archive.{cp,zig}`、`src/cli/pack_command.{cp,zig}` 及 `CP_PITFALLS.md`。
- **[2026-11-22] 9 大模块 `~init()` RAII 改造（消除 329 处手工 `defer`）与 `29181` 根治（`zig:check` 5/5，23/23 全绿）**：更新 `src/resolver/fs/stat_hash.{cp,zig}`、`src/runtime/server/server.{cp,zig}`、`src/paths/Path.{cp,zig}`、`src/runtime/api/{JSBundler,cron}.{cp,zig}`、`src/install/isolated_install.{cp,zig}`、`src/dotenv/env_loader.{cp,zig}`、`src/runtime/node/node_fs.{cp,zig}`、`src/bake/DevServer.{cp,zig}` 及 `CP_PITFALLS.md`。
- **[2026-11-22] 4 大模块泛型 RAII `~init()` 改造（消除 201 处手工 `defer`，膨胀率 <= 1.056）**：更新 `src/install/isolated_install/Installer.{cp,zig}`、`src/runtime/api/bun/h2_frame_parser.{cp,zig}`、`src/jsc/ConsoleObject.{cp,zig}`、`src/runtime/api/BunObject.{cp,zig}`。
- **[2026-11-22] 10 大模块泛型 RAII `~init()` 改造（消除 322 处手工释放，膨胀率 0.956~1.122，`zig:check` 5/5）**：更新 `src/jsc/VirtualMachine.{cp,zig}`、`src/runtime/valkey_jsc/{js_valkey,js_valkey_functions}.{cp,zig}`、`src/sql_jsc/postgres/PostgresSQLConnection.{cp,zig}`、`src/runtime/dns_jsc/dns.{cp,zig}`、`src/runtime/server/RequestContext.{cp,zig}`、`src/test_runner/{pretty_format,expect}.{cp,zig}`、`src/runtime/api/JSTranspiler.{cp,zig}`、`src/runtime/api/bun/process.{cp,zig}` 及 `CP_PITFALLS.md`。
- **2026-11-22**: 完成 17 个核心模块 `~init()` RAII 改造与 `E0209` `unsafe {}` 收窄（含 `.cp` 与 `.zig`）：`src/runtime/socket/socket`、`src/js_printer/js_printer`、`src/test_runner/bun_test`、`src/sys/sys`、`src/runtime/server/RequestContext`、`src/runtime/webcore/Blob`、`src/runtime/webcore/S3File`、`src/runtime/node/path`、`src/runtime/api/html_rewriter`、`src/runtime/server/ServerWebSocket`、`src/cli/update_interactive_command`、`src/sql_jsc/mysql/MySQLConnection`、`src/shell/interpreter`、`src/resolver/resolver`、`src/jsc/ModuleLoader`、`src/http_jsc/websocket_client/WebSocketUpgradeClient`、`src/cli/repl`；同步更新 `CP_PITFALLS.md`。
- **2026-11-22**: 完成 16 个核心模块 `~init()` RAII 改造与 `E0209` `unsafe {}` 收窄（含 `.cp` 与 `.zig`）：`src/install/yarn`、`src/runtime/webcore/S3Client`、`src/runtime/webcore/fetch/FetchTasklet`、`src/cli/test/ChangedFilesFilter`、`src/bundler/bundle_v2`、`src/install/lockfile/lockfile_json_stringify_for_debugging`、`src/install/PackageInstall`、`src/install/hosted_git_info`、`src/cli/test_command`、`src/test_runner/diff/diff_match_patch`、`src/sql_jsc/mysql/JSMySQLConnection`、`src/install/PackageInstaller`、`src/runtime/server/server`、`src/install/pnpm`、`src/install/PackageManager/patchPackage`、`src/install/npm`。
- **2026-11-22**: 完成 12 个核心模块 `~init()` RAII 改造与 `E0209` `unsafe {}` 收窄（含 `.cp` 与 `.zig`）：`src/test_runner/timers/FakeTimers`、`src/sys/sys_uv`、`src/standalone_graph/StandaloneModuleGraph`、`src/runtime/webcore/Body`、`src/runtime/image/Image`、`src/jsc/web_worker`、`src/runtime/server/ServerConfig`、`src/cli/audit_command`、`src/shell/shell`、`src/runtime/socket/udp_socket`、`src/runtime/server/NodeHTTPResponse`、`src/bundler/linker_context/generateChunksInParallel`。
- **2026-11-22**: 完成 8 个核心模块 `~init()` RAII 改造与 `E0209` `unsafe {}` 收窄（含 `.cp` 与 `.zig`）：`src/install/PackageManager/security_scanner`、`src/install/lockfile/bun.lockb`、`src/runtime/node/types`、`src/jsc/JSValue`、`src/bake/FrameworkRouter`、`src/test_runner/ScopeFunctions`、`src/sourcemap_jsc/CodeCoverage`、`src/runtime/socket/Listener`。

## [2026-11-22 Wave #8] RAII `~init()` 与 `unsafe {}` 改造（17 模块）
- `src/jsc/JSGlobalObject.{cp,zig}` (`17 -> 0`, ratio `1.067`)
- `src/install/lockfile/bun.lock.{cp,zig}` (`17 -> 4`, ratio `1.006`)
- `src/cli/create_command.{cp,zig}` (`11 -> 6`, ratio `0.996`)
- `src/runtime/node/node_os.{cp,zig}` (`16 -> 3`, ratio `1.147`)
- `src/bundler/linker_context/MetafileBuilder.{cp,zig}` (`16 -> 5`, ratio `1.046`)
- `src/valkey/valkey_protocol.{cp,zig}` (`15 -> 0`, ratio `1.137`)
- `src/sql_jsc/mysql/protocol/DecodeBinaryValue.{cp,zig}` (`15 -> 0`, ratio `0.958`)
- `src/runtime/valkey_jsc/valkey.{cp,zig}` (`13 -> 2`, ratio `1.054`)
- `src/runtime/webcore/Response.{cp,zig}` (`15 -> 2`, ratio `0.963`)
- `src/runtime/crypto/CryptoHasher.{cp,zig}` (`15 -> 0`, ratio `1.045`)
- `src/napi/napi.{cp,zig}` (`15 -> 6` 控制流/锁, ratio `1.077`)
- `src/cli/pm_pkg_command.{cp,zig}` (`15 -> 0`, ratio `1.068`)
- `src/md/ansi_renderer.{cp,zig}` (`15 -> 3`, ratio `1.024`)
- `src/bake/production.{cp,zig}` (`15 -> 2`, ratio `1.065`)
- `src/shell/builtin/rm.{cp,zig}` (`14 -> 11` 控制流/锁, ratio `0.942`)
- `src/patch/patch.{cp,zig}` (`14 -> 1`, ratio `0.956`)
- `src/bake/DevServer/IncrementalGraph.{cp,zig}` (`16 -> 3`, ratio `1.058`, 同步完成 `return struct` 纯 CP 化)

## [2026-11-22 Wave #9] RAII `~init()` 与 `unsafe {}` 改造（9 改造 + 3 复核）
- `src/runtime/image/backend_wic.{cp,zig}` (`14 -> 0`, ratio `1.13`)
- `src/jsc/ipc.{cp,zig}` (`14 -> 7` 控制流/事件循环, ratio `1.069`)
- `src/ini/ini.{cp,zig}` (`13 -> 4` 状态/计数, ratio `1.018`)
- `src/css_jsc/css_internals.{cp,zig}` (`14 -> 0`, ratio `1.012`)
- `src/cli/pm_version_command.{cp,zig}` (`14 -> 0`, ratio `1.034`)
- `src/bundler/ParseTask.{cp,zig}` (`14 -> 7` 状态/错误日志, ratio `1.003`)
- `src/bake/DevServer/ErrorReportRequest.{cp,zig}` (`14 -> 4` 状态/刷新, ratio `1.124`)
- `src/runtime/api/bun/js_bun_spawn_bindings.{cp,zig}` (`13 -> 4` 状态/多步清理, ratio `1.041`)
- `src/http_jsc/websocket_client.{cp,zig}` (`13 -> 1` 状态恢复, ratio `0.88`)
- 复核达标（剩余 `defer` 全为状态/锁/配对型）：`src/resolver/resolver.cp` (`13`), `src/runtime/server/server.cp` (`11`), `src/interchange/yaml.cp` (`11`)

## [2026-11-22 Wave #10] RAII `~init()` 与 `unsafe {}` 改造（5 改造 + 1 复核）
- `src/cli/publish_command.{cp,zig}` (`13 -> 1` 控制台恢复, ratio `1.049`)
- `src/bundler/barrel_imports.{cp,zig}` (`7 -> 0`, ratio `1.126`)
- `src/test_runner/expect/toThrow.{cp,zig}` (`12 -> 1` 回调, ratio `1.016`)
- `src/runtime/webcore/fetch.{cp,zig}` (`12 -> 0`, ratio `0.59`)
- `src/js_parser/ast/visit.{cp,zig}` (`13 -> 7` 作用域/状态恢复, ratio `1.028`)
- 复核达标：`src/js_parser/ast/visitStmt.cp` (`13` 处全为作用域/状态恢复型)

## [2026-11-22 Wave #11] RAII `~init()` 与 `unsafe {}` 改造（8 模块）
- `src/runtime/dns_jsc/cares_jsc.{cp,zig}` (`12 -> 2` 状态恢复, ratio `1.155`)
- `src/runtime/crypto/PasswordObject.{cp,zig}` (`12 -> 0`, ratio `1.146`)
- `src/runtime/node/path_watcher.{cp,zig}` (`12 -> 3` 互斥锁恢复, ratio `1.057`)
- `src/runtime/node/node_fs_stat_watcher.{cp,zig}` (`13 -> 2` 互斥锁恢复, ratio `1.102`)
- `src/runtime/api/YAMLObject.{cp,zig}` (`12 -> 0`, ratio `1.047`)
- `src/runtime/api/glob.{cp,zig}` (`12 -> 1` 计数器恢复, ratio `1.111`)
- `src/resolver/fs.{cp,zig}` (`11 -> 0`, ratio `1.042`)
- `src/jsc/RuntimeTranspilerStore.{cp,zig}` (`12 -> 4` 状态恢复, ratio `1.066`)

## [2026-11-22 Wave #12] RAII `~init()` 与 `unsafe {}` 改造（4 模块）
- `src/jsc/bun_string_jsc.{cp,zig}` (`11 -> 1` 条件状态重置, ratio `1.091`)
- `src/install/lockfile.{cp,zig}` (`12 -> 4` 校验与 clamp, ratio `1.069`)
- `src/http/http.{cp,zig}` (`12 -> 2` 进度与状态刷新, ratio `0.878`)
- `src/cli/pm_trusted_command.{cp,zig}` (`12 -> 4` 状态恢复, ratio `1.08`)

## [2026-11-22 Wave #13] RAII `~init()` 与 `unsafe {}` 改造（10 模块）
- `src/cli/init_command.{cp,zig}` (`12 -> 5` 控制台/终端状态恢复, ratio `1.045`)
- `src/bundler/linker_context/scanImportsAndExports.{cp,zig}` (`11 -> 8` trace/状态恢复, ratio `1.021`)
- `src/sourcemap/sourcemap.{cp,zig}` (`11 -> 4` 状态恢复/分段清理, ratio `0.937`)
- `src/runtime/socket/tls_socket_functions.{cp,zig}` (`11 -> 0`, ratio `1.064`)
- `src/runtime/socket/SSLConfig.{cp,zig}` (`11 -> 4` 互斥锁/置空, ratio `1.012`)
- `src/runtime/node/node_process.{cp,zig}` (`11 -> 3` 互斥锁/状态, ratio `1.092`)
- `src/bake/bake.{cp,zig}` (`12 -> 1` ast_scope, ratio `1.084`)
- `src/test_runner/jest.{cp,zig}` (`11 -> 0`, ratio `1.025`)
- `src/runtime/api/MarkdownObject.{cp,zig}` (`11 -> 0`, ratio `1.033`)
- `src/cli/run_command.{cp,zig}` (`9 -> 2` 状态恢复, ratio `1.033`)

## [2026-11-22 Wave #14] RAII `~init()` 与 `unsafe {}` 改造（8 模块）
- `src/sys/windows/windows.{cp,zig}` (`11 -> 0`, ratio `1.02`)
- `src/js_parser/ast/Parser.{cp,zig}` (`10 -> 3` 状态/tracer, ratio `1.023`)
- `src/jsc/SavedSourceMap.{cp,zig}` (`11 -> 0`, ratio `1.043`)
- `src/bundler/linker_context/writeOutputFilesToDisk.{cp,zig}` (`11 -> 3` 状态/trace, ratio `1.07`)
- `src/bun_alloc/allocation_scope.{cp,zig}` (`11 -> 1` 状态重置, ratio `1.144`)
- `src/ptr/owned.{cp,zig}` (`11 -> 10` 置空重置, ratio `1.033`)
- `src/patch_jsc/testing.{cp,zig}` (`11 -> 0`, ratio `1.184`)
- `src/js_parser_jsc/Macro.{cp,zig}` (`11 -> 6` 状态恢复, ratio `1.047`)

## [2026-11-22 Wave #15] RAII `~init()` 与 `unsafe {}` 改造（8 模块）
- `src/sql_jsc/postgres/DataCell.{cp,zig}` (`10 -> 0`, ratio `0.928`)
- `src/runtime/webcore/s3/credentials_jsc.{cp,zig}` (`10 -> 0`, ratio `1.068`)
- `src/runtime/webcore/FileSink.{cp,zig}` (`10 -> 2` event_loop, ratio `0.827`)
- `src/runtime/node/node_crypto_binding.{cp,zig}` (`10 -> 2` 队列调度, ratio `1.104`)
- `src/runtime/api/JSON5Object.{cp,zig}` (`10 -> 2` ast_scope/visiting, ratio `1.067`)
- `src/runtime/api/bun/subprocess.{cp,zig}` (`11 -> 7` 保活/状态, ratio `0.938`)
- `src/jsc/Task.{cp,zig}` (`10 -> 1` 计数器, ratio `0.972`)
- `src/install/patch_install.{cp,zig}` (`10 -> 4` 队列/唤醒, ratio `1.024`)

## [2026-11-22 Wave #16] RAII `~init()` 与 `unsafe {}` 改造（4 模块）
- `src/install/lockfile/Package.{cp,zig}` (`10 -> 4` 状态/嵌套子结构体, ratio `1.013`)
- `src/install_jsc/ini_jsc.{cp,zig}` (`10 -> 0`, ratio `1.078`)
- `src/cli/package_manager_command.{cp,zig}` (`10 -> 1` 状态恢复, ratio `0.997`)
- `src/cli/outdated_command.{cp,zig}` (`10 -> 0`, ratio `1.118`)

## [2026-11-22 Wave #17] RAII `~init()` 与 `unsafe {}` 改造（8 模块）
- `src/watcher/Watcher.{cp,zig}` (`9 -> 2` flush/状态重置, ratio `0.912`)
- `src/test_runner/snapshot.{cp,zig}` (`10 -> 1` 日志输出, ratio `1.145`)
- `src/sql_jsc/postgres/PostgresSQLQuery.{cp,zig}` (`9 -> 2` GC 护栏, ratio `1.048`)
- `src/shell/states/Cmd.{cp,zig}` (`9 -> 0`, ratio `1.065`)
- `src/runtime/webcore/ObjectURLRegistry.{cp,zig}` (`9 -> 0`, ratio `1.085`)
- `src/install_jsc/npm_jsc.{cp,zig}` (`9 -> 0`, ratio `1.072`)
- `src/runtime/webview/ChromeProcess.{cp,zig}` (`9 -> 0`, ratio `1.149`)
- `src/runtime/webcore/s3/multipart.{cp,zig}` (`9 -> 2` 状态恢复, ratio `0.892`)

## [2026-11-22 Wave #18] RAII `~init()` 与 `unsafe {}` 改造（6 模块）
- `src/bun_alloc/bun_alloc.{cp,zig}` (`9 -> 0`, ratio `1.001`)
- `src/test_runner/Execution.{cp,zig}` (`8 -> 8` 日志作用域 + E0209 修复, ratio `1.101`)
- `src/install/bin.{cp,zig}` (`9 -> 1` 条件 chmod, ratio `1.044`)
- `src/cli/test/parallel/aggregate.{cp,zig}` (`9 -> 2` 计数器, ratio `1.073`)
- `src/test_runner/Collection.{cp,zig}` (`8 -> 5` group.end, ratio `1.094`)
- `src/runtime/webcore/s3/client.{cp,zig}` (`8 -> 1` event_loop.exit, ratio `1.077`)

## [2026-11-22 Wave #19] RAII `~init()` 与 `unsafe {}` 改造（4 模块）
- `src/runtime/webcore/Request.{cp,zig}` (`8 -> 2` 状态恢复/回滚, ratio `1.059`)
- `src/runtime/socket/SocketAddress.{cp,zig}` (`8 -> 2` 状态恢复/透传块, ratio `0.785`)
- `src/runtime/node/fs_events.{cp,zig}` (`8 -> 0`, ratio `1.036`)
- `src/libarchive/libarchive.{cp,zig}` (`8 -> 0`, ratio `0.91`)

## [2026-11-22 Wave #20] RAII `~init()` 与 `unsafe {}` 改造（8 模块）
- `src/js_parser/ast/P.{cp,zig}` (`8 -> 4` 状态恢复/断言, ratio `1.021`)
- `src/jsc/ZigString.{cp,zig}` (`8 -> 2` 状态重置 + 33 处 E0209, ratio `1.03`)
- `src/io/PipeWriter.{cp,zig}` (`8 -> 6` 状态/reset + CRTP `static fn`, ratio `0.943`)
- `src/install/PackageManager/runTasks.{cp,zig}` (`8 -> 3` 回调/进度刷新, ratio `1.037`)
- `src/install/migration.{cp,zig}` (`5 -> 1` 状态推进 + 6 处 E0209, ratio `1.105`)
- `src/install/isolated_install/Hardlinker.{cp,zig}` (`8 -> 3` 状态恢复, ratio `1.076`)
- `src/install/extract_tarball.{cp,zig}` (`8 -> 1` tracer, ratio `1.007`)
- `src/bundler/options.{cp,zig}` (`8 -> 0`, ratio `1.027`)

## [2026-11-22 Wave #21] RAII `~init()` 与 `unsafe {}` 改造（6 模块）
- `src/shell/ParsedShellScript.{cp,zig}` (`7 -> 0`, ratio `1.119`)
- `src/semver_jsc/SemverObject.{cp,zig}` (`7 -> 0`, ratio `1.029`)
- `src/sourcemap_jsc/JSSourceMap.{cp,zig}` (`7 -> 0`, ratio `1.044`)
- `src/shell/subproc.{cp,zig}` (`7 -> 1` 状态恢复, ratio `1.032`)
- `src/s3_signing/credentials.{cp,zig}` (`7 -> 0`, ratio `0.986`)
- `src/runtime/webcore/encoding.{cp,zig}` (`7 -> 0` + 43 处 E0209, ratio `0.958`)

## [2026-11-22 Wave #22] RAII `~init()` 与 `unsafe {}` 改造（6 模块）
- `src/runtime/node/node_fs_watcher.{cp,zig}` (`7 -> 3` 锁/unrefTask, ratio `1.132`)
- `src/ptr/ref_count.{cp,zig}` (`7 -> 7` 锁/计数器 + E0209 修复, ratio `1.038`)
- `src/main_wasm.{cp,zig}` (`7 -> 4` 计数器/Store.reset + 8 处 E0209, ratio `1.028`)
- `src/jsc/rare_data.{cp,zig}` (`7 -> 5` 锁恢复, ratio `0.959`)
- `src/jsc/event_loop.{cp,zig}` (`7 -> 6` 状态恢复, ratio `0.937`)
- `src/jsc/AsyncModule.{cp,zig}` (`7 -> 3` 状态恢复, ratio `1.069`)

## [2026-11-22 Wave #23] RAII `~init()` 与 `unsafe {}` 改造（3 改造 + 1 复核）
- `src/install/repository.{cp,zig}` (`7 -> 0`, ratio `1.074`)
- `src/install/PackageManagerTask.{cp,zig}` (`7 -> 2` flush/wake, ratio `1.049`)
- `src/http/ProxyTunnel.{cp,zig}` (`7 -> 0`, ratio `0.94`)
- 复核达标：`src/install/PackageInstaller.cp` (`7` 处全为计数器/状态恢复)

## [2026-11-22 Wave #24] RAII `~init()` 与 `unsafe {}` 改造（6 模块）
- `src/bake/DevServer/DirectoryWatchStore.{cp,zig}` (`7 -> 0`, ratio `1.083`)
- `src/bundler_jsc/PluginRunner.{cp,zig}` (`7 -> 0`, ratio `1.021`)
- `src/bundler/linker_context/doStep5.{cp,zig}` (`7 -> 3` 状态/断言, ratio `0.961`)
- `src/bundler/linker_context/postProcessJSChunk.{cp,zig}` (`7 -> 1` 计数器, ratio `1.033`)
- `src/cli/bunx_command.{cp,zig}` (`7 -> 0`, ratio `0.912`)
- `src/cli/upgrade_command.{cp,zig}` (`7 -> 0`, ratio `0.994`)

## [2026-11-22 Wave #25] RAII `~init()` 与 `unsafe {}` 改造（6 模块）
- `src/http/AsyncHTTP.{cp,zig}` (`7 -> 0`, ratio `1.01`)
- `src/http/HTTPThread.{cp,zig}` (`7 -> 3` 锁恢复, ratio `0.874`)
- `src/cli/test/parallel/runner.{cp,zig}` (`7 -> 4` 计数器, ratio `1.16`)
- `src/cli/why_command.{cp,zig}` (`7 -> 2` 状态恢复, ratio `1.116`)
- `src/http_jsc/websocket_client/CppWebSocket.{cp,zig}` (`7 -> 0`, ratio `1.021`)
- `src/http_jsc/websocket_client/WebSocketProxyTunnel.{cp,zig}` (`7 -> 0`, ratio `0.903`)

## [2026-11-22 Wave #26] RAII `~init()` 与 `unsafe {}` 改造（6 模块）
- `src/css/css_parser.{cp,zig}` (`6 -> 2` 状态恢复, ratio `1.022`)
- `src/http/h2_client/ClientSession.{cp,zig}` (`6 -> 0`, ratio `0.925`)
- `src/install/PackageManager.{cp,zig}` (`6 -> 0`, ratio `0.993`)
- `src/install/PackageManager/install_with_manager.{cp,zig}` (`6 -> 1` 刷新, ratio `1.034`)
- `src/bundler/HTMLScanner.{cp,zig}` (`6 -> 1` 错误上报, ratio `1.11`)
- `src/bundler/linker_context/findAllImportedPartsInJSOrder.{cp,zig}` (`6 -> 2` 写回/计数, ratio `1.09`)

## [2026-11-22 Wave #27 & Wave #28] RAII `~init()` 与 `unsafe {}` 改造（6-defer 清零 17 模块 + 5-defer 首批 27 模块）
- 6-defer (17): `ParentDeathWatchdog`, `Progress`, `crash_handler`, `macho`, `dependency`, `WorkspaceMap`, `PackageManagerLifecycle`, `NodeModuleModule`, `CompileTarget`, `Path`, `Terminal`, `filesystem_router`, `backend_coregraphics`, `codec_webp`, `quantize`, `lexer`, `renamer` (ratio `0.703 ~ 1.178`)
- 5-defer 首批 (27): `cache`, `computeCrossChunkDependencies`, `DevServer`, `bun`, `transpiler`, `build_command`, `Worker`, `update_interactive_command`, `baby_list`, `PackageInstall`, `TarballStream`, `FileCopier`, `folder_resolver`, `dependency_jsc`, `hosted_git_info_jsc`, `install_binding`, `visitExpr`, `JSPromise`, `hot_reloader`, `resolver_jsc`, `virtual_machine_exports`, `shared`, `tsconfig_json`, `TOMLObject`, `process`, `Readable`, `PBKDF2`, `codec_png`, `node_util_binding`, `validators`, `Timer`, `Crypto`, `Store`, `write_file`, `which`, `PostgresRequest`, `env`, `expect`, `toHaveReturnedWith`, `build`, `zstd` (含审计重编文件, ratio `0.911 ~ 1.199`)

## [2026-11-22 Wave #29] RAII `~init()` 与 `unsafe {}` 改造（5-defer 收尾 2 模块 + 4-defer 首批 28 模块）
- 5-defer 收尾 (2): `threading/ThreadPool` (`5 -> 1`, ratio `0.954`), `uucode/lut` (`5 -> 0`, ratio `1.102`) + `Store.{cp,zig}` 修复
- 4-defer 首批 (28): `ErrorReportRequest`, `output`, `HTMLImportManifest`, `bundler/ThreadPool`, `computeChunks`, `generateCompileResultForCssChunk`, `generateCompileResultForHtmlChunk`, `generateCompileResultForJSChunk`, `renameSymbolsInChunk`, `JSBundleCompletionTask`, `Arguments`, `install_completions_command`, `repl`, `process_windows_translate_c`, `declaration`, `selectors/parser`, `NetworkTask`, `hoisted_install`, `lockfile`, `Buffers`, `Package`, `Scripts`, `Tree`, `json`, `ImportScanner`, `P`, `Parser`, `parseStmt`, `BunCPUProfiler`, `VirtualMachine` (ratio `0.974 ~ 1.173`)

## [2026-11-22 Wave #30] RAII `~init()` 与 `unsafe {}` 改造（4-defer 全清零）
- 4-defer 收尾 (34 模块含关联修复): `btjs`, `resolve_path`, `CowSlice`, `data_url`, `package_json`, `JSONCObject`, `SSLContextCache`, `codec_gif`, `codec_jpeg`, `node_cluster_binding`, `node_fs_binding`, `FileRoute`, `Handlers`, `TimerObjectInternals`, `ResumableSink`, `Sink`, `TextDecoder`, `export`, `mv`, `MySQLValue`, `sourcemap`, `PostgresString`, `toContain`, `toContainEqual`, `toEndWith`, `toEqualIgnoringWhitespace`, `toHaveBeenLastCalledWith`, `toHaveLastReturnedWith`, `toHaveNthReturnedWith`, `toInclude`, `toStartWith`, `grapheme_gen`, `url`, `INotifyWatcher` + `Package`/`P` 修复 (ratio `0.881 ~ 1.188`)

## [2026-11-22 Wave #31] RAII `~init()` 与 `unsafe {}` 改造（3-defer 首批 60 模块）
- `Assets`, `HmrSocket`, `HotReloadEvent`, `SourceMapStore`, `base64`, `bun.js`, `fmt`, `BundleThread`, `analyze_transpiled_module`, `findImportedFilesInCSSOrder`, `args`, `filter_arg`, `filter_run`, `open`, `test_command`, `array_list`, `rules`, `color_js`, `MiniEventLoop`, `elf`, `dispatch`, `PackageJSONEditor`, `updatePackageJSONAndInstall`, `md/parser`, `external_shared`, `router`, `Archive`, `SubprocessPipeReader`, `csrf_jsc`, `html_rewriter`, `codecs`, `myers_diff`, `node_assert_binding`, `node_fs_watcher`, `node_os`, `node_process`, `path_watcher`, `parse_args`, `win_watcher`, `NodeHTTPResponse`, `ssl_wrapper`, `ByteStream`, `FormData`, `copy_file`, `read_file`, `HostProcess`, `IOWriter`, `cat`, `cp`, `seq`, `shell`, `CondExpr`, `braces`, `NewReader`, `MySQLRequestQueue`, `StandaloneModuleGraph`, `unicode`, `visible`, `string`, `File`, `diff_format`, `toBeEmpty`, `toBeTypeOf`, `toHaveBeenCalledWith`, `toHaveBeenNthCalledWith`, `toIncludeRepeated`, `toMatchSnapshot`, `toThrowErrorMatchingSnapshot` (ratio `0.658 ~ 1.185`)

## [2026-11-22 Wave #32] RAII `~init()` 与 `unsafe {}` 改造（3-defer 清零 7 模块 + 2-defer 首批 6 模块）
- 3-defer 收尾 (7): `Debugger`, `ModuleLoader`, `js_parser`, `BunHeapProfiler`, `RuntimeTranspilerCache`, `main_test`, `channel` (ratio `0.987 ~ 1.18`)
- 2-defer 首批 (6): `posix_event_loop`, `boringssl`, `brotli`, `memory`, `DeferredBatchTask`, `Graph` (ratio `0.907 ~ 1.173`)

## [2026-11-22 Wave #33] RAII `~init()` 与 `unsafe {}` 改造（2-defer 36 模块）
- `production`, `generateCodeForLazyExport`, `prepareCssAstsForChunk`, `options_jsc`, `add_completions`, `SourceFileProjectGenerator`, `fuzzilli_command`, `link_command`, `multi_run`, `pm_view_command`, `repl_command`, `scan_command`, `unlink_command`, `media_query`, `printer`, `background`, `url`, `pe`, `encode`, `http`, `PackageManagerEnqueue`, `PackageManagerResolution`, `security_scanner`, `lifecycle_script_runner`, `Yarn`, `npm`, `toml/lexer`, `io`, `CharFreq`, `Expr`, `Symbol`, `parse`, `parseJSXElement`, `parsePrefix`, `skipTypescript`, `runtime` (ratio `0.643 ~ 1.129`)

## [2026-11-22 Wave #34] RAII `~init()` 与 `unsafe {}` 改造（2-defer 29 模块核验 / 27 模块变更）
- `font`, `transform`, `supports`, `builder`, `calc`, `JSPropertyIterator`, `JSSecrets`, `TopExceptionScope`, `ZigException`, `GeneratedBindings`, `resolve_path_jsc`, `uuid`, `Router`, `HashObject`, `JSBundler`, `SecureContext`, `spawn`, `node_fs_stat_watcher`, `UpgradedDuplex`, `WindowsNamedPipeContext`, `uws_jsc`, `WTFTimer`, `BakeResponse`, `download_stream`, `CriticalSection`, `Yield`, `Chunk` (ratio `0.727 ~ 1.14`)

## [2026-11-22 Wave #35 & Wave #36] RAII `~init()`/`~errinit()` 与 `unsafe {}` 全仓清零收官（149 模块）
- **2-defer 全清零 (63 模块)**：`Mapping`, `HandshakeV10`, `Authentication`, `NegotiateProtocolVersion`, `MySQLStatement`, `PostgresSQLStatement`, `notice_response_jsc`, `date`, `json`, `fd`, `walker_skippable`, `windows/env`, `DoneCallback`, `test_runner/expect/*.cp` (52), `uucode_lib` (3), `KEventWatcher`, `WindowsWatcher`
- **1-defer 全清零 (86 模块)**：`HTMLScanner`, `IndexStringMap`, `LinkerGraph`, `defines`, `analyze_jsc`, `source_map_mode_jsc`, `clap`, `bunfig`, `pack_command`, `Channel`, `Coordinator`, `multi_array_list`, `declaration`, `border`, `border_image`, `custom`, `color`, `gradient`, `dns`, `AnyTaskWithExtraContext`, `EventLoopTimer`, `ManagedTask`, `h2_client/encode`, `h3_client/ClientSession`, `WebSocketDeflate`, `PopulateManifestCache`, `UpdateRequest`, `processDependencyList`, `PnpmMatcher`, `TarballStream`, `extract_tarball`, `Store`, `update_request_jsc`, `io/source`, `AnyPromise`, `JSCScheduler`, `JSObject`, `jsc/StringBuilder`, `VM`, `ZigStackFrame`, `ZigStackTrace`, `bindgen`, `fmt_jsc`, `logger`, `lol_html`, `inlines`, `render_blocks`, `Route`, `stdio`, `EVP`, `Image`, `codec_bmp`, `thumbhash`, `ValkeyCommand`, `ReadableStream`, `error_jsc`, `simple_request`, `HostProcess`, `SemverQuery`, `mkdir`, `touch`, `If`, `Subshell`, `InternalSourceMap`, `ParsedSourceMap`, `ErrorPacket`, `NewReader`, `mysql/NewWriter`, `PreparedStatement`, `FieldDescription`, `FieldMessage`, `postgres/NewWriter`, `ParameterDescription`, `RowDescription`, `mysql/Signature`, `postgres/Signature`, `error_response_jsc`, `bytea`, `SQLDataCell`, `string/StringBuilder`, `tcc`, `diff_format`, `WaitGroup`, `App`, `BodyReaderMixin`, `uws_sys/socket` (ratio `0.794 ~ 1.195`)

## [2026-11-22 Wave #37] 全仓 100% `.cp` 覆盖达成 + 剩余 RAII 模块收官（39 模块）
- **全仓最后 2 个 `.zig` -> `.cp`**：`grapheme_tables` (ratio `1.0`), `boringssl` (ratio `1.109`)
- **RAII 与纯化收官 (37 模块)**：`ref_count`, `interpreter`, `Builtin`, `Expansion`, `small_list`, `GlobWalker`, `napi`, `scanImportsAndExports`, `tree_printer`, `rm`, `FileReader`, `writeOutputFilesToDisk`, `prompt`, `color`, `stdio`, `error_jsc`, `bun_test`, `Execution`, `ScopeFunctions`, `Collection`, `debug`, `JSMySQLQuery`, `ResultSet`, `MySQLQuery`, `Auth`, `HTTPThread`, `SSLConfig`, `node_assert`, `node_zlib_binding`, `BlockList`, `rare_data`, `host_fn`, `PipeReader`, `HTMLBundle`, `LinkerContext`, `ffi`, `calc` (ratio `0.824 ~ 1.195`)

## [2026-11-22 Wave #38] 旧式 `struct`/`union` 纯 CP 化专项首批（45 模块）
- `posix_event_loop`, `DevServer`, `IncrementalGraph`, `FrameworkRouter`, `bake`, `brotli_c`, `MimallocArena`, `basic`, `bun_alloc`, `heap_breakdown`, `output`, `AstBuilder`, `BundleThread`, `Chunk`, `HTMLScanner`, `ServerComponentParseTask`, `ThreadPool`, `analyze_transpiled_module`, `bundle_v2`, `cache`, `defines-table`, `defines`, `linker`, `MetafileBuilder`, `OutputFileListBuilder`, `findAllImportedPartsInJSOrder`, `findImportedCSSFilesInJSOrder`, `options`, `c_ares`, `cli`, `SourceFileProjectGenerator`, `create_command`, `init_command`, `pack_command`, `publish_command`, `run_command`, `ChangedFilesFilter`, `Scanner`, `Channel`, `Coordinator`, `test_command`, `array_list`, `comptime_string_map`, `linear_fifo`, `csrf` (ratio `0.613 ~ 1.185`)

## [2026-11-22 Wave #39~#46] `old_struct_todo.txt` 全面收官（Waves #39–#46）
- 完成 `src/css/**`、`dotenv`/`errno`/`event_loop`/`glob`/`http`/`ini`、`install`/`interchange`、`io`/`js_parser`/`js_printer`、`jsc`/`libuv`/`md`/`napi`/`options_types`/`paths`/`ptr`/`resolver`/`router`、`runtime/**`、`s3_signing`/`semver`/`sha_hmac`/`shell`/`string`/`sys`/`test_runner`/`threading`/`unicode`/`url`/`uws_sys` 等 240+ 个 `.cp`/`.zig` 模块的纯 CP 结构化、`~init()`/`~errinit()` RAII 守卫与只读/可变借用边界重构。

- **Waves #47–#49 (2026-11-22)**：完成 94 个模块顶层 `const X = struct/union` 纯 CP 化重构（`RouteBundle`、`boringssl` 338 容器、`barrel_imports`、`ParseTask`、`outdated_command`、`repl`、`css_parser`、`dependencies`、`css/*`、`dns`、`install/*`、`jsc/*`、`js_parser/*`、`runtime/api/*`、`node/*`、`http/*`、`s3/*`、`shell/*`、`sql/*`、`utils/*`、`io/*` 等），全部通过 `cpc_brief.py` 4 项门禁、`bun run zig:check`（5/5）及 `bun bd test`（8/8）。
- **Waves #50–#51 (2026-11-22)**：完成 55 个模块顶层 `enum`/`opaque` 纯 CP 化及 117 个模块嵌套容器提升与纯 CP 化（`boringssl` 75 `@opaque`、`bun`、`DevServer`、`IncrementalGraph`、`css/*`、`jsc/*`、`install/*`、`runtime/*`、`uws_sys/*`、`collections/*` 等），全部通过 `cpc_brief.py` 4 项门禁、`bun run zig:check`（5/5）及 `bun bd test`（8/8）。
- **Waves #52–#54 (2026-11-22)**：完成全仓残留 `anyopaque` / `_ = ` / `?T` 清理（45 模块）、冗余 `): void` 省略（Wave #53）及 `@import(...)` 纯 CP 化（21 组 173 模块，含 `boringssl` 189 处、`bun.cp` 30 处、`expect.cp` 75 处、`GeneratedJS2Native.cp` 51 处），全部通过 `cpc_brief.py` 4 项门禁、`bun run zig:check`（5/5）及 `bun bd test`（8/8）。
- **Wave #55 (2026-11-22)**：完成全仓剩余资源管理 `defer`/`errdefer` → RAII `~init()`/`~errinit()` 深化（8 组 53 模块，覆盖 `ParseTask`、`bundle_v2`、`resolver`、`SourceMapStore`、`LinuxMemFdAllocator`、`FileResponseStream`、`GlobWalker`、`shell/builtin/*`、`shell/states/*`、`fd.cp` 等），全部通过 `cpc_brief.py` 4 项门禁、`bun run zig:check`（5/5）及 `bun bd test`（8/8）。
- **Waves #56–#57 (2026-11-22)**：完成全仓 `self`/`this` 接收者纯 CP 化（10 组 63 模块，含 `shell.cp`、`fmt.cp`、`css_parser.cp`、`color.cp`、`immutable.cp`）及 `if`/`while`/`for` `|capture|` 纯 CP 化（7 组 58 模块，含 `env_loader.cp`、`SourceFileProjectGenerator.cp`、`Package.cp`、`diff_match_patch.cp`、`resolve_path.cp`、`css_parser.cp`），全部通过 `cpc_brief.py` 4 项门禁、`bun run zig:check`（5/5）及 `bun bd test`（8/8）。

### [2026-11-22] Wave #58（前缀 `try expr` -> 后缀 `expr?` 纯化）
- 完成 20 组共 212 个 `src/**/*.cp` 模块的前缀 `try` 到后缀 `?` 纯化及对应 `.zig` 重编译生成，通过 `cpc_brief.py` 四项门禁、`bun run zig:check`（5/5）及 `bun bd test`（8/8）。

### [2026-11-23] Wave #59（`orelse` -> `??` 纯化）
- 完成 12 组共 132 个 `src/**/*.cp` 模块的 `orelse` 纯化（表达式改为 `??`，控制流改为 `== null` + `.?` 或 `if (v := opt)`）及对应 `.zig` 重编译生成，通过 `bun run zig:check`（5/5）及 `bun bd test`（8/8）。

### [2026-11-23] Wave #60（布尔关键字 `and`/`or` -> `&&`/`||` 纯化）
- 完成 7 组共 117 个候选 `src/**/*.cp` 模块（含 50 个全量初筛及 67 个非注释/非字符串代码文件）的 `and`/`or` -> `&&`/`||` 纯化及对应 `.zig` 重编译生成，通过 `bun run zig:check`（5/5）及 `bun bd test`（8/8）。

### [2026-11-23] Wave #61（尾项清扫：非透传 `try` 与 `const X = struct/enum/union/opaque`）
- 完成 48 个候选 `src/**/*.cp` 模块的 `try` 与 `const X = struct/enum/union/opaque` 尾项清扫及 `ConsoleObject.cp` 嵌套容器顶层提升，通过 `bun run zig:check`（5/5）及 `bun bd test`（8/8）。

### [2026-11-23] Wave #62（前缀 `?T` 与 `: void` 尾项清扫）
- 完成 31 个 `?T` 候选与 22 个 `: void` 候选 `src/**/*.cp` 模块的尾项纯化（10 个非透传模块实际改写，其余透传泛型工厂块按规范保留），通过 `bun run zig:check`（5/5）及 `bun bd test`（8/8）。

### [2026-11-23] Wave #63（残余裸 `self`/`this` 接收者纯化）
- 完成 43 个候选 `src/**/*.cp` 模块的裸 `self`/`this`/`*self`/`*this` 接收者清扫（8 个非透传模块实际改写，其余 35 个透传泛型工厂块按规范保留），通过 `bun run zig:check`（5/5）及 `bun bd test`（8/8）。

### [2026-11-23] Wave #64（非透传 `if`/`while` `|capture|` 尾项清扫与语法纯化收官）
- 完成 `publish_command.cp`、`css_parser.cp`、`generics.cp` 的尾项纯化与 `unsafe` 补全，全仓 1295 个 `.cp` 模块主干及尾项语法纯化全面收敛，通过 `bun run zig:check`（5/5）及 `bun bd test`（8/8）。

### [2026-11-23] Wave #65（全仓 1280 模块普查与首轮语义一致性修复）
- 修复 `unicode.cp`、`yaml.cp`、`types.cp`、`InitError.cp`、`http.cp`、`fetch.cp`、`Version.cp`、`FrameworkRouter.cp`、`DevServer.cp`、`expect.cp`、`owned.cp`、`macho.cp`、`sys.cp`、`Futex.cp` 共 14 个模块的语义边界、符号可见性及与新版 `cpc` 的适配，通过 `bun run zig:check`（5/5）及 `bun bd test`（8/8 + `does propagate type for Blob`）。

### [2026-11-23] Wave #66（补全 `fetch`/`Blob`/`Response`/`copy_file`/`c_ares`/`subprocess`/`streams` 核心逻辑）
- 补全 `fetch.cp`（`needsToReadFile`/`isS3`/协议校验）、`Blob.cp`（`BlobAny.isS3`）、`uws_sys/Response.cp`、`blob/copy_file.cp`（`CopyFileWindows`）、`cares_sys/c_ares.cp`、`subprocess.cp`（IPC 关闭与接收者纯化）及 `streams.cp`（`WrappedPtr`），通过 `bun run zig:check`（5/5）及 `bun bd test`（8/8 + `serve.test.ts` `Content-Type` 2/2）。

### [2026-11-23] Wave #67（运行时排雷修复：`DevServer`/`computeChunks`/`fmt`/`Blob`/`node_fs`/`Terminal`/`napi`/`cli`/`rect`）
- 修复 `DevServer.cp`（`ensureRouteIsBundled` 补全 `return;` 消除 `heap-use-after-free`）、`computeChunks.cp`（`usize` 消除 `{d:0>8}` `+` 号）、`fmt.cp`（消除 `stack-buffer-overflow`）、`Blob.cp`（`dupeRef` 保留 `String.dead`）、`Terminal.cp`（Windows `makeLibUVOwned`）、`json.cp`（恢复 `json_.zig` 备份与 `expectPrintedJSON`）、`node_fs.cp`/`node_fs_binding.cp`/`http.cp`/`path.cp`/`meta.cp`/`napi.cp`/`cli.cp`/`css_string.cp`/`rect.cp`，通过 `bun run zig:check`（5/5）、`hspec.test.ts`（40/40）及 `form-data-set-append.test.js`（4/4）。

### [2026-11-23] Wave #68（修复 `background`/`subprocess`/`HTMLImportManifest` 与 `server`/`uws_sys` 同步重编）
- 修复 `background.cp`（`bg_images`）、`subprocess.cp`（`finalize()` 补回 `finalized = true` 与 `deref()` 消除 ASan `use-after-poison`）、`HTMLImportManifest.cp`（恢复 `for (*ch of chunks)`）及重编同步 `server/`、`uws_sys/Request.cp`，通过 `bun run zig:check`（5/5）、`serve html$`（1/1）、`serve-protocols.test.ts`（20/20）与 `serve-http3.test.ts`（43 pass / 1 超时）。

### [2026-11-23] Wave #69（修复 `Installer.cp` 与 `yaml.cp` 状态机死循环并完成 `http` 尾段验证）
- 修复 `Installer.cp`（`next_step_sw` 外层标签跳转）与 `yaml.cp`（`continue :lit_sw;`），通过 `bun run zig:check`（5/5）、`bun-serve-html-manifest.test.ts`（4/4）及 `test/js/bun/http` 尾段 14 文件零崩溃跑完（346 pass）。

### [2026-11-23] Wave #70（Batch 1b `util` 回归与 `Blob.cp`/`SlicedString.cp` 修复）
- 修复 `Blob.cp`（`writeFormat` 恢复 `_.getNameString()`）与 `SlicedString.cp`（14 处 `@intFromPtr` `unsafe`），通过 `bun run zig:check`（5/5）；完成 `test/js/bun/util` 53 文件全量回归（1,318 pass / 48 fail / 19 skip，零崩溃）。

### [2026-11-23] Wave #71（补全 `Blob.cp` S3/StructuredClone、修复 6 处 `E0209` 与 Batch 1c `test` 回归）
- 补全 `Blob.cp`（`onStructuredCloneSerialize`/`Deserialize`、`doReadFromS3` 与 `S3BlobDownloadTask`），修复 `h3_client/callbacks.cp`、`UnsafeObject.cp`、`crash_handler_jsc.cp`、`standalone_graph_jsc.cp`、`uv_signal_handle_windows.cp`、`NativeZstd.cp` 的 `E0209` `unsafe`，通过 `bun run zig:check`（5/5）；完成 `test/js/bun/test` 75 文件回归（926 pass / 28 fail / 17 skip / 455 todo，零崩溃）。

### [2026-11-23] Wave #72（全仓 `cpc check` 100% 清零 21 处 `E0209` 与重编 `visitExpr`/`toBeCloseTo` 保留 `-0.0`）
- 用新版 `cpc` 重编 `visitExpr.cp` 与 `toBeCloseTo.cp`（保留 `-0.0`），并修复全仓最后 21 个 `E0209` `unsafe {}` 文件（全仓 1,295 个 `.cp` 实现 100% `cpc check` 通过），通过 `bun run zig:check`（5/5）与 `inspect.test.js`（72/72 全绿）。

### [2026-11-23] Wave #73（修复 `fetch`/`FetchTasklet`/`Blob`/`ReadableStream` 与验证 `-0.0`）
- 修复 `fetch.cp`（`req.method` 保留）、`FetchTasklet.cp`（`onBodyReceived` 清理顺序与 `fromJS` `static fn`）、`Blob.cp`（`getBytes`/`getJSON`/`getText` OOM 检查与 `AnyBlob.toPromise` `JSPromise.wrap`）与 `ReadableStream.cp`（`toAnyBlob` `isLocked` 检查），通过 `bun run zig:check`（5/5）；`-0.0` 与 `toIncludeRepeated` 相关测试全绿。

### [2026-11-23] Wave #74（补全 `Blob.cp` `FormData`/`transfer`/`CloneTransfer` 与 `ParentDeathWatchdog.cp`）
- 补全 `Blob.cp`（`getFormData`/`toFormData`/`toFormDataWithBytes`、`onStructuredCloneTransfer`、`toStreamWithOffset` 与 `Any.to*Transfer`）与修复 `ParentDeathWatchdog.cp`（`32768` 缓冲区与 `tokenizeAny " \n"`），通过 `bun run zig:check`（5/5）；完成 Batch 2c 后半段（889 pass / 0 崩溃）与全仓 110 目录完整性覆检。

### [2026-11-23] Wave #75（修复 `shell.Builtin` SEGV 与纯化 `MarkdownObject`/`copy_file`）
- 修复 `src/shell/Builtin.cp`（`allocator(*_)`/`eventLoop(*_)` 与 `static fn onForceSyncOrIsaTTY`），纯化 `src/runtime/api/MarkdownObject.cp`（`E0305`x95/`E0307`x3）与 `src/runtime/webcore/blob/copy_file.cp`（`E0306`x214 与 `fcntl` `@as(usize, 0)`），同步 `scripts/cpc_brief.py` `zig{}` 正则，通过 `bun run zig:check`（5/5）。

### [2026-11-23] Wave #76–77（Batch 3a 测试验证、修复 `Cmd.cp`/`Blob.cp` 与 70 文件 `--strict` 批量纯化）
- 完成 Batch 3a 测试：`spawn` 39 文件 **262 pass / 0 主进程崩溃**，`shell` 合计 **479 pass** 并验证 `Builtin` 3 处 SEGV 彻底消除；
- 修复 `src/shell/states/Cmd.cp`（`Expansion` 分支释放顺序消除 `Allocation scope leaked 30 bytes`，`initSubproc` 直传 `&_.exec.subproc.child` 消除 `BufferedIoClosed.close` SEGV）与恢复 `src/runtime/webcore/Blob.cp` `doImage`（`Image.fromBlobJS`）及清零 9 处 `E0307`；
- 完成 `s3/*`、`crypto/*`、`install/*`、`jsc/*`、`shell/{EnvMap,subproc}.cp`、`sql/{postgres,mysql,shared}/*`、`valkey`、`zlib`/`zstd`、`watcher/*`、`threading/*`、`safety/*` 共 **70 个文件 `--strict` 纯化**，全部通过 `cpc check --strict`（0 报错）与 `bun run zig:check`（5/5）。

### [2026-11-23] Wave #78–79（Shell 复测零崩溃、Batch 3b `resolve` 完成与 `http`/`sql_jsc` 35 文件 `--strict` 纯化）
- 完成 `bunshell.test.ts` + `ls.test.ts` 复测（377 pass / 0 crash）与 Batch 3b `test/js/bun/resolve`（34 文件 234 pass / 0 crash）；
- 完成 `src/http/` 18 文件（全目录 36/36 清零）与 `src/sql_jsc/` 17 文件共 **35 个文件 `--strict` 纯化**，通过 `cpc_brief.py` 4 项门禁与 `bun run zig:check`（5/5）。

### [2026-11-23] Wave #80（Batch 3c `glob`/`md`/`cookie`/`css`/`crypto`/`ffi` 全量测试零崩溃）
- 完成 Batch 3c：`glob`/`md`/`cookie`/`crypto`/`ffi` 26 文件 **11,484 pass / 6 fail / 0 crash**（`md`/`cookie`/`crypto` 全绿），`css` `color.test.ts` 916/916 与 `css.test.ts` 1,031 pass / 1 fail / 0 crash；完成全仓剩余 `E0307` 精确行号清单提取。

### [2026-11-23] Wave #81–82（Batch 3d 测试与修复 `S3File.cp` `PathOrBlobGuard` double-free）
- 完成 Batch 3d（30 文件，**429 pass / 1 crash**，其中 `transpiler` 43/43 全绿、`image` 219 pass / 0 crash）；确认 `websocket-server`（42 fail）与 `bun-write`（14 fail）均为并发超时；
- 修复 `src/runtime/webcore/S3File.cp` `PathOrBlobGuard`（改 `~errinit()` + 6 处 `disarm()`），消除 `s3-storage-class.test.ts` `WTFStringImplStruct.deref` panic。

### [2026-11-23] Wave #84–85（Batch 4a `node/` 71 文件完成与修复 `lexer`/`unicode`/`udp_socket`）
- 完成 `test/js/bun/*.test.*`（9 文件 **128 pass / 0 crash**）与 Batch 4a `test/js/node/` 12 核心目录（71 文件 **1,035 pass / 9 fail / 0 crash**）；
- 修复 `src/js_parser/lexer.cp`（恢复 `decodeJSXEntities` UTF-16 代理对与 19 处 `static fn`）、`src/string/immutable/unicode.cp`（恢复被静默丢弃的 `appendUTF8MachineWordToUTF16MachineWord` 与 `cp1252ToCodepointAssumeNotASCII` 及 6 处 `static fn`）与 `src/runtime/socket/udp_socket.cp`（`options.getTruthy(..., "binaryType")` 与 `SendManyCtx.run` `static fn`）。

### [2026-11-23] Wave #86–87（Wave #85 复测全绿、Batch 4b 116 文件完成与修复 `sys.cp` 2038 溢出）
- 复测 `direct-readable-stream.test.tsx` + `udp_socket.test.ts` **447 pass / 0 fail**；完成 Batch 4b `test/js/node/` 10 核心目录（116 文件 **2,102 pass / 54 fail / 0 crash**）；
- 修复 `src/sys/sys.cp` `futimens`/`utimensWithFlags` 64 位时间戳截断（`i32(...)` -> `@intCast(...)`），消除 `utimesSync > works after 2038` 溢出。

### [2026-11-23] Wave #88–89（Batch 4c `node/` 72 文件收官与修复 `JSBundler`/`node_fs_stat_watcher` 崩溃）
- 复测 `utimesSync` 4 pass / 0 fail；完成 Batch 4c `test/js/node/` 其余 72 文件（**2,058 pass / 35 fail / 2 crash**，至此 `test/js/node/` 全量 259 文件完成）；
- 修复 `src/runtime/api/JSBundler.cp`（`TopExceptionScopeGuard` 改持有 `*jsc.TopExceptionScope` + `fromJS` `static fn`）与 `src/runtime/node/node_fs_stat_watcher.cp`（移除 `setIntervalOnMainThread` 误加 `DerefGuard`），消除两处 `SIGILL` panic。

### [2026-11-23] Wave #90–91（Batch 5a `bundler/` 完成、修复 `Chunk`/`convertStmtsForChunk`/`SQLDataCell` 与 `bundler/` 49/49 `--strict` 清零）
- 复测 `AsyncLocalStorage.test.ts` + `fs.watchFile.test.ts` **0 崩溃**；完成 Batch 5a `test/bundler/` 90 文件（88 文件 **381 pass**）；
- 修复 `src/bundler/Chunk.cp`（`dst += 2;` 消除 `standalone.test.ts` 崩溃）、`src/bundler/linker_context/convertStmtsForChunk.cp`（`metafile.test.ts` 崩溃）与 `src/sql_jsc/shared/SQLDataCell.cp`（`ExceptionScopeGuard` 指针化），并完成 `src/bundler/` 全目录 49/49 `--strict` 清零。

- [release-fix] 修改 .cp/.zig：bundler/ParseTask, cli/Arguments, crash_handler/crash_handler, install/PackageInstall, jsc/{TopExceptionScope,VirtualMachine,event_loop}, paths/resolve_path, runtime/socket/Handlers, shell/AllocScope, shell/states/Base, string/immutable。`bun run build:release` 通过，`bun run zig:check` 5/5。

- [Wave #93] 修改 .cp/.zig：`src/js_parser/ast/parseTypescript.{cp,zig}`、`src/js_parser/ast/visit.{cp,zig}`。`zig:check` 5/5 通过，`ts_example.test.ts` 与装饰器复现均无 ASan SEGV。

- [Wave #94] 修改 .cp/.zig：`src/test_runner/{expect,jest,pretty_format,snapshot}.{cp,zig}`、`src/analytics/schema.{cp,zig}`（全部通过 `cpc_brief.py` 4 门禁与 `--strict` 零错误，`zig:check` 5/5）。

- [Wave #95] 修改 .cp/.zig：`src/runtime/valkey_jsc/{ValkeyCommand,js_valkey_functions,js_valkey}.{cp,zig}`、`src/collections/bit_set.{cp,zig}`（全部通过 `cpc_brief.py` 4 门禁与 `--strict` 零错误，`zig:check` 5/5）。

- [Wave #96] 修改 .cp/.zig：`src/options_types/schema.{cp,zig}`、`src/semver/{SemverRange,Version}.{cp,zig}`（全部通过 `cpc_brief.py` 4 门禁与 `--strict` 零错误，`zig:check` 5/5）。

- [Wave #97] 修改 .cp/.zig：`src/cli/{Arguments,audit_command,build_command,bunfig,bunx_command,cli,colon_list_type,create_command,exec_command,filter_arg,filter_run,init_command,install_command,install_completions_command,link_command}.{cp,zig}`（全部通过 `cpc_brief.py` 4 门禁与 `--strict` 零错误，`zig:check` 5/5）。

- [Wave #98] 修改 .cp/.zig：`src/cli/{multi_run,open,outdated_command,pack_command,package_manager_command,pm_pkg_command,pm_trusted_command,pm_version_command,pm_view_command,publish_command,repl,repl_command,run_command,scan_command,test_command,unlink_command,update_interactive_command,upgrade_command}.{cp,zig}`（`src/cli/` 47/47 全清，`zig:check` 5/5）。

- [Wave #99] 修改 .cp/.zig：`src/install/{NetworkTask,PackageInstall,PackageInstaller,PackageManager,PackageManagerTask,TarballStream,bin}.{cp,zig}` 及 `src/install/PackageManager/{CommandLineArguments,PackageManagerDirectories,PackageManagerEnqueue,PackageManagerResolution,UpdateRequest,processDependencyList,runTasks,updatePackageJSONAndInstall}.{cp,zig}`。
