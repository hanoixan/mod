#include "app/keymap.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <set>
#include <utility>

#include "text/utf8.hpp"

namespace mod {
namespace {

using C = CommandId;

constexpr KeyEvent k(Key key, std::uint8_t mods = 0) { return KeyEvent{key, 0, mods}; }
constexpr KeyEvent ctrl(char letter) { return KeyEvent{Key::CtrlLetter, static_cast<char32_t>(letter), kCtrl}; }
constexpr KeyEvent alt(char letter) { return KeyEvent{Key::Char, static_cast<char32_t>(letter), kAlt}; }

constexpr std::uint8_t S = kShift;
constexpr std::uint8_t Ct = kCtrl;

// The defaults. Order matters only for `binding_label`, which shows a command's first binding.
constexpr KeyBinding kBindings[] = {
    {ctrl('s'), C::Save},
    {k(Key::Home, Ct), C::MoveDocStart},
    {k(Key::End, Ct), C::MoveDocEnd},
    {ctrl('z'), C::Undo},
    {ctrl('y'), C::Redo},
    {ctrl('c'), C::Copy},
    {ctrl('v'), C::Paste},
    {ctrl('f'), C::Find},
    {ctrl('a'), C::MoveLineStart},
    {ctrl('e'), C::MoveLineEnd},
    {k(Key::Left, Ct), C::MoveWordLeft},
    {k(Key::Right, Ct), C::MoveWordRight},
    {k(Key::Left, Ct | S), C::SelectWordLeft},
    {k(Key::Right, Ct | S), C::SelectWordRight},
    {alt('x'), C::ShowMenu},
    {ctrl('q'), C::Exit},
    {ctrl('x'), C::Cut},
    {k(Key::F3), C::FindNext},
    {k(Key::F3, S), C::FindPrev},
    {ctrl('g'), C::GotoLine},
    {ctrl('t'), C::Suspend},
    {ctrl('k'), C::CutToLineEnd},
    {k(Key::F10), C::ShowMenu},
    {k(Key::F1), C::ShowHelp},
    {k(Key::Left), C::MoveLeft},
    {k(Key::Right), C::MoveRight},
    {k(Key::Up), C::MoveUp},
    {k(Key::Down), C::MoveDown},
    {k(Key::Home), C::MoveLineStart},
    {k(Key::End), C::MoveLineEnd},
    {k(Key::PageUp), C::MovePageUp},
    {k(Key::PageDown), C::MovePageDown},
    {k(Key::Left, S), C::SelectLeft},
    {k(Key::Right, S), C::SelectRight},
    {k(Key::Up, S), C::SelectUp},
    {k(Key::Down, S), C::SelectDown},
    {k(Key::Home, S), C::SelectLineStart},
    {k(Key::End, S), C::SelectLineEnd},
    {k(Key::PageUp, S), C::SelectPageUp},
    {k(Key::PageDown, S), C::SelectPageDown},
    {k(Key::Home, Ct | S), C::SelectDocStart},
    {k(Key::End, Ct | S), C::SelectDocEnd},
    {k(Key::Enter), C::Newline},
    {k(Key::Tab), C::InsertTab},
    {k(Key::BackTab), C::Outdent},
    {k(Key::Backspace), C::DeleteBack},
    {k(Key::Delete), C::DeleteForward},
    {k(Key::Backspace, Ct), C::DeleteWordBack},
    {k(Key::Delete, Ct), C::DeleteWordForward},
};

constexpr bool no_duplicates() {
    for (std::size_t i = 0; i < std::size(kBindings); ++i) {
        for (std::size_t j = i + 1; j < std::size(kBindings); ++j) {
            if (kBindings[i].key == kBindings[j].key) return false;
        }
    }
    return true;
}
static_assert(no_duplicates(), "a key is bound twice");

// Key names, indexed by `Key`. BackTab is Shift+Tab; Char and CtrlLetter have no name.
constexpr std::array<std::string_view, 29> kNames = {
    "",     "Enter", "Tab",  "Shift+Tab", "Backspace", "Delete",   "Insert", "Esc", "Up", "Down",
    "Left", "Right", "Home", "End",       "PageUp",    "PageDown", "F1",     "F2",  "F3", "F4",
    "F5",   "F6",    "F7",   "F8",        "F9",        "F10",      "F11",    "F12", ""};

// The characters Ctrl combines with into a control code, besides the letters.
constexpr std::string_view kCtrlSymbols = "@\\]^_ ";

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// Removes a leading "<word>+" (any case) when more text follows it.
bool take_prefix(std::string_view& s, std::string_view word) {
    if (s.size() <= word.size() + 1 || s[word.size()] != '+' || !iequals(s.substr(0, word.size()), word)) return false;
    s.remove_prefix(word.size() + 1);
    return true;
}

std::vector<KeyEvent> default_keys(CommandId command) {
    std::vector<KeyEvent> out;
    for (const KeyBinding& b : kBindings) {
        if (b.command == command) out.push_back(b.key);
    }
    return out;
}

}  // namespace

Keymap::Keymap() : live_(std::begin(kBindings), std::end(kBindings)) {}

// Letters compare in lowercase; Shift is part of an Alt+letter only through the case.
// Ctrl with a letter or one of `@\]^_` and space is a `CtrlLetter` however it arrived.
KeyEvent Keymap::normalized(KeyEvent e) {
    if ((e.key == Key::Char || e.key == Key::CtrlLetter) && e.ch >= U'A' && e.ch <= U'Z') {
        e.ch = e.ch - U'A' + U'a';
        if (e.key == Key::Char) e.mods = static_cast<std::uint8_t>(e.mods & ~kShift);
    }
    if (e.key == Key::Char && (e.mods & kCtrl) && e.ch < 0x80 &&
        ((e.ch >= U'a' && e.ch <= U'z') || kCtrlSymbols.find(static_cast<char>(e.ch)) != std::string_view::npos))
        e.key = Key::CtrlLetter;
    return e;
}

bool Keymap::bindable(const KeyEvent& key) {
    if (key.key == Key::Escape) return false;
    if (key.key == Key::Char) {
        if ((key.mods & (kAlt | kCtrl)) == 0) return false;  // plain text is inserted
        if (key.ch == 0xFFFD || key.ch == 0) return false;
    }
    return true;
}

std::optional<CommandId> Keymap::lookup(const KeyEvent& key) const {
    // Plain text, with or without Shift, is inserted.
    if (key.key == Key::Char && (key.mods & (kAlt | kCtrl)) == 0) return std::nullopt;
    return owner(key);
}

std::optional<CommandId> Keymap::owner(const KeyEvent& key) const {
    const KeyEvent n = normalized(key);
    for (const KeyBinding& b : live_) {
        if (b.key == n) return b.command;
    }
    return std::nullopt;
}

std::span<const KeyBinding> Keymap::bindings() const { return live_; }

std::span<const KeyBinding> Keymap::default_bindings() { return kBindings; }

std::string Keymap::binding_label(CommandId command) const {
    for (const KeyBinding& b : live_) {
        if (b.command == command) return key_label(b.key);
    }
    return {};
}

std::string Keymap::key_label(const KeyEvent& key) {
    std::string out;
    if (key.mods & kCtrl) out += "Ctrl+";
    if (key.mods & kAlt) out += "Alt+";
    if (key.mods & kShift) out += "Shift+";
    if (key.key == Key::Char || key.key == Key::CtrlLetter) {
        const char32_t c = key.ch >= U'a' && key.ch <= U'z' ? key.ch - U'a' + U'A' : key.ch;
        out += c == U' ' ? std::string("Space") : to_utf8(c);
        return out;
    }
    out += kNames[static_cast<std::size_t>(key.key)];
    return out;
}

std::optional<KeyEvent> Keymap::parse_key(std::string_view label) {
    std::uint8_t mods = 0;
    for (bool again = true; again;) {
        again = false;
        for (const auto& [word, bit] : {std::pair{"Ctrl", kCtrl}, std::pair{"Alt", kAlt}, std::pair{"Shift", kShift}}) {
            if (!take_prefix(label, word)) continue;
            mods = static_cast<std::uint8_t>(mods | bit);
            again = true;
        }
    }
    if (label.empty()) return std::nullopt;
    for (std::size_t i = 1; i < kNames.size(); ++i) {
        const Key key = static_cast<Key>(i);
        if (key == Key::BackTab || kNames[i].empty() || !iequals(label, kNames[i])) continue;
        // Shift+Tab is its own key, as the terminal sends it.
        if (key == Key::Tab && (mods & kShift)) return KeyEvent{Key::BackTab, 0, static_cast<std::uint8_t>(mods & ~kShift)};
        return KeyEvent{key, 0, mods};
    }
    char32_t ch = 0;
    if (iequals(label, "Space")) {
        ch = U' ';
    } else {
        const Decoded d = decode(std::as_bytes(std::span(label.data(), label.size())));
        if (!d.valid || d.len != label.size() || d.cp < 0x20 || d.cp == 0x7F) return std::nullopt;  // one character only
        ch = d.cp;
    }
    // A label shows a letter in upper case whatever the modifiers; Shift on a letter
    // only counts together with Ctrl.
    if (ch >= U'A' && ch <= U'Z') ch = ch - U'A' + U'a';
    KeyEvent e = normalized(KeyEvent{Key::Char, ch, mods});
    if (e.key == Key::Char && e.ch >= U'a' && e.ch <= U'z') e.mods = static_cast<std::uint8_t>(e.mods & ~kShift);
    return e;
}

std::vector<KeyEvent> Keymap::keys_for(CommandId command) const {
    std::vector<KeyEvent> out;
    for (const KeyBinding& b : live_) {
        if (b.command == command) out.push_back(b.key);
    }
    return out;
}

BindOutcome Keymap::bind(CommandId command, const KeyEvent& key) {
    const KeyEvent n = normalized(key);
    if (!bindable(n)) return {BindOutcome::not_bindable, std::nullopt};
    if (const auto has = owner(n)) {
        if (*has == command) return {BindOutcome::already_bound, has};
        return {BindOutcome::taken, has};
    }
    live_.push_back(KeyBinding{n, command});
    return {BindOutcome::bound, std::nullopt};
}

bool Keymap::unbind(CommandId command, const KeyEvent& key) {
    const KeyEvent n = normalized(key);
    const auto it = std::find_if(live_.begin(), live_.end(), [&](const KeyBinding& b) { return b.command == command && b.key == n; });
    if (it == live_.end()) return false;
    live_.erase(it);
    return true;
}

std::vector<KeyEvent> Keymap::reset(CommandId command) {
    std::erase_if(live_, [&](const KeyBinding& b) { return b.command == command; });
    std::vector<KeyEvent> kept_elsewhere;
    for (const KeyEvent& key : default_keys(command)) {
        if (owner(key)) {
            kept_elsewhere.push_back(key);
        } else {
            live_.push_back(KeyBinding{key, command});
        }
    }
    return kept_elsewhere;
}

void Keymap::reset_all() { live_.assign(std::begin(kBindings), std::end(kBindings)); }

bool Keymap::is_default(CommandId command) const { return keys_for(command) == default_keys(command); }

Json Keymap::overrides() const {
    Json out = Json::object();
    for (const CommandInfo& info : all_commands()) {
        if (is_default(info.id)) continue;
        Json keys = Json::array();
        for (const KeyEvent& key : keys_for(info.id)) keys.push_back(Json(key_label(key)));
        out.set(std::string(info.name), std::move(keys));
    }
    return out;
}

std::vector<std::string> Keymap::apply_overrides(const Json& overrides) {
    reset_all();
    if (!overrides.is_object()) return {"keymap must be an object"};
    // An entry replaces its command's whole key list, so those lists are emptied first.
    std::set<CommandId> overridden;
    for (const auto& [name, keys] : overrides.members()) {
        const auto command = command_by_name(name);
        if (!command || !command_info(*command).bindable || !keys.is_array()) continue;
        overridden.insert(*command);
        std::erase_if(live_, [&](const KeyBinding& b) { return b.command == *command; });
    }
    std::vector<std::string> warnings;
    for (const auto& [name, keys] : overrides.members()) {
        const auto command = command_by_name(name);
        if (!command) {
            warnings.push_back("unknown command " + name);
            continue;
        }
        if (!command_info(*command).bindable) {
            warnings.push_back(name + " cannot have keys");
            continue;
        }
        if (!keys.is_array()) {
            warnings.push_back(name + ": the keys must be a list");
            continue;
        }
        // A compact integer array has no elements to walk; its keys are not text either.
        if (keys.is_compact_ints() && keys.size() > 0) warnings.push_back(name + ": a key must be text");
        for (const Json& entry : keys.elements()) {
            if (!entry.is_string()) {
                warnings.push_back(name + ": a key must be text");
                continue;
            }
            const std::string& label = entry.as_string();
            const auto key = parse_key(label);
            if (!key) {
                warnings.push_back(std::format("{}: {} is not a key", name, label));
                continue;
            }
            if (!bindable(*key)) {
                warnings.push_back(std::format("{}: {} cannot be bound", name, label));
                continue;
            }
            if (const auto has = owner(*key)) {
                if (*has == *command) continue;  // listed twice
                if (overridden.contains(*has)) {
                    warnings.push_back(std::format("{}: {} is already bound to {}", name, label, command_info(*has).name));
                    continue;
                }
                unbind(*has, *key);  // the user's choice beats a default
            }
            live_.push_back(KeyBinding{*key, *command});
        }
    }
    return warnings;
}

}  // namespace mod
