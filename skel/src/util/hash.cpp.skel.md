---
role: product
unit: ./hash.hpp.skel.md
stamp: source 11d9820c, stand-in 7cf7f863
---
# module: hash (implementation)

Implements [hash.hpp](./hash.hpp.skel.md). CRC-32 uses a slicing-by-8 table built at compile time with `constexpr`. SHA-256 is a straightforward portable implementation of FIPS 180-4: big-endian message-word loads, the 64 round constants as a `constexpr` array, and the bit length appended in the final block.

- **Owns:** the CRC tables and the digest compression function.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** endianness bugs. All multi-byte reads must be explicit loads in the algorithm's byte order (big-endian for SHA-256 message words, reflected bit order for CRC-32), never `reinterpret_cast`. Correctness is verified against the official NIST test vectors in [hash_test](../../tests/hash_test.cpp.skel.md).
- **Depends on:** [ContentHasher](./hash.hpp.skel.md#class-contenthasher)
- **Depends on:** [crc32](./hash.hpp.skel.md#function-crc32)
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)

**Portable code only.** There is no hardware acceleration (no SHA-NI, no ARMv8 crypto extensions, no carry-less-multiply CRC) and no CPU-feature dispatch. The accepted cost is throughput: SHA-256 runs at a few hundred MB/s, so verifying or hashing a 10 GB file takes on the order of 20 s or more. That work is already in the background ([LineScanner](../text/line_scanner.hpp.skel.md#class-linescanner)), so it delays history verification, not editing. Write the compression function for clarity and let the compiler optimize it; no intrinsics and no inline assembly.

- **Unknowns:** none
