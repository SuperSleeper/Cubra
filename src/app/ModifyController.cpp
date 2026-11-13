#include "app/ModifyController.h"

#include "app/SnapSettings.h"
#include "core/Picking.h"
#include "core/Placement.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using td::ModifyKind;
using td::V3;

namespace {

constexpr double kArrowPixels = 70.0;
constexpr double kGrabPixels = 12.0;
constexpr int kHudGap = 110;  // clears a small part in view
constexpr double kMinimumSize = 0.1;
constexpr double kDragStartPixels = 3.0;
constexpr double kNearlyAlongView = 0.97;

const QVector4D kArrowColor(1.0f, 0.62f, 0.15f, 1);
const QVector4D kHoverColor(1.0f, 0.85f, 0.25f, 1);

QString formatValue(double value) {
    QString text = QString::number(value, 'f', 2);
    while (text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text;
}

}  // namespace

ModifyController::ModifyController(Viewport* viewport, QObject* parent) : QObject(parent), viewport_(viewport) {
    hud_ = new QFrame(viewport_);
    hud_->setObjectName("modifyHud");
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

    auto* row = new QHBoxLayout;
    fieldName_ = new QLabel(hud_);
    fieldName_->setMinimumWidth(70);
    field_ = new QLineEdit(hud_);
    field_->setObjectName("modifyValue");
    field_->setFixedWidth(72);
    field_->setAlignment(Qt::AlignRight);
    field_->setValidator(new QRegularExpressionValidator(QRegularExpression("-?[0-9]*\\.?[0-9]{0,3}"), field_));
    row->addWidget(fieldName_);
    row->addWidget(field_);
    row->addWidget(new QLabel(tr("mm"), hud_));
    layout->addLayout(row);

    snap_ = new QCheckBox(tr("Snap"), hud_);
    snap_->setChecked(true);
    snap_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(snap_);

    auto* hint = new QLabel(tr("Enter apply  ·  Esc cancel"), hud_);
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    hint->setFont(small);
    hint->setEnabled(false);
    layout->addWidget(hint);

    connect(field_, &QLineEdit::textEdited, this, &ModifyController::onEdited);
    connect(field_, &QLineEdit::returnPressed, this, &ModifyController::commit);
    connect(viewport_, &Viewport::cameraChanged, this, [this] {
        if (!active_) return;
        if (!dragging_ && kind_ != ModifyKind::Extrude) rebuildGuides();  // another arrow direction may read better now
        refreshHandles();
    });
}

bool ModifyController::snapping() const { return snap_->isChecked(); }

bool ModifyController::isSigned() const { return kind_ == ModifyKind::Extrude; }

void ModifyController::begin(ModifyKind kind, const td::Mesh& mesh, const td::Topology& topology, const std::vector<int>& entities,
                             std::function<bool(const Features&)> preview) {
    if (active_) finish();
    active_ = true;
    kind_ = kind;
    mesh_ = mesh;
    topology_ = topology;
    entities_ = entities;
    preview_ = std::move(preview);
    pending_ = dragging_ = false;
    hovered_ = -1;
    current_.clear();
    snap_->setChecked(snapSettings().grid);

    switch (kind) {
        case ModifyKind::Chamfer: title_->setText(tr("Chamfer")); break;
        case ModifyKind::Fillet:
        case ModifyKind::PointFillet: title_->setText(tr("Fillet")); break;
        case ModifyKind::Extrude: title_->setText(tr("Extrude")); break;
    }
    fieldName_->setText(kind == ModifyKind::Fillet || kind == ModifyKind::PointFillet ? tr("Radius") : tr("Distance"));

    // A face starts where it is. An edge tool starts small so its effect shows.
    value_ = kind == ModifyKind::Extrude ? 0.0 : 1.0;
    if (value_ != 0) applyValue(value_);
    syncField();
    rebuildGuides();

    viewport_->setTool(this);
    viewport_->setMouseTracking(true);
    hud_->adjustSize();
    hud_->show();
    hud_->raise();
    refreshHandles();
    field_->setFocus(Qt::TabFocusReason);
    field_->selectAll();
}

void ModifyController::retarget(const std::vector<int>& entities) {
    entities_ = entities;
    if (!applyValue(value_)) {
        value_ = isSigned() ? 0.0 : 1.0;
        applyValue(value_);
        syncField();
    }
    rebuildGuides();
    refreshHandles();
}

void ModifyController::finish() {
    active_ = false;
    pending_ = dragging_ = false;
    hovered_ = -1;
    arrows_.clear();
    hud_->hide();
    viewport_->setMouseTracking(false);
    viewport_->setTool(nullptr);
    viewport_->setHandles({});
    viewport_->unsetCursor();
}

void ModifyController::cancel() {
    if (!active_) return;
    finish();
    emit cancelled();
}

bool ModifyController::commitIfChanged() {
    if (!active_ || current_.empty()) return false;
    const Features result = current_;
    const ModifyKind kind = kind_;
    finish();
    emit committed(kind, result);
    return true;
}

void ModifyController::commit() {
    if (!commitIfChanged()) cancel();
}

bool ModifyController::applyValue(double value) {
    const Features features = value == 0 ? Features{} : td::modifyFeatures(kind_, mesh_, topology_, entities_, value);
    if ((features.empty() && value != 0) || !preview_(features)) return false;
    value_ = value;
    current_ = features;
    emit previewMoved(value != 0);
    return true;
}

void ModifyController::onEdited() {
    bool parsed = false;
    const double value = field_->text().toDouble(&parsed);
    const bool allowed = parsed && (isSigned() || value >= kMinimumSize);
    const bool valid = allowed && applyValue(value);
    field_->setStyleSheet(valid ? QString() : QStringLiteral("border: 1px solid #e06c5c;"));
    if (valid) refreshHandles();
}

void ModifyController::syncField() {
    field_->setText(formatValue(value_));
    field_->setStyleSheet({});
}

void ModifyController::rebuildGuides() {
    V3 origin, view;
    viewport_->cursorRay(QPointF(viewport_->width() / 2.0, viewport_->height() / 2.0), &origin, &view);

    guides_.clear();
    faceMeshes_.clear();
    // Faces each get an arrow along their own normal. Selected edges share one,
    // on the edge chosen last, which is where the user is looking.
    const std::vector<int> shown = kind_ == ModifyKind::Extrude ? entities_ : std::vector<int>{entities_.back()};
    for (int entity : shown) {
        guides_.push_back(td::modifyGuide(kind_, mesh_, topology_, entity, view));
        if (kind_ == ModifyKind::Extrude) {
            td::Mesh face;
            face.positions = mesh_.positions;
            for (uint32_t t : topology_.faces[entity].triangles)
                for (int i = 0; i < 3; ++i) face.indices.push_back(mesh_.indices[t * 3 + i]);
            faceMeshes_.push_back(std::move(face));
        }
    }
}

void ModifyController::refreshHandles() {
    arrows_.clear();
    std::vector<Viewport::TintedMesh> meshes;
    for (int i = 0; i < int(guides_.size()); ++i) {
        const td::ModifyGuide& guide = guides_[i];
        const V3 tip = guide.origin + guide.dir * (guide.scale * value_);
        // The arrow always points the way the material is about to go.
        const V3 way = value_ < 0 ? guide.dir * -1.0 : guide.dir;
        const double arrowLength = viewport_->worldPerPixel(tip) * kArrowPixels;
        arrows_.push_back({viewport_->project(tip), viewport_->project(tip + way * arrowLength)});
        meshes.push_back({td::makeArrow(tip, way, arrowLength), (i == hovered_ || (dragging_ && i == hovered_)) ? kHoverColor : kArrowColor});
    }
    viewport_->setHandles(meshes);
    if (arrows_.empty()) return;

    const QSize size = hud_->sizeHint();
    const QPointF anchor = arrows_.front().end;
    int x = int(anchor.x()) + kHudGap;
    if (x + size.width() > viewport_->width()) x = int(anchor.x()) - kHudGap - size.width();
    hud_->setGeometry(std::clamp(x, 0, std::max(0, viewport_->width() - size.width())),
                      std::clamp(int(anchor.y()) - size.height() / 2, 0, std::max(0, viewport_->height() - size.height())), size.width(), size.height());
}

int ModifyController::arrowAt(QPointF p) const {
    int best = -1;
    double bestDistance = kGrabPixels;
    for (int i = 0; i < int(arrows_.size()); ++i) {
        const QPointF ab = arrows_[i].end - arrows_[i].start;
        const double lengthSquared = QPointF::dotProduct(ab, ab);
        const double t = lengthSquared > 0 ? std::clamp(QPointF::dotProduct(p - arrows_[i].start, ab) / lengthSquared, 0.0, 1.0) : 0.0;
        const QPointF offset = p - (arrows_[i].start + ab * t);
        const double distance = std::hypot(offset.x(), offset.y());
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

int ModifyController::faceAt(QPointF position) const {
    if (kind_ != ModifyKind::Extrude) return -1;
    V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    int best = -1;
    double nearest = 0;
    for (int i = 0; i < int(faceMeshes_.size()); ++i) {
        // Test the face where the preview currently shows it.
        const auto hit = td::raycastMesh(faceMeshes_[i], origin - guides_[i].dir * value_, direction);
        if (hit && (best < 0 || hit->t < nearest)) {
            best = i;
            nearest = hit->t;
        }
    }
    return best;
}

bool ModifyController::toolMousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;
    const QPointF position = event->position();
    int target = arrowAt(position);
    if (target < 0) target = faceAt(position);
    if (target < 0) return false;  // elsewhere: the view rotates or the click selects

    dragGuide_ = guides_[target];
    dragStartValue_ = value_;
    pressPosition_ = position;
    pressShift_ = event->modifiers() & Qt::ShiftModifier;
    hovered_ = target;

    V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    const auto along = td::closestOnAxis(dragGuide_.origin, dragGuide_.dir, origin, direction);
    // Looking almost straight down the axis, the 3D answer is unstable.
    dragOnScreen_ = !along || std::abs(td::dot(dragGuide_.dir, direction)) > kNearlyAlongView;
    dragOffset_ = along ? *along - dragGuide_.scale * value_ : 0;
    pending_ = true;
    return true;
}

void ModifyController::updateDrag(QPointF position) {
    double raw;
    if (dragOnScreen_) {
        const double perPixel = viewport_->worldPerPixel(dragGuide_.origin);
        raw = dragStartValue_ + (pressPosition_.y() - position.y()) * perPixel / dragGuide_.scale;
    } else {
        V3 origin, direction;
        viewport_->cursorRay(position, &origin, &direction);
        const auto along = td::closestOnAxis(dragGuide_.origin, dragGuide_.dir, origin, direction);
        if (!along) return;
        raw = (*along - dragOffset_) / dragGuide_.scale;
    }

    double stepped = snapLength(raw, snapping());
    if (!isSigned()) stepped = std::max(stepped, snapping() ? std::max(snapSettings().linear, kMinimumSize) : kMinimumSize);
    if (stepped != value_ && applyValue(stepped)) {
        syncField();
        field_->selectAll();
        refreshHandles();
    }
}

bool ModifyController::toolMouseMove(QMouseEvent* event) {
    const QPointF position = event->position();
    if (pending_) {
        const QPointF moved = position - pressPosition_;
        if (std::hypot(moved.x(), moved.y()) < kDragStartPixels) return true;
        pending_ = false;
        dragging_ = true;
        field_->setFocus(Qt::MouseFocusReason);
        field_->selectAll();
    }
    if (dragging_) {
        updateDrag(position);
        return true;
    }
    if (event->buttons() != Qt::NoButton) return false;

    int over = arrowAt(position);
    if (over < 0) over = faceAt(position);
    if (over != hovered_) {
        hovered_ = over;
        if (over >= 0) viewport_->setCursor(Qt::SizeAllCursor);
        else viewport_->unsetCursor();
        refreshHandles();
    }
    return false;
}

bool ModifyController::toolMouseRelease(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;
    if (dragging_) {
        dragging_ = false;
        rebuildGuides();
        refreshHandles();
        return true;
    }
    if (pending_) {
        pending_ = false;
        emit clickedAway(event->position().toPoint(), pressShift_);
        return true;
    }
    return false;
}
