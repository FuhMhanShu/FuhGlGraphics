#include "fuhglgraphics/scene/component.hpp"

#include "fuhglgraphics/scene/node.hpp"
#include "fuhglgraphics/scene/scene.hpp"

namespace fuhglgraphics::scene {

void MeshComponent::fill(const Node& node, FrameData& frame) const {
    if (meshes.empty()) return;
    const bool instanced = node.instancing.enabled && node.instancing.count > 0;
    const std::vector<fuhgl::math::Mat4> instances =
        instanced ? node.instancing.makeLocalInstances() : std::vector<fuhgl::math::Mat4>{};
    ownedPresets_.clear();
    ownedPresets_.reserve(meshes.size()); // stable pointers for the DrawItems below
    for (const MeshEntry& e : meshes) {
        fuhgl::render::Mesh* mesh = e.mesh;
        if (!mesh) continue;
        const fuhgl::render::Material* mat = e.material;
        if (!mat) {
            // no explicit preset: sync the inline entry fields into an owned preset
            ownedPresets_.push_back(fuhgl::render::Material{});
            fuhgl::render::Material& p = ownedPresets_.back();
            p.builtinShader = true;
            p.albedo = e.albedo;
            p.emissive = e.emissive;
            p.albedoTexture = e.albedoTexture;
            p.normalMap = e.normalMap;
            p.metallic = e.metallic;
            p.roughness = e.roughness;
            p.ao = e.ao;
            mat = &p;
        }
        if (instanced) {
            mesh->setInstanceData(instances.data(), static_cast<fuhgl::gl::GLsizei>(instances.size()));
        } else if (mesh->instanceCount() > 0) {
            mesh->clearInstanceData();
        }
        frame.view.items.push_back(
            fuhgl::render::DrawItem{mesh, mat, node.worldMatrix() * e.localOffset, e.lod});
        frame.materialIds.push_back(e.materialId);
        frame.castShadow.push_back(e.castShadow);
        frame.skins.push_back(e.gpuSkin); // G14: parallel to view.items
    }
}

void ParticleEmitterComponent::fill(const Node& /*node*/, FrameData& frame) const {
    if (system.isReady()) frame.particleSystems.push_back(&system);
}

void LightProbeComponent::fill(const Node& /*node*/, FrameData& frame) const {
    if (grid.valid() && !frame.lightProbes) frame.lightProbes = &grid;
}

void DecalComponent::fill(const Node& node, FrameData& frame) const {
    render::Decal d = decal;
    d.world = node.worldMatrix();
    frame.decals.push_back(d);
}

void LightComponent::fill(const Node& node, FrameData& frame) const {
    fuhgl::render::Light l = light;
    const fuhgl::math::Mat4& w = node.worldMatrix();
    const fuhgl::math::Vec4 p = w * fuhgl::math::Vec4{l.position.x, l.position.y, l.position.z, 1.f};
    l.position = {p.x, p.y, p.z};
    const fuhgl::math::Vec4 d = w * fuhgl::math::Vec4{l.direction.x, l.direction.y, l.direction.z, 0.f};
    l.direction = fuhgl::math::normalize(fuhgl::math::Vec3{d.x, d.y, d.z});
    frame.view.lights.push_back(l);
}


void WaterComponent::fill(const Node& node, FrameData& frame) const {
    frame.view.items.push_back(fuhgl::render::DrawItem{&surface.mesh(), nullptr, node.worldMatrix()});
    frame.materialIds.push_back(0);
    frame.castShadow.push_back(false); // the water surface never casts a shadow
    if (!frame.water) frame.water = &surface; // the first water component in the graph wins
}

bool TerrainComponent::create(int gridWidth, int gridHeight, float sizeX_, float sizeZ_,
                              float heightScale_) {
    if (gridWidth < 2 || gridHeight < 2 || sizeX_ <= 0.f || sizeZ_ <= 0.f || heightScale_ <= 0.f)
        return false;
    const fuhgl::render::procedural::Image img =
        fuhgl::render::procedural::heightmap(gridWidth, gridHeight, seed, octaves, noiseScale);
    heights.assign(static_cast<std::size_t>(gridWidth) * gridHeight, 0.f);
    for (int j = 0; j < gridHeight; ++j)
        for (int i = 0; i < gridWidth; ++i)
            heights[static_cast<std::size_t>(j) * gridWidth + i] =
                img.at(i, j)[0] / 255.f;
    gridW = gridWidth;
    gridH = gridHeight;
    sizeX = sizeX_;
    sizeZ = sizeZ_;
    heightScale = heightScale_;
    lodMesh = fuhgl::render::terrainLods(heights.data(), gridW, gridH, sizeX, sizeZ, heightScale,
                                         levels, fovYDeg);
    if (lodMesh.levelCount() <= 0) return false;
    if (!customDistances.empty()) lodMesh.setDistances(customDistances);
    return true;
}

float TerrainComponent::heightAt(float x, float z) const {
    if (heights.empty() || gridW < 2 || gridH < 2) return 0.f;
    // node-local xz -> normalized uv (the terrain grid is centered at the node origin)
    const float u = x / sizeX + 0.5f;
    const float v = z / sizeZ + 0.5f;
    return fuhgl::render::heightmapSample(heights.data(), gridW, gridH, u, v) * heightScale;
}

void TerrainComponent::fill(const Node& node, FrameData& frame) const {
    if (lodMesh.levelCount() <= 0) return;
    ownedPreset_ = fuhgl::render::Material{};
    ownedPreset_.builtinShader = true;
    ownedPreset_.albedo = albedo;
    ownedPreset_.metallic = metallic;
    ownedPreset_.roughness = roughness;
    ownedPreset_.ao = ao;
    // entry.lod set -> the renderer resolves the level from the camera distance (F41)
    frame.view.items.push_back(
        fuhgl::render::DrawItem{&lodMesh.level(0), &ownedPreset_, node.worldMatrix(), &lodMesh});
    frame.materialIds.push_back(materialId);
    frame.castShadow.push_back(castShadow);
    frame.skins.push_back(nullptr); // G14: parallel to view.items
}

} // namespace fuhglgraphics::scene
