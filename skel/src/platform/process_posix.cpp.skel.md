---
role: product
unit: ./process.hpp.skel.md
stamp: source e6362edf, stand-in bacb6d3b
---
# module: process_posix

Implements [ChildProcess](./process.hpp.skel.md#class-childprocess) with `posix_spawnp`, using file actions to dup the pipes onto fds 0 and 1 and open the null device (or the `MOD_LOG` file, appending) as fd 2, and `posix_spawn_file_actions_addchdir_np` where it exists (glibc 2.29 and later, and the MSYS2 runtime on Windows, where an installed mod has no `/bin/sh` beside it). Where it does not (macOS is treated this way), run through `/bin/sh -c 'cd "$1" && shift && exec "$@"' sh <cwd> <argv...>`: `$0` is `sh` and `$1` the directory, so the `shift` leaves the command in `"$@"`. A spawn that fails because the executable is missing is `not_found`; other spawn failures are `process`. Reap with `waitpid`. `kill` is `kill(pid, SIGKILL)` while the pid is unreaped.

- **Owns:** the pid and the pipe fds.
- **Access:** internal.
- **Required:** conditional — builds with LSP enabled (every platform, including the MSYS2 MSYS environment).
- **Failure modes:** a zombie if the child is never reaped: `terminate` always calls `waitpid`. `EINTR` on `read`/`write` is retried.
- **Depends on:** [ChildProcess](./process.hpp.skel.md#class-childprocess)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
