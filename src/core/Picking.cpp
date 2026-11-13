#include "core/Picking.h"

#include <algorithm>
#include <cmath>

namespace td {
namespace {

struct Projected {
    double x, y;
    float depth;
    bool visible;
};

Projected project(const float* p, const std::array<float, 16>& m, double width, double height) {
    const float cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    const float cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    const float cz = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
    const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    if (cw <= 1e-6f) return {0, 0, 0, false};
    return {(cx / cw * 0.5 + 0.5) * width, (1.0 - (cy / cw * 0.5 + 0.5)) * height, cz / cw, true};
}

// Separating-axis test: the rectangle's axes first, then the triangle's edges.
bool triangleTouchesRect(const Projected t[3], const ScreenRect& r) {
    const auto [minX, maxX] = std::minmax({t[0].x, t[1].x, t[2].x});
    const auto [minY, maxY] = std::minmax({t[0].y, t[1].y, t[2].y});
    if (maxX < r.left || minX > r.right || maxY < r.top || minY > r.bottom) return false;

    for (int i = 0; i < 3; ++i) {
        const Projected& a = t[i];
        const Projected& b = t[(i + 1) % 3];
        const double nx = b.y - a.y;
        const double ny = a.x - b.x;
        const auto along = [nx, ny](double x, double y) { return nx * x + ny * y; };

        const auto [triMin, triMax] = std::minmax({along(t[0].x, t[0].y), along(t[1].x, t[1].y), along(t[2].x, t[2].y)});
        const auto [rectMin, rectMax] = std::minmax({along(r.left, r.top), along(r.right, r.top), along(r.right, r.bottom), along(r.left, r.bottom)});
        if (triMax < rectMin || triMin > rectMax) return false;
    }
    return true;
}

}  // namespace

std::optional<float> pickMesh(const Mesh& mesh, const std::array<float, 16>& viewProjection, double width, double height,
                              const ScreenRect& rect) {
    std::optional<float> nearest;
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        Projected corners[3];
        bool visible = true;
        for (int i = 0; i < 3 && visible; ++i) {
            corners[i] = project(&mesh.positions[mesh.indices[t * 3 + i] * 3], viewProjection, width, height);
            visible = corners[i].visible;
        }
        if (!visible || !triangleTouchesRect(corners, rect)) continue;
        const float depth = std::min({corners[0].depth, corners[1].depth, corners[2].depth});
        if (!nearest || depth < *nearest) nearest = depth;
    }
    return nearest;
}

std::optional<RayHit> raycastMesh(const Mesh& mesh, V3 origin, V3 direction) {
    const auto vertex = [&mesh](uint32_t i) -> V3 { return {mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]}; };
    // Without some overlap, a ray landing on the seam between two triangles can miss both.
    constexpr double kEdgeTolerance = 1e-6;
    std::optional<RayHit> best;
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        // Moller-Trumbore intersection.
        const V3 a = vertex(mesh.indices[t * 3]);
        const V3 e1 = vertex(mesh.indices[t * 3 + 1]) - a;
        const V3 e2 = vertex(mesh.indices[t * 3 + 2]) - a;
        const V3 p = cross(direction, e2);
        const double det = dot(e1, p);
        if (det <= 1e-12) continue;  // edge-on or facing away
        const V3 s = origin - a;
        const double u = dot(s, p) / det;
        if (u < -kEdgeTolerance || u > 1 + kEdgeTolerance) continue;
        const V3 q = cross(s, e1);
        const double v = dot(direction, q) / det;
        if (v < -kEdgeTolerance || u + v > 1 + kEdgeTolerance) continue;
        const double distance = dot(e2, q) / det;
        if (distance <= 0 || (best && distance >= best->t)) continue;
        best = RayHit{distance, origin + direction * distance, normalized(cross(e1, e2)), int(t)};
    }
    return best;
}

}  // namespace td
