#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/commands.hpp"
#include "app/keymap.hpp"
#include "app/read_only.hpp"
#include "app/settings.hpp"
#include "syntax/markdown.hpp"
#include "text/piece_tree.hpp"
#include "ui/theme.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

const fs::path kSource = MOD_SOURCE_DIR;
const fs::path kManual = kSource / "docs" / "manual";

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

MarkdownOutline outline_of(const fs::path& p, PieceTree& tree) {
    const std::string text = read_file(p);
    tree.insert(0, std::as_bytes(std::span(text.data(), text.size())));
    return scan_markdown(tree);
}

std::vector<fs::path> pages() {
    std::vector<fs::path> out;
    for (const auto& e : fs::recursive_directory_iterator(kManual))
        if (e.is_regular_file() && e.path().extension() == ".md") out.push_back(e.path().lexically_normal());
    return out;
}

}  // namespace

TEST_CASE("the manual exists, with index.md as its root") {
    REQUIRE(fs::is_directory(kManual));
    CHECK(fs::is_regular_file(kManual / "index.md"));
    CHECK(pages().size() >= 10);
}

TEST_CASE("every relative link in the manual resolves to a file, and every anchor to a heading") {
    for (const fs::path& page : pages()) {
        PieceTree tree;
        const MarkdownOutline here = outline_of(page, tree);
        for (const MarkdownLink& link : here.links) {
            CAPTURE(page.string());
            CAPTURE(link.target);
            const LinkAction a = ReadOnlyNav::resolve(link, page, here);
            if (a.kind == LinkAction::message) {
                CHECK(link.target.find("://") != std::string::npos);  // only web addresses are messages
                continue;
            }
            if (a.kind == LinkAction::jump) continue;  // resolve found the local heading
            REQUIRE(a.kind == LinkAction::open);
            CHECK(fs::is_regular_file(a.path));
            if (!a.anchor.empty() && fs::is_regular_file(a.path)) {
                PieceTree other;
                CHECK(ReadOnlyNav::find_anchor(outline_of(a.path, other), a.anchor).has_value());
            }
        }
    }
}

TEST_CASE("every page of the manual can be reached from index.md") {
    std::set<fs::path> seen{(kManual / "index.md").lexically_normal()};
    std::vector<fs::path> todo(seen.begin(), seen.end());
    while (!todo.empty()) {
        const fs::path page = todo.back();
        todo.pop_back();
        PieceTree tree;
        const MarkdownOutline o = outline_of(page, tree);
        for (const MarkdownLink& link : o.links) {
            const LinkAction a = ReadOnlyNav::resolve(link, page, o);
            if (a.kind != LinkAction::open || a.path.extension() != ".md" || !fs::is_regular_file(a.path)) continue;
            if (seen.insert(a.path).second && a.path.string().starts_with(kManual.string())) todo.push_back(a.path);
        }
    }
    for (const fs::path& page : pages()) {
        CAPTURE(page.string());
        CHECK(seen.contains(page));
    }
}

TEST_CASE("config/settings.json is exactly the schema's defaults") {
    CHECK(read_file(kSource / "config" / "settings.json") == default_settings_json());
}

TEST_CASE("the defaults file lists every scalar setting once, with its default, and an empty keymap") {
    const std::string json = default_settings_json();
    for (const SettingSpec& spec : setting_specs()) {
        CAPTURE(spec.key);
        if (spec.type == SettingType::keymap) {
            CHECK(json.find("\"keymap\": {}") != std::string::npos);
            continue;
        }
        if (spec.type == SettingType::colors) {
            CHECK(json.find("\"colors\"") == std::string::npos);  // absent: every color at its default
            continue;
        }
        const std::string value = spec.type == SettingType::boolean ? (spec.def != 0 ? "true" : "false")
                                  : spec.type == SettingType::choice
                                      ? "\"" + std::string(spec.choices[static_cast<std::size_t>(spec.def)]) + "\""
                                      : std::to_string(spec.def);
        CHECK(json.find("\"" + std::string(spec.key) + "\": " + value) != std::string::npos);
    }
    CHECK(json.starts_with("{\n"));
    CHECK(json.ends_with("}\n"));
}

TEST_CASE("the settings page describes every setting") {
    const std::string page = read_file(kManual / "settings.md");
    for (const SettingSpec& spec : setting_specs()) {
        CAPTURE(spec.key);
        CHECK(page.find("`" + std::string(spec.key) + "`") != std::string::npos);
    }
}

TEST_CASE("the key bindings page lists every default key next to its command") {
    const std::string page = read_file(kManual / "key-bindings.md");
    std::vector<std::string> lines;
    for (std::size_t i = 0, j; i < page.size(); i = j + 1) {
        j = page.find('\n', i);
        if (j == std::string::npos) j = page.size();
        lines.emplace_back(page.substr(i, j - i));
    }
    for (const KeyBinding& b : Keymap::default_bindings()) {
        const std::string key = "`" + Keymap::key_label(b.key) + "`";
        const std::string name = command_display_name(b.command);
        CAPTURE(key);
        CAPTURE(name);
        bool found = false;
        for (const std::string& line : lines) found = found || (line.find(key) != std::string::npos && line.find(name) != std::string::npos);
        CHECK(found);
    }
}

TEST_CASE("the colors page names every color name") {
    const std::string page = read_file(kManual / "colors.md");
    for (const ColorEntry& e : color_names()) {
        CAPTURE(e.name);
        CHECK(page.find("`" + std::string(e.name) + "`") != std::string::npos);
    }
}
