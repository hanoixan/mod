#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <unordered_set>
#include <vector>

#include "edit/sidecar.hpp"
#include "edit/undo_tree.hpp"
#include "platform/file_map.hpp"
#include "text/line_scanner.hpp"
#include "text/piece_tree.hpp"
#include "util/error.hpp"
#include "util/event_queue.hpp"
#include "util/hash.hpp"
#include "util/progress.hpp"

namespace mod {

class Clipboard;

struct ChangeEvent {
    std::uint64_t offset = 0;
    std::uint64_t removed_len = 0;
    std::uint64_t inserted_len = 0;
    std::optional<std::string_view> inserted_small;  // present when at most 64 KiB
    std::uint64_t version = 0;
};

class DocumentListener {
public:
    virtual ~DocumentListener() = default;
    virtual void before_change(const ChangeEvent&) {}
    virtual void after_change(const ChangeEvent&) {}
    virtual void reloaded() {}
    virtual void saved() {}
};

enum class HistoryState { session_only, verifying, attached, read_only, disabled };

enum class SaveMode { atomic, in_place };

struct ExternalChange {
    enum Kind { unchanged, modified, deleted } kind = unchanged;
    bool replaced = false;  // for `modified`: device or inode differ
};

// When the history is written to the sidecar: `if_present` (the default) only when a
// loadable sidecar is there at open, until set_persist_history turns it on; `always` from the start.
enum class PersistHistory { if_present, always };

struct DocumentOptions {
    std::function<std::int64_t()> now_ms;  // Unix milliseconds; the system clock when empty
    std::uint64_t chunk_size = PieceTree::kDefaultChunkSize;
    ProgressSink progress;  // long synchronous operations report here; also the sidecar's
    bool history = true;    // false: no sidecar is read or written; the history lasts the session
    PersistHistory persist_history = PersistHistory::if_present;
};

// Inserted content: bytes, a run of this document's buffers, or a sidecar payload.
using InsertContent = std::variant<std::string_view, PieceRun, SidecarRef>;

// A preview's mark of the selected step's change: text it inserted, or text it removed
// (spliced back in so it can be shown), in offsets of the previewed text.
struct PreviewMark {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    bool removed = false;
    friend bool operator==(const PreviewMark&, const PreviewMark&) = default;
};

// What a prune would remove, for the prune prompts.
struct PrunePreview {
    std::optional<std::uint64_t> oldest_days;  // age of the oldest change; nullopt without changes
    std::int64_t cutoff_ms = 0;
    std::uint64_t remove_count = 0;
    std::uint64_t keep_count = 0;
    std::uint64_t removed_trees = 0;
};

inline constexpr std::int64_t kCoalesceTimeoutMs = 1000;
inline constexpr std::int64_t kDayMs = 86'400'000;

// One open file: its piece tree, undo history, sidecar, background scan and save
// logic. Every content mutation goes through here. Main thread only.
class Document {
public:
    static Result<std::unique_ptr<Document>> open(const std::filesystem::path& path, EventQueue& queue,
                                                  DocumentOptions options = {});
    static std::unique_ptr<Document> open_untitled(EventQueue& queue, DocumentOptions options = {});

    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    void apply(std::uint64_t offset, std::uint64_t remove_len, const InsertContent& insert, EditKind kind,
               std::uint64_t cursor_before, std::uint64_t cursor_after);
    void begin_group(EditKind kind);
    void end_group();

    class GroupGuard {
    public:
        GroupGuard(Document& doc, EditKind kind) : doc_(doc) { doc_.begin_group(kind); }
        ~GroupGuard() { doc_.end_group(); }
        GroupGuard(const GroupGuard&) = delete;
        GroupGuard& operator=(const GroupGuard&) = delete;

    private:
        Document& doc_;
    };

    Result<std::uint64_t> undo();
    Result<std::uint64_t> redo();
    std::optional<BranchIndicator> cycle_branch(int direction);
    Result<std::uint64_t> jump_to(NodeId target);
    Status clear_history();
    Result<PrunePreview> prune_preview(std::optional<std::uint64_t> days);
    Status prune_history(std::int64_t cutoff_ms);

    Status save(SaveMode mode = SaveMode::atomic, Clipboard* clipboard = nullptr);
    Status save_as(const std::filesystem::path& path, SaveMode mode = SaveMode::atomic, Clipboard* clipboard = nullptr);

    ExternalChange check_external_change() const;
    Status reload();
    void keep_in_memory();

    bool is_dirty() const;
    const PieceTree& text() const noexcept { return tree_; }
    // Line queries; non-const only because `force` (and the scan of an unscanned piece)
    // caches line counts in the piece tree. They never change the content.
    std::optional<std::uint64_t> line_of(std::uint64_t offset, bool force) { return tree_.line_of(offset, force); }
    std::optional<std::uint64_t> line_start(std::uint64_t line, bool force) { return tree_.line_start(line, force); }

    void add_listener(DocumentListener* listener);
    void remove_listener(DocumentListener* listener);

    const UndoTree& history() const noexcept { return undo_; }
    HistoryState history_state() const;
    // Whether the history is written to the sidecar file (Persist History).
    bool persist_history() const noexcept { return persist_; }
    // A sidecar file was there but could not be loaded.
    bool history_unreadable() const noexcept { return unreadable_; }
    // Turns persistence on or off. `format`: an unreadable sidecar is in the way and
    // `overwrite` was not given; `unsupported`: the history is read-only here.
    Status set_persist_history(bool on, bool overwrite = false);

    // Shows the text as it is at `node`, silently and without changing the history, with the
    // node's own change marked (its removed text spliced back in), until end_preview walks
    // it back exactly. Nothing else may change the document meanwhile.
    Result<std::vector<PreviewMark>> begin_preview(NodeId node);
    void end_preview();
    bool previewing() const noexcept { return previewing_; }
    std::uint64_t version() const noexcept { return version_; }
    const std::filesystem::path& path() const noexcept { return path_; }
    bool is_untitled() const noexcept { return path_.empty(); }
    bool read_only_file() const noexcept { return read_only_file_; }
    std::string_view line_ending() const noexcept { return crlf_ ? std::string_view("\r\n") : std::string_view("\n"); }
    std::uint64_t id() const noexcept { return id_; }
    std::int64_t history_load_ms() const noexcept { return history_load_ms_; }
    std::string take_status_message();

private:
    struct Resolved;  // a payload ready to be inserted into the tree
    struct Written {
        std::shared_ptr<const MappedFile> mapping;
        std::vector<ChunkLines> chunks;
        ContentHash hash{};
    };

    Document(EventQueue& queue, DocumentOptions options, std::shared_ptr<const MappedFile> original);

    std::int64_t now() const;
    void notify_before(const ChangeEvent& ev);
    void notify_after(const ChangeEvent& ev);
    void mutate(std::uint64_t offset, std::uint64_t remove_len, const Resolved& insert);
    Result<Resolved> resolve(const Payload& payload);
    Status apply_node(NodeId node, StepDirection direction);
    void record(EditOp op, EditKind kind, std::uint64_t cursor_before, std::uint64_t cursor_after);
    void close_group();
    void append_closed(NodeId node);
    Status prune_refusal() const;
    NodeId current_node() const;
    Result<FileState> silent_walk_content(NodeId anchor);
    // One silent step of a preview, recorded so that end_preview can undo it.
    Status preview_step(std::uint64_t offset, std::uint64_t remove_len, const Payload& insert);
    SidecarSeams sidecar_seams() const;

    void install_sidecar(std::unique_ptr<Sidecar> sidecar);
    void start_scanner(const std::shared_ptr<const MappedFile>& mapping, BufferIndex buffer);
    void on_scan_done(std::uint64_t generation, Result<ContentHash> hash);
    void detect_line_ending();
    NodeMeta root_meta() const;

    Result<Written> write_content(const std::filesystem::path& target, SaveMode mode, Clipboard* clipboard);
    void materialize_for_overwrite(const std::filesystem::path& target, Clipboard* clipboard);
    // The clipboard's text taken off the overwritten file, as `copy_run` takes a run off it.
    void rebind_clipboard(Clipboard* clipboard, const std::function<bool(PieceRun&)>& copy_run);
    void copy_live_text(const std::unordered_set<BufferIndex>& hit);
    Status finish_save(const std::filesystem::path& target, Written written);

    EventQueue& queue_;
    DocumentOptions options_;
    std::shared_ptr<Document*> self_;  // cleared on destruction; captured by posted closures
    std::uint64_t id_;
    std::filesystem::path path_;
    PieceTree tree_;
    UndoTree undo_;
    std::unique_ptr<Sidecar> sidecar_;
    std::unique_ptr<LineScanner> scanner_;
    std::vector<DocumentListener*> listeners_;

    std::shared_ptr<const MappedFile> scan_mapping_;  // the mapping whose base hash is pending
    std::vector<std::pair<std::shared_ptr<const MappedFile>, BufferIndex>> mappings_;  // every file mapped
    std::uint64_t chunk_generation_ = 0;  // line counts of older scans are ignored
    std::uint64_t hash_generation_ = 0;   // hashes of abandoned scans are ignored

    bool crlf_ = false;
    bool read_only_file_ = false;
    bool new_file_ = false;  // never existed on disk and never saved
    bool kept_external_ = false;
    std::optional<FileIdentity> known_identity_;  // nullopt: absent
    std::uint64_t version_ = 0;
    std::int64_t history_load_ms_ = 0;
    std::string status_message_;

    // History bookkeeping.
    bool verifying_ = false;
    bool persist_ = false;     // Persist History
    struct PreviewStep {
        std::uint64_t offset;
        std::uint64_t inserted_len;
        PieceRun erased;
    };
    std::vector<PreviewStep> preview_steps_;  // applied by begin_preview, newest last
    bool previewing_ = false;
    bool unreadable_ = false;  // a sidecar file was there but could not be loaded
    NodeId candidate_ = 0;
    NodeId reserved_root_ = 0;
    NodeId first_session_id_ = 0;
    std::uint64_t base_size_ = 0;
    std::optional<NodeId> root_pending_;   // a root whose hash the scanner has not produced
    std::optional<ContentHash> disk_hash_;  // hash of the content on disk, when known

    // The open coalescing group (always the current node) and explicit groups.
    bool group_open_ = false;
    EditKind group_kind_ = EditKind::other;
    std::int64_t last_op_ms_ = 0;
    std::uint64_t last_op_offset_ = 0;
    std::uint64_t last_op_end_ = 0;
    bool last_typed_space_ = false;
    int group_depth_ = 0;
    EditKind explicit_kind_ = EditKind::other;
    bool explicit_has_node_ = false;
};

}  // namespace mod
