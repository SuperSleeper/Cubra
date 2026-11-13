#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>

#include <vector>

class HoverMenu;
class QButtonGroup;
class QComboBox;
class QFrame;
class QPropertyAnimation;
class QToolButton;
class QVBoxLayout;
class ViewCube;
class Viewport;

// Everything that sits on the edges of the workspace: the ViewCube, the
// navigation and display bar under it, the grid and snap controls at the bottom
// right, and the drawer of parts and colours behind the tab on the right edge.
class ViewportChrome : public QObject {
    Q_OBJECT
public:
    struct DrawerItem {
        QString id;      // "drawer_..." for a part, "color_#rrggbb" for a colour
        QString label;
        QIcon icon;
        QColor swatch;   // colours only
    };
    struct DrawerSection {
        QString title;
        std::vector<DrawerItem> items;
    };

    explicit ViewportChrome(Viewport* viewport);

    void setDrawer(const std::vector<DrawerSection>& sections);
    void setDrawerOpen(bool open, bool animate = true);
    bool drawerOpen() const { return drawerOpen_; }
    ViewCube* cube() const { return cube_; }
    void refreshSnapControls();  // after the snap settings change elsewhere

signals:
    void homeRequested();
    void fitRequested();
    void hideSelectedRequested();
    void hideSketchesRequested();
    void showAllRequested();
    void screenshotRequested();
    void editGridRequested();
    void drawerItemClicked(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QToolButton* barButton(const QString& name, int icon, const QString& tip, bool checkable);
    void layout();
    void refreshStyleMenu();

    Viewport* viewport_;
    ViewCube* cube_;
    QFrame* bar_;
    QVBoxLayout* barLayout_;
    QButtonGroup* navigation_;
    HoverMenu* styleMenu_;
    HoverMenu* visibilityMenu_;
    QFrame* precision_;
    QComboBox* linear_;
    QComboBox* angular_;
    QToolButton* snapGrid_;
    QToolButton* snapPoints_;
    QToolButton* tab_;
    QFrame* drawer_;
    QVBoxLayout* drawerLayout_;
    QPropertyAnimation* slide_;
    bool drawerOpen_ = false;
};
