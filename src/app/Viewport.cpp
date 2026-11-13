#include "app/Viewport.h"

#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kFovDegrees = 35.0f;
constexpr int kDragStartPixels = 4;

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
out vec3 vNormal;
uniform mat4 uMvp;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
out vec3 vPos;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    gl_Position = uMvp * world;
}
)";

// Normals come from screen-space derivatives, giving accurate flat shading of
// each facet without storing normals in the mesh.
const char* kFragmentShader = R"(#version 330 core
in vec3 vPos;
in vec3 vNormal;
out vec4 fragColor;
uniform int uSmooth;
uniform vec4 uColor;
uniform int uLit;
uniform vec3 uEye;
void main() {
    if (uLit == 0) { fragColor = uColor; return; }
    vec3 v = normalize(uEye - vPos);
    vec3 n = uSmooth == 1 ? normalize(vNormal) : normalize(cross(dFdx(vPos), dFdy(vPos)));
    if (dot(n, v) < 0.0) n = -n;
    float head = max(dot(n, v), 0.0);
    float sky = 0.5 + 0.5 * n.z;
    float key = max(dot(n, normalize(vec3(0.45, -0.55, 0.7))), 0.0);
    float light = 0.20 + 0.25 * sky + 0.35 * head + 0.30 * key;
    fragColor = vec4(uColor.rgb * light, uColor.a);
}
)";

}  // namespace

Viewport::Viewport(QWidget* parent) : QOpenGLWidget(parent), rubberBand_(new QRubberBand(QRubberBand::Rectangle, this)) {
    setMinimumSize(320, 240);
    setAcceptDrops(true);
}

void Viewport::setStyle(Style style) {
    style_ = style;
    update();
}

void Viewport::setGridVisible(bool visible) {
    gridVisible_ = visible;
    update();
}

void Viewport::setOrientation(float yaw, float pitch, bool animate) {
    pitch = std::clamp(pitch, -89.9f, 89.9f);
    if (turn_) turn_->stop();
    if (!animate) {
        yaw_ = yaw;
        pitch_ = pitch;
        update();
        emit cameraChanged();
        return;
    }
    // Turn the short way round.
    float from = yaw_;
    while (yaw - from > 180) from += 360;
    while (from - yaw > 180) from -= 360;
    const float startPitch = pitch_;
    if (!turn_) turn_ = new QVariantAnimation(this);
    turn_->disconnect(this);
    turn_->setDuration(280);
    turn_->setEasingCurve(QEasingCurve::InOutCubic);
    turn_->setStartValue(0.0);
    turn_->setEndValue(1.0);
    connect(turn_, &QVariantAnimation::valueChanged, this, [this, from, yaw, startPitch, pitch](const QVariant& value) {
        const float t = value.toFloat();
        yaw_ = from + (yaw - from) * t;
        pitch_ = startPitch + (pitch - startPitch) * t;
        update();
        emit cameraChanged();
    });
    turn_->start();
}

void Viewport::orbitBy(QPointF pixels) {
    yaw_ -= float(pixels.x()) * 0.4f;
    pitch_ = std::clamp(pitch_ + float(pixels.y()) * 0.4f, -89.9f, 89.9f);
    update();
    emit cameraChanged();
}

QMatrix4x4 Viewport::viewRotation() const {
    QMatrix4x4 view = viewMatrix();
    view.setColumn(3, QVector4D(0, 0, 0, 1));
    return view;
}

void Viewport::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasFormat(kDrawerMime)) return;
    dropPayload_ = QString::fromUtf8(event->mimeData()->data(kDrawerMime));
    event->acceptProposedAction();
    emit dropEntered(dropPayload_, event->position().toPoint());
}

void Viewport::dragMoveEvent(QDragMoveEvent* event) {
    if (!event->mimeData()->hasFormat(kDrawerMime)) return;
    event->acceptProposedAction();
    emit dropMoved(event->position().toPoint());
}

void Viewport::dragLeaveEvent(QDragLeaveEvent*) { emit dropLeft(); }

void Viewport::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasFormat(kDrawerMime)) return;
    event->acceptProposedAction();
    emit dropFinished(QString::fromUtf8(event->mimeData()->data(kDrawerMime)), event->position().toPoint());
}

void Viewport::setGhost(const td::Mesh& localMesh, const std::vector<float>& outline) {
    pendingGhost_ = localMesh;
    pendingOutline_ = outline;
    ghostDirty_ = true;
    update();
}

void Viewport::setGhostModel(const QMatrix4x4& model) {
    ghostModel_ = model;
    update();
}

void Viewport::setGhostColor(const QVector4D& color) {
    ghostColor_ = color;
    update();
}

void Viewport::setHandles(const std::vector<TintedMesh>& handles) {
    pendingHandles_ = handles;
    handlesDirty_ = true;
    update();
}

void Viewport::setHighlights(const std::vector<TintedMesh>& highlights) {
    pendingHighlights_ = highlights;
    highlightsDirty_ = true;
    update();
}

void Viewport::setOverlayOutline(const std::vector<float>& lines) {
    pendingOverlayOutline_ = lines;
    overlayOutlineDirty_ = true;
    update();
}

void Viewport::clearPlacementGraphics() {
    setGhost({}, {});
    setHandles({});
}

void Viewport::cursorRay(QPointF position, td::V3* origin, td::V3* direction) const {
    const QMatrix4x4 inverse = (projectionMatrix() * viewMatrix()).inverted();
    const float ndcX = float(position.x() / std::max(width(), 1)) * 2.0f - 1.0f;
    const float ndcY = 1.0f - float(position.y() / std::max(height(), 1)) * 2.0f;
    const QVector3D through = inverse.map(QVector3D(ndcX, ndcY, 0.5f));
    const QVector3D from = eye();
    const QVector3D toward = (through - from).normalized();
    *origin = {from.x(), from.y(), from.z()};
    *direction = {toward.x(), toward.y(), toward.z()};
}

QPointF Viewport::project(td::V3 point) const {
    const QVector3D ndc = (projectionMatrix() * viewMatrix()).map(QVector3D(point.x, point.y, point.z));
    return QPointF((ndc.x() * 0.5 + 0.5) * width(), (1.0 - (ndc.y() * 0.5 + 0.5)) * height());
}

double Viewport::worldPerPixel(td::V3 point) const {
    const QVector3D offset = QVector3D(point.x, point.y, point.z) - eye();
    return 2.0 * offset.length() * std::tan(qDegreesToRadians(kFovDegrees * 0.5f)) / std::max(height(), 1);
}

void Viewport::setTool(ViewportTool* tool) {
    tool_ = tool;
    if (tool) return;
    QWidget* focused = window()->focusWidget();
    if (!focused || focused == this || isAncestorOf(focused)) setFocus(Qt::OtherFocusReason);
}

void Viewport::setSelectMode(bool enabled) {
    selectMode_ = enabled;
    selecting_ = false;
    rubberBand_->hide();
    if (enabled) setCursor(Qt::CrossCursor);
    else unsetCursor();
}

QMatrix4x4 Viewport::projectionMatrix() const {
    QMatrix4x4 projection;
    const float aspect = float(width()) / std::max(height(), 1);
    projection.perspective(kFovDegrees, aspect, std::max(distance_ * 0.01f, 0.1f), distance_ * 10.0f + bedWidth_ + bedDepth_);
    return projection;
}

QMatrix4x4 Viewport::viewMatrix() const {
    QMatrix4x4 view;
    view.lookAt(eye(), target_, QVector3D(0, 0, 1));
    return view;
}

std::array<float, 16> Viewport::viewProjection() const {
    const QMatrix4x4 matrix = projectionMatrix() * viewMatrix();
    std::array<float, 16> result;
    std::copy_n(matrix.constData(), 16, result.begin());
    return result;
}

Viewport::~Viewport() {
    if (!glReady_ || !context()) return;
    makeCurrent();
    for (GpuBody& body : gpuBodies_) {
        release(body.buffer);
        release(body.outline);
        if (body.normals) glDeleteBuffers(1, &body.normals);
    }
    for (Buffer* buffer : {&overlay_, &minorGrid_, &majorGrid_, &axisX_, &axisY_, &ghost_, &ghostOutline_, &overlayOutline_}) release(*buffer);
    for (Buffer& buffer : handleBuffers_) release(buffer);
    for (Buffer& buffer : highlightBuffers_) release(buffer);
    doneCurrent();
}

void Viewport::setBodies(const std::vector<td::Mesh>& meshes, const std::vector<QVector4D>& colors) {
    bodyViews_.clear();
    for (size_t i = 0; i < meshes.size(); ++i) bodyViews_.push_back({td::buildDisplayMesh(meshes[i]), td::sharpEdges(meshes[i]), colors[i], QMatrix4x4(), true});
    update();
}

void Viewport::setBodyMesh(int body, const td::Mesh& mesh) {
    if (body < 0 || body >= int(bodyViews_.size())) return;
    bodyViews_[body].display = td::buildDisplayMesh(mesh);
    bodyViews_[body].outline = td::sharpEdges(mesh);
    bodyViews_[body].dirty = true;
    update();
}

void Viewport::setBodyPreview(int body, const QMatrix4x4& change) {
    if (body < 0 || body >= int(bodyViews_.size())) return;
    bodyViews_[body].preview = change;
    update();
}

void Viewport::clearBodyPreviews() {
    for (BodyView& view : bodyViews_) view.preview = QMatrix4x4();
    update();
}

void Viewport::setOverlay(const td::Mesh& mesh) {
    pendingOverlay_ = mesh;
    overlayDirty_ = true;
    update();
}

void Viewport::setBed(int width, int depth) {
    bedWidth_ = width;
    bedDepth_ = depth;
    gridDirty_ = true;
    update();
}

void Viewport::fit(const std::array<double, 3>& min, const std::array<double, 3>& max) {
    const QVector3D low(min[0], min[1], min[2]);
    const QVector3D high(max[0], max[1], max[2]);
    target_ = (low + high) * 0.5f;
    const float radius = std::max((high - low).length() * 0.5f, 5.0f);
    distance_ = radius / std::sin(qDegreesToRadians(kFovDegrees * 0.5f)) * 1.15f;
    update();
    emit cameraChanged();
}

void Viewport::fitBed() { fit({-bedWidth_ / 2.0, -bedDepth_ / 2.0, 0}, {bedWidth_ / 2.0, bedDepth_ / 2.0, 0}); }

QVector3D Viewport::eye() const {
    const float yaw = qDegreesToRadians(yaw_);
    const float pitch = qDegreesToRadians(pitch_);
    return target_ + distance_ * QVector3D(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
}

void Viewport::initializeGL() {
    initializeOpenGLFunctions();
    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    if (!program_.link()) {
        qWarning("Shader link failed: %s", qPrintable(program_.log()));
        return;  // without a working GL context the GL calls below would crash
    }
    glReady_ = true;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Viewport::upload(Buffer& buffer, const std::vector<float>& positions, const std::vector<uint32_t>* indices) {
    if (!buffer.vao) {
        glGenVertexArrays(1, &buffer.vao);
        glGenBuffers(1, &buffer.vbo);
        glGenBuffers(1, &buffer.ebo);
    }
    glBindVertexArray(buffer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(positions.size() * sizeof(float)), positions.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    if (indices) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices->size() * sizeof(uint32_t)), indices->data(), GL_STATIC_DRAW);
        buffer.count = static_cast<int>(indices->size());
    } else {
        buffer.count = static_cast<int>(positions.size() / 3);
    }
    glBindVertexArray(0);
}

void Viewport::release(Buffer& buffer) {
    if (!buffer.vao) return;
    glDeleteVertexArrays(1, &buffer.vao);
    glDeleteBuffers(1, &buffer.vbo);
    glDeleteBuffers(1, &buffer.ebo);
    buffer = Buffer{};
}

void Viewport::rebuildGrid() {
    std::vector<float> minor, major;
    const float halfW = bedWidth_ / 2.0f, halfD = bedDepth_ / 2.0f;
    const float z = -0.02f;
    const auto segment = [z](std::vector<float>& out, float x0, float y0, float x1, float y1) {
        out.insert(out.end(), {x0, y0, z, x1, y1, z});
    };
    // Lines are measured out from the origin so that it always lies on one.
    for (int x = 0; x <= halfW; x += 10)
        for (int sign : {1, -1}) {
            if (x == 0 && sign < 0) continue;
            segment(x % 50 == 0 ? major : minor, sign * float(x), -halfD, sign * float(x), halfD);
        }
    for (int y = 0; y <= halfD; y += 10)
        for (int sign : {1, -1}) {
            if (y == 0 && sign < 0) continue;
            segment(y % 50 == 0 ? major : minor, -halfW, sign * float(y), halfW, sign * float(y));
        }
    segment(major, -halfW, -halfD, halfW, -halfD);
    segment(major, halfW, -halfD, halfW, halfD);
    segment(major, halfW, halfD, -halfW, halfD);
    segment(major, -halfW, halfD, -halfW, -halfD);
    upload(minorGrid_, minor, nullptr);
    upload(majorGrid_, major, nullptr);
    upload(axisX_, {0, 0, 0, std::min(halfW, 40.0f), 0, 0}, nullptr);
    upload(axisY_, {0, 0, 0, 0, std::min(halfD, 40.0f), 0}, nullptr);
    gridDirty_ = false;
}

void Viewport::paintGL() {
    if (!glReady_) return;
    if (gridDirty_) rebuildGrid();
    while (gpuBodies_.size() > bodyViews_.size()) {
        release(gpuBodies_.back().buffer);
        release(gpuBodies_.back().outline);
        if (gpuBodies_.back().normals) glDeleteBuffers(1, &gpuBodies_.back().normals);
        gpuBodies_.pop_back();
    }
    gpuBodies_.resize(bodyViews_.size());
    for (size_t i = 0; i < bodyViews_.size(); ++i) {
        BodyView& view = bodyViews_[i];
        if (!view.dirty) continue;
        GpuBody& gpu = gpuBodies_[i];
        upload(gpu.buffer, view.display.positions, nullptr);
        if (!gpu.normals) glGenBuffers(1, &gpu.normals);
        glBindVertexArray(gpu.buffer.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.normals);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(view.display.normals.size() * sizeof(float)), view.display.normals.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindVertexArray(0);
        upload(gpu.outline, view.outline, nullptr);
        view.dirty = false;
    }
    if (overlayDirty_) {
        upload(overlay_, pendingOverlay_.positions, &pendingOverlay_.indices);
        overlayDirty_ = false;
    }

    if (ghostDirty_) {
        upload(ghost_, pendingGhost_.positions, &pendingGhost_.indices);
        upload(ghostOutline_, pendingOutline_, nullptr);
        ghostDirty_ = false;
    }
    if (overlayOutlineDirty_) {
        upload(overlayOutline_, pendingOverlayOutline_, nullptr);
        overlayOutlineDirty_ = false;
    }
    if (highlightsDirty_) {
        if (highlightBuffers_.size() < pendingHighlights_.size()) highlightBuffers_.resize(pendingHighlights_.size());
        highlightColors_.clear();
        for (size_t i = 0; i < pendingHighlights_.size(); ++i) {
            upload(highlightBuffers_[i], pendingHighlights_[i].mesh.positions, &pendingHighlights_[i].mesh.indices);
            highlightColors_.push_back(pendingHighlights_[i].color);
        }
        for (size_t i = pendingHighlights_.size(); i < highlightBuffers_.size(); ++i) highlightBuffers_[i].count = 0;
        highlightsDirty_ = false;
    }
    if (handlesDirty_) {
        if (handleBuffers_.size() < pendingHandles_.size()) handleBuffers_.resize(pendingHandles_.size());
        handleColors_.clear();
        for (size_t i = 0; i < pendingHandles_.size(); ++i) {
            upload(handleBuffers_[i], pendingHandles_[i].mesh.positions, &pendingHandles_[i].mesh.indices);
            handleColors_.push_back(pendingHandles_[i].color);
        }
        for (size_t i = pendingHandles_.size(); i < handleBuffers_.size(); ++i) handleBuffers_[i].count = 0;
        handlesDirty_ = false;
    }

    glClearColor(0.13f, 0.15f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const QVector3D eyePosition = eye();

    program_.bind();
    program_.setUniformValue("uMvp", projectionMatrix() * viewMatrix());
    program_.setUniformValue("uModel", QMatrix4x4());
    program_.setUniformValue("uNormalMatrix", QMatrix3x3());
    program_.setUniformValue("uEye", eyePosition);

    const auto draw = [this](const Buffer& buffer, GLenum mode, bool indexed) {
        if (!buffer.count) return;
        glBindVertexArray(buffer.vao);
        if (indexed) glDrawElements(mode, buffer.count, GL_UNSIGNED_INT, nullptr);
        else glDrawArrays(mode, 0, buffer.count);
    };
    const auto color = [this](float r, float g, float b, float a) { program_.setUniformValue("uColor", QVector4D(r, g, b, a)); };

    program_.setUniformValue("uLit", 0);
    if (gridVisible_) {
        color(0.25f, 0.28f, 0.32f, 1);
        draw(minorGrid_, GL_LINES, false);
        color(0.42f, 0.46f, 0.52f, 1);
        draw(majorGrid_, GL_LINES, false);
        color(0.85f, 0.30f, 0.30f, 1);
        draw(axisX_, GL_LINES, false);
        color(0.35f, 0.75f, 0.35f, 1);
        draw(axisY_, GL_LINES, false);
    }

    const bool shaded = style_ != Style::OutlinesOnly;
    const bool outlined = style_ != Style::MaterialsOnly;
    const bool xray = style_ == Style::XRay;
    if (shaded) {
        program_.setUniformValue("uLit", 1);
        program_.setUniformValue("uSmooth", 1);
        // Pushed back a touch, so the outlines drawn on top do not flicker into the faces.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        if (xray) {
            glEnable(GL_BLEND);
            glDepthMask(GL_FALSE);
        }
        for (size_t i = 0; i < bodyViews_.size(); ++i) {
            program_.setUniformValue("uModel", bodyViews_[i].preview);
            program_.setUniformValue("uNormalMatrix", bodyViews_[i].preview.normalMatrix());
            QVector4D c = bodyViews_[i].color;
            if (xray) c.setW(0.28f);
            program_.setUniformValue("uColor", c);
            draw(gpuBodies_[i].buffer, GL_TRIANGLES, false);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_POLYGON_OFFSET_FILL);
        program_.setUniformValue("uSmooth", 0);
    }
    if (outlined) {
        program_.setUniformValue("uLit", 0);
        // Over a solid face the lines are dark; alone they take the part's own colour.
        if (xray || !shaded) glDisable(GL_DEPTH_TEST);
        for (size_t i = 0; i < bodyViews_.size(); ++i) {
            program_.setUniformValue("uModel", bodyViews_[i].preview);
            const QVector4D c = bodyViews_[i].color;
            program_.setUniformValue("uColor", shaded && !xray ? QVector4D(0.08f, 0.09f, 0.11f, 1) : QVector4D(c.x(), c.y(), c.z(), 1));
            draw(gpuBodies_[i].outline, GL_LINES, false);
        }
        glEnable(GL_DEPTH_TEST);
    }
    program_.setUniformValue("uModel", QMatrix4x4());
    program_.setUniformValue("uNormalMatrix", QMatrix3x3());

    if (overlay_.count) {
        // Drawn over everything, so that holes inside the solid stay visible.
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        color(0.25f, 0.78f, 1.0f, 0.22f);
        draw(overlay_, GL_TRIANGLES, true);
        glDisable(GL_BLEND);
        program_.setUniformValue("uLit", 0);
        color(0.25f, 0.88f, 1.0f, 1);
        draw(overlayOutline_, GL_LINES, false);
        program_.setUniformValue("uLit", 1);
        glEnable(GL_DEPTH_TEST);
    }

    if (!highlightColors_.empty()) {
        // Faces reuse the model's own vertices, so they must win ties in depth.
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        for (size_t i = 0; i < highlightColors_.size(); ++i) {
            program_.setUniformValue("uColor", highlightColors_[i]);
            draw(highlightBuffers_[i], GL_TRIANGLES, true);
        }
        glDisable(GL_BLEND);
        glDepthFunc(GL_LESS);
    }
    if (ghost_.count) {
        program_.setUniformValue("uModel", ghostModel_);
        glEnable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        program_.setUniformValue("uColor", ghostColor_);
        draw(ghost_, GL_TRIANGLES, true);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        // The outline and arrows stay visible even when behind the model.
        glDisable(GL_DEPTH_TEST);
        program_.setUniformValue("uLit", 0);
        color(0.80f, 0.93f, 1.0f, 1);
        draw(ghostOutline_, GL_LINES, false);
        glEnable(GL_DEPTH_TEST);
        program_.setUniformValue("uModel", QMatrix4x4());
    }

    bool clearedDepth = false;
    program_.setUniformValue("uLit", 1);
    for (size_t i = 0; i < handleColors_.size(); ++i) {
        if (!handleBuffers_[i].count) continue;
        if (!clearedDepth) {
            glClear(GL_DEPTH_BUFFER_BIT);
            clearedDepth = true;
        }
        program_.setUniformValue("uColor", handleColors_[i]);
        draw(handleBuffers_[i], GL_TRIANGLES, true);
    }

    glBindVertexArray(0);
    program_.release();
}

void Viewport::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->position().toPoint();
    if (event->type() == QEvent::MouseButtonPress) swallowRelease_ = false;  // a fresh press: no double-click release is pending
    // Working on the canvas takes the keys back from any text box (the AI prompt,
    // the snap sizes), so Delete, Esc and the tool keys reach the window again.
    // A tool that wants its own field focused does so after this.
    if (!hasFocus()) setFocus(Qt::MouseFocusReason);
    if (tool_ && tool_->toolMousePress(event)) return;
    if (event->button() == Qt::RightButton) {
        rightPressed_ = true;
        rightDragging_ = false;
        rightPressPosition_ = lastMouse_;
    }
    if (event->button() == Qt::LeftButton) {
        leftPressed_ = true;
        leftDragging_ = false;
        leftPressPosition_ = lastMouse_;
        grabbing_ = false;
        grabCandidate_ = !selectMode_ && grabTest_ && grabTest_(lastMouse_);
    }
    if (selectMode_ && event->button() == Qt::LeftButton) {
        selecting_ = true;
        marqueeOrigin_ = lastMouse_;
        rubberBand_->setGeometry(QRect(marqueeOrigin_, QSize()));
        rubberBand_->show();
    }
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event) {
    if (tool_ || event->button() != Qt::LeftButton) {
        mousePressEvent(event);
        return;
    }
    // The release that follows belongs to the double-click, not to a new click.
    leftPressed_ = leftDragging_ = false;
    swallowRelease_ = true;
    emit doubleClicked(event->position().toPoint());
}

void Viewport::mouseReleaseEvent(QMouseEvent* event) {
    if (tool_ && tool_->toolMouseRelease(event)) return;
    if (event->button() == Qt::RightButton) {
        const bool dragged = rightDragging_;
        rightPressed_ = rightDragging_ = false;
        if (!dragged) emit contextMenuRequested(event->position().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (swallowRelease_) {
        swallowRelease_ = false;
        return;
    }
    const QPoint end = event->position().toPoint();
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool wasSelecting = selecting_;
    selecting_ = false;
    rubberBand_->hide();

    if (grabbing_) {
        grabbing_ = grabCandidate_ = false;
        leftPressed_ = leftDragging_ = false;
        emit grabFinished(end);
        return;
    }
    grabCandidate_ = false;
    const bool wasDragging = leftDragging_;
    leftPressed_ = leftDragging_ = false;
    if (wasDragging) {
        // Dragging right to left selects whatever the box touches.
        if (wasSelecting) emit marqueeFinished(QRect(marqueeOrigin_, end).normalized(), shift, end.x() < marqueeOrigin_.x());
        return;
    }
    emit clicked(end, shift);
}

void Viewport::mouseMoveEvent(QMouseEvent* event) {
    if (tool_ && tool_->toolMouseMove(event)) return;
    const QPoint now = event->position().toPoint();
    if (leftPressed_ && !leftDragging_ && (now - leftPressPosition_).manhattanLength() >= kDragStartPixels) {
        leftDragging_ = true;
        if (grabCandidate_) {
            grabbing_ = true;
            emit grabStarted(leftPressPosition_);
        }
    }
    if (grabbing_) {
        emit grabMoved(now);
        return;
    }
    if (selecting_) {
        rubberBand_->setGeometry(QRect(marqueeOrigin_, now).normalized());
        return;
    }
    if (leftPressed_ && !leftDragging_) return;  // a click, so far: the view must not twitch
    if (rightPressed_ && !rightDragging_) {
        if ((now - rightPressPosition_).manhattanLength() < kDragStartPixels) return;  // may still be a menu click
        rightDragging_ = true;
    }
    const QPoint delta = now - lastMouse_;
    lastMouse_ = now;
    const bool leftNavigates = (event->buttons() & Qt::LeftButton) && !selectMode_;
    if ((leftNavigates && navigation_ == Navigation::Orbit) || (event->buttons() & Qt::RightButton)) {
        yaw_ -= delta.x() * 0.4f;
        pitch_ = std::clamp(pitch_ + delta.y() * 0.4f, -89.0f, 89.0f);
    } else if (leftNavigates && navigation_ == Navigation::Zoom) {
        distance_ = std::clamp(distance_ * std::pow(1.01f, float(delta.y())), 2.0f, 20000.0f);
    } else if ((leftNavigates && navigation_ == Navigation::Pan) || (event->buttons() & Qt::MiddleButton)) {
        const QVector3D forward = (target_ - eye()).normalized();
        const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 0, 1)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, forward);
        const float perPixel = 2.0f * distance_ * std::tan(qDegreesToRadians(kFovDegrees * 0.5f)) / std::max(height(), 1);
        target_ += (up * float(delta.y()) - right * float(delta.x())) * perPixel;
    } else {
        return;
    }
    update();
    emit cameraChanged();
}

void Viewport::wheelEvent(QWheelEvent* event) {
    distance_ = std::clamp(distance_ * std::pow(0.999f, float(event->angleDelta().y())), 2.0f, 20000.0f);
    update();
    emit cameraChanged();
}
