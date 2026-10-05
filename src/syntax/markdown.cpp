#include "syntax/markdown.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>

#include "text/utf8.hpp"

namespace mod {
namespace {

// Only this much of a line is kept while scanning for fences; a longer line is never
// a closing fence (it would need the rest of the line to be blank).
constexpr std::size_t kLinePrefix = 1024;

using BlockState = MarkdownHighlighter::BlockState;

bool is_ascii_punct(unsigned char c) {
    return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) || (c >= 0x5B && c <= 0x60) ||
           (c >= 0x7B && c <= 0x7E);
}

std::span<const std::byte> as_span(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

// Leading spaces, or npos when there are more than three (or a tab), which rules out
// fences, headings, quotes and thematic breaks.
std::size_t block_indent(std::string_view s, std::size_t from) {
    std::size_t i = from;
    while (i < s.size() && s[i] == ' ' && i - from < 4) ++i;
    if (i - from > 3 || (i < s.size() && s[i] == '\t')) return std::string_view::npos;
    return i;
}

// An opening fence: up to 3 spaces, then 3+ backticks or tildes; a backtick fence's
// info string may not contain a backtick.
std::optional<BlockState> fence_open(std::string_view line) {
    const std::size_t i = block_indent(line, 0);
    if (i == std::string_view::npos || i >= line.size()) return std::nullopt;
    const char c = line[i];
    if (c != '`' && c != '~') return std::nullopt;
    std::size_t j = i;
    while (j < line.size() && line[j] == c) ++j;
    if (j - i < 3) return std::nullopt;
    if (c == '`' && line.find('`', j) != std::string_view::npos) return std::nullopt;
    return BlockState{true, c, static_cast<std::uint32_t>(j - i)};
}

bool fence_close(std::string_view line, bool truncated, const BlockState& st) {
    if (truncated) return false;
    const std::size_t i = block_indent(line, 0);
    if (i == std::string_view::npos) return false;
    std::size_t j = i;
    while (j < line.size() && line[j] == st.fence_char) ++j;
    if (j - i < st.fence_len) return false;
    for (; j < line.size(); ++j)
        if (line[j] != ' ' && line[j] != '\t' && line[j] != '\r') return false;
    return true;
}

BlockState advance(const BlockState& st, std::string_view line, bool truncated) {
    if (st.in_fence) return fence_close(line, truncated, st) ? BlockState{} : st;
    if (auto open = fence_open(line)) return *open;
    return st;
}

// ---- inline tokenizer -----------------------------------------------------------

struct Range {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct Delim {
    std::size_t start = 0;  // first remaining delimiter character
    std::size_t count = 0;  // remaining delimiter characters
    std::size_t orig = 0;   // length of the original run
    char ch = 0;
    bool can_open = false;
    bool can_close = false;
    bool removed = false;
};

struct Emph {
    Range content;
    Style style = Style::Default;
};

struct Bracket {
    std::size_t pos = 0;  // of '[' (or '!' for an image)
    bool image = false;
    std::size_t delim_bottom = 0;
};

// A link found by the tokenizer, in line offsets. `dest` is the raw text between the
// parentheses (destination and title); `label` is set for a reference link instead.
struct RawLink {
    Range whole;
    Range text;
    Range dest;
    bool reference = false;
    std::string label;
};

class Inline {
public:
    explicit Inline(std::string_view s, bool collect_refs = false) : s_(s), collect_refs_(collect_refs) {}

    void run(std::size_t a, std::size_t b) {
        std::size_t i = a;
        while (i < b) {
            const char c = s_[i];
            if (c == '\\' && i + 1 < b && is_ascii_punct(static_cast<unsigned char>(s_[i + 1]))) {
                markup.push_back({i, i + 1});
                i += 2;
            } else if (c == '`') {
                i = code_span(i, b);
            } else if (c == '<') {
                i = autolink(i, b);
            } else if (c == '!' && i + 1 < b && s_[i + 1] == '[') {
                if (brackets_.size() < MarkdownHighlighter::kMaxDelimiters) brackets_.push_back({i, true, delims_.size()});
                i += 2;
            } else if (c == '[') {
                if (brackets_.size() < MarkdownHighlighter::kMaxDelimiters) brackets_.push_back({i, false, delims_.size()});
                ++i;
            } else if (c == ']') {
                i = close_bracket(i, b);
            } else if (c == '*' || c == '_' || c == '~') {
                i = delimiter_run(i, a, b);
            } else {
                ++i;
            }
        }
        process_emphasis(0);
    }

    std::vector<Range> markup;
    std::vector<Range> code;
    std::vector<Range> urls;
    std::vector<Range> link_text;
    std::vector<Emph> emph;
    std::vector<RawLink> links;

private:
    std::size_t run_length(std::size_t i, std::size_t b, char c) const {
        std::size_t j = i;
        while (j < b && s_[j] == c) ++j;
        return j - i;
    }

    std::size_t code_span(std::size_t i, std::size_t b) {
        const std::size_t k = run_length(i, b, '`');
        // A run of this length not closed from an earlier point is not closed from here.
        if (const auto it = unclosed_code_.find(k); it != unclosed_code_.end() && i >= it->second) return i + k;
        for (std::size_t j = i + k; j < b;) {
            if (s_[j] != '`') {
                ++j;
                continue;
            }
            const std::size_t m = run_length(j, b, '`');
            if (m == k) {
                markup.push_back({i, i + k});
                code.push_back({i + k, j});
                markup.push_back({j, j + k});
                return j + k;
            }
            j += m;
        }
        unclosed_code_.emplace(k, i);
        return i + k;  // no closer: the backticks are literal
    }

    std::size_t autolink(std::size_t i, std::size_t b) {
        std::size_t j = i + 1;
        auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
        auto digit = [](char c) { return c >= '0' && c <= '9'; };
        auto url_char = [](char c) { return c != ' ' && c != '<' && c != '>' && static_cast<unsigned char>(c) >= 0x20; };
        // URI autolink: a scheme of 2-32 characters, ':', no spaces or angle brackets.
        if (j < b && alpha(s_[j])) {
            std::size_t k = j + 1;
            while (k < b && k - j < 32 && (alpha(s_[k]) || digit(s_[k]) || s_[k] == '+' || s_[k] == '.' || s_[k] == '-'))
                ++k;
            if (k < b && s_[k] == ':' && k - j >= 2) {
                std::size_t e = k + 1;
                while (e < b && url_char(s_[e])) ++e;
                if (e < b && s_[e] == '>') return mark_autolink(i, e);
            }
        }
        // Email autolink.
        std::size_t e = j;
        std::size_t at = std::string_view::npos;
        while (e < b && url_char(s_[e]) && s_[e] != '\\') {
            if (s_[e] == '@') {
                if (at != std::string_view::npos) break;
                at = e;
            }
            ++e;
        }
        if (e < b && s_[e] == '>' && at != std::string_view::npos && at > j && at + 1 < e) return mark_autolink(i, e);
        return i + 1;
    }

    std::size_t mark_autolink(std::size_t open, std::size_t close) {
        markup.push_back({open, open + 1});
        urls.push_back({open + 1, close});
        markup.push_back({close, close + 1});
        links.push_back({{open, close + 1}, {open + 1, close}, {open + 1, close}, false, {}});
        return close + 1;
    }

    // `(destination "title")` right after `]`; returns the index of the closing paren.
    std::size_t link_tail(std::size_t p, std::size_t b) {
        if (p >= b || s_[p] != '(') return std::string_view::npos;
        std::size_t q = p + 1;
        if (q >= b || s_[q] != '<') {
            const std::size_t close = paren_pair(p);
            return close < b ? close : std::string_view::npos;
        }
        // `(<destination> …)`: rare, and bounded so a run of them stays linear.
        const std::size_t limit = std::min(b, q + kMaxAngleDestination);
        while (q < limit && s_[q] != '>') ++q;
        if (q >= limit) return std::string_view::npos;
        ++q;
        int depth = 0;
        for (; q < limit; ++q) {
            const char c = s_[q];
            if (c == '\\' && q + 1 < b) {
                ++q;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (depth == 0) return q;
                --depth;
            }
        }
        return std::string_view::npos;
    }

    // The `)` balancing the `(` at `p` (npos when none), from one pass over the line.
    std::size_t paren_pair(std::size_t p) {
        if (paren_pairs_.empty()) {
            paren_pairs_.assign(s_.size(), std::string_view::npos);
            std::vector<std::size_t> open;
            for (std::size_t q = 0; q < s_.size(); ++q) {
                if (s_[q] == '\\') {
                    ++q;
                } else if (s_[q] == '(') {
                    open.push_back(q);
                } else if (s_[q] == ')' && !open.empty()) {
                    paren_pairs_[open.back()] = q;
                    open.pop_back();
                }
            }
        }
        return paren_pairs_[p];
    }

    std::size_t close_bracket(std::size_t i, std::size_t b) {
        if (brackets_.empty()) return i + 1;
        const Bracket open = brackets_.back();
        brackets_.pop_back();
        const std::size_t close = link_tail(i + 1, b);
        const std::size_t text_begin = open.pos + (open.image ? 2 : 1);
        if (close == std::string_view::npos) {
            if (collect_refs_ && !open.image) return reference(open, text_begin, i, b);
            return i + 1;  // literal brackets
        }
        links.push_back({{open.pos, close + 1}, {text_begin, i}, {i + 2, close}, false, {}});
        markup.push_back({open.pos, text_begin});
        link_text.push_back({text_begin, i});
        markup.push_back({i, i + 2});  // "]("
        urls.push_back({i + 2, close});
        markup.push_back({close, close + 1});
        // Emphasis inside the link text is closed off within it.
        process_emphasis(open.delim_bottom);
        delims_.resize(open.delim_bottom);
        if (!open.image) {
            // No links inside links: earlier '[' openers become literal.
            std::erase_if(brackets_, [](const Bracket& br) { return !br.image; });
        }
        return close + 1;
    }

    // `[text][label]`, `[label][]` or `[label]`: recorded for the outline only, never styled.
    std::size_t reference(const Bracket& open, std::size_t text_begin, std::size_t i, std::size_t b) {
        std::size_t end = i + 1;
        std::string label(s_.substr(text_begin, i - text_begin));
        if (i + 1 < b && s_[i + 1] == '[' && i + 2 < no_bracket_after_) {
            const std::size_t close = s_.find(']', i + 2);
            if (close == std::string_view::npos) no_bracket_after_ = i + 2;
            if (close != std::string_view::npos && close < b && s_.substr(i + 2, close - i - 2).find('[') == std::string_view::npos) {
                if (close > i + 2) label.assign(s_.substr(i + 2, close - i - 2));
                end = close + 1;
            }
        }
        links.push_back({{open.pos, end}, {text_begin, i}, {}, true, std::move(label)});
        std::erase_if(brackets_, [](const Bracket& br) { return !br.image; });
        return end;
    }

    // The code point before `i` and after `j` (line edges count as whitespace).
    enum class Kind { space, punct, other };
    Kind kind_of(const Decoded& d) const {
        if (d.len == 0) return Kind::space;
        if (d.valid && d.cp < 0x80) {
            const auto c = static_cast<unsigned char>(d.cp);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return Kind::space;
            return is_ascii_punct(c) ? Kind::punct : Kind::other;
        }
        switch (char_class(d)) {
            case CharClass::space:
            case CharClass::newline: return Kind::space;
            case CharClass::punct: return Kind::punct;
            default: return Kind::other;
        }
    }

    std::size_t delimiter_run(std::size_t i, std::size_t a, std::size_t b) {
        const char c = s_[i];
        const std::size_t n = run_length(i, b, c);
        if (c == '~' && n > 2) return i + n;  // strike runs are one or two tildes
        const Kind before = kind_of(i > a ? decode_before(as_span(s_.substr(a, i - a))) : Decoded{});
        const Kind after = kind_of(i + n < b ? decode(as_span(s_.substr(i + n, b - i - n))) : Decoded{});
        const bool left = after != Kind::space && (after != Kind::punct || before != Kind::other);
        const bool right = before != Kind::space && (before != Kind::punct || after != Kind::other);
        bool can_open = left;
        bool can_close = right;
        if (c == '_') {
            can_open = left && (!right || before == Kind::punct);
            can_close = right && (!left || after == Kind::punct);
        }
        if ((can_open || can_close) && delims_.size() < MarkdownHighlighter::kMaxDelimiters)
            delims_.push_back({i, n, n, c, can_open, can_close, false});
        return i + n;
    }

    // CommonMark's "process emphasis" over delims_[bottom..].
    void process_emphasis(std::size_t bottom) {
        // openers_bottom, keyed by delimiter character, closer length mod 3 and whether
        // the closer can also open.
        std::array<std::size_t, 3 * 3 * 2> openers_bottom;
        openers_bottom.fill(bottom);
        auto key = [](const Delim& d) {
            const std::size_t ci = d.ch == '*' ? 0 : d.ch == '_' ? 1 : 2;
            return (ci * 3 + d.orig % 3) * 2 + (d.can_open ? 1 : 0);
        };
        for (std::size_t c = bottom; c < delims_.size();) {
            Delim& closer = delims_[c];
            if (closer.removed || closer.count == 0 || !closer.can_close) {
                ++c;
                continue;
            }
            const std::size_t k = key(closer);
            std::size_t found = std::string_view::npos;
            for (std::size_t o = c; o-- > openers_bottom[k];) {
                const Delim& op = delims_[o];
                if (op.removed || op.count == 0 || op.ch != closer.ch || !op.can_open) continue;
                if (closer.ch == '~') {
                    if (op.count != closer.count) continue;
                } else if ((op.can_close || closer.can_open) && (op.orig + closer.orig) % 3 == 0 &&
                           !(op.orig % 3 == 0 && closer.orig % 3 == 0)) {
                    continue;
                }
                found = o;
                break;
            }
            if (found == std::string_view::npos) {
                openers_bottom[k] = c;
                if (!closer.can_open) closer.removed = true;
                ++c;
                continue;
            }
            Delim& op = delims_[found];
            std::size_t use;
            Style style;
            if (closer.ch == '~') {
                use = closer.count;
                style = Style::md_strike;
            } else {
                use = (op.count >= 2 && closer.count >= 2) ? 2 : 1;
                style = use == 2 ? Style::md_strong : Style::md_emphasis;
            }
            const std::size_t open_end = op.start + op.count;
            markup.push_back({open_end - use, open_end});
            markup.push_back({closer.start, closer.start + use});
            emph.push_back({{open_end, closer.start}, style});
            op.count -= use;
            closer.start += use;
            closer.count -= use;
            for (std::size_t m = found + 1; m < c; ++m) delims_[m].removed = true;
            if (op.count == 0) op.removed = true;
            if (closer.count == 0) {
                closer.removed = true;
                ++c;
            }
        }
    }

    static constexpr std::size_t kMaxAngleDestination = 4096;

    std::string_view s_;
    bool collect_refs_ = false;
    std::vector<Delim> delims_;
    std::vector<Bracket> brackets_;
    // What earlier searches of this line found missing, so runs of unmatched marks stay linear.
    std::vector<std::size_t> paren_pairs_;
    std::map<std::size_t, std::size_t> unclosed_code_;  // backtick run length -> first start not closed
    std::size_t no_bracket_after_ = std::string_view::npos;
};

void paint(std::vector<Style>& p, Range r, Style s) {
    const std::size_t e = std::min(r.end, p.size());
    for (std::size_t i = r.begin; i < e; ++i) p[i] = s;
}

}  // namespace

MarkdownHighlighter::LineState MarkdownHighlighter::state_at(std::uint64_t line_start) {
    if (line_start == 0) return {};
    if (next_line_ && next_line_->first == line_start) return next_line_->second;
    const PieceTree& text = doc_.text();

    std::uint64_t from = 0;
    LineState ls;  // the document start: no fence, trusted
    auto it = checkpoints_.upper_bound(line_start);
    if (it != checkpoints_.begin()) {
        --it;
        from = it->first;
        ls.state = it->second.state;
        ls.trusted = it->second.trusted;
        if (!ls.trusted) {
            // Repair from the nearest trusted state if it is within the bound, dropping
            // the guesses on the way.
            auto t = it;
            while (t != checkpoints_.begin() && !t->second.trusted) --t;
            const bool found = t->second.trusted;
            const std::uint64_t base = found ? t->first : 0;
            if (line_start - base <= kMaxBackScan) {
                from = base;
                ls.state = found ? t->second.state : BlockState{};
                ls.trusted = true;
                std::erase_if(checkpoints_, [&](const auto& cp) {
                    return cp.first > from && cp.first <= line_start && !cp.second.trusted;
                });
            }
        }
    }
    if (line_start - from > kMaxBackScan) {
        // Too far back: assume no open fence at the first line start in the window.
        const std::uint64_t window = line_start - kMaxBackScan;
        const std::uint64_t lf = text.find_lf_forward(window - 1, line_start - window + 1);
        from = (lf == PieceTree::npos || lf + 1 > line_start) ? line_start : lf + 1;
        ls = {BlockState{}, false, 0};
    }

    std::string prefix;
    bool truncated = false;
    std::uint64_t pos = from;
    ls.lines_since_checkpoint = 0;
    text.read(from, line_start - from, [&](std::span<const std::byte> s) {
        const char* d = reinterpret_cast<const char*>(s.data());
        std::size_t i = 0;
        while (i < s.size()) {
            const void* hit = std::memchr(d + i, '\n', s.size() - i);
            const std::size_t end = hit ? static_cast<std::size_t>(static_cast<const char*>(hit) - d) : s.size();
            const std::size_t room = kLinePrefix - std::min(kLinePrefix, prefix.size());
            prefix.append(d + i, std::min(room, end - i));
            if (end - i > room) truncated = true;
            if (!hit) {
                pos += s.size() - i;
                break;
            }
            ls.state = advance(ls.state, prefix, truncated);
            prefix.clear();
            truncated = false;
            pos += end + 1 - i;
            i = end + 1;
            if (++ls.lines_since_checkpoint >= kCheckpointLines) {
                checkpoints_[pos] = {ls.state, ls.trusted};
                ls.lines_since_checkpoint = 0;
            }
        }
        return true;
    });
    return ls;
}

std::vector<StyleSpan> MarkdownHighlighter::spans_for_line(std::uint64_t line_start, std::string_view line_bytes) {
    const LineState here = state_at(line_start);
    const BlockState st = here.state;
    const PieceTree& text = doc_.text();
    const std::uint64_t after = line_start + line_bytes.size();
    const bool complete = after >= text.size() || text.byte_at(after) == std::byte{'\n'};

    std::string_view line = line_bytes;
    if (complete && line.ends_with('\r')) line.remove_suffix(1);
    const bool truncated = !complete || line.size() > kLinePrefix;

    const BlockState next = advance(st, line.substr(0, std::min(line.size(), kLinePrefix)), truncated);
    if (complete && after < text.size()) {
        LineState following{next, here.trusted, here.lines_since_checkpoint + 1};
        if (following.lines_since_checkpoint >= kCheckpointLines) {
            // Scrolling line by line records checkpoints too, replacing guesses.
            auto& cp = checkpoints_[after + 1];
            if (following.trusted || !cp.trusted) cp = {next, following.trusted};
            following.lines_since_checkpoint = 0;
        }
        next_line_ = {after + 1, following};
    } else {
        next_line_.reset();
    }

    std::vector<Style> p(line.size(), Style::Default);
    if (st.in_fence) {
        paint(p, {0, line.size()}, next.in_fence ? Style::md_code_block : Style::md_markup);
    } else if (next.in_fence) {
        paint(p, {0, line.size()}, Style::md_markup);  // the opening fence line
    } else {
        std::size_t pos = 0;
        std::vector<Range> markup;
        Style base = Style::Default;
        // Block quotes: '>' markers, each with an optional following space.
        for (;;) {
            const std::size_t i = block_indent(line, pos);
            if (i == std::string_view::npos || i >= line.size() || line[i] != '>') break;
            markup.push_back({i, i + 1});
            pos = i + 1;
            if (pos < line.size() && line[pos] == ' ') ++pos;
            base = Style::md_quote;
        }
        if (base == Style::md_quote) paint(p, {pos, line.size()}, base);

        std::size_t content = pos;
        std::size_t content_end = line.size();
        bool inline_text = true;
        const std::size_t ind = block_indent(line, pos);
        if (ind != std::string_view::npos && ind < line.size()) {
            const char c = line[ind];
            std::size_t hashes = 0;
            while (ind + hashes < line.size() && line[ind + hashes] == '#') ++hashes;
            // Thematic break: 3+ of one of * - _ with only spaces or tabs between.
            bool thematic = false;
            if (c == '*' || c == '-' || c == '_') {
                std::size_t n = 0;
                thematic = true;
                for (std::size_t j = ind; j < line.size(); ++j) {
                    if (line[j] == c) ++n;
                    else if (line[j] != ' ' && line[j] != '\t') {
                        thematic = false;
                        break;
                    }
                }
                thematic = thematic && n >= 3;
            }
            if (thematic) {
                markup.push_back({ind, line.size()});
                inline_text = false;
            } else if (hashes >= 1 && hashes <= 6 &&
                       (ind + hashes == line.size() || line[ind + hashes] == ' ' || line[ind + hashes] == '\t')) {
                markup.push_back({ind, ind + hashes});
                const auto level = static_cast<std::uint8_t>(hashes - 1);
                base = static_cast<Style>(static_cast<std::uint8_t>(Style::md_heading1) + level);
                // An optional closing sequence of '#' after a space.
                std::size_t e = line.size();
                while (e > ind + hashes && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
                std::size_t h = e;
                while (h > ind + hashes && line[h - 1] == '#') --h;
                if (h < e && (h == ind + hashes || line[h - 1] == ' ' || line[h - 1] == '\t')) {
                    markup.push_back({h, e});
                    content_end = h;
                }
                paint(p, {ind + hashes, line.size()}, base);
                content = ind + hashes;
            }
        }
        if (inline_text && content == pos) {
            // List marker: any indentation, then - + * or 1-9 digits and . or ),
            // followed by a space, a tab or the end of the line.
            std::size_t i = pos;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            std::size_t m = i;
            if (m < line.size() && (line[m] == '-' || line[m] == '+' || line[m] == '*')) {
                ++m;
            } else {
                while (m < line.size() && m - i < 9 && line[m] >= '0' && line[m] <= '9') ++m;
                if (m > i && m < line.size() && (line[m] == '.' || line[m] == ')')) ++m;
                else m = i;
            }
            if (m > i && (m == line.size() || line[m] == ' ' || line[m] == '\t')) {
                paint(p, {i, m}, Style::md_list_marker);
                content = m;
            }
        }
        if (inline_text) {
            Inline in(line);
            in.run(content, content_end);
            // Emphasis and link text nest: paint outer ranges first so the innermost wins.
            std::vector<Emph> layers = std::move(in.emph);
            for (const Range& r : in.link_text) layers.push_back({r, Style::md_link_text});
            std::ranges::stable_sort(layers, [](const Emph& x, const Emph& y) {
                return x.content.end - x.content.begin > y.content.end - y.content.begin;
            });
            for (const Emph& e : layers) paint(p, e.content, e.style);
            for (const Range& r : in.urls) paint(p, r, Style::md_link_url);
            for (const Range& r : in.code) paint(p, r, Style::md_code);
            for (const Range& r : in.markup) paint(p, r, Style::md_markup);
        }
        for (const Range& r : markup) paint(p, r, Style::md_markup);
    }

    std::vector<StyleSpan> out;
    for (std::size_t i = 0; i < p.size();) {
        std::size_t j = i + 1;
        while (j < p.size() && p[j] == p[i]) ++j;
        if (p[i] != Style::Default) out.push_back({line_start + i, line_start + j, p[i], 0});
        i = j;
    }
    return out;
}

void MarkdownHighlighter::after_change(const ChangeEvent& ev) {
    // The state at a line start depends only on the text before it.
    checkpoints_.erase(checkpoints_.upper_bound(ev.offset), checkpoints_.end());
    if (next_line_ && next_line_->first > ev.offset) next_line_.reset();
}

void MarkdownHighlighter::reloaded() {
    checkpoints_.clear();
    next_line_.reset();
}

namespace {

// Reference labels match case-insensitively with runs of whitespace collapsed.
std::string normalize_label(std::string_view s) {
    std::string out;
    bool space = false;
    for (const char c : s) {
        if (c == ' ' || c == '\t') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return out;
}

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && is_ascii_punct(static_cast<unsigned char>(s[i + 1]))) ++i;
        out += s[i];
    }
    return out;
}

// The destination at the start of `s` (`<...>`, or up to whitespace with balanced
// parentheses), unescaped; the title after it is ignored.
std::string destination(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i < s.size() && s[i] == '<') {
        const std::size_t close = s.find('>', i + 1);
        return close == std::string_view::npos ? std::string() : unescape(s.substr(i + 1, close - i - 1));
    }
    std::size_t j = i;
    int depth = 0;
    for (; j < s.size(); ++j) {
        const char c = s[j];
        if (c == '\\' && j + 1 < s.size()) {
            ++j;
        } else if (c == ' ' || c == '\t') {
            break;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (depth == 0) break;
            --depth;
        }
    }
    return unescape(s.substr(i, j - i));
}

// `[label]: destination` at up to three spaces of indent.
std::optional<std::pair<std::string, std::string>> definition(std::string_view line) {
    const std::size_t i = block_indent(line, 0);
    if (i == std::string_view::npos || i >= line.size() || line[i] != '[') return std::nullopt;
    const std::size_t close = line.find(']', i + 1);
    if (close == std::string_view::npos || close == i + 1 || close + 1 >= line.size() || line[close + 1] != ':') return std::nullopt;
    std::string dest = destination(line.substr(close + 2));
    if (dest.empty()) return std::nullopt;
    return std::pair{normalize_label(line.substr(i + 1, close - i - 1)), std::move(dest)};
}

// An ATX heading's text: 1-6 '#', then a space or the end; closing '#'s dropped.
std::optional<std::string_view> atx_heading(std::string_view line) {
    const std::size_t i = block_indent(line, 0);
    if (i == std::string_view::npos || i >= line.size() || line[i] != '#') return std::nullopt;
    std::size_t j = i;
    while (j < line.size() && line[j] == '#') ++j;
    if (j - i > 6 || (j < line.size() && line[j] != ' ' && line[j] != '\t')) return std::nullopt;
    std::string_view text = line.substr(j);
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    std::size_t k = text.size();
    while (k > 0 && text[k - 1] == '#') --k;
    if (k == 0 || text[k - 1] == ' ' || text[k - 1] == '\t') {
        text = text.substr(0, k);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    }
    return text;
}

// A setext underline: up to three spaces, then only '=' or only '-', then spaces.
bool setext_underline(std::string_view line) {
    const std::size_t i = block_indent(line, 0);
    if (i == std::string_view::npos || i >= line.size() || (line[i] != '=' && line[i] != '-')) return false;
    std::size_t j = i;
    while (j < line.size() && line[j] == line[i]) ++j;
    for (; j < line.size(); ++j)
        if (line[j] != ' ' && line[j] != '\t') return false;
    return true;
}

bool blank(std::string_view line) { return line.find_first_not_of(" \t") == std::string_view::npos; }

// Lower case for ASCII, Latin-1, Greek and Cyrillic capitals; other code points unchanged.
char32_t lower(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

std::string heading_slug(std::string_view heading_text) {
    std::string out;
    std::size_t i = 0;
    while (i < heading_text.size()) {
        const Decoded d = decode(as_span(heading_text.substr(i)));
        i += std::max<std::size_t>(1, d.len);
        if (!d.valid) continue;
        const char32_t c = d.cp;
        if (c == U' ') {
            out += '-';
        } else if (c == U'-' || c == U'_') {
            out += static_cast<char>(c);
        } else if (c < 0x80) {
            if (!is_ascii_punct(static_cast<unsigned char>(c)) && c > 0x20) append_utf8(out, lower(c));
        } else if (char_class(d) != CharClass::punct && char_class(d) != CharClass::space) {
            append_utf8(out, lower(c));
        }
    }
    return out;
}

MarkdownOutline scan_markdown(const PieceTree& text, std::uint64_t max_bytes) {
    MarkdownOutline outline;
    struct Pending {
        RawLink raw;
        std::uint64_t line_start = 0;
        std::string line_dest;  // the destination of an inline link
    };
    std::vector<Pending> pending;
    std::map<std::string, std::string> definitions;
    std::map<std::string, int> slug_count;
    auto add_heading = [&](std::uint64_t start, std::string_view title) {
        std::string slug = heading_slug(title);
        const int n = slug_count[slug]++;
        if (n > 0) slug += "-" + std::to_string(n);
        outline.headings.push_back({start, std::move(slug)});
    };

    const std::uint64_t size = text.size();
    BlockState state;
    std::uint64_t line = 0;
    std::optional<std::pair<std::uint64_t, std::string>> previous;  // a paragraph line: its start and text
    while (line < size) {
        const std::uint64_t lf = text.find_lf_forward(line, size - line);
        const std::uint64_t next = lf == PieceTree::npos ? size : lf + 1;
        if (next > max_bytes) {
            outline.truncated = true;
            break;
        }
        std::string s = text.read(line, (lf == PieceTree::npos ? size : lf) - line);
        if (!s.empty() && s.back() == '\r') s.pop_back();
        const bool was_in_fence = state.in_fence;
        state = advance(state, s, false);
        if (was_in_fence || state.in_fence) {
            previous.reset();  // fence lines, and the opening fence itself, hold no links
        } else if (auto def = definition(s)) {
            definitions.emplace(std::move(def->first), std::move(def->second));
            previous.reset();
        } else if (auto title = atx_heading(s)) {
            add_heading(line, *title);
            previous.reset();
        } else if (setext_underline(s) && previous) {
            add_heading(previous->first, previous->second);
            previous.reset();
        } else {
            Inline in(s, true);
            in.run(0, s.size());
            for (RawLink& raw : in.links) {
                if (pending.size() >= kMaxOutlineLinks) {
                    outline.truncated = true;  // enough to navigate; the rest are left out
                    break;
                }
                Pending p{std::move(raw), line, {}};
                if (!p.raw.reference) p.line_dest = p.raw.whole.begin < s.size() && s[p.raw.whole.begin] == '<'
                                                         ? std::string(s.substr(p.raw.dest.begin, p.raw.dest.end - p.raw.dest.begin))
                                                         : destination(std::string_view(s).substr(p.raw.dest.begin, p.raw.dest.end - p.raw.dest.begin));
                pending.push_back(std::move(p));
            }
            if (blank(s)) {
                previous.reset();
            } else {
                std::string_view t = s;
                while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.remove_prefix(1);
                while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.remove_suffix(1);
                previous = std::pair{line, std::string(t)};
            }
        }
        line = next;
    }

    for (Pending& p : pending) {
        std::string target = std::move(p.line_dest);
        if (p.raw.reference) {
            const auto it = definitions.find(normalize_label(p.raw.label));
            if (it == definitions.end()) continue;  // an unresolved reference is plain text
            target = it->second;
        }
        if (target.empty()) continue;
        outline.links.push_back({p.line_start + p.raw.whole.begin, p.line_start + p.raw.whole.end, p.line_start + p.raw.text.begin,
                                 p.line_start + p.raw.text.end, std::move(target)});
    }
    return outline;
}

}  // namespace mod
