#include "core/Document.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <set>

namespace td {
namespace {

QJsonArray affineToJson(const Affine& a) {
    QJsonArray out;
    for (double v : a.m) out.append(v);
    return out;
}

bool affineFromJson(const QJsonValue& value, Affine* out) {
    const QJsonArray a = value.toArray();
    if (a.size() != 12) return false;
    for (int i = 0; i < 12; ++i) {
        if (!a[i].isDouble()) return false;
        out->m[i] = a[i].toDouble();
    }
    return true;
}

// The group a body belongs to one level inside the context, or 0 for none.
int groupAtLevel(const Body& body, size_t level) { return body.groups.size() > level ? body.groups[level] : 0; }

}  // namespace

QByteArray documentToJson(const Document& document) {
    QJsonArray bodies;
    for (const Body& body : document.bodies) {
        QJsonArray features;
        for (const Feature& f : body.features) features.append(featureToJson(f));
        QJsonObject item{{"name", body.name}, {"features", features}};
        if (!body.transform.isIdentity()) item["transform"] = affineToJson(body.transform);
        if (!body.groups.empty()) {
            QJsonArray groups;
            for (int g : body.groups) groups.append(g);
            item["groups"] = groups;
        }
        if (body.hidden) item["hidden"] = true;
        if (body.color) item["color"] = QStringLiteral("#%1").arg(body.color, 6, 16, QChar('0'));
        bodies.append(item);
    }
    const QJsonObject root{{"format", "cubra"}, {"version", 2}, {"bodies", bodies}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool documentFromJson(const QByteArray& json, Document* document, QString* error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = parseError.error != QJsonParseError::NoError ? QStringLiteral("not valid JSON: %1").arg(parseError.errorString())
                                                              : QStringLiteral("expected a JSON object");
        return false;
    }
    const QJsonObject root = doc.object();
    Document loaded;

    if (!root.contains("bodies")) {
        std::vector<Feature> features;
        if (!featuresFromJson(root.value("features"), &features, error)) return false;
        const bool onlyAdditions = std::all_of(features.begin(), features.end(), [](const Feature& f) { return f.op == Op::Add; });
        if (onlyAdditions) {
            for (Feature& f : features) {
                const QString name = f.name.isEmpty() ? nextName(loaded, QStringLiteral("Body")) : f.name;
                addBody(&loaded, name, {std::move(f)});
            }
        } else if (!features.empty()) {
            // Cuts applied to everything before them, so it all stays one body.
            addBody(&loaded, QStringLiteral("Body 1"), std::move(features));
        }
        *document = std::move(loaded);
        return true;
    }

    const QJsonArray bodies = root.value("bodies").toArray();
    for (qsizetype i = 0; i < bodies.size(); ++i) {
        const QJsonObject item = bodies[i].toObject();
        std::vector<Feature> features;
        QString why;
        if (!featuresFromJson(item.value("features"), &features, &why)) {
            *error = QStringLiteral("body %1: %2").arg(i + 1).arg(why);
            return false;
        }
        Affine transform;
        if (item.contains("transform") && !affineFromJson(item.value("transform"), &transform)) {
            *error = QStringLiteral("body %1 has a malformed placement").arg(i + 1);
            return false;
        }
        const int index = addBody(&loaded, item.value("name").toString(QStringLiteral("Body %1").arg(i + 1)), std::move(features), transform);
        for (const QJsonValue& g : item.value("groups").toArray()) {
            const int id = g.toInt();
            if (id <= 0) continue;
            loaded.bodies[index].groups.push_back(id);
            loaded.nextGroupId = std::max(loaded.nextGroupId, id + 1);
        }
        loaded.bodies[index].hidden = item.value("hidden").toBool(false);
        const QString color = item.value("color").toString();
        if (color.startsWith('#') && color.size() == 7) loaded.bodies[index].color = color.mid(1).toUInt(nullptr, 16) & 0xFFFFFF;
    }
    *document = std::move(loaded);
    return true;
}

QString nextName(const Document& document, const QString& label) {
    const QRegularExpression pattern("^" + QRegularExpression::escape(label) + " (\\d+)$");
    int highest = 0;
    for (const Body& body : document.bodies) {
        const auto match = pattern.match(body.name);
        if (match.hasMatch()) highest = std::max(highest, match.captured(1).toInt());
        else if (body.name == label) highest = std::max(highest, 1);  // a plain "Peg" counts as the first
    }
    return QStringLiteral("%1 %2").arg(label).arg(highest + 1);
}

int addBody(Document* document, const QString& name, std::vector<Feature> features, const Affine& transform) {
    Body body;
    body.id = document->nextBodyId++;
    body.name = name;
    body.features = std::move(features);
    body.transform = transform;
    document->bodies.push_back(std::move(body));
    return int(document->bodies.size()) - 1;
}

int bodyIndex(const Document& document, int id) {
    for (int i = 0; i < int(document.bodies.size()); ++i)
        if (document.bodies[i].id == id) return i;
    return -1;
}

bool inContext(const Body& body, const std::vector<int>& context) {
    return body.groups.size() >= context.size() && std::equal(context.begin(), context.end(), body.groups.begin());
}

std::vector<int> unitOf(const Document& document, int body, const std::vector<int>& context) {
    const Body& clicked = document.bodies[body];
    const int group = inContext(clicked, context) ? groupAtLevel(clicked, context.size()) : 0;
    if (group == 0) return {body};
    std::vector<int> members;
    for (int i = 0; i < int(document.bodies.size()); ++i)
        if (inContext(document.bodies[i], context) && groupAtLevel(document.bodies[i], context.size()) == group) members.push_back(i);
    return members;
}

bool isGroupUnit(const Document& document, int body, const std::vector<int>& context) {
    const Body& b = document.bodies[body];
    return inContext(b, context) && groupAtLevel(b, context.size()) != 0;
}

bool groupBodies(Document* document, const std::vector<int>& bodies, const std::vector<int>& context) {
    // Count whole units: two bodies already grouped together are one thing.
    std::set<int> units;
    for (int b : bodies) {
        const int group = groupAtLevel(document->bodies[b], context.size());
        units.insert(group != 0 ? -group : document->bodies[b].id);
    }
    if (units.size() < 2) return false;
    const int id = document->nextGroupId++;
    for (int b : bodies) {
        auto& groups = document->bodies[b].groups;
        groups.insert(groups.begin() + std::min(context.size(), groups.size()), id);
    }
    return true;
}

bool ungroupBodies(Document* document, const std::vector<int>& bodies, const std::vector<int>& context) {
    std::set<int> dissolve;
    for (int b : bodies)
        if (const int group = groupAtLevel(document->bodies[b], context.size())) dissolve.insert(group);
    if (dissolve.empty()) return false;
    for (Body& body : document->bodies) {
        if (!inContext(body, context) || !dissolve.count(groupAtLevel(body, context.size()))) continue;
        body.groups.erase(body.groups.begin() + context.size());
    }
    return true;
}

bool ungroupAll(Document* document, const std::vector<int>& bodies, const std::vector<int>& context) {
    std::set<int> dissolve;
    for (int b : bodies)
        if (const int group = groupAtLevel(document->bodies[b], context.size())) dissolve.insert(group);
    if (dissolve.empty()) return false;
    for (Body& body : document->bodies)
        if (inContext(body, context) && dissolve.count(groupAtLevel(body, context.size()))) body.groups.resize(context.size());
    return true;
}

void transformBodies(Document* document, const std::vector<int>& bodies, const Affine& change) {
    for (int b : bodies) document->bodies[b].transform = change * document->bodies[b].transform;
}

int combineBodies(Document* document, int target, const std::vector<int>& sources, Op op) {
    Body& kept = document->bodies[target];
    const Affine toTarget = kept.transform.inverse();
    for (int s : sources) {
        if (s == target) continue;
        const Body& source = document->bodies[s];
        Feature part;
        part.name = source.name;
        part.op = op;
        part.shape = Shape::Compound;
        part.children = source.features;
        part.matrix = toTarget * source.transform;
        kept.features.push_back(std::move(part));
    }
    const int keptId = kept.id;
    std::vector<int> doomed(sources.begin(), sources.end());
    std::sort(doomed.rbegin(), doomed.rend());
    for (int s : doomed)
        if (s != target) document->bodies.erase(document->bodies.begin() + s);
    return bodyIndex(*document, keptId);
}

void splitBody(Document* document, int body, V3 point, V3 normal, double size) {
    const V3 n = normalized(normal);
    const V3 helper = std::abs(n.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
    Prism slab;
    slab.u = normalized(cross(helper, n));
    slab.v = cross(n, slab.u);
    slab.origin = point;
    slab.length = size;
    slab.loops = {{{-size, -size}, {size, -size}, {size, size}, {-size, size}}};

    Feature beyond;
    beyond.name = QStringLiteral("Split");
    beyond.shape = Shape::Prisms;
    beyond.prisms = {slab};
    beyond.matrix = document->bodies[body].transform.inverse();

    Body other = document->bodies[body];
    other.id = document->nextBodyId++;
    other.name = document->bodies[body].name + QStringLiteral(" (split)");
    beyond.op = Op::Subtract;
    document->bodies[body].features.push_back(beyond);
    beyond.op = Op::Intersect;
    other.features.push_back(beyond);
    document->bodies.insert(document->bodies.begin() + body + 1, std::move(other));
}

void replaceWithPieces(Document* document, int body, const std::vector<Mesh>& pieces) {
    const Body original = document->bodies[body];
    document->bodies.erase(document->bodies.begin() + body);
    for (size_t i = 0; i < pieces.size(); ++i) {
        Feature solid;
        solid.name = original.name;
        solid.shape = Shape::Mesh;
        solid.meshPositions = pieces[i].positions;
        solid.meshIndices = pieces[i].indices;
        Body piece;
        piece.id = document->nextBodyId++;
        piece.name = QStringLiteral("%1 (%2)").arg(original.name).arg(i + 1);
        piece.features = {std::move(solid)};
        piece.groups = original.groups;
        document->bodies.insert(document->bodies.begin() + body + int(i), std::move(piece));
    }
}

Affine snapTransform(V3 fromPoint, V3 fromNormal, V3 toPoint, V3 toNormal) {
    const V3 a = normalized(fromNormal);
    const V3 b = normalized(toNormal) * -1.0;
    const double cosine = std::clamp(dot(a, b), -1.0, 1.0);
    Affine turn;
    if (cosine < 1 - 1e-9) {
        V3 axis = cross(a, b);
        if (length(axis) < 1e-9) axis = std::abs(a.x) < 0.9 ? cross(a, V3{1, 0, 0}) : cross(a, V3{0, 1, 0});
        turn = Affine::rotation(axis, std::acos(cosine) * 180.0 / std::numbers::pi, {});
    }
    return Affine::translation(toPoint) * turn * Affine::translation(fromPoint * -1.0);
}

V3 alignOffset(const std::array<double, 3>& lowA, const std::array<double, 3>& highA, const std::array<double, 3>& lowB,
               const std::array<double, 3>& highB, const std::array<AlignMode, 3>& modes) {
    double shift[3] = {0, 0, 0};
    for (int a = 0; a < 3; ++a) {
        switch (modes[a]) {
            case AlignMode::None: break;
            case AlignMode::Min: shift[a] = lowB[a] - lowA[a]; break;
            case AlignMode::Centre: shift[a] = (lowB[a] + highB[a]) / 2 - (lowA[a] + highA[a]) / 2; break;
            case AlignMode::Max: shift[a] = highB[a] - highA[a]; break;
        }
    }
    return {shift[0], shift[1], shift[2]};
}

}  // namespace td
