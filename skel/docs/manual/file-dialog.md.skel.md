---
role: product
stamp: source 946c93a4, stand-in fbd0d811
---
# resource: file-dialog.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: the file dialog for Open, Save As and a first save: its parts (breadcrumb, whose parts are clicked to go to a folder, toolbar with Up, Home, +Folder, the Hidden toggle shown by default and the filter, the sortable list with folders first, the File field, which has the cursor when the dialog opens, and buttons), a table of its keys (letters typed into the File field from the list too, Right taking the suggestion, Shift+Up and Shift+Down through the names containing the field's text), typing looking the name up (starts with, else contains, ignoring case; the rest of the name shown lighter, or `→` and the whole name; a path not looked up), Enter on a folder's name entering it and emptying the field, typing a path (a leading `~/` is the home folder, which on Windows is `%USERPROFILE%`, and there `~` also works in a file named on the command line), and the question before replacing a file; the mouse: click, double click, buttons, wheel.

- **Required:** optional — the dialog works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) checks that every page is reachable from the index.
- **Depends on:** [FileDialog](../../src/ui/file_dialog.hpp.skel.md#class-filedialog)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
