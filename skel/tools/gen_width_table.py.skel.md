---
role: product
untested: a generator run by hand when the Unicode version changes; editor_test checks that its output and the conformance table share one Unicode version
stamp: source b3f2d826, stand-in 8784d106
---
# module: gen_width_table

A development-time generator for [width_table.inc](../src/text/width_table.inc.skel.md). It uses Python 3.10+ and the standard library only. It is not part of the build.

- **Owns:** the mapping rules from Unicode properties to display width, character class and grapheme-break property.
- **Access:** a CLI run by developers and CI.
- **Required:** optional — only needed to regenerate the table.
- **Failure modes:** the network is unavailable. Accept `--ucd-dir` to point at pre-downloaded files. The Unicode file format changes between versions, which would break parsing, so the parser fails loudly on any unrecognized line.
- **Depends on:** [Unicode Character Database](https://www.unicode.org/Public/)
- **Unknowns:** none
- **Referred by:** [width_table](../src/text/width_table.inc.skel.md)
- **Referred by:** [grapheme_break_cases](../tests/grapheme_break_cases.inc.skel.md)

## function: main

- **Inputs:** `--unicode VERSION`; `--out PATH` or `--check PATH`; `--ucd-dir DIR` (optional; files are looked up at their UCD-relative path, then by base name); `--test-out PATH` (optional): also write the grapheme-break conformance cases to [grapheme_break_cases.inc](../tests/grapheme_break_cases.inc.skel.md). With `--check`, the `--test-out` file is compared instead of written.
- **Returns:** exit code 0 on success. With `--check`, exit code 1 if either existing file differs from the generated output. Exit code 2 when a UCD file has an unrecognized line or names another version.
- **State changes:** writes `--out` and `--test-out`, and may download UCD files to a temporary directory.
- **Access:** developers and CI.
