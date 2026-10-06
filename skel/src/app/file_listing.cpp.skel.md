---
role: product
unit: ./file_listing.hpp.skel.md
stamp: source a241accd, stand-in 3b7b89a7
---
# module: file_listing (implementation)

Implements [file_listing](./file_listing.hpp.skel.md) with `std::filesystem` and POSIX `fnmatch(FNM_CASEFOLD)`; times are formatted with `localtime_r`.

- **Owns:** nothing.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** a time zone that cannot be determined gives UTC.
- **Depends on:** [file_listing](./file_listing.hpp.skel.md)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
