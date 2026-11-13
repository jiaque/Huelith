#include "new_document_dialog.h"
#include "expression_spin_box.h"
#include "huelith/editing/document_color.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QColorSpace>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
namespace {
double pixels_per_unit(int unit, double ppi) {
    return unit == 1 ? ppi / 25.4 : unit == 2 ? ppi / 2.54 : unit == 3 ? ppi : 1.0;
}
} // namespace
NewDocumentDialog::NewDocumentDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("新建图片"));
    setWindowModality(Qt::ApplicationModal);
    resize(800, 580);
    setMinimumSize(560, 420);
    auto* layout = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;
    layout->addLayout(body, 1);
    auto* browser = new QVBoxLayout;
    auto* categories = new QComboBox(this);
    categories->setObjectName("newDocumentCategory");
    categories->addItems({tr("最近"), tr("已保存"), tr("照片"), tr("印刷"), tr("屏幕")});
    browser->addWidget(categories);
    auto* presets = new QListWidget(this);
    presets->setObjectName("newDocumentPresets");
    browser->addWidget(presets, 1);
    auto* save = new QPushButton(tr("保存预设…"), this);
    auto* rename = new QPushButton(tr("重命名预设…"), this);
    auto* remove = new QPushButton(tr("删除所选记录"), this);
    save->setObjectName("saveDocumentPreset");
    rename->setObjectName("renameDocumentPreset");
    remove->setObjectName("removeDocumentPreset");
    browser->addWidget(save);
    browser->addWidget(rename);
    browser->addWidget(remove);
    body->addLayout(browser, 1);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* host = new QWidget(scroll);
    auto* form = new QFormLayout(host);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    scroll->setWidget(host);
    body->addWidget(scroll, 2);
    QSettings settings;
    name_ =
        new QLineEdit(tr("未命名-%1").arg(settings.value("newDocument/sequence", 1).toInt()), this);
    name_->setObjectName("documentName");
    form->addRow(tr("名称"), name_);
    units_ = new QComboBox(this);
    units_->setObjectName("documentUnit");
    units_->addItems({tr("像素"), tr("毫米"), tr("厘米"), tr("英寸")});
    form->addRow(tr("单位"), units_);
    width_ = new ExpressionSpinBox(this);
    height_ = new ExpressionSpinBox(this);
    width_->setObjectName("documentWidth");
    height_->setObjectName("documentHeight");
    for (auto* dimension : {width_, height_}) {
        dimension->setDecimals(8);
        dimension->setRange(0.0001, max_canvas_dimension);
    }
    width_->setValue(1920);
    height_->setValue(1080);
    form->addRow(tr("宽度"), width_);
    form->addRow(tr("高度"), height_);
    auto* orientation = new QPushButton(tr("交换宽高 / 横竖方向"), this);
    form->addRow(orientation);
    resolution_ = new QDoubleSpinBox(this);
    resolution_->setObjectName("documentResolution");
    resolution_->setRange(1, 9600);
    resolution_->setValue(300);
    resolution_->setSuffix(" ppi");
    form->addRow(tr("分辨率"), resolution_);
    background_ = new QComboBox(this);
    background_->setObjectName("documentBackground");
    background_->addItems({tr("透明"), tr("白色"), tr("自定义颜色")});
    form->addRow(tr("背景"), background_);
    auto* color = new QPushButton(tr("选择背景颜色…"), this);
    form->addRow(color);
    auto* clipboard = new QPushButton(tr("使用剪贴板图片尺寸"), this);
    form->addRow(clipboard);
    const auto update_clipboard = [clipboard] {
        clipboard->setEnabled(!QApplication::clipboard()->image().isNull());
    };
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, update_clipboard);
    update_clipboard();
    color_mode_ = new QComboBox(this);
    color_mode_->setObjectName("newColorMode");
    color_mode_->addItems({"RGB", tr("灰度"), "Lab D50", "CMYK"});
    form->addRow(tr("模式"), color_mode_);
    color_bits_ = new QComboBox(this);
    color_bits_->setObjectName("newColorBits");
    for (int bits : {8, 16, 32})
        color_bits_->addItem(QString::number(bits) + tr(" 位"), bits);
    color_bits_->setCurrentIndex(2);
    form->addRow(tr("合成精度"), color_bits_);
    color_profile_ = new QComboBox(this);
    color_profile_->setObjectName("newColorProfile");
    color_profile_->addItems({"sRGB", "Adobe RGB", "Display P3", "ProPhoto RGB"});
    form->addRow(tr("ICC"), color_profile_);
    auto* icc = new QPushButton(tr("载入 ICC…"), this);
    form->addRow(icc);
    connect(icc, &QPushButton::clicked, this, [this] {
        const auto path =
            QFileDialog::getOpenFileName(this, tr("ICC 配置文件"), {}, "ICC (*.icc *.icm)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
            QMessageBox::warning(this, tr("ICC"), tr("文件不可读或超过 4 MB"));
            return;
        }
        const auto bytes = file.readAll();
        const auto space = QColorSpace::fromIccProfile(bytes);
        if (!space.isValid() || !space.isValidTarget()) {
            QMessageBox::warning(this, tr("ICC"), tr("配置文件不支持双向转换"));
            return;
        }
        icc_profile_ = bytes;
        color_profile_->setEnabled(false);
        color_profile_->setToolTip(space.description());
        update_validation();
    });
    auto* reset_icc = new QPushButton(tr("使用内置 ICC"), this);
    form->addRow(reset_icc);
    connect(reset_icc, &QPushButton::clicked, this, [this] {
        icc_profile_.clear();
        color_profile_->setEnabled(true);
        update_validation();
    });
    connect(color_mode_, &QComboBox::currentIndexChanged, this, [this](int mode) {
        if (mode == 3)
            color_bits_->setCurrentIndex(0);
        update_validation();
    });
    connect(color_bits_, &QComboBox::currentIndexChanged, this, [this] { update_validation(); });
    form->addRow(
        new QLabel(tr("图层内部使用线性 Rec.2020 FP32，合成按所选模式和精度输出。"), this));
    artboard_ = new QCheckBox(tr("创建为画板"), this);
    artboard_->setObjectName("documentArtboard");
    form->addRow(artboard_);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    form->addRow(summary_);
    error_ = new QLabel(this);
    error_->setWordWrap(true);
    layout->addWidget(error_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    create_ = buttons->button(QDialogButtonBox::Ok);
    create_->setText(tr("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    const auto refresh_presets = [categories, presets, rename, remove] {
        presets->clear();
        const int category = categories->currentIndex();
        rename->setEnabled(category == 1);
        remove->setEnabled(category < 2);
        const auto append = [presets](QString title, double w, double h, int unit, double ppi) {
            QVariantMap data{{"name", title}, {"width", w}, {"height", h},
                             {"unit", unit},  {"ppi", ppi}, {"background", 0}};
            auto* item = new QListWidgetItem(title, presets);
            item->setData(Qt::UserRole, data);
        };
        if (category < 2) {
            const auto records =
                QSettings()
                    .value(category == 0 ? "newDocument/recent" : "newDocument/saved")
                    .toList();
            for (const auto& value : records) {
                const auto data = value.toMap();
                auto* item = new QListWidgetItem(data.value("name").toString(), presets);
                item->setData(Qt::UserRole, data);
            }
        } else if (category == 2) {
            append(QObject::tr("照片 4 × 6 英寸"), 4, 6, 3, 300);
            append(QObject::tr("照片 5 × 7 英寸"), 5, 7, 3, 300);
            append(QObject::tr("照片 8 × 10 英寸"), 8, 10, 3, 300);
        } else if (category == 3) {
            append("A3 · 297 × 420 mm", 297, 420, 1, 300);
            append("A4 · 210 × 297 mm", 210, 297, 1, 300);
            append("A5 · 148 × 210 mm", 148, 210, 1, 300);
        } else {
            append("1080p · 1920 × 1080", 1920, 1080, 0, 72);
            append("1440p · 2560 × 1440", 2560, 1440, 0, 72);
            append("4K · 3840 × 2160", 3840, 2160, 0, 72);
            append(QObject::tr("方图 · 1080 × 1080"), 1080, 1080, 0, 72);
            append(QObject::tr("竖图 · 1080 × 1920"), 1080, 1920, 0, 72);
        }
    };
    connect(categories, &QComboBox::currentIndexChanged, this, refresh_presets);
    connect(presets, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { load_preset(item->data(Qt::UserRole).toMap()); });
    connect(save, &QPushButton::clicked, this, [this, categories, refresh_presets] {
        bool ok = false;
        const auto title =
            QInputDialog::getText(this, tr("保存预设"), tr("预设名称"), QLineEdit::Normal, {}, &ok)
                .trimmed();
        if (!ok || title.isEmpty())
            return;
        QSettings store;
        auto records = store.value("newDocument/saved").toList();
        auto data = preset();
        data["name"] = title;
        records.append(data);
        store.setValue("newDocument/saved", records);
        categories->setCurrentIndex(1);
        refresh_presets();
    });
    connect(remove, &QPushButton::clicked, this, [categories, presets, refresh_presets] {
        if (categories->currentIndex() > 1 || presets->currentRow() < 0)
            return;
        const auto key =
            categories->currentIndex() == 0 ? "newDocument/recent" : "newDocument/saved";
        QSettings store;
        auto records = store.value(key).toList();
        records.removeAt(presets->currentRow());
        store.setValue(key, records);
        refresh_presets();
    });
    connect(rename, &QPushButton::clicked, this, [this, categories, presets, refresh_presets] {
        if (categories->currentIndex() != 1 || presets->currentRow() < 0)
            return;
        bool ok = false;
        const auto title =
            QInputDialog::getText(this, tr("重命名预设"), tr("名称"), QLineEdit::Normal,
                                  presets->currentItem()->text(), &ok)
                .trimmed();
        if (!ok || title.isEmpty())
            return;
        QSettings store;
        auto records = store.value("newDocument/saved").toList();
        auto data = records[presets->currentRow()].toMap();
        data["name"] = title;
        records[presets->currentRow()] = data;
        store.setValue("newDocument/saved", records);
        refresh_presets();
    });
    connect(units_, &QComboBox::currentIndexChanged, this, [this](int unit) {
        const double ratio = pixels_per_unit(previous_unit_, resolution_->value()) /
                             pixels_per_unit(unit, resolution_->value());
        const QSignalBlocker bw(width_), bh(height_);
        const double w = width_->value() * ratio, h = height_->value() * ratio;
        for (auto* field : {width_, height_})
            field->setMaximum(max_canvas_dimension / pixels_per_unit(unit, resolution_->value()));
        width_->setValue(w);
        height_->setValue(h);
        previous_unit_ = unit;
        update_validation();
    });
    connect(orientation, &QPushButton::clicked, this, [this] {
        const double w = width_->value();
        width_->setValue(height_->value());
        height_->setValue(w);
    });
    connect(color, &QPushButton::clicked, this, [this, color] {
        const auto chosen = QColorDialog::getColor(background_color_, this, tr("背景颜色"));
        if (chosen.isValid()) {
            background_color_ = chosen;
            background_->setCurrentIndex(2);
            color->setText(chosen.name());
        }
    });
    connect(clipboard, &QPushButton::clicked, this, [this] {
        const auto image = QApplication::clipboard()->image();
        if (image.isNull())
            return;
        units_->setCurrentIndex(0);
        width_->setValue(image.width());
        height_->setValue(image.height());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!width_->hasAcceptableInput() || !height_->hasAcceptableInput() ||
            validate(specification()))
            return;
        QSettings store;
        auto data = preset();
        auto records = store.value("newDocument/recent").toList();
        auto comparable = data;
        comparable.remove("name");
        for (int i = records.size() - 1; i >= 0; --i) {
            auto old = records[i].toMap();
            old.remove("name");
            if (old == comparable)
                records.removeAt(i);
        }
        records.prepend(data);
        while (records.size() > 20)
            records.removeLast();
        store.setValue("newDocument/recent", records);
        store.setValue("newDocument/last", data);
        store.setValue("newDocument/sequence", store.value("newDocument/sequence", 1).toInt() + 1);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_, &QLineEdit::textChanged, this, [this] { update_validation(); });
    for (auto* field : {width_, height_, resolution_})
        connect(field, &QDoubleSpinBox::valueChanged, this, [this] { update_validation(); });
    for (auto* field : {width_, height_})
        connect(field->findChild<QLineEdit*>(), &QLineEdit::textEdited, this,
                [this] { update_validation(); });
    for (auto* field : {color_mode_, color_bits_, color_profile_})
        connect(field, &QComboBox::currentIndexChanged, this, [this] { update_validation(); });
    const auto last = settings.value("newDocument/last").toMap();
    if (!last.isEmpty())
        load_preset(last);
    categories->setCurrentIndex(4);
    refresh_presets();
    update_validation();
}
QVariantMap NewDocumentDialog::preset() const {
    return {{"name", name_->text()},
            {"width", width_->value()},
            {"height", height_->value()},
            {"unit", units_->currentIndex()},
            {"ppi", resolution_->value()},
            {"background", background_->currentIndex()},
            {"artboard", artboard_->isChecked()},
            {"colorMode", color_mode_->currentIndex()},
            {"colorBits", color_bits_->currentIndex()},
            {"colorProfile", color_profile_->currentIndex()},
            {"icc", icc_profile_},
            {"color", background_color_.name()}};
}
void NewDocumentDialog::load_preset(const QVariantMap& record) {
    const QSignalBlocker block(units_);
    const int unit = std::clamp(record.value("unit", 0).toInt(), 0, 3);
    units_->setCurrentIndex(unit);
    previous_unit_ = unit;
    resolution_->setValue(record.value("ppi", 300).toDouble());
    for (auto* field : {width_, height_})
        field->setMaximum(max_canvas_dimension / pixels_per_unit(unit, resolution_->value()));
    width_->setValue(record.value("width", 1920).toDouble());
    height_->setValue(record.value("height", 1080).toDouble());
    background_->setCurrentIndex(std::clamp(record.value("background", 0).toInt(), 0, 2));
    artboard_->setChecked(record.value("artboard", false).toBool());
    color_mode_->setCurrentIndex(std::clamp(record.value("colorMode", 0).toInt(), 0, 3));
    color_bits_->setCurrentIndex(std::clamp(record.value("colorBits", 2).toInt(), 0, 2));
    color_profile_->setCurrentIndex(std::clamp(record.value("colorProfile", 0).toInt(), 0, 3));
    icc_profile_ = record.value("icc").toByteArray();
    color_profile_->setEnabled(icc_profile_.isEmpty());
    const QColor color(record.value("color", "#ffffff").toString());
    if (color.isValid())
        background_color_ = color;
    update_validation();
}
CanvasSpec NewDocumentDialog::specification() const {
    const double factor = pixels_per_unit(units_->currentIndex(), resolution_->value());
    return {name_->text().trimmed().toUtf8().toStdString(),
            int(std::llround(width_->value() * factor)),
            int(std::llround(height_->value() * factor)),
            resolution_->value(),
            static_cast<CanvasBackground>(background_->currentIndex()),
            WorkingSpace::linear_rec2020,
            background_color_.rgb() & 0xffffff,
            artboard_->isChecked(),
            color_mode_->currentIndex(),
            color_bits_->currentData().toInt(),
            color_profile_->currentIndex(),
            icc_profile_.toStdString()};
}
void NewDocumentDialog::update_validation() {
    for (auto* field : {width_, height_})
        static_cast<ExpressionSpinBox*>(field)->setUnitContext(
            resolution_->value(), pixels_per_unit(units_->currentIndex(), resolution_->value()),
            field->value());
    const auto spec = specification();
    bool valid = width_->hasAcceptableInput() && height_->hasAcceptableInput() && !validate(spec);
    QString color_error;
    try {
        const QColorSpace::NamedColorSpace profiles[]{QColorSpace::SRgb, QColorSpace::AdobeRgb,
                                                      QColorSpace::DisplayP3,
                                                      QColorSpace::ProPhotoRgb};
        editing::State temporary;
        editing::set_document_color(temporary, {spec.color_mode, spec.color_bits,
                                                icc_profile_.isEmpty()
                                                    ? QColorSpace(profiles[spec.color_profile])
                                                    : QColorSpace::fromIccProfile(icc_profile_)});
    } catch (const editing::EditError& e) {
        color_error = QString::fromUtf8(e.what());
        valid = false;
    }

    create_->setEnabled(valid);
    error_->setText(valid ? QString{} : tr("请输入名称和有效尺寸；单张图片上限为 5.12 亿像素。"));
    if (!color_error.isEmpty())
        error_->setText(color_error);
    summary_->setText(tr("%1 × %2 像素 · 基础像素内存约 %3 "
                         "MiB\n不含图层、历史与缓存。像素单位下调整 ppi 不改变像素尺寸。")
                          .arg(spec.width)
                          .arg(spec.height)
                          .arg(double(spec.width) * spec.height * 16 / (1024 * 1024), 0, 'f', 1));
}
}
