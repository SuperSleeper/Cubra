#pragma once

// Placements for copies of bodies: rows and columns, around an axis, along a
// path, or reflected in a plane. Each list leaves out the original itself.

#include "core/Affine.h"
#include "core/Document.h"
#include "core/Vec.h"

#include <vector>

namespace td {

inline constexpr int kMaxPatternCopies = 500;

struct RectangularPattern {
    V3 along{1, 0, 0};
    int countAlong = 3, countAcross = 1;          // instances, the original included
    double spacingAlong = 20, spacingAcross = 20;  // mm between neighbours, or the whole span
    bool totalSpan = false;
    bool symmetric = false;  // repeat the count out to both sides of the original
};

struct CircularPattern {
    V3 point, axis{0, 0, 1};
    int count = 6;           // instances, the original included
    bool fullCircle = true;  // spread evenly around 360 degrees, else across `angle`
    double angle = 180;
};

struct PathPattern {
    std::vector<V3> path;  // in order; a closed path does not repeat its first point
    bool closed = false;
    int count = 5;         // instances, the original included
    double distance = 0;   // how far along the path to go; 0 for all of it
    bool followPath = false;  // turn each copy to follow the path's direction
};

// Perpendicular to `along` and level with the bed where possible: the second direction of a grid.
V3 acrossDirection(V3 along);

std::vector<Affine> rectangularPattern(const RectangularPattern& pattern);
std::vector<Affine> circularPattern(const CircularPattern& pattern);
std::vector<Affine> pathPattern(const PathPattern& pattern);
Affine mirrorAcross(V3 point, V3 normal);

// Turns direction a onto direction b about the origin, the shortest way.
Affine rotationBetween(V3 a, V3 b);

// Adds a copy of each body at each placement, as new independent bodies.
// Returns the new bodies' indices.
std::vector<int> addCopies(Document* document, const std::vector<int>& bodies, const std::vector<Affine>& placements);

}  // namespace td
