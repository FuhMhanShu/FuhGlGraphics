#include "test_framework.hpp"
#include "tests.hpp"

#include <fuhglgraphics/version.hpp>

#include <cstring>

#ifndef FUHGLGRAPHICS_VERSION_STRING
#define FUHGLGRAPHICS_VERSION_STRING "0.1.0"
#endif

void runVersionTests() {
    // Compile-time version constants match the project version 0.1.0
    CHECK(fuhglgraphics::kVersionMajor == 0);
    CHECK(fuhglgraphics::kVersionMinor == 1);
    CHECK(fuhglgraphics::kVersionPatch == 0);

    // Runtime string is the compile-time macro
    const char* v = fuhglgraphics::versionString();
    CHECK(v != nullptr);
    CHECK(std::strcmp(v, FUHGLGRAPHICS_VERSION_STRING) == 0);

    // Format "x.y.z": exactly two dots, digits around them
    int dots = 0;
    bool digitsOnly = true;
    for (const char* p = v; *p; ++p) {
        if (*p == '.') {
            ++dots;
        } else if (*p < '0' || *p > '9') {
            digitsOnly = false;
        }
    }
    CHECK(dots == 2);
    CHECK(digitsOnly);
}
