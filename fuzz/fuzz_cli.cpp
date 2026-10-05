// Command lines: the input split at NUL bytes into arguments.
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "app/cli_options.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view all(reinterpret_cast<const char*>(data), size);
    std::vector<std::string_view> args;
    for (std::size_t from = 0; from <= all.size();) {
        std::size_t nul = all.find('\0', from);
        if (nul == std::string_view::npos) nul = all.size();
        args.push_back(all.substr(from, nul - from));
        from = nul + 1;
    }
    (void)mod::parse_cli(args);
    return 0;
}
