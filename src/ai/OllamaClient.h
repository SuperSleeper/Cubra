#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

#include <functional>

class QNetworkReply;

namespace td {

// Talks to a local Ollama server. One pull or chat runs at a time, and only
// one model stays loaded: a chat first unloads every other model, so switching
// models never leaves the old one holding GPU memory.
class OllamaClient : public QObject {
    Q_OBJECT
public:
    explicit OllamaClient(QObject* parent = nullptr);

    void refreshState();
    void pull(const QString& model);
    // schema is JSON text, sent as written so its field order survives.
    void chat(const QString& model, const QJsonArray& messages, const QByteArray& schema, const QJsonObject& options);
    void cancel();
    // Unloads every loaded model except keep (all of them when keep is empty).
    void unloadAllExcept(const QString& keep);
    // Names of the models Ollama holds in memory now (empty if it is not running).
    void loadedModels(std::function<void(const QStringList&)> done);
    // True when a and b name the same model, ignoring an omitted ":latest".
    static bool sameModel(const QString& a, const QString& b);

signals:
    // loadedVramMiB is GPU memory Ollama holds now and would free for a new model.
    void stateReady(bool running, const QStringList& installed, int loadedVramMiB);
    void pullProgress(const QString& status, qint64 completed, qint64 total);
    void pullFinished(bool ok, const QString& error);
    void chatProgress(int characters);
    void chatFinished(bool ok, const QString& content, const QString& error);

private:
    enum class Job { None, Pull, Chat };

    QNetworkRequest jsonRequest(const QString& path) const;
    void unloadOthers(const QString& keep, std::function<void()> done);
    void startStream(Job job, const QString& path, const QByteArray& body);
    void consumeLines(bool flushRemainder);
    void handleLine(const QByteArray& line);
    void finishStream();

    QNetworkAccessManager network_;
    QUrl base_;
    QPointer<QNetworkReply> reply_;
    Job job_ = Job::None;
    QByteArray buffer_;
    QString content_;
    QString streamError_;
    bool complete_ = false;
    // Bumped by cancel() so a chat still waiting on unloads never starts.
    quint64 ticket_ = 0;
};

}  // namespace td
