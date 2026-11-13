#include "core/Modify.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

namespace td {
namespace {

constexpr int kFilletSteps = 16;
constexpr double kPlaneOverlap = 0.01;    // keeps a cut or addition from being exactly coplanar
constexpr double kMinimalEndReach = 0.05;
constexpr double kBisectorHidden = 0.75;
constexpr double kOutlineMargin = 0.002;  // mm: far below printing, far above rounding  // beyond this the arrow is too foreshortened to grab

struct SegmentFrame {
    V3 a, b, dir;
    V3 ray1, ray2;     // along each adjoining surface, away from the edge
    V3 away1, away2;   // perpendicular to each surface, away from the wedge being shaped
    bool convex;
};

V3 inPlaneRay(V3 normal, V3 dir, V3 from, V3 centroid) {
    V3 ray = normalized(cross(normal, dir));
    if (dot(ray, centroid - from) < 0) ray = ray * -1.0;
    return ray;
}

SegmentFrame segmentFrame(const Mesh& mesh, const TopoEdge& edge, size_t segment) {
    SegmentFrame frame;
    frame.a = meshVertex(mesh, edge.vertices[segment]);
    frame.b = meshVertex(mesh, edge.vertices[(segment + 1) % edge.vertices.size()]);
    frame.dir = normalized(frame.b - frame.a);
    const uint32_t t1 = edge.segmentTriangles[segment][0];
    const uint32_t t2 = edge.segmentTriangles[segment][1];
    const V3 n1 = triangleNormal(mesh, t1);
    const V3 n2 = triangleNormal(mesh, t2);
    frame.ray1 = inPlaneRay(n1, frame.dir, frame.a, triangleCentroid(mesh, t1));
    frame.ray2 = inPlaneRay(n2, frame.dir, frame.a, triangleCentroid(mesh, t2));
    frame.convex = dot(frame.ray1, n2) < 0;
    // A convex edge is cut from outside; a concave one is filled from inside.
    frame.away1 = frame.convex ? n1 : n1 * -1.0;
    frame.away2 = frame.convex ? n2 : n2 * -1.0;
    return frame;
}

double halfAngle(const SegmentFrame& frame) { return std::acos(std::clamp(dot(frame.ray1, frame.ray2), -1.0, 1.0)) / 2; }

// Beyond an open end of an edge there may be other material, so the shaping
// solid can only run past that end if every surface there faces away from it.
bool endIsClear(const Mesh& mesh, uint32_t vertex, V3 outward) {
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        const bool touches = mesh.indices[t * 3] == vertex || mesh.indices[t * 3 + 1] == vertex || mesh.indices[t * 3 + 2] == vertex;
        if (touches && dot(triangleNormal(mesh, uint32_t(t)), outward) < -1e-4) return false;
    }
    return true;
}

// The shape to remove (or fill) in the plane across the edge, around the edge at the origin.
std::vector<V3> edgeProfile(ModifyKind kind, const SegmentFrame& f, double value, double* reach) {
    std::vector<V3> points;
    const double half = halfAngle(f);
    V3 first, second;
    if (kind == ModifyKind::Chamfer) {
        first = f.ray1 * value;
        second = f.ray2 * value;
        points = {first, second};
    } else {
        const double tangent = value / std::tan(half);
        first = f.ray1 * tangent;
        second = f.ray2 * tangent;
        const V3 middle = normalized(f.ray1 + f.ray2);
        const V3 centre = middle * (value / std::sin(half));
        const V3 fromCentre1 = first - centre;
        const V3 fromCentre2 = second - centre;
        const double sweep = std::acos(std::clamp(dot(normalized(fromCentre1), normalized(fromCentre2)), -1.0, 1.0));
        for (int i = 0; i <= kFilletSteps; ++i) {
            // Interpolating the two radius vectors keeps the arc on the edge side of the centre.
            const double s = sweep * i / kFilletSteps;
            const double weight1 = std::sin(sweep - s) / std::sin(sweep);
            const double weight2 = std::sin(s) / std::sin(sweep);
            points.push_back(centre + fromCentre1 * weight1 + fromCentre2 * weight2);
        }
    }

    const double overlap = value * 0.5 + 0.1;
    points.push_back(second + f.away2 * overlap);
    points.push_back((f.away1 + f.away2) * overlap);
    points.push_back(first + f.away1 * overlap);
    *reach = std::max(length(first), length(second)) + overlap;
    return points;
}

Prism prismFromProfile(const std::vector<V3>& profile, V3 origin, V3 dir, double prismLength) {
    Prism prism;
    const V3 helper = std::abs(dir.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
    prism.u = normalized(cross(helper, dir));
    prism.v = cross(dir, prism.u);
    prism.origin = origin;
    prism.length = prismLength;

    std::vector<std::array<double, 2>> loop;
    for (const V3& p : profile) loop.push_back({dot(p, prism.u), dot(p, prism.v)});
    double signedArea = 0;
    for (size_t i = 0; i < loop.size(); ++i) {
        const auto& p = loop[i];
        const auto& q = loop[(i + 1) % loop.size()];
        signedArea += p[0] * q[1] - q[0] * p[1];
    }
    if (signedArea < 0) std::reverse(loop.begin(), loop.end());
    prism.loops.push_back(std::move(loop));
    return prism;
}

// True when a closed edge is an evenly divided circle, as the rims of round parts are.
bool circleOf(const Mesh& mesh, const TopoEdge& edge, V3* centre, V3* axis, double* radius) {
    const size_t count = edge.vertices.size();
    if (!edge.closed || count < 8) return false;
    V3 sum;
    for (uint32_t v : edge.vertices) sum = sum + meshVertex(mesh, v);
    *centre = sum * (1.0 / double(count));
    const V3 first = meshVertex(mesh, edge.vertices[0]) - *centre;
    *radius = length(first);
    if (*radius < 1e-6) return false;
    *axis = normalized(cross(first, meshVertex(mesh, edge.vertices[1]) - *centre));

    const double tolerance = 1e-3 + *radius * 1e-3;
    const double stepCos = std::cos(2 * std::numbers::pi / double(count));
    for (size_t i = 0; i < count; ++i) {
        const V3 a = meshVertex(mesh, edge.vertices[i]) - *centre;
        const V3 b = meshVertex(mesh, edge.vertices[(i + 1) % count]) - *centre;
        if (std::abs(length(a) - *radius) > tolerance || std::abs(dot(a, *axis)) > tolerance) return false;
        if (std::abs(dot(a, b) / (length(a) * length(b)) - stepCos) > 1e-3) return false;
    }
    return true;
}

// A complete circle is one revolved cut, lined up facet for facet with the rim.
// Sweeping it piece by piece instead shatters the surface into hundreds of faces.
// Returns false when the size would pass through the axis.
bool revolvedEdge(ModifyKind kind, const Mesh& mesh, const TopoEdge& edge, V3 centre, V3 axis, double radius, double value, Prism* out, bool* adding) {
    (void)radius;
    const SegmentFrame frame = segmentFrame(mesh, edge, 0);
    const V3 middle = (frame.a + frame.b) * 0.5 - centre;
    const V3 radial = normalized(middle);
    const double apothem = length(middle);
    const double toVertexRadius = 1.0 / std::cos(std::numbers::pi / double(edge.vertices.size()));

    double reach = 0;
    std::vector<std::array<double, 2>> loop;
    for (const V3& p : edgeProfile(kind, frame, value, &reach)) {
        const double rho = (apothem + dot(p, radial)) * toVertexRadius;
        if (rho < 0) return false;
        loop.push_back({rho, dot(p, axis)});
    }
    double signedArea = 0;
    for (size_t i = 0; i < loop.size(); ++i) {
        const auto& p = loop[i];
        const auto& q = loop[(i + 1) % loop.size()];
        signedArea += p[0] * q[1] - q[0] * p[1];
    }
    if (signedArea < 0) std::reverse(loop.begin(), loop.end());

    out->origin = centre;
    out->u = normalized(meshVertex(mesh, edge.vertices[0]) - centre);
    out->v = cross(axis, out->u);
    out->length = 1;
    out->revolveSegments = int(edge.vertices.size());
    out->loops = {std::move(loop)};
    *adding = !frame.convex;
    return true;
}

// Where three filleted edges meet at an outside corner, the corner has to be a
// ball touching all three faces. Letting each edge's cut run past the corner
// instead leaves a pinched point where the three cuts cross.
std::map<uint32_t, V3> ballCorners(const Mesh& mesh, const Topology& topology, const std::vector<int>& edges, double radius, std::vector<Prism>* blocks) {
    struct Meeting {
        std::vector<V3> rays;
        std::vector<V3> normals;
        bool convex = true;
    };
    std::map<uint32_t, int> totalAtVertex;
    for (const TopoEdge& edge : topology.edges) {
        if (edge.closed) continue;
        ++totalAtVertex[edge.vertices.front()];
        ++totalAtVertex[edge.vertices.back()];
    }

    std::map<uint32_t, Meeting> meetings;
    for (int id : edges) {
        const TopoEdge& edge = topology.edges[id];
        if (edge.closed) continue;
        const auto add = [&](uint32_t vertex, const SegmentFrame& frame, V3 ray) {
            Meeting& meeting = meetings[vertex];
            meeting.rays.push_back(ray);
            meeting.convex = meeting.convex && frame.convex;
            for (V3 n : {frame.away1, frame.away2}) {
                const bool known = std::any_of(meeting.normals.begin(), meeting.normals.end(), [&](V3 m) { return dot(m, n) > 0.999; });
                if (!known) meeting.normals.push_back(n);
            }
        };
        const SegmentFrame first = segmentFrame(mesh, edge, 0);
        const SegmentFrame last = segmentFrame(mesh, edge, edge.segmentCount() - 1);
        add(edge.vertices.front(), first, first.dir);
        add(edge.vertices.back(), last, last.dir * -1.0);
    }

    std::map<uint32_t, V3> centres;
    for (const auto& [vertex, meeting] : meetings) {
        if (meeting.rays.size() != 3 || meeting.normals.size() != 3 || !meeting.convex || totalAtVertex[vertex] != 3) continue;
        const V3 n1 = meeting.normals[0], n2 = meeting.normals[1], n3 = meeting.normals[2];
        const double det = dot(n1, cross(n2, n3));
        if (std::abs(det) < 1e-6) continue;

        // The point one radius inside all three faces.
        const V3 point = meshVertex(mesh, vertex);
        const V3 centre = point + (cross(n2, n3) + cross(n3, n1) + cross(n1, n2)) * (-radius / det);
        centres[vertex] = centre;

        // The block spans out from the ball's centre past the corner, bounded by
        // planes square to each edge, which is exactly where each edge's cut ends.
        const V3 w1 = meeting.rays[0] * -1.0, w2 = meeting.rays[1] * -1.0, w3 = meeting.rays[2] * -1.0;
        const auto aimed = [](V3 v, V3 toward) { return normalized(dot(v, toward) < 0 ? v * -1.0 : v); };
        const V3 r1 = aimed(cross(w2, w3), w1), r2 = aimed(cross(w3, w1), w2), r3 = aimed(cross(w1, w2), w3);
        const double span = 3 * (length(point - centre) + radius);

        Prism block;
        block.origin = centre;
        block.length = 1;
        block.ballRadius = radius;
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b)
                for (int c = 0; c < 2; ++c) block.hull.push_back(centre + (r1 * a + r2 * b + r3 * c) * span);
        blocks->push_back(std::move(block));
    }
    return centres;
}

// A smooth curved edge, such as where a torus or sphere breaks through a flat face,
// is rounded by one solid swept along the whole edge. Cutting each short segment on
// its own follows every facet's tilt, and the pieces overlap into a saw-tooth rim.
// Returns false when the edge turns sharply, changes faces or ends at a ball corner,
// so the caller cuts it piece by piece.
constexpr double kSmoothTurn = 35.0 * std::numbers::pi / 180.0;

bool sweptEdge(ModifyKind kind, const Mesh& mesh, const Topology& topology, const TopoEdge& edge, const std::map<uint32_t, V3>& balls, double value,
               Feature* out, bool* adding) {
    const size_t count = edge.segmentCount();
    const size_t vertexCount = edge.vertices.size();
    if (count < 3 || vertexCount != (edge.closed ? count : count + 1)) return false;

    // The same face must stay on the same side all along.
    std::vector<std::array<uint32_t, 2>> sides = edge.segmentTriangles;
    const int faceA = topology.faceOfTriangle[sides[0][0]];
    const int faceB = topology.faceOfTriangle[sides[0][1]];
    if (faceA == faceB) return false;
    for (auto& pair : sides) {
        if (topology.faceOfTriangle[pair[0]] == faceB && topology.faceOfTriangle[pair[1]] == faceA) std::swap(pair[0], pair[1]);
        if (topology.faceOfTriangle[pair[0]] != faceA || topology.faceOfTriangle[pair[1]] != faceB) return false;
    }
    std::vector<V3> dirs(count);
    for (size_t s = 0; s < count; ++s) dirs[s] = normalized(meshVertex(mesh, edge.vertices[(s + 1) % vertexCount]) - meshVertex(mesh, edge.vertices[s]));
    for (size_t s = 0; s + 1 < count + (edge.closed ? 1 : 0); ++s)
        if (std::acos(std::clamp(dot(dirs[s], dirs[(s + 1) % count]), -1.0, 1.0)) > kSmoothTurn) return false;
    if (!edge.closed && (balls.count(edge.vertices.front()) || balls.count(edge.vertices.back()))) return false;

    // Each face's direction at each vertex: the angle-weighted normal of its triangles
    // there, which smooths over the facets of a round surface.
    std::map<uint32_t, size_t> slot;
    for (size_t i = 0; i < vertexCount; ++i) slot[edge.vertices[i]] = i;
    std::vector<V3> normalA(vertexCount), normalB(vertexCount);
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        const int face = topology.faceOfTriangle[t];
        if (face != faceA && face != faceB) continue;
        for (int corner = 0; corner < 3; ++corner) {
            const auto found = slot.find(mesh.indices[t * 3 + corner]);
            if (found == slot.end()) continue;
            const V3 p = meshVertex(mesh, mesh.indices[t * 3 + corner]);
            const V3 e1 = normalized(meshVertex(mesh, mesh.indices[t * 3 + (corner + 1) % 3]) - p);
            const V3 e2 = normalized(meshVertex(mesh, mesh.indices[t * 3 + (corner + 2) % 3]) - p);
            const double angle = std::acos(std::clamp(dot(e1, e2), -1.0, 1.0));
            const V3 n = triangleNormal(mesh, uint32_t(t)) * angle;
            (face == faceA ? normalA : normalB)[found->second] = (face == faceA ? normalA : normalB)[found->second] + n;
        }
    }

    // One profile per vertex, across the edge's direction there.
    std::vector<std::vector<V3>> rings;
    std::vector<V3> tangents;
    double reach = 0;
    bool convex = false;
    const SegmentFrame start = segmentFrame(mesh, edge, 0);
    V3 previousRay1 = start.ray1, previousRay2 = start.ray2;
    if (topology.faceOfTriangle[edge.segmentTriangles[0][0]] != faceA) std::swap(previousRay1, previousRay2);
    for (size_t i = 0; i < vertexCount; ++i) {
        V3 tangent;
        if (edge.closed) tangent = normalized(dirs[(i + count - 1) % count] + dirs[i]);
        else tangent = i == 0 ? dirs[0] : i == count ? dirs[count - 1] : normalized(dirs[i - 1] + dirs[i]);
        if (length(normalA[i]) < 1e-9 || length(normalB[i]) < 1e-9) return false;
        const V3 nA = normalized(normalA[i]), nB = normalized(normalB[i]);
        SegmentFrame frame;
        frame.a = meshVertex(mesh, edge.vertices[i]);
        frame.dir = tangent;
        // Which way is into each face: as at the previous vertex, starting from the
        // triangles along the first segment. Nearby triangle centres can mislead on a
        // finely faceted surface and flip the profile.
        frame.ray1 = normalized(cross(nA, tangent));
        frame.ray2 = normalized(cross(nB, tangent));
        if (dot(frame.ray1, previousRay1) < 0) frame.ray1 = frame.ray1 * -1.0;
        if (dot(frame.ray2, previousRay2) < 0) frame.ray2 = frame.ray2 * -1.0;
        previousRay1 = frame.ray1;
        previousRay2 = frame.ray2;
        frame.convex = dot(frame.ray1, nB) < 0;
        if (i > 0 && frame.convex != convex) return false;
        convex = frame.convex;
        frame.away1 = frame.convex ? nA : nA * -1.0;
        frame.away2 = frame.convex ? nB : nB * -1.0;
        if (halfAngle(frame) < 1e-3) return false;  // the faces meet flat: nothing to round
        double ringReach = 0;
        std::vector<V3> ring;
        for (const V3& p : edgeProfile(kind, frame, value, &ringReach)) ring.push_back(frame.a + p);
        reach = std::max(reach, ringReach);
        rings.push_back(std::move(ring));
        tangents.push_back(tangent);
    }
    *adding = !convex;

    // Open ends run on past the edge where nothing is in the way, as the piecewise cut does.
    if (!edge.closed) {
        const auto endReach = [&](uint32_t vertex, V3 outward) {
            if (*adding) return kMinimalEndReach;
            return endIsClear(mesh, vertex, outward) ? reach : kMinimalEndReach;
        };
        const double before = endReach(edge.vertices.front(), tangents.front() * -1.0);
        const double after = endReach(edge.vertices.back(), tangents.back());
        std::vector<V3> first = rings.front(), last = rings.back();
        for (V3& p : first) p = p - tangents.front() * before;
        for (V3& p : last) p = p + tangents.back() * after;
        rings.insert(rings.begin(), std::move(first));
        rings.push_back(std::move(last));
        tangents.insert(tangents.begin(), tangents.front());
        tangents.push_back(tangents.back());
    }

    // Wind every profile anticlockwise about the edge's direction, so the walls face out.
    const size_t P = rings[0].size();
    {
        const V3 w = tangents[0];
        const V3 helper = std::abs(w.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
        const V3 u = normalized(cross(helper, w)), v = cross(w, u);
        double area = 0;
        for (size_t j = 0; j < P; ++j) {
            const V3 p = rings[0][j], q = rings[0][(j + 1) % P];
            area += dot(p, u) * dot(q, v) - dot(q, u) * dot(p, v);
        }
        if (area < 0)
            for (auto& ring : rings) std::reverse(ring.begin(), ring.end());
    }

    out->shape = Shape::Mesh;
    out->meshPositions.clear();
    out->meshIndices.clear();
    for (const auto& ring : rings)
        for (const V3& p : ring) {
            out->meshPositions.push_back(float(p.x));
            out->meshPositions.push_back(float(p.y));
            out->meshPositions.push_back(float(p.z));
        }
    const size_t ringCount = rings.size();
    const auto at = [&](size_t ring, size_t j) { return uint32_t(ring * P + j); };
    for (size_t i = 0; i < (edge.closed ? ringCount : ringCount - 1); ++i) {
        const size_t next = (i + 1) % ringCount;
        for (size_t j = 0; j < P; ++j) {
            const size_t k = (j + 1) % P;
            for (uint32_t index : {at(i, j), at(i, k), at(next, k), at(i, j), at(next, k), at(next, j)}) out->meshIndices.push_back(index);
        }
    }
    if (!edge.closed) {
        // Each end is a fan around the profile's mean: the profile is star-shaped from there.
        for (int end = 0; end < 2; ++end) {
            const size_t ring = end == 0 ? 0 : ringCount - 1;
            V3 mean;
            for (const V3& p : rings[ring]) mean = mean + p;
            mean = mean * (1.0 / double(P));
            const uint32_t centre = uint32_t(out->meshPositions.size() / 3);
            out->meshPositions.push_back(float(mean.x));
            out->meshPositions.push_back(float(mean.y));
            out->meshPositions.push_back(float(mean.z));
            for (size_t j = 0; j < P; ++j) {
                const size_t k = (j + 1) % P;
                if (end == 0) for (uint32_t index : {centre, at(ring, k), at(ring, j)}) out->meshIndices.push_back(index);
                else for (uint32_t index : {centre, at(ring, j), at(ring, k)}) out->meshIndices.push_back(index);
            }
        }
    }
    const EvalResult check = evaluateSingle(*out);
    return check.ok && check.volume > 0;
}

std::vector<Feature> edgeFeatures(ModifyKind kind, const Mesh& mesh, const Topology& topology, const std::vector<int>& edges, double value) {
    std::vector<Prism> cuts, fills;
    std::vector<Feature> sweptCuts, sweptFills;
    std::map<uint32_t, V3> balls;
    if (kind == ModifyKind::Fillet) balls = ballCorners(mesh, topology, edges, value, &cuts);
    for (int id : edges) {
        const TopoEdge& edge = topology.edges[id];
        V3 centre, axis;
        double radius = 0;
        if (circleOf(mesh, edge, &centre, &axis, &radius)) {
            Prism ring;
            bool adding = false;
            if (!revolvedEdge(kind, mesh, edge, centre, axis, radius, value, &ring, &adding)) return {};
            (adding ? fills : cuts).push_back(std::move(ring));
            continue;
        }
        Feature swept;
        bool sweptAdds = false;
        if (sweptEdge(kind, mesh, topology, edge, balls, value, &swept, &sweptAdds)) {
            swept.op = sweptAdds ? Op::Add : Op::Subtract;
            (sweptAdds ? sweptFills : sweptCuts).push_back(std::move(swept));
            continue;
        }
        const size_t count = edge.segmentCount();
        for (size_t s = 0; s < count; ++s) {
            const SegmentFrame frame = segmentFrame(mesh, edge, s);
            double reach = 0;
            const bool adding = !frame.convex;
            const std::vector<V3> profile = edgeProfile(kind, frame, value, &reach);

            // Neighbouring pieces of a curved edge must overlap on the outside of the bend.
            const auto reachAt = [&](bool atStart) {
                const bool isChainEnd = !edge.closed && (atStart ? s == 0 : s + 1 == count);
                if (isChainEnd) {
                    const uint32_t endVertex = atStart ? edge.vertices[s] : edge.vertices[s + 1];
                    const auto ball = balls.find(endVertex);
                    if (ball != balls.end()) {
                        // Stop level with the ball's centre, where the two shapes match. The
                        // sliver of overlap is harmless, as the ball sits inside this cut's curve.
                        return dot(ball->second - meshVertex(mesh, endVertex), atStart ? frame.dir * -1.0 : frame.dir) + kPlaneOverlap;
                    }
                    if (adding) return kMinimalEndReach;  // extra material would hang off into the air
                    const uint32_t vertex = atStart ? edge.vertices[s] : edge.vertices[s + 1];
                    return endIsClear(mesh, vertex, atStart ? frame.dir * -1.0 : frame.dir) ? reach : kMinimalEndReach;
                }
                const size_t other = atStart ? (s + count - 1) % count : (s + 1) % count;
                const V3 otherDir = segmentFrame(mesh, edge, other).dir;
                const double turn = std::acos(std::clamp(dot(frame.dir, otherDir), -1.0, 1.0));
                return reach * std::tan(turn / 2) * 1.2 + 0.01;
            };
            const double before = reachAt(true);
            const double after = reachAt(false);
            const double span = length(frame.b - frame.a) + before + after;
            if (span <= 0) return {};  // the radius is too big for this edge
            Prism prism = prismFromProfile(profile, frame.a - frame.dir * before, frame.dir, span);
            (adding ? fills : cuts).push_back(std::move(prism));
        }
    }

    std::vector<Feature> features;
    const auto push = [&](std::vector<Prism>& prisms, Op op) {
        if (prisms.empty()) return;
        Feature feature;
        feature.shape = Shape::Prisms;
        feature.op = op;
        feature.prisms = std::move(prisms);
        features.push_back(std::move(feature));
    };
    push(cuts, Op::Subtract);
    for (Feature& f : sweptCuts) features.push_back(std::move(f));
    push(fills, Op::Add);
    for (Feature& f : sweptFills) features.push_back(std::move(f));
    return features;
}

struct PointFrame {
    V3 point;
    V3 out;                // straight out of the solid
    std::vector<V3> rays;  // along the creases into the point, or the surface if it has none
    double spread = 0;     // angle from the inward axis to the widest ray
    bool usable = false;
};

PointFrame pointFrame(const Mesh& mesh, const Topology& topology, int corner) {
    PointFrame frame;
    const uint32_t vertex = topology.corners[corner];
    frame.point = meshVertex(mesh, vertex);

    std::vector<V3> surfaceRays;
    V3 weighted;
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        for (int c = 0; c < 3; ++c) {
            if (mesh.indices[t * 3 + c] != vertex) continue;
            const V3 a = normalized(meshVertex(mesh, mesh.indices[t * 3 + (c + 1) % 3]) - frame.point);
            const V3 b = normalized(meshVertex(mesh, mesh.indices[t * 3 + (c + 2) % 3]) - frame.point);
            weighted = weighted + triangleNormal(mesh, uint32_t(t)) * std::acos(std::clamp(dot(a, b), -1.0, 1.0));
            surfaceRays.push_back(a);
            surfaceRays.push_back(b);
        }
    }
    frame.out = normalized(weighted);

    for (const TopoEdge& edge : topology.edges) {
        if (edge.closed) continue;
        if (edge.vertices.front() == vertex) frame.rays.push_back(normalized(meshVertex(mesh, edge.vertices[1]) - frame.point));
        if (edge.vertices.back() == vertex) frame.rays.push_back(normalized(meshVertex(mesh, edge.vertices[edge.vertices.size() - 2]) - frame.point));
    }
    if (frame.rays.empty()) frame.rays = surfaceRays;

    const V3 in = frame.out * -1.0;
    for (const V3& ray : frame.rays) frame.spread = std::max(frame.spread, std::acos(std::clamp(dot(ray, in), -1.0, 1.0)));
    frame.usable = frame.spread > 0.05 && frame.spread < 1.53;  // outward-pointing corners only
    return frame;
}

// Trims away everything around the point that lies outside a ball of the given
// radius, nestled against the point. On a cone the ball meets the surface exactly.
std::vector<Feature> pointFeatures(const Mesh& mesh, const Topology& topology, const std::vector<int>& corners, double value) {
    constexpr int kArcSteps = 24;
    constexpr int kSpin = 64;
    Feature feature;
    feature.shape = Shape::Prisms;
    feature.op = Op::Subtract;
    for (int id : corners) {
        const PointFrame frame = pointFrame(mesh, topology, id);
        if (!frame.usable) return {};
        const double toCentre = value / std::sin(frame.spread);
        const double flat = value * std::sin(frame.spread);       // height of the cutting plane above the ball's centre
        const double ring = value * std::cos(frame.spread);       // radius of the ball there
        const double above = toCentre + std::max(1.0, value);     // well past the point
        const double wide = ring * 1.3 + 0.5;

        std::vector<std::array<double, 2>> loop;
        loop.push_back({wide, flat});
        loop.push_back({wide, above});
        loop.push_back({0.0, above});
        const double start = std::numbers::pi / 2, end = frame.spread;
        for (int i = 0; i <= kArcSteps; ++i) {
            const double angle = start + (end - start) * i / kArcSteps;
            loop.push_back({std::max(value * std::cos(angle), 0.0), value * std::sin(angle)});
        }

        Prism prism;
        const V3 helper = std::abs(frame.out.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
        prism.u = normalized(cross(helper, frame.out));
        prism.v = cross(frame.out, prism.u);
        prism.origin = frame.point - frame.out * toCentre;
        prism.length = 1;
        prism.revolveSegments = kSpin;
        prism.loops = {std::move(loop)};
        feature.prisms.push_back(std::move(prism));
    }
    return {feature};
}

// The face's outline comes from the display mesh, stored in single precision, while the
// solid's own edges (a gear's tooth curves, say) are exact. Taken as is, a push leaves
// walls a millionth of a millimetre thick standing along the edges. Growing the outline
// outward by a hair makes the push always reach past them.
std::vector<std::array<double, 2>> widened(const std::vector<std::array<double, 2>>& loop, double margin) {
    const size_t n = loop.size();
    if (n < 3) return loop;
    // The face lies to the left of each loop, so its outside is to the right.
    const auto rightOf = [](std::array<double, 2> a, std::array<double, 2> b) {
        const double dx = b[0] - a[0], dy = b[1] - a[1], l = std::hypot(dx, dy);
        return l > 0 ? std::array<double, 2>{dy / l, -dx / l} : std::array<double, 2>{0, 0};
    };
    std::vector<std::array<double, 2>> out(n);
    for (size_t i = 0; i < n; ++i) {
        const auto before = rightOf(loop[(i + n - 1) % n], loop[i]);
        const auto after = rightOf(loop[i], loop[(i + 1) % n]);
        std::array<double, 2> m{before[0] + after[0], before[1] + after[1]};
        const double l = std::hypot(m[0], m[1]);
        if (l < 1e-12) m = after;
        else m = {m[0] / l, m[1] / l};
        // Corners move further than straight runs, so each side moves out by the margin.
        const double stretch = margin / std::max(m[0] * after[0] + m[1] * after[1], 0.25);
        out[i] = {loop[i][0] + m[0] * stretch, loop[i][1] + m[1] * stretch};
    }
    return out;
}

std::vector<Feature> extrudeFeatures(const Mesh& mesh, const Topology& topology, const std::vector<int>& faces, double value) {
    if (value == 0) return {};
    Feature feature;
    feature.shape = Shape::Prisms;
    feature.op = value > 0 ? Op::Add : Op::Subtract;

    for (int id : faces) {
        const TopoFace& face = topology.faces[id];
        const V3 n = face.normal;
        const V3 helper = std::abs(n.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};

        Prism prism;
        prism.u = normalized(cross(helper, n));
        prism.v = cross(n, prism.u);
        const V3 onPlane = meshVertex(mesh, mesh.indices[face.triangles.front() * 3]);
        // Starts just inside an addition, or just above a cut, to overlap cleanly.
        prism.origin = onPlane + n * (value > 0 ? -kPlaneOverlap : kPlaneOverlap);
        prism.length = std::abs(value) + kPlaneOverlap;
        if (value < 0) {
            // A cut runs from the face into the solid, which is against the normal.
            prism.origin = onPlane + n * value;
        }
        for (const auto& loop : faceLoops(mesh, face)) {
            std::vector<std::array<double, 2>> points;
            for (uint32_t vertex : loop) {
                const V3 p = meshVertex(mesh, vertex) - onPlane;
                points.push_back({dot(p, prism.u), dot(p, prism.v)});
            }
            // Only a push needs it: on a pull, a wider block would leave a hairline ledge.
            prism.loops.push_back(value < 0 ? widened(points, kOutlineMargin) : std::move(points));
        }
        feature.prisms.push_back(std::move(prism));
    }
    return {feature};
}

}  // namespace

ModifyGuide modifyGuide(ModifyKind kind, const Mesh& mesh, const Topology& topology, int entity, V3 view) {
    ModifyGuide guide;
    if (kind == ModifyKind::Extrude) {
        const TopoFace& face = topology.faces[entity];
        guide.origin = face.centroid;
        guide.dir = face.normal;
        return guide;
    }

    if (kind == ModifyKind::PointFillet) {
        const PointFrame frame = pointFrame(mesh, topology, entity);
        guide.origin = frame.point;
        guide.dir = frame.out * -1.0;
        guide.scale = 1.0 / std::sin(std::max(frame.spread, 0.05)) - 1.0;
        if (std::abs(dot(guide.dir, view)) > kBisectorHidden && !frame.rays.empty()) {
            // Looking straight at the point: follow a crease instead, so the arrow can be grabbed.
            guide.dir = *std::min_element(frame.rays.begin(), frame.rays.end(), [&](V3 a, V3 b) { return std::abs(dot(a, view)) < std::abs(dot(b, view)); });
            guide.scale = 1.0 / std::tan(std::max(frame.spread, 0.05));
        }
        return guide;
    }

    const TopoEdge& edge = topology.edges[entity];
    const SegmentFrame frame = segmentFrame(mesh, edge, edge.segmentCount() / 2);
    const V3 middle = normalized(frame.ray1 + frame.ray2);
    guide.origin = (frame.a + frame.b) * 0.5;

    const double half = halfAngle(frame);
    const double acrossFace = kind == ModifyKind::Chamfer ? 1.0 : 1.0 / std::tan(half);
    const double alongMiddle = kind == ModifyKind::Chamfer ? length(frame.ray1 + frame.ray2) / 2 : 1.0 / std::sin(half) - 1.0;

    // The bisector is the truest direction, but it often points straight at the viewer.
    guide.dir = middle;
    guide.scale = alongMiddle;
    if (std::abs(dot(middle, view)) > kBisectorHidden) {
        const bool useFirst = std::abs(dot(frame.ray1, view)) < std::abs(dot(frame.ray2, view));
        guide.dir = useFirst ? frame.ray1 : frame.ray2;
        guide.scale = acrossFace;
    }
    return guide;
}

bool edgeCircle(const Mesh& mesh, const TopoEdge& edge, V3* centre, V3* axis, double* radius) { return circleOf(mesh, edge, centre, axis, radius); }

std::vector<Feature> modifyFeatures(ModifyKind kind, const Mesh& mesh, const Topology& topology, const std::vector<int>& entities, double value) {
    if (kind == ModifyKind::Extrude) return extrudeFeatures(mesh, topology, entities, value);
    if (kind == ModifyKind::PointFillet) return pointFeatures(mesh, topology, entities, value);
    return edgeFeatures(kind, mesh, topology, entities, value);
}

}  // namespace td
