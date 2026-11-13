#include "core/Geometry.h"

#include <QRegularExpression>

#include <manifold/manifold.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <exception>
#include <map>
#include <numbers>
#include <numeric>
#include <unordered_map>

namespace td {
namespace {

using manifold::Manifold;
using manifold::vec2;
using manifold::vec3;

constexpr int kCornerBallSegments = 64;  // a quarter turn matches a fillet's 16 arc steps
constexpr double kSliverTolerance = 1e-4;  // mm, far below anything printable

// Facet length near 0.5 mm: finer than a typical FDM nozzle can resolve.
int segmentsFor(double diameter) {
    const int wanted = static_cast<int>(std::ceil(std::numbers::pi * diameter / 0.5));
    return std::clamp(wanted, 32, 256);
}

Manifold makeBox(const Feature& f) {
    if (f.corner <= 0) return Manifold::Cube(vec3(f.width, f.depth, f.height)).Translate(vec3(-f.width / 2, -f.depth / 2, 0));

    const double r = f.corner;
    const double ax = f.width / 2 - r;
    const double ay = f.depth / 2 - r;
    const Manifold ball = Manifold::Sphere(r, segmentsFor(r * 2));
    std::vector<Manifold> corners;
    for (double sx : {-1.0, 1.0})
        for (double sy : {-1.0, 1.0})
            for (double z : {r, f.height - r}) corners.push_back(ball.Translate(vec3(sx * ax, sy * ay, z)));
    return Manifold::Hull(corners);
}

Manifold makeCylinder(const Feature& f) {
    const double bottom = f.diameter / 2;
    const double top = f.topDiameter < 0 ? bottom : f.topDiameter / 2;
    const int segments = f.sides > 0 ? f.sides : segmentsFor(std::max(f.diameter, std::max(f.topDiameter, 0.0)));
    if (f.corner > 0 && f.sides == 0 && top == bottom) {
        // Rounded rims, like a tyre: spin a rectangle with two rounded outer corners.
        const double r = f.corner;
        constexpr int kArcSteps = 8;
        manifold::SimplePolygon profile{vec2(0, 0)};
        for (int i = 0; i <= kArcSteps; ++i) {
            const double a = -std::numbers::pi / 2 + std::numbers::pi / 2 * i / kArcSteps;
            profile.push_back(vec2(bottom - r + r * std::cos(a), r + r * std::sin(a)));
        }
        for (int i = 0; i <= kArcSteps; ++i) {
            const double a = std::numbers::pi / 2 * i / kArcSteps;
            profile.push_back(vec2(bottom - r + r * std::cos(a), f.height - r + r * std::sin(a)));
        }
        profile.push_back(vec2(0, f.height));
        return Manifold::Revolve({profile}, segments);
    }
    return Manifold::Cylinder(f.height, bottom, top, segments);
}

}  // namespace

// The middle of a curve's rod at fraction t of the way along it, in the shape's own
// space: it starts at the origin heading up +Z and turns toward +X.
V3 curveSpine(const Feature& f, double t) {
    const double r0 = f.diameter / 2, r1 = (f.topDiameter < 0 ? f.diameter : f.topDiameter) / 2;
    const double length = std::max(0.0, f.height - r0 - r1);
    const double s = length * t;
    const double angle = f.bend * std::numbers::pi / 180;
    if (std::abs(angle) < 1e-9) return {0, 0, r0 + s};
    const double radius = length / angle;
    return {radius * (1 - std::cos(s / radius)), 0, r0 + radius * std::sin(s / radius)};
}

namespace {

// A round rod along an arc, thinning or thickening from one end to the other, with
// round ends: a chain of balls, each joined to the next by its hull.
Manifold makeCurve(const Feature& f) {
    const double r0 = f.diameter / 2, r1 = (f.topDiameter < 0 ? f.diameter : f.topDiameter) / 2;
    const int pieces = std::clamp(int(std::abs(f.bend) / 6) + 1, 1, 48);
    const int segments = segmentsFor(2 * std::max(r0, r1));
    std::vector<Manifold> balls;
    for (int i = 0; i <= pieces; ++i) {
        const double t = double(i) / pieces;
        const V3 c = curveSpine(f, t);
        balls.push_back(Manifold::Sphere(r0 + (r1 - r0) * t, segments).Translate(vec3(c.x, c.y, c.z)));
    }
    std::vector<Manifold> links;
    for (int i = 0; i < pieces; ++i) links.push_back(Manifold::Hull({balls[i], balls[i + 1]}));
    return Manifold::BatchBoolean(links, manifold::OpType::Add);
}

Manifold makeTorus(const Feature& f) {
    const double tube = f.tubeDiameter / 2;
    const double ring = f.diameter / 2 - tube;
    const int profileSegments = segmentsFor(f.tubeDiameter);
    manifold::SimplePolygon profile;
    profile.reserve(profileSegments);
    for (int i = 0; i < profileSegments; ++i) {
        const double a = 2 * std::numbers::pi * i / profileSegments;
        profile.push_back(vec2(ring + tube * std::cos(a), tube * std::sin(a)));
    }
    return Manifold::Revolve({profile}, segmentsFor(f.diameter)).Translate(vec3(0, 0, tube));
}

// One ring of involute teeth: the tooth form real gears use, so two gears of the
// same module mesh and roll smoothly. Tooth i is centred at phase + 2 pi i / teeth.
manifold::SimplePolygon gearOutline(int teeth, double pitchRadius, double tipRadius, double rootRadius, double phase) {
    constexpr double kPressureAngle = 20.0 * std::numbers::pi / 180.0;
    constexpr int kFlankSteps = 8;
    const double baseRadius = pitchRadius * std::cos(kPressureAngle);
    const auto involute = [](double a) { return std::tan(a) - a; };
    const double atPitch = involute(std::acos(baseRadius / pitchRadius));
    const double halfTooth = std::numbers::pi / (2.0 * teeth);
    // How far either flank sits from the tooth's centre line at radius r.
    const auto flank = [&](double r) {
        const double spread = r <= baseRadius ? 0.0 : involute(std::acos(baseRadius / r));
        return std::max(halfTooth + atPitch - spread, 1e-4);
    };
    const double flankStart = std::max(baseRadius, rootRadius);

    manifold::SimplePolygon outline;
    const auto push = [&outline](double r, double a) { outline.push_back(vec2(r * std::cos(a), r * std::sin(a))); };
    for (int i = 0; i < teeth; ++i) {
        const double centre = phase + 2 * std::numbers::pi * i / teeth;
        if (rootRadius < baseRadius) push(rootRadius, centre - flank(baseRadius));
        for (int k = 0; k <= kFlankSteps; ++k) {
            const double r = flankStart + (tipRadius - flankStart) * k / kFlankSteps;
            push(r, centre - flank(r));
        }
        push(tipRadius, centre);
        for (int k = kFlankSteps; k >= 0; --k) {
            const double r = flankStart + (tipRadius - flankStart) * k / kFlankSteps;
            push(r, centre + flank(r));
        }
        if (rootRadius < baseRadius) push(rootRadius, centre + flank(baseRadius));
        push(rootRadius, centre + std::numbers::pi / teeth);
    }
    return outline;
}

Manifold makeGear(const Feature& f) {
    const int teeth = f.sides > 0 ? f.sides : 20;
    const double module = gearModule(f);
    const double pitch = module * teeth / 2;
    if (!f.internal) {
        Manifold gear = Manifold::Extrude({gearOutline(teeth, pitch, pitch + module, pitch - 1.25 * module, 0)}, f.height);
        if (f.innerDiameter > 0) gear = gear - Manifold::Cylinder(f.height + 2, f.innerDiameter / 2, -1, segmentsFor(f.innerDiameter)).Translate(vec3(0, 0, -1));
        return gear;
    }
    // A ring gear's tooth spaces have the shape of an outside gear's teeth, with
    // the tip and root depths swapped, so the hole is cut with that outline.
    const manifold::SimplePolygon hole = gearOutline(teeth, pitch, pitch + 1.25 * module, pitch - module, std::numbers::pi / teeth);
    const Manifold rim = Manifold::Cylinder(f.height, f.diameter / 2, -1, segmentsFor(f.diameter));
    return rim - Manifold::Extrude({hole}, f.height + 2).Translate(vec3(0, 0, -1));
}

// A round wire wound into a helix: rings swept up the helix, capped at both ends.
Manifold makeCoil(const Feature& f) {
    const double wire = f.tubeDiameter / 2;
    const double radius = f.diameter / 2 - wire;  // to the wire's centre
    const double rise = (f.height - f.tubeDiameter) / f.turns;
    // At least 24 sides: each facet turns under the crease angle, so the wire shades and outlines as round.
    const int ringSides = std::clamp(segmentsFor(f.tubeDiameter) / 2, 24, 48);
    const int stepsPerTurn = std::clamp(segmentsFor(f.diameter) / 2, 24, 96);
    const int steps = std::max(2, int(std::ceil(f.turns * stepsPerTurn)));
    const double sweep = 2 * std::numbers::pi * f.turns;
    const double climb = rise / (2 * std::numbers::pi);  // height per radian

    manifold::MeshGL gl;
    gl.numProp = 3;
    const auto push = [&gl](double x, double y, double z) { gl.vertProperties.insert(gl.vertProperties.end(), {float(x), float(y), float(z)}); };
    for (int i = 0; i <= steps; ++i) {
        const double t = sweep * i / steps;
        const V3 centre{radius * std::cos(t), radius * std::sin(t), wire + climb * t};
        const V3 tangent = normalized(V3{-radius * std::sin(t), radius * std::cos(t), climb});
        const V3 inward{-std::cos(t), -std::sin(t), 0};
        const V3 normal = normalized(inward - tangent * dot(inward, tangent));
        const V3 binormal = cross(tangent, normal);
        for (int k = 0; k < ringSides; ++k) {
            const double a = 2 * std::numbers::pi * k / ringSides;
            const V3 p = centre + normal * (wire * std::cos(a)) + binormal * (wire * std::sin(a));
            push(p.x, p.y, p.z);
        }
    }
    const auto ring = [ringSides](int i, int k) { return uint32_t(i * ringSides + (k % ringSides)); };
    for (int i = 0; i < steps; ++i)
        for (int k = 0; k < ringSides; ++k)
            gl.triVerts.insert(gl.triVerts.end(), {ring(i, k), ring(i + 1, k), ring(i + 1, k + 1), ring(i, k), ring(i + 1, k + 1), ring(i, k + 1)});
    // Flat caps at both ends of the wire.
    for (int end = 0; end < 2; ++end) {
        const int i = end == 0 ? 0 : steps;
        V3 middle;
        for (int k = 0; k < ringSides; ++k)
            middle = middle + V3{gl.vertProperties[ring(i, k) * 3], gl.vertProperties[ring(i, k) * 3 + 1], gl.vertProperties[ring(i, k) * 3 + 2]};
        middle = middle * (1.0 / ringSides);
        const auto centre = uint32_t(gl.vertProperties.size() / 3);
        push(middle.x, middle.y, middle.z);
        for (int k = 0; k < ringSides; ++k) {
            // Each cap runs its rim the opposite way to the wire's side beside it, so they join up.
            if (end == 0) gl.triVerts.insert(gl.triVerts.end(), {centre, ring(i, k), ring(i, k + 1)});
            else gl.triVerts.insert(gl.triVerts.end(), {centre, ring(i, k + 1), ring(i, k)});
        }
    }
    Manifold coil(gl);
    // The winding depends on which way the rings were laid out; turn it inside out if needed.
    if (coil.Status() == Manifold::Error::NoError && coil.Volume() < 0) {
        for (size_t t = 0; t < gl.triVerts.size(); t += 3) std::swap(gl.triVerts[t + 1], gl.triVerts[t + 2]);
        coil = Manifold(gl);
    }
    return coil;
}

Manifold makeStar(const Feature& f) {
    const int points = f.sides > 0 ? f.sides : 5;
    manifold::SimplePolygon outline;
    for (int i = 0; i < points * 2; ++i) {
        // Tips and inner corners alternate, the first tip pointing along +Y.
        const double radius = (i % 2 == 0 ? f.diameter : f.innerDiameter) / 2;
        const double angle = std::numbers::pi / 2 + std::numbers::pi * i / points;
        outline.push_back(vec2(radius * std::cos(angle), radius * std::sin(angle)));
    }
    return Manifold::Extrude({outline}, f.height);
}

Manifold makeWedge(const Feature& f) {
    const double x = f.width / 2, y = f.depth / 2;
    return Manifold::Hull(std::vector<vec3>{vec3(-x, -y, 0), vec3(-x, y, 0), vec3(x, -y, 0), vec3(x, y, 0), vec3(-x, -y, f.height), vec3(-x, y, f.height)});
}

Manifold makePrisms(const Feature& f) {
    std::vector<Manifold> solids;
    solids.reserve(f.prisms.size());
    for (const Prism& p : f.prisms) {
        if (!p.hull.empty()) {
            std::vector<vec3> points;
            points.reserve(p.hull.size());
            for (const V3& h : p.hull) points.push_back(vec3(h.x, h.y, h.z));
            solids.push_back(Manifold::Hull(points) - Manifold::Sphere(p.ballRadius, kCornerBallSegments).Translate(vec3(p.origin.x, p.origin.y, p.origin.z)));
            continue;
        }
        manifold::Polygons polygons;
        for (const auto& loop : p.loops) {
            manifold::SimplePolygon polygon;
            polygon.reserve(loop.size());
            for (const auto& point : loop) polygon.push_back(vec2(point[0], point[1]));
            polygons.push_back(std::move(polygon));
        }
        const V3 w = cross(p.u, p.v);
        const manifold::mat3x4 frame(vec3(p.u.x, p.u.y, p.u.z), vec3(p.v.x, p.v.y, p.v.z), vec3(w.x, w.y, w.z), vec3(p.origin.x, p.origin.y, p.origin.z));
        solids.push_back((p.revolveSegments > 0 ? Manifold::Revolve(polygons, p.revolveSegments) : Manifold::Extrude(polygons, p.length)).Transform(frame));
    }
    return Manifold::BatchBoolean(solids, manifold::OpType::Add);
}

Manifold makeFeature(const Feature& f);

Manifold combine(const Manifold& previous, const Manifold& part, Op op) {
    switch (op) {
        case Op::Add: return previous + part;
        case Op::Subtract: return previous - part;
        case Op::Intersect: return previous ^ part;
    }
    return previous;
}

Manifold fold(const std::vector<Feature>& features) {
    Manifold solid;
    for (const Feature& f : features) solid = combine(solid, makeFeature(f), f.op);
    return solid;
}

manifold::mat3x4 toKernel(const Affine& a) {
    const auto& m = a.m;
    return manifold::mat3x4(vec3(m[0], m[1], m[2]), vec3(m[3], m[4], m[5]), vec3(m[6], m[7], m[8]), vec3(m[9], m[10], m[11]));
}

Manifold makeMesh(const Feature& f) {
    manifold::MeshGL gl;
    gl.numProp = 3;
    gl.vertProperties = f.meshPositions;
    gl.triVerts = f.meshIndices;
    return Manifold(gl);
}

Manifold makeFeature(const Feature& f) {
    Manifold m;
    switch (f.shape) {
        case Shape::Box: m = makeBox(f); break;
        case Shape::Cylinder: m = makeCylinder(f); break;
        case Shape::Sphere: m = Manifold::Sphere(f.diameter / 2, segmentsFor(f.diameter)).Translate(vec3(0, 0, f.diameter / 2)); break;
        case Shape::Torus: m = makeTorus(f); break;
        case Shape::Wedge: m = makeWedge(f); break;
        case Shape::Star: m = makeStar(f); break;
        case Shape::Gear: m = makeGear(f); break;
        case Shape::Coil: m = makeCoil(f); break;
        case Shape::Curve: m = makeCurve(f); break;
        case Shape::Capsule: {
            // Two balls and the rod between them: the bottom at z=0, the top at z=height.
            const double r = f.diameter / 2;
            const int segments = segmentsFor(f.diameter);
            m = Manifold::Hull({Manifold::Sphere(r, segments).Translate(vec3(0, 0, r)), Manifold::Sphere(r, segments).Translate(vec3(0, 0, std::max(r, f.height - r)))});
            break;
        }
        case Shape::Prisms: m = makePrisms(f); break;
        case Shape::Compound: m = fold(f.children); break;
        case Shape::Mesh: m = makeMesh(f); break;
    }
    if (f.sx != 1 || f.sy != 1 || f.sz != 1) m = m.Scale(vec3(f.sx, f.sy, f.sz));
    if (f.rx != 0 || f.ry != 0 || f.rz != 0) m = m.Rotate(f.rx, f.ry, f.rz);
    if (f.x != 0 || f.y != 0 || f.z != 0) m = m.Translate(vec3(f.x, f.y, f.z));
    return f.matrix.isIdentity() ? m : m.Transform(toKernel(f.matrix));
}

// Booleans leave zero-width sliver triangles along tangent seams, where two
// vertices land almost on top of each other. They are invisible, but each one
// would count as its own face. Merging such vertices removes the slivers.
void collapseTinyEdges(Mesh* mesh) {
    std::vector<uint32_t> parent(mesh->vertexCount());
    std::iota(parent.begin(), parent.end(), 0u);
    const auto root = [&parent](uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };

    bool merged = false;
    const float limitSquared = float(kSliverTolerance * kSliverTolerance);
    for (size_t t = 0; t < mesh->triangleCount(); ++t) {
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = mesh->indices[t * 3 + e];
            const uint32_t b = mesh->indices[t * 3 + (e + 1) % 3];
            float distance = 0;
            for (int axis = 0; axis < 3; ++axis) {
                const float d = mesh->positions[a * 3 + axis] - mesh->positions[b * 3 + axis];
                distance += d * d;
            }
            if (distance < limitSquared && root(a) != root(b)) {
                parent[root(a)] = root(b);
                merged = true;
            }
        }
    }
    if (!merged) return;

    std::vector<uint32_t> kept;
    kept.reserve(mesh->indices.size());
    for (size_t t = 0; t < mesh->triangleCount(); ++t) {
        const uint32_t a = root(mesh->indices[t * 3]);
        const uint32_t b = root(mesh->indices[t * 3 + 1]);
        const uint32_t c = root(mesh->indices[t * 3 + 2]);
        if (a != b && b != c && a != c) kept.insert(kept.end(), {a, b, c});
    }

    std::vector<uint32_t> remap(mesh->vertexCount(), UINT32_MAX);
    std::vector<float> positions;
    positions.reserve(mesh->positions.size());
    for (uint32_t& index : kept) {
        if (remap[index] == UINT32_MAX) {
            remap[index] = uint32_t(positions.size() / 3);
            positions.insert(positions.end(), mesh->positions.begin() + index * 3, mesh->positions.begin() + index * 3 + 3);
        }
        index = remap[index];
    }
    mesh->positions = std::move(positions);
    mesh->indices = std::move(kept);
}

EvalResult toResult(const Manifold& solid) {
    EvalResult result;
    if (solid.Status() != Manifold::Error::NoError) {
        result.error = QStringLiteral("the geometry kernel rejected this design (error %1)").arg(static_cast<int>(solid.Status()));
        return result;
    }
    result.ok = true;
    if (solid.IsEmpty()) return result;

    const manifold::MeshGL gl = solid.GetMeshGL();
    const size_t vertices = gl.NumVert();
    result.mesh.positions.resize(vertices * 3);
    for (size_t v = 0; v < vertices; ++v)
        for (size_t axis = 0; axis < 3; ++axis) result.mesh.positions[v * 3 + axis] = gl.vertProperties[v * gl.numProp + axis];
    result.mesh.indices.assign(gl.triVerts.begin(), gl.triVerts.end());
    collapseTinyEdges(&result.mesh);

    result.volume = solid.Volume();
    result.area = solid.SurfaceArea();
    const manifold::Box box = solid.BoundingBox();
    result.min = {box.min.x, box.min.y, box.min.z};
    result.max = {box.max.x, box.max.y, box.max.z};
    return result;
}

EvalResult failure(const QString& message) {
    EvalResult result;
    result.error = message;
    return result;
}

}  // namespace

std::vector<float> sharpEdges(const Mesh& mesh) {
    constexpr float kCosCrease = 0.93f;  // about 22 degrees
    const auto normalOf = [&mesh](size_t t) {
        const float* a = &mesh.positions[mesh.indices[t * 3] * 3];
        const float* b = &mesh.positions[mesh.indices[t * 3 + 1] * 3];
        const float* c = &mesh.positions[mesh.indices[t * 3 + 2] * 3];
        const std::array<float, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        std::array<float, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 0)
            for (float& x : n) x /= l;
        return n;
    };

    std::unordered_map<uint64_t, uint32_t> firstFace;
    std::vector<float> lines;
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = mesh.indices[t * 3 + e];
            const uint32_t b = mesh.indices[t * 3 + (e + 1) % 3];
            const uint64_t key = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
            const auto found = firstFace.find(key);
            if (found == firstFace.end()) {
                firstFace.emplace(key, uint32_t(t));
                continue;
            }
            const auto n1 = normalOf(found->second);
            const auto n2 = normalOf(t);
            // A zero-area sliver left by a boolean has no direction; it is not a crease.
            const bool sliver = n1[0] * n1[0] + n1[1] * n1[1] + n1[2] * n1[2] < 0.25f || n2[0] * n2[0] + n2[1] * n2[1] + n2[2] * n2[2] < 0.25f;
            if (!sliver && n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2] < kCosCrease) {
                lines.insert(lines.end(), mesh.positions.begin() + a * 3, mesh.positions.begin() + a * 3 + 3);
                lines.insert(lines.end(), mesh.positions.begin() + b * 3, mesh.positions.begin() + b * 3 + 3);
            }
        }
    }
    return lines;
}

EvalResult evaluate(const std::vector<Feature>& features) {
    try {
        for (const Feature& f : features) {
            QString error;
            if (!validateFeature(f, &error)) return failure(error);
        }
        return toResult(fold(features));
    } catch (const std::exception& e) {
        return failure(QString::fromUtf8(e.what()));
    }
}

struct Evaluator::State {
    std::vector<Feature> features;
    std::vector<Manifold> solids;  // solids[i] is the design through features[i]
};

Evaluator::Evaluator() : state_(std::make_shared<State>()) {}

EvalResult Evaluator::evaluate(const std::vector<Feature>& features) {
    Manifold solid;
    QString error;
    if (!settle(features, &solid, &error)) return failure(error);
    return toResult(solid);
}

bool Evaluator::settle(const std::vector<Feature>& features, void* out, QString* error) {
    Manifold& result = *static_cast<Manifold*>(out);
    try {
        size_t reused = 0;
        while (reused < features.size() && reused < state_->features.size() && sameFeature(features[reused], state_->features[reused])) ++reused;
        state_->features.resize(reused);
        state_->solids.resize(reused);

        for (size_t i = reused; i < features.size(); ++i) {
            if (!validateFeature(features[i], error)) return false;
            const Manifold previous = i == 0 ? Manifold() : state_->solids[i - 1];
            Manifold solid = combine(previous, makeFeature(features[i]), features[i].op);
            solid.Status();  // settles the lazy boolean, so reuse really is free
            state_->features.push_back(features[i]);
            state_->solids.push_back(std::move(solid));
        }
        result = features.empty() ? Manifold() : state_->solids.back();
        if (result.Status() != Manifold::Error::NoError) {
            *error = QStringLiteral("the geometry kernel rejected this design (error %1)").arg(static_cast<int>(result.Status()));
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        state_->features.clear();
        state_->solids.clear();
        *error = QString::fromUtf8(e.what());
        return false;
    }
}

struct DocumentEvaluator::State {
    std::map<int, Evaluator> bodies;  // by body id
};

DocumentEvaluator::DocumentEvaluator() : state_(std::make_shared<State>()) {}

bool DocumentEvaluator::evaluate(const Document& document, std::vector<EvalResult>* results, QString* error) {
    // Forget bodies that are gone, so their solids do not linger.
    for (auto it = state_->bodies.begin(); it != state_->bodies.end();) {
        const bool present = std::any_of(document.bodies.begin(), document.bodies.end(), [&](const Body& b) { return b.id == it->first; });
        it = present ? std::next(it) : state_->bodies.erase(it);
    }
    results->clear();
    for (const Body& body : document.bodies) {
        EvalResult result = evaluateBody(body, {});
        if (!result.ok) {
            *error = QStringLiteral("%1: %2").arg(body.name, result.error);
            return false;
        }
        results->push_back(std::move(result));
    }
    return true;
}

EvalResult DocumentEvaluator::evaluateBody(const Body& body, const std::vector<Feature>& extra) {
    std::vector<Feature> features = body.features;
    features.insert(features.end(), extra.begin(), extra.end());
    Manifold solid;
    QString error;
    if (!state_->bodies[body.id].settle(features, &solid, &error)) return failure(error);
    return toResult(body.transform.isIdentity() ? solid : solid.Transform(toKernel(body.transform)));
}

EvalResult DocumentEvaluator::combined(const Document& document) {
    try {
        std::vector<Manifold> solids;
        for (const Body& body : document.bodies) {
            Manifold solid;
            QString error;
            if (!state_->bodies[body.id].settle(body.features, &solid, &error)) return failure(QStringLiteral("%1: %2").arg(body.name, error));
            solids.push_back(body.transform.isIdentity() ? solid : solid.Transform(toKernel(body.transform)));
        }
        return toResult(solids.empty() ? Manifold() : Manifold::BatchBoolean(solids, manifold::OpType::Add));
    } catch (const std::exception& e) {
        return failure(QString::fromUtf8(e.what()));
    }
}

std::vector<Mesh> DocumentEvaluator::pieces(const Body& body) {
    std::vector<Mesh> out;
    try {
        Manifold solid;
        QString error;
        if (!state_->bodies[body.id].settle(body.features, &solid, &error)) return out;
        if (!body.transform.isIdentity()) solid = solid.Transform(toKernel(body.transform));
        for (const Manifold& piece : solid.Decompose()) {
            // Exact kernel output, so each piece rebuilds as a valid solid.
            const manifold::MeshGL gl = piece.GetMeshGL();
            Mesh mesh;
            for (size_t v = 0; v < gl.NumVert(); ++v)
                for (size_t axis = 0; axis < 3; ++axis) mesh.positions.push_back(gl.vertProperties[v * gl.numProp + axis]);
            mesh.indices.assign(gl.triVerts.begin(), gl.triVerts.end());
            out.push_back(std::move(mesh));
        }
    } catch (const std::exception&) {
        out.clear();
    }
    return out;
}

DisplayMesh buildDisplayMesh(const Mesh& mesh) {
    constexpr float kCosSmooth = 0.93f;  // matches the creases that make faces
    const size_t triangles = mesh.triangleCount();
    std::vector<std::array<float, 3>> weighted(triangles);
    std::vector<std::array<float, 3>> unit(triangles);
    for (size_t t = 0; t < triangles; ++t) {
        const float* a = &mesh.positions[mesh.indices[t * 3] * 3];
        const float* b = &mesh.positions[mesh.indices[t * 3 + 1] * 3];
        const float* c = &mesh.positions[mesh.indices[t * 3 + 2] * 3];
        const std::array<float, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        weighted[t] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const float l = std::sqrt(weighted[t][0] * weighted[t][0] + weighted[t][1] * weighted[t][1] + weighted[t][2] * weighted[t][2]);
        unit[t] = l > 0 ? std::array<float, 3>{weighted[t][0] / l, weighted[t][1] / l, weighted[t][2] / l} : std::array<float, 3>{0, 0, 1};
    }

    std::vector<std::vector<uint32_t>> around(mesh.vertexCount());
    for (size_t t = 0; t < triangles; ++t)
        for (int c = 0; c < 3; ++c) around[mesh.indices[t * 3 + c]].push_back(uint32_t(t));

    DisplayMesh out;
    out.positions.reserve(triangles * 9);
    out.normals.reserve(triangles * 9);
    for (size_t t = 0; t < triangles; ++t) {
        for (int c = 0; c < 3; ++c) {
            const uint32_t vertex = mesh.indices[t * 3 + c];
            std::array<float, 3> sum{0, 0, 0};
            for (uint32_t other : around[vertex]) {
                if (unit[t][0] * unit[other][0] + unit[t][1] * unit[other][1] + unit[t][2] * unit[other][2] < kCosSmooth) continue;
                for (int axis = 0; axis < 3; ++axis) sum[axis] += weighted[other][axis];
            }
            const float l = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
            for (int axis = 0; axis < 3; ++axis) {
                out.positions.push_back(mesh.positions[vertex * 3 + axis]);
                out.normals.push_back(l > 0 ? sum[axis] / l : unit[t][axis]);
            }
        }
    }
    return out;
}

EvalResult evaluateSingle(const Feature& feature) {
    try {
        QString error;
        if (!validateFeature(feature, &error)) return failure(error);
        return toResult(makeFeature(feature));
    } catch (const std::exception& e) {
        return failure(QString::fromUtf8(e.what()));
    }
}

bool nameHasWord(const QString& name, const QStringList& words) {
    // Split "leg_front_left", "LegFrontLeft" and "front left leg" alike.
    QString spaced;
    for (int i = 0; i < name.size(); ++i) {
        if (i > 0 && name[i].isUpper() && name[i - 1].isLower()) spaced += QLatin1Char(' ');
        spaced += name[i].toLower();
    }
    static const QRegularExpression separators(QStringLiteral("[^a-z]+"));
    for (const QString& token : spaced.split(separators, Qt::SkipEmptyParts))
        for (const QString& word : words)
            if (token == word || token == word + QLatin1Char('s') || token == word + QStringLiteral("es")) return true;
    return false;
}

std::vector<LoosePart> looseParts(const std::vector<Feature>& features) {
    struct Part {
        int feature;
        QString name;
        Manifold solid;
        manifold::Box box;
        double volume;
    };
    std::vector<Part> parts;
    for (size_t i = 0; i < features.size(); ++i) {
        const Feature& f = features[i];
        if (f.op != Op::Add) continue;
        QString error;
        if (!validateFeature(f, &error)) continue;
        Manifold solid = makeFeature(f);
        if (solid.IsEmpty()) continue;
        const QString name = f.name.isEmpty() ? QStringLiteral("%1 %2").arg(shapeName(f.shape)).arg(i + 1) : f.name;
        parts.push_back({int(i), name, solid, solid.BoundingBox(), solid.Volume()});
    }
    if (parts.size() < 2) return {};

    // Parts closer than this count as joined.
    constexpr double kTouch = 0.05;
    const auto boxesApart = [](const manifold::Box& a, const manifold::Box& b, double margin) {
        for (int k = 0; k < 3; ++k)
            if (a.min[k] > b.max[k] + margin || b.min[k] > a.max[k] + margin) return true;
        return false;
    };
    std::vector<int> group(parts.size());
    std::iota(group.begin(), group.end(), 0);
    const std::function<int(int)> root = [&](int i) { return group[i] == i ? i : group[i] = root(group[i]); };
    for (size_t a = 0; a < parts.size(); ++a)
        for (size_t b = a + 1; b < parts.size(); ++b) {
            if (root(int(a)) == root(int(b)) || boxesApart(parts[a].box, parts[b].box, kTouch)) continue;
            if (parts[a].solid.MinGap(parts[b].solid, kTouch * 2) <= kTouch) group[root(int(a))] = root(int(b));
        }

    std::map<int, double> volumes;
    for (size_t i = 0; i < parts.size(); ++i) volumes[root(int(i))] += parts[i].volume;
    const int main = std::max_element(volumes.begin(), volumes.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;

    // What a loose part belongs on, by its name: an ear goes on the head even when a leg is
    // nearer. Parts whose name says nothing join whatever is nearest.
    static const std::pair<QStringList, QStringList> kHosts[] = {
        {{"ear", "horn", "antler", "eye", "nose", "snout", "muzzle", "mouth", "jaw", "beak", "hat", "hair", "mane"}, {"head"}},
        {{"paw", "foot", "feet", "hoof", "shoe"}, {"leg"}},
        {{"hand", "finger", "claw"}, {"arm"}},
        {{"tail", "wing", "neck", "leg", "arm", "head", "wheel", "fin"}, {"body", "torso", "chassis", "hull"}},
        {{"stem", "leaf"}, {"fruit", "apple", "pear", "cherry", "banana"}},
    };
    const auto hostsFor = [](const QString& name) {
        for (const auto& [parts, hosts] : kHosts)
            if (nameHasWord(name, parts)) return hosts;
        return QStringList{};
    };
    const auto isHost = [](const QString& name, const QStringList& hosts) { return nameHasWord(name, hosts); };

    std::vector<LoosePart> loose;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (root(int(i)) == main) continue;
        LoosePart part{parts[i].name, {}, 1e9, parts[i].feature, -1};
        const QStringList hosts = hostsFor(parts[i].name);
        bool hostInMain = false;
        for (size_t j = 0; j < parts.size(); ++j) hostInMain |= root(int(j)) == main && isHost(parts[j].name, hosts);
        for (size_t j = 0; j < parts.size(); ++j) {
            if (root(int(j)) != main || (hostInMain && !isHost(parts[j].name, hosts))) continue;
            // Between bounding boxes: a fair guide to how far to move it, and instant, where an
            // exact gap over a long reach compares every triangle with every other.
            double squared = 0;
            for (int k = 0; k < 3; ++k) {
                const double apart = std::max({0.0, parts[i].box.min[k] - parts[j].box.max[k], parts[j].box.min[k] - parts[i].box.max[k]});
                squared += apart * apart;
            }
            const double gap = std::sqrt(squared);
            if (gap < part.gap) part = {parts[i].name, parts[j].name, gap, parts[i].feature, parts[j].feature};
        }
        loose.push_back(part);
    }
    return loose;
}

QStringList joinLooseParts(std::vector<Feature>* features) {
    QStringList moved;
    constexpr double kTouch = 0.05;
    for (size_t round = 0; round < features->size(); ++round) {
        const std::vector<LoosePart> loose = looseParts(*features);
        if (loose.empty()) break;
        const LoosePart& part = loose.front();
        Feature& f = (*features)[part.feature];
        const Manifold solid = makeFeature(f);
        const Manifold target = makeFeature((*features)[part.nearestFeature]);
        const manifold::Box a = solid.BoundingBox(), b = target.BoundingBox();
        const vec3 toward = (b.min + b.max) * 0.5 - (a.min + a.max) * 0.5;
        const double distance = manifold::la::length(toward);
        if (distance < 1e-6) break;
        // The least slide along that line that makes them touch...
        const auto touches = [&](double t) { return solid.Translate(toward * t).MinGap(target, kTouch * 2) <= kTouch; };
        if (!touches(1)) break;
        double low = 0, high = 1;
        for (int step = 0; step < 14; ++step) {
            const double mid = (low + high) / 2;
            (touches(mid) ? high : low) = mid;
        }
        // ...and then a fifth of the part's thickness further, so they print joined.
        const vec3 size = a.max - a.min;
        const double sink = 0.2 * std::min({size.x, size.y, size.z});
        const double t = std::min(1.0, high + sink / distance);
        f.x += toward.x * t;
        f.y += toward.y * t;
        f.z += toward.z * t;
        if (!moved.contains(part.name)) moved << part.name;
    }
    return moved;
}

}  // namespace td
