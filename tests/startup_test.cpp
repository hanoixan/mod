#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "app/startup.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path config_with(const std::string& name, const std::string& settings_json) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "startup_test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "settings.json", std::ios::binary) << settings_json;
    return dir;
}

}  // namespace

TEST_CASE("the saved settings, keys and colors apply, and the command line's over them for the session") {
    Settings settings(config_with("applied", R"({"tab_width":2,"darkness":"night","colors":{"keyword":"green"},"keymap":{"Save":["F2"]}})"));
    Keymap keymap;
    ColorTheme theme;
    CliOptions options;
    options.settings = {{"tab_width", 8}};
    options.colors = {{"comment", "red"}};
    CHECK_FALSE(load_configuration(settings, keymap, theme, options).has_value());
    CHECK(settings.tab_width() == 8);
    CHECK(theme.darkness() == Darkness::night);
    CHECK(theme.spec_of("keyword") == "green");
    CHECK(theme.spec_of("comment") == "red");
    CHECK_FALSE(theme.overrides().get("comment"));  // a session color is never saved
    CHECK(keymap.keys_for(CommandId::Save) == std::vector<KeyEvent>{KeyEvent{Key::F2}});
}

TEST_CASE("a bad entry is reported as the first warning and a count, the colors' over the keys'") {
    Settings settings(config_with("warned", R"({"colors":{"nosuch":"red","neither":"blue"},"keymap":{"NoSuchCommand":["F2"]}})"));
    Keymap keymap;
    ColorTheme theme;
    const auto status = load_configuration(settings, keymap, theme, {});
    REQUIRE(status.has_value());
    CHECK(*status == "settings.json colors: unknown color name nosuch (and 1 more)");
}

TEST_CASE("the key bindings' warning stands when the colors are fine") {
    Settings settings(config_with("keys-warned", R"({"keymap":{"NoSuchCommand":["F2"]}})"));
    Keymap keymap;
    ColorTheme theme;
    CHECK(load_configuration(settings, keymap, theme, {}) == std::optional<std::string>("settings.json keymap: unknown command NoSuchCommand"));
}
