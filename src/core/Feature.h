#pragma once

#include "core/Affine.h"
#include "core/Vec.h"

#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <vector>

namespace td {

enum class Op { Add, Subtract, Intersect };
// Compound folds its own child features into one solid, so a whole part can be
// added to, cut from or intersected with another. Mesh holds raw triangles.
enum class Shape { Box, Cylinder, Sphere, Torus, Wedge, Prisms, Compound, Mesh, Star, Gear, Coil, Capsule, Curve };

// A flat outline swept straight along the axis u x v. Used for direct edits.
struct Prism {
    V3 origin, u, v;
    double length = 0;
    int revolveSegments = 0;  // above 0: loops are (radius, height), spun around u x v at origin
    std::vector<std::vector<std::array<double, 2>>> loops;  // outer CCW, holes CW
    std::vector<V3> hull;     // if set: the solid is this convex hull, minus a ball at origin
    double ballRadius = 0;
};

// One step of the design. Features are applied in order, and each is placed
// by the centre of its bottom. All lengths are millimetres, angles degrees.
struct Feature {
    QString name;
    Op op = Op::Add;
    Shape shape = Shape::Box;

    double width = 20, depth = 20, height = 20;
    double corner = 0;           // box: edge rounding radius, 0 for sharp
    double diameter = 20;        // cylinder, sphere, torus (outer), capsule
    double topDiameter = -1;     // cylinder: negative means same as diameter
    double tubeDiameter = 5;     // torus
    int sides = 0;               // cylinder: 0 for round, 3+ for a polygon prism; star: points, 0 for 5
    double innerDiameter = 8;    // star: across the inner corners; diameter is tip to tip. gear: the bore, 0 for none
    bool internal = false;       // gear: teeth on the inside of a ring rather than the outside
    double turns = 6;            // coil: how many times the wire goes round; tubeDiameter is the wire
    double bend = 0;             // curve: degrees the rod turns from end to end, curling toward +X
    std::vector<Prism> prisms;   // Shape::Prisms only
    std::vector<Feature> children;          // Shape::Compound only
    std::vector<float> meshPositions;       // Shape::Mesh only
    std::vector<uint32_t> meshIndices;

    double x = 0, y = 0, z = 0;
    double rx = 0, ry = 0, rz = 0;
    double sx = 1, sy = 1, sz = 1;  // stretch along the shape's own axes, before turning: a stretched sphere is an ellipsoid
    Affine matrix;  // applied last; maps edits and merged parts into their body's own space
};

inline constexpr int kMaxFeatures = 500;
inline constexpr double kMaxDimension = 2000.0;
inline constexpr int kMaxGearTeeth = 400;
inline constexpr double kMinGearModule = 0.3;  // mm; smaller teeth do not print

QString shapeName(Shape shape);

// Gear sizes. diameter is always the overall outside diameter: the tooth tips of
// an outside gear, or the rim of a ring gear with its teeth on the inside.
double gearModule(const Feature& gear);
double gearRootDiameter(const Feature& gear);
QString opName(Op op);

// Returns false and sets *error when the feature cannot be built.
bool validateFeature(const Feature& feature, QString* error);
bool sameFeature(const Feature& a, const Feature& b);

QJsonObject featureToJson(const Feature& feature);
bool featureFromJson(const QJsonObject& object, Feature* feature, QString* error);

QByteArray designToJson(const std::vector<Feature>& features);
bool designFromJson(const QByteArray& json, std::vector<Feature>* features, QString* error);
bool featuresFromJson(const QJsonValue& array, std::vector<Feature>* features, QString* error);

}  // namespace td
