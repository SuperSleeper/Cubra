// Headless companion to the GUI: builds a design file and exports it.
#include "ai/CadPrompt.h"
#include "ai/DesignSession.h"
#include "ai/HelpSession.h"
#include "ai/ModelTuning.h"
#include "ai/OllamaClient.h"
#include "core/Document.h"
#include "core/Exporters.h"
#include "core/Geometry.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

static int fail(const QString& message) {
    std::fprintf(stderr, "error: %s\n", qPrintable(message));
    return 1;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);

    // With a model, the prompt that model gets: the shared one plus its own notes.
    if (args.value(0) == "--print-system-prompt") {
        std::fputs((args.size() > 1 ? td::systemPromptFor(args[1]) : td::cadSystemPrompt()).toUtf8().constData(), stdout);
        return 0;
    }
    if (args.value(0) == "--print-schema") {
        std::fputs(td::cadResponseSchema().constData(), stdout);
        std::fputc('\n', stdout);
        return 0;
    }
    // Ask the local model for a design, with the same checks the app runs, and save it.
    if (args.value(0) == "ai" && args.size() >= 3) {
        const QString request = args[1];
        const QString out = args[2];
        const QString model = args.value(3, QStringLiteral("qwen2.5-coder:7b"));
        td::OllamaClient client;
        td::DesignSession session(&client);
        int code = 1;
        QObject::connect(&session, &td::DesignSession::note, [](const QString& who, const QString& text) {
            std::fprintf(stderr, "%s: %s\n", qPrintable(who), qPrintable(text));
        });
        QObject::connect(&session, &td::DesignSession::finished, [&](bool applied, const std::vector<td::Feature>& features, const QString&) {
            if (applied) {
                td::Document document;
                td::addBody(&document, QFileInfo(out).completeBaseName(), features);
                QFile file(out);
                if (file.open(QIODevice::WriteOnly) && file.write(td::documentToJson(document)) > 0) code = 0;
                else std::fprintf(stderr, "error: cannot write %s\n", qPrintable(out));
            }
            app.quit();
        });
        session.start(model, {}, 220, 220, request);
        app.exec();
        return code;
    }
    // Answer a question about using Cubra, as the assistant panel does. The interface map
    // comes from "cubra --print-interface-map"; without a model, the manual answers alone.
    if (args.value(0) == "help" && args.size() >= 2) {
        QString map;
        if (QFile mapFile(args.value(3)); !args.value(3).isEmpty() && mapFile.open(QIODevice::ReadOnly)) map = QString::fromUtf8(mapFile.readAll());
        td::OllamaClient client;
        td::HelpSession session(&client);
        QObject::connect(&session, &td::HelpSession::finished, [&](const QString& reply, const QString& topic, bool chosen) {
            std::printf("topic: %s%s\nreply: %s\n", qPrintable(topic.isEmpty() ? QStringLiteral("(none)") : topic), chosen ? "" : " (from the manual search)", qPrintable(reply));
            app.quit();
        });
        session.start(args.value(2), args[1], map);
        if (session.active()) app.exec();
        return 0;
    }
    // Treat a saved raw model reply (CUBRA_AI_TRACE prints them) as the app would, without a model.
    if (args.value(0) == "replay" && args.size() >= 3) {
        QFile in(args[1]);
        if (!in.open(QIODevice::ReadOnly)) return fail(in.errorString());
        const QString request = args.value(3);
        td::AssistantReply reply;
        QString error;
        if (!td::parseAssistantReply(QString::fromUtf8(in.readAll()), &reply, &error, request)) return fail(error);
        std::fprintf(stderr, "reply: %s\n", qPrintable(reply.text));
        for (const QString& note : td::finishDesign(&reply.features, request, true, false)) std::fprintf(stderr, "Cubra: %s\n", qPrintable(note));
        td::Document document;
        td::addBody(&document, QFileInfo(args[2]).completeBaseName(), reply.features);
        QFile out(args[2]);
        if (!out.open(QIODevice::WriteOnly) || out.write(td::documentToJson(document)) <= 0) return fail("cannot write " + args[2]);
        return 0;
    }
    if (args.isEmpty() || args[0].startsWith("--")) {
        std::fputs("usage: cubra-cli DESIGN.cubra [--stl OUT.stl] [--3mf OUT.3mf]\n"
                   "       cubra-cli ai \"REQUEST\" OUT.cubra [MODEL]\n"
                   "       cubra-cli replay REPLY.json OUT.cubra [\"REQUEST\"]\n"
                   "       cubra-cli help \"QUESTION\" [MODEL] [MAP.txt]\n", stderr);
        return 2;
    }

    QFile file(args[0]);
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    td::Document document;
    QString error;
    if (!td::documentFromJson(file.readAll(), &document, &error)) return fail(error);

    // Bodies print as one solid, as the app exports them.
    td::DocumentEvaluator evaluator;
    const td::EvalResult result = evaluator.combined(document);
    if (!result.ok) return fail(result.error);

    for (int i = 1; i + 1 < args.size(); i += 2) {
        bool written = false;
        if (args[i] == "--stl") written = td::exportStl(result.mesh, args[i + 1], &error);
        else if (args[i] == "--3mf") written = td::export3mf(result.mesh, args[i + 1], QFileInfo(args[0]).completeBaseName(), &error);
        else return fail("unknown option " + args[i]);
        if (!written) return fail(error);
    }

    const QJsonObject stats{{"volume", result.volume},
                            {"area", result.area},
                            {"triangles", static_cast<qint64>(result.mesh.triangleCount())},
                            {"min", QJsonArray{result.min[0], result.min[1], result.min[2]}},
                            {"max", QJsonArray{result.max[0], result.max[1], result.max[2]}}};
    std::fputs(QJsonDocument(stats).toJson(QJsonDocument::Compact).constData(), stdout);
    std::fputc('\n', stdout);
    return 0;
}
