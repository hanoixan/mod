#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "platform/file_map.hpp"

namespace mod {

// Index into the tree's buffer table. 0 is the add buffer; the original file, when
// one is given, is 1. Entries are never removed during a session.
using BufferIndex = std::uint32_t;

inline constexpr std::uint64_t kUnknownLines = std::numeric_limits<std::uint64_t>::max();

struct Piece {
    BufferIndex buffer = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::uint64_t lf_count = kUnknownLines;

    friend bool operator==(const Piece&, const Piece&) = default;
};

// A contiguous span of document content expressed as pieces.
using PieceRun = std::vector<Piece>;

// One slice of a buffer, as the tree was seeded with it.
struct Chunk {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

// A chunk of a saved file with its line-feed count, as recorded while saving.
struct ChunkLines {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::uint64_t lf_count = 0;
};

// Bytes a worker thread may read; `keep_alive` owns the storage behind `bytes`.
struct FrozenBytes {
    std::span<const std::byte> bytes;
    std::shared_ptr<const void> keep_alive;
};

// Byte-offset text storage over immutable buffers, in a treap augmented with subtree
// byte length, known line-feed count and number of pieces whose count is unknown.
// Main thread only, except `frozen_bytes` views.
class PieceTree {
public:
    static constexpr std::uint64_t npos = std::numeric_limits<std::uint64_t>::max();
    static constexpr std::uint64_t kDefaultChunkSize = std::uint64_t{1} << 20;

    explicit PieceTree(std::shared_ptr<const MappedFile> original = nullptr,
                       std::uint64_t chunk_size = kDefaultChunkSize);

    void insert(std::uint64_t offset, std::span<const std::byte> bytes);
    void insert_run(std::uint64_t offset, const PieceRun& run);
    PieceRun erase(std::uint64_t offset, std::uint64_t length);

    // The run covering [offset, offset + length), without changing the tree.
    PieceRun pieces(std::uint64_t offset, std::uint64_t length) const;
    // Copies `bytes` into the add buffer and returns them as a run that is not inserted.
    PieceRun store(std::span<const std::byte> bytes);

    BufferIndex add_buffer(std::shared_ptr<const MappedFile> file);
    // Lets go of a file buffer no piece uses any more, so its mapping can close (Windows
    // cannot shorten a file while it is mapped). Its index is never reused.
    void release_buffer(BufferIndex buffer);

    std::uint64_t size() const noexcept;

    // Streams spans pointing straight into the buffers; `sink` returns false to stop.
    void read(std::uint64_t offset, std::uint64_t length,
              const std::function<bool(std::span<const std::byte>)>& sink) const;
    // Copies; only for bounded lengths.
    std::string read(std::uint64_t offset, std::uint64_t length) const;

    std::byte byte_at(std::uint64_t offset) const;

    std::uint64_t find_lf_forward(std::uint64_t from, std::uint64_t limit) const;
    std::uint64_t find_lf_backward(std::uint64_t from, std::uint64_t limit) const;

    std::optional<std::uint64_t> line_of(std::uint64_t offset, bool force);
    std::optional<std::uint64_t> line_start(std::uint64_t line, bool force);
    std::optional<std::uint64_t> line_count() const;

    // The chunks `buffer` was seeded with, in order (empty for the add buffer).
    std::vector<Chunk> chunks(BufferIndex buffer) const;

    void record_chunk_lines(BufferIndex buffer, std::uint64_t chunk_offset, std::uint64_t chunk_length,
                            std::uint64_t lf_count);

    // `[offset, offset + length)` must lie in one piece's storage (one add-buffer block).
    FrozenBytes frozen_bytes(BufferIndex buffer, std::uint64_t offset, std::uint64_t length) const;

    // Replaces the content with `file`, seeded from `chunk_lines` (which must tile it),
    // or with unknown counts as at construction when `chunk_lines` is empty.
    // Returns the new buffer's index.
    BufferIndex rebase(std::shared_ptr<const MappedFile> file, std::span<const ChunkLines> chunk_lines);

private:
    static constexpr std::uint32_t NIL = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t kValidateMaxNodes = 4096;

    struct Node {
        Piece piece;
        std::uint32_t left = NIL;
        std::uint32_t right = NIL;
        std::uint32_t parent = NIL;
        std::uint32_t prio = 0;
        std::uint64_t bytes = 0;    // subtree bytes
        std::uint64_t lf_known = 0; // subtree line feeds over pieces with known counts
        std::uint64_t unknown = 0;  // subtree pieces with unknown counts
    };

    struct AddBlock {
        std::unique_ptr<std::byte[]> data;
        std::uint64_t capacity = 0;
        std::uint64_t used = 0;
    };

    struct ChunkInfo {
        std::uint64_t length = 0;
        std::uint64_t lf_count = kUnknownLines;
    };

    using UnknownKey = std::pair<BufferIndex, std::uint64_t>;

    const std::byte* piece_data(const Piece& p) const;
    std::uint64_t count_lf(const Piece& p, std::uint64_t from, std::uint64_t len) const;

    std::uint32_t next_priority();
    std::uint32_t new_node(const Piece& p);
    void free_subtree(std::uint32_t n);
    void set_piece(std::uint32_t n, const Piece& p);
    void index_remove(std::uint32_t n);
    void pull(std::uint32_t n);
    void pull_to_root(std::uint32_t n);
    std::uint32_t merge(std::uint32_t a, std::uint32_t b);
    std::pair<std::uint32_t, std::uint32_t> split(std::uint32_t n, std::uint64_t k);
    std::uint32_t build(std::span<const Piece> pieces);
    void set_root(std::uint32_t n);
    void resolve_subtree(std::uint32_t n);
    bool resolve_piece(std::uint32_t n);
    void seed(BufferIndex buffer, std::span<const Piece> pieces);
    std::vector<Chunk> make_chunks(const MappedFile& file) const;
    void validate() const;

    std::uint64_t chunk_size_;
    std::uint64_t rng_state_;
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> free_;
    std::uint32_t root_ = NIL;

    std::vector<std::shared_ptr<const MappedFile>> files_;  // index 0 unused (add buffer)
    std::vector<std::shared_ptr<AddBlock>> blocks_;
    std::vector<std::uint64_t> block_start_;                // add-buffer offset of each block
    std::vector<std::vector<Chunk>> seeded_;                // per buffer
    std::vector<std::map<std::uint64_t, ChunkInfo>> chunk_cache_;  // per buffer, by chunk offset
    std::multimap<UnknownKey, std::uint32_t> unknown_index_;      // nodes with unknown counts
};

}  // namespace mod
