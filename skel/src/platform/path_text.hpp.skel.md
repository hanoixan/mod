---
role: product
---
# module: path_text

Paths as the user reads and types them, and as native programs on the system expect them. On Windows mod runs on the MSYS2 runtime, so it works with POSIX paths (`/c/Users/me`), but the user decided that Windows users see and type Windows ones (`C:\Users\me`) and that native Windows language servers get Windows paths in their URIs. On Linux and macOS every function returns the path as it is, so nothing changes there.

- **Owns:** nothing.
- **Access:** public, any thread (pure functions).
- **Required:** always.
- **Failure modes:** a path the runtime cannot convert (an invalid drive, a broken mount): the path is returned unconverted, never an error.
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [path_text (implementation)](./path_text.cpp.skel.md)

## function: display_path

- **Inputs:** `path`.
- **Returns:** the path as the user reads it: on Windows its Windows form (`/c/Users/me` → `C:\Users\me`, through `cygwin_conv_path`); elsewhere `path.string()`. Empty for an empty path.
- **State changes:** none.
- **Access:** the views and App, wherever a full path is shown (messages, the file dialog's breadcrumb).
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [file_listing](../app/file_listing.hpp.skel.md)
- **Referred by:** [path_text_test](../../tests/path_text_test.cpp.skel.md)

## function: path_from_user

- **Inputs:** `text`: a path the user typed or passed on the command line.
- **Returns:** on Windows, a text with a drive (`C:…`), a leading `\\` or any backslash is converted to its POSIX form (`C:\x` and `C:/x` → `/c/x`; `docs\a.md` → `docs/a.md`); anything else is taken as it is. Elsewhere the text as it is (a backslash is a legal file-name character there).
- **State changes:** none.
- **Access:** the command-line parser and the file dialog's typed names.
- **Referred by:** [cli_options](../app/cli_options.hpp.skel.md)
- **Referred by:** [file_dialog](../ui/file_dialog.hpp.skel.md)
- **Referred by:** [path_text_test](../../tests/path_text_test.cpp.skel.md)

## function: uri_path

- **Inputs:** `path`: absolute.
- **Returns:** the path part of a `file://` URI before percent-encoding: the generic form elsewhere; on Windows the drive form with forward slashes (`C:/Users/me`), as native servers expect.
- **State changes:** none.
- **Access:** [file_uri](../syntax/lsp_client.hpp.skel.md#function-file_uri).
- **Referred by:** [lsp_client](../syntax/lsp_client.hpp.skel.md)
- **Referred by:** [path_text_test](../../tests/path_text_test.cpp.skel.md)
