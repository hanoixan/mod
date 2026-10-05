#include "syntax/syntax_highlighter.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_set>

#include "edit/document.hpp"

namespace mod {

// A SyntaxSpec prepared for scanning: every opening marker longest first, and the
// word lists as sets.
class SyntaxRules {
public:
    enum class Kind { line_comment, block_comment, string };
    struct Opener {
        std::string_view text;
        Kind kind;
        std::size_t index;
    };

    explicit SyntaxRules(SyntaxSpec s) : spec(std::move(s)) {
        for (std::size_t i = 0; i < spec.line_comments.size(); ++i) openers.push_back({spec.line_comments[i], Kind::line_comment, i});
        for (std::size_t i = 0; i < spec.block_comments.size(); ++i) openers.push_back({spec.block_comments[i].first, Kind::block_comment, i});
        for (std::size_t i = 0; i < spec.strings.size(); ++i) openers.push_back({spec.strings[i].open, Kind::string, i});
        std::ranges::stable_sort(openers, [](const Opener& a, const Opener& b) { return a.text.size() > b.text.size(); });
        for (const std::string& w : spec.keywords) keywords.insert(fold(w));
        for (const std::string& w : spec.constants) constants.insert(fold(w));
        prefixes.insert(spec.string_prefixes.begin(), spec.string_prefixes.end());
    }

    std::string fold(std::string_view w) const {
        std::string out(w);
        if (!spec.case_sensitive)
            for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    SyntaxSpec spec;
    std::vector<Opener> openers;
    std::unordered_set<std::string> keywords;
    std::unordered_set<std::string> constants;
    std::unordered_set<std::string> prefixes;
};

namespace {

using Kind = SyntaxRules::Kind;

bool word_start(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalpha(u) || c == '_' || u >= 0x80;
}

bool word_char(char c) { return word_start(c) || std::isdigit(static_cast<unsigned char>(c)); }

// The offset just past `close` at or after `from`, skipping escaped characters; npos when absent.
std::size_t find_close(std::string_view line, std::size_t from, std::string_view close, std::optional<char> escape) {
    for (std::size_t i = from; i < line.size();) {
        if (escape && line[i] == *escape) {
            i += 2;
        } else if (line.compare(i, close.size(), close) == 0) {
            return i + close.size();
        } else {
            ++i;
        }
    }
    return std::string_view::npos;
}

struct StringEnd {
    std::size_t end;
    bool carries;  // a multi-line string still open at the line's end
};

// Where a string whose delimiter sits at `delim` ends, or nullopt when it is not one.
std::optional<StringEnd> string_end(const StringRule& rule, std::string_view line, std::size_t delim) {
    const std::size_t body = delim + rule.open.size();
    const std::size_t end = find_close(line, body, rule.close, rule.escape);
    if (rule.char_literal) {
        // One character (one UTF-8 sequence) or one escape sequence, then the close.
        if (end == std::string_view::npos || end - rule.close.size() <= body) return std::nullopt;
        const std::size_t len = end - rule.close.size() - body;
        if (rule.escape && line[body] == *rule.escape) return StringEnd{end, false};
        const auto lead = static_cast<unsigned char>(line[body]);
        const std::size_t want = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        if (len != want) return std::nullopt;
        return StringEnd{end, false};
    }
    if (end == std::string_view::npos) {
        if (rule.multiline) return StringEnd{line.size(), true};
        if (rule.max_length) return std::nullopt;
        return StringEnd{line.size(), false};  // unclosed: colored to the line's end
    }
    if (rule.max_length && end - rule.close.size() - body > *rule.max_length) return std::nullopt;
    return StringEnd{end, false};
}

// Scans `line` from `st`, adding spans relative to the line to `out` when given.
SyntaxState scan(const SyntaxRules& r, SyntaxState st, std::string_view line, std::vector<StyleSpan>* out) {
    auto emit = [&](std::size_t b, std::size_t e, Style s) {
        if (out && e > b) out->push_back(StyleSpan{b, e, s, 0});
    };
    const std::size_t n = line.size();
    std::size_t i = 0;
    if (st.block_comment >= 0) {
        const std::string& close = r.spec.block_comments[static_cast<std::size_t>(st.block_comment)].second;
        const std::size_t f = line.find(close);
        if (f == std::string_view::npos) {
            emit(0, n, Style::lsp_comment);
            return st;
        }
        i = f + close.size();
        emit(0, i, Style::lsp_comment);
        st = {};
    } else if (st.string >= 0) {
        const StringRule& rule = r.spec.strings[static_cast<std::size_t>(st.string)];
        const std::size_t e = find_close(line, 0, rule.close, rule.escape);
        if (e == std::string_view::npos) {
            emit(0, n, Style::lsp_string);
            return st;
        }
        i = e;
        emit(0, i, Style::lsp_string);
        st = {};
    }

    // A string opening at `delim`, starting at `start` (before any prefix); true when one did.
    auto try_string = [&](std::size_t start, std::size_t delim, const SyntaxRules::Opener& op) {
        const StringRule& rule = r.spec.strings[op.index];
        const auto se = string_end(rule, line, delim);
        if (!se) return false;
        emit(start, se->end, Style::lsp_string);
        if (se->carries) st.string = static_cast<int>(op.index);
        i = se->end;
        return true;
    };

    while (i < n) {
        bool matched = false;
        for (const SyntaxRules::Opener& op : r.openers) {
            if (line.compare(i, op.text.size(), op.text) != 0) continue;
            if (op.kind == Kind::line_comment) {
                if (r.spec.comment_needs_space && i > 0 && line[i - 1] != ' ' && line[i - 1] != '\t') continue;
                emit(i, n, Style::lsp_comment);
                return st;
            }
            if (op.kind == Kind::block_comment) {
                const std::string& close = r.spec.block_comments[op.index].second;
                const std::size_t f = line.find(close, i + op.text.size());
                if (f == std::string_view::npos) {
                    emit(i, n, Style::lsp_comment);
                    st.block_comment = static_cast<int>(op.index);
                    return st;
                }
                emit(i, f + close.size(), Style::lsp_comment);
                i = f + close.size();
                matched = true;
                break;
            }
            if (try_string(i, i, op)) {
                matched = true;
                break;
            }
        }
        if (st != SyntaxState{}) return st;  // a multi-line string runs to the line's end
        if (matched) continue;

        const char c = line[i];
        if (r.spec.numbers && std::isdigit(static_cast<unsigned char>(c)) && (i == 0 || !word_char(line[i - 1]))) {
            std::size_t j = i + 1;
            while (j < n) {
                const char d = line[j];
                if (word_char(d) || d == '.') {
                    ++j;
                } else if ((d == '+' || d == '-') && std::strchr("eEpP", line[j - 1]) != nullptr) {
                    ++j;
                } else {
                    break;
                }
            }
            emit(i, j, Style::lsp_number);
            i = j;
        } else if (word_start(c)) {
            std::size_t j = i + 1;
            while (j < n && word_char(line[j])) ++j;
            const std::string_view word = line.substr(i, j - i);
            if (j < n && r.prefixes.contains(std::string(word))) {
                for (const SyntaxRules::Opener& op : r.openers) {
                    if (op.kind != Kind::string || line.compare(j, op.text.size(), op.text) != 0) continue;
                    if (try_string(i, j, op)) {
                        matched = true;
                        break;
                    }
                }
                if (st != SyntaxState{}) return st;
                if (matched) continue;
            }
            const std::string folded = r.fold(word);
            if (r.keywords.contains(folded)) {
                emit(i, j, Style::lsp_keyword);
            } else if (r.constants.contains(folded)) {
                emit(i, j, Style::constant);
            }
            i = j;
        } else {
            ++i;
        }
    }
    return st;
}

}  // namespace

SyntaxHighlighter::SyntaxHighlighter(const Document& doc, SyntaxSpec spec)
    : doc_(doc), rules_(std::make_unique<const SyntaxRules>(std::move(spec))) {}

SyntaxHighlighter::~SyntaxHighlighter() = default;

SyntaxLine SyntaxHighlighter::scan_line(const SyntaxSpec& spec, SyntaxState state, std::string_view line) {
    const SyntaxRules rules(spec);
    SyntaxLine out;
    out.end = scan(rules, state, line, &out.spans);
    return out;
}

SyntaxHighlighter::LineState SyntaxHighlighter::state_at(std::uint64_t line_start) {
    if (line_start == 0) return {};
    if (next_line_ && next_line_->first == line_start) return next_line_->second;
    const PieceTree& text = doc_.text();

    std::uint64_t from = 0;
    LineState ls;  // the document start: plain, trusted
    auto it = checkpoints_.upper_bound(line_start);
    if (it != checkpoints_.begin()) {
        --it;
        from = it->first;
        ls.state = it->second.state;
        ls.trusted = it->second.trusted;
        if (!ls.trusted) {
            // Repair from the nearest trusted state within the bound, dropping guesses.
            auto t = it;
            while (t != checkpoints_.begin() && !t->second.trusted) --t;
            const bool found = t->second.trusted;
            const std::uint64_t base = found ? t->first : 0;
            if (line_start - base <= kMaxBackScan) {
                from = base;
                ls.state = found ? t->second.state : SyntaxState{};
                ls.trusted = true;
                std::erase_if(checkpoints_, [&](const auto& cp) {
                    return cp.first > from && cp.first <= line_start && !cp.second.trusted;
                });
            }
        }
    }
    if (line_start - from > kMaxBackScan) {
        // Too far back: assume the plain state at the first line start in the window.
        const std::uint64_t window = line_start - kMaxBackScan;
        const std::uint64_t lf = text.find_lf_forward(window - 1, line_start - window + 1);
        from = (lf == PieceTree::npos || lf + 1 > line_start) ? line_start : lf + 1;
        ls = {SyntaxState{}, false, 0};
    }

    std::string line;
    std::uint64_t pos = from;
    ls.lines_since_checkpoint = 0;
    text.read(from, line_start - from, [&](std::span<const std::byte> s) {
        const char* d = reinterpret_cast<const char*>(s.data());
        std::size_t i = 0;
        while (i < s.size()) {
            const void* hit = std::memchr(d + i, '\n', s.size() - i);
            const std::size_t end = hit ? static_cast<std::size_t>(static_cast<const char*>(hit) - d) : s.size();
            line.append(d + i, end - i);
            if (!hit) {
                pos += s.size() - i;
                break;
            }
            ls.state = scan(*rules_, ls.state, line, nullptr);
            line.clear();
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

std::vector<StyleSpan> SyntaxHighlighter::spans_for_line(std::uint64_t line_start, std::string_view line_bytes) {
    const LineState here = state_at(line_start);
    const PieceTree& text = doc_.text();
    const std::uint64_t after = line_start + line_bytes.size();
    const bool complete = after >= text.size() || text.byte_at(after) == std::byte{'\n'};

    std::vector<StyleSpan> spans;
    const SyntaxState next = scan(*rules_, here.state, line_bytes, &spans);
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
        next_line_.reset();  // a truncated line's end state is not known
    }
    for (StyleSpan& s : spans) {
        s.start += line_start;
        s.end += line_start;
    }
    return spans;
}

void SyntaxHighlighter::after_change(const ChangeEvent& ev) {
    // The state at a line start depends only on the text before it.
    checkpoints_.erase(checkpoints_.upper_bound(ev.offset), checkpoints_.end());
    if (next_line_ && next_line_->first > ev.offset) next_line_.reset();
}

void SyntaxHighlighter::reloaded() {
    checkpoints_.clear();
    next_line_.reset();
}

}  // namespace mod
