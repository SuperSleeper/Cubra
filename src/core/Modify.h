#pragma once

// Direct edits, expressed as solids that are added to or cut from the design.

#include "core/Feature.h"
#include "core/Geometry.h"
#include "core/Topology.h"

#include <vector>

namespace td {

enum class ModifyKind { Chamfer, Fillet, Extrude, PointFillet };

// Where an on-screen arrow sits: its tip follows origin + dir * (scale * value).
struct ModifyGuide {
    V3 origin;
    V3 dir;
    double scale = 1;
};

// view is the direction the camera looks. An edge tool can show its arrow along
// either adjoining face, and uses whichever reads best from this angle.
ModifyGuide modifyGuide(ModifyKind kind, const Mesh& mesh, const Topology& topology, int entity, V3 view);

// True when a closed edge is an evenly divided circle, such as the rim of a
// cylinder; gives its centre, axis and radius.
bool edgeCircle(const Mesh& mesh, const TopoEdge& edge, V3* centre, V3* axis, double* radius);

// Features to append for the edit, or empty if the value does nothing.
// Convex edges and inward pushes cut; concave edges and outward pulls add.
std::vector<Feature> modifyFeatures(ModifyKind kind, const Mesh& mesh, const Topology& topology, const std::vector<int>& entities, double value);

}  // namespace td
