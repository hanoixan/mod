#include "app/app.hpp"
#include "app/split_layout.hpp"
#include "app/startup.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <exception>
#include <format>
#include <span>
#include <system_error>
#include <utility>

#include "syntax/markdown.hpp"
#include "syntax/layered_highlighter.hpp"
#include "syntax/semantic_highlighter.hpp"
#include "syntax/syntax_highlighter.hpp"
#include "ui/theme.hpp"
#include "util/log.hpp"

namespace mod {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kHistoryHints = "Enter: jump  Tab: text  C: clear  T: trim  P: persist  Esc: close";
constexpr std::string_view kSettingsHints = "Up/Down: move  Enter: change  Left/Right: step a number  Esc: close";
constexpr std::string_view kDocSearchHints = "Type to search every page  Up/Down: choose  Enter: open  Esc: close";
constexpr std::string_view kFileDialogHints = "Tab: next part  Enter: choose  Backspace: up a folder  Esc: cancel";
constexpr std::string_view kHelpHints = "Up/Down: links  Enter: follow  Left: back  /: search  Esc: close";
constexpr std::string_view kColorsHints = "Enter: change  Del: default  Alt+R: all defaults  Esc: close";
constexpr std::string_view kKeymapHints = "Enter: add key  Del: remove  ^R: reset  Alt+R: reset all  Esc: close";
constexpr std::chrono::milliseconds kProgressRedraw{100};
constexpr std::uint64_t kMaxPruneDays = 100'000;
constexpr std::uint64_t kMaxFindPrefill = 200;

std::optional<std::uint64_t> parse_whole(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    if (s.empty()) return std::nullopt;
    std::uint64_t v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size()) return std::nullopt;
    return v;
}

bool is_markdown(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".md" || ext == ".markdown" || ext == ".mdown" || ext == ".mkd";
}

std::optional<Motion> motion_of(CommandId id, bool& extend) {
    const auto v = std::to_underlying(id);
    const auto first_move = std::to_underlying(CommandId::MoveLeft);
    const auto first_select = std::to_underlying(CommandId::SelectLeft);
    constexpr int kMotions = 12;
    if (v >= first_move && v < first_move + kMotions) {
        extend = false;
        return static_cast<Motion>(v - first_move);
    }
    if (v >= first_select && v < first_select + kMotions) {
        extend = true;
        return static_cast<Motion>(v - first_select);
    }
    return std::nullopt;
}

}  // namespace

App::App(const CliOptions& options, std::unique_ptr<Terminal> terminal)
    : terminal_(std::move(terminal)),
      queue_([t = terminal_.get()] { t->wake(); }),
      screen_(*terminal_),
      settings_(user_config_dir()),
      clipboard_([this](std::string_view seq) {
          if (terminal_mode_ == TerminalMode::vt100) return;  // no OSC 52 on a VT100
          (void)terminal_->write(std::as_bytes(std::span(seq.data(), seq.size())));
          screen_.invalidate();
      }),
      menu_(keymap_),
      prompt_(nullptr),
      tree_([] {
          std::error_code ec;
          fs::path cwd = fs::current_path(ec);
          return ec ? fs::path(".") : cwd;
      }()) {
    tree_view_.set_tree(&tree_);
    // Settings first, so the first frame already uses the remembered values. Each document's
    // View options start from their settings as it is opened, and are then its own.
    if (auto warning = load_configuration(settings_, keymap_, active_theme(), options)) set_status(std::move(*warning));
    tree_pinned_ = settings_.flag("pin_folder_tree");
    menu_.set_recent_settings(settings_);
    screen_.set_cursor_style(settings_.cursor_style());
    auto loaded = LanguageConfig::load();
    languages_ = std::move(loaded.config);
    if (loaded.warning) set_status(std::move(*loaded.warning));

    // Start with an untitled buffer, which the first path that opens replaces; a path that
    // cannot be opened is skipped and the first such failure reported.
    ws_.focused().id = ws_.documents().add();
    (void)open_document(std::nullopt);
    std::optional<DocumentList::Id> first;
    std::string failure;
    for (const fs::path& path : options.paths) {
        if (auto s = open_new_document(path); !s) {
            if (failure.empty()) failure = "cannot open " + path.string() + ": " + s.error().message;
        } else if (!first) {
            first = ws_.documents().shown();
        }
    }
    if (first) show_document(*first);
    if (!failure.empty()) set_status(failure);
    apply_start_options(options.read_only, options.persist_history);
}

// Every document open at startup came from the command line (the untitled buffer aside).
void App::apply_start_options(bool open_read_only, bool persist_history) {
    std::vector<std::shared_ptr<Document>> persist;
    ws_.for_each_slot([&](DocumentSlot& s) {
        if (!s.doc || s.doc->is_untitled()) return;
        if (open_read_only) {
            s.options->read_only = true;
            apply_options(s);
        }
        if (persist_history) persist.push_back(s.doc);
    });
    if (open_read_only) {
        for (const DocumentList::Id id : ws_.documents().order()) {
            View* v = ws_.entry_of(id);
            if (v != nullptr && v->slot.view) v->ro.nav.reset(TrailEntry{v->slot.doc->path(), v->slot.editor->cursor(), v->slot.view->top()});
        }
    }
    if (!persist.empty()) persist_next(std::move(persist));
}

// --persist-history: each document's history goes to its sidecar in turn; an unreadable
// sidecar in the way is asked about, as Persist History in the Undo History pane does.
void App::persist_next(std::vector<std::shared_ptr<Document>> docs) {
    while (!docs.empty()) {
        const std::shared_ptr<Document> doc = docs.front();
        docs.erase(docs.begin());
        if (doc->persist_history()) continue;
        const std::string name = sidecar_path_for(doc->path()).filename().string();
        if (auto s = doc->set_persist_history(true); !s) {
            if (s.error().code == ErrorCode::format) {
                confirm_.open(std::format("Overwrite the unreadable history file {}?", name), {"Overwrite", "Cancel"}, 1,
                              [this, doc, docs, name](int choice) {
                                  if (choice == 0) {
                                      if (auto o = doc->set_persist_history(true, true); !o) set_status("history not saved: " + o.error().message);
                                  }
                                  persist_next(docs);
                              });
                return;
            }
            set_status("history not saved for " + doc->path().filename().string() + ": " + s.error().message);
        }
    }
    if (shown().doc) history_view_.set_persist(shown().doc->persist_history());
}

App::~App() {
    // Highlighter (and LSP) first, then the document (sidecar flush, scanner join), then
    // the queue, then the terminal.
    prompt_.set_searcher(nullptr);
    history_view_.close();
    close_preview();
    ws_.for_each_slot([](DocumentSlot& s) { s.release_highlighter(); });
    ws_.clear();  // every view, waiting document and parked document, each torn down in order
    queue_.close();
    terminal_->restore();
}

// ---- documents --------------------------------------------------------------------------

DocumentOptions App::document_options() {
    DocumentOptions o;
    o.progress = [this](const Progress& p) { report_progress(p); };
    return o;
}

Status App::open_document(const std::optional<fs::path>& path) {
    std::unique_ptr<Document> doc;
    if (path) {
        auto opened = Document::open(*path, queue_, document_options());
        if (!opened) return std::unexpected(opened.error());
        doc = std::move(*opened);
    } else {
        doc = Document::open_untitled(queue_, document_options());
    }
    adopt_document(std::move(doc));
    return {};
}

void App::adopt_document(std::unique_ptr<Document> doc) {
    history_view_.close();
    leave_read_only();  // a newly opened file starts editable
    install_document(std::move(doc));
    prune_offer_pending_ = shown().doc->history_load_ms() > kSlowLoadMs;
    next_external_check_ = Clock::now() + kExternalCheckInterval;
}

// ---- the open documents ------------------------------------------------------------------

bool App::replaceable_untitled() const {
    return !ro().away && shown().doc && shown().doc->is_untitled() && !shown().doc->is_dirty() && shown().doc->text().size() == 0;
}


Status App::open_new_document(const std::optional<fs::path>& path) {
    if (path) {
        if (const auto id = ws_.find(*path)) {
            show_document(*id);
            return {};
        }
    }
    if (replaceable_untitled()) return open_document(path);
    // Opened first, so that a file that cannot be opened leaves the shown document alone.
    std::unique_ptr<Document> doc;
    if (path) {
        auto opened = Document::open(*path, queue_, document_options());
        if (!opened) return std::unexpected(opened.error());
        doc = std::move(*opened);
    } else {
        doc = Document::open_untitled(queue_, document_options());
    }
    release_focused();
    ws_.focused().id = ws_.documents().add();
    adopt_document(std::move(doc));
    after_command();
    return {};
}

// ---- split views -------------------------------------------------------------------------

// A new view of `from`'s document: the document and its highlighter are shared, the editor,
// searcher and view are new, at the same place.
DocumentSlot App::share_slot(const DocumentSlot& from) {
    DocumentSlot s;
    s.doc = from.doc;
    s.highlighter = from.highlighter;
    s.options = from.options;
    s.editor = std::make_unique<Editor>(*s.doc, clipboard_, settings_.tab_width());
    if (from.editor) s.editor->select_range(from.editor->cursor(), from.editor->cursor());
    s.searcher = std::make_unique<Searcher>(*s.doc, *s.editor);
    s.view = std::make_unique<EditorView>(*s.doc, *s.editor, s.highlighter.get(), settings_.tab_width());
    apply_options(s);
    if (from.view) s.view->set_top(from.view->top());
    return s;
}

// The focused view lets go of its document (dropped when another view still shows it,
// parked otherwise); the find bar and the history pane belonged to it.
void App::release_focused() {
    prompt_.set_searcher(nullptr);
    history_view_.close();
    ws_.release_focused();
}

// Everything that follows the focused view when it changes or shows another document.
void App::on_focus_changed() {
    prompt_.set_searcher(shown().searcher.get());
    if (shown().highlighter) shown().highlighter_deadline = Clock::now();
    next_external_check_ = Clock::now();  // a document is checked as soon as it is shown
    screen_.invalidate();
    set_status(file_name());
}

// Moves the focus to split `index`, which takes the keys from now on.
void App::focus_split(std::size_t index) {
    if (index == ws_.focus()) return;
    prompt_.set_searcher(nullptr);
    history_preview_.end(shown());
    history_view_.close();
    ws_.focus_on(index);
    on_focus_changed();
}

// View > Split: a second view of the focused view's document below it; the focus stays.
void App::split_view() {
    if (static_cast<int>(ws_.size()) >= max_splits(screen_.rows())) {
        set_status("no room for another split");
        return;
    }
    View v;
    v.id = ws_.focused().id;
    v.slot = share_slot(shown());
    if (read_only()) v.ro.nav.reset(here());
    ws_.insert_below_focus(std::move(v));
    screen_.invalidate();
}

// View > Unsplit: the focused view goes (its document stays open); the one above, else
// below, takes the focus. With one view, nothing happens.
void App::unsplit_view() {
    if (ws_.size() < 2) {
        set_status("there is only one view");
        return;
    }
    release_focused();
    ws_.remove_focused();
    on_focus_changed();
}

// After a resize: the bottom views go until the screen holds the rest (H / 3).
void App::trim_splits() {
    const auto max = static_cast<std::size_t>(max_splits(screen_.rows()));
    if (ws_.focus() >= max) {
        // The focused view goes, as when the focus moves: its preview ends on its own slot,
        // and the history pane, which lists its document's nodes, closes.
        history_preview_.end(shown());
        history_view_.close();
        prompt_.set_searcher(nullptr);
    }
    const std::size_t focus = ws_.focus();
    ws_.trim(max);
    if (ws_.focus() != focus) on_focus_changed();
}

// Only the focused view is laid out while a pane, a panel, a dialog or the help is open.
bool App::single_view() const {
    return ws_.size() < 2 || history_view_.is_open() || file_dialog_ || panel_open() || help_viewer_.is_open() || preview_.has_value();
}

// The View options a document gets as it is opened.
ViewOptions App::options_on_open() const {
    ViewOptions o;
    o.line_numbers = settings_.flag("line_numbers");
    o.syntax = settings_.flag("syntax_coloring");
    o.word_wrap = settings_.flag("word_wrap");
    return o;
}

// The slot's view follows its document's View options.
void App::apply_options(DocumentSlot& s) {
    if (!s.view || !s.options) return;
    s.view->set_line_numbers(s.options->line_numbers);
    s.view->set_word_wrap(s.options->word_wrap);
    s.view->set_read_only(s.options->read_only);
    // Markdown in read-only mode is drawn laid out for reading.
    s.view->set_reading(s.options->read_only && s.doc && !s.doc->is_untitled() && is_markdown(s.doc->path()));
}

void App::apply_options_to_views_of(const ViewOptions* options) {
    ws_.for_each_view([&](DocumentSlot& v) {
        if (v.options.get() == options) apply_options(v);
    });
}

// Read-only is the document's: the other views of the focused document follow it.
// Turning it off brings a view that followed a link back to the document.
void App::set_read_only_in_other_views(bool on) {
    for (std::size_t i = 0; i < ws_.size(); ++i) {
        View& v = ws_.at(i);
        if (i == ws_.focus() || !v.slot.view || v.slot.options != shown().options) continue;
        if (!on && v.ro.away) {
            v.slot = std::move(v.ro.base);
            v.ro.away = false;
        }
        v.ro.outline_doc = nullptr;
        v.ro.nav.reset(on ? TrailEntry{v.slot.doc->path(), v.slot.editor->cursor(), v.slot.view->top()} : TrailEntry{});
        apply_options(v.slot);
    }
}

void App::show_document(DocumentList::Id id) {
    if (ws_.focused().id == id) return;
    // Shown in another view: this view gets a view of its own on the same document.
    if (const auto other = ws_.other_view_of(id)) {
        DocumentSlot shared = share_slot(ws_.at(*other).slot);
        release_focused();
        ws_.focused().id = id;
        shown() = std::move(shared);
        ws_.documents().show(id);
        if (read_only()) ro().nav.reset(here());
        on_focus_changed();
        after_command();
        return;
    }
    if (!ws_.entry_of(id)) return;
    release_focused();
    ws_.take_parked(id);
    on_focus_changed();
    after_command();
}

// Drops the shown document and shows the one viewed most recently before it, or a new
// untitled buffer when it was the last. Its other views go with it.
void App::close_shown() {
    const DocumentList::Id id = ws_.focused().id;
    if (id == 0) return;
    prompt_.set_searcher(nullptr);
    history_view_.close();
    ws_.remove_other_views_of(id);
    ws_.focused() = View{};
    if (const auto next = ws_.documents().remove(id)) {
        if (const auto other = ws_.other_view_of(*next)) {
            View v;
            v.id = *next;
            v.slot = share_slot(ws_.at(*other).slot);
            ws_.focused() = std::move(v);
            ws_.documents().show(*next);
        } else {
            ws_.take_parked(*next);
        }
        on_focus_changed();
    } else {
        ws_.focused().id = ws_.documents().add();
        (void)open_document(std::nullopt);
    }
    screen_.invalidate();
    after_command();
}

// Exit asks about each document with unsaved changes in turn; Cancel stops the exit.
void App::exit_next(std::vector<DocumentList::Id> pending) {
    if (pending.empty()) {
        quit_ = true;
        return;
    }
    const DocumentList::Id id = pending.front();
    pending.erase(pending.begin());
    show_document(id);
    if (ro().away) leave_read_only();  // the document itself is the one asked about
    dirty_check(std::format("Save changes to {} before closing?", file_name()), [this, pending] { exit_next(pending); });
}

std::vector<DocumentMenuEntry> App::document_entries() const {
    std::vector<DocumentMenuEntry> out;
    const auto name = [](const Document* d) {
        return d == nullptr || d->is_untitled() ? std::string("[untitled]") : d->path().filename().string();
    };
    for (const DocumentList::Id id : ws_.documents().order()) {
        const View* v = ws_.entry_of(id);
        if (v == nullptr) continue;
        const Document* base = Workspace::document_of(*v);
        std::string label = name(base);
        if (v->ro.away) label += " → " + name(v->slot.doc.get());
        out.push_back({std::move(label), base != nullptr && base->is_dirty(), id == ws_.focused().id});
    }
    return out;
}

void App::install_document(std::unique_ptr<Document> doc, std::shared_ptr<ViewOptions> options) {
    prompt_.set_searcher(nullptr);
    shown().reset();
    shown().doc = std::move(doc);
    shown().editor = std::make_unique<Editor>(*shown().doc, clipboard_, settings_.tab_width());
    shown().searcher = std::make_unique<Searcher>(*shown().doc, *shown().editor);
    prompt_.set_searcher(shown().searcher.get());
    shown().view = std::make_unique<EditorView>(*shown().doc, *shown().editor, nullptr, settings_.tab_width());
    shown().options = options ? std::move(options) : std::make_shared<ViewOptions>(options_on_open());
    apply_options(shown());
    pick_highlighter();
}

void App::drop_highlighter(DocumentSlot& slot) {
    // Every view of the document lets go, so the last share leaves the document's listeners.
    ws_.for_each_slot([&](DocumentSlot& other) {
        if (&other != &slot && other.doc == slot.doc) other.release_highlighter();
    });
    slot.release_highlighter();
    slot.highlighter_deadline.reset();
}

void App::drop_highlighter() { drop_highlighter(shown()); }

void App::pick_highlighter() { pick_highlighter(shown()); }

void App::pick_highlighter(DocumentSlot& slot) {
    drop_highlighter(slot);
    if (!slot.doc || slot.doc->is_untitled()) return;
    const fs::path& p = slot.doc->path();
    if (is_markdown(p)) {
        if (slot.options->syntax) slot.highlighter = std::make_unique<MarkdownHighlighter>(*slot.doc);  // Markdown follows Syntax Coloring
    } else if (const LanguageServerSpec* spec = languages_.find_for_path(p); spec && slot.options->syntax) {
        // The syntax layer from the entry's data, the server's tokens drawn over it.
        std::unique_ptr<Highlighter> base;
        std::unique_ptr<Highlighter> overlay;
        if (spec->syntax) base = std::make_unique<SyntaxHighlighter>(*slot.doc, *spec->syntax);
        if (!spec->command.empty()) {
            if (slot.doc->text().size() > spec->max_file_bytes) {
                set_status("LSP off: file too large");
            } else {
                overlay = std::make_unique<SemanticHighlighter>(*slot.doc, *spec, lsp_pool_);
            }
        }
        if (base && overlay) {
            slot.highlighter = std::make_unique<LayeredHighlighter>(std::move(base), std::move(overlay));
        } else {
            slot.highlighter = base ? std::move(base) : std::move(overlay);
        }
    }
    if (slot.highlighter) {
        slot.doc->add_listener(slot.highlighter.get());
        slot.highlighter_deadline = Clock::now();
    }
    // Every view of the document shares it.
    ws_.for_each_slot([&](DocumentSlot& other) {
        if (&other == &slot || other.doc != slot.doc) return;
        other.highlighter = slot.highlighter;
        if (other.view) other.view->set_highlighter(slot.highlighter.get());
    });
    if (slot.view) slot.view->set_highlighter(slot.highlighter.get());
}

std::string App::file_name() const {
    if (!shown().doc || shown().doc->is_untitled()) return "[untitled]";
    return shown().doc->path().filename().string();
}

// ---- flows ------------------------------------------------------------------------------

void App::save_document(std::optional<fs::path> target, std::function<void()> then) {
    // An untitled document is named first.
    if (!target && shown().doc->is_untitled()) {
        open_file_dialog(FileDialogMode::save, [this, then = std::move(then)](const fs::path& p) { save_document(p, then); });
        return;
    }
    // Then an unanswered change on disk is settled; Reload cancels the save.
    handle_external_change([this, target = std::move(target), then = std::move(then)] { save_now(target, SaveMode::atomic, then); });
}

void App::save_now(std::optional<fs::path> target, SaveMode mode, std::function<void()> then) {
    const bool save_as = target.has_value();
    Status s = save_as ? shown().doc->save_as(*target, mode, &clipboard_) : shown().doc->save(mode, &clipboard_);
    if (!s && s.error().code == ErrorCode::not_atomic && mode == SaveMode::atomic) {
        const fs::path where = save_as ? *target : shown().doc->path();
        std::error_code ec;
        if (!fs::exists(where, ec)) {
            set_status("cannot save: " + s.error().message);
            return;
        }
        ask("Cannot save safely: " + s.error().message + ".", "Write in place (not crash-safe)",
            [this, target = std::move(target), then = std::move(then)] { save_now(target, SaveMode::in_place, then); });
        return;
    }
    if (!s) {
        set_status("save failed: " + s.error().message);
        return;
    }
    set_status("saved " + file_name());
    if (save_as) pick_highlighter();
    if (then) then();
}

void App::handle_external_change(std::function<void()> proceed) {
    if (shown().doc->is_untitled()) {
        if (proceed) proceed();
        return;
    }
    const ExternalChange change = shown().doc->check_external_change();
    if (change.kind == ExternalChange::unchanged) {
        if (proceed) proceed();
        return;
    }
    if (change.kind == ExternalChange::deleted) {
        set_status(file_name() + " deleted on disk; saving will recreate it");
        shown().doc->keep_in_memory();
        if (proceed) proceed();
        return;
    }
    external_prompt_open_ = true;
    if (change.replaced) {
        confirm_.open(file_name() + " changed on disk.", {"Reload (starts a new history; unsaved changes leave the undo path)", "Keep my version"}, 1,
                             [this, proceed = std::move(proceed)](int choice) {
                                 external_prompt_open_ = false;
                                 if (choice == 0) {
                                     reload_document();
                                 } else {
                                     shown().doc->keep_in_memory();
                                     if (proceed) proceed();
                                 }
                             });
    } else {
        confirm_.open(file_name() + " was modified in place on disk; your version cannot be kept.", {"Reload"}, 0,
                             [this](int) {
                                 external_prompt_open_ = false;
                                 reload_document();
                             });
    }
}

void App::reload_document() {
    history_view_.close();
    if (auto s = shown().doc->reload(); !s) {
        set_status("reload failed: " + s.error().message);
        return;
    }
    pick_highlighter();  // the language server gets a fresh didOpen
    set_status("reloaded " + file_name());
}

void App::dirty_check(std::string question, std::function<void()> proceed) {
    if (!shown().doc->is_dirty()) {
        proceed();
        return;
    }
    confirm_.open(std::move(question), {"Save", "Discard", "Cancel"}, 2, [this, proceed = std::move(proceed)](int choice) {
        if (choice == 0) save_document(std::nullopt, proceed);
        if (choice == 1) proceed();
    });
}

// The menu item chosen by its letter, once its flash has been seen.
void App::run_flashed_command() {
    flash_deadline_.reset();
    menu_.hide();
    if (const auto cmd = std::exchange(flash_command_, std::nullopt)) run_command(*cmd);
}

// Esc three times, each within 250 ms of the one before: whatever is open closes, and mod exits as File > Exit
// does, asking only about documents with unsaved changes.
void App::quit_on_escapes() {
    menu_.hide();
    flash_deadline_.reset();
    flash_command_.reset();
    if (prompt_.is_open()) prompt_.close();
    confirm_.close();
    file_dialog_.reset();
    history_view_.close();
    close_panels();
    if (help_viewer_.is_open()) leave_help();
    request_exit();
}

void App::request_exit() { exit_next(ws_.unsaved()); }

void App::confirm_clear_history() {
    const std::string gone = shown().doc->persist_history() ? std::format("Deleted text kept in {}.history will be gone.", file_name())
                                                           : std::string("Text it could bring back will be gone.");
    ask(std::format("Delete all undo history for {}? {}", file_name(), gone), "Clear", [this] {
        if (auto s = shown().doc->clear_history(); !s) {
            set_status("history not cleared: " + s.error().message);
        } else {
            set_status("history cleared");
        }
        refresh_history_view();
    });
}

// The Undo History pane holds rows built from the tree it was opened on; after a clear or
// a prune it is opened again so it shows the new history.
void App::refresh_history_view() {
    if (history_view_.is_open()) history_view_.open(shown().doc->history());
}

void App::offer_prune() {
    const HistoryState state = shown().doc->history_state();
    if (state == HistoryState::verifying) return;
    if (const auto current = shown().doc->history().current()) {
        const NodeId root = shown().doc->history().root_of(*current);
        if (const auto base = shown().doc->history().root_base(root); base && base->hash == ContentHash{} && base->size > 0) return;
    }
    prune_offer_pending_ = false;
    auto preview = shown().doc->prune_preview(std::nullopt);
    if (!preview) return;
    const auto oldest_days = preview->oldest_days;
    if (!oldest_days) return;
    const std::uint64_t oldest = *oldest_days;
    const double seconds = static_cast<double>(shown().doc->history_load_ms()) / 1000.0;
    ask(std::format("History took {:.1f} s to load. Trim old history?", seconds), "Trim…", [this, oldest] { prune_ask_age(oldest); }, "Not now");
}

void App::prune_ask_age(std::uint64_t oldest_days) {
    prompt_.open(PromptKind::prune_age,
                 std::format("Trim history older than how many days? Oldest change: {} days ago.", oldest_days), "",
                 [this](const std::string& text) -> std::optional<std::string> {
                     const auto days = parse_whole(text);
                     if (!days || *days > kMaxPruneDays) return std::string("enter a whole number of days");
                     prune_confirm(*days);
                     return std::nullopt;
                 });
}

void App::prune_confirm(std::uint64_t days) {
    auto preview = shown().doc->prune_preview(days);
    if (!preview) {
        set_status(preview.error().message);
        return;
    }
    if (preview->remove_count == 0) {
        set_status(std::format("nothing older than {} days", days));
        return;
    }
    const std::string plus =
        preview->removed_trees > 0 ? std::format(", plus {} unreachable earlier histories", preview->removed_trees) : std::string();
    const std::uint64_t removed = preview->remove_count;
    const std::int64_t cutoff = preview->cutoff_ms;
    ask(std::format("Remove {} of {} changes (older than {} days{})? Their text will be gone from {}.history for good.", removed,
                    removed + preview->keep_count, days, plus, file_name()),
        "Trim", [this, removed, cutoff] {
            if (auto s = shown().doc->prune_history(cutoff); !s) {
                set_status("history not trimmed: " + s.error().message);
            } else {
                set_status(std::format("history trimmed: {} changes removed", removed));
            }
            refresh_history_view();
        });
}

void App::open_find() {
    std::string initial;
    const auto sel = shown().editor->selection();
    if (sel && sel->second - sel->first <= kMaxFindPrefill) {
        initial = shown().doc->text().read(sel->first, sel->second - sel->first);
    } else {
        initial = prompt_.last_query();
    }
    const std::uint64_t origin = sel ? sel->first : shown().editor->cursor();
    prompt_.open_find(std::move(initial), origin, [this] { return shown().editor->cursor(); }, !read_only());
}

void App::goto_line() {
    prompt_.open(PromptKind::goto_line, "Go to line:", "", [this](const std::string& text) -> std::optional<std::string> {
        const auto line = parse_whole(text);
        if (!line || *line == 0) return std::string("enter a line number");
        const auto start = shown().doc->line_start(*line - 1, true);
        if (!start) return std::string("past the end of the file");
        shown().editor->select_range(*start, *start);
        after_command();
        return std::nullopt;
    });
}

void App::apply_setting(const SettingSpec& spec, std::int64_t value) {
    if (auto s = settings_.set(spec.key, value); !s) {
        set_status(std::format("{} not remembered: {}", spec.label, s.error().message));
    } else {
        set_status(spec.key == "terminal_mode"
                       ? std::format("{}: {}; applies the next time mod starts", spec.label, setting_value_text(spec, value))
                       : std::format("{}: {}", spec.label, setting_value_text(spec, value)));
    }
    apply_to_session(spec);
    menu_.set_recent_settings(settings_);
    after_command();
}

// A changed setting takes effect at once. For a View toggle's starting value that means
// the session's toggle follows it; the toggle can still be flipped for the session afterwards.
void App::apply_to_session(const SettingSpec& spec) {
    if (spec.key == "tab_width") {
        ws_.for_each_slot([&](DocumentSlot& v) {
            v.editor->set_tab_width(settings_.tab_width());
            v.view->set_tab_width(settings_.tab_width());
        });
        screen_.invalidate();
    } else if (spec.key == "pin_folder_tree") {
        pin_tree(settings_.flag(spec.key));
    } else if (spec.key == "line_numbers") {
        // An "on open" setting also sets the option on every open document.
        ws_.for_each_slot([&](DocumentSlot& v) {
            v.options->line_numbers = settings_.flag(spec.key);
            apply_options(v);
        });
    } else if (spec.key == "syntax_coloring") {
        // Each document once: its highlighter is picked again and shared with its other views.
        std::vector<DocumentSlot*> slots;
        std::vector<const Document*> seen;
        ws_.for_each_slot([&](DocumentSlot& v) {
            v.options->syntax = settings_.flag(spec.key);
            if (std::ranges::find(seen, v.doc.get()) == seen.end()) {
                seen.push_back(v.doc.get());
                slots.push_back(&v);
            }
        });
        for (DocumentSlot* v : slots) pick_highlighter(*v);
    } else if (spec.key == "word_wrap") {
        ws_.for_each_slot([&](DocumentSlot& v) {
            v.options->word_wrap = settings_.flag(spec.key);
            apply_options(v);
        });
    } else if (spec.key == "cursor_style") {
        screen_.set_cursor_style(settings_.cursor_style());
    } else if (spec.key == "darkness") {
        active_theme().set_darkness(settings_.darkness());
        screen_.invalidate();
    }
}

void App::edit_setting_prompt(const SettingSpec& spec) {
    prompt_.open(PromptKind::setting, std::format("{} ({} to {}):", spec.label, spec.min, spec.max),
                 std::to_string(settings_.value(spec.key)), [this, &spec](const std::string& text) -> std::optional<std::string> {
                     const auto v = parse_whole(text);
                     if (!v || *v < static_cast<std::uint64_t>(spec.min) || *v > static_cast<std::uint64_t>(spec.max))
                         return std::format("enter a whole number from {} to {}", spec.min, spec.max);
                     apply_setting(spec, static_cast<std::int64_t>(*v));
                     return std::nullopt;
                 });
}

// An Options menu item for a recently changed setting: an on/off setting is flipped,
// a number is asked for.
void App::run_recent_setting(std::size_t index) {
    const auto recent = settings_.recent(kRecentSettingCount);
    if (index >= recent.size()) return;
    const SettingSpec& spec = *recent[index];
    if (spec.type == SettingType::boolean) {
        apply_setting(spec, settings_.flag(spec.key) ? 0 : 1);
    } else if (spec.type == SettingType::choice) {
        const std::int64_t v = settings_.value(spec.key);
        apply_setting(spec, v >= spec.max ? spec.min : v + 1);  // the next name, wrapping
    } else {
        edit_setting_prompt(spec);
    }
}

void App::handle_settings_key(const KeyEvent& key) {
    const SettingsKeyResult r = settings_view_.handle_key(key);
    if (r.change != nullptr) apply_setting(*r.change, r.value);
    if (r.edit != nullptr) {
        if (r.edit->type == SettingType::keymap) {
            run_command(CommandId::KeyBindings);
        } else if (r.edit->type == SettingType::colors) {
            run_command(CommandId::Colors);
        } else {
            edit_setting_prompt(*r.edit);
        }
    }
    if (r.closed) after_command();
}

void App::handle_keymap_key(const KeyEvent& key) {
    const KeymapKeyResult r = keymap_view_.handle_key(key);
    if (!r.message.empty()) set_status(r.message);
    if (r.changed) save_keymap();
    if (r.reset_all) {
        ask("Reset every key binding to its default?", "Reset", [this] {
            keymap_view_.reset_all();
            set_status("all key bindings reset to their defaults");
            save_keymap();
        });
    }
    if (r.closed) after_command();
}

// The keymap is stored as the commands that differ from the defaults; with none, the
// member is removed from settings.json.
void App::save_keymap() {
    Json overrides = keymap_.overrides();
    const bool any = overrides.size() > 0;
    if (auto s = settings_.set_raw("keymap", any ? std::optional<Json>(std::move(overrides)) : std::nullopt); !s)
        set_status("key bindings not remembered: " + s.error().message);
}

// ---- the file dialog ------------------------------------------------------------------------

App::Panel App::panel() const noexcept {
    if (settings_view_.is_open()) return Panel::settings;
    if (keymap_view_.is_open()) return Panel::keymap;
    if (colors_view_.is_open()) return Panel::colors;
    if (doc_search_.is_open()) return Panel::doc_search;
    return Panel::none;
}

void App::close_panels() {
    settings_view_.close();
    keymap_view_.close();
    colors_view_.close();
    doc_search_.close();
}

void App::open_doc_search() {
    close_panels();
    doc_search_.open([this](std::string_view q) { return search_docs(*help_dir_, q); });
}

// Open and Save As: the dialog starts in the document's folder (the working directory for an
// untitled one), with the file name filled in for Save As.
void App::open_file_dialog(FileDialogMode mode, std::function<void(const fs::path&)> on_chosen) {
    history_view_.close();
    close_panels();
    std::error_code ec;
    const bool named = !shown().doc->is_untitled();
    const fs::path dir = named ? shown().doc->path().parent_path() : fs::current_path(ec);
    const std::string name = mode == FileDialogMode::save && named ? file_name() : std::string();
    file_dialog_.emplace(mode, dir, name);
    file_dialog_mode_ = mode;
    file_dialog_chosen_ = std::move(on_chosen);
}

void App::handle_file_dialog_key(const KeyEvent& key) {
    if (!file_dialog_) return;
    const Layout l = layout();
    // The dialog covers the text area, so its own rows set the page size.
    const FileDialogResult r = file_dialog_->handle_key(key, FileDialog::list_rows_for(l.dialog ? l.dialog->rows : 0));
    if (r.kind == FileDialogResult::Kind::canceled) {
        file_dialog_.reset();
        after_command();
        return;
    }
    if (r.kind != FileDialogResult::Kind::chosen) return;
    const fs::path path = r.path;
    auto finish = [this, path] {
        file_dialog_.reset();
        auto cb = std::exchange(file_dialog_chosen_, nullptr);
        if (cb) cb(path);
        after_command();
    };
    std::error_code ec;
    const bool replaces = file_dialog_mode_ == FileDialogMode::save && fs::exists(path, ec) &&
                          (shown().doc->is_untitled() || !fs::equivalent(path, shown().doc->path(), ec));
    if (!replaces) {
        finish();
        return;
    }
    // The dialog waits behind the question; Cancel brings it back as it was.
    ask(std::format("{} already exists. Replace it?", path.filename().string()), "Replace", finish);
}

// Persist History: on writes the whole history to <file>.history, off keeps it in memory only.
void App::toggle_persist_history(bool overwrite) {
    Document& doc = *shown().doc;
    const bool on = !doc.persist_history();
    const std::string name = doc.is_untitled() ? std::string("the file") : sidecar_path_for(doc.path()).filename().string();
    if (auto s = doc.set_persist_history(on, overwrite); !s) {
        if (s.error().code == ErrorCode::format) {
            ask(std::format("Overwrite the unreadable history file {}?", name), "Overwrite", [this] { toggle_persist_history(true); });
        } else {
            set_status("history not changed: " + s.error().message);
        }
        return;
    }
    if (!on) {
        set_status("history kept in memory only");
    } else if (doc.is_untitled()) {
        set_status("history will be saved with the file");
    } else {
        set_status("history saved to " + name);
    }
    history_view_.set_persist(doc.persist_history());
}

void App::handle_colors_key(const KeyEvent& key) {
    const ColorsKeyResult r = colors_view_.handle_key(key);
    if (r.edit) {
        const std::string name = *r.edit;
        const bool modifier = std::ranges::any_of(color_names(), [&](const ColorEntry& e) { return e.name == name && e.modifier_bit != 0; });
        prompt_.open(PromptKind::color, std::format("{} color:", name), active_theme().spec_of(name),
                     [this, name, modifier](const std::string& text) -> std::optional<std::string> {
                         if (auto spec = parse_color_spec(text, modifier); !spec) return spec.error().message;
                         if (auto s = active_theme().set(name, text); !s) return s.error().message;
                         save_colors();
                         return std::nullopt;
                     });
    }
    if (r.reset) {
        active_theme().reset(*r.reset);
        save_colors();
    }
    if (r.reset_all) {
        ask("Reset every color to its default?", "Reset", [this] {
            active_theme().reset_all();
            set_status("all colors reset to their defaults");
            save_colors();
        });
    }
    if (r.closed) after_command();
}

// The colors are stored as the entries that differ from the defaults; with none, the
// member is removed from settings.json. Everything is redrawn in the new look.
void App::save_colors() {
    Json overrides = active_theme().overrides();
    const bool any = overrides.size() > 0;
    if (auto s = settings_.set_raw("colors", any ? std::optional<Json>(std::move(overrides)) : std::nullopt); !s)
        set_status("colors not remembered: " + s.error().message);
    screen_.invalidate();
}

// ---- commands ---------------------------------------------------------------------------

void App::run_command(CommandId id) {
    if (read_only() && command_edits(id)) {
        refuse_edit();
        return;
    }
    if (help_viewer_.is_open() && !stays_in_help(id)) leave_help();  // Open, Save, Close, Exit… act on the document
    sync_wrap();  // a motion must see the rows as they are drawn now
    const Layout l = layout();
    const auto page = static_cast<std::uint64_t>(std::max(1, l.text.rows - 1));
    bool extend = false;
    if (const auto m = motion_of(id, extend)) {
        if (const ReadingLayout* r = shown().view->reading_layout()) {
            // Laid out for reading: the cursor moves by what is shown.
            const std::uint64_t from = shown().editor->cursor();
            const std::uint64_t to = r->move(*m, from, std::max<std::size_t>(1, shown().view->reading_rows() - 1), shown().view->reading_sticky());
            shown().editor->select_range(extend ? shown().editor->anchor().value_or(from) : to, to);
            after_command();
            return;
        }
        shown().editor->move(*m, extend, page);
        after_command();
        return;
    }
    switch (id) {
        case CommandId::Open:
            open_file_dialog(FileDialogMode::open, [this](const fs::path& p) {
                if (auto s = open_new_document(p); !s) {
                    set_status(std::format("cannot open {}: {}", p.filename().string(), s.error().message));
                } else if (const std::string m = shown().doc->take_status_message(); !m.empty()) {
                    set_status(m);
                }
            });
            break;
        case CommandId::CloseDocument:
            if (ro().away) leave_read_only();  // the document itself is what closes
            dirty_check(std::format("Save changes to {} before closing it?", file_name()), [this] { close_shown(); });
            break;
        case CommandId::ShowDocument:
            if (const int i = menu_.chosen_arg(); i >= 0 && static_cast<std::size_t>(i) < ws_.documents().order().size())
                show_document(ws_.documents().order()[static_cast<std::size_t>(i)]);
            break;
        case CommandId::Save: save_document(std::nullopt, {}); break;
        case CommandId::SaveAs:
            open_file_dialog(FileDialogMode::save, [this](const fs::path& p) { save_document(p, {}); });
            break;
        case CommandId::ClearHistory:
            if (shown().doc->is_dirty()) {
                ask("Save changes before clearing history?", "Save", [this] { save_document(std::nullopt, [this] { confirm_clear_history(); }); });
            } else {
                confirm_clear_history();
            }
            break;
        case CommandId::TogglePersistHistory: toggle_persist_history(false); break;
        case CommandId::TrimHistory: {
            auto preview = shown().doc->prune_preview(std::nullopt);
            if (!preview) {
                set_status(preview.error().message);
            } else if (const auto oldest = preview->oldest_days) {
                prune_ask_age(*oldest);
            } else {
                set_status("no history to trim");
            }
            break;
        }
        case CommandId::Exit: request_exit(); break;
        case CommandId::Undo:
            if (auto s = shown().editor->undo(); !s) set_status(s.error().message);
            break;
        case CommandId::Redo:
            if (auto s = shown().editor->redo(); !s) set_status(s.error().message);
            break;
        case CommandId::UndoHistory:
            close_panels();
            history_view_.open(shown().doc->history());
            history_view_.set_persist(shown().doc->persist_history());
            report_preview(history_preview_.begin(shown(), history_view_.selected_node(), layout().text.rows, layout().text.cols));
            break;
        case CommandId::NextBranch:
        case CommandId::PrevBranch:
            if (const auto b = shown().doc->cycle_branch(id == CommandId::NextBranch ? 1 : -1)) {
                set_status(std::format("redo goes to branch {}/{}", b->index, b->count));
            } else {
                set_status("no other branch here");
            }
            break;
        case CommandId::Cut: shown().editor->cut(); break;
        case CommandId::CutToLineEnd:
            shown().editor->cut_to_line_end(kill_chain_prev_);  // consecutive presses collect into one clip
            kill_chain_ = true;
            break;
        case CommandId::Suspend:
            terminal_->suspend();  // returns once the job is continued
            screen_.invalidate();
            break;
        case CommandId::Copy: {
            const ReadingLayout* r = shown().view->reading_layout();
            const auto sel = shown().editor->selection();
            if (r && sel && settings_.read_only_copy() == ReadOnlyCopy::visible_text) {
                clipboard_.set_text(r->visible_text(sel->first, sel->second));  // the text as shown
                set_status("copied");
            } else if (shown().editor->copy()) {
                set_status("copied");
            }
            break;
        }
        case CommandId::Paste: shown().editor->paste(); break;
        case CommandId::Find:
            if (help_viewer_.is_open()) {
                open_doc_search();
            } else {
                open_find();
            }
            break;
        case CommandId::ShowHelp:
            if (help_viewer_.is_open()) {
                leave_help();
            } else {
                enter_help();
            }
            break;
        case CommandId::FindNext:
        case CommandId::FindPrev:
            if (!shown().searcher->has_query()) {
                open_find();
            } else {
                shown().searcher->find_next(shown().editor->cursor(), id == CommandId::FindNext ? Direction::forward : Direction::backward);
            }
            break;
        case CommandId::GotoLine: goto_line(); break;
        case CommandId::ToggleLineNumbers:
            shown().options->line_numbers = !shown().options->line_numbers;
            apply_options_to_views_of(shown().options.get());
            break;
        case CommandId::Split: split_view(); break;
        case CommandId::Unsplit: unsplit_view(); break;
        case CommandId::ToggleSyntax:
            shown().options->syntax = !shown().options->syntax;
            pick_highlighter();
            break;
        case CommandId::ToggleWordWrap:
            shown().options->word_wrap = !shown().options->word_wrap;
            apply_options_to_views_of(shown().options.get());
            break;
        case CommandId::PinFolderTree: pin_tree(!tree_pinned_); break;
        case CommandId::ToggleReadOnly:
            if (help_viewer_.is_open()) {
                set_status("the manual is always read-only");
            } else {
                toggle_read_only();
            }
            break;
        case CommandId::UserSettings:
            history_view_.close();
            close_panels();
            settings_view_.open(settings_);
            break;
        case CommandId::KeyBindings:
            history_view_.close();
            close_panels();
            keymap_view_.open(keymap_);
            break;
        case CommandId::Colors:
            history_view_.close();
            close_panels();
            colors_view_.open(active_theme());
            break;
        case CommandId::RecentSetting1:
        case CommandId::RecentSetting2:
        case CommandId::RecentSetting3:
        case CommandId::RecentSetting4:
        case CommandId::RecentSetting5:
            if (const auto index = recent_setting_index(id)) run_recent_setting(*index);
            break;
        case CommandId::About:
            prompt_.open_info("About mod",
                              "mod " MOD_VERSION ", a minimalist terminal text editor.\n"
                              "Unlimited branching undo; Persist History keeps it in <file>.history next to the file.\n"
                              "Edit > Undo History… shows every branch.\n"
                              "Menus: Esc (or F10, Alt+X), then a menu's underlined letter.\n"
                              "Options > Key Bindings… lists every command and its keys, and changes them.\n"
                              "macOS Terminal and iTerm2: enable \"Use Option as Meta key\" for Alt.\n"
                              "Shift and Ctrl with arrows need a terminal that sends xterm modifier sequences.");
            break;
        case CommandId::Newline: shown().editor->newline(); break;
        case CommandId::InsertTab: shown().editor->indent(settings_.tab_inserts() == TabInserts::spaces); break;
        case CommandId::Outdent: shown().editor->outdent(); break;
        case CommandId::DeleteBack: shown().editor->delete_backward(false); break;
        case CommandId::DeleteForward: shown().editor->delete_forward(false); break;
        case CommandId::DeleteWordBack: shown().editor->delete_backward(true); break;
        case CommandId::DeleteWordForward: shown().editor->delete_forward(true); break;
        case CommandId::ShowMenu:
            if (menu_.is_visible()) {
                menu_.hide();
            } else {
                menu_.show();
            }
            break;
        case CommandId::OpenMenuFile: menu_.open(MenuId::file); break;
        case CommandId::OpenMenuEdit: menu_.open(MenuId::edit); break;
        case CommandId::OpenMenuView: menu_.open(MenuId::view); break;
        case CommandId::OpenMenuDocuments: menu_.open(MenuId::documents); break;
        case CommandId::OpenMenuOptions: menu_.open(MenuId::options); break;
        case CommandId::OpenMenuHelp: menu_.open(MenuId::help); break;
        default: break;  // motions were handled above
    }
    after_command();
}

void App::sync_wrap() {
    if (!shown().view || !shown().editor) return;
    const Layout l = layout();
    if (shown().options->word_wrap && l.text.cols > 0) {
        shown().editor->set_wrap_width(shown().view->wrap_cols(l.text.cols));
    } else {
        shown().editor->set_wrap_width(std::nullopt);
    }
}

void App::after_command() {
    if (!shown().view) return;
    sync_wrap();
    const Layout l = layout();
    if (l.text.rows > 0 && l.text.cols > 0) shown().view->scroll_to_cursor(l.text.rows, l.text.cols);
}

// ---- input ------------------------------------------------------------------------------

void App::dispatch(const InputEvent& event) {
    // Ctrl+K appends only when the event before it was Ctrl+K too.
    kill_chain_prev_ = kill_chain_;
    kill_chain_ = false;
    if (const auto* key = std::get_if<KeyEvent>(&event)) {
        dispatch_key(*key);
        return;
    }
    const auto& paste = std::get<PasteEvent>(event);
    if (menu_.is_visible() || confirm_.is_open()) return;  // dropped
    if (file_dialog_) {
        file_dialog_->handle_paste(paste.bytes);
        return;
    }
    if (prompt_.is_open()) {
        prompt_.handle_event(event);
        return;
    }
    if (history_view_.is_open()) return;  // dropped
    switch (panel()) {
        case Panel::keymap: keymap_view_.handle_paste(paste.bytes); return;  // into its search field
        case Panel::doc_search: doc_search_.handle_paste(paste.bytes); return;
        case Panel::settings:
        case Panel::colors: return;  // dropped
        case Panel::none: break;
    }
    if (read_only()) {
        refuse_edit();
        return;
    }
    shown().editor->paste_text(paste.bytes, paste.more);
    after_command();
}

void App::dispatch_key(const KeyEvent& key) {
    // Esc three times in quick succession quits, whatever has the focus (asking about unsaved work).
    if (key.key == Key::Escape && key.mods == 0) {
        const auto now = Clock::now();
        // A press too long after the one before starts the count again.
        if (!escapes_.empty() && now - escapes_.back() > kQuitGap) escapes_.clear();
        escapes_.push_back(now);
        if (escapes_.size() >= kQuitEscapes) {
            escapes_.clear();
            quit_on_escapes();
            return;
        }
    }
    // A question takes every key until it is answered.
    if (confirm_.is_open()) {
        confirm_.handle_key(key);
        after_command();
        return;
    }
    if (file_dialog_) {
        handle_file_dialog_key(key);
        return;
    }
    if (preview_) {
        handle_preview_key(key);
        return;
    }
    if (tree_focus_) {
        handle_tree_key(key);
        return;
    }
    if (menu_.is_visible()) {
        if (menu_.flashing()) return;  // the chosen item runs when its flash ends
        // With the bar shown, Shift+Up and Shift+Down move the focus between split views.
        if ((key.key == Key::Up || key.key == Key::Down) && key.mods == kShift && ws_.size() > 1) {
            if (key.key == Key::Up && ws_.focus() > 0) focus_split(ws_.focus() - 1);
            if (key.key == Key::Down && ws_.focus() + 1 < ws_.size()) focus_split(ws_.focus() + 1);
            menu_.show();  // armed again, over the newly focused split
            return;
        }
        // Shift+Left gives the keys to the folder tree, showing it if it is hidden.
        if (key.key == Key::Left && key.mods == kShift) {
            menu_.hide();
            tree_focus_ = true;  // shown while it has the keys, pinned or not
            after_command();
            return;
        }
        // ShowMenu's own key hides the bar again; every other key is the menu's.
        if (keymap_.lookup(key) == CommandId::ShowMenu) {
            menu_.hide();
        } else if (const auto cmd = menu_.handle_key(key)) {
            if (menu_.flashing()) {
                flash_command_ = cmd;
                flash_deadline_ = Clock::now() + kMenuFlash;
            } else {
                run_command(*cmd);
            }
        }
        return;
    }
    if (prompt_.is_open()) {
        prompt_.handle_event(key);  // also a prompt opened from the history pane
        return;
    }
    if (history_view_.is_open()) {
        // Tab and Shift+Tab move the focus between the pane and the previewed text, which
        // the motions scroll while it has the focus.
        if ((key.key == Key::Tab || key.key == Key::BackTab) && (key.mods & (kAlt | kCtrl)) == 0) {
            history_preview_.toggle_focus();
            return;
        }
        if (history_preview_.text_focused() && history_preview_.scroll(shown(), key, layout().text.rows - 1)) return;
        const std::optional<NodeId> before = history_view_.selected_node();
        const HistoryKeyResult r = history_view_.handle_key(key);
        if (!r.message.empty()) set_status(r.message);
        if (r.command) {
            history_preview_.end(shown());
            run_command(*r.command);
            if (history_view_.is_open()) report_preview(history_preview_.begin(shown(), history_view_.selected_node(), layout().text.rows, layout().text.cols));  // the history may have changed
            return;
        }
        if (r.jump || r.closed) history_preview_.end(shown());
        if (!r.jump && !r.closed && history_view_.selected_node() != before) report_preview(history_preview_.show(shown(), history_view_.selected_node(), layout().text.rows, layout().text.cols));
        if (r.jump) {
            auto cursor = shown().doc->jump_to(*r.jump);
            if (cursor) {
                shown().editor->select_range(*cursor, *cursor);
                history_view_.close();
                set_status(std::format("jumped to {}", *r.jump));
            } else {
                set_status(cursor.error().message);
            }
        }
        if (r.jump || r.closed) after_command();
        return;
    }
    switch (panel()) {
        case Panel::settings: handle_settings_key(key); return;
        case Panel::keymap: handle_keymap_key(key); return;
        case Panel::colors: handle_colors_key(key); return;
        case Panel::doc_search: {
            const DocSearchKeyResult r = doc_search_.handle_key(key);
            if (r.open && help_viewer_.is_open()) {
                if (auto st = help_viewer_.show(r.open->page, r.open->line); !st) {
                    set_status(std::format("cannot open {}: {}", r.open->page.generic_string(), st.error().message));
                }
            }
            if (r.closed) after_command();
            return;
        }
        case Panel::none: break;
    }
    if (help_viewer_.is_open()) {
        const HelpKeyResult r = help_viewer_.handle_key(key);
        if (help_viewer_.used()) {
            if (!r.message.empty()) set_status(r.message);
            if (r.closed) leave_help();
            if (r.search) open_doc_search();
            return;
        }
        // Not the viewer's key: a command, which acts with the viewer open or closes it first.
    }
    // With nothing else open, Esc opens the menu.
    if (key.key == Key::Escape && key.mods == 0) {
        menu_.show();
        return;
    }
    if (read_only() && read_only_key(key)) return;
    if (const auto cmd = keymap_.lookup(key)) {
        run_command(*cmd);
        return;
    }
    if (key.key == Key::Char && (key.mods & (kAlt | kCtrl)) == 0) {
        if (read_only()) {
            refuse_edit();
            return;
        }
        shown().editor->insert_text(to_utf8(key.ch), EditKind::typing);
        after_command();
    }
}

void App::refuse_edit() { set_status("read-only: View > Read Only to edit"); }

// ---- help -------------------------------------------------------------------------------

bool App::stays_in_help(CommandId id) {
    switch (id) {
        case CommandId::Find:
        case CommandId::ShowHelp:
        case CommandId::About:
        case CommandId::Suspend:
        case CommandId::ToggleSyntax:
        case CommandId::ToggleReadOnly:
        case CommandId::PinFolderTree:
        case CommandId::UserSettings:
        case CommandId::KeyBindings:
        case CommandId::Colors:
        case CommandId::ShowMenu:
        case CommandId::OpenMenuFile:
        case CommandId::OpenMenuEdit:
        case CommandId::OpenMenuView:
        case CommandId::OpenMenuDocuments:
        case CommandId::OpenMenuOptions:
        case CommandId::OpenMenuHelp: return true;
        default: return false;
    }
}

// Opens the help viewer on the manual: where it was left this session, or its index. The
// shown document is left exactly as it is underneath.
void App::enter_help() {
    if (!help_dir_) help_dir_ = find_doc_dir(doc_dir_sources());
    if (!help_dir_) {
        std::string where;
        for (const fs::path& p : doc_dir_candidates(doc_dir_sources())) where += "\n  " + p.string();
        prompt_.open_info("Help", "The manual was not found. mod looked for index.md in:" + where +
                                      "\nSet MOD_DOC_DIR to the folder that holds it.");
        return;
    }
    if (auto s = help_viewer_.open(*help_dir_); !s) {
        set_status("cannot open the manual: " + s.error().message);
        return;
    }
    history_view_.close();
    screen_.invalidate();
    after_command();
}

// Closes the help viewer (and its search panel); the document shows again as it was.
void App::leave_help() {
    doc_search_.close();
    help_viewer_.close();
    screen_.invalidate();
    after_command();
}

// ---- read-only mode -----------------------------------------------------------------------

TrailEntry App::here() const {
    return TrailEntry{shown().doc->path(), shown().editor->cursor(), shown().view->top()};
}

bool App::shown_is_markdown() const { return !shown().doc->is_untitled() && is_markdown(shown().doc->path()); }

const MarkdownOutline& App::outline() {
    const Document* d = shown().doc.get();
    if (ro().outline_doc != d || ro().outline_version != d->version()) {
        ro().outline = shown_is_markdown() ? scan_markdown(d->text()) : MarkdownOutline{};
        ro().outline_doc = d;
        ro().outline_version = d->version();
    }
    return ro().outline;
}

void App::toggle_read_only() {
    if (!read_only()) {
        shown().options->read_only = true;
        ro().nav.reset(here());
        apply_options(shown());
        set_read_only_in_other_views(true);
        set_status(shown_is_markdown() ? "read-only view: Tab moves between links, Enter follows one" : "read-only view");
        return;
    }
    if (!ro().away) {
        leave_read_only();
        set_status("editing");
        return;
    }
    // Editing a link target opens it as a document of its own; this view steps back off it.
    const TrailEntry target = here();
    if (const auto back = ro().nav.back(target)) show_entry(*back);
    if (auto s = open_new_document(target.path); !s) {
        set_status("cannot open " + target.path.string() + ": " + s.error().message);
        return;
    }
    if (read_only()) leave_read_only();  // it was already open, in read-only mode
    shown().editor->select_range(target.cursor, target.cursor);
    shown().view->set_top(target.top);
    set_status("editing " + file_name());
    after_command();
}

// Back to plain editing of the document itself; a shown link target is dropped.
void App::leave_read_only() {
    if (ro().away) {
        prompt_.set_searcher(nullptr);
        shown() = std::move(ro().base);  // drops the target
        prompt_.set_searcher(shown().searcher.get());
        ro().away = false;
    }
    if (shown().options) shown().options->read_only = false;
    set_read_only_in_other_views(false);
    ro().outline_doc = nullptr;
    ro().nav.reset(TrailEntry{});
    apply_options(shown());
}

bool App::read_only_key(const KeyEvent& key) {
    const bool markdown = shown_is_markdown();
    const auto plain = [&](Key k) { return key.key == k && (key.mods & (kAlt | kCtrl | kShift)) == 0; };
    if (key.key == Key::Left && key.mods == kCtrl) {
        if (const auto e = ro().nav.back(here())) {
            show_entry(*e);
        } else {
            set_status("no earlier page");
        }
        return true;
    }
    if (key.key == Key::Right && key.mods == kCtrl) {
        if (const auto e = ro().nav.forward(here())) {
            show_entry(*e);
        } else {
            set_status("no later page");
        }
        return true;
    }
    if (!markdown) return false;  // Tab, Enter and Space then fall through and are refused
    if (plain(Key::Tab) || key.key == Key::BackTab) {
        const MarkdownOutline& o = outline();
        // From inside a link, both directions start at that link.
        std::uint64_t pos = shown().editor->cursor();
        if (const auto at = ReadOnlyNav::link_at(o, pos)) pos = at->start;
        const auto link = ReadOnlyNav::next_link(o, pos, key.key == Key::BackTab ? -1 : 1);
        if (!link) {
            set_status("no links in " + file_name());
            return true;
        }
        shown().editor->select_range(link->text_start, link->text_end);
        set_status(link->target);
        after_command();
        return true;
    }
    if (plain(Key::Enter) || (key.key == Key::Char && key.ch == U' ' && key.mods == 0)) {
        follow_link();
        return true;
    }
    return false;
}

void App::follow_link() {
    const MarkdownOutline& o = outline();
    const auto sel = shown().editor->selection();
    const auto link = ReadOnlyNav::link_at(o, sel ? sel->first : shown().editor->cursor());
    if (!link) {
        set_status("no link here: Tab moves to the next one");
        return;
    }
    const LinkAction a = ReadOnlyNav::resolve(*link, shown().doc->path(), o);
    switch (a.kind) {
        case LinkAction::none: break;
        case LinkAction::message: set_status(a.text); break;
        case LinkAction::jump:
            shown().editor->select_range(a.offset, a.offset);
            after_command();
            break;
        case LinkAction::open: {
            std::error_code ec;
            if (!fs::is_regular_file(a.path, ec)) {
                set_status("not found: " + a.path.string());
                break;
            }
            const TrailEntry from = here();
            if (show_entry(TrailEntry{a.path, 0, 0}, a.anchor)) ro().nav.visit(from, here());
            break;
        }
    }
}

// Shows a place from the trail: the document itself, or a link target opened for viewing
// (no history is read or written for it), at the entry's position or at `anchor`.
bool App::show_entry(const TrailEntry& entry, const std::string& anchor) {
    const fs::path& base_path = ro().away ? ro().base.doc->path() : shown().doc->path();
    const fs::path want = resolve_real_path(entry.path).value_or(entry.path);  // as Document names its file
    if (!ro().away && shown().doc->path() == want) {
        // Already shown: only the position changes.
    } else if (want == base_path) {
        prompt_.set_searcher(nullptr);
        shown() = std::move(ro().base);
        prompt_.set_searcher(shown().searcher.get());
        ro().away = false;
    } else if (!(ro().away && shown().doc->path() == want)) {
        DocumentOptions options = document_options();
        options.history = false;
        auto opened = Document::open(want, queue_, std::move(options));
        if (!opened) {
            set_status("cannot open " + want.string() + ": " + opened.error().message);
            return false;
        }
        history_view_.close();
        if (!ro().away) {
            ro().base = std::move(shown());
            ro().away = true;
        }
        install_document(std::move(*opened), ro().base.options);  // a link target follows its document's options
    }
    apply_options(shown());  // read-only, as the document it was followed from
    std::uint64_t cursor = entry.cursor;
    std::uint64_t top = entry.top;
    if (!anchor.empty()) {
        if (const auto at = ReadOnlyNav::find_anchor(outline(), anchor)) {
            cursor = top = *at;
        } else {
            set_status("no heading #" + anchor + " in " + file_name());
        }
    }
    shown().editor->select_range(cursor, cursor);
    shown().view->set_top(top);
    if (status_.empty() || anchor.empty()) set_status(file_name());
    after_command();
    return true;
}

// ---- output -----------------------------------------------------------------------------

App::Layout App::layout() const {
    Layout l;
    const int rows = screen_.rows();
    const int cols = screen_.cols();
    // The menu bar, while shown, is the screen's top line. The band of interactions at the
    // screen's bottom holds, from the bottom up, a prompt and a question. The info overlay
    // is drawn over what is above the band instead, so closing it never scrolls.
    const int top_band = menu_.is_visible() ? 1 : 0;
    if (top_band > 0) l.menu_row = 0;
    const bool prompt = prompt_.is_open() && !menu_.is_visible();
    const bool info = prompt && prompt_.kind() == PromptKind::info;
    int band = 0;
    if (prompt && !info) {
        const int n = std::min(prompt_.rows_wanted(rows, cols), std::max(0, rows - 2 - band - top_band));
        band += n;
        l.prompt = Rect{rows - band, 0, n, cols};
    }
    if (confirm_.is_open()) {
        const int n = std::min(confirm_.rows(cols), std::max(0, rows - 2 - band - top_band));
        band += n;
        l.confirm = Rect{rows - band, 0, n, cols};
    }
    if (info) {
        const int n = std::min(prompt_.rows_wanted(rows, cols), std::max(0, rows - band - top_band));
        l.prompt = Rect{rows - band - n, 0, n, cols};
    }
    l.band = band + top_band;
    // The focused split's rows: all between the bands in single-view mode; else its share, the
    // bottom band's rows taken from the bottom split (then the next one up) and the top band's
    // from the top split (then the next one down), each keeping its status row.
    int top = top_band;
    int height = rows - band - top_band;
    if (!single_view()) {
        auto shares = split_rows(rows, static_cast<int>(ws_.size()));
        int take = band;
        for (std::size_t i = shares.size(); i-- > 0 && take > 0;) {
            const int give = std::min(take, shares[i].second - 1);
            shares[i].second -= give;
            take -= give;
        }
        take = top_band;
        for (std::size_t i = 0; i < shares.size() && take > 0; ++i) {
            const int give = std::min(take, shares[i].second - 1);
            shares[i].second -= give;
            take -= give;
        }
        int t = top_band;
        for (std::size_t i = 0; i < ws_.size(); ++i) {
            const int h = shares[i].second;
            if (i == ws_.focus()) {
                top = t;
                height = h;
            } else {
                l.others.push_back({i, Rect{t, 0, std::max(0, h - 1), cols}, t + h - 1});
            }
            t += h;
        }
    }
    l.status_row = top + height - 1;
    // A full-screen UI's own line takes the place of the document's status line.
    l.own_line = history_view_.is_open() || file_dialog_.has_value() || panel_open() || help_viewer_.is_open();
    // The folder tree, on the left between the bands, and a divider, while it is pinned or has
    // the keys; a full-screen UI hides it.
    if ((tree_pinned_ || tree_focus_) && !l.own_line) {
        const int width = std::min(kTreeWidth, cols / 3);
        if (width >= 4) {
            l.tree = Rect{top_band, 0, rows - band - top_band, width};
            l.left = width + 1;
            for (Layout::Other& o : l.others) {
                o.text.col = l.left;
                o.text.cols = std::max(0, cols - l.left);
            }
        }
    }
    const int view_cols = cols - l.left;
    const int body_rows = std::max(0, height - 1);
    if (panel_open()) {
        // A full-width panel takes the whole body; the text is not drawn.
        l.panel_framed = body_rows >= 3 && cols >= 3;
        l.panel = l.panel_framed ? Rect{top + 1, 1, body_rows - 2, cols - 2} : Rect{top, 0, body_rows, cols};
    } else if (file_dialog_) {
        l.dialog = Rect{top, 0, body_rows, cols};  // the file dialog covers the text
    } else if (help_viewer_.is_open()) {
        l.help = Rect{top, 0, body_rows, cols};  // the viewer covers the text, which stays as it was
    } else if (history_view_.is_open() && body_rows >= 3) {
        const PaneWidth pw = history_pane_width(cols);
        l.pane = Rect{top + 1, 1, body_rows - 2, pw.width};
        if (!pw.full_width) l.text = Rect{top + 1, pw.width + 2, body_rows - 2, cols - pw.width - 3};
    } else {
        l.text = Rect{top, l.left, body_rows, view_cols};
    }
    return l;
}

// The terminal's title (its tab's, in most terminal apps): "mod:" and the focused view's own
// document (not a link it is following), with " *" while it has unsaved changes. Sent only
// when it changes; a VT100 gets none.
void App::update_title() {
    const Document* doc = ro().away ? ro().base.doc.get() : shown().doc.get();
    if (doc == nullptr) return;
    std::string title = "mod:" + (doc->is_untitled() ? std::string("[untitled]") : doc->path().filename().string());
    if (doc->is_dirty()) title += " *";
    if (title == title_) return;
    std::string out;
    output_for(terminal_mode_).append_title(out, title);
    if (!out.empty()) (void)terminal_->write(std::as_bytes(std::span(out.data(), out.size())));
    title_ = std::move(title);
}

void App::render() {
    // A preview never outlives the pane, nor the document's own preview.
    if (shown().view && shown().view->previewing() && (!history_view_.is_open() || !shown().doc->previewing())) history_preview_.end(shown());
    const Layout l = layout();
    // The other splits: their text and their own status line, with no message. While an
    // interaction at the bottom is open, which is for the focused split, their text dims.
    for (const Layout::Other& o : l.others) {
        DocumentSlot& s = ws_.at(o.index).slot;
        if (!s.view) continue;
        s.view->set_split_focused(false);
        if (o.text.rows > 0) s.view->render(screen_, o.text, s.searcher->last_match(), false);
        if (l.band > 0) screen_.add_flags(o.text, kDim);
        s.view->render_status(screen_, o.status_row, "", StatusMark::unfocused, l.left);
    }
    if (l.tree) {
        tree_view_.render(screen_, *l.tree, tree_focus_ && !preview_);
        for (int r = l.tree->row; r < l.tree->row + l.tree->rows; ++r) screen_.put(r, l.tree->cols, "│", 1, attr_for(Style::gutter));
    }
    if (l.pane) {
        const int split = l.pane->col + l.pane->cols;
        draw_frame(l.pane->row - 1, l.pane->row + l.pane->rows, l.text.cols > 0 ? std::optional<int>(split) : std::nullopt, {});
        history_view_.render(screen_, *l.pane, !history_preview_.text_focused());
    }
    if (l.panel) {
        const Panel p = panel();
        if (l.panel_framed) {
            const std::string_view title = p == Panel::doc_search ? "Search Help" : p == Panel::keymap ? "Key Bindings" : p == Panel::colors ? "Colors" : "User Settings";
            draw_frame(l.panel->row - 1, l.panel->row + l.panel->rows, std::nullopt, title);
        }
        switch (p) {
            case Panel::doc_search: doc_search_.render(screen_, *l.panel); break;
            case Panel::colors: colors_view_.render(screen_, *l.panel); break;
            case Panel::keymap: keymap_view_.render(screen_, *l.panel); break;  // it places the cursor in its search field
            case Panel::settings:
            case Panel::none: settings_view_.render(screen_, *l.panel); break;
        }
    }
    const bool text_focus = !menu_.is_visible() && !history_view_.is_open() && !prompt_.is_open() && !confirm_.is_open() && !file_dialog_ && !panel_open() &&
                            !tree_focus_ && !preview_;
    screen_.set_cursor(0, 0, false);
    if (l.help) help_viewer_.render(screen_, *l.help);
    if (l.dialog && file_dialog_) file_dialog_->render(screen_, *l.dialog);
    if (preview_) {
        if (l.text.rows > 0 && l.text.cols > 0) preview_->view->render(screen_, l.text, std::nullopt, false);
    } else if (l.text.rows > 0 && l.text.cols > 0) {
        shown().view->set_split_focused(true);
        shown().view->render(screen_, l.text, shown().searcher->last_match(), text_focus);
    }
    if (prompt_.is_open() && !menu_.is_visible()) prompt_.render(screen_, l.prompt);
    if (l.confirm) confirm_.render(screen_, *l.confirm);
    std::string_view message = status_;
    if (message.empty() && history_view_.is_open()) message = kHistoryHints;
    if (message.empty()) {
        switch (panel()) {
            case Panel::settings: message = kSettingsHints; break;
            case Panel::keymap: message = kKeymapHints; break;
            case Panel::colors: message = kColorsHints; break;
            case Panel::doc_search: message = kDocSearchHints; break;
            case Panel::none: break;
        }
    }
    if (message.empty() && help_viewer_.is_open()) message = kHelpHints;
    if (message.empty() && file_dialog_) message = kFileDialogHints;
    const std::string hint = help_hint(keymap_);
    if (message.empty()) message = hint;
    if (help_viewer_.is_open()) {
        draw_status_line(screen_, l.status_row, " Help: " + help_viewer_.page().generic_string(), message, " ");
    } else if (l.own_line) {
        draw_status_line(screen_, l.status_row, " ", message, " ");
    } else if (preview_) {
        draw_status_line(screen_, l.status_row, " Preview: " + preview_->doc->path().filename().string(), "Esc: close", " ", false, l.left);
    } else {
        // While the tree has the keys the view keeps its status line, without the '>'.
        shown().view->render_status(screen_, l.status_row, message, tree_focus_ ? StatusMark::none : StatusMark::focused, l.left);
    }
    menu_.set_documents(document_entries());
    menu_.render(screen_, l.menu_row.value_or(-1), [this](CommandId id) {
        switch (id) {
            case CommandId::ToggleLineNumbers: return shown().options && shown().options->line_numbers;
            case CommandId::ToggleSyntax: return shown().options && shown().options->syntax;
            case CommandId::ToggleWordWrap: return shown().options && shown().options->word_wrap;
            case CommandId::ToggleReadOnly: return read_only();
            case CommandId::PinFolderTree: return tree_pinned_;
            default: break;
        }
        if (const auto index = recent_setting_index(id)) {
            const auto recent = settings_.recent(kRecentSettingCount);
            return *index < recent.size() && settings_.flag(recent[*index]->key);
        }
        return false;
    });
    if (menu_.is_visible() || confirm_.is_open() || ((history_view_.is_open() || settings_view_.is_open()) && !prompt_.is_open())) screen_.set_cursor(0, 0, false);
    if (auto s = screen_.flush(); !s) log(LogLevel::warn, "render: {}", s.error().message);
    update_title();
}

// A box of single box-drawing lines from row `top` to row `bottom` across the full width,
// with an optional column divider and a title in its top edge.
void App::draw_frame(int top, int bottom, std::optional<int> split, std::string_view title) {
    const int right = screen_.cols() - 1;
    const Attr frame = attr_for(Style::gutter);
    for (int c = 0; c <= right; ++c) {
        const bool divider = split && c == *split;
        screen_.put(top, c, c == 0 ? "┌" : c == right ? "┐" : divider ? "┬" : "─", 1, frame);
        screen_.put(bottom, c, c == 0 ? "└" : c == right ? "┘" : divider ? "┴" : "─", 1, frame);
    }
    for (int r = top + 1; r < bottom; ++r) {
        screen_.put(r, 0, "│", 1, frame);
        screen_.put(r, right, "│", 1, frame);
        if (split) screen_.put(r, *split, "│", 1, frame);
    }
    if (!title.empty()) screen_.print(top, 3, right - 1, std::format(" {} ", title), attr_for(Style::gutter_current));
}

void App::report_progress(const Progress& p) {
    try {
        const auto now = Clock::now();
        if (now - last_progress_draw_ < kProgressRedraw) return;
        last_progress_draw_ = now;
        if (screen_.rows() <= 0) return;
        const int row = screen_.rows() - 1;
        const Attr bar = attr_for(Style::status);
        screen_.fill(row, 0, screen_.cols(), bar);
        screen_.print(row, 1, screen_.cols(), format_progress(p), bar);
        (void)screen_.flush();
    } catch (...) {
        // The meter must never break the operation it reports on.
        log(LogLevel::debug, "progress meter failed to draw");
    }
}

// The status for a node the history pane could not preview.
void App::report_preview(const Status& s) {
    if (!s) set_status("cannot preview: " + s.error().message);
}

void App::ask(std::string question, std::string yes, std::function<void()> on_yes, std::string no) {
    confirm_.open(std::move(question), {std::move(yes), std::move(no)}, 1, [on_yes = std::move(on_yes)](int choice) {
        if (choice == 0) on_yes();
    });
}

// ---- the folder tree -------------------------------------------------------------------------

// Pinned, the tree stays beside the views; unpinned, it shows only while it has the keys.
void App::pin_tree(bool on) {
    tree_pinned_ = on;
    screen_.invalidate();
}

void App::handle_tree_key(const KeyEvent& key) {
    // ShowMenu's key leaves the tree for escape mode.
    if (keymap_.lookup(key) == CommandId::ShowMenu) {
        tree_focus_ = false;
        menu_.show();
        return;
    }
    const TreeKeyResult r = tree_view_.handle_key(key);
    if (r.kind == TreeKey::none) {
        // A key the tree does not use runs its command, as in the help: the keys leave the tree.
        bool extend = false;
        if (const auto id = keymap_.lookup(key); id && !motion_of(*id, extend)) {
            tree_focus_ = false;
            run_command(*id);
            return;
        }
    }
    switch (r.kind) {
        case TreeKey::open:
            if (auto s = open_new_document(r.path); !s) {
                tree_view_.set_message("cannot open " + r.path.filename().string() + ": " + s.error().message);
            } else {
                tree_focus_ = false;  // the keys go back to the view, now showing the file
            }
            break;
        case TreeKey::preview: open_preview(r.path); break;
        case TreeKey::back:
            tree_focus_ = false;
            menu_.show();  // back in escape mode, over the last focused view
            break;
        case TreeKey::leave: tree_focus_ = false; break;
        case TreeKey::moved:
        case TreeKey::none: break;
    }
    after_command();
}

// A file from the tree, read-only and without history, shown as the only view: Markdown laid
// out for reading as the help is, anything else as colored text.
void App::open_preview(const fs::path& path) {
    DocumentOptions o = document_options();
    o.history = false;
    auto doc = Document::open(path, queue_, o);
    if (!doc) {
        tree_view_.set_message("cannot preview " + path.filename().string() + ": " + doc.error().message);
        return;
    }
    close_preview();
    DocumentSlot s;
    s.doc = std::move(*doc);
    s.editor = std::make_unique<Editor>(*s.doc, clipboard_, settings_.tab_width());
    s.searcher = std::make_unique<Searcher>(*s.doc, *s.editor);
    s.view = std::make_unique<EditorView>(*s.doc, *s.editor, nullptr, settings_.tab_width());
    s.options = std::make_shared<ViewOptions>(options_on_open());
    s.options->read_only = true;
    apply_options(s);
    pick_highlighter(s);
    preview_ = std::move(s);
    screen_.invalidate();
}

void App::close_preview() {
    if (!preview_) return;
    preview_->release_highlighter();
    preview_.reset();
    screen_.invalidate();
}

// In the preview, the motions move through the file; Esc closes it and the tree has the keys again.
void App::handle_preview_key(const KeyEvent& key) {
    if (!preview_) return;
    if (key.key == Key::Escape && key.mods == 0) {
        close_preview();
        return;
    }
    bool extend = false;
    const auto id = keymap_.lookup(key);
    const auto m = id ? motion_of(*id, extend) : std::nullopt;
    if (!m) return;
    const Layout l = layout();
    DocumentSlot& p = *preview_;
    if (const ReadingLayout* r = p.view->reading_layout()) {
        const std::uint64_t to = r->move(*m, p.editor->cursor(), std::max<std::size_t>(1, p.view->reading_rows() - 1), p.view->reading_sticky());
        p.editor->select_range(to, to);
    } else {
        p.editor->move(*m, false, static_cast<std::uint64_t>(std::max(1, l.text.rows - 1)));
    }
    p.view->scroll_to_cursor(l.text.rows, l.text.cols);
}

void App::set_status(std::string message) {
    if (message.empty()) return;
    log(LogLevel::info, "status: {}", message);
    status_ = std::move(message);
    status_deadline_ = Clock::now() + kStatusTimeout;
}

void App::poll_status() {
    if (shown().doc) {
        if (std::string m = shown().doc->take_status_message(); !m.empty()) set_status(std::move(m));
    }
    if (status_deadline_ && Clock::now() >= *status_deadline_) {
        status_.clear();
        status_deadline_.reset();
    }
}

void App::step_search() {
    if (!shown().searcher || !shown().searcher->active()) return;
    const StepResult r = shown().searcher->step();
    switch (r.kind) {
        case StepResult::found:
            if (r.wrapped) set_status("search wrapped");
            if (r.line_too_long) set_status("a very long line was searched only in part");
            after_command();
            break;
        case StepResult::not_found: set_status(r.message.empty() ? std::string("not found") : r.message); break;
        case StepResult::replaced:
            set_status(std::format("replaced {}{}", r.count, r.message.empty() ? std::string() : " (" + r.message + ")"));
            after_command();
            break;
        default: break;
    }
}

int App::wait_timeout_ms() {
    using std::chrono::milliseconds;
    const auto now = Clock::now();
    std::optional<milliseconds> t;
    auto consider = [&](milliseconds m) { t = t ? std::min(*t, m) : m; };
    auto until = [&](Clock::time_point tp) {
        consider(tp <= now ? milliseconds(0) : std::chrono::ceil<milliseconds>(tp - now));
    };
    if (const auto pending = decoder_.pending_timeout()) until(last_input_ + *pending);
    if (const auto d = shown().highlighter_deadline) until(*d);
    for (std::size_t i = 0; i < ws_.size(); ++i) {
        if (const auto d = ws_.at(i).slot.highlighter_deadline; d && i != ws_.focus()) until(*d);
    }
    if (!shown().doc->is_untitled()) until(next_external_check_);
    if (status_deadline_) until(*status_deadline_);
    if (flash_deadline_) until(*flash_deadline_);
    if (shown().searcher->active()) consider(milliseconds(0));
    if (prune_offer_pending_) consider(kPruneOfferRetry);  // retried until verification resolves
    if (!t) return -1;
    return static_cast<int>(std::min<std::int64_t>(t->count(), 60'000));
}

// The terminal mode for this session: the setting's, else what the environment says,
// else the terminal's answer to a device attributes query, else xterm. Bytes read while
// waiting that are not the answer are returned in `typed`, to be decoded as input.
TerminalMode App::resolve_terminal_mode(std::string& typed) {
    if (const auto chosen = settings_.terminal_mode()) return *chosen;
    if (const auto m = mode_from_environment([](const char* name) { return std::getenv(name); })) return *m;
    (void)terminal_->write(std::as_bytes(std::span(kDeviceAttributesQuery.data(), kDeviceAttributesQuery.size())));
    const auto deadline = Clock::now() + kDeviceAttributesWait;
    std::array<std::byte, 256> buf{};
    while (!find_device_attributes(typed)) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) break;
        if ((terminal_->wait(static_cast<int>(left)) & input_ready) == 0) continue;
        const auto n = terminal_->read_input(buf);
        if (!n || *n == 0) continue;
        typed.append(reinterpret_cast<const char*>(buf.data()), *n);
    }
    const auto range = find_device_attributes(typed);
    const auto mode = mode_from_device_attributes(typed);
    if (range) typed.erase(range->first, range->second - range->first);
    return mode.value_or(TerminalMode::xterm);
}

// One wake of the event loop: input, the decoder's timeout, posted tasks, the search, a
// resize, the highlighters' ticks, and the checks that wait for the focus to be free.
void App::handle_events(WaitEvents ev, std::span<std::byte> buf) {
    if (ev & input_ready) {
        for (;;) {
            auto n = terminal_->read_input(buf);
            if (!n || *n == 0) break;
            last_input_ = Clock::now();
            for (const InputEvent& e : decoder_.feed(std::span(buf.data(), *n))) {
                dispatch(e);
                if (quit_) break;
            }
            if (quit_ || *n < buf.size()) break;
        }
    }
    // Only once the decoder's wait has really passed since the last input: the wait may
    // also end for another deadline, and ending a paste early would cut it in two.
    if (const auto pending = decoder_.pending_timeout(); !quit_ && pending && Clock::now() - last_input_ >= *pending) {
        for (const InputEvent& e : decoder_.timeout()) dispatch(e);
    }
    queue_.drain();
    step_search();
    if (flash_deadline_ && Clock::now() >= *flash_deadline_) run_flashed_command();
    if (ev & resized) {
        title_.clear();  // after a suspended job continues (a resize), the title is set again
        screen_.resize(terminal_->size());
        trim_splits();
        after_command();
    }
    // Ticked every iteration: an edit can start a new debounce at any time.
    if (shown().highlighter) shown().highlighter_deadline = shown().highlighter->tick(Clock::now());
    for (std::size_t i = 0; i < ws_.size(); ++i) {
        DocumentSlot& s = ws_.at(i).slot;
        if (i != ws_.focus() && s.highlighter && s.highlighter != shown().highlighter) s.highlighter_deadline = s.highlighter->tick(Clock::now());
    }
    const bool focus_free = !menu_.is_visible() && !prompt_.is_open() && !confirm_.is_open() && !file_dialog_ && !history_view_.is_open() && !panel_open();
    if (prune_offer_pending_ && focus_free) offer_prune();
    if (!shown().doc->is_untitled() && Clock::now() >= next_external_check_ && focus_free && !external_prompt_open_) {
        next_external_check_ = Clock::now() + kExternalCheckInterval;
        handle_external_change();
    }
}

int App::run() {
    if (auto s = terminal_->enter_raw_mode(); !s) {
        log(LogLevel::error, "enter_raw_mode: {}", s.error().message);
        return 1;
    }
    std::string typed;
    terminal_mode_ = resolve_terminal_mode(typed);
    const TerminalOutput& output = output_for(terminal_mode_);
    terminal_->start_screen(output);
    screen_.set_output(output);
    active_theme().set_vt100(terminal_mode_ == TerminalMode::vt100);
    screen_.resize(terminal_->size());
    after_command();
    for (const InputEvent& e : decoder_.feed(std::as_bytes(std::span(typed.data(), typed.size())))) dispatch(e);
    std::array<std::byte, 4096> buf{};
    // A step that throws is reported and the loop goes on: the documents, and the chance to
    // save them, outlive one failed command.
    auto guarded = [this](auto&& step) {
        try {
            step();
        } catch (const std::exception& e) {
            log(LogLevel::error, "uncaught in event loop: {}", e.what());
            set_status(std::string("internal error: ") + e.what());
        }
    };
    while (!quit_) {
        guarded([this] {
            poll_status();
            render();
        });
        // Outside the guards, so a step that keeps failing still waits for the next event.
        const WaitEvents ev = terminal_->wait(wait_timeout_ms());
        if (ev & terminated) {
            // Asked to end (or the terminal is gone): no questions, but in order, so every
            // document's history reaches its sidecar.
            quit_ = true;
            break;
        }
        guarded([&] { handle_events(ev, buf); });
    }
    return 0;
}

}  // namespace mod
