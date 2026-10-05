---
role: test
stamp: source d157e5cc, stand-in 44608300
---
# module: file_dialog_test

[FileDialog](../src/ui/file_dialog.hpp.skel.md#class-filedialog) over a scratch directory (folders `docs` and `src`, files `a.txt`, `b.md`, `c.txt`, `.hidden`), driving `handle_key` with a fake clock: the dialog opens on `list` with nothing selected; Up from none selects the first entry, Down and Up move, PageDown and PageUp move by `list_rows`, Home and End; selecting a file copies its name to the File field, selecting a folder does not; Enter on a folder enters it (breadcrumb, listing, selection and scroll reset) and Backspace goes to the parent; Enter on a file returns `chosen` with the joined path; with nothing selected Enter submits the File field when not empty and does nothing when empty; a File field holding an absolute path is chosen as is; submitting the name of a folder enters it; Tab and Shift+Tab walk the twelve zones in order and wrap; Left and Right move between zones from the list, the buttons and the ends of the File field, and not from inside the text; Enter on `up`, `home`, `hidden` (listing changes, `◉`/`○`), `cancel` (`canceled`), `submit`, and on each sort header (new column ascending, same column flips, directories stay first and ascending); Escape cancels; typing letters in the list selects the first name containing them ignoring case, accumulates until 2 s pass, restarts after, and Shift+Down and Shift+Up step through further matches; typing in the File field edits it. `folder` then Enter shows the field, typing a name and Enter creates the folder and refreshes, an empty name creates nothing, Escape leaves the mode without creating, and a failure (a read-only directory) shows its error and keeps the dialog usable; the `filter` zone with Enter shows the field, Enter applies `*.txt` (only `a.txt` and `c.txt` and the folders remain), a blank text keeps the old filter, Escape and Tab leave the mode without applying (dialog text modes); an unreadable directory shows the error row and an empty list, and Up still works. `render` to a `Screen`: the title, breadcrumb with ` > `, toolbar, header with the sort arrow, separators, list rows with icon, name, size and `MM-DD HH:MM`, the selected row and the focused control in the focus attribute, the error replacing the last list row, `File:` and the field, `[Open]` or `[Save]` and `[Cancel]`, `N of M` or `M items`, the search mark; a body of one row; narrow widths clip. `list_rows_for` is `body − 8`, at least 1.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** the type-to-search timeout uses the injected `clock`; modified times and the time zone are fixed as in [file_listing_test](./file_listing_test.cpp.skel.md).
- **Depends on:** [FileDialog](../src/ui/file_dialog.hpp.skel.md#class-filedialog)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Depends on:** [make_directory](../src/app/file_listing.hpp.skel.md#function-make_directory)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
