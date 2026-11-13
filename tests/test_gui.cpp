// Drives the real main window with synthetic input. The window is never shown,
// but Qt's offscreen platform cannot host an OpenGL widget, so a display is needed.
#include "ai/Manual.h"
#include "app/AiPanel.h"
#include "app/GuideOverlay.h"
#include "app/FeaturePanel.h"
#include "app/MainWindow.h"
#include "app/ModifyController.h"
#include "app/PatternController.h"
#include "app/PlacementController.h"
#include "app/SelectionController.h"
#include "app/StepTool.h"
#include <QLabel>
#include <QDialogButtonBox>
#include "app/TransformController.h"
#include "app/ViewCube.h"
#include "app/SnapSettings.h"
#include "app/Viewport.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QFile>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QMatrix4x4>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QPlainTextEdit>
#include <QColorDialog>
#include <QMenu>
#include <QStatusBar>
#include <QComboBox>
#include <QDialog>
#include <QDropEvent>
#include <QMimeData>
#include <QSpinBox>
#include <QImage>
#include <QTest>
#include <QTextBrowser>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <numbers>

static int failures = 0;
#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static void hoverEnter(QWidget* widget) {
    QEnterEvent event(QPointF(2, 2), QPointF(2, 2), QPointF(2, 2));
    QApplication::sendEvent(widget, &event);
}

static void hoverLeave(QWidget* widget) {
    QEvent event(QEvent::Leave);
    QApplication::sendEvent(widget, &event);
}

static QPoint toScreen(Viewport* viewport, const QVector3D& point) {
    const auto m = viewport->viewProjection();
    const QVector3D ndc = QMatrix4x4(m.data()).transposed().map(point);
    return QPoint(int((ndc.x() * 0.5 + 0.5) * viewport->width()), int((1 - (ndc.y() * 0.5 + 0.5)) * viewport->height()));
}

static void drag(Viewport* viewport, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::mousePress(viewport, Qt::LeftButton, modifiers, from);
    QTest::mouseMove(viewport, to);
    QTest::mouseRelease(viewport, Qt::LeftButton, modifiers, to);
}

static QAction* action(MainWindow& window, const char* name) { return window.findChild<QAction*>(name); }

static void shortcut(MainWindow& window, QKeySequence key) {
    for (QAction* a : window.findChildren<QAction*>())
        if (a->shortcut() == key) a->trigger();
}

static int featureCount(const MainWindow& window) {
    int count = 0;
    for (const td::Body& body : window.document().bodies) count += int(body.features.size());
    return count;
}

static void moveTo(Viewport* viewport, QPointF to, Qt::MouseButtons buttons = Qt::NoButton, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(QEvent::MouseMove, to, viewport->mapToGlobal(to), Qt::NoButton, buttons, modifiers);
    QApplication::sendEvent(viewport, &event);
}

static void pullHandle(Viewport* viewport, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent down(QEvent::MouseButtonPress, from, viewport->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(viewport, &down);
    moveTo(viewport, to, Qt::LeftButton, modifiers);
    QMouseEvent up(QEvent::MouseButtonRelease, to, viewport->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(viewport, &up);
}

static void leftDrag(Viewport* viewport, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent down(QEvent::MouseButtonPress, from, viewport->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(viewport, &down);
    moveTo(viewport, to, Qt::LeftButton, modifiers);
    QMouseEvent up(QEvent::MouseButtonRelease, to, viewport->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(viewport, &up);
}

static void rightDrag(Viewport* viewport, QPointF from, QPointF to) {
    QMouseEvent down(QEvent::MouseButtonPress, from, viewport->mapToGlobal(from), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &down);
    moveTo(viewport, to, Qt::RightButton);
    QMouseEvent up(QEvent::MouseButtonRelease, to, viewport->mapToGlobal(to), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &up);
}

static void testSelectionFlow(MainWindow& window, Viewport* viewport, FeaturePanel* panel, QListWidget* list, const char* gearShot, const char* chamferShot) {
    QTemporaryDir dir2;
    auto* selection = window.findChild<SelectionController*>();
    auto* modify = window.findChild<ModifyController*>();
    auto* gear = viewport->findChild<QToolButton*>("gearButton");
    const auto parts = [&] { return panel->selectedRows(); };
    const auto point = [&](double x, double y, double z) { return viewport->project(td::V3{x, y, z}).toPoint(); };
    const auto click = [&](QPoint p, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::mouseClick(viewport, Qt::LeftButton, m, p); };
    const auto shortcut = [&](QKeySequence key) {
        for (QAction* a : window.findChildren<QAction*>())
            if (a->shortcut() == key) a->trigger();
    };
    const QPoint empty(5, viewport->height() - 5);
    const auto leftTop = [&] { return point(-60, 0, 20); };
    const auto rightTop = [&] { return point(60, 0, 20); };

    // First click takes the whole part, with no tool switched on.
    click(leftTop());
    CHECK(parts() == std::vector<int>{0} && selection->subKind() == SubKind::None);
    CHECK(gear->isVisible());

    // Shift chains parts, and toggles one back off.
    click(rightTop(), Qt::ShiftModifier);
    CHECK((parts() == std::vector<int>{0, 1}));
    click(rightTop(), Qt::ShiftModifier);
    CHECK(parts() == std::vector<int>{0});

    // Empty space clears everything.
    click(empty);
    CHECK(parts().empty() && selection->subKind() == SubKind::None && !gear->isVisible());

    // A second click drops to what is under the pointer.
    click(leftTop());
    click(leftTop());
    CHECK(selection->subKind() == SubKind::Face && selection->subIds().size() == 1 && parts().empty());

    // Selecting a face starts the drag right away, at zero, changing nothing yet.
    CHECK(modify->active() && modify->kind() == td::ModifyKind::Extrude && modify->value() == 0 && modify->handleCount() == 1);

    // Clicking again on the same part moves the focus between sub-entities.
    click(point(-60, -10, 20));
    CHECK(selection->subKind() == SubKind::Edge && selection->subIds().size() == 1);
    const int firstEdge = selection->subIds()[0];
    click(point(-50, -10, 20));
    CHECK(selection->subKind() == SubKind::Vertex);
    click(leftTop());
    CHECK(selection->subKind() == SubKind::Face);

    // Shift chains edges and removes one when clicked again.
    click(point(-60, -10, 20));
    click(point(-50, 0, 20), Qt::ShiftModifier);
    CHECK(selection->subKind() == SubKind::Edge && selection->subIds().size() == 2);
    click(point(-50, 0, 20), Qt::ShiftModifier);
    CHECK(selection->subIds() == std::vector<int>{firstEdge});

    // The view can be rotated with a selection in place, by either button.
    const QPoint beforeRotate = point(-60, 0, 20);
    leftDrag(viewport, QPoint(700, 500), QPoint(730, 515));
    CHECK(point(-60, 0, 20) != beforeRotate);
    CHECK(selection->subIds() == std::vector<int>{firstEdge});
    const QPoint beforeRight = point(-60, 0, 20);
    rightDrag(viewport, QPoint(700, 500), QPoint(680, 490));
    CHECK(point(-60, 0, 20) != beforeRight);
    CHECK(selection->subIds() == std::vector<int>{firstEdge});

    click(empty);
    CHECK(selection->subKind() == SubKind::None);

    // The selection tool: left to right needs the whole part inside the box.
    auto* selectTool = action(window, "selectAction");
    const QPoint left = point(-60, 0, 10);
    const QPoint margin(90, 90);
    selectTool->setChecked(true);
    leftDrag(viewport, left - margin, left + margin);
    CHECK(parts() == std::vector<int>{0});

    // Having selected something, the tool lets go, so left-drag rotates again.
    CHECK(!selectTool->isChecked());
    const QPoint beforeOrbit = point(-60, 0, 10);
    leftDrag(viewport, QPoint(700, 500), QPoint(720, 510));
    CHECK(point(-60, 0, 10) != beforeOrbit);
    CHECK(parts() == std::vector<int>{0});
    click(empty);

    // A box clipping part of a shape: window misses it, crossing catches it.
    const QPoint corner = point(-60, 0, 10);
    const QPoint a = corner - QPoint(4, 4), b = corner + QPoint(4, 4);
    selectTool->setChecked(true);
    leftDrag(viewport, a, b);
    CHECK(parts().empty());
    CHECK(selectTool->isChecked());
    leftDrag(viewport, b, a);
    CHECK(parts() == std::vector<int>{0});
    CHECK(!selectTool->isChecked());
    click(empty);

    // A hotkey without the selection it needs asks for it in the step bar.
    auto* stepTool = viewport->window()->findChild<StepTool*>();
    click(leftTop());
    shortcut(QKeySequence(Qt::Key_C));
    CHECK(!modify->active() && stepTool->active() && stepTool->id() == "then_chamfer");
    shortcut(QKeySequence(Qt::Key_Escape));
    CHECK(!stepTool->active());
    shortcut(QKeySequence(Qt::Key_E));
    CHECK(!modify->active() && stepTool->active() && stepTool->id() == "then_fillet");
    shortcut(QKeySequence(Qt::Key_Escape));
    CHECK(!stepTool->active());
    click(leftTop());

    // The gear offers what fits the selection.
    const auto openGear = [&] {
        hoverEnter(gear);
        return viewport->findChild<QFrame*>("gearMenu");
    };
    QFrame* menu = openGear();
    CHECK(menu->isVisible() && menu->findChild<QToolButton*>("gear_Delete") && !menu->findChild<QToolButton*>("gear_Fillet"));
    hoverLeave(gear);
    QTest::qWait(320);
    CHECK(!menu->isVisible());

    click(leftTop());
    CHECK(selection->subKind() == SubKind::Face);
    menu = openGear();
    CHECK(menu->findChild<QToolButton*>("gear_Extrude") && menu->findChild<QToolButton*>("gear_Press Pull"));
    CHECK(menu->findChild<QToolButton*>("gear_Shell")->isEnabled() && menu->findChild<QToolButton*>("gear_Split Face")->isEnabled());
    CHECK(!menu->findChild<QToolButton*>("gear_Fillet"));
    QTest::mouseClick(menu->findChild<QToolButton*>("gear_Extrude"), Qt::LeftButton);
    CHECK(modify->active() && modify->value() == 0);

    // Pulling the arrow outward adds, shown live in the field.
    auto* field = viewport->findChild<QLineEdit*>("modifyValue");
    const QPointF grab = modify->handleGrabPoint();
    leftDrag(viewport, grab, grab - QPointF(0, 40));
    CHECK(modify->value() > 0 && modify->value() == std::round(modify->value()));
    CHECK(field->text() == QString::number(modify->value()));

    // Dragging the face itself works too, and goes back the other way.
    const double pulled = modify->value();
    const QPoint faceSpot = point(-54, 5, 20 + pulled);
    leftDrag(viewport, faceSpot, faceSpot + QPoint(0, 90));
    CHECK(modify->value() < pulled);
    CHECK(field->text() == QString::number(modify->value()));
    CHECK(parts().empty() && selection->subKind() == SubKind::Face);

    // The view still rotates with the tool up, without disturbing the edit.
    const double held = modify->value();
    const QPoint rotateCheck = point(60, 0, 0);
    leftDrag(viewport, empty - QPoint(0, 60), empty - QPoint(-30, 80));
    CHECK(point(60, 0, 0) != rotateCheck);
    CHECK(modify->active() && modify->value() == held);

    // Esc restores everything.
    shortcut(QKeySequence(Qt::Key_Escape));
    CHECK(!modify->active() && featureCount(window) == 2);

    // A negative distance pushes in, and Enter commits it.
    click(leftTop());
    click(leftTop());
    CHECK(selection->subKind() == SubKind::Face);
    CHECK(modify->active());
    QTest::keyClicks(field, "-3");
    CHECK(modify->value() == -3);
    QTest::keyClick(field, Qt::Key_Return);
    CHECK(!modify->active() && featureCount(window) == 3);
    action(window, "undoAction")->trigger();
    CHECK(featureCount(window) == 2);

    // After dragging the arrow the keys are with the 3D view, not the box: Enter still applies.
    // (It did nothing, because the window's Enter shortcut had no case for these tools.)
    click(leftTop());
    if (selection->subKind() != SubKind::Face) click(leftTop());
    CHECK(modify->active());
    {
        const QPointF arrow = modify->handleGrabPoint();
        leftDrag(viewport, arrow, arrow - QPointF(0, 40));
    }
    // A real press on the 3D view takes the keys (Viewport::mousePressEvent); the simulated
    // drag leaves them in the box, so hand them to the view as a real drag would.
    viewport->setFocus(Qt::MouseFocusReason);
    CHECK(modify->value() > 0 && window.focusWidget() == viewport);
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(!modify->active() && featureCount(window) == 3);
    action(window, "undoAction")->trigger();
    CHECK(featureCount(window) == 2);

    // Several faces move by the same distance, each with its own arrow.
    viewport->fit({-75, -15, 0}, {-45, 15, 30});
    click(leftTop());
    if (selection->subKind() != SubKind::Face) click(leftTop());
    CHECK(modify->active() && modify->handleCount() == 1);
    click(point(-60, -10, 10), Qt::ShiftModifier);
    CHECK(selection->subKind() == SubKind::Face && selection->subIds().size() == 2);
    CHECK(modify->active() && modify->handleCount() == 2);
    QTest::keyClicks(field, "4");
    CHECK(modify->value() == 4);
    if (gearShot) window.grab().save(QString::fromLocal8Bit(gearShot));

    // Shift-clicking one again drops it and keeps the distance.
    click(point(-60, -10, 10), Qt::ShiftModifier);
    CHECK(modify->handleCount() == 1 && modify->value() == 4);
    click(point(-60, -10, 10), Qt::ShiftModifier);
    CHECK(modify->handleCount() == 2 && modify->value() == 4);

    const int beforeFaces = featureCount(window);
    QTest::keyClick(field, Qt::Key_Return);
    CHECK(featureCount(window) == beforeFaces + 1 && !modify->active());
    action(window, "undoAction")->trigger();
    CHECK(featureCount(window) == beforeFaces);

    // Clicking away applies a change that was dragged in, then selects.
    click(leftTop());
    if (selection->subKind() != SubKind::Face) click(leftTop());
    QTest::keyClicks(field, "2");
    click(empty);
    CHECK(!modify->active() && featureCount(window) == beforeFaces + 1);
    action(window, "undoAction")->trigger();

    // Clicking away with nothing changed adds nothing.
    click(leftTop());
    if (selection->subKind() != SubKind::Face) click(leftTop());
    click(empty);
    CHECK(!modify->active() && featureCount(window) == beforeFaces);

    // C shows a real, grabbable arrow from any angle, and dragging it updates the box.
    click(leftTop());
    click(point(-60, -10, 20));
    click(point(-60, -10, 20));
    CHECK(selection->subKind() == SubKind::Edge);
    shortcut(QKeySequence(Qt::Key_C));
    CHECK(modify->active() && modify->kind() == td::ModifyKind::Chamfer && modify->handleCount() == 1);
    const QPointF chamferSpan = modify->handleEnd() - modify->handleStart();
    CHECK(std::hypot(chamferSpan.x(), chamferSpan.y()) > 25);
    const QPointF chamferGrab = modify->handleGrabPoint();
    leftDrag(viewport, chamferGrab, chamferGrab + chamferSpan * 0.6);
    CHECK(modify->value() > 1 && field->text() == QString::number(modify->value()));
    if (chamferShot) window.grab().save(QString::fromLocal8Bit(chamferShot));
    shortcut(QKeySequence(Qt::Key_Escape));
    CHECK(!modify->active() && featureCount(window) == beforeFaces);

    // E fillets the selected edge.
    CHECK(selection->subKind() == SubKind::Edge);
    shortcut(QKeySequence(Qt::Key_E));
    CHECK(modify->active() && modify->kind() == td::ModifyKind::Fillet && modify->handleCount() == 1);
    QTest::keyClicks(field, "3");
    CHECK(modify->value() == 3);

    // More edges share that one arrow and keep the distance.
    click(point(-50, 0, 20), Qt::ShiftModifier);
    CHECK(selection->subIds().size() == 2 && modify->handleCount() == 1 && modify->value() == 3);
    CHECK(!gear->isVisible());

    // Dragging the one arrow changes the shared distance, in small real-mouse steps.
    const QPointF oneGrab = modify->handleGrabPoint();
    const QPointF oneSpan = modify->handleEnd() - modify->handleStart();
    QMouseEvent oneDown(QEvent::MouseButtonPress, oneGrab, viewport->mapToGlobal(oneGrab), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &oneDown);
    for (int i = 1; i <= 30; ++i) moveTo(viewport, oneGrab + oneSpan * (0.5 * i / 30.0), Qt::LeftButton);
    QMouseEvent oneUp(QEvent::MouseButtonRelease, oneGrab + oneSpan * 0.5, viewport->mapToGlobal(oneGrab + oneSpan * 0.5), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &oneUp);
    CHECK(modify->value() > 3 && field->text() == QString::number(modify->value()));
    CHECK(selection->subIds().size() == 2);

    QTest::keyClick(field, Qt::Key_Return);
    CHECK(featureCount(window) == beforeFaces + 1);
    action(window, "undoAction")->trigger();
    CHECK(featureCount(window) == beforeFaces);

    // The tip of a cone can be picked and rounded with E, by typing or by dragging.
    {
        QTemporaryDir coneDir;
        const QString conePath = coneDir.filePath("cone.cubra");
        QFile coneFile(conePath);
        CHECK(coneFile.open(QIODevice::WriteOnly));
        coneFile.write(R"({"features":[{"name":"Cone","op":"add","shape":"cylinder","diameter":20,"top_diameter":0,"height":20}]})");
        coneFile.close();
        CHECK(window.openFile(conePath));
        click(point(0, 0, 8));
        CHECK(parts() == std::vector<int>{0});
        click(point(0, 0, 19.6));
        CHECK(selection->subKind() == SubKind::Vertex);
        QFrame* pointMenu = openGear();
        CHECK(pointMenu->findChild<QToolButton*>("gear_Fillet") != nullptr);
        hoverLeave(gear);
        shortcut(QKeySequence(Qt::Key_E));
        CHECK(modify->active() && modify->kind() == td::ModifyKind::PointFillet && modify->handleCount() == 1 && !gear->isVisible());
        QTest::keyClicks(field, "2");
        CHECK(modify->value() == 2);
        const QPointF tipGrab = modify->handleGrabPoint();
        const QPointF tipSpan = modify->handleEnd() - modify->handleStart();
        leftDrag(viewport, tipGrab, tipGrab + tipSpan * 0.6);
        CHECK(modify->value() != 2 && field->text() == QString::number(modify->value()));
        QTest::keyClick(field, Qt::Key_Return);
        CHECK(!modify->active() && featureCount(window) == 2);

        // After rounding, the view keeps turning and the part can be selected as usual.
        click(point(0, 0, 8));
        CHECK(parts() == std::vector<int>{0});
        action(window, "undoAction")->trigger();
        CHECK(featureCount(window) == 1);
    }
    const QString twoPath = dir2.filePath("two.cubra");
    QFile twoFile(twoPath);
    CHECK(twoFile.open(QIODevice::WriteOnly));
    twoFile.write(R"({"features":[
        {"name":"Left","op":"add","shape":"box","width":20,"depth":20,"height":20,"x":-60},
        {"name":"Right","op":"add","shape":"box","width":20,"depth":20,"height":20,"x":60}]})");
    twoFile.close();
    CHECK(window.openFile(twoPath));
    viewport->fit({-70, -10, 0}, {-50, 10, 20});
    CHECK(featureCount(window) == 2);
    click(empty);

    // Edge tools with typed values, each one undo step.
    click(leftTop());
    click(point(-60, -10, 20));
    click(point(-60, -10, 20));
    CHECK(selection->subKind() == SubKind::Edge);
    shortcut(QKeySequence(Qt::Key_C));
    CHECK(modify->active());
    QTest::keyClicks(field, "2");
    CHECK(modify->value() == 2);
    QTest::keyClick(field, Qt::Key_Return);
    CHECK(featureCount(window) == 3);
    action(window, "undoAction")->trigger();

    viewport->fit({-70, -10, 0}, {-50, 10, 20});
    click(leftTop());
    click(point(-60, -10, 20));
    click(point(-60, -10, 20));
    shortcut(QKeySequence(Qt::Key_F));
    CHECK(modify->active());
    QTest::keyClicks(field, "3");
    QTest::keyClick(field, Qt::Key_Return);
    CHECK(featureCount(window) == 3);
    action(window, "undoAction")->trigger();

    // F still fits the view when no edges are selected.
    click(empty);
    shortcut(QKeySequence(Qt::Key_F));
    CHECK(!modify->active());

    // Delete from the gear removes the part.
    click(leftTop());
    menu = openGear();
    QTest::mouseClick(menu->findChild<QToolButton*>("gear_Delete"), Qt::LeftButton);
    CHECK(featureCount(window) == 1);
    action(window, "undoAction")->trigger();
    CHECK(featureCount(window) == 2);
}

template <typename Choose, typename Hud, typename Press>
static void testPlacementFlow(MainWindow& window, Viewport* viewport, PlacementController* controller, QListWidget* list, Choose choose, Hud hud,
                              Press press, const char* screenshot) {
    using td::Feature;
    const auto escape = [&] {
        for (QAction* a : window.actions())
            if (a->shortcut() == QKeySequence(Qt::Key_Escape)) a->trigger();
    };
    const auto point = [&](double x, double y, double z) { return viewport->project(td::V3{x, y, z}); };
    const auto dwell = [] { QTest::qWait(450); };

    QTemporaryDir dir;
    const QString path = dir.filePath("cube.cubra");
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(R"({"features":[{"name":"Cube","op":"add","shape":"box","width":40,"depth":40,"height":40}]})");
    file.close();
    CHECK(window.openFile(path));
    CHECK(featureCount(window) == 1);

    // Esc leaves nothing behind.
    choose("primitive_box");
    CHECK(controller->active() && viewport->findChild<QFrame*>("placementHud")->isVisible());
    escape();
    CHECK(!controller->active() && featureCount(window) == 1);
    CHECK(!viewport->findChild<QFrame*>("placementHud")->isVisible());

    // On the grid the base sits at z=0 on whole millimetres.
    choose("primitive_box");
    moveTo(viewport, point(-70.3, 40.2, 0));
    CHECK(controller->feature().z == 0 && std::abs(controller->feature().x + 70) < 1.01 && controller->feature().x == std::round(controller->feature().x));
    CHECK(controller->feature().rx == 0 && controller->feature().ry == 0);

    // Over a body it seats on the top face.
    moveTo(viewport, point(0, 0, 40));
    CHECK(std::abs(controller->feature().z - 40) < 0.01 && controller->feature().rx == 0 && controller->feature().ry == 0);

    // Over a side face it turns to stand on that face.
    moveTo(viewport, point(0, -20, 20));
    CHECK(std::abs(controller->feature().y + 20) < 0.01 && std::abs(controller->feature().rx - 90) < 0.01);

    // Typing replaces the highlighted value and changes the shape live.
    moveTo(viewport, point(0, 0, 40));
    CHECK(window.focusWidget() == hud("Length"));
    QTest::keyClicks(hud("Length"), "35");
    CHECK(hud("Length")->text() == "35");
    CHECK(controller->feature().width == 35);
    CHECK(controller->settled());

    // Tab walks the fields and wraps.
    press(Qt::Key_Tab);
    CHECK(window.focusWidget() == hud("Width") && hud("Width")->selectedText() == hud("Width")->text());
    press(Qt::Key_Tab);
    CHECK(window.focusWidget() == hud("Height"));
    press(Qt::Key_Tab);
    CHECK(window.focusWidget() == hud("Length"));

    // A bad value is flagged and blocks Enter.
    QTest::keyClicks(hud("Length"), "0");
    CHECK(controller->feature().width == 35);
    press(Qt::Key_Return);
    CHECK(controller->active() && featureCount(window) == 1);
    hud("Length")->selectAll();
    QTest::keyClicks(hud("Length"), "22.5");
    CHECK(controller->feature().width == 22.5);

    // Enter bakes it at the seated position, as one undo step.
    press(Qt::Key_Return);
    CHECK(!controller->active() && featureCount(window) == 2);
    action(window, "undoAction")->trigger();
    CHECK(featureCount(window) == 1);
    action(window, "redoAction")->trigger();
    CHECK(featureCount(window) == 2);
    action(window, "undoAction")->trigger();

    // The ghost stays attached until the pointer rests, then arrows can be reached.
    choose("primitive_box");
    moveTo(viewport, point(70, 0, 0));
    CHECK(!controller->settled());
    const auto seatedX = controller->feature().x;
    dwell();
    CHECK(controller->settled());

    int heightHandle = -1, lengthHandle = -1;
    for (int i = 0; i < controller->handleCount(); ++i) {
        if (controller->handleParam(i) == td::Param::Height) heightHandle = i;
        if (controller->handleParam(i) == td::Param::Length && lengthHandle < 0) lengthHandle = i;
    }
    CHECK(controller->handleCount() == 5 && heightHandle >= 0 && lengthHandle >= 0);

    // Moving onto an arrow leaves the ghost where it is.
    const QPointF grab = controller->handleGrabPoint(heightHandle);
    moveTo(viewport, grab);
    CHECK(controller->feature().x == seatedX && controller->settled());
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot));

    // Pulling the top arrow changes only height, in whole millimetres, and shows it live.
    pullHandle(viewport, grab, grab - QPointF(0, 60));
    double height = controller->feature().height;
    CHECK(height > 25 && height == std::round(height));
    CHECK(hud("Height")->text() == QString::number(height));
    CHECK(window.focusWidget() == hud("Height"));
    CHECK(controller->feature().width == 20 && controller->feature().depth == 20);
    CHECK(featureCount(window) == 1 && controller->active());

    // Shift scales every dimension together.
    const QPointF grab2 = controller->handleGrabPoint(heightHandle);
    pullHandle(viewport, grab2, grab2 - QPointF(0, 40), Qt::ShiftModifier);
    const Feature& scaled = controller->feature();
    const double ratio = scaled.height / height;
    CHECK(ratio > 1.1 && std::abs(scaled.width - 20 * ratio) < 0.02 && std::abs(scaled.depth - 20 * ratio) < 0.02);

    // With snapping off, values are free.
    auto* snap = viewport->findChild<QCheckBox*>("hudSnap");
    snap->setChecked(false);
    const QPointF grab3 = controller->handleGrabPoint(heightHandle);
    pullHandle(viewport, grab3, grab3 - QPointF(0, 17));
    CHECK(controller->feature().height != std::round(controller->feature().height));
    snap->setChecked(true);

    // Dragging down cannot collapse the shape.
    const QPointF grab4 = controller->handleGrabPoint(heightHandle);
    pullHandle(viewport, grab4, grab4 + QPointF(0, 3000));
    CHECK(controller->feature().height >= 1);

    // Leaving the area re-attaches the ghost.
    moveTo(viewport, point(-90, -90, 0));
    CHECK(!controller->settled() && controller->feature().x < 0);

    // A left click places it there.
    const QPointF spot = point(-80, -80, 0);
    moveTo(viewport, spot);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, spot.toPoint());
    CHECK(!controller->active() && featureCount(window) == 2);
    action(window, "undoAction")->trigger();

    // The torus fields follow the spec and respect its limits.
    choose("primitive_torus");
    CHECK(hud("Major Radius") && hud("Minor Radius"));
    QTest::keyClicks(hud("Major Radius"), "2");
    CHECK(hud("Major Radius")->property("valid").toBool() == false);
    hud("Major Radius")->selectAll();
    QTest::keyClicks(hud("Major Radius"), "30");
    CHECK(hud("Major Radius")->property("valid").toBool() == true);
    CHECK(std::abs(td::paramValue(controller->feature(), td::Param::MajorRadius) - 30) < 0.01);
    escape();

    choose("primitive_cone");
    CHECK(hud("Radius") && hud("Top Radius") && hud("Height"));
    escape();
    choose("primitive_sphere");
    CHECK(hud("Radius") && !hud("Height"));
    escape();
    choose("primitive_cylinder");
    CHECK(hud("Radius") && hud("Height") && !hud("Top Radius"));
    escape();
    choose("primitive_wedge");
    CHECK(hud("Length") && hud("Width") && hud("Height"));
    escape();
    CHECK(featureCount(window) == 1);
}

static void testBodiesFlow(MainWindow& window, Viewport* viewport, FeaturePanel* panel, const char* screenshot) {
    using td::V3;
    auto* selection = window.findChild<SelectionController*>();
    auto* transform = window.findChild<TransformController*>();
    auto* steps = window.findChild<StepTool*>();
    const auto& doc = window.document();
    const auto point = [&](double x, double y, double z) { return viewport->project(V3{x, y, z}).toPoint(); };
    const auto click = [&](QPoint p, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::mouseClick(viewport, Qt::LeftButton, m, p); };
    const auto doubleClick = [&](QPoint p) { QTest::mouseDClick(viewport, Qt::LeftButton, Qt::NoModifier, p); };
    const auto shortcut = [&](QKeySequence key) {
        for (QAction* a : window.findChildren<QAction*>())
            if (a->shortcut() == key) a->trigger();
    };
    const auto menu = [&](const char* id) {
        auto* item = window.findChild<QToolButton*>(id);
        CHECK(item != nullptr);
        if (item) QTest::mouseClick(item, Qt::LeftButton);
    };
    const auto type = [&](const char* field, const char* text) {
        auto* edit = viewport->findChild<QLineEdit*>(QStringLiteral("transform_") + QLatin1String(field));
        CHECK(edit != nullptr);
        if (!edit) return;
        edit->selectAll();
        QTest::keyClicks(edit, text);
    };
    const auto parts = [&] { return selection->selectedBodies(); };
    const QPoint empty(5, viewport->height() - 5);
    const auto close = [](V3 a, V3 b) { return td::length(a - b) < 0.01; };

    QTemporaryDir dir;
    const QString path = dir.filePath("bodies.cubra");
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(R"({"features":[
        {"name":"Block","shape":"box","width":30,"depth":30,"height":10,"x":-40},
        {"name":"Peg","shape":"cylinder","diameter":10,"height":30,"x":-40,"z":-5},
        {"name":"Cube","shape":"box","width":10,"depth":10,"height":10,"x":40},
        {"name":"Slicer","shape":"box","width":10,"depth":10,"height":10,"x":-40,"y":50}]})");
    file.close();
    CHECK(window.openFile(path));
    CHECK(doc.bodies.size() == 4);
    viewport->fit({-60, -20, -5}, {50, 60, 25});
    click(empty);

    // ---- Move / Rotate: typed values ----
    click(point(40, 0, 10));
    CHECK(parts() == std::vector<int>{2});
    shortcut(QKeySequence(Qt::Key_M));
    CHECK(transform->active() && transform->mode() == TransformMode::MoveRotate && transform->handleCount() == 6);
    type("Move X", "10");
    QTest::keyClick(viewport->findChild<QLineEdit*>("transform_Move X"), Qt::Key_Return);
    CHECK(!transform->active() && close(doc.bodies[2].transform.point(V3{}), V3{10, 0, 0}));
    CHECK(parts() == std::vector<int>{2});
    action(window, "undoAction")->trigger();
    CHECK(doc.bodies[2].transform.isIdentity());

    // Dragging the Z arrow lifts it in whole millimetres; Esc puts it back.
    click(point(40, 0, 10));
    shortcut(QKeySequence(Qt::Key_M));
    int up = -1, turnZ = -1;
    for (int i = 0; i < transform->handleCount(); ++i) {
        if (transform->handleName(i) == "Move Z") up = i;
        if (transform->handleName(i) == "Rotate Z") turnZ = i;
    }
    CHECK(up >= 0 && turnZ >= 0);
    const QPointF lift = transform->handleGrabPoint(up);
    leftDrag(viewport, lift, lift - QPointF(0, 40));
    const V3 lifted = transform->current().point(V3{});
    CHECK(lifted.z > 0 && lifted.z == std::round(lifted.z) && lifted.x == 0);
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot));
    shortcut(QKeySequence(Qt::Key_Escape));
    CHECK(!transform->active() && doc.bodies[2].transform.isIdentity());

    // Dragging the X arrow keeps the part under the pointer, step after step,
    // never falling behind or springing back.
    {
        shortcut(QKeySequence(Qt::Key_M));
        int across = -1;
        for (int i = 0; i < transform->handleCount(); ++i)
            if (transform->handleName(i) == "Move X") across = i;
        const QPointF from = transform->handleGrabPoint(across);
        const QPointF to = from + (viewport->project(V3{80, 0, 5}) - viewport->project(V3{40, 0, 5}));
        QMouseEvent press(QEvent::MouseButtonPress, from, viewport->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &press);
        double previous = 0;
        bool steady = true;
        for (int i = 1; i <= 40; ++i) {
            moveTo(viewport, from + (to - from) * (i / 40.0), Qt::LeftButton);
            const double x = transform->current().point(V3{}).x;
            steady &= x >= previous && std::abs(x - i) <= 1.01;  // one millimetre per step, rounded
            previous = x;
        }
        CHECK(steady && std::abs(previous - 40) <= 1.01);
        QMouseEvent release(QEvent::MouseButtonRelease, to, viewport->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &release);
        shortcut(QKeySequence(Qt::Key_Escape));
    }

    // Dragging a ring turns it in 15 degree steps.
    shortcut(QKeySequence(Qt::Key_M));
    const QPointF ring = transform->handleGrabPoint(turnZ);
    leftDrag(viewport, ring, ring + QPointF(40, 40));
    const V3 turned = transform->current().direction(V3{1, 0, 0});
    const double degrees = std::atan2(turned.y, turned.x) * 180 / std::numbers::pi;
    CHECK(std::abs(degrees) > 1 && std::abs(std::fmod(std::abs(degrees) + 0.001, 15.0)) < 0.01);
    shortcut(QKeySequence(Qt::Key_Escape));

    // A typed turn, applied by clicking away.
    shortcut(QKeySequence(Qt::Key_M));
    type("Rotate Z", "90");
    click(empty);
    CHECK(!transform->active() && close(doc.bodies[2].transform.direction(V3{1, 0, 0}), V3{0, 1, 0}));
    CHECK(close(doc.bodies[2].transform.point(V3{40, 0, 5}), V3{40, 0, 5}));  // turned about its own centre
    action(window, "undoAction")->trigger();

    // ---- Scale, keeping the part on the bed ----
    click(point(40, 0, 10));
    shortcut(QKeySequence(Qt::Key_S));
    CHECK(transform->active() && transform->mode() == TransformMode::Scale);
    type("Scale", "2");
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(!transform->active() && std::abs(td::length(doc.bodies[2].transform.column(0)) - 2) < 1e-9);
    CHECK(close(doc.bodies[2].transform.point(V3{40, 0, 0}), V3{40, 0, 0}));
    action(window, "undoAction")->trigger();

    // Smart Scale sets the outer size directly; Non-Uniform scales one axis.
    click(point(40, 0, 10));
    menu("menu_smart");
    CHECK(transform->active() && transform->mode() == TransformMode::SmartScale);
    type("Length", "30");
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(std::abs(doc.bodies[2].transform.column(0).x - 3) < 1e-9 && std::abs(doc.bodies[2].transform.column(2).z - 1) < 1e-9);
    action(window, "undoAction")->trigger();
    click(point(40, 0, 10));
    menu("menu_nonuniform");
    type("Scale Z", "0.5");
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(std::abs(doc.bodies[2].transform.column(2).z - 0.5) < 1e-9 && std::abs(doc.bodies[2].transform.column(0).x - 1) < 1e-9);
    action(window, "undoAction")->trigger();

    // With nothing selected, Move waits for a click on the solid.
    click(empty);
    shortcut(QKeySequence(Qt::Key_M));
    CHECK(!transform->active());
    click(point(40, 0, 10));
    CHECK(transform->active() && parts() == std::vector<int>{2});
    shortcut(QKeySequence(Qt::Key_Escape));
    click(empty);

    // ---- Combine ----
    // Merge with a selection made first: the bar comes up filled in, Enter applies.
    click(point(-40, -8, 10));
    click(point(-40, 0, 25), Qt::ShiftModifier);
    CHECK((parts() == std::vector<int>{0, 1}));
    menu("menu_merge");
    CHECK(steps->active() && steps->picks()[0].size() == 1 && steps->picks()[1].size() == 1);
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(!steps->active() && doc.bodies.size() == 3 && doc.bodies[0].name == "Block" && doc.bodies[0].features.size() == 2);
    CHECK(doc.bodies[0].features[1].shape == td::Shape::Compound && doc.bodies[0].features[1].op == td::Op::Add);
    action(window, "undoAction")->trigger();
    CHECK(doc.bodies.size() == 4);

    // Subtract picked by clicking: the solid to keep, then the cutter, then Apply.
    click(empty);
    menu("menu_subtract");
    CHECK(steps->active());
    click(point(-40, -8, 10));
    click(point(-40, 0, 25));
    CHECK(steps->picks()[0].size() == 1 && steps->picks()[0][0].body == 0 && steps->picks()[1].size() == 1 && steps->picks()[1][0].body == 1);
    QTest::mouseClick(viewport->findChild<QPushButton*>("stepApply"), Qt::LeftButton);
    CHECK(doc.bodies.size() == 3 && doc.bodies[0].features.back().op == td::Op::Subtract);
    action(window, "undoAction")->trigger();

    // Intersect keeps only the overlap; Esc abandons a half-made pick.
    menu("menu_intersect");
    click(point(-40, -8, 10));
    shortcut(QKeySequence(Qt::Key_Escape));
    CHECK(!steps->active() && doc.bodies.size() == 4);
    menu("menu_intersect");
    click(point(-40, -8, 10));
    click(point(-40, 0, 25));
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(doc.bodies.size() == 3 && doc.bodies[0].features.back().op == td::Op::Intersect);
    action(window, "undoAction")->trigger();

    // Separate gives each loose piece its own body again.
    click(empty);
    click(point(-40, -8, 10));
    click(point(40, 0, 10), Qt::ShiftModifier);
    menu("menu_merge");
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(doc.bodies.size() == 3);
    const int merged = parts().empty() ? -1 : parts().front();
    CHECK(merged >= 0);
    menu("menu_separate");
    CHECK(doc.bodies.size() == 4 && doc.bodies[0].features[0].shape == td::Shape::Mesh);
    action(window, "undoAction")->trigger();
    action(window, "undoAction")->trigger();
    CHECK(doc.bodies.size() == 4 && doc.bodies[0].features.size() == 1);

    // ---- Split Solid along another part's flat face ----
    click(empty);
    menu("menu_splitsolid");
    click(point(-40, -8, 10));
    click(point(-35, 50, 5));  // the slicer's side, in the plane x = -35
    CHECK(!steps->active() && doc.bodies.size() == 5);
    CHECK(doc.bodies[1].name == "Block (split)");
    action(window, "undoAction")->trigger();
    CHECK(doc.bodies.size() == 4);

    // ---- Snap: the cube's top face onto the block's top face ----
    click(empty);
    menu("menu_snap");
    click(point(40, 0, 10));
    click(point(-40, -8, 10));
    CHECK(!steps->active());
    CHECK(close(doc.bodies[2].transform.point(V3{40, 0, 10}), V3{-40, 0, 10}));
    CHECK(close(doc.bodies[2].transform.direction(V3{0, 0, 1}), V3{0, 0, -1}));
    action(window, "undoAction")->trigger();

    // ---- Groups ----
    click(point(-40, -8, 10));
    click(point(40, 0, 10), Qt::ShiftModifier);
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    CHECK(!doc.bodies[0].groups.empty() && doc.bodies[0].groups == doc.bodies[2].groups);
    click(empty);

    // One click takes the whole group; a second keeps it whole.
    click(point(40, 0, 10));
    CHECK((parts() == std::vector<int>{0, 2}));
    click(point(40, 0, 10));
    CHECK((parts() == std::vector<int>{0, 2}) && selection->subKind() == SubKind::None);

    // The group moves as one.
    shortcut(QKeySequence(Qt::Key_M));
    type("Move Y", "5");
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(close(doc.bodies[0].transform.point(V3{}), V3{0, 5, 0}) && close(doc.bodies[2].transform.point(V3{}), V3{0, 5, 0}));
    CHECK(doc.bodies[1].transform.isIdentity());
    action(window, "undoAction")->trigger();

    // Double-click steps inside to work on one member; Esc steps back out.
    click(empty);
    doubleClick(point(40, 0, 10));
    CHECK(!selection->groupContext().empty() && parts() == std::vector<int>{2});
    click(point(-40, -8, 10));
    CHECK(parts() == std::vector<int>{0});
    shortcut(QKeySequence(Qt::Key_Escape));
    CHECK(selection->groupContext().empty() && (parts() == std::vector<int>{0, 2}));

    // Nesting, then Ungroup and Ungroup All.
    click(point(-40, 0, 25), Qt::ShiftModifier);
    CHECK((parts() == std::vector<int>{0, 1, 2}));
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    CHECK(doc.bodies[0].groups.size() == 2 && doc.bodies[1].groups.size() == 1);
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    CHECK(doc.bodies[0].groups.size() == 1 && doc.bodies[1].groups.empty());
    click(empty);
    click(point(40, 0, 10));
    menu("menu_ungroupall");
    for (const td::Body& b : doc.bodies) CHECK(b.groups.empty());

    // Saved and reopened, bodies keep their placement and groups.
    click(point(-40, -8, 10));
    click(point(40, 0, 10), Qt::ShiftModifier);
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    shortcut(QKeySequence(Qt::Key_M));
    type("Move Z", "3");
    shortcut(QKeySequence(Qt::Key_Return));
    const QString saved = dir.filePath("saved.cubra");
    QFile out(saved);
    CHECK(out.open(QIODevice::WriteOnly));
    out.write(td::documentToJson(doc));
    out.close();
    CHECK(window.openFile(saved));
    CHECK(doc.bodies.size() == 4 && !doc.bodies[0].groups.empty() && close(doc.bodies[2].transform.point(V3{}), V3{0, 0, 3}));
}

static void testPatternFlow(MainWindow& window, Viewport* viewport, const char* screenshot) {
    using td::V3;
    auto* steps = window.findChild<StepTool*>();
    auto* pattern = window.findChild<PatternController*>();
    const auto& doc = window.document();
    const auto point = [&](double x, double y, double z) { return viewport->project(V3{x, y, z}).toPoint(); };
    const auto click = [&](QPoint p, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::mouseClick(viewport, Qt::LeftButton, m, p); };
    const auto shortcut = [&](QKeySequence key) {
        for (QAction* a : window.findChildren<QAction*>())
            if (a->shortcut() == key) a->trigger();
    };
    const auto menu = [&](const char* id) {
        auto* item = window.findChild<QToolButton*>(id);
        CHECK(item != nullptr);
        if (item) QTest::mouseClick(item, Qt::LeftButton);
    };
    const auto type = [&](const char* field, const char* text) {
        auto* edit = viewport->findChild<QLineEdit*>(QStringLiteral("pattern_") + QString::fromUtf8(field));
        CHECK(edit != nullptr);
        if (!edit) return;
        edit->selectAll();
        QTest::keyClicks(edit, text);
    };
    const auto close = [](V3 a, V3 b) { return td::length(a - b) < 0.01; };
    const QPoint empty(5, viewport->height() - 5);

    QTemporaryDir dir;
    const QString path = dir.filePath("pattern.cubra");
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(R"({"features":[
        {"name":"Peg","shape":"cylinder","diameter":6,"height":10,"x":-40},
        {"name":"Plate","shape":"box","width":40,"depth":20,"height":4,"x":40}]})");
    file.close();
    CHECK(window.openFile(path));
    viewport->fit({-60, -30, 0}, {70, 30, 15});
    click(empty);

    // Rectangular: 4 along and 2 across, 20 mm apart along the default X direction.
    click(point(-40, 0, 10));
    menu("menu_rectpattern");
    CHECK(steps->active() && pattern->active() && steps->picks()[0].size() == 1);
    type("Copies along", "4");
    type("Copies across", "2");
    type("Distance across", "15");
    CHECK(pattern->handleCount() == 2);
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot));
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(!steps->active() && !pattern->active() && doc.bodies.size() == 2 + 7);
    CHECK(close(doc.bodies.back().transform.point(V3{}), V3{60, 15, 0}));
    CHECK(doc.bodies[2].name == "Peg 2");
    window.findChild<QAction*>("undoAction")->trigger();
    CHECK(doc.bodies.size() == 2);

    // Dragging the arrow sets the spacing, in whole millimetres.
    click(empty);
    click(point(-40, 0, 10));
    menu("menu_rectpattern");
    type("Copies along", "3");
    type("Copies across", "1");  // the box remembers the 2 from last time
    CHECK(pattern->handleCount() == 1);
    const QPointF grab = pattern->handleGrabPoint(0);
    const QPointF farther = viewport->project(V3{-40 + 70, 0, 5});
    QMouseEvent down(QEvent::MouseButtonPress, grab, viewport->mapToGlobal(grab), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &down);
    for (int i = 1; i <= 10; ++i) moveTo(viewport, grab + (farther - grab) * (i / 10.0), Qt::LeftButton);
    QMouseEvent up(QEvent::MouseButtonRelease, farther, viewport->mapToGlobal(farther), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &up);
    const double spacing = pattern->rectangular.spacingAlong;
    CHECK(spacing > 25 && spacing == std::round(spacing));
    CHECK(viewport->findChild<QLineEdit*>("pattern_Distance along")->text() == QString::number(spacing));
    shortcut(QKeySequence(Qt::Key_Escape));
    CHECK(!steps->active() && doc.bodies.size() == 2);

    // Circular, about the default upright axis through the bed centre: 4 make quarter turns.
    click(empty);
    click(point(-40, 0, 10));
    menu("menu_circpattern");
    type("Copies", "4");
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(doc.bodies.size() == 5 && close(doc.bodies[2].transform.point(V3{-40, 0, 0}), V3{0, -40, 0}));
    window.findChild<QAction*>("undoAction")->trigger();

    // Circular about a picked axis: the plate's round... here, a straight edge of it.
    click(empty);
    click(point(-40, 0, 10));
    menu("menu_circpattern");
    click(point(40, -10, 4));  // the plate's long top front edge, along X
    CHECK(steps->picks()[1][0].body == 1);
    type("Copies", "2");
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(doc.bodies.size() == 3);
    // A half turn about a line along X at y = -10, z = 4.
    CHECK(close(doc.bodies[2].transform.point(V3{-40, 0, 0}), V3{-40, -20, 8}));
    window.findChild<QAction*>("undoAction")->trigger();

    // Path: copies along the plate's top front edge, a 40 mm path.
    click(empty);
    click(point(-40, 0, 10));
    menu("menu_pathpattern");
    type("Copies", "3");
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(steps->active());  // no path picked yet
    click(point(40, -10, 4));
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(doc.bodies.size() == 4);
    const V3 step1 = doc.bodies[2].transform.point(V3{}), step2 = doc.bodies[3].transform.point(V3{});
    CHECK(std::abs(td::length(step1) - 20) < 0.01 && std::abs(td::length(step2) - 40) < 0.01 && std::abs(step1.y) < 1e-9);
    window.findChild<QAction*>("undoAction")->trigger();

    // Mirror across the plate's front face: one flipped copy, a body of its own.
    click(empty);
    click(point(-40, 0, 10));
    menu("menu_mirror");
    CHECK(viewport->findChild<QFrame*>("patternHud")->isHidden());
    click(point(40, -10, 2));  // the plate's front, the plane y = -10
    CHECK(steps->active() && steps->picks()[1].size() == 1);
    shortcut(QKeySequence(Qt::Key_Return));
    CHECK(doc.bodies.size() == 3 && close(doc.bodies[2].transform.point(V3{-40, 0, 0}), V3{-40, -20, 0}));
    CHECK(doc.bodies[2].groups.empty() && doc.bodies[0].features.size() == 1);
    window.findChild<QAction*>("undoAction")->trigger();
}

static void testContextMenuFlow(MainWindow& window, Viewport* viewport, const char* screenshot) {
    using td::V3;
    auto* selection = window.findChild<SelectionController*>();
    auto* modify = window.findChild<ModifyController*>();
    const auto& doc = window.document();
    const auto point = [&](double x, double y, double z) { return viewport->project(V3{x, y, z}).toPoint(); };
    const auto click = [&](QPoint p, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::mouseClick(viewport, Qt::LeftButton, m, p); };
    const auto parts = [&] { return selection->selectedBodies(); };
    const auto close = [](V3 a, V3 b) { return td::length(a - b) < 0.01; };
    const QPoint empty(5, viewport->height() - 5);
    const auto settle = [] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); };
    // Right-click and return the menu that opened, or null.
    const auto menuAt = [&](QPoint p) -> QMenu* {
        settle();
        QTest::mouseClick(viewport, Qt::RightButton, Qt::NoModifier, p);
        const auto menus = window.findChildren<QMenu*>("contextMenu");
        return menus.isEmpty() ? nullptr : menus.last();
    };
    const auto item = [](QMenu* menu, const char* id) { return menu ? menu->findChild<QAction*>(id) : nullptr; };
    const auto choose = [&](QMenu* menu, const char* id) {
        QAction* a = item(menu, id);
        CHECK(a && a->isEnabled());
        if (a) a->trigger();
        if (menu) menu->close();
        settle();
    };
    const auto dismiss = [&](QMenu* menu) {
        if (menu) menu->close();
        settle();
    };

    QTemporaryDir dir;
    const QString path = dir.filePath("menus.cubra");
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(R"({"features":[
        {"name":"Peg","shape":"cylinder","diameter":10,"height":10,"x":-40,"z":5},
        {"name":"Plate","shape":"box","width":40,"depth":20,"height":4,"x":40}]})");
    file.close();
    CHECK(window.openFile(path));
    viewport->fit({-60, -30, 0}, {70, 30, 20});
    click(empty);

    // Empty space: clipboard, visibility and camera items, nothing for solids.
    QMenu* menu = menuAt(empty);
    CHECK(menu && item(menu, "ctx_paste") && !item(menu, "ctx_paste")->isEnabled());
    CHECK(item(menu, "ctx_showall") && !item(menu, "ctx_showall")->isEnabled() && item(menu, "ctx_fit"));
    CHECK(item(menu, "ctx_orbit") && item(menu, "ctx_orbit")->isChecked() && !item(menu, "ctx_move"));
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot));
    choose(menu, "ctx_pan");
    CHECK(viewport->navigation() == Viewport::Navigation::Pan);
    const QPoint beforePan = point(0, 0, 0);
    leftDrag(viewport, QPoint(600, 600), QPoint(650, 600));
    CHECK(point(0, 0, 0).x() - beforePan.x() > 40 && std::abs(point(0, 0, 0).y() - beforePan.y()) < 3);  // slid, not turned
    choose(menuAt(empty), "ctx_orbit");
    CHECK(viewport->navigation() == Viewport::Navigation::Orbit);

    // Right-drag still turns the view and opens no menu.
    const int menusBefore = int(window.findChildren<QMenu*>("contextMenu").size());
    const QPoint beforeTurn = point(40, 0, 0);
    QMouseEvent down(QEvent::MouseButtonPress, QPointF(600, 500), viewport->mapToGlobal(QPointF(600, 500)), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &down);
    moveTo(viewport, QPointF(640, 520), Qt::RightButton);
    moveTo(viewport, QPointF(680, 540), Qt::RightButton);
    QMouseEvent up(QEvent::MouseButtonRelease, QPointF(680, 540), viewport->mapToGlobal(QPointF(680, 540)), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &up);
    settle();
    CHECK(int(window.findChildren<QMenu*>("contextMenu").size()) == menusBefore && point(40, 0, 0) != beforeTurn);

    // A solid: right-click selects it and offers the part tools.
    const auto peg = [&] { return point(doc.bodies[0].transform.point(V3{-40, 0, 15}).x, doc.bodies[0].transform.point(V3{-40, 0, 15}).y, doc.bodies[0].transform.point(V3{-40, 0, 15}).z); };
    menu = menuAt(peg());
    CHECK(parts() == std::vector<int>{0});
    for (const char* id : {"ctx_move", "ctx_scale", "ctx_drop", "ctx_cut", "ctx_copy", "ctx_delete", "ctx_hide", "ctx_material"}) CHECK(item(menu, id) != nullptr);
    CHECK(!item(menu, "ctx_group") && !item(menu, "ctx_ungroup"));

    // Drop: the floating peg comes down onto the bed.
    choose(menu, "ctx_drop");
    CHECK(close(doc.bodies[0].transform.point(V3{-40, 0, 5}), V3{-40, 0, 0}));

    // Repeat brings back the last tool.
    menu = menuAt(empty);
    CHECK(item(menu, "ctx_repeat") && item(menu, "ctx_repeat")->text().contains("Drop"));
    dismiss(menu);

    // Copy, then paste at a spot on the bed.
    choose(menuAt(peg()), "ctx_copy");
    const QPoint spot = point(0, 20, 0);
    choose(menuAt(spot), "ctx_paste");
    CHECK(doc.bodies.size() == 3 && doc.bodies[2].name == "Peg 2");
    const V3 pasted = doc.bodies[2].transform.point(V3{-40, 0, 5});  // the peg's base, in its own space
    CHECK(std::abs(pasted.x) <= 1.01 && std::abs(pasted.y - 20) <= 1.01 && std::abs(pasted.z) < 1e-9);
    CHECK(parts() == std::vector<int>{2});

    // Cut takes it out; Ctrl+V from the keyboard puts it back beside where it was.
    for (QAction* a : window.findChildren<QAction*>())
        if (a->shortcut() == QKeySequence(QKeySequence::Cut)) a->trigger();
    CHECK(doc.bodies.size() == 2);
    for (QAction* a : window.findChildren<QAction*>())
        if (a->shortcut() == QKeySequence(QKeySequence::Paste)) a->trigger();
    CHECK(doc.bodies.size() == 3);
    window.findChild<QAction*>("undoAction")->trigger();
    CHECK(doc.bodies.size() == 2);

    // Hide: gone from view and from clicks, still counted; Show All brings it back.
    choose(menuAt(peg()), "ctx_hide");
    CHECK(doc.bodies[0].hidden && parts().empty());
    menu = menuAt(peg());
    CHECK(menu && !item(menu, "ctx_move") && item(menu, "ctx_showall")->isEnabled());
    choose(menu, "ctx_showall");
    CHECK(!doc.bodies[0].hidden);

    // Material: a colour of the part's own.
    choose(menuAt(peg()), "ctx_material");
    auto* dialog = window.findChild<QColorDialog*>("materialDialog");
    CHECK(dialog != nullptr);
    if (dialog) {
        dialog->setCurrentColor(QColor(200, 30, 40));
        dialog->accept();
    }
    settle();
    CHECK(doc.bodies[0].color == 0xC81E28);

    // Two solids selected: Group; then a group: Ungroup.
    click(empty);
    click(peg());
    click(point(40, 0, 4), Qt::ShiftModifier);
    choose(menuAt(point(40, 0, 4)), "ctx_group");
    CHECK(!doc.bodies[0].groups.empty() && doc.bodies[0].groups == doc.bodies[1].groups);
    choose(menuAt(point(40, 0, 4)), "ctx_ungroup");
    CHECK(doc.bodies[0].groups.empty());

    // A face: its own tools, and Measure.
    click(empty);
    click(point(40, 0, 4));
    click(point(40, 0, 4));
    CHECK(selection->subKind() == SubKind::Face);
    menu = menuAt(point(40, 0, 4));
    CHECK(item(menu, "ctx_presspull") && item(menu, "ctx_measure") && !item(menu, "ctx_move"));
    CHECK(item(menu, "ctx_shell") && item(menu, "ctx_shell")->isEnabled());
    choose(menu, "ctx_measure");
    CHECK(window.statusBar()->currentMessage().contains("area 800.00 mm") && window.statusBar()->currentMessage().contains("perimeter 120.00 mm"));

    // An edge: Fillet and Chamfer.
    shortcut(window, QKeySequence(Qt::Key_Escape));
    click(empty);
    click(point(40, 0, 4));
    click(point(40, -10, 4));
    CHECK(selection->subKind() == SubKind::Edge);
    menu = menuAt(point(40, -10, 4));
    CHECK(item(menu, "ctx_chamfer") && item(menu, "ctx_fillet") && !item(menu, "ctx_presspull"));
    choose(menu, "ctx_fillet");
    CHECK(modify->active() && modify->kind() == td::ModifyKind::Fillet);
    shortcut(window, QKeySequence(Qt::Key_Escape));
    menu = menuAt(empty);
    CHECK(item(menu, "ctx_repeat") && item(menu, "ctx_repeat")->text().contains("Fillet"));
    dismiss(menu);
}

static void testChromeFlow(MainWindow& window, Viewport* viewport, const char* screenshot) {
    using td::V3;
    auto* selection = window.findChild<SelectionController*>();
    auto* placement = window.findChild<PlacementController*>();
    auto* transform = window.findChild<TransformController*>();
    auto* cube = viewport->findChild<ViewCube*>("viewCube");
    const auto& doc = window.document();
    const auto point = [&](double x, double y, double z) { return viewport->project(V3{x, y, z}).toPoint(); };
    const auto click = [&](QPoint p, Qt::KeyboardModifiers m = Qt::NoModifier) { QTest::mouseClick(viewport, Qt::LeftButton, m, p); };
    const auto button = [&](const char* name) { return window.findChild<QToolButton*>(name); };
    const auto near = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };
    const QPoint empty(5, viewport->height() - 5);

    QTemporaryDir dir;
    const QString path = dir.filePath("chrome.cubra");
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(R"({"features":[{"name":"Block","shape":"box","width":30,"depth":30,"height":10}]})");
    file.close();
    CHECK(window.openFile(path));
    click(empty);

    // ---- The ViewCube ----
    CHECK(cube && cube->isVisible());
    const QPoint front = cube->pointFor(V3{0, -1, 0});
    CHECK(front.x() >= 0);
    QTest::mouseClick(cube, Qt::LeftButton, Qt::NoModifier, front);
    QTest::qWait(450);  // the view glides there
    CHECK(near(viewport->pitch(), 0, 0.01) && near(std::fmod(viewport->yaw() + 720, 360), 270, 0.01));
    const QPoint corner = cube->pointFor(V3{1, -1, 1});
    CHECK(corner.x() >= 0);
    QTest::mouseClick(cube, Qt::LeftButton, Qt::NoModifier, corner);
    QTest::qWait(450);
    CHECK(near(viewport->pitch(), 35.26, 0.05) && near(std::fmod(viewport->yaw() + 720, 360), 315, 0.05));
    const QPoint top = cube->pointFor(V3{0, 0, 1});
    QTest::mouseClick(cube, Qt::LeftButton, Qt::NoModifier, top);
    QTest::qWait(450);
    CHECK(viewport->pitch() > 89);
    // Dragging the cube orbits.
    const float yawBefore = viewport->yaw();
    QTest::mousePress(cube, Qt::LeftButton, Qt::NoModifier, QPoint(64, 64));
    QMouseEvent drag(QEvent::MouseMove, QPointF(94, 64), cube->mapToGlobal(QPointF(94, 64)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(cube, &drag);
    QTest::mouseRelease(cube, Qt::LeftButton, Qt::NoModifier, QPoint(94, 64));
    CHECK(viewport->yaw() != yawBefore);
    // Home: back to the starting three-quarter view.
    cube->findChild<QToolButton*>("viewCubeHome")->click();
    QTest::qWait(450);
    CHECK(near(viewport->yaw(), -60, 0.01) && near(viewport->pitch(), 30, 0.01));

    // ---- Navigation and display bar ----
    button("nav_pan")->click();
    CHECK(viewport->navigation() == Viewport::Navigation::Pan);
    button("nav_zoom")->click();
    CHECK(viewport->navigation() == Viewport::Navigation::Zoom && button("nav_zoom")->isChecked());
    viewport->setNavigation(Viewport::Navigation::Orbit);  // e.g. from the right-click menu: the bar follows
    CHECK(button("nav_orbit")->isChecked());

    hoverEnter(button("nav_style"));
    auto* styles = window.findChild<QFrame*>("styleMenu");
    CHECK(styles && styles->isVisible() && styles->geometry().right() <= button("nav_style")->mapTo(viewport, QPoint(0, 0)).x());
    QTest::mouseClick(styles->findChild<QToolButton*>("style_xray"), Qt::LeftButton);
    CHECK(viewport->style() == Viewport::Style::XRay && styles->findChild<QToolButton*>("style_xray")->text().startsWith(QStringLiteral("✓")));
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot));
    for (const char* id : {"style_outlines", "style_materials", "style_materials_outlines"}) {
        hoverEnter(button("nav_style"));
        QTest::mouseClick(styles->findChild<QToolButton*>(id), Qt::LeftButton);
    }
    CHECK(viewport->style() == Viewport::Style::MaterialsAndOutlines);

    button("nav_grid")->click();
    CHECK(!viewport->gridVisible());
    button("nav_grid")->click();
    CHECK(viewport->gridVisible());

    // Fit frames the selection, or everything when nothing is selected.
    click(point(0, 0, 10));
    button("nav_fit")->click();
    const QPoint middle = point(0, 0, 5);
    CHECK(std::abs(middle.x() - viewport->width() / 2) < 30 && std::abs(middle.y() - viewport->height() / 2) < 30);

    // Hide and show from the eye.
    hoverEnter(button("nav_visibility"));
    auto* visibility = window.findChild<QFrame*>("visibilityMenu");
    CHECK(visibility->findChild<QToolButton*>("vis_hidesketches")->isEnabled());
    QTest::mouseClick(visibility->findChild<QToolButton*>("vis_hide"), Qt::LeftButton);
    CHECK(doc.bodies[0].hidden);
    hoverEnter(button("nav_visibility"));
    QTest::mouseClick(visibility->findChild<QToolButton*>("vis_showall"), Qt::LeftButton);
    CHECK(!doc.bodies[0].hidden);

    // A picture of the 3D view alone, at its own size.
    const QString picture = dir.filePath("view.png");
    CHECK(window.saveScreenshot(picture));
    const QImage saved(picture);
    CHECK(!saved.isNull() && saved.width() >= viewport->width() && saved.height() >= viewport->height());

    // ---- Snapping ----
    auto* linear = window.findChild<QComboBox*>("snapLinear");
    auto* angular = window.findChild<QComboBox*>("snapAngular");
    CHECK(linear && angular);
    linear->setCurrentText("5");
    angular->setCurrentText("45");
    CHECK(snapSettings().linear == 5 && snapSettings().angular == 45);
    // Dragging the selected part now steps by 5 mm.
    click(empty);
    click(point(0, 0, 10));
    const QPoint grab = point(0, 0, 10);
    const QPoint to = point(13, 0, 10);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(grab), viewport->mapToGlobal(QPointF(grab)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &press);
    for (int i = 1; i <= 10; ++i) moveTo(viewport, QPointF(grab) + QPointF(to - grab) * (i / 10.0), Qt::LeftButton);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), viewport->mapToGlobal(QPointF(to)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &release);
    CHECK(near(doc.bodies[0].transform.point(V3{}).x, 15, 1e-9));
    window.findChild<QAction*>("undoAction")->trigger();
    // Grid snap off: tools start with their Snap box clear, and the step boxes grey out.
    button("nav_snapgrid")->click();
    CHECK(!snapSettings().grid && !linear->isEnabled());
    click(empty);
    click(point(0, 0, 10));
    shortcut(window, QKeySequence(Qt::Key_M));
    CHECK(transform->active() && !viewport->findChild<QCheckBox*>("transformSnap")->isChecked());
    shortcut(window, QKeySequence(Qt::Key_Escape));
    button("nav_snapgrid")->click();
    CHECK(snapSettings().grid);
    linear->setCurrentText("1");
    angular->setCurrentText("15");

    // ---- Edit Grid ----
    window.findChild<QPushButton*>("editGrid")->click();
    auto* grid = window.findChild<QDialog*>("gridDialog");
    CHECK(grid && grid->isVisible() && grid->findChildren<QSpinBox*>().size() == 2);
    grid->hide();

    // ---- The drawer ----
    auto* tab = button("drawerTab");
    auto* drawer = window.findChild<QFrame*>("partsDrawer");
    tab->click();
    QTest::qWait(250);
    CHECK(drawer->isVisible() && drawer->geometry().right() >= viewport->width() - 1);
    for (const char* id : {"drawer_box", "drawer_star", "drawer_gear", "drawer_nut_m3", "drawer_bolt_m5", "drawer_washer_m8", "drawer_gear_48_ring"}) CHECK(drawer->findChild<QToolButton*>(id) != nullptr);
    // Clicking a part picks it up to place, and the drawer slides away.
    click(empty);
    drawer->findChild<QToolButton*>("drawer_nut_m3")->click();
    QTest::qWait(250);
    CHECK(placement->active() && !drawer->isVisible());
    moveTo(viewport, point(-40, 40, 0));
    shortcut(window, QKeySequence(Qt::Key_Return));
    CHECK(!placement->active() && doc.bodies.size() == 2 && doc.bodies[1].name == "M3 nut 1");
    td::DocumentEvaluator evaluator;
    std::vector<td::EvalResult> results;
    QString error;
    CHECK(evaluator.evaluate(doc, &results, &error) && near(results[1].max[2] - results[1].min[2], 2.4, 0.01));
    CHECK(near(results[1].max[1] - results[1].min[1], 5.5, 0.01));  // 5.5 mm across the flats
    window.findChild<QAction*>("undoAction")->trigger();

    // A colour paints the selected part.
    click(empty);
    click(point(0, 0, 10));
    tab->click();
    QTest::qWait(250);
    drawer->findChild<QToolButton*>("color_d8423c")->click();
    CHECK(doc.bodies[0].color == 0xD8423C);
    tab->click();
    QTest::qWait(250);
    window.findChild<QAction*>("undoAction")->trigger();

    // Dragging a part in from the drawer places it where it is dropped.
    QMimeData part;
    part.setData(kDrawerMime, "drawer_cylinder");
    const QPoint dropAt = point(50, -40, 0);
    QDragEnterEvent enter(point(60, -60, 0), Qt::CopyAction, &part, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &enter);
    CHECK(placement->active());
    QDragMoveEvent over(dropAt, Qt::CopyAction, &part, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &over);
    QDropEvent dropped(QPointF(dropAt), Qt::CopyAction, &part, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &dropped);
    CHECK(!placement->active() && doc.bodies.size() == 2);
    CHECK(near(doc.bodies[1].features[0].x, 50, 1.01) && near(doc.bodies[1].features[0].y, -40, 1.01));
    // A colour dragged onto a part paints that part.
    QMimeData paint;
    paint.setData(kDrawerMime, "color_4caf6a");
    QDragEnterEvent paintEnter(point(0, 0, 10), Qt::CopyAction, &paint, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &paintEnter);
    QDropEvent paintDrop(QPointF(point(0, 0, 10)), Qt::CopyAction, &paint, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &paintDrop);
    CHECK(doc.bodies[0].color == 0x4CAF6A && doc.bodies[1].color == 0);

    // With implicit snap off, a new shape ignores the face under the pointer.
    button("nav_snappoints")->click();
    CHECK(!snapSettings().implicit);
    hoverEnter(button("primitivesButton"));
    QTest::mouseClick(window.findChild<QFrame*>("primitivesFlyout")->findChild<QToolButton*>("primitive_box"), Qt::LeftButton);
    moveTo(viewport, point(0, 0, 10));
    CHECK(placement->feature().z == 0);
    shortcut(window, QKeySequence(Qt::Key_Escape));
    button("nav_snappoints")->click();
    CHECK(snapSettings().implicit);
    (void)selection;
    viewport->setOrientation(-60, 30, false);  // leave the view as the later tests expect it
    viewport->fitBed();
}

// Tools started with nothing selected ask for what they need, one step at a time.
static void testGuidedFlow(MainWindow& window, Viewport* viewport, const char* screenshot) {
    using td::V3;
    auto* steps = window.findChild<StepTool*>();
    auto* modify = window.findChild<ModifyController*>();
    auto* transform = window.findChild<TransformController*>();
    auto* selection = window.findChild<SelectionController*>();
    const auto& doc = window.document();
    const auto point = [&](double x, double y, double z) { return viewport->project(V3{x, y, z}).toPoint(); };
    const auto click = [&](QPoint p) { QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, p); };
    const auto menu = [&](const char* id) {
        auto* item = window.findChild<QToolButton*>(id);
        CHECK(item && item->isEnabled());
        if (item) item->click();
    };
    const auto close = [](V3 a, V3 b) { return td::length(a - b) < 0.01; };
    const QPoint empty(5, viewport->height() - 5);

    QTemporaryDir dir;
    const QString path = dir.filePath("guided.cubra");
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(R"({"features":[
        {"name":"Small","shape":"box","width":10,"depth":10,"height":10,"x":-30},
        {"name":"Large","shape":"box","width":20,"depth":20,"height":20,"x":30,"y":10}]})");
    file.close();
    CHECK(window.openFile(path));
    CHECK(doc.bodies.size() == 2);
    viewport->setOrientation(-60, 30, false);
    viewport->fit({-40, -10, 0}, {45, 25, 20});
    click(empty);

    // Align: Object A, Object B, then how they line up, previewed before it is applied.
    menu("menu_align");
    CHECK(steps->active() && steps->id() == "align");
    CHECK(window.findChild<QPushButton*>("stepSlot0")->text().contains("Object A"));
    CHECK(window.findChild<QPushButton*>("stepSlot1")->text().contains("Object B"));
    click(point(-30, 0, 5));
    click(point(30, 10, 10));
    CHECK(steps->picks()[0].size() == 1 && steps->picks()[0][0].body == 0 && steps->picks()[1][0].body == 1);
    shortcut(window, QKeySequence(Qt::Key_Return));
    CHECK(!steps->active());
    auto* align = window.findChild<QDialog*>("alignDialog");
    CHECK(align && align->isVisible());
    if (align) {
        align->findChild<QToolButton*>("align_z3")->click();  // tops together
        if (screenshot) {
            QTest::qWait(50);
            window.grab().save(QString::fromLocal8Bit(screenshot) + "-preview.png");
            align->grab().save(QString::fromLocal8Bit(screenshot) + "-dialog.png");
        }
        align->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    }
    QTest::qWait(20);
    CHECK(!window.findChild<QDialog*>("alignDialog"));
    CHECK(close(doc.bodies[0].transform.point(V3{}), V3{60, 10, 10}));
    CHECK(doc.bodies[1].transform.isIdentity());
    action(window, "undoAction")->trigger();
    CHECK(doc.bodies[0].transform.isIdentity());

    // Cancelling Align leaves the part where it was.
    click(empty);
    menu("menu_align");
    click(point(-30, 0, 5));
    click(point(30, 10, 10));
    shortcut(window, QKeySequence(Qt::Key_Return));
    align = window.findChild<QDialog*>("alignDialog");
    CHECK(align != nullptr);
    shortcut(window, QKeySequence(Qt::Key_Escape));
    QTest::qWait(20);
    CHECK(!window.findChild<QDialog*>("alignDialog") && doc.bodies[0].transform.isIdentity());

    // Fillet with nothing selected: pick the edge, and the arrow comes up on it.
    click(empty);
    menu("menu_fillet");
    CHECK(steps->active() && steps->id() == "then_fillet" && !modify->active());
    click(point(-30, -5, 10));
    CHECK(!steps->active() && modify->active());
    CHECK(selection->subKind() == SubKind::Edge && selection->subOwner() == 0);
    shortcut(window, QKeySequence(Qt::Key_Escape));
    CHECK(!modify->active());

    // Press Pull asks for a flat face.
    click(empty);
    shortcut(window, QKeySequence(Qt::Key_Escape));
    menu("menu_presspull");
    CHECK(steps->active() && steps->id() == "then_presspull");
    click(point(-30, 0, 10));
    CHECK(!steps->active() && modify->active() && selection->subKind() == SubKind::Face);
    shortcut(window, QKeySequence(Qt::Key_Escape));

    // Move asks for the solid, then shows its arrows.
    click(empty);
    shortcut(window, QKeySequence(Qt::Key_Escape));
    shortcut(window, QKeySequence(Qt::Key_Escape));
    CHECK(selection->selectedBodies().empty() && selection->subKind() == SubKind::None);
    shortcut(window, QKeySequence(Qt::Key_M));
    CHECK(steps->active() && steps->id() == "then_move");
    click(point(30, 10, 10));
    CHECK(!steps->active() && transform->active() && selection->selectedBodies() == std::vector<int>{1});
    shortcut(window, QKeySequence(Qt::Key_Escape));
    CHECK(!transform->active());

    // Group asks for the solids, and Enter groups them.
    click(empty);
    shortcut(window, QKeySequence(Qt::Key_Escape));
    menu("menu_group");
    CHECK(steps->active() && steps->id() == "then_group");
    click(point(-30, 0, 5));
    click(point(30, 10, 10));
    shortcut(window, QKeySequence(Qt::Key_Return));
    CHECK(!steps->active() && !doc.bodies[0].groups.empty() && doc.bodies[0].groups == doc.bodies[1].groups);
    action(window, "undoAction")->trigger();

    // Tools not built yet still open their steps, and say so plainly.
    click(empty);
    shortcut(window, QKeySequence(Qt::Key_Escape));
    const int before = int(doc.bodies.size());
    for (const char* id : {"menu_shell", "menu_tweak", "menu_splitface", "menu_joint"}) {
        menu(id);
        CHECK(steps->active() && steps->id().startsWith("planned_"));
        auto* note = window.findChild<QLabel*>("stepNote");
        CHECK(note && note->isVisible() && note->text().contains("isn't built yet"));
        shortcut(window, QKeySequence(Qt::Key_Escape));
        CHECK(!steps->active());
    }
    menu("menu_align");
    click(point(-30, 0, 5));
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot) + "-steps.png");
    shortcut(window, QKeySequence(Qt::Key_Escape));
    menu("menu_shell");
    click(point(-30, 0, 10));
    CHECK(window.findChild<QLabel*>("stepNote")->isVisible());
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot) + "-shell.png");
    shortcut(window, QKeySequence(Qt::Key_Return));
    CHECK(!steps->active() && window.statusBar()->currentMessage().contains("isn't built yet"));
    CHECK(int(doc.bodies.size()) == before && doc.bodies[0].features.size() == 1);
}

// The how-to manual and its on-screen walkthrough. Every name the manual points at must
// exist, and every toolbar button, list item and view control must be in some topic:
// adding or renaming a tool fails here until the manual says how to use it.
static void testHowTo(MainWindow& window, Viewport* viewport, const char* screenshot) {
    QStringList covered;
    for (const td::ManualTopic& topic : td::manual())
        for (const QString& name : topic.targets()) {
            covered << name;
            if (!window.guideSpot(name).known) std::printf("manual topic %s points at %s, which does not exist\n", qPrintable(topic.id), qPrintable(name));
            CHECK(window.guideSpot(name).known);
        }

    QStringList controls;
    auto* toolbar = window.findChild<QToolBar*>("mainToolbar");
    for (QAction* a : toolbar->actions()) {
        if (!a->objectName().isEmpty()) controls << a->objectName();
        if (QWidget* w = toolbar->widgetForAction(a); w && !w->objectName().isEmpty() && qobject_cast<QToolButton*>(w)) controls << w->objectName();
    }
    for (QFrame* frame : window.findChildren<QFrame*>()) {
        if (frame->property("guideAnchor").toString().isEmpty() || frame->objectName() == "partsDrawer") continue;
        for (QToolButton* item : frame->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly))
            if (!frame->objectName().startsWith("primitives")) controls << item->objectName();
    }
    for (QToolButton* b : window.findChild<QFrame*>("navigationBar")->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) controls << b->objectName();
    for (QAction* a : window.findChild<QToolButton*>("folderButton")->menu()->actions())
        if (!a->isSeparator()) controls << a->objectName();
    controls << "editGrid" << "snapLinear" << "snapAngular" << "drawerTab" << "viewCube" << "viewCubeHome" << "primitivesFlyout";
    controls.removeAll(QString());
    controls.removeDuplicates();
    for (const QString& name : controls) {
        if (!covered.contains(name)) std::printf("no manual topic covers %s\n", qPrintable(name));
        CHECK(covered.contains(name));
    }
    CHECK(controls.size() > 50);

    // The assistant's map of the interface is built from the live toolbar.
    const QString map = window.interfaceMap();
    CHECK(map.contains("Fillet") && map.contains("Export STL") && map.contains("Mirror") && map.contains("Visual style"));

    GuideOverlay* guide = window.guide();
    auto* modify = window.findChild<QToolButton*>("modifyButton");
    auto* fillet = window.findChild<QToolButton*>("menu_fillet");
    window.findChild<QFrame*>("modifyMenu")->hide();

    // Round an edge: the ring starts on Modify, with the bubble beside it.
    emit window.aiPanel()->guideRequested("fillet");
    QTest::qWait(100);
    CHECK(guide->active() && guide->step() == 0 && guide->bubble()->isVisible());
    CHECK(guide->spot().widget == modify && guide->spot().direct);
    CHECK(guide->bubble()->findChild<QLabel*>("guideText")->text().contains("Modify"));
    CHECK(!guide->bubble()->geometry().intersects(QRect(modify->mapTo(&window, QPoint()), modify->size())));
    auto* painter = window.findChild<QWidget*>("guidePainter");
    CHECK(painter && painter->isVisible() && painter->geometry() == window.rect());
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot) + "-1-modify.png");

    // Pointing at Modify opens its list, which moves the walkthrough on to Fillet.
    hoverEnter(modify);
    QTest::qWait(150);
    CHECK(guide->step() == 1 && guide->spot().widget == fillet && guide->spot().direct);
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot) + "-2-fillet.png");

    // With the list closed again, it points back at Modify and says to open it.
    window.findChild<QFrame*>("modifyMenu")->hide();
    QTest::qWait(100);
    CHECK(guide->step() == 1 && guide->spot().widget == modify && !guide->spot().direct && !guide->spot().hint.isEmpty());
    CHECK(guide->bubble()->findChild<QLabel*>("guideHint")->isVisible());

    // Clicking Fillet completes that step: next, the 3D view, outlined without an arrow.
    hoverEnter(modify);
    QTest::qWait(100);
    QTest::mouseClick(fillet, Qt::LeftButton);
    QTest::qWait(100);
    CHECK(guide->step() == 2 && guide->spot().widget == viewport && guide->spot().area);
    if (screenshot) window.grab().save(QString::fromLocal8Bit(screenshot) + "-3-viewport.png");

    // Back and Next step by hand; the cross ends it.
    QTest::mouseClick(guide->bubble()->findChild<QPushButton*>("guideBack"), Qt::LeftButton);
    CHECK(guide->step() == 1);
    QTest::mouseClick(guide->bubble()->findChild<QPushButton*>("guideNext"), Qt::LeftButton);
    CHECK(guide->step() == 2);
    QTest::mouseClick(guide->bubble()->findChild<QPushButton*>("guideClose"), Qt::LeftButton);
    CHECK(!guide->active() && !guide->bubble()->isVisible() && !painter->isVisible());
    QTest::keyClick(viewport, Qt::Key_Escape);  // leave the Fillet steps

    // Export: the folder button's list is a menu; closed, step 2 points at the folder button.
    window.showGuide("export");
    QTest::mouseClick(guide->bubble()->findChild<QPushButton*>("guideNext"), Qt::LeftButton);
    CHECK(guide->step() == 1 && guide->spot().widget == window.findChild<QToolButton*>("folderButton") && !guide->spot().direct);
    window.findChild<QPushButton*>("guideClose")->click();

    // A step inside the closed parts drawer points at its tab.
    window.showGuide("parts-drawer");
    QTest::mouseClick(guide->bubble()->findChild<QPushButton*>("guideNext"), Qt::LeftButton);
    CHECK(guide->spot().widget == window.findChild<QWidget*>("drawerTab") && guide->spot().hint.contains("tab"));
    // Done on the last step ends it.
    while (guide->active()) QTest::mouseClick(guide->bubble()->findChild<QPushButton*>("guideNext"), Qt::LeftButton);
    CHECK(!guide->bubble()->isVisible());
    window.showGuide("no-such-topic");
    CHECK(!guide->active());
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        std::puts("skipped: no display");
        return 77;
    }
    QApplication app(argc, argv);
    QTemporaryDir dir;

    MainWindow window;
    window.resize(1400, 860);
    // Laid out and driven as usual, without appearing on the desktop.
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    QTest::qWait(100);

    auto* toolbar = window.findChild<QToolBar*>("mainToolbar");
    auto* viewport = window.findChild<Viewport*>();
    auto* panel = window.findChild<FeaturePanel*>();
    auto* list = panel->findChild<QListWidget*>();
    auto* primitives = window.findChild<QToolButton*>("primitivesButton");
    auto* flyout = window.findChild<QFrame*>("primitivesFlyout");
    auto* designDock = window.findChild<QDockWidget*>("designDock");
    auto* aiDock = window.findChild<QDockWidget*>("aiDock");

    // The workspace owns the window by default.
    CHECK(!designDock->isVisible() && !aiDock->isVisible());
    CHECK(viewport->width() == window.width());
    CHECK(window.findChild<QMenuBar*>() == nullptr || !window.menuBar()->isVisible() || window.menuBar()->actions().isEmpty());

    // Toolbar order: folder, undo, redo, transform, select, primitives, modify, combine, snap, group.
    const QList<QAction*> items = toolbar->actions();
    CHECK(items.size() > 10);
    CHECK(toolbar->widgetForAction(items[0]) == window.findChild<QToolButton*>("folderButton"));
    CHECK(items[1] == action(window, "undoAction"));
    CHECK(items[2] == action(window, "redoAction"));
    CHECK(toolbar->widgetForAction(items[3]) == window.findChild<QToolButton*>("transformButton"));
    CHECK(items[4] == action(window, "selectAction"));
    CHECK(toolbar->widgetForAction(items[5]) == primitives);
    CHECK(toolbar->widgetForAction(items[6]) == window.findChild<QToolButton*>("modifyButton"));
    CHECK(toolbar->widgetForAction(items[7]) == window.findChild<QToolButton*>("combineButton"));
    CHECK(toolbar->widgetForAction(items[8]) == window.findChild<QToolButton*>("snapButton"));
    CHECK(toolbar->widgetForAction(items[9]) == window.findChild<QToolButton*>("groupButton"));
    CHECK(toolbar->widgetForAction(items[10]) == window.findChild<QToolButton*>("patternButton"));

    // Each menu drops down on hover with its tools, none of them greyed out.
    const struct {
        const char* button;
        const char* frame;
        std::vector<const char*> ready;
        std::vector<const char*> greyed;
    } menus[] = {
        {"transformButton", "transformMenu", {"menu_move", "menu_scale", "menu_nonuniform", "menu_smart"}, {"menu_align"}},
        {"modifyButton", "modifyMenu", {"menu_presspull", "menu_fillet", "menu_chamfer", "menu_splitsolid"}, {"menu_tweak", "menu_splitface", "menu_shell"}},
        {"combineButton", "combineMenu", {"menu_merge", "menu_subtract", "menu_intersect", "menu_separate"}, {}},
        {"snapButton", "snapMenu", {"menu_snap"}, {"menu_joint"}},
        {"groupButton", "groupMenu", {"menu_group", "menu_ungroup", "menu_ungroupall"}, {}},
        {"patternButton", "patternMenu", {"menu_rectpattern", "menu_circpattern", "menu_pathpattern", "menu_mirror"}, {}},
    };
    for (const auto& m : menus) {
        auto* frame = window.findChild<QFrame*>(m.frame);
        CHECK(frame && !frame->isVisible());
        hoverEnter(window.findChild<QToolButton*>(m.button));
        CHECK(frame && frame->isVisible());
        for (const char* id : m.ready) CHECK(frame->findChild<QToolButton*>(id) && frame->findChild<QToolButton*>(id)->isEnabled());
        for (const char* id : m.greyed) CHECK(frame->findChild<QToolButton*>(id) && frame->findChild<QToolButton*>(id)->isEnabled());
        hoverLeave(window.findChild<QToolButton*>(m.button));
        QTest::qWait(260);
        CHECK(!frame->isVisible());
    }
    CHECK(!window.findChild<QToolButton*>("folderButton")->menu()->actions().isEmpty());

    // Hovering opens the primitives list, and it closes after the pointer leaves.
    CHECK(!flyout->isVisible());
    hoverEnter(primitives);
    CHECK(flyout->isVisible());
    const QList<QToolButton*> buttons = flyout->findChildren<QToolButton*>();
    CHECK(buttons.size() == 11);
    if (argc > 1) window.grab().save(QString::fromLocal8Bit(argv[1]));

    // Moving from the button onto the list must keep it open.
    hoverLeave(primitives);
    hoverEnter(flyout);
    QTest::qWait(350);
    CHECK(flyout->isVisible());
    hoverLeave(flyout);
    QTest::qWait(350);
    CHECK(!flyout->isVisible());

    // Choosing a primitive starts a preview and adds nothing until it is placed.
    auto* controller = window.findChild<PlacementController*>();
    const auto hud = [&](const char* label) { return viewport->findChild<QLineEdit*>(QStringLiteral("hud_") + QLatin1String(label)); };
    const auto choose = [&](const char* id) {
        hoverEnter(primitives);
        QTest::mouseClick(flyout->findChild<QToolButton*>(id), Qt::LeftButton);
    };
    const auto press = [&](int key) {
        QLineEdit* field = qobject_cast<QLineEdit*>(window.focusWidget());
        CHECK(field != nullptr);
        if (field) QTest::keyClick(field, Qt::Key(key));
    };

    int expected = 0;
    for (QToolButton* button : buttons) {
        hoverEnter(primitives);
        QTest::mouseClick(button, Qt::LeftButton);
        CHECK(controller->active());
        CHECK(!flyout->isVisible());
        CHECK(featureCount(window) == expected);
        press(Qt::Key_Return);
        CHECK(!controller->active());
        CHECK(featureCount(window) == ++expected);
    }
    CHECK(!designDock->isVisible());

    // Undo and redo.
    action(window, "undoAction")->trigger();
    CHECK(featureCount(window) == int(buttons.size()) - 1);
    action(window, "redoAction")->trigger();
    CHECK(featureCount(window) == int(buttons.size()));

    // Two boxes far apart for the selection tests.
    const QString path = dir.filePath("two.cubra");
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(R"({"features":[
        {"name":"Left","op":"add","shape":"box","width":20,"depth":20,"height":20,"x":-60},
        {"name":"Right","op":"add","shape":"box","width":20,"depth":20,"height":20,"x":60}]})");
    file.close();
    CHECK(window.openFile(path));
    CHECK(featureCount(window) == 2);

    testSelectionFlow(window, viewport, panel, list, argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr);

    testPlacementFlow(window, viewport, controller, list, choose, hud, press, argc > 2 ? argv[2] : nullptr);
    testBodiesFlow(window, viewport, panel, argc > 5 ? argv[5] : nullptr);
    testPatternFlow(window, viewport, argc > 6 ? argv[6] : nullptr);
    testContextMenuFlow(window, viewport, argc > 7 ? argv[7] : nullptr);
    testChromeFlow(window, viewport, argc > 8 ? argv[8] : nullptr);
    testGuidedFlow(window, viewport, argc > 9 ? argv[9] : nullptr);
    testHowTo(window, viewport, argc > 10 ? argv[10] : nullptr);

    // A click on the workspace takes the keys back from any text box, so Delete deletes.
    for (QWidget* box : {static_cast<QWidget*>(window.findChild<QComboBox*>()), static_cast<QWidget*>(window.findChild<QPlainTextEdit*>())}) {
        if (!box) continue;
        box->setFocus(Qt::MouseFocusReason);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(5, viewport->height() - 5));
        CHECK(window.focusWidget() == viewport);
    }

    // Place a box the usual way, click it and press the real Delete key: it goes.
    // (The placement boxes used to keep the keyboard, so Delete edited a hidden box.)
    {
        auto* prim = window.findChild<QToolButton*>("primitivesButton");
        hoverEnter(prim);
        QTest::mouseClick(window.findChild<QFrame*>("primitivesFlyout")->findChild<QToolButton*>("primitive_box"), Qt::LeftButton);
        QTest::keyClick(window.focusWidget(), Qt::Key_Return);
        const size_t before = window.document().bodies.size();
        const td::Feature& placed = window.document().bodies.back().features[0];
        const QPoint at = viewport->project(td::V3{placed.x, placed.y, placed.z + 10}).toPoint();
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at);
        CHECK(window.findChild<SelectionController*>()->selectedBodies() == std::vector<int>{int(before) - 1});
        // Qt asks the focused widget first whether it wants the key for itself; only if
        // not does the Delete shortcut run. (The hidden test window is never "active",
        // so the shortcut itself has to be triggered by hand below.)
        QWidget* focus = window.focusWidget();
        CHECK(focus && focus->isVisible() && !qobject_cast<QLineEdit*>(focus));
        QKeyEvent claim(QEvent::ShortcutOverride, Qt::Key_Delete, Qt::NoModifier);
        claim.ignore();
        if (focus) QApplication::sendEvent(focus, &claim);
        CHECK(!claim.isAccepted());
        for (QAction* a : window.findChildren<QAction*>())
            if (a->shortcut() == QKeySequence(QKeySequence::Delete)) a->trigger();
        CHECK(window.document().bodies.size() == before - 1);
        action(window, "undoAction")->trigger();
        CHECK(window.document().bodies.size() == before);
    }

    // Dragging a selected part slides it along the bed in whole millimetres, as one undo step.
    {
        auto* selection = window.findChild<SelectionController*>();
        const td::Document& doc = window.document();
        const int moved = int(doc.bodies.size()) - 1;
        const td::Feature& box = doc.bodies[moved].features[0];
        const td::V3 top{box.x, box.y, box.z + box.height};
        const QPoint grab = viewport->project(top).toPoint();
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(5, viewport->height() - 5));

        // Not selected yet: the same drag turns the view and moves nothing. (Diagonal, so
        // the view also tilts: a part right at the centre does not move on screen when it only turns.)
        const QPoint beforeTurn = viewport->project(top).toPoint();
        leftDrag(viewport, grab, grab + QPoint(30, 20));
        CHECK(doc.bodies[moved].transform.isIdentity() && viewport->project(top).toPoint() != beforeTurn);

        const QPoint again = viewport->project(top).toPoint();
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, again);
        CHECK(selection->selectedBodies() == std::vector<int>{moved});
        const QPoint target = viewport->project(td::V3{top.x + 12.4, top.y - 7.6, top.z}).toPoint();
        const QPoint beforeMove = viewport->project(top).toPoint();
        QMouseEvent down(QEvent::MouseButtonPress, QPointF(again), viewport->mapToGlobal(QPointF(again)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &down);
        for (int i = 1; i <= 20; ++i) moveTo(viewport, QPointF(again) + QPointF(target - again) * (i / 20.0), Qt::LeftButton);
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(target), viewport->mapToGlobal(QPointF(target)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &up);
        const td::V3 shift = doc.bodies[moved].transform.point(td::V3{});
        CHECK(std::abs(shift.x - 12) < 1.01 && std::abs(shift.y + 8) < 1.01 && shift.z == 0);
        CHECK(shift.x == std::round(shift.x) && shift.y == std::round(shift.y));
        CHECK(viewport->project(top).toPoint() == beforeMove);  // the view itself did not turn
        CHECK(selection->selectedBodies() == std::vector<int>{moved});
        action(window, "undoAction")->trigger();
        CHECK(doc.bodies[moved].transform.isIdentity());
    }

    // In the AI prompt, Shift+Enter starts a new line and Enter sends.
    {
        auto* prompt = window.aiPanel()->findChild<QPlainTextEdit*>();
        auto* transcript = window.aiPanel()->findChild<QTextBrowser*>();
        CHECK(prompt && transcript);
        QTest::keyClick(prompt, Qt::Key_Return);  // nothing typed: nothing is sent
        CHECK(transcript->toPlainText().isEmpty());
        QTest::keyClicks(prompt, "a box");
        QTest::keyClick(prompt, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(prompt, "with a hole");
        CHECK(prompt->toPlainText() == "a box\nwith a hole");
        QTest::keyClick(prompt, Qt::Key_Enter);  // the keypad Enter too
        CHECK(prompt->toPlainText().isEmpty() && transcript->toPlainText().contains("with a hole"));
        for (QPushButton* b : window.aiPanel()->findChildren<QPushButton*>())
            if (b->text() == "Cancel" && b->isEnabled()) b->click();  // stop before it reaches the model
    }

    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    window.setWindowModified(false);
    // Skip the unsaved-changes prompt: there is nobody to answer it here.
    std::fflush(stdout);
    std::_Exit(failures ? 1 : 0);
}
