---
role: test
stamp: source eed8c94b, stand-in 0f875480
---
# module: json_test

RFC 8259 conformance subset (literals, integers and doubles including out-of-range ones, escapes, malformed input with its byte offset); surrogate pairs and lone surrogates; the depth limit; large integer arrays stored compactly, the fallback for mixed arrays, and equality across representations; `dump` escaping, U+FFFD for invalid bytes, `null` for non-finite numbers, and round trips; `JsonStringWriter` with UTF-8 split at every position; parsing the embedded default `languages.json`; user overrides by id, user precedence, and each malformed-file case with its warning; `load` from a scratch directory; LSP frame examples (several frames per read, byte-by-byte reads, header case, too-long and too-short `Content-Length`, garbage before a header, a body that is not JSON); LSP message examples (an `initialize` result, a server request, a token response); `file_uri` escaping. `erase` removes one member of an object, keeps the order of the rest, and reports a missing key. A Content-Length beyond kMaxBodyBytes is resynchronized past, never buffered for. An object of 100 000 members parses in linear time, a duplicate name still the last one winning. An integral double is written with a fraction and reads back as a double.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks, and use `EventQueue.drain` to run worker results deterministically.
- **Depends on:** [Json](../src/syntax/json.hpp.skel.md#class-json)
- **Depends on:** [LanguageConfig](../src/syntax/language_config.hpp.skel.md#class-languageconfig)
- **Depends on:** [JsonStringWriter](../src/syntax/json.hpp.skel.md#class-jsonstringwriter)
- **Depends on:** [FrameParser](../src/syntax/lsp_client.hpp.skel.md#class-frameparser)
- **Depends on:** [file_uri](../src/syntax/lsp_client.hpp.skel.md#function-file_uri)
- **Depends on:** [time_budget](./time_budget.hpp.skel.md#function-time_budget)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
