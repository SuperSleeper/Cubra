#include "ai/ModelTuning.h"

#include "ai/CadPrompt.h"

namespace td {

ModelTuning tuningFor(const QString& model) {
    ModelTuning tuning;
    const QString tag = model.toLower();
    // Measured with build-dev/bench: the same seven requests, twice each, per model.
    if (tag.startsWith(QLatin1String("qwen2.5-coder"))) {
        tuning.family = QStringLiteral("qwen2.5-coder");
    } else if (tag.startsWith(QLatin1String("gemma3"))) {
        tuning.family = QStringLiteral("gemma3");
    } else if (tag.startsWith(QLatin1String("gemma4"))) {
        tuning.family = QStringLiteral("gemma4");
    }
    return tuning;
}

QString systemPromptFor(const QString& model) {
    const ModelTuning tuning = tuningFor(model);
    if (tuning.promptNotes.isEmpty()) return cadSystemPrompt();
    return cadSystemPrompt() + QStringLiteral("\nNotes:\n") + tuning.promptNotes + QLatin1Char('\n');
}

}  // namespace td
