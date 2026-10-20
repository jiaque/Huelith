#pragma once
#include "huelith/editing/project.h"
#include <QGraphicsView>
#include <functional>
class QGraphicsTextItem;
class QTextCharFormat;
namespace huelith::desktop {
class CanvasView;
class TextEditView final : public QGraphicsView {
  public:
    TextEditView(CanvasView* canvas, editing::Layer layer, bool existing);
    [[nodiscard]] editing::Layer result() const;
    void format(const QTextCharFormat& format);
    void alignment(Qt::Alignment alignment);
    void line_height(double percent);
    void sync_view();
    [[nodiscard]] QTextCharFormat current_format() const;
    std::function<void(bool)> finished;
    std::function<void()> format_changed;

  protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;

  private:
    CanvasView* canvas_;
    editing::Layer layer_;
    QGraphicsTextItem* item_;
};
} // namespace huelith::desktop
