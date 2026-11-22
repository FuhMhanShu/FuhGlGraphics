#pragma once

/// @file animation.hpp
/// @brief G6 — animation: keyframe clips (TRS channels per target node) + a simple
/// AnimationPlayer (time, loop modes, linear/slerp sampling) that drives G9 scene::Nodes,
/// plus skeletal skinning data (Skeleton with joint palette + inverse bind matrices) and
/// CPU skinning of skinned meshes (GPU matrix-palette path is the G3 extension; the
/// requirement allows CPU skinning). glTF animations/skins are parsed by the G4 loader
/// (io::GltfAnimation / io::GltfSkin) and instantiated here.
///
/// Conventions like the rest of the repo: no exceptions (bool + log where fallible),
/// plain structs + PIMPL where GL is involved, namespace fuhglgraphics::anim.

#include <fuhgl/math.hpp>
#include <fuhgl/render/mesh.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fuhglgraphics::scene {
class Node;
class Scene;
}

namespace fuhglgraphics::anim {

/// @brief Loop behavior of a playing clip.
enum class LoopMode {
    Once = 0,     ///< play once, then hold the last keyframe
    Loop = 1,     ///< wrap to the start
    PingPong = 2, ///< play forward/backward alternately
};

/// @brief One TRS animation channel: keyframes for one named node.
struct TrsChannel {
    std::string nodeName;                 ///< target node (matched against scene::Node::name)
    std::vector<float> times;             ///< seconds, ascending
    std::vector<fuhgl::math::Vec3> translations;
    std::vector<fuhgl::math::Quat> rotations;   ///< may be empty -> not animated
    std::vector<fuhgl::math::Vec3> scales;      ///< may be empty -> not animated
};

/// @brief One animation clip: named TRS channels + duration (max keyframe time).
struct AnimationClip {
    std::string name;
    float duration = 0.f;
    std::vector<TrsChannel> channels;
};

/// @brief Samples one channel at `time` (lerp / slerp via fuhgl::math::slerp; clamped at the ends).
void sampleChannel(const TrsChannel& ch, float time, fuhgl::math::Vec3& t,
                   fuhgl::math::Quat& r, fuhgl::math::Vec3& s);

/// @brief Keyframe animation player: holds clips, plays one at a time with a loop mode and
/// applies the sampled TRS to the named nodes of a scene::Scene (or an explicit
/// name -> Node map). Pure CPU — unit-testable without GL.
class AnimationPlayer {
public:
    AnimationPlayer() = default;

    /// @brief Adds a clip (replaces an existing clip with the same name).
    void addClip(const AnimationClip& clip);
    const AnimationClip* clip(const std::string& name) const;
    const std::vector<AnimationClip>& clips() const { return clips_; }

    /// @brief Starts playing `clipName` from the beginning. Returns false when unknown.
    bool play(const std::string& clipName, LoopMode loop = LoopMode::Loop, float speed = 1.f);
    void stop() { playing_ = false; time_ = 0.f; }
    bool isPlaying() const { return playing_; }
    float time() const { return time_; }
    float speed() const { return speed_; }
    LoopMode loopMode() const { return loop_; }
    const std::string& currentClip() const { return current_; }

    /// @brief Advances the player by dt (scaled by speed) — does not touch any scene.
    void update(float dt);

    /// @brief Applies the current pose to the named nodes of `scene` (nodes that do not
    /// exist are skipped). Call before Scene::fillFrame()/updateWorldTransforms().
    void applyTo(fuhglgraphics::scene::Scene& scene) const;
    /// @brief Applies to an explicit name -> node map (unit tests / non-scene use).
    void applyTo(const std::map<std::string, fuhglgraphics::scene::Node*>& nodes) const;

private:
    std::vector<AnimationClip> clips_;
    std::string current_;
    bool playing_ = false;
    float time_ = 0.f;
    float speed_ = 1.f;
    LoopMode loop_ = LoopMode::Loop;
};

// --- skeletal skinning (G6) ------------------------------------------------------------

/// @brief One skinned vertex attribute set: up to 4 joint indices + weights per vertex.
struct SkinWeights {
    std::vector<std::uint8_t> joints;   ///< 4 per vertex
    std::vector<float> weights;         ///< 4 per vertex (normalized)
};

/// @brief A skeleton: joint node indices into the scene graph + inverse bind matrices.
/// The palette is jointWorldMatrix * inverseBindMatrix per joint, computed per frame.
struct Skeleton {
    std::vector<int> jointNodes;              ///< indices into the glTF node array / scene nodes
    std::vector<fuhgl::math::Mat4> inverseBindMatrices;
    std::vector<fuhgl::math::Mat4> palette;   ///< joint palette, valid after updatePalette()

    /// @brief Computes the palette from the joint nodes' current world matrices.
    void updatePalette(const std::vector<const fuhglgraphics::scene::Node*>& jointNodePtrs);
};

/// @brief CPU skinning: deforms `base` (bind-pose MeshData) with the palette + weights into
/// `out` (same 11-floats/vertex layout: pos3 normal3 color3 uv2). Pure CPU — unit-testable.
void skinMeshCpu(const fuhgl::render::MeshData& base, const SkinWeights& skin,
                 const std::vector<fuhgl::math::Mat4>& palette,
                 fuhgl::render::MeshData& out);

} // namespace fuhglgraphics::anim
