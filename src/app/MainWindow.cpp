#include "app/MainWindow.h"

#include "app/AiPanel.h"
#include "ai/Manual.h"
#include "app/FeaturePanel.h"
#include "app/HoverMenu.h"
#include "app/ModifyController.h"
#include "app/PatternController.h"
#include "app/PlacementController.h"
#include "app/SelectionController.h"
#include "app/SnapSettings.h"
#include "app/ViewportChrome.h"
#include "app/Viewport.h"
#include "core/Exporters.h"
#include "core/Pattern.h"

#include <QAction>
#include <QPushButton>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QStandardPaths>
#include <QActionGroup>
#include <QColorDialog>
#include <QCursor>
#include <QToolTip>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <set>
#include <map>
#include <memory>

using td::Feature;

namespace {
constexpr size_t kUndoLimit = 200;
constexpr qint64 kEditMergeMs = 800;
const QString kDesignFilter = QStringLiteral("Cubra files (*.cubra *.3design)");

struct Preset {
    const char* id;
    const char* label;
    Icon icon;
    Feature feature;
};

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = [] {
        Feature box;
        Feature rounded = box;
        rounded.corner = 3;
        Feature cylinder;
        cylinder.shape = td::Shape::Cylinder;
        Feature cone = cylinder;
        cone.topDiameter = 0;
        Feature hex = cylinder;
        hex.sides = 6;
        Feature sphere;
        sphere.shape = td::Shape::Sphere;
        Feature torus;
        torus.shape = td::Shape::Torus;
        Feature wedge = box;
        wedge.shape = td::Shape::Wedge;
        Feature gear;
        gear.shape = td::Shape::Gear;
        gear.sides = 20;
        gear.diameter = 44;  // 2 mm teeth
        gear.height = 6;
        gear.innerDiameter = 5;
        Feature coil;
        coil.shape = td::Shape::Coil;
        coil.diameter = 20;
        coil.tubeDiameter = 2;
        coil.height = 30;
        coil.turns = 6;
        Feature star;
        star.shape = td::Shape::Star;
        star.sides = 5;
        star.height = 5;
        return std::vector<Preset>{
            {"primitive_box", QT_TRANSLATE_NOOP("MainWindow", "Box"), Icon::Box, box},
            {"primitive_rounded_box", QT_TRANSLATE_NOOP("MainWindow", "Rounded box"), Icon::RoundedBox, rounded},
            {"primitive_wedge", QT_TRANSLATE_NOOP("MainWindow", "Wedge"), Icon::Wedge, wedge},
            {"primitive_cylinder", QT_TRANSLATE_NOOP("MainWindow", "Cylinder"), Icon::Cylinder, cylinder},
            {"primitive_cone", QT_TRANSLATE_NOOP("MainWindow", "Cone"), Icon::Cone, cone},
            {"primitive_hex_prism", QT_TRANSLATE_NOOP("MainWindow", "Hex prism"), Icon::HexPrism, hex},
            {"primitive_sphere", QT_TRANSLATE_NOOP("MainWindow", "Sphere"), Icon::Sphere, sphere},
            {"primitive_torus", QT_TRANSLATE_NOOP("MainWindow", "Torus"), Icon::Torus, torus},
            {"primitive_star", QT_TRANSLATE_NOOP("MainWindow", "Star"), Icon::Star, star},
            {"primitive_gear", QT_TRANSLATE_NOOP("MainWindow", "Gear"), Icon::Gear, gear},
            {"primitive_coil", QT_TRANSLATE_NOOP("MainWindow", "Spring"), Icon::Coil, coil},
        };
    }();
    return list;
}

// Gentle, distinct colours so separate bodies read as separate.
QVector4D bodyColor(int id) {
    static const QVector4D palette[] = {{0.42f, 0.66f, 0.92f, 1}, {0.93f, 0.62f, 0.36f, 1}, {0.50f, 0.80f, 0.56f, 1}, {0.80f, 0.56f, 0.86f, 1},
                                        {0.93f, 0.80f, 0.40f, 1}, {0.44f, 0.80f, 0.82f, 1}, {0.90f, 0.52f, 0.56f, 1}, {0.68f, 0.70f, 0.76f, 1}};
    return palette[(std::max(id, 1) - 1) % 8];
}

// Ready-made parts for the drawer. Nuts, bolts and washers use ISO metric sizes;
// bolts are plain shanks, as printed threads need a profile of their own.
struct Hardware {
    QString id, label;
    Feature feature;
};

Feature solidOf(std::vector<Feature> children, const QString& name) {
    Feature part;
    part.name = name;
    part.shape = td::Shape::Compound;
    part.children = std::move(children);
    return part;
}

Feature hexPrism(double acrossFlats, double height, double z = 0) {
    Feature hex;
    hex.shape = td::Shape::Cylinder;
    hex.sides = 6;
    hex.diameter = acrossFlats / std::cos(std::numbers::pi / 6);  // the shape is sized across its corners
    hex.height = height;
    hex.z = z;  // six segments put a flat facing the front
    return hex;
}

Feature roundBar(double diameter, double height, double z = 0, td::Op op = td::Op::Add) {
    Feature bar;
    bar.shape = td::Shape::Cylinder;
    bar.diameter = diameter;
    bar.height = height;
    bar.z = z;
    bar.op = op;
    return bar;
}

const std::vector<Hardware>& hardware() {
    static const std::vector<Hardware> list = [] {
        std::vector<Hardware> out;
        struct Size {
            const char* name;
            double d, acrossFlats, nut, head, washerOuter, washer;
        };
        const Size sizes[] = {{"M3", 3, 5.5, 2.4, 2.0, 7, 0.5}, {"M4", 4, 7, 3.2, 2.8, 9, 0.8}, {"M5", 5, 8, 4.7, 3.5, 10, 1},
                              {"M6", 6, 10, 5.2, 4, 12, 1.6}, {"M8", 8, 13, 6.8, 5.3, 16, 1.6}};
        for (const Size& s : sizes) {
            const QString name = QString::fromLatin1(s.name);
            const double hole = s.d + 0.3;  // clearance, so a real bolt passes through a printed part
            out.push_back({"drawer_nut_" + name.toLower(), name + " nut",
                           solidOf({hexPrism(s.acrossFlats, s.nut), roundBar(hole, s.nut + 2, -1, td::Op::Subtract)}, name + " nut")});
        }
        for (const Size& s : sizes) {
            const QString name = QString::fromLatin1(s.name);
            const double length = s.d * 4;
            out.push_back({QStringLiteral("drawer_bolt_%1").arg(name.toLower()), QStringLiteral("%1x%2 bolt").arg(name).arg(length),
                           solidOf({hexPrism(s.acrossFlats, s.head), roundBar(s.d, length, s.head)}, QStringLiteral("%1x%2 bolt").arg(name).arg(length))});
        }
        for (const Size& s : sizes) {
            const QString name = QString::fromLatin1(s.name);
            out.push_back({"drawer_washer_" + name.toLower(), name + " washer",
                           solidOf({roundBar(s.washerOuter, s.washer), roundBar(s.d + 0.3, s.washer + 2, -1, td::Op::Subtract)}, name + " washer")});
        }
        const std::pair<int, bool> gears[] = {{12, false}, {20, false}, {40, false}, {48, true}};
        for (const auto& [teeth, inside] : gears) {
            Feature gear;
            gear.shape = td::Shape::Gear;
            gear.sides = teeth;
            gear.internal = inside;
            gear.diameter = inside ? teeth + 8.5 : teeth + 2.0;  // 1 mm teeth, so every gear here meshes with the others
            gear.height = 6;
            gear.innerDiameter = inside ? 0 : (teeth >= 20 ? 5 : 3);
            out.push_back({QStringLiteral("drawer_gear_%1%2").arg(teeth).arg(inside ? "_ring" : ""),
                           inside ? QStringLiteral("%1T ring gear").arg(teeth) : QStringLiteral("%1T gear").arg(teeth), gear});
        }
        return out;
    }();
    return list;
}

QMatrix4x4 toMatrix(const td::Affine& a) {
    const auto& m = a.m;
    return QMatrix4x4(float(m[0]), float(m[3]), float(m[6]), float(m[9]), float(m[1]), float(m[4]), float(m[7]), float(m[10]), float(m[2]), float(m[5]),
                      float(m[8]), float(m[11]), 0, 0, 0, 1);
}

bool isPrimitive(td::Shape shape) { return shape != td::Shape::Prisms && shape != td::Shape::Compound && shape != td::Shape::Mesh; }

}  // namespace

MainWindow::MainWindow() {
    resize(1400, 860);
    setContextMenuPolicy(Qt::NoContextMenu);

    viewport_ = new Viewport(this);
    setCentralWidget(viewport_);
    placement_ = new PlacementController(viewport_, &mesh_, this);
    connect(placement_, &PlacementController::placed, this, &MainWindow::addPrimitive);
    modify_ = new ModifyController(viewport_, this);
    transform_ = new TransformController(viewport_, this);
    steps_ = new StepTool(viewport_);
    pattern_ = new PatternController(viewport_, this);

    panel_ = new FeaturePanel(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(panel_);
    scroll->setWidgetResizable(true);
    designDock_ = new QDockWidget(tr("Design"), this);
    designDock_->setObjectName("designDock");
    designDock_->setWidget(scroll);
    designDock_->setMinimumWidth(300);
    addDockWidget(Qt::LeftDockWidgetArea, designDock_);

    ai_ = new AiPanel(
        [this] {
            const int target = aiTargetBody();
            return AiPanel::Context{target >= 0 ? document_.bodies[target].features : Features{}, bedWidth_->value(), bedDepth_->value(), interfaceMap()};
        },
        this);
    aiDock_ = new QDockWidget(tr("AI assistant (local)"), this);
    aiDock_->setObjectName("aiDock");
    aiDock_->setWidget(ai_);
    aiDock_->setMinimumWidth(340);
    addDockWidget(Qt::RightDockWidgetArea, aiDock_);

    // The workspace gets the whole window until a panel is asked for.
    designDock_->hide();
    aiDock_->hide();

    stats_ = new QLabel(this);
    statusBar()->addPermanentWidget(stats_);
    statusBar()->setSizeGripEnabled(false);

    QSettings settings;
    bedWidth_ = new QSpinBox(this);
    bedDepth_ = new QSpinBox(this);
    for (QSpinBox* spin : {bedWidth_, bedDepth_}) {
        spin->setRange(50, 1000);
        spin->setSuffix(tr(" mm"));
        spin->setKeyboardTracking(false);
    }
    bedWidth_->setValue(settings.value("bed/width", 220).toInt());
    bedDepth_->setValue(settings.value("bed/depth", 220).toInt());

    selection_ = new SelectionController(viewport_, panel_, &document_, &bodyResults_, this);
    buildToolbar();

    const auto bedChanged = [this] {
        QSettings store;
        store.setValue("bed/width", bedWidth_->value());
        store.setValue("bed/depth", bedDepth_->value());
        viewport_->setBed(bedWidth_->value(), bedDepth_->value());
        rebuild();
    };
    connect(bedWidth_, &QSpinBox::valueChanged, this, bedChanged);
    connect(bedDepth_, &QSpinBox::valueChanged, this, bedChanged);
    viewport_->setBed(bedWidth_->value(), bedDepth_->value());
    viewport_->fitBed();

    connect(viewport_, &Viewport::clicked, this, &MainWindow::routeClick);
    connect(viewport_, &Viewport::doubleClicked, this, &MainWindow::routeDoubleClick);
    connect(viewport_, &Viewport::contextMenuRequested, this, &MainWindow::showContextMenu);

    chrome_ = new ViewportChrome(viewport_);
    chrome_->setDrawer(drawerSections());

    guide_ = new GuideOverlay(this, [this](const QString& name) { return guideSpot(name); });
    connect(ai_, &AiPanel::guideRequested, this, &MainWindow::showGuide);
    connect(chrome_, &ViewportChrome::homeRequested, this, [this] {
        viewport_->setOrientation(-60, 30, true);
        fitView();
    });
    connect(chrome_, &ViewportChrome::fitRequested, this, &MainWindow::fitView);
    connect(chrome_, &ViewportChrome::hideSketchesRequested, this, [this] { statusBar()->showMessage(tr("There are no sketches to hide: Sketch on Face isn't built yet."), 6000); });
    connect(chrome_, &ViewportChrome::hideSelectedRequested, this, [this] { hideBodies(selection_->selectedBodies()); });
    connect(chrome_, &ViewportChrome::showAllRequested, this, &MainWindow::showAll);
    connect(chrome_, &ViewportChrome::screenshotRequested, this, [this] {
        const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                                      .filePath(QStringLiteral("cubra-%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")));
        const QString path = QFileDialog::getSaveFileName(this, tr("Save picture"), suggested, tr("PNG images (*.png)"));
        if (path.isEmpty()) return;
        if (saveScreenshot(path)) statusBar()->showMessage(tr("Saved %1").arg(path), 6000);
        else QMessageBox::warning(this, tr("Could not save picture"), tr("The picture could not be written to %1.").arg(path));
    });
    connect(chrome_, &ViewportChrome::editGridRequested, this, &MainWindow::editGrid);
    connect(chrome_, &ViewportChrome::drawerItemClicked, this, &MainWindow::useDrawerItem);
    connect(viewport_, &Viewport::dropEntered, this, [this](const QString& payload, QPoint position) {
        if (payload.startsWith("color_")) return;
        const auto part = drawerPart(payload);
        if (!part) return;
        cancelTools();
        selection_->clear();
        placement_->begin(part->first, part->second);
        placement_->pointerAt(position);
        placingFromDrop_ = true;
    });
    connect(viewport_, &Viewport::dropMoved, this, [this](QPoint position) {
        if (placingFromDrop_) placement_->pointerAt(position);
    });
    connect(viewport_, &Viewport::dropLeft, this, [this] {
        if (placingFromDrop_) placement_->cancel();
        placingFromDrop_ = false;
    });
    connect(viewport_, &Viewport::dropFinished, this, [this](const QString& payload, QPoint position) {
        if (payload.startsWith("color_")) {
            // A colour dropped onto a part paints that part.
            const auto hit = selection_->hitAt(position);
            if (hit) paintBodies({hit->body}, payload.mid(6).toUInt(nullptr, 16));  // after "color_"
            return;
        }
        if (!placingFromDrop_) return;
        placingFromDrop_ = false;
        placement_->pointerAt(position);
        placement_->placeNow();
    });
    viewport_->setGrabTest([this](QPoint position) { return canGrab(position); });
    connect(viewport_, &Viewport::grabStarted, this, &MainWindow::startGrab);
    connect(viewport_, &Viewport::grabMoved, this, &MainWindow::moveGrab);
    connect(viewport_, &Viewport::grabFinished, this, &MainWindow::finishGrab);
    connect(modify_, &ModifyController::clickedAway, this, &MainWindow::routeClick);
    connect(selection_, &SelectionController::subSelectionChanged, this, &MainWindow::onSubSelectionChanged);
    connect(selection_, &SelectionController::contextChanged, this, [this] {
        if (selection_->groupContext().empty()) statusBar()->clearMessage();
        else statusBar()->showMessage(tr("Inside a group: clicks pick its members. Esc, or double-click empty space, to leave."));
    });
    connect(modify_, &ModifyController::previewMoved, selection_, &SelectionController::setPreviewing);
    connect(viewport_, &Viewport::marqueeFinished, this, [this](const QRect& rect, bool additive, bool crossing) {
        if (steps_->active() || transform_->active()) return;
        selection_->handleBox(rect, additive, crossing);
        // With something selected the tool has done its job: left-drag rotates the view again.
        if (!selection_->selectedBodies().empty() || !selection_->subIds().empty()) selectAction_->setChecked(false);
    });
    connect(selection_, &SelectionController::gearAction, this, &MainWindow::runGearTool);
    connect(modify_, &ModifyController::committed, this, &MainWindow::commitModify);
    connect(modify_, &ModifyController::cancelled, this, [this] {
        selection_->setPreviewing(false);
        // The selection stays, since the model never changed.
        if (modifyOwner_ >= 0 && modifyOwner_ < int(bodyResults_.size())) viewport_->setBodyMesh(modifyOwner_, bodyResults_[modifyOwner_].mesh);
    });
    connect(transform_, &TransformController::committed, this, &MainWindow::commitTransform);
    connect(transform_, &TransformController::cancelled, this, [this] {
        viewport_->clearBodyPreviews();
        selection_->setPreviewing(false);
        selection_->refreshGraphics();
    });
    connect(steps_, &StepTool::changed, this, &MainWindow::showStepPicks);
    connect(steps_, &StepTool::cancelled, this, [this] {
        pattern_->end();
        viewport_->setHighlights({});
    });
    connect(pattern_, &PatternController::changed, this, &MainWindow::showStepPicks);
    connect(pattern_, &PatternController::confirmRequested, steps_, &StepTool::confirm);
    connect(steps_, &StepTool::finished, this, &MainWindow::finishSteps);

    connect(panel_, &FeaturePanel::removeRequested, this, &MainWindow::removeBodies);
    connect(panel_, &FeaturePanel::moveRequested, this, [this](int index, int delta) {
        pushUndo();
        std::swap(document_.bodies[index], document_.bodies[index + delta]);
        afterChange({index + delta});
    });
    connect(panel_, &FeaturePanel::selectionChanged, this, [this] { lastEditedIndex_ = -1; });
    connect(panel_, &FeaturePanel::featureEdited, this, [this](int index, const Feature& feature) {
        // Clicking a spin arrow repeatedly should be a single undo step.
        if (index != lastEditedIndex_ || !sinceLastEdit_.isValid() || sinceLastEdit_.elapsed() > kEditMergeMs) pushUndo();
        lastEditedIndex_ = index;
        sinceLastEdit_.restart();
        td::Body& body = document_.bodies[index];
        body.name = feature.name;
        if (body.features.size() == 1 && isPrimitive(body.features[0].shape)) body.features[0] = feature;
        markModified();
        rebuild();
    });
    connect(ai_, &AiPanel::designProposed, this, [this](const Features& features, const QString&, bool fresh) { applyAiDesign(features, fresh); });

    rebuild();
    updateTitle();
}

QToolButton* MainWindow::menuButton(QToolBar* toolbar, const QString& name, Icon icon, const QString& tip) {
    auto* button = new QToolButton(this);
    button->setObjectName(name);
    button->setIcon(makeIcon(icon, palette().color(QPalette::WindowText)));
    button->setIconSize(toolbar->iconSize());
    button->setAutoRaise(true);
    button->setToolTip(tip);
    toolbar->addWidget(button);
    return button;
}

void MainWindow::buildToolbar() {
    const QColor ink = palette().color(QPalette::WindowText);
    const QSize iconSize(26, 26);

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName("mainToolbar");
    toolbar_ = toolbar;
    toolbar->setMovable(false);
    toolbar->setIconSize(iconSize);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    const auto action = [this](const QString& text, QKeySequence key, auto slot) {
        QAction* a = new QAction(text, this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    const auto shortcut = [&](const QString& text, QKeySequence key, auto slot) { addAction(action(text, key, slot)); };

    // Everything that was on the File menu lives behind the folder icon.
    auto* fileMenu = new QMenu(this);
    const auto fileItem = [&](const char* name, const QString& text, QKeySequence key, auto slot) {
        QAction* a = action(text, key, slot);
        a->setObjectName(QString::fromLatin1(name));
        fileMenu->addAction(a);
        addAction(a);  // keeps the shortcut working while the menu is closed
    };
    fileItem("file_new", tr("&New"), QKeySequence::New, [this] { newDesign(); });
    fileItem("file_open", tr("&Open…"), QKeySequence::Open, [this] { open(); });
    fileItem("file_save", tr("&Save"), QKeySequence::Save, [this] { save(); });
    fileItem("file_saveas", tr("Save &As…"), QKeySequence::SaveAs, [this] { saveAs(); });
    fileMenu->addSeparator();
    fileItem("file_exportstl", tr("Export &STL…"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { exportMesh(false); });
    fileItem("file_export3mf", tr("Export &3MF…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [this] { exportMesh(true); });
    fileMenu->addSeparator();
    fileItem("file_quit", tr("&Quit"), QKeySequence::Quit, [this] { close(); });
    fileMenu_ = fileMenu;

    auto* folder = new QToolButton(this);
    folder->setObjectName("folderButton");
    folder->setIcon(makeIcon(Icon::Folder, ink));
    folder->setIconSize(iconSize);
    folder->setAutoRaise(true);
    folder->setToolTip(tr("Open, save and export"));
    folder->setMenu(fileMenu);
    folder->setPopupMode(QToolButton::InstantPopup);
    folder->setStyleSheet("QToolButton::menu-indicator { image: none; }");
    toolbar->addWidget(folder);

    undoAction_ = action(tr("Undo"), QKeySequence::Undo, [this] { undo(); });
    undoAction_->setObjectName("undoAction");
    undoAction_->setIcon(makeIcon(Icon::Undo, ink));
    undoAction_->setEnabled(false);
    toolbar->addAction(undoAction_);

    redoAction_ = action(tr("Redo"), QKeySequence::Redo, [this] { redo(); });
    redoAction_->setObjectName("redoAction");
    redoAction_->setIcon(makeIcon(Icon::Redo, ink));
    redoAction_->setEnabled(false);
    toolbar->addAction(redoAction_);

    const auto addMenu = [&](const QString& button, Icon icon, const QString& tip, const QString& frame,
                             std::initializer_list<std::tuple<const char*, QString, QString>> items) {
        auto* menu = new HoverMenu(menuButton(toolbar, button, icon, tip), this, frame);
        for (const auto& [id, label, tip] : items) menu->addItem(QString::fromLatin1(id), label, {}, true, tip);
        connect(menu, &HoverMenu::triggered, this, &MainWindow::runMenu);
        return menu;
    };

    addMenu("transformButton", Icon::Transform, tr("Transform"), "transformMenu",
            {{"menu_move", tr("Move / Rotate   (M)"), {}},
             {"menu_scale", tr("Scale   (S)"), {}},
             {"menu_nonuniform", tr("Non-Uniform Scale"), {}},
             {"menu_smart", tr("Smart Scale"), {}},
             {"menu_align", tr("Align"), {}}});

    selectAction_ = new QAction(makeIcon(Icon::Select, ink), tr("Select: drag on the workspace. Shift adds. Esc exits."), this);
    selectAction_->setObjectName("selectAction");
    selectAction_->setCheckable(true);
    connect(selectAction_, &QAction::toggled, viewport_, &Viewport::setSelectMode);
    connect(selectAction_, &QAction::toggled, this, [this](bool on) {
        if (on) cancelTools();
    });
    toolbar->addAction(selectAction_);

    primitivesMenu_ = new HoverMenu(menuButton(toolbar, "primitivesButton", Icon::Primitives, tr("Add a primitive")), this, "primitivesFlyout");
    for (const Preset& preset : presets()) {
        const Feature feature = preset.feature;
        const QString label = tr(preset.label);
        QToolButton* item = primitivesMenu_->addItem(QString::fromLatin1(preset.id), label, makeIcon(preset.icon, ink));
        connect(item, &QToolButton::clicked, this, [this, feature, label] {
            selectAction_->setChecked(false);
            cancelTools();
            selection_->clear();
            placement_->begin(feature, label);
        });
    }

    addMenu("modifyButton", Icon::Modify, tr("Modify"), "modifyMenu",
            {{"menu_presspull", tr("Press Pull   (P)"), {}},
             {"menu_tweak", tr("Tweak   (T)"), tr("Not built yet: shows the steps it will take")},
             {"menu_fillet", tr("Fillet   (F)"), {}},
             {"menu_chamfer", tr("Chamfer   (C)"), {}},
             {"menu_splitface", tr("Split Face"), tr("Not built yet: shows the steps it will take")},
             {"menu_splitsolid", tr("Split Solid"), {}},
             {"menu_shell", tr("Shell"), tr("Not built yet: shows the steps it will take")}});
    addMenu("combineButton", Icon::Combine, tr("Combine"), "combineMenu",
            {{"menu_merge", tr("Merge"), {}}, {"menu_subtract", tr("Subtract"), {}}, {"menu_intersect", tr("Intersect"), {}}, {"menu_separate", tr("Separate"), {}}});
    addMenu("snapButton", Icon::Snap, tr("Snap and assemble"), "snapMenu", {{"menu_snap", tr("Snap"), {}}, {"menu_joint", tr("Joint"), tr("Not built yet: shows the steps it will take")}});
    addMenu("groupButton", Icon::Group, tr("Grouping"), "groupMenu",
            {{"menu_group", tr("Group   (Ctrl+G)"), {}}, {"menu_ungroup", tr("Ungroup   (Ctrl+Shift+G)"), {}}, {"menu_ungroupall", tr("Ungroup All"), {}}});
    addMenu("patternButton", Icon::Pattern, tr("Pattern"), "patternMenu",
            {{"menu_rectpattern", tr("Rectangular Pattern"), {}},
             {"menu_circpattern", tr("Circular Pattern"), {}},
             {"menu_pathpattern", tr("Path Pattern"), {}},
             {"menu_mirror", tr("Mirror"), {}}});

    shortcut(tr("Cancel"), QKeySequence(Qt::Key_Escape), [this] {
        if (placement_->active()) placement_->cancel();
        else if (modify_->active()) modify_->cancel();
        else if (transform_->active()) transform_->cancel();
        else if (steps_->active()) steps_->cancel();
        else if (alignDialog_) alignDialog_->reject();
        else if (selectAction_->isChecked()) selectAction_->setChecked(false);
        else if (!selection_->leaveGroup()) selection_->clear();
    });
    for (Qt::Key key : {Qt::Key_Return, Qt::Key_Enter})
        shortcut(tr("Apply"), QKeySequence(key), [this] {
            if (steps_->active()) steps_->confirm();
            // Fillet, chamfer and press pull: their box applies on Enter too, but after a
            // drag or a click on the model the keys are with the 3D view, which lands here.
            else if (modify_->active()) {
                if (!modify_->commitIfChanged()) modify_->cancel();
            }
            else if (transform_->active()) transform_->commit();
            else if (placement_->active()) placement_->placeNow();  // parts with no size boxes, such as nuts
        });
    shortcut(tr("Chamfer"), QKeySequence(Qt::Key_C), [this] { startModify(td::ModifyKind::Chamfer); });
    shortcut(tr("Fillet"), QKeySequence(Qt::Key_E), [this] { startModify(td::ModifyKind::Fillet); });
    shortcut(tr("Press Pull"), QKeySequence(Qt::Key_P), [this] { startModify(td::ModifyKind::Extrude); });
    shortcut(tr("Tweak"), QKeySequence(Qt::Key_T), [this] { startPlanned("tweak"); });
    shortcut(tr("Move / Rotate"), QKeySequence(Qt::Key_M), [this] { startTransform(TransformMode::MoveRotate); });
    shortcut(tr("Scale"), QKeySequence(Qt::Key_S), [this] { startTransform(TransformMode::Scale); });
    shortcut(tr("Group"), QKeySequence(Qt::CTRL | Qt::Key_G), [this] { groupSelection(); });
    shortcut(tr("Ungroup"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this] { ungroupSelection(false); });
    shortcut(tr("Select all"), QKeySequence::SelectAll, [this] {
        std::vector<int> all;
        for (int b = 0; b < int(document_.bodies.size()); ++b)
            if (td::inContext(document_.bodies[b], selection_->groupContext())) all.push_back(b);
        selection_->selectBodies(all);
    });
    shortcut(tr("Delete selected"), QKeySequence::Delete, [this] { removeBodies(selection_->selectedBodies()); });
    shortcut(tr("Copy"), QKeySequence::Copy, [this] { copySelection(false); });
    shortcut(tr("Cut"), QKeySequence::Cut, [this] { copySelection(true); });
    shortcut(tr("Paste"), QKeySequence::Paste, [this] {
        // At the pointer when it is over the workspace, otherwise just beside the originals.
        const QPoint pointer = viewport_->mapFromGlobal(QCursor::pos());
        paste(viewport_->rect().contains(pointer) ? onBed(pointer) : std::nullopt);
    });
    shortcut(tr("Drop"), QKeySequence(Qt::Key_D), [this] { dropSelection(); });

    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // F fillets when edges or points are selected, and fits the view the rest of the time.
    shortcut(tr("Fit"), QKeySequence(Qt::Key_F), [this] {
        if (selection_->subKind() == SubKind::Edge || selection_->subKind() == SubKind::Vertex) startModify(td::ModifyKind::Fillet);
        else fitView();
    });

    QAction* showDesign = designDock_->toggleViewAction();
    showDesign->setIcon(makeIcon(Icon::DesignPanel, ink));
    showDesign->setObjectName("showDesignAction");
    showDesign->setToolTip(tr("Show or hide the design panel"));
    toolbar->addAction(showDesign);

    QAction* showAi = aiDock_->toggleViewAction();
    showAi->setIcon(makeIcon(Icon::AiPanel, ink));
    showAi->setObjectName("showAiAction");
    showAi->setToolTip(tr("Show or hide the AI assistant"));
    toolbar->addAction(showAi);
}

void MainWindow::runMenu(const QString& id) {
    if (id == "drop") {
        dropSelection();
        return;
    }
    if (id == "menu_move") startTransform(TransformMode::MoveRotate);
    else if (id == "menu_scale") startTransform(TransformMode::Scale);
    else if (id == "menu_nonuniform") startTransform(TransformMode::NonUniform);
    else if (id == "menu_smart") startTransform(TransformMode::SmartScale);
    else if (id == "menu_align") startAlign();
    else if (id == "menu_tweak") startPlanned("tweak");
    else if (id == "menu_splitface") startPlanned("splitface");
    else if (id == "menu_shell") startPlanned("shell");
    else if (id == "menu_joint") startPlanned("joint");
    else if (id == "menu_presspull") startModify(td::ModifyKind::Extrude);
    else if (id == "menu_fillet") startModify(td::ModifyKind::Fillet);
    else if (id == "menu_chamfer") startModify(td::ModifyKind::Chamfer);
    else if (id == "menu_splitsolid") startSteps("split");
    else if (id == "menu_merge") startSteps("merge");
    else if (id == "menu_subtract") startSteps("subtract");
    else if (id == "menu_intersect") startSteps("intersect");
    else if (id == "menu_separate") startSteps("separate");
    else if (id == "menu_snap") startSteps("snap");
    else if (id == "menu_group") groupSelection();
    else if (id == "menu_ungroup") ungroupSelection(false);
    else if (id == "menu_ungroupall") ungroupSelection(true);
    else if (id == "menu_rectpattern") startPattern(PatternKind::Rectangular);
    else if (id == "menu_circpattern") startPattern(PatternKind::Circular);
    else if (id == "menu_pathpattern") startPattern(PatternKind::Path);
    else if (id == "menu_mirror") startPattern(PatternKind::Mirror);
}

void MainWindow::cancelTools() {
    placement_->cancel();
    modify_->cancel();
    transform_->cancel();
    steps_->cancel();
    pattern_->end();
    if (alignDialog_) alignDialog_->reject();
}

// ---- Bodies and history -------------------------------------------------

void MainWindow::pushUndo() {
    undoStack_.push_back(document_);
    if (undoStack_.size() > kUndoLimit) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
    undoAction_->setEnabled(true);
    redoAction_->setEnabled(false);
}

void MainWindow::refreshPanel(const std::vector<int>& selectBodies) {
    std::vector<FeaturePanel::Row> rows;
    for (const td::Body& body : document_.bodies) {
        const bool editable = body.features.size() == 1 && isPrimitive(body.features[0].shape);
        Feature shown;
        if (editable) shown = body.features[0];
        else shown.shape = td::Shape::Compound;
        shown.name = body.name;
        rows.push_back({shown, editable, int(body.groups.size())});
    }
    panel_->setRows(rows, selectBodies);
}

void MainWindow::afterChange(const std::vector<int>& selectBodies) {
    lastEditedIndex_ = -1;
    refreshPanel(selectBodies);
    markModified();
    rebuild();
}

void MainWindow::rebuild(bool fitView) {
    std::vector<td::EvalResult> results;
    QString error;
    if (!evaluator_.evaluate(document_, &results, &error)) {
        // Keep showing the last good model, but block exporting it.
        meshCurrent_ = false;
        stats_->setText(tr("Problem: %1").arg(error));
        stats_->setStyleSheet("color: #e06c5c;");
        return;
    }
    meshCurrent_ = true;
    bodyResults_ = std::move(results);

    std::vector<td::Mesh> meshes;
    std::vector<QVector4D> colors;
    mesh_ = {};
    for (size_t b = 0; b < bodyResults_.size(); ++b) {
        const td::Body& body = document_.bodies[b];
        if (body.hidden) {
            meshes.push_back({});
            colors.push_back({});
            continue;
        }
        const td::Mesh& mesh = bodyResults_[b].mesh;
        meshes.push_back(mesh);
        colors.push_back(body.color ? QVector4D(((body.color >> 16) & 0xFF) / 255.0f, ((body.color >> 8) & 0xFF) / 255.0f, (body.color & 0xFF) / 255.0f, 1)
                                    : bodyColor(body.id));
        const auto offset = uint32_t(mesh_.vertexCount());
        mesh_.positions.insert(mesh_.positions.end(), mesh.positions.begin(), mesh.positions.end());
        for (uint32_t index : mesh.indices) mesh_.indices.push_back(index + offset);
    }
    viewport_->setBodies(meshes, colors);
    selection_->meshChanged();

    combined_ = evaluator_.combined(document_);
    if (!combined_.ok || combined_.mesh.empty()) {
        stats_->setText(document_.bodies.empty() ? tr("Empty design") : !combined_.ok ? tr("Problem: %1").arg(combined_.error) : tr("The bodies leave nothing to print"));
        stats_->setStyleSheet(combined_.ok ? "" : "color: #e06c5c;");
        if (fitView) viewport_->fitBed();
        return;
    }

    const td::EvalResult& r = combined_;
    const double halfW = bedWidth_->value() / 2.0;
    const double halfD = bedDepth_->value() / 2.0;
    const bool offBed = r.min[0] < -halfW || r.max[0] > halfW || r.min[1] < -halfD || r.max[1] > halfD;
    const bool belowBed = r.min[2] < -0.001;
    QString text = tr("%1 x %2 x %3 mm   |   %4 cm³   |   %5 %6")
                       .arg(r.max[0] - r.min[0], 0, 'f', 2)
                       .arg(r.max[1] - r.min[1], 0, 'f', 2)
                       .arg(r.max[2] - r.min[2], 0, 'f', 2)
                       .arg(r.volume / 1000.0, 0, 'f', 2)
                       .arg(document_.bodies.size())
                       .arg(document_.bodies.size() == 1 ? tr("body") : tr("bodies"));
    const auto hidden = std::count_if(document_.bodies.begin(), document_.bodies.end(), [](const td::Body& b) { return b.hidden; });
    if (hidden) text += tr("   |   %1 hidden").arg(hidden);
    if (offBed) text += tr("   |   Extends past the bed");
    if (belowBed) text += tr("   |   Goes below the bed");
    stats_->setText(text);
    stats_->setStyleSheet(offBed || belowBed ? "color: #e0a84c;" : "");
    if (fitView) viewport_->fit(r.min, r.max);
}

void MainWindow::updateTitle() {
    const QString name = path_.isEmpty() ? tr("Untitled") : QFileInfo(path_).fileName();
    setWindowTitle(QStringLiteral("%1[*] - Cubra").arg(name));
    setWindowModified(modified_);
}

void MainWindow::markModified() {
    modified_ = true;
    updateTitle();
}

void MainWindow::restore(const td::Document& document) {
    cancelTools();
    document_ = document;
    std::vector<int> selected;
    for (int row : selection_->selectedBodies())
        if (row < int(document_.bodies.size())) selected.push_back(row);
    undoAction_->setEnabled(!undoStack_.empty());
    redoAction_->setEnabled(!redoStack_.empty());
    afterChange(selected);
}

void MainWindow::undo() {
    if (undoStack_.empty()) return;
    redoStack_.push_back(document_);
    const td::Document previous = std::move(undoStack_.back());
    undoStack_.pop_back();
    restore(previous);
}

void MainWindow::redo() {
    if (redoStack_.empty()) return;
    undoStack_.push_back(document_);
    const td::Document next = std::move(redoStack_.back());
    redoStack_.pop_back();
    restore(next);
}

void MainWindow::addPrimitive(const Feature& feature, const QString& label) {
    pushUndo();
    Feature shape = feature;
    shape.name = td::nextName(document_, label);
    td::addBody(&document_, shape.name, {shape});
    afterChange({});
}

void MainWindow::removeBodies(std::vector<int> bodies) {
    if (bodies.empty()) return;
    cancelTools();
    pushUndo();
    std::sort(bodies.rbegin(), bodies.rend());
    bodies.erase(std::unique(bodies.begin(), bodies.end()), bodies.end());
    for (int b : bodies) document_.bodies.erase(document_.bodies.begin() + b);
    afterChange({});
}

int MainWindow::aiTargetBody() const {
    const std::vector<int> rows = selection_->selectedBodies();
    if (rows.size() == 1) return rows.front();
    if (document_.bodies.size() == 1) return 0;
    return -1;
}

void MainWindow::applyAiDesign(const Features& features, bool fresh) {
    pushUndo();
    const int target = fresh ? -1 : aiTargetBody();
    if (target < 0) {
        // Beside whatever is already on the bed, not on top of it.
        double othersRight = -1e30;
        for (int b = 0; b < int(document_.bodies.size()) && b < int(bodyResults_.size()); ++b)
            if (!document_.bodies[b].hidden && !bodyResults_[b].mesh.empty()) othersRight = std::max(othersRight, bodyResults_[b].max[0]);
        td::Affine place;
        const td::EvalResult made = td::evaluate(features);
        if (othersRight > -1e29 && made.ok && !made.mesh.empty()) place = td::Affine::translation({othersRight + 10 - made.min[0], 0, 0});
        td::addBody(&document_, td::nextName(document_, tr("AI part")), features, place);
    } else {
        // The AI never sees direct edits or merged parts, so they carry over onto what it returns.
        td::Body& body = document_.bodies[target];
        Features kept = features;
        for (const Feature& existing : body.features)
            if (!isPrimitive(existing.shape)) kept.push_back(existing);
        body.features = std::move(kept);
    }
    afterChange({});
    rebuild(true);
    statusBar()->showMessage(tr("Applied the AI design. Press Ctrl+Z to undo it."), 8000);
}

// ---- Direct edits ---------------------------------------------------------

void MainWindow::startModify(td::ModifyKind kind) {
    remember(kind == td::ModifyKind::Chamfer ? "menu_chamfer" : kind == td::ModifyKind::Extrude ? "menu_presspull" : "menu_fillet");
    // Fillet applies to whichever of edges or points is selected.
    if (kind == td::ModifyKind::Fillet && selection_->subKind() == SubKind::Vertex) kind = td::ModifyKind::PointFillet;
    const SubKind need = kind == td::ModifyKind::Extrude ? SubKind::Face : kind == td::ModifyKind::PointFillet ? SubKind::Vertex : SubKind::Edge;
    if (selection_->subKind() != need || selection_->subIds().empty()) {
        using Pick = StepTool::Pick;
        const QString more = tr("Shift-click more edges afterwards to work on them together.");
        if (kind == td::ModifyKind::Extrude) askFor("then_presspull", tr("Press Pull"), {{tr("Flat face to push or pull"), Pick::Face}}, false);
        else if (kind == td::ModifyKind::Chamfer) askFor("then_chamfer", tr("Chamfer"), {{tr("Edge to bevel"), Pick::Edge}}, false, more);
        else askFor("then_fillet", tr("Fillet"), {{tr("Edge to round"), Pick::Edge}}, false, more);
        return;
    }
    if (modify_->active() && modify_->kind() == kind) return;  // already running on this selection

    const int owner = selection_->subOwner();
    const td::Topology& topology = selection_->topology();
    for (int id : selection_->subIds()) {
        if (kind == td::ModifyKind::Extrude && !topology.faces[id].planar) {
            statusBar()->showMessage(tr("Press Pull works on flat faces only for now."), 5000);
            return;
        }
    }

    placement_->cancel();
    transform_->cancel();
    steps_->cancel();
    modifyOwner_ = owner;
    const td::Affine toBody = document_.bodies[owner].transform.inverse();
    modify_->begin(kind, bodyResults_[owner].mesh, topology, selection_->subIds(), [this, owner, toBody](const Features& candidates) {
        // Edits are worked out in world space, and kept in the body's own space so they move with it.
        Features local = candidates;
        for (Feature& f : local) f.matrix = toBody * f.matrix;
        const td::EvalResult result = evaluator_.evaluateBody(document_.bodies[owner], local);
        if (!result.ok || result.mesh.empty()) return false;
        viewport_->setBodyMesh(owner, result.mesh);
        return true;
    });
}

void MainWindow::onSubSelectionChanged() {
    const SubKind kind = selection_->subKind();
    if (modify_->active()) {
        const SubKind toolKind = modify_->kind() == td::ModifyKind::Extrude ? SubKind::Face
                                 : modify_->kind() == td::ModifyKind::PointFillet ? SubKind::Vertex
                                                                                  : SubKind::Edge;
        if (kind == toolKind && selection_->subOwner() == modifyOwner_) {
            modify_->retarget(selection_->subIds());
            return;
        }
        modify_->cancel();
    }
    if (kind != SubKind::Face || placement_->active() || transform_->active() || steps_->active()) return;

    // Selecting a face is enough to start dragging it.
    const td::Topology& topology = selection_->topology();
    for (int id : selection_->subIds())
        if (!topology.faces[id].planar) return;
    startModify(td::ModifyKind::Extrude);
}

void MainWindow::commitModify(td::ModifyKind kind, const Features& additions) {
    selection_->setPreviewing(false);
    if (modifyOwner_ < 0 || modifyOwner_ >= int(document_.bodies.size())) return;
    pushUndo();
    td::Body& body = document_.bodies[modifyOwner_];
    const td::Affine toBody = body.transform.inverse();
    const QString label = kind == td::ModifyKind::Chamfer ? tr("Chamfer") : kind == td::ModifyKind::Extrude ? tr("Press Pull") : tr("Fillet");
    for (Feature feature : additions) {
        feature.matrix = toBody * feature.matrix;
        feature.name = label;
        body.features.push_back(std::move(feature));
    }
    afterChange({});
}

// ---- Clicks -----------------------------------------------------------------

void MainWindow::routeClick(QPoint position, bool shift) {
    if (steps_->active()) {
        offerStep(position);
        return;
    }
    // Without Shift, a click away finishes the edit in progress before selecting.
    if (transform_->active()) transform_->commit();
    if (modify_->active() && !shift && !modify_->commitIfChanged()) modify_->cancel();
    selection_->handleClick(position, shift);
}

void MainWindow::routeDoubleClick(QPoint position) {
    if (steps_->active() || transform_->active() || modify_->active()) return;
    selection_->handleDoubleClick(position);
}

void MainWindow::runGearTool(const QString& tool) {
    if (tool == "Fillet") startModify(td::ModifyKind::Fillet);
    else if (tool == "Chamfer") startModify(td::ModifyKind::Chamfer);
    else if (tool == "Extrude" || tool == "Press Pull") startModify(td::ModifyKind::Extrude);
    else if (tool == "Move") startTransform(TransformMode::MoveRotate);
    else if (tool == "Scale") startTransform(TransformMode::Scale);
    else if (tool == "Delete") removeBodies(selection_->selectedBodies());
    else if (tool == "Tweak") startPlanned("tweak");
    else if (tool == "Split Face") startPlanned("splitface");
    else if (tool == "Shell") startPlanned("shell");
}

// ---- Transform --------------------------------------------------------------

std::vector<int> MainWindow::targetBodies() const {
    std::vector<int> bodies = selection_->selectedBodies();
    if (bodies.empty() && selection_->subOwner() >= 0) {
        // A face or edge stands for its whole body here, and its group with it.
        bodies = td::unitOf(document_, selection_->subOwner(), selection_->groupContext());
    }
    return bodies;
}

void MainWindow::startTransform(TransformMode mode) {
    remember(mode == TransformMode::MoveRotate ? "menu_move" : mode == TransformMode::Scale ? "menu_scale" : mode == TransformMode::NonUniform ? "menu_nonuniform" : "menu_smart");
    const std::vector<int> targets = targetBodies();
    if (targets.empty()) {
        const bool moving = mode == TransformMode::MoveRotate;
        const QString action = moving ? "move" : mode == TransformMode::Scale ? "scale" : mode == TransformMode::NonUniform ? "nonuniform" : "smart";
        askFor("then_" + action, menuLabel("menu_" + action), {{moving ? tr("Solid to move") : tr("Solid to scale"), StepTool::Pick::Body}}, false);
        return;
    }
    cancelTools();
    std::array<double, 3> low{1e30, 1e30, 1e30}, high{-1e30, -1e30, -1e30};
    for (int b : targets)
        for (int a = 0; a < 3; ++a) {
            low[a] = std::min(low[a], bodyResults_[b].min[a]);
            high[a] = std::max(high[a], bodyResults_[b].max[a]);
        }
    transformTargets_ = targets;
    selection_->selectBodies(targets);
    selection_->setPreviewing(true);
    // The outline would stay behind at the old position while the parts move.
    viewport_->setOverlay({});
    viewport_->setOverlayOutline({});
    transform_->begin(mode, low, high, [this](const td::Affine& change) {
        const QMatrix4x4 matrix = toMatrix(change);
        for (int b : transformTargets_) viewport_->setBodyPreview(b, matrix);
    });
}

void MainWindow::commitTransform(const td::Affine& change) {
    viewport_->clearBodyPreviews();
    selection_->setPreviewing(false);
    pushUndo();
    td::transformBodies(&document_, transformTargets_, change);
    afterChange(transformTargets_);
}

// ---- Dragging selected parts along the grid ----------------------------------

bool MainWindow::canGrab(QPoint position) const {
    // Only a part already selected moves, so dragging across parts still turns the view.
    if (placement_->active() || modify_->active() || transform_->active() || steps_->active()) return false;
    if (selection_->subKind() != SubKind::None) return false;
    const auto hit = selection_->hitAt(position);
    if (!hit) return false;
    const std::vector<int> selected = selection_->selectedBodies();
    return std::find(selected.begin(), selected.end(), hit->body) != selected.end();
}

void MainWindow::startGrab(QPoint position) {
    const auto hit = selection_->hitAt(position);
    if (!hit) return;
    grabTargets_ = selection_->selectedBodies();
    grabPoint_ = hit->hit.point;
    grabShift_ = {};
    selection_->setPreviewing(true);
    viewport_->setOverlay({});
    viewport_->setOverlayOutline({});
}

void MainWindow::moveGrab(QPoint position) {
    if (grabTargets_.empty()) return;
    // Slide in the plane through the grabbed point, parallel to the bed.
    td::V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    if (std::abs(direction.z) < 1e-4) return;
    const double t = (grabPoint_.z - origin.z) / direction.z;
    if (t <= 0) return;
    const td::V3 onPlane = origin + direction * t;
    grabShift_ = {snapLength(onPlane.x - grabPoint_.x, snapSettings().grid), snapLength(onPlane.y - grabPoint_.y, snapSettings().grid), 0};
    const QMatrix4x4 matrix = toMatrix(td::Affine::translation(grabShift_));
    for (int b : grabTargets_) viewport_->setBodyPreview(b, matrix);
    statusBar()->showMessage(tr("Moving X %1 mm, Y %2 mm").arg(grabShift_.x).arg(grabShift_.y));
}

void MainWindow::finishGrab(QPoint position) {
    moveGrab(position);
    const std::vector<int> targets = std::move(grabTargets_);
    grabTargets_.clear();
    viewport_->clearBodyPreviews();
    selection_->setPreviewing(false);
    statusBar()->clearMessage();
    if (targets.empty() || (grabShift_.x == 0 && grabShift_.y == 0)) {
        selection_->refreshGraphics();
        return;
    }
    pushUndo();
    td::transformBodies(&document_, targets, td::Affine::translation(grabShift_));
    afterChange(targets);
}

// ---- Combine, split, separate and snap ---------------------------------------

void MainWindow::startSteps(const QString& id) {
    remember(id == "split" ? QStringLiteral("menu_splitsolid") : "menu_" + id);
    std::vector<int> selected = selection_->selectedBodies();
    if (selected.empty() && selection_->subOwner() >= 0) selected = {selection_->subOwner()};
    const auto choiceFor = [this](int body) { return StepTool::Choice{body, -1, {}, {}, document_.bodies[body].name}; };
    cancelTools();

    using Pick = StepTool::Pick;
    if (id == "merge" || id == "subtract" || id == "intersect") {
        const QString title = id == "merge" ? tr("Merge") : id == "subtract" ? tr("Subtract") : tr("Intersect");
        const QString sources = id == "subtract" ? tr("Cutting solids") : tr("Other solids");
        steps_->begin(id, title, {{tr("Solid to keep"), Pick::Body}, {sources, Pick::Bodies}}, true);
        if (!selected.empty()) {
            steps_->preset(0, {choiceFor(selected.front())});
            std::vector<StepTool::Choice> rest;
            for (size_t i = 1; i < selected.size(); ++i) rest.push_back(choiceFor(selected[i]));
            if (!rest.empty()) steps_->preset(1, rest);
        }
    } else if (id == "separate") {
        if (selected.size() == 1) {
            finishSteps(id, {{choiceFor(selected.front())}});
            return;
        }
        steps_->begin(id, tr("Separate"), {{tr("Solid with loose pieces"), Pick::Body}}, false);
    } else if (id == "split") {
        steps_->begin(id, tr("Split Solid"), {{tr("Solid to split"), Pick::Body}, {tr("Flat face to split along"), Pick::Face}}, false);
        if (selected.size() == 1) steps_->preset(0, {choiceFor(selected.front())});
    } else if (id == "snap") {
        steps_->begin(id, tr("Snap"), {{tr("Face on the part to move"), Pick::Face}, {tr("Face to snap it onto"), Pick::Face}}, false);
    }
    selection_->clear();
}

void MainWindow::offerStep(QPoint position) {
    const auto hit = selection_->hitAt(position);
    using Pick = StepTool::Pick;
    const Pick pick = steps_->currentPick();

    // Edges can be picked just off the solid, where half of an outline edge lies.
    int body = hit ? hit->body : -1;
    int edge = -1;
    if (pick == Pick::Edge || pick == Pick::Axis) {
        for (int b = 0; edge < 0 && b < int(bodyResults_.size()); ++b) {
            const int candidate = body >= 0 ? body : b;
            edge = selection_->edgeNear(candidate, position);
            if (edge >= 0) body = candidate;
            if (hit) break;
        }
    }
    if (body < 0) {
        steps_->setMessage(tr("That click missed every solid."));
        return;
    }
    StepTool::Choice choice{body, -1, {}, {}, document_.bodies[body].name};
    const td::Mesh& mesh = bodyResults_[body].mesh;
    const td::Topology& topology = selection_->topologyOf(body);

    if (pick == Pick::Edge || pick == Pick::Axis) {
        td::V3 centre, axis;
        double radius = 0;
        if (edge >= 0) {
            const td::TopoEdge& e = topology.edges[edge];
            for (uint32_t v : e.vertices) choice.path.push_back(td::meshVertex(mesh, v));
            choice.closed = e.closed;
            choice.edge = edge;
            choice.point = choice.path.front();
            choice.normal = td::normalized(choice.path.back() - choice.path.front());
            choice.name = tr("an edge of %1").arg(document_.bodies[body].name);
        }
        if (pick == Pick::Axis) {
            if (edge >= 0 && td::edgeCircle(mesh, topology.edges[edge], &centre, &axis, &radius)) {
                choice.point = centre;
                choice.normal = axis;
                choice.name = tr("the centre of a round edge of %1").arg(document_.bodies[body].name);
            } else if (edge >= 0 && !choice.closed && td::length(choice.normal) > 0) {
                choice.name = tr("a straight edge of %1").arg(document_.bodies[body].name);
            } else if (hit) {
                // A face: a round one turns about its own axis, a flat one about its normal.
                const int face = topology.faceOfTriangle[hit->hit.triangle];
                bool found = false;
                for (const td::TopoEdge& e : topology.edges) {
                    if (!e.closed || topology.faceOfTriangle[e.segmentTriangles[0][0]] != face && topology.faceOfTriangle[e.segmentTriangles[0][1]] != face) continue;
                    if (td::edgeCircle(mesh, e, &centre, &axis, &radius)) {
                        choice.point = centre;
                        choice.normal = axis;
                        found = true;
                        break;
                    }
                }
                if (!found && topology.faces[face].planar) {
                    choice.point = topology.faces[face].centroid;
                    choice.normal = topology.faces[face].normal;
                    found = true;
                }
                if (!found) {
                    steps_->setMessage(tr("Pick a round edge, a straight edge, or a cylinder's side."));
                    return;
                }
                choice.name = tr("the axis of a face of %1").arg(document_.bodies[body].name);
            } else {
                steps_->setMessage(tr("Pick a round edge, a straight edge, or a cylinder's side."));
                return;
            }
        } else if (edge < 0) {
            steps_->setMessage(tr("Click on an edge."));
            return;
        }
    } else if (pick == Pick::Face) {
        if (!hit) {
            steps_->setMessage(tr("That click missed every solid."));
            return;
        }
        const int face = topology.faceOfTriangle[hit->hit.triangle];
        if (!topology.faces[face].planar) {
            steps_->setMessage(tr("Pick a flat face."));
            return;
        }
        choice.face = face;
        choice.point = topology.faces[face].centroid;
        choice.normal = topology.faces[face].normal;
        choice.name = tr("a face of %1").arg(document_.bodies[body].name);
    }
    steps_->setMessage({});
    steps_->offer(choice);
}

void MainWindow::showStepPicks() {
    static const QVector4D kColors[] = {{0.25f, 0.78f, 1.0f, 0.45f}, {1.0f, 0.55f, 0.20f, 0.45f}};
    std::vector<Viewport::TintedMesh> highlights;
    const auto& picks = steps_->picks();
    for (size_t step = 0; step < picks.size(); ++step) {
        for (const StepTool::Choice& c : picks[step]) {
            if (c.body < 0 || c.body >= int(bodyResults_.size())) continue;
            const td::Mesh& mesh = bodyResults_[c.body].mesh;
            if (c.face < 0) {
                highlights.push_back({mesh, kColors[std::min<size_t>(step, 1)]});
                continue;
            }
            td::Mesh face;
            for (uint32_t t : selection_->topologyOf(c.body).faces[c.face].triangles)
                for (int i = 0; i < 3; ++i) {
                    const td::V3 p = td::meshVertex(mesh, mesh.indices[t * 3 + i]);
                    face.positions.insert(face.positions.end(), {float(p.x), float(p.y), float(p.z)});
                    face.indices.push_back(uint32_t(face.indices.size()));
                }
            highlights.push_back({face, kColors[std::min<size_t>(step, 1)]});
        }
    }
    // See-through copies wherever the pattern will put them.
    if (steps_->active() && pattern_->active() && !picks.empty()) {
        std::array<double, 3> low{1e30, 1e30, 1e30}, high{-1e30, -1e30, -1e30};
        for (const StepTool::Choice& c : picks[0])
            for (int a = 0; a < 3; ++a) {
                low[a] = std::min(low[a], bodyResults_[c.body].min[a]);
                high[a] = std::max(high[a], bodyResults_[c.body].max[a]);
            }
        if (!picks[0].empty()) pattern_->setOrigin({(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, (low[2] + high[2]) / 2});
        td::Mesh ghosts;
        for (const td::Affine& placement : patternPlacements())
            for (const StepTool::Choice& c : picks[0]) {
                const td::Mesh& mesh = bodyResults_[c.body].mesh;
                const auto offset = uint32_t(ghosts.vertexCount());
                for (size_t v = 0; v < mesh.vertexCount(); ++v) {
                    const td::V3 p = placement.point(td::meshVertex(mesh, uint32_t(v)));
                    ghosts.positions.insert(ghosts.positions.end(), {float(p.x), float(p.y), float(p.z)});
                }
                for (uint32_t index : mesh.indices) ghosts.indices.push_back(index + offset);
            }
        highlights.push_back({ghosts, QVector4D(0.55f, 0.82f, 1.0f, 0.35f)});
    }
    viewport_->setHighlights(steps_->active() ? highlights : std::vector<Viewport::TintedMesh>{});
}

std::vector<td::Affine> MainWindow::patternPlacements() const {
    const auto& picks = steps_->picks();
    if (!pattern_->active() || picks.size() < 2 || picks[0].empty() || picks[1].empty()) return {};
    const StepTool::Choice& guide = picks[1][0];
    switch (pattern_->kind()) {
        case PatternKind::Rectangular: {
            td::RectangularPattern p = pattern_->rectangular;
            p.along = guide.normal;
            return td::rectangularPattern(p);
        }
        case PatternKind::Circular: {
            td::CircularPattern p = pattern_->circular;
            p.point = guide.point;
            p.axis = guide.normal;
            return td::circularPattern(p);
        }
        case PatternKind::Path: {
            td::PathPattern p = pattern_->path;
            p.path = guide.path;
            p.closed = guide.closed;
            return td::pathPattern(p);
        }
        case PatternKind::Mirror: return {td::mirrorAcross(guide.point, guide.normal)};
    }
    return {};
}

void MainWindow::startPattern(PatternKind kind) {
    remember(kind == PatternKind::Rectangular ? "menu_rectpattern" : kind == PatternKind::Circular ? "menu_circpattern" : kind == PatternKind::Path ? "menu_pathpattern" : "menu_mirror");
    std::vector<int> selected = selection_->selectedBodies();
    if (selected.empty() && selection_->subOwner() >= 0) selected = {selection_->subOwner()};
    cancelTools();
    using Pick = StepTool::Pick;
    std::vector<StepTool::Choice> solids;
    for (int b : selected) solids.push_back({b, -1, {}, {}, document_.bodies[b].name});

    switch (kind) {
        case PatternKind::Rectangular:
            steps_->begin("pattern", tr("Rectangular Pattern"), {{tr("Solids"), Pick::Bodies}, {tr("Direction: click a straight edge"), Pick::Edge}}, true);
            // Until an edge is picked, the rows run along the bed's X axis.
            steps_->preset(1, {{-1, -1, {}, {1, 0, 0}, tr("the X axis")}});
            break;
        case PatternKind::Circular:
            steps_->begin("pattern", tr("Circular Pattern"), {{tr("Solids"), Pick::Bodies}, {tr("Axis: click a round edge, straight edge or cylinder"), Pick::Axis}}, true);
            steps_->preset(1, {{-1, -1, {}, {0, 0, 1}, tr("upright through the bed centre")}});
            break;
        case PatternKind::Path:
            steps_->begin("pattern", tr("Path Pattern"), {{tr("Solids"), Pick::Bodies}, {tr("Path: click an edge to follow"), Pick::Edge}}, true);
            break;
        case PatternKind::Mirror:
            steps_->begin("pattern", tr("Mirror"), {{tr("Solids"), Pick::Bodies}, {tr("Mirror plane: click a flat face"), Pick::Face}}, true);
            break;
    }
    if (!solids.empty()) steps_->preset(0, solids);
    pattern_->begin(kind);
    selection_->clear();
    showStepPicks();
}



// ---- Tools that ask for what they work on --------------------------------------

void MainWindow::askFor(const QString& id, const QString& title, std::vector<StepTool::Step> steps, bool confirm, const QString& note) {
    cancelTools();
    selection_->clear();
    steps_->begin(id, title, std::move(steps), confirm);
    if (!note.isEmpty()) steps_->setNote(note);
}

void MainWindow::finishAsked(const QString& action, const std::vector<std::vector<StepTool::Choice>>& picks) {
    const StepTool::Choice& first = picks[0][0];
    if (action == "fillet" || action == "chamfer") {
        if (first.edge < 0) return;
        selection_->selectSub(first.body, SubKind::Edge, {first.edge});
        startModify(action == "fillet" ? td::ModifyKind::Fillet : td::ModifyKind::Chamfer);
        return;
    }
    if (action == "presspull") {
        if (first.face < 0) return;
        selection_->selectSub(first.body, SubKind::Face, {first.face});
        startModify(td::ModifyKind::Extrude);
        return;
    }
    // Everything else works on whole solids, each with the rest of its group.
    std::vector<int> bodies;
    for (const StepTool::Choice& c : picks[0])
        for (int m : td::unitOf(document_, c.body, selection_->groupContext()))
            if (std::find(bodies.begin(), bodies.end(), m) == bodies.end()) bodies.push_back(m);
    if (bodies.empty()) return;
    selection_->selectBodies(bodies);
    if (action == "move") startTransform(TransformMode::MoveRotate);
    else if (action == "scale") startTransform(TransformMode::Scale);
    else if (action == "nonuniform") startTransform(TransformMode::NonUniform);
    else if (action == "smart") startTransform(TransformMode::SmartScale);
    else if (action == "drop") dropSelection();
    else if (action == "group") groupSelection();
    else if (action == "ungroup") ungroupSelection(false);
    else if (action == "ungroupall") ungroupSelection(true);
    else if (action == "hide") hideBodies(bodies);
}

void MainWindow::startAlign() {
    remember("menu_align");
    const std::vector<int> selected = targetBodies();
    using Pick = StepTool::Pick;
    askFor("align", tr("Align"), {{tr("Object A: the solid to move"), Pick::Body}, {tr("Object B: the solid to line it up with"), Pick::Body}}, true,
           tr("Pick both, press Enter, then choose how they line up."));
    if (!selected.empty()) {
        std::vector<StepTool::Choice> choices;
        for (int b : selected) choices.push_back({b, -1, {}, {}, document_.bodies[b].name});
        steps_->preset(0, choices);
    }
}

void MainWindow::showAlign(std::vector<int> moving, int target) {
    const std::vector<int> targetUnit = td::unitOf(document_, target, selection_->groupContext());
    moving.erase(std::remove_if(moving.begin(), moving.end(), [&](int b) { return std::find(targetUnit.begin(), targetUnit.end(), b) != targetUnit.end(); }), moving.end());
    if (moving.empty()) {
        statusBar()->showMessage(tr("Pick two different solids to align."), 6000);
        return;
    }
    const auto boundsOf = [this](const std::vector<int>& bodies, std::array<double, 3>* low, std::array<double, 3>* high) {
        *low = {1e30, 1e30, 1e30};
        *high = {-1e30, -1e30, -1e30};
        for (int b : bodies)
            for (int a = 0; a < 3; ++a) {
                (*low)[a] = std::min((*low)[a], bodyResults_[b].min[a]);
                (*high)[a] = std::max((*high)[a], bodyResults_[b].max[a]);
            }
    };
    std::array<double, 3> lowA, highA, lowB, highB;
    boundsOf(moving, &lowA, &highA);
    boundsOf(targetUnit, &lowB, &highB);

    alignMoving_ = moving;
    selection_->selectBodies(moving);
    selection_->setPreviewing(true);
    viewport_->setOverlay({});
    viewport_->setOverlayOutline({});

    auto* dialog = new QDialog(this);
    dialog->setObjectName("alignDialog");
    dialog->setWindowTitle(tr("Align %1 with %2").arg(document_.bodies[moving.front()].name, document_.bodies[target].name));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* form = new QFormLayout(dialog);
    form->addRow(new QLabel(tr("Line up <b>%1</b> (A) with <b>%2</b> (B):").arg(document_.bodies[moving.front()].name, document_.bodies[target].name)));

    auto modes = std::make_shared<std::array<td::AlignMode, 3>>(std::array<td::AlignMode, 3>{td::AlignMode::Centre, td::AlignMode::Centre, td::AlignMode::None});
    const auto offset = [=] { return td::alignOffset(lowA, highA, lowB, highB, *modes); };
    const auto preview = [this, offset] {
        const QMatrix4x4 matrix = toMatrix(td::Affine::translation(offset()));
        for (int b : alignMoving_) viewport_->setBodyPreview(b, matrix);
    };
    const char* axes[3] = {"x", "y", "z"};
    const QString names[3][4] = {{tr("Keep"), tr("Left sides"), tr("Centres"), tr("Right sides")},
                                 {tr("Keep"), tr("Front sides"), tr("Centres"), tr("Back sides")},
                                 {tr("Keep"), tr("Bottoms"), tr("Centres"), tr("Tops")}};
    const QString rowNames[3] = {tr("Left / right (X)"), tr("Front / back (Y)"), tr("Up / down (Z)")};
    for (int a = 0; a < 3; ++a) {
        auto* row = new QWidget(dialog);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* group = new QButtonGroup(row);
        for (int m = 0; m < 4; ++m) {
            auto* button = new QToolButton(row);
            button->setObjectName(QStringLiteral("align_%1%2").arg(QLatin1String(axes[a])).arg(m));
            button->setText(names[a][m]);
            button->setCheckable(true);
            button->setChecked(int((*modes)[a]) == m);
            group->addButton(button, m);
            rowLayout->addWidget(button);
        }
        connect(group, &QButtonGroup::idClicked, dialog, [modes, a, preview](int m) {
            (*modes)[a] = td::AlignMode(m);
            preview();
        });
        form->addRow(rowNames[a], row);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Align"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, [this, offset](int result) {
        alignDialog_ = nullptr;
        viewport_->clearBodyPreviews();
        selection_->setPreviewing(false);
        const std::vector<int> moving = std::move(alignMoving_);
        alignMoving_.clear();
        const td::V3 shift = offset();
        if (result != QDialog::Accepted || td::length(shift) < 1e-9) {
            selection_->refreshGraphics();
            return;
        }
        pushUndo();
        td::transformBodies(&document_, moving, td::Affine::translation(shift));
        afterChange(moving);
    });
    alignDialog_ = dialog;
    preview();
    dialog->show();
    // Beside the workspace's top edge, clear of the parts.
    dialog->move(mapToGlobal(QPoint(width() - dialog->width() - 40, 120)));
}

void MainWindow::startPlanned(const QString& id) {
    using Pick = StepTool::Pick;
    struct Plan {
        QString title;
        std::vector<StepTool::Step> steps;
        QString what;
    };
    Plan plan;
    if (id == "tweak") plan = {tr("Tweak"), {{tr("Face to move"), Pick::Face}}, tr("move a face and stretch the faces around it")};
    else if (id == "splitface") plan = {tr("Split Face"), {{tr("Face to split"), Pick::Face}, {tr("Flat face whose plane cuts it"), Pick::Face}}, tr("divide a face so each part can be pushed or pulled on its own")};
    else if (id == "shell") plan = {tr("Shell"), {{tr("Face to open up"), Pick::Face}}, tr("hollow the solid out to a wall thickness, leaving the picked face open")};
    else if (id == "joint") plan = {tr("Joint"), {{tr("Object A: the part that moves"), Pick::Body}, {tr("Object B: the part it joins"), Pick::Body}}, tr("link two parts so one can turn or slide on the other")};
    else if (id == "sketch") plan = {tr("Sketch on Face"), {{tr("Flat face to sketch on"), Pick::Face}}, tr("draw outlines on a face")};
    else return;

    // A face already selected fills the first step.
    std::optional<StepTool::Choice> face;
    if (plan.steps[0].pick == Pick::Face && selection_->subKind() == SubKind::Face && !selection_->subIds().empty()) {
        const int owner = selection_->subOwner();
        const td::TopoFace& f = selection_->topology().faces[selection_->subIds().front()];
        if (f.planar) face = StepTool::Choice{owner, selection_->subIds().front(), f.centroid, f.normal, tr("a face of %1").arg(document_.bodies[owner].name)};
    }
    askFor("planned_" + id, plan.title, plan.steps, true,
           tr("%1 isn't built yet. It will %2. You can make the picks to see how it will work, but nothing will change.").arg(plan.title, plan.what));
    if (face) steps_->preset(0, {*face});
}


void MainWindow::finishSteps(const QString& id, const std::vector<std::vector<StepTool::Choice>>& picks) {
    viewport_->setHighlights({});
    if (id.startsWith("then_")) {
        finishAsked(id.mid(5), picks);
        return;
    }
    if (id.startsWith("planned_")) {
        const QString tool = id.mid(8);
        const QString title = tool == "tweak" ? tr("Tweak") : tool == "splitface" ? tr("Split Face") : tool == "shell" ? tr("Shell") : tool == "joint" ? tr("Joint") : tr("Sketch on Face");
        statusBar()->showMessage(tr("%1 isn't built yet, so nothing was changed.").arg(title), 8000);
        return;
    }
    if (id == "align") {
        std::vector<int> moving;
        for (const StepTool::Choice& c : picks[0])
            for (int m : td::unitOf(document_, c.body, selection_->groupContext()))
                if (std::find(moving.begin(), moving.end(), m) == moving.end()) moving.push_back(m);
        showAlign(std::move(moving), picks[1][0].body);
        return;
    }
    if (id == "pattern") {
        const std::vector<td::Affine> placements = patternPlacements();
        pattern_->end();
        if (placements.empty()) return;
        std::vector<int> solids;
        for (const StepTool::Choice& c : picks[0]) solids.push_back(c.body);
        pushUndo();
        std::vector<int> made = td::addCopies(&document_, solids, placements);
        made.insert(made.end(), solids.begin(), solids.end());
        afterChange(made);
        return;
    }
    if (id == "merge" || id == "subtract" || id == "intersect") {
        const int target = picks[0][0].body;
        std::vector<int> sources;
        for (const StepTool::Choice& c : picks[1]) sources.push_back(c.body);
        const td::Op op = id == "merge" ? td::Op::Add : id == "subtract" ? td::Op::Subtract : td::Op::Intersect;
        pushUndo();
        const int kept = td::combineBodies(&document_, target, sources, op);
        afterChange({kept});
        if (kept < int(bodyResults_.size()) && bodyResults_[kept].mesh.empty())
            statusBar()->showMessage(tr("Nothing is left of %1. Press Ctrl+Z to undo.").arg(document_.bodies[kept].name), 8000);
        return;
    }
    if (id == "separate") {
        const int body = picks[0][0].body;
        const std::vector<td::Mesh> pieces = evaluator_.pieces(document_.bodies[body]);
        if (pieces.size() < 2) {
            statusBar()->showMessage(tr("%1 is already one piece.").arg(document_.bodies[body].name), 6000);
            return;
        }
        pushUndo();
        td::replaceWithPieces(&document_, body, pieces);
        std::vector<int> created(pieces.size());
        std::iota(created.begin(), created.end(), body);
        afterChange(created);
        return;
    }
    if (id == "split") {
        const int body = picks[0][0].body;
        const StepTool::Choice& face = picks[1][0];
        const td::EvalResult& r = bodyResults_[body];
        const double size = 4 * std::hypot(r.max[0] - r.min[0], r.max[1] - r.min[1], r.max[2] - r.min[2]) + 10;
        const QString name = document_.bodies[body].name;
        pushUndo();
        td::splitBody(&document_, body, face.point, face.normal, size);
        afterChange({body, body + 1});
        const bool missed = body + 1 >= int(bodyResults_.size()) || bodyResults_[body].mesh.empty() || bodyResults_[body + 1].mesh.empty();
        if (missed) {
            undo();
            redoStack_.clear();
            redoAction_->setEnabled(false);
            statusBar()->showMessage(tr("That face's plane does not pass through %1.").arg(name), 6000);
        }
        return;
    }
    if (id == "snap") {
        const StepTool::Choice& from = picks[0][0];
        const StepTool::Choice& to = picks[1][0];
        if (from.body == to.body) {
            statusBar()->showMessage(tr("Pick faces on two different solids."), 6000);
            return;
        }
        const std::vector<int> moving = td::unitOf(document_, from.body, selection_->groupContext());
        pushUndo();
        td::transformBodies(&document_, moving, td::snapTransform(from.point, from.normal, to.point, to.normal));
        afterChange(moving);
    }
}

// ---- Around the workspace: fit, grid, pictures and the parts drawer -----------

void MainWindow::fitView() {
    // The selection if there is one, otherwise everything in sight.
    std::vector<int> bodies = selection_->selectedBodies();
    if (bodies.empty() && selection_->subOwner() >= 0) bodies = {selection_->subOwner()};
    if (bodies.empty())
        for (int b = 0; b < int(document_.bodies.size()); ++b)
            if (!document_.bodies[b].hidden) bodies.push_back(b);
    std::array<double, 3> low{1e30, 1e30, 1e30}, high{-1e30, -1e30, -1e30};
    bool any = false;
    for (int b : bodies) {
        if (b >= int(bodyResults_.size()) || bodyResults_[b].mesh.empty()) continue;
        any = true;
        for (int a = 0; a < 3; ++a) {
            low[a] = std::min(low[a], bodyResults_[b].min[a]);
            high[a] = std::max(high[a], bodyResults_[b].max[a]);
        }
    }
    if (any) viewport_->fit(low, high);
    else viewport_->fitBed();
}

bool MainWindow::saveScreenshot(const QString& path) {
    // Only the 3D view: no toolbars, panels or buttons.
    return viewport_->grabFramebuffer().save(path, "PNG");
}

void MainWindow::editGrid() {
    if (!gridDialog_) {
        gridDialog_ = new QDialog(this);
        gridDialog_->setObjectName("gridDialog");
        gridDialog_->setWindowTitle(tr("Edit Grid"));
        auto* form = new QFormLayout(gridDialog_);
        form->addRow(new QLabel(tr("The grid is the printer's build plate."), gridDialog_));
        form->addRow(tr("Width (X)"), bedWidth_);
        form->addRow(tr("Length (Y)"), bedDepth_);
        form->addRow(tr("Units"), new QLabel(tr("millimetres"), gridDialog_));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, gridDialog_);
        connect(buttons, &QDialogButtonBox::rejected, gridDialog_, &QDialog::hide);
        form->addRow(buttons);
    }
    gridDialog_->show();
    gridDialog_->raise();
}

std::optional<std::pair<Feature, QString>> MainWindow::drawerPart(const QString& id) const {
    for (const Preset& preset : presets())
        if (id == QStringLiteral("drawer_") + QString::fromLatin1(preset.id).mid(10)) return std::make_pair(preset.feature, tr(preset.label));
    for (const auto& [itemId, label, feature] : hardware())
        if (id == itemId) return std::make_pair(feature, label);
    return std::nullopt;
}

std::vector<ViewportChrome::DrawerSection> MainWindow::drawerSections() const {
    const QColor ink = palette().color(QPalette::WindowText);
    ViewportChrome::DrawerSection shapes{tr("Primitives"), {}};
    for (const Preset& preset : presets())
        shapes.items.push_back({QStringLiteral("drawer_") + QString::fromLatin1(preset.id).mid(10), tr(preset.label), makeIcon(preset.icon, ink), {}});
    ViewportChrome::DrawerSection parts{tr("Hardware"), {}};
    for (const auto& [id, label, feature] : hardware()) {
        const Icon icon = label.contains("nut") ? Icon::HexPrism : label.contains("gear") || label.contains("Gear") ? Icon::Gear
                          : label.contains("washer")                                                         ? Icon::Torus
                                                                                                             : Icon::Cylinder;
        parts.items.push_back({id, label, makeIcon(icon, ink), {}});
    }
    ViewportChrome::DrawerSection colors{tr("Colours"), {}};
    const std::pair<const char*, QRgb> swatches[] = {{"White", 0xF2F2F0}, {"Black", 0x2B2B2E}, {"Grey", 0x8C9096},  {"Red", 0xD8423C},
                                                     {"Orange", 0xF08A2E}, {"Yellow", 0xF2C94C}, {"Green", 0x4CAF6A}, {"Blue", 0x3D7FD9},
                                                     {"Purple", 0x8E5CC8}, {"Pink", 0xE47FB0}, {"Wood", 0xB98A5A}, {"Silver", 0xC4C9D0}};
    for (const auto& [name, rgb] : swatches)
        colors.items.push_back({QStringLiteral("color_%1").arg(rgb, 6, 16, QChar('0')), tr(name), {}, QColor::fromRgb(rgb)});
    return {shapes, parts, colors};
}

void MainWindow::useDrawerItem(const QString& id) {
    if (id.startsWith("color_")) {
        const std::vector<int> bodies = targetBodies();
        if (bodies.empty()) {
            statusBar()->showMessage(tr("Select a part first, or drag the colour onto one."), 5000);
            return;
        }
        paintBodies(bodies, id.mid(6).toUInt(nullptr, 16));
        return;
    }
    const auto part = drawerPart(id);
    if (!part) return;
    chrome_->setDrawerOpen(false);  // out of the way while placing
    selectAction_->setChecked(false);
    cancelTools();
    selection_->clear();
    placement_->begin(part->first, part->second);
}

void MainWindow::paintBodies(const std::vector<int>& bodies, uint32_t color) {
    pushUndo();
    for (int b : bodies) document_.bodies[b].color = color & 0xFFFFFF;
    afterChange(bodies);
}

void MainWindow::hideBodies(const std::vector<int>& bodies) {
    if (bodies.empty()) {
        askFor("then_hide", tr("Hide"), {{tr("Solids to hide"), StepTool::Pick::Bodies}}, true);
        return;
    }
    pushUndo();
    for (int b : bodies) document_.bodies[b].hidden = true;
    afterChange({});
}

// ---- The how-to walkthrough ------------------------------------------------------

GuideSpot MainWindow::guideSpot(const QString& name) {
    GuideSpot spot;
    if (name.isEmpty()) return spot;
    const auto at = [&spot](QWidget* widget, bool direct, const QString& hint = {}, QRect rect = {}) {
        spot.known = true;
        spot.direct = direct;
        spot.widget = widget;
        spot.rect = rect.isNull() ? widget->rect() : rect;
        spot.hint = hint;
        return spot;
    };
    if (name == QLatin1String("viewport")) {
        at(viewport_, true);
        spot.area = true;
        return spot;
    }
    // Toolbar buttons and the folder button's list are actions.
    if (auto* action = findChild<QAction*>(name)) {
        spot.known = true;
        if (QWidget* button = toolbar_->widgetForAction(action)) return at(button, true);
        if (fileMenu_->actions().contains(action)) {
            if (fileMenu_->isVisible()) return at(fileMenu_, true, {}, fileMenu_->actionGeometry(action));
            return at(findChild<QToolButton*>("folderButton"), false, tr("Click the folder button to open its list."));
        }
        return spot;
    }
    QWidget* widget = findChild<QWidget*>(name);
    if (!widget) return spot;
    spot.known = true;
    if (widget->isVisible()) return at(widget, true);
    // Hidden in a closed list, drawer or panel: point at what opens it.
    for (QWidget* w = widget; w; w = w->parentWidget()) {
        const QString anchor = w->property("guideAnchor").toString();
        if (!anchor.isEmpty())
            if (auto* opener = findChild<QWidget*>(anchor); opener && opener->isVisible()) {
                const QString hint = w->property("guideHint").toString();
                return at(opener, false, hint.isEmpty() ? tr("Point at this button to open its list.") : hint);
            }
        if (auto* dock = qobject_cast<QDockWidget*>(w); dock && dock->isHidden())
            if (QWidget* toggle = toolbar_->widgetForAction(dock->toggleViewAction())) return at(toggle, false, tr("Click this button to show the panel."));
    }
    // A tool's own panel, shown once the tool starts: the bubble explains, with no arrow yet.
    return spot;
}

void MainWindow::showGuide(const QString& topicId) {
    if (const td::ManualTopic* topic = td::manualTopic(topicId)) guide_->start(*topic);
}

QString MainWindow::interfaceMap() const {
    const auto clean = [](QString text) { return text.remove(QLatin1Char('&')).replace(QLatin1Char('\t'), QStringLiteral(" ")).simplified(); };
    // The labels in a hover list, in order.
    const auto listOf = [&](const QString& anchor) {
        QStringList items;
        if (anchor.isEmpty()) return items;
        for (QFrame* frame : findChildren<QFrame*>())
            if (frame->property("guideAnchor").toString() == anchor)
                for (QToolButton* item : frame->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) items << clean(item->text());
        return items;
    };
    QStringList lines{tr("Toolbar, left to right:")};
    for (QAction* action : toolbar_->actions()) {
        QWidget* widget = toolbar_->widgetForAction(action);
        auto* button = qobject_cast<QToolButton*>(widget);
        if (!button && !action->isVisible()) continue;
        QString label = clean(action->toolTip().isEmpty() ? action->text() : action->toolTip());
        if (button && button->menu()) {
            QStringList items;
            for (QAction* a : button->menu()->actions())
                if (!a->isSeparator()) items << clean(a->text()) + (a->shortcut().isEmpty() ? QString() : QStringLiteral(" (%1)").arg(a->shortcut().toString()));
            label = clean(button->toolTip()) + QStringLiteral(": ") + items.join(QStringLiteral(", "));
        } else if (button) {
            label = clean(button->toolTip());
            const QStringList items = listOf(button->objectName());
            if (!items.isEmpty()) label += QStringLiteral(" (point at it for: %1)").arg(items.join(QStringLiteral(", ")));
        }
        if (label.isEmpty()) continue;
        lines << QStringLiteral("- ") + label;
    }
    lines << tr("Top right corner of the 3D view: the view cube (click a face to look from there; a house button returns to the start view).");
    lines << tr("Bar under the view cube:");
    if (auto* bar = viewport_->findChild<QFrame*>("navigationBar"))
        for (QToolButton* button : bar->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
            QString label = clean(button->toolTip());
            const QStringList items = listOf(button->objectName());
            if (!items.isEmpty()) label += QStringLiteral(" (point at it for: %1)").arg(items.join(QStringLiteral(", ")));
            lines << QStringLiteral("- ") + label;
        }
    lines << tr("Bottom right of the 3D view: Edit Grid (bed size), Snap (mm a drag steps by) and Angle (degrees a turn steps by).");
    lines << tr("Blue tab on the right edge of the 3D view: opens the parts drawer with shapes, nuts, bolts, washers, gears and colours.");
    lines << tr("Right-click a solid, a selected face or edge, or empty space for a menu: Move / Rotate, Scale, Drop to the bed, Align, Cut, Copy, Paste, Delete, Hide, Material, Group, Press Pull, Fillet, Chamfer, Measure, Show All, Fit All.");
    lines << tr("A tool that needs picks shows a step bar at the top of the 3D view listing what to click, with Apply (Enter) and Cancel (Esc).");
    return lines.join(QLatin1Char('\n'));
}

// ---- The right-click menu -----------------------------------------------------

void MainWindow::remember(const QString& menuId) { lastCommand_ = menuId; }

QString MainWindow::menuLabel(const QString& menuId) const {
    if (menuId == "drop") return tr("Drop");
    const auto* item = findChild<QToolButton*>(menuId);
    return item ? item->text().section(QStringLiteral("   ("), 0, 0) : QString();
}

std::optional<td::V3> MainWindow::onBed(QPoint position) const {
    td::V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    if (std::abs(direction.z) < 1e-6) return std::nullopt;
    const double t = -origin.z / direction.z;
    if (t <= 0) return std::nullopt;
    const td::V3 p = origin + direction * t;
    return td::V3{snapLength(p.x, snapSettings().grid), snapLength(p.y, snapSettings().grid), 0};
}

void MainWindow::showContextMenu(QPoint position) {
    // A tool in progress keeps the right button for turning the view. Selecting a
    // face starts its push-pull arrow, but until it moves that is not an edit yet.
    const bool untouchedFace = modify_->active() && modify_->kind() == td::ModifyKind::Extrude && modify_->value() == 0;
    if (placement_->active() || (modify_->active() && !untouchedFace) || transform_->active() || steps_->active()) return;

    auto* menu = new QMenu(this);
    menu->setObjectName("contextMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto add = [&](const char* id, const QString& text, bool enabled, auto slot) {
        QAction* a = menu->addAction(text);
        a->setObjectName(QString::fromLatin1(id));
        a->setEnabled(enabled);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    const auto addLater = [&](const char* id, const QString& text, const QString& tool) {
        QAction* a = add(id, text, true, [this, tool] { startPlanned(tool); });
        a->setToolTip(tr("Not built yet: shows the steps it will take"));
    };
    if (!lastCommand_.isEmpty()) {
        const QString command = lastCommand_;
        add("ctx_repeat", tr("Repeat %1").arg(menuLabel(command)), true, [this, command] { runMenu(command); });
        menu->addSeparator();
    }

    const auto hit = selection_->hitAt(position);
    if (!hit) {
        const std::optional<td::V3> spot = onBed(position);
        add("ctx_paste", tr("Paste\tCtrl+V"), !clipboard_.empty(), [this, spot] { paste(spot); });
        menu->addSeparator();
        const bool anyHidden = std::any_of(document_.bodies.begin(), document_.bodies.end(), [](const td::Body& b) { return b.hidden; });
        add("ctx_showall", tr("Show All"), anyHidden, [this] { showAll(); });
        menu->addSeparator();
        auto* modes = new QActionGroup(menu);
        const std::pair<const char*, Viewport::Navigation> navigation[] = {
            {"ctx_orbit", Viewport::Navigation::Orbit}, {"ctx_pan", Viewport::Navigation::Pan}, {"ctx_zoom", Viewport::Navigation::Zoom}};
        const QString names[] = {tr("Orbit with left drag"), tr("Pan with left drag"), tr("Zoom with left drag")};
        for (int i = 0; i < 3; ++i) {
            const Viewport::Navigation mode = navigation[i].second;
            QAction* a = add(navigation[i].first, names[i], true, [this, mode] { viewport_->setNavigation(mode); });
            a->setCheckable(true);
            a->setChecked(viewport_->navigation() == mode);
            modes->addAction(a);
        }
        add("ctx_fit", tr("Fit All\tF"), true, [this] { fitView(); });
    } else if (selection_->subKind() != SubKind::None && hit->body == selection_->subOwner()) {
        if (selection_->subKind() == SubKind::Face) {
            add("ctx_presspull", tr("Press Pull\tP"), true, [this] { startModify(td::ModifyKind::Extrude); });
            addLater("ctx_tweak", tr("Tweak\tT"), "tweak");
            addLater("ctx_shell", tr("Shell"), "shell");
            addLater("ctx_sketch", tr("Sketch on Face"), "sketch");
            menu->addSeparator();
            add("ctx_measure", tr("Measure"), true, [this] { measureFace(); });
        } else {
            add("ctx_fillet", tr("Fillet\tF"), true, [this] { startModify(td::ModifyKind::Fillet); });
            if (selection_->subKind() == SubKind::Edge) {
                add("ctx_chamfer", tr("Chamfer\tC"), true, [this] { startModify(td::ModifyKind::Chamfer); });
                addLater("ctx_tweak", tr("Tweak\tT"), "tweak");
            }
        }
    } else {
        // Right-clicking a solid selects it first, unless it is already part of the selection.
        const std::vector<int> selected = selection_->selectedBodies();
        if (std::find(selected.begin(), selected.end(), hit->body) == selected.end())
            selection_->selectBodies(td::unitOf(document_, hit->body, selection_->groupContext()));
        const std::vector<int> bodies = selection_->selectedBodies();
        const int clicked = hit->body;
        add("ctx_move", tr("Move / Rotate\tM"), true, [this] { startTransform(TransformMode::MoveRotate); });
        add("ctx_scale", tr("Scale\tS"), true, [this] { startTransform(TransformMode::Scale); });
        add("ctx_drop", tr("Drop to the bed\tD"), true, [this] { dropSelection(); });
        add("ctx_align", tr("Align"), true, [this] { startAlign(); });
        menu->addSeparator();
        add("ctx_cut", tr("Cut\tCtrl+X"), true, [this] { copySelection(true); });
        add("ctx_copy", tr("Copy\tCtrl+C"), true, [this] { copySelection(false); });
        add("ctx_delete", tr("Delete\tDel"), true, [this, bodies] { removeBodies(bodies); });
        menu->addSeparator();
        add("ctx_hide", tr("Hide Solid"), true, [this, clicked] { hideBody(clicked); });
        add("ctx_material", tr("Material\u2026"), true, [this, bodies] { chooseMaterial(bodies); });

        std::set<int> units;
        bool anyGroup = false;
        for (int b : bodies) {
            const auto unit = td::unitOf(document_, b, selection_->groupContext());
            units.insert(unit.front());
            anyGroup |= td::isGroupUnit(document_, b, selection_->groupContext());
        }
        if (units.size() > 1 || anyGroup) menu->addSeparator();
        if (units.size() > 1) add("ctx_group", tr("Group\tCtrl+G"), true, [this] { groupSelection(); });
        if (anyGroup) add("ctx_ungroup", tr("Ungroup\tCtrl+Shift+G"), true, [this] { ungroupSelection(false); });
    }
    menu->popup(viewport_->mapToGlobal(position));
}

void MainWindow::copySelection(bool cut) {
    std::vector<int> bodies = targetBodies();
    if (bodies.empty()) return;
    clipboard_.clear();
    clipboardMin_ = {1e30, 1e30, 1e30};
    clipboardMax_ = {-1e30, -1e30, -1e30};
    for (int b : bodies) {
        clipboard_.push_back(document_.bodies[b]);
        for (int a = 0; a < 3; ++a) {
            clipboardMin_[a] = std::min(clipboardMin_[a], bodyResults_[b].min[a]);
            clipboardMax_[a] = std::max(clipboardMax_[a], bodyResults_[b].max[a]);
        }
    }
    if (cut) removeBodies(bodies);
    statusBar()->showMessage(cut ? tr("Cut %n solid(s).", nullptr, int(bodies.size())) : tr("Copied %n solid(s).", nullptr, int(bodies.size())), 4000);
}

void MainWindow::paste(std::optional<td::V3> at) {
    if (clipboard_.empty()) return;
    cancelTools();
    // Centred on the spot, at the height it had; without a spot, a little to one side.
    const td::V3 centre{(clipboardMin_[0] + clipboardMax_[0]) / 2, (clipboardMin_[1] + clipboardMax_[1]) / 2, 0};
    const td::V3 shift = at ? td::V3{at->x - centre.x, at->y - centre.y, 0} : td::V3{10, 10, 0};
    pushUndo();
    std::map<int, int> newGroups;  // pasted groups stay together, as new groups
    std::vector<int> pasted;
    for (td::Body body : clipboard_) {
        body.id = document_.nextBodyId++;
        QString label = body.name;
        label.remove(QRegularExpression(QStringLiteral("\\s+\\d+$")));
        body.name = td::nextName(document_, label.isEmpty() ? tr("Body") : label);
        body.transform = td::Affine::translation(shift) * body.transform;
        body.hidden = false;
        for (int& g : body.groups) {
            if (!newGroups.count(g)) newGroups[g] = document_.nextGroupId++;
            g = newGroups[g];
        }
        document_.bodies.push_back(std::move(body));
        pasted.push_back(int(document_.bodies.size()) - 1);
    }
    afterChange(pasted);
}

void MainWindow::dropSelection() {
    remember("drop");
    const std::vector<int> bodies = targetBodies();
    if (bodies.empty()) {
        askFor("then_drop", tr("Drop to the bed"), {{tr("Solids to drop"), StepTool::Pick::Bodies}}, true);
        return;
    }
    // Each group drops as one, so its parts keep their places relative to each other.
    std::vector<std::vector<int>> units;
    std::set<int> seen;
    for (int b : bodies) {
        if (seen.count(b)) continue;
        std::vector<int> unit = td::unitOf(document_, b, selection_->groupContext());
        unit.erase(std::remove_if(unit.begin(), unit.end(), [&](int m) { return std::find(bodies.begin(), bodies.end(), m) == bodies.end(); }), unit.end());
        for (int m : unit) seen.insert(m);
        units.push_back(unit);
    }
    td::Document dropped = document_;
    bool moved = false;
    for (const auto& unit : units) {
        double lowest = 1e30;
        for (int b : unit) lowest = std::min(lowest, bodyResults_[b].min[2]);
        if (std::abs(lowest) < 1e-6) continue;
        td::transformBodies(&dropped, unit, td::Affine::translation({0, 0, -lowest}));
        moved = true;
    }
    if (!moved) {
        statusBar()->showMessage(tr("Already on the bed."), 4000);
        return;
    }
    pushUndo();
    document_ = std::move(dropped);
    afterChange(bodies);
}

void MainWindow::hideBody(int body) {
    pushUndo();
    document_.bodies[body].hidden = true;
    std::vector<int> still = selection_->selectedBodies();
    still.erase(std::remove(still.begin(), still.end(), body), still.end());
    afterChange(still);
}

void MainWindow::showAll() {
    if (std::none_of(document_.bodies.begin(), document_.bodies.end(), [](const td::Body& b) { return b.hidden; })) return;
    pushUndo();
    for (td::Body& b : document_.bodies) b.hidden = false;
    afterChange(selection_->selectedBodies());
}

void MainWindow::chooseMaterial(const std::vector<int>& bodies) {
    if (bodies.empty()) return;
    const QVector4D now = document_.bodies[bodies.front()].color ? QVector4D() : bodyColor(document_.bodies[bodies.front()].id);
    const uint32_t current = document_.bodies[bodies.front()].color;
    auto* dialog = new QColorDialog(current ? QColor::fromRgb(current) : QColor::fromRgbF(now.x(), now.y(), now.z()), this);
    dialog->setObjectName("materialDialog");
    dialog->setWindowTitle(tr("Material colour"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    std::vector<int> ids;
    for (int b : bodies) ids.push_back(document_.bodies[b].id);
    connect(dialog, &QColorDialog::colorSelected, this, [this, ids](const QColor& color) {
        pushUndo();
        std::vector<int> changed;
        for (int id : ids) {
            const int b = td::bodyIndex(document_, id);
            if (b < 0) continue;
            document_.bodies[b].color = color.rgb() & 0xFFFFFF;
            changed.push_back(b);
        }
        afterChange(changed);
    });
    dialog->open();
}

void MainWindow::measureFace() {
    if (selection_->subKind() != SubKind::Face || selection_->subIds().empty()) return;
    const int body = selection_->subOwner();
    const td::Mesh& mesh = bodyResults_[body].mesh;
    const td::TopoFace& face = selection_->topology().faces[selection_->subIds().front()];
    double area = 0;
    for (uint32_t t : face.triangles) {
        const td::V3 a = td::meshVertex(mesh, mesh.indices[t * 3]);
        area += td::length(td::cross(td::meshVertex(mesh, mesh.indices[t * 3 + 1]) - a, td::meshVertex(mesh, mesh.indices[t * 3 + 2]) - a)) / 2;
    }
    double perimeter = 0;
    for (const auto& loop : td::faceLoops(mesh, face))
        for (size_t i = 0; i < loop.size(); ++i) perimeter += td::length(td::meshVertex(mesh, loop[(i + 1) % loop.size()]) - td::meshVertex(mesh, loop[i]));
    const td::V3 c = face.centroid;
    const QString text = tr("Face: area %1 mm\u00b2, perimeter %2 mm, centre (%3, %4, %5)%6")
                             .arg(area, 0, 'f', 2)
                             .arg(perimeter, 0, 'f', 2)
                             .arg(c.x, 0, 'f', 2)
                             .arg(c.y, 0, 'f', 2)
                             .arg(c.z, 0, 'f', 2)
                             .arg(face.planar ? QString() : tr(", curved"));
    statusBar()->showMessage(text, 20000);
    QToolTip::showText(QCursor::pos(), text, viewport_);
}

// ---- Grouping ---------------------------------------------------------------

void MainWindow::groupSelection() {
    const std::vector<int> bodies = targetBodies();
    if (bodies.empty()) {
        askFor("then_group", tr("Group"), {{tr("Solids to group (two or more)"), StepTool::Pick::Bodies}}, true);
        return;
    }
    td::Document grouped = document_;
    if (!td::groupBodies(&grouped, bodies, selection_->groupContext())) {
        statusBar()->showMessage(tr("Select two or more solids to group."), 5000);
        return;
    }
    pushUndo();
    document_ = std::move(grouped);
    afterChange(bodies);
}

void MainWindow::ungroupSelection(bool all) {
    const std::vector<int> bodies = targetBodies();
    if (bodies.empty()) {
        askFor(all ? "then_ungroupall" : "then_ungroup", all ? tr("Ungroup All") : tr("Ungroup"), {{tr("Group to break up"), StepTool::Pick::Body}}, false);
        return;
    }
    td::Document ungrouped = document_;
    const bool changed = all ? td::ungroupAll(&ungrouped, bodies, selection_->groupContext())
                             : td::ungroupBodies(&ungrouped, bodies, selection_->groupContext());
    if (!changed) {
        statusBar()->showMessage(tr("Select a group to ungroup."), 5000);
        return;
    }
    pushUndo();
    document_ = std::move(ungrouped);
    afterChange(bodies);
}

// ---- Files ------------------------------------------------------------------

bool MainWindow::confirmDiscard() {
    if (!modified_) return true;
    const auto choice = QMessageBox::warning(this, tr("Unsaved changes"), tr("Save your changes to this design?"),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Save) return save();
    return choice == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscard()) event->accept();
    else event->ignore();
}

void MainWindow::newDesign() {
    if (!confirmDiscard()) return;
    cancelTools();
    document_ = {};
    undoStack_.clear();
    redoStack_.clear();
    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
    path_.clear();
    modified_ = false;
    refreshPanel({});
    rebuild();
    viewport_->fitBed();
    updateTitle();
}

void MainWindow::open() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open design"), {}, kDesignFilter);
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openFile(const QString& path) {
    QFile file(path);
    QString error;
    td::Document loaded;
    if (!file.open(QIODevice::ReadOnly)) error = file.errorString();
    else td::documentFromJson(file.readAll(), &loaded, &error);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, tr("Could not open design"), error);
        return false;
    }
    cancelTools();
    document_ = std::move(loaded);
    undoStack_.clear();
    redoStack_.clear();
    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
    path_ = path;
    modified_ = false;
    refreshPanel({});
    rebuild(true);
    updateTitle();
    return true;
}

bool MainWindow::save() { return path_.isEmpty() ? saveAs() : saveTo(path_); }

bool MainWindow::saveAs() {
    QFileDialog dialog(this, tr("Save design"), path_, kDesignFilter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("cubra");
    if (dialog.exec() != QDialog::Accepted) return false;
    return saveTo(dialog.selectedFiles().value(0));
}

bool MainWindow::saveTo(const QString& path) {
    const QByteArray json = td::documentToJson(document_);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        QMessageBox::critical(this, tr("Could not save design"), file.errorString());
        return false;
    }
    path_ = path;
    modified_ = false;
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
    return true;
}

void MainWindow::exportMesh(bool threeMf) {
    if (!meshCurrent_ || !combined_.ok) {
        QMessageBox::warning(this, tr("Cannot export"), tr("Fix the problem shown in the status bar first."));
        return;
    }
    if (combined_.mesh.empty()) {
        QMessageBox::information(this, tr("Cannot export"), tr("There is nothing to export yet."));
        return;
    }

    const QString suffix = threeMf ? QStringLiteral("3mf") : QStringLiteral("stl");
    const QString base = path_.isEmpty() ? tr("model") : QFileInfo(path_).completeBaseName();
    QFileDialog dialog(this, tr("Export %1").arg(suffix.toUpper()), base + "." + suffix,
                       threeMf ? tr("3MF files (*.3mf)") : tr("STL files (*.stl)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(suffix);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString path = dialog.selectedFiles().value(0);

    // Every body goes out as one solid: overlaps fused, as the printer needs.
    QString error;
    const bool ok = threeMf ? td::export3mf(combined_.mesh, path, QFileInfo(path).completeBaseName(), &error) : td::exportStl(combined_.mesh, path, &error);
    if (!ok) {
        QMessageBox::critical(this, tr("Export failed"), error);
        return;
    }
    statusBar()->showMessage(tr("Exported %1").arg(path), 6000);
}
