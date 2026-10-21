#pragma once
#include "color_band_slider.h"
#include <QHBoxLayout>
#include <QEvent>
#include <algorithm>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace huelith::desktop {
inline QPushButton* portrait_color_swatch(QWidget* parent, const QColor& color) {
    auto* button = new QPushButton(parent);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    QObject::connect(button,&QPushButton::toggled,button,[button](bool selected){button->setText(selected?QString::fromUtf8("✓"):QString{});});
    button->setFixedSize(36, 36);
    button->setAccessibleName(color.name());
    button->setToolTip(color.name());
    button->setStyleSheet(QString("QPushButton{background:%1;border:2px solid #40464c;border-radius:18px;}"
        "QPushButton:checked{border:3px solid #70b6ff;}QPushButton:focus{border:3px solid white;}").arg(color.name()));
    return button;
}
// Reserve the complete icon and text even with a larger UI font or DPI scale.
class PortraitStyleCard final : public QToolButton {
public:
    using QToolButton::QToolButton;
    QSize sizeHint() const override {
        const auto base=QToolButton::sizeHint();
        return base.expandedTo(QSize(std::max(iconSize().width(),fontMetrics().horizontalAdvance(text()))+16,
                                    iconSize().height()+fontMetrics().height()+20));
    }
    QSize minimumSizeHint() const override {return sizeHint();}
protected:
    bool event(QEvent* event) override {
        if(event->type()==QEvent::Show || event->type()==QEvent::FontChange || event->type()==QEvent::StyleChange){
            setMinimumSize(std::max(80,std::max(iconSize().width(),fontMetrics().horizontalAdvance(text()))+16),
                           std::max(80,iconSize().height()+fontMetrics().height()+20));
        }
        return QToolButton::event(event);
    }
};
inline QToolButton* portrait_style_card(QWidget* parent, const QString& label) {
    auto* button = new PortraitStyleCard(parent);
    button->setText(label);
    button->setAccessibleName(label);
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setMinimumSize(80, 80);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setIconSize({64, 44});
    button->setStyleSheet("QToolButton{border:2px solid transparent;border-radius:6px;padding:4px;}"
        "QToolButton:hover{background:#343d45;}QToolButton:checked{border-color:#70b6ff;background:#29465b;}");
    return button;
}
// Shared RAW-style row. Page code binds the value; it does not choose geometry.
struct PortraitIntegerRow {
    QWidget* host;
    ColorBandSlider* slider;
    QSpinBox* number;
};
inline PortraitIntegerRow portrait_integer_row(QWidget* parent, const QString& label,
                                               const QString& name, int low, int high,
                                               int value, const QString& suffix = {}) {
    auto* host = new QWidget(parent);
    host->setMinimumHeight(76);
    auto* column = new QVBoxLayout(host);
    column->setContentsMargins(0, 3, 0, 3);
    auto* heading = new QHBoxLayout;
    heading->addWidget(new QLabel(label, host));
    heading->addStretch();
    auto* number = new QSpinBox(host);
    number->setObjectName(name + "Value");
    number->setAccessibleName(label);
    number->setRange(low, high);
    number->setValue(value);
    number->setSuffix(suffix);
    number->setKeyboardTracking(false);
    number->setButtonSymbols(QAbstractSpinBox::NoButtons);
    number->setFixedSize(72, 26);
    heading->addWidget(number);
    column->addLayout(heading);
    auto* slider = new ColorBandSlider(-1, 0, host);
    slider->setObjectName(name);
    slider->setAccessibleName(label);
    slider->setFixedHeight(24);
    slider->set_colors(QColor("#69727c"), QColor("#69727c"));
    slider->setRange(low, high);
    slider->setValue(value);
    column->addWidget(slider);
    QObject::connect(slider, &QSlider::valueChanged, number, &QSpinBox::setValue);
    QObject::connect(number, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    return {host, slider, number};
}
} // namespace huelith::desktop
