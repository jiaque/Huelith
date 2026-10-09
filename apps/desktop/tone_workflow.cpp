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
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace huelith::desktop {
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
    QDialog dialog(window_);
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
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(540, 400);
    body->addWidget(preview, 1);
    auto* panel = new TonePanel(selected->tone, &dialog);
    panel->setMinimumWidth(350);
    panel->set_category(category);
    body->addWidget(panel);
    layout->addLayout(body, 1);
    auto* error = new QLabel(&dialog);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto* comparison = new QCheckBox(tr("预览"), &dialog);
    comparison->setChecked(true);
    layout->addWidget(comparison);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QTimer debounce;
    debounce.setSingleShot(true);
    debounce.setInterval(40);
    const auto update = [&] {
        try {
            auto draft = base;
            if (comparison->isChecked())
                editing::find_layer(draft.layers, id)->tone = panel->settings();
            const auto image = editing::display_image(editing::render(draft));
            preview->setPixmap(QPixmap::fromImage(
                image.scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            error->clear();
            buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
        } catch (const std::exception& e) {
            error->setText(QString::fromUtf8(e.what()));
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        }
    };
    panel->changed = [&] { debounce.start(); };
    connect(&debounce, &QTimer::timeout, &dialog, update);
    connect(comparison, &QCheckBox::toggled, &dialog, update);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() == QDialog::Accepted) {
        editing::validate_tone(panel->settings());
        c->edit(tr("调整"), [&](editing::State& state) {
            editing::find_layer(state.layers, id)->tone = panel->settings();
        });
    }
}
} // namespace huelith::desktop
