#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/commands.hpp"
#include "syntax/json.hpp"
#include "ui/input.hpp"

namespace mod {

struct KeyBinding {
    KeyEvent key;  // `ch` is lowercase for letters
    CommandId command;
};

struct BindOutcome {
    enum Kind { bound, already_bound, taken, not_bindable } kind = bound;
    std::optional<CommandId> owner;  // for `taken`: the command that has the key
};

// The key-to-command bindings: the built-in defaults, changed by the user's overrides
// from settings.json and by the Key Bindings editor. Main thread.
class Keymap {
public:
    Keymap();  // the default bindings

    // nullopt for plain text and unbound keys.
    std::optional<CommandId> lookup(const KeyEvent& key) const;
    // The first binding's label, such as "Ctrl+S"; empty for commands with no key.
    std::string binding_label(CommandId command) const;
    std::span<const KeyBinding> bindings() const;
    static std::span<const KeyBinding> default_bindings();

    static std::string key_label(const KeyEvent& key);
    // The inverse of `key_label`; nullopt for text that names no key.
    static std::optional<KeyEvent> parse_key(std::string_view label);
    // The form bindings are compared in: letters lowercase, Ctrl+letter as `CtrlLetter`.
    static KeyEvent normalized(KeyEvent key);
    // False for Esc and for plain text (a character without Ctrl or Alt).
    static bool bindable(const KeyEvent& key);

    // Editing. Keys are compared in their normalized form.
    std::vector<KeyEvent> keys_for(CommandId command) const;
    std::optional<CommandId> owner(const KeyEvent& key) const;
    // Adds `key` to `command`; a key another command has is refused, never taken.
    BindOutcome bind(CommandId command, const KeyEvent& key);
    bool unbind(CommandId command, const KeyEvent& key);
    // Gives `command` its default keys again, except those another command now has,
    // which are returned.
    std::vector<KeyEvent> reset(CommandId command);
    void reset_all();
    bool is_default(CommandId command) const;

    // The commands whose keys differ from the defaults, as {"<CommandName>": ["<key>", …]}.
    Json overrides() const;
    // Starts from the defaults and applies `overrides`; returns a warning per skipped entry.
    std::vector<std::string> apply_overrides(const Json& overrides);

private:
    std::vector<KeyBinding> live_;
};

}  // namespace mod
