---
role: product
unit: ./settings.hpp.skel.md
stamp: source b3f869c3, stand-in 1087cfa4
---
# module: settings (implementation)

Implements [Settings](./settings.hpp.skel.md#class-settings) and holds the schema table behind [setting_specs](./settings.hpp.skel.md#function-setting_specs) as a `constexpr` array. Values and modified times are kept in two vectors parallel to the table. The file is small (well under 1 KB), so it is read whole with `std::ifstream` and parsed with [Json](../syntax/json.hpp.skel.md#class-json); a file larger than 1 MiB is rejected as invalid without reading it. Writing serializes the kept object with `Json.dump` and streams it through `write_atomically`. The directory is created with `std::filesystem::create_directories`.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** `config_dir` is a file rather than a directory: `create_directories` fails and `set` returns `io`.
- **Depends on:** [Settings](./settings.hpp.skel.md#class-settings)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
