---
role: product
---
# infrastructure: local_storage

Every on-disk artifact that `mod` reads or writes. There is no cloud and no IaC tool. This file records storage requirements so that they are explicit and reviewable. It has the placeholder `.iac` extension because nothing is provisioned. Each resource is created at runtime by the code listed under it.

- **Required:** always.
- **Failure modes:** a full disk, a read-only filesystem, network filesystems with weak `rename` and `flock` semantics (NFS, SMB, where advisory locks may be ignored), and case-insensitive filesystems (macOS, Windows), where `Notes.md.mod` and `notes.md.mod` collide. Handled per resource.
- **Depends on:** none
- **Unknowns:** none. Sidecar lifecycle decisions are recorded in [sidecar.hpp](../src/edit/sidecar.hpp.skel.md).

## resource: target_file

The file being edited.

- **Data requirements:** any size up to the filesystem's limit (64-bit offsets). Arbitrary bytes, assumed UTF-8. It is read through a read-only memory mapping and normally written only by atomic replace, which preserves its permission bits. The one exception is a save the user has explicitly confirmed as in-place because atomic replace was impossible; that save stages the new content in [in_place_stage_file](#resource-in_place_stage_file), then overwrites the file directly, and is not crash-safe. PII class: whatever the user stores.
- **Referred by:** [Document.open](../src/edit/document.hpp.skel.md#function-open)
- **Referred by:** [Document.save](../src/edit/document.hpp.skel.md#function-save)
- **Referred by:** [write_in_place](../src/platform/fs.hpp.skel.md#function-write_in_place)

## resource: save_temp_file

The temporary file `.<name>.mod-tmp-<random>`, in the target's directory, used during save.

- **Data requirements:** the same size as the new content, so free space must be at least the file size during save. It lives only for the duration of one save and is removed on failure. A crash can leave it orphaned; it is recognizable by its prefix but not cleaned up automatically. Permissions are 0600 until the target's mode, owner (as root), extended attributes and ACLs are copied onto it just before the rename.
- **Referred by:** [fs_posix](../src/platform/fs_posix.cpp.skel.md)

## resource: in_place_stage_file

The temporary file `$TMPDIR/mod-stage-<random>` (or `/tmp/...` when `TMPDIR` is unset), used only by an in-place save that the user has confirmed. It holds the full new content while the target is overwritten from it.

- **Data requirements:** the same size as the new content, on the `$TMPDIR` filesystem, so an in-place save needs free space equal to the file size there. Mode 0600, because it holds the user's content. Lives only for the duration of one in-place save and is removed on success and on failure. A crash can leave it orphaned; it is recognizable by its prefix but not cleaned up automatically. PII class: whatever the user stores.
- **Referred by:** [write_in_place](../src/platform/fs.hpp.skel.md#function-write_in_place)
- **Referred by:** [fs_posix](../src/platform/fs_posix.cpp.skel.md)

## resource: sidecar_file

The `<name>.mod` undo-history log. Format: [sidecar-format.md](../docs/sidecar-format.md.skel.md).

- **Data requirements:**
- **Referred by:** [sidecar](../src/edit/sidecar.hpp.skel.md)
- **Referred by:** [Sidecar.rewrite](../src/edit/sidecar.hpp.skel.md#function-rewrite)
  - **Growth:** append-only and unbounded ("unlimited undo"). It grows by the size of every inserted and deleted byte plus about 50 bytes per node. The exception is a prune (the Undo History pane's Trim History…, or the offer made when loading takes more than 5 s), which rewrites the file smaller, atomically, through a temp file [save_temp_file](#resource-save_temp_file) in the same directory. A prune needs free space for the new file alongside the old one until the rename.
  - **Writes and reads:** written continuously by one writer thread. Read sequentially once on open. Payload regions are read randomly through a mapping on undo and redo.
  - **Consistency:** durability is at the record level, with crash-tolerant tail truncation. The file is fsynced once per document save, after the SAVE record, and after a Save As copy; never per edit. A power failure can lose the records written since the last save. A single writer is enforced with an advisory lock.
  - **Permissions:** the same permission bits as the target file, because it contains deleted text and is privacy-sensitive.
  - **Retention:** indefinite. `mod` never cleans up orphaned sidecars (document renamed, moved or deleted by another program). The only deletions are the user's Clear History… in the Undo History pane, which deletes the sidecar and starts a new one, and a prune, which removes the history older than the age the user enters for good.
  - **Naming:** visible, `<name>.mod` (for example `notes.txt.mod`), never a hidden dot-file.
  - **Copies:** Save As writes a full copy for the new file name, so one history can exist in several sidecars. A valid, unlocked sidecar already at the new name is replaced by the copy; a file at that name that is not a sidecar is never touched.
  - **Portability:** must be readable on any OS and architecture: little-endian, documented.

## resource: browsed_directory

A directory that the file dialog lists, and in which its +Folder button creates a folder.

- **Data requirements:** read once per navigation, per filter or hidden-files change, never watched; a listing is a snapshot. Entries are listed with their name, kind, size and modification time. A directory may hold any number of entries (the dialog scrolls). A folder created by +Folder is a single `mkdir -p`, with the default permissions (0777 less the umask). No retention. PII class: names of the user's files, shown only on screen.
- **Referred by:** [file_listing](../src/app/file_listing.hpp.skel.md)
- **Referred by:** [list_directory](../src/app/file_listing.hpp.skel.md#function-list_directory)
- **Referred by:** [make_directory](../src/app/file_listing.hpp.skel.md#function-make_directory)

## resource: user_config

The user configuration directory: `$XDG_CONFIG_HOME/mod/`, or `~/.config/mod/` (also under the MSYS2 MSYS environment, where `$HOME` is set).

- **Data requirements:** `mod` reads `languages.json` (well under 1 MB) and reads and writes only [settings_file](#resource-settings_file); it never writes anything else there. It creates the directory only when it first writes `settings.json`. A missing directory or file is normal.
- **Referred by:** [LanguageConfig.load](../src/syntax/language_config.hpp.skel.md#function-load)
- **Referred by:** [Settings](../src/app/settings.hpp.skel.md#class-settings)

## resource: settings_file

`settings.json` in [user_config](#resource-user_config), for example `~/.config/mod/settings.json`: the settings `mod` remembers between sessions. Global: one file per user, shared by every `mod` process and every edited file.

- **Data requirements:**
  - **Shape:** a JSON object with one member per setting of the [schema](../src/app/settings.hpp.skel.md#function-setting_specs) that has been changed (an absent setting has its default): an integer or a boolean. A `modified` object maps a setting's key to the time it was last changed, as an ISO 8601 UTC string with milliseconds. A `keymap` object holds the user's key-binding changes, `{"<CommandName>": ["<key label>", …]}`, only for the commands that differ from the defaults; it is absent when nothing differs. Unknown keys, at the top level and inside `modified`, are preserved on write.
  - **Volume:** a few KB at most.
  - **Writes and reads:** read once at startup; rewritten whole, atomically, each time the user changes a setting or a key binding.
  - **Consistency:** last writer wins between concurrent `mod` processes; no lock.
  - **Permissions:** the new-file default of [write_atomically](../src/platform/fs.hpp.skel.md#function-write_atomically) (0666 less the umask) when created. PII class: none.
  - **Retention:** indefinite; never deleted by `mod`.
- **Referred by:** [Settings.load](../src/app/settings.hpp.skel.md#function-load)
- **Referred by:** [Settings.set](../src/app/settings.hpp.skel.md#function-set)
- **Referred by:** [Settings.set_raw](../src/app/settings.hpp.skel.md#function-set_raw)
- **Referred by:** [Keymap.overrides](../src/app/keymap.hpp.skel.md#function-overrides)

```json
{
  "tab_width": 2,
  "line_numbers": false,
  "keymap": { "GotoLine": ["Ctrl+G", "F5"], "SaveAs": ["F12"] },
  "modified": {
    "tab_width": "2026-10-02T16:30:03.000Z",
    "line_numbers": "2026-10-02T16:30:01.000Z"
  }
}
```

## resource: installed_data

`<prefix>/share/mod/`, written by `cmake --install` and read-only to mod: `doc/` holds the [manual](../docs/manual/index.md.skel.md), and beside it `sidecar-format.md` and a reference `settings.json` ([config/settings.json](../config/settings.json.skel.md)).

- **Data requirements:** a few hundred KB of Markdown and JSON, world-readable, replaced whole by each install. No PII. mod never writes here and does not read the `settings.json` copy; the help screen reads the manual.
- **Referred by:** [CMakeLists.txt](../CMakeLists.txt.skel.md)
- **Referred by:** [doc_search](../src/app/doc_search.hpp.skel.md)
