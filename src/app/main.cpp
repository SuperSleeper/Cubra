#include "app/AiPanel.h"
#include "app/MainWindow.h"

#include <QApplication>
#include <QSettings>
#include <QCommandLineParser>
#include <QIcon>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char** argv) {
    // Must be set before QApplication so that every GL context matches.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setOrganizationName("Cubra");
    QApplication::setApplicationName("Cubra");
    // On Wayland the dock finds the icon through the desktop entry of this name.
    QApplication::setDesktopFileName("cubra");
    QApplication::setWindowIcon(QIcon(":/cubra.png"));
    {
        // Cubra was called 3Design: carry its settings over the first time.
        QSettings settings;
        if (settings.allKeys().isEmpty()) {
            const QSettings old(QStringLiteral("3Design"), QStringLiteral("3Design"));
            for (const QString& key : old.allKeys()) settings.setValue(key, old.value(key));
        }
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("design", "Design file to open.", "[design]");
    const QCommandLineOption promptOption("prompt", "Send this prompt to the AI assistant on startup.", "text");
    const QCommandLineOption screenshotOption("screenshot", "Save a screenshot to this path, then exit.", "png");
    parser.addOption(promptOption);
    const QCommandLineOption iconOption("write-icon", "Save the app icon as a PNG of the given size, then exit.", "png:size");
    parser.addOption(screenshotOption);
    parser.addOption(iconOption);
    parser.process(app);

    if (parser.isSet(iconOption)) {
        const QStringList parts = parser.value(iconOption).split(':');
        const int size = parts.value(1, "256").toInt();
        return QIcon(":/cubra.png").pixmap(size, size).save(parts.value(0)) ? 0 : 1;
    }

    MainWindow window;
    window.showMaximized();
    if (!parser.positionalArguments().isEmpty()) window.openFile(parser.positionalArguments().first());

    const QString screenshot = parser.value(screenshotOption);
    const auto captureAndQuit = [&window, screenshot] {
        window.grab().save(screenshot);
        QApplication::exit(0);
    };

    if (parser.isSet(promptOption)) {
        // Allow the hardware check that starts with the window to finish.
        QTimer::singleShot(500, &window, [&window, &parser, &promptOption] { window.aiPanel()->submit(parser.value(promptOption)); });
        if (!screenshot.isEmpty()) {
            QObject::connect(window.aiPanel(), &AiPanel::requestFinished, &window, [&window, captureAndQuit] {
                QTimer::singleShot(1500, &window, captureAndQuit);
            });
        }
    } else if (!screenshot.isEmpty()) {
        QTimer::singleShot(2000, &window, captureAndQuit);
    }

    return app.exec();
}
