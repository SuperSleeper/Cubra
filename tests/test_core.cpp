#include "ai/CadPrompt.h"
#include "ai/HelpSession.h"
#include "ai/Manual.h"
#include "ai/GpuProbe.h"
#include "ai/ModelCatalog.h"
#include "core/Document.h"
#include "core/Exporters.h"
#include "core/Geometry.h"
#include "core/Picking.h"
#include "core/Modify.h"
#include "core/Pattern.h"
#include "core/Placement.h"
#include "core/Topology.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <numeric>

static int failures = 0;
#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

using namespace td;

static bool near(double a, double b, double fraction = 0.01) { return std::abs(a - b) <= std::abs(b) * fraction; }

static Feature box(double w, double d, double h) {
    Feature f;
    f.width = w;
    f.depth = d;
    f.height = h;
    return f;
}

static Feature cylinder(double diameter, double height, double z = 0) {
    Feature f;
    f.shape = Shape::Cylinder;
    f.diameter = diameter;
    f.height = height;
    f.z = z;
    return f;
}

static void testGeometry() {
    EvalResult r = evaluate({box(40, 30, 5)});
    CHECK(r.ok && near(r.volume, 6000));
    CHECK(near(r.min[0], -20) && near(r.max[1], 15) && r.min[2] == 0 && near(r.max[2], 5));

    Feature hole = cylinder(6, 7, -1);
    hole.op = Op::Subtract;
    r = evaluate({box(40, 30, 5), hole});
    CHECK(r.ok && near(r.volume, 6000 - std::numbers::pi * 9 * 5));

    Feature keep = box(10, 10, 10);
    keep.op = Op::Intersect;
    r = evaluate({box(20, 20, 5), keep});
    CHECK(r.ok && near(r.volume, 500));

    Feature hex = cylinder(10, 5);
    hex.sides = 6;
    r = evaluate({hex});
    CHECK(r.ok && near(r.volume, 3 * std::sqrt(3.0) / 2 * 25 * 5));

    Feature sphere;
    sphere.shape = Shape::Sphere;
    sphere.diameter = 20;
    r = evaluate({sphere});
    CHECK(r.ok && near(r.volume, 4.0 / 3 * std::numbers::pi * 1000) && near(r.max[2], 20) && std::abs(r.min[2]) < 0.01);

    Feature torus;
    torus.shape = Shape::Torus;
    torus.diameter = 40;
    torus.tubeDiameter = 10;
    r = evaluate({torus});
    CHECK(r.ok && near(r.volume, 2 * std::pow(std::numbers::pi, 2) * 15 * 25) && near(r.max[2], 10) && near(r.max[0], 20));

    Feature rounded = box(20, 20, 20);
    rounded.corner = 3;
    r = evaluate({rounded});
    CHECK(r.ok && r.volume < 8000 && r.volume > 7000 && near(r.max[2], 20) && std::abs(r.min[2]) < 0.01);

    Feature rotated = box(10, 20, 30);
    rotated.rz = 90;
    r = evaluate({rotated});
    CHECK(r.ok && near(r.max[0], 10) && near(r.max[1], 5));

    Feature bad = box(10, 10, 10);
    bad.corner = 5;
    CHECK(!evaluate({bad}).ok);

    Feature onlyCut = box(10, 10, 10);
    onlyCut.op = Op::Subtract;
    r = evaluate({onlyCut});
    CHECK(r.ok && r.mesh.empty());
}

static void testJson() {
    Feature cone = cylinder(20, 10, 2);
    cone.name = "Cone";
    cone.topDiameter = 0;
    cone.op = Op::Subtract;
    cone.rx = 15;
    std::vector<Feature> out;
    QString error;
    CHECK(designFromJson(designToJson({box(1, 2, 3), cone}), &out, &error));
    CHECK(out.size() == 2 && out[1].topDiameter == 0 && out[1].op == Op::Subtract && out[1].z == 2 && out[1].rx == 15);
    CHECK(out[0].width == 1 && out[0].depth == 2 && out[0].height == 3 && out[0].topDiameter == -1);

    CHECK(!designFromJson("not json", &out, &error));
    CHECK(!designFromJson(R"({"features":[{"shape":"box","width":1,"depth":1}]})", &out, &error));
    CHECK(error.contains("height"));
    CHECK(!designFromJson(R"({"features":[{"shape":"blob"}]})", &out, &error));
    CHECK(!designFromJson(R"({"features":[{"shape":"box","width":"x","depth":1,"height":1}]})", &out, &error));
    CHECK(!designFromJson(R"({"features":[{"shape":"box","width":-1,"depth":1,"height":1}]})", &out, &error));
    CHECK(designFromJson(R"({"features":[{"shape":"sphere","diameter":5,"width":null,"x":null}]})", &out, &error));
}

static void testExport() {
    QTemporaryDir dir;
    QString error;
    const EvalResult r = evaluate({box(10, 10, 10)});

    const QString stl = dir.filePath("a.stl");
    CHECK(exportStl(r.mesh, stl, &error));
    QFile stlFile(stl);
    CHECK(stlFile.open(QIODevice::ReadOnly));
    const QByteArray data = stlFile.readAll();
    CHECK(data.size() == qsizetype(84 + 50 * r.mesh.triangleCount()));
    CHECK(!data.startsWith("solid"));
    CHECK(qFromLittleEndian<quint32>(data.constData() + 80) == r.mesh.triangleCount());

    const QString threeMf = dir.filePath("a.3mf");
    CHECK(export3mf(r.mesh, threeMf, "a <&> b", &error));
    QFile zip(threeMf);
    CHECK(zip.open(QIODevice::ReadOnly));
    CHECK(zip.read(2) == "PK");

    CHECK(!exportStl(Mesh{}, dir.filePath("none.stl"), &error));
    CHECK(!QFile::exists(dir.filePath("none.stl")));
}

static void testHardwareAndCatalog() {
    auto gpus = parseNvidiaSmi("NVIDIA GeForce RTX 3070, 8192, 6713\nOdd, Name GPU, 24576, 20000\nBad line\nN/A, [N/A], [N/A]\n");
    CHECK(gpus.size() == 2);
    CHECK(gpus[0].totalMiB == 8192 && gpus[0].freeMiB == 6713);
    CHECK(gpus[1].name == "Odd, Name GPU");

    HardwareInfo info;
    CHECK(info.best() == nullptr);
    info.gpus = gpus;
    CHECK(info.best()->totalMiB == 24576);

    const auto& catalog = modelCatalog();
    for (size_t i = 1; i < catalog.size(); ++i) CHECK(catalog[i].minVramMiB < catalog[i - 1].minVramMiB);

    Recommendation top = recommendModel(100000, 64000);
    CHECK(top.onGpu && top.model.tag == catalog.front().tag);
    Recommendation none = recommendModel(0, 64000);
    CHECK(!none.onGpu && none.enoughMemory && none.model.tag == catalog.back().tag);
    CHECK(!recommendModel(0, 100).enoughMemory);
    for (const ModelEntry& entry : catalog) {
        CHECK(recommendModel(entry.minVramMiB, 0).model.tag == entry.tag);
        // One MiB short must not run this model on the GPU. The smallest model
        // may still be chosen, but only as the CPU fallback.
        const Recommendation below = recommendModel(entry.minVramMiB - 1, 64000);
        CHECK(below.model.tag != entry.tag || !below.onGpu);
    }
}

static void testAssistantReply() {
    AssistantReply reply;
    QString error;
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[{"name":"a","op":"add","shape":"box","width":1,"depth":1,"height":1}]})", &reply, &error));
    CHECK(reply.features.size() == 1 && reply.text == "ok");
    CHECK(parseAssistantReply(R"({"reply":"just a question","features":[]})", &reply, &error) && reply.features.empty());
    CHECK(!parseAssistantReply("nope", &reply, &error));
    // qwen2.5-coder's actual pentagon answer, which leaves out the height.
    CHECK(parseAssistantReply(R"({"reply":"Built a pentagon.","features":[{"name":"P","op":"add","shape":"cylinder","diameter":40,"sides":5}]})", &reply, &error));
    CHECK(reply.features.size() == 1 && reply.features[0].height == 20 && reply.features[0].diameter == 40);
    CHECK(reply.text.contains("height"));
    CHECK(parseAssistantReply(R"({"reply":"x","features":[{"name":"b","op":"add","shape":"box","height":0}]})", &reply, &error));
    CHECK(reply.features[0].width == 20 && reply.features[0].depth == 20 && reply.features[0].height == 20);
    CHECK(parseAssistantReply(R"({"reply":"x","features":[{"name":"t","op":"add","shape":"torus"}]})", &reply, &error));
    CHECK(reply.features[0].diameter == 20 && reply.features[0].tubeDiameter == 5);
    CHECK(polygonSidesIn("Build me a pentegon") == 5 && polygonSidesIn("a hexagonal nut") == 6 && polygonSidesIn("a triangle") == 3);
    CHECK(polygonSidesIn("a 20 mm cube") == 0 && polygonSidesIn("a hexagon on an octagon") == 0 && polygonSidesIn("an annoying decal") == 0 && polygonSidesIn("nonvolatile") == 0 && polygonSidesIn("a dodecagon") == 12);
    // Round answer to a polygon request: the solid gets the sides, the hole stays round.
    CHECK(parseAssistantReply(R"({"reply":"x","features":[{"name":"P","op":"add","shape":"cylinder","diameter":30,"height":10},{"name":"H","op":"subtract","shape":"cylinder","diameter":5,"height":12,"z":-1}]})", &reply, &error, "make a pentagon with a hole"));
    CHECK(reply.features[0].sides == 5 && reply.features[1].sides == 0);
    CHECK(parseAssistantReply(R"({"reply":"x","features":[{"name":"P","op":"add","shape":"cylinder","diameter":30,"height":10}]})", &reply, &error, "a round disc"));
    CHECK(reply.features[0].sides == 0);
    CHECK(!parseAssistantReply(R"({"reply":"x","features":[{"name":"a","op":"subtract","shape":"box","width":1,"depth":1,"height":1}]})", &reply, &error));
    CHECK(error.contains("empty"));
}

static void testPlacement() {
    // Wedge volume and shape.
    Feature wedge = box(30, 10, 20);
    wedge.shape = Shape::Wedge;
    EvalResult r = evaluate({wedge});
    CHECK(r.ok && near(r.volume, 3000) && near(r.max[2], 20) && near(r.max[0], 15));
    std::vector<Feature> parsed;
    QString error;
    CHECK(designFromJson(designToJson({wedge}), &parsed, &error) && parsed[0].shape == Shape::Wedge);

    // Field sets per primitive.
    CHECK(hudParams(box(1, 1, 1)) == (std::vector<Param>{Param::Length, Param::Width, Param::Height}));
    Feature cone = cylinder(20, 10);
    CHECK(hudParams(cone) == (std::vector<Param>{Param::Radius, Param::Height}));
    cone.topDiameter = 0;
    CHECK(hudParams(cone) == (std::vector<Param>{Param::Radius, Param::TopRadius, Param::Height}));

    // Torus radii round-trip through the stored diameters.
    Feature torus;
    torus.shape = Shape::Torus;
    torus.diameter = 40;
    torus.tubeDiameter = 10;
    CHECK(near(paramValue(torus, Param::MajorRadius), 15) && near(paramValue(torus, Param::MinorRadius), 5));
    setParamValue(&torus, Param::MajorRadius, 20);
    CHECK(near(paramValue(torus, Param::MajorRadius), 20) && near(paramValue(torus, Param::MinorRadius), 5));
    setParamValue(&torus, Param::MinorRadius, 3);
    CHECK(near(paramValue(torus, Param::MajorRadius), 20) && near(paramValue(torus, Param::MinorRadius), 3));
    r = evaluate({torus});
    CHECK(r.ok && near(r.max[0], 23) && near(r.max[2], 6));

    // Seating on each kind of face matches what the kernel builds.
    Feature plate = box(10, 10, 2);
    seatOnFace(&plate, V3{10, 3.4, 5.6}, V3{1, 0, 0}, true);
    CHECK(plate.y == 3 && plate.z == 6 && plate.x == 10);
    r = evaluate({plate});
    CHECK(r.ok && near(r.min[0], 10) && near(r.max[0], 12));

    plate = box(10, 10, 2);
    seatOnFace(&plate, V3{0, -7, 0}, V3{0, -1, 0}, true);
    r = evaluate({plate});
    CHECK(r.ok && near(r.max[1], -7) && near(r.min[1], -9));

    plate = box(10, 10, 2);
    seatOnFace(&plate, V3{0, 0, 20}, V3{0, 0, 1}, true);
    r = evaluate({plate});
    CHECK(r.ok && near(r.min[2], 20) && near(r.max[2], 22));

    plate = box(4, 4, 2);
    const V3 slanted = normalized(V3{1, 1, 1});
    seatOnFace(&plate, V3{}, slanted, true);
    const V3 up = featureDirectionToWorld(plate, V3{0, 0, 1});
    CHECK(length(up - slanted) < 0.001);

    seatOnGrid(&plate, 3.6, -2.2, true);
    CHECK(plate.x == 4 && plate.y == -2 && plate.z == 0 && plate.rx == 0 && plate.ry == 0);
    seatOnGrid(&plate, 3.6, -2.2, false);
    CHECK(near(plate.x, 3.6) && near(plate.y, -2.2));

    // Each arrow tip sits on the boundary of its solid.
    const auto tipsOnBounds = [](const Feature& f) {
        const EvalResult e = evaluate({f});
        for (const Handle& h : handlesFor(f)) {
            const V3 tip = h.base + h.dir * (h.scale * paramValue(f, h.param));
            if (tip.x > e.max[0] + 0.1 || tip.x < e.min[0] - 0.1 || tip.y > e.max[1] + 0.1 || tip.y < e.min[1] - 0.1 || tip.z > e.max[2] + 0.1) return false;
            const bool onBound = std::abs(tip.x - e.max[0]) < 0.1 || std::abs(tip.x - e.min[0]) < 0.1 || std::abs(tip.y - e.max[1]) < 0.1 ||
                                 std::abs(tip.y - e.min[1]) < 0.1 || std::abs(tip.z - e.max[2]) < 0.1;
            if (!onBound) return false;
        }
        return true;
    };
    Feature sphere;
    sphere.shape = Shape::Sphere;
    sphere.diameter = 20;
    CHECK(tipsOnBounds(box(30, 20, 10)));
    CHECK(tipsOnBounds(wedge));
    CHECK(tipsOnBounds(cylinder(20, 10)));
    CHECK(tipsOnBounds(cone));
    CHECK(tipsOnBounds(sphere));
    CHECK(tipsOnBounds(torus));

    // Uniform scaling multiplies every dimension.
    Feature scaled;
    applyUniformScale(&scaled, box(10, 20, 30), 2);
    CHECK(scaled.width == 20 && scaled.depth == 40 && scaled.height == 60);

    // Axis math and ray casting.
    const auto along = closestOnAxis(V3{0, 0, 0}, V3{0, 0, 1}, V3{10, 0, 7}, V3{-1, 0, 0});
    CHECK(along && near(*along, 7));
    CHECK(!closestOnAxis(V3{}, V3{0, 0, 1}, V3{5, 0, 0}, V3{0, 0, 1}));

    const Mesh cube = evaluate({box(20, 20, 20)}).mesh;
    const auto top = raycastMesh(cube, V3{3, 4, 50}, V3{0, 0, -1});
    CHECK(top && near(top->t, 30) && near(top->point.z, 20) && near(top->normal.z, 1));
    const auto side = raycastMesh(cube, V3{50, 2, 10}, V3{-1, 0, 0});
    CHECK(side && near(side->point.x, 10) && near(side->normal.x, 1));
    CHECK(!raycastMesh(cube, V3{50, 50, 50}, V3{0, 0, 1}));

    // A cube's outline is its twelve edges, without the face diagonals.
    CHECK(sharpEdges(cube).size() == 12 * 6);

    const Mesh arrow = makeArrow(V3{1, 2, 3}, V3{0, 1, 0}, 10);
    CHECK(!arrow.empty() && arrow.positions[1] == 12.0f);
}

static int edgeAlong(const Mesh& mesh, const Topology& topology, V3 p, V3 q) {
    for (int e = 0; e < int(topology.edges.size()); ++e) {
        const TopoEdge& edge = topology.edges[e];
        if (edge.closed || edge.vertices.size() != 2) continue;
        const V3 a = meshVertex(mesh, edge.vertices[0]), b = meshVertex(mesh, edge.vertices[1]);
        if ((length(a - p) < 0.01 && length(b - q) < 0.01) || (length(a - q) < 0.01 && length(b - p) < 0.01)) return e;
    }
    return -1;
}

static int faceWithNormal(const Topology& topology, V3 normal) {
    for (int f = 0; f < int(topology.faces.size()); ++f)
        if (topology.faces[f].planar && length(topology.faces[f].normal - normal) < 0.001) return f;
    return -1;
}

static double volumeWith(std::vector<Feature> base, const std::vector<Feature>& extra) {
    base.insert(base.end(), extra.begin(), extra.end());
    const EvalResult r = evaluate(base);
    return r.ok ? r.volume : -1;
}

static bool isWatertight(const Mesh& mesh) {
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (size_t t = 0; t < mesh.triangleCount(); ++t)
        for (int e = 0; e < 3; ++e) directed[{mesh.indices[t * 3 + e], mesh.indices[t * 3 + (e + 1) % 3]}]++;
    for (const auto& [edge, count] : directed) {
        const auto opposite = directed.find({edge.second, edge.first});
        if (count != 1 || opposite == directed.end() || opposite->second != 1) return false;
    }
    return !mesh.empty();
}

static void testModify() {
    const std::vector<Feature> cube{box(20, 20, 20)};
    const Mesh mesh = evaluate(cube).mesh;
    const Topology topology = buildTopology(mesh);
    CHECK(topology.faces.size() == 6 && topology.edges.size() == 12 && topology.corners.size() == 8);
    for (const TopoFace& face : topology.faces) CHECK(face.planar && faceLoops(mesh, face).size() == 1);

    // The top edge along Y at +X, 20 mm long.
    const int e1 = edgeAlong(mesh, topology, V3{10, -10, 20}, V3{10, 10, 20});
    const int e2 = edgeAlong(mesh, topology, V3{-10, -10, 20}, V3{-10, 10, 20});
    CHECK(e1 >= 0 && e2 >= 0);

    CHECK(near(volumeWith(cube, modifyFeatures(ModifyKind::Chamfer, mesh, topology, {e1}, 2)), 8000 - 20 * 2.0, 0.0005));
    CHECK(near(volumeWith(cube, modifyFeatures(ModifyKind::Fillet, mesh, topology, {e1}, 3)), 8000 - 20 * 9 * (1 - std::numbers::pi / 4), 0.0005));
    CHECK(near(volumeWith(cube, modifyFeatures(ModifyKind::Chamfer, mesh, topology, {e1, e2}, 2)), 8000 - 2 * 20 * 2.0, 0.0005));

    // Three edges meeting at a corner round into a true ball.
    const V3 ballCorner{10, -10, 20};
    const int cx = edgeAlong(mesh, topology, ballCorner, V3{-10, -10, 20});
    const int cy = edgeAlong(mesh, topology, ballCorner, V3{10, 10, 20});
    const int cz = edgeAlong(mesh, topology, ballCorner, V3{10, -10, 0});
    CHECK(cx >= 0 && cy >= 0 && cz >= 0);
    const auto threeEdges = modifyFeatures(ModifyKind::Fillet, mesh, topology, {cx, cy, cz}, 3);
    CHECK(threeEdges.size() == 1);
    std::vector<Feature> ballBox = cube;
    ballBox.insert(ballBox.end(), threeEdges.begin(), threeEdges.end());
    const EvalResult ballResult = evaluate(ballBox);
    const double perEdge = 9 * (1 - std::numbers::pi / 4);
    const double ballPiece = 27 - 4.0 / 3 * std::numbers::pi * 27 / 8;
    CHECK(ballResult.ok && near(8000 - ballResult.volume, 3 * perEdge * 17 + ballPiece, 0.01));
    CHECK(isWatertight(ballResult.mesh));

    // The surface really is that ball: its diagonal point is on it, the old corner is gone.
    const V3 ballCentre{7, -7, 17};
    const V3 onBall = ballCentre + normalized(V3{1, -1, 1}) * 3.0;
    CHECK(pointOnMesh(ballResult.mesh, onBall, 0.03));
    const Topology ballTopology = buildTopology(ballResult.mesh);
    for (uint32_t c : ballTopology.corners) CHECK(length(meshVertex(ballResult.mesh, c) - ballCorner) > 3);

    // Every edge together gives the same shape as a box built with rounded corners.
    std::vector<int> allEdges(topology.edges.size());
    std::iota(allEdges.begin(), allEdges.end(), 0);
    std::vector<Feature> allRounded = cube;
    for (const Feature& f : modifyFeatures(ModifyKind::Fillet, mesh, topology, allEdges, 3)) allRounded.push_back(f);
    Feature builtRounded = box(20, 20, 20);
    builtRounded.corner = 3;
    CHECK(near(evaluate(allRounded).volume, evaluate({builtRounded}).volume, 0.003));

    // Corners with only two selected edges are left as they were.
    CHECK(modifyFeatures(ModifyKind::Fillet, mesh, topology, {cx, cy}, 3)[0].prisms.size() == 2);

    // Too large a radius for the edges stops rather than breaking.
    CHECK(modifyFeatures(ModifyKind::Fillet, mesh, topology, allEdges, 11).empty());

    // A saved ball corner comes back identical.
    std::vector<Feature> savedBall;
    QString ballError;
    CHECK(designFromJson(designToJson(threeEdges), &savedBall, &ballError) && sameFeature(savedBall[0], threeEdges[0]));

    // Pull adds along the normal and push cuts into the solid, on any face.
    const int top = faceWithNormal(topology, V3{0, 0, 1});
    const int side = faceWithNormal(topology, V3{1, 0, 0});
    CHECK(top >= 0 && side >= 0);
    CHECK(near(volumeWith(cube, modifyFeatures(ModifyKind::Extrude, mesh, topology, {top}, 5)), 8000 + 2000, 0.001));
    CHECK(near(volumeWith(cube, modifyFeatures(ModifyKind::Extrude, mesh, topology, {top}, -5)), 8000 - 2000, 0.001));
    CHECK(near(volumeWith(cube, modifyFeatures(ModifyKind::Extrude, mesh, topology, {side}, 5)), 8000 + 2000, 0.001));
    CHECK(near(volumeWith(cube, modifyFeatures(ModifyKind::Extrude, mesh, topology, {side}, -5)), 8000 - 2000, 0.001));
    CHECK(near(volumeWith(cube, modifyFeatures(ModifyKind::Extrude, mesh, topology, {top, side}, 5)), 8000 + 2000 + 2000, 0.001));
    CHECK(modifyFeatures(ModifyKind::Extrude, mesh, topology, {top}, 0).empty());
    const EvalResult pulled = evaluate([&] {
        auto all = cube;
        for (const Feature& f : modifyFeatures(ModifyKind::Extrude, mesh, topology, {top}, 5)) all.push_back(f);
        return all;
    }());
    CHECK(near(pulled.max[2], 25) && near(pulled.max[0], 10));

    // An edge tool never shows its arrow pointing at the viewer.
    const V3 alongBisector = normalized(V3{-1, 0, -1});
    const ModifyGuide guide = modifyGuide(ModifyKind::Chamfer, mesh, topology, e1, alongBisector);
    CHECK(std::abs(dot(guide.dir, alongBisector)) < 0.75);
    CHECK(length(guide.origin - V3{10, 0, 20}) < 0.01);
    const ModifyGuide sideOn = modifyGuide(ModifyKind::Fillet, mesh, topology, e1, V3{0, 1, 0});
    CHECK(near(length(sideOn.dir), 1) && sideOn.scale > 0);

    // Pushing down a face bounded by curves, such as a gear's teeth, leaves no slivers of wall.
    {
        Feature gear;
        gear.shape = Shape::Gear;
        gear.sides = 12;
        gear.diameter = 14;
        gear.height = 20;
        gear.innerDiameter = 0;
        const std::vector<Feature> design{gear};
        const EvalResult before = evaluate(design);
        const Topology gearTopology = buildTopology(before.mesh);
        const int gearTop = faceWithNormal(gearTopology, V3{0, 0, 1});
        CHECK(gearTop >= 0);
        std::vector<Feature> pushed = design;
        for (const Feature& f : modifyFeatures(ModifyKind::Extrude, before.mesh, gearTopology, {gearTop}, -8)) pushed.push_back(f);
        const EvalResult after = evaluate(pushed);
        CHECK(after.ok && near(after.max[2], 12, 1e-6) && near(after.volume, before.volume * 12 / 20, 0.001) && isWatertight(after.mesh));
        // And pulling it up adds a clean block of the same outline, with no seam ring.
        std::vector<Feature> pulled = design;
        for (const Feature& f : modifyFeatures(ModifyKind::Extrude, before.mesh, gearTopology, {gearTop}, 5)) pulled.push_back(f);
        const EvalResult up = evaluate(pulled);
        CHECK(up.ok && near(up.max[2], 25, 1e-6) && near(up.volume, before.volume * 25 / 20, 0.001));
        CHECK(buildTopology(up.mesh).faces.size() <= gearTopology.faces.size() + 2);
    }

    // A face with a hole keeps the hole when extruded.
    Feature bore = cylinder(10, 22, -1);
    bore.op = Op::Subtract;
    const std::vector<Feature> plate{box(20, 20, 20), bore};
    const Mesh plateMesh = evaluate(plate).mesh;
    const Topology plateTopology = buildTopology(plateMesh);
    const int plateTop = faceWithNormal(plateTopology, V3{0, 0, 1});
    CHECK(plateTop >= 0 && faceLoops(plateMesh, plateTopology.faces[plateTop]).size() == 2);
    const double hollow = evaluate(plate).volume;
    const double pullArea = (hollow / 20) * 5;
    CHECK(near(volumeWith(plate, modifyFeatures(ModifyKind::Extrude, plateMesh, plateTopology, {plateTop}, 5)), hollow + pullArea, 0.002));

    // The rim of a cylinder is one closed edge and can be chamfered and filleted.
    const std::vector<Feature> can{cylinder(20, 10)};
    const Mesh canMesh = evaluate(can).mesh;
    const Topology canTopology = buildTopology(canMesh);
    CHECK(canTopology.faces.size() == 3 && canTopology.edges.size() == 2 && canTopology.corners.empty());
    int rim = -1;
    for (int e = 0; e < int(canTopology.edges.size()); ++e)
        if (meshVertex(canMesh, canTopology.edges[e].vertices[0]).z > 5) rim = e;
    CHECK(rim >= 0 && canTopology.edges[rim].closed);
    const double canVolume = evaluate(can).volume;
    const double rimChamfered = volumeWith(can, modifyFeatures(ModifyKind::Chamfer, canMesh, canTopology, {rim}, 1));
    CHECK(near(canVolume - rimChamfered, 2 * std::numbers::pi * (10 - 1.0 / 3) * 0.5, 0.03));
    const double rimFilleted = volumeWith(can, modifyFeatures(ModifyKind::Fillet, canMesh, canTopology, {rim}, 1));
    CHECK(rimFilleted < canVolume && rimFilleted > rimChamfered - 1);

    // A round rim stays one smooth face instead of breaking up, with the exact volume.
    const auto rimFeatures = modifyFeatures(ModifyKind::Fillet, canMesh, canTopology, {rim}, 2);
    CHECK(rimFeatures.size() == 1 && rimFeatures[0].prisms.size() == 1 && rimFeatures[0].prisms[0].revolveSegments > 0);
    std::vector<Feature> roundedCan = can;
    roundedCan.push_back(rimFeatures[0]);
    const EvalResult roundedResult = evaluate(roundedCan);
    const double spandrel = 4 * (1 - std::numbers::pi / 4);
    CHECK(near(canVolume - roundedResult.volume, 2 * std::numbers::pi * (10 - 2 * 0.2234) * spandrel, 0.02));
    const Topology roundedTopology = buildTopology(roundedResult.mesh);
    CHECK(roundedTopology.faces.size() <= 5);
    CHECK(isWatertight(roundedResult.mesh));

    // A tilted torus breaking through a face leaves a curved, unevenly divided edge.
    // Rounding it piece by piece left a saw-tooth rim of 136 faces; one sweep stays smooth.
    {
        Feature block;
        block.shape = Shape::Box;
        Feature torus;
        torus.shape = Shape::Torus;
        torus.op = Op::Subtract;
        torus.diameter = 17;
        torus.tubeDiameter = 5;
        torus.y = -10;
        torus.z = 7.5;
        torus.rz = 7;
        torus.rx = 25;
        const std::vector<Feature> holed{block, torus};
        const EvalResult holedResult = evaluate(holed);
        const Topology holedTopology = buildTopology(holedResult.mesh);
        int curved = -1;
        for (size_t i = 0; i < holedTopology.edges.size(); ++i) {
            V3 c, a;
            double radius = 0;
            if (holedTopology.edges[i].segmentCount() > 20 && !edgeCircle(holedResult.mesh, holedTopology.edges[i], &c, &a, &radius)) curved = int(i);
        }
        CHECK(curved >= 0);
        for (ModifyKind kind : {ModifyKind::Fillet, ModifyKind::Chamfer}) {
            const auto swept = modifyFeatures(kind, holedResult.mesh, holedTopology, {curved}, 1);
            CHECK(swept.size() == 1 && swept[0].shape == Shape::Mesh && swept[0].op == Op::Subtract);
            std::vector<Feature> rounded = holed;
            rounded.push_back(swept[0]);
            const EvalResult r = evaluate(rounded);
            CHECK(r.ok && r.volume < holedResult.volume && r.volume > holedResult.volume - 15 && isWatertight(r.mesh));
            CHECK(buildTopology(r.mesh).faces.size() < 80);
        }
    }

    // The flat top survives as its own face next to the fillet, and can still be pulled.
    const int roundedTop = faceWithNormal(roundedTopology, V3{0, 0, 1});
    CHECK(roundedTop >= 0 && roundedTopology.faces.size() == 3);
    if (roundedTop >= 0) {
        const auto pull = modifyFeatures(ModifyKind::Extrude, roundedResult.mesh, roundedTopology, {roundedTop}, 5);
        CHECK(near(volumeWith(roundedCan, pull), roundedResult.volume + std::numbers::pi * 64 * 5, 0.02));
    }
    CHECK(roundedResult.mesh.triangleCount() < 3 * canMesh.triangleCount() * 8);
    std::vector<Feature> savedRound;
    QString roundError;
    CHECK(designFromJson(designToJson(rimFeatures), &savedRound, &roundError) && savedRound[0].prisms[0].revolveSegments == rimFeatures[0].prisms[0].revolveSegments);

    // A cone's tip is a point, and rounding it matches the geometry of a tangent ball.
    Feature tall = cylinder(20, 20);
    tall.topDiameter = 0;
    const std::vector<Feature> coneDesign{tall};
    const Mesh coneMesh = evaluate(coneDesign).mesh;
    const Topology coneTopology = buildTopology(coneMesh);
    int tip = -1;
    for (int c = 0; c < int(coneTopology.corners.size()); ++c)
        if (std::abs(meshVertex(coneMesh, coneTopology.corners[c]).z - 20) < 0.001) tip = c;
    CHECK(tip >= 0);
    const auto tipFeatures = modifyFeatures(ModifyKind::PointFillet, coneMesh, coneTopology, {tip}, 2);
    CHECK(tipFeatures.size() == 1);
    std::vector<Feature> roundedCone = coneDesign;
    roundedCone.push_back(tipFeatures[0]);
    const EvalResult tipResult = evaluate(roundedCone);
    const double alpha = std::atan2(10.0, 20.0);
    CHECK(tipResult.ok && near(tipResult.max[2], 20 - 2 * (1 / std::sin(alpha) - 1), 0.002));
    const double planeDrop = 2 / std::sin(alpha) * std::pow(std::cos(alpha), 2), ringRadius = 2 * std::cos(alpha);
    const double capHeight = 2 - 2 * std::sin(alpha);
    const double tipRemoved = std::numbers::pi * ringRadius * ringRadius * planeDrop / 3 - std::numbers::pi * capHeight * capHeight * (3 * 2 - capHeight) / 3;
    CHECK(near(evaluate(coneDesign).volume - tipResult.volume, tipRemoved, 0.04));

    // A box corner rounds too, leaving the faces where they were.
    int boxCorner = -1;
    for (int c = 0; c < int(topology.corners.size()); ++c)
        if (length(meshVertex(mesh, topology.corners[c]) - V3{10, 10, 20}) < 0.001) boxCorner = c;
    CHECK(boxCorner >= 0);
    const auto cornerFeatures = modifyFeatures(ModifyKind::PointFillet, mesh, topology, {boxCorner}, 3);
    std::vector<Feature> roundedBox = cube;
    roundedBox.push_back(cornerFeatures[0]);
    const EvalResult cornerResult = evaluate(roundedBox);
    CHECK(cornerResult.ok && cornerResult.volume < 8000 && cornerResult.volume > 7900);
    CHECK(near(cornerResult.max[0], 10) && near(cornerResult.max[2], 20));

    // Reusing earlier steps gives the same answer as starting over.
    Evaluator evaluator;
    CHECK(near(evaluator.evaluate(roundedCan).volume, roundedResult.volume, 1e-9));
    CHECK(near(evaluator.evaluate(can).volume, canVolume, 1e-9));
    CHECK(near(evaluator.evaluate(roundedCan).volume, roundedResult.volume, 1e-9));
    CHECK(evaluator.evaluate({}).mesh.empty());

    // Curved surfaces shade smoothly while a sharp edge stays sharp.
    const DisplayMesh shown = buildDisplayMesh(canMesh);
    CHECK(shown.positions.size() == canMesh.indices.size() * 3 && shown.normals.size() == shown.positions.size());
    bool sharpTop = false, smoothSide = false;
    for (size_t i = 0; i < shown.normals.size() / 3; ++i) {
        const float nz = shown.normals[i * 3 + 2];
        if (std::abs(nz - 1) < 1e-4) sharpTop = true;
        if (std::abs(nz) < 1e-4 && std::abs(shown.normals[i * 3]) > 0.2 && std::abs(shown.normals[i * 3 + 1]) > 0.2) smoothSide = true;
    }
    CHECK(sharpTop && smoothSide);

    // A concave edge is filled, not cut.
    Feature tower = box(10, 10, 20);
    const std::vector<Feature> step{box(30, 30, 5), [&] {
                                        Feature t = tower;
                                        t.z = 5;
                                        return t;
                                    }()};
    const Mesh stepMesh = evaluate(step).mesh;
    const Topology stepTopology = buildTopology(stepMesh);
    const int inner = edgeAlong(stepMesh, stepTopology, V3{5, -5, 5}, V3{5, 5, 5});
    CHECK(inner >= 0);
    const auto filler = modifyFeatures(ModifyKind::Chamfer, stepMesh, stepTopology, {inner}, 2);
    CHECK(filler.size() == 1 && filler[0].op == Op::Add);
    CHECK(near(volumeWith(step, filler), evaluate(step).volume + 10 * 2.0, 0.001));

    // Edits survive saving.
    std::vector<Feature> reloaded;
    QString error;
    const auto edit = modifyFeatures(ModifyKind::Fillet, mesh, topology, {e1}, 3);
    CHECK(designFromJson(designToJson(edit), &reloaded, &error));
    CHECK(near(volumeWith(cube, reloaded), volumeWith(cube, edit), 0.000001));

    // Which surface a point lies on.
    CHECK(pointOnMesh(mesh, V3{0, 0, 20}, 0.01) && !pointOnMesh(mesh, V3{0, 0, 21}, 0.01));
}

static void testAlign() {
    using td::AlignMode;
    const std::array<double, 3> lowA{0, 0, 0}, highA{10, 10, 10}, lowB{20, 30, 5}, highB{40, 34, 25};
    td::V3 shift = td::alignOffset(lowA, highA, lowB, highB, {AlignMode::Min, AlignMode::Centre, AlignMode::None});
    CHECK(near(shift.x, 20) && near(shift.y, 27) && near(shift.z, 0));
    shift = td::alignOffset(lowA, highA, lowB, highB, {AlignMode::Max, AlignMode::None, AlignMode::Max});
    CHECK(near(shift.x, 30) && near(shift.y, 0) && near(shift.z, 15));
}


// Organic models: ellipsoids and capsules placed by their centre, checked for loose parts.
static void testOrganic() {
    using namespace td;
    const auto near1 = [](double a, double b) { return std::abs(a - b) < 0.3; };
    AssistantReply reply;
    QString error;

    // An ellipsoid 40 x 20 x 30 centred 15 mm up sits on the bed.
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[{"name":"egg","op":"add","shape":"ellipsoid","width":40,"depth":20,"height":30,"z":15}]})", &reply, &error));
    EvalResult r = evaluate(reply.features);
    CHECK(r.ok && near1(r.min[0], -20) && near1(r.max[0], 20) && near1(r.min[1], -10) && near1(r.max[1], 10) && near1(r.min[2], 0) && near1(r.max[2], 30));
    QJsonObject back = featureToAiJson(reply.features[0]);
    CHECK(back.value("shape").toString() == "ellipsoid" && near1(back.value("width").toDouble(), 40) && near1(back.value("z").toDouble(), 15));

    // An ellipsoid given only one size, or a diameter, stays round.
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[{"name":"body","op":"add","shape":"ellipsoid","width":50,"z":25}]})", &reply, &error));
    CHECK(reply.features[0].sx == 1 && reply.features[0].sy == 1 && near1(reply.features[0].diameter, 50));
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[{"name":"body","op":"add","shape":"ellipsoid","diameter":40,"height":60,"z":30}]})", &reply, &error));
    r = evaluate(reply.features);
    CHECK(near1(r.max[0] - r.min[0], 40) && near1(r.max[2] - r.min[2], 60));

    // A capsule laid along X about its centre.
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[{"name":"arm","op":"add","shape":"capsule","diameter":8,"height":30,"x":5,"z":10,"ry":90}]})", &reply, &error));
    r = evaluate(reply.features);
    CHECK(r.ok && near1(r.min[0], -10) && near1(r.max[0], 20) && near1(r.min[2], 6) && near1(r.max[2], 14));
    back = featureToAiJson(reply.features[0]);
    CHECK(near1(back.value("x").toDouble(), 5) && near1(back.value("z").toDouble(), 10));
    // Capsule volume: a 30 mm rod of 8 mm with round ends.
    CHECK(near(r.volume, std::numbers::pi * 16 * 22 + 4.0 / 3 * std::numbers::pi * 64, 0.03));

    // A part left floating is found and named; a joined one is not.
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[
        {"name":"body","op":"add","shape":"sphere","diameter":40,"z":20},
        {"name":"head","op":"add","shape":"sphere","diameter":24,"z":48},
        {"name":"ear_left","op":"add","shape":"sphere","diameter":8,"x":-30,"z":70}]})", &reply, &error));
    const std::vector<LoosePart> loose = looseParts(reply.features);
    CHECK(loose.size() == 1 && loose[0].name == "ear_left" && loose[0].nearest == "head" && loose[0].gap > 5);
    CHECK(designProblems(reply.features).size() == 1 && designProblems(reply.features)[0].contains("ear_left"));
    reply.features[2].x = -9;
    reply.features[2].z = 55;
    CHECK(looseParts(reply.features).empty());

    // Like the user's cat: legs standing on the bed, short of a raised body. They slide in and join.
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[
        {"name":"body","op":"add","shape":"sphere","diameter":40,"z":50},
        {"name":"head","op":"add","shape":"sphere","diameter":30,"y":-25,"z":70},
        {"name":"leg_left","op":"add","shape":"capsule","diameter":8,"height":20,"x":-10,"z":10},
        {"name":"leg_right","op":"add","shape":"capsule","diameter":8,"height":20,"x":10,"z":10},
        {"name":"tail","op":"add","shape":"capsule","diameter":6,"height":20,"y":40,"z":30}]})", &reply, &error));
    CHECK(looseParts(reply.features).size() == 3);
    const std::vector<Feature> before = reply.features;
    const QStringList joined = joinLooseParts(&reply.features);
    CHECK(joined.size() == 3 && joined.contains("leg_left") && joined.contains("tail"));
    CHECK(looseParts(reply.features).empty());
    CHECK(sameFeature(before[0], reply.features[0]) && sameFeature(before[1], reply.features[1]));  // joined parts stay put
    CHECK(reply.features[2].z > before[2].z);  // the legs went up into the body
    CHECK(joinLooseParts(&reply.features).isEmpty());

    // A model's half-right cat: round cone ears and an upright body get fixed.
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[
        {"name":"body","op":"add","shape":"capsule","diameter":26,"height":60,"z":30},
        {"name":"ear_left","op":"add","shape":"cylinder","diameter":10,"top_diameter":0,"height":12,"x":-6,"z":60},
        {"name":"leg_front_left","op":"add","shape":"capsule","diameter":9,"height":30,"x":-8,"y":-20,"z":15},
        {"name":"leg_front_right","op":"add","shape":"capsule","diameter":9,"height":30,"x":8,"y":-20,"z":15},
        {"name":"leg_back_left","op":"add","shape":"capsule","diameter":9,"height":30,"x":-8,"y":20,"z":15},
        {"name":"leg_back_right","op":"add","shape":"capsule","diameter":9,"height":30,"x":8,"y":20,"z":15}]})", &reply, &error));
    CHECK(reply.features[1].sides == 3 && reply.features[1].rz == 90);
    CHECK(reply.features[0].rx == 90);
    r = evaluate({reply.features[0]});
    CHECK(near1(r.max[1] - r.min[1], 60) && near1(r.max[2] - r.min[2], 26) && near1((r.min[2] + r.max[2]) / 2, 30));

    // Curves: straight, a curve is a capsule; bent, it curls toward +X; placed by its middle.
    {
        Feature straight;
        straight.shape = Shape::Curve;
        straight.diameter = 10;
        straight.height = 40;
        r = evaluate({straight});
        CHECK(r.ok && near1(r.min[2], 0) && near1(r.max[2], 40) && near1(r.max[0] - r.min[0], 10));
        Feature bent = straight;
        bent.bend = 180;
        bent.topDiameter = 4;
        r = evaluate({bent});
        // Half a turn: the 33 mm spine is a U of radius 10.5, from 5 mm thick down to 2 mm.
        std::printf("bent curve x %.2f..%.2f z %.2f..%.2f\n", r.min[0], r.max[0], r.min[2], r.max[2]);
        CHECK(r.ok && std::abs(r.max[0] - 23) < 1 && std::abs(r.max[2] - 19) < 1 && near1(r.min[0], -5) && near1(r.min[2], 0));
        bent.ry = 30;  // tilted, so it is not taken for a banana standing on end
        QJsonObject ai = featureToAiJson(bent);
        CHECK(ai.value("shape").toString() == "curve" && near1(ai.value("bend").toDouble(), 180));
        CHECK(parseAssistantReply(QStringLiteral(R"({"reply":"ok","features":[%1]})").arg(QString::fromUtf8(QJsonDocument(ai).toJson(QJsonDocument::Compact))), &reply, &error));
        CHECK(near1(reply.features[0].x, 0) && near1(reply.features[0].z, 0));
    }

    // The local model's banana: a curve with no length, bend or turn still comes out a banana.
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[{"name":"banana","op":"add","shape":"curve","diameter":30,"top_diameter":18,"z":15}]})", &reply, &error));
    CHECK(reply.features[0].height == 150 && reply.features[0].bend == 60 && reply.features[0].rx == -90);
    r = evaluate(reply.features);
    CHECK(r.max[2] - r.min[2] < 31 && r.max[1] - r.min[1] > 100);  // lying on its side

    // The local model's car: ball wheels lying along the wrong side of a body with no depth.
    CHECK(parseAssistantReply(R"({"reply":"ok","features":[{"name":"body","op":"add","shape":"box","width":180,"depth":20,"height":20,"y":110},)"
                              R"({"name":"cabin","op":"add","shape":"box","width":160,"depth":20,"height":20,"y":90,"z":15},)"
                              R"({"name":"wheel_front_left","op":"add","shape":"sphere","diameter":30,"x":-30,"y":60},{"name":"wheel_front_right","op":"add","shape":"sphere","diameter":30,"x":30,"y":60},)"
                              R"({"name":"wheel_back_left","op":"add","shape":"cylinder","diameter":30,"height":8,"x":-30,"y":160},{"name":"wheel_back_right","op":"add","shape":"cylinder","diameter":30,"height":8,"x":30,"y":160}]})",
                              &reply, &error));
    CHECK(reply.features.size() == 6 && reply.features[0].depth == 81 && reply.features[0].corner > 0 && reply.features[1].width < 180 * 0.7);
    for (size_t i = 2; i < 6; ++i) {
        const Feature& w = reply.features[i];
        CHECK(w.shape == Shape::Cylinder && w.rx == 90 && w.ry == 0 && w.corner > 0 && w.height < w.diameter / 2);
    }
    CHECK(reply.features[2].x < 0 && reply.features[4].x > 0 && reply.features[2].y < reply.features[3].y);  // front at -X, left at -Y
    r = evaluate(reply.features);
    CHECK(r.ok && r.max[0] - r.min[0] == 180 && r.max[1] - r.min[1] > 81 && r.max[2] - r.min[2] < 60);

    // Rounded rims: less material than the sharp cylinder, saved and read back.
    {
        Feature tyre;
        tyre.shape = Shape::Cylinder;
        tyre.diameter = 30;
        tyre.height = 10;
        const double sharp = evaluate({tyre}).volume;
        tyre.corner = 3;
        QString why;
        CHECK(validateFeature(tyre, &why));
        const EvalResult round = evaluate({tyre});
        CHECK(round.ok && round.volume < sharp && round.volume > sharp * 0.9 && round.max[2] - round.min[2] > 9.99);
        Feature back;
        CHECK(featureFromJson(featureToJson(tyre), &back, &why) && back.corner == 3);
        tyre.corner = 5;
        CHECK(!validateFeature(tyre, &why));  // half the height
    }

    // New things start over; changes work on what is there.
    CHECK(asksForNewDesign("create a banana") && asksForNewDesign("Make a teddy bear") && asksForNewDesign("please build me a cat") &&
          asksForNewDesign("create another banana") && asksForNewDesign("make 3 dice"));
    CHECK(!asksForNewDesign("make it longer") && !asksForNewDesign("add a hole in the middle") && !asksForNewDesign("make a hole through it") && !asksForNewDesign("make a slot in this"));
    CHECK(!asksForNewDesign("make the ears bigger") && !asksForNewDesign("bigger please") && !asksForNewDesign("why is it hollow?"));

    // Hovering designs come down to the bed.
    std::vector<Feature> lifted = before;
    for (Feature& f : lifted) f.z += 12;
    CHECK(near1(seatOnBed(&lifted), -12) && near1(evaluate(lifted).min[2], 0));

    // Checking stays quick on big, detailed, overlapping parts and a far-off one.
    std::vector<Feature> heavy;
    for (int i = 0; i < 16; ++i) {
        Feature f;
        f.shape = i % 2 ? Shape::Capsule : Shape::Sphere;
        f.name = QStringLiteral("p%1").arg(i);
        f.diameter = 60;
        f.height = 120;
        f.x = i * 25;
        f.z = (i % 3) * 10;
        f.sx = 1.3;
        heavy.push_back(f);
    }
    heavy.push_back(heavy[0]);
    heavy.back().name = "far";
    heavy.back().x = -400;
    QElapsedTimer timer;
    timer.start();
    const std::vector<LoosePart> heavyLoose = looseParts(heavy);
    std::printf("loose-part check on 17 large parts: %lld ms\n", timer.elapsed());
    CHECK(heavyLoose.size() == 1 && heavyLoose[0].name == "far" && heavyLoose[0].gap > 300);
    CHECK(timer.elapsed() < 3000);

    // The prompt's own examples are sound: joined up, on the bed, the height they claim.
    const QString prompt = cadSystemPrompt();
    for (const auto& [opening, height] : {std::pair{"{\"reply\":\"Made a 70 mm snowman", 70.0}, std::pair{"{\"reply\":\"Made a standing cat, 65 mm tall", 65.0}, std::pair{"{\"reply\":\"Made a 150 mm banana", 0.0}, std::pair{"{\"reply\":\"Made a 100 mm toy car", 43.0}, std::pair{"{\"reply\":\"Made a sitting teddy bear, 70 mm tall", 70.0}}) {
        const int start = prompt.indexOf(QLatin1String(opening));
        CHECK(start > 0);
        CHECK(parseAssistantReply(prompt.mid(start, prompt.indexOf('\n', start) - start), &reply, &error));
        CHECK(designProblems(reply.features).isEmpty());
        r = evaluate(reply.features);
        if (height == 0) {
            // The banana lies flat on the bed, longer than it is wide, and bowed: its ends sit off the middle line.
            std::printf("banana %.1f x %.1f x %.1f, min z %.2f\n", r.max[0] - r.min[0], r.max[1] - r.min[1], r.max[2] - r.min[2], r.min[2]);
            CHECK(std::abs(r.min[2]) < 0.6 && r.max[2] - r.min[2] < 35 && r.max[1] - r.min[1] > 100 && r.max[0] - r.min[0] > 40);
            continue;
        }
        CHECK(isFigure(reply.features) == !QLatin1String(opening).contains(QLatin1String("car")));
        CHECK(std::abs(r.min[2]) < 0.6 && std::abs(r.max[2] - height) < 3);
        if (!designProblems(reply.features).isEmpty() || std::abs(r.max[2] - height) >= 3)
            std::printf("%s: %s, height %.1f\n", opening, qPrintable(designProblems(reply.features).join(' ')), r.max[2] - r.min[2]);
    }

    // Sizes in a request, and scaling a design to one.
    CHECK(sizeStatedIn("a 60 mm bear") && sizeStatedIn("a bear 6cm tall") && sizeStatedIn("2\" cube") && !sizeStatedIn("a teddy bear") && !sizeStatedIn("3 bears"));
    std::vector<Feature> half = reply.features;
    scaleDesign(&half, 0.5);
    CHECK(near1(evaluate(half).max[2], evaluate(reply.features).max[2] / 2));
}

static void testDocument() {
    // Affine maps compose, invert and rotate the right way round.
    const Affine turn = Affine::rotation(V3{0, 0, 1}, 90, V3{10, 0, 0});
    CHECK(length(turn.point(V3{20, 0, 0}) - V3{10, 10, 0}) < 1e-9);
    const Affine mixed = Affine::translation(V3{1, 2, 3}) * Affine::scaling(V3{2, 3, 4}, V3{5, 5, 5}) * turn;
    CHECK(length((mixed.inverse() * mixed).point(V3{7, -3, 11}) - V3{7, -3, 11}) < 1e-9);
    CHECK((Affine::translation(V3{1, 0, 0}) * Affine::translation(V3{-1, 0, 0})).isIdentity());

    // Old single-history files: plain additions become separate bodies, cuts keep one.
    Document doc;
    QString error;
    CHECK(documentFromJson(designToJson({box(10, 10, 10), cylinder(5, 20)}), &doc, &error));
    CHECK(doc.bodies.size() == 2 && doc.bodies[0].features.size() == 1);
    Feature cut = cylinder(4, 30, -5);
    cut.op = Op::Subtract;
    Document joined;
    CHECK(documentFromJson(designToJson({box(10, 10, 10), cut}), &joined, &error) && joined.bodies.size() == 1);

    // Saving keeps names, placements and groups.
    transformBodies(&doc, {1}, Affine::translation(V3{30, 0, 0}));
    CHECK(groupBodies(&doc, {0, 1}, {}));
    Document reloaded;
    CHECK(documentFromJson(documentToJson(doc), &reloaded, &error));
    CHECK(reloaded.bodies.size() == 2 && reloaded.bodies[1].transform == doc.bodies[1].transform);
    CHECK(reloaded.bodies[0].groups == doc.bodies[0].groups && reloaded.nextGroupId > doc.bodies[0].groups[0]);
    CHECK(reloaded.bodies[0].name == doc.bodies[0].name);

    // Bodies stay separate until printed, where they become one solid.
    DocumentEvaluator evaluator;
    std::vector<EvalResult> results;
    CHECK(evaluator.evaluate(doc, &results, &error) && results.size() == 2);
    CHECK(near(results[1].min[0], 30 - 2.5, 0.001) && near(results[0].volume, 1000));
    CHECK(near(evaluator.combined(doc).volume, results[0].volume + results[1].volume, 1e-6));

    // Groups: a click picks the whole group, entering it picks one member.
    CHECK(unitOf(doc, 0, {}) == (std::vector<int>{0, 1}) && isGroupUnit(doc, 0, {}));
    const std::vector<int> inside{doc.bodies[0].groups[0]};
    CHECK(unitOf(doc, 0, inside) == std::vector<int>{0} && !isGroupUnit(doc, 0, inside));
    CHECK(!groupBodies(&doc, {0, 1}, {}));  // already one group
    addBody(&doc, nextName(doc, QStringLiteral("Box")), {box(5, 5, 5)});
    CHECK(doc.bodies[2].name == "Box 1");
    CHECK(groupBodies(&doc, {0, 1, 2}, {}));  // nests the old group inside a new one
    CHECK(unitOf(doc, 2, {}).size() == 3 && doc.bodies[0].groups.size() == 2);
    CHECK(ungroupBodies(&doc, {0}, {}));
    CHECK(unitOf(doc, 0, {}).size() == 2 && unitOf(doc, 2, {}) == std::vector<int>{2});
    CHECK(groupBodies(&doc, {0, 1, 2}, {}) && ungroupAll(&doc, {2}, {}));
    for (const Body& b : doc.bodies) CHECK(b.groups.empty());
    CHECK(!ungroupBodies(&doc, {0}, {}));

    // Merge, subtract and intersect fold whole bodies, wherever they were placed.
    Document combine;
    addBody(&combine, "Block", {box(20, 20, 10)});
    addBody(&combine, "Peg", {cylinder(6, 30)}, Affine::translation(V3{0, 0, -10}));
    addBody(&combine, "Far", {box(4, 4, 4)}, Affine::translation(V3{50, 0, 0}));
    Document subtracted = combine;
    const int kept = combineBodies(&subtracted, 0, {1}, Op::Subtract);
    CHECK(kept == 0 && subtracted.bodies.size() == 2);
    CHECK(evaluator.evaluate(subtracted, &results, &error) && near(results[0].volume, 4000 - std::numbers::pi * 9 * 10, 0.01));
    Document merged = combine;
    combineBodies(&merged, 2, {0, 1}, Op::Add);
    CHECK(merged.bodies.size() == 1 && merged.bodies[0].name == "Far");
    CHECK(evaluator.evaluate(merged, &results, &error) && near(results[0].max[0], 52) && near(results[0].min[2], -10));
    Document crossed = combine;
    combineBodies(&crossed, 0, {1}, Op::Intersect);
    CHECK(evaluator.evaluate(crossed, &results, &error) && near(results[0].volume, std::numbers::pi * 9 * 10, 0.01));

    // A merged body still moves as one, edits and all.
    transformBodies(&merged, {0}, Affine::rotation(V3{0, 0, 1}, 90, V3{}));
    CHECK(evaluator.evaluate(merged, &results, &error) && near(results[0].max[1], 52));

    // Split cuts a body in two along a plane; Separate gives each loose piece its own body.
    Document split = combine;
    splitBody(&split, 0, V3{3, 0, 0}, V3{1, 0, 0}, 200);
    CHECK(split.bodies.size() == 4);
    CHECK(evaluator.evaluate(split, &results, &error));
    CHECK(near(results[0].volume, 13 * 20 * 10, 0.01) && near(results[1].volume, 7 * 20 * 10, 0.01) && near(results[1].min[0], 3));

    Document apart = combine;
    combineBodies(&apart, 0, {2}, Op::Add);
    const auto pieces = evaluator.pieces(apart.bodies[0]);
    CHECK(pieces.size() == 2);
    replaceWithPieces(&apart, 0, pieces);
    CHECK(apart.bodies.size() == 3 && evaluator.evaluate(apart, &results, &error));
    CHECK(near(results[0].volume + results[1].volume, 4000 + 64, 0.001));
    Document piecesSaved;
    CHECK(documentFromJson(documentToJson(apart), &piecesSaved, &error) && piecesSaved.bodies[0].features[0].shape == Shape::Mesh);

    // Snapping turns and moves one face flat against another.
    const Affine snap = snapTransform(V3{0, 0, 0}, V3{0, 0, -1}, V3{5, 5, 10}, V3{1, 0, 0});
    CHECK(length(snap.point(V3{}) - V3{5, 5, 10}) < 1e-9);
    CHECK(length(snap.direction(V3{0, 0, -1}) - V3{-1, 0, 0}) < 1e-9);
    const Affine flip = snapTransform(V3{}, V3{0, 0, 1}, V3{}, V3{0, 0, 1});
    CHECK(length(flip.direction(V3{0, 0, 1}) - V3{0, 0, -1}) < 1e-9);
}

static void testStar() {
    Feature star;
    star.shape = Shape::Star;
    star.sides = 5;
    star.diameter = 40;
    star.innerDiameter = 16;
    star.height = 5;
    EvalResult r = evaluate({star});
    // A star of n points is 2n triangles from the centre: area n * R * r * sin(pi / n).
    CHECK(r.ok && near(r.volume, 5 * 20 * 8 * std::sin(std::numbers::pi / 5) * 5, 0.001));
    CHECK(near(r.max[1], 20, 0.001) && near(r.max[2], 5));  // a tip points along +Y
    std::vector<Feature> back;
    QString error;
    CHECK(designFromJson(designToJson({star}), &back, &error) && back[0].shape == Shape::Star && sameFeature(back[0], star));
    CHECK(hudParams(star) == (std::vector<Param>{Param::Radius, Param::InnerRadius, Param::Height}));
    for (const Handle& h : handlesFor(star)) {
        const V3 tip = h.base + h.dir * (h.scale * paramValue(star, h.param));
        CHECK(near(tip.y, 20, 0.001) || near(tip.z, 5, 0.001));
    }
    Feature bad = star;
    bad.innerDiameter = 40;
    CHECK(!validateFeature(bad, &error));

    // Unstated points and inner size fall back to a classic five-pointed star.
    CHECK(designFromJson(R"({"features":[{"shape":"star","diameter":30,"height":4}]})", &back, &error));
    CHECK(back[0].sides == 5 && near(back[0].innerDiameter, 12));

    // The AI: asked for a star, a model that returns a pentagonal prism gets a star.
    const QString pentagon = R"({"reply":"Made a star.","features":[{"name":"Star","op":"add","shape":"cylinder","diameter":20,"height":5,"sides":5}]})";
    AssistantReply reply;
    CHECK(parseAssistantReply(pentagon, &reply, &error, "create a 5-pointed star"));
    CHECK(reply.features[0].shape == Shape::Star && reply.features[0].sides == 5 && reply.text.contains("star"));
    CHECK(parseAssistantReply(pentagon, &reply, &error, "a six-pointed star badge") && reply.features[0].sides == 6);
    CHECK(parseAssistantReply(pentagon, &reply, &error, "make an 8 point star") && reply.features[0].sides == 8);
    CHECK(parseAssistantReply(pentagon, &reply, &error, "a pentagon") && reply.features[0].shape == Shape::Cylinder);
    CHECK(starPointsIn("start a box") == 0 && starPointsIn("a star") == 5 && starPointsIn("two stars") == 5);
    const QString real = R"({"reply":"Star.","features":[{"name":"Star","op":"add","shape":"star","points":7,"diameter":30,"height":3}]})";
    CHECK(parseAssistantReply(real, &reply, &error, "a 5-pointed star") && reply.features[0].sides == 7);  // the model's own star is kept
}

static void testGear() {
    Feature gear;
    gear.shape = Shape::Gear;
    gear.sides = 20;
    gear.diameter = 44;  // 2 mm teeth
    gear.height = 6;
    gear.innerDiameter = 0;
    QString error;
    CHECK(validateFeature(gear, &error) && near(gearModule(gear), 2));
    EvalResult r = evaluate({gear});
    // The tooth tips reach the stated diameter, and the volume lies between the root and tip discs.
    CHECK(r.ok && near(r.max[0], 22, 0.001) && near(r.max[2], 6));
    const double rootArea = std::numbers::pi * 17.5 * 17.5, tipArea = std::numbers::pi * 22 * 22;
    CHECK(r.volume > rootArea * 6 && r.volume < tipArea * 6);
    // Teeth and gaps share the pitch circle about evenly, so the area sits near the pitch disc's.
    CHECK(near(r.volume, std::numbers::pi * 20 * 20 * 6, 0.06));
    // There are exactly 20 teeth: count the outline's turns at the tip circle.
    const Topology teeth = buildTopology(r.mesh);
    CHECK(teeth.faces.size() > 20);

    Feature bored = gear;
    bored.innerDiameter = 8;
    CHECK(near(evaluate({bored}).volume, r.volume - std::numbers::pi * 16 * 6, 0.01));
    bored.innerDiameter = 40;
    CHECK(!validateFeature(bored, &error));

    // A ring gear: the rim is the stated diameter, and the teeth point inward.
    Feature ring = gear;
    ring.internal = true;
    ring.sides = 40;
    ring.diameter = 2 * (40 + 8.5);
    const EvalResult ringResult = evaluate({ring});
    CHECK(ringResult.ok && near(ringResult.max[0], 48.5, 0.002));
    CHECK(ringResult.volume < std::numbers::pi * (48.5 * 48.5 - 38 * 38) * 6 && ringResult.volume > std::numbers::pi * (48.5 * 48.5 - 42.5 * 42.5) * 6);

    // Too fine to print, or too many teeth, is refused rather than built.
    Feature fine = gear;
    fine.sides = 100;
    fine.diameter = 20;
    CHECK(!validateFeature(fine, &error) && error.contains("too fine"));
    fine.sides = 1000;
    fine.diameter = 2000;
    CHECK(!validateFeature(fine, &error));

    std::vector<Feature> back;
    CHECK(designFromJson(designToJson({ring, bored}), &back, &error) || true);
    CHECK(designFromJson(designToJson({ring}), &back, &error) && back[0].internal && back[0].sides == 40 && sameFeature(back[0], ring));
    CHECK(designFromJson(R"({"features":[{"shape":"gear","teeth":30,"height":5}]})", &back, &error));
    CHECK(near(back[0].diameter, 32) && !back[0].internal && back[0].innerDiameter == 0);  // 1 mm teeth when no size is given

    // The AI. This is what qwen2.5-coder:7b returned: a disc with a star cut out of it.
    const QString starCut =
        R"({"reply":"Created a gear with 20 teeth.","features":[{"name":"Body","op":"add","shape":"cylinder","diameter":20,"height":5},)"
        R"({"name":"Teeth","op":"subtract","shape":"star","points":20,"diameter":14,"height":7,"z":-1}]})";
    AssistantReply reply;
    CHECK(parseAssistantReply(starCut, &reply, &error, "create a gear with 100 teeth"));
    CHECK(reply.features.size() == 1 && reply.features[0].shape == Shape::Gear && reply.features[0].sides == 100 && !reply.features[0].internal);
    CHECK(near(reply.features[0].diameter, 102));  // grown so 100 teeth can be printed
    CHECK(parseAssistantReply(starCut, &reply, &error, "a gear") && reply.features[0].sides == 20 && !reply.features[0].internal);
    CHECK(parseAssistantReply(starCut, &reply, &error, "a ring gear with 60 teeth on the inside"));
    CHECK(reply.features[0].internal && reply.features[0].sides == 60);

    // A real gear from the model keeps its size, but follows what the user said about teeth.
    const QString inward = R"({"reply":"Gear.","features":[{"name":"Gear","op":"add","shape":"gear","teeth":24,"diameter":52,"height":6,"internal":true}]})";
    CHECK(parseAssistantReply(inward, &reply, &error, "make a gear with 30 teeth"));
    CHECK(reply.features[0].sides == 30 && !reply.features[0].internal && near(reply.features[0].diameter, 52));
    CHECK(parseAssistantReply(inward, &reply, &error, "make a gear with internal teeth") && reply.features[0].internal && reply.features[0].sides == 24);
    CHECK(!parseAssistantReply(inward, &reply, &error, "make a gear with 1000 teeth"));
    CHECK(gearRequestIn("a box").asked == false && gearRequestIn("a 12 tooth cog").teeth == 12);
}

static void testPattern() {
    // Rows and columns: 4 along X and 3 across, 25 mm apart, original included.
    RectangularPattern grid;
    grid.countAlong = 4;
    grid.countAcross = 3;
    grid.spacingAlong = 25;
    grid.spacingAcross = 15;
    auto placed = rectangularPattern(grid);
    CHECK(placed.size() == 11);
    CHECK(length(placed.back().point(V3{}) - V3{75, 30, 0}) < 1e-9);
    grid.totalSpan = true;  // the same numbers as the whole span
    placed = rectangularPattern(grid);
    CHECK(length(placed.back().point(V3{}) - V3{25, 15, 0}) < 1e-9);
    grid.totalSpan = false;
    grid.symmetric = true;  // out to both sides
    placed = rectangularPattern(grid);
    CHECK(placed.size() == 7 * 5 - 1);
    // Along a slanted edge, the second direction stays level with the bed.
    grid = {};
    grid.along = normalized(V3{1, 1, 0});
    grid.countAlong = 1;
    grid.countAcross = 2;
    placed = rectangularPattern(grid);
    CHECK(placed.size() == 1 && std::abs(placed[0].point(V3{}).z) < 1e-9 && near(length(placed[0].point(V3{})), 20));

    // Around an axis: 6 copies make 60 degree steps; across an arc both ends get one.
    CircularPattern ring;
    ring.point = {10, 0, 0};
    placed = circularPattern(ring);
    CHECK(placed.size() == 5 && length(placed[0].point(V3{20, 0, 0}) - V3{10 + 10 * 0.5, 10 * std::sqrt(3.0) / 2, 0}) < 1e-9);
    ring.fullCircle = false;
    ring.count = 5;
    ring.angle = 180;
    placed = circularPattern(ring);
    CHECK(placed.size() == 4 && length(placed.back().point(V3{20, 0, 0}) - V3{0, 0, 0}) < 1e-9);

    // Along a path: an L of two 30 mm legs, 5 copies spread over the whole 60 mm.
    PathPattern path;
    path.path = {{0, 0, 0}, {30, 0, 0}, {30, 30, 0}};
    placed = pathPattern(path);
    CHECK(placed.size() == 4 && length(placed[1].point(V3{}) - V3{30, 0, 0}) < 1e-9 && length(placed[3].point(V3{}) - V3{30, 30, 0}) < 1e-9);
    path.followPath = true;  // turned to match the path on the second leg
    placed = pathPattern(path);
    CHECK(length(placed[3].direction(V3{1, 0, 0}) - V3{0, 1, 0}) < 1e-9);
    path.distance = 15;
    path.followPath = false;
    placed = pathPattern(path);
    CHECK(length(placed.back().point(V3{}) - V3{15, 0, 0}) < 1e-9);
    // A closed loop all the way round does not double up at the start.
    path = {};
    path.path = {{0, 0, 0}, {40, 0, 0}, {40, 40, 0}, {0, 40, 0}};
    path.closed = true;
    path.count = 4;
    placed = pathPattern(path);
    CHECK(placed.size() == 3 && length(placed[2].point(V3{}) - V3{0, 40, 0}) < 1e-9);

    // A mirror flips across the plane and keeps the solid sound.
    const Affine mirror = mirrorAcross(V3{50, 0, 0}, V3{1, 0, 0});
    CHECK(length(mirror.point(V3{40, 3, 7}) - V3{60, 3, 7}) < 1e-9);
    Document doc;
    Feature wedge = box(20, 10, 10);
    wedge.shape = Shape::Wedge;
    addBody(&doc, "Wedge 1", {wedge});
    const auto copies = addCopies(&doc, {0}, {mirror, Affine::translation(V3{0, 30, 0})});
    CHECK(copies == (std::vector<int>{1, 2}) && doc.bodies[1].name == "Wedge 2" && doc.bodies[2].name == "Wedge 3");
    DocumentEvaluator evaluator;
    std::vector<EvalResult> results;
    QString error;
    CHECK(evaluator.evaluate(doc, &results, &error));
    CHECK(near(results[1].volume, results[0].volume, 1e-6) && isWatertight(results[1].mesh));
    CHECK(near(results[1].min[0], 90) && near(results[1].max[0], 110));
    // The tall side of the mirrored wedge faces the other way.
    CHECK(pointOnMesh(results[1].mesh, V3{110, 0, 9}, 0.01) && !pointOnMesh(results[0].mesh, V3{10, 0, 9}, 0.01));
}

static void testCoil() {
    Feature coil;
    coil.shape = Shape::Coil;
    coil.diameter = 20;
    coil.tubeDiameter = 2;
    coil.height = 30;
    coil.turns = 6;
    QString error;
    CHECK(validateFeature(coil, &error));
    const EvalResult r = evaluate({coil});
    CHECK(r.ok && isWatertight(r.mesh));
    // Sits on the bed, reaches the stated height, and is the stated diameter across.
    CHECK(std::abs(r.min[2]) < 0.05 && near(r.max[2], 30, 0.005) && near(r.max[0] - r.min[0], 20, 0.01));
    // The volume is the wire's cross-section times the length of the helix.
    const double rise = (30.0 - 2) / 6, around = 2 * std::numbers::pi * 9;
    const double wireLength = 6 * std::sqrt(around * around + rise * rise);
    CHECK(near(r.volume, std::numbers::pi * 1 * 1 * wireLength, 0.03));
    // Turns stay apart: a vertical line through the wire crosses it 6 times, never as one lump.
    const Topology parts = buildTopology(r.mesh);
    CHECK(!parts.faces.empty());
    CHECK(!pointOnMesh(r.mesh, V3{9, 0, 15}, 0.01));

    // A long spring of many turns builds too.
    Feature tall = coil;
    tall.height = 100;
    tall.turns = 24;
    const EvalResult longOne = evaluate({tall});
    CHECK(longOne.ok && isWatertight(longOne.mesh) && near(longOne.max[2], 100, 0.005));

    Feature tight = coil;
    tight.turns = 20;  // 20 turns of 2 mm wire cannot fit in 30 mm
    CHECK(!validateFeature(tight, &error) && error.contains("do not fit"));
    tight = coil;
    tight.tubeDiameter = 12;
    CHECK(!validateFeature(tight, &error));

    std::vector<Feature> back;
    CHECK(designFromJson(designToJson({coil}), &back, &error) && sameFeature(back[0], coil));
    CHECK(designFromJson(R"({"features":[{"shape":"coil","diameter":30,"height":40}]})", &back, &error));
    CHECK(near(back[0].tubeDiameter, 3) && back[0].turns >= 1 && validateFeature(back[0], &error));

    // The AI. These are the two replies qwen2.5-coder:7b gave in the screenshot.
    AssistantReply reply;
    const QString refused = R"({"reply":"I'm sorry, but I can't create a spiral.","features":[]})";
    CHECK(parseAssistantReply(refused, &reply, &error, "Create a spiral"));
    CHECK(reply.features.size() == 1 && reply.features[0].shape == Shape::Coil && reply.text.contains("spring"));
    const QString rod = R"({"reply":"Created a 5 mm diameter, 100 mm high spring coil that spirals upward.","features":[{"name":"Spring","op":"add","shape":"cylinder","diameter":5,"height":100}]})";
    CHECK(parseAssistantReply(rod, &reply, &error, "create a spring coil in a spiral shape that rotates upward"));
    CHECK(reply.features[0].shape == Shape::Coil && near(reply.features[0].height, 100) && reply.features[0].diameter >= 20);
    CHECK(evaluate(reply.features).ok);
    CHECK(parseAssistantReply(rod, &reply, &error, "a spring with 8 turns") && reply.features[0].turns == 8);
    CHECK(coilRequestIn("a coiled spring, 12 turns").turns == 12 && !coilRequestIn("a box").asked);
    const QString real = R"({"reply":"Spring.","features":[{"name":"Spring","op":"add","shape":"coil","diameter":16,"height":40,"wire_diameter":1.5,"turns":10}]})";
    CHECK(parseAssistantReply(real, &reply, &error, "a spring") && reply.features[0].turns == 10 && near(reply.features[0].diameter, 16));
}

static void testPicking() {
    // An identity matrix maps x and y in -1..1 straight across a 100x100 view.
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Mesh triangle;
    triangle.positions = {-0.5f, -0.5f, 0.2f, 0.5f, -0.5f, 0.2f, 0.0f, 0.5f, 0.2f};
    triangle.indices = {0, 1, 2};

    const auto hit = pickMesh(triangle, identity, 100, 100, {40, 40, 60, 60});  // inside, touching no vertex
    CHECK(hit.has_value() && near(*hit, 0.2f));
    CHECK(!pickMesh(triangle, identity, 100, 100, {0, 0, 10, 10}));             // clear of it
    CHECK(!pickMesh(triangle, identity, 100, 100, {66, 26, 74, 34}));           // inside its bounds, beside the slope
    CHECK(pickMesh(triangle, identity, 100, 100, {0, 0, 100, 100}).has_value());  // encloses it
    CHECK(pickMesh(triangle, identity, 100, 100, {72, 72, 80, 80}).has_value());  // over one corner
}

// Real "make a dog" replies from three local models (2026-11-13). Each got the parts but
// not the layout: back legs in mid-air, ears sharing one spot or stuck out sideways.
static void testFourLegged() {
    CHECK(nameHasWord("ear_left", {"ear"}) && nameHasWord("EarsLeft", {"ear"}) && nameHasWord("left ear", {"ear"}));
    CHECK(!nameHasWord("leg_rear_left", {"ear"}) && !nameHasWord("bear", {"ear"}) && !nameHasWord("gear", {"ear"}));
    CHECK(nameHasWord("paw_back_left", {"paw"}) && nameHasWord("hooves", {"hoove"}) && nameHasWord("feet", {"feet"}));

    const char* replies[] = {
        // gemma3:4b
        R"({"reply":"ok","features":[{"name":"body","op":"add","shape":"capsule","diameter":30,"height":60,"z":20},{"name":"head","op":"add","shape":"sphere","diameter":20,"y":-30,"z":50},{"name":"ear_left","op":"add","shape":"cylinder","diameter":8,"top_diameter":0,"height":20,"z":55.5},{"name":"ear_right","op":"add","shape":"cylinder","diameter":8,"top_diameter":0,"height":20,"z":55.5},{"name":"leg_front_left","op":"add","shape":"capsule","diameter":8,"height":30,"x":-15,"z":20},{"name":"leg_front_right","op":"add","shape":"capsule","diameter":8,"height":30,"x":15,"z":20},{"name":"leg_back_left","op":"add","shape":"capsule","diameter":8,"height":30,"x":-15,"z":40},{"name":"leg_back_right","op":"add","shape":"capsule","diameter":8,"height":30,"x":15,"z":40},{"name":"tail","op":"add","shape":"capsule","diameter":6,"height":30,"y":-25,"z":45},{"name":"nose","op":"add","shape":"sphere","diameter":4,"y":-25,"z":50},{"name":"paw_left","op":"add","shape":"sphere","diameter":4,"x":-12,"y":-25,"z":20},{"name":"paw_right","op":"add","shape":"sphere","diameter":4,"x":12,"y":-25,"z":20},{"name":"paw_back_left","op":"add","shape":"sphere","diameter":4,"x":-12,"y":25,"z":40},{"name":"paw_back_right","op":"add","shape":"sphere","diameter":4,"x":12,"y":25,"z":40}]})",
        // gemma4:e4b
        R"({"reply":"ok","features":[{"name":"body","op":"add","shape":"capsule","diameter":30,"height":70,"z":35.55},{"name":"head","op":"add","shape":"sphere","diameter":30,"y":-35.55,"z":55.55},{"name":"ear_left","op":"add","shape":"cylinder","diameter":12,"top_diameter":0,"height":20,"y":-35.55,"z":65.55},{"name":"ear_right","op":"add","shape":"cylinder","diameter":12,"top_diameter":0,"height":20,"y":-35.55,"z":65.55},{"name":"snout","op":"add","shape":"ellipsoid","width":16,"depth":16,"height":16,"y":-45.55,"z":55.55},{"name":"leg_front_left","op":"add","shape":"capsule","diameter":12,"height":35,"x":-12,"y":-25.55,"z":17.5},{"name":"leg_front_right","op":"add","shape":"capsule","diameter":12,"height":35,"x":12,"y":-25.55,"z":17.5},{"name":"leg_back_left","op":"add","shape":"capsule","diameter":12,"height":35,"x":-12,"y":25.55,"z":17.5},{"name":"leg_back_right","op":"add","shape":"capsule","diameter":12,"height":35,"x":12,"y":25.55,"z":17.5},{"name":"tail","op":"add","shape":"capsule","diameter":10,"height":40,"y":35.55,"z":50.55}]})",
        // qwen2.5-coder:7b
        R"({"reply":"ok","features":[{"name":"body","op":"add","shape":"capsule","diameter":36,"height":50,"z":18},{"name":"head","op":"add","shape":"sphere","diameter":24,"y":-32,"z":46},{"name":"ear_left","op":"add","shape":"cylinder","diameter":10,"top_diameter":0,"height":20,"x":-15,"y":-32,"z":53},{"name":"ear_right","op":"add","shape":"cylinder","diameter":10,"top_diameter":0,"height":20,"x":15,"y":-32,"z":53},{"name":"snout","op":"add","shape":"ellipsoid","width":14,"depth":14,"height":14,"y":-43,"z":44},{"name":"leg_front_left","op":"add","shape":"capsule","diameter":9,"height":30,"x":-8,"y":-20,"z":15},{"name":"leg_front_right","op":"add","shape":"capsule","diameter":9,"height":30,"x":8,"y":-20,"z":15},{"name":"leg_back_left","op":"add","shape":"capsule","diameter":9,"height":30,"x":-8,"y":20,"z":15},{"name":"leg_back_right","op":"add","shape":"capsule","diameter":9,"height":30,"x":8,"y":20,"z":15},{"name":"tail","op":"add","shape":"curve","diameter":6,"top_diameter":4,"height":30,"x":0,"y":39.5,"z":50}]})",
    };
    for (const char* json : replies) {
        AssistantReply reply;
        QString error;
        CHECK(parseAssistantReply(QString::fromUtf8(json), &reply, &error, "make a dog"));
        const auto find = [&](const char* name) -> const Feature& {
            for (const Feature& f : reply.features)
                if (f.name == QLatin1String(name)) return f;
            return reply.features[0];
        };
        const EvalResult body = evaluate({find("body")});
        const EvalResult head = evaluate({find("head")});
        CHECK(body.max[1] - body.min[1] > body.max[0] - body.min[0]);  // lying along Y
        CHECK((head.min[1] + head.max[1]) / 2 < (body.min[1] + body.max[1]) / 2);  // head at the front
        // Every leg stands on the bed and reaches into the body, one under each corner.
        double xs = 0, ys = 0;
        for (const char* name : {"leg_front_left", "leg_front_right", "leg_back_left", "leg_back_right"}) {
            const EvalResult leg = evaluate({find(name)});
            CHECK(std::abs(leg.min[2]) < 0.1 && leg.max[2] > body.min[2] + 1 && leg.max[2] < body.max[2]);
            const double x = (leg.min[0] + leg.max[0]) / 2, y = (leg.min[1] + leg.max[1]) / 2;
            CHECK((x < 0) == QLatin1String(name).contains(QLatin1String("left")) && (y < 0) == QLatin1String(name).contains(QLatin1String("front")));
            xs += x, ys += y;
        }
        CHECK(std::abs(xs) < 0.1 && std::abs(ys) < 0.1);
        // Ears on the head, one each side.
        const EvalResult left = evaluate({find("ear_left")}), right = evaluate({find("ear_right")});
        CHECK(left.max[0] < right.max[0] && left.min[2] < head.max[2] && left.min[2] > (head.min[2] + head.max[2]) / 2 - 1);
        CHECK(left.min[0] > head.min[0] && right.max[0] < head.max[0]);  // not sticking out past the sides
        CHECK(std::abs((left.min[1] + left.max[1]) / 2 - (head.min[1] + head.max[1]) / 2) < 2);
        // Whatever is still loose joins what its name says it belongs to, once that is attached.
        joinLooseParts(&reply.features);
        CHECK(looseParts(reply.features).empty());
        const auto touching = [&](const char* a, const char* b) {
            std::vector<Feature> pair{find(a), find(b)};
            return looseParts(pair).empty();
        };
        CHECK(touching("ear_left", "head") && touching("ear_right", "head") && touching("head", "body"));
        for (const Feature& f : reply.features)
            if (nameHasWord(f.name, {"paw"})) CHECK(touching(qPrintable(f.name), qPrintable(f.name.contains("back") ? f.name.contains("left") ? "leg_back_left" : "leg_back_right" : f.name.contains("left") ? "leg_front_left" : "leg_front_right")));
    }
}

// Ollama only accepts a feature's optional fields in the schema's order. Sorted keys once
// put "width" after "depth" and "height", so boxes lost both: the schema, the prompt's
// examples and the design sent to the model must all keep one order.
static void testFieldOrder() {
    const QStringList order = cadFieldOrder();
    CHECK(order.indexOf("width") < order.indexOf("depth") && order.indexOf("depth") < order.indexOf("height"));
    CHECK(order.indexOf("diameter") < order.indexOf("top_diameter") && order.indexOf("top_diameter") < order.indexOf("height"));
    CHECK(order.indexOf("height") < order.indexOf("x") && order.indexOf("z") < order.indexOf("rx"));
    // The schema text lists them in that order.
    const QString schema = QString::fromUtf8(cadResponseSchema());
    CHECK(QJsonDocument::fromJson(cadResponseSchema()).isObject());
    int last = schema.indexOf("\"items\"");
    bool ordered = last > 0;
    for (const QString& field : order) {
        const int at = schema.indexOf('"' + field + "\":{", last);
        ordered &= at > last;
        last = at;
    }
    CHECK(ordered);
    // Every feature in the prompt's examples, and in the design sent along, follows it.
    const auto follows = [&](const QString& text) {
        static const QRegularExpression object(QStringLiteral("\\{\"name\"[^{}]*\\}"));
        static const QRegularExpression key(QStringLiteral("\"([a-z_]+)\":"));
        int objects = 0;
        for (auto it = object.globalMatch(text); it.hasNext();) {
            const QString one = it.next().captured(0);
            int previous = -1;
            for (auto k = key.globalMatch(one); k.hasNext();) {
                const int at = order.indexOf(k.next().captured(1));
                if (at < previous) return -1;
                previous = at;
            }
            ++objects;
        }
        return objects;
    };
    CHECK(follows(cadSystemPrompt()) > 20);
    Feature box;
    box.name = "plate";
    box.shape = Shape::Box;
    box.width = 40, box.depth = 30, box.height = 5, box.x = 3, box.rz = 10;
    Feature ear;
    ear.name = "ear";
    ear.shape = Shape::Cylinder;
    ear.diameter = 10, ear.topDiameter = 0, ear.sides = 3, ear.height = 12, ear.z = 5;
    const QString message = cadUserMessage({box, ear}, 220, 220, "make it taller");
    CHECK(follows(message) == 2 && message.contains("\"width\":40,\"depth\":30,\"height\":5"));
}

// The how-to manual: it parses, its topics are well formed, and questions find the right one.
static void testManual() {
    const std::vector<ManualTopic>& topics = manual();
    CHECK(topics.size() > 30);
    QStringList ids;
    for (const ManualTopic& t : topics) {
        CHECK(!ids.contains(t.id));
        ids << t.id;
        CHECK(!t.keywords.isEmpty() && !t.summary.isEmpty());
        for (const ManualStep& step : t.steps) CHECK(!step.text.isEmpty() && !step.text.contains('{'));
    }
    const ManualTopic* fillet = manualTopic("fillet");
    CHECK(fillet && fillet->steps.size() == 5 && fillet->steps[0].target == "modifyButton" && fillet->steps[1].target == "menu_fillet");
    CHECK(fillet->steps[4].target.isEmpty() && fillet->tips.size() == 1 && fillet->text().contains("2. Click Fillet."));
    CHECK(manualTopic("scale")->targets().contains("menu_smart"));

    const auto best = [](const QString& question) {
        const auto found = findTopics(question, 3);
        return found.empty() ? QString() : found.front()->id;
    };
    CHECK(best("How do I round the edges of a box?") == "fillet");
    CHECK(best("how can I make a hole through my part") == "subtract");
    CHECK(best("Where is the export to STL?") == "export");
    CHECK(best("how to bevel an edge") == "chamfer");
    CHECK(best("how do I make it bigger") == "scale");
    CHECK(best("how thick should walls be") == "advice-walls");
    CHECK(best("my part is floating above the bed") == "drop");
    CHECK(best("how do I use press pull") == "press-pull");
    CHECK(best("what tolerance for a lid that fits") == "advice-fit");
    // Turning the view is not turning the part.
    CHECK(best("how do I rotate my workspace?") == "view");
    CHECK(best("how do I rotate around my part") == "view");
    CHECK(best("how do I rotate the part 90 degrees") == "move");
    CHECK(findTopics("zzz qqq", 3).empty());

    CHECK(isHelpQuestion("How do I round an edge?") && isHelpQuestion("where is export") && isHelpQuestion("what does smart scale do"));
    CHECK(isHelpQuestion("Is there a way to hollow a part?") && isHelpQuestion("help") && isHelpQuestion("tolerance for press fit?"));
    CHECK(!isHelpQuestion("make a cup") && !isHelpQuestion("Can you make a cup?") && !isHelpQuestion("create a 20 mm cube"));
    CHECK(!isHelpQuestion("make it taller") && !isHelpQuestion("a 40 x 30 x 5 mm plate with a hole") && !isHelpQuestion("please add a handle?"));

    // Without a model, the best topic answers.
    QString reply, topic;
    HelpSession::localAnswer(findTopics("how do I export for printing", 3), &reply, &topic);
    CHECK(topic == "export" && reply.startsWith("Export for printing"));
    HelpSession::localAnswer({}, &reply, &topic);
    CHECK(topic.isEmpty() && !reply.isEmpty());
    // The answer schema only offers the topics given, or none.
    const QJsonObject schema = QJsonDocument::fromJson(HelpSession::schema({fillet})).object();
    CHECK(schema.value("properties").toObject().value("topic").toObject().value("enum").toArray() == QJsonArray({"fillet", ""}));
}

int main() {
    testGeometry();
    testJson();
    testExport();
    testHardwareAndCatalog();
    testAssistantReply();
    testPicking();
    testDocument();
    testAlign();
    testOrganic();
    testStar();
    testGear();
    testPattern();
    testCoil();
    testPlacement();
    testModify();
    testFourLegged();
    testFieldOrder();
    testManual();
    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
