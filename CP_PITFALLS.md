# CP 迁移踩坑速查与权威契约手册 (CP_PITFALLS.md)

> **子智能体唯一必读速查指南（约 4KB）**。参照 `/data/dev/cp/cp_agent_ref.md` 最新权威契约与 Bun 重构实战经验提炼。**子智能体严禁全文读取 68KB 的 `cp_agent_ref.md`！**

---

## A. CP 权威语法核心契约与 Top 29 禁手速查

1. **方法接收者与修饰符**：
   - 彻底废除 `self`/`this`（`E0121`）。方法体内访问当前实例统一用 `_`；
   - 只读方法**省略首参** `fn foo(): T`（形参写 `(_)` 报 `E0123`）；
   - 可变修改方法首参必须写 `*_`（如 `fn set(*_, val: i32)`、`fn init(*_)`）；
   - 转移所有权首参写 `move _`；
   - 实例方法一律写 `pub fn`；仅静态函数写 `static fn`（`struct`/`enum` 内部方法默认全部 `pub`，严禁在内部写冗余 `pub`）。
2. **形参契约与变量声明**：
   - 形参只读 `v: T`；可变 `v: *T`；所有权转移 `move v: T` / `own v: *T`；
   - **绝对禁手**：严禁 `*v: T`、`mut v: T`、`let v: T`（`E0104`/`E0125`）；
   - 局部变量**仅用 `const`（只读）与 `let`（可变）**；彻底废除 `var` 与 `let mut`；
   - 丢弃忽略返回值统一写 `* = expr;`（严禁 `_ = expr;` 或 `let _ = expr;`，`E0120`）。
3. **返回类型与类型后缀法则**：
   - 有返回值**必须加冒号** `): ReturnType`（漏冒号报 `E0116`）；
   - `void` 返回值全场景一律**直接省略** `: void`（含 `@c` 外部声明，`E0122`；`*void` 指针保留）；
   - 类型主体居首：可选类型后缀 `T?`（**严禁**前缀 `?T`，`E0115`）；错误联合后缀 `T!` / `T ! Err`（**严禁**前缀 `!T`）；
   - 彻底废除 `any`（`E0119`），编译期泛型参数单态化推导统一使用 `auto`。
4. **控制流解包与循环遍历**：
   - 彻底废除 Zig 管道捕获 `|capture|`（`E0118`）与 `if (let v = opt)`（`E0126`）；
   - 只读解包唯一合法语法为 Walrus 海象符：`if (v := opt)`、`while (v := reader.next())`；
   - 可变指针就地零拷贝解包：`if (*v := state)`、`while (*item := it.next())`；
   - 循环遍历：只读 `for (v of arr)`、可变借用 `for (*v of arr)`、所有权转移 `for (move v of arr)`（无需前缀 `const`/`let`）；
   - 步进循环支持 `while (i < len) : (i += 1)`；跨层跳出用 `break :label` / `continue :label`。
5. **C-FFI、指针安全与标准库**：
   - C 符号声明使用 `@c { fn foo(a: i32): i32; struct Bar {..} }`；导出使用 `pub @c {..}`；块内严禁重复写 `pub` 或 `extern`；
   - 裸指针：非空 `*!`、可空 `*!?`；裸指针解引用 `p.*`、切片截取 `p[0..len]` 与强转必须包裹在 `unsafe { ... }` 块内；
   - 严禁 `const std = @import("std");`，代码中直接调用全局 `std.*`、`builtin`，**严禁**为调用 std 使用 `zig {}`；
   - 运行时编译宏统一使用 `@is_nest`（替代旧版的 `@is_beam`）。
6. **一等公民内置类（无需 import）**：
   - `JSON` 与 `RegExp` 为语言原生一等公民：直接调用 `JSON.stringify(val)`、`JSON.parse<T>(str)?`、`JSON.parse(str)?`；
   - 正则支持字面量 `/pattern/flags` 与 `new RegExp(pattern, flags)`。严禁使用历史遗留的 `@json_parse`。

---

## B. Bun 资产迁移实战避坑清单 (Lowering Pitfalls)

1. **范围模式严禁使用三点 `...`，必须严格写为两点 `..`**：
   - 踩坑：在 `switch (r) { 0...0x7F => ... }` 中写 `...` 会导致 `cpc` 词法解析错位，发生 AST 语句静默丢失。
   - 规范：全场景严格写为两点 `0..0x7F`、`0x80..0x7FF`、`0..=10`。
2. **标量错误匹配严禁使用 `switch (e)`**：
   - 踩坑：在 `catch |e|` 后对错误标量使用 `switch (e)` 会触发生成未声明的 `cp_has_ok_val` / `cp_is_null_val` 导致编译失败。
   - 规范：改用语句级 `if (e == error.A) ... else if (e == error.B) ...`，或确定不失败时直接 `catch unreachable`。
3. **实例方法禁止声明为 `static fn`**：
   - 踩坑：可变接收者方法若写为 `static fn foo(*_, ...)`，`static` 会从参数列表剔除接收者，但在体内仍将 `_` lower 为 `self`，导致 `undeclared identifier 'self'`。
   - 规范：实例方法一律写 `pub fn foo(*_, ...)` 或省略只读接收者 `pub fn foo(...)`。
4. **数组字面量类型推导陷阱（`E0207`）**：
   - 踩坑：`[ bytes[0], 0, 0, 0 ]` 中的字面量 `0` 会被默认推导为 `i32`，导致生成类型为 `Tuple([U8, I32, I32, I32])` 报类型不匹配。
   - 规范：在函数或模块顶层声明 `const z: u8 = 0;`，数组写为 `[ bytes[0], z, z, z ]`；或者使用 `@as([4]u8, [ bytes[0], 0, 0, 0 ])`。
5. **空切片与字符串类型别名**：
   - 踩坑：写 `const string = []const u8;` 会误声明局部数组变量遮蔽原生类型 `u8`；写 `return &[_]u8{};` 无法推导数组长度。
   - 规范：类型别名写 `type string = []const u8;`；返回空切片写 `return "";`（自动推导为 `[]const u8`）或 `let empty: [0]T = []; return empty[0..0];`。
6. **CP 内置关键字命名冲突**：
   - 踩坑：`codepoint`、`err`、`in`、`as`、`let`、`type`、`import`、`arena`、`from` 等为 CP 内置关键字。若用作变量名或字段名会导致词法断裂。
   - 规范：错误捕获命名为 `e`；Unicode 码点命名为 `cp`；结构体字段或标识符必须转义为 `@"in"`, `@"arena"`, `@"type"`。
7. **`@root struct` 与辅助结构体组织铁律**：
   - 规范：原 Zig 文件若为根结构体，文件级辅助结构体/枚举必须提升到模块顶层（位于 `@root struct Name { ... }` 外部），`@root struct` 内仅放置根字段与自身方法，确保内部 `_`/`*_` 正常绑定且辅助类型被完整 CP lowering。
8. **结构体嵌套提升与分号结尾**：
   - 规范：`struct`/`enum` 内部嵌套的二级子结构体（`pub const Sub = struct {..};`）会被跳过 CP lowering，必须提升至文件顶层声明；所有 `pub struct Foo { ... };` 声明尾部必须带分号 `;`。
9. **`?? return` 语法非法**：
   - 规范：`??` 是表达式运算符，`return` 是语句。严禁 `val ?? return null;`，必须写为 `if (v := opt) { ... } else return null;` 或 `let v = opt; if (v == null) return null;`。

---

## C. 子智能体 Token 极简流水线与 4 项硬门禁

1. **Step 1 机械预转换**：
   ```bash
   python3 scripts/zig2cp_prep.py <file1.zig> <file2.zig>
   ```
   自动创建 `<stem>_.zig` 原始备份（若已存在严禁覆盖），并生成完成 80% 语法的 `<stem>.cp` 初稿。
2. **Step 2 编译器内置修复**：
   ```bash
   /data/dev/cp/cap/bin/cpc fix <file.cp>
   ```
   自动就地修复 `E0101`..`E0104`, `E0115`..`E0123`, `E0126`（如自动修正 `?T` 为 `T?`，修正 `_ =` 为 `* =` 等）。
3. **Step 3 聚合去重诊断验收（严禁查看原始报错）**：
   ```bash
   python3 scripts/cpc_brief.py -n 8 <file.cp>
   ```
   **严禁**直接运行不带过滤的 `cpc` 打印 40~70KB 原始报错！`cpc_brief.py` 自动合并级联错误并完成 4 项硬门禁检查。
4. **Step 4 精确修补与熔断**：
   - 用 `edit_file` 精准修补。同一类错误在整文件一次性修完再复编，严禁逐行微调试错；
   - 单文件修改步数 $\le 50$ 步，超限判定为 `BLOCKED` 并汇报，不可死磕消耗 Token。

### 单模块交付 4 项硬门禁指标
- ① **原文件备份**：`<stem>_.zig` 必须完好无损；
- ② **编译与 AST 校验**：`cpc --lib <stem>.cp -o <stem>.zig && vendor/zig/zig ast-check <stem>.zig` 0 报错；
- ③ **静态零违例**：0 `zig {}` 逃逸、0 `var`、0 `.@"0"`/`.@"1"`、0 `*anyopaque`/`?*anyopaque`；
- ④ **代码膨胀率**：`<stem>.cp` 总行数 $\le 1.20\times$ `<stem>_.zig` 原始行数。
