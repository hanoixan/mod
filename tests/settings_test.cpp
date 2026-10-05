#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "app/settings.hpp"
#include "platform/fs.hpp"
#include "syntax/json.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "settings_test" / name;
    std::error_code ec;
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(dir);
    return dir;
}

void write_file(const fs::path& p, std::string_view bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// Counts the writes that reach the file-operations seam, then performs them.
struct CountingWrite {
    int* count;
    Status operator()(const fs::path& target, const ContentProducer& produce) const {
        ++*count;
        return write_atomically(target, produce);
    }
};

// A clock that advances one second per reading, starting at 2026-10-02T16:30:00Z.
struct StepClock {
    std::int64_t* now;
    std::int64_t operator()() const {
        const std::int64_t t = *now;
        *now += 1000;
        return t;
    }
};
constexpr std::int64_t kStart = 1'790'958'600'000;

std::vector<std::string> keys(const std::vector<const SettingSpec*>& specs) {
    std::vector<std::string> out;
    for (const SettingSpec* s : specs) out.emplace_back(s->key);
    return out;
}

}  // namespace

TEST_CASE("the schema table: unique keys, defaults in range, labels and help text") {
    const auto specs = setting_specs();
    REQUIRE(specs.size() >= 4);
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const SettingSpec& s = specs[i];
        CAPTURE(s.key);
        CHECK(find_setting(s.key) == &s);
        CHECK_FALSE(s.label.empty());
        CHECK_FALSE(s.help.empty());
        CHECK(s.def >= s.min);
        CHECK(s.def <= s.max);
        if (s.type == SettingType::boolean) {
            CHECK(s.min == 0);
            CHECK(s.max == 1);
        }
        for (std::size_t j = i + 1; j < specs.size(); ++j) CHECK(specs[j].key != s.key);
    }
    CHECK(find_setting("no_such_setting") == nullptr);
    const SettingSpec* tab = find_setting("tab_width");
    REQUIRE(tab != nullptr);
    CHECK(tab->type == SettingType::integer);
    CHECK(tab->def == 4);
    CHECK(tab->min == 1);
    CHECK(tab->max == 16);
    for (const char* key : {"line_numbers", "syntax_coloring"}) {
        const SettingSpec* s = find_setting(key);
        REQUIRE(s != nullptr);
        CHECK(s->type == SettingType::boolean);
        CHECK(s->def == 1);
    }
    const SettingSpec* wrap = find_setting("word_wrap");
    REQUIRE(wrap != nullptr);
    CHECK(wrap->type == SettingType::boolean);
    CHECK(wrap->def == 1);  // on unless the user turns it off
    const SettingSpec* keymap = find_setting("keymap");
    REQUIRE(keymap != nullptr);
    CHECK(keymap->type == SettingType::keymap);  // structured: edited in the Key Bindings editor
    const SettingSpec* colors = find_setting("colors");
    REQUIRE(colors != nullptr);
    CHECK(colors->type == SettingType::colors);  // structured: edited in the Colors editor
    CHECK(setting_value_text(*colors, 0).empty());
    CHECK(setting_value_text(*tab, 8) == "8");
    CHECK(setting_value_text(*find_setting("line_numbers"), 1) == "on");
    CHECK(setting_value_text(*find_setting("line_numbers"), 0) == "off");
}

TEST_CASE("a missing directory or file gives every default with no warning") {
    const fs::path dir = scratch("missing");
    Settings s(dir);
    CHECK_FALSE(s.load().has_value());
    for (const SettingSpec& spec : setting_specs()) CHECK(s.value(spec.key) == spec.def);
    CHECK(s.tab_width() == kDefaultTabWidth);
    CHECK(s.flag("line_numbers"));
    fs::create_directories(dir);
    Settings t(dir);
    CHECK_FALSE(t.load().has_value());
    CHECK(t.tab_width() == 4);
    CHECK(t.recent().empty());
}

TEST_CASE("an unavailable configuration directory is never read or written") {
    Settings s(std::unexpected(make_error(ErrorCode::not_found, "HOME is not set")));
    CHECK_FALSE(s.load().has_value());
    CHECK(s.tab_width() == 4);
    CHECK_FALSE(s.set("tab_width", 8).has_value());
    CHECK(s.tab_width() == 8);
}

TEST_CASE("a valid file gives its values; a file written by an older mod still loads") {
    const fs::path dir = scratch("valid");
    write_file(dir / "settings.json", R"({"tab_width":2})");
    Settings s(dir);
    CHECK_FALSE(s.load().has_value());
    CHECK(s.tab_width() == 2);
    CHECK(s.flag("line_numbers"));  // absent keys keep their defaults
    write_file(dir / "settings.json", R"({"tab_width": 8, "line_numbers": false, "syntax_coloring": false})");
    CHECK_FALSE(s.load().has_value());
    CHECK(s.tab_width() == 8);
    CHECK_FALSE(s.flag("line_numbers"));
    CHECK(s.flag("word_wrap"));  // absent: its default, on
    CHECK_FALSE(s.flag("syntax_coloring"));
}

TEST_CASE("a file that cannot be used gives the defaults with a warning and is left untouched") {
    const char* cases[] = {R"({"tab_width": )", R"([4])", "\xff\xfe"};
    int n = 0;
    for (const char* text : cases) {
        CAPTURE(text);
        const fs::path dir = scratch("bad" + std::to_string(n++));
        write_file(dir / "settings.json", text);
        Settings s(dir);
        const auto warning = s.load();
        REQUIRE(warning.has_value());
        CHECK(warning->starts_with("settings.json ignored: "));
        CHECK(s.tab_width() == 4);
        CHECK(read_file(dir / "settings.json") == text);
    }
}

TEST_CASE("a bad value gives that setting's default with a warning; the other settings still load") {
    const char* cases[] = {R"({"tab_width": 0, "line_numbers": false})", R"({"tab_width": 17, "line_numbers": false})",
                           R"({"tab_width": "4", "line_numbers": false})", R"({"tab_width": 4.5, "line_numbers": false})",
                           R"({"tab_width": true, "line_numbers": false})"};
    int n = 0;
    for (const char* text : cases) {
        CAPTURE(text);
        const fs::path dir = scratch("badvalue" + std::to_string(n++));
        write_file(dir / "settings.json", text);
        Settings s(dir);
        const auto warning = s.load();
        REQUIRE(warning.has_value());
        CHECK(*warning == "settings.json: tab_width must be a whole number from 1 to 16");
        CHECK(s.tab_width() == 4);
        CHECK_FALSE(s.flag("line_numbers"));
        CHECK(read_file(dir / "settings.json") == text);
    }
    const fs::path dir = scratch("badbool");
    write_file(dir / "settings.json", R"({"line_numbers": 1, "tab_width": 99})");
    Settings s(dir);
    const auto warning = s.load();
    REQUIRE(warning.has_value());
    CHECK(*warning == "settings.json: tab_width must be a whole number from 1 to 16 (and 1 more bad value)");
    CHECK(s.flag("line_numbers"));
}

TEST_CASE("set creates the directory and the file; a second Settings loads it") {
    const fs::path dir = scratch("create") / "nested" / "mod";
    Settings s(dir);
    s.load();
    REQUIRE(s.set("tab_width", 2).has_value());
    REQUIRE(s.set("word_wrap", 0).has_value());
    CHECK(fs::exists(dir / "settings.json"));
    Settings again(dir);
    CHECK_FALSE(again.load().has_value());
    CHECK(again.tab_width() == 2);
    CHECK_FALSE(again.flag("word_wrap"));
    auto parsed = Json::parse(read_file(dir / "settings.json"));
    REQUIRE(parsed);
    CHECK(parsed->get("tab_width")->as_int() == 2);
    REQUIRE(parsed->get("word_wrap")->is_bool());  // a boolean is stored as a JSON boolean
    CHECK_FALSE(parsed->get("word_wrap")->as_bool());
}

TEST_CASE("set refuses an unknown key and a value out of range") {
    const fs::path dir = scratch("refuse");
    int writes = 0;
    Settings s(dir, CountingWrite{&writes});
    s.load();
    CHECK_FALSE(s.set("no_such_setting", 1).has_value());
    CHECK_FALSE(s.set("tab_width", 0).has_value());
    CHECK_FALSE(s.set("tab_width", 17).has_value());
    CHECK_FALSE(s.set("line_numbers", 2).has_value());
    CHECK_FALSE(s.set("keymap", 0).has_value());  // not a scalar: `set_raw` writes it
    CHECK_FALSE(s.set("colors", 0).has_value());
    CHECK(writes == 0);
    CHECK(s.tab_width() == 4);
}

TEST_CASE("unknown keys survive a write") {
    const fs::path dir = scratch("unknown");
    write_file(dir / "settings.json", R"({"future": {"a": [1, 2]}, "tab_width": 4, "z": true, "modified": {"gone": "2020-01-01T00:00:00.000Z"}})");
    Settings s(dir);
    CHECK_FALSE(s.load().has_value());
    REQUIRE(s.set("tab_width", 6).has_value());
    auto parsed = Json::parse(read_file(dir / "settings.json"));
    REQUIRE(parsed);
    CHECK(parsed->get("tab_width")->as_int() == 6);
    REQUIRE(parsed->get("future") != nullptr);
    CHECK(*parsed->get("future") == *Json::parse(R"({"a": [1, 2]})"));
    CHECK(parsed->get("z")->as_bool());
    REQUIRE(parsed->get("modified")->get("gone") != nullptr);  // a setting this version does not know
    CHECK(keys(s.recent()) == std::vector<std::string>{"tab_width"});
}

TEST_CASE("an unchanged value writes nothing and is not stamped") {
    const fs::path dir = scratch("unchanged");
    int writes = 0;
    Settings s(dir, CountingWrite{&writes});
    s.load();
    CHECK(s.set("tab_width", 4).has_value());  // the default, unchanged
    CHECK(writes == 0);
    CHECK_FALSE(fs::exists(dir / "settings.json"));
    CHECK(s.recent().empty());
    CHECK(s.set("tab_width", 3).has_value());
    CHECK(writes == 1);
    CHECK(s.set("tab_width", 3).has_value());
    CHECK(writes == 1);
    CHECK(s.set("tab_width", 5).has_value());
    CHECK(writes == 2);
}

TEST_CASE("each change is stamped with its time in the file, and recent() lists the newest first") {
    const fs::path dir = scratch("recent");
    std::int64_t now = kStart;
    Settings s(dir, {}, StepClock{&now});
    s.load();
    REQUIRE(s.set("tab_width", 8).has_value());             // 16:30:00
    REQUIRE(s.set("line_numbers", 0).has_value());           // 16:30:01
    REQUIRE(s.set("syntax_coloring", 0).has_value());       // 16:30:02
    CHECK(keys(s.recent()) == std::vector<std::string>{"syntax_coloring", "line_numbers", "tab_width"});
    REQUIRE(s.set("tab_width", 2).has_value());              // 16:30:03: moves to the top
    CHECK(keys(s.recent()) == std::vector<std::string>{"tab_width", "syntax_coloring", "line_numbers"});
    CHECK(keys(s.recent(2)) == std::vector<std::string>{"tab_width", "syntax_coloring"});

    auto parsed = Json::parse(read_file(dir / "settings.json"));
    REQUIRE(parsed);
    const Json* modified = parsed->get("modified");
    REQUIRE(modified != nullptr);
    REQUIRE(modified->is_object());
    CHECK(modified->get("tab_width")->as_string() == "2026-10-02T16:30:03.000Z");
    CHECK(modified->get("line_numbers")->as_string() == "2026-10-02T16:30:01.000Z");
    CHECK(modified->get("word_wrap") == nullptr);

    Settings again(dir);  // the order comes back from the file
    CHECK_FALSE(again.load().has_value());
    CHECK(keys(again.recent()) == std::vector<std::string>{"tab_width", "syntax_coloring", "line_numbers"});
}

TEST_CASE("recent() reads hand-written times and ignores malformed ones") {
    const fs::path dir = scratch("handwritten");
    write_file(dir / "settings.json",
               R"({"modified": {"tab_width": "2026-10-02T16:30:00Z", "line_numbers": "2026-10-02T16:30:00.500Z",
                   "word_wrap": "yesterday", "syntax_coloring": 12}})");
    Settings s(dir);
    CHECK_FALSE(s.load().has_value());
    CHECK(keys(s.recent()) == std::vector<std::string>{"line_numbers", "tab_width"});
    write_file(dir / "settings.json", R"({"modified": "not an object", "tab_width": 3})");
    CHECK_FALSE(s.load().has_value());
    CHECK(s.recent().empty());
    REQUIRE(s.set("tab_width", 4).has_value());  // a malformed "modified" is replaced
    CHECK(keys(s.recent()) == std::vector<std::string>{"tab_width"});
}

TEST_CASE("an unwritable directory returns permission; the value still applies") {
    if (::geteuid() == 0) return;  // root writes anywhere
    const fs::path dir = scratch("readonly");
    fs::create_directories(dir);
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec);
    Settings s(dir);
    s.load();
    const Status st = s.set("tab_width", 7);
    REQUIRE_FALSE(st.has_value());
    CHECK(st.error().code == ErrorCode::permission);
    CHECK(s.tab_width() == 7);
    fs::permissions(dir, fs::perms::owner_all);
}

TEST_CASE("a configuration path that is a file fails with io") {
    const fs::path base = scratch("isfile");
    write_file(base / "file", "x");
    Settings s(base / "file");
    s.load();
    const Status st = s.set("tab_width", 9);
    REQUIRE_FALSE(st.has_value());
    CHECK(st.error().code == ErrorCode::io);
    CHECK(s.tab_width() == 9);
}

TEST_CASE("a structured member: set_raw writes it, raw reads it back, and scalar writes keep it") {
    const fs::path dir = scratch("raw");
    int writes = 0;
    Settings s(dir, CountingWrite{&writes});
    s.load();
    CHECK(s.raw("keymap") == nullptr);
    CHECK(s.set_raw("keymap", std::nullopt).has_value());  // removing what is not there writes nothing
    CHECK(writes == 0);
    const Json keymap = *Json::parse(R"({"GotoLine": ["Ctrl+G", "F5"]})");
    REQUIRE(s.set_raw("keymap", keymap).has_value());
    CHECK(writes == 1);
    CHECK(s.set_raw("keymap", keymap).has_value());  // unchanged
    CHECK(writes == 1);
    REQUIRE(s.set("tab_width", 8).has_value());
    CHECK(s.recent().size() == 1);  // a raw member is never among the recent settings
    CHECK_FALSE(s.load().has_value());  // any keymap value loads here; Keymap reports its own warnings

    Settings again(dir);
    CHECK_FALSE(again.load().has_value());
    REQUIRE(again.raw("keymap") != nullptr);
    CHECK(*again.raw("keymap") == keymap);
    CHECK(again.tab_width() == 8);
    REQUIRE(again.set_raw("keymap", std::nullopt).has_value());
    auto parsed = Json::parse(read_file(dir / "settings.json"));
    REQUIRE(parsed);
    CHECK(parsed->get("keymap") == nullptr);
    CHECK(parsed->get("tab_width")->as_int() == 8);
}

TEST_CASE("a choice setting: its names, stored as a name, a bad name warned and defaulted") {
    const SettingSpec* mode = find_setting("terminal_mode");
    REQUIRE(mode != nullptr);
    CHECK(mode->type == SettingType::choice);
    REQUIRE(mode->choices.size() == 3);
    CHECK(mode->choices[0] == "auto");
    CHECK(mode->choices[1] == "vt100");
    CHECK(mode->choices[2] == "xterm");
    CHECK(mode->def == 0);
    CHECK(mode->min == 0);
    CHECK(mode->max == 2);
    CHECK(setting_value_text(*mode, 1) == "vt100");
    CHECK(default_settings_json().find("\"terminal_mode\": \"auto\"") != std::string::npos);

    const fs::path dir = scratch("choice");
    Settings s(dir);
    s.load();
    REQUIRE(s.set("terminal_mode", 2).has_value());
    auto parsed = Json::parse(read_file(dir / "settings.json"));
    REQUIRE(parsed);
    REQUIRE(parsed->get("terminal_mode")->is_string());
    CHECK(parsed->get("terminal_mode")->as_string() == "xterm");
    Settings again(dir);
    CHECK_FALSE(again.load().has_value());
    CHECK(again.value("terminal_mode") == 2);

    write_file(dir / "settings.json", R"({"terminal_mode": "vt52"})");
    Settings bad(dir);
    const auto warning = bad.load();
    REQUIRE(warning.has_value());
    CHECK(warning->find("terminal_mode must be one of auto, vt100, xterm") != std::string::npos);
    CHECK(bad.value("terminal_mode") == 0);
    write_file(dir / "settings.json", R"({"terminal_mode": 1})");
    Settings number(dir);
    CHECK(number.load().has_value());  // a number is not a name
}

TEST_CASE("Markdown formatting is no longer a setting; an old member is kept but not read") {
    CHECK(find_setting("markdown_formatting") == nullptr);
    CHECK(default_settings_json().find("markdown_formatting") == std::string::npos);
    const fs::path dir = scratch("old_markdown");
    write_file(dir / "settings.json", R"({"markdown_formatting": false})");
    Settings s(dir);
    CHECK_FALSE(s.load().has_value());  // not a bad value: an unknown member
    REQUIRE(s.set("tab_width", 3).has_value());
    auto parsed = Json::parse(read_file(dir / "settings.json"));
    REQUIRE(parsed);
    REQUIRE(parsed->get("markdown_formatting") != nullptr);  // kept as it was
    CHECK_FALSE(parsed->get("markdown_formatting")->as_bool());
}

TEST_CASE("darkness is a choice of night, normal and paper, normal by default") {
    const SettingSpec* d = find_setting("darkness");
    REQUIRE(d != nullptr);
    CHECK(d->type == SettingType::choice);
    REQUIRE(d->choices.size() == 3);
    CHECK(d->choices[0] == "night");
    CHECK(d->choices[1] == "normal");
    CHECK(d->choices[2] == "paper");
    CHECK(d->def == 1);
}

TEST_CASE("cursor_style is a choice of six shapes, a steady bar by default") {
    const SettingSpec* c = find_setting("cursor_style");
    REQUIRE(c != nullptr);
    CHECK(c->type == SettingType::choice);
    REQUIRE(c->choices.size() == 6);
    CHECK(c->choices[0] == "bar");
    CHECK(c->choices[1] == "bar-blink");
    CHECK(c->choices[2] == "block");
    CHECK(c->choices[3] == "block-blink");
    CHECK(c->choices[4] == "underline");
    CHECK(c->choices[5] == "underline-blink");
    CHECK(c->def == 0);
}

TEST_CASE("tab_inserts is spaces or tab, spaces by default; word wrap is on by default") {
    const SettingSpec* t = find_setting("tab_inserts");
    REQUIRE(t != nullptr);
    CHECK(t->type == SettingType::choice);
    REQUIRE(t->choices.size() == 2);
    CHECK(t->choices[0] == "spaces");
    CHECK(t->choices[1] == "tab");
    CHECK(t->def == 0);
    CHECK(find_setting("word_wrap")->def == 1);
}

TEST_CASE("the view settings are what a document gets on open") {
    CHECK(find_setting("line_numbers")->label == std::string_view("Line numbers on open"));
    CHECK(find_setting("syntax_coloring")->label == std::string_view("Syntax coloring on open"));
    CHECK(find_setting("word_wrap")->label == std::string_view("Word wrap on open"));
    for (const char* key : {"line_numbers", "syntax_coloring", "word_wrap"}) {
        CHECK(find_setting(key)->help.find("session only") == std::string_view::npos);
    }
}

TEST_CASE("read_only_copy: markdown or visible text, markdown by default") {
    const SettingSpec* spec = find_setting("read_only_copy");
    REQUIRE(spec != nullptr);
    CHECK(spec->type == SettingType::choice);
    CHECK(spec->def == 0);
    REQUIRE(spec->choices.size() == 2);
    CHECK(spec->choices[0] == "markdown");
    CHECK(spec->choices[1] == "visible text");
    CHECK(spec->label == std::string_view("Copy in read-only Markdown"));
}

TEST_CASE("override: a session value that is never written; set replaces it, other writes leave it out") {
    const fs::path dir = scratch("override");
    int writes = 0;
    Settings s(dir, CountingWrite{&writes});
    REQUIRE_FALSE(s.load());
    REQUIRE(s.override("tab_width", 8));
    CHECK(s.tab_width() == 8);
    CHECK(writes == 0);
    CHECK_FALSE(fs::exists(dir / "settings.json"));
    REQUIRE(s.set("line_numbers", 0));  // another key: only it is written
    CHECK(read_file(dir / "settings.json").find("tab_width") == std::string::npos);
    CHECK(s.tab_width() == 8);
    REQUIRE(s.set("tab_width", 2));  // the same key: replaces the override and is saved
    CHECK(s.tab_width() == 2);
    CHECK(read_file(dir / "settings.json").find("\"tab_width\":2") != std::string::npos);
    CHECK_FALSE(s.override("tab_width", 99));
    CHECK_FALSE(s.override("keymap", 1));
    CHECK_FALSE(s.override("nonsense", 1));
}

TEST_CASE("saving keeps a symlinked settings.json a symlink, and keeps its permissions") {
    const fs::path dir = scratch("symlinked");
    const fs::path dotfiles = scratch("dotfiles");
    fs::create_directories(dir);
    write_file(dotfiles / "settings.json", "{}\n");
    fs::permissions(dotfiles / "settings.json", fs::perms::owner_read | fs::perms::owner_write);
    fs::create_symlink(dotfiles / "settings.json", dir / "settings.json");
    Settings s(dir);
    REQUIRE_FALSE(s.load());
    REQUIRE(s.set("tab_width", 2));
    CHECK(fs::is_symlink(dir / "settings.json"));
    CHECK(read_file(dotfiles / "settings.json").find("\"tab_width\":2") != std::string::npos);
    CHECK(fs::status(dotfiles / "settings.json").permissions() == (fs::perms::owner_read | fs::perms::owner_write));
}

TEST_CASE("two mods changing different settings keep both changes") {
    const fs::path dir = scratch("two_mods");
    Settings a(dir);
    Settings b(dir);
    REQUIRE_FALSE(a.load());
    REQUIRE_FALSE(b.load());
    REQUIRE(a.set_raw("keymap", *Json::parse(R"({"Save": "Ctrl+W"})")));
    REQUIRE(b.set("tab_width", 2));  // b loaded before a's change
    const std::string file = read_file(dir / "settings.json");
    CHECK(file.find("\"Ctrl+W\"") != std::string::npos);
    CHECK(file.find("\"tab_width\":2") != std::string::npos);
    REQUIRE(a.set("line_numbers", 0));
    const std::string later = read_file(dir / "settings.json");
    CHECK(later.find("\"tab_width\":2") != std::string::npos);  // and a does not undo b's
    CHECK(later.find("\"line_numbers\":false") != std::string::npos);
}

TEST_CASE("the choice settings read back as their own types") {
    Settings s(scratch("typed"));
    REQUIRE_FALSE(s.load());
    CHECK(s.darkness() == Darkness::normal);
    CHECK(s.cursor_style() == CursorStyle::bar);
    CHECK(s.tab_inserts() == TabInserts::spaces);
    CHECK(s.read_only_copy() == ReadOnlyCopy::markdown);
    CHECK_FALSE(s.terminal_mode().has_value());  // auto: detected at startup
    REQUIRE(s.override("darkness", 0));
    REQUIRE(s.override("cursor_style", 3));
    REQUIRE(s.override("tab_inserts", 1));
    REQUIRE(s.override("read_only_copy", 1));
    REQUIRE(s.override("terminal_mode", 1));
    CHECK(s.darkness() == Darkness::night);
    CHECK(s.cursor_style() == CursorStyle::block_blink);
    CHECK(s.tab_inserts() == TabInserts::tab);
    CHECK(s.read_only_copy() == ReadOnlyCopy::visible_text);
    CHECK(s.terminal_mode() == TerminalMode::vt100);
}
