#include "syntax/markdown_render.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <numeric>
#include <optional>
#include <span>

#include "syntax/markdown.hpp"
#include "text/utf8.hpp"

namespace mod {
namespace {

constexpr int kMinWidth = 20;

// One code point of inline text, with its look and the link it belongs to (−1 for none).
struct Unit {
    std::string bytes;
    int width = 1;
    Style style = Style::Default;
    int link = -1;
    bool space = false;
    std::uint64_t src = kGenerated;  // source offset of the first byte
};
using Units = std::vector<Unit>;

// Text with the source offset of each of its bytes.
struct Src {
    std::string text;
    std::vector<std::uint64_t> offs;

    // `piece`, whose first byte is at `at` in the source.
    void add(std::string_view piece, std::uint64_t at) {
        text += piece;
        for (std::size_t k = 0; k < piece.size(); ++k) offs.push_back(at + k);
    }
    void add_byte(char c, std::uint64_t at) {
        text += c;
        offs.push_back(at);
    }
};

// `part`, a view into `whole`, whose first byte is at `whole_at` in the source.
std::uint64_t offset_in(std::string_view whole, std::uint64_t whole_at, std::string_view part) {
    return whole_at + static_cast<std::uint64_t>(part.data() - whole.data());
}

Src trimmed(const Src& s) {
    std::size_t b = 0;
    std::size_t e = s.text.size();
    while (b < e && (s.text[b] == ' ' || s.text[b] == '\t')) ++b;
    while (e > b && (s.text[e - 1] == ' ' || s.text[e - 1] == '\t')) --e;
    Src out;
    out.text = s.text.substr(b, e - b);
    out.offs.assign(s.offs.begin() + static_cast<std::ptrdiff_t>(b), s.offs.begin() + static_cast<std::ptrdiff_t>(e));
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

std::size_t indent_of(std::string_view s) {
    std::size_t n = 0;
    while (n < s.size() && s[n] == ' ') ++n;
    return n;
}

bool blank(std::string_view s) { return trim(s).empty(); }

int heading_level(std::string_view line) {
    const std::size_t lead = indent_of(line);
    if (lead > 3) return 0;
    std::size_t n = 0;
    while (lead + n < line.size() && line[lead + n] == '#') ++n;
    if (n == 0 || n > 6) return 0;
    if (lead + n < line.size() && line[lead + n] != ' ') return 0;
    return static_cast<int>(n);
}

std::string_view heading_text(std::string_view line, int level) {
    std::string_view t = trim(line.substr(indent_of(line) + static_cast<std::size_t>(level)));
    // A closing run of #s preceded by a space is not part of the text.
    std::size_t end = t.size();
    while (end > 0 && t[end - 1] == '#') --end;
    if (end < t.size() && (end == 0 || t[end - 1] == ' ')) t = trim(t.substr(0, end));
    return t;
}

// The fence's character and length, or 0 when the line does not open or close one.
std::pair<char, std::size_t> fence(std::string_view line) {
    const std::size_t lead = indent_of(line);
    if (lead > 3 || lead >= line.size()) return {0, 0};
    const char c = line[lead];
    if (c != '`' && c != '~') return {0, 0};
    std::size_t n = 0;
    while (lead + n < line.size() && line[lead + n] == c) ++n;
    return n >= 3 ? std::pair{c, n} : std::pair{'\0', std::size_t{0}};
}

bool is_rule(std::string_view line) {
    const std::string_view t = trim(line);
    if (indent_of(line) > 3 || t.empty() || (t[0] != '-' && t[0] != '*' && t[0] != '_')) return false;
    int count = 0;
    for (char c : t) {
        if (c == t[0]) {
            ++count;
        } else if (c != ' ') {
            return false;
        }
    }
    return count >= 3;
}

struct ListItem {
    std::size_t indent = 0;  // columns before the marker
    std::string marker;      // "• " or "12. "
    std::size_t content = 0; // byte offset of the item's text
};

std::optional<ListItem> list_item(std::string_view line) {
    const std::size_t lead = indent_of(line);
    if (lead >= line.size()) return std::nullopt;
    const char c = line[lead];
    if ((c == '-' || c == '*' || c == '+') && lead + 1 < line.size() && line[lead + 1] == ' ')
        return ListItem{lead, "• ", lead + 2};
    std::size_t d = lead;
    while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d]))) ++d;
    if (d > lead && d - lead <= 9 && d + 1 < line.size() && (line[d] == '.' || line[d] == ')') && line[d + 1] == ' ')
        return ListItem{lead, std::string(line.substr(lead, d - lead)) + ". ", d + 2};
    return std::nullopt;
}

bool is_quote(std::string_view line) {
    const std::size_t lead = indent_of(line);
    return lead <= 3 && lead < line.size() && line[lead] == '>';
}

bool is_table_row(std::string_view line) { return trim(line).starts_with('|'); }

bool is_table_rule(std::string_view line) {
    const std::string_view t = trim(line);
    if (!t.starts_with('|')) return false;
    bool dash = false;
    for (char c : t) {
        if (c == '-') {
            dash = true;
        } else if (c != '|' && c != ':' && c != ' ') {
            return false;
        }
    }
    return dash;
}

// The cells of a table row, split on `|` outside code spans and escapes; the row's
// first byte is at `at` in the source.
std::vector<Src> table_cells(std::string_view line, std::uint64_t at) {
    std::string_view t = trim(line);
    std::uint64_t base = offset_in(line, at, t);
    if (t.starts_with('|')) {
        t.remove_prefix(1);
        ++base;
    }
    if (t.ends_with('|') && !(t.size() >= 2 && t[t.size() - 2] == '\\')) t.remove_suffix(1);
    std::vector<Src> cells;
    Src cell;
    bool code = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            cell.add_byte('|', base + i + 1);
            ++i;
        } else if (c == '`') {
            code = !code;
            cell.add_byte(c, base + i);
        } else if (c == '|' && !code) {
            cells.push_back(trimmed(cell));
            cell = Src{};
        } else {
            cell.add_byte(c, base + i);
        }
    }
    cells.push_back(trimmed(cell));
    return cells;
}

int width_of(const Units& units) {
    int w = 0;
    for (const Unit& u : units) w += u.width;
    return w;
}


std::string repeat(std::string_view s, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += s;
    return out;
}

class Renderer {
public:
    explicit Renderer(int width) : width_(std::max(kMinWidth, width)) {}

    RenderedPage run(std::string_view text);

private:
    // ---- inline ----------------------------------------------------------------------

    // `offs`: the source offset of each byte of `s`.
    void add_text(std::string_view s, const std::uint64_t* offs, Style style, int link, Units& out) const {
        for (std::size_t i = 0; i < s.size();) {
            const char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (out.empty() || !out.back().space) out.push_back(Unit{" ", 1, style, link, true, offs[i]});
                ++i;
                continue;
            }
            const Decoded d = decode(std::as_bytes(std::span(s.data() + i, s.size() - i)));
            const std::size_t len = std::max<std::size_t>(1, d.len);
            out.push_back(Unit{std::string(s.substr(i, len)), d.valid ? std::max(0, display_width(d, 0, 1)) : 1, style, link, false, offs[i]});
            i += len;
        }
    }

    void parse_inline(const Src& src, Style base, int link, Units& out) { parse_inline(src.text, src.offs.data(), base, link, out, 0); }

    // Links and emphasis nest at most this deep; deeper marks stay literal text, so a
    // hostile file cannot exhaust the stack.
    static constexpr int kMaxInlineDepth = 16;

    // Where a closing mark was searched for and not found: it is not found from any later
    // point either, which keeps a run of unmatched marks linear.
    struct Misses {
        std::vector<std::pair<std::string, std::size_t>> after;

        bool absent(std::string_view mark, std::size_t from) const {
            for (const auto& [m, p] : after)
                if (m == mark && from >= p) return true;
            return false;
        }
        void record(std::string_view mark, std::size_t from) {
            for (auto& [m, p] : after) {
                if (m == mark) {
                    p = std::min(p, from);
                    return;
                }
            }
            after.emplace_back(mark, from);
        }
        std::size_t find(std::string_view s, std::string_view mark, std::size_t from) {
            if (absent(mark, from)) return std::string_view::npos;
            const std::size_t at = s.find(mark, from);
            if (at == std::string_view::npos) record(mark, from);
            return at;
        }
    };

    // For each `[` of `s`, the `]` that closes it (npos when none), skipping escapes.
    static std::vector<std::size_t> bracket_pairs(std::string_view s) {
        std::vector<std::size_t> pair(s.size(), std::string_view::npos);
        std::vector<std::size_t> open;
        for (std::size_t j = 0; j < s.size(); ++j) {
            if (s[j] == '\\') {
                ++j;
            } else if (s[j] == '[') {
                open.push_back(j);
            } else if (s[j] == ']' && !open.empty()) {
                pair[open.back()] = j;
                open.pop_back();
            }
        }
        return pair;
    }

    // The `*` or `_` closing emphasis opened at `from - 1`, or npos.
    static std::size_t emphasis_close(std::string_view s, char c, std::size_t from, Misses& misses) {
        const std::string_view mark = c == '*' ? "*" : "_";
        if (misses.absent(mark, from)) return std::string_view::npos;
        for (std::size_t close = from; close < s.size(); ++close) {
            if (s[close] != c) continue;
            if (c == '_' && close + 1 < s.size() && std::isalnum(static_cast<unsigned char>(s[close + 1]))) continue;
            return close;
        }
        misses.record(mark, from);
        return std::string_view::npos;
    }

    void parse_inline(std::string_view s, const std::uint64_t* offs, Style base, int link, Units& out, int depth) {
        std::string literal;
        std::vector<std::uint64_t> literal_offs;
        auto flush = [&] {
            add_text(literal, literal_offs.data(), base, link, out);
            literal.clear();
            literal_offs.clear();
        };
        // Styles nest, but a link keeps its own look throughout.
        auto inner = [&](Style s2) { return link >= 0 ? Style::md_link_text : s2; };
        const bool may_nest = depth < kMaxInlineDepth;
        Misses misses;
        std::vector<std::size_t> pairs;  // made on the first `[`
        std::size_t i = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '\\' && i + 1 < s.size() && std::ispunct(static_cast<unsigned char>(s[i + 1]))) {
                literal += s[i + 1];
                literal_offs.push_back(offs[i + 1]);
                i += 2;
                continue;
            }
            if (c == '`') {
                std::size_t k = 0;
                while (i + k < s.size() && s[i + k] == '`') ++k;
                const std::size_t close = misses.find(s, std::string(k, '`'), i + k);
                if (close != std::string_view::npos) {
                    flush();
                    std::size_t from = i + k;
                    std::string_view code = s.substr(from, close - from);
                    if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ') {
                        code = code.substr(1, code.size() - 2);
                        ++from;
                    }
                    add_text(code, offs + from, Style::md_code, link, out);
                    i = close + k;
                    continue;
                }
                for (std::size_t m = 0; m < k; ++m) {
                    literal += '`';
                    literal_offs.push_back(offs[i + m]);
                }
                i += k;
                continue;
            }
            if (c == '[' && may_nest) {
                if (pairs.empty()) pairs = bracket_pairs(s);
                if (const auto l = link_at(s, i, pairs, misses)) {
                    flush();
                    const int id = static_cast<int>(page_.links.size());
                    page_.links.push_back(RenderedLink{std::string(l->target), {}});
                    parse_inline(l->text, offs + (l->text.data() - s.data()), Style::md_link_text, id, out, depth + 1);
                    i = l->end;
                    continue;
                }
            }
            if (may_nest && (s.substr(i).starts_with("**") || s.substr(i).starts_with("__") || s.substr(i).starts_with("~~"))) {
                const std::string_view mark = s.substr(i, 2);
                const std::size_t close = misses.find(s, mark, i + 2);
                if (close != std::string_view::npos && close > i + 2) {
                    flush();
                    parse_inline(s.substr(i + 2, close - i - 2), offs + i + 2, inner(mark == "~~" ? Style::md_strike : Style::md_strong), link,
                                 out, depth + 1);
                    i = close + 2;
                    continue;
                }
            }
            if (may_nest && (c == '*' || (c == '_' && (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1]))))) && i + 1 < s.size() &&
                s[i + 1] != ' ') {
                const std::size_t close = emphasis_close(s, c, i + 1, misses);
                if (close != std::string_view::npos && close > i + 1) {
                    flush();
                    parse_inline(s.substr(i + 1, close - i - 1), offs + i + 1, inner(Style::md_emphasis), link, out, depth + 1);
                    i = close + 1;
                    continue;
                }
            }
            literal += c;
            literal_offs.push_back(offs[i]);
            ++i;
        }
        flush();
    }

    struct LinkAt {
        std::string_view text;
        std::string_view target;
        std::size_t end;
    };

    // `[text](target)` starting at `i`, with nested brackets in the text; `pairs` from
    // bracket_pairs(s).
    static std::optional<LinkAt> link_at(std::string_view s, std::size_t i, const std::vector<std::size_t>& pairs, Misses& misses) {
        const std::size_t j = pairs[i];
        if (j == std::string_view::npos || j + 1 >= s.size() || s[j + 1] != '(') return std::nullopt;
        const std::size_t close = misses.find(s, ")", j + 2);
        if (close == std::string_view::npos) return std::nullopt;
        std::string_view target = trim(s.substr(j + 2, close - j - 2));
        if (const std::size_t sp = target.find(' '); sp != std::string_view::npos) target = target.substr(0, sp);  // a title
        if (target.starts_with('<') && target.ends_with('>')) target = target.substr(1, target.size() - 2);
        return LinkAt{s.substr(i + 1, j - i - 1), target, close + 1};
    }

    // ---- lines -----------------------------------------------------------------------

    RenderedLine& new_line(std::uint32_t src) {
        page_.lines.push_back(RenderedLine{{}, {}, src, {}});
        return page_.lines.back();
    }

    void blank_before() {
        if (!page_.lines.empty() && !page_.lines.back().text.empty()) {
            const std::uint32_t src = page_.lines.back().source_line;
            new_line(src);
        }
    }

    // `src`: the source offset of the first of `bytes`, which are consecutive in the source,
    // or kGenerated for bytes the layout makes up.
    void append(RenderedLine& line, std::string_view bytes, Style style, int link, std::uint64_t src = kGenerated) {
        const std::size_t at = line.text.size();
        line.text += bytes;
        for (std::size_t k = 0; k < bytes.size(); ++k) line.source.push_back(src == kGenerated ? kGenerated : src + k);
        const std::size_t end = line.text.size();
        if (style != Style::Default) {
            if (!line.spans.empty() && line.spans.back().end == at && line.spans.back().style == style) {
                line.spans.back().end = end;
            } else {
                line.spans.push_back(RenderSpan{at, end, style});
            }
        }
        if (link >= 0) {
            auto& pieces = page_.links[static_cast<std::size_t>(link)].pieces;
            const std::size_t n = page_.lines.size() - 1;
            if (!pieces.empty() && pieces.back().line == n && pieces.back().end == at) {
                pieces.back().end = end;
            } else {
                pieces.push_back(LinkPiece{n, at, end});
            }
        }
    }

    // Word-wraps `units` to the width: `first` before the first line, `rest` before the others.
    void wrap(const Units& units, std::string_view first, std::string_view rest, Style prefix_style, std::uint32_t src) {
        RenderedLine* line = &new_line(src);
        append(*line, first, prefix_style, -1);
        int col = text_columns(first);
        bool has_word = false;
        const int rest_cols = text_columns(rest);
        auto next_line = [&] {
            line = &new_line(src);
            append(*line, rest, prefix_style, -1);
            col = rest_cols;
            has_word = false;
        };
        std::size_t i = 0;
        while (i < units.size()) {
            if (units[i].space) {
                ++i;
                continue;
            }
            std::size_t j = i;
            int w = 0;
            while (j < units.size() && !units[j].space) w += units[j++].width;
            const Unit* gap = i > 0 && units[i - 1].space ? &units[i - 1] : nullptr;
            if (has_word && col + 1 + w > width_) next_line();
            if (has_word) {
                append(*line, " ", gap ? gap->style : Style::Default, gap && gap->link == units[i].link ? gap->link : -1, gap ? gap->src : kGenerated);
                ++col;
            }
            for (std::size_t k = i; k < j; ++k) {
                if (col + units[k].width > width_ && col > rest_cols) next_line();  // a word too long for a line
                append(*line, units[k].bytes, units[k].style, units[k].link, units[k].src);
                col += units[k].width;
            }
            has_word = true;
            i = j;
        }
    }

    // ---- blocks ----------------------------------------------------------------------

    void heading(std::string_view line, std::uint64_t at, int level, std::uint32_t src) {
        blank_before();
        const std::string_view title = heading_text(line, level);
        Src title_src;
        title_src.add(title, offset_in(line, at, title));
        std::string slug = heading_slug(title);
        const int n = slug_count_[slug]++;
        if (n > 0) slug += "-" + std::to_string(n);
        page_.anchors.emplace_back(std::move(slug), page_.lines.size());
        const Style style = static_cast<Style>(static_cast<int>(Style::md_heading1) + level - 1);
        Units units;
        parse_inline(title_src, style, -1, units);
        const std::size_t first = page_.lines.size();
        wrap(units, "", "", Style::Default, src);
        if (level <= 2) {
            int w = 0;
            for (std::size_t k = first; k < page_.lines.size(); ++k) w = std::max(w, text_columns(page_.lines[k].text));
            RenderedLine& rule = new_line(src);
            append(rule, repeat(level == 1 ? "═" : "─", w), style, -1);
        }
    }

    void table(const std::vector<std::string_view>& rows, const std::vector<std::uint64_t>& at, std::uint32_t src) {
        blank_before();
        std::vector<std::vector<Units>> cells;
        std::vector<int> widths;
        for (std::size_t n = 0; n < rows.size(); ++n) {
            std::vector<Units> r;
            for (const Src& c : table_cells(rows[n], at[n])) {
                Units u;
                parse_inline(c, Style::Default, -1, u);
                widths.resize(std::max(widths.size(), r.size() + 1));
                widths[r.size()] = std::max(widths[r.size()], width_of(u));
                r.push_back(std::move(u));
            }
            cells.push_back(std::move(r));
        }
        // Links were numbered while parsing; their pieces are placed as the rows are laid out.
        for (std::size_t r = 0; r < cells.size(); ++r) {
            RenderedLine* line = &new_line(src + static_cast<std::uint32_t>(r == 0 ? 0 : r + 1));
            for (std::size_t c = 0; c < cells[r].size(); ++c) {
                if (c > 0) append(*line, " │ ", Style::Default, -1);
                for (const Unit& u : cells[r][c]) append(*line, u.bytes, u.style, u.link, u.src);
                if (c + 1 < cells[r].size()) append(*line, std::string(static_cast<std::size_t>(widths[c] - width_of(cells[r][c])), ' '), Style::Default, -1);
            }
            if (r == 0) {
                std::string rule;
                for (std::size_t c = 0; c < widths.size(); ++c) {
                    if (c > 0) rule += "─┼─";
                    rule += repeat("─", widths[c]);
                }
                append(new_line(src + 1), rule, Style::Default, -1);
            }
        }
    }

    int width_;
    RenderedPage page_;
    std::map<std::string, int> slug_count_;
};

RenderedPage Renderer::run(std::string_view text) {
    std::vector<std::string_view> lines;
    std::vector<std::uint64_t> starts;  // each line's first byte in `text`
    for (std::size_t i = 0; i <= text.size();) {
        std::size_t e = text.find('\n', i);
        if (e == std::string_view::npos) e = text.size();
        std::string_view l = text.substr(i, e - i);
        if (l.ends_with('\r')) l.remove_suffix(1);
        if (e == text.size() && l.empty()) break;
        lines.push_back(l);
        starts.push_back(i);
        i = e + 1;
    }
    // The source offset just past line `k`: its line break, which a joining space stands for.
    const auto end_of = [&](std::size_t k) { return starts[k] + lines[k].size(); };
    // `part`, a view into line `k`, added to `out`.
    const auto add_part = [&](Src& out, std::size_t k, std::string_view part) { out.add(part, offset_in(lines[k], starts[k], part)); };
    bool in_list = false;  // the previous block was a list item, with no blank line since
    for (std::size_t i = 0; i < lines.size();) {
        const std::string_view line = lines[i];
        const auto src = static_cast<std::uint32_t>(i + 1);
        if (blank(line)) {
            in_list = false;
            ++i;
            continue;
        }
        if (const auto [fc, fn] = fence(line); fn > 0) {
            blank_before();
            std::size_t j = i + 1;
            for (; j < lines.size(); ++j) {
                const auto [c2, n2] = fence(lines[j]);
                if (c2 == fc && n2 >= fn && trim(lines[j]).size() == n2) break;
                RenderedLine& out = new_line(static_cast<std::uint32_t>(j + 1));
                append(out, "    ", Style::Default, -1);
                if (!lines[j].empty()) append(out, lines[j], Style::md_code_block, -1, starts[j]);
            }
            i = std::min(lines.size(), j + 1);
            in_list = false;
            continue;
        }
        if (const int level = heading_level(line); level > 0) {
            heading(line, starts[i], level, src);
            ++i;
            in_list = false;
            continue;
        }
        if (is_rule(line)) {
            blank_before();
            append(new_line(src), repeat("─", width_), Style::Default, -1);
            ++i;
            in_list = false;
            continue;
        }
        if (is_table_row(line) && i + 1 < lines.size() && is_table_rule(lines[i + 1])) {
            std::vector<std::string_view> rows{line};
            std::vector<std::uint64_t> at{starts[i]};
            std::size_t j = i + 2;
            while (j < lines.size() && is_table_row(lines[j])) {
                at.push_back(starts[j]);
                rows.push_back(lines[j++]);
            }
            table(rows, at, src);
            i = j;
            in_list = false;
            continue;
        }
        if (is_quote(line)) {
            blank_before();
            Src joined;
            std::size_t j = i;
            for (; j < lines.size() && is_quote(lines[j]); ++j) {
                std::string_view q = lines[j].substr(indent_of(lines[j]) + 1);
                if (q.starts_with(' ')) q.remove_prefix(1);
                add_part(joined, j, q);
                joined.add_byte(' ', end_of(j));
            }
            Units units;
            parse_inline(joined, Style::md_quote, -1, units);
            wrap(units, "│ ", "│ ", Style::md_quote, src);
            i = j;
            in_list = false;
            continue;
        }
        if (const auto item = list_item(line)) {
            if (!in_list) blank_before();
            Src joined;
            add_part(joined, i, line.substr(item->content));
            std::size_t j = i + 1;
            // Continuation lines: indented, and not a block of their own.
            for (; j < lines.size(); ++j) {
                const std::string_view next = lines[j];
                if (blank(next) || list_item(next) || heading_level(next) > 0 || fence(next).second > 0 || is_quote(next) ||
                    indent_of(next) <= item->indent)
                    break;
                joined.add_byte(' ', end_of(j - 1));
                add_part(joined, j, trim(next));
            }
            const std::string indent(item->indent / 2 * 2, ' ');
            Units units;
            parse_inline(joined, Style::Default, -1, units);
            wrap(units, indent + item->marker, indent + std::string(static_cast<std::size_t>(text_columns(item->marker)), ' '),
                 Style::Default, src);
            i = j;
            in_list = true;
            continue;
        }
        // A paragraph: lines up to a blank one or the start of another block.
        blank_before();
        Src joined;
        add_part(joined, i, trim(line));
        std::size_t j = i + 1;
        for (; j < lines.size(); ++j) {
            const std::string_view next = lines[j];
            if (blank(next) || heading_level(next) > 0 || fence(next).second > 0 || is_rule(next) || is_quote(next) || list_item(next) ||
                (is_table_row(next) && j + 1 < lines.size() && is_table_rule(lines[j + 1])))
                break;
            joined.add_byte(' ', end_of(j - 1));
            add_part(joined, j, trim(next));
        }
        Units units;
        parse_inline(joined, Style::Default, -1, units);
        wrap(units, "", "", Style::Default, src);
        i = j;
        in_list = false;
    }
    return std::move(page_);
}

}  // namespace

RenderedPage render_markdown(std::string_view text, int width) { return Renderer(width).run(text); }

}  // namespace mod
