// Any file opened as Markdown: the scanner, the read-only layout and its cursor motions
// never crash, and every position the layout reports lies inside the document.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "edit/editor.hpp"
#include "fuzz/fuzz_reader.hpp"
#include "syntax/markdown.hpp"
#include "syntax/markdown_render.hpp"
#include "text/piece_tree.hpp"
#include "ui/reading_layout.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    mod::fuzz::Reader in(data, size);
    const int width = 20 + static_cast<int>(in.below(120));
    const std::string text(in.rest());
    mod::PieceTree tree;
    tree.insert(0, std::as_bytes(std::span(text.data(), text.size())));
    const mod::MarkdownOutline outline = mod::scan_markdown(tree);
    for (const mod::MarkdownLink& l : outline.links)
        if (l.end > text.size() || l.start > l.end) mod::fuzz::fail();
    const mod::ReadingLayout layout(text, width);
    for (const mod::RenderedLine& line : layout.page().lines) {
        if (line.source.size() != line.text.size()) mod::fuzz::fail();
        for (std::uint64_t s : line.source)
            if (s != mod::kGenerated && s >= text.size()) mod::fuzz::fail();
    }
    std::optional<int> sticky;
    std::uint64_t cursor = 0;
    for (int m = 0; m < 12; ++m) {
        cursor = layout.move(static_cast<mod::Motion>(m), cursor, 10, sticky);
        if (cursor > text.size()) mod::fuzz::fail();
        (void)layout.locate(cursor);
    }
    (void)layout.visible_text(0, text.size());
    return 0;
}
