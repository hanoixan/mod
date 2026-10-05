---
role: product
stamp: source bdb6d2ac, stand-in f25ffbd7
---
# module: fs

Filesystem operations whose semantics differ by OS: stat, real-path resolution, crash-safe atomic replacement, the user-confirmed in-place fallback, and the user configuration directory.

- **Owns:** the declarations only.
- **Access:** public free functions. Main thread, except where noted.
- **Required:** always.
- **Failure modes:** listed per function.
- **Unknowns:** none
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)
- **Depends on:** [FileIdentity](./file_map.hpp.skel.md#symbol-fileidentity)

## function: stat_path

- **Inputs:** `path`.
- **Returns:** `Result<FileIdentity>`, or `not_found`.
- **State changes:** none.
- **Access:** [Document](../edit/document.hpp.skel.md#function-check_external_change), polled on focus and before save.
- **Referred by:** [Document.check_external_change](../edit/document.hpp.skel.md#function-check_external_change)
- **Referred by:** [Document.open](../edit/document.hpp.skel.md#function-open)
- **Referred by:** [Document.save](../edit/document.hpp.skel.md#function-save)

## function: is_writable

- **Inputs:** `path`: an existing file.
- **Returns:** `bool`: whether this process may write it (`access(W_OK)`); false when it does not exist.
- **State changes:** none.
- **Access:** [Document.open](../edit/document.hpp.skel.md#function-open), to mark a read-only file.

## function: resolve_real_path

Resolves symlinks so that saving replaces the link's *target* and leaves the link itself in place.

- **Inputs:** `path`. The final component may not exist yet, for a new file.
- **Returns:** `Result<std::filesystem::path>`: an absolute path with symlinks resolved.
- **State changes:** none.
- **Access:** [Document.open](../edit/document.hpp.skel.md#function-open) and [sidecar_path_for](../edit/sidecar.hpp.skel.md#function-sidecar_path_for).
- **Referred by:** [Document.check_external_change](../edit/document.hpp.skel.md#function-check_external_change)
- **Referred by:** [Document.open](../edit/document.hpp.skel.md#function-open)
- **Referred by:** [Document.save](../edit/document.hpp.skel.md#function-save)
- **Referred by:** [sidecar_path_for](../edit/sidecar.hpp.skel.md#function-sidecar_path_for)

## symbol: ByteSink

`std::function<Status(std::span<const std::byte>)>`: receives the next slice of the content being written.

- **Access:** public.

## symbol: ContentProducer

`std::function<Status(const ByteSink&)>`: streams the whole content into the sink it is given, and returns the first error.

- **Access:** public.

## function: write_atomically

Writes a new version of `target` without ever leaving a half-written file in its place.

- **Inputs:** `target`: the resolved path; `produce`: a `ContentProducer`, a callback `(sink) -> Status` that streams the content into `sink` (a `ByteSink`, `Status(std::span<const std::byte>)`). An error returned by `produce` is returned unchanged; `mode_from`: an optional path whose permission bits are copied. When `mode_from` is absent or no longer exists (a new file, a settings file, or a document whose file was deleted on disk), the new file gets 0666 masked by the process umask, as any newly created file would, and no owner, extended attributes or ACLs are copied. When `mode_from` exists, its permission bits are copied, and the owner, extended attributes and ACLs are copied from `target` (when `target` exists); the owner and attribute checks below apply only then. `mode`: optional explicit permission bits (`mode_t`, the low 12 bits are used). When present it overrides the permission bits that `mode_from` or the umask default would give, and nothing else: whether the owner, extended attributes and ACLs are copied is still decided by `mode_from` alone. The bits are applied with `fchmod`, so the umask does not mask them. [Sidecar.copy_to](../edit/sidecar.hpp.skel.md#function-copy_to) passes the edited file's permission bits this way.
- **Returns:** `Status`. `ErrorCode::not_atomic` when atomic replacement is impossible for this target; see below. The `Error.message` names the reason ("directory not writable", "file has other hard links", …) so that the save prompt can show it.
- **State changes:** creates `<dir>/.<name>.mod-tmp-<random>` in the target's directory (the [save_temp_file](../../infra/storage.iac.skel.md#resource-save_temp_file) resource). It streams the content, `fsync`s, copies the permission bits (and the owner, when running as root), copies **every** extended attribute and ACL of `target` onto the temp file, and renames over `target`. It then `fsync`s the directory. On any failure it removes the temp file and leaves `target` untouched.
- **Access:** [Document.save](../edit/document.hpp.skel.md#function-save), [Sidecar.copy_to](../edit/sidecar.hpp.skel.md#function-copy_to), [Sidecar.rewrite](../edit/sidecar.hpp.skel.md#function-rewrite) and [Settings.set](../app/settings.hpp.skel.md#function-set). The sidecar's normal appends do not use it.
- **Failure modes:** `ENOSPC` gives `no_space`, and the temp file is removed. Crossing a filesystem is impossible because the temp file shares the directory.
- **Referred by:** [Settings.set](../app/settings.hpp.skel.md#function-set)
- **Referred by:** [settings_test](../../tests/settings_test.cpp.skel.md)

#### When atomic replacement is impossible

The function checks whether a rename would lose something or cannot happen, and returns `not_atomic`, with `target` untouched, if so. The first three checks run before anything is created; the fourth runs on the finished temp file just before the rename:

- the target exists and is writable, but its directory is not (`EACCES` or `EPERM` when creating the temp file);
- the target has other hard links (`st_nlink > 1`): a rename would give this name new content and leave the other links with the old;
- the target's owner or group differ from what the new file would get, and the process cannot `fchown` the temp file to match;
- copying one of the target's extended attributes or ACLs onto the temp file fails (for example a kernel-managed attribute such as `security.selinux` or `trusted.*` that an unprivileged process may not set, or a filesystem that does not support the attribute). This check runs after the temp file has been written: the temp file is removed, `target` is untouched, and the `Error.message` names the attribute.

`not_atomic` is never returned when the target does not exist; a new file in an unwritable directory is `permission`. The caller then asks the user, at save time, whether to write in place (not crash-safe) with [write_in_place](#function-write_in_place) or to cancel. This function never falls back by itself.

**Extended attributes and ACLs** are all copied, never filtered: user, system and security namespaces alike, plus the ACL. Only a copy that *fails* makes the save ask the in-place question. On SELinux systems an unprivileged process may be refused `security.selinux` with a different context than the default; such a target then asks the in-place question on every save, which is accepted as the price of never dropping metadata silently.

## function: write_in_place

Overwrites an existing file's content directly, without replacing the file. It keeps the inode, so hard links, ownership, extended attributes and ACLs all survive. It is **not crash-safe**: a crash, power loss or full disk part-way through leaves the file holding a mix of new and old bytes, or truncated content. It is used only after the user explicitly chose it in the save prompt.

- **Inputs:** `target`: the resolved path of an existing, writable file; `produce`: the same streaming callback as `write_atomically`. Ambient: `TMPDIR`, else `/tmp`, for the stage file.

**Staging.** The producer's bytes usually come from the memory mapping of `target` itself (the piece tree's unedited ranges), so they cannot be streamed straight into it: overwriting the start of the file would destroy bytes that later pieces still need. The full new content is therefore staged first, in the [in_place_stage_file](../../infra/storage.iac.skel.md#resource-in_place_stage_file) under `$TMPDIR`, and only then copied over `target`. `produce` runs to completion before the first byte of `target` is written, so it never sees a partly overwritten file.
- **Returns:** `Status`. `not_found` if the target has disappeared; `permission` if it is not writable; `no_space`; `io`.
- **State changes:**
  1. Creates the stage file `$TMPDIR/mod-stage-<random>` (mode 0600) and streams the whole of `produce` into it. A failure here (`no_space` on the temp filesystem, an unwritable `$TMPDIR`) removes the stage file and returns the error with `target` untouched; the message names the stage directory.
  2. Opens `target` for writing without truncating, copies the stage file's bytes into it sequentially from offset 0, truncates it to the staged length (`ftruncate`), and `fsync`s it. The permission bits, owner, links, extended attributes and ACLs are unchanged.
  3. Removes the stage file, on success and on failure.
- **Access:** [Document.save](../edit/document.hpp.skel.md#function-save) with `mode = in_place`.
- **Failure modes:** not enough free space for the stage under `$TMPDIR` fails in step 1, before `target` is touched. A write error in step 2 after the first byte leaves the file corrupt and returns the error; the message says that the file on disk is damaged and should be saved again. A crash in step 2 does the same, and can also leave the stage file behind in `$TMPDIR`. Staging doubles the I/O and needs free space equal to the file size on the `$TMPDIR` filesystem; that is accepted for a save the user chose explicitly. `mod`'s own read-only mapping of the same file sees the new bytes as they are written in step 2; so [Document.save](../edit/document.hpp.skel.md#function-save) materializes every in-session reference into that mapping before calling this function.
- **Depends on:** [target_file](../../infra/storage.iac.skel.md#resource-target_file)
- **Depends on:** [in_place_stage_file](../../infra/storage.iac.skel.md#resource-in_place_stage_file)
- **Referred by:** [Document.save](../edit/document.hpp.skel.md#function-save)
- **Referred by:** [fs_posix](./fs_posix.cpp.skel.md)


## function: user_config_dir

- **Inputs:** ambient: `XDG_CONFIG_HOME` and `HOME` (macOS and the MSYS2 MSYS environment use the same XDG rule, because this is a terminal tool).
- **Returns:** `Result<std::filesystem::path>`: `$XDG_CONFIG_HOME/mod`, else `$HOME/.config/mod`; an error when neither variable is set. The directory is not created.
- **State changes:** none.
- **Access:** [LanguageConfig.load](../syntax/language_config.hpp.skel.md#function-load) and [Settings](../app/settings.hpp.skel.md#class-settings).
- **Referred by:** [Settings](../app/settings.hpp.skel.md#class-settings)
