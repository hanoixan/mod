// JSON from settings files, language configs and language servers: never crashes; what
// it writes is valid UTF-8; and writing is stable from the first write on. (Only from
// there: invalid UTF-8 is written as U+FFFD, so two names that differed only in invalid
// bytes become one, and a number too large overflows to infinity, written as null.)
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "fuzz/fuzz_reader.hpp"
#include "syntax/json.hpp"
#include "text/utf8.hpp"

namespace {

bool valid_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const mod::Decoded d = mod::decode(std::as_bytes(std::span(s.data() + i, s.size() - i)));
        if (!d.valid) return false;
        i += d.len;
    }
    return true;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    auto parsed = mod::Json::parse(text);
    if (!parsed) return 0;
    const std::string first = parsed->dump();
    if (!valid_utf8(first)) mod::fuzz::fail();
    auto second_value = mod::Json::parse(first);
    if (!second_value) mod::fuzz::fail();  // what it writes it reads
    const std::string second = second_value->dump();
    auto third_value = mod::Json::parse(second);
    if (!third_value || third_value->dump() != second) mod::fuzz::fail();
    return 0;
}
