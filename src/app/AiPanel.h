#pragma once

#include "ai/GpuProbe.h"
#include "ai/ModelCatalog.h"
#include "ai/OllamaClient.h"
#include "ai/DesignSession.h"
#include "ai/HelpSession.h"
#include "core/Feature.h"

#include <QJsonArray>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTextBrowser;

// Prompt box backed by a local model. When the model chosen for this GPU is
// missing, sending the first prompt downloads it automatically.
class AiPanel : public QWidget {
    Q_OBJECT
public:
    struct Context {
        std::vector<td::Feature> features;
        int bedWidth;
        int bedDepth;
        QString interfaceMap;  // the toolbar and controls in words, for help questions
    };

    explicit AiPanel(std::function<Context()> context, QWidget* parent = nullptr);

    void submit(const QString& prompt);

signals:
    // fresh: a new thing was asked for, to sit beside the design rather than replace it.
    void designProposed(const std::vector<td::Feature>& features, const QString& description, bool fresh);
    void requestFinished(bool applied);
    // The user asked how to do something the manual covers: walk them through it on screen.
    void guideRequested(const QString& topic);

private:
    void refresh();
    void onHardware(const td::HardwareInfo& hardware);
    void onState(bool running, const QStringList& installed, int loadedVramMiB);
    void updateCombo();
    void onModelPicked();
    QString pickedModel() const;
    void proceed();
    void startChat(const QString& model);
    void answer(const QString& model);
    void showAnswer(const QString& reply, const QString& topic, bool chosen);
    void finish(bool applied);
    void cancel();
    void setBusy(bool busy);
    void log(const QString& who, const QString& text);
    bool eventFilter(QObject* watched, QEvent* event) override;

    std::function<Context()> context_;
    td::OllamaClient* client_;
    td::DesignSession* session_;
    td::HelpSession* help_;
    bool asking_ = false;  // the request in progress is a help question
    bool fresh_ = false;

    td::HardwareInfo hardware_;
    td::Recommendation recommendation_;
    bool haveRecommendation_ = false;
    bool serverRunning_ = false;
    bool refreshing_ = false;
    QStringList installed_;

    int pickedIndex_ = 0;  // last accepted picker entry, restored when a switch is cancelled
    QString pendingPrompt_;
    QString activeModel_;
    bool busy_ = false;

    QLabel* status_;
    QComboBox* model_;
    QTextBrowser* transcript_;
    QPlainTextEdit* prompt_;
    QPushButton* send_;
    QPushButton* cancel_;
    QProgressBar* progress_;
    QLabel* progressLabel_;
};
