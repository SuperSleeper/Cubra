#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

class QNetworkReply;

namespace td {

// Talks to a local Ollama server. One pull or chat runs at a time.
class OllamaClient : public QObject {
    Q_OBJECT
public:
    explicit OllamaClient(QObject* parent = nullptr);

    void refreshState();
    void pull(const QString& model);
    void chat(const QString& model, const QJsonArray& messages, const QJsonObject& schema);
    void cancel();

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
    void startStream(Job job, const QString& path, const QJsonObject& body);
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
};

}  // namespace td
