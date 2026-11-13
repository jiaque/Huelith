#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/image_storage.h"
#include "huelith/editing/layout_regions.h"
#include "huelith/editing/photo_operations.h"
#include "main_window.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace huelith::desktop {
void EditorController::show_image_size(bool canvas_only) {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    const auto original = c->project().state();
    QDialog dialog(window_);
    dialog.setWindowTitle(canvas_only ? tr("画布大小") : tr("图像大小"));
    QFormLayout form(&dialog);
    QSpinBox width, height;
    width.setRange(1, 65536);
    height.setRange(1, 65536);
    width.setValue(original.size.width());
    height.setValue(original.size.height());
    width.setObjectName("imageSizeWidth");
    height.setObjectName("imageSizeHeight");
    form.addRow(tr("宽度 px"), &width);
    form.addRow(tr("高度 px"), &height);
    QDoubleSpinBox ppi;
    ppi.setRange(1, 100000);
    ppi.setDecimals(2);
    ppi.setValue(original.ppi);
    if (!canvas_only)
        form.addRow(tr("分辨率 ppi"), &ppi);
    QCheckBox resample(tr("重采样（改变像素尺寸）")), lock(tr("保持比例"));
    resample.setChecked(true);
    lock.setChecked(true);
    if (!canvas_only) {
        form.addRow(&resample);
        form.addRow(&lock);
    }
    QComboBox anchor;
    anchor.addItems({tr("左上"), tr("中上"), tr("右上"), tr("左中"), tr("中心"), tr("右中"),
                     tr("左下"), tr("中下"), tr("右下")});
    anchor.setCurrentIndex(4);
    QCheckBox erase(tr("删除画布以外的像素"));
    if (canvas_only) {
        form.addRow(tr("定位"), &anchor);
        form.addRow(&erase);
    }
    QLabel physical, error;
    form.addRow(&physical);
    form.addRow(&error);
    const auto refresh = [&] {
        physical.setText(tr("打印尺寸：%1 × %2 mm")
                             .arg(width.value() / ppi.value() * 25.4, 0, 'f', 2)
                             .arg(height.value() / ppi.value() * 25.4, 0, 'f', 2));
    };
    connect(&width, &QSpinBox::valueChanged, &dialog, [&](int value) {
        if (lock.isChecked() && !canvas_only) {
            QSignalBlocker blocker(height);
            height.setValue(qRound(double(value) * original.size.height() / original.size.width()));
        }
        refresh();
    });
    connect(&height, &QSpinBox::valueChanged, &dialog, [&](int value) {
        if (lock.isChecked() && !canvas_only) {
            QSignalBlocker blocker(width);
            width.setValue(qRound(double(value) * original.size.width() / original.size.height()));
        }
        refresh();
    });
    connect(&ppi, &QDoubleSpinBox::valueChanged, &dialog, [&] { refresh(); });
    connect(&resample, &QCheckBox::toggled, &dialog, [&](bool on) {
        width.setEnabled(on);
        height.setEnabled(on);
        if (!on) {
            width.setValue(original.size.width());
            height.setValue(original.size.height());
        }
        refresh();
    });
    refresh();
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (qint64(width.value()) * height.value() > editing::maximum_image_pixels) {
            error.setText(tr("尺寸超过像素预算"));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    c->edit(dialog.windowTitle(), [&](editing::State& state) {
        if (canvas_only) {
            const int dx = qRound((width.value() - state.size.width()) *
                                  (anchor.currentIndex() % 3) / 2.),
                      dy = qRound((height.value() - state.size.height()) *
                                  (anchor.currentIndex() / 3) / 2.);
            editing::crop_canvas(state, QRect(-dx, -dy, width.value(), height.value()), 0,
                                 erase.isChecked());
        } else if (resample.isChecked())
            editing::resize_document(state, {width.value(), height.value()}, ppi.value());
        else
            state.ppi = ppi.value();
    });
}
void EditorController::show_regions(bool slices) {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    auto draft = c->project().state();
    QDialog dialog(window_);
    dialog.setWindowTitle(slices ? tr("切片管理") : tr("画板管理"));
    dialog.resize(680, 450);
    QVBoxLayout root(&dialog);
    QHBoxLayout body;
    root.addLayout(&body);
    QListWidget list;
    list.setObjectName("regionList");
    body.addWidget(&list);
    QFormLayout form;
    body.addLayout(&form);
    QLineEdit name;
    name.setObjectName("regionName");
    form.addRow(tr("名称"), &name);
    QSpinBox x, y, width, height;
    x.setObjectName("regionX");
    y.setObjectName("regionY");
    width.setObjectName("regionWidth");
    height.setObjectName("regionHeight");
    for (auto* field : {&x, &y})
        field->setRange(0, 65535);
    for (auto* field : {&width, &height})
        field->setRange(1, 65536);
    form.addRow(tr("X"), &x);
    form.addRow(tr("Y"), &y);
    form.addRow(tr("宽度 px"), &width);
    form.addRow(tr("高度 px"), &height);
    QComboBox preset;
    preset.addItems(
        {tr("自定义"), "1920 × 1080", "1080 × 1920", "1080 × 1080", tr("A4（当前分辨率）")});
    form.addRow(tr("尺寸预设"), &preset);
    connect(&preset, &QComboBox::activated, &dialog, [&](int index) {
        const QSize sizes[]{draft.size,
                            {1920, 1080},
                            {1080, 1920},
                            {1080, 1080},
                            {qRound(210 / 25.4 * draft.ppi), qRound(297 / 25.4 * draft.ppi)}};
        width.setValue(sizes[index].width());
        height.setValue(sizes[index].height());
    });
    QCheckBox collect(tr("新建画板时移入当前选中的图层"));
    collect.setChecked(true);
    collect.setVisible(!slices);
    form.addRow(&collect);
    QLabel error;
    error.setWordWrap(true);
    root.addWidget(&error);
    QHBoxLayout actions;
    QPushButton add(tr("新增")), apply(tr("更新当前区域")),
        remove(slices ? tr("删除切片") : tr("删除画板及内容"));
    actions.addWidget(&add);
    actions.addWidget(&apply);
    actions.addWidget(&remove);
    root.addLayout(&actions);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root.addWidget(&buttons);
    std::vector<QUuid> ids;
    const auto rebuild = [&] {
        list.clear();
        ids.clear();
        if (slices) {
            for (const auto& slice : draft.slices) {
                ids.push_back(slice.id);
                list.addItem(QString::number(ids.size()) + ". " + slice.name);
            }
        } else {
            const auto visit = [&](auto&& self, const std::vector<editing::Layer>& layers) -> void {
                for (const auto& layer : layers) {
                    if (layer.artboard) {
                        ids.push_back(layer.id);
                        list.addItem(layer.name);
                    }
                    self(self, layer.children);
                }
            };
            visit(visit, draft.layers);
        }
    };
    connect(&list, &QListWidget::currentRowChanged, &dialog, [&](int index) {
        if (index < 0 || index >= int(ids.size()))
            return;
        QRect bounds;
        if (slices) {
            const auto& slice = draft.slices[index];
            name.setText(slice.name);
            bounds = slice.bounds;
        } else {
            const auto* board = editing::find_layer(draft.layers, ids[index]);
            name.setText(board->name);
            bounds = (board->transform * editing::parent_matrix(draft, board->id))
                         .mapRect(*board->frame)
                         .toAlignedRect();
        }
        x.setValue(bounds.x());
        y.setValue(bounds.y());
        width.setValue(bounds.width());
        height.setValue(bounds.height());
    });
    const auto update = [&](bool creating) {
        const auto before = draft;
        try {
            if (name.text().trimmed().isEmpty())
                throw editing::EditError(tr("请输入名称"));
            const QRect bounds(x.value(), y.value(), width.value(), height.value());
            const int row = list.currentRow();
            if (!creating && row < 0)
                throw editing::EditError(tr("请选择一个区域"));
            if (slices) {
                if (!QRect(QPoint(), draft.size).contains(bounds))
                    throw editing::EditError(tr("切片必须位于画布以内"));
                if (creating)
                    draft.slices.push_back({QUuid::createUuid(), name.text().trimmed(), bounds});
                else {
                    draft.slices[row].name = name.text().trimmed();
                    draft.slices[row].bounds = bounds;
                }
            } else {
                const QSize expanded(std::max(draft.size.width(), bounds.right() + 1),
                                     std::max(draft.size.height(), bounds.bottom() + 1));
                if (expanded != draft.size)
                    editing::crop_canvas(draft, QRect(QPoint(), expanded), 0, false);
                if (creating) {
                    editing::create_artboard(draft, name.text(), bounds,
                                             collect.isChecked() ? c->selected_layers
                                                                 : std::vector<QUuid>{});
                    collect.setChecked(false);
                } else {
                    auto* board = editing::find_layer(draft.layers, ids[row]);
                    board->name = name.text().trimmed();
                    editing::set_artboard_bounds(*board, bounds);
                    board->transform *= editing::parent_matrix(draft, board->id).inverted();
                }
            }
            editing::validate_state(draft);
            rebuild();
            list.setCurrentRow(creating ? list.count() - 1 : row);
            error.clear();
            return true;
        } catch (const std::exception& e) {
            draft = before;
            error.setText(QString::fromUtf8(e.what()));
            return false;
        }
    };
    connect(&add, &QPushButton::clicked, &dialog, [&] { update(true); });
    connect(&apply, &QPushButton::clicked, &dialog, [&] { update(false); });
    connect(&remove, &QPushButton::clicked, &dialog, [&] {
        const int row = list.currentRow();
        if (row < 0)
            return;
        if (slices)
            draft.slices.erase(draft.slices.begin() + row);
        else
            editing::remove_layer(draft, ids[row]);
        rebuild();
        list.setCurrentRow(std::min(row, list.count() - 1));
    });
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (list.currentRow() < 0 || update(false))
            dialog.accept();
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rebuild();
    list.setCurrentRow(0);
    if (list.count() == 0) {
        name.setText(slices ? tr("切片 1") : tr("画板 1"));
        width.setValue(draft.size.width());
        height.setValue(draft.size.height());
    }
    if (dialog.exec() == QDialog::Accepted)
        c->edit(slices ? tr("编辑切片") : tr("编辑画板"),
                [&](editing::State& state) { state = draft; });
}
} // namespace huelith::desktop
