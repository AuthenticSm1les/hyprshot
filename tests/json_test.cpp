// Unit tests for the JSON reader used on hyprctl output.
#include <iostream>
#include <string>

#include "check.hpp"
#include "json.hpp"

using shot::json::Value;

namespace {

Value must_parse(const std::string& text) {
    std::string error;
    auto        value = shot::json::parse(text, &error);
    if (!value) {
        ++check::failures;
        std::cerr << "  FAIL: expected to parse: " << text << " (" << error << ")\n";
        return Value();
    }
    ++check::checks;
    return *value;
}

void expect_rejected(const std::string& text, const std::string& what) {
    std::string error;
    const auto  value = shot::json::parse(text, &error);
    check::expect(!value.has_value() && !error.empty(), what);
}

void test_scalars() {
    check::expect(must_parse("true").as_bool(), "true parses");
    check::expect(!must_parse("false").as_bool(true), "false parses");
    check::expect(must_parse("null").is_null(), "null parses");
    check::expect_eq(must_parse("42").as_number(), 42.0, "integer");
    check::expect_eq(must_parse("-7").as_number(), -7.0, "negative integer");
    check::expect_eq(must_parse("3.5").as_number(), 3.5, "decimal");
    check::expect_eq(must_parse("1e3").as_number(), 1000.0, "exponent");
    check::expect_eq(must_parse("-1.5e-2").as_number(), -0.015, "signed exponent");
    check::expect_eq(must_parse("\"hi\"").as_string(), std::string("hi"), "string");

    // Non-matching accessors must fall back rather than throw.
    check::expect_eq(must_parse("\"hi\"").as_number(9.0), 9.0, "number fallback");
    check::expect_eq(must_parse("1").as_string(), std::string(), "string fallback");
    check::expect_eq(must_parse("1").size(), std::size_t {0}, "scalar size is 0");
    check::expect_eq(must_parse("{}").size(), std::size_t {0}, "empty object size");
    check::expect_eq(must_parse("[]").size(), std::size_t {0}, "empty array size");

    // Whitespace is insignificant.
    check::expect_eq(must_parse("  \n\t 7 \r ").as_number(), 7.0, "surrounding whitespace");
}

void test_string_escapes() {
    check::expect_eq(must_parse(R"("a\"b")").as_string(), std::string("a\"b"), "escaped quote");
    check::expect_eq(must_parse(R"("a\\b")").as_string(), std::string("a\\b"), "escaped backslash");
    check::expect_eq(must_parse(R"("a\nb")").as_string(), std::string("a\nb"), "escaped newline");
    check::expect_eq(must_parse(R"("a\/b")").as_string(), std::string("a/b"), "escaped slash");
    check::expect_eq(must_parse(R"("\u0041")").as_string(), std::string("A"), "basic \\u escape");
    check::expect_eq(must_parse(R"("\u00e9")").as_string(), std::string("\xC3\xA9"), "2-byte \\u escape");
    check::expect_eq(must_parse(R"("\u20ac")").as_string(), std::string("\xE2\x82\xAC"), "3-byte \\u escape");
    check::expect_eq(must_parse(R"("\ud83d\ude00")").as_string(), std::string("\xF0\x9F\x98\x80"),
                     "surrogate pair");
    check::expect_eq(must_parse(R"("\ud83d")").as_string(), std::string("\xEF\xBF\xBD"),
                     "lone surrogate becomes U+FFFD");
}

void test_containers() {
    const Value object = must_parse(R"({"a":1,"b":[1,2,3],"c":{"d":"x"},"e":null})");

    check::expect(object.is_object(), "object parses");
    check::expect_eq(object.size(), std::size_t {4}, "object size");
    check::expect_eq(object["a"].as_number(), 1.0, "object member");
    check::expect(object["missing"].is_null(), "missing member is null");
    check::expect(object["e"].is_null(), "explicit null member");

    const Value& array = object["b"];
    check::expect(array.is_array(), "nested array");
    check::expect_eq(array.size(), std::size_t {3}, "array size");
    check::expect_eq(array[2].as_number(), 3.0, "array element");
    check::expect(array[99].is_null(), "out-of-range index is null");
    check::expect(array["key"].is_null(), "key lookup on an array is null");

    check::expect_eq(object["c"]["d"].as_string(), std::string("x"), "nested object");

    // A top-level array, as `hyprctl clients -j` returns.
    const Value clients = must_parse(R"([{"class":"a","size":[10,20]},{"class":"b"}])");
    check::expect(clients.is_array(), "top-level array");
    check::expect_eq(clients.size(), std::size_t {2}, "top-level array size");
    check::expect_eq(clients[0]["class"].as_string(), std::string("a"), "array of objects");
    check::expect_eq(clients[0]["size"][1].as_number(), 20.0, "nested numbers");
    check::expect(clients[1]["size"].is_null(), "absent nested member");
}

void test_rejections() {
    expect_rejected("", "empty input rejected");
    expect_rejected("   ", "blank input rejected");
    expect_rejected("{", "unterminated object rejected");
    expect_rejected("[1,", "unterminated array rejected");
    expect_rejected("[1,]", "trailing comma rejected");
    expect_rejected(R"({"a":})", "missing value rejected");
    expect_rejected(R"({"a" 1})", "missing colon rejected");
    expect_rejected(R"({a:1})", "unquoted key rejected");
    expect_rejected("tru", "truncated literal rejected");
    expect_rejected("nul", "truncated null rejected");
    expect_rejected("1 2", "trailing data rejected");
    expect_rejected(R"("unterminated)", "unterminated string rejected");
    expect_rejected("\"line\nbreak\"", "raw control character rejected");
    expect_rejected(R"("\q")", "unknown escape rejected");
    expect_rejected(R"("\u00")", "truncated \\u rejected");
    expect_rejected(R"("\uzzzz")", "bad hex rejected");
    expect_rejected("-", "bare minus rejected");
    expect_rejected("1e999", "overflowing number rejected");
    expect_rejected("nan", "nan rejected");
}

}  // namespace

int main() {
    test_scalars();
    test_string_escapes();
    test_containers();
    test_rejections();
    return check::report("json_test");
}
