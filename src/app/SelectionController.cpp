#include "app/SelectionController.h"

#include "app/FeaturePanel.h"
#include "app/Icons.h"
#include "core/Placement.h"

#include <QEvent>
#include <QFrame>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using td::V3;

namespace {

constexpr double kVertexPixels = 9.0;
constexpr double kEdgePixels = 7.0;
constexpr int kGearOffset = 18;
constexpr int kGearHideMs = 250;
const QVector4D kFaceColor(0.25f, 0.78f, 1.0f, 0.45f);
const QVector4D kEdgeColor(0.25f, 0.88f, 1.0f, 1.0f);

double segmentDistance(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    const double t = lengthSquared > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / lengthSquared, 0.0, 1.0) : 0.0;
    const QPointF offset = p - (a + ab * t);
    return std::hypot(offset.x(), offset.y());
}

void appendMesh(td::Mesh& target, const td::Mesh& source) {
    const auto offset = uint32_t(target.vertexCount());
    target.positions.insert(target.positions.end(), source.positions.begin(), source.positions.end());
    for (uint32_t index : source.indices) target.indices.push_back(index + offset);
}

bool allInside(const td::Mesh& mesh, const Viewport* viewport, const QRect& rect) {
    for (size_t v = 0; v < mesh.vertexCount(); ++v)
        if (!QRectF(rect).contains(viewport->project(td::meshVertex(mesh, uint32_t(v))))) return false;
    return !mesh.empty();
}

bool touches(const td::Mesh& mesh, const Viewport* viewport, const QRect& rect) {
    const td::ScreenRect screen{double(rect.left()), double(rect.top()), double(rect.right()), double(rect.bottom())};
    return td::pickMesh(mesh, viewport->viewProjection(), viewport->width(), viewport->height(), screen).has_value();
}

}  // namespace

SelectionController::SelectionController(Viewport* viewport, FeaturePanel* panel, const td::Document* document,
                                         const std::vector<td::EvalResult>* bodies, QObject* parent)
    : QObject(parent), viewport_(viewport), panel_(panel), document_(document), bodies_(bodies) {
    const QColor ink = viewport_->palette().color(QPalette::WindowText);

    gear_ = new QToolButton(viewport_);
    gear_->setObjectName("gearButton");
    gear_->setIcon(makeIcon(Icon::Gear, ink));
    gear_->setIconSize(QSize(22, 22));
    gear_->setToolTip(tr("Tools for this selection"));
    gear_->installEventFilter(this);
    gear_->hide();
    connect(gear_, &QToolButton::clicked, this, &SelectionController::showGearMenu);

    gearMenu_ = new QFrame(viewport_);
    gearMenu_->setObjectName("gearMenu");
    gearMenu_->setFrameShape(QFrame::StyledPanel);
    gearMenu_->setAutoFillBackground(true);
    gearMenu_->installEventFilter(this);
    gearMenu_->hide();
    auto* layout = new QVBoxLayout(gearMenu_);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    gearHideTimer_ = new QTimer(this);
    gearHideTimer_->setSingleShot(true);
    gearHideTimer_->setInterval(kGearHideMs);
    connect(gearHideTimer_, &QTimer::timeout, gearMenu_, &QWidget::hide);

    // Only the on-screen thickness of highlights depends on the camera.
    connect(viewport_, &Viewport::cameraChanged, this, [this] {
        if (subKind_ == SubKind::Edge || subKind_ == SubKind::Vertex) refreshHighlights();
    });
    connect(panel_, &FeaturePanel::selectionChanged, this, [this] {
        // Picking a body in the list leaves sub-entity focus.
        const bool leftSubLevel = !panel_->selectedRows().empty() && subKind_ != SubKind::None;
        if (leftSubLevel) {
            subKind_ = SubKind::None;
            subIds_.clear();
            subOwner_ = -1;
        }
        refreshGraphics();
        updateGear();
        if (leftSubLevel) emit subSelectionChanged();
    });
}

std::vector<int> SelectionController::selectedBodies() const { return panel_->selectedRows(); }

const td::Topology& SelectionController::topologyOf(int body) {
    static const td::Topology none;
    if (body < 0 || body >= int(bodies_->size())) return none;
    const auto found = topologies_.find(body);
    if (found != topologies_.end()) return found->second;
    return topologies_.emplace(body, td::buildTopology(bodyMesh(body))).first->second;
}

std::optional<SelectionController::Hit> SelectionController::hitAt(QPoint position) const {
    V3 origin, direction;
    viewport_->cursorRay(position, &origin, &direction);
    std::optional<Hit> best;
    for (int b = 0; b < int(bodies_->size()); ++b) {
        if (document_->bodies[b].hidden) continue;
        const auto hit = td::raycastMesh(bodyMesh(b), origin, direction);
        if (hit && (!best || hit->t < best->hit.t)) best = Hit{b, *hit};
    }
    return best;
}

void SelectionController::meshChanged() {
    topologies_.clear();
    outlines_.clear();
    subKind_ = SubKind::None;
    subIds_.clear();
    subOwner_ = -1;
    // Group paths may refer to groups that were just dissolved.
    while (!context_.empty() && std::none_of(document_->bodies.begin(), document_->bodies.end(), [&](const td::Body& b) { return td::inContext(b, context_); }))
        context_.pop_back();
    refreshGraphics();
    updateGear();
}

void SelectionController::clear() {
    subKind_ = SubKind::None;
    subIds_.clear();
    subOwner_ = -1;
    panel_->setSelectedRows({});
    refreshGraphics();
    updateGear();
    emit subSelectionChanged();
}

void SelectionController::selectBodies(const std::vector<int>& bodies) { selectParts(bodies); }

void SelectionController::selectParts(const std::vector<int>& rows) {
    subKind_ = SubKind::None;
    subIds_.clear();
    subOwner_ = -1;
    std::vector<int> sorted = rows;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    panel_->setSelectedRows(sorted);
    emit subSelectionChanged();
}

void SelectionController::setSub(SubKind kind, std::vector<int> ids) {
    subKind_ = ids.empty() ? SubKind::None : kind;
    subIds_ = std::move(ids);
    panel_->setSelectedRows({});
    refreshGraphics();
    updateGear();
    emit subSelectionChanged();
}

void SelectionController::selectSub(int body, SubKind kind, std::vector<int> ids) {
    subOwner_ = body;
    setSub(kind, std::move(ids));
}

bool SelectionController::leaveGroup() {
    if (context_.empty()) return false;
    const int left = context_.back();
    context_.pop_back();
    // Leave with the group just exited selected, so the user sees where they were.
    std::vector<int> members;
    for (int b = 0; b < int(document_->bodies.size()); ++b) {
        const auto& groups = document_->bodies[b].groups;
        if (std::find(groups.begin(), groups.end(), left) != groups.end()) members.push_back(b);
    }
    selectParts(members.empty() ? std::vector<int>{} : td::unitOf(*document_, members.front(), context_));
    emit contextChanged();
    return true;
}

void SelectionController::pickSub(int body, QPoint position, const td::RayHit& hit, SubKind* kind, int* id) {
    const td::Mesh& mesh = bodyMesh(body);
    const td::Topology& topo = topologyOf(body);
    const int face = topo.faceOfTriangle[hit.triangle];
    const auto onFace = [&](uint32_t triangle) { return topo.faceOfTriangle[triangle] == face; };

    // Only geometry bounding the face under the pointer counts, so nothing hidden is picked.
    double bestVertex = kVertexPixels;
    int vertexHit = -1;
    for (int c = 0; c < int(topo.corners.size()); ++c) {
        const uint32_t vertex = topo.corners[c];
        bool bounds = false;
        for (uint32_t t : topo.faces[face].triangles)
            if (mesh.indices[t * 3] == vertex || mesh.indices[t * 3 + 1] == vertex || mesh.indices[t * 3 + 2] == vertex) bounds = true;
        if (!bounds) continue;
        const QPointF screen = viewport_->project(td::meshVertex(mesh, vertex));
        const double distance = std::hypot(screen.x() - position.x(), screen.y() - position.y());
        if (distance <= bestVertex) {
            bestVertex = distance;
            vertexHit = c;
        }
    }
    if (vertexHit >= 0) {
        *kind = SubKind::Vertex;
        *id = vertexHit;
        return;
    }

    double bestEdge = kEdgePixels;
    int edgeHit = -1;
    for (int e = 0; e < int(topo.edges.size()); ++e) {
        const td::TopoEdge& edge = topo.edges[e];
        for (size_t s = 0; s < edge.segmentCount(); ++s) {
            if (!onFace(edge.segmentTriangles[s][0]) && !onFace(edge.segmentTriangles[s][1])) continue;
            const QPointF a = viewport_->project(td::meshVertex(mesh, edge.vertices[s]));
            const QPointF b = viewport_->project(td::meshVertex(mesh, edge.vertices[(s + 1) % edge.vertices.size()]));
            const double distance = segmentDistance(position, a, b);
            if (distance <= bestEdge) {
                bestEdge = distance;
                edgeHit = e;
            }
        }
    }
    if (edgeHit >= 0) {
        *kind = SubKind::Edge;
        *id = edgeHit;
        return;
    }
    *kind = SubKind::Face;
    *id = face;
}

int SelectionController::edgeNear(int body, QPoint position, double pixels) {
    if (body < 0 || body >= int(bodies_->size())) return -1;
    const td::Mesh& mesh = bodyMesh(body);
    const td::Topology& topo = topologyOf(body);
    double best = pixels;
    int found = -1;
    for (int e = 0; e < int(topo.edges.size()); ++e) {
        const td::TopoEdge& edge = topo.edges[e];
        for (size_t s = 0; s < edge.segmentCount(); ++s) {
            const QPointF a = viewport_->project(td::meshVertex(mesh, edge.vertices[s]));
            const QPointF b = viewport_->project(td::meshVertex(mesh, edge.vertices[(s + 1) % edge.vertices.size()]));
            const double distance = segmentDistance(position, a, b);
            if (distance <= best) {
                best = distance;
                found = e;
            }
        }
    }
    return found;
}

int SelectionController::silhouetteEdgeAt(int body, QPoint position, V3 direction) {
    if (body < 0) return -1;
    const td::Mesh& mesh = bodyMesh(body);
    const td::Topology& topo = topologyOf(body);
    double best = kEdgePixels;
    int found = -1;
    for (int e = 0; e < int(topo.edges.size()); ++e) {
        const td::TopoEdge& edge = topo.edges[e];
        for (size_t s = 0; s < edge.segmentCount(); ++s) {
            // On the outline, exactly one of the two adjoining triangles faces the viewer.
            const bool front1 = td::dot(td::triangleNormal(mesh, edge.segmentTriangles[s][0]), direction) < 0;
            const bool front2 = td::dot(td::triangleNormal(mesh, edge.segmentTriangles[s][1]), direction) < 0;
            if (front1 == front2) continue;
            const QPointF a = viewport_->project(td::meshVertex(mesh, edge.vertices[s]));
            const QPointF b = viewport_->project(td::meshVertex(mesh, edge.vertices[(s + 1) % edge.vertices.size()]));
            const double distance = segmentDistance(position, a, b);
            if (distance <= best) {
                best = distance;
                found = e;
            }
        }
    }
    return found;
}

void SelectionController::handleClick(QPoint position, bool shift) {
    gearAnchor_ = position;
    const auto found = hitAt(position);
    if (!found) {
        // Half of an edge on a body's outline lies just off the solid.
        const std::vector<int> rows = selectedBodies();
        const int owner = subKind_ != SubKind::None ? subOwner_ : (rows.size() == 1 ? rows.front() : -1);
        V3 origin, direction;
        viewport_->cursorRay(position, &origin, &direction);
        const int edge = silhouetteEdgeAt(owner, position, direction);
        if (edge >= 0) {
            std::vector<int> ids{edge};
            if (shift && subKind_ == SubKind::Edge) {
                ids = subIds_;
                const auto it = std::find(ids.begin(), ids.end(), edge);
                if (it != ids.end()) ids.erase(it);
                else ids.push_back(edge);
            }
            subOwner_ = owner;
            setSub(SubKind::Edge, std::move(ids));
            return;
        }
        if (!shift) clear();
        return;
    }

    const int body = found->body;
    // Clicking outside the group being edited steps back out of it.
    if (!td::inContext(document_->bodies[body], context_)) {
        context_.clear();
        emit contextChanged();
    }
    const std::vector<int> unit = td::unitOf(*document_, body, context_);
    const bool isGroup = td::isGroupUnit(*document_, body, context_);
    std::vector<int> rows = selectedBodies();
    const bool unitSelected = std::all_of(unit.begin(), unit.end(), [&](int b) { return std::find(rows.begin(), rows.end(), b) != rows.end(); });

    if (subKind_ != SubKind::None && (body == subOwner_ || (shift && body == subOwner_))) {
        SubKind kind;
        int id;
        pickSub(body, position, found->hit, &kind, &id);
        if (!shift || kind != subKind_) {
            setSub(kind, {id});
            return;
        }
        // Shift toggles this piece within the set.
        std::vector<int> ids = subIds_;
        const auto it = std::find(ids.begin(), ids.end(), id);
        if (it != ids.end()) ids.erase(it);
        else ids.push_back(id);
        setSub(kind, std::move(ids));
        return;
    }

    if (shift) {
        if (unitSelected)
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](int b) { return std::find(unit.begin(), unit.end(), b) != unit.end(); }), rows.end());
        else
            rows.insert(rows.end(), unit.begin(), unit.end());
        selectParts(rows);
        return;
    }

    // A second click on a selected body drops to its faces, edges and corners.
    // A group stays whole: double-click steps inside it instead.
    if (unitSelected && !isGroup && rows.size() == unit.size()) {
        SubKind kind;
        int id;
        pickSub(body, position, found->hit, &kind, &id);
        subOwner_ = body;
        setSub(kind, {id});
        return;
    }
    selectParts(unit);
}

void SelectionController::handleDoubleClick(QPoint position) {
    const auto found = hitAt(position);
    if (!found) {
        if (!leaveGroup()) clear();
        return;
    }
    const int body = found->body;
    if (!td::inContext(document_->bodies[body], context_)) context_.clear();
    if (td::isGroupUnit(*document_, body, context_)) {
        context_.push_back(document_->bodies[body].groups[context_.size()]);
        emit contextChanged();
    }
    selectParts(td::unitOf(*document_, body, context_));
}

td::Mesh SelectionController::entityMesh(SubKind kind, int id) {
    td::Mesh out;
    if (subOwner_ < 0) return out;
    const td::Mesh& mesh = bodyMesh(subOwner_);
    const td::Topology& topo = topologyOf(subOwner_);
    const auto push = [&out](V3 p) {
        out.positions.insert(out.positions.end(), {float(p.x), float(p.y), float(p.z)});
        return uint32_t(out.vertexCount() - 1);
    };
    if (kind == SubKind::Face) {
        for (uint32_t t : topo.faces[id].triangles)
            for (int i = 0; i < 3; ++i) out.indices.push_back(push(td::meshVertex(mesh, mesh.indices[t * 3 + i])));
    } else if (kind == SubKind::Edge) {
        const td::TopoEdge& edge = topo.edges[id];
        for (size_t s = 0; s < edge.segmentCount(); ++s) {
            const uint32_t a = push(td::meshVertex(mesh, edge.vertices[s]));
            const uint32_t b = push(td::meshVertex(mesh, edge.vertices[(s + 1) % edge.vertices.size()]));
            out.indices.insert(out.indices.end(), {a, b, b});
        }
    } else if (kind == SubKind::Vertex) {
        const uint32_t a = push(td::meshVertex(mesh, topo.corners[id]));
        out.indices.insert(out.indices.end(), {a, a, a});
    }
    return out;
}

void SelectionController::handleBox(const QRect& rect, bool additive, bool crossing) {
    gearAnchor_ = rect.center();
    const auto matches = [&](const td::Mesh& mesh) { return crossing ? touches(mesh, viewport_, rect) : allInside(mesh, viewport_, rect); };

    if (subKind_ != SubKind::None && subOwner_ >= 0) {
        const td::Topology& topo = topologyOf(subOwner_);
        const int count = subKind_ == SubKind::Face   ? int(topo.faces.size())
                          : subKind_ == SubKind::Edge ? int(topo.edges.size())
                                                      : int(topo.corners.size());
        std::vector<int> ids = additive ? subIds_ : std::vector<int>{};
        for (int id = 0; id < count; ++id)
            if (matches(entityMesh(subKind_, id)) && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        setSub(subKind_, std::move(ids));
        return;
    }

    std::vector<int> rows = additive ? selectedBodies() : std::vector<int>{};
    for (int b = 0; b < int(bodies_->size()); ++b) {
        if (document_->bodies[b].hidden || !td::inContext(document_->bodies[b], context_) || !matches(bodyMesh(b))) continue;
        for (int member : td::unitOf(*document_, b, context_)) rows.push_back(member);
    }
    selectParts(rows);
}

void SelectionController::refreshGraphics() {
    td::Mesh combined;
    std::vector<float> outline;
    for (int row : selectedBodies()) {
        if (row >= int(bodies_->size())) continue;
        appendMesh(combined, bodyMesh(row));
        auto cached = outlines_.find(row);
        if (cached == outlines_.end()) cached = outlines_.emplace(row, td::sharpEdges(bodyMesh(row))).first;
        outline.insert(outline.end(), cached->second.begin(), cached->second.end());
    }
    viewport_->setOverlay(combined);
    viewport_->setOverlayOutline(outline);
    refreshHighlights();
}

void SelectionController::refreshHighlights() {
    std::vector<Viewport::TintedMesh> highlights;
    // While a tool previews its edit, the old geometry would float over the new.
    if (subKind_ != SubKind::None && !previewing_ && subOwner_ >= 0) {
        const td::Mesh& mesh = bodyMesh(subOwner_);
        const td::Topology& topo = topologyOf(subOwner_);
        if (subKind_ == SubKind::Face) {
            td::Mesh faces;
            for (int id : subIds_) appendMesh(faces, entityMesh(SubKind::Face, id));
            highlights.push_back({faces, kFaceColor});
        } else if (subKind_ == SubKind::Edge) {
            td::Mesh tubes;
            for (int id : subIds_) {
                const td::TopoEdge& edge = topo.edges[id];
                for (size_t s = 0; s < edge.segmentCount(); ++s) {
                    const V3 a = td::meshVertex(mesh, edge.vertices[s]);
                    const V3 b = td::meshVertex(mesh, edge.vertices[(s + 1) % edge.vertices.size()]);
                    appendMesh(tubes, td::makeTube(a, b, viewport_->worldPerPixel(a) * 2.0));
                }
            }
            highlights.push_back({tubes, kEdgeColor});
        } else {
            td::Mesh markers;
            for (int id : subIds_) {
                const V3 p = td::meshVertex(mesh, topo.corners[id]);
                appendMesh(markers, td::makeMarker(p, viewport_->worldPerPixel(p) * 6.0));
            }
            highlights.push_back({markers, kEdgeColor});
        }
    }
    viewport_->setHighlights(highlights);
}

void SelectionController::setPreviewing(bool previewing) {
    if (previewing == previewing_) return;
    previewing_ = previewing;
    refreshHighlights();
    updateGear();  // it must not sit on top of the arrow during an edit
}

void SelectionController::updateGear() {
    const bool show = !previewing_ && (subKind_ != SubKind::None || !selectedBodies().empty());
    gearMenu_->hide();
    gear_->setVisible(show);
    if (!show) return;
    const QPoint spot = gearAnchor_ + QPoint(kGearOffset, -kGearOffset - gear_->sizeHint().height());
    gear_->move(std::clamp(spot.x(), 0, std::max(0, viewport_->width() - gear_->sizeHint().width())),
                std::clamp(spot.y(), 0, std::max(0, viewport_->height() - gear_->sizeHint().height())));
    gear_->raise();
}

void SelectionController::rebuildGearMenu() {
    const QList<QToolButton*> old = gearMenu_->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly);
    for (QToolButton* button : old) delete button;

    struct Item {
        const char* id;
        const char* label;
        bool available;
    };
    std::vector<Item> items;
    if (subKind_ == SubKind::Edge) items = {{"Fillet", "Fillet (F)", true}, {"Chamfer", "Chamfer (C)", true}, {"Tweak", "Tweak", true}};
    else if (subKind_ == SubKind::Vertex) items = {{"Fillet", "Fillet (F)", true}};
    else if (subKind_ == SubKind::Face) items = {{"Extrude", "Extrude", true}, {"Press Pull", "Press Pull (P)", true}, {"Split Face", "Split Face", true}, {"Shell", "Shell", true}};
    else items = {{"Move", "Move / Rotate (M)", true}, {"Scale", "Scale (S)", true}, {"Delete", "Delete", true}};

    auto* layout = gearMenu_->layout();
    for (const Item& item : items) {
        auto* button = new QToolButton(gearMenu_);
        button->setObjectName(QStringLiteral("gear_") + item.id);
        button->setText(tr(item.label));
        button->setAutoRaise(true);
        button->setEnabled(item.available);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (!item.available) button->setToolTip(tr("Not available yet"));
        layout->addWidget(button);
        const QString tool = QString::fromLatin1(item.id);
        connect(button, &QToolButton::clicked, this, [this, tool] {
            gearMenu_->hide();
            emit gearAction(tool);
        });
    }
}

void SelectionController::showGearMenu() {
    gearHideTimer_->stop();
    rebuildGearMenu();
    gearMenu_->adjustSize();
    const QPoint below = gear_->geometry().bottomLeft();
    gearMenu_->move(std::clamp(below.x(), 0, std::max(0, viewport_->width() - gearMenu_->width())),
                    std::clamp(below.y(), 0, std::max(0, viewport_->height() - gearMenu_->height())));
    gearMenu_->show();
    gearMenu_->raise();
}

bool SelectionController::eventFilter(QObject* watched, QEvent* event) {
    if (watched == gear_ || watched == gearMenu_) {
        if (event->type() == QEvent::Enter) {
            if (watched == gear_ && gearMenu_->isHidden()) showGearMenu();
            gearHideTimer_->stop();
        } else if (event->type() == QEvent::Leave) {
            gearHideTimer_->start();
        }
    }
    return QObject::eventFilter(watched, event);
}
