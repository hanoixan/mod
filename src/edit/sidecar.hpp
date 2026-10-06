#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "edit/undo_tree.hpp"
#include "platform/file_map.hpp"
#include "text/piece_tree.hpp"
#include "util/error.hpp"
#include "util/event_queue.hpp"
#include "util/hash.hpp"
#include "util/progress.hpp"

namespace mod {

// `<dir>/<full file name>.history` for an already resolved document path.
std::filesystem::path sidecar_path_for(const std::filesystem::path& document_path);

// Injection points for tests; empty members fall back to the system.
struct SidecarSeams {
    std::function<std::int64_t()> now_ms;                       // Unix milliseconds
    std::function<long(int fd, const void* data, std::size_t size)> write;  // as ::write
    std::function<int(int fd)> sync;                            // as fdatasync
    ProgressSink progress;  // copy_to, rewrite and flush report here
    std::function<int(int fd, std::int64_t size)> truncate;     // as ftruncate (Windows refuses one on a mapped file)
};

enum class SidecarState { pathless, attached, read_only, disabled, session_only };

struct LoadOutcome {
    enum State { no_history, provisional, read_only, disabled } state = no_history;
    std::optional<NodeId> candidate;
    std::int64_t load_ms = 0;  // how long `open` took; 0 when no sidecar file was read
};

// A payload that `rewrite` wrote as a PAYLOAD record; `op_index` is the pruned index.
struct Rebind {
    NodeId node = 0;
    std::uint32_t op_index = 0;
    Which which = Which::removed;
    SidecarRef ref;
};

// One payload as handed to the writer thread: inline bytes, worker-safe views of
// immutable buffers, or a reference into the sidecar itself.
struct PayloadView {
    std::variant<std::string, std::vector<FrozenBytes>, SidecarRef> form;
    std::uint64_t length = 0;
};

struct NodeOp {
    std::uint64_t offset = 0;
    PayloadView removed;
    PayloadView inserted;
};

// Converts a node's ops for `append_node`; `Pieces` become views through `frozen_bytes`.
std::vector<NodeOp> to_node_ops(std::span<const EditOp> ops, const PieceTree& text);

// Persists an UndoTree to `<real path>.history` as an append-only, checksummed record log.
// Main-thread API; every file write happens on one internal writer thread.
class Sidecar {
public:
    struct Callbacks {
        std::function<void(NodeId, std::uint32_t, Which, SidecarRef)> on_payload_written;
        std::function<void(std::string)> on_failure;
    };

    Sidecar(std::optional<std::filesystem::path> doc_path, EventQueue& queue, std::uint32_t file_mode,
            SidecarSeams seams = {});
    ~Sidecar();
    // A sidecar that never writes, for a path whose history could not be copied.
    static std::unique_ptr<Sidecar> make_session_only(EventQueue& queue);

    Sidecar(const Sidecar&) = delete;
    Sidecar& operator=(const Sidecar&) = delete;

    void set_callbacks(Callbacks callbacks);

    Result<LoadOutcome> open(UndoTree& tree, std::uint64_t file_size);
    // The candidate when `hash` matches its recorded hash.
    std::optional<NodeId> verify(const ContentHash& hash) const;

    // The deferral rule: appends are held in memory until `release_deferred`.
    void defer();
    void release_deferred(const UndoTree& tree);
    void remap_deferred(NodeId from, NodeId to);
    void discard_deferred();
    bool deferred() const noexcept { return deferred_; }

    void append_root(const NodeMeta& meta, std::uint64_t base_size, const ContentHash& base_hash);
    void append_node(const NodeMeta& meta, std::vector<NodeOp> ops);
    void append_save(NodeId node, std::uint64_t size, const ContentHash& hash);
    void append_position(NodeId current, const std::vector<PreferredChange>& preferred_changes);

    Result<std::pair<BufferIndex, std::uint64_t>> payload_bytes(PieceTree& text, SidecarRef ref);

    Result<std::unique_ptr<Sidecar>> copy_to(const std::filesystem::path& new_doc_path, std::uint32_t file_mode,
                                             const UndoTree& tree, const PieceTree& text);
    Status clear();
    Result<std::vector<Rebind>> rewrite(const UndoTree& tree, const PrunePlan& plan, std::uint64_t base_size,
                                        const ContentHash& base_hash, const PieceTree& text);
    void detach_views(const void* mapping);
    Status flush(int timeout_ms);

    SidecarState state() const noexcept { return state_; }
    // Persistence off: records are held in memory instead of written, and no file is
    // created; unpausing writes everything held. Only an `attached` sidecar writes anyway.
    void set_paused(bool paused);
    bool paused() const noexcept { return persist_paused_; }
    // A sidecar file was there when `open` ran (whether or not it could be loaded).
    bool had_file() const noexcept { return had_file_; }
    const std::optional<std::filesystem::path>& path() const noexcept { return path_; }

private:
    struct RootRec {
        NodeMeta meta;
        std::uint64_t size = 0;
        ContentHash hash{};
    };
    struct NodeRec {
        NodeMeta meta;
        std::vector<NodeOp> ops;
    };
    struct SaveRec {
        NodeId node = 0;
        std::uint64_t size = 0;
        ContentHash hash{};
        std::int64_t time_ms = 0;
    };
    struct PositionRec {
        NodeId current = 0;
        std::vector<PreferredChange> changes;
    };
    struct SyncRec {};  // fsync the file once, after a rewrite
    using Job = std::variant<RootRec, NodeRec, SaveRec, PositionRec, SyncRec>;

    // Shared with posted closures so they can tell that this object is gone.
    struct Token {
        bool alive = true;
        Sidecar* self = nullptr;
        Callbacks callbacks;
    };

    struct Candidate {
        NodeId node = 0;
        ContentHash hash{};
    };

    enum class Disabled { none, foreign, write_failed };

    Result<LoadOutcome> load(UndoTree& tree, std::uint64_t file_size, bool& read_file);
    bool wait_idle(int timeout_ms);
    void enqueue(Job job);
    void hold(Job job);
    void queue_held();
    void emit_pending_position();
    void writer_loop(std::stop_token stop);
    void pause_writer(std::unique_lock<std::mutex>& lock);
    void resume_writer(std::unique_lock<std::mutex>& lock);

    // Writer thread (or the main thread while the writer is paused and idle).
    bool ensure_file();
    // Replaces the file with its first `size` bytes, for a system that cannot shorten a file
    // while it is mapped (Windows); false when that fails too.
    bool rewrite_without_tail(std::uint64_t size);
    bool write_bytes(std::span<const std::byte> bytes);
    bool write_job(Job& job);
    void fail(const std::string& message, SidecarState state);
    void sync_file();
    std::uint64_t queued_payload_bytes() const;  // under `mutex_`

    std::int64_t now_ms() const;

    std::optional<std::filesystem::path> path_;
    EventQueue& queue_;
    std::uint32_t file_mode_;
    SidecarSeams seams_;
    std::shared_ptr<Token> token_;
    SidecarState state_;
    Disabled disabled_ = Disabled::none;
    bool newer_version_ = false;

    std::optional<Candidate> candidate_;
    bool deferred_ = true;
    bool persist_paused_ = false;  // persistence off: records are held, nothing is written
    bool had_file_ = false;
    bool file_known_ = false;  // the file exists, or a NODE record has been queued to create it
    std::vector<Job> held_;  // held back by the deferral rule
    std::optional<PositionRec> pending_position_;
    std::int64_t last_position_ms_ = 0;

    std::shared_ptr<const MappedFile> payload_map_;
    const PieceTree* payload_tree_ = nullptr;
    BufferIndex payload_buffer_ = 0;

    // Writer state, guarded by `mutex_`.
    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable idle_cv_;
    std::deque<Job> queue_jobs_;
    bool busy_ = false;
    bool paused_ = false;
    int fd_ = -1;
    bool file_exists_ = false;
    bool need_header_ = true;
    std::uint64_t file_size_ = 0;
    std::atomic<std::uint64_t> bytes_written_{0};  // by the writer, for flush's progress
    std::uint64_t good_size_ = 0;  // end of the last complete record
    std::optional<std::uint64_t> truncate_to_;
    bool failed_ = false;
    std::unordered_set<NodeId> in_file_;  // ROOT and NODE records already in the file
    std::jthread writer_;
};

}  // namespace mod
