#include "text/line_scanner.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <span>
#include <utility>

#include "util/log.hpp"

namespace mod {
namespace {

constexpr std::size_t kBatchChunks = 16;
constexpr auto kBatchInterval = std::chrono::milliseconds(50);

struct ChunkResult {
    std::uint64_t offset;
    std::uint64_t length;
    std::uint64_t lf_count;
};

}  // namespace

LineScanner::LineScanner(std::shared_ptr<const MappedFile> file, BufferIndex buffer, std::vector<Chunk> chunks,
                         EventQueue& queue, ChunkCallback on_chunk, DoneCallback on_done)
    : file_(std::move(file)),
      buffer_(buffer),
      chunks_(std::move(chunks)),
      queue_(queue),
      callbacks_(std::make_shared<const Callbacks>(Callbacks{std::move(on_chunk), std::move(on_done)})) {}

LineScanner::~LineScanner() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void LineScanner::start() {
    if (thread_.joinable()) return;
    thread_ = std::jthread([this](const std::stop_token& stop) { run(stop); });
}

void LineScanner::cancel() { thread_.request_stop(); }

void LineScanner::run(const std::stop_token& stop) {
    const BufferIndex buffer = buffer_;
    auto callbacks = callbacks_;
    auto post_batch = [&](std::vector<ChunkResult>& batch) {
        if (batch.empty()) return;
        queue_.post([callbacks, buffer, results = std::move(batch)] {
            if (!callbacks->on_chunk) return;
            for (const ChunkResult& r : results) callbacks->on_chunk(buffer, r.offset, r.length, r.lf_count);
        });
        batch.clear();
    };
    try {
        ContentHasher hasher;
        std::vector<ChunkResult> batch;
        auto last_post = std::chrono::steady_clock::now();
        const std::byte* data = file_->data();
        for (const Chunk& c : chunks_) {
            if (stop.stop_requested()) return;  // canceled: the partial hash is meaningless
            const std::span<const std::byte> bytes(data + c.offset, static_cast<std::size_t>(c.length));
            const auto lf = static_cast<std::uint64_t>(std::count(bytes.begin(), bytes.end(), std::byte{'\n'}));
            hasher.update(bytes);
            batch.push_back({c.offset, c.length, lf});
            const auto now = std::chrono::steady_clock::now();
            if (batch.size() >= kBatchChunks || now - last_post >= kBatchInterval) {
                post_batch(batch);
                last_post = now;
            }
        }
        post_batch(batch);
        if (stop.stop_requested()) return;
        queue_.post([callbacks, hash = hasher.finish()] {
            if (callbacks->on_done) callbacks->on_done(hash);
        });
        return;
    } catch (const std::exception& e) {
        log(LogLevel::error, "line scan failed: {}", e.what());
    } catch (...) {
        log(LogLevel::error, "line scan failed");
    }
    // Only reached after a failure: line numbers stay unknown and history unverified.
    try {
        queue_.post([callbacks] {
            if (callbacks->on_done) callbacks->on_done(std::unexpected(make_error(ErrorCode::internal, "line scan failed")));
        });
    } catch (...) {
        log(LogLevel::error, "line scan failure could not be reported");
    }
}

}  // namespace mod
