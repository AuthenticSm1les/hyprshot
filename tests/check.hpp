// A minimal test harness: no dependencies, no framework.
#pragma once

#include <iostream>
#include <string>

namespace check {

inline int failures = 0;
inline int checks   = 0;

inline void expect(bool condition, const std::string& what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "  FAIL: " << what << "\n";
    }
}

template <typename Actual, typename Expected>
void expect_eq(const Actual& actual, const Expected& expected, const std::string& what) {
    ++checks;
    if (!(actual == expected)) {
        ++failures;
        std::cerr << "  FAIL: " << what << "\n"
                  << "        expected: " << expected << "\n"
                  << "        actual:   " << actual << "\n";
    }
}

// Two C strings would otherwise be compared as pointers, which passes or fails
// by literal pooling rather than by content.
inline void expect_eq(const char* actual, const char* expected, const std::string& what) {
    expect(std::string(actual != nullptr ? actual : "") ==
               std::string(expected != nullptr ? expected : ""),
           what);
}

inline int report(const std::string& suite) {
    if (failures == 0) {
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << suite << ": " << failures << " of " << checks << " checks FAILED\n";
    return 1;
}

}  // namespace check
