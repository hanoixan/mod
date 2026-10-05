---
role: product
stamp: source 63ab41b9, stand-in 881237d3
---
# module: file_dialog

../modi/'s (after ../modi/) Open and Save As dialog. It replaces the whole text area (the status line stays), and is driven by the keyboard only: there is no mouse. Its looks come from the [theme](./theme.hpp.skel.md): text in `text`, rules and secondary text in `gutter`, the focused zone and selection in `listSelected`, links in `markdownLinkText`, the primary button in `gutterCurrent`, errors in `error`. App opens it for Open, Save As and the first save of an untitled document.

Layout, top to bottom, in the `body` rows the dialog is given (the *list rows* are what is left after the fixed rows, at least 1):

1. **Title:** two `─`, a space, `Open File` or `Save As`, a space, then `─` to the right edge. Dim.
2. **Breadcrumb:** a space, then the folders of the current directory as `/ > home > user`, in the link attribute, with ` > ` between them.
3. **Toolbar:** a space, `[↑Up]`, `[⌂Home]`, then either `[+Folder]` or, in new-folder mode, a text field with the placeholder `folder name`; then, right-aligned, `◉Hidden` (hidden files shown) or `○Hidden`, and the filter, as `[*.*]` or, in filter-edit mode, a text field holding the pattern.
4. **Header:** a space, then `Name` (55% of the width), `Size` (20%, right-aligned) and `Modified` (20%, right-aligned). The sorted column has ` ▲` (ascending) or ` ▼` after its name.
5. **Separator:** `─` across. Dim.
6. **List:** one row per entry from the scroll position: a space, `📁` for a folder or `📄` for a file, a space, the name (52%, cut to fit), the size (empty for a folder) and the modified time without its year (`MM-DD HH:MM`), both dim and right-aligned. The selected row is in the focus attribute. Blank rows pad the list. When there is an error, it replaces the last list row: a space and the message in the error attribute.
7. **Separator.**
8. **File:** ` File: ` and a text field holding the file name.
9. **Actions:** a space, `[Open]` or `[Save]` (primary) and `[Cancel]`; right-aligned, a type-to-search mark `⌕ text` while a search is active, and `N of M` for the selected entry or `M items` when none is selected.

A **focus zone** is where keys go; the focused button, header or list is drawn in `listSelected`. The zones in Tab order are `up`, `home`, `folder`, `hidden`, `filter`, `sort_name`, `sort_size`, `sort_modified`, `list`, `filename`, `submit`, `cancel`. The dialog opens on `list`.

- **Owns:** the current directory, the entries, the filter, the hidden flag, the sort, the selection, the scroll position, the focus zone, the file name field, the type-to-search buffer and the error.
- **Access:** public. One instance at a time, owned by [App](../app/app.hpp.skel.md#class-app), which also keeps it while the overwrite question is asked. Main thread.
- **Required:** always.
- **Failure modes:** an unreadable directory shows its error and an empty list; the dialog stays usable (Up and Home still work). A directory removed while the dialog is open shows the error on the next refresh.
- **Depends on:** [list_directory](../app/file_listing.hpp.skel.md#function-list_directory)
- **Depends on:** [sort_entries](../app/file_listing.hpp.skel.md#function-sort_entries)
- **Depends on:** [format_size](../app/file_listing.hpp.skel.md#function-format_size)
- **Depends on:** [path_parts](../app/file_listing.hpp.skel.md#function-path_parts)
- **Depends on:** [make_directory](../app/file_listing.hpp.skel.md#function-make_directory)
- **Depends on:** [TextField](./text_field.hpp.skel.md#class-textfield)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none. In the filter and new-folder fields Enter applies the field (the filter, or creates the folder) and leaves the mode; Escape leaves the mode without applying it and does not close the dialog; Tab and Shift+Tab leave the mode without applying and move the focus as usual (../modi/'s (after ../modi/) browser inputs let Tab through but leave the mode on, which shows a stale field).

## symbol: FileDialogMode

`enum class FileDialogMode { open, save }`.

- **Access:** public.

## symbol: FileDialogResult

`{ enum class Kind { pending, chosen, canceled } kind; std::filesystem::path path; }`. `chosen` carries the full path the user picked: the current directory joined with the file name (an absolute name is taken as is).

- **Access:** public.

## class: FileDialog

- **Inputs:** `mode`; `initial_dir`: the directory to show (App passes the document's folder, or the working directory when untitled); `initial_filename`: for Save As, the document's file name, else empty; `clock`: a `std::function<double()>` returning seconds (default: the steady clock), the test seam for the type-to-search timeout. `show_hidden`: whether dot-files are listed to begin with, true by default (they are shown unless the Hidden toggle hides them; ../modi/ hid them).
- **State changes:** invariants: `selected` is -1 or an index of `entries`; the scroll position keeps the selected row visible after every key; every change of directory, filter or hidden flag refreshes the listing, clears the error and the selection and scrolls to the top, and leaves new-folder mode.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [file_dialog (implementation)](./file_dialog.cpp.skel.md)
- **Referred by:** [file_dialog_test](../../tests/file_dialog_test.cpp.skel.md)
- **Referred by:** [file-dialog.md](../../docs/manual/file-dialog.md.skel.md)

### function: handle_key

../modi/'s (after ../modi/) `_handle_dialog_key` for the file dialog.

- **Inputs:** `key`: a `KeyEvent`; `list_rows`: the list rows of the current layout, which the dialog needs for PageUp, PageDown and scrolling.
- **Returns:** a `FileDialogResult`: `chosen` when Enter submits a file name, `canceled` on Escape or `[Cancel]`, otherwise `pending`.
- **State changes:**
  - **Tab** and **Shift+Tab** move to the next or previous zone, wrapping; when the file name field had focus, nothing else changes. **Escape** cancels.
  - **In the list:** Up and Down move the selection (Up from none selects the first), PageUp and PageDown move it by a page, Home and End go to the first and last entry; each of these, when the selected entry is a file, copies its name into the File field, and keeps the selection visible. Left and Right (without Shift) move the focus to the previous or next zone. Shift+Up and Shift+Down select the previous or next entry whose name contains the search text. Enter on a folder enters it; on a file it submits that file's name; with nothing selected it submits the File field when that is not empty. Backspace goes to the parent folder. A printable character without Ctrl adds to the **type-to-search** text (which restarts when 2 s have passed since the last character, by `clock`) and selects the first entry, from the current selection on, whose name contains the text, ignoring case.
  - **File field:** keys go to its [TextField](./text_field.hpp.skel.md#class-textfield); Enter submits the field's text; Left at the field's start and Right at its end move the focus to the previous or next zone.
  - **Buttons and headers** (any other zone): Left and Right move the focus to the previous or next zone; Enter activates: `up` goes to the parent; `home` to the home directory; `folder` enters new-folder mode; `hidden` toggles hidden files; `filter` enters filter-edit mode; a `sort_…` header sorts by that column, or flips the direction when it already is the sorted column (a new column starts ascending); `submit` submits the File field; `cancel` cancels.
  - **Submit:** an empty name does nothing. A name that is a directory enters it. Otherwise the result is `chosen`. (For Save As, App asks before replacing a file that exists; the dialog does not.)
  - **New-folder mode** (the field is shown in the toolbar, with focus): Enter creates the folder (the text stripped; nothing when empty) under the current directory with [make_directory](../app/file_listing.hpp.skel.md#function-make_directory), refreshes the listing, and leaves the mode; a failure shows its message in the error row.
  - **Filter-edit mode:** Enter sets the filter to the stripped text (a blank text keeps the old filter), leaves the mode and refreshes the listing.
- **Access:** App.dispatch_key while the dialog is open.

### function: handle_paste

- **Inputs:** `bytes`.
- **Returns:** nothing.
- **State changes:** pastes into the text field that has focus (the File field, the filter or the folder name); ignored in any other zone.
- **Access:** App.dispatch, for a `PasteEvent`.

### function: render

- **Inputs:** a `Screen&`; `area`: a [Rect](./screen.hpp.skel.md#symbol-rect), the body rows.
- **Returns:** nothing.
- **State changes:** draws the layout in the module. The screen cursor is shown in the text field that has focus, and hidden otherwise.
- **Access:** App.render.

### function: list_rows_for

- **Inputs:** `body_rows`.
- **Returns:** `max(body_rows - 8, 1)`: the list rows for a body of that height (8 fixed rows: title, breadcrumb, toolbar, header, two separators, File and actions).
- **State changes:** none.
- **Access:** App.layout, so the dialog and the layout agree.

### function: current_dir

- **Inputs:** none.
- **Returns:** the current directory; `filename()` returns the File field's text, `focus()` the zone, `selected()` the selected index, `entries()` the listing in its order, `error()` the error text or empty, `search_text()` the type-to-search text, `filter()` the pattern and `show_hidden()` the flag.
- **State changes:** none.
- **Access:** App (to restore the dialog after the overwrite question) and tests.
