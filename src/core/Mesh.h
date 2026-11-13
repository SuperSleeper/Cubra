#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace td {

struct Mesh {
    std::vector<float> positions;   // x,y,z per vertex
    std::vector<uint32_t> indices;  // three per triangle
    size_t triangleCount() const { return indices.size() / 3; }
    size_t vertexCount() const { return positions.size() / 3; }
    bool empty() const { return indices.empty(); }
};

}  // namespace td
