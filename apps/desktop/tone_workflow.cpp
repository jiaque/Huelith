#include "canvas_view.h"
#include "editor_controller.h"
#include "main_window.h"
#include "tone_panel.h"
#include "window_header.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <future>

namespace huelith::desktop {
namespace {
class ToneDialog final : public QDialog {
  public:
    using QDialog::QDialog;
    std::function<bool(int)> can_close;
    void done(int result) override {
        if (!can_close || can_close(result))
            QDialog::done(result);
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
    auto* preview = new QLabel(&dialog);
    preview->setObjectName("tonePreview");
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(540, 400);
    body->addWidget(preview, 1);
    auto* panel = new TonePanel(selected->tone, &dialog);
    panel->setMinimumWidth(350);
    panel->set_category(category);
    body->addWidget(panel);
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
    std::future<QImage> worker;
    QTimer poll;
    poll.setInterval(20);
    bool pending = true;
    bool failed = false;
    int closing = -1;
    const auto schedule = [&] {
        pending = true;
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
                const auto image = worker.get();
                if (!pending && closing != QDialog::Rejected) {
                    preview->setPixmap(QPixmap::fromImage(image.scaled(
                        preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                    error->clear();
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
        worker = std::async(std::launch::async, [draft = std::move(draft)] {
            return editing::display_image(editing::render(editing::preview_state(draft)));
        });
    });
    panel->changed = schedule;
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
