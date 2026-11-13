#include "app/AiPanel.h"

#include "ai/CadPrompt.h"

#include <QComboBox>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace {
const QString kAuto = QStringLiteral("__auto__");
}

AiPanel::AiPanel(std::function<Context()> context, QWidget* parent)
    : QWidget(parent), context_(std::move(context)), client_(new td::OllamaClient(this)), session_(new td::DesignSession(client_, this)) {
    auto* root = new QVBoxLayout(this);

    status_ = new QLabel(tr("Checking hardware…"), this);
    status_->setWordWrap(true);
    root->addWidget(status_);

    model_ = new QComboBox(this);
    root->addWidget(model_);

    transcript_ = new QTextBrowser(this);
    transcript_->setOpenExternalLinks(true);
    root->addWidget(transcript_, 1);

    progressLabel_ = new QLabel(this);
    progressLabel_->setWordWrap(true);
    progressLabel_->hide();
    root->addWidget(progressLabel_);
    progress_ = new QProgressBar(this);
    progress_->hide();
    root->addWidget(progress_);

    prompt_ = new QPlainTextEdit(this);
    prompt_->setPlaceholderText(tr("Describe a part, for example: a 60 x 40 x 20 mm box with a 2 mm wall and no lid"));
    prompt_->setMaximumHeight(90);
    prompt_->setToolTip(tr("Enter sends. Shift+Enter starts a new line."));
    prompt_->installEventFilter(this);
    root->addWidget(prompt_);

    auto* buttons = new QHBoxLayout;
    send_ = new QPushButton(tr("Send"), this);
    send_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    send_->setToolTip(tr("Enter"));
    cancel_ = new QPushButton(tr("Cancel"), this);
    cancel_->setEnabled(false);
    buttons->addStretch(1);
    buttons->addWidget(cancel_);
    buttons->addWidget(send_);
    root->addLayout(buttons);

    connect(send_, &QPushButton::clicked, this, [this] { submit(prompt_->toPlainText()); });
    connect(cancel_, &QPushButton::clicked, this, &AiPanel::cancel);
    // Switching models releases the old one straight away. Mid-request the
    // next chat does it instead, so the running reply is not cut off.
    connect(model_, &QComboBox::currentIndexChanged, this, [this] {
        if (busy_) return;
        const QString selection = model_->currentData().toString();
        if (selection == kAuto && !haveRecommendation_) return;
        client_->unloadAllExcept(selection == kAuto ? recommendation_.model.tag : selection);
    });

    connect(client_, &td::OllamaClient::stateReady, this, &AiPanel::onState);
    connect(client_, &td::OllamaClient::pullProgress, this, [this](const QString& status, qint64 completed, qint64 total) {
        progressLabel_->setText(tr("Downloading %1: %2").arg(activeModel_, status));
        progress_->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0) progress_->setValue(static_cast<int>(completed * 1000 / total));
    });
    connect(client_, &td::OllamaClient::pullFinished, this, [this](bool ok, const QString& error) {
        if (!ok) {
            log(tr("Error"), tr("Could not download %1: %2").arg(activeModel_, error));
            finish(false);
            return;
        }
        log(tr("Cubra"), tr("Downloaded %1.").arg(activeModel_));
        installed_ << activeModel_;
        updateCombo();
        startChat(activeModel_);
    });
    connect(client_, &td::OllamaClient::chatProgress, this, [this](int characters) {
        progressLabel_->setText(tr("%1 is writing the design (%2 characters)").arg(activeModel_).arg(characters));
    });
    connect(session_, &td::DesignSession::note, this, &AiPanel::log);
    connect(session_, &td::DesignSession::status, progressLabel_, &QLabel::setText);
    connect(session_, &td::DesignSession::finished, this, [this](bool applied, const std::vector<td::Feature>& features, const QString& text) {
        if (applied) emit designProposed(features, text, fresh_);
        finish(applied);
    });

    refresh();
}

void AiPanel::refresh() {
    if (refreshing_) return;
    refreshing_ = true;
    auto result = std::make_shared<td::HardwareInfo>();
    QThread* thread = QThread::create([result] { *result = td::probeHardware(); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, result] { onHardware(*result); });
    thread->start();
}

void AiPanel::onHardware(const td::HardwareInfo& hardware) {
    hardware_ = hardware;
    client_->refreshState();
}

void AiPanel::onState(bool running, const QStringList& installed, int loadedVramMiB) {
    refreshing_ = false;
    serverRunning_ = running;
    installed_ = installed;

    // Memory Ollama already holds would be released to load a different model.
    const td::GpuInfo* gpu = hardware_.best();
    const int freeVram = gpu ? std::min(gpu->freeMiB + loadedVramMiB, gpu->totalMiB) : 0;
    recommendation_ = td::recommendModel(freeVram, hardware_.ramAvailableMiB);
    haveRecommendation_ = true;

    const QString device = gpu ? tr("%1: %2 of %3 MiB free").arg(gpu->name).arg(freeVram).arg(gpu->totalMiB)
                               : tr("No usable GPU, %1 MiB of system memory available").arg(hardware_.ramAvailableMiB);
    QString text;
    if (!running) {
        text = tr("%1\nOllama is not running. Install it from https://ollama.com or start it with: ollama serve").arg(device);
    } else {
        const QString state = installed_.contains(recommendation_.model.tag)
                                  ? tr("installed")
                                  : tr("%1 GB, downloads on first prompt").arg(recommendation_.model.downloadGB, 0, 'f', 1);
        text = tr("%1\nRecommended: %2 (%3). It %4.").arg(device, recommendation_.model.tag, state, recommendation_.reason);
        if (!recommendation_.enoughMemory) text += tr("\nWarning: system memory is low for this model.");
    }
    status_->setText(text);
    updateCombo();

    if (!pendingPrompt_.isEmpty()) proceed();
}

void AiPanel::updateCombo() {
    const QVariant previous = model_->currentData();
    model_->blockSignals(true);
    model_->clear();
    model_->addItem(haveRecommendation_ ? tr("Automatic (%1)").arg(recommendation_.model.tag) : tr("Automatic"), kAuto);
    for (const QString& name : installed_)
        if (!name.contains(QStringLiteral("embed"))) model_->addItem(name, name);
    const int index = model_->findData(previous);
    model_->setCurrentIndex(index >= 0 ? index : 0);
    model_->blockSignals(false);
}

void AiPanel::submit(const QString& prompt) {
    const QString text = prompt.trimmed();
    if (text.isEmpty() || busy_) return;
    log(tr("You"), text);
    prompt_->clear();
    pendingPrompt_ = text;
    setBusy(true);
    progressLabel_->setText(tr("Checking GPU memory…"));
    progress_->setRange(0, 0);

    // Free memory changes as other programs run, so measure at send time.
    if (refreshing_) return;  // the refresh in flight will pick this up
    refresh();
}

void AiPanel::proceed() {
    if (!serverRunning_) {
        pendingPrompt_.clear();
        log(tr("Error"), tr("Ollama is not running. Install it from https://ollama.com or start it with: ollama serve"));
        finish(false);
        return;
    }
    const QString selection = model_->currentData().toString();
    activeModel_ = selection == kAuto ? recommendation_.model.tag : selection;

    if (!installed_.contains(activeModel_)) {
        log(tr("Cubra"), tr("Downloading %1 (about %2 GB), chosen because it %3.")
                               .arg(activeModel_)
                               .arg(recommendation_.model.downloadGB, 0, 'f', 1)
                               .arg(recommendation_.reason));
        client_->pull(activeModel_);
        return;
    }
    startChat(activeModel_);
}

void AiPanel::startChat(const QString& model) {
    const Context context = context_();
    const QString request = pendingPrompt_;
    pendingPrompt_.clear();
    progress_->setRange(0, 0);
    // "Create a banana" starts over; "make it longer" works on what is there.
    fresh_ = td::asksForNewDesign(request);
    session_->start(model, fresh_ ? std::vector<td::Feature>{} : context.features, context.bedWidth, context.bedDepth, request);
}

void AiPanel::finish(bool applied) {
    setBusy(false);
    emit requestFinished(applied);
}

void AiPanel::cancel() {
    session_->stop();
    client_->cancel();
    pendingPrompt_.clear();
    log(tr("Cubra"), tr("Cancelled."));
    finish(false);
}

void AiPanel::setBusy(bool busy) {
    busy_ = busy;
    send_->setEnabled(!busy);
    cancel_->setEnabled(busy);
    progress_->setVisible(busy);
    progressLabel_->setVisible(busy);
}

bool AiPanel::eventFilter(QObject* watched, QEvent* event) {
    const bool keyEvent = event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride;
    if (watched != prompt_ || !keyEvent) return QWidget::eventFilter(watched, event);
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter) return QWidget::eventFilter(watched, event);

    // Claim Enter before the window's own Enter shortcut can.
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (key->modifiers() & Qt::ShiftModifier) {
        prompt_->insertPlainText(QStringLiteral("\n"));
    } else if (send_->isEnabled()) {
        send_->click();
    }
    return true;
}

void AiPanel::log(const QString& who, const QString& text) {
    transcript_->append(QStringLiteral("<b>%1:</b> %2").arg(who.toHtmlEscaped(), text.toHtmlEscaped()));
}
