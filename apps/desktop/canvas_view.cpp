#include "canvas_view.h"
#include <QFile>
#include <QKeyEvent>
#include <QScreen>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
CanvasView::CanvasView(CanvasSpec spec, QWidget* parent) : QWidget(parent), spec_(std::move(spec)) {
    if (validate(spec_))
        throw std::invalid_argument("Invalid canvas specification");
    editing::State state;
    state.size = {spec_.width, spec_.height};
    state.name = QString::fromStdString(spec_.name);
    state.ppi = spec_.resolution_ppi;
    if (spec_.background == CanvasBackground::white) {
        editing::Layer layer;
        layer.kind = editing::LayerKind::fill;
        layer.name = tr("背景");
        state.layers.push_back(layer);
        active_layer = layer.id;
    }
    project_ = std::make_unique<editing::Project>(state);
    setMinimumSize(160, 120);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(tr("图片画布"));
    refresh();
}
void CanvasView::load_state(editing::State state, bool saved) {
    auto loaded = std::make_unique<editing::Project>(std::move(state));
    if (saved)
        loaded->mark_saved(loaded->revision());
    project_ = std::move(loaded);
    spec_.width = project_->state().size.width();
    spec_.height = project_->state().size.height();
    spec_.name = project_->state().name.toStdString();
    spec_.resolution_ppi = project_->state().ppi;
    active_layer = project_->state().layers.empty() ? QUuid{} : project_->state().layers.back().id;
    refresh();
    fit_to_view();
}
void CanvasView::edit(const QString& name, const std::function<void(editing::State&)>& operation) {
    project_->edit(name, operation);
    refresh();
}
void CanvasView::refresh() {
    linear_preview_ = editing::render(project_->state());
    preview_ = editing::display_image(linear_preview_, monitor_space_);
    update();
    if (changed)
        changed();
}
void CanvasView::preview(const editing::State& state) {
    linear_preview_ = editing::render(state);
    preview_ = editing::display_image(linear_preview_, monitor_space_);
    update();
}
void CanvasView::set_tool(CanvasTool tool) {
    tool_ = tool;
    setCursor(tool == CanvasTool::hand ? Qt::OpenHandCursor : Qt::CrossCursor);
}
void CanvasView::set_zoom(double zoom) {
    if (!std::isfinite(zoom) || zoom <= 0)
        return;
    fit_mode_ = false;
    zoom_ = std::clamp(zoom, .001, 32.);
    update();
}
void CanvasView::fit_to_view() {
    zoom_ = std::clamp(std::min((width() - 64.) / spec_.width, (height() - 64.) / spec_.height),
                       .001, 32.);
    fit_mode_ = true;
    pan_ = {};
    update();
}
QTransform CanvasView::view_transform() const {
    QTransform result;
    result.translate(width() / 2. + pan_.x(), height() / 2. + pan_.y());
    result.rotate(rotation);
    result.scale(zoom_, zoom_);
    result.translate(-spec_.width / 2., -spec_.height / 2.);
    return result;
}
QPointF CanvasView::image_point(QPointF point) const {
    return view_transform().inverted().map(point);
}
void CanvasView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fit_mode_)
        fit_to_view();
}
void CanvasView::paintEvent(QPaintEvent*) {
    const auto screen_name = screen() ? screen()->name() : QString{};
    if (screen_name != monitor_name_) {
        monitor_name_ = screen_name;
        monitor_space_ = QColorSpace::SRgb;
#ifdef Q_OS_WIN
        const auto name = screen_name.toStdWString();
        const HDC dc = CreateDCW(L"DISPLAY", name.c_str(), nullptr, nullptr);
        if (dc) {
            DWORD length{};
            GetICMProfileW(dc, &length, nullptr);
            if (length > 0 && length < 32768) {
                std::vector<wchar_t> profile_path(length);
                if (GetICMProfileW(dc, &length, profile_path.data())) {
                    QFile file(QString::fromWCharArray(profile_path.data()));
                    if (file.open(QIODevice::ReadOnly)) {
                        const auto profile = QColorSpace::fromIccProfile(file.readAll());
                        if (profile.isValid())
                            monitor_space_ = profile;
                    }
                }
            }
            DeleteDC(dc);
        }
#endif
        preview_ = editing::display_image(linear_preview_, monitor_space_);
    }

    QPainter painter(this);
    painter.fillRect(rect(), QColor("#202428"));
    painter.setTransform(view_transform());
    QPixmap checker(16, 16);
    checker.fill(QColor(98, 98, 98));
    {
        QPainter p(&checker);
        p.fillRect(0, 0, 8, 8, QColor(120, 120, 120));
        p.fillRect(8, 8, 8, 8, QColor(120, 120, 120));
    }
    painter.fillRect(QRect(QPoint(), project_->state().size), QBrush(checker));
    QImage image = preview_;
    if (!comparison.isEmpty())
        image =
            editing::display_image(project_->state().snapshots.value(comparison), monitor_space_);
    if (mask_view) {
        if (const auto* layer = editing::find_layer(project_->state().layers, active_layer);
            layer && !layer->mask.isNull())
            image = layer->mask;
    }
    painter.drawImage(0, 0, image);
    if (!project_->state().selection.isNull()) {
        QImage overlay(project_->state().size, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        for (int y = 0; y < overlay.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
            const auto* range = project_->state().selection.constScanLine(y);
            for (int x = 0; x < overlay.width(); ++x)
                row[x] = qRgba(0, range[x] / 4, range[x] / 3, range[x] / 3);
        }
        painter.drawImage(0, 0, overlay);
    }
    QPen pen(QColor("#43c9f7"));
    pen.setCosmetic(true);
    painter.setPen(pen);
    for (const auto x : project_->state().vertical_guides)
        painter.drawLine(QPointF(x, 0), QPointF(x, spec_.height));
    for (const auto y : project_->state().horizontal_guides)
        painter.drawLine(QPointF(0, y), QPointF(spec_.width, y));
    if (show_grid && zoom_ > .05) {
        QPen grid(QColor(120, 120, 120, 90));
        grid.setCosmetic(true);
        painter.setPen(grid);
        for (int x = 0; x < spec_.width; x += 100)
            painter.drawLine(x, 0, x, spec_.height);
        for (int y = 0; y < spec_.height; y += 100)
            painter.drawLine(0, y, spec_.width, y);
    }
    painter.setPen(pen);
    painter.drawPath(path);
    painter.drawPath(stroke_);
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto e = path.elementAt(i);
        painter.drawEllipse(QPointF(e.x, e.y), 3 / zoom_, 3 / zoom_);
    }
    painter.resetTransform();
    if (show_rulers) {
        painter.fillRect(0, 0, width(), 20, QColor("#292e33"));
        painter.fillRect(0, 0, 20, height(), QColor("#292e33"));
        painter.setPen(QColor("#b6bdc4"));
        for (int x = 40; x < width(); x += 100) {
            painter.drawLine(x, 14, x, 20);
            painter.drawText(x + 3, 13, QString::number(qRound(image_point({double(x), 20}).x())));
        }
        for (int y = 40; y < height(); y += 100) {
            painter.drawLine(14, y, 20, y);
            painter.drawText(1, y, QString::number(qRound(image_point({20, double(y)}).y())));
        }
    }
}
void CanvasView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    press_ = event->position();
    last_ = press_;
    dragging_ = true;
    const auto p = image_point(press_);
    stroke_ = QPainterPath(p);
    path_before_ = path;
    node_index_ = -1;
    guide_axis_ = show_rulers ? (press_.y() < 20 ? 1 : press_.x() < 20 ? 2 : 0) : 0;
    if (guide_axis_)
        return;
    if (tool_ == CanvasTool::pen && event->modifiers().testFlag(Qt::ControlModifier)) {
        for (int i = 0; i < path.elementCount(); ++i) {
            const auto e = path.elementAt(i);
            if (QLineF(p, QPointF(e.x, e.y)).length() < 10 / zoom_) {
                node_index_ = i;
                return;
            }
        }
    }
    if (tool_ == CanvasTool::pen || tool_ == CanvasTool::polygon) {
        if (path.isEmpty())
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    if (tool_ == CanvasTool::wand || tool_ == CanvasTool::color_range) {
        const QPoint seed = p.toPoint();
        if (!preview_.rect().contains(seed))
            return;
        const QColor color = preview_.pixelColor(seed);
        QImage range(preview_.size(), QImage::Format_Grayscale8);
        range.fill(0);
        for (int y = 0; y < range.height(); ++y)
            for (int x = 0; x < range.width(); ++x) {
                const auto value = preview_.pixelColor(x, y);
                if (std::abs(value.red() - color.red()) + std::abs(value.green() - color.green()) +
                        std::abs(value.blue() - color.blue()) <
                    90)
                    range.scanLine(y)[x] = 255;
            }
        if (tool_ == CanvasTool::wand) {
            QImage connected(range.size(), QImage::Format_Grayscale8);
            connected.fill(0);
            std::vector<QPoint> queue{seed};
            connected.scanLine(seed.y())[seed.x()] = 255;
            for (std::size_t i = 0; i < queue.size(); ++i) {
                for (const QPoint delta :
                     {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
                    const auto point = queue[i] + delta;
                    if (range.rect().contains(point) && range.constScanLine(point.y())[point.x()] &&
                        !connected.constScanLine(point.y())[point.x()]) {
                        connected.scanLine(point.y())[point.x()] = 255;
                        queue.push_back(point);
                    }
                }
            }
            range = connected;
        }
        edit(tr("颜色范围"),
             [&](editing::State& s) { editing::combine_range(s, range, range_operation); });
    }
    update();
}
void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_)
        return;
    const auto p = image_point(event->position());
    if (node_index_ >= 0) {
        path.setElementPositionAt(node_index_, p.x(), p.y());
    } else if (guide_axis_) {
        last_ = event->position();
        update();
        return;
    } else if (tool_ == CanvasTool::hand || event->buttons().testFlag(Qt::MiddleButton)) {
        pan_ += event->position() - last_;
        fit_mode_ = false;
    } else if (tool_ == CanvasTool::lasso || tool_ == CanvasTool::brush ||
               tool_ == CanvasTool::eraser)
        stroke_.lineTo(p);
    else if (tool_ == CanvasTool::rectangle || tool_ == CanvasTool::ellipse) {
        stroke_ = QPainterPath();
        const QRectF box(image_point(press_), p);
        if (tool_ == CanvasTool::rectangle)
            stroke_.addRect(box.normalized());
        else
            stroke_.addEllipse(box.normalized());
    }
    last_ = event->position();
    update();
}
void CanvasView::apply_stroke() {
    const auto* original = editing::find_layer(project_->state().layers, active_layer);
    if (!original || editing::layer_locked(project_->state().layers, active_layer))
        return;
    if (!mask_target && original->kind != editing::LayerKind::pixels &&
        original->kind != editing::LayerKind::repair)
        return;
    const bool erase = tool_ == CanvasTool::eraser;
    edit(erase ? tr("擦除笔画") : tr("画笔笔画"), [&](editing::State& s) {
        auto* layer = editing::find_layer(s.layers, active_layer);
        QImage coverage(s.size, QImage::Format_ARGB32_Premultiplied);
        coverage.fill(Qt::transparent);
        QPainter painter(&coverage);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(
            QPen(Qt::white, brush_radius * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(stroke_);
        painter.drawPoint(image_point(press_));
        painter.end();
        if (mask_target) {
            if (layer->mask.isNull()) {
                layer->mask = QImage(s.size, QImage::Format_Grayscale8);
                layer->mask.fill(255);
            }
        } else if (layer->pixels.isNull()) {
            QImage image(s.size, QImage::Format_RGBA8888);
            image.fill(Qt::transparent);
            layer->pixels = editing::to_working(image);
        }
        const auto inverse = (mask_target ? layer->mask_transform : layer->transform).inverted();
        for (int y = 0; y < s.size.height(); ++y)
            for (int x = 0; x < s.size.width(); ++x) {
                float alpha = qAlpha(coverage.pixel(x, y)) / 255.F;
                if (!s.selection.isNull())
                    alpha *= s.selection.constScanLine(y)[x] / 255.F;
                if (alpha == 0)
                    continue;
                const QPoint target = inverse.map(QPointF(x, y)).toPoint();
                if (mask_target) {
                    if (!layer->mask.rect().contains(target))
                        continue;
                    auto& v = layer->mask.scanLine(target.y())[target.x()];
                    v = static_cast<uchar>(v * (1 - alpha) + (erase ? 0 : 255) * alpha);
                } else {
                    if (!layer->pixels.rect().contains(target))
                        continue;
                    auto* pixel = reinterpret_cast<float*>(layer->pixels.scanLine(target.y())) +
                                  4 * target.x();
                    for (int c = 0; c < 4; ++c)
                        pixel[c] = pixel[c] * (1 - alpha) + (erase ? 0.F : alpha);
                }
            }
    });
}
void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_)
        return;
    dragging_ = false;
    if (guide_axis_) {
        const auto p = image_point(event->position());
        edit(tr("拖出参考线"), [&](editing::State& s) {
            if (guide_axis_ == 1)
                s.horizontal_guides.push_back(p.y());
            else
                s.vertical_guides.push_back(p.x());
        });
        guide_axis_ = 0;
        stroke_ = {};
        return;
    }
    if (node_index_ >= 0) {
        node_index_ = -1;
        stroke_ = {};
        update();
        return;
    }
    if (tool_ == CanvasTool::rectangle || tool_ == CanvasTool::ellipse ||
        tool_ == CanvasTool::lasso) {
        stroke_.closeSubpath();
        const auto range = editing::path_range(project_->state().size, stroke_);
        edit(tr("手工选区"),
             [&](editing::State& s) { editing::combine_range(s, range, range_operation); });
    } else if (tool_ == CanvasTool::brush || tool_ == CanvasTool::eraser)
        apply_stroke();
    else if (tool_ == CanvasTool::pen && QLineF(press_, event->position()).length() > 3 &&
             path.elementCount() > 1) {
        const auto end = path.currentPosition();
        const auto tangent = image_point(event->position()) - end;
        path.setElementPositionAt(path.elementCount() - 1, end.x(), end.y());
        if (!path_before_.isEmpty()) {
            const auto previous = path_before_.currentPosition();
            path = path_before_;
            path.cubicTo(previous + tangent, end - tangent, end);
        }
    } else if (tool_ == CanvasTool::move) {
        const auto* layer = editing::find_layer(project_->state().layers, active_layer);
        if (layer && !editing::layer_locked(project_->state().layers, active_layer)) {
            QPointF delta = image_point(event->position()) - image_point(press_);
            if (snap) {
                const auto origin = layer->transform.map(QPointF()) + delta;
                const double tolerance = 8 / zoom_;
                for (double x : project_->state().vertical_guides)
                    if (std::abs(origin.x() - x) < tolerance)
                        delta.setX(delta.x() + x - origin.x());
                for (double y : project_->state().horizontal_guides)
                    if (std::abs(origin.y() - y) < tolerance)
                        delta.setY(delta.y() + y - origin.y());
                if (show_grid) {
                    delta.setX(std::round(delta.x() / 10) * 10);
                    delta.setY(std::round(delta.y() / 10) * 10);
                }
            }
            if (!delta.isNull())
                edit(tr("移动目标"), [&](editing::State& s) {
                    auto* l = editing::find_layer(s.layers, active_layer);
                    QTransform shift;
                    shift.translate(delta.x(), delta.y());
                    if (mask_target)
                        l->mask_transform = l->mask_transform * shift;
                    else {
                        l->transform = l->transform * shift;
                        if (l->mask_linked)
                            l->mask_transform = l->mask_transform * shift;
                    }
                });
        }
    }
    stroke_ = QPainterPath();
    update();
}
void CanvasView::finish_path() {
    if (path.isEmpty())
        return;
    path.closeSubpath();
    if (tool_ == CanvasTool::polygon) {
        const auto range = editing::path_range(project_->state().size, path);
        edit(tr("多边形选区"),
             [&](editing::State& s) { editing::combine_range(s, range, range_operation); });
        path = {};
    }
    update();
}
void CanvasView::mouseDoubleClickEvent(QMouseEvent*) {
    if (tool_ == CanvasTool::pen || tool_ == CanvasTool::polygon)
        finish_path();
}
void CanvasView::wheelEvent(QWheelEvent* event) {
    set_zoom(zoom_ * std::pow(1.2, event->angleDelta().y() / 120.));
    event->accept();
}
void CanvasView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        path = {};
        stroke_ = {};
        dragging_ = false;
        update();
    } else if (event->key() == Qt::Key_Return)
        finish_path();
    else {
        const auto key = event->key();
        if (key == Qt::Key_V)
            set_tool(CanvasTool::move);
        else if (key == Qt::Key_H)
            set_tool(CanvasTool::hand);
        else if (key == Qt::Key_M)
            set_tool(CanvasTool::rectangle);
        else if (key == Qt::Key_L)
            set_tool(CanvasTool::lasso);
        else if (key == Qt::Key_P)
            set_tool(CanvasTool::pen);
        else if (key == Qt::Key_B)
            set_tool(CanvasTool::brush);
        else if (key == Qt::Key_E)
            set_tool(CanvasTool::eraser);
        else if (key == Qt::Key_W)
            set_tool(CanvasTool::wand);
        else
            QWidget::keyPressEvent(event);
    }
}
} // namespace huelith::desktop
