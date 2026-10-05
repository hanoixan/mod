---
role: product
unit: ./fs.hpp.skel.md
stamp: source 8bc91dad, stand-in 8414e717
---
# module: fs_posix

Implements [fs.hpp](./fs.hpp.skel.md) with `stat`, `realpath`, `mkstemp`-style creation (`open(O_CREAT|O_EXCL|O_CLOEXEC, 0600)` with a random suffix), `fsync`, `fchmod`, `fchown`, `rename` and a directory `fsync`. The `not_atomic` checks use `stat` (`st_nlink`, `st_uid`, `st_gid`), `access(dir, W_OK)` as a hint only (the authoritative answer is the `open` of the temp file), and the copy of extended attributes and ACLs onto the temp file: `flistxattr`, `fgetxattr` and `fsetxattr` for every attribute (Linux, macOS and MSYS). On Linux, POSIX ACLs travel as the `system.posix_acl_access` and `system.posix_acl_default` attributes, so the attribute copy carries them and no `libacl` is linked (keeping the two-dependency rule). On macOS, the ACL is copied with `acl_get_fd` and `acl_set_fd` from libSystem. Under the MSYS environment, attribute calls that the Cygwin layer reports as unsupported (`ENOTSUP`) on a target that has no attributes are not failures. [write_in_place](./fs.hpp.skel.md#function-write_in_place) creates its stage file with `open(O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC, 0600)` in `$TMPDIR` (else `/tmp`), then opens the target with `open(O_WRONLY|O_CLOEXEC)` without `O_TRUNC` and copies with `pread`/`pwrite` loops (or `copy_file_range` on Linux), then `ftruncate` and `fsync`.

- **Owns:** nothing persistent.
- **Access:** internal.
- **Required:** always. These POSIX backends are the only ones: Linux, macOS and the MSYS2 MSYS environment all build them.
- **Failure modes:** the macOS `fsync` does not flush the drive cache. Use `fcntl(F_FULLFSYNC)` when it is available, and fall back to `fsync`.
- **Depends on:** [write_atomically](./fs.hpp.skel.md#function-write_atomically)
- **Depends on:** [write_in_place](./fs.hpp.skel.md#function-write_in_place)
- **Depends on:** [save_temp_file](../../infra/storage.iac.skel.md#resource-save_temp_file)
- **Depends on:** [in_place_stage_file](../../infra/storage.iac.skel.md#resource-in_place_stage_file)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
