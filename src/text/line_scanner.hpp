#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "platform/file_map.hpp"
#include "text/piece_tree.hpp"
#include "util/error.hpp"
#include "util/event_queue.hpp"
#include "util/hash.hpp"

namespace mod {

// Walks a mapping once on a worker thread: counts line feeds per chunk and computes
// the whole-file content hash. Results reach the main thread through the EventQueue.
class LineScanner {
public:
    // Main thread; takes the same arguments as PieceTree::record_chunk_lines.
    using ChunkCallback = std::function<void(BufferIndex buffer, std::uint64_t chunk_offset,
                                             std::uint64_t chunk_length, std::uint64_t lf_count)>;
    // Main thread; the hash, or an error if the scan failed. Not called when canceled.
    using DoneCallback = std::function<void(Result<ContentHash>)>;

    LineScanner(std::shared_ptr<const MappedFile> file, BufferIndex buffer, std::vector<Chunk> chunks,
                EventQueue& queue, ChunkCallback on_chunk, DoneCallback on_done);

    // Requests a stop and joins.
    ~LineScanner();
    LineScanner(const LineScanner&) = delete;
    LineScanner& operator=(const LineScanner&) = delete;

    void start();
    // Closures already posted still run; receivers check a generation they captured.
    void cancel();

private:
    struct Callbacks {
        ChunkCallback on_chunk;
        DoneCallback on_done;
    };

    void run(const std::stop_token& stop);

    std::shared_ptr<const MappedFile> file_;
    BufferIndex buffer_;
    std::vector<Chunk> chunks_;
    EventQueue& queue_;
    // Shared with posted closures, so they never refer back to this object.
    std::shared_ptr<const Callbacks> callbacks_;
    std::jthread thread_;
};

}  // namespace mod
