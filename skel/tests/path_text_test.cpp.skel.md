---
role: test
stamp: source b2aa4478, stand-in 6c5600ac
---
# module: path_text_test

On Windows (compiled only under the MSYS2 runtime): `/c/Windows/notepad.exe` shows as `C:\Windows\notepad.exe`; `C:\Windows\notepad.exe` and `C:/Windows` are taken as `/c/…`; a POSIX path and a plain relative name are taken as they are, and `docs\notes.md` as `docs/notes.md`; `uri_path` gives `C:/Users/me/a.txt` and [file_uri](../src/syntax/lsp_client.hpp.skel.md#function-file_uri) `file:///C%3A/Users/me/a%20b.txt`. Elsewhere: every path is shown, taken and sent as it is, a backslash included, and `file_uri` is unchanged.

- **Owns:** nothing.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [display_path](../src/platform/path_text.hpp.skel.md#function-display_path)
- **Depends on:** [path_from_user](../src/platform/path_text.hpp.skel.md#function-path_from_user)
- **Depends on:** [uri_path](../src/platform/path_text.hpp.skel.md#function-uri_path)
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
- **Unknowns:** none
