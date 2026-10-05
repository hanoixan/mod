#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "platform/fs.hpp"
#include "platform/terminal_output.hpp"
#include "syntax/json.hpp"
#include "ui/theme.hpp"
#include "util/error.hpp"

namespace mod {

inline constexpr int kDefaultTabWidth = 4;
inline constexpr int kMinTabWidth = 1;
inline constexpr int kMaxTabWidth = 16;

// `keymap` and `colors` are structured values with editors of their own (the Key Bindings
// and Colors editors): they have no scalar value, `set` refuses them, and they are read and
// written with `raw` / `set_raw`.
enum class SettingType { boolean, integer, choice, keymap, colors };

// One row of the settings schema. A boolean is 0 or 1 with `min == 0` and `max == 1`; a
// choice is an index into `choices` (`min` 0, `max` the last), stored in the file as the name.
// The choice settings that are not enums of another module, in the order of their choices.
enum class TabInserts { spaces, tab };
enum class ReadOnlyCopy { markdown, visible_text };

struct SettingSpec {
    std::string_view key;  // the member name in settings.json
    SettingType type;
    std::int64_t def;
    std::int64_t min;
    std::int64_t max;
    std::string_view label;
    std::string_view help;
    std::span<const std::string_view> choices;  // a choice's names, its value is the index
};

// The schema of settings.json: every setting mod knows, in display order. The loader
// and the User Settings panel are both driven by it, so a new row needs no other code
// to be read, written and shown.
std::span<const SettingSpec> setting_specs();
const SettingSpec* find_setting(std::string_view key);
// "on" / "off" for a boolean, the number for an integer, the name for a choice, empty for a
// structured row.
std::string setting_value_text(const SettingSpec& spec, std::int64_t value);

// settings.json with every setting at its default, for the installed reference copy:
// one member per scalar setting, then an empty "keymap". Pretty-printed, ends in a newline.
std::string default_settings_json();

// The settings remembered between sessions, in `<config_dir>/settings.json`. Main thread.
class Settings {
public:
    // Replaces write_atomically, for tests.
    using WriteFile = std::function<Status(const std::filesystem::path&, const ContentProducer&)>;
    using NowFn = std::function<std::int64_t()>;  // Unix milliseconds

    explicit Settings(Result<std::filesystem::path> config_dir, WriteFile write = {}, NowFn now_ms = {});

    // An optional warning for the status line.
    std::optional<std::string> load();
    // The value of a schema key; 0 for a key that is not in the schema.
    std::int64_t value(std::string_view key) const;
    bool flag(std::string_view key) const { return value(key) != 0; }
    int tab_width() const { return static_cast<int>(value("tab_width")); }
    // The choice settings as their types (each enum lists the setting's choices in order).
    Darkness darkness() const { return static_cast<Darkness>(value("darkness")); }
    CursorStyle cursor_style() const { return static_cast<CursorStyle>(value("cursor_style")); }
    TabInserts tab_inserts() const { return static_cast<TabInserts>(value("tab_inserts")); }
    ReadOnlyCopy read_only_copy() const { return static_cast<ReadOnlyCopy>(value("read_only_copy")); }
    // The terminal mode chosen, or nullopt for auto (detected at startup).
    std::optional<TerminalMode> terminal_mode() const {
        const std::int64_t v = value("terminal_mode");
        return v == 1 ? std::optional(TerminalMode::vt100) : v == 2 ? std::optional(TerminalMode::xterm) : std::nullopt;
    }
    Status set(std::string_view key, std::int64_t value);
    // A value for this session only (a command-line flag): never written; a later `set` of
    // the key replaces it.
    Status override(std::string_view key, std::int64_t value);
    // The settings changed most recently, newest first, at most `max`.
    std::vector<const SettingSpec*> recent(std::size_t max = 5) const;
    // A structured member of the file that the schema's scalar rows do not cover (the
    // keymap): null when absent. `set_raw` with nullopt removes it. Not stamped.
    const Json* raw(std::string_view key) const { return object_.get(key); }
    Status set_raw(std::string_view key, std::optional<Json> value);

private:
    std::filesystem::path file() const { return *config_dir_ / "settings.json"; }
    Status write_file();
    void refresh_object();
    Result<std::size_t> scalar_row(std::string_view key, std::int64_t value) const;

    Result<std::filesystem::path> config_dir_;
    WriteFile write_;
    NowFn now_ms_;
    Json object_ = Json::object();
    std::vector<std::int64_t> values_;   // parallel to setting_specs()
    std::vector<std::string> modified_;  // parallel; "YYYY-MM-DDTHH:MM:SS.mmmZ", empty if never changed
};

}  // namespace mod
