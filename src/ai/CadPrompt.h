#pragma once

#include "core/Feature.h"

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QString>

#include <vector>

namespace td {

// Size in mm used for any length the user and the model leave unstated.
constexpr double kDefaultAiSize = 20.0;

QString cadSystemPrompt();

// JSON schema that constrains the model's output, as text so its fields keep their order.
QByteArray cadResponseSchema();

// The order the schema lists a feature's fields in, which the model must follow.
QStringList cadFieldOrder();

QString cadUserMessage(const std::vector<Feature>& current, int bedWidth, int bedDepth, const QString& request);

// A feature as the model sees it: spheres and capsules by their centre, and a
// stretched sphere as an ellipsoid with a width, depth and height.
QJsonObject featureToAiJson(const Feature& feature);

// What Cubra finds wrong with a design it can build, such as parts floating
// free of the rest, worded for the model to fix. Empty when nothing is wrong.
QStringList designProblems(const std::vector<Feature>& features);

// Whether a request asks for a new thing ("create a banana") rather than a change
// to the design on screen ("make it taller").
bool asksForNewDesign(const QString& request);

// Whether a request gives a size, such as "80 mm tall" or "3 cm".
bool sizeStatedIn(const QString& request);

// A figure built from round parts (spheres, ellipsoids, capsules), as opposed to a mechanical part.
bool isFigure(const std::vector<Feature>& features);

// Scales every size and position by factor, about the origin.
void scaleDesign(std::vector<Feature>* features, double factor);

// Moves the whole design up or down so it rests on the bed. Returns the shift in mm.
double seatOnBed(std::vector<Feature>* features);

struct AssistantReply {
    QString text;
    std::vector<Feature> features;  // empty means the design should not change
};

// Parses the model's output and checks that the design builds a non-empty solid. When the request
// names a polygon ("pentagon") and the model returned only round solids, they get that many sides.
bool parseAssistantReply(const QString& content, AssistantReply* reply, QString* error, const QString& request = {});

// Sides of the polygon a request names, such as 5 for "pentagon", or 0 when it names none.
int polygonSidesIn(const QString& request);

// What a request says about a gear: whether it asks for one, the tooth count
// (0 when unstated) and whether the teeth go on the inside.
struct GearRequest {
    bool asked = false;
    int teeth = 0;
    bool internal = false;
};
GearRequest gearRequestIn(const QString& request);

// What a request says about a spring: whether it asks for one, and the turns (0 when unstated).
struct CoilRequest {
    bool asked = false;
    double turns = 0;
};
CoilRequest coilRequestIn(const QString& request);

// Points of the star a request asks for ("6-pointed star" gives 6), 5 if unstated, or 0 when it asks for none.
int starPointsIn(const QString& request);

}  // namespace td
