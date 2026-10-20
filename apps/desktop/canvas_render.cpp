#include "canvas_view.h"
#include <QCursor>
#include <QTimer>
#include <QToolTip>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>

namespace huelith::desktop {
struct CanvasView::RenderWork {
    struct Result {
        QImage linear, display;
        QString error;
        quint64 generation{};
    };
    struct Progress {
        std::mutex mutex;
        std::optional<Result> image;
    };
    std::future<Result> future;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::optional<editing::State> pending;
    QColorSpace color_space;
    quint64 generation{};
    quint64 completed{};
    quint64 first_generation{};
    quint64 proxy_generation{};
    std::shared_ptr<Progress> progress{std::make_shared<Progress>()};
};

CanvasView::~CanvasView() {
    if (render_work_ && render_work_->cancelled)
        render_work_->cancelled->store(true, std::memory_order_relaxed);
    // Workers own immutable document snapshots and never dereference the widget.
    // Join before the application/font engine or the hosting DLL is destroyed.
    if (render_work_ && render_work_->future.valid())
        render_work_->future.wait();
}

bool CanvasView::preview_pending() const {
    return render_work_ && render_work_->completed != render_work_->generation;
}

bool CanvasView::require_current_preview() {
    if (!preview_pending())
        return true;
    QToolTip::showText(QCursor::pos(), tr("预览更新中，请稍后取样"), this);
    return false;
}

void CanvasView::reset_preview(const QImage& linear) {
    if (render_work_) {
        auto& work = *render_work_;
        if (work.cancelled)
            work.cancelled->store(true, std::memory_order_relaxed);
        work.pending.reset();
        work.completed = ++work.generation;
        work.first_generation = work.generation;
    }
    linear_preview_ = linear;
    preview_ = editing::display_image(linear, monitor_space_);
    setProperty("previewPending", false);
    setProperty("previewError", QString{});
}

void CanvasView::request_preview(const editing::State& state) {
    if (!render_work_) {
        render_work_ = std::make_shared<RenderWork>();
        auto* timer = new QTimer(this);
        timer->setObjectName("canvasRenderTimer");
        timer->setInterval(16);
        connect(timer, &QTimer::timeout, this, [this] { poll_preview(); });
        timer->start();
    }
    auto& work = *render_work_;
    ++work.generation;
    work.pending = state;
    work.color_space = monitor_space_;
    if (work.cancelled)
        work.cancelled->store(true, std::memory_order_relaxed);
    setProperty("previewError", QString{});
    setProperty("previewPending", true);
    poll_preview();
}

void CanvasView::poll_preview() {
    auto& work = *render_work_;
    std::optional<RenderWork::Result> progress;
    {
        const std::lock_guard lock(work.progress->mutex);
        progress = std::move(work.progress->image);
        work.progress->image.reset();
    }
    if (progress && progress->generation >= work.first_generation &&
        progress->generation > work.proxy_generation && progress->generation > work.completed) {
        work.proxy_generation = progress->generation;
        preview_ = std::move(progress->display);
        setProperty("previewProxyGeneration", qulonglong(work.proxy_generation));
        update();
    }
    const auto publish = [this, &work](RenderWork::Result result) {
        if (result.generation != work.generation)
            return;
        if (result.error.isEmpty()) {
            linear_preview_ = std::move(result.linear);
            preview_ = std::move(result.display);
        }
        work.completed = result.generation;
        setProperty("previewPending", false);
        setProperty("previewError", result.error);
        setProperty("previewGeneration", qulonglong(work.completed));
        update();
    };
    if (work.future.valid()) {
        if (work.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        publish(work.future.get());
    }
    if (!work.pending)
        return;
    auto state = std::move(*work.pending);
    work.pending.reset();
    work.cancelled = std::make_shared<std::atomic_bool>(false);
    const auto render = [state = std::move(state), cancelled = work.cancelled,
                         color_space = work.color_space, generation = work.generation,
                         progress = work.progress] {
        RenderWork::Result result;
        result.generation = generation;
        try {
            if (qint64(state.size.width()) * state.size.height() > 2'000'000) {
                // Finish a bounded proxy even if another input arrives. Otherwise a
                // continuous drag can starve every frame by cancelling before presentation.
                const auto proxy = editing::preview_state(state, 1280);
                editing::RenderOptions proxy_options;
                proxy_options.pixel_scale = double(proxy.size.width()) / state.size.width();
                RenderWork::Result frame;
                frame.generation = generation;
                frame.display =
                    editing::display_image(editing::render(proxy, proxy_options), color_space);
                {
                    const std::lock_guard lock(progress->mutex);
                    progress->image = std::move(frame);
                }
                if (cancelled->load(std::memory_order_relaxed))
                    return result;
            }
            editing::RenderOptions options;
            options.cancelled = cancelled.get();
            result.linear = editing::render(state, options);
            if (!cancelled->load(std::memory_order_relaxed))
                result.display = editing::display_image(result.linear, color_space);
        } catch (const std::exception& error) {
            result.error = QString::fromUtf8(error.what());
        }
        return result;
    };
    // Small documents retain immediate pixel feedback; large compositions never
    // execute in the event handler. Only one worker and the newest pending request exist.
    if (qint64(spec_.width) * spec_.height <= 2'000'000)
        publish(render());
    else
        work.future = std::async(std::launch::async, render);
}
} // namespace huelith::desktop
