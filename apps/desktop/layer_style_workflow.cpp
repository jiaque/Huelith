#include "canvas_view.h"
#include "editor_controller.h"
#include "main_window.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <chrono>
#include <future>

namespace huelith::desktop {
using namespace editing;
void EditorController::show_layer_styles() {
    guarded([&] {
        auto* c = canvas();
        if (!c)
            return;
        c->finish_text(true);
        c->commit_transform();
        const auto base = c->project().snapshot();
        const auto* layer = find_layer(base.state.layers, c->active_layer);
        if (!layer || layer_locked(base.state.layers, layer->id) ||
            layer->kind == LayerKind::adjustment)
            throw EditError(tr("请选择可编辑的内容图层"));
        const auto id = layer->id;
        auto styles = layer->styles;
        styles.global_angle = base.state.global_light_angle;
        QDialog dialog(window_);
        dialog.setObjectName("layerStyleDialog");
        dialog.setWindowTitle(tr("图层样式"));
        QHBoxLayout root(&dialog);
        QListWidget list;
        list.setObjectName("styleEffects");
        list.setFixedWidth(170);
        const QStringList names{tr("描边"),   tr("投影"),   tr("内阴影"),
                                tr("外发光"), tr("内发光"), tr("颜色叠加")};
        for (int i = 0; i < names.size(); ++i) {
            auto* item = new QListWidgetItem(names[i], &list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(styles.entries[i].enabled ? Qt::Checked : Qt::Unchecked);
        }
        root.addWidget(&list);
        auto* middle = new QVBoxLayout;
        root.addLayout(middle);
        QFormLayout form;
        middle->addLayout(&form);
        QComboBox mode;
        mode.setObjectName("styleBlend");
        mode.addItems({tr("正常"),     tr("变暗"),   tr("正片叠底"), tr("颜色加深"), tr("线性加深"),
                       tr("变亮"),     tr("滤色"),   tr("颜色减淡"), tr("线性减淡"), tr("叠加"),
                       tr("柔光"),     tr("强光"),   tr("亮光"),     tr("线性光"),   tr("点光"),
                       tr("实色混合"), tr("差值"),   tr("排除"),     tr("减去"),     tr("划分"),
                       tr("色相"),     tr("饱和度"), tr("颜色"),     tr("明度")});
        form.addRow(tr("混合模式"), &mode);
        QPushButton color(tr("颜色…"));
        color.setObjectName("styleColor");
        form.addRow(tr("颜色"), &color);
        auto number = [&](const char* name, const QString& title, double minimum, double maximum) {
            auto* spin = new QDoubleSpinBox(&dialog);
            spin->setObjectName(name);
            spin->setRange(minimum, maximum);
            form.addRow(title, spin);
            return spin;
        };
        auto* opacity = number("styleOpacity", tr("不透明度 %"), 0, 100);
        auto* size = number("styleSize", tr("大小 px"), 0, 250);
        auto* distance = number("styleDistance", tr("距离 px"), 0, 1000);
        auto* spread = number("styleSpread", tr("扩展 / 阻塞 %"), 0, 100);
        auto* angle = number("styleAngle", tr("角度"), -360, 360);
        QCheckBox global(tr("使用全局光"));
        global.setObjectName("styleGlobalLight");
        form.addRow(&global);
        QComboBox position;
        position.setObjectName("stylePosition");
        form.addRow(tr("位置 / 源"), &position);
        middle->addStretch();
        auto* side = new QVBoxLayout;
        root.addLayout(side);
        QLabel preview;
        preview.setObjectName("stylePreview");
        preview.setFixedSize(400, 300);
        preview.setAlignment(Qt::AlignCenter);
        preview.setStyleSheet("background:#555;");
        side->addWidget(&preview);
        QLabel error;
        error.setWordWrap(true);
        side->addWidget(&error);
        QCheckBox enabled(tr("预览"));
        enabled.setObjectName("stylePreviewEnabled");
        enabled.setChecked(true);
        side->addWidget(&enabled);
        QPushButton save(tr("保存样式…")), load(tr("载入样式…"));
        side->addWidget(&save);
        side->addWidget(&load);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        side->addWidget(&buttons);
        bool updating{}, dirty{true};
        std::future<QImage> pending;
        const auto proxy = preview_state(base.state, 512);
        auto refresh = [&] {
            updating = true;
            const int index = std::max(0, list.currentRow());
            const auto& s = styles.entries[index];
            mode.setCurrentIndex(int(s.blend));
            opacity->setValue(s.opacity * 100);
            size->setValue(s.size);
            distance->setValue(s.distance);
            spread->setValue(s.spread * 100);
            global.setChecked(s.global_light);
            angle->setValue(s.global_light ? styles.global_angle : s.angle);
            position.clear();
            position.addItems(index == 0 ? QStringList{tr("外部"), tr("内部"), tr("居中")}
                                         : QStringList{tr("边缘"), tr("中心")});
            position.setCurrentIndex(std::min(s.position, position.count() - 1));
            color.setStyleSheet(QString("background:%1;").arg(s.color.name()));
            form.setRowVisible(size, index != 5);
            form.setRowVisible(distance, index == 1 || index == 2);
            form.setRowVisible(spread, index >= 1 && index <= 4);
            form.setRowVisible(angle, index == 1 || index == 2);
            form.setRowVisible(&global, index == 1 || index == 2);
            form.setRowVisible(&position, index == 0 || index == 4);
            updating = false;
        };
        auto apply = [&] {
            if (updating || list.currentRow() < 0)
                return;
            auto& s = styles.entries[list.currentRow()];
            s.blend = static_cast<Blend>(mode.currentIndex());
            s.opacity = opacity->value() / 100;
            s.size = size->value();
            s.distance = distance->value();
            s.spread = spread->value() / 100;
            s.global_light = global.isChecked();
            s.position = position.currentIndex();
            if (s.global_light)
                styles.global_angle = angle->value();
            else
                s.angle = angle->value();
            dirty = true;
        };
        connect(&list, &QListWidget::currentRowChanged, &dialog, [&](int) { refresh(); });
        connect(&list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem* item) {
            styles.entries[list.row(item)].enabled = item->checkState() == Qt::Checked;
            dirty = true;
        });
        for (auto* spin : {opacity, size, distance, spread, angle})
            connect(spin, &QDoubleSpinBox::valueChanged, &dialog, [&](double) { apply(); });
        connect(&mode, &QComboBox::currentIndexChanged, &dialog, [&](int) { apply(); });
        connect(&position, &QComboBox::currentIndexChanged, &dialog, [&](int) { apply(); });
        connect(&global, &QCheckBox::toggled, &dialog, [&](bool) { apply(); });
        connect(&enabled, &QCheckBox::toggled, &dialog, [&](bool) { dirty = true; });
        connect(&color, &QPushButton::clicked, &dialog, [&] {
            auto& s = styles.entries[list.currentRow()];
            const auto picked = QColorDialog::getColor(s.color, &dialog, tr("样式颜色"));
            if (picked.isValid()) {
                s.color = picked;
                dirty = true;
                refresh();
            }
        });
        connect(&save, &QPushButton::clicked, &dialog, [&] {
            const auto path = QFileDialog::getSaveFileName(&dialog, tr("保存图层样式"), {},
                                                           "Huelith Style (*.hstyle)");
            if (path.isEmpty())
                return;
            QSaveFile file(path);
            const auto bytes = QJsonDocument(styles_json(styles)).toJson();
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
                !file.commit())
                error.setText(file.errorString());
        });
        connect(&load, &QPushButton::clicked, &dialog, [&] {
            const auto path = QFileDialog::getOpenFileName(&dialog, tr("载入图层样式"), {},
                                                           "Huelith Style (*.hstyle)");
            if (path.isEmpty())
                return;
            try {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
                    throw EditError(tr("样式文件不可读或过大"));
                styles = styles_from_json(QJsonDocument::fromJson(file.readAll()).object());
                for (int i = 0; i < 6; ++i)
                    list.item(i)->setCheckState(styles.entries[i].enabled ? Qt::Checked
                                                                          : Qt::Unchecked);
                dirty = true;
                refresh();
            } catch (const std::exception& e) {
                error.setText(QString::fromUtf8(e.what()));
            }
        });
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QTimer timer;
        timer.setInterval(25);
        connect(&timer, &QTimer::timeout, &dialog, [&] {
            if (pending.valid() &&
                pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    preview.setPixmap(
                        QPixmap::fromImage(pending.get())
                            .scaled(preview.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                } catch (const std::exception& e) {
                    error.setText(QString::fromUtf8(e.what()));
                }
            }
            if (dirty && !pending.valid()) {
                dirty = false;
                auto state = proxy;
                state.global_light_angle = styles.global_angle;
                auto& value = find_layer(state.layers, id)->styles;
                value = styles;
                value.enabled = enabled.isChecked();
                const double scale = double(state.size.width()) / base.state.size.width();
                pending = std::async(std::launch::async, [state, scale] {
                    return display_image(render(state, {scale}));
                });
            }
            dialog.setProperty("styleReady", !dirty && !pending.valid());
        });
        list.setCurrentRow(0);
        refresh();
        timer.start();
        const bool accepted = dialog.exec() == QDialog::Accepted;
        timer.stop();
        if (pending.valid())
            pending.wait();
        if (accepted && c->project().revision() == base.revision)
            c->edit(tr("图层样式"), [&](State& state) {
                find_layer(state.layers, id)->styles = styles;
                state.global_light_angle = styles.global_angle;
            });
    });
}
} // namespace huelith::desktop
