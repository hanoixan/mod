---
role: test
stamp: source 83f878dd, stand-in 06d1175d
---
# module: file_listing_test

[file_listing](../src/app/file_listing.hpp.skel.md) over a scratch directory: `list_directory` lists files and directories with sizes and `YYYY-MM-DD HH:MM` times (the test sets `TZ=UTC` and the modification times); hides names starting with `.` unless asked; always lists directories; a pattern such as `*.txt` matches ignoring case (`A.TXT`) and `*.*` or empty lists every file, including one with no dot; skips a broken link; follows a link to a directory; an unreadable or missing directory returns an error with the system's message. `sort_entries`: directories first and ascending in every case, files ascending or descending by name (ignoring case), size or modified time, ties stable. `format_size`: `0 B`, `1023 B`, `1.0 KB`, `1.5 KB`, `1.0 MB`, `2.5 GB`. `path_parts` of `/`, `/home/user` and a path with a trailing slash. `make_directory` creates nested folders, accepts one that exists, and reports a failure.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** modified times are set explicitly with `utimensat` and the zone with `TZ`, so the formatted times do not depend on the machine.
- **Depends on:** [list_directory](../src/app/file_listing.hpp.skel.md#function-list_directory)
- **Depends on:** [sort_entries](../src/app/file_listing.hpp.skel.md#function-sort_entries)
- **Depends on:** [format_size](../src/app/file_listing.hpp.skel.md#function-format_size)
- **Depends on:** [path_parts](../src/app/file_listing.hpp.skel.md#function-path_parts)
- **Depends on:** [make_directory](../src/app/file_listing.hpp.skel.md#function-make_directory)
- **Depends on:** [fs_probe](./fs_probe.hpp.skel.md)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
