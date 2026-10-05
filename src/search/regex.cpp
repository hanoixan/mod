#include "search/regex.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <map>

namespace mod {
namespace {

// The backtracking budget: generous for real patterns, bounded for adversarial ones.
constexpr std::uint32_t kMatchLimit = 10'000'000;
constexpr std::uint32_t kDepthLimit = 1'000'000;
constexpr std::uint32_t kHeapLimitKiB = 64 * 1024;
constexpr std::size_t kJitStackMin = 32 * 1024;
constexpr std::size_t kJitStackMax = 1024 * 1024;

std::string error_text(int code) {
    PCRE2_UCHAR buf[256];
    const int n = pcre2_get_error_message(code, buf, sizeof buf);
    if (n < 0) return std::format("regex error {}", code);
    return std::string(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
}

// Escapes every ASCII byte that is not a letter or digit; other bytes are literal.
std::string escape_literal(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        const bool alnum = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
        if (u < 0x80 && !alnum) out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

struct Regex::Impl {
    pcre2_code* code = nullptr;
    pcre2_match_data* match_data = nullptr;
    pcre2_match_context* match_context = nullptr;
    pcre2_jit_stack* jit_stack = nullptr;
    std::uint32_t capture_count = 0;
    std::map<std::string, std::uint32_t, std::less<>> names;
    bool literal = false;

    ~Impl() {
        if (match_data != nullptr) pcre2_match_data_free(match_data);
        if (match_context != nullptr) pcre2_match_context_free(match_context);
        if (jit_stack != nullptr) pcre2_jit_stack_free(jit_stack);
        if (code != nullptr) pcre2_code_free(code);
    }
};

Regex::Regex(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Regex::Regex(Regex&&) noexcept = default;
Regex& Regex::operator=(Regex&&) noexcept = default;
Regex::~Regex() = default;

std::uint32_t Regex::group_count() const noexcept { return impl_->capture_count; }
bool Regex::literal() const noexcept { return impl_->literal; }

Result<Regex> Regex::compile(std::string_view pattern, const RegexOptions& options) {
    auto impl = std::make_unique<Impl>();
    impl->literal = options.literal;
    std::uint32_t flags = PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF;
    if (options.case_insensitive) flags |= PCRE2_CASELESS;
    std::string source;
    if (options.whole_word) {
        // Not next to a word character on either side; unlike \b this also lets a term that
        // starts or ends with punctuation ("foo(", "-x") match. PCRE2_LITERAL would make the
        // wrapper literal too, so escape by hand.
        source = "(?<!\\w)(?:" + (options.literal ? escape_literal(pattern) : std::string(pattern)) + ")(?!\\w)";
    } else {
        source = std::string(pattern);
        // PCRE2_LITERAL refuses PCRE2_UCP; it changes nothing for a literal anyway.
        if (options.literal) flags = (flags & ~static_cast<std::uint32_t>(PCRE2_UCP)) | PCRE2_LITERAL;
    }
    int err = 0;
    PCRE2_SIZE err_offset = 0;
    impl->code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(source.data()), source.size(), flags, &err, &err_offset,
                               nullptr);
    if (impl->code == nullptr) {
        return std::unexpected(make_error(ErrorCode::regex, std::format("{} at offset {}", error_text(err), err_offset)));
    }
    // Under a W^X policy JIT fails; the interpreter is then used silently.
    const bool jit = pcre2_jit_compile(impl->code, PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_HARD) == 0;

    pcre2_pattern_info(impl->code, PCRE2_INFO_CAPTURECOUNT, &impl->capture_count);
    std::uint32_t name_count = 0;
    std::uint32_t entry_size = 0;
    PCRE2_SPTR table = nullptr;
    pcre2_pattern_info(impl->code, PCRE2_INFO_NAMECOUNT, &name_count);
    pcre2_pattern_info(impl->code, PCRE2_INFO_NAMEENTRYSIZE, &entry_size);
    pcre2_pattern_info(impl->code, PCRE2_INFO_NAMETABLE, static_cast<void*>(&table));
    for (std::uint32_t i = 0; i < name_count && table != nullptr; ++i) {
        const PCRE2_SPTR e = table + static_cast<std::size_t>(i) * entry_size;
        const std::uint32_t number = static_cast<std::uint32_t>(e[0]) << 8 | e[1];
        impl->names.emplace(reinterpret_cast<const char*>(e + 2), number);
    }

    impl->match_data = pcre2_match_data_create_from_pattern(impl->code, nullptr);
    impl->match_context = pcre2_match_context_create(nullptr);
    if (impl->match_data == nullptr || impl->match_context == nullptr)
        return std::unexpected(make_error(ErrorCode::internal, "out of memory compiling the pattern"));
    pcre2_set_match_limit(impl->match_context, kMatchLimit);
    pcre2_set_depth_limit(impl->match_context, kDepthLimit);
    pcre2_set_heap_limit(impl->match_context, kHeapLimitKiB);
    if (jit) {
        impl->jit_stack = pcre2_jit_stack_create(kJitStackMin, kJitStackMax, nullptr);
        if (impl->jit_stack != nullptr) pcre2_jit_stack_assign(impl->match_context, nullptr, impl->jit_stack);
    }
    return Regex(std::move(impl));
}

Result<WindowResult> Regex::search_window(std::span<const std::byte> window, std::uint64_t window_offset,
                                          std::uint64_t from, bool at_eof, LineCursor* cursor) const {
    const auto* base = reinterpret_cast<const char*>(window.data());
    const std::size_t size = window.size();
    const std::size_t fr = static_cast<std::size_t>(std::clamp<std::uint64_t>(from, window_offset, window_offset + size) -
                                                    window_offset);
    // The line containing `from`: looked for back only as far as the cursor already knows,
    // and a cursor still on that line already knows where it ends.
    const bool known = cursor != nullptr && cursor->valid && cursor->searched_to <= fr;
    const std::size_t back_to = known ? cursor->searched_to : 0;
    const std::size_t prev_lf = std::string_view(base + back_to, fr - back_to).rfind('\n');
    std::size_t line = prev_lf != std::string_view::npos ? back_to + prev_lf + 1 : known ? cursor->line : 0;
    const bool same_line = known && line == cursor->line;
    bool first = true;
    while (line <= size) {
        const void* lf = first && same_line ? (cursor->line_feed < size ? static_cast<const void*>(base + cursor->line_feed) : nullptr)
                         : line < size     ? std::memchr(base + line, '\n', size - line)
                                           : nullptr;
        if (cursor != nullptr && first) {
            *cursor = LineCursor{true, fr, line, lf != nullptr ? static_cast<std::size_t>(static_cast<const char*>(lf) - base) : size};
        }
        const bool complete = lf != nullptr || at_eof;
        std::size_t end = lf != nullptr ? static_cast<std::size_t>(static_cast<const char*>(lf) - base) : size;
        if (lf != nullptr && end > line && base[end - 1] == '\r') --end;  // CR LF is one line break
        if (line == size && lf == nullptr && !at_eof) return WindowResult{WindowResult::none, {}};
        const std::size_t len = end - line;
        if (first && fr - line > len) {  // `from` is inside the line break: start on the next line
            if (lf == nullptr) break;
            line = static_cast<std::size_t>(static_cast<const char*>(lf) - base) + 1;
            first = false;
            continue;
        }
        const std::size_t start = first ? fr - line : 0;
        const std::uint32_t opts = complete ? 0 : PCRE2_PARTIAL_HARD;
        const int rc = pcre2_match(impl_->code, reinterpret_cast<PCRE2_SPTR>(base + line), len, start, opts,
                                   impl_->match_data, impl_->match_context);
        if (rc >= 0) {
            const PCRE2_SIZE* ov = pcre2_get_ovector_pointer(impl_->match_data);
            const std::uint64_t at = window_offset + line;
            WindowResult r{WindowResult::found, {at + ov[0], at + ov[1], {}}};
            const std::uint32_t pairs = pcre2_get_ovector_count(impl_->match_data);
            for (std::size_t g = 1; g <= impl_->capture_count; ++g) {
                if (g >= pairs || ov[2 * g] == PCRE2_UNSET) {
                    r.match.groups.emplace_back(Match::kUnsetGroup, Match::kUnsetGroup);
                } else {
                    r.match.groups.emplace_back(at + ov[2 * g], at + ov[2 * g + 1]);
                }
            }
            return r;
        }
        switch (rc) {
            case PCRE2_ERROR_NOMATCH: break;
            case PCRE2_ERROR_PARTIAL: return WindowResult{WindowResult::need_more, {}};
            case PCRE2_ERROR_MATCHLIMIT:
            case PCRE2_ERROR_DEPTHLIMIT:
            case PCRE2_ERROR_HEAPLIMIT:
            case PCRE2_ERROR_JIT_STACKLIMIT: return std::unexpected(make_error(ErrorCode::regex, "pattern too complex"));
            default: return std::unexpected(make_error(ErrorCode::regex, error_text(rc)));
        }
        if (lf == nullptr) break;  // the window's last line
        line = static_cast<std::size_t>(static_cast<const char*>(lf) - base) + 1;
        first = false;
    }
    return WindowResult{WindowResult::none, {}};
}

Result<std::string> Regex::expand_replacement(const Match& match, std::string_view template_,
                                              const ReadBytes& read) const {
    if (impl_->literal) return std::string(template_);
    std::string out;
    auto group = [&](std::uint32_t n) {
        if (n == 0) {
            if (match.end > match.start) out += read(match.start, match.end);
            return;
        }
        if (n > match.groups.size()) return;  // did not take part
        const auto [s, e] = match.groups[n - 1];
        if (s != Match::kUnsetGroup && e > s) out += read(s, e);
    };
    auto number = [&](std::uint32_t n) -> Status {
        if (n > impl_->capture_count) return std::unexpected(make_error(ErrorCode::regex, std::format("no group {}", n)));
        group(n);
        return {};
    };
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    for (std::size_t i = 0; i < template_.size();) {
        const char c = template_[i];
        if (c != '$') {
            out += c;
            ++i;
            continue;
        }
        if (i + 1 >= template_.size())
            return std::unexpected(make_error(ErrorCode::regex, "'$' at the end of the replacement"));
        const char d = template_[i + 1];
        if (d == '$') {
            out += '$';
            i += 2;
        } else if (is_digit(d)) {
            std::uint32_t n = static_cast<std::uint32_t>(d - '0');
            i += 2;
            if (i < template_.size() && is_digit(template_[i])) {  // at most two digits
                n = n * 10 + static_cast<std::uint32_t>(template_[i] - '0');
                ++i;
            }
            if (auto s = number(n); !s) return std::unexpected(s.error());
        } else if (d == '{') {
            const std::size_t close = template_.find('}', i + 2);
            if (close == std::string_view::npos)
                return std::unexpected(make_error(ErrorCode::regex, "unterminated '${' in the replacement"));
            const std::string_view ref = template_.substr(i + 2, close - i - 2);
            if (ref.empty()) return std::unexpected(make_error(ErrorCode::regex, "empty '${}' in the replacement"));
            if (std::all_of(ref.begin(), ref.end(), is_digit)) {
                if (ref.size() > 2) return std::unexpected(make_error(ErrorCode::regex, std::format("no group {}", ref)));
                std::uint32_t n = 0;
                for (const char r : ref) n = n * 10 + static_cast<std::uint32_t>(r - '0');
                if (auto s = number(n); !s) return std::unexpected(s.error());
            } else {
                const auto it = impl_->names.find(ref);
                if (it == impl_->names.end())
                    return std::unexpected(make_error(ErrorCode::regex, std::format("no group named '{}'", ref)));
                group(it->second);
            }
            i = close + 1;
        } else {
            return std::unexpected(make_error(ErrorCode::regex, std::format("invalid reference '${}' in the replacement", d)));
        }
    }
    return out;
}

}  // namespace mod
