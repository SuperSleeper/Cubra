#include "core/Topology.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <unordered_map>

namespace td {
namespace {

constexpr double kCosCrease = 0.93;   // about 22 degrees, matching the drawn outline
constexpr double kCosPlanar = 0.9999;

struct MeshEdge {
    uint32_t a, b;
    uint32_t first;
    int second = -1;
};

uint64_t edgeKey(uint32_t a, uint32_t b) { return (uint64_t(std::min(a, b)) << 32) | std::max(a, b); }

int findRoot(std::vector<int>& parent, int x) {
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
}

}  // namespace

V3 meshVertex(const Mesh& mesh, uint32_t i) { return {mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]}; }

V3 triangleNormal(const Mesh& mesh, uint32_t t) {
    const V3 a = meshVertex(mesh, mesh.indices[t * 3]);
    return normalized(cross(meshVertex(mesh, mesh.indices[t * 3 + 1]) - a, meshVertex(mesh, mesh.indices[t * 3 + 2]) - a));
}

V3 triangleCentroid(const Mesh& mesh, uint32_t t) {
    return (meshVertex(mesh, mesh.indices[t * 3]) + meshVertex(mesh, mesh.indices[t * 3 + 1]) + meshVertex(mesh, mesh.indices[t * 3 + 2])) * (1.0 / 3.0);
}

Topology buildTopology(const Mesh& mesh) {
    Topology topology;
    const int triangles = int(mesh.triangleCount());
    if (triangles == 0) return topology;

    std::vector<V3> normals(triangles);
    for (int t = 0; t < triangles; ++t) normals[t] = triangleNormal(mesh, t);

    std::unordered_map<uint64_t, MeshEdge> edges;
    edges.reserve(size_t(triangles) * 2);
    for (int t = 0; t < triangles; ++t) {
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = mesh.indices[t * 3 + e];
            const uint32_t b = mesh.indices[t * 3 + (e + 1) % 3];
            const auto [it, inserted] = edges.try_emplace(edgeKey(a, b), MeshEdge{a, b, uint32_t(t)});
            if (!inserted) it->second.second = t;
        }
    }

    // Booleans can leave zero-area triangles whose three corners lie in a line, along
    // seams where two solids met. They have no direction of their own, and would
    // otherwise read as creases that split a face in two. Each takes its neighbour's.
    std::vector<bool> settled(triangles);
    for (int t = 0; t < triangles; ++t) settled[t] = length(normals[t]) > 0.5;
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [key, edge] : edges) {
            if (edge.second < 0) continue;
            const int a = int(edge.first), b = edge.second;
            if (settled[a] == settled[b]) continue;
            const int from = settled[a] ? a : b, to = settled[a] ? b : a;
            normals[to] = normals[from];
            settled[to] = true;
            changed = true;
        }
    }

    // Triangles joined across smooth edges belong to one face.
    // Except that a real flat area stays its own face even where a fillet blends
    // into it, so it can still be selected and pulled.
    std::vector<int> flatParent(triangles);
    std::iota(flatParent.begin(), flatParent.end(), 0);
    for (const auto& [key, edge] : edges)
        if (edge.second >= 0 && dot(normals[edge.first], normals[edge.second]) >= kCosPlanar) flatParent[findRoot(flatParent, edge.first)] = findRoot(flatParent, edge.second);

    struct FlatStats {
        int triangles = 0;
        double area = 0;
    };
    std::unordered_map<int, FlatStats> flatStats;
    for (int t = 0; t < triangles; ++t) {
        FlatStats& stats = flatStats[findRoot(flatParent, t)];
        ++stats.triangles;
        const V3 a = meshVertex(mesh, mesh.indices[t * 3]);
        stats.area += length(cross(meshVertex(mesh, mesh.indices[t * 3 + 1]) - a, meshVertex(mesh, mesh.indices[t * 3 + 2]) - a)) / 2;
    }
    // A flat patch is usually one facet of a curve, with neighbours the same size.
    // It only counts as a face of its own when it dwarfs what blends into it.
    std::unordered_map<int, double> largestBlend;
    for (const auto& [key, edge] : edges) {
        if (edge.second < 0 || dot(normals[edge.first], normals[edge.second]) < kCosCrease) continue;
        const int groupA = findRoot(flatParent, edge.first);
        const int groupB = findRoot(flatParent, edge.second);
        if (groupA == groupB) continue;
        largestBlend[groupA] = std::max(largestBlend[groupA], flatStats[groupB].area);
        largestBlend[groupB] = std::max(largestBlend[groupB], flatStats[groupA].area);
    }
    const auto isFlatArea = [&](int group) {
        const FlatStats& stats = flatStats[group];
        return stats.triangles >= 2 && stats.area >= 8 * largestBlend[group];
    };

    std::vector<int> parent(triangles);
    std::iota(parent.begin(), parent.end(), 0);
    std::vector<const MeshEdge*> sharp;
    for (const auto& [key, edge] : edges) {
        if (edge.second < 0) continue;
        if (dot(normals[edge.first], normals[edge.second]) < kCosCrease) {
            sharp.push_back(&edge);
            continue;
        }
        const int groupA = findRoot(flatParent, edge.first);
        const int groupB = findRoot(flatParent, edge.second);
        if (groupA == groupB || (!isFlatArea(groupA) && !isFlatArea(groupB))) parent[findRoot(parent, edge.first)] = findRoot(parent, edge.second);
    }

    std::unordered_map<int, int> faceIndex;
    topology.faceOfTriangle.resize(triangles);
    for (int t = 0; t < triangles; ++t) {
        const auto [it, inserted] = faceIndex.try_emplace(findRoot(parent, t), int(topology.faces.size()));
        if (inserted) topology.faces.emplace_back();
        topology.faceOfTriangle[t] = it->second;
        topology.faces[it->second].triangles.push_back(uint32_t(t));
    }
    for (TopoFace& face : topology.faces) {
        face.normal = normals[face.triangles.front()];
        face.planar = true;
        V3 weighted;
        double totalArea = 0;
        for (uint32_t t : face.triangles) {
            if (dot(normals[t], face.normal) < kCosPlanar) face.planar = false;
            const V3 a = meshVertex(mesh, mesh.indices[t * 3]);
            const double area = length(cross(meshVertex(mesh, mesh.indices[t * 3 + 1]) - a, meshVertex(mesh, mesh.indices[t * 3 + 2]) - a)) / 2;
            weighted = weighted + triangleCentroid(mesh, t) * area;
            totalArea += area;
        }
        face.centroid = weighted * (1.0 / std::max(totalArea, 1e-12));
    }

    // Join crease segments into chains that break wherever more than two meet.
    std::map<uint32_t, std::vector<int>> atVertex;
    for (int i = 0; i < int(sharp.size()); ++i) {
        atVertex[sharp[i]->a].push_back(i);
        atVertex[sharp[i]->b].push_back(i);
    }
    for (const auto& [vertex, segments] : atVertex)
        if (segments.size() != 2) topology.corners.push_back(vertex);

    // A cone's tip has no creases running into it, yet is plainly a point:
    // the surface around it turns sharply in every direction.
    constexpr double kCosTip = 0.9;
    std::vector<V3> around(mesh.vertexCount());
    for (int t = 0; t < triangles; ++t)
        for (int c = 0; c < 3; ++c) around[mesh.indices[t * 3 + c]] = around[mesh.indices[t * 3 + c]] + normals[t];
    std::vector<double> leastAligned(mesh.vertexCount(), 1.0);
    for (int t = 0; t < triangles; ++t)
        for (int c = 0; c < 3; ++c) {
            const uint32_t v = mesh.indices[t * 3 + c];
            leastAligned[v] = std::min(leastAligned[v], dot(normals[t], normalized(around[v])));
        }
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v)
        if (leastAligned[v] < kCosTip && !atVertex.count(v)) topology.corners.push_back(v);

    std::vector<bool> used(sharp.size(), false);
    const auto walk = [&](uint32_t start, int firstSegment) {
        TopoEdge edge;
        edge.vertices.push_back(start);
        uint32_t current = start;
        int segment = firstSegment;
        while (true) {
            used[segment] = true;
            const MeshEdge& e = *sharp[segment];
            edge.segmentTriangles.push_back({e.first, uint32_t(e.second)});
            const uint32_t next = e.a == current ? e.b : e.a;
            if (next == start) {
                edge.closed = true;
                break;
            }
            edge.vertices.push_back(next);
            current = next;
            const auto& around = atVertex[current];
            if (around.size() != 2) break;
            segment = around[0] == segment ? around[1] : around[0];
            if (used[segment]) break;
        }
        topology.edges.push_back(std::move(edge));
    };
    for (uint32_t corner : topology.corners)
        for (int segment : atVertex.count(corner) ? atVertex[corner] : std::vector<int>{})
            if (!used[segment]) walk(corner, segment);
    for (int i = 0; i < int(sharp.size()); ++i)
        if (!used[i]) walk(sharp[i]->a, i);

    return topology;
}

std::vector<std::vector<uint32_t>> faceLoops(const Mesh& mesh, const TopoFace& face) {
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (uint32_t t : face.triangles)
        for (int e = 0; e < 3; ++e) directed[{mesh.indices[t * 3 + e], mesh.indices[t * 3 + (e + 1) % 3]}]++;

    std::map<uint32_t, std::vector<uint32_t>> next;
    for (const auto& [edge, count] : directed)
        if (!directed.count({edge.second, edge.first})) next[edge.first].push_back(edge.second);

    std::vector<std::vector<uint32_t>> loops;
    while (!next.empty()) {
        const uint32_t start = next.begin()->first;
        std::vector<uint32_t> loop;
        uint32_t current = start;
        while (true) {
            loop.push_back(current);
            auto found = next.find(current);
            if (found == next.end()) break;
            const uint32_t following = found->second.back();
            found->second.pop_back();
            if (found->second.empty()) next.erase(found);
            current = following;
            if (current == start) break;
        }
        if (loop.size() >= 3) loops.push_back(std::move(loop));
    }
    return loops;
}

bool pointOnMesh(const Mesh& mesh, V3 p, double tolerance) {
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        const V3 a = meshVertex(mesh, mesh.indices[t * 3]);
        const V3 e1 = meshVertex(mesh, mesh.indices[t * 3 + 1]) - a;
        const V3 e2 = meshVertex(mesh, mesh.indices[t * 3 + 2]) - a;
        const V3 n = cross(e1, e2);
        const double area2 = dot(n, n);
        if (area2 < 1e-18) continue;
        const V3 w = p - a;
        if (std::abs(dot(w, n)) / std::sqrt(area2) > tolerance) continue;

        const double u = dot(cross(w, e2), n) / area2;
        const double v = dot(cross(e1, w), n) / area2;
        const double slack = tolerance / std::max(std::sqrt(std::min(dot(e1, e1), dot(e2, e2))), 1e-9);
        if (u >= -slack && v >= -slack && u + v <= 1 + slack) return true;
    }
    return false;
}

}  // namespace td
