#include "ai/CadPrompt.h"

#include "core/Affine.h"
#include "core/Geometry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace td {

QString cadSystemPrompt() {
    return QStringLiteral(R"PROMPT(You are the design engine of Cubra, a CAD program for FDM 3D printing. Turn the user's request into a list of solid features.

Units are millimetres. Z is up. The print bed is the plane z=0 and the origin is the centre of the bed.

Boxes, wedges, cylinders, tori, stars, gears and coils are placed by the centre of their BOTTOM: x, y, z give that point. A box with z=0 rests on the bed and reaches z=height.
Spheres, ellipsoids, capsules and curves are placed by their CENTRE: x, y, z give the middle of the shape (for a curve, the middle of its rod). A sphere of diameter D resting on the bed has z = D/2.

Features apply in order. "add" merges the shape into the design. "subtract" cuts it from everything before it. "intersect" keeps only the overlap. The first feature must be "add", and a cut must come after the solid it cuts.

Shapes and their fields:
- box: width (X), depth (Y), height (Z). Optional corner (edge rounding radius).
- wedge: width (X), depth (Y), height (Z). A ramp, tallest along its -X edge and sloping down to nothing at +X.
- cylinder: diameter, height. Optional top_diameter makes a cone. Optional sides makes a polygon prism, for example sides=6 for a hexagon, with diameter measured across the corners. Optional corner rounds the two rim edges of a round cylinder, like a tyre.
- sphere: diameter.
- ellipsoid: width (X), depth (Y), height (Z). A stretched sphere: use it for bodies, heads, snouts, paws, eggs and other rounded forms.
- capsule: diameter, height (the full length, end to end, including the rounded ends). A rod with round ends, upright along Z: use it for arms, legs, necks, tails, fingers and handles. ry=90 lays it along X; rx=90 lays it along Y; use other angles to tilt it.
- curve: diameter, top_diameter (the far end; leave it out for an even rod), height (the full length, end to end), bend (degrees it turns from end to end, 0 to 330). A round rod bent along an arc, with round ends, starting upright along Z and curling toward +X. Use it for anything curved: bananas, curled tails, horns, tusks, cup handles, hooks, croissants, worms. rx=-90 lays it flat along Y, curling toward +X.
- torus: diameter (overall outer diameter), tube_diameter.
- coil: diameter (overall outside), height, wire_diameter, turns. A helical spring wound around Z: use it for any spring, coil, helix or spiral.
- gear: teeth, height. Optional diameter (overall), bore (centre hole diameter). Teeth point outward. Set internal=true only when the user asks for teeth on the inside, such as a ring gear.
- star: points, diameter (tip to tip), inner_diameter (across the inner corners, about 0.4 of the diameter), height. A flat star, one tip pointing along +Y.
Optional rx, ry, rz rotate in degrees around the feature's placement point (the centre, for spheres, ellipsoids, capsules and curves). Leave out every field that does not apply.

Rules:
1. Return the COMPLETE design every time. Copy unchanged features exactly as given and change only what was asked.
2. A cut that goes all the way through must stick out 1 mm past both faces. For a hole through a plate of thickness T resting on the bed use z=-1 and height=T+2.
3. A pocket open at the top starts at its floor and extends 1 mm above the top face. For a cup of height H with floor thickness F, the cutter has z=F and height=H-F+1.
4. Dimensions the user states must appear exactly. For a wall thickness W, the inner cutter is 2*W smaller than the outer size.
5. "x mm from the edge" means the hole centre is x mm from that edge. For a part of width W centred on the origin, that is at +/-(W/2 - x).
6. Keep the design on the bed unless asked otherwise.
7. Give every shape all of its size fields. When the user does not state a size, use 20 mm. A flat shape name such as "pentagon" or "hexagon" means a prism: a cylinder with that many sides. A star is the star shape, never a polygon. A gear is the gear shape, never a star or a cut-out star, with exactly the number of teeth asked for. A spring, coil, helix or spiral is the coil shape, never a cylinder.
8. Figures, animals, toys and other organic things are built from simple shapes, with a separate named feature for each part (head, body, ear_left, ear_right, arm_left...). Pick the shape that matches each part:
   - Bodies: a capsule. A four-legged animal's body lies along Y (rx=90), with the head at -Y; a standing person or bear's body is upright.
   - Heads: a sphere. Snouts, cheeks, paws and bellies: ellipsoids.
   - Arms, legs, necks and tails: capsules. Legs reach down to the bed. A curled tail, a horn or a tusk: a curve.
   - Vehicles: boxes with rounded corners for the body and cabin; wheels are short cylinders turned on their side (ry=90 for a car pointing along Y), never spheres.
   - Fruit and food: one shape for the whole thing where one fits. A banana is a single curve, an apple a sphere, a pear an ellipsoid with a sphere on top.
   - Pointed parts, such as cat, fox and wolf ears, horns, beaks, claws and spikes: a cone, which is a cylinder with top_diameter 0; with sides=3 it is a flat triangle, and rz=90 turns a flat side to face the front. Rounded ears, such as a bear's or a mouse's: a sphere or a flat ellipsoid.
   Left and right parts mirror each other in x. Every part must sink a few mm into the part it attaches to, so the whole thing prints as one piece; nothing may float. Its lowest part rests on the bed. Unless a size is given, make a figure about 80 mm tall.
9. "reply" is one short sentence describing what you did. If the request is only a question and needs no design change, answer it in "reply" and return an empty "features" list.

Example request: a 40 by 30 by 5 mm plate with a 6 mm hole in the middle
Example response:
{"reply":"Made a 40x30x5 mm plate with a 6 mm centre hole.","features":[{"name":"Plate","op":"add","shape":"box","width":40,"depth":30,"height":5},{"name":"Hole","op":"subtract","shape":"cylinder","diameter":6,"height":7,"z":-1}]}

Example request: a snowman with stick arms
Example response:
{"reply":"Made a 70 mm snowman with two stick arms.","features":[{"name":"base","op":"add","shape":"sphere","diameter":36,"z":18},{"name":"middle","op":"add","shape":"sphere","diameter":26,"z":44},{"name":"head","op":"add","shape":"sphere","diameter":18,"z":62},{"name":"nose","op":"add","shape":"capsule","diameter":4,"height":10,"y":-10,"z":62,"rx":90},{"name":"arm_left","op":"add","shape":"capsule","diameter":3,"height":22,"x":-18,"z":48,"ry":-60},{"name":"arm_right","op":"add","shape":"capsule","diameter":3,"height":22,"x":18,"z":48,"ry":60}]}

Example request: a car
Example response:
{"reply":"Made a 100 mm toy car.","features":[{"name":"body","op":"add","shape":"box","width":44,"depth":100,"height":20,"corner":3,"z":8},{"name":"cabin","op":"add","shape":"box","width":38,"depth":50,"height":16,"corner":3,"y":5,"z":27},{"name":"wheel_front_left","op":"add","shape":"cylinder","diameter":22,"height":8,"x":-26,"y":-30,"z":11,"ry":90},{"name":"wheel_front_right","op":"add","shape":"cylinder","diameter":22,"height":8,"x":18,"y":-30,"z":11,"ry":90},{"name":"wheel_back_left","op":"add","shape":"cylinder","diameter":22,"height":8,"x":-26,"y":30,"z":11,"ry":90},{"name":"wheel_back_right","op":"add","shape":"cylinder","diameter":22,"height":8,"x":18,"y":30,"z":11,"ry":90}]}

Example request: a banana
Example response:
{"reply":"Made a 150 mm banana lying on its side.","features":[{"name":"banana","op":"add","shape":"curve","diameter":30,"top_diameter":18,"height":150,"bend":70,"z":15,"rx":-90},{"name":"stem","op":"add","shape":"capsule","diameter":7,"height":16,"x":56.6,"y":40.4,"z":15,"rx":-90,"rz":-70}]}

Example request: a cat
Example response:
{"reply":"Made a standing cat, 65 mm tall.","features":[{"name":"body","op":"add","shape":"capsule","diameter":26,"height":60,"z":30,"rx":90},{"name":"head","op":"add","shape":"sphere","diameter":24,"y":-32,"z":46},{"name":"ear_left","op":"add","shape":"cylinder","diameter":10,"top_diameter":0,"sides":3,"height":12,"x":-6,"y":-32,"z":53,"rz":90,"ry":-12},{"name":"ear_right","op":"add","shape":"cylinder","diameter":10,"top_diameter":0,"sides":3,"height":12,"x":6,"y":-32,"z":53,"rz":90,"ry":12},{"name":"snout","op":"add","shape":"ellipsoid","width":10,"depth":8,"height":7,"y":-43,"z":44},{"name":"leg_front_left","op":"add","shape":"capsule","diameter":9,"height":30,"x":-8,"y":-20,"z":15},{"name":"leg_front_right","op":"add","shape":"capsule","diameter":9,"height":30,"x":8,"y":-20,"z":15},{"name":"leg_back_left","op":"add","shape":"capsule","diameter":9,"height":30,"x":-8,"y":20,"z":15},{"name":"leg_back_right","op":"add","shape":"capsule","diameter":9,"height":30,"x":8,"y":20,"z":15},{"name":"tail","op":"add","shape":"capsule","diameter":6,"height":36,"y":39.5,"z":50,"rx":-40}]}
)PROMPT");
}

QJsonObject cadResponseSchema() {
    const QJsonObject number{{"type", "number"}};
    const QJsonObject properties{
        {"name", QJsonObject{{"type", "string"}}},
        {"op", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"add", "subtract", "intersect"}}}},
        {"shape", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"box", "wedge", "cylinder", "sphere", "ellipsoid", "capsule", "curve", "torus", "star", "gear", "coil"}}}},
        {"width", number},
        {"depth", number},
        {"height", number},
        {"corner", number},
        {"diameter", number},
        {"top_diameter", number},
        {"tube_diameter", number},
        {"sides", QJsonObject{{"type", "integer"}}},
        {"points", QJsonObject{{"type", "integer"}}},
        {"inner_diameter", number},
        {"teeth", QJsonObject{{"type", "integer"}}},
        {"bore", number},
        {"internal", QJsonObject{{"type", "boolean"}}},
        {"wire_diameter", number},
        {"turns", number},
        {"bend", number},
        {"x", number},
        {"y", number},
        {"z", number},
        {"rx", number},
        {"ry", number},
        {"rz", number},
    };
    const QJsonObject feature{{"type", "object"}, {"properties", properties}, {"required", QJsonArray{"name", "op", "shape"}}};
    return QJsonObject{{"type", "object"},
                       {"properties", QJsonObject{{"reply", QJsonObject{{"type", "string"}}},
                                                  {"features", QJsonObject{{"type", "array"}, {"items", feature}}}}},
                       {"required", QJsonArray{"reply", "features"}}};
}

QString cadUserMessage(const std::vector<Feature>& current, int bedWidth, int bedDepth, const QString& request) {
    QJsonArray array;
    // Direct edits are large point lists a model cannot usefully rewrite. The app keeps them.
    for (const Feature& f : current)
        if (f.shape != Shape::Prisms && f.shape != Shape::Compound && f.shape != Shape::Mesh) array.append(featureToAiJson(f));
    return QStringLiteral("Print bed: %1 x %2 mm.\nCurrent design: %3\nRequest: %4")
        .arg(bedWidth)
        .arg(bedDepth)
        .arg(QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)), request);
}

namespace {

// Size fields each shape needs. Small models often leave some out.
QStringList requiredSizes(const QString& shape) {
    if (shape == "box" || shape == "wedge") return {"width", "depth", "height"};
    if (shape == "cylinder") return {"diameter", "height"};
    if (shape == "sphere") return {"diameter"};
    if (shape == "ellipsoid") return {"width", "depth", "height"};
    if (shape == "capsule" || shape == "curve") return {"diameter", "height"};
    if (shape == "torus") return {"diameter", "tube_diameter"};
    if (shape == "star") return {"diameter", "height"};
    if (shape == "gear") return {"height"};
    if (shape == "coil") return {"diameter", "height"};  // the diameter follows from the teeth when unstated
    return {};
}

// Fills sizes the model left out (or gave as 0) with the default, and lists what was filled.
QJsonValue fillMissingSizes(const QJsonValue& features, QStringList* filled) {
    if (!features.isArray()) return features;
    QJsonArray out;
    for (const QJsonValue& value : features.toArray()) {
        QJsonObject f = value.toObject();
        const QString shape = f.value("shape").toString();
        // A curve left without a length or a bend: long and bent, which is why it was chosen.
        if (shape == "curve") {
            const double thick = std::max(f.value("diameter").toDouble(kDefaultAiSize), f.value("top_diameter").toDouble(0));
            if (f.value("height").toDouble() < thick) f["height"] = std::round(thick * 5);
            if (!f.contains("bend")) f["bend"] = 60;
        }
        // An ellipsoid missing a size stays round: it takes the size it was given (or a
        // diameter, which models sometimes give instead) rather than flattening to the default.
        if (shape == "ellipsoid") {
            double given = f.value("diameter").toDouble();
            if (given <= 0)
                for (const char* key : {"width", "depth", "height"}) given = std::max(given, f.value(QLatin1String(key)).toDouble());
            if (given > 0)
                for (const char* key : {"width", "depth", "height"})
                    if (f.value(QLatin1String(key)).toDouble() <= 0) f[QLatin1String(key)] = given;
            f.remove(QStringLiteral("diameter"));
        }
        for (const QString& key : requiredSizes(shape)) {
            const QJsonValue v = f.value(key);
            if (!v.isUndefined() && !v.isNull() && !(v.isDouble() && v.toDouble() == 0)) continue;
            double size = kDefaultAiSize;
            // A 20 mm tube needs a ring over 40 mm across; keep the ring a ring.
            if (key == "tube_diameter") size = std::min(size, f.value("diameter").toDouble(kDefaultAiSize * 3) / 4);
            f[key] = size;
            const QString label = QString(key).replace('_', ' ');
            if (!filled->contains(label)) filled->append(label);
        }
        out.append(value.isObject() ? QJsonValue(f) : value);
    }
    return out;
}

bool placedByCentre(Shape shape) { return shape == Shape::Sphere || shape == Shape::Capsule || shape == Shape::Curve; }

// From the bottom-centre placement point to the middle of the shape, once stretched and turned.
V3 toCentre(const Feature& f) {
    const V3 middle = f.shape == Shape::Curve ? curveSpine(f, 0.5) : V3{0, 0, (f.shape == Shape::Capsule ? f.height : f.diameter) / 2};
    const Affine turn = Affine::rotation({0, 0, 1}, f.rz, {}) * Affine::rotation({0, 1, 0}, f.ry, {}) * Affine::rotation({1, 0, 0}, f.rx, {});
    return turn.direction(V3{middle.x * f.sx, middle.y * f.sy, middle.z * f.sz});
}

double rounded(double v) { return std::round(v * 1000) / 1000; }

// The model's view of a feature back into Cubra's: ellipsoids become stretched
// spheres, and centres become bottom placement points.
QJsonValue fromModel(const QJsonValue& features) {
    if (!features.isArray()) return features;
    QJsonArray out;
    for (const QJsonValue& value : features.toArray()) {
        QJsonObject o = value.toObject();
        const QString shape = o.value("shape").toString();
        if (shape == "ellipsoid") {
            const double w = o.value("width").toDouble(), d = o.value("depth").toDouble(), h = o.value("height").toDouble();
            o["shape"] = QStringLiteral("sphere");
            o["diameter"] = h;
            if (h > 0) {
                o["sx"] = w / h;
                o["sy"] = d / h;
            }
            for (const char* key : {"width", "depth", "height", "sz"}) o.remove(QLatin1String(key));
        }
        if (shape == "capsule" && o.value("height").toDouble() < o.value("diameter").toDouble()) o["height"] = o.value("diameter");
        Feature f;
        QString ignored;
        if ((shape == "sphere" || shape == "ellipsoid" || shape == "capsule" || shape == "curve") && featureFromJson(o, &f, &ignored)) {
            const V3 bottom = V3{f.x, f.y, f.z} - toCentre(f);
            o["x"] = rounded(bottom.x);
            o["y"] = rounded(bottom.y);
            o["z"] = rounded(bottom.z);
        }
        out.append(o);
    }
    return out;
}

}  // namespace

QJsonObject featureToAiJson(const Feature& f) {
    QJsonObject o = featureToJson(f);
    if (!placedByCentre(f.shape)) return o;
    const V3 centre = V3{f.x, f.y, f.z} + toCentre(f);
    o["x"] = rounded(centre.x);
    o["y"] = rounded(centre.y);
    o["z"] = rounded(centre.z);
    if (f.shape == Shape::Sphere && (f.sx != 1 || f.sy != 1 || f.sz != 1)) {
        o["shape"] = QStringLiteral("ellipsoid");
        o["width"] = rounded(f.diameter * f.sx);
        o["depth"] = rounded(f.diameter * f.sy);
        o["height"] = rounded(f.diameter * f.sz);
        for (const char* key : {"diameter", "sx", "sy", "sz"}) o.remove(QLatin1String(key));
    }
    return o;
}

QStringList designProblems(const std::vector<Feature>& features) {
    QStringList problems;
    for (const LoosePart& part : looseParts(features))
        problems << (part.gap < 1 ? QStringLiteral("\"%1\" does not touch the rest of the model; it sits just off \"%2\".").arg(part.name, part.nearest)
                                  : QStringLiteral("\"%1\" does not touch the rest of the model; it is about %2 mm from \"%3\".").arg(part.name).arg(part.gap, 0, 'f', 0).arg(part.nearest));
    return problems;
}

bool asksForNewDesign(const QString& request) {
    static const QRegularExpression fresh(
        QStringLiteral("^\\W*(please\\s+|can you\\s+|could you\\s+|now\\s+)*(create|make|build|design|generate|draw|model|give me)\\s+(me\\s+)?(a|an|another|one|two|three|four|five|some|\\d+)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    // "Make a hole through it" works on the design on screen.
    static const QRegularExpression refersBack(QStringLiteral("\\b(it|its|this|that|them|these|those)\\b"), QRegularExpression::CaseInsensitiveOption);
    return fresh.match(request).hasMatch() && !refersBack.match(request).hasMatch();
}

bool sizeStatedIn(const QString& request) {
    static const QRegularExpression size(QStringLiteral("\\d\\s*(mm|cm|m|in|inch|inches|millimet\\w*|centimet\\w*|\")(?![a-z])"),
                                         QRegularExpression::CaseInsensitiveOption);
    return size.match(request).hasMatch();
}

bool isFigure(const std::vector<Feature>& features) {
    int round = 0, added = 0;
    for (const Feature& f : features) {
        if (f.op != Op::Add) continue;
        ++added;
        if (placedByCentre(f.shape)) ++round;
    }
    return added >= 3 && round * 2 > added;
}

void scaleDesign(std::vector<Feature>* features, double k) {
    for (Feature& f : *features) {
        for (double* v : {&f.width, &f.depth, &f.height, &f.corner, &f.diameter, &f.tubeDiameter, &f.innerDiameter, &f.x, &f.y, &f.z}) *v = rounded(*v * k);
        if (f.topDiameter >= 0) f.topDiameter = rounded(f.topDiameter * k);
        for (int i = 9; i < 12; ++i) f.matrix.m[i] *= k;
    }
}

double seatOnBed(std::vector<Feature>* features) {
    const EvalResult result = evaluate(*features);
    if (!result.ok || result.mesh.empty()) return 0;
    const double shift = -result.min[2];
    if (std::abs(shift) < 0.05) return 0;
    for (Feature& f : *features) f.z = rounded(f.z + shift);
    return shift;
}

int polygonSidesIn(const QString& request) {
    // Stems allow common misspellings, such as "pentegon".
    static const std::pair<const char*, int> kNames[] = {
        {"triangle|triangular", 3}, {"pent[a-z]*gonal|pent[a-z]*gon", 5}, {"hex[a-z]*gon(al)?", 6}, {"hept[a-z]*gon(al)?", 7},
        {"oct[a-z]*gon(al)?", 8}, {"non[a-z]*gon(al)?", 9}, {"dec[a-z]*gon(al)?", 10}, {"dodec[a-z]*gon(al)?", 12}};
    const QString text = request.toLower();
    int found = 0;
    for (const auto& [stem, sides] : kNames) {
        const QRegularExpression word(QStringLiteral("\\b(%1)\\b").arg(QLatin1String(stem)));
        if (word.match(text).hasMatch()) {
            if (found && found != sides) return 0;  // two different polygons: leave the model's choice alone
            found = sides;
        }
    }
    return found;
}

int starPointsIn(const QString& request) {
    const QString text = request.toLower();
    if (!QRegularExpression(QStringLiteral("\\bstars?\\b")).match(text).hasMatch()) return 0;
    const auto digits = QRegularExpression(QStringLiteral("(\\d+)\\s*-?\\s*(point|pointed|arm|armed)")).match(text);
    if (digits.hasMatch()) return std::clamp(digits.captured(1).toInt(), 3, 64);
    static const char* kWords[] = {"three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve"};
    for (int i = 0; i < 10; ++i)
        if (QRegularExpression(QStringLiteral("\\b%1\\s*-?\\s*(point|pointed|arm|armed)").arg(QLatin1String(kWords[i]))).match(text).hasMatch()) return i + 3;
    return 5;
}

GearRequest gearRequestIn(const QString& request) {
    GearRequest wanted;
    const QString text = request.toLower();
    if (!QRegularExpression(QStringLiteral("\\bgears?\\b|\\bcogs?\\b|\\bsprockets?\\b")).match(text).hasMatch()) return wanted;
    wanted.asked = true;
    const auto count = QRegularExpression(QStringLiteral("(\\d+)\\s*-?\\s*(teeth|tooth|toothed)")).match(text);
    if (count.hasMatch()) wanted.teeth = count.captured(1).toInt();
    wanted.internal = QRegularExpression(QStringLiteral("\\b(inside|internal|inner|inward|ring gear|annular)\\b")).match(text).hasMatch();
    return wanted;
}

CoilRequest coilRequestIn(const QString& request) {
    CoilRequest wanted;
    const QString text = request.toLower();
    if (!QRegularExpression(QStringLiteral("\\b(springs?|coils?|coiled|helix|helical|helices|spirals?|spiralling|spiraling)\\b")).match(text).hasMatch()) return wanted;
    wanted.asked = true;
    const auto turns = QRegularExpression(QStringLiteral("(\\d+(\\.\\d+)?)\\s*(turns?|coils?|loops?|revolutions?|rotations?)")).match(text);
    if (turns.hasMatch()) wanted.turns = turns.captured(1).toDouble();
    return wanted;
}

bool parseAssistantReply(const QString& content, AssistantReply* reply, QString* error, const QString& request) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("the response was not valid JSON");
        return false;
    }
    const QJsonObject root = doc.object();
    AssistantReply parsed;
    parsed.text = root.value("reply").toString();
    QStringList filled;
    if (!featuresFromJson(fromModel(fillMissingSizes(root.value("features"), &filled)), &parsed.features, error)) return false;
    if (!filled.isEmpty())
        parsed.text += QStringLiteral(" (No %1 was given, so Cubra used %2 mm.)").arg(filled.join(QStringLiteral(", "))).arg(kDefaultAiSize);

    // Small models sometimes drop "sides" and return a round cylinder for "make a pentagon".
    // Only when no added cylinder has sides at all; holes are left round.
    if (const int sides = starPointsIn(request) ? 0 : polygonSidesIn(request)) {
        bool anySides = false;
        for (const Feature& f : parsed.features) anySides |= f.shape == Shape::Cylinder && f.sides > 0;
        if (!anySides) {
            bool changed = false;
            for (Feature& f : parsed.features)
                if (f.shape == Shape::Cylinder && f.op == Op::Add) f.sides = sides, changed = true;
            if (changed) parsed.text += QStringLiteral(" (The model made it round, so Cubra gave it %1 sides.)").arg(sides);
        }
    }

    // Asked for a gear, small models cut a star out of a disc, which puts the teeth inside,
    // and they miscount the teeth. What the user said wins.
    if (const GearRequest wanted = gearRequestIn(request); wanted.asked) {
        const auto isGear = [](const Feature& f) { return f.shape == Shape::Gear; };
        if (std::none_of(parsed.features.begin(), parsed.features.end(), isGear)) {
            // Rebuild the biggest added round shape as a real gear, and drop the fake teeth.
            Feature* body = nullptr;
            for (Feature& f : parsed.features)
                if (f.op == Op::Add && (f.shape == Shape::Cylinder || f.shape == Shape::Star) && (!body || f.diameter > body->diameter)) body = &f;
            if (body) {
                const int teeth = wanted.teeth > 0 ? wanted.teeth : (body->shape == Shape::Star && body->sides > 0 ? body->sides : 20);
                Feature gear = *body;
                gear.shape = Shape::Gear;
                gear.sides = teeth;
                gear.internal = wanted.internal;
                gear.innerDiameter = 0;
                gear.topDiameter = -1;
                std::vector<Feature> kept;
                for (const Feature& f : parsed.features) {
                    if (&f == body) kept.push_back(gear);
                    else if (!(f.op == Op::Subtract && f.shape == Shape::Star)) kept.push_back(f);
                }
                parsed.features = std::move(kept);
                parsed.text += QStringLiteral(" (The model had no gear, so Cubra made a real %1-tooth gear.)").arg(teeth);
            }
        }
        for (Feature& f : parsed.features) {
            if (!isGear(f) || f.op != Op::Add) continue;
            if (wanted.teeth > 0 && f.sides != wanted.teeth) {
                parsed.text += QStringLiteral(" (Cubra set the teeth to the %1 you asked for.)").arg(wanted.teeth);
                f.sides = wanted.teeth;
            }
            if (f.internal != wanted.internal) {
                f.internal = wanted.internal;
                parsed.text += wanted.internal ? QStringLiteral(" (Teeth put on the inside, as asked.)") : QStringLiteral(" (Teeth put on the outside.)");
            }
            // Teeth too fine to print: grow the gear to 1 mm teeth rather than fail.
            const int teeth = f.sides > 0 ? f.sides : 20;
            if (teeth <= kMaxGearTeeth && gearModule(f) < kMinGearModule && teeth >= 6) {
                f.diameter = f.internal ? teeth + 8.5 : teeth + 2.0;
                parsed.text += QStringLiteral(" (Sized to %1 mm across so the teeth are large enough to print.)").arg(f.diameter);
            }
        }
    }

    // Asked for a spring, small models return a plain rod, or refuse. Wind what they made into
    // a coil; with nothing to wind, make a standard one.
    if (const CoilRequest wanted = coilRequestIn(request); wanted.asked) {
        const auto isCoil = [](const Feature& f) { return f.shape == Shape::Coil; };
        if (std::none_of(parsed.features.begin(), parsed.features.end(), isCoil)) {
            Feature* rod = nullptr;
            for (Feature& f : parsed.features)
                if (f.op == Op::Add && (f.shape == Shape::Cylinder || f.shape == Shape::Torus) && (!rod || f.height > rod->height)) rod = &f;
            Feature coil;
            coil.shape = Shape::Coil;
            coil.name = QStringLiteral("Spring");
            coil.diameter = 20;
            coil.height = 30;
            if (rod) {
                coil = *rod;
                coil.shape = Shape::Coil;
                coil.topDiameter = -1;
                coil.sides = 0;
                // A thin rod was the model's idea of the wire, not of the coil.
                if (rod->height > rod->diameter * 3) coil.diameter = std::max(rod->diameter * 4, 20.0);
            }
            coil.tubeDiameter = std::max(0.8, coil.diameter / 10);
            coil.turns = std::max(1.0, std::floor((coil.height - coil.tubeDiameter) / (coil.tubeDiameter * 2)));
            if (rod) *rod = coil;
            else parsed.features = {coil};
            parsed.text = rod ? parsed.text + QStringLiteral(" (The model made a plain rod, so Cubra wound it into a %1 mm spring.)").arg(coil.diameter)
                              : QStringLiteral("Made a %1 mm spring, %2 mm tall.").arg(coil.diameter).arg(coil.height);
        }
        if (wanted.turns > 0)
            for (Feature& f : parsed.features)
                if (isCoil(f) && f.turns != wanted.turns) {
                    f.turns = wanted.turns;
                    // Make room for the turns asked for rather than refuse them.
                    const double needed = f.turns * f.tubeDiameter * 1.5 + f.tubeDiameter;
                    if (f.height < needed) f.height = std::ceil(needed);
                }
    }

    // Asked for a star, a model without the idea tends to return a pentagon.
    if (const int points = gearRequestIn(request).asked ? 0 : starPointsIn(request)) {
        const bool hasStar = std::any_of(parsed.features.begin(), parsed.features.end(), [](const Feature& f) { return f.shape == Shape::Star; });
        if (!hasStar) {
            bool changed = false;
            for (Feature& f : parsed.features) {
                if (f.shape != Shape::Cylinder || f.op != Op::Add) continue;
                f.shape = Shape::Star;
                f.sides = points;
                f.innerDiameter = f.diameter * 0.4;
                changed = true;
            }
            if (changed) parsed.text += QStringLiteral(" (The model made a plain prism, so Cubra turned it into a %1-pointed star.)").arg(points);
        }
    }

    // Small models drop the fields that make a figure's parts look right. Pointed ears
    // come back as round cones: make them flat triangles, a flat side to the front.
    for (Feature& f : parsed.features) {
        const bool cone = f.shape == Shape::Cylinder && f.topDiameter == 0 && f.sides == 0 && f.op == Op::Add;
        if (cone && f.name.contains(QStringLiteral("ear"), Qt::CaseInsensitive)) {
            f.sides = 3;
            if (f.rz == 0) f.rz = 90;
        }
    }
    // A four-legged animal's body stands upright like a post: lay it along Y, about its middle.
    const int legs = int(std::count_if(parsed.features.begin(), parsed.features.end(),
                                       [](const Feature& f) { return f.op == Op::Add && f.name.contains(QStringLiteral("leg"), Qt::CaseInsensitive); }));
    if (legs >= 4)
        for (Feature& f : parsed.features) {
            if (f.shape != Shape::Capsule || f.op != Op::Add || f.rx != 0 || f.ry != 0 || !f.name.contains(QStringLiteral("body"), Qt::CaseInsensitive)) continue;
            const V3 centre = V3{f.x, f.y, f.z} + toCentre(f);
            f.rx = 90;
            const V3 bottom = centre - toCentre(f);
            f.x = rounded(bottom.x);
            f.y = rounded(bottom.y);
            f.z = rounded(bottom.z);
        }

    // Vehicles: small models lay wheels flat like coins, make them balls, or line them up
    // along the wrong side. Stand them upright as tyres along the body's long sides.
    {
        const auto named = [](const Feature& f, std::initializer_list<const char*> words) {
            for (const char* w : words)
                if (f.name.contains(QLatin1String(w), Qt::CaseInsensitive)) return true;
            return false;
        };
        const auto squared = [](Feature& f) {
            const double turns = f.rz / 90;
            if (f.rx != 0 || f.ry != 0 || std::abs(turns - std::round(turns)) > 1e-6) return false;
            if (int(std::round(turns)) % 2) std::swap(f.width, f.depth);
            f.rz = 0;
            return true;
        };
        std::vector<Feature*> wheels;
        Feature* body = nullptr;
        double biggest = 0;
        for (Feature& f : parsed.features) {
            if (f.op != Op::Add) continue;
            if (named(f, {"wheel", "tire", "tyre"})) {
                wheels.push_back(&f);
                continue;
            }
            if (f.shape != Shape::Box) continue;
            const double size = f.width * f.depth * f.height * (named(f, {"body", "chassis", "hull", "base"}) ? 100 : 1);
            if (size > biggest) biggest = size, body = &f;
        }
        if (wheels.size() >= 3 && body && squared(*body)) {
            const bool alongY = body->depth >= body->width;
            double& length = alongY ? body->depth : body->width;
            double& across = alongY ? body->width : body->depth;
            if (length > across * 3.5) across = rounded(length * 0.45);
            if (body->corner == 0) body->corner = rounded(std::min(3.0, std::min({length, across, body->height}) / 4));

            double diameter = 0;
            for (const Feature* w : wheels) diameter += w->diameter;
            diameter = rounded(std::clamp(diameter / wheels.size(), length * 0.18, length * 0.32));
            const double radius = diameter / 2;
            const double tread = rounded(diameter * 0.35);

            // Lift the body for ground clearance, keeping it low enough to hold the axles.
            const double floor = body->height > radius * 0.75 ? radius * 0.5 : std::max(0.0, radius - body->height / 2);
            const double dz = floor - body->z;
            for (Feature& f : parsed.features)
                if (std::find(wheels.begin(), wheels.end(), &f) == wheels.end()) f.z = rounded(f.z + dz);

            const double longMid = alongY ? body->y : body->x;
            const double acrossMid = alongY ? body->x : body->y;
            for (Feature& f : parsed.features) {
                if (f.op != Op::Add || f.shape != Shape::Box || !named(f, {"cabin", "cab", "roof", "top", "canopy", "window"}) || !squared(f)) continue;
                double& cabinLength = alongY ? f.depth : f.width;
                double& cabinAcross = alongY ? f.width : f.depth;
                if (cabinLength > length * 0.7) cabinLength = rounded(length * 0.55);
                if (cabinAcross > across * 0.9 || cabinLength > cabinAcross * 3.5) cabinAcross = rounded(across * 0.85);
                if (f.corner == 0) f.corner = rounded(std::min(3.0, std::min({cabinLength, cabinAcross, f.height}) / 4));
                double& cabinLong = alongY ? f.y : f.x;
                double& cabinSide = alongY ? f.x : f.y;
                const double room = (length - cabinLength) / 2;
                cabinLong = rounded(std::clamp(cabinLong, longMid - room, longMid + room));
                cabinSide = rounded(acrossMid);
                f.z = rounded(body->z + body->height - 0.5);
            }

            // Left and right by name where given, otherwise alternately; front first along each side.
            std::vector<Feature*> sides[2];
            for (Feature* w : wheels) {
                const int side = named(*w, {"left"}) ? 0 : named(*w, {"right"}) ? 1 : (sides[0].size() <= sides[1].size() ? 0 : 1);
                sides[side].push_back(w);
            }
            const auto rank = [&](const Feature* w) { return named(*w, {"front"}) ? 0 : named(*w, {"back", "rear"}) ? 2 : 1; };
            const double reach = length / 2 - diameter * 0.75;
            for (int side = 0; side < 2; ++side) {
                auto& list = sides[side];
                std::stable_sort(list.begin(), list.end(), [&](const Feature* a, const Feature* b) { return rank(a) < rank(b); });
                for (size_t i = 0; i < list.size(); ++i) {
                    Feature& w = *list[i];
                    const double t = list.size() == 1 ? 0.5 : double(i) / (list.size() - 1);
                    const double along = longMid - reach + 2 * reach * t;
                    const double out = acrossMid + (side == 0 ? -1 : 1) * across / 2;
                    w.shape = Shape::Cylinder;
                    w.sides = 0;
                    w.topDiameter = -1;
                    w.diameter = diameter;
                    w.height = tread;
                    w.corner = rounded(tread * 0.25);
                    w.sx = w.sy = w.sz = 1;
                    w.rx = alongY ? 0 : 90;
                    w.ry = alongY ? 90 : 0;
                    w.rz = 0;
                    const V3 centre = alongY ? V3{out, along, radius} : V3{along, out, radius};
                    const Affine turn = Affine::rotation({0, 1, 0}, w.ry, {}) * Affine::rotation({1, 0, 0}, w.rx, {});
                    const V3 bottom = centre - turn.direction(V3{0, 0, tread / 2});
                    w.x = rounded(bottom.x);
                    w.y = rounded(bottom.y);
                    w.z = rounded(bottom.z);
                }
            }
            parsed.text += QStringLiteral(" (Cubra stood the wheels upright along the sides of the body.)");
        }
    }

    // A banana or a croissant left standing on end: lay the main curve flat, about its middle.
    {
        Feature* main = nullptr;
        double biggest = 0;
        for (Feature& f : parsed.features) {
            if (f.op != Op::Add) continue;
            const double size = f.height * std::max(f.diameter, f.shape == Shape::Box || f.shape == Shape::Wedge ? f.width : 0.0);
            if (size > biggest) biggest = size, main = &f;
        }
        if (main && main->shape == Shape::Curve && main->rx == 0 && main->ry == 0) {
            const V3 centre = V3{main->x, main->y, main->z} + toCentre(*main);
            main->rx = -90;
            const V3 bottom = centre - toCentre(*main);
            main->x = rounded(bottom.x);
            main->y = rounded(bottom.y);
            main->z = rounded(bottom.z);
        }
    }

    if (!parsed.features.empty()) {
        const EvalResult result = evaluate(parsed.features);
        if (!result.ok) {
            *error = result.error;
            return false;
        }
        if (result.mesh.empty()) {
            *error = QStringLiteral("the features produce an empty solid; check that the first feature is add and that cuts do not remove everything");
            return false;
        }
    }
    *reply = std::move(parsed);
    return true;
}

}  // namespace td
