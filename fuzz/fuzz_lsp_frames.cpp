// What a language server writes, in reads of any sizes: framing never crashes or buffers
// past its limit.
#include <cstddef>
#include <cstdint>

#include "fuzz/fuzz_reader.hpp"
#include "syntax/lsp_client.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    mod::fuzz::Reader in(data, size);
    mod::FrameParser parser;
    while (!in.empty()) {
        parser.feed(in.text(256));
        while (parser.next()) {
        }
    }
    return 0;
}
