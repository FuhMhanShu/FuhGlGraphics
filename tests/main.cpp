#include "test_framework.hpp"
#include "tests.hpp"

#include <cstdio>

int main() {
    runVersionTests();
    runRenderTests();
    runSceneTests();
    std::fprintf(stderr, "fuhglgraphics_tests: %d checks, %d failures\n",
                 fuhglgraphicstest::checks(), fuhglgraphicstest::failures());
    return fuhglgraphicstest::failures() == 0 ? 0 : 1;
}
