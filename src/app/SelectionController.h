#pragma once

#include "app/Viewport.h"
#include "core/Document.h"
#include "core/Geometry.h"
#include "core/Picking.h"
#include "core/Topology.h"

#include <QObject>
#include <QPoint>
#include <QRect>

#include <map>
#include <optional>
#include <vector>

class FeaturePanel;
class QFrame;
class QTimer;
class QToolButton;

enum class SubKind { None, Face, Edge, Vertex };

// Tracks what is selected, from whole bodies and groups down to single faces,
// edges and corners, and draws it. A first click takes the body (or its whole
// group); a second drops to the piece of geometry under the pointer; a
// double-click on a group steps inside it.
class SelectionController : public QObject {
    Q_OBJECT
public:
    SelectionController(Viewport* viewport, FeaturePanel* panel, const td::Document* document, const std::vector<td::EvalResult>* bodies,
                        QObject* parent = nullptr);

    struct Hit {
        int body;
        td::RayHit hit;
    };
    std::optional<Hit> hitAt(QPoint position) const;

    void handleClick(QPoint position, bool shift);
    void handleDoubleClick(QPoint position);
    void handleBox(const QRect& rect, bool additive, bool crossing);
    void clear();
    void selectBodies(const std::vector<int>& bodies);
    void selectSub(int body, SubKind kind, std::vector<int> ids);  // faces, edges or points of one body
    bool leaveGroup();  // false when not inside a group

    // The bodies changed, so ids into their faces and edges no longer mean anything.
    void meshChanged();
    void refreshGraphics();
    void setPreviewing(bool previewing);  // an edit is on display, so old geometry must not show

    std::vector<int> selectedBodies() const;
    SubKind subKind() const { return subKind_; }
    const std::vector<int>& subIds() const { return subIds_; }
    int subOwner() const { return subOwner_; }
    const td::Topology& topology() { return topologyOf(subOwner_); }
    const td::Topology& topologyOf(int body);
    const std::vector<int>& groupContext() const { return context_; }
    // The edge of a body nearest the pointer on screen, or -1 when none is close.
    int edgeNear(int body, QPoint position, double pixels = 12.0);

signals:
    void gearAction(const QString& tool);
    void subSelectionChanged();
    void contextChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    const td::Mesh& bodyMesh(int body) const { return (*bodies_)[body].mesh; }
    void refreshHighlights();
    int silhouetteEdgeAt(int body, QPoint position, td::V3 direction);
    void pickSub(int body, QPoint position, const td::RayHit& hit, SubKind* kind, int* id);
    void selectParts(const std::vector<int>& rows);
    void setSub(SubKind kind, std::vector<int> ids);
    td::Mesh entityMesh(SubKind kind, int id);
    void updateGear();
    void showGearMenu();
    void rebuildGearMenu();

    Viewport* viewport_;
    FeaturePanel* panel_;
    const td::Document* document_;
    const std::vector<td::EvalResult>* bodies_;

    std::map<int, td::Topology> topologies_;
    std::map<int, std::vector<float>> outlines_;
    std::vector<int> context_;

    SubKind subKind_ = SubKind::None;
    std::vector<int> subIds_;
    int subOwner_ = -1;

    QToolButton* gear_;
    QFrame* gearMenu_;
    QTimer* gearHideTimer_;
    QPoint gearAnchor_;
    bool previewing_ = false;
};
