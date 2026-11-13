#pragma once

#include "app/Viewport.h"
#include "core/Affine.h"

#include <QObject>
#include <QPointF>

#include <array>
#include <functional>
#include <vector>

class QCheckBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;

enum class TransformMode { MoveRotate, Scale, NonUniform, SmartScale };

// Moves, turns and resizes whole bodies. Arrows slide along an axis, rings
// turn around one, and grips resize; the values show in boxes that can also
// be typed into. The bodies move on screen only, until the change is committed.
class TransformController : public QObject, public ViewportTool {
    Q_OBJECT
public:
    explicit TransformController(Viewport* viewport, QObject* parent = nullptr);

    // bounds: the world-space box around everything being transformed.
    void begin(TransformMode mode, const std::array<double, 3>& min, const std::array<double, 3>& max, std::function<void(const td::Affine&)> preview);
    void cancel();
    bool commit();  // applies a real change, or cancels when nothing changed

    bool active() const { return active_; }
    TransformMode mode() const { return mode_; }
    td::Affine current() const;

    int handleCount() const { return int(handles_.size()); }
    QString handleName(int index) const { return handles_[index].name; }
    QPointF handleGrabPoint(int index) const;

    bool toolMousePress(QMouseEvent* event) override;
    bool toolMouseMove(QMouseEvent* event) override;
    bool toolMouseRelease(QMouseEvent* event) override;

signals:
    void committed(const td::Affine& change);
    void cancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Field {
        QString name;
        double* value;
        double minimum;
        QLineEdit* edit;
    };
    struct Handle {
        QString name;   // matches the field it drives
        bool ring;
        int axis;
        td::V3 origin;  // arrow start, or ring centre
        td::V3 dir;     // arrow direction, or ring axis
        QPointF start, end;          // arrow on screen
        std::vector<QPointF> loop;   // ring on screen
    };

    void buildFields();
    void addField(const QString& name, const QString& unit, double* value, double minimum);
    void syncFields();
    void onEdited(int index);
    void refresh();
    int handleAt(QPointF position) const;
    void beginDrag(int handle, QPointF position);
    void updateDrag(QPointF position);
    double ringAngle(const Handle& handle, QPointF position, bool* ok) const;
    Field* fieldNamed(const QString& name);
    bool snapping() const;
    bool changed() const;

    Viewport* viewport_;
    QFrame* hud_;
    QLabel* title_;
    QGridLayout* grid_;
    QCheckBox* snap_;
    std::vector<Field> fields_;
    std::vector<QWidget*> fieldWidgets_;

    bool active_ = false;
    TransformMode mode_ = TransformMode::MoveRotate;
    std::array<double, 3> min_{}, max_{};
    std::function<void(const td::Affine&)> preview_;

    // Move and turn, in mm and degrees; scale factors; target sizes in mm.
    double move_[3] = {0, 0, 0};
    double turn_[3] = {0, 0, 0};
    double factor_ = 1;
    double factors_[3] = {1, 1, 1};
    double sizes_[3] = {0, 0, 0};

    std::vector<Handle> handles_;
    int hovered_ = -1;
    int dragging_ = -1;
    double dragStartValue_ = 0;
    double dragStartAlong_ = 0;
    double dragLastAngle_ = 0;
    double dragTurned_ = 0;
    bool dragOnScreen_ = false;
    QPointF dragPress_;
    // The handle as it was when the drag began. The live one moves with the part,
    // and measuring from it would make the part chase its own handle.
    Handle dragHandle_{};
};
