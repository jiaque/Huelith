#pragma once
#include <QWidget>

class QMenuBar;
class QPushButton;
class QLabel;

namespace huelith::desktop {
class WindowHeader final : public QWidget {
public:
    explicit WindowHeader(QMenuBar* menus, QWidget* host);
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void toggle_maximized();
    QPushButton* maximize_{};
};
} // namespace huelith::desktop
