#pragma once

// Snapping shared by every tool, set from the controls at the bottom right of
// the workspace: whether drags step at all, by how much, and whether new shapes
// seat themselves onto the faces they are dropped on.
struct SnapSettings {
    bool grid = true;       // drags and typed positions step by `linear` and `angular`
    bool implicit = true;   // new shapes seat onto faces under the pointer
    double linear = 1.0;    // mm
    double angular = 15.0;  // degrees
};

SnapSettings& snapSettings();

// Rounds to the linear step when snapping, otherwise to a hundredth of a millimetre.
double snapLength(double value, bool snapping);
// Rounds to the angular step when snapping, otherwise to a tenth of a degree.
double snapAngle(double degrees, bool snapping);
