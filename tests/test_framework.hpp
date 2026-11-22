#pragma once

// Minimal test framework (no external dependency): CHECK / CHECK_NEAR macros
// count checks and failures; main() reports and sets the exit code.

#include <cmath>
#include <cstdio>

namespace fuhglgraphicstest {
inline int& checks() { static int c = 0; return c; }
inline int& failures() { static int f = 0; return f; }
} // namespace fuhglgraphicstest

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        ++fuhglgraphicstest::checks();                                                       \
        if (!(cond)) {                                                                       \
            ++fuhglgraphicstest::failures();                                                 \
            std::fprintf(stderr, "FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);      \
        }                                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                \
    do {                                                                                     \
        ++fuhglgraphicstest::checks();                                                       \
        const double _a = static_cast<double>(a);                                           \
        const double _b = static_cast<double>(b);                                           \
        if (std::fabs(_a - _b) > (eps)) {                                                    \
            ++fuhglgraphicstest::failures();                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s (=%.9g) != %s (=%.9g), eps=%g\n",          \
                         __FILE__, __LINE__, #a, _a, #b, _b, static_cast<double>(eps));     \
        }                                                                                    \
    } while (0)
