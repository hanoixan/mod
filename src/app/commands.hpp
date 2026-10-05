#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mod {

enum class CommandId {
    // Files.
    Open,
    Save,
    SaveAs,
    CloseDocument,
    Suspend,
    ClearHistory,
    TrimHistory,
    TogglePersistHistory,
    Exit,
    // History.
    Undo,
    Redo,
    UndoHistory,
    NextBranch,
    PrevBranch,
    // Clipboard.
    Cut,
    CutToLineEnd,
    Copy,
    Paste,
    // Search and navigation.
    Find,
    FindNext,
    FindPrev,
    GotoLine,
    // View toggles.
    ToggleLineNumbers,
    ToggleSyntax,
    ToggleWordWrap,
    ToggleReadOnly,
    PinFolderTree,
    Split,
    Unsplit,
    // Options. KeyBindings opens the key bindings editor, Colors the Colors editor. The RecentSetting commands are the Options menu's recent-settings
    // items, newest first, and must stay consecutive.
    UserSettings,
    Colors,
    KeyBindings,
    RecentSetting1,
    RecentSetting2,
    RecentSetting3,
    RecentSetting4,
    RecentSetting5,
    // Documents: the Documents menu's items; which one comes with the menu item.
    ShowDocument,
    // Help.
    ShowHelp,
    About,
    // Motions, then their Shift twins in the same order.
    MoveLeft,
    MoveRight,
    MoveWordLeft,
    MoveWordRight,
    MoveUp,
    MoveDown,
    MoveLineStart,
    MoveLineEnd,
    MovePageUp,
    MovePageDown,
    MoveDocStart,
    MoveDocEnd,
    SelectLeft,
    SelectRight,
    SelectWordLeft,
    SelectWordRight,
    SelectUp,
    SelectDown,
    SelectLineStart,
    SelectLineEnd,
    SelectPageUp,
    SelectPageDown,
    SelectDocStart,
    SelectDocEnd,
    // Editing.
    Newline,
    InsertTab,
    Outdent,
    DeleteBack,
    DeleteForward,
    DeleteWordBack,
    DeleteWordForward,
    // Menus.
    ShowMenu,
    OpenMenuFile,
    OpenMenuEdit,
    OpenMenuView,
    OpenMenuDocuments,
    OpenMenuOptions,
    OpenMenuHelp,
    Count_,  // not a command: the number of commands
};

inline constexpr std::size_t kCommandCount = static_cast<std::size_t>(CommandId::Count_);

struct CommandInfo {
    CommandId id;
    std::string_view name;
    std::string_view menu_label;  // empty for commands that are not in a menu
    bool bindable = true;         // false for commands that cannot have keys
};

inline constexpr std::size_t kRecentSettingCount = 5;

const CommandInfo& command_info(CommandId id);
// The command of the Options menu's recent-settings item `index` (0 is the newest), and back.
CommandId recent_setting_command(std::size_t index);
std::optional<std::size_t> recent_setting_index(CommandId id);
std::span<const CommandInfo> all_commands();
// The command whose `name` is exactly `name`.
std::optional<CommandId> command_by_name(std::string_view name);
// A name to show: the menu label without its "…", or the name with its words spaced.
std::string command_display_name(CommandId id);
// Whether the command changes the document's text or its history; read-only mode refuses those.
bool command_edits(CommandId id);

}  // namespace mod
