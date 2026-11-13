// Headless companion to the GUI: builds a design file and exports it.
#include "ai/CadPrompt.h"
#include "ai/DesignSession.h"
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

    if (args.value(0) == "--print-system-prompt") {
        std::fputs(td::cadSystemPrompt().toUtf8().constData(), stdout);
        return 0;
    }
    if (args.value(0) == "--print-schema") {
        std::fputs(QJsonDocument(td::cadResponseSchema()).toJson().constData(), stdout);
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
    if (args.isEmpty() || args[0].startsWith("--")) {
        std::fputs("usage: cubra-cli DESIGN.cubra [--stl OUT.stl] [--3mf OUT.3mf]\n"
                   "       cubra-cli ai \"REQUEST\" OUT.cubra [MODEL]\n", stderr);
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
