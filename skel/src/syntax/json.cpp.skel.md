---
role: product
unit: ./json.hpp.skel.md
stamp: source dee2b870, stand-in c74f3484
---
# module: json (implementation)

Implements [Json](./json.hpp.skel.md#class-json) and [JsonStringWriter](./json.hpp.skel.md#class-jsonstringwriter): a recursive-descent parser with a depth counter, and a serializer. Arrays are parsed optimistically compact: a plain integer token goes straight into the `int64` vector, without a `Json` node. Numbers without a fraction or exponent that fit `int64` are integers; all others are doubles.

A number that is not an integer is read with `std::from_chars`, except on macOS, whose libc++ has no floating-point `from_chars` before macOS 26: there `strtod_l` in the C locale reads it (the parser has already checked the token is a JSON number, so both read the same), and an `ERANGE` that gives zero or infinity is the same out-of-range case.

- **Owns:** the parser and serializer.
- **Access:** internal.
- **Required:** conditional — as for the header.
- **Failure modes:** surrogate pairs in `\u` escapes must be combined, and lone surrogates replaced with U+FFFD. A high surrogate followed by a `\u` escape that is not a low surrogate gives U+FFFD and the second escape is decoded on its own.
- **Depends on:** [Json](./json.hpp.skel.md#class-json)
- **Depends on:** [decode](../text/utf8.hpp.skel.md#function-decode)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
