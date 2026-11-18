# Bun.js 1.3.14 → 100% 纯 CP 语言重构工作交接文档 (PROCESS.md)

> **文档目的**：本文件供全新 AGY 会话启动时第 0 秒无缝接续所有开发工作，彻底消除历史上下文负担，实现 0 Token 浪费与高效推进。

---

## 一、 当前阻塞与未完成项

| 文件路径                                          | 状态           | 关键未完成项 / 报错诊断                                                                                                                                                                                                                                                     |
| :------------------------------------------------ | :------------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`src/runtime/api/html_rewriter.cp`** (2,031 行) | **100% PASS**  | 4 项硬门禁全部通过（膨胀率 0.998x，ast-check 0 报错），待执行 Git 原子提交固化。                                                                                                                                                                                            |
| **`src/runtime/api/BunObject.cp`** (2,216 行)     | **100% PASS**  | 4 项硬门禁全部通过（膨胀率 1.018x，ast-check 0 报错），待执行 Git 原子提交固化。                                                                                                                                                                                            |
| **`src/string/immutable/unicode.cp`** (2,058 行)  | **待微调闭环** | `cpc check` 0 报错。`ast-check` 仅剩 1 处报错：<br>`src/string/immutable/unicode.zig:1505:18: error: use of undeclared identifier 'val'`<br>根因：2023 行 `decodeWTF8RuneTMultibyte` 函数内部局部变量 `val` 的 lowering 遗漏，改写为直接返回表达式即可 100% 达成 4 项门禁。 |
| **`src/runtime/api/JSBundler.zig`** (2,050 行)    | **待启动**     | 已规划为下一批并发任务的目标，待预处理后分配子智能体。                                                                                                                                                                                                                      |
| **`src/runtime/node/node_fs.cp`** (7,334 行)      | **已生成初稿** | 已用 `zig2cp_prep.py` 生成初稿并保留 `node_fs_.zig` 备份，待拆解迁移。                                                                                                                                                                                                      |

---

## Bun -> CP 现代化代码迁移避坑与最佳实践指南 (v2.2)

> 核心铁律：CP 语法风格为「TS 的优雅 + Rust 的安全 + Zig 的底层控制」。
> 绝对严禁 Zig 残留语法（如 var、|e|、@as、\*\*），严禁使用胶水迂回代码。

---

### 规则一：变量声明（Top 1 禁手：彻底废除 `var`）

- ❌ **绝对禁手**：
  - `var x: i32 = 0;` （CP 语法中不存在 `var` 关键字，写 `var` 触发直接报错）
- ✅ **纯正 CP 规范**：
  - 不可变常量统一使用：`const x = 10;`
  - 可变变量 100% 统一使用：`let x = 10;` （后续允许 `x = 20;`）

---

### 规则二：数值范围模式匹配（严禁使用古董 Zig `...`）

- ❌ **Zig 残留写法**：
  - `switch (r) { 0...0x7F => ... }` （`...` 在 CP 中是展开/剩余参数操作符，不可用于范围）
- ✅ **纯正 CP 写法**：
  - **闭区间（包含左右端点）**：使用 `..=` 运算符
    ```typescript
    switch (r) {
        0..=0x7F => { /* [0, 127] */ },
        0x80..=0x7FF => { /* [128, 2047] */ },
        else => {},
    }
    ```
  - **半开区间（不含右端点）**：使用 `..` 运算符
    ```typescript
    switch (r) {
        0..0x80 => { /* [0, 128) */ },
        else => {},
    }
    ```

---

### 规则三：错误处理、降级与分发（严禁 Zig 管道 `|e|`）

- ❌ **Zig 残留与丑陋迂回**：
  - `res catch |e| switch (e) { ... }` （禁手清单 #18：CP 彻底废除 Zig 管道捕获 `|e|`）
  - `if (e == error.A) ... else if (e == error.B) ...` （误以为不支持 switch 的冗长迂回）
- ✅ **纯正 CP 写法**：
  - **箭头形参捕获与 switch 分发**：
    ```typescript
    res catch e => switch (e) {
        error.FileNotFound => return null,
        error.AccessDenied => return false,
        else => return e,
    };
    ```
  - **万能降级符 `?`（遇错直接返回默认值或发散块）**：

    ```typescript
    // 1. 降级为默认值
    let val = parsePort(s) ? 8080;

    // 2. 降级为控制流块
    let file = openFile(path) ? {
        print("打开文件失败");
        return null;
    };

    // 3. 错误直接向上传播冒泡
    let data = readFile(path)?;
    ```

  - **空值合并 `??` 与 `??=`**：
    ```typescript
    let name = user.nickname ?? "Anonymous";
    config.timeout ??= 5000;
    ```

---

### 规则四：类型转换与数组字面量（严禁 Zig `@as()`、`**` 与顶层打补丁）

- ❌ **Zig 残留与丑陋迂回**：
  - `[ bytes[0], @as(u8, 0), 0, 0 ]` （`@as` 是 Zig 底层指令，非 CP 语法）
  - `var arr = [0] ** 4;` （`var` 与 `**` 均为 Zig 语法）
  - 在顶层声明 `const z: u8 = 0;` 然后拼凑 `[ bytes[0], z, z, z ]` （消极打补丁）
- ✅ **纯正 CP 写法**：
  - **方式 A（函数式构造，最推荐）**：
    ```typescript
    const arr = [bytes[0], u8(0), u8(0), u8(0)];
    ```
  - **方式 B（目标类型向下推导）**：
    ```typescript
    const arr: [4]u8 = [ bytes[0], 0, 0, 0 ]; // 编译器自动将字面量 0 收敛为 u8
    ```
  - **方式 C（运算符风格）**：
    ```typescript
    const arr = [bytes[0], 0 as u8, 0 as u8, 0 as u8];
    ```
  - **方式 D（对齐 Rust 的定长填充 `[val; count]` + `let`）**：
    ```typescript
    let arr: [4]u8 = [0; 4];
    arr[0] = bytes[0];
    ```

---

### 规则五：面向对象与方法接收者契约（严禁 `static fn` 带实例接收者）

- ❌ **概念混淆写法**：
  - `pub static fn update(*_, val: i32)` （概念自相矛盾：静态函数不可携带接收者）
- ✅ **纯正 CP 契约规范**：
  1. **静态工具/工厂方法**：使用 `static fn`，**首参无接收者**，通过 `TypeName.func()` 调用：
     ```typescript
     pub static fn create(): MyStruct {
         return MyStruct{};
     }
     ```
  2. **只读实例方法**：使用 `pub fn`，**首参直接省略**，体内通过 `_`（或直接使用字段名）访问：
     ```typescript
     pub fn getLength(): usize {
         return _.len;
     }
     ```
  3. **可变实例方法**：使用 `pub fn`，**首参显式声明 `*_`**，体内允许变异自身：
     ```typescript
     pub fn push(*_, val: u8) {
         _.buf[_.len] = val;
         _.len += 1;
     }
     ```

---

### 规则六：空切片与类型别名（`[]` 是一等公民）

- ❌ **荒谬迂回写法**：
  - `let empty: [0]T = []; return empty[0..0];` （脱裤子放屁式提取）
  - `const string = []const u8;` （误用 const 声明类型，导致类型遮蔽）
- ✅ **纯正 CP 写法**：
  1. **返回空切片**：直接返回 `[]`，编译器原生下沉为高效空切片：
     ```typescript
     return []; // 适用任何 []T 切片返回
     ```
  2. **返回空字符串切片**：直接返回 `""` 或 `[]`；
  3. **类型别名**：必须使用 TS 风格的 `type` 关键字：
     ```typescript
     type ByteSlice = []const u8;
     type Handler = fn(i32): void;
     ```
     > 注意：`string` 是 CP 原生内置关键字（等价于 `[]const u8`），严禁重复声明 `type string = ...`。

---

### 规则七：保留关键字避让与原生转义

- ❌ **认知误区**：
  - 误以为 `codepoint` 是关键字（实际不是，报错通常是因为使用了不存在的非标类型如 `u21`，应使用 `u32`）。
- ✅ **保留字与转义规范**：
  1. **常用保留字避让**：`err`, `in`, `as`, `let`, `type`, `class`, `auto` 为核心保留字。
     - 错误变量捕获统一命名为：`e` 或 `error_val`；
     - 字符/码点变量统一命名为：`ch`, `code`, `cp`。
  2. **外部重名字段原生转义**：
     - 若必须引用 C/Zig 中名为 `type` 或 `in` 的结构体字段，使用 `@"ident"`：
     ```typescript
     struct Request {
         @"type": i32,
         @"in": []const u8,
     }
     ```

---

## 三、 关键约定与约束

1. **常驻后台通信守护进程**：
   - 主智能体启动后必须确保后台运行监听进程（IsDaemon: true）：
     ```bash
     python3 scripts/watch_task.py --status resolved --agent bun_agy
     ```
   - 隔离收件箱位于 `/data/dev/cp/.tasks/resolved/bun_agy/`，严格防止与 `capsurf_agy` 串台。
2. **并发子智能体上限 `<= 2` 且严格「用完即弃」**：
   - 任何时刻活跃子智能体严格限制在最多 2 个。
   - **严禁**跨批次复用旧子智能体（避免上下文膨胀至 200k+），单次任务控制在 30~50 步内，完成即销毁。
3. **零轮询铁律**：主智能体派发任务后绝不轮询 transcript 或进度，完全依赖系统的反应式唤醒（Reactive Wakeup）。
4. **单模块 4 项硬门禁标准**：
   - ① **原始备份**：转换前若不存在先备份 `cp <stem>.zig <stem>_.zig`，严禁覆盖现有备份；
   - ② **编译与 AST 校验**：`cpc --lib <stem>.cp -o <stem>.zig && vendor/zig/zig ast-check <stem>.zig` 0 报错；
   - ③ **静态零违例**：0 `zig {}`、0 `var`、0 `.@"0"`/`.@"1"`、0 `*anyopaque`/`?*anyopaque`；
   - ④ **代码膨胀率**：行数严格 $\le 1.20\times$ 原始代码行数。
5. **严禁修改编译器源码**：严禁直接修改 `/data/dev/cp/cpc/src`，遇编译器缺陷通过 `scripts/task_queue.py submit` 提单给 `cp agy`。

---

## 四、 下一步行动（Next Step）

新 AGY 会话启动后，请严格按以下顺序执行操作：

1. **Step 0：检查并启动常驻通信守护任务**
   ```bash
   python3 scripts/watch_task.py --status resolved --agent bun_agy
   ```

````

2. **Step 1：闭环 `src/string/immutable/unicode.cp`**
   - 查看 2023 行 `decodeWTF8RuneTMultibyte` 函数，将其内部的 `val` 变量改为直接表达式计算或直接返回：
     ```cp
     if (len == 2) {
         const res = @as(T, (@as(T, p[0] & 0x1F) << 6) | @as(T, s1 & 0x3F));
         if (res < 0x80) return zero;
         return res;
     }
     ```
   - 运行验收命令：
     ```bash
     python3 scripts/cpc_brief.py src/string/immutable/unicode.cp
     ```
     确保输出 `PASS`。
3. **Step 2：原子 Git 提交 Phase 55**
   - 更新 `SESSION.md`、`CHANGELOG.md`、`DEVLOG.md` 记录已通过全部 4 项门禁的 `html_rewriter.cp` (2,031 行)、`BunObject.cp` (2,216 行) 以及微调后的 `unicode.cp` (2,058 行)；
   - 精确 `git add` 并执行唯一一次原子 `git commit -m "refactor(api/string): convert html_rewriter, BunObject, and unicode to pure CP (Phase 55)"`。
4. **Step 3：派发 2 路全新短生命周期子智能体并发流水线**
   - **智能体 1**：启动 `src/runtime/api/JSBundler.zig`（2,050 行）迁移（先执行 `python3 scripts/zig2cp_prep.py src/runtime/api/JSBundler.zig`）；
   - **智能体 2**：启动 `src/runtime/node/node_fs.cp`（7,334 行，按模块拆分）或 `src/runtime/node/node_stream.zig`。

---

## 五、 Token 极致节省与高效工作流规则

1. **诊断报错禁止全文打印**：
   - 严禁直接打印 40~70KB 的 `cpc` 原始编译输出，统一使用简报工具：
     ```bash
     python3 scripts/cpc_brief.py -n 8 <file.cp>
     ```
2. **预转换脚本先行**：
   - 新模块必须先执行 `python3 scripts/zig2cp_prep.py <file.zig>`，自动完成 80% 机械语法转换后再用 `edit_file` 精修，节省 70% 输出 Tokens。
3. **只读精炼踩坑清单**：
   - 子智能体 Prompt 只需指定阅读根目录的 `CP_PITFALLS.md`（已严格参照最新 68KB 的 `cp_agent_ref.md` 权威契约与 Top 29 禁手重新生成），**严禁**全文读取 68KB 的 `cp_agent_ref.md`，单次派发节省 90% 读取 Token。
4. **Prompt 中性化规范**：
   - 保持中性工程术语（使用“异常边界防护”、“资源耗尽防护”、“终止生命周期”等），杜绝触发安全过滤器与无意义的重试消耗。
````
