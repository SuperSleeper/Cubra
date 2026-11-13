#pragma once

#include "app/Viewport.h"
#include "core/Modify.h"

#include <QObject>
#include <QPoint>
#include <QPointF>

#include <functional>
#include <vector>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;

// Live Chamfer, Fillet and Extrude. An arrow shows which way the operation goes:
// one per selected face, or a single one for all selected edges. Everything
// selected shares one distance, previewed on the real model until committed.
class ModifyController : public QObject, public ViewportTool {
    Q_OBJECT
public:
    using Features = std::vector<td::Feature>;

    explicit ModifyController(Viewport* viewport, QObject* parent = nullptr);

    // preview applies candidate edits to the display and says whether they build.
    void begin(td::ModifyKind kind, const td::Mesh& mesh, const td::Topology& topology, const std::vector<int>& entities,
               std::function<bool(const Features&)> preview);
    void retarget(const std::vector<int>& entities);  // keeps the current distance
    void cancel();
    bool commitIfChanged();

    bool active() const { return active_; }
    td::ModifyKind kind() const { return kind_; }
    double value() const { return value_; }
    int handleCount() const { return int(arrows_.size()); }
    QPointF handleGrabPoint(int index = 0) const { return (arrows_[index].start + arrows_[index].end) * 0.5; }
    QPointF handleStart(int index = 0) const { return arrows_[index].start; }
    QPointF handleEnd(int index = 0) const { return arrows_[index].end; }

    bool toolMousePress(QMouseEvent* event) override;
    bool toolMouseMove(QMouseEvent* event) override;
    bool toolMouseRelease(QMouseEvent* event) override;

signals:
    void committed(td::ModifyKind kind, const std::vector<td::Feature>& features);
    void cancelled();
    void previewMoved(bool displaced);  // false while the model still looks unchanged
    void clickedAway(QPoint position, bool shift);

private:
    struct Arrow {
        QPointF start, end;
    };

    bool isSigned() const;
    bool applyValue(double value);
    void onEdited();
    void commit();
    void finish();
    void rebuildGuides();
    void refreshHandles();
    void syncField();
    int arrowAt(QPointF position) const;
    int faceAt(QPointF position) const;
    void updateDrag(QPointF position);
    bool snapping() const;

    Viewport* viewport_;
    QFrame* hud_;
    QLabel* title_;
    QLabel* fieldName_;
    QLineEdit* field_;
    QCheckBox* snap_;

    bool active_ = false;
    td::ModifyKind kind_ = td::ModifyKind::Chamfer;
    td::Mesh mesh_;
    td::Topology topology_;
    std::vector<int> entities_;
    std::function<bool(const Features&)> preview_;
    std::vector<td::ModifyGuide> guides_;
    std::vector<td::Mesh> faceMeshes_;
    std::vector<Arrow> arrows_;
    double value_ = 1;
    Features current_;

    int hovered_ = -1;
    bool pending_ = false;
    bool dragging_ = false;
    bool pressShift_ = false;
    QPointF pressPosition_;
    td::ModifyGuide dragGuide_;
    double dragOffset_ = 0;
    double dragStartValue_ = 0;
    bool dragOnScreen_ = false;
};
