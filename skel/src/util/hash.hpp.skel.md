---
role: product
stamp: source 139df40f, stand-in feb37af0
---
# module: hash

Content hashing that identifies which undo node matches the bytes on disk, plus CRC-32 framing for sidecar records. Both algorithms appear in the public sidecar format, so they must be standard and documented.

The content hash is **SHA-256** ([FIPS 180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)). It was chosen because every language and platform has it, so third-party sidecar loaders need nothing unusual. Its cost is speed: about 0.5 GB/s in portable code, so verifying a multi-GB file on open takes seconds, which the background scan absorbs. It is hand-written inside `mod`, because PCRE2 and doctest are the only third-party dependencies, and it is verified in the test suite against the official NIST test vectors ([hash_test](../../tests/hash_test.cpp.skel.md)).

- **Owns:** the declarations of the content hash and the record checksum.
- **Access:** public.
- **Required:** always.
- **Failure modes:** none at runtime. One drift risk: if the algorithm ever changes, every existing sidecar is invalidated. Version it with the sidecar format version.
- **Depends on:** [Result](./error.hpp.skel.md#symbol-result)
- **Depends on:** [FIPS 180-4 (SHA-256)](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)
- **Unknowns:** none

## symbol: ContentHash

`std::array<std::byte, 32>`: the SHA-256 digest in the standard big-endian output byte order, exactly as `sha256sum` prints it. It supports equality comparison and lowercase hex formatting (64 characters) through the free function `std::string to_hex(const ContentHash&)`; because `ContentHash` is an alias of `std::array`, there is no `std::formatter` specialization.

- **Access:** public.
- **Referred by:** [sidecar-format.md](../../docs/sidecar-format.md.skel.md)
- **Referred by:** [undo_tree](../edit/undo_tree.hpp.skel.md)

## class: ContentHasher

A streaming hasher fed by both [LineScanner](../text/line_scanner.hpp.skel.md#class-linescanner) (on open) and [Document.save](../edit/document.hpp.skel.md#function-save) (as bytes are written).

- **Inputs:** none.
- **State changes:** accumulates SHA-256 state: eight 32-bit working words, a 64-byte block buffer and a 64-bit message bit length. It cannot be reused after `finish`.
- **Owns:** the internal digest state.
- **Access:** single-threaded per instance. Instances may live on worker threads.
- **Referred by:** [Document.save](../edit/document.hpp.skel.md#function-save)
- **Referred by:** [line_scanner](../text/line_scanner.hpp.skel.md)
- **Referred by:** [hash (implementation)](./hash.cpp.skel.md)
- **Referred by:** [hash_test](../../tests/hash_test.cpp.skel.md)

### function: update

- **Inputs:** `bytes`: `std::span<const std::byte>` of any length, including empty.
- **Returns:** nothing.
- **State changes:** absorbs the bytes.
- **Access:** before `finish`.

### function: finish

- **Inputs:** none.
- **Returns:** the `ContentHash`. An empty input gives `e3b0c442…b855`, the SHA-256 of the empty string.
- **State changes:** applies SHA-256 padding and marks the hasher finished.
- **Access:** once.

## function: crc32

The IEEE 802.3 polynomial (reflected 0xEDB88320, init and xorout 0xFFFFFFFF), the same one zlib and PNG use. Any loader can reproduce it.

- **Inputs:** `bytes`; optionally `seed` (default 0) for chaining: `crc32(b, crc32(a)) == crc32(a + b)`.
- **Returns:** `uint32_t`.
- **State changes:** none.
- **Access:** public, pure.
- **Referred by:** [sidecar](../edit/sidecar.hpp.skel.md)
- **Referred by:** [hash (implementation)](./hash.cpp.skel.md)
- **Referred by:** [hash_test](../../tests/hash_test.cpp.skel.md)
