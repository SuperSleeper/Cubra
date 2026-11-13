#pragma once

// A design is a set of separate bodies. Each body has its own feature history,
// a placement, and the groups it belongs to. Bodies only fuse when the user
// merges them, or when the design is exported for printing.

#include "core/Affine.h"
#include "core/Feature.h"
#include "core/Mesh.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <vector>

namespace td {

struct Body {
    int id = 0;  // stable while the app runs, so cached geometry follows the body
    QString name;
    std::vector<Feature> features;  // in the body's own space
    Affine transform;               // body space to world space
    std::vector<int> groups;        // ids of the groups it is in, outermost first
    bool hidden = false;            // out of sight while working; still printed
    uint32_t color = 0;             // 0xRRGGBB chosen by the user, or 0 for the automatic colour
};

struct Document {
    std::vector<Body> bodies;
    int nextBodyId = 1;
    int nextGroupId = 1;
};

QByteArray documentToJson(const Document& document);
// Reads both this format and the older single-history one. In the older one,
// a history of plain additions becomes one body per shape, as it looked.
bool documentFromJson(const QByteArray& json, Document* document, QString* error);

// "Box" -> "Box 3", one past the highest number in use.
QString nextName(const Document& document, const QString& label);
int addBody(Document* document, const QString& name, std::vector<Feature> features, const Affine& transform = {});
int bodyIndex(const Document& document, int id);

// Grouping. `context` is the path of groups the user has entered, outermost first.
bool inContext(const Body& body, const std::vector<int>& context);
// The bodies that a click on `body` selects: its whole group at this level, or itself.
std::vector<int> unitOf(const Document& document, int body, const std::vector<int>& context);
bool isGroupUnit(const Document& document, int body, const std::vector<int>& context);
// Each returns false when there was nothing to do.
bool groupBodies(Document* document, const std::vector<int>& bodies, const std::vector<int>& context);
bool ungroupBodies(Document* document, const std::vector<int>& bodies, const std::vector<int>& context);
bool ungroupAll(Document* document, const std::vector<int>& bodies, const std::vector<int>& context);

// Applies `change` in world space to each listed body.
void transformBodies(Document* document, const std::vector<int>& bodies, const Affine& change);

// Folds each source into the target with op, then removes the sources.
// Returns the target's index afterwards.
int combineBodies(Document* document, int target, const std::vector<int>& sources, Op op);

// Cuts a copy of the body with a plane: one keeps the side the normal points
// to, the other the rest. The second is a new body after the first.
void splitBody(Document* document, int body, V3 point, V3 normal, double size);

// Replaces a body with one body per piece, given as world-space meshes.
void replaceWithPieces(Document* document, int body, const std::vector<Mesh>& pieces);

// Moves fromPoint to toPoint and turns fromNormal to face against toNormal,
// so the two faces sit flush.
Affine snapTransform(V3 fromPoint, V3 fromNormal, V3 toPoint, V3 toNormal);

// How Align lines up one box with another along an axis.
enum class AlignMode { None, Min, Centre, Max };

// The shift that lines box A (low/high corners) up with box B, axis by axis:
// low sides together, centres together, or high sides together.
V3 alignOffset(const std::array<double, 3>& lowA, const std::array<double, 3>& highA, const std::array<double, 3>& lowB,
               const std::array<double, 3>& highB, const std::array<AlignMode, 3>& modes);

}  // namespace td
