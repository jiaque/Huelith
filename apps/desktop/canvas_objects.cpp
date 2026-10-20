#include "canvas_view.h"
#include "huelith/editing/semantic.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <future>

namespace huelith::desktop {
struct CanvasView::ObjectSelectionState {
    std::future<std::vector<editing::SemanticInstance>> job;
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    std::vector<editing::SemanticInstance> candidates;
    quint64 revision{};
    const editing::Project* document{};
    QUuid layer;
    bool all{}, valid{}, drawing{};
    int hover{-1};
    QPointF origin, cursor;
    QPainterPath gesture;
    QImage overlay;
    editing::RangeOp operation{editing::RangeOp::replace};
    ~ObjectSelectionState() {
        *cancelled = true;
        if (job.valid())
            job.wait();
    }
};
void CanvasView::update_object_options() {
    if (!objects_) {
        objects_ = std::make_shared<ObjectSelectionState>();
        auto* timer = new QTimer(this);
        timer->setInterval(30);
        connect(timer, &QTimer::timeout, this, [this] { object_poll(); });
        timer->start();
    }
    objects_->overlay = {};
    object_poll();
    update();
}
void CanvasView::object_poll() {
    if (!objects_)
        return;
    auto& s = *objects_;
    const bool matches = s.document == project_.get() && s.revision == project_->revision() &&
                         s.layer == active_layer && s.all == object_all_layers;
    if (s.job.valid()) {
        if (!matches || tool_ != CanvasTool::object_selection)
            *s.cancelled = true;
        if (s.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            auto result = s.job.get();
            if (matches && !s.cancelled->load()) {
                s.candidates = std::move(result);
                s.valid = true;
                setProperty("objectReady", true);
                setProperty("objectCount", int(s.candidates.size()));
                setProperty("objectStatus", tr("对象识别完成：%1 个候选").arg(s.candidates.size()));
            }
        } catch (const std::exception& error) {
            if (matches && !s.cancelled->load()) {
                s.valid = true; // Failed inputs wait for an edit or explicit retry.
                setProperty("objectError", true);
                setProperty("objectStatus", QString::fromUtf8(error.what()));
            }
        }
        update();
    }
    if (tool_ != CanvasTool::object_selection || (matches && s.valid))
        return;
    s.candidates.clear();
    s.overlay = {};
    s.hover = -1;
    s.valid = false;
    s.revision = project_->revision();
    s.document = project_.get();
    s.layer = active_layer;
    s.all = object_all_layers;
    *s.cancelled = false;
    setProperty("objectReady", false);
    setProperty("objectError", false);
    setProperty("objectCount", 0);
    setProperty("objectStatus", tr("正在识别对象…"));
    const auto state = project_->state();
    const auto id = active_layer;
    const bool all = object_all_layers;
    QString model = QCoreApplication::applicationDirPath() + "/models/maskrcnn-r50-v2.onnx";
    if (!QFileInfo::exists(model))
        model =
            QDir::current().absoluteFilePath("build/windows-release/models/maskrcnn-r50-v2.onnx");
    s.job = std::async(std::launch::async, [state, id, all, model, cancelled = s.cancelled] {
        auto image = editing::sample_layers(
            state, id, all ? editing::SampleLayers::visible : editing::SampleLayers::current);
        if (cancelled->load())
            throw editing::EditError(QStringLiteral("对象识别已取消"));
        image = image.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return editing::semantic_instances(image, model, *cancelled);
    });
}
void CanvasView::object_cancel() {
    if (!objects_)
        return;
    auto& s = *objects_;
    s.drawing = false;
    s.gesture = {};
    s.hover = -1;
    s.overlay = {};
    if (s.job.valid()) {
        *s.cancelled = true;
        s.valid = false;
    }
    update();
}
void CanvasView::object_press(QPointF point, Qt::KeyboardModifiers modifiers) {
    update_object_options();
    auto& s = *objects_;
    if (!property("objectReady").toBool() ||
        !QRectF(QPointF(), project_->state().size).contains(point))
        return;
    s.origin = s.cursor = point;
    s.gesture = QPainterPath(point);
    s.drawing = true;
    s.operation = range_operation;
    if (modifiers.testFlag(Qt::ShiftModifier))
        s.operation = modifiers.testFlag(Qt::AltModifier) ? editing::RangeOp::intersect
                                                          : editing::RangeOp::add;
    else if (modifiers.testFlag(Qt::AltModifier))
        s.operation = editing::RangeOp::subtract;
}
void CanvasView::object_move(QPointF point) {
    if (!objects_)
        return;
    auto& s = *objects_;
    s.cursor = point;
    if (s.drawing) {
        if (object_mode == 1) {
            if (s.gesture.elementCount() < 16384)
                s.gesture.lineTo(point);
        } else {
            s.gesture = {};
            s.gesture.addRect(QRectF(s.origin, point).normalized());
        }
        update();
        return;
    }
    int hovered = -1;
    double best = 0;
    const auto size = project_->state().size;
    for (std::size_t i = 0; i < s.candidates.size(); ++i) {
        const auto& candidate = s.candidates[i];
        const auto& mask = candidate.mask;
        const QPoint p(int(std::floor(point.x() * mask.width() / size.width())),
                       int(std::floor(point.y() * mask.height() / size.height())));
        if (!mask.rect().contains(p))
            continue;
        const double score = mask.constScanLine(p.y())[p.x()] / 255. * candidate.confidence;
        if (score > std::max(.5, best)) {
            hovered = int(i);
            best = score;
        }
    }
    if (hovered != s.hover) {
        s.hover = hovered;
        s.overlay = {};
        setProperty("objectHover", hovered);
        update();
    }
}
void CanvasView::object_finish(QPointF point) {
    if (!objects_ || !objects_->drawing)
        return;
    auto& s = *objects_;
    object_move(point);
    s.drawing = false;
    if (s.revision != project_->revision() || s.candidates.empty()) {
        s.gesture = {};
        update();
        return;
    }
    const auto bounds = s.gesture.boundingRect();
    const bool click = (object_mode == 1 ? std::hypot(bounds.width(), bounds.height())
                                         : QLineF(s.origin, point).length()) *
                           zoom_ <
                       4;
    if (click)
        object_move(point);
    else
        s.gesture.closeSubpath();
    const auto size = s.candidates.front().mask.size();
    QImage range(size, QImage::Format_Grayscale8);
    range.fill(0);
    bool selected = false;
    for (std::size_t i = 0; i < s.candidates.size(); ++i) {
        const auto& candidate = s.candidates[i];
        bool include = click && int(i) == s.hover;
        if (!click) {
            int covered = 0, total = 0;
            for (int y = 0; y < size.height(); ++y) {
                const auto* row = candidate.mask.constScanLine(y);
                for (int x = 0; x < size.width(); ++x)
                    if (row[x] > 127) {
                        ++total;
                        if (s.gesture.contains(QPointF(
                                (x + .5) * project_->state().size.width() / size.width(),
                                (y + .5) * project_->state().size.height() / size.height())))
                            ++covered;
                    }
            }
            include = total > 0 && covered > total * .5;
        }
        if (!include)
            continue;
        selected = true;
        for (int y = 0; y < size.height(); ++y) {
            auto* row = range.scanLine(y);
            const auto* mask = candidate.mask.constScanLine(y);
            for (int x = 0; x < size.width(); ++x)
                row[x] = std::max(row[x], mask[x]);
        }
    }
    s.gesture = {};
    if (selected) {
        const auto full =
            range.scaled(project_->state().size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        edit(tr("对象选择"),
             [&](editing::State& state) { editing::combine_range(state, full, s.operation); });
        s.revision = project_->revision(); // A selection-only edit preserves inference results.
    }
    update();
}
void CanvasView::object_paint(QPainter& painter) {
    if (!objects_)
        return;
    auto& s = *objects_;
    if (s.overlay.isNull() && !s.candidates.empty() && (object_show_all || s.hover >= 0)) {
        const auto size = s.candidates.front().mask.size();
        s.overlay = QImage(size, QImage::Format_ARGB32_Premultiplied);
        s.overlay.fill(Qt::transparent);
        for (std::size_t i = 0; i < s.candidates.size(); ++i) {
            if (!object_show_all && int(i) != s.hover)
                continue;
            const auto& mask = s.candidates[i].mask;
            for (int y = 0; y < size.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(s.overlay.scanLine(y));
                const auto* m = mask.constScanLine(y);
                for (int x = 0; x < size.width(); ++x) {
                    const int alpha = std::max(qAlpha(row[x]), int(m[x]) * 90 / 255);
                    row[x] = qRgba(alpha * 45 / 255, alpha * 130 / 255, alpha, alpha);
                }
            }
        }
    }
    if (!s.overlay.isNull())
        painter.drawImage(QRectF(QPointF(), project_->state().size), s.overlay);
    if (s.drawing) {
        QPen pen(Qt::white, 1, Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(s.gesture);
    }
}
} // namespace huelith::desktop
