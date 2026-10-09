#include "canvas_view.h"
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace huelith::desktop {
CanvasView::CanvasView(CanvasSpec spec, QWidget* parent)
    : QWidget(parent), spec_(std::move(spec)) {
    if (validate(spec_)) {
        throw std::invalid_argument("Invalid canvas specification");
    }
    setMinimumSize(160, 120);
    setAccessibleName(tr("图片画布"));
}

const CanvasSpec& CanvasView::spec() const noexcept {
    return spec_;
}

double CanvasView::zoom() const noexcept {
    return zoom_;
}

void CanvasView::set_zoom(double zoom) {
    if (!std::isfinite(zoom) || zoom <= 0.0) {
        return;
    }
    fit_mode_ = false;
    zoom_ = std::clamp(zoom, 0.001, 32.0);
    update();
}

void CanvasView::fit_to_view() {
    constexpr double margin{48.0};
    zoom_ = std::clamp(std::min((width() - margin) / spec_.width,
                              (height() - margin) / spec_.height), 0.001, 32.0);
    fit_mode_ = true;
    update();
}

void CanvasView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fit_mode_) {
        fit_to_view();
    }
}

void CanvasView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 30, 30));
    const QSizeF canvas_size(spec_.width * zoom_, spec_.height * zoom_);
    const QRectF canvas((width() - canvas_size.width()) / 2.0,
                        (height() - canvas_size.height()) / 2.0,
                        canvas_size.width(), canvas_size.height());
    // This is only the empty-canvas backdrop, never an image-processing buffer.
    if (spec_.background == CanvasBackground::white) {
        painter.fillRect(canvas, Qt::white);
    } else {
        QPixmap checker(16, 16);
        checker.fill(QColor(98, 98, 98));
        QPainter tile(&checker);
        tile.fillRect(0, 0, 8, 8, QColor(120, 120, 120));
        tile.fillRect(8, 8, 8, 8, QColor(120, 120, 120));
        tile.end();
        painter.fillRect(canvas, QBrush(checker));
    }
    painter.setPen(QColor(12, 12, 12));
    painter.drawRect(canvas);
}
} // namespace huelith::desktop
