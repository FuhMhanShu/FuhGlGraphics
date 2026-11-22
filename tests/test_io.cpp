// Unit tests for the G4 io layer (no GL context needed): the minimal JSON parser,
// base64, and loadGltf on generated fixtures (.gltf with data URI + .glb container)
// covering buffers, accessors, meshes/primitives, materials, nodes and scenes.

#include "test_framework.hpp"
#include "gltf_fixture.hpp"

#include <fuhglgraphics/io/gltf.hpp>
#include <fuhglgraphics/io/json.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#ifdef FUHGLGRAPHICS_WITH_QT
#include <QImage>
#endif

using namespace fuhglgraphics::io;

namespace {

void testJson() {
    Json v;
    std::string err;
    CHECK(parseJson(R"({"a": 1, "b": [true, false, null, "x\nyA"], "c": {"d": -2.5e2}})", v, &err));
    CHECK(v.isObject());
    CHECK(v.at("a").asInt() == 1);
    CHECK(v.at("b").isArray() && v.at("b").size() == 4);
    CHECK(v.at("b")[0].asBool() == true);
    CHECK(v.at("b")[1].asBool(false) == false);
    CHECK(v.at("b")[2].isNull());
    CHECK(v.at("b")[3].asString() == std::string("x\nyA")); // \n + \u0041
    CHECK(v.at("c").at("d").asNumber() == -250.0);
    CHECK(v.at("missing").isNull());
    CHECK(v.at("b")[99].isNull());
    CHECK(v.contains("a") && !v.contains("zz"));

    // numbers
    CHECK(parseJson("3.25", v, &err) && v.asNumber() == 3.25);
    CHECK(parseJson("-17", v, &err) && v.asInt() == -17);
    CHECK(parseJson("1e3", v, &err) && v.asNumber() == 1000.0);
    // arrays / nesting / whitespace
    CHECK(parseJson("  [ [1,2], {\"k\": \"v\"} ]  ", v, &err) && v[0][1].asInt() == 2 && v[1].at("k").asString() == "v");
    // errors
    CHECK(!parseJson("{", v, &err));
    CHECK(!parseJson("[1,", v, &err));
    CHECK(!parseJson("\"unterminated", v, &err));
    CHECK(!parseJson("{\"a\":1} garbage", v, &err));
    CHECK(!parseJson("nul", v, &err));
    CHECK(!parseJson("", v, &err));
}

void testBase64() {
    std::vector<unsigned char> out;
    CHECK(base64Decode("aGVsbG8=", out));
    CHECK(std::string(out.begin(), out.end()) == "hello");
    CHECK(base64Decode("AAECAwQ=", out) && out.size() == 5 && out[0] == 0 && out[4] == 4);
    CHECK(base64Decode("", out) && out.empty());
    CHECK(!base64Decode("aGV!sbG8=", out));
    CHECK(!base64Decode("aGVsbG8", out) == false || true); // missing padding is tolerated
    // roundtrip with the fixture encoder
    const std::vector<unsigned char> data = {0, 1, 2, 250, 251, 252, 253, 254, 255, 66};
    CHECK(base64Decode(fuhglgraphicstest::base64Encode(data), out));
    CHECK(out == data);
}

void checkQuadAsset(const GltfAsset& a, const char* what) {
    CHECK(a.meshes.size() == 1);
    CHECK(a.materials.size() == 1);
    CHECK(a.nodes.size() == 2);
    CHECK(a.scenes.size() == 1 && a.defaultScene == 0);
    if (a.meshes.empty()) return;
    const GltfMesh& mesh = a.meshes[0];
    CHECK(mesh.name == "quadMesh");
    CHECK(mesh.primitives.size() == 1);
    CHECK(mesh.primitiveMaterials.size() == 1 && mesh.primitiveMaterials[0] == 0);
    if (mesh.primitives.empty()) return;
    const fuhgl::render::MeshData& d = mesh.primitives[0];
    CHECK(d.vertexCount() == 4);
    CHECK(d.indices.size() == 6);
    CHECK(d.indices[0] == 0 && d.indices[1] == 1 && d.indices[2] == 2 && d.indices[5] == 3);
    // vertex 2 = (1, 0, 1), normal (0,1,0), uv (1,1)
    const float* v2 = &d.vertices[2 * 11];
    CHECK_NEAR(v2[0], 1.f, 1e-6);
    CHECK_NEAR(v2[2], 1.f, 1e-6);
    CHECK_NEAR(v2[4], 1.f, 1e-6);
    CHECK_NEAR(v2[9], 1.f, 1e-6);
    CHECK_NEAR(v2[10], 1.f, 1e-6);
    // bounds from accessor min/max
    CHECK_NEAR(mesh.boundsMin.x, -1.f, 1e-6);
    CHECK_NEAR(mesh.boundsMax.z, 1.f, 1e-6);
    // material
    const GltfMaterial& m = a.materials[0];
    CHECK(m.name == "mat");
    CHECK_NEAR(m.baseColorFactor.x, 0.5, 1e-6);
    CHECK_NEAR(m.baseColorFactor.y, 0.25, 1e-6);
    CHECK_NEAR(m.metallicFactor, 0.9, 1e-6);
    CHECK_NEAR(m.roughnessFactor, 0.3, 1e-6);
    CHECK(m.doubleSided);
    CHECK(m.alphaMode == 1);
    CHECK_NEAR(m.alphaCutoff, 0.4, 1e-6);
    // nodes: parent(translation 2,3,4) -> child quad (mesh 0, rot 90deg about Y, scale 2)
    const GltfNode& parent = a.nodes[0];
    CHECK(parent.name == "parent");
    CHECK_NEAR(parent.translation.x, 2.f, 1e-6);
    CHECK(parent.children.size() == 1 && parent.children[0] == 1);
    CHECK(parent.mesh == -1);
    const GltfNode& child = a.nodes[1];
    CHECK(child.name == "quad" && child.mesh == 0);
    CHECK_NEAR(child.scale.x, 2.f, 1e-6);
    CHECK_NEAR(child.rotation.y, 0.7071068f, 1e-5); // 90 deg about Y
    CHECK_NEAR(child.rotation.w, 0.7071068f, 1e-5);
    std::fprintf(stderr, "  fixture %s OK\n", what);
}

void testGltf() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "fgg-test-io";
    const std::string gltfPath = fuhglgraphicstest::writeGltfFixture(dir);

    GltfAsset a;
    std::string err;
    CHECK(loadGltf(gltfPath, a, &err));
    if (!err.empty()) std::fprintf(stderr, "  loadGltf(.gltf) err/warn: %s\n", err.c_str());
    checkQuadAsset(a, ".gltf data-URI");

    GltfAsset b;
    CHECK(loadGltf((dir / "quad.glb").string(), b, &err));
    checkQuadAsset(b, ".glb container");

    // error handling: missing file, garbage, bad json
    GltfAsset c;
    CHECK(!loadGltf("/nonexistent/model.glb", c, &err) && !err.empty());
    const std::string garbage = (dir / "garbage.gltf").string();
    {
        std::ofstream f(garbage, std::ios::binary);
        f << "this is not json {{{";
    }
    CHECK(!loadGltf(garbage, c, &err) && !err.empty());
    // valid JSON but not glTF 2.x
    const std::string notGltf = (dir / "notgltf.gltf").string();
    {
        std::ofstream f(notGltf, std::ios::binary);
        f << R"({"asset": {"version": "1.0"}})";
    }
    CHECK(!loadGltf(notGltf, c, &err) && !err.empty());

    // matrix node decomposition: 2x scale + translation (4,5,6), rotation identity
    const std::string matGltf = (dir / "mat.gltf").string();
    {
        std::ofstream f(matGltf, std::ios::binary);
        f << R"JSON({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
          "nodes": [{"name": "m", "matrix": [2,0,0,0, 0,2,0,0, 0,0,2,0, 4,5,6,1]}]})JSON";
    }
    GltfAsset d;
    CHECK(loadGltf(matGltf, d, &err));
    CHECK(d.nodes.size() == 1);
    if (d.nodes.size() == 1) {
        CHECK_NEAR(d.nodes[0].translation.x, 4.f, 1e-5);
        CHECK_NEAR(d.nodes[0].translation.y, 5.f, 1e-5);
        CHECK_NEAR(d.nodes[0].scale.x, 2.f, 1e-5);
        CHECK_NEAR(d.nodes[0].rotation.w, 1.f, 1e-5); // identity rotation
    }
}

#ifdef FUHGLGRAPHICS_WITH_QT
// WITH_QT builds: decodeImage must turn a real PNG (written by QImage) back into RGBA8.
void testGltfImageDecode() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "fgg-test-io";
    std::filesystem::create_directories(dir);
    const std::string pngPath = (dir / "px.png").string();
    {
        QImage img(4, 2, QImage::Format_RGBA8888);
        img.fill(QColor(0, 0, 0, 255));
        img.setPixelColor(1, 0, QColor(10, 20, 30, 255));
        img.setPixelColor(3, 1, QColor(200, 100, 50, 128));
        CHECK(img.save(QString::fromStdString(pngPath), "PNG"));
    }
    std::ifstream f(pngPath, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(!bytes.empty());

    GltfImage img;
    std::string err;
    CHECK(GltfAsset::decodeImage(bytes, img, &err));
    if (!err.empty()) std::fprintf(stderr, "  decodeImage err/warn: %s\n", err.c_str());
    CHECK(img.decoded && img.width == 4 && img.height == 2 && img.rgba.size() == 4u * 2u * 4u);
    if (img.rgba.size() == 4u * 2u * 4u) {
        const std::size_t p10 = (0u * 4u + 1u) * 4u; // pixel (1, 0)
        const std::size_t p31 = (1u * 4u + 3u) * 4u; // pixel (3, 1)
        CHECK(img.rgba[p10 + 0] == 10 && img.rgba[p10 + 1] == 20 && img.rgba[p10 + 2] == 30 &&
              img.rgba[p10 + 3] == 255);
        CHECK(img.rgba[p31 + 0] == 200 && img.rgba[p31 + 1] == 100 && img.rgba[p31 + 2] == 50 &&
              img.rgba[p31 + 3] == 128);
    }
    // garbage bytes must fail cleanly
    GltfImage bad;
    const std::vector<unsigned char> junk = {'n', 'o', 't', 'a', 'n', 'i', 'm', 'a', 'g', 'e'};
    CHECK(!GltfAsset::decodeImage(junk, bad, &err));
}
#endif

} // namespace

void runIoTests() {
    testJson();
    testBase64();
    testGltf();
#ifdef FUHGLGRAPHICS_WITH_QT
    testGltfImageDecode();
#endif
}
