#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace mod {

// One report from a long synchronous operation; `total` is an estimate when present.
struct Progress {
    std::string_view label;
    std::uint64_t done = 0;
    std::optional<std::uint64_t> total;
};

// Called on the main thread from inside the operation; may draw, must not throw.
using ProgressSink = std::function<void(const Progress&)>;

namespace detail {
inline std::string format_bytes(std::uint64_t n) {
    if (n < 1024) return std::format("{} B", n);
    static constexpr std::string_view kUnits[] = {"KiB", "MiB", "GiB", "TiB"};
    double v = static_cast<double>(n) / 1024.0;
    std::size_t unit = 0;
    while (v >= 1024.0 && unit + 1 < std::size(kUnits)) {
        v /= 1024.0;
        ++unit;
    }
    return std::format("{:.1f} {}", v, kUnits[unit]);
}
}  // namespace detail

// "<label>: <done> of <total> (<percent>%)", or "<label>: <done>" without a total.
inline std::string format_progress(const Progress& p) {
    if (!p.total) return std::format("{}: {}", p.label, detail::format_bytes(p.done));
    const std::uint64_t total = *p.total;
    std::uint64_t percent = 100;
    if (p.done < total) {
        percent = total <= UINT64_MAX / 100 ? p.done * 100 / total : p.done / (total / 100);
        if (percent > 99) percent = 99;  // not finished yet
    }
    return std::format("{}: {} of {} ({}%)", p.label, detail::format_bytes(p.done), detail::format_bytes(*p.total),
                       percent);
}

}  // namespace mod
