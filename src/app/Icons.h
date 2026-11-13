#pragma once

#include <QColor>
#include <QIcon>

enum class Icon {
    Folder,
    Undo,
    Redo,
    Select,
    Primitives,
    Fit,
    DesignPanel,
    AiPanel,
    Box,
    RoundedBox,
    Cylinder,
    Cone,
    HexPrism,
    Sphere,
    Torus,
    Wedge,
    Star,
    Coil,
    Gear,
    Transform,
    Modify,
    Combine,
    Snap,
    Group,
    Pattern,
    Orbit,
    OrbitPart,
    Zoom,
    VisualStyle,
    Eye,
    GridEye,
    Camera,
    SnapPoints,
    SnapGrid,
    Home,
};

// Icons are drawn in code so they match the theme's text colour on any desktop.
QIcon makeIcon(Icon icon, const QColor& color);
