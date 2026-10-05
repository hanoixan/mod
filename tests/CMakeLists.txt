
# The one translation unit that defines doctest's main, generated so that no test
# source defines doctest configuration macros.
set(MOD_TEST_MAIN ${CMAKE_CURRENT_BINARY_DIR}/generated/test_main.cpp)
file(CONFIGURE OUTPUT ${MOD_TEST_MAIN} CONTENT [=[
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
]=])
add_library(mod_test_main OBJECT ${MOD_TEST_MAIN})
target_link_libraries(mod_test_main PUBLIC doctest::doctest)

# Scratch space for tests that need files; never the source tree.
set(MOD_TEST_SCRATCH ${CMAKE_CURRENT_BINARY_DIR}/scratch)
file(MAKE_DIRECTORY ${MOD_TEST_SCRATCH})

set(MOD_TESTS
  doc_search_test
  docs_test
  document_list_test
  editor_test
  editor_view_test
  event_queue_test
  hash_test
  history_preview_test
  history_view_test
  input_test
  json_test
  keymap_test
  keymap_view_test
  list_cursor_test
  lsp_client_test
  markdown_links_test
  markdown_test
  menu_test
  piece_tree_test
  progress_test
  read_only_test
  search_test
  settings_test
  settings_view_test
  sidecar_test
  undo_tree_test
  wrap_test
  color_theme_test
  colors_view_test
  syntax_highlighter_test
  terminal_output_test
  markdown_render_test
  help_viewer_test
  confirm_bar_test
  file_listing_test
  text_field_test
  file_dialog_test
  split_layout_test
  startup_test
  reading_layout_test
  cli_options_test
  history_model_test
  process_test
  workspace_test
  folder_tree_test
  folder_tree_view_test
  app_test
  path_text_test
)

foreach(name IN LISTS MOD_TESTS)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE mod_core mod_test_main doctest::doctest)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${name} PRIVATE MOD_TEST_SCRATCH="${MOD_TEST_SCRATCH}" MOD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  mod_target_options(${name})
  add_test(NAME ${name} COMMAND ${name})
endforeach()

# Gigabyte files through the real editor: minutes long and several GB of disk, so the
# label keeps it out of the ordinary run; `ctest --preset linux-release-stress` runs it.
add_executable(stress_test stress_test.cpp)
target_link_libraries(stress_test PRIVATE mod_core mod_test_main doctest::doctest)
target_include_directories(stress_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stress_test PRIVATE MOD_TEST_SCRATCH="${MOD_TEST_SCRATCH}")
mod_target_options(stress_test)
add_test(NAME stress_test COMMAND stress_test)
set_tests_properties(stress_test PROPERTIES LABELS stress TIMEOUT 7200)

# The scripted language server lsp_client_test drives. It defines its own main and is
# a helper, not a test, so it is built outside the loop and never registered.
add_executable(fake_lsp_server fake_lsp_server.cpp)
target_link_libraries(fake_lsp_server PRIVATE mod_core)
mod_target_options(fake_lsp_server)
add_dependencies(lsp_client_test fake_lsp_server)
target_compile_definitions(lsp_client_test PRIVATE MOD_FAKE_LSP_SERVER="$<TARGET_FILE:fake_lsp_server>")
