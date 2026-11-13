#include "app/PatternController.h"

#include "app/SnapSettings.h"
#include "core/Placement.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using td::V3;

namespace {

constexpr double kArrowPixels = 60.0;
constexpr double kGrabPixels = 11.0;
const QVector4D kAlongColor(0.92f, 0.32f, 0.30f, 1), kAcrossColor(0.36f, 0.80f, 0.38f, 1), kHoverColor(1.0f, 0.85f, 0.25f, 1);

QString formatValue(double value) {
    QString text = QString::number(value, 'f', 2);
    while (text.contains('.') && text.endsWith('0')) text.chop(1);
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

PatternController::PatternController(Viewport* viewport, QObject* parent) : QObject(parent), viewport_(viewport) {
    hud_ = new QFrame(viewport_);
    hud_->setObjectName("patternHud");
    hud_->setFrameShape(QFrame::StyledPanel);
    hud_->setAutoFillBackground(true);
    hud_->hide();
    auto* layout = new QVBoxLayout(hud_);
    layout->setContentsMargins(10, 6, 10, 6);
    grid_ = new QGridLayout;
    grid_->setVerticalSpacing(3);
    layout->addLayout(grid_);
    connect(viewport_, &Viewport::cameraChanged, this, [this] {
        if (active_) refreshHandles();
    });
}

QLineEdit* PatternController::addNumber(const QString& name, bool whole) {
    const int row = grid_->rowCount();
    auto* label = new QLabel(name, hud_);
    auto* edit = new QLineEdit(hud_);
    edit->setObjectName("pattern_" + name);
    edit->setFixedWidth(70);
    edit->setAlignment(Qt::AlignRight);
    edit->setValidator(new QRegularExpressionValidator(QRegularExpression(whole ? "[0-9]{0,3}" : "[0-9]*\\.?[0-9]{0,2}"), edit));
    grid_->addWidget(label, row, 0);
    grid_->addWidget(edit, row, 1);
    widgets_.insert(widgets_.end(), {label, edit});
    connect(edit, &QLineEdit::textEdited, this, [this] {
        readFields();
        refreshHandles();
        emit changed();
    });
    connect(edit, &QLineEdit::returnPressed, this, &PatternController::confirmRequested);
    return edit;
}

QComboBox* PatternController::addChoice(const QString& name, const QStringList& options) {
    const int row = grid_->rowCount();
    auto* label = new QLabel(name, hud_);
    auto* box = new QComboBox(hud_);
    box->setObjectName("pattern_" + name);
    box->addItems(options);
    box->setFocusPolicy(Qt::NoFocus);
    grid_->addWidget(label, row, 0);
    grid_->addWidget(box, row, 1);
    widgets_.insert(widgets_.end(), {label, box});
    connect(box, &QComboBox::currentIndexChanged, this, [this] {
        if (syncing_) return;
        readFields();
        refreshHandles();
        emit changed();
    });
    return box;
}

void PatternController::build() {
    for (QWidget* w : widgets_) delete w;
    widgets_.clear();
    count1_ = distance1_ = count2_ = distance2_ = angle_ = nullptr;
    mode_ = nullptr;
    symmetric_ = nullptr;

    switch (kind_) {
        case PatternKind::Rectangular:
            count1_ = addNumber(tr("Copies along"), true);
            distance1_ = addNumber(tr("Distance along"), false);
            count2_ = addNumber(tr("Copies across"), true);
            distance2_ = addNumber(tr("Distance across"), false);
            mode_ = addChoice(tr("Distance is"), {tr("between copies"), tr("the whole span")});
            symmetric_ = new QCheckBox(tr("Symmetric: both sides of the original"), hud_);
            symmetric_->setObjectName("pattern_Symmetric");
            symmetric_->setFocusPolicy(Qt::NoFocus);
            grid_->addWidget(symmetric_, grid_->rowCount(), 0, 1, 2);
            widgets_.push_back(symmetric_);
            connect(symmetric_, &QCheckBox::toggled, this, [this] {
                if (syncing_) return;
                readFields();
                refreshHandles();
                emit changed();
            });
            break;
        case PatternKind::Circular:
            count1_ = addNumber(tr("Copies"), true);
            mode_ = addChoice(tr("Spread"), {tr("full circle"), tr("over an angle")});
            angle_ = addNumber(tr("Angle"), false);
            break;
        case PatternKind::Path:
            count1_ = addNumber(tr("Copies"), true);
            distance1_ = addNumber(tr("Distance (0 = whole path)"), false);
            mode_ = addChoice(tr("Copies face"), {tr("the same way"), tr("along the path")});
            break;
        case PatternKind::Mirror: break;
    }
    syncFields();
}

void PatternController::begin(PatternKind kind) {
    active_ = true;
    kind_ = kind;
    dragging_ = -1;
    build();
    if (kind_ == PatternKind::Mirror) {
        hud_->hide();
    } else {
        hud_->show();
        hud_->raise();
        place();
        if (count1_) {
            count1_->setFocus(Qt::TabFocusReason);
            count1_->selectAll();
        }
    }
    viewport_->setTool(this);
    viewport_->setMouseTracking(true);
    refreshHandles();
}

void PatternController::end() {
    if (!active_) return;
    active_ = false;
    dragging_ = -1;
    hud_->hide();
    handles_.clear();
    viewport_->setHandles({});
    viewport_->setMouseTracking(false);
    viewport_->setTool(nullptr);
}

void PatternController::place() {
    // Sits under the selection bar, which is centred at the top of the canvas.
    const QWidget* bar = viewport_->findChild<QFrame*>("stepBar");
    hud_->adjustSize();
    const QSize size = hud_->sizeHint();
    const int top = bar && bar->isVisible() ? bar->geometry().bottom() + 6 : 10;
    hud_->setGeometry((viewport_->width() - size.width()) / 2, top, size.width(), size.height());
}

void PatternController::readFields() {
    const auto whole = [](QLineEdit* e, int fallback) { return e ? std::clamp(e->text().toInt(), 1, td::kMaxPatternCopies) : fallback; };
    const auto number = [](QLineEdit* e, double fallback) {
        bool ok = false;
        const double v = e ? e->text().toDouble(&ok) : fallback;
        return ok ? v : fallback;
    };
    switch (kind_) {
        case PatternKind::Rectangular:
            rectangular.countAlong = whole(count1_, rectangular.countAlong);
            rectangular.spacingAlong = number(distance1_, rectangular.spacingAlong);
            rectangular.countAcross = whole(count2_, rectangular.countAcross);
            rectangular.spacingAcross = number(distance2_, rectangular.spacingAcross);
            rectangular.totalSpan = mode_->currentIndex() == 1;
            rectangular.symmetric = symmetric_->isChecked();
            break;
        case PatternKind::Circular:
            circular.count = whole(count1_, circular.count);
            circular.fullCircle = mode_->currentIndex() == 0;
            circular.angle = number(angle_, circular.angle);
            angle_->setEnabled(!circular.fullCircle);
            break;
        case PatternKind::Path:
            path.count = whole(count1_, path.count);
            path.distance = number(distance1_, path.distance);
            path.followPath = mode_->currentIndex() == 1;
            break;
        case PatternKind::Mirror: break;
    }
}

void PatternController::syncFields() {
    syncing_ = true;
    const auto set = [](QLineEdit* e, double v) {
        if (e) e->setText(formatValue(v));
    };
    switch (kind_) {
        case PatternKind::Rectangular:
            set(count1_, rectangular.countAlong);
            set(distance1_, rectangular.spacingAlong);
            set(count2_, rectangular.countAcross);
            set(distance2_, rectangular.spacingAcross);
            mode_->setCurrentIndex(rectangular.totalSpan ? 1 : 0);
            symmetric_->setChecked(rectangular.symmetric);
            break;
        case PatternKind::Circular:
            set(count1_, circular.count);
            mode_->setCurrentIndex(circular.fullCircle ? 0 : 1);
            set(angle_, circular.angle);
            angle_->setEnabled(!circular.fullCircle);
            break;
        case PatternKind::Path:
            set(count1_, path.count);
            set(distance1_, path.distance);
            mode_->setCurrentIndex(path.followPath ? 1 : 0);
            break;
        case PatternKind::Mirror: break;
    }
    syncing_ = false;
}

void PatternController::setOrigin(V3 origin) {
    origin_ = origin;
    refreshHandles();
}

double PatternController::reach(bool across) const {
    const int n = across ? rectangular.countAcross : rectangular.countAlong;
    const double distance = across ? rectangular.spacingAcross : rectangular.spacingAlong;
    return rectangular.totalSpan ? distance : distance * (n - 1);
}

void PatternController::refreshHandles() {
    handles_.clear();
    if (!active_ || kind_ != PatternKind::Rectangular) {
        viewport_->setHandles({});
        return;
    }
    const V3 along = td::normalized(rectangular.along);
    const V3 directions[2] = {along, td::acrossDirection(along)};
    std::vector<Viewport::TintedMesh> meshes;
    for (int a = 0; a < 2; ++a) {
        // An arrow sits at the farthest copy and drags the spacing; one copy has nothing to space.
        if ((a == 0 ? rectangular.countAlong : rectangular.countAcross) < 2) continue;
        const V3 tip = origin_ + directions[a] * reach(a == 1);
        const double length = viewport_->worldPerPixel(tip) * kArrowPixels;
        handles_.push_back({a == 1, directions[a], viewport_->project(tip), viewport_->project(tip + directions[a] * length)});
        const bool lit = int(handles_.size()) - 1 == dragging_;
        meshes.push_back({td::makeArrow(tip, directions[a], length), lit ? kHoverColor : (a == 0 ? kAlongColor : kAcrossColor)});
    }
    viewport_->setHandles(meshes);
}

bool PatternController::toolMousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;
    int best = -1;
    double bestDistance = kGrabPixels;
    for (int i = 0; i < int(handles_.size()); ++i) {
        const double d = segmentDistance(event->position(), handles_[i].start, handles_[i].end);
        if (d <= bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    if (best < 0) return false;  // elsewhere: the click picks for the selection bar, or turns the view
    V3 origin, direction;
    viewport_->cursorRay(event->position(), &origin, &direction);
    const auto along = td::closestOnAxis(origin_, handles_[best].dir, origin, direction);
    dragOffset_ = along ? *along - reach(handles_[best].across) : 0;
    dragging_ = best;
    refreshHandles();
    return true;
}

bool PatternController::toolMouseMove(QMouseEvent* event) {
    if (dragging_ < 0) return false;
    const Handle handle = handles_[dragging_];
    V3 origin, direction;
    viewport_->cursorRay(event->position(), &origin, &direction);
    const auto along = td::closestOnAxis(origin_, handle.dir, origin, direction);
    if (!along) return true;
    const int n = handle.across ? rectangular.countAcross : rectangular.countAlong;
    const bool snapping = snapSettings().grid;
    const double least = snapping ? snapSettings().linear : 0.01;
    const double span = std::max(least, snapLength(*along - dragOffset_, snapping));
    const double distance = rectangular.totalSpan ? span : std::max(least, snapLength(span / (n - 1), snapping));
    (handle.across ? rectangular.spacingAcross : rectangular.spacingAlong) = distance;
    syncFields();
    refreshHandles();
    emit changed();
    return true;
}

bool PatternController::toolMouseRelease(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || dragging_ < 0) return false;
    dragging_ = -1;
    refreshHandles();
    return true;
}
