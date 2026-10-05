#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mod {

struct Attr;

enum class TerminalMode { vt100, xterm };

// The text cursor's shape, in the order of the `cursor_style` setting's names.
enum class CursorStyle { bar, bar_blink, block, block_blink, underline, underline_blink };

// Every escape sequence mod writes, for one terminal behavior. The implementations are
// immutable singletons: their strings live for the whole process (safe in signal handlers).
class TerminalOutput {
public:
    virtual ~TerminalOutput() = default;
    virtual TerminalMode mode() const noexcept = 0;
    // Taking over the screen, and giving it back.
    virtual std::string_view enter() const noexcept = 0;
    virtual std::string_view leave() const noexcept = 0;
    // A frame: its prefix, the suffix before the cursor is placed, and its last bytes.
    virtual std::string_view begin_frame() const noexcept = 0;
    virtual std::string_view end_frame() const noexcept = 0;
    virtual std::string_view end_sync() const noexcept = 0;
    virtual std::string_view show_cursor() const noexcept = 0;
    // Sets the cursor's shape (DECSCUSR); empty where the terminal has no such code.
    virtual std::string_view cursor_shape(CursorStyle style) const noexcept = 0;
    // Appends the SGR that sets exactly this look from a reset.
    virtual void append_attr(std::string& out, const Attr& attr) const = 0;
    // Appends one cell's character. A VT100 has no UTF-8: box-drawing characters (and ≥ and
    // •) go through its line-drawing set, switched with ESC ( 0 and ESC ( B as needed and
    // tracked in `line_drawing`, and the UI's other symbols get ASCII stand-ins of the same
    // width; anything else is sent as it is. Other terminals take every character as it is.
    virtual void append_cell(std::string& out, std::string_view utf8, bool& line_drawing) const;
    // Sets the terminal's (and its tab's) title to `title`, in which every control and
    // byte that is not UTF-8 becomes '?'. Nothing on a VT100.
    virtual void append_title(std::string& out, std::string_view title) const = 0;

    // Shared by every behavior (VT100 has them): erase the screen, and move the cursor
    // to a 0-based row and column.
    std::string_view clear_screen() const noexcept { return "\x1b[0m\x1b[2J"; }
    void append_move(std::string& out, int row, int col) const;
};

const TerminalOutput& output_for(TerminalMode mode);

// What the environment says about the terminal, or nullopt when it does not say.
// `env` returns a variable's value, or null when it is unset.
std::optional<TerminalMode> mode_from_environment(const std::function<const char*(const char*)>& env);

// The Primary Device Attributes query, sent when the environment does not say.
inline constexpr std::string_view kDeviceAttributesQuery = "\x1b[c";

// The byte range of a Primary Device Attributes reply (ESC [ ? … c) within `bytes`.
std::optional<std::pair<std::size_t, std::size_t>> find_device_attributes(std::string_view bytes);
// The mode a complete reply in `bytes` implies, or nullopt when there is none.
std::optional<TerminalMode> mode_from_device_attributes(std::string_view bytes);

}  // namespace mod
