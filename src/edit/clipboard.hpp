#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "text/piece_tree.hpp"

namespace mod {

// Copies of at most this many bytes are also sent to the terminal with OSC 52.
inline constexpr std::uint64_t kOsc52MaxBytes = 100'000;

// The run, one keep-alive view per piece, the id of the document it belongs to, and
// its length in bytes.
struct ClipContent {
    PieceRun run;
    std::vector<FrozenBytes> views;
    std::uint64_t source = 0;
    std::uint64_t length = 0;
};

// The copy and paste store; one per process, so it survives File > Open. Main thread.
class Clipboard {
public:
    using TerminalWrite = std::function<void(std::string_view)>;

    explicit Clipboard(TerminalWrite terminal_write = {}) : terminal_write_(std::move(terminal_write)) {}

    // `materialize` is called only when the run is at most kOsc52MaxBytes long.
    void set(PieceRun run, std::vector<FrozenBytes> buffers, std::uint64_t source,
             const std::function<std::string()>& materialize);
    // Joins a run onto the current content when it came from the same document (`source`);
    // otherwise it replaces it, as `set` does. OSC 52 carries the whole result.
    void append(PieceRun run, std::vector<FrozenBytes> views, std::uint64_t source);
    // Bytes that belong to no document (source 0); a paste copies them.
    void set_text(std::string text);
    // nullptr when empty.
    const ClipContent* get() const noexcept { return content_ ? &*content_ : nullptr; }
    // An equivalent replacement for the current content; nothing reaches the terminal.
    void rebind(PieceRun run, std::vector<FrozenBytes> views, std::uint64_t source);

private:
    TerminalWrite terminal_write_;
    std::optional<ClipContent> content_;
};

}  // namespace mod
