// A search pattern and options from the user, matched over any text: compiling, matching
// every match of every line and expanding a replacement never crash, and PCRE2's limits
// keep each search bounded.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz/fuzz_reader.hpp"
#include "search/regex.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    mod::fuzz::Reader in(data, size);
    const std::uint8_t flags = in.byte();
    mod::RegexOptions options;
    options.case_insensitive = (flags & 1) != 0;
    options.literal = (flags & 2) != 0;
    options.whole_word = (flags & 4) != 0;
    const std::string pattern(in.text(128));
    const std::string replacement(in.text(64));
    const std::string subject(in.rest());
    auto re = mod::Regex::compile(pattern, options);
    if (!re) return 0;
    const auto window = std::as_bytes(std::span(subject.data(), subject.size()));
    mod::LineCursor cursor;
    std::uint64_t from = 0;
    for (std::size_t guard = 0; guard <= subject.size() + 1; ++guard) {
        auto r = re->search_window(window, 0, from, true, &cursor);
        if (!r || r->kind != mod::WindowResult::found) break;
        const mod::Match& m = r->match;
        if (m.start < from || m.end < m.start || m.end > subject.size()) mod::fuzz::fail();
        (void)re->expand_replacement(m, replacement, [&](std::uint64_t s, std::uint64_t e) { return subject.substr(s, e - s); });
        from = m.end > m.start ? m.end : m.start + 1;
        if (from > subject.size()) break;
    }
    return 0;
}
