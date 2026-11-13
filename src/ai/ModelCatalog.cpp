#include "ai/ModelCatalog.h"

namespace td {

const std::vector<ModelEntry>& modelCatalog() {
    static const std::vector<ModelEntry> catalog = {
        {QStringLiteral("qwen3-coder:30b"), 19.0, 22000},
        {QStringLiteral("qwen2.5-coder:14b"), 9.0, 11000},
        {QStringLiteral("qwen2.5-coder:7b"), 4.7, 6000},
        {QStringLiteral("qwen3:4b"), 2.6, 3500},
        {QStringLiteral("qwen3:1.7b"), 1.4, 2000},
    };
    return catalog;
}

Recommendation recommendModel(int freeVramMiB, int availableRamMiB) {
    const auto& catalog = modelCatalog();
    Recommendation result;
    for (const ModelEntry& entry : catalog) {
        if (freeVramMiB >= entry.minVramMiB) {
            result.model = entry;
            result.onGpu = true;
            result.reason = QStringLiteral("fits in %1 MiB of free GPU memory").arg(freeVramMiB);
            return result;
        }
    }
    result.model = catalog.back();
    result.enoughMemory = availableRamMiB >= result.model.minVramMiB;
    result.reason = freeVramMiB > 0 ? QStringLiteral("only %1 MiB of GPU memory is free, so it will run on the CPU and be slower").arg(freeVramMiB)
                                    : QStringLiteral("no usable GPU was found, so it will run on the CPU and be slower");
    return result;
}

}  // namespace td
