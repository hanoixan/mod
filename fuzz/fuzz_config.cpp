// A user's settings.json and languages.json, as the editor applies them: colors, key
// bindings and language entries from any bytes.
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/keymap.hpp"
#include "syntax/json.hpp"
#include "syntax/language_config.hpp"
#include "ui/theme.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    (void)mod::LanguageConfig::parse(text);
    (void)mod::LanguageConfig::merge(text);
    auto parsed = mod::Json::parse(text);
    if (!parsed) return 0;
    mod::ColorTheme theme;
    (void)theme.apply(*parsed);
    if (const mod::Json* colors = parsed->get("colors")) (void)theme.apply(*colors);
    mod::Keymap keymap;
    (void)keymap.apply_overrides(*parsed);
    if (const mod::Json* keys = parsed->get("keymap")) (void)keymap.apply_overrides(*keys);
    return 0;
}
