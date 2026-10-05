#include "edit/document.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <unordered_set>

#include "edit/clipboard.hpp"
#include "platform/fs.hpp"
#include "util/log.hpp"

namespace fs = std::filesystem;

namespace mod {
namespace {

constexpr std::uint64_t kInsertedSmallMax = 64 * 1024;
constexpr int kFlushTimeoutMs = 5000;
constexpr std::uint64_t kProgressSlice = std::uint64_t{1} << 20;  // hash bytes between progress reports

std::uint64_t next_document_id() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1);
}

std::uint64_t run_length(const PieceRun& run) {
    std::uint64_t n = 0;
    for (const Piece& p : run) n += p.length;
    return n;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

ContentHash empty_hash() { return ContentHasher{}.finish(); }

std::uint32_t permission_bits(const fs::path& p) {
    std::error_code ec;
    const auto st = fs::status(p, ec);
    if (ec || !fs::exists(st)) return 0600;  // no file yet: keep the history private
    return static_cast<std::uint32_t>(st.permissions() & fs::perms::mask);
}

bool same_file(const FileIdentity& a, const FileIdentity& b) { return a.device == b.device && a.inode == b.inode; }

}  // namespace

struct Document::Resolved {
    std::string bytes;
    PieceRun run;
    bool is_run = false;
    std::uint64_t length = 0;
};

Document::Document(EventQueue& queue, DocumentOptions options, std::shared_ptr<const MappedFile> original)
    : queue_(queue),
      options_(std::move(options)),
      self_(std::make_shared<Document*>(this)),
      id_(next_document_id()),
      tree_(original, options_.chunk_size) {
    if (original) mappings_.emplace_back(std::move(original), BufferIndex{1});
}

Document::~Document() {
    *self_ = nullptr;
    scanner_.reset();
    sidecar_.reset();
}

std::int64_t Document::now() const {
    if (options_.now_ms) return options_.now_ms();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// The sidecar shares the document's clock, so an injected clock also drives `load_ms`.
SidecarSeams Document::sidecar_seams() const { return SidecarSeams{options_.now_ms, {}, {}, options_.progress}; }

NodeMeta Document::root_meta() const { return NodeMeta{0, kNoParent, now(), EditKind::other, 0, 0}; }

// ---- opening -----------------------------------------------------------------------

Result<std::unique_ptr<Document>> Document::open(const fs::path& path, EventQueue& queue, DocumentOptions options) {
    auto real = resolve_real_path(path);
    if (!real) return std::unexpected(real.error());
    std::error_code ec;
    if (fs::is_directory(*real, ec)) return std::unexpected(make_error(ErrorCode::unsupported, real->string() + " is a directory"));

    std::shared_ptr<const MappedFile> mapping;
    if (auto m = MappedFile::open(*real)) {
        mapping = std::move(*m);
    } else if (m.error().code != ErrorCode::not_found) {
        return std::unexpected(m.error());
    }
    std::unique_ptr<Document> doc(new Document(queue, std::move(options), mapping));
    doc->path_ = *real;
    doc->new_file_ = mapping == nullptr;
    doc->read_only_file_ = mapping != nullptr && !is_writable(*real);
    if (mapping) doc->known_identity_ = mapping->identity();
    doc->detect_line_ending();
    const std::uint64_t size = doc->tree_.size();
    doc->base_size_ = size;

    Result<LoadOutcome> outcome = LoadOutcome{LoadOutcome::no_history, std::nullopt};
    if (doc->options_.history) {
        doc->install_sidecar(std::make_unique<Sidecar>(*real, queue, permission_bits(*real), doc->sidecar_seams()));
        outcome = doc->sidecar_->open(doc->undo_, size);
    } else {
        doc->install_sidecar(Sidecar::make_session_only(queue));
    }
    if (!outcome) {
        doc->status_message_ = "history unavailable: " + outcome.error().message;
        doc->install_sidecar(Sidecar::make_session_only(queue));
        outcome = LoadOutcome{LoadOutcome::no_history, std::nullopt};
        doc->unreadable_ = true;
    }
    // History is kept in memory unless a loadable sidecar was there (or persistence was asked for).
    if (doc->options_.history) {
        const SidecarState s = doc->sidecar_->state();
        if (s == SidecarState::disabled && doc->sidecar_->had_file()) doc->unreadable_ = true;
        const bool loaded = doc->sidecar_->had_file() && (s == SidecarState::attached || s == SidecarState::read_only);
        doc->persist_ = loaded || doc->options_.persist_history == PersistHistory::always;
        if (!doc->persist_) doc->sidecar_->set_paused(true);
    }
    doc->history_load_ms_ = outcome->load_ms;
    if (const auto candidate = outcome->candidate) {
        // A provisional match: edit at once, write nothing until the hash confirms it.
        const NodeId c = *candidate;
        doc->verifying_ = true;
        doc->candidate_ = c;
        doc->reserved_root_ = doc->undo_.reserve_id();
        doc->first_session_id_ = doc->undo_.next_id();
        doc->undo_.set_position(c, {});
        const auto state = doc->undo_.save_point(c) ? doc->undo_.save_point(c) : doc->undo_.root_base(c);
        doc->undo_.mark_saved(c, size, state ? state->hash : ContentHash{});
        if (outcome->state == LoadOutcome::read_only) doc->status_message_ = "history is open in another mod";
    } else {
        const NodeId root = doc->undo_.add_root(doc->root_meta(), size, ContentHash{});
        doc->undo_.mark_saved(root, size, ContentHash{});
        doc->root_pending_ = root;
        doc->first_session_id_ = root;
        if (outcome->state == LoadOutcome::read_only) doc->status_message_ = "history is open in another mod";
        if (outcome->state == LoadOutcome::disabled) doc->status_message_ = "history disabled: " + real->filename().string() + ".mod is not a history file";
    }

    if (mapping && mapping->size() > 0) {
        doc->start_scanner(mapping, 1);
    } else {
        // Nothing to scan: the hash of empty content is a constant.
        doc->on_scan_done(doc->hash_generation_, empty_hash());
    }
    return doc;
}

std::unique_ptr<Document> Document::open_untitled(EventQueue& queue, DocumentOptions options) {
    std::unique_ptr<Document> doc(new Document(queue, std::move(options), nullptr));
    doc->new_file_ = true;
    const ContentHash hash = empty_hash();
    doc->disk_hash_ = hash;
    doc->persist_ = doc->options_.persist_history == PersistHistory::always;  // else remembered once asked for
    doc->install_sidecar(std::make_unique<Sidecar>(std::nullopt, queue, 0600, doc->sidecar_seams()));
    const NodeId root = doc->undo_.add_root(doc->root_meta(), 0, hash);
    doc->undo_.mark_saved(root, 0, hash);
    doc->first_session_id_ = root;
    doc->sidecar_->append_root(doc->undo_.meta(root), 0, hash);
    return doc;
}

void Document::install_sidecar(std::unique_ptr<Sidecar> sidecar) {
    sidecar_ = std::move(sidecar);
    Sidecar::Callbacks cb;
    cb.on_payload_written = [self = self_](NodeId node, std::uint32_t op, Which which, SidecarRef ref) {
        if (*self) (*self)->undo_.rebind_payload(node, op, which, ref);
    };
    cb.on_failure = [self = self_](std::string message) {
        if (*self) (*self)->status_message_ = std::move(message);
    };
    sidecar_->set_callbacks(std::move(cb));
}

void Document::detect_line_ending() {
    const std::uint64_t lf = tree_.find_lf_forward(0, options_.chunk_size);
    crlf_ = lf != PieceTree::npos && lf > 0 && tree_.byte_at(lf - 1) == std::byte{'\r'};
}

void Document::start_scanner(const std::shared_ptr<const MappedFile>& mapping, BufferIndex buffer) {
    const std::uint64_t cg = ++chunk_generation_;
    const std::uint64_t hg = ++hash_generation_;
    scan_mapping_ = mapping;
    auto self = self_;
    scanner_ = std::make_unique<LineScanner>(
        mapping, buffer, tree_.chunks(buffer), queue_,
        [self, cg](BufferIndex b, std::uint64_t off, std::uint64_t len, std::uint64_t lf) {
            if (*self && (*self)->chunk_generation_ == cg) (*self)->tree_.record_chunk_lines(b, off, len, lf);
        },
        [self, hg](Result<ContentHash> hash) {
            if (*self) (*self)->on_scan_done(hg, std::move(hash));
        });
    scanner_->start();
}

void Document::on_scan_done(std::uint64_t generation, Result<ContentHash> hash) {
    if (generation != hash_generation_) return;  // a retired scan
    if (!verifying_ && !root_pending_) {
        if (hash && !disk_hash_) disk_hash_ = *hash;
        return;
    }
    if (!hash) {
        // No hash, so nothing can be verified or written: keep the history in memory only.
        status_message_ = "history not saved: " + hash.error().message;
        if (verifying_) {
            undo_.reroot(candidate_, first_session_id_, reserved_root_, root_meta(), base_size_, ContentHash{});
            verifying_ = false;
        }
        root_pending_.reset();
        sidecar_->discard_deferred();
        install_sidecar(Sidecar::make_session_only(queue_));
        return;
    }
    if (!disk_hash_) disk_hash_ = *hash;
    if (verifying_) {
        verifying_ = false;
        if (!sidecar_->verify(*hash)) {
            // The file is not what the history says: the session's nodes move to a new root.
            NodeMeta meta = root_meta();
            undo_.reroot(candidate_, first_session_id_, reserved_root_, meta, base_size_, *hash);
            sidecar_->remap_deferred(candidate_, reserved_root_);
            sidecar_->append_root(undo_.meta(reserved_root_), base_size_, *hash);
        }
    } else {
        const NodeId root = *root_pending_;
        root_pending_.reset();
        undo_.set_base_hash(root, *hash);
        if (const auto base = undo_.root_base(root); base && !undo_.is_retired(root)) sidecar_->append_root(undo_.meta(root), base->size, *hash);
    }
    sidecar_->release_deferred(undo_);
}

// ---- listeners and status ------------------------------------------------------------

void Document::add_listener(DocumentListener* listener) { listeners_.push_back(listener); }

void Document::remove_listener(DocumentListener* listener) { std::erase(listeners_, listener); }

void Document::notify_before(const ChangeEvent& ev) {
    const auto copy = listeners_;
    for (DocumentListener* l : copy) l->before_change(ev);
}

void Document::notify_after(const ChangeEvent& ev) {
    const auto copy = listeners_;
    for (DocumentListener* l : copy) l->after_change(ev);
}

HistoryState Document::history_state() const {
    const SidecarState s = sidecar_->state();
    if (s == SidecarState::read_only) return HistoryState::read_only;
    if (!persist_) return HistoryState::session_only;
    if (verifying_) return HistoryState::verifying;
    switch (s) {
        case SidecarState::attached: return HistoryState::attached;
        case SidecarState::disabled: return HistoryState::disabled;
        default: return HistoryState::session_only;
    }
}

Status Document::set_persist_history(bool on, bool overwrite) {
    end_preview();
    if (!options_.history) return std::unexpected(make_error(ErrorCode::unsupported, "this document keeps no history"));
    if (sidecar_->state() == SidecarState::read_only)
        return std::unexpected(make_error(ErrorCode::unsupported, "history is open in another mod or newer than supported"));
    if (on == persist_) return {};
    close_group();
    if (!on) {
        sidecar_->set_paused(true);
        persist_ = false;
        return {};
    }
    if (path_.empty()) {  // untitled: the first save_as copies the history
        persist_ = true;
        return {};
    }
    const fs::path own = sidecar_path_for(path_);
    if (sidecar_->state() == SidecarState::attached && sidecar_->path() == own) {
        sidecar_->set_paused(false);  // writes the whole held history, creating the file
        persist_ = true;
        return {};
    }
    // A session-only sidecar, an unreadable file, or a sidecar left at an earlier path:
    // the whole history is written to a new sidecar for this path.
    if (unreadable_) {
        if (!overwrite) return std::unexpected(make_error(ErrorCode::format, own.filename().string() + " cannot be read"));
        std::error_code ec;
        fs::remove(own, ec);
        if (ec) return std::unexpected(make_error(ErrorCode::io, "cannot replace " + own.filename().string() + ": " + ec.message()));
    }
    auto copy = sidecar_->copy_to(path_, permission_bits(path_), undo_, tree_);
    if (!copy) return std::unexpected(copy.error());
    install_sidecar(std::move(*copy));
    // Nodes written from memory carry no save points; record them, in creation order, so
    // a reopen finds where the file on disk sits in the history.
    std::vector<NodeId> nodes(undo_.roots().begin(), undo_.roots().end());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const NodeInfo info = undo_.node_info(nodes[i]);
        nodes.insert(nodes.end(), info.children.begin(), info.children.end());
    }
    std::sort(nodes.begin(), nodes.end());
    for (const NodeId n : nodes) {
        if (const auto save = undo_.save_point(n)) sidecar_->append_save(n, save->size, save->hash);
    }
    if (!verifying_ && !root_pending_) sidecar_->release_deferred(undo_);
    unreadable_ = false;
    persist_ = true;
    return {};
}

std::string Document::take_status_message() { return std::exchange(status_message_, {}); }

bool Document::is_dirty() const {
    if (kept_external_) return true;
    if (new_file_) return tree_.size() > 0;
    return !undo_.is_at_saved();
}

// ---- editing ---------------------------------------------------------------------------

Result<Document::Resolved> Document::resolve(const Payload& payload) {
    Resolved r;
    r.length = payload.length();
    if (const auto* in = std::get_if<Payload::Inline>(&payload.form)) {
        r.bytes = in->bytes;
    } else if (const auto* pieces = std::get_if<Payload::Pieces>(&payload.form)) {
        r.run = pieces->run;
        r.is_run = true;
    } else {
        const SidecarRef ref = std::get<SidecarRef>(payload.form);
        auto where = sidecar_->payload_bytes(tree_, ref);
        if (!where) return std::unexpected(where.error());
        r.is_run = true;
        if (ref.length > 0) {
            const FrozenBytes f = tree_.frozen_bytes(where->first, where->second, ref.length);
            const auto lf = static_cast<std::uint64_t>(std::count(f.bytes.begin(), f.bytes.end(), std::byte{'\n'}));
            r.run.push_back({where->first, where->second, ref.length, lf});
        }
    }
    return r;
}

void Document::mutate(std::uint64_t offset, std::uint64_t remove_len, const Resolved& insert) {
    std::string small;
    ChangeEvent ev{offset, remove_len, insert.length, std::nullopt, version_ + 1};
    if (insert.length <= kInsertedSmallMax) {
        if (insert.is_run) {
            for (const Piece& p : insert.run) {
                const FrozenBytes f = tree_.frozen_bytes(p.buffer, p.offset, p.length);
                small.append(reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size());
            }
            ev.inserted_small = small;
        } else {
            ev.inserted_small = insert.bytes;
        }
    }
    notify_before(ev);
    if (remove_len > 0) tree_.erase(offset, remove_len);
    if (insert.is_run) {
        tree_.insert_run(offset, insert.run);
    } else if (!insert.bytes.empty()) {
        tree_.insert(offset, std::as_bytes(std::span(insert.bytes.data(), insert.bytes.size())));
    }
    ++version_;
    notify_after(ev);
}

void Document::apply(std::uint64_t offset, std::uint64_t remove_len, const InsertContent& insert, EditKind kind,
                     std::uint64_t cursor_before, std::uint64_t cursor_after) {
    end_preview();  // a preview never outlives a change to the document
    // Resolve the inserted content first, so that a failure changes nothing.
    std::optional<Resolved> ref_insert;
    std::uint64_t ins_len = 0;
    std::string run_small;
    std::optional<std::string_view> small;
    if (const auto* bytes = std::get_if<std::string_view>(&insert)) {
        ins_len = bytes->size();
        if (ins_len <= kInsertedSmallMax) small = *bytes;
    } else if (const auto* run = std::get_if<PieceRun>(&insert)) {
        ins_len = run_length(*run);
        if (ins_len <= kInsertedSmallMax) {
            for (const Piece& p : *run) {
                const FrozenBytes f = tree_.frozen_bytes(p.buffer, p.offset, p.length);
                run_small.append(reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size());
            }
            small = run_small;
        }
    } else {
        auto r = resolve(Payload::of_ref(std::get<SidecarRef>(insert)));
        if (!r) {
            status_message_ = r.error().message;
            return;
        }
        ref_insert = std::move(*r);
        ins_len = ref_insert->length;
    }
    if (remove_len == 0 && ins_len == 0) return;

    ChangeEvent ev{offset, remove_len, ins_len, small, version_ + 1};
    notify_before(ev);
    EditOp op;
    op.offset = offset;
    if (remove_len <= kInlineMax) {
        op.removed = Payload::of_bytes(tree_.read(offset, remove_len));
        if (remove_len > 0) tree_.erase(offset, remove_len);
    } else {
        op.removed = Payload::of_run(tree_.erase(offset, remove_len), remove_len);
    }
    if (const auto* bytes = std::get_if<std::string_view>(&insert)) {
        if (!bytes->empty()) tree_.insert(offset, std::as_bytes(std::span(bytes->data(), bytes->size())));
        op.inserted = ins_len <= kInlineMax ? Payload::of_bytes(std::string(*bytes))
                                            : Payload::of_run(tree_.pieces(offset, ins_len), ins_len);
    } else if (const auto* run = std::get_if<PieceRun>(&insert)) {
        tree_.insert_run(offset, *run);
        op.inserted = ins_len <= kInlineMax ? Payload::of_bytes(run_small) : Payload::of_run(*run, ins_len);
    } else if (ref_insert) {
        tree_.insert_run(offset, ref_insert->run);
        op.inserted = Payload::of_ref(std::get<SidecarRef>(insert));
    }
    ++version_;
    record(std::move(op), kind, cursor_before, cursor_after);
    notify_after(ev);
}

void Document::record(EditOp op, EditKind kind, std::uint64_t cursor_before, std::uint64_t cursor_after) {
    const std::int64_t t = now();
    const std::uint64_t removed = op.removed.length();
    const std::uint64_t inserted = op.inserted.length();
    const auto* typed = std::get_if<Payload::Inline>(&op.inserted.form);
    const bool starts_space = typed != nullptr && !typed->bytes.empty() && is_space(typed->bytes.front());
    const bool ends_space = typed != nullptr && !typed->bytes.empty() && is_space(typed->bytes.back());

    if (group_depth_ > 0) {  // an explicit group takes every op
        if (!explicit_has_node_) {
            undo_.commit({std::move(op)}, NodeMeta{0, 0, t, explicit_kind_, cursor_before, cursor_after});
            explicit_has_node_ = true;
        } else {
            undo_.amend_current(std::move(op), cursor_after);
        }
        return;
    }

    bool coalesce = group_open_ && kind == group_kind_ && t - last_op_ms_ < kCoalesceTimeoutMs;
    if (coalesce && kind == EditKind::typing) {
        // Adjacent insertion, and whitespace after non-whitespace starts a new word.
        coalesce = removed == 0 && op.offset == last_op_end_ && !(starts_space && !last_typed_space_);
    } else if (coalesce && kind == EditKind::delete_) {
        coalesce = inserted == 0 && (op.offset + removed == last_op_offset_ || op.offset == last_op_offset_);
    }
    const std::uint64_t offset = op.offset;
    if (coalesce) {
        undo_.amend_current(std::move(op), cursor_after);
    } else {
        close_group();
        undo_.commit({std::move(op)}, NodeMeta{0, 0, t, kind, cursor_before, cursor_after});
        group_open_ = true;
        group_kind_ = kind;
    }
    last_op_ms_ = t;
    last_op_offset_ = offset;
    last_op_end_ = offset + inserted;
    if (kind == EditKind::typing) last_typed_space_ = ends_space;
    // Only typing and deleting coalesce; every other kind closes its node at once.
    if (kind != EditKind::typing && kind != EditKind::delete_) close_group();
}

void Document::append_closed(NodeId node) { sidecar_->append_node(undo_.meta(node), to_node_ops(undo_.ops(node), tree_)); }

void Document::close_group() {
    if (!group_open_) return;
    group_open_ = false;
    if (const auto cur = undo_.current()) append_closed(*cur);
}

void Document::begin_group(EditKind kind) {
    if (group_depth_++ > 0) return;
    close_group();
    explicit_kind_ = kind;
    explicit_has_node_ = false;
}

void Document::end_group() {
    if (group_depth_ == 0) return;
    if (--group_depth_ > 0) return;
    if (explicit_has_node_) {
        explicit_has_node_ = false;
        if (const auto cur = undo_.current()) append_closed(*cur);
    }
}

// ---- history navigation ------------------------------------------------------------------

Status Document::apply_node(NodeId node, StepDirection direction) {
    const auto ops = undo_.ops(node);
    struct Step {
        std::uint64_t offset;
        std::uint64_t remove_len;
        Resolved insert;
    };
    std::vector<Step> steps;
    steps.reserve(ops.size());
    for (const EditOp& op : ops) {
        const bool undo = direction == StepDirection::undo;
        auto r = resolve(undo ? op.removed : op.inserted);
        if (!r) return std::unexpected(r.error());
        steps.push_back({op.offset, undo ? op.inserted.length() : op.removed.length(), std::move(*r)});
    }
    if (direction == StepDirection::undo) {
        for (auto it = steps.rbegin(); it != steps.rend(); ++it) mutate(it->offset, it->remove_len, it->insert);
    } else {
        for (const Step& s : steps) mutate(s.offset, s.remove_len, s.insert);
    }
    return {};
}

Result<std::uint64_t> Document::undo() {
    end_preview();
    close_group();
    const auto cur = undo_.current();
    if (!cur || undo_.meta(*cur).parent == kNoParent)
        return std::unexpected(make_error(ErrorCode::canceled, "nothing to undo"));
    if (verifying_ && *cur < first_session_id_)
        return std::unexpected(make_error(ErrorCode::unsupported, "history is still being verified"));
    if (auto s = apply_node(*cur, StepDirection::undo); !s) return std::unexpected(s.error());
    const NodeId parent = undo_.meta(*cur).parent;
    undo_.undo_step();
    sidecar_->append_position(parent, {{parent, *cur}});
    return undo_.meta(*cur).cursor_before;
}

Result<std::uint64_t> Document::redo() {
    end_preview();
    close_group();
    const auto cur = undo_.current();
    const auto child = cur ? undo_.node_info(*cur).preferred_child : std::nullopt;
    if (!child) return std::unexpected(make_error(ErrorCode::canceled, "nothing to redo"));
    if (verifying_ && *child < first_session_id_)
        return std::unexpected(make_error(ErrorCode::unsupported, "history is still being verified"));
    if (auto s = apply_node(*child, StepDirection::redo); !s) return std::unexpected(s.error());
    undo_.redo_step();
    sidecar_->append_position(*child, {{*cur, *child}});
    return undo_.meta(*child).cursor_after;
}

std::optional<BranchIndicator> Document::cycle_branch(int direction) {
    close_group();
    const auto r = undo_.cycle_branch(direction);
    if (const auto cur = undo_.current(); r && cur) {
        if (const auto child = undo_.node_info(*cur).preferred_child) sidecar_->append_position(*cur, {{*cur, *child}});
    }
    return r;
}

Result<std::uint64_t> Document::jump_to(NodeId target) {
    end_preview();
    close_group();
    const auto cur = undo_.current();
    if (!cur || !undo_.contains(target) || undo_.is_retired(target) || undo_.root_of(target) != undo_.root_of(*cur))
        return std::unexpected(make_error(ErrorCode::unsupported, "that history belongs to another version of the file"));
    const std::vector<PathStep> steps = undo_.path(*cur, target);
    if (verifying_ && std::any_of(steps.begin(), steps.end(), [&](const PathStep& s) { return s.node < first_session_id_; }))
        return std::unexpected(make_error(ErrorCode::unsupported, "history is still being verified"));
    std::vector<PreferredChange> changes;
    for (const PathStep& step : steps) {
        if (auto s = apply_node(step.node, step.direction); !s) {
            if (const auto at = undo_.current(); at && !changes.empty()) sidecar_->append_position(*at, changes);
            return std::unexpected(s.error());
        }
        changes.push_back(undo_.follow(step));
    }
    if (!changes.empty()) sidecar_->append_position(target, changes);
    return undo_.meta(target).parent == kNoParent ? 0 : undo_.meta(target).cursor_after;
}

Status Document::clear_history() {
    end_preview();
    if (is_dirty()) return std::unexpected(make_error(ErrorCode::internal, "clear history needs a clean document"));
    if (history_state() == HistoryState::read_only)
        return std::unexpected(make_error(ErrorCode::unsupported, "history is open in another mod or newer than supported"));
    close_group();
    // Delete first: if that fails, nothing in memory has changed either.
    if (auto s = sidecar_->clear(); !s) return s;
    verifying_ = false;  // the history it would have attached to is gone
    root_pending_.reset();
    undo_.reset();
    const std::uint64_t size = tree_.size();
    const NodeId root = undo_.add_root(root_meta(), size, disk_hash_.value_or(ContentHash{}));
    undo_.mark_saved(root, size, disk_hash_.value_or(ContentHash{}));
    first_session_id_ = root;
    if (disk_hash_) {
        sidecar_->append_root(undo_.meta(root), size, *disk_hash_);
        sidecar_->release_deferred(undo_);
    } else {
        root_pending_ = root;  // the running scan supplies the hash
    }
    return {};
}

// ---- pruning -----------------------------------------------------------------------------

Status Document::prune_refusal() const {
    auto refuse = [](const char* message) { return Status(std::unexpected(make_error(ErrorCode::unsupported, message))); };
    switch (history_state()) {
        case HistoryState::verifying: return refuse("history is still being verified");
        case HistoryState::read_only: return refuse("history is open in another mod");
        case HistoryState::session_only:
        case HistoryState::disabled: return refuse("history is kept in memory only: turn on Persist History (P) to trim it");
        case HistoryState::attached: break;
    }
    if (root_pending_) return refuse("history is still being verified");
    return {};
}

Result<PrunePreview> Document::prune_preview(std::optional<std::uint64_t> days) {
    close_group();  // the plan must see the open group as a node
    if (auto s = prune_refusal(); !s) return std::unexpected(s.error());
    const std::int64_t t = now();
    PrunePreview preview;
    if (const auto oldest = undo_.oldest_change()) {
        preview.oldest_days = static_cast<std::uint64_t>(std::max<std::int64_t>(0, t - *oldest) / kDayMs);
    }
    if (!days) return preview;
    preview.cutoff_ms = t - static_cast<std::int64_t>(*days) * kDayMs;
    const PrunePlan plan = undo_.plan_prune(preview.cutoff_ms);
    preview.remove_count = plan.removed_count;
    preview.keep_count = plan.kept.size();
    preview.removed_trees = plan.removed_trees;
    return preview;
}

NodeId Document::current_node() const {
    return *undo_.current();  // NOLINT(bugprone-unchecked-optional-access): every Document is created with a root
}

// The anchor's size and hash, from the piece tree walked silently to it and back: no
// notifications, no version bump, and neither `current` nor any preferred child moves.
Result<FileState> Document::silent_walk_content(NodeId anchor) {
    struct Applied {
        std::uint64_t offset;
        std::uint64_t inserted_len;
        PieceRun erased;
    };
    std::vector<Applied> applied;
    auto walk_back = [&] {
        // Reinsertion reads no payload, so this cannot fail.
        for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
            if (it->inserted_len > 0) tree_.erase(it->offset, it->inserted_len);
            if (!it->erased.empty()) tree_.insert_run(it->offset, it->erased);
        }
    };
    auto step = [&](std::uint64_t offset, std::uint64_t remove_len, const Payload& insert) -> Status {
        auto r = resolve(insert);  // a SidecarRef is read through the old sidecar mapping
        if (!r) return std::unexpected(r.error());
        PieceRun erased = remove_len > 0 ? tree_.erase(offset, remove_len) : PieceRun{};
        if (r->is_run) {
            if (!r->run.empty()) tree_.insert_run(offset, r->run);
        } else if (!r->bytes.empty()) {
            tree_.insert(offset, std::as_bytes(std::span(r->bytes.data(), r->bytes.size())));
        }
        applied.push_back({offset, r->length, std::move(erased)});
        return {};
    };
    for (const PathStep& s : undo_.path(current_node(), anchor)) {
        const auto ops = undo_.ops(s.node);
        Status st;
        if (s.direction == StepDirection::undo) {
            for (auto it = ops.rbegin(); st && it != ops.rend(); ++it) st = step(it->offset, it->inserted.length(), it->removed);
        } else {
            for (auto it = ops.begin(); st && it != ops.end(); ++it) st = step(it->offset, it->removed.length(), it->inserted);
        }
        if (!st) {
            walk_back();
            return std::unexpected(st.error());
        }
    }
    ContentHasher hasher;
    const std::uint64_t size = tree_.size();
    std::uint64_t done = 0;
    std::uint64_t reported = 0;
    tree_.read(0, size, [&](std::span<const std::byte> b) {
        hasher.update(b);
        done += b.size();
        if (options_.progress && (done - reported >= kProgressSlice || done == size)) {
            options_.progress(Progress{"hashing", done, size});
            reported = done;
        }
        return true;
    });
    const FileState state{size, hasher.finish()};
    walk_back();
    return state;
}

Status Document::prune_history(std::int64_t cutoff_ms) {
    end_preview();
    close_group();
    if (auto s = prune_refusal(); !s) return s;
    const PrunePlan plan = undo_.plan_prune(cutoff_ms);
    if (plan.removed_count == 0) return {};
    // The anchor's content is computed before the rewrite replaces the mapping that
    // SidecarRefs point into.
    FileState anchor;
    if (const auto base = plan.anchor_is_root ? undo_.root_base(plan.anchor) : std::nullopt) {
        anchor = *base;
    } else if (const auto save = undo_.save_point(plan.anchor)) {
        anchor = *save;
    } else {
        auto walked = silent_walk_content(plan.anchor);
        if (!walked) return std::unexpected(walked.error());
        anchor = *walked;
    }
    auto rebinds = sidecar_->rewrite(undo_, plan, anchor.size, anchor.hash, tree_);
    if (!rebinds) return std::unexpected(rebinds.error());
    undo_.apply_prune(plan, anchor.size, anchor.hash);
    for (const Rebind& r : *rebinds) undo_.rebind_payload(r.node, r.op_index, r.which, r.ref);
    return {};
}

// ---- previews ----------------------------------------------------------------------------

Status Document::preview_step(std::uint64_t offset, std::uint64_t remove_len, const Payload& insert) {
    auto r = resolve(insert);
    if (!r) return std::unexpected(r.error());
    PieceRun erased = remove_len > 0 ? tree_.erase(offset, remove_len) : PieceRun{};
    if (r->is_run) {
        if (!r->run.empty()) tree_.insert_run(offset, r->run);
    } else if (!r->bytes.empty()) {
        tree_.insert(offset, std::as_bytes(std::span(r->bytes.data(), r->bytes.size())));
    }
    preview_steps_.push_back({offset, r->length, std::move(erased)});
    return {};
}

void Document::end_preview() {
    // Reinsertion reads no payload, so this cannot fail.
    for (auto it = preview_steps_.rbegin(); it != preview_steps_.rend(); ++it) {
        if (it->inserted_len > 0) tree_.erase(it->offset, it->inserted_len);
        if (!it->erased.empty()) tree_.insert_run(it->offset, it->erased);
    }
    preview_steps_.clear();
    previewing_ = false;
}

Result<std::vector<PreviewMark>> Document::begin_preview(NodeId node) {
    end_preview();
    close_group();
    previewing_ = true;
    for (const PathStep& s : undo_.path(*undo_.current(), node)) {
        const auto ops = undo_.ops(s.node);
        Status st;
        if (s.direction == StepDirection::undo) {
            for (auto it = ops.rbegin(); st && it != ops.rend(); ++it) st = preview_step(it->offset, it->inserted.length(), it->removed);
        } else {
            for (auto it = ops.begin(); st && it != ops.end(); ++it) st = preview_step(it->offset, it->removed.length(), it->inserted);
        }
        if (!st) {
            end_preview();
            return std::unexpected(st.error());
        }
    }
    // The node's own change, carried through its later ops into offsets of its text. A
    // removal is a point (where the text was) until it is spliced back in below.
    struct Mark {
        std::uint64_t start, len;
        bool removed;
        const Payload* text;
    };
    std::vector<Mark> marks;
    const auto ops = undo_.ops(node);
    for (const EditOp& op : ops) {
        const std::uint64_t off = op.offset;
        const std::uint64_t rem = op.removed.length();
        const std::uint64_t ins = op.inserted.length();
        auto map = [&](std::uint64_t x) { return x <= off ? x : x >= off + rem ? x + ins - rem : off; };
        for (Mark& m : marks) {
            const std::uint64_t s = map(m.start);
            const std::uint64_t e = m.removed ? s : map(m.start + m.len);
            m.start = s;
            if (!m.removed) m.len = e - s;
        }
        if (rem > 0) marks.push_back({off, rem, true, &op.removed});
        if (ins > 0) marks.push_back({off, ins, false, nullptr});
    }
    std::erase_if(marks, [](const Mark& m) { return !m.removed && m.len == 0; });
    // Splice the removed text back in, last first, each before any text inserted at its place.
    std::vector<std::size_t> order(marks.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return marks[a].start > marks[b].start; });
    for (const std::size_t i : order) {
        if (!marks[i].removed) continue;
        const std::uint64_t at = marks[i].start;
        if (auto st = preview_step(at, 0, *marks[i].text); !st) {
            end_preview();
            return std::unexpected(st.error());
        }
        for (std::size_t k = 0; k < marks.size(); ++k) {
            if (k != i && marks[k].start >= at && (!marks[k].removed || marks[k].start > at)) marks[k].start += marks[i].len;
        }
    }
    std::vector<PreviewMark> out;
    out.reserve(marks.size());
    for (const Mark& m : marks) out.push_back({m.start, m.start + m.len, m.removed});
    std::sort(out.begin(), out.end(), [](const PreviewMark& a, const PreviewMark& b) { return a.start < b.start; });
    return out;
}

// ---- saving ------------------------------------------------------------------------------

void Document::materialize_for_overwrite(const fs::path& target, Clipboard* clipboard) {
    const auto id = stat_path(target);
    if (!id) return;
    std::unordered_set<BufferIndex> hit;
    std::vector<const void*> maps;
    for (const auto& [m, idx] : mappings_) {
        if (m && same_file(m->identity(), *id)) {
            hit.insert(idx);
            maps.push_back(m.get());
        }
    }
    if (hit.empty()) return;

    // The scanner of that file must stop before its bytes change (or shrink: reading a
    // mapping past the end of its file faults). A pending base hash is computed now.
    if (scan_mapping_ && same_file(scan_mapping_->identity(), *id)) {
        std::optional<ContentHash> pending;
        if (verifying_ || root_pending_) {
            ContentHasher hasher;
            if (scan_mapping_->size() > 0) hasher.update({scan_mapping_->data(), static_cast<std::size_t>(scan_mapping_->size())});
            pending = hasher.finish();
        }
        scanner_.reset();
        ++chunk_generation_;  // its posted line counts must not touch the old mapping
        if (pending) on_scan_done(++hash_generation_, *pending);
    }

    auto copy_run = [&](PieceRun& run) {
        if (std::none_of(run.begin(), run.end(), [&](const Piece& p) { return hit.contains(p.buffer); })) return false;
        PieceRun out;
        for (const Piece& p : run) {
            if (!hit.contains(p.buffer)) {
                out.push_back(p);
                continue;
            }
            const PieceRun stored = tree_.store(tree_.frozen_bytes(p.buffer, p.offset, p.length).bytes);
            out.insert(out.end(), stored.begin(), stored.end());
        }
        run = std::move(out);
        return true;
    };
    undo_.visit_pieces([&](NodeId, std::uint32_t, Which, PieceRun& run) { return copy_run(run); });
    for (const void* m : maps) sidecar_->detach_views(m);
    copy_live_text(hit);

    if (clipboard == nullptr || clipboard->get() == nullptr) return;
    const ClipContent& c = *clipboard->get();
    PieceRun run;
    if (c.source == id_) {
        run = c.run;
        if (!copy_run(run)) return;
    } else {
        for (const FrozenBytes& v : c.views) {
            const PieceRun stored = tree_.store(v.bytes);
            run.insert(run.end(), stored.begin(), stored.end());
        }
    }
    std::vector<FrozenBytes> views;
    for (const Piece& p : run) views.push_back(tree_.frozen_bytes(p.buffer, p.offset, p.length));
    clipboard->rebind(std::move(run), std::move(views), id_);
}

// The text itself is copied off the file too, last to first so offsets stay valid: if the
// copy over the file fails partway, or the file cannot be mapped again afterwards, the
// document must not be left reading a mix of old and new bytes.
void Document::copy_live_text(const std::unordered_set<BufferIndex>& hit) {
    std::vector<std::pair<std::uint64_t, Piece>> moved;  // document offset, piece
    std::uint64_t at = 0;
    for (const Piece& p : tree_.pieces(0, tree_.size())) {
        if (hit.contains(p.buffer)) moved.emplace_back(at, p);
        at += p.length;
    }
    for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
        const auto& [offset, p] = *it;
        const PieceRun stored = tree_.store(tree_.frozen_bytes(p.buffer, p.offset, p.length).bytes);
        (void)tree_.erase(offset, p.length);
        tree_.insert_run(offset, stored);
    }
}

Result<Document::Written> Document::write_content(const fs::path& target, SaveMode mode, Clipboard* clipboard) {
    const std::uint64_t size = tree_.size();
    const std::uint64_t chunk = options_.chunk_size;
    ContentHasher hasher;
    std::vector<ChunkLines> chunks;
    const ContentProducer produce = [&](const ByteSink& sink) -> Status {
        Status st;
        std::uint64_t pos = 0;
        std::uint64_t chunk_start = 0;
        std::uint64_t chunk_lf = 0;
        tree_.read(0, size, [&](std::span<const std::byte> s) {
            st = sink(s);
            if (!st) return false;
            hasher.update(s);
            // Line feeds per chunk, at the boundaries the rebased tree will use.
            while (!s.empty()) {
                const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(s.size(), chunk_start + chunk - pos));
                chunk_lf += static_cast<std::uint64_t>(std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(take), std::byte{'\n'}));
                pos += take;
                s = s.subspan(take);
                if (pos == chunk_start + chunk) {
                    chunks.push_back({chunk_start, chunk, chunk_lf});
                    chunk_start = pos;
                    chunk_lf = 0;
                }
            }
            return true;
        });
        if (!st) return st;
        if (pos > chunk_start) chunks.push_back({chunk_start, pos - chunk_start, chunk_lf});
        return {};
    };

    if (mode == SaveMode::atomic) {
        if (auto s = write_atomically(target, produce, target); !s) return std::unexpected(s.error());
    } else {
        (void)sidecar_->flush(kFlushTimeoutMs);  // payloads become SidecarRefs where they can
        materialize_for_overwrite(target, clipboard);
        if (auto s = write_in_place(target, produce); !s) return std::unexpected(s.error());
    }
    auto mapping = MappedFile::open(target);
    if (!mapping) return std::unexpected(mapping.error());
    return Written{std::move(*mapping), std::move(chunks), hasher.finish()};
}

Status Document::finish_save(const fs::path& target, Written written) {
    (void)sidecar_->flush(kFlushTimeoutMs);  // a timeout still saves; old mappings stay alive
    const BufferIndex idx = tree_.rebase(written.mapping, written.chunks);
    mappings_.emplace_back(written.mapping, idx);
    ++chunk_generation_;  // the old scan's line counts no longer apply
    if (!verifying_ && !root_pending_) scanner_.reset();  // otherwise it still owes the base hash
    const std::uint64_t size = written.mapping->size();
    const NodeId cur = current_node();
    undo_.mark_saved(cur, size, written.hash);
    sidecar_->append_save(cur, size, written.hash);
    disk_hash_ = written.hash;
    known_identity_ = written.mapping->identity();
    kept_external_ = false;
    new_file_ = false;
    path_ = target;
    const auto copy = listeners_;
    for (DocumentListener* l : copy) l->saved();
    return {};
}

Status Document::save(SaveMode mode, Clipboard* clipboard) {
    end_preview();
    if (is_untitled()) return std::unexpected(make_error(ErrorCode::unsupported, "an untitled document is saved with Save As"));
    close_group();
    auto written = write_content(path_, mode, clipboard);
    if (!written) return std::unexpected(written.error());
    return finish_save(path_, std::move(*written));
}

Status Document::save_as(const fs::path& path, SaveMode mode, Clipboard* clipboard) {
    end_preview();
    close_group();
    auto real = resolve_real_path(path);
    if (!real) return std::unexpected(real.error());
    if (!path_.empty()) {
        if (*real == path_) return save(mode, clipboard);
        const auto a = stat_path(*real);
        const auto b = stat_path(path_);
        if (a && b && same_file(*a, *b)) return save(mode, clipboard);  // a hard link to the same file
    }
    auto written = write_content(*real, mode, clipboard);
    if (!written) return std::unexpected(written.error());

    // Without persistence the history stays in memory: the current sidecar keeps holding it.
    if (persist_) {
        auto copy = sidecar_->copy_to(*real, permission_bits(*real), undo_, tree_);
        if (copy) {
            install_sidecar(std::move(*copy));
            if (sidecar_->state() == SidecarState::read_only) status_message_ = "history is open in another mod";
        } else {
            status_message_ = "history not copied: " + copy.error().message;
            install_sidecar(Sidecar::make_session_only(queue_));
            persist_ = false;
        }
    }
    if (!verifying_ && !root_pending_) sidecar_->release_deferred(undo_);
    return finish_save(*real, std::move(*written));
}

// ---- the file on disk ----------------------------------------------------------------------

ExternalChange Document::check_external_change() const {
    if (path_.empty() || new_file_) return {};
    const auto id = stat_path(path_);
    if (!id) {
        if (id.error().code != ErrorCode::not_found || !known_identity_) return {};
        return {ExternalChange::deleted, false};
    }
    if (!known_identity_) return {ExternalChange::modified, true};  // reappeared after a deletion
    if (*id == *known_identity_) return {};
    return {ExternalChange::modified, !same_file(*id, *known_identity_)};
}

void Document::keep_in_memory() {
    const auto id = stat_path(path_);
    known_identity_ = id ? std::optional(*id) : std::nullopt;
    kept_external_ = true;
}

Status Document::reload() {
    if (is_untitled()) return std::unexpected(make_error(ErrorCode::unsupported, "an untitled document has no file"));
    close_group();
    auto mapping = MappedFile::open(path_);
    if (!mapping) return std::unexpected(mapping.error());
    scanner_.reset();
    if (verifying_ || root_pending_) sidecar_->discard_deferred();  // an abandoned verification
    verifying_ = false;
    root_pending_.reset();
    const BufferIndex idx = tree_.rebase(*mapping, {});
    mappings_.emplace_back(*mapping, idx);
    ++version_;
    detect_line_ending();
    known_identity_ = (*mapping)->identity();
    kept_external_ = false;
    new_file_ = false;
    disk_hash_.reset();
    base_size_ = (*mapping)->size();
    const NodeId root = undo_.add_root(root_meta(), base_size_, ContentHash{});
    undo_.mark_saved(root, base_size_, ContentHash{});
    first_session_id_ = root;
    root_pending_ = root;
    sidecar_->defer();
    if (base_size_ > 0) {
        start_scanner(*mapping, idx);
    } else {
        on_scan_done(hash_generation_, empty_hash());
    }
    const auto copy = listeners_;
    for (DocumentListener* l : copy) l->reloaded();
    return {};
}

}  // namespace mod
