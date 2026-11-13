#pragma once

#include "core/Vec.h"

#include <QObject>
#include <QString>

#include <vector>

class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

// A tool that asks for its inputs one at a time, in a bar at the top of the
// canvas: "Target solid", then "Source solids", and so on. The window turns
// each click into a choice and hands it over.
class StepTool : public QObject {
    Q_OBJECT
public:
    enum class Pick { Body, Bodies, Face, Edge, Axis };
    struct Step {
        QString label;
        Pick pick;
    };
    struct Choice {
        int body = -1;
        int face = -1;
        td::V3 point, normal;  // faces: centre and outward normal; edges and axes: a point and the direction
        QString name;
        std::vector<td::V3> path;  // edges: the points along it, in order
        bool closed = false;
        int edge = -1;  // edges: which edge of the body's topology
    };

    explicit StepTool(QWidget* host);

    // With confirm, the last step waits for Enter; without, it finishes on its own.
    void begin(const QString& id, const QString& title, std::vector<Step> steps, bool confirm);
    void cancel();
    bool active() const { return active_; }
    const QString& id() const { return id_; }
    Pick currentPick() const { return steps_[current_].pick; }
    const std::vector<std::vector<Choice>>& picks() const { return picks_; }

    void preset(int step, std::vector<Choice> choices);
    void offer(const Choice& choice);
    bool confirm();  // Enter: true if the tool finished
    void setMessage(const QString& message);  // feedback on the last click
    void setNote(const QString& note);        // guidance that stays for the whole tool

signals:
    void finished(const QString& id, const std::vector<std::vector<StepTool::Choice>>& picks);
    void cancelled();
    void changed();

private:
    bool ready() const;
    void focusStep(int step);
    void refresh();

    QWidget* host_;
    QFrame* bar_;
    QLabel* title_;
    QLabel* message_;
    QLabel* note_;
    QVBoxLayout* slots_;
    QPushButton* apply_;
    std::vector<QPushButton*> slotButtons_;

    bool active_ = false;
    bool confirm_ = false;
    QString id_;
    std::vector<Step> steps_;
    std::vector<std::vector<Choice>> picks_;
    int current_ = 0;
};
