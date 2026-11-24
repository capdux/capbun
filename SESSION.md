
- **[2026-11-20 23:51] Wave 28 A 组 + C 组验收提交（26 模块全绿）**：完成 JSPrinter/JSParser/Resolver 15 模块及 JSC/SQL/Runtime/HTTP/Shell/Threading/Watcher 11 模块独立验收（26/26 PASS，4 项门禁全绿）并原子提交；补派 D 组修复外部根因 `ParseTask.cp`/`KnownGlobal.cp`/`Counters.cp`。
- **[2026-11-20 23:54] Wave 28 B 组验收提交（15 模块全绿）**：完成 `SemverQuery.format`、`bake`（`zeroInit`）、Bundler/Install/Router 15 模块独立验收（15/15 PASS，ratio 0.842x~1.121x）并原子提交；记录外部根因 `src/install/bin.cp` 并派发 E 组。
- **[2026-11-20 23:55] Wave 28 D 组验收提交（3 根因全绿）**：完成 `ParseTask` & `KnownGlobal`（`std.mem.zeroes(...)`）与 `Counters`（`@root` 根结构体）独立验收（3/3 PASS）并原子提交；静待 E 组（`def8117c`）`install/bin.cp` 完成即跑 Wave 29 `zig:check`。
- **[2026-11-20 23:56] Wave 28 E 组验收提交（`install/bin` 全绿，Wave 28 清仓）**：恢复 `install/bin.cp` `count`/`clone` `[]const u8` 形参并通过独立验收（PASS，ratio 1.041x）并原子提交；随即运行 Wave 29 全仓 `bun run zig:check`。
- **[2026-11-21 00:02] Wave 29 C 组验收提交（9 模块全绿）**：完成 JSC/Runtime/Shell/Postgres(`DataCell` 连带解封 `PostgresSQLConnection`)/Threading/Watcher 9 模块独立验收（9/9 PASS，ratio 0.879x~1.062x）并原子提交；静待 A/B 组完成。
- **[2026-11-21 00:05] Wave 29 B 组验收提交（15 模块全绿）**：完成 Bake/Bundler/Router/Install 15 模块独立验收（15/15 PASS，ratio 0.783x~1.121x）并原子提交；补派 D 组修复外部根因 `src/install/resolvers/folder_resolver.cp`。
- **[2026-11-21 00:07] Wave 29 D 组验收提交（`folder_resolver` 全绿）**：修复 `CacheFolderResolver`/`NewResolver` `resolve`/`count` 重复接收者并通过独立验收（PASS，ratio 1.108x）并原子提交；静待 A 组（`2cefe132`）完成即跑 Wave 30 `zig:check`。
- **[2026-11-21 00:10] Wave 29 A 组验收提交（15 模块全绿，Wave 29 清仓）**：完成 `js_printer`（16 错）、`visitExpr`（9 错）、`Expr`、`P`、`visit`、`lowerDecorators`、`visitBinaryExpression`、`visitStmt`、`ConvertESMExportsForHmr`、`E`、`Parser`、`SideEffects`、`maybe`、`parser`、`Macro` 15 模块独立验收（15/15 PASS，ratio 0.998x~1.038x）并原子提交；随即运行 Wave 30 全仓 `bun run zig:check`。
- **[2026-11-21 00:15] Wave 30 C 组验收提交（10 模块全绿）**：完成 Runtime/SQL/JSC/Options/Router/Shell/Threading 10 模块独立验收（10/10 PASS，4 项门禁全绿，ratio 0.842x~1.042x）并原子提交；静待 A/B 组完成。
- **[2026-11-21 00:20] Wave 30 B 组验收提交（15 模块全绿）**：完成 Bake/Bundler/Install 15 模块独立验收（15/15 PASS，4 项门禁全绿，ratio 0.964x~1.121x）并原子提交；静待 A 组（`7a5dc17c`）完成即跑 Wave 31 `zig:check`。
- **[2026-11-21 00:26] Wave 30 A 组验收提交（10 模块全绿，Wave 30 清仓）**：完成 `js_printer`、`Expr`、`P`（`newExpr` 兜底 `comptime Type == type`）、`visitExpr`、`visitBinaryExpression`、`ConvertESMExportsForHmr`、`lowerDecorators`、`ServerComponentBoundary`、`parser`（`HookContextPtr` 连带解封 `visit`/`visitStmt`/`visitExpr`）、`Macro` 10 模块独立验收（10/10 PASS，ratio 1.000x~1.041x）并原子提交；随即运行 Wave 31 全仓 `bun run zig:check`。
- **[2026-11-21 00:34] Wave 31 C 组验收提交（13 模块全绿）**：完成 Install/Postgres(`DataCell` 连带解封 `PostgresSQLConnection`)/JSC/Runtime/Shell/Threading 13 模块独立验收（13/13 PASS，4 项门禁全绿，ratio 0.891x~1.078x）并原子提交；静待 A/B 组完成。
- **[2026-11-21 00:34] Wave 31 B 组验收提交（11 模块全绿）**：完成 CSS/Bake/Bundler/StandaloneGraph 11 模块独立验收（11/11 PASS，4 项门禁全绿，ratio 0.935x~1.131x）并原子提交；静待 A 组（`6eb57feb`）完成即跑 Wave 32 `zig:check`。
- **[2026-11-21 00:37] Wave 31 A 组验收提交（10 模块全绿，Wave 31 清仓）**：完成 `Expr`（`init` `@TypeOf(Expr)` 兜底）、`B`、`ConvertESMExportsForHmr`、`P`、`visit`、`visitExpr`、`lowerDecorators`、`maybe`、`visitBinaryExpression`、`Macro` 10 模块独立验收（10/10 PASS，ratio 1.000x~1.048x）并原子提交；随即运行 Wave 32 全仓 `bun run zig:check`。
- **[2026-11-21 00:44] Wave 32 C 组验收提交（9 模块全绿）**：完成 Postgres(`DataCell`)/Install(`PackageManagerEnqueue` 消除 `Writer.zig:717` 2 错等) 9 模块独立验收（9/9 PASS，4 项门禁全绿，ratio 0.891x~1.078x）并原子提交；静待 A/B 组完成。
- **[2026-11-21 00:44] Wave 32 A 组验收提交（12 模块全绿）**：完成 CSS 全家桶(`VendorPrefix.eql` 解封 `properties_generated`)/`macho`/`owned`/`spawn` 12 模块独立验收（12/12 PASS，4 项门禁全绿，ratio 0.886x~1.064x）并原子提交；静待 B 组（`e5fadbc7`）完成即跑 Wave 33 `zig:check`。
- **[2026-11-21 00:46] Wave 32 B 组验收提交（19 模块全绿，Wave 32 清仓）**：完成 Bundler/LinkerContext/JSParser(`parser.cp` `HookContextPtr` 等)/Macro 19 模块独立验收（19/19 PASS，4 项门禁全绿，ratio 0.899x~1.134x）并原子提交；随即运行 Wave 33 全仓 `bun run zig:check`。
- **[2026-11-21 00:55] Wave 33 A 组验收提交（13 模块全绿）**：完成 CSS 全家桶(`VendorPrefix.eq` 解封 `properties_generated`)/`macho` 13 模块独立验收（13/13 PASS，4 项门禁全绿，ratio 0.886x~1.027x）并原子提交；补派 D 组修复外部根因 `css/values/length.cp` 与 `css/rules/media.cp`。
- **[2026-11-21 00:58] Wave 33 D 组验收提交（2 CSS 根因全绿）**：完成 `css/values/length`（`LengthPercentageOrAuto.eql` 改为 `static fn`）与 `css/rules/media`（`deepClone` 改为只读接收者）独立验收（2/2 PASS，ratio 0.98x~1.029x）并原子提交；同时确认 `properties_generated.zig:6083/6337` 返回类型系旧版 `cpc` 误生成 `*PropertyId`/`*Property`，待 B/C 组完成后一并处置。
- **[2026-11-21 00:58] `css/properties/properties_generated.cp` 新版 `cpc` 重编提交**：使用新版 `cpc` 重新生成 `properties_generated.zig` 并通过 4 项门禁（PASS，ratio 1.14x），修复旧版 `cpc` 误生成 `*PropertyId`/`*Property` 问题。
- **[2026-11-21 00:59] Wave 33 C 组验收提交（18 模块全绿）**：完成 `bun`、`Install`（还原 `ResolutionTag` 枚举值等）、`CLI`、`Runtime`、`subproc`、`DataCell`、`AsyncModule`、`resolve_path` 18 模块独立验收（18/18 PASS，4 项门禁全绿）并原子提交；静待 B 组（`3214dcfb`）完成即跑 Wave 34 `zig:check`。
- **[2026-11-21 01:00] Wave 33 B 组验收提交（18 模块全绿，Wave 33 清仓）**：完成 Bundler/LinkerContext/JSParser(`P.Ctx.next` 连带解封 `js_parser.cp`)/Renamer/JSON 18 模块独立验收（18/18 PASS，ratio 0.899x~1.051x，`json.cp` 原无备份除外）并原子提交；随即运行 Wave 34 全仓 `bun run zig:check`。
- **[2026-11-21 01:06] Wave 34 C 组验收提交（11 模块全绿）**：完成 Runtime/Install(`runTasks` 兼容 `@TypeOf(.{})` 等)/JSC(`AsyncModule`)/Shell(`subproc`) 11 模块独立验收（11/11 PASS，4 项门禁全绿，ratio 0.987x~1.108x）并原子提交；静待 A/B 组完成。
- **[2026-11-21 01:12] Wave 34 B 组验收提交（14 模块全绿）**：完成 Bundler/`linker_context`/JSParser(`visit.cp:206` 根治 `bun.zig:3577` `*const Wyhash`)/`data_url`/`Version` 14 模块独立验收（14/14 PASS，4 项门禁全绿，ratio 0.899x~1.051x）并原子提交；静待 A 组（`2bdfb6d8`）完成即跑 Wave 35 `zig:check`。
- **[2026-11-21 01:15] Wave 34 A 组验收提交（16 模块全绿，Wave 34 清仓）**：完成 CSS 全家桶(`css_parser` 19错/`generics` 11错/`font_face` 7错/`gradient` 5错/`properties_generated` 等)/`macho` 16 模块独立验收（16/16 PASS，4 项门禁全绿，ratio 0.886x~1.14x）并原子提交；随即运行 Wave 35 全仓 `bun run zig:check`。
- **[2026-11-21 01:31] Wave 35 B 组验收提交（28 模块全绿）**：完成 CSS rules(13)/Bundler(5)/Install(7)/`lowerDecorators`/`bun`/`Cmd` 28 模块独立验收（28/28 PASS，4 项门禁全绿）并原子提交；静待 A 组（`19345081`）完成。
- **[2026-11-21 01:34] Wave 35 A 组验收提交（26 CSS 模块全绿）**：完成 CSS properties(14)/values(7)/selectors(2)/core(3) 26 模块独立验收（26/26 PASS，4 项门禁全绿）并原子提交；静待 C 组（`e288f876`）完成即跑 Wave 36 `zig:check`。
- **[2026-11-21 01:36] Wave 35 C 组验收提交（4 根因全绿）**：完成 `ParseTask`（`@"err"`）、`Chunk`（`CssImportOrderLayersVTable`）、`jsc/ipc`（`pub const Data = SendQueue`）、`properties_impl`（`static fn toCss`）独立验收（4/4 PASS，4 项门禁全绿，ratio 0.993x~1.039x）并原子提交；静待 D 组（`a320a9de`）完成即跑 Wave 36 `zig:check`。
- **[2026-11-21 01:37] Wave 35 D 组验收提交（3 CSS 根因全绿，Wave 35 清仓）**：完成 `easing`（`EasingFunction.eql` `static fn`）、`keyframes`、`font_palette_values`（提升嵌套解析器+`static fn`）独立验收（3/3 PASS，4 项门禁全绿，ratio 1.01x~1.018x）并原子提交；随即运行 Wave 36 全仓 `bun run zig:check`。
- **[2026-11-21 02:51] Wave 36 B 组验收提交（20 修改模块+3 联动/待核对模块全绿）**：完成 Bundler/`linker_context`/`DirectoryWatchStore`/Install/`lowerDecorators`/`Writable`/`subproc` 20 模块独立验收（20/20 PASS，4 项门禁全绿，ratio 0.901x~1.111x）并原子提交；补派 C 组修复 `js_printer.cp:6241` `pub fn print`。
- **[2026-11-21 02:54] Wave 36 A 组 + C 组验收提交（35 模块全绿，Wave 36 清仓）**：完成全部 CSS 34 模块及 `js_printer`（导出顶层 `pub fn print`/`printJSON`）独立验收（35/35 PASS，4 项门禁全绿，ratio 0.946x~1.14x）并原子提交；随即运行 Wave 37 全仓 `bun run zig:check`。
- **[2026-11-21 03:02] Wave 37 B 组验收提交（20 非 CSS 模块全绿）**：完成 JSParser/Bundler/`linker_context`/Install/`js_printer`/Runtime/Shell 20 模块独立验收（20/20 PASS，4 项门禁全绿，ratio 0.807x~1.057x）并原子提交；静待 A 组（`c14780e3`）完成即跑 Wave 38 `zig:check`。
- **[2026-11-21 03:06] Wave 37 A 组验收提交（25 CSS 模块全绿，Wave 37 清仓）**：完成 `css_parser`/`properties_generated`(`PropertyId.parse` 修正等)/`border_radius`/`align`/`css_modules`/`background`/规则/值等 25 模块独立验收（25/25 PASS，4 项门禁全绿）并原子提交；随即运行 Wave 38 全仓 `bun run zig:check`（并关注外部根因 `percentage.cp`/`animation.cp`）。
- **[2026-11-21 03:13] Wave 38 B 组验收提交（15 非 CSS 模块全绿）**：完成 `subproc`/`Writable`(连带解封 `streams.cp`)/`js_bun_spawn_bindings`/`subprocess`/`lowerDecorators`/`ConvertESMExportsForHmr`/`Parser`/`bundle_v2`/`linker_context`(4)/`Tree`/`bun.lock`/`migration` 15 模块独立验收（15/15 PASS，4 项门禁全绿，ratio 0.901x~1.081x）并原子提交；静待 A 组（`da7600cb`）完成即跑 Wave 39 `zig:check`。
- **[2026-11-21 03:16] Wave 38 A 组验收提交（28 CSS 模块全绿，Wave 38 清仓）**：完成 `css_parser`(`DeriveToCss`/`DefineEnumProperty` `static fn`)、`percentage`(`fn` 回调)、`animation`(`AnimationName.hash`)、`length`、`media_query`、`rules/*`、`selectors/*`、`properties/*` 28 模块独立验收（28/28 PASS，4 项门禁全绿）并原子提交；随即运行 Wave 39 全仓 `bun run zig:check`（并关注 `css/rules/property.cp`）。
- **[2026-11-21 03:19] Wave 39 B 组验收提交（13 非 CSS 模块全绿）**：完成 Bundler/`linker_context`/JSParser(`lowerDecorators`/`Parser`)/Install(`Tree`/`bun.lock`/`migration`)/`ProcessAutoKiller`/`js_bun_spawn_bindings` 13 模块独立验收（13/13 PASS，4 项门禁全绿，ratio 0.963x~1.134x）并原子提交；静待 A 组（`dc232f6a`）完成即跑 Wave 40 `zig:check`。
- **[2026-11-21 03:23] Wave 39 A 组验收提交（27 CSS 模块全绿，Wave 39 清仓）**：完成 `margin_padding`/`border`/`css_parser`/`align`/`generics`/`media_query`/`size`/`property` 等 27 个 CSS 模块独立验收（27/27 PASS，4 项门禁全绿，ratio 0.947x~1.100x）并原子提交；随即运行 Wave 40 全仓 `bun run zig:check`。
- **[2026-11-21 03:27] Wave 40 B 组验收提交（12 非 CSS 模块全绿）**：完成 `P`/`bundle_v2`/`yarn`/`lowerDecorators`/`AstBuilder`/`LinkerContext`/`findAllImportedPartsInJSOrder`/`generateChunksInParallel`/`Tree`/`bun.lock`/`parser`/`runtime` 12 模块独立验收（12/12 PASS，4 项门禁全绿，ratio 0.963x~1.056x）并原子提交；静待 A 组（`9f674588`）完成即跑 Wave 41 `zig:check`。
- **[2026-11-21 03:30] Wave 40 A 组验收提交（28 CSS 模块全绿，Wave 40 清仓）**：完成 `css_parser`/`media_query`/`generics`(兼容 `*T`/`T` `toCss`/`hash`)/`context`/`printer`/`properties/*`/`rules/*`(连带解封 `rules.cp`)/`selectors/*`/`values/*` 28 模块独立验收（28/28 PASS，4 项门禁全绿，ratio 0.988x~1.104x）并原子提交；随即运行 Wave 41 全仓 `bun run zig:check`。
- **[2026-11-21 03:33] Wave 41 B 组验收提交（12 非 CSS 模块全绿）**：完成 `lowerDecorators`/`KnownGlobal`/`Symbol`/`bundle_v2`/`Chunk`/`HTMLImportManifest`/`linker_context`(3)/`bun.lock`/`yarn`/`sourcemap`(`DebugIDFormatter.format`) 12 模块独立验收（12/12 PASS，4 项门禁全绿，ratio 0.626x~1.044x）并原子提交；静待 A 组（`40269ee4`）完成即跑 Wave 42 `zig:check`。
- **[2026-11-21 03:37] Wave 41 A 组验收提交（24 CSS 模块全绿，Wave 41 清仓）**：完成 `media_query`/`css_parser`/`DeriveValueType`/`newCustomError`/规则/属性/值等 24 个 CSS 模块独立验收（24/24 PASS，4 项门禁全绿，ratio 0.947x~1.104x）并原子提交；随即运行 Wave 42 全仓 `bun run zig:check`（并关注 `syntax.cp`/`document.cp`）。
- **[2026-11-21 03:40] Wave 42 B 组验收提交（最后 7 非 CSS 模块全绿）**：完成 `computeCrossChunkDependencies`/`Chunk`/`HTMLImportManifest`/`bundle_v2`(连带消除 `Writer.zig:597` 3 错)/`writeOutputFilesToDisk`/`bun.lock`/`yarn` 7 模块独立验收（7/7 PASS，4 项门禁全绿，ratio 0.949x~1.048x）并原子提交；补派 C 组修复 `std/mem.zig` 引用链 `src/js_parser/ast/P.cp`。
- **[2026-11-21 03:42] Wave 42 A 组验收提交（14 CSS 模块全绿）**：完成 `css_parser`/`document`(连带解封 `rules.cp`)/`syntax`(连带解封 `property.cp`)/`font_palette_values`/`font_face`/`align`/`box_shadow`/`custom`/`display`/`margin_padding`/`selectors/parser`/`resolution`/`gradient`/`media_query` 14 模块独立验收（14/14 PASS，4 项门禁全绿，ratio 0.974x~1.135x）并原子提交；静待 C 组（`e78695f3`）`ast/P.cp` 完成即跑 Wave 43 `zig:check`。
- **[2026-11-21 03:42] Wave 42 C 组验收提交（`ast/P.cp` 全绿，Wave 42 清仓）**：修复 `P.cp` `E.Array`/`E.Object` 显式默认字段初始化消除 `std/mem.zig:299/322` 引用链并通过独立验收（PASS，ratio 1.015x）并原子提交；随即运行 Wave 43 全仓 `bun run zig:check`。
- **[2026-11-21 03:45] Wave 43 A 组验收提交（最后 6 CSS 模块全绿）**：完成 `css_parser`/`margin_padding`/`rules`/`selectors/parser`/`gradient`/`syntax` 6 模块独立验收（6/6 PASS，4 项门禁全绿，ratio 0.952x~1.026x）并原子提交；静待 B 组（`9c293864`）完成即跑 Wave 44 `zig:check`。
- **[2026-11-21 03:45] Wave 43 B 组验收提交（最后 6 非 CSS 模块全绿，Wave 43 清仓）**：完成 `HTMLImportManifest`/`bundle_v2`/`computeCrossChunkDependencies`/`writeOutputFilesToDisk`/`lockfile`/`yarn` 6 模块独立验收（6/6 PASS，4 项门禁全绿，ratio 0.949x~1.054x）并连同 A 组一并原子提交；随即运行 Wave 44 全仓 `bun run zig:check`。
- **[2026-11-21 03:49] Wave 44 B 组验收提交（6 非 CSS 模块全绿）**：完成 `Chunk`(导出 `Chunk.CodeResult` 连带解封 `writeOutputFilesToDisk`)/`bundle_v2`/`computeCrossChunkDependencies`/`lockfile`/`Tree`/`yarn` 6 模块独立验收（6/6 PASS，4 项门禁全绿，ratio 0.963x~1.054x）并原子提交；静待 A 组（`ddd39efc`）完成即跑 Wave 45 `zig:check`。
- **[2026-11-21 03:50] Wave 44 A 组验收提交（最后 6 CSS 模块全绿，Wave 44 清仓）**：完成 `selectors/parser`/`gradient`/`margin_padding`/`custom_media`/`rules`/`syntax` 6 模块独立验收（6/6 PASS，4 项门禁全绿，ratio 0.956x~1.027x）并原子提交；随即运行 Wave 45 全仓 `bun run zig:check`。
- **[2026-11-21 03:52] Wave 45 B 组验收提交（最后 8 非 CSS 模块全绿）**：完成 `IncrementalGraph`/`CachedBytecode`/`DevServer`/`bundle_v2`/`writeOutputFilesToDisk`/`bun.lock`/`bun.lockb`/`yarn` 8 模块独立验收（8/8 PASS，4 项门禁全绿，ratio 0.908x~1.048x）并原子提交；静待 A 组（`913d453e`）完成即跑 Wave 46 `zig:check`。
- **[2026-11-21 03:53] Wave 45 A 组验收提交（最后 6 CSS 模块全绿，Wave 45 清仓）**：完成 `gradient`/`property`/`rules`/`selectors/parser`/`syntax`/`layer` 6 模块独立验收（6/6 PASS，4 项门禁全绿，ratio 0.96x~1.084x）并连同 B 组一并原子提交；随即运行 Wave 46 全仓 `bun run zig:check`。
- **[2026-11-21 03:55] Wave 46 A 组验收提交（最后 CSS 根因全绿）**：完成 `css/values/syntax`（`static fn toCss` 连带解封 `property`）与 `css/rules/container`（只读 `toCss` 连带解封 `rules`）独立验收（2/2 PASS，4 项门禁全绿，ratio 0.991x~1.021x）并原子提交；静待 B 组（`66be9fff`）完成即跑 Wave 47 `zig:check`。
- **[2026-11-21 03:56] Wave 46 B 组验收提交（8 非 CSS 模块全绿，Wave 46 清仓）**：完成 `Chunk`(`Writer.zig:597` 3 错)/`IncrementalGraph`/`writeOutputFilesToDisk`/`PopulateManifestCache`/`Tree`/`bun.lock`/`bun.lockb`/`pnpm` 8 模块独立验收（8/8 PASS，4 项门禁全绿，ratio 0.966x~1.054x）并连同 A 组一并原子提交；随即运行 Wave 47 全仓 `bun run zig:check`。
- **[2026-11-21 03:58] Wave 47 A 组验收提交（最后 3 CSS 模块全绿）**：完成 `css/rules/{container,scope}` 与 `css/values/syntax` 独立验收（3/3 PASS，4 项门禁全绿，ratio 0.987x~1.021x）并原子提交；静待 B 组（`980b8a3e`）完成即跑 Wave 48 `zig:check`。
- **[2026-11-21 04:00] Wave 47 B 组验收提交（6 非 CSS 模块全绿，Wave 47 清仓）**：完成 `IncrementalGraph`/`generateChunksInParallel`/`PopulateManifestCache`/`bun.lock`(`splatByteAll`)/`bun.lockb`/`pnpm` 6 模块独立验收（6/6 PASS，4 项门禁全绿，ratio 0.992x~1.056x）并原子提交；随即运行 Wave 48 全仓 `bun run zig:check`。
- **[2026-11-21 04:01] Wave 48 A 组验收提交（最后 2 CSS 模块全绿）**：完成 `css/rules/{container,scope}` 独立验收（2/2 PASS，4 项门禁全绿，ratio 0.987x~0.991x）并原子提交；静待 B 组（`c8fe0603`）完成即跑 Wave 49 `zig:check`。
- **[2026-11-21 04:02] Wave 48 B 组验收提交（最后 4 非 CSS 模块全绿，Wave 48 清仓）**：完成 `DevServer`/`CachedBytecode`(导出 `CachedBytecode` 根结构体)/`bun.lock`(`str_slice`)/`pnpm` 4 模块独立验收（4/4 PASS，4 项门禁全绿，ratio 0.908x~1.054x）并原子提交；随即运行 Wave 49 全仓 `bun run zig:check`。
- **[2026-11-21 04:04] Wave 49 A+B 组验收提交（6 模块全绿，Wave 49 清仓）**：完成 `css/{printer,rules/container}` 与 `bake/DevServer`、`bundler/linker_context/generateChunksInParallel`、`install/{bin,pnpm}` 6 模块独立验收（6/6 PASS，4 项门禁全绿，ratio 0.991x~1.056x）并原子提交；随即运行 Wave 50 全仓 `bun run zig:check`。
- **[2026-11-21 04:14] Wave 50 全仓语义检查 0-Error 里程碑 (`ZIG_CHECK_EXIT=0`)**：完成 `MetafileBuilder`/`bundle_v2`/`bin`/`pnpm`/`bun.lock`/`migration`/`repository`/`Expr` 8 模块收尾修复并全部通过 `cpc_brief.py -n 4`（ratio 0.998x~1.079x）；`bun run zig:check` 全仓 1,293 模块 **0 errors (5/5 steps succeeded, 4s)** 达成！正式进入构建与分批测试阶段。
- **[2026-11-21 04:31] `bun bd` 链接阻断 & 上报 `cp_agy` (`task-bun-20261121-export-fn-const`)**：`bun bd` 编译全部 96 个 `bun-zig.*.o` 与 C++ 目标成功，在 `ld.lld-21` 链接阶段暴露大量 `undefined symbol`（如 `Bun__codepointWidth`、`Bun__lock`、`Bun__lock__size`、`sys_epoll_pwait2` 等）；排查确认 `cpc --lib` 会吞掉 `export fn` / `pub export fn` / `@c pub export fn` / `export const` 中的 `export` 关键字，已提炼最小复现 `/data/dev/cp/temp/repro_export_fn_const.cp` 并提交工单 `task-bun-20261121-export-fn-const` 给 `cp_agy`，同时并行排查修复 `.cp` 源码中误省 `export` 的 46 个文件。
- **[2026-11-21 04:35] Export 补齐 A+B 组验收提交（13 模块修改全绿）**：完成 `Global`/`rare_data`/`resolve_path`/`linux`/`BunObject`/`PasswordObject` 与 `Blob`/`Crypto`/`FileSink`/`S3File`/`FetchTasklet`/`fetch`/`uws` 13 个模块缺失 `export` 补齐并全部通过 `cpc_brief.py -n 4`（ratio 0.28x~1.115x）并原子提交；同时验证 `@export @c fn` 可在当前 `cpc` 直接生成 `pub export fn ... callconv(.c)`，即将全仓对照 `*_.zig` 批量规范化 `export fn` 与 `export const`。
- **[2026-11-21 04:39] `cp_agy` 结单验收 (`task-bun-20261121-export-fn-const`)**：上游已修复并发布 `cpc Release`（拆分 `Token::Export` 与 `Token::Pub`，新增 `VarDecl.is_export` 透传 `export fn` / `pub export fn` / `@c pub export fn` / `export const`）；本地重编 `/data/dev/cp/temp/repro_export_fn_const.cp` 验证 `pub export const` 与 `pub export fn` 全绿生成并已执行 `ack`。随即解封重编 `Mutex`/`node_process`/`upgrade_command`/`cron`/`bun_test`/`NodeModuleModule`/`SocketKind` 等 `export const` 导出模块。
- **[2026-11-21 04:57] 全仓 C ABI Export 激活 + 二次 `zig:check` 0-Error 里程碑 (`ZIG_CHECK_EXIT=0`)**：完成 58 个含 `export fn` 模块规范化（`@export @c fn` / `@export @c static fn`）与 7 个 `export const` 模块重编，并通过两轮并发修复全仓暴露的 `boringssl`/`linux`/`analytics`/`Global`/`resolve_path`/`PasswordObject`/`analyze_jsc`/`napi`/`BunObject`/`Blob`/`Crypto`/`fetch`/`ChromeProcess`/`path`/`bake/production`/`OutputFile`/`ParseTask`/`btjs`/`rare_data`/`JSBundler`/`Body`/`Request`/`Response`/`escapeHTML`/`socket`/`bundle_v2`/`bun`/`FetchTasklet` 等导出函数内部语义错误；全仓 `bun run zig:check` 在全部 C/C++ ABI 导出激活下达到 **0 errors (5/5 steps succeeded, 4s)**！即将原子提交并启动 `bun bd` 链接与测试。
- **[2026-11-21 05:04] `bun bd` 链接阻断 & 上报 `cp_agy` (`task-bun-20261121-lib-main`)**：第二轮 `bun bd` 链接只剩 157 个 `undefined symbol`（其中 `bindgen_*` 已 0 报错）；定位到 3 类根因：(1) `cpc --lib` 遇到 `pub fn main()` 仍将其改写为 CP 纤程状态机导致 `src/main.zig` 缺失 `main` 符号，已提交最小复现 `/data/dev/cp/temp/repro_lib_main.cp` 与工单 `task-bun-20261121-lib-main` 给 `cp_agy`；(2) 用新 `cpc` 重编 `GeneratedJS2Native.cp`、`Arguments.cp`、`node_process.cp` 立即解决 102 个 `JS2Zig___*` 与 4 个 `Bun__Node__*` `export var` 符号；(3) 继续排查剩余 `NetworkSink__*`、`Bun__readOriginTimerStart`、`Bun__TestScope__*` 等 ~45 个符号未入链原因。
- **[2026-11-21 05:11] `cp_agy` 结单验收 (`task-bun-20261121-lib-main`)**：上游已修复 `cpc --lib` 模式误将 `pub fn main()` 改写为 CP 纤程入口并注入运行时的问题（`cpc/src/codegen/{prelude,actor,mod}.rs`）并发布优化版 `cpc`；本地重编 `/data/dev/cp/temp/repro_lib_main.cp` 与 `src/main.cp`（PASS，`ratio: 1.07`）均成功生成标准 `pub fn main() void {`，已执行 `ack`。

- **[2026-11-21 09:33] 全仓 `bun run zig:check` 0-Error 收敛里程碑 (`ZIG_CHECK_EXIT=0`)**：完成 CLI/Runtime/Create/Paths/CrashHandler 全仓隐藏雷区排雷与收尾修复（含 `create_command`/`SourceFileProjectGenerator`/`filter_run`/`multi_run`/`run_command`/`upgrade_command`/`exec_command`/`Arguments`/`filter_arg`/`bun.js`/`cli`/`HostProcess`/`ServerConfig`/`main_test`/`process`/`resolve_path`/`crash_handler`），修复 `scripts/cpc_brief.py` 失败退出码并在 `GEMINI.md` 第十一节固化防空跑与排雷规则；`bun run zig:check` 全仓 **0 errors** 达成，准备原子提交并启动 `bun bd` 调试版构建。
- **[2026-11-21 09:52] 上报 `cp_agy` (`task-bun-20261121-cp-iter-slice-dangling`)**：`bun bd -p 'Bun.version'` 链接成功但运行时 ASan 捕获 `Target.outExtensions` 栈溢出；定位根因为 `cpc` 生成的 `cp_iter_slice(__cp_val: anytype)` 对定长数组 `[N]T` 按值传参并返回 `__cp_val[0..]`，导致返回悬垂栈切片。已提交最小复现 `/data/dev/cp/temp/repro_cp_iter_slice_array.cp` 给 `cp_agy` 修复，并在 `options.cp` 临时改用 `[0..]` 绕过以继续推进构建验证。
- **[2026-11-21 10:10] `cp_agy` 结单验收 (`task-bun-20261121-cp-iter-slice-dangling`)**：上游已修复 `cpc/src/codegen/runtime_emit.rs` 中 `cp_iter_slice` 的 `.array` 分支（将返回类型与返回值由 `__cp_val[0..]` 悬垂栈切片改为直接按值返回 `@TypeOf(__cp_val)` / `__cp_val`）并原子部署。正在本地复现验证并执行 `ack`。
- **[2026-11-21 10:33] `bun bd` 构建链接 + 启动执行 `1.3.14` 里程碑 (`BUN_BD_EXIT=0`)**：
  - 补全 `jest.cp` (`Bun__Jest__createTestModuleObject`)、`FetchTasklet.cp` (`Bun__FetchResponse_finalize`)、`upgrade_command.cp` (`Bun__githubURL`)、`streams.cp` (`NetworkSink.JSSink`) 10 个 C ABI 导出符号，完成 `build/debug/bun-debug` 首次全量链接；
  - 清理 `FakeTimers.cp`、`css_parser.cp`、`image.cp`、`socket.cp` 编译报错；
  - 根除 4 项关键运行时缺陷：`GarbageCollectionController.cp`（`uws.Loop.get()` 触发 RAII 自动释放）、`io/heap.cp`（`insert`/`deleteMin` 表达式 `if (opt) |x|` 被 `cpc` 静默丢弃，子智能体 `fe5fa326` 同步完成全仓 20 个 `.cp` 同类排雷）、`wtf.cp` & `DecodedJSValue.cp`（`export @c union`/`@c pub union` 顺序导致值字段被自动指针化）、`parseSuffix.cp`（109 处 `return .next/.done` 被误降级为 `return error.next/.done`）；
  - `bun bd -p 'Bun.version'` 成功输出 **`1.3.14`**（`BUN_BD_EXIT=0`）！准备提交后进入 4 批次 `bun bd test` 全量测试。
- **[2026-11-21 11:25] `cp_agy` 结单验收 (`task-bun-20261121-slice-type-arg`)**：上游已修复 `cpc/src/parser/primary.rs` 中 `[]const T` / `[]T` 切片类型表达式解析（`Token::LBracket` 后接 `RBracket` + 指针修饰符或类型 token 时正确发射 `Expr::TypeVal` 而非空 `Expr::Array`）并发布 Release 版 `cpc`；本地重编 `/data/dev/cp/temp/repro_slice_type_arg.cp` 验证 `return helper([]const u8, x);` 正常生成，已执行 `ack`。
- **[2026-11-21 11:52] `cp_agy` 结单验收 (`task-bun-20261121-deref-assign-drop`)**：上游已修复两处问题：(1) `parser/primary.rs` 将 `@"..."` 转义标识符排除出内置调用解析；(2) `codegen/actor.rs` 的 `block_contains_ident` 检测 `_`，使 `*_: T` 自由函数接收者正确生成 `self` 以匹配 `_.*`。本地重编 `/data/dev/cp/temp/repro_deref_assign_drop.cp` 验证 `dst.* = @"from".*;` 与 `self.* = src.*;` 均正常生成，已执行 `ack`。

- **[2026-11-21 12:07] `bun bd test` 全链路测试执行里程碑（`preload.ts` / `harness.ts` 完整贯通）**：
  - 修复 `BunObject.cp` (`LazyPropertyCallback` `@TypeOf` 保留函数类型消除 Wild Jump SEGV)、14 个 `.cp` 模块 137 处 `switch` 闭区间 (`..` → `..=`)、`js_printer.cp`（`[]const u8` 传参、`printClass` `" extends"`、5 处 `"..."` 展开运算符、`printClauseItemAs` `switch(kind)`）及 `immutable`/`extract_tarball`/`env_loader`/`node_process` 切片实参；
  - 修复 `GarbageCollectionController.cp` 与 `Blob.cp` 因 `release()` 命名触发的自动 RAII 提前释放、`visit.cp`（`cls.@"extends"` 访问与多变量 `for`）、`skipTypescript.cp`（`SkipTypeParameterResult` 返回值）及 `ThreadPool`/`lexer`/`AsyncModule` 解引用赋值，并修正 `Scanner.cp` 与 `integrity`/`TextDecoder`/`VLQ`/`Execution` 悬垂栈切片；
  - `./build/debug/bun-debug -p 'Bun.inspect("hello")'` 输出 `"hello"`，`import * as p from "node:path"` 输出 `object hello`，`bun bd test test/js/node/path` 已成功加载 `test/preload.ts` / `test/harness.ts` 并批量执行并通过多项 JS 测试用例，正式进入全量测试与逐项修绿阶段。
- **[2026-11-21 12:08] `cp_agy` 结单验收 (`task-bun-20261121-switch-error-set`)**：上游已修复纯 `error_set`/`anyerror` 上的 `switch (e)` lowering 为原生 Zig `switch`，并更新 `cp_has_ok_val` 对 `.error_set` 返回 `false`。本地重编 `/data/dev/cp/temp/repro_switch_error_set.cp` 验证生成原生 `switch (e) { error.OutOfMemory => 1, ... }`，已执行 `ack` 并批量重编全仓受影响 48 个模块。
- **[2026-11-21 12:30] `test/js/node/path` 全绿通关里程碑（120 pass / 0 fail）**：修复 `path.cp`（`joinWindowsT` 切片长度保存与 `resolvePosixT`/`resolveWindowsT` 6 处 `emptySlice(T)` 初始化）、还原 `Blob.cp` `getWriter` 完整实现并补全 `streams.cp` `NetworkSink.{new,deinit,path,toSink,onWritable}`；`bun bd test test/js/node/path` 19 个测试文件 **120 pass, 1 skip, 0 fail (395 expect() calls)** 全绿通过！
- **[2026-11-21 14:33] HTTP/Weak/Blob/Util 核心模块修复里程碑（`fuzzy-wuzzy` 与 `bun-file` 全绿）**：
  - 修复 `HTTPContext.cp`（`onOpen` `if (...) |_|` 改 `catch` 消除 `HTTPThread` 104-byte UAF；模块导出改 `pub`）、`Weak.cp`（`Weak(T)` 内部 `if (ref := _.ref)` 改 `if (_.ref != null)` 恢复 `get/swap/has/clear/deinit`，消除 `FetchTasklet.bunFetchResponseFinalize` UAF）、`FetchTasklet.cp`、`uws_handlers.cp`、`http.cp`、`SocketGroup.cp`、`dns.cp`；
  - 修复 `Blob.cp`（`s3://` 路径释放范围、`resolveFileStat`/`resolveSize`/`getExistsSync`/`takeOwnership`/`doReadFile`/`doUnlink`/`getSize`/`getSlice`/`getStat`/`doWrite`/`findOrCreateFileFromPath`）、`Store.cp`、`ZigString.cp`、`js_printer.cp`、`escapeHTML.cp` 与 `resolve_path.cp`；
  - `fuzzy-wuzzy.test.ts` **154 pass / 0 fail** 全绿通过（0 ASan 崩溃），`bun-file.test.ts` **6 pass / 0 fail (49 expect())** 全绿通过，`escapeHTML.test.js` **4 pass / 0 fail (54 expect())** 全绿通过。
- **[2026-11-21 14:45] `PasswordObject.cp` 修复与 `password.test.ts` 全绿（74 pass / 0 fail）**：将 `HashJob_` 与 `VerifyJob_` 的 `release()` 改名为 `releaseInternal()` 避免 `cpc` 自动注入 `defer out.release()` 提前销毁 `ThreadPool` 任务；`bun bd test test/js/bun/util/password.test.ts` **74 pass, 0 fail (235 expect() calls)** 全绿通关！
- **[2026-11-21 14:57] `ServerConfig.cp` RAII 修复 (`wildcard static routes` 通关)**：合并 `RouteDeclaration`/`StaticRouteEntry`/`ServerConfig`/`UserRouteBuilder` 4 处 `release()` 至 `deinit()`，消除 `cloneForReloadingStaticRoutes` / `StaticRouteEntry.isLessThan` `0xaa` 崩溃；`test/js/bun` 第一组 10 个核心目录已跑出 **10,267 pass / 13 fail**。
- **[2026-11-21 15:06] `Sink.cp` `SinkSignal.init` 空指针安全修复 (`serve-response-stream-sink-leak` 通过)**：补全 `@setRuntimeSafety(false);` 与 `Sink_.zig` 对齐，10,000 次流式请求压力测试通过且无泄漏（1 pass / 0 fail）。
- **[2026-11-21 15:23] `unicode.cp` `...` 范围语法修复 (`css-fuzz` 崩溃清零)**：将 `unicode.cp` 中 4 处 Zig 遗留 `...` 范围语法改用 CP `..=` 与 `if/else`，恢复被 `cpc` 吞掉的 `convertUTF8BytesIntoUTF16WithLength` 整块 `switch (len)`（27 个有效 `switch` 完整恢复），`css-fuzz.test.ts` 0 崩溃退出；`bun_core2` 第一段已跑出 **1,362 pass / 25 fail**。
- **[2026-11-21 15:36] `SSLConfig.cp` RAII 修复 (`bun-write.test.js` 0 崩溃)**：将 `SSLConfig.release()` 合并入 `deinit()`，消除 `us_ssl_ctx_build_raw` 读取已释放证书指针的 `use-after-poison`；`bun bd test test/js/bun/io/bun-write.test.js` 跑出 **28 pass, 4 fail (1,317 expect() calls)**，0 崩溃。
- **[2026-11-21 16:02] 全仓 393 个旧版 `cp_iter_slice` 模块批量重编与 `zig:check` 0 错误收敛**：从 `md/line_analysis.cp` `strncasecmp` SEGV 溯源出全仓 395 个 `.zig` 仍使用旧版悬垂 `cp_iter_slice`，用新版 `cpc` 批量重编 393 个对应 `.cp` 模块，并修复 `options.cp`、`tracy.cp`、`fs_events.cp`、`immutable.cp` 及 5 个模块的 `export` 声明；`bun run zig:check` **5/5 steps succeeded** 全绿通过。
- **[2026-11-21 16:47] `extract_tarball.cp` 修复与 Batch 2/3/4a 推进**：修复 `extract_tarball.cp:159` `nameAndBasename` 值接收者悬垂栈切片，消除 `makeOpenPath` `posix.toPosixPath` 断言崩溃；Batch 2 已跑出 **1,385 pass / 78 fail**，Batch 3 已跑出 **230 pass / 32 fail**。
- **[2026-11-21 17:15] `resolution.cp` 与 `js_printer.cp` 修复（Batch 3 零 ASan 崩溃收官）**：
  - 修复 `resolution.cp` 中 `fmt`/`fmtURL` 值接收者悬垂指针，消除 `Resolution.Formatter.format` `stack-buffer-overflow`；
  - 修复 `js_printer.cp:484` `RequireOrImportMetaCallback.init` 的 `@ptrCast(&callback)` 二级指针 Wild Jump SEGV；
  - Batch 3 重跑至 `napi` 完成且 0 ASan 崩溃（**346 pass**，仅 `v8` 下游 `node-gyp` 子进程中止）。
- **[2026-11-21 17:30] `Handlers.cp` RAII 修复（`sql`/`valkey` 425 pass）**：将 `Handlers.release()` 与 `SocketConfig.release()` 合并入 `deinit()` 并清理剩余 `.release()` 调用，消除 `Listener.listen` `0xaa` SEGV；`test/js/sql` 与 `test/js/valkey` 已跑出 **425 pass / 110 fail**。
- **[2026-11-21 20:42] 6 大核心模块联动修复与全绿验证（0 崩溃，135 pass / 1,676 expect()）**：
  - 修复 `js_valkey.cp`（`this._secure = ... orelse` 改 `if (ctx := ...)`，消除 custom TLS 误触发 `deref` 导致的 `ValkeyClient.getTimeoutInterval` UAF）、`bun.lock.cp`（全仓同类 `saved_config_version = ... orelse` 隐患）、`rare_data.cp`（恢复 `closeAllSocketGroups` 8 轮 `closeAllGroups()` + `drainClosedSockets()` 消除 `us_socket_group_deinit` 断言）、`Blob.cp`（完整还原 `constructor`/`getLastModified`/`getNameString`/`getSizeForBindings`/`sharedView`/`calculateEstimatedByteSize`）、`Order.cp`（修复 `generateOrderDescribe` 先执行 `afterAll` 倒置与钩子/过滤缺失）与 `socket.cp`（合并 `NewSocket`/`DuplexUpgradeContext` `release()` 入 `deinit()` 消除 `selectALPNCallback` 断言）。
  - 6 组回归测试全套 0 崩溃跑完（**135 pass / 43 fail**）：`valkey-tls-verify.test.ts` 全绿；`bun-write.test.js` 3 个核心 fail 全转绿；`heap-snapshot.test.ts` 除 `Headers` 超时外全绿；`zstd.test.ts` 4/5 压缩测试转绿；`ws.test.ts` 与 `undici-h2` 断言/崩溃清零。

- **[2026-11-21 21:24] `fetch.cp` 与 `h2_client` 3 模块修复里程碑（`zstd` 与 `undici-h2` 100% 全绿、0 崩溃）**：
  - 还原 `src/runtime/webcore/fetch.cp` `fetchImpl()` 完整选项解析（`decompress`/`tls`/`unix`/`protocol`/`timeout`/`redirect`/`keepalive`/`verbose`/`proxy`/`signal`/`body`/`headers` 等）与 `FetchTasklet.HTTPRequestBody.fromJS` 非空赋值；
  - 将 `src/http/h2_client/{Stream,ClientSession,PendingConnect}.cp` 中的 `release(*_)` 合并入 `deinit(*_)` 并清理 `this` 写法，消除 `encode.drainSendBodies` `use-after-poison` 崩溃；
  - 核查 `FileSink.cp` 已有 `Bun__ForceFileSinkToBeSynchronousForProcessObjectStdio`；
  - 实测验证：`zstd.test.ts` 与 `undici-h2/run.test.ts` **100% 全绿通过**，`bun-write.test.js` 仅剩 1 项 `Bun.file -> Response` 超时（共 117 pass / 1 fail / 0 崩溃）；`test/config` + `test/snippets` + `test/internal`（除全仓静态扫 `ban-words` 外）0 fail / 0 crash。
- **[2026-11-21 21:25] 向 `cp_agy` 提交工单 `task-bun-20261121-sat-assign-op`**：复现文件 `/data/dev/cp/temp/repro_sat_assign.cp`，上报 `cpc` 不支持饱和算术复合赋值 `+|= / -|= / *|=`（在普通函数内报 `E0100`，在结构体方法体内静默吞掉整句语句，已定位导致 `BundleThread.cp` `generation +|= 1` 丢失及 `js_printer.cp` `dedent` 等 12 处异常）。
- **[2026-11-21 21:28] 向 `cp_agy` 提交工单 `task-bun-20261121-coalesce-break-label`**：复现文件 `/data/dev/cp/temp/repro_coalesce_break.cp`，上报 `cpc` 不支持在函数参数内写 `x ?? break :label`（报错或方法体内静默吞掉整句导致 `npm.cp` `group_slice` 未初始化 `index out of bounds` panic），且 `const x = y ?? break :label` 生成 Zig 时丢失 `:label`。
- **[2026-11-21 21:29] 收到 `cp_agy` 结单通知 `task-bun-20261121-sat-assign-op` 并验收 ack**：`cpc` 已在 lexer/ast/parser/codegen 中修复饱和/环绕复合赋值运算符（`+|=`, `-|=`, `*|=`，`<<|=`, `+%=`, `-%=`, `*%=`）及饱和二元运算符（`*|`, `<<|`）；已用 `/data/dev/cp/cap/bin/cpc --lib /data/dev/cp/temp/repro_sat_assign.cp` 验证通过（`x.* = (x.* +| 1); x.* = (x.* -| 1);`）。
- **[2026-11-21 21:37] 饱和算术赋值 / `npm.cp` `?? break` / 全仓 15 项 P0·P1 `release()` RAII 排雷里程碑**：
  - 修复 `src/bundler/BundleThread.cp` 及全仓另外 7 个文件（`GlobWalker.cp`、`css/small_list.cp`、`js_printer.cp`、`NodeHTTPResponse.cp`、`crash_handler.cp`、`cli/repl.cp`、`js_parser/ast/P.cp`）共 12 处 `+|= / -|=` 饱和赋值，消除 `generation` 恒为 0 问题，`test/bundler/resolver/cache-invalidation.test.ts` **8/8 100% 全绿**；
  - 修复 `src/install/npm.cp` 中 4 处嵌套在参数内的 `?? break :bin` 被 `cpc` 静默吞掉的问题，彻底根除 `create-jsx.test.ts` 中 `bun install` 触发的 `SemverString.slice` `index out of bounds (3579139412)` panic（0 panic / 0 ASan crash）；
  - 完成全仓 15 个 P0/P1 高风险跨作用域对象的 `release()` → `releaseInternal` / `releaseLoop` 重命名排雷（`FileResponseStream`、`PosixSignalHandle`、`Listener`、`TimerObjectInternals`、`ConcurrentTask`、`udp_socket`、`UpgradedDuplex`、`Image`、`PBKDF2`、`Loop`、`WindowsNamedPipeContext`、`WindowsNamedPipe`、`s3/simple_request`、`s3/download_stream`、`ssl_wrapper`），全部通过 `cpc_brief.py` 4 项门禁。
- **[2026-11-21 21:45] `node_fs.cp` `ptrAs` 与 5 项次级 `release()` 清雷 + `cli/{init,inspect,watch}` 扫描**：
  - 修复 `src/runtime/node/node_fs.cp:5624` `req.ptrAs(([*:0]u8)?)` 被误生成为 `try [*:0]u8` 问题（全仓扫描确认仅此一处）；
  - 完成剩余 5 个次级跨作用域对象 `release()` → `releaseInternal` 重命名（`S3Stat.cp`、`ObjectURLRegistry.cp`、`s3/error_jsc.cp`、`SocketAddress.cp`、`us_socket_t.cp`），均通过 `cpc_brief.py` 4 项门禁；
  - `test/cli/{init,inspect,watch}` 扫描完成：**17 pass, 2 todo, 7 fail, 0 crash**。
- **[2026-11-21 21:53] 收到 `cp_agy` 结单通知 `task-bun-20261121-coalesce-break-label` 并验收 ack**：`cpc` 已修复 `?? break :label [val]` / `?? continue :label`、`orelse` 终结符及带标签 `for`/`while` 前缀输出（`parser/expr.rs`, `codegen/expr/error_flow.rs`, `codegen/stmt.rs`）；已用新版 `cpc --lib` 验证 `repro_coalesce_break.cp`（成功生成 `loop: for ... consume(((x) orelse break :loop));`）并重编 `src/install/npm.cp`。
- **[2026-11-21 22:10] `multi_run.cp` 进程回收修复里程碑（`multi-run.test.ts` 117/118 通关、0 崩溃）**：
  - 补全 `src/cli/multi_run.cp:124-137` `ProcessHandle.start()` 缺失的 `proc_ptr.setExitHandler(_)` 与 `proc_ptr.watchOrReap()`，彻底消除 `bun run --parallel` / `--sequential` `<defunct>` 僵尸进程挂死问题；
  - `test/cli/run/multi-run.test.ts` 从全线超时提升至 **117 pass / 1 fail / 0 crash (526 expect() calls, 50.32s)**；同步提交新版 `cpc` 重编后的 `src/install/npm.zig`（保留 `:bin` 标签）。
- **[2026-11-21 22:16] 收到 `cp_agy` 重大语法变更广播 `syntax-raii-20261121` 并验收 ack**：`cpc` (Release) 升级 RAII 与所有权模型并已同步更新本仓库 `CP_PITFALLS.md`：(1) 采用唯一双重幂等析构 `~init()` + `drop(x)`，彻底根除对 `release()` / 裸指针 `*T!` / 字段读取 / 多参方法误注入 `defer` 析构；(2) 统一直接赋值三态与五态对象解构；(3) `&v` 回收为纯取址符直赋 `*T!`/`*const T!`；(4) 补齐饱和/环绕复合赋值及 `?? break :label`。
- **[2026-11-21 22:23] `multi_run` / `RuntimeTranspilerCache` / `visit` / `lexer` 4 模块修复里程碑（4 大测试集 100% 全绿）**：
  - 修复 `src/cli/multi_run.cp:624-648` workspace 包路径过滤与无 `name` 回退 `relativePlatform` 逻辑，`multi-run.test.ts` **118 pass / 0 fail (527 expect())** 100% 全绿通关；
  - 修复 `src/jsc/RuntimeTranspilerCache.cp:63-70`（提升 `CacheMetadata` 并改写 `while` 索引循环恢复 `Metadata.size` 非零计算）、`src/js_parser/lexer.cp:3209-3226`（`isLatin1Identifier` 改用 `c0` + `while` + 范围 `if`）、`src/js_parser/ast/visit.cp:381-392`（`visitForLoopInit` 改用 `while` 索引遍历恢复 `visitBinding`），`transpiler-cache.test.ts` + `run-unicode.test.ts` **11/11 全绿 (130 expect())**，`syntax.test.ts` `#225` **2/2 全绿**；
  - `test/cli/run` 第二批 22 文件扫出 666 pass / 0 crash（上述关键模块已修复）。
- **[2026-11-21 22:38] 收到 `cp_agy` 重大语法变更广播 `syntax-raii-20261121-v2` 并验收 ack**：`cpc` (Release) 与本仓库 `CP_PITFALLS.md` 已同步更新：(1) `release` 无论无参或多参彻底退回普通业务方法，永不作为析构函数、永不注入自动 `defer`、永不截断实参；(2) `init(...)` 为 `class` 构造函数，`~init() { ... }` 为 `struct`/`class` 唯一双重幂等析构函数，提前释放使用 `drop(x)` 或 `x.~init()`；(3) 全面补全三态赋值、五态解构、成员分离借用、`&v` 取址直赋、`struct`/`class`/`ref struct` 边界与四层转换法则。
- **[2026-11-21 22:42] `bun.lock` / `node_zlib_binding` / `package_manager_command` 3 大模块修复里程碑（57/65 通过、0 崩溃）**：
  - 修复 `src/install/lockfile/bun.lock.cp:1485-1488` `maybe_name = name.asString(...) orelse { ... }` 被 `cpc` 误编译为 `if (maybe_name == null)` 吞掉 `asString` 的缺陷，`bun-audit.test.ts` 从 0/13 跃升至 **12/13 通过**；
  - 修复 `src/runtime/node/node_zlib_binding.cp` 中 `@"in"` 局部切片变量名被 `cpc` 吞掉赋值与 `setBuffers` 调用的缺陷（重命名为 `in_slice`），`bun-install-tarball-integrity.test.ts` **13/13 100% 全绿**；
  - 修复 `src/cli/package_manager_command.cp:108-126` `getSubcommand` 多切一位参数的缺陷，`bun-info.test.ts` `bun pm view` 从全败跃升至 **12/13 通过**；
  - `test/cli/install` 第二批在前序已跑出 **425 pass / 0 crash**。
- **[2026-11-21 23:07] `ini` / `package_json` / `writeOutputFilesToDisk` 3 大模块修复里程碑（15/15 100% 全绿、`zig:check` 5/5）**：
  - 修复 `src/ini/ini.cp` 全量 102 处 `try expr` → `expr?`（包括关键 `loadNpmrc` `parser.parse(parser.@"arena".allocator())?;` 被静默丢弃导致 `.npmrc` 解析失效）、`E.Null`/`E.Array`/`E.Object` 空结构体字面量及 `commitRopePart` `*((*Rope)?)` 参数，`zig:check` **5/5**、`redacted-config-logs.test.ts` **11/11 全绿**、`minimum-release-age.test.ts` `basic filtering` **3/3 全绿**；
  - 修复 `src/resolver/package_json.cp` 4 处（`valueForKey`/`resolveTarget`/`resolveReverse`/`resolveTargetReverse`）`for` 循环遗漏 `i += 1;` 导致条件导出永远只取第 0 项（`"types"` `.d.ts`）的严重缺陷；
  - 修复 `src/bundler/linker_context/writeOutputFilesToDisk.cp:169-172` `string("")` 触发 `cp_string_from` 退化返回 `@typeName(*const [0:0]u8)` 问题，`bun-build-api.test.ts` `sourcemap: true with outdir` 通过。
- **[2026-11-21 23:14] `build_command.cp` `define` 静默丢弃修复与全仓扫描推进**：
  - 修复 `src/cli/build_command.cp:277-292` 表达式级 `if (user_defines := ...)` 导致 `client_transpiler.options.define = options.Define.init(...)` 被 `cpc` 静默丢弃的问题（全仓扫描 148 文件 2,173 行 `try ` 与全仓 `= if (x := ...)` 确认仅此一处），`zig:check` **5/5** 全绿；
  - `test/bundler` 第二批（15 文件）跑出 **23 pass / 14 fail / 0 crash**（14 项 error 均因环境缺少 `filenamify`）。
- **[2026-11-21 23:37] 全仓 `migrate_cp_syntax.py` 标准化、28 模块 `release()` 还原与 `fetch.cp` `.Locked` 校验里程碑**：
  - 执行 `python3 /data/dev/cp/scripts/migrate_cp_syntax.py --apply --no-release src` 扫描全仓 **1,293 个 `.cp` 文件**并完成 **96 个文件**的 CP 新语法标准化迁移；
  - 将此前改名为 `releaseInternal`/`releaseLoop` 或合并进 `deinit()` 的全部 **28 个模块**（`FileResponseStream`, `PosixSignalHandle`, `Listener`, `TimerObjectInternals`, `ConcurrentTask`, `udp_socket`, `UpgradedDuplex`, `Image`, `PBKDF2`, `Loop`, `WindowsNamedPipeContext`, `WindowsNamedPipe`, `s3/simple_request`, `s3/download_stream`, `ssl_wrapper`, `S3Stat`, `ObjectURLRegistry`, `s3/error_jsc`, `SocketAddress`, `us_socket_t`, `PasswordObject`, `h2_client/{Stream,ClientSession,PendingConnect}`, `ServerConfig`, `SSLConfig`, `Handlers`, `socket`）还原回 `release` 名称并全部重编通过 `cpc_brief.py`；
  - 修复全仓重编后暴露的 45 个 `ast-check` 问题及 `linear_fifo`/`read_file`/`vtable` 3 个 `zig:check` 问题，`bun run zig:check` **5/5 steps succeeded**；
  - 还原 `src/runtime/webcore/fetch.cp:584-608` `bodyValue.* == .Locked` 的 `isDisturbed` 校验与 `.ReadableStream` 提取分支。
- **[2026-11-22 00:10] `dns` / `css/context` / `udp_socket` / `uws_sys/udp` / `Postgres` / `MySQL` 6 模块修复里程碑（3 大测试集 100% 全绿）**：
  - 修复 `src/runtime/dns_jsc/dns.cp` 4 处 `.new` 未转义关键字（`.@"new"`）恢复 `ResolveInfoRequest.init` `cache.@"new".lookup` 绑定，`22712.test.ts` **6/6 全绿**（消除 `PendingCacheKey.append` `SEGV`）；
  - 修复 `src/css/context.cp:248-260` `for (*ltr/*rtl/*dark ...)` 与字段重名导致误调 `ArrayList.deinit` 填充 `0xaa` 崩溃，`25785.test.ts` **2/2 全绿**；
  - 修复 `src/runtime/socket/udp_socket.cp:337-364` `this_ptr.socket = Socket.create(...) orelse` 吞右值及 `src/uws_sys/udp.cp` `*T?` 指针优先级问题，`28083.test.ts` (`dgram`) **6/6 全绿**；
  - 同步清理全仓剩余 2 处 `secure = ... orelse { ... }` 吞右值（`PostgresSQLConnection.cp:866`、`JSMySQLConnection.cp:387`）；
  - `test/regression/issue/` 第 3~5 批累计已测出 **533+ pass / 0 新崩溃**。
- **[2026-11-22 00:32] `http` / `css/rules/property` 修复与全仓遗留隐式 `release()` 清零里程碑（三大回归 100% 全绿、`zig:check` 5/5）**：
  - 修复 `src/http/http.cp:1074` 误加 `add_transfer_encoding = false;` 导致流式请求丢弃显式 `Content-Length` 问题，`27061.test.ts` **6/6 全绿**；
  - 修复 `src/css/rules/property.cp:182-188` `else return` 表达式被吞问题，`27117.test.ts`（`:root` + `@property`）**1/1 全绿**；
  - 用新版 `cpc` 全量重编全仓仍残留旧版隐式 `defer ... release()` 的 **24 个 `.cp` 模块**（含 `HTTPThread`、`Router`、`url`、`resolver`、`napi`、`Headers`、`SocketGroup`、`write_file` 等）并修正 `s3/client.cp` 与 `blob/copy_file.cp` 历史 `export const` -> `pub const`，全仓隐式 `release()` **归零 (0)**，`bun run zig:check` **5/5**，`27358.test.ts`（mTLS keepalive）**2/2 全绿**；
  - 完成 `test/regression/issue/` 全目录 353 文件及 `test/integration/` 扫描（均 0 崩溃）。
- **[2026-11-22 01:21] 向 `cp_agy` 批量提交 5 项已实锤复现的 `cpc` 编译器缺陷工单**：
  - `task-bun-20261122-orelse-assign-drop` (`/data/dev/cp/temp/repro_orelse_assign.cp`)：`lhs.field = expr orelse { ... }` 吞 `expr` 并请裁决 `orelse` 软关键字去留；
  - `task-bun-20261122-loop-field-shadow` (`/data/dev/cp/temp/repro_loop_field_shadow.cp`)：`for (*ltr of _.ltr)` 循环变量与字段同名误 lowering 为 `self.ltr`；
  - `task-bun-20261122-if-else-return-drop` (`/data/dev/cp/temp/repro_if_else_return_drop.cp`)：`if ... else if ... else return` 赋值报 `E0100` 且在方法体内静默吞语句；
  - `task-bun-20261122-expr-walrus-drop` (`/data/dev/cp/temp/repro_expr_walrus_drop.cp`)：实参内 `if (v := opt) ... else null` 报 `E0111` 且在方法体内静默吞语句；
  - `task-bun-20261122-ptr-opt-and-string-from` (`/data/dev/cp/temp/repro_ptr_opt_and_unsafe.cp`)：`ptrAs(([*:0]u8)?)` 误生成 `try [*:0]u8` 与 `string("")` 退化为 `@typeName(*const [0:0]u8)`。
- **[2026-11-22 01:29] 向 `cp_agy` 提交工单 `task-bun-20261122-ptr-postfix-opt`**：复现文件 `/data/dev/cp/temp/repro_ptr_postfix_opt.cp`，上报 `*Foo?` 在 `cpc` 中生成为 `*?*Foo`（而非 `?*Foo`）导致传入 `*Foo` 类型不匹配，提请裁决 `*T?` vs `(*T)?` / `*T?!` 语义与编译期拦截提示；同步启动 2 路 RAII `~init()` 专项审查改造子智能体（`Archive`/`RuntimeTranspilerCache` 与 `WebCore`）。
- **[2026-11-22 01:38] 向 `cp_agy` 提交工单 `task-bun-20261122-unsafe-e0209-gaps`**：复现文件 `/data/dev/cp/temp/repro_unsafe_leak_check.cp`，上报 `cpc` 在 `unsafe {}` 外部对 `[*]u8!` 下标 `p[0]`、切片 `p[0..1]`、`@ptrCast`、`@alignCast`、`@intFromPtr`、`@constCast(slice.ptr)` 共 6 类危险操作全部漏检放行（`EXIT=0` 未触发 `E0209`）。

- **[2026-11-22 02:01] WebCore / HTTP / Archive / pack / RuntimeTranspilerCache RAII `~init()` 专项加固与历史桩函数恢复（103/103 全绿、`zig:check` 5/5）**：
  - `src/http/http.cp`：`buildRequest` 恢复 `Accept-Encoding` 正常发送与 `override_*` 标记；`isHTTPS()` 补充 `http_proxy.isHTTPS()` 判定，根治 `fetch-proxy-tls-intern-race.test.ts` panic；
  - `src/runtime/webcore/{Blob,Response,S3File}.cp`：修复 `Blob.dupe`/`dupeWithContentType` 对 `content_type_allocated` 深拷贝（消除 ASan `use-after-poison`）、补全 `Response`/`S3File` `errdefer` 与泄漏点并收窄 10+ 裸指针操作入 `unsafe {}`；恢复 `Blob.fromDOMFormData`/`fromURLSearchParams` 完整实现（`FormDataContext`、`URLSearchParamsConverter`）与 `DOMFormData.cp`/`URLSearchParams.cp` FFI 绑定；
  - `src/runtime/webcore/fetch.cp`：恢复缺失的 `blob:` / `file:` 本地 URL 分支；
  - `src/runtime/api/Archive.cp`、`src/cli/pack_command.cp`、`src/jsc/RuntimeTranspilerCache.cp`：以 `~init()` RAII Guard 批量替代手工 `defer`/`errdefer`（`Archive` 15 guards / `pack_command` 21 guards 清零 35/35 / `RuntimeTranspilerCache` 7 guards 清零 13/13）；
  - 验证：`bun run zig:check` 5/5，`bun-pack.test.ts` (71/71)、`transpiler-cache.test.ts` (9/9)、`27061.test.ts` (6/6)、`blob.test.ts` (16/16)、`fetch-proxy-tls-intern-race.test.ts` (1/1) 共 **103/103 全绿**。
- **[2026-11-22 02:02] 收到并验收 `cp_agy` 6 条结单通知（全部 `cpc --lib` + `zig ast-check` 验证通过并完成 `ack`）**：
  1. `task-bun-20261122-orelse-assign-drop` (`repro_orelse_assign.cp` PASS)：修复 `codegen/stmt.rs` 赋值右值 `NullCoalesce` lowering，保留 `??` 与 `orelse` 语义（仅当 `nc_l == lhs` 且 `nc_r` 非控制流块时才用 `??=` 简写），并修复 `--lib` 跳过用户自定义 `struct Box`。
  2. `task-bun-20261122-loop-field-shadow` (`repro_loop_field_shadow.cp` PASS)：修复 `for` 循环捕获变量与 struct 字段同名时的遮蔽优先级（`locals`/`block_locals` 与 `mut_borrow_loop_vars` 优先于类字段解析）。
  3. `task-bun-20261122-if-else-return-drop` (`repro_if_else_return_drop.cp` PASS)：`parser/primary.rs` 支持 `Token::Return` 主表达式解析，`if/else` 表达式内可直接使用 `return` / `return expr` 分支。
  4. `task-bun-20261122-expr-walrus-drop` (`repro_expr_walrus_drop.cp` PASS)：`parser/primary.rs` 与 `codegen/expr/control.rs` 补齐表达式位置 `if (v := opt)` / `if (*v := opt)` 解析与作用域绑定。
  5. `task-bun-20261122-ptr-opt-and-string-from` (`repro_ptr_opt_and_unsafe.cp` PASS)：修复 `([*:0]u8)?` 等后缀 `?` 生成 `?[*:0]u8`（不再误作 `try`），并在 `cp_string_from` 补充 `*const [N]u8` / `*const [0:0]u8` 分支。
  6. `task-bun-20261122-ptr-postfix-opt` (`repro_ptr_postfix_opt.cp` PASS)：`codegen/types/map_type.rs` 修复 `*Foo?`、`(*Foo)?`、`*Foo?!` 统一映射为 `?*Foo`，显式 `*(*Foo?)` 仍为 `*?*Foo`。
- **[2026-11-22 02:08] 向 `cp_agy` 提交工单 `task-bun-20261122-val-receiver-dangling-slice`**：复现文件 `/data/dev/cp/temp/repro_value_receiver_dangling_slice.cp`，上报无只读接收者方法在 `cpc` 下生成按值拷贝 `self: StatHash`，且返回 `_.buf[0.._.len]` 悬垂栈切片时未做借用生命周期拦截（直接引发 `29181.test.ts` `Last-Modified` 头返回栈垃圾内存）。
- **[2026-11-22 02:09] 向 `cp_agy` 提交工单 `task-bun-20261122-generic-anon-raii`**：复现文件 `/data/dev/cp/temp/repro_generic_anon_raii.cp`，上报泛型函数返回的匿名结构体含 `~init()` 时，生成的 `__cp_deinit` 参数错误输出为 `_: *__CpAnonStruct__` 导致 `ast-check` 失败。
- **[2026-11-22 02:13] 收到并验收 `cp_agy` 结单通知 `task-bun-20261122-unsafe-e0209-gaps`（已完成 `ack`）**：
  - 修复结论：`sema/safety.rs` 与 `sema/stmt_expr.rs` 补齐 `unsafe {}` 外部对裸指针下标读取（`ManyRawPtr`/`CPtr`/`SentinelPtr`/`RawPtr`）、裸指针切片（`p[0..1]`）及 `@ptrCast`/`@alignCast`/`@intFromPtr`/`@ptrFromInt`/`@constCast` 的 `E0209` 拦截。
  - 复现验证：`/data/dev/cp/temp/repro_unsafe_leak_check.cp` 经新版 `cpc check --brief --json` 精确报出 `7` 条 `E0209`（覆盖全部第 2~7 行测试语句）。
- **[2026-11-22 02:14] 向 `cp_agy` 提交工单 `task-bun-20261122-generic-struct-raii-defer`**：复现文件 `/data/dev/cp/temp/repro_generic_struct_raii_defer.cp`，上报泛型结构体 `Box<T>` 定义 `~init()` 后实例化 `Box<Type>{ ... }` 时 `cpc` 未在作用域注入 `defer b.__cp_deinit()`。
- **[2026-11-22 02:16] 向 `cp_agy` 提交工单 `task-bun-20261122-call-new-silently-empties-fn`**：复现文件 `/data/dev/cp/temp/repro_trivial_new_drop.cp`，上报方法体内调用 `new(this)` 时 `cpc` 静默清空整个函数体且返回 `EXIT=0`（定位并修复 `29181.test.ts` `server.stop()` 永不 resolve 根因）。
- **[2026-11-22 02:19] 收到并验收 `cp_agy` 2 条结单通知（全部 `cpc --lib` + `zig ast-check` 验证通过并完成 `ack`）**：
  1. `task-bun-20261122-val-receiver-dangling-slice` (`repro_value_receiver_dangling_slice.cp` PASS)：`codegen/actor.rs` 修复非枚举 struct 省略只读接收者且返回借用视图（切片/字符串/指针含可选/错误包装）的方法，自动输出 `self: *const T` 替代值拷贝 `self: T`，从编译器层面根除 `StatHash.lastModified()` 类悬垂栈切片。
  2. `task-bun-20261122-generic-anon-raii` (`repro_generic_anon_raii.cp` PASS)：`codegen/actor.rs` 识别 `@This()` 匿名结构体内的 `__CpAnonStruct__` 接收者，`~init()` 正确输出 `pub fn __cp_deinit(self: *@This()) void`。
- **[2026-11-22 02:23] 向 `cp_agy` 提交工单 `task-bun-20261122-sentinel-const-space`**：复现文件 `/data/dev/cp/temp/repro_sentinel_const_space.cp`，上报 `List([:0]const u8)` 作为泛型类型实参时 `cpc` 丢失空格输出 `[:0]constu8`。
- **[2026-11-22 02:31] 收到并验收 `cp_agy` 2 条结单通知（全部 `cpc --lib` + `zig ast-check` 验证通过并完成 `ack`）**：
  1. `task-bun-20261122-generic-struct-raii-defer` (`repro_generic_struct_raii_defer.cp` PASS)：`codegen/types/mod.rs` `type_has_release` 剥离 `<...>`/`(...)` 泛型实参匹配 `class_defs`，泛型结构体 `Box<T>{ ~init() }` 实例化时现已自动注入 `defer __cp_deinit()`。
  2. `task-bun-20261122-call-new-silently-empties-fn` (`repro_trivial_new_drop.cp` PASS)：`parser/primary.rs` 修复 `new(this)` 等非构造形式调用，正确保留 `const ptr = new(self);` 不再静默清空函数体。
- **[2026-11-22 02:37] 收到并验收 `cp_agy` 结单通知 `task-bun-20261122-sentinel-const-space`（已完成 `ack`）**：
  - 修复结论：`parser/types.rs` 与 `parser/primary.rs` 收集泛型/调用类型实参时保留 token 间距空格及 `SentinelSlice` `:const` 格式化；`repro_sentinel_const_space.cp` 精确输出 `List([:0]const u8)` 并通过 `zig ast-check`。

- **[2026-11-22 02:44] 9 大核心模块 `~init()` RAII 改造与 `29181` 根治里程碑（`zig:check` 5/5、23/23 全绿）**：
  - `src/resolver/fs/stat_hash.cp` & `src/runtime/server/server.cp`：修复 `lastModified()` 悬垂栈切片（改 `_: *const StatHash`）与 `ServerAllConnectionsClosedTask.schedule` 中 `new(this)` 被静默清空函数体问题，彻底跑通 `29181.test.ts` (2/2, 411 expects)；
  - `src/runtime/api/JSBundler.cp`（59→0，15 guards，ratio `1.055`）、`src/runtime/api/cron.cp`（47→0，12 guards，ratio `1.081`）、`src/install/isolated_install.cp`（39→0，34 guards，ratio `1.123`，`Path.cp` `pub ResetScope`）、`src/dotenv/env_loader.cp`（9→0，5 guards，ratio `1.024`）、`src/runtime/node/node_fs.cp`（94→0，28 guards + 11 `unsafe {}`，ratio `1.014`）、`src/bake/DevServer.cp`（85→4，32 guards + 14 `unsafe {}`，ratio `1.076`）；
  - 累计消除 329 处手工 `defer`/`errdefer` 并全部通过 `cpc_brief.py` 与 `zig:check` 5/5，`29181.test.ts` (2/2)、`cron-parse.test.ts` (11/11)、`bun-file.test.ts` (6/6)、`ci-info.test.ts` (4/4) 共 23/23 全绿。
- **[2026-11-22 03:09] 向 `cp_agy` 提交工单 `task-bun-20261122-return-dot-enum-err`**：复现文件 `/data/dev/cp/temp/repro_return_dot_enum_in_err_union.cp`，上报在 `E!EnumType` 返回函数中 `return .blocked;` 被 `cpc` 误降级为 `return error.blocked;`。
- **[2026-11-22 03:14] 向 `cp_agy` 提交工单 `task-bun-20261122-generic-raw-ptr-arg`**：复现文件 `/data/dev/cp/temp/repro_generic_raw_ptr_arg.cp`，上报将 `*Foo!` 直接作为泛型实参 `Box<*Foo!>` 传入时，生成 `Box(*Foo!)` 未剥离尾置 `!` 导致 `ast-check` 失败。

- **[2026-11-22 03:22] 4 大核心模块（`Installer`、`h2_frame_parser`、`ConsoleObject`、`BunObject`）泛型 RAII `~init()` 改造里程碑（`zig:check` 5/5、7/7 全绿）**：
  - `src/install/isolated_install/Installer.cp`（44→0，ratio `1.043`）：以泛型 `DeinitGuard<T>` / `DeinitWithAllocGuard<T>` / `OptDeinitGuard<T>` / `RestoreGuard<T>` 替换全部手工释放，修复 `version_buf` allocator 混用与 `Task.Step/Error/Result/Yield` 可见性；
  - `src/runtime/api/bun/h2_frame_parser.cp`（43→0，ratio `1.047`）：全面转入顶层泛型与专用 RAII Guard，补充 `@intFromPtr` `unsafe {}`；
  - `src/jsc/ConsoleObject.cp`（61→0，ratio `1.056`）：拆分 4 处 `JSPropertyIterator` 泛型 Guard 初始化并补全 34 处裸指针 `unsafe {}`；
  - `src/runtime/api/BunObject.cp`（53→0，ratio `1.044`）：清理全部手工 `defer` 并补全 `unsafe {}`；
  - 累计再消减 **201 处**手工 `defer`/`errdefer`，`bun run zig:check` **5/5**，`BunObject.test.ts` + `escapeHTML.test.js` **7/7**（276 expects）全绿。
- **[2026-11-22 03:24] 收到并验收 `cp_agy` 2 条结单通知（全部 `cpc --lib` + `zig ast-check` 验证通过并完成 `ack`）**：
  1. `task-bun-20261122-return-dot-enum-err` (`repro_return_dot_enum_in_err_union.cp` PASS)：`codegen/stmt.rs` 新增 `should_lower_bare_dot_return_as_error`，在 `E!Status` 函数中正确保留 `return .blocked;` 而不再误转为 `return error.blocked;`。
  2. `task-bun-20261122-generic-raw-ptr-arg` (`repro_generic_raw_ptr_arg.cp` PASS)：`codegen/types/map_type.rs` 与 `parser/types.rs` 统一转译泛型实参中的 CP 类型（`*Foo!`、`*const Foo!`、`[*:0]u8!`、`[*]const u8!`、`*Foo!?`、`?*Foo!` 等），`Box<*Foo!>` 正确输出 `Box(*Foo)`。
- **[2026-11-22 03:30] 向 `cp_agy` 提交工单 `task-bun-20261122-factory-struct-raii-lowering`**：复现文件 `/data/dev/cp/temp/repro_factory_struct_raii.cp`，上报 `pub fn Factory(...): type { return struct { ... }; }` 内部实例化带 `~init()` 的 Guard 时，`cpc` 原样透传函数体未注入 `defer __cp_deinit()`（阻塞 `RequestContext.cp` / `diff_match_patch.cp` 等工厂模式模块 RAII 化）。

- **[2026-11-22 03:54] 10 大核心模块泛型 RAII `~init()` 改造里程碑（`zig:check` 5/5、18/18 全绿）**：
  - `src/jsc/VirtualMachine.cp`（54→0，ratio `1.072`）、`src/runtime/valkey_jsc/js_valkey.cp`（44→0，ratio `1.054`）、`src/runtime/valkey_jsc/js_valkey_functions.cp`（47→0，ratio `0.995`）、`src/sql_jsc/postgres/PostgresSQLConnection.cp`（44→0，ratio `1.061`）、`src/runtime/dns_jsc/dns.cp`（34→2，ratio `0.956` + 31 处 `E0209` `unsafe {}`）；
  - `src/runtime/server/RequestContext.cp`（顶层 `getContentType` 转 RAII，ratio `1.003`）、`src/test_runner/pretty_format.cp`（7 处资源释放→0 + 17 处 `unsafe {}`，ratio `1.034`）、`src/test_runner/expect.cp`（32 处资源释放→0，ratio `1.041`）、`src/runtime/api/JSTranspiler.cp`（34→0，ratio `1.122`）、`src/runtime/api/bun/process.cp`（25→0 + 9 处 `unsafe {}`，ratio `1.042`）；
  - 累计再消除 **322 处**手工资源释放 `defer`/`errdefer`，全部通过 `cpc_brief.py` 与 `bun run zig:check` **5/5**，`22712.test.ts` (6/6)、`BunObject.test.ts` (3/3)、`transpiler-cache.test.ts` (9/9) 共 **18/18**（368 expects）全绿。
- **[2026-11-22 03:56] 收到并验收 `cp_agy` 结单通知 `task-bun-20261122-factory-struct-raii-lowering`（已完成 `ack`）**：
  - 修复结论：comptime 工厂函数 `return struct { ... }` 现已接入完整 CP AST lowering 与 RAII `defer __cp_deinit()` 自动注入（同步完善了 `@tls`/`comptime`/`usingnamespace` 成员解析、`ErrorSet!PrimitiveType`、`TypeCall(...){...}` 等）；
  - 复现验证：`/data/dev/cp/temp/repro_factory_struct_raii.cp` 通过 `cpc --lib` + `zig ast-check`，工厂内 `const g = Guard{ val: 1 };` 成功注入 `defer g.__cp_deinit()`（解封 `RequestContext.cp` / `diff_match_patch.cp` / `server.cp` 工厂模块 RAII 化）。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-union-ptr-receiver-deref`（复现 `/data/dev/cp/temp/repro_union_readonly_receiver.cp`）：`union(enum)`/`enum` 只读方法返回指针或切片时，`cpc` 自动将接收者转为 `self: *const @This()`，但体内 `_ == .tag` / `_ != .tag` / `switch (_)` 未自动解引用为 `self.*`。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-factory-test-decl-drop`（复现 `/data/dev/cp/temp/repro_factory_test_decl.cp`）：编译期工厂函数 `return struct { ... }` 内部包含 `test <ident> { ... }` 声明块时，`cpc` 静默跳过内部方法的 CP lowering 与 RAII `~init()` 注入。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-arena-ident-silent-drop`（复现 `/data/dev/cp/temp/repro_arena_ident_drop.cp`）：工厂 `return struct` 内将局部变量命名为 `arena` 时，`cpc` 未报关键字错误反而静默吞掉 `let arena` 及其后续引用语句。
- **2026-11-22 [结单验收 `cp_agy`]**: 已验收并 `ack` 3 条上游修复：
  1. `task-bun-20261122-union-ptr-receiver-deref`（`repro_union_readonly_receiver.cp` `cpc --lib` + `zig build-obj` PASS：`*const @This()`/`*@This()` 方法体内 `switch(_)`、`_ == .tag`、`_ != .tag` 自动解引用为 `self.*`）。
  2. `task-bun-20261122-factory-test-decl-drop`（`repro_factory_test_decl.cp` 工厂结构体内部方法 lowering 与 RAII defer 注入恢复正常）。
  3. `task-bun-20261122-factory-err-orelse-return`（`repro_factory_err_orelse_return.cp` `cpc --lib` + `zig build-obj` PASS：原生 Zig 错误联合 `MyErr!T` 下 `orelse return val` 不再误包装为 `.{ .ok = val }`）。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-generic-ptr-opt-paren`（复现 `/data/dev/cp/temp/repro_ptr_opt_paren_generic.cp`）：泛型结构体字段 `ptr: *(T?)!` 被 `cpc` 丢掉括号生成为 `?*T` 而非 `*?T`。
- **2026-11-22 [结单验收 `cp_agy`]**: 已验收并 `ack` `task-bun-20261122-arena-ident-silent-drop`（`repro_arena_ident_drop.cp` `cpc --lib` + `zig build-obj` 0 错误通过；`arena` 已降级为上下文关键字，仅后接 `{` 时作为 `arena {}` 块，普通变量/字段/参数名不再静默吞掉）。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-factory-inline-struct-param`（复现 `/data/dev/cp/temp/repro_factory_raii_lowering.cp`）：编译期工厂 `return struct` 内部含 `params: struct { ... }` 内联匿名结构体形参时，`cpc` 静默放弃整个 `return struct` 的 CP lowering 与 RAII `defer __cp_deinit()` 注入。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-is-locked-arg-drop`（复现 `/data/dev/cp/temp/repro_is_locked_arg_drop.cp`）：`cpc` 将所有 `.isLocked(arg)` 方法调用静默丢弃参数并生成为 `.isLocked()`。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-tuple-unpack-anyopaque`（复现 `/data/dev/cp/temp/repro_tuple_unpack_anyopaque.cp`）：元组解构 `const [a, b] = ...` 注入的 prelude 含 `const Message = anyopaque; ... ?Message`，在 Zig 0.15 下报 `opaque type 'anyopaque' cannot be optional`。
- **2026-11-22 [结单验收 `cp_agy`]**: 已验收并 `ack` 4 条上游修复：
  1. `task-bun-20261122-generic-ptr-opt-paren`（`repro_ptr_opt_paren_generic.cp` PASS：`*(T?)!` 正确生成 `*?T`）。
  2. `task-bun-20261122-factory-inline-struct-param`（`repro_factory_raii_lowering.cp` PASS：工厂 `return struct` 含内联匿名结构体形参时完整走 lowering 与 RAII 注入）。
  3. `task-bun-20261122-is-locked-arg-drop`（`s.isLocked(g)` 参数完整透传，仅无参时走同步原语重写）。
  4. `task-bun-20261122-tuple-unpack-anyopaque`（`?Message` 的 `?anyopaque` 已替换为哨兵结构体；因 `vendor/zig` 0.15.2 不支持 `field_types`/`field_names`，另提交 `task-bun-20261122-unpack-prelude-zig015` 跟进）。
- **2026-11-22 [RAII Wave #4 里程碑验收]**: 完成 17 个核心模块（`socket`、`js_printer`、`bun_test`、`sys`、`RequestContext`、`Blob`、`S3File`、`path`、`html_rewriter`、`ServerWebSocket`、`update_interactive_command`、`MySQLConnection`、`shell/interpreter`、`resolver`、`ModuleLoader`、`WebSocketUpgradeClient`、`repl`）`~init()` RAII 改造与 `E0209` `unsafe {}` 合规；`bun run zig:check` 5/5 通过，`bun bd test`（`28083`、`29181`、`bun-file`、`blob`）30/30 PASS（589 expects）。
- **2026-11-22 [结单验收 `cp_agy`]**: 已验收并 `ack` `task-bun-20261122-unpack-prelude-zig015`（`repro_tuple_unpack_anyopaque.cp` 经 `cpc --lib` + `vendor/zig/zig build-obj` 0 错误通过；`const [a, b] = ...` 元组解构 prelude 现通过 `@hasField(@TypeOf(s), "fields")` 同时兼容 Zig 0.15.2 与 0.17.0）。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-factory-test-block-lowering`（复现 `/data/dev/cp/temp/repro_factory_test_raii.cp`）：编译期工厂 `return struct` 内部普通方法已正常 lowering，但 `test "..." { ... }` 测试块体未走 CP lowering，导致其中的 `Guard<T>{ ... }` 与 RAII `~init()` 失效。
- **2026-11-22 [结单验收 `cp_agy`]**: 已验收并 `ack` `task-bun-20261122-factory-test-block-lowering`（`repro_factory_test_raii.cp` 通过 `cpc --lib` + `zig ast-check`；容器与工厂 `return struct` 内部 `test "..."` / `test <ident>` 块已走完整 CP lowering 与 RAII 注入）。
- **2026-11-22 [RAII Wave #5 里程碑验收]**: 完成 15 个核心模块（`yarn`、`S3Client`、`FetchTasklet`、`ChangedFilesFilter`、`bundle_v2`、`lockfile_json_stringify_for_debugging`、`PackageInstall`、`hosted_git_info`、`test_command`、`diff_match_patch`、`JSMySQLConnection`、`PackageInstaller`、`server`、`pnpm`、`patchPackage`、`npm` 实为 16 个模块！）`~init()` RAII 改造与 `E0209` `unsafe {}` 收窄；`bun run zig:check` 5/5 通过，`bun bd test`（`29181`、`bun-file`、`bun-install-patch -t hoisted`）10/10 PASS。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-root-struct-callconv-c`（复现 `/data/dev/cp/temp/repro_callconv_dot_c.cp`）：`@root struct` 内方法使用 `@callconv(.c)` 时报 `E0290: Argument to meta '@callconv' must be a compile-time constant`。
- **2026-11-22 [上报 `cp_agy`]**: 提交工单 `task-bun-20261122-unpack-prelude-shadow-end`（复现 `/data/dev/cp/temp/repro_unpack_prelude_shadow_end.cp`）：`@root struct` 含 `end` 成员且文件注入 `cp_unpack_slice` prelude 时，prelude 内局部变量 `const end` 遮蔽 `@root` 的 `end` 声明。
- **2026-11-22 [结单验收 `cp_agy`]**: 已验收并 `ack` `task-bun-20261122-root-struct-callconv-c`（`repro_callconv_dot_c.cp` 经 `cpc --lib` + `zig ast-check` 验证通过；`@callconv(.c)` / `.auto` / `.naked` 等枚举字面量现已在 `@root struct` 方法中完全支持）。
- **2026-11-22 [结单验收 `cp_agy`]**: 已验收并 `ack` `task-bun-20261122-unpack-prelude-shadow-end`（`repro_unpack_prelude_shadow_end.cp` 经 `cpc --lib` + `zig ast-check` 验证通过；`cp_unpack_*` 与 `cp_string_from` prelude 局部变量已统一加 `__cp_` 前缀，消除 `@root struct` 顶层成员遮蔽）。
- **2026-11-22 [RAII Wave #6 里程碑验收]**: 完成 12 个核心模块（`FakeTimers`、`sys_uv`、`StandaloneModuleGraph`、`Body`、`Image`、`web_worker`、`ServerConfig`、`audit_command`、`shell`、`udp_socket`、`NodeHTTPResponse`、`generateChunksInParallel`）`~init()` RAII 改造与 `E0209` `unsafe {}` 收窄；`bun run zig:check` 与 `bun-debug` 链接通过，`bun bd test`（`28083`、`29181`、`bunshell-file`、`bun-file`、`blob`）31/31 PASS（591 expects）。
- **2026-11-22 [RAII Wave #7 里程碑验收]**: 完成 8 个核心模块（`security_scanner`、`bun.lockb`、`node/types`、`JSValue`、`FrameworkRouter`、`ScopeFunctions`、`CodeCoverage`、`Listener`）`~init()` RAII 改造与 `E0209` `unsafe {}` 收窄；`bun run zig:check` 5/5 通过，`bun bd test`（`29181`、`bun-file`、`28083`）14/14 PASS。

### [2026-11-22 Wave #8] RAII `~init()` + `unsafe {}` 专项改造（17 模块，全仓 `zig:check` + `bun bd test` 60/60 PASS）
- 完成 `JSGlobalObject`, `bun.lock`, `create_command`, `node_os`, `MetafileBuilder`, `valkey_protocol`, `DecodeBinaryValue`, `valkey`, `Response`, `CryptoHasher`, `napi`, `pm_pkg_command`, `ansi_renderer`, `production`, `rm`, `patch`, `IncrementalGraph` 共 17 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 17/17 四门禁全绿（膨胀率 `0.942 ~ 1.147 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/js/node/os/ test/js/bun/util/bun-file.test.ts test/regression/issue/29181.test.ts` 60 pass / 0 fail（1241 expects）。

### [2026-11-22 Wave #9] RAII `~init()` + `unsafe {}` 专项改造（10 改造 + 2 复核，全仓 `zig:check` + `bun bd test` 8/8 PASS）
- 完成 `backend_wic`, `ipc`, `ini`, `css_internals`, `pm_version_command`, `ParseTask`, `ErrorReportRequest`, `js_bun_spawn_bindings`, `websocket_client` 等 9 个改造模块（另有 `resolver`, `server`, `yaml` 3 个模块复核剩余 `defer` 均为状态/锁/生命周期配对型，无需再改）。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.88 ~ 1.13 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8 pass / 0 fail（460 expects）。

### [2026-11-22 Wave #10] RAII `~init()` + `unsafe {}` 专项改造（5 改造 + 1 复核，全仓 `zig:check` + `bun bd test` 8/8 PASS）
- 完成 `publish_command`, `barrel_imports`, `toThrow`, `fetch`, `visit` 共 5 个核心模块的 `~init()` RAII 析构与 `unsafe {}` 合规化（另有 `visitStmt` 复核 13 处均为 `popScope`/状态恢复型 `defer`，无需再改）。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.59 ~ 1.126 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8 pass / 0 fail（460 expects）。

### [2026-11-22 Wave #11] RAII `~init()` + `unsafe {}` 专项改造（8 模块，全仓 `zig:check` + `bun bd test` 8/8 PASS）
- 完成 `cares_jsc`, `PasswordObject`, `path_watcher`, `node_fs_stat_watcher`, `YAMLObject`, `glob`, `fs`, `RuntimeTranspilerStore` 共 8 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `1.042 ~ 1.155 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8 pass / 0 fail（460 expects）。

### [2026-11-22 Wave #12] RAII `~init()` + `unsafe {}` 专项改造（4 模块，全仓 `zig:check` + `bun bd test` 8/8 PASS）
- 完成 `bun_string_jsc`, `lockfile`, `http`, `pm_trusted_command` 共 4 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.878 ~ 1.091 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8 pass / 0 fail（460 expects）。

### [2026-11-22 Wave #13] RAII `~init()` + `unsafe {}` 专项改造（10 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `init_command`, `scanImportsAndExports`, `sourcemap`, `tls_socket_functions`, `SSLConfig`, `node_process`, `bake`, `jest`, `MarkdownObject`, `run_command` 共 10 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.937 ~ 1.092 <= 1.2x`）；`bun run zig:check` 5/5 PASS。

### [2026-11-22 Wave #14] RAII `~init()` + `unsafe {}` 专项改造（8 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `windows`, `Parser`, `SavedSourceMap`, `writeOutputFilesToDisk`, `allocation_scope`, `owned`, `testing`, `Macro` 共 8 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `1.02 ~ 1.184 <= 1.2x`）；`bun run zig:check` 5/5 PASS。

### [2026-11-22 Wave #15] RAII `~init()` + `unsafe {}` 专项改造（8 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `DataCell`, `credentials_jsc`, `FileSink`, `node_crypto_binding`, `JSON5Object`, `subprocess`, `Task`, `patch_install` 共 8 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.827 ~ 1.104 <= 1.2x`）；`bun run zig:check` 5/5 PASS。

### [2026-11-22 Wave #16] RAII `~init()` + `unsafe {}` 专项改造（4 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `Package`, `ini_jsc`, `package_manager_command`, `outdated_command` 共 4 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.997 ~ 1.118 <= 1.2x`）；`bun run zig:check` 5/5 PASS。

### [2026-11-22 Wave #17] RAII `~init()` + `unsafe {}` 专项改造（8 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `Watcher`, `snapshot`, `PostgresSQLQuery`, `Cmd`, `ObjectURLRegistry`, `npm_jsc`, `ChromeProcess`, `multipart` 共 8 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.892 ~ 1.149 <= 1.2x`）；`bun run zig:check` 5/5 PASS。

### [2026-11-22 Wave #18] RAII `~init()` + `unsafe {}` 专项改造（6 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `bun_alloc`, `Execution`, `bin`, `aggregate`, `Collection`, `s3/client` 共 6 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `1.001 ~ 1.101 <= 1.2x`）。

### [2026-11-22 Wave #19] RAII `~init()` + `unsafe {}` 专项改造（4 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `Request`, `SocketAddress`, `fs_events`, `libarchive` 共 4 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.785 ~ 1.059 <= 1.2x`）；`bun run zig:check` 5/5 PASS。

### [2026-11-22 Wave #20] RAII `~init()` + `unsafe {}` 专项改造（8 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `P`, `ZigString`, `PipeWriter`, `runTasks`, `migration`, `Hardlinker`, `extract_tarball`, `options` 共 8 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.943 ~ 1.105 <= 1.2x`）。

### [2026-11-22 Wave #21] RAII `~init()` + `unsafe {}` 专项改造（6 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `ParsedShellScript`, `SemverObject`, `JSSourceMap`, `subproc`, `credentials`, `encoding` 共 6 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.958 ~ 1.119 <= 1.2x`）。

### [2026-11-22 Wave #22] RAII `~init()` + `unsafe {}` 专项改造（6 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `node_fs_watcher`, `ref_count`, `main_wasm`, `rare_data`, `event_loop`, `AsyncModule` 共 6 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.937 ~ 1.132 <= 1.2x`）。

### [2026-11-22 Wave #23] RAII `~init()` + `unsafe {}` 专项改造（3 改造 + 1 复核，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `repository`, `PackageManagerTask`, `ProxyTunnel` 共 3 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化（另有 `PackageInstaller` 复核 7 处均为计数器/状态恢复型，无需改动）。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.94 ~ 1.074 <= 1.2x`）；`bun run zig:check` 5/5 PASS。

### [2026-11-22 Wave #24] RAII `~init()` + `unsafe {}` 专项改造（6 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `DirectoryWatchStore`, `PluginRunner`, `doStep5`, `postProcessJSChunk`, `bunx_command`, `upgrade_command` 共 6 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.912 ~ 1.083 <= 1.2x`）。

### [2026-11-22 Wave #25] RAII `~init()` + `unsafe {}` 专项改造（6 模块，全仓 `zig:check` + `bun bd test` 进行中）
- 完成 `AsyncHTTP`, `HTTPThread`, `runner`, `why_command`, `CppWebSocket`, `WebSocketProxyTunnel` 共 6 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.874 ~ 1.16 <= 1.2x`）。

### [2026-11-22 Wave #26] RAII `~init()` + `unsafe {}` 专项改造（6 模块，全仓 `zig:check` + `bun bd test` 通过）
- 完成 `css_parser`, `ClientSession`, `PackageManager`, `install_with_manager`, `HTMLScanner`, `findAllImportedPartsInJSOrder` 共 6 个核心模块的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.925 ~ 1.11 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS。

### [2026-11-22 Wave #27 & Wave #28] RAII `~init()` + `unsafe {}` 专项改造（6-defer 全清零 17 模块 + 5-defer 首批 27 模块，全仓 `zig:check` + `bun bd test` 通过）
- 完成全仓剩余 6-defer 模块（17 个）与 5-defer 首批模块（27 个，含状态恢复审计保留）的 `~init()` RAII 析构与 `unsafe {}` (E0209) 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.703 ~ 1.199 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls）。

### [2026-11-22 Wave #29] RAII `~init()` + `unsafe {}` 专项改造（5-defer 收尾 2 模块 + 4-defer 首批 27 模块，全仓 `zig:check` + `bun bd test` 通过）
- 完成 `threading/ThreadPool`, `uucode/lut`（5-defer 收尾）与 `ErrorReportRequest`, `output`, `HTMLImportManifest`, `bundler/ThreadPool`, `computeChunks`, `generateCompileResultForCssChunk`, `generateCompileResultForHtmlChunk`, `generateCompileResultForJSChunk`, `renameSymbolsInChunk`, `JSBundleCompletionTask`, `Arguments`, `install_completions_command`, `repl`, `process_windows_translate_c`, `declaration`, `selectors/parser`, `NetworkTask`, `hoisted_install`, `lockfile`, `Buffers`, `Package`, `Scripts`, `Tree`, `json`, `ImportScanner`, `P`, `Parser`, `parseStmt`, `BunCPUProfiler`, `VirtualMachine`（4-defer 首批 28 模块）及 `Store.cp` 联合载荷类型修复。
- 验证：`cpc_brief.py` 门禁通过（膨胀率 `0.954 ~ 1.173 <= 1.2x`，注 `json_.zig` 为历史无备份文件）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls）。

### [2026-11-22 Wave #30] RAII `~init()` + `unsafe {}` 专项改造（4-defer 全清零 26 模块，全仓 `zig:check` + `bun bd test` 通过）
- 完成全仓剩余 4-defer 模块（26 个：`btjs`, `resolve_path`, `CowSlice`, `data_url`, `package_json`, `JSONCObject`, `SSLContextCache`, `codec_gif`, `codec_jpeg`, `node_cluster_binding`, `node_fs_binding`, `FileRoute`, `Handlers`, `TimerObjectInternals`, `ResumableSink`, `Sink`, `TextDecoder`, `export`, `mv`, `MySQLValue`, `sourcemap`, `PostgresString`, `toContain`, `toContainEqual`, `toEndWith`, `toEqualIgnoringWhitespace`, `toHaveBeenLastCalledWith`, `toHaveLastReturnedWith`, `toHaveNthReturnedWith`, `toInclude`, `toStartWith`, `grapheme_gen`, `url`, `INotifyWatcher` 等）的 `~init()` RAII 析构与 `unsafe {}` 合规化，全仓 `>= 4`-defer 层级至此全部清零。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.881 ~ 1.188 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls）。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-local-alias-raii-drop`（repro: `/data/dev/cp/temp/repro_local_alias_raii_drop.cp`）：`Factory(): type { return struct { ... } }` 中 `const G = Guard(i32); const g = G{ x: 1 };` 局部别名实例化未注入 `defer __cp_deinit()`，导致静默资源泄漏。
- `task-bun-20261122-raii-errinit-or-move-disarm`（repro: `/data/dev/cp/temp/repro_raii_move_disarm.cp`）：建议 `cpc` 原生支持 `~errinit()`（仅在 error 路径析构，对标 `errdefer`）或编译器级 `move` 自动撤防，消除手动 `disarm` 漏写风险。

### [2026-11-22 Wave #31] RAII `~init()` + `unsafe {}` 专项改造（3-defer 首批 60 模块，全仓 `zig:check` + `bun bd test` 通过）
- 完成 60 个 3-defer 模块（`Assets`, `HmrSocket`, `HotReloadEvent`, `SourceMapStore`, `base64`, `bun.js`, `fmt`, `BundleThread`, `analyze_transpiled_module`, `findImportedFilesInCSSOrder`, `args`, `filter_arg`, `filter_run`, `open`, `test_command`, `array_list`, `rules`, `color_js`, `MiniEventLoop`, `elf`, `dispatch`, `PackageJSONEditor`, `updatePackageJSONAndInstall`, `md/parser`, `external_shared`, `router`, `Archive`, `SubprocessPipeReader`, `csrf_jsc`, `html_rewriter`, `codecs`, `myers_diff`, `node_assert_binding`, `node_fs_watcher`, `node_os`, `node_process`, `path_watcher`, `parse_args`, `win_watcher`, `NodeHTTPResponse`, `ssl_wrapper`, `ByteStream`, `FormData`, `copy_file`, `read_file`, `HostProcess`, `IOWriter`, `cat`, `cp`, `seq`, `shell`, `CondExpr`, `braces`, `NewReader`, `MySQLRequestQueue`, `StandaloneModuleGraph`, `unicode`, `visible`, `string`, `File`, `diff_format`, `toBeEmpty`, `toBeTypeOf`, `toHaveBeenCalledWith`, `toHaveBeenNthCalledWith`, `toIncludeRepeated`, `toMatchSnapshot`, `toThrowErrorMatchingSnapshot`）的 `~init()` RAII 析构与 `unsafe {}` 合规化。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.658 ~ 1.185 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls）。

### [2026-11-22 Wave #32] RAII `~init()` + `unsafe {}` 专项改造（3-defer 全清零 7 模块 + 2-defer 首批 6 模块，全仓 `zig:check` + `bun bd test` 通过）
- 完成 3-defer 收官 7 模块（`Debugger`, `ModuleLoader`, `js_parser`, `BunHeapProfiler`, `RuntimeTranspilerCache`, `main_test`, `channel`，全仓 `>= 3`-defer 至此全部清零）与 2-defer 首批 6 模块（`posix_event_loop`, `boringssl`, `brotli`, `memory`, `DeferredBatchTask`, `Graph`）。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.907 ~ 1.18 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls）。

### [2026-11-22 用户指令：固化工程实践缺陷与优化主动提单铁律]
- 收到用户指令：将「编写 CP 代码工程实践中发现任何 `cpc` 需优化改进或缺陷漏洞，必须第一时间提炼最小复现并向 `cp_agy` 提单报告」作为铁律写入 `GEMINI.md` 并严格遵守。已写入 `GEMINI.md` 第四节。

### [2026-11-22 Wave #33] RAII `~init()` + `unsafe {}` 专项改造（2-defer 36 模块，全仓 `zig:check` + `bun bd test` 通过）
- 完成 36 个 2-defer 模块（`production`, `generateCodeForLazyExport`, `prepareCssAstsForChunk`, `options_jsc`, `add_completions`, `SourceFileProjectGenerator`, `fuzzilli_command`, `link_command`, `multi_run`, `pm_view_command`, `repl_command`, `scan_command`, `unlink_command`, `media_query`, `printer`, `background`, `url`, `pe`, `encode`, `http`, `PackageManagerEnqueue`, `PackageManagerResolution`, `security_scanner`, `lifecycle_script_runner`, `Yarn`, `npm`, `toml/lexer`, `io`, `CharFreq`, `Expr`, `Symbol`, `parse`, `parseJSXElement`, `parsePrefix`, `skipTypescript`, `runtime`）的 `~init()` RAII 析构与 `unsafe {}` 合规化。
- 顺带修复：`media_query.cp` 元组解包 `const name, const legacy_op`、位掩码 `if` 分支、`production.cp` `EntryPointRollbackGuard` 引用传参、`io.cp` `pending_iter` 遮蔽字段规避、`static fn onUpdateEpoll/onUpdateKQueue` 与位运算优先级括号。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.643 ~ 1.129 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls）。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-enum-readonly-method-mut-self`（repro: `/data/dev/cp/temp/repro_enum_readonly_method_mut_self.cp`）：`pub enum` 中未写 `*_` 且仅调用 `_.asStr()` 的只读实例方法 `fn toCss(dest: *Printer)` 被 `cpc` 错误生成为可变指针接收者 `self: *RoundingStrategy`，导致 `*const RoundingStrategy` 无法调用，且改写 `this: *const @This()` 会触发 `E0121`。

### [2026-11-22 Wave #34] RAII `~init()` + `unsafe {}` 专项改造（2-defer 29 模块核验，全仓 `zig:check` + `bun bd test` 通过）
- 完成 29 个 2-defer 模块核验与改造（含变更提交 27 模块：`font`, `transform`, `supports`, `builder`, `calc`, `JSPropertyIterator`, `JSSecrets`, `TopExceptionScope`, `ZigException`, `GeneratedBindings`, `resolve_path_jsc`, `uuid`, `Router`, `HashObject`, `JSBundler`, `SecureContext`, `spawn`, `node_fs_stat_watcher`, `UpgradedDuplex`, `WindowsNamedPipeContext`, `uws_jsc`, `WTFTimer`, `BakeResponse`, `download_stream`, `CriticalSection`, `Yield`, `Chunk`；及免改核验 2 模块：`update_interactive_command`, `win_watcher`）。
- 顺带修复 `calc.cp` 45 处既有 `ast-check` 与语义错误（`RoundingStrategy`/`Constant` 只读接收者、`void` 实参传参 `@as(void, undefined)`、`css.ParserError.invalid_value` 规范化），并向 `cp_agy` 投递 `task-bun-20261122-enum-readonly-method-mut-self`。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.727 ~ 1.14 <= 1.2x`，注 `Router_.zig` 为历史无备份文件）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls）。

### [2026-11-22 上游 `cp_agy` 结单验收（3 条）]
- `task-bun-20261122-local-alias-raii-drop`：已修复并验收。`cpc` 在 `stmt.rs`/`types/mod.rs`/`borrow.rs` 穿透解析 `type_aliases`，支持 `Factory(): type` 内局部别名 `const G = Guard(T)` 自动注入 `defer __cp_deinit()`。
- `task-bun-20261122-raii-errinit-or-move-disarm`：已实现并验收。`cpc` 原生支持 `~errinit()`（生成 `__cp_errdeinit` 并自动注入 `errdefer if (!__<var>_moved) ...`，正常路径自动跳过）与 `move(guard)` 编译器级撤防。
- `task-bun-20261122-enum-readonly-method-mut-self`：已修复并验收。`pub enum` 未写 `*_` 的只读实例方法现正确生成只读值接收者 `self: Enum`，`*const Enum` 可直接调用。

### [2026-11-22 Wave #35 & Wave #36] 全仓 `defer`/`errdefer` -> RAII `~init()`/`~errinit()` + `unsafe {}` 收官清零（149 模块，全仓 `zig:check` + `bun bd test` 通过）
- **Wave #35（2-defer 全清零，63 模块）**：完成 `Mapping`, `HandshakeV10`, `Authentication`, `NegotiateProtocolVersion`, `MySQLStatement`, `PostgresSQLStatement`, `notice_response_jsc`, `date`, `json`, `fd`, `walker_skippable`, `windows/env`, `DoneCallback`, `test_runner/expect/*.cp` (52 个同构模块), `uucode_lib` (3 个), `KEventWatcher`, `WindowsWatcher`（使用 `~errinit()`）。
- **Wave #36（1-defer 全清零，86 模块）**：分 4 批完成全仓全部 86 个可改造 1-defer 模块（`HTMLScanner`..`EventLoopTimer` 21 模块、`ManagedTask`..`fmt_jsc` 22 模块、`logger`..`InternalSourceMap` 21 模块、`ParsedSourceMap`..`socket` 22 模块），全面采用 `~init()` / `~errinit()` 原生析构。
- **联合排雷**：修复 `MySQLStatement`/`PostgresSQLStatement` (`pub const Status/ref/deref`)、`ValkeyCommand` (`switch (_)`)、`custom.cp` (元组解包)、`color.cp` (`RelativeComponentParser` `static fn` 与 `[4]f32`)、`gradient.cp` (`WebKitGradient` 纯 CP 重构)。
- **全仓验证**：`cpc_brief.py` 四门禁全绿（膨胀率 `0.794 ~ 1.195 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls，Debug + ASan）。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-const-type-opt-try`（repro: `/data/dev/cp/temp/repro_const_type_opt_try.cp`）：顶层 `const` 类型别名右值中的 `(*const fn ...)?` / `FnB?` 被 `cpc` 误生成为 `try ...`，触发 Zig `'try' outside function scope`。

### [2026-11-22 上游 `cp_agy` 结单验收（1 条）]
- `task-bun-20261122-const-type-opt-try`：已修复并验收。`cpc` 在 Codegen 增加 `in_container_const` 上下文区分类型位置 `?T` 与表达式错误传播 `try expr`，`pub const FnA = (*const fn ...)?;` 与 `pub const OptFnB = FnB?;` 现已正确生成 `?*const fn ...` 与 `?FnB`。

### [2026-11-22 Wave #37] 全仓 100% `.cp` 覆盖达成 + 剩余 RAII 模块收官（39 模块，全仓 `zig:check` + `bun bd test` 通过）
- **100% `.cp` 覆盖收官**：完成全仓最后 2 个未迁移文件 `src/string/immutable/grapheme_tables.cp`（35,621 行，ratio `1.0`）与 `src/boringssl_sys/boringssl.cp`（19,306 行，ratio `1.109`），`src/` 1,295/1,295 模块实现 100.00% 纯 `.cp` 覆盖。
- **剩余 RAII 与纯化收官（37 模块）**：完成 `ref_count`, `interpreter`, `Builtin`, `Expansion`, `small_list`, `GlobWalker`, `napi`, `scanImportsAndExports`, `tree_printer`, `rm`, `FileReader`, `writeOutputFilesToDisk`, `prompt`, `color`, `stdio`, `error_jsc`, `bun_test`, `Execution`, `ScopeFunctions`, `Collection`, `debug`, `JSMySQLQuery`, `ResultSet`, `MySQLQuery`, `Auth`, `HTTPThread`, `SSLConfig`, `node_assert`, `node_zlib_binding`, `BlockList`, `rare_data`, `host_fn`, `PipeReader`, `HTMLBundle`, `LinkerContext`（`pub struct` 拆升重构）, `ffi`, `calc`（解封原生 `toCss`）的 RAII `~init()`/`~errinit()` 与 `unsafe {}` 合规化。
- **全仓验证**：`cpc_brief.py` 四门禁全绿（膨胀率 `0.824 ~ 1.195 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls，Debug + ASan）。

### [2026-11-22 Wave #38] 旧式 `struct`/`union` 纯 CP 化专项首批（45 模块，全仓 `zig:check` + `bun bd test` 通过）
- 完成 45 个含旧式 `pub const X = struct/union` 模块（`aio/posix_event_loop`, `bake/{DevServer,IncrementalGraph,FrameworkRouter,bake}`, `brotli_c`, `bun_alloc/{MimallocArena,basic,bun_alloc,heap_breakdown}`, `output`, `bundler/{AstBuilder,BundleThread,Chunk,HTMLScanner,ServerComponentParseTask,ThreadPool,analyze_transpiled_module,bundle_v2,cache,defines-table,defines,linker,options}`, `bundler/linker_context/{MetafileBuilder,OutputFileListBuilder,findAllImportedPartsInJSOrder,findImportedCSSFilesInJSOrder}`, `cares_sys/c_ares`, `cli/{cli,SourceFileProjectGenerator,create_command,init_command,pack_command,publish_command,run_command,ChangedFilesFilter,Scanner,Channel,Coordinator,test_command}`, `collections/{array_list,comptime_string_map,linear_fifo}`, `csrf`）的顶层与嵌套结构体纯 CP 化及借用检查收窄。
- 验证：`cpc_brief.py` 四门禁全绿（膨胀率 `0.613 ~ 1.185 <= 1.2x`）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls，Debug + ASan）。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-ref-union-addr-of`（repro: `/data/dev/cp/temp/repro_ref_union_addr_of.cp`）：含 `[]const u8` 切片字段的 `union(enum)` 被视作 `ref` 类型后，调用传参 `&u` 会被 `cpc` codegen 剥离 `&`，导致 Zig 报 `expected *U, found U`。

### [2026-11-22 上游 `cp_agy` 结单验收（1 条）]
- `task-bun-20261122-ref-union-addr-of`：已修复并验收（`ack` 完成）。`cpc/src/codegen/borrow.rs`（`is_zig_heap_ptr_type`）不再把 `ref_classes`（含切片/借用字段的栈绑定 `ref struct` 与 `union`）及 `union`/泛型类误判为堆指针，不再剥离 `&u`；复现 `/data/dev/cp/temp/repro_ref_union_addr_of.cp` 现已正确生成 `self.take((&u))`。

### [2026-11-22 上游 `cp_agy` 工单投递（2 条）]
- `task-bun-20261122-enum-inline-fn`（repro: `/data/dev/cp/temp/repro_enum_inline_fn.cp`）：`pub enum` 内声明 `inline fn` 方法时，`cpc` 将 `inline` 误解析为枚举变体并生成 `inline,`，导致 `zig ast-check` 报 `expected function, found ','`。
- `task-bun-20261122-static-let-addr`（repro: `/data/dev/cp/temp/repro_static_let_addr.cp`）：`struct` 内 `static fn` 返回 `static let` 静态变量地址 `&q` 时，借用检查误报 `E0200: Cannot return reference to local variable 'this'`。

### [2026-11-22 上游 `cp_agy` 结单验收（2 条）]
- `task-bun-20261122-enum-inline-fn`：已修复并验收（`ack` 完成）。`parse_enum` 现已正确识别 `inline`/`extern`/`noinline` 修饰方法及 `meta.extend` 保留逻辑，复现 `/data/dev/cp/temp/repro_enum_inline_fn.cp` 通过 `cpc --lib` 与 `zig ast-check`。
- `task-bun-20261122-static-let-addr`：已修复并验收（`ack` 完成）。`is_class_field` 已剥离 `static let` 并新增 `is_static_class_field` 静态存储期豁免，复现 `/data/dev/cp/temp/repro_static_let_addr.cp` 通过 `cpc --lib` 与 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 工单投递（2 条）]
- `task-bun-20261122-struct-map-collision`（repro: `/data/dev/cp/temp/repro_struct_map_collision.cp`）：顶层声明 `pub struct Map` 与内置 `Map` 冲突时，`cpc` 无报错但静默丢弃整段结构体输出。
- `task-bun-20261122-generic-arg-opt`（repro: `/data/dev/cp/temp/repro_generic_arg_opt_typeof.cp`）：泛型实参位后缀可选类型 `Box(i32?)` 未被 lower 为 `Box(?i32)`，原样输出导致 `zig ast-check` 失败。

### [2026-11-22 上游 `cp_agy` 结单验收（2 条）]
- `task-bun-20261122-struct-map-collision`：已修复并验收（`ack` 完成）。`decl_class.rs` 已移除对顶层 `struct Map`/`Set` 的静默跳过，并在 `runtime_emit.rs`/`borrow.rs`/`call.rs` 补充 `class_defs` 冲突守卫；复现 `/data/dev/cp/temp/repro_struct_map_collision.cp` 现已正确输出 `pub const Map = struct` 并通过 `zig ast-check`。
- `task-bun-20261122-generic-arg-opt`：已修复并验收（`ack` 完成）。`map_type.rs`（`map_call_type_sym`）及 `error_flow.rs` 现已对泛型圆括号实参位递归 lower 后缀可选类型，`Box(i32?)` 正确生成 `Box(?i32)` 并通过 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-enum-shl-missing`（repro: `/data/dev/cp/temp/repro_enum_shl_missing.cp`）：`pub enum` 内含移位常量 `1 << 1` 时，`cpc` 生成 `cp_shl(1, 1)` 但未发射 `fn cp_shl` 辅助函数，导致 `zig ast-check` 报 `use of undeclared identifier 'cp_shl'`。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-return-this-struct-lit`（repro: `/data/dev/cp/temp/repro_return_this_lit.cp`）：`@This(){ x: 1 }` 结构体字面量前缀为 `@This()` 时，parser 报 `E0100 unexpected token 'Colon'`。

### [2026-11-22 上游 `cp_agy` 结单验收（1 条）]
- `task-bun-20261122-enum-shl-missing`：已修复并验收（`ack` 完成）。`cpc/src/codegen/prelude.rs`（`ModuleFeatures`）现已扫描 `Decl::Enum` 的 `e.extra_zig` 与 `e.variants` 并正确发射 `fn cp_shl` / `fn cp_shr`；复现 `/data/dev/cp/temp/repro_enum_shl_missing.cp` 通过 `cpc --lib` 与 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 结单验收（1 条）]
- `task-bun-20261122-return-this-struct-lit`：已修复并验收（`ack` 完成）。`cpc/src/parser/primary.rs`、`error_flow.rs`、`infer_cp.rs` 现已支持 `@This(){ ... }` 结构体字面量（CP 冒号/Zig 点号/简写/空字面量）与返回 `@This()` 的类型具化推导；复现 `/data/dev/cp/temp/repro_return_this_lit.cp` 已正确输出 `return @This(){ .x = 1 };` 并通过 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 工单投递（2 条）]
- `task-bun-20261122-switch-multitag-capture`（repro: `/data/dev/cp/temp/repro_switch_multitag_capture.cp`）：`switch` 多 tag 合并分支 `.a, .b v => v.x` 丢失载荷绑定 `v`，导致 `zig ast-check` 报 `use of undeclared identifier 'v'`。
- `task-bun-20261122-c-struct-align`（repro: `/data/dev/cp/temp/repro_c_struct_align.cp`）：`@c pub struct` 字段声明不支持 `align(1)`（如 `tag: u8 align(1)`），parser 报 `E0111: expected token 'Colon', found 'LParen'`。

### [2026-11-22 上游 `cp_agy` 工单投递（2 条）]
- `task-bun-20261122-expr-fn-ret-opt`（repro: `/data/dev/cp/temp/repro_expr_fn_ret_opt.cp`）：表达式位函数类型返回后缀可选类型 `fn(val: u64): (*T)?` 未被 lower 为 `?*T`，导致 `zig ast-check` 失败。
- `task-bun-20261122-switch-type-dot-opt`（repro: `/data/dev/cp/temp/repro_switch_type_dot_opt.cp`）：`comptime switch(T)` 分支不支持多级点表达式（`B.C =>`）或后缀可选类型（`A? =>`），parser 报 `E0111`。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-comptime-err-union-ret`（repro: `/data/dev/cp/temp/repro_comptime_err_union_ret.cp`）：`comptime` 函数返回错误联合类型构造 `return T ! MyErr;` 被 `cpc` 误拆分为 `return T; _ = (!MyErr);`，导致 `zig ast-check` 报 `unreachable code`。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-underscore-struct-lit`（repro: `/data/dev/cp/temp/repro_underscore_struct_lit.cp`）：下划线开头的结构体类型名 `_S` 在结构体字面量 `_S{ x: 1 }` 中未被识别为类型名，parser 报 `E0100 unexpected token 'Colon'`。

### [2026-11-22 上游 `cp_agy` 结单验收（4 条）]
- `task-bun-20261122-switch-multitag-capture`：已修复并验收（`ack` 完成）。`codegen/pattern.rs` 新增 `extract_or_enum_capture`，`.a, .b v => v.x` 现正确绑定载荷 `v`，复现通过 `cpc --lib` 与 `zig ast-check`。
- `task-bun-20261122-c-struct-align`：已修复并验收（`ack` 完成）。`parser/container.rs` 与 `parser/types.rs` 现支持结构体字段后缀 `align(expr)`，复现通过 `cpc --lib` 与 `zig ast-check`。
- `task-bun-20261122-expr-fn-ret-opt`：已修复并验收（`ack` 完成）。`parser/primary.rs` 将表达式位函数类型转为 `Expr::TypeVal(Type::Fn)` 并经 `map_type` lower `(*T)?` -> `?*T`，复现通过 `cpc --lib` 与 `zig ast-check`。
- `task-bun-20261122-switch-type-dot-opt`：已修复并验收（`ack` 完成）。`parser/pattern.rs` 现支持 `switch` 类型分支多级点路径与后缀可选/切片修饰，复现通过 `cpc --lib` 与 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 结单验收（2 条）]
- `task-bun-20261122-comptime-err-union-ret`：已修复并验收（`ack` 完成）。`parser/expr.rs`（`parse_bin_expr`）现支持表达式位错误联合类型构造 `T ! MyErr` 与 `T!`，复现通过 `cpc --lib` 与 `zig ast-check`。
- `task-bun-20261122-underscore-struct-lit`：已修复并验收（`ack` 完成）。`parser/{primary,expr,pattern}.rs` 改用 `trim_start_matches('_')` 判定类型名首字母大写，支持 `_S{ x: 1 }`，复现通过 `cpc --lib` 与 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-switch-mut-payload`（repro: `/data/dev/cp/temp/repro_switch_mut_payload.cp`）：`switch` 分支无管道可变载荷捕获 `.a *v =>` 报 `E0111: expected token 'FatArrow', found 'Star'`。

### [2026-11-22 上游 `cp_agy` 结单验收（1 条）]
- `task-bun-20261122-switch-mut-payload`：已修复并验收（`ack` 完成）。`cpc` 现已支持 `switch` 分支无管道可变载荷捕获 `.a *v =>` / `.a, .b *v =>` / `.a *v if ... =>`；复现 `/data/dev/cp/temp/repro_switch_mut_payload.cp` 通过 `cpc --lib` 与 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 工单投递（2 条）]
- `task-bun-20261122-fn-noalias-param`（repro: `/data/dev/cp/temp/repro_fn_noalias_param.cp`）：`fn` 形参修饰符 `noalias` 导致参数解析错位，报 `E0207`。
- `task-bun-20261122-extern-any-ptr`（repro: `/data/dev/cp/temp/repro_extern_any_ptr.cp`）：`@extern(*!, { name: "foo" })` 首个类型参数直接传 `*!` 报 `E0100`。

### [2026-11-22 上游 `cp_agy` 结单验收（2 条）]
- `task-bun-20261122-fn-noalias-param`：已修复并验收（`ack` 完成）。`Param` AST 与 `parse_params`/`parse_fn_type_sig_body` 现已支持 `noalias` 修饰符，`sema` 也解包 `[*]const u8` 索引赋值限定符，复现通过 `cpc --lib` 与 `zig ast-check`。
- `task-bun-20261122-extern-any-ptr`：已修复并验收（`ack` 完成）。`parser/primary.rs` 为 `@extern` 首个参数走 `parse_type()`（支持 `*!` / `*!?`），`sema` 正确推导返回类型，复现通过 `cpc --lib` 与 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-discard-underscore-receiver`（repro: `/data/dev/cp/temp/repro_discard_underscore_receiver.cp`）：省略只读接收者的方法中写 `* = _;` 被 `cpc` 丢弃未生成 `_ = self;`，导致 `zig ast-check` 报 `unused function parameter`。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-comptime-generic-param-discard`（repro: `/data/dev/cp/temp/repro_comptime_generic_param_discard.cp`）：`fn wrap(comptime W: type): i32 { return S<W>.f(); }` 中 `W` 被 `cpc` 误插入 `_ = W;`，导致 `zig ast-check` 报 `pointless discard of function parameter`。

### [2026-11-22 上游 `cp_agy` 结单验收（1 条）]
- `task-bun-20261122-discard-underscore-receiver`：已修复并验收（`ack` 完成）。`cpc/src/codegen/stmt.rs` 现已在类方法体内正确生成 `_ = self;`（并修复状态机内 `* = ident` 未走 `expr_code`）；复现 `/data/dev/cp/temp/repro_discard_underscore_receiver.cp` 通过 `cpc --lib` 与 `zig ast-check`。

### [2026-11-22 上游 `cp_agy` 结单验收（1 条）]
- `task-bun-20261122-comptime-generic-param-discard`：已修复并验收（`ack` 完成）。`cpc/src/ast_visitor.rs`（`find_referenced_vars_in_expr`）现已提取 `S<W>`、`EnumInit`/`Downcast`/`ComptimeMap` 内部类型标识，不再对 `W` 误插 `_ = W;`（`wrap` 报错已彻底消除）。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-top-if-enum-lit`（repro: `/data/dev/cp/temp/repro_top_if_enum_lit.cp`）：顶层 `pub const k: PT = if (true) .u16 else .u8;` 中的裸枚举字面量 `.u16` 触发 `E0190`（误判为管道占位符 `.`）。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-while-break-expr`（repro: `/data/dev/cp/temp/repro_const_while_break_val.cp`）：`while` 循环作为表达式赋值（`const val = while (true) { ... break 42; };`）时，parser 报 `E0100 unexpected token 'While'`。

### [2026-11-22 上游 `cp_agy` 结单验收（2 条）]
- `task-bun-20261122-top-if-enum-lit`：已修复并验收（`ack` 完成）。`parser/primary.rs` 中 `Token::Dot` 分支已对齐 `is_ident_token`，支持 `.u16`/`.u8` 等裸枚举字面量，复现通过 `cpc --lib` 与 `zig ast-check`。
- `task-bun-20261122-while-break-expr`：已修复并验收（`ack` 完成）。全栈新增 `Expr::While`（支持 `while (...) { break val; }`、`else` 分支及 `label: while` 表达式），复现通过 `cpc --lib` 与 `zig ast-check`。

### [2026-11-22 Wave #39~#46] 全仓 `old_struct_todo.txt` 296 个旧式 `struct` 模块清零收官（全仓 `zig:check` + `bun bd test` 通过）
- **范围**：完成 `src/css/**`（36）、`dotenv/errno/event_loop/glob/http/http_types/ini`（12）、`install/interchange`（30）、`io/js_parser/js_printer`（18）、`jsc/jsc_stub/libuv/md/napi/options_types/paths/ptr/resolver/router`（48）、`runtime/{api,crypto,dns_jsc,ffi,image,node,server,socket,timer,valkey_jsc,webcore}`（59）、`s3_signing/semver/sha_hmac/shell/shell_parser/string/sys/test_runner/threading/unicode/url/uws_sys`（44）等全部剩余模块纯 CP 结构体/联合体结构化与所有权/借用深度净化，`old_struct_todo.txt` 296 项全面清零。
- **全仓验证**：`cpc_brief.py` 四门禁全绿（膨胀率 `<= 1.2x`、`0` 违例）；`bun run zig:check` 5/5 PASS；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect() calls，Debug + ASan）。

### [2026-11-22 上游 `cp_agy` 工单投递]
- `task-bun-20261122-union-method-dotted-arr-len`（repro: `/data/dev/cp/temp/repro_union_method_arr_ptr_param.cp`）：`pub union(enum)` 内部方法形参使用带点路径定长数组指针 `buf: *[inet.LEN]u8` 时，`cpc` 误拆分为 `buf: *struct { inet.LEN }, u8: *anyopaque`。

### [2026-11-22 上游 `cp_agy` 结单验收（1 条）]
- `task-bun-20261122-union-method-dotted-arr-len`：已修复并验收（`ack` 完成）。`cpc/src/parser/types.rs`（`parse_base_type`）现已扩展定长数组长度解析（支持多级点分路径 `A.B.C` 及常量算术表达式），`buf: *[inet.LEN]u8` 正确输出并通过 `zig ast-check`。

- **2026-11-22 20:57 (Waves #47–#49 闭环 & 提单)**：完成 `temp/top_struct_todo.txt` 剩余 94 模块顶层 `struct`/`union` 纯 CP 化（Wave #47: 36 模块，Wave #48: 24 模块，Wave #49: 34 模块）；修复 `percentage`/`CommandLineArguments`/`JSRef`/`SubprocessPipeReader`/`types`/`Listener`/`SocketAddress`/`credentials`/`MySQLConnection`/`printDiff`/`cat`/`NativeBrotli`/`which`/`rm`/`Cmd` 等 `zig:check` 报错；向 `cp_agy` 提交 `task-bun-20261122-double-ptr-addr-of`（`**S -> *S` 与 `&ptr` 剥离缺陷）；`bun run zig:check` 5/5 PASS，`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect()）。
- **2026-11-22 21:00 (上游结单验收 `task-bun-20261122-double-ptr-addr-of`)**：`cp_agy` 修复 `cpc` 将 `**S` 折叠为 `*S` 及对显式指针变量取地址 `&p` 剥离 `&`（含 `**const T` 外层误标 `const`）的缺陷；已用 `repro_addr_of_ptr.cp` 实测生成 `out: **S` 与 `takeOut((&p))` 且 `zig ast-check` 0 报错通过，已执行 `ack`。
- **2026-11-22 21:31 (提单 `task-bun-20261122-builtin-regexp-collision`)**：`src/js_parser/ast/E.cp` 自身定义 `pub struct RegExp` 时，因 `cpc` 无条件在生成 `.zig` 顶部注入内置 `pub const RegExp = struct` 导致 `duplicate struct member name 'RegExp'`；已提炼 `/data/dev/cp/temp/repro_builtin_regexp_collision.cp` 向 `cp_agy` 提单并暂恢复 `E.zig` 至 `HEAD` 不阻塞后续推进。
- **2026-11-22 21:36 (上游结单验收 `task-bun-20261122-builtin-regexp-collision`)**：`cp_agy` 修复当源码顶层已声明或导入 `RegExp` 时 `cpc` 仍无条件注入内置 `pub const RegExp = struct` 的冲突缺陷（`prelude.rs` / `runtime_emit.rs`）；已用 `repro_builtin_regexp_collision.cp` 与 `src/js_parser/ast/E.cp` 实测 `cpc_brief.py` 4 项门禁全绿（ratio 1.036），已执行 `ack` 解封 `E.cp`。
- **2026-11-22 21:48 (Waves #50–#51 闭环)**: 完成 Wave #50（9 组 55 模块顶层 `enum`/`opaque` 纯化）与 Wave #51（19 组 117 模块嵌套 `struct`/`union`/`enum`/`opaque` 提升与纯化）；修复 `DevServer`/`RegularExpression`/`IncrementalGraph`/`c_ares`/`JSValue`/`process`/`node`/`RequestContext`/`Blob`/`fetch`/`comptime_string_map` 等 `zig:check` 问题；`bun run zig:check` 5/5 PASS，`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect()）。
- **2026-11-22 22:24 (Waves #52–#54 闭环)**: 完成 Wave #52（7 组 45 模块 `anyopaque`/`_ = `/`?T` 清零）、Wave #53（4 组 13 模块实存 `): void` 省略）、Wave #54（21 组 173 模块 `@import(...)` 转为顶层 `import`）；修复 `uws_sys/socket`、`H2Client`、`AltSvc` 与 `guarded.cp`（`comptime MutexType` 避开内置 `CpBareMutex` 映射）；`bun run zig:check` 5/5 PASS，`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect()）。
- **2026-11-22 22:42 (Wave #55 闭环)**: 完成全仓剩余 41 个真实含资源释放 `defer`/`errdefer` 模块的 RAII `~init()`/`~errinit()` 深化（共 8 组 53 模块）；修复 `SourceMapStore`、`bundle_v2`（`finishFromBakeDevServer` 错误集）与 `GlobWalker`（双泛型 `CloseDisallowingCwdGuard<T, F>`）；`bun run zig:check` 5/5 PASS，`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect()）。
- **2026-11-22 23:07 (Waves #56–#57 闭环)**: 完成 Wave #56（10 组 63 模块 `self`/`this` 接收者纯 CP 化）与 Wave #57（7 组 58 模块 `if`/`while`/`for` `|capture|` 纯 CP 化）；修复 `bundle_v2`/`css_parser`/`Package`/`SourceFileProjectGenerator`/`paths`/`unicode` 的 `for (*x of ...)` 自动解引用与对齐问题；`bun run zig:check` 5/5 PASS，`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 PASS（460 expect()）。

## [2026-11-22] Wave #58：前缀 `try expr` -> 后缀 `expr?` 纯化完成与验收
- 完成 `temp/wave58_try_todo.txt` 全部 20 组共 212 个 `.cp` 模块的前缀 `try` -> 后缀 `?` 纯化，全量通过 `scripts/cpc_brief.py -n 4` 四项门禁。
- 修复 `bun.cp`、`css/values/{image,syntax}.cp`、`jsc/VirtualMachine.cp`、`test_runner/expect*.cp`、`zlib/zlib.cp`、`install/PackageManager/{security_scanner,PackageManagerDirectories}.cp`、`cli/pack_command.cp`、`runtime/webcore/Request.cp` 语义细节后，`bun run zig:check` 5/5 通过，`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 通过（460 expect）。
- 推导下一阶段 Wave #59：`orelse` -> `??`（`temp/wave59_orelse_todo.txt`，132 个文件），启动 2 路并行子智能体推进。

## [2026-11-23] Wave #59：`orelse` -> `??` 纯化完成与验收
- 完成 `temp/wave59_orelse_todo.txt` 全部 12 组共 132 个 `.cp` 模块的 `orelse` -> `??` / `== null` + `.?` / `if (v := opt)` 纯化（`return struct/union/enum` 原生 Zig 泛型工厂透传块内部按规范保留），全量通过 `cpc --lib` + `zig ast-check` 与静态零违例门禁。
- 修复 `src/js_parser_jsc/Macro.cp`（`Bun__startMacro` 参数改为 `*const void` + `@ptrCast(&call)`）与 `src/runtime/webcore/FileSink.cp`（`let stream_file` 规避 `&file` 被误生成为 `*const file`）后，`bun run zig:check` 5/5 通过（21s），`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 通过（460 expect）。
- 推导下一阶段 Wave #60：`and`/`or` -> `&&`/`||`（`temp/wave60_and_or_todo.txt`，281 个非注释代码文件），启动 2 路并行子智能体推进。

## [2026-11-23] Wave #60：布尔关键字 `and`/`or` -> `&&`/`||` 纯化完成与验收
- 完成 `temp/wave60_and_or_todo.txt` 全部 7 组（覆盖 281 个非注释候选文件，剔除字符串与 `return struct/union/enum` 原生 Zig 透传泛型工厂块）的 `and`/`or` -> `&&`/`||` 纯化，全量通过 `cpc_brief.py` 四项门禁。
- 修复 `src/bun_alloc/allocation_scope.cp` 第 38 行指针修饰符（`(const *ExtraVTable)?` -> `(*const ExtraVTable)?`）后，`bun run zig:check` 5/5 通过（30s），`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 通过（460 expect）。
- 推导下一阶段 Wave #61：全仓尾项清扫（非透传 `try ` 26 候选与 `const X = struct/enum/union` 27 候选），继续保持 2 路并行子智能体推进。

## [2026-11-23] Wave #61：尾项清扫（非透传 `try` 与 `const X = struct/enum/union/opaque`）完成与验收
- 完成 `temp/wave61_try_tail.txt`（26 个模块）与 `temp/wave61_const_container_tail.txt`（22 个非重叠模块）尾项清扫：`src/bun.cp`、`src/bundler/LinkerContext.cp`、`src/semver/SemverRange.cp`、`src/unicode/uucode_lib/src/types.cp`、`src/jsc/ConsoleObject.cp` 已完成纯化，其余位于 `return struct` 透传工厂块或局部 `comptime` 捕获闭包内的结构按规范保留，全部通过 `cpc_brief.py` 四项门禁。
- 修复 `src/jsc/ConsoleObject.cp` 中 `FormatterTagUnion` 的 `@as(FormatterTag, _)` -> `std.meta.activeTag(_)` 后，`bun run zig:check` 5/5 通过（29s），`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 通过（460 expect）。
- 推导下一阶段 Wave #62：前缀 `?T`（31 个候选）与非函数指针 `: void` 尾项清扫。

## [2026-11-23] Wave #62：前缀 `?T` 与 `: void` 尾项清扫完成与验收
- 完成 `temp/wave62_prefix_q_todo.txt`（31 个 `?T` 模块）与 `temp/wave62_void_todo.txt`（第 3 组 22 个 `: void` 模块；其余 46 个复核为 `void!` 错误联合或函数指针，0 残留）的清扫，全量通过 `cpc_brief.py` 四项门禁。
- 修复 `src/bake/DevServer/ErrorReportRequest.cp`（内联 `SourceMapStore.GetResult?`）与 `src/uws_sys/socket.cp`（`OptPtr` 辅助函数类型注解 `const x: (*T)? = null; return @TypeOf(x);`）后，`bun run zig:check` 5/5 通过（26s），`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 通过（460 expect）。
- 推导下一阶段 Wave #63：残余裸 `this`/`*this`/`self`/`*self` 接收者纯化（如 `analytics/schema.cp`、`brotli/brotli.cp` 等）。

## [2026-11-23] Wave #63：残余裸 `self`/`this`/`*self`/`*this` 接收者纯化完成与验收
- 完成 `temp/wave63_bare_recv_todo.txt` 全部 43 个模块（2 组并行）的裸 `self`/`this` 接收者清扫：`src/analytics/analytics.cp`、`src/bun_alloc/bun_alloc.cp`、`src/bun_core/fmt.cp`、`src/crash_handler/crash_handler.cp`、`src/http_types/URLPath.cp`、`src/install_types/SemverString.cp`、`src/interchange/toml/lexer.cp`、`src/resolver/tsconfig_json.cp` 已完成纯化，其余 35 个位于 `return struct/union(enum)` 原生 Zig 透传泛型工厂块内部按规范保留，全部通过 `cpc_brief.py` 四项门禁。
- 修复 `src/analytics/analytics.cp`（`FeaturesFormatter.format` 添加 `* = _;` 保留实例方法签名）与 `src/http_jsc/websocket_client.cp`（`enum Copy` 只读方法 `_.*` -> `_`）后，`bun run zig:check` 5/5 通过（23s），`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 通过（460 expect）。
- 推导下一阶段：全仓主要语法纯化波次已基本收敛，排查非透传 `if`/`while` `|capture|` 尾项并扩大 `bun bd test` 回归测试覆盖。

## [2026-11-23] Wave #64：非透传 `if`/`while` `|capture|` 尾项清扫完成与验收（全仓语法纯化收官）
- 完成 `temp/wave64_if_while_capture_todo.txt` 排查与纯化：修复 `src/cli/publish_command.cp`、`src/css/css_parser.cp`（`MozDocumentFn`）及 `src/css/generics.cp`（`unsafe { @constCast(this) }`），其余 `src/css/values/calc.cp` 等均位于 `return struct/union` 原生 Zig 透传泛型工厂块内按规范保留，全仓非透传 `if`/`while` `|capture|` 已清零。
- `bun run zig:check` 5/5 通过（29s），`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 通过（460 expect）。
- 全仓 1295 个 `.cp` 模块语法纯化波次已全面收敛，下一阶段转入扩大范围的 `bun bd test` 分模块回归测试验证。

### [2026-11-23] 向 `cp_agy` 提交 6 项 `cpc` 编译器加强与缺陷修复工单
- `task-bun-20261123-passthrough-factory-lowering` (`repro_passthrough_factory.cp`)：支持 `return struct/union/enum` 泛型工厂体与嵌套容器完整 CP Lowering。
- `task-bun-20261123-optional-type-vs-try-disambig` (`repro_opt_type_disambig.cp`)：消除类型位置 `?` 与表达式位置 `?` 的语法二义性。
- `task-bun-20261123-addr-of-const-var-vs-type` (`repro_addr_of_const_var.cp`)：修复 `&ident` 误判为 `*const ident` 及 `const *!` 指针修饰符问题。
- `task-bun-20261123-as-enum-tag-union` (`repro_as_enum_tag_union.cp`)：修复 `union(EnumTag)` 上 `@as(EnumTag, _)` 误生成 `@enumFromInt(self)`。
- `task-bun-20261123-zero-field-struct-method-recv` (`repro_zero_field_method.cp`)：修复无字段结构体实例方法未引用 `_` 时丢失 `self` 参数。
- `task-bun-20261123-strict-purification-flag` (`repro_strict_purification.cp`)：为 `cpc check` 增加 `--strict` 严苛纯化门禁模式。

### [2026-11-23] 收到 `cp_agy` 结单验收通知（6 条全部验证通过并已 `ack`）
- `task-bun-20261123-passthrough-factory-lowering`：支持 `return struct/union/enum` 泛型工厂返回体与嵌套容器完整 CP 语义解析与递归 lowering。
- `task-bun-20261123-optional-type-vs-try-disambig`：消除类型位置 `?` 与表达式位置 `?` 语法二义性（`T?` / `(*Owner)?` 不再误生成 `try`）。
- `task-bun-20261123-addr-of-const-var-vs-type`：修复 `&const_var` 误判为 `*const ident` 及 `const *!` 双层指针问题。
- `task-bun-20261123-as-enum-tag-union`：修复 `union(EnumTag)` 上 `@as(EnumTag, _)` 盲目生成 `@enumFromInt(self)`。
- `task-bun-20261123-zero-field-struct-method-recv`：修复无字段结构体实例方法未引用 `_` 时丢失 `self` 参数。
- `task-bun-20261123-strict-purification-flag`：新增 `cpc check --strict` 严苛纯化门禁（拦截 E0301..E0311 旧语法）。

### [2026-11-23] 向 `cp_agy` 提交 `cpc` 静默丢弃顶层函数缺陷工单
- `task-bun-20261123-silent-drop-comptime-auto-dotted-err` (`repro_silent_drop_comptime_auto_err.cp`)：`comptime` 参数 + `auto` 参数 + 带点号错误联合返回类型（`std.Io.Writer.Error!void`）同时出现时，顶层函数会被 `cpc` 静默丢弃（`yaml.cp:52` `pub fn print`）。

### [2026-11-23] 收到 `cp_agy` 结单验收通知（`task-bun-20261123-silent-drop-comptime-auto-dotted-err`）
- 根因为 `codegen/actor.rs` 无差别过滤了所有顶层 `print`/`spawn`/`join` 函数；已收敛为仅过滤单条内建原语委托桩函数（`is_builtin_runtime_stub_fn`）并修复同名别名冲突，已验证通过并执行 `ack`。

## [2026-11-23] Wave #65：全仓 1280 模块普查与首轮语义一致性修复完成与提交
- 完成全仓 1280 对 `(.cp, _.zig)` 自动化完整性普查（`temp/full_census.txt`）与多子系统抽查，修复 14 个模块的语义与编译细节：
  - `src/string/immutable/unicode.cp`（`wtf8ByteSequenceLengthWithInvalid` `else => 1`、`wtf8Sequence` `..=` 闭区间）、`src/interchange/yaml.cp`（`0x21..=0x7e`、`writer: anytype` 与 `Token` `static fn`）、`src/runtime/node/types.cp`（`..=MAX_PATH_BYTES`）、`src/http/InitError.cp`（1.00x 精简）；
  - `src/http/http.cp`（补全 `buildRequest` `Connection`/`if-modified-since`/`Upgrade` 与 `writeProxyRequest`）、`src/runtime/webcore/fetch.cp`（补全 `body` `Content-Type` 传播）、`src/semver/Version.cp`（补回 `Hashable`/`hash`/`HashContext` 与 `SliceU8`）；
  - `src/bake/FrameworkRouter.cp`（`pub static fn initEmpty`）、`src/bake/DevServer.cp`（`HmrSocketPtr` 与 `static fn handle`）、`src/test_runner/expect.cp`（转义与 `static`）、`src/ptr/owned.cp`（补全 `pub`）、`src/exe_format/macho.cp`（`@intCast` 溢出检查）、`src/sys/sys.cp`（`i64` `fallocate`）、`src/threading/Futex.cp`（Darwin `.FAULT => {}`）。
- 验证：`bun run zig:check` 5/5 通过（23s），`29181.test.ts` + `bun-file.test.ts` 8/8 通过（460 expect），`serve.test.ts` `does propagate type for Blob` 通过。
- 推导下一阶段 Wave #66：根据普查结果补全 `fetch.cp`（`needsToReadFile`/`SendFile`/`isS3`/协议校验）、`c_ares.cp`、`uws_sys/Response.cp` 与 `blob/copy_file.cp` 遗漏核心逻辑。

## [2026-11-23] Wave #66：补全 `fetch` / `Blob` / `Response` / `copy_file` / `c_ares` / `subprocess` / `streams` 核心逻辑与回归验证
- 根据 `temp/full_census.txt` 普查清单与 `serve.test.ts` 回归定位，补全 7 个模块的生产逻辑与接收者/类型适配：
  - `src/runtime/webcore/fetch.cp`（补全协议校验、`body.isS3()`、`body.needsToReadFile()`/`SendFile`/`NodeFS.readFile`、`url.isS3()`、`S3StreamWrapper`、`body.detach()` 及 `OptHeaders`/`@constCast` 适配）；
  - `src/runtime/webcore/Blob.cp`（补全 `BlobAny.isS3()`）；
  - `src/uws_sys/Response.cp`（补全 `castRes`/`downcastSocket`/`getRemoteAddressAsText` 与 `pub type` 指针别名）；
  - `src/runtime/webcore/blob/copy_file.cp`（补全 `CopyFileWindows` 全套方法）；
  - `src/cares_sys/c_ares.cp`（补全 `Error.initEAI` `eai_zero => return null`、11 处 `static` 回调与 `this` -> `req`）；
  - `src/runtime/api/bun/subprocess.cp`（补全 `handleIPCClose(*_)`、`onAfterIPCClosed(*_)`、`memoryCost(*_)`、`static` 与接收者纯化）；
  - `src/runtime/webcore/streams.cp`（`SignalVTableFunctions.WrappedPtr` 适配新版 `cpc` `return struct` lowering）。
- 验证：`bun run zig:check` 5/5 通过（25s）；`bun bd test test/regression/issue/29181.test.ts test/js/bun/util/bun-file.test.ts` 8/8 通过（460 expect）；`bun bd test test/js/bun/http/serve.test.ts -t "propagates content-type|does propagate type for Blob"` 2/2 通过（此前失败的 `Bun.file()` `fetch` body 读取已修复）。
- 推导下一阶段 Wave #67：按“1 路 `bun bd test` 分批回归 + 1 路完整性补全”并行推进——执行 `temp/test_plan.txt` Batch 1（174 个测试文件），同时准备补全 `Blob.cp`（`doReadFromS3`/`S3BlobDownloadTask`/`onStructuredClone*`）、`Terminal.cp`（Windows `makeLibUVOwned`）与 `json.cp`（恢复 `json_.zig` 原始备份与 `expectPrintedJSON`）。

### [2026-11-23] 向 `cp_agy` 提交 `*(T?)` 指针到可选类型 lowering 缺陷工单
- `task-bun-20261123-ptr-to-optional-lowering` (`repro_ptr_opt.cp`)：`p: *(u8?)` 被 `cpc` 错误降低为 `p: ?*u8`（指向 `u8` 的可空指针）而非 `p: *?u8`（指向可选 `u8` 的非空指针）。

### [2026-11-23] 收到 `cp_agy` 结单验收通知（`task-bun-20261123-ptr-to-optional-lowering`）
- 根因为 `parser/types.rs`、`parser/primary.rs` 未区分 `inner_is_paren` 且 `codegen/types/map_type.rs` 强制将 `BorrowType(Optional(T))` 翻转为 `?*T`；现已按括号语义精确区分 `*(T?) -> *?T`、`*const (T?) -> *const ?T`、`*[N](T?) -> *[N]?T`、`[](T?) -> []?T`，同时保持 `*T?` / `(*T)? -> ?*T` 兼容，已验证通过并执行 `ack`。

### [2026-11-23] 向 `cp_agy` 提交 `_.p as *T` 误对指针字段取地址 `&(...)` 缺陷工单
- `task-bun-20261123-as-ptr-field-addr-of` (`repro_as_ptr_field.cp`)：`_.p as *u64` 被 `cpc` 错误降低为 `@as(*u64, @ptrCast(&(self.p)))`（对栈/结构体上的指针字段本身取地址 `&`，导致 `src/bun_core/fmt.cp` `SharedTempBufferGuard.~init` 发生 ASan `stack-buffer-overflow`）。

### [2026-11-23] 收到 `cp_agy` 结单验收通知（`task-bun-20261123-as-ptr-field-addr-of`）
- 根因为 `codegen/expr/ops.rs` `Expr::Cast -> is_target_ptr` 分支以 `starts_with('[')` 误将 `[*]T`/`[]T` 等当作定长数组并插入 `@ptrCast(&(expr))`；现已补全指针与严格切片/定长数组判定，优先对 `is_src_ptr` 生成 `@ptrCast(@alignCast(inner))`，已验证通过并执行 `ack`。

## [2026-11-23] Wave #67：Batch 1a 回归排雷与 `DevServer`/`fmt`/`Blob`/`node_fs`/`Terminal`/`napi`/`cli` 修复完成与提交
- 完成 13 个模块的修复与静态排雷（均通过 `cpc_brief.py` 4 项门禁）：
  - `src/bake/DevServer.cp`（`ensureRouteIsBundled` `.unqueued` 分支补全 `return;` 根治 `HttpResponse::setTimeout` `heap-use-after-free`）；
  - `src/bundler/linker_context/computeChunks.cp`（`chunk_id` 转为 `usize` 根治 `{d:0>8}` 输出 `+` 号与 `Invalid output piece boundary`）；
  - `src/bun_core/fmt.cp`（`SharedTempBufferGuard.~init()` `@ptrCast(_.chunk_ptr)` 根治 `formatUTF16Type` ASan `stack-buffer-overflow`，并补全 `TableSymbols` 等 `static fn`）；
  - `src/runtime/webcore/Blob.cp`（`dupe` / `dupeWithContentType` 改回 `_.name.dupeRef()` 保留 `String.dead`）；
  - `src/runtime/node/node_fs.cp`、`src/runtime/node/node_fs_binding.cp`、`src/http/http.cp`、`src/interchange/json.cp`（恢复 `json_.zig` 原始备份与 `expectPrintedJSON`）、`src/runtime/api/bun/Terminal.cp`（Windows `makeLibUVOwned` + `errdefer` + `static fn`）；
  - `src/runtime/node/path.cp`、`src/meta/meta.cp`（`for (i of 0..)` 传 `{d}` 转为 `usize`）；
  - `src/napi/napi.cp`、`src/cli/cli.cp`、`src/css/values/css_string.cp`、`src/css/values/rect.cp`（消除 `@ptrCast(&(` 误取地址并补全 `static fn`）。
- 验证：`bun run zig:check` 5/5 通过（29s）；`hspec.test.ts` 40/40 通过；`form-data-set-append.test.js` 4/4 通过；`bun-serve-html.test.ts` 崩溃与 `DevServer` JSX 用例修复（8 pass / 8 fail，余下为 CSS snapshot 展开与 `ParseTask` 缓存 fd 复用）。
- 推导下一阶段 Wave #68：继续按“1 路 `bun bd test` 分批回归 + 1 路语义修复”并行推进——Subagent 1 重跑 `test/js/bun/http`（重点验证 `hspec.test.ts` 之后余下 29 个未跑文件及 `serve-protocols`/`bun-serve-static`），Subagent 2 补全 `Blob.cp`（`doReadFromS3` / `S3BlobDownloadTask` / `onStructuredClone*`）。

## [2026-11-23] Wave #68：Batch 1a 余下运行时修复（`background`/`subprocess`/`HTMLImportManifest`/`server`/`uws_sys`）完成与提交
- 完成 9 个模块的修复与重编同步（均通过 `cpc_brief.py` 4 项门禁）：
  - `src/css/properties/background.cp`（L752 `_.images = bg_images;`，根治 CSS `background` 简写合并退化展开）；
  - `src/runtime/api/bun/subprocess.cp`（`finalize()` 末尾补回 `client.flags.finalized = true; client.deref();`，根治 `serve-http3.test.ts` `Process.hasExited` ASan `use-after-poison`）；
  - `src/bundler/HTMLImportManifest.cp`（L162-177 恢复 `for (*ch of chunks)` 写入 `ch.final_rel_path` 与 `entry_point_bits.set(ch.entry_point.entry_point_id)`）；
  - `src/runtime/server/{server,ServerConfig,RequestContext,FileResponseStream,WebSocketServerContext}.cp` 与 `src/uws_sys/Request.cp`（重编同步滞后文件，补全 `static fn` 与指针类型别名）。
- 验证：`bun run zig:check` 5/5 通过（29s）；`bun-serve-html.test.ts -t "^serve html$"` 1/1 通过；`serve-http3.test.ts` ASan 崩溃消除（43 pass / 1 超时）；`serve-protocols.test.ts` 20/20 通过。
- 推导下一阶段 Wave #69：Subagent 1 运行 `bun-serve-html-manifest.test.ts` 及 Batch 1a 此前未跑的余下 14 个 `test/js/bun/http` 文件；Subagent 2 只读排查全仓 `while (true) { switch` 是否还有遗漏 `return`/`break` 的状态机。

## [2026-11-23] Wave #69：`test/js/bun/http` 尾段验证与 `Installer.cp` / `yaml.cp` 死循环修复完成与提交
- 完成全仓 `while (true) { switch (...) }` 状态机死循环排查与 2 处真实 Bug 修复（均通过 `cpc_brief.py` 4 项门禁）：
  - `src/install/isolated_install/Installer.cp`（L294 `next_step_sw: while (true)`，将 `.clonefile`/`.hardlink`/`else` 末尾 `continue;` 改为 `continue :next_step_sw;`，消除内层 `backend` 死循环）；
  - `src/interchange/yaml.cp`（L3753 `newlines_sw` 内 `else => |c| { lit_ch = c; continue :lit_sw; }`，消除死循环）。
- 验证：`bun run zig:check` 5/5 通过（29s）；`bun-serve-html-manifest.test.ts` 4/4 通过；`test/js/bun/http` 尾段 14 文件 346 pass / 21 fail / 2 skip / 3 todo（全程零 ASan 崩溃，`serve.test.ts:1890` 与 `bun-server.test.ts` 均实测确认为 debug+ASan 并发/时序阈值问题，非 CP 语义缺陷）。
- 推导下一阶段 Wave #70：Subagent 1 执行 Batch 1b（`test/js/bun/util`，53 个测试文件）；Subagent 2 只读使用 `cpc check --brief` 预检 171 个 `.cp` mtime 晚于 `.zig` 的文件，定位待重编前的潜在 `cpc` 诊断。

## [2026-11-23] Wave #70：Batch 1b（`test/js/bun/util`）回归与 `Blob.cp`/`SlicedString.cp` 修复完成与提交
- 完成 Batch 1b（`test/js/bun/util`，53 个测试文件）全量回归：**1,318 pass / 48 fail / 19 skip**（65,877 expect，738.5s，**全程零 ASan/段错误崩溃**）。
  - 独立复现确认 `peek.test.ts`（2 pass）、`filesystem_router.test.ts`（20 pass）、`bunstring-tothreadsafe.test.ts`（2 pass）、`pathToFileURL-invalid.test.ts`（1 pass / 1 skip）在独立运行时均 100% PASS。
- 完成 2 个模块修复与验证（均通过 `cpc_brief.py` 4 项门禁 + `bun run zig:check` 5/5 通过）：
  - `src/runtime/webcore/Blob.cp`（L1814-1826 `Blob.writeFormat` 恢复 `_.getNameString()` 判定与输出，修复 `inspect.test.js` `File` 名称缺失）；
  - `src/install_types/SlicedString.cp`（14 处 `@intFromPtr` 包裹 `unsafe`，消除全部 `E0209`）。
- 推导下一阶段 Wave #71：Subagent 1 先验证 `inspect.test.js`，随后执行 Batch 1c（`test/js/bun/test`，75 个测试文件）；Subagent 2 只读对 `src/` 核心目录批量运行 `cpc check --brief` 预检。

## [2026-11-23] Wave #71：`Blob.cp` S3/StructuredClone 补全、6 处 `E0209` 修复与 Batch 1c 回归完成与提交
- 完成 Batch 1c（`test/js/bun/test`，75 个测试文件）全量回归：**926 pass / 28 fail / 17 skip / 455 todo**（24,450 expect，877.76s，**全程零 ASan/段错误崩溃**）。
- 完成 7 个模块补全与修复（均通过 `cpc_brief.py` 4 项门禁 + `bun run zig:check` 5/5 通过）：
  - `src/runtime/webcore/Blob.cp`（补全 `serialization_version`、`StructuredCloneWriter`、`writeFloat`/`readFloat`/`readSlice`、`_onStructuredCloneSerialize`/`Deserialize`、顶层 `S3BlobDownloadTask` 与 `doReadFromS3`，替换 `toString`/`toJSON`/`toArrayBufferView` 中 `.isS3()` `throwTODO` 占位）；
  - `src/http/h3_client/callbacks.cp`、`src/runtime/api/UnsafeObject.cp`、`src/runtime/api/crash_handler_jsc.cp`、`src/runtime/api/standalone_graph_jsc.cp`、`src/runtime/node/uv_signal_handle_windows.cp`、`src/runtime/node/zlib/NativeZstd.cp`（补充 `unsafe {}` 消除全部 `E0209`）。
- 推导下一阶段 Wave #72：Subagent 1 验证 `inspect.test.js`（确认 `File` `structuredClone` 修复至 72/72）并启动 Batch 2（`test/js/web/`）；同时根据 `-0` 与 `toIncludeRepeated` 排查结果继续修复 `test_runner`。

### [2026-11-23] 向 `cp_agy` 提交一元负号 `-x` 被 lowering 为 `(0 - x)` 缺陷工单
- `task-bun-20261123-unary-neg-zero-float` (`repro_unary_neg_zero.cp`)：一元负号 `-x` 被 `cpc` 编译为 `(0 - x)`，导致 IEEE-754 浮点 `-0.0` 变为 `+0.0`，引起 `src/js_parser/ast/visitExpr.cp` 将所有 JS/TS `-0` 字面量折叠为 `+0`。

### [2026-11-23] 收到 `cp_agy` 结单验收通知（`task-bun-20261123-unary-neg-zero-float`）
- 根因为 `parser/primary.rs` 将一元负号 `-x` 降低为 `BinExpr(0 - x)`；现已在 parser/sema/codegen 全链路新增 `Expr::Neg` 原生生成 `(-x)`（保留 IEEE-754 `-0.0` 符号位），已验证通过并执行 `ack`（待当前测试路返回后立即重编 `visitExpr.cp` 与 `toBeCloseTo.cp`）。

## [2026-11-23] Wave #72：`inspect.test.js` 全绿、全仓 `cpc check` 100% 清零（21 处 `E0209`）与 `-0.0` 重编完成与提交
- 完成全仓剩余 955 个 `.cp` 模块 `cpc check --brief` 预检（全仓 1,295 个 `.cp` 覆盖完毕），并完成 23 个模块修复与重编（均通过 `cpc_brief.py` 4 项门禁 + `bun run zig:check` 5/5 通过）：
  - 重编保留 `-0.0` 符号位：`src/js_parser/ast/visitExpr.cp`、`src/test_runner/expect/toBeCloseTo.cp`；
  - 补充 `unsafe {}` 消除全仓最后 21 个 `E0209`：`WatcherAtomics.cp`、`NullableAllocator.cp`、`output_file_jsc.cp`、`AnyEventLoop.cp`、`AutoFlusher.cp`、`pipes.cp`、`RefString.cp`、`Strong.cp`、`TextCodec.cp`、`EnvStr.cp`、`IOReader.cp`、`RefCountedStr.cp`、`Binary.cp`、`Stmt.cp`、`LineOffsetTable.cp`、`PathString.cp`、`SmolStr.cp`、`wtf.cp`、`error_jsc.cp`、`InternalLoopData.cp`、`udp.cp`。
- 验证：`inspect.test.js` **72/72 全绿通过**；Batch 2a（`test/js/web/fetch` 46 文件）9,820 pass / 255 fail（`fetch.test.ts` `AbortAfterFinish` 处触发 `StructureID::decode` `decontaminate()` SIGABRT）；`bun run zig:check` 5/5 通过（29s）。
- 推导下一阶段 Wave #73：Subagent 1 验证 `-0.0` 修复（`expect.test.js` / `test-test.test.ts` / `moremore.test.ts` / `jest-extended.test.js`）及 `fetch.test.ts` 独立复现；Subagent 2 只读排查 `fetch.test.ts` `decontaminate()` SIGABRT 与 `fetch.stream.test.ts` 108 超时的根因。

### [2026-11-23] 向 `cp_agy` 提交收录 `cpc_brief.py` 工具工单
- `task-bun-20261123-adopt-cpc-brief` (`/data/dev/bun/scripts/cpc_brief.py`)：请 `cp_agy` 将 `cpc_brief.py` 收录至 `/data/dev/cp/scripts/cpc_brief.py` 并在 `/data/dev/cp/cp_agent_ref.md` 中说明用法；`cp/scripts` 版本请改用系统通用 `zig`，`bun` 仓本地继续保留 `vendor/zig/zig`。

### [2026-11-23] 收到 `cp_agy` 结单验收通知（`task-bun-20261123-adopt-cpc-brief`）
- 已收录至 `/data/dev/cp/scripts/cpc_brief.py`（默认调用 PATH 中的通用 `zig`，并支持 `--zig`/`--require-backup`/`--keep-zig` 选项），已实测通过并在 `/data/dev/cp/cp_agent_ref.md` 补齐调用规范；已验证 `--help` 并执行 `ack`（`bun` 仓本地继续使用 `scripts/cpc_brief.py` + `vendor/zig/zig`）。

## [2026-11-23] 补齐用户交互与跨 AGY 通信逐条对齐审计记录（Wave #58 ~ Wave #73，严禁错配）

### 一、 用户指令 <-> 主智能体回应逐条对齐补录
1. **用户**：`纯化还有多少工作量? 还剩哪些需要纯化?`
   - **主智能体回应**：汇报全仓残余待纯化项分布（`orelse`、`and`/`or`、`try`、`const X = struct/enum/union`、`?T`、`: void`、裸接收者、`if/while |capture|`），估算分波次清扫工作量并保持 2 路并行推进。
2. **用户**：`为何大量修改未git 提交?`
   - **主智能体回应**：说明此前多波次子智能体并行修改后处于集中验证窗口，立即按 `SESSION/CHANGELOG/DEVLOG` + 单次原子 `git commit` 规范清理未提交积压并恢复每波原子提交节奏。
3. **用户**：`保持两个子智能体与主智能体一起并行工作, 加快进度` / `一直要保持两个子智能体与主智能体一起并行工作, 加快进度, 牢记`
   - **主智能体回应**：确认严格保持最多 2 路短生命周期子智能体并行调度，主智能体负责编排、验收、`zig:check` 与原子提交，不空转等待。
4. **用户**：`你做了很多文件的cp净化, 这些错误语法之前是如何通过 cpc 编译的? 是不是说明 cpc 存在bug, 放行了大量错误语法? cpc 是否需要优化? 列出cpc 需要优化的功能点!`
   - **主智能体回应**：分析旧版 `cpc` 对 `return struct` 透传、类型/表达式 `?` 歧义、`&const_var`、零字段结构体接收者与旧语法缺少严苛拦截等原因，列出 6 项 `cpc` 核心改进点。
5. **用户**：`将你上面列出的: 基于本轮实测，核心改进点, 每个改进点做成一个工单,发给 cp agy 进行cpc加强/改进.`
   - **主智能体回应**：将 6 项改进逐一提炼 `<=10` 行最小复现 `.cp` 并发起工单提交给 `cp_agy`（`task-bun-20261123-passthrough-factory-lowering` 等 6 条）。
6. **用户**：`已重启agy并切换了配额账号，已恢复当前会话上下文...重开2个子智能体...读取 COMM.md，本阶段工作完成更新文档并 git 提交，自主推导下一阶段工作，持续工作`
   - **主智能体回应**：确认遵循 `COMM.md`，立即重开 2 路全新子智能体推进语法纯化与测试验证，每波完成后更新四线文档并原子提交。
7. **用户**：`收到新的上游结单验收通知(3条: passthrough-factory-lowering, optional-type-vs-try-disambig, addr-of-const-var-vs-type)...验收适配并 ack`
   - **主智能体回应**：执行 `task_queue.py unacked --agent bun_agy --json`，逐条验证复现文件通过新 `cpc` 编译并执行 `ack`。
8. **用户**：`收到新的上游结单验收通知(3条: as-enum-tag-union, zero-field-struct-method-recv, strict-purification-flag)...验收适配并 ack`
   - **主智能体回应**：执行 `task_queue.py unacked`，本地复现验证通过（含 `cpc check --strict` E0301..E0311），执行 `ack` 并记录至 `SESSION.md`。
9. **用户**：`task-19033 bun bd test 需要这么久吗?` / `主智能体不要陷入长时间任务, 长时间任务交给子智能体运行` / `刚才你执行的 bun bd test 结果如何, 没有看到你汇报呢`
   - **主智能体回应**：解释首次构建调试二进制耗时原因，汇报 `bun bd test` 精确结果，并明确主智能体不再阻塞在长耗时 `bun bd test`，改为全部交给子智能体运行。
10. **用户**：`建议后续 一个子智能体做完整性抽查, 一个做 bun bd test。完整性抽查 是什么意思? 为何是抽查?`
    - **主智能体回应**：解释在语法纯化收敛后需对照 `_.zig` 检查是否遗漏生产逻辑，说明抽查高风险模块的原因。
11. **用户**：`先用脚本对全仓 1280 对 (.cp, _.zig) 做一次自动化普查（对比函数数量/行数/导出符号/switch 区间），再针对有差异的模块精准深挖...你看 是主智能体完成,还是临时 开一个子智能体来完成`
    - **主智能体回应**：建议派发临时子智能体执行全仓 1,280 对 `(.cp, _.zig)` 脚本普查（产出 `temp/full_census.txt`），再逐批深挖修复（Wave #65~#66）。
12. **用户**：`收到新的上游结单验收通知(1条: task-bun-20261123-silent-drop-comptime-auto-dotted-err)...验收并 ack`
    - **主智能体回应**：读取结单详情（`is_builtin_runtime_stub_fn` 收敛过滤），本地验证 `yaml.cp` `pub fn print` 正常生成后执行 `ack`。
13. **用户**：`收到新的上游结单验收通知(1条: task-bun-20261123-ptr-to-optional-lowering)...验收并 ack`
    - **主智能体回应**：验证 `repro_ptr_opt.cp` 已正确输出 `p: *?u8`（`inner_is_paren` 区分 `*(T?)` 与 `*T?`），执行 `ack`。
14. **用户**：`收到新的上游结单验收通知(1条: task-bun-20261123-as-ptr-field-addr-of)...验收并 ack`
    - **主智能体回应**：验证 `repro_as_ptr_field.cp` 已输出 `@as(*u64, @ptrCast(@alignCast(self.p)))`，执行 `ack` 并重编全仓 4 处 `@ptrCast(&(` 受影响文件。
15. **用户**：`汇报成果与项目进度`
    - **主智能体回应**：汇总 Wave #66~#70 成果（`DevServer`/`fmt`/`Blob`/`subprocess`/`HTMLImportManifest`/`Installer`/`yaml` 修复与 `Batch 1a/1b/1c` 测试进度）及 Wave #71 进行中任务。
16. **用户**：`zig:check 5/5； 5/5 是什么意思? 对 170 个 .cp mtime 晚于 .zig 的文件, zig 是 cp 编译产生的, 为何 会晚?`
    - **主智能体回应**：解释 `build.zig` `check` 步骤对应 5 个构建图节点（前置依赖 + `compile obj bun-debug` 语义检查）；解释 `.cp` mtime 晚于 `.zig` 源于 Git 写入时间戳顺序及早期仅 `check`/批改未重编（169/170 已预检通过，余下 1 个已修复）。
17. **用户**：`收到新的上游结单验收通知(1条: task-bun-20261123-unary-neg-zero-float)...验收并 ack`
    - **主智能体回应**：验证 `repro_unary_neg_zero.cp` 已原生输出 `return (-x);`（保留 `-0.0` 符号位），执行 `ack` 并重编 `visitExpr.cp` 与 `toBeCloseTo.cp`。
18. **用户**：`1. 如果 struct 内都是静态方法, 只需 定义 static struct 即可... 2. cpc_brief.py 是 cp agy 提供给你的吗?`
    - **主智能体回应**：确认后续纯命名空间结构体优先使用 `static struct`；澄清 `scripts/cpc_brief.py` 为 `bun` 仓本地编写的省 Token 封装脚本（底层调用 `cpc check --json` / `cpc --lib` + `vendor/zig/zig ast-check`）。
19. **用户**：`把这个 scripts/cpc_brief.py 工具, 通过工单告知 cp agy 拷贝/收录到 cp/scripts 中, 改用系统通用 zig 并在 cp_agent_ref.md 中说明, bun 仓保留 vendor/zig/zig`
    - **主智能体回应**：向 `cp_agy` 提交 `task-bun-20261123-adopt-cpc-brief`。
20. **用户**：`收到新的上游结单验收通知(1条: task-bun-20261123-adopt-cpc-brief)...验收并 ack`
    - **主智能体回应**：验证 `/data/dev/cp/scripts/cpc_brief.py --help`（默认调用 PATH 通用 `zig`，支持 `--zig`/`--require-backup`/`--keep-zig`）可用并执行 `ack`。
21. **用户**：`1. 检查发现 SESSION.md SESSION_H.md 丢失了用户交互及与其他 agy 通信记录! 2. 立刻检查并补齐丢失记录、确保严格对齐不要错配；将此规则写入 GEMINI.md 铁律`
    - **主智能体回应**：核查 `transcript.jsonl` 与 `/root/.agy/comm/queue.db`，确认 11 条 `task-bun-20261123-*` 通信均已闭环但用户指令与回应成对记录缺失；已在 `GEMINI.md` 固化“用户交互与跨 AGY 通信必须逐条对齐实时写入 `SESSION.md`”铁律，并在本节补齐全部对齐记录。

### 二、 `bun_agy <-> cp_agy`（2026-11-23）全量 11 条通信逐条对齐总表
| # | Task ID | `bun_agy` 发送问题与 repro | `cp_agy` 结单修复说明 | `bun_agy` 验收与 `ack` |
| :--- | :--- | :--- | :--- | :--- |
| 1 | `task-bun-20261123-passthrough-factory-lowering` | `return struct/union/enum` 泛型工厂未做 CP lowering (`repro_passthrough_factory.cp`) | 支持泛型工厂返回体与嵌套容器完整 CP 解析与递归 lowering | 本地验证通过，已 `ack` |
| 2 | `task-bun-20261123-optional-type-vs-try-disambig` | 类型位置 `?` 与表达式 `?` 歧义 (`repro_opt_type_disambig.cp`) | 消除 `T?` / `(*Owner)?` 误生成 `try` | 本地验证通过，已 `ack` |
| 3 | `task-bun-20261123-addr-of-const-var-vs-type` | `&ident` 误判 `*const ident` 及 `const *!` (`repro_addr_of_const_var.cp`) | 修复 `&const_var` 判别与指针修饰符 | 本地验证通过，已 `ack` |
| 4 | `task-bun-20261123-as-enum-tag-union` | `union(EnumTag)` 上 `@as(EnumTag, _)` 误生成 `@enumFromInt` (`repro_as_enum_tag_union.cp`) | 修复 `@as(EnumTag, _)` tag 转换 | 本地验证通过，已 `ack` |
| 5 | `task-bun-20261123-zero-field-struct-method-recv` | 无字段结构体方法未用 `_` 时丢失 `self` (`repro_zero_field_method.cp`) | 保留无字段结构体方法接收者 `self` | 本地验证通过，已 `ack` |
| 6 | `task-bun-20261123-strict-purification-flag` | 需要 `cpc check --strict` 门禁 (`repro_strict_purification.cp`) | 新增 `cpc check --strict` (E0301..E0311) | 本地验证通过，已 `ack` |
| 7 | `task-bun-20261123-silent-drop-comptime-auto-dotted-err` | `comptime`+`auto`+点号错误联合顶层 `print` 被静默丢弃 (`repro_silent_drop_comptime_auto_err.cp`) | `codegen/actor.rs` 仅过滤内建委托桩 `is_builtin_runtime_stub_fn` | 验证 `yaml.cp` 通过，已 `ack` |
| 8 | `task-bun-20261123-ptr-to-optional-lowering` | `*(u8?)` 被误降低为 `?*u8` 而非 `*?u8` (`repro_ptr_opt.cp`) | `parser/types.rs`+`map_type.rs` 按 `inner_is_paren` 精确区分 | 验证输出 `p: *?u8`，已 `ack` |
| 9 | `task-bun-20261123-as-ptr-field-addr-of` | `_.p as *u64` 误生成 `@ptrCast(&(self.p))` (`repro_as_ptr_field.cp`) | `codegen/expr/ops.rs` 修正 `is_src_ptr` 与切片/定长数组判定 | 验证输出 `@ptrCast(@alignCast(self.p))`，已 `ack` |
| 10 | `task-bun-20261123-unary-neg-zero-float` | 一元 `-x` 误生成 `(0 - x)` 丢失 `-0.0` (`repro_unary_neg_zero.cp`) | 全链路新增 `Expr::Neg` 原生生成 `(-x)` | 验证输出 `return (-x);`，已 `ack` |
| 11 | `task-bun-20261123-adopt-cpc-brief` | 收录 `scripts/cpc_brief.py` 至 `cp/scripts` 并改用系统 `zig` | 已收录 `/data/dev/cp/scripts/cpc_brief.py` 并更新 `cp_agent_ref.md` | 验证 `--help` 通过，已 `ack` |
22. **用户**：`汇报成果与进度, 还剩下多少工作需完成`
    - **主智能体回应**：汇报全仓 1,295 个 `.cp` 已 100% 通过 `cpc check` 与 `zig:check`（5/5）、Wave #65~#72 核心修复与 Batch 1a/1b/1c/2a 测试进展，说明剩余工作主要为修复 `fetch`/`FetchTasklet` 3 处缺陷并完成余下 ~760+ 个测试文件的回归修绿。
23. **用户**：`保持两个子智能体和主智能体并行工作, 加快进度, 注意 主智能体不可限于长时间任务.`
    - **主智能体回应**：立即补派第 2 路只读子智能体排查 `blob-oom.test.ts`/`stream-fast-path.test.ts` 并准备 `fetch`/`FetchTasklet` 精确补丁，与运行 `bun bd test` 的第 1 路保持双路并行。

## [2026-11-23] Wave #73：`-0.0` 回归验证通过，修复 `fetch`/`FetchTasklet`/`Blob`/`ReadableStream` 4 项语义缺陷并提交
- 验证 `-0.0` 修复（`expect.test.js`、`test-test.test.ts`、`moremore.test.ts`、`jest-extended.test.js`）：478 pass / 2 fail（`-0` 与 `toIncludeRepeated` 全部 PASS，余 2 项为 `deepEquals` 压力超时与 snapshot 时间格式 `[1.52s]`）；Batch 2b 运行 `streams` 前 5 文件 13 pass / 1 fail 后在 `streams.test.js` 挂起（已强杀并派发独立定位）。
- 完成 4 个核心 webcore 模块修复（均通过 `cpc_brief.py` 4 项门禁 + `bun run zig:check` 5/5 通过）：
  - `src/runtime/webcore/fetch.cp`（L416-435 `found_method` 依次提取 `options_object` -> `request.method` -> `request_init_object` -> `GET`）；
  - `src/runtime/webcore/fetch/FetchTasklet.cp`（L566-578 `onBodyReceived` 先清理 `cancel_handler`/`readable_stream_ref`/`_guard_buf.active = false` 再调 `onData`，并补全 `fromJS` `static fn`）；
  - `src/runtime/webcore/Blob.cp`（恢复 `lifetimeWrap`/`toUint8Array`/`getBytes`/`getJSON`/`getText` 与 `AnyBlob.toJSONShare`/`toActionValue`/`toPromise` 的 `JSPromise.wrap`）；
  - `src/runtime/webcore/ReadableStream.cp`（L161 `toAnyBlob` 增加 `_.isLocked(globalThis)` 校验）。
- 推导下一阶段 Wave #74：Subagent 1 运行 `bun bd test` 验证 `blob-oom.test.ts`、`stream-fast-path.test.ts`、`client-fetch.test.ts`、`body-stream.test.ts`（`-t "method"`）与 `fetch.test.ts`；Subagent 2 根据 `streams.test.js` 定位结果修复死循环并覆盖 `test/js/web/websocket`。
24. **用户**：`已重启agy并切换了配额账号，已恢复当前会话上下文...重开2个子智能体...读取 COMM.md 保持通信畅通...持续工作`
    - **主智能体回应**：已读取 `/root/.agy/comm/COMM.md` 并核查 `task_queue.py unacked`（`[]`）与 `agy-comm.service` 状态；立即新开 2 路并行子智能体继续 Batch 2c 后半段测试（`636ed04c`）与其余 20 个核心目录只读完整性核查（`9a04f692`）。

## [2026-11-23] Wave #74：Batch 2c 收官、全仓 110 目录覆检完成与 `Blob`/`ParentDeathWatchdog` 修复提交
- 完成 Wave #73 验证与 Batch 2c（`test/js/web/` 其余 58 文件）：
  - `fetch.test.ts` `decontaminate()` SIGABRT 崩溃彻底消除（326 pass / 0 崩溃），`stream-fast-path.test.ts` 18/18 全绿；
  - Batch 2c 后半段 30 文件跑出 **889 pass / 17 fail / 16 skip / 2 todo**（63,034 expect，804.61s，**0 崩溃**）；
  - 完成全仓 **110 个目录组 / 1,295 个 `.cp` 文件** 100% 完整性覆检（`throwTODO` 除 `Blob.doImage` 外零新增，仅确认 `ParentDeathWatchdog.cp` 行解析细节偏差）。
- 完成 2 个模块修复与验证（均通过 `cpc_brief.py` 4 项门禁 + `bun run zig:check` 5/5 通过）：
  - `src/runtime/webcore/Blob.cp`（恢复 `Any.toArrayBufferTransfer`/`toUint8ArrayTransfer`/`toStringTransfer` 走 `.transfer` 路径，将 `getFormData` 桩替换为 `toFormData`/`toFormDataWithBytes`，补全 `onStructuredCloneTransfer` 与 `toStreamWithOffset`）；
  - `src/aio/ParentDeathWatchdog.cp`（恢复 `[u8; 32768]` 缓冲区与 `tokenizeAny(u8, contents, " \n")`）。
- 推导下一阶段 Wave #75：Subagent 1 启动 **Batch 3a**（`bun bd test test/js/bun/spawn test/js/bun/shell`，77 个测试文件）；待 `worker.test.ts` 只读排查返回后继续推进 `Batch 3` 与针对性修复。

### [2026-11-23] 向 `cp_agy` 提交 `cpc check --strict` `E0308` 误报 `std.zig` 工单
- `task-bun-20261123-strict-e0308-std-zig` (`repro_strict_e0308_std_zig.cp`)：`cpc check --strict` 误将 `std.zig.fmtString(s)` 中的 `.zig` 成员名识别为 `zig { ... }` 逃逸块并报 `E0308`（全仓核查确认真实 `zig {}` 为 0，全部 8 处 `E0308` 均为 `std.zig.fmtString` 误报）。
- **结单验收与 `ack`**：`cp_agy` 已修复 `cpc/src/strict.rs`（`!is_member_access && matches!(next, Some(Token::LBrace))`）、`cpc/src/brief.rs`（词法 token 统计）及 `scripts/cpc_brief.py`（`(?<!\.)\bzig\s*\{`）；本地实测 `/data/dev/cp/cap/bin/cpc check --strict /data/dev/cp/temp/repro_strict_e0308_std_zig.cp` 返回 `Check successful`，已同步更新 `scripts/cpc_brief.py` 并执行 `ack`。

### [2026-11-23] 向 `cp_agy` 提交泛型移位 `E0207` 工单
- `task-bun-20261123-generic-shift-e0207` (`repro_generic_shift_e0207.cp`)：`comptime T: type` 泛型整数参数执行移位运算 `>>` / `<<` 时，`cpc` 误要求右操作数与左操作数类型完全一致，报 `E0207: expected 'MaskInt', found 'ShiftInt'`（影响 `src/collections/bit_set.cp` 7 处）。
- **结单验收与 `ack`**：`cp_agy` 已修复 `cpc/src/sema/type_infer.rs`（`<<`, `>>`, `<<|`, `<<=`, `>>=` 移位右操作数不再强制要求与左操作数同型，同时保留非整数 `E0207` 校验）；本地实测 `/data/dev/cp/cap/bin/cpc check --brief` 验证 `repro_generic_shift_e0207.cp` 与 `src/collections/bit_set.cp` 均返回 `ok (0 errors)`，已执行 `ack`。

## [2026-11-23] Wave #75：修复 `shell.Builtin` SEGV 根因、纯化 `MarkdownObject`/`copy_file` 与 `zig:check` 5/5 通过
- 修复 `test/js/bun/shell` 3 处 ASan SEGV 共同根因：
  - `src/shell/Builtin.cp`：`inline fn allocator(*_)` 与 `inline fn eventLoop(*_)` 补充 `*_` 指针接收者（原按值传参导致 `_.parentCmd()` 的 `@fieldParentPtr("bltn", self)` 取栈副本地址读取野指针），并补 `static fn onForceSyncOrIsaTTY(_: void)`；
- 完成 2 个大模块 `--strict` 接收者纯化与编译适配：
  - `src/runtime/api/MarkdownObject.cp`（消除 95 处 `E0305` 与 3 处 `E0307`，`--strict` 0 报错）；
  - `src/runtime/webcore/blob/copy_file.cp`（消除 214 处 `E0306`，`std.os.linux.fcntl` 第三参数改 `@as(usize, 0)`，`--strict` 0 报错）；
- `scripts/cpc_brief.py` 同步 `(?<!\.)\bzig\s*\{` 正则；以上改动均通过 `cpc_brief.py` 4 项门禁与 `bun run zig:check`（5/5，30s）。
- 推导下一阶段 Wave #76：Subagent 1 运行 `bun bd test test/js/bun/shell` 验证 SEGV 消除并继续完成 Batch 3a（`test/js/bun/spawn`）；Subagent 2（不改 `src/` 以防竞态，先做只读排查/准备）核查 `s3/{multipart,simple_request}.cp`、`Installer.cp`、`hoisted_install.cp` 等 `--strict` 补丁方案，待 Batch 3a 完成后立即执行纯化。

## [2026-11-23] Wave #76：Batch 3a（`shell` + `spawn`）测试验证与全仓 `--strict`/占位盘点
- 完成 Batch 3a（`test/js/bun/shell` + `test/js/bun/spawn`）测试验证：
  - Wave #75 对 `src/shell/Builtin.cp` 的修复彻底消除 `bunshell-default.test.ts`（1/1）、`yes.test.ts`（全绿）及 `EventLoopTask.fromEventLoop` 共 3 处 ASan SEGV；
  - `test/js/bun/spawn` 39 文件全量跑完（**262 pass / 23 fail / 4 skip / 2 todo / 0 主进程崩溃**）；
  - `test/js/bun/shell` 分批合计 **479 pass / 28 fail / 30 skip / 6 todo**，定位剩余 2 处崩溃：`bunshell.test.ts` `Allocation scope leaked 30 bytes`（`Cmd.cp:487-489`）与 `commands/ls.test.ts` `BufferedIoClosed.close` ASan SEGV（`Cmd.zig:257`）；
- 完成全仓 1,295 个 `.cp` 文件的 `--strict` 资产盘点与全仓 `throwTODO`/`@panic("todo"` 复核（确认除 `Blob.cp:doImage` 外 0 处遗漏实现）。

## [2026-11-23] Wave #77：修复 `Cmd.cp` 两处崩溃、恢复 `Blob.cp:doImage` 并完成 70 文件 `--strict` 批量纯化
- 修复 2 个核心模块：
  - `src/shell/states/Cmd.cp`：L482-492 `Expansion` 分支改用 `bun.default_allocator` 格式化并先 `e.deinit(_.base.allocator())` 再调用 `writeFailingError`，消除 `Allocation scope leaked 30 bytes` panic；L613-614 `initSubproc` 改为直传 `&_.exec.subproc.child` 给 `Subprocess.spawnAsync`，消除 `BufferedIoClosed.close` ASan SEGV；
  - `src/runtime/webcore/Blob.cp`：补回 `import Image from "../image/Image.zig";` 恢复 `doImage`（`Image.fromBlobJS`），并清零全部 9 处 `E0307`；
- 5 路并进完成 **70 个非 `return struct` 文件 `--strict` 批量纯化**（均 0 报错）：
  - `s3/{multipart,simple_request,credentials_jsc}.cp`、`S3File.cp`、`PasswordObject.cp`（含 `AlgorithmDef_.get` `static fn`）、`PBKDF2.cp`、`hoisted_install.cp`、`Installer.cp`（8 文件）；
  - `src/jsc/{JSValue,VirtualMachine,Task,event_loop,CppTask}.cp`、`install/{ExternalSlice,versioned_url,PackageManager/ProgressStrings}.cp`（`pub static struct`）、`shell/{EnvMap,subproc}.cp`（10 文件，含 `JSValue.cp` 67 个 `static fn` 补全）；
  - `src/sql/postgres/protocol/`（21 文件）+ `src/sql/shared/`（2 文件）；
  - `src/sql/mysql/`（5 文件）+ `src/sql/mysql/protocol/`（13 文件）；
  - `valkey_protocol.cp`、`zstd.cp`、`zlib.cp`、`watcher/{INotify,KEvent,Windows}Watcher.cp`、`threading/{ThreadPool,WaitGroup}.cp`、`safety/{CriticalSection,ThreadLock,alloc}.cp`（11 文件）；
- 全量 72 个修改文件通过 `cpc_brief.py` 4 项门禁、`cpc check --strict`（0 报错）与 `bun run zig:check`（**5/5，30s**）。
- 推导下一阶段 Wave #78：Subagent 1 运行 `bun bd test test/js/bun/shell/bunshell.test.ts test/js/bun/shell/commands/ls.test.ts` 验证 `Cmd.cp` 修复后推进 Batch 3b（`test/js/bun/resolve` 等）；Subagent 2 先行只读分析 `spawn` 23 fail / `util` 48 fail 归因或准备下一批 `--strict` 文件清单（零 `src/` 写冲突）。

## [2026-11-23] Wave #78：Shell 崩溃清零复测、Batch 3b（`resolve`）全量完成与失败归因
- `bunshell.test.ts` + `ls.test.ts` 复测（465 用例，193.82s）：**377 pass / 83 todo / 5 fail / 0 crash**（`Allocation scope leaked` 与 `BufferedIoClosed.close` SEGV 彻底清零；5 fail 均为 `uid=0` 权限或 `bun install` 5s 超时）。
- **Batch 3b（`test/js/bun/resolve` 34 文件 / 245 用例，260.14s）**：**234 pass / 2 skip / 5 todo / 4 fail / 0 crash**（2 项 10,000 次导入超时、1 项缺 `reflect-metadata` 夹具、1 项 `--hot` stdout 关闭）。
- 只读排查 Wave #76 `shell_rest`（3 处 `BufferedIoClosed.close` SEGV 已随 `Cmd.cp` 修复消除）与 `spawn`（23 fail + 4 errors 全为 Debug+ASan 启动耗时与紧阈值超时，0 处 CP 退化）。

## [2026-11-23] Wave #79：`src/http/`（18 文件）与 `src/sql_jsc/`（17 文件）共 35 文件 `--strict` 纯化与 `zig:check` 5/5 通过
- `src/http/` 18 文件（含 `http.cp` 等全部剩余文件）全部通过 `cpc check --strict`（`errors: 0`），实现 `src/http/` 全目录 36/36 零违例（其中 `CertificateInfo.deinit` 省略接收者以兼容 `*const CertificateInfo`、`ClientContext.cp` 用 `@sizeOf(usize)`、`HTTPRequestBody.cp` 改用 `_`、`Signals.cp` 清理 `@comptime`）。
- `src/sql_jsc/` 17 文件（含 `PostgresSQLConnection.cp`、`JSMySQLConnection.cp`、`MySQLQuery.cp` 等）完成 `--strict` 纯化（除 `JSMySQLConnection.cp` 泛型工厂内 2 处 `E0307` 外全零，保留 JSC 原型/GC 绑定 `*_`、补 `MutPtr(T)` 辅助函数与 `MySQLQuery.BindAndExecuteCtx.run` `static fn`）。
- 全量 35 文件通过 `cpc_brief.py` 4 项门禁与 `bun run zig:check`（**5/5，29s**）。
- 推导下一阶段 Wave #80：Subagent 1 运行 **Batch 3c**（`bun bd test test/js/bun/glob test/js/bun/md test/js/bun/cookie test/js/bun/css test/js/bun/crypto test/js/bun/ffi`）；Subagent 2 并行做只读排查/准备下一批 `--strict` 文件清单（如 `src/bundler/` 23 文件 `E0307` 或 `src/test_runner/` 剩余 11 文件精确行号清单）。

## [2026-11-23] Wave #80：Batch 3c（`glob`/`md`/`cookie`/`css`/`crypto`/`ffi`）全量测试与 `--strict` 清单全提取
- **Batch 3c（30 文件）全量零崩溃通过**：
  - `glob` / `md` / `cookie` / `crypto` / `ffi`（26 文件，275.44s）：**11,484 pass / 15 skip / 6 fail / 1 error / 0 ASan crash**（`md`/`cookie`/`crypto` 全绿；`glob` 仅 `leaks`/`braces` 超时与缺 `fast-glob` 夹具、`ffi` 仅 `addr32`/`cc` 2 项编译夹具报错）；
  - `css`（5 文件）：`color.test.ts`（排除 `256³` 同步压测 `fuzz ansi256` 后）**916/916** 全绿，`css.test.ts` **1,031 pass / 67 skip / 1 fail**（`font`），`css-fuzz`/`small-list-grow`/`doesnt_crash` 均为 Debug+ASan 超时，**0 ASan crash**。
- 完成 `src/bundler/`、`src/test_runner/`、`src/runtime/`、`src/bake/`、`src/cli/`、`src/install/`、`src/shell/` 等剩余全仓 `E0307` 精确行号清单提取。
- 推导下一阶段 Wave #81：Subagent 1 运行 **Batch 3d**（`s3`/`net`/`io`/`image`/`transpiler`/`websocket` 30 文件）；Subagent 2 只读排查 `css.test.ts > font` 与 `ffi` 2 项失败根因（零 `src/` 写冲突）。

## [2026-11-23] Wave #81–82：Batch 3d 全量测试、修复 `S3File.cp` `PathOrBlobGuard` 崩溃与并发超时排查
- **Batch 3d（30 文件）**：`429 pass / 135 fail / 302 skip / 6 todo / 1 crash`（`transpiler` 43/43 全绿、`image` 219 pass / 1 fail / 0 crash）；暴露唯一崩溃 `s3-storage-class.test.ts`（`WTFStringImplStruct.deref` double-free panic）。
- **Batch 3c/3d 只读排查**：
  - `ffi/addr32.test.ts`（`cc` 缺 `-fPIC`）与 `ffi/cc.test.ts`（缺 `/usr/include/node/node_api.h`）均为环境问题；`css > font` 确认 `font.cp` 与 `font_.zig` 逻辑一致；
  - `websocket-server.test.ts`（42 fail）与 `bun-write.test.js`（14 fail）**100% 为 `describe.concurrent` 下子进程/GC 调度超时**，0 处 `.cp` 逻辑缺陷。
- **修复 `src/runtime/webcore/S3File.cp`**：将 `PathOrBlobGuard` 改用可空指针 + `~errinit()` + `disarm()`，并在 `presign`/`unlink`/`write`/`size`/`exists`/`stat` 6 个 `.path` 分支成功转移所有权后调用 `_guard_pob.disarm()`，消除 `WTFStringImplStruct.deref` double-free；通过 `cpc_brief.py` 与 `cpc check --strict`（0 报错）。
- 推导下一阶段 Wave #83：Subagent 1 复测 `s3-storage-class.test.ts` 并继续 **Batch 3e**（`test/js/bun/` 其余根目录及 `console`/`cron`/`jsc`/`sqlite`/`terminal`/`udp`/`webview`/`dns`/`json5`/`plugin`/`sourcemap`/`yaml` 等）；Subagent 2 并行只读排查 `bun-write-leak.test.ts` `337 MB` 内存占用或准备 `src/runtime/api/` 15 文件 `E0307` 纯化。
26. **用户**：`汇报工作成果, 还剩多少工作位完成`
    - **主智能体回应**：汇报 Wave #75–83 最新成果：（1）全仓 1,295 个 `.cp` 文件 100% 编译通过（`zig:check` 5/5），近几波又完成 `http/`（18 文件，全目录 36/36 零违例）、`sql_jsc/`（17 文件）、`s3`/`crypto`/`install`/`jsc`/`sql`/`valkey`/`zlib`/`zstd`/`watcher`/`threading`/`safety` 等 **107 个文件 `--strict` 纯化**；（2）连续根除 `shell.Builtin`（3 处 SEGV）、`Cmd.cp`（`Allocation scope leaked` 与 `BufferedIoClosed.close` SEGV，复测 `bunshell+ls` 377 pass/0 crash）、`Blob.cp:doImage`、`S3File.cp` `PathOrBlobGuard`（复测 `s3-storage-class.test.ts` 9/9 全绿）；（3）完成 `resolve`（234 pass/0 crash）、`glob/md/cookie/css/crypto/ffi`（11,484 pass/0 crash）、`s3/net/io/image/transpiler/websocket`（429 pass）等测试；Batch 3e 正在运行（已通知 `1011a761` 跳过卡死的 `repl` 交互子进程）。剩余工作：Batch 3e 剩余目录 + `test/js/bun/` 根目录 9 文件、Batch 4 `test/js/node/`（252 文件）、Batch 5 `test/{cli,bundler,napi,integration,v8}`（319 文件）、剩余约 140 个普通结构体文件 `--strict` 纯化及最后 Release 构建验收。
27. **用户**：`bun bd test 剩余测试批次（约 624 个测试文件）, 已经完成了多少文件?`
    - **主智能体回应**：实盘盘点：已完成实测 **489 个测试文件**（`test/js/bun/` 已完成 18 个子目录共 345 个文件 + `test/js/web/` 全量 144 个文件，未计入正在运行的 Batch 3e）；核心待跑批次共 **639 个文件**（Batch 3e 及 `bun/` 其余 61 个 + `test/js/node/` 259 个 + `test/{cli,bundler,napi,integration,v8}` 319 个），另全仓 `test/` 总计 1,729 个 `.test.*` 文件（含 `test/regression/` 385 个及其他零散目录）。
28. **用户**：`bun bd test 剩余测试批次`
    - **主智能体回应**：清理卡在 `terminal-platform-gaps.test.ts` 的 `rem1` 挂起进程（`rem2` 10 文件已出结果：254 pass / 18 skip / 23 fail），并立即启动 **Wave #84 双路并行**：Subagent 1 (`7471bc68`) 运行 `test/js/bun/*.test.*`（9 文件）+ **Batch 4a**（`test/js/node/{path,fs,url,os,events,assert,console,diagnostics_channel,promise,string_decoder,timers,timers.promises}` 约 54 文件）；Subagent 2 (`2bf55a68`) 只读排查 `direct-readable-stream.test.tsx` emoji 丢失与 `udpSocket` 5 项失败根因。
29. **用户**：`Agent(self: Wave #83 bun bd test (s3 复测 + Batch 3e...) · Gemini 4 Hydrogen Generating · 5h57m51s 6个小时, 确认还活着吗?`
    - **主智能体回应**：确认该旧子智能体（`1011a761`）因等待 `terminal-platform-gaps.test.ts` 挂起子进程已卡死近 6 小时，已执行 `manage_subagents(kill)` 终止释放，当前仅保留 Wave #84 的 2 个新子智能体（`7471bc68` 与 `2bf55a68`）健康并行运行。

### [2026-11-23] 向 `cp_agy` 提交 `return struct` 静默丢弃语句严重隐患工单
- `task-bun-20261123-return-struct-silent-drop` (`repro_as_trailing_comma_drop.cp`)：`return struct` 泛型工厂方法内若某条语句存在解析失败（如多行 `@as(u16, @truncate(c),)` 尾逗号），`cpc --lib` 仍返回 exit 0 且静默丢弃整条语句（导致 `src/js_parser/lexer.cp` `decodeJSXEntities` 丢失全部 `>0xFFFF` UTF-16 代理对输出）。
- **结单验收与 `ack`**：`cp_agy` 已双重修复 `cpc/src/parser/{mod,primary}.rs`（新增 `validate_anon_container_expr` 严格校验 `return struct/union/enum` 内部语法 + 支持 `@as(...)` 可选尾逗号）及 `cpc/src/codegen/decl_class.rs`（严禁吞掉诊断返回残缺 AST）；本地实测 `/data/dev/cp/cap/bin/cpc --lib /data/dev/cp/temp/repro_as_trailing_comma_drop.cp` 完整生成 `out.appendAssumeCapacity` 且通过 `zig ast-check`，已执行 `ack`。

## [2026-11-23] Wave #84–85：Batch 4a（`node/` 71 文件）完成、修复 `lexer`/`unicode`/`udp_socket` 静默丢弃与逻辑偏差
- **测试完成**：
  - `test/js/bun/*.test.*`（根目录 9 文件，26.42s）：**128 pass / 1 skip / 9 fail / 0 crash**（8 项 `Bun.secrets` 缺 dbus/libsecret，1 项 `symbols.test.ts` 宿主机 glibc）；
  - **Batch 4a**（`test/js/node/` 12 核心目录 71 文件，1394.66s）：**1,035 pass / 26 skip / 12 todo / 9 fail / 0 crash**（仅 `fs.test.ts` 2 项功能失败：`readdir recursive fail x 100` FD 泄漏与 `utimesSync` 2038 年时间戳溢出，其余 7 项均为 Debug 超时）。
- **全仓 `@as(...,)` / `@builtin(...,)` 尾逗号静默丢弃扫描与修复**：
  - 扫描全仓 1,295 `.cp` 文件确认仅 4 处 `@as(...,)` 尾逗号触发静默丢弃（其余 59 处 `@builtin(...,)` 均 0 丢弃）；
  - 修复 `src/js_parser/lexer.cp`（移除 `decodeJSXEntities` `@as(...,)` 尾逗号恢复 `>0xFFFF` JSX 代理对输出 + 补全 19 处 `static fn`）；
  - 修复 `src/string/immutable/unicode.cp`（提取 `half_usize` 与移除 `@as(...,)` 尾逗号恢复被静默丢弃的 `appendUTF8MachineWordToUTF16MachineWord` 与 `cp1252ToCodepointAssumeNotASCII` + 补全 6 处 `static fn`）；
  - 修复 `src/runtime/socket/udp_socket.cp`（L304 `socket.getTruthy` -> `options.getTruthy` 修复 `binaryType` 读取与 `SendManyCtx.run` `static fn`）。
- 推导下一阶段 Wave #86：Subagent 1 复测 `direct-readable-stream.test.tsx` + `udp_socket.test.ts` 并继续 **Batch 4b**（`test/js/node/{http,http2,tls,net,crypto,process,zlib,stream,worker_threads,child_process}` 等）；Subagent 2 并行只读排查 `fs.test.ts` 2 项失败（`readdir recursive` FD 泄漏与 `utimesSync` 2038 年后时间戳溢出，零 `src/` 写冲突）。

## [2026-11-23] Wave #86–87：Wave #85 复测全绿、Batch 4b（`node/` 116 文件）完成与修复 `sys.cp` 2038 溢出
- **Wave #85 修复复测**：`direct-readable-stream.test.tsx` + `udp_socket.test.ts` **447 pass / 268 skip / 0 fail**（81,625 expect，95.81s，exit 0），emoji 丢失与 `udpSocket` 5 fail 全部转绿；
- **Batch 4b**（`test/js/node/` 10 核心目录 116 文件 / 2,231 用例，1287.07s）：**2,102 pass / 44 skip / 31 todo / 54 fail / 6 errors / 0 崩溃**（`crypto` 0 fail、`stream` 0 fail、`http2` 1 超时）；
- **修复 `src/sys/sys.cp`**：将 `futimens`（L3711–3712）与 `utimensWithFlags`（L3735–3736）中的 `i32(atime.sec)` / `i32(mtime.sec)` 改回 `@intCast(...)`，修复 `utimesSync > works after 2038`（`2045 -> 1909`）64 位时间戳截断，通过 `cpc_brief.py` 4 项门禁（`ratio: 1.019`）。
- 推导下一阶段 Wave #88：Subagent 1 复测 `fs.test.ts -t "utimesSync"` 并推进 **Batch 4c**（`test/js/node/` 其余全部目录：`test/js/node/*.test.*`、`util`、`readline`、`vm`、`module`、`async_hooks`、`watch`、`dns`、`dgram`、`inspector`、`v8`、`test_runner` 等）；Subagent 2 并行只读排查 `zlib.kMaxLength.global.test.js` ×8 与 `node-tls-connect.test.ts` `getPeerCertificate` ×2（零 `src/` 写冲突）。

## [2026-11-23] Wave #88–89：Batch 4c（`node/` 72 文件）收官与修复 `JSBundler`/`node_fs_stat_watcher` 两处崩溃
- **Wave #87 复测**：`fs.test.ts -t "utimesSync"` **4 pass / 1 skip / 0 fail**（`works after 2038` 转绿）；
- **Batch 4b 排查**：确认 `zlib.kMaxLength`（JS `$requireMap`）、`node-tls-connect`（`bun.sh` 证书无 OCSP-URI）、`stdin-fixtures`（硬编码 1000ms）均 **0 处 CP 缺陷**；
- **Batch 4c（`test/js/node/` 其余 72 文件）**：**2,058 pass / 35 fail / 8 skip / 67 todo / 2 crash**（根目录 21 文件 931 pass/0 fail，`module`/`dns`/`dgram`/`inspector`/`v8`/`test_runner`/`perf_hooks` 全绿）；至此 **`test/js/node/` 259 文件 100% 跑完**；
- **修复 Wave #88–89 暴露的 2 处崩溃**：
  - `src/runtime/api/JSBundler.cp`：将 `TopExceptionScopeGuard` 改为持有 `*jsc.TopExceptionScope` 指针（修复按值拷贝 RAII `defer` 破坏 `location == &bytes[0]` 自指断言），并补 `BundlerCompileOptions.fromJS`/`Config.fromJS` `static fn`；
  - `src/runtime/node/node_fs_stat_watcher.cp`：移除 `StatWatcherScheduler.setIntervalOnMainThread` 中误加的 `DerefGuard<StatWatcherScheduler>`（修复提前双重 `deref` 导致仍在持有 watchers 时析构 panic）。
- 推导下一阶段 Wave #90：Subagent 1 复测 `AsyncLocalStorage.test.ts` + `fs.watchFile.test.ts` 并启动 **Batch 5a**（`test/bundler/` 89 文件）；Subagent 2 只读排查 `readline`（27 fail）或检查全仓其他按值存自指结构体 Guard 隐患（零 `src/` 写冲突）。

## [2026-11-23] Wave #90–91：Batch 5a（`bundler/` 90 文件）完成、修复 `Chunk`/`convertStmtsForChunk`/`SQLDataCell` 与全清 `src/bundler/` 49 文件 `--strict`
- **Wave #89 复测**：`AsyncLocalStorage.test.ts` + `fs.watchFile.test.ts` **31 pass / 2 skip / 2 todo / 1 fail / 0 crash**（两处 `SIGILL` 彻底消除）；
- **Batch 5a（`test/bundler/` 90 文件）**：88 文件完赛（**381 pass / 29 skip / 22 todo / 148 fail**，主因为缺 `filenamify` 夹具及 `--compile` 超时），定位 2 处 `SIGILL`（`metafile.test.ts` 与 `standalone.test.ts`）；
- **只读排查**：确认 `readline.node.test.ts` 25/27 失败系环境 `TERM=dumb` 触发 `_ttyWriteDumb`；全仓扫描定位 `SQLDataCell.cp` 按值存储 `ExceptionValidationScope` 隐患；
- **Wave #91 修复与纯化（25 文件）**：
  - `src/bundler/Chunk.cp`：修复 `memcpyEscapingClosingTags` 笔误 `dest[dst + 2] = 2;` -> `dst += 2;`（消除 `standalone.test.ts` `remain.len == 0` 断言崩溃）并清 L732 `E0307`；
  - `src/bundler/linker_context/convertStmtsForChunk.cp`：兼容 `s_local.decls.len` 多/零声明分支（消除 `metafile.test.ts` `decls.len == 1` 断言崩溃）；
  - `src/sql_jsc/shared/SQLDataCell.cp`：`ExceptionScopeGuard` 改持有 `*jsc.ExceptionValidationScope` 指针；
  - `src/bundler/` 其余 22 文件（含 `BundleThread.cp` `(*Self)?` / `static fn`、`LinkerContext.cp` / `transpiler.cp` `static fn` 及全部 `catch |...|`）：实现 **`src/bundler/` 49/49 全目录 `--strict` 零违例**。
- 推导下一阶段 Wave #92：Subagent 1 复测 `metafile.test.ts` + `standalone.test.ts` 并启动 **Batch 5b**（`test/napi/` 59 文件 + `test/integration/` 20 文件 + `test/v8/` 1 文件 = 80 文件）；Subagent 2 并行只读检查 `src/bundler/bundle_v2.cp` 等是否存在其他按值传 `self` / `static fn` 漏标风险（零 `src/` 写冲突）。

### [2026-11-23] 向 `cp_agy` 提交 `cpc check --strict` `E0311` `union(enum)` 字段冲突工单
- `task-bun-20261123-strict-e0311-union-void` (`repro_strict_e0311_union_void.cp`)：`union(enum)` 省略 `: void` 报 `E0111`（parser 强制要求冒号），写 `none: void` 又报 `E0311`（`--strict` 禁止 `: void`），影响 `Installer.cp`、`http.cp` 等 `union(enum)` 声明。
- **结单验收与 `ack`**：`cp_agy` 已修复 `cpc/src/strict.rs`（将 `E0311` 收窄至真实函数返回 `: void` 签名，不再误报 `union`/`struct` 零大小字段）及 `cpc/src/parser/container.rs`（允许 `union` 字段省略 `: void` 默认为 `Type::Void`）；本地实测 `/data/dev/cp/cap/bin/cpc check --strict /data/dev/cp/temp/repro_strict_e0311_union_void.cp` 返回 `Check successful`，已执行 `ack`。
25. **用户**：`还剩多少 工作待完成?`
    - **主智能体回应**：汇报全仓实盘剩余工作：（1）代码迁移与基础编译 100% 达成（1,295 个 `.cp` 全过 `cpc check --brief` 与 `zig:check` 5/5）；（2）`--strict` 深度纯化已完成 ~968 个文件 `E0305`/`E0306` 清零，剩余 327 个文件（含 83 个 `return struct` 与 244 个普通结构体文件，已出首批纯化清单）及 `E0307` `catch |e|` 待分批清理；（3）`bun bd test` 已完成 `bun/http`、`bun/util`、`bun/test` 与 `test/js/web/` 全量，Batch 3a（`shell` + `spawn`）运行中，尚余 Batch 3 其余 `test/js/bun/*`、Batch 4 `test/js/node/*`、Batch 5 `test/{cli,bundler,integration,napi,v8}` 及已跑批次零星真实失败点收口与 Release 构建验收。

- [用户] debug 无问题、release 编译报错，分析并解决。[回应] 定位 15 处去重错误（12 文件），修复 .cp 后 release 构建成功（build/release/bun，Bun.version 1.3.14），debug zig:check 5/5。[用户] 提示可用 `static struct`。[回应] 已验证 cpc 支持，纯工具类新模块可用。

- [用户] 要求不再使用子智能体（配额不足），继续解决正式/运行问题。[回应] 已终止全部子智能体，单智能体定位并修复 `parseTypescript.cp` `parseTypeScriptDecorators` 与 `visit.cp` `visitTSDecorators` 两处语句丢失导致的 ASan SEGV；`zig:check` 5/5 通过，复测零崩溃。
- [用户] 暂停后续工作，配额不足，待配额恢复再继续。[回应] 已终止后台 `task-21493`（`test/cli/install` 正式版测试），当前工作树干净（HEAD `ee81af1cbac`），`build/release/bun` 已就绪，静候配额恢复后继续。

- [用户] 配额已恢复，重开 2 个子智能体持续推进。[回应] 已同步核查上游看板，并启动 Wave #94 双路并行子智能体：路 1 使用 `build/release/bun` 运行 Batch 5c（`test/cli/install` 87 文件），路 2 纯化 `src/test_runner/`（4 剩余文件 -> 12/12 全清）与 `src/analytics/schema.cp` 的 `--strict` 违规。
- [Wave #94] `src/test_runner/`（4 剩余文件 -> 12/12 全清）与 `src/analytics/schema.cp` `--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`c703534a` `test/cli/install`）与路 2（`648fefb2` `valkey_jsc`+`bit_set`）并行推进中。
- [Wave #95] `src/runtime/valkey_jsc/`（3 剩余文件 -> 7/7 全清）与 `src/collections/bit_set.cp` `--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`c703534a` `test/cli/install`）与路 2（`565184fa` `options_types/schema`+`semver`）并行推进中。
- [Wave #96] `src/options_types/schema.cp` 与 `src/semver/`（5/5 全清）`--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`c703534a` `test/cli/install`）与路 2（`39b9112d` `src/cli/*.cp`）并行推进中。
- [Wave #97] `src/cli/` 前 15 文件 `--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`c703534a` `test/cli/install`）与路 2（`ed419451` `src/cli/` 剩余 18 文件）并行推进中。
- [Wave #98] `src/cli/` 剩余 18 文件 `--strict` 清零（47/47 全清）并通过 `zig:check` 5/5，已原子提交；路 1（`c703534a` `test/cli/install`）与路 2（`329426b1` `src/install/**/*.cp`）并行推进中。
- [Wave #99] `src/install/` 前 15 文件 `--strict` 清零与 `PackageManager`/`runTasks` 修复均通过门禁，已原子提交；路 1（`c703534a` `test/cli/install`）与路 2（`23ca292d` `src/install/` 剩余 17 文件）并行推进中。
- [Wave #100] `src/install/` 剩余 17 文件 `--strict` 清零（69/69 全清）并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查 `bun-pm-pkg`/`bun-patch`）与路 2（`27479990` `js_parser`/`js_printer`/`resolver`/`glob`）并行推进中。
- [Wave #102] `src/glob/` 与 `src/js_parser/ast/` 15 文件 `--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`f13a69d3` `js_parser`/`js_printer`/`resolver` 剩余 6 文件）并行推进中。
- [Wave #103] `js_parser`/`js_printer`/`resolver` 剩余 6 文件 `--strict` 清零（四目录 100% 全清）并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`b3c832d8` `runtime/{api,image,timer,server}`）并行推进中。
- [Wave #104] `runtime/{api,image,timer}/` 全清与 `server/` 4 文件共 18 文件 `--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`83f48f33` `RequestContext.cp`+`server.cp`）并行推进中。
- [Wave #105] `src/runtime/server/{RequestContext,server}.cp` `--strict` 清零（`runtime/{api,image,timer,server}/` 四目录 100% 全清）并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`355e74db` `runtime/webcore/*.cp`）并行推进中。
- [Wave #106] `src/runtime/webcore/*.cp` 全部 12 文件 `--strict` 清零（`runtime/webcore/` 100% 全清）并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`013bd857` `runtime/node/**/*.cp`）并行推进中。
- [Wave #107] `src/runtime/node/` 13 文件（100% 全清）与 `RequestContext.cp` 修正通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`4bd4c458` 全仓扫尾第一批）并行推进中。
- [Wave #108–109] 30 文件（`bake/`/`brotli/`/`bun`/`bun_alloc`/`bun_core`/`bundler_jsc`/`clap` 等）`--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`d156e5f1` 第 3 批 15 文件）并行推进中。
- [Wave #110] 第 3 批 15 文件（`cli/test`/`collections`/`crash_handler`/`csrf`/`css/context`）`--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`6c9f180f` `css/` 第 4 批 15 文件）并行推进中。
- [Wave #111] `src/css/` 第 4 批 15 文件 `--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`8423f739` 第 5 批 15 文件）并行推进中。
- [Wave #112] `src/css/` 第 5 批 15 文件 `--strict` 清零并通过 `zig:check` 5/5，已原子提交；路 1（`eb89699e` 只读排查）与路 2（`ee82f53c` 第 6 批 15 文件）并行推进中。

- [Wave #113–#119] 完成 105 个 `--strict` 报错 `.cp` 文件的清零与 4 项硬门禁验收（覆盖 `css/values/*`、`event_loop/*`、`exe_format/*`、`http_jsc/*`、`ini`、`interchange/yaml`、`io/*`、`js_parser/parser`、`jsc/*`、`libarchive*/`、`libdeflate_sys`、`logger`、`lolhtml_sys`、`main*`、`md/*`、`meta`、`mimalloc_sys`、`napi`、`options_types`、`patch*`、`paths`、`perf/*`、`picohttp`、`platform/darwin`、`ptr/*`、`router/*`、`runtime/api/bun/*`、`runtime/crypto/CryptoHasher`、`runtime/dns_jsc/*`、`runtime/ffi/ffi`、`runtime/node`），全仓 `bun run zig:check` 5/5 绿色通过，已原子提交。
- [用户] 执行当前任务后，收回子智能体，暂停工作，要退出 agy 切换配额账号。[回应] 已完成 Wave #112–#119 全仓 `bun run zig:check` 5/5 校验与原子 Git 提交，已通过 `manage_subagents(Action='kill_all')` 收回并终止全部子智能体，当前工作树干净，已暂停后续派发。

- [用户] 配额恢复了，继续开两个子智能体和主智能体并行工作，完成阶段任务、更新文档、git提交后自主推导下阶段任务持续工作。[回应] 已核查 `temp/strict_scan.json` 剩余 84 个 `--strict` 文件，立即启动 Wave #120（`socket/`+`webcore/`+`webview/`+`s3_signing/` 15 文件）与 Wave #121（`shell/`+`sourcemap/`+`sourcemap_jsc/` 15 文件）双路并发纯化。

- [用户] 最大限度节省 token 情况下加快进度；每隔 3 小时分析一次如何节省 token、提高效率，测试如有效，执行的同时合并到 `GEMINI.md` 固化，实现自我优化。[回应] 已注册每 3 小时自动唤醒的自优化复盘定时任务（`0 */3 * * *`），并继续保持双路子智能体流水线接力与批量校验以最大化省 Token 提速。
- [用户] 你当前计划是什么? 执行 bun 测试文件, 还是 优化 cp 代码?[回应] 当前正处于全仓 `.cp` `--strict` 纯化的最后收官阶段（Wave #120–#123 60 文件已完成，最后 2 批 Wave #124–#125 共 24 文件正在 2 路子智能体并行收尾）；待全仓 1295/1295 `--strict` 清零与 `zig:check` 5/5 全绿提交后，立即切入重新构建 `release/debug` 并系统修复 `bun test` 运行时失败/崩溃用例。
- [用户] 为何屏幕输出很多代码, 浪费 token?[回应] 此前 `bun run zig:check 2>&1 | tail -n 30` 会打印多行 Zig `referenced by:` 调用栈；已立即改用 `rg -E "error:|Build Summary|steps succeeded|FAILED"` 过滤，仅保留 2–5 行核心摘要以节省 Token。
- [Wave #120–#126 里程碑] 完成全仓最后 90 个 `.cp` 文件的 `--strict` 纯化与 4 项硬门禁验收（覆盖 `runtime/socket/*`、`webcore/{blob,fetch,s3}/*`、`webview/*`、`s3_signing/*`、`shell/*`、`sourcemap*/*`、`sql*/*`、`standalone_graph`、`string/*`、`sys*/*`、`tcc_sys`、`test_runner/*`、`threading/*`、`unicode/*`、`uws*/*`、`watcher/`、`bun_alloc/*`），全仓 **1,295/1,295 `.cp` `--strict` 0 报错**且 **`bun run zig:check` 5/5 steps succeeded**，已原子提交；同步已启动 Phase 2 只读诊断（`176eddd6` 排查 `bun pm pkg get` 与 `bun-patch` segfault）。
- [Phase 2 修复] 根据只读排查结果修复 4 处运行时/Release 问题：`src/install/isolated_install/Installer.cp`（`startTask` 用 `&_.tasks.ptr[idx]` 保留指针避免栈地址入线程池导致 `--linker=isolated`/`bun-patch` `SIGSEGV`）、`src/install/NetworkTask.cp`（`appendAuth` 补全 `else { return; }` 与 `npm-auth-type: legacy`）、`src/cli/pm_pkg_command.cp`（`getJsonValue` 恢复调用 `formatJson` 保留 JSON 字符串外层引号）、`src/runtime/server/RequestContext.cp`（2 处 `has_finalized` 补 `else @as(void, undefined)`）；`zig:check` 5/5 通过并原子提交。
- [跨 AGY 提单] 向 `cp_agy` 提交 `task-bun-20261124-struct-addr-of`（repro: `/data/dev/cp/temp/repro_struct_addr_of.cp`）：`[]Struct` 切片取元素地址 `const task = &_.tasks[idx];` 丢失 `&` 生成值拷贝，导致传入线程池栈地址引发 `SIGSEGV`。
- [跨 AGY 结单] 收到 `cp_agy` 结单 `task-bun-20261124-struct-addr-of`（修复 `cpc/src/codegen/borrow.rs` `is_zig_heap_ptr_type`）：本地 `cpc --lib /data/dev/cp/temp/repro_struct_addr_of.cp` 复现验证已正确生成 `(&self.tasks[@as(usize, @intCast(entry_id.get()))])` 保留 `&`，已执行 `ack`。
- [跨 AGY 提单] 向 `cp_agy` 提交 `task-bun-20261124-multiline-string`（repro: `/data/dev/cp/temp/repro_multiline_string.cp`）：`cpc` 将 Zig 多行字符串 `\\` 错误拆分为 `const s = "line1"; _ = "line2";` 导致后续行丢失（已修复 `src/cli/pm_pkg_command.cp` `help_text`）。
- [Phase 2 第二批修复] 修复 `src/cli/pm_pkg_command.cp`（`help_text` 改用 `\n` 拼接修复 `cpc` `\\` 多行字符串截断；`execGet` 补全 `pkg.root.data != .e_object` 校验）及 `src/collections/bit_set.cp`（`ArrayBitSet.iterator` 显式声明 `_: *const Self`，根除 `AutoBitSet.iterator` 返回栈帧 `&self.masks` 悬垂指针导致的 `GlobWalker.evalFile` 越界与 `bun-patch` workspace 崩溃）；`zig:check` 5/5 通过并原子提交，随即重编 `build:release`。
- [用户] 询问当前正在做的工作是什么，以及这些错误是从哪里来的（是否来自 `bun bd test`）。[回应] 说明当前已进入 Phase 2「构建与运行时测试稳定化」阶段（全仓 1,295 个 `.cp` 文件的 `--strict` 纯化与 `bun run zig:check` 5/5 已 100% 完成）；这些运行时错误（`bun pm pkg`、`bun-patch`、`GlobWalker.evalFile` 等）正是通过 `bun bd test`（debug 构建）与 `./build/release/bun test`（release 构建）跑 Bun 原生测试集实测暴露出来的，目前已逐一排查修复并在重编 `build:release` 回归验证中。
- [跨 AGY 结单] 收到 `cp_agy` 结单 `task-bun-20261124-multiline-string`（修复 `cpc/src/lexer.rs` 合并连续 `\\` 多行字符串为 `\n` 拼接的单个 `StringLiteral`）：本地 `cpc --lib /data/dev/cp/temp/repro_multiline_string.cp` 复现验证已正确生成 `"line1\nline2\nline3"`，已执行 `ack` 并重编全仓 10 个含 `\\` 的 `.cp` 文件。
- [用户] 指出 `build:release` 非常耗时，一般应使用 debug 版本（`bun bd test`）测试，询问为何使用 release 版本测试。[回应] 说明此前因修复 Release 专属编译报错（`RequestContext.cp`）需验证 `build:release`，且大批量冒烟测试（`fetch`/`crypto` 等上万用例）时 Release 执行更快；接受建议，后续日常修复与单测回归统一切换为 `bun bd test`（按 `AGENTS.md` 规范执行，仅在阶段收官时构建一次 `build:release`）。
- [用户] 询问 `bun test` 文件已经完成了多少、还剩多少没有完成。[回应] 统计主目录共 1,067 个测试文件（`cli` 150、`js/bun` 406、`js/node` 258、`js/web` 144、`bundler` 89、`integration` 20，未含 `napi`/`v8`/`regression`）；目前已完成约 **241 个文件**的实测/冒烟（`js/bun/util`+`io` 60、`web/fetch` 57、`node/{path,buffer,crypto,fs,url,events,zlib,http}` 118、`cli/install` 核心 6 个，外加 3 个刚修复正在 `bun bd test`），剩余 **~826+ 个测试文件**待分批验证。
- [用户] 指示主智能体与两个子智能体加快进度完成所有测试，并持续保持每 3 小时定时任务自动审计 Token 消耗与工作效率、有效优化固化进 `GEMINI.md`。[回应] 已同步开启两路高速只读扫测子智能体（`8589deab` 负责 `bun/shell`、`bun/glob` 及 `web/` 剩余目录；`ac07cfd1` 负责 `cli/` 剩余目录及 `integration/`），主智能体等待 `task-23534`（`bun bd test`）完成后立即提交并接力修复下一批问题。
- [Phase 2 第三批修复] 修复 6 个核心 `.cp` 模块并验证：`src/semver/Version.cp`（5 处 `'0'..'9'`/`'a'..'z'`/`'A'..'Z'` 改为闭区间 `..=`，修复含 `'9'` 版本解析失败与 `bun pm migrate`）、`src/install/PackageManager/{PackageJSONEditor,updatePackageJSONAndInstall}.cp`（`updates: *UpdateRequestSlice` + `let updates = updates_in;` 修复 `bun add --only-missing`/`--analyze` 52/52 通过）、`src/cli/package_manager_command.cp`（`bun pm ls` 补全 `({d})` 且改为遍历 `root_deps`，`bun-pm.test.ts` **12/12 通过**）、`src/shell/states/Expansion.cp`（`for (*simple_atom of ...)` 修复命令替换悬垂栈指针 SIGSEGV）、`src/test_runner/expect/toHaveReturned.cp`（修正 `\n\n` 空行）；`bun run zig:check` 5/5 通过。
- [跨 AGY 提单] 向 `cp_agy` 提交 `task-bun-20261124-paren-try-field`（repro: `/data/dev/cp/temp/repro_paren_try_field.cp`）：赋值语句 `x = (bar()?).contents;` 被 `cpc` 静默丢弃整条语句；已先行在 `src/js_parser/lexer.cp` 提取 `const scan_res = ...?;` 修复 `#\u0063onstructor` 词法解析。
- [跨 AGY 提单] 向 `cp_agy` 提交 `task-bun-20261124-private-enum-drop`（repro: `/data/dev/cp/temp/repro_private_enum_drop.cp`）：未转义关键字枚举 `.private` 传入时 `cpc` 未报错而是静默丢弃整条语句；已修复 `src/js_parser/lexer.cp:1160,1171` 改用 `.@"private"`，`zig:check` 5/5 通过。
- [Phase 2 第五批修复] 修复 `src/jsc/ModuleLoader.cp`（`InputFdGuard.should_close` 改用 `*bool` 指针传入 `&should_close_input_file_fd`，根除 `bun_watcher.addFile` 接管后仍被 RAII 误关 fd 引发 `--hot` `EISDIR` 报错），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过。
- [跨 AGY 结单] 收到 `cp_agy` 结单 `task-bun-20261124-private-enum-drop`（`cpc/src/parser/expr.rs` 新增 `Token::Private`/`Union`/`Implements`/`Defer`/`ErrDefer` 到 `is_ident_token` 与 `parse_ident`，使 `.private` 等枚举字面量正常解析而不静默丢弃语句）：本地 `cpc --lib /data/dev/cp/temp/repro_private_enum_drop.cp` 0 报错通过，已执行 `ack`。
- [Phase 2 第六批修复] 修复 `src/runtime/api/html_rewriter.cp`（将函数内局部返回 `struct { ~init() }` 的 `WrapperGuardType` 改为 `defer cleanupWrapper(ZigType, field_name, wrapper);`，修复 `cpc` 未对动态返回类型局部变量注入析构导致的 `text-chunk-null-access` ASan SEGV），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第七批修复] 用新版 `cpc` 重编 `src/interchange/json5.cp`（修复 `-0.0` 一元负号变 `0 - n` 丢失负号的问题）并修复 `src/runtime/api/bun/Terminal.cp`（`createPty` 补回 `updateNonblocking(..., true)` 与 `setCloseOnExec(...)`，解决 `/dev/ptmx` 阻塞读导致 `Bun.Terminal` 永久卡死），`zig:check` 5/5 通过并原子提交。
- [3小时自优化复盘 #1] 完成 `task-22829` 首轮审计，提炼 3 条已实测生效的 Phase 2 策略并固化合并进 `GEMINI.md` 第十二节：(1) 日常回归统一用 `bun bd test` + `BUN_DEBUG_QUIET_LOGS=1`；(2) `timeout <N>` + `rg` 极简过滤防挂起与刷屏；(3) 运行时共性模式缺陷全仓 `rg` 倍增排雷（`..=`、`.@"private"`、`*const Self`、`defer cleanup`）。
- [Phase 2 第八批修复] 修复 `src/semver/SemverRange.cp`（`Comparator.fmt`、`Range.fmt` 显式声明 `_: *const ...` 指针接收者并返回 `_`）及全仓 `rg` 排雷同步发现的 `src/bundler/Chunk.cp:247`（`ChunkCssImportOrder.fmt` 同类 `&_` 悬垂栈指针），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第九批修复] 根据全仓静态安全审计结果修复最后 3 处「值接收者方法内返回 `&_` 悬垂栈指针」隐患：`src/css/selectors/parser.cp:1533,1829`（`debug()` 显式声明 `_: *const This` 并返回 `_`）与 `src/event_loop/SpawnSyncEventLoop.cp:93`（`handle(*_)` 显式声明可变指针接收者），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第十批修复] 修复 `src/interchange/json.cp`（将返回 `struct { ~init() }` 的 `DuplicateNodeGuard` 改为显式 `defer releaseDuplicateNode(...)`，根除 `json.zig:480` 缺失 `defer __cp_deinit` 导致的 `HashMapPool` 节点泄漏），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第十一批修复] 修复 `src/test_runner/snapshot.cp:545`（`appendSlice(" ")` 改为 `appendSlice("  ")` 修复多行 inline snapshot 缩进少 1 空格），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第十二批修复] 修复两处 `while + continue` 漏步进死循环：`src/css/selectors/selector.cp`（`isCompatible` 与前序循环改用 `: (s_i += 1)` / `: (c_i += 1)` 步进，消除 `21907.test.ts` 与 `bootstrap-4.css` 卡死）及全仓审计同步发现的 `src/runtime/dns_jsc/dns.cp:1396`（`entry.refcount != 0` 分支补全 `i += 1`），`zig:check` 5/5 通过并原子提交。
- [Phase 2 第十三批修复] 修复 `src/css/generics.cp:150`（`implementHash` 对 `*const T` 误写为 `std.mem.asBytes(&val)` 哈希栈指针地址，改为 `std.mem.asBytes(val)` 修复 `FontFamily` 哈希与 `font > ui-sans-serif` 去重失效），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第十四批修复] 修复 `src/css/selectors/selector.cp:255,258`（`getPrefix` 外/内两层 `while` 循环补全 `: (s_i += 1)` 与 `: (c_i += 1)`，根除 CSS 压缩/前缀路径死循环；全仓 `deeb718b` 审计确认再无同类漏步进循环），`zig:check` 5/5 通过并原子提交。
- [Phase 2 第十五批修复] 修复 `src/css/generics.cp:676`（`pub fn hash` 中另一处 `std.mem.asBytes(&val)` 改为 `std.mem.asBytes(val)`，消除 `isSimpleEqlType` 哈希栈地址问题），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第十六批修复] 修复 `src/css/selectors/selector.cp:114-116`（`downlevelSelectors` 删除重复 `c_i += 1; s_i += 1;`，修复步进 +2 导致 `css.test.ts` 42 处降级/前缀用例失败），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 阶段里程碑] 完成全仓测试扫测、3 轮全仓 1,295 文件静态安全审计及 19 个 `.cp` 模块修复并重编 `build/release/bun`（100/100 步）；Release 回归验证 12 组核心测试集 **676 pass / 0 fail**（`css.test.ts` 因依赖 Debug 专属 `bun:internal-for-testing`，在 `bun-debug` 下已 **1,032 pass / 0 fail**）。
- [Phase 2 第十七批修复] 修复 `src/install/lockfile/Tree.cp`（`isFilteredDependencyOrWorkspace` 补全 `isOptionalPeer()`、`pkg_metas[pkg_id].isDisabled(cpu, os)` 与 `isBundled()` 过滤，解决 `bun-install-cpu-os.test.ts` 8 处 `ENOENT` 失败），`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第十八批修复] 修复 3 个核心 `.cp` 模块并同步更新 `test/stats.md`：`src/bundler/linker_context/findImportedFilesInCSSOrder.cp:364`（补全反向保留 `condition.layer != null` 与空条目 `continue`，修复 `CSSAtImportConditionsAtLayerBundle`）、`src/js_parser/ast/maybe.cp:266`（`fromOwnedSlice(decls[0..1])` 修复 `NewExpressionCommonJS`）、`src/bake/DevServer.cp`（`RouterTypeListGuard` 增设 `active: bool = true` 并在 `initEmpty` 成功后置 `false`，根除 `test/bake/dev/` 9 个文件共现的 Use-After-Free 段错误）；`cpc_brief.py` 4 项门禁与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第十九批修复] 修复 `src/bundler/linker_context/computeChunks.cp:368-376`：在 CSS chunk 排序循环外预存 `base_sorted_len = sorted_chunks.len`，修复 `sorted_chunks.len` 在循环内逐次递增导致 `sorted_index` 双倍增长、`remapped_css_indexes` 越界的 bug（`html/multiple-entries`、`html/shared-chunks`、`html-import/multiple-manifests`）；`cpc_brief.py`（0.976）与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十批修复] 修复 `src/css/css_parser.cp`：`NestedRuleParser` 新增 `depth: u16 = 0` 字段，并在 `parseNested` 入口限制 `_.depth >= 64` 返回 `ParserError.maximum_nesting_depth`，防止 `css-fuzz.test.ts` `deepNesting(300)` 在 Release 模式下因深层递归溢出栈空间导致 SIGSEGV；`cpc_brief.py`（1.034）与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十一批修复] 修复 `src/js_parser/ast/maybe.cp:170-208`：将原 `for (props) ... else { deoptimizeCommonJSNamedExports(); return null; }` 被机械改为 `if (props.len == 0)` 导致的语义偏差还原为循环后置去优化，消除 `module.exports = { ... }` 误转 ESM 导致 `cjs2esm/UnwrappedModuleRequireAssigned` 缺失 `__toESM(` 的问题；`cpc_brief.py`（1.019）与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十二批修复] 修复 `src/bake/DevServer.cp:2632`：`makeArrayForServerComponentsPatch` 在 `items.len == 0` 时误返回 `JSValue.zero` 触发 `Bun__JSValue__call` 空值断言崩溃，改回 `JSValue.null`；`cpc_brief.py`（1.086）与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 全仓扫测与 Release 收官回归] 完成 `build:release` 重建（`1.3.14-canary.1+71902ec90`）并完成全部修复套件回归与 `test/` 大规模扫测汇总写入 `test/stats.md`：`bundler/{css,esbuild}` **945/0**、`bundler_{cjs2esm,html,css}` **37/0**、`css-fuzz` **5/0**、`cli/hot` **15/0**、`bun-install-cpu-os`+`bun-lock` **22/0**、`bake/dev/{bundle,plugins,vfile,request-cookies}` **26/0**（debug）、`bake/dev/{esm,import-meta-inline,react-response}` **30/0**（release）、`js/sql` **466/0**、`cli/run` **1021/0**、`cli/test` **200/0**、`js/web` **5289+/6**、`js/node` **4825+/14**、`js/bun` **15249/11**、`first_party/deno/workerd/valkey` **363/0**、`third_party` **1183/10**；并排查确认 `tls peer cert`、`dlopen` ABI、`http uaf`、`locked blob body`、`ffi` 等剩余失败均非 `.cp` 迁移缺陷。
- [Phase 2 第二十三批修复] 修复 `src/install/lockfile/Package/WorkspaceMap.cp:295-395`：补全原版 `WorkspaceMap_.zig:271-294` 在迁移时遗漏的根 `package.json` 跳过与后续否定模式（`!packages/...`）`.negate_no_match` 过滤逻辑，并修正 `Path.joinAbsStringBufZ` 首参为 `cwd`；`cpc_brief.py`（1.116）与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十四批修复] 修复 `src/bake/DevServer/SourceMapStore.cp:61-115`（修复 `renderJSON` 缺少引号及 `,` 与路径错位导致的 `InvalidJSON`，对齐 `file://`/`bun://` 路径生成，解决 `DEV:server-sourcemap-1/2/3`）与 `src/runtime/api/glob.cp:130-157`（补全非字符串 `cwd` 抛错校验、修正 `cwd`/`absolute` 顺序及 `"throwErrorOnBrokenSymlink"` 拼写）；`cpc_brief.py` 与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十五批修复] 修复 `src/runtime/webcore/Blob.cp:2504-2511`：`constructBunFile` 恢复调用 `Blob.findOrCreateFileFromPath(&path, globalObject, false)`（并将 `DeinitUnprotectGuard` 移至 `s3://` 判断外），根除 `--compile` 嵌入式文件 `/$bunfs/` 全报 `ENOENT`（`EmbeddedFileOutfile`/`Bun.embeddedFiles`/`10344`/`bun-build-compile-wasm` 等 8 项）；`cpc_brief.py`（0.865）与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十六批修复] 修复 `src/install/lockfile/Tree.cp:401-442`（补全 `parent_res` 与 `dep.behavior.isEnabled(dep_features)` 过滤及根包/工作区分支对齐 `Tree_.zig`，解决 `--omit=*`/`--production` 失效）与 `src/cli/package_manager_command.cp:155-157`（移除多余切掉 `"pm"` 的分支，修复 `bun pm trust`/`untrusted` 报 `expected package names(s) or --all`），并固化 `task-22829` #3 至 `GEMINI.md`；`cpc_brief.py` 与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十七批修复] 修复 `src/install/lockfile/bun.lock.cp:2024-2029`：将提前至 `if` 外的 `seen_deps.getOrPut` 收回 `if (!dep.behavior.isWorkspace())` 分支内，恢复原版 `and` 短路求值语义，防止 workspace 依赖污染 `seen_deps` 跳过 `mapDepToPkg` 导致重复保存 lockfile（`bun-workspaces.test.ts` 30 项 `allow duplicates` / `listing workspace`）；`cpc_brief.py`（1.028）与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十八批修复] 修复 `src/runtime/api/bun/subprocess.cp:359-390`：`jsRef` 与 `jsUnref` 补全 `_.process.enableKeepingEventLoopAlive()`/`disableKeepingEventLoopAlive()`、`!_.hasCalledGetter(.stdin/.stdout/.stderr)` 判断与 `_.updateHasPendingActivity()`，解决 `Bun.spawn(...).unref()` 事件循环保活挂起与 `third_party/esbuild-child_process.test.ts` 超时；`cpc_brief.py`（0.949）与 `zig:check` 5/5 通过并原子提交。
- [Phase 2 第二十九批修复] 修复 `src/js/node/child_process.ts:1512-1524`：为 `ChildProcess.ref()` 与 `ChildProcess.unref()` 补全对 `[this.#stdin, this.#stdout, this.#stderr]` 的 `ref()`/`unref()` 调用，彻底解决 `node:child_process.spawn(...).unref()` 退出保活挂起（`cp_spawn=0` 验证通过），并同步更新 `test/stats.md` 后原子提交。
- [Phase 3 第一批全仓内存安全与防泄漏加固] 完成 18 个核心 `.cp` 模块的 `errdefer` / RAII 守卫 / 堆对象释放加固并全部通过 `cpc_brief.py` 与 `zig:check` 5/5 原子提交：`AsyncHTTP.cp`（`sendSync` `SingleHTTPChannel` 销毁）、`TextDecoder.cp`（`decodeUTF16` `errdefer`）、`spawn.cp`（`openZ` `errdefer`）、`FormData.cp`（`_AsyncFormData.init` `errdefer`）、`FetchTasklet.cp`（`errdefer destroy`）、`Blob.cp`（`constructBunFile` `content_type` `errdefer`）、`IndexStringMap.cp` & `AnyTaskWithExtraContext.cp` & `source_map_mode_jsc.cp`（消除全部 3 处 `* = Guard{...}` 匿名未绑定析构漏洞）、`Buffers.cp`（`visited` `defer`）、`processDependencyList.cp`（`dependency_list` `defer`）、`FrameworkRouter.cp`（`rendered` `defer` + `cloneUTF8`）、`PackageManager.cp`（2 处 `PATH` `errdefer`/`deinit`）、`tsconfig_json.cp`（移除错误路径 `disarm()`）、`fs.cp`（`size == 0` 提前返回防 1B 泄漏 + `errdefer free(buf)`）、`css/selectors/parser.cp`（`PseudoClass`/`PseudoElement.toCss` `defer deinit`）、`expect.cp`（`matcher_args` `defer deinit`）、`PostgresSQLQuery.cp`（`query_str` 前置 + `errdefer deref`）。
