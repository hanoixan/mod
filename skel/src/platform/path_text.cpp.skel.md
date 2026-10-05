---
role: product
unit: ./path_text.hpp.skel.md
---
# module: path_text (implementation)

Implements [path_text](./path_text.hpp.skel.md). Under `__CYGWIN__` (the MSYS2 runtime) it calls `cygwin_conv_path` with `CCP_RELATIVE` (so relative paths stay relative), sizing the result first; any failure returns the input. Otherwise each function is the identity.

- **Owns:** nothing.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** as the header.
- **Depends on:** [path_text](./path_text.hpp.skel.md)
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Unknowns:** none
