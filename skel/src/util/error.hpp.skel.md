---
role: product
stamp: source 038a1871, stand-in 566e3c34
---
# module: error

The shared error vocabulary. Every fallible operation in `mod` returns `Result<T>` so that failures in optional subsystems can be shown on the status line instead of aborting the editor.

- **Owns:** the `ErrorCode` enumeration and the `Error` and `Result` types.
- **Access:** public, header-only. Included by any module that returns a `Result`.
- **Required:** always.
- **Failure modes:** none at runtime. One misuse to watch for: calling `.value()` on an error `Result` throws `std::bad_expected_access`. Callers must branch on `has_value()` instead. Tests should cover this.
- **Depends on:** [std::expected](https://en.cppreference.com/w/cpp/utility/expected)
- **Unknowns:** none

## symbol: ErrorCode

A scoped `enum class` with these values: `io`, `not_found`, `permission`, `no_space`, `not_atomic` (a crash-safe replace is impossible for this file; the caller may offer an in-place write), `too_large`, `format` (malformed sidecar, JSON or LSP frame), `mismatch` (the sidecar does not match the file), `unsupported`, `canceled`, `process` (child process failed), `regex` (bad pattern), `internal`.

- **Access:** public.

## symbol: Error

A value type: `{ ErrorCode code; int sys_errno; std::string message; }`. `message` is short and shown to the user as-is. `sys_errno` is 0 when there is no OS error.

- **Access:** public.

## symbol: Result

`template <class T> using Result = std::expected<T, Error>;` plus `Status = Result<void>`. It also provides a helper `Error make_error(ErrorCode, std::string)` and a `Error from_errno(std::string context)` that captures `errno` first and builds the message `"<context>: <strerror>"`. It maps `ENOENT`/`ENOTDIR` to `not_found`, `EACCES`/`EPERM`/`EROFS` to `permission`, `ENOSPC`/`EDQUOT` to `no_space`, `EFBIG`/`ENOMEM`/`EOVERFLOW` to `too_large`, `ENOTSUP`/`ENODEV`/`EISDIR` to `unsupported`, `ECANCELED` to `canceled`, and everything else to `io`. Callers that know better (for example a child-process failure) overwrite `code`.

- **Access:** public.
- **Referred by:** [document](../edit/document.hpp.skel.md)
- **Referred by:** [file_map](../platform/file_map.hpp.skel.md)
- **Referred by:** [fs](../platform/fs.hpp.skel.md)
- **Referred by:** [process](../platform/process.hpp.skel.md)
- **Referred by:** [terminal](../platform/terminal.hpp.skel.md)
- **Referred by:** [regex](../search/regex.hpp.skel.md)
- **Referred by:** [json](../syntax/json.hpp.skel.md)
- **Referred by:** [hash](./hash.hpp.skel.md)
- **Referred by:** [settings](../app/settings.hpp.skel.md)
- **Referred by:** [file_listing](../app/file_listing.hpp.skel.md)
