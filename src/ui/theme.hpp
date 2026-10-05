#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "syntax/highlight.hpp"
#include "syntax/json.hpp"
#include "ui/screen.hpp"
#include "util/error.hpp"

namespace mod {

namespace theme_color {
inline constexpr std::uint8_t black = 0, red = 1, green = 2, yellow = 3, blue = 4, magenta = 5, cyan = 6, white = 7;
inline constexpr std::uint8_t bright = 8;
}  // namespace theme_color

// How dark the screen is: `normal` uses reverse video for the bars and markers, `night`
// bright bold text instead, and `paper` inverts the text area into a light page.
enum class Darkness { night, normal, paper };

// A parsed color spec: words over the 16 ANSI colors. Unset colors leave the
// look below them alone (for a style, the terminal's own color).
struct ColorSpec {
    std::optional<std::uint8_t> fg;
    std::optional<std::uint8_t> bg;
    std::uint8_t flags = 0;
    bool brighten = false;  // `bright`, in a modifier's spec only
    friend bool operator==(const ColorSpec&, const ColorSpec&) = default;
};

// Parses a color spec; `format` naming the offending word when it is not one.
Result<ColorSpec> parse_color_spec(std::string_view text, bool for_modifier);

// One configurable look: a style (`style` set) or a modifier (`modifier_bit` set).
struct ColorEntry {
    std::string_view name;
    std::optional<Style> style;
    std::uint8_t modifier_bit = 0;
    std::string_view default_spec;
    std::string_view group;  // "Text", "Markdown", "Modifiers" or "Interface"
};

// Every style and modifier, grouped in the Colors editor's order.
std::span<const ColorEntry> color_names();

// The defaults with the user's overrides over them. Main thread.
class ColorTheme {
public:
    ColorTheme();

    // Replaces every override with the valid entries of settings.json's `colors`
    // member (null when absent); one warning per skipped entry.
    std::vector<std::string> apply(const Json& colors);

    // A spec equal to the default removes the override instead. Drops a session color.
    Status set(std::string_view name, std::string_view spec);
    // A color for this session only (the command line's --color), over the saved one and
    // never among `overrides`; set, reset and reset_all drop it.
    Status set_session(std::string_view name, std::string_view spec);
    void reset(std::string_view name);
    void reset_all();

    std::string spec_of(std::string_view name) const;
    bool is_default(std::string_view name) const;
    Json overrides() const;  // name → spec, only the changed entries

    Attr attr(Style style, std::uint8_t modifiers = 0) const;
    // The status line of a split that does not have the focus: readable text on a band
    // darker than the focused one's, fixed for each darkness (not a Colors entry). On a VT100,
    // the focused look.
    Attr unfocused_status() const;

    // The default looks follow the darkness; overrides still win.
    void set_darkness(Darkness d);
    Darkness darkness() const noexcept { return darkness_; }
    // `a` laid over the page: default colors take the page's, and the page's reverse inverts.
    Attr on_page(Attr a) const;

    // vt100 mode: a fixed table of bold, underline and reverse replaces the colors, and
    // overrides do not apply.
    void set_vt100(bool on) noexcept { vt100_ = on; }
    bool vt100() const noexcept { return vt100_; }

private:
    void rebuild();
    std::string_view spec_text(const ColorEntry& e) const;

    std::map<std::string, std::string, std::less<>> overrides_;  // saved in settings.json
    std::map<std::string, std::string, std::less<>> session_;    // the command line's, never saved
    std::vector<ColorSpec> effective_;  // per entry of color_names()
    bool vt100_ = false;
    Darkness darkness_ = Darkness::normal;
    std::array<std::size_t, static_cast<std::size_t>(Style::overflow_marker) + 1> style_entry_{};
};

// The process's theme, which attr_for reads.
ColorTheme& active_theme();

// How a style looks, with the given modifier bits, in the active theme.
Attr attr_for(Style style, std::uint8_t modifiers = 0);
// `a` laid over the active theme's page, for the text area.
Attr on_page(Attr a);

}  // namespace mod
