---
role: product
unit: ./commands.hpp.skel.md
stamp: source 116e2d40, stand-in 1485f4c2
---
# module: commands (implementation)

The `constexpr` table backing [command_info](./commands.hpp.skel.md#function-command_info).

- **Owns:** the table.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** the table order differing from the enum order. Index by `std::to_underlying` and `static_assert` that each entry's id equals its index.
- **Depends on:** [CommandId](./commands.hpp.skel.md#symbol-commandid)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
