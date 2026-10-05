---
role: product
unit: ./lsp_pool.hpp.skel.md
stamp: source 068db9cf, stand-in 6ecb0a60
---
# module: lsp_pool (implementation)

Implements [LspServerPool](./lsp_pool.hpp.skel.md#class-lspserverpool) with a `std::map` from `(id, root)` to `std::weak_ptr<LspClient>`.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [LspServerPool](./lsp_pool.hpp.skel.md#class-lspserverpool)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
