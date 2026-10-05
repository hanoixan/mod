#include "edit/sidecar.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iterator>
#include <map>

#include "platform/fs.hpp"
#include "util/log.hpp"

namespace fs = std::filesystem;

namespace mod {
namespace {

constexpr char kMagic[8] = {'M', 'O', 'D', 'H', 'I', 'S', 'T', '\0'};
constexpr std::uint16_t kFormatVersion = 1;
constexpr std::uint16_t kHashSha256 = 1;
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kFrameHead = 9;  // u64 body_length, u8 type
constexpr std::size_t kFrameTail = 4;  // u32 crc32
constexpr std::size_t kStreamSlice = 1 << 20;
constexpr std::int64_t kPositionInterval = 1000;
constexpr int kRewriteDrainMs = 5000;
constexpr std::chrono::milliseconds kProgressInterval{100};  // flush's wait slice

enum RecordType : std::uint8_t { kRoot = 1, kPayload = 2, kNode = 3, kSave = 4, kPosition = 5 };
enum PayloadForm : std::uint8_t { kInlineForm = 0, kRefForm = 1 };

// Explicit little-endian stores: padding and host byte order never reach the file.
struct Encoder {
    std::string out;

    void u8(std::uint8_t v) { out.push_back(static_cast<char>(v)); }
    void u16(std::uint16_t v) { put(v, 2); }
    void u32(std::uint32_t v) { put(v, 4); }
    void u64(std::uint64_t v) { put(v, 8); }
    void i64(std::int64_t v) { put(static_cast<std::uint64_t>(v), 8); }
    void raw(std::string_view s) { out.append(s); }
    void hash(const ContentHash& h) { out.append(reinterpret_cast<const char*>(h.data()), h.size()); }

private:
    void put(std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) out.push_back(static_cast<char>(v >> (8 * i) & 0xFF));
    }
};

struct Decoder {
    const std::byte* p;
    std::size_t size;
    std::size_t pos = 0;
    bool ok = true;

    bool need(std::size_t n) {
        if (size - pos < n) ok = false;
        return ok;
    }
    std::uint64_t get(int n) {
        if (!need(static_cast<std::size_t>(n))) return 0;
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(p[pos + static_cast<std::size_t>(i)])) << (8 * i);
        pos += static_cast<std::size_t>(n);
        return v;
    }
    std::uint8_t u8() { return static_cast<std::uint8_t>(get(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(get(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(get(4)); }
    std::uint64_t u64() { return get(8); }
    std::int64_t i64() { return static_cast<std::int64_t>(get(8)); }
    ContentHash hash() {
        ContentHash h{};
        if (need(h.size())) {
            std::memcpy(h.data(), p + pos, h.size());
            pos += h.size();
        }
        return h;
    }
    std::string bytes(std::size_t n) {
        if (!need(n)) return {};
        std::string s(reinterpret_cast<const char*>(p + pos), n);
        pos += n;
        return s;
    }
};

std::span<const std::byte> as_bytes(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

std::string header_bytes(std::int64_t created_ms) {
    Encoder e;
    e.raw(std::string_view(kMagic, sizeof kMagic));
    e.u16(kFormatVersion);
    e.u16(kHashSha256);
    e.u32(0);
    e.i64(created_ms);
    e.u64(0);
    return std::move(e.out);
}

std::string frame(std::uint8_t type, const std::string& body) {
    Encoder e;
    e.u64(body.size());
    e.u8(type);
    e.raw(body);
    const std::byte t{type};
    e.u32(crc32(as_bytes(body), crc32(std::span(&t, 1))));
    return std::move(e.out);
}

// A destination for records: the writer's file, or a copy's content producer.
struct Emitter {
    std::function<bool(std::span<const std::byte>)> put;
    std::uint64_t pos = 0;

    bool bytes(std::span<const std::byte> b) {
        if (!put(b)) return false;
        pos += b.size();
        return true;
    }
    bool str(const std::string& s) { return bytes(as_bytes(s)); }
};

struct OpRebind {
    std::uint32_t op_index;
    Which which;
    SidecarRef ref;
};

// Streams a PAYLOAD record in slices of at most 1 MiB, with a running CRC.
std::optional<SidecarRef> emit_payload(Emitter& out, const PayloadView& v) {
    Encoder head;
    head.u64(v.length);
    head.u8(kPayload);
    if (!out.str(head.out)) return std::nullopt;
    const SidecarRef ref{out.pos, v.length};
    const std::byte t{kPayload};
    std::uint32_t crc = crc32(std::span(&t, 1));
    auto stream = [&](std::span<const std::byte> s) {
        while (!s.empty()) {
            const auto part = s.first(std::min(s.size(), kStreamSlice));
            crc = crc32(part, crc);
            if (!out.bytes(part)) return false;
            s = s.subspan(part.size());
        }
        return true;
    };
    if (const auto* str = std::get_if<std::string>(&v.form)) {
        if (!stream(as_bytes(*str))) return std::nullopt;
    } else if (const auto* views = std::get_if<std::vector<FrozenBytes>>(&v.form)) {
        for (const FrozenBytes& f : *views) {
            if (!stream(f.bytes)) return std::nullopt;
        }
    }
    Encoder tail;
    tail.u32(crc);
    if (!out.str(tail.out)) return std::nullopt;
    return ref;
}

std::string gather(const PayloadView& v) {
    if (const auto* s = std::get_if<std::string>(&v.form)) return *s;
    std::string out;
    if (const auto* views = std::get_if<std::vector<FrozenBytes>>(&v.form)) {
        for (const FrozenBytes& f : *views) out.append(reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size());
    }
    return out;
}

// PAYLOAD records for every payload longer than kInlineMax, then the NODE record. With
// `copy_refs_from` (a rewrite), SidecarRefs into that old mapping are copied into new
// PAYLOAD records whatever their length, so that each one can be rebound.
bool emit_node(Emitter& out, const NodeMeta& meta, const std::vector<NodeOp>& ops, std::vector<OpRebind>& rebinds,
               const MappedFile* copy_refs_from = nullptr) {
    Encoder body;
    body.u64(meta.id);
    body.u64(meta.parent);
    body.i64(meta.time_unix_ms);
    body.u8(static_cast<std::uint8_t>(meta.kind));
    body.u64(meta.cursor_before);
    body.u64(meta.cursor_after);
    body.u32(static_cast<std::uint32_t>(ops.size()));
    for (std::uint32_t i = 0; i < ops.size(); ++i) {
        body.u64(ops[i].offset);
        for (Which w : {Which::removed, Which::inserted}) {
            const PayloadView& v = w == Which::removed ? ops[i].removed : ops[i].inserted;
            const auto* ref = std::get_if<SidecarRef>(&v.form);
            if (ref != nullptr && copy_refs_from != nullptr) {
                PayloadView copy;
                copy.length = ref->length;
                copy.form = std::vector<FrozenBytes>{
                    {std::span(copy_refs_from->data() + ref->file_offset, static_cast<std::size_t>(ref->length)), nullptr}};
                const auto written = emit_payload(out, copy);
                if (!written) return false;
                body.u8(kRefForm);
                body.u64(written->file_offset);
                body.u64(written->length);
                rebinds.push_back({i, w, *written});
            } else if (ref != nullptr) {
                body.u8(kRefForm);
                body.u64(ref->file_offset);
                body.u64(ref->length);
            } else if (v.length <= kInlineMax) {
                body.u8(kInlineForm);
                body.u32(static_cast<std::uint32_t>(v.length));
                body.raw(gather(v));
            } else {
                const auto written = emit_payload(out, v);
                if (!written) return false;
                body.u8(kRefForm);
                body.u64(written->file_offset);
                body.u64(written->length);
                rebinds.push_back({i, w, *written});
            }
        }
    }
    return out.str(frame(kNode, body.out));
}

std::string root_body(const NodeMeta& meta, std::uint64_t size, const ContentHash& hash) {
    Encoder e;
    e.u64(meta.id);
    e.u64(size);
    e.hash(hash);
    e.i64(meta.time_unix_ms);
    return std::move(e.out);
}

std::string save_body(NodeId node, std::uint64_t size, const ContentHash& hash, std::int64_t time_ms) {
    Encoder e;
    e.u64(node);
    e.u64(size);
    e.hash(hash);
    e.i64(time_ms);
    return std::move(e.out);
}

std::string position_body(NodeId current, const std::vector<PreferredChange>& changes) {
    Encoder e;
    e.u64(current);
    e.u32(static_cast<std::uint32_t>(changes.size()));
    for (const PreferredChange& c : changes) {
        e.u64(c.parent);
        e.u64(c.child);
    }
    return std::move(e.out);
}

PayloadView view_of(const Payload& p, const PieceTree& text) {
    PayloadView v;
    v.length = p.length();
    if (const auto* in = std::get_if<Payload::Inline>(&p.form)) {
        v.form = in->bytes;
    } else if (const auto* pieces = std::get_if<Payload::Pieces>(&p.form)) {
        std::vector<FrozenBytes> views;
        views.reserve(pieces->run.size());
        for (const Piece& piece : pieces->run) views.push_back(text.frozen_bytes(piece.buffer, piece.offset, piece.length));
        v.form = std::move(views);
    } else {
        v.form = std::get<SidecarRef>(p.form);
    }
    return v;
}

bool is_sidecar_header(const fs::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[sizeof kMagic] = {};
    const ssize_t n = ::pread(fd, buf, sizeof buf, 0);
    ::close(fd);
    return n == static_cast<ssize_t>(sizeof buf) && std::memcmp(buf, kMagic, sizeof buf) == 0;
}

}  // namespace

fs::path sidecar_path_for(const fs::path& document_path) {
    fs::path p = document_path;
    p += ".history";
    return p;
}

std::vector<NodeOp> to_node_ops(std::span<const EditOp> ops, const PieceTree& text) {
    std::vector<NodeOp> out;
    out.reserve(ops.size());
    for (const EditOp& op : ops) out.push_back({op.offset, view_of(op.removed, text), view_of(op.inserted, text)});
    return out;
}

Sidecar::Sidecar(std::optional<fs::path> doc_path, EventQueue& queue, std::uint32_t file_mode, SidecarSeams seams)
    : queue_(queue),
      file_mode_(file_mode),
      seams_(std::move(seams)),
      token_(std::make_shared<Token>()),
      state_(doc_path ? SidecarState::attached : SidecarState::pathless) {
    if (doc_path) path_ = sidecar_path_for(*doc_path);
    token_->self = this;
    writer_ = std::jthread([this](std::stop_token stop) { writer_loop(std::move(stop)); });
}

Sidecar::~Sidecar() {
    if (!deferred_) emit_pending_position();
    token_->alive = false;
    writer_.request_stop();
    {
        std::lock_guard lock(mutex_);
        paused_ = false;
    }
    work_cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (fd_ >= 0) ::close(fd_);  // releases the lock
}

std::unique_ptr<Sidecar> Sidecar::make_session_only(EventQueue& queue) {
    auto side = std::make_unique<Sidecar>(std::nullopt, queue, 0600);
    side->state_ = SidecarState::session_only;
    return side;
}

void Sidecar::set_callbacks(Callbacks callbacks) { token_->callbacks = std::move(callbacks); }

std::int64_t Sidecar::now_ms() const {
    if (seams_.now_ms) return seams_.now_ms();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ---- load -----------------------------------------------------------------------

Result<LoadOutcome> Sidecar::open(UndoTree& tree, std::uint64_t file_size) {
    const std::int64_t start = now_ms();
    bool read_file = false;
    auto outcome = load(tree, file_size, read_file);
    if (outcome && read_file) outcome->load_ms = std::max<std::int64_t>(0, now_ms() - start);
    return outcome;
}

Result<LoadOutcome> Sidecar::load(UndoTree& tree, std::uint64_t file_size, bool& read_file) {
    deferred_ = true;
    if (!path_) return LoadOutcome{LoadOutcome::no_history, std::nullopt};
    const fs::path& sp = *path_;
    struct stat st {};
    if (::stat(sp.c_str(), &st) != 0) {
        if (errno == ENOENT) return LoadOutcome{LoadOutcome::no_history, std::nullopt};  // created lazily
        return std::unexpected(from_errno("stat " + sp.string()));
    }
    int fd = ::open(sp.c_str(), O_RDWR | O_APPEND | O_CLOEXEC);
    bool locked = false;
    if (fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0) locked = true;
    if (!locked && fd >= 0) {
        ::close(fd);
        fd = -1;
    }
    auto map = MappedFile::open(sp);
    if (!map) {
        if (fd >= 0) ::close(fd);
        return std::unexpected(map.error());
    }
    read_file = true;
    had_file_ = true;
    const MappedFile& m = **map;
    const std::size_t size = static_cast<std::size_t>(m.size());
    if (size < kHeaderSize || std::memcmp(m.data(), kMagic, sizeof kMagic) != 0) {
        if (fd >= 0) ::close(fd);
        state_ = SidecarState::disabled;
        disabled_ = Disabled::foreign;
        return LoadOutcome{LoadOutcome::disabled, std::nullopt};
    }
    Decoder head{m.data(), size, sizeof kMagic};
    const std::uint16_t version = head.u16();
    const std::uint16_t algorithm = head.u16();
    if (version > kFormatVersion) {
        if (fd >= 0) ::close(fd);
        state_ = SidecarState::read_only;
        newer_version_ = true;
        file_exists_ = true;
        return LoadOutcome{LoadOutcome::read_only, std::nullopt};
    }
    if (version == 0 || algorithm != kHashSha256) {
        if (fd >= 0) ::close(fd);
        state_ = SidecarState::disabled;
        disabled_ = Disabled::foreign;
        return LoadOutcome{LoadOutcome::disabled, std::nullopt};
    }

    // One forward pass. Stop at the first truncated or corrupt record.
    std::optional<Candidate> candidate;
    std::size_t pos = kHeaderSize;
    while (size - pos >= kFrameHead + kFrameTail) {
        Decoder fr{m.data(), size, pos};
        const std::uint64_t body_len = fr.u64();
        const std::uint8_t type = fr.u8();
        if (body_len > size - pos - kFrameHead - kFrameTail) break;  // runs past the end: truncated tail
        const std::byte* body = m.data() + pos + kFrameHead;
        const auto blen = static_cast<std::size_t>(body_len);
        const std::byte t{type};
        const std::uint32_t crc = crc32(std::span(body, blen), crc32(std::span(&t, 1)));
        Decoder tail{m.data(), size, pos + kFrameHead + blen};
        if (tail.u32() != crc) break;
        const std::size_t record_start = pos;
        pos += kFrameHead + blen + kFrameTail;

        Decoder d{body, blen};
        switch (type) {
            case kRoot: {
                NodeMeta meta;
                meta.id = d.u64();
                const std::uint64_t base_size = d.u64();
                const ContentHash h = d.hash();
                meta.time_unix_ms = d.i64();
                if (!d.ok || d.pos != blen || !tree.load_root(meta, base_size, h)) {
                    log(LogLevel::warn, "sidecar: skipping invalid ROOT at {}", record_start);
                    break;
                }
                in_file_.insert(meta.id);
                if (base_size == file_size) candidate = Candidate{meta.id, h};
                break;
            }
            case kNode: {
                NodeMeta meta;
                meta.id = d.u64();
                meta.parent = d.u64();
                meta.time_unix_ms = d.i64();
                const std::uint8_t kind = d.u8();
                meta.kind = kind <= static_cast<std::uint8_t>(EditKind::other) ? static_cast<EditKind>(kind) : EditKind::other;
                meta.cursor_before = d.u64();
                meta.cursor_after = d.u64();
                const std::uint32_t count = d.u32();
                std::vector<EditOp> ops;
                for (std::uint32_t i = 0; i < count && d.ok; ++i) {
                    EditOp op;
                    op.offset = d.u64();
                    for (Payload* p : {&op.removed, &op.inserted}) {
                        const std::uint8_t form = d.u8();
                        if (form == kInlineForm) {
                            *p = Payload::of_bytes(d.bytes(d.u32()));
                        } else if (form == kRefForm) {
                            const SidecarRef ref{d.u64(), d.u64()};
                            if (ref.file_offset > record_start || ref.length > record_start - ref.file_offset) d.ok = false;
                            *p = Payload::of_ref(ref);
                        } else {
                            d.ok = false;
                        }
                    }
                    ops.push_back(std::move(op));
                }
                if (!d.ok || d.pos != blen || !tree.load_node(meta, std::move(ops))) {
                    log(LogLevel::warn, "sidecar: skipping invalid NODE at {}", record_start);
                    break;
                }
                in_file_.insert(meta.id);
                break;
            }
            case kSave: {
                const NodeId node = d.u64();
                const std::uint64_t saved_size = d.u64();
                const ContentHash h = d.hash();
                d.i64();
                if (!d.ok || d.pos != blen || !tree.contains(node)) break;
                tree.mark_saved(node, saved_size, h);
                if (saved_size == file_size) candidate = Candidate{node, h};
                break;
            }
            case kPosition: {
                const NodeId current = d.u64();
                const std::uint32_t n = d.u32();
                std::vector<PreferredChange> changes;
                for (std::uint32_t i = 0; i < n && d.ok; ++i) {
                    const NodeId parent = d.u64();
                    const NodeId child = d.u64();
                    changes.push_back({parent, child});
                }
                if (!d.ok || d.pos != blen) break;
                (void)current;  // informational; the start node is chosen by size and hash
                tree.set_position(std::nullopt, changes);
                break;
            }
            default: break;  // PAYLOAD bodies are referenced, not parsed; unknown types are skipped
        }
    }

    payload_map_ = std::move(*map);
    candidate_ = candidate;
    file_known_ = true;
    std::lock_guard lock(mutex_);
    file_exists_ = true;
    need_header_ = false;
    file_size_ = pos;
    good_size_ = pos;
    if (!locked) {
        state_ = SidecarState::read_only;
        return LoadOutcome{LoadOutcome::read_only, candidate ? std::optional(candidate->node) : std::nullopt};
    }
    fd_ = fd;
    if (pos < size) truncate_to_ = pos;
    state_ = SidecarState::attached;
    if (!candidate) return LoadOutcome{LoadOutcome::no_history, std::nullopt};
    return LoadOutcome{LoadOutcome::provisional, candidate->node};
}

std::optional<NodeId> Sidecar::verify(const ContentHash& hash) const {
    if (candidate_ && candidate_->hash == hash) return candidate_->node;
    return std::nullopt;
}

// ---- appends --------------------------------------------------------------------

void Sidecar::defer() { deferred_ = true; }

void Sidecar::hold(Job job) {
    if (std::holds_alternative<RootRec>(job)) {
        // ROOT records go before every record held back with them.
        const auto it = std::find_if(held_.begin(), held_.end(), [](const Job& j) { return !std::holds_alternative<RootRec>(j); });
        held_.insert(it, std::move(job));
    } else {
        held_.push_back(std::move(job));
    }
}

void Sidecar::queue_held() {
    {
        std::lock_guard lock(mutex_);
        for (Job& j : held_) queue_jobs_.push_back(std::move(j));
    }
    held_.clear();
    work_cv_.notify_one();
}

void Sidecar::enqueue(Job job) {
    if (state_ == SidecarState::pathless || (state_ == SidecarState::attached && (deferred_ || persist_paused_))) {
        hold(std::move(job));
        return;
    }
    if (state_ != SidecarState::attached) return;  // read-only, disabled or session-only: never append
    // The file is created lazily by the first NODE: viewing a file never creates one.
    if (!file_known_) {
        const bool node = std::holds_alternative<NodeRec>(job);
        hold(std::move(job));
        if (!node) return;
        file_known_ = true;
        queue_held();
        return;
    }
    {
        std::lock_guard lock(mutex_);
        queue_jobs_.push_back(std::move(job));
    }
    work_cv_.notify_one();
}

void Sidecar::emit_pending_position() {
    if (!pending_position_) return;
    PositionRec rec = std::move(*pending_position_);
    pending_position_.reset();
    last_position_ms_ = now_ms();
    enqueue(std::move(rec));
}

void Sidecar::release_deferred(const UndoTree& tree) {
    if (state_ == SidecarState::pathless) return;
    deferred_ = false;
    for (Job& job : held_) {
        if (auto* node = std::get_if<NodeRec>(&job); node && tree.contains(node->meta.id) && !tree.is_retired(node->meta.id))
            node->meta.parent = tree.meta(node->meta.id).parent;
    }
    if (state_ != SidecarState::attached) {
        held_.clear();
    } else if (persist_paused_) {
        // Persistence is off: the records stay held, with their final parents.
    } else if (file_known_ || std::any_of(held_.begin(), held_.end(), [](const Job& j) { return std::holds_alternative<NodeRec>(j); })) {
        file_known_ = true;
        queue_held();
    }
    emit_pending_position();
}

void Sidecar::set_paused(bool paused) {
    if (persist_paused_ == paused) return;
    persist_paused_ = paused;
    if (paused || state_ != SidecarState::attached || deferred_) return;
    // Everything held while paused goes out now, creating the file if need be.
    if (file_known_ || std::any_of(held_.begin(), held_.end(), [](const Job& j) { return std::holds_alternative<NodeRec>(j); })) {
        file_known_ = true;
        queue_held();
    }
    emit_pending_position();
}

void Sidecar::remap_deferred(NodeId from, NodeId to) {
    auto remap_position = [&](PositionRec& p) {
        if (p.current == from) p.current = to;
        for (PreferredChange& c : p.changes) {
            if (c.parent == from) c.parent = to;
        }
    };
    for (Job& job : held_) {
        if (auto* save = std::get_if<SaveRec>(&job); save && save->node == from) save->node = to;
        if (auto* pos = std::get_if<PositionRec>(&job)) remap_position(*pos);
    }
    if (pending_position_) remap_position(*pending_position_);
}

void Sidecar::discard_deferred() {
    held_.clear();
    pending_position_.reset();
}

void Sidecar::append_root(const NodeMeta& meta, std::uint64_t base_size, const ContentHash& base_hash) {
    emit_pending_position();
    enqueue(RootRec{meta, base_size, base_hash});
}

void Sidecar::append_node(const NodeMeta& meta, std::vector<NodeOp> ops) {
    emit_pending_position();
    enqueue(NodeRec{meta, std::move(ops)});
}

void Sidecar::append_save(NodeId node, std::uint64_t size, const ContentHash& hash) {
    emit_pending_position();
    enqueue(SaveRec{node, size, hash, now_ms()});
}

void Sidecar::append_position(NodeId current, const std::vector<PreferredChange>& preferred_changes) {
    if (!pending_position_) pending_position_ = PositionRec{};
    pending_position_->current = current;
    for (const PreferredChange& c : preferred_changes) {
        auto& changes = pending_position_->changes;
        const auto it = std::find_if(changes.begin(), changes.end(), [&](const PreferredChange& x) { return x.parent == c.parent; });
        if (it != changes.end()) {
            it->child = c.child;
        } else {
            changes.push_back(c);
        }
    }
    if (now_ms() - last_position_ms_ >= kPositionInterval) emit_pending_position();
}

// ---- writer thread -----------------------------------------------------------------

void Sidecar::writer_loop(std::stop_token stop) {
    std::unique_lock lock(mutex_);
    for (;;) {
        work_cv_.wait(lock, [&] { return stop.stop_requested() || (!paused_ && !queue_jobs_.empty()); });
        if (queue_jobs_.empty()) {
            if (stop.stop_requested()) return;
            continue;
        }
        if (paused_) continue;
        Job job = std::move(queue_jobs_.front());
        queue_jobs_.pop_front();
        busy_ = true;
        lock.unlock();
        if (!failed_) write_job(job);
        lock.lock();
        busy_ = false;
        idle_cv_.notify_all();
    }
}

void Sidecar::pause_writer(std::unique_lock<std::mutex>& lock) {
    paused_ = true;
    idle_cv_.wait(lock, [&] { return !busy_; });
}

void Sidecar::resume_writer(std::unique_lock<std::mutex>& lock) {
    paused_ = false;
    lock.unlock();
    work_cv_.notify_all();
    lock.lock();
}

void Sidecar::fail(const std::string& message, SidecarState state) {
    failed_ = true;
    log(LogLevel::warn, "sidecar: {}", message);
    queue_.post([token = token_, message, state] {
        if (!token->alive) return;
        Sidecar& self = *token->self;
        if (self.state_ == SidecarState::attached) {
            self.state_ = state;
            if (state == SidecarState::disabled) self.disabled_ = Disabled::write_failed;
        }
        if (token->callbacks.on_failure) token->callbacks.on_failure(message);
    });
}

bool Sidecar::write_bytes(std::span<const std::byte> bytes) {
    for (;;) {
        const long n = seams_.write ? seams_.write(fd_, bytes.data(), bytes.size()) : ::write(fd_, bytes.data(), bytes.size());
        if (n < 0 && errno == EINTR) continue;
        if (n == static_cast<long>(bytes.size())) {
            file_size_ += bytes.size();
            bytes_written_.fetch_add(bytes.size(), std::memory_order_relaxed);
            return true;
        }
        // A short write is never retried: that could interleave garbage.
        fail(n < 0 ? std::string("history write failed: ") + std::strerror(errno) : "history write failed: short write",
             SidecarState::disabled);
        return false;
    }
}

bool Sidecar::ensure_file() {
    if (failed_) return false;
    if (fd_ >= 0) {
        if (truncate_to_) {
            if (::ftruncate(fd_, static_cast<off_t>(*truncate_to_)) != 0) {
                fail(std::string("cannot cut the damaged tail of the history: ") + std::strerror(errno), SidecarState::disabled);
                return false;
            }
            file_size_ = good_size_ = *truncate_to_;
            truncate_to_.reset();
        }
        return true;
    }
    if (!path_) return false;
    const int fd = ::open(path_->c_str(), O_RDWR | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) {
        fail(std::string("history not saved: cannot create ") + path_->filename().string() + ": " + std::strerror(errno),
             SidecarState::session_only);
        return false;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0 || ::fchmod(fd, static_cast<mode_t>(file_mode_ & 07777)) != 0) {
        ::close(fd);
        fail("history not saved: cannot lock " + path_->filename().string(), SidecarState::session_only);
        return false;
    }
    fd_ = fd;
    file_exists_ = true;
    file_size_ = 0;
    const std::string header = header_bytes(now_ms());
    if (!write_bytes(as_bytes(header))) return false;
    need_header_ = false;
    good_size_ = file_size_;
    return true;
}

void Sidecar::sync_file() {
    const int rc = seams_.sync ? seams_.sync(fd_)
#if defined(__APPLE__)
                               : ::fsync(fd_);
#else
                               : ::fdatasync(fd_);
#endif
    if (rc != 0) log(LogLevel::warn, "sidecar: fsync failed: {}", std::strerror(errno));
}

bool Sidecar::write_job(Job& job) {
    if (!ensure_file()) return false;
    Emitter out{[this](std::span<const std::byte> b) { return write_bytes(b); }, file_size_};
    bool ok = true;
    if (auto* root = std::get_if<RootRec>(&job)) {
        ok = out.str(frame(kRoot, root_body(root->meta, root->size, root->hash)));
        if (ok) {
            std::lock_guard lock(mutex_);
            in_file_.insert(root->meta.id);
        }
    } else if (auto* node = std::get_if<NodeRec>(&job)) {
        std::vector<OpRebind> rebinds;
        ok = emit_node(out, node->meta, node->ops, rebinds);
        if (ok) {
            {
                std::lock_guard lock(mutex_);
                in_file_.insert(node->meta.id);
            }
            if (!rebinds.empty()) {
                queue_.post([token = token_, id = node->meta.id, rebinds = std::move(rebinds)] {
                    if (!token->alive || !token->callbacks.on_payload_written) return;
                    for (const OpRebind& r : rebinds) token->callbacks.on_payload_written(id, r.op_index, r.which, r.ref);
                });
            }
        }
    } else if (auto* save = std::get_if<SaveRec>(&job)) {
        ok = out.str(frame(kSave, save_body(save->node, save->size, save->hash, save->time_ms)));
        // Durability: once per document save, right after its SAVE record.
        if (ok) sync_file();
    } else if (auto* pos = std::get_if<PositionRec>(&job)) {
        ok = out.str(frame(kPosition, position_body(pos->current, pos->changes)));
    } else if (std::holds_alternative<SyncRec>(job)) {
        sync_file();
    }
    if (ok) good_size_ = file_size_;
    return ok;
}

// ---- payloads, copies, clearing ---------------------------------------------------

Result<std::pair<BufferIndex, std::uint64_t>> Sidecar::payload_bytes(PieceTree& text, SidecarRef ref) {
    const std::uint64_t need = ref.file_offset + ref.length;
    if (!payload_map_ || payload_map_->size() < need) {
        if (!path_) return std::unexpected(make_error(ErrorCode::io, "history payload unavailable"));
        auto map = MappedFile::open(*path_);
        if (!map) return std::unexpected(map.error());
        if ((*map)->size() < need) return std::unexpected(make_error(ErrorCode::io, "history file is shorter than expected"));
        payload_map_ = std::move(*map);
        payload_tree_ = nullptr;
    }
    if (payload_tree_ != &text) {
        payload_buffer_ = text.add_buffer(payload_map_);
        payload_tree_ = &text;
    }
    return std::pair{payload_buffer_, ref.file_offset};
}

void Sidecar::detach_views(const void* mapping) {
    auto detach = [&](Job& job) {
        auto* node = std::get_if<NodeRec>(&job);
        if (node == nullptr) return;
        for (NodeOp& op : node->ops) {
            for (PayloadView* v : {&op.removed, &op.inserted}) {
                const auto* views = std::get_if<std::vector<FrozenBytes>>(&v->form);
                if (views == nullptr) continue;
                const bool hit = std::any_of(views->begin(), views->end(), [&](const FrozenBytes& f) { return f.keep_alive.get() == mapping; });
                if (hit) v->form = gather(*v);
            }
        }
    };
    std::unique_lock lock(mutex_);
    pause_writer(lock);
    for (Job& job : queue_jobs_) detach(job);
    for (Job& job : held_) detach(job);
    resume_writer(lock);
}

std::uint64_t Sidecar::queued_payload_bytes() const {
    std::uint64_t total = 0;
    for (const Job& job : queue_jobs_) {
        if (const auto* node = std::get_if<NodeRec>(&job)) {
            for (const NodeOp& op : node->ops) total += op.removed.length + op.inserted.length;
        }
    }
    return total;
}

Status Sidecar::flush(int timeout_ms) {
    if (!deferred_) emit_pending_position();
    bool done = false;
    {
        std::unique_lock lock(mutex_);
        auto idle = [&] { return queue_jobs_.empty() && !busy_; };
        if (!seams_.progress || idle()) {
            done = idle_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), idle);
        } else {
            // Waits in slices so the meter can be redrawn while large payloads are written.
            const std::optional<std::uint64_t> total = queued_payload_bytes();
            const std::uint64_t start = bytes_written_.load(std::memory_order_relaxed);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            for (;;) {
                const auto slice = std::min(deadline, std::chrono::steady_clock::now() + kProgressInterval);
                done = idle_cv_.wait_until(lock, slice, idle);
                const std::uint64_t written = bytes_written_.load(std::memory_order_relaxed) - start;
                lock.unlock();
                seams_.progress(Progress{"writing history", written, total});
                lock.lock();
                if (done || std::chrono::steady_clock::now() >= deadline) break;
            }
        }
    }
    queue_.drain();
    if (!done) return std::unexpected(make_error(ErrorCode::canceled, "history is still being written"));
    return {};
}

Status Sidecar::clear() {
    if (state_ == SidecarState::read_only)
        return std::unexpected(make_error(ErrorCode::unsupported, "history is open in another mod or newer than supported"));
    std::unique_lock lock(mutex_);
    pause_writer(lock);
    const bool ours = state_ == SidecarState::attached || (state_ == SidecarState::disabled && disabled_ == Disabled::write_failed);
    if (ours && file_exists_ && path_) {
        // Unlink while still holding the lock, then close, which releases it.
        if (::unlink(path_->c_str()) != 0 && errno != ENOENT) {
            Error e = from_errno("delete " + path_->string());
            resume_writer(lock);
            return std::unexpected(std::move(e));
        }
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        file_exists_ = false;
        need_header_ = true;
        file_size_ = good_size_ = 0;
        truncate_to_.reset();
        failed_ = false;
        if (state_ == SidecarState::disabled) {
            state_ = SidecarState::attached;
            disabled_ = Disabled::none;
        }
    }
    queue_jobs_.clear();
    in_file_.clear();
    resume_writer(lock);
    lock.unlock();
    held_.clear();
    pending_position_.reset();
    candidate_.reset();
    file_known_ = false;
    payload_map_.reset();  // pieces that still use it keep it alive
    payload_tree_ = nullptr;
    deferred_ = true;
    return {};
}

Result<std::unique_ptr<Sidecar>> Sidecar::copy_to(const fs::path& new_doc_path, std::uint32_t file_mode,
                                                  const UndoTree& tree, const PieceTree& text) {
    const fs::path target = sidecar_path_for(new_doc_path);
    if (!deferred_) emit_pending_position();
    std::unique_lock lock(mutex_);
    idle_cv_.wait(lock, [&] { return queue_jobs_.empty() && !busy_; });
    pause_writer(lock);
    struct Resume {
        Sidecar& s;
        std::unique_lock<std::mutex>& l;
        ~Resume() { s.resume_writer(l); }
    } resume{*this, lock};

    // A file that is already there.
    struct stat st {};
    if (::lstat(target.c_str(), &st) == 0) {
        if (!is_sidecar_header(target))
            return std::unexpected(make_error(ErrorCode::format, target.filename().string() + " is not a history file"));
        const int probe = ::open(target.c_str(), O_RDONLY | O_CLOEXEC);
        const bool held = probe >= 0 && ::flock(probe, LOCK_EX | LOCK_NB) != 0;
        if (probe >= 0) ::close(probe);
        if (held) {
            auto side = std::make_unique<Sidecar>(new_doc_path, queue_, file_mode, seams_);
            side->state_ = SidecarState::read_only;
            return side;
        }
    } else if (errno != ENOENT) {
        return std::unexpected(from_errno("stat " + target.string()));
    }

    // What this sidecar's file holds, copied byte for byte so every SidecarRef stays valid.
    const bool copy_file = path_ && file_exists_ && disabled_ != Disabled::foreign && state_ != SidecarState::session_only;
    const std::uint64_t copy_len = newer_version_ ? static_cast<std::uint64_t>(-1) : good_size_;

    // Live nodes that never reached the file and are not held back.
    std::unordered_set<NodeId> held_ids;
    for (const Job& j : held_) {
        if (const auto* r = std::get_if<RootRec>(&j)) held_ids.insert(r->meta.id);
        if (const auto* n = std::get_if<NodeRec>(&j)) held_ids.insert(n->meta.id);
    }
    std::vector<NodeId> missing;
    if (!newer_version_) {
        std::vector<NodeId> stack(tree.roots().begin(), tree.roots().end());
        while (!stack.empty()) {
            const NodeId n = stack.back();
            stack.pop_back();
            if (!in_file_.contains(n) && !held_ids.contains(n)) missing.push_back(n);
            const NodeInfo info = tree.node_info(n);
            stack.insert(stack.end(), info.children.begin(), info.children.end());
        }
        std::sort(missing.begin(), missing.end());
    }

    std::uint64_t written = 0;
    std::optional<std::uint64_t> copy_total;
    if (copy_file && !newer_version_) copy_total = copy_len;
    const ContentProducer produce = [&](const ByteSink& sink) -> Status {
        Status result;
        Emitter out{[&](std::span<const std::byte> b) {
                        result = sink(b);
                        return result.has_value();
                    },
                    0};
        if (copy_file) {
            const int src = ::open(path_->c_str(), O_RDONLY | O_CLOEXEC);
            if (src < 0) return std::unexpected(from_errno("open " + path_->string()));
            std::vector<std::byte> buf(kStreamSlice);
            Status s;
            while (out.pos < copy_len) {
                const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(buf.size(), copy_len - out.pos));
                const ssize_t n = ::pread(src, buf.data(), want, static_cast<off_t>(out.pos));
                if (n < 0 && errno == EINTR) continue;
                if (n < 0) {
                    s = std::unexpected(from_errno("read " + path_->string()));
                    break;
                }
                if (n == 0) break;
                if (!out.bytes(std::span(buf.data(), static_cast<std::size_t>(n)))) {
                    s = result;
                    break;
                }
                if (seams_.progress) seams_.progress(Progress{"copying history", out.pos, copy_total});
            }
            ::close(src);
            if (!s) return s;
        } else {
            if (!out.str(header_bytes(now_ms()))) return result;
        }
        for (const NodeId id : missing) {
            const NodeMeta& meta = tree.meta(id);
            if (const auto base = tree.root_base(id)) {
                if (!out.str(frame(kRoot, root_body(meta, base->size, base->hash)))) return result;
            } else {
                std::vector<OpRebind> ignored;
                if (!emit_node(out, meta, to_node_ops(tree.ops(id), text), ignored)) return result;
            }
        }
        written = out.pos;
        if (seams_.progress) seams_.progress(Progress{"copying history", written, copy_total});
        return {};
    };
    if (auto s = write_atomically(target, produce, std::nullopt, file_mode); !s) return std::unexpected(s.error());

    auto side = std::make_unique<Sidecar>(new_doc_path, queue_, file_mode, seams_);
    side->newer_version_ = newer_version_;
    side->candidate_ = candidate_;
    side->deferred_ = deferred_ || state_ == SidecarState::pathless;
    side->held_ = std::move(held_);
    held_.clear();
    side->pending_position_ = std::move(pending_position_);
    pending_position_.reset();
    side->last_position_ms_ = last_position_ms_;
    side->file_known_ = true;
    {
        std::lock_guard side_lock(side->mutex_);
        side->file_exists_ = true;
        side->need_header_ = false;
        side->file_size_ = side->good_size_ = written;
        side->in_file_ = in_file_;
        side->in_file_.insert(missing.begin(), missing.end());
        if (newer_version_) {
            side->state_ = SidecarState::read_only;
        } else {
            const int fd = ::open(target.c_str(), O_RDWR | O_APPEND | O_CLOEXEC);
            if (fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0) {
                side->fd_ = fd;
                side->state_ = SidecarState::attached;
            } else {
                if (fd >= 0) ::close(fd);
                side->state_ = SidecarState::read_only;  // another mod took it meanwhile
            }
        }
    }
    if (!side->deferred_) side->release_deferred(tree);
    return side;
}

bool Sidecar::wait_idle(int timeout_ms) {
    std::unique_lock lock(mutex_);
    return idle_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return queue_jobs_.empty() && !busy_; });
}

Result<std::vector<Rebind>> Sidecar::rewrite(const UndoTree& tree, const PrunePlan& plan, std::uint64_t base_size,
                                             const ContentHash& base_hash, const PieceTree& text) {
    auto refuse = [&]() -> Result<std::vector<Rebind>> {
        return std::unexpected(make_error(ErrorCode::unsupported, state_ == SidecarState::read_only
                                                                      ? "history is open in another mod"
                                                                      : "history is not being saved"));
    };
    if (state_ != SidecarState::attached || !path_) return refuse();

    // 1. Drain the writer and run its rebind closures, as `flush` does.
    const bool drained = wait_idle(kRewriteDrainMs);
    queue_.drain();
    if (!drained) return std::unexpected(make_error(ErrorCode::canceled, "history is still being written"));
    if (state_ != SidecarState::attached) return refuse();  // a write failed meanwhile

    std::unique_lock lock(mutex_);
    pause_writer(lock);
    struct Resume {
        Sidecar& s;
        std::unique_lock<std::mutex>& l;
        ~Resume() { s.resume_writer(l); }
    } resume{*this, lock};

    // The old file: its creation time, and the mapping SidecarRefs point into.
    std::int64_t created_ms = now_ms();
    std::shared_ptr<const MappedFile> old_map;
    if (file_exists_) {
        auto map = MappedFile::open(*path_);
        if (!map) return std::unexpected(map.error());
        old_map = std::move(*map);
        if (old_map->size() >= kHeaderSize) {
            Decoder head{old_map->data(), static_cast<std::size_t>(old_map->size()), 16};
            created_ms = head.i64();
        }
    }
    const std::uint64_t old_size = old_map ? old_map->size() : 0;
    bool refs_ok = true;
    std::uint64_t payload_total = 0;
    tree.for_each_pruned(plan, [&](const PrunedNode& pn) {
        for (const auto run : pn.op_runs) {
            for (const EditOp& op : run) {
                payload_total += op.removed.length() + op.inserted.length();
                for (const Payload* p : {&op.removed, &op.inserted}) {
                    const auto* ref = std::get_if<SidecarRef>(&p->form);
                    if (ref != nullptr && (ref->file_offset > old_size || ref->length > old_size - ref->file_offset))
                        refs_ok = false;
                }
            }
        }
        return refs_ok;
    });
    if (!refs_ok) return std::unexpected(make_error(ErrorCode::io, "history file is shorter than expected"));

    // 2. The pruned file, streamed into a temp file and renamed over the old one.
    std::vector<Rebind> rebinds;
    std::vector<NodeId> written_ids;
    std::uint64_t written = 0;
    const ContentProducer produce = [&](const ByteSink& sink) -> Status {
        rebinds.clear();
        written_ids.clear();
        Status result;
        std::uint64_t done = 0;
        std::uint64_t reported = 0;
        Emitter out{[&](std::span<const std::byte> b) {
                        result = sink(b);
                        done += b.size();
                        if (seams_.progress && done - reported >= kStreamSlice) {
                            seams_.progress(Progress{"pruning history", done, payload_total});
                            reported = done;
                        }
                        return result.has_value();
                    },
                    0};
        if (!out.str(header_bytes(created_ms))) return result;
        const NodeMeta root{plan.anchor, kNoParent, plan.root_time_ms, EditKind::other, 0, 0};
        if (!out.str(frame(kRoot, root_body(root, base_size, base_hash)))) return result;
        written_ids.push_back(plan.anchor);
        bool ok = true;
        tree.for_each_pruned(plan, [&](const PrunedNode& pn) {
            std::vector<NodeOp> ops;
            for (const auto run : pn.op_runs) {
                std::vector<NodeOp> part = to_node_ops(run, text);
                std::move(part.begin(), part.end(), std::back_inserter(ops));
            }
            std::vector<OpRebind> node_rebinds;
            ok = emit_node(out, pn.meta, ops, node_rebinds, old_map.get());
            for (const OpRebind& r : node_rebinds) rebinds.push_back({pn.meta.id, r.op_index, r.which, r.ref});
            written_ids.push_back(pn.meta.id);
            return ok;
        });
        if (!ok) return result;
        // The kept save points, in the order their SAVE records were first written.
        std::vector<NodeId> saves;
        for (const NodeId id : written_ids) {
            if (tree.save_order(id) > 0) saves.push_back(id);
        }
        std::sort(saves.begin(), saves.end(), [&](NodeId a, NodeId b) { return tree.save_order(a) < tree.save_order(b); });
        const std::int64_t t = now_ms();
        for (const NodeId id : saves) {
            const FileState st = *tree.save_point(id);
            if (!out.str(frame(kSave, save_body(id, st.size, st.hash, t)))) return result;
        }
        if (!out.str(frame(kPosition, position_body(*tree.current(), tree.pruned_preferred(plan))))) return result;
        written = out.pos;
        if (seams_.progress) seams_.progress(Progress{"pruning history", done, payload_total});
        return {};
    };
    if (auto st = write_atomically(*path_, produce, std::nullopt, file_mode_); !st) return std::unexpected(st.error());

    // 3. Lock the new file, then release the old one.
    const int fd = ::open(path_->c_str(), O_RDWR | O_APPEND | O_CLOEXEC);
    const bool locked = fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0;
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    if (locked) {
        fd_ = fd;
    } else if (fd >= 0) {
        ::close(fd);
    }
    file_exists_ = true;
    need_header_ = false;
    file_size_ = good_size_ = written;
    truncate_to_.reset();
    in_file_ = std::unordered_set<NodeId>(written_ids.begin(), written_ids.end());
    held_.clear();
    pending_position_.reset();
    candidate_.reset();
    file_known_ = true;
    payload_map_.reset();  // pieces that still use it keep it alive
    payload_tree_ = nullptr;
    if (locked) {
        queue_jobs_.push_back(SyncRec{});  // 4. one fsync of the new file, then appends go on
    } else {
        state_ = SidecarState::read_only;  // another mod took the new file first
        if (token_->callbacks.on_failure) token_->callbacks.on_failure("history is open in another mod");
    }
    return rebinds;
}

}  // namespace mod
