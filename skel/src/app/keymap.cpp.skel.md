---
role: product
unit: ./keymap.hpp.skel.md
stamp: source 3a85c6ff, stand-in e02b6711
---
# module: keymap (implementation)

Implements [Keymap](./keymap.hpp.skel.md#class-keymap): the default binding table as a `constexpr` array, and the live bindings as a vector that starts as a copy of it. Lookups are a linear search over about fifty entries.

- **Owns:** the table.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** a duplicate default binding, caught at compile time by a `static_assert`.
- **Depends on:** [Keymap](./keymap.hpp.skel.md#class-keymap)
- **Depends on:** [decode](../text/utf8.hpp.skel.md#function-decode)
- **Depends on:** [to_utf8](../ui/input.hpp.skel.md#function-to_utf8)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
