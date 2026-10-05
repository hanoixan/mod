---
role: product
stamp: source f19cb0bd, stand-in 10096aaf
---
# module: file_map

Read-only memory mapping of whole files. This is what lets `mod` open files larger than RAM. Mapped bytes are immutable for the mapping's lifetime as far as `mod` is concerned. See the failure modes for external writers.

- **Owns:** the `MappedFile` interface and `FileIdentity`.
- **Access:** public. Mappings are shared via `std::shared_ptr<const MappedFile>`, because pieces and undo payloads may keep an old mapping alive.
- **Required:** always.
- **Failure modes:**
  - The path does not exist: `not_found`. The caller treats this as a new empty file.
  - Permission denied: `permission`.
  - A zero-length file: a valid mapping with `size()==0` and `data()==nullptr`. Never call `mmap` with length 0.
  - The path is a directory or device: `unsupported`.
  - Another process truncates the file while it is mapped: touching pages past the new end raises SIGBUS. **Decision: the crash risk is accepted.** No guard is placed around mapped reads and no file is copied into memory to avoid it. The SIGBUS handler installed by [Terminal.enter_raw_mode](./terminal.hpp.skel.md#function-enter_raw_mode) restores the terminal and re-raises, so the process dies with the terminal usable. Unsaved edits survive in the sidecar, because nodes are appended as they are created, except for records still held back by the deferral rule or still queued for the writer thread, and except when history is `session_only`.
  - Another process modifies the file in place: displayed text changes underneath the piece tree. [Document](../edit/document.hpp.skel.md#class-document) detects this by polling `FileIdentity`.
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)

- **Unknowns:** none

## symbol: FileIdentity

`{ uint64_t size; int64_t mtime_ns; uint64_t device; uint64_t inode; }`. Equality means "probably unchanged". It is a cheap check; [ContentHash](../util/hash.hpp.skel.md#symbol-contenthash) is the authoritative one.

- **Access:** public.
- **Referred by:** [file_map_posix](./file_map_posix.cpp.skel.md)
- **Referred by:** [fs](./fs.hpp.skel.md)

## class: MappedFile

- **Inputs:** construction goes through the `open` factory.
- **State changes:** immutable after construction.
- **Owns:** the mapping and the file descriptor or handle. Both are released in the destructor.
- **Access:** may be read from any thread, including LineScanner and the sidecar writer.
- **Referred by:** [sidecar](../edit/sidecar.hpp.skel.md)
- **Referred by:** [file_map_posix](./file_map_posix.cpp.skel.md)
- **Referred by:** [line_scanner](../text/line_scanner.hpp.skel.md)
- **Referred by:** [piece_tree](../text/piece_tree.hpp.skel.md)
- **Referred by:** [piece_tree_test](../../tests/piece_tree_test.cpp.skel.md)

### function: open

- **Inputs:** `path`: `std::filesystem::path`.
- **Returns:** `Result<std::shared_ptr<const MappedFile>>`.
- **State changes:** creates an OS mapping. Advises `MADV_SEQUENTIAL` where available, because most accesses are scans.
- **Access:** [Document.open](../edit/document.hpp.skel.md#function-open), [Document.save](../edit/document.hpp.skel.md#function-save) for remapping, and [Sidecar](../edit/sidecar.hpp.skel.md#class-sidecar) for the payload area.
- **Referred by:** [Document.open](../edit/document.hpp.skel.md#function-open)

### function: data

- **Inputs:** none.
- **Returns:** `const std::byte*`, or `nullptr` when the file is empty.
- **State changes:** none.
- **Access:** any thread.

### function: size

- **Inputs:** none.
- **Returns:** `uint64_t` bytes.
- **State changes:** none.
- **Access:** any thread.

### function: identity

- **Inputs:** none.
- **Returns:** the `FileIdentity` captured at open.
- **State changes:** none.
- **Access:** any thread.
