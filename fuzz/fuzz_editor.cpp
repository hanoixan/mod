// Random editing sessions: any sequence of the editor's commands, with history moves, must
// keep the editor's invariants (cursor and selection inside the text, on a character
// boundary) and undo everything back to the start.
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

#include "edit/clipboard.hpp"
#include "edit/document.hpp"
#include "edit/editor.hpp"
#include "fuzz/fuzz_reader.hpp"
#include "text/utf8.hpp"
#include "util/event_queue.hpp"

namespace {

// Whether `at` falls inside a valid multi-byte UTF-8 sequence. (An invalid byte is a
// character of its own to the editor, so a position next to one is a boundary.)
bool inside_character(const mod::PieceTree& t, std::uint64_t at) {
    for (std::uint64_t back = 1; back <= 3 && back <= at; ++back) {
        const std::uint64_t from = at - back;
        const std::string bytes = t.read(from, std::min<std::uint64_t>(4, t.size() - from));
        const mod::Decoded d = mod::decode(std::as_bytes(std::span(bytes.data(), bytes.size())));
        if (d.valid && d.len > back) return true;
    }
    return false;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    mod::fuzz::Reader in(data, size);
    mod::EventQueue queue({});
    auto doc = mod::Document::open_untitled(queue);
    mod::Clipboard clipboard;
    mod::Editor ed(*doc, clipboard, 1 + static_cast<int>(in.below(8)));
    int steps = 0;
    while (!in.empty() && steps++ < 400) {
        switch (in.below(16)) {
            case 0:
            case 1: ed.insert_text(in.text(12)); break;
            case 2: ed.newline(); break;
            case 3: ed.delete_backward(in.below(2) == 0); break;
            case 4: ed.delete_forward(in.below(2) == 0); break;
            case 5: ed.move(static_cast<mod::Motion>(in.below(12)), in.below(2) == 0, 1 + in.below(5)); break;
            case 6: {  // the editor is only ever given character boundaries
                const std::uint64_t n = doc->text().size() + 1;
                auto boundary = [&](std::uint64_t p) {
                    while (inside_character(doc->text(), p)) --p;
                    return p;
                };
                ed.select_range(boundary(in.byte() % n), boundary(in.byte() % n));
                break;
            }
            case 7: (void)ed.undo(); break;
            case 8: (void)ed.redo(); break;
            case 9: ed.cut(); break;
            case 10: (void)ed.copy(); break;
            case 11: ed.paste(); break;
            case 12: ed.indent(in.below(2) == 0); break;
            case 13: ed.outdent(); break;
            case 14: ed.cut_to_line_end(in.below(2) == 0); break;
            default: (void)doc->cycle_branch(in.below(2) == 0 ? 1 : -1); break;
        }
        const mod::PieceTree& t = doc->text();
        if (ed.cursor() > t.size()) mod::fuzz::fail();
        if (const auto a = ed.anchor(); a && *a > t.size()) mod::fuzz::fail();
        if (inside_character(t, ed.cursor())) mod::fuzz::fail();
    }
    while (ed.undo()) {
    }
    if (doc->text().size() != 0) mod::fuzz::fail();  // every edit undone: the empty start again
    return 0;
}
