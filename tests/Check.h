#pragma once

// Minimal test checks. Each test is a plain executable: it runs its checks,
// prints any failure with file and line, and returns
// burp::test::failures() from main (0 = pass), which is what CTest reads.

#include <cmath>
#include <cstdio>

namespace burp::test {

inline int& failures()
{
    static int count = 0;
    return count;
}

inline void check(bool ok, const char* expression, const char* file, int line)
{
    if (!ok) {
        std::printf("FAILED %s:%d  %s\n", file, line, expression);
        ++failures();
    }
}

inline void checkNear(double actual, double expected, double tolerance, const char* expression,
                      const char* file, int line)
{
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::printf("FAILED %s:%d  %s = %g, expected %g +- %g\n", file, line, expression, actual,
                    expected, tolerance);
        ++failures();
    }
}

} // namespace burp::test

#define BURP_CHECK(condition) ::burp::test::check((condition), #condition, __FILE__, __LINE__)
#define BURP_CHECK_NEAR(actual, expected, tolerance) \
    ::burp::test::checkNear((actual), (expected), (tolerance), #actual, __FILE__, __LINE__)
