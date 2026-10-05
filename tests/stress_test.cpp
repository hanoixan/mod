// Gigabyte files through the real editor: App's event loop on a scripted terminal opens,
// moves through, edits, searches and saves them, and the file on disk is then compared
// byte for byte. Labelled `stress`: run with `ctest --preset linux-release-stress`.
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "scripted_terminal.hpp"
#include "time_budget.hpp"

using namespace mod;
using namespace mod::testing;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kGiB = std::uint64_t{1} << 30;
// The heap a session may add, whatever the file's size: the file itself is mapped, not read in.
constexpr std::uint64_t kAnonymousLimit = std::uint64_t{256} << 20;

// MOD_STRESS_BYTES, when set, replaces every file size: a quick run while working on this.
std::uint64_t sized(std::uint64_t size) {
    const char* env = std::getenv("MOD_STRESS_BYTES");
    return env != nullptr && *env != '\0' ? std::stoull(env) : size;
}
const std::string kCtrlEnd = "\x1b[1;5F";
const std::string kCtrlHome = "\x1b[1;5H";
const std::string kPageDown = "\x1b[6~";
const std::string kPageUp = "\x1b[5~";
const std::string kEnter = "\r";
const std::string kEscape = "\x1b";

fs::path stress_dir() {
    const char* env = std::getenv("MOD_STRESS_DIR");
    fs::path dir = env != nullptr && *env != '\0' ? fs::path(env) : fs::path(MOD_TEST_SCRATCH) / "stress";
    fs::create_directories(dir);
    return dir;
}

// The file, its sidecar and the config folder, removed however the test ends.
struct Scratch {
    fs::path file;
    fs::path config;
    explicit Scratch(const std::string& name) : file(stress_dir() / name), config(stress_dir() / ("config-" + name)) {
        remove_all();
        fs::create_directories(config);
    }
    ~Scratch() { remove_all(); }
    void remove_all() const {
        std::error_code ec;
        fs::remove(file, ec);
        fs::remove(file.parent_path() / ("." + file.filename().string() + ".history"), ec);
        fs::remove_all(config, ec);
    }
};

// Ordinary text: numbered lines of about 70 bytes.
std::string text_line(std::uint64_t i) { return std::format("line {:09} the quick brown fox jumps over the lazy dog {:04x}\n", i, (i * 2654435761u) & 0xFFFF); }

// Writes lines until the file holds at least `size` bytes; returns the line count.
std::uint64_t write_text(const fs::path& p, std::uint64_t size) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    std::string buf;
    std::uint64_t written = 0;
    std::uint64_t lines = 0;
    while (written < size) {
        buf += text_line(lines++);
        if (buf.size() >= (8u << 20)) {
            out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
            written += buf.size();
            buf.clear();
        }
    }
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    REQUIRE(out.good());
    return lines;
}

// Random bytes from a fixed seed, with `marker` planted at `marker_at`.
class BinaryStream {
public:
    static constexpr std::string_view marker = "PLANTED-MARKER-7f3a";
    BinaryStream(std::uint64_t size, std::uint64_t marker_at) : size_(size), marker_at_(marker_at) {}
    // The next `n` bytes (fewer at the end).
    std::string next(std::uint64_t n) {
        n = std::min(n, size_ - pos_);
        std::string out(n, '\0');
        for (std::uint64_t k = 0; k < n; ++k) {
            const std::uint64_t at = pos_ + k;
            if (at >= marker_at_ && at < marker_at_ + marker.size()) {
                out[k] = marker[at - marker_at_];
            } else {
                state_ ^= state_ << 13;
                state_ ^= state_ >> 7;
                state_ ^= state_ << 17;
                out[k] = static_cast<char>(state_ >> 56);
            }
        }
        pos_ += n;
        return out;
    }

private:
    std::uint64_t size_;
    std::uint64_t marker_at_;
    std::uint64_t pos_ = 0;
    std::uint64_t state_ = 0x9E3779B97F4A7C15ull;
};

// Compares the file with `expected`, a source of its bytes in order.
bool file_equals(const fs::path& p, std::uint64_t size, const std::function<std::string(std::uint64_t)>& expected) {
    if (fs::file_size(p) != size) return false;
    std::ifstream in(p, std::ios::binary);
    std::string got(8u << 20, '\0');
    for (std::uint64_t done = 0; done < size;) {
        const std::uint64_t n = std::min<std::uint64_t>(got.size(), size - done);
        in.read(got.data(), static_cast<std::streamsize>(n));
        if (static_cast<std::uint64_t>(in.gcount()) != n) return false;
        if (std::string_view(got.data(), n) != expected(n)) return false;
        done += n;
    }
    return true;
}

struct Position {
    std::uint64_t line = 0;
    std::uint64_t column = 0;
    std::uint64_t count = 0;  // 0 while the line count is not known
};

// The status line's "line:column / count".
std::optional<Position> position(const VtScreen& s) {
    static const std::regex re(R"((\d+):(\d+)(?: / (\d+))?)");
    std::smatch m;
    const std::string status = s.status();
    if (!std::regex_search(status, m, re)) return std::nullopt;
    return Position{std::stoull(m[1]), std::stoull(m[2]), m[3].matched ? std::stoull(m[3]) : 0};
}

auto at_line(std::uint64_t line) {
    return [line](const VtScreen& s) { return position(s) && position(s)->line == line; };
}

void run_script(const Scratch& scratch, std::vector<ScriptStep> steps, std::uint64_t anon_limit) {
    ::setenv("XDG_CONFIG_HOME", scratch.config.c_str(), 1);
    ::setenv("TERM", "xterm-256color", 1);
    CliOptions options;
    options.paths = {scratch.file};
    auto owned = std::make_unique<ScriptedTerminal>(24, 100, std::move(steps), time_budget(1.0));
    ScriptedTerminal* term = owned.get();
    const std::uint64_t anon_before = anonymous_resident_bytes();
    {
        App app(options, std::move(owned));
        CHECK(app.run() == 0);
        for (const ScriptStep& s : term->steps()) {
            if (s.timed_out) {
                std::string shown;
                for (int r = 0; r < term->screen().rows(); ++r) shown += term->screen().row(r) + "\n";
                MESSAGE(std::format("{} timed out; the screen:\n{}", s.name, shown));
            }
            CAPTURE(s.name);
            CAPTURE(term->screen().status());
            CHECK_FALSE(s.timed_out);
            CHECK(s.done);
            MESSAGE(std::format("{}: {:.2f} s", s.name, s.took.count()));
        }
        const std::uint64_t grew = term->peak_anonymous_bytes() > anon_before ? term->peak_anonymous_bytes() - anon_before : 0;
        MESSAGE(std::format("peak anonymous memory: +{} MiB", grew >> 20));
        CHECK(grew < anon_limit);
    }
}

void text_file_session(std::uint64_t size) {
    Scratch scratch(std::format("text-{}.txt", size));
    size = sized(size);
    const std::uint64_t lines = write_text(scratch.file, size);
    const std::uint64_t original = fs::file_size(scratch.file);
    const std::uint64_t middle = lines / 2;           // 1-based line numbers below
    const std::uint64_t needle_line = lines - 5;
    const std::string needle = std::format("line {:09} ", needle_line - 1);
    // About ten times what a desktop with an SSD takes (3 s, 0.3 s and 7 s a GB), for slower disks.
    const auto gib = static_cast<double>(size) / static_cast<double>(kGiB);
    const auto per_gib = [gib](std::chrono::seconds s) { return std::chrono::duration_cast<std::chrono::milliseconds>(s * std::max(1.0, gib)); };
    std::vector<ScriptStep> steps;
    steps.push_back({"open: the line count is known", "", [&](const VtScreen& s) { return position(s) && position(s)->count >= lines; }, per_gib(30s)});
    steps.push_back({"Ctrl+End to the last line", kCtrlEnd, at_line(lines + 1), 2min});
    steps.push_back({"Ctrl+G to the middle line", "\x07" + std::to_string(middle) + kEnter, at_line(middle), 2min});
    steps.push_back({"the middle line is on screen", "", [&](const VtScreen& s) { return s.contains(std::format("line {:09} the quick", middle - 1)); }, 1min});
    steps.push_back({"type at the middle line", "EDIT ", [&](const VtScreen& s) { return position(s) && position(s)->column == 6 && s.contains("EDIT line"); }, 1min});
    steps.push_back({"Ctrl+F to a line near the end", "\x06" + needle + kEnter, at_line(needle_line), per_gib(10s)});
    steps.push_back({"close the find bar", kEscape, nullptr, 1min});
    steps.push_back({"Ctrl+S saves", "\x13", [](const VtScreen& s) { return s.status().find("saved ") != std::string::npos; }, per_gib(70s)});
    steps.push_back({"Ctrl+Q quits", "\x11", nullptr, 1min});
    run_script(scratch, std::move(steps), kAnonymousLimit);

    // On disk: the original with "EDIT " at the start of the middle line, nothing else.
    std::uint64_t next_line = 0;
    std::string pending;
    auto expected = [&](std::uint64_t n) {
        while (pending.size() < n && next_line < lines) {
            if (next_line == middle - 1) pending += "EDIT ";
            pending += text_line(next_line++);
        }
        std::string out = pending.substr(0, n);
        pending.erase(0, n);
        return out;
    };
    CHECK(file_equals(scratch.file, original + 5, expected));
}

}  // namespace

TEST_CASE("a 1 GB text file: open, move, edit, search, save, quit") { text_file_session(kGiB); }

TEST_CASE("a 4 GB text file: open, move, edit, search, save, quit") { text_file_session(4 * kGiB); }

TEST_CASE("a 1 GB binary file opens as text, holds together, and saves back unchanged") {
    Scratch scratch("random-1g.bin");
    const std::uint64_t size = sized(kGiB);
    const std::uint64_t marker_at = size / 4 * 3;
    std::uint64_t marker_line = 1;
    {
        std::ofstream out(scratch.file, std::ios::binary | std::ios::trunc);
        BinaryStream gen(size, marker_at);
        for (std::uint64_t done = 0; done < size;) {
            const std::string chunk = gen.next(8u << 20);
            for (std::size_t k = 0; k < chunk.size() && done + k < marker_at; ++k) marker_line += chunk[k] == '\n';
            out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            done += chunk.size();
        }
        REQUIRE(out.good());
    }
    // Nothing drawn may be anything but UTF-8, whatever the bytes in the file.
    bool drawn_valid = true;
    auto all_valid = [&drawn_valid](const VtScreen& s) {
        for (int r = 0; r < s.rows(); ++r) {
            for (int c = 0; c < s.cols(); ++c) {
                const std::string& cell = s.cell(r, c);
                for (std::size_t i = 0; i < cell.size();) {
                    const Decoded d = decode(std::as_bytes(std::span(cell.data() + i, cell.size() - i)));
                    if (!d.valid || d.cp < 0x20 || (d.cp >= 0x7F && d.cp <= 0x9F)) drawn_valid = false;
                    i += std::max<std::size_t>(1, d.len);
                }
            }
        }
        return true;
    };
    auto checked = [&](auto pred) { return [&, pred](const VtScreen& s) { return all_valid(s) && pred(s); }; };
    std::vector<ScriptStep> steps;
    steps.push_back({"open: the line count is known", "", checked([](const VtScreen& s) { return position(s) && position(s)->count > 0; }), 30s});
    std::string pages;
    for (int i = 0; i < 20; ++i) pages += kPageDown;
    // Lines of random bytes wrap over several rows, so a page is a few lines.
    std::uint64_t paged_to = 0;
    steps.push_back({"twenty pages down", pages, checked([&paged_to](const VtScreen& s) {
                         if (!position(s) || position(s)->line < 20) return false;
                         paged_to = position(s)->line;
                         return true;
                     }),
                     1min});
    steps.push_back({"five pages up", kPageUp + kPageUp + kPageUp + kPageUp + kPageUp,
                     checked([&paged_to](const VtScreen& s) { return position(s) && position(s)->line < paged_to && position(s)->line > 1; }), 1min});
    steps.push_back({"Ctrl+End", kCtrlEnd, checked([](const VtScreen& s) { return position(s) && position(s)->count > 0 && position(s)->line == position(s)->count; }), 2min});
    steps.push_back({"Ctrl+Home", kCtrlHome, checked(at_line(1)), 1min});
    steps.push_back({"Ctrl+F to the planted marker", "\x06" + std::string(BinaryStream::marker) + kEnter, checked(at_line(marker_line)), 10s});
    steps.push_back({"close the find bar", kEscape, nullptr, 1min});
    steps.push_back({"type a byte and take it back", kCtrlHome + "X\x7f", checked([](const VtScreen& s) { return position(s) && position(s)->line == 1 && position(s)->column == 1; }), 1min});
    steps.push_back({"Ctrl+S saves", "\x13", [](const VtScreen& s) { return s.status().find("saved ") != std::string::npos; }, 80s});
    steps.push_back({"Ctrl+Q quits", "\x11", nullptr, 1min});
    run_script(scratch, std::move(steps), kAnonymousLimit);
    CHECK(drawn_valid);

    BinaryStream again(size, marker_at);
    CHECK(file_equals(scratch.file, size, [&](std::uint64_t n) { return again.next(n); }));
}
