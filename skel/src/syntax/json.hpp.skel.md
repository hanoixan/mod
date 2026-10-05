---
role: product
stamp: source 8a2d0f5f, stand-in 6913580d
---
# module: json

A minimal, hand-written JSON DOM for LSP messages and `languages.json`: parse, build and serialize. No JSON library is used; see the dependency decision in [SYSTEM.md](../../SYSTEM.md). Numbers are kept as `double` plus an `int64` fast path, because LSP ids and token arrays are integers.

- **Owns:** the `Json` value type.
- **Access:** public. Used by [LspClient](./lsp_client.hpp.skel.md#class-lspclient) and [LanguageConfig](./language_config.hpp.skel.md#class-languageconfig).
- **Required:** conditional — LSP or a user language config is in use.
- **Failure modes:** malformed input gives `ErrorCode::format` with the byte offset. Deep nesting is limited to 256 levels to avoid stack overflow. Large `semanticTokens` arrays (millions of integers) must not allocate a `Json` node per element, so arrays of integers are stored compactly; see [Json](#class-json). Numbers too large or too small for a `double` become ±infinity or 0 rather than an error.
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)
- **Unknowns:** none

## class: Json

A value type over null, bool, `int64`, `double`, string, array (`std::vector<Json>`), object (members in insertion order, unique keys), and a **compact integer array** (`std::vector<int64_t>`). The parser stores an array compactly when every element is an integer that fits `int64`; the first other element converts it to ordinary elements. An empty array is never compact. A compact array reports `Type::array` and its `size()`, and serializes like any array; its elements are read through `ints()`. This is the large-array path chosen over a streaming `parse_int_array`, because the token array arrives inside a whole JSON-RPC message that is parsed anyway.

- **Inputs:** construction from null (`nullptr` or default), bool, any integer type (stored as `int64`), double, string (`std::string`, `std::string_view`, `const char*`), `Json::Array`, `Json::Object` or `Json::IntArray`. `Json::array()` and `Json::object()` make empty containers.
- **State changes:** a value type. `set(key, value)` turns a null value into an object and replaces an existing key in place; `push_back(value)` turns a null value into an array and expands a compact one. Using either on another type is a programming error.
- **Owns:** its children.
- **Access:** main thread, and the LSP reader thread, which parses before posting.
- **Referred by:** [json (implementation)](./json.cpp.skel.md)
- **Referred by:** [language_config](./language_config.hpp.skel.md)
- **Referred by:** [lsp_client](./lsp_client.hpp.skel.md)
- **Referred by:** [json_test](../../tests/json_test.cpp.skel.md)
- **Referred by:** [Settings](../app/settings.hpp.skel.md#class-settings)
- **Referred by:** [fake_lsp_server](../../tests/fake_lsp_server.cpp.skel.md)
- **Referred by:** [keymap](../app/keymap.hpp.skel.md)
- **Referred by:** [keymap_test](../../tests/keymap_test.cpp.skel.md)
- **Referred by:** [theme](../ui/theme.hpp.skel.md)
- **Referred by:** [fuzz_json](../../fuzz/fuzz_json.cpp.skel.md)

Accessors, none of which throw: `type()` and `is_null`/`is_bool`/`is_int`/`is_number` (integer or double)/`is_string`/`is_array`/`is_object`; `as_bool`, `as_int` (a double is truncated and saturated), `as_double`, `as_string`, which give false, 0, 0.0 or an empty string for a value of another type; `size()` (elements or members, else 0); `ints()` (a `std::span<const int64_t>` over a compact array, else empty) and `is_compact_ints()`; `members()` and `elements()` (spans over an object's members and a non-compact array's elements, else empty). `operator==` is deep and ignores the compact representation: `[1,2]` built with `push_back` equals `[1,2]` parsed.

### function: parse

- **Inputs:** `text`: a `std::string_view`.
- **Returns:** `Result<Json>`. Strict RFC 8259: no comments, trailing commas, leading zeros, `NaN` or unescaped control characters, and nothing but whitespace after the value. A leading UTF-8 byte-order mark is skipped. Errors are `ErrorCode::format` with the message `JSON: <what> at byte <offset>`. A duplicated member name keeps the last value; past 16 members an object's names are found through a hash index, so parsing stays linear however many members it has. String bytes that are not valid UTF-8 are kept as they are.
- **State changes:** none.
- **Access:** public.
- **Referred by:** [Settings.load](../app/settings.hpp.skel.md#function-load)
- **Referred by:** [settings_test](../../tests/settings_test.cpp.skel.md)
- **Referred by:** [settings_view_test](../../tests/settings_view_test.cpp.skel.md)

### function: dump

- **Inputs:** none.
- **Returns:** compact UTF-8 text with no whitespace. Strings are escaped per RFC 8259 (`\"`, `\\`, `\b`, `\f`, `\n`, `\r`, `\t`, other C0 controls as `\u00XX`; everything else is written raw), and each invalid UTF-8 byte is emitted as `�` (U+FFFD). Doubles use the shortest round-trip form, with `.0` added to an integral one so it reads back as a double, not an integer; infinities and NaN, which JSON cannot represent, become `null`. `dump_to(std::string&)` appends instead of returning.
- **State changes:** none.
- **Access:** public.
- **Referred by:** [Settings.set](../app/settings.hpp.skel.md#function-set)

### function: get

- **Inputs:** a key (`std::string_view`) or an index (`std::size_t`).
- **Returns:** a pointer to the member or element, or null when it is absent or the value is not an object (for a key) or a non-compact array (for an index). `get(index)` on a compact integer array returns null; read it with `ints()`. No exceptions. The key overload has a non-const twin.
- **State changes:** none.
- **Access:** public.

### function: erase

- **Inputs:** a key.
- **Returns:** whether a member was removed; false for a missing key and for a value that is not an object.
- **State changes:** removes that member; the order of the others is kept.
- **Access:** [Settings.set_raw](../app/settings.hpp.skel.md#function-set_raw).

## class: JsonStringWriter

Writes large text into a JSON string literal without building a `Json` value, so that [LspClient](./lsp_client.hpp.skel.md#class-lspclient) can stream a document from its pieces into a `didOpen` body. Escaping is the same as `dump`'s, including U+FFFD for each invalid byte.

- **Inputs:** `out`: the `std::string` to append to.
- **State changes:** `write(bytes)` appends the escaped bytes. A UTF-8 sequence cut off at the end of one `write` is held (at most 3 bytes) and completed by the next, so piece boundaries never produce replacement characters. `finish()` flushes a held, incomplete sequence as U+FFFD per byte. The quotes around the literal are the caller's.
- **Owns:** the carried bytes.
- **Access:** public; one thread at a time.
- **Referred by:** [lsp_client (implementation)](./lsp_client.cpp.skel.md)
- **Referred by:** [json_test](../../tests/json_test.cpp.skel.md)
