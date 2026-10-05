---
role: product
unit: ./file_map.hpp.skel.md
stamp: source d92fb6d9, stand-in f54ebc52
---
# module: file_map_posix

Implements [MappedFile](./file_map.hpp.skel.md#class-mappedfile) with `open(O_RDONLY|O_CLOEXEC)`, `fstat`, `mmap(PROT_READ, MAP_PRIVATE)` and `posix_madvise`.

- **Owns:** the fd and the mapping.
- **Access:** internal.
- **Required:** always. These POSIX backends are the only ones: Linux, macOS and the MSYS2 MSYS environment all build them.
- **Failure modes:** `mmap` fails with `ENOMEM` on huge files under a restrictive `RLIMIT_AS`: return `too_large`, with a message that names the limit. Files on filesystems that cannot be mapped, such as some FUSE mounts and `/proc`, return `unsupported`.
- **Depends on:** [MappedFile](./file_map.hpp.skel.md#class-mappedfile)
- **Depends on:** [FileIdentity](./file_map.hpp.skel.md#symbol-fileidentity)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
