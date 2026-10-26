---
role: product
unit: ./path_text.hpp.skel.md
stamp: source 2fe9cbc2, stand-in 4d738368
---
# module: path_text (implementation)

Implements [path_text](./path_text.hpp.skel.md). Under `__CYGWIN__` (the MSYS2 runtime) it calls `cygwin_conv_path` with `CCP_RELATIVE` (so relative paths stay relative), sizing the result first; any failure returns the input. Otherwise each function is the identity. `expand_home` is the same code everywhere; only the separators after `~` it accepts differ (`/`, and on Windows `\` too).

- **Owns:** nothing.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** as the header.
- **Depends on:** [path_text](./path_text.hpp.skel.md)
- **Depends on:** [user_home_dir](./fs.hpp.skel.md#function-user_home_dir)
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Unknowns:** none
