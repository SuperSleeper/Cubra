#include "app/TransformController.h"

#include "app/SnapSettings.h"
#include "core/Placement.h"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

using td::Affine;
using td::V3;

namespace {

constexpr double kArrowPixels = 80.0;
constexpr double kRingPixels = 70.0;
constexpr double kGrabPixels = 10.0;
constexpr int kRingSteps = 64;
constexpr int kHudGap = 30;
constexpr double kFactorSnap = 0.05;
constexpr double kNearlyEdgeOn = 0.08;

const V3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const QVector4D kAxisColors[3] = {{0.92f, 0.32f, 0.30f, 1}, {0.36f, 0.80f, 0.38f, 1}, {0.36f, 0.58f, 0.98f, 1}};
const QVector4D kHoverColor(1.0f, 0.85f, 0.25f, 1);
const QVector4D kScaleColor(1.0f, 0.62f, 0.15f, 1);
const char* kAxisNames[3] = {"X", "Y", "Z"};

QString formatValue(double value) {
    QString text = QString::number(value, 'f', 3);
    while (text.contains('.') && text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text == "-0" ? QStringLiteral("0") : text;
}

double segmentDistance(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    const double t = lengthSquared > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / lengthSquared, 0.0, 1.0) : 0.0;
    const QPointF offset = p - (a + ab * t);
    return std::hypot(offset.x(), offset.y());
}

V3 ringU(int axis) { return kAxes[(axis + 1) % 3]; }
V3 ringV(int axis) { return kAxes[(axis + 2) % 3]; }

}  // namespace

TransformController::TransformController(Viewport* viewport, QObject* parent) : QObject(parent), viewport_(viewport) {
    hud_ = new QFrame(viewport_);
    hud_->setObjectName("transformHud");
    hud_->setFrameShape(QFrame::StyledPanel);
    hud_->setAutoFillBackground(true);
    hud_->hide();
    auto* layout = new QVBoxLayout(hud_);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    title_ = new QLabel(hud_);
    QFont bold = title_->font();
    bold.setBold(true);
    title_->setFont(bold);
    layout->addWidget(title_);
    grid_ = new QGridLayout;
    grid_->setVerticalSpacing(3);
    layout->addLayout(grid_);

    snap_ = new QCheckBox(tr("Snap"), hud_);
    snap_->setObjectName("transformSnap");
    snap_->setChecked(true);
    snap_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(snap_);
    auto* hint = new QLabel(tr("Tab next  ·  Enter apply  ·  Esc cancel"), hud_);
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    hint->setFont(small);
    hint->setEnabled(false);
    layout->addWidget(hint);

    connect(viewport_, &Viewport::cameraChanged, this, [this] {
        if (active_) refresh();
    });
}

bool TransformController::snapping() const { return snap_->isChecked(); }

void TransformController::begin(TransformMode mode, const std::array<double, 3>& min, const std::array<double, 3>& max,
                                std::function<void(const Affine&)> preview) {
    active_ = true;
    mode_ = mode;
    min_ = min;
    max_ = max;
    preview_ = std::move(preview);
    std::fill(std::begin(move_), std::end(move_), 0.0);
    std::fill(std::begin(turn_), std::end(turn_), 0.0);
    std::fill(std::begin(factors_), std::end(factors_), 1.0);
    factor_ = 1;
    for (int a = 0; a < 3; ++a) sizes_[a] = std::max(max_[a] - min_[a], 1e-6);
    hovered_ = dragging_ = -1;
    snap_->setChecked(snapSettings().grid);

    switch (mode_) {
        case TransformMode::MoveRotate: title_->setText(tr("Move / Rotate")); break;
        case TransformMode::Scale: title_->setText(tr("Scale")); break;
        case TransformMode::NonUniform: title_->setText(tr("Non-Uniform Scale")); break;
        case TransformMode::SmartScale: title_->setText(tr("Smart Scale")); break;
    }
    buildFields();
    viewport_->setTool(this);
    viewport_->setMouseTracking(true);
    hud_->show();
    hud_->raise();
    refresh();
    fields_.front().edit->setFocus(Qt::TabFocusReason);
    fields_.front().edit->selectAll();
}

void TransformController::addField(const QString& name, const QString& unit, double* value, double minimum) {
    const int row = int(fields_.size());
    auto* label = new QLabel(name, hud_);
    auto* edit = new QLineEdit(hud_);
    edit->setObjectName("transform_" + name);
    edit->setFixedWidth(76);
    edit->setAlignment(Qt::AlignRight);
    edit->setValidator(new QRegularExpressionValidator(QRegularExpression("-?[0-9]*\\.?[0-9]{0,3}"), edit));
    edit->installEventFilter(this);
    auto* units = new QLabel(unit, hud_);
    grid_->addWidget(label, row, 0);
    grid_->addWidget(edit, row, 1);
    grid_->addWidget(units, row, 2);
    fieldWidgets_.insert(fieldWidgets_.end(), {label, edit, units});
    fields_.push_back({name, value, minimum, edit});
    connect(edit, &QLineEdit::textEdited, this, [this, row] { onEdited(row); });
    connect(edit, &QLineEdit::returnPressed, this, &TransformController::commit);
}

void TransformController::buildFields() {
    for (QWidget* widget : fieldWidgets_) delete widget;
    fieldWidgets_.clear();
    fields_.clear();
    const double none = -1e9;
    switch (mode_) {
        case TransformMode::MoveRotate:
            for (int a = 0; a < 3; ++a) addField(QStringLiteral("Move %1").arg(kAxisNames[a]), tr("mm"), &move_[a], none);
            for (int a = 0; a < 3; ++a) addField(QStringLiteral("Rotate %1").arg(kAxisNames[a]), tr("deg"), &turn_[a], none);
            break;
        case TransformMode::Scale: addField(QStringLiteral("Scale"), QStringLiteral("×"), &factor_, 0.001); break;
        case TransformMode::NonUniform:
            for (int a = 0; a < 3; ++a) addField(QStringLiteral("Scale %1").arg(kAxisNames[a]), QStringLiteral("×"), &factors_[a], 0.001);
            break;
        case TransformMode::SmartScale:
            addField(QStringLiteral("Length"), tr("mm"), &sizes_[0], 0.01);
            addField(QStringLiteral("Width"), tr("mm"), &sizes_[1], 0.01);
            addField(QStringLiteral("Height"), tr("mm"), &sizes_[2], 0.01);
            break;
    }
    syncFields();
    hud_->adjustSize();
}

TransformController::Field* TransformController::fieldNamed(const QString& name) {
    for (Field& field : fields_)
        if (field.name == name) return &field;
    return nullptr;
}

void TransformController::syncFields() {
    for (Field& field : fields_) {
        field.edit->setText(formatValue(*field.value));
        field.edit->setStyleSheet({});
    }
}

void TransformController::onEdited(int index) {
    Field& field = fields_[index];
    bool parsed = false;
    const double value = field.edit->text().toDouble(&parsed);
    const bool valid = parsed && value >= field.minimum && std::isfinite(value);
    field.edit->setStyleSheet(valid ? QString() : QStringLiteral("border: 1px solid #e06c5c;"));
    if (!valid) return;
    *field.value = value;
    refresh();
}

bool TransformController::eventFilter(QObject* watched, QEvent* event) {
    if (active_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            int current = 0;
            for (int i = 0; i < int(fields_.size()); ++i)
                if (fields_[i].edit == watched) current = i;
            const int count = int(fields_.size());
            QLineEdit* next = fields_[(current + (key == Qt::Key_Tab ? 1 : -1) + count) % count].edit;
            next->setFocus(Qt::TabFocusReason);
            next->selectAll();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

Affine TransformController::current() const {
    const V3 centre{(min_[0] + max_[0]) / 2, (min_[1] + max_[1]) / 2, (min_[2] + max_[2]) / 2};
    // Resizing keeps the base where it is, so a part on the bed stays on the bed.
    const V3 base{centre.x, centre.y, min_[2]};
    switch (mode_) {
        case TransformMode::MoveRotate:
            return Affine::translation({move_[0], move_[1], move_[2]}) * Affine::rotation(kAxes[2], turn_[2], centre) *
                   Affine::rotation(kAxes[1], turn_[1], centre) * Affine::rotation(kAxes[0], turn_[0], centre);
        case TransformMode::Scale: return Affine::scaling({factor_, factor_, factor_}, base);
        case TransformMode::NonUniform: return Affine::scaling({factors_[0], factors_[1], factors_[2]}, base);
        case TransformMode::SmartScale:
            return Affine::scaling({sizes_[0] / std::max(max_[0] - min_[0], 1e-6), sizes_[1] / std::max(max_[1] - min_[1], 1e-6),
                                    sizes_[2] / std::max(max_[2] - min_[2], 1e-6)},
                                   base);
    }
    return {};
}

bool TransformController::changed() const { return !current().isIdentity(); }

void TransformController::refresh() {
    const Affine change = current();
    preview_(change);

    const V3 centre{(min_[0] + max_[0]) / 2, (min_[1] + max_[1]) / 2, (min_[2] + max_[2]) / 2};
    const V3 base{centre.x, centre.y, min_[2]};
    handles_.clear();
    std::vector<Viewport::TintedMesh> meshes;
    const auto addArrow = [&](const QString& name, int axis, V3 origin, V3 dir, const QVector4D& color) {
        const double length = viewport_->worldPerPixel(origin) * kArrowPixels;
        Handle handle{name, false, axis, origin, dir, viewport_->project(origin), viewport_->project(origin + dir * length), {}};
        const bool lit = int(handles_.size()) == hovered_ || int(handles_.size()) == dragging_;
        meshes.push_back({td::makeArrow(origin, dir, length), lit ? kHoverColor : color});
        handles_.push_back(std::move(handle));
    };

    if (mode_ == TransformMode::MoveRotate) {
        const V3 moved = change.point(centre);
        for (int a = 0; a < 3; ++a) addArrow(QStringLiteral("Move %1").arg(kAxisNames[a]), a, moved, kAxes[a], kAxisColors[a]);
        const double halfDiagonal = 0.5 * std::hypot(max_[0] - min_[0], max_[1] - min_[1], max_[2] - min_[2]);
        const double perPixel = viewport_->worldPerPixel(moved);
        const double radius = std::max(halfDiagonal * 1.1, perPixel * kRingPixels);
        for (int a = 0; a < 3; ++a) {
            Handle ring{QStringLiteral("Rotate %1").arg(kAxisNames[a]), true, a, moved, kAxes[a], {}, {}, {}};
            td::Mesh tube;
            V3 previous;
            for (int i = 0; i <= kRingSteps; ++i) {
                const double angle = 2 * std::numbers::pi * i / kRingSteps;
                const V3 p = moved + ringU(a) * (radius * std::cos(angle)) + ringV(a) * (radius * std::sin(angle));
                ring.loop.push_back(viewport_->project(p));
                if (i > 0) {
                    const td::Mesh piece = td::makeTube(previous, p, perPixel * 1.6);
                    const auto offset = uint32_t(tube.vertexCount());
                    tube.positions.insert(tube.positions.end(), piece.positions.begin(), piece.positions.end());
                    for (uint32_t index : piece.indices) tube.indices.push_back(index + offset);
                }
                previous = p;
            }
            const bool lit = int(handles_.size()) == hovered_ || int(handles_.size()) == dragging_;
            meshes.push_back({tube, lit ? kHoverColor : kAxisColors[a]});
            handles_.push_back(std::move(ring));
        }
    } else if (mode_ == TransformMode::Scale) {
        const V3 corner = change.point({max_[0], max_[1], max_[2]});
        addArrow(QStringLiteral("Scale"), -1, corner, normalized(corner - base), kScaleColor);
    } else {
        const QString names[3] = {mode_ == TransformMode::SmartScale ? QStringLiteral("Length") : QStringLiteral("Scale X"),
                                  mode_ == TransformMode::SmartScale ? QStringLiteral("Width") : QStringLiteral("Scale Y"),
                                  mode_ == TransformMode::SmartScale ? QStringLiteral("Height") : QStringLiteral("Scale Z")};
        const V3 grips[3] = {{max_[0], centre.y, centre.z}, {centre.x, max_[1], centre.z}, {centre.x, centre.y, max_[2]}};
        for (int a = 0; a < 3; ++a) addArrow(names[a], a, change.point(grips[a]), kAxes[a], kAxisColors[a]);
    }
    viewport_->setHandles(meshes);

    // Keep the boxes clear of the handles.
    QRectF bounds;
    for (const Handle& h : handles_) {
        bounds = bounds.united(QRectF(h.start, h.end).normalized());
        for (const QPointF& p : h.loop) bounds = bounds.united(QRectF(p, QSizeF(1, 1)));
    }
    const QSize size = hud_->sizeHint();
    int x = int(bounds.right()) + kHudGap;
    if (x + size.width() > viewport_->width()) x = int(bounds.left()) - kHudGap - size.width();
    hud_->setGeometry(std::clamp(x, 0, std::max(0, viewport_->width() - size.width())),
                      std::clamp(int(bounds.center().y()) - size.height() / 2, 0, std::max(0, viewport_->height() - size.height())), size.width(),
                      size.height());
}

QPointF TransformController::handleGrabPoint(int index) const {
    const Handle& h = handles_[index];
    if (!h.ring) return (h.start + h.end) * 0.5;
    // The point of the ring farthest from the middle of the screen is the least likely to be hidden.
    const QPointF middle(viewport_->width() / 2.0, viewport_->height() / 2.0);
    return *std::max_element(h.loop.begin(), h.loop.end(), [&](QPointF a, QPointF b) {
        return QLineF(a, middle).length() < QLineF(b, middle).length();
    });
}

int TransformController::handleAt(QPointF position) const {
    int best = -1;
    double bestDistance = kGrabPixels;
    for (int i = 0; i < int(handles_.size()); ++i) {
        const Handle& h = handles_[i];
        double distance = 1e9;
        if (!h.ring) distance = segmentDistance(position, h.start, h.end) * 0.8;  // arrows win ties with rings
        else
            for (size_t k = 0; k + 1 < h.loop.size(); ++k) distance = std::min(distance, segmentDistance(position, h.loop[k], h.loop[k + 1]));
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

double TransformController::ringAngle(const Handle& h, QPointF position, bool* ok) const {
    V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    const double facing = td::dot(direction, h.dir);
    *ok = std::abs(facing) > kNearlyEdgeOn;
    if (!*ok) return 0;
    const double t = td::dot(h.origin - origin, h.dir) / facing;
    const V3 onPlane = origin + direction * t - h.origin;
    return std::atan2(td::dot(onPlane, ringV(h.axis)), td::dot(onPlane, ringU(h.axis))) * 180.0 / std::numbers::pi;
}

void TransformController::beginDrag(int index, QPointF position) {
    dragging_ = index;
    dragPress_ = position;
    dragHandle_ = handles_[index];
    const Handle& h = dragHandle_;
    Field* field = fieldNamed(h.name);
    dragStartValue_ = field ? *field->value : 0;
    if (field) {
        field->edit->setFocus(Qt::MouseFocusReason);
        field->edit->selectAll();
    }

    if (h.ring) {
        bool ok = false;
        dragLastAngle_ = ringAngle(h, position, &ok);
        dragOnScreen_ = !ok;
        dragTurned_ = 0;
        return;
    }
    V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    const auto along = td::closestOnAxis(h.origin, h.dir, origin, direction);
    dragOnScreen_ = !along;
    dragStartAlong_ = along.value_or(0);
}

void TransformController::updateDrag(QPointF position) {
    const Handle& h = dragHandle_;
    Field* field = fieldNamed(h.name);
    if (!field) return;

    double value = dragStartValue_;
    if (h.ring) {
        if (dragOnScreen_) {
            dragTurned_ = (position.x() - dragPress_.x()) * 0.5;
        } else {
            bool ok = false;
            const double angle = ringAngle(h, position, &ok);
            if (ok) {
                double step = angle - dragLastAngle_;
                if (step > 180) step -= 360;
                if (step < -180) step += 360;
                dragTurned_ += step;
                dragLastAngle_ = angle;
            }
        }
        value = dragStartValue_ + dragTurned_;
        value = snapAngle(value, snapping());
    } else {
        V3 origin, direction;
        viewport_->cursorRay(position, &origin, &direction);
        const auto along = td::closestOnAxis(h.origin, h.dir, origin, direction);
        const double moved = dragOnScreen_ || !along ? (dragPress_.y() - position.y()) * viewport_->worldPerPixel(h.origin) : *along - dragStartAlong_;

        if (mode_ == TransformMode::MoveRotate) {
            value = dragStartValue_ + moved;
            value = snapLength(value, snapping());
        } else {
            // How far the grip sat from the fixed base when the drag began.
            const V3 centre{(min_[0] + max_[0]) / 2, (min_[1] + max_[1]) / 2, (min_[2] + max_[2]) / 2};
            const V3 base{centre.x, centre.y, min_[2]};
            const int axis = h.axis;
            const double reach = axis < 0 ? td::length(V3{max_[0], max_[1], max_[2]} - base)
                                          : (axis == 2 ? max_[2] - min_[2] : (max_[axis] - min_[axis]) / 2);
            if (mode_ == TransformMode::SmartScale) {
                const double size = max_[axis] - min_[axis];
                value = dragStartValue_ + moved * size / reach;
                value = std::max(snapLength(value, snapping()), snapping() ? snapSettings().linear : 0.01);
            } else {
                value = dragStartValue_ + moved / reach;
                value = std::max(snapping() ? std::round(value / kFactorSnap) * kFactorSnap : std::round(value * 1000) / 1000, 0.01);
            }
        }
    }
    if (value == *field->value) return;
    *field->value = value;
    field->edit->setText(formatValue(value));
    field->edit->selectAll();
    refresh();
}

bool TransformController::toolMousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;
    const int handle = handleAt(event->position());
    if (handle < 0) return false;  // elsewhere: rotate the view, or click away to apply
    beginDrag(handle, event->position());
    return true;
}

bool TransformController::toolMouseMove(QMouseEvent* event) {
    if (dragging_ >= 0) {
        updateDrag(event->position());
        return true;
    }
    if (event->buttons() != Qt::NoButton) return false;
    const int over = handleAt(event->position());
    if (over != hovered_) {
        hovered_ = over;
        if (over >= 0) viewport_->setCursor(Qt::SizeAllCursor);
        else viewport_->unsetCursor();
        refresh();
    }
    return false;
}

bool TransformController::toolMouseRelease(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || dragging_ < 0) return false;
    dragging_ = -1;
    refresh();
    return true;
}

void TransformController::cancel() {
    if (!active_) return;
    active_ = false;
    dragging_ = -1;
    hud_->hide();
    viewport_->setTool(nullptr);
    viewport_->setMouseTracking(false);
    viewport_->setHandles({});
    viewport_->unsetCursor();
    emit cancelled();
}

bool TransformController::commit() {
    if (!active_) return false;
    for (const Field& field : fields_)
        if (!field.edit->styleSheet().isEmpty()) return false;  // a box still holds something invalid
    if (!changed()) {
        cancel();
        return false;
    }
    const Affine change = current();
    active_ = false;
    dragging_ = -1;
    hud_->hide();
    viewport_->setTool(nullptr);
    viewport_->setMouseTracking(false);
    viewport_->setHandles({});
    viewport_->unsetCursor();
    emit committed(change);
    return true;
}
