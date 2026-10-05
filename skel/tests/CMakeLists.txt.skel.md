---
role: manifest
stamp: source c1c569fc, stand-in 61beeecb
---
# resource: tests/CMakeLists.txt

Defines one test executable per `*_test.cpp`, linked against an internal `mod_core` static library and [doctest](https://github.com/doctest/doctest). The top-level CMake splits every non-`main` source into `mod_core` so that tests can link it, and it fetches doctest at a pinned version (see [CMakeLists.txt](../CMakeLists.txt.skel.md)).

- One small object library, `mod_test_main`, holds a single translation unit that defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` and includes `doctest/doctest.h`. The translation unit is generated into the build directory (`file(CONFIGURE …)`), so it is not a source file. Every test executable links it, so no test file defines `main`.
- Test files include `doctest/doctest.h` and use `TEST_CASE`, `SUBCASE` and `CHECK`/`REQUIRE`. They do not define any doctest configuration macros.
- Each executable is registered with `add_test(NAME <name> COMMAND <name>)`. Per-case discovery (`doctest_discover_tests`) is not used, so CTest needs no extra module from the doctest source tree.
- Tests that need a scratch directory create it under `${CMAKE_CURRENT_BINARY_DIR}` through an injected path, never in the source tree. The path is the compile definition `MOD_TEST_SCRATCH` (a string literal naming `${CMAKE_CURRENT_BINARY_DIR}/scratch`); each test uses a subdirectory named after itself.
- Test executables are listed in one `MOD_TESTS` list and built in a loop; a later batch adds a test by adding its name. Each links `mod_core`, `mod_test_main` and `doctest::doctest`, gets the warning and sanitizer options of `mod_core`, and get the compile definitions `MOD_TEST_SCRATCH` (their scratch folder) and `MOD_SOURCE_DIR` (the source tree, for tests that read checked-in files), and may include files from `tests/` (such as generated `.inc` tables).
- `stress_test` is built like the others (without `MOD_SOURCE_DIR`, which it does not read) but outside the list, and registered with the label `stress` and a two-hour timeout; the ordinary test presets exclude that label, and `linux-release-stress` runs only it.

- **The fake language server** is the one exception to "one executable per `*_test.cpp`". [fake_lsp_server](./fake_lsp_server.cpp.skel.md) is built as `add_executable(fake_lsp_server fake_lsp_server.cpp)` outside the `MOD_TESTS` loop:
  - It links `mod_core` only, not `mod_test_main` and not doctest, because it defines its own `main`. It gets the same warning and sanitizer options.
  - It is never registered with `add_test`.
  - `lsp_client_test` is in `MOD_TESTS`. It additionally gets `add_dependencies(lsp_client_test fake_lsp_server)` and the compile definition `MOD_FAKE_LSP_SERVER="$<TARGET_FILE:fake_lsp_server>"`, an absolute path, so no `PATH` lookup is involved.

- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** tests that need a TTY. None are allowed: the input decoder and screen are tested on byte buffers, and the terminal backend is not unit-tested.
- **Depends on:** [piece_tree_test](./piece_tree_test.cpp.skel.md)
- **Depends on:** [undo_tree_test](./undo_tree_test.cpp.skel.md)
- **Depends on:** [sidecar_test](./sidecar_test.cpp.skel.md)
- **Depends on:** [input_test](./input_test.cpp.skel.md)
- **Depends on:** [search_test](./search_test.cpp.skel.md)
- **Depends on:** [markdown_test](./markdown_test.cpp.skel.md)
- **Depends on:** [json_test](./json_test.cpp.skel.md)
- **Depends on:** [editor_test](./editor_test.cpp.skel.md)
- **Depends on:** [editor_view_test](./editor_view_test.cpp.skel.md)
- **Depends on:** [event_queue_test](./event_queue_test.cpp.skel.md)
- **Depends on:** [wrap_test](./wrap_test.cpp.skel.md)
- **Depends on:** [history_view_test](./history_view_test.cpp.skel.md)
- **Depends on:** [history_preview_test](./history_preview_test.cpp.skel.md)
- **Depends on:** [hash_test](./hash_test.cpp.skel.md)
- **Depends on:** [settings_test](./settings_test.cpp.skel.md)
- **Depends on:** [settings_view_test](./settings_view_test.cpp.skel.md)
- **Depends on:** [menu_test](./menu_test.cpp.skel.md)
- **Depends on:** [markdown_links_test](./markdown_links_test.cpp.skel.md)
- **Depends on:** [read_only_test](./read_only_test.cpp.skel.md)
- **Depends on:** [document_list_test](./document_list_test.cpp.skel.md)
- **Depends on:** [docs_test](./docs_test.cpp.skel.md)
- **Depends on:** [doc_search_test](./doc_search_test.cpp.skel.md)
- **Depends on:** [color_theme_test](./color_theme_test.cpp.skel.md)
- **Depends on:** [colors_view_test](./colors_view_test.cpp.skel.md)
- **Depends on:** [syntax_highlighter_test](./syntax_highlighter_test.cpp.skel.md)
- **Depends on:** [terminal_output_test](./terminal_output_test.cpp.skel.md)
- **Depends on:** [markdown_render_test](./markdown_render_test.cpp.skel.md)
- **Depends on:** [help_viewer_test](./help_viewer_test.cpp.skel.md)
- **Depends on:** [confirm_bar_test](./confirm_bar_test.cpp.skel.md)
- **Depends on:** [file_listing_test](./file_listing_test.cpp.skel.md)
- **Depends on:** [text_field_test](./text_field_test.cpp.skel.md)
- **Depends on:** [file_dialog_test](./file_dialog_test.cpp.skel.md)
- **Depends on:** [split_layout_test](./split_layout_test.cpp.skel.md)
- **Depends on:** [startup_test](./startup_test.cpp.skel.md)
- **Depends on:** [reading_layout_test](./reading_layout_test.cpp.skel.md)
- **Depends on:** [cli_options_test](./cli_options_test.cpp.skel.md)
- **Depends on:** [history_model_test](./history_model_test.cpp.skel.md)
- **Depends on:** [process_test](./process_test.cpp.skel.md)
- **Depends on:** [workspace_test](./workspace_test.cpp.skel.md)
- **Depends on:** [folder_tree_view_test](./folder_tree_view_test.cpp.skel.md)
- **Depends on:** [folder_tree_test](./folder_tree_test.cpp.skel.md)
- **Depends on:** [stress_test](./stress_test.cpp.skel.md)
- **Depends on:** [app_test](./app_test.cpp.skel.md)
- **Depends on:** [keymap_test](./keymap_test.cpp.skel.md)
- **Depends on:** [keymap_view_test](./keymap_view_test.cpp.skel.md)
- **Depends on:** [list_cursor_test](./list_cursor_test.cpp.skel.md)
- **Depends on:** [lsp_client_test](./lsp_client_test.cpp.skel.md)
- **Depends on:** [progress_test](./progress_test.cpp.skel.md)
- **Depends on:** [fake_lsp_server](./fake_lsp_server.cpp.skel.md)
- **Depends on:** [doctest](https://github.com/doctest/doctest)
- **Referred by:** [CMakeLists.txt](../CMakeLists.txt.skel.md)

- **Unknowns:** none
