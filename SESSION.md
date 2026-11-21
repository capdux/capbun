
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
