---
role: product
stamp: source 324f73ef, stand-in 429b8592
---
# module: file_listing

The directory work behind the file dialog, kept apart from drawing so that it can be tested on its own. It follows ../modi/'s (after ../modi/) `_list_directory`, `_sort_entries`, `_build_path_parts`, `_path_from_parts` and `_format_file_size`.

- **Owns:** the declarations only.
- **Access:** public free functions. Main thread.
- **Required:** always.
- **Failure modes:** listed per function.
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)
- **Depends on:** [browsed_directory](../../infra/storage.iac.skel.md#resource-browsed_directory)
- **Unknowns:** none
- **Referred by:** [file_listing (implementation)](./file_listing.cpp.skel.md)

## symbol: DirEntry

`{ std::string name; bool is_dir; uint64_t size; std::string modified; }`. `size` is 0 for a directory. `modified` is the local modification time as `YYYY-MM-DD HH:MM`.

- **Access:** public.

## symbol: SortColumn

`enum class SortColumn { name, size, modified }`.

- **Access:** public.

## function: list_directory

- **Inputs:** `dir`; `filter`: a shell pattern such as `*.txt` (`*.*` or empty means everything); `show_hidden`: whether names starting with `.` are listed.
- **Returns:** `Result<std::vector<DirEntry>>`, unsorted. An entry that cannot be examined is skipped. The directory itself unreadable gives `permission` or `not_found` with the system's message, which the dialog shows in its error row.
- **State changes:** none.
- **Access:** [FileDialog](../ui/file_dialog.hpp.skel.md#class-filedialog).
- **Depends on:** [browsed_directory](../../infra/storage.iac.skel.md#resource-browsed_directory)
- **Failure modes:** a directory of millions of entries is listed whole, which can take seconds; accepted. A symlink is described by its target (a link to a directory is a directory); a broken link is skipped.
- **Referred by:** [file_dialog](../ui/file_dialog.hpp.skel.md)
- **Referred by:** [file_listing_test](../../tests/file_listing_test.cpp.skel.md)

Directories are always listed, whatever the filter. A file is listed when its name matches the pattern with `fnmatch`, ignoring case.

## function: sort_entries

- **Inputs:** `entries`: a vector, sorted in place; `column`; `ascending`.
- **Returns:** nothing.
- **State changes:** directories come first, in ascending order of the column whatever the direction (../modi/ keeps them first and ascending); then the files, ascending or descending. The `name` column compares names ignoring case, `size` compares numbers, and `modified` compares the formatted strings. Ties keep their order (stable sort).
- **Access:** FileDialog, on a listing and when the user picks a header.
- **Referred by:** [file_dialog](../ui/file_dialog.hpp.skel.md)
- **Referred by:** [file_listing_test](../../tests/file_listing_test.cpp.skel.md)

## function: format_size

- **Inputs:** `bytes`.
- **Returns:** `"123 B"` below 1 KiB, otherwise one decimal and a unit: `"1.5 KB"`, `"2.0 MB"`, `"3.1 GB"` (powers of 1024).
- **State changes:** none.
- **Access:** FileDialog.
- **Referred by:** [file_dialog](../ui/file_dialog.hpp.skel.md)
- **Referred by:** [file_listing_test](../../tests/file_listing_test.cpp.skel.md)

## function: path_parts

- **Inputs:** `dir`.
- **Returns:** `std::vector<std::string>` of the absolute path's parts, starting with `"/"`: `/home/user` gives `{"/", "home", "user"}`.
- **State changes:** none.
- **Access:** FileDialog, for the breadcrumb.
- **Referred by:** [file_dialog](../ui/file_dialog.hpp.skel.md)
- **Referred by:** [file_listing_test](../../tests/file_listing_test.cpp.skel.md)

## function: make_directory

- **Inputs:** `path`: the folder to create; parents are created too, and an existing folder is not an error (`mkdir -p`).
- **Returns:** `Status`; an error carries the system's message.
- **State changes:** creates the directories.
- **Access:** FileDialog, for +Folder.
- **Depends on:** [browsed_directory](../../infra/storage.iac.skel.md#resource-browsed_directory)
- **Referred by:** [file_dialog](../ui/file_dialog.hpp.skel.md)
- **Referred by:** [file_dialog_test](../../tests/file_dialog_test.cpp.skel.md)
- **Referred by:** [file_listing_test](../../tests/file_listing_test.cpp.skel.md)
