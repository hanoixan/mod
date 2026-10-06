---
role: test
stamp: source 9c2bbacf, stand-in 1494a577
---
# module: fs_probe

Test helpers that ask the file system under the tests what it does, so tests of behavior it may lack skip (with a doctest `MESSAGE`) instead of failing. On Windows the MSYS2 runtime, mounted `noacl` by default, neither keeps nor enforces permissions and makes symbolic links as copies unless `MSYS=winsymlinks:…` asks otherwise; as root on Linux nothing is unreadable. Each probe works in a scratch folder the test gives it and cleans up after itself.

- **Owns:** nothing.
- **Access:** tests, header-only, namespace `mod::probe`.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none; a probe that cannot tell answers false, so the check is skipped.
- **Depends on:** none
- **Referred by:** [settings_test](./settings_test.cpp.skel.md)
- **Referred by:** [sidecar_test](./sidecar_test.cpp.skel.md)
- **Referred by:** [folder_tree_test](./folder_tree_test.cpp.skel.md)
- **Referred by:** [folder_tree_view_test](./folder_tree_view_test.cpp.skel.md)
- **Referred by:** [file_listing_test](./file_listing_test.cpp.skel.md)
- **Referred by:** [workspace_test](./workspace_test.cpp.skel.md)
- **Unknowns:** none

## function: permissions_kept

- **Inputs:** `dir`.
- **Returns:** whether a file set to 0640 reads back as 0640.
- **State changes:** creates and removes a probe file in `dir`.
- **Access:** tests that check the permissions mod gives a file.

## function: permissions_enforced

- **Inputs:** `dir`.
- **Returns:** whether a file with no permissions cannot be opened: false as root, and where permissions are not enforced.
- **State changes:** creates and removes a probe file in `dir`.
- **Access:** tests of unreadable or unwritable folders.

## function: symlinks_work

- **Inputs:** `dir`.
- **Returns:** whether a symbolic link to a name that does not exist is made, as a link.
- **State changes:** creates and removes a probe link in `dir`.
- **Access:** tests that make symbolic links.
