#include "ai/DesignSession.h"

#include "ai/ModelTuning.h"
#include "ai/OllamaClient.h"
#include "core/Geometry.h"

#include <QJsonObject>

#include <algorithm>

namespace td {

DesignSession::DesignSession(OllamaClient* client, QObject* parent) : QObject(parent), client_(client) {
    connect(client_, &OllamaClient::chatFinished, this, [this](bool ok, const QString& content, const QString& error) {
        if (active_) onChatFinished(ok, content, error);
    });
}

void DesignSession::start(const QString& model, const std::vector<Feature>& current, int bedWidth, int bedDepth, const QString& request) {
    active_ = true;
    model_ = model;
    request_ = request;
    repairsLeft_ = 1;
    checksLeft_ = 1;
    lastGood_.reset();
    newDesign_ = current.empty();
    keepsEdits_ = std::any_of(current.begin(), current.end(), [](const Feature& f) {
        return f.shape == Shape::Prisms || f.shape == Shape::Compound || f.shape == Shape::Mesh;
    });
    messages_ = QJsonArray{QJsonObject{{"role", "system"}, {"content", systemPromptFor(model)}},
                           QJsonObject{{"role", "user"}, {"content", cadUserMessage(current, bedWidth, bedDepth, request)}}};
    emit status(tr("Loading %1…").arg(model));
    client_->chat(model, messages_, cadResponseSchema(), tuningFor(model).options());
}

void DesignSession::stop() {
    if (!active_) return;
    active_ = false;
    client_->cancel();
}

void DesignSession::ask(const QString& content, const QString& followUp) {
    messages_.append(QJsonObject{{"role", "assistant"}, {"content", content}});
    messages_.append(QJsonObject{{"role", "user"}, {"content", followUp}});
    client_->chat(model_, messages_, cadResponseSchema(), tuningFor(model_).options());
}

void DesignSession::onChatFinished(bool ok, const QString& content, const QString& error) {
    if (!ok) {
        emit note(tr("Error"), tr("%1 failed: %2").arg(model_, error));
        end(false);
        return;
    }

    AssistantReply reply;
    QString problem;
    if (!parseAssistantReply(content, &reply, &problem, request_)) {
        if (lastGood_) {
            emit note(tr("Cubra"), tr("The fixed version could not be used (%1), so Cubra kept the earlier one.").arg(problem));
            apply(*lastGood_);
            return;
        }
        if (repairsLeft_ > 0) {
            --repairsLeft_;
            emit status(tr("Asking %1 to fix its answer…").arg(model_));
            ask(content, QStringLiteral("That design was rejected: %1. Return the corrected complete JSON.").arg(problem));
            return;
        }
        emit note(tr("Error"), tr("The model's design could not be used, so nothing was changed: %1").arg(problem));
        end(false);
        return;
    }
    if (reply.features.empty()) {
        emit note(model_, reply.text);
        end(false, {}, reply.text);
        return;
    }

    // Look the design over the way a person would before printing it, and send back what is wrong.
    const QStringList problems = designProblems(reply.features);
    const bool unchanged = lastGood_ && lastGood_->features.size() == reply.features.size() &&
                           std::equal(reply.features.begin(), reply.features.end(), lastGood_->features.begin(), sameFeature);
    if (!problems.isEmpty() && checksLeft_ > 0 && !unchanged) {
        --checksLeft_;
        emit note(model_, reply.text);
        emit note(tr("Cubra"), tr("Checked the model: %1 Asking %2 to fix it.").arg(problems.join(QLatin1Char(' ')), model_));
        lastGood_ = std::move(reply);
        emit status(tr("Asking %1 to join the loose parts…").arg(model_));
        ask(content, QStringLiteral("Cubra built your design and checked it. %1 Parts of one object must sink a few mm into the part they attach to, "
                                    "so it prints as one piece. Move each loose part so it overlaps its neighbour. Return the complete corrected JSON.")
                         .arg(problems.join(QLatin1Char(' '))));
        return;
    }
    const bool alreadyShown = unchanged || (lastGood_ && lastGood_->text == reply.text);
    if (!alreadyShown) emit note(model_, reply.text);
    lastGood_.reset();
    apply(std::move(reply));
}

QStringList finishDesign(std::vector<Feature>* features, const QString& request, bool newDesign, bool keepsEdits) {
    QStringList notes;
    // Whatever the model left floating, slide in until it touches what it belongs to.
    if (const QStringList joined = joinLooseParts(features); !joined.isEmpty())
        notes << QObject::tr("Moved %1 in until they overlap the rest, so it prints as one piece.").arg(joined.join(QStringLiteral(", ")));
    // Small models get a figure's parts in proportion but not its size: a teddy bear half a
    // metre tall. With no size asked for, make it a usual figure height.
    if (newDesign && !sizeStatedIn(request) && isFigure(*features)) {
        const EvalResult result = evaluate(*features);
        const double height = result.max[2] - result.min[2];
        constexpr double kFigureHeight = 80;
        if (result.ok && height > 0 && (height > kFigureHeight * 1.25 || height < kFigureHeight * 0.6)) {
            scaleDesign(features, kFigureHeight / height);
            notes << QObject::tr("Scaled it from %1 mm to %2 mm tall. Ask for a size to change it.").arg(height, 0, 'f', 0).arg(kFigureHeight);
        }
    }
    // A design hovering over the bed, or sunk into it, would print badly or not at all.
    if (!keepsEdits)
        if (const double shift = seatOnBed(features); shift != 0)
            notes << (shift > 0 ? QObject::tr("Raised it %1 mm so it rests on the bed.").arg(shift, 0, 'f', 1)
                                : QObject::tr("Lowered it %1 mm so it rests on the bed.").arg(-shift, 0, 'f', 1));
    return notes;
}

void DesignSession::apply(AssistantReply reply) {
    for (const QString& text : finishDesign(&reply.features, request_, newDesign_, keepsEdits_)) emit note(tr("Cubra"), text);
    end(true, reply.features, reply.text);
}

void DesignSession::end(bool applied, const std::vector<Feature>& features, const QString& text) {
    active_ = false;
    emit finished(applied, features, text);
}

}  // namespace td
