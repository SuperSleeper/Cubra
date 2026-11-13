#include "app/GuideOverlay.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace {

const QColor kAccent(255, 140, 26);  // bright orange: stands out on the dark theme and on blue parts

// A see-through layer over a window that draws the ring and the arrow. It never takes
// the mouse, so the user clicks straight through to what it points at.
class GuidePainter : public QWidget {
public:
    explicit GuidePainter(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("guidePainter"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        clock_.start();
    }
    void set(const QRect& ring, bool area, const QRect& from) {
        ring_ = ring;
        area_ = area;
        from_ = from;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (ring_.isNull()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // A slow pulse draws the eye without flashing.
        const double pulse = 0.5 + 0.5 * std::sin(clock_.elapsed() / 260.0);
        const QRect ring = ring_.adjusted(-4, -4, 4, 4);
        if (area_) {
            p.setPen(QPen(kAccent, 3, Qt::DashLine));
            p.drawRoundedRect(ring.adjusted(6, 6, -6, -6), 8, 8);
            return;
        }
        QColor glow = kAccent;
        glow.setAlphaF(0.25 + 0.3 * pulse);
        p.setPen(QPen(glow, 6 + 4 * pulse));
        p.drawRoundedRect(ring, 6, 6);
        p.setPen(QPen(kAccent, 3));
        p.drawRoundedRect(ring, 6, 6);
        if (from_.isNull()) return;

        // From the bubble's edge to the ring's edge, along the line between their centres.
        const QLineF line(from_.center(), ring.center());
        const auto edge = [&](const QRectF& r, bool leaving) {
            const QLineF sides[] = {{r.topLeft(), r.topRight()}, {r.topRight(), r.bottomRight()}, {r.bottomRight(), r.bottomLeft()}, {r.bottomLeft(), r.topLeft()}};
            QPointF best = leaving ? line.p1() : line.p2();
            for (const QLineF& side : sides) {
                QPointF hit;
                if (line.intersects(side, &hit) == QLineF::BoundedIntersection) return hit;
            }
            return best;
        };
        const QPointF start = edge(QRectF(from_), true);
        const QPointF end = edge(QRectF(ring).adjusted(-6, -6, 6, 6), false);
        const QLineF arrow(start, end);
        if (arrow.length() < 12) return;
        p.setPen(QPen(kAccent, 4, Qt::SolidLine, Qt::RoundCap));
        const QLineF shaft(start, arrow.pointAt(1 - 14 / arrow.length()));
        p.drawLine(shaft);
        // The head.
        const double angle = std::atan2(end.y() - start.y(), end.x() - start.x());
        QPainterPath head;
        head.moveTo(end);
        head.lineTo(end - QPointF(std::cos(angle - 0.45) * 18, std::sin(angle - 0.45) * 18));
        head.lineTo(end - QPointF(std::cos(angle + 0.45) * 18, std::sin(angle + 0.45) * 18));
        head.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(kAccent);
        p.drawPath(head);
    }

private:
    QRect ring_, from_;
    bool area_ = false;
    QElapsedTimer clock_;
};

}  // namespace

GuideOverlay::GuideOverlay(QWidget* window, std::function<GuideSpot(const QString&)> locate)
    : QObject(window), window_(window), locate_(std::move(locate)) {
    bubble_ = new QFrame(window_);
    bubble_->setObjectName(QStringLiteral("guideBubble"));
    bubble_->setFrameShape(QFrame::StyledPanel);
    bubble_->setAutoFillBackground(true);
    // A style sheet border drops the frame's own fill, so the background is set here too.
    bubble_->setStyleSheet(QStringLiteral("#guideBubble { background-color: palette(window); border: 2px solid %1; border-radius: 6px; }").arg(kAccent.name()));
    bubble_->setFixedWidth(320);
    auto* layout = new QVBoxLayout(bubble_);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);

    auto* top = new QHBoxLayout;
    heading_ = new QLabel(bubble_);
    heading_->setObjectName(QStringLiteral("guideHeading"));
    heading_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    heading_->setWordWrap(true);
    top->addWidget(heading_, 1);
    auto* close = new QPushButton(QStringLiteral("✕"), bubble_);
    close->setObjectName(QStringLiteral("guideClose"));
    close->setFlat(true);
    close->setFixedWidth(28);
    close->setToolTip(tr("End the walkthrough (Esc does not end it, so it cannot cancel a tool by accident)"));
    top->addWidget(close);
    layout->addLayout(top);

    text_ = new QLabel(bubble_);
    text_->setObjectName(QStringLiteral("guideText"));
    text_->setWordWrap(true);
    layout->addWidget(text_);
    hint_ = new QLabel(bubble_);
    hint_->setObjectName(QStringLiteral("guideHint"));
    hint_->setWordWrap(true);
    hint_->setStyleSheet(QStringLiteral("color: %1;").arg(kAccent.name()));
    layout->addWidget(hint_);

    auto* buttons = new QHBoxLayout;
    back_ = new QPushButton(tr("Back"), bubble_);
    back_->setObjectName(QStringLiteral("guideBack"));
    next_ = new QPushButton(tr("Next"), bubble_);
    next_->setObjectName(QStringLiteral("guideNext"));
    for (QPushButton* b : {back_, next_}) b->setFocusPolicy(Qt::NoFocus);
    close->setFocusPolicy(Qt::NoFocus);
    buttons->addWidget(back_);
    buttons->addStretch(1);
    buttons->addWidget(next_);
    layout->addLayout(buttons);
    bubble_->hide();

    connect(back_, &QPushButton::clicked, this, [this] { goTo(step_ - 1); });
    connect(next_, &QPushButton::clicked, this, [this] { goTo(step_ + 1); });
    connect(close, &QPushButton::clicked, this, &GuideOverlay::stop);

    timer_ = new QTimer(this);
    timer_->setInterval(40);
    connect(timer_, &QTimer::timeout, this, &GuideOverlay::tick);
}

void GuideOverlay::start(const td::ManualTopic& topic) {
    if (topic.steps.empty()) return;
    topic_ = topic;
    active_ = true;
    qApp->installEventFilter(this);
    timer_->start();
    goTo(0);
}

void GuideOverlay::stop() {
    if (!active_) return;
    active_ = false;
    timer_->stop();
    qApp->removeEventFilter(this);
    bubble_->hide();
    if (painter_) painter_->hide();
    spot_ = {};
    emit finished();
}

void GuideOverlay::goTo(int step) {
    if (!active_) return;
    if (step >= int(topic_.steps.size())) {
        stop();
        return;
    }
    step_ = std::max(0, step);
    const int count = int(topic_.steps.size());
    heading_->setText(tr("%1 — step %2 of %3").arg(topic_.title).arg(step_ + 1).arg(count));
    text_->setText(topic_.steps[step_].text);
    back_->setEnabled(step_ > 0);
    next_->setText(step_ + 1 == count ? tr("Done") : tr("Next"));
    nextWasShown_ = nextTargetShown();
    bubble_->show();
    tick();
}

bool GuideOverlay::nextTargetShown() const {
    if (step_ + 1 >= int(topic_.steps.size())) return false;
    const QString target = topic_.steps[step_ + 1].target;
    if (target.isEmpty()) return false;
    const GuideSpot next = locate_(target);
    return next.direct && next.widget && next.widget->isVisible();
}

void GuideOverlay::tick() {
    if (!active_) return;
    // The next step's target appearing means this one is done: a list opened, a tool's panel showed.
    const bool shown = nextTargetShown();
    if (shown && !nextWasShown_) {
        nextWasShown_ = true;
        goTo(step_ + 1);
        return;
    }
    nextWasShown_ = shown;

    spot_ = locate_(topic_.steps[step_].target);
    hint_->setText(spot_.hint);
    hint_->setVisible(!spot_.hint.isEmpty());
    // Wrapped labels need the height for this width, which adjustSize does not ask for.
    bubble_->setFixedHeight(bubble_->layout()->hasHeightForWidth() ? bubble_->layout()->heightForWidth(bubble_->width()) : bubble_->sizeHint().height());

    QWidget* target = spot_.widget && spot_.widget->isVisible() ? spot_.widget.data() : nullptr;
    QWidget* host = target ? target->window() : window_;
    if (!painter_) painter_ = new GuidePainter(host);
    if (painter_->parentWidget() != host) painter_->setParent(host);
    painter_->setGeometry(host->rect());
    painter_->show();
    painter_->raise();

    if (!target) {
        placeBubble({}, false);
        static_cast<GuidePainter*>(painter_.data())->set({}, false, {});
    } else {
        const QRect ring(target->mapTo(host, spot_.rect.topLeft()), spot_.rect.size());
        // Keep clear of the whole open list the target is in, not just the target.
        QRect clear = ring;
        if (QWidget* list = target->parentWidget(); list && !list->property("guideAnchor").toString().isEmpty())
            clear = clear.united(QRect(list->mapTo(host, QPoint()), list->size()));
        // The arrow runs from the bubble when both are in the main window; a popup list gets the ring only.
        if (host == window_) placeBubble(clear, spot_.area);
        else placeBubble({}, false);
        const QRect from = host == window_ && !spot_.area ? bubble_->geometry() : QRect();
        static_cast<GuidePainter*>(painter_.data())->set(ring, spot_.area, from);
    }
    bubble_->raise();
}

void GuideOverlay::placeBubble(const QRect& target, bool area) {
    const QRect bounds = window_->rect().adjusted(12, 12, -12, -12);
    const QSize size = bubble_->size();
    QRect place;
    if (target.isNull()) {
        // Nothing to point at: top centre of the window, under the toolbar.
        place = QRect(QPoint(bounds.center().x() - size.width() / 2, bounds.top() + 60), size);
    } else if (area) {
        // Bottom left of the 3D view: the top holds the step bar and the right the view controls.
        place = QRect(QPoint(target.left() + 24, target.bottom() - 24 - size.height()), size);
    } else {
        constexpr int kGap = 60;  // room for the arrow
        const QRect candidates[] = {
            QRect(QPoint(target.center().x() - size.width() / 2, target.bottom() + kGap), size),   // below
            QRect(QPoint(target.left() - kGap - size.width(), target.center().y() - size.height() / 2), size),  // left
            QRect(QPoint(target.center().x() - size.width() / 2, target.top() - kGap - size.height()), size),   // above
            QRect(QPoint(target.right() + kGap, target.center().y() - size.height() / 2), size),  // right
        };
        place = candidates[0];
        for (QRect c : candidates) {
            // Slide along the window edge rather than give up on a side.
            c.moveLeft(std::clamp(c.left(), bounds.left(), bounds.right() - size.width()));
            c.moveTop(std::clamp(c.top(), bounds.top(), bounds.bottom() - size.height()));
            if (!c.intersects(target.adjusted(-20, -20, 20, 20))) {
                place = c;
                break;
            }
        }
    }
    place.moveLeft(std::clamp(place.left(), bounds.left(), std::max(bounds.left(), bounds.right() - size.width())));
    place.moveTop(std::clamp(place.top(), bounds.top(), std::max(bounds.top(), bounds.bottom() - size.height())));
    bubble_->move(place.topLeft());
}

bool GuideOverlay::eventFilter(QObject* watched, QEvent* event) {
    // Clicking the button or menu item a step points at completes that step.
    if (active_ && event->type() == QEvent::MouseButtonRelease && spot_.direct && !spot_.area && spot_.widget) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && (widget == spot_.widget || spot_.widget->isAncestorOf(widget))) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint at = widget->mapTo(spot_.widget, mouse->position().toPoint());
            if (spot_.rect.contains(at)) {
                const int done = step_;
                QTimer::singleShot(0, this, [this, done] {
                    if (active_ && step_ == done) goTo(step_ + 1);
                });
            }
        }
    }
    return QObject::eventFilter(watched, event);
}
