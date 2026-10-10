#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/tiff_io.h"
#include "main_window.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageWriter>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
using namespace editing;
void EditorController::copy_pixels(bool merged, bool cut) {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    const auto& state = c->project().state();
    State source = state;
    const auto* layer = find_layer(state.layers, c->active_layer);
    if (!merged) {
        if (!layer)
            return;
        auto copy = *layer;
        const auto parent = parent_matrix(state, layer->id);
        copy.transform *= parent;
        copy.mask_transform *= parent;
        copy.visible = true;
        source.layers = {copy};
    }
    if (cut && (!layer || layer_locked(state.layers, c->active_layer)))
        throw EditError(tr("图层不可剪切"));
    auto image = display_image(render(source));
    QRect bounds;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            auto color = image.pixelColor(x, y);
            if (!state.selection.isNull())
                color.setAlpha(color.alpha() * state.selection.constScanLine(y)[x] / 255);
            image.setPixelColor(x, y, color);
            if (color.alpha())
                bounds = bounds.united(QRect(x, y, 1, 1));
        }
    if (bounds.isEmpty())
        return;
    auto* mime = new QMimeData;
    mime->setImageData(image.copy(bounds));
    mime->setData("application/x-huelith-origin",
                  QByteArray::number(bounds.x()) + "," + QByteArray::number(bounds.y()));
    QApplication::clipboard()->setMimeData(mime);
    if (QApplication::clipboard()->image().isNull())
        throw EditError(tr("系统剪贴板当前不可写，未执行剪切。请恢复桌面访问后重试。"));

    if (cut)
        c->edit(tr("剪切"), [&](State& target) {
            auto* current = find_layer(target.layers, c->active_layer);
            if (current->pixels.isNull())
                throw EditError(tr("请先将此层转换为像素图层"));
            const auto world = current->transform * parent_matrix(target, current->id);
            for (int y = 0; y < current->pixels.height(); ++y) {
                auto* row = reinterpret_cast<float*>(current->pixels.scanLine(y));
                for (int x = 0; x < current->pixels.width(); ++x) {
                    const auto point = world.map(QPointF(x + .5, y + .5)).toPoint();
                    const float amount =
                        target.selection.isNull() ? 1.F
                        : target.selection.rect().contains(point)
                            ? target.selection.constScanLine(point.y())[point.x()] / 255.F
                            : 0.F;
                    for (int channel = 0; channel < 4; ++channel)
                        row[x * 4 + channel] *= 1 - amount;
                }
            }
            current->kind = LayerKind::pixels;
            current->linked_path.clear();
            current->linked_hash.clear();
        });
}
void EditorController::paste_pixels(bool in_place, bool into_selection) {
    auto* c = canvas();
    if (!c)
        return;
    const auto image = QApplication::clipboard()->image();
    if (image.isNull())
        return;
    c->commit_transform();
    const auto& state = c->project().state();
    if (into_selection && state.selection.isNull())
        throw EditError(tr("请先建立目标选区"));
    Layer layer;
    layer.name = tr("粘贴");
    layer.pixels = to_working(image);
    QPointF origin((state.size.width() - image.width()) / 2.,
                   (state.size.height() - image.height()) / 2.);
    if (in_place) {
        const auto fields =
            QApplication::clipboard()->mimeData()->data("application/x-huelith-origin").split(',');
        if (fields.size() == 2) {
            bool x_ok{}, y_ok{};
            const double x = fields[0].toDouble(&x_ok), y = fields[1].toDouble(&y_ok);
            if (x_ok && y_ok && std::isfinite(x) && std::isfinite(y))
                origin = QPointF(x, y);
        }
    }
    if (into_selection) {
        QRect bounds;
        for (int y = 0; y < state.selection.height(); ++y)
            for (int x = 0; x < state.selection.width(); ++x)
                if (state.selection.constScanLine(y)[x])
                    bounds = bounds.united(QRect(x, y, 1, 1));
        if (bounds.isEmpty())
            return;
        origin = bounds.center() - QPointF(image.width() / 2., image.height() / 2.);
        layer.mask = state.selection;
        layer.mask_linked = false;
    }
    layer.transform.translate(origin.x(), origin.y());
    c->edit(tr("粘贴"), [&](State& target) {
        target.layers.push_back(layer);
        if (into_selection)
            target.selection = {};
    });
    c->active_layer = layer.id;
    c->selected_layers = {layer.id};
    c->prepare_inserted_layer();
    refresh();
}
void EditorController::align_layers(const QString& operation) {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    const auto ids = root_targets(c->project().state(), c->selected_layers.empty()
                                                            ? std::vector<QUuid>{c->active_layer}
                                                            : c->selected_layers);
    if (ids.empty())
        return;
    c->edit(operation, [&](State& state) {
        QRectF extent;
        for (const auto& id : ids)
            extent = extent.united(layer_bounds(state, id));
        if (align_to_canvas_)
            extent = QRectF(QPointF(), QSizeF(state.size));
        auto order = ids;
        const bool distribute = operation.contains(tr("分布"));
        const bool vertical = operation == tr("垂直分布");
        if (distribute)
            std::sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
                const auto pa = layer_bounds(state, a).center(),
                           pb = layer_bounds(state, b).center();
                return vertical ? pa.y() < pb.y() : pa.x() < pb.x();
            });
        double length = 0;
        if (distribute)
            for (const auto& id : order) {
                const auto bounds = layer_bounds(state, id);
                length += vertical ? bounds.height() : bounds.width();
            }
        const double gap = order.size() > 1
                               ? ((vertical ? extent.height() : extent.width()) - length) /
                                     double(order.size() - 1)
                               : 0;
        double cursor = vertical ? extent.top() : extent.left();
        for (const auto& id : order) {
            const auto bounds = layer_bounds(state, id);
            double x = 0, y = 0;
            if (operation == tr("左对齐画布"))
                x = extent.left() - bounds.left();
            else if (operation == tr("右对齐"))
                x = extent.right() - bounds.right();
            else if (operation == tr("水平居中画布"))
                x = extent.center().x() - bounds.center().x();
            else if (operation == tr("顶对齐"))
                y = extent.top() - bounds.top();
            else if (operation == tr("底对齐"))
                y = extent.bottom() - bounds.bottom();
            else if (operation == tr("垂直居中画布"))
                y = extent.center().y() - bounds.center().y();
            else if (distribute) {
                if (vertical)
                    y = cursor - bounds.top();
                else
                    x = cursor - bounds.left();
                cursor += (vertical ? bounds.height() : bounds.width()) + gap;
            }
            QTransform delta;
            delta.translate(x, y);
            transform_targets(state, {id}, delta, false);
        }
    });
}
void EditorController::show_export() {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    const auto original = render(c->project().state());
    QDialog dialog(window_);
    dialog.setObjectName("exportDialog");
    dialog.setWindowTitle(tr("导出图片"));
    dialog.resize(900, 650);
    auto* layout = new QHBoxLayout(&dialog);
    auto* preview = new QLabel(&dialog);
    preview->setMinimumSize(500, 400);
    preview->setAlignment(Qt::AlignCenter);
    layout->addWidget(preview, 1);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* format = new QComboBox(&dialog);
    format->setObjectName("exportFormat");
    format->addItems({"PNG", "JPEG", "TIFF"});
    form->addRow(tr("格式"), format);
    auto* profile = new QComboBox(&dialog);
    profile->setObjectName("exportProfile");
    profile->addItems({"sRGB", "Adobe RGB", "Display P3"});
    form->addRow(tr("色彩空间"), profile);
    auto* depth = new QComboBox(&dialog);
    depth->setObjectName("exportDepth");
    depth->addItems({tr("8位"), tr("16位")});
    form->addRow(tr("位深"), depth);
    auto* width = new QSpinBox(&dialog);
    width->setObjectName("exportWidth");
    width->setRange(1, 65536);
    width->setValue(original.width());
    form->addRow(tr("宽度"), width);
    auto* height = new QSpinBox(&dialog);
    height->setObjectName("exportHeight");
    height->setRange(1, 65536);
    height->setValue(original.height());
    form->addRow(tr("高度"), height);
    auto* proportions = new QCheckBox(tr("锁定宽高比"), &dialog);
    proportions->setObjectName("exportProportions");
    proportions->setChecked(true);
    form->addRow(proportions);
    connect(width, &QSpinBox::valueChanged, &dialog, [&](int value) {
        if (proportions->isChecked()) {
            QSignalBlocker blocker(height);
            height->setValue(qRound(double(value) * original.height() / original.width()));
        }
    });
    connect(height, &QSpinBox::valueChanged, &dialog, [&](int value) {
        if (proportions->isChecked()) {
            QSignalBlocker blocker(width);
            width->setValue(qRound(double(value) * original.width() / original.height()));
        }
    });
    auto* quality = new QSpinBox(&dialog);
    quality->setObjectName("exportQuality");
    quality->setRange(1, 100);
    quality->setValue(95);
    form->addRow(tr("JPEG质量"), quality);
    auto* transparency = new QCheckBox(tr("保留透明度"), &dialog);
    transparency->setObjectName("exportAlpha");
    transparency->setChecked(true);
    form->addRow(transparency);
    QColor matte = Qt::white;
    auto* background = new QPushButton(tr("背景填充色…"), &dialog);
    form->addRow(background);
    auto* error = new QLabel(&dialog);
    error->setWordWrap(true);
    form->addRow(error);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QImage converted;
    const auto update = [&] {
        const bool jpeg = format->currentIndex() == 1;
        depth->setEnabled(!jpeg);
        quality->setEnabled(jpeg);
        transparency->setEnabled(!jpeg);
        if (qint64(width->value()) * height->value() > 64000000) {
            error->setText(tr("输出超过6400万像素预算"));
            buttons->button(QDialogButtonBox::Save)->setEnabled(false);
            return;
        }
        buttons->button(QDialogButtonBox::Save)->setEnabled(true);
        error->clear();
        const QColorSpace space(profile->currentIndex() == 1   ? QColorSpace::AdobeRgb
                                : profile->currentIndex() == 2 ? QColorSpace::DisplayP3
                                                               : QColorSpace::SRgb);
        converted = display_image(original, space)
                        .scaled(width->value(), height->value(), Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation);
        if (jpeg || !transparency->isChecked()) {
            QImage filled(converted.size(), QImage::Format_RGB32);
            filled.setColorSpace(space);
            filled.fill(matte);
            QPainter painter(&filled);
            painter.drawImage(0, 0, converted);
            painter.end();
            converted = filled;
        }
        auto sample = converted.scaled(540, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (jpeg) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            sample.save(&buffer, "JPEG", quality->value());
            sample = QImage::fromData(bytes, "JPEG");
        }
        preview->setPixmap(QPixmap::fromImage(sample));
    };
    for (auto* box : {format, profile, depth})
        connect(box, &QComboBox::currentIndexChanged, &dialog, update);
    connect(width, &QSpinBox::valueChanged, &dialog, update);
    connect(height, &QSpinBox::valueChanged, &dialog, update);
    connect(quality, &QSpinBox::valueChanged, &dialog, update);
    connect(transparency, &QCheckBox::toggled, &dialog, update);
    connect(background, &QPushButton::clicked, &dialog, [&] {
        const auto color = QColorDialog::getColor(matte, &dialog);
        if (color.isValid()) {
            matte = color;
            update();
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    if (dialog.exec() != QDialog::Accepted)
        return;
    const bool jpeg = format->currentIndex() == 1;
    const auto path = QFileDialog::getSaveFileName(
        window_, tr("导出图片"),
        c->project().state().name + (jpeg                          ? ".jpg"
                                     : format->currentIndex() == 2 ? ".tif"
                                                                   : ".png"),
        jpeg                          ? "JPEG (*.jpg)"
        : format->currentIndex() == 2 ? "TIFF (*.tif *.tiff)"
                                      : "PNG (*.png)");
    if (path.isEmpty())
        return;
    if (!jpeg && depth->currentIndex() == 1) {
        const QColorSpace space(profile->currentIndex() == 1   ? QColorSpace::AdobeRgb
                                : profile->currentIndex() == 2 ? QColorSpace::DisplayP3
                                                               : QColorSpace::SRgb);
        converted = original.convertedToColorSpace(space, QImage::Format_RGBA64)
                        .scaled(width->value(), height->value(), Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation);
        if (!transparency->isChecked()) {
            QImage filled(converted.size(), QImage::Format_RGBA64);
            filled.setColorSpace(space);
            filled.fill(matte);
            QPainter painter(&filled);
            painter.drawImage(0, 0, converted);
            painter.end();
            converted = filled;
        }
    }
    if (format->currentIndex() == 2) {
        write_tiff(converted, path, depth->currentIndex() == 1 ? 16 : 8, std::atomic_bool{false},
                   c->project().state().ppi);
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw EditError(file.errorString());
    QImageWriter writer(&file, jpeg ? "jpg" : "png");
    writer.setQuality(quality->value());
    if (!writer.write(converted))
        throw EditError(writer.errorString());
    if (!file.commit())
        throw EditError(file.errorString());
}
void EditorController::show_shortcuts() {
    QDialog dialog(window_);
    dialog.setObjectName("shortcutsDialog");
    dialog.setWindowTitle(tr("快捷键"));
    dialog.resize(720, 650);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(&dialog);
    table->setObjectName("shortcutTable");
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("命令"), tr("快捷键")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table);
    QList<QAction*> actions;
    QSet<QString> names;
    for (auto* action : window_->findChildren<QAction*>())
        if (!action->isSeparator() && !action->menu() && !action->text().isEmpty() &&
            !names.contains(action->text())) {
            names.insert(action->text());
            actions.push_back(action);
        }
    table->setRowCount(int(actions.size()));
    for (int i = 0; i < actions.size(); ++i) {
        auto* title = new QTableWidgetItem(actions[i]->text());
        title->setFlags(title->flags() & ~Qt::ItemIsEditable);
        table->setItem(i, 0, title);
        table->setCellWidget(i, 1, new QKeySequenceEdit(actions[i]->shortcut(), table));
    }
    auto* error = new QLabel(&dialog);
    error->setObjectName("shortcutError");
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::RestoreDefaults,
                                         &dialog);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog,
            [&] {
                for (int i = 0; i < actions.size(); ++i)
                    if (actions[i]->property("defaultShortcut").isValid())
                        static_cast<QKeySequenceEdit*>(table->cellWidget(i, 1))
                            ->setKeySequence(
                                QKeySequence(actions[i]->property("defaultShortcut").toString()));
            });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QSet<QString> seen;
        for (int i = 0; i < actions.size(); ++i) {
            const auto text =
                static_cast<QKeySequenceEdit*>(table->cellWidget(i, 1))->keySequence().toString();
            if (!text.isEmpty() && seen.contains(text)) {
                error->setText(tr("快捷键冲突：%1；尚未应用任何修改").arg(text));
                return;
            }
            if (!text.isEmpty())
                seen.insert(text);
        }
        for (int i = 0; i < actions.size(); ++i) {
            const auto key = static_cast<QKeySequenceEdit*>(table->cellWidget(i, 1))->keySequence();
            actions[i]->setShortcut(key);
            QSettings().setValue("shortcuts/" + actions[i]->text(), key.toString());
        }
        dialog.accept();
    });
    dialog.exec();
}
void EditorController::offer_recovery() {
    const QDir directory(recovery_directory());
    const auto files = directory.entryInfoList({"*.huelith"}, QDir::Files, QDir::Time);
    if (files.isEmpty())
        return;
    QDialog dialog(window_);
    dialog.setObjectName("recoveryDialog");
    dialog.setWindowTitle(tr("恢复未保存的文档"));
    dialog.resize(650, 380);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("发现恢复副本。打开后请另存工程；取消会保留副本。"), &dialog));
    auto* list = new QListWidget(&dialog);
    list->setObjectName("recoveryList");
    for (const auto& file : files) {
        auto* item = new QListWidgetItem(
            file.lastModified().toString("yyyy-MM-dd HH:mm") + "  " + file.fileName(), list);
        item->setData(Qt::UserRole, file.absoluteFilePath());
    }
    list->setCurrentRow(0);
    layout->addWidget(list);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted && list->currentItem() &&
        open(list->currentItem()->data(Qt::UserRole).toString())) {
        canvas()->filename.clear();
        canvas()->project().mark_saved(0);
        refresh();
    }
}
} // namespace huelith::desktop
