#include "edit/clipboard.hpp"

#include <iterator>
#include <string>
#include <utility>

namespace mod {
namespace {

std::uint64_t run_length(const PieceRun& run) {
    std::uint64_t n = 0;
    for (const Piece& p : run) n += p.length;
    return n;
}

// Standard base64 with padding, for OSC 52.
std::string base64_encode(std::string_view bytes) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t v = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << 16 |
                                static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8 |
                                static_cast<unsigned char>(bytes[i + 2]);
        out += kAlphabet[v >> 18 & 63];
        out += kAlphabet[v >> 12 & 63];
        out += kAlphabet[v >> 6 & 63];
        out += kAlphabet[v & 63];
    }
    if (const std::size_t rest = bytes.size() - i; rest > 0) {
        std::uint32_t v = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << 16;
        if (rest == 2) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8;
        out += kAlphabet[v >> 18 & 63];
        out += kAlphabet[v >> 12 & 63];
        out += rest == 2 ? kAlphabet[v >> 6 & 63] : '=';
        out += '=';
    }
    return out;
}

}  // namespace

void Clipboard::set(PieceRun run, std::vector<FrozenBytes> buffers, std::uint64_t source,
                    const std::function<std::string()>& materialize) {
    const std::uint64_t length = run_length(run);
    content_ = ClipContent{std::move(run), std::move(buffers), source, length};
    if (length > kOsc52MaxBytes || !terminal_write_ || !materialize) return;
    // ESC ] 52 ; c ; <base64> ESC \ : write-only; whether it arrives cannot be known.
    std::string seq = "\x1b]52;c;";
    seq += base64_encode(materialize());
    seq += "\x1b\\";
    terminal_write_(seq);
}

void Clipboard::set_text(std::string text) {
    auto held = std::make_shared<const std::string>(std::move(text));
    const auto length = static_cast<std::uint64_t>(held->size());
    FrozenBytes view{std::as_bytes(std::span(held->data(), held->size())), held};
    set(PieceRun{Piece{0, 0, length}}, {std::move(view)}, 0, [&] { return *held; });
}

void Clipboard::append(PieceRun run, std::vector<FrozenBytes> views, std::uint64_t source) {
    // The views hold the bytes, so the whole content can be read back from them alone.
    auto bytes_of = [](const std::vector<FrozenBytes>& vs) {
        std::string out;
        for (const FrozenBytes& v : vs) out.append(reinterpret_cast<const char*>(v.bytes.data()), v.bytes.size());
        return out;
    };
    if (!content_ || content_->source != source) {
        std::string text = bytes_of(views);
        set(std::move(run), std::move(views), source, [&] { return text; });
        return;
    }
    PieceRun joined = std::move(content_->run);
    joined.insert(joined.end(), run.begin(), run.end());
    std::vector<FrozenBytes> all = std::move(content_->views);
    all.insert(all.end(), std::make_move_iterator(views.begin()), std::make_move_iterator(views.end()));
    std::string text = bytes_of(all);
    set(std::move(joined), std::move(all), source, [&] { return text; });
}

void Clipboard::rebind(PieceRun run, std::vector<FrozenBytes> views, std::uint64_t source) {
    if (!content_) return;
    content_->run = std::move(run);
    content_->views = std::move(views);
    content_->source = source;
}

}  // namespace mod
