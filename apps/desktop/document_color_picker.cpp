#include "document_color_picker.h"
#include "huelith/editing/document_color.h"
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace huelith::desktop {
QColor pick_document_color(QColor initial, const editing::State& state, QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("颜色"));
    QVBoxLayout layout(&dialog);
    QColorDialog picker(initial, &dialog);
    picker.setOptions(QColorDialog::DontUseNativeDialog | QColorDialog::NoButtons);
    picker.setWindowFlags(Qt::Widget);
    layout.addWidget(&picker);
    QComboBox model;
    model.addItems({"RGB", "HSB", "Lab D50", "CMYK (ICC)"});
    layout.addWidget(&model);
    QFormLayout form;
    layout.addLayout(&form);
    QDoubleSpinBox fields[4];
    QLabel labels[4];
    for (int i = 0; i < 4; ++i) {
        fields[i].setDecimals(2);
        fields[i].setKeyboardTracking(false);
        form.addRow(&labels[i], &fields[i]);
    }
    QLabel warning;
    warning.setWordWrap(true);
    layout.addWidget(&warning);
    QComboBox swatches;
    layout.addWidget(&swatches);
    QPushButton save(QObject::tr("添加到自定色板"));
    layout.addWidget(&save);
    const auto rebuild = [&] {
        swatches.clear();
        swatches.addItem(QObject::tr("自定色板"));
        for (const auto& value : QSettings().value("color/customSwatches").toStringList()) {
            QPixmap icon(24, 16);
            icon.fill(QColor(value));
            swatches.addItem(QIcon(icon), value, value);
        }
    };
    rebuild();
    QObject::connect(&swatches, &QComboBox::activated, &dialog, [&](int index) {
        if (index > 0)
            picker.setCurrentColor(QColor(swatches.itemData(index).toString()));
    });
    QObject::connect(&save, &QPushButton::clicked, &dialog, [&] {
        auto list = QSettings().value("color/customSwatches").toStringList();
        const auto value = picker.currentColor().name();
        list.removeAll(value);
        list.prepend(value);
        while (list.size() > 128)
            list.removeLast();
        QSettings().setValue("color/customSwatches", list);
        rebuild();
    });
    bool updating = false;
    const auto profile = editing::document_color(state).profile;
    const auto refresh = [&] {
        updating = true;
        const auto color = picker.currentColor();
        double values[4]{};
        QStringList names;
        double minima[4]{}, maxima[4]{255, 255, 255, 255};
        if (model.currentIndex() == 0) {
            names = {"R", "G", "B"};
            values[0] = color.red();
            values[1] = color.green();
            values[2] = color.blue();
        } else if (model.currentIndex() == 1) {
            names = {"H", "S", "B"};
            values[0] = std::max(0., double(color.hsvHueF()) * 360);
            values[1] = color.hsvSaturationF() * 100;
            values[2] = color.valueF() * 100;
            maxima[0] = 360;
            maxima[1] = maxima[2] = 100;
        } else if (model.currentIndex() == 2) {
            names = {"L*", "a*", "b*"};
            QImage sample(1, 1, QImage::Format_RGBA8888);
            sample.fill(color);
            sample.setColorSpace(QColorSpace::SRgb);
            sample = editing::to_working(sample);
            const auto* p = reinterpret_cast<const float*>(sample.constBits());
            const auto lab = editing::working_to_lab({p[0], p[1], p[2]});
            for (int i = 0; i < 3; ++i)
                values[i] = lab[i];
            maxima[0] = 100;
            minima[1] = minima[2] = -128;
            maxima[1] = maxima[2] = 127;
        } else {
            names = {"C %", "M %", "Y %", "K %"};
            for (auto& max : maxima)
                max = 100;
            if (profile.colorModel() == QColorSpace::ColorModel::Cmyk) {
                QImage sample(1, 1, QImage::Format_RGBA8888);
                sample.fill(color);
                sample.setColorSpace(QColorSpace::SRgb);
                sample = sample.convertedToColorSpace(profile, QImage::Format_CMYK8888);
                for (int i = 0; i < 4; ++i)
                    values[i] = sample.constBits()[i] * 100. / 255;
            }
        }
        const bool usable =
            model.currentIndex() != 3 || profile.colorModel() == QColorSpace::ColorModel::Cmyk;
        warning.setText(
            usable ? QString()
                   : QObject::tr(
                         "CMYK 数值使用文档的设备 ICC；请先在文档颜色设置中选择 CMYK 配置文件。"));
        for (int i = 0; i < 4; ++i) {
            form.setRowVisible(&fields[i], i < names.size());
            if (i < names.size())
                labels[i].setText(names[i]);
            fields[i].setRange(minima[i], maxima[i]);
            fields[i].setValue(values[i]);
            fields[i].setEnabled(usable);
        }
        updating = false;
    };
    for (auto& field : fields)
        QObject::connect(&field, &QDoubleSpinBox::valueChanged, &dialog, [&] {
            if (updating)
                return;
            QColor color;
            if (model.currentIndex() == 0)
                color = QColor(qRound(fields[0].value()), qRound(fields[1].value()),
                               qRound(fields[2].value()));
            else if (model.currentIndex() == 1)
                color = QColor::fromHsvF(std::fmod(fields[0].value(), 360.) / 360.,
                                         fields[1].value() / 100, fields[2].value() / 100);
            else if (model.currentIndex() == 2) {
                const auto rgb = editing::lab_to_working(
                    {fields[0].value(), fields[1].value(), fields[2].value()});
                QImage sample(1, 1, QImage::Format_RGBA32FPx4);
                sample.setColorSpace(editing::working_space());
                auto* p = reinterpret_cast<float*>(sample.bits());
                for (int i = 0; i < 3; ++i)
                    p[i] = float(rgb[i]);
                p[3] = 1;
                color = sample.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888)
                            .pixelColor(0, 0);
            } else {
                QImage sample(1, 1, QImage::Format_CMYK8888);
                sample.setColorSpace(profile);
                for (int i = 0; i < 4; ++i)
                    sample.bits()[i] = uchar(qRound(fields[i].value() * 255 / 100));
                color = sample.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888)
                            .pixelColor(0, 0);
            }
            picker.setCurrentColor(color);
        });
    QObject::connect(&picker, &QColorDialog::currentColorChanged, &dialog, [&] { refresh(); });
    QObject::connect(&model, &QComboBox::currentIndexChanged, &dialog, [&] { refresh(); });
    refresh();
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return dialog.exec() == QDialog::Accepted ? picker.currentColor() : QColor();
}
} // namespace huelith::desktop
