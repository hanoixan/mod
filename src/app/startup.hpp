#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/cli_options.hpp"
#include "app/keymap.hpp"
#include "app/settings.hpp"
#include "ui/theme.hpp"

namespace mod {

// The user's configuration for a session: settings.json read into `settings` (its values,
// then its keymap into `keymap` and its colors into `theme`), with the command line's
// settings and colors over it for this session only. Returns the status line to start with:
// the last of the warnings settings.json gave (each part's first one, and how many more).
std::optional<std::string> load_configuration(Settings& settings, Keymap& keymap, ColorTheme& theme, const CliOptions& options);

// "<what>: <first warning>", with " (and N more)" when there are others; nullopt for none.
std::optional<std::string> summarize_warnings(std::string_view what, const std::vector<std::string>& warnings);

}  // namespace mod
