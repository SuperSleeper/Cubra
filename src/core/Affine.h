#pragma once

#include "core/Vec.h"

#include <array>
#include <cmath>
#include <numbers>

namespace td {

// A 3D affine map: p' = x * c0 + y * c1 + z * c2 + t, stored column by column
// (c0, c1, c2, t), which is also the geometry kernel's layout.
struct Affine {
    std::array<double, 12> m{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

    static Affine fromColumns(V3 x, V3 y, V3 z, V3 t) { return {{x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z, t.x, t.y, t.z}}; }
    static Affine translation(V3 t) { return fromColumns({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, t); }

    // Scales about pivot, along the world axes.
    static Affine scaling(V3 factors, V3 pivot) {
        return translation(pivot) * fromColumns({factors.x, 0, 0}, {0, factors.y, 0}, {0, 0, factors.z}, {}) * translation(pivot * -1.0);
    }

    // Turns about an axis through pivot, counter-clockwise looking down the axis.
    static Affine rotation(V3 axis, double degrees, V3 pivot) {
        const V3 k = normalized(axis);
        const double a = degrees * std::numbers::pi / 180.0, c = std::cos(a), s = std::sin(a), t = 1 - c;
        const Affine turn = fromColumns({t * k.x * k.x + c, t * k.x * k.y + s * k.z, t * k.x * k.z - s * k.y},
                                        {t * k.x * k.y - s * k.z, t * k.y * k.y + c, t * k.y * k.z + s * k.x},
                                        {t * k.x * k.z + s * k.y, t * k.y * k.z - s * k.x, t * k.z * k.z + c}, {});
        return translation(pivot) * turn * translation(pivot * -1.0);
    }

    V3 column(int i) const { return {m[i * 3], m[i * 3 + 1], m[i * 3 + 2]}; }
    V3 direction(V3 d) const { return column(0) * d.x + column(1) * d.y + column(2) * d.z; }
    V3 point(V3 p) const { return direction(p) + column(3); }

    // Applies other first, then this.
    Affine operator*(const Affine& other) const {
        return fromColumns(direction(other.column(0)), direction(other.column(1)), direction(other.column(2)), point(other.column(3)));
    }

    Affine inverse() const {
        const V3 a = column(0), b = column(1), c = column(2);
        const double det = dot(a, cross(b, c));
        if (std::abs(det) < 1e-15) return {};
        // Rows of the inverse are the cross products of the columns.
        const V3 r0 = cross(b, c) * (1 / det), r1 = cross(c, a) * (1 / det), r2 = cross(a, b) * (1 / det);
        const Affine linear = fromColumns({r0.x, r1.x, r2.x}, {r0.y, r1.y, r2.y}, {r0.z, r1.z, r2.z}, {});
        return translation(linear.direction(column(3)) * -1.0) * linear;
    }

    bool isIdentity() const { return *this == Affine{}; }
    bool operator==(const Affine& other) const { return m == other.m; }
    bool operator!=(const Affine& other) const { return m != other.m; }
};

}  // namespace td
