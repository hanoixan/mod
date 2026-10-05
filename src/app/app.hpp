#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <map>
#include <span>
#include <string>
#include <vector>
#include <string_view>

#include "app/cli_options.hpp"
#include "app/commands.hpp"
#include "app/doc_search.hpp"
#include "app/document_list.hpp"
#include "app/workspace.hpp"
#include "app/document_slot.hpp"
#include "app/folder_tree.hpp"
#include "app/history_preview.hpp"
#include "app/read_only.hpp"
#include "app/keymap.hpp"
#include "app/settings.hpp"
#include "edit/clipboard.hpp"
#include "edit/document.hpp"
#include "edit/editor.hpp"
#include "platform/terminal.hpp"
#include "search/search.hpp"
#include "syntax/highlight.hpp"
#include "syntax/language_config.hpp"
#include "syntax/lsp_pool.hpp"
#include "ui/doc_search_view.hpp"
#include "ui/editor_view.hpp"
#include "ui/history_view.hpp"
#include "ui/input.hpp"
#include "ui/colors_view.hpp"
#include "ui/confirm_bar.hpp"
#include "ui/file_dialog.hpp"
#include "ui/folder_tree_view.hpp"
#include "ui/help_viewer.hpp"
#include "ui/keymap_view.hpp"
#include "ui/menu.hpp"
#include "ui/prompt.hpp"
#include "ui/screen.hpp"
#include "ui/settings_view.hpp"
#include "util/event_queue.hpp"
#include "util/progress.hpp"

namespace mod {

// The application: owns every long-lived component, runs the event loop, routes input
// and executes commands. One instance, main thread only.
class App {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr std::chrono::seconds kStatusTimeout{5};
    static constexpr std::chrono::seconds kExternalCheckInterval{2};
    static constexpr std::int64_t kSlowLoadMs = 5000;
    // How long the terminal has to answer the device attributes query at startup.
    static constexpr std::chrono::milliseconds kDeviceAttributesWait{100};
    // How often the slow-load prune offer is retried while the history is still verifying.
    static constexpr std::chrono::milliseconds kPruneOfferRetry{250};

    // Each path opens as a document, the first one shown; none starts an untitled buffer.
    App(const CliOptions& options, std::unique_ptr<Terminal> terminal);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();
    void run_command(CommandId id);

private:
    struct Layout {
        Rect text;               // the text area with its gutter; empty while hidden
        std::optional<Rect> pane;  // the history pane, inside its frame
        std::optional<Rect> panel;  // a full-width panel (User Settings, Key Bindings)
        std::optional<Rect> help;   // the help viewer, over the text
        std::optional<Rect> dialog;  // the file dialog, over the text
        struct Other {
            std::size_t index;  // into the workspace's views
            Rect text;
            int status_row;
        };
        std::vector<Other> others;  // the splits other than the focused one, when several are shown
        bool panel_framed = false;
        Rect prompt;                   // in the band, or the info overlay just above it
        std::optional<Rect> confirm;   // a question, in the band above the prompt
        std::optional<int> menu_row;   // the menu bar, the screen's top line
        int band = 0;                  // the rows of the bands: the menu bar at the top, interactions at the bottom
        bool own_line = false;         // a full-screen UI's line replaces the document's status line
        std::optional<Rect> tree;      // the folder tree's panel, its own line at its bottom
        int left = 0;                  // the views' first column: right of the tree and its divider
        int status_row = 0;
    };

    // Startup and documents.
    DocumentOptions document_options();
    // Builds the editor, searcher, view and highlighter for `doc` into the focused view.
    void install_document(std::unique_ptr<Document> doc, std::shared_ptr<ViewOptions> options = nullptr);
    // Replaces the shown document with `path` (or a new untitled one).
    Status open_document(const std::optional<std::filesystem::path>& path);
    void adopt_document(std::unique_ptr<Document> doc);
    // File > Open and the command line: a new document, or a switch to one already open.
    Status open_new_document(const std::optional<std::filesystem::path>& path);
    // The shown document is an untitled, unchanged, empty buffer that opening a file replaces.
    bool replaceable_untitled() const;
    void show_document(DocumentList::Id id);
    void close_shown();
    void exit_next(std::vector<DocumentList::Id> pending);
    std::vector<DocumentMenuEntry> document_entries() const;
    void pick_highlighter();
    void pick_highlighter(DocumentSlot& slot);
    void drop_highlighter();
    void drop_highlighter(DocumentSlot& slot);
    std::string file_name() const;

    // Flows.
    void save_document(std::optional<std::filesystem::path> target, std::function<void()> then);
    void save_now(std::optional<std::filesystem::path> target, SaveMode mode, std::function<void()> then);
    void handle_external_change(std::function<void()> proceed = {});
    void reload_document();
    void offer_prune();
    void prune_ask_age(std::uint64_t oldest_days);
    void prune_confirm(std::uint64_t days);
    void dirty_check(std::string question, std::function<void()> proceed);
    void confirm_clear_history();
    void refresh_history_view();
    void request_exit();
    void open_find();
    void goto_line();
    // Settings: save a value, then make the running session follow it.
    void apply_setting(const SettingSpec& spec, std::int64_t value);
    void apply_to_session(const SettingSpec& spec);
    void edit_setting_prompt(const SettingSpec& spec);
    void run_recent_setting(std::size_t index);
    void handle_settings_key(const KeyEvent& key);
    void handle_keymap_key(const KeyEvent& key);
    void save_keymap();
    void handle_colors_key(const KeyEvent& key);
    void save_colors();
    void toggle_persist_history(bool overwrite);
    // The command line's -ro and --persist-history, for the documents it opened.
    void apply_start_options(bool open_read_only, bool persist_history);
    void persist_next(std::vector<std::shared_ptr<Document>> docs);
    void run_flashed_command();
    // Split views.
    DocumentSlot share_slot(const DocumentSlot& from);
    void release_focused();
    void on_focus_changed();
    void focus_split(std::size_t index);
    void split_view();
    void unsplit_view();
    void trim_splits();
    bool single_view() const;
    // Per-document View options.
    ViewOptions options_on_open() const;
    bool read_only() const { return shown().options && shown().options->read_only; }
    void apply_options(DocumentSlot& s);
    void apply_options_to_views_of(const ViewOptions* options);
    void set_read_only_in_other_views(bool on);
    void open_file_dialog(FileDialogMode mode, std::function<void(const std::filesystem::path&)> on_chosen);
    void handle_file_dialog_key(const KeyEvent& key);
    void quit_on_escapes();
    void report_preview(const Status& s);
    // The folder tree.
    void pin_tree(bool on);
    void handle_tree_key(const KeyEvent& key);
    void open_preview(const std::filesystem::path& path);
    void close_preview();
    void handle_preview_key(const KeyEvent& key);
    TerminalMode resolve_terminal_mode(std::string& typed);
    // The full-width panels. Opening one closes the others, so at most one is open.
    enum class Panel { none, settings, keymap, colors, doc_search };
    Panel panel() const noexcept;
    bool panel_open() const noexcept { return panel() != Panel::none; }
    void close_panels();
    void open_doc_search();  // the help's Search Help
    void draw_frame(int top, int bottom, std::optional<int> split, std::string_view title);

    // Input and output.
    void dispatch(const InputEvent& event);
    void dispatch_key(const KeyEvent& key);
    void after_command();

    // The focused view's slot and read-only state.
    DocumentSlot& shown() { return ws_.focused().slot; }
    const DocumentSlot& shown() const { return ws_.focused().slot; }
    ReadOnlyState& ro() { return ws_.focused().ro; }
    const ReadOnlyState& ro() const { return ws_.focused().ro; }
    void enter_help();
    // The status message for an edit refused in read-only mode or in the help.
    void refuse_edit();
    void leave_help();
    // Commands the help view handles itself; any other leaves it first.
    static bool stays_in_help(CommandId id);
    void toggle_read_only();
    void leave_read_only();
    // Keys that mean something else in read-only mode (Tab, Enter, Space, Ctrl+Left/Right);
    // true when consumed.
    bool read_only_key(const KeyEvent& key);
    const MarkdownOutline& outline();
    bool shown_is_markdown() const;
    void follow_link();
    bool show_entry(const TrailEntry& entry, const std::string& anchor = {});
    TrailEntry here() const;
    // Gives the editor the view's wrap width, so Up and Down move by the rows on screen.
    void sync_wrap();
    Layout layout() const;
    void render();
    void update_title();
    void report_progress(const Progress& p);
    void set_status(std::string message);
    // A yes-or-no question on the confirm bar: `on_yes` runs for the first button; the
    // second, and Esc, do nothing.
    void ask(std::string question, std::string yes, std::function<void()> on_yes, std::string no = "Cancel");
    void poll_status();
    void step_search();
    int wait_timeout_ms();
    void handle_events(WaitEvents ev, std::span<std::byte> buf);

    // Declaration order is the reverse of destruction; ~App also tears down explicitly.
    std::unique_ptr<Terminal> terminal_;
    EventQueue queue_;
    LspServerPool lsp_pool_{queue_};  // servers shared by the documents of one language and project
    Screen screen_;
    TerminalMode terminal_mode_ = TerminalMode::xterm;  // resolved at startup
    InputDecoder decoder_;
    Clock::time_point last_input_{};  // when the terminal last sent bytes, for the decoder's timeout
    Keymap keymap_;
    Settings settings_;
    Clipboard clipboard_;
    LanguageConfig languages_;
    MenuBar menu_;
    Prompt prompt_;
    HistoryView history_view_;
    SettingsView settings_view_;
    KeymapView keymap_view_;
    ColorsView colors_view_;

    // The open documents and the split views onto them; the focused view is the one on
    // screen that takes the keys.
    Workspace ws_;
    std::optional<std::filesystem::path> help_dir_;  // the manual's folder, once found
    HelpViewer help_viewer_;
    ConfirmBar confirm_;  // questions with buttons, above the status line
    // Open and Save As: the dialog, its mode and what to do with the chosen path.
    std::optional<FileDialog> file_dialog_;
    FileDialogMode file_dialog_mode_ = FileDialogMode::open;
    std::function<void(const std::filesystem::path&)> file_dialog_chosen_;
    HistoryPreview history_preview_;  // the Undo History pane's, in the focused view
    // The folder tree on the left: pinned or not (unpinned, it shows only while it has the
    // keys), whether it has the keys, and a file previewed from it, the only view while it lasts.
    static constexpr int kTreeWidth = 30;
    FolderTree tree_;
    FolderTreeView tree_view_;
    bool tree_pinned_ = false;
    bool tree_focus_ = false;
    std::optional<DocumentSlot> preview_;
    DocSearchView doc_search_;
    bool quit_ = false;
    bool kill_chain_ = false;       // the last event was CutToLineEnd
    bool kill_chain_prev_ = false;  // ... as it stood before the current event
    bool prune_offer_pending_ = false;
    bool external_prompt_open_ = false;
    std::string status_;
    std::string title_;  // the terminal's title as last set
    std::optional<Clock::time_point> status_deadline_;
    // A menu item chosen by its letter, shown chosen until its flash ends.
    static constexpr std::chrono::milliseconds kMenuFlash{120};
    std::optional<CommandId> flash_command_;
    std::optional<Clock::time_point> flash_deadline_;
    // Recent menu-key presses (Esc, or a Show Menu key): kQuitEscapes in a row, each within
    // kQuitGap of the one before, quit.
    static constexpr std::chrono::milliseconds kQuitGap{250};
    static constexpr std::size_t kQuitEscapes = 3;
    std::vector<Clock::time_point> escapes_;
    Clock::time_point next_external_check_;
    Clock::time_point last_progress_draw_{};
};

}  // namespace mod
