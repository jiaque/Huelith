#include "canvas_view.h"
#include "editor_controller.h"
#include "main_window.h"
#include "tone_panel.h"
#include "window_header.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <future>

namespace huelith::desktop {
namespace {
class ToneDialog final : public QDialog {
  public:
    using QDialog::QDialog;
    std::function<bool(int)> can_close;
    std::function<bool()> escape_picker;
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape && escape_picker && escape_picker()) {
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }
    void done(int result) override {
        if (!can_close || can_close(result))
            QDialog::done(result);
    }
};
class TonePreview final : public QLabel {
  public:
    using QLabel::QLabel;
    QImage sample_image;
    std::function<void(QColor)> sampled;
    std::function<void(QColor)> target_begin;
    std::function<void(double)> target_move;
    std::function<void()> target_end;
    QPointF target_start;
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons().testFlag(Qt::LeftButton) && target_move)
            target_move((target_start.y() - event->position().y()) * .5 *
                        (event->modifiers().testFlag(Qt::AltModifier) ? .1 : 1.));
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && target_end)
            target_end();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !sampled || sample_image.isNull())
            return;
        const auto fitted = sample_image.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect area(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2),
                         fitted);
        if (!area.contains(event->position().toPoint()))
            return;
        const int x = int((event->position().x() - area.x()) * sample_image.width() / area.width());
        const int y =
            int((event->position().y() - area.y()) * sample_image.height() / area.height());
        target_start = event->position();
        if (target_begin)
            target_begin(sample_image.pixelColor(x, y));
        sampled(sample_image.pixelColor(x, y));
    }
};
} // namespace
void EditorController::show_tone(int category) {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    const auto base = c->project().state();
    const auto id = c->active_layer;
    const auto* selected = editing::find_layer(base.layers, id);
    if (!selected || editing::layer_locked(base.layers, id) ||
        selected->kind == editing::LayerKind::group)
        throw editing::EditError(tr("请选择未锁定的图片、像素或调整图层"));
    const QPointer<CanvasView> canvas_guard(c);
    const auto revision = c->project().revision();
    ToneDialog dialog(window_);
    dialog.setObjectName("toneDialog");
    dialog.setWindowTitle(tr("调整"));
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.resize(1000, 680);
    auto* layout = new QVBoxLayout(&dialog);
    auto* title = new QMenuBar;
    title->addAction(tr("调整"))->setEnabled(false);
    layout->addWidget(new WindowHeader(title, &dialog));
    auto* body = new QHBoxLayout;
    auto* preview = new TonePreview(&dialog);
    preview->setObjectName("tonePreview");
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(540, 400);
    body->addWidget(preview, 1);
    auto* panel = new TonePanel(selected->tone, &dialog);
    panel->setMinimumWidth(350);
    panel->set_category(category);
    auto* panel_scroll = new QScrollArea(&dialog);
    panel_scroll->setObjectName("tonePanelScroll");
    panel_scroll->setWidgetResizable(true);
    panel_scroll->setMinimumWidth(420);
    panel_scroll->setWidget(panel);
    body->addWidget(panel_scroll);
    layout->addLayout(body, 1);
    auto* error = new QLabel(&dialog);
    error->setObjectName("tonePreviewStatus");
    error->setWordWrap(true);
    layout->addWidget(error);
    auto* comparison = new QCheckBox(tr("预览"), &dialog);
    comparison->setChecked(true);
    layout->addWidget(comparison);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    std::future<std::array<QImage, 2>> worker;
    QTimer poll;
    poll.setInterval(20);
    bool pending = true;
    bool failed = false;
    int closing = -1;
    const auto schedule = [&] {
        pending = true;
        dialog.setProperty("previewBusy", true);
        failed = false;
        poll.start();
    };
    dialog.can_close = [&](int result) {
        if (result == QDialog::Accepted && failed)
            return false;
        if (!worker.valid() && !(result == QDialog::Accepted && pending))
            return true;
        closing = result;
        if (result == QDialog::Rejected)
            pending = false;
        panel->setEnabled(false);
        comparison->setEnabled(false);
        buttons->setEnabled(false);
        error->setText(tr("正在结束预览任务…"));
        poll.start();
        return false;
    };
    connect(&poll, &QTimer::timeout, &dialog, [&] {
        if (worker.valid()) {
            if (worker.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            try {
                const auto images = worker.get();
                if (!pending && closing != QDialog::Rejected) {
                    preview->sample_image = images[1];
                    panel->set_curve_histogram(images[1]);
                    preview->setPixmap(QPixmap::fromImage(images[0].scaled(
                        preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                    error->clear();
                    dialog.setProperty("previewBusy", false);
                    dialog.setProperty("previewPresented",
                                       dialog.property("previewPresented").toInt() + 1);
                }
            } catch (const std::exception& e) {
                if (!pending && closing != QDialog::Rejected) {
                    failed = true;
                    closing = -1;
                    panel->setEnabled(true);
                    comparison->setEnabled(true);
                    buttons->setEnabled(true);
                    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
                    error->setText(QString::fromUtf8(e.what()));
                }
            }
        }
        if (closing >= 0 && !pending) {
            dialog.done(closing);
            return;
        }
        if (!pending) {
            poll.stop();
            return;
        }
        pending = false;
        auto draft = base;
        if (comparison->isChecked())
            editing::find_layer(draft.layers, id)->tone = panel->settings();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        error->setText(tr("正在更新预览…"));
        const auto point_range =
            comparison->isChecked() ? panel->point_color_visualized() : std::nullopt;
        const int target_mode = panel->target_mode();
        const bool need_sample =
            panel->point_color_sampling() || point_range.has_value() || target_mode >= 0;
        const int detail_preview = comparison->isChecked() ? panel->detail_preview() : 0;
        dialog.setProperty("detailPreview", detail_preview);
        dialog.setProperty("previewBusy", true);
        worker = std::async(std::launch::async, [draft = std::move(draft), id, point_range,
                                                 detail_preview, need_sample, target_mode] {
            auto proxy = editing::preview_state(draft);
            editing::RenderOptions options{double(proxy.size.width()) / draft.size.width(), id,
                                           detail_preview};
            auto visible = editing::display_image(editing::render(proxy, options));
            QImage sample;
            if (need_sample) {
                auto* sample_layer = editing::find_layer(proxy.layers, id);
                sample_layer->tone.point_colors.clear();
                if (target_mode >= 0 && target_mode < 3) {
                    sample_layer->tone.hue.fill(0);
                    sample_layer->tone.saturation_bands.fill(0);
                    sample_layer->tone.luminance_bands.fill(0);
                } else if (target_mode == 3)
                    sample_layer->tone.parametric_curve.fill(0);
                options.detail_preview = 0;
                sample = editing::display_image(editing::render(proxy, options));
            }
            if (point_range) {
                visible = visible.convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < visible.height(); ++y) {
                    auto* row = reinterpret_cast<QRgb*>(visible.scanLine(y));
                    for (int x = 0; x < visible.width(); ++x) {
                        const auto color = QColor::fromRgba(row[x]),
                                   input = sample.pixelColor(x, y);
                        const double weight = editing::point_color_weight(
                            editing::point_color_hsl({input.redF(), input.greenF(), input.blueF()}),
                            *point_range);
                        const int gray = qGray(row[x]);
                        row[x] =
                            qRgba(qRound(gray + (color.red() - gray) * weight),
                                  qRound(gray + (color.green() - gray) * weight),
                                  qRound(gray + (color.blue() - gray) * weight), color.alpha());
                    }
                }
            }
            return std::array{visible, sample};
        });
    });
    panel->changed = schedule;
    panel->detail_view_changed = schedule;
    panel->point_view_changed = [&] {
        preview->setCursor((panel->point_color_sampling() || panel->target_mode() >= 0)
                               ? Qt::CrossCursor
                               : Qt::ArrowCursor);
        schedule();
    };
    preview->sampled = [&](QColor color) {
        if (panel->point_color_sampling() && !pending && !worker.valid())
            panel->sample_point_color(color);
    };
    preview->target_begin = [&](QColor color) {
        if (panel->target_mode() >= 0 && !pending && !worker.valid())
            panel->begin_target(color);
    };
    preview->target_move = [&](double amount) { panel->move_target(amount); };
    preview->target_end = [&] { panel->finish_target(); };
    dialog.escape_picker = [&] {
        if (panel->target_mode() >= 0) {
            const bool dragging = panel->target_dragging();
            panel->finish_target(true);
            if (!dragging)
                panel->set_target_mode(-1);
            return true;
        }
        if (!panel->point_color_sampling())
            return false;
        panel->stop_point_sampling();
        return true;
    };
    connect(comparison, &QCheckBox::toggled, &dialog, schedule);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    poll.start();
    if (dialog.exec() == QDialog::Accepted) {
        if (!canvas_guard || canvas_guard->project().revision() != revision)
            throw editing::EditError(tr("原文档已更改，请重新打开调整窗口"));
        editing::validate_tone(panel->settings());
        c->edit(tr("调整"), [&](editing::State& state) {
            editing::find_layer(state.layers, id)->tone = panel->settings();
        });
    }
}
} // namespace huelith::desktop
