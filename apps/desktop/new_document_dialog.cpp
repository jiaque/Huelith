#include "new_document_dialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace huelith::desktop {
NewDocumentDialog::NewDocumentDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("新建图片"));
    setWindowModality(Qt::ApplicationModal);
    setMinimumWidth(420);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    name_ = new QLineEdit(tr("未命名"), this);
    name_->setObjectName("documentName");
    form->addRow(tr("名称"), name_);
    width_ = new QSpinBox(this);
    height_ = new QSpinBox(this);
    for (auto* dimension : {width_, height_}) {
        dimension->setRange(1, max_canvas_dimension);
        dimension->setSuffix(tr(" 像素"));
    }
    width_->setValue(1920);
    height_->setValue(1080);
    form->addRow(tr("宽度"), width_);
    form->addRow(tr("高度"), height_);
    resolution_ = new QDoubleSpinBox(this);
    resolution_->setRange(1.0, 9600.0);
    resolution_->setValue(300.0);
    resolution_->setSuffix(" ppi");
    form->addRow(tr("分辨率"), resolution_);
    background_ = new QComboBox(this);
    background_->addItems({tr("透明"), tr("白色")});
    form->addRow(tr("背景"), background_);
    form->addRow(tr("工作色彩"), new QLabel(tr("线性 Rec.2020 · FP32"), this));
    error_ = new QLabel(this);
    error_->setAccessibleName(tr("输入提示"));
    layout->addWidget(error_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    create_ = buttons->button(QDialogButtonBox::Ok);
    create_->setText(tr("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this, [this] { update_validation(); });
    connect(width_, &QSpinBox::valueChanged, this, [this] { update_validation(); });
    connect(height_, &QSpinBox::valueChanged, this, [this] { update_validation(); });
    update_validation();
}

CanvasSpec NewDocumentDialog::specification() const {
    return {name_->text().trimmed().toUtf8().toStdString(),
            width_->value(),
            height_->value(),
            resolution_->value(),
            background_->currentIndex() == 0 ? CanvasBackground::transparent
                                             : CanvasBackground::white,
            WorkingSpace::linear_rec2020};
}

void NewDocumentDialog::update_validation() {
    const bool valid = !validate(specification());
    create_->setEnabled(valid);
    error_->setText(valid ? QString{} : tr("请输入名称；当前单张图片上限为 5.12 亿像素。"));
}
} // namespace huelith::desktop
