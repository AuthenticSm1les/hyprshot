#include "json.hpp"

#include <charconv>
#include <cstdint>
#include <system_error>

namespace shot::json {
namespace {

const Value& null_value() {
    static const Value value;
    return value;
}

const Value::Object& empty_object() {
    static const Value::Object value;
    return value;
}

const std::string& empty_string() {
    static const std::string value;
    return value;
}

constexpr int kMaxDepth = 64;

void append_utf8(std::uint32_t code_point, std::string& out) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

class Parser {
  public:
    Parser(std::string_view text, std::string& error) : m_text(text), m_error(error) {}

    bool parse(Value& out) {
        skip_whitespace();
        if (!parse_value(out, 0))
            return false;
        skip_whitespace();
        if (m_pos != m_text.size())
            return fail("trailing data after JSON value");
        return true;
    }

  private:
    bool fail(std::string message) {
        if (m_error.empty())
            m_error = std::move(message);
        return false;
    }

    void skip_whitespace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                break;
            ++m_pos;
        }
    }

    bool at_end() const { return m_pos >= m_text.size(); }
    char peek() const { return at_end() ? '\0' : m_text[m_pos]; }

    bool consume(char expected) {
        if (peek() != expected)
            return false;
        ++m_pos;
        return true;
    }

    bool parse_value(Value& out, int depth) {
        if (depth > kMaxDepth)
            return fail("JSON nested too deeply");

        skip_whitespace();
        if (at_end())
            return fail("unexpected end of input");

        switch (peek()) {
            case '{': return parse_object(out, depth);
            case '[': return parse_array(out, depth);
            case '"': {
                std::string text;
                if (!parse_string(text))
                    return false;
                out = Value::string(std::move(text));
                return true;
            }
            case 't': return parse_literal("true", Value::boolean(true), out);
            case 'f': return parse_literal("false", Value::boolean(false), out);
            case 'n': return parse_literal("null", Value(), out);
            default: return parse_number(out);
        }
    }

    bool parse_literal(std::string_view literal, Value value, Value& out) {
        if (m_text.compare(m_pos, literal.size(), literal) != 0)
            return fail("invalid literal");
        m_pos += literal.size();
        out = std::move(value);
        return true;
    }

    bool parse_object(Value& out, int depth) {
        ++m_pos;  // '{'
        Value::Object members;

        skip_whitespace();
        if (consume('}')) {
            out = Value::object(std::move(members));
            return true;
        }

        for (;;) {
            skip_whitespace();
            std::string key;
            if (!parse_string(key))
                return false;

            skip_whitespace();
            if (!consume(':'))
                return fail("expected ':' in object");

            skip_whitespace();
            Value member;
            if (!parse_value(member, depth + 1))
                return false;
            members.insert_or_assign(std::move(key), std::move(member));

            skip_whitespace();
            if (consume(','))
                continue;
            if (consume('}')) {
                out = Value::object(std::move(members));
                return true;
            }
            return fail("expected ',' or '}' in object");
        }
    }

    bool parse_array(Value& out, int depth) {
        ++m_pos;  // '['
        Value::Array elements;

        skip_whitespace();
        if (consume(']')) {
            out = Value::array(std::move(elements));
            return true;
        }

        for (;;) {
            skip_whitespace();
            Value element;
            if (!parse_value(element, depth + 1))
                return false;
            elements.push_back(std::move(element));

            skip_whitespace();
            if (consume(','))
                continue;
            if (consume(']')) {
                out = Value::array(std::move(elements));
                return true;
            }
            return fail("expected ',' or ']' in array");
        }
    }

    bool parse_hex4(std::uint32_t& out) {
        if (m_pos + 4 > m_text.size())
            return fail("truncated \\u escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_digit(m_text[m_pos + static_cast<std::size_t>(i)]);
            if (digit < 0)
                return fail("invalid hex digit in \\u escape");
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        m_pos += 4;
        out = value;
        return true;
    }

    bool parse_string(std::string& out) {
        if (!consume('"'))
            return fail("expected string");

        out.clear();
        while (!at_end()) {
            const char c = m_text[m_pos];

            if (c == '"') {
                ++m_pos;
                return true;
            }

            if (c == '\\') {
                ++m_pos;
                if (at_end())
                    return fail("truncated escape sequence");

                switch (m_text[m_pos]) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        ++m_pos;
                        std::uint32_t code_point = 0;
                        if (!parse_hex4(code_point))
                            return false;

                        // Combine a surrogate pair into one code point.
                        if (code_point >= 0xD800 && code_point <= 0xDBFF &&
                            m_text.compare(m_pos, 2, "\\u") == 0) {
                            const std::size_t saved = m_pos;
                            m_pos += 2;
                            std::uint32_t low = 0;
                            if (!parse_hex4(low))
                                return false;
                            if (low >= 0xDC00 && low <= 0xDFFF)
                                code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                            else
                                m_pos = saved;  // not a low surrogate; reparse it on its own
                        }

                        if (code_point >= 0xD800 && code_point <= 0xDFFF)
                            append_utf8(0xFFFD, out);  // lone surrogate
                        else
                            append_utf8(code_point, out);
                        continue;  // m_pos already points past the escape
                    }
                    default: return fail("invalid escape sequence");
                }
                ++m_pos;
                continue;
            }

            if (static_cast<unsigned char>(c) < 0x20)
                return fail("unescaped control character in string");

            out.push_back(c);
            ++m_pos;
        }
        return fail("unterminated string");
    }

    bool parse_number(Value& out) {
        const std::size_t start = m_pos;
        if (peek() == '-')
            ++m_pos;

        if (at_end() || !is_digit(peek()))
            return fail("invalid number");
        while (!at_end() && is_digit(peek()))
            ++m_pos;

        if (peek() == '.') {
            ++m_pos;
            if (at_end() || !is_digit(peek()))
                return fail("invalid number");
            while (!at_end() && is_digit(peek()))
                ++m_pos;
        }

        if (peek() == 'e' || peek() == 'E') {
            ++m_pos;
            if (peek() == '+' || peek() == '-')
                ++m_pos;
            if (at_end() || !is_digit(peek()))
                return fail("invalid number");
            while (!at_end() && is_digit(peek()))
                ++m_pos;
        }

        double value = 0.0;
        const auto* first = m_text.data() + start;
        const auto* last  = m_text.data() + m_pos;
        const auto  conv  = std::from_chars(first, last, value, std::chars_format::general);
        if (conv.ec != std::errc {} || conv.ptr != last)
            return fail("number out of range");

        out = Value::number(value);
        return true;
    }

    std::string_view m_text;
    std::string&     m_error;
    std::size_t      m_pos = 0;
};

}  // namespace

Value Value::boolean(bool value) {
    Value result;
    result.m_data = value;
    return result;
}

Value Value::number(double value) {
    Value result;
    result.m_data = value;
    return result;
}

Value Value::string(std::string value) {
    Value result;
    result.m_data = std::move(value);
    return result;
}

Value Value::array(Array values) {
    Value result;
    result.m_data = std::make_shared<const Array>(std::move(values));
    return result;
}

Value Value::object(Object values) {
    Value result;
    result.m_data = std::make_shared<const Object>(std::move(values));
    return result;
}

bool Value::is_null() const { return std::holds_alternative<std::monostate>(m_data); }
bool Value::is_bool() const { return std::holds_alternative<bool>(m_data); }
bool Value::is_number() const { return std::holds_alternative<double>(m_data); }
bool Value::is_array() const { return std::holds_alternative<ArrayPtr>(m_data); }
bool Value::is_object() const { return std::holds_alternative<ObjectPtr>(m_data); }

bool Value::as_bool(bool fallback) const {
    const auto* value = std::get_if<bool>(&m_data);
    return value != nullptr ? *value : fallback;
}

double Value::as_number(double fallback) const {
    const auto* value = std::get_if<double>(&m_data);
    return value != nullptr ? *value : fallback;
}

const std::string& Value::as_string() const {
    const auto* value = std::get_if<std::string>(&m_data);
    return value != nullptr ? *value : empty_string();
}

const Value::Object& Value::as_object() const {
    const auto* value = std::get_if<ObjectPtr>(&m_data);
    return value != nullptr && *value ? **value : empty_object();
}

const Value& Value::operator[](std::string_view key) const {
    const auto* object = std::get_if<ObjectPtr>(&m_data);
    if (object == nullptr || !*object)
        return null_value();
    const auto it = (*object)->find(key);
    return it == (*object)->end() ? null_value() : it->second;
}

const Value& Value::operator[](std::size_t index) const {
    const auto* array = std::get_if<ArrayPtr>(&m_data);
    if (array == nullptr || !*array || index >= (*array)->size())
        return null_value();
    return (**array)[index];
}

std::size_t Value::size() const {
    if (const auto* array = std::get_if<ArrayPtr>(&m_data); array != nullptr && *array)
        return (*array)->size();
    if (const auto* object = std::get_if<ObjectPtr>(&m_data); object != nullptr && *object)
        return (*object)->size();
    return 0;
}

std::optional<Value> parse(std::string_view text, std::string* error) {
    std::string message;
    Parser      parser(text, message);

    Value value;
    if (!parser.parse(value)) {
        if (error != nullptr)
            *error = message.empty() ? "invalid JSON" : message;
        return std::nullopt;
    }
    return value;
}

}  // namespace shot::json
