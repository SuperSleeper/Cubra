#pragma once

// The mesh is all the kernel keeps, so faces, edges and corners are recovered
// from it: creases split the surface into faces, and those creases are the edges.

#include "core/Geometry.h"
#include "core/Vec.h"

#include <vector>

namespace td {

struct TopoFace {
    std::vector<uint32_t> triangles;
    V3 normal;      // of the first triangle; the normal of the whole face if planar
    V3 centroid;    // area-weighted
    bool planar = false;
};

struct TopoEdge {
    std::vector<uint32_t> vertices;  // in order; a closed loop does not repeat the first
    bool closed = false;
    std::vector<std::array<uint32_t, 2>> segmentTriangles;  // the two triangles along each segment
    size_t segmentCount() const { return segmentTriangles.size(); }
};

struct Topology {
    std::vector<TopoFace> faces;
    std::vector<TopoEdge> edges;
    std::vector<uint32_t> corners;   // mesh vertex ids where edges meet
    std::vector<int> faceOfTriangle;
};

Topology buildTopology(const Mesh& mesh);

V3 meshVertex(const Mesh& mesh, uint32_t index);
V3 triangleNormal(const Mesh& mesh, uint32_t triangle);
V3 triangleCentroid(const Mesh& mesh, uint32_t triangle);

// The boundary of a face as closed loops of mesh vertex ids, each directed so
// the face lies on its left when seen from outside the solid.
std::vector<std::vector<uint32_t>> faceLoops(const Mesh& mesh, const TopoFace& face);

// True when p lies on the mesh surface, on either side, within tolerance.
bool pointOnMesh(const Mesh& mesh, V3 p, double tolerance);

}  // namespace td
