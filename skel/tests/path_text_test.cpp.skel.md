---
role: test
stamp: source 62946304, stand-in 9e40dc3c
---
# module: path_text_test

On Windows (compiled only under the MSYS2 runtime): `/c/Windows/notepad.exe` shows as `C:\Windows\notepad.exe`; `C:\Windows\notepad.exe` and `C:/Windows` are taken as `/c/…`; a POSIX path and a plain relative name are taken as they are, and `docs\notes.md` as `docs/notes.md`; `uri_path` gives `C:/Users/me/a.txt` and [file_uri](../src/syntax/lsp_client.hpp.skel.md#function-file_uri) `file:///C%3A/Users/me/a%20b.txt`. Elsewhere: every path is shown, taken and sent as it is, a backslash included, and `file_uri` is unchanged. Home (with `HOME`, `USERPROFILE` and `XDG_CONFIG_HOME` set for the test through [EnvVar](./env_var.hpp.skel.md#class-envvar)): on Windows `user_home_dir` is `USERPROFILE` in POSIX form even with `HOME` set, `user_config_dir` is under it, `expand_home` takes `~`, `~/` and `~\`, and `path_from_user` expands `~/…` and `~\…` but not `~me/a`; elsewhere `user_home_dir` is `$HOME`, `expand_home` takes `~` and `~/` but not `~\`, and `path_from_user` leaves `~/notes.md` as it is. Everywhere: an empty text, `~me/a`, `a/~/b` and `notes~` are unchanged; with no home `~/a` stays, `user_home_dir` and `user_config_dir` fail, and `XDG_CONFIG_HOME` still gives `<it>/mod`.

- **Owns:** nothing.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [display_path](../src/platform/path_text.hpp.skel.md#function-display_path)
- **Depends on:** [path_from_user](../src/platform/path_text.hpp.skel.md#function-path_from_user)
- **Depends on:** [uri_path](../src/platform/path_text.hpp.skel.md#function-uri_path)
- **Depends on:** [expand_home](../src/platform/path_text.hpp.skel.md#function-expand_home)
- **Depends on:** [user_home_dir](../src/platform/fs.hpp.skel.md#function-user_home_dir)
- **Depends on:** [user_config_dir](../src/platform/fs.hpp.skel.md#function-user_config_dir)
- **Depends on:** [EnvVar](./env_var.hpp.skel.md#class-envvar)
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
- **Unknowns:** none
