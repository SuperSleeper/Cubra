#include "ai/HelpSession.h"

#include "ai/ModelTuning.h"
#include "ai/OllamaClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace td {

namespace {
// Enough topics to cover a vague question, few enough for a small model's context.
constexpr int kTopics = 5;
}  // namespace

HelpSession::HelpSession(OllamaClient* client, QObject* parent) : QObject(parent), client_(client) {
    connect(client_, &OllamaClient::chatFinished, this, [this](bool ok, const QString& content, const QString& error) {
        if (active_) onChatFinished(ok, content, error);
    });
}

QString HelpSession::systemPrompt(const std::vector<const ManualTopic*>& topics, const QString& interfaceMap) {
    QString prompt = QStringLiteral(
        "You are the help assistant inside Cubra, a CAD program for designing 3D prints. Answer the user's question about using Cubra, "
        "or give advice on designing for 3D printing.\n\n"
        "Rules:\n"
        "1. Use only the manual topics and the interface map below. Never invent buttons, menus or shortcuts.\n"
        "2. \"reply\" is one to three short, plain sentences. Name the button or menu the user needs.\n"
        "3. If one topic shows how to do what the user asks, put its id in \"topic\". Cubra then walks the user through that topic's steps "
        "with arrows on the screen, so do not list the steps yourself.\n"
        "4. If no topic fits, leave \"topic\" empty and say briefly what the manual does cover that comes closest.\n"
        "5. For advice questions, give the practical advice from the manual in the reply.\n\n");
    prompt += QStringLiteral("Interface map (what is on screen now):\n%1\n\nManual topics:\n").arg(interfaceMap.trimmed());
    for (const ManualTopic* topic : topics) prompt += topic->text() + QLatin1Char('\n');
    return prompt;
}

QByteArray HelpSession::schema(const std::vector<const ManualTopic*>& topics) {
    // The topic comes first, so the reply is written knowing which steps will be shown.
    // "None" goes last: listed first, small models (gemma3) chose it every time.
    QJsonArray ids;
    for (const ManualTopic* topic : topics) ids.append(topic->id);
    ids.append(QString());
    return R"({"type":"object","properties":{"reply":{"type":"string"},"topic":{"type":"string","enum":)" + QJsonDocument(ids).toJson(QJsonDocument::Compact) +
           R"(}},"required":["reply","topic"]})";
}

void HelpSession::localAnswer(const std::vector<const ManualTopic*>& topics, QString* reply, QString* topic) {
    if (topics.empty()) {
        *topic = QString();
        *reply = QStringLiteral("I couldn't find that in the manual. Try asking how to add a shape, round an edge, make a hole, or export for printing.");
        return;
    }
    *topic = topics.front()->steps.empty() ? QString() : topics.front()->id;
    *reply = QStringLiteral("%1: %2").arg(topics.front()->title, topics.front()->summary);
}

void HelpSession::start(const QString& model, const QString& question, const QString& interfaceMap) {
    active_ = true;
    topics_ = findTopics(question, kTopics);
    if (model.isEmpty() || topics_.empty()) {
        QString reply, topic;
        localAnswer(topics_, &reply, &topic);
        active_ = false;
        emit finished(reply, topic, false);
        return;
    }
    const QJsonArray messages{QJsonObject{{"role", "system"}, {"content", systemPrompt(topics_, interfaceMap)}},
                              QJsonObject{{"role", "user"}, {"content", question}}};
    emit status(tr("Looking it up in the manual…"));
    client_->chat(model, messages, schema(topics_), tuningFor(model).options());
}

void HelpSession::stop() {
    if (!active_) return;
    active_ = false;
    client_->cancel();
}

void HelpSession::onChatFinished(bool ok, const QString& content, const QString& error) {
    active_ = false;
    QString reply, topic;
    const QJsonObject answer = QJsonDocument::fromJson(content.toUtf8()).object();
    reply = answer.value("reply").toString().trimmed();
    topic = answer.value("topic").toString();
    // Only a topic that was offered, so the walkthrough can trust its targets.
    if (std::none_of(topics_.begin(), topics_.end(), [&](const ManualTopic* t) { return t->id == topic; })) topic.clear();
    if (!ok || reply.isEmpty()) {
        localAnswer(topics_, &reply, &topic);
        if (!ok) reply += QStringLiteral(" (The model failed: %1, so this comes straight from the manual.)").arg(error);
        emit finished(reply, topic, false);
        return;
    }
    // A bare label such as "Export STL…" is not an answer: the topic's own description is.
    if (const ManualTopic* chosen = manualTopic(topic); chosen && reply.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() < 4)
        reply = chosen->summary;
    // Small models often answer well but name no topic. The manual's best match still
    // offers its steps, without starting the walkthrough on its own.
    if (topic.isEmpty()) {
        QString unused;
        localAnswer(topics_, &unused, &topic);
        emit finished(reply, topic, false);
        return;
    }
    emit finished(reply, topic, true);
}

}  // namespace td
