#include "fuhglgraphics/anim/animation.hpp"

#include "fuhglgraphics/scene/node.hpp"
#include "fuhglgraphics/scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace fuhglgraphics::anim {

// --- channel sampling -------------------------------------------------------------------

void sampleChannel(const TrsChannel& ch, float time, fuhgl::math::Vec3& t, fuhgl::math::Quat& r,
                   fuhgl::math::Vec3& s) {
    const std::size_t n = ch.times.size();
    if (n == 0) return;
    auto frac = [&](std::size_t i) {
        const float t0 = ch.times[i], t1 = ch.times[std::min(i + 1, n - 1)];
        return t1 > t0 ? std::min(1.f, std::max(0.f, (time - t0) / (t1 - t0))) : 0.f;
    };
    std::size_t i = 0;
    while (i + 1 < n && ch.times[i + 1] < time) ++i;
    const float f = frac(i);
    if (!ch.translations.empty())
        t = ch.translations[i] + (ch.translations[std::min(i + 1, n - 1)] - ch.translations[i]) * f;
    if (!ch.rotations.empty())
        r = fuhgl::math::slerp(ch.rotations[i], ch.rotations[std::min(i + 1, n - 1)], f);
    if (!ch.scales.empty())
        s = ch.scales[i] + (ch.scales[std::min(i + 1, n - 1)] - ch.scales[i]) * f;
}

// --- AnimationPlayer ---------------------------------------------------------------------

void AnimationPlayer::addClip(const AnimationClip& clip) {
    for (auto& c : clips_)
        if (c.name == clip.name) {
            c = clip;
            return;
        }
    clips_.push_back(clip);
}

const AnimationClip* AnimationPlayer::clip(const std::string& name) const {
    for (const auto& c : clips_)
        if (c.name == name) return &c;
    return nullptr;
}

bool AnimationPlayer::play(const std::string& clipName, LoopMode loop, float speed) {
    if (!clip(clipName)) return false;
    current_ = clipName;
    loop_ = loop;
    speed_ = speed;
    time_ = 0.f;
    playing_ = true;
    return true;
}

void AnimationPlayer::update(float dt) {
    if (!playing_) return;
    const AnimationClip* c = clip(current_);
    if (!c || c->duration <= 0.f) return;
    time_ += dt * speed_;
    if (loop_ == LoopMode::Once) {
        if (time_ >= c->duration) {
            time_ = c->duration;
            playing_ = false;
        }
    } else if (loop_ == LoopMode::Loop) {
        while (time_ >= c->duration) time_ -= c->duration;
        if (time_ < 0.f) time_ += c->duration;
    } else { // PingPong
        if (time_ >= c->duration) {
            time_ = 2.f * c->duration - time_;
            speed_ = -speed_;
        }
        if (time_ < 0.f) {
            time_ = -time_;
            speed_ = -speed_;
        }
    }
}

void AnimationPlayer::applyTo(const std::map<std::string, scene::Node*>& nodes) const {
    const AnimationClip* c = clip(current_);
    if (!c) return;
    float t = time_;
    if (loop_ == LoopMode::PingPong && speed_ < 0.f) t = c->duration - time_; // mirrored phase
    for (const TrsChannel& ch : c->channels) {
        auto it = nodes.find(ch.nodeName);
        if (it == nodes.end() || !it->second) continue;
        fuhgl::math::Vec3 tv{0.f}, sv{1.f};
        fuhgl::math::Quat rv{};
        fuhgl::math::Vec3 t0 = it->second->translation;
        fuhgl::math::Quat r0 = it->second->rotation;
        fuhgl::math::Vec3 s0 = it->second->scale;
        sampleChannel(ch, t, tv, rv, sv);
        // channels are absolute in glTF; for hand-made clips empty vectors keep the node's value
        if (!ch.translations.empty()) t0 = tv;
        if (!ch.rotations.empty()) r0 = rv;
        if (!ch.scales.empty()) s0 = sv;
        it->second->translation = t0;
        it->second->rotation = r0;
        it->second->scale = s0;
    }
}

void AnimationPlayer::applyTo(scene::Scene& scene) const {
    std::map<std::string, scene::Node*> nodes;
    std::function<void(scene::Node*)> collect = [&](scene::Node* n) {
        nodes[n->name()] = n;
        for (const auto& ch : n->children()) collect(ch.get());
    };
    collect(&scene.root());
    applyTo(nodes);
}

// --- skeleton + CPU skinning -------------------------------------------------------------

void Skeleton::updatePalette(const std::vector<const scene::Node*>& jointNodePtrs) {
    const std::size_t n = std::min({jointNodes.size(), inverseBindMatrices.size(), jointNodePtrs.size()});
    palette.resize(jointNodes.size());
    for (std::size_t i = 0; i < n; ++i) {
        const scene::Node* jn = jointNodePtrs[i];
        if (!jn) continue;
        palette[i] = jn->worldMatrix() * inverseBindMatrices[i];
    }
}

void skinMeshCpu(const fuhgl::render::MeshData& base, const SkinWeights& skin,
                 const std::vector<fuhgl::math::Mat4>& palette, fuhgl::render::MeshData& out) {
    out.vertices = base.vertices;
    out.indices = base.indices;
    const std::size_t vcount = base.vertices.size() / 11;
    if (skin.joints.size() < vcount * 4 || skin.weights.size() < vcount * 4 || vcount == 0) return;
    for (std::size_t v = 0; v < vcount; ++v) {
        const float* in = &base.vertices[v * 11];
        fuhgl::math::Vec3 pos{in[0], in[1], in[2]};
        fuhgl::math::Vec3 nrm{in[3], in[4], in[5]};
        fuhgl::math::Vec3 sp{0.f}, sn{0.f};
        float wsum = 0.f;
        for (int k = 0; k < 4; ++k) {
            const std::uint8_t j = skin.joints[v * 4 + k];
            const float w = skin.weights[v * 4 + k];
            if (w <= 0.f || j >= palette.size()) continue;
            const fuhgl::math::Mat4& m = palette[j];
            const fuhgl::math::Vec4 p = m * fuhgl::math::Vec4{pos.x, pos.y, pos.z, 1.f};
            sp = sp + fuhgl::math::Vec3{p.x, p.y, p.z} * w;
            // normal: linear part only (approximation, fine for rigid joints)
            const fuhgl::math::Vec4 nn = m * fuhgl::math::Vec4{nrm.x, nrm.y, nrm.z, 0.f};
            sn = sn + fuhgl::math::Vec3{nn.x, nn.y, nn.z} * w;
            wsum += w;
        }
        float* o = &out.vertices[v * 11];
        if (wsum > 1e-6f) {
            o[0] = sp.x / wsum;
            o[1] = sp.y / wsum;
            o[2] = sp.z / wsum;
            const fuhgl::math::Vec3 nn = fuhgl::math::normalize(sn);
            o[3] = nn.x;
            o[4] = nn.y;
            o[5] = nn.z;
        } else {
            o[0] = in[0]; o[1] = in[1]; o[2] = in[2];
            o[3] = in[3]; o[4] = in[4]; o[5] = in[5];
        }
    }
}

} // namespace fuhglgraphics::anim
