---
role: test
stamp: source 1767c5b1, stand-in 43bd0b29
---
# module: fuzz_reader

Helpers shared by the fuzz targets.

- **Owns:** nothing.
- **Access:** the fuzz targets.
- **Required:** conditional — with the fuzz targets.
- **Failure modes:** none.
- **Depends on:** none
- **Unknowns:** none

## class: Reader

- **Inputs:** a fuzz input's bytes.
- **State changes:** consumes them: `byte()`, `below(n)` (a choice in `[0, n)`), `text(max)` (a length byte, then up to `max` bytes), `rest()`; past the end every read gives zero or nothing.
- **Owns:** a position.
- **Access:** the fuzz targets.
- **Referred by:** [fuzz_cli](./fuzz_cli.cpp.skel.md)
- **Referred by:** [fuzz_color_spec](./fuzz_color_spec.cpp.skel.md)
- **Referred by:** [fuzz_config](./fuzz_config.cpp.skel.md)
- **Referred by:** [fuzz_editor](./fuzz_editor.cpp.skel.md)
- **Referred by:** [fuzz_input](./fuzz_input.cpp.skel.md)
- **Referred by:** [fuzz_json](./fuzz_json.cpp.skel.md)
- **Referred by:** [fuzz_lsp_frames](./fuzz_lsp_frames.cpp.skel.md)
- **Referred by:** [fuzz_markdown](./fuzz_markdown.cpp.skel.md)
- **Referred by:** [fuzz_regex](./fuzz_regex.cpp.skel.md)
- **Referred by:** [fuzz_sidecar](./fuzz_sidecar.cpp.skel.md)
- **Referred by:** [fuzz_utf8](./fuzz_utf8.cpp.skel.md)

## function: fail

- **Inputs:** none.
- **Returns:** never: traps, which libFuzzer reports as a crash with the input saved.
- **State changes:** none.
- **Access:** the fuzz targets, when a property does not hold.
