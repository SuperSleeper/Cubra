#include "app/SnapSettings.h"

#include <cmath>

SnapSettings& snapSettings() {
    static SnapSettings settings;
    return settings;
}

double snapLength(double value, bool snapping) {
    const double step = snapSettings().linear;
    if (snapping && step > 0) return std::round(value / step) * step;
    return std::round(value * 100.0) / 100.0;
}

double snapAngle(double degrees, bool snapping) {
    const double step = snapSettings().angular;
    if (snapping && step > 0) return std::round(degrees / step) * step;
    return std::round(degrees * 10.0) / 10.0;
}
