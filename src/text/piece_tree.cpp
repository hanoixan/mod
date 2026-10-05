#include "text/piece_tree.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace mod {
namespace {

constexpr std::uint64_t kAddBlockSize = 64 * 1024;
constexpr std::uint64_t kBackwardWindow = 64 * 1024;

std::uint64_t count_lf_bytes(std::span<const std::byte> s) {
    return static_cast<std::uint64_t>(std::count(s.begin(), s.end(), std::byte{'\n'}));
}

}  // namespace

PieceTree::PieceTree(std::shared_ptr<const MappedFile> original, std::uint64_t chunk_size)
    : chunk_size_(chunk_size == 0 ? kDefaultChunkSize : chunk_size), rng_state_(0x6d6f645f74726565ull) {
    files_.push_back(nullptr);
    seeded_.emplace_back();
    chunk_cache_.emplace_back();
    if (original) {
        std::vector<Chunk> chunks = make_chunks(*original);
        const BufferIndex idx = add_buffer(std::move(original));
        seeded_[idx] = std::move(chunks);
        std::vector<Piece> pieces;
        pieces.reserve(seeded_[idx].size());
        for (const Chunk& c : seeded_[idx]) pieces.push_back({idx, c.offset, c.length, kUnknownLines});
        seed(idx, pieces);
    }
}

// ---- storage -------------------------------------------------------------------

const std::byte* PieceTree::piece_data(const Piece& p) const {
    if (p.buffer == 0) {
        const auto it = std::upper_bound(block_start_.begin(), block_start_.end(), p.offset);
        const auto i = static_cast<std::size_t>(it - block_start_.begin()) - 1;
        return blocks_[i]->data.get() + (p.offset - block_start_[i]);
    }
    return files_[p.buffer]->data() + p.offset;
}

std::uint64_t PieceTree::count_lf(const Piece& p, std::uint64_t from, std::uint64_t len) const {
    if (len == 0) return 0;
    return count_lf_bytes({piece_data(p) + from, static_cast<std::size_t>(len)});
}

BufferIndex PieceTree::add_buffer(std::shared_ptr<const MappedFile> file) {
    files_.push_back(std::move(file));
    seeded_.emplace_back();
    chunk_cache_.emplace_back();
    return static_cast<BufferIndex>(files_.size() - 1);
}

void PieceTree::release_buffer(BufferIndex buffer) {
    if (buffer == 0 || buffer >= files_.size()) return;
    files_[buffer].reset();
    seeded_[buffer] = {};
    chunk_cache_[buffer] = {};
}

std::vector<Chunk> PieceTree::make_chunks(const MappedFile& file) const {
    // Hard boundaries: seeding reads no file bytes, so a line may straddle two chunks.
    std::vector<Chunk> out;
    const std::uint64_t size = file.size();
    out.reserve(static_cast<std::size_t>((size + chunk_size_ - 1) / chunk_size_));
    for (std::uint64_t start = 0; start < size; start += chunk_size_)
        out.push_back({start, std::min(chunk_size_, size - start)});
    return out;
}

std::vector<Chunk> PieceTree::chunks(BufferIndex buffer) const {
    return buffer < seeded_.size() ? seeded_[buffer] : std::vector<Chunk>{};
}

// ---- nodes and the unknown-count index -----------------------------------------

// splitmix64: deterministic priorities keep tests reproducible.
std::uint32_t PieceTree::next_priority() {
    std::uint64_t z = (rng_state_ += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
}

std::uint32_t PieceTree::new_node(const Piece& p) {
    std::uint32_t n;
    if (!free_.empty()) {
        n = free_.back();
        free_.pop_back();
        nodes_[n] = Node{};
    } else {
        n = static_cast<std::uint32_t>(nodes_.size());
        nodes_.emplace_back();
    }
    nodes_[n].prio = next_priority();
    nodes_[n].piece.lf_count = 0;  // nothing to remove from the index yet
    set_piece(n, p);
    pull(n);
    return n;
}

void PieceTree::index_remove(std::uint32_t n) {
    const Piece& p = nodes_[n].piece;
    if (p.lf_count != kUnknownLines) return;
    auto [lo, hi] = unknown_index_.equal_range({p.buffer, p.offset});
    for (auto it = lo; it != hi; ++it) {
        if (it->second == n) {
            unknown_index_.erase(it);
            return;
        }
    }
}

// Sets a node's piece (aggregates are the caller's job). An unknown count that lies
// inside an already-recorded chunk is resolved at once, so it never stays unknown.
void PieceTree::set_piece(std::uint32_t n, const Piece& p) {
    index_remove(n);
    Piece piece = p;
    if (piece.lf_count == kUnknownLines && piece.buffer < chunk_cache_.size()) {
        const auto& cache = chunk_cache_[piece.buffer];
        auto it = cache.upper_bound(piece.offset);
        if (it != cache.begin()) {
            --it;
            const bool inside = piece.offset + piece.length <= it->first + it->second.length;
            if (inside && it->second.lf_count != kUnknownLines) {
                piece.lf_count = (it->first == piece.offset && it->second.length == piece.length)
                                     ? it->second.lf_count
                                     : count_lf(piece, 0, piece.length);
            }
        }
    }
    nodes_[n].piece = piece;
    if (piece.lf_count == kUnknownLines) unknown_index_.insert({{piece.buffer, piece.offset}, n});
}

void PieceTree::free_subtree(std::uint32_t n) {
    std::vector<std::uint32_t> stack;
    if (n != NIL) stack.push_back(n);
    while (!stack.empty()) {
        const std::uint32_t x = stack.back();
        stack.pop_back();
        if (nodes_[x].left != NIL) stack.push_back(nodes_[x].left);
        if (nodes_[x].right != NIL) stack.push_back(nodes_[x].right);
        index_remove(x);
        nodes_[x].piece.lf_count = 0;
        free_.push_back(x);
    }
}

void PieceTree::pull(std::uint32_t n) {
    Node& x = nodes_[n];
    x.bytes = x.piece.length;
    const bool unknown = x.piece.lf_count == kUnknownLines;
    x.lf_known = unknown ? 0 : x.piece.lf_count;
    x.unknown = unknown ? 1 : 0;
    for (std::uint32_t c : {x.left, x.right}) {
        if (c == NIL) continue;
        Node& child = nodes_[c];
        child.parent = n;
        x.bytes += child.bytes;
        x.lf_known += child.lf_known;
        x.unknown += child.unknown;
    }
}

void PieceTree::pull_to_root(std::uint32_t n) {
    for (; n != NIL; n = nodes_[n].parent) pull(n);
}

void PieceTree::set_root(std::uint32_t n) {
    root_ = n;
    if (n != NIL) nodes_[n].parent = NIL;
}

// ---- treap primitives ----------------------------------------------------------

std::uint32_t PieceTree::merge(std::uint32_t a, std::uint32_t b) {
    if (a == NIL) return b;
    if (b == NIL) return a;
    if (nodes_[a].prio > nodes_[b].prio) {
        nodes_[a].right = merge(nodes_[a].right, b);
        pull(a);
        return a;
    }
    nodes_[b].left = merge(a, nodes_[b].left);
    pull(b);
    return b;
}

// Splits so that the first tree holds exactly the first `k` bytes, cutting a piece if needed.
std::pair<std::uint32_t, std::uint32_t> PieceTree::split(std::uint32_t n, std::uint64_t k) {
    if (n == NIL) return {NIL, NIL};
    const std::uint32_t left = nodes_[n].left;
    const std::uint64_t lb = left == NIL ? 0 : nodes_[left].bytes;
    const std::uint64_t plen = nodes_[n].piece.length;
    if (k <= lb) {
        auto [l, r] = split(left, k);
        nodes_[n].left = r;
        pull(n);
        return {l, n};
    }
    if (k >= lb + plen) {
        auto [l, r] = split(nodes_[n].right, k - lb - plen);
        nodes_[n].right = l;
        pull(n);
        return {n, r};
    }
    const Piece p = nodes_[n].piece;
    const std::uint64_t cut = k - lb;
    Piece head{p.buffer, p.offset, cut, kUnknownLines};
    Piece tail{p.buffer, p.offset + cut, p.length - cut, kUnknownLines};
    if (p.lf_count != kUnknownLines) {  // count the smaller side
        if (cut <= p.length - cut) {
            head.lf_count = count_lf(p, 0, cut);
            tail.lf_count = p.lf_count - head.lf_count;
        } else {
            tail.lf_count = count_lf(p, cut, p.length - cut);
            head.lf_count = p.lf_count - tail.lf_count;
        }
    }
    set_piece(n, head);
    const std::uint32_t m = new_node(tail);
    // The tail tree is hung where n was, so it may not outrank n's ancestors: a random
    // priority no higher than n's. (An equal one would make every fragment of a piece tie,
    // and deletions alone would then build a chain.)
    nodes_[m].prio = static_cast<std::uint32_t>(next_priority() % (std::uint64_t{nodes_[n].prio} + 1));
    const std::uint32_t right = nodes_[n].right;
    nodes_[n].right = NIL;
    pull(n);
    return {n, merge(m, right)};
}

std::uint32_t PieceTree::build(std::span<const Piece> pieces) {
    std::uint32_t t = NIL;
    for (const Piece& p : pieces) {
        if (p.length == 0) continue;
        assert(p.buffer < files_.size());
        t = merge(t, new_node(p));
    }
    return t;
}

void PieceTree::seed(BufferIndex buffer, std::span<const Piece> pieces) {
    (void)buffer;
    set_root(build(pieces));
    validate();
}

// ---- edits ---------------------------------------------------------------------

void PieceTree::insert(std::uint64_t offset, std::span<const std::byte> bytes) {
    assert(offset <= size());
    if (bytes.empty()) return;
    const std::uint64_t len = bytes.size();
    const std::uint64_t lf = count_lf_bytes(bytes);
    AddBlock* last = blocks_.empty() ? nullptr : blocks_.back().get();
    const bool fits = last != nullptr && last->capacity - last->used >= len;

    // Typing at the end of the newest add-buffer piece extends that piece in place.
    if (fits && offset > 0) {
        const std::uint64_t add_end = block_start_.back() + last->used;
        std::uint32_t n = root_;
        std::uint64_t off = offset - 1;  // the byte just before the insertion point
        while (n != NIL) {
            const std::uint32_t l = nodes_[n].left;
            const std::uint64_t lb = l == NIL ? 0 : nodes_[l].bytes;
            const std::uint64_t plen = nodes_[n].piece.length;
            if (off < lb) {
                n = l;
            } else if (off < lb + plen) {
                break;
            } else {
                off -= lb + plen;
                n = nodes_[n].right;
            }
        }
        if (n != NIL) {
            const Piece p = nodes_[n].piece;
            const std::uint64_t lb = nodes_[n].left == NIL ? 0 : nodes_[nodes_[n].left].bytes;
            if (off - lb + 1 == p.length && p.buffer == 0 && p.offset + p.length == add_end) {
                std::memcpy(last->data.get() + last->used, bytes.data(), bytes.size());
                last->used += len;
                set_piece(n, {0, p.offset, p.length + len, p.lf_count + lf});
                pull_to_root(n);
                validate();
                return;
            }
        }
    }

    if (!fits) {
        auto block = std::make_shared<AddBlock>();
        block->capacity = std::max(kAddBlockSize, len);
        block->data = std::make_unique_for_overwrite<std::byte[]>(static_cast<std::size_t>(block->capacity));
        block_start_.push_back(blocks_.empty() ? 0 : block_start_.back() + blocks_.back()->capacity);
        blocks_.push_back(std::move(block));
        last = blocks_.back().get();
    }
    const Piece p{0, block_start_.back() + last->used, len, lf};
    std::memcpy(last->data.get() + last->used, bytes.data(), bytes.size());
    last->used += len;

    auto [a, c] = split(root_, offset);
    set_root(merge(merge(a, new_node(p)), c));
    validate();
}

void PieceTree::insert_run(std::uint64_t offset, const PieceRun& run) {
    assert(offset <= size());
    const std::uint32_t mid = build(run);
    if (mid == NIL) return;
    auto [a, c] = split(root_, offset);
    set_root(merge(merge(a, mid), c));
    validate();
}

PieceRun PieceTree::erase(std::uint64_t offset, std::uint64_t length) {
    assert(offset <= size() && length <= size() - offset);
    PieceRun run;
    if (length == 0) return run;
    auto [a, bc] = split(root_, offset);
    auto [b, c] = split(bc, length);
    // In-order collection of the removed pieces.
    std::vector<std::uint32_t> stack;
    for (std::uint32_t n = b; n != NIL || !stack.empty();) {
        while (n != NIL) {
            stack.push_back(n);
            n = nodes_[n].left;
        }
        n = stack.back();
        stack.pop_back();
        run.push_back(nodes_[n].piece);
        n = nodes_[n].right;
    }
    free_subtree(b);
    set_root(merge(a, c));
    validate();
    return run;
}

PieceRun PieceTree::pieces(std::uint64_t offset, std::uint64_t length) const {
    assert(offset <= size() && length <= size() - offset);
    PieceRun run;
    if (length == 0) return run;
    std::uint64_t remaining = length;
    std::vector<std::uint32_t> stack;  // ancestors still to visit, in order
    std::uint32_t n = root_;
    std::uint64_t off = offset;
    std::uint64_t inner = 0;
    while (n != NIL) {
        const std::uint32_t l = nodes_[n].left;
        const std::uint64_t lb = l == NIL ? 0 : nodes_[l].bytes;
        const std::uint64_t plen = nodes_[n].piece.length;
        if (off < lb) {
            stack.push_back(n);
            n = l;
        } else if (off < lb + plen) {
            inner = off - lb;
            break;
        } else {
            off -= lb + plen;
            n = nodes_[n].right;
        }
    }
    while (n != NIL && remaining > 0) {
        const Piece& p = nodes_[n].piece;
        const std::uint64_t take = std::min(remaining, p.length - inner);
        Piece part{p.buffer, p.offset + inner, take, p.lf_count};
        if (take != p.length && p.lf_count != kUnknownLines) part.lf_count = count_lf(p, inner, take);
        run.push_back(part);
        remaining -= take;
        inner = 0;
        std::uint32_t r = nodes_[n].right;
        while (r != NIL) {
            stack.push_back(r);
            r = nodes_[r].left;
        }
        if (stack.empty()) break;
        n = stack.back();
        stack.pop_back();
    }
    return run;
}

PieceRun PieceTree::store(std::span<const std::byte> bytes) {
    PieceRun run;
    while (!bytes.empty()) {
        AddBlock* last = blocks_.empty() ? nullptr : blocks_.back().get();
        std::uint64_t room = last == nullptr ? 0 : last->capacity - last->used;
        if (room == 0) {
            // A block never moves, so `bytes` may point into an older block of this tree.
            auto block = std::make_shared<AddBlock>();
            block->capacity = std::max<std::uint64_t>(kAddBlockSize, bytes.size());
            block->data = std::make_unique_for_overwrite<std::byte[]>(static_cast<std::size_t>(block->capacity));
            block_start_.push_back(blocks_.empty() ? 0 : block_start_.back() + blocks_.back()->capacity);
            blocks_.push_back(std::move(block));
            last = blocks_.back().get();
            room = last->capacity;
        }
        const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(room, bytes.size()));
        const std::span<const std::byte> part = bytes.first(take);
        std::memcpy(last->data.get() + last->used, part.data(), take);
        run.push_back({0, block_start_.back() + last->used, take, count_lf_bytes(part)});
        last->used += take;
        bytes = bytes.subspan(take);
    }
    return run;
}

BufferIndex PieceTree::rebase(std::shared_ptr<const MappedFile> file, std::span<const ChunkLines> chunk_lines) {
    nodes_.clear();
    free_.clear();
    unknown_index_.clear();
    root_ = NIL;
    const std::uint64_t size = file ? file->size() : 0;
    const BufferIndex idx = add_buffer(std::move(file));
    std::uint64_t expect = 0;
    bool tiles = true;
    for (const ChunkLines& c : chunk_lines) {
        if (c.offset != expect || c.length == 0) tiles = false;
        expect = c.offset + c.length;
    }
    tiles = tiles && expect == size;
    // An empty `chunk_lines` asks for unknown counts, as at construction (a reload).
    assert((tiles || chunk_lines.empty()) && "rebase: chunk_lines must tile the file");
    std::vector<Piece> pieces;
    if (tiles) {
        for (const ChunkLines& c : chunk_lines) {
            seeded_[idx].push_back({c.offset, c.length});
            chunk_cache_[idx][c.offset] = {c.length, c.lf_count};
            pieces.push_back({idx, c.offset, c.length, c.lf_count});
        }
    } else if (size > 0) {  // defensive: fall back to unknown counts
        seeded_[idx] = make_chunks(*files_[idx]);
        for (const Chunk& c : seeded_[idx]) pieces.push_back({idx, c.offset, c.length, kUnknownLines});
    }
    seed(idx, pieces);
    return idx;
}

// ---- queries -------------------------------------------------------------------

std::uint64_t PieceTree::size() const noexcept { return root_ == NIL ? 0 : nodes_[root_].bytes; }

void PieceTree::read(std::uint64_t offset, std::uint64_t length,
                     const std::function<bool(std::span<const std::byte>)>& sink) const {
    const std::uint64_t total = size();
    if (offset >= total || length == 0) return;
    std::uint64_t remaining = std::min(length, total - offset);
    std::vector<std::uint32_t> stack;  // ancestors still to visit, in order
    std::uint32_t n = root_;
    std::uint64_t off = offset;
    std::uint64_t inner = 0;
    while (n != NIL) {
        const std::uint32_t l = nodes_[n].left;
        const std::uint64_t lb = l == NIL ? 0 : nodes_[l].bytes;
        const std::uint64_t plen = nodes_[n].piece.length;
        if (off < lb) {
            stack.push_back(n);
            n = l;
        } else if (off < lb + plen) {
            inner = off - lb;
            break;
        } else {
            off -= lb + plen;
            n = nodes_[n].right;
        }
    }
    while (n != NIL && remaining > 0) {
        const Piece& p = nodes_[n].piece;
        const std::uint64_t take = std::min(remaining, p.length - inner);
        if (!sink({piece_data(p) + inner, static_cast<std::size_t>(take)})) return;
        remaining -= take;
        inner = 0;
        // In-order successor.
        std::uint32_t r = nodes_[n].right;
        while (r != NIL) {
            stack.push_back(r);
            r = nodes_[r].left;
        }
        if (stack.empty()) break;
        n = stack.back();
        stack.pop_back();
    }
}

std::string PieceTree::read(std::uint64_t offset, std::uint64_t length) const {
    std::string out;
    read(offset, length, [&](std::span<const std::byte> s) {
        out.append(reinterpret_cast<const char*>(s.data()), s.size());
        return true;
    });
    return out;
}

std::byte PieceTree::byte_at(std::uint64_t offset) const {
    assert(offset < size());
    std::uint32_t n = root_;
    while (true) {
        const std::uint32_t l = nodes_[n].left;
        const std::uint64_t lb = l == NIL ? 0 : nodes_[l].bytes;
        const Piece& p = nodes_[n].piece;
        if (offset < lb) {
            n = l;
        } else if (offset < lb + p.length) {
            return piece_data(p)[offset - lb];
        } else {
            offset -= lb + p.length;
            n = nodes_[n].right;
        }
    }
}

std::uint64_t PieceTree::find_lf_forward(std::uint64_t from, std::uint64_t limit) const {
    const std::uint64_t total = size();
    if (from >= total || limit == 0) return npos;
    std::uint64_t result = npos;
    std::uint64_t pos = from;
    read(from, std::min(limit, total - from), [&](std::span<const std::byte> s) {
        if (const void* hit = std::memchr(s.data(), '\n', s.size())) {
            result = pos + static_cast<std::uint64_t>(static_cast<const std::byte*>(hit) - s.data());
            return false;
        }
        pos += s.size();
        return true;
    });
    return result;
}

std::uint64_t PieceTree::find_lf_backward(std::uint64_t from, std::uint64_t limit) const {
    from = std::min(from, size());
    const std::uint64_t lo = from - std::min(limit, from);
    std::vector<std::pair<std::uint64_t, std::span<const std::byte>>> spans;
    for (std::uint64_t end = from; end > lo;) {
        const std::uint64_t start = end - std::min(kBackwardWindow, end - lo);
        spans.clear();
        std::uint64_t pos = start;
        read(start, end - start, [&](std::span<const std::byte> s) {
            spans.emplace_back(pos, s);
            pos += s.size();
            return true;
        });
        for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
            const auto& s = it->second;
            const auto hit = std::find(s.rbegin(), s.rend(), std::byte{'\n'});
            if (hit != s.rend()) return it->first + static_cast<std::uint64_t>(s.rend() - hit) - 1;
        }
        end = start;
    }
    return npos;
}

bool PieceTree::resolve_piece(std::uint32_t n) {
    Piece p = nodes_[n].piece;
    if (p.lf_count != kUnknownLines) return false;
    const auto& cache = chunk_cache_[p.buffer];
    const auto it = cache.find(p.offset);
    if (it != cache.end() && it->second.length == p.length && it->second.lf_count != kUnknownLines) {
        p.lf_count = it->second.lf_count;
    } else {
        p.lf_count = count_lf(p, 0, p.length);
        // A piece that is exactly a seeded chunk fills the chunk cache too.
        const auto& seeded = seeded_[p.buffer];
        const auto s = std::lower_bound(seeded.begin(), seeded.end(), p.offset,
                                        [](const Chunk& c, std::uint64_t o) { return c.offset < o; });
        if (s != seeded.end() && s->offset == p.offset && s->length == p.length)
            chunk_cache_[p.buffer][p.offset] = {p.length, p.lf_count};
    }
    set_piece(n, p);
    return true;
}

void PieceTree::resolve_subtree(std::uint32_t n) {
    if (n == NIL || nodes_[n].unknown == 0) return;
    resolve_subtree(nodes_[n].left);
    resolve_piece(n);
    resolve_subtree(nodes_[n].right);
    pull(n);
}

std::optional<std::uint64_t> PieceTree::line_of(std::uint64_t offset, bool force) {
    assert(offset <= size());
    std::vector<std::uint32_t> path;
    bool resolved = false;
    std::uint64_t line = 0;
    std::uint64_t off = offset;
    std::optional<std::uint64_t> result;
    std::uint32_t n = root_;
    for (;;) {
        if (n == NIL) {
            result = line;
            break;
        }
        path.push_back(n);
        const std::uint32_t l = nodes_[n].left;
        const std::uint64_t lb = l == NIL ? 0 : nodes_[l].bytes;
        if (off < lb) {
            n = l;
            continue;
        }
        if (l != NIL && nodes_[l].unknown > 0) {
            if (!force) break;
            resolve_subtree(l);
            resolved = true;
        }
        line += l == NIL ? 0 : nodes_[l].lf_known;
        off -= lb;
        const Piece& p = nodes_[n].piece;
        if (off < p.length) {
            result = line + count_lf(p, 0, off);
            break;
        }
        if (p.lf_count == kUnknownLines) {
            if (!force) break;
            resolved = resolve_piece(n) || resolved;
        }
        line += nodes_[n].piece.lf_count;
        off -= nodes_[n].piece.length;
        n = nodes_[n].right;
    }
    if (resolved)
        for (auto it = path.rbegin(); it != path.rend(); ++it) pull(*it);
    if (resolved) validate();
    return result;
}

std::optional<std::uint64_t> PieceTree::line_start(std::uint64_t line, bool force) {
    if (line == 0) return 0;
    std::vector<std::uint32_t> path;
    bool resolved = false;
    std::uint64_t remaining = line;  // which line feed (1-based) ends the previous line
    std::uint64_t base = 0;
    std::optional<std::uint64_t> result;
    std::uint32_t n = root_;
    while (n != NIL) {
        path.push_back(n);
        const std::uint32_t l = nodes_[n].left;
        if (l != NIL && nodes_[l].unknown > 0) {
            if (!force) break;
            resolve_subtree(l);
            resolved = true;
        }
        const std::uint64_t llf = l == NIL ? 0 : nodes_[l].lf_known;
        if (remaining <= llf) {
            n = l;
            continue;
        }
        remaining -= llf;
        base += l == NIL ? 0 : nodes_[l].bytes;
        if (nodes_[n].piece.lf_count == kUnknownLines) {
            if (!force) break;
            resolved = resolve_piece(n) || resolved;
        }
        const Piece& p = nodes_[n].piece;
        if (remaining <= p.lf_count) {
            const std::byte* d = piece_data(p);
            for (std::uint64_t i = 0; i < p.length; ++i) {  // bounded: a wrong cached count must not read past the piece
                if (d[i] == std::byte{'\n'} && --remaining == 0) {
                    result = base + i + 1;
                    break;
                }
            }
            assert(result.has_value());
            break;
        }
        remaining -= p.lf_count;
        base += p.length;
        n = nodes_[n].right;
    }
    if (resolved) {
        for (auto it = path.rbegin(); it != path.rend(); ++it) pull(*it);
        validate();
    }
    return result;
}

std::optional<std::uint64_t> PieceTree::line_count() const {
    if (root_ == NIL) return 1;
    if (nodes_[root_].unknown > 0) return std::nullopt;
    return nodes_[root_].lf_known + 1;
}

void PieceTree::record_chunk_lines(BufferIndex buffer, std::uint64_t chunk_offset, std::uint64_t chunk_length,
                                   std::uint64_t lf_count) {
    if (buffer == 0 || buffer >= files_.size() || chunk_length == 0) return;
    chunk_cache_[buffer][chunk_offset] = {chunk_length, lf_count};
    const std::uint64_t chunk_end = chunk_offset + chunk_length;
    std::vector<std::uint32_t> hits;
    for (auto it = unknown_index_.lower_bound({buffer, chunk_offset});
         it != unknown_index_.end() && it->first.first == buffer && it->first.second < chunk_end; ++it)
        hits.push_back(it->second);
    // Pieces that still exactly cover the chunk take its count; pieces split from it
    // are counted here too (bounded by the chunk's length).
    for (std::uint32_t n : hits) {
        Piece p = nodes_[n].piece;
        if (p.offset + p.length > chunk_end) continue;
        p.lf_count = (p.offset == chunk_offset && p.length == chunk_length) ? lf_count : count_lf(p, 0, p.length);
        set_piece(n, p);
        pull_to_root(n);
    }
    validate();
}

FrozenBytes PieceTree::frozen_bytes(BufferIndex buffer, std::uint64_t offset, std::uint64_t length) const {
    if (length == 0) return {};
    if (buffer == 0) {
        const auto it = std::upper_bound(block_start_.begin(), block_start_.end(), offset);
        const auto i = static_cast<std::size_t>(it - block_start_.begin()) - 1;
        assert(offset + length <= block_start_[i] + blocks_[i]->used);
        return {{blocks_[i]->data.get() + (offset - block_start_[i]), static_cast<std::size_t>(length)}, blocks_[i]};
    }
    assert(offset + length <= files_[buffer]->size());
    return {{files_[buffer]->data() + offset, static_cast<std::size_t>(length)}, files_[buffer]};
}

// Debug builds check every invariant after each mutation.
void PieceTree::validate() const {
#ifndef NDEBUG
    // A full walk after every edit: kept to trees small enough that it stays cheap, which
    // the randomized tests are; on a huge tree it would make every edit linear.
    if (nodes_.size() - free_.size() > kValidateMaxNodes) return;
    std::uint64_t unknown_nodes = 0;
    struct Agg {
        std::uint64_t bytes, lf, unknown;
    };
    auto check = [&](auto&& self, std::uint32_t n, std::uint32_t parent) -> Agg {
        if (n == NIL) return {0, 0, 0};
        const Node& x = nodes_[n];
        assert(x.parent == parent);
        assert(x.piece.length > 0);
        assert(x.piece.buffer < files_.size());
        if (x.left != NIL) assert(nodes_[x.left].prio <= x.prio);
        if (x.right != NIL) assert(nodes_[x.right].prio <= x.prio);
        const Agg l = self(self, x.left, n);
        const Agg r = self(self, x.right, n);
        const bool unknown = x.piece.lf_count == kUnknownLines;
        unknown_nodes += unknown ? 1 : 0;
        const Agg a{l.bytes + r.bytes + x.piece.length, l.lf + r.lf + (unknown ? 0 : x.piece.lf_count),
                    l.unknown + r.unknown + (unknown ? 1 : 0)};
        assert(a.bytes == x.bytes && a.lf == x.lf_known && a.unknown == x.unknown);
        return a;
    };
    check(check, root_, NIL);
    assert(unknown_nodes == unknown_index_.size());
#endif
}

}  // namespace mod
