#include "core/Pattern.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace td {
namespace {

int clampCount(int count) { return std::clamp(count, 1, kMaxPatternCopies); }

}  // namespace

V3 acrossDirection(V3 along) {
    const V3 a = normalized(along);
    if (std::abs(a.z) < 0.99) return normalized(cross(V3{0, 0, 1}, a));
    return normalized(cross(a, V3{1, 0, 0}));
}

std::vector<Affine> rectangularPattern(const RectangularPattern& p) {
    const V3 along = normalized(p.along);
    const V3 across = acrossDirection(along);
    const int n1 = clampCount(p.countAlong), n2 = clampCount(p.countAcross);
    const double step1 = p.totalSpan ? (n1 > 1 ? p.spacingAlong / (n1 - 1) : 0) : p.spacingAlong;
    const double step2 = p.totalSpan ? (n2 > 1 ? p.spacingAcross / (n2 - 1) : 0) : p.spacingAcross;
    const int from1 = p.symmetric ? -(n1 - 1) : 0, from2 = p.symmetric ? -(n2 - 1) : 0;

    std::vector<Affine> out;
    for (int i = from1; i < n1; ++i)
        for (int j = from2; j < n2; ++j) {
            if (i == 0 && j == 0) continue;
            if (int(out.size()) >= kMaxPatternCopies) return out;
            out.push_back(Affine::translation(along * (i * step1) + across * (j * step2)));
        }
    return out;
}

std::vector<Affine> circularPattern(const CircularPattern& p) {
    const int n = clampCount(p.count);
    // Across an arc both ends get a copy; around a full circle the last would land on the first.
    const double step = p.fullCircle || std::abs(p.angle) >= 360 ? 360.0 / n : (n > 1 ? p.angle / (n - 1) : 0);
    std::vector<Affine> out;
    for (int k = 1; k < n; ++k) out.push_back(Affine::rotation(p.axis, k * step, p.point));
    return out;
}

Affine rotationBetween(V3 a, V3 b) {
    const V3 from = normalized(a), to = normalized(b);
    const double cosine = std::clamp(dot(from, to), -1.0, 1.0);
    if (cosine > 1 - 1e-12) return {};
    V3 axis = cross(from, to);
    if (length(axis) < 1e-9) axis = std::abs(from.x) < 0.9 ? cross(from, V3{1, 0, 0}) : cross(from, V3{0, 1, 0});
    return Affine::rotation(axis, std::acos(cosine) * 180.0 / std::numbers::pi, {});
}

std::vector<Affine> pathPattern(const PathPattern& p) {
    std::vector<V3> points = p.path;
    if (points.size() < 2) return {};
    if (p.closed) points.push_back(points.front());
    std::vector<double> at{0};
    for (size_t i = 1; i < points.size(); ++i) at.push_back(at.back() + length(points[i] - points[i - 1]));
    const double total = at.back();
    if (total <= 0) return {};

    const int n = clampCount(p.count);
    const double reach = p.distance > 0 ? std::min(p.distance, total) : total;
    // Going all the way round a loop, the last copy would sit on the original.
    const double step = p.closed && reach >= total - 1e-9 ? reach / n : (n > 1 ? reach / (n - 1) : 0);

    const auto sample = [&](double s, V3* point, V3* tangent) {
        size_t i = std::upper_bound(at.begin(), at.end(), s) - at.begin();
        i = std::clamp<size_t>(i, 1, points.size() - 1);
        const double span = at[i] - at[i - 1];
        const double t = span > 0 ? (s - at[i - 1]) / span : 0;
        *point = points[i - 1] + (points[i] - points[i - 1]) * t;
        *tangent = normalized(points[i] - points[i - 1]);
    };
    V3 start, startTangent;
    sample(0, &start, &startTangent);

    std::vector<Affine> out;
    for (int k = 1; k < n; ++k) {
        V3 point, tangent;
        sample(k * step, &point, &tangent);
        if (p.followPath) out.push_back(Affine::translation(point) * rotationBetween(startTangent, tangent) * Affine::translation(start * -1.0));
        else out.push_back(Affine::translation(point - start));
    }
    return out;
}

Affine mirrorAcross(V3 point, V3 normal) {
    const V3 n = normalized(normal);
    // p' = p - 2 (n . (p - q)) n
    const auto column = [&](V3 axis) { return axis - n * (2 * dot(n, axis)); };
    return Affine::fromColumns(column({1, 0, 0}), column({0, 1, 0}), column({0, 0, 1}), n * (2 * dot(n, point)));
}

std::vector<int> addCopies(Document* document, const std::vector<int>& bodies, const std::vector<Affine>& placements) {
    std::vector<int> created;
    const std::vector<Body> originals = [&] {
        std::vector<Body> list;
        for (int b : bodies) list.push_back(document->bodies[b]);
        return list;
    }();
    for (const Affine& placement : placements) {
        for (const Body& original : originals) {
            Body copy = original;
            copy.id = document->nextBodyId++;
            // "Box 1" is copied as "Box 2", "Box 3" and so on.
            QString label = original.name.section(QStringLiteral(" ("), 0, 0);
            label.remove(QRegularExpression(QStringLiteral("\\s+\\d+$")));
            copy.name = nextName(*document, label.isEmpty() ? QStringLiteral("Body") : label);
            copy.transform = placement * original.transform;
            copy.groups.clear();  // copies stand on their own
            document->bodies.push_back(std::move(copy));
            created.push_back(int(document->bodies.size()) - 1);
        }
    }
    return created;
}

}  // namespace td
