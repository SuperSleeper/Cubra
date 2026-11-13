#include "app/ViewportChrome.h"

#include "app/HoverMenu.h"
#include "app/Icons.h"
#include "app/SnapSettings.h"
#include "app/ViewCube.h"
#include "app/Viewport.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleValidator>
#include <QDrag>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kMargin = 10;
constexpr int kDrawerWidth = 262;
constexpr int kTabWidth = 16;

// A drawer entry: a click picks it, a drag carries it onto the workspace.
class DrawerButton : public QToolButton {
public:
    DrawerButton(const QString& id, QWidget* parent) : QToolButton(parent), id_(id) {}

protected:
    void mousePressEvent(QMouseEvent* event) override {
        pressAt_ = event->position().toPoint();
        QToolButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton) || (event->position().toPoint() - pressAt_).manhattanLength() < QApplication::startDragDistance()) {
            QToolButton::mouseMoveEvent(event);
            return;
        }
        setDown(false);
        auto* drag = new QDrag(this);
        auto* data = new QMimeData;
        data->setData(kDrawerMime, id_.toUtf8());
        drag->setMimeData(data);
        drag->setPixmap(icon().pixmap(40, 40));
        drag->exec(Qt::CopyAction);
    }

private:
    QString id_;
    QPoint pressAt_;
};

QIcon swatchIcon(const QColor& color) {
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(20, 22, 26), 2));
    p.setBrush(color);
    p.drawRoundedRect(QRectF(4, 4, 40, 40), 8, 8);
    return QIcon(pixmap);
}

}  // namespace

ViewportChrome::ViewportChrome(Viewport* viewport) : QObject(viewport), viewport_(viewport) {
    const QColor ink = viewport_->palette().color(QPalette::WindowText);
    cube_ = new ViewCube(viewport_);
    connect(cube_, &ViewCube::homeRequested, this, &ViewportChrome::homeRequested);

    // ---- The vertical bar under the cube ----
    bar_ = new QFrame(viewport_);
    bar_->setObjectName("navigationBar");
    bar_->setFrameShape(QFrame::StyledPanel);
    bar_->setAutoFillBackground(true);
    barLayout_ = new QVBoxLayout(bar_);
    barLayout_->setContentsMargins(3, 3, 3, 3);
    barLayout_->setSpacing(1);

    navigation_ = new QButtonGroup(this);
    const std::pair<const char*, Viewport::Navigation> modes[] = {
        {"nav_pan", Viewport::Navigation::Pan}, {"nav_orbit", Viewport::Navigation::Orbit}, {"nav_zoom", Viewport::Navigation::Zoom}};
    const Icon icons[] = {Icon::Transform, Icon::Orbit, Icon::Zoom};
    const QString tips[] = {tr("Pan: left-drag slides the view (middle-drag always does)"), tr("Orbit: left-drag turns the view (right-drag always does)"),
                            tr("Zoom: left-drag up and down zooms (the wheel always does)")};
    for (int i = 0; i < 3; ++i) {
        QToolButton* button = barButton(modes[i].first, int(icons[i]), tips[i], true);
        navigation_->addButton(button, int(modes[i].second));
        if (modes[i].second == viewport_->navigation()) button->setChecked(true);
    }
    connect(navigation_, &QButtonGroup::idClicked, this, [this](int mode) { viewport_->setNavigation(Viewport::Navigation(mode)); });
    connect(viewport_, &Viewport::navigationChanged, this, [this](Viewport::Navigation mode) {
        if (QAbstractButton* button = navigation_->button(int(mode))) button->setChecked(true);
    });
    connect(barButton("nav_fit", int(Icon::Fit), tr("Fit: frame the selection, or everything shown (F)"), false), &QToolButton::clicked, this,
            &ViewportChrome::fitRequested);

    styleMenu_ = new HoverMenu(barButton("nav_style", int(Icon::VisualStyle), tr("Visual style"), false), viewport_, "styleMenu");
    styleMenu_->setOpensLeft(true);
    styleMenu_->addItem("style_materials_outlines", tr("Materials and outlines"));
    styleMenu_->addItem("style_materials", tr("Materials only"));
    styleMenu_->addItem("style_outlines", tr("Outlines only"));
    styleMenu_->addItem("style_xray", tr("X-ray"));
    connect(styleMenu_, &HoverMenu::triggered, this, [this](const QString& id) {
        viewport_->setStyle(id == "style_materials" ? Viewport::Style::MaterialsOnly
                            : id == "style_outlines" ? Viewport::Style::OutlinesOnly
                            : id == "style_xray"     ? Viewport::Style::XRay
                                                     : Viewport::Style::MaterialsAndOutlines);
        refreshStyleMenu();
    });
    refreshStyleMenu();

    visibilityMenu_ = new HoverMenu(barButton("nav_visibility", int(Icon::Eye), tr("Hide and show"), false), viewport_, "visibilityMenu");
    visibilityMenu_->setOpensLeft(true);
    visibilityMenu_->addItem("vis_hide", tr("Hide selected solids"));
    visibilityMenu_->addItem("vis_hidesketches", tr("Hide sketches"), {}, true, tr("There are no sketches yet"));
    visibilityMenu_->addItem("vis_showall", tr("Show all"));
    connect(visibilityMenu_, &HoverMenu::triggered, this, [this](const QString& id) {
        if (id == "vis_hide") emit hideSelectedRequested();
        else if (id == "vis_hidesketches") emit hideSketchesRequested();
        else if (id == "vis_showall") emit showAllRequested();
    });

    QToolButton* grid = barButton("nav_grid", int(Icon::GridEye), tr("Show the grid (snapping is unaffected)"), true);
    grid->setChecked(viewport_->gridVisible());
    connect(grid, &QToolButton::toggled, viewport_, &Viewport::setGridVisible);
    connect(barButton("nav_screenshot", int(Icon::Camera), tr("Save a picture of the workspace"), false), &QToolButton::clicked, this,
            &ViewportChrome::screenshotRequested);

    snapPoints_ = barButton("nav_snappoints", int(Icon::SnapPoints), tr("Implicit snap: new shapes seat onto the faces under the pointer"), true);
    snapGrid_ = barButton("nav_snapgrid", int(Icon::SnapGrid), tr("Grid snap: drags step by the snap sizes at the bottom right"), true);
    connect(snapPoints_, &QToolButton::toggled, this, [](bool on) { snapSettings().implicit = on; });
    connect(snapGrid_, &QToolButton::toggled, this, [this](bool on) {
        snapSettings().grid = on;
        refreshSnapControls();
    });
    (void)ink;

    // ---- Grid and precision, bottom right ----
    precision_ = new QFrame(viewport_);
    precision_->setObjectName("precisionBar");
    precision_->setFrameShape(QFrame::StyledPanel);
    precision_->setAutoFillBackground(true);
    auto* row = new QHBoxLayout(precision_);
    row->setContentsMargins(6, 3, 6, 3);
    auto* editGrid = new QPushButton(tr("Edit Grid"), precision_);
    editGrid->setObjectName("editGrid");
    editGrid->setFocusPolicy(Qt::NoFocus);
    connect(editGrid, &QPushButton::clicked, this, &ViewportChrome::editGridRequested);
    row->addWidget(editGrid);
    const auto stepBox = [&](const char* name, const QStringList& steps, double* setting, const QString& unit, const QString& tip) {
        row->addSpacing(8);
        row->addWidget(new QLabel(unit == QStringLiteral("mm") ? tr("Snap") : tr("Angle"), precision_));
        auto* box = new QComboBox(precision_);
        box->setObjectName(name);
        box->setEditable(true);
        box->addItems(steps);
        box->setToolTip(tip);
        box->setFixedWidth(70);
        box->lineEdit()->setValidator(new QDoubleValidator(0.01, 360, 2, box));
        connect(box, &QComboBox::currentTextChanged, this, [setting](const QString& text) {
            bool ok = false;
            const double value = text.toDouble(&ok);
            if (ok && value > 0) *setting = value;
        });
        row->addWidget(box);
        row->addWidget(new QLabel(unit, precision_));
        return box;
    };
    linear_ = stepBox("snapLinear", {"0.1", "0.5", "1", "2", "5", "10"}, &snapSettings().linear, tr("mm"), tr("How far drags step, in millimetres"));
    angular_ = stepBox("snapAngular", {"1", "5", "15", "30", "45", "90"}, &snapSettings().angular, QStringLiteral("°"), tr("How far turns step, in degrees"));
    refreshSnapControls();

    // ---- The drawer, behind the tab on the right edge ----
    drawer_ = new QFrame(viewport_);
    drawer_->setObjectName("partsDrawer");
    // While closed, the how-to walkthrough points at the tab that opens it.
    drawer_->setProperty("guideAnchor", QStringLiteral("drawerTab"));
    drawer_->setProperty("guideHint", tr("Click the blue tab to open the drawer."));
    drawer_->setFrameShape(QFrame::StyledPanel);
    drawer_->setAutoFillBackground(true);
    auto* drawerRoot = new QVBoxLayout(drawer_);
    drawerRoot->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(drawer_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    drawerLayout_ = new QVBoxLayout(content);
    drawerLayout_->setContentsMargins(8, 8, 8, 8);
    scroll->setWidget(content);
    drawerRoot->addWidget(scroll);
    drawer_->hide();

    tab_ = new QToolButton(viewport_);
    tab_->setObjectName("drawerTab");
    tab_->setText(QStringLiteral("◀"));
    tab_->setToolTip(tr("Parts, hardware and colours"));
    tab_->setStyleSheet("QToolButton { background: #2f6fb7; color: white; border: none; border-top-left-radius: 5px; border-bottom-left-radius: 5px; }");
    tab_->installEventFilter(this);
    connect(tab_, &QToolButton::clicked, this, [this] { setDrawerOpen(!drawerOpen_); });

    slide_ = new QPropertyAnimation(drawer_, "pos", this);
    slide_->setDuration(180);
    slide_->setEasingCurve(QEasingCurve::OutCubic);
    connect(slide_, &QPropertyAnimation::finished, this, [this] {
        if (!drawerOpen_) drawer_->hide();
        layout();
    });

    viewport_->installEventFilter(this);
    layout();
}

QToolButton* ViewportChrome::barButton(const QString& name, int icon, const QString& tip, bool checkable) {
    auto* button = new QToolButton(bar_);
    button->setObjectName(name);
    button->setIcon(makeIcon(Icon(icon), viewport_->palette().color(QPalette::WindowText)));
    button->setIconSize(QSize(22, 22));
    button->setAutoRaise(true);
    button->setCheckable(checkable);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    barLayout_->addWidget(button);
    return button;
}

void ViewportChrome::refreshStyleMenu() {
    const char* ids[] = {"style_materials_outlines", "style_materials", "style_outlines", "style_xray"};
    const int current = int(viewport_->style());
    for (int i = 0; i < 4; ++i)
        if (auto* item = styleMenu_->frame()->findChild<QToolButton*>(ids[i])) {
            QString text = item->text();
            if (text.startsWith(QStringLiteral("✓ "))) text = text.mid(2);
            item->setText(i == current ? QStringLiteral("✓ ") + text : text);
        }
}

void ViewportChrome::refreshSnapControls() {
    const SnapSettings& s = snapSettings();
    snapGrid_->setChecked(s.grid);
    snapPoints_->setChecked(s.implicit);
    linear_->setEnabled(s.grid);
    angular_->setEnabled(s.grid);
    const QString linear = QString::number(s.linear), angular = QString::number(s.angular);
    if (linear_->currentText() != linear) linear_->setCurrentText(linear);
    if (angular_->currentText() != angular) angular_->setCurrentText(angular);
}

void ViewportChrome::setDrawer(const std::vector<DrawerSection>& sections) {
    while (QLayoutItem* item = drawerLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const DrawerSection& section : sections) {
        auto* title = new QLabel(section.title, drawer_);
        QFont bold = title->font();
        bold.setBold(true);
        title->setFont(bold);
        drawerLayout_->addWidget(title);
        auto* holder = new QWidget(drawer_);
        auto* grid = new QGridLayout(holder);
        grid->setContentsMargins(0, 0, 0, 6);
        grid->setSpacing(2);
        for (int i = 0; i < int(section.items.size()); ++i) {
            const DrawerItem& item = section.items[i];
            auto* button = new DrawerButton(item.id, holder);
            button->setObjectName(item.id);
            button->setText(item.label);
            button->setIcon(item.swatch.isValid() ? swatchIcon(item.swatch) : item.icon);
            button->setIconSize(QSize(30, 30));
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setAutoRaise(true);
            button->setFixedSize(76, 66);
            button->setToolTip(item.swatch.isValid() ? tr("Click to colour the selection, or drag onto a part") : tr("Click to place, or drag onto the workspace"));
            grid->addWidget(button, i / 3, i % 3);
            const QString id = item.id;
            connect(button, &QToolButton::clicked, this, [this, id] { emit drawerItemClicked(id); });
        }
        drawerLayout_->addWidget(holder);
    }
    drawerLayout_->addStretch(1);
}

void ViewportChrome::setDrawerOpen(bool open, bool animate) {
    if (open == drawerOpen_) return;
    drawerOpen_ = open;
    tab_->setText(open ? QStringLiteral("▶") : QStringLiteral("◀"));
    const int top = kMargin, height = viewport_->height() - 2 * kMargin;
    const QPoint shown(viewport_->width() - kDrawerWidth, top), hidden(viewport_->width(), top);
    drawer_->resize(kDrawerWidth, height);
    if (open) {
        drawer_->move(animate ? hidden : shown);
        drawer_->show();
        drawer_->raise();
    }
    tab_->raise();
    slide_->stop();
    if (!animate) {
        drawer_->move(open ? shown : hidden);
        if (!open) drawer_->hide();
        layout();
        return;
    }
    slide_->setStartValue(drawer_->pos());
    slide_->setEndValue(open ? shown : hidden);
    slide_->start();
}

void ViewportChrome::layout() {
    const int w = viewport_->width(), h = viewport_->height();
    const int drawerShift = drawerOpen_ ? kDrawerWidth : 0;
    cube_->move(w - cube_->width() - kMargin - kTabWidth, kMargin);
    bar_->adjustSize();
    bar_->move(w - bar_->width() - kMargin - kTabWidth - (cube_->width() - bar_->width()) / 2, cube_->geometry().bottom() + 8);
    precision_->adjustSize();
    precision_->move(w - precision_->width() - kMargin - kTabWidth, h - precision_->height() - kMargin);
    tab_->setGeometry(w - kTabWidth - drawerShift, h / 2 - 40, kTabWidth, 80);
    if (drawerOpen_ && slide_->state() != QAbstractAnimation::Running) drawer_->setGeometry(w - kDrawerWidth, kMargin, kDrawerWidth, h - 2 * kMargin);
    tab_->raise();
}

bool ViewportChrome::eventFilter(QObject* watched, QEvent* event) {
    if (watched == viewport_ && event->type() == QEvent::Resize) layout();
    // Hovering the tab is enough to slide the drawer out.
    if (watched == tab_ && event->type() == QEvent::Enter && !drawerOpen_) setDrawerOpen(true);
    return QObject::eventFilter(watched, event);
}
