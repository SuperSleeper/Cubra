#pragma once

#include <QJsonObject>
#include <QString>

namespace td {

// What differs between models. Every model gets the same system prompt (cadSystemPrompt);
// a model family adds only short notes for mistakes measured on that family, and its own
// sampling options. Unknown models get no notes and the defaults.
struct ModelTuning {
    QString family;      // the matched family, such as "gemma4", or empty
    QString promptNotes; // appended to the shared system prompt
    double temperature = 0.1;
    int contextLength = 8192;

    QJsonObject options() const { return QJsonObject{{"temperature", temperature}, {"num_ctx", contextLength}}; }
};

// Matches a model tag such as "gemma4:e4b" or "qwen2.5-coder:7b" to its family's tuning.
ModelTuning tuningFor(const QString& model);

// The shared system prompt followed by the model's own notes.
QString systemPromptFor(const QString& model);

}  // namespace td
