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
#include <QRadialGradient>
#include <QRegion>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QTimer>
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
        layer.background = true;
        QImage white(state.size, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        layer.pixels = editing::to_working(white);
        layer.name = tr("背景");
        state.layers.push_back(layer);
        active_layer = layer.id;
    }
    project_ = std::make_unique<editing::Project>(state);
    history_source = editing::render(state);
    setMinimumSize(160, 120);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(tr("图片画布"));
    auto* ants = new QTimer(this);
    ants->setObjectName("selectionAntsTimer");
    ants->setInterval(90);
    connect(ants, &QTimer::timeout, this, [this] {
        if (isVisible() && !project_->state().selection.isNull()) {
            ants_phase_ = (ants_phase_ + 1) % 8;
            update();
        }
    });
    ants->start();
    refresh();
}
void CanvasView::set_path(QPainterPath value) {
    path = std::move(value);
    pen_nodes_.clear();
    pen_closed_ = !path.isEmpty() &&
                  path.currentPosition() == QPointF(path.elementAt(0).x, path.elementAt(0).y);
    update();
}
void CanvasView::load_state(editing::State state, bool saved) {
    cancel_transform();
    selected_layers.clear();
    path = {};
    pen_nodes_.clear();
    pen_closed_ = false;
    mask_target = false;
    mask_view = false;
    stroke_base_.reset();
    brush_draft_.reset();
    clone_source_.reset();
    clone_offset_.reset();
    crop_bounds_.reset();
    quick_mask = false;
    auto loaded = std::make_unique<editing::Project>(std::move(state));
    if (saved)
        loaded->mark_saved(loaded->revision());
    project_ = std::move(loaded);
    history_source = editing::render(project_->state());
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
    spec_.width = project_->state().size.width();
    spec_.height = project_->state().size.height();
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
    if (tool != tool_) {
        cancel_crop();
        commit_transform();
    }
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
    if (transform_draft_)
        return transform_rect_;
    const auto ids = editing::root_targets(
        project_->state(),
        selected_layers.size() > 1 ? selected_layers : std::vector<QUuid>{active_layer});
    if (ids.size() > 1) {
        QRectF bounds;
        for (const auto& id : ids)
            bounds = bounds.united(editing::layer_bounds(project_->state(), id));
        return bounds;
    }
    const auto* layer = editing::find_layer(project_->state().layers, active_layer);
    if (!layer)
        return {};
    if (mask_target && !layer->mask.isNull())
        return layer->mask.rect();
    if (!layer->children.empty())
        return editing::layer_bounds(project_->state(), active_layer);
    return layer->pixels.isNull() ? QRectF(QPointF(), QSizeF(project_->state().size))
                                  : QRectF(layer->pixels.rect());
}
QTransform CanvasView::parent_transform() const {
    return editing::parent_matrix(visible_state(), active_layer);
}
QTransform CanvasView::current_layer_transform() const {
    if (transform_draft_)
        return transform_frame_;
    if (selected_layers.size() > 1)
        return {};
    const auto* layer = editing::find_layer(project_->state().layers, active_layer);
    if (!layer || !layer->children.empty())
        return {};
    return (mask_target ? layer->mask_transform : layer->transform) * parent_transform();
}
std::array<QPointF, 8> CanvasView::transform_handles() const {
    const auto r = target_rect();
    const auto c = r.center();
    std::array<QPointF, 8> points{
        r.topLeft(),     QPointF(c.x(), r.top()),    r.topRight(),   QPointF(r.right(), c.y()),
        r.bottomRight(), QPointF(c.x(), r.bottom()), r.bottomLeft(), QPointF(r.left(), c.y())};
    const auto matrix = current_layer_transform() * view_transform();
    for (auto& point : points)
        point = matrix.map(point);
    return points;
}
bool CanvasView::begin_transform(bool selection) {
    if (transform_draft_)
        return true;
    selection_transform_ = selection;
    if (selection) {
        const auto& range = project_->state().selection;
        if (range.isNull())
            return false;
        QRect bounds;
        for (int y = 0; y < range.height(); ++y)
            for (int x = 0; x < range.width(); ++x)
                if (range.constScanLine(y)[x])
                    bounds = bounds.united(QRect(x, y, 1, 1));
        if (bounds.isEmpty())
            return false;
        transform_rect_ = bounds;
        transform_frame_ = {};
        transform_ids_.clear();
    } else {
        transform_ids_ = editing::root_targets(
            project_->state(),
            selected_layers.size() > 1 ? selected_layers : std::vector<QUuid>{active_layer});
        if (transform_ids_.empty())
            return false;
        for (const auto& id : transform_ids_)
            if (editing::layer_position_locked(project_->state().layers, id))
                return false;
        transform_rect_ = target_rect();
        transform_frame_ = current_layer_transform();
    }
    transform_target_ = active_layer;
    transform_mask_ = mask_target;
    transform_revision_ = project_->revision();
    transform_modified_ = false;
    placement_ = false;
    tool_ = CanvasTool::move;
    transform_draft_ = project_->state();
    setCursor(Qt::ArrowCursor);
    update();
    if (tool_changed)
        tool_changed();
    return true;
}
void CanvasView::stage_layer(editing::Layer layer) {
    commit_transform();
    active_layer = layer.id;
    selected_layers = {layer.id};
    mask_target = false;
    transform_rect_ = layer.pixels.isNull() ? QRectF(QPointF(), QSizeF(project_->state().size))
                                            : QRectF(layer.pixels.rect());
    transform_frame_ = layer.transform;
    transform_target_ = layer.id;
    transform_ids_ = {layer.id};
    transform_mask_ = false;
    selection_transform_ = false;
    transform_revision_ = project_->revision();
    transform_modified_ = true;
    placement_ = true;
    transform_draft_ = project_->state();
    transform_draft_->layers.push_back(std::move(layer));
    tool_ = CanvasTool::move;
    prepare_inserted_layer();
    preview(*transform_draft_);
    if (tool_changed)
        tool_changed();
}
void CanvasView::commit_transform() {
    if (!transform_draft_)
        return;
    const auto draft = *transform_draft_;
    const bool modified = transform_modified_ || placement_;
    const bool valid = transform_revision_ == project_->revision();
    transform_draft_.reset();
    transform_handle_ = -1;
    move_gesture_ = false;
    dragging_ = false;
    selection_transform_ = false;
    placement_ = false;
    if (modified && valid)
        project_->edit(tr("自由变换"), [&](editing::State& state) { state = draft; });
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
    selection_transform_ = false;
    placement_ = false;
    refresh();
    if (tool_changed)
        tool_changed();
}
void CanvasView::prepare_inserted_layer() {
    tool_ = CanvasTool::move;
    mask_target = false;
    comparison.clear();
    const auto bounds = (current_layer_transform()).mapRect(target_rect());
    if (bounds.width() * zoom_ > width() - 80 || bounds.height() * zoom_ > height() - 80) {
        zoom_ = std::clamp(
            std::min((width() - 80.) / bounds.width(), (height() - 80.) / bounds.height()), .001,
            32.);
        fit_mode_ = false;
    }
    pan_ += QPointF(width() / 2., height() / 2.) - view_transform().map(bounds.center());
    update();
    if (tool_changed)
        tool_changed();
    setFocus();
}
std::array<QPointF, 9> CanvasView::warp_handles() const {
    std::array<QPointF, 9> points{};
    const auto* layer = editing::find_layer(visible_state().layers, active_layer);
    if (!layer)
        return points;
    const QSize size = layer->pixels.isNull() ? visible_state().size : layer->pixels.size();
    const auto matrix = layer->transform * parent_transform() * view_transform();
    for (std::size_t i = 0; i < 9; ++i)
        points[i] = matrix.map(QPointF(layer->warp_grid[i].x() * size.width(),
                                       layer->warp_grid[i].y() * size.height()));
    return points;
}
int CanvasView::transform_hit(QPointF point) const {
    if (tool_ != CanvasTool::move || (!show_transform_controls && !transform_draft_) ||
        target_rect().isEmpty())
        return -1;
    if (!selection_transform_ &&
        editing::layer_position_locked(visible_state().layers, active_layer))
        return -1;
    if (warp_mode && !selection_transform_ && selected_layers.size() < 2) {
        const auto points = warp_handles();
        for (int i = 0; i < 9; ++i)
            if (QLineF(point, points[std::size_t(i)]).length() <= 8)
                return 20 + i;
        return -1;
    }
    const auto points = transform_handles();
    for (int i = 0; i < 8; ++i)
        if (QLineF(point, points[std::size_t(i)]).length() <= 7)
            return i;
    for (int i : {0, 2, 4, 6})
        if (QLineF(point, points[std::size_t(i)]).length() <= 23)
            return 9;
    if (transform_draft_ &&
        target_rect().contains(
            (current_layer_transform() * view_transform()).inverted().map(point)))
        return 8;
    return -1;
}
void CanvasView::start_transform_drag(int handle, QPointF position) {
    transform_handle_ = handle;
    transform_press_ = position;
    drag_frame_ = transform_frame_;
    drag_state_ = *transform_draft_;
    dragging_ = true;
}
void CanvasView::apply_transform_delta(const QTransform& delta) {
    *transform_draft_ = drag_state_;
    if (selection_transform_) {
        QImage range(drag_state_.size, QImage::Format_RGB32);
        range.fill(Qt::black);
        QPainter painter(&range);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setTransform(delta);
        painter.drawImage(0, 0, drag_state_.selection);
        painter.end();
        transform_draft_->selection = range.convertToFormat(QImage::Format_Grayscale8);
    } else
        editing::transform_targets(*transform_draft_, transform_ids_, delta, transform_mask_);
    transform_frame_ = drag_frame_ * delta;
    transform_modified_ = true;
    preview(*transform_draft_);
    update();
}
void CanvasView::transform_motion(QPointF point, Qt::KeyboardModifiers modifiers) {
    if (!transform_draft_ || transform_handle_ < 0)
        return;
    if (transform_handle_ >= 20) {
        *transform_draft_ = drag_state_;
        auto* layer = editing::find_layer(transform_draft_->layers, active_layer);
        if (!layer)
            return;
        const auto size = layer->pixels.isNull() ? drag_state_.size : layer->pixels.size();
        const auto local =
            (layer->transform * parent_transform()).inverted().map(image_point(point));
        layer->warped = true;
        layer->warp_grid[std::size_t(transform_handle_ - 20)] = {
            std::clamp(local.x() / size.width(), -100., 100.),
            std::clamp(local.y() / size.height(), -100., 100.)};
        transform_modified_ = true;
        preview(*transform_draft_);
        return;
    }
    const auto start = drag_frame_;
    QTransform delta;
    if (transform_handle_ == 8) {
        auto shift = image_point(point) - image_point(transform_press_);
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            if (std::abs(shift.x()) > std::abs(shift.y()))
                shift.setY(0);
            else
                shift.setX(0);
        }
        if (snap && move_gesture_) {
            const auto bounds = start.mapRect(transform_rect_).translated(shift);
            const double tolerance = 8 / zoom_;
            for (double x : project_->state().vertical_guides)
                for (double edge : {bounds.left(), bounds.center().x(), bounds.right()})
                    if (std::abs(edge - x) < tolerance)
                        shift.setX(shift.x() + x - edge);
            for (double y : project_->state().horizontal_guides)
                for (double edge : {bounds.top(), bounds.center().y(), bounds.bottom()})
                    if (std::abs(edge - y) < tolerance)
                        shift.setY(shift.y() + y - edge);
            if (show_grid) {
                double best_x = tolerance, best_y = tolerance;
                for (double edge : {bounds.left(), bounds.center().x(), bounds.right()}) {
                    const double distance = std::round(edge / grid_spacing) * grid_spacing - edge;
                    if (std::abs(distance) < std::abs(best_x))
                        best_x = distance;
                }
                for (double edge : {bounds.top(), bounds.center().y(), bounds.bottom()}) {
                    const double distance = std::round(edge / grid_spacing) * grid_spacing - edge;
                    if (std::abs(distance) < std::abs(best_y))
                        best_y = distance;
                }
                if (std::abs(best_x) < tolerance)
                    shift.rx() += best_x;
                if (std::abs(best_y) < tolerance)
                    shift.ry() += best_y;
            }
        }
        delta.translate(shift.x(), shift.y());
    } else if (transform_handle_ == 9) {
        const auto center = start.map(transform_rect_.center());
        const auto a = image_point(transform_press_) - center, b = image_point(point) - center;
        double angle =
            (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180 / 3.141592653589793;
        if (modifiers.testFlag(Qt::ShiftModifier))
            angle = std::round(angle / 15) * 15;
        delta.translate(center.x(), center.y());
        delta.rotate(angle);
        delta.translate(-center.x(), -center.y());
    } else if (modifiers.testFlag(Qt::ControlModifier)) {
        const auto r = transform_rect_;
        QPolygonF source =
            start.map(QPolygonF{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()});
        auto destination = source;
        auto shift = image_point(point) - image_point(transform_press_);
        if (transform_handle_ % 2 == 0) {
            const int corner = transform_handle_ / 2;
            destination[corner] += shift;
            if (modifiers.testFlag(Qt::AltModifier) && modifiers.testFlag(Qt::ShiftModifier)) {
                const int adjacent = corner ^ 1;
                destination[adjacent] -= shift;
            } else if (modifiers.testFlag(Qt::AltModifier))
                destination[(corner + 2) % 4] -= shift;
        } else {
            const int first = (transform_handle_ - 1) / 2;
            destination[first] += shift;
            destination[(first + 1) % 4] += shift;
        }
        if (!QTransform::quadToQuad(source, destination, delta) || !delta.isInvertible())
            return;
    } else {
        const auto r = transform_rect_;
        const auto c = r.center();
        const std::array<QPointF, 8> corners{
            r.topLeft(),     QPointF(c.x(), r.top()),    r.topRight(),   QPointF(r.right(), c.y()),
            r.bottomRight(), QPointF(c.x(), r.bottom()), r.bottomLeft(), QPointF(r.left(), c.y())};
        const auto anchor = modifiers.testFlag(Qt::AltModifier)
                                ? c
                                : corners[std::size_t((transform_handle_ + 4) % 8)];
        const auto a = start.inverted().map(image_point(transform_press_)) - anchor,
                   b = start.inverted().map(image_point(point)) - anchor;
        double sx = std::abs(a.x()) > 1e-8 ? b.x() / a.x() : 1,
               sy = std::abs(a.y()) > 1e-8 ? b.y() / a.y() : 1;
        if (keep_aspect != modifiers.testFlag(Qt::ShiftModifier)) {
            if (transform_handle_ == 1 || transform_handle_ == 5)
                sx = sy;
            else if (transform_handle_ == 3 || transform_handle_ == 7)
                sy = sx;
            else {
                const double factor = std::abs(sx - 1) > std::abs(sy - 1) ? sx : sy;
                sx = sy = factor;
            }
        }
        const auto safe = [](double v) {
            return std::copysign(std::clamp(std::abs(v), .001, 100.), v);
        };
        sx = safe(sx);
        sy = safe(sy);
        QTransform local;
        local.translate(anchor.x(), anchor.y());
        local.scale(sx, sy);
        local.translate(-anchor.x(), -anchor.y());
        delta = start.inverted() * local * start;
    }
    apply_transform_delta(delta);
}
void CanvasView::update_selection_outline() {
    const auto& range = visible_state().selection;
    if (range.cacheKey() == selection_cache_key_)
        return;
    selection_cache_key_ = range.cacheKey();
    QRegion region;
    for (int y = 0; y < range.height(); ++y) {
        int start = -1;
        for (int x = 0; x <= range.width(); ++x) {
            const bool selected = x < range.width() && range.constScanLine(y)[x] >= 128;
            if (selected && start < 0)
                start = x;
            if (!selected && start >= 0) {
                region += QRect(start, y, x - start, 1);
                start = -1;
            }
        }
    }
    selection_outline_ = {};
    selection_outline_.addRegion(region);
    selection_outline_ = selection_outline_.simplified();
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
    if (quick_mask) {
        const auto& range = display_state().selection;
        if (!range.isNull()) {
            QImage overlay(range.size(), QImage::Format_ARGB32_Premultiplied);
            overlay.fill(Qt::transparent);
            for (int y = 0; y < range.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
                for (int x = 0; x < range.width(); ++x) {
                    const int alpha = (255 - range.constScanLine(y)[x]) / 2;
                    row[x] = qRgba(alpha, 0, 0, alpha);
                }
            }
            painter.drawImage(0, 0, overlay);
        }
    }
    update_selection_outline();
    QPen edge(Qt::white, 1);
    edge.setCosmetic(true);
    painter.setPen(edge);
    if (!quick_mask)
        painter.drawPath(selection_outline_);
    edge.setColor(Qt::black);
    edge.setStyle(Qt::DashLine);
    edge.setDashOffset(ants_phase_);
    painter.setPen(edge);
    if (!quick_mask)
        painter.drawPath(selection_outline_);
    QPen pen(QColor("#43c9f7"));
    pen.setCosmetic(true);
    painter.setPen(pen);
    if (show_guides)
        for (const auto x : project_->state().vertical_guides)
            painter.drawLine(QPointF(x, 0), QPointF(x, spec_.height));
    if (show_guides)
        for (const auto y : project_->state().horizontal_guides)
            painter.drawLine(QPointF(0, y), QPointF(spec_.width, y));
    if (show_grid && zoom_ > .05) {
        QPen grid(QColor(120, 120, 120, 90));
        grid.setCosmetic(true);
        painter.setPen(grid);
        for (double x = 0; x < spec_.width; x += grid_spacing)
            painter.drawLine(QPointF(x, 0), QPointF(x, spec_.height));
        for (double y = 0; y < spec_.height; y += grid_spacing)
            painter.drawLine(QPointF(0, y), QPointF(spec_.width, y));
    }
    painter.setPen(pen);
    painter.drawPath(path);
    painter.drawPath(stroke_);
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto e = path.elementAt(i);
        painter.drawEllipse(QPointF(e.x, e.y), 3 / zoom_, 3 / zoom_);
    }
    for (const auto& node : pen_nodes_) {
        painter.drawLine(node.anchor, node.incoming);
        painter.drawLine(node.anchor, node.outgoing);
        painter.drawEllipse(node.incoming, 2 / zoom_, 2 / zoom_);
        painter.drawEllipse(node.outgoing, 2 / zoom_, 2 / zoom_);
    }
    if (guide_axis_ && dragging_) {
        const auto p = image_point(last_);
        if (guide_axis_ == 1)
            painter.drawLine(QPointF(0, p.y()), QPointF(spec_.width, p.y()));
        else
            painter.drawLine(QPointF(p.x(), 0), QPointF(p.x(), spec_.height));
    }
    painter.resetTransform();
    if (tool_ == CanvasTool::move && (show_transform_controls || transform_draft_) &&
        !target_rect().isEmpty()) {
        const auto points = transform_handles();
        QPen border(QColor("#58adff"), 1);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(QPolygonF{points[0], points[2], points[4], points[6]});
        if (!editing::layer_position_locked(visible_state().layers, active_layer)) {
            painter.setBrush(QColor("#f0f4f8"));
            for (const auto& p : points)
                painter.drawRect(QRectF(p.x() - 3.5, p.y() - 3.5, 7, 7));
        }
    }
    if (crop_bounds_) {
        painter.save();
        painter.setTransform(view_transform());
        painter.setPen(QPen(QColor("#ffffff"), 1 / zoom_));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(*crop_bounds_);
        painter.restore();
    }
    if (painting_tool()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawEllipse(cursor_position_, brush_radius * zoom_, brush_radius * zoom_);
    }
    if (warp_mode && tool_ == CanvasTool::move && selected_layers.size() < 2) {
        const auto points = warp_handles();
        painter.setPen(QPen(QColor("#58adff"), 1));
        painter.setBrush(Qt::white);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) {
                const auto point = points[std::size_t(y * 3 + x)];
                if (x < 2)
                    painter.drawLine(point, points[std::size_t(y * 3 + x + 1)]);
                if (y < 2)
                    painter.drawLine(point, points[std::size_t((y + 1) * 3 + x)]);
                painter.drawRect(QRectF(point.x() - 3, point.y() - 3, 6, 6));
            }
    }
    if (show_rulers) {
        QFont ruler_font = painter.font();
        ruler_font.setPixelSize(10);
        painter.setFont(ruler_font);
        painter.fillRect(0, 0, width(), 20, QColor("#292e33"));
        painter.fillRect(0, 0, 20, height(), QColor("#292e33"));
        painter.setPen(QColor("#b6bdc4"));
        // Label exact image coordinates with a 1/2/5 interval; never stretch long values into the
        // canvas.
        const auto nice = [](double raw) {
            const double scale = std::pow(10., std::floor(std::log10(std::max(raw, 1e-6))));
            for (double step : {1., 2., 5., 10.})
                if (step * scale >= raw)
                    return step * scale;
            return 10 * scale;
        };
        const auto inv = view_transform().inverted();
        for (bool vertical : {false, true}) {
            const int extent = vertical ? height() : width();
            const QPointF start = vertical ? inv.map(QPointF(20, 20)) : inv.map(QPointF(20, 20));
            const QPointF finish =
                vertical ? inv.map(QPointF(20, extent)) : inv.map(QPointF(extent, 20));
            const double first = vertical ? start.y() : start.x(),
                         last = vertical ? finish.y() : finish.x();
            if (std::abs(last - first) < 1e-8)
                continue;
            const double step = nice(std::abs(last - first) * 75 / std::max(extent - 20, 1));
            for (double value = std::ceil(std::min(first, last) / (step / 5)) * (step / 5);
                 value <= std::max(first, last); value += step / 5) {
                const double position = 20 + (value - first) / (last - first) * (extent - 20);
                const bool major = std::abs(value / step - std::round(value / step)) < 1e-5;
                if (vertical) {
                    painter.drawLine(QPointF(major ? 12 : 16, position), QPointF(20, position));
                    if (major) {
                        painter.save();
                        painter.translate(11, position - 3);
                        painter.rotate(-90);
                        painter.drawText(
                            0, 0, QString::number(std::abs(value) < 1e-8 ? 0 : value, 'g', 5));
                        painter.restore();
                    }
                } else {
                    painter.drawLine(QPointF(position, major ? 12 : 16), QPointF(position, 20));
                    if (major)
                        painter.drawText(
                            QPointF(position + 3, 10),
                            QString::number(std::abs(value) < 1e-8 ? 0 : value, 'g', 5));
                }
            }
        }
        painter.fillRect(0, 0, 20, 20, QColor("#292e33"));
    }
}
void CanvasView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton)
        return;
    if (tool_ == CanvasTool::zoom ||
        (temporary_hand_ && event->modifiers().testFlag(Qt::ControlModifier))) {
        const auto fixed = image_point(event->position());
        set_zoom(zoom_ * (event->modifiers().testFlag(Qt::AltModifier) ? .5 : 2));
        pan_ += event->position() - view_transform().map(fixed);
        update();
        return;
    }
    if (tool_ == CanvasTool::move && !transforming() && !lock_guides) {
        guide_index_ = -1;
        guide_axis_ = show_rulers ? (event->position().y() < 20   ? 1
                                     : event->position().x() < 20 ? 2
                                                                  : 0)
                                  : 0;
        const auto p = image_point(event->position());
        if (!guide_axis_ && show_guides)
            for (std::size_t i = 0; i < project_->state().horizontal_guides.size(); ++i)
                if (std::abs(p.y() - project_->state().horizontal_guides[i]) * zoom_ < 5) {
                    guide_axis_ = 1;
                    guide_index_ = int(i);
                    break;
                }
        if (!guide_axis_ && show_guides)
            for (std::size_t i = 0; i < project_->state().vertical_guides.size(); ++i)
                if (std::abs(p.x() - project_->state().vertical_guides[i]) * zoom_ < 5) {
                    guide_axis_ = 2;
                    guide_index_ = int(i);
                    break;
                }
        if (guide_axis_) {
            press_ = last_ = event->position();
            dragging_ = true;
            return;
        }
    }
    if (temporary_hand_ || event->button() == Qt::MiddleButton || tool_ == CanvasTool::hand) {
        pan_gesture_ = true;
        dragging_ = true;
        last_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    selection_start_modifiers_ = event->modifiers();
    gesture_range_ = range_operation;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        gesture_range_ = event->modifiers().testFlag(Qt::AltModifier) ? editing::RangeOp::intersect
                                                                      : editing::RangeOp::add;
    else if (event->modifiers().testFlag(Qt::AltModifier))
        gesture_range_ = editing::RangeOp::subtract;
    if (event->button() == Qt::LeftButton) {
        const int hit = transform_hit(event->position());
        if (hit >= 0 && begin_transform()) {
            start_transform_drag(hit, event->position());
            return;
        }
        if (tool_ == CanvasTool::move &&
            (auto_select != event->modifiers().testFlag(Qt::ControlModifier)) &&
            !transform_draft_) {
            const auto id = editing::hit_layer(project_->state(), image_point(event->position()),
                                               hit_tolerance / zoom_);
            if (!id.isNull()) {
                active_layer = id;
                mask_target = false;
                if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                    if (std::find(selected_layers.begin(), selected_layers.end(), id) ==
                        selected_layers.end())
                        selected_layers.push_back(id);
                } else if (std::find(selected_layers.begin(), selected_layers.end(), id) ==
                           selected_layers.end())
                    selected_layers = {id};
                if (changed)
                    changed();
                update();
            } else {
                selected_layers.clear();
                active_layer = {};
                if (changed)
                    changed();
                return;
            }
        }
    }
    if (tool_ == CanvasTool::move && event->button() == Qt::LeftButton &&
        (!show_rulers || (event->position().x() >= 20 && event->position().y() >= 20)) &&
        begin_transform()) {
        start_transform_drag(8, event->position());
        move_gesture_ = true;
        return;
    }
    press_ = event->position();
    last_ = press_;
    dragging_ = true;
    const auto p = image_point(press_);
    stroke_ = QPainterPath(p);
    path_before_ = path;
    node_index_ = -1;
    guide_axis_ = show_rulers && !lock_guides ? (press_.y() < 20 ? 1 : press_.x() < 20 ? 2 : 0) : 0;
    if (guide_axis_)
        return;
    if (photo_press(p, event->modifiers()))
        return;
    if (painting_tool()) {
        const auto* layer = editing::find_layer(project_->state().layers, active_layer);
        if (!quick_mask &&
            (!layer || editing::layer_locked(project_->state().layers, active_layer))) {
            dragging_ = false;
            return;
        }
        stroke_base_ = project_->state();
        brush_revision_ = project_->revision();
        stroke_coverage_ = QImage(project_->state().size, QImage::Format_ARGB32_Premultiplied);
        stroke_coverage_.fill(Qt::transparent);
        stamp_brush(p, p);
        update_brush_preview();
        return;
    }
    if (tool_ == CanvasTool::pen || tool_ == CanvasTool::path_select) {
        pen_drag_node_ = -1;
        pen_drag_control_ = 0;
        if (pen_nodes_.empty() && !path.isEmpty()) {
            for (int i = 0; i < path.elementCount(); ++i) {
                const auto element = path.elementAt(i);
                if (element.isMoveTo() || element.isLineTo()) {
                    const QPointF point(element.x, element.y);
                    pen_nodes_.push_back({point, point, point});
                } else if (element.type == QPainterPath::CurveToElement &&
                           i + 2 < path.elementCount()) {
                    const auto second = path.elementAt(i + 1), end = path.elementAt(i + 2);
                    if (!pen_nodes_.empty())
                        pen_nodes_.back().outgoing = {element.x, element.y};
                    pen_nodes_.push_back({{end.x, end.y}, {second.x, second.y}, {end.x, end.y}});
                    i += 2;
                }
            }
        }
        if (tool_ == CanvasTool::path_select || event->modifiers().testFlag(Qt::ControlModifier) ||
            event->modifiers().testFlag(Qt::AltModifier)) {
            for (std::size_t i = 0; i < pen_nodes_.size(); ++i) {
                const auto& node = pen_nodes_[i];
                for (int control : {1, 2, 0}) {
                    const auto point = control == 1   ? node.incoming
                                       : control == 2 ? node.outgoing
                                                      : node.anchor;
                    if (QLineF(p, point).length() < 7 / zoom_) {
                        pen_drag_node_ = int(i);
                        pen_drag_control_ = control;
                        return;
                    }
                }
            }
            dragging_ = false;
            return;
        }
        if (pen_nodes_.size() > 2 && QLineF(p, pen_nodes_.front().anchor).length() < 8 / zoom_) {
            pen_closed_ = true;
            rebuild_pen();
            dragging_ = false;
            return;
        }
        if (pen_closed_) {
            pen_nodes_.clear();
            path = {};
            pen_closed_ = false;
        }
        pen_nodes_.push_back({p, p, p});
        pen_drag_node_ = int(pen_nodes_.size() - 1);
        pen_drag_control_ = 3;
        rebuild_pen();
        return;
    }
    if (tool_ == CanvasTool::polygon) {
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
                    color_tolerance * 3 + 1)
                    range.scanLine(y)[x] = 255;
            }
        if (tool_ == CanvasTool::wand && contiguous) {
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
        if (antialias) {
            const auto binary = range;
            for (int y = 1; y < range.height() - 1; ++y)
                for (int x = 1; x < range.width() - 1; ++x) {
                    int sum = 0;
                    for (int yy = -1; yy <= 1; ++yy)
                        for (int xx = -1; xx <= 1; ++xx)
                            sum += binary.constScanLine(y + yy)[x + xx];
                    range.scanLine(y)[x] = uchar(sum / 9);
                }
        }
        edit(tr("颜色范围"),
             [&](editing::State& s) { editing::combine_range(s, range, gesture_range_); });
    }
    update();
}
void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    cursor_position_ = event->position();
    if (dragging_ && (painting_tool()) && stroke_base_) {
        stamp_brush(image_point(last_), image_point(event->position()));
        last_ = event->position();
        update_brush_preview();
        return;
    }
    if (dragging_ && pen_drag_node_ >= 0) {
        auto& node = pen_nodes_[std::size_t(pen_drag_node_)];
        const auto p = image_point(event->position());
        if (pen_drag_control_ == 0) {
            const auto delta = p - node.anchor;
            node.anchor = p;
            node.incoming += delta;
            node.outgoing += delta;
        } else {
            const bool incoming = pen_drag_control_ == 1;
            if (incoming)
                node.incoming = p;
            else
                node.outgoing = p;
            if (!event->modifiers().testFlag(Qt::AltModifier)) {
                if (incoming)
                    node.outgoing = node.anchor * 2 - p;
                else
                    node.incoming = node.anchor * 2 - p;
            }
        }
        rebuild_pen();
        return;
    }

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

    if (!dragging_) {
        update();
        return;
    }
    const auto p = image_point(event->position());
    if (tool_ == CanvasTool::crop) {
        crop_bounds_ = QRectF(image_point(press_), p).normalized();
        update();
        return;
    }
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
        if (temporary_hand_)
            press_ += event->position() - last_;
        const auto origin = image_point(press_);
        auto offset = p - origin;
        if (event->modifiers().testFlag(Qt::ShiftModifier) &&
            !selection_start_modifiers_.testFlag(Qt::ShiftModifier)) {
            const double side = std::max(std::abs(offset.x()), std::abs(offset.y()));
            offset = {std::copysign(side, offset.x()), std::copysign(side, offset.y())};
        }
        const QRectF box(event->modifiers().testFlag(Qt::AltModifier) &&
                                 !selection_start_modifiers_.testFlag(Qt::AltModifier)
                             ? origin - offset
                             : origin,
                         origin + offset);
        if (tool_ == CanvasTool::rectangle)
            stroke_.addRect(box.normalized());
        else
            stroke_.addEllipse(box.normalized());
    }
    last_ = event->position();
    update();
}
void CanvasView::stamp_brush(QPointF from, QPointF to) {
    QPainter painter(&stroke_coverage_);
    painter.setRenderHint(QPainter::Antialiasing);
    const double radius = std::max(.5, brush_radius * (pressure_enabled ? pressure_ : 1));
    const double distance = QLineF(from, to).length();
    const int count = std::max(1, int(std::ceil(distance / std::max(1., radius * .2))));
    for (int i = 0; i <= count; ++i) {
        const auto center = from + (to - from) * (double(i) / count);
        QRadialGradient gradient(center, radius);
        const QColor solid = QColor::fromRgbF(1, 1, 1, brush_flow);
        gradient.setColorAt(0, solid);
        gradient.setColorAt(std::min(.999, brush_hardness), solid);
        gradient.setColorAt(1, QColor(255, 255, 255, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawEllipse(center, radius, radius);
    }
}
void CanvasView::update_brush_preview() {
    if (!stroke_base_)
        return;
    brush_draft_ = *stroke_base_;
    if (quick_mask && (tool_ == CanvasTool::brush || tool_ == CanvasTool::eraser)) {
        auto& range = brush_draft_->selection;
        if (range.isNull()) {
            range = QImage(brush_draft_->size, QImage::Format_Grayscale8);
            range.fill(255);
        }
        for (int y = 0; y < range.height(); ++y)
            for (int x = 0; x < range.width(); ++x) {
                const double amount = qAlpha(stroke_coverage_.pixel(x, y)) / 255. * brush_opacity;
                auto& value = range.scanLine(y)[x];
                value = uchar(value * (1 - amount) + (tool_ == CanvasTool::eraser
                                                          ? qGray(background.rgb())
                                                          : qGray(foreground.rgb())) *
                                                         amount);
            }
        update();
        return;
    }
    if (retouch_tool()) {
        photo_preview();
        return;
    }
    auto* layer = editing::find_layer(brush_draft_->layers, active_layer);
    if (!layer)
        return;
    if (!mask_target && layer->kind != editing::LayerKind::pixels &&
        layer->kind != editing::LayerKind::repair && layer->kind != editing::LayerKind::source) {
        brush_draft_.reset();
        return;
    }
    const bool erase = tool_ == CanvasTool::eraser;
    if (mask_target && layer->mask.isNull()) {
        layer->mask = QImage(brush_draft_->size, QImage::Format_Grayscale8);
        layer->mask.fill(255);
    } else if (!mask_target && layer->pixels.isNull()) {
        QImage blank(brush_draft_->size, QImage::Format_RGBA8888);
        blank.fill(Qt::transparent);
        layer->pixels = editing::to_working(blank);
    }
    QImage sample(1, 1, QImage::Format_RGBA8888);
    auto paint_color = erase && layer->background && !mask_target ? background : foreground;
    if (layer->background)
        paint_color.setAlpha(255);
    sample.fill(paint_color);
    const auto color = editing::to_working(sample);
    const auto* channels = reinterpret_cast<const float*>(color.constBits());
    const auto matrix = (mask_target ? layer->mask_transform : layer->transform) *
                        editing::parent_matrix(*stroke_base_, active_layer);
    auto& target = mask_target ? layer->mask : layer->pixels;
    for (int y = 0; y < target.height(); ++y)
        for (int x = 0; x < target.width(); ++x) {
            const auto warped = mask_target ? std::optional<QPointF>(QPointF(x + .5, y + .5))
                                            : editing::warp_point(*layer, target.size(),
                                                                  QPointF(x + .5, y + .5), false);
            if (!warped)
                continue;
            const auto point = matrix.map(*warped).toPoint();
            if (!stroke_coverage_.rect().contains(point))
                continue;
            float alpha = float(qAlpha(stroke_coverage_.pixel(point)) / 255. * brush_opacity);
            if (!stroke_base_->selection.isNull())
                alpha *= stroke_base_->selection.constScanLine(point.y())[point.x()] / 255.F;
            if (alpha == 0)
                continue;
            if (mask_target) {
                auto& value = target.scanLine(y)[x];
                value = uchar(value * (1 - alpha) + (erase ? 0 : qGray(foreground.rgb())) * alpha);
            } else {
                auto* value = reinterpret_cast<float*>(target.scanLine(y)) + x * 4;
                const float* source = channels;
                if (tool_ == CanvasTool::history_brush) {
                    if (history_source.isNull() || !history_source.rect().contains(point))
                        continue;
                    source =
                        reinterpret_cast<const float*>(history_source.constScanLine(point.y())) +
                        point.x() * 4;
                }
                for (int channel = 0; channel < 4; ++channel)
                    value[channel] =
                        value[channel] * (1 - alpha * (layer->background ? source[3] : 1.F)) +
                        (erase && !layer->background ? 0 : source[channel] * alpha);
            }
        }
    if (!mask_target && layer->kind == editing::LayerKind::source) {
        layer->kind = editing::LayerKind::pixels;
        layer->linked_path.clear();
        layer->linked_hash.clear();
    }
    preview(*brush_draft_);
}
void CanvasView::apply_stroke() {
    if (brush_draft_ && brush_revision_ == project_->revision()) {
        const auto draft = *brush_draft_;
        QString name = quick_mask ? tr("快速蒙版笔画") : tr("画笔笔画");
        if (retouch_tool()) {
            const QStringList names{tr("仿制图章"), tr("修复画笔"), tr("污点修复"), tr("模糊"),
                                    tr("锐化"),     tr("减淡"),     tr("加深"),     tr("海绵")};
            name = names[int(tool_) - int(CanvasTool::clone)];
        }
        project_->edit(name, [&](editing::State& state) { state = draft; });
    }
    brush_draft_.reset();
    stroke_base_.reset();
    stroke_coverage_ = {};
    refresh();
}
void CanvasView::rebuild_pen() {
    path = {};
    if (pen_nodes_.empty())
        return;
    path.moveTo(pen_nodes_[0].anchor);
    for (std::size_t i = 1; i < pen_nodes_.size(); ++i)
        path.cubicTo(pen_nodes_[i - 1].outgoing, pen_nodes_[i].incoming, pen_nodes_[i].anchor);
    if (pen_closed_) {
        path.cubicTo(pen_nodes_.back().outgoing, pen_nodes_.front().incoming,
                     pen_nodes_.front().anchor);
        path.closeSubpath();
    }
    update();
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
    if (pen_drag_node_ >= 0) {
        pen_drag_node_ = -1;
        stroke_ = {};
        update();
        return;
    }
    if (guide_axis_) {
        const auto p = image_point(event->position());
        const bool remove = event->position().x() < 20 || event->position().y() < 20 ||
                            !rect().contains(event->position().toPoint());
        edit(tr("参考线"), [&](editing::State& state) {
            auto& guides = guide_axis_ == 1 ? state.horizontal_guides : state.vertical_guides;
            const double value = guide_axis_ == 1 ? p.y() : p.x();
            if (guide_index_ >= 0) {
                if (remove)
                    guides.erase(guides.begin() + guide_index_);
                else
                    guides[std::size_t(guide_index_)] = value;
            } else if (!remove)
                guides.push_back(value);
        });
        guide_axis_ = 0;
        guide_index_ = -1;
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
        auto range = editing::path_range(project_->state().size, stroke_);
        if (!antialias)
            for (int y = 0; y < range.height(); ++y)
                for (int x = 0; x < range.width(); ++x)
                    range.scanLine(y)[x] = range.constScanLine(y)[x] >= 128 ? 255 : 0;
        edit(tr("手工选区"),
             [&](editing::State& s) { editing::combine_range(s, range, gesture_range_); });
    } else if (painting_tool())
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
    if (tool_ == CanvasTool::pen || tool_ == CanvasTool::path_select) {
        pen_nodes_.clear();
        pen_closed_ = false;
        return;
    }
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
    if (crop_bounds_ && event->key() == Qt::Key_Escape) {
        cancel_crop();
        return;
    }
    if (crop_bounds_ && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        commit_crop();
        return;
    }
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
        for (const auto& id : selected_layers)
            if (editing::layer_position_locked(project_->state().layers, id))
                return;
        const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        const QPointF delta(event->key() == Qt::Key_Left    ? -step
                            : event->key() == Qt::Key_Right ? step
                                                            : 0,
                            event->key() == Qt::Key_Up     ? -step
                            : event->key() == Qt::Key_Down ? step
                                                           : 0);
        if (editing::find_layer(project_->state().layers, active_layer) &&
            !editing::layer_position_locked(project_->state().layers, active_layer))
            edit(tr("微移图层"), [&](editing::State& state) {
                QTransform shift;
                shift.translate(delta.x(), delta.y());
                editing::transform_targets(
                    state,
                    selected_layers.empty() ? std::vector<QUuid>{active_layer} : selected_layers,
                    shift, mask_target);
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
        pen_nodes_.clear();
        pen_drag_node_ = -1;
        brush_draft_.reset();
        stroke_base_.reset();
        refresh();
        stroke_ = {};
        dragging_ = false;
        update();
    } else if (event->key() == Qt::Key_Return)
        finish_path();
    else {
        const auto key = event->key();
        if (event->modifiers().testFlag(Qt::ShiftModifier) && key == Qt::Key_M)
            set_tool(tool_ == CanvasTool::rectangle ? CanvasTool::ellipse : CanvasTool::rectangle);
        else if (event->modifiers().testFlag(Qt::ShiftModifier) && key == Qt::Key_L)
            set_tool(tool_ == CanvasTool::lasso ? CanvasTool::polygon : CanvasTool::lasso);
        else if (event->modifiers().testFlag(Qt::ShiftModifier) && key == Qt::Key_W)
            set_tool(tool_ == CanvasTool::wand ? CanvasTool::color_range : CanvasTool::wand);
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
void CanvasView::tabletEvent(QTabletEvent* event) {
    pressure_ = std::clamp(double(event->pressure()), .05, 1.);
    const auto type = event->type() == QEvent::TabletPress     ? QEvent::MouseButtonPress
                      : event->type() == QEvent::TabletRelease ? QEvent::MouseButtonRelease
                                                               : QEvent::MouseMove;
    QMouseEvent mouse(type, event->position(), event->globalPosition(),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      event->modifiers());
    if (type == QEvent::MouseButtonPress)
        mousePressEvent(&mouse);
    else if (type == QEvent::MouseButtonRelease)
        mouseReleaseEvent(&mouse);
    else
        mouseMoveEvent(&mouse);
    event->accept();
    if (type == QEvent::MouseButtonRelease)
        pressure_ = 1;
}
} // namespace huelith::desktop
