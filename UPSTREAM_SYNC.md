# CP-Bun 上游演进同步追踪表

- **基线 Tag**：`cp-v1.3.14-green` (`907c8096077`)
- **目标路线**：`#30412` (`23427dbc12f`) → `a215285063c` (935 commits) → `consolidation-step-1..7` → `bun-v1.4.0` → `bun-v1.4.1..1.4.5`

| 阶段 | 上游区间 | 状态 | 说明 |
| :--- | :--- | :--- | :--- |
| Step 1 | `cp-v1.3.14-green` & `#30412` 坐标系 | **进行中** | 已建 `../bun-ref` 只读 worktree 与 `MODULE_MAP.md` |
| Step 2 | `23427dbc12f..a215285063c` (935 commits) | 待推进 | 聚焦泄漏/UAF/竞态/边界/Win+Linux 运行时 Bugfix |
| Step 3 | `a215285063c..bun-v1.4.0` (1097 commits) | 待推进 | `consolidation-step-1..7` 路径映射更新 + 1.4.0 收口 |
| Step 4 | `bun-v1.4.0..bun-v1.4.5` (1501 commits) | 待推进 | 小步快跑逐版本吸收至 CP 1.4.5 |
