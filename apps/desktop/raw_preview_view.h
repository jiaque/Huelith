#pragma once
#include <QElapsedTimer>
#include <QGraphicsView>
#include <functional>
class QRubberBand;
class QTimer;
class QVariantAnimation;
namespace huelith::desktop {
class RawPreviewView final : public QGraphicsView {
  public:
    RawPreviewView(QGraphicsScene* scene, QWidget* parent);
    void set_tool(int tool);
    int active_tool() const { return tool_; }
    bool temporary_pan() const {
        return space_ || (dragging_ && panning_);
    }
    bool navigating() const {
        return space_ || tool_ > 0 || dragging_;
    }
    void zoom_to(double scale, QPointF screen_anchor, bool animate = true);
    void step_zoom(bool reverse, QPointF screen_anchor);
    void cancel_navigation();
    std::function<void()> navigated, fit_requested;

  protected:
    bool viewportEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

  private:
    void apply_zoom(double scale, QPointF anchor, QPointF screen);
    void pan(QPointF delta);
    void cursor();
    int tool_{};
    bool space_{}, dragging_{}, panning_{}, reverse_{};
    QPointF start_, last_, velocity_, anchor_;
    double start_scale_{};
    QElapsedTimer clock_;
    QTimer* inertia_{};
    QVariantAnimation* animation_{};
    QRubberBand* rubber_{};
};
} // namespace huelith::desktop
