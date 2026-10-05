#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "app/cli_options.hpp"
#include "app/settings.hpp"

using namespace mod;

namespace {

std::expected<CliOptions, std::string> parse(std::vector<std::string_view> args) { return parse_cli(args); }

std::int64_t setting(const CliOptions& o, std::string_view key) {
    std::int64_t v = -1;
    for (const auto& [k, value] : o.settings)
        if (k == key) v = value;
    return v;
}

}  // namespace

TEST_CASE("paths, --, help and version") {
    auto o = parse({"a.txt", "--", "-b.txt"});
    REQUIRE(o);
    CHECK(o->action == CliOptions::Action::run);
    REQUIRE(o->paths.size() == 2);
    CHECK(o->paths[1] == "-b.txt");
    CHECK(parse({"-h"})->action == CliOptions::Action::help);
    CHECK(parse({"--help", "x"})->action == CliOptions::Action::help);
    CHECK(parse({"--version"})->action == CliOptions::Action::version);
}

TEST_CASE("-ro, --read-only and --persist-history") {
    auto o = parse({"-ro", "x.md"});
    REQUIRE(o);
    CHECK(o->read_only);
    CHECK_FALSE(o->persist_history);
    CHECK(parse({"--read-only"})->read_only);
    CHECK(parse({"--persist-history", "y"})->persist_history);
}

TEST_CASE("every scalar setting has a flag; the values are validated against the schema") {
    auto o = parse({"--tab-width=8", "--darkness=night", "--cursor-style=block-blink", "--read-only-copy=visible-text", "--terminal-mode=vt100", "--tab-inserts=tab"});
    REQUIRE(o);
    CHECK(setting(*o, "tab_width") == 8);
    CHECK(setting(*o, "darkness") == 0);
    CHECK(setting(*o, "cursor_style") == 3);
    CHECK(setting(*o, "read_only_copy") == 1);
    CHECK(setting(*o, "terminal_mode") == 1);
    CHECK(setting(*o, "tab_inserts") == 1);
    for (const SettingSpec& spec : setting_specs()) {
        if (spec.type == SettingType::keymap || spec.type == SettingType::colors) continue;
        std::string flag(spec.key);
        for (char& c : flag) c = c == '_' ? '-' : c;
        // Booleans are listed as --[no-]name.
        const std::string listed = (spec.type == SettingType::boolean ? "--[no-]" : "--") + flag;
        CHECK_MESSAGE(cli_usage().find(listed) != std::string::npos, listed);
    }
}

TEST_CASE("boolean flags: bare, --no-, and on/off words") {
    CHECK(setting(*parse({"--no-word-wrap"}), "word_wrap") == 0);
    CHECK(setting(*parse({"--line-numbers"}), "line_numbers") == 1);
    CHECK(setting(*parse({"--syntax-coloring=off"}), "syntax_coloring") == 0);
    CHECK(setting(*parse({"--syntax-coloring=yes"}), "syntax_coloring") == 1);
    CHECK(setting(*parse({"--word-wrap=0", "--word-wrap"}), "word_wrap") == 1);  // the later flag wins
}

TEST_CASE("bad arguments give a message naming the problem") {
    CHECK(parse({"--frobnicate"}).error() == "unknown option --frobnicate");
    CHECK(parse({"-x"}).error() == "unknown option -x");
    CHECK(parse({"--tab-width=99"}).error().find("--tab-width wants a number from 1 to 16") != std::string::npos);
    CHECK(parse({"--tab-width"}).error().find("--tab-width wants a number") != std::string::npos);
    CHECK(parse({"--tab-width=four"}).error().find("--tab-width wants a number") != std::string::npos);
    CHECK(parse({"--darkness=dim"}).error() == "--darkness wants night, normal or paper");
    CHECK(parse({"--word-wrap=maybe"}).error() == "--word-wrap wants on or off");
    CHECK(parse({"--no-tab-width"}).error() == "unknown option --no-tab-width");
    CHECK(parse({"--keymap=x"}).error() == "unknown option --keymap=x");  // structured settings stay in settings.json
    CHECK(parse({"--read-only=yes"}).error() == "unknown option --read-only=yes");
}

TEST_CASE("--color, repeatable, in both forms, validated") {
    auto o = parse({"--color", "keyword=bold red", "--color=string=green"});
    REQUIRE(o);
    REQUIRE(o->colors.size() == 2);
    CHECK(o->colors[0] == std::pair<std::string, std::string>{"keyword", "bold red"});
    CHECK(o->colors[1] == std::pair<std::string, std::string>{"string", "green"});
    CHECK(parse({"--color"}).error() == "--color wants name=spec");
    CHECK(parse({"--color", "keyword"}).error() == "--color wants name=spec");
    CHECK(parse({"--color", "kewyord=red"}).error().starts_with("--color kewyord: "));
    CHECK(parse({"--color", "keyword=purple"}).error().starts_with("--color keyword: "));
}
