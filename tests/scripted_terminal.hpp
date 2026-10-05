#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <regex>
#include <span>
#include <stop_token>
#include <thread>
#include <string>
#include <string_view>
#include <vector>

#include "platform/terminal.hpp"
#include "text/utf8.hpp"

namespace mod::testing {

// The screen a terminal would show for what mod writes: the cursor moves, clears and text
// its Screen sends, and of the attributes only dim; modes are ignored. One UTF-8 string per cell, a wide
// character's second cell empty.
class VtScreen {
public:
    VtScreen(int rows, int cols)
        : rows_(rows), cols_(cols), cells_(static_cast<std::size_t>(rows * cols), " "), dim_(static_cast<std::size_t>(rows * cols), false) {}

    void feed(std::string_view bytes) {
        pending_ += bytes;
        std::size_t i = 0;
        while (i < pending_.size()) {
            const auto c = static_cast<unsigned char>(pending_[i]);
            if (c == 0x1b) {
                const std::size_t used = escape(i);
                if (used == 0) break;  // incomplete: wait for the rest
                i += used;
                continue;
            }
            if (c < 0x20) {
                ++i;
                continue;
            }
            const Decoded d = decode(std::as_bytes(std::span(pending_.data() + i, pending_.size() - i)));
            if (d.len == 0 || (!d.valid && pending_.size() - i < 4 && c >= 0xC0)) break;
            const std::size_t len = std::max<std::size_t>(1, d.len);
            put(pending_.substr(i, len), d.valid ? std::max(0, display_width(d, 0, 1)) : 1);
            i += len;
        }
        pending_.erase(0, i);
    }

    const std::string& cell(int r, int c) const { return cells_[static_cast<std::size_t>(r * cols_ + c)]; }
    bool dim(int r, int c) const { return dim_[static_cast<std::size_t>(r * cols_ + c)]; }
    std::string row(int r) const {
        std::string out;
        for (int c = 0; c < cols_; ++c) out += cell(r, c);
        return out;
    }
    // The focused view's status line: the lowest row showing a position (line:column), which a
    // prompt, question or menu bar below it pushes up; else the last row.
    std::string status() const {
        static const std::regex position(R"(\d+:\d+)");
        for (int r = rows_ - 1; r >= 0; --r) {
            std::string text = row(r);
            if (std::regex_search(text, position)) return text;
        }
        return row(rows_ - 1);
    }
    bool contains(std::string_view text) const {
        for (int r = 0; r < rows_; ++r) {
            if (row(r).find(text) != std::string::npos) return true;
        }
        return false;
    }
    int rows() const { return rows_; }
    // Every title set (OSC 0), in order.
    const std::vector<std::string>& titles() const { return titles_; }
    int cols() const { return cols_; }

private:
    // The length of the sequence at pending_[i], or 0 while it is incomplete.
    std::size_t escape(std::size_t i) {
        if (i + 1 >= pending_.size()) return 0;
        const char kind = pending_[i + 1];
        if (kind == ']') {  // OSC (a title, the clipboard): up to BEL or ESC backslash
            for (std::size_t j = i + 2; j < pending_.size(); ++j) {
                const bool bel = pending_[j] == '\a';
                if (bel || (pending_[j] == '\x1b' && j + 1 < pending_.size() && pending_[j + 1] == '\\')) {
                    const std::string_view body(pending_.data() + i + 2, j - i - 2);
                    if (body.starts_with("0;")) titles_.emplace_back(body.substr(2));
                    return j + (bel ? 1 : 2) - i;
                }
            }
            return 0;
        }
        if (kind != '[') return 2;
        std::size_t j = i + 2;
        while (j < pending_.size() && (pending_[j] < 0x40 || pending_[j] > 0x7E)) ++j;
        if (j >= pending_.size()) return 0;
        const std::string params = pending_.substr(i + 2, j - i - 2);
        const char final = pending_[j];
        if (final == 'H') {
            int r = 1;
            int c = 1;
            if (const auto semi = params.find(';'); semi != std::string::npos) {
                r = std::max(1, std::atoi(params.c_str()));
                c = std::max(1, std::atoi(params.c_str() + semi + 1));
            }
            row_ = std::min(rows_ - 1, r - 1);
            col_ = std::min(cols_ - 1, c - 1);
        } else if (final == 'J' && params == "2") {
            std::fill(cells_.begin(), cells_.end(), " ");
            std::fill(dim_.begin(), dim_.end(), false);
        } else if (final == 'm') {
            // SGR: 0 (or nothing) resets, 2 is dim, 22 normal intensity; the rest is ignored.
            std::size_t from = 0;
            if (params.empty()) attr_dim_ = false;
            while (from <= params.size() && !params.empty()) {
                const std::size_t semi = std::min(params.find(';', from), params.size());
                const std::string_view p(params.data() + from, semi - from);
                if (p.empty() || p == "0" || p == "22") attr_dim_ = false;
                if (p == "2") attr_dim_ = true;
                if (p == "38" || p == "48") break;  // a 256-color or RGB color follows
                from = semi + 1;
            }
        }
        return j + 1 - i;
    }

    void put(const std::string& glyph, int width) {
        if (width == 0) {
            if (col_ > 0) cells_[static_cast<std::size_t>(row_ * cols_ + col_ - 1)] += glyph;
            return;
        }
        if (col_ >= cols_) return;
        cells_[static_cast<std::size_t>(row_ * cols_ + col_)] = glyph;
        dim_[static_cast<std::size_t>(row_ * cols_ + col_)] = attr_dim_;
        if (width == 2 && col_ + 1 < cols_) cells_[static_cast<std::size_t>(row_ * cols_ + col_ + 1)] = "";
        col_ += width;
    }

    int rows_;
    int cols_;
    std::vector<std::string> cells_;
    std::vector<bool> dim_;
    bool attr_dim_ = false;
    int row_ = 0;
    int col_ = 0;
    std::string pending_;
    std::vector<std::string> titles_;
};

// The process's anonymous resident memory (heap and stacks, not mapped files), in bytes.
inline std::uint64_t anonymous_resident_bytes() {
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with("RssAnon:")) return std::stoull(line.substr(8)) * 1024;
    }
    return 0;
}

// One step of a script: keys typed, then the screen awaited.
struct ScriptStep {
    std::string name;
    std::string keys;
    std::function<bool(const VtScreen&)> until;  // null: done once the keys are read
    std::chrono::milliseconds limit{std::chrono::seconds(60)};
    // A step whose keys end in Esc waits this long first: a lone Esc is only Esc once the
    // decoder's wait for more has passed. Longer than App's gap between quitting Escapes, so
    // steps that each end in Esc never add up to a quit unless a test shortens it.
    std::chrono::milliseconds esc_settle{300};
    // Filled in as the script runs.
    std::chrono::duration<double> took{};
    bool done = false;
    bool timed_out = false;
};

// A terminal that plays a script against App's real event loop: each step's keys are
// read as input, and the next step starts once the screen App drew satisfies the step's
// `until`. A step past its limit ends the session as SIGTERM would, so the test can
// report it. Single-threaded apart from wake(), which background work calls.
class ScriptedTerminal : public Terminal {
public:
    // `limit_scale` multiplies every step's limit (a slower build, a smaller file).
    ScriptedTerminal(int rows, int cols, std::vector<ScriptStep> steps, double limit_scale = 1.0)
        : screen_(rows, cols), steps_(std::move(steps)), limit_scale_(limit_scale) {
        // Sampled apart from the loop too: a long save never waits.
        sampler_ = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::uint64_t seen = anonymous_resident_bytes();
                std::uint64_t peak = peak_anonymous_.load();
                while (seen > peak && !peak_anonymous_.compare_exchange_weak(peak, seen)) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }
    ~ScriptedTerminal() override { sampler_.request_stop(); }

    Status enter_raw_mode() override { return {}; }
    void restore() noexcept override {}
    TerminalSize size() override { return {screen_.rows(), screen_.cols()}; }
    Status write(std::span<const std::byte> bytes) override {
        screen_.feed(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        return {};
    }
    Result<std::size_t> read_input(std::span<std::byte> buf) override {
        const std::size_t n = std::min(buf.size(), input_.size());
        std::memcpy(buf.data(), input_.data(), n);
        input_.erase(0, n);
        // A step that awaits nothing is done once its keys are read (the last may quit,
        // and App then never waits again).
        if (input_.empty() && started_ && next_ < steps_.size()) {
            ScriptStep& s = steps_[next_];
            if (!s.until && !s.keys.ends_with('\x1b')) {
                s.done = true;
                s.took = std::chrono::steady_clock::now() - step_start_;
            }
        }
        return n;
    }
    void wake() noexcept override {
        {
            std::lock_guard lock(mutex_);
            woken_ = true;
        }
        cv_.notify_one();
    }

    WaitEvents wait(int timeout_ms) override {
        if (!input_.empty()) return input_ready;
        const auto now = std::chrono::steady_clock::now();
        while (next_ < steps_.size()) {
            ScriptStep& s = steps_[next_];
            if (!started_) {
                started_ = true;
                step_start_ = now;
                input_ = s.keys;
                if (!input_.empty()) return input_ready;
            }
            const bool settling = s.keys.ends_with('\x1b') && now - step_start_ < s.esc_settle;
            if (!settling && (!s.until || s.until(screen_))) {
                s.took = now - step_start_;
                s.done = true;
                ++next_;
                started_ = false;
                continue;
            }
            if (now - step_start_ > s.limit * limit_scale_) {
                s.timed_out = true;
                return terminated;
            }
            break;
        }
        if (next_ >= steps_.size()) return terminated;  // the script is over: end in order
        std::unique_lock lock(mutex_);
        const auto slice = std::chrono::milliseconds(timeout_ms < 0 ? 20 : std::min(timeout_ms, 20));
        cv_.wait_for(lock, slice, [&] { return woken_; });
        if (woken_) {
            woken_ = false;
            return woken;
        }
        return timed_out;
    }

    const VtScreen& screen() const { return screen_; }
    const std::vector<ScriptStep>& steps() const { return steps_; }
    std::uint64_t peak_anonymous_bytes() const { return peak_anonymous_.load(); }

private:
    VtScreen screen_;
    std::vector<ScriptStep> steps_;
    std::size_t next_ = 0;
    bool started_ = false;
    std::chrono::steady_clock::time_point step_start_;
    std::string input_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool woken_ = false;
    std::atomic<std::uint64_t> peak_anonymous_{0};
    double limit_scale_;
    std::jthread sampler_;  // last: stopped and joined before the members it reads go
};

}  // namespace mod::testing
