#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "edit/document.hpp"
#include "edit/sidecar.hpp"
#include "util/event_queue.hpp"
#include "util/progress.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

struct Report {
    std::string label;
    std::uint64_t done = 0;
    std::optional<std::uint64_t> total;
};

struct Recorder {
    std::shared_ptr<std::vector<Report>> reports = std::make_shared<std::vector<Report>>();
    ProgressSink sink() const {
        return [r = reports](const Progress& p) { r->push_back({std::string(p.label), p.done, p.total}); };
    }
    std::vector<Report> with(std::string_view label) const {
        std::vector<Report> out;
        for (const Report& r : *reports) {
            if (r.label == label) out.push_back(r);
        }
        return out;
    }
};

bool non_decreasing(const std::vector<Report>& rs) {
    for (std::size_t i = 1; i < rs.size(); ++i) {
        if (rs[i].done < rs[i - 1].done) return false;
    }
    return true;
}

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "progress_test";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    fs::remove(p);
    fs::remove(sidecar_path_for(p));
    return p;
}

void write_file(const fs::path& p, std::string_view bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

template <class F>
bool pump(EventQueue& q, F done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done()) {
        if (q.drain() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (std::chrono::steady_clock::now() > deadline) return false;
    }
    return true;
}

bool wait_for_root_hash(EventQueue& q, const Document& d) {
    const NodeId root = d.history().root_of(*d.history().current());
    return pump(q, [&] { return d.history().root_base(root)->hash != ContentHash{}; });
}

struct Clock {
    std::shared_ptr<std::int64_t> now = std::make_shared<std::int64_t>(1'000'000);
    DocumentOptions options(const Recorder& rec) const {
        DocumentOptions o;
        o.persist_history = PersistHistory::always;  // these tests are about the written history
        o.now_ms = [n = now] { return *n; };
        o.progress = rec.sink();
        return o;
    }
    void advance(std::int64_t ms) const { *now += ms; }
};

void paste(Document& d, std::uint64_t at, std::string_view s) { d.apply(at, 0, s, EditKind::paste, at, at + s.size()); }

}  // namespace

TEST_CASE("format_progress") {
    CHECK(format_progress({"copying history", 512, std::nullopt}) == "copying history: 512 B");
    CHECK(format_progress({"x", 1023, std::nullopt}) == "x: 1023 B");
    CHECK(format_progress({"x", 1024, std::nullopt}) == "x: 1.0 KiB");
    CHECK(format_progress({"x", 1536, std::nullopt}) == "x: 1.5 KiB");
    CHECK(format_progress({"x", std::uint64_t{1} << 20, std::nullopt}) == "x: 1.0 MiB");
    CHECK(format_progress({"x", std::uint64_t{1} << 30, std::nullopt}) == "x: 1.0 GiB");
    CHECK(format_progress({"x", std::uint64_t{1} << 40, std::nullopt}) == "x: 1.0 TiB");
    CHECK(format_progress({"x", std::uint64_t{3} << 50, std::nullopt}) == "x: 3072.0 TiB");
    CHECK(format_progress({"hashing", std::uint64_t{1} << 20, std::uint64_t{4} << 20}) == "hashing: 1.0 MiB of 4.0 MiB (25%)");
    CHECK(format_progress({"x", 999, 1000}) == "x: 999 B of 1000 B (99%)");
    CHECK(format_progress({"x", 1200, 1000}) == "x: 1.2 KiB of 1000 B (100%)");
    CHECK(format_progress({"x", 0, 0}) == "x: 0 B of 0 B (100%)");
    CHECK(format_progress({"x", UINT64_MAX - 1, UINT64_MAX}).ends_with("(99%)"));
}

TEST_CASE("Sidecar.copy_to reports the bytes it copies") {
    const fs::path p = scratch("copy.txt");
    const fs::path q2 = scratch("copy2.txt");
    write_file(p, "base\n");
    EventQueue q({});
    Clock clock;
    Recorder rec;
    auto opened = Document::open(p, q, clock.options(rec));
    REQUIRE(opened);
    Document& d = **opened;
    REQUIRE(wait_for_root_hash(q, d));
    paste(d, 0, std::string(std::size_t{3} << 20, 'p'));  // a multi-MiB PAYLOAD record
    clock.advance(2000);
    paste(d, 0, "q");
    REQUIRE(d.save());
    const auto old_size = fs::file_size(sidecar_path_for(p));
    REQUIRE(old_size > (std::uint64_t{3} << 20));
    rec.reports->clear();

    REQUIRE(d.save_as(q2));
    const auto copies = rec.with("copying history");
    REQUIRE(copies.size() >= 3);  // one report per MiB slice, and one at the end
    CHECK(non_decreasing(copies));
    // The whole file is copied: the saved records, plus the POSITION record Save As
    // flushes first.
    REQUIRE(copies.back().total.has_value());
    CHECK(copies.back().done == *copies.back().total);
    CHECK(*copies.back().total >= old_size);
    CHECK(fs::file_size(sidecar_path_for(q2)) >= copies.back().done);
}

TEST_CASE("Document.prune_history reports hashing and the rewrite") {
    const fs::path p = scratch("prune.txt");
    write_file(p, "base\n");
    EventQueue q({});
    Clock clock;
    Recorder rec;
    auto opened = Document::open(p, q, clock.options(rec));
    REQUIRE(opened);
    Document& d = **opened;
    REQUIRE(wait_for_root_hash(q, d));
    // As in undo_tree_test: the anchor b is neither the root nor a save point.
    paste(d, 0, std::string(5000, 'x'));
    clock.advance(1000);
    paste(d, 0, "B");
    const NodeId b = *d.history().current();
    clock.advance(10 * kDayMs);
    d.apply(1, 5000, std::string_view(), EditKind::delete_, 1, 1);
    const NodeId c = *d.history().current();
    clock.advance(1000);
    paste(d, 0, "D");
    REQUIRE(pump(q, [&] { return std::holds_alternative<SidecarRef>(d.history().ops(c)[0].removed.form); }));
    REQUIRE(d.undo());
    clock.advance(1000);
    paste(d, 0, "E");
    REQUIRE(d.save());
    rec.reports->clear();

    REQUIRE(d.prune_history(*clock.now - 5 * kDayMs));
    REQUIRE(d.history().roots() == std::vector<NodeId>{b});
    const std::uint64_t anchor_size = d.history().root_base(b)->size;
    const auto hashing = rec.with("hashing");
    REQUIRE_FALSE(hashing.empty());
    CHECK(non_decreasing(hashing));
    REQUIRE(hashing.back().total.has_value());
    CHECK(*hashing.back().total == anchor_size);
    CHECK(hashing.back().done == anchor_size);
    const auto pruning = rec.with("pruning history");
    REQUIRE_FALSE(pruning.empty());
    CHECK(non_decreasing(pruning));
    CHECK(pruning.back().total.has_value());
    // The hash is read before the rewrite starts.
    std::size_t last_hash = 0, first_prune = rec.reports->size();
    for (std::size_t i = 0; i < rec.reports->size(); ++i) {
        if ((*rec.reports)[i].label == "hashing") last_hash = i;
        if ((*rec.reports)[i].label == "pruning history") first_prune = std::min(first_prune, i);
    }
    CHECK(last_hash < first_prune);
}

TEST_CASE("Sidecar.flush with an empty queue reports nothing; an empty sink is never called") {
    const fs::path p = scratch("flush.txt");
    write_file(p, "x\n");
    EventQueue q({});
    Recorder rec;
    SidecarSeams seams;
    seams.progress = rec.sink();
    {
        Sidecar side(p, q, 0644, seams);
        UndoTree tree;
        REQUIRE(side.open(tree, 2));
        REQUIRE(side.flush(1000));
    }
    CHECK(rec.reports->empty());

    // A document with no sink saves and copies as before.
    const fs::path p2 = scratch("nosink.txt");
    write_file(p2, "y\n");
    auto doc = Document::open(p2, q);
    REQUIRE(doc);
    REQUIRE(wait_for_root_hash(q, **doc));
    paste(**doc, 0, std::string(std::size_t{2} << 20, 'z'));
    REQUIRE((*doc)->save());
    REQUIRE((*doc)->save_as(scratch("nosink2.txt")));
}
