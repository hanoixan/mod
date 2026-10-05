// Color specs from settings files and the command line.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ui/theme.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    (void)mod::parse_color_spec(text, false);
    (void)mod::parse_color_spec(text, true);
    return 0;
}
