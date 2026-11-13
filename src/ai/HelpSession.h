#pragma once

#include "ai/Manual.h"

#include <QObject>
#include <QString>

#include <vector>

namespace td {

class OllamaClient;

// Answers a question about using Cubra from the how-to manual. The model gets the
// topics that best match the question and a map of the interface as it is now, and
// replies in a few sentences, naming the topic whose steps fit. The steps themselves
// always come from the manual, so the walkthrough never points at something made up.
// Without a model, or if it fails, the best-matching topic answers on its own.
class HelpSession : public QObject {
    Q_OBJECT
public:
    explicit HelpSession(OllamaClient* client, QObject* parent = nullptr);

    // model empty: answer from the manual alone.
    void start(const QString& model, const QString& question, const QString& interfaceMap);
    void stop();
    bool active() const { return active_; }

    // The prompt and the answer schema, exposed for tests and the command-line tool.
    static QString systemPrompt(const std::vector<const ManualTopic*>& topics, const QString& interfaceMap);
    static QByteArray schema(const std::vector<const ManualTopic*>& topics);
    // The answer without a model: the best topic's summary, or a pointer to what the manual covers.
    static void localAnswer(const std::vector<const ManualTopic*>& topics, QString* reply, QString* topic);

signals:
    void status(const QString& text);
    // topic is the manual topic whose steps fit, or empty when none does. chosen is true
    // when the model picked it, false when it only came from searching the manual.
    void finished(const QString& reply, const QString& topic, bool chosen);

private:
    void onChatFinished(bool ok, const QString& content, const QString& error);

    OllamaClient* client_;
    bool active_ = false;
    std::vector<const ManualTopic*> topics_;
};

}  // namespace td
