#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "edit/sidecar.hpp"
#include "edit/undo_tree.hpp"
#include "text/piece_tree.hpp"
#include "util/event_queue.hpp"
#include "util/hash.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "sidecar_test";
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

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

ContentHash hash_of(std::string_view s) {
    ContentHasher h;
    h.update(std::as_bytes(std::span(s.data(), s.size())));
    return h.finish();
}

std::string hex(std::string_view bytes) {
    std::string out;
    char buf[4];
    for (unsigned char c : bytes) {
        std::snprintf(buf, sizeof buf, "%02x", c);
        out += buf;
    }
    return out;
}

std::string unhex(std::string_view h) {
    std::string out;
    for (std::size_t i = 0; i + 1 < h.size();) {
        if (h[i] == ' ' || h[i] == '\n') {
            ++i;
            continue;
        }
        out += static_cast<char>(std::stoi(std::string(h.substr(i, 2)), nullptr, 16));
        i += 2;
    }
    return out;
}

struct Record {
    std::uint8_t type;
    std::size_t start;  // of the frame
    std::size_t body;   // offset of the body
    std::uint64_t length;
};

std::uint64_t le(const std::string& s, std::size_t at, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(static_cast<unsigned char>(s[at + static_cast<std::size_t>(i)])) << (8 * i);
    return v;
}

std::vector<Record> records(const std::string& file) {
    std::vector<Record> out;
    std::size_t pos = 32;
    while (pos + 13 <= file.size()) {
        const std::uint64_t len = le(file, pos, 8);
        if (pos + 13 + len > file.size()) break;
        out.push_back({static_cast<std::uint8_t>(file[pos + 8]), pos, pos + 9, len});
        pos += 13 + len;
    }
    return out;
}

std::string frame(std::uint8_t type, const std::string& body) {
    std::string out;
    for (int i = 0; i < 8; ++i) out += static_cast<char>(body.size() >> (8 * i) & 0xFF);
    out += static_cast<char>(type);
    out += body;
    const std::byte t{type};
    const std::uint32_t crc = crc32(std::as_bytes(std::span(body.data(), body.size())), crc32(std::span(&t, 1)));
    for (int i = 0; i < 4; ++i) out += static_cast<char>(crc >> (8 * i) & 0xFF);
    return out;
}

// Runs main-thread closures until `done` or a generous deadline; never timing-dependent.
template <class F>
bool pump(EventQueue& q, F done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done()) {
        if (q.drain() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (std::chrono::steady_clock::now() > deadline) return false;
    }
    return true;
}

SidecarSeams fixed_clock(std::int64_t ms) {
    SidecarSeams s;
    s.now_ms = [ms] { return ms; };
    return s;
}

NodeMeta meta(NodeId id, NodeId parent, std::int64_t t, EditKind kind, std::uint64_t cb, std::uint64_t ca) {
    return NodeMeta{id, parent, t, kind, cb, ca};
}

EditOp insert_op(std::uint64_t offset, std::string bytes) {
    return EditOp{offset, Payload::of_bytes({}), Payload::of_bytes(std::move(bytes))};
}

std::vector<NodeOp> inline_ops(const std::vector<EditOp>& ops) {
    PieceTree none;
    return to_node_ops(ops, none);
}

constexpr std::int64_t kT0 = 1'700'000'000'000;

// The worked example of docs/sidecar-format.md: "hello\n", then two branches from it,
// one of them saved.
std::string build_worked_example(const fs::path& doc) {
    EventQueue q({});
    {
        Sidecar s(doc, q, 0644, fixed_clock(kT0 + 3000));
        UndoTree t;
        REQUIRE(s.open(t, 6)->state == LoadOutcome::no_history);
        const NodeId root = t.add_root(meta(0, kNoParent, kT0, EditKind::other, 0, 0), 6, hash_of("hello\n"));
        s.append_root(t.meta(root), 6, hash_of("hello\n"));
        s.release_deferred(t);
        const NodeId a = t.commit({insert_op(0, "big ")}, meta(0, 0, kT0 + 1000, EditKind::typing, 0, 4));
        s.append_node(t.meta(a), inline_ops({insert_op(0, "big ")}));
        t.undo_step();
        const NodeId b = t.commit({insert_op(5, "!")}, meta(0, 0, kT0 + 2000, EditKind::typing, 5, 6));
        s.append_node(t.meta(b), inline_ops({insert_op(5, "!")}));
        s.append_save(b, 7, hash_of("hello!\n"));
        s.append_position(b, {{root, b}});
        REQUIRE(s.flush(10000).has_value());
    }
    return read_file(sidecar_path_for(doc));
}

// Bytes of the worked example, as printed in docs/sidecar-format.md.
constexpr std::string_view kWorkedExampleHex = R"(
4d4f444849535400 0100 0100 00000000 b873e5cf8b010000 0000000000000000
3800000000000000 01
  0100000000000000 0600000000000000
  5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03
  0068e5cf8b010000
  c2265073
4300000000000000 03
  0200000000000000 0100000000000000 e86be5cf8b010000 00
  0000000000000000 0400000000000000 01000000
  0000000000000000 00 00000000 00 04000000 62696720
  13f67fa0
4000000000000000 03
  0300000000000000 0100000000000000 d06fe5cf8b010000 00
  0500000000000000 0600000000000000 01000000
  0500000000000000 00 00000000 00 01000000 21
  330406ff
3800000000000000 04
  0300000000000000 0700000000000000
  c8a31cb076b21999bd2cdcfa5f446a7a6644de88037087112fa18bd90cc13984
  b873e5cf8b010000
  1447b9cc
1c00000000000000 05
  0300000000000000 01000000 0100000000000000 0300000000000000
  c0d46378
)";

}  // namespace

TEST_CASE("sidecar_path_for appends .history to the full name") {
    CHECK(sidecar_path_for("/a/b/notes.txt") == fs::path("/a/b/notes.txt.history"));
    CHECK(sidecar_path_for("/a/b/go") == fs::path("/a/b/go.history"));
}

TEST_CASE("golden hashes match sha256sum of the fixtures") {
    // sha256sum of the files holding "hello\n" and "hello!\n".
    CHECK(to_hex(hash_of("hello\n")) == "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03");
    CHECK(to_hex(hash_of("hello!\n")) == "c8a31cb076b21999bd2cdcfa5f446a7a6644de88037087112fa18bd90cc13984");
}

TEST_CASE("the worked example in the format document is byte-identical") {
    const fs::path doc = scratch("example.txt");
    write_file(doc, "hello\n");
    const std::string bytes = build_worked_example(doc);
    INFO("actual: " << hex(bytes));
    CHECK(hex(bytes) == hex(unhex(kWorkedExampleHex)));

    SUBCASE("and loads back as the same tree") {
        EventQueue q({});
        write_file(sidecar_path_for(doc), unhex(kWorkedExampleHex));
        Sidecar s(doc, q, 0644);
        UndoTree t;
        auto out = s.open(t, 7);
        REQUIRE(out);
        CHECK(out->state == LoadOutcome::provisional);
        CHECK(out->candidate == NodeId{3});
        CHECK(s.verify(hash_of("hello!\n")) == NodeId{3});
        CHECK_FALSE(s.verify(hash_of("hello?\n")));
        REQUIRE(t.roots().size() == 1);
        const NodeInfo root = t.node_info(1);
        CHECK(root.children.size() == 2);
        CHECK(root.preferred_child == NodeId{3});  // from the POSITION record
        CHECK(t.node_info(3).is_save_point);
        CHECK(t.meta(2).kind == EditKind::typing);
        CHECK(std::get<Payload::Inline>(t.ops(2)[0].inserted.form).bytes == "big ");
        CHECK(t.next_id() == 4);
    }
}

TEST_CASE("round trip: the tree and save points survive a reopen") {
    const fs::path doc = scratch("round.txt");
    write_file(doc, "abc\n");
    EventQueue q({});
    NodeId saved = 0;
    {
        Sidecar s(doc, q, 0644);
        UndoTree t;
        REQUIRE(s.open(t, 4)->state == LoadOutcome::no_history);
        const NodeId root = t.add_root({}, 4, hash_of("abc\n"));
        s.append_root(t.meta(root), 4, hash_of("abc\n"));
        CHECK_FALSE(fs::exists(sidecar_path_for(doc)));  // nothing before the first NODE
        s.release_deferred(t);
        std::vector<EditOp> ops{{1, Payload::of_bytes("b"), Payload::of_bytes("XY")}};
        const NodeId n1 = t.commit(ops, meta(0, 0, 5, EditKind::replace, 1, 3));
        s.append_node(t.meta(n1), inline_ops(ops));
        const NodeId n2 = t.commit({insert_op(0, ">")}, meta(0, 0, 6, EditKind::paste, 0, 1));
        s.append_node(t.meta(n2), inline_ops({insert_op(0, ">")}));
        s.append_save(n2, 6, hash_of(">aXYc\n"));
        saved = n2;
        REQUIRE(s.flush(10000).has_value());
    }
    Sidecar s(doc, q, 0644);
    UndoTree t;
    auto out = s.open(t, 6);
    REQUIRE(out);
    CHECK(out->state == LoadOutcome::provisional);
    CHECK(out->candidate == saved);
    CHECK(t.meta(saved).parent == 2);
    CHECK(t.meta(2).kind == EditKind::replace);
    CHECK(t.meta(2).cursor_after == 3);
    const EditOp& op = t.ops(2)[0];
    CHECK(op.offset == 1);
    CHECK(std::get<Payload::Inline>(op.removed.form).bytes == "b");
    CHECK(std::get<Payload::Inline>(op.inserted.form).bytes == "XY");
    CHECK(t.save_point(saved)->hash == hash_of(">aXYc\n"));
    CHECK(s.verify(hash_of(">aXYc\n")) == saved);
}

TEST_CASE("damaged sidecars") {
    const fs::path doc = scratch("damaged.txt");
    write_file(doc, "x");
    EventQueue q({});
    {
        Sidecar s(doc, q, 0644);
        UndoTree t;
        s.open(t, 1);
        const NodeId root = t.add_root({}, 1, hash_of("x"));
        s.append_root(t.meta(root), 1, hash_of("x"));
        s.release_deferred(t);
        for (int i = 0; i < 3; ++i) {
            const NodeId n = t.commit({insert_op(0, "a")}, {});
            s.append_node(t.meta(n), inline_ops({insert_op(0, "a")}));
        }
        REQUIRE(s.flush(10000).has_value());
    }
    const fs::path side = sidecar_path_for(doc);
    const std::string good = read_file(side);
    const auto recs = records(good);
    REQUIRE(recs.size() == 4);

    SUBCASE("a truncated tail is ignored, then cut off before the next append") {
        write_file(side, good + good.substr(recs[1].start, 20));
        Sidecar s(doc, q, 0644);
        UndoTree t;
        REQUIRE(s.open(t, 1));
        CHECK(t.contains(4));
        t.set_position(4, {});
        s.release_deferred(t);
        const NodeId n = t.commit({insert_op(0, "z")}, {});
        s.append_node(t.meta(n), inline_ops({insert_op(0, "z")}));
        REQUIRE(s.flush(10000).has_value());
        const std::string after = read_file(side);
        CHECK(after.substr(0, good.size()) == good);
        CHECK(records(after).size() == 5);
        CHECK(records(after).back().start == good.size());
    }
    SUBCASE("a CRC mismatch ends the valid part") {
        std::string bad = good;
        bad[recs[2].body + 2] ^= 0x01;  // inside the second NODE
        write_file(side, bad);
        Sidecar s(doc, q, 0644);
        UndoTree t;
        REQUIRE(s.open(t, 1));
        CHECK(t.contains(2));
        CHECK_FALSE(t.contains(3));
        CHECK_FALSE(t.contains(4));
    }
    SUBCASE("unknown record types are skipped") {
        std::string spliced = good.substr(0, recs[2].start) + frame(99, "future") + good.substr(recs[2].start);
        write_file(side, spliced);
        Sidecar s(doc, q, 0644);
        UndoTree t;
        REQUIRE(s.open(t, 1));
        CHECK(t.contains(4));
    }
    SUBCASE("a newer format version is read-only") {
        std::string newer = good;
        newer[8] = 2;
        write_file(side, newer);
        Sidecar s(doc, q, 0644);
        UndoTree t;
        auto out = s.open(t, 1);
        REQUIRE(out);
        CHECK(out->state == LoadOutcome::read_only);
        CHECK(s.state() == SidecarState::read_only);
        s.release_deferred(t);
        const NodeId root = t.add_root({}, 1, hash_of("x"));
        s.append_root(t.meta(root), 1, hash_of("x"));
        CHECK(s.flush(10000).has_value());
        CHECK(read_file(side) == newer);
        CHECK(s.clear().error().code == ErrorCode::unsupported);
        CHECK(fs::exists(side));
    }
}

TEST_CASE("a file that is not a sidecar is never touched") {
    const fs::path doc = scratch("go");
    write_file(doc, "package main\n");
    const fs::path side = sidecar_path_for(doc);
    write_file(side, "module example.com/x\n");
    EventQueue q({});
    Sidecar s(doc, q, 0644);
    UndoTree t;
    auto out = s.open(t, 13);
    REQUIRE(out);
    CHECK(out->state == LoadOutcome::disabled);
    CHECK(s.state() == SidecarState::disabled);
    const NodeId root = t.add_root({}, 13, {});
    s.append_root(t.meta(root), 13, {});
    s.release_deferred(t);
    const NodeId n = t.commit({insert_op(0, "a")}, {});
    s.append_node(t.meta(n), inline_ops({insert_op(0, "a")}));
    CHECK(s.flush(10000).has_value());
    CHECK(s.clear().has_value());
    CHECK(read_file(side) == "module example.com/x\n");
}

TEST_CASE("a lock held by another instance gives read-only") {
    const fs::path doc = scratch("locked.txt");
    write_file(doc, "x");
    EventQueue q({});
    Sidecar first(doc, q, 0644);
    UndoTree t1;
    first.open(t1, 1);
    const NodeId root = t1.add_root({}, 1, hash_of("x"));
    first.append_root(t1.meta(root), 1, hash_of("x"));
    first.release_deferred(t1);
    const NodeId n = t1.commit({insert_op(0, "a")}, {});
    first.append_node(t1.meta(n), inline_ops({insert_op(0, "a")}));
    REQUIRE(first.flush(10000).has_value());
    const std::string before = read_file(sidecar_path_for(doc));

    Sidecar second(doc, q, 0644);
    UndoTree t2;
    auto out = second.open(t2, 1);
    REQUIRE(out);
    CHECK(out->state == LoadOutcome::read_only);
    CHECK(out->candidate == root);  // history is still loaded
    CHECK(t2.contains(n));
    t2.set_position(n, {});
    second.release_deferred(t2);
    const NodeId m = t2.commit({insert_op(0, "b")}, {});
    second.append_node(t2.meta(m), inline_ops({insert_op(0, "b")}));
    CHECK(second.flush(10000).has_value());
    CHECK(read_file(sidecar_path_for(doc)) == before);
    CHECK(second.clear().error().code == ErrorCode::unsupported);
}

TEST_CASE("payloads: inline up to 4096 bytes, PAYLOAD records from 4097, rebound to SidecarRef") {
    const fs::path doc = scratch("payload.txt");
    write_file(doc, "");
    EventQueue q({});
    Sidecar s(doc, q, 0644);
    std::vector<std::tuple<NodeId, std::uint32_t, Which, SidecarRef>> rebinds;
    s.set_callbacks({[&](NodeId n, std::uint32_t op, Which w, SidecarRef r) { rebinds.emplace_back(n, op, w, r); }, {}});
    UndoTree t;
    s.open(t, 0);
    const NodeId root = t.add_root({}, 0, hash_of(""));
    s.append_root(t.meta(root), 0, hash_of(""));
    s.release_deferred(t);

    PieceTree text;
    const std::string small(4096, 'a');
    const std::string big(4097, 'b');
    text.insert(0, std::as_bytes(std::span(small.data(), small.size())));
    const PieceRun small_run = text.pieces(0, 4096);
    const PieceRun big_run = text.store(std::as_bytes(std::span(big.data(), big.size())));
    std::vector<EditOp> ops{{0, Payload::of_bytes({}), Payload::of_run(small_run, 4096)},
                            {0, Payload::of_bytes({}), Payload::of_run(big_run, 4097)}};
    const NodeId n = t.commit(ops, {});
    s.append_node(t.meta(n), to_node_ops(t.ops(n), text));
    REQUIRE(s.flush(10000).has_value());  // drains the rebind closure

    const std::string file = read_file(sidecar_path_for(doc));
    const auto recs = records(file);
    REQUIRE(recs.size() == 3);  // ROOT, one PAYLOAD (the 4097 bytes), NODE
    CHECK(recs[1].type == 2);
    CHECK(recs[1].length == 4097);
    REQUIRE(rebinds.size() == 1);
    const auto [node, op_index, which, ref] = rebinds[0];
    CHECK(node == n);
    CHECK(op_index == 1);
    CHECK(which == Which::inserted);
    CHECK(ref.file_offset == recs[1].body);
    CHECK(ref.length == 4097);
    t.rebind_payload(node, op_index, which, ref);
    CHECK(std::holds_alternative<SidecarRef>(t.ops(n)[1].inserted.form));

    auto where = s.payload_bytes(text, ref);
    REQUIRE(where);
    const FrozenBytes f = text.frozen_bytes(where->first, where->second, ref.length);
    CHECK(std::string(reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size()) == big);
}

TEST_CASE("fsync runs once after a SAVE record and never after NODE records") {
    const fs::path doc = scratch("sync.txt");
    write_file(doc, "x");
    EventQueue q({});
    int syncs = 0;
    SidecarSeams seams;
    seams.sync = [&](int) {
        ++syncs;
        return 0;
    };
    Sidecar s(doc, q, 0644, seams);
    UndoTree t;
    s.open(t, 1);
    const NodeId root = t.add_root({}, 1, hash_of("x"));
    s.append_root(t.meta(root), 1, hash_of("x"));
    s.release_deferred(t);
    for (int i = 0; i < 5; ++i) {
        const NodeId n = t.commit({insert_op(0, "a")}, {});
        s.append_node(t.meta(n), inline_ops({insert_op(0, "a")}));
    }
    REQUIRE(s.flush(10000).has_value());
    CHECK(syncs == 0);
    s.append_save(*t.current(), 6, hash_of("aaaaax"));
    REQUIRE(s.flush(10000).has_value());
    CHECK(syncs == 1);
}

TEST_CASE("a failed write disables persistence and reports it") {
    const fs::path doc = scratch("fail.txt");
    write_file(doc, "x");
    EventQueue q({});
    int writes = 0;
    SidecarSeams seams;
    seams.write = [&](int fd, const void* data, std::size_t size) -> long {
        if (++writes > 3) return static_cast<long>(size / 2);  // a short write
        return ::write(fd, data, size);
    };
    Sidecar s(doc, q, 0644, seams);
    std::string message;
    s.set_callbacks({{}, [&](std::string m) { message = std::move(m); }});
    UndoTree t;
    s.open(t, 1);
    const NodeId root = t.add_root({}, 1, hash_of("x"));
    s.append_root(t.meta(root), 1, hash_of("x"));
    s.release_deferred(t);
    for (int i = 0; i < 4; ++i) {
        const NodeId n = t.commit({insert_op(0, "a")}, {});
        s.append_node(t.meta(n), inline_ops({insert_op(0, "a")}));
    }
    s.flush(10000);
    CHECK(s.state() == SidecarState::disabled);
    CHECK(message.find("short write") != std::string::npos);
}

TEST_CASE("copy_to") {
    const fs::path doc = scratch("orig.txt");
    const fs::path copy = scratch("copy.txt");
    write_file(doc, "x");
    write_file(copy, "x");
    EventQueue q({});
    PieceTree text;
    const std::string big(5000, 'q');
    const PieceRun big_run = text.store(std::as_bytes(std::span(big.data(), big.size())));

    auto make = [&](Sidecar& s, UndoTree& t) {
        s.open(t, 1);
        const NodeId root = t.add_root({}, 1, hash_of("x"));
        s.append_root(t.meta(root), 1, hash_of("x"));
        s.release_deferred(t);
        std::vector<EditOp> ops{{0, Payload::of_bytes({}), Payload::of_run(big_run, 5000)}};
        const NodeId n = t.commit(ops, {});
        s.set_callbacks({[&](NodeId node, std::uint32_t op, Which w, SidecarRef r) { t.rebind_payload(node, op, w, r); }, {}});
        s.append_node(t.meta(n), to_node_ops(t.ops(n), text));
        REQUIRE(s.flush(10000).has_value());
        return n;
    };

    SUBCASE("byte-identical copy plus unpersisted nodes; the old file is unchanged") {
        Sidecar s(doc, q, 0644);
        UndoTree t;
        const NodeId n = make(s, t);
        CHECK(std::holds_alternative<SidecarRef>(t.ops(n)[0].inserted.form));
        const std::string old_bytes = read_file(sidecar_path_for(doc));
        // A node held back by the deferral rule goes only to the new file.
        s.defer();
        const NodeId held = t.commit({insert_op(0, "h")}, {});
        s.append_node(t.meta(held), inline_ops({insert_op(0, "h")}));
        auto side = s.copy_to(copy, 0600, t, text);
        REQUIRE(side);
        CHECK((*side)->state() == SidecarState::attached);
        CHECK(((fs::status(sidecar_path_for(copy)).permissions() & fs::perms::mask) == (fs::perms::owner_read | fs::perms::owner_write)));
        const std::string new_bytes = read_file(sidecar_path_for(copy));
        CHECK(new_bytes == old_bytes);
        (*side)->release_deferred(t);
        REQUIRE((*side)->flush(10000).has_value());
        CHECK(read_file(sidecar_path_for(doc)) == old_bytes);
        const std::string grown = read_file(sidecar_path_for(copy));
        CHECK(grown.substr(0, old_bytes.size()) == old_bytes);
        CHECK(records(grown).size() == records(old_bytes).size() + 1);
        // SidecarRefs resolve in the copy.
        const SidecarRef ref = std::get<SidecarRef>(t.ops(n)[0].inserted.form);
        auto where = (*side)->payload_bytes(text, ref);
        REQUIRE(where);
        const FrozenBytes f = text.frozen_bytes(where->first, where->second, ref.length);
        CHECK(std::string(reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size()) == big);
    }
    SUBCASE("a foreign file at the new name is never touched") {
        write_file(sidecar_path_for(copy), "module x\n");
        Sidecar s(doc, q, 0644);
        UndoTree t;
        make(s, t);
        auto side = s.copy_to(copy, 0644, t, text);
        CHECK_FALSE(side);
        CHECK(side.error().code == ErrorCode::format);
        CHECK(read_file(sidecar_path_for(copy)) == "module x\n");
    }
    SUBCASE("a valid unlocked sidecar of another history is replaced") {
        {
            Sidecar other(copy, q, 0644);
            UndoTree t;
            other.open(t, 1);
            const NodeId root = t.add_root({}, 1, hash_of("x"));
            other.append_root(t.meta(root), 1, hash_of("x"));
            other.release_deferred(t);
            for (int i = 0; i < 3; ++i) {
                const NodeId n = t.commit({insert_op(0, "o")}, {});
                other.append_node(t.meta(n), inline_ops({insert_op(0, "o")}));
            }
            REQUIRE(other.flush(10000).has_value());
        }
        Sidecar s(doc, q, 0644);
        UndoTree t;
        make(s, t);
        auto side = s.copy_to(copy, 0644, t, text);
        REQUIRE(side);
        CHECK(read_file(sidecar_path_for(copy)) == read_file(sidecar_path_for(doc)));
    }
    SUBCASE("a sidecar locked by another instance is left alone") {
        Sidecar other(copy, q, 0644);
        UndoTree ot;
        other.open(ot, 1);
        const NodeId root = ot.add_root({}, 1, hash_of("x"));
        other.append_root(ot.meta(root), 1, hash_of("x"));
        other.release_deferred(ot);
        const NodeId on = ot.commit({insert_op(0, "o")}, {});
        other.append_node(ot.meta(on), inline_ops({insert_op(0, "o")}));
        REQUIRE(other.flush(10000).has_value());
        const std::string theirs = read_file(sidecar_path_for(copy));
        Sidecar s(doc, q, 0644);
        UndoTree t;
        make(s, t);
        auto side = s.copy_to(copy, 0644, t, text);
        REQUIRE(side);
        CHECK((*side)->state() == SidecarState::read_only);
        CHECK(read_file(sidecar_path_for(copy)) == theirs);
    }
    SUBCASE("from a pathless sidecar: a fresh header, the ROOT and every session node") {
        Sidecar s(std::nullopt, q, 0600, fixed_clock(kT0));
        CHECK(s.state() == SidecarState::pathless);
        UndoTree t;
        const NodeId root = t.add_root({}, 0, hash_of(""));
        s.append_root(t.meta(root), 0, hash_of(""));
        s.release_deferred(t);  // ignored: a pathless sidecar never writes
        for (int i = 0; i < 2; ++i) {
            const NodeId n = t.commit({insert_op(0, "u")}, {});
            s.append_node(t.meta(n), inline_ops({insert_op(0, "u")}));
        }
        auto side = s.copy_to(copy, 0644, t, text);
        REQUIRE(side);
        (*side)->release_deferred(t);
        REQUIRE((*side)->flush(10000).has_value());
        const std::string file = read_file(sidecar_path_for(copy));
        const auto recs = records(file);
        REQUIRE(recs.size() == 3);
        CHECK(recs[0].type == 1);
        CHECK(recs[1].type == 3);
        CHECK(recs[2].type == 3);
        CHECK(file.substr(0, 8) == std::string("MODHIST\0", 8));
    }
    SUBCASE("nodes a read-only sidecar never wrote are serialized into the copy") {
        Sidecar first(doc, q, 0644);
        UndoTree t1;
        make(first, t1);
        Sidecar second(doc, q, 0644);
        UndoTree t2;
        REQUIRE(second.open(t2, 1)->state == LoadOutcome::read_only);
        t2.set_position(1, {});
        second.release_deferred(t2);
        const NodeId mine = t2.commit({insert_op(0, "m")}, {});
        second.append_node(t2.meta(mine), inline_ops({insert_op(0, "m")}));
        auto side = second.copy_to(copy, 0644, t2, text);
        REQUIRE(side);
        CHECK((*side)->state() == SidecarState::attached);
        UndoTree t3;
        side->reset();
        Sidecar reader(copy, q, 0644);
        reader.open(t3, 1);
        CHECK(t3.contains(mine));
        CHECK(t3.meta(mine).parent == 1);
    }
}

TEST_CASE("clear") {
    const fs::path doc = scratch("clear.txt");
    write_file(doc, "x");
    EventQueue q({});
    Sidecar s(doc, q, 0644);
    UndoTree t;
    s.open(t, 1);
    const NodeId root = t.add_root({}, 1, hash_of("x"));
    s.append_root(t.meta(root), 1, hash_of("x"));
    s.release_deferred(t);
    const NodeId n = t.commit({insert_op(0, "a")}, {});
    s.append_node(t.meta(n), inline_ops({insert_op(0, "a")}));
    REQUIRE(s.flush(10000).has_value());
    const fs::path side = sidecar_path_for(doc);
    REQUIRE(fs::exists(side));

    // Held-back records are dropped too.
    s.defer();
    const NodeId held = t.commit({insert_op(0, "b")}, {});
    s.append_node(t.meta(held), inline_ops({insert_op(0, "b")}));
    REQUIRE(s.clear().has_value());
    CHECK_FALSE(fs::exists(side));
    s.release_deferred(t);
    REQUIRE(s.flush(10000).has_value());
    CHECK_FALSE(fs::exists(side));  // only the next append creates a file

    t.reset();
    const NodeId r2 = t.add_root({}, 2, hash_of("ax"));
    s.append_root(t.meta(r2), 2, hash_of("ax"));
    CHECK(s.flush(10000).has_value());
    CHECK_FALSE(fs::exists(side));  // a ROOT alone does not create it
    const NodeId m = t.commit({insert_op(0, "c")}, {});
    s.append_node(t.meta(m), inline_ops({insert_op(0, "c")}));
    REQUIRE(s.flush(10000).has_value());
    const auto recs = records(read_file(side));
    REQUIRE(recs.size() == 2);
    CHECK(recs[0].type == 1);
    CHECK(recs[1].type == 3);
}

TEST_CASE("open reports load_ms from the injected now_ms") {
    const fs::path doc = scratch("load_ms.txt");
    write_file(doc, "hello!\n");
    build_worked_example(doc);
    auto clock = [](std::vector<std::int64_t> values) {
        SidecarSeams s;
        s.now_ms = [values, i = std::make_shared<std::size_t>(0)] { return values[std::min((*i)++, values.size() - 1)]; };
        return s;
    };
    EventQueue q({});
    SUBCASE("a clock that advances 6 000 ms during the read gives 6000") {
        Sidecar s(doc, q, 0644, clock({1000, 7000}));
        UndoTree t;
        auto out = s.open(t, 7);
        REQUIRE(out);
        CHECK(out->load_ms == 6000);
    }
    SUBCASE("a clock stepping backwards gives 0") {
        Sidecar s(doc, q, 0644, clock({7000, 1000}));
        UndoTree t;
        CHECK(s.open(t, 7)->load_ms == 0);
    }
    SUBCASE("no sidecar file gives 0") {
        const fs::path none = scratch("load_ms_none.txt");
        write_file(none, "x");
        Sidecar s(none, q, 0644, clock({1000, 7000}));
        UndoTree t;
        CHECK(s.open(t, 1)->load_ms == 0);
    }
}

namespace {

PieceRun stored(PieceTree& text, const std::string& bytes) { return text.store(std::as_bytes(std::span(bytes.data(), bytes.size()))); }

std::string bytes_at(const PieceTree& text, std::pair<BufferIndex, std::uint64_t> where, std::uint64_t length) {
    const FrozenBytes f = text.frozen_bytes(where.first, where.second, length);
    return std::string(reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size());
}

// R ─┬─ g (old, 20 000-byte payload)
//    └─ a (old) ─┬─ b (old) ── c (recent, 5000-byte payload) ── d (recent, saved) ── e (recent, unpersisted 4097-byte Pieces)
//                └─ f (recent, saved last, current)
// With cutoff 500: kept c, d, e, f; tops c and f; anchor a; c absorbs b.
struct RewriteFixture {
    fs::path doc;
    EventQueue q{{}};
    PieceTree text;
    UndoTree t;
    std::unique_ptr<Sidecar> s;
    NodeId r = 0, g = 0, a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
    const std::string big_c = std::string(5000, 'q');
    const std::string big_e = std::string(4097, 'e');

    explicit RewriteFixture(const std::string& name, SidecarSeams seams = {}) : doc(scratch(name)) {
        write_file(doc, "x");
        s = std::make_unique<Sidecar>(doc, q, 0640, std::move(seams));
        s->set_callbacks({[this](NodeId n, std::uint32_t op, Which w, SidecarRef ref) { t.rebind_payload(n, op, w, ref); }, {}});
        REQUIRE(s->open(t, 1));
        r = t.add_root(meta(0, kNoParent, 0, EditKind::other, 0, 0), 1, hash_of("x"));
        s->append_root(t.meta(r), 1, hash_of("x"));
        s->release_deferred(t);
        auto add = [&](std::vector<EditOp> ops, std::int64_t time, bool persist = true) {
            const NodeId n = t.commit(std::move(ops), meta(0, 0, time, EditKind::paste, 0, 0));
            if (persist) s->append_node(t.meta(n), to_node_ops(t.ops(n), text));
            return n;
        };
        auto big = [&](std::uint64_t at, const std::string& bytes) {
            return EditOp{at, Payload::of_bytes({}), Payload::of_run(stored(text, bytes), bytes.size())};
        };
        g = add({big(0, std::string(20000, 'g'))}, 100);
        t.undo_step();
        a = add({insert_op(0, "a")}, 100);
        b = add({insert_op(1, "b")}, 200);
        c = add({big(2, big_c)}, 1000);
        d = add({insert_op(0, "d")}, 1100);
        t.mark_saved(d, 9, hash_of("d"));
        s->append_save(d, 9, hash_of("d"));
        e = add({big(0, big_e)}, 1300, false);
        t.set_position(a, {});
        f = add({insert_op(1, "f")}, 1200);
        t.mark_saved(f, 9, hash_of("f"));
        s->append_save(f, 9, hash_of("f"));
        REQUIRE(s->flush(10000).has_value());  // c's and g's payloads become SidecarRefs
        REQUIRE(std::holds_alternative<SidecarRef>(t.ops(c)[0].inserted.form));
    }
};

}  // namespace

TEST_CASE("rewrite") {
    RewriteFixture fx("rewrite.txt");
    const PrunePlan plan = fx.t.plan_prune(500);
    REQUIRE(plan.anchor == fx.a);
    REQUIRE(plan.tops == std::vector<NodeId>{fx.c, fx.f});
    REQUIRE(plan.kept == std::vector<NodeId>{fx.c, fx.d, fx.e, fx.f});
    const fs::path side = sidecar_path_for(fx.doc);
    const std::string old_file = read_file(side);

    SUBCASE("output, payloads, permissions, lock and appends") {
        // A piece read from the old mapping before the rewrite.
        const SidecarRef old_ref = std::get<SidecarRef>(fx.t.ops(fx.c)[0].inserted.form);
        auto old_where = fx.s->payload_bytes(fx.text, old_ref);
        REQUIRE(old_where);

        auto rebinds = fx.s->rewrite(fx.t, plan, 2, hash_of("ax"), fx.text);
        REQUIRE(rebinds);
        CHECK(read_file(side).size() < old_file.size());
        CHECK(bytes_at(fx.text, *old_where, old_ref.length) == fx.big_c);

        // c's SidecarRef was copied (its pruned index is 1, after b's absorbed op), and
        // e's Pieces were written out.
        REQUIRE(rebinds->size() == 2);
        CHECK((*rebinds)[0].node == fx.c);
        CHECK((*rebinds)[0].op_index == 1);
        CHECK((*rebinds)[0].which == Which::inserted);
        CHECK((*rebinds)[1].node == fx.e);
        CHECK((*rebinds)[1].op_index == 0);
        fx.t.apply_prune(plan, 2, hash_of("ax"));
        for (const Rebind& rb : *rebinds) fx.t.rebind_payload(rb.node, rb.op_index, rb.which, rb.ref);
        const SidecarRef new_c = std::get<SidecarRef>(fx.t.ops(fx.c)[1].inserted.form);
        const SidecarRef new_e = std::get<SidecarRef>(fx.t.ops(fx.e)[0].inserted.form);
        auto where_c = fx.s->payload_bytes(fx.text, new_c);
        REQUIRE(where_c);
        CHECK(bytes_at(fx.text, *where_c, new_c.length) == fx.big_c);
        auto where_e = fx.s->payload_bytes(fx.text, new_e);
        REQUIRE(where_e);
        CHECK(bytes_at(fx.text, *where_e, new_e.length) == fx.big_e);

        CHECK((fs::status(side).permissions() & fs::perms::mask) == static_cast<fs::perms>(0640));

        // Reopening yields exactly the pruned tree; the lock is held, so read-only.
        {
            Sidecar second(fx.doc, fx.q, 0644);
            UndoTree t2;
            auto out = second.open(t2, 9);
            REQUIRE(out);
            CHECK(out->state == LoadOutcome::read_only);
            CHECK(out->candidate == fx.f);  // the latest save point
            CHECK(t2.roots() == std::vector<NodeId>{fx.a});
            CHECK(t2.root_base(fx.a)->size == 2);
            CHECK(t2.root_base(fx.a)->hash == hash_of("ax"));
            CHECK(t2.meta(fx.a).time_unix_ms == 1000);
            CHECK_FALSE(t2.contains(fx.r));
            CHECK_FALSE(t2.contains(fx.g));
            CHECK_FALSE(t2.contains(fx.b));
            CHECK(t2.meta(fx.c).parent == fx.a);
            REQUIRE(t2.ops(fx.c).size() == 2);
            CHECK(std::get<Payload::Inline>(t2.ops(fx.c)[0].inserted.form).bytes == "b");
            CHECK(std::get<SidecarRef>(t2.ops(fx.c)[1].inserted.form) == new_c);
            CHECK(std::get<SidecarRef>(t2.ops(fx.e)[0].inserted.form) == new_e);
            CHECK(t2.save_point(fx.d));
            CHECK(t2.save_point(fx.f));
            CHECK(t2.node_info(fx.a).preferred_child == fx.f);
            CHECK(t2.node_info(fx.c).preferred_child == fx.d);
            CHECK(t2.node_info(fx.d).preferred_child == fx.e);
            const auto recs = records(read_file(side));
            REQUIRE(recs.size() >= 2);
            CHECK(recs[0].type == 1);
            CHECK(recs.back().type == 5);
        }

        // Appends after the rewrite go to the new file.
        const NodeId h = fx.t.commit({insert_op(0, "h")}, {});
        fx.s->append_node(fx.t.meta(h), inline_ops({insert_op(0, "h")}));
        REQUIRE(fx.s->flush(10000).has_value());
        Sidecar third(fx.doc, fx.q, 0644);
        UndoTree t3;
        REQUIRE(third.open(t3, 9));
        CHECK(t3.contains(h));
        CHECK(t3.meta(h).parent == fx.f);
    }

    SUBCASE("a hard-linked sidecar gives not_atomic and stays byte-identical") {
        const fs::path link = side.parent_path() / "rewrite-link.mod";
        fs::remove(link);
        fs::create_hard_link(side, link);
        auto out = fx.s->rewrite(fx.t, plan, 2, hash_of("ax"), fx.text);
        REQUIRE_FALSE(out);
        CHECK(out.error().code == ErrorCode::not_atomic);
        CHECK(read_file(side) == old_file);
        fs::remove(link);
    }

    SUBCASE("refused in read_only") {
        Sidecar second(fx.doc, fx.q, 0644);
        UndoTree t2;
        REQUIRE(second.open(t2, 9));
        t2.set_position(fx.f, {});
        auto out = second.rewrite(t2, t2.plan_prune(500), 2, hash_of("ax"), fx.text);
        REQUIRE_FALSE(out);
        CHECK(out.error().code == ErrorCode::unsupported);
        CHECK(read_file(side) == old_file);
    }
}

TEST_CASE("rewrite: a write failing through the seams leaves the old file in place") {
    auto failing = std::make_shared<bool>(false);
    SidecarSeams seams;
    seams.write = [failing](int fd, const void* data, std::size_t size) -> long {
        if (*failing) {
            errno = EIO;
            return -1;
        }
        return ::write(fd, data, size);
    };
    RewriteFixture fx("rewrite-fail.txt", seams);
    const fs::path side = sidecar_path_for(fx.doc);
    const std::string old_file = read_file(side);
    struct stat before {};
    REQUIRE(::stat(side.c_str(), &before) == 0);

    // The append still queued when the rewrite drains the writer fails.
    *failing = true;
    const NodeId h = fx.t.commit({insert_op(0, "h")}, {});
    fx.s->append_node(fx.t.meta(h), inline_ops({insert_op(0, "h")}));
    auto out = fx.s->rewrite(fx.t, fx.t.plan_prune(500), 2, hash_of("ax"), fx.text);
    REQUIRE_FALSE(out);
    CHECK(fx.s->state() == SidecarState::disabled);
    struct stat after {};
    REQUIRE(::stat(side.c_str(), &after) == 0);
    CHECK(after.st_ino == before.st_ino);  // never renamed over
    CHECK(read_file(side) == old_file);
}
