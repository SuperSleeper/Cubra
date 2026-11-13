#include "core/Placement.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace td {
namespace {

constexpr double kDegrees = 180.0 / std::numbers::pi;
constexpr double kAxisAligned = 0.9999;

double round3(double v) { return std::round(v * 1000.0) / 1000.0; }
double round2(double v) { return std::round(v * 100.0) / 100.0; }
bool isCone(const Feature& f) { return f.shape == Shape::Cylinder && f.topDiameter >= 0; }

}  // namespace

QString paramLabel(Param param) {
    switch (param) {
        case Param::Length: return QStringLiteral("Length");
        case Param::Width: return QStringLiteral("Width");
        case Param::Height: return QStringLiteral("Height");
        case Param::Corner: return QStringLiteral("Corner");
        case Param::Radius: return QStringLiteral("Radius");
        case Param::TopRadius: return QStringLiteral("Top Radius");
        case Param::MajorRadius: return QStringLiteral("Major Radius");
        case Param::MinorRadius: return QStringLiteral("Minor Radius");
        case Param::InnerRadius: return QStringLiteral("Inner Radius");
    }
    return {};
}

std::vector<Param> hudParams(const Feature& f) {
    switch (f.shape) {
        case Shape::Box:
            if (f.corner > 0) return {Param::Length, Param::Width, Param::Height, Param::Corner};
            return {Param::Length, Param::Width, Param::Height};
        case Shape::Wedge: return {Param::Length, Param::Width, Param::Height};
        case Shape::Cylinder:
            if (isCone(f)) return {Param::Radius, Param::TopRadius, Param::Height};
            if (f.corner > 0) return {Param::Radius, Param::Height, Param::Corner};
            return {Param::Radius, Param::Height};
        case Shape::Sphere: return {Param::Radius};
        case Shape::Torus: return {Param::MajorRadius, Param::MinorRadius};
        case Shape::Star: return {Param::Radius, Param::InnerRadius, Param::Height};
        case Shape::Gear: return {Param::Radius, Param::Height};
        case Shape::Coil: return {Param::Radius, Param::Height};
        case Shape::Capsule:
        case Shape::Curve: return {Param::Radius, Param::Height};
        case Shape::Prisms:
        case Shape::Compound:
        case Shape::Mesh: return {};
    }
    return {};
}

double paramValue(const Feature& f, Param param) {
    switch (param) {
        case Param::Length: return f.width;
        case Param::Width: return f.depth;
        case Param::Height: return f.height;
        case Param::Corner: return f.corner;
        case Param::Radius: return f.diameter / 2;
        case Param::TopRadius: return std::max(f.topDiameter, 0.0) / 2;
        case Param::MajorRadius: return (f.diameter - f.tubeDiameter) / 2;
        case Param::MinorRadius: return f.tubeDiameter / 2;
        case Param::InnerRadius: return f.innerDiameter / 2;
    }
    return 0;
}

void setParamValue(Feature* f, Param param, double value) {
    switch (param) {
        case Param::Length: f->width = value; break;
        case Param::Width: f->depth = value; break;
        case Param::Height: f->height = value; break;
        case Param::Corner: f->corner = value; break;
        case Param::Radius: f->diameter = value * 2; break;
        case Param::TopRadius: f->topDiameter = value * 2; break;
        case Param::MajorRadius: f->diameter = 2 * (value + f->tubeDiameter / 2); break;
        case Param::InnerRadius: f->innerDiameter = value * 2; break;
        case Param::MinorRadius: {
            const double major = (f->diameter - f->tubeDiameter) / 2;
            f->tubeDiameter = value * 2;
            f->diameter = 2 * (major + value);
            break;
        }
    }
}

double paramMinimum(Param param, bool snapped) {
    if (param == Param::TopRadius || param == Param::Corner) return 0;
    return snapped ? 1.0 : 0.1;
}

void applyUniformScale(Feature* feature, const Feature& start, double ratio) {
    *feature = start;
    for (Param param : hudParams(start)) setParamValue(feature, param, round2(paramValue(start, param) * ratio));
}

std::vector<Handle> handlesFor(const Feature& f) {
    const V3 px{1, 0, 0}, nx{-1, 0, 0}, py{0, 1, 0}, ny{0, -1, 0}, pz{0, 0, 1};
    const auto sides = [&](Param param, V3 base) {
        return std::vector<Handle>{{param, base, px, 1, 0}, {param, base, nx, 1, 0}, {param, base, py, 1, 1}, {param, base, ny, 1, 1}};
    };

    switch (f.shape) {
        case Shape::Box:
        case Shape::Wedge: {
            // A wedge narrows to nothing at +X, so its arrows sit low and toward the tall side.
            const bool wedge = f.shape == Shape::Wedge;
            const V3 x{0, 0, wedge ? 0 : f.height / 2};
            const V3 y{wedge ? -f.width / 4 : 0, 0, wedge ? f.height / 4 : f.height / 2};
            const V3 z{wedge ? -f.width / 2 : 0, 0, 0};
            return {{Param::Length, x, px, 0.5, 0}, {Param::Length, x, nx, 0.5, 0}, {Param::Width, y, py, 0.5, 1},
                    {Param::Width, y, ny, 0.5, 1},  {Param::Height, z, pz, 1, 2}};
        }
        case Shape::Cylinder: {
            auto handles = sides(Param::Radius, V3{0, 0, isCone(f) ? 0 : f.height / 2});
            handles.push_back({Param::Height, V3{}, pz, 1, 2});
            if (isCone(f)) handles.push_back({Param::TopRadius, V3{0, 0, f.height}, px, 1, 0});
            return handles;
        }
        case Shape::Sphere: {
            auto handles = sides(Param::Radius, V3{0, 0, f.diameter / 2});
            handles.push_back({Param::Radius, V3{}, pz, 2, 2});
            return handles;
        }
        case Shape::Torus: {
            const double minor = f.tubeDiameter / 2;
            const double major = (f.diameter - f.tubeDiameter) / 2;
            // The ring arrows start one tube radius out, so their tips sit on the outer edge.
            return {{Param::MajorRadius, V3{minor, 0, minor}, px, 1, 0},
                    {Param::MajorRadius, V3{-minor, 0, minor}, nx, 1, 0},
                    {Param::MajorRadius, V3{0, minor, minor}, py, 1, 1},
                    {Param::MajorRadius, V3{0, -minor, minor}, ny, 1, 1},
                    {Param::MinorRadius, V3{major, 0, 0}, pz, 2, 2}};
        }
        case Shape::Capsule:
        case Shape::Curve:
        case Shape::Coil:
            return {{Param::Radius, V3{0, 0, f.height / 2}, V3{1, 0, 0}, 1, 0}, {Param::Height, V3{}, V3{0, 0, 1}, 1, 2}};
        case Shape::Gear:
            // The first tooth points along +X.
            return {{Param::Radius, V3{0, 0, f.height / 2}, px, 1, 0}, {Param::Height, V3{}, pz, 1, 2}};
        case Shape::Star:
            // The first tip points along +Y.
            return {{Param::Radius, V3{0, 0, f.height / 2}, py, 1, 1}, {Param::Height, V3{}, pz, 1, 2}};
        case Shape::Prisms:
        case Shape::Compound:
        case Shape::Mesh: return {};
    }
    return {};
}

V3 rotateEuler(V3 v, double rx, double ry, double rz) {
    const double ax = rx / kDegrees, ay = ry / kDegrees, az = rz / kDegrees;
    const V3 a{v.x, v.y * std::cos(ax) - v.z * std::sin(ax), v.y * std::sin(ax) + v.z * std::cos(ax)};
    const V3 b{a.x * std::cos(ay) + a.z * std::sin(ay), a.y, -a.x * std::sin(ay) + a.z * std::cos(ay)};
    return {b.x * std::cos(az) - b.y * std::sin(az), b.x * std::sin(az) + b.y * std::cos(az), b.z};
}

V3 featureDirectionToWorld(const Feature& f, V3 direction) { return rotateEuler(direction, f.rx, f.ry, f.rz); }

V3 featureToWorld(const Feature& f, V3 local) { return featureDirectionToWorld(f, local) + V3{f.x, f.y, f.z}; }

void seatOnGrid(Feature* f, double x, double y, double step) {
    f->x = step > 0 ? std::round(x / step) * step : round3(x);
    f->y = step > 0 ? std::round(y / step) * step : round3(y);
    f->z = 0;
    f->rx = f->ry = f->rz = 0;
}

void seatOnFace(Feature* f, V3 point, V3 n, double step) {
    // Rotating X then Y takes +Z to (cos a sin b, -sin a, cos a cos b).
    f->rx = round2(-std::asin(std::clamp(n.y, -1.0, 1.0)) * kDegrees);
    f->ry = round2(std::atan2(n.x, n.z) * kDegrees);
    f->rz = 0;

    // On a face square to the grid, keep the in-plane position on whole millimetres.
    const bool square = std::abs(n.x) > kAxisAligned || std::abs(n.y) > kAxisAligned || std::abs(n.z) > kAxisAligned;
    const auto place = [&](double value, double component) {
        return step > 0 && square && std::abs(component) <= kAxisAligned ? std::round(value / step) * step : round3(value);
    };
    f->x = place(point.x, n.x);
    f->y = place(point.y, n.y);
    f->z = place(point.z, n.z);
}

std::optional<double> closestOnAxis(V3 axisOrigin, V3 axisDir, V3 rayOrigin, V3 rayDir) {
    const V3 w = axisOrigin - rayOrigin;
    const double b = dot(axisDir, rayDir);
    const double denominator = 1.0 - b * b;
    if (denominator < 1e-5) return std::nullopt;
    return (b * dot(rayDir, w) - dot(axisDir, w)) / denominator;
}

Mesh makeTube(V3 a, V3 b, double radius) {
    constexpr int kSides = 6;
    const V3 dir = normalized(b - a);
    const V3 helper = std::abs(dir.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
    const V3 u = normalized(cross(dir, helper));
    const V3 v = cross(dir, u);
    Mesh mesh;
    for (int end = 0; end < 2; ++end) {
        for (int i = 0; i < kSides; ++i) {
            const double angle = 2 * std::numbers::pi * i / kSides;
            const V3 p = (end ? b : a) + u * (std::cos(angle) * radius) + v * (std::sin(angle) * radius);
            mesh.positions.insert(mesh.positions.end(), {float(p.x), float(p.y), float(p.z)});
        }
    }
    for (uint32_t i = 0; i < kSides; ++i) {
        const uint32_t j = (i + 1) % kSides;
        mesh.indices.insert(mesh.indices.end(), {i, j, j + kSides, i, j + kSides, i + kSides});
    }
    return mesh;
}

Mesh makeMarker(V3 c, double r) {
    Mesh mesh;
    for (const V3 d : {V3{1, 0, 0}, V3{-1, 0, 0}, V3{0, 1, 0}, V3{0, -1, 0}, V3{0, 0, 1}, V3{0, 0, -1}}) {
        const V3 p = c + d * r;
        mesh.positions.insert(mesh.positions.end(), {float(p.x), float(p.y), float(p.z)});
    }
    mesh.indices = {0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4, 2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5};
    return mesh;
}

Mesh makeArrow(V3 start, V3 dir, double arrowLength) {
    constexpr int kSides = 12;
    const double shaftRadius = arrowLength * 0.06;
    const double headRadius = arrowLength * 0.17;
    const double shaftLength = arrowLength * 0.62;

    const V3 helper = std::abs(dir.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
    const V3 u = normalized(cross(dir, helper));
    const V3 v = cross(dir, u);

    Mesh mesh;
    const auto push = [&mesh](V3 p) {
        mesh.positions.insert(mesh.positions.end(), {float(p.x), float(p.y), float(p.z)});
        return uint32_t(mesh.vertexCount() - 1);
    };
    const auto ringPoint = [&](int i, double along, double radius) {
        const double a = 2 * std::numbers::pi * i / kSides;
        return start + dir * along + u * (std::cos(a) * radius) + v * (std::sin(a) * radius);
    };

    const uint32_t tip = push(start + dir * arrowLength);
    const uint32_t headCentre = push(start + dir * shaftLength);
    for (int i = 0; i < kSides; ++i) {
        push(ringPoint(i, 0, shaftRadius));
        push(ringPoint(i, shaftLength, shaftRadius));
        push(ringPoint(i, shaftLength, headRadius));
    }
    const auto ring = [](int i, int layer) { return uint32_t(2 + (i % kSides) * 3 + layer); };
    for (int i = 0; i < kSides; ++i) {
        mesh.indices.insert(mesh.indices.end(), {ring(i, 0), ring(i + 1, 0), ring(i + 1, 1), ring(i, 0), ring(i + 1, 1), ring(i, 1)});
        mesh.indices.insert(mesh.indices.end(), {ring(i, 2), ring(i + 1, 2), tip, ring(i + 1, 2), ring(i, 2), headCentre});
    }
    return mesh;
}

}  // namespace td
