#pragma once

#include "app/Viewport.h"
#include "core/Placement.h"

#include <QObject>
#include <QPointF>
#include <QRect>

#include <optional>
#include <vector>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QTimer;
class QVBoxLayout;

// Runs the "new primitive" interaction: a ghost that follows the pointer and
// seats on the grid or a face, dimension fields beside it, and pull arrows.
//
// Arrows on something glued to the pointer could never be reached, so the ghost
// settles when the pointer pauses, or when the fields are used, and re-attaches
// once the pointer leaves the area around it.
class PlacementController : public QObject, public ViewportTool {
    Q_OBJECT
public:
    PlacementController(Viewport* viewport, const td::Mesh* body, QObject* parent = nullptr);

    void begin(const td::Feature& preset, const QString& label);
    void cancel();
    void pointerAt(QPointF position);  // for a part dragged in from the drawer
    void placeNow();
    bool active() const { return active_; }
    bool settled() const { return settled_; }
    const td::Feature& feature() const { return feature_; }

    int handleCount() const { return int(handles_.size()); }
    td::Param handleParam(int index) const { return handles_[index].param; }
    QPointF handleGrabPoint(int index) const;

    bool toolMousePress(QMouseEvent* event) override;
    bool toolMouseMove(QMouseEvent* event) override;
    bool toolMouseRelease(QMouseEvent* event) override;

signals:
    void placed(const td::Feature& feature, const QString& label);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Field {
        td::Param param;
        QLineEdit* edit;
    };
    struct ScreenHandle {
        td::Param param;
        double scale;
        td::V3 axisOrigin;
        td::V3 axisDir;
        QPointF start;
        QPointF end;
        int axis;
    };

    void buildFields();
    void onFieldEdited(int index);
    void focusField(int index);
    void cycleField(int delta);
    void syncFields();
    QLineEdit* fieldFor(td::Param param) const;

    void track(QPointF position);
    void settle();
    bool insideSettledArea(QPoint position) const;
    int handleAt(QPointF position) const;
    void beginDrag(int handle, QPointF position, Qt::KeyboardModifiers modifiers);
    void updateDrag(QPointF position, Qt::KeyboardModifiers modifiers);
    void commit();
    void finish();

    bool snapping() const;
    void rebuildGhost();
    void refreshPose();
    void refreshHandles();
    void positionHud();

    Viewport* viewport_;
    const td::Mesh* body_;

    QFrame* hud_;
    QLabel* title_;
    QVBoxLayout* fieldsLayout_;
    QCheckBox* snap_;
    QTimer* dwell_;
    std::vector<Field> fields_;

    bool active_ = false;
    bool settled_ = false;
    bool syncing_ = false;
    td::Feature feature_;
    QString label_;
    std::vector<ScreenHandle> handles_;
    QRect handleBounds_;
    int hoveredHandle_ = -1;
    QPointF lastPointer_;

    bool pressPending_ = false;
    int dragHandle_ = -1;
    td::Feature dragStart_;
    double dragOffset_ = 0;
    ScreenHandle dragInfo_{};
};
