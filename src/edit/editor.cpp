#include "edit/editor.hpp"

#include <algorithm>
#include <string>

#include "text/utf8.hpp"
#include "text/wrap.hpp"

namespace mod {
namespace {

constexpr std::uint64_t kCheckpointStep = 4096;
constexpr std::uint64_t kColumnBlock = 64 * 1024;
// Windows for the boundary functions: a capped cluster needs at most 32 x 4 bytes, and
// a backward search needs up to 64 code points of context.
constexpr std::uint64_t kFirstWindow = 128;
constexpr std::uint64_t kMaxNextWindow = std::uint64_t{4} * MAX_CLUSTER_CODE_POINTS * 4;
constexpr std::uint64_t kMaxPrevWindow = std::uint64_t{2} * MAX_CLUSTER_CODE_POINTS * 4 * 4;

std::span<const std::byte> bytes_of(const std::string& s) { return std::as_bytes(std::span(s.data(), s.size())); }

}  // namespace

Editor::Editor(Document& doc, Clipboard& clipboard, int tab_width)
    : doc_(doc), clipboard_(clipboard), tab_width_(std::clamp(tab_width, 1, 16)) {
    doc_.add_listener(this);
}

Editor::~Editor() {
    if (pasting_) doc_.end_group();  // a paste cut off: its pieces so far stay one step
    doc_.remove_listener(this);
}

std::uint64_t Editor::size() const { return doc_.text().size(); }

std::optional<std::pair<std::uint64_t, std::uint64_t>> Editor::selection() const {
    if (!anchor_ || *anchor_ == cursor_) return std::nullopt;
    return std::pair{std::min(*anchor_, cursor_), std::max(*anchor_, cursor_)};
}

// ---- positions ---------------------------------------------------------------------------

std::uint64_t Editor::snap(std::uint64_t pos) const {
    const PieceTree& t = doc_.text();
    pos = std::min(pos, t.size());
    if (pos == 0 || pos >= t.size()) return pos;
    if ((std::to_integer<unsigned>(t.byte_at(pos)) & 0xC0) == 0x80) {
        // Inside a valid multi-byte sequence: move to its lead byte.
        for (std::uint64_t back = 1; back <= 3 && back <= pos; ++back) {
            if ((std::to_integer<unsigned>(t.byte_at(pos - back)) & 0xC0) == 0x80) continue;
            const std::string s = t.read(pos - back, 4);
            const Decoded d = decode(bytes_of(s));
            if (d.valid && back < d.len) pos -= back;
            break;
        }
    }
    if (pos > 0 && t.byte_at(pos) == std::byte{'\n'} && t.byte_at(pos - 1) == std::byte{'\r'}) --pos;
    return pos;
}

std::uint64_t Editor::next_boundary(std::uint64_t pos) const {
    const PieceTree& t = doc_.text();
    if (pos >= t.size()) return t.size();
    for (std::uint64_t w = kFirstWindow;; w *= 2) {
        const std::uint64_t n = std::min(w, t.size() - pos);
        const std::string s = t.read(pos, n);
        const bool at_end = pos + n == t.size();
        const ClusterResult r = next_grapheme_boundary(bytes_of(s), at_end || w >= kMaxNextWindow);
        if (r.kind == ClusterResult::found) return pos + std::max<std::uint64_t>(1, r.length);
    }
}

std::uint64_t Editor::prev_boundary(std::uint64_t pos) const {
    const PieceTree& t = doc_.text();
    if (pos == 0) return 0;
    for (std::uint64_t w = kFirstWindow;; w *= 2) {
        const std::uint64_t start = pos > w ? pos - w : 0;
        const std::string s = t.read(start, pos - start);
        const ClusterResult r = prev_grapheme_boundary(bytes_of(s), start == 0 || w >= kMaxPrevWindow);
        if (r.kind == ClusterResult::found) return pos - std::clamp<std::uint64_t>(r.length, 1, pos);
    }
}

CharClass Editor::class_at(std::uint64_t pos) const {
    const std::string s = doc_.text().read(pos, 4);
    return char_class(decode(bytes_of(s)));
}

// VS Code rule: skip whitespace, then a run of one class. Line feeds are stops of their own.
std::uint64_t Editor::word_right(std::uint64_t pos) const {
    const std::uint64_t end = size();
    if (pos >= end) return end;
    if (class_at(pos) == CharClass::newline) return next_boundary(pos);
    while (pos < end && class_at(pos) == CharClass::space) pos = next_boundary(pos);
    if (pos >= end || class_at(pos) == CharClass::newline) return pos;
    const CharClass k = class_at(pos);
    while (pos < end && class_at(pos) == k) pos = next_boundary(pos);
    return pos;
}

std::uint64_t Editor::word_left(std::uint64_t pos) const {
    if (pos == 0) return 0;
    std::uint64_t prev = prev_boundary(pos);
    if (class_at(prev) == CharClass::newline) return prev;
    while (pos > 0 && class_at(prev) == CharClass::space) {
        pos = prev;
        if (pos > 0) prev = prev_boundary(pos);
    }
    if (pos == 0 || class_at(prev) == CharClass::newline) return pos;
    const CharClass k = class_at(prev);
    while (pos > 0 && class_at(prev) == k) {
        pos = prev;
        if (pos > 0) prev = prev_boundary(pos);
    }
    return pos;
}

std::uint64_t Editor::line_start(std::uint64_t pos) const {
    const std::uint64_t lf = doc_.text().find_lf_backward(pos, pos);
    return lf == PieceTree::npos ? 0 : lf + 1;
}

std::uint64_t Editor::line_end(std::uint64_t pos) const {
    const PieceTree& t = doc_.text();
    const std::uint64_t lf = t.find_lf_forward(pos, t.size() - pos);
    if (lf == PieceTree::npos) return t.size();
    if (lf > pos && t.byte_at(lf - 1) == std::byte{'\r'}) return lf - 1;  // before CR LF
    return lf;
}

// Display column of `pos`, walking code points from the nearest checkpoint.
std::uint64_t Editor::column_at(std::uint64_t ls, std::uint64_t pos) {
    const PieceTree& t = doc_.text();
    // Only a line long enough to get a mark gets an entry; short lines leave the map alone.
    const auto found = checkpoints_.find(ls);
    std::vector<std::pair<std::uint64_t, std::uint64_t>>* marks = found == checkpoints_.end() ? nullptr : &found->second;
    std::uint64_t o = ls;
    std::uint64_t col = 0;
    if (marks) {
        for (const auto& [mo, mc] : *marks) {
            if (mo > pos) break;
            o = mo;
            col = mc;
        }
    }
    std::uint64_t next_mark = ls + ((o - ls) / kCheckpointStep + 1) * kCheckpointStep;
    while (o < pos) {
        const std::uint64_t n = std::min(kColumnBlock, pos - o);
        const std::string s = t.read(o, std::min(n + 4, t.size() - o));
        const auto b = bytes_of(s);
        std::uint64_t i = 0;
        while (i < n) {
            const Decoded d = decode(b.subspan(static_cast<std::size_t>(i)));
            col += static_cast<std::uint64_t>(display_width(d, col, tab_width_));
            i += std::max<std::uint64_t>(1, d.len);
            if (o + i >= next_mark) {
                if (!marks) marks = &checkpoints_[ls];
                if (marks->empty() || marks->back().first < o + i) marks->emplace_back(o + i, col);
                next_mark += kCheckpointStep;
            }
        }
        o += i;
    }
    return col;
}

// The first cluster boundary on the line at or past `column`, or the line end.
std::uint64_t Editor::offset_at_column(std::uint64_t ls, std::uint64_t column) {
    const PieceTree& t = doc_.text();
    const std::uint64_t le = line_end(ls);
    std::uint64_t o = ls;
    std::uint64_t col = 0;
    if (const auto found = checkpoints_.find(ls); found != checkpoints_.end()) {
        for (const auto& [mo, mc] : found->second) {
            if (mc > column || mo > le) break;
            o = mo;
            col = mc;
        }
    }
    while (o < le && col < column) {
        const std::uint64_t next = std::min(next_boundary(o), le);
        const std::string s = t.read(o, next - o);
        const auto b = bytes_of(s);
        for (std::size_t i = 0; i < b.size();) {
            const Decoded d = decode(b.subspan(i));
            col += static_cast<std::uint64_t>(display_width(d, col, tab_width_));
            i += std::max<std::size_t>(1, d.len);
        }
        o = next;
    }
    return o;
}

std::uint64_t Editor::vertical(std::uint64_t pos, std::int64_t lines) {
    if (const auto width = wrap_width_) return vertical_rows(pos, lines, *width);
    const PieceTree& t = doc_.text();
    std::uint64_t ls = line_start(pos);
    if (!sticky_column_) sticky_column_ = column_at(ls, pos);
    for (; lines > 0; --lines) {
        const std::uint64_t lf = t.find_lf_forward(ls, t.size() - ls);
        if (lf == PieceTree::npos) break;
        ls = lf + 1;
    }
    for (; lines < 0 && ls > 0; ++lines) ls = line_start(ls - 1);
    return offset_at_column(ls, *sticky_column_);
}

void Editor::set_cursor(std::uint64_t pos, bool extend) {
    if (extend) {
        if (!anchor_) anchor_ = cursor_;
    } else {
        anchor_.reset();
    }
    cursor_ = pos;
    if (anchor_ && *anchor_ == cursor_) anchor_.reset();
}

void Editor::move(Motion motion, bool extend, std::uint64_t page_rows) {
    const auto sel = selection();
    std::uint64_t target = cursor_;
    bool keep_sticky = false;
    const auto rows = static_cast<std::int64_t>(std::max<std::uint64_t>(1, page_rows));
    switch (motion) {
        case Motion::Left:
            if (!extend && sel) {
                set_cursor(sel->first, false);
                sticky_column_.reset();
                return;
            }
            target = prev_boundary(cursor_);
            break;
        case Motion::Right:
            if (!extend && sel) {
                set_cursor(sel->second, false);
                sticky_column_.reset();
                return;
            }
            target = next_boundary(cursor_);
            break;
        case Motion::WordLeft: target = word_left(cursor_); break;
        case Motion::WordRight: target = word_right(cursor_); break;
        case Motion::Up: target = vertical(cursor_, -1); keep_sticky = true; break;
        case Motion::Down: target = vertical(cursor_, 1); keep_sticky = true; break;
        case Motion::PageUp: target = vertical(cursor_, -rows); keep_sticky = true; break;
        case Motion::PageDown: target = vertical(cursor_, rows); keep_sticky = true; break;
        case Motion::LineStart: target = line_start(cursor_); break;
        case Motion::LineEnd: target = line_end(cursor_); break;
        case Motion::DocStart: target = 0; break;
        case Motion::DocEnd: target = size(); break;
    }
    if (!keep_sticky) sticky_column_.reset();
    set_cursor(snap(target), extend);
}

// ---- editing -----------------------------------------------------------------------------

void Editor::delete_selection(EditKind kind) {
    const auto sel = selection();
    if (!sel) return;
    doc_.apply(sel->first, sel->second - sel->first, std::string_view{}, kind, cursor_, sel->first);
    anchor_.reset();
    cursor_ = snap(sel->first);  // the edit may have joined bytes into one character
}

void Editor::insert_text(std::string_view bytes, EditKind kind) {
    sticky_column_.reset();
    if (selection()) {
        Document::GroupGuard group(doc_, kind);  // replacing a selection is one node
        delete_selection(kind);
        doc_.apply(cursor_, 0, bytes, kind, cursor_, cursor_ + bytes.size());
    } else {
        if (bytes.empty()) return;
        doc_.apply(cursor_, 0, bytes, kind, cursor_, cursor_ + bytes.size());
    }
    cursor_ = snap(cursor_ + bytes.size());
    anchor_.reset();
}

void Editor::indent(bool spaces) {
    if (!spaces) {
        insert_text("\t", EditKind::typing);
        return;
    }
    // The cursor's display column (tabs expanded), after any selection is replaced.
    const auto sel = selection();
    const std::uint64_t at = sel ? sel->first : cursor_;
    const std::uint64_t col = column_at(line_start(at), at);
    const auto width = static_cast<std::uint64_t>(tab_width_);
    insert_text(std::string(static_cast<std::size_t>(width - col % width), ' '), EditKind::typing);
}

void Editor::outdent() {
    sticky_column_.reset();
    const auto sel = selection();
    const std::uint64_t from = line_start(sel ? sel->first : cursor_);
    const std::uint64_t to = sel ? sel->second : cursor_;
    const PieceTree& t = doc_.text();
    // The starts of the lines touched, last first, so earlier offsets stay valid.
    std::vector<std::uint64_t> starts;
    for (std::uint64_t ls = from;;) {
        starts.push_back(ls);
        const std::uint64_t lf = ls < t.size() ? t.find_lf_forward(ls, t.size() - ls) : PieceTree::npos;
        if (lf == PieceTree::npos || lf + 1 >= to || (sel && lf + 1 == sel->second)) break;
        ls = lf + 1;
    }
    Document::GroupGuard group(doc_, EditKind::other);
    for (auto it = starts.rbegin(); it != starts.rend(); ++it) {
        const std::uint64_t ls = *it;
        const std::string head = t.read(ls, std::min<std::uint64_t>(static_cast<std::uint64_t>(tab_width_), t.size() - ls));
        std::uint64_t n = 0;
        if (!head.empty() && head[0] == '\t') {
            n = 1;
        } else {
            while (n < head.size() && head[n] == ' ') ++n;
        }
        if (n == 0) continue;
        auto shift = [&](std::uint64_t p) { return p <= ls ? p : p < ls + n ? ls : p - n; };
        const std::uint64_t cursor_after = shift(cursor_);
        doc_.apply(ls, n, std::string_view{}, EditKind::other, cursor_, cursor_after);  // after_change moves cursor and anchor
    }
}

void Editor::set_tab_width(int width) {
    tab_width_ = std::clamp(width, 1, 16);
    sticky_column_.reset();
    checkpoints_.clear();
}

void Editor::set_wrap_width(std::optional<int> columns) {
    if (columns) columns = std::max(1, *columns);
    if (columns == wrap_width_) return;
    wrap_width_ = columns;
    sticky_column_.reset();  // a column measured in the other layout means nothing here
}

// Up and Down by soft-wrapped row. The sticky column is measured inside the row, with
// tab stops counted from the row's start, as the view draws it.
std::uint64_t Editor::vertical_rows(std::uint64_t pos, std::int64_t rows, int width) {
    const PieceTree& t = doc_.text();
    const WrapLayout wrap(t, width, tab_width_);
    auto advance = [&](std::uint64_t from, std::uint64_t to, std::uint64_t col) {
        const std::string s = t.read(from, to - from);
        const auto b = bytes_of(s);
        for (std::size_t i = 0; i < b.size();) {
            const Decoded d = decode(b.subspan(i));
            col += static_cast<std::uint64_t>(std::max(0, display_width(d, col, tab_width_)));
            i += std::max<std::size_t>(1, d.len);
        }
        return col;
    };
    std::uint64_t row = wrap.row_start(pos);
    if (!sticky_column_) sticky_column_ = advance(row, std::max(row, pos), 0);
    for (; rows > 0; --rows) {
        const std::uint64_t next = wrap.next_row(row);
        if (next == WrapLayout::npos) break;
        row = next;
    }
    for (; rows < 0; ++rows) {
        const std::uint64_t prev = wrap.prev_row(row);
        if (prev == WrapLayout::npos) break;
        row = prev;
    }
    const std::uint64_t end = wrap.row_end(row);
    const bool last_row = end >= wrap.line_end(wrap.line_start(row));
    std::uint64_t o = row;
    std::uint64_t col = 0;
    while (o < end && col < *sticky_column_) {
        const std::uint64_t next = std::min(next_boundary(o), end);
        if (!last_row && next >= end) break;  // the end of this row is the start of the next
        col = advance(o, next, col);
        o = next;
    }
    return o;
}

void Editor::newline() { insert_text(doc_.line_ending(), EditKind::newline); }

void Editor::delete_backward(bool word) {
    sticky_column_.reset();
    if (selection()) {
        delete_selection(EditKind::delete_);
        return;
    }
    if (cursor_ == 0) return;
    const std::uint64_t start = word ? word_left(cursor_) : prev_boundary(cursor_);
    doc_.apply(start, cursor_ - start, std::string_view{}, EditKind::delete_, cursor_, start);
    cursor_ = snap(start);  // the edit may have joined bytes into one character
    anchor_.reset();
}

void Editor::delete_forward(bool word) {
    sticky_column_.reset();
    if (selection()) {
        delete_selection(EditKind::delete_);
        return;
    }
    if (cursor_ >= size()) return;
    const std::uint64_t end = word ? word_right(cursor_) : next_boundary(cursor_);
    doc_.apply(cursor_, end - cursor_, std::string_view{}, EditKind::delete_, cursor_, cursor_);
    anchor_.reset();
}

bool Editor::copy() {
    const auto sel = selection();
    if (!sel) return false;
    const PieceTree& t = doc_.text();
    const std::uint64_t len = sel->second - sel->first;
    PieceRun run = t.pieces(sel->first, len);
    std::vector<FrozenBytes> views;
    views.reserve(run.size());
    for (const Piece& p : run) views.push_back(t.frozen_bytes(p.buffer, p.offset, p.length));
    clipboard_.set(std::move(run), std::move(views), doc_.id(), [&] { return t.read(sel->first, len); });
    return true;
}

void Editor::cut() {
    const auto sel = selection();
    if (!sel) return;
    copy();
    doc_.apply(sel->first, sel->second - sel->first, std::string_view{}, EditKind::cut, cursor_, sel->first);
    cursor_ = snap(sel->first);  // the edit may have joined bytes into one character
    anchor_.reset();
    sticky_column_.reset();
}

void Editor::cut_to_line_end(bool append) {
    std::uint64_t start = cursor_;
    std::uint64_t end = cursor_;
    if (const auto sel = selection()) {
        start = sel->first;
        end = sel->second;
    } else {
        const PieceTree& t = doc_.text();
        const std::uint64_t le = line_end(cursor_);
        if (cursor_ < le) {
            end = le;
        } else if (le < t.size()) {
            // At the line end the line break goes, CR LF whole.
            end = t.byte_at(le) == std::byte{'\r'} && le + 1 < t.size() ? le + 2 : le + 1;
        } else {
            return;  // the end of the text: nothing to cut
        }
    }
    const PieceTree& t = doc_.text();
    const std::uint64_t len = end - start;
    PieceRun run = t.pieces(start, len);
    std::vector<FrozenBytes> views;
    views.reserve(run.size());
    for (const Piece& p : run) views.push_back(t.frozen_bytes(p.buffer, p.offset, p.length));
    if (append) {
        clipboard_.append(std::move(run), std::move(views), doc_.id());
    } else {
        clipboard_.set(std::move(run), std::move(views), doc_.id(), [&] { return t.read(start, len); });
    }
    doc_.apply(start, len, std::string_view{}, EditKind::cut, cursor_, start);
    cursor_ = snap(start);  // the edit may have joined bytes into one character
    anchor_.reset();
    sticky_column_.reset();
}

void Editor::paste() {
    const ClipContent* c = clipboard_.get();
    if (c == nullptr || c->length == 0) return;
    sticky_column_.reset();
    // Content from another document is pasted as a copy of its bytes: its buffers are
    // not in this document's table.
    std::string foreign;
    if (c->source != doc_.id()) {
        foreign.reserve(static_cast<std::size_t>(c->length));
        for (const FrozenBytes& v : c->views) foreign.append(reinterpret_cast<const char*>(v.bytes.data()), v.bytes.size());
    }
    const InsertContent content = c->source == doc_.id() ? InsertContent(c->run) : InsertContent(std::string_view(foreign));
    const std::uint64_t len = c->length;
    {
        Document::GroupGuard group(doc_, EditKind::paste);
        delete_selection(EditKind::paste);
        doc_.apply(cursor_, 0, content, EditKind::paste, cursor_, cursor_ + len);
    }
    cursor_ = snap(cursor_ + len);
    anchor_.reset();
}

void Editor::paste_text(std::string_view bytes, bool more) {
    // The pieces of one paste are one undo step.
    if (!pasting_) {
        doc_.begin_group(EditKind::paste);
        pasting_ = true;
    }
    if (!bytes.empty()) insert_pasted(bytes);
    if (!more) {
        pasting_ = false;
        doc_.end_group();
    }
}

void Editor::insert_pasted(std::string_view bytes) {
    // Terminals send each newline of a paste as a bare CR; CR LF and LF also count once.
    const std::string_view ending = doc_.line_ending();
    std::string text;
    text.reserve(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\r') {
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n') ++i;
            text += ending;
        } else if (bytes[i] == '\n') {
            text += ending;
        } else {
            text += bytes[i];
        }
    }
    insert_text(text, EditKind::paste);
}

void Editor::select_range(std::uint64_t start, std::uint64_t end) {
    anchor_ = std::min(start, size());
    cursor_ = std::min(end, size());
    sticky_column_.reset();
    if (*anchor_ == cursor_) anchor_.reset();
}

Status Editor::undo() {
    auto r = doc_.undo();
    if (!r) return std::unexpected(r.error());
    cursor_ = snap(*r);
    anchor_.reset();
    sticky_column_.reset();
    return {};
}

Status Editor::redo() {
    auto r = doc_.redo();
    if (!r) return std::unexpected(r.error());
    cursor_ = snap(*r);
    anchor_.reset();
    sticky_column_.reset();
    return {};
}

// ---- listener ----------------------------------------------------------------------------

void Editor::after_change(const ChangeEvent& ev) {
    checkpoints_.clear();
    auto shift = [&](std::uint64_t p) {
        if (p <= ev.offset) return p;
        if (p >= ev.offset + ev.removed_len) return p - ev.removed_len + ev.inserted_len;
        return ev.offset;  // inside the removed range
    };
    // An edit can join bytes on either side into one character: stay on its boundary.
    cursor_ = snap(shift(cursor_));
    if (anchor_) anchor_ = snap(shift(*anchor_));
    if (anchor_ && *anchor_ == cursor_) anchor_.reset();
}

void Editor::reloaded() {
    checkpoints_.clear();
    cursor_ = snap(std::min(cursor_, size()));
    anchor_.reset();
    sticky_column_.reset();
}

}  // namespace mod
