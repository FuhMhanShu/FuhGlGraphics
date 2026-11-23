#include "test_framework.hpp"
#include "tests.hpp"

#include <cstdio>

int main() {
    runVersionTests();
    runRenderTests();
    runSceneTests();
    runIoTests();
    runParticlesTests();
    runAnimTests();
    runProbesTests();
    runWaterTests();
    runSsaoTests();
    runSsrTests();
    runBloomTests();
    runPickingTests();
    runSerializeTests();
    runTerrainLodTests();
    runVolumetricTests();
    runWetnessTests();
    runTonemapTests();
    runParallaxTests();
    runProceduralNoiseTests();
    std::fprintf(stderr, "fuhglgraphics_tests: %d checks, %d failures\n",
                 fuhglgraphicstest::checks(), fuhglgraphicstest::failures());
    return fuhglgraphicstest::failures() == 0 ? 0 : 1;
}
