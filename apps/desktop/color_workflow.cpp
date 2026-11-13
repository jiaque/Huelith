#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/document_color.h"
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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSettings>

namespace huelith::desktop {
void EditorController::show_spot_channel() {
    auto* c = canvas();
    if (!c)
        return;
    const auto original = c->project().state();
    const auto spots = original.metadata.value("spotChannels").toObject();
    const auto selected = channels_->currentItem()
                              ? channels_->currentItem()->data(Qt::UserRole).toString()
                              : QString{};
    const auto previous = spots.value(selected).toObject();
    QDialog dialog(window_);
    dialog.setWindowTitle(tr("专色通道"));
    QFormLayout form(&dialog);
    QLineEdit name(previous.isEmpty() ? tr("专色 1") : selected);
    form.addRow(tr("油墨名称"), &name);
    QColor color(previous.value("color").toString("#d94666"));
    QPushButton choose(color.name());
    form.addRow(tr("预览颜色"), &choose);
    QDoubleSpinBox solidity;
    solidity.setRange(0, 100);
    solidity.setValue(previous.value("solidity").toDouble(1) * 100);
    solidity.setSuffix(" %");
    form.addRow(tr("预览实度"), &solidity);
    QCheckBox selection(tr("用当前选区替换覆盖范围"));
    selection.setChecked(previous.isEmpty());
    form.addRow(&selection);
    QLabel hint(tr("覆盖范围可载入快速蒙版后用画笔编辑。普通图片导出合成专色外观；印刷分版请使用“导"
                   "出专色印版”。实度只影响预览和合成，不改变印版覆盖量。"));
    hint.setWordWrap(true);
    hint.setMaximumWidth(460);
    form.addRow(&hint);
    QLabel error;
    form.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&choose, &QPushButton::clicked, &dialog, [&] {
        const auto value = QColorDialog::getColor(color, &dialog);
        if (value.isValid()) {
            color = value;
            choose.setText(color.name());
        }
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name.text().trimmed().isEmpty()) {
            error.setText(tr("请输入名称"));
            return;
        }
        if (name.text().trimmed() != selected &&
            original.saved_selections.contains(name.text().trimmed())) {
            error.setText(tr("通道名称已存在"));
            return;
        }
        if (selection.isChecked() && original.selection.isNull()) {
            error.setText(tr("请先建立油墨覆盖选区"));
            return;
        }
        if (previous.isEmpty() && !selection.isChecked()) {
            error.setText(tr("新专色需要当前选区"));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    c->edit(tr("编辑专色通道"), [&](editing::State& state) {
        auto inks = state.metadata.value("spotChannels").toObject();
        const auto title = name.text().trimmed();
        const auto mask =
            selection.isChecked() ? state.selection : state.saved_selections.value(selected);
        if (!previous.isEmpty() && title != selected) {
            inks.remove(selected);
            state.saved_selections.remove(selected);
        }
        state.saved_selections[title] = mask;
        inks[title] = QJsonObject{{"color", color.name()}, {"solidity", solidity.value() / 100}};
        state.metadata["spotChannels"] = inks;
    });
}
void EditorController::show_document_color(bool proof) {
    auto* c = canvas();
    if (!c)
        return;
    auto config = editing::document_color(c->project().state());
    QDialog dialog(window_);
    dialog.setWindowTitle(proof ? tr("校样设置") : tr("文档颜色与精度"));
    QFormLayout form(&dialog);
    QComboBox mode, depth, profile;
    mode.setObjectName("documentColorMode");
    depth.setObjectName("documentColorDepth");
    profile.setObjectName("documentColorProfile");
    mode.addItems({"RGB", tr("灰度"), "Lab D50", "CMYK"});
    mode.setCurrentIndex(config.mode);
    for (int bits : {8, 16, 32})
        depth.addItem(QString::number(bits) + tr(" 位"), bits);
    depth.setCurrentIndex(depth.findData(config.bits));
    std::vector<QColorSpace> spaces{QColorSpace::SRgb, QColorSpace::AdobeRgb,
                                    QColorSpace::DisplayP3, QColorSpace::ProPhotoRgb};
    profile.addItems({"sRGB", "Adobe RGB", "Display P3", "ProPhoto RGB"});
    const auto select_profile = [&](const QColorSpace& value) {
        auto it = std::find(spaces.begin(), spaces.end(), value);
        if (it == spaces.end()) {
            spaces.push_back(value);
            profile.addItem(value.description().isEmpty() ? tr("自定义 ICC") : value.description());
        }
        profile.setCurrentIndex(
            int(std::find(spaces.begin(), spaces.end(), value) - spaces.begin()));
    };
    const auto saved_proof =
        QColorSpace::fromIccProfile(QSettings().value("color/proofIcc").toByteArray());
    select_profile(proof ? (c->proof_space.isValid()
                                ? c->proof_space
                                : (saved_proof.isValid() ? saved_proof : config.profile))
                         : config.profile);
    if (!proof) {
        form.addRow(tr("文档模式"), &mode);
        form.addRow(tr("合成精度"), &depth);
    }
    form.addRow(tr("ICC 配置文件"), &profile);
    QPushButton load(tr("载入 ICC…"));
    form.addRow(&load);
    QComboBox operation;
    operation.addItems({tr("转换：保留颜色外观"), tr("指定：重新解释现有颜色数值")});
    if (!proof)
        form.addRow(tr("操作"), &operation);
    QCheckBox enabled(tr("启用屏幕软校样")), gamut(tr("显示超出校样色域的颜色"));
    enabled.setChecked(c->proof_space.isValid());
    gamut.setChecked(c->gamut_warning);
    QColor warning = c->gamut_color;
    QPushButton warning_button(tr("色域警告颜色…"));
    if (proof) {
        form.addRow(&enabled);
        form.addRow(&gamut);
        form.addRow(&warning_button);
    }
    QLabel description(proof
                           ? tr("只影响屏幕预览，不改变图像、取样或导出。超出色域以 Lab ΔE76 大于 "
                                "2 的往返差值标记。")
                           : tr("图层内部保留线性 Rec.2020 FP32。文档模式和精度约束合成结果；8/16 "
                                "位会量化。CMYK 需要设备 ICC，当前为 8 位；Lab 使用 D50。"));
    description.setWordWrap(true);
    description.setMaximumWidth(480);
    form.addRow(&description);
    QLabel error;
    error.setWordWrap(true);
    form.addRow(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&mode, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        if (index == 3)
            depth.setCurrentIndex(0);
    });
    connect(&load, &QPushButton::clicked, &dialog, [&] {
        const auto path =
            QFileDialog::getOpenFileName(&dialog, tr("选择 ICC 配置文件"), {}, "ICC (*.icc *.icm)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
            error.setText(tr("ICC 文件不可读或超过 4 MB"));
            return;
        }
        const auto value = QColorSpace::fromIccProfile(file.readAll());
        if (!value.isValid() || !value.isValidTarget()) {
            error.setText(tr("ICC 不支持双向颜色转换"));
            return;
        }
        select_profile(value);
        error.clear();
    });
    connect(&warning_button, &QPushButton::clicked, &dialog, [&] {
        const auto color = QColorDialog::getColor(warning, &dialog);
        if (color.isValid())
            warning = color;
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!proof) {
                auto state = c->project().state();
                editing::set_document_color(state,
                                            {mode.currentIndex(), depth.currentData().toInt(),
                                             spaces[profile.currentIndex()]},
                                            operation.currentIndex() == 1);
            }
            dialog.accept();
        } catch (const std::exception& e) {
            error.setText(QString::fromUtf8(e.what()));
        }
    });
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (proof) {
        c->proof_space = enabled.isChecked() ? spaces[profile.currentIndex()] : QColorSpace();
        c->gamut_warning = gamut.isChecked();
        c->gamut_color = warning;
        c->refresh();
        QSettings().setValue("color/proofIcc", spaces[profile.currentIndex()].iccProfile());
        QSettings().setValue("color/gamutWarningColor", warning);
    } else
        c->edit(tr("文档颜色与精度"), [&](editing::State& state) {
            editing::set_document_color(
                state,
                {mode.currentIndex(), depth.currentData().toInt(), spaces[profile.currentIndex()]},
                operation.currentIndex() == 1);
        });
}
void EditorController::print_document() {
    auto* c = canvas();
    if (!c)
        return;
    c->commit_transform();
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(c->project().state().name);
    QPrintDialog dialog(&printer, window_);
    dialog.setWindowTitle(tr("打印（由打印机驱动管理颜色）"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto image =
        editing::display_image(editing::render(c->project().state()), QColorSpace::SRgb);
    QPainter painter;
    if (!painter.begin(&printer))
        throw editing::EditError(tr("无法启动打印任务"));
    const QRect page = printer.pageLayout().paintRectPixels(printer.resolution());
    const auto size = image.size().scaled(page.size(), Qt::KeepAspectRatio);
    painter.drawImage(QRect(page.center() - QPoint(size.width() / 2, size.height() / 2), size),
                      image);
    if (!painter.end())
        throw editing::EditError(tr("打印任务未正常结束，请检查打印机状态"));
}
} // namespace huelith::desktop
