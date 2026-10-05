---
role: product
unit: ./language_config.hpp.skel.md
stamp: source bb25f37b, stand-in a958cb11
---
# module: language_config (implementation)

Implements [LanguageConfig](./language_config.hpp.skel.md#class-languageconfig). The defaults are embedded as a string literal generated at configure time from `config/languages.json`, by CMake `file(READ)` and `file(CONFIGURE … @ONLY)` (the content form of `configure_file`, which needs no template file in the source tree) into `${CMAKE_BINARY_DIR}/generated/default_languages.inc`. That file holds one declaration, `inline constexpr std::string_view kDefaultLanguagesJson = R"mod_json(…)mod_json";`, and is included inside the implementation's anonymous namespace; the generated directory is a private include directory of `mod_core`. The defaults are parsed on each `merge`; a parse failure is logged and leaves no defaults.

- **Owns:** the merge logic.
- **Access:** internal.
- **Required:** optional — as for the header.
- **Failure modes:** an embedded default that fails to parse is a build bug, so a unit test parses it.
- **Depends on:** [LanguageConfig](./language_config.hpp.skel.md#class-languageconfig)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
