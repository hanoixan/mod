#include "app/commands.hpp"

#include <array>
#include <utility>

namespace mod {
namespace {

using C = CommandId;

constexpr std::array<CommandInfo, kCommandCount> kTable{{
    {C::Open, "Open", "Open…"},
    {C::Save, "Save", "Save"},
    {C::SaveAs, "SaveAs", "Save As…"},
    {C::CloseDocument, "CloseDocument", "Close"},
    {C::Suspend, "Suspend", "Suspend"},
    {C::ClearHistory, "ClearHistory", "Clear History…"},
    {C::TrimHistory, "TrimHistory", "Trim History…"},
    {C::TogglePersistHistory, "TogglePersistHistory", "Persist History"},
    {C::Exit, "Exit", "Exit"},
    {C::Undo, "Undo", "Undo"},
    {C::Redo, "Redo", "Redo"},
    {C::UndoHistory, "UndoHistory", "Undo History…"},
    {C::NextBranch, "NextBranch", "Next Branch"},
    {C::PrevBranch, "PrevBranch", "Previous Branch"},
    {C::Cut, "Cut", "Cut"},
    {C::CutToLineEnd, "CutToLineEnd", "Cut to Line End"},
    {C::Copy, "Copy", "Copy"},
    {C::Paste, "Paste", "Paste"},
    {C::Find, "Find", "Find/Replace"},
    {C::FindNext, "FindNext", "Find Next"},
    {C::FindPrev, "FindPrev", "Find Previous"},
    {C::GotoLine, "GotoLine", "Go to Line…"},
    {C::ToggleLineNumbers, "ToggleLineNumbers", "Line Numbers"},
    {C::ToggleSyntax, "ToggleSyntax", "Syntax Coloring"},
    {C::ToggleWordWrap, "ToggleWordWrap", "Word Wrap"},
    {C::ToggleReadOnly, "ToggleReadOnly", "Read Only"},
    {C::PinFolderTree, "PinFolderTree", "Pin Folder Tree"},
    {C::Split, "Split", "Split"},
    {C::Unsplit, "Unsplit", "Unsplit"},
    {C::UserSettings, "UserSettings", "User Settings…"},
    {C::Colors, "Colors", "Colors…"},
    {C::KeyBindings, "KeyBindings", "Key Bindings…"},
    {C::RecentSetting1, "RecentSetting1", "", false},
    {C::RecentSetting2, "RecentSetting2", "", false},
    {C::RecentSetting3, "RecentSetting3", "", false},
    {C::RecentSetting4, "RecentSetting4", "", false},
    {C::RecentSetting5, "RecentSetting5", "", false},
    {C::ShowDocument, "ShowDocument", "", false},
    {C::ShowHelp, "ShowHelp", "Documentation"},
    {C::About, "About", "About mod"},
    {C::MoveLeft, "MoveLeft", ""},
    {C::MoveRight, "MoveRight", ""},
    {C::MoveWordLeft, "MoveWordLeft", ""},
    {C::MoveWordRight, "MoveWordRight", ""},
    {C::MoveUp, "MoveUp", ""},
    {C::MoveDown, "MoveDown", ""},
    {C::MoveLineStart, "MoveLineStart", ""},
    {C::MoveLineEnd, "MoveLineEnd", ""},
    {C::MovePageUp, "MovePageUp", ""},
    {C::MovePageDown, "MovePageDown", ""},
    {C::MoveDocStart, "MoveDocStart", ""},
    {C::MoveDocEnd, "MoveDocEnd", ""},
    {C::SelectLeft, "SelectLeft", ""},
    {C::SelectRight, "SelectRight", ""},
    {C::SelectWordLeft, "SelectWordLeft", ""},
    {C::SelectWordRight, "SelectWordRight", ""},
    {C::SelectUp, "SelectUp", ""},
    {C::SelectDown, "SelectDown", ""},
    {C::SelectLineStart, "SelectLineStart", ""},
    {C::SelectLineEnd, "SelectLineEnd", ""},
    {C::SelectPageUp, "SelectPageUp", ""},
    {C::SelectPageDown, "SelectPageDown", ""},
    {C::SelectDocStart, "SelectDocStart", ""},
    {C::SelectDocEnd, "SelectDocEnd", ""},
    {C::Newline, "Newline", ""},
    {C::InsertTab, "InsertTab", ""},
    {C::Outdent, "Outdent", ""},
    {C::DeleteBack, "DeleteBack", ""},
    {C::DeleteForward, "DeleteForward", ""},
    {C::DeleteWordBack, "DeleteWordBack", ""},
    {C::DeleteWordForward, "DeleteWordForward", ""},
    {C::ShowMenu, "ShowMenu", ""},
    {C::OpenMenuFile, "OpenMenuFile", ""},
    {C::OpenMenuEdit, "OpenMenuEdit", ""},
    {C::OpenMenuView, "OpenMenuView", ""},
    {C::OpenMenuDocuments, "OpenMenuDocuments", ""},
    {C::OpenMenuOptions, "OpenMenuOptions", ""},
    {C::OpenMenuHelp, "OpenMenuHelp", ""},
}};

consteval bool table_in_enum_order() {
    for (std::size_t i = 0; i < kTable.size(); ++i) {
        if (std::to_underlying(kTable[i].id) != static_cast<int>(i)) return false;
    }
    return true;
}
static_assert(table_in_enum_order(), "the command table must list every CommandId in enum order");

}  // namespace

const CommandInfo& command_info(CommandId id) { return kTable[static_cast<std::size_t>(std::to_underlying(id))]; }

std::span<const CommandInfo> all_commands() { return kTable; }

std::optional<CommandId> command_by_name(std::string_view name) {
    if (name == "PruneHistory") return C::TrimHistory;  // its name before it was renamed
    for (const CommandInfo& info : kTable) {
        if (info.name == name) return info.id;
    }
    return std::nullopt;
}

std::string command_display_name(CommandId id) {
    const CommandInfo& info = command_info(id);
    if (!info.menu_label.empty()) {
        std::string label(info.menu_label);
        if (label.ends_with("…")) label.resize(label.size() - std::string_view("…").size());
        return label;
    }
    std::string out;
    for (const char c : info.name) {
        if (c >= 'A' && c <= 'Z' && !out.empty()) out += ' ';
        out += c;
    }
    return out;
}

bool command_edits(CommandId id) {
    switch (id) {
        case C::Undo:
        case C::Redo:
        case C::UndoHistory:
        case C::NextBranch:
        case C::PrevBranch:
        case C::ClearHistory:
        case C::TrimHistory:
        case C::Cut:
        case C::CutToLineEnd:
        case C::Paste:
        case C::Newline:
        case C::InsertTab:
        case C::Outdent:
        case C::DeleteBack:
        case C::DeleteForward:
        case C::DeleteWordBack:
        case C::DeleteWordForward: return true;
        default: return false;
    }
}

CommandId recent_setting_command(std::size_t index) {
    return static_cast<CommandId>(std::to_underlying(C::RecentSetting1) + static_cast<int>(index));
}

std::optional<std::size_t> recent_setting_index(CommandId id) {
    const int i = std::to_underlying(id) - std::to_underlying(C::RecentSetting1);
    if (i < 0 || i >= static_cast<int>(kRecentSettingCount)) return std::nullopt;
    return static_cast<std::size_t>(i);
}

}  // namespace mod
