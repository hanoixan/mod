#include "syntax/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <unordered_map>

#include "text/utf8.hpp"

namespace mod {
namespace {

const std::string kEmptyString;

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

// Escapes `s` completely (no carry): the body of a JSON string literal.
void escape_into(std::string& out, std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c >= 0x20 && c < 0x80 && c != '"' && c != '\\') {
            std::size_t j = i + 1;
            while (j < s.size()) {
                const auto d = static_cast<unsigned char>(s[j]);
                if (d < 0x20 || d >= 0x80 || d == '"' || d == '\\') break;
                ++j;
            }
            out.append(s.substr(i, j - i));
            i = j;
            continue;
        }
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0xF]);
            }
            ++i;
            continue;
        }
        const Decoded d = decode(std::as_bytes(std::span(s.data() + i, s.size() - i)));
        if (d.valid) out.append(s.substr(i, d.len));
        else out.append(kReplacement);
        i += d.len;
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    Result<Json> run() {
        if (s_.starts_with("\xEF\xBB\xBF")) pos_ = 3;
        skip_ws();
        Json v;
        if (!value(v, 0)) return std::unexpected(std::move(err_));
        skip_ws();
        if (pos_ != s_.size()) {
            fail("unexpected trailing characters");
            return std::unexpected(std::move(err_));
        }
        return v;
    }

private:
    bool fail(std::string_view what) {
        err_ = make_error(ErrorCode::format, std::format("JSON: {} at byte {}", what, pos_));
        return false;
    }

    void skip_ws() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++pos_;
        }
    }

    bool literal(std::string_view word) {
        if (s_.substr(pos_, word.size()) != word) return fail("invalid literal");
        pos_ += word.size();
        return true;
    }

    bool value(Json& out, int depth) {
        if (pos_ >= s_.size()) return fail("unexpected end of input");
        switch (s_[pos_]) {
            case '{': return object(out, depth + 1);
            case '[': return array(out, depth + 1);
            case '"': {
                std::string str;
                if (!string(str)) return false;
                out = Json(std::move(str));
                return true;
            }
            case 't':
                out = Json(true);
                return literal("true");
            case 'f':
                out = Json(false);
                return literal("false");
            case 'n':
                out = Json(nullptr);
                return literal("null");
            default: return number(out);
        }
    }

    static constexpr std::size_t kIndexAfter = 16;

    bool object(Json& out, int depth) {
        if (depth > Json::kMaxDepth) return fail(std::format("nesting deeper than {} levels", Json::kMaxDepth));
        ++pos_;  // '{'
        Json::Object members;
        std::unordered_map<std::string, std::size_t> index;  // name -> member, once there are many
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            out = Json(std::move(members));
            return true;
        }
        for (;;) {
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != '"') return fail("expected a member name");
            std::string key;
            if (!string(key)) return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            skip_ws();
            Json v;
            if (!value(v, depth)) return false;
            // A duplicate name: the last one wins. Past a few members the names are found
            // through an index, so a huge object stays linear.
            std::optional<std::size_t> at;
            if (members.size() < kIndexAfter) {
                for (std::size_t m = 0; m < members.size(); ++m)
                    if (members[m].first == key) at = m;
            } else {
                if (index.empty())
                    for (std::size_t m = 0; m < members.size(); ++m) index.emplace(members[m].first, m);
                if (const auto it = index.find(key); it != index.end()) at = it->second;
            }
            if (at) {
                members[*at].second = std::move(v);
            } else {
                if (!index.empty()) index.emplace(key, members.size());
                members.emplace_back(std::move(key), std::move(v));
            }
            skip_ws();
            if (pos_ >= s_.size()) return fail("unexpected end of input");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == '}') {
                ++pos_;
                out = Json(std::move(members));
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool array(Json& out, int depth) {
        if (depth > Json::kMaxDepth) return fail(std::format("nesting deeper than {} levels", Json::kMaxDepth));
        ++pos_;  // '['
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            out = Json(Json::Array{});
            return true;
        }
        // Optimistically compact: integers go straight into an int64 vector until the
        // first element that is not an integer.
        Json::IntArray ints;
        Json::Array elems;
        bool compact = true;
        for (;;) {
            skip_ws();
            if (compact && pos_ < s_.size() && (s_[pos_] == '-' || (s_[pos_] >= '0' && s_[pos_] <= '9'))) {
                std::int64_t i = 0;
                if (int_fast(i)) {
                    ints.push_back(i);
                    goto next;
                }
            }
            {
                Json v;
                if (!value(v, depth)) return false;
                if (compact && v.is_int()) {
                    ints.push_back(v.as_int());
                } else {
                    if (compact) {
                        compact = false;
                        elems.reserve(ints.size() + 1);
                        for (std::int64_t i : ints) elems.emplace_back(i);
                        Json::IntArray().swap(ints);
                    }
                    elems.push_back(std::move(v));
                }
            }
        next:
            skip_ws();
            if (pos_ >= s_.size()) return fail("unexpected end of input");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == ']') {
                ++pos_;
                out = compact ? Json(std::move(ints)) : Json(std::move(elems));
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    // A plain integer token (no fraction or exponent) that fits int64; leaves `pos_`
    // unchanged and returns false otherwise, so the general path re-parses it.
    bool int_fast(std::int64_t& out) {
        std::size_t p = pos_;
        if (s_[p] == '-') ++p;
        if (p >= s_.size()) return false;
        if (s_[p] == '0') {
            ++p;
        } else if (s_[p] >= '1' && s_[p] <= '9') {
            while (p < s_.size() && s_[p] >= '0' && s_[p] <= '9') ++p;
        } else {
            return false;
        }
        if (p < s_.size() && (s_[p] == '.' || s_[p] == 'e' || s_[p] == 'E')) return false;
        const auto r = std::from_chars(s_.data() + pos_, s_.data() + p, out);
        if (r.ec != std::errc{} || r.ptr != s_.data() + p) return false;
        pos_ = p;
        return true;
    }

    bool number(Json& out) {
        const std::size_t start = pos_;
        std::size_t p = pos_;
        auto digits = [&] {
            const std::size_t d = p;
            while (p < s_.size() && s_[p] >= '0' && s_[p] <= '9') ++p;
            return p > d;
        };
        if (p < s_.size() && s_[p] == '-') ++p;
        if (p < s_.size() && s_[p] == '0') {
            ++p;
        } else if (p < s_.size() && s_[p] >= '1' && s_[p] <= '9') {
            digits();
        } else {
            return fail("invalid value");
        }
        bool integral = true;
        if (p < s_.size() && s_[p] == '.') {
            ++p;
            integral = false;
            if (!digits()) {
                pos_ = p;
                return fail("expected a digit");
            }
        }
        if (p < s_.size() && (s_[p] == 'e' || s_[p] == 'E')) {
            ++p;
            integral = false;
            if (p < s_.size() && (s_[p] == '+' || s_[p] == '-')) ++p;
            if (!digits()) {
                pos_ = p;
                return fail("expected a digit");
            }
        }
        const char* first = s_.data() + start;
        const char* last = s_.data() + p;
        pos_ = p;
        if (integral) {
            std::int64_t i = 0;
            const auto r = std::from_chars(first, last, i);
            if (r.ec == std::errc{} && r.ptr == last) {
                out = Json(i);
                return true;
            }
        }
        double d = 0;
        const auto r = std::from_chars(first, last, d);
        if (r.ec == std::errc::result_out_of_range) {
            // Too large overflows to infinity; too small underflows to zero.
            bool tiny = false;
            for (const char* c = first; c < last; ++c) {
                if (*c == 'e' || *c == 'E') {
                    tiny = c + 1 < last && c[1] == '-';
                    break;
                }
            }
            const bool negative = *first == '-';
            d = tiny ? (negative ? -0.0 : 0.0)
                     : (negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
        } else if (r.ec != std::errc{}) {
            return fail("invalid number");
        }
        out = Json(d);
        return true;
    }

    bool hex4(std::uint32_t& out) {
        if (pos_ + 4 > s_.size()) return fail("truncated \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<std::uint32_t>(c - 'A' + 10);
            else {
                --pos_;
                return fail("invalid \\u escape");
            }
        }
        return true;
    }

    bool string(std::string& out) {
        ++pos_;  // opening quote
        for (;;) {
            const std::size_t run = pos_;
            while (pos_ < s_.size()) {
                const auto c = static_cast<unsigned char>(s_[pos_]);
                if (c == '"' || c == '\\' || c < 0x20) break;
                ++pos_;
            }
            out.append(s_.substr(run, pos_ - run));
            if (pos_ >= s_.size()) return fail("unterminated string");
            const char c = s_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c != '\\') return fail("control character in string");
            ++pos_;
            if (pos_ >= s_.size()) return fail("unterminated string");
            const char e = s_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t u = 0;
                    if (!hex4(u)) return false;
                    if (u >= 0xD800 && u <= 0xDBFF) {
                        // A high surrogate combines with a following low one.
                        if (s_.substr(pos_, 2) == "\\u") {
                            const std::size_t save = pos_;
                            pos_ += 2;
                            std::uint32_t lo = 0;
                            if (!hex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                append_utf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                                break;
                            }
                            pos_ = save;  // not a low surrogate: parse it on its own
                        }
                        out.append(kReplacement);
                    } else if (u >= 0xDC00 && u <= 0xDFFF) {
                        out.append(kReplacement);  // a lone low surrogate
                    } else {
                        append_utf8(out, static_cast<char32_t>(u));
                    }
                    break;
                }
                default:
                    --pos_;
                    return fail("invalid escape");
            }
        }
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    Error err_;
};

}  // namespace

Result<Json> Json::parse(std::string_view text) { return Parser(text).run(); }

std::string Json::dump() const {
    std::string out;
    dump_to(out);
    return out;
}

void Json::dump_to(std::string& out) const {
    switch (v_.index()) {
        case 0: out += "null"; break;
        case 1: out += std::get<bool>(v_) ? "true" : "false"; break;
        case 2: {
            char buf[24];
            const auto r = std::to_chars(buf, buf + sizeof buf, std::get<std::int64_t>(v_));
            out.append(buf, r.ptr);
            break;
        }
        case 3: {
            const double d = std::get<double>(v_);
            if (!std::isfinite(d)) {
                out += "null";
                break;
            }
            char buf[32];
            const auto r = std::to_chars(buf, buf + sizeof buf, d);
            const std::string_view written(buf, static_cast<std::size_t>(r.ptr - buf));
            out += written;
            // An integral double keeps a fraction, so it reads back as a double, not an integer.
            if (written.find_first_of(".eE") == std::string_view::npos) out += ".0";
            break;
        }
        case 4:
            out.push_back('"');
            escape_into(out, std::get<std::string>(v_));
            out.push_back('"');
            break;
        case 5: {
            out.push_back('[');
            bool first = true;
            for (const Json& e : std::get<Array>(v_)) {
                if (!first) out.push_back(',');
                first = false;
                e.dump_to(out);
            }
            out.push_back(']');
            break;
        }
        case 6: {
            out.push_back('{');
            bool first = true;
            for (const auto& [k, v] : std::get<Object>(v_)) {
                if (!first) out.push_back(',');
                first = false;
                out.push_back('"');
                escape_into(out, k);
                out += "\":";
                v.dump_to(out);
            }
            out.push_back('}');
            break;
        }
        case 7: {
            out.push_back('[');
            bool first = true;
            char buf[24];
            for (std::int64_t i : std::get<IntArray>(v_)) {
                if (!first) out.push_back(',');
                first = false;
                const auto r = std::to_chars(buf, buf + sizeof buf, i);
                out.append(buf, r.ptr);
            }
            out.push_back(']');
            break;
        }
        default: std::unreachable();
    }
}

Json::Type Json::type() const noexcept {
    switch (v_.index()) {
        case 0: return Type::null;
        case 1: return Type::boolean;
        case 2: return Type::integer;
        case 3: return Type::number;
        case 4: return Type::string;
        case 5:
        case 7: return Type::array;
        default: return Type::object;
    }
}

bool Json::as_bool() const noexcept {
    const bool* b = std::get_if<bool>(&v_);
    return b != nullptr && *b;
}

std::int64_t Json::as_int() const noexcept {
    if (const auto* i = std::get_if<std::int64_t>(&v_)) return *i;
    if (const auto* d = std::get_if<double>(&v_)) {
        if (!std::isfinite(*d)) return 0;
        if (*d >= 9.2233720368547758e18) return std::numeric_limits<std::int64_t>::max();
        if (*d <= -9.2233720368547758e18) return std::numeric_limits<std::int64_t>::min();
        return static_cast<std::int64_t>(*d);
    }
    return 0;
}

double Json::as_double() const noexcept {
    if (const auto* d = std::get_if<double>(&v_)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&v_)) return static_cast<double>(*i);
    return 0.0;
}

const std::string& Json::as_string() const noexcept {
    const auto* s = std::get_if<std::string>(&v_);
    return s != nullptr ? *s : kEmptyString;
}

std::size_t Json::size() const noexcept {
    if (const auto* a = std::get_if<Array>(&v_)) return a->size();
    if (const auto* o = std::get_if<Object>(&v_)) return o->size();
    if (const auto* c = std::get_if<IntArray>(&v_)) return c->size();
    return 0;
}

const Json* Json::get(std::string_view key) const noexcept {
    const auto* o = std::get_if<Object>(&v_);
    if (o == nullptr) return nullptr;
    for (const auto& [k, v] : *o)
        if (k == key) return &v;
    return nullptr;
}

Json* Json::get(std::string_view key) noexcept { return const_cast<Json*>(std::as_const(*this).get(key)); }

const Json* Json::get(std::size_t index) const noexcept {
    const auto* a = std::get_if<Array>(&v_);
    if (a == nullptr || index >= a->size()) return nullptr;
    return &(*a)[index];
}

std::span<const std::int64_t> Json::ints() const noexcept {
    if (const auto* c = std::get_if<IntArray>(&v_)) return *c;
    return {};
}

std::span<const std::pair<std::string, Json>> Json::members() const noexcept {
    if (const auto* o = std::get_if<Object>(&v_)) return *o;
    return {};
}

std::span<const Json> Json::elements() const noexcept {
    if (const auto* a = std::get_if<Array>(&v_)) return *a;
    return {};
}

Json& Json::set(std::string key, Json value) {
    if (is_null()) v_ = Object{};
    auto& o = std::get<Object>(v_);  // throws bad_variant_access on misuse: a programming error
    for (auto& m : o) {
        if (m.first == key) {
            m.second = std::move(value);
            return m.second;
        }
    }
    o.emplace_back(std::move(key), std::move(value));
    return o.back().second;
}

bool Json::erase(std::string_view key) {
    auto* o = std::get_if<Object>(&v_);
    if (o == nullptr) return false;
    return std::erase_if(*o, [&](const auto& m) { return m.first == key; }) > 0;
}

Json& Json::push_back(Json value) {
    if (is_null()) v_ = Array{};
    if (auto* c = std::get_if<IntArray>(&v_)) {
        Array expanded;
        expanded.reserve(c->size() + 1);
        for (std::int64_t i : *c) expanded.emplace_back(i);
        v_ = std::move(expanded);
    }
    auto& a = std::get<Array>(v_);
    a.push_back(std::move(value));
    return a.back();
}

bool operator==(const Json& a, const Json& b) {
    const auto ta = a.type();
    if (ta != b.type()) return false;
    switch (ta) {
        case Json::Type::null: return true;
        case Json::Type::boolean: return a.as_bool() == b.as_bool();
        case Json::Type::integer: return a.as_int() == b.as_int();
        case Json::Type::number: return a.as_double() == b.as_double();
        case Json::Type::string: return a.as_string() == b.as_string();
        case Json::Type::array: {
            if (a.size() != b.size()) return false;
            auto element = [](const Json& j, std::size_t i) -> Json {
                if (j.is_compact_ints()) return Json(j.ints()[i]);
                return *j.get(i);
            };
            if (a.is_compact_ints() && b.is_compact_ints())
                return std::equal(a.ints().begin(), a.ints().end(), b.ints().begin());
            for (std::size_t i = 0; i < a.size(); ++i)
                if (!(element(a, i) == element(b, i))) return false;
            return true;
        }
        case Json::Type::object: {
            if (a.size() != b.size()) return false;
            for (const auto& [k, v] : a.members()) {
                const Json* other = b.get(k);
                if (other == nullptr || !(*other == v)) return false;
            }
            return true;
        }
    }
    return false;
}

void JsonStringWriter::write(std::string_view bytes) {
    if (carry_len_ > 0) {
        // Complete the carried sequence first.
        const auto lead = static_cast<unsigned char>(carry_[0]);
        const std::size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        while (carry_len_ < need && !bytes.empty() && (static_cast<unsigned char>(bytes[0]) & 0xC0) == 0x80) {
            carry_[carry_len_++] = bytes[0];
            bytes.remove_prefix(1);
        }
        if (carry_len_ < need && bytes.empty()) return;  // still incomplete
        escape_into(out_, std::string_view(carry_, carry_len_));
        carry_len_ = 0;
    }
    // Hold back an incomplete sequence at the end.
    std::size_t keep = 0;
    for (std::size_t back = 1; back <= 3 && back <= bytes.size(); ++back) {
        const auto c = static_cast<unsigned char>(bytes[bytes.size() - back]);
        if ((c & 0xC0) == 0x80) continue;  // a continuation byte: look further back
        const std::size_t need = c >= 0xF0 && c <= 0xF4 ? 4 : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xC2 && c <= 0xDF ? 2 : 0;
        if (need > back) keep = back;
        break;
    }
    escape_into(out_, bytes.substr(0, bytes.size() - keep));
    for (std::size_t k = 0; k < keep; ++k) carry_[k] = bytes[bytes.size() - keep + k];
    carry_len_ = keep;
}

void JsonStringWriter::finish() {
    if (carry_len_ > 0) escape_into(out_, std::string_view(carry_, carry_len_));
    carry_len_ = 0;
}

}  // namespace mod
