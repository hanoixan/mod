// A .mod history file of any content next to a document (one could come with a cloned
// repository): opening it never crashes, never changes the document, and whatever
// history loads can be walked.
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "edit/document.hpp"
#include "edit/sidecar.hpp"
#include "fuzz/fuzz_reader.hpp"
#include "util/event_queue.hpp"

namespace {

const std::string kText = "alpha\nbeta\n";

std::filesystem::path document_path() {
    static const std::filesystem::path p = [] {
        const auto dir = std::filesystem::temp_directory_path() / ("mod-fuzz-sidecar-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "doc.txt", std::ios::binary) << kText;
        return dir / "doc.txt";
    }();
    return p;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::filesystem::path doc_path = document_path();
    {
        std::ofstream out(mod::sidecar_path_for(doc_path), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    mod::EventQueue queue({});
    mod::DocumentOptions options;
    options.persist_history = mod::PersistHistory::always;
    auto doc = mod::Document::open(doc_path, queue, options);
    if (!doc) return 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((*doc)->history_state() == mod::HistoryState::verifying && std::chrono::steady_clock::now() < deadline) {
        if (queue.drain() == 0) std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    if ((*doc)->text().read(0, (*doc)->text().size()) != kText) mod::fuzz::fail();
    const mod::UndoTree& h = (*doc)->history();
    for (const mod::NodeId root : h.roots()) {
        (void)(*doc)->jump_to(root);
        for (mod::NodeId child : h.node_info(root).children) (void)(*doc)->jump_to(child);
    }
    // Writing must not fail through what was loaded: the session goes on.
    (*doc)->apply(0, 0, std::string_view("x"), mod::EditKind::typing, 0, 1);
    return 0;
}
