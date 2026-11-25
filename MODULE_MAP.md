# CP ↔ Rust (#30412 / `23427dbc12f`) 模块路径映射表

- **CP `.cp` 总数**：`1295` | **Rust `#30412` `.rs` 总数**：`1437` | **自动同名匹配**：`1217/1295` (93%)
- **参考 Worktree**：`../bun-ref`（当前锚定 `23427dbc12f`）

| CP 源码 (`.cp`) | Rust 对应模块 (`#30412` `.rs`) |
| :--- | :--- |
| `src/aio/ParentDeathWatchdog.cp` | `src/io/ParentDeathWatchdog.rs` |
| `src/aio/posix_event_loop.cp` | `src/io/posix_event_loop.rs` |
| `src/aio/stub_event_loop.cp` | `src/io/stub_event_loop.rs` |
| `src/aio/windows_event_loop.cp` | `src/io/windows_event_loop.rs` |
| `src/analytics/analytics.cp` | _需按子系统/函数手工定位_ |
| `src/analytics/schema.cp` | `src/analytics/schema.rs`, `src/options_types/schema.rs` |
| `src/bake/DevServer.cp` | `src/runtime/bake/DevServer.rs` |
| `src/bake/DevServer/Assets.cp` | `src/runtime/bake/DevServer/Assets.rs`, `src/runtime/bake/dev_server/assets.rs` |
| `src/bake/DevServer/DirectoryWatchStore.cp` | `src/runtime/bake/DevServer/DirectoryWatchStore.rs` |
| `src/bake/DevServer/ErrorReportRequest.cp` | `src/runtime/bake/DevServer/ErrorReportRequest.rs` |
| `src/bake/DevServer/HmrSocket.cp` | `src/runtime/bake/DevServer/HmrSocket.rs` |
| `src/bake/DevServer/HotReloadEvent.cp` | `src/runtime/bake/DevServer/HotReloadEvent.rs` |
| `src/bake/DevServer/IncrementalGraph.cp` | `src/runtime/bake/DevServer/IncrementalGraph.rs`, `src/runtime/bake/dev_server/incremental_graph.rs` |
| `src/bake/DevServer/PackedMap.cp` | `src/runtime/bake/DevServer/PackedMap.rs`, `src/runtime/bake/dev_server/packed_map.rs` |
| `src/bake/DevServer/RouteBundle.cp` | `src/runtime/bake/DevServer/RouteBundle.rs`, `src/runtime/bake/dev_server/route_bundle.rs` |
| `src/bake/DevServer/SerializedFailure.cp` | `src/runtime/bake/DevServer/SerializedFailure.rs`, `src/runtime/bake/dev_server/serialized_failure.rs` |
| `src/bake/DevServer/SourceMapStore.cp` | `src/runtime/bake/DevServer/SourceMapStore.rs`, `src/runtime/bake/dev_server/source_map_store.rs` |
| `src/bake/DevServer/WatcherAtomics.cp` | `src/runtime/bake/DevServer/WatcherAtomics.rs` |
| `src/bake/DevServer/memory_cost.cp` | `src/runtime/bake/DevServer/memory_cost.rs` |
| `src/bake/FrameworkRouter.cp` | `src/runtime/bake/FrameworkRouter.rs` |
| `src/bake/bake.cp` | _需按子系统/函数手工定位_ |
| `src/bake/production.cp` | `src/runtime/bake/production.rs` |
| `src/base64/base64.cp` | _需按子系统/函数手工定位_ |
| `src/boringssl/boringssl.cp` | `src/boringssl_sys/boringssl.rs` |
| `src/boringssl_sys/boringssl.cp` | `src/boringssl_sys/boringssl.rs` |
| `src/brotli/brotli.cp` | _需按子系统/函数手工定位_ |
| `src/brotli_sys/brotli_c.cp` | `src/brotli_sys/brotli_c.rs` |
| `src/bun.cp` | `src/bun.rs` |
| `src/bun.js.cp` | `src/bun.js.rs` |
| `src/bun_alloc/BufferFallbackAllocator.cp` | `src/bun_alloc/BufferFallbackAllocator.rs` |
| `src/bun_alloc/LinuxMemFdAllocator.cp` | `src/runtime/allocators/LinuxMemFdAllocator.rs` |
| `src/bun_alloc/MaxHeapAllocator.cp` | `src/bun_alloc/MaxHeapAllocator.rs` |
| `src/bun_alloc/MimallocArena.cp` | `src/bun_alloc/MimallocArena.rs` |
| `src/bun_alloc/NullableAllocator.cp` | `src/bun_alloc/NullableAllocator.rs` |
| `src/bun_alloc/allocation_scope.cp` | _需按子系统/函数手工定位_ |
| `src/bun_alloc/basic.cp` | `src/bun_alloc/basic.rs` |
| `src/bun_alloc/bun_alloc.cp` | _需按子系统/函数手工定位_ |
| `src/bun_alloc/fallback.cp` | `src/bun_alloc/fallback.rs` |
| `src/bun_alloc/fallback/z.cp` | `src/bun_alloc/fallback/z.rs` |
| `src/bun_alloc/heap_breakdown.cp` | `src/bun_alloc/heap_breakdown.rs` |
| `src/bun_alloc/maybe_owned.cp` | `src/bun_alloc/maybe_owned.rs` |
| `src/bun_alloc/memory.cp` | `src/bun_alloc/memory.rs` |
| `src/bun_core/Global.cp` | `src/bun_core/Global.rs` |
| `src/bun_core/Progress.cp` | `src/bun_core/Progress.rs` |
| `src/bun_core/deprecated.cp` | `src/bun_core/deprecated.rs` |
| `src/bun_core/env.cp` | `src/bun_core/env.rs`, `src/sys/windows/env.rs` |
| `src/bun_core/env_var.cp` | `src/bun_core/env_var.rs` |
| `src/bun_core/feature_flags.cp` | `src/bun_core/feature_flags.rs` |
| `src/bun_core/fmt.cp` | `src/bun_core/fmt.rs` |
| `src/bun_core/output.cp` | `src/bun_core/output.rs` |
| `src/bun_core/result.cp` | `src/bun_core/result.rs` |
| `src/bun_core/tty.cp` | `src/bun_core/tty.rs` |
| `src/bun_core/util.cp` | `src/bun_core/util.rs`, `src/runtime/shell/util.rs` |
| `src/bundler/AstBuilder.cp` | `src/bundler/AstBuilder.rs` |
| `src/bundler/BundleThread.cp` | `src/bundler/BundleThread.rs` |
| `src/bundler/Chunk.cp` | `src/bundler/Chunk.rs`, `src/sourcemap/Chunk.rs` |
| `src/bundler/DeferredBatchTask.cp` | `src/bundler/DeferredBatchTask.rs` |
| `src/bundler/Graph.cp` | `src/bundler/Graph.rs` |
| `src/bundler/HTMLImportManifest.cp` | `src/bundler/HTMLImportManifest.rs` |
| `src/bundler/HTMLScanner.cp` | `src/bundler/HTMLScanner.rs` |
| `src/bundler/IndexStringMap.cp` | `src/bundler/IndexStringMap.rs` |
| `src/bundler/LinkerContext.cp` | `src/bundler/LinkerContext.rs` |
| `src/bundler/LinkerGraph.cp` | `src/bundler/LinkerGraph.rs` |
| `src/bundler/OutputFile.cp` | `src/bundler/OutputFile.rs` |
| `src/bundler/ParseTask.cp` | `src/bundler/ParseTask.rs` |
| `src/bundler/PathToSourceIndexMap.cp` | `src/bundler/PathToSourceIndexMap.rs` |
| `src/bundler/ServerComponentParseTask.cp` | `src/bundler/ServerComponentParseTask.rs` |
| `src/bundler/ThreadPool.cp` | `src/bundler/ThreadPool.rs`, `src/threading/ThreadPool.rs` |
| `src/bundler/analyze_transpiled_module.cp` | `src/bundler/analyze_transpiled_module.rs` |
| `src/bundler/barrel_imports.cp` | `src/bundler/barrel_imports.rs` |
| `src/bundler/bundle_v2.cp` | `src/bundler/bundle_v2.rs` |
| `src/bundler/cache.cp` | `src/bundler/cache.rs` |
| `src/bundler/defines-table.cp` | _需按子系统/函数手工定位_ |
| `src/bundler/defines.cp` | `src/bundler/defines.rs` |
| `src/bundler/entry_points.cp` | `src/bundler/entry_points.rs` |
| `src/bundler/linker.cp` | `src/bundler/linker.rs` |
| `src/bundler/linker_context/MetafileBuilder.cp` | `src/bundler/linker_context/MetafileBuilder.rs` |
| `src/bundler/linker_context/OutputFileListBuilder.cp` | `src/bundler/linker_context/OutputFileListBuilder.rs` |
| `src/bundler/linker_context/StaticRouteVisitor.cp` | `src/bundler/linker_context/StaticRouteVisitor.rs` |
| `src/bundler/linker_context/computeChunks.cp` | `src/bundler/linker_context/computeChunks.rs` |
| `src/bundler/linker_context/computeCrossChunkDependencies.cp` | `src/bundler/linker_context/computeCrossChunkDependencies.rs` |
| `src/bundler/linker_context/convertStmtsForChunk.cp` | `src/bundler/linker_context/convertStmtsForChunk.rs` |
| `src/bundler/linker_context/convertStmtsForChunkForDevServer.cp` | `src/bundler/linker_context/convertStmtsForChunkForDevServer.rs` |
| `src/bundler/linker_context/doStep5.cp` | `src/bundler/linker_context/doStep5.rs` |
| `src/bundler/linker_context/findAllImportedPartsInJSOrder.cp` | `src/bundler/linker_context/findAllImportedPartsInJSOrder.rs` |
| `src/bundler/linker_context/findImportedCSSFilesInJSOrder.cp` | `src/bundler/linker_context/findImportedCSSFilesInJSOrder.rs` |
| `src/bundler/linker_context/findImportedFilesInCSSOrder.cp` | `src/bundler/linker_context/findImportedFilesInCSSOrder.rs` |
| `src/bundler/linker_context/generateChunksInParallel.cp` | `src/bundler/linker_context/generateChunksInParallel.rs` |
| `src/bundler/linker_context/generateCodeForFileInChunkJS.cp` | `src/bundler/linker_context/generateCodeForFileInChunkJS.rs` |
| `src/bundler/linker_context/generateCodeForLazyExport.cp` | `src/bundler/linker_context/generateCodeForLazyExport.rs` |
| `src/bundler/linker_context/generateCompileResultForCssChunk.cp` | `src/bundler/linker_context/generateCompileResultForCssChunk.rs` |
| `src/bundler/linker_context/generateCompileResultForHtmlChunk.cp` | `src/bundler/linker_context/generateCompileResultForHtmlChunk.rs` |
| `src/bundler/linker_context/generateCompileResultForJSChunk.cp` | `src/bundler/linker_context/generateCompileResultForJSChunk.rs` |
| `src/bundler/linker_context/postProcessCSSChunk.cp` | `src/bundler/linker_context/postProcessCSSChunk.rs` |
| `src/bundler/linker_context/postProcessHTMLChunk.cp` | `src/bundler/linker_context/postProcessHTMLChunk.rs` |
| `src/bundler/linker_context/postProcessJSChunk.cp` | `src/bundler/linker_context/postProcessJSChunk.rs` |
| `src/bundler/linker_context/prepareCssAstsForChunk.cp` | `src/bundler/linker_context/prepareCssAstsForChunk.rs` |
| `src/bundler/linker_context/renameSymbolsInChunk.cp` | `src/bundler/linker_context/renameSymbolsInChunk.rs` |
| `src/bundler/linker_context/scanImportsAndExports.cp` | `src/bundler/linker_context/scanImportsAndExports.rs` |
| `src/bundler/linker_context/writeOutputFilesToDisk.cp` | `src/bundler/linker_context/writeOutputFilesToDisk.rs` |
| `src/bundler/options.cp` | `src/bundler/options.rs` |
| `src/bundler/transpiler.cp` | `src/bundler/transpiler.rs` |
| `src/bundler_jsc/JSBundleCompletionTask.cp` | `src/runtime/api/js_bundle_completion_task.rs` |
| `src/bundler_jsc/PluginRunner.cp` | `src/bundler_jsc/PluginRunner.rs`, `src/jsc/PluginRunner.rs` |
| `src/bundler_jsc/analyze_jsc.cp` | `src/bundler_jsc/analyze_jsc.rs` |
| `src/bundler_jsc/options_jsc.cp` | `src/bundler_jsc/options_jsc.rs`, `src/runtime/dns_jsc/options_jsc.rs` |
| `src/bundler_jsc/output_file_jsc.cp` | `src/runtime/api/output_file_jsc.rs` |
| `src/bundler_jsc/source_map_mode_jsc.cp` | `src/bundler_jsc/source_map_mode_jsc.rs` |
| `src/cares_sys/c_ares.cp` | `src/cares_sys/c_ares.rs` |
| `src/clap/args.cp` | `src/clap/args.rs` |
| `src/clap/clap.cp` | _需按子系统/函数手工定位_ |
| `src/clap/comptime.cp` | `src/clap/comptime.rs` |
| `src/clap/streaming.cp` | `src/clap/streaming.rs` |
| `src/cli/Arguments.cp` | `src/bunfig/arguments.rs`, `src/runtime/cli/Arguments.rs` |
| `src/cli/add_command.cp` | `src/runtime/cli/add_command.rs` |
| `src/cli/add_completions.cp` | `src/runtime/cli/add_completions.rs` |
| `src/cli/audit_command.cp` | `src/runtime/cli/audit_command.rs` |
| `src/cli/build_command.cp` | `src/runtime/cli/build_command.rs` |
| `src/cli/bunfig.cp` | `src/bunfig/bunfig.rs` |
| `src/cli/bunx_command.cp` | `src/runtime/cli/bunx_command.rs` |
| `src/cli/ci_info.cp` | `src/runtime/cli/ci_info.rs` |
| `src/cli/cli.cp` | _需按子系统/函数手工定位_ |
| `src/cli/colon_list_type.cp` | `src/runtime/cli/colon_list_type.rs` |
| `src/cli/create/SourceFileProjectGenerator.cp` | `src/runtime/cli/create/SourceFileProjectGenerator.rs` |
| `src/cli/create_command.cp` | `src/runtime/cli/create_command.rs` |
| `src/cli/discord_command.cp` | `src/runtime/cli/discord_command.rs` |
| `src/cli/exec_command.cp` | `src/runtime/cli/exec_command.rs` |
| `src/cli/filter_arg.cp` | `src/runtime/cli/filter_arg.rs` |
| `src/cli/filter_run.cp` | `src/runtime/cli/filter_run.rs` |
| `src/cli/fuzzilli_command.cp` | `src/runtime/cli/fuzzilli_command.rs` |
| `src/cli/init_command.cp` | `src/runtime/cli/init_command.rs` |
| `src/cli/install_command.cp` | `src/runtime/cli/install_command.rs` |
| `src/cli/install_completions_command.cp` | `src/runtime/cli/install_completions_command.rs` |
| `src/cli/link_command.cp` | `src/runtime/cli/link_command.rs` |
| `src/cli/list-of-yarn-commands.cp` | `src/runtime/cli/list-of-yarn-commands.rs` |
| `src/cli/multi_run.cp` | `src/runtime/cli/multi_run.rs` |
| `src/cli/open.cp` | `src/runtime/cli/open.rs` |
| `src/cli/outdated_command.cp` | `src/runtime/cli/outdated_command.rs` |
| `src/cli/pack_command.cp` | `src/runtime/cli/pack_command.rs` |
| `src/cli/package_manager_command.cp` | `src/runtime/cli/package_manager_command.rs` |
| `src/cli/patch_command.cp` | `src/runtime/cli/patch_command.rs` |
| `src/cli/patch_commit_command.cp` | `src/runtime/cli/patch_commit_command.rs` |
| `src/cli/pm_pkg_command.cp` | `src/runtime/cli/pm_pkg_command.rs` |
| `src/cli/pm_trusted_command.cp` | `src/runtime/cli/pm_trusted_command.rs` |
| `src/cli/pm_version_command.cp` | `src/runtime/cli/pm_version_command.rs` |
| `src/cli/pm_view_command.cp` | `src/runtime/cli/pm_view_command.rs` |
| `src/cli/pm_why_command.cp` | `src/runtime/cli/pm_why_command.rs` |
| `src/cli/publish_command.cp` | `src/runtime/cli/publish_command.rs` |
| `src/cli/remove_command.cp` | `src/runtime/cli/remove_command.rs` |
| `src/cli/repl.cp` | `src/runtime/cli/repl.rs` |
| `src/cli/repl_command.cp` | `src/runtime/cli/repl_command.rs` |
| `src/cli/run_command.cp` | `src/runtime/cli/run_command.rs` |
| `src/cli/scan_command.cp` | `src/runtime/cli/scan_command.rs` |
| `src/cli/shell_completions.cp` | `src/runtime/cli/shell_completions.rs` |
| `src/cli/test/ChangedFilesFilter.cp` | `src/runtime/cli/test/ChangedFilesFilter.rs` |
| `src/cli/test/ParallelRunner.cp` | `src/runtime/cli/test/ParallelRunner.rs` |
| `src/cli/test/Scanner.cp` | `src/runtime/cli/test/Scanner.rs` |
| `src/cli/test/parallel/Channel.cp` | `src/runtime/cli/test/parallel/Channel.rs`, `src/threading/channel.rs` |
| `src/cli/test/parallel/Coordinator.cp` | `src/runtime/cli/test/parallel/Coordinator.rs` |
| `src/cli/test/parallel/FileRange.cp` | `src/runtime/cli/test/parallel/FileRange.rs` |
| `src/cli/test/parallel/Frame.cp` | `src/runtime/cli/test/parallel/Frame.rs` |
| `src/cli/test/parallel/Worker.cp` | `src/runtime/cli/test/parallel/Worker.rs` |
| `src/cli/test/parallel/aggregate.cp` | `src/runtime/cli/test/parallel/aggregate.rs` |
| `src/cli/test/parallel/runner.cp` | `src/runtime/cli/test/parallel/runner.rs` |
| `src/cli/test_command.cp` | `src/runtime/cli/test_command.rs` |
| `src/cli/unlink_command.cp` | `src/runtime/cli/unlink_command.rs` |
| `src/cli/update_command.cp` | `src/runtime/cli/update_command.rs` |
| `src/cli/update_interactive_command.cp` | `src/runtime/cli/update_interactive_command.rs` |
| `src/cli/upgrade_command.cp` | `src/runtime/cli/upgrade_command.rs` |
| `src/cli/which_npm_client.cp` | `src/runtime/cli/which_npm_client.rs` |
| `src/cli/why_command.cp` | `src/runtime/cli/why_command.rs` |
| `src/codegen/process_windows_translate_c.cp` | `src/codegen/process_windows_translate_c.rs` |
| `src/collections/StaticHashMap.cp` | `src/collections/StaticHashMap.rs` |
| `src/collections/array_list.cp` | `src/collections/array_list.rs`, `src/sql/postgres/protocol/ArrayList.rs` |
| `src/collections/baby_list.cp` | _需按子系统/函数手工定位_ |
| `src/collections/bit_set.cp` | `src/collections/bit_set.rs` |
| `src/collections/bounded_array.cp` | `src/bun_core/bounded_array.rs` |
| `src/collections/collections.cp` | _需按子系统/函数手工定位_ |
| `src/collections/comptime_string_map.cp` | `src/collections/comptime_string_map.rs` |
| `src/collections/hive_array.cp` | `src/collections/hive_array.rs` |
| `src/collections/identity_context.cp` | `src/collections/identity_context.rs` |
| `src/collections/linear_fifo.cp` | `src/collections/linear_fifo.rs` |
| `src/collections/multi_array_list.cp` | `src/collections/multi_array_list.rs` |
| `src/collections/pool.cp` | `src/collections/pool.rs` |
| `src/crash_handler/CPUFeatures.cp` | `src/crash_handler/CPUFeatures.rs` |
| `src/crash_handler/crash_handler.cp` | _需按子系统/函数手工定位_ |
| `src/crash_handler/handle_oom.cp` | `src/crash_handler/handle_oom.rs` |
| `src/csrf/csrf.cp` | _需按子系统/函数手工定位_ |
| `src/css/compat.cp` | `src/css/compat.rs` |
| `src/css/context.cp` | `src/css/context.rs`, `src/options_types/context.rs`, `src/uws_sys/quic/Context.rs` |
| `src/css/css_modules.cp` | `src/css/css_modules.rs`, `src/css/properties/css_modules.rs` |
| `src/css/css_parser.cp` | `src/css/css_parser.rs` |
| `src/css/css_parser.d.cp` | _需按子系统/函数手工定位_ |
| `src/css/declaration.cp` | `src/css/declaration.rs` |
| `src/css/dependencies.cp` | `src/css/dependencies.rs` |
| `src/css/error.cp` | `src/css/error.rs`, `src/s3_signing/error.rs`, `src/sys/Error.rs` |
| `src/css/generics.cp` | `src/css/generics.rs` |
| `src/css/logical.cp` | `src/css/logical.rs` |
| `src/css/media_query.cp` | `src/css/media_query.rs` |
| `src/css/prefixes.cp` | `src/css/prefixes.rs` |
| `src/css/printer.cp` | `src/css/printer.rs` |
| `src/css/properties/align.cp` | `src/css/properties/align.rs` |
| `src/css/properties/animation.cp` | `src/css/properties/animation.rs` |
| `src/css/properties/background.cp` | `src/css/properties/background.rs` |
| `src/css/properties/border.cp` | `src/css/properties/border.rs` |
| `src/css/properties/border_image.cp` | `src/css/properties/border_image.rs` |
| `src/css/properties/border_radius.cp` | `src/css/properties/border_radius.rs` |
| `src/css/properties/box_shadow.cp` | `src/css/properties/box_shadow.rs` |
| `src/css/properties/contain.cp` | `src/css/properties/contain.rs` |
| `src/css/properties/css_modules.cp` | `src/css/css_modules.rs`, `src/css/properties/css_modules.rs` |
| `src/css/properties/custom.cp` | `src/css/properties/custom.rs` |
| `src/css/properties/display.cp` | `src/css/properties/display.rs` |
| `src/css/properties/effects.cp` | `src/css/properties/effects.rs` |
| `src/css/properties/flex.cp` | `src/css/properties/flex.rs` |
| `src/css/properties/font.cp` | `src/css/properties/font.rs` |
| `src/css/properties/grid.cp` | `src/css/properties/grid.rs` |
| `src/css/properties/list.cp` | `src/css/properties/list.rs` |
| `src/css/properties/margin_padding.cp` | `src/css/properties/margin_padding.rs` |
| `src/css/properties/masking.cp` | `src/css/properties/masking.rs` |
| `src/css/properties/outline.cp` | `src/css/properties/outline.rs` |
| `src/css/properties/overflow.cp` | `src/css/properties/overflow.rs` |
| `src/css/properties/position.cp` | `src/css/properties/position.rs`, `src/css/values/position.rs` |
| `src/css/properties/prefix_handler.cp` | `src/css/properties/prefix_handler.rs` |
| `src/css/properties/properties.cp` | _需按子系统/函数手工定位_ |
| `src/css/properties/properties_generated.cp` | `src/css/properties/properties_generated.rs` |
| `src/css/properties/properties_impl.cp` | `src/css/properties/properties_impl.rs` |
| `src/css/properties/shape.cp` | `src/css/properties/shape.rs`, `src/dispatch/tests/shape.rs` |
| `src/css/properties/size.cp` | `src/css/properties/size.rs`, `src/css/values/size.rs` |
| `src/css/properties/svg.cp` | `src/css/properties/svg.rs` |
| `src/css/properties/text.cp` | `src/css/properties/text.rs` |
| `src/css/properties/transform.cp` | `src/css/properties/transform.rs` |
| `src/css/properties/transition.cp` | `src/css/properties/transition.rs` |
| `src/css/properties/ui.cp` | `src/css/properties/ui.rs` |
| `src/css/rules/container.cp` | `src/css/rules/container.rs` |
| `src/css/rules/counter_style.cp` | `src/css/rules/counter_style.rs` |
| `src/css/rules/custom_media.cp` | `src/css/rules/custom_media.rs` |
| `src/css/rules/document.cp` | `src/css/rules/document.rs` |
| `src/css/rules/font_face.cp` | `src/css/rules/font_face.rs` |
| `src/css/rules/font_palette_values.cp` | `src/css/rules/font_palette_values.rs` |
| `src/css/rules/import.cp` | `src/css/rules/import.rs` |
| `src/css/rules/keyframes.cp` | `src/css/rules/keyframes.rs` |
| `src/css/rules/layer.cp` | `src/css/rules/layer.rs` |
| `src/css/rules/media.cp` | `src/css/rules/media.rs` |
| `src/css/rules/namespace.cp` | `src/css/rules/namespace.rs` |
| `src/css/rules/nesting.cp` | `src/css/rules/nesting.rs` |
| `src/css/rules/page.cp` | `src/css/rules/page.rs` |
| `src/css/rules/property.cp` | `src/css/rules/property.rs` |
| `src/css/rules/rules.cp` | _需按子系统/函数手工定位_ |
| `src/css/rules/scope.cp` | `src/ast/scope.rs`, `src/css/rules/scope.rs` |
| `src/css/rules/starting_style.cp` | `src/css/rules/starting_style.rs` |
| `src/css/rules/style.cp` | `src/css/rules/style.rs` |
| `src/css/rules/supports.cp` | `src/css/rules/supports.rs` |
| `src/css/rules/tailwind.cp` | `src/css/rules/tailwind.rs` |
| `src/css/rules/unknown.cp` | `src/css/rules/unknown.rs` |
| `src/css/rules/viewport.cp` | `src/css/rules/viewport.rs` |
| `src/css/selectors/builder.cp` | `src/css/selectors/builder.rs` |
| `src/css/selectors/parser.cp` | `src/css/selectors/parser.rs`, `src/js_parser/parser.rs`, `src/md/parser.rs` |
| `src/css/selectors/selector.cp` | `src/css/selectors/selector.rs` |
| `src/css/small_list.cp` | `src/css/small_list.rs` |
| `src/css/sourcemap.cp` | `src/css/sourcemap.rs` |
| `src/css/targets.cp` | `src/css/targets.rs` |
| `src/css/values/alpha.cp` | `src/css/values/alpha.rs` |
| `src/css/values/angle.cp` | `src/css/values/angle.rs` |
| `src/css/values/calc.cp` | `src/css/values/calc.rs` |
| `src/css/values/color.cp` | `src/css/values/color.rs` |
| `src/css/values/color_generated.cp` | `src/css/values/color_generated.rs` |
| `src/css/values/css_string.cp` | `src/css/values/css_string.rs` |
| `src/css/values/easing.cp` | `src/css/values/easing.rs` |
| `src/css/values/gradient.cp` | `src/css/values/gradient.rs` |
| `src/css/values/ident.cp` | `src/css/values/ident.rs` |
| `src/css/values/image.cp` | `src/css/values/image.rs`, `src/runtime/image/Image.rs` |
| `src/css/values/length.cp` | `src/css/values/length.rs` |
| `src/css/values/number.cp` | `src/css/values/number.rs` |
| `src/css/values/percentage.cp` | `src/css/values/percentage.rs` |
| `src/css/values/position.cp` | `src/css/properties/position.rs`, `src/css/values/position.rs` |
| `src/css/values/ratio.cp` | `src/css/values/ratio.rs` |
| `src/css/values/rect.cp` | `src/css/values/rect.rs` |
| `src/css/values/resolution.cp` | `src/css/values/resolution.rs`, `src/install/resolution.rs` |
| `src/css/values/size.cp` | `src/css/properties/size.rs`, `src/css/values/size.rs` |
| `src/css/values/syntax.cp` | `src/css/values/syntax.rs` |
| `src/css/values/time.cp` | `src/css/values/time.rs` |
| `src/css/values/url.cp` | `src/css/values/url.rs`, `src/jsc/URL.rs` |
| `src/css/values/values.cp` | _需按子系统/函数手工定位_ |
| `src/css_jsc/color_js.cp` | `src/css_jsc/color_js.rs` |
| `src/css_jsc/css_internals.cp` | `src/css_jsc/css_internals.rs` |
| `src/css_jsc/error_jsc.cp` | `src/css_jsc/error_jsc.rs`, `src/runtime/webcore/s3/error_jsc.rs`, `src/sql_jsc/postgres/error_jsc.rs` (+1) |
| `src/dns/dns.cp` | `src/runtime/dns_jsc/dns.rs` |
| `src/dotenv/env_loader.cp` | `src/dotenv/env_loader.rs` |
| `src/errno/darwin_errno.cp` | `src/errno/darwin_errno.rs` |
| `src/errno/freebsd_errno.cp` | `src/errno/freebsd_errno.rs` |
| `src/errno/linux_errno.cp` | `src/errno/linux_errno.rs` |
| `src/errno/windows_errno.cp` | `src/errno/windows_errno.rs` |
| `src/event_loop/AnyEventLoop.cp` | `src/event_loop/AnyEventLoop.rs` |
| `src/event_loop/AnyTask.cp` | `src/event_loop/AnyTask.rs` |
| `src/event_loop/AnyTaskWithExtraContext.cp` | `src/event_loop/AnyTaskWithExtraContext.rs` |
| `src/event_loop/AutoFlusher.cp` | `src/event_loop/AutoFlusher.rs` |
| `src/event_loop/ConcurrentTask.cp` | `src/event_loop/ConcurrentTask.rs` |
| `src/event_loop/DeferredTaskQueue.cp` | `src/event_loop/DeferredTaskQueue.rs` |
| `src/event_loop/EventLoopTimer.cp` | `src/event_loop/EventLoopTimer.rs` |
| `src/event_loop/ManagedTask.cp` | `src/event_loop/ManagedTask.rs` |
| `src/event_loop/MiniEventLoop.cp` | `src/event_loop/MiniEventLoop.rs` |
| `src/event_loop/SpawnSyncEventLoop.cp` | `src/event_loop/SpawnSyncEventLoop.rs` |
| `src/exe_format/elf.cp` | `src/exe_format/elf.rs` |
| `src/exe_format/macho.cp` | `src/exe_format/macho.rs` |
| `src/exe_format/pe.cp` | `src/exe_format/pe.rs` |
| `src/glob/GlobWalker.cp` | `src/glob/GlobWalker.rs` |
| `src/glob/glob.cp` | `src/runtime/api/glob.rs` |
| `src/glob/matcher.cp` | `src/glob/matcher.rs` |
| `src/highway/highway.cp` | _需按子系统/函数手工定位_ |
| `src/http/AsyncHTTP.cp` | `src/http/AsyncHTTP.rs` |
| `src/http/CertificateInfo.cp` | `src/http/CertificateInfo.rs` |
| `src/http/Decompressor.cp` | `src/http/Decompressor.rs` |
| `src/http/H2Client.cp` | `src/http/H2Client.rs` |
| `src/http/H2FrameParser.cp` | `src/runtime/api/bun/h2_frame_parser.rs` |
| `src/http/H3Client.cp` | `src/http/H3Client.rs` |
| `src/http/HTTPCertError.cp` | `src/http/HTTPCertError.rs` |
| `src/http/HTTPContext.cp` | `src/http/HTTPContext.rs` |
| `src/http/HTTPRequestBody.cp` | `src/http/HTTPRequestBody.rs` |
| `src/http/HTTPThread.cp` | `src/http/HTTPThread.rs` |
| `src/http/HeaderBuilder.cp` | `src/http/HeaderBuilder.rs` |
| `src/http/HeaderValueIterator.cp` | `src/http/HeaderValueIterator.rs` |
| `src/http/Headers.cp` | `src/http/Headers.rs` |
| `src/http/InitError.cp` | `src/http/InitError.rs` |
| `src/http/InternalState.cp` | `src/http/InternalState.rs` |
| `src/http/ProxyTunnel.cp` | `src/http/ProxyTunnel.rs` |
| `src/http/SendFile.cp` | `src/http/SendFile.rs` |
| `src/http/Signals.cp` | `src/http/Signals.rs` |
| `src/http/ThreadSafeStreamBuffer.cp` | `src/http/ThreadSafeStreamBuffer.rs` |
| `src/http/h2_client/ClientSession.cp` | `src/http/h2_client/ClientSession.rs`, `src/http/h3_client/ClientSession.rs` |
| `src/http/h2_client/PendingConnect.cp` | `src/http/h2_client/PendingConnect.rs`, `src/http/h3_client/PendingConnect.rs`, `src/uws_sys/quic/PendingConnect.rs` |
| `src/http/h2_client/Stream.cp` | `src/http/h2_client/Stream.rs`, `src/http/h3_client/Stream.rs`, `src/uws_sys/quic/Stream.rs` |
| `src/http/h2_client/dispatch.cp` | `src/http/h2_client/dispatch.rs`, `src/runtime/dispatch.rs` |
| `src/http/h2_client/encode.cp` | `src/http/h2_client/encode.rs`, `src/http/h3_client/encode.rs` |
| `src/http/h3_client/AltSvc.cp` | `src/http/h3_client/AltSvc.rs` |
| `src/http/h3_client/ClientContext.cp` | `src/http/h3_client/ClientContext.rs` |
| `src/http/h3_client/ClientSession.cp` | `src/http/h2_client/ClientSession.rs`, `src/http/h3_client/ClientSession.rs` |
| `src/http/h3_client/PendingConnect.cp` | `src/http/h2_client/PendingConnect.rs`, `src/http/h3_client/PendingConnect.rs`, `src/uws_sys/quic/PendingConnect.rs` |
| `src/http/h3_client/Stream.cp` | `src/http/h2_client/Stream.rs`, `src/http/h3_client/Stream.rs`, `src/uws_sys/quic/Stream.rs` |
| `src/http/h3_client/callbacks.cp` | `src/http/h3_client/callbacks.rs` |
| `src/http/h3_client/encode.cp` | `src/http/h2_client/encode.rs`, `src/http/h3_client/encode.rs` |
| `src/http/http.cp` | _需按子系统/函数手工定位_ |
| `src/http/lshpack.cp` | `src/http/lshpack.rs` |
| `src/http/websocket.cp` | `src/http/websocket.rs`, `src/uws_sys/WebSocket.rs` |
| `src/http/websocket_http_client.cp` | `src/http/websocket_http_client.rs` |
| `src/http/zlib.cp` | `src/http/zlib.rs` |
| `src/http_jsc/fetch_enums_jsc.cp` | `src/http_jsc/fetch_enums_jsc.rs` |
| `src/http_jsc/headers_jsc.cp` | `src/http_jsc/headers_jsc.rs` |
| `src/http_jsc/method_jsc.cp` | `src/http_jsc/method_jsc.rs` |
| `src/http_jsc/websocket_client.cp` | `src/http_jsc/websocket_client.rs` |
| `src/http_jsc/websocket_client/CppWebSocket.cp` | `src/http_jsc/websocket_client/CppWebSocket.rs` |
| `src/http_jsc/websocket_client/WebSocketDeflate.cp` | `src/http_jsc/websocket_client/WebSocketDeflate.rs` |
| `src/http_jsc/websocket_client/WebSocketProxy.cp` | `src/http_jsc/websocket_client/WebSocketProxy.rs` |
| `src/http_jsc/websocket_client/WebSocketProxyTunnel.cp` | `src/http_jsc/websocket_client/WebSocketProxyTunnel.rs` |
| `src/http_jsc/websocket_client/WebSocketUpgradeClient.cp` | `src/http_jsc/websocket_client/WebSocketUpgradeClient.rs` |
| `src/http_types/ETag.cp` | `src/http_types/ETag.rs` |
| `src/http_types/Encoding.cp` | `src/http_types/Encoding.rs`, `src/runtime/webcore/encoding.rs` |
| `src/http_types/FetchCacheMode.cp` | `src/http_types/FetchCacheMode.rs` |
| `src/http_types/FetchRedirect.cp` | `src/http_types/FetchRedirect.rs` |
| `src/http_types/FetchRequestMode.cp` | `src/http_types/FetchRequestMode.rs` |
| `src/http_types/Method.cp` | `src/http_types/Method.rs` |
| `src/http_types/MimeType.cp` | `src/http_types/MimeType.rs` |
| `src/http_types/URLPath.cp` | `src/http_types/URLPath.rs` |
| `src/http_types/mime_type_list_enum.cp` | `src/http_types/mime_type_list_enum.rs` |
| `src/ini/ini.cp` | _需按子系统/函数手工定位_ |
| `src/install/ConfigVersion.cp` | `src/install/ConfigVersion.rs` |
| `src/install/ExternalSlice.cp` | `src/install/ExternalSlice.rs` |
| `src/install/NetworkTask.cp` | `src/install/NetworkTask.rs` |
| `src/install/PackageInstall.cp` | `src/install/PackageInstall.rs` |
| `src/install/PackageInstaller.cp` | `src/install/PackageInstaller.rs` |
| `src/install/PackageManager.cp` | `src/install/PackageManager.rs` |
| `src/install/PackageManager/CommandLineArguments.cp` | `src/install/PackageManager/CommandLineArguments.rs` |
| `src/install/PackageManager/PackageJSONEditor.cp` | `src/install/PackageManager/PackageJSONEditor.rs` |
| `src/install/PackageManager/PackageManagerDirectories.cp` | `src/install/PackageManager/PackageManagerDirectories.rs` |
| `src/install/PackageManager/PackageManagerEnqueue.cp` | `src/install/PackageManager/PackageManagerEnqueue.rs` |
| `src/install/PackageManager/PackageManagerLifecycle.cp` | `src/install/PackageManager/PackageManagerLifecycle.rs` |
| `src/install/PackageManager/PackageManagerOptions.cp` | `src/install/PackageManager/PackageManagerOptions.rs` |
| `src/install/PackageManager/PackageManagerResolution.cp` | `src/install/PackageManager/PackageManagerResolution.rs` |
| `src/install/PackageManager/PopulateManifestCache.cp` | `src/install/PackageManager/PopulateManifestCache.rs` |
| `src/install/PackageManager/ProgressStrings.cp` | `src/install/PackageManager/ProgressStrings.rs` |
| `src/install/PackageManager/UpdateRequest.cp` | `src/install/PackageManager/UpdateRequest.rs` |
| `src/install/PackageManager/WorkspacePackageJSONCache.cp` | `src/install/PackageManager/WorkspacePackageJSONCache.rs` |
| `src/install/PackageManager/install_with_manager.cp` | `src/install/PackageManager/install_with_manager.rs` |
| `src/install/PackageManager/patchPackage.cp` | `src/install/PackageManager/patchPackage.rs` |
| `src/install/PackageManager/processDependencyList.cp` | `src/install/PackageManager/processDependencyList.rs` |
| `src/install/PackageManager/runTasks.cp` | `src/install/PackageManager/runTasks.rs` |
| `src/install/PackageManager/security_scanner.cp` | `src/install/PackageManager/security_scanner.rs` |
| `src/install/PackageManager/updatePackageJSONAndInstall.cp` | `src/install/PackageManager/updatePackageJSONAndInstall.rs` |
| `src/install/PackageManagerTask.cp` | `src/install/PackageManagerTask.rs` |
| `src/install/PackageManifestMap.cp` | `src/install/PackageManifestMap.rs` |
| `src/install/PnpmMatcher.cp` | _需按子系统/函数手工定位_ |
| `src/install/TarballStream.cp` | `src/install/TarballStream.rs` |
| `src/install/bin.cp` | `src/install/bin.rs` |
| `src/install/dependency.cp` | `src/install/dependency.rs` |
| `src/install/extract_tarball.cp` | `src/install/extract_tarball.rs` |
| `src/install/hoisted_install.cp` | `src/install/hoisted_install.rs` |
| `src/install/hosted_git_info.cp` | `src/install/hosted_git_info.rs` |
| `src/install/install.cp` | _需按子系统/函数手工定位_ |
| `src/install/integrity.cp` | `src/install/integrity.rs` |
| `src/install/isolated_install.cp` | `src/install/isolated_install.rs` |
| `src/install/isolated_install/FileCloner.cp` | `src/install/isolated_install/FileCloner.rs` |
| `src/install/isolated_install/FileCopier.cp` | `src/install/isolated_install/FileCopier.rs` |
| `src/install/isolated_install/Hardlinker.cp` | `src/install/isolated_install/Hardlinker.rs` |
| `src/install/isolated_install/Installer.cp` | `src/install/isolated_install/Installer.rs` |
| `src/install/isolated_install/Store.cp` | `src/install/isolated_install/Store.rs`, `src/runtime/webcore/blob/Store.rs` |
| `src/install/isolated_install/Symlinker.cp` | `src/install/isolated_install/Symlinker.rs` |
| `src/install/lifecycle_script_runner.cp` | `src/install/lifecycle_script_runner.rs` |
| `src/install/lockfile.cp` | `src/install/lockfile.rs` |
| `src/install/lockfile/Buffers.cp` | `src/install/lockfile/Buffers.rs` |
| `src/install/lockfile/CatalogMap.cp` | `src/install/lockfile/CatalogMap.rs` |
| `src/install/lockfile/OverrideMap.cp` | `src/install/lockfile/OverrideMap.rs` |
| `src/install/lockfile/Package.cp` | `src/install/lockfile/Package.rs` |
| `src/install/lockfile/Package/Meta.cp` | `src/install/lockfile/Package/Meta.rs`, `src/ptr/meta.rs` |
| `src/install/lockfile/Package/Scripts.cp` | `src/install/lockfile/Package/Scripts.rs` |
| `src/install/lockfile/Package/WorkspaceMap.cp` | `src/install/lockfile/Package/WorkspaceMap.rs` |
| `src/install/lockfile/Tree.cp` | `src/install/lockfile/Tree.rs` |
| `src/install/lockfile/bun.lock.cp` | `src/install/lockfile/bun.lock.rs` |
| `src/install/lockfile/bun.lockb.cp` | `src/install/lockfile/bun.lockb.rs` |
| `src/install/lockfile/lockfile_json_stringify_for_debugging.cp` | `src/install/lockfile/lockfile_json_stringify_for_debugging.rs` |
| `src/install/lockfile/printer/Yarn.cp` | `src/install/lockfile/printer/Yarn.rs`, `src/install/yarn.rs` |
| `src/install/lockfile/printer/tree_printer.cp` | `src/install/lockfile/printer/tree_printer.rs` |
| `src/install/migration.cp` | `src/install/migration.rs` |
| `src/install/npm.cp` | `src/install/npm.rs` |
| `src/install/padding_checker.cp` | `src/install/padding_checker.rs` |
| `src/install/patch_install.cp` | `src/install/patch_install.rs` |
| `src/install/pnpm.cp` | `src/install/pnpm.rs` |
| `src/install/postinstall_optimizer.cp` | `src/install/postinstall_optimizer.rs` |
| `src/install/repository.cp` | `src/install/repository.rs` |
| `src/install/resolution.cp` | `src/css/values/resolution.rs`, `src/install/resolution.rs` |
| `src/install/resolvers/folder_resolver.cp` | `src/install/resolvers/folder_resolver.rs` |
| `src/install/versioned_url.cp` | `src/install/versioned_url.rs` |
| `src/install/windows-shim/BinLinkingShim.cp` | `src/install/windows-shim/BinLinkingShim.rs` |
| `src/install/windows-shim/bun_shim_impl.cp` | `src/install/windows-shim/bun_shim_impl.rs` |
| `src/install/yarn.cp` | `src/install/lockfile/printer/Yarn.rs`, `src/install/yarn.rs` |
| `src/install_jsc/dependency_jsc.cp` | `src/install_jsc/dependency_jsc.rs` |
| `src/install_jsc/hosted_git_info_jsc.cp` | `src/install_jsc/hosted_git_info_jsc.rs` |
| `src/install_jsc/ini_jsc.cp` | `src/install_jsc/ini_jsc.rs` |
| `src/install_jsc/install_binding.cp` | `src/install_jsc/install_binding.rs` |
| `src/install_jsc/npm_jsc.cp` | `src/install_jsc/npm_jsc.rs` |
| `src/install_jsc/update_request_jsc.cp` | `src/install_jsc/update_request_jsc.rs` |
| `src/install_types/ExternalString.cp` | _需按子系统/函数手工定位_ |
| `src/install_types/NodeLinker.cp` | `src/install_types/NodeLinker.rs` |
| `src/install_types/SemverString.cp` | _需按子系统/函数手工定位_ |
| `src/install_types/SlicedString.cp` | _需按子系统/函数手工定位_ |
| `src/interchange/interchange.cp` | _需按子系统/函数手工定位_ |
| `src/interchange/json.cp` | `src/parsers/json.rs`, `src/sql_jsc/postgres/types/json.rs` |
| `src/interchange/json5.cp` | `src/parsers/json5.rs` |
| `src/interchange/toml.cp` | `src/parsers/toml.rs` |
| `src/interchange/toml/lexer.cp` | `src/js_parser/lexer.rs`, `src/parsers/toml/lexer.rs` |
| `src/interchange/yaml.cp` | `src/parsers/yaml.rs` |
| `src/io/MaxBuf.cp` | `src/io/MaxBuf.rs` |
| `src/io/PipeReader.cp` | `src/io/PipeReader.rs` |
| `src/io/PipeWriter.cp` | `src/io/PipeWriter.rs` |
| `src/io/heap.cp` | `src/bun_core/heap.rs`, `src/io/heap.rs` |
| `src/io/io.cp` | `src/runtime/shell/IO.rs` |
| `src/io/openForWriting.cp` | `src/io/openForWriting.rs` |
| `src/io/pipes.cp` | `src/io/pipes.rs` |
| `src/io/source.cp` | `src/io/source.rs` |
| `src/js_parser/ast/ASTMemoryAllocator.cp` | `src/ast/ast_memory_allocator.rs` |
| `src/js_parser/ast/Ast.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/B.cp` | `src/ast/b.rs` |
| `src/js_parser/ast/Binding.cp` | `src/ast/binding.rs` |
| `src/js_parser/ast/BundledAst.cp` | `src/bundler/bundled_ast.rs` |
| `src/js_parser/ast/CharFreq.cp` | `src/ast/char_freq.rs` |
| `src/js_parser/ast/ConvertESMExportsForHmr.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/E.cp` | `src/ast/e.rs` |
| `src/js_parser/ast/Expr.cp` | `src/ast/expr.rs` |
| `src/js_parser/ast/G.cp` | `src/ast/g.rs` |
| `src/js_parser/ast/ImportScanner.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/KnownGlobal.cp` | `src/ast/known_global.rs` |
| `src/js_parser/ast/NewStore.cp` | `src/ast/new_store.rs` |
| `src/js_parser/ast/Op.cp` | `src/ast/op.rs` |
| `src/js_parser/ast/P.cp` | `src/js_parser/p.rs` |
| `src/js_parser/ast/Parser.cp` | `src/css/selectors/parser.rs`, `src/js_parser/parser.rs`, `src/md/parser.rs` |
| `src/js_parser/ast/S.cp` | `src/ast/s.rs` |
| `src/js_parser/ast/Scope.cp` | `src/ast/scope.rs`, `src/css/rules/scope.rs` |
| `src/js_parser/ast/ServerComponentBoundary.cp` | `src/ast/server_component_boundary.rs` |
| `src/js_parser/ast/SideEffects.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/Stmt.cp` | `src/ast/stmt.rs`, `src/runtime/shell/states/Stmt.rs` |
| `src/js_parser/ast/Symbol.cp` | `src/ast/symbol.rs` |
| `src/js_parser/ast/TS.cp` | `src/ast/ts.rs` |
| `src/js_parser/ast/TypeScript.cp` | `src/js_parser/typescript.rs` |
| `src/js_parser/ast/UseDirective.cp` | `src/ast/use_directive.rs` |
| `src/js_parser/ast/base.cp` | `src/ast/base.rs`, `src/runtime/shell/states/Base.rs` |
| `src/js_parser/ast/foldStringAddition.cp` | `src/ast/fold_string_addition.rs` |
| `src/js_parser/ast/lowerDecorators.cp` | `src/js_parser/lower/lower_decorators.rs` |
| `src/js_parser/ast/maybe.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/parse.cp` | `src/shell_parser/parse.rs`, `src/sql/postgres/protocol/Parse.rs` |
| `src/js_parser/ast/parseFn.cp` | `src/js_parser/parse/parse_fn.rs` |
| `src/js_parser/ast/parseImportExport.cp` | `src/js_parser/parse/parse_import_export.rs` |
| `src/js_parser/ast/parseJSXElement.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/parsePrefix.cp` | `src/js_parser/parse/parse_prefix.rs` |
| `src/js_parser/ast/parseProperty.cp` | `src/js_parser/parse/parse_property.rs` |
| `src/js_parser/ast/parseStmt.cp` | `src/js_parser/parse/parse_stmt.rs` |
| `src/js_parser/ast/parseSuffix.cp` | `src/js_parser/parse/parse_suffix.rs` |
| `src/js_parser/ast/parseTypescript.cp` | `src/js_parser/parse/parse_typescript.rs` |
| `src/js_parser/ast/repl_transforms.cp` | `src/js_parser/repl_transforms.rs` |
| `src/js_parser/ast/skipTypescript.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/symbols.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/visit.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/visitBinaryExpression.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/ast/visitExpr.cp` | `src/js_parser/visit/visit_expr.rs` |
| `src/js_parser/ast/visitStmt.cp` | `src/js_parser/visit/visit_stmt.rs` |
| `src/js_parser/js_parser.cp` | _需按子系统/函数手工定位_ |
| `src/js_parser/lexer.cp` | `src/js_parser/lexer.rs`, `src/parsers/toml/lexer.rs` |
| `src/js_parser/lexer/identifier.cp` | `src/bun_core/string/identifier.rs` |
| `src/js_parser/lexer_tables.cp` | `src/ast/lexer_tables.rs` |
| `src/js_parser/parser.cp` | `src/css/selectors/parser.rs`, `src/js_parser/parser.rs`, `src/md/parser.rs` |
| `src/js_parser/runtime.cp` | `src/ast/runtime.rs` |
| `src/js_parser_jsc/Macro.cp` | `src/js_parser_jsc/Macro.rs` |
| `src/js_parser_jsc/expr_jsc.cp` | `src/js_parser_jsc/expr_jsc.rs` |
| `src/js_printer/js_printer.cp` | _需按子系统/函数手工定位_ |
| `src/js_printer/renamer.cp` | `src/js_printer/renamer.rs` |
| `src/jsc/AbortSignal.cp` | `src/jsc/AbortSignal.rs` |
| `src/jsc/AnyPromise.cp` | `src/jsc/AnyPromise.rs` |
| `src/jsc/AsyncModule.cp` | `src/jsc/AsyncModule.rs` |
| `src/jsc/BuildMessage.cp` | `src/jsc/BuildMessage.rs` |
| `src/jsc/BunCPUProfiler.cp` | `src/jsc/BunCPUProfiler.rs` |
| `src/jsc/BunHeapProfiler.cp` | `src/jsc/BunHeapProfiler.rs` |
| `src/jsc/CachedBytecode.cp` | `src/jsc/CachedBytecode.rs` |
| `src/jsc/CallFrame.cp` | `src/jsc/CallFrame.rs` |
| `src/jsc/CommonAbortReason.cp` | `src/jsc/CommonAbortReason.rs` |
| `src/jsc/CommonStrings.cp` | `src/jsc/CommonStrings.rs` |
| `src/jsc/ConcurrentPromiseTask.cp` | `src/jsc/ConcurrentPromiseTask.rs` |
| `src/jsc/ConsoleObject.cp` | `src/jsc/ConsoleObject.rs` |
| `src/jsc/Counters.cp` | `src/jsc/Counters.rs` |
| `src/jsc/CppTask.cp` | `src/jsc/CppTask.rs` |
| `src/jsc/CustomGetterSetter.cp` | `src/jsc/CustomGetterSetter.rs` |
| `src/jsc/DOMFormData.cp` | `src/jsc/DOMFormData.rs` |
| `src/jsc/DOMURL.cp` | `src/jsc/DOMURL.rs` |
| `src/jsc/Debugger.cp` | `src/jsc/Debugger.rs` |
| `src/jsc/DecodedJSValue.cp` | `src/jsc/DecodedJSValue.rs` |
| `src/jsc/DeferredError.cp` | `src/jsc/DeferredError.rs` |
| `src/jsc/DeprecatedStrong.cp` | `src/jsc/DeprecatedStrong.rs` |
| `src/jsc/ErrorCode.cp` | `src/jsc/ErrorCode.rs` |
| `src/jsc/Errorable.cp` | `src/jsc/Errorable.rs` |
| `src/jsc/EventLoopHandle.cp` | `src/jsc/EventLoopHandle.rs` |
| `src/jsc/EventType.cp` | `src/jsc/EventType.rs` |
| `src/jsc/Exception.cp` | `src/jsc/Exception.rs` |
| `src/jsc/FFI.cp` | `src/jsc/FFI.rs` |
| `src/jsc/FetchHeaders.cp` | `src/jsc/FetchHeaders.rs` |
| `src/jsc/GarbageCollectionController.cp` | `src/jsc/GarbageCollectionController.rs` |
| `src/jsc/GetterSetter.cp` | `src/jsc/GetterSetter.rs` |
| `src/jsc/HTTPServerAgent.cp` | `src/jsc/HTTPServerAgent.rs` |
| `src/jsc/JSArray.cp` | `src/jsc/JSArray.rs` |
| `src/jsc/JSArrayIterator.cp` | `src/jsc/JSArrayIterator.rs` |
| `src/jsc/JSBigInt.cp` | `src/jsc/JSBigInt.rs` |
| `src/jsc/JSCScheduler.cp` | `src/jsc/JSCScheduler.rs` |
| `src/jsc/JSCell.cp` | `src/jsc/JSCell.rs` |
| `src/jsc/JSErrorCode.cp` | `src/jsc/JSErrorCode.rs` |
| `src/jsc/JSFunction.cp` | `src/jsc/JSFunction.rs` |
| `src/jsc/JSGlobalObject.cp` | `src/jsc/JSGlobalObject.rs` |
| `src/jsc/JSInternalPromise.cp` | `src/jsc/JSInternalPromise.rs` |
| `src/jsc/JSMap.cp` | `src/jsc/JSMap.rs` |
| `src/jsc/JSModuleLoader.cp` | `src/jsc/JSModuleLoader.rs` |
| `src/jsc/JSONLineBuffer.cp` | `src/jsc/JSONLineBuffer.rs` |
| `src/jsc/JSObject.cp` | `src/jsc/JSObject.rs` |
| `src/jsc/JSPromise.cp` | `src/jsc/JSPromise.rs` |
| `src/jsc/JSPromiseRejectionOperation.cp` | `src/jsc/JSPromiseRejectionOperation.rs` |
| `src/jsc/JSPropertyIterator.cp` | `src/jsc/JSPropertyIterator.rs` |
| `src/jsc/JSRef.cp` | `src/jsc/JSRef.rs` |
| `src/jsc/JSRuntimeType.cp` | `src/jsc/JSRuntimeType.rs` |
| `src/jsc/JSSecrets.cp` | `src/jsc/JSSecrets.rs` |
| `src/jsc/JSString.cp` | `src/jsc/JSString.rs` |
| `src/jsc/JSType.cp` | `src/jsc/JSType.rs` |
| `src/jsc/JSUint8Array.cp` | `src/jsc/JSUint8Array.rs` |
| `src/jsc/JSValue.cp` | `src/jsc/JSValue.rs` |
| `src/jsc/MarkedArgumentBuffer.cp` | `src/jsc/MarkedArgumentBuffer.rs` |
| `src/jsc/ModuleLoader.cp` | `src/jsc/ModuleLoader.rs` |
| `src/jsc/NodeModuleModule.cp` | `src/jsc/NodeModuleModule.rs` |
| `src/jsc/PosixSignalHandle.cp` | `src/jsc/PosixSignalHandle.rs` |
| `src/jsc/ProcessAutoKiller.cp` | `src/jsc/ProcessAutoKiller.rs` |
| `src/jsc/RefString.cp` | `src/jsc/RefString.rs` |
| `src/jsc/RegularExpression.cp` | `src/jsc/RegularExpression.rs` |
| `src/jsc/ResolveMessage.cp` | `src/jsc/ResolveMessage.rs` |
| `src/jsc/ResolvedSource.cp` | `src/jsc/ResolvedSource.rs` |
| `src/jsc/RuntimeTranspilerCache.cp` | `src/jsc/RuntimeTranspilerCache.rs` |
| `src/jsc/RuntimeTranspilerStore.cp` | `src/jsc/RuntimeTranspilerStore.rs` |
| `src/jsc/SavedSourceMap.cp` | `src/jsc/SavedSourceMap.rs` |
| `src/jsc/ScriptExecutionStatus.cp` | `src/jsc/ScriptExecutionStatus.rs` |
| `src/jsc/SourceProvider.cp` | `src/jsc/SourceProvider.rs`, `src/sourcemap_jsc/source_provider.rs` |
| `src/jsc/SourceType.cp` | `src/jsc/SourceType.rs` |
| `src/jsc/StringBuilder.cp` | `src/bun_core/string/StringBuilder.rs`, `src/jsc/StringBuilder.rs` |
| `src/jsc/Strong.cp` | `src/jsc/Strong.rs` |
| `src/jsc/SystemError.cp` | `src/jsc/SystemError.rs` |
| `src/jsc/Task.cp` | `src/jsc/Task.rs` |
| `src/jsc/TextCodec.cp` | `src/jsc/TextCodec.rs` |
| `src/jsc/TopExceptionScope.cp` | `src/jsc/TopExceptionScope.rs` |
| `src/jsc/URL.cp` | `src/css/values/url.rs`, `src/jsc/URL.rs` |
| `src/jsc/URLSearchParams.cp` | `src/jsc/URLSearchParams.rs` |
| `src/jsc/VM.cp` | `src/jsc/VM.rs` |
| `src/jsc/VirtualMachine.cp` | `src/jsc/VirtualMachine.rs` |
| `src/jsc/WTF.cp` | `src/bun_core/string/wtf.rs`, `src/bun_core/wtf.rs`, `src/jsc/WTF.rs` |
| `src/jsc/Weak.cp` | `src/jsc/Weak.rs` |
| `src/jsc/WorkTask.cp` | `src/jsc/WorkTask.rs` |
| `src/jsc/ZigErrorType.cp` | `src/jsc/ZigErrorType.rs` |
| `src/jsc/ZigException.cp` | `src/jsc/ZigException.rs` |
| `src/jsc/ZigStackFrame.cp` | `src/jsc/ZigStackFrame.rs` |
| `src/jsc/ZigStackFrameCode.cp` | `src/jsc/ZigStackFrameCode.rs` |
| `src/jsc/ZigStackFramePosition.cp` | `src/jsc/ZigStackFramePosition.rs` |
| `src/jsc/ZigStackTrace.cp` | `src/jsc/ZigStackTrace.rs` |
| `src/jsc/ZigString.cp` | `src/jsc/ZigString.rs` |
| `src/jsc/array_buffer.cp` | `src/jsc/array_buffer.rs` |
| `src/jsc/bindgen.cp` | `src/jsc/bindgen.rs` |
| `src/jsc/bindgen_test.cp` | `src/jsc/bindgen_test.rs` |
| `src/jsc/bindings/GeneratedBindings.cp` | `src/jsc/bindings/GeneratedBindings.rs` |
| `src/jsc/bindings/GeneratedJS2Native.cp` | `src/jsc/bindings/GeneratedJS2Native.rs`, `src/runtime/generated_js2native.rs` |
| `src/jsc/btjs.cp` | `src/jsc/btjs.rs` |
| `src/jsc/bun_string_jsc.cp` | `src/jsc/bun_string_jsc.rs` |
| `src/jsc/codegen.cp` | `src/jsc/codegen.rs` |
| `src/jsc/comptime_string_map_jsc.cp` | `src/jsc/comptime_string_map_jsc.rs` |
| `src/jsc/config.cp` | `src/jsc/config.rs` |
| `src/jsc/event_loop.cp` | `src/jsc/event_loop.rs` |
| `src/jsc/fmt_jsc.cp` | `src/jsc/fmt_jsc.rs` |
| `src/jsc/generated_classes_list.cp` | `src/jsc/generated_classes_list.rs` |
| `src/jsc/host_fn.cp` | `src/jsc/host_fn.rs` |
| `src/jsc/hot_reloader.cp` | `src/jsc/hot_reloader.rs` |
| `src/jsc/ipc.cp` | `src/jsc/ipc.rs` |
| `src/jsc/javascript_core_c_api.cp` | `src/jsc/javascript_core_c_api.rs` |
| `src/jsc/jsc.cp` | `src/sql_jsc/jsc.rs` |
| `src/jsc/rare_data.cp` | `src/jsc/rare_data.rs` |
| `src/jsc/resolve_path_jsc.cp` | `src/jsc/resolve_path_jsc.rs` |
| `src/jsc/resolver_jsc.cp` | `src/jsc/resolver_jsc.rs` |
| `src/jsc/sizes.cp` | `src/jsc/sizes.rs` |
| `src/jsc/static_export.cp` | `src/jsc/static_export.rs` |
| `src/jsc/uuid.cp` | `src/jsc/uuid.rs` |
| `src/jsc/virtual_machine_exports.cp` | `src/jsc/virtual_machine_exports.rs` |
| `src/jsc/web_worker.cp` | `src/jsc/web_worker.rs` |
| `src/jsc_stub.cp` | `src/jsc_stub.rs` |
| `src/libarchive/libarchive.cp` | _需按子系统/函数手工定位_ |
| `src/libarchive_sys/bindings.cp` | `src/libarchive_sys/bindings.rs` |
| `src/libdeflate_sys/libdeflate.cp` | `src/libdeflate_sys/libdeflate.rs` |
| `src/libuv_sys/libuv.cp` | `src/libuv_sys/libuv.rs` |
| `src/logger/logger.cp` | _需按子系统/函数手工定位_ |
| `src/logger_jsc/logger_jsc.cp` | _需按子系统/函数手工定位_ |
| `src/lolhtml_sys/lol_html.cp` | `src/lolhtml_sys/lol_html.rs` |
| `src/main.cp` | `src/install/windows-shim/main.rs` |
| `src/main_test.cp` | `src/main_test.rs` |
| `src/main_wasm.cp` | `src/main_wasm.rs` |
| `src/md/ansi_renderer.cp` | `src/md/ansi_renderer.rs` |
| `src/md/autolinks.cp` | `src/md/autolinks.rs` |
| `src/md/blocks.cp` | `src/md/blocks.rs` |
| `src/md/containers.cp` | `src/md/containers.rs` |
| `src/md/entity.cp` | `src/md/entity.rs` |
| `src/md/helpers.cp` | `src/md/helpers.rs` |
| `src/md/html_renderer.cp` | `src/md/html_renderer.rs` |
| `src/md/inlines.cp` | `src/md/inlines.rs` |
| `src/md/line_analysis.cp` | `src/md/line_analysis.rs` |
| `src/md/links.cp` | `src/md/links.rs` |
| `src/md/parser.cp` | `src/css/selectors/parser.rs`, `src/js_parser/parser.rs`, `src/md/parser.rs` |
| `src/md/ref_defs.cp` | `src/md/ref_defs.rs` |
| `src/md/render_blocks.cp` | `src/md/render_blocks.rs` |
| `src/md/root.cp` | `src/md/root.rs` |
| `src/md/types.cp` | `src/md/types.rs`, `src/runtime/node/types.rs` |
| `src/md/unicode.cp` | `src/bun_core/string/immutable/unicode.rs`, `src/md/unicode.rs` |
| `src/meta/bits.cp` | `src/meta/bits.rs` |
| `src/meta/meta.cp` | `src/install/lockfile/Package/Meta.rs`, `src/ptr/meta.rs` |
| `src/meta/tagged_union.cp` | `src/meta/tagged_union.rs` |
| `src/meta/traits.cp` | `src/meta/traits.rs` |
| `src/mimalloc_sys/mimalloc.cp` | `src/mimalloc_sys/mimalloc.rs` |
| `src/napi/napi.cp` | _需按子系统/函数手工定位_ |
| `src/options_types/BundleEnums.cp` | `src/options_types/bundle_enums.rs` |
| `src/options_types/CodeCoverageOptions.cp` | `src/options_types/code_coverage_options.rs` |
| `src/options_types/CommandTag.cp` | `src/options_types/command_tag.rs`, `src/sql/postgres/CommandTag.rs` |
| `src/options_types/CompileTarget.cp` | `src/options_types/compile_target.rs` |
| `src/options_types/Context.cp` | `src/css/context.rs`, `src/options_types/context.rs`, `src/uws_sys/quic/Context.rs` |
| `src/options_types/GlobalCache.cp` | `src/options_types/global_cache.rs` |
| `src/options_types/OfflineMode.cp` | `src/options_types/offline_mode.rs` |
| `src/options_types/import_record.cp` | `src/ast/import_record.rs` |
| `src/options_types/schema.cp` | `src/analytics/schema.rs`, `src/options_types/schema.rs` |
| `src/patch/patch.cp` | _需按子系统/函数手工定位_ |
| `src/patch_jsc/testing.cp` | `src/patch_jsc/testing.rs` |
| `src/paths/EnvPath.cp` | `src/paths/EnvPath.rs` |
| `src/paths/Path.cp` | `src/paths/Path.rs`, `src/runtime/node/path.rs` |
| `src/paths/path_buffer_pool.cp` | `src/paths/path_buffer_pool.rs` |
| `src/paths/paths.cp` | _需按子系统/函数手工定位_ |
| `src/paths/resolve_path.cp` | `src/paths/resolve_path.rs` |
| `src/perf/generated_perf_trace_events.cp` | `src/perf/generated_perf_trace_events.rs` |
| `src/perf/hw_timer.cp` | `src/perf/hw_timer.rs` |
| `src/perf/perf.cp` | _需按子系统/函数手工定位_ |
| `src/perf/system_timer.cp` | `src/perf/system_timer.rs` |
| `src/perf/tracy.cp` | `src/perf/tracy.rs` |
| `src/picohttp/picohttp.cp` | _需按子系统/函数手工定位_ |
| `src/picohttp_sys/picohttpparser.cp` | `src/picohttp_sys/picohttpparser.rs` |
| `src/platform/darwin.cp` | `src/platform/darwin.rs` |
| `src/platform/linux.cp` | `src/platform/linux.rs` |
| `src/ptr/Cow.cp` | `src/ptr/Cow.rs` |
| `src/ptr/CowSlice.cp` | `src/ptr/CowSlice.rs` |
| `src/ptr/external_shared.cp` | `src/bun_core/external_shared.rs` |
| `src/ptr/meta.cp` | `src/install/lockfile/Package/Meta.rs`, `src/ptr/meta.rs` |
| `src/ptr/owned.cp` | `src/ptr/owned.rs` |
| `src/ptr/ptr.cp` | _需按子系统/函数手工定位_ |
| `src/ptr/raw_ref_count.cp` | `src/ptr/raw_ref_count.rs` |
| `src/ptr/ref_count.cp` | `src/ptr/ref_count.rs` |
| `src/ptr/shared.cp` | `src/ptr/shared.rs`, `src/zlib_sys/shared.rs` |
| `src/ptr/tagged_pointer.cp` | `src/ptr/tagged_pointer.rs` |
| `src/ptr/weak_ptr.cp` | `src/ptr/weak_ptr.rs` |
| `src/resolve_builtins/HardcodedModule.cp` | `src/resolve_builtins/HardcodedModule.rs` |
| `src/resolver/data_url.cp` | `src/resolver/data_url.rs` |
| `src/resolver/dir_info.cp` | `src/resolver/dir_info.rs` |
| `src/resolver/fs.cp` | `src/resolver/fs.rs` |
| `src/resolver/fs/stat_hash.cp` | `src/resolver/fs/stat_hash.rs` |
| `src/resolver/node_fallbacks.cp` | `src/resolver/node_fallbacks.rs` |
| `src/resolver/package_json.cp` | `src/resolver/package_json.rs` |
| `src/resolver/resolver.cp` | _需按子系统/函数手工定位_ |
| `src/resolver/tsconfig_json.cp` | `src/resolver/tsconfig_json.rs` |
| `src/router/ParamMap.cp` | _需按子系统/函数手工定位_ |
| `src/router/Route.cp` | _需按子系统/函数手工定位_ |
| `src/router/Router.cp` | _需按子系统/函数手工定位_ |
| `src/router/route_types.cp` | _需按子系统/函数手工定位_ |
| `src/router/router.cp` | _需按子系统/函数手工定位_ |
| `src/runtime/api.cp` | `src/runtime/api.rs` |
| `src/runtime/api/Archive.cp` | `src/runtime/api/Archive.rs` |
| `src/runtime/api/BunObject.cp` | `src/runtime/api/BunObject.rs` |
| `src/runtime/api/HashObject.cp` | `src/runtime/api/HashObject.rs` |
| `src/runtime/api/JSBundler.cp` | `src/runtime/api/JSBundler.rs` |
| `src/runtime/api/JSON5Object.cp` | `src/runtime/api/JSON5Object.rs` |
| `src/runtime/api/JSONCObject.cp` | `src/runtime/api/JSONCObject.rs` |
| `src/runtime/api/JSTranspiler.cp` | `src/runtime/api/JSTranspiler.rs` |
| `src/runtime/api/MarkdownObject.cp` | `src/runtime/api/MarkdownObject.rs` |
| `src/runtime/api/NativePromiseContext.cp` | `src/runtime/api/NativePromiseContext.rs` |
| `src/runtime/api/TOMLObject.cp` | `src/runtime/api/TOMLObject.rs` |
| `src/runtime/api/UnsafeObject.cp` | `src/runtime/api/UnsafeObject.rs` |
| `src/runtime/api/YAMLObject.cp` | `src/runtime/api/YAMLObject.rs` |
| `src/runtime/api/bun/SSLContextCache.cp` | `src/runtime/api/bun/SSLContextCache.rs` |
| `src/runtime/api/bun/SecureContext.cp` | `src/runtime/api/bun/SecureContext.rs` |
| `src/runtime/api/bun/Terminal.cp` | `src/runtime/api/bun/Terminal.rs` |
| `src/runtime/api/bun/h2_frame_parser.cp` | `src/runtime/api/bun/h2_frame_parser.rs` |
| `src/runtime/api/bun/js_bun_spawn_bindings.cp` | `src/runtime/api/bun/js_bun_spawn_bindings.rs` |
| `src/runtime/api/bun/process.cp` | `src/runtime/api/bun/process.rs`, `src/spawn/process.rs` |
| `src/runtime/api/bun/spawn.cp` | `src/runtime/api/bun/spawn.rs` |
| `src/runtime/api/bun/spawn/stdio.cp` | `src/runtime/api/bun/spawn/stdio.rs` |
| `src/runtime/api/bun/subprocess.cp` | `src/runtime/api/bun/subprocess.rs` |
| `src/runtime/api/bun/subprocess/Readable.cp` | `src/runtime/api/bun/subprocess/Readable.rs` |
| `src/runtime/api/bun/subprocess/ResourceUsage.cp` | `src/runtime/api/bun/subprocess/ResourceUsage.rs` |
| `src/runtime/api/bun/subprocess/StaticPipeWriter.cp` | `src/runtime/api/bun/subprocess/StaticPipeWriter.rs`, `src/spawn/static_pipe_writer.rs` |
| `src/runtime/api/bun/subprocess/SubprocessPipeReader.cp` | `src/runtime/api/bun/subprocess/SubprocessPipeReader.rs` |
| `src/runtime/api/bun/subprocess/Writable.cp` | `src/runtime/api/bun/subprocess/Writable.rs` |
| `src/runtime/api/bun/x509.cp` | `src/runtime/api/bun/x509.rs` |
| `src/runtime/api/crash_handler_jsc.cp` | `src/runtime/api/crash_handler_jsc.rs` |
| `src/runtime/api/cron.cp` | `src/runtime/api/cron.rs` |
| `src/runtime/api/cron_parser.cp` | `src/runtime/api/cron_parser.rs` |
| `src/runtime/api/csrf_jsc.cp` | `src/runtime/api/csrf_jsc.rs` |
| `src/runtime/api/filesystem_router.cp` | `src/runtime/api/filesystem_router.rs` |
| `src/runtime/api/glob.cp` | `src/runtime/api/glob.rs` |
| `src/runtime/api/html_rewriter.cp` | `src/runtime/api/html_rewriter.rs` |
| `src/runtime/api/lolhtml_jsc.cp` | `src/runtime/api/lolhtml_jsc.rs` |
| `src/runtime/api/standalone_graph_jsc.cp` | `src/runtime/api/standalone_graph_jsc.rs` |
| `src/runtime/crypto/CryptoHasher.cp` | `src/runtime/crypto/CryptoHasher.rs` |
| `src/runtime/crypto/EVP.cp` | `src/runtime/crypto/EVP.rs` |
| `src/runtime/crypto/HMAC.cp` | `src/runtime/crypto/HMAC.rs`, `src/sha_hmac/hmac.rs` |
| `src/runtime/crypto/PBKDF2.cp` | `src/runtime/crypto/PBKDF2.rs` |
| `src/runtime/crypto/PasswordObject.cp` | `src/runtime/crypto/PasswordObject.rs` |
| `src/runtime/crypto/boringssl_jsc.cp` | `src/runtime/crypto/boringssl_jsc.rs` |
| `src/runtime/crypto/crypto.cp` | `src/runtime/webcore/Crypto.rs` |
| `src/runtime/dns_jsc/cares_jsc.cp` | `src/runtime/dns_jsc/cares_jsc.rs` |
| `src/runtime/dns_jsc/dns.cp` | `src/runtime/dns_jsc/dns.rs` |
| `src/runtime/dns_jsc/options_jsc.cp` | `src/bundler_jsc/options_jsc.rs`, `src/runtime/dns_jsc/options_jsc.rs` |
| `src/runtime/ffi/FFIObject.cp` | `src/runtime/ffi/FFIObject.rs` |
| `src/runtime/ffi/ffi.cp` | `src/jsc/FFI.rs` |
| `src/runtime/image/Image.cp` | `src/css/values/image.rs`, `src/runtime/image/Image.rs` |
| `src/runtime/image/backend_coregraphics.cp` | `src/runtime/image/backend_coregraphics.rs` |
| `src/runtime/image/backend_wic.cp` | `src/runtime/image/backend_wic.rs` |
| `src/runtime/image/codec_bmp.cp` | `src/runtime/image/codec_bmp.rs` |
| `src/runtime/image/codec_gif.cp` | `src/runtime/image/codec_gif.rs` |
| `src/runtime/image/codec_jpeg.cp` | `src/runtime/image/codec_jpeg.rs` |
| `src/runtime/image/codec_png.cp` | `src/runtime/image/codec_png.rs` |
| `src/runtime/image/codec_webp.cp` | `src/runtime/image/codec_webp.rs` |
| `src/runtime/image/codecs.cp` | `src/runtime/image/codecs.rs` |
| `src/runtime/image/exif.cp` | `src/runtime/image/exif.rs` |
| `src/runtime/image/quantize.cp` | `src/runtime/image/quantize.rs` |
| `src/runtime/image/thumbhash.cp` | `src/runtime/image/thumbhash.rs` |
| `src/runtime/node.cp` | `src/runtime/node.rs` |
| `src/runtime/node/Stat.cp` | `src/runtime/node/Stat.rs` |
| `src/runtime/node/StatFS.cp` | `src/runtime/node/StatFS.rs` |
| `src/runtime/node/assert/myers_diff.cp` | `src/runtime/node/assert/myers_diff.rs` |
| `src/runtime/node/buffer.cp` | `src/runtime/node/buffer.rs` |
| `src/runtime/node/dir_iterator.cp` | `src/runtime/node/dir_iterator.rs` |
| `src/runtime/node/fs_events.cp` | `src/runtime/node/fs_events.rs` |
| `src/runtime/node/net/BlockList.cp` | `src/runtime/node/net/BlockList.rs` |
| `src/runtime/node/node_assert.cp` | `src/runtime/node/node_assert.rs` |
| `src/runtime/node/node_assert_binding.cp` | `src/runtime/node/node_assert_binding.rs` |
| `src/runtime/node/node_cluster_binding.cp` | `src/runtime/node/node_cluster_binding.rs` |
| `src/runtime/node/node_crypto_binding.cp` | `src/runtime/node/node_crypto_binding.rs` |
| `src/runtime/node/node_error_binding.cp` | `src/runtime/node/node_error_binding.rs` |
| `src/runtime/node/node_fs.cp` | `src/runtime/node/node_fs.rs` |
| `src/runtime/node/node_fs_binding.cp` | `src/runtime/node/node_fs_binding.rs` |
| `src/runtime/node/node_fs_constant.cp` | `src/runtime/node/node_fs_constant.rs` |
| `src/runtime/node/node_fs_stat_watcher.cp` | `src/runtime/node/node_fs_stat_watcher.rs` |
| `src/runtime/node/node_fs_watcher.cp` | `src/runtime/node/node_fs_watcher.rs` |
| `src/runtime/node/node_http_binding.cp` | `src/runtime/node/node_http_binding.rs` |
| `src/runtime/node/node_net_binding.cp` | `src/runtime/node/node_net_binding.rs` |
| `src/runtime/node/node_os.cp` | `src/runtime/node/node_os.rs` |
| `src/runtime/node/node_process.cp` | `src/runtime/node/node_process.rs` |
| `src/runtime/node/node_util_binding.cp` | `src/runtime/node/node_util_binding.rs` |
| `src/runtime/node/node_zlib_binding.cp` | `src/runtime/node/node_zlib_binding.rs` |
| `src/runtime/node/nodejs_error_code.cp` | `src/runtime/node/nodejs_error_code.rs` |
| `src/runtime/node/os/constants.cp` | `src/runtime/node/os/constants.rs` |
| `src/runtime/node/path.cp` | `src/paths/Path.rs`, `src/runtime/node/path.rs` |
| `src/runtime/node/path_watcher.cp` | `src/runtime/node/path_watcher.rs` |
| `src/runtime/node/time_like.cp` | `src/runtime/node/time_like.rs` |
| `src/runtime/node/types.cp` | `src/md/types.rs`, `src/runtime/node/types.rs` |
| `src/runtime/node/util/parse_args.cp` | `src/runtime/node/util/parse_args.rs` |
| `src/runtime/node/util/parse_args_utils.cp` | `src/runtime/node/util/parse_args_utils.rs` |
| `src/runtime/node/util/validators.cp` | `src/runtime/node/util/validators.rs` |
| `src/runtime/node/uv_signal_handle_windows.cp` | `src/runtime/node/uv_signal_handle_windows.rs` |
| `src/runtime/node/win_watcher.cp` | `src/runtime/node/win_watcher.rs` |
| `src/runtime/node/zlib/NativeBrotli.cp` | `src/runtime/node/zlib/NativeBrotli.rs` |
| `src/runtime/node/zlib/NativeZlib.cp` | `src/runtime/node/zlib/NativeZlib.rs` |
| `src/runtime/node/zlib/NativeZstd.cp` | `src/runtime/node/zlib/NativeZstd.rs` |
| `src/runtime/server/AnyRequestContext.cp` | `src/runtime/server/AnyRequestContext.rs` |
| `src/runtime/server/FileResponseStream.cp` | `src/runtime/server/FileResponseStream.rs` |
| `src/runtime/server/FileRoute.cp` | `src/runtime/server/FileRoute.rs` |
| `src/runtime/server/HTMLBundle.cp` | `src/runtime/server/HTMLBundle.rs` |
| `src/runtime/server/HTTPStatusText.cp` | `src/runtime/server/HTTPStatusText.rs` |
| `src/runtime/server/InspectorBunFrontendDevServerAgent.cp` | `src/runtime/server/InspectorBunFrontendDevServerAgent.rs` |
| `src/runtime/server/NodeHTTPResponse.cp` | `src/runtime/server/NodeHTTPResponse.rs` |
| `src/runtime/server/RangeRequest.cp` | `src/runtime/server/RangeRequest.rs` |
| `src/runtime/server/RequestContext.cp` | `src/runtime/server/RequestContext.rs` |
| `src/runtime/server/ServerConfig.cp` | `src/runtime/server/ServerConfig.rs` |
| `src/runtime/server/ServerWebSocket.cp` | `src/runtime/server/ServerWebSocket.rs` |
| `src/runtime/server/StaticRoute.cp` | `src/runtime/server/StaticRoute.rs` |
| `src/runtime/server/WebSocketServerContext.cp` | `src/runtime/server/WebSocketServerContext.rs` |
| `src/runtime/server/server.cp` | _需按子系统/函数手工定位_ |
| `src/runtime/socket/Handlers.cp` | `src/runtime/socket/Handlers.rs` |
| `src/runtime/socket/Listener.cp` | `src/runtime/socket/Listener.rs` |
| `src/runtime/socket/SSLConfig.cp` | `src/http/ssl_config.rs`, `src/runtime/socket/SSLConfig.rs` |
| `src/runtime/socket/SocketAddress.cp` | `src/runtime/socket/SocketAddress.rs` |
| `src/runtime/socket/UpgradedDuplex.cp` | `src/runtime/socket/UpgradedDuplex.rs` |
| `src/runtime/socket/WindowsNamedPipe.cp` | `src/runtime/socket/WindowsNamedPipe.rs` |
| `src/runtime/socket/WindowsNamedPipeContext.cp` | `src/runtime/socket/WindowsNamedPipeContext.rs` |
| `src/runtime/socket/socket.cp` | `src/uws_sys/quic/Socket.rs`, `src/uws_sys/socket.rs` |
| `src/runtime/socket/ssl_wrapper.cp` | _需按子系统/函数手工定位_ |
| `src/runtime/socket/tls_socket_functions.cp` | `src/runtime/socket/tls_socket_functions.rs` |
| `src/runtime/socket/udp_socket.cp` | `src/runtime/socket/udp_socket.rs` |
| `src/runtime/socket/uws_dispatch.cp` | `src/runtime/socket/uws_dispatch.rs` |
| `src/runtime/socket/uws_handlers.cp` | `src/runtime/socket/uws_handlers.rs` |
| `src/runtime/socket/uws_jsc.cp` | `src/runtime/socket/uws_jsc.rs` |
| `src/runtime/timer/DateHeaderTimer.cp` | `src/runtime/timer/DateHeaderTimer.rs` |
| `src/runtime/timer/EventLoopDelayMonitor.cp` | `src/runtime/timer/EventLoopDelayMonitor.rs` |
| `src/runtime/timer/ImmediateObject.cp` | `src/runtime/timer/ImmediateObject.rs` |
| `src/runtime/timer/TimeoutObject.cp` | `src/runtime/timer/TimeoutObject.rs` |
| `src/runtime/timer/Timer.cp` | `src/runtime/timer/Timer.rs`, `src/uws_sys/Timer.rs` |
| `src/runtime/timer/TimerObjectInternals.cp` | `src/runtime/timer/timer_object_internals.rs` |
| `src/runtime/timer/WTFTimer.cp` | `src/runtime/timer/WTFTimer.rs` |
| `src/runtime/valkey_jsc/ValkeyCommand.cp` | `src/runtime/valkey_jsc/ValkeyCommand.rs` |
| `src/runtime/valkey_jsc/ValkeyContext.cp` | `src/runtime/valkey_jsc/ValkeyContext.rs` |
| `src/runtime/valkey_jsc/index.cp` | `src/runtime/valkey_jsc/index.rs` |
| `src/runtime/valkey_jsc/js_valkey.cp` | `src/runtime/valkey_jsc/js_valkey.rs` |
| `src/runtime/valkey_jsc/js_valkey_functions.cp` | `src/runtime/valkey_jsc/js_valkey_functions.rs` |
| `src/runtime/valkey_jsc/protocol_jsc.cp` | `src/runtime/valkey_jsc/protocol_jsc.rs` |
| `src/runtime/valkey_jsc/valkey.cp` | `src/runtime/valkey_jsc/valkey.rs` |
| `src/runtime/webcore.cp` | `src/runtime/webcore.rs` |
| `src/runtime/webcore/ArrayBufferSink.cp` | `src/runtime/webcore/ArrayBufferSink.rs` |
| `src/runtime/webcore/BakeResponse.cp` | `src/runtime/webcore/BakeResponse.rs` |
| `src/runtime/webcore/Blob.cp` | `src/runtime/webcore/Blob.rs` |
| `src/runtime/webcore/Body.cp` | `src/runtime/webcore/Body.rs` |
| `src/runtime/webcore/ByteBlobLoader.cp` | `src/runtime/webcore/ByteBlobLoader.rs` |
| `src/runtime/webcore/ByteStream.cp` | `src/runtime/webcore/ByteStream.rs` |
| `src/runtime/webcore/CookieMap.cp` | `src/runtime/webcore/CookieMap.rs` |
| `src/runtime/webcore/Crypto.cp` | `src/runtime/webcore/Crypto.rs` |
| `src/runtime/webcore/EncodingLabel.cp` | `src/runtime/webcore/EncodingLabel.rs` |
| `src/runtime/webcore/FileReader.cp` | `src/runtime/webcore/FileReader.rs` |
| `src/runtime/webcore/FileSink.cp` | `src/runtime/webcore/FileSink.rs` |
| `src/runtime/webcore/FormData.cp` | `src/runtime/webcore/FormData.rs` |
| `src/runtime/webcore/ObjectURLRegistry.cp` | `src/runtime/webcore/ObjectURLRegistry.rs` |
| `src/runtime/webcore/ReadableStream.cp` | `src/runtime/webcore/ReadableStream.rs` |
| `src/runtime/webcore/Request.cp` | `src/runtime/webcore/Request.rs`, `src/uws_sys/Request.rs` |
| `src/runtime/webcore/Response.cp` | `src/runtime/webcore/Response.rs`, `src/uws_sys/Response.rs` |
| `src/runtime/webcore/ResumableSink.cp` | `src/runtime/webcore/ResumableSink.rs` |
| `src/runtime/webcore/S3Client.cp` | `src/runtime/webcore/S3Client.rs` |
| `src/runtime/webcore/S3File.cp` | `src/runtime/webcore/S3File.rs` |
| `src/runtime/webcore/S3Stat.cp` | `src/runtime/webcore/S3Stat.rs` |
| `src/runtime/webcore/ScriptExecutionContext.cp` | `src/runtime/webcore/ScriptExecutionContext.rs` |
| `src/runtime/webcore/Sink.cp` | `src/runtime/webcore/Sink.rs` |
| `src/runtime/webcore/TextDecoder.cp` | `src/runtime/webcore/TextDecoder.rs` |
| `src/runtime/webcore/TextEncoder.cp` | `src/runtime/webcore/TextEncoder.rs` |
| `src/runtime/webcore/TextEncoderStreamEncoder.cp` | `src/runtime/webcore/TextEncoderStreamEncoder.rs` |
| `src/runtime/webcore/blob/Store.cp` | `src/install/isolated_install/Store.rs`, `src/runtime/webcore/blob/Store.rs` |
| `src/runtime/webcore/blob/copy_file.cp` | `src/runtime/webcore/blob/copy_file.rs`, `src/sys/copy_file.rs` |
| `src/runtime/webcore/blob/read_file.cp` | `src/runtime/webcore/blob/read_file.rs` |
| `src/runtime/webcore/blob/write_file.cp` | `src/runtime/webcore/blob/write_file.rs` |
| `src/runtime/webcore/encoding.cp` | `src/http_types/Encoding.rs`, `src/runtime/webcore/encoding.rs` |
| `src/runtime/webcore/fetch.cp` | `src/runtime/webcore/fetch.rs` |
| `src/runtime/webcore/fetch/FetchTasklet.cp` | `src/runtime/webcore/fetch/FetchTasklet.rs` |
| `src/runtime/webcore/prompt.cp` | `src/runtime/webcore/prompt.rs` |
| `src/runtime/webcore/s3/client.cp` | `src/runtime/webcore/s3/client.rs` |
| `src/runtime/webcore/s3/credentials_jsc.cp` | `src/runtime/webcore/s3/credentials_jsc.rs` |
| `src/runtime/webcore/s3/download_stream.cp` | `src/runtime/webcore/s3/download_stream.rs` |
| `src/runtime/webcore/s3/error_jsc.cp` | `src/css_jsc/error_jsc.rs`, `src/runtime/webcore/s3/error_jsc.rs`, `src/sql_jsc/postgres/error_jsc.rs` (+1) |
| `src/runtime/webcore/s3/list_objects.cp` | `src/runtime/webcore/s3/list_objects.rs` |
| `src/runtime/webcore/s3/multipart.cp` | `src/runtime/webcore/s3/multipart.rs` |
| `src/runtime/webcore/s3/multipart_options.cp` | `src/runtime/webcore/s3/multipart_options.rs` |
| `src/runtime/webcore/s3/simple_request.cp` | `src/runtime/webcore/s3/simple_request.rs` |
| `src/runtime/webcore/streams.cp` | `src/runtime/webcore/streams.rs` |
| `src/runtime/webview/ChromeProcess.cp` | `src/runtime/webview/ChromeProcess.rs` |
| `src/runtime/webview/HostProcess.cp` | `src/runtime/webview/HostProcess.rs` |
| `src/s3_signing/acl.cp` | `src/s3_signing/acl.rs` |
| `src/s3_signing/credentials.cp` | `src/s3_signing/credentials.rs` |
| `src/s3_signing/error.cp` | `src/css/error.rs`, `src/s3_signing/error.rs`, `src/sys/Error.rs` |
| `src/s3_signing/storage_class.cp` | `src/s3_signing/storage_class.rs` |
| `src/safety/CriticalSection.cp` | `src/safety/CriticalSection.rs` |
| `src/safety/ThreadLock.cp` | `src/safety/ThreadLock.rs` |
| `src/safety/alloc.cp` | `src/safety/alloc.rs` |
| `src/safety/asan.cp` | `src/safety/asan.rs` |
| `src/safety/safety.cp` | _需按子系统/函数手工定位_ |
| `src/safety/thread_id.cp` | `src/bun_core/thread_id.rs`, `src/safety/thread_id.rs` |
| `src/semver/SemverQuery.cp` | `src/semver/SemverQuery.rs` |
| `src/semver/SemverRange.cp` | `src/semver/SemverRange.rs` |
| `src/semver/Version.cp` | `src/semver/Version.rs` |
| `src/semver/semver.cp` | _需按子系统/函数手工定位_ |
| `src/semver_jsc/SemverObject.cp` | `src/semver_jsc/SemverObject.rs` |
| `src/semver_jsc/SemverString_jsc.cp` | `src/semver_jsc/SemverString_jsc.rs` |
| `src/sha_hmac/hmac.cp` | `src/runtime/crypto/HMAC.rs`, `src/sha_hmac/hmac.rs` |
| `src/sha_hmac/sha.cp` | `src/sha_hmac/sha.rs` |
| `src/shell/AllocScope.cp` | _需按子系统/函数手工定位_ |
| `src/shell/Builtin.cp` | `src/runtime/shell/Builtin.rs` |
| `src/shell/EnvMap.cp` | `src/runtime/shell/EnvMap.rs` |
| `src/shell/EnvStr.cp` | `src/runtime/shell/EnvStr.rs` |
| `src/shell/IO.cp` | `src/runtime/shell/IO.rs` |
| `src/shell/IOReader.cp` | `src/runtime/shell/IOReader.rs` |
| `src/shell/IOWriter.cp` | `src/runtime/shell/IOWriter.rs` |
| `src/shell/ParsedShellScript.cp` | `src/runtime/shell/ParsedShellScript.rs` |
| `src/shell/RefCountedStr.cp` | `src/runtime/shell/RefCountedStr.rs` |
| `src/shell/Yield.cp` | `src/runtime/shell/Yield.rs` |
| `src/shell/builtin/basename.cp` | `src/runtime/shell/builtin/basename.rs` |
| `src/shell/builtin/cat.cp` | `src/runtime/shell/builtin/cat.rs` |
| `src/shell/builtin/cd.cp` | `src/runtime/shell/builtin/cd.rs` |
| `src/shell/builtin/cp.cp` | `src/runtime/shell/builtin/cp.rs` |
| `src/shell/builtin/dirname.cp` | `src/runtime/shell/builtin/dirname.rs` |
| `src/shell/builtin/echo.cp` | `src/runtime/shell/builtin/echo.rs` |
| `src/shell/builtin/exit.cp` | `src/runtime/shell/builtin/exit.rs` |
| `src/shell/builtin/export.cp` | `src/runtime/shell/builtin/export.rs` |
| `src/shell/builtin/ls.cp` | `src/runtime/shell/builtin/ls.rs` |
| `src/shell/builtin/mkdir.cp` | `src/runtime/shell/builtin/mkdir.rs` |
| `src/shell/builtin/mv.cp` | `src/runtime/shell/builtin/mv.rs` |
| `src/shell/builtin/pwd.cp` | `src/runtime/shell/builtin/pwd.rs` |
| `src/shell/builtin/rm.cp` | `src/runtime/shell/builtin/rm.rs` |
| `src/shell/builtin/seq.cp` | `src/runtime/shell/builtin/seq.rs` |
| `src/shell/builtin/touch.cp` | `src/runtime/shell/builtin/touch.rs` |
| `src/shell/builtin/which.cp` | `src/runtime/shell/builtin/which.rs` |
| `src/shell/builtin/yes.cp` | `src/runtime/shell/builtin/yes.rs` |
| `src/shell/interpreter.cp` | `src/runtime/shell/interpreter.rs` |
| `src/shell/shell.cp` | _需按子系统/函数手工定位_ |
| `src/shell/states/Assigns.cp` | `src/runtime/shell/states/Assigns.rs` |
| `src/shell/states/Async.cp` | `src/runtime/shell/states/Async.rs` |
| `src/shell/states/Base.cp` | `src/ast/base.rs`, `src/runtime/shell/states/Base.rs` |
| `src/shell/states/Binary.cp` | `src/runtime/shell/states/Binary.rs` |
| `src/shell/states/Cmd.cp` | `src/runtime/shell/states/Cmd.rs` |
| `src/shell/states/CondExpr.cp` | `src/runtime/shell/states/CondExpr.rs` |
| `src/shell/states/Expansion.cp` | `src/runtime/shell/states/Expansion.rs` |
| `src/shell/states/If.cp` | `src/runtime/shell/states/If.rs` |
| `src/shell/states/Pipeline.cp` | `src/runtime/shell/states/Pipeline.rs` |
| `src/shell/states/Script.cp` | `src/runtime/shell/states/Script.rs` |
| `src/shell/states/Stmt.cp` | `src/ast/stmt.rs`, `src/runtime/shell/states/Stmt.rs` |
| `src/shell/states/Subshell.cp` | `src/runtime/shell/states/Subshell.rs` |
| `src/shell/subproc.cp` | `src/runtime/shell/subproc.rs` |
| `src/shell/util.cp` | `src/bun_core/util.rs`, `src/runtime/shell/util.rs` |
| `src/shell_parser/braces.cp` | `src/shell_parser/braces.rs` |
| `src/simdutf_sys/simdutf.cp` | `src/simdutf_sys/simdutf.rs` |
| `src/sourcemap/Chunk.cp` | `src/bundler/Chunk.rs`, `src/sourcemap/Chunk.rs` |
| `src/sourcemap/InternalSourceMap.cp` | `src/sourcemap/InternalSourceMap.rs` |
| `src/sourcemap/LineOffsetTable.cp` | `src/sourcemap/LineOffsetTable.rs` |
| `src/sourcemap/Mapping.cp` | `src/sourcemap/Mapping.rs` |
| `src/sourcemap/ParsedSourceMap.cp` | `src/sourcemap/ParsedSourceMap.rs` |
| `src/sourcemap/VLQ.cp` | _需按子系统/函数手工定位_ |
| `src/sourcemap/sourcemap.cp` | `src/css/sourcemap.rs` |
| `src/sourcemap_jsc/CodeCoverage.cp` | `src/sourcemap_jsc/CodeCoverage.rs` |
| `src/sourcemap_jsc/JSSourceMap.cp` | `src/sourcemap_jsc/JSSourceMap.rs` |
| `src/sourcemap_jsc/internal_jsc.cp` | `src/sourcemap_jsc/internal_jsc.rs` |
| `src/sourcemap_jsc/source_provider.cp` | `src/jsc/SourceProvider.rs`, `src/sourcemap_jsc/source_provider.rs` |
| `src/sql/mysql/AuthMethod.cp` | `src/sql/mysql/AuthMethod.rs` |
| `src/sql/mysql/Capabilities.cp` | `src/sql/mysql/Capabilities.rs` |
| `src/sql/mysql/ConnectionState.cp` | `src/sql/mysql/ConnectionState.rs` |
| `src/sql/mysql/MySQLParam.cp` | `src/sql/mysql/MySQLParam.rs` |
| `src/sql/mysql/MySQLQueryResult.cp` | `src/sql/mysql/MySQLQueryResult.rs` |
| `src/sql/mysql/MySQLRequest.cp` | `src/sql/mysql/MySQLRequest.rs` |
| `src/sql/mysql/MySQLTypes.cp` | `src/sql/mysql/MySQLTypes.rs` |
| `src/sql/mysql/QueryStatus.cp` | `src/sql/mysql/QueryStatus.rs` |
| `src/sql/mysql/SSLMode.cp` | `src/sql/mysql/SSLMode.rs`, `src/sql/postgres/SSLMode.rs` |
| `src/sql/mysql/StatusFlags.cp` | `src/sql/mysql/StatusFlags.rs` |
| `src/sql/mysql/TLSStatus.cp` | `src/sql/mysql/TLSStatus.rs`, `src/sql/postgres/TLSStatus.rs` |
| `src/sql/mysql/protocol/AnyMySQLError.cp` | `src/sql/mysql/protocol/AnyMySQLError.rs` |
| `src/sql/mysql/protocol/Auth.cp` | `src/sql/mysql/protocol/Auth.rs` |
| `src/sql/mysql/protocol/AuthSwitchRequest.cp` | `src/sql/mysql/protocol/AuthSwitchRequest.rs` |
| `src/sql/mysql/protocol/AuthSwitchResponse.cp` | `src/sql/mysql/protocol/AuthSwitchResponse.rs` |
| `src/sql/mysql/protocol/CharacterSet.cp` | `src/sql/mysql/protocol/CharacterSet.rs` |
| `src/sql/mysql/protocol/ColumnDefinition41.cp` | `src/sql/mysql/protocol/ColumnDefinition41.rs` |
| `src/sql/mysql/protocol/CommandType.cp` | `src/sql/mysql/protocol/CommandType.rs` |
| `src/sql/mysql/protocol/EOFPacket.cp` | `src/sql/mysql/protocol/EOFPacket.rs` |
| `src/sql/mysql/protocol/EncodeInt.cp` | `src/sql/mysql/protocol/EncodeInt.rs` |
| `src/sql/mysql/protocol/ErrorPacket.cp` | `src/sql/mysql/protocol/ErrorPacket.rs` |
| `src/sql/mysql/protocol/HandshakeResponse41.cp` | `src/sql/mysql/protocol/HandshakeResponse41.rs` |
| `src/sql/mysql/protocol/HandshakeV10.cp` | `src/sql/mysql/protocol/HandshakeV10.rs` |
| `src/sql/mysql/protocol/LocalInfileRequest.cp` | `src/sql/mysql/protocol/LocalInfileRequest.rs` |
| `src/sql/mysql/protocol/NewReader.cp` | `src/sql/mysql/protocol/NewReader.rs`, `src/sql/postgres/protocol/NewReader.rs` |
| `src/sql/mysql/protocol/NewWriter.cp` | `src/sql/mysql/protocol/NewWriter.rs`, `src/sql/postgres/protocol/NewWriter.rs` |
| `src/sql/mysql/protocol/OKPacket.cp` | `src/sql/mysql/protocol/OKPacket.rs` |
| `src/sql/mysql/protocol/PacketHeader.cp` | `src/sql/mysql/protocol/PacketHeader.rs` |
| `src/sql/mysql/protocol/PacketType.cp` | `src/sql/mysql/protocol/PacketType.rs` |
| `src/sql/mysql/protocol/PreparedStatement.cp` | `src/sql/mysql/protocol/PreparedStatement.rs` |
| `src/sql/mysql/protocol/Query.cp` | `src/sql/mysql/protocol/Query.rs` |
| `src/sql/mysql/protocol/ResultSetHeader.cp` | `src/sql/mysql/protocol/ResultSetHeader.rs` |
| `src/sql/mysql/protocol/SSLRequest.cp` | `src/sql/mysql/protocol/SSLRequest.rs` |
| `src/sql/mysql/protocol/StackReader.cp` | `src/sql/mysql/protocol/StackReader.rs`, `src/sql/postgres/protocol/StackReader.rs` |
| `src/sql/mysql/protocol/StmtPrepareOKPacket.cp` | `src/sql/mysql/protocol/StmtPrepareOKPacket.rs` |
| `src/sql/postgres/AnyPostgresError.cp` | `src/sql/postgres/AnyPostgresError.rs` |
| `src/sql/postgres/CommandTag.cp` | `src/options_types/command_tag.rs`, `src/sql/postgres/CommandTag.rs` |
| `src/sql/postgres/DebugSocketMonitorReader.cp` | _需按子系统/函数手工定位_ |
| `src/sql/postgres/DebugSocketMonitorWriter.cp` | _需按子系统/函数手工定位_ |
| `src/sql/postgres/PostgresProtocol.cp` | `src/sql/postgres/PostgresProtocol.rs` |
| `src/sql/postgres/PostgresTypes.cp` | `src/sql/postgres/PostgresTypes.rs` |
| `src/sql/postgres/SSLMode.cp` | `src/sql/mysql/SSLMode.rs`, `src/sql/postgres/SSLMode.rs` |
| `src/sql/postgres/SocketMonitor.cp` | `src/sql/postgres/SocketMonitor.rs` |
| `src/sql/postgres/Status.cp` | `src/sql/postgres/Status.rs` |
| `src/sql/postgres/TLSStatus.cp` | `src/sql/mysql/TLSStatus.rs`, `src/sql/postgres/TLSStatus.rs` |
| `src/sql/postgres/protocol/ArrayList.cp` | `src/collections/array_list.rs`, `src/sql/postgres/protocol/ArrayList.rs` |
| `src/sql/postgres/protocol/Authentication.cp` | `src/sql/postgres/protocol/Authentication.rs` |
| `src/sql/postgres/protocol/BackendKeyData.cp` | `src/sql/postgres/protocol/BackendKeyData.rs` |
| `src/sql/postgres/protocol/Close.cp` | `src/sql/postgres/protocol/Close.rs` |
| `src/sql/postgres/protocol/CommandComplete.cp` | `src/sql/postgres/protocol/CommandComplete.rs` |
| `src/sql/postgres/protocol/CopyData.cp` | `src/sql/postgres/protocol/CopyData.rs` |
| `src/sql/postgres/protocol/CopyFail.cp` | `src/sql/postgres/protocol/CopyFail.rs` |
| `src/sql/postgres/protocol/CopyInResponse.cp` | `src/sql/postgres/protocol/CopyInResponse.rs` |
| `src/sql/postgres/protocol/CopyOutResponse.cp` | `src/sql/postgres/protocol/CopyOutResponse.rs` |
| `src/sql/postgres/protocol/DataRow.cp` | `src/sql/postgres/protocol/DataRow.rs` |
| `src/sql/postgres/protocol/DecoderWrap.cp` | `src/sql/postgres/protocol/DecoderWrap.rs` |
| `src/sql/postgres/protocol/Describe.cp` | `src/sql/postgres/protocol/Describe.rs` |
| `src/sql/postgres/protocol/ErrorResponse.cp` | `src/sql/postgres/protocol/ErrorResponse.rs` |
| `src/sql/postgres/protocol/Execute.cp` | `src/sql/postgres/protocol/Execute.rs` |
| `src/sql/postgres/protocol/FieldDescription.cp` | `src/sql/postgres/protocol/FieldDescription.rs` |
| `src/sql/postgres/protocol/FieldMessage.cp` | `src/sql/postgres/protocol/FieldMessage.rs` |
| `src/sql/postgres/protocol/FieldType.cp` | `src/sql/postgres/protocol/FieldType.rs` |
| `src/sql/postgres/protocol/NegotiateProtocolVersion.cp` | `src/sql/postgres/protocol/NegotiateProtocolVersion.rs` |
| `src/sql/postgres/protocol/NewReader.cp` | `src/sql/mysql/protocol/NewReader.rs`, `src/sql/postgres/protocol/NewReader.rs` |
| `src/sql/postgres/protocol/NewWriter.cp` | `src/sql/mysql/protocol/NewWriter.rs`, `src/sql/postgres/protocol/NewWriter.rs` |
| `src/sql/postgres/protocol/NoticeResponse.cp` | `src/sql/postgres/protocol/NoticeResponse.rs` |
| `src/sql/postgres/protocol/NotificationResponse.cp` | `src/sql/postgres/protocol/NotificationResponse.rs` |
| `src/sql/postgres/protocol/ParameterDescription.cp` | `src/sql/postgres/protocol/ParameterDescription.rs` |
| `src/sql/postgres/protocol/ParameterStatus.cp` | `src/sql/postgres/protocol/ParameterStatus.rs` |
| `src/sql/postgres/protocol/Parse.cp` | `src/shell_parser/parse.rs`, `src/sql/postgres/protocol/Parse.rs` |
| `src/sql/postgres/protocol/PasswordMessage.cp` | `src/sql/postgres/protocol/PasswordMessage.rs` |
| `src/sql/postgres/protocol/PortalOrPreparedStatement.cp` | `src/sql/postgres/protocol/PortalOrPreparedStatement.rs` |
| `src/sql/postgres/protocol/ReadyForQuery.cp` | `src/sql/postgres/protocol/ReadyForQuery.rs` |
| `src/sql/postgres/protocol/RowDescription.cp` | `src/sql/postgres/protocol/RowDescription.rs` |
| `src/sql/postgres/protocol/SASLInitialResponse.cp` | `src/sql/postgres/protocol/SASLInitialResponse.rs` |
| `src/sql/postgres/protocol/SASLResponse.cp` | `src/sql/postgres/protocol/SASLResponse.rs` |
| `src/sql/postgres/protocol/StackReader.cp` | `src/sql/mysql/protocol/StackReader.rs`, `src/sql/postgres/protocol/StackReader.rs` |
| `src/sql/postgres/protocol/StartupMessage.cp` | `src/sql/postgres/protocol/StartupMessage.rs` |
| `src/sql/postgres/protocol/TransactionStatusIndicator.cp` | `src/sql/postgres/protocol/TransactionStatusIndicator.rs` |
| `src/sql/postgres/protocol/WriteWrap.cp` | `src/sql/postgres/protocol/WriteWrap.rs` |
| `src/sql/postgres/protocol/zHelpers.cp` | `src/sql/postgres/protocol/zHelpers.rs` |
| `src/sql/postgres/types/Tag.cp` | `src/sql/postgres/types/Tag.rs` |
| `src/sql/postgres/types/int_types.cp` | `src/sql/postgres/types/int_types.rs` |
| `src/sql/shared/ColumnIdentifier.cp` | `src/sql/shared/ColumnIdentifier.rs` |
| `src/sql/shared/ConnectionFlags.cp` | `src/sql/shared/ConnectionFlags.rs` |
| `src/sql/shared/Data.cp` | `src/sql/shared/Data.rs` |
| `src/sql/shared/SQLQueryResultMode.cp` | `src/sql/shared/SQLQueryResultMode.rs` |
| `src/sql_jsc/mysql.cp` | `src/sql_jsc/mysql.rs` |
| `src/sql_jsc/mysql/JSMySQLConnection.cp` | `src/sql_jsc/mysql/JSMySQLConnection.rs` |
| `src/sql_jsc/mysql/JSMySQLQuery.cp` | `src/sql_jsc/mysql/JSMySQLQuery.rs` |
| `src/sql_jsc/mysql/MySQLConnection.cp` | `src/sql_jsc/mysql/MySQLConnection.rs` |
| `src/sql_jsc/mysql/MySQLContext.cp` | `src/sql_jsc/mysql/MySQLContext.rs` |
| `src/sql_jsc/mysql/MySQLQuery.cp` | `src/sql_jsc/mysql/MySQLQuery.rs` |
| `src/sql_jsc/mysql/MySQLRequestQueue.cp` | `src/sql_jsc/mysql/MySQLRequestQueue.rs` |
| `src/sql_jsc/mysql/MySQLStatement.cp` | `src/sql_jsc/mysql/MySQLStatement.rs` |
| `src/sql_jsc/mysql/MySQLValue.cp` | `src/sql_jsc/mysql/MySQLValue.rs` |
| `src/sql_jsc/mysql/protocol/DecodeBinaryValue.cp` | `src/sql_jsc/mysql/protocol/DecodeBinaryValue.rs` |
| `src/sql_jsc/mysql/protocol/ResultSet.cp` | `src/sql_jsc/mysql/protocol/ResultSet.rs` |
| `src/sql_jsc/mysql/protocol/Signature.cp` | `src/sql_jsc/mysql/protocol/Signature.rs`, `src/sql_jsc/postgres/Signature.rs` |
| `src/sql_jsc/mysql/protocol/any_mysql_error_jsc.cp` | `src/sql_jsc/mysql/protocol/any_mysql_error_jsc.rs` |
| `src/sql_jsc/mysql/protocol/error_packet_jsc.cp` | `src/sql_jsc/mysql/protocol/error_packet_jsc.rs` |
| `src/sql_jsc/postgres.cp` | `src/sql_jsc/postgres.rs` |
| `src/sql_jsc/postgres/AuthenticationState.cp` | `src/sql_jsc/postgres/AuthenticationState.rs` |
| `src/sql_jsc/postgres/DataCell.cp` | `src/sql_jsc/postgres/DataCell.rs` |
| `src/sql_jsc/postgres/PostgresRequest.cp` | `src/sql_jsc/postgres/PostgresRequest.rs` |
| `src/sql_jsc/postgres/PostgresSQLConnection.cp` | `src/sql_jsc/postgres/PostgresSQLConnection.rs` |
| `src/sql_jsc/postgres/PostgresSQLContext.cp` | `src/sql_jsc/postgres/PostgresSQLContext.rs` |
| `src/sql_jsc/postgres/PostgresSQLQuery.cp` | `src/sql_jsc/postgres/PostgresSQLQuery.rs` |
| `src/sql_jsc/postgres/PostgresSQLStatement.cp` | `src/sql_jsc/postgres/PostgresSQLStatement.rs` |
| `src/sql_jsc/postgres/SASL.cp` | `src/sql_jsc/postgres/SASL.rs` |
| `src/sql_jsc/postgres/Signature.cp` | `src/sql_jsc/mysql/protocol/Signature.rs`, `src/sql_jsc/postgres/Signature.rs` |
| `src/sql_jsc/postgres/command_tag_jsc.cp` | `src/sql_jsc/postgres/command_tag_jsc.rs` |
| `src/sql_jsc/postgres/error_jsc.cp` | `src/css_jsc/error_jsc.rs`, `src/runtime/webcore/s3/error_jsc.rs`, `src/sql_jsc/postgres/error_jsc.rs` (+1) |
| `src/sql_jsc/postgres/protocol/error_response_jsc.cp` | `src/sql_jsc/postgres/protocol/error_response_jsc.rs` |
| `src/sql_jsc/postgres/protocol/notice_response_jsc.cp` | `src/sql_jsc/postgres/protocol/notice_response_jsc.rs` |
| `src/sql_jsc/postgres/types/PostgresString.cp` | `src/sql_jsc/postgres/types/PostgresString.rs` |
| `src/sql_jsc/postgres/types/bool.cp` | `src/sql_jsc/postgres/types/bool.rs` |
| `src/sql_jsc/postgres/types/bytea.cp` | `src/sql_jsc/postgres/types/bytea.rs` |
| `src/sql_jsc/postgres/types/date.cp` | `src/sql_jsc/postgres/types/date.rs` |
| `src/sql_jsc/postgres/types/json.cp` | `src/parsers/json.rs`, `src/sql_jsc/postgres/types/json.rs` |
| `src/sql_jsc/postgres/types/tag_jsc.cp` | `src/sql_jsc/postgres/types/tag_jsc.rs` |
| `src/sql_jsc/shared/CachedStructure.cp` | `src/sql_jsc/shared/CachedStructure.rs` |
| `src/sql_jsc/shared/ObjectIterator.cp` | `src/sql_jsc/shared/ObjectIterator.rs` |
| `src/sql_jsc/shared/QueryBindingIterator.cp` | `src/sql_jsc/shared/QueryBindingIterator.rs` |
| `src/sql_jsc/shared/SQLDataCell.cp` | `src/sql_jsc/shared/SQLDataCell.rs` |
| `src/standalone_graph/StandaloneModuleGraph.cp` | `src/standalone_graph/StandaloneModuleGraph.rs` |
| `src/string/HashedString.cp` | `src/bun_core/string/HashedString.rs` |
| `src/string/MutableString.cp` | `src/bun_core/string/MutableString.rs` |
| `src/string/PathString.cp` | `src/bun_core/string/PathString.rs` |
| `src/string/SmolStr.cp` | `src/bun_core/string/SmolStr.rs` |
| `src/string/StringBuilder.cp` | `src/bun_core/string/StringBuilder.rs`, `src/jsc/StringBuilder.rs` |
| `src/string/StringJoiner.cp` | `src/bun_core/string/StringJoiner.rs` |
| `src/string/escapeRegExp.cp` | `src/bun_core/string/escapeRegExp.rs` |
| `src/string/immutable.cp` | `src/bun_core/string/immutable.rs` |
| `src/string/immutable/escapeHTML.cp` | `src/bun_core/string/immutable/escapeHTML.rs` |
| `src/string/immutable/exact_size_matcher.cp` | `src/bun_core/string/immutable/exact_size_matcher.rs` |
| `src/string/immutable/grapheme.cp` | `src/bun_core/string/immutable/grapheme.rs` |
| `src/string/immutable/grapheme_tables.cp` | `src/bun_core/string/immutable/grapheme_tables.rs` |
| `src/string/immutable/paths.cp` | _需按子系统/函数手工定位_ |
| `src/string/immutable/unicode.cp` | `src/bun_core/string/immutable/unicode.rs`, `src/md/unicode.rs` |
| `src/string/immutable/visible.cp` | `src/bun_core/string/immutable/visible.rs` |
| `src/string/string.cp` | _需按子系统/函数手工定位_ |
| `src/string/wtf.cp` | `src/bun_core/string/wtf.rs`, `src/bun_core/wtf.rs`, `src/jsc/WTF.rs` |
| `src/sys/Error.cp` | `src/css/error.rs`, `src/s3_signing/error.rs`, `src/sys/Error.rs` |
| `src/sys/File.cp` | `src/sys/file.rs` |
| `src/sys/PosixStat.cp` | `src/sys/PosixStat.rs` |
| `src/sys/SignalCode.cp` | `src/sys/SignalCode.rs` |
| `src/sys/copy_file.cp` | `src/runtime/webcore/blob/copy_file.rs`, `src/sys/copy_file.rs` |
| `src/sys/coreutils_error_map.cp` | `src/sys/coreutils_error_map.rs` |
| `src/sys/dir.cp` | `src/sys/dir.rs` |
| `src/sys/fd.cp` | `src/sys/fd.rs` |
| `src/sys/libuv_error_map.cp` | `src/sys/libuv_error_map.rs` |
| `src/sys/sys.cp` | _需按子系统/函数手工定位_ |
| `src/sys/sys_uv.cp` | `src/sys/sys_uv.rs` |
| `src/sys/tmp.cp` | `src/sys/tmp.rs` |
| `src/sys/walker_skippable.cp` | `src/sys/walker_skippable.rs` |
| `src/sys/windows/env.cp` | `src/bun_core/env.rs`, `src/sys/windows/env.rs` |
| `src/sys/windows/windows.cp` | _需按子系统/函数手工定位_ |
| `src/sys_jsc/error_jsc.cp` | `src/css_jsc/error_jsc.rs`, `src/runtime/webcore/s3/error_jsc.rs`, `src/sql_jsc/postgres/error_jsc.rs` (+1) |
| `src/sys_jsc/fd_jsc.cp` | `src/sys_jsc/fd_jsc.rs` |
| `src/sys_jsc/signal_code_jsc.cp` | `src/sys_jsc/signal_code_jsc.rs` |
| `src/tcc_sys/tcc.cp` | `src/tcc_sys/tcc.rs` |
| `src/test_runner/Collection.cp` | `src/runtime/test_runner/Collection.rs` |
| `src/test_runner/DoneCallback.cp` | `src/runtime/test_runner/DoneCallback.rs` |
| `src/test_runner/Execution.cp` | `src/runtime/test_runner/Execution.rs` |
| `src/test_runner/Order.cp` | `src/runtime/test_runner/Order.rs` |
| `src/test_runner/ScopeFunctions.cp` | `src/runtime/test_runner/ScopeFunctions.rs` |
| `src/test_runner/bun_test.cp` | `src/runtime/test_runner/bun_test.rs` |
| `src/test_runner/debug.cp` | `src/runtime/test_runner/debug.rs` |
| `src/test_runner/diff/diff_match_patch.cp` | `src/runtime/test_runner/diff/diff_match_patch.rs` |
| `src/test_runner/diff/printDiff.cp` | `src/runtime/test_runner/diff/printDiff.rs` |
| `src/test_runner/diff_format.cp` | `src/runtime/test_runner/diff_format.rs` |
| `src/test_runner/expect.cp` | `src/runtime/test_runner/expect.rs` |
| `src/test_runner/expect/toBe.cp` | `src/runtime/test_runner/expect/toBe.rs` |
| `src/test_runner/expect/toBeArray.cp` | `src/runtime/test_runner/expect/toBeArray.rs` |
| `src/test_runner/expect/toBeArrayOfSize.cp` | `src/runtime/test_runner/expect/toBeArrayOfSize.rs` |
| `src/test_runner/expect/toBeBoolean.cp` | `src/runtime/test_runner/expect/toBeBoolean.rs` |
| `src/test_runner/expect/toBeCloseTo.cp` | `src/runtime/test_runner/expect/toBeCloseTo.rs` |
| `src/test_runner/expect/toBeDate.cp` | `src/runtime/test_runner/expect/toBeDate.rs` |
| `src/test_runner/expect/toBeDefined.cp` | `src/runtime/test_runner/expect/toBeDefined.rs` |
| `src/test_runner/expect/toBeEmpty.cp` | `src/runtime/test_runner/expect/toBeEmpty.rs` |
| `src/test_runner/expect/toBeEmptyObject.cp` | `src/runtime/test_runner/expect/toBeEmptyObject.rs` |
| `src/test_runner/expect/toBeEven.cp` | `src/runtime/test_runner/expect/toBeEven.rs` |
| `src/test_runner/expect/toBeFalse.cp` | `src/runtime/test_runner/expect/toBeFalse.rs` |
| `src/test_runner/expect/toBeFalsy.cp` | `src/runtime/test_runner/expect/toBeFalsy.rs` |
| `src/test_runner/expect/toBeFinite.cp` | `src/runtime/test_runner/expect/toBeFinite.rs` |
| `src/test_runner/expect/toBeFunction.cp` | `src/runtime/test_runner/expect/toBeFunction.rs` |
| `src/test_runner/expect/toBeGreaterThan.cp` | `src/runtime/test_runner/expect/toBeGreaterThan.rs` |
| `src/test_runner/expect/toBeGreaterThanOrEqual.cp` | `src/runtime/test_runner/expect/toBeGreaterThanOrEqual.rs` |
| `src/test_runner/expect/toBeInstanceOf.cp` | `src/runtime/test_runner/expect/toBeInstanceOf.rs` |
| `src/test_runner/expect/toBeInteger.cp` | `src/runtime/test_runner/expect/toBeInteger.rs` |
| `src/test_runner/expect/toBeLessThan.cp` | `src/runtime/test_runner/expect/toBeLessThan.rs` |
| `src/test_runner/expect/toBeLessThanOrEqual.cp` | `src/runtime/test_runner/expect/toBeLessThanOrEqual.rs` |
| `src/test_runner/expect/toBeNaN.cp` | `src/runtime/test_runner/expect/toBeNaN.rs` |
| `src/test_runner/expect/toBeNegative.cp` | `src/runtime/test_runner/expect/toBeNegative.rs` |
| `src/test_runner/expect/toBeNil.cp` | `src/runtime/test_runner/expect/toBeNil.rs` |
| `src/test_runner/expect/toBeNull.cp` | `src/runtime/test_runner/expect/toBeNull.rs` |
| `src/test_runner/expect/toBeNumber.cp` | `src/runtime/test_runner/expect/toBeNumber.rs` |
| `src/test_runner/expect/toBeObject.cp` | `src/runtime/test_runner/expect/toBeObject.rs` |
| `src/test_runner/expect/toBeOdd.cp` | `src/runtime/test_runner/expect/toBeOdd.rs` |
| `src/test_runner/expect/toBeOneOf.cp` | `src/runtime/test_runner/expect/toBeOneOf.rs` |
| `src/test_runner/expect/toBePositive.cp` | `src/runtime/test_runner/expect/toBePositive.rs` |
| `src/test_runner/expect/toBeString.cp` | `src/runtime/test_runner/expect/toBeString.rs` |
| `src/test_runner/expect/toBeSymbol.cp` | `src/runtime/test_runner/expect/toBeSymbol.rs` |
| `src/test_runner/expect/toBeTrue.cp` | `src/runtime/test_runner/expect/toBeTrue.rs` |
| `src/test_runner/expect/toBeTruthy.cp` | `src/runtime/test_runner/expect/toBeTruthy.rs` |
| `src/test_runner/expect/toBeTypeOf.cp` | `src/runtime/test_runner/expect/toBeTypeOf.rs` |
| `src/test_runner/expect/toBeUndefined.cp` | `src/runtime/test_runner/expect/toBeUndefined.rs` |
| `src/test_runner/expect/toBeValidDate.cp` | `src/runtime/test_runner/expect/toBeValidDate.rs` |
| `src/test_runner/expect/toBeWithin.cp` | `src/runtime/test_runner/expect/toBeWithin.rs` |
| `src/test_runner/expect/toContain.cp` | `src/runtime/test_runner/expect/toContain.rs` |
| `src/test_runner/expect/toContainAllKeys.cp` | `src/runtime/test_runner/expect/toContainAllKeys.rs` |
| `src/test_runner/expect/toContainAllValues.cp` | `src/runtime/test_runner/expect/toContainAllValues.rs` |
| `src/test_runner/expect/toContainAnyKeys.cp` | `src/runtime/test_runner/expect/toContainAnyKeys.rs` |
| `src/test_runner/expect/toContainAnyValues.cp` | `src/runtime/test_runner/expect/toContainAnyValues.rs` |
| `src/test_runner/expect/toContainEqual.cp` | `src/runtime/test_runner/expect/toContainEqual.rs` |
| `src/test_runner/expect/toContainKey.cp` | `src/runtime/test_runner/expect/toContainKey.rs` |
| `src/test_runner/expect/toContainKeys.cp` | `src/runtime/test_runner/expect/toContainKeys.rs` |
| `src/test_runner/expect/toContainValue.cp` | `src/runtime/test_runner/expect/toContainValue.rs` |
| `src/test_runner/expect/toContainValues.cp` | `src/runtime/test_runner/expect/toContainValues.rs` |
| `src/test_runner/expect/toEndWith.cp` | `src/runtime/test_runner/expect/toEndWith.rs` |
| `src/test_runner/expect/toEqual.cp` | `src/runtime/test_runner/expect/toEqual.rs` |
| `src/test_runner/expect/toEqualIgnoringWhitespace.cp` | `src/runtime/test_runner/expect/toEqualIgnoringWhitespace.rs` |
| `src/test_runner/expect/toHaveBeenCalled.cp` | `src/runtime/test_runner/expect/toHaveBeenCalled.rs` |
| `src/test_runner/expect/toHaveBeenCalledOnce.cp` | `src/runtime/test_runner/expect/toHaveBeenCalledOnce.rs` |
| `src/test_runner/expect/toHaveBeenCalledTimes.cp` | `src/runtime/test_runner/expect/toHaveBeenCalledTimes.rs` |
| `src/test_runner/expect/toHaveBeenCalledWith.cp` | `src/runtime/test_runner/expect/toHaveBeenCalledWith.rs` |
| `src/test_runner/expect/toHaveBeenLastCalledWith.cp` | `src/runtime/test_runner/expect/toHaveBeenLastCalledWith.rs` |
| `src/test_runner/expect/toHaveBeenNthCalledWith.cp` | `src/runtime/test_runner/expect/toHaveBeenNthCalledWith.rs` |
| `src/test_runner/expect/toHaveLastReturnedWith.cp` | `src/runtime/test_runner/expect/toHaveLastReturnedWith.rs` |
| `src/test_runner/expect/toHaveLength.cp` | `src/runtime/test_runner/expect/toHaveLength.rs` |
| `src/test_runner/expect/toHaveNthReturnedWith.cp` | `src/runtime/test_runner/expect/toHaveNthReturnedWith.rs` |
| `src/test_runner/expect/toHaveProperty.cp` | `src/runtime/test_runner/expect/toHaveProperty.rs` |
| `src/test_runner/expect/toHaveReturned.cp` | `src/runtime/test_runner/expect/toHaveReturned.rs` |
| `src/test_runner/expect/toHaveReturnedTimes.cp` | `src/runtime/test_runner/expect/toHaveReturnedTimes.rs` |
| `src/test_runner/expect/toHaveReturnedWith.cp` | `src/runtime/test_runner/expect/toHaveReturnedWith.rs` |
| `src/test_runner/expect/toInclude.cp` | `src/runtime/test_runner/expect/toInclude.rs` |
| `src/test_runner/expect/toIncludeRepeated.cp` | `src/runtime/test_runner/expect/toIncludeRepeated.rs` |
| `src/test_runner/expect/toMatch.cp` | `src/runtime/test_runner/expect/toMatch.rs` |
| `src/test_runner/expect/toMatchInlineSnapshot.cp` | `src/runtime/test_runner/expect/toMatchInlineSnapshot.rs` |
| `src/test_runner/expect/toMatchObject.cp` | `src/runtime/test_runner/expect/toMatchObject.rs` |
| `src/test_runner/expect/toMatchSnapshot.cp` | `src/runtime/test_runner/expect/toMatchSnapshot.rs` |
| `src/test_runner/expect/toSatisfy.cp` | `src/runtime/test_runner/expect/toSatisfy.rs` |
| `src/test_runner/expect/toStartWith.cp` | `src/runtime/test_runner/expect/toStartWith.rs` |
| `src/test_runner/expect/toStrictEqual.cp` | `src/runtime/test_runner/expect/toStrictEqual.rs` |
| `src/test_runner/expect/toThrow.cp` | `src/runtime/test_runner/expect/toThrow.rs` |
| `src/test_runner/expect/toThrowErrorMatchingInlineSnapshot.cp` | `src/runtime/test_runner/expect/toThrowErrorMatchingInlineSnapshot.rs` |
| `src/test_runner/expect/toThrowErrorMatchingSnapshot.cp` | `src/runtime/test_runner/expect/toThrowErrorMatchingSnapshot.rs` |
| `src/test_runner/harness/fixtures.cp` | `src/runtime/test_runner/harness/fixtures.rs` |
| `src/test_runner/harness/recover.cp` | `src/runtime/test_runner/harness/recover.rs` |
| `src/test_runner/jest.cp` | `src/runtime/test_runner/jest.rs` |
| `src/test_runner/pretty_format.cp` | `src/runtime/test_runner/pretty_format.rs` |
| `src/test_runner/snapshot.cp` | `src/runtime/test_runner/snapshot.rs` |
| `src/test_runner/timers/FakeTimers.cp` | `src/runtime/test_runner/timers/FakeTimers.rs` |
| `src/threading/Condition.cp` | `src/threading/Condition.rs` |
| `src/threading/Futex.cp` | `src/threading/Futex.rs` |
| `src/threading/Mutex.cp` | `src/threading/Mutex.rs` |
| `src/threading/ThreadPool.cp` | `src/bundler/ThreadPool.rs`, `src/threading/ThreadPool.rs` |
| `src/threading/WaitGroup.cp` | `src/threading/WaitGroup.rs` |
| `src/threading/channel.cp` | `src/runtime/cli/test/parallel/Channel.rs`, `src/threading/channel.rs` |
| `src/threading/guarded.cp` | `src/threading/guarded.rs` |
| `src/threading/threading.cp` | _需按子系统/函数手工定位_ |
| `src/threading/unbounded_queue.cp` | `src/threading/unbounded_queue.rs` |
| `src/threading/work_pool.cp` | `src/threading/work_pool.rs` |
| `src/unicode/uucode/grapheme_gen.cp` | `src/unicode/uucode/grapheme_gen.rs` |
| `src/unicode/uucode/lut.cp` | `src/unicode/uucode/lut.rs` |
| `src/unicode/uucode/uucode_config.cp` | `src/unicode/uucode/uucode_config.rs` |
| `src/unicode/uucode_lib/build.cp` | `src/install/build.rs`, `src/jsc/build.rs`, `src/runtime/build.rs` |
| `src/unicode/uucode_lib/src/ascii.cp` | _需按子系统/函数手工定位_ |
| `src/unicode/uucode_lib/src/config.cp` | `src/jsc/config.rs` |
| `src/unicode/uucode_lib/src/get.cp` | _需按子系统/函数手工定位_ |
| `src/unicode/uucode_lib/src/grapheme.cp` | `src/bun_core/string/immutable/grapheme.rs` |
| `src/unicode/uucode_lib/src/root.cp` | `src/md/root.rs` |
| `src/unicode/uucode_lib/src/types.cp` | `src/md/types.rs`, `src/runtime/node/types.rs` |
| `src/unicode/uucode_lib/src/utf8.cp` | _需按子系统/函数手工定位_ |
| `src/unicode/uucode_lib/src/x/config.x.cp` | _需按子系统/函数手工定位_ |
| `src/unicode/uucode_lib/src/x/config_x/grapheme_break.cp` | _需按子系统/函数手工定位_ |
| `src/unicode/uucode_lib/src/x/config_x/wcwidth.cp` | _需按子系统/函数手工定位_ |
| `src/unicode/uucode_lib/src/x/grapheme.cp` | `src/bun_core/string/immutable/grapheme.rs` |
| `src/unicode/uucode_lib/src/x/root.cp` | `src/md/root.rs` |
| `src/unicode/uucode_lib/src/x/types.x.cp` | _需按子系统/函数手工定位_ |
| `src/unicode/uucode_lib/src/x/types_x/grapheme.cp` | `src/bun_core/string/immutable/grapheme.rs` |
| `src/unit_test.cp` | `src/unit_test.rs` |
| `src/url/url.cp` | `src/css/values/url.rs`, `src/jsc/URL.rs` |
| `src/url_jsc/url_jsc.cp` | _需按子系统/函数手工定位_ |
| `src/uws/uws.cp` | _需按子系统/函数手工定位_ |
| `src/uws_sys/App.cp` | `src/uws_sys/App.rs` |
| `src/uws_sys/BodyReaderMixin.cp` | `src/uws_sys/BodyReaderMixin.rs` |
| `src/uws_sys/ConnectingSocket.cp` | `src/uws_sys/ConnectingSocket.rs` |
| `src/uws_sys/InternalLoopData.cp` | `src/uws_sys/InternalLoopData.rs` |
| `src/uws_sys/ListenSocket.cp` | `src/uws_sys/ListenSocket.rs` |
| `src/uws_sys/Loop.cp` | `src/uws_sys/Loop.rs` |
| `src/uws_sys/Request.cp` | `src/runtime/webcore/Request.rs`, `src/uws_sys/Request.rs` |
| `src/uws_sys/Response.cp` | `src/runtime/webcore/Response.rs`, `src/uws_sys/Response.rs` |
| `src/uws_sys/SocketContext.cp` | `src/uws_sys/SocketContext.rs` |
| `src/uws_sys/SocketGroup.cp` | `src/uws_sys/SocketGroup.rs` |
| `src/uws_sys/SocketKind.cp` | `src/uws_sys/SocketKind.rs` |
| `src/uws_sys/Timer.cp` | `src/runtime/timer/Timer.rs`, `src/uws_sys/Timer.rs` |
| `src/uws_sys/WebSocket.cp` | `src/http/websocket.rs`, `src/uws_sys/WebSocket.rs` |
| `src/uws_sys/h3.cp` | `src/uws_sys/h3.rs` |
| `src/uws_sys/quic.cp` | `src/uws_sys/quic.rs` |
| `src/uws_sys/quic/Context.cp` | `src/css/context.rs`, `src/options_types/context.rs`, `src/uws_sys/quic/Context.rs` |
| `src/uws_sys/quic/Header.cp` | `src/uws_sys/quic/Header.rs` |
| `src/uws_sys/quic/PendingConnect.cp` | `src/http/h2_client/PendingConnect.rs`, `src/http/h3_client/PendingConnect.rs`, `src/uws_sys/quic/PendingConnect.rs` |
| `src/uws_sys/quic/Socket.cp` | `src/uws_sys/quic/Socket.rs`, `src/uws_sys/socket.rs` |
| `src/uws_sys/quic/Stream.cp` | `src/http/h2_client/Stream.rs`, `src/http/h3_client/Stream.rs`, `src/uws_sys/quic/Stream.rs` |
| `src/uws_sys/socket.cp` | `src/uws_sys/quic/Socket.rs`, `src/uws_sys/socket.rs` |
| `src/uws_sys/udp.cp` | `src/uws_sys/udp.rs` |
| `src/uws_sys/us_socket_t.cp` | `src/uws_sys/us_socket_t.rs` |
| `src/uws_sys/vtable.cp` | `src/uws_sys/vtable.rs` |
| `src/valkey/valkey_protocol.cp` | `src/valkey/valkey_protocol.rs` |
| `src/watcher/INotifyWatcher.cp` | `src/watcher/INotifyWatcher.rs` |
| `src/watcher/KEventWatcher.cp` | `src/watcher/KEventWatcher.rs` |
| `src/watcher/Watcher.cp` | `src/watcher/Watcher.rs` |
| `src/watcher/WatcherTrace.cp` | `src/watcher/WatcherTrace.rs` |
| `src/watcher/WindowsWatcher.cp` | `src/watcher/WindowsWatcher.rs` |
| `src/which/which.cp` | `src/runtime/shell/builtin/which.rs` |
| `src/windows_sys/externs.cp` | `src/windows_sys/externs.rs` |
| `src/workaround_missing_symbols.cp` | `src/workaround_missing_symbols.rs` |
| `src/wyhash/wyhash.cp` | _需按子系统/函数手工定位_ |
| `src/zlib/zlib.cp` | `src/http/zlib.rs` |
| `src/zlib_sys/posix.cp` | `src/zlib_sys/posix.rs` |
| `src/zlib_sys/shared.cp` | `src/ptr/shared.rs`, `src/zlib_sys/shared.rs` |
| `src/zlib_sys/win32.cp` | `src/zlib_sys/win32.rs` |
| `src/zstd/zstd.cp` | _需按子系统/函数手工定位_ |
