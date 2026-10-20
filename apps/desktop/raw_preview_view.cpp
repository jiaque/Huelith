#include "raw_preview_view.h"
#include "canvas_preferences.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRubberBand>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
namespace huelith::desktop {
RawPreviewView::RawPreviewView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent) {
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(NoAnchor);
    setResizeAnchor(AnchorViewCenter);
    setDragMode(NoDrag);
    inertia_ = new QTimer(this);
    inertia_->setInterval(16);
    connect(inertia_, &QTimer::timeout, this, [this] {
        pan(velocity_ * 16);
        velocity_ *= .86;
        if (std::hypot(velocity_.x(), velocity_.y()) < .015)
            inertia_->stop();
    });
    animation_ = new QVariantAnimation(this);
    animation_->setDuration(120);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { apply_zoom(value.toDouble(), anchor_, start_); });
    rubber_ = new QRubberBand(QRubberBand::Rectangle, viewport());
    cursor();
}
void RawPreviewView::set_tool(int tool) {
    tool_ = tool;
    dragging_ = false;
    rubber_->hide();
    inertia_->stop();
    cursor();
}
void RawPreviewView::cursor() {
    if(dragging_ && panning_){viewport()->setCursor(Qt::ClosedHandCursor);return;}
    if (space_ || tool_ == 0) {
        viewport()->setCursor(Qt::OpenHandCursor);
        return;
    }
    if (tool_ < 0) {
        viewport()->setCursor(property("portraitBrushCursor").toBool()?Qt::CrossCursor:Qt::ArrowCursor);
        return;
    }
    QPixmap glyph(48, 48);
    glyph.fill(Qt::transparent);
    QPainter painter(&glyph);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto& pen : {QPen(Qt::black, 5), QPen(Qt::white, 2)}) {
        painter.setPen(pen);
        painter.drawEllipse(QRectF(5, 5, 24, 24));
        painter.drawLine(QPointF(26, 26), QPointF(42, 42));
        painter.drawLine(QPointF(11, 17), QPointF(23, 17));
        if ((tool_ == 2) == QApplication::keyboardModifiers().testFlag(Qt::AltModifier))
            painter.drawLine(QPointF(17, 11), QPointF(17, 23));
    }
    painter.end();
    glyph.setDevicePixelRatio(2);
    viewport()->setCursor(QCursor(glyph, 8, 8));
}
void RawPreviewView::apply_zoom(double scale, QPointF anchor, QPointF screen) {
    scale = std::clamp(scale, .001, 32.);
    const auto original = scene()->sceneRect();
    const double margin = read_canvas_preferences().overscroll
                              ? std::max(viewport()->width(), viewport()->height()) / scale
                              : 0;
    setSceneRect(original.adjusted(-margin, -margin, margin, margin));
    const double old=std::hypot(transform().m11(),transform().m12());
    auto next=transform();next.scale(scale/std::max(.000001,old),scale/std::max(.000001,old));
    setTransform(next);
    const QPointF center = QPointF(viewport()->rect().center());
    const auto inverse=transform().inverted();
    centerOn(anchor + inverse.map(center-screen)-inverse.map(QPointF()));
    if (navigated)
        navigated();
}
void RawPreviewView::zoom_to(double scale, QPointF screen, bool animate) {
    inertia_->stop();
    animation_->stop();
    const auto anchor = mapToScene(screen.toPoint());
    scale = std::clamp(scale, .001, 32.);
    if (animate && read_canvas_preferences().animated_zoom) {
        anchor_ = anchor;
        start_ = screen;
        animation_->setStartValue(std::hypot(transform().m11(),transform().m12()));
        animation_->setEndValue(scale);
        animation_->start();
    } else
        apply_zoom(scale, anchor, screen);
}
void RawPreviewView::step_zoom(bool reverse, QPointF screen) {
    constexpr double steps[]{.001,  .01,
                             .02,   .04,
                             .0625, .083333333333,
                             .125,  .166666666667,
                             .25,   .333333333333,
                             .5,    .666666666667,
                             1,     2,
                             3,     4,
                             8,     16,
                             32};
    const double current = std::hypot(transform().m11(),transform().m12());
    double target = reverse ? .001 : 32.;
    if (reverse) {
        for (double step : steps)
            if (step < current - 1e-8)
                target = step;
    } else {
        for (double step : steps)
            if (step > current + 1e-8) {
                target = step;
                break;
            }
    }
    const auto anchor = mapToScene(screen.toPoint());
    zoom_to(target, screen);
    if (read_canvas_preferences().center_clicked) {
        animation_->stop();
        apply_zoom(target, anchor, viewport()->rect().center());
    }
}
void RawPreviewView::pan(QPointF delta) {
    const double scale = std::hypot(transform().m11(),transform().m12());
    const auto center = mapToScene(viewport()->rect().center());
    const auto original = scene()->sceneRect();
    const double margin = read_canvas_preferences().overscroll
                              ? std::max(viewport()->width(), viewport()->height()) / scale
                              : 0;
    setSceneRect(original.adjusted(-margin, -margin, margin, margin));
    const auto inverse=transform().inverted();
    centerOn(center-inverse.map(delta)+inverse.map(QPointF()));
    if (navigated)
        navigated();
}
void RawPreviewView::wheelEvent(QWheelEvent* event) {
    if (read_canvas_preferences().wheel_zoom || event->modifiers().testFlag(Qt::AltModifier)) {
        const double amount = event->angleDelta().y() != 0 ? event->angleDelta().y() / 120.
                                                           : event->pixelDelta().y() / 40.;
        zoom_to(std::hypot(transform().m11(),transform().m12()) * std::pow(1.2, amount), event->position(), false);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
    if (navigated)
        navigated();
}
void RawPreviewView::mousePressEvent(QMouseEvent* event) {
    if ((event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) ||
        (tool_ < 0 && !space_ && event->button() != Qt::MiddleButton)) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    inertia_->stop();
    animation_->stop();
    dragging_ = true;
    panning_ = space_ || tool_ == 0 || event->button() == Qt::MiddleButton;
    start_ = last_ = event->position();
    start_scale_ = std::hypot(transform().m11(),transform().m12());
    anchor_ = mapToScene(start_.toPoint());
    reverse_ = (tool_ == 2) != event->modifiers().testFlag(Qt::AltModifier);
    velocity_ = {};
    clock_.restart();
    if (panning_)
        viewport()->setCursor(Qt::ClosedHandCursor);
    event->accept();
}
void RawPreviewView::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        QGraphicsView::mouseMoveEvent(event);
        cursor();
        return;
    }
    if (panning_) {
        const auto delta = event->position() - last_;
        velocity_ = delta / std::max(1., double(clock_.restart()));
        const double speed = std::hypot(velocity_.x(), velocity_.y());
        if (speed > 4)
            velocity_ *= 4 / speed;
        pan(delta);
    } else if (read_canvas_preferences().scrubby_zoom)
        apply_zoom(start_scale_ * std::exp((event->position().x() - start_.x()) * .01), anchor_,
                   start_);
    else if (!reverse_) {
        rubber_->setGeometry(QRect(start_.toPoint(), event->position().toPoint()).normalized());
        rubber_->show();
    }
    last_ = event->position();
    event->accept();
}
void RawPreviewView::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    rubber_->hide();
    if (panning_) {
        if (read_canvas_preferences().flick_panning && clock_.elapsed() < 80)
            inertia_->start();
    } else if (std::hypot(event->position().x() - start_.x(), event->position().y() - start_.y()) <
                   4 ||
               reverse_) {
        step_zoom(reverse_, event->position());
    } else if (!read_canvas_preferences().scrubby_zoom) {
        const QRectF box = QRectF(start_, event->position()).normalized();
        const auto image = mapToScene(box.center().toPoint());
        apply_zoom(start_scale_ * std::min(viewport()->width() / std::max(1., box.width()),
                                           viewport()->height() / std::max(1., box.height())),
                   image, viewport()->rect().center());
    }
    cursor();
    event->accept();
}
void RawPreviewView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (tool_ == 0) {
        if (fit_requested)
            fit_requested();
    } else if (tool_ > 0)
        zoom_to(1, event->position());
    else
        QGraphicsView::mouseDoubleClickEvent(event);
}
void RawPreviewView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Alt)
        cursor();
    if (event->key() == Qt::Key_Space) {
        space_ = true;
        cursor();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}
void RawPreviewView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Alt)
        cursor();
    if (event->key() == Qt::Key_Space) {
        if(event->isAutoRepeat()){event->accept();return;}
        space_ = false;
        cursor();
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}
void RawPreviewView::cancel_navigation() {
    space_ = dragging_ = panning_ = false;
    velocity_ = {};
    inertia_->stop();
    animation_->stop();
    rubber_->hide();
    cursor();
}
bool RawPreviewView::viewportEvent(QEvent* event) {
    if(event->type()==QEvent::UngrabMouse)cancel_navigation();
    return QGraphicsView::viewportEvent(event);
}
void RawPreviewView::focusOutEvent(QFocusEvent* event) {
    cancel_navigation();
    QGraphicsView::focusOutEvent(event);
}
void RawPreviewView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    auto* fit = menu.addAction(tr("适应窗口"));
    connect(fit, &QAction::triggered, this, [this] {
        if (fit_requested)
            fit_requested();
    });
    for (double scale : {.25, .5, 1., 2., 4.}) {
        auto* action = menu.addAction(QString::number(scale * 100) + "%");
        connect(action, &QAction::triggered, this,
                [this, scale] { zoom_to(scale, viewport()->rect().center()); });
    }
    menu.exec(event->globalPos());
}
} // namespace huelith::desktop
