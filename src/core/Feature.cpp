#include "core/Feature.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace td {
namespace {

bool readNumber(const QJsonObject& o, const char* key, double* out, bool required, QString* error) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isUndefined() || v.isNull()) {
        if (required) *error = QStringLiteral("missing \"%1\"").arg(QLatin1String(key));
        return !required;
    }
    if (!v.isDouble()) {
        *error = QStringLiteral("\"%1\" must be a number").arg(QLatin1String(key));
        return false;
    }
    *out = v.toDouble();
    return true;
}

bool inSize(double v) { return std::isfinite(v) && v > 0 && v <= kMaxDimension; }

}  // namespace

QString shapeName(Shape shape) {
    switch (shape) {
        case Shape::Box: return QStringLiteral("box");
        case Shape::Cylinder: return QStringLiteral("cylinder");
        case Shape::Sphere: return QStringLiteral("sphere");
        case Shape::Torus: return QStringLiteral("torus");
        case Shape::Wedge: return QStringLiteral("wedge");
        case Shape::Prisms: return QStringLiteral("prisms");
        case Shape::Compound: return QStringLiteral("compound");
        case Shape::Mesh: return QStringLiteral("mesh");
        case Shape::Star: return QStringLiteral("star");
        case Shape::Gear: return QStringLiteral("gear");
        case Shape::Coil: return QStringLiteral("coil");
        case Shape::Capsule: return QStringLiteral("capsule");
        case Shape::Curve: return QStringLiteral("curve");
    }
    return {};
}

double gearModule(const Feature& g) {
    const int teeth = g.sides > 0 ? g.sides : 20;
    // A ring gear also has a dedendum inside and a 3-module rim around it.
    return g.diameter / (g.internal ? teeth + 8.5 : teeth + 2.0);
}

double gearRootDiameter(const Feature& g) {
    const int teeth = g.sides > 0 ? g.sides : 20;
    return gearModule(g) * (g.internal ? teeth - 2.0 : teeth - 2.5);
}

QString opName(Op op) {
    switch (op) {
        case Op::Add: return QStringLiteral("add");
        case Op::Subtract: return QStringLiteral("subtract");
        case Op::Intersect: return QStringLiteral("intersect");
    }
    return {};
}

bool validateFeature(const Feature& f, QString* error) {
    const QString who = f.name.isEmpty() ? shapeName(f.shape) : f.name;
    const auto fail = [&](const QString& why) {
        *error = QStringLiteral("%1: %2").arg(who, why);
        return false;
    };
    const QString sizeRule = QStringLiteral("must be above 0 and at most %1 mm").arg(kMaxDimension);

    for (double v : {f.x, f.y, f.z, f.rx, f.ry, f.rz})
        if (!std::isfinite(v) || std::abs(v) > 100000) return fail(QStringLiteral("position or rotation is out of range"));
    for (double v : {f.sx, f.sy, f.sz})
        if (!std::isfinite(v) || v < 0.01 || v > 100) return fail(QStringLiteral("stretch must be between 0.01 and 100"));
    for (double v : f.matrix.m)
        if (!std::isfinite(v)) return fail(QStringLiteral("has an invalid placement"));

    switch (f.shape) {
        case Shape::Box:
            if (!inSize(f.width) || !inSize(f.depth) || !inSize(f.height)) return fail("width, depth and height " + sizeRule);
            if (!std::isfinite(f.corner) || f.corner < 0 || f.corner * 2 >= std::min({f.width, f.depth, f.height}))
                return fail(QStringLiteral("corner radius must be less than half the smallest side"));
            break;
        case Shape::Prisms:
            if (f.prisms.empty()) return fail(QStringLiteral("has no geometry"));
            for (const Prism& p : f.prisms) {
                if (!std::isfinite(p.length) || p.length <= 0 || p.length > kMaxDimension * 4) return fail(QStringLiteral("has an invalid length"));
                if (!p.hull.empty()) {
                    if (p.hull.size() < 4 || !std::isfinite(p.ballRadius) || p.ballRadius <= 0) return fail(QStringLiteral("has an invalid corner"));
                    continue;
                }
                if (p.loops.empty()) return fail(QStringLiteral("has no outline"));
                for (const auto& loop : p.loops)
                    if (loop.size() < 3) return fail(QStringLiteral("has an outline with too few points"));
            }
            break;
        case Shape::Wedge:
            if (!inSize(f.width) || !inSize(f.depth) || !inSize(f.height)) return fail("width, depth and height " + sizeRule);
            break;
        case Shape::Compound:
            if (f.children.empty()) return fail(QStringLiteral("has no parts"));
            for (const Feature& child : f.children)
                if (!validateFeature(child, error)) return false;
            break;
        case Shape::Mesh: {
            const size_t vertices = f.meshPositions.size() / 3;
            if (f.meshPositions.size() % 3 != 0 || f.meshIndices.size() % 3 != 0 || f.meshIndices.size() < 12) return fail(QStringLiteral("has a broken mesh"));
            for (uint32_t index : f.meshIndices)
                if (index >= vertices) return fail(QStringLiteral("has a broken mesh"));
            for (float v : f.meshPositions)
                if (!std::isfinite(v)) return fail(QStringLiteral("has a broken mesh"));
            break;
        }
        case Shape::Cylinder:
            if (!inSize(f.diameter) || !inSize(f.height)) return fail("diameter and height " + sizeRule);
            if (!std::isfinite(f.topDiameter) || f.topDiameter > kMaxDimension || (f.topDiameter < 0 && f.topDiameter != -1))
                return fail(QStringLiteral("top diameter is out of range"));
            if (f.sides != 0 && (f.sides < 3 || f.sides > 256)) return fail(QStringLiteral("sides must be 0 (round) or 3 to 256"));
            if (!std::isfinite(f.corner) || f.corner < 0 || f.corner * 2 >= std::min(f.diameter, f.height))
                return fail(QStringLiteral("edge rounding must be less than half the diameter and half the height"));
            break;
        case Shape::Sphere:
            if (!inSize(f.diameter)) return fail("diameter " + sizeRule);
            break;
        case Shape::Curve:
            if (!inSize(f.diameter) || !inSize(f.height)) return fail("diameter and length " + sizeRule);
            if (!std::isfinite(f.topDiameter) || f.topDiameter > kMaxDimension || (f.topDiameter <= 0 && f.topDiameter != -1))
                return fail(QStringLiteral("end diameter must be above 0"));
            if (!std::isfinite(f.bend) || std::abs(f.bend) > 330) return fail(QStringLiteral("bend must be between -330 and 330 degrees"));
            if (f.height < std::max(f.diameter, f.topDiameter)) return fail(QStringLiteral("a curve must be longer than it is thick"));
            break;
        case Shape::Capsule:
            if (!inSize(f.diameter) || !inSize(f.height)) return fail("diameter and length " + sizeRule);
            if (f.height < f.diameter) return fail(QStringLiteral("a capsule must be at least as long as it is thick"));
            break;
        case Shape::Coil: {
            if (!inSize(f.diameter) || !inSize(f.height) || !inSize(f.tubeDiameter)) return fail("diameter, height and wire diameter " + sizeRule);
            if (!std::isfinite(f.turns) || f.turns < 0.25 || f.turns > 200) return fail(QStringLiteral("a coil needs 0.25 to 200 turns"));
            if (f.tubeDiameter * 2 >= f.diameter) return fail(QStringLiteral("the wire must be less than half the coil's diameter, to leave a hole down the middle"));
            if (f.height <= f.tubeDiameter) return fail(QStringLiteral("the coil must be taller than its wire is thick"));
            // Neighbouring turns may not touch, or the spring would be one solid lump.
            if ((f.height - f.tubeDiameter) / f.turns < f.tubeDiameter * 1.1)
                return fail(QStringLiteral("%1 turns of %2 mm wire do not fit in %3 mm; use fewer turns or make it taller").arg(f.turns).arg(f.tubeDiameter).arg(f.height));
            break;
        }
        case Shape::Gear: {
            if (!inSize(f.diameter) || !inSize(f.height)) return fail("diameter and height " + sizeRule);
            const int teeth = f.sides > 0 ? f.sides : 20;
            if (teeth < 6 || teeth > kMaxGearTeeth) return fail(QStringLiteral("a gear needs 6 to %1 teeth").arg(kMaxGearTeeth));
            if (gearModule(f) < kMinGearModule)
                return fail(QStringLiteral("%1 teeth are too fine to print at %2 mm across; make it larger or use fewer teeth").arg(teeth).arg(f.diameter));
            if (!std::isfinite(f.innerDiameter) || f.innerDiameter < 0) return fail(QStringLiteral("bore is out of range"));
            if (!f.internal && f.innerDiameter > 0 && f.innerDiameter >= gearRootDiameter(f) - 2 * gearModule(f))
                return fail(QStringLiteral("bore leaves too little material under the teeth"));
            break;
        }
        case Shape::Star:
            if (!inSize(f.diameter) || !inSize(f.height)) return fail("diameter and height " + sizeRule);
            if (!std::isfinite(f.innerDiameter) || f.innerDiameter <= 0 || f.innerDiameter >= f.diameter)
                return fail(QStringLiteral("inner diameter must be above 0 and less than the diameter"));
            if (f.sides != 0 && (f.sides < 3 || f.sides > 64)) return fail(QStringLiteral("a star needs 3 to 64 points"));
            break;
        case Shape::Torus:
            if (!inSize(f.diameter) || !inSize(f.tubeDiameter)) return fail("diameter and tube diameter " + sizeRule);
            if (f.tubeDiameter * 2 >= f.diameter) return fail(QStringLiteral("tube diameter must be less than half the outer diameter"));
            break;
    }
    return true;
}

bool sameFeature(const Feature& a, const Feature& b) {
    const auto sameV = [](V3 p, V3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    if (a.op != b.op || a.shape != b.shape || a.width != b.width || a.depth != b.depth || a.height != b.height || a.corner != b.corner ||
        a.diameter != b.diameter || a.topDiameter != b.topDiameter || a.tubeDiameter != b.tubeDiameter || a.sides != b.sides || a.innerDiameter != b.innerDiameter || a.internal != b.internal || a.turns != b.turns || a.bend != b.bend || a.x != b.x ||
        a.y != b.y || a.z != b.z || a.rx != b.rx || a.ry != b.ry || a.rz != b.rz || a.sx != b.sx || a.sy != b.sy || a.sz != b.sz || a.prisms.size() != b.prisms.size() ||
        a.matrix != b.matrix || a.children.size() != b.children.size() || a.meshPositions != b.meshPositions || a.meshIndices != b.meshIndices)
        return false;
    for (size_t i = 0; i < a.children.size(); ++i)
        if (!sameFeature(a.children[i], b.children[i])) return false;
    for (size_t i = 0; i < a.prisms.size(); ++i) {
        const Prism& p = a.prisms[i];
        const Prism& q = b.prisms[i];
        if (!sameV(p.origin, q.origin) || !sameV(p.u, q.u) || !sameV(p.v, q.v) || p.length != q.length || p.revolveSegments != q.revolveSegments || p.loops != q.loops || p.ballRadius != q.ballRadius || p.hull.size() != q.hull.size())
            return false;
        for (size_t h = 0; h < p.hull.size(); ++h)
            if (!sameV(p.hull[h], q.hull[h])) return false;
    }
    return true;
}

QJsonObject featureToJson(const Feature& f) {
    QJsonObject o;
    o["name"] = f.name;
    o["op"] = opName(f.op);
    o["shape"] = shapeName(f.shape);
    switch (f.shape) {
        case Shape::Compound: {
            QJsonArray children;
            for (const Feature& child : f.children) children.append(featureToJson(child));
            o["children"] = children;
            break;
        }
        case Shape::Mesh: {
            QJsonArray positions, indices;
            for (float v : f.meshPositions) positions.append(double(v));
            for (uint32_t i : f.meshIndices) indices.append(qint64(i));
            o["positions"] = positions;
            o["indices"] = indices;
            break;
        }
        case Shape::Prisms: {
            const auto triple = [](V3 v) { return QJsonArray{v.x, v.y, v.z}; };
            QJsonArray prisms;
            for (const Prism& p : f.prisms) {
                QJsonArray loops;
                for (const auto& loop : p.loops) {
                    QJsonArray flat;
                    for (const auto& point : loop) {
                        flat.append(point[0]);
                        flat.append(point[1]);
                    }
                    loops.append(flat);
                }
                QJsonObject item{{"origin", triple(p.origin)}, {"u", triple(p.u)}, {"v", triple(p.v)}, {"length", p.length}, {"loops", loops}};
                if (p.revolveSegments > 0) item["revolve"] = p.revolveSegments;
                if (!p.hull.empty()) {
                    QJsonArray hull;
                    for (const V3& point : p.hull) hull.append(triple(point));
                    item["hull"] = hull;
                    item["ball"] = p.ballRadius;
                }
                prisms.append(item);
            }
            o["prisms"] = prisms;
            break;
        }
        case Shape::Wedge:
            o["width"] = f.width;
            o["depth"] = f.depth;
            o["height"] = f.height;
            break;
        case Shape::Box:
            o["width"] = f.width;
            o["depth"] = f.depth;
            o["height"] = f.height;
            if (f.corner > 0) o["corner"] = f.corner;
            break;
        case Shape::Cylinder:
            o["diameter"] = f.diameter;
            o["height"] = f.height;
            if (f.topDiameter >= 0) o["top_diameter"] = f.topDiameter;
            if (f.sides > 0) o["sides"] = f.sides;
            if (f.corner > 0) o["corner"] = f.corner;
            break;
        case Shape::Sphere:
            o["diameter"] = f.diameter;
            break;
        case Shape::Capsule:
            o["diameter"] = f.diameter;
            o["height"] = f.height;
            break;
        case Shape::Curve:
            o["diameter"] = f.diameter;
            if (f.topDiameter >= 0) o["top_diameter"] = f.topDiameter;
            o["height"] = f.height;
            o["bend"] = f.bend;
            break;
        case Shape::Coil:
            o["diameter"] = f.diameter;
            o["wire_diameter"] = f.tubeDiameter;
            o["height"] = f.height;
            o["turns"] = f.turns;
            break;
        case Shape::Gear:
            o["teeth"] = f.sides > 0 ? f.sides : 20;
            o["diameter"] = f.diameter;
            o["height"] = f.height;
            if (f.innerDiameter > 0 && !f.internal) o["bore"] = f.innerDiameter;
            if (f.internal) o["internal"] = true;
            break;
        case Shape::Star:
            o["points"] = f.sides > 0 ? f.sides : 5;
            o["diameter"] = f.diameter;
            o["inner_diameter"] = f.innerDiameter;
            o["height"] = f.height;
            break;
        case Shape::Torus:
            o["diameter"] = f.diameter;
            o["tube_diameter"] = f.tubeDiameter;
            break;
    }
    const auto putIfSet = [&o](const char* key, double v) {
        if (v != 0) o[QLatin1String(key)] = v;
    };
    putIfSet("x", f.x);
    putIfSet("y", f.y);
    putIfSet("z", f.z);
    putIfSet("rx", f.rx);
    putIfSet("ry", f.ry);
    putIfSet("rz", f.rz);
    const auto putStretch = [&o](const char* key, double v) {
        if (v != 1) o[QLatin1String(key)] = v;
    };
    putStretch("sx", f.sx);
    putStretch("sy", f.sy);
    putStretch("sz", f.sz);
    if (!f.matrix.isIdentity()) {
        QJsonArray matrix;
        for (double v : f.matrix.m) matrix.append(v);
        o["matrix"] = matrix;
    }
    return o;
}

bool featureFromJson(const QJsonObject& o, Feature* out, QString* error) {
    Feature f;
    f.name = o.value("name").toString();

    const QString op = o.value("op").toString(QStringLiteral("add"));
    if (op == "add") f.op = Op::Add;
    else if (op == "subtract") f.op = Op::Subtract;
    else if (op == "intersect") f.op = Op::Intersect;
    else {
        *error = QStringLiteral("unknown op \"%1\" (use add, subtract or intersect)").arg(op);
        return false;
    }

    const QString shape = o.value("shape").toString();
    if (shape == "box") f.shape = Shape::Box;
    else if (shape == "cylinder") f.shape = Shape::Cylinder;
    else if (shape == "sphere") f.shape = Shape::Sphere;
    else if (shape == "torus") f.shape = Shape::Torus;
    else if (shape == "wedge") f.shape = Shape::Wedge;
    else if (shape == "prisms") f.shape = Shape::Prisms;
    else if (shape == "compound") f.shape = Shape::Compound;
    else if (shape == "mesh") f.shape = Shape::Mesh;
    else if (shape == "star") f.shape = Shape::Star;
    else if (shape == "gear") f.shape = Shape::Gear;
    else if (shape == "coil") f.shape = Shape::Coil;
    else if (shape == "capsule") f.shape = Shape::Capsule;
    else if (shape == "curve") f.shape = Shape::Curve;
    else {
        *error = QStringLiteral("unknown shape \"%1\" (use box, wedge, cylinder, sphere, torus, star, gear, coil, capsule or curve)").arg(shape);
        return false;
    }

    // Only the fields belonging to the shape are read, so a stray value on an
    // unrelated field cannot change the result.
    double sides = 0;
    bool ok = true;
    switch (f.shape) {
        case Shape::Compound:
            for (const QJsonValue& value : o.value("children").toArray()) {
                Feature child;
                if (!featureFromJson(value.toObject(), &child, error)) return false;
                f.children.push_back(std::move(child));
            }
            break;
        case Shape::Mesh:
            for (const QJsonValue& value : o.value("positions").toArray()) f.meshPositions.push_back(float(value.toDouble()));
            for (const QJsonValue& value : o.value("indices").toArray()) f.meshIndices.push_back(uint32_t(value.toInteger()));
            break;
        case Shape::Prisms: {
            const auto triple = [](const QJsonValue& v, V3* out) {
                const QJsonArray a = v.toArray();
                if (a.size() != 3 || !a[0].isDouble() || !a[1].isDouble() || !a[2].isDouble()) return false;
                *out = {a[0].toDouble(), a[1].toDouble(), a[2].toDouble()};
                return true;
            };
            for (const QJsonValue& value : o.value("prisms").toArray()) {
                const QJsonObject po = value.toObject();
                Prism p;
                ok = ok && triple(po.value("origin"), &p.origin) && triple(po.value("u"), &p.u) && triple(po.value("v"), &p.v);
                p.length = po.value("length").toDouble();
                p.revolveSegments = po.value("revolve").toInt();
                p.ballRadius = po.value("ball").toDouble();
                for (const QJsonValue& point : po.value("hull").toArray()) {
                    V3 h;
                    ok = ok && triple(point, &h);
                    p.hull.push_back(h);
                }
                for (const QJsonValue& loopValue : po.value("loops").toArray()) {
                    const QJsonArray flat = loopValue.toArray();
                    std::vector<std::array<double, 2>> loop;
                    for (qsizetype i = 0; i + 1 < flat.size(); i += 2) loop.push_back({flat[i].toDouble(), flat[i + 1].toDouble()});
                    p.loops.push_back(std::move(loop));
                }
                f.prisms.push_back(std::move(p));
            }
            if (!ok) *error = QStringLiteral("has malformed prism data");
            break;
        }
        case Shape::Box:
            ok = readNumber(o, "width", &f.width, true, error) && readNumber(o, "depth", &f.depth, true, error) &&
                 readNumber(o, "height", &f.height, true, error) && readNumber(o, "corner", &f.corner, false, error);
            break;
        case Shape::Wedge:
            ok = readNumber(o, "width", &f.width, true, error) && readNumber(o, "depth", &f.depth, true, error) &&
                 readNumber(o, "height", &f.height, true, error);
            break;
        case Shape::Cylinder:
            ok = readNumber(o, "diameter", &f.diameter, true, error) && readNumber(o, "height", &f.height, true, error) &&
                 readNumber(o, "top_diameter", &f.topDiameter, false, error) && readNumber(o, "sides", &sides, false, error) &&
                 readNumber(o, "corner", &f.corner, false, error);
            f.sides = static_cast<int>(sides);
            break;
        case Shape::Sphere:
            ok = readNumber(o, "diameter", &f.diameter, true, error);
            break;
        case Shape::Capsule:
            ok = readNumber(o, "diameter", &f.diameter, true, error) && readNumber(o, "height", &f.height, true, error);
            break;
        case Shape::Curve:
            ok = readNumber(o, "diameter", &f.diameter, true, error) && readNumber(o, "height", &f.height, true, error) &&
                 readNumber(o, "top_diameter", &f.topDiameter, false, error) && readNumber(o, "bend", &f.bend, false, error);
            if (f.topDiameter == 0) f.topDiameter = -1;  // a point cannot be swept; keep the thickness
            break;
        case Shape::Coil: {
            f.tubeDiameter = 0;
            f.turns = 0;
            ok = readNumber(o, "diameter", &f.diameter, true, error) && readNumber(o, "height", &f.height, true, error) &&
                 readNumber(o, "wire_diameter", &f.tubeDiameter, false, error) && readNumber(o, "turns", &f.turns, false, error);
            // Unstated: a wire a tenth of the coil, and as many turns as leave a gap of one wire.
            if (f.tubeDiameter <= 0) f.tubeDiameter = std::max(0.8, f.diameter / 10);
            if (f.turns <= 0) f.turns = std::max(1.0, std::floor((f.height - f.tubeDiameter) / (f.tubeDiameter * 2)));
            break;
        }
        case Shape::Gear: {
            double teeth = 20;
            f.innerDiameter = 0;
            f.diameter = 0;
            ok = readNumber(o, "teeth", &teeth, false, error) && readNumber(o, "diameter", &f.diameter, false, error) &&
                 readNumber(o, "height", &f.height, true, error) && readNumber(o, "bore", &f.innerDiameter, false, error);
            f.sides = static_cast<int>(teeth);
            f.internal = o.value("internal").toBool(false);
            // Without a size, teeth of 1 mm module: printable, and the gear grows with its tooth count.
            if (f.diameter <= 0) f.diameter = f.internal ? f.sides + 8.5 : f.sides + 2.0;
            break;
        }
        case Shape::Star:
            f.innerDiameter = -1;
            ok = readNumber(o, "diameter", &f.diameter, true, error) && readNumber(o, "height", &f.height, true, error) &&
                 readNumber(o, "inner_diameter", &f.innerDiameter, false, error) && readNumber(o, "points", &sides, false, error);
            if (f.innerDiameter <= 0) f.innerDiameter = f.diameter * 0.4;  // a classic star when unstated
            f.sides = sides > 0 ? static_cast<int>(sides) : 5;
            break;
        case Shape::Torus:
            ok = readNumber(o, "diameter", &f.diameter, true, error) && readNumber(o, "tube_diameter", &f.tubeDiameter, true, error);
            break;
    }
    ok = ok && readNumber(o, "x", &f.x, false, error) && readNumber(o, "y", &f.y, false, error) &&
         readNumber(o, "z", &f.z, false, error) && readNumber(o, "rx", &f.rx, false, error) &&
         readNumber(o, "ry", &f.ry, false, error) && readNumber(o, "rz", &f.rz, false, error);
    for (const auto& [key, member] : {std::pair{"sx", &Feature::sx}, std::pair{"sy", &Feature::sy}, std::pair{"sz", &Feature::sz}})
        if (ok && o.contains(QLatin1String(key))) ok = readNumber(o, key, &(f.*member), false, error);
    if (ok && o.contains("matrix")) {
        const QJsonArray matrix = o.value("matrix").toArray();
        ok = matrix.size() == 12;
        for (int i = 0; ok && i < 12; ++i) {
            ok = matrix[i].isDouble();
            f.matrix.m[i] = matrix[i].toDouble();
        }
        if (!ok) *error = QStringLiteral("has a malformed placement");
    }

    if (!ok) {
        if (!f.name.isEmpty()) *error = QStringLiteral("%1: %2").arg(f.name, *error);
        return false;
    }
    if (!validateFeature(f, error)) return false;
    *out = f;
    return true;
}

QByteArray designToJson(const std::vector<Feature>& features) {
    QJsonArray array;
    for (const Feature& f : features) array.append(featureToJson(f));
    QJsonObject root;
    root["format"] = "cubra";
    root["version"] = 1;
    root["features"] = array;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool featuresFromJson(const QJsonValue& value, std::vector<Feature>* features, QString* error) {
    if (!value.isArray()) {
        *error = QStringLiteral("\"features\" must be an array");
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.size() > kMaxFeatures) {
        *error = QStringLiteral("too many features (limit is %1)").arg(kMaxFeatures);
        return false;
    }
    std::vector<Feature> parsed;
    parsed.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array[i].isObject()) {
            *error = QStringLiteral("feature %1 is not an object").arg(i + 1);
            return false;
        }
        Feature f;
        QString why;
        if (!featureFromJson(array[i].toObject(), &f, &why)) {
            *error = QStringLiteral("feature %1: %2").arg(i + 1).arg(why);
            return false;
        }
        parsed.push_back(std::move(f));
    }
    *features = std::move(parsed);
    return true;
}

bool designFromJson(const QByteArray& json, std::vector<Feature>* features, QString* error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        *error = QStringLiteral("expected a JSON object");
        return false;
    }
    return featuresFromJson(doc.object().value("features"), features, error);
}

}  // namespace td
