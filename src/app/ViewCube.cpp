#include "app/ViewCube.h"

#include "app/Icons.h"
#include "app/Viewport.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QtMath>

#include <algorithm>
#include <cmath>

using td::V3;

namespace {

constexpr int kSize = 128;
constexpr double kScale = 30.0;     // pixels per unit of the cube's half-width
constexpr double kEdgeBand = 0.56;  // where an edge strip begins, out of 1 from the face centre

struct Face {
    V3 normal, right, up;
    const char* label;
};
// right and up are as the face reads from outside, so its label is the right way round.
const Face kFaces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, "TOP"},     {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}, "BOTTOM"},
                        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, "FRONT"},  {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, "BACK"},
                        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "RIGHT"},   {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, "LEFT"}};

QVector3D toQt(V3 v) { return QVector3D(float(v.x), float(v.y), float(v.z)); }

}  // namespace

ViewCube::ViewCube(Viewport* viewport) : QWidget(viewport), viewport_(viewport) {
    setObjectName("viewCube");
    setFixedSize(kSize, kSize);
    setMouseTracking(true);
    setToolTip(tr("Click a face, edge or corner to look from there; drag to orbit"));

    home_ = new QToolButton(this);
    home_->setObjectName("viewCubeHome");
    home_->setIcon(makeIcon(Icon::Home, QColor(230, 233, 238)));
    home_->setIconSize(QSize(18, 18));
    home_->setAutoRaise(true);
    home_->setToolTip(tr("Home view"));
    home_->move(2, 2);
    home_->hide();
    connect(home_, &QToolButton::clicked, this, &ViewCube::homeRequested);
    connect(viewport_, &Viewport::cameraChanged, this, qOverload<>(&QWidget::update));
}

QPointF ViewCube::project(V3 point) const {
    const QVector3D v = viewport_->viewRotation().map(toQt(point));
    return {width() / 2.0 + v.x() * kScale, height() / 2.0 - v.y() * kScale};
}

std::vector<ViewCube::Region> ViewCube::regions() const {
    const QMatrix4x4 rotation = viewport_->viewRotation();
    std::vector<std::pair<float, Region>> found;
    const double bounds[4] = {-1, -kEdgeBand, kEdgeBand, 1};
    for (int f = 0; f < 6; ++f) {
        const Face& face = kFaces[f];
        const float facing = rotation.mapVector(toQt(face.normal)).z();
        if (facing <= 0.02f) continue;  // turned away
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const auto corner = [&](int a, int b) { return project(face.normal + face.right * bounds[a] + face.up * bounds[b]); };
                Region region{QPolygonF{corner(i, j), corner(i + 1, j), corner(i + 1, j + 1), corner(i, j + 1)},
                              td::normalized(face.normal + face.right * double(i - 1) + face.up * double(j - 1)), f};
                found.push_back({facing, region});
            }
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Region> out;
    for (auto& [depth, region] : found) out.push_back(std::move(region));
    return out;
}

int ViewCube::regionAt(QPointF position) const {
    const auto all = regions();
    for (int i = int(all.size()) - 1; i >= 0; --i)
        if (all[i].shape.containsPoint(position, Qt::OddEvenFill)) return i;
    return -1;
}

QPoint ViewCube::pointFor(V3 direction) const {
    const V3 wanted = td::normalized(direction);
    for (const Region& region : regions())
        if (td::length(region.direction - wanted) < 1e-6) return region.shape.boundingRect().center().toPoint();
    return {-1, -1};
}

void ViewCube::orientationFor(V3 direction, float currentYaw, float* yaw, float* pitch) {
    const V3 d = td::normalized(direction);
    *pitch = float(qRadiansToDegrees(std::asin(std::clamp(d.z, -1.0, 1.0))));
    // Straight down or up has no yaw of its own; look with the front at the bottom of the screen.
    *yaw = std::hypot(d.x, d.y) < 1e-6 ? -90.0f : float(qRadiansToDegrees(std::atan2(d.y, d.x)));
    (void)currentYaw;
}

void ViewCube::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto all = regions();
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(13);
    p.setFont(font);

    for (int i = 0; i < int(all.size()); ++i) {
        const Region& region = all[i];
        const bool lit = i == hovered_;
        p.setPen(Qt::NoPen);
        p.setBrush(lit ? QColor(120, 190, 255) : QColor(236, 238, 242));
        p.drawPolygon(region.shape);
    }
    // Face borders and labels over the pieces.
    p.setPen(QPen(QColor(90, 96, 108), 1.2));
    p.setBrush(Qt::NoBrush);
    const QMatrix4x4 rotation = viewport_->viewRotation();
    for (const Face& face : kFaces) {
        if (rotation.mapVector(toQt(face.normal)).z() <= 0.02f) continue;
        const QPointF tl = project(face.normal - face.right + face.up), tr = project(face.normal + face.right + face.up);
        const QPointF br = project(face.normal + face.right - face.up), bl = project(face.normal - face.right - face.up);
        p.setPen(QPen(QColor(90, 96, 108), 1.2));
        p.drawPolygon(QPolygonF{tl, tr, br, bl});
        QTransform map;
        if (!QTransform::squareToQuad(QPolygonF{tl, tr, br, bl}, map)) continue;
        p.save();
        p.setTransform(map, true);
        p.scale(1.0 / 100.0, 1.0 / 100.0);
        p.setPen(QColor(40, 44, 52));
        QFont small = font;
        small.setPixelSize(face.label == QLatin1String("BOTTOM") ? 19 : 22);
        p.setFont(small);
        p.drawText(QRectF(0, 0, 100, 100), Qt::AlignCenter, QString::fromLatin1(face.label));
        p.restore();
    }
}

void ViewCube::mousePressEvent(QMouseEvent* event) {
    pressed_ = true;
    dragged_ = false;
    pressPosition_ = lastPosition_ = event->position().toPoint();
}

void ViewCube::mouseMoveEvent(QMouseEvent* event) {
    const QPoint now = event->position().toPoint();
    if (pressed_) {
        if (!dragged_ && (now - pressPosition_).manhattanLength() >= 4) dragged_ = true;
        if (dragged_) viewport_->orbitBy(QPointF(now - lastPosition_));
        lastPosition_ = now;
        return;
    }
    const int over = regionAt(event->position());
    if (over != hovered_) {
        hovered_ = over;
        update();
    }
}

void ViewCube::mouseReleaseEvent(QMouseEvent* event) {
    const bool click = pressed_ && !dragged_;
    pressed_ = dragged_ = false;
    if (!click) return;
    const int region = regionAt(event->position());
    if (region < 0) return;
    float yaw = 0, pitch = 0;
    orientationFor(regions()[region].direction, viewport_->yaw(), &yaw, &pitch);
    viewport_->setOrientation(yaw, pitch, true);
}

void ViewCube::enterEvent(QEnterEvent*) { home_->show(); }

void ViewCube::leaveEvent(QEvent*) {
    home_->hide();
    hovered_ = -1;
    update();
}
