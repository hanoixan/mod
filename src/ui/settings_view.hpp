#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "app/settings.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

struct SettingsKeyResult {
    bool closed = false;                  // Esc closed the panel
    const SettingSpec* change = nullptr;  // set this setting to `value`
    std::int64_t value = 0;
    const SettingSpec* edit = nullptr;    // a number: ask with a prompt; a keymap: open its editor
};

// The User Settings panel: one row per row of the settings schema. It never changes
// Settings itself; it asks App to. Main thread.
class SettingsView {
public:
    void open(const Settings& settings);
    void close();
    bool is_open() const noexcept { return settings_ != nullptr; }

    SettingsKeyResult handle_key(const KeyEvent& key);
    void render(Screen& screen, Rect area);

    std::size_t row_count() const;
    std::size_t selected() const noexcept { return selected_; }
    // The row as drawn, without the selection marker: the label, then the value.
    std::string row_text(std::size_t index) const;

private:
    const Settings* settings_ = nullptr;  // while open
    std::size_t selected_ = 0;
    std::size_t scroll_ = 0;
};

}  // namespace mod
