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

} // namespace fuhglgraphics::scene
