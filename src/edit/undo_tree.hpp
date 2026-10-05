#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "text/piece_tree.hpp"
#include "util/error.hpp"
#include "util/hash.hpp"

namespace mod {

using NodeId = std::uint64_t;
inline constexpr NodeId kNoParent = std::numeric_limits<NodeId>::max();
// The largest id a loaded history may use: far beyond any real history, and far enough
// below kNoParent that new ids can never reach it.
inline constexpr NodeId kMaxNodeId = NodeId{1} << 62;

// Payloads up to this many bytes are stored inline (in memory and in NODE records).
inline constexpr std::uint64_t kInlineMax = 4096;

// Bytes stored in the sidecar's payload area.
struct SidecarRef {
    std::uint64_t file_offset = 0;
    std::uint64_t length = 0;

    friend bool operator==(const SidecarRef&, const SidecarRef&) = default;
};

// The bytes removed or inserted by one op, in exactly one of three forms.
struct Payload {
    struct Inline {
        std::string bytes;
    };
    struct Pieces {
        PieceRun run;
        std::uint64_t length = 0;
    };

    std::variant<Inline, Pieces, SidecarRef> form;

    static Payload of_bytes(std::string bytes) { return {Inline{std::move(bytes)}}; }
    static Payload of_run(PieceRun run, std::uint64_t length) { return {Pieces{std::move(run), length}}; }
    static Payload of_ref(SidecarRef ref) { return {ref}; }

    std::uint64_t length() const noexcept;
};

enum class Which : std::uint8_t { removed, inserted };

// Erase `removed.length()` bytes at `offset`, then insert `inserted` there.
struct EditOp {
    std::uint64_t offset = 0;
    Payload removed;
    Payload inserted;
};

// Numeric values are the sidecar's `kind` byte.
enum class EditKind : std::uint8_t { typing, delete_, paste, cut, replace, replace_all, newline, indent, other };

struct NodeMeta {
    NodeId id = 0;
    NodeId parent = kNoParent;
    std::int64_t time_unix_ms = 0;
    EditKind kind = EditKind::other;
    std::uint64_t cursor_before = 0;
    std::uint64_t cursor_after = 0;
};

enum class StepDirection : std::uint8_t { undo, redo };

struct PathStep {
    NodeId node = 0;
    StepDirection direction = StepDirection::undo;
};

struct PreferredChange {
    NodeId parent = 0;
    NodeId child = 0;
};

// `index` is 1-based, for "branch 2/3".
struct BranchIndicator {
    std::uint32_t index = 0;
    std::uint32_t count = 0;
};

struct FileState {
    std::uint64_t size = 0;
    ContentHash hash{};
};

struct NodeInfo {
    const NodeMeta& meta;
    std::span<const NodeId> children;
    std::optional<NodeId> preferred_child;
    bool is_save_point = false;
    std::uint32_t op_count = 0;
};

// Which nodes a prune keeps and how they are joined. Invalidated by any structural
// mutation of the tree; rebinding payloads does not invalidate it.
struct PrunePlan {
    std::int64_t cutoff_ms = 0;
    NodeId anchor = 0;  // its content becomes the new root's base; the new root reuses its id
    bool anchor_is_root = false;
    std::vector<NodeId> tops;  // kept nodes whose parent is not kept, ascending
    std::vector<NodeId> kept;  // kept non-root nodes of the current tree, ascending
    std::int64_t root_time_ms = 0;
    std::uint64_t removed_count = 0;
    std::uint64_t removed_trees = 0;
};

// One kept node as it will be after the prune. Its pruned ops are the concatenation of
// `op_runs`; the spans are invalidated by the next mutation of the tree.
struct PrunedNode {
    NodeMeta meta;
    std::vector<std::span<const EditOp>> op_runs;
};

// An unlimited, automatically branching undo forest. Pure bookkeeping: it never
// touches the piece tree or the disk. Main thread only.
class UndoTree {
public:
    NodeId add_root(NodeMeta meta, std::uint64_t base_size, const ContentHash& base_hash);
    NodeId commit(std::vector<EditOp> ops, NodeMeta meta);
    void amend_current(EditOp op, std::uint64_t cursor_after);

    std::optional<NodeId> undo_step();
    std::optional<NodeId> redo_step();
    std::optional<BranchIndicator> cycle_branch(int direction);

    std::vector<PathStep> path(NodeId from, NodeId to) const;
    PreferredChange follow(const PathStep& step);

    NodeInfo node_info(NodeId node) const;
    const std::vector<NodeId>& roots() const noexcept { return roots_; }
    const std::vector<NodeId>& retired_roots() const noexcept { return retired_roots_; }
    NodeId root_of(NodeId node) const;
    bool contains(NodeId node) const { return index_.contains(node); }
    bool is_retired(NodeId node) const;
    std::optional<NodeId> current() const noexcept { return current_; }

    void reset();

    void mark_saved(NodeId node, std::uint64_t size, const ContentHash& hash);
    bool is_at_saved() const noexcept { return current_ && saved_ && *current_ == *saved_; }
    std::optional<NodeId> saved_node() const noexcept { return saved_; }

    Status load_root(NodeMeta meta, std::uint64_t base_size, const ContentHash& base_hash);
    Status load_node(NodeMeta meta, std::vector<EditOp> ops);
    void set_position(std::optional<NodeId> current, std::span<const PreferredChange> preferred_changes);

    // Fills in a live root's base hash once the scanner has produced it.
    void set_base_hash(NodeId root, const ContentHash& hash);

    NodeId reserve_id() { return next_id_++; }
    void reroot(NodeId old_parent, NodeId first_session_id, NodeId root_id, NodeMeta meta,
                std::uint64_t base_size, const ContentHash& base_hash);

    void visit_pieces(const std::function<bool(NodeId, std::uint32_t, Which, PieceRun&)>& visit);
    void rebind_payload(NodeId node, std::uint32_t op_index, Which which, SidecarRef ref);

    std::span<const EditOp> ops(NodeId node) const;
    const NodeMeta& meta(NodeId node) const;
    std::optional<FileState> root_base(NodeId node) const;
    std::optional<FileState> save_point(NodeId node) const;
    // Increases with each `mark_saved`; 0 for a node that is not a live save point.
    std::uint64_t save_order(NodeId node) const;
    NodeId next_id() const noexcept { return next_id_; }

    std::optional<std::int64_t> oldest_change() const;
    PrunePlan plan_prune(std::int64_t cutoff_ms) const;
    void for_each_pruned(const PrunePlan& plan, const std::function<bool(const PrunedNode&)>& visit) const;
    // The `(parent, preferred_child)` pairs `apply_prune` will leave, for the POSITION record.
    std::vector<PreferredChange> pruned_preferred(const PrunePlan& plan) const;
    void apply_prune(const PrunePlan& plan, std::uint64_t base_size, const ContentHash& base_hash);

private:
    struct Node {
        NodeMeta meta;
        std::vector<NodeId> children;  // ascending
        std::optional<NodeId> preferred;
        std::vector<EditOp> ops;
        std::optional<FileState> base;  // roots only
        std::optional<FileState> save;  // latest save point
        std::uint64_t save_seq = 0;     // order of the latest `mark_saved`
        bool was_save_point = false;
        bool retired = false;
    };

    Node& at(NodeId id);
    const Node& at(NodeId id) const;
    const Node* find(NodeId id) const;
    Node& emplace(NodeMeta meta);
    static void insert_child(Node& parent, NodeId child);
    bool too_old(const Node& n, std::int64_t cutoff_ms) const;
    std::vector<NodeId> absorbed_path(const PrunePlan& plan, NodeId top) const;

    std::vector<Node> nodes_;
    std::unordered_map<NodeId, std::uint32_t> index_;
    std::vector<NodeId> roots_;
    std::vector<NodeId> retired_roots_;
    std::optional<NodeId> current_;
    std::optional<NodeId> saved_;
    NodeId next_id_ = 1;
    std::uint64_t save_counter_ = 0;
};

}  // namespace mod
