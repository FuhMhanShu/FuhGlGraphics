#include "test_framework.hpp"
#include "tests.hpp"

#include <cstdio>

int main() {
    runVersionTests();
    runRenderTests();
    runPostPassTests();
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
    runHbaoTests();
    runProceduralNoiseTests();
    runProceduralMcTests();
    runProceduralLsystemTests();
    runProceduralWfcTests();
    runProceduralVoronoiTests();
    runProceduralSdfTests();
    runProceduralCreatureTests();
    runProceduralCreaturegenTests();
    runProceduralWorldgenTests();
    runProceduralRoadgenTests();
    runProceduralClutterTests();
    runProceduralLifegenTests();
    runProceduralHouseTests();
    runProceduralCitygenTests();
    runProceduralPlantTests();
    runMcpTests();
    runProceduralVegetationTests();
    runWeatherTests();
    std::fprintf(stderr, "fuhglgraphics_tests: %d checks, %d failures\n",
                 fuhglgraphicstest::checks(), fuhglgraphicstest::failures());
    return fuhglgraphicstest::failures() == 0 ? 0 : 1;
}
