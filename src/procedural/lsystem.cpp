// G26 L-system implementation: deterministic string rewriting (derive) + 3D turtle
// interpretation (generate) into MeshData. The turtle keeps an orthonormal frame
// (forward, left, up); + - roll around forward, & ^ pitch around left, \ | yaw around up,
// [ ] push/pop the frame (branch depth -> radius taper), F draws a tapered cylinder,
// f moves, L emits two crossed leaf quads.

#include <fuhglgraphics/procedural/lsystem.hpp>

#include <cmath>
#include <stack>

namespace fuhglgraphics::procedural {

std::string LSystem::derive() const {
    std::string current = axiom;
    for (int it = 0; it < iterations; ++it) {
        std::string next;
        next.reserve(current.size() * 2);
        for (const char c : current) {
            bool replaced = false;
            for (const auto& r : rules)
                if (r.first == c) {
                    next += r.second;
                    replaced = true;
                    break;
                }
            if (!replaced) next += c;
        }
        current = std::move(next);
    }
    return current;
}

namespace {

struct TurtleState {
    fuhgl::math::Vec3 pos{0.f, 0.f, 0.f};
    fuhgl::math::Vec3 fwd{0.f, 1.f, 0.f};
    fuhgl::math::Vec3 left{-1.f, 0.f, 0.f};
    fuhgl::math::Vec3 up{0.f, 0.f, 1.f};
    int depth = 0;
};

void pushVertex(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& p, const fuhgl::math::Vec3& n,
                const fuhgl::math::Vec3& c) {
    md.vertices.push_back(p.x); md.vertices.push_back(p.y); md.vertices.push_back(p.z);
    md.vertices.push_back(n.x); md.vertices.push_back(n.y); md.vertices.push_back(n.z);
    md.vertices.push_back(c.x); md.vertices.push_back(c.y); md.vertices.push_back(c.z);
    md.vertices.push_back(0.f); md.vertices.push_back(0.f);
}

// tapered cylinder from p0 (radius r0) to p1 (radius r1), `sides` segments
void addCylinder(fuhgl::render::MeshData& md, const fuhgl::math::Vec3& p0, const fuhgl::math::Vec3& p1,
                 float r0, float r1, int sides, const fuhgl::math::Vec3& color) {
    const fuhgl::math::Vec3 d = p1 - p0;
    const float len = fuhgl::math::length(d);
    if (len < 1e-6f || r0 <= 1e-6f) return;
    const fuhgl::math::Vec3 axis = d / len;
    const fuhgl::math::Vec3 h = std::fabs(axis.y) < 0.9f ? fuhgl::math::Vec3{0.f, 1.f, 0.f}
                                                         : fuhgl::math::Vec3{1.f, 0.f, 0.f};
    const fuhgl::math::Vec3 u = fuhgl::math::normalize(fuhgl::math::cross(axis, h));
    const fuhgl::math::Vec3 v = fuhgl::math::cross(axis, u);
    const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
    for (int i = 0; i < sides; ++i) {
        const float a = 2.f * fuhgl::math::kPi * i / sides;
        const fuhgl::math::Vec3 n = u * std::cos(a) + v * std::sin(a);
        pushVertex(md, p0 + n * r0, n, color);
        pushVertex(md, p1 + n * r1, n, color);
    }
    for (int i = 0; i < sides; ++i) {
        const std::uint32_t a = base + 2 * i, b = base + 2 * ((i + 1) % sides);
        md.indices.push_back(a); md.indices.push_back(a + 1); md.indices.push_back(b);
        md.indices.push_back(b); md.indices.push_back(a + 1); md.indices.push_back(b + 1);
    }
}

// two crossed quads (leaf) at the turtle frame
void addLeaf(fuhgl::render::MeshData& md, const TurtleState& t, float size,
             const fuhgl::math::Vec3& color) {
    const std::uint32_t base = static_cast<std::uint32_t>(md.vertexCount());
    auto quad = [&](const fuhgl::math::Vec3& a, const fuhgl::math::Vec3& b) {
        const fuhgl::math::Vec3 n = fuhgl::math::normalize(fuhgl::math::cross(b - a, t.up));
        const fuhgl::math::Vec3 c0 = t.pos - a * size - b * size;
        const fuhgl::math::Vec3 c1 = t.pos + a * size - b * size;
        const fuhgl::math::Vec3 c2 = t.pos + a * size + b * size;
        const fuhgl::math::Vec3 c3 = t.pos - a * size + b * size;
        pushVertex(md, c0, n, color); pushVertex(md, c1, n, color);
        pushVertex(md, c2, n, color); pushVertex(md, c3, n, color);
        const std::uint32_t b0 = static_cast<std::uint32_t>(md.vertexCount()) - 4;
        md.indices.push_back(b0); md.indices.push_back(b0 + 1); md.indices.push_back(b0 + 2);
        md.indices.push_back(b0); md.indices.push_back(b0 + 2); md.indices.push_back(b0 + 3);
    };
    quad(t.left, t.up);
    quad(t.fwd, t.up);
}

} // namespace

fuhgl::render::MeshData LSystem::generate(const fuhgl::math::Vec3& trunkColor,
                                          const fuhgl::math::Vec3& leafColor) const {
    fuhgl::render::MeshData md;
    const std::string code = derive();
    if (code.empty()) return md;
    const float angle = fuhgl::math::radians(angleDeg);
    const float ca = std::cos(angle), sa = std::sin(angle);
    std::stack<TurtleState> stack;
    TurtleState t;
    for (const char c : code) {
        switch (c) {
            case 'F': {
                const float r = radius0 * std::pow(radiusTaper, static_cast<float>(t.depth));
                const fuhgl::math::Vec3 p1 = t.pos + t.fwd * step;
                addCylinder(md, t.pos, p1, r, r * 0.82f, sides, trunkColor);
                t.pos = p1;
                break;
            }
            case 'f':
                t.pos = t.pos + t.fwd * step;
                break;
            case '+': { // roll around forward
                const fuhgl::math::Vec3 l = t.left * ca + t.up * sa;
                const fuhgl::math::Vec3 u = t.up * ca - t.left * sa;
                t.left = l; t.up = u;
                break;
            }
            case '-': {
                const fuhgl::math::Vec3 l = t.left * ca - t.up * sa;
                const fuhgl::math::Vec3 u = t.up * ca + t.left * sa;
                t.left = l; t.up = u;
                break;
            }
            case '&': { // pitch around left
                const fuhgl::math::Vec3 f = t.fwd * ca + t.up * sa;
                const fuhgl::math::Vec3 u = t.up * ca - t.fwd * sa;
                t.fwd = f; t.up = u;
                break;
            }
            case '^': {
                const fuhgl::math::Vec3 f = t.fwd * ca - t.up * sa;
                const fuhgl::math::Vec3 u = t.up * ca + t.fwd * sa;
                t.fwd = f; t.up = u;
                break;
            }
            case '\\': { // yaw around up
                const fuhgl::math::Vec3 f = t.fwd * ca + t.left * sa;
                const fuhgl::math::Vec3 l = t.left * ca - t.fwd * sa;
                t.fwd = f; t.left = l;
                break;
            }
            case '|': {
                const fuhgl::math::Vec3 f = t.fwd * ca - t.left * sa;
                const fuhgl::math::Vec3 l = t.left * ca + t.fwd * sa;
                t.fwd = f; t.left = l;
                break;
            }
            case '[':
                stack.push(t);
                ++t.depth;
                break;
            case ']':
                if (!stack.empty()) {
                    t = stack.top();
                    stack.pop();
                }
                break;
            case 'L':
                if (leaves) addLeaf(md, t, leafSize, leafColor);
                break;
            default:
                break; // unknown symbols are ignored
        }
    }
    return md;
}

LSystem LSystem::makeTree(int variant, unsigned seedJitter) {
    LSystem s;
    s.iterations = 4;
    s.sides = 6;
    s.leaves = true;
    s.leafSize = 0.5f;
    s.step = 0.9f;
    s.radius0 = 0.11f;
    s.radiusTaper = 0.7f;
    const float jitter = 1.f + static_cast<float>(seedJitter % 5u) * 0.03f;
    // NOTE: the turtle grows along +Y, and '+/-' only ROLL around the heading — a rule that
    // uses just '+/-' draws every segment straight up (a stick). Real 3D branches need a
    // pitch ('&') or yaw ('\\') per branch, with '+/-' rolls in front to vary the azimuth.
    switch (variant % 3) {
        case 0: // broadleaf: trunk + two sub-branches per segment, rolled to varying azimuths
            s.axiom = "F";
            s.rules = {{'F', "FF+[+&F-FL]-[-&F+FL]"}};
            s.angleDeg = 22.5f * jitter;
            break;
        case 1: // narrow conifer: whorls of two branches pitched ~45 deg off vertical
            s.axiom = "F";
            s.rules = {{'F', "FF[&+F][&-F]"}};
            s.iterations = 5;
            s.angleDeg = 45.f * jitter;
            s.step = 0.7f;
            s.radius0 = 0.07f;
            s.radiusTaper = 0.75f;
            break;
        default: // gnarled Barnsley-style tree on X, pitched branches in 3D
            s.axiom = "X";
            s.rules = {{'X', "F[+&X]L[-&X]&LX"}, {'F', "FF"}};
            s.iterations = 5;
            s.angleDeg = 25.f * jitter;
            s.step = 0.55f;
            s.radius0 = 0.06f;
            s.radiusTaper = 0.72f;
            s.leafSize = 0.45f;
            break;
    }
    return s;
}

LSystem LSystem::makeFern() {
    LSystem s;
    s.axiom = "X";
    // classic fern frond, with '&' pitches so the frond arches sideways instead of stacking up
    s.rules = {{'X', "F[+&[X]-X]L-F[-&FX]+X"}, {'F', "FF"}};
    s.iterations = 5;
    s.angleDeg = 25.f;
    s.step = 0.16f;
    s.radius0 = 0.02f;
    s.radiusTaper = 0.8f;
    s.sides = 4;
    s.leaves = true;
    s.leafSize = 0.18f;
    return s;
}

LSystem LSystem::makeBush() {
    LSystem s;
    s.axiom = "F";
    s.rules = {{'F', "FF[+&F&F-L][-&F&FL]"}};
    s.iterations = 3;
    s.angleDeg = 35.f;
    s.step = 0.5f;
    s.radius0 = 0.06f;
    s.radiusTaper = 0.6f;
    s.sides = 5;
    s.leaves = true;
    s.leafSize = 0.38f;
    return s;
}

} // namespace fuhglgraphics::procedural
