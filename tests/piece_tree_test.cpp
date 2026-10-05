#include <doctest/doctest.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "platform/file_map.hpp"
#include "text/line_scanner.hpp"
#include "text/piece_tree.hpp"
#include "util/event_queue.hpp"
#include "util/hash.hpp"
#include "time_budget.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "piece_tree_test";
    fs::create_directories(dir);
    return dir / name;
}

std::shared_ptr<const MappedFile> map_text(const std::string& name, std::string_view text) {
    const fs::path p = scratch(name);
    {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    auto m = MappedFile::open(p);
    REQUIRE(m.has_value());
    return *m;
}

std::span<const std::byte> bytes_of(std::string_view s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

std::string content(const PieceTree& t) { return t.read(0, t.size()); }

std::uint64_t oracle_line_of(const std::string& s, std::uint64_t offset) {
    return static_cast<std::uint64_t>(std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
}

std::optional<std::uint64_t> oracle_line_start(const std::string& s, std::uint64_t line) {
    if (line == 0) return 0;
    std::uint64_t seen = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i] == '\n' && ++seen == line) return i + 1;
    return std::nullopt;
}

std::uint64_t oracle_find_forward(const std::string& s, std::uint64_t from, std::uint64_t limit) {
    if (from >= s.size()) return PieceTree::npos;
    const auto end = std::min<std::uint64_t>(s.size(), from + std::min<std::uint64_t>(limit, s.size()));
    for (std::uint64_t i = from; i < end; ++i)
        if (s[i] == '\n') return i;
    return PieceTree::npos;
}

std::uint64_t oracle_find_backward(const std::string& s, std::uint64_t from, std::uint64_t limit) {
    const std::uint64_t lo = from - std::min(limit, from);
    for (std::uint64_t i = from; i > lo; --i)
        if (s[i - 1] == '\n') return i - 1;
    return PieceTree::npos;
}

std::uint64_t resident_bytes() {
    std::ifstream statm("/proc/self/statm");
    std::uint64_t size = 0, resident = 0;
    statm >> size >> resident;
    return resident * static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
}

}  // namespace

TEST_CASE("randomized edits match a std::string oracle") {
    std::mt19937_64 rng(20261001);
    auto rand_below = [&](std::uint64_t n) { return n == 0 ? 0 : rng() % n; };

    std::string original;
    for (int i = 0; original.size() < 6000; ++i)
        original += "line " + std::to_string(i) + std::string(rand_below(40), 'x') + (i % 7 == 0 ? "\r\n" : "\n");
    auto file = map_text("random.txt", original);
    PieceTree tree(file, 256);  // small chunks: many pieces with unknown counts
    std::string oracle = original;
    const std::vector<Chunk> chunks = tree.chunks(1);
    REQUIRE(!chunks.empty());

    struct SavedRun {
        PieceRun run;
        std::string text;
    };
    std::vector<SavedRun> saved;
    const char alphabet[] = "abc\n\r\xC3\xA9 ";

    for (int op = 0; op < 100'000; ++op) {
        const auto size = oracle.size();
        const auto kind = rand_below(100);
        if (kind < 40 && size < 8000) {
            const auto at = rand_below(size + 1);
            std::string text;
            const auto len = 1 + rand_below(rand_below(4) == 0 ? 300 : 6);
            for (std::uint64_t i = 0; i < len; ++i) text += alphabet[rand_below(sizeof alphabet - 1)];
            tree.insert(at, bytes_of(text));
            oracle.insert(at, text);
        } else if (kind < 75 && size > 0) {
            const auto at = rand_below(size);
            const auto len = 1 + rand_below(std::min<std::uint64_t>(size - at, size > 6000 ? 400 : 40) );
            PieceRun run = tree.erase(at, len);
            std::uint64_t run_bytes = 0;
            for (const Piece& p : run) run_bytes += p.length;
            REQUIRE(run_bytes == len);
            saved.push_back({std::move(run), oracle.substr(at, len)});
            if (saved.size() > 16) saved.erase(saved.begin());
            oracle.erase(at, len);
        } else if (kind < 88 && !saved.empty() && size < 8000) {
            const auto& s = saved[rand_below(saved.size())];
            const auto at = rand_below(size + 1);
            tree.insert_run(at, s.run);
            oracle.insert(at, s.text);
        } else if (kind < 93) {
            const Chunk& c = chunks[rand_below(chunks.size())];
            const auto lf = static_cast<std::uint64_t>(
                std::count(original.begin() + static_cast<std::ptrdiff_t>(c.offset),
                           original.begin() + static_cast<std::ptrdiff_t>(c.offset + c.length), '\n'));
            tree.record_chunk_lines(1, c.offset, c.length, lf);
        } else {
            const auto at = rand_below(oracle.size() + 1);
            const bool force = rand_below(2) == 0;
            const auto line = tree.line_of(at, force);
            if (force) REQUIRE(line.has_value());
            if (line) REQUIRE(*line == oracle_line_of(oracle, at));
            const auto want = rand_below(oracle_line_of(oracle, oracle.size()) + 3);
            const auto start = tree.line_start(want, force);
            if (force || start) REQUIRE(start == oracle_line_start(oracle, want));
        }

        REQUIRE(tree.size() == oracle.size());
        if (!oracle.empty()) {
            const auto at = rand_below(oracle.size());
            REQUIRE(tree.byte_at(at) == static_cast<std::byte>(oracle[at]));
        }
        if (op % 97 == 0) {
            const auto from = rand_below(oracle.size() + 1);
            const auto limit = rand_below(200'000);
            REQUIRE(tree.find_lf_forward(from, limit) == oracle_find_forward(oracle, from, limit));
            REQUIRE(tree.find_lf_backward(from, limit) == oracle_find_backward(oracle, from, limit));
        }
        if (op % 1000 == 0) {
            REQUIRE(content(tree) == oracle);
            const auto count = tree.line_count();
            if (count) REQUIRE(*count == oracle_line_of(oracle, oracle.size()) + 1);
        }
    }
    REQUIRE(content(tree) == oracle);
}

TEST_CASE("line_of and line_start round trip") {
    const std::string text = "alpha\nbeta\n\ngamma\r\ndelta";
    PieceTree tree(map_text("lines.txt", text), 4);
    tree.insert(3, bytes_of("X\nY"));
    const std::string doc = content(tree);
    REQUIRE(doc == "alpX\nYha\nbeta\n\ngamma\r\ndelta");
    CHECK_FALSE(tree.line_count().has_value());  // the original's chunks are not scanned yet
    REQUIRE(tree.line_of(tree.size(), true) == std::optional<std::uint64_t>(5));
    const auto lines = tree.line_count();
    REQUIRE(lines == std::optional<std::uint64_t>(6));
    for (std::uint64_t line = 0; line < *lines; ++line) {
        const auto start = tree.line_start(line, true);
        REQUIRE(start.has_value());
        CHECK(tree.line_of(*start, false) == line);
        CHECK(*start == *oracle_line_start(doc, line));
    }
    CHECK_FALSE(tree.line_start(*lines, true).has_value());
    for (std::uint64_t off = 0; off <= doc.size(); ++off) CHECK(tree.line_of(off, false) == oracle_line_of(doc, off));
}

TEST_CASE("unknown line counts: force resolves, no force reports unknown") {
    std::string text;
    for (int i = 0; i < 200; ++i) text += "row " + std::to_string(i) + "\n";
    PieceTree tree(map_text("unknown.txt", text), 64);
    const auto chunks = tree.chunks(1);
    REQUIRE(chunks.size() > 3);
    for (std::size_t i = 0; i + 1 < chunks.size(); ++i) {
        CHECK(chunks[i].offset + chunks[i].length == chunks[i + 1].offset);
        CHECK(chunks[i].length == 64);  // hard boundaries: no look-ahead for a line feed
    }
    CHECK(chunks.back().offset + chunks.back().length == text.size());

    CHECK_FALSE(tree.line_count().has_value());
    // Inside the first chunk nothing unknown precedes the offset.
    CHECK(tree.line_of(5, false) == std::optional<std::uint64_t>(0));
    const std::uint64_t late = chunks[2].offset + 1;
    CHECK_FALSE(tree.line_of(late, false).has_value());
    CHECK_FALSE(tree.line_start(150, false).has_value());

    CHECK(tree.line_of(late, true) == oracle_line_of(text, late));
    CHECK(tree.line_of(late, false) == oracle_line_of(text, late));  // cached by the forced query
    CHECK(tree.line_start(150, true) == oracle_line_start(text, 150));
    CHECK(tree.line_of(text.size(), true) == 200u);
    CHECK(tree.line_count() == std::optional<std::uint64_t>(201));
}

TEST_CASE("record_chunk_lines fills whole chunks and chunks split by edits") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "entry " + std::to_string(i) + "\n";
    PieceTree tree(map_text("record.txt", text), 64);
    const auto chunks = tree.chunks(1);
    REQUIRE(chunks.size() > 4);

    // Split chunk 1 by an edit before its count is known, then erase across chunks 2-3.
    tree.insert(chunks[1].offset + 3, bytes_of("new\n"));
    tree.erase(chunks[2].offset + 4 + 2, chunks[2].length);  // +4: the inserted bytes
    std::string oracle = text;
    oracle.insert(chunks[1].offset + 3, "new\n");
    oracle.erase(chunks[2].offset + 4 + 2, chunks[2].length);
    REQUIRE(content(tree) == oracle);
    CHECK_FALSE(tree.line_count().has_value());

    for (const Chunk& c : chunks) {
        const auto lf = static_cast<std::uint64_t>(
            std::count(text.begin() + static_cast<std::ptrdiff_t>(c.offset),
                       text.begin() + static_cast<std::ptrdiff_t>(c.offset + c.length), '\n'));
        tree.record_chunk_lines(1, c.offset, c.length, lf);
    }
    REQUIRE(tree.line_count().has_value());
    CHECK(*tree.line_count() == oracle_line_of(oracle, oracle.size()) + 1);
    for (std::uint64_t off = 0; off <= oracle.size(); off += 7)
        CHECK(tree.line_of(off, false) == oracle_line_of(oracle, off));

    // Pieces re-inserted after their chunk was recorded are known at once.
    PieceRun run = tree.erase(0, 30);
    tree.insert_run(10, run);
    CHECK(tree.line_count().has_value());
}

TEST_CASE("rebase seeds the saved file with known counts and keeps old buffers") {
    const std::string text = "one\ntwo\nthree\n";
    PieceTree tree(map_text("before.txt", text), 4);
    tree.insert(0, bytes_of("zero\n"));
    PieceRun undo_run = tree.erase(5, 4);  // "one\n", refers to buffer 1
    const std::string saved = content(tree);

    auto file = map_text("after.txt", saved);
    std::vector<ChunkLines> chunk_lines{{0, 5, 1}, {5, saved.size() - 5, 2}};
    tree.rebase(file, chunk_lines);
    CHECK(content(tree) == saved);
    CHECK(tree.line_count() == std::optional<std::uint64_t>(4));
    CHECK(tree.chunks(2).size() == 2);

    tree.insert_run(0, undo_run);  // old buffers stay valid after rebase
    CHECK(content(tree) == "one\n" + saved);
    CHECK_FALSE(tree.line_count().has_value());  // that piece's buffer was never scanned
    CHECK(tree.line_of(tree.size(), true) == 4u);
    CHECK(tree.line_count() == std::optional<std::uint64_t>(5));
}

TEST_CASE("zero-length files") {
    auto file = map_text("empty.txt", "");
    CHECK(file->size() == 0);
    CHECK(file->data() == nullptr);
    PieceTree tree(file);
    CHECK(tree.size() == 0);
    CHECK(tree.chunks(1).empty());
    CHECK(tree.line_count() == std::optional<std::uint64_t>(1));
    CHECK(tree.line_of(0, false) == std::optional<std::uint64_t>(0));
    CHECK(tree.line_start(0, false) == std::optional<std::uint64_t>(0));
    CHECK_FALSE(tree.line_start(1, true).has_value());
    CHECK(tree.find_lf_forward(0, 100) == PieceTree::npos);
    CHECK(tree.find_lf_backward(0, 100) == PieceTree::npos);
    CHECK(tree.erase(0, 0).empty());
    tree.insert(0, bytes_of("hi\n"));
    CHECK(content(tree) == "hi\n");
    CHECK(tree.line_count() == std::optional<std::uint64_t>(2));

    PieceTree no_file;
    CHECK(no_file.size() == 0);
    CHECK(no_file.line_count() == std::optional<std::uint64_t>(1));
}

TEST_CASE("CRLF counts as one line feed and is preserved") {
    const std::string text = "a\r\nb\r\n\r\nc";
    PieceTree tree(map_text("crlf.txt", text));
    CHECK(tree.line_count() == std::nullopt);
    CHECK(tree.line_start(1, true) == std::optional<std::uint64_t>(3));
    CHECK(tree.line_start(3, true) == std::optional<std::uint64_t>(8));
    CHECK(tree.line_count() == std::optional<std::uint64_t>(4));
    CHECK(tree.find_lf_forward(0, 100) == 2u);
    CHECK(tree.find_lf_backward(3, 100) == 2u);
    CHECK(tree.byte_at(1) == std::byte{'\r'});
    tree.insert(tree.size(), bytes_of("\r\n"));
    CHECK(content(tree) == text + "\r\n");
}

TEST_CASE("frozen_bytes outlive later edits") {
    PieceTree tree;
    tree.insert(0, bytes_of("hello"));
    PieceRun run = tree.erase(0, 5);
    REQUIRE(run.size() == 1);
    FrozenBytes view = tree.frozen_bytes(run[0].buffer, run[0].offset, run[0].length);
    for (int i = 0; i < 2000; ++i) tree.insert(tree.size(), bytes_of("0123456789abcdef0123456789abcdef0123456789"));
    CHECK(std::string_view(reinterpret_cast<const char*>(view.bytes.data()), view.bytes.size()) == "hello");
    CHECK(view.keep_alive != nullptr);
}

TEST_CASE("consecutive typing extends one add-buffer piece") {
    PieceTree tree;
    for (char c : std::string("typing")) tree.insert(tree.size(), bytes_of(std::string_view(&c, 1)));
    PieceRun run = tree.erase(0, tree.size());
    REQUIRE(run.size() == 1);
    CHECK(run[0].length == 6);
    CHECK(run[0].lf_count == 0);
}

TEST_CASE("LineScanner results arrive through EventQueue.drain") {
    std::string text;
    for (int i = 0; i < 500; ++i) text += "scan line " + std::to_string(i) + "\n";
    auto file = map_text("scan.txt", text);
    PieceTree tree(file, 128);
    int wakes = 0;
    EventQueue queue([&] { ++wakes; });
    auto on_chunk = [&](BufferIndex b, std::uint64_t off, std::uint64_t len, std::uint64_t lf) {
        tree.record_chunk_lines(b, off, len, lf);
    };
    std::optional<Result<ContentHash>> done;

    SUBCASE("a scanner destroyed at once leaves the queue safe to drain") {
        { LineScanner scanner(file, 1, tree.chunks(1), queue, on_chunk, [&](Result<ContentHash> h) { done = std::move(h); }); scanner.start(); }
        queue.drain();  // whatever was posted before the stop still runs
        return;
    }

    LineScanner scanner(file, 1, tree.chunks(1), queue, on_chunk, [&](Result<ContentHash> h) { done = std::move(h); });
    scanner.start();
    while (!done.has_value()) {  // results only ever arrive through drain, on this thread
        queue.drain();
        std::this_thread::yield();
    }
    CHECK(wakes > 0);
    REQUIRE(done->has_value());
    ContentHasher expect;
    expect.update(bytes_of(text));
    CHECK(**done == expect.finish());
    CHECK(tree.line_count() == std::optional<std::uint64_t>(501));

    queue.close();
    CHECK_FALSE(queue.post([] {}));
    CHECK(queue.drain() == 0);
}

TEST_CASE("sparse 8 GiB file opens and edits at both ends without RSS growth") {
    const fs::path p = scratch("sparse.bin");
    constexpr std::uint64_t kSize = std::uint64_t{8} << 30;
    {
        const int fd = ::open(p.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        REQUIRE(fd >= 0);
        REQUIRE(::ftruncate(fd, static_cast<off_t>(kSize)) == 0);
        REQUIRE(::pwrite(fd, "head\n", 5, 0) == 5);
        REQUIRE(::pwrite(fd, "\ntail", 5, static_cast<off_t>(kSize - 5)) == 5);
        ::close(fd);
    }
    struct Cleanup {
        fs::path path;
        ~Cleanup() { fs::remove(path); }
    } cleanup{p};

    auto file = MappedFile::open(p);
    REQUIRE(file.has_value());
    PieceTree tree(*file);
    REQUIRE(tree.size() == kSize);
    CHECK(tree.chunks(1).size() >= kSize / PieceTree::kDefaultChunkSize);

    const std::uint64_t before = resident_bytes();
    tree.insert(0, bytes_of("start "));
    tree.insert(tree.size(), bytes_of(" end"));
    PieceRun head = tree.erase(2, 3);
    PieceRun tail = tree.erase(tree.size() - 7, 3);
    tree.insert_run(0, tail);
    tree.insert_run(tree.size(), head);
    CHECK(tree.read(0, 11) == "ailst head\n");
    CHECK(tree.read(tree.size() - 9, 9) == "\nt endart");
    CHECK(tree.byte_at(tree.size() - 1) == std::byte{'t'});
    const std::uint64_t after = resident_bytes();
    CHECK(tree.size() == kSize + 10);
    CHECK(after < before + (std::uint64_t{32} << 20));
}

TEST_CASE("deleting from one large piece keeps the tree balanced: 100 000 erases neither crash nor crawl") {
    std::string original(1 << 20, 'x');
    for (std::size_t i = 0; i < original.size(); i += 64) original[i] = '\n';
    auto file = map_text("erases.txt", original);
    PieceTree tree(file);
    std::string oracle = original;
    const auto start = std::chrono::steady_clock::now();
    // Every few bytes, from the end backwards: each erase splits the one original piece.
    for (std::uint64_t at = oracle.size() - 2; at > 10 && oracle.size() > (1 << 20) - 100'000; at -= 7) {
        tree.erase(at, 1);
        oracle.erase(at, 1);
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < time_budget(10.0));
    REQUIRE(tree.size() == oracle.size());
    CHECK(content(tree) == oracle);
}
