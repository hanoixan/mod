---
role: product
unit: ./sidecar.hpp.skel.md
stamp: source 87741a06, stand-in 5ddd415b
---
# module: sidecar (implementation)

Implements [Sidecar](./sidecar.hpp.skel.md#class-sidecar).
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)

#### Implementation guidance

- Serialize records into a byte vector with explicit little-endian stores. Never memcpy structs, because padding and endianness must not leak into the format.
- The writer thread pops `WriteJob`s, which are either a record or a payload stream. A payload stream copies from the frozen spans in pieces of 1 MiB at a time, tracking the running CRC.
- Remap the payload area lazily: map the file once on open, and remap only when `payload_bytes` needs an offset beyond the mapped size.
- Load is a single forward pass. Reject records whose length runs past end of file; this is the truncated-tail case.
- `rewrite` reuses the record encoder. Its producer serializes straight into the `write_atomically` sink, and copies `SidecarRef` payloads from the old mapping 1 MiB at a time with a running CRC, as the payload stream does. It never builds the new file in memory.

- **Owns:** the encoder, the decoder and the writer loop.
- **Access:** internal.
- **Required:** optional — as for the header.
- **Failure modes:** partial write followed by a successful retry, which would interleave garbage. Writes are append-only with `O_APPEND`, and any short write disables persistence for the rest of the session rather than retrying.
- **Depends on:** [Sidecar](./sidecar.hpp.skel.md#class-sidecar)
- **Depends on:** [sidecar format](../../docs/sidecar-format.md.skel.md)
- **Unknowns:** none
