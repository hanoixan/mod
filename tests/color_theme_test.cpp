#include <doctest/doctest.h>

#include <set>
#include <string>
#include <vector>

#include "syntax/json.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {

using namespace theme_color;

// The fixed table before colors were configurable, kept to pin the defaults.
constexpr Attr legacy_attr(Style style, std::uint8_t modifiers = 0) {
    using namespace theme_color;
    Attr a;
    switch (style) {
        case Style::Default: break;
        case Style::md_heading1: a = {cyan + bright, kDefaultColor, kBold}; break;
        case Style::md_heading2: a = {cyan, kDefaultColor, kBold}; break;
        case Style::md_heading3:
        case Style::md_heading4:
        case Style::md_heading5:
        case Style::md_heading6: a = {blue + bright, kDefaultColor, kBold}; break;
        case Style::md_emphasis: a.flags = kItalic; break;
        case Style::md_strong: a.flags = kBold; break;
        case Style::md_strike: a.flags = kStrike; break;
        case Style::md_code:
        case Style::md_code_block: a.fg = green; break;
        case Style::md_link_text: a = {blue, kDefaultColor, kUnderline}; break;
        case Style::md_link_url: a = {blue, kDefaultColor, kDim}; break;
        case Style::md_quote: a.flags = kItalic | kDim; break;
        case Style::md_list_marker: a = {yellow, kDefaultColor, kBold}; break;
        case Style::md_markup: a.flags = kDim; break;
        case Style::lsp_namespace:
        case Style::lsp_type:
        case Style::lsp_class:
        case Style::lsp_enum:
        case Style::lsp_interface:
        case Style::lsp_struct:
        case Style::lsp_type_parameter: a.fg = cyan; break;
        case Style::lsp_parameter: a = {cyan + bright, kDefaultColor, kItalic}; break;
        case Style::lsp_variable: a.fg = cyan + bright; break;
        case Style::lsp_property: a.fg = blue + bright; break;
        case Style::lsp_enum_member: a.fg = blue + bright; break;
        case Style::lsp_event:
        case Style::lsp_function:
        case Style::lsp_method: a.fg = blue; break;
        case Style::lsp_macro: a.fg = magenta + bright; break;
        case Style::lsp_keyword:
        case Style::lsp_modifier: a.fg = magenta; break;
        case Style::lsp_comment: a.flags = kDim | kItalic; break;
        case Style::lsp_string: a.fg = green; break;
        case Style::lsp_number: a.fg = yellow; break;
        case Style::lsp_regexp: a.fg = red; break;
        case Style::lsp_operator: break;
        case Style::lsp_decorator: a.fg = yellow; break;
        case Style::constant: a.fg = yellow; break;
        case Style::search_match: a = {black, yellow, 0}; break;
        case Style::selection: a.flags = kReverse; break;
        case Style::gutter: a.flags = kDim; break;
        case Style::gutter_current: a.flags = kBold; break;
        case Style::status: a.flags = kReverse; break;
        case Style::menu: a.flags = kReverse; break;
        case Style::menu_selected: a = {white + bright, blue, kBold}; break;
        case Style::menu_accel: a.flags = kReverse | kUnderline; break;
        case Style::error: a = {white + bright, red, kBold}; break;
        case Style::history_read_only: a.flags = kDim; break;
        case Style::page:
        case Style::history_inserted:
        case Style::history_removed: break;
        case Style::overflow_marker: a.flags = kReverse; break;
    }
    // Modifiers, in the theme table's order.
    if (modifiers & kModDeclaration) a.flags |= kBold;
    if ((modifiers & kModReadonly) && a.fg < bright) a.fg = static_cast<std::uint8_t>(a.fg + bright);
    if (modifiers & kModDefaultLibrary) a.flags |= kItalic;
    if (modifiers & kModDeprecated) a.flags |= kStrike;
    if (modifiers & kModDocumentation) a.flags |= kItalic;
    return a;
}

TEST_CASE("color specs: colors, bright, backgrounds, attributes, order and case") {
    auto s = parse_color_spec("Bold bright-YELLOW on-blue italic", false);
    REQUIRE(s);
    CHECK(s->fg == yellow + bright);
    CHECK(s->bg == blue);
    CHECK(s->flags == (kBold | kItalic));
    CHECK_FALSE(s->brighten);
    CHECK(parse_color_spec("red green", false)->fg == green);  // a later color wins
    CHECK(parse_color_spec("on-bright-black", false)->bg == black + bright);
    CHECK(parse_color_spec("dim underline reverse strike", false)->flags == (kDim | kUnderline | kReverse | kStrike));
    const auto plain = parse_color_spec("plain", false);
    REQUIRE(plain);
    CHECK(plain->flags == 0);
    CHECK_FALSE(plain->fg);
    CHECK_FALSE(plain->bg);
    CHECK_FALSE(parse_color_spec("plain bold", false));
    CHECK_FALSE(parse_color_spec("bright", false));  // only for modifiers
    CHECK(parse_color_spec("bright", true)->brighten);
    const auto bad = parse_color_spec("bold purple", false);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == ErrorCode::format);
    CHECK(bad.error().message.find("purple") != std::string::npos);
    CHECK_FALSE(parse_color_spec("", false));
    CHECK_FALSE(parse_color_spec("   ", false));
}

TEST_CASE("the names: unique, one per style, then the modifiers") {
    std::set<std::string_view> names;
    std::set<int> styles;
    int modifiers = 0;
    for (const ColorEntry& e : color_names()) {
        CAPTURE(e.name);
        CHECK(names.insert(e.name).second);
        CHECK(e.style.has_value() != (e.modifier_bit != 0));
        if (e.style) CHECK(styles.insert(static_cast<int>(*e.style)).second);
        if (e.modifier_bit != 0) ++modifiers;
        CHECK(parse_color_spec(e.default_spec, e.modifier_bit != 0));
    }
    CHECK(styles.size() == static_cast<std::size_t>(Style::overflow_marker) + 1);
    CHECK(modifiers == 5);
}

TEST_CASE("the default theme gives the spec's defaults") {
    ColorTheme t;
    CHECK(t.attr(Style::Default) == Attr{});
    CHECK(t.attr(Style::lsp_keyword) == Attr{magenta, kDefaultColor, 0});
    CHECK(t.attr(Style::md_heading1) == Attr{cyan + bright, kDefaultColor, kBold});
    CHECK(t.attr(Style::md_link_url) == Attr{blue + bright, kDefaultColor, 0});
    CHECK(t.attr(Style::lsp_comment) == Attr{kDefaultColor, kDefaultColor, kDim | kItalic});
    CHECK(t.attr(Style::menu_selected) == Attr{black, blue + bright, kBold});
    CHECK(t.attr(Style::search_match) == Attr{black, yellow, 0});
    CHECK(t.attr(Style::lsp_variable, kModReadonly).fg == cyan + bright);
    CHECK(t.attr(Style::lsp_function, kModReadonly).fg == blue + bright);
    CHECK(t.attr(Style::lsp_comment, kModReadonly).fg == kDefaultColor);  // no color to brighten
    CHECK(t.attr(Style::lsp_function, kModDeclaration | kModDeprecated).flags == (kBold | kStrike));
}

TEST_CASE("the default theme is exactly the old fixed table, apart from the new entries and the brightened blues") {
    ColorTheme t;
    for (int i = 0; i <= static_cast<int>(Style::overflow_marker); ++i) {
        const Style s = static_cast<Style>(i);
        CAPTURE(i);
        if (s == Style::history_inserted || s == Style::history_removed) continue;  // new since the fixed table
        // Dark blue on a dark screen became bright blue.
        if (s == Style::lsp_event || s == Style::lsp_function || s == Style::lsp_method || s == Style::md_link_text || s == Style::md_link_url ||
            s == Style::menu_selected)
            continue;
        for (int m : {0, 1, 2, 4, 8, 16, 31}) CHECK(t.attr(s, static_cast<std::uint8_t>(m)) == legacy_attr(s, static_cast<std::uint8_t>(m)));
    }
}

TEST_CASE("overrides: apply warns per bad entry") {
    ColorTheme t;
    const auto warnings =
        t.apply(*Json::parse(R"({"keyword": "bold red", "keword": "red", "string": "bright-purple"})"));
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0] == "unknown color name keword");
    CHECK(warnings[1].starts_with("string: "));
    CHECK(t.attr(Style::lsp_keyword) == Attr{red, kDefaultColor, kBold});
    CHECK(t.attr(Style::lsp_string) == Attr{green, kDefaultColor, 0});
    CHECK_FALSE(t.is_default("keyword"));
    CHECK(t.spec_of("keyword") == "bold red");
    CHECK(t.spec_of("string") == "green");
    CHECK(t.apply(Json(7)) == std::vector<std::string>{"colors must be an object"});
    CHECK(t.is_default("keyword"));  // apply replaces every override
    CHECK(t.apply(Json()).empty());  // absent: no overrides, no warning
    CHECK(t.apply(*Json::parse(R"({"text": 3})")).size() == 1);
}

TEST_CASE("overrides: set, reset and the overrides object") {
    ColorTheme t;
    REQUIRE(t.set("keyword", "bold red"));
    CHECK(t.set("keyword", "magenta"));  // the default: the override goes
    CHECK(t.is_default("keyword"));
    const auto unknown = t.set("nope", "red");
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == ErrorCode::format);
    CHECK_FALSE(t.set("comment", "blink"));
    CHECK(t.is_default("comment"));
    CHECK_FALSE(t.set("keyword", "bright"));  // only modifiers take bright
    CHECK(t.set("declaration", "underline red"));
    CHECK(t.attr(Style::lsp_function, kModDeclaration) == Attr{red, kDefaultColor, kUnderline});
    CHECK(t.overrides() == *Json::parse(R"({"declaration": "underline red"})"));
    REQUIRE(t.set("gutter", "red"));
    t.reset("gutter");
    CHECK(t.is_default("gutter"));
    t.reset_all();
    CHECK(t.overrides() == Json::object());
}

TEST_CASE("modifiers merge in order: attributes add, a named color replaces, bright brightens") {
    ColorTheme t;
    REQUIRE(t.set("readonly", "bright bold"));
    REQUIRE(t.set("deprecated", "red"));
    const Attr a = t.attr(Style::lsp_function, kModReadonly | kModDeprecated);
    CHECK(a.fg == red);  // deprecated comes after readonly and names a color
    CHECK(a.flags == kBold);
    CHECK(t.attr(Style::lsp_function, kModReadonly).fg == blue + bright);
}

TEST_CASE("attr_for reads the active theme") {
    REQUIRE(active_theme().set("gutter", "red"));
    CHECK(attr_for(Style::gutter).fg == red);
    active_theme().reset_all();
    CHECK(attr_for(Style::gutter) == Attr{kDefaultColor, kDefaultColor, kDim});
}

}  // namespace
}  // namespace mod

namespace mod {
namespace {

using namespace theme_color;

TEST_CASE("vt100: the fallback table, with no color and only bold, underline and reverse") {
    ColorTheme t;
    REQUIRE(t.set("keyword", "bold red"));  // overrides do not apply in vt100 mode
    t.set_vt100(true);
    const Attr plain{kDefaultColor, kDefaultColor, 0};
    CHECK(t.attr(Style::lsp_keyword) == Attr{kDefaultColor, kDefaultColor, kBold});
    CHECK(t.attr(Style::md_heading1) == Attr{kDefaultColor, kDefaultColor, kBold});
    CHECK(t.attr(Style::lsp_comment) == Attr{kDefaultColor, kDefaultColor, kUnderline});
    CHECK(t.attr(Style::md_link_text) == Attr{kDefaultColor, kDefaultColor, kUnderline});
    CHECK(t.attr(Style::md_strike) == Attr{kDefaultColor, kDefaultColor, kReverse});
    CHECK(t.attr(Style::lsp_string) == plain);
    CHECK(t.attr(Style::lsp_number) == plain);
    CHECK(t.attr(Style::lsp_variable) == plain);
    CHECK(t.attr(Style::status) == Attr{kDefaultColor, kDefaultColor, kReverse});
    CHECK(t.attr(Style::menu_selected) == Attr{kDefaultColor, kDefaultColor, kBold});
    CHECK(t.attr(Style::search_match) == Attr{kDefaultColor, kDefaultColor, kReverse});
    CHECK(t.attr(Style::lsp_function, kModDeprecated) == Attr{kDefaultColor, kDefaultColor, kReverse});
    CHECK(t.attr(Style::lsp_function, kModDeclaration) == Attr{kDefaultColor, kDefaultColor, kBold});
    for (int i = 0; i <= static_cast<int>(Style::overflow_marker); ++i) {
        const Attr a = t.attr(static_cast<Style>(i), 31);
        CHECK(a.fg == kDefaultColor);
        CHECK(a.bg == kDefaultColor);
        CHECK((a.flags & ~(kBold | kUnderline | kReverse)) == 0);
    }
    t.set_vt100(false);
    CHECK(t.attr(Style::lsp_keyword) == Attr{red, kDefaultColor, kBold});  // the override is back
}

TEST_CASE("darkness: normal is unchanged; night trades reverse for bright bold; paper inverts") {
    ColorTheme t;
    CHECK(t.darkness() == Darkness::normal);
    CHECK(t.attr(Style::status) == Attr{kDefaultColor, kDefaultColor, kReverse});
    CHECK(t.attr(Style::page) == Attr{});
    t.set_darkness(Darkness::night);
    // Bold bright white on a dark grey band: brighter than the text, without reverse video.
    const Attr band{white + bright, black + bright, kBold};
    CHECK(t.attr(Style::status) == band);
    CHECK(t.attr(Style::menu) == band);
    CHECK(t.attr(Style::overflow_marker) == band);
    CHECK(t.attr(Style::menu_accel) == Attr{white + bright, black + bright, kBold | kUnderline});
    CHECK(t.attr(Style::selection) == Attr{kDefaultColor, kDefaultColor, kReverse});  // the selection stays
    CHECK(t.spec_of("status") == "bold bright-white on-bright-black");
    CHECK(t.is_default("status"));
    t.set_darkness(Darkness::paper);
    CHECK(t.attr(Style::status) == Attr{});
    CHECK(t.attr(Style::menu) == Attr{});
    CHECK(t.attr(Style::menu_accel) == Attr{kDefaultColor, kDefaultColor, kUnderline});
    CHECK(t.attr(Style::overflow_marker) == Attr{});
    CHECK(t.attr(Style::page) == Attr{black, white + bright, 0});
    CHECK(t.spec_of("page") == "black on-bright-white");
}

TEST_CASE("darkness: overrides still win, and a spec equal to the level's default is no override") {
    ColorTheme t;
    t.set_darkness(Darkness::night);
    REQUIRE(t.set("status", "red"));
    CHECK(t.attr(Style::status).fg == red);
    REQUIRE(t.set("status", "bold bright-white on-bright-black"));  // night's own default
    CHECK(t.is_default("status"));
    t.set_darkness(Darkness::paper);
    REQUIRE(t.set("page", "black on-white"));
    CHECK(t.attr(Style::page) == Attr{black, white, 0});
}

TEST_CASE("on_page lays a look over the page: default colors take the page's, reverse inverts") {
    ColorTheme t;
    t.set_darkness(Darkness::paper);
    CHECK(t.on_page(Attr{}) == Attr{black, white + bright, 0});
    CHECK(t.on_page(Attr{magenta, kDefaultColor, kBold}) == Attr{magenta, white + bright, kBold});
    CHECK(t.on_page(Attr{black, yellow, 0}) == Attr{black, yellow, 0});  // its own background stays
    t.set_darkness(Darkness::normal);
    CHECK(t.on_page(Attr{magenta, kDefaultColor, 0}) == Attr{magenta, kDefaultColor, 0});
}

TEST_CASE("darkness in vt100 mode: night drops reverse for bold, paper reverses the page") {
    ColorTheme t;
    t.set_vt100(true);
    t.set_darkness(Darkness::night);
    CHECK(t.attr(Style::status) == Attr{kDefaultColor, kDefaultColor, kBold});
    CHECK(t.attr(Style::menu_accel) == Attr{kDefaultColor, kDefaultColor, kBold | kUnderline});
    t.set_darkness(Darkness::paper);
    CHECK(t.attr(Style::status) == Attr{});
    CHECK(t.attr(Style::page) == Attr{kDefaultColor, kDefaultColor, kReverse});
    CHECK(t.on_page(Attr{}) == Attr{kDefaultColor, kDefaultColor, kReverse});
    CHECK(t.on_page(Attr{kDefaultColor, kDefaultColor, kReverse}) == Attr{});  // a selection shows inverted
}

TEST_CASE("defaults use no dark blue on a dark screen; paper keeps the plain blues") {
    ColorTheme t;
    for (Darkness d : {Darkness::normal, Darkness::night}) {
        t.set_darkness(d);
        CHECK(t.spec_of("markdownLinkText") == "underline bright-blue");
        CHECK(t.spec_of("markdownLinkUrl") == "bright-blue");
        CHECK(t.spec_of("function") == "bright-blue");
        CHECK(t.spec_of("method") == "bright-blue");
        CHECK(t.spec_of("event") == "bright-blue");
        CHECK(t.spec_of("menuSelected") == "bold black on-bright-blue");
    }
    t.set_darkness(Darkness::paper);
    CHECK(t.spec_of("markdownLinkText") == "underline blue");
    CHECK(t.spec_of("markdownLinkUrl") == "blue");
    CHECK(t.spec_of("function") == "blue");
    CHECK(t.spec_of("menuSelected") == "bold bright-white on-blue");
    CHECK(t.is_default("function"));
}


TEST_CASE("session colors: drawn over the saved ones, never among the overrides, dropped by a change") {
    ColorTheme t;
    REQUIRE(t.set("keyword", "red"));
    REQUIRE(t.set_session("keyword", "bold green"));
    REQUIRE(t.set_session("string", "yellow"));
    CHECK(t.attr(Style::lsp_keyword) == Attr{green, kDefaultColor, kBold});
    CHECK(t.spec_of("keyword") == "bold green");
    CHECK(t.overrides() == *Json::parse(R"({"keyword": "red"})"));
    CHECK_FALSE(t.is_default("string"));
    CHECK_FALSE(t.set_session("kewyord", "red"));
    CHECK_FALSE(t.set_session("keyword", "purple"));
    REQUIRE(t.set("keyword", "blue"));  // the Colors editor: replaces the session color
    CHECK(t.attr(Style::lsp_keyword).fg == blue);
    t.reset("string");
    CHECK(t.is_default("string"));
    REQUIRE(t.set_session("number", "red"));
    t.reset_all();
    CHECK(t.is_default("number"));
}
}  // namespace
TEST_CASE("an unfocused split's status line: readable text on a darker band, fixed for each darkness") {
    auto attr_of = [](std::string_view spec) {
        const auto c = parse_color_spec(spec, false);
        REQUIRE(c);
        return Attr{c->fg.value_or(kDefaultColor), c->bg.value_or(kDefaultColor), c->flags};
    };
    ColorTheme t;
    t.set_darkness(Darkness::normal);
    CHECK(t.unfocused_status() == attr_of("white on-bright-black"));
    t.set_darkness(Darkness::night);
    CHECK(t.unfocused_status() == attr_of("white on-black"));
    t.set_darkness(Darkness::paper);
    CHECK(t.unfocused_status() == attr_of("black on-white"));
    for (const Darkness d : {Darkness::normal, Darkness::night, Darkness::paper}) {
        t.set_darkness(d);
        const Attr a = t.unfocused_status();
        CHECK(a.fg != a.bg);  // never text the color of its band
        CHECK((a.flags & kDim) == 0);
    }
    t.set_vt100(true);  // no colors: the focused look, the '>' tells them apart
    CHECK(t.unfocused_status() == t.attr(Style::status));
}

}  // namespace mod
