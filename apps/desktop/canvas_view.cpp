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
    commit_transform();
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
    if (tool != tool_)
        commit_transform();
    tool_ = tool;
    setCursor(tool == CanvasTool::hand   ? Qt::OpenHandCursor
              : tool == CanvasTool::move ? Qt::ArrowCursor
                                         : Qt::CrossCursor);
    update();
    if (tool_changed)
        tool_changed();
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
const editing::State& CanvasView::visible_state() const {
    return transform_draft_ ? *transform_draft_ : project_->state();
}
QRectF CanvasView::target_rect() const {
    const auto* layer = editing::find_layer(visible_state().layers, active_layer);
    if (!layer)
        return {};
    if (mask_target && !layer->mask.isNull())
        return QRectF(layer->mask.rect());
    if (!layer->pixels.isNull())
        return QRectF(layer->pixels.rect());
    return QRectF(QPointF(), QSizeF(project_->state().size));
}
QTransform CanvasView::parent_transform() const {
    QTransform result;
    std::function<bool(const std::vector<editing::Layer>&, QTransform)> visit;
    visit = [&](const auto& layers, QTransform parent) {
        for (const auto& layer : layers) {
            if (layer.id == active_layer) {
                result = parent;
                return true;
            }
            if (visit(layer.children, layer.transform * parent))
                return true;
        }
        return false;
    };
    visit(visible_state().layers, {});
    return result;
}
QTransform CanvasView::current_layer_transform() const {
    const auto* layer = editing::find_layer(visible_state().layers, active_layer);
    return layer ? (mask_target ? layer->mask_transform : layer->transform) * parent_transform()
                 : QTransform{};
}
std::array<QPointF, 8> CanvasView::transform_handles() const {
    const auto rect = target_rect();
    const auto c = rect.center();
    std::array<QPointF, 8> points{rect.topLeft(),     QPointF(c.x(), rect.top()),
                                  rect.topRight(),    QPointF(rect.right(), c.y()),
                                  rect.bottomRight(), QPointF(c.x(), rect.bottom()),
                                  rect.bottomLeft(),  QPointF(rect.left(), c.y())};
    const auto matrix = current_layer_transform() * view_transform();
    for (auto& p : points)
        p = matrix.map(p);
    return points;
}
bool CanvasView::begin_transform() {
    if (transform_draft_)
        return true;
    const auto* layer = editing::find_layer(project_->state().layers, active_layer);
    if (!layer || editing::layer_locked(project_->state().layers, active_layer))
        return false;
    tool_ = CanvasTool::move;
    setCursor(Qt::ArrowCursor);
    transform_draft_ = project_->state();
    transform_target_ = active_layer;
    transform_mask_ = mask_target;
    transform_revision_ = project_->revision();
    transform_modified_ = false;
    update();
    if (tool_changed)
        tool_changed();
    return true;
}
void CanvasView::commit_transform() {
    if (!transform_draft_)
        return;
    const auto* layer = editing::find_layer(transform_draft_->layers, transform_target_);
    const auto transform = layer->transform, mask = layer->mask_transform;
    const auto id = transform_target_;
    const auto revision = transform_revision_;
    const bool modified = transform_modified_;
    transform_draft_.reset();
    transform_handle_ = -1;
    move_gesture_ = false;
    dragging_ = false;
    if (modified && revision == project_->revision())
        project_->edit(tr("自由变换"), [&](editing::State& state) {
            auto* target = editing::find_layer(state.layers, id);
            if (target && !editing::layer_locked(state.layers, id)) {
                target->transform = transform;
                target->mask_transform = mask;
            }
        });
    refresh();
    if (tool_changed)
        tool_changed();
}
void CanvasView::cancel_transform() {
    if (!transform_draft_)
        return;
    transform_draft_.reset();
    transform_handle_ = -1;
    move_gesture_ = false;
    dragging_ = false;
    refresh();
    if (tool_changed)
        tool_changed();
}
void CanvasView::prepare_inserted_layer() {
    set_tool(CanvasTool::move);
    mask_target = false;
    comparison.clear();
    const auto rect = current_layer_transform().mapRect(target_rect());
    if (rect.width() * zoom_ > width() - 80 || rect.height() * zoom_ > height() - 80) {
        zoom_ = std::clamp(
            std::min((width() - 80.) / rect.width(), (height() - 80.) / rect.height()), .001, 32.);
        fit_mode_ = false;
    }
    pan_ += QPointF(width() / 2., height() / 2.) - view_transform().map(rect.center());
    update();
    if (tool_changed)
        tool_changed();
    setFocus();
}
int CanvasView::transform_hit(QPointF point) const {
    if (tool_ != CanvasTool::move || (!show_transform_controls && !transform_draft_) ||
        target_rect().isEmpty())
        return -1;
    if (editing::layer_locked(visible_state().layers, active_layer))
        return -1;
    const auto handles = transform_handles();
    for (int i = 0; i < 8; ++i)
        if (QLineF(point, handles[static_cast<std::size_t>(i)]).length() <= 8)
            return i;
    for (int i : {0, 2, 4, 6})
        if (QLineF(point, handles[static_cast<std::size_t>(i)]).length() <= 24)
            return 9;
    if (transform_draft_ &&
        target_rect().contains(
            (current_layer_transform() * view_transform()).inverted().map(point)))
        return 8;
    return -1;
}
void CanvasView::transform_motion(QPointF point, Qt::KeyboardModifiers modifiers) {
    if (!transform_draft_ || transform_handle_ < 0)
        return;
    const auto parent = parent_transform();
    const auto start = (transform_mask_ ? drag_mask_transform_ : drag_transform_) * parent;
    QTransform world_delta;
    if (transform_handle_ == 8) {
        auto delta = image_point(point) - image_point(transform_press_);
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            if (std::abs(delta.x()) > std::abs(delta.y()))
                delta.setY(0);
            else
                delta.setX(0);
        }
        if (snap && move_gesture_ && !modifiers.testFlag(Qt::ShiftModifier)) {
            const auto origin = start.map(QPointF()) + delta;
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
        world_delta.translate(delta.x(), delta.y());
    } else if (transform_handle_ == 9) {
        const auto center = start.map(target_rect().center());
        const auto a = image_point(transform_press_) - center, b = image_point(point) - center;
        double degrees =
            (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180 / 3.141592653589793;
        if (modifiers.testFlag(Qt::ShiftModifier))
            degrees = std::round(degrees / 15) * 15;
        world_delta.translate(center.x(), center.y());
        world_delta.rotate(degrees);
        world_delta.translate(-center.x(), -center.y());
    } else {
        const auto rect = target_rect();
        const auto center = rect.center();
        const std::array<QPointF, 8> corners{rect.topLeft(),     QPointF(center.x(), rect.top()),
                                             rect.topRight(),    QPointF(rect.right(), center.y()),
                                             rect.bottomRight(), QPointF(center.x(), rect.bottom()),
                                             rect.bottomLeft(),  QPointF(rect.left(), center.y())};
        const auto anchor = modifiers.testFlag(Qt::AltModifier)
                                ? center
                                : corners[static_cast<std::size_t>((transform_handle_ + 4) % 8)];
        const auto initial = start.inverted().map(image_point(transform_press_)) - anchor;
        const auto current = start.inverted().map(image_point(point)) - anchor;
        double sx = std::abs(initial.x()) > 1e-8 ? current.x() / initial.x() : 1,
               sy = std::abs(initial.y()) > 1e-8 ? current.y() / initial.y() : 1;
        const bool proportional = keep_aspect != modifiers.testFlag(Qt::ShiftModifier);
        if (proportional) {
            if (transform_handle_ == 1 || transform_handle_ == 5)
                sx = sy;
            else if (transform_handle_ == 3 || transform_handle_ == 7)
                sy = sx;
            else
                sx = sy = std::abs(sx - 1) > std::abs(sy - 1) ? sx : sy;
        }
        sx = std::clamp(sx, .01, 100.);
        sy = std::clamp(sy, .01, 100.);
        QTransform local;
        local.translate(anchor.x(), anchor.y());
        local.scale(sx, sy);
        local.translate(-anchor.x(), -anchor.y());
        world_delta = start.inverted() * local * start;
    }
    auto* layer = editing::find_layer(transform_draft_->layers, transform_target_);
    if (transform_mask_)
        layer->mask_transform = drag_mask_transform_ * parent * world_delta * parent.inverted();
    else {
        layer->transform = drag_transform_ * parent * world_delta * parent.inverted();
        if (layer->mask_linked)
            layer->mask_transform = drag_mask_transform_ * parent * world_delta * parent.inverted();
    }
    const auto* original = editing::find_layer(project_->state().layers, transform_target_);
    transform_modified_ = layer->transform != original->transform ||
                          layer->mask_transform != original->mask_transform;
    preview(*transform_draft_);
    update();
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
        const auto& range = project_->state().selection;
        QPainterPath boundary;
        for (int y = 0; y < range.height(); ++y)
            for (int x = 0; x < range.width(); ++x)
                if (range.constScanLine(y)[x] >= 128) {
                    if (y == 0 || range.constScanLine(y - 1)[x] < 128) {
                        boundary.moveTo(x, y);
                        boundary.lineTo(x + 1, y);
                    }
                    if (x == 0 || range.constScanLine(y)[x - 1] < 128) {
                        boundary.moveTo(x, y);
                        boundary.lineTo(x, y + 1);
                    }
                    if (y == range.height() - 1 || range.constScanLine(y + 1)[x] < 128) {
                        boundary.moveTo(x, y + 1);
                        boundary.lineTo(x + 1, y + 1);
                    }
                    if (x == range.width() - 1 || range.constScanLine(y)[x + 1] < 128) {
                        boundary.moveTo(x + 1, y);
                        boundary.lineTo(x + 1, y + 1);
                    }
                }
        QPen edge(Qt::white, 1);
        edge.setCosmetic(true);
        painter.setPen(edge);
        painter.drawPath(boundary);
        edge.setColor(Qt::black);
        edge.setStyle(Qt::DashLine);
        painter.setPen(edge);
        painter.drawPath(boundary);
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
    if (tool_ == CanvasTool::move && (show_transform_controls || transform_draft_) &&
        !target_rect().isEmpty()) {
        const auto points = transform_handles();
        QPen border(QColor("#58adff"), 1);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(QPolygonF{points[0], points[2], points[4], points[6]});
        if (!editing::layer_locked(visible_state().layers, active_layer)) {
            painter.setBrush(QColor("#f0f4f8"));
            for (const auto& p : points)
                painter.drawRect(QRectF(p.x() - 3.5, p.y() - 3.5, 7, 7));
        }
    }
    if (show_rulers) {
        QFont ruler_font = painter.font();
        ruler_font.setPixelSize(10);
        painter.setFont(ruler_font);
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
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton)
        return;
    if (temporary_hand_ || event->button() == Qt::MiddleButton || tool_ == CanvasTool::hand) {
        pan_gesture_ = true;
        dragging_ = true;
        last_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    gesture_range_ = range_operation;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        gesture_range_ = event->modifiers().testFlag(Qt::AltModifier) ? editing::RangeOp::intersect
                                                                      : editing::RangeOp::add;
    else if (event->modifiers().testFlag(Qt::AltModifier))
        gesture_range_ = editing::RangeOp::subtract;
    if (event->button() == Qt::LeftButton) {
        const int hit = transform_hit(event->position());
        if (hit >= 0 && begin_transform()) {
            transform_handle_ = hit;
            transform_press_ = event->position();
            const auto* layer = editing::find_layer(transform_draft_->layers, transform_target_);
            drag_transform_ = layer->transform;
            drag_mask_transform_ = layer->mask_transform;
            dragging_ = true;
            return;
        }
        if (tool_ == CanvasTool::move &&
            (auto_select != event->modifiers().testFlag(Qt::ControlModifier)) &&
            !transform_draft_) {
            const auto p = image_point(event->position());
            const auto& layers = project_->state().layers;
            for (auto it = layers.rbegin(); it != layers.rend(); ++it)
                if (it->visible && !it->locked) {
                    const QRectF r = it->pixels.isNull()
                                         ? QRectF(QPointF(), QSizeF(project_->state().size))
                                         : QRectF(it->pixels.rect());
                    if (r.contains(it->transform.inverted().map(p))) {
                        active_layer = it->id;
                        mask_target = false;
                        update();
                        if (changed)
                            changed();
                        break;
                    }
                }
        }
    }
    if (tool_ == CanvasTool::move && event->button() == Qt::LeftButton &&
        (!show_rulers || (event->position().x() >= 20 && event->position().y() >= 20)) &&
        begin_transform()) {
        transform_handle_ = 8;
        transform_press_ = event->position();
        const auto* layer = editing::find_layer(transform_draft_->layers, transform_target_);
        drag_transform_ = layer->transform;
        drag_mask_transform_ = layer->mask_transform;
        move_gesture_ = true;
        dragging_ = true;
        return;
    }
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
             [&](editing::State& s) { editing::combine_range(s, range, gesture_range_); });
    }
    update();
}
void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    if (pan_gesture_) {
        pan_ += event->position() - last_;
        last_ = event->position();
        fit_mode_ = false;
        update();
        return;
    }
    if (transform_handle_ >= 0 && dragging_) {
        transform_motion(event->position(), event->modifiers());
        return;
    }
    if (!dragging_ && tool_ == CanvasTool::move) {
        const int hit = transform_hit(event->position());
        setCursor(hit == 9               ? Qt::CrossCursor
                  : hit == 8             ? Qt::SizeAllCursor
                  : hit == 1 || hit == 5 ? Qt::SizeVerCursor
                  : hit == 3 || hit == 7 ? Qt::SizeHorCursor
                  : hit == 0 || hit == 4 ? Qt::SizeFDiagCursor
                  : hit == 2 || hit == 6 ? Qt::SizeBDiagCursor
                                         : Qt::ArrowCursor);
    }

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
    QImage color_sample(1, 1, QImage::Format_RGBA8888);
    color_sample.fill(foreground);
    const auto working_color = editing::to_working(color_sample);
    const auto* components = reinterpret_cast<const float*>(working_color.constScanLine(0));
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
                    v = static_cast<uchar>(v * (1 - alpha) +
                                           (erase ? 0 : qGray(foreground.rgb())) * alpha);
                } else {
                    if (!layer->pixels.rect().contains(target))
                        continue;
                    auto* pixel = reinterpret_cast<float*>(layer->pixels.scanLine(target.y())) +
                                  4 * target.x();
                    for (int c = 0; c < 4; ++c)
                        pixel[c] = pixel[c] * (1 - alpha) + (erase ? 0.F : components[c] * alpha);
                }
            }
    });
}
void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
    if (pan_gesture_) {
        pan_gesture_ = false;
        dragging_ = false;
        setCursor(temporary_hand_ || tool_ == CanvasTool::hand ? Qt::OpenHandCursor
                                                               : Qt::ArrowCursor);
        return;
    }
    if (transform_handle_ >= 0) {
        transform_motion(event->position(), event->modifiers());
        transform_handle_ = -1;
        dragging_ = false;
        if (move_gesture_) {
            move_gesture_ = false;
            commit_transform();
        }
        return;
    }

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
             [&](editing::State& s) { editing::combine_range(s, range, gesture_range_); });
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
             [&](editing::State& s) { editing::combine_range(s, range, gesture_range_); });
        path = {};
    }
    update();
}
void CanvasView::mouseDoubleClickEvent(QMouseEvent*) {
    if (transforming()) {
        commit_transform();
        return;
    }
    if (tool_ == CanvasTool::pen || tool_ == CanvasTool::polygon)
        finish_path();
}
void CanvasView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers().testFlag(Qt::AltModifier)) {
        const auto fixed = image_point(event->position());
        set_zoom(zoom_ * std::pow(1.2, event->angleDelta().y() / 120.));
        pan_ += event->position() - view_transform().map(fixed);
    } else {
        pan_ += event->modifiers().testFlag(Qt::ControlModifier)
                    ? QPointF(event->angleDelta().y() / 2., 0)
                    : QPointF(0, event->angleDelta().y() / 2.);
        fit_mode_ = false;
    }
    update();
    event->accept();
}
void CanvasView::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_D) {
        foreground = Qt::black;
        background = Qt::white;
        if (tool_changed)
            tool_changed();
        return;
    }
    if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_X) {
        std::swap(foreground, background);
        if (tool_changed)
            tool_changed();
        return;
    }
    if (event->key() == Qt::Key_Space) {
        temporary_hand_ = true;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier) ||
        event->modifiers().testFlag(Qt::AltModifier)) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight) {
        brush_radius =
            std::clamp(brush_radius + (event->key() == Qt::Key_BracketLeft ? -1. : 1.), 1., 500.);
        if (tool_changed)
            tool_changed();
        return;
    }
    if (tool_ == CanvasTool::move && event->key() >= Qt::Key_Left && event->key() <= Qt::Key_Down) {
        const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        const QPointF delta(event->key() == Qt::Key_Left    ? -step
                            : event->key() == Qt::Key_Right ? step
                                                            : 0,
                            event->key() == Qt::Key_Up     ? -step
                            : event->key() == Qt::Key_Down ? step
                                                           : 0);
        if (editing::find_layer(project_->state().layers, active_layer) &&
            !editing::layer_locked(project_->state().layers, active_layer))
            edit(tr("微移图层"), [&](editing::State& state) {
                auto* layer = editing::find_layer(state.layers, active_layer);
                const auto parent = parent_transform();
                QTransform shift;
                shift.translate(delta.x(), delta.y());
                const auto adjustment = parent * shift * parent.inverted();
                if (mask_target)
                    layer->mask_transform *= adjustment;
                else {
                    layer->transform *= adjustment;
                    if (layer->mask_linked)
                        layer->mask_transform *= adjustment;
                }
            });
        return;
    }

    if (transforming() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        commit_transform();
        return;
    }
    if (transforming() && event->key() == Qt::Key_Escape) {
        cancel_transform();
        return;
    }

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
            set_tool(event->modifiers().testFlag(Qt::ShiftModifier) &&
                             tool_ == CanvasTool::rectangle
                         ? CanvasTool::ellipse
                         : CanvasTool::rectangle);
        else if (key == Qt::Key_L)
            set_tool(event->modifiers().testFlag(Qt::ShiftModifier) && tool_ == CanvasTool::lasso
                         ? CanvasTool::polygon
                         : CanvasTool::lasso);
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
void CanvasView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        temporary_hand_ = false;
        setCursor(tool_ == CanvasTool::hand ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    QWidget::keyReleaseEvent(event);
}
} // namespace huelith::desktop
