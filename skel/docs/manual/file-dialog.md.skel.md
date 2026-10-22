---
role: product
stamp: source 2b45c814, stand-in f8f0d734
---
# resource: file-dialog.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: the file dialog for Open, Save As and a first save: its parts (breadcrumb, toolbar with Up, Home, +Folder, the Hidden toggle shown by default and the filter, the sortable list with folders first, the File field and buttons), a table of its keys, typing a path (a leading `~/` is the home folder, which on Windows is `%USERPROFILE%`, and there `~` also works in a file named on the command line), and the question before replacing a file.

- **Required:** optional — the dialog works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) checks that every page is reachable from the index.
- **Depends on:** [FileDialog](../../src/ui/file_dialog.hpp.skel.md#class-filedialog)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
