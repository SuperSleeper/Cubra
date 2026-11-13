#pragma once

#include "ai/CadPrompt.h"
#include "core/Feature.h"

#include <QJsonArray>
#include <QObject>
#include <QString>

#include <optional>
#include <vector>

namespace td {

class OllamaClient;

// The last steps for every accepted design: join loose parts, size a figure with no size
// asked for, and rest it on the bed. Returns a note for each change, for the transcript.
QStringList finishDesign(std::vector<Feature>* features, const QString& request, bool newDesign, bool keepsEdits);

// One request to the model, from the prompt to a design Cubra can use.
// Replies that do not parse are sent back once to be repaired. Designs that
// build but have loose parts are described back to the model, as a person
// checking them would, for up to two rounds of fixes. The result rests on the bed.
class DesignSession : public QObject {
    Q_OBJECT
public:
    explicit DesignSession(OllamaClient* client, QObject* parent = nullptr);

    // current is the design being changed; empty for a new one.
    void start(const QString& model, const std::vector<Feature>& current, int bedWidth, int bedDepth, const QString& request);
    void stop();
    bool active() const { return active_; }

signals:
    void note(const QString& who, const QString& text);  // for the transcript
    void status(const QString& text);                    // what is happening now
    // applied is false when nothing should change; text is the model's reply.
    void finished(bool applied, const std::vector<td::Feature>& features, const QString& text);

private:
    void onChatFinished(bool ok, const QString& content, const QString& error);
    void ask(const QString& content, const QString& followUp);
    void apply(AssistantReply reply);
    void end(bool applied, const std::vector<Feature>& features = {}, const QString& text = {});

    OllamaClient* client_;
    bool active_ = false;
    QString model_;
    QString request_;
    QJsonArray messages_;
    int repairsLeft_ = 0;
    int checksLeft_ = 0;
    std::optional<AssistantReply> lastGood_;  // kept in case a fix comes back unusable
    bool keepsEdits_ = false;                 // the design has direct edits, which must not shift
    bool newDesign_ = false;                  // nothing to change yet: the model starts from scratch
};

}  // namespace td
