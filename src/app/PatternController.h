#pragma once

#include "app/Viewport.h"
#include "core/Pattern.h"

#include <QObject>
#include <QPointF>

#include <vector>

class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;

enum class PatternKind { Rectangular, Circular, Path, Mirror };

// The numbers for a pattern, in a box under the selection bar, and for the
// rectangular pattern two arrows that drag the spacing. The window draws the
// see-through copies whenever anything here changes.
class PatternController : public QObject, public ViewportTool {
    Q_OBJECT
public:
    explicit PatternController(Viewport* viewport, QObject* parent = nullptr);

    void begin(PatternKind kind);
    void end();
    bool active() const { return active_; }
    PatternKind kind() const { return kind_; }

    td::RectangularPattern rectangular;
    td::CircularPattern circular;
    td::PathPattern path;

    // Where the rectangular pattern's arrows start: the middle of the solids.
    void setOrigin(td::V3 origin);
    int handleCount() const { return int(handles_.size()); }
    QPointF handleGrabPoint(int index) const { return (handles_[index].start + handles_[index].end) * 0.5; }

    bool toolMousePress(QMouseEvent* event) override;
    bool toolMouseMove(QMouseEvent* event) override;
    bool toolMouseRelease(QMouseEvent* event) override;

signals:
    void changed();
    void confirmRequested();

private:
    struct Handle {
        bool across;
        td::V3 dir;
        QPointF start, end;
    };

    QLineEdit* addNumber(const QString& name, bool whole);
    QComboBox* addChoice(const QString& name, const QStringList& options);
    void build();
    void readFields();
    void syncFields();
    void refreshHandles();
    void place();
    double reach(bool across) const;

    Viewport* viewport_;
    QFrame* hud_;
    QGridLayout* grid_;
    std::vector<QWidget*> widgets_;
    QLineEdit* count1_ = nullptr;
    QLineEdit* distance1_ = nullptr;
    QLineEdit* count2_ = nullptr;
    QLineEdit* distance2_ = nullptr;
    QComboBox* mode_ = nullptr;
    QCheckBox* symmetric_ = nullptr;
    QLineEdit* angle_ = nullptr;

    bool active_ = false;
    bool syncing_ = false;
    PatternKind kind_ = PatternKind::Rectangular;
    td::V3 origin_;
    std::vector<Handle> handles_;
    int dragging_ = -1;
    double dragOffset_ = 0;
};
