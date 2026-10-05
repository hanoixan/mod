cmake_minimum_required(VERSION 3.25)
project(mod VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  message(FATAL_ERROR "mod needs GCC 13 or later (found ${CMAKE_CXX_COMPILER_VERSION}); "
                      "configure with -DCMAKE_CXX_COMPILER=g++-13 or use a preset")
endif()

include(CheckCXXSourceCompiles)
check_cxx_source_compiles([=[
#include <expected>
int main() { std::expected<int, int> e = 1; return *e - 1; }
]=] MOD_HAVE_STD_EXPECTED)
if(NOT MOD_HAVE_STD_EXPECTED)
  message(FATAL_ERROR "the compiler's standard library lacks std::expected; use g++-13 or later")
endif()

option(MOD_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(MOD_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (Debug)" OFF)
option(MOD_WERROR "Treat warnings as errors" OFF)
option(MOD_BUILD_FUZZERS "Build the libFuzzer targets (clang only)" OFF)

find_package(Threads REQUIRED)
include(FetchContent)
# The fetched dependencies are built into mod, never installed: on CMake 3.28 and later
# their own install rules are left out with EXCLUDE_FROM_ALL.
set(MOD_FETCH_EXCLUDE)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  set(MOD_FETCH_EXCLUDE EXCLUDE_FROM_ALL)
endif()
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)

# Applied to mod's own targets only, never to fetched dependencies.
function(mod_target_options target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(MOD_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
  if(MOD_SANITIZE)
    # Undefined behavior aborts, so a test run that meets it fails instead of printing on.
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# PCRE2 for regex search: the 8-bit library only, static, with JIT.
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_STATIC_PIC ON CACHE BOOL "" FORCE)
set(MOD_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare(pcre2
  GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git
  GIT_TAG 6f9d7c1373262c541324a16a358785b33ef116cf  # pcre2-10.49
  ${MOD_FETCH_EXCLUDE}
)
FetchContent_MakeAvailable(pcre2)
set(BUILD_SHARED_LIBS ${MOD_SAVED_BUILD_SHARED_LIBS})

# The default language configuration, embedded as a raw string literal.
set(MOD_LANGUAGES_JSON ${CMAKE_CURRENT_SOURCE_DIR}/config/languages.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MOD_LANGUAGES_JSON})
file(READ ${MOD_LANGUAGES_JSON} MOD_DEFAULT_LANGUAGES_JSON)
string(FIND "${MOD_DEFAULT_LANGUAGES_JSON}" ")mod_json\"" MOD_DELIMITER_AT)
if(NOT MOD_DELIMITER_AT EQUAL -1)
  message(FATAL_ERROR "config/languages.json contains the raw-string delimiter )mod_json\"")
endif()
set(MOD_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(CONFIGURE OUTPUT ${MOD_GENERATED_DIR}/default_languages.inc
  CONTENT "inline constexpr std::string_view kDefaultLanguagesJson = R\"mod_json(@MOD_DEFAULT_LANGUAGES_JSON@)mod_json\";\n"
  @ONLY)

# Every source except main.cpp, so that tests can link it.
set(MOD_CORE_SOURCES
  src/app/app.cpp
  src/app/commands.cpp
  src/app/doc_search.cpp
  src/app/file_listing.cpp
  src/app/document_list.cpp
  src/app/document_slot.cpp
  src/app/keymap.cpp
  src/app/read_only.cpp
  src/app/settings.cpp
  src/app/split_layout.cpp
  src/app/startup.cpp
  src/ui/reading_layout.cpp
  src/app/cli_options.cpp
  src/app/folder_tree.cpp
  src/app/history_preview.cpp
  src/app/workspace.cpp
  src/edit/clipboard.cpp
  src/edit/document.cpp
  src/edit/editor.cpp
  src/edit/sidecar.cpp
  src/edit/undo_tree.cpp
  src/platform/file_map_posix.cpp
  src/platform/fs_posix.cpp
  src/platform/process_posix.cpp
  src/platform/terminal_posix.cpp
  src/platform/terminal_output.cpp
  src/search/regex.cpp
  src/search/search.cpp
  src/syntax/json.cpp
  src/syntax/language_config.cpp
  src/syntax/lsp_client.cpp
  src/syntax/lsp_pool.cpp
  src/syntax/markdown.cpp
  src/syntax/semantic_highlighter.cpp
  src/syntax/syntax_highlighter.cpp
  src/syntax/layered_highlighter.cpp
  src/syntax/markdown_render.cpp
  src/text/line_scanner.cpp
  src/text/piece_tree.cpp
  src/text/utf8.cpp
  src/text/wrap.cpp
  src/ui/doc_search_view.cpp
  src/ui/editor_view.cpp
  src/ui/history_view.cpp
  src/ui/input.cpp
  src/ui/folder_tree_view.cpp
  src/ui/keymap_view.cpp
  src/ui/list_cursor.cpp
  src/ui/menu.cpp
  src/ui/prompt.cpp
  src/ui/screen.cpp
  src/ui/settings_view.cpp
  src/ui/theme.cpp
  src/ui/colors_view.cpp
  src/ui/confirm_bar.cpp
  src/ui/file_dialog.cpp
  src/ui/text_field.cpp
  src/ui/help_viewer.cpp
  src/util/hash.cpp
  src/util/log.cpp
)

add_library(mod_core STATIC ${MOD_CORE_SOURCES})
# The one place the version is written: project() above.
target_compile_definitions(mod_core PUBLIC MOD_VERSION="${PROJECT_VERSION}")
target_include_directories(mod_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src PRIVATE ${MOD_GENERATED_DIR})
target_link_libraries(mod_core PUBLIC Threads::Threads PRIVATE pcre2-8-static)
mod_target_options(mod_core)
if(MOD_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "MOD_BUILD_FUZZERS needs clang (libFuzzer)")
  endif()
  # Coverage for the fuzzers, and the sanitizers they run under, across the core.
  target_compile_options(mod_core PUBLIC -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  target_link_options(mod_core PUBLIC -fsanitize=address,undefined)
endif()
if(MOD_SANITIZE)
  # Consumers of the static library must link the sanitizer runtimes too.
  target_link_options(mod_core INTERFACE -fsanitize=address,undefined)
endif()

add_executable(mod src/main.cpp)
target_link_libraries(mod PRIVATE mod_core)
mod_target_options(mod)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Runs on a stock Ubuntu 22.04, whose system libstdc++ is older than GCC 13's.
  target_link_options(mod PRIVATE -static-libstdc++)
endif()
install(TARGETS mod)

# The manual and a reference settings.json, under <prefix>/share/mod. The defaults file is
# generated from the settings schema, so it can never disagree with the code.
include(GNUInstallDirs)
add_executable(gen_default_settings tools/gen_default_settings.cpp)
target_link_libraries(gen_default_settings PRIVATE mod_core)
mod_target_options(gen_default_settings)
set(MOD_DEFAULT_SETTINGS ${CMAKE_BINARY_DIR}/generated/settings.json)
add_custom_command(OUTPUT ${MOD_DEFAULT_SETTINGS}
  COMMAND gen_default_settings > ${MOD_DEFAULT_SETTINGS}
  DEPENDS gen_default_settings
  COMMENT "Generating the default settings.json")
add_custom_target(default_settings ALL DEPENDS ${MOD_DEFAULT_SETTINGS})
install(DIRECTORY docs/manual/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mod/doc)
install(FILES docs/sidecar-format.md DESTINATION ${CMAKE_INSTALL_DATADIR}/mod)
install(FILES ${MOD_DEFAULT_SETTINGS} DESTINATION ${CMAKE_INSTALL_DATADIR}/mod)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/mod)

# Linux packages (tools/ci/package.sh runs `cpack -G "DEB;RPM"`); the Arch package is made
# from the same install tree by packaging/arch/PKGBUILD-bin.in.
set(CPACK_PACKAGE_NAME mod)
set(CPACK_PACKAGE_VENDOR hanoixan)
set(CPACK_PACKAGE_CONTACT "hanoixan <https://github.com/hanoixan/mod>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A terminal text editor")
set(CPACK_PACKAGE_HOMEPAGE_URL https://github.com/hanoixan/mod)
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION editors)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_ARCHITECTURE x86_64)
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_GROUP Applications/Editors)
set(CPACK_RPM_PACKAGE_URL https://github.com/hanoixan/mod)
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/licenses)
include(CPack)
# Where F1 looks for the manual: the installed copy, then the source tree for a build run
# from its build directory.
target_compile_definitions(mod_core PRIVATE
  MOD_INSTALL_DOC_DIR="${CMAKE_INSTALL_FULL_DATADIR}/mod/doc"
  MOD_SOURCE_DOC_DIR="${PROJECT_SOURCE_DIR}/docs/manual")

if(MOD_BUILD_TESTS)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG 2d0a9359a60c51affe2a9bebb1be1dca47868151  # v2.5.3
    ${MOD_FETCH_EXCLUDE}
  )
  FetchContent_MakeAvailable(doctest)
  enable_testing()
  add_subdirectory(tests)
endif()
if(MOD_BUILD_TESTS OR MOD_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()
