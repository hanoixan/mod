#include "text/utf8.hpp"

#include <algorithm>
#include <iterator>

namespace mod {
namespace {

// Character classes, grapheme-break properties and flags used by width_table.inc.
enum : std::uint8_t { CC_SPACE, CC_WORD, CC_PUNCT, CC_CONTROL };
enum : std::uint8_t {
    GB_OTHER,
    GB_CR,
    GB_LF,
    GB_CONTROL,
    GB_EXTEND,
    GB_ZWJ,
    GB_REGIONAL_INDICATOR,
    GB_PREPEND,
    GB_SPACINGMARK,
    GB_L,
    GB_V,
    GB_T,
    GB_LV,
    GB_LVT,
};
enum : std::uint8_t {
    GF_EXTPICT = 1u << 0,
    GF_INCB_LINKER = 1u << 1,
    GF_INCB_CONSONANT = 1u << 2,
    GF_INCB_EXTEND = 1u << 3,
};

struct Range {
    char32_t first;
    char32_t last;
    std::uint8_t width;
    std::uint8_t cls;
    std::uint8_t gcb;
    std::uint8_t flags;
};

constexpr Range kTable[] = {
#include "text/width_table.inc"
};

constexpr bool table_is_sorted() {
    for (std::size_t i = 0; i + 1 < std::size(kTable); ++i)
        if (!(kTable[i].first <= kTable[i].last && kTable[i].last < kTable[i + 1].first)) return false;
    return true;
}
static_assert(table_is_sorted(), "width_table.inc must be sorted and disjoint");

struct Props {
    std::uint8_t width = 1;
    std::uint8_t cls = CC_WORD;
    std::uint8_t gcb = GB_OTHER;
    std::uint8_t flags = 0;
};

Props lookup(char32_t cp) {
    const auto* it = std::upper_bound(std::begin(kTable), std::end(kTable), cp,
                                      [](char32_t c, const Range& r) { return c < r.first; });
    if (it == std::begin(kTable)) return {};
    --it;
    if (cp > it->last) return {};
    return {it->width, it->cls, it->gcb, it->flags};
}

// Invalid bytes behave as Control for segmentation.
Props props_of(const Decoded& d) {
    if (!d.valid) return {4, CC_PUNCT, GB_CONTROL, 0};
    return lookup(d.cp);
}

bool is_cont(std::byte b) { return (std::to_integer<unsigned>(b) & 0xC0u) == 0x80u; }

// True when `bytes` is a proper prefix of a sequence that could still decode as valid,
// so the decision has to wait for more bytes.
bool truncated_prefix(std::span<const std::byte> bytes) {
    if (bytes.empty()) return false;
    const unsigned b0 = std::to_integer<unsigned>(bytes[0]);
    std::size_t need;
    if (b0 >= 0xC2 && b0 <= 0xDF) need = 2;
    else if (b0 >= 0xE0 && b0 <= 0xEF) need = 3;
    else if (b0 >= 0xF0 && b0 <= 0xF4) need = 4;
    else return false;
    if (bytes.size() >= need) return false;
    for (std::size_t i = 1; i < bytes.size(); ++i)
        if (!is_cont(bytes[i])) return false;
    if (bytes.size() >= 2) {
        const unsigned b1 = std::to_integer<unsigned>(bytes[1]);
        if (b0 == 0xE0 && b1 < 0xA0) return false;
        if (b0 == 0xED && b1 > 0x9F) return false;
        if (b0 == 0xF0 && b1 < 0x90) return false;
        if (b0 == 0xF4 && b1 > 0x8F) return false;
    }
    return true;
}

bool is_control_like(std::uint8_t gcb) { return gcb == GB_CONTROL || gcb == GB_CR || gcb == GB_LF; }

// Forward segmentation state over the code points seen so far.
struct SegState {
    Props prev;
    bool ri_odd = false;     // the trailing run of regional indicators has odd length
    std::uint8_t ep = 0;     // 1: ExtPict Extend* seen; 2: ... followed by ZWJ
    std::uint8_t incb = 0;   // 1: InCB Consonant [Extend Linker]*; 2: with a Linker in it

    void start(const Props& p) {
        prev = p;
        ri_odd = p.gcb == GB_REGIONAL_INDICATOR;
        ep = (p.flags & GF_EXTPICT) ? 1 : 0;
        incb = (p.flags & GF_INCB_CONSONANT) ? 1 : 0;
    }

    bool breaks_before(const Props& c) const {
        const auto p = prev.gcb;
        if (p == GB_CR && c.gcb == GB_LF) return false;                                     // GB3
        if (is_control_like(p)) return true;                                                // GB4
        if (is_control_like(c.gcb)) return true;                                            // GB5
        if (p == GB_L && (c.gcb == GB_L || c.gcb == GB_V || c.gcb == GB_LV || c.gcb == GB_LVT)) return false;  // GB6
        if ((p == GB_LV || p == GB_V) && (c.gcb == GB_V || c.gcb == GB_T)) return false;    // GB7
        if ((p == GB_LVT || p == GB_T) && c.gcb == GB_T) return false;                      // GB8
        if (c.gcb == GB_EXTEND || c.gcb == GB_ZWJ) return false;                            // GB9
        if (c.gcb == GB_SPACINGMARK) return false;                                          // GB9a
        if (p == GB_PREPEND) return false;                                                  // GB9b
        if ((c.flags & GF_INCB_CONSONANT) && incb == 2) return false;                       // GB9c
        if (p == GB_ZWJ && ep == 2 && (c.flags & GF_EXTPICT)) return false;                 // GB11
        if (p == GB_REGIONAL_INDICATOR && c.gcb == GB_REGIONAL_INDICATOR && ri_odd) return false;  // GB12, GB13
        return true;                                                                        // GB999
    }

    void advance(const Props& c) {
        ri_odd = c.gcb == GB_REGIONAL_INDICATOR && !(prev.gcb == GB_REGIONAL_INDICATOR && ri_odd);
        if (c.flags & GF_EXTPICT) ep = 1;
        else if (ep == 1 && c.gcb == GB_EXTEND) ep = 1;
        else if (ep == 1 && c.gcb == GB_ZWJ) ep = 2;
        else ep = 0;
        if (c.flags & GF_INCB_CONSONANT) incb = 1;
        else if (incb != 0 && (c.flags & GF_INCB_LINKER)) incb = 2;
        else if (incb != 0 && (c.flags & GF_INCB_EXTEND)) {
        } else incb = 0;
        prev = c;
    }
};

// A break between `a` and `b` that no earlier context can remove.
bool certain_break(const Props& a, const Props& b) {
    SegState s;
    s.prev = a;
    if (!s.breaks_before(b)) return false;
    if ((b.flags & GF_INCB_CONSONANT) && (a.flags & (GF_INCB_LINKER | GF_INCB_EXTEND))) return false;  // GB9c
    if (a.gcb == GB_ZWJ && (b.flags & GF_EXTPICT)) return false;                                      // GB11
    if (a.gcb == GB_REGIONAL_INDICATOR && b.gcb == GB_REGIONAL_INDICATOR) return false;               // GB12/13
    return true;
}

}  // namespace

Decoded decode(std::span<const std::byte> bytes) {
    if (bytes.empty()) return {0, 0, false};
    const unsigned b0 = std::to_integer<unsigned>(bytes[0]);
    const Decoded invalid{static_cast<char32_t>(b0), 1, false};
    if (b0 < 0x80) return {static_cast<char32_t>(b0), 1, true};
    std::size_t n;
    char32_t cp;
    char32_t min;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        n = 2;
        cp = b0 & 0x1Fu;
        min = 0x80;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        n = 3;
        cp = b0 & 0x0Fu;
        min = 0x800;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        n = 4;
        cp = b0 & 0x07u;
        min = 0x10000;
    } else {
        return invalid;
    }
    if (bytes.size() < n) return invalid;
    for (std::size_t i = 1; i < n; ++i) {
        if (!is_cont(bytes[i])) return invalid;
        cp = (cp << 6) | (std::to_integer<char32_t>(bytes[i]) & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return invalid;
    return {cp, static_cast<std::uint8_t>(n), true};
}

Decoded decode_before(std::span<const std::byte> bytes) {
    if (bytes.empty()) return {0, 0, false};
    const std::size_t size = bytes.size();
    // Forward decoding is self-synchronizing, so the unit ending here is the valid
    // sequence led by the nearest non-continuation byte, if it ends exactly here.
    for (std::size_t k = 2; k <= 4 && k <= size; ++k) {
        const std::byte lead = bytes[size - k];
        if (is_cont(lead)) continue;
        const Decoded d = decode(bytes.subspan(size - k));
        if (d.valid && d.len == k) return d;
        break;
    }
    return decode(bytes.subspan(size - 1, 1));
}

ClusterResult next_grapheme_boundary(std::span<const std::byte> bytes, bool at_end) {
    if (bytes.empty()) return {at_end ? ClusterResult::found : ClusterResult::need_more, 0};
    if (!at_end && truncated_prefix(bytes)) return {ClusterResult::need_more, 0};
    const Decoded first = decode(bytes);
    SegState state;
    state.start(props_of(first));
    std::size_t pos = first.len;
    for (std::uint32_t count = 1;; ++count) {
        const auto len = static_cast<std::uint32_t>(pos);
        if (count >= MAX_CLUSTER_CODE_POINTS) return {ClusterResult::found, len};
        if (state.prev.gcb == GB_CONTROL || state.prev.gcb == GB_LF) return {ClusterResult::found, len};  // GB4
        if (pos == bytes.size()) return {at_end ? ClusterResult::found : ClusterResult::need_more, len};
        const auto rest = bytes.subspan(pos);
        if (!at_end && truncated_prefix(rest)) return {ClusterResult::need_more, len};
        const Decoded d = decode(rest);
        const Props p = props_of(d);
        if (state.breaks_before(p)) return {ClusterResult::found, len};
        state.advance(p);
        pos += d.len;
    }
}

ClusterResult prev_grapheme_boundary(std::span<const std::byte> bytes, bool at_start) {
    if (bytes.empty()) return {at_start ? ClusterResult::found : ClusterResult::need_more, 0};
    // Scan back to a position where a boundary is certain, then segment forward.
    // Past this many code points without one, the scan position is taken as the
    // boundary (even, so regional-indicator pairs keep their alignment).
    constexpr std::size_t kScanLimit = std::size_t{2} * MAX_CLUSTER_CODE_POINTS;
    auto unit_before = [&](std::size_t end, Decoded& out) -> bool {
        out = decode_before(bytes.first(end));
        // Fewer than 4 bytes of context may hide the lead byte of a longer sequence.
        return out.valid || at_start || end >= 4;
    };
    std::size_t q = bytes.size();
    Decoded b;
    if (!unit_before(q, b)) return {ClusterResult::need_more, 0};
    q -= b.len;
    std::size_t scanned = 1;
    while (q > 0) {
        if (scanned >= kScanLimit) break;
        Decoded a;
        if (!unit_before(q, a)) return {ClusterResult::need_more, 0};
        if (certain_break(props_of(a), props_of(b))) break;
        b = a;
        q -= a.len;
        ++scanned;
    }
    if (q == 0 && !at_start && scanned < kScanLimit) return {ClusterResult::need_more, 0};
    for (;;) {
        const auto rest = bytes.subspan(q);
        const ClusterResult r = next_grapheme_boundary(rest, true);
        if (r.length == 0 || q + r.length >= bytes.size()) return {ClusterResult::found, static_cast<std::uint32_t>(rest.size())};
        q += r.length;
    }
}

int display_width(const Decoded& d, std::uint64_t column, int tab_width) {
    if (!d.valid) return 4;  // shown as \xNN
    if (d.cp == U'\t') {
        const auto tw = static_cast<std::uint64_t>(std::clamp(tab_width, 1, 16));
        return static_cast<int>(tw - column % tw);
    }
    if (d.cp < 0x20 || d.cp == 0x7F) return 2;  // shown as ^X
    if (d.cp >= 0x80 && d.cp <= 0x9F) return 6;  // C1 control, shown as \u0080
    return lookup(d.cp).width;
}

CharClass char_class(const Decoded& d) {
    if (!d.valid) return CharClass::punct;
    if (d.cp == U'\n' || d.cp == U'\r') return CharClass::newline;
    if (d.cp == U'_') return CharClass::word;
    switch (lookup(d.cp).cls) {
        case CC_SPACE: return CharClass::space;
        case CC_PUNCT: return CharClass::punct;
        case CC_CONTROL:
            return (d.cp == U'\t' || d.cp == U'\v' || d.cp == U'\f') ? CharClass::space : CharClass::punct;
        default: return CharClass::word;
    }
}

std::uint64_t utf16_length(std::span<const std::byte> bytes) {
    std::uint64_t n = 0;
    while (!bytes.empty()) {
        const Decoded d = decode(bytes);
        n += (d.valid && d.cp >= 0x10000) ? 2 : 1;
        bytes = bytes.subspan(d.len);
    }
    return n;
}

namespace {
bool g_vt100_text = false;
}  // namespace

void set_vt100_text(bool on) noexcept { g_vt100_text = on; }
bool vt100_text() noexcept { return g_vt100_text; }

int text_columns(std::string_view text) {
    int columns = 0;
    for (std::size_t i = 0; i < text.size();) {
        const Decoded d = decode(std::as_bytes(std::span(text.data() + i, text.size() - i)));
        if (d.valid && d.cp == 0x2026 && g_vt100_text) {
            columns += 3;  // drawn as "..."
            i += d.len;
            continue;
        }
        columns += d.valid ? display_width(d, static_cast<std::uint64_t>(columns), 1) : 1;
        i += d.len;
    }
    return columns;
}

}  // namespace mod
