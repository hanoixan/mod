#include "search/search.hpp"

#include <algorithm>
#include <span>

#include "text/utf8.hpp"

namespace mod {
namespace {

constexpr std::uint64_t kWindow = 4ull << 20;
constexpr std::uint64_t kWindowCap = 16ull << 20;

}  // namespace

// ---- query and jobs ------------------------------------------------------------------------

Status Searcher::set_query(std::string_view find_text, const SearchOptions& options) {
    cancel();
    last_match_.reset();
    options_ = options;
    auto compiled = Regex::compile(find_text, RegexOptions{options.case_insensitive, !options.regex, options.whole_word});
    if (!compiled) {
        regex_.reset();
        return std::unexpected(compiled.error());
    }
    regex_ = std::move(*compiled);
    return {};
}

std::optional<Match> Searcher::last_match() const {
    if (last_match_ && last_version_ == doc_.version()) return last_match_;
    return std::nullopt;
}

void Searcher::cancel() {
    if (job_ && job_->kind == Job::replace_all) doc_.end_group();  // keeps what was replaced, as one node
    job_.reset();
}

void Searcher::find_next(std::uint64_t from, Direction direction) {
    if (!regex_) return;
    cancel();
    const std::uint64_t size = doc_.text().size();
    from = std::min(from, size);
    Job job;
    job.direction = direction;
    // Repeating from the current match steps over it.
    const auto lm = last_match();
    const auto sel = editor_.selection();
    const bool on_match = lm && ((sel && sel->first == lm->start && sel->second == lm->end) ||
                                 (lm->start == lm->end && !sel && editor_.cursor() == lm->start));
    if (on_match && direction == Direction::forward) {
        from = lm->end;
        if (lm->start == lm->end) job.skip_empty_at = lm->start;
    } else if (on_match) {
        from = lm->start;
    }
    job.origin = from;
    job.pos = from;
    job.seg_end = direction == Direction::forward ? size : 0;  // backward: the match-start floor
    job.version = doc_.version();
    job_ = std::move(job);
}

Status Searcher::replace_all(std::string_view replacement) {
    if (!regex_) return std::unexpected(make_error(ErrorCode::regex, "no search pattern"));
    // Check the template against the pattern's groups before anything is replaced.
    const Match probe{0, 0, std::vector(regex_->group_count(), std::pair{Match::kUnsetGroup, Match::kUnsetGroup})};
    if (auto t = regex_->expand_replacement(probe, replacement, [](std::uint64_t, std::uint64_t) { return std::string(); }); !t)
        return std::unexpected(t.error());
    cancel();
    doc_.begin_group(EditKind::replace_all);
    Job job;
    job.kind = Job::replace_all;
    job.seg_end = doc_.text().size();
    job.version = doc_.version();
    job.replacement = std::string(replacement);
    job_ = std::move(job);
    return {};
}

Status Searcher::replace_current(std::string_view replacement) {
    if (!regex_) return std::unexpected(make_error(ErrorCode::regex, "no search pattern"));
    const auto lm = last_match();
    const auto sel = editor_.selection();
    const bool on_match = lm && ((sel && sel->first == lm->start && sel->second == lm->end) ||
                                 (lm->start == lm->end && !sel && editor_.cursor() == lm->start));
    if (!on_match) return std::unexpected(make_error(ErrorCode::canceled, "the selection is not the current match"));
    const PieceTree& t = doc_.text();
    auto repl = regex_->expand_replacement(*lm, replacement, [&](std::uint64_t s, std::uint64_t e) { return t.read(s, e - s); });
    if (!repl) return std::unexpected(repl.error());
    const std::uint64_t end = lm->start + repl->size();
    doc_.apply(lm->start, lm->end - lm->start, std::string_view(*repl), EditKind::replace, editor_.cursor(), end);
    editor_.select_range(end, end);
    last_match_.reset();
    find_next(end, Direction::forward);
    return {};
}

StepResult Searcher::finish(StepResult r) {
    if (job_) {
        r.line_too_long = job_->too_long;
        if (job_->second_lap && !job_->reported_wrap) r.wrapped = true;
        if (job_->kind == Job::replace_all) doc_.end_group();
    }
    job_.reset();
    if (r.kind == StepResult::found) {
        last_match_ = r.match;
        last_version_ = doc_.version();
        editor_.select_range(r.match.start, r.match.end);
    }
    return r;
}

// ---- windows ---------------------------------------------------------------------------------

std::uint64_t Searcher::read_window(std::uint64_t start, std::uint64_t length) {
    window_.clear();
    window_.reserve(static_cast<std::size_t>(length));
    doc_.text().read(start, length, [&](std::span<const std::byte> s) {
        window_.append(reinterpret_cast<const char*>(s.data()), s.size());
        return true;
    });
    return window_.size();
}

std::uint64_t Searcher::cp_length_at(std::uint64_t window_start, std::uint64_t offset) const {
    const std::size_t i = static_cast<std::size_t>(offset - window_start);
    if (i >= window_.size()) return 1;
    const Decoded d = decode(std::as_bytes(std::span(window_.data() + i, window_.size() - i)));
    return std::max<std::uint64_t>(1, d.len);
}

Result<Searcher::Scan> Searcher::scan_forward(std::uint64_t pos, bool mid_line, std::uint64_t seg_end, bool inclusive_end, bool all,
                                              std::optional<std::uint64_t> skip_empty_at, bool& too_long) {
    if (!regex_) return std::unexpected(make_error(ErrorCode::regex, "no search pattern"));
    const PieceTree& t = doc_.text();
    const std::uint64_t size = t.size();
    // Windows start at a line start, except inside a line longer than the cap, which is
    // searched in consecutive pieces (and flagged).
    const std::uint64_t back = std::min(pos, kWindowCap);
    const std::uint64_t lf_before = pos == 0 || mid_line ? PieceTree::npos : t.find_lf_backward(pos, back);
    const std::uint64_t ws = lf_before != PieceTree::npos ? lf_before + 1 : (back == pos && !mid_line ? 0 : pos);
    if (lf_before == PieceTree::npos && ws > 0) too_long = true;
    std::uint64_t w = kWindow;
    std::uint64_t n = 0;
    std::uint64_t complete = 0;  // bytes of complete lines in the window
    bool at_eof = false;
    bool truncated = false;
    for (;;) {
        n = read_window(ws, std::min(w, size - ws));
        at_eof = ws + n == size;
        const std::size_t last_lf = window_.rfind('\n');
        complete = at_eof ? n : (last_lf == std::string::npos ? 0 : last_lf + 1);
        if (complete > 0) break;
        if (w < kWindowCap) {
            w = std::min(kWindowCap, w * 2);
            continue;
        }
        // One line longer than the cap: search what fits, as if the line ended there.
        too_long = truncated = true;
        complete = n;
        break;
    }
    const auto bytes = std::as_bytes(std::span(window_.data(), window_.size()));
    Scan s;
    s.window_start = ws;
    std::uint64_t f = std::max(pos, ws);
    LineCursor cursor;
    for (;;) {
        auto r = regex_->search_window(bytes, ws, f, at_eof || truncated, &cursor);
        if (!r) return std::unexpected(r.error());
        if (r->kind != WindowResult::found) break;  // `need_more` only concerns the incomplete last line
        const Match& m = r->match;
        if (!at_eof && !truncated && m.start >= ws + complete) break;  // the next window starts there
        if (m.start > seg_end || (m.start == seg_end && !inclusive_end)) {
            s.segment_done = true;
            break;
        }
        if (skip_empty_at && m.start == m.end && m.start == *skip_empty_at) {
            f = m.start + cp_length_at(ws, m.start);
            if (f > ws + n) break;
            continue;
        }
        if (!all) {
            s.first = m;
            break;
        }
        s.all.push_back(m);
        f = m.end > m.start ? m.end : m.start + cp_length_at(ws, m.start);
        if (f > ws + n) break;
    }
    s.next = ws + complete;  // with `truncated`, the rest of the long line follows directly
    s.truncated = truncated && !at_eof;
    if (at_eof || s.next >= size || (!inclusive_end && s.next >= seg_end)) s.segment_done = true;
    return s;
}

Result<std::optional<Match>> Searcher::scan_backward(std::uint64_t limit, std::uint64_t floor, std::uint64_t& next_limit,
                                                     bool& too_long) {
    if (!regex_) return std::unexpected(make_error(ErrorCode::regex, "no search pattern"));
    const PieceTree& t = doc_.text();
    const std::uint64_t size = t.size();
    // The window ends at a line boundary at or after `limit`, so a match starting before
    // `limit` is seen whole.
    std::uint64_t end = limit;
    if (limit > 0 && limit < size && t.byte_at(limit - 1) != std::byte{'\n'}) {
        const std::uint64_t ahead = std::min(size - limit, kWindowCap);
        const std::uint64_t lf = t.find_lf_forward(limit, ahead);
        end = lf != PieceTree::npos ? lf + 1 : limit + ahead;
        if (lf == PieceTree::npos && end < size) too_long = true;
    }
    const std::uint64_t probe = end > kWindow ? end - kWindow : 0;
    const std::uint64_t back = std::min(probe, kWindowCap - std::min(kWindowCap, end - probe));
    const std::uint64_t lf = probe == 0 ? PieceTree::npos : t.find_lf_backward(probe, back);
    std::uint64_t ws = lf != PieceTree::npos ? lf + 1 : probe - back;
    if (ws > 0 && lf == PieceTree::npos) too_long = true;  // mid-line: a line longer than the cap
    const std::uint64_t n = read_window(ws, end - ws);
    const auto bytes = std::as_bytes(std::span(window_.data(), window_.size()));
    std::optional<Match> last;
    std::uint64_t f = ws;
    LineCursor cursor;
    for (;;) {
        auto r = regex_->search_window(bytes, ws, f, true, &cursor);
        if (!r) return std::unexpected(r.error());
        if (r->kind != WindowResult::found) break;
        const Match& m = r->match;
        if (m.start >= limit) break;
        if (m.start >= floor) last = m;
        f = m.end > m.start ? m.end : m.start + cp_length_at(ws, m.start);
        if (f > ws + n) break;
    }
    next_limit = ws;
    return last;
}

// ---- stepping -------------------------------------------------------------------------------

StepResult Searcher::step(std::uint64_t budget_bytes) {
    if (!job_) return {};
    if (doc_.version() != job_->version) {
        job_->second_lap = false;  // nothing to report
        return finish(StepResult::of(StepResult::canceled));
    }
    Job& job = *job_;
    // An engine error ends the job; replacements already made stay, as one node.
    auto fail = [&](const Error& e) {
        StepResult r = StepResult::of(job.kind == Job::replace_all ? StepResult::replaced : StepResult::not_found);
        r.count = job.count;
        r.message = e.message;
        return finish(r);
    };
    std::uint64_t used = 0;
    StepResult progress = StepResult::of(StepResult::in_progress);
    while (used < budget_bytes) {
        if (job.direction == Direction::backward) {
            std::uint64_t next_limit = 0;
            auto found = scan_backward(job.pos, job.seg_end, next_limit, job.too_long);
            if (!found) return fail(found.error());
            if (const std::optional<Match>& match = *found) {
                StepResult r = StepResult::of(StepResult::found);
                r.match = *match;
                return finish(r);
            }
            used += std::max<std::uint64_t>(1, job.pos - next_limit);
            job.pos = next_limit;
            if (job.pos <= job.seg_end) {
                const std::uint64_t size = doc_.text().size();
                if (job.second_lap || !options_.wrap || job.origin >= size) return finish(StepResult::of(StepResult::not_found));
                job.second_lap = true;
                job.pos = size;
                job.seg_end = job.origin;
            }
            continue;
        }

        const bool all = job.kind == Job::replace_all;
        if (all) job.seg_end = doc_.text().size();  // replacements move the end
        auto scan = scan_forward(job.pos, job.mid_line, job.seg_end, !job.second_lap, all, job.skip_empty_at, job.too_long);
        if (!scan) return fail(scan.error());
        if (const std::optional<Match>& first = scan->first) {
            StepResult r = StepResult::of(StepResult::found);
            r.match = *first;
            return finish(r);
        }
        std::int64_t delta = 0;
        for (const Match& m : scan->all) {
            const std::uint64_t ws = scan->window_start;
            auto repl = regex_->expand_replacement(m, job.replacement, [&](std::uint64_t s, std::uint64_t e) {
                return window_.substr(static_cast<std::size_t>(s - ws), static_cast<std::size_t>(e - s));
            });
            if (!repl) return fail(repl.error());
            const std::uint64_t at = static_cast<std::uint64_t>(static_cast<std::int64_t>(m.start) + delta);
            doc_.apply(at, m.end - m.start, std::string_view(*repl), EditKind::replace_all, at, at + repl->size());
            delta += static_cast<std::int64_t>(repl->size()) - static_cast<std::int64_t>(m.end - m.start);
            ++job.count;
        }
        job.version = doc_.version();
        used += std::max<std::uint64_t>(1, scan->next > job.pos ? scan->next - job.pos : 0);
        job.pos = static_cast<std::uint64_t>(static_cast<std::int64_t>(scan->next) + delta);
        job.mid_line = scan->truncated;
        job.skip_empty_at.reset();
        if (scan->segment_done) {
            if (all) {
                StepResult r = StepResult::of(StepResult::replaced);
                r.count = job.count;
                return finish(r);
            }
            if (job.second_lap || !options_.wrap || job.origin == 0) return finish(StepResult::of(StepResult::not_found));
            job.second_lap = true;
            job.pos = 0;
            job.mid_line = false;
            job.seg_end = job.origin;
        }
    }
    if (job.second_lap && !job.reported_wrap) {
        progress.wrapped = true;
        job.reported_wrap = true;
    }
    progress.line_too_long = job.too_long;
    return progress;
}

}  // namespace mod
