#pragma once

#include "core/Geometry.h"

#include <QString>

namespace td {

// Both write atomically, so a failed export never damages an existing file.
bool exportStl(const Mesh& mesh, const QString& path, QString* error);
bool export3mf(const Mesh& mesh, const QString& path, const QString& objectName, QString* error);

}  // namespace td
