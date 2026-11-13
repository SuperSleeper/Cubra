#pragma once

#include "app/GuideOverlay.h"
#include "app/Icons.h"
#include "app/StepTool.h"
#include "app/TransformController.h"
#include "app/ViewportChrome.h"
#include "core/Document.h"
#include "core/Geometry.h"
#include "core/Modify.h"

#include <QElapsedTimer>
#include <QMainWindow>

#include <optional>
#include <vector>

class AiPanel;
class FeaturePanel;
class HoverMenu;
class PatternController;
enum class PatternKind;
class ModifyController;
class PlacementController;
class SelectionController;
class QAction;
class QDockWidget;
class QLabel;
class QSpinBox;
class QToolBar;
class QToolButton;
class Viewport;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    bool openFile(const QString& path);
    AiPanel* aiPanel() const { return ai_; }
    const td::Document& document() const { return document_; }
    bool saveScreenshot(const QString& path);  // the 3D view only, as a PNG

    // The how-to walkthrough: where a manual step's target is on screen, and the
    // walkthrough itself. showGuide starts the topic with that id.
    GuideSpot guideSpot(const QString& name);
    GuideOverlay* guide() const { return guide_; }
    void showGuide(const QString& topic);
    // The toolbar, its lists and the view controls as they are now, in words, for the
    // assistant: it always matches the running interface.
    QString interfaceMap() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    using Features = std::vector<td::Feature>;

    void buildToolbar();
    QToolButton* menuButton(QToolBar* toolbar, const QString& name, Icon icon, const QString& tip);
    void runMenu(const QString& id);

    void pushUndo();
    void afterChange(const std::vector<int>& selectBodies);
    void refreshPanel(const std::vector<int>& selectBodies);
    void rebuild(bool fitView = false);
    void updateTitle();
    void markModified();
    void undo();
    void redo();
    void restore(const td::Document& document);

    void addPrimitive(const td::Feature& feature, const QString& label);
    void removeBodies(std::vector<int> bodies);
    void applyAiDesign(const Features& features, bool fresh = false);
    int aiTargetBody() const;

    // Direct edits of faces, edges and corners.
    void startModify(td::ModifyKind kind);
    void onSubSelectionChanged();
    void commitModify(td::ModifyKind kind, const Features& additions);

    // Clicks, routed to whichever tool is waiting for one.
    void routeClick(QPoint position, bool shift);
    void routeDoubleClick(QPoint position);
    void runGearTool(const QString& tool);
    void cancelTools();

    // Transform, Combine, Snap and Group.
    std::vector<int> targetBodies() const;
    void startTransform(TransformMode mode);
    void commitTransform(const td::Affine& change);
    void startSteps(const QString& id);
    void offerStep(QPoint position);
    void showStepPicks();
    void finishSteps(const QString& id, const std::vector<std::vector<StepTool::Choice>>& picks);
    bool canGrab(QPoint position) const;
    void startGrab(QPoint position);
    void moveGrab(QPoint position);
    void finishGrab(QPoint position);
    void startPattern(PatternKind kind);
    // With nothing suitable selected, a tool asks for its inputs in the step bar.
    void askFor(const QString& id, const QString& title, std::vector<StepTool::Step> steps, bool confirm, const QString& note = {});
    void finishAsked(const QString& action, const std::vector<std::vector<StepTool::Choice>>& picks);
    void startAlign();
    void showAlign(std::vector<int> moving, int target);
    // Tools whose steps are laid out but whose geometry is not built yet.
    void startPlanned(const QString& id);
    std::vector<td::Affine> patternPlacements() const;
    // The right-click menu, and what it offers.
    void showContextMenu(QPoint position);
    void remember(const QString& menuId);
    QString menuLabel(const QString& menuId) const;
    void copySelection(bool cut);
    void paste(std::optional<td::V3> at);
    void dropSelection();
    void hideBody(int body);
    void showAll();
    void chooseMaterial(const std::vector<int>& bodies);
    void measureFace();
    std::optional<td::V3> onBed(QPoint position) const;
    void fitView();
    void editGrid();
    std::vector<ViewportChrome::DrawerSection> drawerSections() const;
    std::optional<std::pair<td::Feature, QString>> drawerPart(const QString& id) const;
    void useDrawerItem(const QString& id);
    void paintBodies(const std::vector<int>& bodies, uint32_t color);
    void hideBodies(const std::vector<int>& bodies);
    void groupSelection();
    void ungroupSelection(bool all);

    void newDesign();
    void open();
    bool save();
    bool saveAs();
    bool saveTo(const QString& path);
    bool confirmDiscard();
    void exportMesh(bool threeMf);

    Viewport* viewport_;
    FeaturePanel* panel_;
    AiPanel* ai_;
    GuideOverlay* guide_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QMenu* fileMenu_ = nullptr;
    QLabel* stats_;
    QSpinBox* bedWidth_;
    QSpinBox* bedDepth_;
    QAction* undoAction_;
    QAction* redoAction_;
    QAction* selectAction_;
    QDockWidget* designDock_;
    QDockWidget* aiDock_;
    HoverMenu* primitivesMenu_;
    PlacementController* placement_;
    ModifyController* modify_;
    SelectionController* selection_;
    TransformController* transform_;
    StepTool* steps_;
    PatternController* pattern_;
    ViewportChrome* chrome_;
    class QDialog* gridDialog_ = nullptr;
    bool placingFromDrop_ = false;

    td::Document document_;
    std::vector<td::EvalResult> bodyResults_;  // world space, one per body
    td::EvalResult combined_;                  // everything as one solid, as printed
    td::Mesh mesh_;                            // all bodies together, for placing new shapes on them
    td::DocumentEvaluator evaluator_;
    std::vector<td::Document> undoStack_, redoStack_;
    bool meshCurrent_ = true;

    int modifyOwner_ = -1;
    std::vector<int> transformTargets_;
    class QDialog* alignDialog_ = nullptr;
    std::vector<int> alignMoving_;
    std::vector<int> grabTargets_;
    td::V3 grabPoint_;
    td::V3 grabShift_;

    std::vector<td::Body> clipboard_;
    std::array<double, 3> clipboardMin_{}, clipboardMax_{};
    QString lastCommand_;

    QString path_;
    bool modified_ = false;
    int lastEditedIndex_ = -1;
    QElapsedTimer sinceLastEdit_;
};
