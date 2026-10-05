// A small immutable JSON value used to read `hyprctl -j` output.
//
// Only what this utility needs is implemented: strict parsing, non-throwing
// accessors, and null results for anything missing or of the wrong type.
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace shot::json {

class Value {
  public:
    using Array  = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;

    Value() = default;

    static Value boolean(bool value);
    static Value number(double value);
    static Value string(std::string value);
    static Value array(Array values);
    static Value object(Object values);

    [[nodiscard]] bool is_null() const;
    [[nodiscard]] bool is_bool() const;
    [[nodiscard]] bool is_number() const;
    [[nodiscard]] bool is_array() const;
    [[nodiscard]] bool is_object() const;

    [[nodiscard]] bool               as_bool(bool fallback = false) const;
    [[nodiscard]] double             as_number(double fallback = 0.0) const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const Object&      as_object() const;

    // A null value when the key or index is absent, or when this is not an
    // object/array.
    [[nodiscard]] const Value& operator[](std::string_view key) const;
    [[nodiscard]] const Value& operator[](std::size_t index) const;

    [[nodiscard]] std::size_t size() const;

  private:
    using ArrayPtr  = std::shared_ptr<const Array>;
    using ObjectPtr = std::shared_ptr<const Object>;

    std::variant<std::monostate, bool, double, std::string, ArrayPtr, ObjectPtr> m_data;
};

// Parse `text`. On failure returns nullopt and, when `error` is given, a short
// description of the problem.
std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

}  // namespace shot::json
