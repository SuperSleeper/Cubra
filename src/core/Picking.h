#pragma once

#include "core/Geometry.h"
#include "core/Vec.h"

#include <array>
#include <optional>

namespace td {

struct ScreenRect {
    double left, top, right, bottom;
};

// Tests a mesh against a rectangle in widget coordinates (y down). Returns the
// nearest depth of any triangle touching the rectangle, or nothing on a miss.
// viewProjection is column-major.
std::optional<float> pickMesh(const Mesh& mesh, const std::array<float, 16>& viewProjection, double width, double height,
                              const ScreenRect& rect);

struct RayHit {
    double t;  // distance along the unit ray
    V3 point;
    V3 normal;  // unit outward normal of the triangle that was hit
    int triangle;
};

// Nearest front-facing triangle along the ray.
std::optional<RayHit> raycastMesh(const Mesh& mesh, V3 origin, V3 direction);

}  // namespace td
