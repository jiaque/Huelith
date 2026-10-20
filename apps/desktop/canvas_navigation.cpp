#include "canvas_preferences.h"
#include "canvas_view.h"
#include <QLineF>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
void CanvasView::initialize_navigation() {
    navigation_timer_ = new QTimer(this);
    navigation_timer_->setObjectName("canvasNavigationTimer");
    navigation_timer_->setInterval(16);
    connect(navigation_timer_, &QTimer::timeout, this, &CanvasView::navigation_tick);
}
void CanvasView::constrain_pan() {
    if (read_canvas_preferences().overscroll)
        return;
    QTransform frame;
    frame.rotate(rotation);
    frame.scale(zoom_, zoom_);
    const auto bounds = frame.mapRect(QRectF(0, 0, spec_.width, spec_.height));
    const double limit_x = std::max(0., (bounds.width() - width()) / 2);
    const double limit_y = std::max(0., (bounds.height() - height()) / 2);
    pan_.setX(std::clamp(pan_.x(), -limit_x, limit_x));
    pan_.setY(std::clamp(pan_.y(), -limit_y, limit_y));
}
void CanvasView::navigation_sync(bool zoom, bool pan) {
    const auto preferences = read_canvas_preferences();
    if ((!zoom || !preferences.zoom_all) && (!pan || !preferences.pan_all))
        return;
    const auto center = image_point(QPointF(width() / 2., height() / 2.));
    for (auto* widget : window()->findChildren<QWidget*>()) {
        auto* other = dynamic_cast<CanvasView*>(widget);
        if (!other || other == this)
            continue;
        if (zoom && preferences.zoom_all)
            other->zoom_ = zoom_;
        if (pan && preferences.pan_all) {
            const QPointF target(center.x() / spec_.width * other->spec_.width,
                                 center.y() / spec_.height * other->spec_.height);
            other->pan_ += QPointF(other->width() / 2., other->height() / 2.) -
                           other->view_transform().map(target);
        }
        other->fit_mode_ = false;
        other->constrain_pan();
        other->update();
    }
}
void CanvasView::navigate_zoom(double scale, QPointF image_anchor, QPointF screen_anchor,
                               bool animate) {
    pan_velocity_ = {};
    zoom_start_ = zoom_;
    zoom_target_ = std::clamp(scale, .001, 32.);
    zoom_anchor_ = image_anchor;
    zoom_screen_ = screen_anchor;
    fit_mode_ = false;
    if (animate && read_canvas_preferences().animated_zoom) {
        zoom_animating_ = true;
        navigation_clock_.restart();
        navigation_timer_->start();
    } else {
        zoom_animating_ = false;
        zoom_ = zoom_target_;
        pan_ += screen_anchor - view_transform().map(image_anchor);
        constrain_pan();
        navigation_sync(true, true);
        if (tool_changed)
            tool_changed();
        update();
    }
}
void CanvasView::pan_motion(QPointF position) {
    const auto delta = position - last_;
    const double milliseconds = std::max(1., double(pan_clock_.restart()));
    pan_velocity_ = delta / milliseconds;
    if (QLineF(QPointF(), pan_velocity_).length() > 4)
        pan_velocity_ *= 4 / QLineF(QPointF(), pan_velocity_).length();
    pan_ += delta;
    last_ = position;
    fit_mode_ = false;
    constrain_pan();
    navigation_sync(false, true);
    update();
}
void CanvasView::finish_pan() {
    if (read_canvas_preferences().flick_panning && pan_clock_.isValid() &&
        pan_clock_.elapsed() < 100 && QLineF(QPointF(), pan_velocity_).length() > .05) {
        navigation_clock_.restart();
        navigation_timer_->start();
    } else
        pan_velocity_ = {};
}
void CanvasView::navigation_tick() {
    if (!isVisible()) {
        navigation_timer_->stop();
        zoom_animating_ = false;
        pan_velocity_ = {};
        return;
    }
    const bool zoom_updated = zoom_animating_;
    if (zoom_animating_) {
        const double t = std::clamp(navigation_clock_.elapsed() / 150., 0., 1.);
        const double eased = 1 - std::pow(1 - t, 3);
        zoom_ = std::exp(std::lerp(std::log(zoom_start_), std::log(zoom_target_), eased));
        pan_ += zoom_screen_ - view_transform().map(zoom_anchor_);
        zoom_animating_ = t < 1;
        if (tool_changed)
            tool_changed();
    } else if (!pan_velocity_.isNull()) {
        const double dt = std::min(50., double(navigation_clock_.restart()));
        pan_ += pan_velocity_ * dt;
        pan_velocity_ *= std::exp(-dt / 140);
        if (QLineF(QPointF(), pan_velocity_).length() < .005)
            pan_velocity_ = {};
    }
    constrain_pan();
    navigation_sync(zoom_updated, true);
    update();
    if (!zoom_animating_ && pan_velocity_.isNull())
        navigation_timer_->stop();
}
} // namespace huelith::desktop
