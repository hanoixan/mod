---
role: product
unit: ./json.hpp.skel.md
stamp: source 2906526f, stand-in fe0a19e5
---
# module: json (implementation)

Implements [Json](./json.hpp.skel.md#class-json) and [JsonStringWriter](./json.hpp.skel.md#class-jsonstringwriter): a recursive-descent parser with a depth counter, and a serializer. Arrays are parsed optimistically compact: a plain integer token goes straight into the `int64` vector, without a `Json` node. Numbers without a fraction or exponent that fit `int64` are integers; all others are doubles.

- **Owns:** the parser and serializer.
- **Access:** internal.
- **Required:** conditional — as for the header.
- **Failure modes:** surrogate pairs in `\u` escapes must be combined, and lone surrogates replaced with U+FFFD. A high surrogate followed by a `\u` escape that is not a low surrogate gives U+FFFD and the second escape is decoded on its own.
- **Depends on:** [Json](./json.hpp.skel.md#class-json)
- **Depends on:** [decode](../text/utf8.hpp.skel.md#function-decode)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
