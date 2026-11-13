#pragma once

#include "core/Vec.h"

#include <QPoint>
#include <QPolygonF>
#include <QWidget>

#include <vector>

class QToolButton;
class Viewport;

// The cube in the corner that shows which way the camera faces. Clicking a
// face, edge or corner turns the view to look from there; dragging orbits.
class ViewCube : public QWidget {
    Q_OBJECT
public:
    explicit ViewCube(Viewport* viewport);

    // Where on the cube to click to look from this direction, or (-1, -1) if it is out of sight.
    QPoint pointFor(td::V3 direction) const;
    // Yaw and pitch, in degrees, that look from this direction.
    static void orientationFor(td::V3 direction, float currentYaw, float* yaw, float* pitch);

signals:
    void homeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Region {
        QPolygonF shape;
        td::V3 direction;  // where the camera looks from when it is clicked
        int face;
    };
    std::vector<Region> regions() const;  // the visible ones, nearest last
    int regionAt(QPointF position) const;
    QPointF project(td::V3 point) const;

    Viewport* viewport_;
    QToolButton* home_;
    int hovered_ = -1;
    bool pressed_ = false;
    bool dragged_ = false;
    QPoint pressPosition_, lastPosition_;
};
