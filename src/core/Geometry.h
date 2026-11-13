#pragma once

#include "core/Document.h"
#include "core/Feature.h"
#include "core/Mesh.h"

#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace td {

struct EvalResult {
    bool ok = false;
    QString error;
    Mesh mesh;
    double volume = 0;  // mm^3
    double area = 0;    // mm^2
    std::array<double, 3> min{0, 0, 0};
    std::array<double, 3> max{0, 0, 0};
};

// Applies the features in order. The resulting mesh is always watertight.
EvalResult evaluate(const std::vector<Feature>& features);

// The middle of a curve's rod, a fraction t (0 to 1) along it, in the shape's own space.
V3 curveSpine(const Feature& curve, double t);

// An added part that touches none of the others, so it would print as a loose piece.
struct LoosePart {
    QString name;
    QString nearest;  // the closest part of the main piece
    double gap = 0;   // mm
    int feature = -1;         // index of the loose feature
    int nearestFeature = -1;  // index of the closest feature of the main piece
};
// The added parts that do not join the largest connected piece of the design.
std::vector<LoosePart> looseParts(const std::vector<Feature>& features);

// Slides each loose part straight toward the part it is nearest, until it sinks a
// little into it, so the design prints as one piece. Returns the names of the parts moved.
QStringList joinLooseParts(std::vector<Feature>* features);

// Keeps each intermediate solid, so re-evaluating a design that only changed
// at its end (the usual case while dragging) redoes only that last step.
class Evaluator {
public:
    Evaluator();
    EvalResult evaluate(const std::vector<Feature>& features);

private:
    friend class DocumentEvaluator;
    bool settle(const std::vector<Feature>& features, void* solid, QString* error);  // solid: a kernel Manifold
    struct State;
    std::shared_ptr<State> state_;
};

// Evaluates each body of a document on its own, remembering the work per body.
class DocumentEvaluator {
public:
    DocumentEvaluator();

    // World-space results in body order; false if any body cannot be built.
    bool evaluate(const Document& document, std::vector<EvalResult>* bodies, QString* error);
    // One body with extra features appended, for previewing an edit.
    EvalResult evaluateBody(const Body& body, const std::vector<Feature>& extra);
    // Everything as one solid, which is what gets printed.
    EvalResult combined(const Document& document);
    // The physically separate pieces of a body, in world space.
    std::vector<Mesh> pieces(const Body& body);

private:
    struct State;
    std::shared_ptr<State> state_;
};

// Triangles with their own corner normals, averaged across smooth creases only,
// so curved surfaces look curved and sharp edges stay sharp.
struct DisplayMesh {
    std::vector<float> positions;
    std::vector<float> normals;
};
DisplayMesh buildDisplayMesh(const Mesh& mesh);

// Line segments (x,y,z pairs) along creases sharper than a slight bend, for
// drawing a shape's outline. Coplanar triangle diagonals are left out.
std::vector<float> sharpEdges(const Mesh& mesh);

// Builds one feature on its own, ignoring its op. Used for the edit preview.
EvalResult evaluateSingle(const Feature& feature);

}  // namespace td
