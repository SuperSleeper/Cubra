#include "app/PlacementController.h"

#include "app/SnapSettings.h"
#include "core/Picking.h"

#include <QCheckBox>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using td::Feature;
using td::Param;
using td::V3;

namespace {

constexpr int kDwellMs = 350;
constexpr double kDwellJitterPixels = 2.0;
constexpr double kArrowPixels = 56.0;
constexpr double kGrabPixels = 10.0;
constexpr int kHudGap = 18;
constexpr int kSettledMargin = 28;
constexpr double kFarLimit = 5000.0;

const QVector4D kAxisColors[3] = {{0.92f, 0.32f, 0.30f, 1}, {0.36f, 0.80f, 0.38f, 1}, {0.36f, 0.58f, 0.98f, 1}};
const QVector4D kHoverColor(1.0f, 0.85f, 0.25f, 1);
const QVector4D kGhostMoving(0.55f, 0.82f, 1.0f, 0.35f);
const QVector4D kGhostSettled(0.55f, 0.82f, 1.0f, 0.55f);

QString formatValue(double value) {
    QString text = QString::number(value, 'f', 2);
    while (text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text;
}

double segmentDistance(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    const double t = lengthSquared > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / lengthSquared, 0.0, 1.0) : 0.0;
    const QPointF offset = p - (a + ab * t);
    return std::hypot(offset.x(), offset.y());
}

}  // namespace

PlacementController::PlacementController(Viewport* viewport, const td::Mesh* body, QObject* parent)
    : QObject(parent), viewport_(viewport), body_(body) {
    hud_ = new QFrame(viewport_);
    hud_->setObjectName("placementHud");
    hud_->setFrameShape(QFrame::StyledPanel);
    hud_->setAutoFillBackground(true);
    hud_->installEventFilter(this);
    hud_->hide();

    auto* layout = new QVBoxLayout(hud_);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    title_ = new QLabel(hud_);
    QFont bold = title_->font();
    bold.setBold(true);
    title_->setFont(bold);
    layout->addWidget(title_);

    fieldsLayout_ = new QVBoxLayout;
    fieldsLayout_->setSpacing(3);
    layout->addLayout(fieldsLayout_);

    snap_ = new QCheckBox(tr("Snap"), hud_);
    snap_->setObjectName("hudSnap");
    snap_->setChecked(true);
    snap_->setToolTip(tr("Steps by the snap size set at the bottom right"));
    snap_->setFocusPolicy(Qt::NoFocus);  // typing must keep going to the dimension box
    layout->addWidget(snap_);

    auto* hint = new QLabel(tr("Tab next  ·  Enter place  ·  Esc cancel\nShift+drag arrow scales all"), hud_);
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    hint->setFont(small);
    hint->setEnabled(false);
    layout->addWidget(hint);

    dwell_ = new QTimer(this);
    dwell_->setSingleShot(true);
    dwell_->setInterval(kDwellMs);
    connect(dwell_, &QTimer::timeout, this, &PlacementController::settle);

    connect(viewport_, &Viewport::cameraChanged, this, [this] {
        if (active_) refreshPose();
    });
}

void PlacementController::begin(const Feature& preset, const QString& label) {
    if (active_) finish();
    active_ = true;
    settled_ = false;
    feature_ = preset;
    label_ = label;
    hoveredHandle_ = -1;
    dragHandle_ = -1;
    pressPending_ = false;

    title_->setText(label);
    snap_->setChecked(snapSettings().grid);
    buildFields();
    viewport_->setTool(this);
    viewport_->setMouseTracking(true);
    rebuildGhost();
    hud_->show();
    hud_->raise();

    const QPoint pointer = viewport_->mapFromGlobal(QCursor::pos());
    if (viewport_->rect().contains(pointer)) track(pointer);
    else refreshPose();
    if (!fields_.empty()) focusField(0);
}

void PlacementController::pointerAt(QPointF position) {
    if (!active_) return;
    settled_ = false;
    track(position);
}

void PlacementController::placeNow() {
    if (active_) commit();
}

void PlacementController::cancel() {
    if (active_) finish();
}

void PlacementController::finish() {
    active_ = false;
    settled_ = false;
    dwell_->stop();
    hud_->hide();
    viewport_->setMouseTracking(false);
    viewport_->setTool(nullptr);
    viewport_->clearPlacementGraphics();
    viewport_->unsetCursor();
    handles_.clear();
}

void PlacementController::commit() {
    for (const Field& field : fields_)
        if (!field.edit->property("valid").toBool()) {
            focusField(int(&field - fields_.data()));
            return;
        }
    const Feature result = feature_;
    const QString label = label_;
    finish();
    emit placed(result, label);
}

void PlacementController::buildFields() {
    for (const Field& field : fields_) {
        QWidget* row = field.edit->parentWidget();
        fieldsLayout_->removeWidget(row);
        delete row;
    }
    fields_.clear();

    const auto params = td::hudParams(feature_);
    for (int i = 0; i < int(params.size()); ++i) {
        auto* row = new QWidget(hud_);
        auto* form = new QHBoxLayout(row);
        form->setContentsMargins(0, 0, 0, 0);
        auto* name = new QLabel(td::paramLabel(params[i]), row);
        name->setMinimumWidth(92);
        auto* edit = new QLineEdit(row);
        edit->setObjectName("hud_" + td::paramLabel(params[i]));
        edit->setFixedWidth(72);
        edit->setAlignment(Qt::AlignRight);
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]*\\.?[0-9]{0,3}"), edit));
        edit->setText(formatValue(td::paramValue(feature_, params[i])));
        edit->setProperty("valid", true);
        edit->installEventFilter(this);
        form->addWidget(name);
        form->addWidget(edit);
        form->addWidget(new QLabel(tr("mm"), row));
        fieldsLayout_->addWidget(row);
        fields_.push_back({params[i], edit});

        connect(edit, &QLineEdit::textEdited, this, [this, i] { onFieldEdited(i); });
        connect(edit, &QLineEdit::returnPressed, this, &PlacementController::commit);
    }
    hud_->adjustSize();
}

QLineEdit* PlacementController::fieldFor(Param param) const {
    for (const Field& field : fields_)
        if (field.param == param) return field.edit;
    return nullptr;
}

void PlacementController::onFieldEdited(int index) {
    const Field& field = fields_[index];
    bool parsed = false;
    const double value = field.edit->text().toDouble(&parsed);

    Feature candidate = feature_;
    bool valid = parsed && value >= td::paramMinimum(field.param, false);
    if (valid) {
        td::setParamValue(&candidate, field.param, value);
        QString why;
        valid = td::validateFeature(candidate, &why);
    }
    field.edit->setProperty("valid", valid);
    field.edit->setStyleSheet(valid ? QString() : QStringLiteral("border: 1px solid #e06c5c;"));
    if (!valid) return;

    feature_ = candidate;
    settled_ = true;
    dwell_->stop();
    rebuildGhost();
}

void PlacementController::focusField(int index) {
    QLineEdit* edit = fields_[index].edit;
    edit->setFocus(Qt::TabFocusReason);
    edit->selectAll();  // the next digit typed replaces the old value
}

void PlacementController::cycleField(int delta) {
    int current = 0;
    for (int i = 0; i < int(fields_.size()); ++i)
        if (fields_[i].edit->hasFocus() || fields_[i].edit == viewport_->window()->focusWidget()) current = i;
    const int count = int(fields_.size());
    focusField((current + delta + count) % count);
    settled_ = true;
    dwell_->stop();
    refreshPose();
}

void PlacementController::syncFields() {
    syncing_ = true;
    for (const Field& field : fields_) {
        field.edit->setText(formatValue(td::paramValue(feature_, field.param)));
        field.edit->setProperty("valid", true);
        field.edit->setStyleSheet({});
    }
    syncing_ = false;
    if (dragHandle_ >= 0)
        if (QLineEdit* edit = fieldFor(dragInfo_.param)) edit->selectAll();
}

bool PlacementController::eventFilter(QObject* watched, QEvent* event) {
    if (!active_) return QObject::eventFilter(watched, event);
    if (watched == hud_ && event->type() == QEvent::Enter) {
        settled_ = true;
        dwell_->stop();
        refreshHandles();
    }
    if (event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            cycleField(key == Qt::Key_Tab ? 1 : -1);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

bool PlacementController::snapping() const { return snap_->isChecked(); }

void PlacementController::track(QPointF position) {
    lastPointer_ = position;
    V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);

    const auto hit = td::raycastMesh(*body_, origin, direction);
    const double planeT = std::abs(direction.z) > 1e-9 ? -origin.z / direction.z : -1.0;
    // With implicit snap off, new shapes ignore the faces under the pointer and stay on the grid.
    if (hit && snapSettings().implicit && (planeT <= 0 || hit->t < planeT)) {
        td::seatOnFace(&feature_, hit->point, hit->normal, snapping() ? snapSettings().linear : 0);
    } else if (planeT > 0) {
        const V3 onPlane = origin + direction * planeT;
        td::seatOnGrid(&feature_, std::clamp(onPlane.x, -kFarLimit, kFarLimit), std::clamp(onPlane.y, -kFarLimit, kFarLimit), snapping() ? snapSettings().linear : 0);
    }
    refreshPose();
}

void PlacementController::settle() {
    if (!active_ || settled_) return;
    settled_ = true;
    refreshHandles();
}

bool PlacementController::insideSettledArea(QPoint position) const {
    return handleBounds_.united(hud_->geometry()).adjusted(-kSettledMargin, -kSettledMargin, kSettledMargin, kSettledMargin).contains(position);
}

int PlacementController::handleAt(QPointF position) const {
    int best = -1;
    double bestDistance = kGrabPixels;
    for (int i = 0; i < int(handles_.size()); ++i) {
        const double distance = segmentDistance(position, handles_[i].start, handles_[i].end);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

QPointF PlacementController::handleGrabPoint(int index) const { return (handles_[index].start + handles_[index].end) * 0.5; }

bool PlacementController::toolMousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;
    const QPointF position = event->position();
    const int handle = settled_ ? handleAt(position) : -1;
    if (handle >= 0) beginDrag(handle, position, event->modifiers());
    else pressPending_ = true;
    return true;
}

bool PlacementController::toolMouseMove(QMouseEvent* event) {
    const QPointF position = event->position();
    if (dragHandle_ >= 0) {
        updateDrag(position, event->modifiers());
        return true;
    }
    if (pressPending_) return true;
    if (event->buttons() & (Qt::RightButton | Qt::MiddleButton)) return false;

    if (settled_) {
        if (insideSettledArea(position.toPoint())) {
            const int handle = handleAt(position);
            if (handle != hoveredHandle_) {
                hoveredHandle_ = handle;
                viewport_->setCursor(handle >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
                refreshHandles();
            }
            return true;
        }
        settled_ = false;
        hoveredHandle_ = -1;
        viewport_->unsetCursor();
    }

    const QPointF moved = position - lastPointer_;
    if (std::hypot(moved.x(), moved.y()) >= kDwellJitterPixels || !dwell_->isActive()) dwell_->start();
    track(position);
    return true;
}

bool PlacementController::toolMouseRelease(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;
    if (dragHandle_ >= 0) {
        dragHandle_ = -1;
        refreshHandles();
        return true;
    }
    if (pressPending_) {
        pressPending_ = false;
        commit();
    }
    return true;
}

void PlacementController::beginDrag(int handle, QPointF position, Qt::KeyboardModifiers modifiers) {
    dragHandle_ = handle;
    dragInfo_ = handles_[handle];
    dragStart_ = feature_;

    V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    const auto along = td::closestOnAxis(dragInfo_.axisOrigin, dragInfo_.axisDir, origin, direction);
    const double startValue = td::paramValue(feature_, dragInfo_.param);
    dragOffset_ = along ? *along - dragInfo_.scale * startValue : 0;

    if (QLineEdit* edit = fieldFor(dragInfo_.param)) {
        edit->setFocus(Qt::MouseFocusReason);
        edit->selectAll();
    }
    updateDrag(position, modifiers);
}

void PlacementController::updateDrag(QPointF position, Qt::KeyboardModifiers modifiers) {
    V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    const auto along = td::closestOnAxis(dragInfo_.axisOrigin, dragInfo_.axisDir, origin, direction);
    if (!along) return;

    const double raw = (*along - dragOffset_) / dragInfo_.scale;
    const double stepped = snapLength(raw, snapping());
    const double floor = td::paramMinimum(dragInfo_.param, false);
    const double value = std::max(stepped, snapping() && floor > 0 ? std::max(floor, snapSettings().linear) : floor);
    const double startValue = td::paramValue(dragStart_, dragInfo_.param);

    Feature next = dragStart_;
    if ((modifiers & Qt::ShiftModifier) && startValue > 0) {
        td::applyUniformScale(&next, dragStart_, value / startValue);
        td::setParamValue(&next, dragInfo_.param, value);  // keep the pulled value exact
    } else {
        td::setParamValue(&next, dragInfo_.param, value);
    }

    QString why;
    if (!td::validateFeature(next, &why)) return;  // stops at the shape's limits
    feature_ = next;
    rebuildGhost();
    syncFields();
}

void PlacementController::rebuildGhost() {
    Feature local = feature_;
    local.x = local.y = local.z = local.rx = local.ry = local.rz = 0;
    const td::EvalResult result = td::evaluateSingle(local);
    if (result.ok) viewport_->setGhost(result.mesh, td::sharpEdges(result.mesh));
    refreshPose();
}

void PlacementController::refreshPose() {
    QMatrix4x4 model;
    model.translate(feature_.x, feature_.y, feature_.z);
    model.rotate(feature_.rz, 0, 0, 1);
    model.rotate(feature_.ry, 0, 1, 0);
    model.rotate(feature_.rx, 1, 0, 0);
    viewport_->setGhostModel(model);
    refreshHandles();
    positionHud();
}

void PlacementController::refreshHandles() {
    viewport_->setGhostColor(settled_ ? kGhostSettled : kGhostMoving);

    handles_.clear();
    std::vector<Viewport::TintedMesh> arrows;
    QRectF bounds;
    bool first = true;

    for (const td::Handle& handle : td::handlesFor(feature_)) {
        const V3 axisOrigin = td::featureToWorld(feature_, handle.base);
        const V3 axisDir = td::featureDirectionToWorld(feature_, handle.dir);
        const V3 tip = axisOrigin + axisDir * (handle.scale * td::paramValue(feature_, handle.param));
        const double arrowLength = viewport_->worldPerPixel(tip) * kArrowPixels;
        const V3 end = tip + axisDir * arrowLength;

        ScreenHandle screen{handle.param, handle.scale, axisOrigin, axisDir, viewport_->project(tip), viewport_->project(end), handle.axis};
        const int index = int(handles_.size());
        const bool lit = index == hoveredHandle_ || index == dragHandle_;
        arrows.push_back({td::makeArrow(tip, axisDir, arrowLength), lit ? kHoverColor : kAxisColors[handle.axis]});

        const QRectF segment = QRectF(screen.start, screen.end).normalized();
        bounds = first ? segment : bounds.united(segment);
        first = false;
        handles_.push_back(screen);
    }
    handleBounds_ = bounds.toAlignedRect();
    viewport_->setHandles(arrows);
}

void PlacementController::positionHud() {
    const QSize size = hud_->sizeHint();
    int x = handleBounds_.right() + kHudGap;
    if (x + size.width() > viewport_->width()) x = handleBounds_.left() - kHudGap - size.width();
    x = std::clamp(x, 0, std::max(0, viewport_->width() - size.width()));
    const int y = std::clamp(handleBounds_.center().y() - size.height() / 2, 0, std::max(0, viewport_->height() - size.height()));
    hud_->setGeometry(x, y, size.width(), size.height());
}
