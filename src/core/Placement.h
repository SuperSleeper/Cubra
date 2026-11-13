#pragma once

// Rules for placing a new primitive: which dimensions it exposes, where its
// pull handles sit, and how it seats on the grid or on a face.

#include "core/Feature.h"
#include "core/Geometry.h"
#include "core/Vec.h"

#include <optional>
#include <vector>

namespace td {

enum class Param { Length, Width, Height, Corner, Radius, TopRadius, MajorRadius, MinorRadius, InnerRadius };

QString paramLabel(Param param);
std::vector<Param> hudParams(const Feature& feature);
double paramValue(const Feature& feature, Param param);
void setParamValue(Feature* feature, Param param, double value);
double paramMinimum(Param param, bool snapped);

// Scales every dimension of `start` by ratio and stores the result in *feature.
void applyUniformScale(Feature* feature, const Feature& start, double ratio);

// A pull arrow. In the feature's own coordinates its tip is at
// base + dir * (scale * paramValue), pointing along dir.
struct Handle {
    Param param;
    V3 base;
    V3 dir;
    double scale;
    int axis;  // 0 X, 1 Y, 2 Z: picks the colour
};
std::vector<Handle> handlesFor(const Feature& feature);

// Same rotation order as the geometry kernel: X, then Y, then Z, in degrees.
V3 rotateEuler(V3 v, double rx, double ry, double rz);
V3 featureToWorld(const Feature& feature, V3 local);
V3 featureDirectionToWorld(const Feature& feature, V3 direction);

// step is the grid spacing in mm, or 0 to place freely.
void seatOnGrid(Feature* feature, double x, double y, double step);
void seatOnFace(Feature* feature, V3 point, V3 normal, double step);

// Parameter along the unit axis of the point closest to the unit ray, or
// nothing when the two are nearly parallel.
std::optional<double> closestOnAxis(V3 axisOrigin, V3 axisDir, V3 rayOrigin, V3 rayDir);

Mesh makeArrow(V3 start, V3 dir, double length);
Mesh makeTube(V3 a, V3 b, double radius);
Mesh makeMarker(V3 centre, double radius);

}  // namespace td
