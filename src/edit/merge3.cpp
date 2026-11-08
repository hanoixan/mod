#include "edit/merge3.hpp"

#include <algorithm>

namespace mod {

// ---- line hashes -------------------------------------------------------------------------

void LineIndexer::feed(std::span<const std::byte> bytes) {
    for (const std::byte b : bytes) {
        h_ = (h_ ^ std::to_integer<std::uint64_t>(b)) * 0x100000001b3ull;
        open_ = true;
        ++at_;
        if (b == std::byte{'\n'}) {
            index_.hash.push_back(h_);
            index_.start.push_back(at_);
            h_ = kSeed;
            open_ = false;
        }
    }
}

LineIndex LineIndexer::finish() {
    if (open_) {
        // The last line, without its line feed, never equals one with it.
        index_.hash.push_back(h_ ^ 0x9e3779b97f4a7c15ull);
        index_.start.push_back(at_);
        open_ = false;
    }
    return std::move(index_);
}

// ---- diff ----------------------------------------------------------------------------------

namespace {

struct Differ {
    std::span<const std::uint64_t> a, b;
    std::stop_token stop;
    std::atomic<std::size_t>* progress;
    std::vector<LineEdit> out;
    std::vector<std::ptrdiff_t> vf, vb;
    bool stopped = false;

    void settle(std::size_t n) {
        if (progress != nullptr && n > 0) progress->fetch_add(n, std::memory_order_relaxed);
    }

    void emit(std::size_t a0, std::size_t a1, std::size_t b0, std::size_t b1) {
        if (a0 == a1 && b0 == b1) return;
        if (!out.empty() && out.back().a1 == a0 && out.back().b1 == b0) {
            out.back().a1 = a1;
            out.back().b1 = b1;
        } else {
            out.push_back({a0, a1, b0, b1});
        }
        settle(a1 - a0);
    }

    // The middle snake of a[x0, x1) against b[y0, y1), both non-empty: from (sx, sy) to (ux, uy).
    void middle(std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1, std::size_t& sx, std::size_t& sy, std::size_t& ux, std::size_t& uy) {
        const auto n = static_cast<std::ptrdiff_t>(x1 - x0);
        const auto m = static_cast<std::ptrdiff_t>(y1 - y0);
        const std::ptrdiff_t delta = n - m;
        const bool odd = (delta & 1) != 0;
        const std::ptrdiff_t max = (n + m + 1) / 2 + 1;
        const std::ptrdiff_t off = max;
        vf.assign(static_cast<std::size_t>(2 * max + 2), 0);
        vb.assign(static_cast<std::size_t>(2 * max + 2), 0);
        auto F = [&](std::ptrdiff_t k) -> std::ptrdiff_t& { return vf[static_cast<std::size_t>(k + off)]; };
        auto B = [&](std::ptrdiff_t k) -> std::ptrdiff_t& { return vb[static_cast<std::size_t>(k + off)]; };
        auto A = [&](std::ptrdiff_t i) { return a[x0 + static_cast<std::size_t>(i)]; };
        auto Bv = [&](std::ptrdiff_t j) { return b[y0 + static_cast<std::size_t>(j)]; };
        for (std::ptrdiff_t d = 0; d < max; ++d) {
            if ((d & 63) == 0 && stop.stop_requested()) {
                stopped = true;
                return;
            }
            for (std::ptrdiff_t k = -d; k <= d; k += 2) {
                std::ptrdiff_t x = (k == -d || (k != d && F(k - 1) < F(k + 1))) ? F(k + 1) : F(k - 1) + 1;
                std::ptrdiff_t y = x - k;
                const std::ptrdiff_t xs = x;
                const std::ptrdiff_t ys = y;
                while (x < n && y < m && A(x) == Bv(y)) ++x, ++y;
                F(k) = x;
                const std::ptrdiff_t kb = delta - k;
                if (odd && kb >= -(d - 1) && kb <= d - 1 && F(k) + B(kb) >= n) {
                    sx = x0 + static_cast<std::size_t>(xs);
                    sy = y0 + static_cast<std::size_t>(ys);
                    ux = x0 + static_cast<std::size_t>(x);
                    uy = y0 + static_cast<std::size_t>(y);
                    return;
                }
            }
            for (std::ptrdiff_t k = -d; k <= d; k += 2) {
                std::ptrdiff_t x = (k == -d || (k != d && B(k - 1) < B(k + 1))) ? B(k + 1) : B(k - 1) + 1;
                std::ptrdiff_t y = x - k;
                const std::ptrdiff_t xs = x;
                const std::ptrdiff_t ys = y;
                while (x < n && y < m && A(n - 1 - x) == Bv(m - 1 - y)) ++x, ++y;
                B(k) = x;
                const std::ptrdiff_t kf = delta - k;
                if (!odd && kf >= -d && kf <= d && B(k) + F(kf) >= n) {
                    sx = x0 + static_cast<std::size_t>(n - x);
                    sy = y0 + static_cast<std::size_t>(m - y);
                    ux = x0 + static_cast<std::size_t>(n - xs);
                    uy = y0 + static_cast<std::size_t>(m - ys);
                    return;
                }
            }
        }
        // Not reached for non-empty inputs; treat as a full replacement.
        sx = ux = x1;
        sy = uy = y1;
    }

    void run(std::size_t x0, std::size_t x1, std::size_t y0, std::size_t y1) {
        if (stopped) return;
        // Common prefix and suffix.
        std::size_t p = 0;
        while (x0 + p < x1 && y0 + p < y1 && a[x0 + p] == b[y0 + p]) ++p;
        settle(p);
        x0 += p;
        y0 += p;
        std::size_t s = 0;
        while (x1 - s > x0 && y1 - s > y0 && a[x1 - 1 - s] == b[y1 - 1 - s]) ++s;
        x1 -= s;
        y1 -= s;
        if (x0 == x1 || y0 == y1) {
            emit(x0, x1, y0, y1);
            settle(s);
            return;
        }
        std::size_t sx = 0, sy = 0, ux = 0, uy = 0;
        middle(x0, x1, y0, y1, sx, sy, ux, uy);
        if (stopped) return;
        if (sx == x1 && sy == y1 && ux == x1) {
            emit(x0, x1, y0, y1);
        } else {
            run(x0, sx, y0, sy);
            settle(ux - sx);
            run(ux, x1, uy, y1);
        }
        settle(s);
    }
};

}  // namespace

std::optional<std::vector<LineEdit>> diff_lines(std::span<const std::uint64_t> a, std::span<const std::uint64_t> b, std::stop_token stop,
                                                std::atomic<std::size_t>* progress) {
    Differ d{a, b, std::move(stop), progress, {}, {}, {}, false};
    d.run(0, a.size(), 0, b.size());
    if (d.stopped) return std::nullopt;
    return std::move(d.out);
}

// ---- diff3 ---------------------------------------------------------------------------------

std::optional<std::vector<MergeHunk>> merge3(std::span<const std::uint64_t> base, std::span<const std::uint64_t> ours,
                                             std::span<const std::uint64_t> theirs, const std::stop_token& stop, std::atomic<std::size_t>* progress) {
    auto mine = diff_lines(base, ours, stop, progress);
    if (!mine) return std::nullopt;
    auto other = diff_lines(base, theirs, stop, progress);
    if (!other) return std::nullopt;
    // Where a base line lands in a side, given that side's edits: the edits wholly before it
    // shift it by their growth.
    struct Side {
        const std::vector<LineEdit>& edits;
        std::size_t next = 0;           // first edit not yet passed
        std::ptrdiff_t shift = 0;       // the growth of the edits passed
    };
    Side o{*mine};
    Side t{*other};
    std::vector<MergeHunk> out;
    auto seq_equal = [](std::span<const std::uint64_t> x, std::span<const std::uint64_t> y) { return std::ranges::equal(x, y); };
    while (o.next < o.edits.size() || t.next < t.edits.size()) {
        // The next chunk: start from the edit that comes first, then take in every edit of
        // either side that overlaps it, or touches it at a point where one of them inserts.
        const bool from_o = t.next >= t.edits.size() || (o.next < o.edits.size() && o.edits[o.next].a0 <= t.edits[t.next].a0);
        std::size_t lo = from_o ? o.edits[o.next].a0 : t.edits[t.next].a0;
        std::size_t hi = from_o ? o.edits[o.next].a1 : t.edits[t.next].a1;
        std::size_t oi = o.next, ti = t.next;  // the chunk's edits: [o.next, oi) and [t.next, ti)
        auto touches = [&](const LineEdit& e) { return e.a0 < hi || (e.a0 == hi && (e.a0 == e.a1 || lo == hi)); };
        for (bool grew = true; grew;) {
            grew = false;
            while (oi < o.edits.size() && touches(o.edits[oi])) {
                hi = std::max(hi, o.edits[oi].a1);
                lo = std::min(lo, o.edits[oi].a0);
                ++oi;
                grew = true;
            }
            while (ti < t.edits.size() && touches(t.edits[ti])) {
                hi = std::max(hi, t.edits[ti].a1);
                lo = std::min(lo, t.edits[ti].a0);
                ++ti;
                grew = true;
            }
        }
        // The chunk's range in each side: the base range shifted by the edits before it, and
        // grown by the chunk's own.
        auto range_in = [&](Side& s, std::size_t end_index, std::size_t& r0, std::size_t& r1) {
            r0 = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(lo) + s.shift);
            std::ptrdiff_t grow = 0;
            for (std::size_t i = s.next; i < end_index; ++i) {
                const LineEdit& e = s.edits[i];
                grow += static_cast<std::ptrdiff_t>(e.b1 - e.b0) - static_cast<std::ptrdiff_t>(e.a1 - e.a0);
            }
            r1 = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(hi) + s.shift + grow);
            s.shift += grow;
            s.next = end_index;
        };
        const bool ours_changed = oi > o.next;
        const bool theirs_changed = ti > t.next;
        std::size_t o0 = 0, o1 = 0, t0 = 0, t1 = 0;
        range_in(o, oi, o0, o1);
        range_in(t, ti, t0, t1);
        if (!theirs_changed) continue;  // ours alone: already in the text
        if (!ours_changed) {
            out.push_back({MergeHunk::take_theirs, lo, hi, o0, o1, t0, t1});
            continue;
        }
        if (seq_equal(ours.subspan(o0, o1 - o0), theirs.subspan(t0, t1 - t0))) continue;  // the same change on both sides
        out.push_back({MergeHunk::conflict, lo, hi, o0, o1, t0, t1});
    }
    return out;
}

}  // namespace mod
