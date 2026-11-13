#pragma once

#include "ai/Manual.h"

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

#include <functional>

class QFrame;
class QLabel;
class QPushButton;
class QTimer;

// Where a walkthrough step points.
struct GuideSpot {
    bool known = false;        // the name exists in the interface (shown or not)
    bool direct = false;       // pointing at the step's own target, not a way to reach it
    QPointer<QWidget> widget;  // what to point at; null when nothing on screen leads there yet
    QRect rect;                // in widget's own coordinates
    bool area = false;         // a large area such as the 3D view: outlined, without an arrow
    QString hint;              // what to do first when pointing elsewhere ("Point at Modify to open its list.")
};

// Walks the user through a manual topic on the real interface: a pulsing ring and an
// arrow on the button, box or panel each step names, and a bubble with the step's text.
// A step moves on when its target is clicked, when the next step's target appears (a
// list opening, a tool's panel showing), or with Next.
class GuideOverlay : public QObject {
    Q_OBJECT
public:
    // locate turns a target name into a spot; the window hosts the bubble.
    GuideOverlay(QWidget* window, std::function<GuideSpot(const QString&)> locate);

    void start(const td::ManualTopic& topic);
    void stop();
    bool active() const { return active_; }
    int step() const { return step_; }
    const td::ManualTopic& topic() const { return topic_; }
    GuideSpot spot() const { return spot_; }
    QFrame* bubble() const { return bubble_; }

signals:
    void finished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void goTo(int step);
    void tick();
    void placeBubble(const QRect& target, bool area);
    bool nextTargetShown() const;

    QWidget* window_;
    std::function<GuideSpot(const QString&)> locate_;
    QPointer<QWidget> painter_;
    QFrame* bubble_;
    QLabel* heading_;
    QLabel* text_;
    QLabel* hint_;
    QPushButton* back_;
    QPushButton* next_;
    QTimer* timer_;

    td::ManualTopic topic_;
    bool active_ = false;
    int step_ = 0;
    GuideSpot spot_;
    bool nextWasShown_ = false;
};
