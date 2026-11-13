#include "app/HoverMenu.h"

#include <QEvent>
#include <QFrame>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kHideMs = 200;
}

HoverMenu::HoverMenu(QToolButton* anchor, QWidget* host, const QString& frameName) : QObject(host), anchor_(anchor), host_(host) {
    frame_ = new QFrame(host_);
    frame_->setObjectName(frameName);
    // The how-to walkthrough points at this button while the list is closed.
    frame_->setProperty("guideAnchor", anchor->objectName());
    frame_->setFrameShape(QFrame::StyledPanel);
    frame_->setAutoFillBackground(true);
    frame_->hide();
    auto* layout = new QVBoxLayout(frame_);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    hideTimer_ = new QTimer(this);
    hideTimer_->setSingleShot(true);
    hideTimer_->setInterval(kHideMs);
    connect(hideTimer_, &QTimer::timeout, frame_, &QWidget::hide);

    anchor_->installEventFilter(this);
    frame_->installEventFilter(this);
    connect(anchor_, &QToolButton::clicked, this, &HoverMenu::show);
}

QToolButton* HoverMenu::addItem(const QString& id, const QString& label, const QIcon& icon, bool enabled, const QString& tip) {
    auto* button = new QToolButton(frame_);
    button->setObjectName(id);
    button->setText(label);
    button->setIcon(icon);
    button->setIconSize(QSize(22, 22));
    button->setToolButtonStyle(icon.isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    button->setEnabled(enabled);
    button->setToolTip(tip);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (icon.isNull()) button->setStyleSheet(QStringLiteral("text-align: left; padding: 4px 12px;"));
    frame_->layout()->addWidget(button);
    connect(button, &QToolButton::clicked, this, [this, id] {
        frame_->hide();
        emit triggered(id);
    });
    return button;
}

void HoverMenu::show() {
    hideTimer_->stop();
    frame_->adjustSize();
    frame_->move(opensLeft_ ? anchor_->mapTo(host_, anchor_->rect().topLeft()) - QPoint(frame_->width(), 0)
                            : anchor_->mapTo(host_, anchor_->rect().bottomLeft()));
    frame_->show();
    frame_->raise();
}

void HoverMenu::hide() { frame_->hide(); }

bool HoverMenu::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Enter) {
        if (watched == anchor_) show();
        hideTimer_->stop();
    } else if (event->type() == QEvent::Leave) {
        hideTimer_->start();
    }
    return QObject::eventFilter(watched, event);
}
