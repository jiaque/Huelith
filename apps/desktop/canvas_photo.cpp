#include "canvas_view.h"
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
bool CanvasView::retouch_tool() const {
    return tool_ >= CanvasTool::clone && tool_ <= CanvasTool::sponge;
}
bool CanvasView::painting_tool() const {
    return tool_ == CanvasTool::brush || tool_ == CanvasTool::eraser ||
           tool_ == CanvasTool::history_brush || retouch_tool();
}
bool CanvasView::photo_press(QPointF point, Qt::KeyboardModifiers modifiers) {
    const QPoint seed(int(std::floor(point.x())), int(std::floor(point.y())));
    if (tool_ == CanvasTool::crop) {
        crop_bounds_ = QRectF(point, point);
        return true;
    }
    if (tool_ == CanvasTool::eyedropper) {
        if (preview_.rect().contains(seed)) {
            (modifiers.testFlag(Qt::AltModifier) ? background : foreground) =
                preview_.pixelColor(seed);
            if (tool_changed)
                tool_changed();
        }
        dragging_ = false;
        return true;
    }
    if (tool_ != CanvasTool::bucket && !retouch_tool())
        return false;
    try {
        if ((tool_ == CanvasTool::clone || tool_ == CanvasTool::heal) &&
            modifiers.testFlag(Qt::AltModifier)) {
            clone_source_ = point;
            clone_offset_.reset();
            dragging_ = false;
            return true;
        }
        const auto* layer = editing::find_layer(project_->state().layers, active_layer);
        if (mask_target || !layer ||
            editing::layer_locked(project_->state().layers, active_layer) ||
            (layer->kind != editing::LayerKind::pixels &&
             layer->kind != editing::LayerKind::repair))
            throw editing::EditError(tr("请选择未锁定的像素图层，或新建修复层进行操作"));
        if (tool_ == CanvasTool::bucket) {
            edit(tr("油漆桶"), [&](editing::State& state) {
                editing::fill_color(state, active_layer, seed, foreground, color_tolerance,
                                    contiguous, sample_all_layers, brush_opacity);
            });
            dragging_ = false;
            return true;
        }
        if (tool_ == CanvasTool::clone || tool_ == CanvasTool::heal) {
            if (!clone_source_)
                throw editing::EditError(tr("请先按住 Alt 点击图像，设置取样源"));
            if (!clone_aligned || !clone_offset_)
                clone_offset_ = *clone_source_ - point;
            photo_brush.source_offset = *clone_offset_;
        }
    } catch (const editing::EditError& error) {
        QToolTip::showText(mapToGlobal(view_transform().map(point).toPoint()),
                           QString::fromUtf8(error.what()), this);
        dragging_ = false;
        return true;
    }
    return false;
}
void CanvasView::photo_preview() {
    QImage coverage(stroke_coverage_.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < coverage.height(); ++y)
        for (int x = 0; x < coverage.width(); ++x)
            coverage.scanLine(y)[x] = uchar(qAlpha(stroke_coverage_.pixel(x, y)));
    auto options = photo_brush;
    options.operation = static_cast<editing::Retouch>(int(tool_) - int(CanvasTool::clone));
    options.strength = brush_opacity;
    options.radius = brush_radius;
    try {
        editing::retouch(*brush_draft_, active_layer, coverage, options);
        preview(*brush_draft_);
    } catch (const std::exception& error) {
        brush_draft_.reset();
        stroke_base_.reset();
        stroke_coverage_ = {};
        dragging_ = false;
        refresh();
        QToolTip::showText(mapToGlobal(cursor_position_.toPoint()), QString::fromUtf8(error.what()),
                           this);
    }
}
void CanvasView::commit_crop() {
    if (!crop_bounds_ || crop_bounds_->width() < 1 || crop_bounds_->height() < 1)
        return;
    const QRect bounds(qRound(crop_bounds_->x()), qRound(crop_bounds_->y()),
                       qRound(crop_bounds_->width()), qRound(crop_bounds_->height()));
    crop_bounds_.reset();
    edit(tr("裁剪"), [&](editing::State& state) { editing::crop_canvas(state, bounds); });
    fit_to_view();
}
void CanvasView::cancel_crop() {
    crop_bounds_.reset();
    update();
}
void CanvasView::toggle_quick_mask() {
    if (brush_draft_)
        apply_stroke();
    quick_mask = !quick_mask;
    update();
    if (tool_changed)
        tool_changed();
}
} // namespace huelith::desktop
