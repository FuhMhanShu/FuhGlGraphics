// Unit tests for the G7 light-probe module (no GL context needed): SH L2 basis /
// projection / irradiance (constant radiance -> E = pi*L everywhere), ray-triangle
// intersection, grid layout, and the CPU bake (sky over a floor, red emissive wall in
// a closed box = color bleeding + occlusion, empty closed box = zero).

#include "test_framework.hpp"

#include <fuhglgraphics/probes/light_probes.hpp>

#include <cmath>
#include <vector>

using namespace fuhglgraphics;

namespace {

constexpr float kPi = 3.14159265358979f;

fuhgl::math::Vec3 fibSphere(int k, int n) {
    const float phi = static_cast<float>(k) * 2.39996322972865332f;
    const float z = 1.f - 2.f * (static_cast<float>(k) + 0.5f) / static_cast<float>(n);
    const float r = std::sqrt(std::max(1.f - z * z, 0.f));
    return {r * std::cos(phi), r * std::sin(phi), z};
}

void addQuad(std::vector<probes::ProbeTriangle>& soup, const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b,
             const fuhgl::math::Vec3& c, const fuhgl::math::Vec3& d, const fuhgl::math::Vec3& albedo,
             const fuhgl::math::Vec3& emissive = {0.f, 0.f, 0.f}) {
    soup.push_back(probes::ProbeTriangle{a, b, c, albedo, emissive});
    soup.push_back(probes::ProbeTriangle{a, c, d, albedo, emissive});
}

/// Closed box [-h,h]^3; the -X wall gets the given emissive (color bleeding source).
std::vector<probes::ProbeTriangle> makeBoxSoup(float h, const fuhgl::math::Vec3& redWallEmissive) {
    std::vector<probes::ProbeTriangle> soup;
    const fuhgl::math::Vec3 w{0.8f, 0.8f, 0.8f}; // white-ish walls
    addQuad(soup, {-h, -h, -h}, {-h, h, -h}, {-h, h, h}, {-h, -h, h}, w, redWallEmissive); // x = -h
    addQuad(soup, {h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h}, w);                     // x = +h
    addQuad(soup, {-h, -h, -h}, {-h, -h, h}, {h, -h, h}, {h, -h, -h}, w);                 // y = -h (floor)
    addQuad(soup, {-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h}, w);                     // y = +h (ceiling)
    addQuad(soup, {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, w);                 // z = -h
    addQuad(soup, {-h, -h, h}, {-h, h, h}, {h, h, h}, {h, -h, h}, w);                     // z = +h
    return soup;
}

void testShBasis() {
    float y[9];
    probes::shBasis({0.f, 0.f, 1.f}, y);
    CHECK_NEAR(y[0], 0.2820947918f, 1e-6f);          // Y00 constant
    CHECK_NEAR(y[2], 0.4886025119f, 1e-6f);          // Y10 = c*z, z = 1
    CHECK_NEAR(y[6], 0.3153915653f * 2.f, 1e-6f);    // Y20 = c*(3z^2-1), z = 1
    probes::shBasis({1.f, 0.f, 0.f}, y);
    CHECK_NEAR(y[3], 0.4886025119f, 1e-6f);          // Y11 = c*x
    CHECK_NEAR(y[8], 0.5462742153f, 1e-6f);          // Y22 = c*(x^2-y^2)
    // Y00^2 = 1/(4 pi) for any direction
    for (const fuhgl::math::Vec3 d : {fuhgl::math::Vec3{1.f, 2.f, 3.f}, fuhgl::math::Vec3{-0.3f, 0.9f, 0.1f}}) {
        const fuhgl::math::Vec3 n = fuhgl::math::normalize(d);
        probes::shBasis(n, y);
        CHECK_NEAR(y[0] * y[0], 1.f / (4.f * kPi), 1e-6f);
    }
}

void testShProjectConstant() {
    // constant radiance L over the sphere -> E(n) = pi*L for every n
    const fuhgl::math::Vec3 L{1.f, 0.5f, 0.25f};
    const int n = 512;
    std::vector<std::pair<fuhgl::math::Vec3, fuhgl::math::Vec3>> samples;
    samples.reserve(n);
    for (int k = 0; k < n; ++k) samples.emplace_back(fibSphere(k, n), L);
    probes::Sh9 c;
    probes::shProject(samples, c);
    for (const fuhgl::math::Vec3 nn : {fuhgl::math::Vec3{0.f, 1.f, 0.f}, fuhgl::math::Vec3{1.f, 0.f, 0.f},
                                       fuhgl::math::Vec3{0.f, 0.f, -1.f}, fuhgl::math::normalize(fuhgl::math::Vec3{1.f, 1.f, 1.f})}) {
        const fuhgl::math::Vec3 e = probes::shIrradiance(c, nn);
        CHECK_NEAR(e.x, kPi * L.x, 0.03f * kPi * L.x + 1e-3f);
        CHECK_NEAR(e.y, kPi * L.y, 0.03f * kPi * L.y + 1e-3f);
        CHECK_NEAR(e.z, kPi * L.z, 0.03f * kPi * L.z + 1e-3f);
    }
    // empty samples -> zero coefficients -> zero irradiance
    probes::Sh9 zero;
    probes::shProject({}, zero);
    const fuhgl::math::Vec3 e0 = probes::shIrradiance(zero, {0.f, 1.f, 0.f});
    CHECK_NEAR(e0.x, 0.f, 1e-7f);
    CHECK_NEAR(e0.y, 0.f, 1e-7f);
}

void testRayTriangle() {
    const probes::ProbeTriangle tri{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
    float t = 0.f;
    fuhgl::math::Vec3 n{};
    CHECK(probes::rayTriangle({0.25f, 0.25f, 1.f}, {0.f, 0.f, -1.f}, tri, t, n));
    CHECK_NEAR(t, 1.f, 1e-5f);
    CHECK_NEAR(std::fabs(n.z), 1.f, 1e-5f);
    CHECK(!probes::rayTriangle({0.25f, 0.25f, 1.f}, {0.f, 0.f, 1.f}, tri, t, n)); // away
    CHECK(!probes::rayTriangle({2.f, 2.f, 1.f}, {0.f, 0.f, -1.f}, tri, t, n));   // outside
}

void testGridLayout() {
    probes::LightProbeGrid g;
    CHECK(!g.create(1, 3, 3, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}));   // dims >= 2
    CHECK(!g.create(3, 3, 3, {1.f, 0.f, 0.f}, {0.f, 1.f, 1.f}));   // max > min
    CHECK(g.create(4, 3, 2, {-2.f, 0.f, -1.f}, {2.f, 3.f, 1.f}));
    CHECK(g.nx() == 4 && g.ny() == 3 && g.nz() == 2);
    CHECK(g.probeCount() == 24);
    const fuhgl::math::Vec3 c = g.cell();
    CHECK_NEAR(c.x, 4.f / 3.f, 1e-6f);
    CHECK_NEAR(c.y, 1.5f, 1e-6f);
    CHECK_NEAR(c.z, 2.f, 1e-6f);
    const fuhgl::math::Vec3 p = g.probePos(3, 2, 1);
    CHECK_NEAR(p.x, 2.f, 1e-6f);
    CHECK_NEAR(p.y, 3.f, 1e-6f);
    CHECK_NEAR(p.z, 1.f, 1e-6f);
    g.coeffs(1, 1, 1)[0] = {1.f, 2.f, 3.f};
    CHECK_NEAR(g.coeffs(1, 1, 1)[0].z, 3.f, 1e-6f);
}

void testBakeSkyOverFloor() {
    // one floor quad at y=0, constant sky radiance, no lights: the probe sees more
    // irradiance from above (open sky) than from below (floor bounce only)
    probes::LightProbeGrid g;
    CHECK(g.create(3, 3, 3, {-2.f, 0.2f, -2.f}, {2.f, 2.2f, 2.f}));
    probes::BakeInput in;
    addQuad(in.triangles, {-8.f, 0.f, -8.f}, {8.f, 0.f, -8.f}, {8.f, 0.f, 8.f}, {-8.f, 0.f, 8.f},
            {0.9f, 0.9f, 0.9f});
    in.skyRadiance = [](const fuhgl::math::Vec3&) { return fuhgl::math::Vec3{0.5f, 0.5f, 0.5f}; };
    in.raySamples = 128;
    CHECK(g.bake(in));
    const probes::Sh9& c = g.coeffs(1, 1, 1); // center probe
    const fuhgl::math::Vec3 up = probes::shIrradiance(c, {0.f, 1.f, 0.f});
    const fuhgl::math::Vec3 down = probes::shIrradiance(c, {0.f, -1.f, 0.f});
    CHECK(up.y > 1.0f);        // ~ pi * (sky + floor bounce) > pi * 0.5
    CHECK(up.y > down.y);      // open sky above, bounce only below
    CHECK(down.y > 0.f);
}

void testBakeColorBleedingAndOcclusion() {
    // closed box, only the -X wall emissive red, sky = 0: the center probe must see
    // strong red irradiance toward -X and almost none toward +X (directional SH)
    probes::LightProbeGrid g;
    CHECK(g.create(3, 3, 3, {-1.8f, -1.8f, -1.8f}, {1.8f, 1.8f, 1.8f}));
    probes::BakeInput in;
    in.triangles = makeBoxSoup(2.f, {4.f, 0.4f, 0.4f});
    in.skyRadiance = [](const fuhgl::math::Vec3&) { return fuhgl::math::Vec3{0.f, 0.f, 0.f}; };
    in.raySamples = 256;
    CHECK(g.bake(in));
    const probes::Sh9& c = g.coeffs(1, 1, 1);
    const fuhgl::math::Vec3 towardRed = probes::shIrradiance(c, {-1.f, 0.f, 0.f});
    const fuhgl::math::Vec3 awayRed = probes::shIrradiance(c, {1.f, 0.f, 0.f});
    CHECK(towardRed.x > 0.05f);                    // red wall lights the -X hemisphere
    CHECK(towardRed.x > 5.f * std::max(towardRed.z, 1e-4f)); // strongly red, not white
    CHECK(awayRed.x < 0.2f * towardRed.x);         // opposite side stays dark (occlusion)

    // same box but nothing emissive and no sky/lights: total darkness inside
    probes::LightProbeGrid g2;
    CHECK(g2.create(3, 3, 3, {-1.8f, -1.8f, -1.8f}, {1.8f, 1.8f, 1.8f}));
    probes::BakeInput in2;
    in2.triangles = makeBoxSoup(2.f, {0.f, 0.f, 0.f});
    in2.skyRadiance = [](const fuhgl::math::Vec3&) { return fuhgl::math::Vec3{0.f, 0.f, 0.f}; };
    in2.raySamples = 128;
    CHECK(g2.bake(in2));
    const fuhgl::math::Vec3 dark = probes::shIrradiance(g2.coeffs(1, 1, 1), {0.f, 1.f, 0.f});
    CHECK_NEAR(dark.x, 0.f, 1e-4f);
    CHECK_NEAR(dark.y, 0.f, 1e-4f);
    CHECK_NEAR(dark.z, 0.f, 1e-4f);
}

} // namespace

void runProbesTests() {
    testShBasis();
    testShProjectConstant();
    testRayTriangle();
    testGridLayout();
    testBakeSkyOverFloor();
    testBakeColorBleedingAndOcclusion();
}
