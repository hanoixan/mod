#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mod {

// What the command line asks for.
struct CliOptions {
    enum class Action { run, help, version };
    Action action = Action::run;
    std::vector<std::filesystem::path> paths;
    bool read_only = false;        // -ro, --read-only: the named files open read-only
    bool persist_history = false;  // --persist-history: their history goes to the sidecar
    // Session values for scalar settings, by schema key, in command-line order.
    std::vector<std::pair<std::string, std::int64_t>> settings;
    // Session colors: name and spec.
    std::vector<std::pair<std::string, std::string>> colors;
};

// The arguments after the program name, or the message for the first bad one.
std::expected<CliOptions, std::string> parse_cli(std::span<const std::string_view> args);

// The usage text, the setting flags generated from the schema.
std::string cli_usage();

}  // namespace mod
