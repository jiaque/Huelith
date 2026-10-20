#include "huelith/editing/image_storage.h"
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
    if (tool_ == CanvasTool::patch) {
        patch_base_ = project_->state();
        patch_draft_.reset();
        patch_offset_ = {};
        const auto& selected = project_->state().selection;
        patch_selecting_ = selected.isNull() || !selected.rect().contains(seed) ||
                           selected.constScanLine(seed.y())[seed.x()] == 0 ||
                           modifiers.testFlag(Qt::ShiftModifier) ||
                           modifiers.testFlag(Qt::AltModifier);
        return true;
    }
    if (tool_ == CanvasTool::crop) {
        crop_handle_ = -1;
        crop_before_ = crop_bounds_.value_or(QRectF{});
        crop_angle_before_ = crop_angle;
        if (!crop_straighten && crop_bounds_) {
            const auto positions = crop_handles();
            double nearest = 9 / zoom_;
            for (int i = 0; i < 8; ++i) {
                const double distance = QLineF(point, positions[i]).length();
                if (distance < nearest) {
                    nearest = distance;
                    crop_handle_ = i;
                }
            }
            if (crop_handle_ < 0)
                crop_handle_ = crop_bounds_->contains(point) ? 8 : 9;
        }
        if (!crop_bounds_ && !crop_straighten)
            crop_bounds_ = QRectF(point, point);
        return true;
    }
    if (tool_ == CanvasTool::eyedropper) {
        if (require_current_preview() && preview_.rect().contains(seed)) {
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
            const auto sampled = editing::sample_layers(project_->state(), active_layer, photo_brush.sample);
            photo_sample_ = sampled;
            photo_sample_revision_ = project_->revision();
            photo_sample_layer_ = active_layer;
            photo_sample_scope_ = photo_brush.sample;
            clone_preview_size_ = sampled.size();
            clone_preview_ = editing::display_image(sampled.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            dragging_ = false;
            update();
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
            if (!clone_aligned || !clone_offset_) {
                clone_offset_ = *clone_source_ - point;
                photo_brush.source_anchor = point;
            }
            photo_brush.source_offset = *clone_offset_;
        }
        if (photo_sample_.isNull() || photo_sample_revision_ != project_->revision() ||
            photo_sample_layer_ != active_layer || photo_sample_scope_ != photo_brush.sample) {
            photo_sample_ = editing::sample_layers(project_->state(), active_layer, photo_brush.sample);
            photo_sample_revision_ = project_->revision();
            photo_sample_layer_ = active_layer;
            photo_sample_scope_ = photo_brush.sample;
            if (tool_ == CanvasTool::clone && clone_source_) {
                clone_preview_size_ = photo_sample_.size();
                clone_preview_ = editing::display_image(photo_sample_.scaled(1280,1280,Qt::KeepAspectRatio,Qt::SmoothTransformation));
            }
        }
    } catch (const editing::EditError& error) {
        QToolTip::showText(mapToGlobal(view_transform().map(point).toPoint()),
                           QString::fromUtf8(error.what()), this);
        dragging_ = false;
        return true;
    }
    return false;
}
void CanvasView::patch_move(QPointF point) {
    if (!patch_base_)
        return;
    if (patch_selecting_) {
        stroke_.lineTo(point);
        update();
        return;
    }
    patch_offset_ = point - image_point(press_);
    try {
        patch_draft_ = *patch_base_;
        editing::patch_selection(*patch_draft_, active_layer, patch_offset_, patch_destination,
                                 photo_brush.sample, patch_color_adaptation);
        preview(*patch_draft_);
    } catch (const std::exception& error) {
        patch_draft_.reset();
        QToolTip::showText(mapToGlobal(cursor_position_.toPoint()), QString::fromUtf8(error.what()),
                           this);
    }
}
void CanvasView::patch_finish(QPointF point) {
    if (!patch_base_)
        return;
    patch_move(point);
    if (patch_selecting_) {
        stroke_.closeSubpath();
        const auto range = editing::path_range(project_->state().size, stroke_);
        project_->edit(tr("修补选区"), [&](editing::State& state) {
            editing::combine_range(state, range, gesture_range_);
        });
    } else if (patch_draft_ && QLineF(QPointF(), patch_offset_).length() > .1) {
        const auto draft = *patch_draft_;
        project_->edit(tr("修补"), [&](editing::State& state) { state = draft; });
    }
    patch_cancel();
}
void CanvasView::patch_cancel() {
    patch_base_.reset();
    patch_draft_.reset();
    patch_offset_ = {};
    stroke_ = {};
    dragging_ = false;
    refresh();
}
void CanvasView::photo_preview() {
    QImage coverage(stroke_coverage_.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < coverage.height(); ++y) {
        auto* output=coverage.scanLine(y);
        const auto* input=reinterpret_cast<const QRgb*>(stroke_coverage_.constScanLine(y));
        for (int x = 0; x < coverage.width(); ++x) output[x]=uchar(qAlpha(input[x]));
    }
    auto options = photo_brush;
    options.operation = static_cast<editing::Retouch>(int(tool_) - int(CanvasTool::clone));
    if (selection_start_modifiers_.testFlag(Qt::AltModifier)) {
        if (options.operation == editing::Retouch::dodge)
            options.operation = editing::Retouch::burn;
        else if (options.operation == editing::Retouch::burn)
            options.operation = editing::Retouch::dodge;
    }
    options.strength =
        tool_ >= CanvasTool::blur && tool_ <= CanvasTool::sponge ? retouch_strength : brush_opacity;
    options.radius = brush_radius;
    try {
        editing::retouch(*brush_draft_, active_layer, coverage, options, &photo_sample_);
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
    const double angle = crop_angle;
    crop_bounds_.reset();
    crop_angle = 0;
    crop_straighten = false;
    edit(tr("裁剪"), [&](editing::State& state) { editing::crop_canvas(state, bounds, angle, crop_delete_pixels); });
    fit_to_view();
}
void CanvasView::cancel_crop() {
    crop_bounds_.reset();
    crop_angle = 0;
    crop_straighten = false;
    dragging_ = false;
    update();
    if (tool_changed)
        tool_changed();
}
std::array<QPointF, 8> CanvasView::crop_handles() const {
    const auto r = crop_rectangle();
    return {r.topLeft(),     QPointF(r.center().x(), r.top()),
            r.topRight(),    QPointF(r.right(), r.center().y()),
            r.bottomRight(), QPointF(r.center().x(), r.bottom()),
            r.bottomLeft(),  QPointF(r.left(), r.center().y())};
}
void CanvasView::set_crop_ratio(double value) {
    crop_ratio = value;
    if (!crop_bounds_)
        crop_bounds_ = QRectF(QPointF(), project_->state().size);
    if (value > 0) {
        auto r = *crop_bounds_;
        const auto center = r.center();
        if (r.width() / r.height() > value)
            r.setWidth(r.height() * value);
        else
            r.setHeight(r.width() / value);
        r.moveCenter(center);
        crop_bounds_ = r;
    }
    update();
}
void CanvasView::crop_motion(QPointF point, Qt::KeyboardModifiers modifiers) {
    const auto start = image_point(press_);
    if (crop_straighten) {
        stroke_ = QPainterPath(start);
        stroke_.lineTo(point);
        update();
        return;
    }
    if (crop_handle_ == 9) {
        const auto a = start - crop_before_.center(), b = point - crop_before_.center();
        crop_angle = std::remainder(crop_angle_before_ +
                                        (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) *
                                            180 / 3.141592653589793,
                                    360.);
        if (modifiers.testFlag(Qt::ShiftModifier))
            crop_angle = std::round(crop_angle / 15) * 15;
        update();
        return;
    }
    if (crop_handle_ == 8) {
        crop_bounds_ = crop_before_.translated(point - start);
        update();
        return;
    }
    QPointF anchor = start;
    if (crop_handle_ >= 0) {
        const auto previous = crop_bounds_;
        crop_bounds_ = crop_before_;
        anchor = crop_handles()[(crop_handle_ + 4) % 8];
        crop_bounds_ = previous;
    }
    const bool centered = modifiers.testFlag(Qt::AltModifier);
    const bool side = crop_handle_ >= 0 && crop_handle_ % 2;
    if (centered && crop_handle_ >= 0)
        anchor = crop_before_.center();
    auto delta = point - anchor;
    if (side) {
        if (crop_handle_ == 1 || crop_handle_ == 5)
            delta.setX(crop_before_.width() / 2);
        else
            delta.setY(crop_before_.height() / 2);
    }
    const bool constrained = (crop_ratio > 0) != modifiers.testFlag(Qt::ShiftModifier);
    if (constrained) {
        const double ratio = crop_ratio > 0 ? crop_ratio
                             : crop_before_.isEmpty()
                                 ? 1
                                 : crop_before_.width() / crop_before_.height();
        if (side && (crop_handle_ == 1 || crop_handle_ == 5))
            delta.setX(std::abs(delta.y()) * ratio / (centered ? 1 : 2));
        else if (side)
            delta.setY(std::abs(delta.x()) / ratio / (centered ? 1 : 2));
        else if (std::abs(delta.x()) > std::abs(delta.y()) * ratio)
            delta.setY(std::copysign(std::abs(delta.x()) / ratio, delta.y()));
        else
            delta.setX(std::copysign(std::abs(delta.y()) * ratio, delta.x()));
    }
    auto r = QRectF(anchor, anchor + delta).normalized();
    if (centered)
        r = QRectF(anchor - QPointF(std::abs(delta.x()), std::abs(delta.y())),
                   anchor + QPointF(std::abs(delta.x()), std::abs(delta.y())));
    else if (side) {
        if (crop_handle_ == 1 || crop_handle_ == 5)
            r.setLeft(anchor.x() - std::abs(delta.x()));
        else
            r.setTop(anchor.y() - std::abs(delta.y()));
    }
    if (r.width() <= 32000 && r.height() <= 32000 && r.width() * r.height() <= huelith::editing::maximum_image_pixels)
        crop_bounds_ = r;
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
