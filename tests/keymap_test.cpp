#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app/commands.hpp"
#include "app/keymap.hpp"
#include "syntax/json.hpp"
#include "ui/input.hpp"

using namespace mod;

namespace {

using C = CommandId;

constexpr KeyEvent K(Key k, std::uint8_t mods = 0) { return KeyEvent{k, 0, mods}; }
constexpr KeyEvent ctrl(char c) { return KeyEvent{Key::CtrlLetter, static_cast<char32_t>(c), kCtrl}; }
constexpr KeyEvent alt(char c) { return KeyEvent{Key::Char, static_cast<char32_t>(c), kAlt}; }

std::vector<std::string> labels(const Keymap& km, C command) {
    std::vector<std::string> out;
    for (const KeyEvent& k : km.keys_for(command)) out.push_back(Keymap::key_label(k));
    return out;
}

Json json(std::string_view text) {
    auto j = Json::parse(text);
    REQUIRE(j);
    return *j;
}

}  // namespace

TEST_CASE("every default binding's label parses back to the same key") {
    for (const KeyBinding& b : Keymap::default_bindings()) {
        const std::string label = Keymap::key_label(b.key);
        CAPTURE(label);
        const auto parsed = Keymap::parse_key(label);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == b.key);
    }
}

TEST_CASE("parse_key: names, modifiers in any order and any case, and what is not a key") {
    CHECK(Keymap::parse_key("F5") == K(Key::F5));
    CHECK(Keymap::parse_key("f12") == K(Key::F12));
    CHECK(Keymap::parse_key("Ctrl+G") == ctrl('g'));
    CHECK(Keymap::parse_key("ctrl+g") == ctrl('g'));
    CHECK(Keymap::parse_key("Alt+F") == alt('f'));
    CHECK(Keymap::parse_key("Alt+1") == alt('1'));
    CHECK(Keymap::parse_key("Alt++") == alt('+'));
    CHECK(Keymap::parse_key("Ctrl+Shift+Left") == K(Key::Left, kCtrl | kShift));
    CHECK(Keymap::parse_key("Shift+Ctrl+Left") == K(Key::Left, kCtrl | kShift));
    CHECK(Keymap::parse_key("Shift+Tab") == K(Key::BackTab));
    CHECK(Keymap::parse_key("PageDown") == K(Key::PageDown));
    CHECK(Keymap::parse_key("Ctrl+Space") == KeyEvent{Key::CtrlLetter, U' ', kCtrl});
    CHECK(Keymap::parse_key("Ctrl+Alt+K") == KeyEvent{Key::CtrlLetter, U'k', kCtrl | kAlt});
    CHECK(Keymap::parse_key("Ctrl+Shift+Z") == KeyEvent{Key::CtrlLetter, U'z', kCtrl | kShift});
    for (const char* bad : {"", "Ctrl+", "Ctrl", "Hyper+X", "F13", "F0", "Ctrl+Gee", "Alt+ab", "Shift+"}) {
        CAPTURE(bad);
        CHECK_FALSE(Keymap::parse_key(bad).has_value());
    }
    // Labels for keys that are not in the default table still round-trip.
    for (const KeyEvent& k : {K(Key::F5), K(Key::Insert, kShift), K(Key::BackTab), alt('1'), KeyEvent{Key::CtrlLetter, U' ', kCtrl},
                              KeyEvent{Key::CtrlLetter, U'z', kCtrl | kShift}, K(Key::Enter, kAlt)}) {
        CAPTURE(Keymap::key_label(k));
        CHECK(Keymap::parse_key(Keymap::key_label(k)) == k);
    }
}

TEST_CASE("what can be bound: not plain text, not Esc") {
    CHECK(Keymap::bindable(ctrl('k')));
    CHECK(Keymap::bindable(alt('k')));
    CHECK(Keymap::bindable(K(Key::F5)));
    CHECK(Keymap::bindable(K(Key::Enter)));
    CHECK_FALSE(Keymap::bindable(KeyEvent{Key::Char, U'k', 0}));
    CHECK_FALSE(Keymap::bindable(KeyEvent{Key::Char, U'K', kShift}));
    CHECK_FALSE(Keymap::bindable(K(Key::Escape)));
    CHECK_FALSE(Keymap::bindable(KeyEvent{Key::Char, 0xFFFD, kAlt}));
}

TEST_CASE("bind adds a key, lookup follows it, and the first key stays the menu label") {
    Keymap km;
    CHECK_FALSE(km.lookup(K(Key::F5)).has_value());
    CHECK(km.bind(C::GotoLine, K(Key::F5)).kind == BindOutcome::bound);
    CHECK(km.lookup(K(Key::F5)) == C::GotoLine);
    CHECK(km.lookup(ctrl('g')) == C::GotoLine);  // the old key stays
    CHECK(labels(km, C::GotoLine) == std::vector<std::string>{"Ctrl+G", "F5"});
    CHECK(km.binding_label(C::GotoLine) == "Ctrl+G");
    CHECK(km.owner(K(Key::F5)) == C::GotoLine);
    CHECK_FALSE(km.is_default(C::GotoLine));
    CHECK(km.is_default(C::Save));
    // A menu-only command gets its first key.
    CHECK(km.bind(C::SaveAs, K(Key::F12)).kind == BindOutcome::bound);
    CHECK(km.binding_label(C::SaveAs) == "F12");
    // Letters compare in lowercase.
    CHECK(km.bind(C::SaveAs, KeyEvent{Key::Char, U'P', kAlt | kShift}).kind == BindOutcome::bound);
    CHECK(km.lookup(alt('p')) == C::SaveAs);
}

TEST_CASE("a key another command has is refused, and nothing changes") {
    Keymap km;
    const BindOutcome r = km.bind(C::GotoLine, ctrl('s'));
    CHECK(r.kind == BindOutcome::taken);
    CHECK(r.owner == C::Save);
    CHECK(km.lookup(ctrl('s')) == C::Save);
    CHECK(km.is_default(C::GotoLine));
    CHECK(km.bind(C::Save, ctrl('s')).kind == BindOutcome::already_bound);
    CHECK(km.bind(C::Save, K(Key::Escape)).kind == BindOutcome::not_bindable);
    CHECK(km.bind(C::Save, KeyEvent{Key::Char, U'x', 0}).kind == BindOutcome::not_bindable);
    CHECK(km.is_default(C::Save));
}

TEST_CASE("unbind removes one key; a freed key can go to another command") {
    Keymap km;
    CHECK_FALSE(km.unbind(C::Save, K(Key::F5)));  // not one of its keys
    CHECK(km.unbind(C::MoveLineStart, ctrl('a')));
    CHECK(labels(km, C::MoveLineStart) == std::vector<std::string>{"Home"});
    CHECK_FALSE(km.lookup(ctrl('a')).has_value());
    CHECK(km.bind(C::Find, ctrl('a')).kind == BindOutcome::bound);
    CHECK(km.lookup(ctrl('a')) == C::Find);
    CHECK(km.unbind(C::Save, ctrl('s')));
    CHECK(km.binding_label(C::Save).empty());
}

TEST_CASE("reset gives a command its default keys again, except those another command now has") {
    Keymap km;
    REQUIRE(km.unbind(C::MoveLineStart, ctrl('a')));
    REQUIRE(km.bind(C::Find, ctrl('a')).kind == BindOutcome::bound);
    REQUIRE(km.bind(C::MoveLineStart, K(Key::F5)).kind == BindOutcome::bound);
    const std::vector<KeyEvent> kept_elsewhere = km.reset(C::MoveLineStart);
    REQUIRE(kept_elsewhere.size() == 1);
    CHECK(kept_elsewhere[0] == ctrl('a'));
    CHECK(labels(km, C::MoveLineStart) == std::vector<std::string>{"Home"});
    CHECK_FALSE(km.lookup(K(Key::F5)).has_value());
    CHECK(km.reset(C::Find).empty());
    CHECK(km.reset(C::MoveLineStart).empty());
    CHECK(labels(km, C::MoveLineStart) == std::vector<std::string>{"Ctrl+A", "Home"});
    REQUIRE(km.bind(C::SaveAs, K(Key::F12)).kind == BindOutcome::bound);
    km.reset_all();
    CHECK(km.overrides().size() == 0);
    CHECK(km.binding_label(C::SaveAs).empty());
}

TEST_CASE("overrides lists only the commands that differ, and round-trips") {
    Keymap km;
    CHECK(km.overrides() == Json::object());
    REQUIRE(km.bind(C::GotoLine, K(Key::F5)).kind == BindOutcome::bound);
    REQUIRE(km.unbind(C::Save, ctrl('s')));
    CHECK(km.overrides() == json(R"({"Save": [], "GotoLine": ["Ctrl+G", "F5"]})"));

    Keymap other;
    CHECK(other.apply_overrides(km.overrides()).empty());
    CHECK(other.lookup(K(Key::F5)) == C::GotoLine);
    CHECK_FALSE(other.lookup(ctrl('s')).has_value());
    CHECK(other.overrides() == km.overrides());
    CHECK(other.apply_overrides(Json::object()).empty());  // applying starts from the defaults
    CHECK(other.overrides() == Json::object());
    CHECK(other.lookup(ctrl('s')) == C::Save);
}

TEST_CASE("an override replaces the command's whole key list and beats a default owner of the same key") {
    Keymap km;
    // Ctrl+S is Save's by default; the user gave it to GotoLine.
    CHECK(km.apply_overrides(json(R"({"GotoLine": ["Ctrl+S"]})")).empty());
    CHECK(km.lookup(ctrl('s')) == C::GotoLine);
    CHECK_FALSE(km.lookup(ctrl('g')).has_value());
    CHECK(km.binding_label(C::Save).empty());
    // Saving now names both commands, so the file says what happened.
    CHECK(km.overrides() == json(R"({"Save": [], "GotoLine": ["Ctrl+S"]})"));
}

TEST_CASE("bad override entries are skipped with a warning each; the rest still apply") {
    Keymap km;
    const auto warnings = km.apply_overrides(json(
        R"({"NoSuchCommand": ["F5"], "GotoLine": ["F5", "Hyper+X", "x", 7], "Find": "Ctrl+F", "SaveAs": ["F5"], "RecentSetting1": ["F6"]})"));
    CHECK(km.lookup(K(Key::F5)) == C::GotoLine);
    CHECK(labels(km, C::GotoLine) == std::vector<std::string>{"F5"});
    CHECK(km.lookup(ctrl('f')) == C::Find);  // untouched: its entry was not a list
    CHECK(km.binding_label(C::SaveAs).empty());
    CHECK_FALSE(km.lookup(K(Key::F6)).has_value());
    REQUIRE(warnings.size() == 7);
    CHECK(warnings[0] == "unknown command NoSuchCommand");
    CHECK(warnings[1] == "GotoLine: Hyper+X is not a key");
    CHECK(warnings[2] == "GotoLine: x cannot be bound");
    CHECK(warnings[3] == "GotoLine: a key must be text");
    CHECK(warnings[4] == "Find: the keys must be a list");
    CHECK(warnings[5] == "SaveAs: F5 is already bound to GotoLine");
    CHECK(warnings[6] == "RecentSetting1 cannot have keys");
    CHECK(km.apply_overrides(json(R"(["F5"])")) == std::vector<std::string>{"keymap must be an object"});
}

TEST_CASE("command display names") {
    CHECK(command_display_name(C::Save) == "Save");
    CHECK(command_display_name(C::GotoLine) == "Go to Line");
    CHECK(command_display_name(C::MoveWordLeft) == "Move Word Left");
    CHECK(command_display_name(C::OpenMenuFile) == "Open Menu File");
    CHECK(command_info(C::Save).bindable);
    CHECK_FALSE(command_info(C::RecentSetting3).bindable);
    CHECK(command_by_name("GotoLine") == C::GotoLine);
    CHECK_FALSE(command_by_name("gotoline").has_value());
}

TEST_CASE("Ctrl+T suspends and Ctrl+K cuts to the line end; Ctrl+Z and Ctrl+Y stay undo and redo") {
    Keymap km;
    CHECK(km.lookup(ctrl('t')) == C::Suspend);
    CHECK(km.lookup(ctrl('k')) == C::CutToLineEnd);
    CHECK(km.lookup(ctrl('z')) == C::Undo);
    CHECK(km.lookup(ctrl('y')) == C::Redo);
    CHECK(km.binding_label(C::Suspend) == "Ctrl+T");
    CHECK(km.binding_label(C::CutToLineEnd) == "Ctrl+K");
    CHECK(command_info(C::Suspend).bindable);
}

TEST_CASE("which commands edit the document, for read-only mode") {
    for (C c : {C::Undo, C::Redo, C::Cut, C::CutToLineEnd, C::Paste, C::Newline, C::InsertTab, C::DeleteBack, C::DeleteForward,
                C::DeleteWordBack, C::DeleteWordForward, C::UndoHistory, C::NextBranch, C::PrevBranch, C::ClearHistory, C::TrimHistory}) {
        CAPTURE(command_info(c).name);
        CHECK(command_edits(c));
    }
    for (C c : {C::Copy, C::Find, C::FindNext, C::Save, C::Open, C::MoveLeft, C::SelectRight, C::ToggleReadOnly, C::Exit, C::Suspend}) {
        CAPTURE(command_info(c).name);
        CHECK_FALSE(command_edits(c));
    }
}

TEST_CASE("F1 opens the help") {
    Keymap km;
    CHECK(km.lookup(K(Key::F1)) == C::ShowHelp);
    CHECK(km.binding_label(C::ShowHelp) == "F1");
    CHECK_FALSE(command_edits(C::ShowHelp));
}
