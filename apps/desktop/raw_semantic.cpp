#include "huelith/editing/raw_adjustments.h"
#include "raw_dialog.h"
#include "selection_refine_dialog.h"
#include "semantic_dialog.h"
#include "tone_panel.h"
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QTimer>
namespace huelith::desktop {
void RawDialog::refine_local_mask() {
    const int index = adjustment_->currentIndex() - 1;
    auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    if (index < 0 || index >= locals.size() || point_sample_preview_.isNull() || job_ ||
        commit_job_) {
        message_->setText(tr("请选择局部蒙版，并等待预览完成。"));
        return;
    }
    const auto geometry = sources_[current_].raw_recipe.value("geometry").toObject();
    auto local = locals[index].toObject();
    const auto mask = editing::raw_local_mask(point_sample_preview_.size(), local, geometry,
                                              point_sample_preview_);
    SelectionRefineDialog dialog(point_sample_preview_, mask, this, true);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto refined = dialog.result_images().mask;
    if (refined.width() > 1024 || refined.height() > 1024)
        refined = refined.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!refined.save(&buffer, "PNG")) {
        message_->setText(tr("无法保存细化后的蒙版"));
        return;
    }
    history_timer_->stop();
    record_history();
    local.insert("components",
                 QJsonArray{QJsonObject{{"mode", "bitmap"},
                                        {"name", tr("细化蒙版")},
                                        {"geometryAtCreation", geometry},
                                        {"png", QString::fromLatin1(bytes.toBase64())}}});
    local.insert("invert", false);
    locals[index] = local;
    sources_[current_].raw_recipe.insert("locals", locals);
    mask_component_ = 0;
    record_history();
    refresh_mask_components();
    display();
}
void RawDialog::semantic_local(int target) {
    if (point_sample_preview_.isNull() || job_ || commit_job_ ||
        property("previewPresented").toULongLong() != preview_revision_) {
        message_->setText(tr("请等待当前预览完成后识别范围。"));
        return;
    }
    auto locals = sources_[current_].raw_recipe.value("locals").toArray();
    if (locals.size() >= 64) {
        message_->setText(tr("当前最多支持 64 个局部调整"));
        return;
    }
    SemanticDialog dialog(point_sample_preview_, editing::SemanticTarget(target), this,
                          target != 3 && target != 6);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto mask = dialog.mask().scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!mask.save(&buffer, "PNG")) {
        message_->setText(tr("无法保存识别范围"));
        return;
    }
    history_timer_->stop();
    record_history();
    const QStringList names{tr("天空"), tr("草地"), tr("植被"), tr("人物"),
                            tr("主体"), tr("背景"), tr("对象")};
    const QJsonObject component{
        {"mode", "bitmap"},
        {"geometryAtCreation", sources_[current_].raw_recipe.value("geometry").toObject()},
        {"png", QString::fromLatin1(bytes.toBase64())},
        {"source", int(dialog.target()) >= 3 ? "maskrcnn-r50-v2" : "pp-mobileseg-base"}};
    locals.append(QJsonObject{{"name", names[int(dialog.target())]},
                              {"tone", editing::tone_json({})},
                              {"components", QJsonArray{component}}});
    sources_[current_].raw_recipe.insert("locals", locals);
    refresh_local_controls();
    adjustment_->setCurrentIndex(int(locals.size()));
    edit_mode_->setCurrentIndex(1);
    overlay_->setChecked(true);
    record_history();
    display();
}
} // namespace huelith::desktop
