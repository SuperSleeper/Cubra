#pragma once

#include "core/Geometry.h"
#include "core/Vec.h"

#include <QMatrix4x4>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <QRect>
#include <QRubberBand>
#include <QVector3D>
#include <QVector4D>

#include <array>
#include <functional>
#include <optional>

class QMouseEvent;

// What the parts drawer puts in a drag: the id of the part or colour.
inline constexpr const char* kDrawerMime = "application/x-cubra-drawer";

// While set, a tool sees mouse input first. Returning true consumes the event.
class ViewportTool {
public:
    virtual ~ViewportTool() = default;
    virtual bool toolMousePress(QMouseEvent* event) = 0;
    virtual bool toolMouseMove(QMouseEvent* event) = 0;
    virtual bool toolMouseRelease(QMouseEvent* event) = 0;
};

class Viewport : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    // One mesh per body, in world space, each drawn in its own colour.
    void setBodies(const std::vector<td::Mesh>& meshes, const std::vector<QVector4D>& colors);
    void setBodyMesh(int body, const td::Mesh& mesh);
    // Moves a body on screen only, for a live preview of a transform.
    void setBodyPreview(int body, const QMatrix4x4& change);
    void clearBodyPreviews();
    void setOverlay(const td::Mesh& mesh);  // translucent preview of the selected feature
    void setBed(int width, int depth);
    void fit(const std::array<double, 3>& min, const std::array<double, 3>& max);
    void fitBed();

    // How solids are drawn.
    enum class Style { MaterialsAndOutlines, MaterialsOnly, OutlinesOnly, XRay };
    void setStyle(Style style);
    Style style() const { return style_; }
    void setGridVisible(bool visible);
    bool gridVisible() const { return gridVisible_; }

    // The camera's direction, in degrees: yaw around the bed, pitch above it.
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    // Glides there when animate is set, so the change of view is easy to follow.
    void setOrientation(float yaw, float pitch, bool animate);
    void orbitBy(QPointF pixels);  // as a right-drag of this many pixels

    // Orbit around the part: turning the view circles the point pivot gives (the selected
    // part's centre, say) instead of the middle of the view. With no point, or with it off,
    // the view turns about its own middle as usual.
    void setOrbitPivot(std::function<std::optional<QVector3D>()> pivot) { pivot_ = std::move(pivot); }
    // While on, the view also keeps the part in the middle: selecting another part, or an
    // edit that moves it, glides it back. Panning turns it off, for free movement.
    void setOrbitAroundPart(bool on);
    bool orbitAroundPart() const { return orbitAroundPart_; }
    // Slides the view, without turning or zooming, until point is in the middle.
    void centreOn(const QVector3D& point, bool animate);
    QMatrix4x4 viewRotation() const;  // the view without its position, for the ViewCube

    // What a plain left drag does to the camera. Right drag always orbits, middle drag pans.
    enum class Navigation { Orbit, Pan, Zoom };
    void setNavigation(Navigation mode) {
        navigation_ = mode;
        emit navigationChanged(mode);
    }
    Navigation navigation() const { return navigation_; }

    // In select mode a left drag draws a selection box instead of orbiting.
    void setSelectMode(bool enabled);
    std::array<float, 16> viewProjection() const;

    struct TintedMesh {
        td::Mesh mesh;
        QVector4D color;
    };

    // Clearing the tool hands keyboard focus back from the tool's boxes, so
    // shortcuts such as Delete work again instead of editing a hidden box.
    void setTool(ViewportTool* tool);

    // Decides whether a left press at this point grabs what is under it, so a
    // drag moves that instead of turning the view.
    void setGrabTest(std::function<bool(QPoint)> test) { grabTest_ = std::move(test); }
    void setGhost(const td::Mesh& localMesh, const std::vector<float>& outline);
    void setGhostModel(const QMatrix4x4& model);
    void setGhostColor(const QVector4D& color);
    void setHandles(const std::vector<TintedMesh>& handles);  // world coordinates
    void setHighlights(const std::vector<TintedMesh>& highlights);  // selected faces, edges, corners
    void setOverlayOutline(const std::vector<float>& lines);
    void clearPlacementGraphics();

    void cursorRay(QPointF position, td::V3* origin, td::V3* direction) const;
    QPointF project(td::V3 point) const;
    double worldPerPixel(td::V3 point) const;

signals:
    void orbitAroundPartChanged(bool on);  // also when panning turns it off
    void marqueeFinished(const QRect& rect, bool additive, bool crossing);
    void clicked(QPoint position, bool shift);
    void doubleClicked(QPoint position);
    void contextMenuRequested(QPoint position);  // a right click that was not a drag
    void navigationChanged(Viewport::Navigation mode);
    // A part or colour dragged in from the drawer. payload is the drawer item's id.
    void dropEntered(const QString& payload, QPoint position);
    void dropMoved(QPoint position);
    void dropFinished(const QString& payload, QPoint position);
    void dropLeft();
    void grabStarted(QPoint position);
    void grabMoved(QPoint position);
    void grabFinished(QPoint position);
    void cameraChanged();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct Buffer {
        unsigned vao = 0, vbo = 0, ebo = 0;
        int count = 0;
    };

    void upload(Buffer& buffer, const std::vector<float>& positions, const std::vector<uint32_t>* indices);
    void release(Buffer& buffer);
    void rebuildGrid();
    QVector3D eye() const;
    QMatrix4x4 projectionMatrix() const;
    QMatrix4x4 viewMatrix() const;

    ViewportTool* tool_ = nullptr;
    Buffer ghost_, ghostOutline_;
    std::vector<Buffer> handleBuffers_;
    td::Mesh pendingGhost_;
    std::vector<float> pendingOutline_;
    std::vector<TintedMesh> pendingHandles_;
    std::vector<QVector4D> handleColors_;
    bool ghostDirty_ = false, handlesDirty_ = false;
    QMatrix4x4 ghostModel_;
    QVector4D ghostColor_;

    bool glReady_ = false;
    Buffer overlayOutline_;
    std::vector<float> pendingOverlayOutline_;
    bool overlayOutlineDirty_ = false;
    std::vector<Buffer> highlightBuffers_;
    std::vector<TintedMesh> pendingHighlights_;
    std::vector<QVector4D> highlightColors_;
    bool highlightsDirty_ = false;

    struct BodyView {
        td::DisplayMesh display;
        std::vector<float> outline;  // creases, as line segments
        QVector4D color;
        QMatrix4x4 preview;
        bool dirty = true;
    };
    struct GpuBody {
        Buffer buffer;
        unsigned normals = 0;
        Buffer outline;
    };
    std::vector<BodyView> bodyViews_;
    std::vector<GpuBody> gpuBodies_;
    bool swallowRelease_ = false;
    Style style_ = Style::MaterialsAndOutlines;
    bool gridVisible_ = true;
    QString dropPayload_;
    class QVariantAnimation* turn_ = nullptr;
    class QVariantAnimation* glide_ = nullptr;
    class QTimer* follow_ = nullptr;
    std::optional<QVector3D> centred_;  // the part centre the view was last moved to
    void followPart();
    std::function<std::optional<QVector3D>()> pivot_;
    bool orbitAroundPart_ = false;
    void turnBy(float yawDegrees, float pitchDegrees, float pitchLimit);
    Navigation navigation_ = Navigation::Orbit;
    bool rightPressed_ = false;
    bool rightDragging_ = false;
    QPoint rightPressPosition_;
    std::function<bool(QPoint)> grabTest_;
    bool grabCandidate_ = false;
    bool grabbing_ = false;

    bool leftPressed_ = false;
    bool leftDragging_ = false;
    QPoint leftPressPosition_;
    bool selectMode_ = false;
    bool selecting_ = false;
    QPoint marqueeOrigin_;
    QRubberBand* rubberBand_ = nullptr;

    QOpenGLShaderProgram program_;
    Buffer overlay_, minorGrid_, majorGrid_, axisX_, axisY_;
    td::Mesh pendingOverlay_;
    bool overlayDirty_ = false, gridDirty_ = true;

    int bedWidth_ = 220, bedDepth_ = 220;
    QVector3D target_{0, 0, 0};
    float yaw_ = -60, pitch_ = 30, distance_ = 400;
    QPoint lastMouse_;
};
