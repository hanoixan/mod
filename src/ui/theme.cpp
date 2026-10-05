#include "ui/theme.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace mod {
namespace {

using enum Style;

// Grouped as the Colors editor shows them; modifiers in the order they apply.
constexpr ColorEntry kEntries[] = {
    {"text", Default, 0, "plain", "Text"},
    {"namespace", lsp_namespace, 0, "cyan", "Text"},
    {"type", lsp_type, 0, "cyan", "Text"},
    {"class", lsp_class, 0, "cyan", "Text"},
    {"enum", lsp_enum, 0, "cyan", "Text"},
    {"interface", lsp_interface, 0, "cyan", "Text"},
    {"struct", lsp_struct, 0, "cyan", "Text"},
    {"typeParameter", lsp_type_parameter, 0, "cyan", "Text"},
    {"parameter", lsp_parameter, 0, "italic bright-cyan", "Text"},
    {"variable", lsp_variable, 0, "bright-cyan", "Text"},
    {"property", lsp_property, 0, "bright-blue", "Text"},
    {"enumMember", lsp_enum_member, 0, "bright-blue", "Text"},
    {"event", lsp_event, 0, "bright-blue", "Text"},
    {"function", lsp_function, 0, "bright-blue", "Text"},
    {"method", lsp_method, 0, "bright-blue", "Text"},
    {"macro", lsp_macro, 0, "bright-magenta", "Text"},
    {"keyword", lsp_keyword, 0, "magenta", "Text"},
    {"modifier", lsp_modifier, 0, "magenta", "Text"},
    {"comment", lsp_comment, 0, "dim italic", "Text"},
    {"string", lsp_string, 0, "green", "Text"},
    {"number", lsp_number, 0, "yellow", "Text"},
    {"constant", constant, 0, "yellow", "Text"},
    {"regexp", lsp_regexp, 0, "red", "Text"},
    {"operator", lsp_operator, 0, "plain", "Text"},
    {"decorator", lsp_decorator, 0, "yellow", "Text"},
    {"markdownHeading1", md_heading1, 0, "bold bright-cyan", "Markdown"},
    {"markdownHeading2", md_heading2, 0, "bold cyan", "Markdown"},
    {"markdownHeading3", md_heading3, 0, "bold bright-blue", "Markdown"},
    {"markdownHeading4", md_heading4, 0, "bold bright-blue", "Markdown"},
    {"markdownHeading5", md_heading5, 0, "bold bright-blue", "Markdown"},
    {"markdownHeading6", md_heading6, 0, "bold bright-blue", "Markdown"},
    {"markdownEmphasis", md_emphasis, 0, "italic", "Markdown"},
    {"markdownStrong", md_strong, 0, "bold", "Markdown"},
    {"markdownStrike", md_strike, 0, "strike", "Markdown"},
    {"markdownCode", md_code, 0, "green", "Markdown"},
    {"markdownCodeBlock", md_code_block, 0, "green", "Markdown"},
    {"markdownLinkText", md_link_text, 0, "underline bright-blue", "Markdown"},
    {"markdownLinkUrl", md_link_url, 0, "bright-blue", "Markdown"},
    {"markdownQuote", md_quote, 0, "italic dim", "Markdown"},
    {"markdownListMarker", md_list_marker, 0, "bold yellow", "Markdown"},
    {"markdownMarkup", md_markup, 0, "dim", "Markdown"},
    {"declaration", std::nullopt, kModDeclaration, "bold", "Modifiers"},
    {"readonly", std::nullopt, kModReadonly, "bright", "Modifiers"},
    {"defaultLibrary", std::nullopt, kModDefaultLibrary, "italic", "Modifiers"},
    {"deprecated", std::nullopt, kModDeprecated, "strike", "Modifiers"},
    {"documentation", std::nullopt, kModDocumentation, "italic", "Modifiers"},
    {"searchMatch", search_match, 0, "black on-yellow", "Interface"},
    {"selection", selection, 0, "reverse", "Interface"},
    {"gutter", gutter, 0, "dim", "Interface"},
    {"gutterCurrent", gutter_current, 0, "bold", "Interface"},
    {"status", status, 0, "black on-bright-white", "Interface"},
    {"statusUnfocused", status_unfocused, 0, "white on-bright-black", "Interface"},
    {"menu", menu, 0, "black on-bright-white", "Interface"},
    {"menuSelected", menu_selected, 0, "bold black on-bright-blue", "Interface"},
    {"menuAccel", menu_accel, 0, "underline black on-bright-white", "Interface"},
    {"error", error, 0, "bold bright-white on-red", "Interface"},
    {"historyReadOnly", history_read_only, 0, "dim", "Interface"},
    {"overflowMarker", overflow_marker, 0, "black on-bright-white", "Interface"},
    {"listSelected", list_selected, 0, "bold black on-bright-blue", "Interface"},
    {"listSelectedUnfocused", list_selected_unfocused, 0, "reverse", "Interface"},
    {"page", page, 0, "plain", "Interface"},
    {"historyInserted", history_inserted, 0, "black on-green", "Interface"},
    {"historyRemoved", history_removed, 0, "strike red", "Interface"},
};

constexpr std::string_view kColors[] = {"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"};

std::optional<std::uint8_t> color_number(std::string_view word) {
    std::uint8_t add = 0;
    if (word.starts_with("bright-")) {
        word.remove_prefix(7);
        add = theme_color::bright;
    }
    for (std::size_t i = 0; i < std::size(kColors); ++i)
        if (kColors[i] == word) return static_cast<std::uint8_t>(i + add);
    return std::nullopt;
}

std::optional<std::uint8_t> attribute_flag(std::string_view word) {
    if (word == "bold") return kBold;
    if (word == "dim") return kDim;
    if (word == "italic") return kItalic;
    if (word == "underline") return kUnderline;
    if (word == "reverse") return kReverse;
    if (word == "strike") return kStrike;
    return std::nullopt;
}

const ColorEntry* find_entry(std::string_view name) {
    for (const ColorEntry& e : kEntries)
        if (e.name == name) return &e;
    return nullptr;
}

// An entry's default at this darkness: night and paper replace the reverse-video looks
// (and paper's page), and paper's light page keeps the plain blues that the table's
// bright ones replace on a dark screen; everything else keeps its table default.
std::string_view default_spec(const ColorEntry& e, Darkness d) {
    if (d == Darkness::paper) {
        if (e.name == "page") return "black on-bright-white";
        if (e.name == "event" || e.name == "function" || e.name == "method") return "blue";
        if (e.name == "markdownLinkText") return "underline blue";
        if (e.name == "markdownLinkUrl") return "blue";
        if (e.name == "menuSelected" || e.name == "listSelected") return "bold bright-white on-blue";
    }
    return e.default_spec;
}

// The vt100 look of a style: no color, only bold, underline and reverse.
std::uint8_t vt100_flags(Style s, Darkness d) {
    switch (s) {
        case status:
        case menu:
        case overflow_marker: return d == Darkness::normal ? kReverse : d == Darkness::night ? kBold : 0;
        case menu_accel: return d == Darkness::normal ? kReverse | kUnderline : d == Darkness::night ? kBold | kUnderline : kUnderline;
        case page: return d == Darkness::paper ? kReverse : 0;
        case lsp_keyword:
        case lsp_modifier:
        case lsp_macro:
        case md_heading1:
        case md_heading2:
        case md_heading3:
        case md_heading4:
        case md_heading5:
        case md_heading6:
        case md_strong:
        case gutter_current:
        case menu_selected: return kBold;
        case lsp_comment:
        case md_emphasis:
        case md_quote:
        case md_link_text:
        case md_link_url: return kUnderline;
        case md_strike:
        case history_removed:
        case selection:
        case search_match: return kReverse;
        case history_inserted: return kUnderline;
        case error: return kBold | kReverse;
        default: return 0;
    }
}

std::size_t index_of(const ColorEntry* e) { return static_cast<std::size_t>(e - std::begin(kEntries)); }

}  // namespace

Result<ColorSpec> parse_color_spec(std::string_view text, bool for_modifier) {
    std::vector<std::string> words;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == ' ' || text[i] == '\t') {
            ++i;
            continue;
        }
        std::string w;
        for (; i < text.size() && text[i] != ' ' && text[i] != '\t'; ++i)
            w += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        words.push_back(std::move(w));
    }
    if (words.empty()) return std::unexpected(make_error(ErrorCode::format, "a color needs at least one word"));
    ColorSpec spec;
    for (const std::string& w : words) {
        if (w == "plain") {
            if (words.size() > 1) return std::unexpected(make_error(ErrorCode::format, "plain must stand alone"));
        } else if (w == "bright") {
            if (!for_modifier)
                return std::unexpected(make_error(ErrorCode::format, "bright is only for modifiers; use bright-<color>"));
            spec.brighten = true;
        } else if (auto flag = attribute_flag(w)) {
            spec.flags |= *flag;
        } else if (auto fg = color_number(w)) {
            spec.fg = fg;
        } else if (auto bg = w.starts_with("on-") ? color_number(std::string_view(w).substr(3)) : std::nullopt) {
            spec.bg = bg;
        } else {
            return std::unexpected(make_error(ErrorCode::format, std::format("unknown color word \"{}\"", w)));
        }
    }
    return spec;
}

std::span<const ColorEntry> color_names() { return kEntries; }

ColorTheme::ColorTheme() {
    for (const ColorEntry& e : kEntries)
        if (e.style) style_entry_[static_cast<std::size_t>(*e.style)] = index_of(&e);
    rebuild();
}

// The spec in force for `e`: the session's, else the saved override, else the default.
std::string_view ColorTheme::spec_text(const ColorEntry& e) const {
    if (const auto it = session_.find(e.name); it != session_.end()) return it->second;
    if (const auto it = overrides_.find(e.name); it != overrides_.end()) return it->second;
    return default_spec(e, darkness_);
}

void ColorTheme::rebuild() {
    effective_.clear();
    for (const ColorEntry& e : kEntries) {
        const std::string_view text = spec_text(e);
        effective_.push_back(parse_color_spec(text, e.modifier_bit != 0).value_or(ColorSpec{}));
    }
}

std::vector<std::string> ColorTheme::apply(const Json& colors) {
    overrides_.clear();
    std::vector<std::string> warnings;
    if (colors.is_null()) {
        rebuild();
        return warnings;
    }
    if (!colors.is_object()) {
        rebuild();
        return {"colors must be an object"};
    }
    for (const auto& [name, value] : colors.members()) {
        const ColorEntry* e = find_entry(name);
        if (e == nullptr) {
            warnings.push_back(std::format("unknown color name {}", name));
        } else if (!value.is_string()) {
            warnings.push_back(std::format("{}: must be a string", name));
        } else if (auto spec = parse_color_spec(value.as_string(), e->modifier_bit != 0); !spec) {
            warnings.push_back(std::format("{}: {}", name, spec.error().message));
        } else if (*spec != *parse_color_spec(default_spec(*e, darkness_), e->modifier_bit != 0)) {
            overrides_.insert_or_assign(name, value.as_string());
        }
    }
    rebuild();
    return warnings;
}

Status ColorTheme::set(std::string_view name, std::string_view spec) {
    const ColorEntry* e = find_entry(name);
    if (e == nullptr) return std::unexpected(make_error(ErrorCode::format, std::format("unknown color name {}", name)));
    auto parsed = parse_color_spec(spec, e->modifier_bit != 0);
    if (!parsed) return std::unexpected(parsed.error());
    session_.erase(std::string(name));  // a change in the Colors editor replaces the command line's
    if (*parsed == *parse_color_spec(default_spec(*e, darkness_), e->modifier_bit != 0)) {
        overrides_.erase(std::string(name));
    } else {
        overrides_.insert_or_assign(std::string(name), std::string(spec));
    }
    rebuild();
    return {};
}

Status ColorTheme::set_session(std::string_view name, std::string_view spec) {
    const ColorEntry* e = find_entry(name);
    if (e == nullptr) return std::unexpected(make_error(ErrorCode::format, std::format("unknown color name {}", name)));
    if (auto parsed = parse_color_spec(spec, e->modifier_bit != 0); !parsed) return std::unexpected(parsed.error());
    session_.insert_or_assign(std::string(name), std::string(spec));
    rebuild();
    return {};
}

void ColorTheme::reset(std::string_view name) {
    const bool had = session_.erase(std::string(name)) + overrides_.erase(std::string(name)) > 0;
    if (had) rebuild();
}

void ColorTheme::reset_all() {
    session_.clear();
    overrides_.clear();
    rebuild();
}

std::string ColorTheme::spec_of(std::string_view name) const {
    const ColorEntry* e = find_entry(name);
    return e != nullptr ? std::string(spec_text(*e)) : std::string();
}

bool ColorTheme::is_default(std::string_view name) const { return !overrides_.contains(name) && !session_.contains(name); }

Json ColorTheme::overrides() const {
    Json o = Json::object();
    for (const auto& [name, spec] : overrides_) o.set(name, spec);
    return o;
}

Attr ColorTheme::attr(Style style, std::uint8_t modifiers) const {
    if (vt100_) {
        Attr a{kDefaultColor, kDefaultColor, vt100_flags(style, darkness_)};
        if (modifiers & kModDeclaration) a.flags |= kBold;
        if (modifiers & kModDeprecated) a.flags |= kReverse;
        return a;
    }
    const ColorSpec& base = effective_[style_entry_[static_cast<std::size_t>(style)]];
    Attr a{base.fg.value_or(kDefaultColor), base.bg.value_or(kDefaultColor), base.flags};
    if (modifiers == 0) return a;
    for (std::size_t i = 0; i < std::size(kEntries); ++i) {
        if ((kEntries[i].modifier_bit & modifiers) == 0) continue;
        const ColorSpec& m = effective_[i];
        if (m.fg) a.fg = *m.fg;
        if (m.bg) a.bg = *m.bg;
        a.flags |= m.flags;
        if (m.brighten && a.fg < theme_color::bright) a.fg = static_cast<std::uint8_t>(a.fg + theme_color::bright);
    }
    return a;
}

void ColorTheme::set_darkness(Darkness d) {
    darkness_ = d;
    rebuild();
}

Attr ColorTheme::on_page(Attr a) const {
    const Attr p = attr(Style::page);
    if (p.bg != kDefaultColor && a.bg == kDefaultColor) a.bg = p.bg;
    if (p.bg != kDefaultColor && a.fg == kDefaultColor) a.fg = p.fg;
    a.flags = static_cast<std::uint8_t>((a.flags ^ (p.flags & kReverse)) | (p.flags & ~kReverse));
    return a;
}

ColorTheme& active_theme() {
    static ColorTheme theme;
    return theme;
}

Attr ColorTheme::unfocused_status() const { return attr(Style::status_unfocused); }

Attr attr_for(Style style, std::uint8_t modifiers) { return active_theme().attr(style, modifiers); }

Attr on_page(Attr a) { return active_theme().on_page(a); }

}  // namespace mod
