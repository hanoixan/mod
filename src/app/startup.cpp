#include "app/startup.hpp"

#include <format>

namespace mod {

std::optional<std::string> summarize_warnings(std::string_view what, const std::vector<std::string>& warnings) {
    if (warnings.empty()) return std::nullopt;
    const std::string more = warnings.size() > 1 ? std::format(" (and {} more)", warnings.size() - 1) : std::string();
    return std::format("{}: {}{}", what, warnings.front(), more);
}

std::optional<std::string> load_configuration(Settings& settings, Keymap& keymap, ColorTheme& theme, const CliOptions& options) {
    // Each warning replaces the one before on the status line, so the last one shows.
    std::optional<std::string> status = settings.load();
    // The command line's settings: this session's values, never written (parse_cli checked them).
    for (const auto& [key, value] : options.settings) (void)settings.override(key, value);
    if (const Json* keys = settings.raw("keymap")) {
        if (auto w = summarize_warnings("settings.json keymap", keymap.apply_overrides(*keys))) status = std::move(w);
    }
    theme.set_darkness(settings.darkness());
    if (const Json* colors = settings.raw("colors")) {
        if (auto w = summarize_warnings("settings.json colors", theme.apply(*colors))) status = std::move(w);
    }
    for (const auto& [name, spec] : options.colors) (void)theme.set_session(name, spec);  // never saved
    return status;
}

}  // namespace mod
