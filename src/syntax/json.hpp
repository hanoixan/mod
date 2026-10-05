#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "util/error.hpp"

namespace mod {

// A minimal JSON value. Arrays whose elements are all integers are stored compactly
// as `int64_t`s when parsed (an LSP token array can hold millions); `ints()` reads them.
class Json {
public:
    enum class Type { null, boolean, integer, number, string, array, object };

    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;  // in insertion order, unique keys
    using IntArray = std::vector<std::int64_t>;

    static constexpr int kMaxDepth = 256;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : v_(b) {}
    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    Json(T i) : v_(static_cast<std::int64_t>(i)) {}
    Json(double d) : v_(d) {}
    Json(std::string s) : v_(std::move(s)) {}
    Json(std::string_view s) : v_(std::string(s)) {}
    Json(const char* s) : v_(std::string(s)) {}
    Json(Array a) : v_(std::move(a)) {}
    Json(Object o) : v_(std::move(o)) {}
    Json(IntArray a) : v_(std::move(a)) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    // Strict RFC 8259 (a leading UTF-8 BOM is skipped). Errors are `format` with the
    // byte offset in the message.
    static Result<Json> parse(std::string_view text);

    // Compact UTF-8; invalid UTF-8 bytes become U+FFFD and non-finite numbers `null`.
    std::string dump() const;
    void dump_to(std::string& out) const;

    Type type() const noexcept;
    bool is_null() const noexcept { return type() == Type::null; }
    bool is_bool() const noexcept { return type() == Type::boolean; }
    bool is_int() const noexcept { return type() == Type::integer; }
    bool is_number() const noexcept { return type() == Type::integer || type() == Type::number; }
    bool is_string() const noexcept { return type() == Type::string; }
    bool is_array() const noexcept { return type() == Type::array; }
    bool is_object() const noexcept { return type() == Type::object; }

    // Values of the wrong type read as false, 0, 0.0 or "".
    bool as_bool() const noexcept;
    std::int64_t as_int() const noexcept;  // a double is truncated
    double as_double() const noexcept;
    const std::string& as_string() const noexcept;

    // Elements of an array or members of an object; 0 for anything else.
    std::size_t size() const noexcept;

    // The member named `key` of an object, or null.
    const Json* get(std::string_view key) const noexcept;
    Json* get(std::string_view key) noexcept;
    // Element `index` of an array, or null. Null for a compact integer array too:
    // read those with `ints()`.
    const Json* get(std::size_t index) const noexcept;

    // The elements of a compact integer array; empty for anything else.
    std::span<const std::int64_t> ints() const noexcept;
    bool is_compact_ints() const noexcept { return std::holds_alternative<IntArray>(v_); }

    // The object's members, or empty.
    std::span<const std::pair<std::string, Json>> members() const noexcept;
    // The array's elements (not compact), or empty.
    std::span<const Json> elements() const noexcept;

    // Building. `set` makes a null value an object and replaces an existing key;
    // `push_back` makes a null value an array (a compact one is expanded).
    Json& set(std::string key, Json value);
    Json& push_back(Json value);
    // Removes the member named `key` of an object; false when there is none.
    bool erase(std::string_view key);

    // Deep equality; a compact integer array equals the same integers stored as elements.
    friend bool operator==(const Json& a, const Json& b);

private:
    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object, IntArray> v_ = nullptr;
};

// Appends `bytes` to `out` as the inside of a JSON string literal, for writing large
// text without building a Json value. Each invalid UTF-8 byte becomes U+FFFD. A UTF-8
// sequence split between calls is carried over; `finish` flushes an incomplete tail.
class JsonStringWriter {
public:
    explicit JsonStringWriter(std::string& out) : out_(out) {}
    void write(std::string_view bytes);
    void finish();

private:
    std::string& out_;
    char carry_[4] = {};
    std::size_t carry_len_ = 0;
};

}  // namespace mod
