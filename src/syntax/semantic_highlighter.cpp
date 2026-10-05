#include "syntax/semantic_highlighter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <format>
#include <limits>
#include <utility>

#include "text/utf8.hpp"

namespace mod {
namespace {

struct NamedStyle {
    std::string_view name;
    Style style;
};

constexpr std::array<NamedStyle, 23> kTypeStyles = {{
    {"namespace", Style::lsp_namespace},   {"type", Style::lsp_type},
    {"class", Style::lsp_class},           {"enum", Style::lsp_enum},
    {"interface", Style::lsp_interface},   {"struct", Style::lsp_struct},
    {"typeParameter", Style::lsp_type_parameter}, {"parameter", Style::lsp_parameter},
    {"variable", Style::lsp_variable},     {"property", Style::lsp_property},
    {"enumMember", Style::lsp_enum_member}, {"event", Style::lsp_event},
    {"function", Style::lsp_function},     {"method", Style::lsp_method},
    {"macro", Style::lsp_macro},           {"keyword", Style::lsp_keyword},
    {"modifier", Style::lsp_modifier},     {"comment", Style::lsp_comment},
    {"string", Style::lsp_string},         {"number", Style::lsp_number},
    {"regexp", Style::lsp_regexp},         {"operator", Style::lsp_operator},
    {"decorator", Style::lsp_decorator},
}};

std::span<const std::byte> as_span(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

// A position in a line, in UTF-16 code units and in bytes. A line's tokens arrive in order,
// so each conversion walks on from the last one instead of from the line start.
struct Utf16Cursor {
    std::uint64_t units = 0;
    std::size_t byte = 0;
};

// Byte offset of `units` UTF-16 code units into `line`, clamped to its end.
std::uint64_t utf16_to_byte(std::string_view line, std::uint64_t units, Utf16Cursor& at) {
    if (units < at.units) at = {};  // out of order: start over
    while (at.byte < line.size() && at.units < units) {
        const Decoded d = decode(as_span(line.substr(at.byte)));
        at.units += (d.valid && d.cp >= 0x10000) ? 2 : 1;
        at.byte += d.len;
    }
    return at.byte;
}

bool ends_with_nocase(std::string_view s, std::string_view suffix) {
    if (suffix.size() > s.size()) return false;
    s.remove_prefix(s.size() - suffix.size());
    return std::ranges::equal(s, suffix, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

}  // namespace

Style style_for_token_type(std::string_view name) {
    for (const NamedStyle& n : kTypeStyles)
        if (n.name == name) return n.style;
    Style best = Style::Default;  // ends with no standard name: not colored
    std::size_t best_len = 0;
    for (const NamedStyle& n : kTypeStyles) {
        if (n.name.size() > best_len && ends_with_nocase(name, n.name)) {
            best = n.style;
            best_len = n.name.size();
        }
    }
    return best;
}

std::uint8_t modifier_bit(std::string_view name) {
    if (name == "deprecated") return kModDeprecated;
    if (name == "readonly") return kModReadonly;
    if (name == "documentation") return kModDocumentation;
    if (name == "defaultLibrary") return kModDefaultLibrary;
    if (name == "declaration" || name == "definition") return kModDeclaration;
    return 0;
}

SemanticHighlighter::SemanticHighlighter(const Document& doc, const LanguageServerSpec& spec, LspServerPool& servers)
    : doc_(doc) {
    if (doc.text().size() > spec.max_file_bytes) {
        status_ = "LSP off: file too large";
        return;
    }
    build_line_index();
    const std::filesystem::path path = std::filesystem::absolute(doc.path());
    LspClient::Callbacks callbacks;
    callbacks.on_ready = [this] {
        build_legend_map();
        request();
    };
    callbacks.on_tokens = [this](TokenResponse r) { on_tokens(std::move(r)); };
    client_ = servers.acquire(spec, find_project_root(path));
    doc_id_ = client_->open_document(file_uri(path), spec.id, doc.text(), std::move(callbacks));
}

SemanticHighlighter::~SemanticHighlighter() {
    // The server shuts down when its last document lets go of it.
    if (client_) client_->close_document(doc_id_);
}

std::string SemanticHighlighter::status() const { return client_ ? client_->status_text() : status_; }

// ---- the line index ---------------------------------------------------------------

void SemanticHighlighter::build_line_index() {
    line_starts_.assign(1, 0);
    std::uint64_t pos = 0;
    doc_.text().read(0, doc_.text().size(), [&](std::span<const std::byte> s) {
        const char* d = reinterpret_cast<const char*>(s.data());
        for (const char* p = d; (p = static_cast<const char*>(std::memchr(p, '\n', s.size() - static_cast<std::size_t>(p - d)))) != nullptr; ++p)
            line_starts_.push_back(pos + static_cast<std::uint64_t>(p - d) + 1);
        pos += s.size();
        return true;
    });
}

std::uint64_t SemanticHighlighter::line_of(std::uint64_t offset) const {
    const auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    return static_cast<std::uint64_t>(it - line_starts_.begin()) - 1;
}

std::uint64_t SemanticHighlighter::line_end(std::uint64_t line) const {
    return line + 1 < line_starts_.size() ? line_starts_[line + 1] - 1 : doc_.text().size();
}

LspPosition SemanticHighlighter::position_of(std::uint64_t offset) const {
    const std::uint64_t line = line_of(offset);
    const std::uint64_t start = line_starts_[line];
    if (client_->encoding() == PositionEncoding::utf8) return {line, offset - start};
    const std::string prefix = doc_.text().read(start, offset - start);
    return {line, utf16_length(as_span(prefix))};
}

// ---- changes ------------------------------------------------------------------------

void SemanticHighlighter::before_change(const ChangeEvent& ev) {
    if (!client_ || burst_) return;
    pending_change_ = {position_of(ev.offset), position_of(ev.offset + ev.removed_len)};
}

void SemanticHighlighter::after_change(const ChangeEvent& ev) {
    if (!client_) return;
    // A burst (a Replace All): past kBurstChanges changes before the next tick, keeping the
    // line index, the cached spans and the server's copy in step change by change would
    // cost O(lines + spans) each. The spans are dropped instead, and the next tick rebuilds
    // the index and sends the whole text once.
    if (!burst_ && ++changes_since_tick_ > kBurstChanges) {
        burst_ = true;
        spans_.clear();
    }
    if (burst_) {
        pending_change_.reset();
        debounce_reset_ = true;
        return;
    }
    const std::uint64_t removed_end = ev.offset + ev.removed_len;
    const auto shift = [&](std::uint64_t p) { return p - ev.removed_len + ev.inserted_len; };

    // Line starts: drop those inside the removed text, shift the later ones, add the
    // inserted line feeds.
    std::string inserted_copy;
    std::string_view inserted;
    if (ev.inserted_small) {
        inserted = *ev.inserted_small;
    } else {
        inserted_copy = doc_.text().read(ev.offset, ev.inserted_len);
        inserted = inserted_copy;
    }
    auto first = std::upper_bound(line_starts_.begin(), line_starts_.end(), ev.offset);
    auto last = std::upper_bound(first, line_starts_.end(), removed_end);
    for (auto it = last; it != line_starts_.end(); ++it) *it = shift(*it);
    std::vector<std::uint64_t> added;
    for (std::size_t i = 0; (i = inserted.find('\n', i)) != std::string_view::npos; ++i)
        added.push_back(ev.offset + i + 1);
    const auto at = line_starts_.erase(first, last);
    line_starts_.insert(at, added.begin(), added.end());

    // Cached spans: keep those before the edit, shift those after it, and trim those
    // that overlap it, until fresh tokens arrive.
    std::vector<StyleSpan> kept;
    kept.reserve(spans_.size());
    for (StyleSpan s : spans_) {
        if (s.end <= ev.offset) {
            kept.push_back(s);
            continue;
        }
        s.start = s.start >= removed_end ? shift(s.start)
                  : s.start < ev.offset  ? s.start
                                         : ev.offset + ev.inserted_len;
        s.end = s.end >= removed_end ? shift(s.end) : ev.offset;
        // A token that contained the edit point keeps covering the typed text unless it
        // would swallow a new line.
        if (s.start < ev.offset && s.end > ev.offset + ev.inserted_len && inserted.find('\n') != std::string_view::npos)
            s.end = ev.offset;
        if (s.end > s.start) kept.push_back(s);
    }
    spans_ = std::move(kept);

    const auto [start, end] = pending_change_.value_or(std::pair{position_of(ev.offset), position_of(ev.offset)});
    pending_change_.reset();
    client_->did_change(doc_id_, start, end, inserted);
    debounce_reset_ = true;
}

void SemanticHighlighter::reloaded() {
    if (!client_) return;
    build_line_index();
    spans_.clear();
    client_->did_change_full(doc_id_);
    debounce_reset_ = true;
}

void SemanticHighlighter::saved() {
    if (client_) client_->did_save(doc_id_);
}

// ---- requests -----------------------------------------------------------------------

void SemanticHighlighter::visible_range_changed(std::uint64_t first_offset, std::uint64_t last_offset) {
    if (!client_) return;
    const std::uint64_t total = line_starts_.size();
    const std::uint64_t first = line_of(first_offset);
    const std::uint64_t last = line_of(std::max(first_offset, last_offset));
    const LineRange range{first > kRangeMarginLines ? first - kRangeMarginLines : 0,
                          std::min(total, last + 1 + kRangeMarginLines)};
    if (visible_ && visible_->first <= first && visible_->second > last) return;  // still covered
    visible_ = range;
    if (client_->supports_range()) debounce_reset_ = true;
}

void SemanticHighlighter::request() {
    if (!client_ || !client_->ready(doc_id_)) return;
    std::optional<LineRange> range;
    if (client_->supports_range() && (visible_ || !client_->supports_full()))
        range = visible_.value_or(LineRange{0, std::min<std::uint64_t>(line_starts_.size(), 200)});
    client_->request_tokens(doc_id_, range);
}

std::optional<Highlighter::Clock::time_point> SemanticHighlighter::tick(Clock::time_point now) {
    if (!client_) return std::nullopt;
    if (burst_) {
        build_line_index();
        client_->did_change_full(doc_id_);
        burst_ = false;
    }
    changes_since_tick_ = 0;
    if (debounce_reset_) {
        deadline_ = now + kDebounce;
        debounce_reset_ = false;
    }
    if (deadline_ && now >= *deadline_) {
        deadline_.reset();
        request();
    }
    const auto client_next = client_->tick(now);
    if (!deadline_) return client_next;
    if (!client_next) return deadline_;
    return std::min(*deadline_, *client_next);
}

void SemanticHighlighter::build_legend_map() {
    const TokenLegend& legend = client_->legend();
    type_styles_.clear();
    for (const std::string& name : legend.types) type_styles_.push_back(style_for_token_type(name));
    modifier_bits_.clear();
    for (const std::string& name : legend.modifiers) modifier_bits_.push_back(modifier_bit(name));
}

// ---- decoding -----------------------------------------------------------------------

void SemanticHighlighter::on_tokens(TokenResponse r) {
    if (r.version != client_->version(doc_id_)) {
        debounce_reset_ = true;  // computed for an older text: ask again
        return;
    }
    const bool utf8 = client_->encoding() == PositionEncoding::utf8;
    const PieceTree& text = doc_.text();

    std::vector<StyleSpan> fresh;
    fresh.reserve(r.data.size() / 5);
    std::uint64_t line = 0;
    std::uint64_t start_char = 0;
    std::uint64_t cached_line = std::numeric_limits<std::uint64_t>::max();
    std::string line_text;
    Utf16Cursor cursor;
    std::uint64_t last_end = 0;
    for (std::size_t i = 0; i + 5 <= r.data.size(); i += 5) {
        const std::uint32_t delta_line = r.data[i];
        const std::uint32_t delta_start = r.data[i + 1];
        const std::uint32_t length = r.data[i + 2];
        const std::uint32_t type = r.data[i + 3];
        const std::uint32_t mods = r.data[i + 4];
        line += delta_line;
        start_char = delta_line == 0 ? start_char + delta_start : delta_start;
        if (line >= line_starts_.size()) break;
        const std::uint64_t ls = line_starts_[line];
        const std::uint64_t le = line_end(line);
        std::uint64_t b0;
        std::uint64_t b1;
        if (utf8) {
            b0 = std::min(start_char, le - ls);
            b1 = std::min(start_char + length, le - ls);  // clamped to the line
        } else {
            if (cached_line != line) {
                line_text = text.read(ls, le - ls);
                cached_line = line;
                cursor = {};
            }
            b0 = utf16_to_byte(line_text, start_char, cursor);
            b1 = utf16_to_byte(line_text, start_char + length, cursor);
        }
        const Style style = type < type_styles_.size() ? type_styles_[type] : Style::Default;
        if (style == Style::Default) continue;
        std::uint8_t bits = 0;
        for (std::size_t m = 0; m < modifier_bits_.size() && m < 32; ++m)
            if ((mods >> m) & 1u) bits |= modifier_bits_[m];
        StyleSpan s{ls + b0, ls + b1, style, bits};
        s.start = std::max(s.start, last_end);  // never overlap
        if (s.end <= s.start) continue;
        last_end = s.end;
        fresh.push_back(s);
    }

    if (!r.range) {
        spans_ = std::move(fresh);
        return;
    }
    // Replace the cached spans of the requested lines only.
    const std::uint64_t lo = r.range->first < line_starts_.size() ? line_starts_[r.range->first] : text.size();
    const std::uint64_t hi = r.range->second < line_starts_.size() ? line_starts_[r.range->second] : text.size();
    std::erase_if(fresh, [&](const StyleSpan& s) { return s.start < lo || s.end > hi; });  // outside the request
    std::vector<StyleSpan> merged;
    merged.reserve(spans_.size() + fresh.size());
    auto it = spans_.begin();
    for (; it != spans_.end() && it->end <= lo; ++it) merged.push_back(*it);
    for (; it != spans_.end() && it->start < hi; ++it) {}
    merged.insert(merged.end(), fresh.begin(), fresh.end());
    merged.insert(merged.end(), it, spans_.end());
    spans_ = std::move(merged);
}

std::vector<StyleSpan> SemanticHighlighter::spans_for_line(std::uint64_t line_start, std::string_view line_bytes) {
    std::vector<StyleSpan> out;
    const std::uint64_t end = line_start + line_bytes.size();
    auto it = std::upper_bound(spans_.begin(), spans_.end(), line_start,
                               [](std::uint64_t off, const StyleSpan& s) { return off < s.end; });
    for (; it != spans_.end() && it->start < end; ++it) {
        StyleSpan s = *it;
        s.start = std::max(s.start, line_start);
        s.end = std::min(s.end, end);
        if (s.end > s.start) out.push_back(s);
    }
    return out;
}

}  // namespace mod
