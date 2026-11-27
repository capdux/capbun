# Bun.js 1.3.14 (Zig) → 100% 纯 CP 语言重构开发指令与工作流指南（精炼常驻版）

> **历史完整背景归档**：长篇背景说明、早期教程与历史模板已归档至 `GEMINI_H.md` 与 `AGENTS_H.md`。本文件作为主智能体与子智能体每步自动加载的唯一常驻核心准则，保持高度精炼以节省 Token。

---

## 一、 核心使命与角色定位

- **核心目标**：将 **Bun.js 1.3.14 (`src/**/*.zig`)** 100% 重构为 **纯 CP 语言 (`.cp`)**，利用 CP 语言的借用检查（Borrow Checker）、单写多读排他安全（`*this`/`this`）、所有权转移（`own`/`move`）与 `unsafe {}` 显式收窄，根除悬垂指针与内存泄漏。
- **单模块交付标准（4 项硬门禁）**：
  1. **原文件备份**：转换 `<stem>.zig` 前，若 `<stem>_.zig` 不存在，先备份 `cp <stem>.zig <stem>_.zig`（**严禁覆盖已存在的 `<stem>_.zig` 原始备份**）。
  2. **编译与语法校验**：`cpc --lib <stem>.cp -o <stem>.zig && vendor/zig/zig ast-check <stem>.zig` 必须 0 报错通过（且不生成多余 `.zig.map` 入库）。
  3. **静态零违例门禁**：`.cp` 源码中必须满足：
     - **0** `zig { ... }` 逃逸块；
     - **0** `var` 变量声明（仅限 `const` 与 `let`）；
     - **0** `.@"0"` / `.@"1"` 元组属性访问（必须使用 `t[0]` 或 `let [a, b] = t`）；
     - **0** `*anyopaque` / `?*anyopaque` / `*any!` / `*any!?`（必须使用 `*!` 或 `*!?` 或 `*void` / `?*void`）。
  4. **代码膨胀率门禁**：`<stem>.cp` 行数不得超过 `<stem>_.zig` 原始行数的 **+20%**（`<= 1.2x`）。

---

## 二、 严格操作纪律与工具路由 (CRITICAL)

1. **强制工具映射**：
   - **修改/新建源码文件**：优先调用 `edit_file` / `write_file`，或通过 `scripts/zig2cp_prep.py` 批量生成初稿后再用 `edit_file` 精修。
   - **阅读文件**：调用 `read_file`（指定 `StartLine`/`EndLine`）；对于 `> 800` 行的大文件或批量小文件，允许用 `run_command` 执行 `rg -n` 提取结构大纲或批量预览。
   - **代码检索与编译验证**：调用 `run_command` 执行 `rg -n`、`find`、`cpc`、`vendor/zig/zig ast-check` 等，支持管道（`|`）与重定向（`>`、`2>&1`）。
2. **清理与临时文件规范**：
   - 所有临时探测文件、最小复现用例统一存放在 `temp/` 目录（Git 已忽略），严禁污染项目根目录或 `/root/`。
   - 具备直接清理 `temp/`、`bin/`、`.cap/` 下临时产物的完全权限，无需向用户确认。
3. **编码与汇报中性化规范**：
   - 在撰写 `SESSION.md`、`DEVLOG.md` 及对话汇报时，使用中性工程术语（用「异常边界防护 / 畸形报文校验」替代含 attack/exploit 词汇，用「资源耗尽防护」替代 bomb/flood，用「终止生命周期」替代敏感进程控制词）。
   - 避免将测试夹具中的大段 Base64/证书/构造载荷打印到上下文中。
   - 交互语言统一使用**中文**。
4. “针对 > 800 行的大文件，推荐优先使用 rg -n "^(pub\s+)?(fn|class|export|const)\s+" <path> 秒级提取全局结构与方法大纲，再精准使用 read_file 查阅目标实现，消除跨 800行多次分段读取的往返开销。”
5. “修改超过 2000 行的文件时，禁止单回合多次调用编辑工具，必须一次性合并修改（或先拆分子模块）”

---

## 三、 四线文档冷热分离与 Git 原子提交铁律

### 1. 四线文档冷热分离（仅主智能体维护，子智能体严禁修改）

- **热日志文件（仅记录当日活动）**：
  - `SESSION.md`：**交互审计铁律（严禁丢失或错配）**——必须按时间顺序**逐条实时对齐记录**：
    1. **用户完整指令 <-> 主智能体关键回应**：每一条用户指令/提问都必须与主智能体的对应回应成对记录、严格对齐，严禁只写波次代码总结而遗漏用户交互，严禁指令与回应错配；
    2. **跨 AGY 通信（如 `bun_agy <-> cp_agy`）**：每一次工单发送（Task ID、repro 路径、问题摘要）与每一次结单接收（修复结论、本地复现验证、`ack` 状态）都必须成对对齐记录，严禁遗漏或张冠李戴；
    3. **严禁覆盖或删减**历史交互记录。
  - `CHANGELOG.md`：按里程碑追加新增/修改的 `.cp`、`.zig`、`_.zig` 文件清单。
  - `DEVLOG.md`：按里程碑追加核心技术方案、语法 lowering 经验与问题解决思路。
- **冷归档文件**：超过当日的历史记录归档至 `SESSION_H.md`、`CHANGELOG_H.md`、`DEVLOG_H.md`（永久保留）。
- **记录节奏**：用户指令/主智能体回应与 `cp agy` 双向通信必须在当轮/当阶段**实时**写入 `SESSION.md`（不得丢失）；其余模块迁移按阶段性里程碑（每完成 1~3 个子目录）集中汇总写入一次，90% 火力集中于代码攻坚。

### 2. Git 原子提交铁律（仅主智能体执行）

1. **阶段里程碑自动提交**：每完成一个完整子目录或一批模块闭环验证后，自动完成 Git 提交。
2. **先写全文档**：提交前先更新 `SESSION.md`、`CHANGELOG.md`、`DEVLOG.md`。
3. **一次性暂存与提交**：精确 `git add` 变更的源码与文档（注意排除被 `.gitignore` 忽略的 `*.zig.map`），只执行唯一 **1 次** `git commit`。
4. **提交后绝对止步**：严禁 `--amend`，严禁事后补充二次提交。

---

## 四、 与上游 `cp agy` 跨智能体异步双向通信协议

1. **标准操作指令**：`/root/.agy/comm/COMM.md`文件，熟悉多AGY通信机制与指令。
2. **职责边界与多 AGY 隔离铁律**：

- 当前项目名称为`bun`，agy名称为`bun_agy`。
- **主智能体 (`bun_agy`) 与子智能体严禁直接修改 `/data/dev/cp/cpc/src` 编译器源码！**
- 所有 `cpc` 编译器 Bug 或缺失特性，必须且只能通过 `COMM.md` 中约定的方式提交给上游 `cp agy` 修复。
- **CP 语法更新双端广播**：当上游 `cp agy` 更新 CP 语法规范并广播时，`bun_agy` 唤醒后须第一时间同步更新本地 `CP_PITFALLS.md`。
- **工程实践缺陷与优化主动提单铁律（CRITICAL）**：在编写与重构 50 余万行 CP 代码的工程实践中，**一旦发现任何 `cpc` 编译器的隐藏缺陷、代码生成漏洞、安全隐患（如 RAII 漏注入、`move`/借用分析缺口）或语法/工具链可优化改进点，即使当前可通过变通绕过，也必须第一时间提炼最小可复现用例（`/data/dev/cp/temp/repro_<name>.cp`）并向 `cp agy` 提单报告**，严禁“绕过即忘”，持续反哺打磨 CP 编译器生态。

3. **遇到`cp`编译器缺陷时的标准上报流程（绝不单点死锁、绝不用非标语法绕过）**：

- 单个文件编译修复尝试不超过 **5 轮**（Token 熔断安全阀）；
- 提炼 `<= 10` 行最小可复现代码写入 `/data/dev/cp/temp/repro_<name>.cp`；
- 调用投递脚本：
  ```bash
  python3 /root/.agy/comm/task_queue.py submit --id "task-bun-YYYYMMDD-<slug>" --repro "/data/dev/cp/temp/repro_<name>.cp" --error "简明报错说明与代码生成根因定位" --by "bun_agy"
  ```
- 将受阻文件标记为 `BLOCKED`（保留原 `<stem>.zig` 不被破坏），立即切换迁移下一个无依赖模块。
- 在当前下游代码中，使用 `zig { ... }` 原生逃逸块包裹该疑难逻辑，或打上 `// TODO(cpc): [task-id] 待上游支持后重构`。
  **绝不死等，立即继续推进后续文件或整个模块的宏观编译与测试！**

4. **查看看板状态**：
   ```bash
   python3 /root/.agy/comm/task_queue.py list
   ```
5. **收到 `cp agy` 结单通知时**：必须在对话屏幕显式打印结单详情（Task ID、修复结论、复现验证结果），**第一时间实时追加写入 `SESSION.md`**，并立即用新 `cpc` 编译解封此前 `BLOCKED` 的文件。

- 将受阻文件标记为 `BLOCKED`（保留原 `<stem>.zig` 不被破坏），立即切换迁移下一个无依赖模块。

## 三 CP AGY (`cp_agy`) 行为军规

1. **自动唤醒与认领**：一旦被 `agy-comm.service` 唤醒或收到新任务通知，立即执行：

```bash
python3 ~/.agy/comm/task_queue.py unacked --agent cp_agy --json
```

认领待处理工单并开始排查。

2. **修改与部署**：读取 repro 用例并修复 `cpc/src` 源码后，运行 `bash scripts/sync_cpc.sh` 完成原子构建部署，并同步至 `cpc_dev/src`。
3. **验收、批量结单、语法广播与 `ack**`：验证通过后，调用支持多 ID 的 `resolve`命令结单并路由通知：`python3 ~/.agy/comm/task_queue.py resolve <id1> [id2 ...] --note "已修复并验证"`

若涉及 CP 语言语法规范或 `cpc` 规则变更，附加 `--broadcast` 参数或单独执行：
`python3 ~/.agy/comm/task_queue.py broadcast --id "syntax-YYYYMMDD-<slug>" --note "详细语法变更说明"`

处理完成后立即执行确认：`python3 ~/.agy/comm/task_queue.py ack --agent cp_agy <id1> [id2 ...]`

---

## 五、 Token 极致节省与子智能体并行调度 5 大铁律 (CRITICAL)

为防止配额因长上下文累积而快速耗尽，主智能体与子智能体必须严格执行以下 5 大铁律：

1. **铁律 1：并发上限 `<= 2` + 「用完即弃」短生命周期子智能体（严禁跨批次 `send_message` 复用！）**
   - 同时运行的子智能体严格限制在 **最多 2 路**。
   - **彻底禁止通过 `send_message` 向已完成上一批任务的子智能体追加新目录批次**！复用子智能体会使历史上下文膨胀至 200k~300k Tokens/step（$O(N^2)$ 平方级消耗）。
   - 每批任务必须通过 `invoke_subagent` 启动**全新子智能体**（从 0 上下文起步），单次任务控制在 **30~50 步以内**完成即结束。
2. **铁律 2：预转换脚本先行（`scripts/zig2cp_prep.py`）+ `edit_file` 精修**
   - 对待迁移的 `.zig` 文件，子智能体第一步优先运行：
     ```bash
     python3 scripts/zig2cp_prep.py <file1.zig> <file2.zig> ...
     ```
     该脚本自动创建 `<stem>_.zig` 备份并生成完成 80% 机械语法转换的 `<stem>.cp` 初稿。
   - 子智能体随后运行 `cpc --lib` 定位剩余的 15%~20% 语义/结构问题，并用 `edit_file` 精准修复，避免让大模型从零手写上千行重复代码（节省 70% Output Tokens）。
3. **铁律 3：工具调用批处理化（Batch Read / Parallel Edit / Single-Command Verify）**
   - 严禁对 10~50 行的小文件逐个串行执行 `read_file` → `write_file` → `cpc`。
   - 必须一次性批量预转换、同一轮并行调用多个 `edit_file`/`write_file`、并用单条 `for` 循环命令一次性完成整批文件的 `cpc --lib` 与 `zig ast-check` 验证。
4. **铁律 4：内置精炼语法速查，免除重复全文读取 `/data/dev/cp/cp_agent_ref.md`**
   - 子智能体直接使用下方的「第六节：CP 纯化语法与 `cpc` 代码生成速查表」，**严禁在子智能体启动时全文读取 57KB 的 `/data/dev/cp/cp_agent_ref.md`**；仅当遇到生僻语法时才用 `rg -n` 按行检索 `/data/dev/cp/cp_agent_ref.md`。
   - 注意：子智能体 Prompt 中不要直接包含复杂正则字符串（静态正则扫描统一由主智能体在验收阶段执行）。
5. **铁律 5：配额耗尽（Quota Exhaustion 429）自主定时恢复**
   - 若遇到 `RESOURCE_EXHAUSTED (code 429): ... Resets in XhYmZs`，主智能体计算重置秒数 + 120 秒缓冲，调用 `schedule(DurationSeconds=..., Prompt="配额已重置，请检查状态并继续迁移")` 注册单次唤醒定时器后立即休眠结束轮次。

---

## 六、 CP 纯化语法与 `cpc` 代码生成实战速查表（旧版；与第七节冲突处以第七节为准）

1. **模块导入**：
   - `import std from "std";`、`import bun from "bun";`、`import Foo from "./foo.zig";`（严禁 `@import(...)`）。
2. **变量与常量**：
   - 仅用 `const`（只读）与 `let`（可变）；**严禁 `var`**，**严禁 `let mut`**。
   - 注意：`err` 是 CP 关键字（用于声明错误集 `err MyError { ... }`），**严禁将变量、参数或 `catch |e|` / `.err e =>` 的载荷命名为 `err`**（统一命名为 `e`）。
3. **类型与指针**：
   - 原始无类型指针：`*!`（非空，对应 `*anyopaque`）、`*!?`（可空，对应 `?*anyopaque`）或 `*void` / `?*void`。
   - 可空类型指针：注意优先级！`*T?` 会被解析为「指向可空 `T` 的非空指针 `*(T?)`」；**可空指针必须写为 `(*T)?` 或 `?*T` 或 `*T!?`**。
   - 多元素裸指针切片：`[*]u8!` / `[*]const u8!`；对其进行切片 `ptr[0..len]` 或解引用 `ptr.*` 或 `@ptrCast`/`@alignCast` 必须包裹在 `unsafe { ... }` 块中。
   - 空切片初始化：若需要空可变切片 `[]T`，在模块顶层声明 `let empty_x: [0]T = [];`，使用 `empty_x[0..0]`。
4. **结构体、枚举与联合体**：
   - 文件级根结构体：`@root struct Foo { ... }`（或 `pub struct Foo { ... }`）。
   - 位域/紧凑结构体：`@packed(u8) pub struct Flags { ... }`；C ABI 结构体：`@c pub struct CStruct { ... }`；不透明类型：`@opaque pub struct Opaque {}`。
   - **重要 lowering 规则**：`struct` 或 `enum` 内部嵌套的二级子结构体（如 `pub const Sub = struct { ... };`）或包含 CP 语法的复杂 `pub const` 初始化器，会被 `cpc` 原样输出而不经过 CP 表达式 lowering。**必须将二级子结构体或复杂常量提升到模块顶层声明（`pub struct Sub { ... }`），再在父结构体内写 `pub const Sub = TopLevelSub;`**。
   - 若联合体含名为 `name` 的字段（如 `name: Data`），使用 `pub union(enum) Foo` 声明，避免与 `pub enum` 自动生成的 `pub fn name()` 方法冲突。
5. **函数与方法接收者**：
   - 接收者规范：在 `enum`（及最新规范下的类型方法）中，只读实例方法省略接收者形参、在函数体内通过 `_`（或 `_.field`）访问当前实例；可变实例方法首参写 `*_`；静态方法必须显式标注 `static fn`（`enum` 内部成员默认 `pub`，禁止在 `enum` 内写冗余 `pub`）。对于兼容 `*this` / `this` 的 `struct`，若遇 `E0121`，一律切换为省略只读接收者 + `_` / `*_`。
   - 无返回值函数：直接省略 `: void`（若可能返回错误写 `fn foo()!`）。
   - 具名可变参数：形参需在函数体内修改或作为可变借用时，写为 `param: *T`（或在函数首行 `let p = param;`）。
   - 编译期参数修饰：写 `comptime T: type`（**不要写 `@comptime`**）。
   - 线程局部变量：写 `@tls pub let x: T = ...;`（**`@tls` 必须放在 `pub` 之前**）。
   - 值传递可变参数：函数体内需修改按值传入的参数时，声明为 `mut param: T` 或在函数首行 `let p = param;`。
   - 单参数 `.slice(arg)` 调用：对自定义类型调用单参数 `obj.slice(arg)` 会被 `cpc` 当作数组切片 `[arg..]`，请改写为 `const sliceFn = Type.slice; sliceFn(obj, arg)`。
6. **控制流与运算符**：
   - 布尔逻辑：严格使用 `&&` 与 `||`（严禁 `and` / `or`）；空值合并使用 `??`（或 `orelse`）。
   - 错误传播：后缀 `expr?`（替代前缀 `try expr`）；错误捕获 `expr catch val` 或 `expr catch |e| { ... }`。
   - 可选值解包：语句级支持 `if (val := opt_expr) { ... }`、`while (val := opt_expr) { ... }`；注意在**表达式位置**（如 `const x = if (...) a else b`）不要使用 `:=`，请先声明 `let x = ...; if (val := opt) { x = ...; }`。
   - `switch` 语法：枚举/联合体分支写 `.tag => |payload| ...` 或 `.tag payload => ...`；注意当 `switch` 赋给 `u8`/`u32` 等无符号整数变量时，字面量分支写 `@as(u32, 0)` 避免默认推导为 `i32` 触发 `E0207`。
   - 结构体字面量：`Foo{ a: 1, b: 2 }` 或匿名 `{ a: 1, b: 2 }`（字段名不加前导点 `.a = 1`）。
   - 标识符转义：遇到 CP 关键字作为字段/方法/枚举名（如 `in`, `as`, `let`, `import`, `arena`, `from`, `impl`, `meta`, `macro`, `err`, `type`），必须转义为 `@"in"`, `@"arena"`, `@"macro"` 等。

---

## 七、 新版 `cpc` 语法规则（cp agy 2026-11-14 下发；与第六节冲突时**以本节为准**）

完整速查与踩坑清单见仓库根目录 **`CP_PITFALLS.md`**（约 3KB，子智能体启动时只读它，不读 `cp_agent_ref.md`）。要点：

1. **接收者**：禁 `self`/`this`；体内用 `_`；只读省略首参（写 `(_)` 报 E0123）；修改字段/`init` 写 `*_`；转移所有权 `move _`。
2. **形参**：只读 `v: T`；可变 `v: *T`；`move v: T` / `own v: T`；禁 `*v: T` / `mut v: T` / `let v: T`。
3. **局部变量**：仅 `const`/`let`；忽略返回值写 `* = expr;`（禁 `_ = expr;`）。
4. **返回类型**：有返回值必须 `): T`；void 全场景省略 `: void`（`*void` 保留）。
5. **类型修饰**：可选后缀 `T?`（禁 `?T`）；错误联合 `T!` / `T!E`；禁 `any`，泛型推导用 `auto`。
6. **条件/循环**：禁 `|capture|`；只读 `if (v := opt)`、`while (v := it.next())`；可变 `if (*v := opt)`；`for (v of arr)` / `for (*v of arr)` / `for (move v of arr)`（无需写 const/let）；`while (..) : (i += 1)` 合法。**`let v = e` 形式 cpc 不支持**。
7. **C-FFI**：`@c { fn foo(a: i32): i32; struct bar {..} enum baz: c_int {..} }`，导出 `pub @c {..}`，块内不写 `pub`/`extern`。
8. **可见性**：struct 字段/方法、enum 方法默认 `pub`（删多余 `pub`），静态方法 `static fn`；空切片 `= []`（禁 `&.{}`）。
9. **std**：删除 `const std = @import("std");`，直接用 `std.*`；严禁为调 std 用 `zig {}`。
10. **编译宏**：`@is_nest` 替代 `@is_beam`。
11. **一等公民内置类**：`JSON` 与 `RegExp` 已作为全局一等公民内置类（无需 `import`，且与 Bun 100% 同构）。直接使用 `JSON.stringify(val)`、`JSON.parse<T>(str)`、`JSON.parse(str)` 以及 `/pattern/flags` 或 `new RegExp(pattern, flags)`。废除早期遗留的 `@json_parse`。

## 八、 Token 节省固化铁律（2026-11-14 追加，主/子智能体必须遵守）

1. **工具链**：
   - `python3 scripts/zig2cp_prep.py <a.zig> ...`：备份 + 按第七节规则机械转换（接收者、返回类型、`|capture|`、`?T`、`try`、struct/err 声明等）。
   - `/data/dev/cp/cap/bin/cpc fix <file.cp>`（或 `cpc check --fix`）：利用编译器内置修复能力自动完成 `E0101`..`E0104`, `E0115` (`?T`->`T?`), `E0116` (`:`), `E0118` (`|v|`), `E0119`, `E0120` (`* =`), `E0121` (`*_`), `E0122`, `E0123`, `E0126` 的多轮就地机械修复。
   - `cpc check --brief <file.cp>` / `python3 scripts/cpc_brief.py -n 8 <a.cp> ...`：聚合去重诊断模式，折叠 `cascade_of` 连带错误；一条命令完成 `cpc check --json --brief` 提取 + `cpc --lib` 编译 + `ast-check` + 4 门禁。**严禁**直接查看 `cpc` 原始报错（单次 40~70KB）。
2. **踩坑表驱动**：子智能体 Prompt 只写「读 `CP_PITFALLS.md`、按其 C 节工作流执行」+ 目标文件与门禁，不再内联长篇语法说明。发现新坑由主智能体追加一行到 `CP_PITFALLS.md`（子智能体可在汇报中提交）。
3. **步数熔断**：子智能体单文件 ≤ 50 步仍未过门禁 → 汇报 `BLOCKED` 并停止；>2,500 行大文件按函数段拆给多个全新子智能体；同类错误一次修完整个文件再重编，禁止逐条编译。
4. **主智能体禁止轮询**：不得为“看进度”解析子智能体 transcript / 重复 `manage_subagents list`；只等 `send_message` 唤醒。收到汇报后由主智能体自己用 `cpc_brief.py` 一条命令验收。
5. **汇报精简**：阶段汇报只给 3~5 行数字（模块数、闭合目录数、膨胀率区间）；`SESSION/CHANGELOG/DEVLOG` 用单行条目追加，不重复叙述；每 3~5 个模块（或闭合一个目录）提交一次。
6. **配额耗尽（429）**：计算重置秒数 + 120 秒，`schedule` 单次唤醒后立即结束轮次，唤醒后重派全新子智能体（不复用旧的）。
7. **存量净化**：已迁移 `.cp` 若含旧写法（`this`/`_ = `/`: void`/`?T`/`|capture|`/`zig {}` 取 std 等），在新 `cpc` 全量生效后分批用 `cpc_brief.py` 批量发现并净化，优先于新增迁移前先抽样 3 个目录验证。
8. **`@root struct` 与辅助结构体组织铁律**：
   - 当原 Zig 文件表示根结构体时，文件级的辅助结构体/枚举（如 `Route`, `Type`, `EncodedPattern`, `TinyLog` 等）必须声明在模块顶层（位于 `@root struct Name { ... }` 外部），使 `cpc` 能进行完整的 CP 语法 lowering（`static fn` -> `pub fn`, `: ReturnType` -> `ReturnType` 等）；
   - `@root struct Name { ... }` 仅包裹根结构体自身的字段与自身的方法（如 `initEmpty`, `deinit`, `scan`, `insert`），确保方法体内的 `_` 与 `*_` 能正确绑定到根结构体实例接收者；
   - 严禁将二级子结构体嵌套在 `@root struct` 字段声明中间（会被 codegen 跳过或解析为 field 报错）。

   ### 九、 核心使命践行：利用 CP 借用规则根治 Zig 内存泄漏

   将所有权转移与单写多读排他安全写入主智能体和子智能体核心指令中，作为全量迁移的最高优先级要求：
   - 可变性排他收窄：所有方法严格区分只读（_、v: T）与独占可变（\*_、v: \*T），消除 Zig 中由于指针别名引发的并发数据竞争与悬垂引用；
   - 所有权转移明确化：资源释放与跨状态传递显式标注 move \_ / own v: T，禁止隐式浅拷贝与双重释放；
   - RAII 生命周期保障：配合 defer 资源析构，利用 CP 编译器的严格生命周期分析，彻底消除 Zig 原版 Bun 中的内存泄漏隐患，打造如 Rust 般稳定高健壮的纯 CP运行时。

---

## 十、 全仓收敛期提速增效铁律（2026-11-20 追加，固化防退化）

1. **恢复 3 路互斥并发 + 小包裹快周转**：
   - 在配额允许时保持 **3 路并发**（单路控制在 **15~25 个互斥文件**，避免 35+ 文件长上下文拖慢单轮周期）。
   - 采用**流水线接力**：任意一路交付即独立验收、原子提交并立刻补派下一路，不互等木桶短板。
2. **全仓机械雷区提前静态排雷（打破 Zig 惰性检查洋葱效应）**：
   - 定期用脚本扫描全仓 `.zig` 中的确定性低级生成缺陷（如 `return error.(js_undefined|zero|true|false|null|Empty|Null|Used)`、`(&.{})`、`?[*]u8` 丢失 `const`、`*const *const fn`），以及上游 `cpc` 修复缺陷后对历史受影响 `.cp` 的批量静默重编（`cpc_brief.py -n 4`），一次性消除未来数波才会暴露的隐藏报错。
3. **Zig 编译与校验加速**：
   - 语义检查阶段统一使用 `bun run zig:check`（仅类型/语义检查，不触发 LLVM codegen）。
   - 进入二进制编译联调阶段时，优先使用 **Debug 模式并跳过 LLVM**（如 `--profile=debug` 配合 `-fno-llvm` / 本地快速调试配置）提速，待功能与测试全绿后再跑完整构建。

---

## 十一、 收敛期高频 `cpc` 雷区与子智能体省 Token 指派规范（2026-11-21 固化）

1. **5 类高频机械雷区（主/子智能体主动排雷必查）**：
   - `std.sort.*(..., .{}, ...)` / `std.mem.sort(..., {}, ...)` → 必须先声明 `const empty_sort_ctx: void = undefined;` 再传入 `empty_sort_ctx`（否则报 `parameter 0 'void' cannot cast into '@TypeOf(.{})'`）。
   - `.windows = if (Environment.isWindows) .{...}` → 必须显式补全 `else @as(void, undefined)`（禁省略或 `else null`）。
   - `Output.pretty*` / `Output.print` → 严禁直接传入多行 `\\` 字符串（`cpc` 会拆成多个参数），统一改用 `\n` 转义单行字符串。
   - `XxxCommand.exec(a, b, ...)` → 多参数直接调用会被 `cpc` 截断成 1 个参数，必须提取 `const exec_fn = XxxCommand.exec; exec_fn(a, b, ...)`。
   - `[0]T = []` / `&.{}` → 改为 `= undefined`，切片点用 `arr[0..0]`。
   - `Type!void` / `[][]const u8 = .{ ... }` → 错误联合写为 `: !Type`（禁 `: Type!`），局部数组字面量必须显式定长 `[N]T = [ ... ]`（禁推导为 `[]*anyopaque` / `[][]const u8 = .{`）。
2. **结构性与静默丢弃防护**：
   - **禁止匿名 `struct` 作函数返回类型**（如 `fn foo(): !struct { ... }`），否则 `cpc` 会静默跳过整段父结构体 codegen；必须先提炼具名 `pub struct`。
   - 修改含 `pub struct XxxCommand` 的文件后，验收时用 `rg -n "pub const XxxCommand" <out>.zig` 二次确认导出结构体未被吞掉。
3. **子智能体省 Token 精确指派铁律**：
   - 主智能体派发子智能体前，先给出**精确目录范围 + `rg -n` 命中正则/特征模式 + 1 条 `cpc_brief.py -n 4` 验收命令**，禁止子智能体盲目全目录 `find`/`read_file` 浪费上下文；短任务结束后及时回收或按批次接力，保持最多 2 路并发持续推进。
4. **全仓 `zig:check` 防空跑与提速铁律**：
   - `cpc_brief.py` 失败时必须返回非 0 退出码，严禁在 `FAIL(ast-check)` 时继续串联运行 40 秒的 `bun run zig:check`；
   - 发现任意新模式错误（如 `Type!void`、未定长数组字面量）后，立即用 `rg -n` 一次性扫描全仓 `.zig` 批量排雷，打破“改 1 处 → 等 40 秒”的洋葱循环；
   - 仅修改 `.cp -> .zig` 且无构建配置变更时，可跳过外层 `scripts/build.ts` configure 包装以缩短单轮验证耗时。
5. **运行时/ABI 致命雷区固化（2026-11-21 `bun-debug` 联调沉淀）**：
   - `@c` 联合体值字段：`pub @c union` 必须 `pub` 在 `@c` 前（禁 `export @c union` 或 `@c pub union`），否则引用它的结构体字段会被自动指针化为 `*Union` 破坏 C++ ABI 布局；
   - 错误联合中返回枚举：`!EnumType` 函数体内严禁 `return .tag;`，否则 `cpc` 会降级为 `return error.tag;`，必须显式写 `return EnumType.tag;`；
   - 借用单例防 RAII 自动释放：严禁将带 `.release()`/`.deinit()` 的借用单例（如 `uws.Loop.get()`）绑定到局部 `const`/`let`，避免 `cpc` 自动注入 `defer release()`；
   - 表达式 `if (opt) |x|`：严禁在赋值/字段初始化中使用旧 Zig `= if (opt) |x|`（会被 `cpc` 静默丢弃），统一改写为语句级 `if (x := opt)`。

---

## 十二、 Phase 2 运行时测试与省 Token 提速固化铁律（2026-11-24 自优化合并）

1. **Debug 优先与静默日志防刷屏**：
   - 日常修复与回归验证统一使用 **`bun bd test`**（或当 `build/debug/bun-debug` 已最新时直接调用 `BUN_DEBUG_QUIET_LOGS=1 ./build/debug/bun-debug test` 跳过重复 `ninja` 检查），严禁在单点修复迭代中频繁触发耗时的 `build:release`（仅在阶段收官或 Release 专属验证时构建）。
   - 执行 `bun-debug` 时优先附加 `BUN_DEBUG_QUIET_LOGS=1`，消除数千行 `[sys]`/`[fs]` 调试日志对上下文的污染。
2. **测试输出极简过滤与防卡死熔断**：
   - 批量扫测或回归命令强制包裹 `timeout <N>` 并配合 `--timeout=10000` + `2>&1 | rg "^\(fail\)| pass$| fail$|Ran [0-9]+ tests|panic|SIGSEGV"`，将单次输出压缩至 2~8 行，彻底杜绝 600s 后台挂起与长日志刷屏浪费 Token。
3. **运行时缺陷全仓倍增排雷 + 编译器高频雷区追加**：
   - 每通过运行时测试定位 1 类共性 `.cp` 模式缺陷，立即用 1 条 `rg -n` 扫描全仓 1,295 个 `.cp` 文件一次性清零同类隐患（如：闭区间字符范围 `'0'..='9'` 防半开 `..` 漏掉 `'9'`；枚举字面量关键字转义 `.@"private"` 防 `cpc` 静默丢弃语句；`ArrayBitSet.iterator` 等返回 `&_.field` 的方法禁省略 `_: *const Self` 防按值传参悬垂栈指针；禁将局部函数返回 `struct { ~init() }` 绑定为 RAII 守卫，改用显式 `defer cleanup(...)`）。
4. **`bun bd` 串行构建互斥 + 小批次 `--timeout=5000` 防阻塞**：
   - 当 `bun bd` 正在后台链接 `build/debug/bun-debug` 时，严禁并发启动第二个 `bun bd`；等待期间用 `./build/release/bun test` 摸底独立测试目录，或做 `.cp` 静态排雷 + `cpc_brief.py` + `zig:check`。
   - 摸底含外部网络/子进程挂起风险的集成与 CLI 测试时，强制传 `--timeout=5000` 并限制 `≤ 4` 个文件/批，确保单批在 `timeout 30` 内必然打印 `Ran X tests` 摘要。
5. **Release 解锁 `bun:internal-for-testing` + 复用产物并发扫测**：
   - 使用 `./build/release/bun test` 并发扫测时，统一附加 `BUN_GARBAGE_COLLECTOR_LEVEL=0 BUN_FEATURE_FLAG_INTERNAL_FOR_TESTING=1`，无需依赖 Debug 构建即可启用 `bun:internal-for-testing`（避免 `ENOENT` 假失败）。
   - 不新开 `--build-dir` 冷构建，而是待一次 `bun bd`/`build:release` 完成后直接并发调用产物二进制运行多路只读扫测（过滤管道使用 `rg -a` 兼容含 `\0` 输出），仅在集中修复多处 `.cp` 后再串行重编一次。
6. **并发测试降拥堵与严禁 `gdb` 挂死铁律（2026-11-25 Phase 3 自优化固化）**：
   - 运行多子进程并发类测试（如 `websocket-server`/`http`）时，在 `bun-debug` 下统一附加 `--max-concurrency=2` 并使用绝对路径 `/data/dev/bun/build/debug/bun-debug`（避免 `cwd` 切换后找不到相对路径 `${BUN}`）。
   - **严禁**在子智能体中对大型 Debug 二进制（`bun-debug`）启动 `gdb`/`lldb` 断点调试（极易因符号加载/断点停顿挂死 15+ 分钟），排查泄漏统一采用静态源码追踪 + 轻量 `-e` 复现探针 + `BUN_DEBUG_<scope>=1`；主智能体发现子智能体 `> 10` 分钟无进展或 `gdb` 驻留时立即回收重派。

---

## 十三、 吸收上游 Bun Rust 版本 Commit 提速固化规则（2026-11-25 固化）

1. **按“净 Diff 合并”替代“逐 Commit 移植”**：
   - 在同一个区段（如 Step 2 / Step 3 / Step 4 小版本区间）内，若同一文件被多个 commit 连续修改，直接按子系统/文件提取 `起点..终点` 的净安全 Diff（过滤 Rust 语法/重构噪声），一次性等价落入 `.cp`，严禁同一文件被反复修改多次。
2. **批量脚本预筛选 + 同构子系统大包分配**：
   - 先用 Python 脚本自动过滤 `clippy`、`cargo`、`windows-stub`、`crate move`、纯 `unsafe`/类型重构等 Rust-only 噪声，仅保留触及逻辑边界、内存生命周期、`defer`/`errdefer`、整数溢出、缓冲区校验的 Diff。
   - 按同构子系统打包（10~20 个文件/路），主智能体保持 2 路并发子智能体，同时主智能体同步承担快速修复/JS-TS/C++ 移植与协调，不闲置等待。
3. **编译与测试分层节奏（吸收期禁止 `bun bd` 防 OOM）**：
   - 子智能体移植期间仅逐文件跑轻量 `python3 scripts/cpc_brief.py -n 8`（4 门禁），不触发全仓构建。
   - **吸收期严禁执行 `bun bd` 链接与 `bun bd test` 并发扫测**（Debug+ASan 链接与多进程测试易耗尽服务器内存导致重启）；阶段收官仅运行轻量 `bun run zig:check`（约 2GB RAM）做全仓语义校验，**同步吸收上游测试文件暂不运行**，待全部吸收至 1.4.5 完毕后再统一构建与回归测试。
4. **仅对非 Rust-only 的 Linux/Windows/跨平台运行时修复吸收**：
   - 跳过 Cargo/clippy/Windows Rust stub crate 搬家 commit；对真正影响 Windows/Linux/跨平台运行时行为与内存安全的修复全部等价吸收。

---

## 十四、 上游 Rust Bun 版本持续跟进、吸收与发布标准流程（SOP）

为保证后续新版本（`bun-v1.4.6` 及以后）能沿用当前已验证的“三路并行 + 逐版锁定”方法持续吸收并发布同版本号 CP-Bun，执行以下标准步骤：

1. **配套脚本**：`scripts/upstream_sync.py`（配合 `scripts/zig2cp_prep.py`、`scripts/cpc_brief.py`）
   - `python3 scripts/upstream_sync.py plan <from_tag> <to_tag>`：切换 `../bun-ref` 至目标版本，过滤 `ci:`/`docs:`/`test:`/`bun check`/`bun lint`/`bun fmt` 等非运行时噪声，按目录统计受影响模块与净 Commit 列表。
   - `python3 scripts/upstream_sync.py sync-tests <to_tag>`：将 `test/` 与 `packages/bun-types/` 与上游目标版本 `rsync --delete` 对齐。
   - `python3 scripts/upstream_sync.py verify <cp_files...>`：批量运行 `cpc_brief.py -n 8` 四项硬门禁，并执行 `bun run zig:check`（5/5 语义校验）。
   - `python3 scripts/upstream_sync.py tag <cp-vX.Y.Z-sync>`：排除 `*.zig.map` 后原子提交并打标。

2. **三路并行吸收分工**
   - **Subagent 1**：`src/install/`、`src/cli/`、`src/resolver/`、`src/sys/`、`src/io/`、`src/glob/`、`src/semver/`。
   - **Subagent 2**：`src/bundler/`、`src/js_parser/`、`src/js_printer/`、`src/css/`、`src/sourcemap/`、`src/ast/`、`src/bake/`。
   - **主智能体**：`test/` 与 `packages/bun-types/` 同步、`http/http2/http3/usockets/ws/tls/sql/jsc/webcore/sqlite`、`scripts/build/deps/*`、`patches/*`，以及统一 `bun run zig:check` 跨模块编译收敛。
   - **硬性纪律**：吸收期内子智能体**严禁**运行 `bun bd` / `bun bd test`（仅跑 `cpc_brief.py`）；全仓语义通过 `zig:check` 后再由主智能体串行执行 `bun bd` 构建与分域测试验证。

3. **发布同版本号 CP-Bun 流程**
   - 完成 `cp-vX.Y.Z-sync` 打标后，运行 `bun bd -p 'Bun.version'` 验证链接与启动；
   - 分域回归核心测试集（`xml`、`yaml`、`sqlite`、`http2`、`tls`、`webcrypto`、`diagnostics_channel`、`bundler`、`install`），修复残余回归后执行最终 `git commit` 与版本标签发布。

---

## 十五、 全仓测试大扫测、集中修正与单次构建提速规范（2026-11-26 固化）

为彻底消除「修 1~2 个小模块就执行一次 `bun bd` 重编」造成的分钟级链接等待浪费，全仓测试收敛严格采用**「大扫测 → 集中修正 → 单次构建回归 → 原子提交推送」**三段式流水线：

1. **阶段 1：大扫测与失败归档（零构建、只读批量）**
   - 直接复用当前 `./build/debug/bun-debug`，运行 `python3 scripts/scan_tests.py`（强制 `BUN_DEBUG_QUIET_LOGS=1`、`--timeout=5000`、单分片 `timeout 45` 防卡死，并自动跳过 `test/cli/lint`、`test/cli/format`、`test/cli/check` 等未引入独立 Rust crate 目录）。
   - 将全仓待测目录的 `(fail)`、`panic`、`AddressSanitizer`、`Ran X tests` 汇总输出至 `temp/test_scan_report.txt`，按源码子系统聚类。

2. **阶段 2：多路大包集中修正（严禁中途 `bun bd`）**
   - **禁止一错一改**：主智能体 + **2 路互斥子智能体**同时认领大包（每路同时修复 **3~5 个子目录 / 20~50 项失败**），集中修改 `.cp` / `.cpp` / `.ts`。
   - 修复期子智能体**严禁运行 `bun bd` 或 `bun bd test`**；修改 `.cp` 后仅用 `python3 scripts/cpc_brief.py -n 8` 通过 4 项硬门禁；若跨多个 `.cp` 文件需确认符号引用，统一跑轻量 `bun run zig:check`（仅类型校验、不触发 LLVM/链接）。

3. **阶段 3：单次集中构建 + 批量回归 + 状态账本与提交推送**
   - 待本大轮 2 路子智能体 + 主智能体修改全部就位后，由主智能体**仅执行唯一 1 次 `bun bd -p 'Bun.version'`** 生成最新 Debug 二进制。
   - 用单条批处理循环（`BUN_DEBUG_QUIET_LOGS=1 timeout 45 ./build/debug/bun-debug test --timeout=10000`）一次性验收本轮全部修改过的目录，实时更新 **`test/stats.md`**（记录已全绿、已修待编、在修与待修队列，重启后优先读取以避免重复测试），随后执行原子 `git commit && git push origin main`，无缝进入下一大轮或最终 `build:release` 发布。
4. 临时文件, 放入当前项目的tmp目录!
5. 测试时, 先扫描收集错误, 集中修改后编译为debug版本, 再扫描收集错误,如此往复循环, 提高效率, 不要一错一改.
6. 测试结果及时写入 `test/stats.md`， 测试通过的， 不要重复测试（除非我下指令）， 浪费token。
7. 每波测试完成， 更新文档， 及时git 提交并push， 固化成果, 然后继续下一波, 不要停。
8. 确保开启并三个子智能体,与主智能体一起工作, 主智能体不可空闲等待,也不可陷入长时间任务. 子智能体收回后立刻新发, 确保三个子智能体在线.
9. **【2026-11-27 3小时审计固化】防 OOM/Token 浪费 4 项铁律**：
   - 所有测试命令必须带 `timeout -k 3 <sec>` 防止 `Bun.spawn` 孤儿进程残留消耗内存/拖慢调度；
   - 已全绿套件写入 `test/stats.md` 后**严禁重复运行**，仅重跑新增/修复套件；
   - 明确缺失上游大型特性（如 `Bun.ModuleGraph`、`node:repl`、`bun lint`、`maxHeadersCount`、`alpnCallback`）直接写入冻结清单，禁止反复排查浪费 Token；
   - 严禁在多路子智能体同时运行 `bun-debug` 扫测期间中途触发 `zig:check` 覆盖 `build/debug/js/`，必须集中在 `bun bd` 前统一校验。
   - **2026-11-27 3h 审计固化**：
     - (a) 批量验收集前用 `ls`/glob 校验精确后缀（`.test.ts`/`.test.js`），避免 `escapeHTML.test.ts` 类误报重跑；子进程会 `cwd` 的测试必须传绝对路径 `/data/dev/bun/build/debug/bun-debug`；
     - (b) 修改 `lexer.cp` 等基础路径后先对照 `_.zig` 核验无符号减法边界再 `bun bd`，防止 `harness.ts` 级重链返工；C++ 修改优先用 `JSC::asString`、`dynamicDowncast`、`uncheckedDowncast`、`tryClearException()`、`PropertyNameArrayBuilder`；
     - (c) 每次 `bun-debug test` 扫测或验收批结束必须执行 `pkill -9 -x bun-debug || true`（严禁用 `pkill -f`，会误杀当前 shell），彻底清理测试派生的孤儿进程（如 `sleep-4ever.js`、`fixture.js`）。

---

## 十六、 千级测试加速收敛与吞吐倍增铁律（2026-11-27 固化）

针对剩余 1,200+ 测试文件收敛慢的瓶颈，固化以下 5 项强制提速手段：

1. **脚本秒级预筛冻结项，绝不人工逐个排查缺依赖**：
   - 用单条 `rg`/Python 脚本一次性扫描 `test/` 下的外部未安装 npm 包导入（`ws`、`svelte`、`happy-dom`、`lodash`、`mysql2`、`sharp`、`@grpc/grpc-js`、`filenamify` 等）、在线 `bun install` 测试及 `node-gyp` 缺失头文件用例，直接批量归档为冻结项写入 `test/stats.md`，禁止派发子智能体重复排查。
2. **「扫绿清库」与「扫修一体大包」双轮驱动（砍半往返轮次）**：
   - 取消「先派只读分析、再派修改」的二段式派发（仅在 `bun bd` 链接窗口使用只读/静态扫描）；平时子智能体直接领**扫修一体大包**（每路同时认领 5~10 个文件、15~30 处失败，一次改完并全部通过 `cpc_brief.py`）。
   - **批量清库存**：`bun bd` 构建完成后，用 4~6 路并发短超时扫测（`timeout -k 3 30`，`--timeout=5000`）快速横扫未登记的 `test/cli/`、`test/bundler/`、`test/js/{bun,node,web}/`、`test/regression/issue/`，10 分钟内把事实上已 `0 fail` 的数百个文件一次性划入 `test/stats.md` 已绿清单，只把真正仍 fail 的少量文件留给修复子智能体。
3. **按「全仓共性根因」聚类横扫，而非按单测文件逐个磨**：
   - 从扫测报告中按报错签名聚类（如同类 `ERR_INVALID_ARG_TYPE`、`Proxy`/深递归栈溢出、`PathBuffer` 越界、`.slice()`/`toNumber` 校验），1 个子智能体一次修复同一子系统全部同类问题，避免同一模块被反复修改 5~6 轮。
4. **Release 辅助快筛 + Debug+ASan 精准复核**：
   - 对含大量子进程启动或重循环的套件（`test/cli/`、`glob/stress`、`s3`、`cp -n`），Debug+ASan（`-O0`）单进程开销大易假超时；可用 `./build/release/bun test`（配 `BUN_FEATURE_FLAG_INTERNAL_FOR_TESTING=1`）先做逻辑层极速筛选，确认逻辑通过后再用 `bun-debug` 做最终 ASan 复核（集中修改 15~25 个文件才重编 1 次 `bun bd`）。
5. **保持 2 路高密度互斥并行 + 30 分钟强制熔断回收**：
   - 严格保持 2 路子智能体同时在线、修改目录完全互斥；若任意子智能体运行超过 30 分钟或陷入反复试错，立即 `manage_subagents kill` 收回并拆分更小包重派。
6. **2026-11-27 11:13 3h 审计固化**：
   - **(a) 验收输出精确抽取**：`rg -E` 为 encoding 参数而非 ERE，过滤测试汇总严禁加 `-E`；对批量验收后仍 FAIL 的文件，统一用单条命令 `rg -A 6 "\(fail\)"` 抽取精确断言行后再派发子智能体，避免盲查浪费 Token。
   - **(b) `stats.md` 逐路径实名制**：新增全绿套件必须在 `test/stats.md` 写出完整文件路径或明确文件名列表（禁止仅写「XX 个」总数缩写），确保自动化对账脚本零遗漏去重。
   - **(c) 构建窗口零空转**：主智能体跑 `bun run zig:check` / `bun bd` 期间，2 路并发全部派给纯只读分析子智能体（不写文件），构建完成即刻切换写入，最大化压缩端到端周期。
7. **2026-11-27 11:14 战略升级（96 核多路并发扫绿 + 实名建账 + 错误聚类）**：
   - **(a) 多核并发横扫**：本机具备 96 核 / 125GiB 内存，禁止再用单核 `for` 循环慢吞吞扫测！全仓摸底必须使用 `xargs -P 16`（`timeout -k 2 12`、`--timeout=5000`），几分钟内扫完上千文件，先将所有容易通过的测试全部测通。
   - **(b) 100% 实名路径账本**：所有通过的测试文件必须以完整相对路径写入 `test/stats.md`（或配套机读清单），严禁仅写总数缩写，确保任何时刻通过率/剩余数 100% 精确可算。
   - **(c) 失败签名聚类集中攻关**：横扫结束后先用脚本按 `(fail)` / `error:` / `panic:` 签名自动聚类归类，再按错误类别批量派发子智能体集中根治。
