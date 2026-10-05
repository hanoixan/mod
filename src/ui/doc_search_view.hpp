#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "app/doc_search.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

struct DocSearchKeyResult {
    bool closed = false;            // the panel closed (Esc, or Enter on a match)
    std::optional<DocMatch> open;   // Enter: show this match
};

// The help search panel: a query field over every page of the manual and the list of
// matches. Its query, results and selection outlive closing it, for the session.
class DocSearchView {
public:
    using SearchFn = std::function<DocSearchResult(std::string_view)>;

    void open(SearchFn search);
    void close() { open_ = false; }
    bool is_open() const noexcept { return open_; }

    DocSearchKeyResult handle_key(const KeyEvent& key);
    void handle_paste(std::string_view bytes);
    void render(Screen& screen, Rect area);

    const std::string& query() const noexcept { return query_; }
    const DocSearchResult& results() const noexcept { return results_; }
    std::size_t selected() const noexcept { return selected_; }
    // "page:line  text", as a row shows it.
    std::string row_text(std::size_t index) const;

private:
    void search();

    bool open_ = false;
    SearchFn search_;
    std::string query_;
    DocSearchResult results_;
    std::size_t selected_ = 0;
    std::size_t scroll_ = 0;
};

}  // namespace mod
