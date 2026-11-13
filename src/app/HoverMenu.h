#pragma once

#include <QIcon>
#include <QObject>
#include <QString>

class QFrame;
class QTimer;
class QToolButton;
class QWidget;

// A toolbar button whose list of tools drops down while the pointer is over it.
// The list is a child frame rather than a QMenu: menus grab the pointer and
// cannot close themselves when it moves away, which a hover menu has to do.
class HoverMenu : public QObject {
    Q_OBJECT
public:
    HoverMenu(QToolButton* anchor, QWidget* host, const QString& frameName);

    QToolButton* addItem(const QString& id, const QString& label, const QIcon& icon = {}, bool enabled = true, const QString& tip = {});
    QFrame* frame() const { return frame_; }
    // For a button on the right edge: the list opens to its left instead of below it.
    void setOpensLeft(bool left) { opensLeft_ = left; }
    void show();
    void hide();

signals:
    void triggered(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QToolButton* anchor_;
    QWidget* host_;
    QFrame* frame_;
    QTimer* hideTimer_;
    bool opensLeft_ = false;
};
