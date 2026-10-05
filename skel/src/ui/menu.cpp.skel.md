---
role: product
unit: ./menu.hpp.skel.md
stamp: source 38fcd092, stand-in 810087f7
---
# module: menu (implementation)

Implements [MenuBar](./menu.hpp.skel.md#class-menubar), including the static menu table.

- **Owns:** the menu table.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** two items in one menu sharing an accelerator. A unit-test-style `static_assert` or a startup check catches this.
- **Depends on:** [MenuBar](./menu.hpp.skel.md#class-menubar)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
