#include "edit/undo_tree.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <unordered_set>

namespace mod {

std::uint64_t Payload::length() const noexcept {
    if (const auto* p = std::get_if<Inline>(&form)) return p->bytes.size();
    if (const auto* p = std::get_if<Pieces>(&form)) return p->length;
    if (const auto* p = std::get_if<SidecarRef>(&form)) return p->length;
    return 0;  // valueless, which only a throwing assignment leaves behind
}

UndoTree::Node& UndoTree::at(NodeId id) {
    const auto it = index_.find(id);
    assert(it != index_.end());
    return nodes_[it->second];
}

const UndoTree::Node& UndoTree::at(NodeId id) const {
    const auto it = index_.find(id);
    assert(it != index_.end());
    return nodes_[it->second];
}

const UndoTree::Node* UndoTree::find(NodeId id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &nodes_[it->second];
}

UndoTree::Node& UndoTree::emplace(NodeMeta meta) {
    index_.emplace(meta.id, static_cast<std::uint32_t>(nodes_.size()));
    next_id_ = std::max(next_id_, meta.id + 1);
    Node& n = nodes_.emplace_back();
    n.meta = meta;
    return n;
}

void UndoTree::insert_child(Node& parent, NodeId child) {
    auto& c = parent.children;
    c.insert(std::lower_bound(c.begin(), c.end(), child), child);
}

NodeId UndoTree::add_root(NodeMeta meta, std::uint64_t base_size, const ContentHash& base_hash) {
    meta.id = next_id_;
    meta.parent = kNoParent;
    Node& n = emplace(meta);
    n.base = FileState{base_size, base_hash};
    roots_.push_back(meta.id);
    current_ = meta.id;
    return meta.id;
}

NodeId UndoTree::commit(std::vector<EditOp> ops, NodeMeta meta) {
    assert(current_ && !ops.empty());
    meta.id = next_id_;
    meta.parent = *current_;
    Node& n = emplace(meta);
    n.ops = std::move(ops);
    Node& parent = at(meta.parent);  // after emplace: `nodes_` may have moved
    insert_child(parent, meta.id);
    parent.preferred = meta.id;
    current_ = meta.id;
    return meta.id;
}

namespace {

std::string* inline_bytes(Payload& p) { return std::get_if<Payload::Inline>(&p.form) ? &std::get<Payload::Inline>(p.form).bytes : nullptr; }

}  // namespace

void UndoTree::amend_current(EditOp op, std::uint64_t cursor_after) {
    assert(current_);
    Node& n = at(*current_);
    n.meta.cursor_after = cursor_after;
    if (!n.ops.empty()) {
        EditOp& last = n.ops.back();
        // An insertion right after the last insertion.
        if (last.removed.length() == 0 && op.removed.length() == 0 && op.offset == last.offset + last.inserted.length() &&
            last.inserted.length() + op.inserted.length() <= kInlineMax) {
            std::string* a = inline_bytes(last.inserted);
            std::string* b = inline_bytes(op.inserted);
            if (a != nullptr && b != nullptr) {
                *a += *b;
                return;
            }
        }
        // A deletion just before (Backspace) or at (Delete) the last deletion.
        if (last.inserted.length() == 0 && op.inserted.length() == 0 &&
            last.removed.length() + op.removed.length() <= kInlineMax) {
            std::string* a = inline_bytes(last.removed);
            std::string* b = inline_bytes(op.removed);
            if (a != nullptr && b != nullptr) {
                if (op.offset + b->size() == last.offset) {
                    *a = *b + *a;
                    last.offset = op.offset;
                    return;
                }
                if (op.offset == last.offset) {
                    *a += *b;
                    return;
                }
            }
        }
    }
    n.ops.push_back(std::move(op));
}

std::optional<NodeId> UndoTree::undo_step() {
    if (!current_) return std::nullopt;
    const NodeId node = *current_;
    const NodeId parent = at(node).meta.parent;
    if (parent == kNoParent) return std::nullopt;
    at(parent).preferred = node;
    current_ = parent;
    return node;
}

std::optional<NodeId> UndoTree::redo_step() {
    if (!current_) return std::nullopt;
    const auto child = at(*current_).preferred;
    if (!child) return std::nullopt;
    current_ = child;
    return child;
}

std::optional<BranchIndicator> UndoTree::cycle_branch(int direction) {
    if (!current_) return std::nullopt;
    Node& n = at(*current_);
    const auto count = static_cast<std::int64_t>(n.children.size());
    if (count < 2) return std::nullopt;
    std::int64_t idx = 0;
    if (n.preferred) {
        idx = std::lower_bound(n.children.begin(), n.children.end(), *n.preferred) - n.children.begin();
        idx = ((idx + (direction < 0 ? -1 : 1)) % count + count) % count;
    }
    n.preferred = n.children[static_cast<std::size_t>(idx)];
    return BranchIndicator{static_cast<std::uint32_t>(idx + 1), static_cast<std::uint32_t>(count)};
}

std::vector<PathStep> UndoTree::path(NodeId from, NodeId to) const {
    if (root_of(from) != root_of(to) || is_retired(from) || is_retired(to)) return {};
    auto depth = [&](NodeId n) {
        std::uint64_t d = 0;
        for (NodeId p = at(n).meta.parent; p != kNoParent; p = at(p).meta.parent) ++d;
        return d;
    };
    NodeId a = from;
    NodeId b = to;
    std::uint64_t da = depth(a);
    std::uint64_t db = depth(b);
    std::vector<PathStep> up;
    std::vector<PathStep> down;
    for (; da > db; --da) {
        up.push_back({a, StepDirection::undo});
        a = at(a).meta.parent;
    }
    for (; db > da; --db) {
        down.push_back({b, StepDirection::redo});
        b = at(b).meta.parent;
    }
    while (a != b) {
        up.push_back({a, StepDirection::undo});
        down.push_back({b, StepDirection::redo});
        a = at(a).meta.parent;
        b = at(b).meta.parent;
    }
    up.insert(up.end(), down.rbegin(), down.rend());
    return up;
}

PreferredChange UndoTree::follow(const PathStep& step) {
    assert(current_);
    const NodeId parent = at(step.node).meta.parent;
    assert(parent != kNoParent);
    if (step.direction == StepDirection::undo) {
        assert(step.node == *current_);
        current_ = parent;
    } else {
        assert(parent == *current_);
        current_ = step.node;
    }
    at(parent).preferred = step.node;
    return {parent, step.node};
}

NodeInfo UndoTree::node_info(NodeId node) const {
    const Node& n = at(node);
    return NodeInfo{n.meta, n.children, n.preferred, n.was_save_point, static_cast<std::uint32_t>(n.ops.size())};
}

NodeId UndoTree::root_of(NodeId node) const {
    NodeId n = node;
    for (NodeId p = at(n).meta.parent; p != kNoParent; p = at(p).meta.parent) n = p;
    return n;
}

bool UndoTree::is_retired(NodeId node) const {
    const Node* n = find(node);
    return n != nullptr && n->retired;
}

void UndoTree::reset() {
    for (Node& n : nodes_) {
        if (n.retired) continue;
        n.retired = true;
        n.ops.clear();
        n.ops.shrink_to_fit();
        n.preferred.reset();
        n.base.reset();
        n.save.reset();
        n.save_seq = 0;
    }
    retired_roots_.insert(retired_roots_.end(), roots_.begin(), roots_.end());
    roots_.clear();
    current_.reset();
    saved_.reset();
}

void UndoTree::mark_saved(NodeId node, std::uint64_t size, const ContentHash& hash) {
    Node& n = at(node);
    n.save = FileState{size, hash};
    n.save_seq = ++save_counter_;
    n.was_save_point = true;
    saved_ = node;
}

Status UndoTree::load_root(NodeMeta meta, std::uint64_t base_size, const ContentHash& base_hash) {
    if (meta.parent != kNoParent || index_.contains(meta.id) || meta.id > kMaxNodeId)
        return std::unexpected(make_error(ErrorCode::format, "invalid root record"));
    Node& n = emplace(meta);
    n.base = FileState{base_size, base_hash};
    roots_.push_back(meta.id);
    return {};
}

Status UndoTree::load_node(NodeMeta meta, std::vector<EditOp> ops) {
    const Node* parent = find(meta.parent);
    if (parent == nullptr || parent->retired || meta.id <= meta.parent || index_.contains(meta.id) || meta.id > kMaxNodeId)
        return std::unexpected(make_error(ErrorCode::format, "node with an invalid parent"));
    Node& n = emplace(meta);
    n.ops = std::move(ops);
    Node& p = at(meta.parent);
    insert_child(p, meta.id);
    p.preferred = meta.id;
    return {};
}

void UndoTree::set_position(std::optional<NodeId> current, std::span<const PreferredChange> preferred_changes) {
    for (const PreferredChange& c : preferred_changes) {
        const Node* child = find(c.child);
        if (child == nullptr || child->retired || child->meta.parent != c.parent) continue;
        at(c.parent).preferred = c.child;
    }
    if (current) {
        const Node* n = find(*current);
        if (n != nullptr && !n->retired) current_ = current;
    }
}

void UndoTree::reroot(NodeId old_parent, NodeId first_session_id, NodeId root_id, NodeMeta meta,
                      std::uint64_t base_size, const ContentHash& base_hash) {
    assert(!index_.contains(root_id) && root_id < next_id_);
    meta.id = root_id;
    meta.parent = kNoParent;
    emplace(meta).base = FileState{base_size, base_hash};
    roots_.push_back(root_id);
    Node& old = at(old_parent);
    Node& root = at(root_id);
    std::vector<NodeId> kept;
    for (NodeId c : old.children) {
        if (c >= first_session_id) {
            root.children.push_back(c);  // stays ascending
        } else {
            kept.push_back(c);
        }
    }
    old.children = std::move(kept);
    for (NodeId c : root.children) at(c).meta.parent = root_id;
    if (old.preferred && *old.preferred >= first_session_id) {
        root.preferred = old.preferred;
        old.preferred = old.children.empty() ? std::nullopt : std::optional<NodeId>(old.children.back());
    }
    if (current_ == old_parent) current_ = root_id;
    if (saved_ == old_parent) {
        root.save = FileState{base_size, base_hash};
        root.save_seq = ++save_counter_;
        root.was_save_point = true;
        saved_ = root_id;
    }
}

void UndoTree::set_base_hash(NodeId root, const ContentHash& hash) {
    Node& n = at(root);
    assert(n.base && !n.retired);
    n.base->hash = hash;
    if (n.save && n.save->size == n.base->size) n.save->hash = hash;
}

void UndoTree::visit_pieces(const std::function<bool(NodeId, std::uint32_t, Which, PieceRun&)>& visit) {
    for (Node& n : nodes_) {
        if (n.retired) continue;
        for (std::uint32_t i = 0; i < n.ops.size(); ++i) {
            for (Which w : {Which::removed, Which::inserted}) {
                Payload& p = w == Which::removed ? n.ops[i].removed : n.ops[i].inserted;
                if (auto* pieces = std::get_if<Payload::Pieces>(&p.form)) visit(n.meta.id, i, w, pieces->run);
            }
        }
    }
}

void UndoTree::rebind_payload(NodeId node, std::uint32_t op_index, Which which, SidecarRef ref) {
    const auto it = index_.find(node);
    if (it == index_.end()) return;
    Node& n = nodes_[it->second];
    if (n.retired || op_index >= n.ops.size()) return;  // a stale closure from before a reset
    Payload& p = which == Which::removed ? n.ops[op_index].removed : n.ops[op_index].inserted;
    // A SidecarRef is rebound only by a prune, into the rewritten sidecar.
    if (std::holds_alternative<Payload::Inline>(p.form) || p.length() != ref.length) return;
    p.form = ref;
}

std::span<const EditOp> UndoTree::ops(NodeId node) const { return at(node).ops; }

const NodeMeta& UndoTree::meta(NodeId node) const { return at(node).meta; }

std::optional<FileState> UndoTree::root_base(NodeId node) const {
    const Node* n = find(node);
    return n == nullptr ? std::nullopt : n->base;
}

std::optional<FileState> UndoTree::save_point(NodeId node) const {
    const Node* n = find(node);
    return n == nullptr ? std::nullopt : n->save;
}

std::uint64_t UndoTree::save_order(NodeId node) const {
    const Node* n = find(node);
    return n == nullptr || !n->save ? 0 : n->save_seq;
}

// ---- pruning -----------------------------------------------------------------------

bool UndoTree::too_old(const Node& n, std::int64_t cutoff_ms) const {
    return n.meta.parent != kNoParent && n.meta.time_unix_ms < cutoff_ms;
}

std::optional<std::int64_t> UndoTree::oldest_change() const {
    std::optional<std::int64_t> oldest;
    for (const Node& n : nodes_) {
        if (n.retired || n.meta.parent == kNoParent) continue;
        if (!oldest || n.meta.time_unix_ms < *oldest) oldest = n.meta.time_unix_ms;
    }
    return oldest;
}

PrunePlan UndoTree::plan_prune(std::int64_t cutoff_ms) const {
    assert(current_);
    PrunePlan plan;
    plan.cutoff_ms = cutoff_ms;
    const NodeId root = root_of(*current_);

    // The current tree, in ascending id.
    std::vector<NodeId> tree;
    for (std::vector<NodeId> stack{root}; !stack.empty();) {
        const NodeId n = stack.back();
        stack.pop_back();
        tree.push_back(n);
        const auto& c = at(n).children;
        stack.insert(stack.end(), c.begin(), c.end());
    }
    std::sort(tree.begin(), tree.end());

    // Forced: `current` and the latest save point in the tree.
    std::optional<NodeId> latest_save;
    if (saved_ && contains(*saved_) && !is_retired(*saved_) && root_of(*saved_) == root) {
        latest_save = saved_;
    } else {
        std::uint64_t best = 0;
        for (const NodeId id : tree) {
            const Node& n = at(id);
            if (n.save && n.save_seq >= best) {
                best = n.save_seq;
                latest_save = id;
            }
        }
    }
    auto forced = [&](NodeId id) { return id == *current_ || id == latest_save; };

    // 1. Walk back from every leaf and forced node.
    std::unordered_set<NodeId> kept;
    for (const NodeId s : tree) {
        const Node& n = at(s);
        if (!n.children.empty() && !forced(s)) continue;  // not a start
        if (s == root || kept.contains(s)) continue;
        if (too_old(n, cutoff_ms) && !forced(s)) continue;
        kept.insert(s);
        for (NodeId p = n.meta.parent; p != root && !kept.contains(p) && !too_old(at(p), cutoff_ms); p = at(p).meta.parent)
            kept.insert(p);
    }
    for (const NodeId id : tree) {
        if (!kept.contains(id)) continue;
        plan.kept.push_back(id);
        if (!kept.contains(at(id).meta.parent)) plan.tops.push_back(id);
    }

    // 3. The anchor: the lowest common ancestor of the tops' parents. Climb from each
    // parent until a node already seen, counting the distinct children climbed from;
    // then descend from the root while there is one way down and no parent is passed.
    plan.anchor = root;
    if (!forced(root) && !plan.kept.empty()) {
        struct Seen {
            std::uint32_t children = 0;
            NodeId child = 0;
        };
        std::unordered_map<NodeId, Seen> seen;
        std::unordered_set<NodeId> parents;
        for (const NodeId t : plan.tops) parents.insert(at(t).meta.parent);
        for (const NodeId start : parents) {
            std::optional<NodeId> from;
            for (NodeId n = start;; n = at(n).meta.parent) {
                const auto [it, fresh] = seen.try_emplace(n);
                if (from) {
                    ++it->second.children;
                    it->second.child = *from;
                }
                if (!fresh || n == root) break;
                from = n;
            }
        }
        NodeId n = root;
        while (!parents.contains(n) && seen[n].children == 1) n = seen[n].child;
        plan.anchor = n;
    }
    plan.anchor_is_root = plan.anchor == root;

    plan.root_time_ms = at(plan.anchor).meta.time_unix_ms;
    if (!plan.tops.empty()) {
        plan.root_time_ms = at(plan.tops.front()).meta.time_unix_ms;
        for (const NodeId t : plan.tops) plan.root_time_ms = std::min(plan.root_time_ms, at(t).meta.time_unix_ms);
    }

    // 4. Everything else in the tree goes, and every other live tree goes whole.
    plan.removed_count = tree.size() - plan.kept.size() - 1;
    for (const NodeId r : roots_) {
        if (r == root) continue;
        ++plan.removed_trees;
        for (std::vector<NodeId> stack{r}; !stack.empty();) {
            const NodeId n = stack.back();
            stack.pop_back();
            ++plan.removed_count;
            const auto& c = at(n).children;
            stack.insert(stack.end(), c.begin(), c.end());
        }
    }
    return plan;
}

// The nodes from the anchor's child down to `top` inclusive, in path order.
std::vector<NodeId> UndoTree::absorbed_path(const PrunePlan& plan, NodeId top) const {
    std::vector<NodeId> path;
    for (NodeId n = top; n != plan.anchor; n = at(n).meta.parent) {
        assert(n != kNoParent);
        path.push_back(n);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

void UndoTree::for_each_pruned(const PrunePlan& plan, const std::function<bool(const PrunedNode&)>& visit) const {
    const std::unordered_set<NodeId> kept(plan.kept.begin(), plan.kept.end());
    for (const NodeId id : plan.kept) {
        const Node& n = at(id);
        PrunedNode pn{n.meta, {}};
        if (kept.contains(n.meta.parent)) {
            pn.op_runs.emplace_back(n.ops);
        } else {
            const std::vector<NodeId> path = absorbed_path(plan, id);
            pn.meta.parent = plan.anchor;
            pn.meta.cursor_before = at(path.front()).meta.cursor_before;
            for (const NodeId p : path) pn.op_runs.emplace_back(at(p).ops);
        }
        if (!visit(pn)) return;
    }
}

std::vector<PreferredChange> UndoTree::pruned_preferred(const PrunePlan& plan) const {
    assert(current_);
    const std::unordered_set<NodeId> kept(plan.kept.begin(), plan.kept.end());
    std::vector<PreferredChange> out;
    if (*current_ != plan.anchor) {
        NodeId n = *current_;
        while (kept.contains(at(n).meta.parent)) n = at(n).meta.parent;
        out.push_back({plan.anchor, n});
    }
    for (const NodeId id : plan.kept) {
        const Node& n = at(id);
        if (n.preferred && kept.contains(*n.preferred)) {
            out.push_back({id, *n.preferred});
            continue;
        }
        // Otherwise the most recently created kept child (children are ascending).
        for (auto it = n.children.rbegin(); it != n.children.rend(); ++it) {
            if (kept.contains(*it)) {
                out.push_back({id, *it});
                break;
            }
        }
    }
    return out;
}

void UndoTree::apply_prune(const PrunePlan& plan, std::uint64_t base_size, const ContentHash& base_hash) {
    assert(current_ && contains(plan.anchor) && root_of(*current_) == root_of(plan.anchor));
    const std::unordered_set<NodeId> kept(plan.kept.begin(), plan.kept.end());
    assert(*current_ == plan.anchor || kept.contains(*current_));
    const std::vector<PreferredChange> preferred = pruned_preferred(plan);

    // The tops' concatenated ops. A removed node's ops move to the last top that absorbs
    // it; a kept node, or one shared by several tops, is copied.
    struct Absorbed {
        std::vector<EditOp> ops;
        std::uint64_t cursor_before = 0;
    };
    std::unordered_map<NodeId, Absorbed> absorbed;
    std::unordered_map<NodeId, std::uint32_t> uses;
    std::vector<std::vector<NodeId>> paths;
    for (const NodeId top : plan.tops) {
        paths.push_back(absorbed_path(plan, top));
        for (const NodeId p : paths.back()) ++uses[p];
    }
    for (std::size_t i = 0; i < plan.tops.size(); ++i) {
        const std::vector<NodeId>& path = paths[i];
        if (path.size() == 1) continue;  // a child of the anchor keeps its own ops
        Absorbed a;
        a.cursor_before = at(path.front()).meta.cursor_before;
        for (const NodeId p : path) {
            Node& n = at(p);
            if (kept.contains(p) || --uses[p] > 0) {
                a.ops.insert(a.ops.end(), n.ops.begin(), n.ops.end());
            } else {
                std::move(n.ops.begin(), n.ops.end(), std::back_inserter(a.ops));
                n.ops.clear();
            }
        }
        absorbed.emplace(plan.tops[i], std::move(a));
    }

    // One pass: keep retired nodes, the new root and the kept nodes; drop the rest.
    std::vector<Node> out;
    out.reserve(plan.kept.size() + 1);
    for (Node& n : nodes_) {
        const NodeId id = n.meta.id;
        if (n.retired) {
            out.push_back(std::move(n));
        } else if (id == plan.anchor) {
            Node r;
            r.meta = NodeMeta{id, kNoParent, plan.root_time_ms, EditKind::other, 0, 0};
            r.base = FileState{base_size, base_hash};
            if (n.save) {
                r.save = FileState{n.save->size, n.save->hash};
                r.save_seq = n.save_seq;
                r.was_save_point = true;
            }
            r.children = plan.tops;
            out.push_back(std::move(r));
        } else if (kept.contains(id)) {
            Node k = std::move(n);
            if (!kept.contains(k.meta.parent)) {
                k.meta.parent = plan.anchor;
                if (auto it = absorbed.find(id); it != absorbed.end()) {
                    k.ops = std::move(it->second.ops);
                    k.meta.cursor_before = it->second.cursor_before;
                }
            }
            std::erase_if(k.children, [&](NodeId c) { return !kept.contains(c); });
            k.preferred.reset();
            out.push_back(std::move(k));
        }
    }
    nodes_ = std::move(out);
    index_.clear();
    for (std::uint32_t i = 0; i < nodes_.size(); ++i) index_.emplace(nodes_[i].meta.id, i);
    for (const PreferredChange& c : preferred) at(c.parent).preferred = c.child;
    roots_ = {plan.anchor};
    if (saved_ && !index_.contains(*saved_)) saved_.reset();
}

}  // namespace mod
