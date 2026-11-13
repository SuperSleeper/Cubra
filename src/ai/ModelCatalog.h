#pragma once

#include <QString>

#include <vector>

namespace td {

struct ModelEntry {
    QString tag;          // Ollama model tag
    double downloadGB;    // approximate download size
    int minVramMiB;       // memory the model needs to run fully on the GPU
};

struct Recommendation {
    ModelEntry model;
    bool onGpu = false;
    bool enoughMemory = true;
    QString reason;
};

// Best model first. Every tag has been confirmed to exist in the Ollama registry.
const std::vector<ModelEntry>& modelCatalog();

// Picks the best model that fits the free VRAM, falling back to the smallest
// model running on the CPU when no GPU has room.
Recommendation recommendModel(int freeVramMiB, int availableRamMiB);

}  // namespace td
