#include "app/Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <cmath>
#include <numbers>

namespace {

constexpr int kPixels = 96;
constexpr double kGrid = 24.0;  // all shapes below are drawn on a 24x24 grid

void drawCube(QPainter& p, double left, double top, double size) {
    const double depth = size * 0.3;
    const QRectF front(left, top + depth, size - depth, size - depth);
    p.drawRect(front);
    p.drawPolyline(QPolygonF{front.topLeft(), QPointF(left + depth, top), QPointF(left + size, top), QPointF(left + size, top + size - depth), front.bottomRight()});
    p.drawLine(front.topRight(), QPointF(left + size, top));
}

void drawCylinder(QPainter& p, double topHalfWidth) {
    p.drawEllipse(QPointF(12, 6), topHalfWidth, topHalfWidth * 0.35);
    p.drawLine(QPointF(12 - topHalfWidth, 6), QPointF(4, 18));
    p.drawLine(QPointF(12 + topHalfWidth, 6), QPointF(20, 18));
    QPainterPath base;
    base.moveTo(4, 18);
    base.cubicTo(4, 23, 20, 23, 20, 18);
    p.drawPath(base);
}

void drawArrow(QPainter& p, bool left) {
    const double tip = left ? 3 : 21;
    const double tail = left ? 21 : 3;
    const double barb = left ? 10 : 14;
    p.drawLine(QPointF(tail, 12), QPointF(tip, 12));
    p.drawPolyline(QPolygonF{QPointF(barb, 5), QPointF(tip, 12), QPointF(barb, 19)});
}

void draw(QPainter& p, Icon icon, const QColor& color) {
    switch (icon) {
        case Icon::Folder: {
            QPainterPath path;
            path.moveTo(2.5, 19.5);
            path.lineTo(2.5, 5);
            path.lineTo(9.5, 5);
            path.lineTo(12, 8);
            path.lineTo(21.5, 8);
            path.lineTo(21.5, 19.5);
            path.closeSubpath();
            p.drawPath(path);
            break;
        }
        case Icon::Undo: drawArrow(p, true); break;
        case Icon::Redo: drawArrow(p, false); break;
        case Icon::Select: {
            QPen dashed = p.pen();
            dashed.setDashPattern({2.2, 1.8});
            p.setPen(dashed);
            p.drawRect(QRectF(2.5, 2.5, 15, 15));
            p.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(color);
            p.drawPolygon(QPolygonF{QPointF(11, 10), QPointF(21.5, 15), QPointF(17, 17), QPointF(15, 21.5)});
            break;
        }
        case Icon::Primitives:
            drawCube(p, 2.5, 2.5, 11);
            p.drawEllipse(QPointF(17, 17), 4.8, 4.8);
            break;
        case Icon::Fit:
            for (int sx : {-1, 1})
                for (int sy : {-1, 1})
                    p.drawPolyline(QPolygonF{QPointF(12 + sx * 9.5, 12 + sy * 4), QPointF(12 + sx * 9.5, 12 + sy * 9.5), QPointF(12 + sx * 4, 12 + sy * 9.5)});
            p.drawEllipse(QPointF(12, 12), 3.5, 3.5);
            break;
        case Icon::DesignPanel:
            p.drawRoundedRect(QRectF(3, 3.5, 18, 17), 1.5, 1.5);
            p.drawLine(QPointF(9, 3.5), QPointF(9, 20.5));
            for (double y : {8.0, 12.0, 16.0}) p.drawLine(QPointF(12, y), QPointF(18, y));
            break;
        case Icon::AiPanel: {
            QPainterPath path;
            path.addRoundedRect(QRectF(2.5, 3.5, 19, 13.5), 2.5, 2.5);
            p.drawPath(path);
            p.drawPolyline(QPolygonF{QPointF(8, 17), QPointF(8, 21.5), QPointF(13, 17)});
            p.setBrush(color);
            for (double x : {8.0, 12.0, 16.0}) p.drawEllipse(QPointF(x, 10.2), 0.7, 0.7);
            break;
        }
        case Icon::Box: drawCube(p, 3, 3, 18); break;
        case Icon::RoundedBox: p.drawRoundedRect(QRectF(3.5, 3.5, 17, 17), 5, 5); break;
        case Icon::Cylinder: drawCylinder(p, 8); break;
        case Icon::Cone: drawCylinder(p, 0.4); break;
        case Icon::HexPrism: {
            QPolygonF hex;
            for (int i = 0; i < 6; ++i) {
                const double a = std::numbers::pi / 3 * i;
                hex << QPointF(12 + 9.5 * std::cos(a), 12 + 9.5 * std::sin(a));
            }
            p.drawPolygon(hex);
            break;
        }
        case Icon::Sphere:
            p.drawEllipse(QPointF(12, 12), 9, 9);
            p.drawEllipse(QPointF(12, 12), 9, 3);
            break;
        case Icon::Transform: {
            p.drawRect(QRectF(9, 9, 6, 6));
            const QPointF tips[4] = {{12, 2}, {12, 22}, {2, 12}, {22, 12}};
            const QPointF roots[4] = {{12, 7}, {12, 17}, {7, 12}, {17, 12}};
            for (int i = 0; i < 4; ++i) {
                p.drawLine(roots[i], tips[i]);
                const QPointF along = (tips[i] - roots[i]) / 5.0;
                const QPointF side(-along.y(), along.x());
                p.drawPolyline(QPolygonF{tips[i] - along * 0.6 + side * 0.6, tips[i], tips[i] - along * 0.6 - side * 0.6});
            }
            break;
        }
        case Icon::Modify: {
            QPainterPath path;
            path.moveTo(3.5, 20.5);
            path.lineTo(3.5, 5.5);
            path.lineTo(12, 5.5);
            path.arcTo(QRectF(6.5, 5.5, 14, 14), 90, -90);
            path.lineTo(20.5, 20.5);
            path.closeSubpath();
            p.drawPath(path);
            break;
        }
        case Icon::Combine:
            p.drawEllipse(QPointF(9, 12), 6.5, 6.5);
            p.drawEllipse(QPointF(15, 12), 6.5, 6.5);
            break;
        case Icon::Snap: {
            QPainterPath magnet;
            magnet.moveTo(5, 4);
            magnet.lineTo(5, 13);
            magnet.arcTo(QRectF(5, 6, 14, 14), 180, 180);
            magnet.lineTo(19, 4);
            p.drawPath(magnet);
            p.drawLine(QPointF(3, 7.5), QPointF(7, 7.5));
            p.drawLine(QPointF(17, 7.5), QPointF(21, 7.5));
            break;
        }
        case Icon::Orbit: {
            drawCube(p, 7, 8, 10);
            QPainterPath arc;
            arc.arcMoveTo(QRectF(2.5, 3, 19, 18), 200);
            arc.arcTo(QRectF(2.5, 3, 19, 18), 200, 250);
            p.drawPath(arc);
            const QPointF end = arc.currentPosition();
            p.drawPolyline(QPolygonF{end + QPointF(-3.2, -1.2), end, end + QPointF(-0.6, -3.4)});
            break;
        }
        case Icon::Zoom:
            p.drawEllipse(QPointF(10, 10), 6.5, 6.5);
            p.drawLine(QPointF(14.8, 14.8), QPointF(21, 21));
            break;
        case Icon::VisualStyle: {
            p.drawEllipse(QPointF(14.5, 9.5), 6.5, 6.5);
            p.setBrush(color);
            drawCube(p, 3, 9, 11);
            break;
        }
        case Icon::Eye: {
            QPainterPath eye;
            eye.moveTo(2.5, 12);
            eye.quadTo(12, 3, 21.5, 12);
            eye.quadTo(12, 21, 2.5, 12);
            p.drawPath(eye);
            p.setBrush(color);
            p.drawEllipse(QPointF(12, 12), 3, 3);
            break;
        }
        case Icon::GridEye:
            p.drawPolygon(QPolygonF{QPointF(2.5, 20.5), QPointF(7, 11), QPointF(21.5, 11), QPointF(17, 20.5)});
            p.drawLine(QPointF(4.8, 15.8), QPointF(19.2, 15.8));
            p.drawLine(QPointF(12, 11), QPointF(9.8, 20.5));
            p.drawEllipse(QPointF(18, 5.5), 3.2, 2);
            break;
        case Icon::Camera:
            p.drawRoundedRect(QRectF(2.5, 7, 19, 13), 2, 2);
            p.drawPolyline(QPolygonF{QPointF(8, 7), QPointF(9.5, 4), QPointF(14.5, 4), QPointF(16, 7)});
            p.drawEllipse(QPointF(12, 13.5), 3.8, 3.8);
            break;
        case Icon::SnapPoints:
        case Icon::SnapGrid: {
            QPainterPath magnet;
            magnet.moveTo(3, 3);
            magnet.lineTo(3, 9);
            magnet.arcTo(QRectF(3, 3, 12, 12), 180, 180);
            magnet.lineTo(15, 3);
            p.drawPath(magnet);
            if (icon == Icon::SnapPoints) {
                p.setBrush(color);
                p.drawRect(QRectF(16.5, 16.5, 5, 5));
            } else {
                for (double v : {17.0, 21.0}) {
                    p.drawLine(QPointF(v, 14), QPointF(v, 22));
                    p.drawLine(QPointF(14, v), QPointF(22, v));
                }
            }
            break;
        }
        case Icon::Home:
            p.drawPolyline(QPolygonF{QPointF(3, 12), QPointF(12, 4), QPointF(21, 12)});
            p.drawPolyline(QPolygonF{QPointF(6, 10), QPointF(6, 20), QPointF(18, 20), QPointF(18, 10)});
            break;
        case Icon::Pattern:
            for (double x : {3.5, 13.5})
                for (double y : {3.5, 13.5}) p.drawRect(QRectF(x, y, 7, 7));
            break;
        case Icon::Group: {
            QPen dashed = p.pen();
            dashed.setDashPattern({2.0, 1.6});
            p.setPen(dashed);
            p.drawRect(QRectF(2.5, 2.5, 19, 19));
            p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawRect(QRectF(6, 6, 5, 5));
            p.drawRect(QRectF(13, 6, 5, 5));
            p.drawRect(QRectF(9.5, 13, 5, 5));
            break;
        }
        case Icon::Gear: {
            for (int i = 0; i < 8; ++i) {
                const double a = std::numbers::pi / 4 * i;
                p.drawLine(QPointF(12 + 6.8 * std::cos(a), 12 + 6.8 * std::sin(a)), QPointF(12 + 10 * std::cos(a), 12 + 10 * std::sin(a)));
            }
            p.drawEllipse(QPointF(12, 12), 6.8, 6.8);
            p.drawEllipse(QPointF(12, 12), 2.6, 2.6);
            break;
        }
        case Icon::Coil: {
            for (int turn = 0; turn < 4; ++turn) {
                const double y = 5 + turn * 4;
                QPainterPath loop;
                loop.moveTo(4, y + 1.5);
                loop.cubicTo(4, y - 2.5, 20, y - 2.5, 20, y + 1.5);
                p.drawPath(loop);
                p.drawLine(QPointF(20, y + 1.5), QPointF(4, y + 5.5));
            }
            break;
        }
        case Icon::Star: {
            QPolygonF star;
            for (int i = 0; i < 10; ++i) {
                const double r = i % 2 == 0 ? 10.0 : 4.2;
                const double a = -std::numbers::pi / 2 + std::numbers::pi * i / 5;
                star << QPointF(12 + r * std::cos(a), 12.8 + r * std::sin(a));
            }
            p.drawPolygon(star);
            break;
        }
        case Icon::Wedge:
            p.drawPolygon(QPolygonF{QPointF(3.5, 4), QPointF(20.5, 19.5), QPointF(3.5, 19.5)});
            break;
        case Icon::Torus:
            p.drawEllipse(QPointF(12, 12), 9.5, 6.5);
            p.drawEllipse(QPointF(12, 12), 3.8, 2);
            break;
    }
}

}  // namespace

QIcon makeIcon(Icon icon, const QColor& color) {
    QPixmap pixmap(kPixels, kPixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(kPixels / kGrid, kPixels / kGrid);
    painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    draw(painter, icon, color);
    painter.end();
    return QIcon(pixmap);
}
