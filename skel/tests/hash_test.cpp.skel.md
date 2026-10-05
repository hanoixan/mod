---
role: test
stamp: source 54f60aa6, stand-in 99aa5970
---
# module: hash_test

Verifies the hand-written SHA-256 in [hash](../src/util/hash.hpp.skel.md) against the official NIST test vectors, and CRC-32 against its published check value. SHA-256 is implemented inside `mod` rather than taken from a library, so these vectors are the proof that it is correct; a wrong digest would make every sidecar unreadable by third-party loaders.

Cases:

- The SHA-256 examples published by NIST for [FIPS 180-4](https://csrc.nist.gov/projects/cryptographic-standards-and-guidelines/example-values): `""` (`e3b0c442…b855`), `"abc"`, the 448-bit message `"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"`, the 896-bit message, and one million repetitions of `a`. The expected digests are written into the test as 64-character lowercase hex literals copied from the NIST document, never computed by another tool at test time.
- The short and long message vectors of the NIST CAVP SHA-256 byte-oriented test set ([SHAVS](https://csrc.nist.gov/projects/cryptographic-algorithm-validation-program/secure-hashing)), embedded as a table, covering every message length from 0 to 64 bytes so that each padding case (including a length that leaves no room for the bit count in the last block) is exercised.
- Every vector is fed to [ContentHasher](../src/util/hash.hpp.skel.md#class-contenthasher) in every split into two `update` calls, and also one byte at a time, so block-boundary handling in `update` is covered. The million-`a` vector is fed in a few fixed chunk sizes (1, 63, 64, 65 and 4096 bytes) rather than every split, to keep the test fast. The CAVP long messages (163 to 6400 bytes) are split at every position within 130 bytes of either end and on both sides of every 64-byte block boundary, plus one byte at a time, for the same reason: every split of all 64 long vectors hashes about 0.9 GB, which took 48 s in the sanitized debug build.
- `finish` on a hasher that received no `update` gives the empty-string digest.
- [crc32](../src/util/hash.hpp.skel.md#function-crc32): the check value `0xCBF43926` for `"123456789"`, an empty input giving 0, and chaining with `seed` across a split equaling the unsplit result.

- **Owns:** test fixtures only, including the embedded vector tables.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** a vector copied wrongly from the NIST document makes a correct implementation fail. Each literal carries a comment naming its source section, so it can be re-checked against the document.
- **Depends on:** [ContentHasher](../src/util/hash.hpp.skel.md#class-contenthasher)
- **Depends on:** [crc32](../src/util/hash.hpp.skel.md#function-crc32)
- **Depends on:** [NIST example values](https://csrc.nist.gov/projects/cryptographic-standards-and-guidelines/example-values)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
