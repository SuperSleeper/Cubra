#include "ai/OllamaClient.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <memory>

namespace td {
namespace {

QUrl serverUrl() {
    QString host = qEnvironmentVariable("OLLAMA_HOST").trimmed();
    if (host.isEmpty()) return QUrl(QStringLiteral("http://127.0.0.1:11434"));
    if (!host.contains(QStringLiteral("://"))) host.prepend(QStringLiteral("http://"));
    QUrl url(host);
    if (url.host() == QStringLiteral("0.0.0.0")) url.setHost(QStringLiteral("127.0.0.1"));
    if (url.port() == -1 && url.scheme() == QStringLiteral("http")) url.setPort(11434);
    return url;
}

// Ollama reports "name" with an explicit tag; requests may leave ":latest" off.
QString fullTag(const QString& model) {
    return model.contains(QLatin1Char(':')) ? model : model + QStringLiteral(":latest");
}

}  // namespace

OllamaClient::OllamaClient(QObject* parent) : QObject(parent), base_(serverUrl()) {}

QNetworkRequest OllamaClient::jsonRequest(const QString& path) const {
    QUrl url = base_;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

void OllamaClient::refreshState() {
    QNetworkReply* tags = network_.get(jsonRequest(QStringLiteral("/api/tags")));
    connect(tags, &QNetworkReply::finished, this, [this, tags] {
        tags->deleteLater();
        if (tags->error() != QNetworkReply::NoError) {
            emit stateReady(false, {}, 0);
            return;
        }
        QStringList installed;
        const QJsonArray models = QJsonDocument::fromJson(tags->readAll()).object().value("models").toArray();
        for (const QJsonValue& model : models) installed << model.toObject().value("name").toString();
        installed.sort();

        QNetworkReply* running = network_.get(jsonRequest(QStringLiteral("/api/ps")));
        connect(running, &QNetworkReply::finished, this, [this, running, installed] {
            running->deleteLater();
            qint64 bytes = 0;
            const QJsonArray loaded = QJsonDocument::fromJson(running->readAll()).object().value("models").toArray();
            for (const QJsonValue& model : loaded) bytes += model.toObject().value("size_vram").toInteger();
            emit stateReady(true, installed, static_cast<int>(bytes / (1024 * 1024)));
        });
    });
}

void OllamaClient::startStream(Job job, const QString& path, const QJsonObject& body) {
    cancel();
    job_ = job;
    buffer_.clear();
    content_.clear();
    streamError_.clear();
    complete_ = false;
    QNetworkRequest request = jsonRequest(path);
    // Loading a model or downloading one can legitimately take many minutes.
    request.setTransferTimeout(0);
    reply_ = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        buffer_.append(reply_->readAll());
        consumeLines(false);
    });
    connect(reply_, &QNetworkReply::finished, this, &OllamaClient::finishStream);
}

void OllamaClient::pull(const QString& model) {
    startStream(Job::Pull, QStringLiteral("/api/pull"), QJsonObject{{"model", model}, {"stream", true}});
}

void OllamaClient::unloadAllExcept(const QString& keep) {
    unloadOthers(keep, [] {});
}

void OllamaClient::unloadOthers(const QString& keep, std::function<void()> done) {
    QNetworkReply* running = network_.get(jsonRequest(QStringLiteral("/api/ps")));
    connect(running, &QNetworkReply::finished, this, [this, running, keep, done] {
        running->deleteLater();
        QStringList others;
        const QJsonArray loaded = QJsonDocument::fromJson(running->readAll()).object().value("models").toArray();
        for (const QJsonValue& model : loaded) {
            const QString name = model.toObject().value("name").toString();
            if (!name.isEmpty() && (keep.isEmpty() || fullTag(name) != fullTag(keep))) others << name;
        }
        if (others.isEmpty()) {
            done();
            return;
        }
        // keep_alive 0 unloads; Ollama answers once the memory is released.
        auto remaining = std::make_shared<int>(static_cast<int>(others.size()));
        for (const QString& name : others) {
            QNetworkRequest request = jsonRequest(QStringLiteral("/api/generate"));
            request.setTransferTimeout(30000);
            QNetworkReply* unload = network_.post(
                request, QJsonDocument(QJsonObject{{"model", name}, {"keep_alive", 0}}).toJson(QJsonDocument::Compact));
            connect(unload, &QNetworkReply::finished, this, [unload, remaining, done] {
                unload->deleteLater();
                if (--*remaining == 0) done();
            });
        }
    });
}

void OllamaClient::chat(const QString& model, const QJsonArray& messages, const QJsonObject& schema) {
    cancel();
    const quint64 ticket = ++ticket_;
    const QJsonObject body{{"model", model},
                           {"messages", messages},
                           {"format", schema},
                           {"stream", true},
                           {"think", false},
                           {"options", QJsonObject{{"temperature", 0.1}, {"num_ctx", 8192}}}};
    // Free the GPU of any other model before this one loads.
    unloadOthers(model, [this, ticket, body] {
        if (ticket == ticket_) startStream(Job::Chat, QStringLiteral("/api/chat"), body);
    });
}

void OllamaClient::cancel() {
    ++ticket_;
    if (!reply_) return;
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    job_ = Job::None;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void OllamaClient::consumeLines(bool flushRemainder) {
    qsizetype newline;
    while ((newline = buffer_.indexOf('\n')) >= 0) {
        handleLine(buffer_.left(newline));
        buffer_.remove(0, newline + 1);
    }
    if (flushRemainder && !buffer_.trimmed().isEmpty()) {
        handleLine(buffer_);
        buffer_.clear();
    }
}

void OllamaClient::handleLine(const QByteArray& line) {
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    if (object.isEmpty()) return;
    if (object.contains("error")) {
        streamError_ = object.value("error").toString();
        return;
    }
    if (job_ == Job::Pull) {
        const QString status = object.value("status").toString();
        if (status == QStringLiteral("success")) complete_ = true;
        else emit pullProgress(status, object.value("completed").toInteger(), object.value("total").toInteger());
    } else if (job_ == Job::Chat) {
        content_ += object.value("message").toObject().value("content").toString();
        emit chatProgress(static_cast<int>(content_.size()));
        if (object.value("done").toBool()) complete_ = true;
    }
}

void OllamaClient::finishStream() {
    QNetworkReply* reply = reply_;
    if (!reply) return;
    buffer_.append(reply->readAll());
    consumeLines(true);

    const Job job = job_;
    QString error = streamError_;
    if (error.isEmpty() && !complete_) {
        error = reply->error() != QNetworkReply::NoError ? reply->errorString() : QStringLiteral("the server ended the response early");
    }
    const QString content = content_;
    reply_ = nullptr;
    job_ = Job::None;
    reply->deleteLater();

    if (job == Job::Pull) emit pullFinished(error.isEmpty(), error);
    else if (job == Job::Chat) emit chatFinished(error.isEmpty(), content, error);
}

}  // namespace td
