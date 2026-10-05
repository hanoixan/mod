// Terminal input in reads of any sizes, with timeouts between: the decoder never crashes,
// and never turns bytes into a typed control character.
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include "fuzz/fuzz_reader.hpp"
#include "ui/input.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    mod::fuzz::Reader in(data, size);
    mod::InputDecoder decoder;
    const auto check = [](const std::vector<mod::InputEvent>& events) {
        for (const mod::InputEvent& e : events) {
            // C0 controls and DEL come from keys (Ctrl+letter, Backspace…), never as typed text.
            // (A C1 character sent as UTF-8 is text the user pasted or composed: it is kept,
            // and the screen shows it escaped.)
            if (const auto* k = std::get_if<mod::KeyEvent>(&e); k && k->key == mod::Key::Char && (k->ch < 0x20 || k->ch == 0x7F))
                mod::fuzz::fail();
        }
    };
    while (!in.empty()) {
        const std::string_view chunk = in.text(64);
        check(decoder.feed(std::as_bytes(std::span(chunk.data(), chunk.size()))));
        if (in.below(4) == 0) check(decoder.timeout());
    }
    check(decoder.timeout());
    return 0;
}
